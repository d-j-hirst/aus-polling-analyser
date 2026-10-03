# Normalized Turnout Evidence

These datasets make published election turnout evidence available for
historical comparisons and analysis of vote counts.

The JSON files store enrolment, final votes by district and voting category,
and dated early-voting or postal counts. Each file retains its source details
and is checked against the published totals during import.

Regenerate the normalized files from the commissions' official material with:

```bash
cd analysis
./env/bin/python -B -m scripts.turnout.turnout_aec --election all
./env/bin/python -B -m scripts.turnout.turnout_aec_operational --election all
./env/bin/python -B -m scripts.turnout.turnout_federal_prepoll

./env/bin/python -B -m scripts.turnout.turnout_nsw_operational --election 2015nsw
./env/bin/python -B -m scripts.turnout.turnout_nsw --election all
./env/bin/python -B -m scripts.turnout.turnout_vic --election all
./env/bin/python -B -m scripts.turnout.turnout_qld --election all
./env/bin/python -B -m scripts.turnout.turnout_qld_operational --election all
./env/bin/python -B -m scripts.turnout.turnout_wa --election all
./env/bin/python -B -m scripts.turnout.turnout_sa --election all
./env/bin/python -B -m scripts.turnout.turnout_sa_operational --election 2026sa
./env/bin/python -B -m scripts.turnout.turnout_sa_final --election 2026sa
./env/bin/python -B -m scripts.turnout.turnout_published_operational --election all
```

Each adapter validates its official inputs before replacing an election file.
The operational AEC adapter merges daily pre-poll and postal evidence into the
already normalized federal files without replacing their final-result records.
Pre-poll geography means the administering division; postal geography means
the elector's enrolled division. Later consolidated series and contemporaneous
snapshots are labelled separately.
AEC ordinary votes are classified as election-day votes before 2010 and as a
combined election-day/early category from 2010. NSW 2015 retains separate
enrolment and provisional categories; later combined categories remain
combined, and no inferred split is stored.

The separate `FederalPrepoll/final.json` supplement recovers final federal early
formal counts for 2010–2025 from official polling-place classifications and
first-preference results. It adds formal ordinary votes at pre-poll centres
(AEC type 5) to final declaration pre-poll formal votes once, excluding informal
rows. All polling-place ordinary votes must reconcile to each district's
existing ordinary category. This does not alter the original category
partition. The supplement records source URLs and hashes; add `--refresh` to
its command to fetch current AEC files while retaining prior raw revisions.
Operational pre-poll counts still describe administering divisions, so the
calibration report assesses them separately as indicators of resident votes.
VEC early votes remain combined and its small Marked As Voted category remains
separate from 2010 onward; the 2006 Declaration Votes aggregate remains
combined. VEC did not publish voting-centre figures for the 2014 Prahran and
2018 Ripon recounts. VEC's linked 2018 Brunswick voting-centre page contains
stale totals inconsistent with its district and statewide summaries. Final
district totals are retained for all three, but their unreliable or unavailable
vote-type partitions are deliberately absent.
Queensland files use ECQ's final XML archives from 2006 through 2024. ECQ booth
type codes supply the main categories; explicit pre-poll, telephone,
electronically assisted and mobile venue labels refine otherwise generic
historical polling-booth records. Each resulting partition reconciles exactly
to the ECQ district total. The 2024 file also includes ECQ's final-reconciled
daily early-voting mark-offs by administering district and postal ballots
issued, returned and accepted by elector district. The separately published
state postal totals are retained because they include a small unallocated
residual absent from the 93 district rows.
The published-operational adapter adds the last useful pre-election update
retained by Antony Green for Queensland 2020/2024, Western Australia 2021/2025,
South Australia 2022, Victoria 2014/2022 and NSW 2023. The 2014 Victorian
district records are the contemporaneous 6pm election-eve percentages of
postal votes received plus pre-poll votes cast. It also retains exact
retrospective final controls for South Australia 2014/2018 and Victoria 2018.
The surviving federal 2022 election-eve national pre-poll, postal-application
and postal-return totals supplement the later AEC reconciled postal snapshot.
Integer headline figures are kept exact. The 2014/2022 Victorian, 2021 WA,
2022 SA and 2023 NSW district tables that publish only rounded rates are converted
using each district's official enrolment and explicitly marked approximate;
they must not be treated as commission-published exact counts.
Western Australian files use the Legislative Assembly vote-type tables in
WAEC's final results and statistics reports from 2005 through 2025. Reports
through 2021 distinguish election-day ordinary and early in-person votes. For
2025, the report combines election-day, early and mobile polling, but WAEC's
final verbose results XML identifies every polling place. The adapter classifies
only names containing the exact phrase `Early Polling Place` as early, retains
`Mobile Polling` separately, and requires each district split to reproduce the
report aggregate exactly. WAEC reports vote-type formal votes but not informal
votes by category. The 2017 and 2021 PDFs detach the printed ordinary column
from its rows in their text layer; the adapter therefore records those ordinary
counts as the exact difference between each official formal total and the other
published categories.
The operational NSW adapter streams NSWEC's complete depersonalized 2015
pre-poll transaction file. It records the exact final election-eve mark-off
count for each elector's enrolled district and the corresponding state sum;
the 65 MB source CSV is not retained in the repository. Compare these marks
with final rows whose source category is `Total Pre-Poll Ordinary Votes`.
The broader canonical `early_combined` category also contains declared-
institution votes and is not a like-for-like denominator.
South Australian files use ECSA's final election-statistics tables from 2006
through 2018 and the equivalent final CSVs for 2022. They retain a complete
ordinary/declaration partition with formal and informal votes in each category.
The declaration aggregate is not split into early, postal, absent or provisional
votes because ECSA's district summary does not support that distinction.

