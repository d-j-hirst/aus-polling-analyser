# Choosing finalists when Liberal and National both stand

This document explains how the forecast decides whether Liberal and National
candidates both reach the final two-candidate-preferred (TCP) count. The same
calculation serves pre-election forecasts, live baselines, odds calibration and
live forecasts. It repairs the candidate-pairing decision while retaining the
existing treatment of the Coalition's combined receipt of preferences and the
separate calculation of final TCP shares.

## The question being answered

The forecast generates a combined Coalition primary vote and a Liberal/National
split. During live counting, first-preference (FP) evidence updates the two
candidates separately before their shares are temporarily recombined for
preference allocation.

A combined Coalition pile can be larger than Labor's even when neither Coalition
candidate is individually ahead. Conversely, both Coalition candidates can be
ahead of Labor despite a Labor-versus-Coalition TPP estimate that would previously
have caused the count to stop at that combined pair. The pairing check therefore
compares individual candidate piles before accepting the shortcut that confirms
the combined group's final opponent.

The inputs are one simulated seat's already-generated FP shares, its National
fraction of Coalition primaries, and the existing preference allocator. Shares
are percentages of the seat's formal first-preference vote. The check returns
either two Coalition finalists or the identity of the surviving Coalition
candidate. Identifying that survivor does not itself guarantee that the
Coalition reaches the final pair: the existing count still determines its
contest against outside candidates.

## Counting the candidates while preserving the combined allocation

`CoalitionPairing.h` performs an auxiliary exclusion count on private copies:

1. The existing allocator distributes excluded candidates' original primaries
   among the remaining groups. Liberal and National remain one receiving group
   for this allocation. Adding a second candidate therefore does not give the
   Coalition an extra preference weight against other parties.
2. Each Coalition candidate keeps its own primary votes. Only preferences
   received by the combined group are divided between the two candidates.
   For example, LIB 35% and NAT 25% receiving another 10 percentage points
   become 40% and 30% under an even incoming division.
3. If a Coalition candidate is the lowest, the check returns the surviving
   partner's identity. The main count continues with its existing combined
   treatment against outsiders. This preserves the inherited approximation
   about transfers between Coalition partners; it introduces no new leakage
   or exhaustion rule.
4. Otherwise, the lowest outside candidate is excluded. The next allocation
   redistributes all excluded candidates' original primaries, rather than
   transferring an accumulated pile that would count earlier preferences
   twice. If both Coalition candidates beat the last outsider, they are the
   final pair.

The usual merged-party or single-Coalition-candidate case bypasses this check.
A small but positive second candidate is still considered: there is no minimum
primary-share threshold that makes a candidate eligible for the calculation.

## Uncertainty in the incoming division

The National fraction of incoming Coalition preferences has a median of 50%,
with its central 90% range approximately 25–75%. This is a broad modelling
assumption, not a coefficient fitted to historical results. The same sampled
fraction applies throughout a seat's exclusion count; it represents a shared
tendency across sources, without separately modelling each source's
Liberal/National preference division.

The calculation draws a normal value with mean zero and standard deviation
0.6679 on the log-odds scale, then converts it to a fraction using
`1 / (1 + exp(-draw))`. This scale keeps the fraction between zero and one.
The random draw has its own key, so adding this uncertainty does not change
the existing keyed draws for primary votes or other preference allocations.

The uncertainty decides which candidates survive. It does not replace the
existing final-share assumptions. In particular, a Liberal/National final pair
still starts from each candidate's own FP plus half the other primaries each,
then uses matching live TCP evidence and the existing counted-vote constraints.
The inherited even final-share assumption is separate from the uncertain
division used during exclusions.

## Interaction with live and pre-election forecasts

Labor versus a Coalition candidate continues to use the prepared TPP estimate.
Other pairs use the existing non-classic calculation. A Liberal/National pair
uses matching live TCP integration, because Labor-versus-Coalition TPP cannot
describe that contest.

A preliminary TCP pair is evidence conditional on those candidates surviving;
it does not force the simulated pair. An authoritative final district result
still bypasses exclusions and fixes the FP account, TCP pair, shares and winner.

