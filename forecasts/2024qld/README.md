# Queensland 2024 live replay

This package prepares the 2024 Queensland election for diagnostic live replays.
It supplies the current ballot roster, comparable 2020 booth accounts and a
turnout prior using information available before counting. Received captures,
prepared turnout samples and replay outputs remain private operator files.

## Candidates and count accounts

All 525 candidates across 93 districts are checked against ECQ's roster. The
authored seat file retains its pre-election margins and campaign assumptions.
Running-party lists include the number of independent and unmodelled minor
candidates: for example, `OTH**` represents three candidates. Candidate-name
entries identify representatives of modelled parties; listing a low-profile
independent on the ballot does not create a prominent-independent prior.

Queensland's LNP is one party. There is no separate Liberal/National split or
National candidacy probability to configure. Existing independent-incumbent
and prominent-candidate settings are retained.

The ECQ reader prefers official rechecks once they cover a comparable number
of votes, retaining fuller preliminary accounts while the rechecks are still
partial. Both rounds are available in the current source, so this selection
also works when starting a replay midway through rechecking. They are never
added together.

A recheck is clearly smaller when the shortfall exceeds the shared allowance
of the greater of 10 votes or 5% of the official count. For example, an official
batch of 10 FP votes does not replace 3,552 preliminary votes. A larger downward
correction can nevertheless replace the preliminary account once official FP
and TCP reconcile within that allowance, identify the complete ballot and two
finalists, and respect the finalists' primary counts. This also allows an
explicit, reconciled correction to zero.

FP and TCP can catch up separately. A new TCP pairing is a different account
and is not held merely because it has fewer votes than the old pairing. The
official distribution's two-candidate account remains usable when the indicative
round disappears. Empty placeholders remain unavailable; malformed records
continue through normal input recovery. The shared recovery checks reconcile
the selected accounts after round selection.

Physical ordinary, early and mobile booths are counted once. Early absents,
election-day absents, postals, in-person declarations and telephone votes are
separate category accounts. Early absents use the same category in both years.
The two telephone service labels contribute to one telephone account.

The private preload adds Mackay City from ECQ's updated pre-election venue
list, without importing any counted votes. The older preload omitted it.
Historical parties keep their affiliation even when absent from the current
ballot. The 2020 booth history uses official first preferences and available
final two-candidate accounts from [ECQ's final archive](https://resultsdata.elections.qld.gov.au/XMLData/publicResults_State2020_aurukun2020_Final.zip).
Missing historical TCP accounts are left missing.

## Turnout assumptions

The maintained `2024qld/earlier_only` preset uses earlier elections to estimate
turnout and formality variation. Current enrolment and venue identities come
from the preload; previous resident vote counts divide the starting allowance
between districts and categories.

The statewide controls are 1,620,434 early votes cast and 692,180 postal ballots
dispatched, reported in the [election-day early-voting summary](https://antonygreen.com.au/early-voting-and-how-tonights-queensland-election-night-count-might-unfold/).
The early control covers ordinary early booths and early absents together, but
these retain separate live records and uncertain shares of that total.

The 2020 ratio of formal early votes to early votes issued is
1,235,086 / 1,288,696 = 0.95840. Applying it gives an initial expectation of
about 1,553,024 formal early votes. This is a starting estimate, not a fixed
total: with only one comparable election, the common uncertainty has a standard
deviation of 10% of that expectation. District allocation retains a further
20% relative uncertainty. The postal conversion factor is 0.77109, giving
about 533,731 expected formal postal votes, also with broad conversion and
district-allocation uncertainty.

Mobile polling was cancelled in 2020. Its zero counts therefore do not define
the expected size of the resumed 2024 service. Mobile allowances use 2017
district counts, with a statewide fallback where necessary; these are marked
as unmatched estimates. Other matched venues use their previous size, and new
venues use the district median for their kind of booth.

The timetable uses Queensland local time (AEST): polls close on 26 October at
18:00 and postal receipt closes on 5 November at 18:00, following the
[ECQ election timetable](https://www.ecq.qld.gov.au/elections/election-events/2024-election-events/2024-state-general-election).
Receipt closure does not mean counting is complete.

With the private source files installed, regenerate historical accounts from
the repository root:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\scripts\elections\prepare_qld_live.py downloads\turnout\qld\publicResults_State2020_aurukun2020_Final.zip --year 2020 --historical-output "analysis/Booth Results/2020qld.json"
```

Then prepare the fixed turnout prior from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_qld 2024qld
```

This prepares 256 turnout outcomes and eight per-snapshot count draws for the
existing live interface. It performs no Stan fitting or forecast simulation.
The normalized inputs and source hashes are retained in `live-inputs/`.

## Replay captures

Store operator-supplied captures in
`forecasts/2024qld/live-snapshots/feeds/`. Configure the desired chronological
selection using the [snapshot sequence interface](../../docs/live-snapshot-sequences.md).
Received feeds, generated selection lists, replay projects and forecast outputs
are local artifacts rather than part of this configuration package.

Capture comparison ignores export timestamps and derived percentages, retaining
count corrections, availability, candidate identities, enrolment and official-round
changes. The council by-election in the same XML does not trigger a state
forecast update. Repeated state-election accounts can be omitted from a replay;
a return to an earlier account after a correction remains a real update.
An unchanged numerical feed can still lead to a different forecast through
elapsed counting time, so a data-change selection does not test every possible
update cadence.

ECQ's [final 12 November archive](https://resultsdata.elections.qld.gov.au/XMLData/publicResults_SGE2024_ICCDiv4_Final.zip)
provides an endpoint that includes Mirani's completed two-candidate count.
It supplies results only, never the turnout prior or accepted-count history in
advance.
