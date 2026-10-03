"""Compare conserved category allocations using audited historical controls.

This offline analysis keeps historical category definitions explicit, separates
allocation from total-size errors, and describes errors shared by categories
and districts. It does not fit Stan or change the live forecast.
"""

import argparse
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import statistics

from lib.paths import REPOSITORY_DIRECTORY
from scripts.turnout import turnout_changes as changes
from scripts.turnout import turnout_operational_calibration as calibration
from scripts.turnout import turnout_priors as priors
from scripts.turnout import turnout_category_report as reporting
from lib.turnout import category_policy as policy


OUTPUT = REPOSITORY_DIRECTORY / 'docs/turnout-category-dynamics'
MODELS = ('previous_shares', 'proportional_remainder', 'ordinary_adjustment')
SCHEMES = calibration.SCHEMES
ANCHORS = ('actual_total', 'zero_drift', 'half_drift', 'full_drift')
COVID = frozenset({'2020qld', '2021wa', '2022fed', '2022vic', '2022sa'})
POSTAL_PRIORITY = ('postal_applications_cumulative', 'postal_ballots_issued_cumulative',
                   'postal_votes_returned_cumulative', 'postal_votes_accepted_cumulative')


def mean(values):
    values = [v for v in values if v is not None]
    return statistics.mean(values) if values else None


def regime(previous, current):
    """Choose a common observed partition; administrative changes stay visible."""
    jurisdiction = current.elections[0].jurisdiction
    old, new = previous.elections[0].election_code, current.elections[0].election_code
    notes, usable = [], True
    mode = jurisdiction
    if jurisdiction == 'fed':
        mode = 'fed_detail' if int(old[:4]) >= 2010 else 'fed_combined_early'
        notes.append('Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll.')
        if old == '2007fed' and new == '2010fed':
            notes.append('Use total early only across the 2010 ordinary/declaration processing change.')
        elif mode == 'fed_combined_early':
            notes.append('All early votes were declarations before 2010; retain total early without a fabricated ordinary split.')
    elif jurisdiction == 'vic':
        notes.append('Early remains the published aggregate; other combines provisional, marked-as-voted and the small 2006 declaration group.')
    elif jurisdiction == 'sa':
        mode = 'sa_combined'
        usable = new != '2026sa'
        notes.append('Old declarations remain combined; no historical early/postal or smaller declaration split.')
        if not usable:
            notes.append('2026 detailed categories have no matched earlier detailed baseline; separate controlled-budget diagnostic.')
    elif jurisdiction == 'wa':
        mode = 'wa_broad' if new == '2025wa' else 'wa'
        if mode == 'wa_broad':
            notes.append('Combine ordinary, early, mobile and absent; unresolved early/absent distinction.')
    elif jurisdiction == 'qld':
        mode = 'qld_detail' if int(old[:4]) >= 2015 else 'qld_combined_early'
        if (old, new) in {('2012qld', '2015qld'), ('2017qld', '2020qld')}:
            usable = False
            notes.append('Reporting/category break: descriptive totals only, excluded from routine dynamics and allocation tests.')
    if old in COVID or new in COVID:
        notes.append('COVID-period endpoint; also assess training without these transitions.')
    if policy.merge_other_required(old, new):
        notes.append('Combine other with ordinary in a broader observed pool across a definition or service change.')
    return mode, usable, notes


