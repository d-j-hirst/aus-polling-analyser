"""Compare published early/postal counts with matched final formal vote pools.

One compact, non-Stan analysis produces conversion diagnostics, whole-election
predictions and pooled candidate parameters. It reuses the evidence audit's
precision/category rules and does not change live forecast assumptions.
The separate turnout_operational_report module formats completed results.
"""

import argparse
from collections import Counter, defaultdict
from datetime import date, datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import statistics

from lib.paths import REPOSITORY_DIRECTORY
from lib.shared import turnout_data
from lib.turnout import category_policy as policy
from scripts.turnout import turnout_evidence_audit as audit
from scripts.turnout import turnout_federal_prepoll as federal_source
from scripts.turnout import turnout_operational_report as reporting


OUTPUT_DIRECTORY = REPOSITORY_DIRECTORY / 'docs/turnout-operational-calibration'
MODELS = ('pooled', 'kind_pooled', 'previous_conversion', 'previous_category')
SCHEMES = ('leave_one_out', 'earlier_only')
Z80 = 1.2815515655446004


def mean(values):
    values = list(values)
    return statistics.mean(values) if values else None


def family(measure):
    # These two names describe the same postal-return stage. Other stages and
    # the joint early/postal measurement retain independent conversion factors.
    return measure.replace('postal_ballots_returned', 'postal_votes_returned')


def load_inputs(directory):
    """Read one consistent local snapshot and reuse the normalized validation."""
    paths = sorted(directory.glob('*.json'))
    if not paths:
        raise turnout_data.TurnoutDataError('No normalized turnout datasets found.')
    payloads = {p.name: json.loads(p.read_text(encoding='utf-8')) for p in paths}
    datasets = {p.stem: turnout_data.dataset_from_dict(payloads[p.name]) for p in paths}
    if any(len(d.elections) != 1 for d in datasets.values()):
        raise turnout_data.TurnoutDataError('Each dataset must identify one election.')
    fingerprint = audit.source_fingerprint(paths, payloads)
    for code_path in (Path(__file__), Path(reporting.__file__)):
        fingerprint['code'][code_path.name] = hashlib.sha256(
            code_path.read_text(encoding='utf-8').encode()).hexdigest()
    if federal_source.OUTPUT.exists():
        fingerprint['inputs']['FederalPrepoll/final.json'] = hashlib.sha256(json.dumps(
            json.loads(federal_source.OUTPUT.read_text(encoding='utf-8')), sort_keys=True,
            separators=(',', ':'), ensure_ascii=False).encode()).hexdigest()
        fingerprint['code'][Path(federal_source.__file__).name] = hashlib.sha256(
            Path(federal_source.__file__).read_text(encoding='utf-8').encode()).hexdigest()
    return datasets, fingerprint


