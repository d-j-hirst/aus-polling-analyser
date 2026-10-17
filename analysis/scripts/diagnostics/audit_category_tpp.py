"""Reconcile historical category comparisons to their election-wide totals.

This diagnostic separates a change in a category's behaviour from a change
in how many people use it. It does not change the live forecast or fit models.
Run from analysis with python -B -m scripts.diagnostics.audit_category_tpp.
"""

import json
from collections import defaultdict

import numpy as np

from scripts.diagnostics.category_sources import CACHE, ROOT
from scripts.diagnostics.category_tpp_relationships import (
    aggregate, category_count, final_results, seat_accounts, unz, z,
)

DESTINATION = ROOT / 'analysis' / 'reports' / 'live-category-tpp'
ELECTIONS = ('2025fed', '2024qld', '2023nsw', '2022vic', '2022fed', '2019fed', '2018vic')
NSW_SEATS = ('Ryde', 'Terrigal', 'Goulburn', 'Miranda', 'Holsworthy', 'Oatley')


def share(count):
    return float(100 * count[0] / sum(count))


def category_partition(account, merge_remote=False):
    """Keep every received vote once, with a documented NSW sensitivity.

    Usually categories retain their published meaning. For the 2019-to-2023
    NSW comparison we also examine postals plus iVote as one remote channel:
    iVote disappeared in 2023. This is a comparability adjustment, not a claim
    that every former iVote user became a postal voter.
    """
    result = defaultdict(lambda: np.zeros(2))
    for category, count in account.items():
        if category in ('early_declaration', 'declaration_early'):
            category = 'early'
        if merge_remote and category == 'ivote':
            category = 'postal'
        if category not in ('ordinary', 'early', 'postal', 'absent', 'ivote'):
            category = 'other'
        result[category] += count
    return dict(result)


def common_accounts(previous, current, merge_remote):
    # Fix the named district sample for all categories in a comparison. This
    # is still not redistribution-matched geography, which remains a caveat.
    seats = sorted(s for s in previous.keys() & current.keys()
                   if all(has_main_categories(a[s]) for a in (previous, current)))
    partitions = [{s: category_partition(a[s], merge_remote) for s in seats}
                  for a in (previous, current)]
    return seats, partitions


def has_main_categories(account):
    counts = [category_count(account, c) for c in ('ordinary', 'early', 'postal')]
    return all(count is not None and sum(count) > 0 for count in counts)


def centre_at_total(previous_shares, weights, target):
    """Find one common log-odds shift that reproduces the specified total.

    The current category fractions are held fixed. A percentage-point shift
    could put a very partisan category outside 0--100%; log odds cannot.
    """
    lower, upper = -1000., 1000.
    for _ in range(100):
        middle = (lower + upper) / 2
        value = sum(weights[c] * unz(z(p) + middle) for c, p in previous_shares.items())
        if value < target:
            lower = middle
        else:
            upper = middle
    shift = (lower + upper) / 2
    return {c: unz(z(p) + shift) for c, p in previous_shares.items()}


