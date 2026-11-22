"""Check what the completed SA replay could have inferred about One Nation's late votes.

Read the numerical forecast and its accepted, per-location vote accounts at
each source time. Change only the allocation of outstanding postal and absent
votes in a retrospective calculation. The reviewed state result enters the
score and the account of the eventual miss, never the alternative predictions.
"""
import argparse
from datetime import datetime, timedelta
import gzip
import json
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.optimize import brentq
from scipy.special import expit, logit

from history import HERE, ROOT, MODES, REFERENCE, contrast, historical_evidence, reviewed_sa, score_transfer

DEFAULT_RUN = ROOT / 'live_runs/2026sa-1-late-movement'
PHYSICAL = ['Ordinary', 'Early centre']
FIELDS = ['counted', 'counted_on', 'projected', 'projected_on']


def read_packages(run):
    """Use the last issued numerical package for a repeated commission time."""
    by_time = {}
    for path in sorted((run/'packages').glob('f*/numerical.json')):
        package = json.loads(path.read_bytes())
        report = package['report']
        party, = [i for i, name in report['partyAbbr'] if name in ('ON','ONP')]
        bands = dict(zip(report['voteTotalThresholds'], dict(report['fpFrequencies'])[party]))
        time = datetime.fromisoformat(package['source_time'])
        by_time[time] = dict(time=time, package=path.parent.name, party=str(party),
                             revision=package['input_revision'], low=bands[5], median=bands[50], high=bands[95])
    return list(sorted(by_time.values(), key=lambda x: x['time']))


def accepted_accounts(run, package):
    """Keep each district and voting population separate, including explicit zero accounts.

    The archived per-booth evidence carries the numerical model's accepted
    ballots and projected final ballots. Summing all candidates includes
    small parties and independents in the formal denominator. SA mobile teams
    and physical early centres have the ordinary feed label.
    """
    with gzip.open(run/'packages'/package['package']/'evidence.json.gz', 'rt', encoding='utf-8') as f:
        evidence = json.load(f)
    rows = []
    for b in evidence['booths'].values():
        mode = b['vote_type']
        if mode == 'Ordinary':
            if b['booth_type'] == 'PPVC':
                mode = 'Early centre'
            elif b['name'].endswith(' Mobile'):
                mode = 'Mobile'
        counts, estimate = b['fp_votes_current'], b['fp_votes_projected']
        party = package['party']
        rows.append((b['seat_name'], mode, sum(counts.values()), counts.get(party, 0),
                     sum(estimate.values()), estimate.get(party, 0)))
    frame = pd.DataFrame(rows, columns=['seat','mode',*FIELDS]).groupby(['seat','mode']).sum()
    if frame.index.get_level_values('seat').nunique() != 47:
        raise ValueError('Incomplete district accounts at '+package['package'])
    # Whole counts can have a small decimal rounding error in the archived
    # projection. A material negative remainder would invalidate the comparison.
    if (frame.projected+0.1 < frame.counted).any() or (frame.projected_on+0.1 < frame.counted_on).any():
        raise ValueError('Projected ballots below accepted ballots at '+package['package'])
    return frame


