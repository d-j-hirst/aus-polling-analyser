# Estimating One Nation’s outstanding first preferences

The live forecast uses the 2025 federal House results to estimate One Nation’s share of outstanding ordinary and declaration votes. An ordinary booth that reports as expected gives little reason to revise the rest of the district. Where absent or postal voters historically differed from home voters, the forecast allows for that difference in the votes still to come. Victoria uses both comparisons for the 2026 rehearsal and election. [SA tests of the outstanding vote](../analysis/reports/2026vic-one-nation-booths/baseline.md)

## Before votes arrive

1. The district’s pre-count forecast median sets One Nation’s share of all formal first preferences. The five Victorian districts where the party previously stood have measured state results that can distinguish ordinary booths from home early centres. In the other 83 districts those two groups start at a common level. The Victorian declaration estimates described below apply in all 88 districts.
2. At each ordinary state venue, pool the One Nation and total formal ballots from the three closest distinct federal ordinary sites. Federal booths sharing coordinates count as one site. The 2026 SA fit transfers 0.724438 of the difference in federal log odds to Victoria. For example, twice the federal odds of a One Nation vote corresponds to about 1.65 times the Victorian odds. A missing location or a nearest site more than 10 km away receives the district ordinary average. Early centres, mobile teams and postal votes keep their voting-type estimate. [Geographic comparison and source data](../analysis/reports/2026vic-one-nation-booths/README.md)
3. Scale ordinary venue estimates to give the intended ordinary share, and voting types to give the intended whole-district share. Both calculations use the turnout model’s pre-count expected formal ballots. The five Victorian districts with previous One Nation candidates use the same federal ordinary pattern as the other 83. The other candidates divide the ballots remaining at each location, with their relative district totals preserved across all locations.

## Victorian declaration votes

For each Victorian district, the reference is One Nation’s share of formal first preferences across ordinary election-day booths and home early centres combined. Comparing each declaration group with home voters within the same federal division accounts for differences in which divisions cast more absent or postal votes. The 2025 House results provide one Victorian estimate for each group across 38 divisions. The transfer weights are modelling choices informed by the Queensland and South Australian comparisons. [AEC results by candidate and vote type](https://results.aec.gov.au/31496/Website/Downloads/HouseFirstPrefsByCandidateByVoteTypeDownload-31496.csv) · [Historical transfer tests](../analysis/reports/2026sa-late-one-nation/README.md#which-weights-does-the-sa-replay-use)

| VEC group | Federal difference carried over | Expected One Nation share if home voters give 20% |
| --- | --- | ---: |
| Election-day `Absent` | Full difference for federal absents, +0.29557 in log odds | 25.15% |
| `Postal` | Half the federal postal difference, +0.11042 in log odds | 21.83% |
| Residual `Early` | Zero, using the home in-person share | 20.00% |

Odds express One Nation ballots relative to ballots for all other candidates. Applying half the federal difference on the log-odds scale gives the same proportional change in odds at different levels of party support. The calculation finds the home share that makes the turnout-weighted sum across **all** booths and vote types reproduce the district median. Home centres and ordinary booths can differ from each other while retaining that combined home share. Mobile votes have a separate count and receive no federal declaration adjustment. [Victorian declaration parameters](../forecasts/2026vic/one-nation-declaration-prior.json)

As home results arrive, the forecast updates that reference using the counted ballots and the projected remainder at home booths and centres. Each declaration group’s own results then gain weight in the estimate of its outstanding ballots. If a declaration reports first, the forecast blends its batch with that group’s pre-count estimate. Residual early batches use the slower learning rate for a district early total that can accumulate voters from different places. Every accepted first preference stays with its candidate.

## When an ordinary booth reports

The forecast compares the observed One Nation ballots with the number expected at the reporting locations, using the formal volumes they actually report. It finds the common change in odds that matches those ballots. A remote booth represents an average mix of its district’s ordinary voters. The model uses how much of the count is available when sharing this movement across the district, region and state.

The district also has an estimate based on current vote shares. For One Nation this estimate applies the measured change to the specific locations still outstanding, weighted by their remaining formal ballots. A separate early or postal batch can update its own category. For example, if an ordinary booth reports its expected 40% and the outstanding booth was expected to give 10%, the first return supplies zero additional movement to the second booth. All accepted candidate counts remain fixed. Turnout and the composition of future votes can still vary across simulations. The geographic strength is held fixed within a run.

## Victorian preparation

The standard Victorian preparation command refreshes the geographic input after refreshing the VEC roster and the turnout prior. Both live projects read `forecasts/2026vic/live-inputs/one-nation-booths.json`. For a geography-only rebuild with the current local setup files, run the following from the repository root. The input records the source hashes. A watched run includes the prepared file in its input revision. [Victorian rehearsal and election instructions](../forecasts/2026vic/README.md)

```powershell
.\analysis\.venv-win\Scripts\python.exe -B live_scripts\prepare_one_nation_booths.py 2026vic
```

## Isolated South Australian replay

Open the new watcher project under `live_runs/2026sa-federal-booths` in an Analyser build containing the geographic comparison. Press **F1**, run its saved model and projection macro, then select **Results → Watch / Resume**. From the repository root, the first command below checks the capture list. Each invocation of the second delivers one capture to this replay’s own inbox. The first capture is the original zero-count feed.

```powershell
.\live_runs\2026sa-federal-booths\Next.ps1 -DryRun
.\live_runs\2026sa-federal-booths\Next.ps1
```

The new run has private visibility, separate history, and its own `live-inputs/one-nation-booths.json`. Each SA district uses a strength fitted from districts outside its dominant federal division. This makes the replay a retrospective test using other SA districts’ eventual results. Its location inputs contain federal counts, and the watched model receives the SA count through the selected captures.

All SA 2026 replays also read the checked-in One Nation declaration prior. Once a district reports home-district first preferences, its estimated combined polling-day and early vote supplies the level for outstanding absent and postal votes. Both absent groups receive the 2025 SA federal difference in full and postals receive half, with the district’s own category returns gaining weight as they arrive. The term-wide prior is fingerprinted separately from the optional ordinary-booth file. [SA calibration and checkpoint results](../analysis/reports/2026sa-late-one-nation/README.md#which-weights-does-the-sa-replay-use)

With the prepared SA turnout weights, the mean absolute error in One Nation’s **combined outstanding ordinary share** across 47 districts was **2.67 percentage points** at the first update with at least one booth reporting 100 formal votes, and **1.17 points** at the first update with at least three such booths. Carrying the counted share straight across gave **3.26** and **1.78** points respectively. These scores assess the geographic component on actual returns. The full live forecast also uses the pre-election district forecast, other voting types and evidence shared across districts. The read-only check below reproduces the comparison using the locally recorded SA first returns. [Study results and instructions for preparing the local inputs](../analysis/reports/2026vic-one-nation-booths/README.md#reproducing-the-study)

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\reports\2026vic-one-nation-booths\check_prepared_replay.py live_runs\2026sa-federal-booths\live-inputs\one-nation-booths.json
```

To prepare another comparison, use a new run ID and specify the saved SA project whose model and simulation settings should be copied. The command reads the archived captures and shared turnout prior. The new watcher selects its own ordinary-booth file and uses the same declaration prior as other SA replays.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B live_scripts\prepare_one_nation_booths.py 2026sa --new-replay 2026sa-federal-booths-2 --project ".\live_runs\2026sa-1-late-movement\2026sa-1-late-movement.pol2"
```
