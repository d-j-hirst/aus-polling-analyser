# Federal 2022 live replay

This package prepares the 2022 federal election for the current automatic live
forecast. It supplies ballot identities, separate Liberal and National parties,
and a turnout prior based on information available before counting. Received
feeds and generated replay outputs stay in the operator's private folders.

The authored seat file retains its original pre-election margins, polling and
campaign assumptions. Its running-party lists now match the AEC's candidate
preload, including the number of independents and unmodelled minor candidates.
For example, `IND*` means two independent candidates and `OTH***` means four
other candidates. Candidate names identify the representative of each modelled
party. Named independent entries are limited to the authored seat file's
originally confirmed prominent independents and existing independent incumbents.
Other independents remain in the running-party lists and retain their separate
count identities in the live feed; appearing on the ballot does not give them a
prominent-candidate prior.

The returning defeated-independent flag applies only when that same named
prominent independent stood in 2019. A different minor independent returning
in the same seat does not qualify the prominent candidate. Wannon's Alex Dyson
is the only such returner in this configuration. Independent incumbents retain
their usual path.

## Coalition contests

Liberal, LNP and CLP codes map to the existing `LNP` model party. `NP` and `NAT`
map to the separate National party, which has the normal Coalition relationship.
The fitted national polling series remains the combined `LNP` series. Actual
running-party lists determine candidate availability in each district.

Where both Coalition parties stand, `fNationalsCoalitionShare` supplies the
National fraction of their combined starting FP estimate. This is a share
within the Coalition, not a share of all district votes. The historical starting
fractions are 4.3721% in Barker, 21.2125% in Indi and 18.6210% in Durack, from
their 2019 district FP accounts. Nicholls uses 52.4545%, from the last dual
contest in its predecessor Murray: 31,105 National and 28,194 Liberal votes in
[the AEC's 2016 results](https://results.aec.gov.au/20499/website/HouseDivisionPage-20499-231.htm).
Current counts can replace these starting expectations through the normal live
evidence handoff. Existing nonclassic preference assumptions are also available
for the separate National source and target.

## Turnout inputs

The maintained `2022fed/earlier_only` preset uses training elections before
21 May 2022. Its district rates and resident vote-category proportions come
from previous elections. New PPVC size weights use likely-local centres added
or changed in 2019 compared with 2016, rather than those centres' 2022 final
sizes. Current enrolment and booth identities come from the current preload.

The operator-local preload uses `local:downloads/2022fed-preload.xml`, with its
companion `downloads/2022fed-preload@polling_places.xml`. It preserves the May 3
roster and classifications and adds Lismore Central in Page, present with zero
votes in the received 17:44 election-day source. This keeps the late-added
identity available without using its later count or overwriting the original
remote-cache files. Its location remains unknown where no pre-election
coordinate is available.

The early and postal controls are national observations, rather than falsely
precise district readings. They use 5,541,757 issued pre-polls and 2,731,060 postal
applications recorded in [the election-eve early-vote summary](https://antonygreen.com.au/5647-2/).
The model converts these to expected formal counts and distributes them using
previous resident-category rates and the current roll. It retains uncertainty
in both the conversion and the district distribution. A centre's issuing
district is not substituted for its voters' home districts.

The timetable uses eastern polling close on 21 May at 18:00 and postal receipt
closure on 3 June at 18:00, from [the AEC's 2022 timetable](https://www.aec.gov.au/Elections/federal_elections/2022/timetable.htm).
Receipt closure does not imply that counting has finished.

With the private normalized configuration present, regenerate the fixed prior
from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_prepare_live "..\forecasts\2022fed\live-inputs\turnout-config.json"
```

This prepares 256 count outcomes for the existing C++ interface. It does not
fit Stan or run forecast simulations. The configuration and its source hashes
are retained locally beside `turnout-prior.json`.

## Select a checkpoint

Use the normal snapshot sequence dialog with
`forecasts/2022fed/live-snapshots/feeds/`. Detailed and Detailed/Light captures
retain their received XML bytes and original source clocks. The archived ZIP
series alone begins its election-night counting at 23:02; earlier original XML
captures can supply the missing evening without reconstructing or interpolating
results.

For a shorter data-change checkpoint, the top-level folder can retain the first
capture of each changed House account. Repeated accounts are kept in the
`no-data-change/` subfolder with a hash manifest, and `data-change-codes.txt`
provides the complete retained selection. Count corrections and records
disappearing or returning remain in order. Senate-only changes, generation
timestamps and derived historical-comparison fields do not require another
House forecast run. An unchanged numerical account can still produce a later
forecast change through elapsed counting time; that cadence is outside a
data-change checkpoint.