def partition(dataset, mode, federal):
    """Build exhaustive formal partitions without assigning an unseen old split."""
    dataset = policy.apply_missing_count_policy(dataset)
    seats = {s.seat_name: s for s in dataset.seat_totals}
    records = defaultdict(list)
    for row in dataset.vote_types:
        if policy.suppressed_record(dataset.elections[0].election_code, row):
            continue
        records[row.seat_name].append(row)
    result = {}
    for name, rows in records.items():
        if any(r.coverage != 'complete' or r.formal_votes is None for r in rows):
            continue
        native = defaultdict(int)
        for row in rows:
            native[row.canonical_category] += row.formal_votes
        groups = defaultdict(int)
        for category, value in native.items():
            if mode == 'sa_combined':
                key = 'ordinary' if category == 'election_day_ordinary' else 'declarations'
            elif category == 'postal':
                key = 'postal'
            elif mode == 'wa_broad':
                key = 'ordinary_early_absent' if category in {
                    'election_day_ordinary', 'early_in_person', 'mobile_or_institution', 'absent'
                } else 'other'
            elif category in {'election_day_ordinary', 'ordinary_combined'}:
                key = 'ordinary'
            elif mode == 'wa' and category == 'mobile_or_institution':
                key = 'ordinary'
            elif category in {'early_combined', 'early_in_person', 'declaration_early'}:
                key = ('early_declaration' if category == 'declaration_early' else 'early_ordinary') \
                    if mode in {'fed_detail', 'qld_detail'} else 'early'
            elif category == 'absent':
                key = 'absent'
            else:
                key = 'other'
            groups[key] += value
        if dataset.elections[0].jurisdiction == 'fed' and 'ordinary_combined' in native:
            code = dataset.elections[0].election_code
            if code not in federal or name not in federal[code]:
                raise ValueError('Missing exact federal final pre-poll supplement: ' + code)
            early = federal[code][name]['ordinary_early_formal']
            groups['ordinary'] -= early
            groups['early_ordinary' if mode == 'fed_detail' else 'early'] += early
        if sum(groups.values()) != seats[name].formal_votes or any(v < 0 for v in groups.values()):
            raise ValueError('Category partition does not reconcile: ' + name)
        result[name] = dict(groups)
    return result


def election_objects(datasets):
    """Use the existing total-rate research with the same validated input snapshot."""
    output = []
    for dataset in datasets.values():
        dataset = policy.apply_missing_count_policy(dataset)
        definition = dataset.elections[0]
        if not dataset.seat_totals:
            continue
        categories = defaultdict(list)
        for row in dataset.vote_types:
            categories[row.seat_name].append(row)
        complete = {name: rows for name, rows in categories.items()
                    if all(r.coverage == 'complete' for r in rows)}
        output.append(changes.Election(definition.election_code, definition.election_date,
            definition.jurisdiction, {s.seat_name: s for s in dataset.seat_totals}, complete,
            [s for s in dataset.sources if s.status == 'final']))
    return sorted(output, key=lambda e: e.date)


