import argparse
import datetime
import math
import numpy as np
import os
import pandas as pd
import pystan
import sys
import statistics
import time
from approvals import generate_synthetic_tpps
from dataclasses import dataclass
from datetime import timedelta
from election_code import ElectionCode
from time import perf_counter
from typing import Any, List, Optional, Tuple

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
        parser.add_argument('--priority', action='store_true',
                            help="Never suspend this model.",
                            default=0)
        self.election_instructions = parser.parse_args().election.lower()
        self.calibrate_pollsters = parser.parse_args().calibrate == True
        self.calibrate_bias = (not self.calibrate_pollsters and 
                               parser.parse_args().bias == True)
        self.cutoff = parser.parse_args().cutoff
        self.pure = parser.parse_args().pure == True
        self.priority = parser.parse_args().priority == True
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


@dataclass
class ElectionDataInputs:
    config: Config
    desired_election: ElectionCode
    m_data: ModellingData

class ElectionData:
    def __init__(self, inputs: ElectionDataInputs):
        config = inputs.config
        m_data = inputs.m_data
        desired_election = inputs.desired_election

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
        # federal trend medians for selected minor parties
        self.fed_trends = {}

        # pick the most recent federal cycle covering the model end date
        fed_cycles = select_fed_cycles_for_model_end(
            SelectFedCyclesForModelEndInputs(m_data=m_data, model_end=self.end)
        )

        if len(fed_cycles) > 0:
            print(f"Using federal cycles for model prior: {', '.join(f'{c[0]}' for c in fed_cycles)}")
        else:
            print("No federal cycles found for model prior")

        fed_minor_parties = others_parties + ['GRN FP', 'OTH FP']
        for party in fed_minor_parties:
            if party in major_parties or party in ['@TPP']:
                continue
            series = load_fed_trend_series_for_party(
                LoadFedTrendSeriesForPartyInputs(fed_cycles=fed_cycles, party=party)
            )
            if series is not None:
                self.fed_trends[party] = series

        self.fed_trends_aligned = {}
        for party, series in self.fed_trends.items():
            self.fed_trends_aligned[party] = align_fed_trend_to_state(
                AlignFedTrendToStateInputs(e_data=self, fed_series=series)
            )

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

        self.create_tpp_series(self.CreateTppSeriesInputs(
            m_data=m_data, 
            desired_election=desired_election, 
            df=self.base_df
        ))

    @dataclass
    class CreateTppSeriesInputs:
        desired_election: ElectionCode
        df: pd.DataFrame
        m_data: ModellingData

    def create_tpp_series(self, inputs: CreateTppSeriesInputs):
        m_data = inputs.m_data
        desired_election = inputs.desired_election
        df = inputs.df

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


@dataclass
class PollVectors:
    houses: List[str]
    missingObs: List[int]
    n_houses: int
    n_polls: int
    pollDays: List[int]
    pollHouses: List[int]
    pollObs: List[float]
    sigmasList: List[float]


@dataclass
class PriorSeries:
    prior_series_daily: List[float]
    sigma_daily: List[float]


@dataclass
class ReducedSeries:
    prior_series: PriorSeries
    prior_series_t: List[float]
    prior_sigma_t: List[float]
    tDayCount: int
    tDiscontinuities: List[int]
    tElectionDay: int
    tPollDays: List[int]
    tHouseEffectNew: int
    tHouseEffectOld: int


@dataclass
class HouseEffects:
    biases: List[float]
    he_weights: List[float]


@dataclass
class ModelParams:
    # Prior construction
    min_observation: float = 0.01
    prior_min_result: float = 0.25
    prior_sigma_no_fed: float = 200.0
    prior_sigma_fed: float = 8.0
    prior_sigma_fed_oth: float = 16.0
    prior_tpp_default: float = 50.0

    # Poll variance / calibration
    calibration_sample_size: int = 1000
    default_poll_sigma: float = 3.0

    # Approvals
    approval_sigma_min: float = 3.0
    approval_sigma_max: float = 5.0

    # Time compression / house effects
    houseEffectNew: int = 120
    houseEffectOld: int = 240
    tFactor: int = 2

    # Stan hyperparameters
    campaign_sigma_base: float = 0.45
    daily_sigma_base: float = 0.25
    final_sigma_base: float = 0.7
    house_effect_sigma: float = 1.2
    house_effect_sum_sigma: float = 0.001
    stan_adapt_delta: float = 0.8
    stan_max_treedepth: int = 18

    def validate(self):
        if self.tFactor < 1:
            raise ValueError("tFactor must be >= 1")
        if not (self.houseEffectOld > self.houseEffectNew >= 0):
            raise ValueError("houseEffectOld must be > houseEffectNew >= 0")
        if self.min_observation <= 0:
            raise ValueError("min_observation must be > 0")
        if self.approval_sigma_min > self.approval_sigma_max:
            raise ValueError("approval_sigma_min must be <= approval_sigma_max")
        for name in [
            "prior_sigma_no_fed", "prior_sigma_fed", "prior_sigma_fed_oth",
            "default_poll_sigma", "daily_sigma_base", "campaign_sigma_base",
            "final_sigma_base", "house_effect_sigma", "house_effect_sum_sigma"
        ]:
            if getattr(self, name) <= 0:
                raise ValueError(f"{name} must be > 0")


@dataclass
class SelectFedCyclesForModelEndInputs:
    m_data: ModellingData
    model_end: pd.Timestamp
    max_cycles: int = 2

