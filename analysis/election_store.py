"""Export cached historical results in the format consumed by C++.

The pickle cache preserves Python objects for statistical analysis. This
script applies the shared party categories and writes the deliberately simple
line-based CSV consumed by ``SimulationPreparation::loadPastSeatResults``.
Candidate names are not quoted; the C++ reader parses stable result fields from
the end of each row so names containing commas remain supported.
"""

from election_check import get_checked_elections
import generated_provenance

from pathlib import Path
import sys


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
GENERATED_MANIFEST = (
    ANALYSIS_DIRECTORY / 'elections' / 'generated-provenance.json'
)


def write_candidate_to_file(file, c):
    file.write(f'{c.name},{c.party},{c.votes},{c.percent},{c.swing}\n')


def write_election_to_file(file, election_code, election_results):
    file.write(f'{election_results.name}\n')
    for seat in election_results.seat_results:
        file.write(f'Seat,{seat.name}\n')
        file.write(f'fp\n')
        for fp in seat.fp:
            write_candidate_to_file(file, fp)
        file.write(f'tcp\n')
        for tcp in seat.tcp:
            write_candidate_to_file(file, tcp)


def store_elections(elections):
    """Write every configured election and return its provenance output list."""
    output_directory = ANALYSIS_DIRECTORY / 'elections'
    output_directory.mkdir(parents=True, exist_ok=True)
    stored_elections = []
    for election_code, election_results in elections.items():
        filename = (
            output_directory
            / (
                f'results_{election_code.year()}'
                f'{election_code.region()}.csv'
            )
        )
        print(filename)
        with open(filename, 'w') as file:
            write_election_to_file(file, election_code, election_results)
        stored_elections.append((election_code, filename))
    return stored_elections


def record_generated_provenance(stored_elections):
    dependencies = {
        'election_result_rules':
            generated_provenance.source_manifest_dependency(
                'election_result_rules',
                ANALYSIS_DIRECTORY / 'Data' / 'provenance.json',
                ANALYSIS_DIRECTORY,
            )
    }
    for category in (
        'election_store_script',
        'election_check_script',
        'election_data_script',
        'election_code_script',
    ):
        dependencies[category] = (
            generated_provenance.source_manifest_dependency(
                category,
                ANALYSIS_DIRECTORY / 'provenance.json',
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

    for election_code, output_path in stored_elections:
        election = election_code.short()
        cache_path = (
            ANALYSIS_DIRECTORY
            / 'elections'
            / f'{election}_results.pkl'
        )
        records[f'election_result_exports:{election}'] = (
            generated_provenance.generation_record(
                category='election_result_exports',
                stage='export_election_results',
                scope=generated_provenance.generation_scope(
                    elections=[election]
                ),
                run=run_id,
                dependencies=dependencies,
                outputs=generated_provenance.output_fingerprints(
                    [cache_path, output_path], ANALYSIS_DIRECTORY
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
            'Bundled provenance for checked historical election-result '
            'exports.'
        ),
    )
    print(f'Recorded generated provenance in {GENERATED_MANIFEST}')


if __name__ == '__main__':
    try:
        elections = get_checked_elections(allow_download=False)
        stored_elections = store_elections(elections)
        record_generated_provenance(stored_elections)
    except (
        FileNotFoundError,
        generated_provenance.GeneratedProvenanceError,
    ) as error:
        print(f'Could not export election results: {error}', file=sys.stderr)
        sys.exit(2)
