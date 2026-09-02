# Analysis Pipeline Completion Plan

## Purpose

Finish the provenance and orchestration work by giving audit output a clear
task-first structure, expanding regular generation to cover routine C++ inputs,
adding repository-wide generation, and making archive preflight use the same
definition of required work. Inactive future elections and generated files
with no current consumer must not obscure work that can affect an active
forecast or historical rebuild.

The implementation is divided into three phases, but all three depend on one
shared election-status and graph-selection foundation.

## Agreed Election Status Model

Extend `analysis/Data/future-elections.csv` from two to three columns:

```csv
2026,vic,active
2027,nsw,active
2028,fed,active
2028,qld,inactive
2029,wa,inactive
```

The file remains headerless. Valid statuses are exactly `active` and
`inactive`; parsing should reject missing/unknown statuses and duplicate
elections with a contextual configuration error.

The status is operational, not methodological:

* Every row remains a configured future election. Existing generators may
  still target an inactive election explicitly.
* Existing generator-level `all` behavior remains unchanged unless a command
  is invoked through the new pipeline profile.
* `active` controls default audit visibility, routine repository planning and
  archive requirements.
* An explicit `--election` selection overrides inactive status so an inactive
  election can still be tested or prepared deliberately.
* Historical elections continue to come from `polled-elections.csv`.

All current readers tolerate a third column: they either use only the first two
fields directly or call `ElectionCode.load_elections_from_file()`, which does
the same. Nevertheless, status interpretation must be central rather than
reimplemented in individual audit, planner and archive modules.

### Open Future Terms Versus Active Elections

The existing `approvals_provenance.current_elections()` name conflates two
concepts. Its present behavior must remain based on every election in
`future-elections.csv`, not only those marked active. These elections are
better described as **open future terms**: their poll data and pure trends can
still change because their final result is not known.

This distinction is important for historical cutoffs. Synthetic-TPP fitting
for a historical cutoff can retain pure-trend evidence from an open future
term. That dependency is recorded, but intentionally made non-invalidating:
otherwise every new current poll could invalidate weeks of completed cutoff
work even though it has little effect on the early historical fit. An inactive
future election can still acquire source data or have an old pure trend, so
removing it from this exception would let an operational status flag make
historical cutoffs stale.

Introduce explicit shared terms such as:

* `configured_future_elections()` for every row;
* `open_future_elections()` for every row, used by the cutoff exception;
* `active_future_elections()` for rows marked active;
* `inactive_future_elections()` for rows marked inactive.

Keep `current_elections()` temporarily as a compatibility alias for
`open_future_elections()`, then update comments and internal call sites to the
clearer name without changing cutoff behavior.

## Phase 1: Shared Required-Graph Foundation

Implementation status: complete. The operational catalogue, open-term cutoff
exception, shared C++-root reachability metadata and graph-filtered archive
selection are implemented and covered by the Python CI suite.

### Central Catalogue Loading

Add one strict catalogue loader shared by provenance, pipeline and archive
code. It should return normalized election codes and statuses while preserving
the existing ordered `ElectionCode` lists needed by generators.

Do not initially rewrite every generator around activity status. Generators
should continue to see all configured future elections; only orchestration and
reporting use active/inactive status.

### Required Work Selection

Create one reusable selector that classifies audited work units by actual graph
reachability rather than static category labels. It should:

1. Start from durable C++-facing output roots for the requested elections.
2. Traverse recorded generated dependencies to include required upstream work.
3. Include global work such as election analysis when a selected terminal
   output depends on it.
4. Treat explicitly selected inactive elections as selected roots.
5. Classify work reachable only from inactive future elections separately.
6. Exclude work with no path to a historical or selected active root.
7. Preserve blockers for unregistered source changes even when no generated
   record is currently reachable, because their scope is not yet trusted.

Repository-wide selection should use all historical elections plus active
future elections. It should not use all existing manifest records as roots.
This prevents orphaned compatibility data from becoming invented work.

The known example is the 45 legacy pure trends from `1972fed` through
`1984fed`. Synthetic-TPP dependencies begin at `1987fed`; those earlier records
have no past or active consumer and should be absent from audits, generation
plans and new archives.

### Archive Integration

Archive preflight and payload selection must use the same required graph.
Currently preflight rejects every non-current manifest record and payload
selection copies almost every generated file. Both behaviors must change
together:

* Non-current inactive-only or unreferenced work must not block an archive.
* Generated files owned only by inactive or unreferenced records should not be
  copied into a newly built archive.
* Authored files remain unaffected.
* Diagnostic traces and staging/checkpoint files remain excluded.
* Activating an election later should expose its missing/stale work and require
  generation before its outputs enter the next archive.

### Provenance Policy

Register the `future-elections.csv` status-column update and supporting
catalogue/orchestration changes as `negligible`. They change scheduling and
presentation but do not alter the numerical output of any explicitly selected
generator. Do not increment generated-data semantic dependencies merely for
this operational classification.

## Phase 2: Task-First Audit Presentation

Implementation status: complete. Text audits now lead with executable stages,
their selected targets and concise dependency/source reasons. Repository-wide
historical, deferred and metadata sections are compact summaries; complete
per-target details and suppressed identifiers remain available in JSON.

Retain the structured root-cause data for machine use, but make human-readable
output lead with downstream work that can actually be performed.

