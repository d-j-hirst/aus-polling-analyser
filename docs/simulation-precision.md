# Comparing ordinary and live simulation draws

This check finds changes caused by live integration that would be hidden by
rounded result tables or by averaging many simulated elections. It compares a
Standard Simulation with a separate Live Simulation using the same main-run
iteration numbers. The live baseline deliberately uses different samples and
is **not** the comparison run.

## Working precision and unchanged inputs

Vote shares, estimated vote counts, preference allocations and live deviations
are calculated in double precision. Published integer counts stay integers.
Existing stored model samples and saved report fields retain their established
types; this change does not alter the `.pol2` format. Live working values are not
rounded back to those storage types halfway through a calculation.

Several operations preserve their input exactly when they have no adjustment to
make. An unchanged transformed TPP retains its original margin, an already valid
percentage allocation retains its shares, and an unchanged Liberal/National split
retains its combined total and ratio. Received-count constraints still apply.
A declaration composition with no observed or simulated adjustment retains real
zeros instead of passing them through the small floor needed for log odds.
These are properties of individual operations at any counting stage, not a rule
that bypasses live calculation when the election has no reported votes.

Double precision reduces arithmetic error but does not prove two paths are
equivalent. The comparison below therefore reports every exact difference,
including very small differences, without clipping them to zero. Deliberate
electoral thresholds and statistical assumptions are separate from this check.

## Running a comparison

Add this optional setting to the workspace's `config.cfg`, then restart the
application:

```ini
sSimulationAuditDirectory=downloads/simulation-audits
```

Use the same build and platform for both runs. Run the Standard Simulation and
Live Simulation with the same number of
iterations, projection, election date and other forecast inputs. Select a
no-count feed for an invariance check. Keep candidate configuration and betting
odds settings consistent too. Supporting calibration and baseline runs are not
included in the trace and their sampling indices are unchanged.

Relative directories are resolved against the project workspace. Each main run
creates a new subdirectory containing `iterations.jsonl`.
`PALog.log` prints its path. The first record identifies the simulation, election,
seat order and party codes. Subsequent records retain successful draws at four
stages, with enough decimal digits to recover the exact working doubles:

| Stage | Quantities recorded |
| --- | --- |
| `before_live` | Seat FP percentages, Labor TPP margins above 50%, and the National fraction of Coalition FP before live integration |
| `after_live` | The same quantities after incorporating live evidence |
| `after_preferences` | Seat TCP identities and shares, winners, and the preceding quantities after preference distribution |
| `reported` | Shares after the Emerging Independent reporting classification, plus FP and TPP aggregation weights |

TCP shares are percentages for the first candidate in the recorded pair.
Aggregation weights are fractions of the election-wide total, not vote shares.
Candidate keys are model party indices, including its special anonymous groups.
Iteration and retry numbers identify which random draws were used. A final
completion record distinguishes a full comparison from an interrupted run.

Compare the two files from the repository root:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\compare_simulation_audits.py `
  "downloads/simulation-audits/<standard-run>/iterations.jsonl" `
  "downloads/simulation-audits/<live-run>/iterations.jsonl"
```

The tool pairs iteration IDs rather than file order. It reports changed
seat/draw counts by stage, the first changed stage for each affected draw,
maximum absolute differences by quantity, and examples with unrounded values.
Zero exit status means an exact match; status 1 means differences were found.
Incomplete traces or mismatched elections, iteration counts, seats or parties
are rejected. A difference already present at `before_live` points to preparation,
inputs or upstream live influence, rather than local integration alone. A retry
mismatch means those successful draws no longer use the same random sample.

This diagnostic is off by default and has no `.pol2` setting. It writes detailed
seat records for every iteration and can produce large files and additional run
time. Remove the setting after the comparison. The Python tool indexes file
offsets and reads one iteration at a time rather than retaining all seat records.
The usual compressed live archives remain separate and are not precision audits.

Individual draws do not depend on worker recording order. Aggregate floating-point
sums can nevertheless vary slightly with that order; use one simulation thread
when separately comparing aggregate reports bit for bit. The trace is designed
to distinguish that reporting effect from a difference in the simulated elections.
