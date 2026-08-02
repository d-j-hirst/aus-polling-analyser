import argparse
import calibration_provenance
import calibration_summary
import calibration_summary_provenance
import csv
import datetime
import fp_model_provenance
import math
import numpy as np
import os
import pandas as pd
import pystan
import secrets
import sys
import statistics
import time
from approvals import generate_synthetic_tpps
from dataclasses import dataclass
from datetime import timedelta
from election_code import ElectionCode
from time import perf_counter
from typing import Any, Dict, List, Optional, Tuple

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

unnamed_others_base = 3.0
unnamed_others_diagnostic_threshold = 1.0
unnamed_others_diagnostic_limit = 10


class ConfigError(ValueError):
    pass


def derive_unnamed_others_median(inclusive_others, named_minor_total):
    """Return a positive residual without discarding named-party evidence.

    Separate fits can briefly put named minor parties above inclusive Others,
    particularly when a newly reported party moves sharply. This mirrors the
    established trend-adjustment and C++ treatment: retain ordinary subtraction
    when there is at least a three-point residual, otherwise reduce the
    inclusive-Others distribution proportionally. The result is always between
    zero and the original inclusive-Others median.
    """

    if (
        not math.isfinite(inclusive_others)
        or not math.isfinite(named_minor_total)
        or inclusive_others < 0
        or named_minor_total < 0
    ):
        raise ConfigError(
            'Cannot derive unnamed Others from invalid medians: '
            'inclusive={}, named={}.'.format(
                inclusive_others, named_minor_total
            )
        )

    denominator = max(
        inclusive_others,
        named_minor_total + unnamed_others_base,
    )
    unnamed_others = (
        inclusive_others
        * (1.0 - named_minor_total / denominator)
    )
    # Guard against insignificant floating-point excursions at the bounds.
    return min(inclusive_others, max(0.0, unnamed_others))


class UnnamedOthersDiagnosticsRecorder:
    """Keep a bounded summary of materially low raw xOTH estimates."""

    def __init__(
        self,
        threshold=unnamed_others_diagnostic_threshold,
        example_limit=unnamed_others_diagnostic_limit,
    ):
        self.threshold = threshold
        self.example_limit = example_limit
        self.issue_count = 0
        self.examples = []

    def record(
        self,
        election,
        mode,
        day,
        inclusive_others,
        named_minor_total,
        adjusted_unnamed_others,
    ):
        raw_unnamed_others = inclusive_others - named_minor_total
        if raw_unnamed_others >= self.threshold:
            return

        self.issue_count += 1
        self.examples.append((
            raw_unnamed_others,
            election,
            mode,
            day,
            inclusive_others,
            named_minor_total,
            adjusted_unnamed_others,
        ))
        self.examples.sort(key=lambda example: example[0])
        del self.examples[self.example_limit:]

    def report(self, completed=True):
        if not self.issue_count:
            return

        status = (
            'Batch completed'
            if completed
            else 'Batch terminated before completion'
        )
        print(
            '{} with {} raw unnamed-Others median estimate(s) below '
            '{:.1f}%. Lowest {}:'.format(
                status,
                self.issue_count,
                self.threshold,
                len(self.examples),
            )
        )
        for (
            raw_unnamed_others,
            election,
            mode,
            day,
            inclusive_others,
            named_minor_total,
            adjusted_unnamed_others,
        ) in self.examples:
            print(
                '  {} | {} | trend day {} | raw xOTH {:+.3f}% '
                '(OTH {:.3f}% - named {:.3f}%); adjusted to {:.3f}%'
                .format(
                    election,
                    mode,
                    day,
                    raw_unnamed_others,
                    inclusive_others,
                    named_minor_total,
                    adjusted_unnamed_others,
                )
            )


def order_parties_for_model(parties):
    """Return the dependency order required by the sequential party fits."""

    if len(parties) != len(set(parties)):
        raise ConfigError(
            'Significant-party configuration contains duplicate parties: {}'
            .format(', '.join(parties))
        )

    median_inputs = set(others_parties + ['GRN FP', 'NAT FP'])

    def dependency_rank(party):
        if party in median_inputs:
            return 0
        if party == 'OTH FP':
            return 1
        if party in major_parties:
            return 2
        if party == '@TPP':
            return 3
        return 0

    # sorted() is stable, preserving the configured order within each stage.
    return sorted(parties, key=dependency_rank)


def load_election_cycles(path='./Data/election-cycles.csv'):
    with open(path, 'r') as source:
        return {
            (row[0], row[1]): (
                pd.Timestamp(row[2]),
                pd.Timestamp(row[3]),
            )
            for row in csv.reader(source)
            if row
        }


def overlapping_federal_elections(election, election_cycles):
    """Return federal terms whose configured periods overlap an election."""

    if election.region() == 'fed':
        return []

    election_key = (str(election.year()), election.region())
    if election_key not in election_cycles:
        return []
    election_start, election_end = election_cycles[election_key]
    federal_elections = [
        ElectionCode(year, region)
        for (year, region), (start, end) in election_cycles.items()
        if (
            region == 'fed'
            and start <= election_end
            and election_start <= end
        )
    ]
    federal_elections.sort(
        key=lambda code: (
            election_cycles[(str(code.year()), code.region())][0],
            code.year(),
        )
    )
    return federal_elections


