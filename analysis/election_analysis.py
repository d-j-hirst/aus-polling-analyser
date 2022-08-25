from election_code import ElectionCode
import copy
import math
import numpy
import statistics
from sklearn.linear_model import LinearRegression
from scipy.optimize import curve_fit
from scipy.interpolate import UnivariateSpline
from election_check import get_checked_elections
from poll_transform import transform_vote_share, detransform_vote_share, clamp
from sample_kurtosis import calc_rmse, one_tail_kurtosis

ind_bucket_size = 2
fp_threshold = detransform_vote_share(int(math.floor(transform_vote_share(8) / ind_bucket_size)) * ind_bucket_size)

def analyse_greens(elections):
    bucket_min = -90
    bucket_base = 10
    bucket_max = -30
    this_buckets = {(-10000, bucket_min): []}
    this_buckets.update({(a, a + bucket_base): [] for a in range(bucket_min, bucket_max, bucket_base)})
    this_buckets.update({(bucket_max, 10000): []})
    swing_buckets = copy.deepcopy(this_buckets)
    sophomore_buckets = copy.deepcopy(this_buckets)
    party = 'Greens'
    for this_election, this_results in elections.items():
        # print(f'Gathering results for {party} in {this_election}')
        if len(elections.next_elections(this_election)) == 0:
            continue
        next_election = elections.next_elections(this_election)[0]
        next_results = elections[next_election]
        if len(elections.previous_elections(this_election)) > 0:
            previous_election = elections.previous_elections(this_election)[-1]
            previous_results = elections[previous_election]
        else:
            previous_results = None
        election_swing = (transform_vote_share(next_results.total_fp_percentage_party(party))
                 - transform_vote_share(this_results.total_fp_percentage_party(party)))
        # print(f'{this_election.short()} - {next_election.short()} overall swing to greens: {election_swing}')
        for this_seat_name in this_results.seat_names():
            this_seat_results = this_results.seat_by_name(this_seat_name)
            if len(this_seat_results.tcp) == 0:
                continue  # ignore seats where candidates are unopposed
            if this_seat_name not in next_results.seat_names():
                continue
            next_seat_results = next_results.seat_by_name(this_seat_name)
            if len(next_seat_results.tcp) == 0:
                continue  # ignore seats where candidates are unopposed
            sophomore = False
            if (previous_results is not None
                and this_seat_name in previous_results.seat_names(include_name_changes=True)):
                previous_seat_results = \
                    previous_results.seat_by_name(this_seat_name,
                                                    include_name_changes=True)
                if (len(previous_seat_results.tcp) > 0 and
                    previous_seat_results.tcp[0].party != party and
                    this_seat_results.tcp[0].party == party):
                    sophomore = True
            if party in [a.party for a in this_seat_results.fp]:
                this_greens = sum(x.percent for x in this_seat_results.fp
                                    if x.party == party)
            else:
                continue
            if party in [a.party for a in next_seat_results.fp]:
                next_greens = sum(x.percent for x in next_seat_results.fp
                                    if x.party == party)
            else:
                continue
            this_greens = transform_vote_share(this_greens)
            next_greens = transform_vote_share(next_greens)
            greens_change = next_greens - this_greens
            this_bucket = next(a for a in this_buckets
                                if a[0] < this_greens
                                and a[1] > this_greens)
            this_buckets[this_bucket].append(greens_change)
            swing_buckets[this_bucket].append(election_swing)
            sophomore_buckets[this_bucket].append(1 if sophomore else 0)

    ordered_buckets = sorted(this_buckets.keys(), key=lambda x: x[0])
    bucket_counts = {}
    bucket_swing_coefficients = {}
    bucket_sophomore_coefficients = {}
    bucket_intercepts = {}
    bucket_median_errors = {}
    bucket_lower_rmses = {}
    bucket_upper_rmses = {}
    bucket_lower_kurtoses = {}
    bucket_upper_kurtoses = {}

    for bucket, results in this_buckets.items():
        # Run regression between the seat swing and election swing
        # to find the relationship between the two for initial primary
        # votes in this bucket
        swings = swing_buckets[bucket]
        sophomores = sophomore_buckets[bucket]
        inputs_array = numpy.transpose(numpy.array([swings, sophomores]))
        results_array = numpy.array(results)
        reg = LinearRegression().fit(inputs_array, results_array)
        swing_coefficient = reg.coef_[0]
        sophomore_coefficient = reg.coef_[1]
        overall_intercept = reg.intercept_

        # Get the residuals (~= errors if the above relationship is used
        # as a prediction), find the median, and split the errors into
        # a group above and below the median, measured by their distance
        # from the median
        residuals = [results[index] -
                        (swing_coefficient * swings[index] +
                        sophomore_coefficient * sophomores[index]
                        + overall_intercept)
                        for index in range(0, len(results))
        ]
        median_error = statistics.median(residuals)
        lower_errors = [a - median_error for a in residuals if a < median_error]
        upper_errors = [a - median_error for a in residuals if a >= median_error]

        # Find effective RMSE and kurtosis for the two tails of the
        # distribution (in each case, as if the other side of the
        # distribution is symmetrical)
        lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                            / (len(lower_errors) - 1))
        upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                            / (len(upper_errors) - 1))
        lower_kurtosis = one_tail_kurtosis(lower_errors)
        upper_kurtosis = one_tail_kurtosis(upper_errors)

        bucket_counts[bucket] = len(results)
        bucket_swing_coefficients[bucket] = swing_coefficient
        bucket_sophomore_coefficients[bucket] = sophomore_coefficient
        bucket_intercepts[bucket] = overall_intercept
        bucket_median_errors[bucket] = median_error
        bucket_lower_rmses[bucket] = lower_rmse
        bucket_upper_rmses[bucket] = upper_rmse
        bucket_lower_kurtoses[bucket] = lower_kurtosis
        bucket_upper_kurtoses[bucket] = upper_kurtosis

    for bucket_index in range(len(ordered_buckets) - 2, -1, -1):
        bucket = ordered_buckets[bucket_index]
        if not 1 in sophomore_buckets[bucket]:
            next_bucket = ordered_buckets[bucket_index + 1]
            bucket_sophomore_coefficients[bucket] = \
                bucket_sophomore_coefficients[next_bucket]

    # for bucket in bucket_swing_coefficients.keys():
    #     print(f'Primary vote bucket: {detransform_vote_share(bucket[0])} - {detransform_vote_share(bucket[1])}')
    #     print(f'Sample size: {bucket_counts[bucket]}')
    #     print(f'Election swing coefficient: {bucket_swing_coefficients[bucket]}')
    #     print(f'Sophomore coefficient: {bucket_sophomore_coefficients[bucket]}')
    #     print(f'Intercept: {bucket_intercepts[bucket]}')
    #     print(f'Median error: {bucket_median_errors[bucket]}')
    #     print(f'Lower rmse: {bucket_lower_rmses[bucket]}')
    #     print(f'Upper rmse: {bucket_upper_rmses[bucket]}')
    #     print(f'Lower kurtosis: {bucket_lower_kurtoses[bucket]}')
    #     print(f'Upper kurtosis: {bucket_upper_kurtoses[bucket]}')
    #     print('\n')

    x = list(range(int(bucket_min - bucket_base / 2),
                   bucket_max + bucket_base,
                   bucket_base))
    swing_coefficients = [a for a in bucket_swing_coefficients.values()]
    spline = UnivariateSpline(x=x, y=swing_coefficients, s=10)
    smoothed_swing_coefficients = spline(x)
    sophomore_coefficients = [a for a in bucket_sophomore_coefficients.values()]
    spline = UnivariateSpline(x=x, y=sophomore_coefficients, s=10)
    smoothed_sophomore_coefficients = spline(x)
    offsets = [a + b for a, b in zip(bucket_intercepts.values(),
                                     bucket_median_errors.values())]
    spline = UnivariateSpline(x=x, y=offsets, s=10)
    smoothed_offsets = spline(x)
    lower_rmses = [a for a in bucket_lower_rmses.values()]
    spline = UnivariateSpline(x=x, y=lower_rmses, s=10)
    smoothed_lower_rmses = spline(x)
    upper_rmses = [a for a in bucket_upper_rmses.values()]
    spline = UnivariateSpline(x=x, y=upper_rmses, s=10)
    smoothed_upper_rmses = spline(x)
    lower_kurtoses = [a for a in bucket_lower_kurtoses.values()]
    spline = UnivariateSpline(x=x, y=lower_kurtoses, s=10)
    smoothed_lower_kurtoses = spline(x)
    upper_kurtoses = [a for a in bucket_upper_kurtoses.values()]
    spline = UnivariateSpline(x=x, y=upper_kurtoses, s=10)
    smoothed_upper_kurtoses = spline(x)
    # Assume Greens always recontest
    recontest_rates = [1 for a in bucket_counts]
    recontest_incumbent_rates = [1 for a in bucket_counts]

    party_code = 'GRN'

    filename = (f'./Seat Statistics/statistics_{party_code}.csv')
    with open(filename, 'w') as f:
        f.write(','.join([f'{a:.4f}' for a in x]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_swing_coefficients]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_sophomore_coefficients]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_offsets]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_lower_rmses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_upper_rmses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_lower_kurtoses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_upper_kurtoses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in recontest_rates]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in recontest_incumbent_rates]) + '\n')


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
    # Note: Covers "effective independents" (those who are technically
    # affiliated with a party but mainly dependent on their personal vote)
    bucket_min = -50
    bucket_base = 15
    bucket_max = -5
    this_buckets = {(-10000, bucket_min): []}
    this_buckets.update({(a, a + bucket_base): [] for a in
                         range(bucket_min, bucket_max, bucket_base)})
    this_buckets.update({(bucket_max, 10000): []})
    sophomore_buckets = copy.deepcopy(this_buckets)
    incumbent_buckets = copy.deepcopy(this_buckets)
    recontest_buckets = copy.deepcopy(this_buckets)
    recontest_sophomore_buckets = copy.deepcopy(this_buckets)
    recontest_incumbent_buckets = copy.deepcopy(this_buckets)
    for this_election, this_results in elections.items():
        # print(f'\nGathering results for {party} in {this_election}')
        if len(elections.next_elections(this_election)) == 0:
            continue
        next_election = elections.next_elections(this_election)[0]
        next_results = elections[next_election]
        if len(elections.previous_elections(this_election)) > 0:
            previous_election = elections.previous_elections(this_election)[-1]
            previous_results = elections[previous_election]
        else:
            previous_results = None
        for this_seat_name in this_results.seat_names():
            this_seat_results = this_results.seat_by_name(this_seat_name)
            # ignore seats where candidates are unopposed
            if len(this_seat_results.tcp) == 0:
                continue
            if this_seat_name not in next_results.seat_names():
                continue
            next_seat_results = next_results.seat_by_name(this_seat_name)
            # ignore seats where candidates are unopposed
            if len(next_seat_results.tcp) == 0:
                continue
            independents = [a for a in this_seat_results.fp
                            if a.percent > fp_threshold
                            and effective_independent(a.party,
                                                        this_results)
                            ]
            if (len(independents) == 0):
                continue
            # Only consider the highest polling independent from each seat
            highest = max(independents, key=lambda x: x.percent)
            # Only consider independents with above a certain primary vote
            if highest.percent < fp_threshold:
                continue
            sophomore = False
            if (previous_results is not None
                and this_seat_name in previous_results.seat_names(include_name_changes=True)):
                previous_seat_results = \
                    previous_results.seat_by_name(this_seat_name,
                                                    include_name_changes=True)
                # For independent sophomore effects, independents with a different name
                # should not be counted
                if (len(previous_seat_results.tcp) > 0
                    and this_seat_results.tcp[0].name == highest.name
                    and (not effective_independent(previous_seat_results.tcp[0].party,
                                                previous_results)
                            or previous_seat_results.tcp[0].name !=
                            this_seat_results.tcp[0].name)):
                    sophomore = True
            incumbent = (this_seat_results.tcp[0].name == highest.name)
            this_fp = highest.percent
            this_fp = transform_vote_share(this_fp)
            this_bucket = next(a for a in this_buckets
                                if a[0] < this_fp
                                and a[1] > this_fp)
            matching_next = [a for a in next_seat_results.fp
                                if a.name == highest.name]
            if len(matching_next) > 0:
                next_fp = matching_next[0].percent
                recontest_buckets[this_bucket].append(1)
                recontest_incumbent_buckets[this_bucket].append(1 if incumbent else 0)
            else:
                recontest_buckets[this_bucket].append(0)
                recontest_incumbent_buckets[this_bucket].append(1 if incumbent else 0)
                continue
            # print(f' Found independent for seat {this_seat_name}: {matching_next}')
            next_fp = transform_vote_share(next_fp)
            fp_change = next_fp - this_fp
            this_buckets[this_bucket].append(fp_change)
            incumbent_buckets[this_bucket].append(1 if incumbent else 0)
            sophomore_buckets[this_bucket].append(1 if sophomore else 0)

    ordered_buckets = sorted(this_buckets.keys(), key=lambda x: x[0])
    bucket_counts = {}
    bucket_sophomore_coefficients = {}
    bucket_intercepts = {}
    bucket_median_errors = {}
    bucket_lower_rmses = {}
    bucket_upper_rmses = {}
    bucket_lower_kurtoses = {}
    bucket_upper_kurtoses = {}
    # bucket_recontest_sophomores = {}
    bucket_recontest_incumbents = {}
    bucket_recontest_rates = {}

    for bucket in ordered_buckets:
        # Run regression between the seat swing and election swing
        # to find the relationship between the two for initial primary
        # votes in this bucket
        sophomores = sophomore_buckets[bucket]
        incumbents = incumbent_buckets[bucket]
        inputs_array = numpy.transpose(numpy.array([sophomores, incumbents]))
        results_array = numpy.array(this_buckets[bucket])
        reg = LinearRegression().fit(inputs_array, results_array)
        sophomore_coefficient = reg.coef_[0]
        incumbent_coefficient = reg.coef_[1]
        overall_intercept = reg.intercept_

        # Get the residuals (~= errors if the above relationship is used
        # as a prediction), find the median, and split the errors into
        # a group above and below the median, measured by their distance
        # from the median
        residuals = [this_buckets[bucket][index] -
                        (sophomore_coefficient * sophomores[index]
                        + overall_intercept)
                        for index in range(0, len(this_buckets[bucket]))
        ]
        median_error = statistics.median(residuals)
        lower_errors = [a - median_error for a in residuals if a < median_error]
        upper_errors = [a - median_error for a in residuals if a >= median_error]

        # Find effective RMSE and kurtosis for the two tails of the
        # distribution (in each case, as if the other side of the
        # distribution is symmetrical)
        lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                            / (len(lower_errors) - 1))
        upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                            / (len(upper_errors) - 1))
        lower_kurtosis = one_tail_kurtosis(lower_errors)
        upper_kurtosis = one_tail_kurtosis(upper_errors)


        bucket_counts[bucket] = len(this_buckets[bucket])
        bucket_sophomore_coefficients[bucket] = sophomore_coefficient
        bucket_intercepts[bucket] = overall_intercept
        bucket_median_errors[bucket] = median_error
        bucket_lower_rmses[bucket] = lower_rmse
        bucket_upper_rmses[bucket] = upper_rmse
        bucket_lower_kurtoses[bucket] = lower_kurtosis
        bucket_upper_kurtoses[bucket] = upper_kurtosis

        recontests = recontest_buckets[bucket]
        incumbent_recontests = recontest_incumbent_buckets[bucket]
        inputs_array = numpy.transpose(numpy.array([incumbent_recontests]))
        results_array = numpy.array(recontests)
        # print(inputs_array)
        # print(results_array)
        reg = LinearRegression().fit(inputs_array, results_array)
        incumbent_recontest_coefficient = reg.coef_[0]
        recontest_intercept = reg.intercept_
        bucket_recontest_incumbents[bucket] = incumbent_recontest_coefficient
        bucket_recontest_rates[bucket] = recontest_intercept

    for bucket_index in range(len(ordered_buckets) - 2, -1, -1):
        bucket = ordered_buckets[bucket_index]
        if not 1 in sophomore_buckets[bucket]:
            next_bucket = ordered_buckets[bucket_index + 1]
            bucket_sophomore_coefficients[bucket] = \
                bucket_sophomore_coefficients[next_bucket]
        if not 1 in recontest_incumbent_buckets[bucket]:
            next_bucket = ordered_buckets[bucket_index + 1]
            bucket_recontest_incumbents[bucket] = \
                bucket_recontest_incumbents[next_bucket]

    # for bucket in bucket_counts.keys():
    #     print(f'Primary vote bucket: {detransform_vote_share(bucket[0])} - {detransform_vote_share(bucket[1])}')
    #     print(f'Sample size: {bucket_counts[bucket]}')
    #     print(f'Sophomore coefficient: {bucket_sophomore_coefficients[bucket]}')
    #     print(f'Intercept: {bucket_intercepts[bucket]}')
    #     print(f'Median error: {bucket_median_errors[bucket]}')
    #     print(f'Lower rmse: {bucket_lower_rmses[bucket]}')
    #     print(f'Upper rmse: {bucket_upper_rmses[bucket]}')
    #     print(f'Lower kurtosis: {bucket_lower_kurtoses[bucket]}')
    #     print(f'Upper kurtosis: {bucket_upper_kurtoses[bucket]}')
    #     print(f'Recontest rate: {bucket_recontest_rates[bucket]}')
    #     print(f'Recontest incumbent: {bucket_recontest_incumbents[bucket]}')
    #     print('\n')

    x = list(range(int(bucket_min - bucket_base / 2),
                   bucket_max + bucket_base,
                   bucket_base))
    # assume no relationship between overall and individual IND swing
    swing_coefficients = [0 for a in bucket_counts]
    sophomore_coefficients = [a for a in bucket_sophomore_coefficients.values()]
    spline = UnivariateSpline(x=x, y=sophomore_coefficients, s=100)
    smoothed_sophomore_coefficients = spline(x)
    offsets = [a + b for a, b in zip(bucket_intercepts.values(),
                                     bucket_median_errors.values())]
    spline = UnivariateSpline(x=x, y=offsets, s=100)
    smoothed_offsets = spline(x)
    lower_rmses = [a for a in bucket_lower_rmses.values()]
    spline = UnivariateSpline(x=x, y=lower_rmses, s=100)
    smoothed_lower_rmses = spline(x)
    upper_rmses = [a for a in bucket_upper_rmses.values()]
    spline = UnivariateSpline(x=x, y=upper_rmses, s=100)
    smoothed_upper_rmses = spline(x)
    lower_kurtoses = [a for a in bucket_lower_kurtoses.values()]
    spline = UnivariateSpline(x=x, y=lower_kurtoses, s=100)
    smoothed_lower_kurtoses = spline(x)
    upper_kurtoses = [a for a in bucket_upper_kurtoses.values()]
    spline = UnivariateSpline(x=x, y=upper_kurtoses, s=100)
    smoothed_upper_kurtoses = spline(x)
    recontest_rates = [a for a in bucket_recontest_rates.values()]
    spline = UnivariateSpline(x=x, y=recontest_rates, s=100)
    smoothed_recontest_rates = spline(x)
    recontest_incumbent_rates = [a for a in bucket_recontest_incumbents.values()]
    spline = UnivariateSpline(x=x, y=recontest_incumbent_rates, s=100)
    smoothed_recontest_incumbent_rates = spline(x)

    party_code = 'IND'

    filename = (f'./Seat Statistics/statistics_{party_code}.csv')
    with open(filename, 'w') as f:
        f.write(','.join([f'{a:.4f}' for a in x]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in swing_coefficients]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_sophomore_coefficients]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_offsets]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_lower_rmses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_upper_rmses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_lower_kurtoses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_upper_kurtoses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_recontest_rates]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_recontest_incumbent_rates]) + '\n')


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


