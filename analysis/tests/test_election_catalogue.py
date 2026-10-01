import tempfile
import unittest
from pathlib import Path

import election_catalogue


class ElectionCatalogueTests(unittest.TestCase):
    def test_future_status_sets_preserve_all_open_terms(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "future-elections.csv"
            path.write_text(
                "2026,vic,active\n2028,qld,inactive\n",
                encoding="utf-8",
            )

            self.assertEqual(
                election_catalogue.configured_future_elections(path),
                {"2026vic", "2028qld"},
            )
            self.assertEqual(
                election_catalogue.open_future_elections(path),
                {"2026vic", "2028qld"},
            )
            self.assertEqual(
                election_catalogue.active_future_elections(path),
                {"2026vic"},
            )
            self.assertEqual(
                election_catalogue.inactive_future_elections(path),
                {"2028qld"},
            )

    def test_future_catalogue_requires_status(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "future-elections.csv"
            path.write_text("2026,vic\n", encoding="utf-8")

            with self.assertRaisesRegex(
                election_catalogue.ElectionCatalogueError,
                "active/inactive status",
            ):
                election_catalogue.load_future_elections(path)

    def test_future_catalogue_rejects_unknown_status_and_duplicates(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "future-elections.csv"
            path.write_text("2026,vic,pending\n", encoding="utf-8")
            with self.assertRaisesRegex(
                election_catalogue.ElectionCatalogueError,
                "unsupported status",
            ):
                election_catalogue.load_future_elections(path)

            path.write_text(
                "2026,vic,active\n2026,VIC,inactive\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                election_catalogue.ElectionCatalogueError,
                "duplicates election 2026vic",
            ):
                election_catalogue.load_future_elections(path)

    def test_historical_catalogue_is_strict_and_normalized(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "polled-elections.csv"
            path.write_text("2022,SA\n2025,fed\n", encoding="utf-8")

            self.assertEqual(
                election_catalogue.historical_elections(path),
                {"2022sa", "2025fed"},
            )


if __name__ == "__main__":
    unittest.main()
