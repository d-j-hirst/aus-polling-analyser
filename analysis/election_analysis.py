from election_code import ElectionCode
import copy
import math
import numpy
import statistics
import statsmodels.api as sm
from sklearn.linear_model import LinearRegression
from scipy.optimize import curve_fit
from scipy.interpolate import UnivariateSpline
from election_check import get_checked_elections
from poll_transform import transform_vote_share, detransform_vote_share, clamp
from sample_kurtosis import calc_rmse, one_tail_kurtosis, two_tail_kurtosis

ind_bucket_size = 2
fp_threshold = detransform_vote_share(int(math.floor(transform_vote_share(8)
    / ind_bucket_size)) * ind_bucket_size)


def create_bucket_template(bucket_info):
    buckets = {(-10000, bucket_info['min']): []}
    buckets.update({(a, a + bucket_info['interval']): 
                        [] for a in range(bucket_info['min'],
                        bucket_info['max'], bucket_info['interval'])})
    buckets.update({(bucket_info['max'], 10000): []})
    return buckets


def collect_election_data(elections, data_tuple, func, use_previous=True, use_others=False):
    d = data_tuple  # for brevity
    for this_election, this_results in elections.items():
        d['this_results'] = this_results
        if len(elections.next_elections(this_election)) == 0:
            continue
        d['next_election'] = elections.next_elections(this_election)[0]
        d['next_results'] = elections[d['next_election']]
        if use_previous:
            if len(elections.previous_elections(this_election)) > 0:
                previous_election = elections.previous_elections(this_election)[-1]
                d['previous_results'] = elections[previous_election]
            else:
                d['previous_results'] = None
        if 'party' in d:
            if use_others:
                next_others_percent = total_others_vote_share(d['next_results'])
                this_others_percent = total_others_vote_share(d['this_results'])
                # print(f'Next election {next_election.short()} others vote share: {next_others_percent}')
                # print(f'This election {this_election.short()} others vote share: {this_others_percent}')
                d['election_swing'] = (transform_vote_share(next_others_percent)
                            - transform_vote_share(this_others_percent))
            else:
                d['election_swing'] = (transform_vote_share(d['next_results'].total_fp_percentage_party(d['party']))
                    - transform_vote_share(d['this_results'].total_fp_percentage_party(d['party'])))
        for this_seat_name in d['this_results'].seat_names():
            d['this_seat_name'] = this_seat_name
            d['this_seat_results'] = d['this_results'].seat_by_name(this_seat_name)
            if len(d['this_seat_results'].tcp) == 0:
                continue  # ignore seats where candidates are unopposed
            if this_seat_name not in d['next_results'].seat_names():
                continue
            d['next_seat_results'] = d['next_results'].seat_by_name(this_seat_name)
            if len(d['next_seat_results'].tcp) == 0:
                continue  # ignore seats where candidates are unopposed
            func(d)


def perform_regression(d, bucket, results, input_names):
    # Run regression between the seat swing and election swing
    # to find the relationship between the two for initial primary
    # votes in this bucket
    inputs = [d[f'{name}_buckets'][bucket] for name in input_names]
    inputs_np = numpy.transpose(numpy.array(inputs))
    results_np = numpy.array(results)
    reg = LinearRegression().fit(inputs_np, results_np)
    for index, name in enumerate(input_names):
        d[f'{name}_coefficient'] = reg.coef_[index]
    d['overall_intercept'] = reg.intercept_

    # Get the residuals (~= errors if the above relationship is used
    # as a prediction), find the median, and split the errors into
    # a group above and below the median, measured by their distance
    # from the median
    d['residuals'] = [
        results[index] - (
            sum(d[f'{name}_coefficient'] * inputs[name_index][index]
                for name_index, name in enumerate(input_names))
            + d['overall_intercept']
        )
        for index in range(0, len(results))
    ]


def run_bucket_regressions(d, buckets, input_names):
    for bucket, results in d['result_buckets'].items():
        # Run regression between the seat swing and election swing
        # to find the relationship between the two for initial primary
        # votes in this bucket
        perform_regression(d, bucket, results, input_names)
        median_error = statistics.median(d['residuals'])
        lower_errors = [a - median_error for a in d['residuals'] if a < median_error]
        upper_errors = [a - median_error for a in d['residuals'] if a >= median_error]

        # Find effective RMSE and kurtosis for the two tails of the
        # distribution (in each case, as if the other side of the
        # distribution is symmetrical)
        lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                            / (len(lower_errors) - 1))
        upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                            / (len(upper_errors) - 1))
        lower_kurtosis = one_tail_kurtosis(lower_errors)
        upper_kurtosis = one_tail_kurtosis(upper_errors)

        buckets['counts'][bucket] = len(results)
        if 'swing_coefficient' in d:
            buckets['swing_coefficients'][bucket] = d['swing_coefficient']
        if 'sophomore_coefficient' in d:
            buckets['sophomore_coefficients'][bucket] = \
                d['sophomore_coefficient']
        buckets['intercepts'][bucket] = d['overall_intercept']
        buckets['median_errors'][bucket] = median_error
        buckets['offsets'][bucket] = d['overall_intercept'] + median_error
        buckets['lower_rmses'][bucket] = lower_rmse
        buckets['upper_rmses'][bucket] = upper_rmse
        buckets['lower_kurtoses'][bucket] = lower_kurtosis
        buckets['upper_kurtoses'][bucket] = upper_kurtosis

        if 'recontest_incumbent_buckets' in d:
            recontests = d['recontest_buckets'][bucket]
            incumbent_recontests = d['recontest_incumbent_buckets'][bucket]
            inputs_array = numpy.transpose(numpy.array([incumbent_recontests]))
            results_array = numpy.array(recontests)
            # print(inputs_array)
            # print(results_array)
            reg = LinearRegression().fit(inputs_array, results_array)
            incumbent_recontest_coefficient = reg.coef_[0]
            recontest_intercept = reg.intercept_
            buckets['recontest_incumbent_coefficients'][bucket] = incumbent_recontest_coefficient
            buckets['recontest_rates'][bucket] = recontest_intercept
        elif 'recontest_buckets' in d:
            buckets['recontest_rates'][bucket] = (
                d['recontest_buckets'][bucket].count(1) 
                / len(d['recontest_buckets'][bucket])
            )



def transfer_buckets(d, buckets, ordered_buckets, names):

    # For certain factors that might not appear *at all* in a given
    # bucket, use the values calculated for higher buckets
    for bucket_index in range(len(ordered_buckets) - 2, -1, -1):
        bucket = ordered_buckets[bucket_index]
        for name in names:
            if not 1 in d[f'{name}_buckets'][bucket]:
                next_bucket = ordered_buckets[bucket_index + 1]
                buckets[f'{name}_coefficients'][bucket] = \
                    buckets[f'{name}_coefficients'][next_bucket]


def create_bucket_centres(bucket_info):
    return list(range(int(bucket_info['min'] - bucket_info['interval'] / 2),
                      bucket_info['max'] + bucket_info['interval'],
                      bucket_info['interval']))


def create_smoothed_series(bucket_stats, bucket_centres, smoothing=10):
    coefficients = [a for a in bucket_stats.values()]
    spline = UnivariateSpline(x=bucket_centres,
                                y=coefficients,
                                s=smoothing)
    return spline(bucket_centres)


def smooth_buckets_and_save(d, buckets, bucket_info, to_smooth):
    bucket_centres = create_bucket_centres(bucket_info)

    smoothed = {
        name: [to_smooth[name] for _ in buckets['counts']]
        if to_smooth[name] is not True
        else create_smoothed_series(buckets[name], bucket_centres)
        for name in to_smooth
    }

    filename = (f'./Seat Statistics/statistics_{d["party_code"]}.csv')
    write_stat_lines(
        filename,
        [bucket_centres] + list(smoothed.values())
    )


def write_stat_lines(filename, stat_lines):
    with open(filename, 'w') as f:
        for stat_line in stat_lines:
            f.write(','.join([f'{a:.4f}' for a in stat_line]) + '\n')


