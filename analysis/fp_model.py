import argparse
import chime
import datetime
import math
import numpy as np
import os
import pandas as pd
import pystan
import sys
import statistics
from approvals import generate_synthetic_tpps
from datetime import timedelta
from election_code import ElectionCode
from time import perf_counter

from stan_cache import stan_cache


# File paths for polling data in each jurisdiction
data_source = {
    'fed': './Data/poll-data-fed.csv',
    'nsw': './Data/poll-data-nsw.csv',
    'vic': './Data/poll-data-vic.csv',
    'qld': './Data/poll-data-qld.csv',
    'wa': './Data/poll-data-wa.csv',
    'sa': './Data/poll-data-sa.csv',
}


# N.B. The "Others" (OTH) "party" values include votes for these other
# minor parties, so these are effectively counted twice. The reason for
# this is that many polls do not report separate UAP/ONP figures, so they
# are aggregated from the polls that do, count them together with the
# other "others" under OTH, and then (in the main program) subtract the
# minor parties from the OTH value to get the true exclusive-others value
others_parties = ['ONP FP', 'UAP FP', 'SFF FP', 'CA FP',
                'KAP FP', 'SAB FP', 'DEM FP', 'FF FP',
                'DLP FP']

major_parties = ['ALP FP', 'LNP FP', 'LIB FP']


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
        parser.add_argument('-c', '--calibrate', action='store_true',
                            help='If set, will run in pollster calibration '
                            'mode. This will exclude each pollster from '
                            'calculations so that their polls can be '
                            'calibrated using the trend from the other polls.')
        parser.add_argument('-b', '--bias', action='store_true',
                            help='If set, will run in bias calibration '
                            'mode. This will record relative bias for each '
                            'pollster that can then be used to calibrate '
                            'the house effects in actual forecast runs. '
                            'Ignored if --calibrate is also used.')
        parser.add_argument('--cutoff', action='store', type=int,
                            help='Exclude polls occurring fewer than this many'
                            ' days before an election. Useful for creating'
                            ' hindcasts for previous elections.', 
                            default=0)
        parser.add_argument('--pure', action='store_true',
                            help="Only use primary voting intention results, "
                            'not approval ratings, TPP-only polls or other '
                            'measures. Outputs with _pure suffix.', 
                            default=0)
        self.election_instructions = parser.parse_args().election.lower()
        self.calibrate_pollsters = parser.parse_args().calibrate == True
        self.calibrate_bias = (not self.calibrate_pollsters and 
                               parser.parse_args().bias == True)
        self.cutoff = parser.parse_args().cutoff
        self.pure = parser.parse_args().pure == True
        self.prepare_election_list()

    def prepare_election_list(self):
        with open('./Data/polled-elections.csv', 'r') as f:
            elections = ElectionCode.load_elections_from_file(f)
        if self.cutoff == 0:
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

    def use_approvals(self):
        return (
            not self.calibrate_pollsters
            and not self.calibrate_bias
            and not self.pure
        )


class ModellingData:
    def __init__(self):
            
        # Load the file containing a list of significant parties for each election
        # and arrange the data for the rest of the program to efficiently use
        with open('./Data/significant-parties.csv', 'r') as f:
            self.parties = {
                (a[0], a[1]): a[2:] for a in
                [b.strip().split(',') for b in f.readlines()]}

        with open('./Data/preference-estimates.csv', 'r') as f:
            self.preference_flows = {
                (a[0], a[1], a[2]): (float(a[3]) * 0.01,
                float(a[4]) * 0.01 if len(a) > 4 and a[4][0] != "#" else 0)
                for a in [b.strip().split(',') for b in f.readlines()]}

        # Load the file containing prior results for each election
        with open('./Data/prior-results.csv', 'r') as f:
            self.prior_results = {((a[0], a[1]), a[2]): float(a[3]) for a in
                            [b.strip().split(',') for b in
                            f.readlines()]}

        # Discontinuities for leader changes
        # or other exceptionally significant events
        with open('./Data/discontinuities.csv', 'r') as f:
            self.discontinuities = {a[0]: a[1:] for a in [b.strip().split(',')
                            for b in f.readlines()]}

        # Number of iterations to run for each model
        # (note: half of the iterations will be warm-up)
        # At least 300 is recommended, more will make the
        # path more consistent at the cost of taking more time
        # Sparsely polled periods take more time as the model has more freedom
        with open('./Data/desired-iterations.csv', 'r') as f:
            self.desired_iterations = {(a[0], a[1]): int(a[2]) for a in [
                                b.strip().split(',') for b in f.readlines()]}

        # Load the dates of next and previous elections
        # We will only model polls between those two dates
        with open('./Data/election-cycles.csv', 'r') as f:
            self.election_cycles = {
                (a[0], a[1]):
                (
                    pd.Timestamp(a[2]),
                    pd.Timestamp(a[3])
                )
                for a in [b.strip().split(',')
                for b in f.readlines()]
            }


