# Live Snapshot Display Implementation Plan

This is a temporary implementation plan for adapting the C++ GUI's **Live
Booths** tab to display the diagnostic snapshot files written under
`live_runs/`.

## Scope

Implement the first two disk-backed live snapshot display modes:

- **Node Inspector**: retain the direction of the existing Booths display and
  allow the user to select an individual exported run.
- **Parliament**: display Parliament outcome probabilities over successive
  source snapshots.

The design should support later display modes, filtering, and navigation from
summary cells to corresponding Node Inspector content. Those extensions are
not part of this implementation.

## Snapshot Set Selection

Use the first simulation in project order for which both conditions hold:

```cpp
simulation.isLive() &&
!simulation.getSettings().liveOutputFolder.empty()
```

Load its snapshot set from:

```text
<project workspace>/live_runs/<liveOutputFolder>/
```

Do not silently fall through to a later simulation when the selected
simulation has an invalid, missing, or unreadable output folder. Show a concise
in-tab error identifying the simulation and configured folder instead.

## Snapshot Loading Model

Add a GUI-independent loader, likely `LiveSnapshotData.h/.cpp`, which:

1. Enumerates completed `snapshot_*.json` files and ignores temporary files.
2. Supports the current `format_version == 2` schema.
3. Loads the filename, source `snapshot_code`, `completed_at`, simulation and
   election metadata, party catalog, Parliament outcomes, and full JSON
   document.
4. Retains every valid exported run, including multiple runs with the same
   source snapshot code.
5. Sorts runs by source `snapshot_code`, then `completed_at`, then filename.
6. Returns valid records together with per-file warnings. One malformed or
   unsupported file must not hide otherwise valid snapshots.
7. Recognises explicitly tagged non-finite diagnostic values without failing
   the complete load. Parliament probabilities should normally be finite;
   invalid values should display a clear diagnostic marker rather than being
   silently converted to zero.

Retaining the full parsed document allows later modes to use additional
diagnostic fields without repeatedly parsing files or redesigning the common
snapshot index.

## Toolbar Behaviour

The leftmost combo box should contain:

- `Node Inspector`
- `Parliament`

Build the toolbar once. `refreshData()` must update its contents without
destroying and recreating the controls, because rebuilding currently loses all
selection state.

### Node Inspector

Show a second combo box containing every valid exported run in the sorted
order, including duplicate source snapshot codes. A duplicate should remain
distinguishable in this selector by including its completion time or another
compact run identifier.

Store the selection by filename, not combo-box index. It must persist when the
user switches to Parliament and back, and across refreshes while that file
still exists. Select the newest run initially, or when the selected file has
been removed.

For this first implementation, preserve the existing basic detailed-display
direction using the selected file's `live_analysis` data. The existing Filter
control remains visible but has no filtering behaviour yet.

### Parliament

Hide the second combo box and its label. Do not alter the remembered Node
Inspector selection.

## Parliament Data

Build party columns from the union of party IDs appearing in any loaded
record's:

- `simulation_report.majority_percent`
- `simulation_report.minority_percent`
- `simulation_report.most_seats_percent`

Use the party catalog for names, abbreviations, and colours only. This avoids
creating Parliament columns for internal synthetic party identities that are
present in the catalog but never participate in Parliament outcomes.

For each represented party, display:

- Majority
- Minority
- Most Seats

Add one final `Exact Tie` column. An omitted outcome entry for a party that is
represented elsewhere in the snapshot set means `0.00%`.

A practical initial column order is party-major:

```text
Snapshot | ALP Majority | ALP Minority | ALP Most Seats |
LNP Majority | LNP Minority | LNP Most Seats | ... | Exact Tie
```

## Duplicate Source Snapshots

The Parliament view should contain one row per distinct source
`snapshot_code`, rather than one row per exported run.

For each duplicate group:

1. Select the record with the latest valid `completed_at`.
2. Use filename as a deterministic final tie-breaker.
3. Render an asterisk immediately after the displayed source timestamp.
4. Keep all members of the group available in Node Inspector.

