"""Track election-specific regional swing model outputs."""

import argparse
import csv
import hashlib
import sys
from datetime import date
from pathlib import Path

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
REGIONAL_DIRECTORY = ANALYSIS_DIRECTORY / "Regional"
MANIFEST_PATH = REGIONAL_DIRECTORY / "generated-provenance.json"
MANIFEST_DESCRIPTION = (
    "Bundled provenance for election-specific regional swing deviations."
)
SOURCE_DEPENDENCIES = {
    "election_catalogue":
        ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "regional_poll_inputs":
        REGIONAL_DIRECTORY / "provenance.json",
    "region_model_script": ANALYSIS_DIRECTORY / "provenance.json",
    "region_model_provenance_script":
        ANALYSIS_DIRECTORY / "provenance.json",
    "poll_transform_script": ANALYSIS_DIRECTORY / "provenance.json",
    "stan_cache_script": ANALYSIS_DIRECTORY / "provenance.json",
    "election_code_script": ANALYSIS_DIRECTORY / "provenance.json",
    "regional_stan_models":
        ANALYSIS_DIRECTORY / "Models" / "provenance.json",
}


class RegionalProvenanceError(
    generated_provenance.GeneratedProvenanceError
):
    pass


def canonical_party(party):
    party = (party or "").strip().casefold()
    if party in ("", "@tpp", "tpp"):
        return "@TPP"
    if party in ("on", "onp", "onp fp"):
        return "ONP FP"
    raise RegionalProvenanceError(
        "unsupported regional-model party '{}'".format(party)
    )


def party_suffix(party):
    return "" if canonical_party(party) == "@TPP" else "-on"


def record_key(election, party):
    return "regional_swing_deviations:{}:{}".format(
        election, canonical_party(party)
    )


def _case_insensitive_path(filename):
    matches = [
        path
        for path in REGIONAL_DIRECTORY.iterdir()
        if path.is_file() and path.name.casefold() == filename.casefold()
    ]
    if len(matches) > 1:
        raise RegionalProvenanceError(
            "multiple regional files differ only by case: {}".format(
                ", ".join(path.name for path in sorted(matches))
            )
        )
    return matches[0] if matches else None


def input_path(election, party):
    filename = "{}-polls{}.csv".format(
        election, party_suffix(party)
    )
    return _case_insensitive_path(filename)


def output_path(election, party):
    filename = "{}-swing-deviations{}.csv".format(
        election, party_suffix(party)
    )
    return (
        _case_insensitive_path(filename)
        or REGIONAL_DIRECTORY / filename
    )


def has_actual_poll_data(path):
    """Return whether a regional input contains a non-baseline poll row."""

    with Path(path).open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or "Firm" not in reader.fieldnames:
            raise RegionalProvenanceError(
                "{} lacks a Firm column".format(path)
            )
        has_actual_poll = False
        for line_number, row in enumerate(reader, start=2):
            firm = (row.get("Firm") or "").strip()
            if not firm or firm.casefold() == "election":
                continue
            has_actual_poll = True
            for field in ("StartDate", "EndDate"):
                try:
                    date.fromisoformat((row.get(field) or "").strip())
                except ValueError as error:
                    raise RegionalProvenanceError(
                        "{}:{} has an invalid {}".format(
                            path, line_number, field
                        )
                    ) from error
        return has_actual_poll


def required_work_units(target_elections=None):
    """Return work units backed by at least one real regional poll."""

    target_elections = (
        set(target_elections) if target_elections else None
    )
    work_units = {}
    for path in sorted(REGIONAL_DIRECTORY.glob("*-polls*.csv")):
        name = path.name
        lower_name = name.casefold()
        marker = "-polls"
        marker_position = lower_name.find(marker)
        if marker_position <= 0 or not lower_name.endswith(".csv"):
            continue
        election = lower_name[:marker_position]
        suffix = lower_name[
            marker_position + len(marker):-len(".csv")
        ]
        party = canonical_party(suffix[1:] if suffix.startswith("-") else "")
        if target_elections and election not in target_elections:
            continue
        if not has_actual_poll_data(path):
            continue
        key = record_key(election, party)
        if key in work_units:
            raise RegionalProvenanceError(
                "multiple inputs define regional work unit {}".format(key)
            )
        work_units[key] = {
            "election": election,
            "party": party,
            "input": path,
            "output": output_path(election, party),
        }
    return work_units


def derive_stan_seed(base_seed, election, party):
    """Derive a stable valid Stan seed for one regional work unit."""

    payload = "{}\0{}\0{}".format(
        int(base_seed), election, canonical_party(party)
    ).encode("utf-8")
    digest = hashlib.sha256(payload).digest()
    return int.from_bytes(digest[:8], "big") % (2 ** 31 - 1) + 1


def _source_dependencies():
    return {
        category: generated_provenance.source_manifest_dependency(
            category,
            manifest_path,
            ANALYSIS_DIRECTORY,
        )
        for category, manifest_path in SOURCE_DEPENDENCIES.items()
    }


class RegionalModelRecorder:
    """Certify completed regional model work units."""

    def __init__(self, command):
        self.source_dependencies = _source_dependencies()
        self.run_id, self.run = generated_provenance.generation_run(
            command=command,
            source_revision=generated_provenance.current_source_revision(
                ANALYSIS_DIRECTORY
            ),
            environment=generated_provenance.current_environment(
                ("numpy", "pandas", "pystan")
            ),
        )

    def record(self, election, party, output, random_seed):
        party = canonical_party(party)
        record = generated_provenance.generation_record(
            category="regional_swing_deviations",
            stage="generate_regional_swings",
            scope=generated_provenance.generation_scope(
                elections=[election],
                parties=[party],
                qualifiers={"regional_party": party},
            ),
            run=self.run_id,
            dependencies=self.source_dependencies,
            outputs=generated_provenance.output_fingerprints(
                [output], ANALYSIS_DIRECTORY
            ),
            random_seed=random_seed,
        )
        generated_provenance.update_manifest(
            MANIFEST_PATH,
            {record_key(election, party): record},
            {self.run_id: self.run},
            path_base="..",
            description=MANIFEST_DESCRIPTION,
        )


def baseline_existing_outputs():
    dependencies = _source_dependencies()
    records = {}
    for key, work_unit in required_work_units().items():
        output = work_unit["output"]
        if not output.is_file():
            continue
        records[key] = generated_provenance.generation_record(
            category="regional_swing_deviations",
            stage="generate_regional_swings",
            scope=generated_provenance.generation_scope(
                elections=[work_unit["election"]],
                parties=[work_unit["party"]],
                qualifiers={"regional_party": work_unit["party"]},
            ),
            run="legacy-regional-model-baseline",
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                [output], ANALYSIS_DIRECTORY
            ),
            random_seed=None,
            status="legacy",
        )
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
        {"legacy-regional-model-baseline": run},
        path_base="..",
        description=MANIFEST_DESCRIPTION,
    )
    print(
        "Recorded {} legacy regional-model work units in {}".format(
            sum(
                record["status"] == "legacy"
                for record in manifest["records"].values()
            ),
            MANIFEST_PATH,
        )
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Maintain provenance for regional model outputs."
    )
    parser.add_argument("command", choices=("baseline",))
    args = parser.parse_args(argv)
    if args.command == "baseline":
        baseline_existing_outputs()
        return 0
    raise AssertionError("unhandled command")


if __name__ == "__main__":
    sys.exit(main())