class ElectionData:
    def __init__(self, config, m_data, desired_election):
        self.e_tuple = (str(desired_election.year()),
                          desired_election.region())           
        tup = self.e_tuple
        self.others_medians = {}

        if not (config.calibrate_pollsters or config.calibrate_bias):
            self.get_pollster_analysis(desired_election)

        # collect the model data
        self.base_df = pd.read_csv(data_source[tup[1]])

        # Drop this column to make debug output more useful
        self.base_df.drop('Comments', axis=1)

        # drop data not in range of this election period
        self.base_df['MidDate'] = [pd.Timestamp(date)
                            for date in self.base_df['MidDate']]
        self.start_date = m_data.election_cycles[tup][0]
        self.end_date = (m_data.election_cycles[tup][1] - 
                    pd.to_timedelta(config.cutoff, unit="D"))
        self.base_df = self.base_df[self.base_df['MidDate'] >= self.start_date]
        self.base_df = self.base_df[self.base_df['MidDate'] <= self.end_date]

        # convert dates to days from start
        # do this before removing polls with N/A values so that
        # start times are consistent amongst series
        # (otherwise, a poll missing some parties, or with only approval ratings,
        # could cause inconsistent date indexing)
        self.start = self.base_df['MidDate'].min()  # day zero
        self.end = self.base_df['MidDate'].max()
        # day number for each poll
        self.base_df['Day'] = (self.base_df['MidDate'] - self.start).dt.days
        self.n_days = self.base_df['Day'].max() + 1
        self.days_to_election = (m_data.election_cycles[tup][1] - self.end).days

        # drop data without a defined OTH FP (since that would indicate
        # that we don't know if undecided were excluded)
        self.base_df.dropna(subset=['OTH FP'], inplace=True)

        # store the election day for when the model needs it later
        self.election_day = (m_data.election_cycles[tup][1] - self.start).days

        self.all_houses = self.base_df['Firm'].unique().tolist()

        if config.calibrate_pollsters:
            # Don't run calibration for any pollsters with only one poll
            # in this election period as at least two polls are required
            self.pollster_exclusions = \
                [a for a in self.all_houses if
                 list(self.base_df['Firm']).count(a) > 1]
            
            self.pollster_exclusions += ['']

            self.poll_calibrations = {}
        else:
            self.pollster_exclusions = ['']

        self.create_day_series()

        if len(self.base_df.index) == 0: return

        self.base_df['OTH base'] = self.base_df['OTH FP']

        self.create_tpp_series(m_data=m_data, 
                               desired_election=desired_election, 
                               df=self.base_df)

    def create_tpp_series(self, m_data, desired_election, df):
        self.base_df['OTH FP'] = self.base_df['OTH base']
        if 'old_tpp' not in df:
            df['old_tpp'] = df['@TPP']
        adjustments = {a: 0 for a in df.index.values}
        for others_party in others_parties + ['GRN FP']:
            if others_party in df and others_party in self.others_medians:
                pref_tuple = (self.e_tuple[0], self.e_tuple[1], others_party)
                oth_tuple = (self.e_tuple[0], self.e_tuple[1], 'OTH FP')
                adj_flow = (m_data.preference_flows[pref_tuple][0] -
                            m_data.preference_flows[oth_tuple][0])
                for a in adjustments.keys():
                    if math.isnan(df.loc[a, others_party]):
                        day = df.loc[a, 'Day']
                        estimated_fp = self.others_medians[others_party][day]
                        pref_adjust = estimated_fp * adj_flow
                        adjustments[a] += pref_adjust
        adjustment_series = pd.Series(data=adjustments)
        df['Total'] = (df['ALP FP'] + df['LIB FP'
                       if 'LIB FP' in df else 'LNP FP'])
        df['@TPP'] = df['ALP FP']
        for column in df:
            pref_tuple = (self.e_tuple[0], self.e_tuple[1], column)
            if pref_tuple not in m_data.preference_flows:
                continue
            preference_flow = m_data.preference_flows[pref_tuple][0]
            preference_survival = 1 - m_data.preference_flows[pref_tuple][1]
            if column == 'OTH FP':
                lnp_col = 'LIB FP' if 'LIB FP' in df else 'LNP FP'
                df['OTH FP'] = df.apply(
                    lambda row: (
                        100 - row['ALP FP'] - row[lnp_col] - 
                        (row['GRN FP'] if not math.isnan(row['GRN FP']) else 0)
                        if pd.isnull(row['OTH FP']) else row['OTH FP']
                        ),
                    axis=1
                )
            pref_col = df[column].fillna(0)
            df['@TPP'] += pref_col * preference_flow * preference_survival
            df['Total'] += pref_col * preference_survival
        df['@TPP'] += adjustment_series
        df['@TPP'] /= (df['Total'] * 0.01)
        if desired_election.region() == 'fed':
            df['@TPP'] += 0.1  # leakage in LIB/NAT seats
        
        # This order is important: do not want to overwrite
        # the OTH FP column until after the TPP has been calculated
        self.combine_others_parties()

        print(desired_election)
        print("Note: TPP values will not be accurate until the contribution others-medians are calculated.")
        print(self.base_df)
    
    def combine_others_parties(self):
        # push misc parties into Others, as explained above
        for others_party in others_parties:
            try:
                # make sure any N/A values do not get
                # propagated into the Others data
                tempCol = self.base_df[others_party].fillna(0)
                self.base_df['OTH FP'] = self.base_df['OTH FP'] + tempCol
            except KeyError:
                pass  # it's expected that some parties aren't in the file

        # remove imputed Greens vote from OTH (if it exists)
        # This occurs in cases where the Greens are considered a
        # significant party but a poll doesn't report their vote
        # in which case they are included among "Others"
        # In order to make sure that "Others" has the same meaning for
        # each poll, we need to remove the imputed Greens vote from it
        # as estimated from the Greens poll trend
        if 'GRN FP' in self.base_df and 'GRN FP' in self.others_medians:
            # create dict with imputed GRN values
            adjustments = {a: 0 for a in self.base_df.index.values}
            for a in adjustments.keys():
                if math.isnan(self.base_df.loc[a, 'GRN FP']):
                    day = self.base_df.loc[a, 'Day']
                    estimated_fp = self.others_medians['GRN FP'][day]
                    adjustments[a] += estimated_fp
                    adjustments[a] = min(self.base_df.loc[a, 'OTH FP'], adjustments[a])
            adjustment_series = pd.Series(data=adjustments)
            # Subtract imputed GRN values from OTH
            self.base_df['OTH FP'] -= adjustment_series

    def create_day_series(self):
        # Convert "days" objects into raw numerical data
        # that Stan can accept
        for i in self.base_df.index:
            self.base_df.loc[i, 'DayNum'] = int(self.base_df.loc[i, 'Day'] + 1)
    
    def get_pollster_analysis(self, desired_election):
        code = desired_election.short()
        with open(f'./Outputs/Calibration/variability-{code}.csv', 'r') as f:
            self.pollster_sigmas = {(a[0], a[1]): float(a[2])
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}

        with open(f'./Outputs/Calibration/he_weighting-{code}.csv', 'r') as f:
            self.pollster_he_weights = {(a[0], a[1]): float(a[2])
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}

        with open(f'./Outputs/Calibration/biases-{code}.csv', 'r') as f:
            self.pollster_biases = {(a[0], a[1]): (float(a[2]), float(a[3]))
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}


