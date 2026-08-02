"""Generated provenance for compact calibration summaries.

Parent: calibration_summary.py writes one compact CSV per election, then uses
this module to record its ancestry.  The compact CSV can be derived from older
calibration work, so its dependency is deliberately allowed to be stale: an
audit must continue to report legacy or stale calibration parents instead of
certifying the smaller derived file as independently current.
"""

import argparse
import sys
from pathlib import Path

import generated_provenance
import calibration_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
CALIBRATION_DIRECTORY = ANALYSIS_DIRECTORY / "Outputs" / "Calibration"
MANIFEST_PATH = CALIBRATION_DIRECTORY / "generated-provenance.json"
MANIFEST_DESCRIPTION = (
    "Bundled provenance for legacy calibration compatibility inputs and "
    "compact election-level calibration summaries."
)
SOURCE_DEPENDENCIES = {
    "calibration_summary_script": ANALYSIS_DIRECTORY / "provenance.json",
}

# Existing manifests may still use the pre-compaction categories. They remain
# valid parents until all calibration data has been regenerated under the new
# compatibility category.
LEGACY_CALIBRATION_SUMMARY_CATEGORY = "poll_calibration_summaries"
COMPACT_OUTPUT_PREFIX = "Outputs/Calibration/Summaries/"


def _record_key(election):
    # Historical ``calib_*`` records used the shorter key. Keep that parent
    # intact: the compact file must reference it rather than replace it.
    return "poll_calibration_summaries:{}:compact".format(election)


def _scope_election(record):
    elections = record["scope"]["elections"]
    return elections[0] if len(elections) == 1 else None


def _source_dependencies():
    return {
        category: generated_provenance.source_manifest_dependency(
            category, manifest_path, ANALYSIS_DIRECTORY
        )
        for category, manifest_path in SOURCE_DEPENDENCIES.items()
    }


def _is_legacy_loo_summary(record):
    """Whether an older poll_calibration_summaries record owns ``calib_*``."""

    if record["category"] != LEGACY_CALIBRATION_SUMMARY_CATEGORY:
        return False
    return any(
        Path(output).name.startswith("calib_")
        for output in record["outputs"]
    )


def compatibility_record_keys(election, category, manifest=None):
    """Return the exact detailed/legacy records needed to compact election."""

    if manifest is None:
        manifest = generated_provenance.load_manifest(MANIFEST_PATH)
    current_keys = [
        record_key
        for record_key, record in manifest["records"].items()
        if (
            _scope_election(record) == election
            and record["category"] == category
        )
    ]
    # A current calibration stage owns a complete category for its election.
    # Its files supersede same-named legacy leftovers which fp_model.py does
    # not delete when a pollster disappears from the configuration.
    if current_keys:
        return sorted(current_keys)

    keys = []
    for record_key, record in manifest["records"].items():
        if _scope_election(record) != election:
            continue
        record_category = record["category"]
        is_poll_compatibility = (
            record_category in {
                "poll_calibration_compatibility_inputs",
                "poll_calibration_traces",
            }
            or _is_legacy_loo_summary(record)
        )
        is_bias_compatibility = record_category in {
            "bias_calibration_compatibility_inputs",
            "bias_calibration_outputs",
        }
        if (
            category == "poll_calibration_compatibility_inputs"
            and is_poll_compatibility
        ) or (
            category == "bias_calibration_compatibility_inputs"
            and is_bias_compatibility
        ):
            keys.append(record_key)
    return sorted(keys)


def compatibility_input_paths(election, manifest=None):
    """Return active detailed files, preferring regenerated categories."""

    if manifest is None:
        manifest = generated_provenance.load_manifest(MANIFEST_PATH)
    paths = set()
    for category in (
        "poll_calibration_compatibility_inputs",
        "bias_calibration_compatibility_inputs",
    ):
        for record_key in compatibility_record_keys(election, category, manifest):
            paths.update(manifest["records"][record_key]["outputs"])
    return {
        (ANALYSIS_DIRECTORY / path).resolve()
        for path in paths
    }


def compatibility_record_keys_for_paths(
    election, category, input_paths, manifest=None
):
    """Return only records whose outputs were read for one summary."""

    if manifest is None:
        manifest = generated_provenance.load_manifest(MANIFEST_PATH)
    selected_paths = {Path(path).resolve() for path in input_paths}
    selected = []
    for record_key in compatibility_record_keys(election, category, manifest):
        outputs = {
            (ANALYSIS_DIRECTORY / output).resolve()
            for output in manifest["records"][record_key]["outputs"]
        }
        if outputs & selected_paths:
            selected.append(record_key)
    return selected