def order_elections_by_federal_dependencies(
    elections,
    election_cycles,
    assumed_complete=(),
):
    """Stably place selected federal prerequisites before state elections."""

    selected = {
        (str(election.year()), election.region()): election
        for election in elections
    }
    assumed_complete_keys = {
        (str(election.year()), election.region())
        for election in assumed_complete
    }
    ordered = []
    emitted = set()

    def emit(election):
        key = (str(election.year()), election.region())
        if key in emitted or key in assumed_complete_keys:
            return
        for dependency in overlapping_federal_elections(
            election, election_cycles
        ):
            dependency_key = (
                str(dependency.year()),
                dependency.region(),
            )
            if dependency_key in selected:
                emit(selected[dependency_key])
        emitted.add(key)
        ordered.append(election)

    for election in elections:
        emit(election)
    return ordered


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
        parser.add_argument(
            '--calibration-traces',
            action='store_true',
            help=(
                'Retain detailed calibration trend, poll and house-effect '
                'CSVs under a run-specific diagnostics directory. Compact '
                'summaries are always written for completed calibration.'
            ),
        )
        parser.add_argument(
            '--cutoff',
            action='store_true',
            help=(
                'Generate every historical point-in-time fit in the cutoff '
                'schedule used by trend_adjust.py.'
            ),
        )
        parser.add_argument('--pure', action='store_true',
                            help="Only use primary voting intention results, "
                            'not approval ratings, TPP-only polls or other '
                            'measures. Outputs with _pure suffix.', 
                            default=0)
        parser.add_argument('--priority', action='store_true',
                            help="Never suspend this model.",
                            default=0)
        parser.add_argument(
            '--seed',
            action='store',
            type=int,
            help=(
                'Base random seed. Calibration derives a separate stable '
                'Stan seed for each election, party and excluded pollster.'
            ),
        )
        args = parser.parse_args()
        if args.election is None:
            raise ConfigError('The --election argument is required.')
        self.election_instructions = args.election.lower()
        self.calibrate_pollsters = args.calibrate
        self.calibrate_bias = (not self.calibrate_pollsters and 
                               args.bias)
        self.calibration_traces = args.calibration_traces
        self.calibration_trace_directory = None
        self.cutoff_mode = args.cutoff
        self.cutoff_days = 0
        self.pure = args.pure
        self.priority = args.priority
        self.unnamed_others_diagnostics = (
            UnnamedOthersDiagnosticsRecorder()
        )
        if self.cutoff_mode and (
            self.calibrate_pollsters or self.calibrate_bias or self.pure
        ):
            raise ConfigError(
                '--cutoff cannot be combined with --calibrate, --bias or '
                '--pure.'
            )
        if self.calibration_traces and not (
            self.calibrate_pollsters or self.calibrate_bias
        ):
            raise ConfigError(
                '--calibration-traces requires --calibrate or --bias.'
            )
        if args.seed is not None and not 1 <= args.seed < 2 ** 31:
            raise ConfigError('The --seed value must be between 1 and 2^31-1.')
        self.seed = args.seed
        self.synthetic_tpps_by_region = None
        self.prepare_election_list()

    def prepare_election_list(self):
        with open('./Data/polled-elections.csv', 'r') as f:
            completed_elections = ElectionCode.load_elections_from_file(f)
        election_cycles = load_election_cycles()

        # Cutoffs calibrate historical forecasts against known results. Never
        # extend cutoff batches into the separately configured future cycles.
        elections = list(completed_elections)
        if not self.cutoff_mode:
            with open('./Data/future-elections.csv', 'r') as f:
                elections += ElectionCode.load_elections_from_file(f)
        if self.election_instructions == 'all':
            selected_elections = elections
            assumed_complete = []
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
            if self.cutoff_mode and code not in completed_elections:
                raise ConfigError(
                    'Cutoff generation only supports completed elections '
                    'listed in Data/polled-elections.csv.'
                )
            if code not in elections:
                raise ConfigError('Error in "elections" argument: '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv')
            if len(parts) == 2:
                selected_elections = [code]
                assumed_complete = []
            elif parts[2] == 'onwards':
                try:
                    selected_elections = elections[elections.index(code):]
                except ValueError:
                    raise ConfigError('Error in "elections" argument: '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv')
                # Starting from a state election means its federal priors are
                # already available. Do not pull those federal terms back into
                # the batch merely because their election dates occur later.
                assumed_complete = overlapping_federal_elections(
                    code, election_cycles
                )
                selected_elections = [
                    election
                    for election in selected_elections
                    if election not in assumed_complete
                ]
            else:
                raise ConfigError('Invalid instruction in "elections"'
                                  'argument.')

        self.elections = order_elections_by_federal_dependencies(
            selected_elections,
            election_cycles,
            assumed_complete,
        )

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
                (a[0], a[1]): order_parties_for_model(a[2:]) for a in
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
        self.election_cycles = load_election_cycles()


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
                    pd.to_timedelta(config.cutoff_days, unit="D"))
        self.base_df = self.base_df[self.base_df['MidDate'] >= self.start_date]
        self.base_df = self.base_df[self.base_df['MidDate'] <= self.end_date]
        if self.base_df.empty:
            return

        # convert dates to days from start
        # do this before removing polls with N/A values so that
        # start times are consistent amongst series
        # (otherwise, a poll missing some parties, or with only approval ratings,
        # could cause inconsistent date indexing)
        self.start = self.base_df['MidDate'].min()  # day zero
        self.end = self.base_df['MidDate'].max()
        # federal trend medians for selected minor parties
        self.fed_trends = {}
        self.federal_prior_files = []
        self.federal_prior_files_by_party = {}

        # Federal trends provide date-aligned minor-party priors for state
        # elections. Federal elections must not consume federal trend outputs:
        # doing so creates either self-feedback or a dependency on a prior
        # election cycle rather than an independent fit of the federal polls.
        fed_cycles = select_overlapping_fed_cycles(
            SelectOverlappingFedCyclesInputs(
                m_data=m_data,
                election=self.e_tuple,
            )
        )

        if fed_cycles:
            federal_years = ', '.join(cycle[0] for cycle in fed_cycles)
            print(
                f'Using federal cycles for model prior: {federal_years}'
            )
        else:
            print('No federal cycles found for model prior')

        fed_minor_parties = others_parties + ['GRN FP', 'OTH FP']
        state_significant_parties = set(m_data.parties[tup])
        for party in fed_minor_parties:
            if (
                party in major_parties
                or party in ['@TPP']
                or party not in state_significant_parties
            ):
                continue
            party_fed_cycles = [
                cycle
                for cycle in fed_cycles
                if party in m_data.parties.get(
                    (str(cycle[0]), 'fed'), []
                )
            ]
            series = load_fed_trend_series_for_party(
                LoadFedTrendSeriesForPartyInputs(
                    available_through=(
                        self.end if config.cutoff_mode else None
                    ),
                    fed_cycles=party_fed_cycles,
                    party=party,
                    pure=config.pure,
                    used_files=self.federal_prior_files_by_party.setdefault(
                        party, []
                    ),
                )
            )
            if series is not None:
                self.fed_trends[party] = series
            self.federal_prior_files.extend(
                self.federal_prior_files_by_party[party]
            )

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
        if config.calibrate_bias:
            self.calibration_bias_records = {}

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
        # Reconstruct TPP series from the first preference votes for each poll
        # This, not the "reported" TPP, is used to calculate the TPP trend
        m_data = inputs.m_data
        desired_election = inputs.desired_election
        df = inputs.df

        # Estimate missing minor-party contributions
        # This ensures that the OTH FP column is consistent across all polls
        # When a pollster doesn't report a minor party, we need to estimate their contribution
        # from the minor party's poll trend and record the adjustment that needs
        # to be made to the TPP series so that the preference flows represent the
        # likely breakdown of the OTH vote.
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

        # This "Total" column is used to handle exhaust in OPV elections
        # where not all votes contribute to the TPP due to exhaustion of preferences
        # We now add up the total votes expected to reach ALP, and the total expected
        # to reach either "major".
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
        # Adjust the TPP series to account for the estimated missing minor-party contributions
        df['@TPP'] += adjustment_series
        # Convert the total votes to a percentage of the total votes (for OPV)
        df['@TPP'] /= (df['Total'] * 0.01)
        if desired_election.region() == 'fed':
            df['@TPP'] += 0.1  # small adjustment for leakage in LIB/NAT seats
        
        # This order is important: this overwrites the OTH FP column, and we
        # do not want to overwrite the OTH FP column until after the TPP has been calculated
        self.combine_others_parties()

    def combine_others_parties(self):
        # push misc parties into Others, as explained above
        # The OTH FP column now should include the vote share of (non-Greens) "minor" parties
        # This ensures that the meaning of the OTH FP value is consistent across all polls
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


# Configuration for the model, centralised for easier adjustment
@dataclass
class ModelParams:
    # Prior construction
    min_observation: float = 0.01
    prior_min_result: float = 0.25
    # Really large values: these should only give the prior a "center of gravity",
    # not strongly pull the trend toward it
    prior_sigma_no_fed: float = 48.0
    prior_sigma_fed: float = 16.0
    prior_sigma_fed_oth: float = 32.0
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
class SelectOverlappingFedCyclesInputs:
    m_data: ModellingData
    election: Tuple[str, str]


def select_overlapping_fed_cycles(inputs: SelectOverlappingFedCyclesInputs):
    """Return federal cycles overlapping a state election cycle."""

    m_data = inputs.m_data
    election = inputs.election
    federal_elections = overlapping_federal_elections(
        ElectionCode(election[0], election[1]),
        m_data.election_cycles,
    )
    return [
        (
            str(federal_election.year()),
            *m_data.election_cycles[
                (str(federal_election.year()), 'fed')
            ],
        )
        for federal_election in federal_elections
    ]


@dataclass
class LoadFedTrendMedianInputs:
    available_through: Optional[pd.Timestamp]
    election_end: Optional[pd.Timestamp]
    election_year: int
    party: str
    pure: bool
    used_files: list


def load_fed_cutoff_median(inputs: LoadFedTrendMedianInputs):
    """Load the latest federal cutoff available by the state endpoint."""

    filename = fp_model_provenance.cutoff_output_path(
        '{}fed'.format(inputs.election_year)
    )
    if not filename.is_file():
        return None

    selected = None
    with filename.open(newline='', encoding='utf-8-sig') as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or '50%' not in reader.fieldnames:
            raise ConfigError(
                'Federal cutoff file has no median column: {}'.format(
                    filename
                )
            )
        for line_number, row in enumerate(reader, start=2):
            if row.get('Party') != inputs.party:
                continue
            try:
                endpoint_date = (
                    inputs.election_end
                    - pd.to_timedelta(
                        int(row['PollTrendEndDays']),
                        unit='D',
                    )
                )
                median = float(row['50%'])
                if not math.isfinite(median):
                    raise ValueError('non-finite median')
            except (TypeError, ValueError) as error:
                raise ConfigError(
                    '{}:{} has invalid federal cutoff data for {}'.format(
                        filename,
                        line_number,
                        inputs.party,
                    )
                ) from error
            if (
                endpoint_date <= inputs.available_through
                and (
                    selected is None
                    or endpoint_date > selected[0]
                )
            ):
                selected = (endpoint_date, median)

    if selected is None:
        return None
    inputs.used_files.append(str(filename))
    return pd.Series([selected[1]], index=[selected[0]])


def load_fed_trend_median(inputs: LoadFedTrendMedianInputs):
    election_year = inputs.election_year
    party = inputs.party

    if inputs.available_through is not None:
        return load_fed_cutoff_median(inputs)

    pure_suffix = '_pure' if inputs.pure else ''
    filename = (
        f'./Outputs/fp_trend_{election_year}fed_{party}'
        f'{pure_suffix}.csv'
    )
    if not os.path.exists(filename):
        return None
    inputs.used_files.append(filename)

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
    available_through: Optional[pd.Timestamp]
    fed_cycles: List[Tuple[int, pd.Timestamp, pd.Timestamp]]
    party: str
    pure: bool
    used_files: list

def load_fed_trend_series_for_party(inputs: LoadFedTrendSeriesForPartyInputs):
    fed_cycles = inputs.fed_cycles
    party = inputs.party

    combined = None

    for year, start, end in fed_cycles:
        series = load_fed_trend_median(LoadFedTrendMedianInputs(
            available_through=inputs.available_through,
            election_end=end,
            election_year=year,
            party=party,
            pure=inputs.pure,
            used_files=inputs.used_files,
        ))
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
    if config.calibrate_pollsters or config.calibrate_bias:
        if not config.calibration_traces:
            raise ConfigError(
                'Detailed calibration files require --calibration-traces.'
            )
        folder = config.calibration_trace_directory.rstrip('/') + '/'
    else:
        folder = './Outputs/'
    if config.cutoff_mode:
        raise ConfigError(
            'Cutoff mode writes through CutoffOutputStore, not legacy '
            'per-party output filenames.'
        )
    pure_append = f'_pure' if config.pure else ''

    return (
        f'{folder}fp_{file_type}_{e_tag}_{party}{pollster_append}'
        f'{pure_append}.csv'
    )


@dataclass
class RunContext:
    house_effects: HouseEffects
    model_params: ModelParams
    poll_vectors: PollVectors
    prior_result: float
    reduced_series: ReducedSeries


class StanDiagnosticsRecorder:
    """Accumulate non-fatal HMC diagnostic failures for one batch."""

    EXPECTED_CHECKS = {
        'n_eff',
        'Rhat',
        'divergence',
        'treedepth',
        'energy',
    }

    def __init__(self, path='./Outputs/fp_model_diagnostics.log'):
        self.path = path
        self.model_count = 0
        self.issue_counts = {}
        self.issue_count = 0
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as output:
            output.write(
                'Stan diagnostic failures for the current fp_model batch\n'
            )

    def record(
        self,
        election,
        party,
        excluded_pollster,
        random_seed,
        checks,
    ):
        self.model_count += 1
        failed = {
            check
            for check, passed in checks.items()
            if not passed
        }
        failed.update(
            'unavailable:{}'.format(check)
            for check in self.EXPECTED_CHECKS - set(checks)
        )
        if not failed:
            return

        self.issue_count += 1
        for check in failed:
            self.issue_counts[check] = (
                self.issue_counts.get(check, 0) + 1
            )
        pollster = excluded_pollster or '<none>'
        with open(self.path, 'a') as output:
            output.write(
                '{} | {} | excluded pollster {} | seed {} | failed: {}\n'
                .format(
                    election,
                    party,
                    pollster,
                    random_seed,
                    ', '.join(sorted(failed)),
                )
            )

    def report(self, completed=True):
        if self.issue_counts:
            counts = ', '.join(
                '{}={}'.format(check, count)
                for check, count in sorted(self.issue_counts.items())
            )
            summary = (
                '{} of {} Stan models had diagnostic problems ({}). '
                'See {}.'
                .format(
                    self.issue_count,
                    self.model_count,
                    counts,
                    self.path,
                )
            )
        else:
            summary = (
                'All {} completed Stan models passed the available HMC '
                'diagnostic checks.'.format(self.model_count)
            )
        if not completed:
            summary = 'Batch terminated before completion. ' + summary
        with open(self.path, 'a') as output:
            output.write(summary + '\n')
        print(summary)


@dataclass
class ModelInputs:
    chains: int
    diagnostics_recorder: StanDiagnosticsRecorder
    e_data: ElectionData
    excluded_pollster: str
    iterations: int
    model_params: ModelParams
    party: str
    random_seed: int
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
    random_seed: int
    run_context: RunContext


@dataclass
class TrendOutputs:
    day_data: List[List[float]]
    final_median: float


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
    # When federal polls for a minor party are rapidly changing but state polls are
    # sparse/nonexistent/unreliable, we want to use the federal trends to establish a prior
    # rather than the one derived from the previous election. This function creates a
    # series for each day in the state period, using the greater of the federal trend
    # and the expected value based on the state prior result.
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
        # Maximum of federal trend (if available) and state prior result is used
        # A low federal trend shouldn't drag down a party that was strong in a state
        # and a low state trend is often a result of sparse polling for that particular party
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
    # We only use (government leader) approvals for the TPP and major parties
    # there's no useful connection between the leader ratings and minor parties' vote shares
    return config.use_approvals() and (party == "@TPP" or party in major_parties)


def load_approvals(
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    config = party_context.config
    e_data = party_context.e_data
    if config.synthetic_tpps_by_region is not None:
        return [
            (pd.Timestamp(date), float(tpp), float(weight))
            for date, pollster, tpp, weight
            in config.synthetic_tpps_by_region.get(e_data.e_tuple[1], ())
        ]
    with open(f'Synthetic TPPs/{e_data.e_tuple[1]}.csv') as f:
        return [
            (pd.Timestamp(line[0]), float(line[2]), float(line[3]))
            for line in (
                row.strip().split(',')
                for row in f.readlines()
            )
        ]


def filter_approvals_by_cycle(
    approvals: List[Tuple[pd.Timestamp, float, float]],
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    e_data = party_context.e_data
    return [
        approval
        for approval in approvals
        if (
            approval[0] >= e_data.start_date
            # Use the actual final voting-intention poll date, not merely the
            # configured election/cutoff boundary. Approval observations must
            # not extend either a cutoff or an ordinary trend into the future.
            and approval[0] <= e_data.end
            and approval[2] > 0
        )
    ]


def adjust_approvals_for_party (
    approvals: List[Tuple[pd.Timestamp, float, float]],
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    e_data = party_context.e_data
    m_data = party_context.m_data
    party = party_context.party
    
    # We previously converted the approval rating to a TPP estimate
    # Now we need to calculate how that converts to FP for the "major" parties

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
        skipped_approvals = len(approvals) - len(approvals_in_range)
        print(
            f'Skipping {skipped_approvals} approval entries outside '
            'the poll range'
        )

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
        # Load the synthetic TPPs from the CSV file
        approvals = load_approvals(party_context)
        # Filter the approvals to only include those within the cycle of the election
        approvals = filter_approvals_by_cycle(approvals, party_context)
        # Make sure that the approvals are all within the range of days that have polls
        # before indexing any already-generated minor-party trend. This is
        # particularly important for cutoff runs, whose trend is truncated.
        approvals_in_range, approval_days_in_range = \
             filter_approvals_by_poll_range(approvals, party_context)
        # Create the FP series from the approvals, if necessary
        approvals_in_range = adjust_approvals_for_party(
            approvals_in_range,
            party_context,
        )

        # Append the approvals to the poll vectors
        # so that they act as (low-impact) polls in the model
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


def build_house_effect_weights(inputs: HouseEffectsInputs) -> List[float]:
    poll_vectors = inputs.poll_vectors
    e_data = inputs.party_context.e_data
    party = inputs.party_context.party
    config = inputs.party_context.config

    # Equal weights for house effects when calibrating,
    # use house effect weights when running forecasts
    # that have been determined by the pollster calibration process
    return [
        1 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_he_weights[(x, party)] ** 2 if
        (x, party) in e_data.pollster_he_weights else 0.05
        for x in poll_vectors.houses
    ]


def build_house_effect_biases(inputs: HouseEffectsInputs) -> List[float]:
    poll_vectors = inputs.poll_vectors
    e_data = inputs.party_context.e_data
    party = inputs.party_context.party
    config = inputs.party_context.config

    return [
        0 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_biases[(x, party)][0] if
        (x, party) in e_data.pollster_biases else 0
        for x in poll_vectors.houses
    ]


@dataclass
class LogExpectedHouseEffectSumInputs:
    inputs: HouseEffectsInputs
    he_weights: List[float]
    biases: List[float]

def log_expected_house_effect_sum(inputs: LogExpectedHouseEffectSumInputs) -> float:
    biases = inputs.biases
    he_weights = inputs.he_weights
    df = inputs.inputs.df
    poll_vectors = inputs.inputs.poll_vectors

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


def prepare_house_effects(inputs: HouseEffectsInputs) -> HouseEffects:
    df = inputs.df
    poll_vectors = inputs.poll_vectors
    e_data = inputs.party_context.e_data
    party = inputs.party_context.party
    config = inputs.party_context.config

    he_weights = build_house_effect_weights(inputs)

    # No biases when calibrating, use biases when running forecasts
    # that are determined by the pollster calibration process
    biases = build_house_effect_biases(inputs)

    # Print an estimate for the expected house effect sum
    # (this whole section doesn't have any impact on subsequent calculations)
    log_expected_house_effect_sum(LogExpectedHouseEffectSumInputs(
        inputs=inputs,
        he_weights=he_weights,
        biases=biases
    ))

    return HouseEffects(he_weights=he_weights, biases=biases)


@dataclass
class ReducedSeriesInputs:
    discontinuities_filtered: List[int]
    e_data: ElectionData
    model_params: ModelParams
    poll_vectors: PollVectors
    prior_series: PriorSeries


def build_reduced_series(inputs: ReducedSeriesInputs) -> ReducedSeries:
    # To save on computation, we only compute the Bayesian aggregation every tFactor days
    # This function adjusts the previously prepared data to reflect this
    # Each series starts at 1 as Stan prefers 1-based indexing
    model_params = inputs.model_params

    # For these first series, we do the (n - 1) // tFactor + 1 calculations
    # because it's important that they aren't ever zero
    # For the other series, we can just use n // tFactor as they won't ever by near zero
    # being sometimes off by one is not a problem since the poll dates are fuzzier in the first place

    # Calculate the number of days in the reduced series
    tDayCount = (inputs.e_data.n_days - 1) // model_params.tFactor + 1
    # Calculate the (reduced) day indices for the polls
    tPollDays = [(day - 1) // model_params.tFactor + 1 for day in inputs.poll_vectors.pollDays]
    # Calculate the (reduced) day indices for the discontinuities
    tDiscontinuities = [(day - 1) // model_params.tFactor + 1 for day in inputs.discontinuities_filtered]
    # Stan doesn't like zero-length arrays so put in a dummy value
    # if there are no discontinuities
    if len(tDiscontinuities) == 0:
        tDiscontinuities.append(0)
    # Calculate the (reduced) day index for the election day
    # (this determines when the lowered sigma for the campaign starts)
    # Raw day zero maps to Stan day one, matching the poll-day conversion.
    tElectionDay = (
        inputs.e_data.election_day // model_params.tFactor + 1
    )
    # Calculate the thresholds between new and old house effects
    # (this determines when the house effects are mixed)
    tHouseEffectNew = model_params.houseEffectNew // model_params.tFactor
    tHouseEffectOld = model_params.houseEffectOld // model_params.tFactor
    # Calculate the prior series for each day in the reduced series
    # This is the default assumption for each day before polls are taken into account
    prior_series_t = [inputs.prior_series.prior_series_daily[i * model_params.tFactor] for i in range(tDayCount)]
    # Calculate the prior sigma for each day in the reduced series
    # This is the variance in the default assumption
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
    print(f'*** Beginning sampling for {model_inputs.party} ***')
    end = e_data.start + timedelta(days=int(e_data.n_days))
    print(f'Start date of model: {e_data.start:%Y-%m-%d}')
    print(f'End date of model: {end:%Y-%m-%d}')
    print()

    # Do model sampling. Time for diagnostic purposes
    start_time = perf_counter()
    fit = sm.sampling(data=model_inputs.stan_data,
                        iter=model_inputs.iterations,
                        chains=model_inputs.chains,
                        seed=model_inputs.random_seed,
                        control={'max_treedepth': model_params.stan_max_treedepth,
                                'adapt_delta': model_params.stan_adapt_delta})
    finish_time = perf_counter()
    print('Time elapsed: ' + format(finish_time - start_time, '.2f')
            + ' seconds')
    print(f'*** Finished sampling for {model_inputs.party} ***')

    # Check technical model diagnostics
    diagnostic_results = pystan.diagnostics.check_hmc_diagnostics(fit)
    print(diagnostic_results)
    model_inputs.diagnostics_recorder.record(
        election=''.join(e_data.e_tuple),
        party=model_inputs.party,
        excluded_pollster=model_inputs.excluded_pollster,
        random_seed=model_inputs.random_seed,
        checks=diagnostic_results,
    )

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
    # Isolates the extration of the trend days from the STAN output
    # so that the logic isn't repeated in multiple places
    # and this is therefore done consistently across the program
    e_data = inputs.e_data
    run_context = inputs.run_context
    summary = inputs.summary
    output_probs_t = inputs.output_probs_t
    model_params = run_context.model_params
    poll_vectors = run_context.poll_vectors
    tDayCount = run_context.reduced_series.tDayCount

    # This is the index of the first day in the summary table (STAN output)
    # that corresponds to the first day in the model
    offset = tDayCount + poll_vectors.n_houses * 2
    # The first three Stan summary columns are mean, standard error and
    # standard deviation; percentile columns start at index three.
    median_col = 3 + output_probs_t.index(0.5)

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
        trend_file.write(to_write)
        day_data.append(day.day_infos)

    trend_file.close()
    print('Saved trend file at ' + output_trend)
    return TrendOutputs(
        day_data=day_data,
        final_median=round(day_data[-1][output_probs_t.index(0.5)], 3),
    )


def collect_trend_outputs(output_context, writing_context):
    """Extract only the compact calibration values without serialising a trace."""

    days = list(iter_trend_days(IterTrendDaysInputs(
        e_data=output_context.e_data,
        run_context=output_context.run_context,
        summary=writing_context.summary,
        output_probs_t=writing_context.output_probs_t,
    )))
    if not days:
        raise ConfigError(
            'Stan output contained no trend days for {}.'.format(
                output_context.party
            )
        )
    return TrendOutputs(
        day_data=[day.day_infos for day in days],
        # Match legacy CSV compaction, which reads the rounded 50% column.
        final_median=round(days[-1].median_val, 3),
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
        if party == 'OTH FP':
            named_minor_total = sum(
                medians[day.effective_day]
                for oth_party, medians in e_data.others_medians.items()
                if oth_party in others_parties
            )
            unnamed_others = derive_unnamed_others_median(
                day.median_val,
                named_minor_total,
            )
            e_data.others_medians[party][
                day.effective_day
            ] = unnamed_others
            if output_context.config.use_approvals():
                mode = (
                    'scheduled cutoff {}d / poll endpoint {}d'.format(
                        output_context.config.cutoff_days,
                        e_data.days_to_election,
                    )
                    if output_context.config.cutoff_mode
                    else 'final trend'
                )
                output_context.config.unnamed_others_diagnostics.record(
                    election=''.join(e_data.e_tuple),
                    mode=mode,
                    day=day.effective_day,
                    inclusive_others=day.median_val,
                    named_minor_total=named_minor_total,
                    adjusted_unnamed_others=unnamed_others,
                )
        else:
            e_data.others_medians[party][
                day.effective_day
            ] = day.median_val


def write_cutoff_trend(
    output_context: OutputContext,
    writing_context: WritingContext,
):
    """Store only the endpoint distribution needed by downstream calibration."""

    trend_days = iter_trend_days(IterTrendDaysInputs(
        e_data=output_context.e_data,
        run_context=output_context.run_context,
        summary=writing_context.summary,
        output_probs_t=writing_context.output_probs_t,
    ))
    final_day = None
    for final_day in trend_days:
        pass
    if final_day is None:
        raise ConfigError(
            'Stan output contained no trend days for {}.'.format(
                output_context.party
            )
        )

    election = ''.join(output_context.e_data.e_tuple)
    output_context.config.cutoff_output_store.write(
        election=election,
        party=output_context.party,
        scheduled_cutoff_days=output_context.config.cutoff_days,
        poll_trend_end_days=output_context.e_data.days_to_election,
        random_seed=output_context.random_seed,
        probabilities=writing_context.output_probs_t,
        values=final_day.day_infos,
    )
    print(
        'Saved scheduled cutoff {}d (poll trend ends {}d out) for {} in {}'
        .format(
            output_context.config.cutoff_days,
            output_context.e_data.days_to_election,
            output_context.party,
            fp_model_provenance.cutoff_output_path(election),
        )
    )


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
    new_house_effect_medians: Dict[str, float]


def collect_house_effect_outputs(inputs: WriteHouseEffectsInputs):
    """Extract the house-effect data required by poll adjustment and summary."""

    output_probs_t = inputs.writing_context.output_probs_t
    poll_vectors = inputs.run_context.poll_vectors
    summary = inputs.writing_context.summary
    offset = inputs.run_context.reduced_series.tDayCount
    median_column = 3 + output_probs_t.index(0.5)
    new_house_effects = []
    old_house_effects = []
    new_house_effect_medians = {}
    for house in range(poll_vectors.n_houses):
        new_house_effects.append(summary[offset + house, 0])
        old_house_effects.append(
            summary[offset + poll_vectors.n_houses + house, 0]
        )
        new_house_effect_medians[poll_vectors.houses[house]] = round(
            summary[offset + house][median_column], 3
        )
    return WriteHouseEffectsOutputs(
        new_house_effects=new_house_effects,
        old_house_effects=old_house_effects,
        new_house_effect_medians=new_house_effect_medians,
    )

def write_house_effects(inputs: WriteHouseEffectsInputs):
    output_context = inputs.output_context
    e_data = output_context.e_data
    output_probs_t = inputs.writing_context.output_probs_t
    party = inputs.party
    poll_vectors = inputs.run_context.poll_vectors
    summary = inputs.writing_context.summary
    tDayCount = inputs.run_context.reduced_series.tDayCount
    
    output_house_effects = output_filename_ctx(inputs.output_context, 'house_effects')

    collected = collect_house_effect_outputs(inputs)
    new_house_effects = collected.new_house_effects
    old_house_effects = collected.old_house_effects

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
    
    return collected


def calibration_recent_poll_counts(df):
    """Match the legacy compactor's final-183-model-day pollster counts."""

    polls = [
        (str(df.loc[index, 'Firm']), int(df.loc[index, 'DayNum']))
        for index in df.index
    ]
    if not polls:
        raise ConfigError('Bias calibration had no polls to summarise.')
    final_day = max(day for _, day in polls)
    start_day = final_day - calibration_summary.RECENT_POLL_WINDOW_DAYS
    counts = {pollster: 0 for pollster, _ in polls}
    for pollster, day in polls:
        if day >= start_day:
            counts[pollster] += 1
    return counts


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

@dataclass
class ExcludedPoll:
    day_index: int  # 0-based day (DayNum - 1)
    vote: float
    poll_index: int
    pollster: str

def build_excluded_polls(inputs: CalibratePollstersInputs) -> List[ExcludedPoll]:
    exc_polls = inputs.exc_polls
    party = inputs.party
    rows = zip(exc_polls['DayNum'], exc_polls[party], exc_polls.axes[0], exc_polls['Firm'])
    return [
        ExcludedPoll(day_index=int(day) - 1, vote=vote, poll_index=poll_index, pollster=pollster)
        for day, vote, poll_index, pollster in rows
    ]
                    

@dataclass
class ComputerPollsterHouseEffectsInputs:
    excluded_polls: List[ExcludedPoll]
    median_col: int
    parent_inputs: CalibratePollstersInputs

def compute_pollster_house_effects(inputs: ComputerPollsterHouseEffectsInputs) -> Dict[str, float]:
    excluded_polls = inputs.excluded_polls
    median_col = inputs.median_col
    day_data = inputs.parent_inputs.trend_outputs.day_data

    diff_sum = {}
    pollster_count = {}
    house_effects = {}
    for a in excluded_polls:
        day, vote, pollster = a.day_index, a.vote, a.pollster
        trend_value = day_data[day][median_col]
        if pollster not in diff_sum:
            diff_sum[pollster] = 0
            pollster_count[pollster] = 0
        diff_sum[pollster] += vote - trend_value
        pollster_count[pollster] += 1
    for key in diff_sum.keys():
        house_effects[key] = diff_sum[key] / pollster_count[key]
    return house_effects

@dataclass
class InterpolatePercentileInputs:
    day_distribution: List[float]
    output_probs: List[float]
    value: float

def interpolate_percentile(inputs: InterpolatePercentileInputs) -> float:
    day_distribution = inputs.day_distribution
    output_probs = inputs.output_probs
    value = inputs.value

    for index, upper_prob in enumerate(output_probs):
        upper_value = day_distribution[index]
        if value < upper_value:
            if index == 0:
                return 0.001
            else:
                lower_value = day_distribution[index - 1]
                lower_prob = output_probs[index - 1]
                lerp = ((value - lower_value) /
                    (upper_value - lower_value))
                return (lower_prob + lerp * 
                    (upper_prob - lower_prob))
    # default high percentile if above all thresholds
    return 0.999

@dataclass
class PollCalibration:
    vote: float
    trend_median: float
    adjusted_vote: float
    percentile: float
    deviation: float
    prob_deviation: float
    neighbours: float

@dataclass
class BuildPollCalibrationInputs:
    poll: ExcludedPoll
    day_data: List[List[float]]
    median_col: int
    output_probs: List[float]
    house_effects: Dict[str, float]
    df_daynum: pd.Series

def build_poll_calibration(inputs: BuildPollCalibrationInputs) -> PollCalibration:
    day_data = inputs.day_data
    df_daynum = inputs.df_daynum
    house_effects = inputs.house_effects
    median_col = inputs.median_col
    output_probs = inputs.output_probs
    poll = inputs.poll

    trend_median = day_data[poll.day_index][median_col]
    adjusted_vote = poll.vote - house_effects[poll.pollster]
    percentile = interpolate_percentile(InterpolatePercentileInputs(
        day_distribution=day_data[poll.day_index],
        output_probs=output_probs,
        value=adjusted_vote,
    ))
    deviation = adjusted_vote - trend_median
    prob_deviation = abs(percentile - 0.5)
    neighbours = sum(min(1, 2 ** (-abs(poll.day_index + 1 - other_day) / 20) * 0.5)
                    for other_day in df_daynum)
    return PollCalibration(
        vote=poll.vote,
        trend_median=trend_median,
        adjusted_vote=adjusted_vote,
        percentile=percentile,
        deviation=deviation,
        prob_deviation=prob_deviation,
        neighbours=neighbours,
    )

@dataclass
class RecordCalibrationInputs:
    e_data: ElectionData
    excluded_pollster: str
    party: str
    poll: ExcludedPoll
    cal: PollCalibration

def record_calibration(inputs: RecordCalibrationInputs) -> None:
    e_data = inputs.e_data
    excluded_pollster = inputs.excluded_pollster
    party = inputs.party
    poll = inputs.poll
    cal = inputs.cal
    e_data.poll_calibrations[(excluded_pollster, poll.day_index, party, poll.poll_index)] = (
        cal.vote,
        cal.trend_median,
        cal.adjusted_vote,
        cal.percentile,
        cal.deviation,
        cal.prob_deviation,
        cal.neighbours,
    )

def calibrate_pollsters(inputs: CalibratePollstersInputs) -> None:

    # An initial calibration step without using historical house effects
    # or variability data as inputs. The poll calibrations are later used to
    # determine how reliably each pollster indicates the trend, its historical bias,
    # and how useful it is for estimating overall bias.

    day_data = inputs.trend_outputs.day_data
    df = inputs.df
    e_data = inputs.output_context.e_data
    excluded_pollster = inputs.excluded_pollster
    output_probs = inputs.writing_context.output_probs_t
    party = inputs.party
                        
    excluded_polls = build_excluded_polls(inputs)
    if len(excluded_polls) <= 1: return
    print(f'Trend closeness statistics for {excluded_pollster}')
    median_col = output_probs.index(0.5)
    house_effects = compute_pollster_house_effects(ComputerPollsterHouseEffectsInputs(
        excluded_polls=excluded_polls,
        median_col=median_col,
        parent_inputs=inputs,
    ))
        
    deviations = []
    prob_deviations = []
    for a in excluded_polls:
        poll_calibration = build_poll_calibration(BuildPollCalibrationInputs(
            poll=a,
            day_data=day_data,
            median_col=median_col,
            output_probs=output_probs,
            house_effects=house_effects,
            df_daynum=df['DayNum'],
        ))
        record_calibration(RecordCalibrationInputs(
            e_data=e_data,
            excluded_pollster=excluded_pollster,
            party=party,
            poll=a,
            cal=poll_calibration,
        ))
        deviations.append(poll_calibration.deviation)
        prob_deviations.append(poll_calibration.prob_deviation)
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

    if config.cutoff_mode:
        prepare_others_medians(PrepareOthersMediansInputs(
            output_context=output_context,
            writing_context=writing_context,
        ))
        write_cutoff_trend(output_context, writing_context)
        return

    detailed_calibration_trace = (
        (config.calibrate_pollsters or config.calibrate_bias)
        and config.calibration_traces
    )
    if detailed_calibration_trace or not (
        config.calibrate_pollsters or config.calibrate_bias
    ):
        trend_outputs = write_trend(WriteTrendInputs(
            output_context=output_context,
            writing_context=writing_context,
        ))
    else:
        trend_outputs = collect_trend_outputs(output_context, writing_context)

    prepare_others_medians(PrepareOthersMediansInputs(
        output_context=output_context,
        writing_context=writing_context,
    ))
    
    house_effects_inputs = WriteHouseEffectsInputs(
        output_context=output_context,
        party=party,
        run_context=run_context,
        writing_context=writing_context,
    )
    if detailed_calibration_trace or not (
        config.calibrate_pollsters or config.calibrate_bias
    ):
        house_effects_outputs = write_house_effects(house_effects_inputs)
        write_polls(WritePollsInputs(
            df=df,
            output_context=output_context,
            party=party,
            run_context=run_context,
            write_house_effects_outputs=house_effects_outputs,
        ))
    else:
        house_effects_outputs = collect_house_effect_outputs(
            house_effects_inputs
        )
    
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
    elif config.calibrate_bias:
        e_data = output_context.e_data
        e_data.calibration_bias_records[party] = (
            trend_outputs.final_median,
            house_effects_outputs.new_house_effect_medians,
            calibration_recent_poll_counts(df),
        )


@dataclass
class RunPartyInputs:
    config: Config
    diagnostics_recorder: StanDiagnosticsRecorder
    e_data: ElectionData
    excluded_pollster: str
    m_data: ModellingData
    model_params: ModelParams
    party: str
    random_seed: int

def run_party(inputs: RunPartyInputs) -> Optional[OutputContext]:
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
        diagnostics_recorder=inputs.diagnostics_recorder,
        party=party,
        e_data=e_data,
        excluded_pollster=excluded_pollster,
        model_params=model_params,
        random_seed=inputs.random_seed,
    )

    verify_timeline_consistency(party_context)

    fit = run_stan_model(model_inputs)
    
    output_context = OutputContext(
        e_data=e_data,
        party=party,
        config=config,
        excluded_pollster=excluded_pollster,
        poll_prep_result=poll_prep_result,
        random_seed=inputs.random_seed,
        run_context=run_context,
    )

    write_outputs(output_context, fit)
    return output_context


def finalise_calibrations(e_data, trace_directory=None):
    polls_string = {}
    total_weight = {}
    total_weighted_dev = {}
    output_files = []
    summary_values = []
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
        summary_values.append(
            (key[1], key[0], weighted_average_deviation, weight)
        )
        if trace_directory is not None:
            filename = (
                f'{trace_directory}/calib_'
                f'{e_data.e_tuple[0]}{e_data.e_tuple[1]}_'
                f'{key[0]}_{key[1]}.csv'
            )
            with open(filename, 'w') as f:
                f.write(f'{weighted_average_deviation},'
                        f'{weight},\n{polls_string[key]}')
            output_files.append(filename)
    return output_files, summary_values


def check_suspension(
    suspension_path='suspend.txt',
    before_pause=None,
    input_func=input,
    sleep_func=time.sleep,
):
    """Pause safely between Stan fits when the control file contains 1."""

    def suspension_requested():
        try:
            with open(suspension_path, 'r', encoding='utf-8') as control_file:
                return control_file.read().strip() == '1'
        except FileNotFoundError:
            return False

    if not suspension_requested():
        return False

    if before_pause is not None:
        before_pause()

    try:
        input_func(
            'Suspension requested. Completed outputs have been saved. '
            'Press Enter to resume: '
        )
        with open(suspension_path, 'w', encoding='utf-8') as control_file:
            control_file.write('0\n')
    except EOFError:
        # Detached runs have no keyboard input, so retain the old file-based
        # resume path rather than terminating a long-running batch.
        print(
            'No interactive input is available; change suspend.txt from 1 '
            'to 0 to resume.'
        )
        while suspension_requested():
            sleep_func(5)

    print('Resuming fp_model generation.')
    return True


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
        config.synthetic_tpps_by_region = generate_synthetic_tpps()


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
    # Cutoff mode deliberately reruns every party after an interrupted cutoff:
    # later party fits can depend on medians prepared by earlier fits.
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


def cutoff_work_items(config, m_data, schedule):
    """Return distinct scheduled cutoff and actual poll-endpoint pairs."""

    poll_dates_by_region = {}
    for election in config.elections:
        election_tuple = (str(election.year()), election.region())
        region = election.region()
        if region not in poll_dates_by_region:
            poll_data = pd.read_csv(
                data_source[region],
                usecols=['MidDate'],
            )
            parsed_dates = pd.to_datetime(
                poll_data['MidDate'],
                errors='raise',
            )
            poll_dates_by_region[region] = [
                poll_date.date()
                for poll_date in parsed_dates
                if not pd.isna(poll_date)
            ]

        cycle_start, election_day = m_data.election_cycles[election_tuple]
        cycle_poll_dates = [
            poll_date
            for poll_date in poll_dates_by_region[region]
            if cycle_start.date() <= poll_date <= election_day.date()
        ]
        effective_cutoffs = (
            fp_model_provenance.effective_cutoff_schedule(
                election_day.date(),
                cycle_poll_dates,
                schedule,
            )
        )
        print(
            '{} has {} distinct poll information sets across {} scheduled '
            'cutoff points.'.format(
                election.short(),
                len(effective_cutoffs),
                len(schedule),
            )
        )
        for cutoff_index, (
            scheduled_days,
            poll_trend_end_days,
        ) in enumerate(effective_cutoffs):
            yield (
                election,
                scheduled_days,
                poll_trend_end_days,
                cutoff_index == len(effective_cutoffs) - 1,
            )


def run_models() -> None:
    # check version information
    print('Python version: {}'.format(sys.version))
    print('pystan version: {}'.format(pystan.__version__))

    diagnostics_recorder = StanDiagnosticsRecorder()
    try:
        config = build_config()

        model_params = build_model_params()
        base_seed = (
            config.seed
            if config.seed is not None
            else secrets.randbelow(2 ** 31 - 1) + 1
        )
        print('Base random seed: {}'.format(base_seed))
        if config.calibration_traces:
            trace_run_id = '{}-{}-{}'.format(
                datetime.datetime.utcnow().strftime('%Y%m%dT%H%M%SZ'),
                os.getpid(),
                base_seed,
            )
            config.calibration_trace_directory = (
                './Outputs/Calibration/Diagnostics/{}/'.format(trace_run_id)
            )
            os.makedirs(config.calibration_trace_directory, exist_ok=True)
            print(
                'Writing optional calibration traces under {}.'.format(
                    config.calibration_trace_directory
                )
            )
        provenance_recorder = (
            calibration_provenance.CalibrationRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if config.calibrate_pollsters or config.calibrate_bias
            else None
        )
        pure_provenance_recorder = (
            fp_model_provenance.PureTrendRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if (
                config.pure
                and not config.calibrate_pollsters
                and not config.calibrate_bias
                and not config.cutoff_mode
            )
            else None
        )
        final_provenance_recorder = (
            fp_model_provenance.FinalTrendRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if config.use_approvals() and not config.cutoff_mode
            else None
        )
        cutoff_provenance_recorder = (
            fp_model_provenance.CutoffTrendRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if config.use_approvals() and config.cutoff_mode
            else None
        )

        maybe_generate_approvals(config)

        m_data = ModellingData()
        if config.cutoff_mode:
            cutoff_schedule = fp_model_provenance.cutoff_schedule()
            config.cutoff_output_store = (
                fp_model_provenance.CutoffOutputStore()
            )
            print(
                'Loaded {} triangular cutoff points shared with '
                'trend_adjust.py.'
                .format(len(cutoff_schedule))
            )
            work_items = cutoff_work_items(
                config,
                m_data,
                cutoff_schedule,
            )
        else:
            work_items = (
                (election, 0, 0, True) for election in config.elections
            )

        cutoff_elections_started = set()
        cutoff_federal_prior_files = {}
        for (
            desired_election,
            requested_cutoff_days,
            expected_poll_trend_end_days,
            final_cutoff_for_election,
        ) in work_items:
            config.cutoff_days = requested_cutoff_days
            e_data = build_election_data(ElectionDataInputs(
                config=config,
                m_data=m_data,
                desired_election=desired_election,
            ))
            if e_data is None:
                continue
            election_tag = ''.join(e_data.e_tuple)
            if (
                config.cutoff_mode
                and election_tag not in cutoff_elections_started
            ):
                # A cutoff invocation is one complete election batch. Never
                # mix imported or older rows with newly generated fits.
                config.cutoff_output_store.reset(election_tag)
                cutoff_elections_started.add(election_tag)
                print(
                    'Started a fresh consolidated cutoff working file for {}.'
                    .format(election_tag)
                )
            if (
                config.cutoff_mode
                and e_data.days_to_election
                != expected_poll_trend_end_days
            ):
                raise ConfigError(
                    'Scheduled cutoff {}d for {} resolved to a {}d poll '
                    'endpoint, but preflight resolved it to {}d.'.format(
                        requested_cutoff_days,
                        desired_election.short(),
                        e_data.days_to_election,
                        expected_poll_trend_end_days,
                    )
                )
            if (
                config.cutoff_mode
                and config.cutoff_output_store.is_complete(
                    election_tag,
                    requested_cutoff_days,
                    e_data.days_to_election,
                )
            ):
                print(
                    'Scheduled cutoff {}d (poll trend ends {}d out) for '
                    'election {} is complete, skipping.'
                    .format(
                        requested_cutoff_days,
                        e_data.days_to_election,
                        desired_election.short(),
                    )
                )
                continue
            calibration_trace_files = []
            for excluded_pollster in e_data.pollster_exclusions:
                # Each exclusion is an independent fit. Never allow a skipped
                # party to leave a median from the previous pollster round.
                e_data.others_medians = {}
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
                        check_suspension(
                            before_pause=(
                                provenance_recorder.flush
                                if provenance_recorder is not None
                                else None
                            )
                        )

                    preparation_target = election_tag
                    if config.cutoff_mode:
                        preparation_target += (
                            f' at poll endpoint '
                            f'{e_data.days_to_election}d'
                        )
                    print(
                        f'*** Beginning preparation for {party} in '
                        f'{preparation_target} ***'
                    )

                    # This has to be done here because it updates the TPP based on
                    # others_medians, allowing the estimation of the size of
                    # minor parties that some pollsters don't report
                    maybe_create_tpp_series(MaybeCreateTppSeriesInputs(
                        desired_election=desired_election,
                        e_data=e_data,
                        m_data=m_data,
                        party=party,
                    ))

                    mode = (
                        'pollster-bias'
                        if config.calibrate_bias
                        else 'pollster-calibration'
                        if config.calibrate_pollsters
                        else 'cutoff-{}d'.format(
                            e_data.days_to_election
                        )
                        if config.cutoff_mode
                        else 'poll-trend'
                    )
                    random_seed = (
                        calibration_provenance.derive_stan_seed(
                            base_seed,
                            election_tag,
                            party,
                            excluded_pollster,
                            mode,
                        )
                    )
                    output_context = run_party(RunPartyInputs(
                        config=config,
                        diagnostics_recorder=diagnostics_recorder,
                        e_data=e_data,
                        excluded_pollster=excluded_pollster,
                        m_data=m_data,
                        model_params=model_params,
                        party=party,
                        random_seed=random_seed,
                    ))
                    if (
                        provenance_recorder is not None
                        and output_context is not None
                        and config.calibration_traces
                    ):
                        output_files = [
                            output_filename_ctx(output_context, kind)
                            for kind in (
                                'trend',
                                'polls',
                                'house_effects',
                            )
                        ]
                        provenance_recorder.record_model_outputs(
                            election=election_tag,
                            party=party,
                            excluded_pollster=excluded_pollster,
                            bias_calibration=config.calibrate_bias,
                            outputs=output_files,
                            random_seed=random_seed,
                            feedback_files=sorted(set(
                                e_data.federal_prior_files
                            )),
                        )
                        if config.calibrate_pollsters:
                            calibration_trace_files.extend(output_files)
                    if (
                        pure_provenance_recorder is not None
                        and output_context is not None
                    ):
                        pure_dependencies = (
                            pure_provenance_recorder.dependencies_for(
                                election_tag,
                                e_data.federal_prior_files_by_party.get(
                                    party, []
                                ),
                            )
                        )
                        output_files = [
                            output_filename_ctx(output_context, kind)
                            for kind in (
                                'trend',
                                'polls',
                                'house_effects',
                            )
                        ]
                        pure_provenance_recorder.record(
                            election=election_tag,
                            party=party,
                            outputs=output_files,
                            dependencies=pure_dependencies,
                            random_seed=random_seed,
                        )
                    if (
                        final_provenance_recorder is not None
                        and output_context is not None
                    ):
                        final_dependencies = (
                            final_provenance_recorder.dependencies_for(
                                election_tag,
                                party,
                                e_data.federal_prior_files_by_party.get(
                                    party, []
                                ),
                            )
                        )
                        output_files = [
                            output_filename_ctx(output_context, kind)
                            for kind in (
                                'trend',
                                'polls',
                                'house_effects',
                            )
                        ]
                        final_provenance_recorder.record(
                            election=election_tag,
                            party=party,
                            outputs=output_files,
                            dependencies=final_dependencies,
                            random_seed=random_seed,
                        )
                # Preserve completed work-unit provenance if a later Stan fit
                # or a later excluded-pollster block is interrupted.
                if provenance_recorder is not None:
                    provenance_recorder.flush()

            if config.calibrate_pollsters:
                trace_files, summary_values = finalise_calibrations(
                    e_data=e_data,
                    trace_directory=(
                        config.calibration_trace_directory
                        if config.calibration_traces else None
                    ),
                )
                staging_rows = calibration_summary.build_leave_one_out_rows(
                    election_tag, summary_values
                )
                staging_output = calibration_summary.direct_staging_path(
                    './Outputs/Calibration', election_tag, 'leave-one-out'
                )
                calibration_summary.write_direct_staging_atomically(
                    staging_output, staging_rows
                )
                if provenance_recorder is not None:
                    provenance_recorder.record_summaries(
                        election=election_tag,
                        outputs=[staging_output],
                        trace_files=calibration_trace_files + trace_files,
                    )
                    provenance_recorder.flush()
            if config.calibrate_bias:
                staging_rows = calibration_summary.build_bias_rows(
                    election_tag,
                    [
                        (party, *values)
                        for party, values in e_data.calibration_bias_records.items()
                    ],
                )
                staging_output = calibration_summary.direct_staging_path(
                    './Outputs/Calibration', election_tag, 'bias'
                )
                calibration_summary.write_direct_staging_atomically(
                    staging_output, staging_rows
                )
                if provenance_recorder is not None:
                    provenance_recorder.record_bias_staging(
                        election_tag, staging_output
                    )
                    provenance_recorder.flush()
                loo_staging = calibration_summary.direct_staging_path(
                    './Outputs/Calibration', election_tag, 'leave-one-out'
                )
                if loo_staging.is_file():
                    summary_output, row_count = (
                        calibration_summary.promote_direct_summary(
                            './Outputs/Calibration', election_tag
                        )
                    )
                    calibration_summary_provenance.record_direct_summary(
                        election_tag,
                        summary_output,
                        [os.path.basename(sys.executable)] + sys.argv,
                    )
                    print(
                        'Saved {} compact calibration rows for {}.'.format(
                            row_count, election_tag
                        )
                    )
                else:
                    print(
                        'Bias calibration staging for {} is complete; run '
                        '--calibrate before promoting its compact summary.'
                        .format(election_tag)
                    )
            if cutoff_provenance_recorder is not None:
                config.cutoff_output_store.mark_complete(
                    election_tag,
                    requested_cutoff_days,
                    e_data.days_to_election,
                )
                cutoff_federal_prior_files.setdefault(
                    election_tag, set()
                ).update(e_data.federal_prior_files)
                if final_cutoff_for_election:
                    cutoff_dependencies = (
                        cutoff_provenance_recorder
                        .dependencies_for_election(
                            election_tag,
                            sorted(
                                cutoff_federal_prior_files[election_tag]
                            ),
                        )
                    )
                    config.cutoff_output_store.promote(
                        election_tag,
                        certify=lambda output: (
                            cutoff_provenance_recorder.record(
                                election=election_tag,
                                output=output,
                                dependencies=cutoff_dependencies,
                            )
                        ),
                    )
                    print(
                        'Completed and promoted consolidated cutoff file '
                        'for {}.'.format(election_tag)
                    )

    # indicate completion (delete these lines if not the original author)
    except Exception as e:
        diagnostics_recorder.report(completed=False)
        if 'config' in locals():
            config.unnamed_others_diagnostics.report(completed=False)
        with open(f'itsdone.txt', 'w') as f:
            f.write('2')
        raise
    
    diagnostics_recorder.report()
    config.unnamed_others_diagnostics.report()
    with open(f'itsdone.txt', 'w') as f:
        f.write('1')


if __name__ == '__main__':
    run_models()
