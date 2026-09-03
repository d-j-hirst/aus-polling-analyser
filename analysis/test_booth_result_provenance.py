import importlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import booth_result_provenance
import fetch_booth_results


class BoothResultProvenanceTests(unittest.TestCase):
    def test_supported_work_excludes_deferred_2022_victoria(self):
        work = booth_result_provenance.required_work_units()

        self.assertEqual(
            set(work),
            {
                "booth_result_archives:2015nsw",
                "booth_result_archives:2018vic",
                "booth_result_archives:2019nsw",
                "booth_result_archives:2020qld",
                "booth_result_archives:2022sa",
            },
        )
        self.assertNotIn(
            "booth_result_archives:2022vic",
            work,
        )

    def test_required_work_can_be_scoped(self):
        work = booth_result_provenance.required_work_units(
            ["2022sa", "2022vic"]
        )

        self.assertEqual(
            list(work),
            ["booth_result_archives:2022sa"],
        )

    def test_sa_records_authored_conversion_inputs(self):
        with mock.patch.object(
            booth_result_provenance,
            "_source_dependency",
            side_effect=lambda category: category,
        ):
            dependencies = booth_result_provenance.dependencies_for("2022sa")
            nsw_dependencies = booth_result_provenance.dependencies_for(
                "2019nsw"
            )

        self.assertIn("sa_booth_result_inputs", dependencies)
        self.assertNotIn("sa_booth_result_inputs", nsw_dependencies)

    def test_dispatcher_runs_generator_before_recording(self):
        calls = []
        with mock.patch.object(
            fetch_booth_results.subprocess,
            "run",
            side_effect=lambda *args, **kwargs: calls.append("run"),
        ), mock.patch.object(
            fetch_booth_results.booth_result_provenance,
            "record_generated_output",
            side_effect=lambda *args, **kwargs: calls.append("record")
            or Path("2022sa.json"),
        ):
            result = fetch_booth_results.main(
                ["--election", "2022sa"]
            )

        self.assertEqual(result, 0)
        self.assertEqual(calls, ["run", "record"])

    def test_legacy_scrapers_are_import_safe(self):
        for module_name in (
            "fetch_election_data_nsw",
            "fetch_election_data_qld",
            "fetch_election_data_vic",
        ):
            with self.subTest(module=module_name):
                module = importlib.import_module(module_name)
                self.assertIsNone(module.driver)

    def test_record_rejects_missing_output(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            with mock.patch.object(
                booth_result_provenance,
                "BOOTH_RESULTS_DIRECTORY",
                directory,
            ):
                with self.assertRaisesRegex(
                    booth_result_provenance.BoothResultProvenanceError,
                    "did not create",
                ):
                    booth_result_provenance.record_generated_output("2022sa")


if __name__ == "__main__":
    unittest.main()
