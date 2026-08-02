"""Federal regional-polling analysis used by the seat simulation.

Parent: election_analysis.py runs this federal regional component alongside
the party and seat analyses that form the historical analysis bundle.

Main functions:
* ``analyse_region_swings`` performs the actual regional polling/swing
  regression and writes the selected-factor output consumed by simulation.
* ``regress_and_write_to_file`` fits and serializes one regional relationship.
* ``RegionPolls`` holds aligned observations while the calculation is built.
"""

import copy
import math
import statistics

import numpy
from scipy.optimize import curve_fit
from sklearn.linear_model import LinearRegression

from election_analysis_common import (
    extend_region_errors_with_selected_factor,
    one_tail_kurtosis,
)
from election_code import ElectionCode
from poll_transform import clamp
from sample_kurtosis import calc_rmse, two_tail_kurtosis


class RegionPolls:
    def __init__(self):
        self.prev_tpp = 50
        self.next_tpp = 50
        self.next_deviation = 50
        self.polls = []
        self.deviations = []
        self.population = 0


    def __repr__(self):
        return (f'prev_tpp: {self.prev_tpp}, next_tpp: {self.next_tpp}, '
                f'next_deviation: {self.next_deviation}, '
                f'population: {self.population}, '
                f'polls: {self.polls}, deviations: {self.deviations}')


# Core federal regional-swing calculation

def regress_and_write_to_file(f, inputs, outputs, region):
    inputs_array = numpy.transpose(numpy.array([inputs]))
    results_array = numpy.array(outputs)
    reg = LinearRegression().fit(inputs_array, results_array)
    residuals = [outputs[a] -
                 (reg.coef_[0] * inputs[a] + reg.intercept_)
                 for a in range(0,len(outputs))]
    rmse = math.sqrt(sum([a ** 2 for a in residuals])
                        / (len(residuals) - 1))
    kurtosis = one_tail_kurtosis(residuals)
    f.write(f'{region},{reg.coef_[0]},{reg.intercept_},{rmse},{kurtosis}\n')
    return reg


