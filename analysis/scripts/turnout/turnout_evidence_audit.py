"""Describe the coverage and comparability of published turnout count evidence.

This command reads the normalized turnout datasets, selects precise pre-election
operational counts, and records final comparison targets and unresolved matches.
It writes one report and one JSON manifest; it never changes model inputs.
"""

import argparse
from collections import Counter, defaultdict
from dataclasses import asdict
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path
import re

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.shared import turnout_data
from lib.turnout import category_policy as policy


INPUT_DIRECTORY = ANALYSIS_DIRECTORY / 'Data/Turnout'
OUTPUT_DIRECTORY = REPOSITORY_DIRECTORY / 'docs/turnout-evidence-audit'
AUDIT_VERSION = 1

# These curated district tables publish rates to one decimal place. Their
# serialized values sometimes omit a trailing zero (e.g. 17.0 is shown as 17),
# so a whole-number display in these particular tables retains 0.1pp precision.
ONE_DECIMAL_TABLES = frozenset({
    'antony-green-2021wa-election-eve',
    'antony-green-2022sa-election-eve',
    'antony-green-2022vic-election-eve',
    'antony-green-2023nsw-election-eve',
})
RATE_PATTERN = re.compile(r'(\d+(?:\.\d+)?)% of (?:enrolment|[\d,]+ dispatched postal packs)')
EARLY_MEASURES = frozenset({
    'prepoll_votes_cast_cumulative', 'prepoll_votes_issued_cumulative',
    'early_in_person_attendance_cumulative',
})
POSTAL_MEASURES = frozenset({
    'postal_applications_cumulative', 'postal_ballots_issued_cumulative',
    'postal_votes_returned_cumulative', 'postal_ballots_returned_cumulative',
    'postal_votes_accepted_cumulative',
})


def precision_decision(observation):
    """Admit only reported point counts or documented rates at <=0.1pp precision."""
    if observation.count_basis != 'reported':
        return 'forecast', None
    if observation.count_relation != 'equal':
        return 'bound', None
    if observation.count_precision == 'exact':
        return 'exact', None
    if observation.derivation.startswith('rounded_rate_times_'):
        match = RATE_PATTERN.search(observation.source_category)
        if match:
            rate = Decimal(match.group(1))
            quantum = Decimal(10) ** rate.as_tuple().exponent
            if observation.source_id in ONE_DECIMAL_TABLES:
                quantum = min(quantum, Decimal('0.1'))
            if quantum <= Decimal('0.1'):
                return 'close_rate', float(quantum)
    return 'coarse_or_unspecified_approximation', None


def latest_controls(observations, election_date):
    """Keep a source's last precise control through polling day, including revisions.

    Daily cumulative rows are alternatives, not repeated calibration samples.
    Sources, geographic bases and reconciliation statuses remain separate so
    consumers cannot accidentally pool overlapping or retrospective data.
    """
    latest, decisions = {}, Counter()
    for observation in observations:
        reason, quantum = precision_decision(observation)
        if reason not in {'exact', 'close_rate'}:
            decisions[reason] += 1
            continue
        if observation.observed_at[:10] > election_date:
            decisions['after_polling_day'] += 1
            continue
        decisions[reason] += 1
        key = (observation.source_id, observation.measure,
               observation.geography_basis, observation.seat_name,
               observation.observation_status)
        # ISO dates are normalized by ingestion. Within these source series the
        # time-zone representation is consistent; source timestamps stay intact.
        if key not in latest or observation.observed_at > latest[key][0].observed_at:
            latest[key] = (observation, quantum)
    decisions['superseded_precise_rows'] = (
        decisions['exact'] + decisions['close_rate'] - len(latest))
    return [latest[key] for key in sorted(latest)], dict(decisions)


