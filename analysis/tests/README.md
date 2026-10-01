# Analysis Tests

The Python tests live here rather than beside the production procedures.
The separate repository-root `tests/` directory contains the C++ core tests.

## Routine Tests

From the repository root:

```bash
python3 -B analysis/tests/run_tests.py
```

From `analysis/`:

```bash
python3 -B tests/run_tests.py
```

The runner locates the analysis directory from its own path, makes production
modules importable and runs with `analysis/` as the working directory. It also
works when invoked by absolute path from another directory. Tests locate
repository scripts/data with `tests.ANALYSIS_DIRECTORY`; temporary test data
remains in temporary directories.

By default it runs the same selected modules, in the same order, as the prior
Linux/Windows CI commands, plus regression tests for the runner. Both CI jobs
now use this shared selection. The usual test dependencies are
`beautifulsoup4`, `numpy`, `pandas`, `pypdf==5.9.0`, `requests` and `statsmodels`,
as installed by the workflow. Real Stan compilation/sampling is not part of
this default suite. Windows uses `python` in place of `python3`.

## Focused And Extended Tests

Select modules, a moved test-file path, or a specific dotted test method:

```bash
python3 -B analysis/tests/run_tests.py test_pipeline test_turnout_priors
python3 -B analysis/tests/run_tests.py analysis/tests/test_region_model.py
python3 -B analysis/tests/run_tests.py tests.test_stan_cache.StanCacheTests.test_public_api_accepts_only_model_code
```

Add `-v` to show individual test names. Unknown tests and failed imports return
a non-zero exit status rather than being silently omitted.

To discover all tests, including numerical tests omitted from CI, use the
complete analysis environment (normally WSL):

```bash
analysis/env/bin/python -B analysis/tests/run_tests.py --all
```

Some extra modules import PyStan even when they do not run a fit, so `--all`
requires more than the CI dependencies. The production-model smoke test still
requires a deliberate opt-in and performs a small real compile/sample:

```bash
FP_MODEL_STAN_SMOKE=1 analysis/env/bin/python -B analysis/tests/run_tests.py test_fp_model_stan_smoke
```

Standard `unittest` execution also works from `analysis/` using package names:

```bash
python3 -B -m unittest tests.test_pipeline tests.test_turnout_priors
python3 -B -m unittest discover -s tests -t .
```

The discovery command includes all tests and has the same dependency caveat
as `--all`. Prefer the shared runner for the routine CI selection. New test
files should use `test_*.py`; add routine modules to `CI_TEST_MODULES` in
`run_tests.py` when they are ready for CI.
