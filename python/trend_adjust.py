from scipy.interpolate import UnivariateSpline
from sklearn.linear_model import LinearRegression
from numpy import array, transpose, dot
import math
import argparse
import statistics
from election_code import ElectionCode
from poll_transform import transform_vote_share, detransform_vote_share, clamp
from sample_kurtosis import one_tail_kurtosis

poll_score_threshold = 3

# To keep analysis simple, and maintain decent sample sizes, group
# polled parties into categories with similar expected behaviour.
with open('./Data/party-groups.csv', 'r') as f:
    party_groups = {
        b[0]: b[1:] for b in
        [a.strip().split(',') for a in f.readlines()]}

average_length = {a: 6 if a == "ALP" or a == "LNP" else 1
                  for a in party_groups.keys()}

unnamed_others_code = party_groups['xOTH'][0]


no_target_election_marker = ElectionCode(0, 'none')


class ElectionPartyCode:
    def __init__(self, election, party):
        self._internal = (int(election.year()),
                          str(election.region()),
                          str(party))

    def __hash__(self):
        return hash((self._internal))

    def __eq__(self, another):
        return self._internal == another._internal

    def year(self):
        return self._internal[0]

    def region(self):
        return self._internal[1]

    def party(self):
        return self._internal[2]

    def __repr__(self):
        return (f'ElectionPartyCode({self.year()}, '
                f'{self.region()}, {self.party()})')


class ConfigError(ValueError):
    pass


class Config:
    def __init__(self):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('-f', '--files', action='store_true',
                            help='Show loaded files')
        parser.add_argument('-p', '--parameters', action='store_true',
                            help='Show parameters by party group and day')
        parser.add_argument('--election', action='store', type=str,
                            help='Exclude this election from calculations '
                            '(so that they can be used for hindcasting that '
                            'election). Enter as 1234-xxx format,'
                            ' e.g. 2013-fed. Write "none" to exclude no '
                            'elections (for present-day forecasting) or "all" '
                            'to do it for all elections (including "none"). '
                            'Default is "none"', default='none')
        parser.add_argument('-w', '--writtenfiles', action='store_true',
                            help='Show written files')
        self.show_loaded_files = parser.parse_args().files
        self.show_parameters = parser.parse_args().parameters
        self.show_written_files = parser.parse_args().writtenfiles
        self.exclude_instuctions = parser.parse_args().election.lower()
        self.prepare_election_list()
        day_test_count = 46
        self.days = [int((n * (n + 1)) / 2) for n in range(0, day_test_count)]

    def prepare_election_list(self):
        with open('./Data/ordered-elections.csv', 'r') as f:
            elections = ElectionCode.load_elections_from_file(f)
        if self.exclude_instuctions == 'all':
            self.elections = elections + [no_target_election_marker]
        elif self.exclude_instuctions == 'none':
            self.elections = [no_target_election_marker]
        else:
            parts = self.exclude_instuctions.split('-')
            if len(parts) != 2:
                raise ConfigError('Error in "elections" argument: given value '
                                  'did not consist of two parts separated '
                                  'by a hyphen (e.g. 2013-fed)')
            try:
                code = ElectionCode(parts[0], parts[1])
            except ValueError:
                raise ConfigError('Error in "elections" argument: first part'
                                  'of election name could not be converted'
                                  'into an integer')
            if code not in elections:
                raise ConfigError('Error in "elections" argument: given value'
                                  'value given did not match any election'
                                  'given in Data/ordered-elections.csv')
            self.elections = [code]


