"""Define and validate normalized historical turnout evidence.

This module does not download results or estimate turnout. Source-specific
adapters should preserve each commission's published category name while
mapping it to the coarsest defensible canonical category. In particular,
combined published categories must not be split into invented observations.

Main classes and functions:
* ``ElectionDefinition`` and ``SourceDefinition`` identify elections and the
  official material from which records were extracted.
* ``SeatTotal`` stores final enrolment and ballot totals without treating
  unavailable values as zero.
* ``VoteTypeRecord`` stores final, mutually exclusive vote-type observations.
* ``OperationalObservation`` stores dated pre-election counts such as early
  voting marks and postal applications separately from final results.
* ``TurnoutDataset.validate`` enforces identities, arithmetic and complete
  vote-type partition totals before records reach analytical code.
"""

from dataclasses import dataclass, field
from datetime import date, datetime
import json
import os
from pathlib import Path
import re
import tempfile
from typing import List, Optional


class TurnoutDataError(ValueError):
    """Raised when normalized turnout evidence is incomplete or inconsistent."""


CANONICAL_VOTE_TYPES = frozenset({
    'election_day_ordinary',
    'early_in_person',
    'ordinary_combined',
    'early_combined',
    'absent',
    'declaration_early',
    'postal',
    'provisional',
    'enrolment',
    'enrolment_or_provisional',
    'mobile_or_institution',
    'telephone',
    'remote_electronic',
    'marked_as_voted',
    'declaration_combined',
    'other',
})

SOURCE_STATUSES = frozenset({'final', 'provisional', 'operational'})
PARTITION_COVERAGE = frozenset({'complete', 'partial'})
OPERATIONAL_GEOGRAPHY_BASES = frozenset({
    'administering_division',
    'elector_division',
    'national',
    'state',
})
OPERATIONAL_OBSERVATION_STATUSES = frozenset({
    'contemporaneous',
    'final_reconciled',
})
OPERATIONAL_COUNT_PRECISIONS = frozenset({'approximate', 'exact'})
OPERATIONAL_COUNT_RELATIONS = frozenset({
    'equal',
    'lower_bound',
    'upper_bound',
})
OPERATIONAL_COUNT_BASES = frozenset({'reported', 'forecast'})
OPERATIONAL_DERIVATIONS = frozenset({
    'direct',
    'rounded_rate_times_enrolment',
    'rounded_rate_times_reference_count',
    'sum_published_counts',
})
DERIVATIONS = frozenset({
    'direct',
    'sum_official_rows',
    'difference_official_total',
})

_ELECTION_CODE_PATTERN = re.compile(r'^[1-9][0-9]{3}[a-z]+$')
_IDENTIFIER_PATTERN = re.compile(r'^[a-z0-9][a-z0-9_.-]*$')
CURRENT_SCHEMA_VERSION = 5


def _require_text(value, field_name):
    if not isinstance(value, str) or not value.strip():
        raise TurnoutDataError('{} must be non-empty text'.format(field_name))


def _validate_election_code(value):
    if not isinstance(value, str) or not _ELECTION_CODE_PATTERN.fullmatch(value):
        raise TurnoutDataError(
            "election_code must use the compact form, such as '2025fed'"
        )


def _validate_identifier(value, field_name):
    if not isinstance(value, str) or not _IDENTIFIER_PATTERN.fullmatch(value):
        raise TurnoutDataError(
            '{} must contain only lower-case letters, numbers, dots, '
            'underscores or hyphens'.format(field_name)
        )


def _validate_optional_count(value, field_name):
    if value is not None and (isinstance(value, bool) or not isinstance(value, int)):
        raise TurnoutDataError('{} must be an integer or null'.format(field_name))
    if value is not None and value < 0:
        raise TurnoutDataError('{} cannot be negative'.format(field_name))


