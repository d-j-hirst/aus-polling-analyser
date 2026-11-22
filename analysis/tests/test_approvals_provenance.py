import tempfile
import unittest
from pathlib import Path
from unittest import mock

from lib.provenance import approvals_provenance
from lib.provenance import generated_provenance
from lib.provenance import source_provenance
from lib.shared.election_code import ElectionCode


class ApprovalsProvenanceTests(unittest.TestCase):
    def _write_data(self, base):
        data = base / "Data"
        outputs = base / "Outputs"
        synthetic = base / "Synthetic TPPs"
        data.mkdir()
        outputs.mkdir()
        synthetic.mkdir()
        (data / "polled-elections.csv").write_text(
            "1984,fed\n1987,fed\n2026,vic\n", encoding="utf-8"
        )
        (data / "future-elections.csv").write_text(
            "2028,qld,active\n2029,wa,inactive\n", encoding="utf-8"
        )
        (data / "election-cycles.csv").write_text(
            "1984,fed,1983-03-06,1984-12-02\n"
            "1987,fed,1984-12-03,1987-07-11\n"
            "2026,vic,2022-11-27,2026-11-28\n",
            encoding="utf-8",
        )
        poll_header = "MidDate,Firm,GLApp,GLDis\n"
        (data / "poll-data-fed.csv").write_text(
            poll_header
            + "1984-01-01,Old Pollster,,\n"
            + "1986-01-01,Newspoll,45,40\n",
            encoding="utf-8",
        )
        (data / "poll-data-vic.csv").write_text(
            poll_header + "2025-01-01,Newspoll,40,45\n",
            encoding="utf-8",
        )
        for region in ("nsw", "qld", "wa", "sa"):
            (data / "poll-data-{}.csv".format(region)).write_text(
                poll_header, encoding="utf-8"
            )
        return data, outputs, synthetic

    def test_trend_fit_and_approval_training_use_registered_poll_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            data, _, _ = self._write_data(base)
            manifest = data / "provenance.json"
            source_provenance.initialize_manifest(manifest, "Poll inputs")
            source_provenance.add_category(manifest, "raw_poll_data", "Polls", ["poll-data-*.csv"])
            vic = data / "poll-data-vic.csv"
            fed = data / "poll-data-fed.csv"
            registered_federal = fed.read_bytes().replace(b"\r\n", b"\n")
            with fed.open("a") as output:
                output.write("1984-02-01,Unregistered,49,40\n")
            victoria = [ElectionCode(2026, "vic")]
            with mock.patch.object(approvals_provenance, "DATA_DIRECTORY", data), mock.patch.object(
                approvals_provenance, "ANALYSIS_DIRECTORY", base
            ):
                snapshot = approvals_provenance.TrendPollInputs(victoria, include_approvals=True)
                self.assertEqual(snapshot.open("fed").read(), registered_federal)
                self.assertNotIn("1984fed", approvals_provenance.approval_elections(snapshot))
                self.assertIn("1984fed", approvals_provenance.approval_elections())
                with self.assertRaisesRegex(generated_provenance.GeneratedProvenanceError, "fed.csv"):
                    approvals_provenance.TrendPollInputs([ElectionCode(1984, "fed")], False)

                # A correction within foreign training data cannot be peeled off like an append.
                fed.write_text("edited existing rows\n", encoding="utf-8")
                with self.assertRaisesRegex(generated_provenance.GeneratedProvenanceError, "fed.csv"):
                    approvals_provenance.TrendPollInputs(victoria, True)
                with mock.patch.object(source_provenance.subprocess, "run") as git:
                    git.return_value.returncode = 0
                    git.return_value.stdout = registered_federal
                    restored = approvals_provenance.TrendPollInputs(victoria, True)
                    self.assertEqual(restored.open("fed").read(), registered_federal)
                    git.return_value.stdout = b"different committed revision\n"
                    with self.assertRaisesRegex(generated_provenance.GeneratedProvenanceError, "fed.csv"):
                        approvals_provenance.TrendPollInputs(victoria, True)
                pure = approvals_provenance.TrendPollInputs(victoria, False)
                with vic.open("a") as output:
                    output.write("2026-01-01,New Victorian poll,52,40\n")
                self.assertEqual(pure.open("vic").read(), snapshot.open("vic").read())
                self.assertNotIn(b"New Victorian poll", pure.open("vic").read())
                with self.assertRaisesRegex(generated_provenance.GeneratedProvenanceError, "vic.csv"):
                    approvals_provenance.TrendPollInputs(victoria, False)

    def test_approval_elections_exclude_terms_without_approval_polls(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            data, outputs, synthetic = self._write_data(base)
            with mock.patch.object(
                approvals_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                approvals_provenance, "DATA_DIRECTORY", data
            ), mock.patch.object(
                approvals_provenance, "OUTPUT_DIRECTORY", outputs
            ), mock.patch.object(
                approvals_provenance,
                "SYNTHETIC_DIRECTORY",
                synthetic,
            ):
                elections = approvals_provenance.approval_elections()

            self.assertEqual(elections, {"1987fed", "2026vic"})

    def test_current_elections_come_only_from_future_catalogue(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            data, outputs, synthetic = self._write_data(base)
            with mock.patch.object(
                approvals_provenance, "DATA_DIRECTORY", data
            ):
                elections = approvals_provenance.current_elections()

            self.assertEqual(elections, {"2028qld", "2029wa"})

    def test_open_future_elections_include_inactive_terms(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            data, outputs, synthetic = self._write_data(base)
            with mock.patch.object(
                approvals_provenance, "DATA_DIRECTORY", data
            ):
                elections = approvals_provenance.open_future_elections()

            self.assertEqual(elections, {"2028qld", "2029wa"})

    def test_available_records_require_both_pure_tpp_files(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            data, outputs, synthetic = self._write_data(base)
            for filename in (
                "fp_trend_1987fed_@TPP_pure.csv",
                "fp_polls_1987fed_@TPP_pure.csv",
                "fp_trend_2026vic_@TPP_pure.csv",
            ):
                (outputs / filename).write_text(
                    "data\n", encoding="utf-8"
                )
            with mock.patch.object(
                approvals_provenance, "OUTPUT_DIRECTORY", outputs
            ):
                records = (
                    approvals_provenance.available_pure_tpp_records(
                        {"1987fed", "2026vic"}
                    )
                )

            self.assertEqual(
                records, ["pure_poll_outputs:1987fed:@TPP"]
            )

    def test_target_dependencies_include_only_earlier_approval_terms(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            data, outputs, synthetic = self._write_data(base)
            with mock.patch.object(
                approvals_provenance, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                approvals_provenance, "DATA_DIRECTORY", data
            ), mock.patch.object(
                approvals_provenance, "OUTPUT_DIRECTORY", outputs
            ), mock.patch.object(
                approvals_provenance,
                "SYNTHETIC_DIRECTORY",
                synthetic,
            ):
                dependencies = (
                    approvals_provenance.synthetic_dependency_elections(
                        {"2026vic"}
                    )
                )

            self.assertEqual(dependencies, {"1987fed", "2026vic"})

    def test_current_pure_records_can_be_non_invalidating(self):
        with mock.patch.object(
            approvals_provenance,
            "_source_dependencies",
            return_value={},
        ), mock.patch.object(
            approvals_provenance,
            "synthetic_dependency_elections",
            return_value={"1987fed", "2026vic"},
        ), mock.patch.object(
            approvals_provenance,
            "available_pure_tpp_records",
            return_value=[
                "pure_poll_outputs:1987fed:@TPP",
                "pure_poll_outputs:2026vic:@TPP",
            ],
        ), mock.patch.object(
            generated_provenance,
            "generated_manifest_dependency",
            return_value={"dependency": "pure"},
        ) as dependency_call:
            dependencies = approvals_provenance.generation_dependencies(
                ["2025fed"],
                non_invalidating_elections={"2026vic"},
            )

        self.assertEqual(
            dependencies, {"pure_poll_outputs": {"dependency": "pure"}}
        )
        self.assertEqual(
            dependency_call.call_args.kwargs[
                "non_invalidating_records"
            ],
            ["pure_poll_outputs:2026vic:@TPP"],
        )


if __name__ == "__main__":
    unittest.main()
