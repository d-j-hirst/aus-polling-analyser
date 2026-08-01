"""Calculate poll/fundamentals mixes and forecast-error adjustments.

Parent: trend_adjust.py provides validated data and publishes this module's
mixed forecast-error estimates through the output stage.
"""

import math
import statistics

from numpy import average
from sklearn.metrics import mean_squared_error

from election_code import no_target_election_marker
from poll_transform import transform_vote_share, detransform_vote_share, clamp
from sample_kurtosis import one_tail_kurtosis
from trend_adjust_data import ElectionPartyCode, TREND_ADJUSTMENT_LEVELS
from trend_adjust_io import save_party_data


TREND_SIMILARITY_STDDEV = 15
MIXED_SIMILARITY_SCALE = 20
DIAGNOSTIC_ROWS_PER_RANKING = 5
MIX_GRID_INTERVALS = 10
MAX_MIX_CANDIDATE_INTERVALS = 2
MIX_SEARCH_TOLERANCE = 0.0001
GOLDEN_RATIO_CONJUGATE = (math.sqrt(5) - 1) / 2

# Symmetric pseudo-observations regularise sparse historical categories. The
# values are on the transformed vote-share scale.
PRIOR_ERRORS = {
    'ALP': 1.5,
    'LNP': 1.5,
    'Misc-c': 2,
    'Misc-p': 6,
    'OTH': 2.5,
    'xOTH': 3,
    'TPP': 1.5,
}


def smoothed_median(container, smoothing):
    s = sorted(container)
    n = len(s)
    high_mid = math.floor(n / 2)
    low_mid = high_mid - 1 if n % 2 == 0 else high_mid
    high_end = min(high_mid + smoothing + 1, n)
    low_end = max(low_mid - smoothing, 0)
    return statistics.mean(s[low_end:high_end])


def weighted_median(container, weights):
    if len(container) != len(weights):
        raise ValueError('weighted median values and weights differ in length')
    if any(not math.isfinite(value) for value in container):
        raise ValueError('weighted median values must be finite')
    if any(not math.isfinite(weight) for weight in weights):
        raise ValueError('weighted median weights must be finite')
    weighted_values = sorted(
        (value, weight) for value, weight in zip(container, weights)
        if weight > 0)
    if not weighted_values:
        raise statistics.StatisticsError(
            'weighted median requires at least one positive weight')
    halfway = sum(weight for _, weight in weighted_values) * 0.5
    cumulative = 0
    for index, (value, weight) in enumerate(weighted_values):
        cumulative += weight
        if cumulative > halfway:
            return value
        if cumulative == halfway and index + 1 < len(weighted_values):
            return statistics.mean(
                [value, weighted_values[index + 1][0]])
    return weighted_values[-1][0]


def trend_similarity(poll_value, target_trend):
    transformed_poll = transform_vote_share(poll_value)
    comparable_poll = clamp(
        transformed_poll,
        TREND_ADJUSTMENT_LEVELS[0],
        TREND_ADJUSTMENT_LEVELS[-1])
    distance = (
        (comparable_poll - target_trend) / TREND_SIMILARITY_STDDEV)
    return math.exp(-0.5 * distance ** 2), transformed_poll


class BiasData:
    """Historical errors and weights for one leave-one-out calculation."""

    def __init__(self):
        self.fundamentals_errors = []
        self.fundamentals_weights = []
        self.poll_errors = []
        self.poll_distance = []
        self.relevance = []
        self.poll_similarity = []
        self.studied_poll_errors = []
        self.studied_poll_parties = []
        self.poll_error_sources = []


