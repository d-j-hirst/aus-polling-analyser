import unittest

import required_work


class RequiredWorkTests(unittest.TestCase):
    def setUp(self):
        self.registry = {
            "categories": {
                "final": {"kind": "generated"},
                "upstream": {"kind": "generated"},
                "authored": {"kind": "authored"},
            },
            "consumers": [
                {"id": "cpp_stan_model", "inputs": ["final", "authored"]}
            ],
        }
        self.work_units = [
            self.unit("h-final", "final", ["2022sa"], ["h-upstream"]),
            self.unit("h-upstream", "upstream", ["2022sa"]),
            self.unit("a-final", "final", ["2028fed"], ["a-upstream"]),
            self.unit("a-upstream", "upstream", ["2028fed"]),
            self.unit("i-final", "final", ["2028qld"], ["i-upstream"]),
            self.unit("i-upstream", "upstream", ["2028qld"]),
            self.unit("orphan", "upstream", ["1984fed"]),
            self.unit("global", "final", []),
        ]

    @staticmethod
    def unit(identifier, category, elections, dependencies=None):
        return {
            "id": identifier,
            "category": category,
            "scope": {"all": False, "elections": elections},
            "dependencies": dependencies or [],
        }

    def classify(self, explicit_elections=None):
        return required_work.classify_required_work(
            self.work_units,
            self.registry,
            explicit_elections=explicit_elections,
            historical_elections={"2022sa"},
            active_elections={"2028fed"},
            inactive_elections={"2028qld"},
        )

    def test_repository_selection_uses_historical_and_active_roots(self):
        selection = self.classify()

        self.assertEqual(
            selection.required_ids,
            {"h-final", "h-upstream", "a-final", "a-upstream", "global"},
        )
        self.assertEqual(
            selection.inactive_only_ids,
            {"i-final", "i-upstream"},
        )
        self.assertEqual(selection.unreferenced_ids, {"orphan"})

    def test_explicit_inactive_election_overrides_operational_status(self):
        selection = self.classify({"2028qld"})

        self.assertEqual(
            selection.required_ids,
            {"i-final", "i-upstream", "global"},
        )

    def test_only_generated_cpp_inputs_are_roots(self):
        self.assertEqual(
            required_work.cpp_input_categories(self.registry), {"final"}
        )

    def test_default_election_is_not_an_unconditional_global_root(self):
        fallback = self.unit("fallback", "final", ["0none"])
        self.work_units.append(fallback)

        selection = self.classify()

        self.assertNotIn("fallback", selection.required_ids)
        self.assertIn("fallback", selection.unreferenced_ids)


if __name__ == "__main__":
    unittest.main()
