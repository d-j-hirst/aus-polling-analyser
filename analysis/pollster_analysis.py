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
    parse_finite_float,
    parse_poll_day,
)
from pollster_analysis_house_effects import (
    analyse_house_effects,
    get_n_polls,
    load_final_trend_median,
    load_new_house_effects,
)
import pollster_analysis_provenance
from pollster_analysis_variability import analyse_variability


def write_completion_status(status):
    with open('itsdone.txt', 'w') as output_file:
        output_file.write(str(status))


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
        dependencies, filenames = recorder.inputs_for(
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
        final_paths = output_paths(election)
        # Generate all three related files before replacing any current output.
        with tempfile.TemporaryDirectory(
            prefix='pollster-analysis-{}-'.format(election_code),
            dir=directory,
        ) as staging_directory:
            staged_paths = [
                str(Path(staging_directory) / Path(path).name)
                for path in final_paths
            ]
            analyse_variability(
                election, cycles, links, filenames, staged_paths[0]
            )
            analyse_house_effects(
                election, cycles, links, filenames, staged_paths[1]
            )
            analyse_bias(
                election, cycles, links, filenames, staged_paths[2]
            )
            for staged_path, final_path in zip(
                staged_paths, final_paths
            ):
                os.replace(staged_path, final_path)
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