def prepare_pairs(datasets, federal):
    """Keep compatible category history and missing district baselines explicit."""
    histories = defaultdict(list)
    for dataset in datasets.values():
        if dataset.seat_totals:
            histories[dataset.elections[0].jurisdiction].append(dataset)
    pairs = []
    for history in histories.values():
        history.sort(key=lambda d: d.elections[0].election_date)
        for previous, current in zip(history, history[1:]):
            mode, usable, notes = regime(previous, current)
            old_parts, new_parts = partition(previous, mode, federal), partition(current, mode, federal)
            # An introduced or redefined small category has no behavioural
            # baseline. Compare a complete broader pool instead of supplying a
            # made-up zero or importing changes from an incompatible definition.
            if policy.merge_other_required(previous.elections[0].election_code, current.elections[0].election_code):
                old_parts = {n: policy.merge_other(v) for n, v in old_parts.items()}
                new_parts = {n: policy.merge_other(v) for n, v in new_parts.items()}
            old_seats = {s.seat_name: s for s in previous.seat_totals}
            new_seats = {s.seat_name: s for s in current.seat_totals}
            keys = sorted({k for p in old_parts.values() for k in p} &
                          {k for p in new_parts.values() for k in p})
            if any(set(v) != set(keys) for v in list(old_parts.values()) + list(new_parts.values())):
                raise ValueError('No complete common observed category partition: ' + current.elections[0].election_code)
            total_old = sum(old_seats[n].formal_votes for n in old_parts)
            roll_old = sum(old_seats[n].enrolment for n in old_parts)
            old_counts = {k: sum(p[k] for p in old_parts.values()) for k in keys}
            baselines = {}
            for name, seat in new_seats.items():
                matched = name in old_parts
                counts = old_parts[name] if matched else old_counts
                formal = old_seats[name].formal_votes if matched else total_old
                roll = old_seats[name].enrolment if matched else roll_old
                baselines[name] = dict(
                    shares={k: counts[k] / formal for k in keys},
                    counts={k: counts[k] * seat.enrolment / roll for k in keys},
                    observed_counts={k: counts[k] for k in keys},
                    matched=matched)
                possible = policy.possible_proportions([counts[k] for k in keys])
                baselines[name]['model_shares'] = dict(zip(keys, possible))
                baselines[name]['model_counts'] = {k: share * formal * seat.enrolment / roll
                                                 for k, share in zip(keys, possible)}
            code = current.elections[0].election_code
            pairs.append(dict(previous=previous.elections[0].election_code, current=code,
                date=current.elections[0].election_date, jurisdiction=current.elections[0].jurisdiction,
                mode=mode, usable=usable, covid=(code in COVID or previous.elections[0].election_code in COVID),
                notes=notes, keys=keys, old_parts=old_parts, actual=new_parts, baseline=baselines,
                old_seats=old_seats, seats=new_seats,
                missing_current=sorted(new_seats.keys() - new_parts.keys()),
                unmatched_previous=sorted(new_seats.keys() - old_parts.keys())))
    return sorted(pairs, key=lambda p: p['date'])


def total_estimates(pair, rate_rows, scheme, anchor, exclude_covid=False):
    """Hold out rate transitions at both endpoints and use one total rule for all allocations."""
    target = next(r for r in rate_rows if r['current'] == pair['current'] and r['level'] == 'election')
    training = priors.training_elections(rate_rows, target, scheme)
    if exclude_covid:
        training = [r for r in training if not ({r['previous'], r['current']} & COVID)]
    fraction = {'zero_drift': 0, 'half_drift': .5, 'full_drift': 1}.get(anchor, 0)
    drift = fraction * priors.mean(
        math.log(r['current_turnout_pct'] / (100 - r['current_turnout_pct']))
        - math.log(r['previous_turnout_pct'] / (100 - r['previous_turnout_pct'])) for r in training)
    lookup = {r['geography']: r for r in rate_rows
              if r['current'] == pair['current'] and r['level'] == 'seat'}
    output = {}
    for name, seat in pair['seats'].items():
        row = lookup.get(name, dict(target, current_enrolment=seat.enrolment,
                                   current_formal_votes=seat.formal_votes))
        if anchor == 'actual_total':
            output[name] = seat.formal_votes
        else:
            # Participation is a subset of enrolment. Apply the shared change
            # in log odds, then convert back before multiplying by enrolment
            # and the previous formality rate. This matches the prior prototype.
            previous = row['previous_turnout_pct']
            odds = math.log(previous / (100 - previous)) + drift
            turnout = 1 / (1 + math.exp(-odds))
            output[name] = seat.enrolment * turnout * row['previous_formality_pct'] / 100
    return output, [r['current'] for r in training]