def comparison_rows(datasets, include_nsw=False, include_unknown_targets=False):
    """Choose one non-overlapping control series per election and measure.

    District controls are retained as prediction cases; their sum forms one
    election conversion observation. Published parent totals and alternative
    sources never become extra training observations. Prediction construction
    may also retain controls whose final category split is unknown; these have
    null targets and cannot enter conversion estimation or accuracy scores.
    """
    rows, excluded = [], Counter()
    for code, dataset in sorted(datasets.items()):
        evidence = audit.audit_election(dataset)
        definition = dataset.elections[0]
        totals = {r.seat_name: r for r in dataset.seat_totals}
        sources = {s.source_id: s for s in dataset.sources}
        candidates = defaultdict(list)
        for control in evidence['controls']:
            prediction_only = (include_unknown_targets and control['comparison_status'] == 'missing_target'
                               and bool(control['target_categories']))
            if control['comparison_status'] != 'supported' and not prediction_only:
                excluded[control['comparison_status']] += 1
                continue
            if (not control['final_target'] or control['final_target']['formal_votes'] is None) and not prediction_only:
                excluded['missing_formal_target'] += 1
                continue
            key = (family(control['measure']), control['source_id'],
                   control['observation_status'], control['geography_basis'])
            candidates[key].append(control)
        measures = sorted({key[0] for key in candidates})
        for measure in measures:
            alternatives = [(key, controls) for key, controls in candidates.items() if key[0] == measure]
            def preference(item):
                key, controls = item
                official = 'commission' in sources[key[1]].authority.lower()
                return (key[2] != 'contemporaneous', key[3] != 'elector_division',
                        not official, -len(controls), key)
            selected_key, selected = min(alternatives, key=preference)
            excluded['alternative_source_or_parent'] += sum(
                len(controls) for key, controls in alternatives if key != selected_key)
            for control in selected:
                target_record = control['final_target']
                members = (sorted({r['seat_name'] for r in target_record['records']}) if target_record else
                           [control['seat_name']] if control['seat_name'] in totals else sorted(totals))
                matched = [totals[name] for name in members]
                target = target_record['formal_votes'] if target_record else None
                rows.append(dict(
                    row_id='{} / {} / {}'.format(code, measure, control['seat_name'] or 'matched aggregate'),
                    election_code=code, election_date=definition.election_date,
                    jurisdiction=definition.jurisdiction,
                    kind='federal' if definition.jurisdiction == 'fed' else 'state',
                    family=measure, source_id=control['source_id'], measure=control['measure'],
                    geography_basis=control['geography_basis'], seat_name=control['seat_name'],
                    members=members, observation_status=control['observation_status'],
                    observed_at=control['observed_at'], availability=control['availability'],
                    target_categories=control['target_categories'],
                    target_source_category=control['target_source_category'],
                    operational_count=control['count'], final_formal=target,
                    enrolment=sum(r.enrolment for r in matched),
                    seat_formal=sum(r.formal_votes for r in matched),
                    conversion=target / control['count'] if target is not None and control['count'] else None,
                    final_target_status='known' if target is not None else 'missing_category_split',
                    rounding_half_width=control.get('rounding_half_width_count') or 0,
                    precision=control['inclusion_reason'],
                    eligible=include_nsw or definition.jurisdiction != 'nsw'))
    return rows, dict(excluded)


def election_summaries(rows):
    """Use matched counts once per election, retaining cutoff and source status."""
    grouped = defaultdict(list)
    for row in rows:
        if row['final_formal'] is None:
            continue
        grouped[(row['family'], row['election_code'])].append(row)
    summaries = []
    for (measure, code), cases in sorted(grouped.items()):
        operational = sum(r['operational_count'] for r in cases)
        target = sum(r['final_formal'] for r in cases)
        width = sum(r['rounding_half_width'] for r in cases)
        ratio = target / operational if operational else None
        # Local spread is a shared descriptive scale, never a district fit.
        # Exposure weighting avoids tiny controls dominating ratio variability.
        local_variance = (sum(r['operational_count'] * (r['conversion'] - ratio) ** 2
                              for r in cases if r['operational_count']) / operational
                          if operational and cases[0]['seat_name'] and len(cases) > 1 else None)
        summaries.append(dict(
            family=measure, election_code=code, election_date=cases[0]['election_date'],
            jurisdiction=cases[0]['jurisdiction'], kind=cases[0]['kind'],
            source_id=cases[0]['source_id'], observation_status=cases[0]['observation_status'],
            first_cutoff=min(r['observed_at'] for r in cases),
            last_cutoff=max(r['observed_at'] for r in cases), cases=len(cases),
            operational_count=operational, final_formal=target, conversion=ratio,
            conversion_low=target / (operational + width) if operational else None,
            conversion_high=target / (operational - width) if operational > width else None,
            local_variance=local_variance, eligible=cases[0]['eligible']))
    return summaries


