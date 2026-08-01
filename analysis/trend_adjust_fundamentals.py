"""Fit and validate the fundamentals component of trend adjustment."""

import math
import statistics

from numpy import array, transpose, dot, amax, amin
from sklearn.linear_model import ElasticNetCV

from trend_adjust_data import (
    ElectionPartyCode,
    TrendAdjustmentDataError,
    parties_with_unnamed_others,
)
from trend_adjust_io import save_fundamentals


def create_fundamentals_inputs(inputs, target_election, party):
    """Build the contextual predictors for one fundamentals estimate."""

    effective_party = 'ALP FP' if party == '@TPP' else party
    incumbent = 1 if inputs.incumbency[target_election][0] == effective_party else 0
    opposition = 1 if inputs.incumbency[target_election][1] == effective_party else 0
    incumbency_length = (inputs.incumbency[target_election][2]
                        if incumbent else 0)
    opposition_length = (inputs.incumbency[target_election][2]
                        if opposition else 0)
    federal = 1 if target_election.region() == 'fed' else 0
    if federal:
        federal_same = 0
        federal_opposite = 0
    else:
        federal_same = (inputs.federal_situation[target_election][2]
                        if inputs.federal_situation[target_election][0] == effective_party
                        else 1 - inputs.federal_situation[target_election][2])
        federal_opposite = (inputs.federal_situation[target_election][2]
                        if inputs.federal_situation[target_election][1] == effective_party
                        else 1 - inputs.federal_situation[target_election][2])
    return array([incumbent,
                  opposition,
                  incumbency_length,
                  opposition_length,
                  federal_same,
                  federal_opposite
                  ])


def preference_coefficients(inputs, election, party):
    """Return ALP-preference and continuing-vote shares per primary vote."""

    party_code = ElectionPartyCode(election, party)
    overall_others_code = ElectionPartyCode(election, 'OTH FP')
    if party_code in inputs.preference_estimates:
        preference_flow, exhaust_rate = inputs.preference_estimates[party_code]
    elif (party == inputs.party_groups.unnamed_others_code
          and overall_others_code in inputs.preference_estimates):
        preference_flow, exhaust_rate = (
            inputs.preference_estimates[overall_others_code]
        )
    elif party == 'ALP FP':
        preference_flow, exhaust_rate = 100, 0
    else:
        # Preserve the legacy fallback: unknown parties contribute to the
        # continuing total but supply no preferences to ALP.
        preference_flow, exhaust_rate = 0, 0

    continuing_share = (100 - exhaust_rate) * 0.01
    return preference_flow * 0.01 * continuing_share, continuing_share


def align_major_party_fundamentals_with_tpp(inputs, election, predictions):
    """Make major-party FP fundamentals imply the stronger TPP estimate.

    Preference flows convert each predicted primary vote into an ALP share of
    the non-exhausted two-party vote. Moving the two major primaries in
    opposite directions preserves their combined vote while shifting that
    implied TPP by the required amount.
    """

    if '@TPP' not in predictions:
        raise TrendAdjustmentDataError(
            f'{election.short()} fundamentals contain no @TPP prediction'
        )
    if ('ALP FP' not in predictions
            or not any(party in predictions for party in ('LNP FP', 'LIB FP'))):
        raise TrendAdjustmentDataError(
            f'{election.short()} fundamentals cannot be aligned with TPP '
            'without both major-party primary predictions'
        )
    if any(not math.isfinite(value) for value in predictions.values()):
        raise TrendAdjustmentDataError(
            f'{election.short()} fundamentals contain a non-finite value'
        )
    if not 0 < predictions['@TPP'] < 100:
        raise TrendAdjustmentDataError(
            f'{election.short()} fundamentals TPP must be between 0 and 100'
        )

    alp_preference_votes = 0.0
    non_exhausted_votes = 0.0
    for party, prediction in predictions.items():
        if party in ('OTH FP', '@TPP'):
            # Inclusive OTH would double-count the named and residual parties.
            continue
        alp_share, continuing_share = preference_coefficients(
            inputs, election, party
        )
        alp_preference_votes += prediction * alp_share
        non_exhausted_votes += prediction * continuing_share

    if not math.isfinite(non_exhausted_votes) or non_exhausted_votes <= 0:
        raise TrendAdjustmentDataError(
            f'{election.short()} fundamentals imply no non-exhausted vote'
        )
    coalition_party = 'LNP FP' if 'LNP FP' in predictions else 'LIB FP'
    alp_preference_share, alp_continuing_share = preference_coefficients(
        inputs, election, 'ALP FP'
    )
    coalition_preference_share, coalition_continuing_share = (
        preference_coefficients(inputs, election, coalition_party)
    )
    target_tpp = predictions['@TPP'] * 0.01
    preference_slope = (
        alp_preference_share - coalition_preference_share
    )
    continuing_slope = (
        alp_continuing_share - coalition_continuing_share
    )
    transfer_denominator = (
        preference_slope - target_tpp * continuing_slope
    )
    if abs(transfer_denominator) < 1e-12:
        raise TrendAdjustmentDataError(
            f'{election.short()} major-party preference settings cannot '
            'produce the target TPP'
        )
    # Solve (preference votes + transfer * preference_slope) /
    # (continuing votes + transfer * continuing_slope) = target TPP.
    primary_transfer = (
        target_tpp * non_exhausted_votes - alp_preference_votes
    ) / transfer_denominator
    predictions['ALP FP'] += primary_transfer
    predictions[coalition_party] -= primary_transfer
    if (not 0 < predictions['ALP FP'] < 100
            or not 0 < predictions[coalition_party] < 100):
        raise TrendAdjustmentDataError(
            f'{election.short()} TPP alignment produced an out-of-range '
            'major-party primary prediction'
        )