Pre-election pairing and the Liberal/National winner identity can change,
including where preferences overturn their primary-vote ranking. Calibration
that depends on simulated winners also runs through this calculation. The live
baseline and odds-calibration caches are held in memory, not saved in the
project file: restarting the rebuilt application regenerates them normally.
No project-file format or seat configuration change is required.

## Classic TPP and primary votes in the 2026 Victorian forecast

The forecast uses historical preferences to infer the major-party primaries
that go with a simulated Labor TPP. When a National contested the previous
election, preparation identifies the Coalition candidate in the counted
Labor–Coalition pair. It uses the actual TCP when that pair reached the final
count and the separate published TPP when available for another final pair.
The counts must cover the same formal votes as the historical first preferences.
The seat setting for the previous margin supplies the swing projection. The
counted historical TPP supplies the preference comparison.

In a classic count, the excluded Coalition partner can send some of its original
first preferences to Labor. Remove this contribution before estimating how many
minor and independent ballots flowed to Labor. For example, the last Liberal
transfer in 2022 Euroa sent 1,353 ballots to Labor, and the Liberal pile included
1,172 inherited ballots. Between 181 and 1,353 of the 9,227 original Liberal
ballots therefore reached Labor. That range implies a 40.9–58.5% flow to Labor
among the 6,679 minor and independent primaries. A forecast expectation within
that range needs no historical correction. An expectation outside the range
receives the smallest correction supported by the count. [Euroa distribution](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/euroa-district-results/euroa-results-distribution)

The [2022 source table](../analysis/Data/coalition-preference-history-2022vic.csv)
records the district, formal votes, the three major-party primaries, the
Coalition finalist's party code, Labor TPP votes and the lower and upper bounds
on the excluded partner's original ballots reaching Labor. Bass, Euroa and
Morwell use bounds from the last exclusion. Mildura and Shepparton use the
separate published Labor–Nationals totals and allow the full range of the
excluded Liberal primary, subject to the arithmetic of those totals. The
optional table is captured with the other simulation inputs so a change also
refreshes betting-odds calibration and the pre-count baseline. [Bass distribution](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/bass-district-results/bass-results-distribution) · [Morwell distribution](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/morwell-district-results/morwell-results-distribution) · [Mildura results](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/mildura-district-results) · [Shepparton results](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results/results-by-district/shepparton-district-results)

For current scenarios involving a historical National candidate or two current
Coalition candidates, Labor's own primary, the minor and independent preferences
to Labor and the excluded partner's contribution must add to Labor TPP. One
current Coalition candidate gives zero partner contribution. With two, the
primary solver and the preference comparison share an assumption of 15.4% of
the smaller partner's primary, measured as a share of the formal vote. This
keeps both hypothetical classic counts on the same simulated TPP. The primary
solver includes that quantity once. During statewide FP reconciliation, the
terminal adjustment keeps the split between Labor and the Coalition within the
range that still permits the district TPP, where their combined primary allows
it. Live votes are applied after this forecast calculation.

## Verification and limits

Portable tests in `tests/CoalitionPairingTests.cpp` check the accounting with
small explicit examples: own primaries stay fixed, the combined recipient is
preserved, excluded primaries are not counted twice, a single split draw is
reused, a weak partner is excluded, and numerical errors are rejected. They are
included in the standard portable test runners. To run just this component with
a C++20 compiler, without building or running the forecast application:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror tests/CoalitionPairingTests.cpp -o /tmp/coalition-pairing-tests
/tmp/coalition-pairing-tests
```

The integrated final-result function was also compared with the isolated
prototype at 975 saved input cases across 15 configured elections, 518
primary-split sensitivity cases and 10 boundary cases. Each case used 4,096
identical keyed preference draws; final FP shares, TCP pairs/shares and winners
matched the prototype exactly. These comparisons hold primary and TPP
inputs fixed; they test the integration, not the accuracy of fully regenerated
forecast probabilities. The coverage includes live and pre-election inputs,
comfortable Liberal/National contests, close exclusions, weak second Coalition
candidates, single-candidate controls and authoritative final counts.

The checks do not estimate an optimal incoming-split distribution or establish
calibration across elections. Full replay comparisons additionally exercise
upstream primary uncertainty, odds calibration and live-baseline generation.