def fit(summaries):
    """Average election ratios equally; report common and local variability."""
    summaries = [r for r in summaries if r['conversion'] is not None]
    if not summaries:
        return None
    ratios = [r['conversion'] for r in summaries]
    local = [r['local_variance'] for r in summaries if r['local_variance'] is not None]
    return dict(
        factor=mean(ratios), training_elections=[r['election_code'] for r in summaries],
        n_elections=len(summaries),
        common_sd=statistics.stdev(ratios) if len(ratios) > 1 else None,
        local_rms=math.sqrt(mean(local)) if local else None, local_elections=len(local),
        factor_low=mean(r['conversion_low'] for r in summaries),
        factor_high=mean(r['conversion_high'] for r in summaries)
                    if all(r['conversion_high'] is not None for r in summaries) else None,
        support='single_election_only' if len(summaries) == 1 else 'pooled_candidate')


def training_for(summaries, target, scheme, model):
    """Hold the entire target election out, with explicit small-sample fallback."""
    training = [r for r in summaries if r['eligible'] and r['family'] == target['family']
                and r['election_code'] != target['election_code']]
    if scheme == 'earlier_only':
        training = [r for r in training if r['election_date'] < target['election_date']]
    elif scheme != 'leave_one_out':
        raise ValueError('Unknown scheme: ' + scheme)
    fallback = ''
    if model == 'kind_pooled':
        same_kind = [r for r in training if r['kind'] == target['kind']]
        if len(same_kind) >= 2:
            training = same_kind
        else:
            fallback = 'fewer_than_two_same_kind_elections_use_global_pool'
    elif model == 'previous_conversion':
        earlier = [r for r in training if r['jurisdiction'] == target['jurisdiction']
                   and r['election_date'] < target['election_date']]
        training = [max(earlier, key=lambda r: r['election_date'])] if earlier else []
    return training, fallback


def previous_category(row, datasets):
    """Carry previous named-category counts forward using the current enrolment.

    This is an unadjusted same-name baseline, not a fitted district parameter.
    Missing categories or changed district names yield no baseline prediction.
    """
    if 'previous_category_prediction' in row:
        return row['previous_category_prediction']
    previous = [d for d in datasets.values() if d.seat_totals
                and d.elections[0].jurisdiction == row['jurisdiction']
                and d.elections[0].election_date < row['election_date']]
    if not previous:
        return None
    dataset = policy.apply_missing_count_policy(max(previous, key=lambda d: d.elections[0].election_date))
    seats = {r.seat_name: r for r in dataset.seat_totals}
    if any(name not in seats for name in row['members']):
        return None
    records = [r for r in dataset.vote_types if r.seat_name in row['members']
               and r.canonical_category in row['target_categories']
               and (not row['target_source_category'] or r.source_category == row['target_source_category'])]
    unreliable = {r['seat'] for r in policy.missing_count_review(dataset)}
    if {r.seat_name for r in records} != set(row['members']) or any(
            r.formal_votes is None or r.seat_name in unreliable for r in records):
        return None
    old_roll = sum(seats[name].enrolment for name in row['members'])
    return sum(r.formal_votes for r in records) * row['enrolment'] / old_roll if old_roll else None