def control_estimates(pair, controls, summaries, scheme, exclude_covid=False):
    """Convert controls using other elections; aggregate evidence stays aggregate.

    One postal stage is chosen in a fixed order, never selected by test errors.
    Federal early controls are national indicators, distributed by previous
    resident-electorate early rates and current enrolment.

    The issuing-district proxy comparisons in operational calibration are
    diagnostics only. Deliberately select the national federal early family
    below: an issuing centre's attendance cannot identify the number of early
    votes belonging to residents of the district administering that centre.
    """
    selected, support, used_groups = {}, [], set()
    code = pair['current']
    candidates = [
        ('early', 'prepoll_votes_cast_cumulative'),
        ('early', 'federal_prepoll_national'),
    ] + [('postal', m) for m in POSTAL_PRIORITY] + [
        ('early_postal', 'early_and_postal_votes_recorded_cumulative')]
    for group, measure in candidates:
        members = ([k for k in pair['keys'] if k.startswith('early')] if group == 'early' else
                   ['postal'] if group == 'postal' else
                   [k for k in pair['keys'] if k.startswith('early') or k == 'postal'])
        # A control must match complete categories, not a component hidden in
        # an old declaration pool or the WA broad group.
        if (not members or (group == 'postal' and 'postal' not in pair['keys'])
                or (group == 'early_postal' and not any(k.startswith('early') for k in pair['keys']))
                or used_groups & set(members)):
            continue
        observed = [r for r in controls if r['election_code'] == code and r['family'] == measure and r['eligible']]
        if not observed:
            continue
        model = 'kind_pooled' if measure == 'postal_applications_cumulative' else 'pooled'
        available = [r for r in summaries if not exclude_covid or r['election_code'] not in COVID]
        training, fallback = calibration.training_for(available, observed[0], scheme, model)
        parameters = calibration.fit(training)
        if not parameters:
            continue
        values = {}
        for observation in observed:
            estimate = parameters['factor'] * observation['operational_count']
            if observation['seat_name']:
                values[observation['seat_name']] = estimate
            else:
                names = (list(pair['seats']) if measure == 'federal_prepoll_national'
                         else observation.get('members') or list(pair['seats']))
                names = [n for n in names if n in pair['baseline']]
                weights = {n: sum(pair['baseline'][n]['counts'][k] for k in members) for n in names}
                denominator = sum(weights.values())
                if not denominator:
                    continue
                values.update({n: estimate * w / denominator for n, w in weights.items()})
        if not values:
            continue
        selected[group] = dict(members=members, values=values)
        used_groups.update(members)
        support.append(dict(group=group, measure=measure, factor=parameters['factor'],
            training_elections=parameters['training_elections'], fallback=fallback,
            observation_status=observed[0]['observation_status'],
            first_cutoff=min(r['observed_at'] for r in observed), last_cutoff=max(r['observed_at'] for r in observed),
            aggregate=not bool(observed[0]['seat_name']), districts=len(values)))
    return selected, support


def allocate(total, baseline, controls, model):
    """Conserve a nonnegative vote budget without treating uncertain controls as hard facts."""
    shares = baseline.get('model_shares', baseline['shares'])
    prior = baseline.get('model_counts', baseline['counts'])
    if model == 'previous_shares':
        return {k: total * share for k, share in shares.items()}, []
    result, activations = {k: 0.0 for k in shares}, []
    fixed = {}
    for members, value in controls:
        for k in members:
            fixed[k] = value * shares[k] / sum(shares[m] for m in members) if sum(shares[m] for m in members) else value / len(members)
        # The ordinary-adjustment rule also protects declaration pre-polls
        # within a controlled early total, where that old split is observed.
        if model == 'ordinary_adjustment' and 'early_declaration' in members and 'early_ordinary' in members:
            fixed['early_declaration'] = min(prior['early_declaration'], value)
            fixed['early_ordinary'] = value - fixed['early_declaration']
            if prior['early_declaration'] > value:
                activations.append('declaration_early_exceeds_early_control')
    controlled = sum(fixed.values())
    if controlled > total:
        fixed = {k: v * total / controlled for k, v in fixed.items()}
        activations.append('controls_exceed_total')
    result.update(fixed)
    remaining = max(0, total - sum(fixed.values()))
    free = [k for k in shares if k not in fixed]
    if model == 'proportional_remainder':
        denominator = sum(shares[k] for k in free)
        for k in free:
            result[k] = remaining * shares[k] / denominator if denominator else remaining / len(free)
    elif model == 'ordinary_adjustment':
        absorber = 'ordinary' if 'ordinary' in shares else 'ordinary_early_absent'
        protected = [k for k in free if k != absorber]
        desired = sum(prior[k] for k in protected)
        scale = min(1, remaining / desired) if desired else 1
        if scale < 1:
            activations.append('protected_categories_exceed_remainder')
        for k in protected:
            result[k] = prior[k] * scale
        result[absorber] = max(0, remaining - sum(result[k] for k in protected))
    else:
        raise ValueError('Unknown allocation rule: ' + model)
    if abs(sum(result.values()) - total) > 1e-6 or any(v < 0 for v in result.values()):
        raise ValueError('Allocation did not conserve its total.')
    return result, activations


