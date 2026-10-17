# Historical seat TPP volatility

This calculation supplies the uncertainty in a seat's TPP after allowing for
movement in its region. It lets a historically variable seat have more local
variation than a stable seat, while retaining an allowance for political
conditions changing. National and regional uncertainty are separate components
of the forecast; these figures are not the seat's total forecast uncertainty.

Each election has its own CSV and explanatory JSON in
`analysis/Seat Statistics/individual-seat-factors/`. Only elections with years
strictly before the forecast election enter its calculation. The CSV supplies
elasticity, local standard deviation and the common tail-shape parameter;
the JSON records the training years, observations and weighting for each seat.
The generation manifest fingerprints the code and historical inputs.

## Historical comparisons and units

Ordinary Labor–Coalition contests use the checked historical TPP share and
swing. Federal non-classic contests also use the AEC's statistical TPP counts,
available in the maintained supplement from 2007 onwards. This does not change
the actual TCP pair or candidate history. Unavailable comparisons are excluded,
with reasons retained in the JSON; missing values are never zero swings.

For a current TPP percentage `p` and comparable swing `s`, the previous share is
`p - s`. This preserves the published redistribution adjustment. Both shares
are transformed using `z(p) = 25 ln(p / (100 - p))`, and their difference is the
seat's transformed swing. Near 50%, one transformed unit is approximately one
percentage point; further from 50%, it represents a smaller percentage change.

Elasticity describes the seat's response to its regional swing. The regional
predictor remains a raw percentage-point swing, matching the simulation's
existing mean calculation. The regional average uses seats with valid
comparisons. The zero-intercept regression coefficient is shrunk 62% towards 1
(only 38% of its departure from 1 is retained). Fewer than three observations,
or a non-positive fitted shrunken coefficient, use elasticity 1. Volatility
measures errors around this exported coefficient, not the unconstrained fit.
The disruption allowance changes uncertainty, not elasticity or the mean.

## Recency, generic uncertainty and limited history

Historical squared errors receive a four-election half-life: an observation
four elections older than the latest available election receives half its
weight. All available earlier history can contribute; there is no ten-election
cutoff. Elasticity retains its ordinary unweighted regression, while the
uncertainty calculation applies the recency weights.

The generic standard deviation `G` is the square root of the weighted mean
squared error, floored at 2 transformed units. Each election contributes equal
total weight before applying recency, so elections with more seats do not
dominate. Federal forecasts use federal history; state forecasts use their
jurisdiction's history when at least four elections are available and otherwise
pool state history. The fourth and second error moments supply the common
kurtosis, with a normal-distribution minimum of 3. All these inputs obey the
same historical cutoff.

For a seat with `n` usable observations, add `k = max(0, 4 - n)` ordinary generic
observations. This uses the actual observation count, not its recency-weighted
equivalent. Then add one full-weight squared error of magnitude `2G` to allow
for disruption beyond that seat's established history. If `r_i` is an observed
error and `w_i` its recency weight, the final variance is:

```
max(2², (sum(w_i * r_i²) + k * G² + (2G)²) / (sum(w_i) + k + 1))
```

For example, two historical observations receive two ordinary generic
observations plus the disruption allowance. Ten historical observations receive
only the disruption allowance. A seat with no history receives four generic
observations plus that allowance: its SD is `sqrt(8/5) * G`, with elasticity 1.
The simulation uses these final scales directly, without its former 75/25
blend with a separate mean-absolute-deviation estimate.

## Reviewed historical repairs

The AEC download sometimes supplies an invalid swing when the TCP pairing
changes. A swing implying a previous share of zero or 100% is unavailable unless
a reviewed comparison is supplied. The current TPP share remains usable.
`analysis/Data/historical-tpp-corrections.json` records sources and guards each
repair against a changed current percentage:

| Election and seat | Current ALP TPP | Comparable prior TPP | ALP swing |
| --- | ---: | ---: | ---: |
| 2019 Whitlam | 60.91% | 63.72% | −2.81 points |
| 2025 Bendigo | 51.40% | 61.21% | −9.81 points |

Both switched Coalition TCP finalists. Whitlam uses its unchanged-boundary
2016 TPP; Bendigo uses the AEC's redistributed prior share, not its old-boundary
2022 result. The historical importer reapplies these repairs when regenerating
the maintained input. Other invalid comparisons remain unavailable.

## Regeneration and simulation use

From `analysis/` on Windows, regenerate every configured election without Stan,
network access or simulation runs:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.pipeline.seat_volatility
```

Use `--election 2022fed` for one election, or
`--output-directory ../cli-build/seat-volatility-review` for a review copy.
The full election-analysis pipeline also generates these files. Historical
result caches must already be present; the standalone command never downloads
missing elections. The older all-years CSV remains a legacy analysis output;
the simulation reads only the election-specific files.

The runtime loader verifies the election and required finite parameters. A
missing election file is a configuration error. Within a valid file, a seat
matches by current name, then previous name, then its configured borrowed
result name; otherwise it uses the election's fallback.

Rebuild the application and restart it before comparing forecasts, so the
in-memory betting calibration and live baseline are regenerated with the new
distributions. Existing saved reports remain unchanged. Regenerating parameter
files while the application is already running does not invalidate its cached
baseline automatically. No `.pol2` format change is required.

The Python tests cover the historical cutoff, transform, recency and padding;
the portable C++ test checks the runtime parser, aliases, fallback and every
maintained generated CSV. Forecast calibration still requires replay checks;
passing these tests does not establish coverage of the final forecast intervals.