def calibrate_pollsters(e_data, exc_polls, excluded_pollster, party, summary,
                        n_houses, output_probs, df):
    exc_poll_data = [a for a in zip(exc_polls['DayNum'], exc_polls[party],
                     exc_polls.axes[0], exc_polls['Firm'])]
    if len(exc_poll_data) <= 1: return
    print(f'Trend closeness statistics for {excluded_pollster}')
    offset = e_data.n_days + n_houses * 2 - 1
    median_col = 3 + output_probs.index(0.5)
    diff_sum = {}
    pollster_count = {}
    house_effects = {}
    for a in exc_poll_data:
        day, vote, pollster = int(a[0]), a[1], a[3]
        table_index = day + offset
        trend_value = summary[table_index][median_col]
        if pollster not in diff_sum:
            diff_sum[pollster] = 0
            pollster_count[pollster] = 0
        diff_sum[pollster] += vote - trend_value
        pollster_count[pollster] += 1
    for key in diff_sum.keys():
        house_effects[key] = diff_sum[key] / pollster_count[key]
        
    deviations = []
    prob_deviations = []
    for a in exc_poll_data:
        day, vote, poll_index, pollster = int(a[0]), a[1], a[2], a[3]
        table_index = day + offset
        trend_median = summary[table_index][median_col]
        eff_house_effect = house_effects[pollster]
        adj_poll = vote - eff_house_effect
        # for the case where the poll is higher than any
        # probability threshold, have this as the default value
        percentile = 0.999 
        for index, upper_prob in enumerate(output_probs):
            upper_value = summary[table_index][index + 3]
            if adj_poll < upper_value:
                if index == 0:
                    percentile = 0.001
                else:
                    lower_value = summary[table_index][index + 2]
                    lower_prob = output_probs[index - 1]
                    lerp = ((adj_poll - lower_value) /
                            (upper_value - lower_value))
                    percentile = (lower_prob + lerp * 
                                (upper_prob - lower_prob))
                break
            upper_value = summary[table_index][index + 3]
        deviation = adj_poll - trend_median
        prob_deviation = abs(percentile - 0.5)
        neighbours = sum([min(1, 2 ** (-abs(day - other_day) / 20) * 0.5)
                      for other_day in df['DayNum']
                     ])
        e_data.poll_calibrations[(excluded_pollster, day,
                                  party, poll_index)] = \
            (vote, trend_median, adj_poll, 
             percentile, deviation, prob_deviation, neighbours)
        deviations.append(deviation)
        prob_deviations.append(prob_deviation)
    std_dev = statistics.stdev(deviations)
    prob_dev_avg = statistics.mean(prob_deviations)
    print(f'Overall ({excluded_pollster}, {party}):'
          f' standard deviation from trend median: {std_dev}'
          f' average probability deviation: {prob_dev_avg}')