def collapsed(values):
    """Use four exhaustive groups to describe co-movement across detailed regimes."""
    if any(key.startswith('ordinary_') for key in values):
        # A combined observed pool cannot supply its hidden components. Keep
        # it in allocation scores, but omit it from this four-way diagnostic.
        return None
    output = dict(ordinary=values.get('ordinary', 0), early=0, postal=values.get('postal', 0), other=0)
    for key, value in values.items():
        if key.startswith('early'):
            output['early'] += value
        elif key not in {'ordinary', 'postal'}:
            output['other'] += value
    return output


def category_changes(pairs):
    """Describe like-for-like share and per-elector changes on the same named districts."""
    output = []
    for pair in pairs:
        names = sorted(pair['old_parts'].keys() & pair['actual'].keys())
        if not names:
            continue
        old = {k: sum(pair['old_parts'][n][k] for n in names) for k in pair['keys']}
        new = {k: sum(pair['actual'][n][k] for n in names) for k in pair['keys']}
        old_roll = sum(pair['old_seats'][n].enrolment for n in names)
        new_roll = sum(pair['seats'][n].enrolment for n in names)
        old_total, new_total = sum(old.values()), sum(new.values())
        for key in pair['keys']:
            output.append(dict(previous=pair['previous'], current=pair['current'], mode=pair['mode'],
                category=key, districts=len(names), usable=pair['usable'], covid=pair['covid'],
                previous_share_pct=100 * old[key] / old_total, current_share_pct=100 * new[key] / new_total,
                share_change_pp=100 * (new[key] / new_total - old[key] / old_total),
                share_change_log_odds=policy.subset_log_odds(new[key], new_total) - policy.subset_log_odds(old[key], old_total),
                count_change_per_1000=1000 * (new[key] / new_roll - old[key] / old_roll),
                allocation_change_per_1000=1000 * (new[key] / new_roll - old[key] / old_total * new_total / new_roll)))
    return output


def predict(pairs, controls, summaries, rate_rows, exclude_covid=False):
    """Score the same districts for all three rules under each common total assumption."""
    output, tests, skipped = [], [], []
    for pair in pairs:
        if not pair['usable'] or pair['jurisdiction'] in {'sa'}:
            continue
        for scheme in SCHEMES:
            converted, support = control_estimates(pair, controls, summaries, scheme, exclude_covid)
            if not converted:
                skipped.append(dict(current=pair['current'], scheme=scheme,
                                    reason='No supported control with an independently estimated conversion.'))
                continue
            names = [n for n in sorted(pair['actual']) if any(n in v['values'] for v in converted.values())]
            tests.append(dict(current=pair['current'], previous=pair['previous'], scheme=scheme,
                training_variant='without_covid' if exclude_covid else 'all_history',
                districts=len(names), controls=support))
            for anchor in ANCHORS:
                totals, total_training = total_estimates(pair, rate_rows, scheme, anchor, exclude_covid)
                for name in names:
                    observed = [(v['members'], v['values'][name]) for v in converted.values() if name in v['values']]
                    coverage = '+'.join(sorted(k for k, v in converted.items() if name in v['values']))
                    actual = {k: pair['actual'][name].get(k, 0) for k in pair['keys']}
                    for model in MODELS:
                        estimate, activations = allocate(totals[name], pair['baseline'][name], observed, model)
                        errors = {k: estimate[k] - actual[k] for k in pair['keys']}
                        output.append(dict(current=pair['current'], previous=pair['previous'],
                            jurisdiction=pair['jurisdiction'], mode=pair['mode'], seat_name=name,
                            scheme=scheme, anchor=anchor, model=model, covid=pair['covid'],
                            training_variant='without_covid' if exclude_covid else 'all_history',
                            controls=coverage, matched_baseline=pair['baseline'][name]['matched'],
                            enrolment=pair['seats'][name].enrolment, actual_total=sum(actual.values()),
                            predicted_total=totals[name], actual=actual, predicted=estimate, errors=errors,
                            activations=activations, total_training_elections=total_training))
    return output, tests, skipped


