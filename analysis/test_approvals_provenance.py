import tempfile
import unittest
from pathlib import Path
from unittest import mock

import approvals_provenance
import generated_provenance


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
            "2029,wa\n", encoding="utf-8"
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


if __name__ == "__main__":
    unittest.main()
