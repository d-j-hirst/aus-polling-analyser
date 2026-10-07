"""Replay SA 2026 exported counted accounts without running the C++ simulator.

The pre-election fixture and no-results allocation template are prediction
inputs. Reviewed final results enter only the scoring functions. Raw ECSA XML
provides source-time identity, counted-total reconciliation and FP finalisation;
LiveV2's export provides the already-normalised booth/party counted account.
"""

import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import time
import xml.etree.ElementTree as ET

import numpy as np
import scipy

from lib import live_analysis_archive
from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.live_analysis_archive import analysis_path_for_snapshot, load_json
from lib.turnout import category_policy, live, prior, maintained_parameters
from lib.turnout.paths import download_directory
from scripts.turnout import turnout_live_report


DEFAULT_ARCHIVE = REPOSITORY_DIRECTORY / 'live_runs/sa2026-test2'
DEFAULT_FIXTURE = REPOSITORY_DIRECTORY / 'docs/turnout-prior-prototype/fixtures-v3.json'
DEFAULT_OUTPUT = REPOSITORY_DIRECTORY / 'docs/turnout-live-prototype'
# These closures were known before election day, and are also excluded in the
# current C++ ECSA loader. Keeping them explicit corrects older replay exports
# without treating every unexplained missing booth as closed.
CLOSED_BOOTHS = {('Giles', name): 'Pre-election cancellation: staffing shortages.'
                 for name in ('Whyalla Norrie North', 'Whyalla Norrie North-West', 'Willsden')}
CHECKPOINTS = ('20260321180632', '20260321185746', '20260321203932',
               '20260321211209', '20260321213404', '20260322003211',
               '20260325123447', '20260327125830')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    return load_json(path)


def vote_sum(node, field):
    """Sum the exported party account without guessing candidate identities."""
    return sum(row['value'] for row in node[field])


def load_fixture(path, election_code='2026sa'):
    """Use the saved earlier-only example, checking its stored source revisions.

    Saved source hashes
    prevent silently combining a revised input with an old fitted distribution.
    """
    payload = read_json(path)
    if payload['schema_version'] != prior.SCHEMA_VERSION or payload['model_version'] != prior.MODEL_VERSION:
        raise ValueError('Regenerate the current stage-four fixture before replaying.')
    for filename, expected in payload['fingerprint']['inputs'].items():
        source = ANALYSIS_DIRECTORY / 'Data/Turnout' / filename
        canonical = json.dumps(read_json(source), sort_keys=True, separators=(',', ':'), ensure_ascii=False)
        if hashlib.sha256(canonical.encode()).hexdigest() != expected:
            raise ValueError('Prior source changed; regenerate the fixture: ' + filename)
    # The fixture freezes fitted parameters, rather than rerunning historical
    # analysis here. Check the sampler that interprets their units, and retain
    # the full fixture hash; report-only or analysis-code edits do not refit it.
    sampler = Path(prior.__file__)
    if hashlib.sha256(sampler.read_text(encoding='utf-8').encode()).hexdigest() != payload['fingerprint']['code']['prior.py']:
        raise ValueError('Prior sampler changed; regenerate the fixture.')
    fixture = next(f for f in payload['fixtures'] if f['inputs']['identity'] == election_code + '/earlier_only')
    # The private fixture supplies election inputs. Installed tuning is read
    # from maintained source so an experimental export cannot choose it.
    fixture['parameters'] = maintained_parameters.prior_parameters(election_code + '/earlier_only')
    return fixture


def source_index(directory):
    """Index embedded source timestamps, which differ from capture filenames.

    Read only the UTF-16 header of each archive. Multiple captures can contain
    the same update; their counted totals are checked when selecting a source.
    No current-results file or replay selection state is changed.
    """
    result = defaultdict(list)
    for path in sorted(directory.glob('el2026*.xml')):
        if not re.fullmatch(r'el2026\d{12}\.xml', path.name):
            continue
        with path.open(encoding='utf-16') as stream:
            match = re.search(r'<last_updated>([^<]+)</last_updated>', stream.read(8192))
        if match:
            stamp = ''.join(re.findall(r'\d', match[1]))[:14]
            result[stamp].append(path)
    return result


