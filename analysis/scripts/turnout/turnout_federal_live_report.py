"""Explain the Federal 2025 count replay; calculations live in its analysis script."""

import math


def number(value, digits=0):
    return '—' if value is None else f'{value:,.{digits}f}'


def table(headers, rows):
    return ['| ' + ' | '.join(headers) + ' |', '| ' + ' | '.join(['---'] * len(headers)) + ' |',
            *['| ' + ' | '.join(map(str, row)) + ' |' for row in rows]]


def starting_sizes_description(result):
    """Explain how unobserved centre sizes divide a fixed early-vote estimate."""
    statistic = result['config'].get('ppvc_starting_sizes', 'legacy')
    if statistic == 'legacy':
        return ['Starting booth weights use the no-results count baseline. '
                'New or changed PPVCs retain its existing previous count or generic fallback.', '']
    calibration = result['ppvc_starting_size_calibration']
    lines = ['### Starting sizes for new or changed pre-poll centres', '',
        'This allocation prevents a new public centre from receiving a tiny '
        'fallback while existing centres inherit almost all the district’s '
        'expected early vote. It changes the shares assigned to centres within '
        'the existing category expectation; it does not increase that expectation.', '',
        'The historical reference is 2022 centres without an exact positive-count '
        'same-district, same-name and same-type match in the 2019 feed. EAV, '
        'Divisional Office and BLV services are excluded. A location screen '
        'retains centres whose nearest ordinary booth belongs to the home '
        'electorate. The ' + number(calibration['centres']) + ' retained centres '
        'have a median of ' + number(calibration['median']) + ' formal votes and '
        'a mean of ' + number(calibration['mean']) + '.', '',
        'For 2025, coordinates come from the election-day preload. A centre is '
        '**likely local** when its nearest ordinary booth belongs to its '
        'electorate, **likely outside** when none of the nearest three does, '
        'and **uncertain** otherwise. Missing coordinates remain unknown. '
        'These labels are a practical location screen, not a boundary lookup. '
        'Ordinary venues listed for multiple electorates at the same coordinates '
        'are excluded from the geographic reference, because their assignments '
        'do not establish which electorate contains the site. '
        'An outside city service such as Melbourne CALWELL PPVC does not train '
        'the local historical estimate or receive the large local fallback.', '',
        'Unreliably matched public PPVCs screened as local use the historical '
        + statistic.split('_')[0] + (' for their seat type' if statistic.endswith('_by_type') else ' across all seat types')
        + ' as their starting weight. Other centres retain their '
        'previous count or existing fallback, and EAV retains its separate '
        'treatment. Weights are normalised within each category. The new size '
        'assumption does not turn a changed centre into a reliable observation '
        'for learning trends or compensation. No 2025 final count chooses these weights.', '']
    if statistic.endswith('_by_type'):
        lines += ['The project’s existing seat classifications separate city electorates '
            '(inner and outer metropolitan), regional urban electorates (provincial), '
            'and rural electorates. This checks whether a single national size '
            'assumption overestimates small rural services. Each row describes '
            'the retained 2022 new/changed local centres, with formal votes per '
            'centre. Districts count distinct electorates in that row. An '
            'unclassified seat uses the national reference.', '']
        lines += table(['Seat type', 'Districts', 'Centres', 'Mean votes', 'Median votes'], [
            [r['name'], r['districts'], r['centres'], number(r['mean']), number(r['median'])]
            for r in calibration['by_seat_type'].values()])
        lines += ['']
    return lines


