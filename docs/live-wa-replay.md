# WA replay input preparation

This page explains how archived WAEC exports are prepared for a live replay,
which identities the reader uses, and the remaining limits of the historical
category data. Preparation copies official captures without altering their XML.
It does not run the forecast or fit statistical models.

## Capture clocks and repeated exports

The 2025 archive was captured using the operator's Sydney clock. Individual
WAEC vote records instead carry WA-local modification times. The preparation
command converts each filename's capture time with the named timezones, including
Sydney's daylight-saving changes, and writes `waec-la-verbose-YYYYMMDDHHMMSS.xml`
using WA time. For example, Sydney `250308224521` becomes WA `20250308194521`.

Run from the repository root on Windows:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\scripts\elections\prepare_wa_live.py "F:\Election Data\WA 2025 feed" --source-timezone Australia/Sydney --update-seats
```

Prepared files go under `forecasts/2025wa/live-snapshots/feeds`. Consecutive
captures with identical contents after excluding `LastUpdated` attributes go
in `feeds/no-data-change`. All other fields, including counts and statuses,
participate in this comparison. A return to an earlier count remains an update.
Sequence discovery scans only the main folder, leaving these repeats out.

The adjacent `preparation-manifest.json` records original filenames, original
and WA capture codes, source hashes, the filter decision and every Assembly
ballot entry. These exports and their manifest remain operator-local and are
gitignored. Original archive files are never moved or changed. Repeating the
command verifies existing copies rather than silently replacing different XML.

The normalized capture clock is the received observation time used by replay.
A booth's last modification time cannot replace it: an unchanged booth can
remain in a newly received export days later. Fixed-name live exports, which
have no capture code, use their original file modification instant converted
to WA time before a working copy is made. That fallback assumes the file time
represents receipt; historical replay should use normalized capture names.

## District, booth and candidate identities

WAEC venue GUIDs identify physical buildings, which can serve multiple districts.
The reader uses election date, district and polling-place number for a resident
vote account. Numbers are not stable across elections: for example, Albany
number 2 changed venues between 2021 and 2025. Historical links instead use a
unique venue GUID or normalized venue name, preferring an unambiguous resident
district match. Competing claims on one historical account remain unmatched.
Candidate ballot names are also scoped to their district. The election date
must agree with the candidate and polling-place preloads.
Independent ballot entries use the same reserved affiliation as other readers,
so separate candidates retain separate live vote accounts rather than pooling
into one Independent candidate.

Primary (`LAPC`) and two-candidate (`LAND`) rounds are selected by their codes,
independently of ordering and availability. Declaration category codes preserve
both generations of postal and mobile category names. The reader rejects
unknown identities or category codes instead of dropping their counts.
Missing accounts remain unavailable for the normal recovery path; the reader
does not invent measured zeros for absent records. Physical venues named
`Early Polling Place` are classified as early booths.

The preparation command updates only `sRunningParties` in the authored seat
file. Repeated candidates use the existing asterisk convention. It preserves
named-candidate lists and independent-prominence settings: appearing on a
ballot does not itself make an independent a prominent forecast candidate.
The full source roster, including Other candidates, remains in the manifest.
The portable forecast package maps the official `NATS` code to Nationals and
uses the existing separate Liberal/National Coalition calculation.

## Historical category limitations

The 2021 results feed publishes one early-in-person account per district. The
2025 feed publishes physical early-booth accounts and a broader absent account.
The [WAEC post-election report](https://www.elections.wa.gov.au/sites/default/files/SGE2025/Reports/WAEC9489%20State%20Election%20Post%20Election%20Report%20WEB.pdf)
describes the change to counting in-district early votes with ordinary votes.

The [2021 statistics archive](https://www.elections.wa.gov.au/voterstatistics)
also publishes votes issued by early centre and, separately, by the elector's
district. These are two totals across different populations, not a table linking
each centre to each elector district. They therefore do not identify how many
early voters attended a centre inside their own district. The final district
results retain the aggregate early account rather than that missing split.

Those publications cannot supply an observed historical match for every new
2025 early booth, or an exactly comparable absent category. The turnout prior
and any reconstructed comparison must state this limitation explicitly; the
capture/roster command is therefore separate from baseline preparation below.

## Historical and synthetic comparisons

The [2021 district results report](https://www.elections.wa.gov.au/sites/default/files/content/SGE%202021/2020-2021%20content/Reports/4_21_Res_Stats_Dist_Prof_Elect_Results.pdf)
contains final booth FP and TCP counts missing from the archived XML. The
preparation script extracts all 59 tables, validates each row and each district's
candidate totals, and uses the final published TCP pair. It does not attach
counts to an obsolete indicative pair from the XML.

The prepared comparison retains the same statewide historical FP and TCP
counts, candidate by candidate. It changes their allocation to reporting
accounts as follows; these allocations are assumptions, not observed history:

* Postal and mobile history retain their recorded compositions.
* A starting 60% of old early voters goes to local early places, reduced where
  published election-eve centre capacity is below the old resident early total.
  The four districts without a centre have no local early allocation. The
  remainder joins old absents. Multiple local centres divide the local allowance
  by their published issued-ballot capacities.
* Mixed early places also receive a district day-vote component: one typical
  venue per early place, capped at 10% of the district's day account. It is
  removed proportionally from day comparisons, preserving their party shares
  apart from integer rounding, rather than being added as extra historical votes.
* Provisional comparison composition borrows 2% of that historical day account.
  This supplies a synthetic party mix, not a prediction of provisional quantity.

Matched ordinary venues keep their source district for redistribution and
preference-flow comparisons. Unmatched historical day accounts remain in the
category baseline without acquiring a false current venue identity. Mid-West
combines Moore and North West Central. Oakford and Jandakot divide their shared
old Jandakot category reference by current enrolment. These broad district
references do not claim to reconstruct individual redistributed electors.

Synthetic early, absent and provisional comparisons retain the existing joint
TPP allowance of four local and three shared transformed points of uncertainty
(five combined, approximately five percentage points near 50%). In WA they also
receive one-quarter of ordinary historical relevance when pooling FP swings.
That is a conservative working safeguard, not a fitted coefficient. Received
votes remain counted in full and can inform absolute-share evidence.

## Remaining vote quantities

The turnout prior uses current enrolment and earlier turnout/formality rates.
Its six groups are ordinary places, mixed early places, combined absents,
postals, mobile and provisional votes. Each simulation's groups add to its
district formal-vote total.

The corrected [7 March early-vote publication](https://www.elections.wa.gov.au/sites/default/files/SGE2025/EV%20stat%20reports/SGE2025%20Early%20Voting%20Day%2010%202025-03-07.pdf)
reports **557,693 cumulative issued ballots**. The Friday increase is already
included. WA 2021's formal/issued ratio converts this to a starting formal early
allowance. It informs the starting non-postal proportions; it is not a separate
hard control after the new categories mix early and day ballots.

Postal applications use the maintained conversion based on WA 2005, 2017 and
2021. Provisional quantity instead uses NSW 2023's formal enrolment-vote fraction,
because WA 2021 eligibility was much narrower. Its quantity stays separate and
is excluded from compensating for unrelated category shortfalls.

The `2025wa/synthetic_reporting` preset retains the installed turnout/formality
uncertainty, adding conservative uncertainty in the new reporting split. Early
versus absent log-odds SD is 0.8 shared across districts and 1.1 locally. Other
directions allow day versus early/absent movement and particularly broad mobile
and provisional variation. These are explicit starting assumptions, not a fit
to WA 2025 final counts. Normalizing category shares means a transfer cannot
create voters in both categories. A district with no early place keeps zero in
that structural category in every prior outcome.

The postal receipt date is 9am on 13 March. It does not close absent or
provisional processing. Time-based decay of unreported early places is disabled
for this first replay because mixed batches and delayed large early counts make
that assumption particularly uncertain.

Declaration processing uses separate category timetables. For this replay, the
Postal transition centre is 13 calendar days after polling closes (21 March at
6pm), with a two-day logistic scale. Only about 69% of final formal postals had
been counted at the receipt deadline, versus about 95% by the morning of 21 March
and 98% in that evening's capture. The broad processing clock therefore also
tempers quietness-based completion evidence while a backlog may remain. It does
not change the legal receipt safeguard or the small positive finishing remainder.
These figures support a retrospective timing assumption, not a rule calibrated
across elections or a known final count available during the replay.

The Absent transition centre is 16 calendar days after polling closes (24 March
at 6pm), with a two-day logistic scale. This is explicitly retrospective: the
accepted formal absent count was approximately 81% of its final total in the
23 March capture, 87% on 24 March, and approximately complete on 25 March.
WA 2021's different reporting structure supplies no comparable progression.
The timetable uses only this broad timing pattern, not district-specific final
sizes or vote shares. Provisional retains the general 18-day centre. All three
remain gradual estimates, allowing ongoing activity and exceptional additions;
neither declares a category complete. These settings are recorded in
`forecasts/2025wa/live-turnout-settings.json` and copied into the prepared prior.
See [the timetable definitions](live-turnout.md#independent-category-processing-timetables)
for defaults, overrides and diagnostics.

## Mixed early/day batches

WA early places have an explicit `incremental` input flag. Only WA accounts
marked this way use the two-batch routine-counting rule in `WaEarlyCounting.h`.
Other jurisdictions' incremental accounts keep their existing treatment.

The rule counts substantial increases in accepted FP totals, using the compact
change index up to the replay time. Repeated totals and restoration below an
earlier maximum do not count again. An increase must reach the larger of 100
votes and 1% of the earlier maximum; the first positive report always counts.
After two such increases, no further *routine* voters are expected. Actual later
reports are still accepted, and rare signed revisions remain possible.

With one report, three explanations remain: only the small component, only the
large component, or both already included. Below roughly 1,000 formal votes the
small-component explanation dominates smoothly. Early substantial reports give
most weight to the large-component explanation, with at least 10-15% initial
support for both components. Support for both rises smoothly over the next
local calendar day (9 March in 2025), reaching one at its end. This is a working
counting-procedure assumption, not a guarantee about every future WA election.
Unreported places retain their original uncertainty regardless of that clock.

If the small component alone has arrived, the existing broad turnout allowance
supplies the other component. If the large component alone has arrived, the
smaller remainder has a starting median of 12% of the reported count and a
log-odds spread of 0.6 within the existing allowance. Reducing an early-place
allowance reduces total turnout; it does not enlarge absent or postal counts.
Outstanding TCP on already reported FP is separate from new voters throughout.
`live_analysis.turnout.units[].wa_early_batches` records the batch count and
three explanation probabilities. No prior regeneration or `.pol2` change is needed.

WA's explicit `Results Declared` status closes the district account; neither
`Final for the Night` nor `Awaiting Declaration` does so. No primary total in the
2025 archive changes after declaration. Before that status, routine FP counting
can nevertheless finish under the two-batch rule. This does not manufacture an
official declaration or infer that outstanding preference work has finished.

The WA reader also uses explicit declaration to interpret omitted FP candidates
as zero, but only within a nonempty account whose valid, uniquely identified
candidate counts sum exactly to its published `FormalVotes`. This admits the
final Joondalup mobile account: six listed candidates exhaust its 33 formal
votes, with three zero-vote candidates omitted. Entirely missing accounts and
malformed or unreconciled records still follow normal recovery, and TCP pairs
are never padded. Other electoral commissions' readers retain their rules.

Declaration is essential to this exception. Miling Town Hall's first 2025
capture listed one vote with five candidates absent; its formal total and
district summary both agreed. The next capture included another 71 votes for
those candidates. The source account timestamps were only 0.343 seconds apart,
consistent with a partly published update. Matching totals during counting
therefore do not justify filling missing candidate entries with zeros.

The separate general revision model assigns a possible signed correction to
each reported centre or declaration account, including completed ones. Its initial
90% unchanged / 10% signed-revision mixture has conditional SD 5% of the reported
FP total, subject to candidate and enrolment bounds. It permits candidate-specific
errors rather than proportional scaling only. The inspected Joondalup +510 and
Armadale -632 corrections motivate checking its 99% tails; these two cases do
not establish general calibration. See `docs/live-turnout.md` for accounting and
the distinction from the existing preference-only correction model.

## Prepare and use the baseline

Save the two official PDFs linked above locally. From `analysis/`, run:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.elections.prepare_wa_baseline --report <2021-district-results.pdf> --early-centres <2025-election-eve-early-centres.pdf>
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_wa
```