def get_bias_data(exclude, inputs, poll_trend, party_group,
                  day, studied_election, target_trend):
    bias_data = BiasData()
    target_year = (inputs.reference_year
                   if studied_election == no_target_election_marker
                   else studied_election.year())
    for other_election in inputs.polled_elections:
        for party in inputs.party_groups.groups[party_group]:
            if (party != inputs.party_groups.unnamed_others_code
                    and party not in inputs.polled_parties[other_election]):
                continue
            party_code = ElectionPartyCode(other_election, party)
            polls = poll_trend.value_at(party_code, day, 50)
            has_eventual_result = party_code in inputs.eventual_results
            # Keep parties that disappeared or barely contested as low-result
            # observations. Dropping them would condition the adjustment on a
            # party having survived and bias current minor-party forecasts up.
            result = (inputs.eventual_results[party_code]
                      if has_eventual_result else 0.5)
            result_t = transform_vote_share(result)

            fundamentals = inputs.fundamentals[party_code] if party_code in inputs.fundamentals else None

            # The final forecast is explicitly a poll/fundamentals blend. Very
            # early elections without a fundamentals estimate cannot provide a
            # like-for-like historical error for this adjustment calculation.
            if fundamentals is not None:
                fundamentals_error = transform_vote_share(fundamentals) - result_t
                if polls is not None:
                    similarity, transformed_polls = trend_similarity(
                        polls, target_trend)
                    poll_error = transformed_polls - result_t
                    year_distance = abs(target_year - other_election.year())
                    relevance = (1 if exclude.region() == "fed"
                        and other_election.region() == "fed" else 0)
                    if other_election == studied_election:
                        bias_data.studied_poll_errors.append(poll_error)
                        bias_data.studied_poll_parties.append(party)
                    else:
                        bias_data.fundamentals_errors.append(
                            fundamentals_error)
                        bias_data.fundamentals_weights.append(similarity)
                        bias_data.poll_errors.append(poll_error)
                        bias_data.poll_distance.append(year_distance)
                        bias_data.relevance.append(relevance)
                        bias_data.poll_similarity.append(similarity)
                        bias_data.poll_error_sources.append(
                            (other_election, party, polls, result,
                             fundamentals, has_eventual_result,
                             transformed_polls, similarity))

    return bias_data


