"""Provenance helpers for the long-running poll calibration stages.

Calibration is recorded at the smallest independently completed Stan work
unit: one election, party and excluded pollster for leave-one-pollster-out
calibration, or one election and party for bias calibration. Existing files
predating provenance are fingerprinted as legacy rather than falsely
certified as reproducible generations.
"""

import argparse
import csv
import hashlib
import sys
from collections import defaultdict
from pathlib import Path

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
CALIBRATION_DIRECTORY = ANALYSIS_DIRECTORY / "Outputs" / "Calibration"
MANIFEST_PATH = CALIBRATION_DIRECTORY / "generated-provenance.json"
MANIFEST_DESCRIPTION = (
    "Bundled provenance for Stan calibration compatibility inputs and compact "
    "election-level calibration summaries."
)
POLL_COMPATIBILITY_CATEGORY = "poll_calibration_compatibility_inputs"
BIAS_COMPATIBILITY_CATEGORY = "bias_calibration_compatibility_inputs"
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
SIGNIFICANT_PARTIES_PATH = (
    ANALYSIS_DIRECTORY / "Data" / "significant-parties.csv"
)


def configured_parties_by_election(path=None):
    """Return the parties currently produced by each calibration run."""

    path = Path(path or SIGNIFICANT_PARTIES_PATH)
    parties = {}
    try:
        with path.open(newline="", encoding="utf-8-sig") as source:
            for line_number, row in enumerate(csv.reader(source), start=1):
                if not row:
                    continue
                if len(row) < 3:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} must contain an election and parties".format(
                            path, line_number
                        )
                    )
                election = "{}{}".format(row[0], row[1])
                if election in parties:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} duplicates election {}".format(
                            path, line_number, election
                        )
                    )
                parties[election] = set(row[2:])
    except OSError as error:
        raise generated_provenance.GeneratedProvenanceError(
            "could not read significant parties from {}: {}".format(
                path, error
            )
        ) from error
    return parties


def _compatibility_record_key(category, election, party, role):
    return "{}:{}:{}:{}".format(
        category,
        election, party, role
    )


def _loo_summary_record_key(election):
    return "calibration_compatibility_inputs:{}:loo-summary".format(election)


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
            record_key = _compatibility_record_key(
                BIAS_COMPATIBILITY_CATEGORY,
                election,
                party,
                "bias-calibration",
            )
            category = BIAS_COMPATIBILITY_CATEGORY
            stage = "calibrate_pollster_bias"
            scope = _scope(election, party)
        else:
            record_key = _compatibility_record_key(
                POLL_COMPATIBILITY_CATEGORY,
                election,
                party,
                excluded_pollster or "full",
            )
            category = POLL_COMPATIBILITY_CATEGORY
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
        self.pending_records[_loo_summary_record_key(election)] = (
            generated_provenance.generation_record(
                category=POLL_COMPATIBILITY_CATEGORY,
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

    def record_bias_staging(self, election, output):
        """Record the small direct-output hand-off from the bias pass."""

        self.pending_records[_compatibility_record_key(
            BIAS_COMPATIBILITY_CATEGORY,
            election,
            "summary",
            "bias-staging",
        )] = generated_provenance.generation_record(
            category=BIAS_COMPATIBILITY_CATEGORY,
            stage="calibrate_pollster_bias",
            scope=_scope(election),
            run=self.run_id,
            dependencies=dict(self.dependencies),
            outputs=generated_provenance.output_fingerprints(
                [output], ANALYSIS_DIRECTORY
            ),
            random_seed=None,
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
                key = _compatibility_record_key(
                    BIAS_COMPATIBILITY_CATEGORY,
                    election,
                    party,
                    "bias-calibration",
                )
                category = BIAS_COMPATIBILITY_CATEGORY
                stage = "calibrate_pollster_bias"
                scope = _scope(election, party)
            else:
                key = _compatibility_record_key(
                    POLL_COMPATIBILITY_CATEGORY,
                    election,
                    party,
                    suffix or "full",
                )
                category = POLL_COMPATIBILITY_CATEGORY
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
            if stage == "calibrate_pollster_bias"
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
        records[_loo_summary_record_key(election)] = (
            generated_provenance.generation_record(
                category=POLL_COMPATIBILITY_CATEGORY,
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
        existing_outputs = {
            output
            for record in existing["records"].values()
            for output in record["outputs"]
        }
        records = {
            key: record
            for key, record in records.items()
            if (
                key not in existing["records"]
                or existing["records"][key]["status"] == "legacy"
            ) and not set(record["outputs"]) & existing_outputs
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