def analyse_emerging_independents(elections, seat_types):
    seat_ind_count = []
    seat_fed = []
    seat_rural = []
    seat_provincial = []
    seat_outer_metro = []
    seat_rural = []
    seat_prev_others = []
    cand_fp_vote = []
    cand_fed = []
    cand_rural = []
    cand_provincial = []
    cand_outer_metro = []
    cand_prev_others = []
    party = 'Independent'
    for this_election, this_results in elections.items():
        if len(elections.next_elections(this_election)) == 0:
            continue
        next_election = elections.next_elections(this_election)[0]
        next_results = elections[next_election]
        weight = 1
        for this_seat_name in this_results.seat_names():
            this_seat_results = this_results.seat_by_name(this_seat_name)
            # ignore seats where candidates are unopposed
            if len(this_seat_results.tcp) == 0:
                continue
            if this_seat_name not in next_results.seat_names():
                continue
            next_seat_results = next_results.seat_by_name(this_seat_name)
            # ignore seats where candidates are unopposed
            if len(next_seat_results.tcp) == 0:
                continue
            old_names = [a.name for a in this_seat_results.fp
                         if a.percent > fp_threshold
                         and effective_independent(a.party, this_results)]
            new_independents = [a for a in next_seat_results.fp
                                if effective_independent(a.party, next_results)
                                and a.name not in old_names
                                and a.percent > fp_threshold]
            fed = 1 if next_election.region() == 'fed' else 0
            this_others = sum([min(a.percent, fp_threshold) for a in this_seat_results.fp
                               if a.party not in ['Labor', 'Liberal', 'Greens', 'National']])
            others_indicator = max(2, this_others)
            seat_type = seat_types.get((this_seat_name, next_election.region()), -1)
            for i in range(0, weight):
                seat_ind_count.append(len(new_independents))
                seat_fed.append(fed)
                seat_rural.append(1 if seat_type == 3 else 0)
                seat_provincial.append(1 if seat_type == 2 else 0)
                seat_outer_metro.append(1 if seat_type == 1 else 0)
                seat_prev_others.append(others_indicator)
                for candidate in new_independents:
                    # print(f'Found emerging independent - {candidate} in {this_seat_name}')
                    cand_fp_vote.append(transform_vote_share(candidate.percent))
                    cand_fed.append(fed)
                    cand_rural.append(1 if seat_type == 3 else 0)
                    cand_provincial.append(1 if seat_type == 2 else 0)
                    cand_outer_metro.append(1 if seat_type == 1 else 0)
                    cand_prev_others.append(others_indicator)

    inputs_array = numpy.transpose(numpy.array([seat_fed, seat_rural, seat_provincial, seat_outer_metro, seat_prev_others]))
    results_array = numpy.array(seat_ind_count)
    reg = LinearRegression().fit(inputs_array, results_array)
    fed_coefficient = reg.coef_[0]
    rural_coefficient = reg.coef_[1]
    provincial_coefficient = reg.coef_[2]
    outer_metro_coefficient = reg.coef_[3]
    prev_others_coefficient = reg.coef_[4]
    intercept = reg.intercept_
    # print(f'Federal emergence coefficient: {fed_coefficient}')
    # print(f'Rural emergence coefficient: {rural_coefficient}')
    # print(f'Provincial emergence coefficient: {provincial_coefficient}')
    # print(f'Outer Metro emergence coefficient: {outer_metro_coefficient}')
    # print(f'Previous-others coefficient: {prev_others_coefficient}')
    # print(f'Emergence base rate: {intercept}')

    fp_vote_buckets = {}
    for index in range(0, len(cand_fp_vote)):
        fp_vote = cand_fp_vote[index]
        fp_vote_bucket = int(math.floor(fp_vote / ind_bucket_size)) * ind_bucket_size
        if fp_vote_bucket in fp_vote_buckets:
            fp_vote_buckets[fp_vote_bucket] += 1
        else:
            fp_vote_buckets[fp_vote_bucket] = 1

    inputs_array = numpy.transpose(numpy.array([cand_fed, cand_rural, cand_provincial, cand_outer_metro, cand_prev_others]))
    results_array = numpy.array(cand_fp_vote)
    reg = LinearRegression().fit(inputs_array, results_array)
    fed_vote_coefficient = reg.coef_[0]
    rural_vote_coefficient = reg.coef_[1]
    provincial_vote_coefficient = reg.coef_[2]
    outer_metro_vote_coefficient = reg.coef_[3]
    prev_others_vote_coefficient = reg.coef_[4]
    vote_intercept = reg.intercept_
    # print(f'Federal vote coefficient: {fed_vote_coefficient}')
    # print(f'Rural vote coefficient: {rural_vote_coefficient}')
    # print(f'Provincial vote coefficient: {provincial_vote_coefficient}')
    # print(f'Outer Metro vote coefficient: {outer_metro_vote_coefficient}')
    # print(f'Previous-others coefficient: {prev_others_vote_coefficient}')
    # print(f'Vote intercept: {vote_intercept} - detransformed {detransform_vote_share(vote_intercept)}')
    # print(f'fp threshold: {fp_threshold}')

    deviations = [a - transform_vote_share(fp_threshold) for a in cand_fp_vote]
    upper_rmse = math.sqrt(sum([a ** 2 for a in deviations])
                           / (len(deviations) - 1))
    upper_kurtosis = one_tail_kurtosis(deviations)
    # print(f'\nAverage extra vote (transformed): {average_extra_vote}')
    # print(f'\nUpper rmse: {upper_rmse}')
    # print(f'\nUpper kurtosis: {upper_kurtosis}')

    filename = (f'./Seat Statistics/statistics_emerging_IND.csv')
    with open(filename, 'w') as f:
        f.write(f'{fp_threshold}\n')
        f.write(f'{intercept}\n')
        f.write(f'{fed_coefficient}\n')
        f.write(f'{rural_coefficient}\n')
        f.write(f'{provincial_coefficient}\n')
        f.write(f'{outer_metro_coefficient}\n')
        f.write(f'{prev_others_coefficient}\n')
        f.write(f'{upper_rmse}\n')
        f.write(f'{upper_kurtosis}\n')
        f.write(f'{fed_vote_coefficient}\n')
        f.write(f'{rural_vote_coefficient}\n')
        f.write(f'{provincial_vote_coefficient}\n')
        f.write(f'{outer_metro_vote_coefficient}\n')
        f.write(f'{prev_others_vote_coefficient}\n')
        f.write(f'{vote_intercept}\n')


