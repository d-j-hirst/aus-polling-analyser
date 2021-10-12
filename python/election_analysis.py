from election_code import ElectionCode
import copy
import math
import numpy
import statistics
from sklearn.linear_model import LinearRegression
from scipy.interpolate import UnivariateSpline
from election_check import get_checked_elections
from poll_transform import transform_vote_share, detransform_vote_share, clamp
from sample_kurtosis import one_tail_kurtosis

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


larger_parties = ['Labor', 'Liberal', 'Liberal National', 'Greens', 'Democrats', 'National', 'Nationals', 'One Nation']


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
                     or party == "Independent"))
    return votes / election_results.total_fp_votes() * 100


def analyse_existing_independents(elections):
    # Note: Covers "effective independents" (those who are technically
    # affiliated with a party but mainly dependent on their personal vote)
    bucket_min = -50
    bucket_base = 15
    bucket_max = -5
    this_buckets = {(-10000, bucket_min): []}
    this_buckets.update({(a, a + bucket_base): [] for a in range(bucket_min, bucket_max, bucket_base)})
    this_buckets.update({(bucket_max, 10000): []})
    sophomore_buckets = copy.deepcopy(this_buckets)
    recontest_buckets = copy.deepcopy(this_buckets)
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
            else:
                recontest_buckets[this_bucket].append(0)
                continue
            # print(f' Found independent for seat {this_seat_name}: {matching_next}')
            next_fp = transform_vote_share(next_fp)
            fp_change = next_fp - this_fp
            this_buckets[this_bucket].append(fp_change)
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
    bucket_recontest_rates = {}
    
    for bucket in ordered_buckets:
        # Run regression between the seat swing and election swing
        # to find the relationship between the two for initial primary
        # votes in this bucket
        sophomores = sophomore_buckets[bucket]
        inputs_array = numpy.transpose(numpy.array([sophomores]))
        results_array = numpy.array(this_buckets[bucket])
        reg = LinearRegression().fit(inputs_array, results_array)
        sophomore_coefficient = reg.coef_[0]
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
        bucket_recontest_rates[bucket] = (recontest_buckets[bucket].count(1)
                                        / len(recontest_buckets[bucket]))
    
    for bucket_index in range(len(ordered_buckets) - 2, -1, -1):
        bucket = ordered_buckets[bucket_index]
        if not 1 in sophomore_buckets[bucket]:
            next_bucket = ordered_buckets[bucket_index + 1]
            bucket_sophomore_coefficients[bucket] = \
                bucket_sophomore_coefficients[next_bucket]

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
            seat_ind_count.append(len(new_independents))
            fed = 1 if next_election.region() == 'fed' else 0
            this_others = sum([min(a.percent, fp_threshold) for a in this_seat_results.fp
                               if a.party not in ['Labor', 'Liberal', 'Greens', 'National']])
            others_indicator = max(2, this_others)
            seat_fed.append(fed)
            seat_type = seat_types.get((this_seat_name, next_election.region()), -1)
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

    filename = (f'./Seat Statistics/statistics_populist.csv')
    with open(filename, 'w') as f:
        f.write(f'{lower_rmse}\n')
        f.write(f'{upper_rmse}\n')
        f.write(f'{lower_kurtosis}\n')
        f.write(f'{upper_kurtosis}\n')

    # re-do this with recent results
    # and include seat characteristics for regressions
    avg_mult_seat = {}
    rural_seat = {}
    provincial_seat = {}
    outer_metro_seat = {}
    # Conviently the home state of all right-populist parties is QLD
    qld_seat = {}
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
            is_qld = seat_regions.get((seat_name, region_name), '') == 'qld'
            avg_mult_seat[seat_id] = mult_sum / mult_count
            rural_seat[seat_id] = 1 if seat_type == 3 else 0
            provincial_seat[seat_id] = 1 if seat_type == 2 else 0
            outer_metro_seat[seat_id] = 1 if seat_type == 1 else 0
            qld_seat[seat_id] = 1 if is_qld else 0
    
    avg_mult_list = [avg_mult_seat[key] for key in sorted(avg_mult_seat.keys())]
    rural_list = [rural_seat[key] for key in sorted(avg_mult_seat.keys())]
    provincial_list = [provincial_seat[key] for key in sorted(avg_mult_seat.keys())]
    outer_metro_list = [outer_metro_seat[key] for key in sorted(avg_mult_seat.keys())]
    qld_list = [qld_seat[key] for key in sorted(avg_mult_seat.keys())]
    inputs_array = numpy.transpose(numpy.array([rural_list, provincial_list, outer_metro_list, qld_list]))
    results_array = numpy.array(avg_mult_list)
    reg = LinearRegression().fit(inputs_array, results_array)
    rural_coefficient = reg.coef_[0]
    provincial_coefficient = reg.coef_[1]
    outer_metro_coefficient = reg.coef_[2]
    qld_coefficient = reg.coef_[3]
    vote_intercept = reg.intercept_

    print(f'rural_coefficient: {rural_coefficient}')
    print(f'provincial_coefficient: {provincial_coefficient}')
    print(f'outer_metro_coefficient: {outer_metro_coefficient}')
    print(f'qld_coefficient: {qld_coefficient}')
    print(f'vote_intercept: {vote_intercept}')

    for seat_id, type in seat_types.items():
        if seat_id not in avg_mult_seat:
            avg_mult_seat[seat_id] = vote_intercept
            if type == 3:
                avg_mult_seat[seat_id] += rural_coefficient
            if type == 2:
                avg_mult_seat[seat_id] += provincial_coefficient
            if type == 1:
                avg_mult_seat[seat_id] += outer_metro_coefficient
            if seat_regions in seat_id:
                if seat_regions[seat_id] == 'qld':
                    avg_mult_seat[seat_id] += qld_coefficient

    filename = (f'./Seat Statistics/modifiers_populist.csv')
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

    filename = (f'./Seat Statistics/statistics_emerging_party.csv')
    with open(filename, 'w') as f:
        f.write(f'{fp_threshold}\n')
        f.write(f'{emergence_rate}\n')
        f.write(f'{rmse}\n')
        f.write(f'{kurtosis}\n')

if __name__ == '__main__':
    elections = get_checked_elections()
    seat_types = load_seat_types()
    seat_regions = load_seat_regions()
    analyse_greens(elections)
    analyse_existing_independents(elections)
    analyse_emerging_independents(elections, seat_types)
    analyse_populist_minors(elections, seat_types, seat_regions)
    analyse_others(elections)
    analyse_emerging_parties(elections)