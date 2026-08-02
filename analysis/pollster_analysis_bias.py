"""Reduce calibration trends into pollster bias parameters.

Parent: pollster_analysis.py combines this reducer with the variability and
house-effect reducers, publishing the three related files atomically per
target election.
"""

import math

from election_code import ElectionCode
from pollster_analysis_common import (
    LIBERAL_PARTY,
    ConfigError,
    canonical_party,
    check_dates,
    get_significant_parties,
    output_party,
    parse_finite_float,
)
from pollster_analysis_house_effects import (
    get_n_polls,
)


def analyse_bias(
    target_election, cycles, links, evidence, output_path
):
    import numpy as np
    from statsmodels.stats.weightstats import DescrStatsW

    print("Analysing bias")
    n_polls = get_n_polls(evidence)
    
    # get ordered list of elections
    elections = []
    with open(f'Data/polled-elections.csv', 'r') as f:
        for line in f:
            year, region = (a.strip() for a in line.split(','))
            elections.append(ElectionCode(int(year), region))

    # get eventual results for all elections
    results = {}
    with open(f'Data/eventual-results.csv', 'r') as f:
        for line in f:
            split_line = (a.strip() for a in line.split(',')[:4])
            year, region, party, median = split_line
            key = (
                ElectionCode(int(year), region),
                canonical_party(party),
            )
            median = parse_finite_float(
                median,
                '{} {} eventual result'.format(
                    ElectionCode(int(year), region).short(),
                    party,
                ),
            )
            results[key] = median
    
    trend_medians = {
        (source.election, canonical_party(source.party)):
        source.final_trend_median
        for source in evidence.bias
    }
    
    bias_infos = []
    bias_list = {}
    weight_list = {}
    lib = False
    target_pollsters = set()
    target_parties = set()
    significant_parties = {
        canonical_party(party)
        for party in get_significant_parties(target_election)
    }

    # Get all target-election pollsters and parties that have calibration files.
    for source in evidence.bias_for_election(target_election):
        if source.party == LIBERAL_PARTY:
            lib = True
        party = canonical_party(source.party)
        if significant_parties and party not in significant_parties:
            continue
        target_parties.add(party)
        target_pollsters.update(source.house_effects)

    # Establish a prior expectation for every target pollster/significant party
    # combination, even if that pollster has not reported the party yet.
    for pollster in target_pollsters:
        for party in target_parties:
            key = (pollster, party)
            # Symmetric pseudo-observations give a zero bias with SD 4.
            bias_list[key] = [4, -4]
            weight_list[key] = [0.5, 0.5]


    # Cycle through elections to exclude
    # so that these can be used for that election's forecast
    # without the actual result for that election impacting it in any way.
    for election in elections:
        if not check_dates(election, target_election, cycles): continue
        for source in evidence.bias_for_election(election):
            party = canonical_party(source.party)
            key = (election, party)
            if key not in results:
                continue
            if key not in trend_medians:
                raise ConfigError(
                    'Missing bias-calibration trend for {} {}.'.format(
                        election.short(), party
                    )
                )
            trend_median = trend_medians[key]
            for pollster, house_effect in source.house_effects.items():
                target_key = (pollster, party)
                if target_key not in bias_list:
                    continue
                pollster_key = (election, pollster, party)
                all_key = (election, 'all', party)
                if pollster_key not in n_polls:
                    continue
                pollster_trend = trend_median + house_effect
                bias = pollster_trend - results[key]
                this_n_polls = n_polls[pollster_key]
                all_n_polls = n_polls[all_key]
                # Calibrated so that a very frequent poll in a very
                # frequently polled election gets a weight of 1.
                weight = (min(math.log(this_n_polls + 1), 3) *
                          min(math.log(all_n_polls + 1), 4) / 12)
                # Elections in closer proximity should have more weight.
                weight *= 2 ** -(abs(
                    target_election.year() - election.year()
                ) / 6)
                # Downweight federal evidence for state targets. Federal
                # targets retain the denser federal polling evidence.
                if ((election.region() == 'fed') and
                    (target_election.region() != 'fed')):
                    weight *= 0.2
                bias_list[target_key].append(bias)
                weight_list[target_key].append(weight)
    linked_keys = set(sum(links.values(), []))
    for target_key in sorted(bias_list.keys()):
        # Don't do linked pollsters yet
        if target_key[0] in linked_keys: continue
        bias_arr = np.array(bias_list[target_key])
        weight_arr = np.array(weight_list[target_key])
        desc = DescrStatsW(bias_arr, weights=weight_arr)
        mean = parse_finite_float(
            desc.mean, '{} {} bias mean'.format(*target_key)
        )
        stddev = parse_finite_float(
            desc.std, '{} {} bias standard deviation'.format(*target_key)
        )
        # Adjust data for any linked pollsters
        if target_key[0] in links:
            linked = links[target_key[0]]
            for linked_pollster in linked:
                linked_key = (linked_pollster, target_key[1])
                if linked_key not in bias_list: continue
                link_weight = 1.5
                weight_list[linked_key] += [link_weight, link_weight]
                bias_list[linked_key] += [
                    mean + stddev,
                    mean - stddev,
                ]
        party = output_party(target_key[1], lib)
        bias_infos.append((target_key[0], party, mean, stddev))
    for target_key in sorted(bias_list.keys()):
        # Non-linked pollsters have already been done
        if target_key[0] not in linked_keys: continue
        bias_arr = np.array(bias_list[target_key])
        weight_arr = np.array(weight_list[target_key])
        desc = DescrStatsW(bias_arr, weights=weight_arr)
        mean = parse_finite_float(
            desc.mean, '{} {} bias mean'.format(*target_key)
        )
        stddev = parse_finite_float(
            desc.std, '{} {} bias standard deviation'.format(*target_key)
        )
        party = output_party(target_key[1], lib)
        bias_infos.append((target_key[0], party, mean, stddev))

    with open(output_path, 'w') as f:
        for bias_info in bias_infos:
            f.write(','.join(str(a) for a in bias_info) + '\n')

    print('Bias analysis successfully completed')
