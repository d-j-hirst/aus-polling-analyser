import tempfile
import unittest
from pathlib import Path
from unittest import mock

import region_model_provenance


class RegionalWorkUnitTests(unittest.TestCase):
    def test_only_files_with_actual_poll_rows_require_generation(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            regional_directory = Path(temporary_directory)
            (regional_directory / "2027nsw-polls.csv").write_text(
                "StartDate,EndDate,Firm,Size,State,Metro,Regional\n"
                "#N/A,#N/A,Election,#N/A,54,55,49\n",
                encoding="utf-8",
            )
            (regional_directory / "2027nsw-polls-ON.csv").write_text(
                "StartDate,EndDate,Firm,Size,State,Metro,Regional\n"
                "#N/A,#N/A,Election,#N/A,6,4,8\n"
                "2026-02-16,2026-02-19,Morgan,1,30,25.5,38\n",
                encoding="utf-8",
            )
            output = (
                regional_directory
                / "2027nsw-swing-deviations-on.csv"
            )
            output.write_text("metro,regional\n1,2\n", encoding="utf-8")

            with mock.patch.object(
                region_model_provenance,
                "REGIONAL_DIRECTORY",
                regional_directory,
            ):
                work_units = (
                    region_model_provenance.required_work_units()
                )

            self.assertEqual(
                set(work_units),
                {
                    "regional_swing_deviations:2027nsw:ONP FP",
                },
            )
            self.assertEqual(
                work_units[
                    "regional_swing_deviations:2027nsw:ONP FP"
                ]["output"],
                output,
            )

    def test_seed_is_stable_and_separates_parties(self):
        tpp_seed = region_model_provenance.derive_stan_seed(
            123, "2028fed", "@TPP"
        )
        self.assertEqual(
            tpp_seed,
            region_model_provenance.derive_stan_seed(
                123, "2028fed", "@TPP"
            ),
        )
        self.assertNotEqual(
            tpp_seed,
            region_model_provenance.derive_stan_seed(
                123, "2028fed", "ONP FP"
            ),
        )
        self.assertTrue(1 <= tpp_seed < 2 ** 31)


if __name__ == "__main__":
    unittest.main()