def audit_election(previous, current, forecast, actual, merge_remote=False):
    seats, partitions = common_accounts(previous, current, merge_remote)
    totals = [share(aggregate(a, seats)) for a in partitions]
    present = set(c for a in partitions for s in seats for c in a[s])
    categories = [c for c in ('ordinary', 'early', 'postal', 'absent', 'ivote', 'other') if c in present]
    counts = [{c: aggregate(a, seats, c) for c in categories} for a in partitions]
    # aggregate's 'early' operation is safe here: the partition has already
    # combined early subtypes and no subtype keys remain to count twice.
    sizes = [{c: float(sum(v)) for c, v in row.items()} for row in counts]
    weights = [{c: n / sum(row.values()) for c, n in row.items()} for row in sizes]
    shares = [{c: share(v) for c, v in row.items() if sum(v)} for row in counts]
    if any(c not in shares[0] for c in shares[1]):
        raise ValueError('A current category has no previous share')

    # Preserve the original diagnostic's sample-to-election offset, but make
    # the expected category shares sum to that expected sample total exactly.
    sample_forecast = unz(z(totals[1]) + z(forecast) - z(actual))
    expected = centre_at_total(shares[0], weights[1], sample_forecast)
    old_expected = {c: unz(z(p) - z(totals[0]) + z(sample_forecast))
                    for c, p in shares[0].items()}
    ordinary_shift = z(shares[1]['ordinary']) - z(shares[0]['ordinary'])
    ordinary_update = {c: unz(z(p) + ordinary_shift) for c, p in shares[0].items()}
    categories_output = {}
    for c in shares[1]:
        error = shares[1][c] - expected[c]
        contrast = (z(shares[1][c]) - z(shares[1]['ordinary'])
                    - z(shares[0][c]) + z(shares[0]['ordinary']))
        categories_output[c] = dict(previous_share=shares[0][c], current_share=shares[1][c],
            previous_fraction=weights[0][c], current_fraction=weights[1][c],
            expected_share=expected[c], error_pp=error,
            contribution_pp=weights[1][c] * error, contrast_change=contrast)
    return dict(seats=len(seats), previous_sample_tpp=totals[0], current_sample_tpp=totals[1],
        sample_forecast=sample_forecast, forecast=forecast, actual=actual,
        old_expected_weighted_total=sum(weights[1][c]*p for c, p in old_expected.items()),
        expected_weighted_total=sum(weights[1][c]*p for c, p in expected.items()),
        reconstructed_error=sum(r['contribution_pp'] for r in categories_output.values()),
        ordinary_based_total=sum(weights[1][c]*p for c, p in ordinary_update.items()),
        categories=categories_output)


def nsw_seat_audit(accounts):
    result = {}
    for seat in NSW_SEATS:
        old, new = [accounts[code][seat] for code in ('2019nsw', '2023nsw')]
        old_remote = old['postal'] + old.get('ivote', np.zeros(2))
        result[seat] = dict(ordinary=share(new['ordinary']), early=share(new['early']),
            postal=share(new['postal']), total=share(sum(new.values())),
            previous_ordinary=share(old['ordinary']), previous_early=share(old['early']),
            previous_postal=share(old['postal']), previous_postal_ivote=share(old_remote),
            remote_swing_minus_ordinary_pp=(share(new['postal'])-share(old_remote)
                                           -share(new['ordinary'])+share(old['ordinary'])))
    return result


def build_audit(normalized, relationships):
    accounts = seat_accounts(normalized['records'])
    actual = final_results()
    results = {}
    for code in ELECTIONS:
        old = next(r['previous'] for r in relationships['comparisons'] if r['election'] == code)
        forecast = relationships['forecasts'][code]
        result = audit_election(accounts[old], accounts[code], forecast['adjusted'], actual[code],
                                merge_remote=code == '2023nsw')
        result.update(previous=old, raw_trend=forecast['raw'])
        if code == '2023nsw':
            result['postal_without_ivote'] = audit_election(
                accounts[old], accounts[code], forecast['adjusted'], actual[code])['categories']['postal']
        # The central trend and GUI sample median are different quantities.
        # Repeat the accounting with the raw trend for an explicit sensitivity.
        result['raw_trend_ordinary_error_pp'] = audit_election(
            accounts[old], accounts[code], forecast['raw'], actual[code],
            merge_remote=code == '2023nsw')['categories']['ordinary']['error_pp']
        results[code] = result
    return dict(elections=results, nsw_seats=nsw_seat_audit(accounts))


def table(headers, rows):
    return '\n'.join(['| ' + ' | '.join(headers) + ' |',
        '| ' + ' | '.join('---' for _ in headers) + ' |'] +
        ['| ' + ' | '.join(map(str, row)) + ' |' for row in rows]) + '\n'


