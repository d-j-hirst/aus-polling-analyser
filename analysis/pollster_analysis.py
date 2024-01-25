import argparse
import math
import numpy as np
import os
import pandas as pd
from mailbox import linesep
from election_code import ElectionCode
from statsmodels.stats.weightstats import DescrStatsW

directory = 'Outputs/Calibration'


class ConfigError(ValueError):
    pass


class Config:
    def __init__(self):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('--election', action='store', type=str,
                            help='Generate forecast trend for this election.'
                            ' Enter as 1234-xxx format,'
                            ' e.g. 2013-fed. Write "all" '
                            'to do it for all elections.')
        self.election_instructions = parser.parse_args().election.lower()
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


def analyse_variability(target_election, cycles):
    # The trend calibration process for each prior election has to be done
    # before performing this analysis (via fp_model.py --calibrate)
    print("Analysing variability")
    filenames = os.listdir(directory)
    # This dictionary will contain the weighted error sums for each
    # pollster/party combination, i.e. one value for each election
    weighted_error_sums = {}
    # This dictionary will contain the weight sums for each pollster/party
    # combination, i.e. one value for each election
    weight_sums = {}

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
        with open(f'{directory}/{filename}', 'r') as f:
            data = f.readlines()[1:]
            for line in data:
                pollster = line.split(',')[0]
                key = (pollster, party)
                # Add a small amount to the weight when a new pollster/party is
                # encountered to establish prior expectation and avoid
                # overfitting to the first few data points
                weighted_error_sums[key] = 14
                weight_sums[key] = 7


    for filename in filenames:
        if (filename[:5]) != 'calib': continue
        # The file we have excludes a particular pollster from the calibration
        # so that we can see the variability of that pollster from the trend
        # line created from all other pollsters
        _, election, pollster, party = filename.split(".")[0].split('_')
        year = int(election[:4])
        # Don't use elections from the future
        if year > target_election.year(): continue
        if year == target_election.year():
            # If we're in the same year, make sure that the election is earlier
            # than the target election (i.e. don't use the target election itself
            # or any future election in the same year)
            # (Later, add some logic for partial series of a parallel election cycle
            # when that series is before the target election)
            region = election[4:]
            # Check the end dates of the corresponding election period
            if cycles[(year, region)][1] > cycles[(target_election.year(), target_election.region())][1]:
                continue
        print(f'Analysing {election} {pollster} {party}')
        key = (pollster, party)
        with open(f'{directory}/{filename}', 'r') as f:
            # The first line of the file contains the weighted error and weight
            # The rest of the file is not required for this analysis
            # (it contains the deviations and weights for each poll)
            stat_strs = f.readlines()[0].split(',')[:2]
            error, weight = float(stat_strs[0]), float(stat_strs[1])
            # If this pollster/party isn't in the dictionary, skip it
            # as we don't need it for the target election
            if key not in weighted_error_sums:
                continue
            # Add the weighted error and weight to the dictionaries
            weighted_error_sums[key] += weight * error
            weight_sums[key] += weight

    with open(f'{directory}/variability-{target_election.year()}{target_election.region()}.csv', 'w') as f:
        # Store the standard deviation in error and sum of weights
        # for each pollster/party combination
        for key in sorted(weight_sums.keys()):
            weight_sum = weight_sums[key]
            weighted_error_sum = weighted_error_sums[key]
            error_average = weighted_error_sum / weight_sum
            error_stddev = error_average / math.sqrt(2 / math.pi)
            f.write(f'{key[0]},{key[1]},{error_stddev},{weight_sum - 7}\n')
    
    print('Variability analysis successfully completed')


# get number of polls in election
def get_n_polls(filenames):
    n_polls = {}
    for filename in filenames:
        if (filename[:4]) != 'fp_p': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[2:4]
        election = (election[:4], election[4:])
        with open(f'{directory}/{filename}', 'r') as f:
            lines = f.readlines()[1:]
            final_day = max(int(float(a.split(',')[1]) + 0.01) for a in lines)
            # Only count polls that would contribute to "new" house effect
            start_day = final_day - 183
            for line in lines:
                pollster = line.split(',')[0]
                if int(float(line.split(',')[1]) + 0.01) < start_day: continue
                key = (election, pollster, party)
                if key not in n_polls:
                    n_polls[key] = 0
                n_polls[key] += 1
                overall_key = (election, 'all', party)
                if overall_key not in n_polls:
                    n_polls[overall_key] = 0
                n_polls[overall_key] += 1
    return n_polls


def load_new_house_effects(f):
    lines = []
    for line in f:
        if line[:4] == 'Hous' or line[:4] == 'New ': continue
        if line[:4] == 'Old ': break
        lines.append(line)
    return {line.split(',')[0]: float(line.split(',')[7])
            for line in lines}


