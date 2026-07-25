"""Provenance helpers for the long-running poll calibration stages.

Calibration is recorded at the smallest independently completed Stan work
unit: one election, party and excluded pollster for leave-one-pollster-out
calibration, or one election and party for bias calibration. Existing files
predating provenance are fingerprinted as legacy rather than falsely
certified as reproducible generations.
"""

import argparse
import hashlib
import sys
from collections import defaultdict
from pathlib import Path

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
CALIBRATION_DIRECTORY = ANALYSIS_DIRECTORY / "Outputs" / "Calibration"
MANIFEST_PATH = CALIBRATION_DIRECTORY / "generated-provenance.json"
MANIFEST_DESCRIPTION = (
    "Bundled provenance for Stan poll calibration traces, bias calibration "
    "outputs and compact leave-one-pollster-out summaries."
)
SOURCE_DEPENDENCIES = {
    "election_catalogue": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "raw_poll_data": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "poll_model_configuration":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "preference_estimates": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "prior_result_inputs": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "fp_model_script": ANALYSIS_DIRECTORY / "provenance.json",
    "stan_cache_script": ANALYSIS_DIRECTORY / "provenance.json",
    "election_code_script": ANALYSIS_DIRECTORY / "provenance.json",
    "fp_stan_model": ANALYSIS_DIRECTORY / "Models" / "provenance.json",
}
MODEL_OUTPUT_KINDS = ("trend", "polls", "house_effects")


def _trace_record_key(election, party, excluded_pollster):
    role = excluded_pollster if excluded_pollster else "full"
    return "poll_calibration_traces:{}:{}:{}".format(
        election, party, role
    )


def _bias_record_key(election, party):
    return "bias_calibration_outputs:{}:{}".format(election, party)


def _summary_record_key(election):
    return "poll_calibration_summaries:{}".format(election)


def _scope(election, party=None, excluded_pollster=None):
    qualifiers = {}
    if excluded_pollster is not None:
        qualifiers["excluded_pollster"] = (
            excluded_pollster if excluded_pollster else "full"
        )
    return generated_provenance.generation_scope(
        elections=[election],
        parties=[party] if party else [],
        qualifiers=qualifiers,
    )


def _source_dependencies():
    return {
        category: generated_provenance.source_manifest_dependency(
            category,
            manifest_path,
            ANALYSIS_DIRECTORY,
        )
        for category, manifest_path in SOURCE_DEPENDENCIES.items()
    }


def derive_stan_seed(base_seed, election, party, excluded_pollster, mode):
    """Derive a stable, independent Stan seed for one calibration unit."""

    material = "\0".join(
        (
            str(base_seed),
            election,
            party,
            excluded_pollster,
            mode,
        )
    ).encode("utf-8")
    digest = hashlib.sha256(material).digest()
    return int.from_bytes(digest[:8], "big") % (2 ** 31 - 1) + 1


class CalibrationRecorder:
    """Buffer and atomically persist completed calibration work units."""

    def __init__(self, command):
        self.dependencies = _source_dependencies()
        self.pending_records = {}
        self.run_id, self.run = generated_provenance.generation_run(
            command=command,
            source_revision=generated_provenance.current_source_revision(
                ANALYSIS_DIRECTORY
            ),
            environment=generated_provenance.current_environment(
                ("numpy", "pandas", "pystan")
            ),
        )

    def record_model_outputs(
        self,
        election,
        party,
        excluded_pollster,
        bias_calibration,
        outputs,
        random_seed,
        feedback_files,
    ):
        dependencies = dict(self.dependencies)
        if feedback_files:
            dependencies["poll_trend_outputs"] = (
                generated_provenance.file_dependency(
                    "poll_trend_outputs",
                    feedback_files,
                    ANALYSIS_DIRECTORY,
                )
            )
        if bias_calibration:
            record_key = _bias_record_key(election, party)
            category = "bias_calibration_outputs"
            stage = "calibrate_pollster_bias"
            scope = _scope(election, party)
        else:
            record_key = _trace_record_key(
                election, party, excluded_pollster
            )
            category = "poll_calibration_traces"
            stage = "calibrate_pollsters"
            scope = _scope(election, party, excluded_pollster)

        self.pending_records[record_key] = (
            generated_provenance.generation_record(
                category=category,
                stage=stage,
                scope=scope,
                run=self.run_id,
                dependencies=dependencies,
                outputs=generated_provenance.output_fingerprints(
                    outputs, ANALYSIS_DIRECTORY
                ),
                random_seed=random_seed,
            )
        )

    def record_summaries(self, election, outputs, trace_files):
        dependencies = dict(self.dependencies)
        if trace_files:
            dependencies["poll_calibration_traces"] = (
                generated_provenance.file_dependency(
                    "poll_calibration_traces",
                    trace_files,
                    ANALYSIS_DIRECTORY,
                )
            )
        self.pending_records[_summary_record_key(election)] = (
            generated_provenance.generation_record(
                category="poll_calibration_summaries",
                stage="calibrate_pollsters",
                scope=_scope(election),
                run=self.run_id,
                dependencies=dependencies,
                outputs=generated_provenance.output_fingerprints(
                    outputs, ANALYSIS_DIRECTORY
                ),
                random_seed=None,
            )
        )

    def flush(self):
        if not self.pending_records:
            return
        generated_provenance.update_manifest(
            MANIFEST_PATH,
            self.pending_records,
            {self.run_id: self.run},
            path_base="../..",
            description=MANIFEST_DESCRIPTION,
        )
        self.pending_records = {}


