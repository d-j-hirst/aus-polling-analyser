"""Reduce historical calibration runs into pollster-level model parameters.

This entry point selects calibration evidence, stages the three output files
for each election, and records their provenance. The numerical reducers live
in focused modules: pollster_analysis_variability,
pollster_analysis_house_effects and pollster_analysis_bias.
"""

import os
from pathlib import Path
import sys
import tempfile

from election_code import ElectionCode
import generated_provenance
from pollster_analysis_bias import analyse_bias
from pollster_analysis_common import (
    COALITION_PARTY,
    LIBERAL_PARTY,
    Config,
    ConfigError,
    canonical_party,
    check_dates,
    directory,
    get_election_cycles,
    get_links,
    get_significant_parties,
    output_party,
    output_paths,
)
from pollster_analysis_evidence import (
    BiasEvidence,
    CalibrationEvidence,
    load_calibration_evidence,
)
from pollster_analysis_house_effects import analyse_house_effects
import pollster_analysis_provenance
from pollster_analysis_variability import analyse_variability


def write_completion_status(status):
    with open('itsdone.txt', 'w') as output_file:
        output_file.write(str(status))


def analyse_evidence(election, cycles, links, evidence, output_directory):
    """Stage and publish one election's three reducers from typed evidence."""

    output_directory = Path(output_directory)
    final_paths = [
        output_directory / Path(path).name
        for path in output_paths(election)
    ]
    with tempfile.TemporaryDirectory(
        prefix='pollster-analysis-{}-'.format(election.short()),
        dir=output_directory,
    ) as staging_directory:
        staged_paths = [
            Path(staging_directory) / path.name
            for path in final_paths
        ]
        analyse_variability(
            election, cycles, links, evidence, staged_paths[0]
        )
        analyse_house_effects(
            election, cycles, links, evidence, staged_paths[1]
        )
        analyse_bias(
            election, cycles, links, evidence, staged_paths[2]
        )
        for staged_path, final_path in zip(staged_paths, final_paths):
            os.replace(staged_path, final_path)
    return [str(path) for path in final_paths]


def run_analysis(argv=None):
    config = Config(argv)
    cycles = get_election_cycles()
    links = get_links()
    command_arguments = sys.argv[1:] if argv is None else list(argv)
    recorder = pollster_analysis_provenance.PollsterAnalysisRecorder(
        [Path(__file__).name] + command_arguments
    )
    for election in config.elections:
        election_code = election.short()
        dependencies, calibration_paths = recorder.inputs_for(
            election_code,
            lambda candidate, target: check_dates(
                ElectionCode(int(candidate[:4]), candidate[4:]),
                ElectionCode(int(target[:4]), target[4:]),
                cycles,
                equals=True,
            ),
            [
                canonical_party(party)
                for party in get_significant_parties(election)
            ],
        )
        evidence = load_calibration_evidence(calibration_paths)
        final_paths = analyse_evidence(
            election, cycles, links, evidence, directory
        )
        recorder.record(
            election_code,
            final_paths,
            dependencies,
        )
    write_completion_status(1)
    return 0


def main(argv=None):
    try:
        return run_analysis(argv)
    except (
        ConfigError,
        generated_provenance.GeneratedProvenanceError,
    ) as e:
        print(
            'Could not analyse pollsters: {}'.format(e),
            file=sys.stderr,
        )
        write_completion_status(2)
        return 2


if __name__ == '__main__':
    sys.exit(main())