def analyse_greens(elections):
    bucket_info = {'interval': 10, 'min': -90, 'max': -30}
    bucket_template = create_bucket_template(bucket_info)
    d = {
        'result_buckets': copy.deepcopy(bucket_template),
        'swing_buckets': copy.deepcopy(bucket_template),
        'sophomore_buckets': copy.deepcopy(bucket_template),
        'party': 'Greens',
        'party_code': 'GRN'
    }
    def func(d):
        sophomore = False
        if (d['previous_results'] is not None
            and d['this_seat_name'] in d['previous_results'].seat_names(include_name_changes=True)):
            previous_seat_results = \
                d['previous_results'].seat_by_name(d['this_seat_name'],
                                                include_name_changes=True)
            if (len(previous_seat_results.tcp) > 0 and
                previous_seat_results.tcp[0].party != d['party'] and
                d['this_seat_results'].tcp[0].party == d['party']):
                sophomore = True
        if d['party'] in [a.party for a in d['this_seat_results'].fp]:
            this_greens = sum(x.percent for x in d['this_seat_results'].fp
                                if x.party == d['party'])
        else:
            return
        if d['party'] in [a.party for a in d['next_seat_results'].fp]:
            next_greens = sum(x.percent for x in d['next_seat_results'].fp
                                if x.party == d['party'])
        else:
            return
        this_greens = transform_vote_share(this_greens)
        next_greens = transform_vote_share(next_greens)
        greens_change = next_greens - this_greens
        this_bucket = next(a for a in d['result_buckets']
                            if a[0] < this_greens
                            and a[1] > this_greens)
        d['result_buckets'][this_bucket].append(greens_change)
        d['swing_buckets'][this_bucket].append(d['election_swing'])
        d['sophomore_buckets'][this_bucket].append(1 if sophomore else 0)

    collect_election_data(elections, d, func)

    buckets = {name: {} for name in [
            'counts', 'swing_coefficients', 'sophomore_coefficients',
            'intercepts', 'median_errors', 'lower_rmses', 'upper_rmses',
            'lower_kurtoses', 'upper_kurtoses', 'offsets'
        ]
    }

    ordered_buckets = sorted(d['result_buckets'].keys(), key=lambda x: x[0])
    
    run_bucket_regressions(d, buckets, ['swing', 'sophomore'])

    transfer_buckets(d, buckets, ordered_buckets, ['sophomore'])

    to_smooth = {
        'swing_coefficients': True, 'sophomore_coefficients': True,
        'offsets': True, 'lower_rmses': True, 'upper_rmses': True,
        'lower_kurtoses': True, 'upper_kurtoses': True,
        'recontest_rates': 1, 'recontest_incumbent_rates': 1
    }

    smooth_buckets_and_save(d, buckets, bucket_info, to_smooth)


larger_parties = ['Labor', 'Liberal', 'Liberal National', 'Greens', 'Democrats', 'National', 'Nationals', 'One Nation', 'Country Liberal']


def effective_independent(party, election_results):
    if party == 'Independent':
        return True
    if party in larger_parties:
        return False
    if election_results.total_fp_percentage_party(party) > 3:
        return False
    return True


def effective_others(party, election_results, fp_percent):
    if fp_percent > fp_threshold:
        return False
    elif election_results.total_fp_percentage_party(party) > 3:
        return False
    elif party in larger_parties:
        return False
    return True


def total_others_vote_share(election_results):
    votes = sum(votes for party, votes in election_results.fp_by_party.items()
                if party not in larger_parties
                and (election_results.total_fp_percentage_party(party) < 3
                     or party == "Independent" and votes <= 8))
    return votes / election_results.total_fp_votes() * 100


def analyse_existing_independents(elections):
    # Note: This part of the analysis covers
    # "effective independents" (those who are technically
    # affiliated with a party but mainly dependent on their personal vote)
    bucket_info = {'interval': 15, 'min': -50, 'max': -5}
    bucket_template = create_bucket_template(bucket_info)
    d = {
        'result_buckets': copy.deepcopy(bucket_template),
        'sophomore_buckets': copy.deepcopy(bucket_template),
        'recontest_buckets': copy.deepcopy(bucket_template),
        'recontest_sophomore_buckets': copy.deepcopy(bucket_template),
        'recontest_incumbent_buckets': copy.deepcopy(bucket_template),
        'party_code': 'IND'
    }
    def func(d):
        independents = [a for a in d['this_seat_results'].fp
                        if a.percent > fp_threshold
                        and effective_independent(a.party,
                                                    d['this_results'])
                        ]
        if (len(independents) == 0):
            return
        # Only consider the highest polling independent from each seat
        highest = max(independents, key=lambda x: x.percent)
        # Only consider independents with above a certain primary vote
        if highest.percent < fp_threshold:
            return
        sophomore = False
        if (d['previous_results'] is not None
            and d['this_seat_name'] in d['previous_results'].seat_names(include_name_changes=True)):
            previous_seat_results = \
                d['previous_results'].seat_by_name(d['this_seat_name'],
                                                include_name_changes=True)
            # For independent sophomore effects, independents with a different name
            # should not be counted
            if (len(previous_seat_results.tcp) > 0
                and d['this_seat_results'].tcp[0].name == highest.name
                and (not effective_independent(previous_seat_results.tcp[0].party,
                                            d['previous_results'])
                        or previous_seat_results.tcp[0].name !=
                        d['this_seat_results'].tcp[0].name)):
                sophomore = True
        incumbent = (d['this_seat_results'].tcp[0].name == highest.name)
        this_fp = highest.percent
        this_fp = transform_vote_share(this_fp)
        this_bucket = next(a for a in d['result_buckets']
                            if a[0] < this_fp
                            and a[1] > this_fp)
        matching_next = [a for a in d['next_seat_results'].fp
                            if a.name == highest.name]
        if len(matching_next) > 0:
            next_fp = matching_next[0].percent
            d['recontest_buckets'][this_bucket].append(1)
            d['recontest_incumbent_buckets'][this_bucket].append(1 if incumbent else 0)
        else:
            d['recontest_buckets'][this_bucket].append(0)
            d['recontest_incumbent_buckets'][this_bucket].append(1 if incumbent else 0)
            return
        # print(f' Found independent for seat {this_seat_name}: {matching_next}')
        next_fp = transform_vote_share(next_fp)
        fp_change = next_fp - this_fp
        d['result_buckets'][this_bucket].append(fp_change)
        d['sophomore_buckets'][this_bucket].append(1 if sophomore else 0)

    collect_election_data(elections, d, func)

    buckets = {name: {} for name in [
            'counts', 'sophomore_coefficients',
            'intercepts', 'median_errors', 'lower_rmses', 'upper_rmses',
            'lower_kurtoses', 'upper_kurtoses', 'recontest_rates',
            'recontest_incumbent_coefficients', 'offsets'
        ]
    }

    ordered_buckets = sorted(d['result_buckets'].keys(), key=lambda x: x[0])
    
    run_bucket_regressions(d, buckets, ['sophomore'])

    transfer_buckets(d, buckets, ordered_buckets, ['sophomore', 'recontest_incumbent'])

    to_smooth = {
        'swing_coefficients': 0, 'sophomore_coefficients': True,
        'offsets': True, 'lower_rmses': True, 'upper_rmses': True,
        'lower_kurtoses': True, 'upper_kurtoses': True,
        'recontest_rates': True, 'recontest_incumbent_coefficients': True
    }

    smooth_buckets_and_save(d, buckets, bucket_info, to_smooth)


