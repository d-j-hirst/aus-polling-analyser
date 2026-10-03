# Turnout Evidence and Sources

This document explains the election turnout evidence available in this
repository. It helps readers understand what the recorded counts represent
and reproduce the datasets and historical analyses.

The turnout tools collect enrolment, final vote counts and published counts
of early voting and postal applications. They store these in a common format,
retain source details and check that the reported totals agree. The reports
describe changes between elections and the limits of comparisons between
different voting categories and geographic areas.

## Running the tools

Run Python command examples from `analysis/`. Commands using `env/bin/python`
refer to the Linux/WSL environment; Windows uses `.venv-win/Scripts/python.exe`.
Turnout scripts live in the `scripts.turnout` package and use `python -m`.

## Available Official Evidence

The preferred source order is an electoral commission's final machine-readable
result, its final statistical report, and then a retained official media feed.
Adapters remain jurisdiction-specific because category definitions and file
formats differ.

| Jurisdiction | Useful official evidence | Important limitation |
| --- | --- | --- |
| Federal | [AEC final CSV files](https://results.aec.gov.au/31496/Website/HouseDownloadsMenu-31496-Csv.htm) provide division enrolment, formal and informal totals, and candidate formal votes split into Ordinary, Absent, Provisional, Declaration pre-poll and Postal. | From 2010, `OrdinaryVotes` combines election-day votes with early votes cast in the elector's own division. In 2004 and 2007, all pre-poll votes were declarations and remain separately identifiable. |
| NSW | [Final election reports](https://elections.nsw.gov.au/about-us/reports/election-reports) and Virtual Tally Room pages provide district enrolment and categories including Ordinary, Early Voting, Declared Facility, Absent, Postal and Enrolment/Provisional. | Labels and aggregation rules have changed between elections; telephone and other small modes may be folded into broader categories. |
| Victoria | [Final result pages](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results), election reports and retained official XML feeds provide district totals and vote-type evidence. VEC defines Ordinary, Absent, Early, Postal, Provisional and Marked-as-voted categories. | VEC's `Early` category itself includes several modes, including mobile and telephone-assisted voting. Preserve it as a combined category. |
| Queensland | Retained final ECQ XML media feeds provide district totals and booth vote types from 2006 through 2024; [election reports and election-data spreadsheets](https://www.ecq.qld.gov.au/elections/election-events/2024-election-events/2024-state-general-election) provide pre-election early/postal evidence. | Generic historical polling-booth records require explicit venue labels to distinguish pre-poll, telephone, electronically assisted and mobile voting. |
| South Australia | [ECSA publishes](https://www.ecsa.sa.gov.au/elections/past-state-election-results?catid=12:elections&id=636:2022-state-election-results-and-statistics-downloads&view=article) historical enrolment and vote statistics. Its [reviewed final 2026 results](https://result.ecsa.sa.gov.au/) provide district structure, polling places and detailed declaration/absent batches as public JSON. | Historical declaration totals remain combined. SA 2026 has a separate detailed category regime; first preferences and later count stages are distinct sources. |
| Western Australia | [Final statistical reports](https://www.elections.wa.gov.au/elections/state/reports) provide district enrolment and category totals from 2005 through 2025. Reports through 2021 separate ordinary, absent, postal, early in-person and provisional votes; the [2025 final verbose XML](http://media.waec.wa.gov.au/Archive/2025%20SGE%20Final/8%20March%202025%20State%20General%20Election%20-%20LA%20VERBOSE%20RESULTS.xml) also retains polling-place identities. | The 2025 report combines election-day, early and mobile polling. The final XML permits an exact split by WAEC's `Early Polling Place` designation, reconciled to every report district total. |

## Normalized Structure

`analysis/lib/shared/turnout_data.py` defines four kinds of evidence:

Schema version 2 added explicit operational geography and reconciliation
status. Schema version 3 adds count precision and derivation so rounded
published rates cannot be mistaken for exact counts. Schema version 4 adds
`count_relation`, distinguishing point values from lower and upper bounds.
Schema version 5 adds `count_basis`, so a commission forecast cannot be
mistaken for a count already reported. The loader continues to accept earlier
schema versions.

1. `SeatTotal` records final enrolment, formal, informal and total ballots,
   together with the state or territory subdivision needed for federal data.
2. `VoteTypeRecord` records final vote-type counts. `source_category` retains
   the commission's exact term; `canonical_category` permits comparisons only
   at a defensible common level.
3. `OperationalObservation` records dated pre-election values such as early
   voting marks, postal applications and postal returns. These are not final
   vote counts and must not be mixed with them implicitly. Every observation
   identifies whether its geography refers to an elector's division, an
   administering division, a state or the nation. It also distinguishes a
   contemporaneously published snapshot from a later final-reconciled series.
   Exact direct point counts omit the optional `count_precision`,
   `count_relation` and `derivation` fields. Approximate counts identify their
   precision explicitly; `count_basis=forecast` identifies forward estimates;
   district counts reconstructed from a rounded
   published rate also record their derivation. Phrases such as “at least” are
   retained as `lower_bound`, rather than treated as point estimates.
4. `SourceDefinition` records the authority, source location, adapter, status
   and category regime used for each set of observations.

Each vote-type partition is marked `complete` or `partial`. A complete
partition must sum exactly to the seat's formal-vote total. A partial partition
may be used when an official source exposes only some categories. Different
partitions must not be added together unless an adapter explicitly establishes
that they are mutually exclusive.

Unreported measurements are `null`, never zero. A zero is accepted only when
the official source actually reports zero. It records the observed count,
without implying that the vote category or service was unavailable. A small
possible category can happen to receive no votes, while a service known not
to operate requires a separate explanation. Empty batches or venue rows are
added with the other rows in their category; they do not establish that the
whole category was empty. A category reported inside a combined total has
an unknown separate count, rather than a zero count.

## Canonical Category Policy

Adapters should choose the least specific category supported by the source:

* `election_day_ordinary` and `early_in_person` are used only when the source
  distinguishes those modes.
* `ordinary_combined`, `early_combined` and `declaration_combined` preserve
  official aggregates that cannot be split without assumptions.
* `absent`, `declaration_early`, `postal`, `provisional`,
  `mobile_or_institution` and `telephone` are used when directly identifiable.
* `enrolment` and `provisional` preserve the separate 2015 NSW categories;
  `enrolment_or_provisional` preserves the inseparable aggregate in later NSW
  elections. `remote_electronic` retains NSW iVote without treating it as
  telephone voting.
* `marked_as_voted` preserves the VEC's small distinct declaration category.
* `other` is retained for a genuine official residual, not for missing data.

This means models can aggregate detailed elections to a comparable historical
level, but ingestion never fabricates detail in older elections.

## Acquisition Tools

1. `analysis/scripts/turnout/turnout_aec.py` acquires 2004 through 2025 federal evidence and
   tests exact reconciliation against official division and category totals.
   The adapter preserves the 2010 change that moved own-division pre-poll
   votes into the ordinary count.
2. `analysis/scripts/turnout/turnout_nsw.py` acquires 2015, 2019 and 2023 NSW Legislative Assembly
   evidence from final VTR district pages. It preserves combined NSWEC
   categories and retains 2019 iVote separately.
3. `analysis/scripts/turnout/turnout_vic.py` acquires 2006 through 2022 Victorian Lower House
   evidence from final VEC district-result pages. The 2006 declaration category
   remains combined, and the postponed 2022 Narracan supplementary election is
   correctly excluded from the general election.
4. `analysis/scripts/turnout/turnout_qld.py` acquires 2006 through 2024 Queensland district
   evidence from ECQ final XML archives. It uses official booth type codes and
   explicit venue labels, and reconciles every category partition to its final
   district total.
5. `analysis/scripts/turnout/turnout_wa.py` acquires 2005 through 2025 Western Australian
   Legislative Assembly evidence from WAEC final results and statistics
   reports. For 2025, final verbose XML polling-place names split the report's
    combined ordinary/early/mobile figure into exact reconciled categories.
    The named early-polling-place count does not identify early absent votes;
    matching the 2025 statewide early-voting operation remains unresolved.
6. `analysis/scripts/turnout/turnout_sa.py` acquires 2006 through 2022 South Australian House
   of Assembly evidence from ECSA's final statistics reports and CSVs. It
   retains ordinary votes separately and preserves all other modes as the
   published `declaration_combined` aggregate, including category informality.
7. `analysis/scripts/turnout/turnout_sa_operational.py` ingests ECSA's final 2026 district and
   state early-voting mark-offs and postal applications as operational
   observations; they are not treated as final accepted ballot totals.
8. `analysis/scripts/turnout/turnout_sa_final.py` imports reviewed final SA 2026
   first preferences from ECSA's public results JSON, preserving detailed
   categories and existing pre-election observations.

## Federal Operational Evidence

`analysis/scripts/turnout/turnout_aec_operational.py` adds official AEC operational series to
the existing 2010 through 2025 federal JSON files:

```bash
cd analysis
./env/bin/python -B -m scripts.turnout.turnout_aec_operational --election all
```

The adapter records cumulative daily pre-poll votes issued and postal vote
applications. The surviving 2022 AEC postal file supplies only its final
applications and returns snapshot; date-stamped 2025 files supply the daily
applications and returns series. AEC pre-poll counts are attached to the
division administering each PPVC, not necessarily the elector's enrolled
division. Postal records are attached to the enrolled division.

Daily columns reconstructed from the AEC's retained consolidated files are
marked `final_reconciled`, because later cancellations or corrections may have
changed their historical values. Date-stamped 2025 postal snapshots are marked
`contemporaneous`. The 2019 postal file contains a very small explicitly
undated residual. The adapter reconciles it to the source total but does not
assign it an invented observation date.
Rows for applications still awaiting division assignment, or subsequently
withdrawn, duplicated or rejected, are likewise excluded from division series.

`analysis/scripts/turnout/turnout_sa_operational.py` reads ECSA's published 2026 daily-tally
page:

```bash
cd analysis
./env/bin/python -B -m scripts.turnout.turnout_sa_operational --election 2026sa
```

It reconciles every district's daily cells to its published total. The 47
district early-markoff rows sum to 454,860, while the source's displayed state
total is 454,862; both are preserved with the two-vote residual. Postal district
rows sum to the published state total of 174,121. Antony Green separately reported
466,364 early votes while noting an approximately 11,000-vote discrepancy
with ECSA; the normalized exact records therefore use ECSA's own table.

`analysis/scripts/turnout/turnout_nsw_operational.py` streams NSWEC's complete depersonalized
2015 pre-poll transaction file:

```bash
cd analysis
./env/bin/python -B -m scripts.turnout.turnout_nsw_operational --election 2015nsw
```

The source covers 16-27 March and therefore directly reconstructs the final
pre-election count. The adapter validates all 642,408 transactions and all 93
districts, then stores exact cumulative totals by enrolled district plus their
state sum. It does not retain the 65 MB source CSV or infer postal counts from
the separate transaction file's final-state fields. Calibration should compare
these mark-offs with final `Total Pre-Poll Ordinary Votes` source rows, not the
broader canonical `early_combined` category, which also contains declared-
institution votes.

## Curated Election-Eve Evidence

`analysis/scripts/turnout/turnout_published_operational.py` preserves the last useful update
available before polls closed where original commission operational files were
not retained:

```bash
cd analysis
./env/bin/python -B -m scripts.turnout.turnout_published_operational --election all
```

The adapter covers federal elections in 2004 and 2007, Queensland 2006, 2009,
2015, 2017, 2020 and 2024, Western Australia
2005, 2008, 2013, 2017, 2021 and 2025, South Australia 2006, 2010, 2014, 2018 and 2022, Victoria
2006, 2010, 2014, 2018 and 2022, and NSW 2019 and 2023. The 2014 Victorian records preserve each district's
contemporaneous 6pm
election-eve percentage of postal votes received plus pre-poll votes cast. It
also retains exact retrospective final controls for South Australia 2014/2018
and Victoria 2018, labelled `final_reconciled` rather than contemporaneous. It
uses fixed Antony Green or ABC News articles and embedded tables based on
electoral-commission data. The federal 2022 election-eve national totals
supplement the AEC's later reconciled postal snapshot. Exact aggregate totals
and the Queensland 2020 district postal table remain exact. NSW, Victorian,
South Australian and 2021 WA district tables expose only rounded rates; their
counts are deterministic approximations based on final official district
enrolment and are labelled accordingly. Approximate statements retain explicit
bounds, while the 2017 WA election-eve estimate is additionally labelled as a
forecast rather than a reported count.
The 2013 WA records are deliberately limited to pre-poll and postal ballots
processed for election-night counting; they are not represented as the final
numbers cast. The 2017 Queensland records are rounded election-day reports of
pre-poll votes cast and postal ballots issued. The postal figure is not treated
as returned ballots: ECQ later reported only 168,000 returns before election
day and approximately 300,000 postals counted overall.
The 2008 WA records come from later WAEC reports and are therefore labelled
`final_reconciled`. They preserve a lower bound of more than 60,000 in-person
early votes and more than 65,000 postal applications four days before polling,
and distinguish 81,219 early-by-post votes issued from the 35,467 postal votes
admitted for election-night counting. The 2009
Queensland record is a contemporaneous Electoral Commissioner estimate of
about 213,000 postal-vote requests, not a count of returned or accepted votes.
The 2005 WA report similarly preserves 35,220 in-person early votes issued,
50,419 postal applications and the 34,821 postal votes ready for election-night
counting. These separate controls should not be combined as equivalent votes.
The 2006 South Australian report preserves 23,419 in-person pre-polls, 66,066
processed postal applications, 61,364 postal packs issued including automatic
declaration-register electors, 54,543 returned certificates and 51,584 accepted
for scrutiny. These are later reconciliations of election-period operations,
not observations reconstructed from final result-category totals.
For 2014 South Australia, an ABC election-eve report adds a contemporaneous
lower bound of 50,000 pre-polls, approximately 86,000 postal applications, and
ECSA forecasts of more than 70,000 final pre-polls and about 160,000 early or
postal voters. The forecasts are explicitly distinguished from both reported
progress and Antony Green's later exact 80,087 pre-poll control.
For 2018 South Australia, an ABC election-morning report records approximately
120,000 pre-polls, 95,000 postal votes and more than 215,000 votes before
election day. The rounded postal figure is classified as ballots issued because
it agrees with the later exact 94,831 issue total rather than the 82,213 postal
applications. Antony Green's retrospective figures remain separate exact
controls, allowing the election-eve estimates to be evaluated rather than
silently replaced by final data.
The latest retained 2010 South Australian update predates polling day by six
days and reports only that postal applications exceeded 80,000. It is retained
as an approximate lower bound, not promoted to a final application total.
The early federal records are later AEC reconciliations of pre-election postal
operations: an approximate 760,000 packages issued in 2004, and exact totals of
833,178 applications and 812,826 packages issued in 2007. They fill the postal
gap before the daily AEC operational adapter begins in 2010, but do not provide
equivalent daily series.
Older commission reports add exact 2006 Victorian early-vote and postal-
application totals, an approximate 2006 Queensland postal-application total,
and Queensland's exact 2015 central postal mailout. These are marked
`final_reconciled` because the surviving publications postdate the election;
they remain operational quantities rather than final accepted ballot counts.
An election-eve ABC PM report supplies an approximate contemporaneous count of
half a million early in-person votes for 2010 Victoria. Its wording is loose,
but the figure aligns with the later exact 543,763 pre-poll total rather than
the combined total. A separate ABC retrospective supplies a combined total of
768,483 votes cast before polling day; that control is deliberately not split
into in-person and postal components because the source does not establish the
split.

The source survey covered Antony Green's current election-eve tracking posts,
his older ABC election-blog archive, and surviving electoral-commission
reports. All supported general-election tracking posts with usable numeric
updates are represented above. The remaining gap is 2012 Queensland: retained
publications provide final category totals or post-election commentary, but no
defensible pre-election operational snapshot. That election therefore remains
without operational observations rather than having final-result totals
mislabelled as forecasts or election-eve counts.
ACT, Northern Territory and Tasmanian elections, referendums and by-elections
were reviewed but remain outside the current normalized general-election
dataset.

## Historical Change Analysis

[`turnout-election-changes/report.md`](turnout-election-changes/report.md)
describes consecutive-election changes in turnout, formality, formal votes per
enrolled elector, enrolment, formal-vote counts and vote-category shares. It
covers election totals, same-named seats, and federal states and territories.
Detailed CSVs accompany the report, including category formality where known,
seat dispersion, unmatched seats and residual changes after election/state
adjustments.

Regenerate the report from the normalized final-result files with:

```bash
python3 -B -m scripts.turnout.turnout_changes
```

These reports describe observed historical changes. Seat comparisons are not
redistribution-adjusted, and pre-election operational observations are not
inputs to these historical-change calculations.

[`turnout-expectations/report.md`](turnout-expectations/report.md) builds on
that description with election-wide drift statistics, federal state residuals,
seat variation, stable ballot-regime comparisons and simple formal-vote
expectation tests. Both leave-one-out and earlier-elections-only validation
hold out entire elections. The leave-one-out training set also excludes the
successor transition, which would otherwise leak the held result through its
previous-election baseline. Enrolment is treated as a known exposure.

```bash
python3 -B -m scripts.turnout.turnout_expectations
```

This remains a research report: it does not change live turnout assumptions.
Known lower- and upper-house ballot reforms are documented with official
sources; their individual causal effects are not inferred from single events.

[`turnout-priors/report.md`](turnout-priors/report.md) is the follow-up pooled
analysis. It supersedes jurisdiction-specific expectation models with shared
turnout/formality drift estimates, whole-election validation and a test of
transferability when each jurisdiction is left out. Its main findings highlight
uncertainty scales for common election shocks, pooled federal-state variation
and seat-local changes. Detailed seat histories and prior-rate-gap bins identify
persistent differences and unusually volatile seats without fitting separate
coefficients to small seat/state histories.

```bash
python3 -B -m scripts.turnout.turnout_priors
```

This is still offline research, not a change to the live forecasting model.

## Queensland Operational Evidence

`analysis/scripts/turnout/turnout_qld_operational.py` adds ECQ's retained 2024 operational
workbooks to the existing final Queensland dataset:

```bash
cd analysis
./env/bin/python -B -m scripts.turnout.turnout_qld_operational --election 2024qld
```

The daily in-person attendance workbook identifies the electorate in which a
voter was marked off, including absent voters enrolled elsewhere. It is
therefore recorded by administering district, not elector district. Only the
ten pre-election voting dates enter the cumulative early-attendance series;
the election-day column is still validated against the workbook totals.

The postal workbook provides a final-reconciled snapshot rather than a daily
series. Ballots issued, returned and accepted are recorded by elector district,
with the ECQ's warning that issued ballots can include duplicates. Its
statewide returned and accepted totals slightly exceed the sums of the 93
district rows, so both the district observations and distinct statewide totals
are preserved without assigning the residual to invented districts.

## Conservative Evidence Audit

`analysis/scripts/turnout/turnout_evidence_audit.py` describes the coverage and
comparability of the recorded evidence. It uses the existing normalized-data validation,
then writes [a coverage/comparison report](turnout-evidence-audit/report.md)
and a machine-readable `turnout-evidence-audit/audit.json` manifest.

Run from `analysis/` on native Windows:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_evidence_audit
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_evidence_audit --check
```

The selection admits reported exact point counts and counts reconstructed
from published rates rounded to 0.1 percentage point or finer. It excludes
forecasts, bounds and vague or unspecified approximations. Those excluded
records remain in the original datasets. Each source contributes its latest
eligible control through polling day for each measure, geography, district
and reconciliation status. Source alternatives and district/parent totals
must not be treated as independent calibration samples.

The audit covers 35 datasets, all with final seat evidence,
and retains 3,038 latest controls: 2,318 exact and 720 reconstructed from close
rates. A precise control is not automatically usable for calibration. The
manifest records a supported final target or an explicit category/geography
gap, with publication availability marked unknown where it is unverified.
Final-reconciled controls are distinguished from contemporary evidence.
QLD 2024's detailed final postal workbook is dated after polling and therefore
does not supply election-eve district controls. SA 2026 has detailed reviewed
final counts. The main federal category files combine ordinary early and
election-day votes from 2010; the separate federal supplement described below
recovers their early component from polling-place results. SA's older combined
declarations remain combined.

For source updates during or after an election, refresh the relevant
ingestion adapter and regenerate the audit. The manifest records source URLs,
adapters, quantity dates, category regimes and normalized snapshot hashes.
`--check` detects changed counts/metadata, added or removed datasets and
changed audit/loader code; JSON formatting changes alone do not invalidate it.
It checks local refreshed evidence, rather than polling remote publications.
The consolidated audit report is public documentation. Its detailed JSON
manifest and retained raw source downloads are generated local artifacts.

## South Australia 2026 Final Results

The SA 2026 dataset contains reviewed final first-preference counts for all
47 House of Assembly districts. The source is the public JSON used by the
[ECSA results website](https://result.ecsa.sa.gov.au/), with election status
`final`, result revision 912 and a source update of 21 September 2026.

The imported revision totals 1,317,186 enrolled electors, 1,115,864 formal
votes, 50,332 informal votes and 1,166,196 ballots. These are the current
source's first-preference counts, rather than totals reconstructed from
two-candidate-preferred or preference-distribution records. ECSA's
[review statement](https://ecsa.sa.gov.au/se2026news/se2026-results-review-complete)
acknowledges residual differences between some count stages.

The import retains named early-voting-centre votes and early absent-ordinary
votes in `early_in_person`, with early declaration votes separately recorded
as `declaration_early`. Final early-vote comparisons include both categories.
Postal, polling-day absent, provisional and mobile votes retain their own
observed categories. Telephone/interstate/overseas declarations stay combined
as `other`. No historical SA declaration aggregate is split using these data.

Each polling place and published batch contributes once. Candidate ordinary
and declaration summaries reconcile to the corresponding detailed counts;
the absent-ordinary batches are separate additions. The existing 96 dated
pre-election observations are preserved when final data are refreshed.

Download and validate without changing files, then import:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_sa_final --election 2026sa --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_sa_final --election 2026sa
```

An import retains `elections.json`, `static.json` and `results.json` beneath
`downloads/turnout/2026sa/version-<version>-<hash>/`. Their hashes and URLs are
recorded in the normalized source notes. A later source revision receives a
separate snapshot directory, including corrections without a version-number
change. To reproduce an import from retained files, add
`--source-directory <snapshot-directory>` to the command.

## Operational Count Calibration

The [operational calibration report](turnout-operational-calibration/report.md)
compares published early-voting and postal counts with the corresponding final
formal vote categories. It describes how much information the controls supply
about final category sizes and how much conversion error remains.

`analysis/scripts/turnout/turnout_operational_calibration.py` reuses the audit's
exact/closely approximate selection and supported target definitions. It keeps
one control series per election and measure, preferring contemporary evidence
and district controls over retrospective series and duplicate parent totals.
One matched observation is a district count paired with its final category
count, or a total observation where only an aggregate is usable. A conversion
multiplier is final matched formal votes divided by the operational count.
The estimated multiplier is shared across districts rather than fitted
separately for each one; each training election contributes equally to its mean.

The analysis module calculates predictions, grouped errors, rounding
sensitivity and outlier diagnostics. The separate
`analysis/scripts/turnout/turnout_operational_report.py` module explains and
formats those completed results as the public report. The command below
continues to produce both the analytical output and the report.

Run from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_operational_calibration --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_operational_calibration
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_operational_calibration --check
```

Both predictions trained on all other elections and predictions trained only
on earlier elections are computed, with their training sample sizes retained.
The models compare one multiplier across elections, separate multipliers for
federal and state elections, the previous compatible election's multiplier,
and a same-name previous-category count scaled by current enrolment. NSW controls
are diagnostic-only by default; `--include-nsw` adds them to fitting and testing.
One-sided source bounds remain excluded. Publication rounding receives a
simple endpoint sensitivity check.

The generated local `calibration.json` records matched comparisons, held-election
predictions, training identities, pooled factor candidates, descriptive common
and local error scales, and conservative common error allowances based on both
validation schemes. Nominal intervals are diagnostics; small samples and
unverified publication times limit claims about predictive coverage and
historical availability. The report is public documentation, while the detailed
JSON is ignored. Neither output changes live forecast behaviour.

The report also investigates whether federal pre-poll counts reported under
the electorate administering a voting centre help predict final early votes
belonging to that electorate's residents. These are different voter groups,
so this is a separate test of an informative indicator, rather than a change
to the main audit's matching rules. Both electorate and national comparisons
are reported.

`analysis/scripts/turnout/turnout_federal_prepoll.py` recovers the final federal
early target for 2010–2025 using AEC polling-place classifications and
first-preference CSVs. It sums formal ordinary votes at pre-poll voting centres,
excludes informal votes, and adds final declaration pre-poll formal votes once.
All polling-place ordinary formal counts must reproduce the existing normalized
ordinary category before the supplement is written. The original category
partitions remain unchanged.

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_federal_prepoll
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_federal_prepoll --refresh
```

The first command reuses retained raw files where present; `--refresh` fetches
current AEC files. The public `Data/Turnout/FederalPrepoll/final.json` records
the reconstructed counts, source URLs and hashes, and the normalized dataset
version used. Raw CSV revisions are retained in separate content-hash
directories under the ignored `downloads/turnout/federal-prepoll/` cache.
Regenerate the supplement when its normalized federal inputs change, then rerun
calibration. Its input and adapter hashes are included in the calibration
freshness check.

Refresh source data through the relevant ingestion adapter and rerun calibration
after a source revision. The local input/code fingerprints and options are
checked by `--check`, using the same semantic-input hashing as the evidence audit.

## Category Allocation Analysis

The [category allocation report](turnout-category-dynamics/report.md) assesses
how calibrated early-voting and postal counts help allocate a formal-vote
budget across voting categories. It compares previous category shares,
proportional allocation of the uncontrolled remainder, and an alternative
that retains smaller-category counts per enrolled elector and assigns most
adjustment to ordinary voting.

`analysis/scripts/turnout/turnout_category_dynamics.py` computes these
comparisons and the separate `turnout_category_report.py` formats the public
report. Each comparison uses the same election/district observations across
the three rules. Supplying the actual final total isolates allocation error;
predicted totals test the combined effect of allocation and total-size error.
Both training on other elections and training only on earlier elections are
reported. Turnout-change training also excludes successor transitions that
contain the test election. Zero, half and full historical turnout change, and
training without specified COVID-period endpoints, are fixed sensitivity checks.

Historical categories are grouped only where their published definitions are
compatible. Federal national early controls are distributed by previous
resident-electorate early counts per enrolled elector and current enrolment.
Issuing-centre electorate counts are not used as resident controls. Older SA
declarations remain combined; SA 2026 instead receives an early/postal budget
diagnostic. WA 2025's unresolved early/absent distinction is preserved in a
broad grouping. NSW is shown descriptively and excluded from training and
prediction by default.

The report explains category changes, individual-election results, conversion
training sample sizes, and common and district errors that move together.
Joint error matrices describe historical patterns with category errors summing
to zero when the total is supplied; they are not fitted district parameters
or calibrated probability intervals. The local `analysis.json` is ignored,
while the consolidated report is public. Neither changes the live forecast.

Run from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_category_dynamics --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_category_dynamics
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_category_dynamics --check
```

The command reuses audited exact or closely approximate controls and the
normalized final results. Refresh the relevant source adapter and federal
supplement after source revisions, then regenerate the report. `--check`
detects changes to the local inputs, relevant code and command options;
it does not fetch remote updates. Add `--include-nsw` to include NSW in training
and prediction tests.

## Initial Vote-Count Distributions

The [initial-distribution report](turnout-prior-prototype/report.md) examines
how useful uncertainty ranges can be constructed around total and category
expectations before live results are counted. It compares prediction intervals
with a previous-category-share baseline and exports reduced-sample local
statistics and distinct shared count responses.

`analysis/lib/turnout/prior.py` constructs bounded counts, distinct uncertainty
responses and reduced-sample local count statistics. The analysis command
`analysis/scripts/turnout/turnout_prior_prototype.py` fits parameters separately
for each test election and scores predictions; `turnout_prior_report.py`
formats the public report. Turnout, formality, early conversion, postal
conversion and category composition retain distinct responses. Every outcome
has nonnegative categories adding to its district total, and geographic totals
are sums of districts. Historical combined categories remain combined.
Turnout changes and turnout/formality uncertainty are fitted and applied in
natural log odds, tapering toward the endpoints without rate caps. Both count
interfaces retain the inexpensive turnout-times-formality product, so formal
votes stay below the drawn ballot count. The preparation export retains local
summaries and separate shared responses; rate responses and covariances are
labelled in log-odds units rather than percentage points.
Combined early/postal shares and the federal declaration anchor also use
smooth transformed changes during draws. Extreme requests leave a continuous
positive remainder rather than a fixed negligible reserve. This reconciliation
can change requested national controls; the numerical diagnostics record it.
Incompatible starting controls are reported as errors rather than silently capped.

Both training on other elections and training only on earlier elections are
reported. Parameters exclude the test election and transitions involving it;
final votes enter scoring separately from prediction inputs. Some interval
comparisons miss more often than their stated coverage would suggest, so the
reported widths remain provisional. Category proportions use a logarithmic
transformation so positive categories stay positive. For a controlled federal
early total, the declaration expectation retains its previous percentage of
all formal votes and ordinary pre-polls receive the balance. The report
compares national declaration counts and district interval coverage against
final results. Federal early counts are used nationally,
with resident allocation uncertainty, rather than as issuing-centre district
controls. NSW is excluded from this prototype.
Detailed Queensland uncertainty keeps the 2015/2017 and 2020-onward reporting
regimes separate. Where comparable training is unavailable, the report labels
the assumed widths. Zero observations retain their published values. Possible
zeros in available categories receive a half-vote equivalent for transformed
starting weights and uncertainty fitting. The input floor is the smaller of
0.1% and half a vote divided by the observed group total, so large groups retain
genuine smaller positive shares. Unknown components are not assigned zeros,
and whole empty groups do not establish an internal division.

Reviewed implausible reporting zeros are represented as unknown in the
analytical view. The underlying published records remain unchanged. Where
votes may have been classified elsewhere, the whole district category split
is excluded from allocation training and scoring; district totals remain
usable. Missing earlier partitions receive the labelled aggregate baseline.
Final targets that depend on an unreliable split cannot train conversion
factors, but the published pre-election controls remain available for prediction.
The shared policy records these decisions and releases an exclusion when a
refreshed source supplies a positive count for the reviewed group.

The shared `analysis/lib/turnout/category_policy.py` records known category
definition, eligibility and service changes. The affected category's separate
change is excluded from behavioural training; comparisons use a broader
observed group where one is available. VIC 2006–2010 and WA 2021–2025 use this
mechanism. QLD 2020 cancelled declared-institution placeholders are suppressed
within-election, while the separately reported remote-mobile votes remain.
The local output lists aggregated empty groups for individual review, final
controls with zero counts, and relationships involving different parent
denominators. Postal application conversion remains a linear multiplier;
subset rates and category proportions use bounded transformations.

Run from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prior_prototype --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prior_prototype
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prior_prototype --check
```

Defaults use 1,024 evaluation outcomes and 288 local preparation outcomes.
The ignored `analysis.json` records scores, training parameters, endpoint and
control-adjustment diagnostics, and agreement between the count interfaces.
The ignored `fixtures-v3.json`
exports versioned inputs, distinct count responses, preparation statistics and
three reproducible outcomes for each representation of VIC 2022, WA 2025,
Federal 2025 and SA 2026. These are count examples, not prepared party-share
sensitivities. Neither output changes the live forecast or measures the full
application's runtime.

Refresh source adapters and the federal supplement after source corrections,
then regenerate. The freshness check includes local input/code fingerprints,
sample options and the NumPy version; it does not poll remote sources. The
consolidated report is public documentation, while the numerical outputs are
reproduced locally.
