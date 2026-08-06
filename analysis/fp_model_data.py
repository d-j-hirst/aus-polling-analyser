"""Configuration, election loading and typed model data for fp_model."""

import argparse
import calibration_provenance
import csv
import fp_model_checkpoints
import fp_model_provenance
import math
import numpy as np
import os
import pandas as pd
from dataclasses import dataclass
from election_code import ElectionCode
from pathlib import Path
from typing import List, Optional, Tuple

import fp_model_constants
from fp_model_constants import (
    STAN_SEED_NAMESPACE,
    data_source,
    fp_model_source_files,
    major_parties,
    others_parties,
    unnamed_others_base,
    unnamed_others_diagnostic_limit,
    unnamed_others_diagnostic_threshold,
)


class ConfigError(ValueError):
    pass


def compressed_day_number(day_number, t_factor):
    """Map a one-based calendar day number to a one-based model node."""

    if t_factor < 1:
        raise ValueError('t_factor must be at least one')
    if day_number < 1:
        raise ValueError('day_number must be at least one')
    return (int(day_number) - 1) // int(t_factor) + 1


def compressed_calendar_offset(day_offset, t_factor):
    """Map a zero-based calendar offset to a one-based model node."""

    if day_offset < 0:
        raise ValueError('day_offset must not be negative')
    return compressed_day_number(int(day_offset) + 1, t_factor)


def transition_entering_calendar_offset(day_offset, t_factor):
    """Return the model transition entering a zero-based calendar offset.

    Transition ``n`` links model node ``n`` to node ``n + 1``. An event on
    calendar day zero has no preceding in-model transition and returns zero.
    """

    if t_factor < 1:
        raise ValueError('t_factor must be at least one')
    if day_offset < 0:
        raise ValueError('day_offset must not be negative')
    if day_offset == 0:
        return 0
    return (int(day_offset) + int(t_factor) - 1) // int(t_factor)


def house_effect_new_factor(days_before_present, new_threshold, old_threshold):
    """Return the fraction of the new house effect used at one poll."""

    if not old_threshold > new_threshold >= 0:
        raise ValueError(
            'old house-effect threshold must exceed the new threshold'
        )
    if days_before_present >= old_threshold:
        return 0.0
    if days_before_present >= new_threshold:
        return (
            (old_threshold - days_before_present)
            / (old_threshold - new_threshold)
        )
    return 1.0


def stan_seed_mode(config, e_data):
    """Return a stable mode label for one independently sampled fit."""

    if config.calibrate_bias:
        return 'bias'
    if config.calibrate_pollsters:
        return 'calibration'
    if config.cutoff_mode:
        return 'cutoff-{}d'.format(e_data.days_to_election)
    if config.pure:
        return 'pure'
    return 'final'



def calibration_checkpoint_source_files(e_data):
    """Return direct files whose contents affect one calibration checkpoint."""

    files = list(fp_model_source_files())
    files.extend([
        Path(fp_model_checkpoints.__file__),
        Path(calibration_provenance.__file__),
        Path('./Models/fp_model.stan'),
        Path(data_source[e_data.e_tuple[1]]),
        Path('./Data/significant-parties.csv'),
        Path('./Data/preference-estimates.csv'),
        Path('./Data/prior-results.csv'),
        Path('./Data/discontinuities.csv'),
        Path('./Data/desired-iterations.csv'),
        Path('./Data/election-cycles.csv'),
    ])
    files.extend(Path(path) for path in e_data.federal_prior_files)
    return files

def calibration_checkpoint_identity(
    config,
    e_data,
    excluded_pollster,
    base_seed,
    parties,
):
    election = ''.join(e_data.e_tuple)
    mode = stan_seed_mode(config, e_data)
    seeds = {
        party: calibration_provenance.derive_stan_seed(
            base_seed,
            election,
            party,
            excluded_pollster,
            '{}:{}'.format(STAN_SEED_NAMESPACE, mode),
        )
        for party in parties
    }
    return {
        'election': election,
        'excluded_pollster': excluded_pollster,
        'mode': mode,
        'base_seed': base_seed,
        'seed_namespace': STAN_SEED_NAMESPACE,
        'parties': list(parties),
        'party_seeds': seeds,
        'source_fingerprint': fp_model_checkpoints.fingerprint_files(
            calibration_checkpoint_source_files(e_data)
        ),
    }


