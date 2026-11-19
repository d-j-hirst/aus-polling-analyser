"""GUI-launched local SA replay/benchmark coordinator; no upload destinations."""
import argparse
from pathlib import Path
import sys

WORKSPACE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKSPACE/'uploads'))
from live_pipeline.storage import atomic
from live_pipeline.trial import run
from live_pipeline.worker import utc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True, type=Path)
    args = parser.parse_args()
    try:
        run(WORKSPACE, args.run)
    except BlockingIOError:
        return  # An existing helper already owns this run.
    except Exception as error:
        atomic(args.run/'trial-status.json', {'stage': 'error', 'message': str(error), 'updated_at': utc()})
        raise


if __name__ == '__main__':
    main()
