# Federal 2019 live replay

This package sets up the 2019 federal election for the current live forecast.
It uses the AEC candidate preload, previous-election accounts and archived
snapshots to test how the forecast responds as votes arrive. Received feeds,
turnout input files and replay outputs remain operator-local.

The authored seat file preserves existing margins, polling, betting odds and
campaign assumptions. Its running-party lists account for all 1,056 candidates
across 151 seats, including repeated IND and OTH entries: `IND*` means two
independent candidates. A complete ballot roster does not itself make any of
those candidates prominent. Named independent entries remain limited to the
original prominent-candidate settings and independent incumbents.

Cowper's Robert Oakeshott is marked as the returning prominent independent.
The 2016 preload's longer name, Robert James Murray Oakeshott, identifies the
same candidate. Independents holding seats retain their existing incumbent
path; Helen Haines in Indi does not acquire a defeated-returner flag from
Cathy McGowan's previous result.

## Candidates and the Coalition

Liberal, merged Queensland LNP and NT CLP candidates remain in the `LNP`
forecast group. Separate National candidates map to `NAT`. Candidate lists
determine which parties actually stand, while the fitted national polling
series remains the combined Coalition series.

For dual contests, starting National fractions of the combined Coalition FP
are 38.4168% in Indi, 27.6882% in Durack, 30.0520% in O'Connor and 9.3511% in
Pearce, from 2016 FP counts. Mallee uses its last dual contest in 2013:
33,270 National votes and 23,363 Liberal votes, or 58.7467% National. These are
fractions *within the Coalition*, not percentages of all district votes.
Other new dual contests retain the existing Nationals model, including its
5% minimum starting fraction when a National candidate is explicitly named.
That default is an assumption, not a measured local split.

Calare retains the existing prominent-independent prior for Sam Romano,
whose official affiliation was Shooters, Fishers and Farmers. The explicit
`Sam Romano,IND` entry in `sCandidateNames` maps that person to the independent
path when reading a live account. Matching requires the complete name within
Calare, allowing case, whitespace and surname-comma order differences.
Other SFF candidates remain ordinary unmodelled minor candidates. Neither
the received files nor candidate vote totals are rewritten. This mechanism
also supports similarly configured candidates in other elections; it is not
a global alias from SFF to IND. See
[LiveIndependentMapping.h](../../LiveIndependentMapping.h).

## Turnout inputs

The maintained `2019fed/earlier_only` preset uses earlier elections for turnout,
formality and category uncertainty. Current enrolment and booth identities come
from the 17 May preload. New or changed PPVC starting sizes use 2016-versus-2013
evidence; they do not use those centres' eventual 2019 sizes.

The retained [AEC operational data](https://www.aec.gov.au/elections/federal_elections/2019/downloads.htm)
contain 4,778,856 pre-polls issued and 1,538,624 postal applications at the last
17 May observation. The source datasets are marked `final_reconciled`, so this
is a diagnostic replay, not an exact reconstruction of every information
vintage. Neither number is a final count of formal votes. Earlier-election
conversion factors yield expected formal totals of approximately 4.807 million
early votes and 1.258 million postals, with uncertainty retained.

Postal applications identify electors' districts and provide district controls.
Pre-poll attendance is reported by administering district, so only its national
total is used as a control. Previous resident-category shares and the current
roll distribute that total among districts. Ordinary PPVCs and declaration
pre-polls remain distinct forecast categories.

The archived results preload contains names and classifications but lacks the
separate location file. Its companion is reconstructed from those identities,
with coordinates restored from the published AEC polling-place directory only
where ID, district and name agree. No final vote quantity enters that location
reconstruction; unlocated booths remain unlocated.

The timetable uses eastern polling close on 18 May at 18:00 and postal receipt
closure on 31 May at 18:00, following the
[AEC timetable](https://www.aec.gov.au/Elections/Federal_Elections/2019/timetable.htm).
Separate non-postal processing timetables retain the general model defaults.
Receipt closure does not mean that postal processing is complete.

With the private normalized configuration present, regenerate the 256-outcome
turnout prior from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_live "..\forecasts\2019fed\live-inputs\turnout-config.json"
```

This command prepares turnout inputs; it does not fit Stan or run simulations.

## Running the diagnostic sequence

Open the separate `Federal Election 2019 turnout replay.pol2` project in the
rebuilt Release application. Its original model/projection series are retained,
with the required emerging-Others configuration slot added using the existing
Others preference parameters. Existing reports and manual live overrides are
cleared. The original project is unchanged.

Run the saved preparation macro once:

```text
prepare-model:0;run-projection:0
```

It loads the existing fitted analysis outputs and prepares projection sampling;
it does not launch Python/Stan fitting. Then use **Results → Run snapshot
sequence**. The replay is configured for 10,000 iterations per snapshot and
output folder `2019fed-turnout1`.

The expanded diagnostic selection contains 60 timestamps in
`live-snapshots/feeds/diagnostic-60-codes.txt`; `starter-codes.txt` contains the
same list. In **Configure snapshot sequence**, replace the original 26 codes
stored in the prepared project with this list. The selection covers the
zero-count baseline, tiny early booths, the evening ramp-up, WA reporting,
overnight counts, declaration counting, receipt closure and final accounts.
It also includes observations before and after TCP accounts are withdrawn
and replaced in Calare, New England, Barker, Grey and Kooyong. The live input
folder is `forecasts/2019fed/live-snapshots/feeds`.

The expanded archive contains 1,559 distinct capture times. The top-level
folder holds 1,254 captures with changed House accounts; 305 consecutive
repeats are retained under `no-data-change/`. Six additional ZIP copies with
identical XML are preserved in its `additional-copies/` subfolder. The
comparison excludes Senate-only updates, generation
timestamps and derived percentages/historical comparison fields, while keeping
count corrections, identities, availability and status. `source-manifest.json`
records source copies, hashes and selection decisions. The full retained
timestamp list is `live-snapshots/all-data-change-codes.txt`. The original
26-code list is preserved in `live-snapshots/sequences/`; one of its captures
became redundant after an earlier identical account was added, so it should
not be reused without resolving its codes again.
Filtering does not erase an A→B→A correction. An unchanged numerical account
can still affect forecasts through elapsed counting time; a data-change
checkpoint is not a replay of every received observation.

## Verification

The prepared project is independently parsed to its exact end, with original
model/projection series checked byte-for-byte. All archived feed identities
are covered by the preload. Production portable readers accepted the package,
turnout prior and the original 26 selected feeds; the independent mapping was
tested for seat isolation, ambiguous names and unchanged counts. The expanded
60-code selection has been checked for unique, ordered, available files and
preload-compatible candidate and booth identities. Separate parsed-record
checks also agree with the filtering decisions on sampled redundant captures.
These checks verify setup and input handling, not the calibration of resulting
win probabilities.