def print_poll_bias_diagnostics(
    exclude,
    party_group,
    target_trend,
    bias_data,
    weights,
    prior_poll_errors,
    poll_bias,
):
    """Explain the observations contributing most to a poll-bias fit."""

    historical_count = len(bias_data.poll_error_sources)
    weighted_sources = [
        (
            abs(error * weights[index]),
            error * weights[index],
            weights[index],
            error,
            source,
        )
        for index, (error, source) in enumerate(zip(
            bias_data.poll_errors[:historical_count],
            bias_data.poll_error_sources,
        ))
    ]
    weighted_sources.sort(reverse=True, key=lambda item: item[0])
    weighted_sources_by_weight = sorted(
        weighted_sources, reverse=True, key=lambda item: item[2]
    )

    def source_key(item):
        return (item[4][0], item[4][1])

    contribution_ranks = {
        source_key(item): rank
        for rank, item in enumerate(weighted_sources, 1)
    }
    weight_ranks = {
        source_key(item): rank
        for rank, item in enumerate(weighted_sources_by_weight, 1)
    }
    selected_keys = {
        source_key(item)
        for item in (
            weighted_sources[:DIAGNOSTIC_ROWS_PER_RANKING]
            + weighted_sources_by_weight[:DIAGNOSTIC_ROWS_PER_RANKING]
        )
    }
    selected_sources = [
        item for item in weighted_sources
        if source_key(item) in selected_keys
    ]

    print(f'\n*** {party_group} day-zero poll-bias diagnostics for '
          f'{exclude.short()}, target trend {target_trend:g} '
          f'(raw vote share '
          f'{detransform_vote_share(target_trend):.2f}%) ***')
    print('The first adjustment-file row is Poll Bias. Historical '
          'errors below are on the transformed vote-share scale.')
    print(f'Included historical observations: {historical_count}')
    defaulted_result_count = sum(
        not source[5] for source in bias_data.poll_error_sources
    )
    print(f'Observations using the 0.5% default result: '
          f'{defaulted_result_count}')
    print('Fundamentals are shown because a missing fundamentals '
          'prediction excludes the observation; their values do not '
          'enter Poll Bias directly.')
    print('Rows are the union of the top five absolute weighted '
          'contributions and top five final weights.')
    print('Contribution rank | Weight rank | Election | Party | Poll | '
          'Trend | Similarity | Result | Raw error | Fundamentals | '
          'Transformed error | Final weight | Weighted contribution')
    for _, contribution, weight, error, source in selected_sources:
        (election, party, polls, result, fundamentals,
         has_eventual_result, transformed_polls, similarity) = source
        key = (election, party)
        result_label = (f'{result:.4f}' if has_eventual_result
                        else f'{result:.4f} (default)')
        print(f'{contribution_ranks[key]} | {weight_ranks[key]} | '
              f'{election.short()} | {party} | {polls:.4f} | '
              f'{transformed_polls:+.4f} | {similarity:.4f} | '
              f'{result_label} | {polls - result:+.4f} | '
              f'{fundamentals:.4f} | {error:+.4f} | {weight:.4f} | '
              f'{contribution:+.4f}')

    omitted_sources = [
        item for item in weighted_sources
        if source_key(item) not in selected_keys
    ]
    if omitted_sources:
        omitted_contribution = sum(item[1] for item in omitted_sources)
        print(f'... {len(omitted_sources)} additional observations '
              f'omitted; combined weighted contribution '
              f'{omitted_contribution:+.4f}')

    historical_weighted_sum = sum(
        error * weights[index]
        for index, error in enumerate(
            bias_data.poll_errors[:historical_count]
        )
    )
    historical_weight_sum = sum(weights[:historical_count])
    positive_contribution = sum(
        max(0, item[1]) for item in weighted_sources
    )
    negative_contribution = sum(
        min(0, item[1]) for item in weighted_sources
    )
    prior_weights = weights[historical_count:]
    prior_weighted_sum = sum(
        error * weight
        for error, weight in zip(prior_poll_errors, prior_weights)
    )
    prior_weight_sum = sum(prior_weights)
    print(f'Symmetric prior errors: {prior_poll_errors}, '
          f'weights: {prior_weights}')
    print(f'Historical weighted sum: {historical_weighted_sum:+.4f}; '
          f'historical weight: {historical_weight_sum:.4f}')
    print(f'Positive contributions: {positive_contribution:+.4f}; '
          f'negative contributions: {negative_contribution:+.4f}')
    print(f'Prior weighted sum: {prior_weighted_sum:+.4f}; '
          f'prior weight: {prior_weight_sum:.4f}')
    print(f'Raw poll bias: ({historical_weighted_sum:+.4f} '
          f'{prior_weighted_sum:+.4f}) / '
          f'{historical_weight_sum + prior_weight_sum:.4f} '
          f'= {poll_bias:+.4f}\n')

    if weighted_sources:
        _, largest_contribution, largest_weight, _, largest_source = (
            weighted_sources[0]
        )
        largest_election, largest_party = largest_source[:2]
        bias_without_largest = (
            historical_weighted_sum - largest_contribution
            + prior_weighted_sum
        ) / (
            historical_weight_sum - largest_weight + prior_weight_sum
        )
        print(f'Raw poll bias without largest contributor '
              f'({largest_election.short()} {largest_party}): '
              f'{bias_without_largest:+.4f}\n')


class DayData:
    """Errors collected while selecting one day's poll/fundamentals mix."""

    def __init__(self, candidate_count=1):
        self.mixed_errors = [[] for _ in range(candidate_count)]
        self.mixed_weights = [[] for _ in range(candidate_count)]
        self.overall_poll_biases = []
        self.overall_fundamentals_biases = []
        self.final_mix_factor = 0


