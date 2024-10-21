from election_code import ElectionCode, no_target_election_marker
from poll_transform import transform_vote_share, detransform_vote_share, clamp
from sample_kurtosis import one_tail_kurtosis

from time import perf_counter

from scipy.interpolate import UnivariateSpline
from sklearn.linear_model import ElasticNetCV
from numpy import array, transpose, dot, average, amax, amin, median, random

from stan_cache import stan_cache

import argparse
import copy
import math
import os
import statistics
import sys

import pystan

poll_score_threshold = 3

backtest_limit = 1990 # Only backtest from this year onwards
                       # Rationale: a complete set of state results
                       # is available from this year onwards

rng = random.default_rng()

# To keep analysis simple, and maintain decent sample sizes, group
# polled parties into categories with similar expected behaviour.
with open('./Data/party-groups.csv', 'r') as f:
    party_groups = {
        b[0]: b[1:] for b in
        [a.strip().split(',') for a in f.readlines()]}

average_length = {a: 6 if a == "ALP" or a == "LNP" or a == "TPP" else 1
                  for a in party_groups.keys()}

unnamed_others_code = party_groups['xOTH'][0]


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
    
    def election(self):
        return ElectionCode(self.year(), self.region())

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
        parser.add_argument('--check', action='store', type=str,
                            help='Compares accuracy of projection types. Enter'
                            ' "no" (default) for no checking, yes for '
                            'checking after doing adjustment calculations, '
                            'and "only" to do only checks and skip '
                            'calculations (of course calculations will still '
                            'need to have been done at some point before). '
                            'Note the check will only include elections '
                            'included under the --election argument, if '
                            'you get a StatisticsError, try setting '
                            '"--election none" as well.'
                            , default='no')
        parser.add_argument('--checkday', action='store', type=int,
                            help='Number of days out to check poll data.'
                            ' Not used if --check is absent or set to "no".'
                            , default=300)
        parser.add_argument('--checkregion', action='store', type=str,
                            help='Filter for a region to check for'
                            '(e.g. "fed", "nsw" or "sa").'
                            'Use "nofed" to exclude federal election only.'
                            , default='')
        parser.add_argument('-w', '--writtenfiles', action='store_true',
                            help='Show written files')
        parser.add_argument('-u', '--fundamentals', action='store_true',
                            help='Show regression results for fundamentals '
                            'forecasts')
        self.show_loaded_files = parser.parse_args().files
        self.show_parameters = parser.parse_args().parameters
        self.show_written_files = parser.parse_args().writtenfiles
        self.show_fundamentals = parser.parse_args().fundamentals
        self.check = parser.parse_args().check
        self.check_day = parser.parse_args().checkday
        self.check_region = parser.parse_args().checkregion
        self.election_instructions = parser.parse_args().election.lower()
        self.prepare_election_list()
        day_test_count = 46
        self.days = [int((n * (n + 1)) / 2) for n in range(0, day_test_count)]

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


class Inputs:
    def __init__(self, exclude):
        # Only elections with usable polling data
        # [0] year of election, [1] region of election
        with open('./Data/polled-elections.csv', 'r') as f:
            self.polled_elections = ElectionCode.load_elections_from_file(f, exclude=exclude)
        # Old elections without enough polling data, but still useful
        # for determining fundamentals forecasts
        # [0] year of election, [1] region of election
        with open('./Data/old-elections.csv', 'r') as f:
            old_elections = ElectionCode.load_elections_from_file(f, exclude=exclude)
        old_elections = [a for a in old_elections if a != exclude]
        self.past_elections = self.polled_elections + old_elections
        # We need data for the current election
        self.all_elections = self.past_elections + [exclude]
        # key: [0] year of election, [1] region of election
        # value: list of significant party codes modelled in that election
        with open('./Data/significant-parties.csv', 'r') as f:
            parties = {
                ElectionCode(a[0], a[1]): a[2:]
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.all_elections
            }
        # key: [0] year of election, [1] region of election, [2] party code
        # value: primary vote recorded in this election
        # avoid storing eventual results for current election since that shouldn't
        # be used for predicting it
        with open('./Data/eventual-results.csv', 'r') as f:
            self.eventual_results = {
                ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]): float(a[3])
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.past_elections
            }
        # key: [0] year of election, [1] region of election, [2] party code
        # value: primary vote recorded in the previous election
        with open('./Data/prior-results.csv', 'r') as f:
            linelists = [b.strip().split(',') for b in f.readlines()]
            self.prior_results = {
                ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]):
                [float(x) for x in a[3:]]
                for a in linelists
                if ElectionCode(a[0], a[1]) in self.all_elections
            }

        # stores: first incumbent party, then main opposition party,
        # finally years incumbent party has been in power
        with open('./Data/incumbency.csv', 'r') as f:
            self.incumbency = {
                ElectionCode(a[0], a[1]): (a[2], a[3], float(a[4]))
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.all_elections
            }
        # stores: party corresponding to federal government,
        # then party opposing federal government,
        # then the chance the federal government is still in power at this election,
        # given in the file as a percentage (defaults to 100 if not given)
        with open('./Data/federal-situation.csv', 'r') as f:
            self.federal_situation = {
                ElectionCode(a[0], a[1]):
                (a[2], a[3], float(a[4]) / 100 if len(a) >= 5 else 1)
                for a in [b.strip().split(',') for b in f.readlines()]
                if ElectionCode(a[0], a[1]) in self.all_elections
            }

        # Trim party list so that we only store it for completed elections
        self.polled_parties = {e: parties[e] for e in self.polled_elections}
        self.past_parties = {e: parties[e] for e in self.past_elections}
        self.all_parties = {e: parties[e] for e in self.all_elections}
        # Create averages of prior results
        avg_counts = list(range(1, 9))
        self.avg_prior_results = {
            avg_n: {
                k: median(v[:avg_n])
                if avg_n < 5  else sum(sorted(v[:avg_n])[1:-1]) / (avg_n - 2)
                for k, v in self.prior_results.items()
            } for avg_n in avg_counts}
        self.studied_elections = self.polled_elections + [no_target_election_marker]
        self.fundamentals = {}  # Filled in later
        self.exclude = exclude
    
    def safe_prior_average(self, n_elections, e_p_c):
        if n_elections not in self.avg_prior_results:
            n_elections = 1
        if e_p_c in self.avg_prior_results[n_elections]:
            return self.avg_prior_results[n_elections][e_p_c]
        else:
            return 0
        

    def determine_eventual_others_results(self):
        for e in self.past_elections:
            others_code = ElectionPartyCode(e, 'OTH FP')
            eventual_others = self.eventual_results[others_code]
            eventual_named = 0
            for p in self.past_parties[e]:
                party_code = ElectionPartyCode(e, p)
                if p not in not_others and party_code in self.eventual_results:
                    eventual_named += self.eventual_results[party_code]
            eventual_unnamed = eventual_others - eventual_named
            unnamed_code = ElectionPartyCode(e, unnamed_others_code)
            self.eventual_results[unnamed_code] = eventual_unnamed
            self.past_parties[e].append(unnamed_others_code)
            if e in self.polled_parties:
                self.polled_parties[e].append(unnamed_others_code)