def build_fundamentals_training_set(
    inputs, studied_election, party_group_list, average_count
):
    """Build leave-one-election-out outcomes and contextual predictors."""

    result_deviations = []
    feature_columns = [[] for _ in range(6)]
    studied_is_federal = studied_election.region() == 'fed'
    for election in inputs.past_elections:
        if election == studied_election:
            continue
        # Federal and state fundamentals validate better as separate pools.
        if (election.region() == 'fed') != studied_is_federal:
            continue

        for party in parties_with_unnamed_others(
                inputs.past_parties[election],
                inputs.party_groups.unnamed_others_code):
            if party not in party_group_list:
                continue
            party_code = ElectionPartyCode(election, party)
            eventual_result = inputs.eventual_results.get(party_code, 0)
            result_deviations.append(
                eventual_result
                - inputs.safe_prior_average(average_count, party_code)
            )

            effective_party = 'ALP FP' if party == '@TPP' else party
            incumbent = int(
                inputs.incumbency[election][0] == effective_party
            )
            opposition = int(
                inputs.incumbency[election][1] == effective_party
            )
            feature_columns[0].append(incumbent)
            feature_columns[1].append(opposition)
            feature_columns[2].append(
                inputs.incumbency[election][2] if incumbent else 0
            )
            feature_columns[3].append(
                inputs.incumbency[election][2] if opposition else 0
            )
            feature_columns[4].append(int(
                not studied_is_federal
                and inputs.federal_situation[election][0] == effective_party
            ))
            feature_columns[5].append(int(
                not studied_is_federal
                and inputs.federal_situation[election][1] == effective_party
            ))

    return transpose(array(feature_columns)), array(result_deviations)


def fit_fundamentals_model(input_array, result_deviations):
    """Fit contextual effects, or a regularised mean when none vary."""

    if amax(input_array) > 0 or amin(input_array) < 0:
        regression = ElasticNetCV().fit(input_array, result_deviations)
        return regression.coef_, regression.intercept_

    coefficients = [0 for _ in input_array[0]]
    # Two zero deviations regularise sparse minor-party groups.
    intercept = statistics.mean([*result_deviations, 0, 0])
    return coefficients, intercept


def predict_fundamentals(
    inputs,
    studied_election,
    party,
    party_group_code,
    average_count,
    coefficients,
    intercept,
):
    """Apply the fitted model and established validation-based overrides."""

    party_code = ElectionPartyCode(studied_election, party)
    baseline = (
        inputs.safe_prior_average(average_count, party_code)
        if party_group_code != 'TPP'
        else 50
    )
    predictors = create_fundamentals_inputs(
        inputs, studied_election, party
    )
    prediction = baseline + dot(predictors, coefficients) + intercept

    if party_group_code == 'TPP' and studied_election.region() == 'fed':
        return 50
    if party_group_code == 'LNP' and studied_election.region() == 'fed':
        offset = (
            inputs.prior_results[
                ElectionPartyCode(studied_election, '@TPP')
            ][0] - 50
        )
        return inputs.prior_results[party_code][0] + offset
    if party_group_code == 'ALP' and studied_election.region() == 'fed':
        offset = (
            50 - inputs.prior_results[
                ElectionPartyCode(studied_election, '@TPP')
            ][0]
        )
        return inputs.prior_results[party_code][0] + offset
    if party_group_code in ('OTH', 'xOTH'):
        return inputs.safe_prior_average(average_count, party_code)
    return prediction


