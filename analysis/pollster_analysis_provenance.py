"""Record provenance for compact pollster-parameter outputs.

One work unit contains the variability, house-effect weighting and bias files
for a target election. Historical calibration may legitimately remain stale
for long periods, so generation is permitted from stale calibration records;
that inherited calibration staleness remains visible in the resulting record.

Main functions:
* ``_calibration_record_keys`` and ``_calibration_input_paths`` select the
  calibration evidence actually consumed by one target election.
* ``refresh_calibration_dependencies`` performs a metadata-only repair when
  irrelevant historical party dependencies were previously recorded.
* ``PollsterAnalysisRecorder`` publishes the three-file pollster output unit.
* ``baseline_existing_outputs`` registers old parameter files as legacy.
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
    "poll_calibration_compatibility_inputs",
    "bias_calibration_compatibility_inputs",
)
COMPACT_SUMMARY_PREFIX = "Outputs/Calibration/Summaries/"
# This named migration is registered when legacy records need their overly
# broad calibration-party dependencies corrected without rerunning reducers.
CALIBRATION_DEPENDENCY_REFRESH_UPGRADE = (
    "refresh-pollster-calibration-dependencies-v1"
)


# Calibration-evidence selection and validation

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


def _canonical_calibration_party(party):
    """Match the reducer's pooling of state Liberal data with Coalition."""

    return "LNP FP" if party == "LIB FP" else party


def _calibration_record_keys(
    target_election, include_election, target_parties=None, manifest=None
):
    """Select only calibration records that can affect the target outputs."""

    if manifest is None:
        manifest = generated_provenance.load_manifest(
            CALIBRATION_MANIFEST_PATH
        )
    target_parties = {
        _canonical_calibration_party(party)
        for party in (target_parties or ())
    }
    selected = {category: [] for category in CALIBRATION_CATEGORIES}
    compact_elections = set()
    for record_key, record in manifest["records"].items():
        if record["category"] != "poll_calibration_summaries":
            continue
        election = _scope_election(record)
        if not election or not include_election(election, target_election):
            continue
        if not any(
            output.startswith(COMPACT_SUMMARY_PREFIX)
            for output in record["outputs"]
        ):
            continue
        if election in compact_elections:
            raise generated_provenance.GeneratedProvenanceError(
                "multiple compact calibration summaries apply to {}".format(
                    election
                )
            )
        compact_elections.add(election)
        selected["poll_calibration_summaries"].append(record_key)

    for record_key, record in manifest["records"].items():
        category = record["category"]
        is_legacy_loo_summary = (
            category == "poll_calibration_summaries"
            and (
                not record.get("outputs")
                or any(
                    Path(output).name.startswith("calib_")
                    for output in record["outputs"]
                )
            )
        )
        compatibility_category = None
        if category in {
            "poll_calibration_compatibility_inputs",
            "poll_calibration_traces",
        } or is_legacy_loo_summary:
            compatibility_category = "poll_calibration_compatibility_inputs"
        elif category in {
            "bias_calibration_compatibility_inputs",
            "bias_calibration_outputs",
        }:
            compatibility_category = "bias_calibration_compatibility_inputs"
        if compatibility_category is None:
            continue
        election = _scope_election(record)
        if not election or not include_election(election, target_election):
            continue
        # A compact unit contains every detailed calibration value the
        # reducers use. It supersedes legacy files for that election only;
        # other historical elections continue through the fallback path.
        if election in compact_elections:
            continue
        record_parties = {
            _canonical_calibration_party(party)
            for party in record["scope"].get("parties", ())
        }
        # Some legacy or aggregate records lack a party scope. Keep them
        # conservatively because their relevance cannot be inferred here.
        if target_parties and record_parties and not (
            target_parties & record_parties
        ):
            continue
        selected[compatibility_category].append(record_key)
    return selected


def _calibration_input_paths(manifest, selected):
    """Return the exact active evidence files represented by selected records."""

    return sorted({
        (ANALYSIS_DIRECTORY / output).resolve()
        for record_keys in selected.values()
        for record_key in record_keys
        for output in manifest["records"][record_key]["outputs"]
    }, key=str)