poll_trend_cache = {}
dir_cache = None

class PollTrend:
    def __init__(self, inputs, config):
        self._data = {}
        self.cutoff_data = {}
        cache_usage = 0
        for election, party_list in inputs.polled_parties.items():
            for party in party_list:
                code = ElectionPartyCode(election, party)
                cutoff_dir = './Outputs/Cutoffs'
                if code in poll_trend_cache:
                    self.cutoff_data[code] = poll_trend_cache[code]
                global dir_cache
                if dir_cache is None:
                    dir_cache = os.listdir(cutoff_dir)
                for filename in dir_cache:
                    if filename.startswith(f'fp_trend_{election.year()}{election.region()}_{party}_') and filename.endswith('d.csv'):
                        num_days = int(filename.split('_')[-1].split('d.csv')[0])
                        if code in self.cutoff_data and num_days in self.cutoff_data[code]:
                            continue
                        if code in poll_trend_cache and num_days in poll_trend_cache[code]:
                            if code not in self.cutoff_data:
                                self.cutoff_data[code] = {}
                            self.cutoff_data[code][num_days] = poll_trend_cache[code][num_days]
                            cache_usage += 1
                            continue
                        cutoff_filename = os.path.join(cutoff_dir, filename)
                        with open(cutoff_filename, 'r') as f:
                            lines = f.readlines()
                            if len(lines) >= 4:
                                elements = lines[3].strip().split(',')
                                if len(elements) >= 52:
                                    extracted_value = float(elements[51])
                                    if code not in self.cutoff_data:
                                        self.cutoff_data[code] = {}
                                    self.cutoff_data[code][num_days] = extracted_value
                                    if code not in poll_trend_cache:
                                        poll_trend_cache[code] = {}
                                    poll_trend_cache[code][num_days] = extracted_value


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


class HyperparamSet:
    def __init__(self, average_error=10, median_error=10, rmse=10):
        self.sigma_prior = 0.5
        self.federal_state_difference_sigma = 1
        self.poll_weight_change_sigma = 0.195
        self.sigma_change_sigma = 0.057
        self.bias_change_sigma = 0.0247
        self.recency_bias_half_life = 2.114
        self.sample_weight = 3

        self.average_error = 10
        self.median_error = 10
        self.rmse = 10
    
    def limit_hyperparam_set(self):
        self.sigma_prior = max(0.5, self.sigma_prior)
        self.sigma_prior = min(10, self.sigma_prior)
        self.federal_state_difference_sigma = max(0.05, self.federal_state_difference_sigma)
        self.federal_state_difference_sigma = min(1, self.federal_state_difference_sigma)
        self.poll_weight_change_sigma = max(0.02, self.poll_weight_change_sigma)
        self.poll_weight_change_sigma = min(0.2, self.poll_weight_change_sigma)
        self.sigma_change_sigma = max(0.02, self.sigma_change_sigma)
        self.sigma_change_sigma = min(0.5, self.sigma_change_sigma)
        self.bias_change_sigma = max(0.01, self.bias_change_sigma)
        self.bias_change_sigma = min(0.5, self.bias_change_sigma)
        self.recency_bias_half_life = max(1, self.recency_bias_half_life)
        self.recency_bias_half_life = min(100, self.recency_bias_half_life)
        self.sample_weight = math.floor(self.sample_weight + 0.5)
        self.sample_weight = max(2, self.sample_weight)
        self.sample_weight = min(20, self.sample_weight)
    
    def energy(self):
        # High sample weight is time-consuming and theoretically questionable,
        # so penalise it unless it really helps the predictions
        return self.average_error + self.median_error + self.rmse + self.sample_weight * 0.01


