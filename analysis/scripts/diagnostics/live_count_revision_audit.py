"""Identify revisions to published preference counts in saved GUI snapshots.

This audit asks how much later TCP movement comes from changes to candidate
counts already reported, rather than additional votes. It compares each saved
snapshot with the last retained feed and also compares successive snapshots.
Opposing candidate changes provide a minimum amount of redistribution; they do
not prove that the same physical ballots changed piles. Unchanged FP candidate
counts and unchanged TCP totals give the strongest direct evidence of preference
revisions. Different finalist pairs and unreported pairs are not compared.

The JSON output contains per-booth evidence and per-seat share effects. It is an
audit of retained feed counts, not a fitted uncertainty model or a GUI rerun.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics


def vote_map(rows, independent):
    """Use the forecast's principal-independent identity consistently over time."""
    result = {}
    for row in rows:
        value = row['value']
        if value <= 0:
            continue
        party = 6 if row['party_index'] == independent else row['party_index']
        result[party] = result.get(party, 0) + value
    return result


def revision_record(fp_before, fp_after, tcp_before, tcp_after, party_a):
    """Separate candidate redistribution from a later addition to the pair total.

    For example, changes of -102 and +99 imply at least 99 votes of opposing
    revision, alongside a net reduction of three. Changes of +102 and +99 are
    consistent with ordinary additions and provide no such evidence. A partial
    preference count may subsequently grow without any FP change; that alone
    is not classified as rechecking.
    """
    if len(tcp_before) != 2 or set(tcp_before) != set(tcp_after) or party_a not in tcp_before:
        return None
    party_b = next(p for p in tcp_before if p != party_a)
    delta_a = tcp_after[party_a] - tcp_before[party_a]
    delta_b = tcp_after[party_b] - tcp_before[party_b]
    transfer = min(abs(delta_a), abs(delta_b)) if delta_a * delta_b < 0 else 0
    fp_change = sum(fp_after.values()) - sum(fp_before.values())
    tcp_change = delta_a + delta_b
    return dict(delta_a=delta_a, delta_b=delta_b, fp_total_change=fp_change,
                tcp_total_change=tcp_change, fp_vector_unchanged=fp_before == fp_after,
                minimum_opposing_revision=transfer,
                signed_minimum_revision=transfer if delta_a > 0 else -transfer,
                pure_preference_revision=bool(transfer and fp_before == fp_after and tcp_change == 0))


def log_odds_change(votes, party_a, transfer):
    """Measure a preference revision on the model's bounded-share scale.

    Hold the reported pair total fixed and move only the signed minimum revision
    between its candidates. A quarter-vote equivalent handles possible zeros.
    This avoids interpreting the arrival of a new batch as rechecking variation.
    Percentage-point effects are retained separately for readable diagnostics.
    """
    party_b = next(p for p in votes if p != party_a)
    a, b = votes[party_a], votes[party_b]
    return 25 * (math.log((a + transfer + .25) / (b - transfer + .25))
                 - math.log((a + .25) / (b + .25)))


def load_frame(path):
    """Retain only the identities and counts needed for these comparisons."""
    content = path.read_bytes()
    analysis = json.loads(content)
    report = json.loads(path.with_name(path.name.replace('.analysis.json', '.json')).read_text(encoding='utf-8'))
    independent = {s['name']: s['live_independent_party_index'] for s in analysis['seats']}
    seats = {}
    for seat in analysis['seats']:
        node = seat['node']; name = seat['name']
        seats[name] = dict(fp=vote_map(node['fp_votes_current'], independent[name]),
                           tcp=vote_map(node['tcp_votes_current'], independent[name]))
    booths = {}
    for booth in analysis['booths']:
        name = booth['seat_name']; node = booth['node']
        key = (name, booth['name'], booth['vote_type'])
        booths[key] = dict(fp=vote_map(node['fp_votes_current'], independent[name]),
                           tcp=vote_map(node['tcp_votes_current'], independent[name]),
                           booth_type=booth['booth_type'])
    return dict(source=report['run']['snapshot_code'], election=report['run']['term_code'],
                path=str(path), sha256=hashlib.sha256(content).hexdigest(), seats=seats, booths=booths)


