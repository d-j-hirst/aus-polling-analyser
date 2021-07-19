from sklearn import datasets, linear_model
from scipy.interpolate import UnivariateSpline
import math
import argparse

unnamed_others_code = 'xOTH FP'
poll_score_threshold = 3

# To keep analysis simple, and maintain decent sample sizes, group
# polled parties into categories with similar expected behaviour.
party_groups = {
    'ALP' : ['ALP FP'],
    'LNP' : ['LNP FP', 'LIB FP'],
    # Minor "constituency" parties that represent a particular group/viewpoint
    # Fairly stable, slow moving vote shares
    'Misc-c' : ['GRN FP', 'NAT FP', 'FF FP'],
    # Minor "protest" parties that are primarily anti-political,
    # defining themselves as being against the current system
    # Volatile, rapidly changing vote shares
    'Misc-p' : ['UAP FP', 'ONP FP', 'SAB FP', 'SFF FP', 'DEM FP', 'KAP FP'],
    # General others: Anything not ALP/LIB/NAT/GRN (or their equivalents)
    'OTH' : ['OTH FP'],
    # Unnamed others: Anything not listed at all
    'xOTH' : [unnamed_others_code]
}

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
        day_test_count = 41
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
        avg_n = 6
        self.avg_prior_results = {k: sum(v[:avg_n]) / avg_n
            for k, v in self.prior_results.items()}
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

def prepare_individual_info(inputs, election, party_code, day, poll_trend_now,
                            party, party_group):
    poll_trend_now = transform_vote_share(poll_trend_now)
    incumbent = (inputs.incumbency[election][0] == party)
    incumbent = 1 if incumbent else 0
    opposition = (inputs.incumbency[election][1] == party)
    government_length = inputs.incumbency[election][2]
    federal = 1 if (party_code.region() == 'fed') else 0
    opposite_federal = 0 if election not in inputs.federal_situation \
        else (1 if party == inputs.federal_situation[election][1] else 0)
    same_federal = 0 if election not in inputs.federal_situation \
        else (1 if party == inputs.federal_situation[election][0] else 0)
    # previous election average only predictive for xOTH
    previous = transform_vote_share(inputs.avg_prior_results[party_code])
    previous = previous if party_group == 'xOTH' else 0
    # some factors are only predictive in some circumstances
    if party_group in ['OTH', 'Misc-p']:
        federal = 0
    if party_group in ['ALP', 'LNP', 'Misc-c'] and day > 70:
        federal = 0
    in_power_length = government_length if incumbent else 0
    opposition_length = government_length if opposition else 0
    if party_group != 'LNP':
        incumbent = 0
    # these factors aren't currently predictive, but
    # are left in the code in case they can be incorporated
    # in the future
    in_power_length = 0
    opposition_length = 0

    # note: commented out lines were for factors that
    # didn't improve predictiveness
    # leaving them here in case something changes that
    return [
        poll_trend_now,
        same_federal,
        opposite_federal,
        previous,
        incumbent,
        federal,
        in_power_length,
        opposition_length,
    ]

def determine_regression_inputs(day, studied_election, inputs,
                               config, poll_trend, outputs):
    complete_info = {}
    info = {}
    transformed_results = {}
    for election in inputs.elections:
        for party in inputs.parties[election]:
            party_group = ''
            for group, group_list in party_groups.items():
                if party in group_list:
                    party_group = group
                    break
            if party_group == '':
                print(f'Warning: {party} not categorised!')
                continue
            if party_group not in info:
                info[party_group] = []
            if party_group not in transformed_results:
                transformed_results[party_group] = []

            if party_group not in outputs.sum_squared_errors:
                outputs.sum_squared_errors[party_group] = {}
                outputs.error_count[party_group] = {}
            if day not in outputs.sum_squared_errors[party_group]:
                outputs.sum_squared_errors[party_group][day] = [0, 0]
                outputs.error_count[party_group][day] = [0, 0]

            party_code = ElectionPartyCode(election, party)
            if not party_code in inputs.prior_results:
                inputs.prior_results[party_code] = [0]
                inputs.avg_prior_results[party_code] = 0
                print(f'Info: prior result not found for: {party_code}')
            if not party_code in inputs.eventual_results:
                inputs.eventual_results[party_code] = 0
                print(f'Info: eventual result not found for: {party_code}')
            # should actually randomply sample poll results from distribution
            poll_trend_now = poll_trend.value_at(
                        party_code=party_code, 
                        day=day, 
                        percentile=50,
                        default_value=inputs.prior_results[party_code])
            # Very small poll trends cause distortions in the
            # Misc-p results, so best to ignore them and
            # only use parties polling a substantial % of vote
            if poll_trend_now < poll_score_threshold:
                continue
            this_info = prepare_individual_info(
                inputs, election, party_code, day,
                poll_trend_now, party, party_group)
            complete_info[party_code] = this_info + [1]
            # need to store the info so accuracy can be evaluated later
            # but we don't want anything from the studied election
            # being used to determine the forecast for it, so
            # exit out here
            if election == studied_election:
                continue
            info[party_group].append(this_info)
            transformed_result = \
                transform_vote_share(inputs.eventual_results[party_code])
            transformed_results[party_group].append(transformed_result)
    return RegressionInputs(complete_info, info, transformed_results)