def source_metadata(path):
    """Read authoritative FP flags and the detailed counted account.

    ECSA's district summary can lag newly added declaration rows. Sum its
    first-preference polling-place entries, matching the account LiveV2 uses,
    and retain the summary discrepancy as a source diagnostic.
    """
    root = ET.parse(path).getroot()
    namespace = root.tag.split('}')[0] + '}'
    result = {}
    for district in root.findall(namespace + 'districts/' + namespace + 'district'):
        name = district.findtext(namespace + 'district_name')
        summary = district.find(namespace + 'district_summary')
        candidates = district.findall(namespace + 'first_preferences/' + namespace + 'candidate')
        detailed = sum(int(p.text) for candidate in candidates for p in candidate.findall('.//' + namespace + 'ballot_papers'))
        correction = archived_narungga_correction(name, candidates, namespace)
        result[name] = dict(counted=detailed + correction, raw_detailed=detailed,
                            summary=int(summary.findtext(namespace + 'district_total_formal')),
                            archived_count_correction=correction,
                            finalised=district.findtext(namespace + 'first_preferences_finalised') == 'true')
    return result


def archived_narungga_correction(name, candidates, namespace):
    """Recognise the existing C++ correction in the original replay account.

    ElectionData.cpp explicitly replaces Narungga's misordered absent early
    row when candidate 133004 has 189 votes. Its replacement totals 848 rather
    than the raw row's 831. Preserve that archived counted input for comparison,
    but identify its 17-vote difference instead of accepting arbitrary mismatch.
    This is a reproduction of an existing exception, not a new inferred count.
    """
    if name != 'Narungga':
        return 0
    counts = {}
    for candidate in candidates:
        selected = [p for p in candidate.findall(namespace + 'polling_places/' + namespace + 'polling_place')
                    if p.findtext(namespace + 'polling_place_name') == 'Early Voting Absent Ordinary Votes']
        if selected:
            counts[candidate.findtext(namespace + 'candidate_id')] = sum(int(p.findtext(namespace + 'ballot_papers')) for p in selected)
    if counts.get('133004') == 189:
        return 848 - sum(counts.values())
    return 0


def select_source(paths, seat_counts):
    """Resolve repeated source timestamps using the counted account itself.

    An export cannot borrow flags from a different revision carrying the same
    timestamp. Refuse a mismatch rather than making a best-effort substitution.
    Record the chosen file hash in the replay result for later source updates.
    """
    matches = [(p, source_metadata(p)) for p in paths]
    matches = [(p, m) for p, m in matches if {s: r['counted'] for s, r in m.items()} == seat_counts]
    if not matches:
        raise ValueError('No raw source with matching district counts for this export; candidates: '
                         + ', '.join(p.name for p in paths))
    flags = [{s: r['finalised'] for s, r in m.items()} for _, m in matches]
    if any(f != flags[0] for f in flags):
        raise ValueError('Repeated source timestamp has conflicting FP finalisation flags.')
    return matches[-1]


def group_and_kind(booth):
    """Map current labels into the three historically supported SA groups.

    The early group combines ordinary PPVC and declaration early votes. The
    remainder includes polling-day ordinary booths and other declarations.
    Finer weights below are forecast assumptions, not fabricated old SA splits.
    """
    if booth['vote_type'] == 'Ordinary':
        return ('early', 'ppvc') if booth['booth_type'] == 'PPVC' else ('remaining', 'ordinary')
    if booth['vote_type'] in ('PrePoll', 'Early Provisional'):
        return 'early', 'declaration'
    return ('postal' if booth['vote_type'] == 'Postal' else 'remaining'), 'declaration'


