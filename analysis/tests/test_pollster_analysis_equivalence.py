"""Prove compact calibration evidence preserves pollster-analysis outputs."""

import os
import shutil
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

import calibration_summary
from election_code import ElectionCode
import pollster_analysis
from pollster_analysis_evidence import load_calibration_evidence


class _WeightedStatistics:
    """Small test double for the two weighted statistics used by bias output."""

    def __init__(self, values, weights):
        total_weight = sum(weights)
        self.mean = sum(
            value * weight for value, weight in zip(values, weights)
        ) / total_weight
        self.std = (
            sum(
                weight * (value - self.mean) ** 2
                for value, weight in zip(values, weights)
            ) / total_weight
        ) ** 0.5


class PollsterAnalysisEquivalenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.legacy_directory = self.root / "legacy"
        self.legacy_directory.mkdir()
        self._write_legacy_bundle()
        calibration_summary.compact(self.legacy_directory, ["2025wa"])

        self.summary_directory = self.root / "summary-only" / "Summaries"
        self.summary_directory.mkdir(parents=True)
        shutil.copy2(
            calibration_summary.summary_path(self.legacy_directory, "2025wa"),
            self.summary_directory / "2025wa.csv",
        )
        data_directory = self.root / "Data"
        data_directory.mkdir()
        (data_directory / "polled-elections.csv").write_text(
            "2025,wa\n", encoding="utf-8"
        )
        (data_directory / "eventual-results.csv").write_text(
            "2025,wa,@TPP,50\n", encoding="utf-8"
        )
        (data_directory / "significant-parties.csv").write_text(
            "2025,wa,@TPP\n", encoding="utf-8"
        )
        (data_directory / "linked-pollsters.csv").write_text(
            "", encoding="utf-8"
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def _write(self, name, contents):
        (self.legacy_directory / name).write_text(contents, encoding="utf-8")

    def _write_legacy_bundle(self):
        self._write("calib_2025wa_Example_@TPP.csv", "1.5,2.5,\n")
        self._write(
            "fp_trend_2025wa_@TPP_biascal.csv",
            "Start date day,Month,Year\n"
            "01,01,2025\n"
            "Day,Party,0%,50%,100%\n"
            "0,@TPP,40,50,60\n"
            "185,@TPP,41,51.25,61\n",
        )
        self._write(
            "fp_house_effects_2025wa_@TPP_biascal.csv",
            "House,Party,0%,50%,100%\n"
            "New house effects\n"
            "Example,@TPP,-2,0.25,2\n"
            "Second,@TPP,-2,-0.5,2\n"
            "Old house effects\n",
        )
        self._write(
            "fp_polls_2025wa_@TPP_biascal.csv",
            "Firm,Day,@TPP,@TPP adj,@TPP reported\n"
            "Example,1,48,48.1,50\n"
            "Example,2,49,49.1,50\n"
            "Second,1,48,48.1,50\n"
            "Second,185,50,50.1,50\n",
        )

    def _run_analysis(self, evidence, name):
        output_directory = self.root / name
        output_directory.mkdir()
        target = ElectionCode(2025, "wa")
        cycles = {(2025, "wa"): (0, 1)}
        modules = {
            "numpy": types.SimpleNamespace(array=lambda values: list(values)),
            "statsmodels": types.ModuleType("statsmodels"),
            "statsmodels.stats": types.ModuleType("statsmodels.stats"),
            "statsmodels.stats.weightstats": types.SimpleNamespace(
                DescrStatsW=_WeightedStatistics
            ),
        }
        current_directory = Path.cwd()
        try:
            os.chdir(self.root)
            with mock.patch.dict(sys.modules, modules):
                return [
                    Path(path) for path in pollster_analysis.analyse_evidence(
                        target, cycles, {}, evidence, output_directory
                    )
                ]
        finally:
            os.chdir(current_directory)

    def test_compact_and_legacy_evidence_produce_byte_equivalent_outputs(self):
        legacy_evidence = load_calibration_evidence(
            sorted(self.legacy_directory.glob("*.csv"))
        )
        compact_evidence = load_calibration_evidence(
            [self.summary_directory / "2025wa.csv"]
        )

        legacy_outputs = self._run_analysis(legacy_evidence, "legacy-output")
        compact_outputs = self._run_analysis(compact_evidence, "compact-output")

        self.assertEqual(
            [path.read_bytes() for path in legacy_outputs],
            [path.read_bytes() for path in compact_outputs],
        )

    def test_analysis_failure_does_not_replace_any_existing_output(self):
        evidence = load_calibration_evidence(
            sorted(self.legacy_directory.glob("*.csv"))
        )
        output_directory = self.root / "atomic-output"
        output_directory.mkdir()
        target = ElectionCode(2025, "wa")
        expected_outputs = [
            output_directory / "variability-2025wa.csv",
            output_directory / "he_weighting-2025wa.csv",
            output_directory / "biases-2025wa.csv",
        ]
        for path in expected_outputs:
            path.write_text("old output\n", encoding="utf-8")

        with mock.patch.object(
            pollster_analysis,
            "analyse_house_effects",
            side_effect=RuntimeError("deliberate reducer failure"),
        ):
            with self.assertRaisesRegex(RuntimeError, "deliberate reducer failure"):
                pollster_analysis.analyse_evidence(
                    target,
                    {(2025, "wa"): (0, 1)},
                    {},
                    evidence,
                    output_directory,
                )

        self.assertEqual(
            [path.read_text(encoding="utf-8") for path in expected_outputs],
            ["old output\n"] * 3,
        )
        self.assertFalse(list(output_directory.glob("pollster-analysis-*")))


if __name__ == "__main__":
    unittest.main()