def backtest(rows, summaries, datasets):
    """Generate both validation schemes without ranking either as authoritative."""
    grouped = defaultdict(list)
    for row in rows:
        if row['eligible']:
            grouped[(row['family'], row['election_code'])].append(row)
    folds, predictions = [], []
    for scheme in SCHEMES:
        for (measure, code), cases in sorted(grouped.items()):
            target = cases[0]
            for model in MODELS:
                training, fallback = training_for(summaries, target, scheme, model)
                parameters = None if model == 'previous_category' else fit(training)
                fold_id = '{} / {} / {} / {}'.format(scheme, measure, code, model)
                folds.append(dict(fold_id=fold_id, scheme=scheme, family=measure,
                                  election_code=code, model=model, parameters=parameters, fallback=fallback))
                for row in cases:
                    estimate = (previous_category(row, datasets) if model == 'previous_category' else
                                parameters['factor'] * row['operational_count'] if parameters else None)
                    if estimate is None:
                        continue
                    error = estimate - row['final_formal']
                    # Intervals are a transparent normal approximation, available
                    # only when a common election scale can be estimated. We do
                    # not invent a variance for one-election training folds.
                    factor_sd = None
                    if parameters and parameters['common_sd'] is not None:
                        variance = parameters['common_sd'] ** 2 * (1 + 1 / parameters['n_elections'])
                        if row['seat_name']:
                            if parameters['local_rms'] is None:
                                variance = None
                            else:
                                variance += parameters['local_rms'] ** 2
                        if variance is not None:
                            factor_sd = math.sqrt(variance)
                    width = Z80 * factor_sd * row['operational_count'] if factor_sd is not None else None
                    training_rounding = (max(parameters['factor'] - parameters['factor_low'],
                                             parameters['factor_high'] - parameters['factor']) * row['operational_count']
                                         if parameters and parameters['factor_high'] is not None else 0)
                    predictions.append(dict(
                        row_id=row['row_id'], fold_id=fold_id, scheme=scheme, family=measure,
                        election_code=code, model=model, seat_name=row['seat_name'],
                        observation_status=row['observation_status'], final_formal=row['final_formal'],
                        seat_formal=row['seat_formal'], operational_count=row['operational_count'],
                        predicted_formal=estimate, error_votes=error,
                        interval_low=max(0, estimate - width) if width is not None else None,
                        interval_high=estimate + width if width is not None else None,
                        rounding_effect_votes=(training_rounding + parameters['factor'] * row['rounding_half_width']
                                               if parameters else 0)))
    return folds, predictions


def score(predictions):
    """Summarize each election first so its district count cannot set its weight."""
    by_election = defaultdict(list)
    for row in predictions:
        by_election[row['election_code']].append(row)
    aggregate, absolute, impact, bias, covered = [], [], [], [], []
    for cases in by_election.values():
        final = sum(r['final_formal'] for r in cases)
        seat_final = sum(r['seat_formal'] for r in cases)
        if not final:
            continue
        errors = [r['error_votes'] for r in cases]
        aggregate.append(100 * abs(sum(errors)) / final)
        absolute.append(100 * sum(abs(e) for e in errors) / final)
        bias.append(100 * sum(errors) / final)
        impact.append(100 * sum(abs(e) for e in errors) / seat_final if seat_final else 0)
        intervals = [r for r in cases if r['interval_low'] is not None]
        if intervals:
            covered.append(mean(r['interval_low'] <= r['final_formal'] <= r['interval_high'] for r in intervals))
    return dict(elections=len(by_election), cases=len(predictions),
                aggregate_abs_pct=mean(aggregate), case_abs_pct=mean(absolute),
                bias_pct=mean(bias), seat_impact_pct=mean(impact), coverage80=mean(covered),
                interval_elections=len(covered))


def paired_scores(predictions):
    """Compare models only on shared cases, including partial historical baselines."""
    lookup = {(r['scheme'], r['family'], r['row_id'], r['model']): r for r in predictions}
    paired = []
    for scheme in SCHEMES:
        for measure in sorted({r['family'] for r in predictions}):
            pooled = [r for r in predictions if r['scheme'] == scheme and r['family'] == measure and r['model'] == 'pooled']
            for model in MODELS[1:]:
                left, right = [], []
                for row in pooled:
                    other = lookup.get((scheme, measure, row['row_id'], model))
                    if other:
                        left.append(row)
                        right.append(other)
                paired.append(dict(scheme=scheme, family=measure, model=model,
                                   pooled=score(left), alternative=score(right)))
    return paired