def _target_parties(election):
    """Read the target's configured parties without importing reducers."""

    significant_parties_path = (
        ANALYSIS_DIRECTORY / "Data" / "significant-parties.csv"
    )
    match = re.fullmatch(r"(\d{4})([a-z]+)", election)
    if not match:
        raise generated_provenance.GeneratedProvenanceError(
            "invalid pollster-parameter election '{}'".format(election)
        )
    with open(significant_parties_path, encoding="utf-8") as input_file:
        for line in input_file:
            fields = [field.strip() for field in line.split(",")]
            if fields[:2] == [match.group(1), match.group(2)]:
                return {
                    _canonical_calibration_party(party)
                    for party in fields[2:]
                }
    raise generated_provenance.GeneratedProvenanceError(
        "no significant-party configuration exists for {}".format(election)
    )


def refresh_calibration_dependencies(record, base_directory):
    """Remove obsolete party-specific calibration edges without rerunning.

    Earlier provenance baselines conservatively treated every historical party
    calibration as an input to every target.  The reducers never read a
    calibration party outside the target's significant-party set, so pruning
    those stale edges corrects metadata only; it does not alter the published
    pollster parameter files.
    """

    election = _scope_election(record)
    if record["category"] != "pollster_parameters" or not election:
        return False
    target_parties = _target_parties(election)
    changed = False
    for category in CALIBRATION_CATEGORIES:
        dependency = record["dependencies"].get(category)
        if dependency is None:
            continue
        manifest_path = (
            Path(base_directory) / dependency["manifest"]
        ).resolve()
        manifest = generated_provenance.load_manifest(manifest_path)
        retained_keys = []
        for record_key in dependency["records"]:
            calibration_record = manifest["records"].get(record_key)
            if calibration_record is None:
                retained_keys.append(record_key)
                continue
            parties = {
                _canonical_calibration_party(party)
                for party in calibration_record["scope"].get("parties", ())
            }
            # Keep unscoped aggregate records conservatively.
            if not parties or target_parties & parties:
                retained_keys.append(record_key)
        if retained_keys == dependency["records"]:
            continue
        record["dependencies"][category] = (
            generated_provenance.generated_manifest_dependency(
                category,
                manifest_path,
                retained_keys,
                base_directory,
                allow_stale=False,
                non_invalidating_records=dependency.get(
                    "non_invalidating_records", ()
                ),
            )
        )
        changed = True
    return changed


def obsolete_calibration_dependency_issues(record, base_directory):
    """Report party-specific calibration edges no longer used by a record.

    This is deliberately narrower than recomputing the complete historical
    selection. It identifies the dangerous legacy case: a record tracks a
    calibration party which the current reducer would ignore because that
    party is no longer significant for the target election.
    """

    election = _scope_election(record)
    if record["category"] != "pollster_parameters" or not election:
        return []
    target_parties = _target_parties(election)
    issues = []
    for category in CALIBRATION_CATEGORIES:
        dependency = record["dependencies"].get(category)
        if dependency is None:
            continue
        manifest_path = (
            Path(base_directory) / dependency["manifest"]
        ).resolve()
        manifest = generated_provenance.load_manifest(manifest_path)
        for record_key in dependency["records"]:
            calibration_record = manifest["records"].get(record_key)
            if calibration_record is None:
                continue
            parties = {
                _canonical_calibration_party(party)
                for party in calibration_record["scope"].get("parties", ())
            }
            if parties and not target_parties & parties:
                issues.append(
                    "obsolete calibration-party dependency {} ({})".format(
                        category, record_key
                    )
                )
    return issues


# Generated pollster-parameter provenance publication

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

    def _dependencies_for_selected(self, target_election, selected):
        dependencies = dict(self.source_dependencies)
        if not any(selected.values()):
            raise generated_provenance.GeneratedProvenanceError(
                "no calibration evidence records apply to {}".format(
                    target_election
                )
            )
        for category, record_keys in selected.items():
            if not record_keys:
                continue
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

    def inputs_for(self, target_election, include_election, target_parties=None):
        """Return matching provenance dependencies and their input files."""

        manifest = generated_provenance.load_manifest(
            CALIBRATION_MANIFEST_PATH
        )
        selected = _calibration_record_keys(
            target_election, include_election, target_parties, manifest
        )
        return (
            self._dependencies_for_selected(target_election, selected),
            _calibration_input_paths(manifest, selected),
        )

    def dependencies_for(
        self, target_election, include_election, target_parties=None
    ):
        """Compatibility wrapper for callers that only need provenance."""

        selected = _calibration_record_keys(
            target_election, include_election, target_parties
        )
        return self._dependencies_for_selected(
            target_election, selected
        )

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
