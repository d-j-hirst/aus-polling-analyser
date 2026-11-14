# Observed preference evidence

This catalogue preserves the counts and contexts available for studying lower-house
preferences. It follows original first-preference ballots where the published
tables allow that, and retains complete exclusion sequences where only mixed
piles are observed. The catalogue supplies evidence for preparing models.
The application reads only the small optional comparison settings described below.

## Coverage and interpretation

One **original-source view** records where the ballots initially cast for one
candidate went between a specified final pair. Its denominator is that candidate's
original formal FP votes. A second pairing can reveal more about those same
ballots, but does not supply another independent sample of voters.

A **published distribution** records successive candidate exclusions. A candidate's
pile can include votes received from other candidates, so a transfer of that pile
is generally not an original-source flow. The published sequence is retained as
one district evidence unit. Publication can stop when a winner is established,
before only two candidates remain.

The current collection comprises 1,432 district/election units:

| Source population | District/election units | Accepted original-source views | Published distributions | Exclusion stages |
| --- | ---: | ---: | ---: | ---: |
| Nine federal general elections, 2001–2025 | 1,352 | 6,896 | — | — |
| 25 federal by-elections, 2005–2026 | 25 | 223 | — | — |
| Alternative federal TPP pairings, 2019/2022/2025 | Same units as above | 426 additional views | — | — |
| SA 2026 | 47 | — | 47 | 294 |
| Selected Victorian districts, 2022 | 8 | — | 8 | 51 |

There are 7,545 accepted federal source/pair views of 7,195 different original
candidate ballot groups. Of those groups, 350 have overlapping accepted pairing
views. These are counts of records and ballot groups, not recommended statistical
sample sizes or fitting weights.

Of the 345 exclusion stages, 56 contain no inherited ballots and 289 contain a
mixture. The uncontaminated stages remain inside their published distributions;
they are not duplicated in the federal original-source table. Of the 55 published
distributions, 54 reach two continuing candidates. Brunswick stops with three,
so it supplies no invented Liberal-to-ALP/Greens final transfer.

