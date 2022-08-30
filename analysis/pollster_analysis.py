import os
import math


def analyse_pollsters():
    print("Analysing pollsters")
    directory = 'Outputs/Calibration'
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

    with open(f'{directory}/summaries.csv', 'w') as f:
        for key in sorted(weight_sums.keys()):
            weight_sum = weight_sums[key]
            weighted_error_sum = weighted_error_sums[key]
            error_average = weighted_error_sum / weight_sum
            error_stddev = error_average / math.sqrt(2 / math.pi)
            f.write(f'{key[0]},{key[1]},{error_stddev},{weight_sum - 7}\n')
    
    print('Pollster analysis successfully completed')


if __name__ == '__main__':
    analyse_pollsters()