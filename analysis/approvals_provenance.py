"""Record provenance for synthetic TPP observations derived from approvals."""

import argparse
import csv
import math
import sys
from datetime import date
from pathlib import Path

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
DATA_DIRECTORY = ANALYSIS_DIRECTORY / "Data"
OUTPUT_DIRECTORY = ANALYSIS_DIRECTORY / "Outputs"
SYNTHETIC_DIRECTORY = ANALYSIS_DIRECTORY / "Synthetic TPPs"
MANIFEST_PATH = SYNTHETIC_DIRECTORY / "generated-provenance.json"
PURE_MANIFEST_PATH = OUTPUT_DIRECTORY / "pure-generated-provenance.json"
MANIFEST_DESCRIPTION = (
    "Bundled provenance for jurisdiction-level synthetic TPP observations."
)
POLL_REGIONS = ("fed", "nsw", "vic", "qld", "wa", "sa")
SOURCE_DEPENDENCIES = {
    "election_catalogue": DATA_DIRECTORY / "provenance.json",
    "raw_poll_data": DATA_DIRECTORY / "provenance.json",
    "approval_context": DATA_DIRECTORY / "provenance.json",
    "approvals_script": ANALYSIS_DIRECTORY / "provenance.json",
    "approvals_provenance_script": ANALYSIS_DIRECTORY / "provenance.json",
    "election_code_script": ANALYSIS_DIRECTORY / "provenance.json",
}


def _configured_elections():
    elections = set()
    for filename in ("polled-elections.csv", "future-elections.csv"):
        path = DATA_DIRECTORY / filename
        with path.open(newline="", encoding="utf-8-sig") as source:
            for line_number, row in enumerate(csv.reader(source), start=1):
                if not row:
                    continue
                if len(row) < 2:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{}:{} must contain year and region".format(
                            path, line_number
                        )
                    )
                elections.add("{}{}".format(row[0], row[1]))
    return elections


def _election_cycles():
    path = DATA_DIRECTORY / "election-cycles.csv"
    cycles = {}
    with path.open(newline="", encoding="utf-8-sig") as source:
        for line_number, row in enumerate(csv.reader(source), start=1):
            if not row:
                continue
            if len(row) != 4:
                raise generated_provenance.GeneratedProvenanceError(
                    "{}:{} must contain four columns".format(
                        path, line_number
                    )
                )
            election = "{}{}".format(row[0], row[1])
            if election in cycles:
                raise generated_provenance.GeneratedProvenanceError(
                    "{}:{} duplicates election {}".format(
                        path, line_number, election
                    )
                )
            try:
                cycles[election] = (
                    date.fromisoformat(row[2]),
                    date.fromisoformat(row[3]),
                )
            except ValueError as error:
                raise generated_provenance.GeneratedProvenanceError(
                    "{}:{} contains an invalid ISO date".format(
                        path, line_number
                    )
                ) from error
    return cycles


def _valid_number(value):
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def approval_elections():
    """Return configured election terms containing valid approval polls."""

    configured = _configured_elections()
    cycles = _election_cycles()
    elections_by_region = {}
    for election in configured:
        if election not in cycles:
            # Future placeholders without a modelled cycle or pure trend are
            # dormant and cannot contribute approval observations yet.
            continue
        region = election[4:]
        elections_by_region.setdefault(region, []).append(election)

    selected = set()
    for region in POLL_REGIONS:
        path = DATA_DIRECTORY / "poll-data-{}.csv".format(region)
        with path.open(newline="", encoding="utf-8-sig") as source:
            reader = csv.DictReader(source)
            required = {"MidDate", "GLApp", "GLDis"}
            if reader.fieldnames is None or not required <= set(
                reader.fieldnames
            ):
                raise generated_provenance.GeneratedProvenanceError(
                    "{} lacks approval-poll columns".format(path)
                )
            for row in reader:
                if not (
                    _valid_number(row["GLApp"])
                    and _valid_number(row["GLDis"])
                ):
                    continue
                try:
                    poll_date = date.fromisoformat(row["MidDate"])
                except ValueError as error:
                    raise generated_provenance.GeneratedProvenanceError(
                        "{} contains invalid poll date '{}'".format(
                            path, row["MidDate"]
                        )
                    ) from error
                for election in elections_by_region.get(region, []):
                    start, end = cycles[election]
                    if start <= poll_date <= end:
                        selected.add(election)
    return selected


