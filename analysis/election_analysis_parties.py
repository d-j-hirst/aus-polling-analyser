"""Historical models for Greens, independent and other minor-party votes.

Parent: election_analysis.py invokes this module as part of the historical
seat-analysis bundle before provenance is recorded.

Main functions:
* ``analyse_greens`` and ``analyse_existing_independents`` fit continuing
  seat-level minor-party behaviour.
* ``analyse_emerging_independents`` and ``analyse_emerging_parties`` model
  new entrants from historical seat context.
* ``analyse_populist_minors``, ``analyse_centrist_minors`` and
  ``analyse_others`` produce the remaining party-family parameters.
* ``load_seat_*`` and ``load_by_elections`` are authored-input loaders used by
  the core analysis functions.
"""

import copy
import math
import statistics

import numpy
import statsmodels.api as sm
from scipy.optimize import curve_fit
from sklearn.linear_model import LinearRegression

from election_analysis_common import (
    collect_election_data,
    create_bucket_template,
    effective_independent,
    effective_others,
    fp_threshold,
    ind_bucket_size,
    one_tail_kurtosis,
    run_bucket_regressions,
    smooth_buckets_and_save,
    total_others_vote_share,
    transfer_buckets,
)
from election_code import ElectionCode
from poll_transform import clamp, transform_vote_share
from sample_kurtosis import calc_rmse, two_tail_kurtosis


# Core historical party-vote analyses

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


# Authored contextual-input loading

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
        coef = reg.coef_[0]
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
    for seat_id, seat_type in seat_types.items():
        if seat_id not in avg_mult_seat:
            avg_mult_seat[seat_id] = vote_intercept
            if seat_type == 3:
                avg_mult_seat[seat_id] += rural_coefficient
            if seat_type == 2:
                avg_mult_seat[seat_id] += provincial_coefficient
            if seat_type == 1:
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
        'calib_cutoff': 2025,
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