def select_fed_cycles_for_model_end(inputs: SelectFedCyclesForModelEndInputs):
    m_data = inputs.m_data
    model_end = inputs.model_end
    max_cycles = inputs.max_cycles

    fed_cycles = [
        (year, start, end)
        for (year, region), (start, end) in m_data.election_cycles.items()
        if region == 'fed'
    ]
    # sort by start date
    fed_cycles.sort(key=lambda x: x[1])

    # find the most recent cycle that starts before model_end
    idx = None
    for i, (_, start, end) in enumerate(fed_cycles):
        if start <= model_end <= end:
            idx = i
            break
        if start <= model_end:
            idx = i
    
    if idx is None:
          return [fed_cycles[0]]

    # take this cycle and the previous one (if available)
    start_idx = max(0, idx - (max_cycles - 1))
    return fed_cycles[start_idx:idx + 1]


@dataclass
class LoadFedTrendMedianInputs:
    election_year: int
    party: str

def load_fed_trend_median(inputs: LoadFedTrendMedianInputs):
    election_year = inputs.election_year
    party = inputs.party

    # TODO: if we ever use cutoff files, add Cutoffs/ + _{cutoff}d here
    filename = f'./Outputs/fp_trend_{election_year}fed_{party}.csv'
    if not os.path.exists(filename):
        return None

    # read header lines
    with open(filename, 'r') as f:
        f.readline()  # "Start date day,Month,Year"
        start_line = f.readline().strip()
    start_day, start_month, start_year = [int(x) for x in start_line.split(',')]
    start_date = pd.Timestamp(start_year, start_month, start_day)

    # read the data rows
    df = pd.read_csv(filename, skiprows=2)
    # median is 51st percentile; column index 2 + 50 = 52
    median = df.iloc[:, 52]
    dates = start_date + pd.to_timedelta(df['Day'], unit='D')
    return pd.Series(median.values, index=dates)


@dataclass
class LoadFedTrendSeriesForPartyInputs:
    fed_cycles: List[Tuple[int, pd.Timestamp, pd.Timestamp]]
    party: str

def load_fed_trend_series_for_party(inputs: LoadFedTrendSeriesForPartyInputs):
    fed_cycles = inputs.fed_cycles
    party = inputs.party

    combined = None

    for year, start, end in fed_cycles:
        series = load_fed_trend_median(LoadFedTrendMedianInputs(election_year=year, party=party))
        if series is None:
            continue

        if combined is None:
            combined = series
        else:
            # use newer data for overlapping dates
            combined = combined.combine_first(series)  # older first
            combined.update(series)  # overwrite with newer

    return combined


@dataclass
class AlignFedTrendToStateInputs:
    e_data: ElectionData
    fed_series: pd.Series

def align_fed_trend_to_state(inputs: AlignFedTrendToStateInputs):
    e_data = inputs.e_data
    fed_series = inputs.fed_series

    state_dates = pd.date_range(e_data.start, e_data.end, freq='D')

    if fed_series is None or fed_series.empty:
        return pd.Series([None] * len(state_dates), index=state_dates)

    aligned = pd.Series([None] * len(state_dates), index=state_dates)

    fed_start = fed_series.index.min()
    mask = state_dates >= fed_start

    # Reindex to state dates, forward-fill (last available value)
    fed_reindexed = fed_series.reindex(state_dates[mask], method='ffill')

    aligned.loc[mask] = fed_reindexed.values
    return aligned


@dataclass
class OutputFilenameInputs:
    config: Config
    e_data: ElectionData
    excluded_pollster: str
    file_type: str
    party: str

def output_filename(inputs: OutputFilenameInputs):
    config = inputs.config
    e_data = inputs.e_data
    party = inputs.party
    excluded_pollster = inputs.excluded_pollster
    file_type = inputs.file_type

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


@dataclass
class RunContext:
    house_effects: HouseEffects
    model_params: ModelParams
    poll_vectors: PollVectors
    prior_result: float
    reduced_series: ReducedSeries


@dataclass
class ModelInputs:
    chains: int
    e_data: ElectionData
    iterations: int
    model_params: ModelParams
    party: str
    stan_data: dict


@dataclass
class PartyContext:
    config: Config
    e_data: ElectionData
    excluded_pollster: str
    m_data: ModellingData
    model_params: ModelParams
    party: str


@dataclass
class PollPrepResult:
    df: pd.DataFrame
    exc_polls: pd.DataFrame


@dataclass
class OutputContext:
    config: Config
    e_data: ElectionData
    excluded_pollster: str
    party: str
    poll_prep_result: PollPrepResult
    run_context: RunContext


@dataclass
class TrendOutputs:
    day_data: List[List[float]]


def prepare_poll_df(party_context: PartyContext) -> Optional[PollPrepResult]:
    e_data = party_context.e_data
    party = party_context.party
    excluded_pollster = party_context.excluded_pollster
    config = party_context.config

    df = party_context.e_data.base_df.copy()

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
            return None
    elif config.calibrate_pollsters:
        exc_polls = df
    else:
        # Make sure we have an empty dataframe with the right columns
        # to avoid errors but still skip the calibration process later on
        exc_polls = df.iloc[0:0]

    # if we're excluding a pollster for calibrations
    # remove their polls now
    df = df[df.Firm != excluded_pollster]
    n_polls = len(df)
    # It's possible for there to actually be no polls at all if
    # the party hasn't been polled before the cutoff date
    if n_polls == 0:
        print(f'No polls for party {party} at all, skipping round')
        return None
    
    return PollPrepResult(df=df, exc_polls=exc_polls)


def get_prior_result(party_context: PartyContext) -> float:
    m_data = party_context.m_data
    e_data = party_context.e_data
    party = party_context.party
    model_params = party_context.model_params

    # Get the prior result, or a small vote share if
    # the prior result is not given
    if (e_data.e_tuple, party) in m_data.prior_results:
        prior_result = max(model_params.prior_min_result, m_data.prior_results[(e_data.e_tuple, party)])
    elif party == '@TPP':
        prior_result = model_params.prior_tpp_default  # placeholder TPP
    else:
        prior_result = model_params.prior_min_result  # percentage
    print(party)
    print(prior_result)

    return prior_result


@dataclass
class PollVectorInputs:
    df: pd.DataFrame
    party_context: PartyContext
    prior_result: float


