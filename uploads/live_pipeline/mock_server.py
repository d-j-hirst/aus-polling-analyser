"""In-memory contract server for tests, not the Australian Election Forecasts backend.

The test server deliberately requires versioned endpoints. It demonstrates
idempotency, replay separation and latest selection without holding credentials,
writing reports into the real website, or implementing its presentation layer.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import hashlib
import re
import threading

from .storage import fingerprint
from .worker import utc
from .comparisons import instant


class MockServer:
    def __init__(self):
        self.runs, self.packages, self.latest, self.receipts = {}, {}, {}, {}
        self.mutex = threading.Lock()
        owner = self
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_): pass
            def reply(self, status, value, cookie=False):
                raw = json.dumps(value).encode()
                self.send_response(status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(raw)))
                if cookie:
                    self.send_header('Set-Cookie', 'jwt_token=mock-token; Path=/')
                self.end_headers(); self.wfile.write(raw)
            def do_GET(self):
                if self.path != '/forecast-api/live/v1/capabilities':
                    return self.reply(404, {})
                self.reply(200, {'schema_version': 1, 'replay_isolation': True,
                                'idempotent_submission': True, 'monotonic_latest': True})
            def do_POST(self):
                raw = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                if self.path == '/auth-api/v1/auth/login/':
                    return self.reply(200, {}, True)
                if self.headers.get('Authorization') != 'JWT mock-token':
                    return self.reply(401, {})
                try:
                    value = json.loads(raw)
                    with owner.mutex:
                        code, result = owner.submit(self.path, value, self.headers.get('Idempotency-Key'),
                            self.headers.get('X-Content-SHA256'), hashlib.sha256(raw).hexdigest())
                    self.reply(code, result)
                except (KeyError, ValueError, TypeError) as error:
                    self.reply(400, {'error': str(error)})
        self.http = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.http.serve_forever, daemon=True)

    @property
    def url(self):
        return f'http://127.0.0.1:{self.http.server_port}'

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.http.shutdown(); self.thread.join(); self.http.server_close()

    def submit(self, path, value, idempotency, digest=None, received_digest=None):
        if path == '/forecast-api/live/v1/runs':
            identity = value['run_id']
            if value['kind'] not in ('live', 'replay') or value['version'] != 1:
                raise ValueError('Unsupported run')
            if identity in self.runs and self.runs[identity] != value:
                return 409, {'error': 'Run metadata conflict'}
            self.runs[identity] = value
            return 200, {'run_id': identity, 'public_route': f"/{value['election']}/replays/{identity}" if value['kind'] == 'replay' else f"/{value['election']}/live"}
        match = re.fullmatch(r'/forecast-api/live/v1/runs/([a-z0-9-]+)/forecasts/(f\d{8}|origin)', path)
        if not match:
            return 404, {}
        run, forecast = match.groups()
        payload, sha = value, digest
        if run not in self.runs or payload['run'] != self.runs[run] or payload['forecast']['forecast_id'] != forecast:
            raise ValueError('Unregistered or inconsistent forecast identity')
        if idempotency != run+'/'+forecast or sha != received_digest or payload['schema_version'] != 1:
            raise ValueError('Invalid idempotency key, schema or digest')
        key = run, forecast
        if key in self.packages:
            if fingerprint(self.packages[key]) != sha:
                return 409, {'error': 'Forecast identity is already immutable'}
        else:
            self.packages[key] = payload
            self.receipts[key] = {'run_id': run, 'forecast_id': forecast, 'sha256': sha, 'published_at': utc()}
        newest = self.latest.get(run)
        # Source observations advance monotonically within a run. The forecast
        # sequence resolves same-time recalculations following an input revision.
        if not payload['forecast'].get('origin') and (newest is None or (instant(payload['forecast']['source_time'], payload['run']['timezone']), forecast) > (
                instant(self.packages[run, newest]['forecast']['source_time'], payload['run']['timezone']), newest)):
            self.latest[run] = forecast
        newest = self.latest.get(run)
        return 200, dict(self.receipts[key], latest_forecast_id=newest,
                        latest_source_time=self.packages[run, newest]['forecast']['source_time'] if newest else None)
