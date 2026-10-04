"""Compare slowing-count rules and smooth evidence weights on retained feeds.

This is an offline diagnostic, not a change to the live forecast. Current and
earlier counts define each signal; reviewed final counts measure what followed.
One earliest qualifying observation per district/category avoids treating many
unchanged late snapshots as independent successes. Results are JSON for analysis;
the assumptions and findings can be explained separately from the calculations.
"""

import argparse
from datetime import datetime, timedelta, timezone
from ftplib import FTP
import hashlib
import io
from itertools import product
import json
from pathlib import Path
import re

import numpy as np
from scipy.stats import norm, t

from lib.paths import REPOSITORY_DIRECTORY
from scripts.turnout import turnout_declaration_progress as progress
from scripts.turnout import turnout_live_prototype as sa


def fetch_daily_checkpoints(code, refresh=False):
    """Fill late-count sampling gaps without downloading every released feed.

    Select one official checkpoint near 21:00 on days 8–28 for the older
    elections, and days 20–28 for 2025, whose preceding daily sources are local.
    The existing content-addressed cache preserves replaced source bytes. Its
    manifest records the current revision and is included in output provenance.
    """
    event, date = progress.FEDERAL[code]
    directory = REPOSITORY_DIRECTORY/'downloads/turnout/declaration-progress'/code
    manifest_path = directory/'latest.json'
    manifest = sa.read_json(manifest_path) if manifest_path.exists() else {}
    with FTP('mediafeedarchive.aec.gov.au', timeout=30) as ftp:
        ftp.login()
        ftp.cwd('/'+event+'/Detailed/Light')
        candidates = {}
        for name in ftp.nlst():
            match = re.fullmatch(r'aec-mediafeed-Detailed-Light-'+event+r'-(\d{14})\.zip', name)
            if match:
                candidates[datetime.strptime(match[1], '%Y%m%d%H%M%S')] = name
        date = datetime.fromisoformat(date)
        for day in range(20 if code == '2025fed' else 8, 29):
            target = date+timedelta(days=day, hours=21)
            stamp = min(candidates, key=lambda t: abs((t-target).total_seconds()))
            # Do not label a checkpoint from another day as today's observation.
            if stamp.date() != target.date():
                continue
            name = candidates[stamp]
            if name in manifest and not refresh:
                continue
            buffer = io.BytesIO()
            ftp.retrbinary('RETR '+name, buffer.write)
            data = buffer.getvalue()
            fingerprint = hashlib.sha256(data).hexdigest()
            path = directory/fingerprint/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            manifest[name] = dict(sha256=fingerprint,
                url='ftp://mediafeedarchive.aec.gov.au/'+event+'/Detailed/Light/'+name,
                retrieved_at=datetime.now(timezone.utc).isoformat())
            # Persist each successful acquisition so an interrupted run can
            # resume without discarding already retained source revisions.
            manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(code+' checkpoints retained', flush=True)


def observations(snapshots, categories, final, hours=72, relative_tolerance=.001, state=None):
    """Measure recent movement, including districts that are not yet quiet.

    The window starts at the latest actual observation approximately `hours` old.
    Allow one hour of release-clock variation so a few minutes do not turn a
    three-day comparison into four days. Record the actual interval throughout.
    Allow up to 48 extra hours because the archive is sampled sparsely, but do
    not infer stability across a longer unobserved gap. A rise and subsequent
    correction is activity even if the endpoints agree. Pooling includes known
    zero counts, so a category that has not started cannot look finished simply
    because the already-started districts are quiet. Unknown counts stay absent.
    """
    rows = []
    for i, current in enumerate(snapshots):
        time = datetime.fromisoformat(current['source_time'])
        earlier = [j for j, s in enumerate(snapshots[:i])
                   if (time-datetime.fromisoformat(s['source_time'])).total_seconds() >= (hours-1)*3600]
        if not earlier:
            continue
        start = earlier[-1]
        span = (time-datetime.fromisoformat(snapshots[start]['source_time'])).total_seconds()/3600
        if span > hours+48:
            continue
        for category in categories:
            measured = []
            for seat, data in current['seats'].items():
                if state is not None and data.get('state') != state:
                    continue
                history = [s['seats'].get(seat, {}).get('vote_types', {}).get(category)
                           for s in snapshots[start:i+1]]
                if any(v is None for v in history):
                    continue
                value, movement = history[-1], max(history)-min(history)
                tolerance = max(10, relative_tolerance*value)
                measured.append((seat, value, movement, tolerance))
            # A small jurisdiction cannot support a useful fraction-of-seats
            # comparison. Its districts still enter the whole-election analysis.
            if len(measured) < 10:
                continue
            total = sum(value for _, value, _, _ in measured)
            if total == 0:
                continue
            active = sum(change > tolerance for _, _, change, tolerance in measured)/len(measured)
            started = sum(value > 0 for _, value, _, _ in measured)/len(measured)
            magnitude = 100*sum(change for _, _, change, _ in measured)/total
            # The threshold grid uses discrete started/quiet classifications.
            # The continuous alternative also smooths those classifications:
            # ten votes gives half-strength evidence of starting, and movement
            # equal to the recheck tolerance gives half-strength activity.
            # Thus a single extra vote cannot abruptly switch a pooled indicator.
            activity_evidence = sum(1-1/(1+(change/tolerance)**2)
                                    for _, _, change, tolerance in measured)/len(measured)
            started_evidence = sum(value/(value+10) for _, value, _, _ in measured)/len(measured)
            for seat, value, movement, tolerance in measured:
                target = final.get(seat, {}).get(category)
                if value < 100 or target is None:
                    continue
                rows.append(dict(seat=seat, category=category, day=current['day'],
                    source_timestamp=current['source_time'], scope=state or 'whole_election',
                    current=value, net_remaining=target-value, window_hours=span,
                    window_change=movement, local_movement_ratio=movement/tolerance,
                    active_fraction=active, started_fraction=started,
                    activity_evidence_mean=activity_evidence, started_evidence_mean=started_evidence,
                    activity_percent=magnitude, pooling_districts=len(measured)))
    return rows