def build_poll_vectors(inputs: PollVectorInputs) -> PollVectors:
    party_context = inputs.party_context
    model_params = party_context.model_params
    df = inputs.df
    party = party_context.party
    config = party_context.config
    e_data = party_context.e_data

    # Get a series for any missing data
    missing = df[party].apply(lambda x: 1 if np.isnan(x) else 0)
    y = df[party].fillna(inputs.prior_result)
    y = y.apply(lambda x: max(x, model_params.min_observation))

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

    # Have a standard sigma for calibrating pollsters,
    # otherwise used the observed sigmas
    sample_size = model_params.calibration_sample_size
    calibration_sigma = np.sqrt((50 * 50) / (sample_size))
    sigmas = df['Firm'].apply(
        lambda x: calibration_sigma if (
            config.calibrate_pollsters or config.calibrate_bias
        ) else
        e_data.pollster_sigmas[(x, party)] if
        (x, party) in e_data.pollster_sigmas else model_params.default_poll_sigma
    )

    # convert columns to list
    pollObs = y.values.tolist()
    missingObs = missing.values.tolist()
    pollHouses = df['House'].values.tolist()
    pollDays = [int(a) for a in df['DayNum'].values]
    sigmasList = sigmas.values.tolist()

    return PollVectors(
        pollObs=pollObs,
        missingObs=missingObs,
        pollHouses=pollHouses,
        pollDays=pollDays,
        sigmasList=sigmasList,
        houses=houses,
        n_houses=n_houses,
        n_polls=len(pollObs),
    )


def build_prior_series(party_context: PartyContext, prior_result: float) -> PriorSeries:
    e_data = party_context.e_data
    party = party_context.party
    model_params = party_context.model_params

    # Build daily series for full state period
    days = pd.date_range(e_data.start, e_data.end, freq='D')
    fed_series = e_data.fed_trends_aligned.get(party)
    prior_daily = []
    sigma_daily = []

    for day in days:
        fed_val = None if fed_series is None else fed_series.get(day, None)
        if fed_val is None or fed_val < prior_result:
            prior_daily.append(prior_result)
            sigma_daily.append(model_params.prior_sigma_no_fed)
        else:
            prior_daily.append(fed_val)
            # Parameters estimated as a best guess from limited scenarios;
            # federal OTH is a less reliable indicator of state OTH,
            # but we still need it because it includes other "minor" parties that
            # will have their prior series calculated from the federal trends.
            sigma_daily.append(
                model_params.prior_sigma_fed_oth
                if party == 'OTH FP'
                else model_params.prior_sigma_fed
            )

    return PriorSeries(prior_series_daily=prior_daily, sigma_daily=sigma_daily)


def should_use_approvals(party_context: PartyContext) -> bool:
    config = party_context.config
    party = party_context.party
    return config.use_approvals() and (party == "@TPP" or party in major_parties)


def load_approvals(party_context: PartyContext) -> List[List[str]]:
    e_data = party_context.e_data
    with open(f'Synthetic TPPs/{e_data.e_tuple[1]}.csv') as f:
        approvals = [
            line.strip().split(',')
            for line in f.readlines()
        ]
    return approvals