Six additional 2025 Bradfield source rows are preserved as `quarantined`. The
[AEC's division page](https://results.aec.gov.au/31496/Website/HouseDivisionPage-31496-108.htm)
explains why the additional TPP count was not conducted, and its
[preference-flow table](https://results.aec.gov.au/31496/Website/HouseStateTppFlow-31496-NAT.htm)
identifies Bradfield's data as mathematically approximated. These rows must not be
used as observed source preferences. The ordinary observed Bradfield TCP evidence
remains available.

## Files

- `evidence.json.gz`: the compact, self-contained count catalogue, approximately
  0.74 MB compressed. Normal checks and analysis need no raw feed archive.
- `sources.json`: identities, URLs and hashes of the 67 source artifacts used,
  source selection, explicit exclusions, verified recommendations and candidate
  classifications. It also identifies the selected members of the 2001 archive.
- `aec-alternative-tpp-extracts.json`: 238 original-source counts transcribed from
  published AEC-origin 2019/2022 alternative-TPP tables, with candidate identities
  and totals matched to separate official FP tables.

The 2019/2022 supplemental counts were published in AEC-supplied tables hosted by
Antony Green. Their publication URLs are retained, but the original PDF bytes were
not available to this preparation, so document hashes are explicitly unavailable.
That limitation is different from the raw commission downloads, whose content
hashes are checked. The transcription is maintained input, not an automatic PDF
parser or an assertion that its destination split was independently measured twice.

Raw commission files remain in an operator's source cache and are not copied into
the repository. The catalogue and its maintained inputs together occupy less than
1 MB. They do not contain individual ballots or per-simulation ranking histories.

## Data structure and denominators

The catalogue has `sources`, `publications`, `inputs`, `exclusion_rules`,
`coverage_limits` and a list of `contests`. Each contest is one district in one
polling event. `election` is a year/jurisdiction label, such as `2018fed`; the
separate `event` and `election_type` distinguish by-elections in the same year.
Candidate identifiers are local to that contest. The original identifier scheme
is retained, including old ballot positions and Werriwa's printed-name identities.

Each candidate has an original `fp_votes` count and `fp_fraction`. The fraction
uses **all formal FP votes in the district**, including the eventual finalists'
own primaries. This measures source-primary strength. It is not the candidate's
fraction of the preference pool. Missing party affiliation remains distinct from
an explicit independent affiliation, and independently named candidates retain
different identities even when they have the same party/group code.

Each original-source observation has:

- `source_candidate` and `source_ballots`: the candidate and original ballot group;
- `denominator_kind: original_first_preferences` and `denominator_votes`;
- two `destinations` with actual integer vote counts, and any printed exhausted
  count retained separately;
- `views`, identifying TCP or alternative TPP, `status`, and source references;
- a stable observation ID determined by source and recipient pair.

`overlapping_source_views` links all views of the same original ballots, including
quarantined views for auditing. Exact duplicate tables merge their references;
contradictory duplicate counts are rejected. Quarantined observations remain
separately labelled and cannot be included merely because their arithmetic works.

A `distribution` contains each exclusion's `pile_votes`, `inherited_votes`,
recipient transfers and `progressive_totals`. `denominator_kind: excluded_pile`
means that its raw transfer percentages describe the whole pile. The final
`continuing` count and `completed_to_pair` flag preserve where publication stops.

Each recipient also has `original_source_vote_bounds` for that stage. For example,
if a pile contains 200 original Greens ballots and 20 inherited ballots, and sends
150 votes to Labor, between 130 and 150 of those votes must be original Greens.
These are arithmetic limits, **not probability intervals**. The whole sequence
is preserved because additional stages can restrict the origins, and different
recipients' marginal endpoints need not all be jointly possible.

Counts are non-negative integers. Fractions are proportions from 0 to 1, not
percentages. A genuine zero remains zero; an unknown context remains explicitly
unknown. No preference rate is manufactured from a zero denominator.

## Recommendations, candidate contexts and party labels

Five source-specific recommendations have been checked against lodged cards:
Greens and Liberal cards in Elder and King, SA 2026, and the Liberal card in
Melbourne, Victoria 2022. Source document hashes, pages and target orders are
retained. Melbourne also retains registration 453 and its printed numbers.

Nine further Liberal recommendations for 2025 federal ALP/Greens final-pair
contests are matched to the candidate names and orders in
[ABC NEWS Verify's collection of party how-to-vote cards](https://www.abc.net.au/news/2025-05-01/how-to-vote-cards-in-your-electorates/105217758),
published on 1 May 2025. They cover Canberra, Cooper, Fraser, Grayndler, Griffith,
Melbourne, Newcastle, Sydney and Wills. Each puts Labor above the Greens. Their
verification method is identified as a published transcription, separately from
the five manually checked lodged-card cases. The source CSV and its hash, and
the printed preference numbers, are retained; a PDF page number is inapplicable.

All other candidate recommendations are `unknown`. Advice is not inferred from
the year, the final pairing or the observed flow. A verified ranking is a party's
recommendation, not a measured fraction of voters following it.

Candidate types describe recorded party affiliation, independent or unaffiliated
status. Political subtypes are not inferred from primary strength. The explicit
`independent_like` classification for Mayo's Sharkie in 2019, 2022 and 2025 is
identified as an existing forecast configuration, separate from source observations.
Her official party identity remains recorded. That classification is not applied
to NXT in 2016; other political subtypes remain unspecified.

Comparison aliases normalise spelling/code variants while retaining the original
FP affiliation. Nationals remain separate from Liberals. Raw LNP/LNQ/CLP labels
remain available even where their comparison group is `LIB`. The 2025 supplemental
table also uses `GVIC` for `GRN`, `FFPA` for `FFP`, and blanks for some unaffiliated
codes. Those printed differences are retained in `reported_party_codes` beside
the affected observations. The parser repairs only the six specifically known
unquoted Shooters party-name commas; unexpected column shifts fail preparation.

## Source checks and reproduction

The federal general-election rosters come from separate official FP tables,
including the 2001 candidate import file. Werriwa also has its separate published
FP table. For the other 24 by-elections, non-finalists' original-source totals and
finalists' own-FP rows reconstruct the roster. This is a complete denominator,
but is explicitly labelled as **not an independent FP cross-check**.

Validation checks source hashes during rebuilding, candidate identities, all
original-source totals, complete donor coverage for each pair table, duplicate
views, progressive counts and conservation at every exclusion. It preserves
unknown advice and actual publication stops. An identical district name in two
elections is a repeated geographic context, not proof of unchanged boundaries.

With `analysis` as the working directory, check the compact retained catalogue
without raw downloads, network access, fitting or application execution:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.prepare_preference_evidence --check
```

Run the portable Python checks directly from the repository root with:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\tests\run_tests.py test_preference_evidence
```

To reconstruct from a cache containing the source artifacts named in
`sources.json`, use `analysis` as the working directory:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.prepare_preference_evidence --source-dir C:\path\to\preference-sources --check
.\.venv-win\Scripts\python.exe -B -m scripts.diagnostics.prepare_preference_evidence --source-dir C:\path\to\preference-sources --write
```

Without `--write`, rebuilding is read-only. `--check` with a cache also compares
the rebuilt counts against the retained catalogue; without a cache it validates
the catalogue and its maintained input fingerprints, but does not re-read the
original sources. Preparation never downloads data. The legacy 2001 cache contains
selected official archive members with individual hashes, rather than requiring
a complete statistical-CD download. Source URLs, archive members and artifact
hashes are all in `sources.json`. For locally repackaged ZIP/GZIP artifacts, the
authoritative hashes describe the **uncompressed official members**; changing a
compression timestamp or level does not change their identity. The original
local-container hash is retained for auditing. Direct commission downloads still
require their exact recorded bytes.

All included observations are CPV. Unparsed older federal distributions and
Nicklin's OPV count are not silently counted as additional original-source data.
This catalogue reports source coverage and known gaps; it makes no claim that
the number of ballots establishes how narrowly another election should behave.

## 2026 Victoria and South Australia preference counts

The ranking model uses each group's original ballot preferences to decide
district exclusions and the final two-candidate vote. It starts with the
simulated first preferences and Labor-versus-Coalition two-party-preferred
vote (TPP). The election's `mode` setting in [2026vic.json](2026vic.json)
or [2026sa.json](2026sa.json) controls how the model is used.

- `"active"` supplies the seat winners for the forecast, betting-odds
  calibration and the pre-count live baseline. Calibration can then adjust
  a candidate's primary share using the same count that will decide whether
  they win. The ranking uses the reference settings, including classic TPP
  matching. If it cannot resolve a simulated district, the existing count
  resolves that district within the same scenario. **Active ranking count**
  in `PALog.log` gives the totals and the first reason for each district
  that used the fallback in that run.
- `"compare"` uses the existing count for the forecast and its supporting
  runs. It also runs the ranking count on the same main forecast draws and
  reports the differences under **Preference comparison** in `PALog.log`.
- `"off"` uses the existing count alone.

The simulation captures the file with its other inputs, so a change refreshes
any retained calibration and live baseline when the next run starts.

The 2026 South Australian replay uses the same fitted profiles and uncertainty
as Victoria, with the two verified South Australian Liberal cards in Elder and
King. Other districts use the unknown-advice assumption. The replay classifies
the former Liberal MPs Nick McBride (MacKillop) and Fraser Ellis (Narungga) as
right-leaning independents for the Greens preference model. The SA project calls
One Nation `ON` and models a single Liberal account. Any minor Nationals
candidacy is part of Others in that project, so there is no Coalition split
in the ranking count. The fitted profiles include final 2026 South Australian
preference evidence. The replay checks how the model behaves as results arrive
and how much further the live data can inform it.

One observation is one district in one successful simulation iteration. The
comparison log gives paired counts of how often the final pair or winner changes, the
largest changes in classic source rates and a few examples. A source rate is
the fraction of ballots originally cast for a candidate or for the Others
group. For example, a 20-point change in the rate from a source on 5% of the
district's first preferences is one point of the district vote. Same-pair TCP
differences compare the shares given to the same two candidates. Official final
district results supply their own pairing and counts. An incompatible TPP means that the
simulated primaries and preference pool cannot reproduce the supplied classic
TPP. Such cases are counted separately, with the attainable bounds in the
examples. Matching live TCP evidence can refine each projected pair once,
before any manual winner override.

The versioned settings describe distributions of rankings for Labor, Liberal,
Greens and One Nation original ballots. They were prepared offline from
original-source pair counts and exclusion distributions, principally the SA
2026, Victorian 2022 and federal counts in the catalogue. File hashes identify
the evidence and fitted parameters used. Five conditional fractions describe
the six possible orders among each source's three core recipients. The
election-wide and district concentrations control how tightly those fractions
vary. Others starts as a single source. The existing classic draw supplies its
starting Labor/Coalition split, and poorly observed splits have broad priors
centred around even proportions. The existing project rows for independent
recipients and Nationals donors supply additional prior centres.

The optional `districts` object is keyed by the exact district name.
`liberal_advice` accepts `labor_first`, `greens_first`, `open` and
`unknown`. Unknown advice shares an election-wide draw of the direction,
initially with an even chance of either outcome. A district can also supply
an `independent` object with the configured candidate's `name` and a
`position` of `left`, `centre` or `right`. This affects how often Greens
ballots rank that candidate above both majors. For a named `right` independent,
the share of Labor ballots ranking them ahead of the Liberal starts at 60%.
This is a broad assumption for cases where Labor voters have less reason to
favour the independent, with about 20 percentage points of standard deviation
under the supplied settings. Observed Labor flows in
[2018](https://antonygreen.com.au/preference-flows-at-the-2018-south-australian-election-and-the-influence-of-how-to-votes/)
and [2022](https://antonygreen.com.au/do-how-to-votes-matter-evidence-from-the-2022-sa-election/)
South Australian contests illustrate the range of outcomes, including changes
with Labor's recommendation. Other independents use the configured project
rate. When Liberal and One Nation are both still in the count, the two
head-to-head rates allow a range for the share ranking the independent ahead
of both. The ranking model uses the midpoint of that range for each draw,
preserving the feasible head-to-head rates and the Liberal–One Nation order.

Districts can also include `historical_transfers` for a recorded exclusion
from an earlier election. Each entry gives the `election`, `source` URL,
`donor` party code, the donor's `original_votes`, the `inherited_votes`
it received at earlier exclusions, and the `transfers` to each of the two
to four surviving candidates by party code. Include a zero for any surviving
candidate who received no votes. The transfers must sum to the original
and inherited votes. A row involving `IND` also needs the configured
`independent_name` to identify the returning person. Use one exclusion
per donor and district.

The count shows each ballot's highest choice among those survivors. The
model varies how many of the inherited ballots went to each recipient, then
uses the estimated original ballots to adjust the current rankings. The
`weight` from zero to one controls the blend toward the old split before
preserving the drawn Labor–Coalition split and the chance that an unobserved
candidate in the fitted rankings beats all the recorded recipients. Those
constraints can reduce the adjustment. The model uses the record when the
donor and every recorded recipient are in the projected ballot.

In Finniss the 2022 Greens pile transferred 1,102 votes to Lou Nicholson,
537 to Labor and 164 to Liberal, including 156 ballots inherited from earlier
exclusions. The replay uses a weight of 0.5 to give that local result some
influence while allowing preferences to change between elections. One Nation
had already been excluded in that round, so its position is still estimated
from the fitted rankings. See the [2022 Finniss distribution](https://en.wikipedia.org/wiki/Electoral_results_for_the_district_of_Finniss#2022)
and the [ECSA distribution archive](https://www.ecsa.sa.gov.au/elections/past-state-election-results?catid=12:elections&id=636:2022-state-election-results-and-statistics-downloads&view=article).

For checks at fixed preference
means, set `sample_uncertainty` to `false` and set
`unknown_liberal_labor_first_probability` to 0 or 1 to use one advice direction.
Both modes use the original simulated first preferences and TPP.

## Manual live preference counts

A three- or four-candidate preference count shows which of those candidates
was ranked highest on the ballots examined. It can help estimate who survives
the next exclusion. Record these current-election observations in
`forecasts/<election>/live-inputs/preference-counts.json`. The file is optional
and read during automatic live preparation. It is used in districts with an
enabled ranking model. The `live_trial` mode enables the ranking model only
for automatic live counts in districts with `"live_trial": true` in the
ranking settings. This lets a diagnostic replay compare forecasts with and
without the sample against the same underlying ranking model.

```json
{
  "schema_version": 1,
  "observations": [
    {
      "district": "Finniss",
      "available_at": "2026-03-30T12:10:00+11:00",
      "sample": "Victor Harbor Early Voting Centre, Greens and Bron Lewis",
      "sampled_preference_ballots": 1053,
      "sampled_first_preferences": {"GRN": 560, "IND:Bron Lewis": 493},
      "flow_percent": {"IND:Lou Nicholson": 58, "ALP": 22, "LNP": 13, "ON": 7},
      "additional_sd_pp": 7,
      "source": "https://antonygreen.com.au/sa2026-finniss-will-a-candidate-win-from-fourth-place/"
    }
  ]
}
```

- Append updated observations and retain the earlier rows. The current run
  uses the latest row for each district available by its capture time, or
  by the run time when using the current live feed. A fixed test feed uses
  its own source timestamp. Supply an ISO date and time with a UTC offset.
  The date is when the figures became available to the forecast operator.
  Two rows for the same district at the same instant are ambiguous and are
  rejected.
- `flow_percent` describes the split of only the redistributed ballots.
  To convert published candidate totals into this split, subtract each
  recipient's own first preferences for the same booths or vote types.
  Use project party codes or `IND:Full Name` for independents. An optional
  `sampled_first_preferences` itemises the donor pool and must add up to
  `sampled_preference_ballots`. `OTH` is available for donors grouped by the
  model. The live ranking ballot distinguishes the leading two independent
  candidates. Without itemised counts the model uses the drawn district donor mix.
- Set `additional_sd_pp` to the uncertainty in each recipient's share of
  the preference pool, in percentage points, due to differences between the
  sample and the rest of the seat. The model adds allowance for finite sample
  size. In Finniss the 7 points and the exact cutoff are replay assumptions.
  The feed gives about 1,053 sampled first preferences from the two named
  donors, and the published split is rounded.

For observations like the Finniss example, the sample favours local ranking
draws that reproduce its split while keeping the scenario's first preferences,
Labor–Coalition TPP and shared election preference effects. The same rankings
then supply every exclusion.

A compulsory-preferential 3CP with Labor, one Coalition candidate and a
third candidate can also supply `sampled_candidate_totals`. This itemises the
three candidates' published tallies on the same ballots used for
`sampled_first_preferences`. Supply the three recipients' first preferences
on those ballots in `sampled_finalist_first_preferences`. Subtracting each
recipient's first preferences from its tally gives the exact transfer counts
and takes precedence over the rounded `flow_percent`. For example, the 11 May
Ryan sample uses tallies of `{"ALP": 28369, "GRN": 28949, "LNP": 37906}` and
finalist first preferences of `{"ALP": 26818, "GRN": 27371, "LNP": 33548}`.

The simulation then fixes the published allocations for the sampled ballots
and models only transfers on the other ballots. When both Coalition parties
stood and just one appears in the published three, the ordinary ranking
model still handles any earlier exclusions. The sampled transfers already
include any ballots from the excluded Coalition candidate.
In this calculation,
`additional_sd_pp` describes the spread of the flow on the ballots outside the
sample, with a provisional minimum of ten points. If earlier exclusions in a
draw leave a different group of three, or if the sample cannot fit within the
drawn district FP totals, that draw uses the ordinary ranking count.

If no Labor–Coalition TPP votes are available, the model uses this same
three-way allocation to estimate TPP. Ballots with a major already rank that
major above its opponent. The third candidate's own voters and the ballots
they inherited can split differently between the majors. An optional
`tpp_prior` supplies the two expected Labor shares as percentages, for example
`{"third_primary_to_labor_percent": 89.80435, "third_inherited_to_labor_percent": 50}`.
These are assumptions about the unobserved preferences. Without those
settings, the model uses the simulated source rate for the third candidate's
own voters and a neutral split for inherited ballots. When measured TPP
exists, the count-based TPP estimate takes priority and the sample can still
inform the pairing.

Official final results take precedence. The live analysis export records the
chosen row under `preference_count_observations`, and the active ranking log
reports how many draws used the joint account and their average transfer split.
`sample_uncertainty: false` uses fixed preference rates and still honours
published candidate totals when they are usable.

Source estimates with limited precedent include modelling assumptions and
carry broad uncertainty when estimating exclusions and final margins.
