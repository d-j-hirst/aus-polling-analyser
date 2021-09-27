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
        print(f'Gathering results for {party} in {this_election}')
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
        print(f'{this_election.short()} - {next_election.short()} overall swing to greens: {election_swing}')
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
                    print(f'Sophomore found: {this_seat_name}')
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
        if len(results) > 0:
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
    
    for bucket in bucket_swing_coefficients.keys():
        print(f'Primary vote bucket: {detransform_vote_share(bucket[0])} - {detransform_vote_share(bucket[1])}')
        print(f'Sample size: {bucket_counts[bucket]}')
        print(f'Election swing coefficient: {bucket_swing_coefficients[bucket]}')
        print(f'Sophomore coefficient: {bucket_sophomore_coefficients[bucket]}')
        print(f'Intercept: {bucket_intercepts[bucket]}')
        print(f'Median error: {bucket_median_errors[bucket]}')
        print(f'Lower rmse: {bucket_lower_rmses[bucket]}')
        print(f'Upper rmse: {bucket_upper_rmses[bucket]}')
        print(f'Lower kurtosis: {bucket_lower_kurtoses[bucket]}')
        print(f'Upper kurtosis: {bucket_upper_kurtoses[bucket]}')
        print('\n')
    
    print(bucket_min - bucket_base / 2)
    print(bucket_max + bucket_base)
    print(bucket_base)
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


def effective_independent(party, this_election):
    if party in ['Labor', 'Liberal', 'Greens', 'Democrats', 'National', 'One Nation']:
        return False
    if (party == "Katter's Australian"
        and this_election.region() == "qld"):
        return False
    if (party == 'Centre Alliance'
        and this_election.region() == "sa"):
        return False
    return True


def analyse_existing_independents(elections):
    # Note: Covers "effective independents" (those who are technically
    # standing for a party but mainly dependent on their)
    bucket_min = -50
    bucket_base = 15
    bucket_max = -5
    this_buckets = {(-10000, bucket_min): []}
    this_buckets.update({(a, a + bucket_base): [] for a in range(bucket_min, bucket_max, bucket_base)})
    this_buckets.update({(bucket_max, 10000): []})
    swing_buckets = copy.deepcopy(this_buckets)
    sophomore_buckets = copy.deepcopy(this_buckets)
    recontest_buckets = copy.deepcopy(this_buckets)
    party = 'Independent'
    for this_election, this_results in elections.items():
        print(f'\nGathering results for {party} in {this_election}')
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
                                                        this_election)
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
                                                previous_election)
                            or previous_seat_results.tcp[0].name != 
                            this_seat_results.tcp[0].name)):
                    sophomore = True
                    print(f'Sophomore found: {this_seat_name}')
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

    for bucket in bucket_counts.keys():
        print(f'Primary vote bucket: {detransform_vote_share(bucket[0])} - {detransform_vote_share(bucket[1])}')
        print(f'Sample size: {bucket_counts[bucket]}')
        print(f'Sophomore coefficient: {bucket_sophomore_coefficients[bucket]}')
        print(f'Intercept: {bucket_intercepts[bucket]}')
        print(f'Median error: {bucket_median_errors[bucket]}')
        print(f'Lower rmse: {bucket_lower_rmses[bucket]}')
        print(f'Upper rmse: {bucket_upper_rmses[bucket]}')
        print(f'Lower kurtosis: {bucket_lower_kurtoses[bucket]}')
        print(f'Upper kurtosis: {bucket_upper_kurtoses[bucket]}')
        print(f'Recontest rate: {bucket_recontest_rates[bucket]}')
        print('\n')
    
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
    print(seat_types)
    return seat_types


