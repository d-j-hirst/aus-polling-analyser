"""Render the completed operational-calibration analysis as a public report.

This module owns terminology, explanatory prose, table ordering and Markdown.
The analysis supplies all scores, sensitivity checks and outlier selections;
rendering does not fit parameters or recalculate analytical diagnostics.
"""

def table(headers, rows):
    def fmt(value):
        if value is None:
            return '—'
        return '{:.4f}'.format(value) if isinstance(value, float) else str(value)
    return ['| ' + ' | '.join(headers) + ' |', '| ' + ' | '.join('---' for _ in headers) + ' |'] + [
        '| ' + ' | '.join(fmt(value) for value in row) + ' |' for row in rows]


MEASURE_NAMES = {
    'early_and_postal_votes_recorded_cumulative': 'Early and postal ballots reported together',
    'postal_applications_cumulative': 'Postal applications',
    'postal_ballots_issued_cumulative': 'Postal ballots issued',
    'postal_votes_returned_cumulative': 'Postal ballots returned',
    'postal_votes_accepted_cumulative': 'Postal votes accepted so far',
    'prepoll_votes_cast_cumulative': 'Early ballots reported cast',
    'federal_prepoll_district_proxy': 'Federal early voting: administering electorate',
    'federal_prepoll_national': 'Federal early voting: national total',
}
MODEL_NAMES = {
    'pooled': 'Single multiplier',
    'kind_pooled': 'Separate federal and state multipliers',
    'previous_conversion': 'Last comparable multiplier',
    'previous_category': 'Previous category count, enrolment-adjusted',
}
TEST_NAMES = {'leave_one_out': 'Other elections', 'earlier_only': 'Earlier elections only'}
STATUS_NAMES = {
    'contemporaneous': 'Recorded during the election',
    'final_reconciled': 'Revised or reconstructed afterward',
}
GROUP_NAMES = {
    'all': 'Federal and state together',
    'federal': 'Federal elections',
    'state': 'State elections together',
}


MODELS = tuple(MODEL_NAMES)
SCHEMES = tuple(TEST_NAMES)


def paired_error_table(comparisons):
    """Keep the same measurement and comparison model together for readers."""
    return table(['Comparison model', 'Test method', 'Elections', 'Observations',
                  'Single: total error %', 'Comparison: total error %',
                  'Single: observation error %', 'Comparison: observation error %'], [
        [MODEL_NAMES[r['model']], TEST_NAMES[r['scheme']], r['pooled']['elections'],
         r['pooled']['cases'], r['pooled']['aggregate_abs_pct'], r['alternative']['aggregate_abs_pct'],
         r['pooled']['case_abs_pct'], r['alternative']['case_abs_pct']]
        for r in sorted(comparisons, key=lambda r: (r['family'], MODELS.index(r['model']),
                                                    SCHEMES.index(r['scheme'])))
        if r['pooled']['elections']])