def analyse_populist_minors(elections, seat_types, seat_regions):

    on_results = {}
    on_election_votes = {}
    on_election_cands = {}
    for election, results in elections.items():
        region = election.region()
        year = election.year()
        if region not in on_results:
            on_results[region] = {}
        if (region, year) not in on_election_votes:
            on_election_votes[(region, year)] = 0
            on_election_cands[(region, year)] = 0
        for seat_result in results.seat_results:
            for cand in [a for a in seat_result.fp if a.party == 'One Nation']:
                on_percent = cand.percent
                on_election_votes[(region, year)] += cand.percent
                on_election_cands[(region, year)] += 1
                if seat_result.name not in on_results[election.region()]:
                    on_results[region][seat_result.name] = [(year, on_percent)]
                else:
                    on_results[region][seat_result.name].append((year, on_percent))
    on_election_average = {}
    for key, total in on_election_votes.items():
        if on_election_cands[key] == 0:
            continue
        on_election_average[key] = total / on_election_cands[key]


    avg_mult_seat = {}
    for region_name, region_results in on_results.items():
        for seat_name, seat_results in region_results.items():
            max_mult = 0
            min_mult = 100
            mult_sum = 0
            mult_count = 0
            for cand in seat_results:
                if cand[0] > 2003:
                    continue
                mult = cand[1] / on_election_average[region_name, cand[0]]
                max_mult = max(max_mult, mult)
                min_mult = min(min_mult, mult)
                mult_sum += mult
                mult_count += 1
            if mult_count == 0:
                continue
            seat_id = (region_name, seat_name)
            avg_mult_seat[seat_id] = mult_sum / mult_count

    test_settings = [(2019, 'fed', 'One Nation'),
                  (2019, 'fed', 'United Australia'),
                  (2017, 'qld', 'One Nation'),
                  (2020, 'qld', 'One Nation'),
                  (2015, 'qld', 'United Australia'),
                  (2013, 'fed', 'United Australia')]
    all_residuals = []
    for test_setting in test_settings:
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
            if seat_name == 'Fairfax' and test_year == 2013:
                continue  # Clive Palmer standing, skews results
            if seat_name == 'Mirani' and test_year == 2020:
                continue  # One Nation incumbent, skews results
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

        median_error = statistics.median(residuals)
        lower_errors = [a - median_error for a in residuals if a < median_error]
        upper_errors = [a - median_error for a in residuals if a >= median_error]
        lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                            / (len(lower_errors) - 1))
        upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                            / (len(upper_errors) - 1))
        lower_kurtosis = one_tail_kurtosis(lower_errors)
        upper_kurtosis = one_tail_kurtosis(upper_errors)

    median_error = statistics.median(all_residuals)
    lower_errors = [a - median_error for a in all_residuals if a < median_error]
    upper_errors = [a - median_error for a in all_residuals if a >= median_error]
    lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                        / (len(lower_errors) - 1))
    upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                        / (len(upper_errors) - 1))
    lower_kurtosis = one_tail_kurtosis(lower_errors)
    upper_kurtosis = one_tail_kurtosis(upper_errors)

    # re-do this with recent results
    # and include seat characteristics for regressions
    avg_mult_seat = {}
    rural_seat = {}
    provincial_seat = {}
    outer_metro_seat = {}
    home_state_seat = {}
    for region_name, region_results in on_results.items():
        for seat_name, seat_results in region_results.items():
            max_mult = 0
            min_mult = 100
            mult_sum = 0
            mult_count = 0
            for cand in seat_results:
                if cand[0] > 2003 and cand[0] < 2017:
                    continue
                mult = cand[1] / on_election_average[region_name, cand[0]]
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
            # Conviently the home state of all right-populist parties is QLD
            is_home_state = seat_regions.get((seat_name, region_name), '') == 'qld'
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

    for seat_id, type in seat_types.items():
        if seat_id not in avg_mult_seat:
            avg_mult_seat[seat_id] = vote_intercept
            if type == 3:
                avg_mult_seat[seat_id] += rural_coefficient
            if type == 2:
                avg_mult_seat[seat_id] += provincial_coefficient
            if type == 1:
                avg_mult_seat[seat_id] += outer_metro_coefficient

    filename = (f'./Seat Statistics/statistics_populist.csv')
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
        if seat_regions.get((key[0], key[1]), '') == 'qld':
            avg_mult_seat[key] -= home_state_coefficient

    filename = (f'./Seat Statistics/modifiers_populist.csv')
    with open(filename, 'w') as f:
        for key, value in avg_mult_seat.items():
            f.write(f'{key[0]},{key[1]},{value:.4f}\n')


