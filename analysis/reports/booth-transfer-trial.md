# How far should early booth results move the forecast?

This trial asks whether a widely shared early FP shift should move the remaining election-day vote faster, and whether a shift concentrated in a few booths should have less influence on early and declaration voters. Shepparton, Bendigo and Mildura motivated the rules. All three are excluded from the broader scores. The analysis uses saved forecasts and counts. It makes no changes to the simulator.

## The four comparisons

The control is the split ordinary-booth update. Each election-day booth retains its opening party mix, adjusted for the statewide and regional movement known at the capture. Counted ballots enter in full. When predicting other voters, the opening estimate has the influence of 250 effective voters for election-day booths and 1,000 elsewhere. A real booth of 500 contributes 250 effective voters about other locations.

- **Faster ordinary update.** Compare each candidate’s FP with the opening estimate at the same places. Check the ballot-weighted middle booth, the average after omitting the two biggest contributors to the shift, and separate averages in areas of lower and higher expected support. Each half needs at least two reporting booths. Also check that the primary gap against the most likely forecast TCP opponent for that candidate moves in the same direction after removing its two biggest contributors. For a well-supported shift the ordinary fit can use a starting weight as low as 62.5. The response grows gradually from a four-point to a ten-point FP surprise and from three to six reporting booths.
- **More cautious transfer to other voters.** Reduce the local part of the adjustment for a vote type with no FP yet when the booth comparisons disagree. Keep the election and regional movements. A diagnostic retaining less than half the average shift starts to reduce the transfer. When it reverses, up to 75% of the local correction can be removed. The response tapers for fewer than six booths and for deviations smaller than five points. Insufficient coverage of higher- or lower-support areas alone does not trigger restraint.
- **Both.** Apply the first change to outstanding ordinary booths and the second to outstanding voters of the eligible other types. Balance candidate shares at each location and retain measured ballots.

The trial constants are deliberately different enough to expose the effect of each change. Their values have not been fitted to election outcomes. A separate, narrower check requires **both** the middle booth and the average after removing the two biggest contributors to point in the opposite direction to the full average before moderating transfer to other vote types.

## The three motivating seats

The table gives district mean FP and the probability of winning the seat, in percent. The control columns use the published replay. The alternatives estimate the incremental effect of the changed FP without rerunning the preference distributions. Absolute win chances in the alternatives are consequently approximate. Times are local on election night.

| Seat and candidate | Time | Current FP | Faster FP | Cautious FP | Both FP | Current win | Faster win | Cautious win | Both win |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Shepparton NAT | 19:15 | 23.82 | 24.12 | 23.77 | 24.07 | 28.74 | 31.08 | 29.28 | 31.64 |
| Shepparton NAT | 19:30 | 24.39 | 24.57 | 24.34 | 24.51 | 33.92 | 35.60 | 34.24 | 35.93 |
| Bendigo NAT | 19:56 | 25.29 | 25.37 | 25.29 | 25.37 | 0.99 | 1.07 | 0.99 | 1.07 |
| Bendigo NAT | 20:29 | 23.39 | 23.42 | 23.20 | 23.23 | 0.25 | 0.26 | 0.21 | 0.21 |
| Mildura IND | 19:15 | 45.12 | 45.12 | 44.51 | 44.51 | 98.96 | 98.96 | 98.61 | 98.61 |
| Mildura IND | 19:30 | 44.45 | 44.45 | 43.35 | 43.35 | 99.55 | 99.55 | 99.06 | 99.06 |
| Mildura IND | 21:00 | 35.22 | 35.22 | 35.34 | 35.34 | 28.82 | 28.81 | 29.54 | 29.53 |

In Shepparton at 19:15 the faster update increased the Nationals’ chance of reaching the TCP from 51.1% to 53.4%. Their conditional chance of beating Suzanna Sheed increased from 56.3% to 58.2%. At 18:45 neither update altered Shepparton or Mildura, each of which had only two reporting booths.

The latest federal sequence begins local counting in Bendigo at 19:55. For an earlier check, reconstruct the same four FP methods at 19:13 from the ten reporting booths in `2025fed-testing2`. They give 13.94%, 14.38%, 13.78% and 14.22% respectively. These are standalone FP reconstructions on that archive’s opening, separate from the published forecasts in the table. By 19:56 the Nationals were already in the final two throughout the saved simulation. Over 8,200 effective ordinary voters then outweighed the ordinary fit’s starting 250, and only 19% of outstanding voters belonged to ordinary booths. Changing that starting weight has little leverage over the remaining seat projection.

In Mildura at 19:30 the cautious transfer cut Cupper’s median TCP against Benham from 59.87% to 58.70%. Given the width of the archived TCP distribution, this left Cupper highly favoured. By 21:00 the effect had changed sign. Applying a similar restraint after the reversal also slowed the subsequent movement away from her.

## How the changes score elsewhere

The main comparison uses all eight `2022vic-testing6`, seven `2025fed-testing3` and nine `2026sa-testing2` captures. Score captures with some ordinary FP. For district FP error, add the absolute differences between the forecast and final primary shares for all named groups and the remainder, then divide by two. For example, moving one percentage point from Labor to Liberal gives one point of error. Average the captures within each district before averaging districts. The next-return checks weight each new booth or vote category by its formal ballots within its district. Those checks compare reconstructed booth projections.

