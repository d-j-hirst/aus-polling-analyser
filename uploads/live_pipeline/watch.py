"""Run reporting and destination workers together; one slow server cannot hold others."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import time

from .storage import read, atomic, run_lock
from .worker import Processor, utc
from .protocol import Server, UploadQueue, DESTINATIONS


def run(folder, destinations, settings_path=None, once=False, prepare_only=False):
    folder = Path(folder).resolve()
    with run_lock(folder):
        config = read(folder/'run.json')
        settings = None if prepare_only else config.get('commentary')
        if settings_path:
            settings = read(settings_path)
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
        print(f"Watching {config['run_id']} ({target}, commentary {model}), {status}.", flush=True)
        try:
            while True:
                try:
                    worker.tick()
                except Exception as error:
                    atomic(folder/'reporting-status.json', {'updated_at': utc(), 'error': str(error)})
                    if once:
                        raise
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
            worker.close()
            # A one-shot invocation waits for bounded requests and then applies
            # the same deadline rules. It is useful for mocked/service checks.
            worker.tick()
            uploads.shutdown(wait=True)
            if once:
                for name, server in servers.items():
                    queue.tick_destination(name, server)
