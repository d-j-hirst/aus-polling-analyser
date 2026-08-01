"""Track fundamentals and trend-adjustment outputs."""

import argparse
import csv
import re
import sys
from pathlib import Path

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DATA_DIRECTORY = ANALYSIS_DIRECTORY / "Data"
ADJUSTMENTS_DIRECTORY = ANALYSIS_DIRECTORY / "Adjustments"
FUNDAMENTALS_DIRECTORY = ANALYSIS_DIRECTORY / "Fundamentals"
MANIFEST_PATH = ADJUSTMENTS_DIRECTORY / "generated-provenance.json"
CUTOFF_MANIFEST_PATH = (
    ANALYSIS_DIRECTORY / "Outputs" / "cutoff-generated-provenance.json"
)
MANIFEST_DESCRIPTION = (
    "Bundled provenance for election fundamentals and trend adjustments."
)
SOURCE_DEPENDENCIES = {
    "election_catalogue": DATA_DIRECTORY / "provenance.json",
    "poll_model_configuration": DATA_DIRECTORY / "provenance.json",
    "preference_estimates": DATA_DIRECTORY / "provenance.json",
    "prior_result_inputs": DATA_DIRECTORY / "provenance.json",
    "eventual_result_inputs": DATA_DIRECTORY / "provenance.json",
    "party_group_definitions": DATA_DIRECTORY / "provenance.json",
    "trend_context": DATA_DIRECTORY / "provenance.json",
    "trend_adjust_script": ANALYSIS_DIRECTORY / "provenance.json",
    "trend_adjust_provenance_script":
        ANALYSIS_DIRECTORY / "provenance.json",
    "trend_adjust_cutoffs_script": ANALYSIS_DIRECTORY / "provenance.json",
    "poll_transform_script": ANALYSIS_DIRECTORY / "provenance.json",
    "sample_kurtosis_script": ANALYSIS_DIRECTORY / "provenance.json",
    "election_code_script": ANALYSIS_DIRECTORY / "provenance.json",
}
FUNDAMENTALS_SOURCE_CATEGORIES = {
    "election_catalogue",
    "poll_model_configuration",
    "preference_estimates",
    "prior_result_inputs",
    "eventual_result_inputs",
    "party_group_definitions",
    "trend_context",
    "trend_adjust_script",
    "trend_adjust_provenance_script",
    "election_code_script",
}
ADJUSTMENT_PATTERN = re.compile(
    r"adjust_(0none|\d{4}[a-z]+)_([A-Za-z0-9-]+)\.csv"
)
FUNDAMENTALS_PATTERN = re.compile(
    r"fundamentals_(0none|\d{4}[a-z]+)\.csv"
)


def _csv_rows(path):
    with Path(path).open(newline="", encoding="utf-8-sig") as source:
        return [row for row in csv.reader(source) if row]


def adjustment_groups(data_directory=None):
    data_directory = data_directory or DATA_DIRECTORY
    return [
        row[0]
        for row in _csv_rows(Path(data_directory) / "party-groups.csv")
    ]


def historical_elections(
    target_election,
    data_directory=None,
):
    """Return elections whose trends calibrate one adjustment target."""

    data_directory = data_directory or DATA_DIRECTORY
    target_election = str(target_election)
    polled_elections = [
        "{}{}".format(row[0], row[1])
        for row in _csv_rows(
            Path(data_directory) / "polled-elections.csv"
        )
    ]
    if target_election in polled_elections:
        polled_elections = polled_elections[
            :polled_elections.index(target_election)
        ]

    return polled_elections


def historical_cutoff_record_keys(
    target_election,
    data_directory=None,
):
    return [
        "cutoff_poll_outputs:{}".format(election)
        for election in historical_elections(
            target_election, data_directory
        )
    ]


def required_cutoff_work_units(target_elections=None):
    """Return cutoff records needed by the selected adjustment targets."""

    if not target_elections:
        return set(historical_cutoff_record_keys("0none"))
    return {
        record_key
        for target_election in target_elections
        for record_key in historical_cutoff_record_keys(target_election)
    }


def adjustment_output_path(target_election, party_group):
    return ADJUSTMENTS_DIRECTORY / "adjust_{}_{}.csv".format(
        target_election, party_group
    )