SA 2026 uses ECSA's reviewed final results JSON and records detailed polling-
place, declaration and absent-ordinary counts instead. Named EVCs and early
absent-ordinary votes contribute to `early_in_person`; early declarations
remain `declaration_early`. The complete partition includes postal, ordinary,
absent, provisional, mobile and the combined telephone/interstate/overseas
category. Its source notes record the result version, update time, endpoint
URLs and raw-source hashes. Refreshing final evidence preserves the dated
pre-election observations.

See [`docs/turnout-data-foundation.md`](../../../docs/turnout-data-foundation.md)
for the shared schema, category policy, available sources and reproduction commands.

The [category allocation report](../../../docs/turnout-category-dynamics/report.md)
uses these final partitions and independently calibrated early/postal counts
to compare three ways of distributing a formal-vote budget. It records
compatible historical groupings, separates allocation from total-size error,
and describes common and district errors without inventing missing historical
category splits. Its detailed local output is ignored; the consolidated report
and reproduction instructions are public.

The [initial-distribution report](../../../docs/turnout-prior-prototype/report.md)
uses the same evidence to assess uncertainty around initial total and category
estimates. It rebuilds parameters without each test election, compares
previous-share and controlled proportional allocations, and exports local
preparation summaries with distinct shared responses. Categories add to district
totals, and geographic totals add from districts. Its interval widths remain
provisional where observed coverage falls short. Logarithmic category changes
keep positive categories positive. Controlled federal declaration pre-polls
retain their previous percentage of all formal votes; ordinary pre-polls
receive the balance of the combined early estimate. The report compares
national declaration counts and district interval coverage against final
results; agreement between representations alone does not establish calibration.
Turnout and formality changes use natural log odds in both fitting and drawing,
so movements taper near 0% and 100% without endpoint caps. Both count interfaces
retain the inexpensive rate product and smooth reconciliation of combined
early/postal shares; extreme requests leave a continuous positive remainder.
Published zeros remain unchanged in the data. Available categories with
observed zero counts receive half-vote equivalents before transformation;
unknown counts do not become zeros or fabricated internal divisions. The
modelling input floor is the smaller of 0.1% and half a vote divided by the
observed parent count, preserving genuine smaller positive shares in large
groups. The shared `analysis/lib/turnout/category_policy.py` lists known
definition, eligibility and service changes. Affected individual comparisons
are excluded from behavioural training; broader observed pools remain usable.
Aggregated empty groups and relationships with different parent denominators
are listed for human review in the ignored analytical output. Only selected
final pre-election early/postal records enter calibration; proportional postal
application conversion remains a linear multiplier.
Reviewed implausible reporting zeros become unknown in the analytical view,
while the source JSON retains the published count. Affected district splits
cannot train or score category allocations because votes may have been
misclassified. District totals and published operational controls remain
usable; unknown final category targets cannot train conversion multipliers.
Rate responses and covariances use labelled log-odds units. The schema 3 fixtures
in `fixtures-v3.json` distinguish these units from earlier exports.
Detailed Queensland uncertainty
does not transfer category changes across the 2017–2020 reporting break.
Versioned count fixtures and
training-specific numerical output are regenerated locally and ignored; the
public report explains the methods, results and reproduction commands.