The asterisk means that a newer simulation run replaced an earlier run for
that source snapshot in the Parliament view. A tooltip or status area should
show the selected filename, completion time, and duplicate count.

Parliament rows remain ordered by source `snapshot_code`, not by simulation
completion time. This is important when historical source snapshots are
replayed later.

## DC-Based Table Rendering

Keep the display in a custom drawing context similar to the existing
`wxPanel`/buffered-DC implementation. Do not replace it with `wxGrid`.

Implement a reusable table-layout layer over the DC with:

- calculated column positions and widths;
- a fixed header row;
- row and column clipping;
- vertical and horizontal scroll offsets;
- hit-testable cell rectangles for future click navigation;
- party-aware header or cell colours;
- percentage formatting to two decimal places; and
- deterministic rendering independent of window repaint frequency.

Use native scrollbars, a scrolled window, or explicit scrollbar controls, but
keep scrolling state in the view rather than changing the underlying row
order. The layout should expose row/column/cell geometry separately from
drawing so it can be unit-tested and later reused by other summary modes.

The first column contains a compact source timestamp. Use a format that
includes the day; `DD-MM-YY HH:MM:SS` is recommended because literal
`MM-YY HH:MM:SS` cannot distinguish snapshots from different days in the same
month. Append `*` for a duplicated source snapshot.

## Rendering and Refresh

`LiveBoothFrame` should maintain:

- selected primary mode;
- selected Node Inspector filename;
- loaded snapshot records and warnings;
- Parliament deduplicated row indices;
- horizontal and vertical scroll state; and
- calculated table geometry/hit-test regions.

Refresh the snapshot set:

- after a simulation completes, using the existing refresher call;
- when the Live Booths tab becomes active; and
- after a relevant simulation is added, removed, or edited.

Preserve mode, selected filename, and valid scroll positions during refresh.
Clamp scroll positions only when the table becomes smaller.

Display missing-folder, empty-folder, and skipped-file information inside the
tab rather than through modal dialogs. Include the chosen simulation name and
output-set name in a compact status line.

No filesystem watcher is required at this stage.

## Suggested Implementation Sequence

1. Add the snapshot record types, JSON parsing, ordering, and warning handling.
2. Add a pure helper that groups records by `snapshot_code` and selects the
   newest completed run for Parliament.
3. Add a pure Parliament view model containing party columns, rows, formatted
   values, and duplicate markers.
4. Refactor `LiveBoothFrame` so the toolbar is persistent and mode changes are
   handled.
5. Connect Node Inspector selection to disk-backed snapshot records while
   preserving the current basic rendering.
6. Add the DC table layout, scrolling, clipping, painting, and future-facing
   hit-test metadata.
7. Refresh the view on tab activation and simulation changes.
8. Update Visual Studio project/filter files and portable test source lists.

## Tests

Add unit tests for the non-wxWidgets loader and view-model code:

- valid version-2 loading;
- malformed JSON and unsupported format versions;
- chronological source-snapshot ordering;
- duplicate retention in the full record set;
- latest-completed-run selection for duplicate Parliament rows;
- deterministic filename tie-breaking;
- duplicate asterisk generation;
- party-union construction and stable party order;
- omitted outcome values becoming zero;
- explicitly non-finite outcome values remaining visibly diagnostic;
- missing and empty folders;
- timestamp formatting; and
- DC table geometry, clipping, scrolling bounds, and hit testing where these
  can be tested without constructing a GUI window.

Selection persistence and mode-specific control visibility may require a small
manual GUI check if the existing test setup cannot construct wxWidgets frames.

## Acceptance Checks

Using the existing `live_runs/sa2026-test1` set:

1. The correct configured simulation and output folder are shown.
2. Node Inspector lists every exported file, including repeated runs of the
   same source snapshot.
3. Switching to Parliament and back preserves the selected Node Inspector
   file.
4. Parliament displays one row per source snapshot code.
5. A duplicate source snapshot uses its latest completed run and displays `*`
   after its timestamp.
6. Every displayed Parliament percentage matches the selected JSON record.
7. Both horizontal and vertical scrolling work without changing row order.
8. A malformed file produces an in-tab warning while valid rows remain usable.

