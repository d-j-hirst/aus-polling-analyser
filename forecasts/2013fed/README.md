# 2013 federal live replay

This setup runs the 7 September 2013 House count through Australian Election
Forecasts. It uses the archived 2013 counts, the 2010 results as a comparison
and the existing 2013 national polling trends.

## Run the diagnostic sequence

Open `Federal Election 2013 turnout replay.pol2` from the repository root.
Prepare the stored model and projection with the project's macro:

```text
prepare-model:0;run-projection:0
```

Then use **Run snapshot sequence** on Results. The project has 62 captures
selected and writes to `2013fed-turnout1`. The selection covers election night,
the declaration count and the final return. The full checkpoint list is in
`live-snapshots/all-data-change-codes.txt`. It retains 1,145 captures, dropping
321 consecutive copies of the same House data from the sequence. The underlying
ZIP files are kept. Changes in pairing, published counts, declarations and
the final results phase remain in the list.

## Ballots and earlier results

The 2013 preload defines the roll, ballot IDs and reporting units for all 150
divisions. The Light archive omits candidate and booth descriptions. The local
preload attaches the same candidate identities from the archived Verbose
return and booth descriptions from the AEC polling-place directory. The
preload counts remain zero. The 2010 Light final return supplies the baseline
counts, with names and party codes attached from AEC polling-place FP downloads.
Three unlisted zero-vote locations in that return contribute no baseline votes.

The pre-election simulation also reads the checked 2010 district results
export. Its older cache covered 124 divisions. The local cache and export
now include the AEC's final FP and TCP accounts for the 26 omitted WA and SA
divisions. The restored counts supply the major-party starting shares in
those seats.

The previous TPP margins are the AEC's historical comparisons on the 2013
boundaries. State baselines use 2010 final TPP and state weights use the 2013
preload roll. The replay keeps Liberals and Nationals separate, with the
Queensland LNP and NT CLP in the Liberal group. Dual contests use the 2010 FP
split where both parties stood. The starting National share is 70% in
National-held Mallee and 10% in other newly contested Liberal–National seats.
These are broad starting assumptions.

The Liberal aliases include both `LNP` and the 2010 feed's `LNQ` code for the
Queensland Liberal National Party. This lets the live comparison use the actual
2010 Labor/LNP TCP as TPP, rather than treating LNP primaries as minor-party
votes and estimating their preferences.

Palmer United keeps its separately modelled 2013 national series under the
code `UAP`. Fairfax uses the existing prominent-minor path. Indi uses Cathy
McGowan's 23.3% primary in the August campaign poll with moderate weight.
Kennedy carries Bob Katter's 2010 Independent support into his 2013 KAP
incumbency. The model uses generic non-classic preference assumptions, with
35% Liberal/National flow to Greens against Labor based on the change in
recommendations before the election. No final 2013 vote shares set those
assumptions. See the contemporary [Indi campaign guide](https://www.abc.net.au/news/elections/federal/2013/guide/indi)
and [Melbourne campaign guide](https://www.abc.net.au/news/elections/federal/2013/guide/melb).

## Turnout

Both archives identify ordinary pre-poll votes at individual centres and
declaration pre-polls at district level. The existing federal categories and
matching rules apply. Earlier local centre sizes guide the initial distribution
among new centres. The AEC's 2,317,323 early votes issued by 6 September constrain
the national early pool, with 2010 overall formality as the starting conversion.
The earlier issued series stops two days before the 2010 election, so its
conversion to final counts is unsuitable for the 2013 eve total. The prior
uses broad assumed uncertainty of 12% for the national conversion and 40%
for local allocation. Previous district counts inform the split. Postal
applications use the existing pre-2013 conversion estimates. The AEC's
receipt deadline was 18:00 on 20 September. [AEC timetable](https://aec.gov.au/Elections/federal_elections/2013/timetable.htm)