def calibration_checkpoint_payload(e_data, excluded_pollster):
    records = []
    for key, values in sorted(
        e_data.poll_calibrations.items(),
        key=lambda item: (
            item[0][0], item[0][2], item[0][1], item[0][3]
        ),
    ):
        if key[0] != excluded_pollster:
            continue
        records.append({
            'day_index': int(key[1]),
            'party': str(key[2]),
            'poll_index': int(key[3]),
            'values': [
                None if value is None else float(value)
                for value in values
            ],
        })
    federal_priors = {
        party: [
            [pd.Timestamp(date).strftime('%Y-%m-%d'), float(median)]
            for date, median in values
        ]
        for party, values in sorted(
            e_data.calibration_federal_priors.items()
        )
    }
    stan_seeds = [
        {'party': party, 'seed': int(seed)}
        for (mode, pollster, party), seed in sorted(
            e_data.resolved_stan_seeds.items()
        )
        if mode == 'calibration' and pollster == excluded_pollster
    ]
    return records, federal_priors, stan_seeds


def restore_calibration_checkpoint(e_data, identity, payload):
    excluded_pollster = identity['excluded_pollster']
    for record in payload['poll_calibrations']:
        values = record.get('values')
        if not isinstance(values, list) or len(values) != 7:
            raise ConfigError(
                'Calibration checkpoint for {} has an invalid poll record.'
                .format(identity['election'])
            )
        key = (
            excluded_pollster,
            int(record['day_index']),
            str(record['party']),
            int(record['poll_index']),
        )
        e_data.poll_calibrations[key] = tuple(values)
    e_data.calibration_federal_priors.update({
        party: [
            (pd.Timestamp(date), float(median))
            for date, median in values
        ]
        for party, values in payload['federal_priors'].items()
    })
    for record in payload['stan_seeds']:
        e_data.resolved_stan_seeds[
            ('calibration', excluded_pollster, record['party'])
        ] = int(record['seed'])


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

def load_election_cycles(path='./Data/election-cycles.csv'):
    cycles = {}
    try:
        with open(path, 'r', newline='', encoding='utf-8-sig') as source:
            for line_number, row in enumerate(csv.reader(source), start=1):
                if not row or not any(field.strip() for field in row):
                    continue
                if len(row) != 4:
                    raise ConfigError(
                        '{}:{} must contain year, region, start and end'
                        .format(path, line_number)
                    )
                year, region, start_text, end_text = (
                    field.strip() for field in row
                )
                key = (year, region)
                if not year or not region:
                    raise ConfigError(
                        '{}:{} has an empty election identifier'.format(
                            path, line_number
                        )
                    )
                if key in cycles:
                    raise ConfigError(
                        '{}:{} duplicates election {}{}'.format(
                            path, line_number, year, region
                        )
                    )
                try:
                    start = pd.Timestamp(start_text)
                    end = pd.Timestamp(end_text)
                except (TypeError, ValueError) as error:
                    raise ConfigError(
                        '{}:{} has an invalid election date'.format(
                            path, line_number
                        )
                    ) from error
                if pd.isna(start) or pd.isna(end) or start > end:
                    raise ConfigError(
                        '{}:{} has an invalid election period'.format(
                            path, line_number
                        )
                    )
                cycles[key] = (start, end)
    except OSError as error:
        raise ConfigError(
            'Could not read election cycles from {}: {}'.format(path, error)
        ) from error
    return cycles


def load_authored_rows(path, minimum_columns):
    """Load non-empty authored CSV rows with basic shape validation."""

    rows = []
    try:
        with open(path, newline='', encoding='utf-8-sig') as source:
            for line_number, row in enumerate(csv.reader(source), start=1):
                if not row or not any(field.strip() for field in row):
                    continue
                row = [field.strip() for field in row]
                if len(row) < minimum_columns:
                    raise ConfigError(
                        '{}:{} has {} columns; expected at least {}'.format(
                            path, line_number, len(row), minimum_columns
                        )
                    )
                rows.append((line_number, row))
    except OSError as error:
        raise ConfigError(
            'Could not read authored input {}: {}'.format(path, error)
        ) from error
    return rows


def finite_float(value, context):
    try:
        parsed = float(value)
    except (TypeError, ValueError) as error:
        raise ConfigError('{} is not numeric'.format(context)) from error
    if not math.isfinite(parsed):
        raise ConfigError('{} is not finite'.format(context))
    return parsed


def vote_share(value, context):
    parsed = finite_float(value, context)
    if not 0 <= parsed <= 100:
        raise ConfigError('{} is outside 0–100'.format(context))
    return parsed


