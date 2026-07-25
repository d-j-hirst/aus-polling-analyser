"""Record provenance for compact pollster-parameter outputs.

One work unit contains the variability, house-effect weighting and bias files
for a target election. Historical calibration may legitimately remain stale
for long periods, so generation is permitted from stale calibration records;
that inherited calibration staleness remains visible in the resulting record.
"""

import argparse
import re
import sys
from pathlib import Path

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
CALIBRATION_DIRECTORY = ANALYSIS_DIRECTORY / "Outputs" / "Calibration"
CALIBRATION_MANIFEST_PATH = (
    CALIBRATION_DIRECTORY / "generated-provenance.json"
)
MANIFEST_PATH = (
    CALIBRATION_DIRECTORY / "pollster-generated-provenance.json"
)
MANIFEST_DESCRIPTION = (
    "Bundled provenance for compact election-level pollster variability, "
    "house-effect weighting and bias parameters."
)
SOURCE_DEPENDENCIES = {
    "election_catalogue": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "poll_model_configuration":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "pollster_relationships":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "eventual_result_inputs":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "pollster_analysis_script": ANALYSIS_DIRECTORY / "provenance.json",
    "pollster_analysis_provenance_script":
        ANALYSIS_DIRECTORY / "provenance.json",
    "election_code_script": ANALYSIS_DIRECTORY / "provenance.json",
}
OUTPUT_PATTERN = re.compile(
    r"^(variability|he_weighting|biases)-(\d{4}[a-z]+)\.csv$"
)
CALIBRATION_CATEGORIES = (
    "poll_calibration_summaries",
    "bias_calibration_outputs",
)
OPTIONAL_CALIBRATION_CATEGORIES = {
    "poll_calibration_summaries",
}


def _record_key(election):
    return "pollster_parameters:{}".format(election)


def _scope_election(record):
    elections = record["scope"]["elections"]
    return elections[0] if len(elections) == 1 else None


def _source_dependencies():
    return {
        category: generated_provenance.source_manifest_dependency(
            category,
            manifest_path,
            ANALYSIS_DIRECTORY,
        )
        for category, manifest_path in SOURCE_DEPENDENCIES.items()
    }


def _calibration_record_keys(target_election, include_election):
    manifest = generated_provenance.load_manifest(
        CALIBRATION_MANIFEST_PATH
    )
    selected = {category: [] for category in CALIBRATION_CATEGORIES}
    for record_key, record in manifest["records"].items():
        category = record["category"]
        if category not in selected:
            continue
        election = _scope_election(record)
        if election and include_election(election, target_election):
            selected[category].append(record_key)
    return selected


class PollsterAnalysisRecorder:
    """Prepare dependencies and certify completed election work units."""

    def __init__(self, command):
        self.source_dependencies = _source_dependencies()
        self.run_id, self.run = generated_provenance.generation_run(
            command=command,
            source_revision=generated_provenance.current_source_revision(
                ANALYSIS_DIRECTORY
            ),
            environment=generated_provenance.current_environment(
                ("numpy", "pandas", "statsmodels")
            ),
        )

    def dependencies_for(self, target_election, include_election):
        dependencies = dict(self.source_dependencies)
        selected = _calibration_record_keys(
            target_election, include_election
        )
        for category, record_keys in selected.items():
            if not record_keys:
                if category in OPTIONAL_CALIBRATION_CATEGORIES:
                    continue
                raise generated_provenance.GeneratedProvenanceError(
                    "no {} records apply to {}".format(
                        category, target_election
                    )
                )
            dependencies[category] = (
                generated_provenance.generated_manifest_dependency(
                    category,
                    CALIBRATION_MANIFEST_PATH,
                    record_keys,
                    ANALYSIS_DIRECTORY,
                    allow_stale=True,
                )
            )
        return dependencies

    def record(self, election, outputs, dependencies):
        record = generated_provenance.generation_record(
            category="pollster_parameters",
            stage="analyse_pollsters",
            scope=generated_provenance.generation_scope(
                elections=[election]
            ),
            run=self.run_id,
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                outputs, ANALYSIS_DIRECTORY
            ),
            random_seed=None,
        )
        generated_provenance.update_manifest(
            MANIFEST_PATH,
            {_record_key(election): record},
            {self.run_id: self.run},
            path_base="../..",
            description=MANIFEST_DESCRIPTION,
        )


def _legacy_records():
    grouped = {}
    for path in sorted(CALIBRATION_DIRECTORY.glob("*.csv")):
        match = OUTPUT_PATTERN.fullmatch(path.name)
        if not match:
            continue
        grouped.setdefault(match.group(2), []).append(path)

    records = {}
    for election, outputs in grouped.items():
        records[_record_key(election)] = (
            generated_provenance.generation_record(
                category="pollster_parameters",
                stage="analyse_pollsters",
                scope=generated_provenance.generation_scope(
                    elections=[election]
                ),
                run="legacy-pollster-analysis-baseline",
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
        {"legacy-pollster-analysis-baseline": run},
        path_base="../..",
        description=MANIFEST_DESCRIPTION,
    )
    legacy_count = sum(
        record["status"] == "legacy"
        for record in manifest["records"].values()
    )
    print(
        "Recorded {} legacy pollster-analysis work units in {}".format(
            legacy_count, MANIFEST_PATH
        )
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Maintain provenance for pollster-analysis outputs."
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