def build_units(template, inputs, previous):
    """Freeze the old no-results forecast's within-group allocation weights.

    Exact-name, same-seat matches must also reproduce the exported previous
    count. This rules out moved or ambiguous booths for adaptation, while
    retaining their current votes and an unreported forecast weight. Boundary
    changes near otherwise good matches are not modelled by this prototype.
    """
    names, categories = inputs['seat_names'], inputs['categories']
    units = []
    for booth in template['booths']:
        seat, name = booth['seat_name'], booth['name']
        group, kind = group_and_kind(booth)
        previous_booth = previous.get(seat, {}).get('booths', {}).get(name)
        exported_previous = vote_sum(booth['node'], 'fp_votes_previous')
        matched = bool(booth['same_seat'] and previous_booth and exported_previous > 0
                       and sum(previous_booth['fp'].values()) == exported_previous)
        units.append(dict(seat_name=seat, name=name, vote_type=booth['vote_type'], kind=kind,
                          seat_index=names.index(seat), group_index=categories.index(group),
                          group=group, matched=matched, previous=exported_previous,
                          weight=vote_sum(booth['node'], 'fp_votes_projected'),
                          closed_reason=CLOSED_BOOTHS.get((seat, name))))
    # Closed booths lose their own future weight. Their district's overall
    # turnout target stays unchanged, so the remaining pool can move elsewhere.
    for seat in range(len(names)):
        for group in range(len(categories)):
            selected = [u for u in units if u['seat_index'] == seat and u['group_index'] == group]
            for unit in selected:
                unit['weight'] = 0 if unit['closed_reason'] else max(.5, unit['weight'])
            amount = sum(u['weight'] for u in selected)
            if amount <= 0:
                raise ValueError('No open template support for district group.')
            for unit in selected:
                unit['weight'] /= amount
    return units


def snapshot_units(base, analysis, inputs):
    """Attach counted votes from this snapshot without changing the prior."""
    booths = {(b['seat_name'], b['name']): b for b in analysis['booths']}
    if booths.keys() != {(u['seat_name'], u['name']) for u in base}:
        raise ValueError('Snapshot booth identities differ from the immutable template.')
    units = []
    for unit in base:
        booth = booths[(unit['seat_name'], unit['name'])]
        if group_and_kind(booth) != (unit['group'], unit['kind']):
            raise ValueError('Snapshot category definition differs from the template.')
        votes = booth['node']['fp_votes_current']
        if any(v['value'] < 0 or int(v['value']) != v['value'] for v in votes):
            raise ValueError('Counted account contains a negative or non-integer vote count.')
        matched = unit['matched'] and booth['same_seat'] and vote_sum(booth['node'], 'fp_votes_previous') == unit['previous']
        units.append(dict(unit, matched=bool(matched), counted=vote_sum(booth['node'], 'fp_votes_current'), counted_party_votes=votes))
    seat_counts = {s['name']: vote_sum(s['node'], 'fp_votes_current') for s in analysis['seats']}
    for name in inputs['seat_names']:
        if sum(u['counted'] for u in units if u['seat_name'] == name) != seat_counts[name]:
            raise ValueError('Seat and booth counted accounts do not reconcile: ' + name)
    return units, seat_counts


def scoring_data(path, names):
    """Load reviewed outcomes only after constructing the prediction inputs.

    Unknown category partitions remain omitted. District formal totals are
    trusted even where a detailed declaration split is unreliable.
    """
    dataset = read_json(path)
    totals = {s['seat_name']: s['formal_votes'] for s in dataset['seat_totals']}
    from lib.shared import turnout_data
    reviewed = category_policy.missing_count_review(turnout_data.load_dataset(path))
    unreliable = {r['seat'] for r in reviewed}
    groups = np.full((len(names), 3), np.nan)
    for i, name in enumerate(names):
        if name in unreliable:
            continue
        grouped = [0, 0, 0]
        for row in dataset['vote_types']:
            if row['seat_name'] != name:
                continue
            category = row['canonical_category']
            group = 0 if category in ('early_in_person', 'declaration_early') else 1 if category == 'postal' else 2
            grouped[group] += row['formal_votes']
        if sum(grouped) != totals[name]:
            raise ValueError('Reviewed scoring partition does not reconcile: ' + name)
        groups[i] = grouped
    return dict(totals=np.array([totals[n] for n in names]), groups=groups, omitted=sorted(unreliable))


def interval_summary(values, actual, *, mean=None, bounds=None):
    """Score one snapshot with equal weight for each known district or group.

    A 95% interval score is its width plus a forty-fold penalty for each vote
    by which the final result falls outside it. Smaller scores reward both
    useful precision and adequate coverage; all results here are in votes.
    """
    valid = np.isfinite(actual)
    # Explicit mixtures can supply their full means and interval endpoints;
    # their sample arrays may describe only the conditional positive branch.
    low, high = np.quantile(values, [.025, .975], axis=0) if bounds is None else bounds
    mean = values.mean(axis=0) if mean is None else mean
    score = high - low + 40 * (np.maximum(low - actual, 0) + np.maximum(actual - high, 0))
    return dict(observations=int(valid.sum()), mean_absolute_error=float(np.abs(mean - actual)[valid].mean()),
                coverage_95=float(((low <= actual) & (actual <= high))[valid].mean()),
                width_95=float((high - low)[valid].mean()), interval_score_95=float(score[valid].mean()))


