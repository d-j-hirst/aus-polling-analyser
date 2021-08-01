from scipy.interpolate import UnivariateSpline
import math
import argparse
import statistics

poll_score_threshold = 3

# To keep analysis simple, and maintain decent sample sizes, group
# polled parties into categories with similar expected behaviour.
with open('./Data/party-groups.csv', 'r') as f:
    party_groups = {
        b[0]: b[1:] for b in
        [a.strip().split(',') for a in f.readlines()]}

unnamed_others_code = party_groups['xOTH'][0]

class ElectionCode:
    def __init__(self, year, region):
        self._internal = (int(year), str(region))
    
    def __hash__(self):
        return hash(self._internal)
    
    def __eq__(self, another):
        return self._internal == another._internal
    
    def year(self):
        return self._internal[0]
    
    def region(self):
        return self._internal[1]

    def __repr__(self):
        return f'ElectionCode({self.year()}, {self.region()})'
    
    @staticmethod
    def load_elections_from_file(file):
        split_lines = [line.strip().split(',') for line in file.readlines()]
        return [ElectionCode(int(a[0]), a[1]) for a in split_lines]

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
        parser.add_argument('-p', '--previous', action='store_true',
                            help='Show hindcasted previous elections')
        parser.add_argument('-e', '--errors', action='store_true',
                            help='Show total errors by day')
        parser.add_argument('-c', '--parameters', action='store_true',
                            help='Show parameters for selected day')
        parser.add_argument('--day', action='store', type=int,
                            help='Day to display coefficients for (default: 0)')
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
        self.show_previous_elections = parser.parse_args().previous
        self.show_errors_by_day = parser.parse_args().errors
        self.show_parameters = parser.parse_args().parameters
        self.feedback_day = parser.parse_args().day
        self.show_written_files = parser.parse_args().writtenfiles
        self.exclude_instuctions = parser.parse_args().election.lower()
        self.prepare_election_list()
        day_test_count = 46
        self.days = [int((n * (n + 1)) / 2) for n in range(0, day_test_count)]
        self.adjust_feedback_day()
    
    def adjust_feedback_day(self):
        if self.feedback_day not in self.days:
            for day in self.days:
                if self.feedback_day < day:
                    self.feedback_day = day
                    break
            if self.feedback_day not in self.days:
                self.feedback_day=self.days[-1]
    
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
            self.eventual_results = {ElectionPartyCode(ElectionCode(a[0], a[1]), a[2])
                    : float(a[3]) for a in
                    [b.strip().split(',') for b in f.readlines()]
                    if ElectionCode(a[0], a[1]) != exclude}
        # key: [0] year of election, [1] region of election, [2] party code
        # value: primary vote recorded in the previous election
        with open('./Data/prior-results.csv', 'r') as f:
            linelists = [b.strip().split(',') for b in f.readlines()]
            self.prior_results = {ElectionPartyCode(ElectionCode(a[0], a[1]), a[2])
                : [float(x) for x in a[3:]]
                for a in linelists
                if ElectionCode(a[0], a[1]) != exclude}
        # stores: first incumbent party, then main opposition party,
        # finally years incumbent party has been in power
        with open('./Data/incumbency.csv', 'r') as f:
            self.incumbency = {ElectionCode(a[0], a[1]) : (a[2], a[3], float(a[4])) for a in
                    [b.strip().split(',') for b in f.readlines()]
                    if ElectionCode(a[0], a[1]) != exclude}
        # stores: party corresponding to federal government, 
        # then party opposing federal government
        with open('./Data/federal-situation.csv', 'r') as f:
            self.federal_situation = {ElectionCode(a[0], a[1]) : (a[2], a[3]) for a in
                    [b.strip().split(',') for b in f.readlines()]
                    if ElectionCode(a[0], a[1]) != exclude}
        # Trim party list so that we only store it for completed elections
        self.parties = {e: parties[e] for e in self.elections}
        # Create averages of prior results
        avg_counts = list(range(1,9))
        self.avg_prior_results = {
            avg_n: {
                k: sum(v[:avg_n]) / avg_n
                for k, v in self.prior_results.items()
            } for avg_n in avg_counts}
        self.studied_elections = self.elections + [no_target_election_marker]

    def determine_eventual_others_results(self):
        for e in self.elections:
            eventual_others = self.eventual_results[ElectionPartyCode(e, 'OTH FP')]
            eventual_named = 0
            for p in self.parties[e]:
                party_code = ElectionPartyCode(e, p)
                if p not in not_others and party_code in self.eventual_results:
                    eventual_named += self.eventual_results[party_code]
            eventual_unnamed = eventual_others - eventual_named
            self.eventual_results[ElectionPartyCode(e, unnamed_others_code)] = eventual_unnamed
            self.parties[e].append(unnamed_others_code)


