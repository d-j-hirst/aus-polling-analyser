"""Check the repository's run lists and intentional omission/error fixtures."""
from pathlib import Path
import subprocess
import re
import sys
import tempfile
import unittest

import check_portable_test_runners as checker


class PortableRunnerAuditTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="pa-runner-audit-")
        self.root = Path(temporary.name).resolve()
        # Cleanup owns only this newly created directory below the OS temp root.
        self.assertEqual(self.root.parent, Path(tempfile.gettempdir()).resolve())
        self.addCleanup(temporary.cleanup)
        self.manifest = self.root / "portable-test-sources.txt"
        self.windows = self.root / "run-portable-tests.cmd"
        self.shell = self.root / "run-portable-tests.sh"
        self.manifest.write_text("# Build list\nSimpleTests\nArgumentsTests\n", encoding="utf-8")
        self.windows.write_text(
            'call :run SimpleTests\n"%TEST_DIRECTORY%\\ArgumentsTests.exe" "."\n', encoding="utf-8")
        self.shell.write_text(
            'run_test SimpleTests\nrun_test ArgumentsTests "$ROOT_DIRECTORY"\n', encoding="utf-8")

    def test_repository_has_no_omissions(self):
        self.assertEqual(checker.audit(Path(__file__).resolve().parent.parent), [])

    def test_direct_calls_arguments_and_repeated_runs_are_recognized(self):
        self.windows.write_text(self.windows.read_text() + 'call :run SimpleTests\n', encoding="utf-8")
        self.assertEqual(checker.audit(self.root), [])

    def test_windows_omission_is_reported(self):
        self.windows.write_text('"%TEST_DIRECTORY%\\ArgumentsTests.exe" "."\n', encoding="utf-8")
        self.assertEqual(checker.audit(self.root), ["run-portable-tests.cmd does not execute: SimpleTests"])

    def test_original_four_windows_omissions_are_detected(self):
        repository = Path(__file__).resolve().parent.parent
        for filename in ("portable-test-sources.txt", "run-portable-tests.cmd", "run-portable-tests.sh"):
            (self.root / filename).write_text((repository / filename).read_text(), encoding="utf-8")
        omitted = ["LivePartyConversionTests", "LivePreparedStorageTests", "LiveVariabilityTests", "SeatVolatilityTests"]
        text = self.windows.read_text()
        for suite in omitted:
            text = re.sub(r"^call :run " + suite + r"[^\n]*\n", "", text, flags=re.MULTILINE)
        self.windows.write_text(text, encoding="utf-8")
        self.assertEqual(checker.audit(self.root),
            ["run-portable-tests.cmd does not execute: " + ", ".join(omitted)])

    def test_shell_omission_is_reported(self):
        self.shell.write_text('run_test SimpleTests\n', encoding="utf-8")
        self.assertEqual(checker.audit(self.root), ["run-portable-tests.sh does not execute: ArgumentsTests"])

    def test_misspelled_invocation_and_duplicate_build_entry_are_reported(self):
        self.manifest.write_text('SimpleTests\nArgumentsTests\nSimpleTests\n', encoding="utf-8")
        self.shell.write_text('run_test SimpleTests\nrun_test ArgumentTests\n', encoding="utf-8")
        errors = checker.audit(self.root)
        self.assertIn("portable-test-sources.txt repeats suites: SimpleTests", errors)
        self.assertIn("run-portable-tests.sh does not execute: ArgumentsTests", errors)
        self.assertIn("run-portable-tests.sh invokes unregistered suites: ArgumentTests", errors)

    def test_cli_returns_failure_for_an_unexecuted_suite(self):
        self.windows.write_text('call :run SimpleTests\n', encoding="utf-8")
        result = subprocess.run([sys.executable, "-B", str(Path(checker.__file__).resolve()), str(self.root)],
                                text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("run-portable-tests.cmd does not execute: ArgumentsTests", result.stderr)
        self.assertNotIn("audit passed", result.stdout)

    def test_comments_do_not_count_as_executions(self):
        self.windows.write_text(
            'rem call :run SimpleTests\nrem "%TEST_DIRECTORY%\\ArgumentsTests.exe"\n'
            'echo "%TEST_DIRECTORY%\\ArgumentsTests.exe"\n', encoding="utf-8")
        self.shell.write_text('# run_test SimpleTests\n', encoding="utf-8")
        self.assertEqual(len(checker.audit(self.root)), 2)


if __name__ == "__main__":
    unittest.main()