def booth_scoring_data(normalized_path):
    """Find the retained final source revision named by the normalized evidence.

    Use final booth counts only to measure prediction error and reporting-order
    bias. A revised normalized source must have its matching raw capture; using
    whichever results.json happens to be newest would hide source revisions.
    """
    source = next(s for s in read_json(normalized_path)['sources'] if s['source_id'] == 'ecsa-2026-assembly-final-json')
    expected = re.search(r'results URL=\S+ SHA256=([a-f0-9]{64})', source['notes'])[1]
    path = next((p for p in (REPOSITORY_DIRECTORY / 'downloads/turnout/2026sa').glob('version-*/results.json')
                 if digest(p) == expected), None)
    if path is None:
        raise ValueError('The reviewed final source revision is not retained locally.')
    booths, kinds, empty = {}, {}, []
    for district in read_json(path)['districts']:
        for booth in district['pollingPlaces']:
            if booth['pollingPlaceTypeIdName'] not in {'PB', 'S1', 'S2', 'S3', 'PP', 'P1', 'P2', 'P3'}:
                continue
            key = (district['districtId'], booth['pollingPlaceName'].casefold())
            if key in booths:
                raise ValueError('Duplicate final booth identity.')
            formal = sum(c['formalVotes'] for c in booth['pollingCandidates'])
            if formal == 0 and booth['informalVotes'] == 0:
                # A named final placeholder with no ballots does not establish
                # a behavioural zero. Omit its booth score pending review, while
                # keeping an unexplained current nonreporter in the prediction.
                empty.append(dict(seat=district['districtId'], name=booth['pollingPlaceName']))
                continue
            booths[key] = formal
            kinds[key] = 'ppvc' if booth['pollingPlaceTypeIdName'] in {'PP', 'P1', 'P2', 'P3'} else 'ordinary'
    return booths, kinds, empty, path


def substantial_complete_booth_differences(units, final_booths):
    """Export exceptional reported-to-final differences for human assessment.

    A difference of at least 100 votes is a review trigger, not an estimate of
    correction probability. It may reflect a revision or identity/reporting
    anomaly. It never broadens normal completed-booth uncertainty or changes
    current counted votes in the prediction path.
    """
    result = []
    for unit in units:
        key = (unit['seat_name'], unit['name'].casefold())
        if unit['kind'] != 'declaration' and unit['counted'] > 0 and key in final_booths:
            difference = final_booths[key] - unit['counted']
            if abs(difference) >= 100:
                result.append(dict(seat=unit['seat_name'], name=unit['name'],
                                   counted=unit['counted'], final=final_booths[key], difference=difference))
    return result


def reporting_diagnostics(units, central, final_booths):
    """Expose which sizes report first, without treating them as random voters."""
    rows = []
    for kind in ('ordinary', 'ppvc'):
        eligible = [u for u in units if u['kind'] == kind and u['matched'] and not u['closed_reason']]
        pivot = np.median([central[u['seat_index'], u['group_index']] * u['weight'] for u in eligible]) if eligible else 0
        for size in ('smaller', 'larger'):
            selected = [u for u in eligible if (central[u['seat_index'], u['group_index']] * u['weight'] <= pivot) == (size == 'smaller')]
            reported = [u for u in selected if u['counted'] > 0]
            ratios = [np.log(u['counted'] / (central[u['seat_index'], u['group_index']] * u['weight'])) for u in reported]
            final_ratios = [np.log(final_booths[(u['seat_name'], u['name'].casefold())] /
                                  (central[u['seat_index'], u['group_index']] * u['weight'])) for u in selected
                            if final_booths.get((u['seat_name'], u['name'].casefold()), 0) > 0]
            rows.append(dict(kind=kind, size=size, eligible=len(selected), reported=len(reported),
                             mean_log_ratio=float(np.mean(ratios)) if ratios else None,
                             final_mean_log_ratio=float(np.mean(final_ratios)) if final_ratios else None))
    return rows


