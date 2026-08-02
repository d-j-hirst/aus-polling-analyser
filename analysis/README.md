# Python Analysis

The generated-data dependency graph, regeneration order, calibration caveats
and planned provenance system are documented in [PIPELINE.md](PIPELINE.md).
The corresponding machine-readable registry is
[`pipeline_registry.json`](pipeline_registry.json).

Audit authored inputs, monitored scripts and generated provenance with:

```bash
python3 analysis_provenance.py audit
```

Run `python3 analysis_provenance.py` without arguments for an interactive menu.
InquirerPy is used when installed, with a plain terminal fallback otherwise.
Interactive change registration defaults to an election-specific scope. A
scope that can affect every election requires a separate confirmation that
defaults to declining the broad impact.
Limit an audit to one election, or repeat the option for a custom group:

```bash
python3 analysis_provenance.py audit --election 2028fed
python3 analysis_provenance.py audit \
  --election 2026vic --election 2027nsw --election 2028fed
```

Targeted audits include generated work units for the named elections plus
cross-election work units explicitly recorded as their dependencies.
Automation can request the same result without parsing terminal prose:

```bash
python3 analysis_provenance.py audit --election 2028fed --format json
```

The JSON distinguishes current, stale, legacy, missing, altered and blocked
work units. Unregistered source changes and invalid dependencies are blockers;
legacy outputs remain identifiable and usable while awaiting regeneration.

## Pipeline Status And Planning

Open the interactive status and planning interface:

```bash
python3 pipeline.py
```

The equivalent explicit commands are:

```bash
python3 pipeline.py status --election 2026vic
python3 pipeline.py plan --election 2026vic --profile regular
python3 pipeline.py plan --election 2026vic \
  --profile regular-with-approvals
python3 pipeline.py plan --election 2026vic --profile cutoffs
python3 pipeline.py run --election 2026vic --profile regular
python3 pipeline.py run --election 2026vic \
  --profile regular-with-approvals
python3 pipeline.py run --election 2026vic --profile calibration
python3 pipeline.py run --election 2026vic --profile cutoffs
python3 pipeline.py plan --profile metadata
python3 pipeline.py run --profile metadata
```

Each `run` writes an ignored log under `Logs/Pipeline/` while preserving live
stdout and stderr in the terminal. The log records the profile and targets,
task commands and boundaries, subprocess output, elapsed time, exit status,
post-task provenance problems, and final completion status.

Status is aggregated by default. Plans are grouped by generation stage; add
`--details` to show every command or `--format json` for the complete
machine-readable plan. The cutoff profile selects historical elections needed
to calibrate the target election's trend adjustments; it does not generate a
cutoff for the target itself. Unlike regular profiles, cutoff planning does
not tolerate inherited calibration or synthetic-TPP staleness: it schedules
the recorded calibration, pollster-parameter and pure-trend prerequisites
before the expensive cutoff fits. Pure trends belonging to current terms are
the exception when they only supply historical approval evidence: they remain
documented but do not repeatedly invalidate completed cutoffs as new polls
arrive. Status and planning are read-only and do not run generators.

The calibration profile runs only the leave-one-pollster-out and bias
calibration fits. After those complete, build a separate regular or
regular-with-approvals plan for the forecast elections being updated. That
second audit will detect changed calibration digests, refresh the applicable
pollster analysis, and then schedule only the required target trends.

The `run` command supports regular trends, regular trends with refreshed
approval inputs, calibration, historical cutoffs and metadata maintenance.
The `all` profile remains planning-only because it includes broader source
acquisition and global analysis stages. Generation runs prevalidate the
complete plan, execute one election command at a time, stream model output to
the terminal, and stop on the first failure. After every successful command
the runner rebuilds the plan and requires the completed task to have cleared.
Each rebuild uses a fresh Python process, so changes to provenance tooling
during a long Stan task do not leave the runner using obsolete imported code.
The initial plan includes downstream work that its own tasks are expected to
invalidate. Work made stale by changes recorded after the run starts is
held until the primary snapshot is complete, then included in one automatic
follow-up pass. If still more work becomes actionable, the runner asks before
each additional refreshed pass. Rerunning after an interruption naturally
skips work already recorded by the generators. The executor writes directly
to existing output paths. Separate pipeline runs may operate concurrently;
generated provenance manifests are locked only during their brief
read-modify-write updates. Avoid deliberately running the same work unit
twice because its generator may target the same data files.

If a post-task audit fails, the runner reports an action-required message and
waits for Enter before checking again. It continues this cycle until
provenance is valid and the completed task has cleared, or the user stops the
run with Ctrl-C. Completed generator output is not rerun merely because a
temporary source-registration issue occurred at the task boundary.
A task that wrote new generated records but was made stale by a newer change
is accepted against the current execution snapshot and moved to the next
follow-up pass.