def get_single_election_data(
    exclude,
    inputs,
    poll_trend,
    party_group,
    day_data,
    day,
    studied_election,
    mix_factors,
    target_trend,
    diagnostics=False,
):
    """Add one leave-one-election-out observation to each candidate mix."""

    bias_data = get_bias_data(
                              exclude=exclude,
                              inputs=inputs,
                              poll_trend=poll_trend,
                              party_group=party_group,
                              day=day,
                              studied_election=studied_election,
                              target_trend=target_trend)
    weights = [10 * 2 ** -(val / 8) *
               (1 + 2 * bias_data.relevance[n]) *
               bias_data.poll_similarity[n]
               for n, val in enumerate(bias_data.poll_distance)]

    prior_fundamentals_errors = [
        PRIOR_ERRORS[party_group] * 2,
        -PRIOR_ERRORS[party_group] * 2,
    ]
    prior_error_single = (
        PRIOR_ERRORS[party_group] * (1 + math.sqrt(day / 100))
    )
    prior_poll_errors = [prior_error_single, -prior_error_single]

    bias_data.fundamentals_errors.extend(prior_fundamentals_errors)
    bias_data.fundamentals_weights.extend([1, 1])
    fundamentals_bias = average(
        bias_data.fundamentals_errors,
        weights=bias_data.fundamentals_weights)
    bias_data.poll_errors.extend(prior_poll_errors)
    weights.extend([20, 20])
    poll_bias = average(bias_data.poll_errors, weights=weights)
    if diagnostics:
        print_poll_bias_diagnostics(
            exclude=exclude,
            party_group=party_group,
            target_trend=target_trend,
            bias_data=bias_data,
            weights=weights,
            prior_poll_errors=prior_poll_errors,
            poll_bias=poll_bias,
        )
    if studied_election == no_target_election_marker:
        day_data.overall_fundamentals_biases = [fundamentals_bias]
        day_data.overall_poll_biases = [poll_bias]
        return
    if len(bias_data.studied_poll_errors) > 0:
        zipped_bias_data = zip(bias_data.studied_poll_errors,
                               bias_data.studied_poll_parties)
        for _, studied_poll_party in zipped_bias_data:
            party_code = ElectionPartyCode(studied_election,
                                           studied_poll_party)
            fundamentals = inputs.fundamentals[party_code]
            debiased_fundamentals = transform_vote_share(fundamentals) - fundamentals_bias
            polls = poll_trend.value_at(party_code, day, 50)
            debiased_polls = transform_vote_share(polls) - poll_bias
            similarity, _ = trend_similarity(polls, target_trend)
            result = (max(0.5, inputs.eventual_results[party_code])
                      if party_code in inputs.eventual_results else 0.5)
            result_t = transform_vote_share(result)
            for mix_index, mix_factor in enumerate(mix_factors):
                mixed = (debiased_polls * mix_factor
                         + debiased_fundamentals * (1 - mix_factor))
                mixed_error = mixed - result_t
                poll_distance = abs(
                    studied_election.year() - inputs.reference_year
                )
                relevance = (1 if exclude.region() == "fed"
                        and studied_election.region() == "fed" else 0)
                weight = 10 * 2 ** -(poll_distance / 12) * (1 + 3 * relevance)
                weight *= similarity * MIXED_SIMILARITY_SCALE
                day_data.mixed_errors[mix_index].append(mixed_error)
                day_data.mixed_weights[mix_index].append(weight)