def score(rows):
    """Average election-level count errors; every allocation uses identical observations."""
    elections = defaultdict(list)
    for row in rows:
        elections[row['current']].append(row)
    values = []
    for records in elections.values():
        actual = sum(r['actual_total'] for r in records)
        categories = sorted({k for r in records for k in r['errors']})
        gross = sum(abs(v) for r in records for v in r['errors'].values())
        net = sum(abs(sum(r['errors'].get(k, 0) for r in records)) for k in categories)
        total_error = sum(r['predicted_total'] - r['actual_total'] for r in records)
        values.append(dict(category_error_pct=100 * gross / actual,
            aggregate_category_error_pct=100 * net / actual, total_error_pct=100 * abs(total_error) / actual,
            reallocation_pct=100 * gross / (2 * actual) if rows[0]['anchor'] == 'actual_total' else None,
            constraints=sum(bool(r['activations']) for r in records)))
    return dict(elections=len(elections), districts=len(rows),
        category_error_pct=mean(v['category_error_pct'] for v in values),
        aggregate_category_error_pct=mean(v['aggregate_category_error_pct'] for v in values),
        total_error_pct=mean(v['total_error_pct'] for v in values),
        reallocation_pct=mean(v['reallocation_pct'] for v in values),
        constrained_predictions=sum(v['constraints'] for v in values))


def score_tables(predictions):
    grouped = defaultdict(list)
    for row in predictions:
        for scope, group in [('all', 'all'), ('controls', row['controls']),
                             ('jurisdiction', row['jurisdiction']), ('election', row['current'])]:
            grouped[row['training_variant'], row['scheme'], row['anchor'], row['model'], scope, group].append(row)
    return [dict(zip(('training_variant', 'scheme', 'anchor', 'model', 'scope', 'group'), key), **score(rows))
            for key, rows in sorted(grouped.items())]


def paired_covid_scores(predictions):
    """Compare training choices on exactly the same election/district observations."""
    grouped = defaultdict(dict)
    for row in predictions:
        if row['anchor'] in {'actual_total', 'half_drift'}:
            key = (row['scheme'], row['anchor'], row['model'])
            identity = (row['current'], row['seat_name'])
            grouped[key].setdefault(row['training_variant'], {})[identity] = row
    output = []
    for (scheme, anchor, model), variants in sorted(grouped.items()):
        complete = variants.get('all_history', {})
        restricted = variants.get('without_covid', {})
        shared = sorted(complete.keys() & restricted.keys())
        if shared:
            output.append(dict(scheme=scheme, anchor=anchor, model=model,
                all_history=score([complete[k] for k in shared]),
                without_covid=score([restricted[k] for k in shared])))
    return output


