"""Run reporting and destination workers together; one slow server cannot hold others."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import time

from .storage import read, atomic, run_lock
from .worker import Processor, ready_forecasts, utc
from .protocol import Server, UploadQueue, DESTINATIONS
from .run_settings import check_trial_options


DEFAULT_IDLE_HOURS = 14


def run(folder, destinations, settings_path=None, once=False, prepare_only=False, idle_hours=DEFAULT_IDLE_HOURS):
    folder = Path(folder).resolve()
    with run_lock(folder):
        config = read(folder/'run.json')
        settings = None if prepare_only else config.get('commentary')
        if settings_path:
            settings = read(settings_path)
        check_trial_options(config, destinations, settings)
        worker = Processor(folder, settings)
        servers = {} if prepare_only else {name: Server(DESTINATIONS[name],
            os.environ.get('AUTO_EMAIL', ''), os.environ.get('AUTO_PASSWORD', '')) for name in destinations}
        queue = UploadQueue(folder, servers)
        uploads = ThreadPoolExecutor(max_workers=max(1, len(servers)))
        pending = {}
        target = ', '.join(servers) if servers else 'local files'
        has_forecasts = any((folder/'packages').glob('*/ready-numerical.json'))
        status = 'watching for new forecasts' if has_forecasts else 'waiting for first forecast'
        model = settings['model'] if settings and settings.get('enabled') else 'off'
        writers = f'commentary {model}'
        if worker.rules_only:
            writers = 'rules-based commentary'
        elif worker.compare_writers:
            writers += ', rule-based comparison on'
        print(f"Watching {config['run_id']} ({target}, {writers}), {status}.", flush=True)
        seen_forecasts = set()
        paths = []
        last_arrival = time.monotonic()
        try:
            while True:
                try:
                    paths = ready_forecasts(folder)
                    # Measure actual time waiting for a new numerical handoff.
                    # Reopening old packages, status heartbeats and upload
                    # retries are routine work even in an abandoned replay.
                    known_count = len(seen_forecasts)
                    seen_forecasts.update(path.parent.name for path in paths)
                    if len(seen_forecasts) > known_count:
                        last_arrival = time.monotonic()
                    worker.tick(paths)
                except Exception as error:
                    worker.publish_status({'error': str(error)})
                    if once:
                        raise
                if not once and idle_hours and time.monotonic() - last_arrival >= idle_hours * 3600:
                    print(f"Stopping {config['run_id']} after {idle_hours:g} hours without a new forecast.", flush=True)
                    break
                for name, server in servers.items():
                    if name in pending and pending[name].done():
                        try:
                            pending[name].result()
                        except Exception as error:
                            atomic(folder/'receipts'/f'{name}-error.json', {'error': str(error), 'updated_at': utc()})
                        del pending[name]
                    if name not in pending:
                        pending[name] = uploads.submit(queue.tick_destination, name, server)
                if once:
                    break
                time.sleep(.5)
        except KeyboardInterrupt:
            pass
        finally:
            try:
                worker.close()
                # Finish only the handoffs seen while watching. A forecast
                # arriving during shutdown belongs to the next invocation.
                worker.tick(paths)
            finally:
                uploads.shutdown(wait=True)
            if once:
                for name, server in servers.items():
                    queue.tick_destination(name, server)
