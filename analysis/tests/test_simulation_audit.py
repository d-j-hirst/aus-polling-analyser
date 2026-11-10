"""Check comparison ordering, exact differences and rejection of partial traces."""
import json
from pathlib import Path
import tempfile
import unittest

from scripts.diagnostics import compare_simulation_audits as audit


class AuditTests(unittest.TestCase):
    def write(self, path, order=(0, 1), delta=0, complete=True):
        records = [dict(kind="metadata", schema=1, election="test", seats=["Seat"], parties=["ALP", "LNP"], iterations=2)]
        for index in order:
            stages = {stage: [dict(tpp_margin=1.0)] for stage in audit.STAGES}
            if index == 1:
                stages["after_live"][0]["tpp_margin"] += delta
            records.append(dict(kind="iteration", iteration=index, retry=0, stages=stages))
        if complete:
            records.append(dict(kind="complete", iterations=2))
        path.write_text("\n".join(map(json.dumps, records)), encoding="utf-8")

    def test_order_and_tiny_difference(self):
        with tempfile.TemporaryDirectory() as directory:
            left, right = [Path(directory) / name for name in ("a.jsonl", "b.jsonl")]
            self.write(left)
            self.write(right, order=(1, 0))
            self.assertTrue(audit.compare(left, right)["exact_match"])
            self.write(right, order=(1, 0), delta=1e-12)
            result = audit.compare(left, right)
            self.assertFalse(result["exact_match"])
            self.assertEqual(result["changed_seat_draws"], {"after_live": 1})
            self.assertEqual(result["first_changed_stage"], {"after_live": 1})

    def test_partial_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "partial.jsonl"
            self.write(path, complete=False)
            with self.assertRaises(ValueError):
                audit.index_file(path)


if __name__ == "__main__":
    unittest.main()