def joint_errors(predictions):
    """Describe common and district allocation errors without fitting an independent-error model."""
    groups = defaultdict(list)
    for row in predictions:
        if (row['anchor'] == 'actual_total' and row['training_variant'] == 'all_history'
                and collapsed(row['errors']) is not None):
            groups[row['scheme'], row['model'], row['controls']].append(row)
    output = []
    keys = ('ordinary', 'early', 'postal', 'other')
    for (scheme, model, controls), rows in sorted(groups.items()):
        elections = defaultdict(list)
        for row in rows:
            elections[row['current']].append(row)
        common, local = [], []
        for records in elections.values():
            roll = sum(r['enrolment'] for r in records)
            errors = [collapsed(r['errors']) for r in records]
            centre = {k: 1000 * sum(e[k] for e in errors) / roll for k in keys}
            common.append((centre, 1 / len(elections)))
            for row, error in zip(records, errors):
                local.append(({k: 1000 * error[k] / row['enrolment'] - centre[k] for k in keys},
                              row['enrolment'] / roll / len(elections)))
        for level, vectors in [('election', common), ('district_after_election', local)]:
            centre = {k: sum(w * v[k] for v, w in vectors) for k in keys}
            covariance = {a: {b: sum(w * (v[a] - centre[a]) * (v[b] - centre[b]) for v, w in vectors)
                              for b in keys} for a in keys}
            correlation = {a: {b: covariance[a][b] / math.sqrt(covariance[a][a] * covariance[b][b])
                              if covariance[a][a] * covariance[b][b] > 0 else None for b in keys} for a in keys}
            output.append(dict(scheme=scheme, model=model, controls=controls, level=level,
                elections=len(elections), districts=len(rows), mean_per_1000=centre,
                covariance_per_1000_squared=covariance, correlation=correlation,
                rms_per_1000={k: math.sqrt(sum(w * v[k] ** 2 for v, w in vectors)) for k in keys}))
    return output


def sa_budget(pairs, controls, summaries, rate_rows):
    """Check the SA vote budget without inferring missing previous declaration shares."""
    pair = next((p for p in pairs if p['current'] == '2026sa'), None)
    if not pair:
        return []
    output = []
    for scheme in SCHEMES:
        estimates = {}
        for group, measure in [('early', 'prepoll_votes_cast_cumulative'), ('postal', 'postal_applications_cumulative')]:
            observed = [r for r in controls if r['election_code'] == '2026sa' and r['family'] == measure]
            if not observed:
                continue
            training, fallback = calibration.training_for(summaries, observed[0], scheme,
                'kind_pooled' if group == 'postal' else 'pooled')
            parameter = calibration.fit(training)
            if parameter:
                estimates[group] = {r['seat_name']: (r, parameter['factor'] * r['operational_count']) for r in observed}
        for anchor in ('actual_total', 'half_drift'):
            totals, training = total_estimates(pair, rate_rows, scheme, anchor)
            for name in sorted(pair['seats']):
                if any(name not in estimates.get(k, {}) for k in ('early', 'postal')):
                    continue
                early, postal = estimates['early'][name], estimates['postal'][name]
                if name not in pair['actual'] or any(r[0]['final_formal'] is None for r in (early, postal)):
                    continue
                actual_total = pair['seats'][name].formal_votes
                output.append(dict(scheme=scheme, anchor=anchor, seat_name=name, actual_total=actual_total,
                    predicted_total=totals[name], predicted_early=early[1], actual_early=early[0]['final_formal'],
                    predicted_postal=postal[1], actual_postal=postal[0]['final_formal'],
                    predicted_other=totals[name] - early[1] - postal[1],
                    actual_other=actual_total - early[0]['final_formal'] - postal[0]['final_formal']))
    return output


