"""Explain completed category-dynamics results without recalculating the analysis."""


MODEL = {
    'previous_shares': 'Previous category shares',
    'proportional_remainder': 'Controls + proportional remainder',
    'ordinary_adjustment': 'Controls + mainly ordinary adjustment',
}
TEST = {'leave_one_out': 'Other elections', 'earlier_only': 'Earlier elections only'}
ANCHOR = {'actual_total': 'Actual final total (diagnostic)', 'zero_drift': 'Previous turnout',
          'half_drift': 'Half historical turnout change', 'full_drift': 'Full historical turnout change'}
VARIANT = {'all_history': 'All eligible training', 'without_covid': 'COVID endpoints excluded from training'}
CATEGORY = {
    'ordinary': 'Ordinary remainder', 'early': 'Combined early',
    'early_ordinary': 'Ordinary early', 'early_declaration': 'Declaration early',
    'postal': 'Postal', 'absent': 'Absent', 'other': 'Other smaller categories',
    'declarations': 'Combined declarations',
    'ordinary_early_absent': 'Ordinary + early + absent',
    'ordinary_other': 'Ordinary + other',
    'ordinary_early_absent_other': 'All non-postal categories',
}
MODE = {
    'vic': 'VIC observed early aggregate',
    'fed_detail': 'Federal PPVC/declaration early split',
    'fed_combined_early': 'Federal total early, old declaration regime',
    'qld_detail': 'QLD ordinary/declaration early split',
    'qld_combined_early': 'QLD early aggregate',
    'sa_combined': 'SA ordinary/combined declarations',
    'wa': 'WA early/ordinary, ordinary includes mobile',
    'wa_broad': 'WA broad non-postal group',
    'nsw': 'NSW observed early aggregate',
}
CONTROL = {'early+postal': 'Early and postal', 'postal': 'Postal only', 'early': 'Early only',
           'early_postal': 'Combined early/postal count'}
MEASURE = {
    'prepoll_votes_cast_cumulative': 'Early votes recorded',
    'federal_prepoll_national': 'Federal national early count',
    'postal_applications_cumulative': 'Postal applications',
    'postal_ballots_issued_cumulative': 'Postal ballots issued',
    'postal_votes_returned_cumulative': 'Postal ballots returned',
    'postal_votes_accepted_cumulative': 'Postal ballots accepted so far',
    'early_and_postal_votes_recorded_cumulative': 'Combined early/postal count',
}


def table(headers, rows):
    def fmt(value):
        if value is None:
            return '—'
        return '{:.3f}'.format(value) if isinstance(value, float) else str(value)
    return ['| ' + ' | '.join(headers) + ' |', '| ' + ' | '.join('---' for _ in headers) + ' |'] + [
        '| ' + ' | '.join(fmt(v) for v in row) + ' |' for row in rows]


def score_table(rows, extra=None):
    return table((extra or []) + ['Test method', 'Vote budget', 'Allocation rule', 'Elections', 'Districts',
        'Category error %', 'Combined-category error %', 'Total error %'], [
        ([CONTROL.get(r['group'], r['group'])] if extra else []) +
        [TEST[r['scheme']], ANCHOR[r['anchor']], MODEL[r['model']], r['elections'], r['districts'],
         r['category_error_pct'], r['aggregate_category_error_pct'], r['total_error_pct']]
        for r in sorted(rows, key=lambda r: (r['group'], list(TEST).index(r['scheme']),
                                             list(ANCHOR).index(r['anchor']), list(MODEL).index(r['model'])))])


