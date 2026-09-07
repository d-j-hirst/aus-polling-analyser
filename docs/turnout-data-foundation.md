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
| Queensland | Final result pages/media feeds provide electorate and booth or vote-type results; [election reports and election-data spreadsheets](https://www.ecq.qld.gov.au/elections/election-events/2024-election-events/2024-state-general-election) provide turnout and pre-election early/postal evidence. | Data is spread across the results site, XML and reports rather than one stable historical CSV family. |
| South Australia | [ECSA publishes](https://www.ecsa.sa.gov.au/elections/past-state-election-results?catid=12:elections&id=636:2022-state-election-results-and-statistics-downloads&view=article) enrolment, early-vote and postal statistics plus final result downloads. The 2022 summary provides ordinary and combined declaration totals by district. | The combined 2022 declaration total cannot be defensibly split into the distinct categories reported in 2026. Keep it combined. |
| Western Australia | [Final statistical reports](https://www.elections.wa.gov.au/elections/state-elections/reports/2021-state-election-results-and-statistics-report) provide district enrolment and category totals. The 2021 report separates ordinary, absent, postal, early in-person and provisional votes; operational early/postal tables are also published. | Later reporting regimes may combine categories differently, so source labels and regime identifiers must be retained. |

## Normalized Structure

`analysis/turnout_data.py` defines four kinds of evidence:

1. `SeatTotal` records final enrolment, formal, informal and total ballots,
   together with the state or territory subdivision needed for federal data.
2. `VoteTypeRecord` records final vote-type counts. `source_category` retains
   the commission's exact term; `canonical_category` permits comparisons only
   at a defensible common level.
3. `OperationalObservation` records dated pre-election values such as early
   voting marks, postal applications and postal returns. These are not final
   vote counts and must not be mixed with them implicitly.
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
4. Add the ECSA final-result adapter while retaining 2022 declaration votes as
   `declaration_combined`; ingest 2026 daily early/postal figures separately as
   operational observations.
5. Add WA report extraction, which provides strong state-level historical
   category evidence but requires a reliable PDF/table source adapter.
6. Only after coverage is measured should the turnout model decide which
   common categories and election years have enough evidence for estimation.