This writes `historical-2021.json`, `historical-baseline.json`, the matching audit,
`turnout-config.json`, the quantity audit and `turnout-prior.json` under the ignored
`forecasts/2025wa/live-inputs/` folder. Source hashes record the exact reports,
rosters and settings. Re-run both commands after changing the reconstruction
settings. No current-election results or fitting jobs are used.

After rebuilding, reopen the WA 2025 project so its portable forecast settings
are imported. Select `forecasts/2025wa/live-snapshots/feeds` for the sequence;
timestamps in prepared filenames are WA local time. A short diagnostic sequence
should cover the Fremantle early-place batch transition before the full checkpoint.
Late category quantities are deliberately uncertain; errors there are diagnostic
evidence, not grounds to fit the prior to this particular election's final counts.

## Verification

`test_prepare_wa_live` checks Sydney/WA clock conversion across daylight saving,
metadata repeats versus count reversions, Assembly-only candidate selection,
and preservation of authored seat assumptions. It is part of the normal
non-Stan Python test selection.

`tests/WaecInputTests.cpp` exercises shared venue/name identities, reordered
rounds, missing TCP and election identity using fictional XML. Its optional
repository-root argument audits operator-local 2021 results and all prepared
2025 captures, checking that parsed FP and TCP totals match every source
account. It also checks the recovered historical total, venue links, turnout
service mapping and incremental-place completion at early and final captures.
At those selected captures it checks mapped FP and TCP totals separately for
each party, with the historical baseline loaded alongside the current feed.
Preserving the grand total alone would miss votes assigned to the wrong party.
This is an input test; it does not construct or run a forecast.

The current feed and prepared historical baseline have independent numeric
party IDs. Live loading keeps these IDs separate by election and uses official
codes and project aliases to identify the same registered party across years.
For example, current party 4 is Greens while historical party 4 is Nationals;
those votes must remain separate. `LiveDataTests` also covers reused candidate
IDs, aliases for combined or separate Coalition parties, missing records and
distinct unconfigured parties without needing local WA files.

`test_prepare_wa_baseline` checks candidate conservation, ambiguous/renumbered
venues, structural category absence and the corrected cumulative early total.
Portable recovery and turnout tests cover incremental completion, including the
unchanged behaviour of inputs without the new flag. A rebuilt GUI replay is
still needed to verify forecast behaviour end to end.
