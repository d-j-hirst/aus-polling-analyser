"""Define required federal regional-statistics work units and outputs."""

from pathlib import Path

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
MANIFEST_PATH = (
    ANALYSIS_DIRECTORY / "Seat Statistics" / "generated-provenance.json"
)
SUPPORTED_ELECTIONS = ("2022fed", "2025fed", "2028fed")
OUTPUT_SUFFIXES = (
    "-regions-base.csv",
    "-regions-polled.csv",
    "-mix-regions.csv",
    "-mix-parameters.csv",
)


def record_key(election):
    if election not in SUPPORTED_ELECTIONS:
        raise generated_provenance.GeneratedProvenanceError(
            "unsupported federal regional-statistics election '{}'".format(
                election
            )
        )
    return "federal_regional_statistics:{}".format(election)


def output_paths(election):
    record_key(election)
    return [
        ANALYSIS_DIRECTORY / "Regional" / "{}{}".format(election, suffix)
        for suffix in OUTPUT_SUFFIXES
    ]


def required_work_units(target_elections=None):
    targets = set(target_elections) if target_elections else None
    return {
        record_key(election): {
            "election": election,
            "outputs": output_paths(election),
        }
        for election in SUPPORTED_ELECTIONS
        if targets is None or election in targets
    }