def target_definition(observation, jurisdiction, categories):
    """Return only reviewed target pools; expose gaps rather than inventing splits."""
    measure = observation.measure
    if observation.election_code == '2022vic' and observation.geography_basis == 'state':
        return [], '', 'coverage_mismatch', (
            'State operational controls and the final 87-district sample have '
            'different coverage; Narracan is excluded from the final sample. '
            'Use the matched district controls for conversion diagnostics.')
    if observation.geography_basis == 'administering_division':
        return [], '', 'aggregate_only', (
            'Attendance belongs to the administering district. Aggregate at the '
            'election level; elector-seat conversion needs a crosswalk.')
    if not categories:
        return [], '', 'missing_final', 'No normalized final category evidence.'
    if measure in POSTAL_MEASURES:
        if 'postal' in categories:
            return ['postal'], '', 'supported', (
                'Final postal pool; applications/issues/returns/accepted votes '
                'remain separate stages. A formal-count ratio is a combined '
                'conversion, not a pure acceptance probability.')
        return [], '', 'combined_only', 'Postal votes are inside an unsplit declaration pool.'
    if measure == 'early_and_postal_votes_recorded_cumulative':
        if {'early_combined', 'postal'} <= categories:
            return ['early_combined', 'postal'], '', 'supported', (
                'Combined early/postal conversion only; do not split the control.')
    if measure in EARLY_MEASURES:
        if jurisdiction == 'sa':
            if observation.election_code == '2026sa' and {'early_in_person', 'declaration_early'} <= categories:
                return ['early_in_person', 'declaration_early'], '', 'supported', (
                    'Final early pool includes named EVCs, early absent-ordinary '
                    'votes and separate early declarations.')
            return [], '', 'combined_only', 'Early votes are inside an unsplit declaration pool.'
        if jurisdiction == 'fed':
            return [], '', 'needs_split', 'Own-division early votes are inside OrdinaryVotes.'
        if jurisdiction == 'nsw' and observation.election_code == '2015nsw':
            return ['early_combined'], 'Total Pre-Poll Ordinary Votes', 'supported', (
                'Match the named pre-poll ordinary source rows; exclude other early modes.')
        if jurisdiction in {'vic', 'nsw'} and 'early_combined' in categories:
            return ['early_combined'], '', 'supported', (
                'Empirical conversion to the published combined early pool; '
                'not a pure in-person acceptance/formality rate.')
        if jurisdiction == 'wa' and observation.election_code == '2025wa':
            return ['early_in_person'], '', 'needs_definition_review', (
                'The named early-polling-place split does not identify early absent votes. '
                'The final absent pool has no early/polling-day split, so it cannot yet '
                'be matched to the complete operational early-vote count.')
        if jurisdiction == 'wa' and 'early_in_person' in categories:
            return ['early_in_person'], '', 'supported', 'Final in-person early pool.'
        if jurisdiction == 'qld':
            return ['early_in_person', 'declaration_early'], '', 'needs_definition_review', (
                'Published counts have unverified coverage of absent early voters and smaller early modes. '
                'These categories are not certified as a comparable target.')
    return [], '', 'different_stage', (
        'Scrutiny-ready or other operational quantity has no reviewed final-category target.')


def final_target(dataset, observation, categories, source_category):
    """Attach available target quantities without treating missing ballot data as zero."""
    sources = {source.source_id for source in dataset.sources if source.status == 'final'}
    records = [row for row in dataset.vote_types if row.source_id in sources
               and (not observation.seat_name or row.seat_name == observation.seat_name)]
    expected_seats = {row.seat_name for row in dataset.seat_totals if row.source_id in sources
                      and (not observation.seat_name or row.seat_name == observation.seat_name)}
    partitions = defaultdict(set)
    for row in records:
        partitions[row.seat_name].add(row.partition_id)
    if any(len(ids) != 1 for ids in partitions.values()):
        return None, 'Alternative final partitions need explicit selection.'
    selected = [row for row in records if row.canonical_category in categories
                and (not source_category or row.source_category == source_category)]
    if not selected:
        return None, 'No matching final category rows for this geography.'
    # A complete geographic match is necessary even for a single target pool.
    # Missing rows under partial partitions cannot stand for observed zeros.
    if {row.seat_name for row in selected} != expected_seats:
        return None, 'Target category coverage is incomplete across final districts.'
    unreliable = {r['seat'] for r in policy.missing_count_review(dataset)}
    if any(row.seat_name in unreliable for row in selected):
        return None, 'Target belongs to a reviewed unreliable district category split.'
    counts = {}
    for field in ('formal_votes', 'informal_votes', 'total_ballots'):
        values = [getattr(row, field) for row in selected]
        counts[field] = sum(values) if all(value is not None for value in values) else None
    if all(value is None for value in counts.values()):
        return None, 'Target quantities are unavailable.'
    return dict(counts, records=[dict(source_id=row.source_id,
                                    seat_name=row.seat_name,
                                    partition_id=row.partition_id,
                                    source_category=row.source_category)
                                for row in selected]), ''