def analyse_centrist_minors(elections, seat_types, seat_regions):
    dem_results = {}
    dem_election_votes = {}
    dem_election_cands = {}
    for election, results in elections.items():
        region = election.region()
        year = election.year()
        if region not in dem_results:
            dem_results[region] = {}
        if (region, year) not in dem_election_votes:
            dem_election_votes[(region, year)] = 0
            dem_election_cands[(region, year)] = 0
        for seat_result in results.seat_results:
            for cand in [a for a in seat_result.fp if a.party == 'Democrats']:
                on_percent = cand.percent
                dem_election_votes[(region, year)] += cand.percent
                dem_election_cands[(region, year)] += 1
                if seat_result.name not in dem_results[election.region()]:
                    dem_results[region][seat_result.name] = [(year, on_percent)]
                else:
                    dem_results[region][seat_result.name].append((year, on_percent))
    dem_election_average = {}
    for key, total in dem_election_votes.items():
        if dem_election_cands[key] == 0:
            continue
        dem_election_average[key] = total / dem_election_cands[key]


    avg_mult_seat = {}
    for region_name, region_results in dem_results.items():
        for seat_name, seat_results in region_results.items():
            max_mult = 0
            min_mult = 100
            mult_sum = 0
            mult_count = 0
            for cand in seat_results:
                if cand[0] > 2002:
                    continue
                mult = cand[1] / dem_election_average[region_name, cand[0]]
                max_mult = max(max_mult, mult)
                min_mult = min(min_mult, mult)
                mult_sum += mult
                mult_count += 1
            if mult_count == 0:
                continue
            seat_id = (region_name, seat_name)
            avg_mult_seat[seat_id] = mult_sum / mult_count

    test_settings = [(1990, 'fed', 'Democrats'),
                  (1993, 'fed', 'Democrats'),
                  (1996, 'fed', 'Democrats'),
                  (1998, 'fed', 'Democrats'),
                  (2001, 'fed', 'Democrats')]
    all_residuals = []
    for test_setting in test_settings:
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
            if seat_name == 'Fraser' and test_year < 2018:
                continue  # two federal Frasers, only use the latest one
            dem_sum = sum([a.percent for a in seat_result.fp if a.party == test_party])
            avg_mult = avg_mult_seat[(test_region, seat_name)]
            if dem_sum > 0:
                avg_mults.append(avg_mult)
                vote_shares.append(dem_sum)
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

        median_error = statistics.median(residuals)
        lower_errors = [a - median_error for a in residuals if a < median_error]
        upper_errors = [a - median_error for a in residuals if a >= median_error]
        lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                            / (len(lower_errors) - 1))
        upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                            / (len(upper_errors) - 1))
        lower_kurtosis = one_tail_kurtosis(lower_errors)
        upper_kurtosis = one_tail_kurtosis(upper_errors)

    median_error = statistics.median(all_residuals)
    lower_errors = [a - median_error for a in all_residuals if a < median_error]
    upper_errors = [a - median_error for a in all_residuals if a >= median_error]
    lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                        / (len(lower_errors) - 1))
    upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                        / (len(upper_errors) - 1))
    lower_kurtosis = one_tail_kurtosis(lower_errors)
    upper_kurtosis = one_tail_kurtosis(upper_errors)

    # re-do this with recent results
    # and include seat characteristics for regressions
    avg_mult_seat = {}
    rural_seat = {}
    provincial_seat = {}
    outer_metro_seat = {}
    home_state_seat = {}
    for region_name, region_results in dem_results.items():
        for seat_name, seat_results in region_results.items():
            max_mult = 0
            min_mult = 100
            mult_sum = 0
            mult_count = 0
            for cand in seat_results:
                if cand[0] > 2003 and cand[0] < 2017:
                    continue
                mult = cand[1] / dem_election_average[region_name, cand[0]]
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
            # Conviently the home state of all right-populist parties is QLD
            is_home_state = seat_regions.get((seat_name, region_name), '') == 'sa'
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

    # print(f'rural_coefficient: {rural_coefficient}')
    # print(f'provincial_coefficient: {provincial_coefficient}')
    # print(f'outer_metro_coefficient: {outer_metro_coefficient}')
    # print(f'home_state_coefficient: {home_state_coefficient}')
    # print(f'vote_intercept: {vote_intercept}')

    for seat_id, type in seat_types.items():
        if seat_id not in avg_mult_seat:
            avg_mult_seat[seat_id] = vote_intercept
            if type == 3:
                avg_mult_seat[seat_id] += rural_coefficient
            if type == 2:
                avg_mult_seat[seat_id] += provincial_coefficient
            if type == 1:
                avg_mult_seat[seat_id] += outer_metro_coefficient

    filename = (f'./Seat Statistics/statistics_centrist.csv')
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
        if seat_regions.get((key[0], key[1]), '') == 'sa':
            avg_mult_seat[key] -= home_state_coefficient

    filename = (f'./Seat Statistics/modifiers_centrist.csv')
    with open(filename, 'w') as f:
        for key, value in avg_mult_seat.items():
            f.write(f'{key[0]},{key[1]},{value:.4f}\n')