def _validate_ballot_counts(
    formal_votes,
    informal_votes,
    total_ballots,
    label,
    require_recorded_count=True,
):
    values = (formal_votes, informal_votes, total_ballots)
    if require_recorded_count and all(value is None for value in values):
        raise TurnoutDataError('{} has no recorded ballot count'.format(label))

    _validate_optional_count(formal_votes, '{} formal_votes'.format(label))
    _validate_optional_count(informal_votes, '{} informal_votes'.format(label))
    _validate_optional_count(total_ballots, '{} total_ballots'.format(label))

    if (
        formal_votes is not None
        and informal_votes is not None
        and total_ballots is not None
        and formal_votes + informal_votes != total_ballots
    ):
        raise TurnoutDataError(
            '{} formal and informal votes do not equal total ballots'.format(label)
        )
    if formal_votes is not None and total_ballots is not None:
        if formal_votes > total_ballots:
            raise TurnoutDataError(
                '{} formal votes exceed total ballots'.format(label)
            )
    if informal_votes is not None and total_ballots is not None:
        if informal_votes > total_ballots:
            raise TurnoutDataError(
                '{} informal votes exceed total ballots'.format(label)
            )


@dataclass(frozen=True)
class ElectionDefinition:
    election_code: str
    election_date: str
    jurisdiction: str

    def validate(self):
        _validate_election_code(self.election_code)
        _validate_identifier(self.jurisdiction, 'jurisdiction')
        try:
            date.fromisoformat(self.election_date)
        except (TypeError, ValueError):
            raise TurnoutDataError(
                '{} has an invalid ISO election_date'.format(self.election_code)
            )


@dataclass(frozen=True)
class SourceDefinition:
    source_id: str
    election_code: str
    authority: str
    locator: str
    adapter: str
    status: str
    category_regime: str
    notes: str = ''

    def validate(self):
        _validate_identifier(self.source_id, 'source_id')
        _validate_election_code(self.election_code)
        _require_text(self.authority, 'authority')
        _require_text(self.locator, 'locator')
        _validate_identifier(self.adapter, 'adapter')
        _validate_identifier(self.category_regime, 'category_regime')
        if self.status not in SOURCE_STATUSES:
            raise TurnoutDataError(
                '{} has unsupported source status {!r}'.format(
                    self.source_id, self.status
                )
            )


@dataclass(frozen=True)
class SeatTotal:
    election_code: str
    seat_name: str
    source_id: str
    enrolment: Optional[int]
    formal_votes: Optional[int]
    informal_votes: Optional[int]
    total_ballots: Optional[int]
    source_seat_id: str = ''
    subdivision: str = ''

    def validate(self):
        _validate_election_code(self.election_code)
        _require_text(self.seat_name, 'seat_name')
        _validate_identifier(self.source_id, 'source_id')
        if self.subdivision:
            _validate_identifier(self.subdivision, 'subdivision')
        _validate_optional_count(
            self.enrolment,
            '{} enrolment'.format(self.seat_name),
        )
        if (
            self.enrolment is None
            and self.formal_votes is None
            and self.informal_votes is None
            and self.total_ballots is None
        ):
            raise TurnoutDataError(
                '{} seat total has no recorded count'.format(self.seat_name)
            )
        _validate_ballot_counts(
            self.formal_votes,
            self.informal_votes,
            self.total_ballots,
            '{} seat total'.format(self.seat_name),
            require_recorded_count=False,
        )
        if (
            self.enrolment is not None
            and self.total_ballots is not None
            and self.total_ballots > self.enrolment
        ):
            raise TurnoutDataError(
                '{} total ballots exceed enrolment'.format(self.seat_name)
            )