def category_inputs(accounts, prior, transfer_errors):
    """Prepare predictions using only this snapshot, federal results and earlier elections.

    Federal ordinary includes home-division early centres. Use the estimated
    combined home-district polling-day and early result in each state seat as
    its comparable reference. The prior predicts category differences on
    log odds, so it transfers relative support when the party is at 22% here
    and was near 6% federally. The raw federal percentage is never a baseline.
    """
    in_person = accounts.reset_index().query('mode in @PHYSICAL').groupby('seat')[FIELDS].sum()
    # For this one-party comparison, half a ballot each for One Nation and
    # all other candidates combined prevents infinite odds at zero or 100%.
    in_person_share = (in_person.projected_on + .5) / (in_person.projected + 1)
    base = logit(in_person_share).to_numpy()[:, None]

    def matrix(field):
        return accounts[field].unstack('mode').loc[in_person.index, list(MODES)].to_numpy()

    counted, on, projected, projected_on = [matrix(field) for field in FIELDS]
    remaining = np.maximum(0, projected - counted)
    measured = (on + .5) / (counted + 1)
    # Use the replay's own-category vote weights. Early absent (PrePoll) can
    # arrive in geographically distinct batches, so its local count gains
    # weight more slowly. Accepted ballots never enter the outstanding pool.
    weight = np.divide(counted, projected, out=np.zeros_like(projected), where=projected > 0).clip(0, 1)
    early = MODES.index('PrePoll')
    weight[:, early] = counted[:, early] / (counted[:, early] + 4 * remaining[:, early] + 300)

    # A peer district's evidence is its category share relative to *its own*
    # in-person level. Each target excludes its own category from the pooled
    # comparison, then uses its own observed share once via the local weight.
    # Between-division dispersion in 2025 SA and count noise limit a large
    # early district's influence. Historical state-level change sets the
    # uncertainty in the prior mean. These are working transfer assumptions.
    variance = prior.dispersion.to_numpy()**2 + 1 / (on + .5) + 1 / (counted - on + .5)
    precision = np.where(counted > 0, 1/variance, 0)
    prior_precision = 1 / np.mean(transfer_errors**2, axis=0)
    peer_precision = precision.sum(axis=0) - precision
    data = precision * (logit(measured) - base)
    total_precision = prior_precision + peer_precision
    prior_weight = prior_precision / total_precision
    peer_shift = (data.sum(axis=0) - data) / total_precision
    return dict(
        seats=in_person.index, base=base, counted=counted, on=on, remaining=remaining,
        home_reported=(in_person.counted.to_numpy() > 0)[:, None],
        replay_remaining=projected_on - on, measured=measured, local_weight=weight,
        federal=prior['shift'].to_numpy(), two_elections=prior.two_election_shift.to_numpy(),
        prior_weight=prior_weight, peers=peer_shift,
    )


def predict(inputs, offset):
    """Return expected outstanding One Nation ballots per district and category."""
    local_weight = inputs['local_weight']
    expected_share = expit(inputs['base'] + offset)
    share = (1 - local_weight) * expected_share + local_weight * inputs['measured']
    return inputs['remaining'] * share


def alternatives(accounts, inputs, snapshot, errors, configured_shifts):
    """Move the published median by the calculated change in statewide share.

    All other services and turnout sizes keep their archived estimates. This
    is an arithmetic sensitivity of the saved forecast. It cannot reconstruct
    the simulator's joint draws or a newly fitted prediction interval.
    """
    statewide_formal = accounts.projected.sum()

    def median(offset):
        extra_votes = (predict(inputs, offset) - inputs['replay_remaining']).sum()
        return snapshot['median'] + 100 * extra_votes / statewide_formal

    federal = inputs['federal']
    updated = inputs['prior_weight'] * federal + inputs['peers']
    result = dict(
        neutral=median(0), half_federal=median(federal * .5), federal=median(federal),
        two_elections=median(inputs['two_elections']), peers=median(updated),
    )
    # Apply the checked-in policy to these saved booth accounts. A district
    # still awaiting all in-person results retains the original projection.
    # As with the other medians, this measures the change in the booth sums
    # on the published scale. A full simulation also integrates seat priors.
    change = predict(inputs, configured_shifts) - inputs['replay_remaining']
    result['configured'] = snapshot['median'] + 100 * np.where(inputs['home_reported'], change, 0).sum() / statewide_formal
    # One historical state/election supplies all three perturbations across
    # all 47 districts. Symmetrising them represents uncertainty in either
    # direction; combining their separate squared effects instead measures
    # what independent categories would imply. Earlier reports remain fixed,
    # and current peers attenuate uncertainty in the historical prior.
    full, independent = [], []
    central_votes = predict(inputs, updated)
    for error in errors:
        for sign in (-1, 1):
            shifts = sign * error * inputs['prior_weight']
            changed_votes = predict(inputs, updated + shifts) - central_votes
            impacts = 100 * changed_votes.sum(axis=0) / statewide_formal
            full.append(impacts.sum())
            independent.append(impacts @ impacts)
    result['shared_rms_pp'] = np.sqrt(np.mean(np.square(full)))
    result['independent_modes_rms_pp'] = np.sqrt(np.mean(independent))
    return result


def reported_pattern(inputs, category_index):
    """Read what the current districts imply for this category in the rest of the count.

    Fit a common change in One Nation support relative to each reporting
    district's own in-person share. Then weight those district-specific shares
    by the replay's current estimates of where the uncounted votes are. This
    comparison uses only the count to date and allows the first districts to
    be stronger or weaker than the districts still to come. A category with
    no usable returns or no outstanding votes has no estimate from this test.
    """
    counted = inputs['counted'][:, category_index]
    on = inputs['on'][:, category_index]
    remaining = inputs['remaining'][:, category_index]
    if not 0 < on.sum() < counted.sum() or remaining.sum() <= 0:
        return np.nan
    base = inputs['base'][:, 0]
    shift = brentq(lambda d: counted @ expit(base + d) - on.sum(), -25, 25)
    return 100 * np.average(expit(base + shift), weights=remaining)


