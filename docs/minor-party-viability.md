# Local minor-party candidate viability

This document explains how a manually assessed candidate profile changes local
first-preference (FP) support in Australian Election Forecasts. It supplements
the ordinary party model where a candidate has unusual prominence or campaign
support. It is an assumption supplied by the forecast operator, not observed
votes or a measured probability of winning.

## Settings and scale

Within a seat's entry in `analysis/seats/`, a setting such as
`sMinorViability=ONP,1;IND,-1` gives One Nation a positive assessment and the
locally modelled independent a negative assessment. Codes resolve to the
project's configured parties, including aliases. The setting does not itself
confirm that a candidate is running; ballot and candidate-eligibility rules
continue to apply.

Zero, or an omitted rating, means the ordinary model. The inherited multiplier
is `1 + 0.4 * rating`: +1 gives 1.4, while -1 gives 0.6. The 0.4 coefficient is
an existing modelling assumption, not a new fit to One Nation or other parties.
Suggested profile considerations are recorded beside `Seat::minorViability` in
`Seat.h`; ratings can also represent evidence of an unusually weak campaign.

## How the different candidate calculations use it

- Greens and continuing independent-style candidates scale their initial
  historical FP share. This can also change the historical support band used
  to estimate local variation. Returning unsuccessful IND candidates retain
  their existing exemption: their observed previous primary already captures
  the profile being carried forward.
- New confirmed prominent independents scale the spread of their positive
  support draws above the emergence threshold. This raises or lowers the
  prospect of a strong campaign, rather than multiplying a historical share
  that does not exist.
- Centrist/populist parties, including One Nation, scale the local starting
  share produced from their overall support, geographic modifier and any
  incumbent baseline. For example, 15% becomes 21% at rating +1 before regional
  and local variation. This calculation previously omitted viability entirely.
- Prominent-candidate supplements use the multiplier for the small-primary
  supplement and its square for the variable additional boost. The
  centrist/populist calculation now follows the existing continuing-candidate
  scaling while retaining its own original bonus calculation. Its supplements
  cannot become negative or grow again at extreme negative ratings.

These paths describe different sources of local support, so their final
distributions need not move by identical percentages. Local FP percentages in
the centrist/populist baseline retain numerical bounds of 0.01--99% before the
log-odds transformation. These bounds keep the calculation finite; they are not
guaranteed final candidate shares.

## Greens standing after missing a district

The ordinary Greens calculation starts from their previous district result.
When the Greens field a candidate in a seat they previously missed, the model
instead needs a starting estimate of their share of formal first preferences.
The result is missing even though the previous election's total formal vote
is known. A recorded Greens result rounded to zero still uses the ordinary
historical calculation.

When there is comparable older evidence, an election can supply a median
percentage by current district name in `analysis/seats/<election>-greens-fp-priors.json`.
The SA replay uses 2018 votes on the 2022 boundaries, all available before
the 2026 election. The estimates are 3.1% in Narungga, 2.5% in MacKillop,
3.1% in Mount Gambier and 5.6% in Ngadjuri. The last figure uses redistributed
Frome because the original district included Port Pirie. Sources are the
2022 election guides for [Narungga](https://www.abc.net.au/news/elections/sa/2022/guide/naru),
[MacKillop](https://www.abc.net.au/news/elections/sa/2022/guide/mack),
[Mount Gambier](https://www.abc.net.au/news/elections/sa/2022/guide/moun) and
[Frome](https://www.abc.net.au/news/elections/sa/2022/guide/from).

Without a local estimate the model assumes a median of 2%. Skipping a
district previously suggests relatively weak support, but leaves considerable
uncertainty. Both kinds of estimate use a normal distribution on the model's
log-odds scale. Around the 2% median, the middle 90% spans approximately
0.5–7.7% before any other evidence. These are assumptions about support when
standing. A known ballot, or the usual minor-party contest selection before
nominations are known, determines whether there is a candidate in that draw.

Local viability, odds and seat polls still apply to the resulting estimate.
The election-wide FP target is reconciled across seats as usual. The starting
estimate supplies no historical votes to turnout, swing or live booth comparisons.
Published intervals can differ from this initial range once these steps and
live counting evidence have been applied.

## Candidates grouped into Others

Once nominations are known, the forecast also uses the size of the local field
to distribute the election-wide Others vote. It counts grouped minor candidates
and independents on both ballots. For example, an increase from two candidates
to four multiplies the odds of an Others vote by `(5 / 3)^(1/4)`, about 1.14.
The added one allows for a previously empty field. The quarter power allows
for candidates competing for the same voters. Districts with a sitting,
returning or confirmed prominent independent skip this field-size adjustment.

An election may supply local evidence for a named party included in Others in
`analysis/seats/<election>-grouped-fp-priors.json`. For each district and party,
`party_votes` and `formal_votes` describe the same historical ballot sample.
In Bendigo East, One Nation received 1,188 of 19,101 formal votes at matched
ordinary polling places in the May 2022 federal election. The model moves one
candidate's allowance halfway towards that 6.22% district estimate. The half
weight is an assumption about how well support carries between elections.
The same rule uses the remaining grouped candidates when a draw gives an
independent or emerging party a separate share. [Victorian inputs and source](../analysis/seats/2022vic-grouped-fp-priors.json)

Each draw balances the resulting district changes using historical turnout,
preserving the combined Others vote at this step. The simulation then infers
major-party first preferences and later incorporates live counts as usual.

## Polling, odds and the overall forecast

The baseline profile adjustment precedes local FP-poll integration. Polls can
therefore moderate the manually assessed support; see
[seat FP polls](seat-fp-polls.md). Prominent-candidate supplements retain their
existing later stage. The odds-calibration procedure includes viability inside
the local support calculation it calibrates. No extra viability adjustment is
applied after statewide reconciliation.

The setting does not change the party's election-wide support target, contest
ranking or statewide FP reconciliation. An increase in one seat can be offset
by changes elsewhere. It also does not set a live-count override: live vote
evidence subsequently updates the resulting prior in the usual way.

## Verification

`tests/MinorPartyFpMathTests.cpp` compares the zero-rating baseline and prominent
bonuses exactly with the previous centrist/populist arithmetic. Controlled
positive/negative cases check the incumbent baseline, finite bounds, prominent
supplements and interaction with the local poll blend. The tests are included in
`run-portable-tests.cmd` and `run-portable-tests.sh` and do not run an election
forecast.
