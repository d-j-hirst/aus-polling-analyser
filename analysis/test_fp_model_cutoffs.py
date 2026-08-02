import csv
import tempfile
import unittest
from datetime import date
from pathlib import Path
from unittest import mock

import fp_model_provenance
import trend_adjust_cutoffs


class CutoffScheduleTests(unittest.TestCase):
    def test_schedule_matches_trend_adjust_triangular_days(self):
        expected = [
            n * (n + 1) // 2
            for n in range(46)
        ]
        self.assertEqual(
            fp_model_provenance.cutoff_schedule(),
            expected,
        )
        self.assertEqual(expected[:5], [0, 1, 3, 6, 10])
        self.assertEqual(expected[-1], 1035)

    def test_effective_schedule_keeps_both_scheduled_and_poll_end_days(self):
        self.assertEqual(
            fp_model_provenance.effective_cutoff_schedule(
                election_day=date(2026, 3, 21),
                poll_dates=[
                    date(2026, 3, 10),
                    date(2026, 3, 15),
                    date(2026, 3, 18),
                ],
                schedule=[0, 1, 3, 6, 10],
            ),
            [(10, 11), (6, 6), (3, 3)],
        )


class CutoffOutputStoreTests(unittest.TestCase):
    @staticmethod
    def metadata(fingerprint="inputs-a", federal_prior_files=None):
        return {
            "schema_version": 1,
            "election": "2025fed",
            "expected_endpoints": [
                {
                    "scheduled_cutoff_days": 28,
                    "poll_trend_end_days": 30,
                },
                {
                    "scheduled_cutoff_days": 21,
                    "poll_trend_end_days": 22,
                },
            ],
            "parties": ["@TPP", "ALP FP"],
            "probability_columns": ["0%", "50%", "100%"],
            "base_seed": 123,
            "seed_namespace": "fp-model-v1",
            "dependencies": {"source": "current"},
            "source_fingerprint": fingerprint,
            "federal_prior_files": list(federal_prior_files or []),
        }

    def test_matching_draft_resumes_across_store_instances(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                first = fp_model_provenance.CutoffOutputStore()
                self.assertFalse(first.begin("2025fed", self.metadata()))
                for party in ("@TPP", "ALP FP"):
                    first.write(
                        election="2025fed",
                        party=party,
                        scheduled_cutoff_days=28,
                        poll_trend_end_days=30,
                        random_seed=123,
                        probabilities=(0.001, 0.5, 0.999),
                        values=(48.1, 50.2, 52.3),
                    )
                first.mark_complete(
                    "2025fed",
                    28,
                    30,
                    expected_parties=("@TPP", "ALP FP"),
                )

                resumed = fp_model_provenance.CutoffOutputStore()
                self.assertTrue(resumed.begin("2025fed", self.metadata()))
                self.assertTrue(resumed.is_complete("2025fed", 28, 30))
                self.assertFalse(resumed.is_complete("2025fed", 21, 22))

    def test_incompatible_draft_is_discarded_without_touching_certified_output(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                final_path = fp_model_provenance.cutoff_output_path("2025fed")
                final_path.parent.mkdir(parents=True, exist_ok=True)
                final_path.write_text("certified\n", encoding="utf-8")
                first = fp_model_provenance.CutoffOutputStore()
                first.begin("2025fed", self.metadata())
                first.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )

                replacement = fp_model_provenance.CutoffOutputStore()
                self.assertFalse(
                    replacement.begin(
                        "2025fed", self.metadata(fingerprint="inputs-b")
                    )
                )
                self.assertFalse(
                    replacement.contains("2025fed", "@TPP", 28, 30)
                )
                self.assertEqual(
                    final_path.read_text(encoding="utf-8"),
                    "certified\n",
                )

    def test_corrupt_matching_draft_is_discarded(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                first = fp_model_provenance.CutoffOutputStore()
                first.begin("2025fed", self.metadata())
                first.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                working = fp_model_provenance.cutoff_working_path(
                    "2025fed"
                )
                working.write_text(
                    working.read_text(encoding="utf-8").replace(
                        "48.1,50.2,52.3", "not-a-number,50.2,52.3"
                    ),
                    encoding="utf-8",
                )

                resumed = fp_model_provenance.CutoffOutputStore()
                self.assertFalse(resumed.begin("2025fed", self.metadata()))
                self.assertFalse(
                    resumed.contains("2025fed", "@TPP", 28, 30)
                )

    def test_metadata_requires_every_expected_endpoint_before_promotion(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                store.begin("2025fed", self.metadata())
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                store.mark_complete(
                    "2025fed", 28, 30, expected_parties=("@TPP",)
                )
                with self.assertRaisesRegex(
                    fp_model_provenance
                    .generated_provenance.GeneratedProvenanceError,
                    "has no completed party manifest",
                ):
                    store.promote("2025fed")

    def test_rows_are_upserted_atomically(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                store.write(
                    election="2025fed",
                    party="ALP FP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=456,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(30.1, 32.2, 34.3),
                )
                self.assertFalse(store.is_complete("2025fed", 28, 30))
                store.mark_complete("2025fed", 28, 30)

                restored = fp_model_provenance.CutoffOutputStore()
                self.assertTrue(
                    restored.contains("2025fed", "@TPP", 28, 30)
                )
                self.assertTrue(
                    restored.contains("2025fed", "ALP FP", 28, 30)
                )
                self.assertTrue(
                    restored.is_complete("2025fed", 28, 30)
                )

                path = fp_model_provenance.cutoff_output_path("2025fed")
                working_path = (
                    fp_model_provenance.cutoff_working_path("2025fed")
                )
                self.assertFalse(path.exists())
                self.assertTrue(working_path.exists())
                store.promote("2025fed")
                with path.open(newline="", encoding="utf-8") as source:
                    rows = list(csv.reader(source))

            self.assertEqual(
                rows[0],
                [
                    "ScheduledCutoffDays",
                    "PollTrendEndDays",
                    "Party",
                    "StanSeed",
                    "0%",
                    "50%",
                    "100%",
                ],
            )
            self.assertEqual(len(rows), 4)
            self.assertFalse(working_path.exists())
            self.assertFalse(
                working_path.with_suffix(
                    working_path.suffix + ".tmp"
                ).exists()
            )

    def test_promoted_full_percentile_file_matches_trend_adjust_contract(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            probabilities = tuple(
                [0.001]
                + [index * 0.01 for index in range(1, 100)]
                + [0.999]
            )
            values = tuple(40.0 + index * 0.2 for index in range(101))
            metadata = self.metadata()
            metadata["expected_endpoints"] = [{
                "scheduled_cutoff_days": 28,
                "poll_trend_end_days": 30,
            }]
            metadata["parties"] = ["@TPP"]
            metadata["probability_columns"] = [
                "{}%".format(round(probability * 100))
                for probability in probabilities
            ]
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                store.begin("2025fed", metadata)
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=probabilities,
                    values=values,
                )
                store.mark_complete(
                    "2025fed", 28, 30, expected_parties=("@TPP",)
                )
                store.promote("2025fed")
                loaded = trend_adjust_cutoffs.CutoffTrendData(
                    fp_model_provenance.cutoff_output_path("2025fed")
                )

            loaded.require_parties(["@TPP"])
            self.assertAlmostEqual(
                loaded.value_at("@TPP", 30, 50),
                values[50],
            )

    def test_reset_removes_draft_but_preserves_certified_file(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                path = fp_model_provenance.cutoff_output_path("2025fed")
                store.mark_complete("2025fed", 28, 30)
                store.promote("2025fed")
                self.assertTrue(path.exists())
                certified_contents = path.read_bytes()

                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=21,
                    poll_trend_end_days=22,
                    random_seed=456,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.2, 50.3, 52.4),
                )
                working_path = (
                    fp_model_provenance.cutoff_working_path("2025fed")
                )
                self.assertTrue(working_path.exists())
                self.assertEqual(path.read_bytes(), certified_contents)
                store.reset("2025fed")

                self.assertTrue(path.exists())
                self.assertEqual(path.read_bytes(), certified_contents)
                self.assertFalse(working_path.exists())
                self.assertFalse(store.contains("2025fed", "@TPP", 28, 30))

    def test_incomplete_draft_cannot_be_promoted(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )

                with self.assertRaisesRegex(
                    fp_model_provenance
                    .generated_provenance.GeneratedProvenanceError,
                    "Cannot promote incomplete cutoff",
                ):
                    store.promote("2025fed")

                self.assertFalse(
                    fp_model_provenance
                    .cutoff_output_path("2025fed")
                    .exists()
                )

    def test_failed_certification_restores_output_and_keeps_draft(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                store = fp_model_provenance.CutoffOutputStore()
                path = fp_model_provenance.cutoff_output_path("2025fed")
                path.parent.mkdir(parents=True, exist_ok=True)
                previous_contents = (
                    "ScheduledCutoffDays,PollTrendEndDays,Party,"
                    "StanSeed,0%,50%,100%\n"
                    "35,36,#COMPLETE,,,,\n"
                    "35,36,@TPP,456,47.9,50.0,52.1\n"
                )
                path.write_text(previous_contents, encoding="utf-8")
                metadata = self.metadata()
                metadata["expected_endpoints"] = [{
                    "scheduled_cutoff_days": 28,
                    "poll_trend_end_days": 30,
                }]
                metadata["parties"] = ["@TPP"]
                store.begin("2025fed", metadata)
                store.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                store.mark_complete(
                    "2025fed",
                    28,
                    30,
                    expected_parties=("@TPP",),
                )

                def fail_certification(output):
                    raise RuntimeError("certification failed")

                with self.assertRaisesRegex(RuntimeError, "failed"):
                    store.promote(
                        "2025fed",
                        certify=fail_certification,
                    )

                self.assertEqual(
                    path.read_text(encoding="utf-8"),
                    previous_contents,
                )
                self.assertTrue(
                    fp_model_provenance
                    .cutoff_working_path("2025fed")
                    .is_file()
                )
                self.assertTrue(
                    fp_model_provenance
                    .cutoff_working_metadata_path("2025fed")
                    .is_file()
                )
                resumed = fp_model_provenance.CutoffOutputStore()
                self.assertTrue(resumed.begin("2025fed", metadata))
                self.assertTrue(resumed.is_complete("2025fed", 28, 30))

    def test_growing_federal_priors_do_not_invalidate_resume(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            prior_a = directory / "fed_a.csv"
            prior_b = directory / "fed_b.csv"
            prior_a.write_text("a\n", encoding="utf-8")
            prior_b.write_text("b\n", encoding="utf-8")
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                first = fp_model_provenance.CutoffOutputStore()
                first.begin(
                    "2025fed",
                    self.metadata(federal_prior_files=[str(prior_a)]),
                )
                first.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                first.update_federal_priors(
                    "2025fed",
                    [prior_a, prior_b],
                    dependencies={
                        "source": "current",
                        "cutoff_poll_outputs": {"files": ["fed_a", "fed_b"]},
                    },
                )

                resumed = fp_model_provenance.CutoffOutputStore()
                self.assertTrue(
                    resumed.begin(
                        "2025fed",
                        self.metadata(federal_prior_files=[str(prior_a)]),
                    )
                )
                self.assertEqual(
                    set(resumed.federal_prior_files("2025fed")),
                    {str(prior_a), str(prior_b)},
                )

    def test_changed_federal_prior_contents_invalidate_resume(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            prior = directory / "fed_a.csv"
            prior.write_text("a\n", encoding="utf-8")
            with mock.patch.object(
                fp_model_provenance,
                "CUTOFF_OUTPUT_DIRECTORY",
                directory,
            ):
                first = fp_model_provenance.CutoffOutputStore()
                first.begin(
                    "2025fed",
                    self.metadata(federal_prior_files=[str(prior)]),
                )
                first.write(
                    election="2025fed",
                    party="@TPP",
                    scheduled_cutoff_days=28,
                    poll_trend_end_days=30,
                    random_seed=123,
                    probabilities=(0.001, 0.5, 0.999),
                    values=(48.1, 50.2, 52.3),
                )
                prior.write_text("changed\n", encoding="utf-8")

                resumed = fp_model_provenance.CutoffOutputStore()
                self.assertFalse(
                    resumed.begin(
                        "2025fed",
                        self.metadata(federal_prior_files=[str(prior)]),
                    )
                )
                self.assertFalse(
                    resumed.contains("2025fed", "@TPP", 28, 30)
                )


if __name__ == "__main__":
    unittest.main()