def later_district_mix(inputs, reviewed):
    """Distinguish a change in the reporting seats from a genuine change in late batches.

    Fit the counted statewide category ballots using the in-person levels
    for the districts that actually reported. Reweight that same relation to
    the districts in the eventual remaining count. Only this attribution uses
    hindsight about sizes. Revisions that cannot represent additional ballots
    are omitted from both sides and their affected seat count is reported.
    """
    results = []
    for j, mode in enumerate(MODES):
        final = reviewed.xs(mode, level='mode').reindex(inputs['seats']).fillna(0)
        current_n, current_on = inputs['counted'][:, j], inputs['on'][:, j]
        left_n = final.formal.to_numpy() - current_n
        left_on = final.on.to_numpy() - current_on
        valid = (left_n >= 0) & (left_on >= 0) & (left_on <= left_n)
        for scope, mask in [('all',valid),('already reporting',valid & (current_n>0))]:
            n, on = current_n[mask], current_on[mask]
            later, later_on = left_n[mask], left_on[mask]
            if not 0 < on.sum() < n.sum() or later.sum() <= 0:
                continue
            base = inputs['base'][mask, 0]
            shift = brentq(lambda d: n @ expit(base + d) - on.sum(), -25, 25)
            mix = 100 * np.average(expit(base + shift), weights=later)
            actual = 100 * later_on.sum() / later.sum()
            comparison = dict(
                mode=mode, scope=scope, counted_pct=100*on.sum()/n.sum(),
                later_pct=actual, later_if_same_within_seat_pattern=mix, later_residual_pp=actual-mix,
                earlier_ballots=int(n.sum()), later_ballots=int(later.sum()), invalid_seats=int((~valid).sum()),
            )
            if scope == 'already reporting':
                # Keep the exact observed category share within each reporting
                # district and weight it by that district's later ballots. The
                # residual removes changes in the mix of *these same districts*
                # without assuming they all have the same postal/absent tilt.
                from_own = 100 * np.average(on / n, weights=later)
                comparison['later_at_own_counted_pct'] = from_own
                comparison['within_reporting_district_change_pp'] = actual - from_own
            results.append(comparison)
    return results


def account_errors(accounts, reviewed, inputs):
    """Assign the gap from the booth projection to the reviewed state result.

    A missing reviewed category in a district contributes zero final ballots.
    The contribution is its party-vote error less the state's projected share
    of its formal-volume error, divided by all reviewed state formal ballots.
    The signed contributions add exactly to the statewide arithmetic gap.
    """
    joined = accounts.join(reviewed, how='outer').fillna(0)
    total = joined.formal.sum()
    centre = joined.projected_on.sum()/joined.projected.sum()
    joined['contribution_pp'] = 100*((joined.on-joined.projected_on)-centre*(joined.formal-joined.projected))/total
    grouped = joined.groupby(level='mode').sum()
    grouped['estimated_left'] = grouped.projected-grouped.counted
    grouped['actual_net_left'] = grouped.formal-grouped.counted
    grouped['expected_left_pct'] = 100*(grouped.projected_on-grouped.counted_on)/grouped.estimated_left.where(grouped.estimated_left>1)
    grouped['actual_net_left_pct'] = 100*(grouped.on-grouped.counted_on)/grouped.actual_net_left.where(grouped.actual_net_left>0)
    # The oracle changes *only* future ballot quantities in the three large
    # modes, using the replay's estimates for their remaining composition.
    # Keep negative revisions and units with no projected remainder separate
    # from this diagnostic: they do not have an estimated future share.
    core = joined.loc[joined.index.get_level_values('mode').isin(MODES)].copy()
    remaining = core.projected-core.counted
    keep = (remaining>1) & (core.formal>=core.counted)
    projected_share = ((core.projected_on-core.counted_on)/remaining.where(keep)).fillna(centre)
    volume = (core.formal-core.projected).where(keep,0)
    oracle = 100*((joined.projected_on.sum()+(volume*projected_share).sum())/(joined.projected.sum()+volume.sum())-centre)
    for i, mode in enumerate(MODES):
        count = inputs['counted'][:,i]
        grouped.loc[mode,'reporting_districts'] = (count>0).sum()
        if count.sum():
            grouped.loc[mode,'observed_pct'] = 100*inputs['on'][:,i].sum()/count.sum()
            grouped.loc[mode,'reporters_in_person_pct'] = 100*np.average(expit(inputs['base'][:,0]),weights=count)
            grouped.loc[mode,'remaining_from_reporting_pattern_pct'] = reported_pattern(inputs, i)
    if not np.isclose(grouped.contribution_pp.sum(),100*(joined.on.sum()/total-centre),atol=1e-8):
        raise ValueError('The statewide error account does not reconcile.')
    return grouped.reset_index(), oracle


