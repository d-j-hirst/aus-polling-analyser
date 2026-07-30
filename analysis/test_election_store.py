import unittest
from unittest import mock

import election_store


class FakeElectionCode:
    def __init__(self, code):
        self.code = code

    def short(self):
        return self.code


class FakeElections:
    def items(self):
        return [
            (FakeElectionCode("2024qld"), object()),
            (FakeElectionCode("2025fed"), object()),
        ]


class ElectionStoreTests(unittest.TestCase):
    def test_current_exports_are_not_rewritten(self):
        elections = FakeElections()

        with mock.patch.object(
            election_store.generated_provenance,
            "generated_manifest_dependency",
        ) as check_dependency:
            with mock.patch.object(
                election_store, "store_elections"
            ) as store:
                with mock.patch.object(
                    election_store, "record_generated_provenance"
                ) as record:
                    refreshed = election_store.ensure_election_exports(
                        elections
                    )

        self.assertFalse(refreshed)
        store.assert_not_called()
        record.assert_not_called()
        self.assertEqual(
            check_dependency.call_args.args[2],
            [
                "election_result_exports:2024qld",
                "election_result_exports:2025fed",
            ],
        )

    def test_stale_exports_are_rewritten_and_recorded(self):
        elections = FakeElections()
        stored_elections = [("2024qld", "results_2024qld.csv")]

        with mock.patch.object(
            election_store.generated_provenance,
            "generated_manifest_dependency",
            side_effect=(
                election_store.generated_provenance
                .GeneratedProvenanceError("stale output")
            ),
        ):
            with mock.patch.object(
                election_store,
                "store_elections",
                return_value=stored_elections,
            ) as store:
                with mock.patch.object(
                    election_store, "record_generated_provenance"
                ) as record:
                    refreshed = election_store.ensure_election_exports(
                        elections
                    )

        self.assertTrue(refreshed)
        store.assert_called_once_with(elections)
        record.assert_called_once_with(stored_elections)


if __name__ == "__main__":
    unittest.main()