def fundamentals_output_path(target_election):
    return FUNDAMENTALS_DIRECTORY / "fundamentals_{}.csv".format(
        target_election
    )


def adjustment_record_key(target_election, party_group):
    return "trend_adjustments:{}:{}".format(
        target_election, party_group
    )


def fundamentals_record_key(target_election):
    return "fundamentals:{}".format(target_election)


def _source_dependencies():
    return {
        category: generated_provenance.source_manifest_dependency(
            category,
            manifest_path,
            ANALYSIS_DIRECTORY,
        )
        for category, manifest_path in SOURCE_DEPENDENCIES.items()
    }


def _cutoff_dependency(
    record_keys, allow_stale, check_context=None
):
    record_keys = sorted(set(record_keys))
    if not record_keys:
        return None
    return generated_provenance.generated_manifest_dependency(
        "cutoff_poll_outputs",
        CUTOFF_MANIFEST_PATH,
        record_keys,
        ANALYSIS_DIRECTORY,
        allow_stale=allow_stale,
        _context=check_context,
    )


class TrendAdjustmentRecorder:
    """Preflight dependencies and certify one completed target run."""

    def __init__(self, command):
        self.source_dependencies = _source_dependencies()
        self.run_id, self.run = generated_provenance.generation_run(
            command=command,
            source_revision=generated_provenance.current_source_revision(
                ANALYSIS_DIRECTORY
            ),
            environment=generated_provenance.current_environment(
                ("numpy", "scipy", "scikit-learn")
            ),
        )

    def dependencies_for(self, cutoff_record_keys):
        dependencies = dict(self.source_dependencies)
        cutoffs = _cutoff_dependency(
            cutoff_record_keys, allow_stale=True
        )
        if cutoffs is not None:
            dependencies["cutoff_poll_outputs"] = cutoffs
        return dependencies

    def record(
        self,
        target_election,
        adjustment_outputs,
        fundamentals_output,
        dependencies,
        expected_groups=None,
    ):
        target_election = str(target_election)
        adjustment_outputs = {
            party_group: Path(path)
            for party_group, path in adjustment_outputs.items()
        }
        if fundamentals_output is None:
            raise generated_provenance.GeneratedProvenanceError(
                "{} produced no fundamentals output".format(
                    target_election
                )
            )
        fundamentals_output = Path(fundamentals_output)
        expected_groups = set(
            adjustment_groups() if expected_groups is None else expected_groups
        )
        if set(adjustment_outputs) != expected_groups:
            raise generated_provenance.GeneratedProvenanceError(
                "{} adjustment outputs do not match configured groups; "
                "expected {}, received {}".format(
                    target_election,
                    ", ".join(sorted(expected_groups)),
                    ", ".join(sorted(adjustment_outputs)),
                )
            )
        missing_outputs = [
            str(path)
            for path in list(adjustment_outputs.values())
            + [fundamentals_output]
            if not path.is_file()
        ]
        if missing_outputs:
            raise generated_provenance.GeneratedProvenanceError(
                "cannot record incomplete trend-adjustment run; missing "
                "output(s): {}".format(", ".join(missing_outputs))
            )

        adjustment_dependencies = dict(dependencies)
        fundamentals_dependencies = {
            category: dependency
            for category, dependency in dependencies.items()
            if category in FUNDAMENTALS_SOURCE_CATEGORIES
        }
        records = {
            adjustment_record_key(target_election, party_group):
                generated_provenance.generation_record(
                    category="trend_adjustments",
                    stage="generate_trend_adjustments",
                    scope=generated_provenance.generation_scope(
                        elections=[target_election],
                        qualifiers={"party_group": party_group},
                    ),
                    run=self.run_id,
                    dependencies=adjustment_dependencies,
                    outputs=generated_provenance.output_fingerprints(
                        [output], ANALYSIS_DIRECTORY
                    ),
                    random_seed=None,
                )
            for party_group, output in adjustment_outputs.items()
        }
        records[fundamentals_record_key(target_election)] = (
            generated_provenance.generation_record(
                category="fundamentals",
                stage="generate_trend_adjustments",
                scope=generated_provenance.generation_scope(
                    elections=[target_election]
                ),
                run=self.run_id,
                dependencies=fundamentals_dependencies,
                outputs=generated_provenance.output_fingerprints(
                    [fundamentals_output], ANALYSIS_DIRECTORY
                ),
                random_seed=None,
            )
        )
        generated_provenance.update_manifest(
            MANIFEST_PATH,
            records,
            {self.run_id: self.run},
            path_base="..",
            description=MANIFEST_DESCRIPTION,
        )