class Validation:
    def __init__(self):
        self.errors = {}
        self.hyperparam_sets = []
        self.current_hyperparam_set = HyperparamSet()
        self.previous_hyperparam_set = None
        self.temperature = 0.4
    
    def analyse(self):
        for election in self.errors.keys():
            print(f"{election} error: {self.errors[election]}")
            
        average_error = statistics.mean([abs(a) for a in self.errors.values()])
        median_error = statistics.median([abs(a) for a in self.errors.values()])
        rmse = math.sqrt(statistics.mean([abs(a) ** 2 for a in self.errors.values()]))

        if len(self.errors.values()) > 0:
            print(f"Average error: {statistics.mean([abs(a) for a in self.errors.values()])}")
            print(f"Median error: {statistics.median([abs(a) for a in self.errors.values()])}")
            print(f"RMSE: {math.sqrt(statistics.mean([abs(a) ** 2 for a in self.errors.values()]))}")

        self.current_hyperparam_set.average_error = average_error
        self.current_hyperparam_set.median_error = median_error
        self.current_hyperparam_set.rmse = rmse

        current_energy = self.current_hyperparam_set.energy()
        previous_energy = self.previous_hyperparam_set.energy() if self.previous_hyperparam_set is not None else None

        # Test whether this set should be accepted
        update = False
        if self.previous_hyperparam_set is not None:
            energy_diff = current_energy - previous_energy
            if energy_diff < 0:
                update = True
            else:
                r = rng.uniform(0, 1)
                if r < math.exp(-energy_diff / self.temperature):
                    update = True
                else:
                    update = False
        else:
            update = True
        
        if update == False:
            print(f'Did not accept hyperparam set')
            self.current_hyperparam_set = copy.deepcopy(self.previous_hyperparam_set)
            return

        print(f'Accepted hyperparam set')
        print(f'Current energy: {current_energy}')
        print(f'Previous energy: {previous_energy}')
        self.hyperparam_sets.append(copy.deepcopy(self.current_hyperparam_set))

    def next_hyperparam_set(self):
        self.temperature *= 0.95
        self.previous_hyperparam_set = copy.deepcopy(self.current_hyperparam_set)
        self.current_hyperparam_set.sigma_prior *= rng.normal(1, 0.5);
        self.current_hyperparam_set.federal_state_difference_sigma *= rng.normal(1, 0.5);
        self.current_hyperparam_set.poll_weight_change_sigma *= rng.normal(1, 0.5);
        self.current_hyperparam_set.sigma_change_sigma *= rng.normal(1, 0.5);
        self.current_hyperparam_set.bias_change_sigma *= rng.normal(1, 0.5);
        self.current_hyperparam_set.recency_bias_half_life *= rng.normal(1, 0.5);
        self.current_hyperparam_set.sample_weight *= rng.normal(1, 0.5);
        self.current_hyperparam_set.limit_hyperparam_set()
        print(f'Next hyperparam set:')
        print(f'Sigma prior: {self.current_hyperparam_set.sigma_prior}')
        print(f'Federal state difference sigma: {self.current_hyperparam_set.federal_state_difference_sigma}')
        print(f'Poll weight change sigma: {self.current_hyperparam_set.poll_weight_change_sigma}')
        print(f'Sigma change sigma: {self.current_hyperparam_set.sigma_change_sigma}')
        print(f'Bias change sigma: {self.current_hyperparam_set.bias_change_sigma}')
        print(f'Recency bias half life: {self.current_hyperparam_set.recency_bias_half_life}')
        print(f'Sample weight: {self.current_hyperparam_set.sample_weight}')


class RegressionInputs:
    def __init__(self, complete_info, info, transformed_results):
        self.complete_info = complete_info
        self.info = info
        self.transformed_results = transformed_results


# Parties that shouldn't be included as part of the OTH FP
# when calculating exclusive-others vote shares
not_others = ['@TPP', 'ALP FP', 'LNP FP', 'LIB FP', 'NAT FP', 'GRN FP', 'OTH FP']



def create_fundamentals_inputs(inputs, target_election, party, avg_len):
    e_p_c = ElectionPartyCode(target_election, party)
    eventual_results = (inputs.eventual_results[e_p_c]
                        if e_p_c in inputs.eventual_results else 0)
    result_deviation = eventual_results - inputs.safe_prior_average(avg_len, e_p_c)
    effective_party = 'ALP FP' if party == '@TPP' else party
    incumbent = 1 if inputs.incumbency[target_election][0] == effective_party else 0
    opposition = 1 if inputs.incumbency[target_election][1] == effective_party else 0
    incumbency_length = (inputs.incumbency[target_election][2]
                        if incumbent else 0)
    opposition_length = (inputs.incumbency[target_election][2]
                        if opposition else 0)
    federal = 1 if target_election.region() == 'fed' else 0
    if federal:
        federal_same = 0
        federal_opposite = 0
    else:
        federal_same = (inputs.federal_situation[target_election][2]
                        if inputs.federal_situation[target_election][0] == effective_party
                        else 1 - inputs.federal_situation[target_election][2])
        federal_opposite = (inputs.federal_situation[target_election][2]
                        if inputs.federal_situation[target_election][1] == effective_party
                        else 1 - inputs.federal_situation[target_election][2])
    return array([incumbent,
                  opposition,
                  incumbency_length,
                  opposition_length,
                  federal_same,
                  federal_opposite
                  ])


def save_fundamentals(results):
    for election, election_data in results.items():
        filename = (f'./Fundamentals/fundamentals_{election.year()}'
                    f'{election.region()}.csv')
        with open(filename, 'w') as f:
            for party, prediction in election_data.items():
                 f.write(f'{party},{prediction}\n')