class Inputs:
    def __init__(self, exclude):
        # [0] year of election, [1] region of election
        with open('./Data/ordered-elections.csv', 'r') as f:
            self.elections = ElectionCode.load_elections_from_file(f)
        self.elections = [a for a in self.elections if a != exclude]
        # key: [0] year of election, [1] region of election
        # value: list of significant party codes modelled in that election
        with open('./Data/significant-parties.csv', 'r') as f:
            parties = {ElectionCode(a[0], a[1]): a[2:] for a in
                       [b.strip().split(',') for b in f.readlines()]
                       if ElectionCode(a[0], a[1]) != exclude}
        # key: [0] year of election, [1] region of election, [2] party code
        # value: primary vote recorded in this election
        with open('./Data/eventual-results.csv', 'r') as f:
            self.eventual_results = {
                ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]): float(a[3])
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) != exclude}
        # key: [0] year of election, [1] region of election, [2] party code
        # value: primary vote recorded in the previous election
        with open('./Data/prior-results.csv', 'r') as f:
            linelists = [b.strip().split(',') for b in f.readlines()]
            self.prior_results = {
                ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]):
                [float(x) for x in a[3:]] for a in linelists
                if ElectionCode(a[0], a[1]) != exclude}

        # Note: These two inputs aren't currently used, but leaving them here
        # in case they are found to be useful again.

        # stores: first incumbent party, then main opposition party,
        # finally years incumbent party has been in power
        with open('./Data/incumbency.csv', 'r') as f:
            self.incumbency = {
                ElectionCode(a[0], a[1]): (a[2], a[3], float(a[4])) for a in
                [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) != exclude}
        # stores: party corresponding to federal government,
        # then party opposing federal government
        with open('./Data/federal-situation.csv', 'r') as f:
            self.federal_situation = {
                ElectionCode(a[0], a[1]): (a[2], a[3])
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) != exclude}

        # Trim party list so that we only store it for completed elections
        self.parties = {e: parties[e] for e in self.elections}
        # Create averages of prior results
        avg_counts = list(range(1, 9))
        self.avg_prior_results = {
            avg_n: {
                k: sum(v[:avg_n]) / avg_n
                for k, v in self.prior_results.items()
            } for avg_n in avg_counts}
        self.studied_elections = self.elections + [no_target_election_marker]
        self.fundamentals = {}  # Filled in later

    def determine_eventual_others_results(self):
        for e in self.elections:
            others_code = ElectionPartyCode(e, 'OTH FP')
            eventual_others = self.eventual_results[others_code]
            eventual_named = 0
            for p in self.parties[e]:
                party_code = ElectionPartyCode(e, p)
                if p not in not_others and party_code in self.eventual_results:
                    eventual_named += self.eventual_results[party_code]
            eventual_unnamed = eventual_others - eventual_named
            unnamed_code = ElectionPartyCode(e, unnamed_others_code)
            self.eventual_results[unnamed_code] = eventual_unnamed
            self.parties[e].append(unnamed_others_code)


class PollTrend:
    def __init__(self, inputs, config):
        self._data = {}
        for election, party_list in inputs.parties.items():
            for party in party_list:
                if party == unnamed_others_code:
                    continue
                trend_filename = (f'./Outputs/fp_trend_{election.year()}'
                                  f'{election.region()}_{party}.csv')
                if config.show_loaded_files:
                    print(trend_filename)
                data = import_trend_file(trend_filename)
                self._data[ElectionPartyCode(election, party)] = data
            self._data[ElectionPartyCode(election, unnamed_others_code)] = \
                self.create_exclusive_others_series(election, party_list)

    def value_at(self, party_code, day, percentile, default_value=None):
        if day >= len(self._data[party_code]) or day < 0:
            return default_value
        return self._data[party_code][day][percentile]

    # Create exclusive others raw series
    def create_exclusive_others_series(self, election, party_list):
        series = []
        # Base of 3% for unnamed others mirrors the C++ code
        unnamed_others_base = 3
        for day in range(0, len(self._data[
                ElectionPartyCode(election, party_list[0])])):
            median = 0  # Median values for minor parties
            for party in party_list:
                if party not in not_others:
                    code = ElectionPartyCode(election, party)
                    median += self.value_at(code, day, 50)
            oth_code = ElectionPartyCode(election, 'OTH FP')
            oth_median = self.value_at(oth_code, day, 50)
            modified_oth_median = max(oth_median, median + unnamed_others_base)
            xoth_proportion = 1 - median / modified_oth_median
            spread = []
            for value in range(0, 101):
                oth_value = self.value_at(oth_code, day, value)
                spread.append(oth_value * xoth_proportion)
            series.append(spread)
        return series


class Outputs:
    def __init__(self):
        self.sum_squared_errors = {}
        self.error_count = {}
        self.estimations = {}
        self.raw_params = {}
        self.rmse = {}


class RegressionInputs:
    def __init__(self, complete_info, info, transformed_results):
        self.complete_info = complete_info
        self.info = info
        self.transformed_results = transformed_results