def federal_prepoll_analysis(datasets, fingerprint):
    """Test administering-district counts as indicators, keeping their scope explicit.

    The source supplement recovers a final early-only target for residents of
    each electorate. Issuing-district counts are tested as approximate predictors,
    not relabelled as ballots belonging to those residents. National sums provide
    a second check where movements between districts cancel.

    Keep the two output families distinct. The issuing-district proxy family is
    evaluated only to describe its usefulness and geographic failures; it is not
    a district control or a source of district uncertainty in the allocation or
    prior prototype. Those modules select only the national family and learn its
    district allocation from previous results belonging to resident electors.
    """
    if not federal_source.OUTPUT.exists():
        return dict(available=False)
    supplement = json.loads(federal_source.OUTPUT.read_text(encoding='utf-8'))
    final = {e['election_code']: e for e in supplement['elections']}
    rows, diagnostics = [], []
    for code, evidence in sorted(final.items()):
        if code not in datasets:
            continue
        dataset = datasets[code]
        if evidence['normalized_snapshot_sha256'] != fingerprint['inputs'][code + '.json']:
            raise turnout_data.TurnoutDataError('Federal pre-poll supplement is stale for {}; rerun turnout_federal_prepoll.'.format(code))
        definition = dataset.elections[0]
        seats = {r.seat_name: r for r in dataset.seat_totals}
        observations = [o for o in dataset.operational_observations
                        if o.geography_basis == 'administering_division' and o.measure in audit.EARLY_MEASURES]
        latest, _ = audit.latest_controls(observations, definition.election_date)
        controls = {o.seat_name: o for o, quantum in latest}
        if set(controls) != set(seats):
            raise turnout_data.TurnoutDataError('Incomplete federal pre-poll issuing-district coverage: ' + code)
        targets = {r['seat_name']: r for r in evidence['districts']}
        previous_codes = [old for old in final if final[old]['election_date'] < definition.election_date]
        previous = max(previous_codes, key=lambda old: final[old]['election_date']) if previous_codes else None
        old_targets = {r['seat_name']: r for r in final[previous]['districts']} if previous else {}
        old_seats = {r.seat_name: r for r in datasets[previous].seat_totals} if previous else {}
        # The 2010 retained issue report stops on Thursday. It is shown as
        # evidence but cannot teach a multiplier for a complete voting period.
        eligible = min(o.observed_at[:10] for o in controls.values()) >= (
            date.fromisoformat(definition.election_date) - timedelta(days=1)).isoformat()
        districts = []
        for name, seat in sorted(seats.items()):
            observation, target = controls[name], targets[name]
            baseline = (old_targets[name]['final_early_formal'] * seat.enrolment / old_seats[name].enrolment
                        if name in old_targets and name in old_seats else None)
            districts.append(dict(row_id=code + ' / issuing-district proxy / ' + name,
                election_code=code, election_date=definition.election_date, jurisdiction='fed', kind='federal',
                family='federal_prepoll_district_proxy', source_id=observation.source_id,
                measure=observation.measure, geography_basis='administering_division', seat_name=name,
                members=[name], observed_at=observation.observed_at, observation_status=observation.observation_status,
                operational_count=observation.count, final_formal=target['final_early_formal'],
                enrolment=seat.enrolment, seat_formal=seat.formal_votes,
                conversion=target['final_early_formal'] / observation.count if observation.count else None,
                rounding_half_width=0, eligible=eligible, previous_category_prediction=baseline))
        total = dict(districts[0], row_id=code + ' / national pre-poll total',
                     family='federal_prepoll_national', geography_basis='national', seat_name='',
                     operational_count=sum(r['operational_count'] for r in districts),
                     final_formal=sum(r['final_formal'] for r in districts),
                     enrolment=sum(r['enrolment'] for r in districts), seat_formal=sum(r['seat_formal'] for r in districts),
                     observed_at=max(r['observed_at'] for r in districts),
                     previous_category_prediction=(sum(r['final_early_formal'] for r in old_targets.values()) *
                        sum(r.enrolment for r in seats.values()) / sum(r.enrolment for r in old_seats.values())
                        if old_seats else None))
        total['conversion'] = total['final_formal'] / total['operational_count']
        x = [r['operational_count'] / r['enrolment'] for r in districts]
        y = [r['final_formal'] / r['enrolment'] for r in districts]
        dx, dy = [v - mean(x) for v in x], [v - mean(y) for v in y]
        denominator = math.sqrt(sum(v*v for v in dx) * sum(v*v for v in dy))
        correlation = sum(a*b for a,b in zip(dx,dy)) / denominator if denominator else None
        diagnostics.append(dict(election_code=code, districts=len(districts),
            issued=total['operational_count'], final_early_formal=total['final_formal'],
            multiplier=total['conversion'], cutoff=total['observed_at'], eligible=eligible,
            rate_correlation=correlation))
        rows.extend(districts + [total])
    summaries = election_summaries(rows)
    folds, predictions = backtest(rows, summaries, datasets)
    return dict(available=True, diagnostics=diagnostics, comparisons=rows, conversions=summaries,
                tests=folds, predictions=predictions, paired_scores=paired_scores(predictions),
                sources=[s for e in final.values() for s in e['sources'].values()])