def compare_frames(before, after):
    """Measure individual revisions and their signed contributions to seat shares.

    A seat effect uses the later seat TCP total as its denominator. Summing signed
    effects allows opposite revisions in different booths to cancel. The summed
    absolute vote revision remains available to describe the underlying activity.
    """
    rows = []
    seats = {}
    groups = {}
    for name, target in after['seats'].items():
        pair = target['tcp']
        current = before['seats'].get(name, {}).get('tcp', {})
        if len(pair) != 2:
            continue
        party_a = 0 if 0 in pair else min(pair)
        denominator = sum(pair.values())
        change = None
        if set(current) == set(pair):
            change = 100 * (pair[party_a] / denominator - current[party_a] / sum(current.values()))
        seats[name] = dict(party_a=party_a, later_tcp_total=denominator,
                           reported_share_change_pp=change, opposing_revision_votes=0,
                           opposing_revision_effect_pp=0, pure_preference_effect_pp=0,
                           missing_preferences_before=sum(max(0, sum(b['fp'].values()) - sum(b['tcp'].values()))
                               for key, b in before['booths'].items() if key[0] == name))
    for key, target in after['booths'].items():
        current = before['booths'].get(key)
        name, booth, vote_type = key
        if current is None or name not in seats or set(target['tcp']) != set(after['seats'][name]['tcp']):
            continue
        detail = revision_record(current['fp'], target['fp'], current['tcp'], target['tcp'], seats[name]['party_a'])
        if detail is None:
            continue
        group = target['booth_type'] if vote_type == 'Ordinary' else vote_type
        summary = groups.setdefault(group, dict(comparable_records=0, revised_records=0,
                                                minimum_revision_votes=0))
        summary['comparable_records'] += 1
        summary['revised_records'] += bool(detail['minimum_opposing_revision'])
        summary['minimum_revision_votes'] += detail['minimum_opposing_revision']
        effect = 100 * detail['signed_minimum_revision'] / seats[name]['later_tcp_total']
        seats[name]['opposing_revision_votes'] += detail['minimum_opposing_revision']
        seats[name]['opposing_revision_effect_pp'] += effect
        if detail['pure_preference_revision']:
            seats[name]['pure_preference_effect_pp'] += effect
        if detail['minimum_opposing_revision']:
            rows.append(dict(seat=name, booth=booth, vote_type=vote_type,
                             booth_type=target['booth_type'],
                             before_fp=sum(current['fp'].values()), after_fp=sum(target['fp'].values()),
                             before_tcp=current['tcp'], after_tcp=target['tcp'],
                             minimum_revision_effect_pp=effect, **detail))
    for name, seat in seats.items():
        current = before['seats'].get(name, {}).get('tcp', {})
        seat['opposing_revision_log_odds_change'] = None
        if set(current) == set(after['seats'][name]['tcp']):
            transfer = seat['opposing_revision_effect_pp'] * seat['later_tcp_total'] / 100
            seat['opposing_revision_log_odds_change'] = log_odds_change(current, seat['party_a'], transfer)
    changes = [abs(s['reported_share_change_pp']) for s in seats.values() if s['reported_share_change_pp'] is not None]
    effects = [abs(s['opposing_revision_effect_pp']) for s in seats.values() if s['reported_share_change_pp'] is not None]
    return dict(before=before['source'], after=after['source'],
                paired_seats=len(changes), revision_booths=len(rows),
                mean_absolute_reported_share_change_pp=statistics.mean(changes) if changes else None,
                mean_absolute_opposing_revision_effect_pp=statistics.mean(effects) if effects else None,
                minimum_opposing_revision_votes=sum(r['minimum_opposing_revision'] for r in rows),
                pure_preference_revision_booths=sum(r['pure_preference_revision'] for r in rows),
                reporting_groups=groups,
                fp_change_groups={label:dict(booths=len(selected),
                    minimum_revision_votes=sum(r['minimum_opposing_revision'] for r in selected))
                    for label, selected in (
                        ('unchanged_total', [r for r in rows if r['fp_total_change'] == 0]),
                        ('1_to_10_votes', [r for r in rows if 0 < abs(r['fp_total_change']) <= 10]),
                        ('11_to_100_votes', [r for r in rows if 10 < abs(r['fp_total_change']) <= 100]),
                        ('over_100_votes', [r for r in rows if abs(r['fp_total_change']) > 100]))},
                seats=seats, revisions=sorted(rows, key=lambda r: -r['minimum_opposing_revision']))


def audit_series(folder):
    """Use the most recent saved run for each source time; do not double-count reruns."""
    paths = {}
    for path in sorted(folder.glob('*.analysis.json')):
        paths[path.name.split('__')[0]] = path
    frames = [load_frame(path) for _, path in sorted(paths.items())]
    if not frames:
        raise ValueError(f'No saved analysis snapshots in {folder}')
    return dict(election=frames[0]['election'], sources=[{k: f[k] for k in ('source', 'path', 'sha256')} for f in frames],
                to_last_feed=[compare_frames(f, frames[-1]) for f in frames[:-1]],
                successive_snapshots=[compare_frames(a, b) for a, b in zip(frames, frames[1:])])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--series', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = dict(definitions=dict(
        reference='Last retained feed, not independently reviewed final results.',
        minimum_opposing_revision='Smaller absolute candidate change when the two changes have opposite signs; not proof of a physical ballot transfer.',
        pure_preference_revision='FP candidate counts and TCP total unchanged while the two TCP candidates change.',
        effects='Percentage points of the later whole-seat TCP total; signed effects can cancel between booths.',
        transformed_effect='Change in 25 times the natural log of candidate A/candidate B, with quarter-vote smoothing; the reported pair total is held fixed.',
        fp_change_groups='Diagnostic descriptions, not thresholds applied to a forecast.'),
        series={str(folder): audit_series(folder) for folder in args.series})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    for name, series in output['series'].items():
        for row in series['to_last_feed']:
            print(name, row['before'], 'paired seats', row['paired_seats'],
                  'revision booths', row['revision_booths'],
                  'minimum opposing revisions', row['minimum_opposing_revision_votes'],
                  'pure preference revision booths', row['pure_preference_revision_booths'])


if __name__ == '__main__':
    main()
