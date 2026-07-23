# Polling Analyser

This README is under construction and may be incomplete.

This project contains the analysis code used to produce polling trends and
election simulations. The overall approach is described on the
[AE Forecasts methodology page](https://www.aeforecasts.com/methodology).

For a given election forecast, the implementation has four stages:

* Calculate historical trends for overall, regional and seat-specific outcomes
  while excluding data from the election being forecast. Python scripts in
  `analysis/` perform this work.
* Calculate the unadjusted poll trend for the forecast election with
  `analysis/fp_model.py`.
* Combine the trend with historical bias and error estimates to create
  distributions of overall vote shares. The C++ application performs this
  stage.
* Run multithreaded Monte Carlo simulations of the election in the C++
  application.

The application uses paths relative to the repository root, so run it with the
repository root as its working directory. See [analysis/README.md](analysis/README.md)
for the Python environment and data-generation workflow.

## Command-Line Forecasts

`polling-cli` runs the portable core forecast pipeline without wxWidgets,
TinyXML, curl, CMake, or other third-party build dependencies. It requires a
C++20 compiler and the standard library. The forecast inputs under `analysis/`
must already have been generated or restored from an archive; not all generated
analysis data is stored in this repository.

Run all build and forecast commands from the repository root.

### Linux

Build with GCC:

```bash
CXX=g++ sh build-polling-cli.sh
```

GCC 10 or later is recommended. The script also accepts the older `-std=c++2a`
spelling used by GCC 9 for C++20 mode.
Use `CXX=clang++ sh build-polling-cli.sh` to build with Clang instead.

Run a forecast:

```bash
./cli-build/polling-cli --no-log --term 2028fed \
  --macro 'run-model;run-projection;run-simulation:nowcast'
```

### macOS

Build with Apple Clang, available through the Xcode Command Line Tools:

```bash
CXX=clang++ sh build-polling-cli.sh
```

Run a forecast:

```bash
./cli-build/polling-cli --no-log --term 2028fed \
  --macro 'run-model;run-projection;run-simulation:nowcast'
```

### Windows

Install Visual Studio 2022 or its Build Tools with the **Desktop development
with C++** workload. From Command Prompt:

```bat
build-polling-cli.cmd
cli-build\polling-cli.exe --no-log --term 2028fed --macro "run-model;run-projection;run-simulation:nowcast"
```

From PowerShell, prefix each relative executable path with `.\`:

```powershell
.\build-polling-cli.cmd
.\cli-build\polling-cli.exe --no-log --term 2028fed --macro "run-model;run-projection;run-simulation:nowcast"
```

The Windows script automatically locates the latest installed MSVC x64 tools.
Both build scripts read the shared source list in `cli-sources.txt` and place
outputs under `cli-build/`.

### CLI Options

Use `--forecast path/to/forecast.json` instead of `--term` to load a manifest
from a non-default location. `--no-log` suppresses `PALog.log` pipeline output
while retaining terminal progress, warnings, and result summaries. Run
`polling-cli --help` for the complete option list.

### Macro Commands

Macros contain semicolon-delimited commands. Model and projection targets may
be omitted when exactly one is configured; simulation targets are always
required. Targets use case-insensitive exact or unambiguous partial matching
against the stable IDs in `forecast.json`.

| Command | Purpose |
| --- | --- |
| `prepare-model[:model]` | Load and validate model inputs without performing the full model run. |
| `load-model[:model]` | Load generated model output from `model-<term>.bin`. |
| `dump-model[:model]` | Cache the currently generated model output. |
| `run-model[:model]` | Run the configured model. |
| `run-projection[:projection]` | Generate the configured projection. |
| `run-simulation:<simulation>` | Run a non-live simulation. |
| `save-report:<simulation>:<label>` | Add the latest simulation output to its in-memory saved reports. |
| `export-report:<simulation>:<label>` | Serialize an exactly named saved report to `uploads/latest_json.dat`. |

For example, generate and cache a model before running and naming a report:

```bash
./cli-build/polling-cli --no-log --term 2028fed \
  --macro 'run-model;dump-model;run-projection;run-simulation:nowcast;save-report:nowcast:Baseline nowcast;export-report:nowcast:Baseline nowcast'
```

A later process can replace `run-model;dump-model` with `load-model`. Cache
contents are checked against the election and model configuration before use.
They also contain binary-layout metadata and must be regenerated when switching
between incompatible builds, such as the x64 CLI and Win32 GUI.
Reports remain in memory unless explicitly exported. `export-report` requires
an exact, unique saved-report label and writes the same local JSON interchange
file as the GUI. It does not contact any server. Report labels may contain
colons, but not semicolons.

Website submission remains a separate, authenticated operation performed by
`uploads/upload_manager.py`. Credentials are read from the untracked
`uploads/.env`; only placeholders are committed. The reference website also
requires the authenticated account to hold its forecast-submission permission,
so possessing an exported JSON file is not sufficient to upload there.

The entire macro is parsed and its static references are validated before any
command executes. Fatal errors stop execution, warnings are printed while
execution continues, and action-required events wait for Enter. A successful
simulation prints outcome probabilities, party seat means and medians, and
FP/TPP means and medians.

### Numerical Reproducibility

Repeated runs of the same executable with unchanged inputs are deterministic.
Different compilers, standard libraries, operating systems, or target
architectures can produce slightly different results because their
floating-point maths and standard-library distribution implementations are not
bit-for-bit identical. Use the same build when comparing exact benchmarks;
cross-build results should instead be compared for statistical equivalence.