def run_fundamentals_regression(config, inputs, excluded_election):
    to_file = {}
    for party_group_code, party_group_list in party_groups.items():
        previous_errors = []
        prediction_errors = []
        baseline_errors = []
        avg_len = average_length[party_group_code]
        for studied_election in inputs.past_elections + [excluded_election]:
            if studied_election.year() < backtest_limit:
                continue # Only backtest towards data from 1990 onwards
            result_deviations = []
            incumbents = []
            oppositions = []
            incumbency_lengths = []
            opposition_lengths = []
            federal_sames = []
            federal_opposites = []
            for election in inputs.past_elections:
                if election == studied_election:
                    break # Only test on data before the election itself
                
                # Make sure that federal elections are only used to predict
                # federal elections, and state elections are only used to
                # predict state elections
                # This gives the best results in validation
                if (election.region() == 'fed'
                    and studied_election.region() != 'fed'):
                    continue
                if (election.region() != 'fed'
                    and studied_election.region() == 'fed'):
                    continue

                for party in inputs.past_parties[election] + [unnamed_others_code]:
                    if party not in party_group_list:
                        continue
                    e_p_c = ElectionPartyCode(election, party)
                    eventual_results = (inputs.eventual_results[e_p_c]
                                        if e_p_c in inputs.eventual_results else 0)
                    result_deviation = eventual_results - inputs.safe_prior_average(avg_len, e_p_c)
                    effective_party = 'ALP FP' if party == '@TPP' else party
                    incumbent = 1 if inputs.incumbency[election][0] == effective_party else 0
                    opposition = 1 if inputs.incumbency[election][1] == effective_party else 0
                    incumbency_length = (inputs.incumbency[election][2]
                                        if incumbent else 0)
                    opposition_length = (inputs.incumbency[election][2]
                                        if opposition else 0)
                    federal = 1 if election.region() == 'fed' else 0
                    federal_same = 1 if not federal and inputs.federal_situation[election][0] == effective_party else 0
                    federal_opposite = 1 if not federal and inputs.federal_situation[election][1] == effective_party else 0

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
            if len(input_array) == 0:
                # No data for this party group, so can't do regression
                # If this is the excluded election, save a dummy file
                # based on the fact that a significant party should be getting
                # at least 3% of the vote to be included in analysis in the
                # first place
                if studied_election not in inputs.past_elections:
                    if studied_election not in to_file:
                        to_file[studied_election] = {}
                    to_file[studied_election][party] = 3
                continue
            if amax(input_array) > 0 or amin(input_array) < 0:
                # reg = QuantileRegressor(alpha=0, quantile=0.5).fit(input_array, dependent_array)
                reg = ElasticNetCV().fit(input_array, dependent_array)
                coefs = reg.coef_
                intercept = reg.intercept_
            else:
                # Simplified procedure when no inputs
                # (which is usually the case for minor parties)
                coefs = [0 for _ in input_array[0]]
                # Add a couple of zeros to the dependent array to make the
                # intercept calculation less sensitive in small sample sizes
                intercept = statistics.mean(result_deviations + [0, 0])
            if config.show_fundamentals:
                # print(f'{input_array}')
                # print(f'{dependent_array}')
                # print(f'Quantile regressor:')
                # print(f'Election/party: {studied_election.short()}, '
                #       f'{party_group_code}\n Coeffs: {coefs}\n '
                #       f'Intercept: {intercept}')
                pass
            # Test with studied election information:
            for party in inputs.all_parties[studied_election] + [unnamed_others_code]:
                if party not in party_group_list:
                    continue
                input_array = create_fundamentals_inputs(inputs,
                                                         studied_election,
                                                         party,
                                                         avg_len)
                e_p_c = ElectionPartyCode(studied_election, party)
                # If the party is TPP, use a 50-50 baseline as that performs better in validation
                # on 2 out of 3 criteria
                prediction = ((inputs.safe_prior_average(avg_len, e_p_c) if party_group_code != "TPP" else 50) +
                            dot(input_array, coefs) + intercept)
                # Fundamentals regression is worse for federal
                # results than a 50-50 baseline, so just use that instead there:
                if (party_group_code == "TPP"
                    and studied_election.region() == "fed"):
                    prediction = 50
                # Fundamentals regression is worse for LNP
                # results than an average of the past results, so just use that instead there:
                # Note that ALP is actually slightly better with fundamentals regression,
                # so we don't override that
                if ((party_group_code == "LNP")
                    and studied_election.region() == "fed"):
                    prediction = inputs.safe_prior_average(avg_len, e_p_c)
                # Fundamentals regression is worse for OTH than the most recent past results,
                # so just use that instead there:
                if (party_group_code == "OTH" or party_group_code == "xOTH"):
                    prediction = inputs.safe_prior_average(avg_len, e_p_c)
                if studied_election in inputs.past_elections:
                    eventual_results = (inputs.eventual_results[e_p_c]
                                        if e_p_c in inputs.eventual_results else 0)
                    previous_errors.append(inputs.safe_prior_average(avg_len, e_p_c)
                                        - eventual_results)
                    baseline_errors.append((50 if party_group_code == "TPP" else 0)
                                        - eventual_results)
                    # if party_group_code == "TPP":
                    #     print(e_p_c)
                    #     print(prediction)
                    #     print(inputs.safe_prior_average(avg_len, e_p_c))
                    #     print(input_array)
                    #     print(coefs)
                    #     print(intercept)
                    # if (party_group_code == "ALP" 
                    #     and studied_election.region() == "fed"):
                    #     prediction = inputs.safe_prior_average(avg_len, e_p_c)
                    # if (party_group_code == "LNP" 
                    #     and studied_election.region() == "fed"):
                    #     prediction = inputs.safe_prior_average(avg_len, e_p_c)
                    prediction_errors.append(prediction - eventual_results)
                    inputs.fundamentals[e_p_c] = prediction
                if studied_election not in inputs.past_elections:
                    # This means it's the excluded election, so want to
                    # save the fundamentals forecast for the main program to use
                    if studied_election not in to_file:
                        to_file[studied_election] = {}
                    to_file[studied_election][party] = prediction

        if config.show_fundamentals:
            if len(previous_errors) == 0:
                print(f'No data for {party_group_code}')
                continue
            print(f'Party group: {party_group_code}')
            print(previous_errors)
            previous_rmse = math.sqrt(sum([a ** 2 for a in previous_errors])
                                    / (len(previous_errors) - 1))
            prediction_rmse = math.sqrt(sum([a ** 2 for a in prediction_errors])
                                    / (len(prediction_errors) - 1))
            baseline_rmse = math.sqrt(sum([a ** 2 for a in baseline_errors])
                                    / (len(previous_errors) - 1))
            print(f'RMSEs: previous {previous_rmse} vs baseline {baseline_rmse} vs prediction {prediction_rmse}')
            previous_average_error = statistics.mean([abs(a) for a in previous_errors])
            prediction_average_error = statistics.mean([abs(a) for a in prediction_errors])
            baseline_average_error = statistics.mean([abs(a) for a in baseline_errors])
            print(f'Average errors: previous {previous_average_error} vs baseline {baseline_average_error} vs prediction {prediction_average_error}')
            previous_median_error = statistics.median([abs(a) for a in previous_errors])
            prediction_median_error = statistics.median([abs(a) for a in prediction_errors])
            baseline_median_error = statistics.median([abs(a) for a in baseline_errors])
            print(f'Median errors: previous {previous_median_error} vs baseline {baseline_median_error} vs prediction {prediction_median_error}')

    if config.show_fundamentals:
        for e_p_c, prediction in inputs.fundamentals.items():
            print(f'{e_p_c} - fundamentals prediction: {prediction}')
            if e_p_c in inputs.eventual_results:
                print(f'{e_p_c} - actual: {inputs.eventual_results[e_p_c]}')
    
    save_fundamentals(to_file)


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
    # print(label)
    # print(some_dict)
    # print(file)
    x_orig, y = zip(*some_dict.items())
    x = range(0, len(x_orig))
    total_days = x_orig[len(x_orig) - 1]
    w = [100 if a == 0 else 1 for a in x]
    spline = UnivariateSpline(x=x, y=y, w=w, s=100)
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


