"""Analysis tests, kept separate from production procedures and their data.

Tests use ANALYSIS_DIRECTORY for repository fixtures and script-loading stubs.
Importing this package does not change the process directory or import path;
run_tests.py supplies that setup for command-line execution from any directory.
"""

from pathlib import Path


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent.parent