class PollTrend:
    def __init__(self, inputs, config):
        self._data = {}
        for election, party_list in inputs.parties.items():
            for party in party_list:
                if party == unnamed_others_code:
                    continue
                trend_filename = ('./Outputs/fp_trend_' 
                    + str(election.year()) 
                    + election.region()
                    + '_' + party + '.csv')
                if config.show_loaded_files:
                    print(trend_filename)
                data = import_trend_file(trend_filename)
                self._data[ElectionPartyCode(election, party)] = data
            self._data[ElectionPartyCode(election, unnamed_others_code)] = \
                self.create_exclusive_others_series(election, party_list)
    
    def value_at(self, party_code, day, percentile, default_value = None):
        if day >= len(self._data[party_code]) or day < 0:
            return default_value
        return self._data[party_code][day][percentile]

    # Create exclusive others raw series
    def create_exclusive_others_series(self, election, party_list):
        series = []
        unnamed_others_base = 3  # Base of 3% for unnamed others mirrors the C++
        for day in range(0, len(self._data[
                ElectionPartyCode(election, party_list[0])])):
            median = 0  # Median values for minor parties
            for party in party_list:
                if party not in not_others:
                    median += self.value_at(ElectionPartyCode(election, party), day, 50)
            oth_code = ElectionPartyCode(election, 'OTH FP')
            oth_median = self.value_at(oth_code, day, 50)
            modified_oth_median = max(oth_median, median + unnamed_others_base)
            xoth_proportion = 1 - median / modified_oth_median
            spread = []
            for value in range(0, 101):
                spread.append(self.value_at(oth_code, day, value) * xoth_proportion)
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

def import_trend_file(filename):
    debug = (filename == './Outputs/fp_trend_2019fed_ALP FP.csv')
    with open(filename, 'r') as f:
        lines = f.readlines()
    lines = [line.strip().split(',')[2:] for line in lines[3:]]
    lines = [[float(a) for a in line] for line in lines]
    lines.reverse()
    return lines

def print_coeffs(coeffs):
    for party_group, coeff_values in coeffs.items():
        print(f'Model coefficients for party group: {party_group}')
        print(f' Poll trend: {coeff_values[0]}')
        print(f' Same party federally: {coeff_values[1]}')
        print(f' Opposite party federally: {coeff_values[2]}')
        print(f' 6-election average: {coeff_values[3]}')
        print(f' Incumbency: {coeff_values[4]}')
        print(f' Federal election: {coeff_values[5]}')
        print(f' Government length: {coeff_values[6]}')
        print(f' Opposition length: {coeff_values[7]}')

def transform_vote_share(vote_share):
    vote_share = clamp(vote_share, 0.1, 99.9)
    return math.log((vote_share * 0.01) / (1 - vote_share * 0.01)) * 25

def detransform_vote_share(vote_share):
    return 100 / (1 + math.exp(-0.04 * vote_share))

def clamp(n, min_n, max_n):
    return max(min(max_n, n), min_n)


# force_monotone: will look at the endpoints 
# to determine direction of monotonicity
def print_smoothed_series(label, some_dict, file, force_monotone=False, bounds=[-math.inf, math.inf]):
    x_orig, y = zip(*some_dict.items())
    x = range(0, len(x_orig))
    total_days = x_orig[len(x_orig) - 1]
    w = [10 if a == 0 else 1 for a in x]
    spline = UnivariateSpline(x=x, y=y, w=w, s=1000)
    full_spline = spline(x)
    full_spline = {x_orig[a]: b for a, b in enumerate(full_spline)}
    print(f'{label} smoothed: ' + '\n'.join([f'{a}: {b:.4f}' for a, b in full_spline.items()]) + '\n')
    daily_x = [.5 * (math.sqrt(8 * n + 1) - 1) 
            for n in range(0, total_days + 1)]
    daily_spline = list(spline(daily_x))
    if force_monotone:
        if daily_spline[len(daily_spline) - 1] > daily_spline[0]:
            for day in range(0, len(daily_spline) - 1):
                daily_spline[day + 1] = max(daily_spline[day + 1], daily_spline[day]) 
        else:
            for day in range(0, len(daily_spline) - 1):
                daily_spline[day + 1] = min(daily_spline[day + 1], daily_spline[day])
    for day in range(0, len(daily_spline) - 1):
        daily_spline[day + 1] = clamp(daily_spline[day + 1], bounds[0], bounds[1])
    file.write(','.join([f'{a:.4f}' for a in daily_spline]) + '\n')


