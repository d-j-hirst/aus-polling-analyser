# Turnout Data Foundation

This dataset is intended to support a general live-turnout model. It must not
encode the one-off conversion between the combined 2022 SA declaration total
and the separate 2026 SA declaration categories as if that conversion were an
observed historical fact.

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
| South Australia | [ECSA publishes](https://www.ecsa.sa.gov.au/elections/past-state-election-results?catid=12:elections&id=636:2022-state-election-results-and-statistics-downloads&view=article) enrolment, early-vote and postal statistics plus final result downloads. The 2022 summary provides ordinary and combined declaration totals by district. | The combined 2022 declaration total cannot be defensibly split into the distinct categories reported in 2026. Keep it combined. |
| Western Australia | [Final statistical reports](https://www.elections.wa.gov.au/elections/state/reports) provide district enrolment and category totals from 2005 through 2025. Reports through 2021 separate ordinary, absent, postal, early in-person and provisional votes; the [2025 final verbose XML](http://media.waec.wa.gov.au/Archive/2025%20SGE%20Final/8%20March%202025%20State%20General%20Election%20-%20LA%20VERBOSE%20RESULTS.xml) also retains polling-place identities. | The 2025 report combines election-day, early and mobile polling. The final XML permits an exact split by WAEC's `Early Polling Place` designation, reconciled to every report district total. |

## Normalized Structure

`analysis/turnout_data.py` defines four kinds of evidence:

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

Unavailable values are `null`, never zero. A zero is accepted only when the
official source actually reports zero.

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

## Initial Acquisition Sequence

1. `analysis/turnout_aec.py` acquires 2004 through 2025 federal evidence and
   tests exact reconciliation against official division and category totals.
   The adapter preserves the 2010 change that moved own-division pre-poll
   votes into the ordinary count.
2. `analysis/turnout_nsw.py` acquires 2015, 2019 and 2023 NSW Legislative Assembly
   evidence from final VTR district pages. It preserves combined NSWEC
   categories and retains 2019 iVote separately.
3. `analysis/turnout_vic.py` acquires 2006 through 2022 Victorian Lower House
   evidence from final VEC district-result pages. The 2006 declaration category
   remains combined, and the postponed 2022 Narracan supplementary election is
   correctly excluded from the general election.
4. `analysis/turnout_qld.py` acquires 2006 through 2024 Queensland district
   evidence from ECQ final XML archives. It uses official booth type codes and
   explicit venue labels, and reconciles every category partition to its final
   district total.
5. `analysis/turnout_wa.py` acquires 2005 through 2025 Western Australian
   Legislative Assembly evidence from WAEC final results and statistics
   reports. For 2025, final verbose XML polling-place names split the report's
   combined ordinary/early/mobile figure into exact reconciled categories.
6. `analysis/turnout_sa.py` acquires 2006 through 2022 South Australian House
   of Assembly evidence from ECSA's final statistics reports and CSVs. It
   retains ordinary votes separately and preserves all other modes as the
   published `declaration_combined` aggregate, including category informality.
7. `analysis/turnout_sa_operational.py` ingests ECSA's final 2026 district and
   state early-voting mark-offs and postal applications as operational
   observations; they are not treated as final accepted ballot totals.
8. Only after coverage is measured should the turnout model decide which
   common categories and election years have enough evidence for estimation.

## Federal Operational Evidence

`analysis/turnout_aec_operational.py` adds official AEC operational series to
the existing 2010 through 2025 federal JSON files:

```bash
cd analysis
./env/bin/python -B turnout_aec_operational.py --election all
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

`analysis/turnout_sa_operational.py` reads ECSA's published 2026 daily-tally
page:

```bash
cd analysis
./env/bin/python -B turnout_sa_operational.py --election 2026sa
```

It reconciles every district's daily cells to its published total and all 47
district totals to the state totals. The source reports 454,862 early-voting
mark-offs and 174,121 postal applications. Antony Green separately reported
466,364 early votes while noting an approximately 11,000-vote discrepancy
with ECSA; the normalized exact records therefore use ECSA's own table.

`analysis/turnout_nsw_operational.py` streams NSWEC's complete depersonalized
2015 pre-poll transaction file:

```bash
cd analysis
./env/bin/python -B turnout_nsw_operational.py --election 2015nsw
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

`analysis/turnout_published_operational.py` preserves the last useful update
available before polls closed where original commission operational files were
not retained:

```bash
cd analysis
./env/bin/python -B turnout_published_operational.py --election all
```

The adapter covers federal elections in 2004 and 2007, Queensland 2006, 2009,
2015, 2017, 2020 and 2024, Western Australia
2008, 2013, 2017, 2021 and 2025, South Australia 2010 and 2022, Victoria
2006, 2014, 2018 and 2022, and NSW 2019 and 2023. The 2014 Victorian records preserve each district's
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
The 2008 WA records come from the later WAEC election report and are therefore
labelled `final_reconciled`: they distinguish 81,219 early-by-post votes issued
from the 35,467 postal votes admitted for election-night counting. The 2009
Queensland record is a contemporaneous Electoral Commissioner estimate of
about 213,000 postal-vote requests, not a count of returned or accepted votes.
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

## Queensland Operational Evidence

`analysis/turnout_qld_operational.py` adds ECQ's retained 2024 operational
workbooks to the existing final Queensland dataset:

```bash
cd analysis
./env/bin/python -B turnout_qld_operational.py --election 2024qld
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
