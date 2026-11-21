"""Deliver retained feeds to an inbox without running a forecast or changing votes.

Names retain their source clocks. Acceleration changes only the wait between
deliveries, never the observation timestamp used by the counting model. Stepping
uses the latest file already in the inbox. A full local rehearsal can instead
advance on the website's acknowledgement of each forecast.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime

WORKSPACE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WORKSPACE/'uploads'))
from live_pipeline.run_settings import feed_inbox, load_run
from live_pipeline.storage import read, atomic, run_lock


def catalogue(folder, election):
    region = election[4:]
    patterns = {
        'sa': rf'el{election[:4]}(\d{{12}})\.xml',
        'vic': rf'state{election[:4]}mediafilelitepplh(?:v2)?_(\d{{8}})_(\d{{6}})\.(?:zip|xml)',
        'fed': r'aec-mediafeed-(?:results-)?detailed(?:-light)?-\d+-(\d{14})\.(?:zip|xml)',
        'qld': r'(\d{14})_publicresults\.(?:zip|xml)',
        'wa': r'waec-la-verbose-(\d{14})\.xml',
    }
    if region not in patterns:
        raise ValueError('No timestamped replay filename convention for this jurisdiction')
    records = {}
    for path in Path(folder).iterdir():
        match = re.fullmatch(patterns[region], path.name, re.I)
        if not path.is_file() or not match:
            continue
        code = ''.join(match.groups())
        if len(code) == 12:
            code = '20'+code
        clock = datetime.strptime(code, '%Y%m%d%H%M%S')
        if code in records:
            raise ValueError(f'Multiple files have capture time {code}; choose a unique source set')
        records[code] = (code, clock, path)
    return [records[k] for k in sorted(records)]


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


def deliver(source, destination):
    target = Path(destination)/source.name
    digest = file_hash(source)
    if target.exists():
        if file_hash(target) != digest:
            raise ValueError('Delivery would replace a different immutable capture: '+target.name)
        return digest
    temporary = target.with_name(target.name+'.part')
    shutil.copy2(source, temporary)
    if file_hash(temporary) != digest:
        raise ValueError('Source changed during delivery')
    os.replace(temporary, target)
    return digest


def next_delivery(records, inbox):
    """Use the watched folder as the position for single-step delivery.

    This also recognises files copied manually before the feeder was started.
    An empty inbox selects the first capture; a present final capture stops.
    The archive remains immutable and every delivery is an atomic copy.
    """
    present = []
    for index, (_, _, source) in enumerate(records):
        target = Path(inbox)/source.name
        if target.exists():
            if file_hash(source) != file_hash(target):
                raise ValueError('Inbox contains a conflicting immutable capture: '+target.name)
            present.append(index)
    index = max(present)+1 if present else 0
    return records[index:index+1]


def wait_for_local_upload(folder, code=None):
    """Wait for this specific capture (or the pre-count reference) to reach
    the local website. The GUI can skip captures that arrive while it is busy.
    Waiting on its recorded forecast ID and the uploader's receipt gives every
    capture a forecast, even after a restart or a temporary upload failure."""
    state_path = folder/'state.json'
    receipts_path = folder/'receipts'/'local.json'
    last_message = None
    while True:
        state = read(state_path) if state_path.exists() else {}
        message = state.get('message') or 'Waiting for Watch / Resume.'
        if state.get('state') == 'armed':
            identity = 'origin'
            if code is not None:
                identity = None
                matches = [row for row in state['captures'].values() if row.get('capture_code') == code]
                for row in matches:
                    if row['state'] == 'rejected' or row.get('forecast') in (
                            'failed', 'skipped_newer_available', 'interrupted_superseded'):
                        reason = row.get('forecast_message') or row.get('error') or row.get('forecast') or row['state']
                        raise ValueError(f'{code}: {reason}')
                    if row.get('forecast') == 'completed':
                        identity = row['forecast_id']
                    elif row['state'] == 'duplicate' and len(matches) == 1:
                        raise ValueError(f"{code}: this capture duplicates an earlier source observation.")
            message = f'Waiting for forecast {code}.'
            if identity:
                receipts = read(receipts_path)['forecasts'] if receipts_path.exists() else {}
                receipt = receipts.get(identity, {})
                if receipt.get('accepted'):
                    return
                message = f'Waiting for local upload of {identity}. ' + receipt.get('error', 'Start the uploader with --local.')
        if message != last_message:
            print(message, flush=True)
            last_message = message
        time.sleep(2)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-id', help='Use the election and inbox from a saved replay run')
    parser.add_argument('--election')
    parser.add_argument('--source', type=Path)
    parser.add_argument('--inbox', type=Path)
    pacing = parser.add_mutually_exclusive_group()
    pacing.add_argument('--step', action='store_true', help='Deliver just the next capture')
    pacing.add_argument('--after-local-upload', action='store_true',
                        help='Deliver each capture after the previous local website receipt, including the pre-count reference')
    parser.add_argument('--speed', type=float, default=1, help='Original speed is 1; for example 60 is one hour per minute')
    parser.add_argument('--codes-file', type=Path, help='Optional subset of timestamp codes, one per line')
    parser.add_argument('--limit', type=int)
    parser.add_argument('--dry-run', action='store_true', help='List capture count and additional inbox storage without copying')
    args = parser.parse_args(argv)
    if args.run_id:
        if args.inbox:
            parser.error('Choose either --run-id or --inbox.')
        try:
            run_folder, config = load_run(WORKSPACE, args.run_id)
            if config.get('kind') != 'replay':
                raise ValueError('The selected run is not a replay.')
            if args.election and args.election != config['election']:
                raise ValueError('The selected replay is for a different election.')
            args.election = config['election']
            args.inbox = feed_inbox(WORKSPACE, config)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            parser.error(str(error))
        if args.source is None:
            args.source = WORKSPACE/'forecasts'/args.election/'live-snapshots'/'feeds'
    elif not all((args.election, args.source, args.inbox)):
        parser.error('Specify --run-id or all of --election, --source and --inbox.')
    if args.after_local_upload and (not args.run_id or args.speed != 1):
        parser.error('--after-local-upload requires a saved --run-id and uses receipt-based pacing instead of --speed.')
    if args.speed <= 0 or not args.speed < float('inf'):
        parser.error('Speed must be positive and finite')
    if args.source.resolve() == args.inbox.resolve():
        parser.error('The delivery inbox must differ from the archive')
    records = catalogue(args.source, args.election)
    if args.codes_file:
        selected = {line.strip() for line in args.codes_file.read_text(encoding='utf-8-sig').splitlines() if line.strip()}
        selected = {'20'+s if len(s) == 12 else s for s in selected}
        if selected-{r[0] for r in records}:
            parser.error('Sequence contains unavailable timestamps')
        records = [r for r in records if r[0] in selected]
    if args.limit:
        records = records[:args.limit]
    print(f'{len(records)} captures; {sum(p.stat().st_size for _, _, p in records)/1024**2:.1f} MiB of inbox copies (plus retained run copies).')
    if args.dry_run:
        return
    args.inbox.mkdir(parents=True, exist_ok=True)
    state_path = args.inbox/'feeder-state.json'
    with run_lock(args.inbox):
        state = read(state_path) if state_path.exists() else {'election': args.election, 'delivered': {}}
        if state['election'] != args.election:
            raise ValueError('Inbox feeder state belongs to a different election')
        if args.step:
            records = next_delivery(records, args.inbox)
            if not records:
                print('The final selected capture is already in the inbox.')
        if args.after_local_upload:
            wait_for_local_upload(run_folder)
        previous = None
        for code, clock, path in records:
            if args.step or code not in state['delivered']:
                if previous and not args.step and not args.after_local_upload:
                    until = time.monotonic()+max(0, (clock-previous).total_seconds())/args.speed
                    while time.monotonic() < until:
                        time.sleep(max(0, min(.5, until-time.monotonic())))
                digest = deliver(path, args.inbox)
                state['delivered'][code] = {'sha256': digest, 'delivered_at_unix': time.time()}
                atomic(state_path, state)
                print(code, path.name, flush=True)
            if args.after_local_upload:
                wait_for_local_upload(run_folder, code)
            previous = clock
            if args.step:
                break
        if args.after_local_upload:
            print(f'Local website acknowledged all {len(records)} selected captures.', flush=True)


if __name__ == '__main__':
    main()
