# Normalized Turnout Evidence

The JSON files in this directory contain validated official turnout evidence
for modelling and coverage analysis. They are source-derived data, not model
outputs.

Regenerate the normalized files from the commissions' official material with:

```bash
cd analysis
./env/bin/python -B turnout_aec.py --election all
./env/bin/python -B turnout_aec_operational.py --election all

./env/bin/python -B turnout_nsw_operational.py --election 2015nsw
./env/bin/python -B turnout_nsw.py --election all
./env/bin/python -B turnout_vic.py --election all
./env/bin/python -B turnout_qld.py --election all
./env/bin/python -B turnout_qld_operational.py --election all
./env/bin/python -B turnout_wa.py --election all
./env/bin/python -B turnout_sa.py --election all
./env/bin/python -B turnout_published_operational.py --election all
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

See [`docs/turnout-data-foundation.md`](../../../docs/turnout-data-foundation.md)
for the shared schema, category policy and planned state-election adapters.
