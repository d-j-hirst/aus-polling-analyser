"""Shared numerical helpers for historical election analysis.

Parent: election_analysis.py coordinates the party, regional and seat output
families. These helpers operate on checked-election results but do not own an
output family, allowing those analysis modules to share unchanged calculations.
"""

import math
import statistics

import numpy
from scipy.interpolate import UnivariateSpline
from sklearn.linear_model import LinearRegression

from poll_transform import detransform_vote_share, transform_vote_share
from sample_kurtosis import one_tail_kurtosis


ind_bucket_size = 2
fp_threshold = detransform_vote_share(int(math.floor(transform_vote_share(8)
    / ind_bucket_size)) * ind_bucket_size)
independent_others_limit = 8
larger_parties = [
    'Labor', 'Liberal', 'Liberal National', 'Greens', 'Democrats',
    'National', 'Nationals', 'One Nation', 'Country Liberal'
]

def extend_region_errors_with_selected_factor(
        region_errors, errors_by_factor, selected_factor):
    """Accumulate only residuals produced by the selected mixing factor."""

    for region, errors in errors_by_factor[selected_factor].items():
        region_errors.setdefault(region, []).extend(errors)


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
    """Return statewide vote represented by the seat-level Others model.

    Small parties are included below 3%. Independents are an exception because
    their statewide aggregate can be larger while still consisting of local,
    unrelated candidacies that belong in the same seat-level model.
    """

    votes = sum(
        votes
        for party, votes in election_results.fp_by_party.items()
        if (
            party not in larger_parties
            and (
                election_results.total_fp_percentage_party(party) < 3
                or (
                    party == "Independent"
                    and election_results.total_fp_percentage_party(party)
                    <= independent_others_limit
                )
            )
        )
    )
    return votes / election_results.total_fp_votes() * 100


def has_material_independent_vote(previous_vote, next_vote):
    """Whether either endpoint has enough independent support to model."""

    return max(previous_vote, next_vote) >= independent_others_limit