def smoothed_median(container, smoothing):
    s = sorted(container)
    n = len(s)
    high_mid = math.floor(n / 2)
    low_mid = high_mid - 1 if n % 2 == 0 else high_mid
    high_end = min(high_mid + smoothing + 1, n)
    low_end = max(low_mid - smoothing, 0)
    return statistics.mean(s[low_end:high_end])


# Note: this assessment assumes the mean is 0 (as the
# calculation is being made for one tail of a distribution,
# the mean is not actually being calculated)
def sample_kurtosis(sample):
    numerator = sum([a ** 4 for a in sample])
    n = max(4, len(sample))
    denominator = sum([a ** 2 for a in sample]) ** 2
    sample_size_corrected = (n * (n + 1) * (n - 1)) / ((n - 2) * (n - 3))
    kurtosis_estimate = numerator * sample_size_corrected / denominator
    return kurtosis_estimate


def test_procedure(config, inputs, poll_trend, exclude):
    for party_group in party_groups.keys():
        print(f'*** DETERMINING TREND ADJUSTMENTS FOR PARTY GROUP {party_group} ***')
        avg_n = 6 if party_group == "ALP" or party_group == "LNP" else 1
        print(f'*** USING AVERAGE OF PREVIOUS {avg_n} ELECTIONS ***')
        days = config.days
        poll_biases = {}
        previous_biases = {}
        biases = {}
        deviations = {}
        lower_rmses = {}
        upper_rmses = {}
        lower_kurtoses = {}
        upper_kurtoses = {}
        final_mix_factors = {}
        for day in days:
            if day == 0:
                previous_debiased_errors = []
            poll_debiased_errors = []
            overall_poll_biases = []
            overall_previous_biases = []
            mix_limits = (0, 1)
            while mix_limits[1] - mix_limits[0] > 0.0001:
                mixed_errors = [[],[]]
                for studied_election in inputs.studied_elections:
                    previous_errors = []
                    poll_errors = []
                    studied_previous_error = None
                    studied_poll = None
                    studied_poll_errors = []
                    studied_poll_party = []
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
                                    studied_previous_error = previous_error
                                else:
                                    previous_errors.append(previous_error)

                                if polls is not None:
                                    poll_error = transform_vote_share(polls) - result_t
                                    if other_election == studied_election:
                                        studied_poll_errors.append(poll_error)
                                        studied_poll_party.append(party)
                                    else:
                                        poll_errors.append(poll_error)
                    previous_bias = statistics.median(previous_errors)
                    poll_bias = statistics.median(poll_errors)
                    overall_previous_biases.append(previous_bias)
                    overall_poll_biases.append(poll_bias)
                    if studied_election == no_target_election_marker:
                        continue
                    if studied_previous_error is not None:
                        previous_debiased_error = studied_previous_error - previous_bias
                        if mix_limits == (0, 1):
                            previous_debiased_errors.append(previous_debiased_error)
                    if len(studied_poll_errors) > 0:
                        for studied_poll_error, studied_poll_party in zip(studied_poll_errors, studied_poll_party):
                            poll_debiased_error = studied_poll_error - poll_bias
                            if mix_limits == (0, 1):
                                poll_debiased_errors.append(poll_debiased_error)
                            party_code = ElectionPartyCode(studied_election, studied_poll_party)
                            previous = inputs.avg_prior_results[avg_n][party_code]
                            debiased_previous = transform_vote_share(previous) - previous_bias
                            polls = poll_trend.value_at(party_code, day, 50)
                            debiased_polls = transform_vote_share(polls) - poll_bias
                            result = (max(0.5, inputs.eventual_results[party_code])
                                if party_code in inputs.eventual_results else 0.5)
                            result_t = transform_vote_share(result)
                            for mix_index, mix_factor in enumerate(mix_limits):
                                mixed = debiased_polls * mix_factor + debiased_previous * (1 - mix_factor)
                                mixed_error = mixed - result_t
                                mixed_errors[mix_index].append(mixed_error)
                rmse_factor = 0.3
                mixed_criteria = [0, 0]
                for mix_index in range(0, len(mix_limits)):
                    mixed_deviation = smoothed_median([abs(a) for a in mixed_errors[mix_index]], 2)
                    mixed_rmse = math.sqrt(sum([a ** 2 for a in mixed_errors[mix_index]]) / len(mixed_errors[mix_index]))
                    mixed_criteria[mix_index] = mixed_rmse * rmse_factor + mixed_deviation * (1 - rmse_factor)
                window_factor = 0.8  # should be in range [0.5, 1)
                if mixed_criteria[0] < mixed_criteria[1]:
                    mix_limits = (mix_limits[0], mix_limits[0] * (1 - window_factor) + mix_limits[1] * window_factor)
                else:
                    mix_limits = (mix_limits[1] * (1 - window_factor) + mix_limits[0] * window_factor, mix_limits[1])
            final_mix_factor = statistics.mean(mix_limits)
            if day == 0:
                previous_bias = smoothed_median(previous_debiased_errors, 2)
            poll_bias = smoothed_median(overall_poll_biases, 2)
            previous_bias = smoothed_median(overall_previous_biases, 2)
            mixed_bias = smoothed_median(mixed_errors[1], 2)
            mixed_deviation = smoothed_median([abs(a) for a in mixed_errors[1]], 2)
            lower_errors = [a - mixed_bias for a in mixed_errors[1] if a < mixed_bias]
            upper_errors = [a - mixed_bias for a in mixed_errors[1] if a > mixed_bias]
            lower_rmse = math.sqrt(sum([a ** 2 for a in lower_errors]) / (len(lower_errors) - 1))
            upper_rmse = math.sqrt(sum([a ** 2 for a in upper_errors]) / (len(upper_errors) - 1))
            lower_kurtosis = sample_kurtosis(lower_errors) 
            upper_kurtosis = sample_kurtosis(upper_errors) 
            poll_biases[day] = poll_bias
            previous_biases[day] = previous_bias
            biases[day] = mixed_bias
            deviations[day] = mixed_deviation
            lower_rmses[day] = lower_rmse
            upper_rmses[day] = upper_rmse
            lower_kurtoses[day] = lower_kurtosis
            upper_kurtoses[day] = upper_kurtosis
            final_mix_factors[day] = final_mix_factor
        # These parameters should always be monotonic - if they aren't,
        # it's likely a case of underestimated variation, so make them so
        for index in range(0, len(days) - 1):
            next_day = days[index + 1]
            previous_day = days[index]
            final_mix_factors[next_day] = min(final_mix_factors[next_day], final_mix_factors[previous_day])
            lower_rmses[next_day] = max(lower_rmses[next_day], lower_rmses[previous_day]) 
            upper_rmses[next_day] = max(upper_rmses[next_day], upper_rmses[previous_day]) 
        filename = f'./Adjustments/adjust_{exclude.year()}{exclude.region()}_{party_group}.csv'
        with open(filename, 'w') as f:
            print_smoothed_series('Poll Bias', poll_biases, f)
            print_smoothed_series('Previous-Elections Bias', previous_biases, f)
            print_smoothed_series('Mixed Bias', biases, f)
            print_smoothed_series('Lower Error', lower_rmses, f, force_monotone=True, bounds=(0, math.inf))
            print_smoothed_series('Upper Error', upper_rmses, f, force_monotone=True, bounds=(0, math.inf))
            print_smoothed_series('Lower Kurtosis', lower_kurtoses, f, bounds=(3, math.inf))
            print_smoothed_series('Upper Kurtosis', upper_kurtoses, f, bounds=(3, math.inf))
            print_smoothed_series('Mix factor', final_mix_factors, f, force_monotone=True, bounds=(0, 1))


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
        poll_trend = PollTrend(inputs, config)
        outputs = Outputs()

        # Leave this until now so it doesn't interfere with initialization
        # of poll_trend
        inputs.determine_eventual_others_results()

        test_procedure(config, inputs, poll_trend, exclude)
        print(f'Completed trend adjustment algorithm for: {exclude}')

if __name__ == '__main__':
    trend_adjust()
