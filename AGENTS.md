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

## Documentation

Public documentation explains implemented behaviour, reproduction commands,
source evidence and consolidated findings for observers and contributors.
Start each public document with its purpose, then explain its function in
plain language before giving commands or technical details.

For public and internal reports, explain the question and purpose of each
analysis before presenting its results. Define terms, comparison units,
model names, columns and denominators; use a small example when helpful.
Group like measurements and comparisons together, and prefer plain descriptions
such as "test election" to opaque terminology such as "fold".

Keep internal plans, priorities, delivery sequences and proposed work under
the gitignored `docs/planning/` directory. Public documents and generated
public reports must not link to or reproduce that planning material.

## Code style and structure

Maintain a clear distinction between different levels of abstraction. For any
given process, a top-level function should indicate an outline of the overall
flow of the that process without direct manipulation, which should be performed
in a separate function where possible.

Comments should be used extensively to establish context for a piece of code
and to explain decisions made. The comments should be stated plainly enough that
a reader familiar with Australian elections and intermediate-level mathematics
can understand the general purpose of a section of code - even when they may not
be able to follow the algorithmic details or language-specific syntax and
libraries.

When making a fix or adjusting a piece of code to account for an atypical
situation or specific case, ensure that the more common flow is also mentioned
among comments so that the atypical situation is not given undue weight. Without
this, a reader not familiar with the code's broader context and intentions may
incorrectly conclude that the entire piece of code is about the specific case.