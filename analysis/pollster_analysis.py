from mailbox import linesep
import os
import math

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


# Which pollsters' house effects are usually close to the middle
# of their elections' trend lines
def analyse_anchoring():
    print("Analysing anchoring")
    filenames = os.listdir(directory)
    n_polls = {}
    abs_he_sums = {}
    abs_he_weights = {}

    # get number of polls in election
    for filename in filenames:
        if (filename[:4]) != 'fp_p': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[2:4]
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
        


    for filename in filenames:
        if (filename[:4]) != 'fp_h': continue
        if 'biascal' not in filename: continue
        election, party = filename.split(".")[0].split('_')[3:5]
        with open(f'{directory}/{filename}', 'r') as f:
            lines = []
            for line in f:
                if line[:4] == 'Hous' or line[:4] == 'New ': continue
                if line[:4] == 'Old ': break
                lines.append(line)
            data = {line.split(',')[0]: float(line.split(',')[7])
                    for line in lines}
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
    
    with open(f'{directory}/anchor_weighting.csv', 'w') as f:
        for key in sorted(abs_he_sums.keys()):
            average_he = abs_he_sums[key] / abs_he_weights[key]
            weighting = 1 / average_he
            if abs_he_weights[key] == 0.4: continue
            f.write(f'{key[0]},{key[1]},{weighting}\n')
    
    print('Anchoring analysis successfully completed')
        


if __name__ == '__main__':
    analyse_variability()
    analyse_anchoring()