@dataclass(frozen=True)
class VoteTypeRecord:
    election_code: str
    seat_name: str
    source_id: str
    partition_id: str
    source_category: str
    canonical_category: str
    formal_votes: Optional[int]
    informal_votes: Optional[int]
    total_ballots: Optional[int]
    coverage: str
    derivation: str = 'direct'

    def validate(self):
        _validate_election_code(self.election_code)
        _require_text(self.seat_name, 'seat_name')
        _validate_identifier(self.source_id, 'source_id')
        _validate_identifier(self.partition_id, 'partition_id')
        _require_text(self.source_category, 'source_category')
        if self.canonical_category not in CANONICAL_VOTE_TYPES:
            raise TurnoutDataError(
                '{} has unsupported canonical category {!r}'.format(
                    self.source_category, self.canonical_category
                )
            )
        if self.coverage not in PARTITION_COVERAGE:
            raise TurnoutDataError(
                '{} has unsupported coverage {!r}'.format(
                    self.source_category, self.coverage
                )
            )
        if self.derivation not in DERIVATIONS:
            raise TurnoutDataError(
                '{} has unsupported derivation {!r}'.format(
                    self.source_category, self.derivation
                )
            )
        _validate_ballot_counts(
            self.formal_votes,
            self.informal_votes,
            self.total_ballots,
            '{} {}'.format(self.seat_name, self.source_category),
        )


@dataclass(frozen=True)
class OperationalObservation:
    election_code: str
    source_id: str
    measure: str
    observed_at: str
    count: int
    geography_basis: str
    observation_status: str
    seat_name: str = ''
    source_category: str = ''
    count_precision: str = 'exact'
    count_relation: str = 'equal'
    count_basis: str = 'reported'
    derivation: str = 'direct'

    def validate(self):
        _validate_election_code(self.election_code)
        _validate_identifier(self.source_id, 'source_id')
        _validate_identifier(self.measure, 'measure')
        if isinstance(self.count, bool) or not isinstance(self.count, int):
            raise TurnoutDataError(
                '{} count must be an integer'.format(self.measure)
            )
        if self.count < 0:
            raise TurnoutDataError(
                '{} count cannot be negative'.format(self.measure)
            )
        if self.geography_basis not in OPERATIONAL_GEOGRAPHY_BASES:
            raise TurnoutDataError(
                '{} has unsupported geography_basis {!r}'.format(
                    self.measure, self.geography_basis
                )
            )
        if self.observation_status not in OPERATIONAL_OBSERVATION_STATUSES:
            raise TurnoutDataError(
                '{} has unsupported observation_status {!r}'.format(
                    self.measure, self.observation_status
                )
            )
        if self.count_precision not in OPERATIONAL_COUNT_PRECISIONS:
            raise TurnoutDataError(
                '{} has unsupported count_precision {!r}'.format(
                    self.measure, self.count_precision
                )
            )
        if self.count_relation not in OPERATIONAL_COUNT_RELATIONS:
            raise TurnoutDataError(
                '{} has unsupported count_relation {!r}'.format(
                    self.measure, self.count_relation
                )
            )
        if self.count_basis not in OPERATIONAL_COUNT_BASES:
            raise TurnoutDataError(
                '{} has unsupported count_basis {!r}'.format(
                    self.measure, self.count_basis
                )
            )
        if self.derivation not in OPERATIONAL_DERIVATIONS:
            raise TurnoutDataError(
                '{} has unsupported operational derivation {!r}'.format(
                    self.measure, self.derivation
                )
            )
        if (
            self.derivation.startswith('rounded_rate_times_')
            and self.count_precision != 'approximate'
        ):
            raise TurnoutDataError(
                '{} derived from a rounded rate must be approximate'.format(
                    self.measure
                )
            )
        if self.geography_basis in {
            'administering_division', 'elector_division'
        } and not self.seat_name:
            raise TurnoutDataError(
                '{} requires seat_name for {} geography'.format(
                    self.measure, self.geography_basis
                )
            )
        if self.geography_basis in {'national', 'state'} and self.seat_name:
            raise TurnoutDataError(
                '{} cannot use seat_name with {} geography'.format(
                    self.measure, self.geography_basis
                )
            )
        try:
            datetime.fromisoformat(self.observed_at)
        except (TypeError, ValueError):
            raise TurnoutDataError(
                '{} has an invalid ISO observed_at'.format(self.measure)
            )