def selection_summary(rows):
    """Score the first signal for each district/category, including exceptions.

    Net remaining counts may be negative after rechecks or category corrections;
    keep those separate from positive future additions. These summaries describe
    selected observations, rather than estimating independent trial probabilities.
    """
    earliest = {}
    for row in sorted(rows, key=lambda r: r['source_timestamp']):
        earliest.setdefault((row['seat'], row['category']), row)
    selected = list(earliest.values())
    if not selected:
        return dict(district_categories=0)
    positive = np.array([max(0, r['net_remaining']) for r in selected])
    substantial = [r for r in selected if r['net_remaining'] > max(100, .01*r['current'])]
    return dict(district_categories=len(selected), substantial=len(substantial),
        substantial_percent=100*len(substantial)/len(selected),
        median_signal_day=float(np.median([r['day'] for r in selected])),
        mean_positive_addition=float(positive.mean()),
        p95_positive_addition=float(np.quantile(positive, .95)),
        largest_positive_addition=int(positive.max()),
        negative_recheck_cases=sum(r['net_remaining'] < 0 for r in selected),
        largest_later_additions=sorted(selected, key=lambda r: r['net_remaining'], reverse=True)[:5])


def threshold_configurations():
    """Compare shared progress criteria, then a few local-window variations.

    The full small grid tests whether the fraction of districts still changing
    needs a separate limit on the amount of vote movement. The additional rows
    check sensitivity to the local quietness definition without multiplying
    every possible parameter combination into a large tuning exercise.
    """
    configs = [dict(hours=72, tolerance=.001, started=started, active=active, magnitude=magnitude)
               for started, active, magnitude in product((.8, .9, .95), (.1, .25, .5), (None, .1, .5, 2.))]
    configs += [dict(hours=hours, tolerance=.001, started=.9, active=active, magnitude=magnitude)
                for hours, active, magnitude in product((48, 96), (.1, .25), (None, .5))]
    configs += [dict(hours=72, tolerance=tolerance, started=.9, active=.1, magnitude=magnitude)
                for tolerance, magnitude in product((.0005, .005, .01), (None, .5))]
    return configs


def smooth_weight(row, active_scale=.1, magnitude_scale=.5, unstarted_scale=.1,
                  shared_rule='product', fraction_weight=.75):
    """Give gradual strength to evidence that additional counting is unlikely.

    Each scale is the value where its individual factor falls to one half.
    Use continuous pooled activity and starting evidence, rather than binary
    district classifications. Own movement and weak starting evidence reduce
    either rule. Compare two
    ways to combine shared evidence: multiplying the fraction and amount of
    activity requires both to be small; averaging their evidence allows a few
    large moving districts to coexist with many apparently finished districts.
    This is an illustrative evidence weight, NOT a calibrated chance of
    completion or of a late batch. Final counts are not inputs to either rule.
    """
    ratios = (row['local_movement_ratio'], row['activity_evidence_mean']/active_scale,
              row['activity_percent']/magnitude_scale,
              (1-row['started_evidence_mean'])/unstarted_scale)
    local, active, magnitude, started = [1/(1+ratio*ratio) for ratio in ratios]
    if shared_rule == 'product':
        shared = active*magnitude
    elif shared_rule == 'blend':
        shared = fraction_weight*active+(1-fraction_weight)*magnitude
    else:
        raise ValueError('Choose product or blend for shared slowing-count evidence.')
    return float(local*started*shared)


