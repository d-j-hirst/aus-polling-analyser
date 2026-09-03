"""Run one supported booth-result generator and publish its provenance."""

import argparse
from pathlib import Path
import subprocess
import sys

import booth_result_provenance


ANALYSIS_DIRECTORY = Path(__file__).resolve().parent
SCRIPT_BY_JURISDICTION = {
    "nsw": "fetch_election_data_nsw.py",
    "vic": "fetch_election_data_vic.py",
    "qld": "fetch_election_data_qld.py",
    "sa": "fetch_election_data_sa.py",
}


def generator_command(election):
    region = booth_result_provenance.jurisdiction(election)
    return [
        sys.executable,
        str(ANALYSIS_DIRECTORY / SCRIPT_BY_JURISDICTION[region]),
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
        command=[Path(sys.executable).name, Path(__file__).name] + list(
            argv if argv is not None else sys.argv[1:]
        ),
    )
    print("Recorded generated provenance for {}".format(output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