def analyse_emerging_independents(elections, seat_types):
    seat_ind_count = []
    seat_fed = []
    seat_rural = []
    seat_provincial = []
    seat_outer_metro = []
    seat_rural = []
    cand_fp_vote = []
    cand_fed = []
    cand_rural = []
    cand_provincial = []
    cand_outer_metro = []
    party = 'Independent'
    for this_election, this_results in elections.items():
        print(f'\nGathering results for emerging {party} in {this_election}')
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
                         and effective_independent(a.party, this_election)]
            new_independents = [a for a in next_seat_results.fp
                                if effective_independent(a.party, next_election)
                                and a.name not in old_names
                                and a.percent > fp_threshold]
            seat_ind_count.append(len(new_independents))
            fed = 1 if next_election.region() == 'fed' else 0
            seat_fed.append(fed)
            seat_type = seat_types.get((this_seat_name, next_election.region()), -1)
            seat_rural.append(1 if seat_type == 3 else 0)
            seat_provincial.append(1 if seat_type == 2 else 0)
            seat_outer_metro.append(1 if seat_type == 1 else 0)
            for candidate in new_independents:
                # print(f'Found emerging independent - {candidate} in {this_seat_name}')
                cand_fp_vote.append(transform_vote_share(candidate.percent))
                cand_fed.append(fed)
                cand_rural.append(1 if seat_type == 3 else 0)
                cand_provincial.append(1 if seat_type == 2 else 0)
                cand_outer_metro.append(1 if seat_type == 1 else 0)

    for count in range(0, max(seat_ind_count) + 1):
        print (f'Independent count {count}: {seat_ind_count.count(count)}')

    for count in range(0, 2):
        print (f'Federal count {count}: {seat_fed.count(count)}')

    inputs_array = numpy.transpose(numpy.array([seat_fed, seat_rural, seat_provincial, seat_outer_metro]))
    results_array = numpy.array(seat_ind_count)
    reg = LinearRegression().fit(inputs_array, results_array)
    fed_coefficient = reg.coef_[0]
    rural_coefficient = reg.coef_[1]
    provincial_coefficient = reg.coef_[2]
    outer_metro_coefficient = reg.coef_[3]
    intercept = reg.intercept_
    print(f'Federal emergence coefficient: {fed_coefficient}')
    print(f'Rural emergence coefficient: {rural_coefficient}')
    print(f'Provincial emergence coefficient: {provincial_coefficient}')
    print(f'Outer Metro emergence coefficient: {outer_metro_coefficient}')
    print(f'Emergence intercept: {intercept}')

    fp_vote_buckets = {}
    for index in range(0, len(cand_fp_vote)):
        fp_vote = cand_fp_vote[index]
        fp_vote_bucket = int(math.floor(fp_vote / ind_bucket_size)) * ind_bucket_size
        if fp_vote_bucket in fp_vote_buckets:
            fp_vote_buckets[fp_vote_bucket] += 1
        else:
            fp_vote_buckets[fp_vote_bucket] = 1

    print ('')
    for bucket in sorted(fp_vote_buckets.keys()):
        print (f'Fp vote in range {bucket} - {bucket + ind_bucket_size}: '
               f'{fp_vote_buckets[bucket]}')

    inputs_array = numpy.transpose(numpy.array([cand_fed, cand_rural, cand_provincial, cand_outer_metro]))
    results_array = numpy.array(cand_fp_vote)
    reg = LinearRegression().fit(inputs_array, results_array)
    fed_vote_coefficient = reg.coef_[0]
    rural_vote_coefficient = reg.coef_[1]
    provincial_vote_coefficient = reg.coef_[2]
    outer_metro_vote_coefficient = reg.coef_[3]
    vote_intercept = reg.intercept_
    print(f'Federal vote coefficient: {fed_vote_coefficient}')
    print(f'Rural vote coefficient: {rural_vote_coefficient}')
    print(f'Provincial vote coefficient: {provincial_vote_coefficient}')
    print(f'Outer Metro vote coefficient: {outer_metro_vote_coefficient}')
    print(f'Vote intercept: {vote_intercept}')
    print(f'fp threshold: {fp_threshold}')


if __name__ == '__main__':
    elections = get_checked_elections()
    seat_types = load_seat_types()
    analyse_greens(elections)
    analyse_existing_independents(elections)
    analyse_emerging_independents(elections, seat_types)