def load_seat_types():
    with open('Data/seat-types.csv', 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        seat_types = {(a[0], a[1]): int(a[2]) for a in linelists}
    return seat_types


def load_seat_regions():
    with open('Data/seat-regions.csv', 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        seat_regions = {(a[0], a[1]): a[2] for a in linelists}
    return seat_regions

def load_by_elections():
    with open('Data/by-elections.csv', 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        by_elections = {(int(a[7]), a[0]): float(a[3]) for a in linelists[1:]}
    return by_elections



def analyse_emerging_independents(elections, seat_types):
    d = {
        'seat_ind_count': [],
        'seat_fed': [],
        'seat_rural': [],
        'seat_provincial': [],
        'seat_outer_metro': [],
        'seat_rural': [],
        'seat_prev_others': [],
        'cand_fp_vote': [],
        'cand_fed': [],
        'cand_rural': [],
        'cand_provincial': [],
        'cand_outer_metro': [],
        'cand_prev_others': [],
        'party': 'Independent'
    }
    def func(d):
        old_names = [a.name for a in d['this_seat_results'].fp
                        if a.percent > fp_threshold
                        and effective_independent(a.party, d['this_results'])]
        new_independents = [a for a in d['next_seat_results'].fp
                            if effective_independent(a.party, d['next_results'])
                            and a.name not in old_names
                            and a.percent > fp_threshold]
        fed = 1 if d['next_election'].region() == 'fed' else 0
        this_others = sum([min(a.percent, fp_threshold) for a in d['this_seat_results'].fp
                            if a.party not in ['Labor', 'Liberal', 'Greens', 'National']])
        others_indicator = max(2, this_others)
        seat_type = seat_types.get((d['this_seat_name'], d['next_election'].region()), -1)
        d['seat_ind_count'].append(len(new_independents))
        d['seat_fed'].append(fed)
        d['seat_rural'].append(1 if seat_type == 3 else 0)
        d['seat_provincial'].append(1 if seat_type == 2 else 0)
        d['seat_outer_metro'].append(1 if seat_type == 1 else 0)
        d['seat_prev_others'].append(others_indicator)
        for candidate in new_independents:
            d['cand_fp_vote'].append(transform_vote_share(candidate.percent))
            d['cand_fed'].append(fed)
            d['cand_rural'].append(1 if seat_type == 3 else 0)
            d['cand_provincial'].append(1 if seat_type == 2 else 0)
            d['cand_outer_metro'].append(1 if seat_type == 1 else 0)
            d['cand_prev_others'].append(others_indicator)

    collect_election_data(elections, d, func, use_previous=False)

    input_types = ['fed', 'rural', 'provincial', 'outer_metro', 'prev_others']
    inputs_array = numpy.transpose(numpy.array(
        [d['seat_' + a] for a in input_types]))
    results_array = numpy.array(d['seat_ind_count'])
    reg = LinearRegression().fit(inputs_array, results_array)
    coefficient = {name: reg.coef_[a] for a, name in enumerate(input_types)}
    intercept = reg.intercept_

    fp_vote_buckets = {}
    for index in range(0, len(d['cand_fp_vote'])):
        fp_vote = d['cand_fp_vote'][index]
        fp_vote_bucket = int(math.floor(fp_vote / ind_bucket_size)) * ind_bucket_size
        if fp_vote_bucket in fp_vote_buckets:
            fp_vote_buckets[fp_vote_bucket] += 1
        else:
            fp_vote_buckets[fp_vote_bucket] = 1

    inputs_array = numpy.transpose(numpy.array(
        [d['cand_' + a] for a in input_types]))
    results_array = numpy.array(d['cand_fp_vote'])
    reg = LinearRegression().fit(inputs_array, results_array)
    vote_coefficient = {name: reg.coef_[a] for
                        a, name in enumerate(input_types)}
    vote_intercept = reg.intercept_

    deviations = [a - transform_vote_share(fp_threshold)
                  for a in d['cand_fp_vote']]
    upper_rmse = math.sqrt(sum([a ** 2 for a in deviations])
                           / (len(deviations) - 1))
    upper_kurtosis = one_tail_kurtosis(deviations)

    filename = (f'./Seat Statistics/statistics_emerging_IND.csv')
    with open(filename, 'w') as f:
        f.write(f'{fp_threshold}\n')
        f.write(f'{intercept}\n')
        for a in coefficient:
            f.write(f'{coefficient[a]}\n')
        f.write(f'{upper_rmse}\n')
        f.write(f'{upper_kurtosis}\n')
        for a in vote_coefficient:
            f.write(f'{vote_coefficient[a]}\n')
        f.write(f'{vote_intercept}\n')


def analyse_minors(elections, seat_types, seat_regions, settings):

    # First, collect all the calibration party's results in one place
    # (i.e. calib_results) and calulate per-candidate vote-shares
    # for each election

    # All election results in each seat arranged by region and then seat
    # name, with each result being a tuple of election year and vote share
    calib_results = {}
    # Summed vote share for each election (region, year)
    calib_election_votes = {}
    # Number of candidates for each election (region, year)
    calib_election_cands = {}
    for election, results in elections.items():
        region = election.region()
        year = election.year()
        if region not in calib_results:
            calib_results[region] = {}
        if (region, year) not in calib_election_votes:
            calib_election_votes[(region, year)] = 0
            calib_election_cands[(region, year)] = 0
        for seat_result in results.seat_results:
            for cand in [a for a in seat_result.fp 
                         if a.party == settings['calib_party']]:
                calib_percent = cand.percent
                calib_election_votes[(region, year)] += cand.percent
                calib_election_cands[(region, year)] += 1
                name = seat_result.name
                if name not in calib_results[election.region()]:
                    calib_results[region][name] = [(year, calib_percent)]
                else:
                    calib_results[region][name].append((year, calib_percent))
    # Average vote share *per candidate* for each election
    calib_election_average = {}
    for key, total in calib_election_votes.items():
        if calib_election_cands[key] == 0:
            continue
        calib_election_average[key] = total / calib_election_cands[key]

    # Now, calculate for each seat the average proportion between that seat's
    # ON vote share and the overall ON election share (per-seat)

    # Average vote share compared to average for each seat
    avg_mult_seat = {}
    for region_name, region_results in calib_results.items():
        for seat_name, seat_results in region_results.items():
            max_mult = 0
            min_mult = 100
            mult_sum = 0
            mult_count = 0
            for cand in seat_results:
                if cand[0] > settings['calib_cutoff']:
                    continue
                mult = cand[1] / calib_election_average[region_name, cand[0]]
                max_mult = max(max_mult, mult)
                min_mult = min(min_mult, mult)
                mult_sum += mult
                mult_count += 1
            if mult_count == 0:
                continue
            seat_id = (region_name, seat_name)
            avg_mult_seat[seat_id] = mult_sum / mult_count
    
    # Analyse variation not explained by the above seat-specific proportionality
    # list of differences between projected vote share and actual vote share
    # (under transformation)
    all_residuals = []
    for test_setting in settings['elections_seat_variability']:
        test_year = test_setting[0]
        test_region = test_setting[1]
        test_party = test_setting[2]

        test_election = elections[ElectionCode(test_year, test_region)]
        avg_mults = []
        vote_shares = []
        seat_names = []
        for seat_result in test_election.seat_results:
            seat_name = seat_result.name
            seat_id = (test_region, seat_name)
            if seat_id not in avg_mult_seat:
                continue
            if settings['avoid_seats'](seat_name, test_year):
                continue
            if seat_name == 'Fraser' and test_year < 2018:
                continue  # two federal Frasers, only use the latest one
            on_sum = sum([a.percent for a in seat_result.fp if a.party == test_party])
            avg_mult = avg_mult_seat[(test_region, seat_name)]
            if on_sum > 0:
                avg_mults.append(avg_mult)
                vote_shares.append(on_sum)
                seat_names.append(seat_name)

        use_intercepts = False

        inputs_array = numpy.transpose(numpy.array([avg_mults]))
        results_array = numpy.array(vote_shares)
        reg = LinearRegression(fit_intercept=use_intercepts).fit(inputs_array, results_array)
        coef = reg.coef_
        intercept = reg.intercept_
        residuals = [transform_vote_share(vote_shares[index]) -
                    transform_vote_share(coef * avg_mults[index] + intercept)
                    for index in range(0, len(vote_shares))]
        all_residuals += residuals

    # Calculate statistics for the variability in ON vote share
    # after accounting for the above calculated tendency
    # for some seats to have more ON vote share than others
    median_error = statistics.median(all_residuals)
    lower_errors = [a - median_error for a in all_residuals if a < median_error]
    upper_errors = [a - median_error for a in all_residuals if a >= median_error]
    lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                        / (len(lower_errors) - 1))
    upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                        / (len(upper_errors) - 1))
    lower_kurtosis = one_tail_kurtosis(lower_errors)
    upper_kurtosis = one_tail_kurtosis(upper_errors)

    # re-do the calculation of ON seat-specific multipliers
    # this time weighting recent results and recording seat characteristics
    # in order to create regressions for seats that don't have enough data
    avg_mult_seat = {}
    rural_seat = {}
    provincial_seat = {}
    outer_metro_seat = {}
    home_state_seat = {}
    for region_name, region_results in calib_results.items():
        for seat_name, seat_results in region_results.items():
            max_mult = 0
            min_mult = 100
            mult_sum = 0
            mult_count = 0
            for cand in seat_results:
                if cand[0] > 2003 and cand[0] < 2017:
                    continue
                mult = cand[1] / calib_election_average[region_name, cand[0]]
                max_mult = max(max_mult, mult)
                min_mult = min(min_mult, mult)
                mult_sum += mult
                mult_count += 1
                if cand[0] > 2016:  # weight recent years more
                    mult_sum += mult
                    mult_count += 1
            if mult_count == 0:
                continue
            seat_id = (seat_name, region_name)
            seat_type = seat_types.get((seat_name, region_name), -1)
            # Conveniently the home state of all right-populist parties is QLD
            is_home_state = (seat_regions.get((seat_name, region_name), '')
                             == settings['home_state'])
            avg_mult_seat[seat_id] = mult_sum / mult_count
            rural_seat[seat_id] = 1 if seat_type == 3 else 0
            provincial_seat[seat_id] = 1 if seat_type == 2 else 0
            outer_metro_seat[seat_id] = 1 if seat_type == 1 else 0
            home_state_seat[seat_id] = 1 if is_home_state else 0

    avg_mult_list = [avg_mult_seat[key] for key in sorted(avg_mult_seat.keys())]
    rural_list = [rural_seat[key] for key in sorted(avg_mult_seat.keys())]
    provincial_list = [provincial_seat[key] for key in sorted(avg_mult_seat.keys())]
    outer_metro_list = [outer_metro_seat[key] for key in sorted(avg_mult_seat.keys())]
    home_state_list = [home_state_seat[key] for key in sorted(avg_mult_seat.keys())]
    inputs_array = numpy.transpose(numpy.array([rural_list, provincial_list, outer_metro_list, home_state_list]))
    results_array = numpy.array(avg_mult_list)
    reg = LinearRegression().fit(inputs_array, results_array)
    rural_coefficient = reg.coef_[0]
    provincial_coefficient = reg.coef_[1]
    outer_metro_coefficient = reg.coef_[2]
    home_state_coefficient = reg.coef_[3]
    vote_intercept = reg.intercept_

    # Calculate estimated data for those seats that didn't have enough
    # existing results to determine a multiplier before
    for seat_id, type in seat_types.items():
        if seat_id not in avg_mult_seat:
            avg_mult_seat[seat_id] = vote_intercept
            if type == 3:
                avg_mult_seat[seat_id] += rural_coefficient
            if type == 2:
                avg_mult_seat[seat_id] += provincial_coefficient
            if type == 1:
                avg_mult_seat[seat_id] += outer_metro_coefficient

    # Save general populist party variability data
    filename = (f'./Seat Statistics/statistics_'
                f'{settings["file_identifier"]}.csv')
    with open(filename, 'w') as f:
        f.write(f'{lower_rmse}\n')
        f.write(f'{upper_rmse}\n')
        f.write(f'{lower_kurtosis}\n')
        f.write(f'{upper_kurtosis}\n')
        f.write(f'{home_state_coefficient}\n')

    # The home state coefficient gets automatically added to all
    # seats in the main program, so remove it from the seats in
    # that state to avoid double-counting it
    for key in avg_mult_seat.keys():
        if seat_regions.get((key[0], key[1]), '') == settings['home_state']:
            avg_mult_seat[key] -= home_state_coefficient

    # Save seat-specific populist vote share multipliers
    filename = (f'./Seat Statistics/modifiers_'
                f'{settings["file_identifier"]}.csv')
    with open(filename, 'w') as f:
        for key, value in avg_mult_seat.items():
            f.write(f'{key[0]},{key[1]},{value:.4f}\n')


def analyse_populist_minors(elections, seat_types, seat_regions):

    def avoid_seats(seat_name, test_year):
        if seat_name == 'Fairfax' and test_year == 2013:
            return True  # Clive Palmer standing, skews results
        if seat_name == 'Mirani' and test_year == 2020:
            return True  # One Nation incumbent, skews results
        return False

    settings = {
        # The calibration party is used to measure how much vote share a minor
        # is likely to get compared to the per-seat average in an election.
        'calib_party': 'One Nation',
        # Don't use elections past this date for the seat-specific calibration
        'calib_cutoff': 2003,
        # Elections to use for measuring seat variability
        # and the parties to use in each election
        'elections_seat_variability': [
            (2022, 'fed', 'One Nation'),
            (2022, 'fed', 'United Australia'),
            (2019, 'fed', 'One Nation'),
            (2019, 'fed', 'United Australia'),
            (2017, 'qld', 'One Nation'),
            (2020, 'qld', 'One Nation'),
            (2015, 'qld', 'United Australia'),
            (2013, 'fed', 'United Australia')
        ],
        'avoid_seats': avoid_seats,
        'file_identifier': 'populist',
        'home_state': 'qld'
    }

    analyse_minors(elections, seat_types, seat_regions, settings)


def analyse_centrist_minors(elections, seat_types, seat_regions):

    def avoid_seats(seat_name, test_year):
        return False

    settings = {
        # The calibration party is used to measure how much vote share a minor
        # is likely to get compared to the per-seat average in an election.
        'calib_party': 'Democrats',
        # Don't use elections past this date for the seat-specific calibration
        'calib_cutoff': 2002,
        # Elections to use for measuring seat variability
        # and the parties to use in each election
        'elections_seat_variability': [
            (1990, 'fed', 'Democrats'),
            (1993, 'fed', 'Democrats'),
            (1996, 'fed', 'Democrats'),
            (1998, 'fed', 'Democrats'),
            (2001, 'fed', 'Democrats')
        ],
        'avoid_seats': avoid_seats,
        'file_identifier': 'centrist',
        'home_state': 'sa'
    }

    analyse_minors(elections, seat_types, seat_regions, settings)


def analyse_others(elections):
    bucket_info = {'interval': 10, 'min': -90, 'max': -50}
    bucket_template = create_bucket_template(bucket_info)
    d = {
        'result_buckets': copy.deepcopy(bucket_template),
        'swing_buckets': copy.deepcopy(bucket_template),
        'recontest_buckets': copy.deepcopy(bucket_template),
        'party': 'Others',
        'party_code': 'OTH'
    }
    def collection_func(d):
        this_others = sum(a.percent for a in d['this_seat_results'].fp
                            if effective_others(a.party,
                                                d['this_results'],
                                                a.percent))
        next_others = sum(a.percent for a in d['next_seat_results'].fp
                            if effective_others(a.party,
                                                d['next_results'],
                                                a.percent))
        # Sometimes a seat won't have any "others" candidate at all,
        # or only a very poorly polling one, have a minimum floor
        # on the effective others vote to avoid this having a
        # disproportionate effect on results
        this_others = max(2, this_others)
        this_others = transform_vote_share(this_others)
        this_bucket = next(a for a in d['result_buckets']
                            if a[0] < this_others
                            and a[1] > this_others)
        if next_others > 0:
            d['recontest_buckets'][this_bucket].append(1)
        else:
            d['recontest_buckets'][this_bucket].append(0)
            return
        next_others = transform_vote_share(next_others)
        others_change = next_others - this_others
        d['result_buckets'][this_bucket].append(others_change)
        d['swing_buckets'][this_bucket].append(d['election_swing'])
    
    collect_election_data(elections, d, collection_func, use_others=True)

    buckets = {name: {} for name in [
            'counts', 'swing_coefficients', 'intercepts', 'median_errors',
            'lower_rmses', 'upper_rmses', 'lower_kurtoses', 'upper_kurtoses',
            'recontest_rates', 'offsets'
        ]
    }

    run_bucket_regressions(d, buckets, ['swing'])

    to_smooth = {
        'swing_coefficients': True, 'sophomore_coefficients': 0,
        'offsets': True, 'lower_rmses': True, 'upper_rmses': True,
        'lower_kurtoses': True, 'upper_kurtoses': True,
        'recontest_rates': True, 'recontest_incumbent_rates': 1
    }

    smooth_buckets_and_save(d, buckets, bucket_info, to_smooth)


def analyse_emerging_parties(elections):
    election_count = 0
    party_count = 0
    vote_shares = []
    fp_threshold = 3
    for this_election, this_results in elections.items():
        if len(elections.previous_elections(this_election)) > 0:
            previous_election = elections.previous_elections(this_election)[-1]
            previous_results = elections[previous_election]
            election_count += 1
            emerged_vote = 0
            for party in this_results.fp_by_party.keys():
                if party == "Independent" or (party in larger_parties and party != 'One Nation'):
                    continue
                vote = this_results.total_fp_percentage_party(party)
                if vote > fp_threshold:
                    if party in previous_results.fp_by_party:
                        if previous_results.total_fp_percentage_party(party) < fp_threshold:
                            emerged_vote += vote
                            # print(f'{this_election} {party} {vote}')
                    else:
                        emerged_vote += vote
                        # print(f'{this_election} {party} {vote}')
            if emerged_vote > 0:
                party_count += 1
                vote_shares.append(transform_vote_share(emerged_vote))
    emergence_rate = party_count / election_count

    residuals = [a - transform_vote_share(fp_threshold) for a in vote_shares]

    # one-tailed RMSE and kurtosis equivalent
    rmse = math.sqrt(sum([a ** 2 for a in residuals])
                        / (len(residuals) - 1))
    kurtosis = one_tail_kurtosis(residuals)

    filename = f'./Seat Statistics/statistics_emerging_party.csv'
    with open(filename, 'w') as f:
        f.write(f'{fp_threshold}\n')
        f.write(f'{emergence_rate}\n')
        f.write(f'{rmse}\n')
        f.write(f'{kurtosis}\n')


class RegionPolls:
    def __init__(self):
        self.prev_tpp = 50
        self.next_tpp = 50
        self.next_deviation = 50
        self.polls = []
        self.deviations = []
        self.population = 0


    def __repr__(self):
        return (f'prev_tpp: {self.prev_tpp}, next_tpp: {self.next_tpp}, '
                f'next_deviation: {self.next_deviation}, '
                f'population: {self.population}, '
                f'polls: {self.polls}, deviations: {self.deviations}')


def regress_and_write_to_file(f, inputs, outputs, region):
    inputs_array = numpy.transpose(numpy.array([inputs]))
    results_array = numpy.array(outputs)
    reg = LinearRegression().fit(inputs_array, results_array)
    residuals = [outputs[a] -
                 (reg.coef_[0] * inputs[a] + reg.intercept_)
                 for a in range(0,len(outputs))]
    rmse = math.sqrt(sum([a ** 2 for a in residuals])
                        / (len(residuals) - 1))
    kurtosis = one_tail_kurtosis(residuals)
    f.write(f'{region},{reg.coef_[0]},{reg.intercept_},{rmse},{kurtosis}\n')
    return reg


def analyse_region_swings():
    target_year = 2025
    election_results = {}
    state_results = {}
    filename = './Data/tpp-fed-regions.csv'
    with open(filename, 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        for a in linelists:
            code = ElectionCode(a[0], a[1])
            results = (float(a[3]), float(a[4]))
            if a[2] == 'all':
                election_results[code] = results
            else:
                state_results[(code, a[2])] = results

    poll_lists = {}
    highest_poll_number = 0

    filename = './Data/region-polls-fed.csv'
    with open(filename, 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        for a in linelists:
            code = ElectionCode(a[0], a[1])
            region = a[2]
            region_polls = RegionPolls()
            region_polls.prev_tpp = float(a[3])
            region_polls.next_tpp = float(a[4])
            region_polls.population = int(a[5])
            for i in range(6, len(a)):
                region_polls.polls.append(float(a[i]))
            if code not in poll_lists:
                poll_lists[code] = {}
            poll_lists[code][region] = region_polls

    next_overall_tpps = {}
    prev_overall_tpps = {}
    poll_overall_tpps = {}
    for election, poll_list in poll_lists.items():
        total_population = sum([i.population for i in poll_list.values()])
        next_tpp_overall = sum([i.next_tpp * i.population / total_population
                                for i in poll_list.values()])
        prev_tpp_overall = sum([i.prev_tpp * i.population / total_population
                                for i in poll_list.values()])
        next_swing_overall = next_tpp_overall - prev_tpp_overall
        overall_tpps = []
        for j in range(0, len(next(iter(poll_list.values())).polls)):
            overall_tpps.append(sum([i.polls[j] * i.population / total_population
                                for i in poll_list.values()]))
        next_overall_tpps[election] = next_tpp_overall
        prev_overall_tpps[election] = prev_tpp_overall
        poll_overall_tpps[election] = overall_tpps
        for region, polls in poll_lists[election].items():
            polls.next_deviation = (polls.next_tpp - polls.prev_tpp
                                    - next_swing_overall)
            highest_poll_number = max(highest_poll_number, len(polls.polls))
            for i in range(0, len(polls.polls)):
                polls.deviations.append((polls.polls[i] - polls.prev_tpp
                                         - overall_tpps[i] + prev_tpp_overall))


    fed_swings = {}
    state_swings = {}
    weights = {}
    for election, result in state_results.items():
        if election[0].year() >= target_year:
            continue
        if target_year - election[0].year() > 25:
            continue
        if election[1] not in fed_swings:
            fed_swings[election[1]] = []
            state_swings[election[1]] = []
        fed_swings[election[1]].append(election_results[election[0]][1])
        state_swings[election[1]].append(result[1])
        # swing_deviation = result[1] - election_results[election[0]][1]
        # print(f'{election}, {result}, {swing_deviation}')

    naive_coefficients = {}
    naive_intercepts = {}

    output_filename = (f'./Regional/{target_year}fed-regions-base.csv')
    with open(output_filename, 'w') as f:
        for key in fed_swings.keys():
            reg = regress_and_write_to_file(f, fed_swings[key], state_swings[key], key)
            naive_coefficients[key] = float(reg.coef_[0])
            naive_intercepts[key] = float(reg.intercept_)
        # Note: following relies on ordered dicts (python 3.6+)
        flat_fed_swings = [a for sublist in fed_swings.values() for a in sublist]
        flat_state_swings = [a for sublist in state_swings.values() for a in sublist]
        regress_and_write_to_file(f, flat_fed_swings, flat_state_swings, 'all')

    best_mix_factors = []
    best_rmses = []
    best_kurtoses = []
    region_errors = {'all': []}
    for poll_number in range(0, 9):
        poll_deviations = {}
        next_deviations = {}
        for election, election_polls in poll_lists.items():
            for region, region_polls in election_polls.items():
                if poll_number >= len(region_polls.deviations):
                    continue
                if region not in poll_deviations:
                    poll_deviations[region] = []
                    next_deviations[region] = []
                poll_deviations[region].append(region_polls.deviations[poll_number])
                next_deviations[region].append(region_polls.next_deviation)

        if len(poll_deviations[region]) < 2:
            break

        if poll_number == 0:
            polled_coefficients = {}
            polled_intercepts = {}
            output_filename = (f'./Regional/{target_year}fed-regions-polled.csv')
            with open(output_filename, 'w') as f:
                for region in poll_deviations.keys():
                    reg = regress_and_write_to_file(f, poll_deviations[region], next_deviations[region], region)
                    polled_coefficients[region] = float(reg.coef_[0])
                    polled_intercepts[region] = float(reg.intercept_)
                # Note: following relies on ordered dicts (python 3.6+)
                flat_poll_dev = [a for sublist in poll_deviations.values() for a in sublist]
                flat_actual_dev = [a for sublist in next_deviations.values() for a in sublist]
                region = 'all'
                reg = regress_and_write_to_file(f, flat_poll_dev, flat_actual_dev, region)
                polled_coefficients[region] = float(reg.coef_[0])
                polled_intercepts[region] = float(reg.intercept_)

        mixed_rmses = {}
        mixed_kurtoses = {}
        for mix_factor in [a / 100 for a in range(1, 101)]:
            mixed_errors = []

            for election, poll_overall_tpp in poll_overall_tpps.items():
                for region in poll_deviations.keys():
                    if poll_number >= len(poll_overall_tpp):
                        continue
                    polled_coefficient = (polled_coefficients[region] * 0.5 +
                                         polled_coefficients['all'] * 0.5)
                    polled_intercept = (polled_intercepts[region] * 0.5 +
                                       polled_intercepts['all'] * 0.5)
                    poll_overall_swing = poll_overall_tpp[poll_number] - prev_overall_tpps[election]
                    polled_region_swing = (poll_lists[election][region].polls[poll_number]
                                                - poll_lists[election][region].prev_tpp)
                    polled_raw_deviation = polled_region_swing - poll_overall_swing
                    naive_region_swing = (naive_coefficients[region] *
                                        poll_overall_swing +
                                        naive_intercepts[region])
                    naive_deviation = naive_region_swing - poll_overall_swing
                    polled_final_deviation = (polled_coefficient *
                                            polled_raw_deviation +
                                            polled_intercept)
                    actual_overall_swing = next_overall_tpps[election] - prev_overall_tpps[election]
                    actual_region_swing = (poll_lists[election][region].next_tpp -
                                           poll_lists[election][region].prev_tpp)
                    actual_deviation = actual_region_swing - actual_overall_swing
                    mixed_deviation = (polled_final_deviation * mix_factor +
                                    naive_deviation * (1 - mix_factor))
                    mixed_error = mixed_deviation - actual_deviation
                    mixed_errors.append(mixed_error)
                    if region not in region_errors:
                        region_errors[region] = []
                    region_errors[region].append(mixed_error)
                    region_errors['all'].append(mixed_error)

            mixed_rmse = calc_rmse(mixed_errors)
            mixed_rmses[mix_factor] = mixed_rmse
            mixed_kurtosis = one_tail_kurtosis(mixed_errors)
            mixed_kurtoses[mix_factor] = mixed_kurtosis

        best_mix_factor = min(mixed_rmses, key=mixed_rmses.get)
        best_mix_factors.append(best_mix_factor)
        best_rmses.append(mixed_rmses[best_mix_factor])
        best_kurtoses.append(mixed_kurtoses[best_mix_factor])

    all_rmse = calc_rmse(region_errors['all'])
    output_filename = (f'./Regional/{target_year}fed-mix-regions.csv')
    with open(output_filename, 'w') as f:
        for region, error_list in region_errors.items():
            if region == 'all':
                continue
            region_bias = statistics.median(error_list)
            region_rmse = calc_rmse(error_list, region_bias)
            rmse_modifier = region_rmse / all_rmse
            # To account for small sample size, which may result in
            # rmse factors being underestimated, increase the rmse
            # factor a bit for a region when it's under the overall average
            if rmse_modifier < 1: rmse_modifier = rmse_modifier * 0.5 + 0.5
            f.write(f'{region},{region_bias},{rmse_modifier}\n')


    def func(x, a, b):
        return a*numpy.exp(-b*x)

    def func2(x, a, b, c):
        return a*numpy.exp(-b*x)+c

    def func3(x, a, b):
        return a*x+b

    # In order to avoid too sharp a change after the first timepoint,
    # add a dummy value linearly interpolated between the two so that
    # smooths down more clearly
    dummy_time = 0.25
    x_list = list(range(0, len(best_mix_factors))) + [dummy_time]
    x = numpy.array(x_list)

    mix_factor_dummy = (best_mix_factors[0] * (1 - dummy_time) +
                        best_mix_factors[1] * dummy_time)
    y = numpy.array(best_mix_factors + [mix_factor_dummy])
    mix_factor_params, mix_factor_cov = curve_fit(func, x, y, [1, 1])

    rmse_dummy = (best_rmses[0] * (1 - dummy_time) +
                  best_rmses[1] * dummy_time)
    y = numpy.array(best_rmses + [rmse_dummy])
    rmse_params, rmse_cov = curve_fit(func2, x, y, [-1, 0.1, 2.5])

    kurtosis_dummy = (best_kurtoses[0] * (1 - dummy_time) +
                      best_kurtoses[1] * dummy_time)
    y = numpy.array(best_kurtoses + [kurtosis_dummy])
    kurtosis_params, kurtosis_cov = curve_fit(func3, x, y, [2, 1])

    output_filename = (f'./Regional/{target_year}fed-mix-parameters.csv')
    with open(output_filename, 'w') as f:
        f.write(f'mix_factor,{mix_factor_params[0]},{mix_factor_params[1]}\n')
        f.write(f'rmse,{rmse_params[0]},{rmse_params[1]},{rmse_params[2]}\n')
        f.write(f'kurtosis,{kurtosis_params[0]},{kurtosis_params[1]}\n')


majors = ['Liberal', 'National', 'Liberal National', 'Labor', 'Country Liberal']


def analyse_seat_swings(elections, seat_types, seat_regions, by_elections):
    alp_swings = {}
    federals = {}
    margins = {}
    incumbent_retirement_urbans = {}
    incumbent_retirement_regionals = {}
    sophomore_candidate_urbans = {}
    sophomore_candidate_regionals = {}
    sophomore_party_urbans = {}
    sophomore_party_regionals = {}
    previous_swings = {}
    by_election_swings = {}
    names = {}
    for this_election, this_results in elections.items():
        previous_elections = elections.previous_elections(this_election)
        if len(previous_elections) > 0:
            previous_election = previous_elections[-1]
            previous_results = elections[previous_election]
        else:
            previous_results = None
        if len(previous_elections) > 1:
            old_election = previous_elections[-2]
            old_results = elections[old_election]
        else:
            old_results = None
        for this_seat_name in this_results.seat_names():
            # Fraser appears federally in both VIC and ACT (both safe seats)
            # since VIC is more recent, pretend the ACT results don't exist
            if this_seat_name == "Fraser" and this_election.year() < 2019:
                continue
            # Northern Territory is the only seat in its region,
            # so we can't analyse how it compares to its state
            if this_seat_name == "Northern Territory":
                continue
            this_seat_result = this_results.seat_by_name(this_seat_name)
            # These automatically gives None if no seat is found
            previous_seat_result = (previous_results.seat_by_name(
                this_seat_name,
                include_name_changes=True
            ) if previous_results is not None else None)
            old_seat_result = (old_results.seat_by_name(this_seat_name,
                               include_name_changes=True)
                               if old_results is not None else None)
            # Check seat has a classic 2cp swing
            if len(this_seat_result.tcp) != 2:
                continue
            if this_seat_result.tcp[0].party not in majors:
                continue
            if this_seat_result.tcp[1].party not in majors:
                continue
            if 'Labor' not in (this_seat_result.tcp[0].party,
                                this_seat_result.tcp[1].party):
                continue
            if this_seat_result.tcp[0].swing is None:
                continue

            # Check previous results has a classic 2cp swing
            if previous_seat_result is not None:
                if len(previous_seat_result.tcp) != 2:
                    previous_seat_result = None
                elif previous_seat_result.tcp[0].party not in majors:
                    previous_seat_result = None
                elif previous_seat_result.tcp[1].party not in majors:
                    previous_seat_result = None
                elif 'Labor' not in (previous_seat_result.tcp[0].party,
                                    previous_seat_result.tcp[1].party):
                    previous_seat_result = None
                elif previous_seat_result.tcp[0].swing is None:
                    previous_seat_result = None

            # Check old results has a classic 2cp swing
            if old_seat_result is not None:
                if len(old_seat_result.tcp) != 2:
                    old_seat_result = None
                elif old_seat_result.tcp[0].party not in majors:
                    old_seat_result = None
                elif old_seat_result.tcp[1].party not in majors:
                    old_seat_result = None
                elif 'Labor' not in (old_seat_result.tcp[0].party,
                                    old_seat_result.tcp[1].party):
                    old_seat_result = None
                elif old_seat_result.tcp[0].swing is None:
                    old_seat_result = None

            if (this_seat_name, this_election.region()) in seat_regions:
                this_seat_region = seat_regions[(this_seat_name, this_election.region())]
            else:
                this_seat_region = 'none'
            if this_election not in alp_swings:
                alp_swings[this_election] = {}
                federals[this_election] = {}
                margins[this_election] = {}
                incumbent_retirement_urbans[this_election] = {}
                incumbent_retirement_regionals[this_election] = {}
                sophomore_candidate_urbans[this_election] = {}
                sophomore_candidate_regionals[this_election] = {}
                sophomore_party_urbans[this_election] = {}
                sophomore_party_regionals[this_election] = {}
                previous_swings[this_election] = {}
                by_election_swings[this_election] = {}
                names[this_election] = {}
            if this_seat_region not in alp_swings[this_election]:
                alp_swings[this_election][this_seat_region] = []
                federals[this_election][this_seat_region] = []
                margins[this_election][this_seat_region] = []
                incumbent_retirement_urbans[this_election][this_seat_region] = []
                incumbent_retirement_regionals[this_election][this_seat_region] = []
                sophomore_candidate_urbans[this_election][this_seat_region] = []
                sophomore_candidate_regionals[this_election][this_seat_region] = []
                sophomore_party_urbans[this_election][this_seat_region] = []
                sophomore_party_regionals[this_election][this_seat_region] = []
                previous_swings[this_election][this_seat_region] = []
                by_election_swings[this_election][this_seat_region] = []
                names[this_election][this_seat_region] = []

            temp_incumbent_retirement = 0
            if previous_seat_result is not None and len(previous_seat_result.tcp) == 2:
                previous_winner_name = previous_seat_result.tcp[0].name
                if len([a for a in this_seat_result.fp if a.name == previous_winner_name]) == 0:
                    temp_incumbent_retirement = 1 if previous_seat_result.tcp[0].party == 'Labor' else -1

            temp_sophomore_candidate = 0
            if (previous_seat_result is not None and old_seat_result is not None
                 and len(previous_seat_result.tcp) == 2 and len(old_seat_result.tcp)) == 2:
                old_winner_name = old_seat_result.tcp[0].name
                previous_winner_name = previous_seat_result.tcp[0].name
                if old_winner_name != previous_winner_name:
                    if len([a for a in this_seat_result.fp if a.name == previous_winner_name]) != 0:
                        temp_sophomore_candidate = 1 if previous_seat_result.tcp[0].party == 'Labor' else -1

            temp_sophomore_party = 0
            if (previous_seat_result is not None and old_seat_result is not None
                 and len(previous_seat_result.tcp) == 2 and len(old_seat_result.tcp)) == 2:
                old_winner_party = (old_seat_result.tcp[0].party == 'Labor')
                previous_winner_party = (previous_seat_result.tcp[0].party == 'Labor')
                if old_winner_party != previous_winner_party:
                    temp_sophomore_party = 1 if previous_seat_result.tcp[0].party == 'Labor' else -1

            temp_previous_swing = None
            if previous_seat_result is not None and len(previous_seat_result.tcp) == 2:
                if previous_seat_result.tcp[0].swing is not None:
                    temp_previous_swing = previous_seat_result.tcp[0].swing
                    if previous_seat_result.tcp[0].party != 'Labor':
                        temp_previous_swing *= -1

            temp_by_election_swing = 0
            by_election_tag = (this_election.year(), this_seat_name)
            if by_election_tag in by_elections:
                temp_by_election_swing = by_elections[by_election_tag]

            # This code is here to look for movements to and from
            # three-cornered ALP/Lib/Nat contests. It didn't find a strong
            # enough connection to be worth using for now, except perhaps
            # in NSW.

            # temp_lnp_contest = 0
            # if previous_seat_result is not None:
            #     # exclude these states as the dynamics of three-cornered contests is different under OPV
            #     if this_election.region() != 'qld' and this_election.region() != 'nsw':
            #         if ('National' in (a.party for a in this_seat_result.fp)
            #                 and 'Liberal' in (a.party for a in this_seat_result.fp)):
            #             if ('National' not in (a.party for a in previous_seat_result.fp)
            #                 or 'Liberal' not in (a.party for a in previous_seat_result.fp)):
            #                 if this_election.region() != 'fed': continue
            #                 third_party_result = min([a.percent for a in this_seat_result.fp
            #                     if a.party == "Liberal" or a.party == "National"])
            #                 print(f'New LNP contest in {this_seat_name} for election {this_election}, lower fp vote {third_party_result}')
            #                 temp_lnp_contest = third_party_result


            alp_swing = (this_seat_result.tcp[0].swing
                         if this_seat_result.tcp[0].party == 'Labor'
                         else -this_seat_result.tcp[0].swing)
            alp_swings[this_election][this_seat_region].append(alp_swing)
            federals[this_election][this_seat_region].append(1 if this_election.region() == "fed" else 0)
            margins[this_election][this_seat_region].append(abs(this_seat_result.tcp[0].percent - 50))
            incumbent_retirement_urbans[this_election][this_seat_region].append(temp_incumbent_retirement
                if seat_types[(this_seat_name, this_election.region())] <= 1 else 0)
            incumbent_retirement_regionals[this_election][this_seat_region].append(temp_incumbent_retirement
                if seat_types[(this_seat_name, this_election.region())] >= 2 else 0)
            sophomore_candidate_urbans[this_election][this_seat_region].append(temp_sophomore_candidate
                if seat_types[(this_seat_name, this_election.region())] <= 1 else 0)
            sophomore_candidate_regionals[this_election][this_seat_region].append(temp_sophomore_candidate
                if seat_types[(this_seat_name, this_election.region())] >= 2 else 0)
            sophomore_party_urbans[this_election][this_seat_region].append(temp_sophomore_party
                if seat_types[(this_seat_name, this_election.region())] <= 1 else 0)
            sophomore_party_regionals[this_election][this_seat_region].append(temp_sophomore_party
                if seat_types[(this_seat_name, this_election.region())] >= 2 else 0)
            previous_swings[this_election][this_seat_region].append(temp_previous_swing)
            by_election_swings[this_election][this_seat_region].append(temp_by_election_swing)
            names[this_election][this_seat_region].append(this_seat_name)
    region_averages = {election: {region: statistics.mean(x)
                                  for region, x in a.items()}
                       for election, a in alp_swings.items()}
    swing_deviations = {election: {region: [seat_swing - region_averages[election][region] for seat_swing in seat_swings]
                                   for region, seat_swings in election_regions.items()}
                        for election, election_regions in alp_swings.items()}
    region_swings = {election: {region: [region_averages[election][region] for a in seat_regions]
                                   for region, seat_regions in election_regions.items()}
                        for election, election_regions in alp_swings.items()}
    previous_swing_deviations = {}
    for election, regions in previous_swings.items():
        previous_election = (elections.previous_elections(election)[-1]
                             if len(elections.previous_elections(election)) > 0 else None)
        if previous_election is not None:
            previous_average = region_averages[previous_election]
            previous_swing_deviations[election] = {region_code:
                    [a - region_averages[previous_election][region_code]
                    if a is not None else 0 for a in previous_swings]
                for region_code, previous_swings in regions.items()}
        else:
            previous_swing_deviations[election] = {region_code:
                    [0 for a in previous_swings]
                for region_code, previous_swings in regions.items()}

    alp_swings_flat = []
    alp_deviations_flat = []
    federal_flat = []
    region_swings_flat = []
    margins_flat = []
    incumbent_retirement_urban_flat = []
    incumbent_retirement_regional_flat = []
    sophomore_candidate_urban_flat = []
    sophomore_candidate_regional_flat = []
    sophomore_party_urban_flat = []
    sophomore_party_regional_flat = []
    previous_swing_deviations_flat = []
    by_election_swing_flat = []
    names_flat = []
    election_regions_flat = []
    regions_flat = []
    for election_code, election in swing_deviations.items():
        for region_code, region in election.items():
            alp_swings_flat += alp_swings[election_code][region_code]
            alp_deviations_flat += region
            federal_flat += federals[election_code][region_code]
            region_swings_flat += region_swings[election_code][region_code]
            margins_flat += margins[election_code][region_code]
            incumbent_retirement_urban_flat += incumbent_retirement_urbans[election_code][region_code]
            incumbent_retirement_regional_flat += incumbent_retirement_regionals[election_code][region_code]
            sophomore_candidate_urban_flat += sophomore_candidate_urbans[election_code][region_code]
            sophomore_candidate_regional_flat += sophomore_candidate_regionals[election_code][region_code]
            sophomore_party_urban_flat += sophomore_party_urbans[election_code][region_code]
            sophomore_party_regional_flat += sophomore_party_regionals[election_code][region_code]
            previous_swing_deviations_flat += previous_swing_deviations[election_code][region_code]
            by_election_swing_flat += by_election_swings[election_code][region_code]
            names_flat += names[election_code][region_code]
            election_regions_flat += [election_code.region()] * len(region)
            regions_flat += [region_code] * len(region)
    abs_swings_flat = [abs(x) for x in alp_deviations_flat]

    # Analysis of swing *direction* factors
    inputs_array = numpy.transpose(numpy.array([incumbent_retirement_urban_flat,
                                                incumbent_retirement_regional_flat,
                                                sophomore_candidate_urban_flat,
                                                sophomore_candidate_regional_flat,
                                                sophomore_party_urban_flat,
                                                sophomore_party_regional_flat,
                                                previous_swing_deviations_flat,
                                                by_election_swing_flat]))
    results_array = numpy.array(alp_deviations_flat)
    reg = LinearRegression().fit(inputs_array, results_array)
    retirement_urban = reg.coef_[0]
    retirement_regional = reg.coef_[1]
    sophomore_candidate_urban = reg.coef_[2]
    sophomore_candidate_regional = reg.coef_[3]
    sophomore_party_urban = reg.coef_[4]
    sophomore_party_regional = reg.coef_[5]
    previous_swing_modifier = reg.coef_[6]
    by_election_modifier = reg.coef_[7]

    # Analysis of swing *magnitude* factors
    inputs_array = numpy.transpose(numpy.array([federal_flat, region_swings_flat, margins_flat]))
    results_array = numpy.array(abs_swings_flat)
    reg = LinearRegression().fit(inputs_array, results_array)

    mean_swing_deviation = reg.intercept_
    federal_modifier = reg.coef_[0]
    region_swing_effect = reg.coef_[1]
    margin_effect = reg.coef_[2]
    swing_kurtosis = one_tail_kurtosis(abs_swings_flat)

    filename = (f'./Seat Statistics/tpp-swing-factors.csv')
    with open(filename, 'w') as f:
        f.write(f'mean-swing-deviation,{mean_swing_deviation}\n')
        f.write(f'swing-kurtosis,{swing_kurtosis}\n')
        f.write(f'federal-modifier,{federal_modifier}\n')
        f.write(f'retirement-urban,{retirement_urban}\n')
        f.write(f'retirement-regional,{retirement_regional}\n')
        f.write(f'sophomore-candidate-urban,{sophomore_candidate_urban}\n')
        f.write(f'sophomore-candidate-regional,{sophomore_candidate_regional}\n')
        f.write(f'sophomore-party-urban,{sophomore_party_urban}\n')
        f.write(f'sophomore-party-regional,{sophomore_party_regional}\n')
        f.write(f'previous-swing-modifier,{previous_swing_modifier}\n')
        f.write(f'by-election-modifier,{by_election_modifier}\n')

    individual_infos = {}
    for i in range(0, len(names_flat)):
        key = (names_flat[i], election_regions_flat[i], regions_flat[i])
        vals = (alp_swings_flat[i], region_swings_flat[i])
        if key not in individual_infos:
            individual_infos[key] = []
        individual_infos[key].append(vals)

    filename = (f'./Seat Statistics/individual-seat-factors.csv')
    with open(filename, 'w') as f:
        # mix_factor = 0.38
        elasticity_factor = 0.38
        # base_errors = []
        # predicted_errors = []
        # mixed_errors = []
        for key, values in individual_infos.items():
            if (len(values) < 10 and key[1] != 'vic') or len(values) < 7:
                continue
            inputs_array = numpy.transpose(numpy.array(
                [[a[1] for a in values]]))
            results_array = numpy.array([a[0] for a in values])
            reg = LinearRegression(fit_intercept=False).fit(inputs_array, results_array)
            elasticity = reg.coef_[0]
            trend = 0  # set to zero as it seems to harm predictiveness so far
            residuals = [a[0] - elasticity * a[1] - trend for a in values]
            volatility = calc_rmse(residuals)
            # The elasticity from the regression significantly overestimates
            # seat elasticity in new samples. A factor of 0.38 was found to
            # provided the optimum predictiveness for sample sizes of 10 and up.
            adjusted_elasticity = (elasticity - 1) * elasticity_factor + 1
            # High trend/low volatility values are likely artifacts of small
            # sample sizes, so cap them
            limited_trend = min(max(trend, -2.5), 2.5)
            limited_volatility = max(volatility, 2)
            f.write(f'{key[0]},{key[1]},{key[2]},{adjusted_elasticity},{limited_trend},{limited_volatility}\n')

            # code for testing elasticity predictiveness

            # for remove in range(0, len(values)):
            #     new_values = [a for ind, a in enumerate(values) if ind != remove]
            #     inputs_array = numpy.transpose(numpy.array(
            #         [[a[1] for a in new_values]]))
            #     results_array = numpy.array([a[0] for a in new_values])
            #     reg = LinearRegression(fit_intercept=False).fit(inputs_array, results_array)
            #     new_elasticity = reg.coef_[0]
            #     new_trend = reg.intercept_
            #     observed_swing = values[remove][0]
            #     predicted_swing = values[remove][1] * new_elasticity + new_trend
            #     mixed_swing = predicted_swing * mix_factor + values[remove][1] * (1 - mix_factor)
            #     base_error = abs(observed_swing - values[remove][1])
            #     predicted_error = abs(observed_swing - predicted_swing)
            #     mixed_error = abs(observed_swing - mixed_swing)
            #     base_errors.append(base_error)
            #     predicted_errors.append(predicted_error)
            #     mixed_errors.append(mixed_error)
        # print("Overall errors:")
        # print(mix_factor)
        # print(len(base_errors))
        # print(statistics.mean(base_errors))
        # print(statistics.mean(predicted_errors))
        # print(statistics.mean(mixed_errors))


def analyse_green_independent_correlation(elections):
    d = {
        'greens_swings': [],
        'ind_swings': [],
        'party': 'Greens'
    }
    def func(d):
        if 'Greens' in [a.party for a in d['this_seat_results'].fp]:
            this_greens = sum(x.percent for x in d['this_seat_results'].fp
                                if x.party == 'Greens')
        else:
            return
        if 'Greens' in [a.party for a in d['next_seat_results'].fp]:
            next_greens = sum(x.percent for x in d['next_seat_results'].fp
                                if x.party == 'Greens')
        else:
            return
        this_ind = sum(x.percent for x in d['this_seat_results'].fp
                                if x.party == 'Independent')
        next_ind = sum(x.percent for x in d['next_seat_results'].fp
                                if x.party == 'Independent')
        if max(this_ind == 0, next_ind) < 8:
            return
        greens_swing = (transform_vote_share(next_greens)
                        - transform_vote_share(this_greens)
                        - d['election_swing'])
        # Can't run this under transformation as it may be zero
        ind_swing = next_ind - this_ind
        d['greens_swings'].append(greens_swing)
        d['ind_swings'].append(ind_swing)
    
    collect_election_data(elections, d, func, use_previous = False)
    
    inputs_np = numpy.transpose(numpy.array([d['ind_swings']]))
    results_np = numpy.array(d['greens_swings'])
    mod = sm.OLS(results_np, inputs_np)
    fit = mod.fit()

    filename = (f'./Seat Statistics/GRN_IND_correlation.csv')
    with open(filename, 'w') as f:
        f.write(f'{fit.params[0]}')


def get_all_elections():
    with open('./Data/polled-elections.csv', 'r') as f:
        polled_elections = ElectionCode.load_elections_from_file(f)
    with open('./Data/old-elections.csv', 'r') as f:
        old_elections = ElectionCode.load_elections_from_file(f)
    with open('./Data/future-elections.csv', 'r') as f:
        future_elections = ElectionCode.load_elections_from_file(f)
    return polled_elections + old_elections + future_elections


def analyse_nationals(elections, all_elections):
    def get_nationals_share(results, seat_name):
        seat_result = (results.seat_by_name(
            seat_name,
            include_name_changes=True
        ) if results is not None else None)
        if seat_result is None:
            return None
        nationals_candidate = next((a for a in seat_result.fp if a.party == "National"), None)
        liberals_candidate = next((a for a in seat_result.fp if a.party == "Liberal"), None)
        nationals_percent = 0
        liberals_percent = 0
        if nationals_candidate is not None:
            nationals_percent = nationals_candidate.percent
        else:
            return None
        if liberals_candidate is not None:
            liberals_percent = liberals_candidate.percent
        else:
            return None
        return nationals_percent / (nationals_percent + liberals_percent) * 100
    
    for target_election in all_elections:
        transformed_nationals_shares = []
        transformed_previous_nationals_shares = []
        transformed_old_nationals_shares = []
        for this_election, data in elections.items():
            if (
                this_election.region() == target_election.region()
                and int(this_election.year()) < int(target_election.year())
            ):
                previous_elections = elections.previous_elections(this_election)
                if len(previous_elections) > 0:
                    previous_election = previous_elections[-1]
                    previous_results = elections[previous_election]
                else:
                    continue
                if len(previous_elections) > 1:
                    old_election = previous_elections[-2]
                    old_results = elections[old_election]
                else:
                    continue
                for seat in data.seat_results:


                    this_nationals_share = get_nationals_share(data, seat.name)
                    if (this_nationals_share is None or this_nationals_share == 0):
                        continue
                    if previous_results is not None:
                        previous_nationals_share = get_nationals_share(previous_results, seat.name)
                    if old_results is not None:
                        old_nationals_share = get_nationals_share(old_results, seat.name)
                    if this_nationals_share is not None and previous_nationals_share is not None:
                        transformed_nationals_shares.append(transform_vote_share(this_nationals_share))
                        transformed_previous_nationals_shares.append(transform_vote_share(previous_nationals_share))
                        if old_nationals_share is not None:
                            transformed_old_nationals_shares.append(transform_vote_share(old_nationals_share))
                        else:
                            transformed_old_nationals_shares.append(transformed_previous_nationals_shares[-1])

        if len(transformed_nationals_shares) < 4:
            continue

        inputs_np = numpy.transpose(numpy.array([transformed_previous_nationals_shares, transformed_old_nationals_shares]))
        results_np = numpy.array(transformed_nationals_shares)
        reg = LinearRegression().fit(inputs_np, results_np)

        #calculate rmse of transformed_nationals_shares following this regression:
        predictions = [
            (reg.coef_[0] * transformed_previous_nationals_shares[i] +
            reg.coef_[1] * transformed_old_nationals_shares[i] +
            reg.intercept_)
            for i in range(len(transformed_nationals_shares))
        ]
        residuals = [
            transformed_nationals_shares[i] - predictions[i]
            for i in range(len(transformed_nationals_shares))
        ]
        mean_residual = numpy.mean(residuals)
        adjusted_residuals = [a - mean_residual for a in residuals]
        rmse = numpy.sqrt(numpy.mean(numpy.square(adjusted_residuals)))

        # calculate sample kurtosis of residuals
        this_kurtosis = two_tail_kurtosis(adjusted_residuals)

        filename = (f'./Nationals/{target_election.year()}{target_election.region()}_stats.csv')
        with open(filename, 'w') as f:
            f.write(f'prev_coef,old_coef,intercept,rmse,kurtosis\n')
            f.write(f'{reg.coef_[0]},{reg.coef_[1]},{reg.intercept_},{rmse},{this_kurtosis}')

        previous_elections = elections.previous_elections(target_election)
        if len(previous_elections) > 0:
            previous_election = previous_elections[-1]
            previous_results = elections[previous_election]
        else:
            continue
        if len(previous_elections) > 1:
            old_election = previous_elections[-2]
            old_results = elections[old_election]
        else:
            continue

        # generate predictions for individual seats 
        predictions = []
        for seat in previous_results.seat_results:
            if previous_results is not None:
                previous_nationals_share = get_nationals_share(previous_results, seat.name)
            if old_results is not None:
                old_nationals_share = get_nationals_share(old_results, seat.name)
            if previous_nationals_share is not None and old_nationals_share is not None:
                predictions.append((seat.name, previous_nationals_share * reg.coef_[0] + old_nationals_share * reg.coef_[1] + reg.intercept_))
            elif previous_nationals_share is not None:
                predictions.append((seat.name, previous_nationals_share))
            else:
                predictions.append((seat.name, 0))
        
        filename = (f'./Nationals/{target_election.year()}{target_election.region()}_seats.csv')
        with open(filename, 'w') as f:
            f.write(f'seat,prediction\n')
            for i in range(len(predictions)):
                f.write(f'{predictions[i][0]},{predictions[i][1]}\n')



if __name__ == '__main__':
    all_elections = get_all_elections()
    elections = get_checked_elections()
    seat_types = load_seat_types()
    seat_regions = load_seat_regions()
    by_elections = load_by_elections()
    analyse_greens(elections)
    analyse_existing_independents(elections)
    analyse_emerging_independents(elections, seat_types)
    analyse_populist_minors(elections, seat_types, seat_regions)
    analyse_centrist_minors(elections, seat_types, seat_regions)
    analyse_others(elections)
    analyse_emerging_parties(elections)
    analyse_region_swings()
    analyse_seat_swings(elections, seat_types, seat_regions, by_elections)
    analyse_green_independent_correlation(elections)
    analyse_nationals(elections, all_elections)
    print("Analysis completed.")