def report(audit):
    text = '''# Audit of historical category comparisons

This audit checks why completed category counts sometimes appear inconsistent
with election-night experience. It separates category behaviour, category
sizes, historical forecast centres and the order in which votes reported.
The earlier relationship report remains an exploratory first pass; its fitted
coefficients should not be used for calibration before these issues are resolved.
No production forecast code or election inputs are changed.

## What needed correction

The earlier category errors carry previous category leans forward but do not
re-centre them for current category fractions. They therefore do not combine
to the election-wide forecast error. This audit adds a common log-odds shift
to the previous category shares until their current-vote-weighted mean equals
the expected total. Category errors then reconcile exactly in percentage points.
This uses final category fractions as a diagnostic, not as election-night
information. A live model would have to allow uncertainty in those fractions.

NSW 2023 also needs a comparability adjustment: iVote disappeared. We show
2019 postals plus iVote as the previous remote-voting category, compared with
2023 postals. This is a sensitivity with a strong procedural basis, not proof
that all former iVote users voted postal. Comparing postals alone changes the
composition of that population and gives the wrong impression of its behaviour.
[Antony Green's first-hand analysis](https://antonygreen.com.au/why-nsw-labors-election-night-majority-disappeared/)
identifies the same problem.

Each row below uses the same named districts across both elections and all
categories. These are not redistribution-adjusted districts. State samples
exclude unavailable non-classic preference pairs. The expected sample total
preserves the original analysis's transformed difference between the sample
result and the election-wide result. Thus sample levels are not statewide TPP.
Counts are from the commission source tables used by the original diagnostic;
indicative preference tables can differ slightly from final recounts.

## Forecast centres and accounting

All numbers in this table are Labor TPP percentages. The adjusted centre is
the saved median-level Python reconstruction, not the original GUI forecast.
The raw trend is the last saved unadjusted TPP median. Historical adjustment
files can be retrospective; neither column proves what was forecast live.
The result is the saved eventual-results value used in the original study.

'''
    text += table(['Election', 'Districts', 'Raw trend', 'Adjusted centre', 'Result',
                   'Sample forecast', 'Old weighted expectation', 'Re-centred ordinary error (pp)'],
        [[code, r['seats'], f"{r['raw_trend']:.2f}", f"{r['forecast']:.2f}", f"{r['actual']:.2f}",
          f"{r['sample_forecast']:.2f}", f"{r['old_expected_weighted_total']:.2f}",
          f"{r['categories']['ordinary']['error_pp']:+.2f}"] for code, r in audit['elections'].items()])
    text += '''
The old weighted expectation should have equalled the sample forecast but did
not. The difference measures the missing category-size re-centring, not a new
polling error. The re-centred ordinary error is actual minus expected ordinary
TPP, in percentage points; earlier reported errors used transformed points.

## Election-by-election counts

Each category row gives its previous and current Labor TPP, its fraction of
the current two-party count, and its error against the re-centred expectation.
Contribution is that error multiplied by the category fraction. Contributions
sum to actual minus expected sample TPP. Other includes small residual categories
such as provisional and mobile votes; every normalized vote is included once.
The contrast change is the change in category-minus-ordinary log odds, measured
in transformed points. It is not the category's contribution to the final result.

'''
    for code, r in audit['elections'].items():
        text += f"### {code}, compared with {r['previous']}\n\n"
        text += table(['Category', 'Previous TPP', 'Current TPP', 'Current fraction (%)',
                       'Expected TPP', 'Error (pp)', 'Contribution (pp)', 'Contrast change'],
            [[c, f"{v['previous_share']:.2f}", f"{v['current_share']:.2f}",
              f"{100*v['current_fraction']:.2f}", f"{v['expected_share']:.2f}",
              f"{v['error_pp']:+.2f}", f"{v['contribution_pp']:+.2f}",
              f"{v['contrast_change']:+.2f}"] for c, v in r['categories'].items()])
        text += (f"\nSample result **{r['current_sample_tpp']:.2f}%**, expected "
                 f"**{r['sample_forecast']:.2f}%**; contributions sum to "
                 f"**{r['reconstructed_error']:+.2f} pp**. Carrying the completed ordinary "
                 f"swing into all previous category shares, with current category fractions, "
                 f"would give **{r['ordinary_based_total']:.2f}%**. This is a completed-count "
                 "counterfactual, not a reconstruction of the election-night projection.\n\n")
    text += '''## NSW marginal seats

All shares below use published 2023 preference counts, including the NSWEC's
[Ryde table](https://pastvtr.elections.nsw.gov.au/SG2301/LA/ryde/TCP/lane-jordan/howison-lyndal/tcp_table.html)
and [Terrigal table](https://pastvtr.elections.nsw.gov.au/SG2301/LA/terrigal/TCP/crouch-adam/boughton-sam/tcp_table.html).

'''
    text += table(['Seat', 'Ordinary TPP', 'Early TPP', 'Postal TPP', 'Total TPP',
                   'Postal swing minus ordinary swing (pp; 2019 postals + iVote)'],
        [[seat] + [f"{r[k]:.2f}" for k in ('ordinary', 'early', 'postal', 'total')]
         + [f"{r['remote_swing_minus_ordinary_pp']:+.2f}"] for seat, r in audit['nsw_seats'].items()])
    text += '''
These are same-name district comparisons, not redistributed matched-booth
swings. The direction is informative, but boundary changes particularly limit
Holsworthy and Oatley. Kiama's incumbent changed from Liberal to independent,
so it is not represented by this Labor-Coalition diagnostic.

## Interpretation and limitations

- NSW's postal contrast changes from +3.42 to -3.43 transformed points when
  2019 iVote is included. The early contrast remains -1.06. This supports the
  observed adverse late-count pattern rather than contradicting it.
- Victoria 2022's large positive departures are a narrowing of the exceptionally
  adverse 2018 early/postal gap. Early and postal TPP still lie below ordinary
  TPP in 2022. A five-point postal departure is not a five-point statewide gain:
  postal votes are only about eleven percent of the sample. The published
  broader [VIC category comparison](https://antonygreen.com.au/vic22-results-by-vote-type-and-vote-type-by-electorate/)
  shows the same pattern (ordinary swing -3.5 pp, early -1.2, postal +1.3).
- Federal 2022's saved adjusted centre is 52.12%, effectively the 52.13% result;
  its raw trend is 52.66%. Category-size re-centring makes the ordinary error
  more negative and resolves the apparent failure of positive early/postal
  departures to add up. The old -0.33 was not a complete accounting residual.
- Federal 2019 uses 50.73% as its adjusted centre, not 51.5%. This explains much
  of the smaller measured error. The raw trend is 51.18%; a 51.5% benchmark would
  imply a 3.03-point total miss. These expectations must not be interchanged.
- Victoria 2018's statewide category means mask some district differences.
  The original current-category-weighted mean of within-district transformed
  contrast changes is early -2.42 and postal -2.30, compared with aggregated
  -1.71 and -0.87. This is a composition sensitivity, not a replacement estimate
  on redistributed geography. Completed ordinary counts also conceal the order
  in which particularly strong Melbourne booths reported.

Further calibration needs comparable category definitions, category fractions,
and explicit treatment of geography. A final ordinary category is not the
partially counted ordinary vote at 19:00, nor the mix of ordinary and pre-poll
votes counted by midnight. These data alone cannot identify an intranight peak.

## Reproduction

From `analysis/`, after producing the original cached diagnostic:

```powershell
.\\.venv-win\\Scripts\\python.exe -B -m scripts.diagnostics.audit_category_tpp
.\\.venv-win\\Scripts\\python.exe -B -m unittest discover -s tests -p test_category_tpp_relationships.py
```

`election-audit.json` retains the exact values underlying this report. Original
commission URL provenance remains in the normalized cache and the original
report's source inventory. No downloads or forecast runs are performed here.
'''
    return text


def main():
    normalized = json.loads((CACHE / 'normalized.json').read_text(encoding='utf-8'))
    relationships = json.loads((CACHE / 'relationships.json').read_text(encoding='utf-8'))
    audit = build_audit(normalized, relationships)
    DESTINATION.mkdir(parents=True, exist_ok=True)
    (DESTINATION / 'election-audit.json').write_text(json.dumps(audit, indent=2), encoding='utf-8')
    (DESTINATION / 'election-audit.md').write_text(report(audit), encoding='utf-8')
    for code, row in audit['elections'].items():
        print(code, 'ordinary error', round(row['categories']['ordinary']['error_pp'], 3),
              'sample forecast / old expectation', round(row['sample_forecast'], 3),
              round(row['old_expected_weighted_total'], 3), 'sum', round(row['reconstructed_error'], 3))


if __name__ == '__main__':
    main()