def reporting_delay_description(result):
    """Explain observed reporting delays separately from the experimental response."""
    history = result.get('ppvc_reporting_history')
    enabled = result['config'].get('ppvc_reporting_decay', False)
    if not history and not enabled:
        return []
    lines = ['## What does a still-unreported centre imply as counting progresses?', '',
        'A centre still without a formal return several days after polling may '
        'contribute fewer future votes than its starting expectation. This analysis '
        'checks that possibility before reducing its expected size: a large centre '
        'can also simply report late. Public PPVCs are considered separately from '
        'EAV services, which are excluded here.', '']
    if history:
        lines += ['The historical checks use Federal 2019 and 2022. At each saved update, '
            '**Unreported centres** counts public PPVC identities present with no '
            'formal votes. **Later positive returns** counts those that eventually '
            'report positive formal votes under that same identity. **Total ratio** '
            'is their combined eventual formal feed count divided by the combined '
            'starting expectation of all currently unreported centres. **Mean centre '
            'ratio** averages each centre’s eventual count divided by its own '
            'expectation, giving small and large centres equal weight. Both ratios '
            'are shown as percentages; 100% means the starting expectation was met.', '',
            'Starting expectations use the preceding election’s count, or an '
            'older local new/changed-centre median for that seat type. They do not '
            'use the current election’s final sizes, turnout prior or pooled '
            'adjustment. A record still empty in the final feed contributes no '
            'observed later return to that identity; its actual venue count remains '
            'unknown. This distinction is essential: the analysis measures later '
            'feed additions, rather than claiming that the venue received no votes. '
            'Final CSV counts reconcile with retained final verbose feeds.', '',
            'The following selected observations retain their actual source times; '
            'they do not establish exactly when each centre first reported. '
            'Coordinates come from retrospective final polling-place records, '
            'so the geographic screen is not a reconstruction of historical '
            'pre-election boundaries. Five unresolved 2022 final identities are '
            'excluded; none are excluded for that reason in 2019.', '']
        lines += table(['Election', 'AEC source time', 'Hours after eastern poll close',
                        'Unreported centres', 'Later positive returns', 'Total ratio %', 'Mean centre ratio %'], [
            [e['election'], s['source_timestamp'].replace('T', ' '), number(s['hours_after_eastern_poll_close'], 1),
             r['centres'], r['later_returns'], number(100*r['feed_total_relative_to_expected'], 1),
             number(100*r['mean_feed_ratio'], 1)]
            for e in history['elections'] for s in e['snapshots']
            if 17 <= s['hours_after_eastern_poll_close'] <= 105
            for r in [s['all_public']]])
        lines += ['', 'The elections differ substantially. In 2022, the Monday '
            'afternoon unreported group still subsequently returned roughly '
            'three quarters of its starting expectation, before falling sharply '
            'that evening. In 2019 the decline occurred much earlier. Two '
            'elections support a cautious delay rule, but do not establish a '
            'universal schedule or a calibrated probability distribution.', '']
    if enabled:
        lines += ['This replay retains full expected size through 48 hours after '
            '18:00 on Saturday, then uses the factors below. Between these times '
            'the factor changes smoothly on a logarithmic scale; after 102 hours '
            'it stays at 0.02. These are conservative judgement choices informed '
            'by the historical ratios, rather than fitted parameters. The clock '
            'uses the embedded feed time and one eastern polling-close reference '
            'for the whole election.', '',
            'The factor multiplies the odds of a centre’s expected formal votes '
            'as a share of enrolment. For a small share this is close to '
            'multiplying its count. District accounting then reconciles the '
            'result with other unfinished categories. Positive counted votes '
            'always replace this estimate; EAV receives no direct time adjustment. '
            'The positive final factor retains some allowance for a later return '
            'without declaring a centre closed. This simple response does not '
            'separately model the chance of a return and its size when it arrives.', '']
        lines += table(['Hours after eastern poll close', 'Approximate time', 'Odds factor'], [
            [h, label, f] for (h, f), label in zip(result['ppvc_reporting_decay_knots'],
                ['Monday 18:00', 'Tuesday 00:00', 'Wednesday 00:00', 'Thursday 00:00'])])
        lines += ['']
    return lines