def render_report(result):
    lines = ['# Turnout category allocation analysis', '',
        'This report assesses how early-voting and postal information can improve estimates '
        'of the final vote counts in each voting category. Its purpose is to determine '
        'how to allocate a total-vote estimate when voting patterns change.', '',
        'The analysis compares three simple rules using historical final results and audited '
        'operational counts. It tests allocation with the actual final total supplied, repeats '
        'the comparison with predicted totals, documents comparable category definitions, '
        'and describes errors that move together. It does not change live forecasting.', '',
        'Generated {}.'.format(result['generated_at']), '',
        '## Definitions and the three rules', '',
        'A formal vote is a valid lower-house ballot in the final first-preference count. '
        'Enrolment is the number of registered electors. An operational control is a published '
        'early-voting or postal count converted into an estimate of final formal votes. The '
        '[operational calibration report](../turnout-operational-calibration/report.md) explains '
        'these conversion multipliers and their errors.', '',
        'A district observation here is one election and district with usable final category '
        'counts and at least one independently calibrated control. Every rule is tested on '
        'the same observations. Earlier complete same-name category counts provide the baseline. '
        'For a new name or missing earlier partition, the previous election’s observed aggregate '
        'category rates supply a labelled baseline; no earlier district counts are invented.', '']
    lines += table(['Allocation rule', 'Calculation', 'Question'], [
        [MODEL['previous_shares'], 'Previous category shares × current vote budget',
         'Do new controls improve on carrying the previous distribution forward?'],
        [MODEL['proportional_remainder'], 'Use controls, then share the remaining budget in previous uncontrolled proportions',
         'Does a simple proportional adjustment allocate the residual accurately?'],
        [MODEL['ordinary_adjustment'], 'Use controls, retain uncontrolled category counts per enrolled elector, allocate the remainder to ordinary',
         'Should ordinary voting absorb most of the adjustment instead of reducing smaller categories?']])
    lines += ['', 'When a controlled early total has an observed ordinary/declaration split, '
        'the proportional rule keeps their previous proportions. The ordinary-adjustment rule '
        'retains the previous declaration-early rate per enrolled elector and assigns the rest '
        'to ordinary early. VIC’s published combined early category is not split. Early votes '
        'without a current control retain their previous rate under ordinary adjustment; that '
        'assumption is tested explicitly through the postal-only comparisons.', '',
        '## Main findings', '',
        'With the actual total supplied, category error can be expressed as the percentage '
        'of votes that would need to move between categories to correct the allocation. '
        'This is half the sum of absolute category errors, divided by formal votes. It is '
        'a diagnostic of allocation, not a forecast using an unknown election total.', '']
    for scheme in TEST:
        selected = [r for r in result['scores'] if r['scope'] == 'all' and r['anchor'] == 'actual_total'
                    and r['scheme'] == scheme and r['training_variant'] == 'all_history']
        if selected:
            values = {r['model']: r for r in selected}
            lines.append('- {}: votes requiring reallocation average {:.2f}% with previous shares, '
                         '{:.2f}% with proportional remainder, and {:.2f}% with mainly ordinary '
                         'adjustment, across {} elections.'.format(TEST[scheme],
                             values['previous_shares']['reallocation_pct'],
                             values['proportional_remainder']['reallocation_pct'],
                             values['ordinary_adjustment']['reallocation_pct'],
                             selected[0]['elections']))
    winners = [min((r for r in result['scores'] if r['scope'] == 'all'
                   and r['anchor'] == 'actual_total' and r['scheme'] == scheme
                   and r['training_variant'] == 'all_history'),
                  key=lambda r: r['category_error_pct'])['model'] for scheme in TEST]
    if winners == ['proportional_remainder', 'proportional_remainder']:
        lines += ['', 'Of the three tested rules, calibrated controls with proportional '
            'allocation of the remainder perform best overall under both training methods. '
            'The evidence does not support treating unchanged smaller-category rates as '
            'a general rule. Federal comparisons are closer and sometimes favour ordinary '
            'adjustment; the jurisdiction and individual-election tables retain that distinction.']
    lines += ['', 'The control-specific and jurisdiction results below show whether that '
        'overall comparison is transferable. Scores are descriptive evidence from a small '
        'number of elections; many districts do not create many independent election conditions.', '',
        '## Which historical categories are comparable?', '',
        'This table establishes which comparisons describe the same voting groups. A category '
        'regime is a set of compatible published definitions, not a fitted statistical effect. '
        'Grouping observed counts preserves their totals; it does not recover a hidden old split.', '',
        'Known service or eligibility changes are listed in a shared category policy. '
        'The affected category is combined with ordinary votes where a broader observed '
        'comparison is available. Its separate change does not train behavioural uncertainty. '
        'QLD 2020 cancelled declared-institution placeholders are suppressed within that '
        'election; the separately reported remote-mobile votes remain included.', '',
        'Current districts counts available final partitions in the current election. Missing current '
        'partitions cannot be scored. Missing prior baselines includes renamed/new districts '
        'and absent earlier partitions; aggregate-rate baselines are used for those districts. '
        'Routine eligibility allows the category comparison, but does not guarantee a suitable '
        'operational control or an independent conversion estimate.', '']
    lines += table(['Previous → current', 'Category grouping', 'Current districts', 'Missing current',
                    'Missing prior baseline', 'Routine eligible', 'Definitions and cautions'], [
        [r['previous'] + ' → ' + r['current'], MODE[r['mode']], r['districts'],
         len(r['missing_current']), len(r['unmatched_previous']), 'Yes' if r['usable'] else 'No',
         ' '.join(r['notes']) or 'Observed category groups retained.'] for r in result['compatibility']])
    lines += ['', 'Federal ordinary remainder means ordinary votes outside the identified PPVC '
        'component, including mobile/hospital votes. Federal early counts are used nationally '
        'and distributed across all current districts according to previous resident early '
        'rates and current enrolment. Issuing-centre district counts are not used as resident '
        'controls. Aggregate allocation includes districts lacking a named prior baseline '
        'before scoring the districts with current category data.', '',
        'SA 2026 has no earlier detailed declaration baseline. Its early/postal budget check '
        'appears separately below. WA 2025 uses all non-postal votes as one group; that view '
        'cannot identify ordinary-to-early substitution. NSW {}.'.format(
            'is included in training and tests in this run' if result['config']['include_nsw']
            else 'is displayed as category evidence but excluded from training and prediction tests'), '',
        '## Recent observed category changes', '',
        'This check asks whether changes in voting methods principally coincide with ordinary '
        'declines or with changes in postal and smaller categories. It describes the same '
        'named districts at both elections; it does not establish who switched voting methods.', '',
        'Share change is the change in percentage points of formal votes. Count change per '
        '1,000 electors removes enrolment growth. Allocation change per 1,000 compares the '
        'current count with previous shares applied to the actual current formal total, removing '
        'the change in total voting participation/formality. These allocation changes sum to '
        'zero, so negative relationships partly follow from accounting. Reporting breaks stay '
        'visible and are not routine training evidence.', '']
    recent = {'2022vic', '2025fed', '2024qld', '2025wa', '2026sa', '2021wa'}
    lines += table(['Previous → current', 'Category', 'Districts', 'Share change (points)',
                    'Count change / 1,000 electors', 'Allocation change / 1,000 electors'], [
        [r['previous'] + ' → ' + r['current'], CATEGORY[r['category']], r['districts'],
         r['share_change_pp'], r['count_change_per_1000'], r['allocation_change_per_1000']]
        for r in result['changes'] if r['current'] in recent])
    lines += ['', 'VIC 2022 and WA 2021 show that growth in early/postal voting can accompany '
        'substantial reductions in absent voting as well as ordinary voting. FED 2025 shows '
        'a different pattern: most growth is in ordinary early voting, while absent and '
        'declaration early rates change much less. These are reasons to test allocation '
        'assumptions rather than require all smaller categories to stay unchanged. They '
        'are observations across different conditions, including pandemic-period elections, '
        'rather than estimates of individual voters’ switching behaviour.']
    lines += ['', '## How the prediction tests work', '',
        'The purpose is to test transfer to another election rather than reproduce each '
        'election with its own conversion. “Other elections” excludes the test election. '
        'For turnout changes it also excludes a successor transition using that election '
        'as its starting point. “Earlier elections only” uses historical data completed '
        'before the test election. Neither is treated as the definitive verdict.', '',
        'Early and postal conversion uses the independent operational-calibration estimates. '
        'Postal stages are chosen in this fixed order: applications, issued ballots, returned '
        'ballots, then accepted-so-far counts. A stage without an independent conversion is '
        'skipped. Contemporary evidence is preferred by the audit; revised historical series '
        'remain labelled in the local output. Dates are count dates, with historical publication '
        'availability not fully verified. This is not a complete historical live replay.', '',
        'The predicted vote budget is current enrolment × previous turnout, adjusted by half '
        'the mean training turnout change in log odds, × previous formality. The sensitivity table also '
        'uses zero and full turnout change. No formality point drift or per-district coefficients '
        'are fitted. The actual-total pass supplies final district totals solely to isolate '
        'allocation error.', '',
        'Category error % is the sum of absolute district/category errors divided by all formal '
        'votes, times 100. Combined-category error % adds districts within each category before '
        'taking absolute errors: offsetting district errors can cancel. Total error % measures '
        'the absolute error in the combined formal total. Each percentage is calculated per '
        'election and elections are averaged equally. A vote shifted between two categories '
        'contributes twice to category error. Predicted-total errors also include missing or '
        'extra total votes, so they cannot simply be halved into votes requiring reallocation.', '',
        '### Controls and independent training support', '',
        'This table shows what information each prediction actually uses and how many '
        'independent elections supply its conversion multiplier. A large district sample '
        'cannot compensate for a multiplier learned from one election. A multiplier of '
        '0.8 means 100 recorded votes or applications become an estimate of 80 final '
        'formal votes. Aggregate controls are distributed using previous category rates '
        'and current enrolment; district controls are converted directly.', '',
        'Input status distinguishes counts retained during an election from later '
        'reconciled historical series. The count dates and source identities are retained '
        'in the local analytical output and the operational-calibration report. A one-election '
        'estimate is kept visible as weak evidence, rather than silently dropping those '
        'prediction tests.', '']
    lines += table(['Election', 'Test method', 'Control', 'Geography', 'Multiplier',
                    'Training elections', 'Input status'], [
        [t['current'], TEST[t['scheme']], MEASURE[c['measure']],
         'Aggregate distributed to districts' if c['aggregate'] else 'District count',
         c['factor'], len(c['training_elections']),
         'Recorded during the election' if c['observation_status'] == 'contemporaneous'
         else 'Later reconciled series']
        for t, c in sorted(((t, c) for t in result['tests']
                            if t['training_variant'] == 'all_history' for c in t['controls']),
                           key=lambda item: (item[0]['current'], item[1]['measure'],
                                             list(TEST).index(item[0]['scheme'])))])
    lines += ['',
        '### Overall comparison', '']
    primary = [r for r in result['scores'] if r['scope'] == 'all' and r['training_variant'] == 'all_history'
               and r['anchor'] in {'actual_total', 'half_drift'}]
    lines += score_table(primary)
    lines += ['', '### What controls are available?', '',
        'This separates elections with both early and postal information from those with '
        'only one type. It tests whether ordinary adjustment works only when early growth '
        'is actually observed. Group samples can differ; compare rules within a row group.', '']
    lines += score_table([r for r in result['scores'] if r['scope'] == 'controls'
        and r['training_variant'] == 'all_history' and r['anchor'] == 'actual_total'], ['Controls'])
    lines += ['', '### Results by jurisdiction', '',
        'This checks whether a pooled result conceals a failure in the target jurisdiction. '
        'The supplied final total isolates allocation; the full pooled training exclusions '
        'remain unchanged.', '']
    lines += table(['Jurisdiction', 'Test method', 'Rule', 'Elections', 'Districts', 'Category error %'], [
        [r['group'].upper(), TEST[r['scheme']], MODEL[r['model']], r['elections'], r['districts'],
         r['category_error_pct']] for r in sorted((r for r in result['scores']
             if r['scope'] == 'jurisdiction' and r['training_variant'] == 'all_history'
             and r['anchor'] == 'actual_total'), key=lambda r: (
                 r['group'], list(TEST).index(r['scheme']), list(MODEL).index(r['model'])))])
    lines += ['', '### Individual election comparisons', '',
        'These results identify concrete successes and failures behind the averages. '
        'The percentage is votes requiring reallocation, with the actual final total '
        'supplied: it is half the category error percentage above. The columns compare '
        'the same districts within each election and training method.', '']
    individual = {(r['group'], r['scheme'], r['model']): r for r in result['scores']
                  if r['scope'] == 'election' and r['anchor'] == 'actual_total'
                  and r['training_variant'] == 'all_history'}
    lines += table(['Election', 'Test method', 'Districts', 'Previous shares %',
                    'Proportional remainder %', 'Mainly ordinary adjustment %'], [
        [code, TEST[scheme], individual[code, scheme, 'previous_shares']['districts'],
         *[individual[code, scheme, model]['reallocation_pct'] for model in MODEL]]
        for code, scheme in sorted({(code, scheme) for code, scheme, _ in individual},
                                   key=lambda k: (k[0], list(TEST).index(k[1])))])
    lines += ['', 'Only VIC 2022 has both a compatible prior partition and usable controls '
        'with independent conversion estimates in the default prediction sample. Its '
        'absent category explains much of the difference between the two controlled rules: '
        'the ordinary-adjustment rule retains the higher prior absent rate and compensates '
        'by estimating fewer ordinary votes. One Victorian election cannot establish a '
        'universal allocation relationship. QLD 2024 has only a postal control here; '
        'retaining the old early rate misses the observed rise in early voting.']
    lines += ['', '## Sensitivity to totals and COVID-period training', '',
        'These checks ask whether the allocation comparison changes under another reasonable '
        'formal-total assumption or when unusual pandemic-period evidence is removed from '
        'training. They are fixed comparisons, not a search for an optimal drift coefficient.', '',
        'The COVID sensitivity removes rate transitions touching QLD 2020, WA 2021 and '
        'Federal/VIC/SA 2022, and removes those elections from conversion training. The same '
        'test elections, their previous baselines and their observed controls remain eligible. '
        'If conversion evidence disappears, no prediction '
        'is produced. The comparison below uses only election/district observations '
        'available under both training choices, keeping the evaluation sample identical.', '']
    lines += score_table([r for r in result['scores'] if r['scope'] == 'all'
        and r['training_variant'] == 'all_history' and r['anchor'] in {'zero_drift', 'full_drift'}])
    lines += ['', '### Paired comparison of training choices', '',
        'Category error has the same definition as above. “All training” retains eligible '
        'COVID-period evidence; “Without COVID” removes the specified endpoints from '
        'training. The tested elections, including pandemic-period elections, stay the '
        'same within each row.', '']
    lines += table(['Test method', 'Vote budget', 'Rule', 'Shared elections', 'Shared districts',
                    'All training category error %', 'Without COVID category error %'], [
        [TEST[r['scheme']], ANCHOR[r['anchor']], MODEL[r['model']],
         r['all_history']['elections'], r['all_history']['districts'],
         r['all_history']['category_error_pct'], r['without_covid']['category_error_pct']]
        for r in sorted(result['covid_comparison'], key=lambda r: (
            list(TEST).index(r['scheme']), list(ANCHOR).index(r['anchor']), list(MODEL).index(r['model'])))])
    lines += ['', '## Do category errors move together?', '',
        'This checks the uncertainty needed by a conserved allocation. When the total is '
        'fixed, increasing early or postal estimates requires a decrease somewhere else. '
        'Treating all categories as independent would lose that relationship.', '',
        'These diagnostics use the actual-total pass. Ordinary, all early, postal and all '
        'other categories form four exhaustive groups whose errors sum to zero. Election '
        'errors are the combined category error per 1,000 enrolled electors. District errors '
        'remove that election’s error first; districts are weighted by enrolment within an '
        'election and elections get equal weight. These observed error patterns are not '
        'independent probability-coverage guarantees.', '',
        'Comparisons that publish only a broader ordinary/other or non-postal pool are '
        'omitted from this four-group diagnostic. They remain in allocation scores; their '
        'hidden components cannot be recovered without inventing counts. The errors here '
        'are signed vote-count differences, so they are reported directly. Modelled '
        'category proportions use logarithmic transformations.', '',
        'RMS is the square root of the mean squared error, so it includes systematic bias '
        'and gives larger errors more weight. All RMS columns use votes per 1,000 enrolled '
        'electors. The correlation columns measure linear '
        'co-movement, from −1 to +1. Negative values indicate errors tending in opposite '
        'directions, but conservation itself causes some negative relationships. The local '
        'JSON includes complete covariance matrices: covariance describes how the sizes '
        'and directions of two errors vary together.', '']
    lines += table(['Controls', 'Test method', 'Rule', 'Error level', 'Elections', 'Ordinary RMS',
                    'Early RMS', 'Postal RMS', 'Other RMS', 'Ordinary/early correlation',
                    'Ordinary/postal correlation'], [
        [CONTROL.get(r['controls'], r['controls']), TEST[r['scheme']], MODEL[r['model']],
         'Election total' if r['level'] == 'election' else 'District after election error',
         r['elections'], *[r['rms_per_1000'][k] for k in ('ordinary', 'early', 'postal', 'other')],
         r['correlation']['ordinary']['early'], r['correlation']['ordinary']['postal']]
        for r in sorted(result['joint_errors'], key=lambda r: (
            r['controls'], list(TEST).index(r['scheme']), r['level'], list(MODEL).index(r['model'])))])
    lines += ['', 'A minimal joint structure is ordinary = total − early − postal − other. '
        'Total uncertainty moves the overall budget; allocation uncertainty redistributes '
        'that budget. A common conversion error must be shared across its districts, while '
        'district variation remains additional. These matrices describe the historical '
        'evidence; they do not justify fitting a separate relationship for every district.', '',
        '## SA 2026 controlled vote budget', '',
        'This diagnostic asks what the early/postal estimates leave for all remaining categories '
        'combined. It uses SA 2026 controls with SA 2026 excluded from conversion and turnout '
        'training. It does not infer the previous absent/provisional/other declaration split '
        'or establish that a live count is complete.', '',
        'Totals over districts with reliable category splits separate errors in the vote budget from errors in '
        'the two controls. All errors are estimated minus actual final votes. Remaining-category '
        'error equals total error minus early error minus postal error. Supplying the '
        'actual total removes the first contribution and isolates conversion error; using '
        'the predicted total shows the complete initial estimate.', '']
    lines += table(['Test method', 'Vote budget', 'Districts', 'Total error (votes)',
                    'Early error (votes)', 'Postal error (votes)', 'Remaining-category error (votes)'], [
        [TEST[r['scheme']], ANCHOR[r['anchor']], r['districts'], r['total_error'],
         r['early_error'], r['postal_error'], r['other_error']] for r in result['sa_summary']])
    lines += ['',
        'Six districts were selected for low estimated completion near the final SA 2026 count. '
        'The table includes those with reliable category targets; reviewed missing splits are omitted. '
        'They are shown because errors in their expected category sizes can inflate the '
        'projected uncounted vote. The table uses the predicted total. Remaining-category '
        'error is predicted total minus estimated '
        'early and postal votes, less the actual sum of all other final formal votes. Positive '
        'means too large a combined remainder. This is the initial size estimate, not a count '
        'of votes still unreported at the final snapshot.', '']
    seats = {'Croydon', 'Flinders', 'Giles', 'Mount Gambier', 'Taylor', 'Stuart'}
    lines += table(['District', 'Test method', 'Predicted total', 'Actual total', 'Estimated early',
        'Estimated postal', 'Estimated other', 'Actual other', 'Other error (votes)'], [
        [r['seat_name'], TEST[r['scheme']], r['predicted_total'], r['actual_total'], r['predicted_early'],
         r['predicted_postal'], r['predicted_other'], r['actual_other'], r['predicted_other'] - r['actual_other']]
        for r in sorted((r for r in result['sa_budget'] if r['seat_name'] in seats
                         and r['anchor'] == 'half_drift'),
                        key=lambda r: (r['seat_name'], list(TEST).index(r['scheme'])))])
    lines += ['', 'Both training methods give identical SA 2026 estimates because it is '
        'the latest election in the retained evidence. The budget over comparison districts is reasonably '
        'close while individual districts still differ. In particular, an overall total '
        'estimate does not identify closed or mismatched booths, or establish that an '
        'apparently open declaration batch has finished counting.']
    lines += ['', '## Data limitations and reproduction', '',
        'Constraints preserve nonnegative category estimates and their total. If controls '
        'exceed the budget they are reduced proportionally. If protected categories exceed '
        'the remainder they are reduced proportionally and ordinary is zero. Those are '
        'recorded conflicts between uncertain estimates, not evidence that a category is '
        'known complete. The analytical output records every activation.', '',
        'Renamed districts and redistributions are not boundary-adjusted. Missing category '
        'partitions are omitted from scores. Whole-election sample sizes, source histories '
        'and control training identities remain visible. Elections without a usable '
        'independent conversion are listed below.', '']
    lines += ['', 'Reviewed implausible zeros represent missing counts, and the entire '
        'affected district split is omitted from allocation comparisons because votes '
        'may have been recorded elsewhere. District totals and pre-election controls '
        'remain usable. Unknown final targets cannot train conversion factors. '
        'The local output records these exclusions separately from empty groups.', '',
        'Possible observed category zeros receive a half-vote equivalent in '
        'starting weights; the published counts and scoring targets stay unchanged. The '
        'share input floor is the smaller of 0.1% and half a vote divided by the observed '
        'group total, preserving genuine smaller shares in large groups. Entirely empty '
        'aggregated groups remain listed for review in the local numerical output; unknown '
        'components are omitted rather than filled with zeros. Only the selected final '
        'pre-election control is used.', '',
        'Relationships between turnout (ballots/enrolment), formality (formal/ballots) '
        'and category shares (category/formal) have different parent groups. The local '
        'output explicitly flags their joint interpretation for human review. Transforming '
        'the individual rates does not make these denominators interchangeable. '
        'The historical category-share/turnout correlations are exploratory '
        'report diagnostics; they do not supply prediction coefficients to this '
        'allocation or to the initial count-distribution prototype. Federal '
        'issuing-centre district comparisons are also diagnostic only. Federal '
        'early allocation uses the national published count and previous '
        'resident-electorate results, rather than treating centre attendance '
        'as a count of that district’s residents.', '']
    lines += table(['Election', 'Test method', 'Reason'], [
        [r['current'], TEST[r['scheme']], r['reason']] for r in result['skipped']])
    lines += ['', 'Run from the analysis directory on native Windows:', '', '~~~powershell',
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_category_dynamics --dry-run',
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_category_dynamics',
        '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_category_dynamics --check', '~~~', '',
        'The public report contains implemented methods and consolidated findings. The '
        'ignored local analysis.json contains comparisons, predictions, training identities, '
        'scores and joint error matrices. Refresh normalized sources and the federal supplement '
        'after a source correction, then regenerate this analysis. Input and code content '
        'hashes let --check detect local changes; it does not poll remote sources. '
        'Add --include-nsw to include NSW in training and prediction. An em dash indicates '
        'that a quantity cannot be estimated from the available evidence.', '']
    return '\n'.join(lines)