def source_fingerprint(paths, payloads=None):
    """Fingerprint refreshed normalized evidence, ignoring JSON formatting changes."""
    inputs = {path.name: hashlib.sha256(json.dumps(
        payloads[path.name] if payloads is not None else json.loads(path.read_text(encoding='utf-8')), sort_keys=True,
        separators=(',', ':'), ensure_ascii=False).encode('utf-8')).hexdigest()
              for path in paths}
    code_paths = [Path(__file__), Path(turnout_data.__file__), Path(policy.__file__)]
    code = {path.name: hashlib.sha256(path.read_text(encoding='utf-8').encode('utf-8')).hexdigest()
            for path in code_paths}
    return dict(inputs=inputs, code=code)


def audit_election(dataset):
    """Build the availability/category manifest for one validated election."""
    missing_review = policy.missing_count_review(dataset)
    dataset = policy.apply_missing_count_policy(dataset)
    election = dataset.elections[0]
    final_sources = {source.source_id for source in dataset.sources if source.status == 'final'}
    final_rows = [row for row in dataset.vote_types if row.source_id in final_sources]
    totals = [row for row in dataset.seat_totals if row.source_id in final_sources]
    categories = {row.canonical_category for row in final_rows}
    latest, decisions = latest_controls(dataset.operational_observations, election.election_date)
    seats = {row.seat_name: row for row in totals}
    # Summarize rejected evidence by source and measure, without exporting the
    # thousands of daily rows again. The original datasets retain those rows.
    excluded = defaultdict(list)
    for observation in dataset.operational_observations:
        reason, _ = precision_decision(observation)
        if reason in {'exact', 'close_rate'}:
            if observation.observed_at[:10] <= election.election_date:
                continue
            reason = 'after_polling_day'
        excluded[(observation.source_id, observation.measure, reason)].append(observation)
    exclusions = [dict(source_id=source, measure=measure, reason=reason, rows=len(rows),
                       first_date=min(row.observed_at for row in rows),
                       last_date=max(row.observed_at for row in rows),
                       example=rows[0].source_category)
                  for (source, measure, reason), rows in sorted(excluded.items())]
    controls = []
    for observation, quantum in latest:
        pool, category, status, reason = target_definition(observation, election.jurisdiction, categories)
        target = None
        if status == 'supported':
            target, problem = final_target(dataset, observation, pool, category)
            if problem:
                status, reason = 'missing_target', problem
        record = dict(asdict(observation), inclusion_reason=(
            'Exact reported point count.' if quantum is None else
            'Reported rate rounded to at most 0.1 percentage point.'),
            rate_precision_pp=quantum, comparison_status=status,
            election_fold=observation.election_code,
            alternative_control_group=' / '.join((observation.election_code, observation.measure,
                observation.geography_basis, observation.seat_name or 'all')),
            target_categories=pool, target_source_category=category,
            comparison_reason=reason, final_target=target,
            availability=('publication_time_unverified' if observation.observation_status == 'contemporaneous'
                          else 'later_reconciled_not_historical_as_of'),
            publication_available_at=None)
        # Keep a simple rounding bound for reconstructed counts. It describes
        # publication rounding only, not application acceptance or model error.
        if quantum is not None:
            if observation.derivation == 'rounded_rate_times_enrolment':
                seat = seats.get(observation.seat_name)
                reference = seat.enrolment if seat else None
            else:
                match = re.search(r'of ([\d,]+) dispatched postal packs', observation.source_category)
                reference = int(match.group(1).replace(',', '')) if match else None
            record['rounding_reference_count'] = reference
            record['rounding_half_width_count'] = (
                math.ceil(reference * quantum / 200 + 0.5) if reference is not None else None)
        controls.append(record)
    return dict(asdict(election), sources=[asdict(source) for source in dataset.sources],
                reviewed_missing_counts=missing_review,
                final_seats=len(totals), final_categories=sorted(categories),
                final_totals={field: sum(getattr(row, field) for row in totals)
                              if totals and all(getattr(row, field) is not None for row in totals) else None
                              for field in ('enrolment', 'formal_votes', 'informal_votes', 'total_ballots')},
                final_category_regimes=sorted({source.category_regime for source in dataset.sources
                                              if source.status == 'final'}),
                final_seats_with_all_totals=sum(all(getattr(row, field) is not None for field in
                    ('enrolment', 'formal_votes', 'informal_votes', 'total_ballots')) for row in totals),
                category_rows=len(final_rows),
                missing_category_seats=sorted(set(seats) - {row.seat_name for row in final_rows}),
                partial_partition_seats=sorted({row.seat_name for row in final_rows if row.coverage == 'partial'}),
                operational_rows=len(dataset.operational_observations),
                selection_counts=decisions, exclusions=exclusions, controls=controls,
                boundary_quality='Unadjusted: same names do not establish boundary continuity.')