def render(result):
    """Put definitions and the question answered before each comparison table."""
    snapshots, config = result['snapshots'], result['config']
    replay_options = (' --ppvc-starting-sizes ' + config.get('ppvc_starting_sizes', 'legacy')
                      + ' --pooled-half-votes ' + str(int(config['pooled_half_votes']))
                      + ' --compensation-order ' + config['compensation_order'])
    if config.get('ppvc_reporting_decay'):
        replay_options += ' --ppvc-reporting-decay'
    if config.get('reporting_history'):
        replay_options += ' --reporting-history "' + config['reporting_history'] + '"'
    stamp = lambda s: s.replace('T', ' ')
    lines = ['# Federal 2025 live turnout count prototype', '',
        'This report examines whether using a shared estimate of the eventual vote '
        'total improves live estimates of votes still to be counted. It compares '
        'the existing booth/category size rules with the turnout prototype at '
        'successive retained Federal 2025 result updates. It measures formal vote '
        'counts; it does not calculate party shares, winners or seat probabilities. '
        'The initial comparisons use the broad turnout prediction. The counting-progress '
        'comparison, when included below, additionally varies declaration additions and can '
        'reduce the district total as counting slows.', '',
        '## Inputs and comparison methods', '',
        'The AEC polling-place preload supplies current district, booth ID, name and '
        'type. The retained final 2022 House feed supplies actual previous counts. '
        'Current first-preference counts come directly from the 2025 detailed Light '
        'ZIP archives. Historical “Ghost” candidates, two-candidate counts and Senate '
        'results are excluded. Candidate totals reconcile with formal summaries; '
        'booth totals reconcile with district ordinary counts. A numeric zero is '
        'retained as reported; a missing measurement is not replaced with zero.', '',
        '**Count baseline** is a Python reproduction of the deterministic size rules '
        'in `LiveV2.cpp`. Reported ordinary booths, including ordinary pre-poll '
        'voting centres (PPVCs), use their counted totals. Unreported ordinary booths '
        'use previous counts; a missing or zero previous count uses the existing '
        '500-vote fallback, or 50 for a hospital booth. Unreported PPVCs use previous '
        'counts or 500, multiplied by the existing national PPVC size factor. '
        'That factor is (2,000 + current votes at matched reported PPVCs) / '
        '(2,000 + their previous votes). Each declaration category uses '
        'max(current count, integer part of 1.05 × max(previous count, 30)), '
        'using the same 32-bit arithmetic as the C++ calculation. '
        'The reference deliberately retains these existing fallback and bound rules.', '',
        'This reference does not run the GUI, prepare party-share distributions or '
        'include the simulator’s random declaration-size variation. It is a '
        'central count-rule comparison, not an export from a full simulation. '
        'Divisional Office booths and those identified by `BLV` in their official '
        'names retain LiveV2’s Other treatment and do not '
        'train or receive its PPVC multiplier, while remaining in their official '
        'vote category.', '',
        f'{number(result["initial_unknown_booth_counts"])} current booths have no accepted '
        'previous-ID match under the existing rules. Their previous measurements '
        'remain unknown in the export; the baseline fallback is an estimate, not '
        'an invented observed zero. Previous district declaration counts cannot be '
        'matched by name for: ' + ', '.join(result['missing_previous_seats']) + '. '
        'The analysis makes no inferred predecessor assignment. Explicit project '
        'previous-seat aliases can be supplied when known.', '',
        '**Prototype** uses the saved pre-election distribution for Federal 2025, '
        'trained on earlier elections. It combines previous turnout and formality, '
        'current enrolment, district postal-application counts and an aggregate '
        'early-vote estimate. Federal district early-vote measurements are not used '
        'as direct district targets because counting-location and elector-district '
        'totals differ. Final counts do not enter the prior or current counted account. This is one earlier-only '
        'training example, not a comparison establishing that this training choice '
        'is superior to leaving out each test election in turn.', '',
        'The operational inputs are saved final pre-election records, including '
        'later source reconciliation where present. This replay tests those '
        'fixed estimates against the live count progression; it does not '
        'reconstruct every version of an operational source available at the '
        'time of the original election.', '',
        'The fitted parameters and operational measurements remain frozen, but '
        'the replay uses the enrolment recorded in the no-results election-day '
        'feed. This differs from the fixture’s later final published roll by '
        f'{number(sum(r["election_day"] - r["fixture"] for r in result["enrolment_revisions"]))} '
        'electors nationally. Individual differences are retained in the export. '
        'The later roll is not substituted into prediction inputs.', '',
        'The six groups are **ordinary** (ordinary votes outside the pre-poll '
        'group, including mobile services), **ordinary pre-poll**, **declaration '
        'pre-poll**, **absent**, **provisional** and **postal**. Provisional votes '
        'use the fixture’s `other` key. Ordinary pre-poll and declaration pre-poll '
        'are separate parts of the combined early-vote estimate. All six sum to '
        'one district formal total. Previous counts and explicit starting-size '
        'assumptions supply fixed relative booth weights within each group; '
        'final booth sizes never supply those weights.', '',
        *starting_sizes_description(result),
        *reporting_delay_description(result),
        '## What does balancing do?', '',
        '**Coherent allocation** means that the count estimates agree with each '
        'other: counted votes are preserved, additions are nonnegative, booths and '
        'declarations sum to their groups, and groups sum to one district total. '
        'For example, a district expected to finish with 90,000 votes after '
        '80,000 are counted must allocate exactly 10,000 additions among its '
        'unfinished units. Accounting consistency does not establish accuracy.', '',
        'Each update starts again from the same pre-election outcomes and cumulative '
        'counts. Reported ordinary/PPVC booths are approximately complete; declaration '
        'counts are minimums. A bounded calculation in log odds, log(p / (1 − p)), '
        'moves impossible low outcomes above counted minimums. Unfinished booths '
        'start from their own expected sizes. Their combined amount is reserved '
        'smoothly within the district remainder. Declaration estimates divide '
        'what remains. Categories are sums of their units, rather than targets '
        'whose shortfalls must enter the last unreported booth.', '',
        'The broad district-total rule in the initial comparisons does not lower the total '
        'because reported ordinary or PPVC counts fall below expectations. '
        'Declarations consequently absorb differences after the booth reservation. '
        'This is an untested assumption about compensation between vote categories. '
        'The early/postal evidence supplies starting estimates, rather than '
        'immutable category totals after counting begins.', '',
        'Partial compensation applies only to unfinished, reliably matched '
        'non-EAV PPVCs. If other reliable completed centres in the same district '
        'run below expectations, an eligible unfinished centre can increase, '
        'and vice versa. The deviation is attenuated by the share of the other '
        'expected PPVC vote represented by those completed centres. Unmatched '
        'centres, EAV services and centres expected below 2,000 votes receive no '
        'such adjustment. They retain their starting size estimates, subject '
        'to the common district accounting constraint.', '',
        'The coefficients are 0.14 for expected sizes 2,000–<4,000, 1.17 for '
        '4,000–<8,000, and 0.63 for 8,000 or more. These are prediction slopes '
        'in transformed shares of enrolment, not correlations or fractions of '
        'a vote-count deficit. They use the conservative end nearest zero of '
        '95% district-resampling intervals from reliable Federal 2025 matches, '
        'rounded towards zero. This election’s final results choose the fixed '
        'research coefficients: the replay is therefore an exploratory same-election '
        'check, not independent validation of this adjustment. Separate uncertainty '
        'in individual booth allocations has not yet been calibrated.', '',
        'The Federal 2025 Rockingham PPVC and Rockingham Central PPVC identities '
        'are excluded from learning live booth-size trends because their returns '
        'were consolidated. Brand is also excluded from the paired compensation '
        'calibration: each centre’s comparison includes the other centres in '
        'that district. This exception does not delete reported votes, close '
        'either identity, or exclude Brand’s district total from scoring. It '
        'does not apply to earlier elections’ Rockingham records.', '',
        '**Balanced** uses these size estimates and partial compensation. '
        '**Adapted** additionally learns one ordinary adjustment and one PPVC '
        'adjustment from reliable completed booths across the election. '
        '**Pooled** means the observations are combined rather than estimating '
        'a factor per district. Each observation is a log counted/expected ratio. '
        'Ordinary adjustment retains ' + number(config['prior_equivalent_booths']) +
        ' equivalent unchanged booths. PPVC adjustment instead uses the observed '
        'vote count V: influence is V^0.9 / (H^0.9 + V^0.9), with H = ' + number(config['pooled_half_votes']) +
        ' votes. H is the count at which half the raw deviation is applied; '
        'this is a cautious experimental strength rather than fitted precision. '
        'The average log ratio is multiplied by this influence before producing '
        'a factor. Only reliable non-EAV unfinished booths '
        'receive it, applied in log odds. A factor of 0.9 corresponds approximately '
        'to a 10% reduction for a small share. Counted votes and district totals '
        'remain unchanged; category totals can change.', '',
        'The main Prototype columns use Balanced. Adapted is a diagnostic '
        'comparison: a reporting subset can give a misleading shared factor, '
        'so the extra adjustment is not part of the main prototype estimate.', '',
        ('In the diagnostic version, compensation is measured against reference '
         'sizes already adjusted for the pooled trend. This keeps a common '
         'election-wide decline separate from the remaining district deviation.'
         if config['compensation_order'] == 'after_pool' else
         'In the diagnostic version, compensation is measured against original '
         'reference sizes before adding the pooled trend.'), '',
        'EAV is identified by its service role, independently of size. It neither '
        'trains nor receives public-centre trend or compensation adjustments. '
        'An unmatched EAV overrides the generic centre fallback. Its starting '
        'count is the median expectation among reliably matched historical EAV '
        'services, calculated separately in each prior outcome. With no historical '
        'EAV reference, 25 votes is an explicit service assumption. This uses '
        'historical prediction inputs, not this election’s final EAV counts. '
        'An already reported EAV always retains its actual count.', '',
        'The Light feed has no authoritative first-preference finalisation flag '
        'used here. Declared-winner status and dates do not establish completion. '
        'Rare substantial booth revisions are exported separately. Matching '
        'identities does not establish comparability after boundary changes.', '',
        '## Does the district-total estimate improve?', '',
        'The following table compares predicted final formal totals with the '
        'reviewed final totals over all 150 districts. **Mean absolute error** '
        'is the average absolute difference in votes per district, using mean '
        'prototype predictions. **Actually remaining** is final votes minus '
        'votes counted nationally; it is retrospective scoring information. '
        'All rows use the same districts, so changes can be compared directly.', '']
    lines += table(['AEC source time', 'Votes counted', 'Actually remaining', 'Baseline error', 'Prototype error'], [
        [stamp(s['source_timestamp']), number(s['counted']), number(s['final_remaining']),
         number(s['baseline_total_mae']), number(s['total_score']['mean_absolute_error'])] for s in snapshots])
    lines += ['', 'Actually remaining can be negative when a retained snapshot '
        'contains more votes than the subsequently revised final result. Counted '
        'votes are preserved in that snapshot; the prototype does not borrow '
        'a later downward correction to improve its score.']
    first_matched, last_matched = (s['previous_district_comparison'] for s in (snapshots[0], snapshots[-1]))
    lines += ['', 'The missing previous district is not the main source of the '
        'difference. Restricting both methods to the same '
        f'{first_matched["districts"]} districts with previous declaration counts '
        'changes the no-results comparison to '
        f'{number(first_matched["baseline_mae"])} versus {number(first_matched["prototype_mae"])} '
        'votes of mean absolute error, and the latest comparison to '
        f'{number(last_matched["baseline_mae"])} versus {number(last_matched["prototype_mae"])}. '
        'Much of the district-total improvement exists before results arrive: '
        'it comes from the calibrated pre-election estimate, rather than learning '
        'a turnout change from reporting booths.']
    lines += ['', '## Are the total and category intervals useful?', '',
        'Intervals should be narrow enough to inform a forecast while still '
        'covering the final counts. A nominal 95% interval contains the middle '
        '95% of modelled outcomes. **Coverage** is the percentage of final '
        'counts inside their intervals. **Width** is their average width in '
        'votes. **Interval score** is width plus 40 times the distance of a '
        'missed final count outside the interval; lower is better. The baseline '
        'has no corresponding count interval in this analysis.', '',
        'District rows give equal weight to 150 final totals. Category rows give '
        'equal weight to 150 districts × 6 groups = 900 final counts. Category '
        'errors are therefore in votes per district-group, not percentage points '
        'or percentages of the national total. Repeated snapshots remain a '
        'single election and cannot establish general probability calibration. '
        'The extra pooled-adjustment error column checks whether the diagnostic '
        'Adapted version improves category counts on these same cases.', '']
    lines += table(['Quantity', 'AEC source time', 'Baseline error', 'Prototype error', 'Pooled-adjustment error', 'Coverage %', 'Mean width', 'Interval score'], [
        [label, stamp(s['source_timestamp']), number(s[baseline_key]), number(score['mean_absolute_error']),
         number(s['adapted_category_score']['mean_absolute_error']) if score_key == 'category_score' else '—',
         number(100*score['coverage_95'], 1), number(score['width_95']), number(score['interval_score_95'])]
        for label, baseline_key, score_key in [('District total', 'baseline_total_mae', 'total_score'),
                                               ('Category count', 'baseline_category_mae', 'category_score')]
        for s in snapshots for score in [s[score_key]]])
    lines += ['', '## Are still-unreported booth estimates better?', '',
        'This isolates unfinished ordinary booths and PPVCs, comparing their '
        'predicted final counts with final AEC booth counts. Each booth has equal '
        'weight. District, booth ID, name and official category must match. '
        'Reported booths are excluded because the completion assumption gives '
        'them their counted total. The reviewed Rockingham consolidation '
        'identities are excluded because their individual final counts are not '
        'comparable. Entirely empty final records are excluded '
        'pending assessment, without closing their prediction identities. '
        f'There are {number(len(result["empty_final_booth_placeholders"]))} such final '
        'records in the retained CSVs. A dash means no eligible unreported '
        'booth remains. “ordinary” in this booth-type table includes mobile '
        'services and Divisional Office/BLV booths that do not receive the '
        'PPVC size rule. It is not the six-group category label. All errors '
        'are mean absolute differences in votes per booth.', '']
    lines += table(['AEC source time', 'Type', 'Booths', 'Baseline error', 'Balanced error', 'Adapted error'], [
        [stamp(s['source_timestamp']), b['kind'], b['booths'], number(b['baseline']),
         number(b['balanced']), number(b['adapted'])] for s in snapshots for b in s['booth_scores']])
    lines += ['', 'At the latest snapshot, open declaration categories still '
        'retain additions. District-total 95% coverage is '
        f'{number(100*snapshots[-1]["total_score"]["coverage_95"], 1)}%. '
        'Subsequent downward corrections can put final counts below the preserved '
        'counted minimum, but they do not explain all expected additions. These '
        'intervals retain pre-election uncertainty and counted bounds; they are '
        'not a calibrated model of declaration-counting completion.']
    lines += ['', '## Could reporting order bias the shared factor?', '',
        'Small and large booths can report at different times. Eligible booths '
        'are split at their type’s median frozen expected size. **Reported '
        'change** is the geometric mean counted/expected ratio among currently '
        'reported booths, expressed as a percentage change before shrinkage. '
        '**Final change** uses all positive, matching final counts in that size '
        'group and is never used for prediction. Differences expose a reporting '
        'subset that does not yet represent the full group.', '']
    change = lambda x: '—' if x is None else number(100*math.expm1(x), 1)
    lines += table(['AEC source time', 'Type', 'Size', 'Reported / eligible', 'Reported change %', 'Final change %'], [
        [stamp(s['source_timestamp']), r['kind'], r['size'], f"{r['reported']} / {r['eligible']}",
         change(r['mean_log_ratio']), change(r['final_mean_log_ratio'])] for s in snapshots for r in s['reporting']])
    lines += ['', '## Accounting, timing and reproduction', '',
        'All counted candidate records are retained in the local export under '
        'their AEC candidate IDs. The prototype produces additional counts only; '
        'it does not invent projected party records. The largest category-to-total '
        'accounting difference was '
        f'{max(s["diagnostics"]["maximum_accounting_error"] for s in snapshots):.2g} votes. '
        'Log-odds marginal conditioning followed by proportional reconciliation '
        'is an approximation, not an exact joint posterior. Updated turnout and '
        'formality rates, party-share sensitivities and new joint uncertainty '
        'calibration are not produced by this component.', '',
        f'The calculation took {result["elapsed_seconds"]:.2f} seconds for '
        f'{len(snapshots)} selected snapshots with {number(config["samples"])} '
        'prior outcomes each, across all 150 districts. The slowest pair of '
        'Balanced/Adapted count updates took '
        f'{max(s["update_seconds"] for s in snapshots):.3f} seconds. '
        'These Python count timings exclude full party-share preparation and '
        'simulation iterations; they do not demonstrate full application timing.', '',
        'From the repository’s `analysis` directory:', '', '```powershell',
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_federal_live --baseline-only',
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_federal_live --dry-run' + replay_options,
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_federal_live' + replay_options,
        '```', '',
        '`--baseline-only` writes `baseline.json` and needs no prior fixture or '
        'final scoring inputs. The full comparison writes `analysis.json` and '
        'this report. `--dry-run` writes nothing. Default snapshots come from '
        'Downloads and run through 22 May 2025, selecting eight points from '
        'no results through late counting. The previous verbose result XML '
        'and current polling-place preload XML are retained in `downloads`. '
        'Use `--sources`, `--previous` and `--preload` for explicit locations. '
        '`--through` selects a different final date. These large local inputs '
        'are not distributed in Git.', '',
        '`--ppvc-starting-sizes legacy` reproduces the original allocation '
        'weights; `mean` and `median` select the historical local-centre '
        'assumption across all seat types. `mean_by_type` and `median_by_type` '
        'use separate seat-type references. `--ppvc-reporting-decay` enables '
        'the delayed-public-centre experiment; it is off by default. The '
        'default starting weights remain `legacy`. The commands above reproduce the '
        'settings used in this report. These alternatives require the '
        'retained 2019 verbose result feed and the 2022 polling-place CSV selected '
        'by `downloads/turnout/federal-prepoll/2022fed/latest.json`. '
        '`--pooled-half-votes` selects the diagnostic PPVC influence scale. '
        'For a controlled PPVC-only comparison, run '
        '`scripts.turnout.turnout_pooled_size_experiment` with '
        '`--ppvc-starting-sizes median --samples 1024`; its export contains '
        'formula, scale and compensation-order alternatives with ordinary '
        'adjustment held off throughout.', '',
        'Historical reporting diagnostics can be reproduced with '
        '`scripts.turnout.turnout_ppvc_reporting --fetch`. This selects a small '
        'set of checkpoints from the [AEC media-feed archive](https://results.aec.gov.au/) '
        'and retains downloaded bytes by SHA256. Later runs reuse them; '
        '`--refresh` retrieves revisions without deleting older source bytes. '
        'The script also uses retained preloads and final feeds under '
        '`downloads/turnout/feed-archive` (or `POLLING_ANALYSER_FEED_ARCHIVE`) and `downloads`, plus earlier '
        'official CSV revisions selected by `downloads/turnout/federal-prepoll`. '
        'Use `--archive` for a different local archive location. '
        '`--reporting-history` includes the resulting JSON in this report only, '
        'after checking its source hashes; it is not a prediction input.', '',
        'The frozen fixture is `docs/turnout-prior-prototype/fixtures-v3.json`. '
        'Final scoring uses `analysis/Data/Turnout/2025fed.json`, the audited '
        '`FederalPrepoll/final.json` supplement and byte-addressed CSV downloads '
        'under `downloads/turnout/federal-prepoll/2025fed`. The export records '
        'embedded AEC timestamps, SHA256 hashes of input revisions and code, '
        'model versions, samples, seed and shrinkage strength. Changes to prior '
        'source measurements or its sampler require a refreshed fixture. '
        'Final scoring revisions must reconcile with the saved district and '
        'category results.', '',
        'If GUI project settings give a district a different previous name or '
        'a `useFpResults` source, supply `--seat-aliases` with a JSON object '
        'mapping the current name to the relevant project fields. For '
        'example, `{"Current name": {"previousName": "Previous name"}}`. '
        'A `useFpResults` value supplies declaration counts but does not make '
        'booths from that district eligible for same-district adaptation. Matching district '
        'settings are needed to reproduce a particular GUI project’s baseline '
        'exactly; this run uses current district names only.', '']
    return '\n'.join(lines)
