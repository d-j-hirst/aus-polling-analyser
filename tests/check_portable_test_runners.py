#!/usr/bin/env python3
"""Reject portable suites that are compiled but absent from a platform's run list.

Compilation uses portable-test-sources.txt; each runner also has explicit calls
to handle suites with arguments and additional forecast-package checks. Audit
those literal calls without requiring a C++ build or running any forecasts.
This checks registration, not the runtime behaviour of shell branches.
"""
from __future__ import annotations

from collections import Counter
from pathlib import Path
import re
import sys


def registered_suites(root: Path) -> list[str]:
    return [line.strip() for line in
            (root / "portable-test-sources.txt").read_text(encoding="utf-8-sig").splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def audit(root: Path) -> list[str]:
    suites = registered_suites(root)
    expected = set(suites)
    errors = []
    duplicates = sorted(name for name, count in Counter(suites).items() if count > 1)
    if duplicates:
        errors.append("portable-test-sources.txt repeats suites: " + ", ".join(duplicates))
    if not expected:
        errors.append("portable-test-sources.txt contains no suites")

    windows = (root / "run-portable-tests.cmd").read_text(encoding="utf-8-sig")
    shell = (root / "run-portable-tests.sh").read_text(encoding="utf-8-sig")
    # Most Windows suites use :run; argument-heavy integration suites invoke
    # literal executable names directly. ForecastSpecificationTests deliberately
    # runs twice, so repeated invocations are valid even though build entries
    # must be unique. Ignore generic %~1.exe inside the :run implementation.
    windows_calls = set(re.findall(r"^\s*call\s+:run\s+(\w+)", windows, re.MULTILINE | re.IGNORECASE))
    windows_calls.update(re.findall(
        r'^\s*(?:call\s+)?"?%TEST_DIRECTORY%[\\/](\w+)\.exe',
        windows, re.MULTILINE | re.IGNORECASE))
    shell_calls = set(re.findall(r"^\s*run_test\s+(\w+)", shell, re.MULTILINE))
    for filename, calls in (("run-portable-tests.cmd", windows_calls),
                            ("run-portable-tests.sh", shell_calls)):
        missing = sorted(expected - calls)
        unexpected = sorted(calls - expected)
        if missing:
            errors.append(filename + " does not execute: " + ", ".join(missing))
        if unexpected:
            errors.append(filename + " invokes unregistered suites: " + ", ".join(unexpected))
    return errors


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
    try:
        errors = audit(root)
    except OSError as error:
        print(f"Portable test runner audit failed: {error}", file=sys.stderr)
        return 1
    for error in errors:
        print("Portable test runner audit failed: " + error, file=sys.stderr)
    if errors:
        return 1
    print(f"Portable test runner audit passed: {len(registered_suites(root))} suites registered on both platforms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
