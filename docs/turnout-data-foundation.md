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
published rates cannot be mistaken for exact counts. The loader continues to
accept version-1 final-result datasets and version-2 operational datasets.

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
   Exact direct counts omit the optional `count_precision` and `derivation`
   fields. Approximate counts identify both fields explicitly; district counts
   reconstructed from a one-decimal published rate use
   `rounded_rate_times_enrolment`.
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
7. Ingest 2026 daily early/postal figures separately as operational
   observations; they must not be treated as final category totals.
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
the separate transaction file's final-state fields.

## Curated Election-Eve Evidence

`analysis/turnout_published_operational.py` preserves the last useful update
available before polls closed where original commission operational files were
not retained:

```bash
cd analysis
./env/bin/python -B turnout_published_operational.py --election all
```

The adapter covers Queensland 2020 and 2024, Western Australia 2021 and 2025,
South Australia 2022, Victoria 2014 and 2022, and NSW 2023. The 2014 Victorian
records preserve each district's contemporaneous 6pm election-eve percentage
of postal votes received plus pre-poll votes cast. It also retains exact
retrospective final controls for South Australia 2014/2018 and Victoria 2018,
labelled `final_reconciled` rather than contemporaneous. It uses fixed Antony
Green articles and embedded tables based on electoral-commission data. The
federal 2022 election-eve national totals supplement the AEC's later reconciled
postal snapshot. Exact aggregate totals and the Queensland 2020 district postal
table remain exact. NSW, Victorian, South Australian and 2021 WA district
tables expose only rounded rates; their counts are deterministic approximations based on final official
district enrolment and are labelled accordingly. Approximate statements such
as votes ready for election-night counting are also kept separate from exact
totals.

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
