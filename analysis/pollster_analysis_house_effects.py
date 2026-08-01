"""Reduce calibration house effects into pollster weighting parameters.

Parent: pollster_analysis.py invokes this reducer between variability and bias
analysis, staging its output until the full election-level bundle passes.
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
    parse_poll_day,
)


def get_n_polls(filenames):
    """Count recent polls used to weight house-effect and bias evidence."""

    n_polls = {}
    for filename in filenames:
        if (filename[:4]) != 'fp_p': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[2:4]
        election = ElectionCode(int(election[:4]), election[4:])
        party = canonical_party(party)
        with open(f'{directory}/{filename}', 'r') as f:
            lines = f.readlines()[1:]
            if not lines:
                continue
            poll_days = [
                parse_poll_day(line, filename) for line in lines
            ]
            final_day = max(int(day + 0.01) for day in poll_days)
            # Only count polls that would contribute to "new" house effect
            start_day = final_day - 183
            for line in lines:
                pollster = line.split(',')[0]
                poll_day = parse_poll_day(line, filename)
                if int(poll_day + 0.01) < start_day: continue
                key = (election, pollster, party)
                if key not in n_polls:
                    n_polls[key] = 0
                n_polls[key] += 1
                overall_key = (election, 'all', party)
                if overall_key not in n_polls:
                    n_polls[overall_key] = 0
                n_polls[overall_key] += 1
    return n_polls


def load_new_house_effects(f, filename):
    header = None
    lines = []
    for line in f:
        if line[:4] == 'Hous':
            header = line.rstrip('\r\n').split(',')
            continue
        if line[:4] == 'New ': continue
        if line[:4] == 'Old ': break
        lines.append(line)
    if header is None or '50%' not in header:
        raise ConfigError(
            '{} lacks a house-effect median column.'.format(filename)
        )
    median_index = header.index('50%')
    house_effects = {}
    for line in lines:
        fields = line.rstrip('\r\n').split(',')
        if len(fields) <= median_index:
            raise ConfigError(
                '{} contains a short house-effect row.'.format(filename)
            )
        house_effects[fields[0]] = parse_finite_float(
            fields[median_index],
            '{} {} median house effect'.format(filename, fields[0]),
        )
    return house_effects


def load_final_trend_median(input_file, filename):
    """Read the last model day's median by its percentile header."""

    rows = [line.rstrip('\r\n').split(',') for line in input_file]
    if len(rows) < 4 or '50%' not in rows[2]:
        raise ConfigError(
            '{} lacks a usable trend percentile header.'.format(filename)
        )
    median_index = rows[2].index('50%')
    if len(rows[-1]) <= median_index:
        raise ConfigError(
            '{} final trend row lacks its median value.'.format(filename)
        )
    try:
        median = float(rows[-1][median_index])
    except ValueError as error:
        raise ConfigError(
            '{} final trend median is not numeric.'.format(filename)
        ) from error
    if not math.isfinite(median):
        raise ConfigError(
            '{} final trend median is not finite.'.format(filename)
        )
    return median


# Which pollsters' house effects are usually close to the middle
# of their elections' trend lines
def analyse_house_effects(
    target_election, cycles, links, filenames, output_path
):
    print("Analysing house effects")
    n_polls = get_n_polls(filenames)
    
    # This dictionary will contain the weighted house effect sums for each
    # pollster/party combination, i.e. one value for each election
    abs_he_sums = {}
    abs_he_weights = {}
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
                # Prior mean absolute house effect of 2.5.
                abs_he_sums[key] = 1
                abs_he_weights[key] = 0.4
    
    for filename in filenames:
        if (filename[:4]) != 'fp_h': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[3:5]
        party = canonical_party(party)
        election = ElectionCode(int(election[:4]), election[4:])
        if not check_dates(election, target_election, cycles, equals=True): continue

        # Load the relevant data as (pollster:median house effect) dict pairs
        with open(f'{directory}/{filename}', 'r') as f:
            data = load_new_house_effects(f, filename)
        
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