def run_regression(reg_inputs):
    coeffs = {}
    for party_group in reg_inputs.info.keys():
        regr = linear_model.LinearRegression(fit_intercept=True)
        regr.fit(reg_inputs.info[party_group], reg_inputs.transformed_results[party_group])
        this_coeffs = [round(x, 3) for x in regr.coef_]
        coeffs[party_group] = this_coeffs
        coeffs[party_group] += [regr.intercept_]
        reg_inputs.info[party_group] += [1]
        print(coeffs[party_group])
    return coeffs


def record_outputs(config, outputs, inputs, studied_election, day, coeffs, reg_inputs):
    if studied_election == no_target_election_marker \
            and day == config.feedback_day and config.show_parameters:
        print_coeffs(coeffs)
    if studied_election == no_target_election_marker:
        for party_group in reg_inputs.info.keys():
            if studied_election not in outputs.raw_params:
                outputs.raw_params[studied_election] = {}
            if party_group not in outputs.raw_params[studied_election]:
                outputs.raw_params[studied_election][party_group] = []
                for i in range(0, len(coeffs[party_group])):
                    outputs.raw_params[studied_election][party_group].append({})
            for index, coeff_value in enumerate(coeffs[party_group]):
                outputs.raw_params[studied_election][party_group][index][day] = coeff_value
    elif studied_election != no_target_election_marker:
        for party in inputs.parties[studied_election]:
            party_group = ''
            for group, group_list in party_groups.items():
                if party in group_list:
                    party_group = group
                    break
            if party_group == '':
                continue
            party_code = ElectionPartyCode(studied_election, party)
            # Some parties have poll trend scores too low to usefully
            # be used for trend adjustment, so they are excluded from
            # the algorithm. Since their data is not stored, it would
            # cause an error for them to be included here, so skip them
            if party_code not in reg_inputs.complete_info:
                continue
            print(coeffs[party_group])
            print(reg_inputs.complete_info[party_code])
            zipped = zip(reg_inputs.complete_info[party_code], 
                        coeffs[party_group])
            estimated = sum([a[0] * a[1] for a in zipped])
            if day == config.feedback_day:
                outputs.estimations[party_code] = estimated
            detransformed = detransform_vote_share(estimated)
            transformed_eventual = transform_vote_share(inputs.eventual_results[party_code])
            error_dir = 1 if transformed_eventual > estimated else 0
            outputs.sum_squared_errors[party_group][day][error_dir] += \
                (estimated - transformed_eventual) ** 2
            outputs.error_count[party_group][day][error_dir] += 1
    

def determine_trend_adjustments(inputs, config, poll_trend, outputs):
    for studied_election in inputs.studied_elections:
        for day in config.days:
            reg_inputs = determine_regression_inputs(day, studied_election,
                                                    inputs, config,
                                                    poll_trend, outputs)
            coeffs = run_regression(reg_inputs)
            record_outputs(config, outputs, inputs, studied_election,
                           day, coeffs, reg_inputs)


def record_errors_by_day(config, outputs):
    for party_group, days in outputs.sum_squared_errors.items():
        if config.show_errors_by_day:
            print(f' Errors for party group: {party_group}')
        for day in days.keys():
            for side in [0, 1]:
                rmse = math.sqrt(outputs.sum_squared_errors[party_group][day][side] \
                        / outputs.error_count[party_group][day][side])
                if party_group not in outputs.rmse:
                    outputs.rmse[party_group] = [{}, {}]
                outputs.rmse[party_group][side][day] = rmse
                if config.show_errors_by_day:
                    print(f' {("Lower" if side == 0 else "Upper")}'
                        f' RMSE for day {day} forecast: {rmse}')