def filter_approvals_by_cycle(
    approvals: List[List[str]],
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    e_data = party_context.e_data
    return [
        (   #date, tpp, info weight
            pd.Timestamp(line[0]),
            float(line[2]), float(line[3])
        )
        for line in approvals if (pd.Timestamp(line[0]) >= e_data.start_date
            and pd.Timestamp(line[0]) <= e_data.end_date)
    ]


def adjust_approvals_for_party (
    approvals: List[Tuple[pd.Timestamp, float, float]],
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    e_data = party_context.e_data
    m_data = party_context.m_data
    party = party_context.party
        
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
    
    return approvals


def filter_approvals_by_poll_range(
    approvals: List[Tuple[pd.Timestamp, float, float]],
    party_context: PartyContext
) -> Tuple[List[Tuple[pd.Timestamp, float, float]], List[int]]:
    e_data = party_context.e_data

    approval_days = [(a[0] - e_data.start).days + 1 for a in approvals]
    approvals_in_range = [
        a for a, day in zip(approvals, approval_days)
        if 1 <= day <= e_data.n_days
    ]
    approval_days_in_range = [
        day for day in approval_days
        if 1 <= day <= e_data.n_days
    ]

    if len(approvals_in_range) < len(approvals):
        print(f"Skipping {len(approvals) - len(approvals_in_range)} approval entries outside poll range")

    return approvals_in_range, approval_days_in_range


@dataclass
class AppendApprovalsInputs:
    approval_days: List[int]
    approvals: List[Tuple[pd.Timestamp, float, float]]
    house_effects: HouseEffects
    model_params: ModelParams
    poll_vectors: PollVectors


def append_approvals_to_vectors(
    inputs: AppendApprovalsInputs
) -> PollVectors:
    poll_vectors = inputs.poll_vectors
    house_effects = inputs.house_effects
    approvals = inputs.approvals
    approval_days = inputs.approval_days
    model_params = inputs.model_params

    if len(approvals) > 0:
        poll_vectors.n_polls += len(approvals)
        poll_vectors.n_houses += 1
        poll_vectors.houses += ['Approvals']
        poll_vectors.pollObs += [a[1] for a in approvals]
        poll_vectors.missingObs += [0 for a in approvals]
        poll_vectors.pollHouses += [len(poll_vectors.houses) for a in approvals]
        poll_vectors.pollDays += approval_days
        # Sigma of approval rating-derived TPP will be between 3 and 5
        # depending on the weight of the approval rating
        # Even at the lowest end this is similar to a "bad" poll
        # and overwhelmed by a good poll
        poll_vectors.sigmasList += [
            max(model_params.approval_sigma_min, model_params.approval_sigma_max - a[2])
            for a in approvals
        ]
        house_effects.he_weights += [0]
        house_effects.biases += [0]


@dataclass
class ApprovalsInputs:
    house_effects: HouseEffects
    party_context: PartyContext
    poll_vectors: PollVectors

def maybe_add_approvals(inputs: ApprovalsInputs) -> PollVectors:
    party_context = inputs.party_context
    model_params = party_context.model_params
    poll_vectors = inputs.poll_vectors
    house_effects = inputs.house_effects

    # Add synthetic data (from approval ratings)
    # for TPP and major party primaries
    if should_use_approvals(party_context):
        approvals = load_approvals(party_context)
        approvals = filter_approvals_by_cycle(approvals, party_context)
        approvals = adjust_approvals_for_party(approvals, party_context)
        approvals_in_range, approval_days_in_range = \
             filter_approvals_by_poll_range(approvals, party_context)

        append_approvals_inputs = AppendApprovalsInputs(
            approval_days=approval_days_in_range,
            approvals=approvals_in_range,
            house_effects=house_effects,
            model_params=model_params,
            poll_vectors=poll_vectors
        )
        append_approvals_to_vectors(append_approvals_inputs)

    return poll_vectors


def prepare_discontinuities(party_context: PartyContext) -> List[int]:
    m_data = party_context.m_data
    e_data = party_context.e_data

    # Transform discontinuities from dates to raw numbers
    discontinuities_filtered = m_data.discontinuities[e_data.e_tuple[1]]
    discontinuities_filtered = \
        [(pd.Timestamp(date) - e_data.start).days + 1
            for date in discontinuities_filtered]

    # Remove discontinuities outside of the election period
    discontinuities_filtered = \
        [date for date in discontinuities_filtered
            if 1 <= date <= e_data.n_days]

    # Stan doesn't like zero-length arrays so put in a dummy value
    # if there are no discontinuities
    if not discontinuities_filtered:
        discontinuities_filtered.append(0)
    
    return discontinuities_filtered


@dataclass
class HouseEffectsInputs:
    df: pd.DataFrame
    party_context: PartyContext
    poll_vectors: PollVectors


def prepare_house_effects(inputs: HouseEffectsInputs) -> HouseEffects:
    df = inputs.df
    poll_vectors = inputs.poll_vectors
    e_data = inputs.party_context.e_data
    party = inputs.party_context.party
    config = inputs.party_context.config

    # Equal weights for house effects when calibrating,
    # use determined house effect weights when running forecasts
    he_weights = [
        1 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_he_weights[(x, party)] ** 2 if
        (x, party) in e_data.pollster_he_weights else 0.05
        for x in poll_vectors.houses
    ]

    # Equal weights for house effects when calibrating,
    # use determined house effect weights when running forecasts
    biases = [
        0 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_biases[(x, party)][0] if
        (x, party) in e_data.pollster_biases else 0
        for x in poll_vectors.houses
    ]

    # Print an estimate for the expected house effect sum
    # (doesn't have any impact on subsequent calculations)
    weightedBiasSum = 0
    housePollCount = [0 for a in poll_vectors.houses]
    houseWeight = [0 for a in poll_vectors.houses]
    houseList = df['House'].values.tolist()
    for poll in range(0, poll_vectors.n_polls):
        housePollCount[houseList[poll] - 1] = housePollCount[houseList[poll] - 1] + 1
    for house in range(0, poll_vectors.n_houses):
        houseWeight[house] = he_weights[house]
        weightedBiasSum += biases[house] * houseWeight[house]
    totalHouseWeight = sum(houseWeight)
    weightedBias = weightedBiasSum / totalHouseWeight
    print(f'Expected house effect sum: {weightedBias}')
    print(f'House effect weights: {houseWeight} for {poll_vectors.houses}')

    return HouseEffects(he_weights=he_weights, biases=biases)


@dataclass
class ReducedSeriesInputs:
    discontinuities_filtered: List[int]
    e_data: ElectionData
    model_params: ModelParams
    poll_vectors: PollVectors
    prior_series: PriorSeries


def build_reduced_series(inputs: ReducedSeriesInputs) -> ReducedSeries:
    model_params = inputs.model_params

    tDayCount = (inputs.e_data.n_days - 1) // model_params.tFactor + 1
    tPollDays = [(day - 1) // model_params.tFactor + 1 for day in inputs.poll_vectors.pollDays]
    tDiscontinuities = [(day - 1) // model_params.tFactor + 1 for day in inputs.discontinuities_filtered]
    if len(tDiscontinuities) == 0:
        tDiscontinuities.append(0)
    tElectionDay = inputs.e_data.election_day // model_params.tFactor
    tHouseEffectNew = model_params.houseEffectNew // model_params.tFactor
    tHouseEffectOld = model_params.houseEffectOld // model_params.tFactor
    prior_series_t = [inputs.prior_series.prior_series_daily[i * model_params.tFactor] for i in range(tDayCount)]
    prior_sigma_t = [inputs.prior_series.sigma_daily[i * model_params.tFactor] for i in range(tDayCount)]

    return ReducedSeries(
        prior_series=inputs.prior_series,
        prior_series_t=prior_series_t,
        prior_sigma_t=prior_sigma_t,
        tDayCount=tDayCount,
        tPollDays=tPollDays,
        tDiscontinuities=tDiscontinuities,
        tElectionDay=tElectionDay,
        tHouseEffectNew=tHouseEffectNew,
        tHouseEffectOld=tHouseEffectOld,
    )

def build_stan_data(run_context):
    poll_vectors = run_context.poll_vectors
    reduced_series = run_context.reduced_series
    house_effects = run_context.house_effects
    model_params = run_context.model_params

    # Prepare the data for Stan to process
    stan_data = {
        'dayCount': reduced_series.tDayCount,
        'pollCount': poll_vectors.n_polls,
        'houseCount': poll_vectors.n_houses,
        'discontinuityCount': len(reduced_series.tDiscontinuities),
        'priorResult': run_context.prior_result,
        'priorSeries': reduced_series.prior_series_t,
        'priorVoteShareSigma': reduced_series.prior_sigma_t,

        'pollObservations': poll_vectors.pollObs,
        'missingObservations': poll_vectors.missingObs,
        'pollHouse': poll_vectors.pollHouses,
        'pollDay': reduced_series.tPollDays,
        'discontinuities': reduced_series.tDiscontinuities,
        'sigmas': poll_vectors.sigmasList,
        'heWeights': house_effects.he_weights,
        'biases': house_effects.biases,

        'electionDay': reduced_series.tElectionDay,

        # distributions for the daily change in vote share
        # higher values during campaigns, since it's more likely
        # people are paying attention and changing their mind then
        'dailySigma': model_params.daily_sigma_base * math.sqrt(model_params.tFactor),
        'campaignSigma': model_params.campaign_sigma_base * math.sqrt(model_params.tFactor),
        'finalSigma': model_params.final_sigma_base * math.sqrt(model_params.tFactor),

        # prior distribution for each house effect
        # modelled as a double exponential to avoid
        # easily giving a large house effect, but
        # still giving a big one when it's really warranted
        'houseEffectSigma': model_params.house_effect_sigma,

        # prior distribution for sum of house effects
        # keep this very small, will deal with systemic bias variability
        # in the main program, so for now keep the variance of house
        # effects at approximately zero
        'houseEffectSumSigma': model_params.house_effect_sum_sigma,

        # prior distribution for each day's vote share
        # very weak prior, want to avoid pulling extreme vote shares
        # towards the center since that historically harms accuracy
        # 'priorVoteShareSigma': 200.0,

        # Bounds for the transition between old and new house effects
        'houseEffectOld': reduced_series.tHouseEffectOld,
        'houseEffectNew': reduced_series.tHouseEffectNew
    }

    print(stan_data)

    return stan_data


def verify_timeline_consistency(party_context: PartyContext):
    e_data = party_context.e_data
    expected_end = e_data.start + timedelta(days=int(e_data.n_days) - 1)
    if e_data.end != expected_end:
        raise ValueError(
            f"Inconsistent timeline: start={e_data.start} end={e_data.end} "
            f"n_days={e_data.n_days} expected_end={expected_end}"
        )


def run_stan_model(model_inputs: ModelInputs):
    e_data = model_inputs.e_data
    model_params = model_inputs.model_params

    # get the Stan model code
    with open("./Models/fp_model.stan", "r") as f:
        model = f.read()

    # encode the STAN model in C++ or retrieve it if already cached
    sm = stan_cache(model_code=model)

    # Report dates for model, this means we can easily check if new
    # data has actually been saved without waiting for model to run
    print('Beginning sampling for ' + model_inputs.party + ' ...')
    end = e_data.start + timedelta(days=int(e_data.n_days))
    print('Start date of model: ' + e_data.start.strftime('%Y-%m-%d\n'))
    print('End date of model: ' + end.strftime('%Y-%m-%d\n'))

    # Do model sampling. Time for diagnostic purposes
    start_time = perf_counter()
    fit = sm.sampling(data=model_inputs.stan_data,
                        iter=model_inputs.iterations,
                        chains=model_inputs.chains,
                        control={'max_treedepth': model_params.stan_max_treedepth,
                                'adapt_delta': model_params.stan_adapt_delta})
    finish_time = perf_counter()
    print('Time elapsed: ' + format(finish_time - start_time, '.2f')
            + ' seconds')
    print('Stan Finished ...')

    # Check technical model diagnostics
    print(pystan.diagnostics.check_hmc_diagnostics(fit))

    return fit


# Helper function to output the filename for an OutputContext
def output_filename_ctx(output_ctx, kind):
    return output_filename(OutputFilenameInputs(
        config=output_ctx.config,
        e_data=output_ctx.e_data,
        party=output_ctx.party,
        excluded_pollster=output_ctx.excluded_pollster,
        file_type=kind,
    ))


@dataclass
class WritingContext:
    output_probs_t: Tuple[float, ...]
    summary: Any

def prepare_writing(fit: Any):
    probs_list = [0.001]
    for i in range(1, 100):
        probs_list.append(i * 0.01)
    probs_list.append(0.999)
    output_probs_t = tuple(probs_list)
    summary = fit.summary(probs=output_probs_t)['summary']

    return WritingContext(
        output_probs_t=output_probs_t,
        summary=summary,
    )


@dataclass
class IterTrendDaysInputs:
    e_data: ElectionData
    run_context: RunContext
    summary: Any
    output_probs_t: Tuple[float, ...]

@dataclass
class TrendDay:
    effective_day: int
    day_infos: List[float]
    median_val: float
    table_index: int

def iter_trend_days(inputs: IterTrendDaysInputs):
    e_data = inputs.e_data
    run_context = inputs.run_context
    summary = inputs.summary
    output_probs_t = inputs.output_probs_t
    model_params = run_context.model_params
    poll_vectors = run_context.poll_vectors
    tDayCount = run_context.reduced_series.tDayCount

    offset = tDayCount + poll_vectors.n_houses * 2
    median_col = math.floor((4 + len(output_probs_t)) / 2)

    for summary_day in range(tDayCount):
        for duplicate_num in range(model_params.tFactor):
            effective_day = summary_day * model_params.tFactor + duplicate_num
            if effective_day >= e_data.n_days:
                break
            table_index = summary_day + offset

            day_infos = []
            for col in range(3, 3 + len(output_probs_t)):
                day_infos.append(summary[table_index][col])

            median_val = summary[table_index][median_col]

            yield TrendDay(
                effective_day=effective_day,
                day_infos=day_infos,
                median_val=median_val,
                table_index=table_index,
            )


@dataclass
class WriteTrendInputs:
    output_context: OutputContext
    writing_context: WritingContext

def write_trend(inputs: WriteTrendInputs):
    output_context = inputs.output_context
    writing_context = inputs.writing_context

    config = output_context.config
    e_data = output_context.e_data
    output_probs_t = writing_context.output_probs_t
    party = output_context.party
    run_context = output_context.run_context
    summary = writing_context.summary

    output_trend = output_filename_ctx(output_context, 'trend')

    # Extract trend data from model summary and write to file
    trend_file = open(output_trend, 'w')
    trend_file.write('Start date day,Month,Year\n')
    trend_file.write(e_data.start.strftime('%d,%m,%Y\n'))
    trend_file.write('Day,Party')
    for prob in output_probs_t:
        trend_file.write(',' + str(round(prob * 100)) + "%")
    trend_file.write('\n')

    day_data = []
    for day in iter_trend_days(IterTrendDaysInputs(
        e_data=e_data,
        run_context=run_context,
        summary=summary,
        output_probs_t=output_probs_t,
    )):
        to_write = f"{day.effective_day},{party}"
        to_write += "," + ",".join(str(round(v, 3)) for v in day.day_infos)
        to_write += "\n"
        if not (config.cutoff > 0 and day.effective_day < e_data.n_days - 1):
            trend_file.write(to_write)
        day_data.append(day.day_infos)

    trend_file.close()
    print('Saved trend file at ' + output_trend)
    return TrendOutputs(
        day_data=day_data
    )


@dataclass
class PrepareOthersMediansInputs:
    output_context: OutputContext
    writing_context: WritingContext

def prepare_others_medians(inputs: PrepareOthersMediansInputs):
    output_context = inputs.output_context
    writing_context = inputs.writing_context

    e_data = output_context.e_data
    party = output_context.party
    run_context = output_context.run_context
    summary = writing_context.summary
    output_probs_t = writing_context.output_probs_t

    if party in others_parties or party in ['GRN FP', 'NAT FP', 'OTH FP']:
        e_data.others_medians[party] = {}
    else:
        return

    for day in iter_trend_days(IterTrendDaysInputs(
        e_data=e_data,
        run_context=run_context,
        summary=summary,
        output_probs_t=output_probs_t,
    )):
        e_data.others_medians[party][day.effective_day] = day.median_val
        if party == 'OTH FP':
            for oth_party in e_data.others_medians.keys():
                if oth_party in others_parties:
                    e_data.others_medians[party][day.effective_day] -= \
                        e_data.others_medians[oth_party][day.effective_day]


@dataclass
class WriteHouseEffectsInputs:
    output_context: OutputContext
    party: str
    run_context: RunContext
    writing_context: WritingContext

@dataclass
class WriteHouseEffectsOutputs:
    new_house_effects: List[float]
    old_house_effects: List[float]

def write_house_effects(inputs: WriteHouseEffectsInputs):
    output_context = inputs.output_context
    e_data = output_context.e_data
    output_probs_t = inputs.writing_context.output_probs_t
    party = inputs.party
    poll_vectors = inputs.run_context.poll_vectors
    summary = inputs.writing_context.summary
    tDayCount = inputs.run_context.reduced_series.tDayCount
    
    output_house_effects = output_filename_ctx(inputs.output_context, 'house_effects')

    # Extract house effect data from model summary
    new_house_effects = []
    old_house_effects = []
    offset = tDayCount
    for house in range(0, poll_vectors.n_houses):
        new_house_effects.append(summary[offset + house, 0])
        old_house_effects.append(summary[offset + poll_vectors.n_houses + house, 0])

    # Extract house effect data from model summary and write to file
    house_effects_file = open(output_house_effects, 'w')
    house_effects_file.write('House,Party')
    for prob in output_probs_t:
        house_effects_file.write(',' + str(round(prob * 100)) + "%")
    house_effects_file.write('\n')
    house_effects_file.write('New house effects\n')
    offset = tDayCount
    for house_index in range(0, poll_vectors.n_houses):
        house_effects_file.write(poll_vectors.houses[house_index])
        table_index = offset + house_index
        house_effects_file.write("," + party)
        for col in range(3, 3+len(output_probs_t)):
            house_effects_file.write(
                ',' + str(round(summary[table_index][col], 3)))
        house_effects_file.write('\n')
    offset = tDayCount + poll_vectors.n_houses
    house_effects_file.write('Old house effects\n')
    for house_index in range(0, poll_vectors.n_houses):
        house_effects_file.write(poll_vectors.houses[house_index])
        table_index = offset + house_index
        house_effects_file.write("," + party)
        for col in range(3, 3+len(output_probs_t)):
            house_effects_file.write(
                ',' + str(round(summary[table_index][col], 3)))
        house_effects_file.write('\n')

    house_effects_file.close()
    print('Saved house effects file at ' + output_house_effects)
    
    return WriteHouseEffectsOutputs(
        new_house_effects=new_house_effects,
        old_house_effects=old_house_effects,
    )


@dataclass
class WritePollsInputs:
    df: pd.DataFrame
    output_context: OutputContext
    party: str
    run_context: RunContext
    write_house_effects_outputs: WriteHouseEffectsOutputs

def write_polls(inputs: WritePollsInputs):
    df = inputs.df
    output_context = inputs.output_context
    config = output_context.config
    e_data = output_context.e_data
    model_params = output_context.run_context.model_params
    new_house_effects = inputs.write_house_effects_outputs.new_house_effects
    old_house_effects = inputs.write_house_effects_outputs.old_house_effects
    party = inputs.party

    output_polls = output_filename_ctx(output_context, 'polls')

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
            and len(df.loc[poll_index, 'Brand']) > 0
            and not config.calibrate_pollsters and not config.calibrate_bias):
            polls_file.write(str(df.loc[poll_index, 'Brand']))
        else:
            polls_file.write(str(df.loc[poll_index, 'Firm']))
        day = int(df.loc[poll_index, 'DayNum'])
        days_ago = e_data.n_days - day
        polls_file.write(',' + str(day))
        fp = df.loc[poll_index, party]
        new_he = new_house_effects[df.loc[poll_index, 'House'] - 1]
        old_he = old_house_effects[df.loc[poll_index, 'House'] - 1]
        old_factor = ((days_ago - model_params.houseEffectNew) /
                        (model_params.houseEffectOld - model_params.houseEffectNew))
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


@dataclass
class CalibratePollstersInputs:
    df: pd.DataFrame
    excluded_pollster: str
    exc_polls: pd.DataFrame
    output_context: OutputContext
    party: str
    trend_outputs: TrendOutputs
    writing_context: WritingContext


def calibrate_pollsters(inputs: CalibratePollstersInputs) -> None:
    day_data = inputs.trend_outputs.day_data
    df = inputs.df
    e_data = inputs.output_context.e_data
    excluded_pollster = inputs.excluded_pollster
    exc_polls = inputs.exc_polls
    output_probs = inputs.writing_context.output_probs_t
    party = inputs.party
                        
    exc_poll_data = [a for a in zip(exc_polls['DayNum'], exc_polls[party],
                     exc_polls.axes[0], exc_polls['Firm'])]
    if len(exc_poll_data) <= 1: return
    print(f'Trend closeness statistics for {excluded_pollster}')
    median_col = output_probs.index(0.5)
    diff_sum = {}
    pollster_count = {}
    house_effects = {}
    for a in exc_poll_data:
        day, vote, pollster = int(a[0]) - 1, a[1], a[3]
        trend_value = day_data[day][median_col]
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
        day, vote, poll_index, pollster = int(a[0]) - 1, a[1], a[2], a[3]
        trend_median = day_data[day][median_col]
        eff_house_effect = house_effects[pollster]
        adj_poll = vote - eff_house_effect
        # for the case where the poll is higher than any
        # probability threshold, have this as the default value
        percentile = 0.999 
        for index, upper_prob in enumerate(output_probs):
            upper_value = day_data[day][index]
            if adj_poll < upper_value:
                if index == 0:
                    percentile = 0.001
                else:
                    lower_value = day_data[day][index - 1]
                    lower_prob = output_probs[index - 1]
                    lerp = ((adj_poll - lower_value) /
                            (upper_value - lower_value))
                    percentile = (lower_prob + lerp * 
                                (upper_prob - lower_prob))
                break
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


def write_outputs(output_context: OutputContext, fit):
    run_context = output_context.run_context
    party = output_context.party
    config = output_context.config
    excluded_pollster = output_context.excluded_pollster
    df = output_context.poll_prep_result.df
    exc_polls = output_context.poll_prep_result.exc_polls
    
    writing_context = prepare_writing(fit)

    trend_outputs = write_trend(WriteTrendInputs(
        output_context=output_context,
        writing_context=writing_context,
    ))

    prepare_others_medians(PrepareOthersMediansInputs(
        output_context=output_context,
        writing_context=writing_context,
    ))
    
    house_effects_outputs = write_house_effects(WriteHouseEffectsInputs(
        output_context=output_context,
        party=party,
        run_context=run_context,
        writing_context=writing_context,
    ))

    write_polls(WritePollsInputs(
        df=df,
        output_context=output_context,
        party=party,
        run_context=run_context,
        write_house_effects_outputs=house_effects_outputs,
    ))
    
    if config.calibrate_pollsters:
        calibrate_pollsters(CalibratePollstersInputs(
            df=df,
            excluded_pollster=excluded_pollster,
            exc_polls=exc_polls,
            output_context=output_context,
            party=party,
            trend_outputs=trend_outputs,
            writing_context=writing_context,
        ))


@dataclass
class RunPartyInputs:
    config: Config
    e_data: ElectionData
    excluded_pollster: str
    m_data: ModellingData
    model_params: ModelParams
    party: str

def run_party(inputs: RunPartyInputs) -> None:
    config = inputs.config
    e_data = inputs.e_data
    excluded_pollster = inputs.excluded_pollster
    m_data = inputs.m_data
    model_params = inputs.model_params
    party = inputs.party
    
    if excluded_pollster != '':
        print(f'Excluding pollster: {excluded_pollster}')
    else:
        print('Not excluding any pollsters.')

    party_context = PartyContext(
        config=config,
        m_data=m_data,
        e_data=e_data,
        party=party,
        excluded_pollster=excluded_pollster,
        model_params=model_params,
    )

    poll_prep_result = prepare_poll_df(party_context)

    if poll_prep_result is None:
        return

    prior_result = get_prior_result(party_context)

    # Note "df" is mutated in place by build_poll_vectors
    poll_vector_inputs = PollVectorInputs(
        df=poll_prep_result.df,
        party_context=party_context,
        prior_result=prior_result,
    )

    poll_vectors = build_poll_vectors(poll_vector_inputs)
  
    prior_series = build_prior_series(party_context, prior_result)

    house_effects_inputs = HouseEffectsInputs(
        party_context=party_context,
        poll_vectors=poll_vectors,
        df=poll_prep_result.df,
    )

    house_effects = prepare_house_effects(house_effects_inputs)

    approvals_inputs = ApprovalsInputs(
        party_context=party_context,
        poll_vectors=poll_vectors,
        house_effects=house_effects,
    )

    poll_vectors = maybe_add_approvals(approvals_inputs)

    discontinuities_filtered = prepare_discontinuities(party_context)

    reduced_series_inputs = ReducedSeriesInputs(
        e_data=e_data,
        model_params=model_params,
        poll_vectors=poll_vectors,
        discontinuities_filtered=discontinuities_filtered,
        prior_series=prior_series,
    )

    reduced_series = build_reduced_series(reduced_series_inputs)

    run_context = RunContext(
        poll_vectors=poll_vectors,
        reduced_series=reduced_series,
        house_effects=house_effects,
        prior_result=prior_result,
        model_params=model_params,
    )

    stan_data = build_stan_data(run_context)

    model_inputs = ModelInputs(
        stan_data=stan_data,
        iterations=m_data.desired_iterations[e_data.e_tuple],
        chains=15,
        party=party,
        e_data=e_data,
        model_params=model_params,
    )

    verify_timeline_consistency(party_context)

    fit = run_stan_model(model_inputs)
    
    output_context = OutputContext(
        e_data=e_data,
        party=party,
        config=config,
        excluded_pollster=excluded_pollster,
        poll_prep_result=poll_prep_result,
        run_context=run_context,
    )

    write_outputs(output_context, fit)


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


def check_suspension():
    message_seen = False
    while True:
        if not os.path.exists(os.path.join(os.getcwd(), f'suspend.txt')): break
        with open(f'suspend.txt', 'r') as f:
            a = f.read()
            if a != '1':
                break
        if not message_seen:
            message_seen = True
            print('Suspended, waiting for resume')
        time.sleep(60)


def build_config() -> Config:
    try:
        return Config()
    except ConfigError as e:
        with open(f'itsdone.txt', 'w') as f:
            f.write('2')
        raise e


def build_model_params() -> ModelParams:
    model_params = ModelParams()
    model_params.validate()
    return model_params


def maybe_generate_approvals(config: Config) -> None:
    if config.use_approvals():
        generate_synthetic_tpps()


def build_election_data(inputs: ElectionDataInputs) -> Optional[ElectionData]:
    e_data = ElectionData(ElectionDataInputs(
        config=inputs.config,
        m_data=inputs.m_data,
        desired_election=inputs.desired_election,
    ))

    if len(e_data.base_df) == 0:
        print(f'No polls for election {inputs.desired_election.short()} in the requested time range, skipping')
        return None

    return e_data


@dataclass
class ShouldSkipPollsterCalibrationInputs:
    config: Config
    e_data: ElectionData
    excluded_pollster: str

def should_skip_pollster_calibration(inputs: ShouldSkipPollsterCalibrationInputs) -> bool:
    return (
        inputs.config.calibrate_pollsters
        and inputs.excluded_pollster == ''
        and len(inputs.e_data.poll_calibrations) == 0
    )


@dataclass
class ShouldSkipPartyOutputInputs:
    config: Config
    desired_election: ElectionCode
    e_data: ElectionData
    excluded_pollster: str
    party: str

def should_skip_party_output(inputs: ShouldSkipPartyOutputInputs) -> bool:
    party = inputs.party
    # Avoid unnecessary duplication of effort for cutoffs that would be identical
    if inputs.config.cutoff > 0:
        trend_filename = output_filename(OutputFilenameInputs(
            config=inputs.config,
            e_data=inputs.e_data,
            excluded_pollster=inputs.excluded_pollster,
            file_type='trend',
            party=party,
        ))
        print(trend_filename)

        if os.path.exists(trend_filename):
            print(f'Trend file for {party} in election {inputs.desired_election.short()} already exists, skipping')
            return True
    return False


@dataclass
class MaybeCreateTppSeriesInputs:
    desired_election: ElectionCode
    e_data: ElectionData
    m_data: ModellingData
    party: str

def maybe_create_tpp_series(inputs: MaybeCreateTppSeriesInputs) -> None:
    if inputs.party == "@TPP" or inputs.party == "OTH FP":
        inputs.e_data.create_tpp_series(
            ElectionData.CreateTppSeriesInputs(
                m_data=inputs.m_data,
                desired_election=inputs.desired_election,
                df=inputs.e_data.base_df
            )
        )


def run_models() -> None:
    # check version information
    print('Python version: {}'.format(sys.version))
    print('pystan version: {}'.format(pystan.__version__))

    try:
        config = build_config()

        model_params = build_model_params()

        maybe_generate_approvals(config)

        m_data = ModellingData()

        for desired_election in config.elections:
            e_data = build_election_data(ElectionDataInputs(
                config=config,
                m_data=m_data,
                desired_election=desired_election,
            ))
            if e_data is None:
                continue

            for excluded_pollster in e_data.pollster_exclusions:
                # Don't waste time calculating the no-pollster-excluded trend
                # if there are no pollster-excluded trends to compare it to
                # (and that is the only purpose for which it is calculated)
                if should_skip_pollster_calibration(ShouldSkipPollsterCalibrationInputs(
                    config=config,
                    e_data=e_data,
                    excluded_pollster=excluded_pollster,
                )):
                    continue

                for party in m_data.parties[e_data.e_tuple]:
                    
                    if should_skip_party_output(ShouldSkipPartyOutputInputs(
                        config=config,
                        desired_election=desired_election,
                        e_data=e_data,
                        excluded_pollster=excluded_pollster,
                        party=party,
                    )):
                        continue

                    if not config.priority:
                        check_suspension()

                    # This has to be done here because it updates the TPP based on
                    # others_medians, allowing the estimation of the size of
                    # minor parties that some pollsters don't report
                    maybe_create_tpp_series(MaybeCreateTppSeriesInputs(
                        desired_election=desired_election,
                        e_data=e_data,
                        m_data=m_data,
                        party=party,
                    ))

                    run_party(RunPartyInputs(
                        config=config,
                        e_data=e_data,
                        excluded_pollster=excluded_pollster,
                        m_data=m_data,
                        model_params=model_params,
                        party=party,
                    ))

            if config.calibrate_pollsters:
                finalise_calibrations(e_data=e_data)

    # indicate completion (delete these lines if not the original author)
    except Exception as e:
        with open(f'itsdone.txt', 'w') as f:
            f.write('2')
        raise
    
    with open(f'itsdone.txt', 'w') as f:
        f.write('1')


if __name__ == '__main__':
    run_models()