def render_report(audit):
    """Summarize coverage and the actual comparison units, with full rows in JSON."""
    elections = audit['elections']
    controls = [row for election in elections for row in election['controls']]
    decisions = Counter()
    for election in elections:
        decisions.update(election['selection_counts'])
    lines = ['# Turnout evidence audit', '',
             'This report describes the published vote and application counts available for '
             'turnout analysis. It helps readers understand which counts can be compared with '
             'final election results and where the evidence has gaps.', '',
             'The audit reads the normalized election datasets, selects exact counts and closely '
             'approximate counts, and checks their category and geographic coverage. It records '
             'the latest eligible count from each source and the final-result quantities that '
             'describe the same voting pool. Reproduction commands appear below.', '',
             'Generated {}.'.format(audit['generated_at']), '',
             '{} datasets; {} elections with final seat evidence; {} operational rows; {} latest precise controls.'.format(
                 len(elections), sum(e['final_seats'] > 0 for e in elections),
                 sum(e['operational_rows'] for e in elections), len(controls)), '',
             '## Inclusion rule', '',
             'Include reported exact point counts and counts reconstructed from published rates rounded to '
             '0.1 percentage point or finer. Exclude forecasts, bounds and loose/unspecified approximations. '
             'Retain the last eligible observation per source, measure, geography, district and reconciliation '
             'status through polling day. Later rows stay in the source dataset but do not enter the pre-election controls.', '',
             'The four reviewed one-decimal district tables retain that precision when trailing zeros disappear '
             'in ingestion. The JSON records rate precision, reference count and a conservative rounding half-width. '
             'Final-category quantities retain missing values as null.', '',
             '| Selection | Rows |', '| --- | ---: |']
    lines += ['| {} | {} |'.format(reason, count) for reason, count in sorted(decisions.items())]
    lines += ['', 'Exact/close-rate row counts above include superseded daily observations. Controls below are '
              'alternative source snapshots, not independent samples. Never add a state total to its districts '
              'or pool duplicate source reports. Applications, issues, returns, accepted and scrutiny-ready '
              'counts remain separate measures.', '', '## Final evidence coverage', '',
              '| Election | Final seats (all totals) | Category rows | Partial / missing category seats | Latest controls (close rate) | Final categories |',
              '| --- | ---: | ---: | ---: | ---: | --- |']
    for election in elections:
        lines.append('| {} | {} ({}) | {} | {} / {} | {} ({}) | {} |'.format(
            election['election_code'], election['final_seats'], election['final_seats_with_all_totals'],
            election['category_rows'], len(election['partial_partition_seats']),
            len(election['missing_category_seats']), len(election['controls']),
            sum(row['count_precision'] == 'approximate' for row in election['controls']),
            ', '.join(election['final_categories']) or 'None'))
    lines += ['', 'Districts with final totals but no category partition:']
    lines += ['- {}: {}.'.format(election['election_code'], ', '.join(election['missing_category_seats']))
              for election in elections if election['missing_category_seats']]
    lines += ['', '## Excluded controls', '',
              'Post-election operational quantities remain available for later progression/acceptance '
              'diagnostics, but do not substitute for a pre-election count. In particular, QLD 2024’s '
              'final postal workbook is dated after polling; its district issue/return/acceptance rows '
              'are not election-eve controls.', '',
              '| Election / source | Measure | Reason | Rows | Quantity dates | Example label |',
              '| --- | --- | --- | ---: | --- | --- |']
    for election in elections:
        for row in election['exclusions']:
            lines.append('| {} / {} | {} | {} | {} | {} to {} | {} |'.format(
                election['election_code'], row['source_id'], row['measure'], row['reason'],
                row['rows'], row['first_date'], row['last_date'], row['example'].replace('|', '/')))
    lines += ['', '## Comparison and availability manifest', '',
              'Each row groups controls with the same comparison definition. District counts in a row '
              'share an election fold. Final formal/ballot targets and their source-row identities are '
              'in `audit.json`; no conversion ratios have been fitted.', '',
              '“Supported” identifies a defensible target for conversion diagnostics. It does not certify '
              'historical availability: contemporaneous records have unverified publication times, while '
              'final-reconciled records are retrospective. `observed_at` is a quantity date, not a publication date.', '',
              '| Election / source | Measure | Geography / status | Latest quantity dates | Controls | Target / decision |',
              '| --- | --- | --- | --- | ---: | --- |']
    groups = defaultdict(list)
    for row in controls:
        key = (row['election_code'], row['source_id'], row['measure'], row['geography_basis'],
               row['observation_status'], row['comparison_status'], row['comparison_reason'])
        groups[key].append(row)
    folds = defaultdict(set)
    for key, rows in sorted(groups.items()):
        code, source, measure, geography, reconciliation, status, reason = key
        dates = sorted({row['observed_at'] for row in rows})
        lines.append('| {} / {} | {} | {} / {} | {} | {} | {}: {} |'.format(
            code, source, measure, geography, reconciliation,
            dates[0] if len(dates) == 1 else dates[0] + ' to ' + dates[-1], len(rows), status, reason))
        if status == 'supported':
            folds[(measure, geography)].add(code)
    lines += ['', '## Independent conversion evidence', '',
              'Counts below count elections once per measure/geography, regardless of district count or source count. '
              'These are diagnostic folds, including retrospective evidence; they are not a backtest availability guarantee.', '',
              '| Measure | Geography | Elections | Codes |', '| --- | --- | ---: | --- |']
    lines += ['| {} | {} | {} | {} |'.format(measure, geography, len(codes), ', '.join(sorted(codes)))
              for (measure, geography), codes in sorted(folds.items())]
    lines += ['', '## Overlap and aggregate residuals', '',
              'Published parent totals and district sums are kept as alternatives. These residuals are '
              'reported only for the same source, measure, date and reconciliation status; districts with '
              'different latest dates are not summed. Small residuals do not warrant invented district allocations.', '',
              'VIC 2022’s final district sample excludes Narracan; its statewide operational totals and '
              'the 87-district sample therefore have different coverage. Do not force their residual into '
              'the retained districts.', '',
              '| Election / source | Measure / date | District sum | Published parent | Parent minus districts |',
              '| --- | --- | ---: | ---: | ---: |']
    parents = {(row['election_code'], row['source_id'], row['measure'], row['observed_at'], row['observation_status']): row
               for row in controls if row['geography_basis'] in {'state', 'national'}}
    district_groups = defaultdict(list)
    district_names = defaultdict(set)
    for row in controls:
        if row['geography_basis'] == 'elector_division':
            district_groups[(row['election_code'], row['source_id'], row['measure'],
                             row['observed_at'], row['observation_status'])].append(row)
            district_names[(row['election_code'], row['source_id'], row['measure'],
                            row['observation_status'])].add(row['seat_name'])
    for key, rows in sorted(district_groups.items()):
        if key in parents and {row['seat_name'] for row in rows} == district_names[key[:3] + key[4:]]:
            parent = parents[key]
            total = sum(row['count'] for row in rows)
            lines.append('| {} / {} | {} / {} | {} | {} | {} |'.format(
                key[0], key[1], key[2], key[3], total, parent['count'], parent['count'] - total))
    sa = next((election for election in elections if election['election_code'] == '2026sa'), None)
    if sa and sa['final_seats']:
        totals = sa['final_totals']
        lines += ['', '## South Australia 2026 final evidence', '',
                  'The dataset contains reviewed final first-preference counts for all 47 districts '
                  'from the [ECSA results website](https://result.ecsa.sa.gov.au/). District totals '
                  'combine polling places, declaration batches and absent-ordinary batches once each. '
                  'They total {:,} enrolled electors, {:,} formal votes, {:,} informal votes and {:,} ballots.'.format(
                      *(totals[field] for field in ('enrolment', 'formal_votes', 'informal_votes', 'total_ballots'))), '',
                  'Early-vote comparisons include named early-voting centres, early absent-ordinary '
                  'votes and the separate early declaration category. Postal votes have their own '
                  'observed final category. Older SA declaration totals remain combined.', '',
                  'Reviewed implausible zeros leave category counts missing in some districts. '
                  'Those district splits are excluded from final-category comparisons because '
                  'the votes may have been recorded elsewhere. District totals remain usable; '
                  'the local manifest records the affected groups and their published source labels.', '',
                  'ECSA acknowledges residual differences between some count stages in its '
                  '[results-review statement](https://ecsa.sa.gov.au/se2026news/se2026-results-review-complete). '
                  'This dataset uses first preferences; TCP and preference-distribution counts are '
                  'separate stages and do not replace its turnout totals. The source revision and '
                  'raw-source hashes are recorded in the normalized dataset.']
    lines += ['', '## Coverage limitations', '',
              'Historical same-name seat comparisons are unadjusted for redistribution. '
              'This audit does not establish boundary continuity or an official geography crosswalk. '
              'Federal early counts from 2010 lack a comparable final early-only category in '
              'the normalized ordinary results. Attendance recorded by the administering district '
              'cannot be treated as turnout of electors enrolled in that district.', '',
              '## Refresh and provenance', '',
              'The manifest retains source URLs, adapters, category regimes, observation dates/statuses '
              'and semantic SHA-256 hashes of each normalized input. Refresh the relevant ingestion adapter '
              'when a source changes, then rerun this command. `--check` detects changed counts/metadata, '
              'added or removed datasets, and changes to the audit/loader code. Formatting alone does not '
              'invalidate the input hash. It does not poll remote sources or reconstruct unavailable publications.', '',
              'Run from `analysis/`:', '', '```powershell',
              '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_evidence_audit',
              '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_evidence_audit --check',
              '```', '']
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-directory', type=Path, default=INPUT_DIRECTORY)
    parser.add_argument('--output-directory', type=Path, default=OUTPUT_DIRECTORY)
    parser.add_argument('--check', action='store_true', help='check existing manifest freshness without rewriting')
    args = parser.parse_args(argv)
    paths = sorted(args.input_directory.glob('*.json'))
    if not paths:
        parser.error('No normalized turnout datasets found.')
    # Read each source once so a refresh during this run cannot give the manifest
    # a fingerprint for different counts from those actually audited.
    payloads = {path.name: json.loads(path.read_text(encoding='utf-8')) for path in paths}
    fingerprint = source_fingerprint(paths, payloads)
    manifest = args.output_directory / 'audit.json'
    if args.check:
        if not manifest.exists():
            print('No audit manifest; generate it first.')
            return 1
        previous = json.loads(manifest.read_text(encoding='utf-8'))
        if previous.get('fingerprint') != fingerprint:
            print('Audit is stale: normalized evidence or audit/loader code changed. Regenerate the audit.')
            return 1
        print('Audit is current against the normalized local evidence.')
        return 0
    # The existing schema performs arithmetic/partition validation. This audit
    # adds selection and comparison decisions without another validation layer.
    elections = []
    for path in paths:
        dataset = turnout_data.dataset_from_dict(payloads[path.name])
        if len(dataset.elections) != 1:
            parser.error('{} must describe exactly one election.'.format(path))
        elections.append(dict(audit_election(dataset), input_file=str(path.resolve()),
                              normalized_snapshot_sha256=fingerprint['inputs'][path.name]))
    audit = dict(audit_version=AUDIT_VERSION, generated_at=datetime.now(timezone.utc).isoformat(),
                 input_directory=str(args.input_directory.resolve()), fingerprint=fingerprint,
                 elections=elections)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps(audit, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    report = args.output_directory / 'report.md'
    report.write_text(render_report(audit), encoding='utf-8')
    print('Audited {} datasets. Report: {}'.format(len(elections), report))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
