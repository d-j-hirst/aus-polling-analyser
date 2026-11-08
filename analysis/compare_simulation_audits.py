"""Compare unrounded main-run draws without using the separate live baseline.

The audit records are indexed by byte offset, then read one iteration at a time.
Memory therefore depends on the number of iterations, not all their seat values.
Thread completion order does not matter. Every exact difference is reported in
the counts; numeric magnitudes are supplementary, never a suppression threshold.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

STAGES = ("before_live", "after_live", "after_preferences", "reported")


def index_file(path):
    offsets = {}
    metadata = None
    completion = None
    with Path(path).open("rb") as stream:
        while True:
            offset = stream.tell()
            line = stream.readline()
            if not line:
                break
            record = json.loads(line)
            kind = record["kind"]
            if kind == "metadata":
                if metadata is not None or record["schema"] != 1:
                    raise ValueError("Unsupported or repeated audit metadata")
                metadata = record
            elif kind == "complete":
                completion = record["iterations"]
            elif kind == "iteration":
                iteration = record["iteration"]
                if iteration in offsets:
                    raise ValueError(f"Repeated iteration {iteration}")
                offsets[iteration] = offset
            else:
                raise ValueError(f"Unknown audit record: {kind}")
    if metadata is None or completion != metadata["iterations"]:
        raise ValueError(f"Audit did not finish: {path}")
    if set(offsets) != set(range(completion)):
        raise ValueError(f"Incomplete iteration coverage: {path}")
    return metadata, offsets


def differences(left, right, path=""):
    if type(left) is not type(right):
        yield path, left, right
    elif isinstance(left, dict):
        for key in sorted(left.keys() | right.keys()):
            child = f"{path}/{key}"
            if key not in left or key not in right:
                yield child, left.get(key), right.get(key)
            else:
                yield from differences(left[key], right[key], child)
    elif isinstance(left, list):
        if len(left) != len(right):
            yield path + "/length", len(left), len(right)
        for index, (a, b) in enumerate(zip(left, right)):
            yield from differences(a, b, f"{path}/{index}")
    elif left != right:
        yield path, left, right


def compare(left_path, right_path):
    left_meta, left_index = index_file(left_path)
    right_meta, right_index = index_file(right_path)
    for field in ("election", "iterations", "seats", "parties"):
        if left_meta[field] != right_meta[field]:
            raise ValueError(f"Cannot pair audits with different {field}")
    totals = Counter()
    first_stages = Counter()
    affected_seats = Counter()
    maximum = defaultdict(float)
    examples = []
    retry_mismatches = 0
    with Path(left_path).open("rb") as left, Path(right_path).open("rb") as right:
        for iteration in sorted(left_index):
            left.seek(left_index[iteration])
            right.seek(right_index[iteration])
            a, b = json.loads(left.readline()), json.loads(right.readline())
            if a["retry"] != b["retry"]:
                retry_mismatches += 1
            first = True
            for stage in STAGES:
                x, y = a["stages"][stage], b["stages"][stage]
                if len(x) != len(left_meta["seats"]) or len(y) != len(x):
                    raise ValueError("Audit seat coverage differs from metadata")
                for seat, (sx, sy) in enumerate(zip(x, y)):
                    changes = list(differences(sx, sy))
                    if not changes:
                        continue
                    totals[stage] += 1  # one changed seat/draw at this stage
                    affected_seats[left_meta["seats"][seat]] += 1
                    if first:
                        first_stages[stage] += 1
                        first = False
                    for field, before, after in changes:
                        # Only floating electoral quantities have a meaningful
                        # numerical distance. Candidate IDs and lengths do not.
                        if isinstance(before, float) and isinstance(after, float):
                            maximum[stage + field] = max(maximum[stage + field], abs(after-before))
                        if len(examples) < 12:
                            examples.append(dict(iteration=iteration, stage=stage,
                                                 seat=left_meta["seats"][seat], field=field,
                                                 standard=before, live=after))
    return dict(iterations=left_meta["iterations"], retry_mismatches=retry_mismatches,
                changed_seat_draws=dict(totals), first_changed_stage=dict(first_stages),
                affected_seats=dict(affected_seats), maximum_absolute_differences=dict(maximum),
                examples=examples, exact_match=not totals and retry_mismatches == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("standard", type=Path)
    parser.add_argument("live", type=Path)
    args = parser.parse_args()
    result = compare(args.standard, args.live)
    print(json.dumps(result, indent=2))
    return 0 if result["exact_match"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