def analyse_region_swings():
    target_year = 2028
    election_results = {}
    state_results = {}
    filename = './Data/tpp-fed-regions.csv'
    with open(filename, 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        for a in linelists:
            code = ElectionCode(a[0], a[1])
            results = (float(a[3]), float(a[4]))
            if a[2] == 'all':
                election_results[code] = results
            else:
                state_results[(code, a[2])] = results

    poll_lists = {}
    highest_poll_number = 0

    filename = './Data/region-polls-fed.csv'
    with open(filename, 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        for a in linelists:
            code = ElectionCode(a[0], a[1])
            region = a[2]
            region_polls = RegionPolls()
            region_polls.prev_tpp = float(a[3])
            region_polls.next_tpp = float(a[4])
            region_polls.population = int(a[5])
            for i in range(6, len(a)):
                region_polls.polls.append(float(a[i]))
            if code not in poll_lists:
                poll_lists[code] = {}
            poll_lists[code][region] = region_polls

    next_overall_tpps = {}
    prev_overall_tpps = {}
    poll_overall_tpps = {}
    for election, poll_list in poll_lists.items():
        total_population = sum([i.population for i in poll_list.values()])
        next_tpp_overall = sum([i.next_tpp * i.population / total_population
                                for i in poll_list.values()])
        prev_tpp_overall = sum([i.prev_tpp * i.population / total_population
                                for i in poll_list.values()])
        next_swing_overall = next_tpp_overall - prev_tpp_overall
        overall_tpps = []
        for j in range(0, len(next(iter(poll_list.values())).polls)):
            overall_tpps.append(sum([i.polls[j] * i.population / total_population
                                for i in poll_list.values()]))
        next_overall_tpps[election] = next_tpp_overall
        prev_overall_tpps[election] = prev_tpp_overall
        poll_overall_tpps[election] = overall_tpps
        for region, polls in poll_lists[election].items():
            polls.next_deviation = (polls.next_tpp - polls.prev_tpp
                                    - next_swing_overall)
            highest_poll_number = max(highest_poll_number, len(polls.polls))
            for i in range(0, len(polls.polls)):
                polls.deviations.append((polls.polls[i] - polls.prev_tpp
                                         - overall_tpps[i] + prev_tpp_overall))


    fed_swings = {}
    state_swings = {}
    weights = {}
    for election, result in state_results.items():
        if election[0].year() >= target_year:
            continue
        if target_year - election[0].year() > 25:
            continue
        if election[1] not in fed_swings:
            fed_swings[election[1]] = []
            state_swings[election[1]] = []
        fed_swings[election[1]].append(election_results[election[0]][1])
        state_swings[election[1]].append(result[1])
        # swing_deviation = result[1] - election_results[election[0]][1]
        # print(f'{election}, {result}, {swing_deviation}')

    naive_coefficients = {}
    naive_intercepts = {}

    output_filename = (f'./Regional/{target_year}fed-regions-base.csv')
    with open(output_filename, 'w') as f:
        for key in fed_swings.keys():
            reg = regress_and_write_to_file(f, fed_swings[key], state_swings[key], key)
            naive_coefficients[key] = float(reg.coef_[0])
            naive_intercepts[key] = float(reg.intercept_)
        # Note: following relies on ordered dicts (python 3.6+)
        flat_fed_swings = [a for sublist in fed_swings.values() for a in sublist]
        flat_state_swings = [a for sublist in state_swings.values() for a in sublist]
        regress_and_write_to_file(f, flat_fed_swings, flat_state_swings, 'all')

    best_mix_factors = []
    best_rmses = []
    best_kurtoses = []
    region_errors = {'all': []}
    # Later snapshots are available for progressively fewer elections. Keep
    # only points supported by at least three independent election samples;
    # with the current source data this retains snapshots 0 through 8.
    minimum_elections_per_snapshot = 3
    for poll_number in range(highest_poll_number):
        poll_deviations = {}
        next_deviations = {}
        for election, election_polls in poll_lists.items():
            for region, region_polls in election_polls.items():
                if poll_number >= len(region_polls.deviations):
                    continue
                if region not in poll_deviations:
                    poll_deviations[region] = []
                    next_deviations[region] = []
                poll_deviations[region].append(region_polls.deviations[poll_number])
                next_deviations[region].append(region_polls.next_deviation)

        if (
            not poll_deviations
            or any(
                len(region_values) < minimum_elections_per_snapshot
                for region_values in poll_deviations.values()
            )
        ):
            break

        if poll_number == 0:
            polled_coefficients = {}
            polled_intercepts = {}
            output_filename = (f'./Regional/{target_year}fed-regions-polled.csv')
            with open(output_filename, 'w') as f:
                for region in poll_deviations.keys():
                    reg = regress_and_write_to_file(f, poll_deviations[region], next_deviations[region], region)
                    polled_coefficients[region] = float(reg.coef_[0])
                    polled_intercepts[region] = float(reg.intercept_)
                # Note: following relies on ordered dicts (python 3.6+)
                flat_poll_dev = [a for sublist in poll_deviations.values() for a in sublist]
                flat_actual_dev = [a for sublist in next_deviations.values() for a in sublist]
                region = 'all'
                reg = regress_and_write_to_file(f, flat_poll_dev, flat_actual_dev, region)
                polled_coefficients[region] = float(reg.coef_[0])
                polled_intercepts[region] = float(reg.intercept_)

        mixed_rmses = {}
        mixed_kurtoses = {}
        errors_by_factor = {}
        for mix_factor in [a / 100 for a in range(1, 101)]:
            factor_region_errors = {'all': []}

            for election, poll_overall_tpp in poll_overall_tpps.items():
                for region in poll_deviations.keys():
                    if poll_number >= len(poll_overall_tpp):
                        continue
                    polled_coefficient = (polled_coefficients[region] * 0.5 +
                                         polled_coefficients['all'] * 0.5)
                    polled_intercept = (polled_intercepts[region] * 0.5 +
                                       polled_intercepts['all'] * 0.5)
                    poll_overall_swing = poll_overall_tpp[poll_number] - prev_overall_tpps[election]
                    polled_region_swing = (poll_lists[election][region].polls[poll_number]
                                                - poll_lists[election][region].prev_tpp)
                    polled_raw_deviation = polled_region_swing - poll_overall_swing
                    naive_region_swing = (naive_coefficients[region] *
                                        poll_overall_swing +
                                        naive_intercepts[region])
                    naive_deviation = naive_region_swing - poll_overall_swing
                    polled_final_deviation = (polled_coefficient *
                                            polled_raw_deviation +
                                            polled_intercept)
                    actual_overall_swing = next_overall_tpps[election] - prev_overall_tpps[election]
                    actual_region_swing = (poll_lists[election][region].next_tpp -
                                           poll_lists[election][region].prev_tpp)
                    actual_deviation = actual_region_swing - actual_overall_swing
                    mixed_deviation = (polled_final_deviation * mix_factor +
                                    naive_deviation * (1 - mix_factor))
                    mixed_error = mixed_deviation - actual_deviation
                    factor_region_errors.setdefault(region, []).append(
                        mixed_error
                    )
                    factor_region_errors['all'].append(mixed_error)

            mixed_errors = factor_region_errors['all']
            errors_by_factor[mix_factor] = factor_region_errors
            mixed_rmse = calc_rmse(mixed_errors)
            mixed_rmses[mix_factor] = mixed_rmse
            mixed_kurtosis = one_tail_kurtosis(mixed_errors)
            mixed_kurtoses[mix_factor] = mixed_kurtosis

        best_mix_factor = min(mixed_rmses, key=mixed_rmses.get)
        best_mix_factors.append(best_mix_factor)
        best_rmses.append(mixed_rmses[best_mix_factor])
        best_kurtoses.append(mixed_kurtoses[best_mix_factor])
        extend_region_errors_with_selected_factor(
            region_errors, errors_by_factor, best_mix_factor
        )

    all_rmse = calc_rmse(region_errors['all'])
    output_filename = (f'./Regional/{target_year}fed-mix-regions.csv')
    with open(output_filename, 'w') as f:
        for region, error_list in region_errors.items():
            if region == 'all':
                continue
            region_bias = statistics.median(error_list)
            region_rmse = calc_rmse(error_list, region_bias)
            rmse_modifier = region_rmse / all_rmse
            # To account for small sample size, which may result in
            # rmse factors being underestimated, increase the rmse
            # factor a bit for a region when it's under the overall average
            if rmse_modifier < 1: rmse_modifier = rmse_modifier * 0.5 + 0.5
            f.write(f'{region},{region_bias},{rmse_modifier}\n')


    def func(x, a, b):
        return a*numpy.exp(-b*x)

    def func2(x, a, b, c):
        return a*numpy.exp(-b*x)+c

    def func3(x, a, b):
        return a*x+b

    # In order to avoid too sharp a change after the first timepoint,
    # add a dummy value linearly interpolated between the two so that
    # smooths down more clearly
    dummy_time = 0.25
    x_list = list(range(0, len(best_mix_factors))) + [dummy_time]
    x = numpy.array(x_list)

    mix_factor_dummy = (best_mix_factors[0] * (1 - dummy_time) +
                        best_mix_factors[1] * dummy_time)
    y = numpy.array(best_mix_factors + [mix_factor_dummy])
    mix_factor_params, mix_factor_cov = curve_fit(func, x, y, [1, 1])

    rmse_dummy = (best_rmses[0] * (1 - dummy_time) +
                  best_rmses[1] * dummy_time)
    y = numpy.array(best_rmses + [rmse_dummy])
    rmse_params, rmse_cov = curve_fit(func2, x, y, [-1, 0.1, 2.5])

    kurtosis_dummy = (best_kurtoses[0] * (1 - dummy_time) +
                      best_kurtoses[1] * dummy_time)
    y = numpy.array(best_kurtoses + [kurtosis_dummy])
    kurtosis_params, kurtosis_cov = curve_fit(func3, x, y, [2, 1])

    output_filename = (f'./Regional/{target_year}fed-mix-parameters.csv')
    with open(output_filename, 'w') as f:
        f.write(f'mix_factor,{mix_factor_params[0]},{mix_factor_params[1]}\n')
        f.write(f'rmse,{rmse_params[0]},{rmse_params[1]},{rmse_params[2]}\n')
        f.write(f'kurtosis,{kurtosis_params[0]},{kurtosis_params[1]}\n')


majors = ['Liberal', 'National', 'Liberal National', 'Labor', 'Country Liberal']
