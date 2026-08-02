"""Generate historical inputs used by the C++ seat simulation.

The script orchestrates independent historical analyses for seat-level party
and swing behaviour, federal regional polling behaviour, and the allocation
of Coalition votes between Liberal and Nationals candidates. The numerical
models live in focused modules; this entry point owns shared setup and the
single provenance record written after all analyses succeed.

Main functions:
* ``main`` loads normalized election data and performs the ordered analysis
  bundle through the imported party, regional and seat model functions.
* ``record_generated_provenance`` records the complete output set only after
  all component calculations have succeeded.
"""

from pathlib import Path
import sys

from election_analysis_common import (
    extend_region_errors_with_selected_factor,
    has_material_independent_vote,
    total_others_vote_share,
)
from election_analysis_parties import (
    analyse_centrist_minors,
    analyse_emerging_independents,
    analyse_emerging_parties,
    analyse_existing_independents,
    analyse_greens,
    analyse_others,
    analyse_populist_minors,
    load_by_elections,
    load_seat_regions,
    load_seat_types,
)
from election_analysis_regions import analyse_region_swings
from election_analysis_seats import (
    analyse_green_independent_correlation,
    analyse_nationals,
    analyse_seat_swings,
    get_all_elections,
)
from election_check import get_checked_elections
from election_store import ensure_election_exports
import generated_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
GENERATED_MANIFEST = (
    ANALYSIS_DIRECTORY
    / 'Seat Statistics'
    / 'generated-provenance.json'
)


# Output validation and provenance publication

def record_generated_provenance():
    dependencies = {}
    for category in (
        'election_analysis_script',
        'election_check_script',
        'election_data_script',
        'election_code_script',
        'poll_transform_script',
        'sample_kurtosis_script',
    ):
        dependencies[category] = (
            generated_provenance.source_manifest_dependency(
                category,
                ANALYSIS_DIRECTORY / 'provenance.json',
                ANALYSIS_DIRECTORY,
            )
        )
    for category in (
        'election_catalogue',
        'election_result_rules',
        'seat_analysis_inputs',
    ):
        dependencies[category] = (
            generated_provenance.source_manifest_dependency(
                category,
                ANALYSIS_DIRECTORY / 'Data' / 'provenance.json',
                ANALYSIS_DIRECTORY,
            )
        )

    election_manifest = (
        ANALYSIS_DIRECTORY
        / 'elections'
        / 'generated-provenance.json'
    )
    election_records = generated_provenance.load_manifest(
        election_manifest
    )['records']
    dependencies['election_result_cache'] = (
        generated_provenance.generated_manifest_dependency(
            'election_result_cache',
            election_manifest,
            sorted(election_records),
            ANALYSIS_DIRECTORY,
        )
    )

    source_revision = generated_provenance.current_source_revision(
        ANALYSIS_DIRECTORY
    )
    environment = generated_provenance.current_environment()
    command = [Path(sys.executable).name] + sys.argv
    run_id, run = generated_provenance.generation_run(
        command=command,
        source_revision=source_revision,
        environment=environment,
    )
    records = {}

    seat_statistics = sorted(
        (ANALYSIS_DIRECTORY / 'Seat Statistics').glob('*.csv')
    )
    records['seat_statistics:all'] = (
        generated_provenance.generation_record(
            category='seat_statistics',
            stage='analyse_elections',
            scope=generated_provenance.generation_scope(
                all_scopes=True
            ),
            run=run_id,
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                seat_statistics, ANALYSIS_DIRECTORY
            ),
            random_seed=None,
        )
    )

    nationals_by_election = {}
    for output_path in sorted(
        (ANALYSIS_DIRECTORY / 'Nationals').glob('*.csv')
    ):
        election = output_path.stem.split('_', 1)[0]
        nationals_by_election.setdefault(election, []).append(output_path)
    for election, outputs in nationals_by_election.items():
        records[f'nationals_allocations:{election}'] = (
            generated_provenance.generation_record(
                category='nationals_allocations',
                stage='analyse_elections',
                scope=generated_provenance.generation_scope(
                    elections=[election]
                ),
                run=run_id,
                dependencies=dependencies,
                outputs=generated_provenance.output_fingerprints(
                    outputs, ANALYSIS_DIRECTORY
                ),
                random_seed=None,
            )
        )

    regional_outputs = sorted(
        path
        for pattern in (
            '2028fed-regions-base.csv',
            '2028fed-regions-polled.csv',
            '2028fed-mix-regions.csv',
            '2028fed-mix-parameters.csv',
        )
        for path in (ANALYSIS_DIRECTORY / 'Regional').glob(pattern)
    )
    records['federal_regional_statistics:2028fed'] = (
        generated_provenance.generation_record(
            category='federal_regional_statistics',
            stage='analyse_elections',
            scope=generated_provenance.generation_scope(
                elections=['2028fed']
            ),
            run=run_id,
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                regional_outputs, ANALYSIS_DIRECTORY
            ),
            random_seed=None,
        )
    )

    generated_provenance.update_manifest(
        GENERATED_MANIFEST,
        records,
        {run_id: run},
        path_base='..',
        description=(
            'Bundled provenance for historical seat, Nationals and '
            'federal regional analysis.'
        ),
    )
    print(f'Recorded generated provenance in {GENERATED_MANIFEST}')



# Core historical analysis orchestration

def main():
    """Run every historical analysis before certifying the output bundle."""

    all_elections = get_all_elections()
    elections = get_checked_elections()
    # A deliberately deleted cache may have been downloaded again above.
    # Synchronise the checked CSV exports and their provenance before using
    # them as a certified dependency of this analysis.
    ensure_election_exports(elections)
    seat_types = load_seat_types()
    seat_regions = load_seat_regions()
    by_elections = load_by_elections()
    analyse_greens(elections)
    analyse_existing_independents(elections)
    analyse_emerging_independents(elections, seat_types)
    analyse_populist_minors(elections, seat_types, seat_regions)
    analyse_centrist_minors(elections, seat_types, seat_regions)
    analyse_others(elections)
    analyse_emerging_parties(elections)
    analyse_region_swings()
    analyse_seat_swings(elections, seat_types, seat_regions, by_elections)
    analyse_green_independent_correlation(elections)
    analyse_nationals(elections, all_elections)
    record_generated_provenance()
    print("Analysis completed.")


if __name__ == '__main__':
    main()
