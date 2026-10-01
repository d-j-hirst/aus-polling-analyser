import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import backfill_live_snapshot_summary as backfill


def sidecar_analysis():
    return {
        "election": {
            "projected_2pp": 58.21556854248047,
            "node": {"tpp_deviation": -1.8026573657989502},
        },
        "category_biases": {
            "tpp": {
                "booth_type": [
                    {
                        "category": "PPVC",
                        "bias": 0.0123,
                        "std_dev": 0.04,
                        "raw": 0.02,
                        "source_count": 8.0,
                        "vote_count": 12000.0,
                    }
                ],
                "vote_type": [
                    {
                        "category": "Postal",
                        "bias": -0.01,
                        "std_dev": 0.03,
                        "raw": -0.02,
                        "source_count": 4.0,
                        "vote_count": 3000.0,
                    }
                ],
                "non_classic": {"bias_percentage_points": 1.0, "confidence": 0.2},
            }
        },
        "seats": [
            {
                "name": "Frome",
                "node": {
                    "fp_completion": 0.2,
                    "tpp_completion": 0.00005,
                    "tcp_completion": 0.5,
                },
            },
            {
                "name": "Adelaide",
                "node": {
                    "fp_completion": 0.15,
                    "tpp_completion": 0.0002,
                    "tcp_completion": 1.0,
                },
            },
        ],
    }


class BackfillLiveSnapshotSummaryTests(unittest.TestCase):
    def test_completions_follow_report_seat_order(self):
        values = backfill.completions_for_seats(
            sidecar_analysis(), ["Adelaide", "Frome", "Florey"]
        )
        self.assertEqual(values["seat_fp_completion"], [0.15, 0.2, 0.0])
        self.assertEqual(values["seat_tpp_completion"], [0.0002, 0.00005, 0.0])
        self.assertEqual(values["seat_tcp_completion"], [1.0, 0.5, 0.0])

    def test_live_summary_copies_tpp_evidence_and_2pp(self):
        summary = backfill.live_summary_from_analysis(sidecar_analysis())
        self.assertEqual(summary["booth_type"][0]["category"], "PPVC")
        self.assertEqual(summary["vote_type"][0]["category"], "Postal")
        self.assertEqual(summary["projected_2pp"], 58.21556854248047)
        self.assertEqual(summary["raw_2pp_deviation"], -1.8026573657989502)
        self.assertNotIn("non_classic", summary)

    def test_null_tpp_deviation_becomes_zero(self):
        summary = backfill.live_summary_from_analysis(
            {"election": {"node": {"tpp_deviation": None}}}
        )
        self.assertEqual(summary["raw_2pp_deviation"], 0.0)
        self.assertEqual(summary["booth_type"], [])
        self.assertEqual(summary["vote_type"], [])

    def test_apply_keeps_existing_summary_counts(self):
        document = {
            "simulation_report": {"seat_name": ["Adelaide"]},
            "live_analysis_summary": {
                "booth_count": 12,
                "seat_count": 47,
                "first_booth_name": "Adelaide",
            },
        }
        backfill.apply_sidecar_to_document(document, sidecar_analysis())
        summary = document["live_analysis_summary"]
        self.assertEqual(summary["booth_count"], 12)
        self.assertEqual(summary["first_booth_name"], "Adelaide")
        self.assertEqual(document["simulation_report"]["seat_fp_completion"], [0.15])
        self.assertEqual(summary["booth_type"][0]["category"], "PPVC")

    def test_dump_completion_keeps_four_decimals(self):
        dumped = backfill.dump_compact_json({"seat_fp_completion": [0.0002]})
        self.assertEqual(dumped, '{"seat_fp_completion":[0.0002]}')

    def test_backfill_writes_main_file(self):
        with TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            main = directory / "snapshot_20260402143211__run_a.json"
            sidecar = directory / "snapshot_20260402143211__run_a.analysis.json"
            main.write_text(
                json.dumps(
                    {
                        "simulation_report": {"seat_name": ["Adelaide", "Frome"]},
                        "live_analysis_summary": {"booth_count": 3},
                    }
                ),
                encoding="utf-8",
            )
            sidecar.write_text(json.dumps(sidecar_analysis()), encoding="utf-8")
            status, _ = backfill.backfill_snapshot(main)
            self.assertEqual(status, "updated")
            loaded = json.loads(main.read_text(encoding="utf-8"))
            self.assertEqual(
                loaded["simulation_report"]["seat_fp_completion"], [0.15, 0.2]
            )
            self.assertEqual(
                loaded["live_analysis_summary"]["vote_type"][0]["category"],
                "Postal",
            )
            self.assertEqual(loaded["live_analysis_summary"]["booth_count"], 3)

    def test_embedded_live_analysis_fallback(self):
        with TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            main = directory / "snapshot_old.json"
            main.write_text(
                json.dumps(
                    {
                        "simulation_report": {"seat_name": ["Adelaide"]},
                        "live_analysis": sidecar_analysis(),
                    }
                ),
                encoding="utf-8",
            )
            status, sidecar = backfill.backfill_snapshot(main)
            self.assertEqual(status, "updated")
            self.assertIsNone(sidecar)
            loaded = json.loads(main.read_text(encoding="utf-8"))
            self.assertEqual(
                loaded["simulation_report"]["seat_tcp_completion"], [1.0]
            )

    def test_dry_run_does_not_write(self):
        with TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            main = directory / "snapshot_a.json"
            sidecar = directory / "snapshot_a.analysis.json"
            original = json.dumps({"simulation_report": {"seat_name": ["Adelaide"]}})
            main.write_text(original, encoding="utf-8")
            sidecar.write_text(json.dumps(sidecar_analysis()), encoding="utf-8")
            status, _ = backfill.backfill_snapshot(main, dry_run=True)
            self.assertEqual(status, "dry-run")
            self.assertEqual(main.read_text(encoding="utf-8"), original)

    def test_missing_analysis_is_reported(self):
        with TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            main = directory / "snapshot_a.json"
            main.write_text("{}", encoding="utf-8")
            status, sidecar = backfill.backfill_snapshot(main)
            self.assertEqual(status, "missing-analysis")
            self.assertEqual(sidecar, directory / "snapshot_a.analysis.json")


if __name__ == "__main__":
    unittest.main()
