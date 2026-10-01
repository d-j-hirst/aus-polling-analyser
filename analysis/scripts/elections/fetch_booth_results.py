"""Run one supported booth-result generator and publish its provenance."""

import argparse
from pathlib import Path
from lib.paths import ANALYSIS_DIRECTORY
import subprocess
import sys

from lib.provenance import booth_result_provenance
from lib.provenance import generated_provenance


MODULE_BY_JURISDICTION = {
    "nsw": "scripts.elections.fetch_election_data_nsw",
    "vic": "scripts.elections.fetch_election_data_vic",
    "qld": "scripts.elections.fetch_election_data_qld",
    "sa": "scripts.elections.fetch_election_data_sa",
}


def generator_command(election):
    region = booth_result_provenance.jurisdiction(election)
    return [
        sys.executable,
        "-m",
        MODULE_BY_JURISDICTION[region],
        "--election",
        election,
    ]


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate one historical live booth-result JSON file."
    )
    parser.add_argument(
        "--election",
        required=True,
        choices=booth_result_provenance.SUPPORTED_ELECTIONS,
    )
    args = parser.parse_args(argv)
    command = generator_command(args.election)
    subprocess.run(command, cwd=ANALYSIS_DIRECTORY, check=True)
    output = booth_result_provenance.record_generated_output(
        args.election,
        command=generated_provenance.current_command(
            arguments=argv, module="scripts.elections.fetch_booth_results"
        ),
    )
    print("Recorded generated provenance for {}".format(output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
