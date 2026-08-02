"""Reduce calibration house effects into pollster weighting parameters.

Parent: pollster_analysis.py invokes this reducer between variability and bias
analysis, staging its output until the full election-level bundle passes.
"""

import math

from pollster_analysis_common import (
    LIBERAL_PARTY,
    canonical_party,
    check_dates,
    output_party,
)


def get_n_polls(evidence):
    """Count recent polls used to weight house-effect and bias evidence."""

    return evidence.recent_poll_counts()


# Which pollsters' house effects are usually close to the middle
# of their elections' trend lines
def analyse_house_effects(
    target_election, cycles, links, evidence, output_path
):
    print("Analysing house effects")
    n_polls = get_n_polls(evidence)
    
    # This dictionary will contain the weighted house effect sums for each
    # pollster/party combination, i.e. one value for each election
    abs_he_sums = {}
    abs_he_weights = {}
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
            # encountered to establish prior expectation and avoid
            # overfitting to the first few data points. Prior mean absolute
            # house effect: 2.5.
            abs_he_sums[key] = 1
            abs_he_weights[key] = 0.4
    
    for source in evidence.bias:
        election = source.election
        party = canonical_party(source.party)
        if not check_dates(election, target_election, cycles, equals=True):
            continue
        data = source.house_effects
        
        # if there is only one pollster, then don't use data from this election
        # at all, as it is trivially zero
        if len(data) == 1: continue
        # Higher weight when there are more pollsters in the election
        diversity_weight = len(data) / (len(data) + 1)
        for pollster, median in data.items():
            key = (pollster, party)
            if key not in abs_he_sums:
                continue
            pollster_key = (election, pollster, party)
            all_key = (election, 'all', party)
            if pollster_key not in n_polls: continue
            pollster_n_polls = n_polls[pollster_key]
            all_n_polls = n_polls[all_key]
            party_weight = (math.log(min(max(pollster_n_polls, 1), 10))
                            / math.log(10))
            all_weight = (math.log(min(max(all_n_polls, 1), 20))
                            / math.log(20))
            total_weight = party_weight * all_weight * diversity_weight
            # Adjust for the fact that having a small number of pollsters
            # in an election makes the house effect likely to be closer to
            # zero than it would be if there were more pollsters
            adjusted_median = median / diversity_weight
            abs_he_sums[key] += abs(adjusted_median) * total_weight
            abs_he_weights[key] += 1 * total_weight

    linked_keys = set(sum(links.values(), []))
    
    with open(output_path, 'w') as f:
        for key in sorted(abs_he_sums.keys()):
            # Don't do linked pollsters yet
            if key[0] in linked_keys: continue
            average_he = abs_he_sums[key] / abs_he_weights[key]
            weighting = 1 / average_he
            # Adjust data for any linked pollsters
            if key[0] in links:
                linked = links[key[0]]
                for linked_pollster in linked:
                    linked_key = (linked_pollster, key[1])
                    if linked_key not in abs_he_weights: continue
                    link_weight = 1.2
                    abs_he_weights[linked_key] += link_weight
                    abs_he_sums[linked_key] += average_he * link_weight
            party = output_party(key[1], lib)
            f.write(f'{key[0]},{party},{weighting}\n')
        for key in sorted(abs_he_sums.keys()):
            # Non-linked pollsters have already been done
            if key[0] not in linked_keys: continue
            average_he = abs_he_sums[key] / abs_he_weights[key]
            weighting = 1 / average_he
            party = output_party(key[1], lib)
            f.write(f'{key[0]},{party},{weighting}\n')
    
    print('House effect analysis successfully completed')


# Whether the pollster has any consistent bias