def output_filename(config, e_data, party, excluded_pollster, file_type):

    # construct the file names that the script will output results into -
    # put calibration files in calibration folder, with the file name
    # appended with the pollster name if calibrated for a pollster's variance
    # or "biascal" if calibrating for bias.
    pollster_append = (
        f'_{excluded_pollster}' if 
        excluded_pollster != '' else
        f'_biascal' if config.calibrate_bias else ''
    )
    e_tag = ''.join(e_data.e_tuple)
    calib_str = (
        "Calibration/" if config.calibrate_pollsters
        or config.calibrate_bias else ""
    )
    cutoff = e_data.days_to_election
    cutoff_str = "Cutoffs/" if config.cutoff > 0 else ""
    folder = (f'./Outputs/{calib_str}{cutoff_str}')
    pure_append = f'_pure' if config.pure else ''
    cutoff_append = f'_{cutoff}d' if config.cutoff > 0 else ''

    return (
        f'{folder}fp_{file_type}_{e_tag}_{party}{pollster_append}'
        f'{pure_append}{cutoff_append}.csv'
    )


def run_individual_party(config, m_data, e_data,
                         excluded_pollster, party):

    df = e_data.base_df.copy()

    # drop any rows with N/A values for the current party
    df = df.dropna(subset=[party])

    # If we're not excluding any pollster then we want to record
    # calibration stats for all pollsters (so that they may be
    # compared to those with pollsters excluded)
    if excluded_pollster != '':
        exc_polls = df[df.Firm == excluded_pollster]
        if exc_polls.empty:
            print(f'No polls by {excluded_pollster} for {party}'
                  f', skipping round')
            return
    elif config.calibrate_pollsters:
        exc_polls = df

    # if we're excluding a pollster for calibrations
    # remove their polls now
    df = df[df.Firm != excluded_pollster]
    n_polls = len(df)
    # It's possible for there to actually be no polls at all if
    # the party hasn't been polled before the cutoff date
    if n_polls == 0:
        print(f'No polls for party {party} at all, skipping round')
        return

    # Get the prior result, or a small vote share if
    # the prior result is not given
    if (e_data.e_tuple, party) in m_data.prior_results:
        prior_result = max(0.25, m_data.prior_results[(e_data.e_tuple, party)])
    elif party == '@TPP':
        prior_result = 50  # placeholder TPP
    else:
        prior_result = 0.25  # percentage

    # Get a series for any missing data
    missing = df[party].apply(lambda x: 1 if np.isnan(x) else 0)
    y = df[party].fillna(prior_result)
    y = y.apply(lambda x: max(x, 0.01))

    # We are excluding some houses
    # from the sum to zero constraint because
    # they have unusual or infrequent poll results compared
    # with other pollsters
    # Organise the polling houses so that the pollsters
    # included in the sum-to-zero are first, and then the
    # others follow
    houses = df['Firm'].unique().tolist()
    house_map = dict(zip(houses, range(1, len(houses)+1)))
    df['House'] = df['Firm'].map(house_map)
    n_houses = len(df['House'].unique())

    # Transform discontinuities from dates to raw numbers
    discontinuities_filtered = m_data.discontinuities[e_data.e_tuple[1]]
    discontinuities_filtered = \
        [(pd.Timestamp(date) - e_data.start).days + 1
            for date in discontinuities_filtered]

    # Remove discontinuities outside of the election period
    discontinuities_filtered = \
        [date for date in discontinuities_filtered
            if date >= 0 and date < e_data.n_days]

    # Stan doesn't like zero-length arrays so put in a dummy value
    # if there are no discontinuities
    if not discontinuities_filtered:
        discontinuities_filtered.append(0)

    # Have a standard sigma for calibrating pollsters,
    # otherwise used the observed sigmas
    sample_size = 1000
    calibration_sigma = np.sqrt((50 * 50) / (sample_size))
    sigmas = df['Firm'].apply(
        lambda x: calibration_sigma if (
            config.calibrate_pollsters or config.calibrate_bias
        ) else
        e_data.pollster_sigmas[(x, party)] if
        (x, party) in e_data.pollster_sigmas else 3
    )

    # Equal weights for house effects when calibrating,
    # use determined house effect weights when running forecasts
    he_weights = [
        1 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_he_weights[(x, party)] ** 2 if
        (x, party) in e_data.pollster_he_weights else 0.05
        for x in houses
    ]

    # Equal weights for house effects when calibrating,
    # use determined house effect weights when running forecasts
    biases = [
        0 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_biases[(x, party)][0] if
        (x, party) in e_data.pollster_biases else 0
        for x in houses
    ]

    houseEffectOld = 240
    houseEffectNew = 120

    # Print an estimate for the expected house effect sum
    # (doesn't have any impact on subsequent calculations)
    weightedBiasSum = 0
    housePollCount = [0 for a in houses]
    houseWeight = [0 for a in houses]
    houseList = df['House'].values.tolist()
    for poll in range(0, n_polls):
        housePollCount[houseList[poll] - 1] = housePollCount[houseList[poll] - 1] + 1
    for house in range(0, n_houses):
        houseWeight[house] = he_weights[house]
        weightedBiasSum += biases[house] * houseWeight[house]
    totalHouseWeight = sum(houseWeight)
    weightedBias = weightedBiasSum / totalHouseWeight
    print(f'Expected house effect sum: {weightedBias}')
    print(f'House effect weights: {houseWeight} for {houses}')

    # convert columns to list
    pollObs = y.values.tolist()
    missingObs = missing.values.tolist()
    pollHouses = df['House'].values.tolist()
    pollDays = [int(a) for a in df['DayNum'].values]
    sigmasList = sigmas.values.tolist()

    # Add synthetic data (from approval ratings)
    # for TPP and major party primaries
    if config.use_approvals():
        if party == "@TPP" or party in major_parties:
            with open(f'Synthetic TPPs/{e_data.e_tuple[1]}.csv') as f:
                approvals = [
                    line.strip().split(',')
                    for line in f.readlines()
                ]
            approvals = [
                (   #date, tpp, info weight
                    pd.Timestamp(line[0]),
                    float(line[2]), float(line[3])
                )
                for line in approvals
                if (pd.Timestamp(line[0]) >= e_data.start_date
                    and pd.Timestamp(line[0]) <= e_data.end_date)
            ]
            
            # Go through each approval and remove the part of the TPP
            # that comes from preferences, leaving an estimate of the
            # major party FP
            if party == 'ALP FP':
                for oth_party in others_parties + ['GRN FP', 'NAT FP', 'OTH FP']:
                    if oth_party in e_data.others_medians:
                        pref_tuple = (e_data.e_tuple[0], e_data.e_tuple[1], oth_party)
                        flow = m_data.preference_flows[pref_tuple][0]
                        approvals = [
                            (
                                a, 
                                b - flow *
                                e_data.others_medians[oth_party][(a - e_data.start).days],
                                c
                            )
                            for a, b, c in approvals
                        ]
            elif party in ['LNP FP', 'LIB FP']:
                # Convert to LNP TPP
                approvals = [(a, 100 - b, c) for a, b, c in approvals]
                for oth_party in others_parties + ['GRN FP', 'NAT FP', 'OTH FP']:
                    if oth_party in e_data.others_medians:
                        pref_tuple = (e_data.e_tuple[0], e_data.e_tuple[1], oth_party)
                        flow = m_data.preference_flows[pref_tuple][0]
                        approvals = [
                            (
                                a,
                                b - (1 - flow) *
                                # if the other party's trend doesn't reach this
                                # point, just use the last value
                                e_data.others_medians[oth_party][min(
                                    (a - e_data.start).days,
                                    len(e_data.others_medians[oth_party]) - 1
                                )],
                                c
                            )
                            for a, b, c in approvals
                        ]

            n_polls += len(approvals)
            n_houses += 1
            houses += ['Approvals']
            pollObs += [a[1] for a in approvals]
            missingObs += [0 for a in approvals]
            pollHouses += [len(houses) - 1 for a in approvals]
            pollDays += [(a[0] - e_data.start).days + 1 for a in approvals]
            # Sigma of approval rating-derived TPP will be between 3 and 5
            # depending on the weight of the approval rating
            # Even at the lowest end this is similar to a "bad" poll
            # and overwhelmed by a good poll
            sigmasList += [max(3, 5 - a[2]) for a in approvals]
            he_weights += [0]
            biases += [0]

    # Prepare the data for Stan to process
    stan_data = {
        'dayCount': e_data.n_days,
        'pollCount': n_polls,
        'houseCount': n_houses,
        'discontinuityCount': len(discontinuities_filtered),
        'priorResult': prior_result,

        'pollObservations': pollObs,
        'missingObservations': missingObs,
        'pollHouse': pollHouses,
        'pollDay': pollDays,
        'discontinuities': discontinuities_filtered,
        'sigmas': sigmasList,
        'heWeights': he_weights,
        'biases': biases,

        'electionDay': e_data.election_day,

        # distributions for the daily change in vote share
        # higher values during campaigns, since it's more likely
        # people are paying attention and changing their mind then
        'dailySigma': 0.25,
        'campaignSigma': 0.45,
        'finalSigma': 0.7,

        # prior distribution for each house effect
        # modelled as a double exponential to avoid
        # easily giving a large house effect, but
        # still giving a big one when it's really warranted
        'houseEffectSigma': 1.2,

        # prior distribution for sum of house effects
        # keep this very small, will deal with systemic bias variability
        # in the main program, so for now keep the variance of house
        # effects at approximately zero
        'houseEffectSumSigma': 0.001,

        # prior distribution for each day's vote share
        # very weak prior, want to avoid pulling extreme vote shares
        # towards the center since that historically harms accuracy
        'priorVoteShareSigma': 200.0,

        # Bounds for the transition between
        'houseEffectOld': houseEffectOld,
        'houseEffectNew': houseEffectNew
    }

    # get the Stan model code
    with open("./Models/fp_model.stan", "r") as f:
        model = f.read()

    # encode the STAN model in C++ or retrieve it if already cached
    sm = stan_cache(model_code=model)

    # Report dates for model, this means we can easily check if new
    # data has actually been saved without waiting for model to run
    print('Beginning sampling for ' + party + ' ...')
    end = e_data.start + timedelta(days=int(e_data.n_days))
    print('Start date of model: ' + e_data.start.strftime('%Y-%m-%d\n'))
    print('End date of model: ' + end.strftime('%Y-%m-%d\n'))

    # Stan model configuration
    chains = 8
    iterations = m_data.desired_iterations[e_data.e_tuple]

    # Do model sampling. Time for diagnostic purposes
    start_time = perf_counter()
    fit = sm.sampling(data=stan_data,
                        iter=iterations,
                        chains=chains,
                        control={'max_treedepth': 16,
                                'adapt_delta': 0.8})
    finish_time = perf_counter()
    print('Time elapsed: ' + format(finish_time - start_time, '.2f')
            + ' seconds')
    print('Stan Finished ...')

    # Check technical model diagnostics
    import pystan.diagnostics as psd
    print(psd.check_hmc_diagnostics(fit))

    output_trend = output_filename(config, e_data, party, excluded_pollster, 'trend')
    output_polls = output_filename(config, e_data, party, excluded_pollster, 'polls')
    output_house_effects = output_filename(config, e_data, party, excluded_pollster, 'house_effects')

    if party in others_parties or party in ['GRN FP', 'NAT FP', 'OTH FP']:
        e_data.others_medians[party] = {}

    # Extract trend data from model summary and write to file
    probs_list = [0.001]
    for i in range(1, 100):
        probs_list.append(i * 0.01)
    probs_list.append(0.999)
    output_probs = tuple(probs_list)
    summary = fit.summary(probs=output_probs)['summary']
    trend_file = open(output_trend, 'w')
    trend_file.write('Start date day,Month,Year\n')
    trend_file.write(e_data.start.strftime('%d,%m,%Y\n'))
    trend_file.write('Day,Party')
    for prob in output_probs:
        trend_file.write(',' + str(round(prob * 100)) + "%")
    trend_file.write('\n')
    # need to get past the centered values and house effects
    # this is where the actual FP trend starts
    offset = e_data.n_days + n_houses * 2
    for summaryDay in range(0, e_data.n_days):
        table_index = summaryDay + offset
        to_write = str(summaryDay) + ","
        to_write += party + ","
        for col in range(3, 3+len(output_probs)-1):
            trend_value = summary[table_index][col]
            to_write += str(round(trend_value, 3)) + ','

        # Prepare others-medians, this isn't related to the trend file
        # but it's needed for the runs of other parties
        if party in others_parties or party in ['GRN FP', 'NAT FP', 'OTH FP']:
            # Average of first and last
            median_col = math.floor((4+len(output_probs)) / 2)
            median_val = summary[table_index][median_col]
            e_data.others_medians[party][summaryDay] = median_val
            # The others-median should exclude "others" parties that already
            # have trend medians recorded, otherwise they will be double
            # counted.
            if party == 'OTH FP':
                for oth_party in e_data.others_medians.keys():
                    if oth_party in others_parties:
                        e_data.others_medians[party][summaryDay] -= \
                            e_data.others_medians[oth_party][summaryDay]
        to_write += str(round(summary[table_index][3+len(output_probs)-1], 3)) + '\n'
        if config.cutoff > 0 and summaryDay < e_data.n_days - 1: continue
        trend_file.write(to_write)
    trend_file.close()
    print('Saved trend file at ' + output_trend)

    # Extract house effect data from model summary
    new_house_effects = []
    old_house_effects = []
    offset = e_data.n_days
    for house in range(0, n_houses):
        new_house_effects.append(summary[offset + house, 0])
        old_house_effects.append(summary[offset + n_houses + house, 0])

    # Write poll data to file, giving both raw and
    # house effect adjusted values
    polls_file = open(output_polls, 'w')
    polls_file.write('Firm,Day')
    polls_file.write(',' + party)
    polls_file.write(',' + party + ' adj')
    if party == "@TPP":
        polls_file.write(',' + party + ' reported')
    polls_file.write('\n')
    for poll_index in df.index:
        if ('Brand' in df and isinstance(df.loc[poll_index, 'Brand'], str)
            and len(df.loc[poll_index, 'Brand']) > 0):
            polls_file.write(str(df.loc[poll_index, 'Brand']))
        else:
            polls_file.write(str(df.loc[poll_index, 'Firm']))
        day = int(df.loc[poll_index, 'DayNum'])
        days_ago = e_data.n_days - day
        polls_file.write(',' + str(day))
        fp = df.loc[poll_index, party]
        new_he = new_house_effects[df.loc[poll_index, 'House'] - 1]
        old_he = old_house_effects[df.loc[poll_index, 'House'] - 1]
        old_factor = ((days_ago - houseEffectNew) /
                        (houseEffectOld - houseEffectNew))
        old_factor = max(min(old_factor, 1), 0)
        mixed_he = (old_factor * old_he +
                    (1 - old_factor) * new_he)
        adjusted_fp = fp - mixed_he
        polls_file.write(',' + str(round(fp, 3)))
        polls_file.write(',' + str(round(adjusted_fp, 3)))
        if party == "@TPP":
            polls_file.write(',' + str(round(df.loc[poll_index, 'old_tpp'], 3)))
        polls_file.write('\n')
    polls_file.close()
    print('Saved polls file at ' + output_polls)

    # Extract house effect data from model summary and write to file
    probs_list = []
    probs_list.append(0.001)
    for i in range(1, 10):
        probs_list.append(i * 0.1)
    probs_list.append(0.999)
    output_probs = tuple(probs_list)
    summary = fit.summary(probs=output_probs)['summary']
    house_effects_file = open(output_house_effects, 'w')
    house_effects_file.write('House,Party')
    for prob in output_probs:
        house_effects_file.write(',' + str(round(prob * 100)) + "%")
    house_effects_file.write('\n')
    house_effects_file.write('New house effects\n')
    offset = e_data.n_days
    for house_index in range(0, n_houses):
        house_effects_file.write(houses[house_index])
        table_index = offset + house_index
        house_effects_file.write("," + party)
        for col in range(3, 3+len(output_probs)):
            house_effects_file.write(
                ',' + str(round(summary[table_index][col], 3)))
        house_effects_file.write('\n')
    offset = e_data.n_days + n_houses
    house_effects_file.write('Old house effects\n')
    for house_index in range(0, n_houses):
        house_effects_file.write(houses[house_index])
        table_index = offset + house_index
        house_effects_file.write("," + party)
        for col in range(3, 3+len(output_probs)):
            house_effects_file.write(
                ',' + str(round(summary[table_index][col], 3)))
        house_effects_file.write('\n')

    house_effects_file.close()
    print('Saved house effects file at ' + output_house_effects)
    
    if config.calibrate_pollsters:
        calibrate_pollsters(e_data=e_data,
                            exc_polls=exc_polls,
                            excluded_pollster=excluded_pollster,
                            party=party,
                            summary=summary,
                            n_houses=n_houses,
                            output_probs=output_probs,
                            df=df
                           )