# Which pollsters' house effects are usually close to the middle
# of their elections' trend lines
def analyse_house_effects():
    print("Analysing house effects")
    filenames = os.listdir(directory)
    n_polls = get_n_polls(filenames)
        

    abs_he_sums = {}
    abs_he_weights = {}
    for filename in filenames:
        if (filename[:4]) != 'fp_h': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[3:5]
        election = (election[:4], election[4:])
        with open(f'{directory}/{filename}', 'r') as f:
            data = load_new_house_effects(f)
        for pollster, median in data.items():
            key = (pollster, party)
            if key not in abs_he_sums:
                abs_he_sums[key] = 1
                abs_he_weights[key] = 0.4
            pollster_key = (election, pollster, party)
            all_key = (election, 'all', party)
            if pollster_key not in n_polls: continue
            pollster_n_polls = n_polls[pollster_key]
            all_n_polls = n_polls[all_key]
            party_weight = (math.log(min(max(pollster_n_polls, 1), 10))
                            / math.log(10))
            all_weight = (math.log(min(max(all_n_polls, 1), 20))
                            / math.log(20))
            total_weight = party_weight * all_weight
            abs_he_sums[key] += abs(median) * total_weight
            abs_he_weights[key] += 1 * total_weight
    
    with open(f'{directory}/he_weighting.csv', 'w') as f:
        for key in sorted(abs_he_sums.keys()):
            average_he = abs_he_sums[key] / abs_he_weights[key]
            weighting = 1 / average_he
            if abs_he_weights[key] == 0.4: continue
            f.write(f'{key[0]},{key[1]},{weighting}\n')
    
    print('House effect analysis successfully completed')


# Whether the pollster has any consistent bias
def analyse_bias():
    print("Analysing bias")
    calib_filenames = os.listdir(directory)
    n_polls = get_n_polls(calib_filenames)
    abs_he_sums = {}
    abs_he_weights = {}
    
    # get ordered list of elections
    elections = []
    with open(f'Data/polled-elections.csv', 'r') as f:
        for line in f:
            year, region = (a.strip() for a in line.split(','))
            elections.append((year, region))
    
    # get ordered list of elections
    future_elections = []
    with open(f'Data/future-elections.csv', 'r') as f:
        for line in f:
            year, region = (a.strip() for a in line.split(','))
            future_elections.append((year, region))

    # get eventual results for all elections
    results = {}
    with open(f'Data/eventual-results.csv', 'r') as f:
        for line in f:
            split_line = (a.strip() for a in line.split(',')[:4])
            year, region, party, median = split_line
            key = ((year, region), party)
            median = float(median)
            results[key] = median
    
    # get poll trend medians for each election/party
    trend_medians = {}
    for election in elections:
        # collect median values for all elections
        for filename in calib_filenames:
            file_marker = f'fp_trend_{election[0]}{election[1]}'
            if (file_marker in filename and 'biascal' in filename):
                party = filename.split('_')[3]
                with open(f'{directory}/{filename}', 'r') as f:
                    last_line = f.readlines()[-1]
                    # 52 is the value of the line where the median
                    # of the distribution is found
                    median = last_line.split(',')[52]
                    trend_medians[((election[0], election[1]), party)] = \
                        float(median)
    
    target_elections = elections + future_elections
    
    bias_infos = []
    # Cycle through elections to exclude
    # so that these can be used for that election's forecast
    # without the actual result for that election impacting it in any way.
    for target_election in target_elections:
        bias_list = {}
        weight_list = {}
        for election in elections:
            # Elections before 2022 may use only pre-2022 elections
            # (except themselves) and elections from 2022 onwards
            # may use all prior elections
            if election == target_election:
                if int(target_election[0]) >= 2022: break
                else: continue
            if int(election[0]) >= 2022 and int(target_election[0]) < 2022:
                break
            for filename in calib_filenames:
                file_marker = f'fp_house_effects_{election[0]}{election[1]}'
                if (file_marker in filename and 'biascal' in filename):
                    party = filename.split('_')[4]
                    with open(f'{directory}/{filename}', 'r') as f:
                        data = load_new_house_effects(f)
                    key = ((election[0], election[1]), party)
                    if key not in results: continue
                    trend_median = trend_medians[key]
                    for pollster, house_effect in data.items():
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
                        # "Future" elections to be used for calibration
                        # are treated as if they were held 18 years earlier
                        # to ensure that the total weighting of all elections
                        # is not unduly increased due to having too many
                        # "recent" elections
                        effective_year = (
                            int(election[0])
                            if int(election[0]) <= int(target_election[0])
                            else int(election[0]) - 18
                        )
                        # Elections in closer proximity should have more weight
                        weight *= 2 ** -(abs(
                            int(target_election[0]) - effective_year
                        ) / 6)
                        # Downweight polls from federal elections
                        # if the target election is not federal
                        # (but not vice versa, because federal elections
                        # naturally get high weightings from the higher
                        # density of polls)
                        if ((election[1] == 'fed') and
                            (target_election[1] != 'fed')):
                            weight *= 0.2
                        target_key = (pollster, party)
                        if target_key not in bias_list:
                            bias_list[target_key] = []
                            weight_list[target_key] = []
                        bias_list[target_key].append(bias)
                        weight_list[target_key].append(weight)
        for target_key in sorted(bias_list.keys()):
            bias_arr = np.array(bias_list[target_key] + [4, -4])
            weight_arr = np.array(weight_list[target_key] + [0.5, 0.5])
            desc = DescrStatsW(bias_arr, weights=weight_arr)
            bias_infos.append((target_election[0], target_election[1],
                           target_key[0], target_key[1], desc.mean, desc.std))
        print(f'Performed bias analysis for {target_election}')

    with open(f'{directory}/biases.csv', 'w') as f:
        for bias_info in bias_infos:
            f.write(','.join(str(a) for a in bias_info) + '\n')

    print('Bias analysis successfully completed')


if __name__ == '__main__':

    try:
        config = Config()
        cycles = get_election_cycles()
        for election in config.elections:
            analyse_variability(election, cycles)
        #analyse_house_effects()
        #analyse_bias()
        with open(f'itsdone.txt', 'w') as f:
            f.write('1')
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        with open(f'itsdone.txt', 'w') as f:
            f.write('2')