# Parties that shouldn't be included as part of the OTH FP
# when calculating exclusive-others vote shares
not_others = ['ALP FP', 'LNP FP', 'LIB FP', 'NAT FP', 'GRN FP', 'OTH FP']


def run_non_poll_regression(inputs):
    previous_errors = []
    prediction_errors = []
    for party_group_code, party_group_list in party_groups.items():
        avg_len = average_length[party_group_code]
        for studied_election in inputs.elections:
            result_deviations = []
            incumbents = []
            oppositions = []
            incumbency_lengths = []
            opposition_lengths = []
            federal_sames = []
            federal_opposites = []
            for election in inputs.elections:
                if election == studied_election:
                    continue
                for party in inputs.parties[election] + [unnamed_others_code]:
                    if party not in party_group_list:
                        continue
                    e_p_c = ElectionPartyCode(election, party)
                    eventual_results = (inputs.eventual_results[e_p_c]
                                        if e_p_c in inputs.eventual_results else 0)
                    result_deviation = eventual_results - inputs.avg_prior_results[avg_len][e_p_c]
                    incumbent = 1 if inputs.incumbency[election][0] == party else 0
                    opposition = 1 if inputs.incumbency[election][1] == party else 0
                    incumbency_length = (inputs.incumbency[election][2]
                                        if incumbent else 0)
                    opposition_length = (inputs.incumbency[election][2]
                                        if opposition else 0)
                    federal = 1 if election.region() == 'fed' else 0
                    federal_same = 1 if not federal and inputs.federal_situation[election][0] == party else 0
                    federal_opposite = 1 if not federal and inputs.federal_situation[election][1] == party else 0
                    result_deviations.append(result_deviation)
                    incumbents.append(incumbent)
                    oppositions.append(opposition)
                    incumbency_lengths.append(incumbency_length)
                    opposition_lengths.append(opposition_length)
                    federal_sames.append(federal_same)
                    federal_opposites.append(federal_opposite)
            input_array = array([incumbents,
                                oppositions,
                                incumbency_lengths,
                                opposition_lengths,
                                federal_sames,
                                federal_opposites
                                ])
            input_array = transpose(input_array)
            dependent_array = array(result_deviations)
            reg = LinearRegression().fit(input_array, dependent_array)
            # Test with studied election information:
            for party in inputs.parties[studied_election]:
                if party not in party_group_list:
                    continue
                e_p_c = ElectionPartyCode(studied_election, party)
                eventual_results = (inputs.eventual_results[e_p_c]
                                    if e_p_c in inputs.eventual_results else 0)
                result_deviation = eventual_results - inputs.avg_prior_results[avg_len][e_p_c]
                incumbent = 1 if inputs.incumbency[studied_election][0] == party else 0
                opposition = 1 if inputs.incumbency[studied_election][1] == party else 0
                incumbency_length = (inputs.incumbency[studied_election][2]
                                    if incumbent else 0)
                opposition_length = (inputs.incumbency[studied_election][2]
                                    if opposition else 0)
                federal = 1 if studied_election.region() == 'fed' else 0
                federal_same = 1 if not federal and inputs.federal_situation[studied_election][0] == party else 0
                federal_opposite = 1 if not federal and inputs.federal_situation[studied_election][1] == party else 0
                input_array = array([incumbent,
                                opposition,
                                incumbency_length,
                                opposition_length,
                                federal_same,
                                federal_opposite
                                ])
                prediction = (inputs.avg_prior_results[avg_len][e_p_c] +
                            dot(input_array, reg.coef_) + reg.intercept_)
                previous_errors.append(inputs.avg_prior_results[avg_len][e_p_c]
                                    - eventual_results)
                prediction_errors.append(prediction - eventual_results)
                inputs.fundamentals[e_p_c] = prediction

        print(f'Party group: {party_group_code}')
        previous_rmse = math.sqrt(sum([a ** 2 for a in previous_errors])
                                / (len(previous_errors) - 1))
        prediction_rmse = math.sqrt(sum([a ** 2 for a in prediction_errors])
                                / (len(prediction_errors) - 1))
        print(f'RMSEs: previous {previous_rmse} vs prediction {prediction_rmse}')
        previous_average_error = statistics.mean([abs(a) for a in previous_errors])
        prediction_average_error = statistics.mean([abs(a) for a in prediction_errors])
        print(f'Average errors: previous {previous_average_error} vs prediction {prediction_average_error}')
        previous_median_error = statistics.median([abs(a) for a in previous_errors])
        prediction_median_error = statistics.median([abs(a) for a in prediction_errors])
        print(f'Median errors: previous {previous_median_error} vs prediction {prediction_median_error}')
    for e_p_c, prediction in inputs.fundamentals.items():
        print(f'{e_p_c} - prediction: {prediction}')
        if e_p_c in inputs.eventual_results:
            print(f'{e_p_c} - actual: {inputs.eventual_results[e_p_c]}')