def shape_illustration():
    """Show why equal variation size does not preserve a rare directional batch.

    This deliberately invented example has a two-percent chance of a one-point
    negative share shift, with 0.02 points of ordinary noise in either state.
    Analytic CDFs compare it with normal and heavy-tailed approximations using
    the same mean and variance. The t4 row illustrates the high-kurtosis limit
    of the C++ family, not its exact quantile lookup or asymmetric blending.
    No observed electorate probability or application timing is claimed here.
    """
    probability, shift, noise = .02, -1., .02
    mean = probability*shift
    sd = np.sqrt(noise**2+probability*(1-probability)*shift**2)
    cdfs = {
        'two_state_reference': lambda x: (1-probability)*norm.cdf(x, scale=noise)
                                             + probability*norm.cdf(x, loc=shift, scale=noise),
        'normal_same_mean_variance': lambda x: norm.cdf(x, loc=mean, scale=sd),
        'student_t4_same_mean_variance': lambda x: t.cdf((x-mean)/(sd/np.sqrt(2)), df=4)}
    return dict(assumed_batch_probability=probability, assumed_batch_shift_pp=shift,
        ordinary_noise_sd_pp=noise, mean_shift_pp=mean, sd_pp=float(sd),
        models={name:dict(within_005_pp=100*(cdf(.05)-cdf(-.05)),
            negative_shift_over_05_pp=100*cdf(-.5),
            positive_shift_over_05_pp=100*(1-cdf(.5))) for name,cdf in cdfs.items()},
        preparation_missing_batch_probability={str(p):float((1-p)**288) for p in (.001,.005,.01,.02)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=Path('F:/Election Data/AEC media feed archive'))
    parser.add_argument('--downloads', type=Path, default=Path.home()/'Downloads')
    parser.add_argument('--fetch', action='store_true', help='Retain selected daily official late checkpoints.')
    parser.add_argument('--refresh', action='store_true', help='Refresh retained checkpoints, preserving old bytes.')
    parser.add_argument('--output', type=Path, default=progress.DIRECTORY/'declaration-thresholds.json')
    args = parser.parse_args()
    configurations = threshold_configurations()
    results, files = [], [Path(__file__), Path(progress.__file__), Path(sa.__file__)]
    for code in (*progress.FEDERAL, '2026sa'):
        if code in progress.FEDERAL and (args.fetch or args.refresh):
            fetch_daily_checkpoints(code, args.refresh)
        history, snapshots, sources = progress.analyse_history(code, args.archive, args.downloads)
        files.extend(sources)
        _, final, _, _ = progress.final_counts(code)
        scopes = [None] + sorted({d['state'] for s in snapshots for d in s['seats'].values() if 'state' in d})
        for scope in scopes:
            prepared = {}
            # Source parsing is shared across all alternatives. Only small
            # category-count arrays are examined for different time windows.
            for hours, tolerance in {(c['hours'],c['tolerance']) for c in configurations}:
                prepared[hours,tolerance] = observations(snapshots, history['categories'], final, hours, tolerance, scope)
            thresholds = []
            for config in configurations:
                selected = [r for r in prepared[config['hours'], config['tolerance']]
                    if r['local_movement_ratio'] <= 1 and r['started_fraction'] >= config['started']
                    and r['active_fraction'] <= config['active']
                    and (config['magnitude'] is None or r['activity_percent'] <= config['magnitude'])]
                thresholds.append(dict(config=config, **selection_summary(selected)))
            base = prepared[72,.001]
            continuous = []
            for active, magnitude, unstarted, rule in product((.1,.25), (.1,.5,2.), (.05,.1), ('product','blend')):
                parameters = dict(active_scale=active, magnitude_scale=magnitude, unstarted_scale=unstarted,
                                  shared_rule=rule)
                scored = [dict(r, weight=smooth_weight(r, **parameters)) for r in base]
                continuous.append(dict(parameters=parameters,
                    crossings=[dict(minimum_weight=w, **selection_summary(r for r in scored if r['weight'] >= w))
                               for w in (.25,.5,.75,.9)]))
            results.append(dict(election=code, scope=scope or 'whole_election', thresholds=thresholds,
                smooth_weights=continuous, observations=base))
        print(code+' compared', flush=True)
    # Hash the current readers as well as feeds/manifests so a later source or
    # parser update is distinguishable from this exact retained comparison.
    files.extend([Path(progress.aec_live.__file__), Path(progress.category_policy.__file__),
                  Path(progress.turnout_data.__file__), REPOSITORY_DIRECTORY/'LiveV2.cpp',
                  REPOSITORY_DIRECTORY/'RandomGenerator.h'])
    result = dict(results=results, shape_illustration=shape_illustration(),
        definitions=dict(unit='First qualifying observation for each district/category, separately within each election and pooling scope.',
            substantial='Net later addition exceeds both 100 votes and 1% of the currently counted category.',
            activity='Largest minus smallest observed count; includes reversed corrections, not just net growth.',
            shared_activity='Sum of district count ranges divided by current category votes across measured districts.',
            scope='Whole election or an individual federal state with at least ten measured districts.',
            continuous='Illustrative strength of slowing-count evidence, not a fitted completion probability.',
            continuous_inputs='Mean movement strength is change_squared/(change_squared+tolerance_squared); mean starting strength is current_count/(current_count+10).',
            final_targets='Reviewed final counts score net changes; they cannot distinguish gross arrivals from corrections.'),
        provenance={str(p):sa.digest(p) for p in dict.fromkeys(files)})
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print('Saved '+str(args.output), flush=True)


if __name__ == '__main__':
    main()
