"""Explain the implemented SA count replay and format its measured results."""

from datetime import datetime
import math


def number(value, decimals=0):
    return '—' if value is None else f'{value:,.{decimals}f}'


def stamp(value):
    return datetime.strptime(value, '%Y%m%d%H%M%S').strftime('%d %b %H:%M:%S')


def table(headings, rows):
    return ['| ' + ' | '.join(headings) + ' |', '| ' + ' | '.join(['---'] * len(headings)) + ' |'] + [
        '| ' + ' | '.join(str(v) for v in row) + ' |' for row in rows]


def render(result):
    """Keep methods, denominators and limitations alongside their results."""
    snapshots = result['snapshots']
    config = result['config']
    lines = ['# SA 2026 live vote-count prototype', '',
        'This report assesses whether a single district vote-total estimate can improve '
        'estimates of votes remaining as election results arrive. It replays archived '
        'SA 2026 counts, preserves the votes already counted, and compares estimated final '
        'counts with the reviewed final results.', '',
        'The prototype estimates vote counts only. It does not update party projections, '
        'winner probabilities. An optional C++ shadow calculation reproduces the count '
        'updater alongside the existing forecast; see [its reproduction instructions](../turnout-cpp-shadow.md). '
        'An outcome is one possible set '
        'of final counts across all 47 districts; the replay uses ' + str(config['samples']) +
        ' such outcomes at each snapshot in the initial comparisons. The counting-progress '
        'comparison, when included below, additionally varies declaration additions and can '
        'reduce the district total as counting slows.', '',
        'A **coherent allocation** means that all estimated counts agree with each '
        'other: counted votes are preserved, estimated additions are nonnegative, '
        'the units within each voting group add to that group’s total, and the '
        'groups add to one district total. For example, if a district is expected '
        'to finish with 25,000 formal votes and 20,000 are counted, its unfinished '
        'booths and declarations must receive exactly 5,000 additional votes '
        'between them. “Coherent” describes this accounting consistency; it does '
        'not mean the estimate is necessarily accurate.', '',
        '## Inputs and count treatment', '',
        'The frozen pre-election distribution comes from the saved SA count prior: '
        'previous turnout and formality, current enrolment, and district early-vote and '
        'postal-application measurements converted into expected formal votes. Its '
        'three supported groups are **early** (ordinary PPVC plus early declarations), '
        '**postal**, and **remaining** (polling-day ordinary plus other declarations). '
        'PPVC means a pre-poll voting centre.', '',
        'The first, no-results `sa2026-test2` export supplies relative weights within '
        'those groups. These finer weights are existing forecast assumptions, not '
        'observed historical SA declaration splits. They are held fixed throughout '
        'the replay. Current counts come from LiveV2’s exported booth/party account, '
        'checked against its district account and the matching ECSA XML detailed first-preference entries. '
        'XML district summaries can lag those entries and are diagnosed separately. '
        'The existing Narungga correction in the archived C++ loader is retained and '
        'identified; it changes the raw early row’s total by 17 votes. '
        'Embedded source timestamps, rather than archive capture filenames or simulation '
        'run dates, identify the observations.', '',
        'Reported ordinary booths, including PPVCs, are treated as approximately complete. '
        'Their counted votes receive no additions. Routine small rechecks are not given '
        'extra uncertainty here. Partial declarations provide lower bounds; their count '
        'does not by itself establish completion. A district is fixed only when its '
        'ECSA first-preference finalisation flag is true. The two-candidate flag is '
        'not used for that purpose.', '',
        'The three known cancelled Giles booths have no future return under their old '
        'identities. Their expected votes can move to other unfinished units within '
        'the district. Unexplained missing booths, including the Port Augusta centre '
        'listed in Stuart, retain an allocation. A positive count under a closed identity '
        'is preserved and flagged.', '',
        '## Updating a snapshot', '',
        'Each update starts again from the same pre-election outcomes and uses all counts '
        'in the current snapshot. It does not carry an earlier updated distribution '
        'forward. This avoids counting a cumulative return twice and allows official '
        'downward revisions.', '',
        'There is no downward district-total adjustment from reported-booth shortfalls '
        'in the broad prediction used by the initial comparisons. Lower totals in early-counted categories can be compensated '
        'by later categories. Where a prior total is below counted votes, a normal '
        'approximation in log odds maps the prior probabilities into the range above '
        'that count. This raises the distribution smoothly without collecting impossible '
        'outcomes at exactly zero remaining votes. Log odds means log(p / (1 − p)) '
        'for a share p; converting back keeps that share inside its parent’s bounds '
        'without capping it. Formal votes remain below enrolment.', '',
        'The bounded calculation applies each supported aggregate group’s counted '
        'minimum. Those groups divide the district remainder in their existing '
        'proportions. Within a mixed group, unfinished booths start from their own '
        'expected sizes; a smooth reservation leaves a positive declaration '
        'remainder. Declarations divide that remainder according to their '
        'count-conditioned estimates. Finer assumed booth sizes cannot override '
        'the supported aggregate early/postal evidence. Counted party records '
        'remain unchanged.', '',
        '## Does the district-total estimate improve?', '',
        'This compares the mean predicted final formal total with the reviewed final '
        'total in each district. **Mean absolute error** is the average absolute difference '
        'in votes over the same 47 districts. “Original” is the archived `sa2026-test2` '
        'projection; “Prototype” uses the coherent turnout prior and counted minimum. '
        'Booth-weight adaptation leaves these total estimates unchanged. “Actually '
        'remaining” is final votes minus votes counted statewide, and is scoring information only.', '']
    lines += table(['Source time', 'Votes counted', 'Actually remaining', 'Original error', 'Prototype error'], [
        [stamp(s['source_timestamp']), number(s['counted']), number(s['final_remaining']),
         number(s['legacy_total_mae']), number(s['total_score']['mean_absolute_error'])] for s in snapshots])
    last = snapshots[-1]
    lines += ['', 'At the latest tested snapshot, the mean district-total error changes '
        f'from {number(last["legacy_total_mae"])} to '
        f'{number(last["total_score"]["mean_absolute_error"])} votes. Much of this '
        'improvement is already present before any results arrive: it comes from '
        'replacing the original independent sizes with the calibrated turnout prior. '
        'Counted minimums cause a smaller subsequent adjustment. This comparison '
        'does not establish that turnout has been learned from reporting booths.']
    lines += ['', '## Are the total intervals useful?', '',
        'A nominal 95% interval contains the middle 95% of possible counts. **Coverage** '
        'is the percentage of the 47 final district totals that fall inside their intervals. '
        '**Width** is the average district interval width in votes. **Interval score** '
        'adds a penalty of 40 votes for each vote by which the final total misses the '
        'interval; lower scores favour useful precision without rewarding missed outcomes. '
        'These repeated snapshots are one election, not independent election tests. '
        'The archive does not provide a comparable original count interval.', '']
    lines += table(['Source time', 'Coverage %', 'Mean width', 'Mean interval score'], [
        [stamp(s['source_timestamp']), number(100*s['total_score']['coverage_95'], 1),
         number(s['total_score']['width_95']), number(s['total_score']['interval_score_95'])] for s in snapshots])
    lines += ['', '## Does adaptation improve the unfinished allocation?', '',
        'Ordinary and PPVC changes are estimated separately from complete booths with '
        'an exact previous-election name match, a same-seat match and an agreeing previous '
        'count. Each booth contributes one log count ratio against its frozen expected '
        'size. **Pooled** means that these observations are combined across eligible '
        'booths throughout the election to estimate one shared adjustment for each '
        'booth type, rather than a separate adjustment for each district or booth. '
        'A **multiplier** is that adjustment expressed as a factor. It is applied '
        'in log odds only to reliable unfinished non-EAV booths; 0.9 corresponds '
        'approximately to a 10% decrease for a small share. It never changes '
        'counted votes. In this SA grouping it changes finer allocations while '
        'preserving the supported aggregate group totals. The average is shrunk '
        'towards no change using ' + number(config['prior_equivalent_booths']) +
        ' equivalent unchanged booths, an assumed strength rather than fitted '
        'uncertainty.', '',
        'The retained 2022 SA source has no observed individual PPVC counts: LiveV2 '
        'reconstructs them from combined declaration votes. Those reconstructed values '
        'are excluded as empirical matching evidence. Consequently this SA replay '
        'has no eligible PPVC multiplier observations. '
        'Reported current PPVC counts are still treated as complete.', '',
        'The next table compares still-unreported current booths with their final counts, '
        'using matching district/name identities and booth types in the reviewed final '
        'source. This current-to-final match can score PPVC forecasts even though their '
        'previous counts cannot train a multiplier. All-zero final booth placeholders '
        'are omitted pending review; they remain open prediction units unless a closure '
        'was known. Errors are mean absolute differences in votes per booth. '
        '“Balanced” starts from the fixed pre-election sizes and counted minimums. '
        'It reserves plausible unfinished-booth amounts within the supported '
        'aggregate group, then assigns the rest to declarations. “Adapted” '
        'additionally applies the shared booth-type factor to reliable unfinished '
        'booths. Both preserve the same district and aggregate group totals here. '
        'A dash means no eligible booth remains.', '',
        'The shared updater also supports partial compensation among matched '
        'non-EAV PPVCs with genuine historical centre counts. It uses conservative '
        'prediction slopes from Federal 2025 research. The reconstructed SA PPVC '
        'baselines are not eligible, so this adjustment has no effect in this '
        'replay. Unmatched centre estimates are retained, rather than inventing '
        'historical sizes or borrowing the Federal relationship for them.', '']
    lines += table(['Source time', 'Booth type', 'Booths', 'Original error', 'Balanced error', 'Adapted error'], [
        [stamp(s['source_timestamp']), b['kind'], b['booths'], number(b['legacy']),
         number(b['unadjusted']), number(b['adjusted'])] for s in snapshots for b in s['booth_scores']])
    lines += ['', 'Category allocation is also checked using the three supported groups. '
        'The nine districts with reviewed missing category counts are omitted from this '
        'comparison, leaving 38 districts × 3 groups = 114 district-group counts. Their '
        'district totals remain in the preceding comparisons. Each group count has equal '
        'weight. Interval scores have the same definition as above. Booth adaptation '
        'changes only allocations within these groups, so both variants have the same '
        'group scores. Mean absolute error is the average absolute difference between '
        'the predicted and final group count. These broad-group results do not validate '
        'the assumed finer declaration splits or their uncertainty.', '']
    lines += table(['Source time', 'Mean absolute error', 'Coverage %', 'Mean interval score'], [
        [stamp(s['source_timestamp']), number(score['mean_absolute_error']),
         number(100*score['coverage_95'], 1), number(score['interval_score_95'])]
        for s in snapshots for score in [s['category_scores']['unadjusted']]])
    lines += ['', '## Could reporting order bias the multiplier?', '',
        'Small and large booths can report at different times. The following diagnostic '
        'splits eligible ordinary booths and PPVCs at the median expected size of each '
        'type. **Reported change** is the geometric mean count ratio for currently '
        'reported booths, expressed as a percentage change. **Final change** applies '
        'the same calculation to all matched final booths in that size group. The final '
        'column is a retrospective reference, never a prediction input. Differences '
        'suggest that the reporting subset does not yet represent the full group.', '']
    change = lambda x: '—' if x is None else number(100 * math.expm1(x), 1)
    lines += table(['Source time', 'Type', 'Size', 'Reported / eligible', 'Reported change %', 'Final change %'], [
        [stamp(s['source_timestamp']), r['kind'], r['size'], f"{r['reported']} / {r['eligible']}",
         change(r['mean_log_ratio']), change(r['final_mean_log_ratio'])] for s in snapshots for r in s['reporting']])
    maximum_error = max(s['diagnostics']['maximum_accounting_error'] for s in snapshots)
    maximum_time = max(s['update_seconds'] for s in snapshots)
    lines += ['', '## Accounting, timing and reproduction', '',
        f'All categories sum to their district total, with a maximum floating-point '
        f'difference of {maximum_error:.2g} votes. Reported complete booths and counted '
        'party records are preserved; open allocation units receive positive additions. '
        'Rare substantial official corrections are diagnosed by the replay and are '
        'not used to broaden the ordinary-booth completion assumption.', '',
        f'This run took {result["elapsed_seconds"]:.2f} seconds for {len(snapshots)} snapshots. '
        f'The slowest pair of balanced/adapted count updates took {maximum_time:.3f} seconds '
        'for all 47 districts and their booths. This measures the Python count prototype '
        'only; it excludes party-share preparation and full simulation iterations. '
        'It does not demonstrate the complete application’s timing.', '',
        'From the repository’s `analysis` directory:', '',
        '```powershell',
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_live_prototype --dry-run',
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_live_prototype',
        '```', '',
        'The default uses `live_runs/sa2026-test2`, source XML archives in Downloads, '
        'the saved `docs/turnout-prior-prototype/fixtures-v3.json`, and reviewed final '
        'evidence in `analysis/Data/Turnout/2026sa.json`. Override `--archive`, `--sources`, '
        '`--fixture` or `--final-counts` for another retained location. The matching '
        'reviewed raw final source must also be retained under `downloads/turnout/2026sa`. '
        'These large local inputs are not distributed in Git.', '',
        'The default selects eight count-progression checkpoints through 27 March, '
        'using the closest available source update to each target time. '
        '`--all-snapshots` replays every retained update within `--through`. '
        '`--prior-booths` changes the explicit shrinkage assumption. '
        'The separate `Replay-Sa2026LiveSnapshot.ps1 -List` command lists available '
        'raw archives without installing a feed or changing replay state.', '',
        'The generated local `analysis.json` records source timestamps, archive and '
        'final-source SHA256 hashes, the frozen prior version, current counts, estimated '
        'additions, shrinkage settings and scores. Normalized input hashes ignore JSON '
        'formatting changes. A changed source input or sampler requires a refreshed '
        'prior fixture; final scoring follows the exact retained revision named by '
        'the normalized source. The public Markdown report is separate from this local data export.', '']
    return '\n'.join(lines)