def render_report(result):
    """Explain the question, quantities and limitations beside each analysis."""
    rows, summaries = result['comparisons'], result['conversions']
    diagnostics = result['diagnostics']
    federal = result['federal_prepoll']
    lines = ['# Operational turnout calibration', '',
             'This report assesses whether published early-voting and postal counts can help '
             'estimate how many formal votes will eventually be counted. Its purpose is to '
             'show which counts are informative, how accurately they predict final voting-'
             'category sizes, and where substantial errors remain.', '',
             'The analysis compares historical published counts with final results, tests '
             'predictions on elections excluded from estimation, and describes conversion '
             'multipliers and their variation. A separate investigation tests federal pre-poll '
             'counts whose electorate definitions differ from those of the final results. '
             'These offline analyses do not change the live forecast.', '',
             'Generated {}. The main analysis contains {:,} matched observations across {} '
             'election/measurement combinations. Federal pre-poll comparisons appear separately.'.format(
                 result['generated_at'], len(rows), len(summaries)), '', '## Main findings', '',
             'These findings identify where a simple conversion helps and where substantial '
             'errors remain. The following sections define the comparisons and show their evidence.', '']
    for comparison in result['paired_scores']:
        if comparison['family'] != 'postal_applications_cumulative' or comparison['model'] != 'kind_pooled':
            continue
        left, right = comparison['pooled'], comparison['alternative']
        if left['elections']:
            lines.append('- Postal applications, {}: estimating one multiplier from federal '
                         'elections and another from state elections reduces average error '
                         'in the combined postal total from {:.2f}% to {:.2f}%, compared with '
                         'one multiplier estimated from both groups together. The comparison '
                         'uses identical observations across {} elections.'.format(
                             TEST_NAMES[comparison['scheme']].lower(), left['aggregate_abs_pct'],
                             right['aggregate_abs_pct'], left['elections']))
    early = next((p for p in result['parameters']
                  if p['family'] == 'prepoll_votes_cast_cumulative' and p['kind'] == 'all'), None)
    if early:
        lines.append('- For the {} eligible state early-vote comparisons, the mean ratio of '
                     'final formal early votes to reported early ballots is {:.4f}. Applied '
                     'to 10,000 reported ballots, this multiplier predicts about {:,.0f} '
                     'formal early votes. It is shared across districts; it is a coefficient, '
                     'not a correlation or a district-specific estimate.'.format(
                         early['n_elections'], early['factor'], early['factor'] * 10000))
    if federal['available']:
        pair = next((r for r in federal['paired_scores'] if r['family'] == 'federal_prepoll_national'
                     and r['model'] == 'previous_category' and r['scheme'] == 'leave_one_out'), None)
        if pair and pair['pooled']['elections']:
            lines.append("- Federal pre-poll counts are informative despite being reported under "
                         "the electorate administering the centre. Across {} elections, a "
                         "multiplier applied to the national count gives {:.2f}% average total "
                         "error, compared with {:.2f}% for the previous election's early-vote "
                         "count adjusted for enrolment. Electorate errors include severe exceptions.".format(
                             pair['pooled']['elections'], pair['pooled']['aggregate_abs_pct'],
                             pair['alternative']['aggregate_abs_pct']))
    lines += ['- Small election-total errors can hide large district errors because overestimates '
              'and underestimates cancel. Both errors are reported separately.',
              '- Trial intervals intended to contain 80% of outcomes miss that target in some '
              'comparisons. The reported error allowances describe historical evidence; their '
              'probability coverage has not been independently established.', '',
              '## What is being counted and estimated?', '',
              'The aim is to estimate the final size of a voting category before its count is '
              'complete. Published application or early-voting counts help only if their '
              'relationship with final formal votes is understood.', '',
              'A **formal vote** is a valid ballot included in the lower-house first-preference '
              'count. **Enrolment** is the number of registered electors. **District**, '
              '**electorate** and **seat** refer to the same lower-house electoral area here; '
              'a federal electorate is also called a division.', '',
              'Reviewed implausible category zeros are treated as missing counts in '
              'the analytical view. Their district splits cannot provide final targets '
              'for conversion estimation or accuracy scoring, since votes may have '
              'been classified elsewhere. Whole-district totals remain available. '
              'The published controls still inform initial predictions elsewhere in '
              'the analysis; their missing targets do not become zero votes.', '',
              'An **operational count** is a published application, issue, return, acceptance '
              'or early-voting count at a stated date. It is not necessarily a final formal '
              'vote count. “Cumulative” in the source data means the total through that date, '
              'rather than the number on that day.', '']
    lines += table(['Measurement', 'Published quantity', 'Final quantity being estimated'], [
        ['Postal applications', 'Applications recorded by the stated date', 'Final formal postal votes'],
        ['Postal ballots issued', 'Postal ballot packages sent by the stated date', 'Final formal postal votes'],
        ['Postal ballots returned', 'Postal ballots received by the stated date', 'Final formal postal votes'],
        ['Postal votes accepted so far', 'Postal votes accepted at the stated processing stage', 'Final formal postal votes'],
        ['Early ballots reported cast', 'Reported in-person early ballots or voters recorded as having voted',
         'Final formal early votes in the matched source categories'],
        ['Early and postal ballots reported together', 'One published combined early/postal count',
         'Final formal early and postal votes together']])
    lines += ['', 'The **conversion multiplier** is final matched formal votes divided by the '
              'published operational count. Prediction uses **predicted formal votes = multiplier '
              '× operational count**. A multiplier of 0.95 converts 10,000 reported ballots into '
              'an estimate of 9,500 formal votes. It is a coefficient, not a correlation. It is '
              'not simply an acceptance probability: timing, return rates, informal ballots, '
              'eligibility and differences in scope can all contribute. A value above 1 can '
              'occur when further returns or acceptance processing follow the published count.', '',
              'An **observation** is one selected operational count paired with its final target: '
              'usually one district for one election and measurement, or one state/national total '
              'when only a total is usable. This is what the machine-readable output calls a '
              '“case”. The target adds the final categories matching that observation; it is '
              'not necessarily the total vote across all categories in the district.', '',
              'For each measurement, the **single multiplier** averages the available election '
              'ratios. District counts are first added within each election to calculate its '
              'ratio, and each election gets equal weight in the mean. “Shared” or “pooled” means '
              'this multiplier applies to all relevant districts rather than being fitted '
              'separately for each one. No district coefficients are fitted.', '',
              'One source series is selected per election and measurement, preferring records '
              'made during the election, then counts for electors enrolled in the named district, '
              'then official sources. A parent total and its district rows are not independent '
              'evidence. The [evidence audit](../turnout-evidence-audit/report.md) documents '
              'precision and category matching.', '',
              '## Observed conversion ratios', '',
              'This table checks whether a measurement consistently translates into the same '
              'final quantity. Large changes in the multiplier suggest that copying one '
              'historical relationship may be unreliable.', '',
              '**Observations** counts matched district or total observations. **Reported count** '
              'and **Final formal** add those observations once. **Multiplier** is their ratio. '
              '**Count through** is the latest quantity date, not a verified publication time; '
              'full date ranges are retained in the local JSON when districts differ. '
              '**Input history** distinguishes records made during the election from later '
              'revised/reconstructed series. A later reconstruction may describe quantities '
              'at election time without showing those exact figures were publicly available then.', '']
    lines += table(['Measurement', 'Election', 'Observations', 'Reported count', 'Final formal',
                    'Multiplier', 'Count through', 'Input history'], [
        [MEASURE_NAMES[r['family']], r['election_code'], r['cases'], r['operational_count'],
         r['final_formal'], r['conversion'], r['last_cutoff'], STATUS_NAMES[r['observation_status']]]
        for r in sorted(summaries, key=lambda r: (r['family'], r['election_date']))])
    lines += ['', 'Election identifiers combine year and jurisdiction: fed = federal, vic = '
              'Victoria, sa = South Australia, wa = Western Australia, qld = Queensland and '
              'nsw = New South Wales. NSW observations {}.'.format(
                  'enter estimation and prediction tests in this run' if result['config']['include_nsw']
                  else 'are displayed here but excluded from estimation and prediction tests by default'), '',
              '## How prediction comparisons work', '',
              'An observed ratio can fit its own election perfectly without helping predict '
              'another election. These tests assess whether the relationship transfers by '
              'excluding every observation from the election being predicted when estimating '
              'its multiplier. The excluded election is the **test election**; the elections '
              'used to estimate the multiplier are the **training elections**.', '',
              'Both test methods are shown because the sample is small. Neither is assumed '
              'to give the more reliable verdict.', '']
    lines += table(['Test method', 'Training data', 'Purpose and limitation'], [
        ['Other elections', 'All eligible elections except the test election',
         'Uses more evidence, but can include elections later than the test election'],
        ['Earlier elections only', 'Eligible elections before the test election',
         'Tests learning from the past, but early tests can have very few training elections']])
    lines += ['', 'The models below test whether the operational count adds information and '
              'whether separating election types improves conversion. **Comparison model** '
              'replaces the old “alternative” label; each error row names the model being '
              'compared with the single multiplier.', '']
    lines += table(['Model', 'Calculation', 'Question it addresses'], [
        ['Single multiplier', 'Mean of election conversion ratios for the measurement',
         'Can one shared conversion describe these elections?'],
        ['Separate federal and state multipliers', 'One mean for federal elections, another for all state elections together',
         'Does this distinction improve on combining federal and state elections?'],
        ['Last comparable multiplier', 'Latest earlier eligible ratio in the same jurisdiction × new operational count',
         'Is the latest comparable election a better guide than the wider historical mean?'],
        ['Previous category count, enrolment-adjusted',
         'Previous formal category count × current enrolment / previous enrolment',
         'Does the new operational count improve on carrying the previous category forward?']])
    lines += ['', '“Federal and state” means **federal elections versus state elections**, not '
              'separate parameters for individual Australian states within a federal election. '
              'The separate model needs at least two training elections of the appropriate type; '
              'otherwise it uses the single multiplier across both types. The last-comparable '
              'model always uses an earlier election under either test method. The previous-count '
              'model matches district names and categories where possible; it does not adjust '
              'for redistribution. Missing matches produce no prediction.', '',
              '### Reading the error columns', '',
              'These two errors answer different questions: is the overall category size right, '
              'and are its votes being assigned to the right districts?', '',
              '- **Total error %** (formerly “Aggregate %”) = 100 × absolute value of '
              '(sum of predicted counts − sum of actual counts) / sum of actual counts, '
              'within an election. Overestimates and underestimates can cancel.',
              '- **Observation error %** (formerly “Case %”) = 100 × sum of absolute '
              '(predicted count − actual count) for each observation / sum of actual counts, '
              'within an election. District errors cannot cancel. For one total observation, '
              'this equals total error.',
              '- Tables average these election-level percentages equally across elections. '
              'Within an election, observations contribute according to vote count. Lower '
              'values mean more accurate predictions.', '',
              'For example, two districts with 100 actual votes each and predictions of 110 '
              'and 90 have 0% total error but 10% observation error. The denominator is the '
              'matched voting category, not all votes in the seat.', '',
              '## Prediction errors on identical observations', '',
              'Each row compares the single multiplier with the named model on exactly the '
              'same observations. This prevents missing historical matches from favouring '
              'a model merely because it was tested on easier districts. **Elections** and '
              '**Observations** describe the shared sample and can differ between rows. '
              '**Single** and **Comparison** identify the two models whose errors are shown. '
              'Measurements are grouped first, then comparison model and test method.', '']
    for measure in sorted({r['family'] for r in result['paired_scores']}):
        selected = [r for r in result['paired_scores'] if r['family'] == measure and r['pooled']['elections']]
        if selected:
            lines += ['### ' + MEASURE_NAMES[measure], ''] + paired_error_table(selected) + ['']
    lines += ['## Counts recorded during the election versus revised series', '',
              'This check shows whether accuracy depends heavily on counts revised or '
              'reconstructed afterward. Such data can inform conversion analysis without '
              'establishing what a live system could have predicted from the original figures.', '',
              "**Test input history** refers to the test election's count. Training can use "
              "eligible elections of either history because their final records are available "
              "to later elections. The error columns use the definitions above; **Observations** "
              "shows how much evidence supports each result. Even records made during an "
              "election have unverified publication times, so these are not complete historical "
              "live replays.", '']
    status_rows = [
        [MEASURE_NAMES[r['family']], MODEL_NAMES[r['model']], STATUS_NAMES[r['observation_status']],
         TEST_NAMES[r['scheme']], r['elections'], r['cases'], r['aggregate_abs_pct'], r['case_abs_pct']]
        for r in diagnostics['input_history']]
    lines += table(['Measurement', 'Model', 'Test input history', 'Test method', 'Elections',
                    'Observations', 'Total error %', 'Observation error %'], status_rows)
    lines += ['', '## Training sample sizes and trial uncertainty intervals', '',
              'This check prevents a good average error from hiding predictions based on very '
              'little evidence. It also asks whether simple uncertainty intervals contain '
              'the final counts as often as intended.', '',
              '**Training elections: min / median / max** gives the smallest, middle and largest '
              'number of training elections used across the tests. **No training** counts tests '
              'with no multiplier and therefore no fitted '
              'prediction. **One training election** counts tests with a point prediction but '
              'insufficient evidence to estimate variation between elections.', '',
              '**Error / all seat votes %** divides summed absolute observation errors by all '
              'formal votes in the matched districts, then averages elections equally. Unlike '
              'category error, this describes the size of error relative to the whole seat '
              'count. **Interval coverage %** is the share of actual counts inside the trial '
              'intervals, averaged equally across elections with intervals. **Elections with '
              'intervals** is its supporting sample; tests without intervals do not contribute.', '',
              'The trial intervals aim for 80% coverage: about 80 out of 100 predictions should '
              'contain the final count if the assumptions are adequate. They use a bell-shaped '
              '(normal) approximation, variation between election ratios, uncertainty in their '
              'estimated mean, and extra district variation when district data exist. With only '
              'one training election, between-election variation cannot be estimated. Missing '
              'district variation also prevents a district interval. Coverage is an empirical '
              'check on a small sample, not a guarantee.', '']
    support = [
        [MEASURE_NAMES[r['family']], MODEL_NAMES[r['model']], TEST_NAMES[r['scheme']],
         '{} / {:g} / {}'.format(r['training_min'], r['training_median'], r['training_max']),
         r['no_training'], r['one_training'], r['seat_impact_pct'],
         100 * r['coverage80'] if r['coverage80'] is not None else None, r['interval_elections']]
        for r in diagnostics['training_support']]
    lines += table(['Measurement', 'Model', 'Test method', 'Training elections: min / median / max',
                    'No training', 'One training election', 'Error / all seat votes %',
                    'Interval coverage %', 'Elections with intervals'], support)
    lines += ['', '## Does publication rounding matter?', '',
              'This check asks whether counts reconstructed from rounded percentages could '
              'materially change the conclusions. If rounding is much smaller than prediction '
              'error, more elaborate rounding treatment would add little.', '',
              'Only exact counts and rates rounded to 0.1 percentage point or finer are admitted. '
              'For rounded inputs, the check moves training counts and test counts to their '
              'precision limits and conservatively adds the possible prediction changes. '
              'It is a sensitivity check, not a separate fitted model. Forecasts, one-sided '
              'bounds and vague approximations remain excluded.', '',
              '**Rounding / prediction error %** = 100 × summed possible rounding effects / '
              'summed absolute prediction errors, among affected observations using the single '
              'multiplier. **Median** and **Maximum** describe the possible change in one '
              'prediction in votes. An observation can be a large state total, so the maximum '
              'need not describe a typical district.', '']
    rounding_rows = [
        [TEST_NAMES[r['scheme']], r['effect_vs_error_pct'], r['median_effect_votes'], r['maximum_effect_votes']]
        for r in diagnostics['rounding']]
    lines += table(['Test method', 'Rounding / prediction error %', 'Median possible change (votes)',
                    'Maximum possible change (votes)'], rounding_rows)
    lines += ['', '## Largest district errors', '',
              'This check identifies failures concealed by averages or election-total errors. '
              'The five largest errors relative to all district formal votes are shown for '
              'each test method. Postal applications use separate federal/state multipliers; '
              'other measurements use the single multiplier.', '',
              "**Error votes** is prediction minus actual category count: positive means too "
              "many votes predicted, negative means too few. **Error / all district votes %** "
              "divides that signed error by the district's formal votes across all categories. "
              "These are observed errors, not fitted district adjustments.", '']
    worst = [
        [MEASURE_NAMES[r['family']], TEST_NAMES[r['scheme']], r['election_code'], r['seat_name'],
         r['error_votes'], r['error_all_votes_pct']]
        for r in diagnostics['largest_district_errors']]
    lines += table(['Measurement', 'Test method', 'Election', 'District', 'Error votes',
                    'Error / all district votes %'], worst)
    lines += ['', '## Shared conversion multipliers and error allowances', '',
              'This table describes coefficients for converting a reported count into a final '
              'category estimate and the evidence about their variation. All eligible elections '
              'contribute here. These full-sample coefficients are not used to predict an '
              'excluded election; those predictions use only their own training elections.', '',
              '**Election group** identifies the contributors to the mean multiplier. State '
              'elections are combined rather than fitting each state separately. **Elections** '
              'counts contributors. A one-election multiplier is descriptive only: it cannot '
              'establish transferability or variation between elections.', '',
              '- **Between-election spread** is the sample standard deviation of the election '
              'ratios, measuring how far they vary around their mean.',
              '- **Extra district spread** measures district-ratio deviations from their own '
              'election ratio. Squared deviations are weighted by operational counts and '
              'averaged within each election, then averaged equally across elections and '
              'square-rooted. This shared root mean square (RMS) measure gives larger deviations '
              'more weight. **Elections with district data** counts the contributing elections.',
              '- **Election error allowance** takes the largest of the between-election spread '
              'and the RMS election-total prediction error under each test method, expressed '
              'per operational count. This avoids choosing the most favourable check. It is '
              'derived from these errors, not independently tested as an uncertainty interval.', '',
              'Spreads and allowances use multiplier units: 0.01 corresponds to 100 votes per '
              '10,000 operational counts. They are not percentages of final votes or interval '
              'widths by themselves. Sparse federal/state training can use the combined '
              'multiplier fallback described above; the local JSON identifies those tests.', '']
    lines += table(['Measurement', 'Election group', 'Elections', 'Multiplier', 'Between-election spread',
                    'Election error allowance', 'Extra district spread', 'Elections with district data'], [
        [MEASURE_NAMES[p['family']], GROUP_NAMES[p['kind']], p['n_elections'], p['factor'],
         p['common_sd'], p['common_error_allowance'], p['local_rms'], p['local_elections']]
        for p in result['parameters']])
    lines += ['', '## Federal pre-poll counts as an indicator', '',
              'This investigation asks whether federal pre-poll counts help estimate final '
              'early votes despite being assigned to a different electorate in the operational '
              'data. Rejecting them solely because the definitions differ could discard useful '
              'information.', '',
              'The reported count covers ballots issued by early-voting centres administered '
              'under the named electorate, including ballots for voters enrolled elsewhere. '
              'The final target covers formal early votes belonging to residents of the '
              'named electorate, including those who voted elsewhere. We test the administering-'
              'electorate count as an indicator of the home-electorate target and the national '
              'sum, where movements between electorates cancel.', '',
              'The administering-electorate comparison is a diagnostic only. It is '
              'not used as a district input in the category allocation or initial '
              'count-distribution prototype, and its district errors do not set '
              'those models’ uncertainty. Those prototypes use the national '
              'early count, with district allocations and their uncertainty '
              'estimated from previous results for resident electors. This keeps '
              'useful national evidence while respecting the different district '
              'populations counted by issuing-centre and final-result records.', '',
              'A separate data limitation was that final federal summaries combine ordinary '
              'early and election-day votes. The supplement now recovers the ordinary early '
              'component from AEC polling-place first-preference files and the official '
              'polling-place classification. It sums formal ordinary votes at PrePollVotingCentre '
              'places (AEC type 5), excludes informal rows, then adds final declaration pre-poll '
              'formal votes once. Declaration pre-polls are early ballots requiring a separate '
              "check of the voter's entitlement before admission to the count. This target "
              'covers those two published early-vote components; it does not infer when votes '
              'in separate mobile or hospital categories were cast. Summing all polling-place '
              'ordinary formal votes reproduces '
              'the existing ordinary category for every imported district. This is a published-'
              'count reconstruction, not an estimated split. The AEC supplies '
              '[final polling-place CSVs and classifications]'
              '(https://results.aec.gov.au/31496/Website/HouseDownloadsMenu-31496-Csv.htm) and '
              'explains the [ordinary and declaration categories]'
              '(https://results.aec.gov.au/31496/Website/HouseVotesCountedByDivision-31496-NAT.htm).', '']
    if federal['available']:
        lines += ['### Counts and electorate association', '',
                  'This checks the national conversion and whether electorates with more reported '
                  'early voting also tend to have more final early votes. **Rate correlation** '
                  'is the Pearson linear correlation between issued ballots / enrolment and '
                  'final formal early votes / enrolment across electorates. Dividing by '
                  'enrolment avoids treating electorate size alone as evidence of a relationship. '
                  'Values range from −1 to +1; near +1 means a strong positive association. '
                  'This is distinct from the multiplier and does not establish accurate '
                  'electorate predictions.', '',
                  '**Used in tests** requires a retained count dated election eve or polling '
                  'day. The 2010 series stops on Thursday and is shown for context, but excluded '
                  'from estimating/testing a full-voting-period conversion. The remaining '
                  'five elections supply the prediction tests. Dates do not verify historical '
                  'publication availability: these selected district series were revised or '
                  'reconstructed afterward.', '']
        lines += table(['Election', 'Electorates', 'Count through', 'Reported issued', 'Final formal early',
                        'Multiplier', 'Rate correlation', 'Used in tests'], [
            [r['election_code'], r['districts'], r['cutoff'], r['issued'], r['final_early_formal'],
             r['multiplier'], r['rate_correlation'], 'Yes' if r['eligible'] else 'No']
            for r in federal['diagnostics']])
        lines += ['', '### Prediction errors', '',
                  'These tests assess predictive usefulness rather than relying on correlation '
                  'alone. They use the same excluded-election training, error definitions and '
                  'identical-observation comparisons as the main analysis. The single multiplier '
                  'here is learned only from eligible federal pre-poll comparisons. National '
                  'and electorate views use the same election ratios; they are not additional '
                  'independent training elections.', '',
                  'The federal/state model would equal the single multiplier in this federal-'
                  'only investigation and is omitted. Previous district counts match names '
                  'without adjusting boundaries; missing names reduce their shared sample. '
                  'With earlier-only training, the first eligible election has no previous '
                  'eligible conversion and no fitted prediction. The next has one training election.', '']
        for measure in ('federal_prepoll_national', 'federal_prepoll_district_proxy'):
            lines += ['#### ' + MEASURE_NAMES[measure], '']
            lines += paired_error_table([r for r in federal['paired_scores']
                                        if r['family'] == measure and r['model'] != 'kind_pooled']) + ['']
        lines += ['### What the federal results establish', '',
                  'These comparisons distinguish the benefit of using new counts from the '
                  'remaining errors in distributing those counts between electorates.', '']
        for scheme in SCHEMES:
            for measure in ('federal_prepoll_national', 'federal_prepoll_district_proxy'):
                pair = next((r for r in federal['paired_scores'] if (r['scheme'], r['family'], r['model'])
                             == (scheme, measure, 'previous_category')), None)
                if pair and pair['pooled']['elections']:
                    metric = 'aggregate_abs_pct' if measure.endswith('national') else 'case_abs_pct'
                    label = 'national total' if measure.endswith('national') else 'individual electorate counts'
                    lines.append('- {}: for {}, issued-ballot counts with a shared multiplier '
                                 'give {:.2f}% error, compared with {:.2f}% for the previous '
                                 'category count adjusted for enrolment, on {} elections and '
                                 '{} shared observations.'.format(TEST_NAMES[scheme], label,
                                     pair['pooled'][metric], pair['alternative'][metric],
                                     pair['pooled']['elections'], pair['pooled']['cases']))
        lines += ['', 'The last comparable multiplier is also competitive with, and on these '
                  'samples more accurate than, the mean of all training multipliers. National '
                  'ratios decline from 2013 to 2025; these results do not justify assuming '
                  'one timeless coefficient removes all election-to-election change.', '',
                  'Strong rate correlations coexist with large electorate exceptions. These '
                  'are the five largest errors relative to all electorate formal votes using '
                  'the single multiplier trained on other elections. Columns have the same '
                  'meaning as the main largest-error table.', '']
        selected = [r for r in federal['largest_district_errors'] if r['scheme'] == 'leave_one_out']
        lines += table(['Election', 'Electorate', 'Reported issued', 'Actual formal early',
                        'Predicted formal early', 'Error votes', 'Error / all district votes %'], [
            [r['election_code'], r['seat_name'], r['operational_count'], r['final_formal'],
             r['predicted_formal'], r['error_votes'], r['error_all_votes_pct']]
            for r in selected])
        lines += ['', "These counts are informative indicators, especially nationally. They do "
                  "not provide an exact count for residents of the administering electorate. "
                  "The outliers are consistent with geographic mismatch, but this analysis "
                  "does not attribute each error to a particular cause or estimate transfers "
                  "between centres. Federal tests remain separate from the main like-for-like "
                  "category conversions.", '']
    else:
        lines += ['The federal final-target supplement is absent. Generate it with the command '
                  'below to include these comparisons.', '']
    lines += ['## Exclusions and coverage', '',
              'These exclusions prevent counts for different voters, categories or processing '
              'stages from being treated as directly comparable. Exclusion from the main '
              'conversion analysis does not show that evidence has no predictive value.', '',
              'WA 2025 early voting has an unresolved early/absent split. Older SA declaration '
              'totals cannot supply separate early/postal targets. VIC 2022 uses the matched '
              '87-district sample, excluding Narracan. NSW is outside training by default '
              'because its optional preferential ballot rules differ. Federal pre-poll '
              'counts with different electorate definitions are assessed separately above '
              'without changing the strict matching in the main audit.', '',
              '**Controls** counts latest source observations rejected from the main selected '
              'comparison series. These include alternative sources for the same underlying '
              'count; they are not independent missing elections.', '']
    reasons = {
        'aggregate_only': "Administering district differs from voters' home district; requires aggregate comparison",
        'alternative_source_or_parent': 'Duplicate source series or parent total excluded in favour of the selected series',
        'combined_only': 'Target exists only inside a combined final category',
        'coverage_mismatch': 'Published total and available final districts cover different areas',
        'different_stage': 'Measurement stage has no supported final-category comparison',
        'missing_target': 'Required final category is missing or cannot be matched',
        'missing_formal_target': 'Matched category has no final formal count',
        'needs_definition_review': 'Category or geographic definition needs clarification',
        'needs_split': 'Final early votes remain inside combined ordinary votes in the main dataset',
    }
    lines += table(['Reason', 'Controls'], [[reasons.get(reason, reason.replace('_', ' ')), count]
                                           for reason, count in sorted(result['exclusions'].items())])
    lines += ['', '## Reproduction and source updates', '',
              'These commands reproduce the comparisons and recover the separate federal '
              'early-vote targets. Run from the analysis directory on native Windows:', '',
              '~~~powershell',
              '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_federal_prepoll',
              '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_operational_calibration --dry-run',
              '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_operational_calibration',
              '.\\.venv-win\\Scripts\\python.exe -B -m scripts.turnout.turnout_operational_calibration --check',
              '~~~', '',
              'The federal adapter reads official polling-place classifications and first-'
              'preference counts for all states and territories in 2010–2025. '
              'Data/Turnout/FederalPrepoll/final.json retains reconstructed category counts, '
              'source URLs/hashes and the normalized dataset version they accompany. Raw '
              'CSVs are cached under downloads/turnout/federal-prepoll/. Add --refresh to '
              'download current AEC files after a correction; changed files receive separate '
              'content-hash directories so earlier revisions remain available. Without '
              'that option, retained files are reused.', '',
              'After a source revision, refresh its normalized ingestion adapter, regenerate '
              'the federal supplement if its federal inputs changed, and rerun calibration. '
              'The generated local calibration.json retains observations, training election '
              'identities and coefficients for every prediction test, predictions, full-sample '
              'multipliers, source details and input/code fingerprints. A fingerprint is a '
              'content hash used to detect local changes. --check compares local evidence, '
              'code and options with the generated version; it does not poll remote sources. '
              'Add --include-nsw to include NSW in estimation and testing. An em dash means '
              'the quantity cannot be estimated from the available evidence.', '']
    return '\n'.join(lines)

