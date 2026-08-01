"""Reduce historical calibration runs into pollster-level model parameters.

For each target election, this script combines applicable historical
calibration evidence with conservative priors and writes:

* typical poll error by pollster and party;
* the relative strength assigned to estimated house effects; and
* historical pollster bias means and standard deviations.

Only calibration files selected by the generated provenance manifest are
consumed. This prevents retained backups or superseded calibration runs from
silently affecting current parameters.
"""

import argparse
import math
import numpy as np
import os
import pandas as pd
import sys
import tempfile
from pathlib import Path

from election_code import ElectionCode
import generated_provenance
import pollster_analysis_provenance
from statsmodels.stats.weightstats import DescrStatsW

directory = 'Outputs/Calibration'
LIBERAL_PARTY = 'LIB FP'
COALITION_PARTY = 'LNP FP'


def canonical_party(party):
    """Pool state Liberal evidence with the national Coalition series."""

    return COALITION_PARTY if party == LIBERAL_PARTY else party


def output_party(party, target_uses_liberal):
    if target_uses_liberal and party == COALITION_PARTY:
        return LIBERAL_PARTY
    return party


def output_paths(target_election):
    code = f'{target_election.year()}{target_election.region()}'
    return [
        f'{directory}/variability-{code}.csv',
        f'{directory}/he_weighting-{code}.csv',
        f'{directory}/biases-{code}.csv',
    ]


class ConfigError(ValueError):
    pass


def parse_finite_float(value, context):
    try:
        parsed = float(value)
    except (TypeError, ValueError) as error:
        raise ConfigError(
            '{} is not numeric: {!r}.'.format(context, value)
        ) from error
    if not math.isfinite(parsed):
        raise ConfigError('{} is not finite.'.format(context))
    return parsed


def parse_poll_day(line, filename):
    fields = line.rstrip('\r\n').split(',')
    if len(fields) <= 1:
        raise ConfigError(
            '{} contains a short poll row.'.format(filename)
        )
    return parse_finite_float(
        fields[1], '{} poll day'.format(filename)
    )