def print_fundamentals_validation(
    party_group_code,
    previous_errors,
    baseline_errors,
    prediction_errors,
):
    """Print leave-one-election-out comparisons for optional diagnostics."""

    if len(previous_errors) < 2:
        print(f'Insufficient validation data for {party_group_code}')
        return

    def root_sample_squared_error(errors):
        return math.sqrt(
            sum(error ** 2 for error in errors) / (len(errors) - 1)
        )

    print(f'Party group: {party_group_code}')
    print(previous_errors)
    print(
        'RMSEs: previous '
        f'{root_sample_squared_error(previous_errors)} vs baseline '
        f'{root_sample_squared_error(baseline_errors)} vs prediction '
        f'{root_sample_squared_error(prediction_errors)}'
    )
    print(
        'Average errors: previous '
        f'{statistics.mean(abs(error) for error in previous_errors)} '
        'vs baseline '
        f'{statistics.mean(abs(error) for error in baseline_errors)} '
        'vs prediction '
        f'{statistics.mean(abs(error) for error in prediction_errors)}'
    )
    print(
        'Median errors: previous '
        f'{statistics.median(abs(error) for error in previous_errors)} '
        'vs baseline '
        f'{statistics.median(abs(error) for error in baseline_errors)} '
        'vs prediction '
        f'{statistics.median(abs(error) for error in prediction_errors)}'
    )


def run_fundamentals_regression(
    config,
    inputs,
    excluded_election,
    output_directory='./Fundamentals',
):
    """Fit and validate fundamentals, then save the requested target."""

    to_file = {}
    for party_group_code, party_group_list in inputs.party_groups.groups.items():
        previous_errors = []
        prediction_errors = []
        baseline_errors = []
        avg_len = inputs.party_groups.average_lengths[party_group_code]
        for studied_election in inputs.past_elections + [excluded_election]:
            input_array, dependent_array = build_fundamentals_training_set(
                inputs,
                studied_election,
                party_group_list,
                avg_len,
            )
            if len(input_array) == 0:
                # No data for this party group, so can't do regression
                # If this is the excluded election, save a dummy file
                # based on the fact that a significant party should be getting
                # at least 3% of the vote to be included in analysis in the
                # first place
                if studied_election not in inputs.past_elections:
                    if studied_election not in to_file:
                        to_file[studied_election] = {}
                    for party in parties_with_unnamed_others(
                            inputs.all_parties[studied_election],
                            inputs.party_groups.unnamed_others_code):
                        if party not in party_group_list:
                            continue
                        to_file[studied_election][party] = 3
                continue
            coefs, intercept = fit_fundamentals_model(
                input_array, dependent_array
            )
            # Test with studied election information:
            for party in parties_with_unnamed_others(
                    inputs.all_parties[studied_election],
                    inputs.party_groups.unnamed_others_code):
                if party not in party_group_list:
                    continue
                e_p_c = ElectionPartyCode(studied_election, party)
                prediction = predict_fundamentals(
                    inputs,
                    studied_election,
                    party,
                    party_group_code,
                    avg_len,
                    coefs,
                    intercept,
                )
                if studied_election in inputs.past_elections:
                    # A missing result commonly means that the minor party did
                    # not contest, or contested too little of the state to be
                    # reported. Retaining it as a zero-like observation avoids
                    # selecting only the parties that survived and overstating
                    # minor-party prospects in current forecasts.
                    eventual_results = (inputs.eventual_results[e_p_c]
                                        if e_p_c in inputs.eventual_results else 0)
                    previous_errors.append(inputs.safe_prior_average(avg_len, e_p_c)
                                        - eventual_results)
                    baseline_errors.append((50 if party_group_code == "TPP" else 0)
                                        - eventual_results)
                    prediction_errors.append(prediction - eventual_results)
                    inputs.fundamentals[e_p_c] = prediction
                if studied_election not in inputs.past_elections:
                    # This means it's the excluded election, so want to
                    # save the fundamentals forecast for the main program to use
                    if studied_election not in to_file:
                        to_file[studied_election] = {}
                    to_file[studied_election][party] = prediction

        if config.show_fundamentals:
            print_fundamentals_validation(
                party_group_code,
                previous_errors,
                baseline_errors,
                prediction_errors,
            )
    
    # TPP fundamentals validate better than major-party FP fundamentals, so
    # use the TPP estimate to reconcile the two major primary predictions.
    for election, predictions in to_file.items():
        align_major_party_fundamentals_with_tpp(
            inputs, election, predictions
        )
                


    if config.show_fundamentals:
        for e_p_c, prediction in inputs.fundamentals.items():
            print(f'{e_p_c} - fundamentals prediction: {prediction}')
            if e_p_c in inputs.eventual_results:
                print(f'{e_p_c} - actual: {inputs.eventual_results[e_p_c]}')

    return save_fundamentals(
        to_file, output_directory=output_directory
    ).get(excluded_election)