def find_best_mix(evaluate):
    """Find the best mix with a coarse grid followed by local refinement.

    The grid protects against selecting the wrong broad basin when the error
    objective is not unimodal. Golden-section search then gives high precision
    within the best grid interval while requiring only one new evaluation per
    refinement step. Results are cached because each evaluation traverses all
    historical elections.
    """

    evaluations = {}

    def cached_evaluate(mix_factor):
        mix_factor = clamp(mix_factor, 0, 1)
        if mix_factor not in evaluations:
            evaluations[mix_factor] = evaluate(mix_factor)
        return evaluations[mix_factor]

    grid = [index / MIX_GRID_INTERVALS
            for index in range(MIX_GRID_INTERVALS + 1)]
    grid_scores = [cached_evaluate(factor)[0] for factor in grid]
    local_minima = [
        index for index, score in enumerate(grid_scores)
        if (index == 0 or score <= grid_scores[index - 1])
        and (index == MIX_GRID_INTERVALS
             or score <= grid_scores[index + 1])
    ]
    local_minima.sort(key=lambda index: (grid_scores[index], grid[index]))
    candidate_intervals = []
    for best_index in local_minima:
        interval = (
            grid[max(0, best_index - 1)],
            grid[min(MIX_GRID_INTERVALS, best_index + 1)],
        )
        if interval not in candidate_intervals:
            candidate_intervals.append(interval)
        if len(candidate_intervals) == MAX_MIX_CANDIDATE_INTERVALS:
            break

    for left, right in candidate_intervals:
        inner_left = right - GOLDEN_RATIO_CONJUGATE * (right - left)
        inner_right = left + GOLDEN_RATIO_CONJUGATE * (right - left)
        left_score = cached_evaluate(inner_left)[0]
        right_score = cached_evaluate(inner_right)[0]
        while right - left > MIX_SEARCH_TOLERANCE:
            if left_score <= right_score:
                right = inner_right
                inner_right = inner_left
                right_score = left_score
                inner_left = right - GOLDEN_RATIO_CONJUGATE * (right - left)
                left_score = cached_evaluate(inner_left)[0]
            else:
                left = inner_left
                inner_left = inner_right
                left_score = right_score
                inner_right = left + GOLDEN_RATIO_CONJUGATE * (right - left)
                right_score = cached_evaluate(inner_right)[0]
        cached_evaluate(statistics.mean((left, right)))
    best_factor, (_, best_data) = min(
        evaluations.items(), key=lambda item: (item[1][0], item[0])
    )
    return best_factor, best_data


def get_day_data(exclude, inputs, poll_trend, party_group, day,
                 target_trend, diagnostics=False):
    """Select the poll/fundamentals mix for one forecast horizon."""

    diagnostics_pending = diagnostics

    def evaluate(mix_factor):
        nonlocal diagnostics_pending
        day_data = DayData()
        for studied_election in inputs.studied_elections:
            get_single_election_data(
                exclude=exclude,
                inputs=inputs,
                poll_trend=poll_trend,
                party_group=party_group,
                day=day,
                studied_election=studied_election,
                day_data=day_data,
                mix_factors=(mix_factor,),
                target_trend=target_trend,
                diagnostics=(
                    diagnostics_pending
                    and studied_election == no_target_election_marker
                ),
            )
        diagnostics_pending = False

        prior_error_single = (
            PRIOR_ERRORS[party_group] * (1 + math.sqrt(day / 100))
        )
        day_data.mixed_errors[0].extend(
            [prior_error_single, -prior_error_single]
        )
        day_data.mixed_weights[0].extend([150, 150])

        mixed_rmse = math.sqrt(
            mean_squared_error(
                day_data.mixed_errors[0],
                [0 for _ in day_data.mixed_errors[0]],
                sample_weight=day_data.mixed_weights[0],
            )
        )
        mixed_average_error = average(
            [abs(error) for error in day_data.mixed_errors[0]],
            weights=day_data.mixed_weights[0],
        )
        criterion = mixed_rmse * 0.6 + mixed_average_error * 0.4
        # Preserve the established preference towards an endpoint: lower
        # factors are biased moderately towards zero and higher factors
        # slightly towards one.
        criterion -= mix_factor * (mix_factor - 1.6)
        return criterion, day_data

    best_factor, day_data = find_best_mix(evaluate)
    day_data.final_mix_factor = best_factor
    return day_data