def weighted_median(container, weights):
    new_container = []
    for index, val in enumerate(container):
        for i in range(0, math.floor(weights[index])):
            new_container.append(val)
    return statistics.median(new_container)


class BiasData:
    def __init__(self):
        self.fundamentals_errors = []
        self.poll_errors = []
        self.poll_distance = []
        self.relevance = []
        self.studied_fundamentals_error = None
        self.studied_poll_errors = []
        self.studied_poll_parties = []


def get_bias_data(inputs, poll_trend, party_group,
                  day, studied_election):
    bias_data = BiasData()
    null_year = (max(inputs.polled_elections, key=lambda a: a.year()).year()
                 if inputs.exclude == no_target_election_marker
                 else inputs.exclude.year())
    target_year = (null_year
                   if studied_election == no_target_election_marker
                   else studied_election.year())
    for other_election in inputs.polled_elections:
        for party in party_groups[party_group]:
            if party not in inputs.polled_parties[other_election]:
                continue
            party_code = ElectionPartyCode(other_election, party)
            polls = poll_trend.value_at(party_code, day, 50)
            result = (inputs.eventual_results[party_code]
                      if party_code in inputs.eventual_results else 0.5)
            result_t = transform_vote_share(result)

            fundamentals = inputs.fundamentals[party_code]

            if fundamentals is not None:
                fundamentals_error = transform_vote_share(fundamentals) - result_t
                if other_election == studied_election:
                    bias_data.studied_fundamentals_error = fundamentals_error
                else:
                    bias_data.fundamentals_errors.append(fundamentals_error)

                if polls is not None:
                    poll_error = transform_vote_share(polls) - result_t
                    year_distance = abs(target_year - other_election.year())
                    relevance = (1 if inputs.exclude.region() == "fed"
                        and other_election.region() == "fed" else 0)
                    if other_election == studied_election:
                        bias_data.studied_poll_errors.append(poll_error)
                        bias_data.studied_poll_parties.append(party)
                    else:
                        bias_data.poll_errors.append(poll_error)
                        bias_data.poll_distance.append(year_distance)
                        bias_data.relevance.append(relevance)
    return bias_data


class DayData:
    def __init__(self):
        self.mixed_errors = [[], []]
        self.overall_poll_biases = []
        self.overall_fundamentals_biases = []
        self.final_mix_factor = 0


def get_single_election_data(inputs, poll_trend, party_group, day_data, day,
                             studied_election, mix_limits):
    bias_data = get_bias_data(inputs=inputs,
                              poll_trend=poll_trend,
                              party_group=party_group,
                              day=day,
                              studied_election=studied_election)
    weights = [10 * 2 ** -(val / 4) * (1 + 2 * bias_data.relevance[n])
               for n, val in enumerate(bias_data.poll_distance)]

    fundamentals_bias = average(bias_data.fundamentals_errors)
    poll_bias = average(bias_data.poll_errors, weights=weights)
    if studied_election == no_target_election_marker:
        day_data.overall_fundamentals_biases = [fundamentals_bias]
        day_data.overall_poll_biases = [poll_bias]
    if studied_election == no_target_election_marker:
        return
    if bias_data.studied_fundamentals_error is not None:
        previous_debiased_error = (bias_data.studied_fundamentals_error
                                   - fundamentals_bias)
    if len(bias_data.studied_poll_errors) > 0:
        zipped_bias_data = zip(bias_data.studied_poll_errors,
                               bias_data.studied_poll_parties)
        for studied_poll_error, studied_poll_party in zipped_bias_data:
            poll_debiased_error = studied_poll_error - poll_bias
            party_code = ElectionPartyCode(studied_election,
                                           studied_poll_party)
            fundamentals = inputs.fundamentals[party_code]
            debiased_fundamentals = transform_vote_share(fundamentals) - fundamentals_bias
            polls = poll_trend.value_at(party_code, day, 50)
            debiased_polls = transform_vote_share(polls) - poll_bias
            result = (max(0.5, inputs.eventual_results[party_code])
                      if party_code in inputs.eventual_results else 0.5)
            result_t = transform_vote_share(result)
            for mix_index, mix_factor in enumerate(mix_limits):
                mixed = (debiased_polls * mix_factor
                         + debiased_fundamentals * (1 - mix_factor))
                mixed_error = mixed - result_t
                # Adding the error to the list multiple times effectively
                # increases the weight of the error in the average
                # This is needed in order to down-weight the errors from
                # a very low base that would create unrealistically high
                # variation for higher poll trend levels
                for _ in range(int((min(10, polls) / 10) ** 2 * 20)):
                    day_data.mixed_errors[mix_index].append(mixed_error)