def analyse_others(elections):
    bucket_min = -90
    bucket_base = 10
    bucket_max = -50
    this_buckets = {(-10000, bucket_min): []}
    this_buckets.update({(a, a + bucket_base): [] for a in range(bucket_min, bucket_max, bucket_base)})
    this_buckets.update({(bucket_max, 10000): []})
    swing_buckets = copy.deepcopy(this_buckets)
    recontest_buckets = copy.deepcopy(this_buckets)
    for this_election, this_results in elections.items():
        # print(f'Gathering results for {party} in {this_election}')
        if len(elections.next_elections(this_election)) == 0:
            continue
        next_election = elections.next_elections(this_election)[0]
        next_results = elections[next_election]
        next_others_percent = total_others_vote_share(next_results)
        this_others_percent = total_others_vote_share(this_results)
        # print(f'Next election {next_election.short()} others vote share: {next_others_percent}')
        # print(f'This election {this_election.short()} others vote share: {this_others_percent}')
        election_swing = (transform_vote_share(next_others_percent)
                 - transform_vote_share(this_others_percent))
        # print(f'{this_election.short()} - {next_election.short()} overall swing to others: {election_swing}')
        for this_seat_name in this_results.seat_names():
            this_seat_results = this_results.seat_by_name(this_seat_name)
            if len(this_seat_results.tcp) == 0:
                continue  # ignore seats where candidates are unopposed
            if this_seat_name not in next_results.seat_names():
                continue
            next_seat_results = next_results.seat_by_name(this_seat_name)
            if len(next_seat_results.tcp) == 0:
                continue  # ignore seats where candidates are unopposed
            this_others = sum(a.percent for a in this_seat_results.fp
                              if effective_others(a.party,
                                                  this_results,
                                                  a.percent))
            next_others = sum(a.percent for a in next_seat_results.fp
                              if effective_others(a.party,
                                                  next_results,
                                                  a.percent))
            # Sometimes a seat won't have any "others" candidate at all,
            # or only a very poorly polling one, have a minimum floor
            # on the effective others vote to avoid this having a
            # disproportionate effect on results
            this_others = max(2, this_others)
            this_others = transform_vote_share(this_others)
            this_bucket = next(a for a in this_buckets
                                if a[0] < this_others
                                and a[1] > this_others)
            if next_others > 0:
                recontest_buckets[this_bucket].append(1)
            else:
                recontest_buckets[this_bucket].append(0)
                continue
            next_others = transform_vote_share(next_others)
            others_change = next_others - this_others
            this_buckets[this_bucket].append(others_change)
            swing_buckets[this_bucket].append(election_swing)

    bucket_counts = {}
    bucket_swing_coefficients = {}
    bucket_intercepts = {}
    bucket_median_errors = {}
    bucket_lower_rmses = {}
    bucket_upper_rmses = {}
    bucket_lower_kurtoses = {}
    bucket_upper_kurtoses = {}
    bucket_recontest_rates = {}

    for bucket, results in this_buckets.items():
        # Run regression between the seat swing and election swing
        # to find the relationship between the two for initial primary
        # votes in this bucket
        swings = swing_buckets[bucket]
        inputs_array = numpy.transpose(numpy.array([swings]))
        results_array = numpy.array(results)
        # print(swings)
        # print(results)
        # return
        reg = LinearRegression().fit(inputs_array, results_array)
        swing_coefficient = reg.coef_[0]
        overall_intercept = reg.intercept_

        # Get the residuals (~= errors if the above relationship is used
        # as a prediction), find the median, and split the errors into
        # a group above and below the median, measured by their distance
        # from the median
        residuals = [results[index] -
                        (swing_coefficient * swings[index]
                        + overall_intercept)
                        for index in range(0, len(results))
        ]
        median_error = statistics.median(residuals)
        lower_errors = [a - median_error for a in residuals if a < median_error]
        upper_errors = [a - median_error for a in residuals if a >= median_error]

        # Find effective RMSE and kurtosis for the two tails of the
        # distribution (in each case, as if the other side of the
        # distribution is symmetrical)
        lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                            / (len(lower_errors) - 1))
        upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                            / (len(upper_errors) - 1))
        lower_kurtosis = one_tail_kurtosis(lower_errors)
        upper_kurtosis = one_tail_kurtosis(upper_errors)

        bucket_counts[bucket] = len(results)
        bucket_swing_coefficients[bucket] = swing_coefficient
        bucket_intercepts[bucket] = overall_intercept
        bucket_median_errors[bucket] = median_error
        bucket_lower_rmses[bucket] = lower_rmse
        bucket_upper_rmses[bucket] = upper_rmse
        bucket_lower_kurtoses[bucket] = lower_kurtosis
        bucket_upper_kurtoses[bucket] = upper_kurtosis
        bucket_recontest_rates[bucket] = (recontest_buckets[bucket].count(1)
                                        / len(recontest_buckets[bucket]))

    # for bucket in bucket_swing_coefficients.keys():
    #     print(f'Primary vote bucket: {detransform_vote_share(bucket[0])} - {detransform_vote_share(bucket[1])}')
    #     print(f'Sample size: {bucket_counts[bucket]}')
    #     print(f'Election swing coefficient: {bucket_swing_coefficients[bucket]}')
    #     print(f'Intercept: {bucket_intercepts[bucket]}')
    #     print(f'Median error: {bucket_median_errors[bucket]}')
    #     print(f'Lower rmse: {bucket_lower_rmses[bucket]}')
    #     print(f'Upper rmse: {bucket_upper_rmses[bucket]}')
    #     print(f'Lower kurtosis: {bucket_lower_kurtoses[bucket]}')
    #     print(f'Upper kurtosis: {bucket_upper_kurtoses[bucket]}')
    #     print('\n')

    x = list(range(int(bucket_min - bucket_base / 2),
                   bucket_max + bucket_base,
                   bucket_base))
    swing_coefficients = [a for a in bucket_swing_coefficients.values()]
    spline = UnivariateSpline(x=x, y=swing_coefficients, s=10)
    smoothed_swing_coefficients = spline(x)
    smoothed_sophomore_coefficients = [0 for a in bucket_counts]
    offsets = [a + b for a, b in zip(bucket_intercepts.values(),
                                     bucket_median_errors.values())]
    spline = UnivariateSpline(x=x, y=offsets, s=10)
    smoothed_offsets = spline(x)
    lower_rmses = [a for a in bucket_lower_rmses.values()]
    spline = UnivariateSpline(x=x, y=lower_rmses, s=10)
    smoothed_lower_rmses = spline(x)
    upper_rmses = [a for a in bucket_upper_rmses.values()]
    spline = UnivariateSpline(x=x, y=upper_rmses, s=10)
    smoothed_upper_rmses = spline(x)
    lower_kurtoses = [a for a in bucket_lower_kurtoses.values()]
    spline = UnivariateSpline(x=x, y=lower_kurtoses, s=10)
    smoothed_lower_kurtoses = spline(x)
    upper_kurtoses = [a for a in bucket_upper_kurtoses.values()]
    spline = UnivariateSpline(x=x, y=upper_kurtoses, s=10)
    smoothed_upper_kurtoses = spline(x)
    recontest_rates = [a for a in bucket_recontest_rates.values()]
    spline = UnivariateSpline(x=x, y=recontest_rates, s=100)
    smoothed_recontest_rates = spline(x)
    recontest_incumbent_rates = [1 for a in bucket_counts]

    party_code = 'OTH'

    filename = (f'./Seat Statistics/statistics_{party_code}.csv')
    with open(filename, 'w') as f:
        f.write(','.join([f'{a:.4f}' for a in x]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_swing_coefficients]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_sophomore_coefficients]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_offsets]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_lower_rmses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_upper_rmses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_lower_kurtoses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_upper_kurtoses]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in smoothed_recontest_rates]) + '\n')
        f.write(','.join([f'{a:.4f}' for a in recontest_incumbent_rates]) + '\n')


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
    # print(f'Election count: {election_count}')
    # print(f'Emerging party count: {party_count}')

    residuals = [a - transform_vote_share(fp_threshold) for a in vote_shares]

    # one-tailed RMSE and kurtosis equivalent
    rmse = math.sqrt(sum([a ** 2 for a in residuals])
                        / (len(residuals) - 1))
    kurtosis = one_tail_kurtosis(residuals)

    # print(f'Transformed threshold: {transform_vote_share(fp_threshold)}')
    # print(f'2.5% untransformed: {detransform_vote_share(transform_vote_share(fp_threshold) + 2 * rmse)}')
    # print(f'0.15% untransformed: {detransform_vote_share(transform_vote_share(fp_threshold) + 3 * rmse)}')
    # print(f'Emergence rate: {emergence_rate}')
    # print(f'upper_rmse: {rmse}')
    # print(f'upper_kurtosis: {kurtosis}')

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
    target_year = 2022
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