def import_trend_file(filename):
    with open(filename, 'r') as f:
        lines = f.readlines()
    lines = [line.strip().split(',')[2:] for line in lines[3:]]
    lines = [[float(a) for a in line] for line in lines]
    lines.reverse()
    return lines


# force_monotone: will look at the endpoints
# to determine direction of monotonicity
def print_smoothed_series(config, label, some_dict, file,
                          force_monotone=False,
                          bounds=[-math.inf, math.inf]):
    x_orig, y = zip(*some_dict.items())
    x = range(0, len(x_orig))
    total_days = x_orig[len(x_orig) - 1]
    w = [10 if a == 0 else 1 for a in x]
    spline = UnivariateSpline(x=x, y=y, w=w, s=1000)
    full_spline = spline(x)
    full_spline = {x_orig[a]: b for a, b in enumerate(full_spline)}
    if config.show_parameters:
        joined = '\n'.join([f'{a}: {b:.4f}' for a, b in full_spline.items()])
        print(f'{label} smoothed: {joined}\n')
    daily_x = [.5 * (math.sqrt(8 * n + 1) - 1)
               for n in range(0, total_days + 1)]
    daily_spline = list(spline(daily_x))
    if force_monotone:
        if daily_spline[len(daily_spline) - 1] > daily_spline[0]:
            for day in range(0, len(daily_spline) - 1):
                new_val = max(daily_spline[day + 1], daily_spline[day])
                daily_spline[day + 1] = new_val
        else:
            for day in range(0, len(daily_spline) - 1):
                new_val = min(daily_spline[day + 1], daily_spline[day])
                daily_spline[day + 1] = new_val
    for day in range(0, len(daily_spline)):
        daily_spline[day] = clamp(daily_spline[day], bounds[0], bounds[1])
    file.write(','.join([f'{a:.4f}' for a in daily_spline]) + '\n')


def smoothed_median(container, smoothing):
    s = sorted(container)
    n = len(s)
    high_mid = math.floor(n / 2)
    low_mid = high_mid - 1 if n % 2 == 0 else high_mid
    high_end = min(high_mid + smoothing + 1, n)
    low_end = max(low_mid - smoothing, 0)
    return statistics.mean(s[low_end:high_end])


class BiasData:
    def __init__(self):
        self.previous_errors = []
        self.poll_errors = []
        self.studied_previous_error = None
        self.studied_poll_errors = []
        self.studied_poll_parties = []


def get_bias_data(inputs, poll_trend, party_group,
                  day, avg_n, studied_election):
    biasData = BiasData()
    for other_election in inputs.elections:
        for party in party_groups[party_group]:
            if party not in inputs.parties[other_election]:
                continue
            party_code = ElectionPartyCode(other_election, party)
            polls = poll_trend.value_at(party_code, day, 50)
            result = (inputs.eventual_results[party_code]
                      if party_code in inputs.eventual_results else 0.5)
            result_t = transform_vote_share(result)

            previous = inputs.avg_prior_results[avg_n][party_code]

            if previous is not None:
                previous_error = transform_vote_share(previous) - result_t
                if other_election == studied_election:
                    biasData.studied_previous_error = previous_error
                else:
                    biasData.previous_errors.append(previous_error)

                if polls is not None:
                    poll_error = transform_vote_share(polls) - result_t
                    if other_election == studied_election:
                        biasData.studied_poll_errors.append(poll_error)
                        biasData.studied_poll_parties.append(party)
                    else:
                        biasData.poll_errors.append(poll_error)
    return biasData