def parameter_candidates(summaries):
    """Export shared candidates with support counts; make no district coefficients."""
    candidates = []
    for measure in sorted({r['family'] for r in summaries}):
        eligible = [r for r in summaries if r['family'] == measure and r['eligible']]
        for kind in ('all', 'federal', 'state'):
            training = [r for r in eligible if kind == 'all' or r['kind'] == kind]
            parameters = fit(training)
            if parameters:
                candidates.append(dict(family=measure, kind=kind, **parameters))
    return candidates


def attach_error_allowances(parameters, predictions, comparisons):
    """Keep conservative shared error scales alongside the descriptive fits.

    The maximum of observed election spread and the two held-election RMS
    errors avoids relying on the most favourable validation scheme. These
    allowances are calibrated from the reported errors, not independently
    validated interval guarantees; the original interval diagnostics stay intact.
    """
    kinds = {r['row_id']: r['kind'] for r in comparisons}
    for parameter in parameters:
        model = 'pooled' if parameter['kind'] == 'all' else 'kind_pooled'
        checks = {}
        for scheme in SCHEMES:
            elections = defaultdict(list)
            for row in predictions:
                if (row['scheme'] == scheme and row['model'] == model
                        and row['family'] == parameter['family']
                        and (parameter['kind'] == 'all' or kinds[row['row_id']] == parameter['kind'])):
                    elections[row['election_code']].append(row)
            errors = [sum(r['error_votes'] for r in cases) / sum(r['operational_count'] for r in cases)
                      for cases in elections.values() if sum(r['operational_count'] for r in cases)]
            checks[scheme] = dict(elections=len(errors),
                                 common_rms=math.sqrt(mean(e * e for e in errors)) if errors else None)
        scales = [parameter['common_sd']] + [r['common_rms'] for r in checks.values()]
        parameter['validation_errors'] = checks
        parameter['common_error_allowance'] = (max(v for v in scales if v is not None)
                                               if parameter['n_elections'] > 1 else None)


def largest_district_errors(predictions):
    """Select material observed errors using the point models shown in diagnostics."""
    worst = []
    for scheme in SCHEMES:
        selected = [r for r in predictions if r['scheme'] == scheme and r['seat_name']
                    and r['model'] == ('kind_pooled' if r['family'] == 'postal_applications_cumulative' else 'pooled')]
        for row in sorted(selected, key=lambda r: abs(r['error_votes']) / r['seat_formal'], reverse=True)[:5]:
            worst.append(dict(row, error_all_votes_pct=100 * row['error_votes'] / row['seat_formal']))
    return worst


