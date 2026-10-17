"""Generate election-specific local TPP scales without Stan or forecast runs.

Run from analysis/: python -m scripts.pipeline.seat_volatility --election 2022fed.
Omitting --election covers configured seat files and future elections. Use
--output-directory for a review copy. The manifest records code and input hashes
alongside the actual past-only training years in each election's JSON audit.
"""

import argparse
import csv
import json
from pathlib import Path

from lib.election_analysis.seat_volatility import calculate, collect_observations
from lib.elections.election_check import get_checked_elections
from lib.paths import ANALYSIS_DIRECTORY
from lib.provenance import generated_provenance

OUTPUT = ANALYSIS_DIRECTORY / 'Seat Statistics/individual-seat-factors'


def configured_elections():
    elections = {p.stem for p in (ANALYSIS_DIRECTORY / 'seats').glob('*.txt')}
    with (ANALYSIS_DIRECTORY / 'Data/future-elections.csv').open(encoding='utf-8') as stream:
        elections.update(r[0]+r[1] for r in csv.reader(stream))
    return sorted(elections)


def write_parameters(result, directory):
    """Keep runtime input small; put sample counts and evidence in the sidecar."""
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / (result['election'] + '.csv')
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, lineterminator='\n')
        writer.writerow(['format', 1])
        writer.writerow(['election', result['election']])
        for key in ('generic_sd', 'fallback_sd', 'kurtosis'):
            writer.writerow([key, format(result[key], '.12g')])
        for seat in result['seats']:
            writer.writerow(['seat', seat['name'], format(seat['elasticity'], '.12g'), format(seat['sd'], '.12g')])
    path.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return [path, path.with_suffix('.json')]


def record_generation(outputs):
    """Certify only this component, never unrelated historical-analysis files."""
    base = ANALYSIS_DIRECTORY
    code = [base / p for p in ['scripts/pipeline/seat_volatility.py',
        'lib/election_analysis/seat_volatility.py', 'lib/elections/historical_tpp.py',
        'lib/elections/election_data.py', 'lib/elections/election_check.py']]
    inputs = [base / 'Data' / p for p in ['seat-regions.csv', 'federal-tpp-history.csv',
        'historical-tpp-corrections.json', 'party-simplification.csv']]
    # The checked-result cache is what calculation actually reads. Record those
    # exact bytes, rather than assuming an older CSV export still matches it.
    from lib.elections.election_data import ELECTION_CACHE_DIRECTORY
    inputs += sorted(ELECTION_CACHE_DIRECTORY.glob('*_results.pkl'))
    dependencies = {
        'seat_volatility_code': generated_provenance.file_dependency('seat_volatility_code', code, base),
        'seat_volatility_history': generated_provenance.file_dependency('seat_volatility_history', inputs, base)}
    run_id, run = generated_provenance.generation_run(generated_provenance.current_command(),
        generated_provenance.current_source_revision(base), generated_provenance.current_environment())
    records = {f'seat_volatility:{election}': generated_provenance.generation_record(
        category='seat_volatility', stage='analyse_elections',
        scope=generated_provenance.generation_scope(elections=[election]), run=run_id,
        dependencies=dependencies, outputs=generated_provenance.output_fingerprints(paths, base))
        for election, paths in outputs.items()}
    generated_provenance.update_manifest(OUTPUT / 'generated-provenance.json', records,
        {run_id: run}, path_base='../..', description=
        'Election-specific seat TPP scales calculated exclusively from earlier elections.')


def publish_parameters(results, excluded, directory):
    """Write runtime inputs together with the comparisons omitted from each term."""
    outputs = {}
    for result in results:
        result['excluded_comparisons'] = [r for r in excluded if r['year'] < int(result['election'][:4])
            and r['jurisdiction'] == result['election'][4:]]
        outputs[result['election']] = write_parameters(result, directory)
        print(f"{result['election']}: generic SD {result['generic_sd']:.3f}, "
              f"fallback {result['fallback_sd']:.3f}, {len(result['seats'])} seat estimates")
    return outputs


def generate(elections, directory=OUTPUT, checked_elections=None):
    """Load once, calculate all targets, publish files, then record provenance."""
    observations, excluded = collect_observations(checked_elections if checked_elections is not None else
        get_checked_elections(allow_download=False))
    results = [calculate(observations, election) for election in elections]
    outputs = publish_parameters(results, excluded, directory)
    if directory.resolve() == OUTPUT.resolve():
        record_generation(outputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--election', action='append')
    parser.add_argument('--output-directory', type=Path, default=OUTPUT)
    args = parser.parse_args()
    generate(list(dict.fromkeys(args.election or configured_elections())), args.output_directory)


if __name__ == '__main__':
    main()