def _parse_model_output(path):
    for kind in MODEL_OUTPUT_KINDS:
        prefix = "fp_{}_".format(kind)
        if not path.name.startswith(prefix) or path.suffix != ".csv":
            continue
        parts = path.stem[len(prefix):].split("_", 2)
        if len(parts) < 2:
            return None
        election, party = parts[:2]
        suffix = parts[2] if len(parts) == 3 else ""
        return kind, election, party, suffix
    return None


def _legacy_records():
    model_groups = defaultdict(list)
    summary_groups = defaultdict(list)
    for path in sorted(CALIBRATION_DIRECTORY.glob("*.csv")):
        parsed = _parse_model_output(path)
        if parsed:
            kind, election, party, suffix = parsed
            if suffix == "biascal":
                key = _bias_record_key(election, party)
                category = "bias_calibration_outputs"
                stage = "calibrate_pollster_bias"
                scope = _scope(election, party)
            else:
                key = _trace_record_key(election, party, suffix)
                category = "poll_calibration_traces"
                stage = "calibrate_pollsters"
                scope = _scope(election, party, suffix)
            model_groups[
                (key, category, stage, election, party, suffix)
            ].append(path)
            continue
        if path.name.startswith("calib_"):
            parts = path.stem.split("_", 2)
            if len(parts) >= 2:
                summary_groups[parts[1]].append(path)

    records = {}
    for group, outputs in model_groups.items():
        key, category, stage, election, party, suffix = group
        scope = (
            _scope(election, party)
            if category == "bias_calibration_outputs"
            else _scope(election, party, suffix)
        )
        records[key] = generated_provenance.generation_record(
            category=category,
            stage=stage,
            scope=scope,
            run="legacy-calibration-baseline",
            dependencies={},
            outputs=generated_provenance.output_fingerprints(
                outputs, ANALYSIS_DIRECTORY
            ),
            random_seed=None,
            status="legacy",
        )
    for election, outputs in summary_groups.items():
        records[_summary_record_key(election)] = (
            generated_provenance.generation_record(
                category="poll_calibration_summaries",
                stage="calibrate_pollsters",
                scope=_scope(election),
                run="legacy-calibration-baseline",
                dependencies={},
                outputs=generated_provenance.output_fingerprints(
                    outputs, ANALYSIS_DIRECTORY
                ),
                random_seed=None,
                status="legacy",
            )
        )
    return records


def baseline_existing_outputs():
    records = _legacy_records()
    if MANIFEST_PATH.exists():
        existing = generated_provenance.load_manifest(MANIFEST_PATH)
        records = {
            key: record
            for key, record in records.items()
            if (
                key not in existing["records"]
                or existing["records"][key]["status"] == "legacy"
            )
        }
    run = {
        "generated_at_utc": generated_provenance.utc_now(),
        "command": [Path(sys.executable).name] + sys.argv,
        "source_revision": generated_provenance.current_source_revision(
            ANALYSIS_DIRECTORY
        ),
        "environment": generated_provenance.current_environment(),
    }
    manifest = generated_provenance.update_manifest(
        MANIFEST_PATH,
        records,
        {"legacy-calibration-baseline": run},
        path_base="../..",
        description=MANIFEST_DESCRIPTION,
    )
    legacy_count = sum(
        record["status"] == "legacy"
        for record in manifest["records"].values()
    )
    print(
        "Recorded {} legacy calibration work units in {}".format(
            legacy_count, MANIFEST_PATH
        )
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Maintain provenance for poll calibration outputs."
    )
    parser.add_argument(
        "command",
        choices=("baseline",),
        help=(
            "baseline fingerprints existing outputs without claiming they "
            "were reproduced under the current sources"
        ),
    )
    args = parser.parse_args(argv)
    if args.command == "baseline":
        baseline_existing_outputs()
        return 0
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())