def sa_budget_summary(rows):
    """Separate the total and control contributions to the SA residual error."""
    grouped = defaultdict(list)
    for row in rows:
        grouped[row['scheme'], row['anchor']].append(row)
    output = []
    for (scheme, anchor), records in sorted(grouped.items()):
        totals = {k: sum(r[k] for r in records) for k in (
            'actual_total', 'predicted_total', 'actual_early', 'predicted_early',
            'actual_postal', 'predicted_postal', 'actual_other', 'predicted_other')}
        total_error = totals['predicted_total'] - totals['actual_total']
        early_error = totals['predicted_early'] - totals['actual_early']
        postal_error = totals['predicted_postal'] - totals['actual_postal']
        output.append(dict(scheme=scheme, anchor=anchor, districts=len(records), **totals,
            total_error=total_error, early_error=early_error, postal_error=postal_error,
            other_error=total_error - early_error - postal_error))
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-directory', type=Path, default=OUTPUT)
    parser.add_argument('--include-nsw', action='store_true')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args(argv)
    datasets, fingerprint = calibration.load_inputs(calibration.audit.INPUT_DIRECTORY)
    for module in (Path(__file__), Path(reporting.__file__), Path(changes.__file__),
                   Path(priors.__file__), Path(priors.expectations.__file__), Path(policy.__file__)):
        fingerprint['code'][module.name] = hashlib.sha256(module.read_text(encoding='utf-8').encode()).hexdigest()
    config = dict(include_nsw=args.include_nsw)
    manifest = args.output_directory / 'analysis.json'
    if args.check:
        previous = json.loads(manifest.read_text(encoding='utf-8')) if manifest.exists() else {}
        current = previous.get('fingerprint') == fingerprint and previous.get('config') == config
        print('Category analysis is current.' if current else 'Category analysis is stale; regenerate it.')
        return 0 if current else 1
    supplement = calibration.federal_prepoll_analysis(datasets, fingerprint)
    federal = {e['election_code']: {r['seat_name']: r for r in e['districts']}
               for e in json.loads(calibration.federal_source.OUTPUT.read_text(encoding='utf-8'))['elections']}
    pairs = prepare_pairs(datasets, federal)
    controls, _ = calibration.comparison_rows(datasets, args.include_nsw, include_unknown_targets=True)
    controls += [r for r in supplement['comparisons'] if r['family'] == 'federal_prepoll_national']
    summaries = calibration.election_summaries(controls)
    rate_rows = priors.prepare_rows(election_objects(datasets))
    if not args.include_nsw:
        rate_rows = [r for r in rate_rows if r['jurisdiction'] != 'nsw']
    test_pairs = [p for p in pairs if args.include_nsw or p['jurisdiction'] != 'nsw']
    predictions, tests, skipped = [], [], []
    for exclude_covid in (False, True):
        values, checks, missing = predict(test_pairs, controls, summaries, rate_rows, exclude_covid)
        predictions += values
        tests += checks
        if not exclude_covid:
            skipped = missing
    compatible = [{k: p[k] for k in ('previous', 'current', 'mode', 'usable', 'covid', 'notes',
                                    'keys', 'missing_current', 'unmatched_previous')}
                  | dict(districts=len(p['actual'])) for p in pairs]
    sa_rows = sa_budget(pairs, controls, summaries, rate_rows)
    result = dict(version=1, generated_at=datetime.now(timezone.utc).isoformat(), config=config,
        fingerprint=fingerprint, compatibility=compatible, changes=category_changes(pairs),
        predictions=predictions, tests=tests, skipped=skipped, scores=score_tables(predictions),
        category_definition_changes=policy.DEFINITION_CHANGES,
        service_exclusions=policy.SERVICE_EXCLUSIONS, empty_group_review=policy.empty_groups(datasets),
        parent_denominator_review=policy.PARENT_REVIEWS,
        reviewed_missing_counts=[r for dataset in datasets.values() for r in policy.missing_count_review(dataset)],
        final_control_zero_review=[r for r in controls if r['operational_count'] == 0],
        covid_comparison=paired_covid_scores(predictions), joint_errors=joint_errors(predictions),
        sa_budget=sa_rows, sa_summary=sa_budget_summary(sa_rows))
    print('{} transitions; {} prediction tests; {:,} district/rule predictions.'.format(
        len(pairs), len(tests), len(predictions)))
    if args.dry_run:
        print('Dry run: no files written.')
        return 0
    args.output_directory.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    (args.output_directory / 'report.md').write_text(reporting.render_report(result), encoding='utf-8')
    print('Report: {}'.format(args.output_directory / 'report.md'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
