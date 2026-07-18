# Python Analysis

## Environment

Run these commands from the `analysis/` directory. `fp_model.py` is unlikely to
work natively on Windows because it depends on pystan; WSL is recommended.

Create the virtual environment once:

```bash
python3 -m venv env
env/bin/python -m pip install -r requirements.txt
```

Activate it in each new shell:

```bash
source env/bin/activate
```

## Poll Trends

`fp_model.py` generates poll trends from poll data and supporting inputs. A run
can take one to four hours depending on the election and processor. Outputs
include the trend, adjusted polls, and house-effect estimates for each modelled
party grouping and TPP.

Generate one election:

```bash
python3 fp_model.py --election 2022-fed
```

Generate every configured election:

```bash
python3 fp_model.py --election all
```

Generating every configured election is likely to take multiple days.

Generate each configured election from a particular election onward,
including the named election. This is useful for resuming an interrupted
multi-election run:

```bash
python3 fp_model.py --election 2016-fed-onwards
```

## Trend Adjustments

`trend_adjust.py` compares generated trends with historical results. It writes
time-dependent parameters to `Adjustments/` and fundamentals estimates to
`Fundamentals/`. Targeting a past election excludes its result from training,
which prevents look-ahead when hindcasting.

Generate adjustments for one hindcast:

```bash
python3 trend_adjust.py --election 2022-fed
```

Use `--election none` for current forecasts, or `--election all` to regenerate
all configured hindcast and forecast files. Add `--diagnostics` to show bounded
day-zero poll-bias diagnostics for every party group, or specify a group such
as `--diagnostics Misc-p`.

Current adjustment files contain eight-row parameter blocks at transformed
support anchors from `-100` to `0`. The C++ model selects the surrounding
anchors from the median poll trend and linearly interpolates their parameters.
Legacy eight-row files remain supported as unconditioned adjustments.

## Seat Analysis

`election_analysis.py` calculates individual-seat inputs, including:

* Download data (from Wikipedia, as it has consistent formatting and is easy to fix at the source) and perform a number of checks for data consistency to avoid errors. (These should be largely unnecessary as the errors found have been fixed and relevant pages are being actively watched for changes, but are still undertaken as a precaution.) Once downloaded the results for each election are saved locally as a .pkl file; move/delete these to force a new download.
* Analyse trends in seat results for minor parties and independents (both emerging and incumbent).
* Analyse trends in regional breakdowns (currently limited to major state breakdowns in federal polls).
* Analyse trends in TPP seat results including e.g. incumbency and state effects.

Run it with:

```bash
python3 election_analysis.py
```

## File Permissions

If having problems with file permissions in WSL2, try following commands, replacing any c/C with the drive letter you're using (if not C):
```bash
sudo umount /mnt/c
sudo mount -t drvfs C: /mnt/c -o metadata
```

## Archived Inputs

For a quick setup, copy the contents of `Archived/` into `analysis/`. Regenerating
the full analysis remains preferable because archived outputs may not include
recent data or methodology changes.