def prediction_diagnostics(predictions, tests, summaries):
    """Calculate grouped evidence checks independently of their report presentation.

    Keep scores, sparse-training support and rounding effects in the analytical
    output so the renderer only labels and formats completed results.
    """
    history, support = [], []
    measures = sorted({r['family'] for r in summaries if r['eligible']})
    for measure in measures:
        for model in MODELS[:2]:
            for status in ('contemporaneous', 'final_reconciled'):
                for scheme in SCHEMES:
                    selected = [r for r in predictions
                                if (r['family'], r['model'], r['observation_status'], r['scheme'])
                                == (measure, model, status, scheme)]
                    if selected:
                        history.append(dict(family=measure, model=model, observation_status=status,
                                            scheme=scheme, **score(selected)))
            for scheme in SCHEMES:
                selected_tests = [f for f in tests
                                  if (f['family'], f['model'], f['scheme']) == (measure, model, scheme)]
                counts = [f['parameters']['n_elections'] if f['parameters'] else 0 for f in selected_tests]
                selected = [r for r in predictions
                            if (r['family'], r['model'], r['scheme']) == (measure, model, scheme)]
                support.append(dict(family=measure, model=model, scheme=scheme,
                                    training_min=min(counts), training_median=statistics.median(counts),
                                    training_max=max(counts), no_training=counts.count(0),
                                    one_training=counts.count(1), **score(selected)))

    # Rounding is a small sensitivity check, separate from conversion uncertainty.
    rounded = [r for r in predictions if r['model'] == 'pooled' and r['rounding_effect_votes']]
    rounding = []
    for scheme in SCHEMES:
        selected = [r for r in rounded if r['scheme'] == scheme]
        error = sum(abs(r['error_votes']) for r in selected)
        rounding.append(dict(scheme=scheme,
            effect_vs_error_pct=100 * sum(r['rounding_effect_votes'] for r in selected) / error if error else None,
            median_effect_votes=statistics.median(r['rounding_effect_votes'] for r in selected) if selected else None,
            maximum_effect_votes=max((r['rounding_effect_votes'] for r in selected), default=None)))
    return dict(input_history=history, training_support=support, rounding=rounding,
                largest_district_errors=largest_district_errors(predictions))



def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-directory', type=Path, default=audit.INPUT_DIRECTORY)
    parser.add_argument('--output-directory', type=Path, default=OUTPUT_DIRECTORY)
    parser.add_argument('--include-nsw', action='store_true')
    parser.add_argument('--dry-run', action='store_true', help='compute and summarize without writing')
    parser.add_argument('--check', action='store_true', help='check local input/code/options freshness')
    args = parser.parse_args(argv)
    datasets, fingerprint = load_inputs(args.input_directory)
    config = dict(include_nsw=args.include_nsw)
    manifest = args.output_directory / 'calibration.json'
    if args.check:
        if not manifest.exists():
            print('No calibration manifest; generate it first.')
            return 1
        previous = json.loads(manifest.read_text(encoding='utf-8'))
        current = previous.get('fingerprint') == fingerprint and previous.get('config') == config
        print('Calibration is current.' if current else 'Calibration is stale; regenerate it.')
        return 0 if current else 1
    rows, excluded = comparison_rows(datasets, args.include_nsw)
    summaries = election_summaries(rows)
    folds, predictions = backtest(rows, summaries, datasets)
    parameters = parameter_candidates(summaries)
    attach_error_allowances(parameters, predictions, rows)
    result = dict(calibration_version=2, generated_at=datetime.now(timezone.utc).isoformat(),
                  config=config, fingerprint=fingerprint, comparisons=rows, conversions=summaries,
                  exclusions=excluded, folds=folds, predictions=predictions,
                  paired_scores=paired_scores(predictions), parameters=parameters,
                  reviewed_missing_counts=[r for d in datasets.values() for r in policy.missing_count_review(d)],
                  sources=[vars(s) for d in datasets.values() for s in d.sources],
                  federal_prepoll=federal_prepoll_analysis(datasets, fingerprint))
    result['diagnostics'] = prediction_diagnostics(predictions, folds, summaries)
    if result['federal_prepoll']['available']:
        result['federal_prepoll']['largest_district_errors'] = largest_district_errors(
            result['federal_prepoll']['predictions'])
    print('{} matched cases; {} election/measure comparisons; {} predictions; {} parameter candidates.'.format(
        len(rows), len(summaries), len(predictions), len(result['parameters'])))
    if args.dry_run:
        print('Dry run: no files written.')
        return 0
    args.output_directory.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    report = args.output_directory / 'report.md'
    report.write_text(reporting.render_report(result), encoding='utf-8')
    print('Report: {}'.format(report))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