class DayData:
    def __init__(self):
        self.mixed_errors = [[], []]
        self.previous_debiased_errors = []
        self.poll_debiased_errors = []
        self.overall_poll_biases = []
        self.overall_previous_biases = []
        self.final_mix_factor = 0


def get_single_election_data(inputs, poll_trend, party_group, day_data, day,
                             avg_n, studied_election, mix_limits):
    bias_data = get_bias_data(inputs=inputs,
                              poll_trend=poll_trend,
                              party_group=party_group,
                              day=day,
                              avg_n=avg_n,
                              studied_election=studied_election)
    previous_bias = statistics.median(bias_data.previous_errors)
    poll_bias = statistics.median(bias_data.poll_errors)
    day_data.overall_previous_biases.append(previous_bias)
    day_data.overall_poll_biases.append(poll_bias)
    if studied_election == no_target_election_marker:
        return
    if bias_data.studied_previous_error is not None:
        previous_debiased_error = (bias_data.studied_previous_error
                                   - previous_bias)
        if mix_limits == (0, 1):
            day_data.previous_debiased_errors.append(previous_debiased_error)
    if len(bias_data.studied_poll_errors) > 0:
        zipped_bias_data = zip(bias_data.studied_poll_errors,
                               bias_data.studied_poll_parties)
        for studied_poll_error, studied_poll_party in zipped_bias_data:
            poll_debiased_error = studied_poll_error - poll_bias
            if mix_limits == (0, 1):
                day_data.poll_debiased_errors.append(poll_debiased_error)
            party_code = ElectionPartyCode(studied_election,
                                           studied_poll_party)
            previous = inputs.avg_prior_results[avg_n][party_code]
            debiased_previous = transform_vote_share(previous) - previous_bias
            polls = poll_trend.value_at(party_code, day, 50)
            debiased_polls = transform_vote_share(polls) - poll_bias
            result = (max(0.5, inputs.eventual_results[party_code])
                      if party_code in inputs.eventual_results else 0.5)
            result_t = transform_vote_share(result)
            for mix_index, mix_factor in enumerate(mix_limits):
                mixed = (debiased_polls * mix_factor
                         + debiased_previous * (1 - mix_factor))
                mixed_error = mixed - result_t
                day_data.mixed_errors[mix_index].append(mixed_error)


def get_day_data(inputs, poll_trend, party_group, day, avg_n):
    day_data = DayData()
    mix_limits = (0, 1)
    while mix_limits[1] - mix_limits[0] > 0.0001:
        day_data.mixed_errors = [[], []]
        for studied_election in inputs.studied_elections:
            get_single_election_data(inputs=inputs,
                                     poll_trend=poll_trend,
                                     party_group=party_group,
                                     day=day,
                                     avg_n=avg_n,
                                     studied_election=studied_election,
                                     day_data=day_data,
                                     mix_limits=mix_limits)
        rmse_factor = 0.3
        mixed_criteria = [0, 0]
        for mix_index in range(0, len(mix_limits)):
            mixed_deviation = smoothed_median(
                [abs(a) for a in day_data.mixed_errors[mix_index]], 2)
            mixed_rmse = math.sqrt(
                sum([a ** 2 for a in day_data.mixed_errors[mix_index]])
                / len(day_data.mixed_errors[mix_index]))
            mixed_criteria[mix_index] = (mixed_rmse * rmse_factor
                                         + mixed_deviation * (1 - rmse_factor))
        window_factor = 0.8  # should be in range [0.5, 1)
        if mixed_criteria[0] < mixed_criteria[1]:
            mix_limits = (mix_limits[0],
                          mix_limits[0] * (1 - window_factor)
                          + mix_limits[1] * window_factor)
        else:
            mix_limits = (mix_limits[1] * (1 - window_factor)
                          + mix_limits[0] * window_factor,
                          mix_limits[1])
    day_data.final_mix_factor = statistics.mean(mix_limits)
    return day_data


class PartyData:
    def __init__(self):
        self.poll_biases = {}
        self.previous_biases = {}
        self.biases = {}
        self.deviations = {}
        self.lower_rmses = {}
        self.upper_rmses = {}
        self.lower_kurtoses = {}
        self.upper_kurtoses = {}
        self.final_mix_factors = {}


