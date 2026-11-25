# Starting estimates in Victorian Labor–Greens contests

This note explains how the 2022 and 2026 Victorian forecasts use two possible
final counts to estimate primary votes before counting starts. Both elections
use the same calculation. Labor's two-party-preferred vote (TPP) describes a
contest with the Coalition. In a district likely to finish Labor versus Greens,
the ranked count supplies additional information about which primary splits
are plausible. For instance, moving a point from Labor to Liberal leaves
some of that point with Labor against the Greens when Liberal voters transfer.

## How much the second contest matters

Preparation runs an unadjusted forecast with the current projection, party
settings and betting odds, using up to 4,096 simulated elections. The relevant
fraction is the number ending Labor–Greens divided by **all** draws in that
district. The adjustment starts above 30%, reaches half weight at 65%, and
reaches full weight at 100%. These thresholds are modelling choices. The
calculation applies where Labor, Liberal and Greens stand and the Nationals do
not. A live run reuses its zero-count adjustment across captures with matching
preparation inputs.

The forecast divides the existing combined primary vote of Labor, Liberal and
Greens between those three parties. Moving a well-established primary share
costs more in the fit than moving one with a wide forecast range. TPP may move,
and the Labor–Greens outcome may move according to the district's Liberal
ticket. A three-percentage-point change in either final share sets the scale
for the fit. The pairing frequency tapers both the contribution of the second
contest and the overall revision. Normal statewide balancing, calibration to
betting odds, and live count updates follow. The log reports the starting
changes under `Soft pairing` for each eligible district.

## Evidence available before polling day

The previous Victorian election supplies district primaries, a Greens support
benchmark and Labor's TPP against the relevant opponent. Where the historical
district and opponent match, the calculation retains half the unexplained
local difference in preference rates, measured on the log-odds scale. A
missing or incompatible result supplies no correction. The Greens preference
estimate also varies with their primary strength using the relationship in
classic divisions at the 2019 federal election.

For the grouped Others vote, nominations identify which parties stood.
Pre-election statewide party flows give an estimate for each, with equal
shares of the unresolved vote across nominees and the general Others rate as
the fallback. A single named independent can receive a tactical-voting
adjustment. One Nation has a separate projected primary in 2026. Its historical
preference rate is taken from the year of the historical comparison.

The prepared files live under `analysis/Preferences/pairing/`. Refresh them
from the repository root after changing the nominations or source assumptions.
The last argument is the VEC's all-district TPP workbook from the **previous**
election. The filenames below are examples.

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/prepare_vic_pairing.py 2022vic downloads/2022vic_candidates.xml tmp/vec-2018-tpp.xlsx
.\analysis\.venv-win\Scripts\python.exe -B analysis/scripts/exploratory/prepare_vic_pairing.py 2026vic downloads/2026vic_candidates.xml tmp/vec-2022-final-replay-tpp.xlsx
```

The underlying counts are available in the [VEC 2018 summary](https://itsitecoreblobvecprd01.blob.core.windows.net/public-files/historical-results/state2018/summary.html)
and [VEC 2022 district results](https://www.vec.vic.gov.au/results/state-election-results/2022-state-election-results).