def finalise_calibrations(e_data):
    polls_string = {}
    # for key, val in e_data.poll_calibrations.items():
    #     print(f'{key}: {val}')
    total_weight = {}
    total_weighted_dev = {}
    for key, val in e_data.poll_calibrations.items():
        if (key[0] != ''):
            full_val = e_data.poll_calibrations[('', key[1], key[2], key[3])]
            cal_deviation = val[4]
            full_deviation = full_val[4]
            difference = abs(cal_deviation) - abs(full_deviation)
            quotient = min(max(0.5, abs(full_deviation)) /
                           max(0.5, abs(cal_deviation)),
                           1)
            neighbours_weight = val[6]
            final_weight = min(quotient, neighbours_weight)
            new_key = (key[0], key[2])
            if new_key not in total_weight:
                total_weight[new_key] = 0
                total_weighted_dev[new_key] = 0
                polls_string[new_key] = ''
            total_weight[new_key] += final_weight
            total_weighted_dev[new_key] += final_weight * abs(cal_deviation)
            print(f'{key}: Calibrated deviation: {cal_deviation},'
                  f' full deviation: {full_deviation},'
                  f' difference: {difference}\n '
                  f' quotient weight: {quotient},'
                  f' neighbours weight: {neighbours_weight},'
                  f' final weight: {final_weight}')
            polls_string[new_key] += (f'{key[1]},{cal_deviation},{full_deviation},'
                             f'{final_weight}\n')
    for key, val in total_weighted_dev.items():
        weight = total_weight[key]
        if weight == 0: continue
        weighted_average_deviation = val / max(weight / 2, weight - 1)
        print(f'{key}: weighted avg deviation: {weighted_average_deviation}, '
              f'total weight: {weight}')
        filename = (f'./Outputs/Calibration/calib_'
                    f'{e_data.e_tuple[0]}{e_data.e_tuple[1]}_'
                    f'{key[0]}_{key[1]}.csv')
        with open(filename, 'w') as f:
            f.write(f'{weighted_average_deviation},'
                    f'{weight},\n{polls_string[key]}')