def analyse_seat_swings(elections, seat_types, seat_regions):
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
            previous_seat_result = (previous_results.seat_by_name(this_seat_name,
                                    include_name_changes=True)
                                    if previous_results is not None else None)
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

            # Check previou results has a classic 2cp swing
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
                                                previous_swing_deviations_flat]))
    results_array = numpy.array(alp_deviations_flat)
    reg = LinearRegression().fit(inputs_array, results_array)
    retirement_urban = reg.coef_[0]
    retirement_regional = reg.coef_[1]
    sophomore_candidate_urban = reg.coef_[2]
    sophomore_candidate_regional = reg.coef_[3]
    sophomore_party_urban = reg.coef_[4]
    sophomore_party_regional = reg.coef_[5]
    previous_swing_modifier = reg.coef_[6]

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
            if len(values) < 10:
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




if __name__ == '__main__':
    elections = get_checked_elections()
    seat_types = load_seat_types()
    seat_regions = load_seat_regions()
    analyse_greens(elections)
    analyse_existing_independents(elections)
    analyse_emerging_independents(elections, seat_types)
    analyse_populist_minors(elections, seat_types, seat_regions)
    analyse_centrist_minors(elections, seat_types, seat_regions)
    analyse_others(elections)
    analyse_emerging_parties(elections)
    analyse_region_swings()
    analyse_seat_swings(elections, seat_types, seat_regions)
    print("Analysis completed.")