class CalibrationSummaryRecorder:
    """Record completed summaries while retaining all parent staleness."""

    def __init__(self, command):
        self.dependencies = _source_dependencies()
        self.pending_records = {}
        self.run_id, self.run = generated_provenance.generation_run(
            command=command,
            source_revision=generated_provenance.current_source_revision(
                ANALYSIS_DIRECTORY
            ),
            environment=generated_provenance.current_environment(),
        )

    def record(self, election, output, input_paths=None, manifest=None):
        """Queue one compact summary after its CSV was fully promoted."""

        manifest = manifest or generated_provenance.load_manifest(MANIFEST_PATH)
        if input_paths is None:
            input_paths = compatibility_input_paths(election, manifest)
        parent_keys = {
            category: compatibility_record_keys_for_paths(
                election, category, input_paths, manifest
            )
            for category in (
                "poll_calibration_compatibility_inputs",
                "bias_calibration_compatibility_inputs",
            )
        }
        if not any(parent_keys.values()):
            raise generated_provenance.GeneratedProvenanceError(
                "no calibration compatibility records apply to {}".format(
                    election
                )
            )
        dependencies = dict(self.dependencies)
        for category, record_keys in parent_keys.items():
            if not record_keys:
                continue
            dependencies[category] = (
                generated_provenance.generated_manifest_dependency(
                    category,
                    MANIFEST_PATH,
                    record_keys,
                    ANALYSIS_DIRECTORY,
                    # Compacting a legacy or stale parent is useful, but must
                    # not make the new file appear fully reproduced/current.
                    allow_stale=True,
                )
            )
        self.pending_records[_record_key(election)] = (
            generated_provenance.generation_record(
                category="poll_calibration_summaries",
                stage="compact_calibration_summaries",
                scope=generated_provenance.generation_scope(
                    elections=[election]
                ),
                run=self.run_id,
                dependencies=dependencies,
                outputs=generated_provenance.output_fingerprints(
                    [output], ANALYSIS_DIRECTORY
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


def record_summaries(elections, command, input_paths_for_election=None):
    """Record already-promoted compact CSVs from one successful CLI run."""

    recorder = CalibrationSummaryRecorder(command)
    manifest = generated_provenance.load_manifest(MANIFEST_PATH)
    for election in elections:
        output = CALIBRATION_DIRECTORY / "Summaries" / "{}.csv".format(
            election
        )
        if not output.is_file():
            raise generated_provenance.GeneratedProvenanceError(
                "{} was not promoted before provenance recording".format(
                    output
                )
            )
        input_paths = (
            input_paths_for_election(election)
            if input_paths_for_election is not None
            else compatibility_input_paths(election, manifest)
        )
        recorder.record(election, output, input_paths, manifest)
    recorder.flush()


def record_direct_summary(election, output, command):
    """Record a summary promoted from current in-memory calibration results.

    The two short staging files are deleted after promotion, so the final
    record depends directly on the calibration sources rather than treating
    those transient files as durable generated parents.
    """

    dependencies = calibration_provenance._source_dependencies()
    dependencies.update(_source_dependencies())
    run_id, run = generated_provenance.generation_run(
        command=command,
        source_revision=generated_provenance.current_source_revision(
            ANALYSIS_DIRECTORY
        ),
        environment=generated_provenance.current_environment(
            ("numpy", "pandas", "pystan")
        ),
    )
    record = generated_provenance.generation_record(
        category="poll_calibration_summaries",
        stage="compact_calibration_summaries",
        scope=generated_provenance.generation_scope(elections=[election]),
        run=run_id,
        dependencies=dependencies,
        outputs=generated_provenance.output_fingerprints(
            [output], ANALYSIS_DIRECTORY
        ),
        random_seed=None,
    )
    generated_provenance.update_manifest(
        MANIFEST_PATH,
        {_record_key(election): record},
        {run_id: run},
        path_base="../..",
        description=MANIFEST_DESCRIPTION,
    )


def _legacy_records():
    """Fingerprint pre-recorder compact files without certifying ancestry."""

    records = {}
    for path in sorted((CALIBRATION_DIRECTORY / "Summaries").glob("*.csv")):
        election = path.stem
        if not election or not election[0:4].isdigit():
            continue
        records[_record_key(election)] = generated_provenance.generation_record(
            category="poll_calibration_summaries",
            stage="compact_calibration_summaries",
            scope=generated_provenance.generation_scope(elections=[election]),
            run="legacy-calibration-summary-baseline",
            dependencies={},
            outputs=generated_provenance.output_fingerprints(
                [path], ANALYSIS_DIRECTORY
            ),
            random_seed=None,
            status="legacy",
        )
    return records


def baseline_existing_summaries():
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
    generated_provenance.update_manifest(
        MANIFEST_PATH,
        records,
        {"legacy-calibration-summary-baseline": run},
        path_base="../..",
        description=MANIFEST_DESCRIPTION,
    )
    print("Recorded {} legacy compact calibration summaries.".format(len(records)))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Maintain provenance for compact calibration summaries."
    )
    parser.add_argument("command", choices=("baseline",))
    args = parser.parse_args(argv)
    if args.command == "baseline":
        baseline_existing_summaries()
        return 0
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())
