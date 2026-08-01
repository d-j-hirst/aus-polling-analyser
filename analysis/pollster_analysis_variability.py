"""Reduce calibration errors into pollster variability parameters.

Parent: pollster_analysis.py stages this output with the house-effect and bias
files, publishing all three only after the target election succeeds.
"""

import math

from election_code import ElectionCode
from pollster_analysis_common import (
    COALITION_PARTY,
    LIBERAL_PARTY,
    ConfigError,
    canonical_party,
    check_dates,
    directory,
    output_party,
    parse_finite_float,
)


def analyse_variability(
    target_election, cycles, links, filenames, output_path
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
    for filename in filenames:
        if 'biascal' not in filename or 'polls' not in filename: continue
        election = filename.split('.')[0].split('_')[2]
        year = int(election[:4])
        region = election[4:]
        if year != target_election.year(): continue
        if region != target_election.region(): continue
        party = filename.split('.')[0].split('_')[3]
        if party == LIBERAL_PARTY:
            lib = True
        party = canonical_party(party)
        with open(f'{directory}/{filename}', 'r') as f:
            data = f.readlines()[1:]
            for line in data:
                pollster = line.split(',')[0]
                key = (pollster, party)
                # Add a small amount to the weight when a new pollster/party is
                # encountered to establish prior expectation and avoid
                # overfitting to the first few data points
                # A mean absolute error of 2 over seven pseudo-observations.
                weighted_error_sums[key] = 14
                weight_sums[key] = 7


    for filename in filenames:
        if (filename[:5]) != 'calib': continue
        # The file we have excludes a particular pollster from the calibration
        # so that we can see the variability of that pollster from the trend
        # line created from all other pollsters
        _, election, pollster, party = filename.split(".")[0].split('_')
        party = canonical_party(party)
        election = ElectionCode(int(election[:4]), election[4:])
        # Don't use elections from the future
        if not check_dates(election, target_election, cycles, equals=True): continue
        key = (pollster, party)
        with open(f'{directory}/{filename}', 'r') as f:
            # The first line of the file contains the weighted error and weight
            # The rest of the file is not required for this analysis
            # (it contains the deviations and weights for each poll)
            stat_strs = f.readline().split(',')[:2]
            if len(stat_strs) < 2:
                raise ConfigError(
                    '{} lacks its weighted error summary.'.format(
                        filename
                    )
                )
            error = parse_finite_float(
                stat_strs[0], '{} weighted error'.format(filename)
            )
            weight = parse_finite_float(
                stat_strs[1], '{} weight'.format(filename)
            )
            if error < 0 or weight < 0:
                raise ConfigError(
                    '{} contains a negative error or weight.'.format(
                        filename
                    )
                )
            # If this pollster/party isn't in the dictionary, skip it
            # as we don't need it for the target election
            if key not in weighted_error_sums:
                continue
            # Add the weighted error and weight to the dictionaries
            weighted_error_sums[key] += weight * error
            weight_sums[key] += weight

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