class PartyData:
    """Sparse triangular-day parameter series for one party group."""

    def __init__(self):
        self.poll_biases = {}
        self.fundamentals_biases = {}
        self.mixed_biases = {}
        self.lower_rmses = {}
        self.upper_rmses = {}
        self.lower_kurtoses = {}
        self.upper_kurtoses = {}
        self.final_mix_factors = {}


def one_sided_error_parameters(errors, weights, lower_tail):
    """Return RMSE and kurtosis for one side of forecast errors."""

    selected = [
        (error, weight)
        for error, weight in zip(errors, weights)
        if (error >= 0) == lower_tail
    ]
    if not selected:
        tail_name = 'lower' if lower_tail else 'upper'
        raise TrendAdjustmentDataError(
            f'No observations were available for the {tail_name} error tail'
        )
    selected_errors = [error for error, _ in selected]
    selected_weights = [weight for _, weight in selected]
    rmse = math.sqrt(mean_squared_error(
        selected_errors,
        [0 for _ in selected_errors],
        sample_weight=selected_weights,
    ))
    kurtosis = one_tail_kurtosis(
        selected_errors,
        weights=selected_weights,
        weight_scale=50,
    )
    return rmse, kurtosis


def get_party_data(config, exclude, inputs, poll_trend, party_group,
                   target_trend):
    """Calculate sparse adjustment parameters for one support anchor."""

    party_data = PartyData()
    for day in config.days:
        day_data = get_day_data(exclude=exclude,
                                inputs=inputs,
                                poll_trend=poll_trend,
                                party_group=party_group,
                                day=day,
                                target_trend=target_trend,
                                diagnostics=(
                                    config.diagnostics_enabled_for(party_group)
                                    and day == 0))
        poll_bias = smoothed_median(
            day_data.overall_poll_biases, 2)
        fundamentals_bias = smoothed_median(
            day_data.overall_fundamentals_biases, 2)
        mixed_bias = weighted_median(
            day_data.mixed_errors[0],
            day_data.mixed_weights[0])
        # Positive forecast-minus-result errors populate the lower outcome
        # tail; negative errors populate the upper outcome tail.
        lower_rmse, lower_kurtosis = one_sided_error_parameters(
            day_data.mixed_errors[0],
            day_data.mixed_weights[0],
            lower_tail=True,
        )
        upper_rmse, upper_kurtosis = one_sided_error_parameters(
            day_data.mixed_errors[0],
            day_data.mixed_weights[0],
            lower_tail=False,
        )
        party_data.poll_biases[day] = poll_bias
        party_data.fundamentals_biases[day] = fundamentals_bias
        party_data.mixed_biases[day] = mixed_bias
        party_data.lower_rmses[day] = lower_rmse
        party_data.upper_rmses[day] = upper_rmse
        party_data.lower_kurtoses[day] = lower_kurtosis
        party_data.upper_kurtoses[day] = upper_kurtosis
        party_data.final_mix_factors[day] = day_data.final_mix_factor
    return party_data


def generate_adjustments(
    config,
    inputs,
    poll_trend,
    exclude,
    output_directory='./Adjustments',
):
    """Generate and save every configured party-group adjustment grid."""

    output_paths = {}
    for party_group in inputs.party_groups.groups.keys():
        print(f'*** DETERMINING TREND ADJUSTMENTS FOR PARTY GROUP'
              f' {party_group} ***')
        party_data_by_level = {}
        for target_trend in TREND_ADJUSTMENT_LEVELS:
            party_data_by_level[target_trend] = get_party_data(
                config=config,
                exclude=exclude,
                inputs=inputs,
                poll_trend=poll_trend,
                party_group=party_group,
                target_trend=target_trend)
        output_paths[party_group] = save_party_data(
            config=config,
            party_data_by_level=party_data_by_level,
            exclude=exclude,
            party_group=party_group,
            output_directory=output_directory)
    return output_paths

