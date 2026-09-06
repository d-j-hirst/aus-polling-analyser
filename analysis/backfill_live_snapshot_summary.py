"""Copy compact live-summary fields from analysis sidecars into main snapshots.

Seat completions, TPP booth/vote-type evidence, projected 2PP, and raw 2PP
deviation already exist on each ``snapshot_*__run_*.analysis.json`` sidecar.
Live Booths reads the sibling main file, which historically omitted them.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


COMPLETION_KEYS = (
    ("seat_fp_completion", "fp_completion"),
    ("seat_tpp_completion", "tpp_completion"),
    ("seat_tcp_completion", "tcp_completion"),
)

HIGH_PRECISION_KEYS = {
    "seat_fp_completion",
    "seat_tpp_completion",
    "seat_tcp_completion",
    "bias",
    "std_dev",
    "raw",
    "raw_2pp_deviation",
}


def format_export_float(value, decimals):
    if not math.isfinite(value):
        return "null"
    scale = 10 ** decimals
    rounded = round(value * scale) / scale
    if rounded == 0.0:
        return "0"
    text = f"{rounded:.{decimals}f}".rstrip("0").rstrip(".")
    return text or "0"


def append_compact(parts, value, parent_key=""):
    if value is None:
        parts.append("null")
    elif isinstance(value, bool):
        parts.append("true" if value else "false")
    elif isinstance(value, int):
        parts.append(str(value))
    elif isinstance(value, float):
        decimals = 4 if parent_key in HIGH_PRECISION_KEYS else 3
        parts.append(format_export_float(value, decimals))
    elif isinstance(value, str):
        parts.append(json.dumps(value, ensure_ascii=False))
    elif isinstance(value, list):
        parts.append("[")
        for index, item in enumerate(value):
            if index:
                parts.append(",")
            append_compact(parts, item, parent_key)
        parts.append("]")
    elif isinstance(value, dict):
        parts.append("{")
        for index, (key, item) in enumerate(value.items()):
            if index:
                parts.append(",")
            parts.append(json.dumps(key, ensure_ascii=False))
            parts.append(":")
            append_compact(parts, item, key)
        parts.append("}")
    else:
        parts.append(json.dumps(value, ensure_ascii=False))


def dump_compact_json(value):
    parts = []
    append_compact(parts, value)
    return "".join(parts)


def seat_nodes_by_name(analysis):
    nodes = {}
    seats = analysis.get("seats")
    if not isinstance(seats, list):
        return nodes
    for seat in seats:
        if not isinstance(seat, dict):
            continue
        name = seat.get("name")
        node = seat.get("node")
        if isinstance(name, str) and isinstance(node, dict):
            nodes[name] = node
    return nodes


def completion_value(node, key):
    value = node.get(key, 0.0)
    if value is None or isinstance(value, bool):
        return 0.0
    if isinstance(value, (int, float)):
        return value
    return 0.0


def completions_for_seats(analysis, seat_names):
    nodes = seat_nodes_by_name(analysis)
    result = {}
    for report_key, node_key in COMPLETION_KEYS:
        values = []
        for name in seat_names:
            node = nodes.get(name, {})
            values.append(completion_value(node, node_key))
        result[report_key] = values
    return result


def copy_category_array(tpp, key):
    value = tpp.get(key)
    return value if isinstance(value, list) else []


def live_summary_from_analysis(analysis):
    biases = analysis.get("category_biases")
    tpp = biases.get("tpp") if isinstance(biases, dict) else {}
    if not isinstance(tpp, dict):
        tpp = {}
    election = analysis.get("election")
    if not isinstance(election, dict):
        election = {}
    node = election.get("node")
    if not isinstance(node, dict):
        node = {}
    deviation = node.get("tpp_deviation")
    if deviation is None:
        deviation = 0.0
    summary = {
        "booth_type": copy_category_array(tpp, "booth_type"),
        "vote_type": copy_category_array(tpp, "vote_type"),
        "raw_2pp_deviation": deviation,
    }
    if "projected_2pp" in election:
        summary["projected_2pp"] = election["projected_2pp"]
    return summary


def apply_sidecar_to_document(document, analysis):
    report = document.get("simulation_report")
    if not isinstance(report, dict):
        report = {}
        document["simulation_report"] = report
    seat_names = report.get("seat_name")
    if not isinstance(seat_names, list):
        seat_names = []
    report.update(completions_for_seats(analysis, seat_names))

    summary = document.get("live_analysis_summary")
    if not isinstance(summary, dict):
        summary = {}
        document["live_analysis_summary"] = summary
    summary.update(live_summary_from_analysis(analysis))
    return document


def is_main_snapshot(path):
    name = path.name.lower()
    return (
        name.startswith("snapshot_")
        and name.endswith(".json")
        and not name.endswith(".analysis.json")
        and not name.endswith(".tmp")
    )


def analysis_for_snapshot(path, document):
    sidecar = path.with_name(path.stem + ".analysis.json")
    if sidecar.is_file():
        with sidecar.open(encoding="utf-8") as handle:
            return json.load(handle), sidecar
    live_analysis = document.get("live_analysis")
    if isinstance(live_analysis, dict):
        return live_analysis, None
    return None, sidecar


def iter_main_snapshots(root):
    for path in sorted(root.rglob("snapshot_*.json")):
        if is_main_snapshot(path):
            yield path


def backfill_snapshot(path, dry_run=False):
    with path.open(encoding="utf-8") as handle:
        document = json.load(handle)
    analysis, sidecar = analysis_for_snapshot(path, document)
    if analysis is None:
        return "missing-analysis", sidecar
    apply_sidecar_to_document(document, analysis)
    serialised = dump_compact_json(document)
    if dry_run:
        return "dry-run", sidecar
    path.write_text(serialised, encoding="utf-8")
    return "updated", sidecar


def default_root():
    return Path(__file__).resolve().parent.parent / "live_runs"


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description=(
            "Copy seat completions and TPP live-summary fields from "
            "analysis sidecars into main live snapshot files."
        )
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=default_root(),
        help="Directory to search for snapshot_*.json files.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Load and patch in memory without rewriting files.",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    root = args.root
    if not root.is_dir():
        print(f"No live_runs directory at {root}", file=sys.stderr)
        return 1

    counts = {"updated": 0, "dry-run": 0, "missing-analysis": 0}
    for path in iter_main_snapshots(root):
        status, sidecar = backfill_snapshot(path, dry_run=args.dry_run)
        counts[status] += 1
        if status == "missing-analysis":
            print(f"skip {path}: no sidecar {sidecar}", file=sys.stderr)

    print(
        f"{counts['updated'] + counts['dry-run']} snapshots processed, "
        f"{counts['missing-analysis']} missing analysis, "
        f"dry_run={args.dry_run}"
    )
    return 0 if counts["missing-analysis"] == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
