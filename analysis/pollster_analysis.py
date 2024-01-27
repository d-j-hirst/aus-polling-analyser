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
    

def get_links():
    links = {
        line.strip().split(',')[0]:  line.strip().split(',')[1:] for line in
        open('Data/linked-pollsters.csv', 'r').readlines()
    }
    return links


def check_dates(election, target_election, cycles, equals=False):
    # Returns True is the election should be used for the target election
    if election.year() > target_election.year(): return False
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
            print(cycles[(election.year(), election.region())][1])
            print(cycles[(target_election.year(), target_election.region())][1])
            if (cycles[
                (election.year(), election.region())
            ][1] >= cycles[
                (target_election.year(), target_election.region())
            ][1]):
                return False
    return True


def analyse_variability(target_election, cycles, links):
    # The trend calibration process for each prior election has to be done
    # before performing this analysis (via fp_model.py --calibrate)
    print("Analysing variability")
    filenames = os.listdir(directory)
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
        if party == "LIB FP": party = "LNP FP"; lib = True
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
        if party == "LIB FP": party = "LNP FP"
        election = ElectionCode(int(election[:4]), election[4:])
        # Don't use elections from the future
        if not check_dates(election, target_election, cycles, equals=True): continue
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
            # Don't do linked pollsters yet
            if key[0] in sum(links.values(), []): continue
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
            if key[1] == "LNP FP" and lib: key = (key[0], "LIB FP")
            f.write(f'{key[0]},{key[1]},{error_stddev},{weight_sum - 7}\n')
        for key in sorted(weight_sums.keys()):
            # Non-linked pollsters have already been done
            if key[0] not in sum(links.values(), []): continue
            weight_sum = weight_sums[key]
            weighted_error_sum = weighted_error_sums[key]
            error_average = weighted_error_sum / weight_sum
            error_stddev = error_average / math.sqrt(2 / math.pi)
            if key[1] == "LNP FP" and lib: key = (key[0], "LIB FP")
            f.write(f'{key[0]},{key[1]},{error_stddev},{weight_sum - 7}\n')
    
    print('Variability analysis successfully completed')


# get number of polls in election
def get_n_polls(filenames):
    n_polls = {}
    for filename in filenames:
        if (filename[:4]) != 'fp_p': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[2:4]
        election = ElectionCode(int(election[:4]), election[4:])
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
    return {
        line.split(',')[0]: float(line.split(',')[7])
        for line in lines
    }


# Which pollsters' house effects are usually close to the middle
# of their elections' trend lines
def analyse_house_effects(target_election, cycles, links):
    print("Analysing house effects")
    filenames = os.listdir(directory)
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
        if party == "LIB FP": party = "LNP FP"; lib = True
        with open(f'{directory}/{filename}', 'r') as f:
            data = f.readlines()[1:]
            for line in data:
                pollster = line.split(',')[0]
                key = (pollster, party)
                # Add a small amount to the weight when a new pollster/party is
                # encountered to establish prior expectation and avoid
                # overfitting to the first few data points
                abs_he_sums[key] = 1
                abs_he_weights[key] = 0.4
    
    for filename in filenames:
        if (filename[:4]) != 'fp_h': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[3:5]
        if party == "LIB FP": party = "LNP FP"
        election = ElectionCode(int(election[:4]), election[4:])
        if not check_dates(election, target_election, cycles, equals=True): continue
        # Load the relevant data as (pollster:median house effect) dict pairs
        with open(f'{directory}/{filename}', 'r') as f:
            data = load_new_house_effects(f)
        
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
            # print(n_polls)
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
    
    with open(f'{directory}/he_weighting-{target_election.year()}{target_election.region()}.csv', 'w') as f:
        for key in sorted(abs_he_sums.keys()):
            # Don't do linked pollsters yet
            if key[0] in sum(links.values(), []): continue
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
            if key[1] == "LNP FP" and lib: key = (key[0], "LIB FP")
            f.write(f'{key[0]},{key[1]},{weighting}\n')
        for key in sorted(abs_he_sums.keys()):
            # Non-linked pollsters have already been done
            if key[0] not in sum(links.values(), []): continue
            average_he = abs_he_sums[key] / abs_he_weights[key]
            weighting = 1 / average_he
            if key[1] == "LNP FP" and lib: key = (key[0], "LIB FP")
            f.write(f'{key[0]},{key[1]},{weighting}\n')
    
    print('House effect analysis successfully completed')