def coverage(timeline, target):
    """Weight forecasts by the time for which they actually applied in the first week."""
    start = datetime(2026,3,22)
    end = start + timedelta(days=7)
    spans = (timeline.time.shift(-1).fillna(end).clip(lower=start,upper=end)-
             timeline.time.clip(lower=start,upper=end)).dt.total_seconds().clip(lower=0)/3600
    if not np.isclose(spans.sum(),168):
        raise ValueError('The full Sunday-to-Sunday week is not covered.')
    last = timeline.iloc[-1]
    accepted_share = 100 * last.counted_on / last.counted
    outside = (accepted_share > timeline.high) | (accepted_share < timeline.low)
    print(f'Last feed accepted {accepted_share:.4f}%, outside archived band for {spans[outside].sum():.2f} / 168 hours')
    for column in ('median','neutral','half_federal','federal','two_elections','peers','configured'):
        change = timeline[column]-timeline['median']
        miss = (target>timeline.high+change) | (target<timeline.low+change)
        print(f'{column:14} hours outside translated band {spans[miss].sum():6.2f} / 168, '
              f'time-weighted absolute median error {(spans*(timeline[column]-target).abs()).sum()/168:.3f} pp')


def plot(timeline, target, output):
    """Show the archived interval, the implemented rule and two comparison estimates."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.dates as dates
    import matplotlib.pyplot as plt
    start, end = datetime(2026,3,22), datetime(2026,3,30)
    selected = pd.concat([timeline[timeline.time<start].tail(1),timeline[(timeline.time>=start)&(timeline.time<=end)]]).copy()
    selected.iloc[0,selected.columns.get_loc('time')] = start
    selected = pd.concat([selected,selected.tail(1).assign(time=end)])
    fig, ax = plt.subplots(figsize=(9,4.1), layout='constrained')
    ax.fill_between(selected.time, selected.low, selected.high,step='post',color='#c8d5e5',alpha=.85,label='Replay 90% interval')
    ax.step(selected.time,selected['median'],where='post',color='#265483',lw=2,label='Replay median')
    ax.step(selected.time,selected.configured,where='post',color='#08725e',lw=2.3,label='SA replay rule (half postal weight)')
    ax.step(selected.time,selected.federal,where='post',color='#75568b',lw=1.6,ls=':',label='Full federal weight for all three types')
    ax.step(selected.time,selected.peers,where='post',color='#ac4f1c',lw=1.4,ls='-.',label='Full weight plus other districts’ returns')
    ax.axhline(target,color='#202b28',ls='--',lw=1.3,label=f'Reviewed result {target:.3f}%')
    ax.set_xlim(start, end)
    ax.set_ylabel('One Nation statewide first preferences (%)')
    ax.set_xlabel('March 2026 · South Australian source time')
    ax.xaxis.set_major_locator(dates.DayLocator())
    ax.xaxis.set_major_formatter(dates.DateFormatter('%a %d'))
    ax.grid(axis='y', alpha=.23)
    ax.spines[['top', 'right']].set_visible(False)
    ax.legend(fontsize=8,loc='lower right',framealpha=.95,ncol=2)
    fig.savefig(output / 'replay.png', dpi=165)
    plt.close(fig)


def audit_snapshots(run, prior, differences, reviewed, configured_shifts):
    """Produce as-of estimates at every source time and explain each day's final gap.

    Predictions receive only the original archived forecast and historical
    voting patterns. The reviewed SA outcomes are used to attribute errors
    after these predictions have been computed.
    """
    timeline, details, batch_mix = [], [], []
    packages = read_packages(run)
    last_each_day = {s['time'].date(): s['package'] for s in packages}
    for snapshot in packages:
        accounts = accepted_accounts(run, snapshot)
        inputs = category_inputs(accounts, prior, differences)
        trial = alternatives(accounts, inputs, snapshot, differences, configured_shifts)
        grouped, oracle = account_errors(accounts, reviewed, inputs)
        total = accounts[FIELDS].sum()
        timeline.append({
            **snapshot, **trial, **total.to_dict(), 'volume_oracle_change_pp': oracle,
            'booth_pct': 100 * total.projected_on / total.projected,
        })
        if snapshot['package'] == last_each_day[snapshot['time'].date()]:
            details.append(grouped.assign(time=snapshot['time'], package=snapshot['package']))
            for row in later_district_mix(inputs, reviewed):
                batch_mix.append({**row, 'time': snapshot['time']})
    return pd.DataFrame(timeline), pd.concat(details), pd.DataFrame(batch_mix)


def write_comparisons(output, timeline, daily, batch_mix):
    """Keep the replay's ballot accounts locally and publish the forecast comparisons.

    Each statewide checkpoint compares median estimates at the same time. The
    daily and batch tables compare expected and eventual shares of outstanding
    votes by voting method. Only the local audit needs the underlying counts,
    package identities and individual groups' accepted totals.
    """
    local = output / 'operator/count-history'
    local.mkdir(parents=True, exist_ok=True)
    tables = (
        ('checkpoints', timeline, 'forecast-comparisons',
         ['time', 'low', 'median', 'high', 'neutral', 'half_federal', 'federal', 'two_elections',
          'peers', 'configured', 'shared_rms_pp', 'independent_modes_rms_pp', 'volume_oracle_change_pp', 'booth_pct']),
        ('daily-accounts', daily, 'daily-comparisons',
         ['time', 'mode', 'expected_left_pct', 'actual_net_left_pct', 'contribution_pp',
          'reporters_in_person_pct', 'remaining_from_reporting_pattern_pct']),
        ('batch-mix', batch_mix, 'batch-comparisons',
         ['time', 'mode', 'scope', 'later_pct', 'later_if_same_within_seat_pattern', 'later_residual_pp',
          'later_at_own_counted_pct', 'within_reporting_district_change_pp', 'invalid_seats']),
    )
    for account_name, table, comparison_name, fields in tables:
        table.to_csv(local / (account_name + '.csv'), index=False, float_format='%.7f')
        table.to_csv(output / (comparison_name + '.csv'), columns=fields, index=False, float_format='%.7f')


def main(run, output):
    """Compare the as-of forecasts with the reviewed result and write the evidence tables and plot."""
    history, prior, differences, transfer_scores = historical_evidence()
    final = reviewed_sa()
    target = 100 * final.on.sum() / final.formal.sum()
    config = json.loads((ROOT / 'forecasts/2026sa/one-nation-declaration-prior.json').read_bytes())
    configured_shifts = np.array([
        config['federal_log_odds'][mode] * config['weights']['postal' if mode == 'Postal' else 'absent']
        for mode in MODES
    ])
    timeline, daily, batch_mix = audit_snapshots(run, prior, differences, final.set_index(['seat', 'mode']), configured_shifts)
    coverage(timeline, target)

    # The same scoring rule and denominator used for Queensland also show
    # how well the 2025 federal starting pattern transferred to SA in 2026.
    scored = final.copy()
    scored.loc[scored['mode'].isin(PHYSICAL), 'mode'] = REFERENCE
    scored = scored.groupby(['seat', 'mode'], as_index=False).sum()
    comparison = [dict(year=2026, state='SA', kind='state', mode=m, **contrast(scored, m)) for m in MODES]
    history = pd.concat([history, pd.DataFrame(comparison)], ignore_index=True)
    transfer_scores.append(score_transfer(scored, prior.reset_index(), 2026, 'SA'))

    output.mkdir(parents=True, exist_ok=True)
    write_comparisons(output, timeline, daily, batch_mix)
    tables = {'history': history, 'state-transfer': pd.DataFrame(transfer_scores)}
    for name, table in tables.items():
        table.to_csv(output / (name + '.csv'), index=False, float_format='%.7f')
    plot(timeline, target, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--run',type=Path,default=DEFAULT_RUN)
    parser.add_argument('--out',type=Path,default=HERE)
    args = parser.parse_args()
    main(args.run,args.out)
