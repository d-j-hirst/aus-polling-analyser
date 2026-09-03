"""Track generated historical booth-result JSON used by live simulations.

The supported set intentionally excludes 2022vic.  That file does not yet
exist and the 2026 Victorian live setup is not ready, so it must not block the
generated-data archive until a reproducible generator is added.
"""

from pathlib import Path
import sys

import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
REPOSITORY_DIRECTORY = ANALYSIS_DIRECTORY.parent
BOOTH_RESULTS_DIRECTORY = ANALYSIS_DIRECTORY / "Booth Results"
MANIFEST_PATH = BOOTH_RESULTS_DIRECTORY / "generated-provenance.json"
MANIFEST_DESCRIPTION = (
    "Generated historical booth-result JSON used by automatic live analysis."
)
SUPPORTED_ELECTIONS = (
    "2015nsw",
    "2018vic",
    "2019nsw",
    "2020qld",
    "2022sa",
)
SCRIPT_CATEGORY_BY_JURISDICTION = {
    "nsw": "booth_result_nsw_script",
    "vic": "booth_result_vic_script",
    "qld": "booth_result_qld_script",
    "sa": "booth_result_sa_script",
}
SOURCE_MANIFEST_BY_CATEGORY = {
    "election_catalogue": ANALYSIS_DIRECTORY / "Data" / "provenance.json",
    "booth_result_dispatcher_script": ANALYSIS_DIRECTORY / "provenance.json",
    "booth_result_provenance_script": ANALYSIS_DIRECTORY / "provenance.json",
    **{
        category: ANALYSIS_DIRECTORY / "provenance.json"
        for category in SCRIPT_CATEGORY_BY_JURISDICTION.values()
    },
    "sa_booth_result_inputs": REPOSITORY_DIRECTORY / "downloads" / "provenance.json",
}


class BoothResultProvenanceError(
    generated_provenance.GeneratedProvenanceError
):
    """Raised when a booth-result work unit cannot be certified."""


def jurisdiction(election):
    election = str(election).strip().casefold()
    if election not in SUPPORTED_ELECTIONS:
        raise BoothResultProvenanceError(
            "unsupported booth-result election '{}'".format(election)
        )
    return election[4:]


def record_key(election):
    jurisdiction(election)
    return "booth_result_archives:{}".format(election)


def output_path(election):
    jurisdiction(election)
    return BOOTH_RESULTS_DIRECTORY / "{}.json".format(election)


def required_work_units(target_elections=None):
    """Return supported work units, optionally restricted by election code."""

    targets = set(target_elections) if target_elections else None
    return {
        record_key(election): {
            "election": election,
            "output": output_path(election),
        }
        for election in SUPPORTED_ELECTIONS
        if targets is None or election in targets
    }


def _source_dependency(category):
    return generated_provenance.source_manifest_dependency(
        category,
        SOURCE_MANIFEST_BY_CATEGORY[category],
        REPOSITORY_DIRECTORY,
    )


def dependencies_for(election):
    region = jurisdiction(election)
    categories = [
        "election_catalogue",
        "booth_result_dispatcher_script",
        "booth_result_provenance_script",
        SCRIPT_CATEGORY_BY_JURISDICTION[region],
    ]
    if region == "sa":
        categories.append("sa_booth_result_inputs")
    return {
        category: _source_dependency(category)
        for category in categories
    }


def record_generated_output(election, command=None):
    """Record one output only after its generator completed successfully."""

    output = output_path(election)
    if not output.is_file():
        raise BoothResultProvenanceError(
            "booth-result generator did not create {}".format(output)
        )
    run_id, run = generated_provenance.generation_run(
        command=command or [Path(sys.executable).name] + sys.argv,
        source_revision=generated_provenance.current_source_revision(
            REPOSITORY_DIRECTORY
        ),
        environment=generated_provenance.current_environment(),
    )
    record = generated_provenance.generation_record(
        category="booth_result_archives",
        stage="fetch_live_booth_results",
        scope=generated_provenance.generation_scope(elections=[election]),
        run=run_id,
        dependencies=dependencies_for(election),
        outputs=generated_provenance.output_fingerprints(
            [output], REPOSITORY_DIRECTORY
        ),
        random_seed=None,
    )
    generated_provenance.update_manifest(
        MANIFEST_PATH,
        {record_key(election): record},
        {run_id: run},
        path_base="../..",
        description=MANIFEST_DESCRIPTION,
    )
    return output
