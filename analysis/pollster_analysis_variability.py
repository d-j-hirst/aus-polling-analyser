"""Reduce calibration errors into pollster variability parameters.

Parent: pollster_analysis.py stages this output with the house-effect and bias
files, publishing all three only after the target election succeeds.

Main functions:
* ``analyse_variability`` performs the weighted leave-one-out error reduction
  and writes per-pollster variability parameters for one target election.
"""

import math
from pollster_analysis_common import (
    LIBERAL_PARTY,
    canonical_party,
    check_dates,
    output_party,
)


# Core variability reduction

def analyse_variability(
    target_election, cycles, links, evidence, output_path
):
    # The trend calibration process for each prior election has to be done
    # before performing this analysis (via fp_model.py --calibrate)
    print("Analysing variability")
    # This dictionary will contain the weighted error sums for each
    # pollster/party combination
    weighted_error_sums = {}
    # This dictionary will contain the weight sums for each pollster/party
    # combination
    weight_sums = {}
    lib = False

    # Get all the different pollster/party combinations
    # that are actually needed for this election
    # and establish a prior expectation for each
    for source in evidence.bias_for_election(target_election):
        if source.party == LIBERAL_PARTY:
            lib = True
        party = canonical_party(source.party)
        for pollster in source.house_effects:
            key = (pollster, party)
            # Add a small amount to the weight when a new pollster/party is
            # encountered to establish prior expectation and avoid overfitting
            # to the first few data points: mean absolute error 2 over seven
            # pseudo-observations.
            weighted_error_sums[key] = 14
            weight_sums[key] = 7

    for record in evidence.leave_one_out:
        # The file we have excludes a particular pollster from the calibration
        # so that we can see the variability of that pollster from the trend
        # line created from all other pollsters
        # Don't use elections from the future
        if not check_dates(
            record.election, target_election, cycles, equals=True
        ):
            continue
        key = (record.pollster, canonical_party(record.party))
        # If this pollster/party isn't in the dictionary, skip it as we don't
        # need it for the target election.
        if key not in weighted_error_sums:
            continue
        weighted_error_sums[key] += (
            record.error_weight * record.weighted_abs_error
        )
        weight_sums[key] += record.error_weight

    linked_keys = set(sum(links.values(), []))
    with open(output_path, 'w') as f:
        # Store the standard deviation in error and sum of weights
        # for each pollster/party combination
        for key in sorted(weight_sums.keys()):
            # Don't do linked pollsters yet
            if key[0] in linked_keys: continue
            weight_sum = weight_sums[key]
            weighted_error_sum = weighted_error_sums[key]
            error_average = weighted_error_sum / weight_sum
            error_stddev = error_average / math.sqrt(2 / math.pi)
            # Adjust data for any linked pollsters
            if key[0] in links:
                linked = links[key[0]]
                for linked_pollster in linked:
                    linked_key = (linked_pollster, key[1])
                    if linked_key not in weight_sums: continue
                    link_weight = 21
                    weight_sums[linked_key] += link_weight
                    weighted_error_sums[linked_key] += error_average * link_weight
            party = output_party(key[1], lib)
            f.write(f'{key[0]},{party},{error_stddev},{weight_sum - 7}\n')
        for key in sorted(weight_sums.keys()):
            # Non-linked pollsters have already been done
            if key[0] not in linked_keys: continue
            weight_sum = weight_sums[key]
            weighted_error_sum = weighted_error_sums[key]
            error_average = weighted_error_sum / weight_sum
            error_stddev = error_average / math.sqrt(2 / math.pi)
            party = output_party(key[1], lib)
            f.write(f'{key[0]},{party},{error_stddev},{weight_sum - 7}\n')
    
    print('Variability analysis successfully completed')