def unique_mapping(items, path):
    """Build a mapping while rejecting duplicate authored keys."""

    result = {}
    for line_number, key, value in items:
        if key in result:
            raise ConfigError(
                '{}:{} duplicates key {}'.format(path, line_number, key)
            )
        result[key] = value
    return result


def load_pollster_parameter_file(path, value_names, validators):
    """Load a headerless pollster/party parameter file strictly."""

    mapping = {}
    expected_columns = 2 + len(value_names)
    for line_number, row in load_authored_rows(path, expected_columns):
        if len(row) != expected_columns:
            raise ConfigError(
                '{}:{} has {} columns; expected {}'.format(
                    path, line_number, len(row), expected_columns
                )
            )
        key = (row[0], row[1])
        if not key[0] or not key[1]:
            raise ConfigError(
                '{}:{} has an empty pollster or party'.format(
                    path, line_number
                )
            )
        if key in mapping:
            raise ConfigError(
                '{}:{} duplicates {} / {}'.format(
                    path, line_number, *key
                )
            )
        values = []
        for offset, (name, validator) in enumerate(
            zip(value_names, validators), start=2
        ):
            value = finite_float(
                row[offset],
                '{}:{} {}'.format(path, line_number, name),
            )
            if not validator(value):
                raise ConfigError(
                    '{}:{} has invalid {} {}'.format(
                        path, line_number, name, value
                    )
                )
            values.append(value)
        mapping[key] = values[0] if len(values) == 1 else tuple(values)
    return mapping


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


