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
        print(f'Gathering results for {this_election}')
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
            if this_seat_name in next_results.seat_names():
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
            if (bucket[0] == bucket_max):
                print(swings)
                print(sophomores)
                print(results)
    
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
    # don't smooth the sophomore coefficients as most of them are meaningless
    sophomore_coefficients = [a for a in bucket_sophomore_coefficients.values()]
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


if __name__ == '__main__':
    elections = get_checked_elections()
    analyse_greens(elections)