def _legacy_records():
    source_dependencies = _source_dependencies()
    dependencies_by_target = {}
    check_context = generated_provenance.ManifestCheckContext()
    available_cutoff_keys = set()
    if CUTOFF_MANIFEST_PATH.is_file():
        available_cutoff_keys = set(
            check_context.load_manifest(
                CUTOFF_MANIFEST_PATH
            )["records"]
        )

    def dependencies(target_election, include_cutoffs):
        cache_key = (target_election, include_cutoffs)
        if cache_key not in dependencies_by_target:
            selected = dict(source_dependencies)
            if not include_cutoffs:
                selected = {
                    category: dependency
                    for category, dependency in selected.items()
                    if category in FUNDAMENTALS_SOURCE_CATEGORIES
                }
            if include_cutoffs:
                historical_cutoffs = [
                    key
                    for key in historical_cutoff_record_keys(
                        target_election
                    )
                    if key in available_cutoff_keys
                ]
                cutoffs = _cutoff_dependency(
                    historical_cutoffs,
                    allow_stale=True,
                    check_context=check_context,
                )
                if cutoffs is not None:
                    selected["cutoff_poll_outputs"] = cutoffs
            dependencies_by_target[cache_key] = selected
        return dependencies_by_target[cache_key]

    records = {}
    valid_groups = set(adjustment_groups())
    for output in sorted(ADJUSTMENTS_DIRECTORY.glob("adjust_*.csv")):
        match = ADJUSTMENT_PATTERN.fullmatch(output.name)
        if not match or match.group(2) not in valid_groups:
            continue
        target_election, party_group = match.groups()
        records[
            adjustment_record_key(target_election, party_group)
        ] = generated_provenance.generation_record(
            category="trend_adjustments",
            stage="generate_trend_adjustments",
            scope=generated_provenance.generation_scope(
                elections=[target_election],
                qualifiers={"party_group": party_group},
            ),
            run="legacy-trend-adjustment-baseline",
            dependencies=dependencies(target_election, True),
            outputs=generated_provenance.output_fingerprints(
                [output], ANALYSIS_DIRECTORY
            ),
            random_seed=None,
            status="legacy",
        )

    for output in sorted(
        FUNDAMENTALS_DIRECTORY.glob("fundamentals_*.csv")
    ):
        match = FUNDAMENTALS_PATTERN.fullmatch(output.name)
        if not match:
            continue
        target_election = match.group(1)
        records[fundamentals_record_key(target_election)] = (
            generated_provenance.generation_record(
                category="fundamentals",
                stage="generate_trend_adjustments",
                scope=generated_provenance.generation_scope(
                    elections=[target_election]
                ),
                run="legacy-trend-adjustment-baseline",
                dependencies=dependencies(target_election, False),
                outputs=generated_provenance.output_fingerprints(
                    [output], ANALYSIS_DIRECTORY
                ),
                random_seed=None,
                status="legacy",
            )
        )
    return records


def baseline_existing_outputs():
    records = _legacy_records()
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
        {"legacy-trend-adjustment-baseline": run},
        path_base="..",
        description=MANIFEST_DESCRIPTION,
    )
    print(
        "Recorded {} legacy trend-adjustment work units in {}".format(
            sum(
                record["status"] == "legacy"
                for record in manifest["records"].values()
            ),
            MANIFEST_PATH,
        )
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Maintain trend-adjustment output provenance."
    )
    parser.add_argument("command", choices=("baseline",))
    args = parser.parse_args(argv)
    try:
        if args.command == "baseline":
            baseline_existing_outputs()
            return 0
    except generated_provenance.GeneratedProvenanceError as error:
        print("Error: {}".format(error), file=sys.stderr)
        return 2
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())
