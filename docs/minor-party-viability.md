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