@dataclass
class TurnoutDataset:
    elections: List[ElectionDefinition] = field(default_factory=list)
    sources: List[SourceDefinition] = field(default_factory=list)
    seat_totals: List[SeatTotal] = field(default_factory=list)
    vote_types: List[VoteTypeRecord] = field(default_factory=list)
    operational_observations: List[OperationalObservation] = field(
        default_factory=list
    )

    def validate(self):
        """Validate source identity and final-result arithmetic."""
        for records in (
            self.elections,
            self.sources,
            self.seat_totals,
            self.vote_types,
            self.operational_observations,
        ):
            for record in records:
                record.validate()

        elections = self._unique_by(
            self.elections,
            lambda record: record.election_code,
            'election',
        )
        sources = self._unique_by(
            self.sources,
            lambda record: record.source_id,
            'source',
        )
        seats = self._unique_by(
            self.seat_totals,
            lambda record: (record.election_code, record.seat_name),
            'seat total',
        )

        for source in self.sources:
            if source.election_code not in elections:
                raise TurnoutDataError(
                    '{} references unknown election {}'.format(
                        source.source_id, source.election_code
                    )
                )

        for record in self.seat_totals:
            source = self._validate_record_source(record, sources)
            if source.status == 'operational':
                raise TurnoutDataError(
                    '{} cannot supply final seat totals'.format(source.source_id)
                )

        vote_type_keys = set()
        partitions = {}
        for record in self.vote_types:
            source = self._validate_record_source(record, sources)
            if source.status == 'operational':
                raise TurnoutDataError(
                    '{} cannot supply final vote types'.format(source.source_id)
                )
            seat_key = (record.election_code, record.seat_name)
            if seat_key not in seats:
                raise TurnoutDataError(
                    '{} references an unknown seat total'.format(seat_key)
                )
            key = (
                record.election_code,
                record.seat_name,
                record.partition_id,
                record.source_category,
            )
            if key in vote_type_keys:
                raise TurnoutDataError(
                    'duplicate vote-type record {}'.format(key)
                )
            vote_type_keys.add(key)
            partition_key = key[:3]
            partitions.setdefault(partition_key, []).append(record)

        for partition_key, records in partitions.items():
            self._validate_partition(partition_key, records, seats)

        observation_keys = set()
        elections_with_seat_totals = {
            record.election_code for record in self.seat_totals
        }
        for record in self.operational_observations:
            source = self._validate_record_source(record, sources)
            if source.status != 'operational':
                raise TurnoutDataError(
                    '{} cannot supply operational observations'.format(
                        source.source_id
                    )
                )
            if record.seat_name:
                seat_key = (record.election_code, record.seat_name)
                if (
                    record.election_code in elections_with_seat_totals
                    and seat_key not in seats
                ):
                    raise TurnoutDataError(
                        '{} references an unknown seat total'.format(seat_key)
                    )
            key = (
                record.election_code,
                record.source_id,
                record.measure,
                record.observed_at,
                record.seat_name,
                record.source_category,
                record.geography_basis,
                record.observation_status,
            )
            if key in observation_keys:
                raise TurnoutDataError(
                    'duplicate operational observation {}'.format(key)
                )
            observation_keys.add(key)

    @staticmethod
    def _unique_by(records, key_function, label):
        indexed = {}
        for record in records:
            key = key_function(record)
            if key in indexed:
                raise TurnoutDataError('duplicate {} {}'.format(label, key))
            indexed[key] = record
        return indexed

    @staticmethod
    def _validate_record_source(record, sources):
        source = sources.get(record.source_id)
        if source is None:
            raise TurnoutDataError(
                '{} references unknown source {}'.format(
                    record.election_code, record.source_id
                )
            )
        if source.election_code != record.election_code:
            raise TurnoutDataError(
                '{} belongs to {}, not {}'.format(
                    record.source_id,
                    source.election_code,
                    record.election_code,
                )
            )
        return source

    @staticmethod
    def _validate_partition(partition_key, records, seats):
        coverage_values = {record.coverage for record in records}
        if len(coverage_values) != 1:
            raise TurnoutDataError(
                '{} mixes complete and partial coverage'.format(partition_key)
            )
        if coverage_values != {'complete'}:
            return

        if any(record.formal_votes is None for record in records):
            raise TurnoutDataError(
                '{} is complete but has an unknown formal count'.format(
                    partition_key
                )
            )
        seat_total = seats[partition_key[:2]]
        if seat_total.formal_votes is None:
            raise TurnoutDataError(
                '{} is complete but its seat formal total is unknown'.format(
                    partition_key
                )
            )
        partition_total = sum(record.formal_votes for record in records)
        if partition_total != seat_total.formal_votes:
            raise TurnoutDataError(
                '{} formal votes total {}, expected {}'.format(
                    partition_key,
                    partition_total,
                    seat_total.formal_votes,
                )
            )