def federal_prior_needed_for_states(election, election_cycles):
    """Whether a federal term is used as a prior by any state election."""

    if election.region() != 'fed':
        return False
    federal_key = (str(election.year()), election.region())
    if federal_key not in election_cycles:
        return False
    federal_start, federal_end = election_cycles[federal_key]
    return any(
        region != 'fed'
        and start <= federal_end
        and federal_start <= end
        for (_, region), (start, end) in election_cycles.items()
    )


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
                'Base random seed. Every mode derives a stable Stan seed for '
                'its election, party, endpoint and excluded pollster.'
            ),
        )
        args = parser.parse_args()
        if args.election is None:
            raise ConfigError('The --election argument is required.')
        if args.calibrate and args.bias:
            raise ConfigError('--calibrate cannot be combined with --bias.')
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
        if self.pure and (
            self.calibrate_pollsters or self.calibrate_bias
        ):
            raise ConfigError(
                '--pure cannot be combined with --calibrate or --bias.'
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
        # This import is deferred to avoid a module-load cycle: preparation
        # depends on the data contracts defined in this module.
        from fp_model_prepare import order_parties_for_model

        # Load significant parties and arrange them in dependency order.
        party_path = './Data/significant-parties.csv'
        self.parties = unique_mapping(
            [
                (
                    line_number,
                    (row[0], row[1]),
                    order_parties_for_model(row[2:]),
                )
                for line_number, row in load_authored_rows(party_path, 3)
            ],
            party_path,
        )

        preference_path = './Data/preference-estimates.csv'
        preference_items = []
        for line_number, row in load_authored_rows(preference_path, 4):
            survival = 0.0
            if len(row) > 4 and row[4] and not row[4].startswith('#'):
                survival_percent = finite_float(
                    row[4],
                    '{}:{} preference exhaustion'.format(
                        preference_path, line_number
                    ),
                )
                if not 0 <= survival_percent <= 100:
                    raise ConfigError(
                        '{}:{} preference exhaustion is outside 0–100'
                        .format(preference_path, line_number)
                    )
                survival = survival_percent * 0.01
            flow_percent = finite_float(
                row[3],
                '{}:{} preference flow'.format(
                    preference_path, line_number
                ),
            )
            if not 0 <= flow_percent <= 100:
                raise ConfigError(
                    '{}:{} preference flow is outside 0–100'.format(
                        preference_path, line_number
                    )
                )
            preference_items.append((
                line_number,
                (row[0], row[1], row[2]),
                (
                    flow_percent * 0.01,
                    survival,
                ),
            ))
        self.preference_flows = unique_mapping(
            preference_items, preference_path
        )

        prior_path = './Data/prior-results.csv'
        self.prior_results = unique_mapping(
            [
                (
                    line_number,
                    ((row[0], row[1]), row[2]),
                    vote_share(
                        row[3],
                        '{}:{} prior result'.format(
                            prior_path, line_number
                        ),
                    ),
                )
                for line_number, row in load_authored_rows(prior_path, 4)
            ],
            prior_path,
        )

        # Discontinuities for leader changes or exceptionally significant events.
        discontinuity_path = './Data/discontinuities.csv'
        discontinuity_items = []
        for line_number, row in load_authored_rows(discontinuity_path, 1):
            for date_text in row[1:]:
                try:
                    parsed = pd.Timestamp(date_text)
                except (TypeError, ValueError) as error:
                    raise ConfigError(
                        '{}:{} has invalid discontinuity date {}'.format(
                            discontinuity_path, line_number, date_text
                        )
                    ) from error
                if pd.isna(parsed):
                    raise ConfigError(
                        '{}:{} has an empty discontinuity date'.format(
                            discontinuity_path, line_number
                        )
                    )
            discontinuity_items.append((line_number, row[0], row[1:]))
        self.discontinuities = unique_mapping(
            discontinuity_items, discontinuity_path
        )

        # Number of iterations per model (half are warm-up).
        iteration_path = './Data/desired-iterations.csv'
        iteration_items = []
        for line_number, row in load_authored_rows(iteration_path, 3):
            try:
                iterations = int(row[2])
            except ValueError as error:
                raise ConfigError(
                    '{}:{} has invalid iteration count'.format(
                        iteration_path, line_number
                    )
                ) from error
            if iterations < 2:
                raise ConfigError(
                    '{}:{} iteration count must be at least two'.format(
                        iteration_path, line_number
                    )
                )
            iteration_items.append((
                line_number, (row[0], row[1]), iterations
            ))
        self.desired_iterations = unique_mapping(
            iteration_items, iteration_path
        )

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
        self.election_cycles = m_data.election_cycles
        self.expected_parties = tuple(m_data.parties[tup])
        self.resolved_stan_seeds = {}
        self.others_medians = {}
        self.calibration_federal_priors = {}
        self.federal_prior_category = (
            'federal_calibration_priors'
            if config.calibrate_pollsters or config.calibrate_bias
            else 'poll_trend_outputs'
        )

        if not (config.calibrate_pollsters or config.calibrate_bias):
            self.get_pollster_analysis(desired_election)

        # Collect and validate the model data before doing expensive work.
        poll_path = data_source[tup[1]]
        try:
            self.base_df = pd.read_csv(poll_path)
        except (OSError, ValueError) as error:
            raise ConfigError(
                'Could not read poll data from {}: {}'.format(
                    poll_path, error
                )
            ) from error
        required_columns = {
            'MidDate', 'Firm', 'OTH FP', 'ALP FP', 'GRN FP', '@TPP',
            *self.expected_parties,
        }
        if not ({'LIB FP', 'LNP FP'} & set(self.base_df.columns)):
            required_columns.add('LNP FP or LIB FP')
        missing_columns = sorted(
            column for column in required_columns
            if (
                column == 'LNP FP or LIB FP'
                or column not in self.base_df.columns
            )
        )
        if missing_columns:
            raise ConfigError(
                '{} is missing required poll column(s): {}'.format(
                    poll_path, ', '.join(missing_columns)
                )
            )
        self.base_df.drop(
            columns=['Comments'],
            inplace=True,
            errors='ignore',
        )
        try:
            self.base_df['MidDate'] = pd.to_datetime(
                self.base_df['MidDate'], errors='raise'
            )
        except (TypeError, ValueError) as error:
            raise ConfigError(
                '{} contains an invalid MidDate'.format(poll_path)
            ) from error
        if self.base_df['MidDate'].isna().any():
            raise ConfigError(
                '{} contains an empty MidDate'.format(poll_path)
            )
        if self.base_df['Firm'].apply(
            lambda value: not isinstance(value, str) or not value.strip()
        ).any():
            raise ConfigError(
                '{} contains an empty polling firm'.format(poll_path)
            )
        vote_columns = (
            set(self.expected_parties)
            | {'OTH FP', 'ALP FP', 'GRN FP', '@TPP'}
            | ({'LIB FP'} if 'LIB FP' in self.base_df else {'LNP FP'})
        )
        for column in sorted(vote_columns):
            try:
                values = pd.to_numeric(
                    self.base_df[column], errors='raise'
                )
            except (TypeError, ValueError) as error:
                raise ConfigError(
                    '{} column {} contains a non-numeric value'.format(
                        poll_path, column
                    )
                ) from error
            non_missing = values.dropna()
            if (
                not np.isfinite(non_missing).all()
                or ((non_missing < 0) | (non_missing > 100)).any()
            ):
                raise ConfigError(
                    '{} column {} contains a value outside 0–100'.format(
                        poll_path, column
                    )
                )
            self.base_df[column] = values

        # Drop data not in range of this election period.
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
                    calibration=(
                        config.calibrate_pollsters or config.calibrate_bias
                    ),
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
            
            # Full-fit sentinel '' must stay last: LOO exclusions run first so
            # poll_calibrations is populated before should_skip_pollster_calibration
            # decides whether the unexcluded fit is still needed.
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
        calibration_directory = './Outputs/Calibration'
        # Variability files store poll sigma plus the effective evidence weight
        # used when estimating it. The model only consumes the sigma.
        variability = load_pollster_parameter_file(
            '{}/variability-{}.csv'.format(calibration_directory, code),
            ('poll sigma', 'evidence weight'),
            (
                lambda value: value > 0,
                lambda value: value >= 0,
            ),
        )
        self.pollster_sigmas = {
            key: values[0] for key, values in variability.items()
        }
        self.pollster_he_weights = load_pollster_parameter_file(
            '{}/he_weighting-{}.csv'.format(calibration_directory, code),
            ('house-effect weight',),
            (lambda value: value >= 0,),
        )
        self.pollster_biases = load_pollster_parameter_file(
            '{}/biases-{}.csv'.format(calibration_directory, code),
            ('bias mean', 'bias standard deviation'),
            (
                lambda value: True,
                lambda value: value >= 0,
            ),
        )

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
    tCampaignStartDay: int
    tFinalStartDay: int
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
        if not (self.houseEffectOld > self.houseEffectNew >= self.tFactor):
            raise ValueError(
                "houseEffectOld must be > houseEffectNew >= tFactor"
            )
        if (
            self.houseEffectOld // self.tFactor
            <= self.houseEffectNew // self.tFactor
        ):
            raise ValueError(
                "house-effect thresholds collapse after time compression"
            )
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
        if not 0 < self.stan_adapt_delta < 1:
            raise ValueError("stan_adapt_delta must be between zero and one")
        if self.stan_max_treedepth < 1:
            raise ValueError("stan_max_treedepth must be positive")


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
    calibration: bool = False


def calibration_prior_path(election_year):
    """Return the compact full-federal calibration prior for one term."""

    return (
        fp_model_constants.CALIBRATION_PRIOR_DIRECTORY
        / '{}fed.csv'.format(election_year)
    )


def load_fed_calibration_median(inputs: LoadFedTrendMedianInputs):
    """Load a federal full-calibration prior for a state calibration run.

    State calibration must not consume normal final trends: those forecasts
    already depend on pollster calibration and create a feedback loop. New
    calibration runs write a compact shared prior file. The detailed full
    calibration trend remains a read-only compatibility fallback while old
    archives are being migrated.
    """

    filename = calibration_prior_path(inputs.election_year)
    if filename.is_file():
        try:
            frame = pd.read_csv(filename)
            required_columns = {'Date', 'Party', '50%'}
            if not required_columns.issubset(frame.columns):
                raise ValueError(
                    'missing {}'.format(
                        ', '.join(sorted(required_columns - set(frame.columns)))
                    )
                )
            frame = frame[frame['Party'] == inputs.party].copy()
            if frame.empty:
                raise ValueError('no rows for {}'.format(inputs.party))
            frame['Date'] = pd.to_datetime(frame['Date'], errors='raise')
            frame['50%'] = pd.to_numeric(frame['50%'], errors='raise')
            if (
                not np.isfinite(frame['50%']).all()
                or ((frame['50%'] < 0) | (frame['50%'] > 100)).any()
            ):
                raise ValueError('non-finite or out-of-range median')
            frame = frame.sort_values('Date')
            if frame['Date'].duplicated().any():
                raise ValueError('duplicate dates')
        except (OSError, TypeError, ValueError) as error:
            raise ConfigError(
                'Invalid federal calibration prior {} for {}: {}'.format(
                    filename, inputs.party, error
                )
            ) from error
        inputs.used_files.append(str(filename))
        return pd.Series(frame['50%'].values, index=frame['Date'])

    # Completed pre-compaction calibration rounds wrote the full fit here.
    # It remains a calibration-only fallback, never a normal final trend.
    legacy_filename = (
        './Outputs/Calibration/fp_trend_{}fed_{}.csv'.format(
            inputs.election_year, inputs.party
        )
    )
    if not os.path.exists(legacy_filename):
        raise ConfigError(
            'State calibration requires a federal calibration prior for {} '
            'in {}. Run --calibrate for the overlapping federal election '
            'first.'.format(inputs.party, '{}fed'.format(inputs.election_year))
        )
    try:
        frame = pd.read_csv(legacy_filename, skiprows=2)
        if 'Day' not in frame or '50%' not in frame:
            raise ValueError('missing Day or 50% column')
        days = pd.to_numeric(frame['Day'], errors='raise')
        medians = pd.to_numeric(frame['50%'], errors='raise')
        if (
            not np.isfinite(days).all()
            or days.duplicated().any()
            or not np.isfinite(medians).all()
            or ((medians < 0) | (medians > 100)).any()
        ):
            raise ValueError('invalid day or median')
        with open(legacy_filename, 'r') as source:
            source.readline()
            start_day, start_month, start_year = [
                int(value) for value in source.readline().strip().split(',')
            ]
    except (OSError, TypeError, ValueError) as error:
        raise ConfigError(
            'Invalid legacy federal calibration prior {} for {}: {}'.format(
                legacy_filename, inputs.party, error
            )
        ) from error
    inputs.used_files.append(legacy_filename)
    start_date = pd.Timestamp(start_year, start_month, start_day)
    dates = start_date + pd.to_timedelta(days, unit='D')
    return pd.Series(medians.values, index=dates)


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
        required_columns = {
            'ScheduledCutoffDays',
            'PollTrendEndDays',
            'Party',
            'StanSeed',
            '50%',
        }
        if (
            reader.fieldnames is None
            or not required_columns.issubset(reader.fieldnames)
        ):
            raise ConfigError(
                'Federal cutoff file is missing required columns: {}'.format(
                    filename
                )
            )
        seen_endpoints = set()
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
                endpoint_key = int(row['PollTrendEndDays'])
                if endpoint_key in seen_endpoints:
                    raise ValueError('duplicate party/endpoint row')
                seen_endpoints.add(endpoint_key)
                if not math.isfinite(median) or not 0 <= median <= 100:
                    raise ValueError('non-finite or out-of-range median')
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

    if inputs.calibration:
        return load_fed_calibration_median(inputs)

    pure_suffix = '_pure' if inputs.pure else ''
    filename = (
        f'./Outputs/fp_trend_{election_year}fed_{party}'
        f'{pure_suffix}.csv'
    )
    if not os.path.exists(filename):
        return None
    inputs.used_files.append(filename)

    try:
        with open(filename, 'r', encoding='utf-8-sig') as source:
            label = source.readline().strip()
            start_line = source.readline().strip()
        if label != 'Start date day,Month,Year':
            raise ValueError('invalid start-date header')
        start_parts = [int(value) for value in start_line.split(',')]
        if len(start_parts) != 3:
            raise ValueError('invalid start date')
        start_day, start_month, start_year = start_parts
        start_date = pd.Timestamp(start_year, start_month, start_day)
        df = pd.read_csv(filename, skiprows=2)
        required_columns = {'Day', '50%'}
        if not required_columns.issubset(df.columns):
            raise ValueError(
                'missing {}'.format(
                    ', '.join(sorted(required_columns - set(df.columns)))
                )
            )
        day_values = pd.to_numeric(df['Day'], errors='raise')
        median = pd.to_numeric(df['50%'], errors='raise')
        if (
            not np.isfinite(day_values).all()
            or not np.isfinite(median).all()
            or ((median < 0) | (median > 100)).any()
        ):
            raise ValueError('non-finite or out-of-range trend data')
        if day_values.duplicated().any():
            raise ValueError('duplicate trend days')
    except (OSError, TypeError, ValueError) as error:
        raise ConfigError(
            'Invalid federal trend {} for {}: {}'.format(
                filename, party, error
            )
        ) from error
    dates = start_date + pd.to_timedelta(day_values, unit='D')
    return pd.Series(median.values, index=dates)


@dataclass
class LoadFedTrendSeriesForPartyInputs:
    available_through: Optional[pd.Timestamp]
    fed_cycles: List[Tuple[int, pd.Timestamp, pd.Timestamp]]
    party: str
    pure: bool
    used_files: list
    calibration: bool = False

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
            calibration=inputs.calibration,
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