def show_previous_election_predictions(inputs, config, poll_trend, outputs):
    for studied_election in inputs.studied_elections:
        if studied_election == no_target_election_marker:
            continue
        for party in inputs.parties[studied_election]:
            party_group = ''
            for group, group_list in party_groups.items():
                if party in group_list:
                    party_group = group
                    break
            if party_group == '':
                continue
            party_code = ElectionPartyCode(studied_election, party)
            print(party_code)
            # Some parties have poll trend scores too low to usefully
            # be used for trend adjustment, so they are excluded from
            # the algorithm. Since their data is not stored, it would
            # cause an error for them to be included here, so skip them
            if party_code not in outputs.estimations:
                print(f'  Poll score too low for trend adjustment')
                continue
            estimated = outputs.estimations[party_code]
            detransformed = detransform_vote_share(estimated)
            poll_trend_now = poll_trend.value_at(party_code, config.feedback_day, 50)
            upper_rmse = math.sqrt(outputs.sum_squared_errors[party_group][config.feedback_day][1] \
                    / outputs.error_count[party_group][config.feedback_day][1])
            lower_rmse = math.sqrt(outputs.sum_squared_errors[party_group][config.feedback_day][0] \
                    / outputs.error_count[party_group][config.feedback_day][0])
            estimation_low = estimated - 2 * upper_rmse
            estimation_high = estimated + 2 * lower_rmse
            estimated_low = detransform_vote_share(estimation_low)
            estimated_high = detransform_vote_share(estimation_high)
            print(f'  Feedback day: {config.feedback_day}')
            print(f'  Prior result: {inputs.prior_results[party_code][0]}')
            print(f'  Prior average: {inputs.avg_prior_results[party_code]}')
            print(f'  Poll trend: {poll_trend_now}')
            print(f'  Estimated result: {detransformed}')
            print(f'  95% range: {estimated_low}-{estimated_high}')
            print(f'  Eventual result: {inputs.eventual_results[party_code]}')


def calculate_parameter_curves(config, exclude, outputs):
    election_params = outputs.raw_params[no_target_election_marker]
    for party_group, party_params in election_params.items():
        filename = f'./Adjustments/adjust_{exclude.year()}{exclude.region()}_{party_group}.csv'
        with open(filename, 'w') as f:
            for coeff_index, coeffs in enumerate(party_params):
                x, y = zip(*coeffs.items())
                total_days = x[len(x) - 1]
                x = range(0, len(x))
                w = [10 if a == 0 else 1 for a in x]
                spline = UnivariateSpline(x=x, y=y, w=w, s=3)
                days_to_study = [.5 * (math.sqrt(8 * n + 1) - 1) 
                    for n in range(0, total_days + 1)]
                full_spline = spline(days_to_study)
                f.write(','.join([f'{a:.4f}' for a in full_spline]) + '\n')
            if config.show_written_files:
                print(f'Wrote trend adjustment details to: {filename}')
    for party_group, party_errors in outputs.rmse.items():
        filename = f'./Adjustments/errors_{exclude.year()}{exclude.region()}_{party_group}.csv'
        with open(filename, 'w') as f:
            for side in (0, 1):
                day_errors = party_errors[side]
                x, y = zip(*day_errors.items())
                total_days = x[len(x) - 1]
                x = range(0, len(x))
                w = [10 if a == 0 else 1 for a in x]
                spline = UnivariateSpline(x=x, y=y, w=w, s=3)
                days_to_study = [.5 * (math.sqrt(8 * n + 1) - 1) 
                    for n in range(0, total_days + 1)]
                full_spline = spline(days_to_study)
                f.write(','.join([f'{a:.4f}' for a in full_spline]) + '\n')
            if config.show_written_files:
                print(f'Wrote trend error details to: {filename}')


def trend_adjust():
    try:
        config = Config()
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return

    for exclude in config.elections:
        inputs = Inputs(exclude)
        poll_trend = PollTrend(inputs, config)
        outputs = Outputs()

        # Leave this until now so it doesn't interfere with initialization
        # of poll_trend
        inputs.determine_eventual_others_results()

        determine_trend_adjustments(inputs, config, poll_trend, outputs)

        record_errors_by_day(config, outputs)

        if config.show_previous_elections:
            show_previous_election_predictions(inputs, config, poll_trend, outputs)

        calculate_parameter_curves(config, exclude, outputs)

if __name__ == '__main__':
    trend_adjust()