def run_models():

    try:
        config = Config()
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        with open(f'itsdone.txt', 'w') as f:
            f.write('2')
        return

    try:
        # check version information
        print('Python version: {}'.format(sys.version))
        print('pystan version: {}'.format(pystan.__version__))

        if config.use_approvals(): generate_synthetic_tpps()

        m_data = ModellingData()

        # Load the list of election periods we want to model
        desired_elections = config.elections

        for desired_election in desired_elections:

            e_data = ElectionData(config=config,
                                m_data=m_data,
                                desired_election=desired_election)

            if len(e_data.base_df) == 0:
                print(f'No polls for election {desired_election.short()} in the requested time range, skipping')
                continue

            for excluded_pollster in e_data.pollster_exclusions:

                # Don't waste time calculating the no-pollster-excluded trend
                # if there are no pollster-excluded trends to compare it to
                # (and that is the only purpose for which it is calculated)
                if config.calibrate_pollsters and excluded_pollster == '' and \
                    len(e_data.poll_calibrations) == 0: continue

                for party in m_data.parties[e_data.e_tuple]:
                    
                    # Avoid unnecessary duplication of effort for cutoffs that would be identical
                    if config.cutoff > 0:
                        trend_filename = output_filename(config, e_data, party, excluded_pollster, 'trend')
                        print(trend_filename)

                        if os.path.exists(trend_filename):
                            print(f'Trend file for {party} in election {desired_election.short()} already exists, skipping')
                            continue

                    if party == "@TPP" or party == "OTH FP":
                        e_data.create_tpp_series(m_data,
                                                desired_election,
                                                e_data.base_df)
                        

                    if excluded_pollster != '':
                        print(f'Excluding pollster: {excluded_pollster}')
                    else:
                        print('Not excluding any pollsters.')

                    run_individual_party(config=config,
                                        m_data=m_data,
                                        e_data=e_data,
                                        excluded_pollster=excluded_pollster,
                                        party=party)

            if config.calibrate_pollsters:
                finalise_calibrations(e_data=e_data)
    except Exception as e:
        with open(f'itsdone.txt', 'w') as f:
            f.write('2')
        raise
    
    with open(f'itsdone.txt', 'w') as f:
        f.write('1')


if __name__ == '__main__':
    run_models()