### Actionable Sections

Display, in priority order:

1. Blockers and unregistered source changes.
2. Active-election work, grouped by executable stage and election.
3. Required upstream tasks, in dependency order.
4. Historical convergence backlog.
5. Deferred calibration, synthetic-TPP and cutoff-path work that is genuinely
   reachable from a selected root.
6. Metadata-only maintenance.
7. Diagnostic notices.

For each downstream task, list concise reasons underneath it. Distinguish
source/code revisions from generated prerequisites. For example:

```text
generate_trend_adjustments:
  2028fed: 7 legacy work units
    waiting on cutoff regeneration: 2022vic, 2023nsw
    source changes: trend-adjust algorithm, prior-result corrections
```

If an upstream generated unit itself needs regeneration, show it once as a
separate task. The dependent task should say that it is waiting on that task
rather than presenting the upstream category as though it were the broken
output. Thus `cutoff_poll_outputs: 410` becomes a trend-adjustment backlog with
specific stale cutoff prerequisites.

### Suppressed Work

Do not list inactive-election work units individually. When at least one
non-current unit was suppressed, print one diagnostic line:

```text
Work units required only for inactive elections 2028qld, 2029wa are not listed.
```

Do not mention unreferenced retained outputs at all. Include suppressed counts,
inactive election codes and unreferenced record IDs in structured JSON for
diagnosis and tests, but keep them out of normal terminal output.

For an unscoped audit, replace category-only claims such as “C++ direct inputs
requiring prompt regeneration” with active/historical task information from
the required graph. Scoped audits continue to include explicitly requested
inactive elections.

## Phase 3: Expanded Generation Profiles

**Status: complete.** Both regular profiles include routine C++ inputs, while
repository-wide `all` generation uses the shared required graph, strong
confirmation and a final metadata-maintenance pass.

### Regular Profiles Include Routine C++ Inputs

Do not add a separate `cpp-inputs` profile. Expand both `regular` and
`regular-with-approvals` so that a routine selected-election update also
refreshes all non-time-consuming generated inputs needed for proper C++ model
execution. Their roots should include:

* checked election-result exports when stale;
* global election analysis (`seat_statistics`, `nationals_allocations` and
  `federal_regional_statistics`);
* election-specific trend adjustments and fundamentals;
* election-specific regional swing deviations where actual regional polling
  requires them;
* the existing pure/final poll-trend work selected by the regular profile.

Both regular profiles continue to skip calibration and historical cutoff
generation. The ordinary `regular` profile also skips the broader approval
refresh path. `regular-with-approvals` retains its existing approval-refresh
behavior, but otherwise selects the same routine C++ inputs. This keeps the
choice focused on whether the slower approval path is wanted, rather than on
whether easily forgotten C++ prerequisites are refreshed.

The profiles must still follow required prerequisites. In particular, they
must not regenerate trend adjustments from known-stale cutoff records or
silently launch expensive cutoff/calibration work. Such prerequisites should
be shown as deferred blockers for the affected short task while unrelated
routine tasks continue. Once those expensive prerequisites have been refreshed
by their dedicated profiles, the next regular run should include the now-
actionable trend adjustment automatically.

### Repository-Wide All Profile

Make an executable all-generation setting that uses every historical election
plus active future elections. It should:

* select work through the shared required graph;
* skip inactive-only and unreferenced work;
* retain existing topological ordering;
* skip calibration/cutoff work already current;
* run remaining metadata maintenance after numerical generation;
* retain fixed-snapshot/follow-up behavior for changes arriving during a run;
* print stage counts and cost classes before execution;
* require an explicit confirmation phrase such as `RUN ALL GENERATION`.

The existing plan-only `all` behavior should either be replaced by this defined
repository-wide behavior or retained under a clearly different diagnostic
name. There should be only one user-facing meaning of “all.”

The all profile is the final convergence operation before archive generation,
not a routine update profile. Archive creation remains a separate explicit
action with its own preflight and confirmation.

## Validation

Add focused tests for realistic operational risks:

* third-column compatibility and strict status validation;
* open-future versus active-future sets;
* inactive status not changing the historical-cutoff exception;
* explicit selection overriding inactive status;
* inactive-only work omitted with one diagnostic line;
* unreferenced pure trends absent from text and executable plans;
* downstream task headings naming exact stale generated prerequisites;
* regular-profile selection and ordering of routine C++ inputs;
* identical routine C++ roots for regular and regular-with-approvals;
* expensive calibration/cutoff prerequisites being reported but not launched
  by regular profiles;
* all-profile historical/active inclusion and inactive exclusion;
* archive preflight and payload exclusion using the same required graph;
* activation of an inactive election making its outstanding work actionable.

Do not run Stan in CI. Planner, audit and archive behavior should use synthetic
manifest fixtures and mocked subprocess execution.

## Implementation Order

1. Add and test strict future-election status parsing.
2. Update `future-elections.csv` and register the change as negligible.
3. Introduce the shared required-graph selector and structured suppression
   metadata.
4. Apply the selector to archive preflight/payload selection.
5. Replace human audit output with the task-first view.
6. Expand and test both regular profiles with routine C++ input generation.
7. Define and enable repository-wide all generation with strong confirmation.
8. Update pipeline documentation and run the complete provenance/orchestration
   test suite.