def available_pure_tpp_records(elections):
    records = []
    for election in sorted(elections):
        trend = OUTPUT_DIRECTORY / (
            "fp_trend_{}_@TPP_pure.csv".format(election)
        )
        polls = OUTPUT_DIRECTORY / (
            "fp_polls_{}_@TPP_pure.csv".format(election)
        )
        if trend.is_file() and polls.is_file():
            records.append(
                "pure_poll_outputs:{}:@TPP".format(election)
            )
    return records


def _source_dependencies():
    return {
        category: generated_provenance.source_manifest_dependency(
            category,
            manifest,
            ANALYSIS_DIRECTORY,
        )
        for category, manifest in SOURCE_DEPENDENCIES.items()
    }


class SyntheticTppRecorder:
    """Preflight dependencies and certify a complete jurisdiction output."""

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

    def dependencies_for(self, elections):
        dependencies = dict(self.source_dependencies)
        records = available_pure_tpp_records(elections)
        if records:
            dependencies["pure_poll_outputs"] = (
                generated_provenance.generated_manifest_dependency(
                    "pure_poll_outputs",
                    PURE_MANIFEST_PATH,
                    records,
                    ANALYSIS_DIRECTORY,
                    allow_stale=True,
                )
            )
        return dependencies

    def record(self, output_files, output_elections, dependencies):
        records = {}
        for region, output in sorted(output_files.items()):
            elections = sorted(output_elections.get(region, set()))
            if not elections:
                continue
            records[
                "synthetic_tpp_outputs:{}".format(region)
            ] = generated_provenance.generation_record(
                category="synthetic_tpp_outputs",
                stage="generate_synthetic_tpp",
                scope=generated_provenance.generation_scope(
                    elections=elections,
                    qualifiers={"jurisdiction": region},
                ),
                run=self.run_id,
                dependencies=dependencies,
                outputs=generated_provenance.output_fingerprints(
                    [output], ANALYSIS_DIRECTORY
                ),
                random_seed=None,
            )
        if records:
            generated_provenance.update_manifest(
                MANIFEST_PATH,
                records,
                {self.run_id: self.run},
                path_base="..",
                description=MANIFEST_DESCRIPTION,
            )


def _legacy_records():
    approval_terms = approval_elections()
    pure_records = available_pure_tpp_records(approval_terms)
    dependencies = _source_dependencies()
    if pure_records:
        dependencies["pure_poll_outputs"] = (
            generated_provenance.generated_manifest_dependency(
                "pure_poll_outputs",
                PURE_MANIFEST_PATH,
                pure_records,
                ANALYSIS_DIRECTORY,
                allow_stale=True,
            )
        )

    records = {}
    for output in sorted(SYNTHETIC_DIRECTORY.glob("*.csv")):
        region = output.stem
        elections = sorted(
            election
            for election in approval_terms
            if election[4:] == region
        )
        if not elections:
            continue
        records[
            "synthetic_tpp_outputs:{}".format(region)
        ] = generated_provenance.generation_record(
            category="synthetic_tpp_outputs",
            stage="generate_synthetic_tpp",
            scope=generated_provenance.generation_scope(
                elections=elections,
                qualifiers={"jurisdiction": region},
            ),
            run="legacy-synthetic-tpp-baseline",
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                [output], ANALYSIS_DIRECTORY
            ),
            random_seed=None,
            status="legacy",
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
        {"legacy-synthetic-tpp-baseline": run},
        path_base="..",
        description=MANIFEST_DESCRIPTION,
    )
    print(
        "Recorded {} legacy synthetic-TPP work units in {}".format(
            sum(
                record["status"] == "legacy"
                for record in manifest["records"].values()
            ),
            MANIFEST_PATH,
        )
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Maintain provenance for synthetic TPP outputs."
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
