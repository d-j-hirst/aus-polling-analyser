## Resource-intensive operations

Do not run Stan models or other full Bayesian fitting jobs unless the user
explicitly asks you to do so.

Do not build or run the wxWidgets/C++ computation application unless the
user explicitly asks.

Prefer pipeline dry-run/test options when validating Python changes.

## Native Windows Python

Use `analysis/.venv-win/Scripts/python.exe` for non-Stan work on Windows.
The PATH Python commands may be unconfigured pyenv shims. Keep the existing
Linux `analysis/env` separate; do not use it from a native Windows shell.

From the repository root, run routine tests with:

```powershell
.\analysis\.venv-win\Scripts\python.exe -B analysis\tests\run_tests.py
```

For pipeline inspection, use `analysis/` as the working directory:

```powershell
.\.venv-win\Scripts\python.exe -B pipeline.py status --election 2026vic
.\.venv-win\Scripts\python.exe -B pipeline.py plan --election 2026vic --profile regular
```

Status and all planning profiles are non-Stan. Generation profiles can launch
Stan even when named `regular`; `run` defaults to calibration. The routine
test selection excludes real Stan fitting; do not use `--all` in this environment.
Setup and recreation instructions are in `analysis/README.md`.