def get_party_data(config, inputs, poll_trend, party_group, avg_n):
    party_data = PartyData()
    for day in config.days:
        day_data = get_day_data(inputs=inputs,
                                poll_trend=poll_trend,
                                party_group=party_group,
                                day=day,
                                avg_n=avg_n)
        if day == 0:
            previous_bias = smoothed_median(
                day_data.previous_debiased_errors, 2)
        poll_bias = smoothed_median(
            day_data.overall_poll_biases, 2)
        previous_bias = smoothed_median(
            day_data.overall_previous_biases, 2)
        mixed_bias = smoothed_median(day_data.mixed_errors[1], 2)
        mixed_deviation = smoothed_median(
            [abs(a) for a in day_data.mixed_errors[1]], 2)
        lower_errors = [a - mixed_bias
                        for a in day_data.mixed_errors[1]
                        if a < mixed_bias]
        upper_errors = [a - mixed_bias
                        for a in day_data.mixed_errors[1]
                        if a >= mixed_bias]
        lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors])
                               / (len(lower_errors) - 1))
        upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors])
                               / (len(upper_errors) - 1))
        lower_kurtosis = one_tail_kurtosis(lower_errors)
        upper_kurtosis = one_tail_kurtosis(upper_errors)
        party_data.poll_biases[day] = poll_bias
        party_data.previous_biases[day] = previous_bias
        party_data.biases[day] = mixed_bias
        party_data.deviations[day] = mixed_deviation
        party_data.lower_rmses[day] = lower_rmse
        party_data.upper_rmses[day] = upper_rmse
        party_data.lower_kurtoses[day] = lower_kurtosis
        party_data.upper_kurtoses[day] = upper_kurtosis
        party_data.final_mix_factors[day] = day_data.final_mix_factor
    return party_data


def save_party_data(config, party_data, exclude, party_group):
    filename = (f'./Adjustments/adjust_{exclude.year()}'
                f'{exclude.region()}_{party_group}.csv')
    with open(filename, 'w') as f:
        print_smoothed_series(config, 'Poll Bias',
                              party_data.poll_biases, f)
        print_smoothed_series(config, 'Previous-Elections Bias',
                              party_data.previous_biases, f)
        print_smoothed_series(config, 'Mixed Bias',
                              party_data.biases, f)
        print_smoothed_series(config, 'Lower Error',
                              party_data.lower_rmses, f,
                              force_monotone=True, bounds=(0, math.inf))
        print_smoothed_series(config, 'Upper Error',
                              party_data.upper_rmses, f,
                              force_monotone=True, bounds=(0, math.inf))
        print_smoothed_series(config, 'Lower Kurtosis',
                              party_data.lower_kurtoses, f,
                              bounds=(3, math.inf))
        print_smoothed_series(config, 'Upper Kurtosis',
                              party_data.upper_kurtoses, f,
                              bounds=(3, math.inf))
        print_smoothed_series(config, 'Mix factor',
                              party_data.final_mix_factors, f,
                              force_monotone=True, bounds=(0, 1))
        if config.show_written_files:
            print(f'Wrote parameter data to: {filename}')


def test_procedure(config, inputs, poll_trend, exclude):
    for party_group in party_groups.keys():
        print(f'*** DETERMINING TREND ADJUSTMENTS FOR PARTY GROUP'
              f' {party_group} ***')
        avg_n = average_length[party_group]
        party_data = get_party_data(config=config,
                                    inputs=inputs,
                                    poll_trend=poll_trend,
                                    party_group=party_group,
                                    avg_n=avg_n)
        save_party_data(config=config,
                        party_data=party_data,
                        exclude=exclude,
                        party_group=party_group)


def trend_adjust():
    try:
        config = Config()
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return

    for exclude in config.elections:
        print(f'Beginning trend adjustment algorithm for: {exclude}')
        inputs = Inputs(exclude)
        run_non_poll_regression(inputs)
        poll_trend = PollTrend(inputs, config)
        outputs = Outputs()

        # Leave this until now so it doesn't interfere with initialization
        # of poll_trend
        inputs.determine_eventual_others_results()

        test_procedure(config, inputs, poll_trend, exclude)
        print(f'Completed trend adjustment algorithm for: {exclude}')


if __name__ == '__main__':
    trend_adjust()
