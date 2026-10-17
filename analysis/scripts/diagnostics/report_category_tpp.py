"""Write the category-relationship diagnostic tables and scientific figure."""

import json
import os
from pathlib import Path

from scripts.diagnostics.category_sources import CACHE, ROOT

os.environ.setdefault('MPLCONFIGDIR', str(CACHE / 'matplotlib'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

DESTINATION = ROOT / 'analysis' / 'reports' / 'live-category-tpp'
LABELS = {'early': 'All early votes', 'postal': 'Postal', 'absent': 'Absent',
          'provisional': 'Provisional', 'ppvc': 'Federal PPVC', 'early_declaration': 'Federal declaration early'}


def table(headers, rows):
    return '\n'.join(['| ' + ' | '.join(headers) + ' |',
        '| ' + ' | '.join('---' for _ in headers) + ' |'] +
        ['| ' + ' | '.join(map(str, row)) + ' |' for row in rows]) + '\n'


def plot_relationships(data):
    fig, axes = plt.subplots(1, 3, figsize=(15, 5.3), sharex=True, sharey=True)
    for axis, category in zip(axes, ('early', 'postal', 'absent')):
        rows = [r for r in data['comparisons'] if r['category'] == category]
        for row in rows:
            federal = row['election'].endswith('fed')
            axis.scatter(row['ordinary_error'], row['contrast_change'],
                         color='#17699c' if federal else '#b35b19', marker='o' if federal else '^')
            # Every election is plotted. Omit labels in the dense central
            # cluster; the accompanying table identifies every observation.
            if abs(row['contrast_change']) < 1.5 and row['election'] not in (
                    '2022fed', '2022vic', '2023nsw', '2024qld', '2025fed'):
                continue
            offset = (3, 5)
            align = 'left'
            if row['election'] == '2025fed':
                offset, align = (-4, 5), 'right'
            axis.annotate(row['election'], (row['ordinary_error'], row['contrast_change']),
                          xytext=offset, textcoords='offset points', fontsize=7, ha=align)
        summary = data['summaries'][category]
        x = np.array([-3, 5.8])
        axis.plot(x, summary['intercept'] + summary['contrast_slope']*x, color='#555555', lw=1)
        axis.axhline(0, color='#bbbbbb', ls='--', lw=1)
        axis.axvline(0, color='#dddddd', lw=1)
        axis.set_title(LABELS[category])
        axis.set_xlabel('Ordinary-vote forecast error\n(transformed points; positive = more Labor)')
        axis.set_xlim(-3, 5.8)
        axis.set_ylim(-6, 6.5)
        axis.grid(alpha=.15)
    axes[0].set_ylabel('Change in category lean relative to ordinary\n(transformed points; positive = more Labor)')
    fig.suptitle('Category departures remain substantial after ordinary votes are known', fontsize=14)
    fig.text(.5, .005, '14 election comparisons. Blue circles: federal; orange triangles: state. '
             'Lines are descriptive fits, not deployed model parameters.', ha='center', fontsize=10)
    fig.tight_layout(rect=(0, .04, 1, .95))
    fig.savefig(DESTINATION / 'category-relationships.png', dpi=160)
    plt.close(fig)


def report_text(data, normalized):
    text = '''# Historical relationships between vote categories

This analysis asks how an ordinary-vote surprise should affect expectations for
early, postal and absent votes. It uses available Australian election counts,
the previous election's category leans, and saved trend-adjusted central TPP
forecasts. The practical purpose is to inform the prior covariance of the live
TPP model without mistaking a district's category mix for evidence about its
uncounted votes. These exploratory fits do not directly supply forecast parameters.
The subsequent [implemented composition model](../../../docs/live-tpp-composition.md)
uses a separate regularised calibration that preserves the category distinctions.

**Audit qualification:** the [election-by-election audit](election-audit.md)
identifies a NSW 2019-to-2023 iVote/postal comparability problem and shows that
the category errors below do not reconcile under changed category fractions.
These original fits are retained for comparison, not recommended calibration
coefficients. Read the audit before interpreting the election rows or slopes.
The [declaration pre-poll analysis](declaration-prepoll.md) also separates the
broad early series used below: ordinary PPVC and declaration pre-poll behaviour
must not be assumed identical or substituted for their distinct live categories.

The usable comparison sample contains 14 successive-election pairs: seven
federal, four Victorian, two NSW and one Queensland. Elections were selected
by data availability and comparable definitions, not by the direction of their
category differences. Western Australian sources were also checked, but do not
provide a usable consecutive pair here. SA is excluded because its historical
batching does not establish the required category split.

## What the measurements mean

All shares are Labor votes divided by Labor plus Coalition votes in the
published preference count. In NSW, exhausted optional-preferential ballots
are outside that denominator. Non-classic TCP counts are excluded unless a
separate Labor-Coalition TPP count is available.

Calculations use `Z(p) = 25 log(p / (100-p))`. Near 50%, one transformed point
is approximately one percentage point; they are not identical away from 50%.
A half-vote in each side prevents infinite values in very small categories.
Zero total counts are unavailable observations, not measured 50:50 results.

A category's **lean** is its transformed TPP minus the transformed overall TPP.
Its expected lean is the previous election's lean. The **ordinary error** is
the election-wide forecast error plus the change in the ordinary category's
lean. A category error is defined the same way. The **contrast change** is
category error minus ordinary error: it measures the part an ordinary-vote
update cannot simply carry over.

For example, an ordinary error of +2 and postal error of +3 imply a postal
contrast change of +1. A response coefficient of 0.8 means a +2 ordinary error
predicts +1.6 in the other category, before any fitted intercept. A 60% reversal
of the ordinary movement means a response of 0.4, not -0.6.

Each comparison uses the same named districts in both elections, allowing
punctuation differences but not assuming renamed districts are unchanged.
Category leans are centred on the overall TPP of that same district sample;
the actual election-wide forecast error is then added. This avoids calling a
change in coverage of non-classic seats a national forecast error. It assumes
the sampled districts' category-lean changes are informative about the whole
election. It does not remove redistribution or demographic-composition effects.

The forecast centre applies the saved day-zero TPP adjustment to the final
raw-trend median, using the saved fundamentals and adjustment file, exactly as
the existing Python trend diagnostic does. This is a median-level central
estimate; it does not recreate the GUI's stochastic mixture of FP-first and
TPP-first adjusted samples. Input paths and both medians are in `summary.json`.
These are retrospective diagnostics, not a claim of strict historical forecasts.

## Election-wide response and remaining variation

The response is an equal-election-weight regression of category error on
ordinary error with an intercept. The departure SD is the sample standard
deviation of contrast changes across elections; it is a shared election-level
quantity, not the standard error of a seat mean. Intervals resample whole
elections 4,000 times. Small samples, particularly the five federal-only
subtype comparisons, do not support precise coefficients.
The bootstrap ranges are descriptive: adjacent comparisons share an election,
and repeated elections within a jurisdiction may also share lasting patterns.
They should not be read as rigorously calibrated 95% coverage guarantees.

'''
    rows = []
    for c, s in data['summaries'].items():
        interval = data['bootstrap_slope_intervals'][c]
        rows.append([LABELS[c], s['elections'], f"{s['intercept']:+.2f}", f"{s['slope']:.2f}",
                     f'{interval[0]:.2f} to {interval[1]:.2f}', f"{s['contrast_sd']:.2f}",
                     f"{s['local_change_rms']:.2f}"])
    text += table(['Category', 'Elections', 'Intercept', 'Response', 'Bootstrap 95% response range',
                   'Election departure SD', 'Within-election district RMS'], rows)
    text += '''
The district RMS is the category-versus-ordinary change after subtracting that
election's category-vote-weighted district mean, averaged across elections in
squared units. It includes geography, redistribution, small-count noise and
measurement error. It is not an isolated estimate of the live model's local
sampling uncertainty. Districts are not treated as independent national shocks.

![Election-wide category departures](category-relationships.png)

## Do fitted responses improve predictions

For the fitted rule, each election is predicted using coefficients learned
from the other elections. The fixed carry rule predicts category error equal
to ordinary error; the fixed reversion rule predicts 0.4 times ordinary error;
the zero rule leaves the category at its pre-count expected value. Numbers are
root mean squared prediction errors in transformed points; smaller is better.
Fixed rules require no fitted parameters. This comparison tests mean prediction,
not calibration of probability tails or interval coverage.

'''
    text += table(['Category', 'Carry ordinary', '60% reversion', 'No ordinary update', 'Fitted response'],
        [[LABELS[c]]+[f"{s['predictive_rmse'][m]:.2f}" for m in
          ('carry_ordinary','sixty_percent_reversion','independent_category','fitted')]
         for c,s in data['summaries'].items()])
    text += '''
The fitted responses do not consistently outperform the carry rule. This
supports retaining a common movement with uncertain category departures more
strongly than it supports deploying the fitted slope estimates as exact rules.
The present sample does not establish a universal 60% reversion.

## Correlations and sensitivity

These correlations concern changes in each category's lean relative to
ordinary votes, not raw category shares. They use the same 14 election pairs.
Shared ordinary measurement error and real common category movement can both
contribute. The estimates are descriptive and should not be copied into a
large covariance matrix without regularisation.

'''
    covariance = data['contrast_covariance']
    text += table(['Category', 'Early', 'Postal', 'Absent'],
        [[LABELS[c]]+[f'{v:.2f}' for v in covariance['correlation'][i]]
         for i,c in enumerate(covariance['categories'])])
    text += '''
The early-postal correlation is positive; absent-vote departures are weaker
and of the opposite sign in this sample. Treating all categories as independent
would lose a material part of the uncertainty when early and postal votes are
both uncounted.

The following checks show how much the sample matters. “Examples omitted”
removes VIC 2022, NSW 2023, QLD 2024 and federal 2022 only as a sensitivity
check; all remain in the main estimate. SD measures election-level departure
variation, not uncertainty in the fitted slope.

'''
    text += table(['Category', 'Subset', 'Elections', 'Response', 'Departure SD'],
        [[LABELS[c], name, s['elections'], f"{s['slope']:.2f}", f"{s['contrast_sd']:.2f}"]
         for c in ('early','postal','absent')
         for key,name in [('federal_only','Federal only'),('state_only','States only'),
                          ('without_examples','Examples omitted')]
         for s in [data['summaries'][c]['sensitivities'][key]]])
    text += '''
State-election category departures are more variable here than federal ones.
Removing the highlighted recent elections reduces that variation appreciably,
especially for postals. They therefore matter for the risk estimate even though
they are not a sound basis for selecting the sample.

## Every included election comparison

This table exposes the whole sample, including cases that disagree with the
motivating examples. “Ordinary error” is relative to the adjusted forecast and
inherited ordinary lean. The other columns are category-versus-ordinary
contrast changes. Positive means an additional movement towards Labor.
All values are transformed points. N is the common district count.

'''
    by_election = {}
    for row in data['comparisons']:
        by_election.setdefault(row['election'], {})[row['category']] = row
    text += table(['Election', 'Previous', 'N', 'Ordinary error', 'Early departure', 'Postal departure', 'Absent departure'],
        [[code, rows['early']['previous'], rows['early']['seats'], f"{rows['early']['ordinary_error']:+.2f}"]+
         [f"{rows[c]['contrast_change']:+.2f}" for c in ('early','postal','absent')]
         for code,rows in by_election.items() if all(c in rows for c in ('early','postal','absent'))])
    text += '''
These are completed category counts. They do not measure the separate error
from which ordinary booths happened to report first. In particular, the final
VIC 2022 ordinary-vote error in this definition is slightly negative; this does
not contradict an early election-night estimate being too favourable to Labor.
The table distinguishes a category's absolute preference share, its inherited
lean, and the change from that lean.

## Coverage and source checks

The normalized file contains one record per election, district and category,
with integer Labor and Coalition counts and the source URL. Cached source
responses have URL and SHA-256 manifests. AEC booth sums must exactly match
ordinary accounts; VEC/NSW category sums must match their published preference
totals; ECQ final distribution booth counts must match district totals.

VEC and WA may retain indicative 2CP rather than final distributed counts.
Accounts differing from final FP totals by more than max(20 votes, 2%) are
excluded, as are known unavailable VEC category partitions. Small mis-sort
differences remain a source of measurement noise. This is an analysis-quality
screen, not a change to live-feed recovery. The FP comparison is not applied
to optional-preferential NSW, where exhaustion is legitimate.

'''
    text += table(['Election', 'Districts retained', 'Excluded records or districts'],
        [[code, c['seats'], len(c['exclusions'])] for code,c in normalized['coverage'].items()])
    text += '''
Coverage notes:

- Federal 2004–2013 declaration data are available here only where TCP is
  Labor–Coalition. From 2016 the separate TPP category download supplies all
  divisions. Polling-place type 5 identifies PPVCs; mobile types remain separate.
  The main early series combines in-person and declaration early votes across
  the 2010 classification change. PPVC/declaration-only regressions start with
  the 2010-to-2013 comparison.
- VEC includes 2006–2022 and excludes the delayed Narracan 2023 election from
  the 2022 event. Only comparable Labor–Coalition category counts are used.
  New and nonmatching district names reduce matched-sample coverage.
- NSW's preference-analysis tables permit Labor–Coalition comparisons even
  where the actual final TCP pairing differs. Two 2023 districts have multiple
  Coalition options and are omitted rather than selecting a pairing arbitrarily.
  Historical NSW `CLP` means Country Labor, not the NT Country Liberal Party.
- ECQ 2020 and 2024 final XML includes booth-level distributions. The 2006,
  2009, 2012, 2015 and 2017 XML archives contain FP only. Final preference PDFs
  exist for older elections, but the ECQ website returned HTTP 403 to direct
  downloads, including a standard browser user agent. Those election pairs
  remain a documented acquisition gap, not evidence of no relationship.
- WA's accessible final XML archive begins in 2017. Most 2021 indicative TCP
  accounts are zero or incomplete: only one district passes the coverage screen.
  Neither 2017-to-2021 nor 2021-to-2025 supplies a usable election-wide comparison.
  The separate detailed CSV inspected contains primary votes. Older retained
  turnout tables also lack party preference counts. Published final preference
  tables would be needed to extend this jurisdiction.
- SA is excluded from fitting because the historical batched categories do not
  establish a comparable early/declaration split. No invented zeros or substitute
  categories are used to fill missing history.

Source entry points:

- [AEC final TPP by category](https://results.aec.gov.au/27966/website/HouseTppByDivisionByVoteType-27966.htm)
  and [all federal downloads](https://results.aec.gov.au/27966/Website/HouseDownloadsMenu-27966-csv.htm).
- [VEC 2022 district results](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district)
  and [2018 historical results](https://itsitecoreblobvecprd01.blob.core.windows.net/public-files/historical-results/state2018/summary.html).
- [NSW 2023 results and preference tools](https://pastvtr.elections.nsw.gov.au/SG2301/LA/results).
- [ECQ 2024 final XML](https://resultsdata.elections.qld.gov.au/XMLData/publicResults_SGE2024_ICCDiv4_Final.zip)
  and [historical statistical reports](https://www.ecq.qld.gov.au/about-us/publications).
- [WAEC archive](http://media.waec.wa.gov.au/Archive/).

## Reproduction

Run from `analysis/` using the native Windows virtual environment. Collection
is a read-only request to public commission sources; normalization and analysis
then use cached bytes. The collector retains successful downloads across runs.
The optional figure uses Matplotlib 3.9.4 and contourpy 1.3.1 with the existing
native environment's NumPy 1.26.4. Analysis itself needs no plotting package.

```powershell
.\\.venv-win\\Scripts\\python.exe -B -m pip install -r scripts/diagnostics/requirements-category-plot.txt
.\\.venv-win\\Scripts\\python.exe -B -m scripts.diagnostics.collect_category_sources --download
.\\.venv-win\\Scripts\\python.exe -B -m scripts.diagnostics.category_tpp_history
.\\.venv-win\\Scripts\\python.exe -B -m scripts.diagnostics.category_tpp_relationships
.\\.venv-win\\Scripts\\python.exe -B -m scripts.diagnostics.report_category_tpp
.\\.venv-win\\Scripts\\python.exe -B -m unittest discover -s tests -p test_category_tpp_relationships.py
```

Downloaded bytes and full district-level diagnostics are private local files
under `downloads/category-tpp-research/`. This report and `summary.json` contain
the consolidated findings. No Stan fitting or application build is required.
'''
    return text


def main():
    data = json.loads((CACHE / 'relationships.json').read_text())
    normalized = json.loads((CACHE / 'normalized.json').read_text())
    DESTINATION.mkdir(parents=True, exist_ok=True)
    plot_relationships(data)
    (DESTINATION / 'README.md').write_text(report_text(data, normalized), encoding='utf-8')
    # Keep the public result compact. Detailed per-district records stay in the
    # private reproducibility cache and are unnecessary for reading the report.
    for row in data['comparisons']:
        row.pop('local', None)
    data['coverage'] = normalized['coverage']
    (DESTINATION / 'summary.json').write_text(json.dumps(data, indent=2), encoding='utf-8')
    print(DESTINATION)


if __name__ == '__main__':
    main()