def dataset_to_dict(dataset):
    """Return the stable JSON representation of a validated dataset."""
    dataset.validate()

    def record_dict(record):
        values = dict(record.__dict__)
        if isinstance(record, OperationalObservation):
            if record.count_precision == 'exact':
                values.pop('count_precision')
            if record.count_relation == 'equal':
                values.pop('count_relation')
            if record.count_basis == 'reported':
                values.pop('count_basis')
            if record.derivation == 'direct':
                values.pop('derivation')
        return values

    return {
        'schema_version': CURRENT_SCHEMA_VERSION,
        'elections': [record_dict(record) for record in dataset.elections],
        'sources': [record_dict(record) for record in dataset.sources],
        'seat_totals': [record_dict(record) for record in dataset.seat_totals],
        'vote_types': [record_dict(record) for record in dataset.vote_types],
        'operational_observations': [
            record_dict(record) for record in dataset.operational_observations
        ],
    }


def dataset_from_dict(payload):
    """Load and validate a supported JSON representation."""
    if not isinstance(payload, dict):
        raise TurnoutDataError('turnout dataset must be a JSON object')
    schema_version = payload.get('schema_version')
    if schema_version not in {1, 2, 3, 4, CURRENT_SCHEMA_VERSION}:
        raise TurnoutDataError('unsupported turnout dataset schema_version')
    expected_keys = {
        'schema_version',
        'elections',
        'sources',
        'seat_totals',
        'vote_types',
        'operational_observations',
    }
    unexpected_keys = set(payload) - expected_keys
    if unexpected_keys:
        raise TurnoutDataError(
            'unexpected turnout dataset fields: {}'.format(
                ', '.join(sorted(unexpected_keys))
            )
        )
    missing_keys = expected_keys - set(payload)
    if missing_keys:
        raise TurnoutDataError(
            'missing turnout dataset fields: {}'.format(
                ', '.join(sorted(missing_keys))
            )
        )

    if schema_version == 1 and any(
        'geography_basis' not in row or 'observation_status' not in row
        for row in payload['operational_observations']
    ):
        raise TurnoutDataError(
            'schema version 1 operational observations do not identify '
            'geography or reconciliation status; regenerate this dataset'
        )

    try:
        dataset = TurnoutDataset(
            elections=[ElectionDefinition(**row) for row in payload['elections']],
            sources=[SourceDefinition(**row) for row in payload['sources']],
            seat_totals=[SeatTotal(**row) for row in payload['seat_totals']],
            vote_types=[VoteTypeRecord(**row) for row in payload['vote_types']],
            operational_observations=[
                OperationalObservation(**row)
                for row in payload['operational_observations']
            ],
        )
    except (TypeError, AttributeError) as error:
        raise TurnoutDataError(
            'invalid turnout dataset record: {}'.format(error)
        )
    dataset.validate()
    return dataset


def load_dataset(path):
    """Load one normalized turnout JSON file."""
    try:
        with open(path, encoding='utf-8') as source:
            payload = json.load(source)
    except (OSError, json.JSONDecodeError) as error:
        raise TurnoutDataError('could not read {}: {}'.format(path, error))
    return dataset_from_dict(payload)


def write_dataset_atomically(path, dataset):
    """Validate and replace one normalized turnout JSON file atomically."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = dataset_to_dict(dataset)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix='.{}-'.format(path.name),
        suffix='.tmp',
        dir=str(path.parent),
    )
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8', newline='\n') as output:
            json.dump(payload, output, indent=2, sort_keys=True)
            output.write('\n')
        os.replace(temporary_name, str(path))
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)
