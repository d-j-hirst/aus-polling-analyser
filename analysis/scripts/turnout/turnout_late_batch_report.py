"""Explain the implemented late-count comparison without doing its analysis."""


HEADING = '## How does counting progress change the remaining-vote estimate?'


def displayed_snapshots(result):
    """Keep election-night references beside relevant late-count checkpoints."""
    rows = result['snapshots']
    dates = ({'2026-03-27','2026-03-29','2026-03-30','2026-03-31','2026-04-01','2026-04-02'}
             if result['election'] == '2026sa' else
             {'2025-05-11','2025-05-14','2025-05-17','2025-05-18','2025-05-19',
              '2025-05-20','2025-05-22','2025-05-23','2025-05-31'})
    last_on_date = {r['source_timestamp'][:10]:r for r in rows}
    selected = [rows[0],rows[min(5,len(rows)-1)],*(last_on_date[d] for d in sorted(dates) if d in last_on_date),rows[-1]]
    return list({r['source_timestamp']:r for r in sorted(selected,key=lambda r:r['source_timestamp'])}.values())


def render(result):
    """Describe the prediction inputs, paired scores and computational limits."""
    config = result['config']
    lines = [HEADING, '',
        'This analysis examines whether observed slowing of declaration counting improves estimates of votes still to arrive. '
        'It compares the broad prediction described above with an additional layer that gradually concentrates predictions '
        'near small additions while retaining a rare larger-batch possibility. Final results measure errors; they do not '
        'define completion evidence. The SA comparison additionally uses explicit retrospective category repairs, '
        'described below. Both predictions start from the Balanced allocation, without a shared ordinary '
        'or PPVC trend multiplier. Federal starting sizes and the existing unreported-PPVC time adjustment are the '
        'same on both sides of this comparison.', '',
        *(['Before applying the late-batch mixture, the updated prediction also relaxes excessive declaration allowances '
           'as counting in the entire historical group settles. A historical group can combine ordinary booths, PPVCs '
           'and declarations where earlier results did not support finer divisions. Each declaration retains its own '
           'count-conditioned estimate before it was rescaled to meet the group and district allowances. The update '
           'moves towards that estimate smoothly, favouring reductions and giving less influence to increases based '
           'only on a larger individual prior. This changes the expected remainder rather than declaring the category '
           'complete. The allocation progress and directional response scales are both 0.5; these remain experimental '
           'assumptions. Postal uses its same receipt-window discount, and no group evidence leaves the original '
           'allocation unchanged.', ''] if result.get('allocation_model') else []),
        'Each prediction starts from the same frozen prior and current counted account. Recent absolute count changes '
        'include corrections and reversals, and their influence decays continuously with age. The amount of movement and '
        'how widely it occurs provide separate evidence. Missing measurements supply no evidence; long observation gaps '
        'reduce confidence. Known zero counts are included when assessing whether reporting has started. Federal state '
        'measurements blend with national measurements according to their available district count: a state with '
        '20 measured districts receives equal state and national influence. For an unstarted local category, this '
        'layer supplies no category-specific slowing-count evidence. Evidence from the rest of the district can '
        'nevertheless support a shared possibility of no further votes.', '',
        'Shared measurements describe typical district progress rather than every exceptional source record. '
        'Before averaging each measurement, the model removes one observation at each end per ten measured districts, '
        'up to two per end. Pools of fewer than ten districts retain every observation. Activity, reporting-start '
        'support and relative movement are trimmed separately. Relative movement is the recent age-weighted absolute '
        'count change divided by that district\'s current category count plus half a vote; its shared average gives '
        'districts equal weight. Local counts and local activity are never trimmed. This limits errors in a few records '
        'from determining expectations across an election, without recognising or correcting their cause.', '',
        '**Evidence strength** is a score between zero and one describing support for slowing counts. It combines '
        'recent observation coverage, local activity, activity across districts and whether reporting has started. '
        'It is not a probability that counting is complete. Activity responds smoothly to the size of each change; '
        'a ten-vote scale combined with 0.1% of the current count sets its scale rather than a pass/fail tolerance. Its memory fades '
        'exponentially. Regular observations build support, while a long gap supplies less support than daily returns.', '',
        'The evidence changes a mixture of three distributions: the broad previous-expectations prediction, very small '
        'additions, and a wider late batch. Its weights vary continuously; there is no cutoff on either the evidence '
        'score or elapsed time. Both added distributions have overlapping continuous support. Counted votes remain fixed, and a '
        'reported ordinary/pre-poll booth remains complete. Within the branch with further votes, unfinished booth estimates '
        'are held at their broad values. '
        'Declarations and unused electorate capacity share a bounded transform, so reducing declarations can reduce '
        'the formal total instead of forcing all those votes into another category.', '',
        'Here, **unused capacity** means enrolment minus the broad predicted formal total; it includes informal votes '
        'and people who do not vote. For each preparation outcome, declaration additions are expressed relative to '
        'that capacity in log odds. Log odds means log(p / (1 − p)) for a share p. The small-addition and late-batch '
        'components vary on that scale, and their joint allocation stays inside the available pool. There is no '
        'cap or accumulated probability at the enrolment bound. Positive additions are not rounded down to zero.', '',
        '**No further votes** is a separate, shared outcome for the whole district. It keeps every current count exactly '
        'unchanged, including in tiny or unstarted categories. Its probability grows continuously with count-weighted '
        'slowing evidence from started declaration categories. The broad expected remainder weakens that probability: '
        'multiply by the square of counted declaration votes divided by counted declaration votes plus the broad '
        'expected remainder. Unreported booths therefore also weaken it. A supplied postal receipt window discounts '
        'the whole-district probability as well as postal-specific slowing evidence.', '',
        f"The current exploratory maximum fraction is {100*config['zero_addition_fraction']:.0f}%, further discounted by "
        'the product of each category\'s probability of no exceptional batch. Probability is transferred from the broad '
        'and small-addition branches; each category\'s original unconditional late-batch probability is preserved. '
        'These settings have not been fitted as completion probabilities. Explicit authoritative finalisation still '
        'makes no further votes certain. Ordinary evidence never permanently closes a category.', '',
        'The exact counted outcome is retained analytically alongside samples of the branch with further votes. '
        'Means and intervals include both branches: for example, a 60% probability of no further votes and a '
        'conditional mean of 100 additional votes gives an overall mean of 40. This avoids changes driven by '
        'whether a small preparation sample happened to select a zero outcome, and makes the branch available '
        'for separate party-composition evaluation.', '',
        '**These are exploratory shared parameters, rather than fitted district parameters.** The late-batch probability '
        f"within the slowing-count distribution is {100*config['batch_probability']:.0f}%; the broad distribution remains "
        'an additional source of substantial additions until the evidence weakens its weight. The near-zero component '
        f"starts from a {config['small_median_votes']} vote equivalent. Counting-activity memory is "
        f"{config['activity_decay_hours']:.0f} hours. These assumptions have not been established as optimal or calibrated "
        'for every jurisdiction. The conditional batch starts from the broad remaining estimate plus '
        f"{100*config['batch_current_fraction']:.0f}% of the category's counted votes, with the small count equivalent "
        'also included. Its uncertainty is deliberately much wider than that of small additions.', '',
        '| Setting | Value | Purpose |', '|---|---:|---|',
        f"| Activity memory | {config['activity_decay_hours']:g} hours | Old count changes gradually stop arguing against finalisation. |",
        f"| Observation coverage memory | {config['coverage_hours']:g} hours | Discount stale evidence and gaps between available sources. |",
        f"| Shared activity scale | {config['activity_scale']:g} | The shared quietness factor halves at this mean activity strength. |",
        f"| Shared movement scale | {100*config['magnitude_scale']:g}% | The movement factor halves when the trimmed mean district-relative movement reaches this value. |",
        f"| Unstarted-reporting scale | {100*config['unstarted_scale']:g}% | The starting-support factor halves when this fraction of shared starting evidence is absent. |",
        f"| Evidence response scale | {config['evidence_scale']:g} | Sets how gradually the broad prediction loses weight. |",
        f"| Maximum no-addition fraction | {100*config['zero_addition_fraction']:g}% | Sets the shared finished-count branch before evidence, remaining-count and receipt-window discounts. |",
        f"| Small / batch spread in log odds | {config['small_log_odds_sd']:g} / {config['batch_log_odds_sd']:g} | Separate standard deviations retain a concentrated small addition and a wide batch. |", '',
        'If e is evidence strength, the fraction assigned to the two slowing-count components is '
        f"1 − exp(−(e / {config['evidence_scale']:g})²). With the current settings, evidence of 0.25 replaces roughly "
        'half the broad prediction; evidence of 0.5 replaces about 94%. The remaining fraction keeps its broad '
        'distribution. This response was made conservative after observing postal-count pauses; it is a modelling '
        'assumption, not a fitted completion probability.', '',
        '### Do the paired remaining-vote predictions improve?', '',
        'Each row compares the same districts against the same final formal totals. **Broad MAE** and **Progress MAE** '
        'are mean absolute errors in votes for the broad and progress-aware predictions. **Mean additions** is the '
        'progress-aware expected additional vote per district. **95% width** is the average width of its 95% prediction '
        'interval. **Interval score** is that width plus forty times any distance by which the final result lies outside '
        'the interval; lower is better. It penalizes premature confidence as well as unnecessary uncertainty. '
        'Broad samples are repeated onto the same count-draw grid before scoring, so repetition alone cannot change '
        'the paired interval endpoints. '
        'Final rechecks can leave the reviewed final total below the counted account, which this count-preserving '
        'prototype cannot predict. The explicit no-addition branch permits an interval to include the exact '
        'current total. These intervals are not established '
        'as calibrated 95% probabilities.', '',
        '| Source update | Broad MAE | Progress MAE | Mean additions | 95% width | Interval score: broad → progress |',
        '|---|---:|---:|---:|---:|---:|']
    if result['election'] == '2025fed':
        context = [
            'Postal counting has a different reason for pauses: votes can still be arriving from voters. '
            'The [AEC timetable]('+result['postal_deadline_source']+') sets the 2025 receipt deadline at '
            '**6 pm on 16 May**, thirteen days after polling day. Votes must have been cast before polling closed. '
            'A quiet local postal count before this date provides weaker completion evidence than a quiet category '
            'whose ballots have already been cast and are awaiting internal transport or scrutiny.', '',
            'For Postal only, multiply the ordinary slowing-evidence score by **1 / (1 + exp(−t / h))**, where '
            't is the number of hours after the published receipt deadline and '
            f"h is {config['postal_deadline_transition_hours']:g} hours. The factor is half at the deadline, "
            'smaller beforehand and gradually approaches one afterwards. This preserves more of the original '
            'remaining-vote prediction during pauses. The deadline never closes counting: current activity still '
            'weakens completion evidence, and the broad and late-batch possibilities remain. The transition scale '
            'is an exploratory assumption, not a calibrated arrival curve. Other jurisdictions receive no federal '
            'deadline unless their own date is explicitly supplied.', '']
    else:
        context = [
            'The SA comparison repairs two reviewed live-feed category errors, in Croydon and Taylor, without '
            'changing any candidate\'s counted total. It restores the complete polling-day absent batch once the '
            'combined absent/provisional candidate counts contain that batch, leaving further votes as provisionals. '
            'The early batch is relabelled only when its full candidate vector matches the reviewed final record. '
            'No general merging of categories is applied.', '',
            '**These are retrospective repairs using reviewed final candidate records.** They let the replay '
            'examine the forecast separately from known feed mistakes; they do not demonstrate automatic correction '
            'in a real-time forecast. Original feed captures remain unchanged, and the JSON records each repair '
            'and the exact reviewed source hashes. Unidentified partial batches remain unchanged.', '',
            'Flinders and Kavel\'s zero early-absent entries are treated as missing reporting, including in the '
            'reviewed final data. The other previously reviewed missing-category exceptions also apply to the replay. '
            'Missing units supply no progress evidence and are omitted from the detailed '
            'prediction account. Counted votes and broader group priors remain; no category is declared complete '
            'and no replacement split is invented. Their final category partitions are excluded from scoring, '
            'while district totals remain in the total-count comparison. This also marks the final split as '
            'unsuitable for a later election\'s detailed baseline. The remaining broad-group allocation can still '
            'appear in another open category; missing-data exclusions cannot recover the true split, so these '
            'seats remain flagged when interpreting individual category estimates.', '']
    position = lines.index('### Do the paired remaining-vote predictions improve?')
    lines[position:position] = context
    # Keep the consolidated public table short. All daily observations, category
    # scores and individual conditional components remain in the JSON export.
    rows = result['snapshots']
    for row in displayed_snapshots(result):
        broad, score = row['broad_total_score'],row['total_score']
        lines.append(f"| {row['source_timestamp'].replace('T',' ')} | {broad['mean_absolute_error']:,.0f} | "
                     f"{score['mean_absolute_error']:,.0f} | {row['mean_remaining']:,.0f} | {score['width_95']:,.0f} | "
                     f"{broad['interval_score_95']:,.0f} → {score['interval_score_95']:,.0f} |")
    last = rows[-1]
    lines += ['', f"At the last retained source, district-total error falls from {last['broad_total_score']['mean_absolute_error']:,.0f} "
        f"to {last['total_score']['mean_absolute_error']:,.0f} votes. These are repeated observations of one election; "
        'they do not establish performance in another jurisdiction.', '',
        'Category errors check whether a smaller total is also allocated more accurately, rather than merely moving '
        'the error elsewhere. Each observation is one district/group with reviewed final data. SA uses the three '
        'supported early/postal/remaining groups and excludes unreliable category partitions; federal uses six groups. '
        'These comparison units differ from the district-total table. The following averages use the last retained '
        'source, with equal weight per known district/group.', '',
        '| Known district/group observations | Broad category MAE | Progress category MAE | Interval score: broad → progress |',
        '|---:|---:|---:|---:|',
        f"| {last['category_score']['observations']} | {last['broad_category_score']['mean_absolute_error']:,.0f} | "
        f"{last['category_score']['mean_absolute_error']:,.0f} | {last['broad_category_score']['interval_score_95']:,.0f} → "
        f"{last['category_score']['interval_score_95']:,.0f} |", '']
    if result['election'] == '2025fed':
        pause = next((r for r in rows if r['source_timestamp'].startswith('2025-05-11')),None)
        if pause:
            lines += [f"At the 11 May postal pause, district-total error changes from {pause['broad_total_score']['mean_absolute_error']:,.0f} "
                f"to {pause['total_score']['mean_absolute_error']:,.0f} votes. Some postal estimates fall before counting resumes. "
                'The receipt-window discount preserves more of the broad prediction during such pauses; '
                'counting can continue after the last receipt date.', '']
        lines += ['Late residual totals also include unfinished ordinary-booth identities, whose expectations '
                  'this layer does not change.', '']
    else:
        lines += ['Residual additions in seats with missing or inconsistent categories require care: '
            'a repaired replay is not evidence that the original detailed source split was usable. '
            'Unknown reporting is kept separate from genuine zeros and authoritative completion.', '']
    continuous = result['continuity']
    maximum_error = max(r['diagnostics']['maximum_accounting_error'] for r in rows)
    lines += ['', '### Preparation, continuity and reproduction', '',
        f"The comparison uses {config['samples']} frozen preparation outcomes for the whole election and "
        f"{config['count_draws']} count draws per outcome, producing {config['samples']*config['count_draws']} "
        'updated count outcomes per snapshot. These additional draws reuse the preparation inputs; they do not '
        'represent extra independent party-composition samples. Both conditional distributions are also evaluated '
        'explicitly, so a rare batch remains represented without requiring a random branch selection. The core '
        'also returns the per-preparation reference, small-addition and batch parameters separately, alongside the '
        'joint sampled category/total outcomes. They are not reduced to a single standard deviation.', '',
        f"For {continuous['seat']}, the continuity check sweeps evidence strength from zero to one and perturbs each "
        f"point by {continuous['evidence_perturbation']:g}. The largest change in the mean or 95% interval endpoints "
        f"was {continuous['maximum_mean_or_interval_change_votes']:.6f} votes. This is a numerical continuity check, "
        'not evidence that the chosen response speed is optimal. The largest category-to-total accounting discrepancy '
        f"across snapshots was {maximum_error:.3g} votes.", '',
        'The JSON also includes a mechanical first-preference illustration using the currently observed candidate mix '
        'within each source category. Districts with unsupported outstanding-category mixes are omitted. It does not '
        'run the C++ party forecast, estimate new late-voter preferences, or calculate winners.', '',
        'First reproduce the base count replay described above to create its local `analysis.json`. That file '
        'supplies the frozen no-results allocation and metadata paths. The progress comparison also requires '
        'the retained daily source snapshots; these large inputs are not distributed in Git. Then run from '
        '`analysis/`:', '', '```powershell',
        rf".\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_late_batch --elections {result['election']}",
        '```', '',
        'Use `--dry-run` to calculate without writing outputs. `--samples` controls preparation outcomes and '
        '`--count-draws` controls their additional count evaluations. Retained feed locations can be supplied with '
        '`--archive` and `--downloads`. `--postal-transition-hours` changes the smooth postal deadline response '
        'scale for the federal experiment. The command writes `late-batch-analysis.json` beside this report and updates '
        'this section. Its provenance records the frozen origin, fixture, current code and exact source revisions. '
        'Python replay timings include this count calculation; they do not establish whole-application C++ runtime.', '',
        f"Reusing the last broad preparation, one count draw per preparation took {result['count_draw_seconds']['1']:.3f} seconds; "
        f"{config['count_draws']} draws took {result['count_draw_seconds'][str(config['count_draws'])]:.3f} seconds for the whole election. "
        'This times count evaluations only. It includes no additional party-composition preparation, feed parsing '
        'or main simulation iterations.', '']
    return '\n'.join(lines)