For TCP there are two different probability questions. The final-pair Brier score sums the squared errors in the probabilities assigned to all pairings. The conditional-winner Brier score is the squared error in the probability of winning given the actual pair. A smaller score is better within each row. Score only races where the identities can be aligned. The three design seats are excluded.

| Measure | Other districts | Seat/capture observations | Current | Faster ordinary | Cautious transfer | Both |
|---|---:|---:|---:|---:|---:|---:|
| District FP error, pp | 281 | 1,408 | 2.9746 | 2.9730 | 2.9783 | 2.9766 |
| Final-pair Brier | 280 | 1,402 | 0.06118 | 0.06131 | 0.06135 | 0.06148 |
| Conditional-winner Brier | 280 | 1,402 | 0.03387 | 0.03382 | 0.03346 | 0.03341 |
| Final-pair Brier where the two leading pairings could exchange a finalist | 140 | 485 | 0.10509 | 0.10531 | 0.10546 | 0.10567 |
| Conditional-winner Brier where the initial conditional win chance was 5–95% | 113 | 435 | 0.08835 | 0.08826 | 0.08736 | 0.08725 |

The ordinary change moved next-ordinary-booth FP error from 6.541 to 6.545 pp (269 districts). The cautious transfer moved error at the next non-ordinary returns from 7.245 to 7.250 pp (166 districts). In paired resampling of whole districts, the 95% intervals for the overall district FP and Brier differences all included zero. Districts in the same election can respond to a common political movement.

There were individual gains and losses behind the averages. At 20:29 in Wills the cautious transfer lowered district FP error from 1.82 to 1.05 pp. At 19:56 in Melbourne it increased that error from 4.23 to 4.85 pp. In SA the faster ordinary change reduced average FP error from 3.916 to 3.906 pp, whereas federally it increased from 2.028 to 2.030 pp. The results give little support for deploying either general rule.

### Longer series and a narrower restraint

As a separate FP check, reconstruct the current split and the alternatives on the 35-capture `2022vic-2-initial`, 35-capture `2025fed-preferences11` and 153-capture `2026sa-changes2-261122` archives. Include seat/captures only before the first non-ordinary FP in each district so all versions have the same source of local evidence. The three design seats remain excluded. These runs cover the same elections and use earlier software, so they provide a timing check rather than three further election results.

Across 4,956 eligible seat/captures in 243 districts, average FP error was **3.284 pp** for the reconstructed control, **3.281** for faster ordinary updates, **3.302** for cautious transfer and **3.299** for both. The cautious transfer increased FP error in each election. For the first newly reporting non-ordinary votes, error increased from 7.417 to 7.463 pp.

The narrower restraint tested after the main comparison had virtually no overall effect in the latest runs. On its own it moved district FP error from 2.9746 to 2.9751 pp, final-pair Brier from 0.061184 to 0.061174, and conditional-winner Brier from 0.033865 to 0.033862. On the longer series its district FP error was 3.2844 pp against 3.2842 for the control. Its estimated win chance for Cupper was 98.57% at 19:15 and 99.48% at 19:30. The three selected examples give little basis for choosing a threshold.

## Interpretation of the probability estimates

The primary comparison applies each trial’s *change* to the actual district FP forecast. Where the native archived fit is unavailable the relevant group receives no direct change. Where other vote types have already reported, the native ordinary share of the evidence scales the change. The old-build FP check instead reconstructs all four estimates from the same opening and snapshot.

The archives contain marginal distributions. They do not retain every draw’s FP, rankings, pair and winner together. Two approximations make the TCP sensitivity explicit.

1. For the two most likely pairs sharing one finalist and accounting together for at least 90% of draws, shift the contest for the remaining place by the FP gap change between the rivals. The rarer alternative needs at least 1% support. The current probability sets the starting point. The archived FP ranges estimate the spread, with a working correlation of −0.35 between rivals. Using zero or −0.65 gives the same ordering of the four pairing scores in the contested subset. Other pairings retain their published mass. Very rare unlisted pairings share one remainder for scoring.
2. For a given final pair, retain matched FP/TCP ballots, the earlier effective preference rate on remaining ballots, and the archived spread around the median. Where both Coalition parties ran, assume 80% of the excluded partner’s votes go to its partner against Labor. Shift the centre and interpolate the probability at 50% using the archived percentile bands and exact archived win frequency. An unidentified remaining flow prevents scoring that conditional probability. The sensitivity does not rerun exclusion orders, change preference behaviour, or jointly resample turnout and uncertainty.

## Reproduction

The following PowerShell commands read existing replay archives from the repository root and write compact score files in the temporary build directory.

```powershell
$dir = 'cli-build/booth-revision-trial'
New-Item -ItemType Directory -Force $dir | Out-Null
$python = '.\analysis\.venv-win\Scripts\python.exe'
$trial = 'analysis/scripts/exploratory/score_booth_transfer_trial.py'
$files = foreach ($run in '2022vic-testing6','2025fed-testing3','2026sa-testing2') {
    $file = "$dir/variants-$run.json.gz"
    & $python -B $trial $run $file | Out-Host
    $file
}
& $python -B $trial --summarise $files
```

For the earlier archives use `--historical`. For the narrower sensitivity use `--reversal-only` and a different output filename. Summarise each configuration separately.
