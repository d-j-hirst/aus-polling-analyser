# Using first-preference seat polls

This document explains how local first-preference (FP) polls update the seat
distributions used by Australian Election Forecasts. They provide local evidence
alongside the broader party forecast; they do not replace the statewide or
national polling projection. The same treatment supplies pre-election forecasts,
nowcasts and the pre-count distributions used by automatic live forecasts.

## Inputs and supported candidates

Seat files under `analysis/seats/` can contain, for example:

```text
sPolls=ONP,40,1;GRN,13,1
```

Each entry gives a configured party code, its FP percentage of the seat's formal
votes, and a credibility rating. The final `1` is a rating, not the poll's sample
size. Party aliases resolve to the project's party indices. Sample sizes,
fieldwork dates and poll sources in accompanying notes are not additional inputs
to this calculation.

Greens and other named non-major parties use their reported poll percentages.
The existing IND treatment retains its reading adjustment and eligibility rules
for confirmed prominent and returning unsuccessful independents. A poll does not
itself establish candidacy: ballot-presence and recontest checks still determine
whether a candidate receives a generated primary vote. Anonymous emerging groups
do not inherit a named party's poll.

Major-party FP polls and separate Nationals FP polls are outside this mechanism.
Their primary shares are supplied by the major-party TPP/preference calculation
and the Coalition split respectively. TPP seat polls use their separate existing
calculation.

## Averaging and combining the evidence

For a poll with credibility rating `c`, its quality weight is `0.6^c`. Lower
ratings mean stronger evidence. The poll target is the quality-weighted average
of its percentages, and its combined contribution is:

```text
contribution = 0.8 * (1 - 0.6 ^ sum_of_quality_weights)
```

These weights are inherited modelling choices from the independent method, not
newly fitted coefficients for Greens or other named parties. Multiple readings
raise the contribution towards an 80% maximum. Correlation between readings from
the same pollster or survey is not modelled separately.

The forecast blends each generated seat share with this fixed target on the
transformed vote-share scale, `25 * log(p / (100-p))`, where `p` is a percentage.
This scale allows blending without producing percentages outside zero to 100.
Poll averages are bounded to 0.1--99.9% before transformation so genuine endpoint
readings do not produce infinite values. This is a numerical safeguard, not a
minimum share imposed on candidates.

One credibility-1 poll receives a 21.12% contribution. In isolation, a 40% poll
would move a 15% generated median to about 18.94%. That example precedes seat
normalization, prominent-candidate adjustments and statewide reconciliation; it
is not a prediction of the final displayed median. Blending also narrows the
distribution: differences between generated shares shrink by about 21.12% on
the transformed scale in this example. There is no separately sampled poll error.

IND readings retain the inherited empirical adjustment
`0.503 * reported_percentage + 15.59`. This relationship concerns historical
independent/pseudo-independent polls and is not applied to named-party readings.
For example, a reported 13% Greens reading remains 13%, rather than becoming
22.13%. The constants and the existing independent call sites are unchanged.

## Placement in the forecast

`SimulationPreparation::loadSeatPolls` validates reported percentages and prepares
one target and contribution per supported seat/party. These fixed quantities are
shared read-only; each iteration still generates its own party support, regional
effects and local uncertainty. No generated result is cached with the poll.

`SimulationIteration` applies the update after generating local variation and
before the usual normalization and reconciliation. Polls are skipped while
calibrating that candidate's betting odds, retaining the existing independent
treatment; calibrated odds and polls then contribute in the main forecast.

Named parties retain their ordinary statewide/national FP targets and
reconciliation. A local poll can relocate a party's support between seats, while
an equivalent shift across many polled seats can be moderated by the overall
target. The poll is not reapplied during each reconciliation cycle. Previously
stored named-minor-party readings, including MRP estimates, now participate in
this treatment; the mechanism previously read IND entries only.

The [minor-viability adjustment](minor-party-viability.md) supplies a local
candidate-profile assumption before the poll blend. Poll evidence therefore
moderates that assumption instead of receiving an extra profile adjustment
afterwards. Prominent-candidate supplements retain their later existing stage.

## Verification

`tests/SeatFpPollsTests.cpp` checks exact agreement with the previous independent
arithmetic, separate ONP/Greens readings, credibility weights, accumulated polls,
finite endpoint readings, missing evidence and betting-odds calibration. It also
checks that prepared inputs remain fixed while scenario shares vary. It is
included in the portable test scripts:

```text
run-portable-tests.cmd
```

On Linux/macOS use `sh run-portable-tests.sh`. These tests do not establish the
size of a displayed forecast movement after statewide reconciliation. That
comparison requires the same projection, simulation mode, iteration count and
other settings with and without the seat poll.