# Whether the pollster has any consistent bias
def analyse_bias(target_election, cycles, links):
    print("Analysing bias")
    calib_filenames = os.listdir(directory)
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
            key = (ElectionCode(int(year), region), party)
            median = float(median)
            results[key] = median
    
    # get poll trend medians for each election/party
    trend_medians = {}
    for election in elections:
        # collect median values for all elections
        for filename in calib_filenames:
            file_marker = f'fp_trend_{election.year()}{election.region()}'
            if (file_marker in filename and 'biascal' in filename):
                party = filename.split('_')[3]
                with open(f'{directory}/{filename}', 'r') as f:
                    last_line = f.readlines()[-1]
                    # 52 is the value of the line where the median
                    # of the distribution is found
                    median = last_line.split(',')[52]
                    trend_medians[(election, party)] = \
                        float(median)
    
    bias_infos = []
    bias_list = {}
    weight_list = {}
    filenames = os.listdir(directory)
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
        if party == "LIB FP": party = "LNP FP"; lib = True
        with open(f'{directory}/{filename}', 'r') as f:
            data = f.readlines()[1:]
            for line in data:
                pollster = line.split(',')[0]
                key = (pollster, party)
                # Add a small amount to the weight when a new pollster/party is
                # encountered to establish prior expectation and avoid
                # overfitting to the first few data points
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
                party = filename.split('_')[4]
                if party == "LIB FP": party = "LNP FP"
                with open(f'{directory}/{filename}', 'r') as f:
                    data = load_new_house_effects(f)
                key = (election, party)
                if key not in results: continue
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
    for target_key in sorted(bias_list.keys()):
        # Don't do linked pollsters yet
        if target_key[0] in sum(links.values(), []): continue
        bias_arr = np.array(bias_list[target_key])
        weight_arr = np.array(weight_list[target_key])
        desc = DescrStatsW(bias_arr, weights=weight_arr)
        # Adjust data for any linked pollsters
        if target_key[0] in links:
            linked = links[target_key[0]]
            for linked_pollster in linked:
                linked_key = (linked_pollster, target_key[1])
                if linked_key not in bias_list: continue
                link_weight = 1.5
                weight_list[linked_key] += [link_weight, link_weight]
                bias_list[linked_key] += [desc.mean + desc.std, desc.mean - desc.std]
                print(target_key[0])
                print(desc.mean)
                print(desc.std)
                print(weight_list[linked_key])
                print(bias_list[linked_key])
        if target_key[1] == "LNP FP" and lib: target_key = (target_key[0], "LIB FP")
        bias_infos.append((target_key[0], target_key[1], desc.mean, desc.std))
    for target_key in sorted(bias_list.keys()):
        # Non-linked pollsters have already been done
        if target_key[0] not in sum(links.values(), []): continue
        bias_arr = np.array(bias_list[target_key])
        weight_arr = np.array(weight_list[target_key])
        desc = DescrStatsW(bias_arr, weights=weight_arr)
        if target_key[1] == "LNP FP" and lib: target_key = (target_key[0], "LIB FP")
        bias_infos.append((target_key[0], target_key[1], desc.mean, desc.std))

    with open(f'{directory}/biases-{target_election.year()}{target_election.region()}.csv', 'w') as f:
        for bias_info in bias_infos:
            f.write(','.join(str(a) for a in bias_info) + '\n')

    print('Bias analysis successfully completed')


if __name__ == '__main__':

    try:
        config = Config()
        cycles = get_election_cycles()
        links = get_links()
        for election in config.elections:
            analyse_variability(election, cycles, links)
            analyse_house_effects(election, cycles, links)
            analyse_bias(election, cycles, links)
        with open(f'itsdone.txt', 'w') as f:
            f.write('1')
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        with open(f'itsdone.txt', 'w') as f:
            f.write('2')