class Config:
    def __init__(self, argv=None):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('--election', action='store', type=str,
                            required=True,
                            help='Generate forecast trend for this election.'
                            ' Enter as 1234-xxx format,'
                            ' e.g. 2013-fed. Write "all" '
                            'to do it for all elections.')
        self.election_instructions = (
            parser.parse_args(argv).election.lower()
        )
        self.prepare_election_list()

    def prepare_election_list(self):
        with open('./Data/polled-elections.csv', 'r') as f:
            elections = ElectionCode.load_elections_from_file(f)
        with open('./Data/future-elections.csv', 'r') as f:
            elections += ElectionCode.load_elections_from_file(f)
        if self.election_instructions == 'all':
            self.elections = elections
        else:
            parts = self.election_instructions.split('-')
            if len(parts) < 2:
                raise ConfigError('Error in "elections" argument: given value '
                                  'did not have two parts separated '
                                  'by a hyphen (e.g. 2013-fed)')
            try:
                code = ElectionCode(parts[0], parts[1])
            except ValueError:
                raise ConfigError('Error in "elections" argument: first part '
                                  'of election name could not be converted '
                                  'into an integer')
            if code not in elections:
                raise ConfigError('Error in "elections" argument: '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv')
            if len(parts) == 2:
                self.elections = [code]
            elif parts[2] == 'onwards':
                try:
                    self.elections = (elections[elections.index(code):])
                except ValueError:
                    raise ConfigError('Error in "elections" argument: '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv')
            else:
                raise ConfigError('Invalid instruction in "elections"'
                                  'argument.')


def get_election_cycles():
    # Load the dates of each election cycle
    # to ensure that we don't use any data from the future
    with open('./Data/election-cycles.csv', 'r') as f:
        election_cycles = {
            (int(a[0]), a[1]):
            (
                pd.Timestamp(a[2]),
                pd.Timestamp(a[3])
            )
            for a in [b.strip().split(',')
            for b in f.readlines()]
        }
        return election_cycles
    

def get_links():
    with open('Data/linked-pollsters.csv', 'r') as input_file:
        return {
            parts[0]: parts[1:]
            for parts in (
                [field.strip() for field in line.split(',')]
                for line in input_file
                if line.strip()
            )
        }


def get_significant_parties(target_election):
    with open('Data/significant-parties.csv', 'r') as f:
        for line in f:
            parts = [a.strip() for a in line.split(',')]
            if (int(parts[0]) == target_election.year()
                and parts[1] == target_election.region()):
                return parts[2:]
    return []


def check_dates(election, target_election, cycles, equals=False):
    """Return whether an election's evidence predates the target endpoint."""

    if election.year() > target_election.year():
        return False
    if election.year() == target_election.year():
        # If we're in the same year, make sure that the election is earlier
        # than the target election (i.e. don't use the target election itself
        # or any future election in the same year)
        # if Equals is true, it's ok for the election to be the same as the
        # target election
        # (Later, add some logic for partial series of a parallel election cycle
        # when that series is before the target election)
        # Check the end dates of the corresponding election period
        if equals:
            if (cycles[
                (election.year(), election.region())
            ][1] > cycles[
                (target_election.year(), target_election.region())
            ][1]):
                return False
        else:
            if (cycles[
                (election.year(), election.region())
            ][1] >= cycles[
                (target_election.year(), target_election.region())
            ][1]):
                return False
    return True


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
def analyse_bias(
    target_election, cycles, links, calib_filenames, output_path
):
    print("Analysing bias")
    n_polls = get_n_polls(calib_filenames)
    
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
    
    # get poll trend medians for each election/party
    trend_medians = {}
    for election in elections:
        # collect median values for all elections
        for filename in calib_filenames:
            file_marker = f'fp_trend_{election.year()}{election.region()}'
            if (file_marker in filename and 'biascal' in filename):
                party = canonical_party(filename.split('_')[3])
                with open(f'{directory}/{filename}', 'r') as f:
                    trend_medians[(election, party)] = (
                        load_final_trend_median(f, filename)
                    )
    
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
    for filename in calib_filenames:
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
        if significant_parties and party not in significant_parties:
            continue
        target_parties.add(party)
        with open(f'{directory}/{filename}', 'r') as f:
            data = f.readlines()[1:]
            for line in data:
                target_pollsters.add(line.split(',')[0])

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
        for filename in calib_filenames:
            file_marker = f'fp_house_effects_{election.year()}{election.region()}'
            if (file_marker in filename and 'biascal' in filename):
                party = canonical_party(filename.split('_')[4])
                with open(f'{directory}/{filename}', 'r') as f:
                    data = load_new_house_effects(f, filename)
                key = (election, party)
                if key not in results: continue
                if key not in trend_medians:
                    raise ConfigError(
                        'Missing bias-calibration trend for {} {}.'.format(
                            election.short(), party
                        )
                    )
                trend_median = trend_medians[key]
                for pollster, house_effect in data.items():
                    target_key = (pollster, party)
                    if target_key not in bias_list: continue
                    pollster_key = (election, pollster, party)
                    all_key = (election, 'all', party)
                    if pollster_key not in n_polls: continue
                    pollster_trend = trend_median + house_effect
                    bias = pollster_trend - results[key]
                    this_n_polls = n_polls[pollster_key]
                    all_n_polls = n_polls[all_key]
                    # Calibrated so that a very frequent poll in a very
                    # frequently polled election gets a weight of 1
                    weight = (min(math.log(this_n_polls + 1), 3) *
                                min(math.log(all_n_polls + 1), 4) / 12)
                    # Elections in closer proximity should have more weight
                    weight *= 2 ** -(abs(
                        target_election.year() - election.year()
                    ) / 6)
                    # Downweight polls from federal elections
                    # if the target election is not federal
                    # (but not vice versa, because federal elections
                    # naturally get high weightings from the higher
                    # density of polls)
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


def write_completion_status(status):
    with open('itsdone.txt', 'w') as output_file:
        output_file.write(str(status))


def run_analysis(argv=None):
    config = Config(argv)
    cycles = get_election_cycles()
    links = get_links()
    command_arguments = sys.argv[1:] if argv is None else list(argv)
    recorder = pollster_analysis_provenance.PollsterAnalysisRecorder(
        [Path(__file__).name] + command_arguments
    )
    for election in config.elections:
        election_code = election.short()
        dependencies, filenames = recorder.inputs_for(
            election_code,
            lambda candidate, target: check_dates(
                ElectionCode(int(candidate[:4]), candidate[4:]),
                ElectionCode(int(target[:4]), target[4:]),
                cycles,
                equals=True,
            ),
        )
        final_paths = output_paths(election)
        # Generate all three related files before replacing any current output.
        with tempfile.TemporaryDirectory(
            prefix='pollster-analysis-{}-'.format(election_code),
            dir=directory,
        ) as staging_directory:
            staged_paths = [
                str(Path(staging_directory) / Path(path).name)
                for path in final_paths
            ]
            analyse_variability(
                election, cycles, links, filenames, staged_paths[0]
            )
            analyse_house_effects(
                election, cycles, links, filenames, staged_paths[1]
            )
            analyse_bias(
                election, cycles, links, filenames, staged_paths[2]
            )
            for staged_path, final_path in zip(
                staged_paths, final_paths
            ):
                os.replace(staged_path, final_path)
        recorder.record(
            election_code,
            final_paths,
            dependencies,
        )
    write_completion_status(1)
    return 0


def main(argv=None):
    try:
        return run_analysis(argv)
    except (
        ConfigError,
        generated_provenance.GeneratedProvenanceError,
    ) as e:
        print(
            'Could not analyse pollsters: {}'.format(e),
            file=sys.stderr,
        )
        write_completion_status(2)
        return 2


if __name__ == '__main__':
    sys.exit(main())