def get_day_data(inputs, poll_trend, party_group, day):
    day_data = DayData()
    mix_limits = (0, 1)
    while mix_limits[1] - mix_limits[0] > 0.0001:
        day_data.mixed_errors = [[], []]
        for studied_election in inputs.studied_elections:
            get_single_election_data(inputs=inputs,
                                     poll_trend=poll_trend,
                                     party_group=party_group,
                                     day=day,
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
            mixed_average_error = statistics.mean(
                [abs(a) for a in day_data.mixed_errors[mix_index]]
            )
            # mixed_criteria[mix_index] = (mixed_rmse * rmse_factor
            #                           + mixed_deviation * (1 - rmse_factor))
            mixed_criteria[mix_index] = mixed_average_error
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
    # if "TPP" in party_group and day == 190:
    #     print("final data:")
    #     print(party_group)
    #     print(day)
    #     print(mix_limits)
    #     print(day_data.overall_poll_biases)
    return day_data


class PartyData:
    def __init__(self):
        self.poll_biases = {}
        self.fundamentals_biases = {}
        self.mixed_biases = {}
        self.deviations = {}
        self.lower_rmses = {}
        self.upper_rmses = {}
        self.lower_kurtoses = {}
        self.upper_kurtoses = {}
        self.final_mix_factors = {}


def get_party_data(config, inputs, poll_trend, party_group):
    party_data = PartyData()
    for day in config.days:
        day_data = get_day_data(inputs=inputs,
                                poll_trend=poll_trend,
                                party_group=party_group,
                                day=day)
        if day == 0:
            fundamentals_bias = smoothed_median(
                day_data.overall_fundamentals_biases, 2)
        poll_bias = smoothed_median(
            day_data.overall_poll_biases, 2)
        # if "TPP" in party_group and day <= 200:
        #     print("overall data for day:")
        #     print(poll_bias)
        #     print(day_data.overall_poll_biases)
        fundamentals_bias = smoothed_median(
            day_data.overall_fundamentals_biases, 2)
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
        party_data.fundamentals_biases[day] = fundamentals_bias
        party_data.mixed_biases[day] = mixed_bias
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
        print_smoothed_series(config, 'Fundamentals Bias',
                              party_data.fundamentals_biases, f)
        print_smoothed_series(config, 'Mixed Bias',
                              party_data.mixed_biases, f)
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
        party_data = get_party_data(config=config,
                                    inputs=inputs,
                                    poll_trend=poll_trend,
                                    party_group=party_group)
        save_party_data(config=config,
                        party_data=party_data,
                        exclude=exclude,
                        party_group=party_group)


def check_poll_predictiveness(config):
    poll_days = [int((n * n + n) / 2) for n in range(0, 45)]
    for poll_day in poll_days:
        baseline_errors = []
        poll_errors = []
        fundamentals_errors = []
        mixed_errors = []
        for election in config.elections:
            if election == no_target_election_marker:
                continue
            if config.check_region == "nofed" and election.region() == "fed":
                continue
            elif (config.check_region != "" and config.check_region != "nofed"
                and election.region() != config.check_region):
                continue
            party_group = "TPP"
            party = "@TPP"
            adjust_filename = (f'./Adjustments/adjust_{election.year()}'
                        f'{election.region()}_{party_group}.csv')
            with open(adjust_filename, 'r') as f:
                poll_bias = float(f.readline().split(',')[poll_day])
                fund_bias = float(f.readline().split(',')[poll_day])
                mixed_bias = float(f.readline().split(',')[poll_day])
                lower_error = float(f.readline().split(',')[poll_day])
                upper_error = float(f.readline().split(',')[poll_day])
                lower_kurtosis = float(f.readline().split(',')[poll_day])
                upper_kurtosis = float(f.readline().split(',')[poll_day])
                mix_factor = float(f.readline().split(',')[poll_day])
            trend_filename = (f'./Outputs/fp_trend_{election.year()}'
                            f'{election.region()}_{party}.csv')
            try:
                trend_data = import_trend_file(trend_filename)
            except FileNotFoundError:
                continue
            # print(f"election: {election}")
            try:
                poll_trend = trend_data[poll_day][50]
            except IndexError:
                continue
            fundamentals_filename = (f'./Fundamentals/fundamentals_{election.year()}'
                        f'{election.region()}.csv')
            with open(fundamentals_filename, 'r') as f:
                fundamentals = next(float(obj.split(',')[1]) for obj in f.readlines()
                                    if obj.split(',')[0] == "@TPP")
            poll_adjusted = poll_trend - poll_bias
            fund_adjusted = fundamentals - fund_bias
            mixed = poll_adjusted * mix_factor + fund_adjusted * (1 - mix_factor) - mixed_bias
            try:
                with open('./Data/eventual-results.csv', 'r') as f:
                    eventual_result = next(float(a.split(",")[3]) for a in f.readlines()
                                        if int(a.split(",")[0]) == election.year()
                                        and a.split(",")[1] == election.region()
                                        and a.split(",")[2] == party)
            except StopIteration:
                continue
            baseline_errors.append(50 - eventual_result)
            poll_errors.append(poll_trend - eventual_result)
            fundamentals_errors.append(fundamentals - eventual_result)
            mixed_errors.append(mixed - eventual_result)
            # print(party_group)
            # print(f"poll_bias: {poll_bias}")
            # print(f"fund_bias: {fund_bias}")
            # print(f"mixed_bias: {mixed_bias}")
            # print(f"lower_error: {lower_error}")
            # print(f"upper_error: {upper_error}")
            # print(f"lower_kurtosis: {lower_kurtosis}")
            # print(f"upper_kurtosis: {upper_kurtosis}")
            # print(f"mix_factor: {mix_factor}")
            # print(f"poll trend: {poll_trend}")
            # print(f"fundamentals: {fundamentals}")
            # print(f"mixed: {mixed}")
            # print(f"eventual_result: {eventual_result}")
        
        try:
            print(f"poll day: {poll_day}")
            print(f"Average baseline error:      {statistics.mean([abs(a) for a in baseline_errors])}")
            print(f"Average poll error:          {statistics.mean([abs(a) for a in poll_errors])}")
            print(f"Average fundamentals error:  {statistics.mean([abs(a) for a in fundamentals_errors])}")
            print(f"Average mixed error:         {statistics.mean([abs(a) for a in mixed_errors])}")
            print(f"Median baseline error:      {statistics.median([abs(a) for a in baseline_errors])}")
            print(f"Median poll error:          {statistics.median([abs(a) for a in poll_errors])}")
            print(f"Median fundamentals error:  {statistics.median([abs(a) for a in fundamentals_errors])}")
            print(f"Median mixed error:         {statistics.median([abs(a) for a in mixed_errors])}")
            print(f"baseline RMSE:      {math.sqrt(statistics.mean([abs(a) ** 2 for a in baseline_errors]))}")
            print(f"poll RMSE:          {math.sqrt(statistics.mean([abs(a) ** 2 for a in poll_errors]))}")
            print(f"fundamentals RMSE:  {math.sqrt(statistics.mean([abs(a) ** 2 for a in fundamentals_errors]))}")
            print(f"mixed RMSE:         {math.sqrt(statistics.mean([abs(a) ** 2 for a in mixed_errors]))}")
        except statistics.StatisticsError:
            print("Could not check statistics as there were no data. Make sure you use --election all so that the program uses all available elections")


def temp_run_stan(config, inputs, poll_trend, validation, party, exclude):
    eventual_results = {}
    for code, result in inputs.eventual_results.items():
        if code.year() < backtest_limit:
            continue
        if code.party() != party:
            continue
        eventual_results[code] = result
        
    fundamentals = {}
    for code, result in inputs.fundamentals.items():
        if code.year() < backtest_limit:
            continue
        fundamentals[code] = result
    
    incumbencies = {}
    for code, result in inputs.incumbency.items():
        if code.year() < backtest_limit:
            continue
        incumbencies[code] = 1 if result[0] == "ALP FP" else 0

    results_list = []
    trends_list = []
    fundamentals_list = []
    incumbencies_list = []
    days_list = []
    federals_list = []
    years_prior_list = []

    for code, cutoffs in poll_trend.cutoff_data.items():
        if code.year() < backtest_limit:
            continue
        if code.party() != party:
            continue
        if not cutoffs:
            continue
        for day in cutoffs.keys():
            result = cutoffs[day]
            results_list.append(eventual_results[code])
            trends_list.append(result)
            fundamentals_list.append(fundamentals[code])
            incumbencies_list.append(incumbencies[code.election()])
            days_list.append(day)
            years_prior_list.append(exclude.year() - code.year())
            federals_list.append(1 if code.region() == "fed" else 0)

    def to_triangular(day):
        return int(0.5 * (math.sqrt(8 * day + 1) - 1) + 0.01)

    def from_triangular(day):
        return int((day * (day + 1)) / 2)

    days_list_t = [to_triangular(day) for day in days_list]
    leftover_time = [int((to_triangular(day) % 1 * 100)) for day in days_list]

    # NSW 2015 has a time point of 54, so we need to have the series be
    # at least 54 when targeting that election
    time_points = max(max(days_list_t) + 1, 54)
    
    stan_data = {
        "sampleCount": len(results_list),
        "dayCount": time_points,
        "pollTrend": trends_list,
        "federal": federals_list,
        "result": results_list,
        "fundamentals": fundamentals_list,
        "incumbencies": incumbencies_list,
        "days": days_list_t,
        "leftoverTime": leftover_time,
        "yearsPrior": years_prior_list,

        "obsPriorSigma": validation.current_hyperparam_set.sigma_prior,
        "federalObsPriorSigma": validation.current_hyperparam_set.sigma_prior,
        "federalStateDifferenceSigma": validation.current_hyperparam_set.federal_state_difference_sigma,
        "pollWeightChangeSigma": validation.current_hyperparam_set.poll_weight_change_sigma,
        "sigmaChangeSigma": validation.current_hyperparam_set.sigma_change_sigma,
        "biasChangeSigma": validation.current_hyperparam_set.bias_change_sigma,
        "recencyBiasHalfLife": validation.current_hyperparam_set.recency_bias_half_life,
        "sampleWeight": validation.current_hyperparam_set.sample_weight
    }

    # get the Stan model code
    with open("./Models/adjust_model.stan", "r") as f:
        model = f.read()

    # print('Beginning sampling...')

    # Stan model configuration
    chains = 8
    iterations = 300

    # encode the STAN model in C++ or retrieve it if already cached
    sm = stan_cache(model_code=model)

    # Do model sampling. Time for diagnostic purposes
    start_time = perf_counter()

    # This suppresses the pystan stdout output
    old = os.dup(1)
    os.close(1)
    os.open("pystan.log", os.O_WRONLY) # should open on 1

    fit = sm.sampling(data=stan_data,
                    iter=iterations,
                    chains=chains,
                    refresh=-1,
                    control={'max_treedepth': 18,
                            'adapt_delta': 0.9})
    
    # Restore stdout
    os.close(1)
    os.dup(old) # should dup to 1
    os.close(old) # get rid of left overs

    finish_time = perf_counter()
    # print('Time elapsed: ' + format(finish_time - start_time, '.2f')
    #         + ' seconds')
    # print('Stan Finished ...')

    # Check technical model diagnostics
    import pystan.diagnostics as psd
    # print(psd.check_hmc_diagnostics(fit))

    probs_list = [0.001]
    for i in range(1, 20):
        probs_list.append(i * 0.05)
    probs_list.append(0.999)
    output_probs_t = tuple(probs_list)
    summary = fit.summary(probs=output_probs_t)['summary']

    if (exclude.year() == 2024):
        print("Bias prior sigma: ", stan_data["obsPriorSigma"])
        for i in range(0, time_points):
            print(f"Median poll weight day {from_triangular(i)}: ", summary[i][0])
        for i in range(0, time_points):
            print(f"Median sigma day {from_triangular(i)}: ", summary[time_points + i][0])
        for i in range(0, time_points):
            print(f"Median federal poll weight day {from_triangular(i)}: ", summary[2 * time_points + i][0])
        for i in range(0, time_points):
            print(f"Median federal sigma day {from_triangular(i)}: ", summary[3 * time_points + i][0])
        for i in range(0, time_points):
            print(f"Median bias day {from_triangular(i)}: ", summary[4 * time_points + i][0])
        for i in range(0, time_points):
            print(f"Median federal bias day {from_triangular(i)}: ", summary[5 * time_points + i][0])
        # for i in range(0, time_points):
        #     print(f"Median incumbency bias dayfrom_triangular(i)}: ", summary[6 * time_points + i][0])

    election = exclude
    with open('./Data/eventual-results.csv', 'r') as f:
        exclude_results_all = {
            ElectionPartyCode(ElectionCode(a[0], a[1]), a[2]): float(a[3])
            for a in [b.strip().split(',') for b in f.readlines()]
        }
        if ElectionPartyCode(election, party) not in exclude_results_all:
            return
        exclude_results = exclude_results_all[ElectionPartyCode(election, party)]

    with open(f'./Fundamentals/fundamentals_{election.year()}{election.region()}.csv', 'r') as f:
        print(f'{election.year()}{election.region()}')
        lines = f.readlines()
        exclude_fundamentals_all = {
            a[0]: float(a[1])
            for a in [b.strip().split(',') for b in lines]
        }
        if party not in exclude_fundamentals_all:
            return  # can't use this election to validate
        exclude_fundamentals = exclude_fundamentals_all[party]

    exclude_federal = 1 if election.region() == "fed" else 0

    # get excluded election trend
    cutoff_dir = './Outputs/Cutoffs'
    # latest_cutoff = 10000
    # exclude_trend = None
    errors = []
    for filename in os.listdir(cutoff_dir):
        if filename.startswith(f'fp_trend_{election.year()}{election.region()}_{party}_') and filename.endswith('d.csv'):
            num_days = int(filename.split('_')[-1].split('d.csv')[0])
            cutoff_filename = os.path.join(cutoff_dir, filename)
            exclude_trend = None
            with open(cutoff_filename, 'r') as f:
                lines = f.readlines()
                if len(lines) >= 4:
                    elements = lines[3].strip().split(',')
                    if len(elements) >= 52:
                        extracted_value = float(elements[51])
                        exclude_trend = extracted_value
            time_point = to_triangular(num_days)

            this_poll_weight = summary[time_point][0]
            this_sigma = summary[time_points + time_point][0]
            this_federal_poll_weight = summary[2 * time_points + time_point][0]
            this_federal_sigma = summary[3 * time_points + time_point][0]
            this_bias = summary[4 * time_points + time_point][0]
            this_federal_bias = summary[5 * time_points + time_point][0]                

            exclude_trend_estimate = exclude_trend + (this_federal_bias if exclude_federal else this_bias)
            exclude_poll_weight = this_federal_poll_weight if exclude_federal else this_poll_weight
            exclude_effective_sigma = this_federal_sigma if exclude_federal else this_sigma
            exclude_final_estimate = exclude_trend_estimate * exclude_poll_weight + exclude_fundamentals * (1 - exclude_poll_weight)
            
            # print(f"num_days: {num_days}")
            # print(f"exclude_fundamentals: {exclude_fundamentals}")
            # print(f"this_poll_weight: {this_poll_weight}, this_sigma: {this_sigma}, this_federal_poll_weight: {this_federal_poll_weight}, this_federal_sigma: {this_federal_sigma}, this_bias: {this_bias}, this_federal_bias: {this_federal_bias}")
            # print(f"exclude_trend_estimate: {exclude_trend_estimate}, exclude_poll_weight: {exclude_poll_weight}, exclude_effective_sigma: {exclude_effective_sigma}, exclude_final_estimate: {exclude_final_estimate}")

            # print(f"{election}, {num_days} days remaining: "
            #       f"Final Estimate: {exclude_final_estimate:.2f}, "
            #       f"actual: {exclude_results:.2f}, "
            #       f"sigma: {exclude_effective_sigma:.3f}, "
            #       f"error: {exclude_final_estimate - exclude_results:.3f} "
            #       f"z: {(exclude_final_estimate - exclude_results) / exclude_effective_sigma:.3f} ")
            errors.append(exclude_final_estimate - exclude_results)

    validation.errors[election] = average([abs(a) for a in errors])
    # print(f"Average error: {validation.errors[election]}")


def complete_validation(config, validation):
    for hyperparam_set in validation.hyperparam_sets:
        print(f"Sigma prior: {hyperparam_set.sigma_prior},"
              f" Average error: {hyperparam_set.average_error},"
              f" Median error: {hyperparam_set.median_error},"
              f" RMSE: {hyperparam_set.rmse}")


def trend_adjust():
    try:
        config = Config()
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return
    
    validation_parties = ['ONP FP']
    
    validations = {}
    for party in validation_parties:
        validations[party] = Validation()

    if config.check != "only":
        for i in range(1, 100):
            print(f"Annealing iteration: {i}")

            for exclude in config.elections:

                print(f'Loading inputs for {exclude}')
                inputs = Inputs(exclude)

                print(f'Loading poll trends for {exclude}')
                poll_trend = PollTrend(inputs, config)

                # Leave this until now so it doesn't interfere with initialization
                # of poll_trend
                print(f'Determining eventual "others" results for: {exclude}')
                inputs.determine_eventual_others_results()

                print(f'Determining fundamentals for: {exclude}')
                run_fundamentals_regression(config, inputs, exclude)

                for party in validation_parties:
                    print(f'Beginning trend adjustment algorithm for: {party} in {exclude}')
                    temp_run_stan(config, inputs, poll_trend, validations[party], party, exclude)

                # test_procedure(config, inputs, poll_trend, exclude)
                print(f'Completed trend adjustment algorithm for: {exclude}')

            for party in validation_parties:
                print(f'Analysing results for: {party}')
                validations[party].analyse()
                print(f'Next hyperparam set for: {party}')
                validations[party].next_hyperparam_set()
    
    for party in validation_parties:
        print(f'Final analysis for: {party}')
        complete_validation(config, validations[party])

    # if config.check == "only" or config.check == "yes":
    #     check_poll_predictiveness(config)


if __name__ == '__main__':
    trend_adjust()
