from mailbox import linesep
import os
import math
import numpy as np
from statsmodels.stats.weightstats import DescrStatsW

directory = 'Outputs/Calibration'

def analyse_variability():
    print("Analysing variability")
    filenames = os.listdir(directory)
    weighted_error_sums = {}
    weight_sums = {}
    for filename in filenames:
        if (filename[:5]) != 'calib': continue
        _, election, pollster, party = filename.split(".")[0].split('_')
        key = (pollster, party)
        with open(f'{directory}/{filename}', 'r') as f:
            stat_strs = f.readlines()[0].split(',')[:2]
            error, weight = float(stat_strs[0]), float(stat_strs[1])
            if key not in weighted_error_sums:
                weighted_error_sums[key] = 14
                weight_sums[key] = 7
            weighted_error_sums[key] += weight * error
            weight_sums[key] += weight

    with open(f'{directory}/variability.csv', 'w') as f:
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
            final_day = max(int(a.split(',')[1]) for a in lines)
            # Only count polls that would contribute to "new" house effect
            start_day = final_day - 183
            for line in lines:
                pollster = line.split(',')[0]
                if int(line.split(',')[1]) < start_day: continue
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
    analyse_variability()
    analyse_house_effects()
    analyse_bias()