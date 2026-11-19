"""Run a prepared commentary replay without manual feeder steps.

The GUI owns forecasting; this helper supplies one immutable feed at a time,
assembles local evidence, and benchmarks consecutive forecast comparisons.
It never uploads. A restart resumes receipts instead of repeating paid calls.
"""
import os
from pathlib import Path
import time

from . import benchmark
from .storage import atomic, read, run_lock
from .worker import Processor, utc


def load_key(workspace):
    """Use the upload manager's existing local credential file, never exports."""
    if not os.environ.get('OPENAI_API_KEY'):
        import environ
        path = Path(workspace)/'uploads'/'.env'
        if path.is_file():
            environ.Env.read_env(str(path))
    return bool(os.environ.get('OPENAI_API_KEY'))


class Trial:
    def __init__(self, workspace, folder, key_available=None, client_factory=None):
        self.workspace, self.folder = Path(workspace).resolve(), Path(folder).resolve()
        self.config = read(self.folder/'run.json')
        self.settings = self.config['commentary_trial']
        if self.config['election'] != '2026sa' or self.config['kind'] != 'replay':
            raise ValueError('The prepared commentary trial requires a 2026sa replay')
        if not self.settings.get('enabled'):
            raise ValueError('The prepared commentary trial is disabled')
        # Older projects set paid_authorized automatically. Only an explicit
        # selection when preparing the project requests the paid part of a trial.
        self.paid_benchmark_requested = self.settings.get('paid_benchmark_requested') is True
        self.key_available = key_available or (lambda: load_key(self.workspace))
        self.client_factory = client_factory
        if self.paid_benchmark_requested:
            self.models = read(self.workspace/self.settings['models_file'])
            benchmark.validate_models(self.models)
            self.budget = self.settings.get('budget_usd', 10)
            benchmark.validate_budget(self.budget)
        self.inbox = self.workspace/self.config['source_directory']
        self.inbox.mkdir(parents=True, exist_ok=True)
        # Import the shared delivery implementation rather than reproducing its
        # filename, immutable-copy and temporary-file rules here.
        import replay_feeder
        self.feeder = replay_feeder
        records = {r[0]: r[2] for r in self.feeder.catalogue(
            self.workspace/self.settings['archive'], self.config['election'])}
        codes = self.settings['capture_codes']
        if len(codes) < 2 or codes != sorted(set(codes)):
            raise ValueError('Select at least two distinct captures in chronological order')
        self.sources = [records[code] for code in codes]
        allowed = {source.name for source in self.sources}
        unexpected = [p.name for p in self.inbox.iterdir()
                      if p.suffix.lower() in ('.xml', '.zip') and p.name not in allowed]
        if unexpected:
            raise ValueError('The trial inbox contains unselected feeds: '+', '.join(unexpected))
        # Numerical packages are released only locally. Paid commentary is
        # exclusively handled by the benchmark, once per variant, below.
        self.processor = Processor(self.folder, {'enabled': False})

    def status(self, stage, message, **extra):
        atomic(self.folder/'trial-status.json', dict(stage=stage, message=message,
               updated_at=utc(), **extra))

    def armed(self):
        path = self.folder/'state.json'
        return path.exists() and read(path).get('state') == 'armed'

    def forecast_for(self, source, records):
        matches = [r for r in records.values() if Path(r.get('file', '')).name == source.name]
        for record in matches:
            if record.get('state') == 'rejected' or record.get('forecast') in ('failed', 'skipped_newer_available'):
                raise ValueError(source.name+': '+record.get('forecast_message', record.get('error', 'The selected capture was skipped')))
            identity = record.get('forecast_id')
            if record.get('forecast') == 'completed' and identity and (self.folder/'packages'/identity/'ready-numerical.json').exists():
                return identity
        return None

    def tick(self):
        """Advance one stage, finishing after the forecasts and any requested benchmark.

        Waiting for a successful first forecast is essential: otherwise the
        normal newest-feed policy could skip it, leaving no zero-count anchor.
        Neither the feeder nor the benchmark advances an explicitly paused run.
        """
        # One published ledger supplies a consistent view of this polling
        # cycle. Reopening it for every preceding capture made a long replay
        # repeatedly compete with the Windows writer for the same file. New
        # completions are picked up on the next cycle, as for new deliveries.
        state = read(self.folder/'state.json')
        if state.get('state') != 'armed':
            self.status('paused', 'Trial paused; resume watching to continue.')
            return False
        self.processor.tick()
        identities = []
        for number, source in enumerate(self.sources, 1):
            identity = self.forecast_for(source, state.get('captures', {}))
            if identity is None:
                self.feeder.deliver(source, self.inbox)
                self.status('forecasting', f'Waiting for forecast {number} of {len(self.sources)}: {source.name}')
                return False
            identities.append(identity)
        self.processor.tick()
        if not self.paid_benchmark_requested:
            self.status('complete', 'Numerical replay complete. Paid commentary was not selected.')
            return True
        # Compare each delivered capture only with its immediate predecessor.
        # The normal worker also prepares since-start and longer-window reports;
        # including those here would add paid cases without testing a new update.
        # Unchanged or insignificant counts have local notices, not LLM requests.
        selected = []
        for previous, current in zip(identities, identities[1:]):
            selected.extend(benchmark.select_packets(self.folder, [current], previous, 1))
        if benchmark.has_pending_requests(selected, self.models, self.folder/'benchmark') and not self.key_available():
            self.status('waiting_for_key', 'Add OPENAI_API_KEY to uploads/.env. All selected forecasts are ready; no new API calls have been made.')
            return False
        self.status('benchmarking', f'Comparing {len(self.models)} model/speed/reasoning variants on {len(selected)} successive updates; no uploads.')
        results = benchmark.run_benchmark(selected, self.models, self.folder/'benchmark',
            self.budget, concurrency=3, client_factory=self.client_factory,
            keep_running=self.armed,
            on_progress=lambda done, total: self.status('benchmarking', f'Benchmark receipts: {done} of {total}', completed=done, total=total))
        if not self.armed():
            self.status('paused', 'Trial paused. Completed calls are retained and will not repeat.')
            return False
        successes = sum(r['status'] == 'complete' for r in results)
        self.status('complete', f'Benchmark finished: {successes} of {len(results)} responses accepted. Open benchmark/review.html in the run folder.',
                    accepted=successes, total=len(results), review=str(self.folder/'benchmark'/'review.html'))
        return True

    def close(self):
        self.processor.close()


def run(workspace, folder):
    folder = Path(folder)
    with run_lock(folder):
        # C++ writes run.json when it has armed the session. The GUI launches
        # this helper at the same time, so allow that short handover to finish.
        deadline = time.monotonic()+60
        while not ((folder/'run.json').exists() and (folder/'state.json').exists()
                   and read(folder/'state.json').get('state') == 'armed'):
            if time.monotonic() >= deadline:
                raise TimeoutError('The GUI did not arm the replay. Check its live-run error message.')
            time.sleep(.5)
        trial = Trial(workspace, folder)
        try:
            while trial.armed():
                if trial.tick():
                    break
                time.sleep(2)
        finally:
            trial.close()