def run(args):
    """Replay selected snapshots, then compare with outcomes kept outside input construction."""
    started = time.perf_counter()
    fixture = load_fixture(args.fixture)
    inputs, parameters = fixture['inputs'], fixture['parameters']
    exports = sorted(args.archive.glob('snapshot_*__run_*.json'))
    exports = [p for p in exports if '.analysis.' not in p.name and p.name.split('__')[0][9:17] <= args.through.replace('-', '')]
    by_stamp = {}
    for path in exports:
        by_stamp[path.name.split('__')[0][9:]] = path
    if not by_stamp:
        raise ValueError('No SA replay exports within the requested dates.')
    first = by_stamp[min(by_stamp)]
    template_path = analysis_path_for_snapshot(first)
    template = read_json(template_path)
    if any(vote_sum(b['node'], 'fp_votes_current') for b in template['booths']):
        raise ValueError('The earliest export must be a no-results allocation template.')
    previous_path = ANALYSIS_DIRECTORY / 'Booth Results/2022sa.json'
    base = build_units(template, inputs, read_json(previous_path))
    sources = source_index(args.sources)
    draws = prior.draw(inputs, parameters, args.samples, args.seed)
    _, central = prior.central_counts(inputs, parameters)
    selected = sorted(by_stamp) if args.all_snapshots else sorted({min(by_stamp, key=lambda s: abs(
        (datetime.strptime(s, '%Y%m%d%H%M%S') - datetime.strptime(t, '%Y%m%d%H%M%S')).total_seconds())) for t in CHECKPOINTS
        if t[:8] <= args.through.replace('-', '')} | {min(by_stamp), max(by_stamp)})
    truth = scoring_data(args.final_counts, inputs['seat_names'])
    final_booths, final_kinds, final_empty, final_booth_path = booth_scoring_data(args.final_counts)
    snapshots, provenance = [], {}
    for stamp in selected:
        main_path = by_stamp[stamp]
        sidecar = analysis_path_for_snapshot(main_path)
        main, analysis = read_json(main_path), read_json(sidecar)
        if main['run']['term_code'] != '2026sa' or main['run']['snapshot_code'] != stamp:
            raise ValueError('Export run identity disagrees with its filename.')
        units, seat_counts = snapshot_units(base, analysis, inputs)
        source_path, metadata = select_source(sources[stamp], seat_counts)
        finalised = [name for name, value in metadata.items() if value['finalised']]
        changes = live.pooled_booth_changes(units, central, args.prior_booths)
        update_started = time.perf_counter()
        unchanged = live.update(draws, inputs, units, finalised)
        adjusted = live.update(draws, inputs, units, finalised, changes)
        seconds = time.perf_counter() - update_started
        legacy = np.array([vote_sum(next(s for s in analysis['seats'] if s['name'] == name)['node'], 'fp_votes_projected')
                           for name in inputs['seat_names']])
        rows = []
        for i, name in enumerate(inputs['seat_names']):
            low, high = np.quantile(adjusted['totals'][:, i], [.025, .975])
            rows.append(dict(seat=name, counted=seat_counts[name], actual=int(truth['totals'][i]),
                             legacy_total=float(legacy[i]), mean_total=float(adjusted['totals'][:, i].mean()),
                             mean_remaining=float(adjusted['totals'][:, i].mean() - seat_counts[name]),
                             counted_fraction_of_mean_total=float(seat_counts[name] / adjusted['totals'][:, i].mean()),
                             low_95=float(low), high_95=float(high),
                             mean_groups=adjusted['counts'][:, i].mean(axis=0).tolist(),
                             actual_groups=[float(v) if np.isfinite(v) else None for v in truth['groups'][i]]))
        # Unit exports retain the original counted party records. Additions are
        # count estimates only: this prototype does not create new party shares.
        unit_rows = [dict(u, complete=bool(adjusted['complete'][j]),
                          final_mean=float(adjusted['unit_counts'][:, j].mean()),
                          unadjusted_final_mean=float(unchanged['unit_counts'][:, j].mean())) for j, u in enumerate(units)]
        booth_errors = []
        current_booths = {(b['seat_name'], b['name']): b for b in analysis['booths']}
        for j, u in enumerate(units):
            key = (u['seat_name'], u['name'].casefold())
            if u['kind'] == 'declaration' or u['counted'] or u['closed_reason'] or key not in final_booths or final_kinds[key] != u['kind']:
                continue
            actual = final_booths[key]
            legacy_booth = vote_sum(current_booths[(u['seat_name'], u['name'])]['node'], 'fp_votes_projected')
            booth_errors.append(dict(kind=u['kind'], actual=actual, legacy=abs(legacy_booth - actual),
                unadjusted=abs(unchanged['unit_counts'][:, j].mean() - actual),
                adjusted=abs(adjusted['unit_counts'][:, j].mean() - actual)))
        booth_scores = []
        for kind in ('ordinary', 'ppvc'):
            matching = [b for b in booth_errors if b['kind'] == kind]
            booth_scores.append(dict(kind=kind, booths=len(matching), **{method: float(np.mean([b[method] for b in matching]))
                if matching else None for method in ('legacy', 'unadjusted', 'adjusted')}))
        snapshots.append(dict(source_timestamp=stamp, run_generated_at=main['run']['completed_at'],
            counted=sum(seat_counts.values()), final_remaining=int(truth['totals'].sum() - sum(seat_counts.values())),
            legacy_total_mae=float(np.abs(legacy - truth['totals']).mean()),
            total_score=interval_summary(adjusted['totals'], truth['totals']),
            category_scores=dict(unadjusted=interval_summary(unchanged['counts'], truth['groups']),
                                 adjusted=interval_summary(adjusted['counts'], truth['groups'])),
            changes=changes, reporting=reporting_diagnostics(units, central, final_booths), booth_scores=booth_scores, update_seconds=seconds,
            source_account_diagnostics=[dict(seat=name, **m) for name, m in metadata.items()
                                        if m['summary'] != m['raw_detailed'] or m['archived_count_correction']],
            substantial_complete_booth_differences=substantial_complete_booth_differences(units, final_booths),
            diagnostics=adjusted['diagnostics'], districts=rows, units=unit_rows))
        provenance[stamp] = {str(p): digest(p) for p in (main_path, sidecar, source_path)}
    paths = [args.fixture, template_path, previous_path, args.final_counts, final_booth_path,
             Path(__file__), Path(live.__file__), Path(prior.__file__), Path(category_policy.__file__),
             Path(turnout_live_report.__file__), Path(live_analysis_archive.__file__)]
    return dict(model_version=live.MODEL_VERSION, prior_model=prior.MODEL_VERSION,
                compensation=live.compensation_configuration(),
                config=dict(samples=args.samples, seed=args.seed, prior_equivalent_booths=args.prior_booths,
                            through=args.through, all_snapshots=args.all_snapshots),
                libraries=dict(numpy=np.__version__, scipy=scipy.__version__),
                provenance=dict(inputs={str(p): digest(p) for p in paths}, snapshots=provenance),
                omitted_scoring_partitions=truth['omitted'], empty_final_booth_placeholders=final_empty, snapshots=snapshots,
                elapsed_seconds=time.perf_counter() - started)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument('--sources', type=Path, default=download_directory())
    parser.add_argument('--fixture', type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument('--final-counts', type=Path, default=ANALYSIS_DIRECTORY / 'Data/Turnout/2026sa.json')
    parser.add_argument('--output-directory', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--through', default='2026-03-27')
    parser.add_argument('--samples', type=int, default=1024)
    parser.add_argument('--seed', type=int, default=20261002)
    parser.add_argument('--prior-booths', type=float, default=8)
    parser.add_argument('--all-snapshots', action='store_true')
    parser.add_argument('--dry-run', action='store_true', help='Perform the replay without writing outputs.')
    args = parser.parse_args()
    if args.samples < 2 or args.prior_booths <= 0:
        parser.error('Use at least two samples and a positive prior-booth strength.')
    result = run(args)
    if not args.dry_run:
        args.output_directory.mkdir(parents=True, exist_ok=True)
        (args.output_directory / 'analysis.json').write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
        (args.output_directory / 'report.md').write_text(turnout_live_report.render(result), encoding='utf-8')
    print(json.dumps(dict(model=result['model_version'], snapshots=len(result['snapshots']),
                          seconds=round(result['elapsed_seconds'], 3), dry_run=args.dry_run)))


if __name__ == '__main__':
    main()