Source changes use these impact levels:

* `negligible`: no generated data or generated metadata needs updating.
* `provenance-only`: data remains valid, but explicitly identified metadata
  maintenance is required.
* `minor`: data generation may change modestly and affected work is stale.
* `material` and `major`: increasingly substantial generation changes.

Every `provenance-only` registration requires a registered metadata upgrade
ID. The metadata profile applies pending upgrades in event order while
preserving the original generation run, random seed and output fingerprints.
If a work unit is also stale from a `minor` or larger change, metadata
maintenance skips it because regeneration will replace its metadata.

For example:

```bash
python3 analysis_provenance.py register-change fp_model_provenance.py \
  --summary "Updated dependency bookkeeping without changing trends." \
  --impact provenance-only \
  --provenance-upgrade refresh-source-dependency-v1 \
  --all-scopes
```

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

`fp_model.py` is the command-line entry point; implementation lives in
`fp_model_constants.py`, `fp_model_data.py`, `fp_model_prepare.py`,
`fp_model_stan.py`, `fp_model_outputs.py` and `fp_model_runner.py`. It
generates poll trends from poll data and supporting inputs. A run can take one
to four hours depending on the election and processor. Outputs include the
trend, adjusted polls, and house-effect estimates for each modelled party
grouping and TPP. Unnamed-Others soft-tail adjustment affects output
serialization only and does not change Stan sampling.

Generate one election:

```bash
python3 fp_model.py --election 2022-fed
```

To retain progress output while hiding Stan's repetitive per-chain gradient
timing estimates, use the argument-compatible wrapper. It also limits
`Iteration:` updates to approximately one every five seconds across the full
batch and condenses each three-line elapsed-time summary to one line:

```bash
python3 run_fp_model.py --election 2022-fed
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

Multi-election runs preserve the configured order where possible, but move
each selected federal trend ahead of state trends whose election cycles
overlap it. When an `-onwards` run starts from a state election, that state's
overlapping federal trends are assumed to be complete and are not run again;
later federal trends are still moved ahead of state trends that depend on
them.

Generate consolidated historical poll-endpoint fits with:

```bash
python3 fp_model.py --election 2025-fed --cutoff
```

Cutoff generation keeps the certified output untouched while writing a
fingerprinted `.in-progress` draft and JSON sidecar. A matching rerun skips
complete endpoints and reruns every party for the interrupted endpoint before
atomic promotion. These are poll-data-as-of hindcasts, not complete historical
information-vintage reconstructions: pollster parameters and approval evidence
come from the selected current generated inputs.

### Calibration

Generate leave-one-pollster-out calibration data for one election:

```bash
python3 fp_model.py --election 2025-fed --calibrate
```

Generate its pollster-bias calibration:

```bash
python3 fp_model.py --election 2025-fed --bias
```

These stages can be very slow. Runs use a versioned default base seed and derive
a separate stable seed from the election, party, mode, actual cutoff endpoint
and excluded pollster. `--seed N` overrides the base seed. Pure and final fits
have distinct seed namespaces. Every completed calibration and bias fit seed is
persisted under `Outputs/Calibration/Seeds/`.

Default calibration runs request only the posterior median consumed by the
reducer; `--calibration-traces` retains all 101 detailed percentiles. After each
complete excluded-pollster block, an atomic local checkpoint is written below
`Outputs/Calibration/Checkpoints/`. A matching rerun resumes complete blocks
and repeats only the interrupted block. Successfully published calibration
staging removes those restart files. The established MAE/full-fit estimator is
unchanged, while versioned held-out-poll evidence is retained under
`Outputs/Calibration/Evidence/` for later estimator comparisons.

HMC diagnostic failures remain non-fatal and are appended, with mode and seed,
to `Outputs/fp_model_diagnostics.log`; sampling exceptions and invalid model
outputs still fail the run. Completed calibration work units are recorded in
the ignored generated-provenance bundle. Existing pre-provenance calibration
files can be fingerprinted, without claiming they were reproduced, using:

```bash
python3 calibration_provenance.py baseline
```

The production daily-prior strength and scaling are unchanged. Inspect its
finite-chain endpoint behavior with the exact Gaussian solver, optionally
cross-checked against a configurable standalone Stan model:

```bash
python3 prior_chain_diagnostic.py --chain-lengths 15,31,91,181,365
python3 prior_chain_diagnostic.py --chain-lengths 31,181 --stan
```

`low_share_diagnostic.py` compares the current raw Gaussian inference plus
the production smooth soft-tail reported-output mapping (identity on
``[0.5, 99.5]`` with a narrow blend into the legacy exponential tails) with
bounded exponential-inference and smooth-logit alternatives. Inference
alternatives remain isolated from production; the exponential inference
comparison showed materially poorer effective sample size in the seeded
low-share pilot, and a full random-walk parameterization
decision would be required before adopting the smooth alternative.

Reduce the calibration evidence into the compact parameters used by normal
poll-trend runs:

```bash
python3 pollster_analysis.py --election 2028-fed
```

This records the election's `variability`, `he_weighting` and `biases` files
as one generated work unit. Existing parameter files can be fingerprinted as
legacy outputs without rerunning the analysis:

```bash
python3 pollster_analysis_provenance.py baseline
```

Generate voting-intention-only trends, excluding approval and TPP-only
observations:

```bash
python3 fp_model.py --election 2028-fed --pure
```

Each completed election-party fit records its trend, adjusted-poll and
house-effect files in `Outputs/pure-generated-provenance.json`. Existing pure
outputs can be fingerprinted as legacy work units without rerunning Stan:

```bash
python3 fp_model_provenance.py baseline
```

## Trend Adjustments

`trend_adjust.py` compares generated trends with historical results. It writes
time-dependent parameters to `Adjustments/` and fundamentals estimates to
`Fundamentals/`. Targeting a past election excludes its result from training,
which prevents look-ahead when hindcasting.

Each completed target is recorded in
`Adjustments/generated-provenance.json`. Adjustment records depend on the
historical point-in-time cutoff work units actually loaded. Fundamentals
records retain only their authored election and context dependencies. Existing
files can be fingerprinted as legacy outputs without rerunning the analysis:

```bash
python3 trend_adjust_provenance.py baseline
```

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

## Election Results And Seat Analysis

Historical lower-house results pass through three separate stages:

1. `election_data.py` downloads missing elections from Wikipedia and stores the
   parsed objects in `elections/<term>_results.pkl`. Existing caches are reused;
   move or delete a cache only when that election needs to be downloaded again.
   The downloader contains explicit corrections for unusual source tables and
   may need another correction when a newly completed election is added.
2. `election_store.py` loads the caches without network access, reports
   consistency diagnostics, applies common historical party categories and
   writes `elections/results_<term>.csv` for the C++ simulation.
3. `election_analysis.py` uses the same checked cache objects to calculate
   seat-level inputs. These cover minor parties and independents, federal
   regional effects, TPP seat variation, incumbency and Coalition allocation.

Run the complete branch from `analysis/` with:

```bash
python3 election_data.py
python3 election_store.py
python3 election_analysis.py
```

Wikipedia is used because its historical tables are relatively consistent and
source errors can be corrected publicly. The numerical checks remain advisory:
published percentages are rounded, so small differences from vote-count
calculations are expected and displayed for review.

## File Permissions

If having problems with file permissions in WSL2, try following commands, replacing any c/C with the drive letter you're using (if not C):
```bash
sudo umount /mnt/c
sudo mount -t drvfs C: /mnt/c -o metadata
```

## Generated-Data Archive

### Restore For A New Clone

When `Archived/generated-data-archive.json` is present, the archive is the
fastest supported way to install the generated/cache data needed by the C++
forecast pipeline. From `analysis/`, run:

```bash
python3 pipeline.py
```

Choose **Restore validated generated-data archive** and type
`RESTORE GENERATED DATA` exactly. The action validates every archived payload
file against its SHA-256 hash before replacing local generated/cache outputs.
It preserves authored inputs in mixed directories, including `Regional/*-polls`
and `Federal-State/booths-*.txt`. It is intentionally available only through
the interactive menu, because restoration replaces local generated data.

Do not copy archive files over the working tree manually. An archive created
before this workflow has no validation manifest and is a legacy archive; it can
be retained for reference but should be replaced by a newly generated archive
before being used for a reproducible setup.

### Build After Full Regeneration

Only build a replacement archive once the generated graph is fully current.
From the same interactive menu, choose **Build validated generated-data
archive** and type `BUILD GENERATED ARCHIVE` exactly. The preflight rejects
stale, legacy, missing, altered, blocked or provenance-incomplete work units,
as well as unfinished calibration staging files. It then copies generated/cache
data to a temporary sibling directory, validates its manifest and hashes, and
replaces `Archived/` only after validation succeeds.

The archive includes generated/cache roots such as `Outputs`, `Adjustments`,
`Fundamentals`, `Seat Statistics`, `Nationals` and `elections`. It includes only
the generated files from mixed `Regional` and `Federal-State` directories, not
their authored inputs. Calibration diagnostic traces, local calibration
checkpoints, cutoff `.in-progress` drafts and incomplete staging files are
deliberately excluded. Retained legacy `calib_*` and detailed calibration
trend/poll/house-effect files are also excluded. Compact summaries and
versioned residual evidence and resolved seed manifests under
`Outputs/Calibration/Summaries/`, `Outputs/Calibration/Evidence/` and
`Outputs/Calibration/Seeds/` are retained.
