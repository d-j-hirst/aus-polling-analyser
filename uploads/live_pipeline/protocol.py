"""Versioned website boundary, with independent receipts for each destination.

No package is sent through the legacy submit-report endpoint. An explicit server
capability prevents a replay being mistaken for the official live election. The
server owns latest selection: accepting an older package must only add history.
"""
from pathlib import Path
import json
import re
import time
import requests

from .storage import read, atomic, fingerprint
from .comparisons import instant
from .worker import public_run, utc

DESTINATIONS = {'local': 'http://localhost:8000', 'test': 'https://dendrite.pythonanywhere.com',
                'remote': 'https://www.aeforecasts.com'}


class DuplicateObservation(ValueError):
    def __init__(self, forecast_id):
        self.forecast_id = forecast_id
        super().__init__(f'This feed was already uploaded as {forecast_id}. The existing forecast was preserved.')


def source_observation(metadata):
    """The same votes at a later feed time still constitute a new observation."""
    timestamp, capture = metadata.get('source_time'), metadata.get('source_fingerprint')
    if timestamp and capture and not metadata.get('origin'):
        return instant(timestamp, 'UTC'), capture
    return None


def replay_manifest(folder, config):
    """Read the frozen publication markers once for all selected servers."""
    if config['kind'] != 'replay' or not re.fullmatch('[a-z0-9][a-z0-9-]*', config['run_id']):
        raise ValueError('Completion requires a saved replay run.')
    publications = {}
    for package in (Path(folder)/'packages').iterdir():
        if not package.is_dir() or not re.fullmatch(r'origin|f[0-9]{8}', package.name):
            continue
        marker = package/'ready-publication.json'
        if not marker.is_file():
            raise ValueError(f'{package.name} is still awaiting publication preparation.')
        ready = read(marker)
        if (not isinstance(ready, dict) or ready.get('forecast_id') != package.name
                or not isinstance(ready.get('sha256'), str) or not re.fullmatch('[a-f0-9]{64}', ready['sha256'])):
            raise ValueError(f'Invalid publication marker for {package.name}.')
        publications[package.name] = ready['sha256']
    if not any(identity != 'origin' for identity in publications):
        raise ValueError('The replay has no published snapshots to complete.')
    return publications


class Server:
    def __init__(self, base, email, password, session=None):
        self.base, self.email, self.password = base.rstrip('/'), email, password
        self.session = session or requests.Session()
        self.connected = False

    def check_capabilities(self):
        """Check the public contract without logging in, registering a run or sending any votes."""
        capability = self.session.get(self.base+'/forecast-api/live/v1/capabilities', timeout=10)
        capability.raise_for_status()
        try:
            value = capability.json()
        except ValueError:
            content_type = capability.headers.get('Content-Type', 'unspecified').split(';', 1)[0]
            raise ValueError(f'The capability URL returned HTTP {capability.status_code} ({content_type}). Expected JSON.') from None
        if not isinstance(value, dict) or value.get('schema_version') != 1 or not all(value.get(k) is True for k in
                ('replay_isolation', 'idempotent_submission', 'monotonic_latest')):
            raise ValueError('Server does not explicitly support isolated version-one live/replay publication')

    def authenticate(self):
        """Use the upload login shared by forecast and human commentary endpoints."""
        login = self.session.post(self.base+'/auth-api/v1/auth/login/',
                                  data={'email': self.email, 'password': self.password}, timeout=10)
        login.raise_for_status()
        token = login.cookies.get('jwt_token')
        if not token:
            raise ValueError('Server did not return an authentication token')
        self.session.headers.update({'Authorization': 'JWT '+token})

    def connect(self):
        self.check_capabilities()
        self.authenticate()
        self.connected = True

    def _post(self, path, **options):
        """Send a publication request, renewing an expired upload login once."""
        if not self.connected:
            self.connect()
        url = self.base + path
        for attempt in range(2):
            response = self.session.post(url, **options)
            expired = response.status_code == 401
            if response.status_code == 403:
                # The website also uses 403 for a genuine lack of publishing
                # permission. Only its explicit token-expiry reply calls for login.
                try:
                    detail = response.json()
                except ValueError:
                    detail = None
                expired = isinstance(detail, dict) and detail.get('detail') == 'Token has expired.'
            if not expired:
                break
            self.connected = False
            if attempt == 0:
                self.connect()
        return response

    def complete_replay(self, config, publications):
        identity = config['run_id']
        response = self._post(f'/forecast-api/live/v1/runs/{identity}/complete',
            json={'election': config['election'], 'publications': publications}, timeout=30)
        if response.status_code == 404:
            raise ValueError('This destination lacks the saved replay or the replay completion endpoint.')
        if response.status_code == 409:
            raise ValueError(response.json().get('detail', 'The server has a different set of publications.'))
        response.raise_for_status()
        receipt = response.json()
        count = len(publications) - int('origin' in publications)
        if (receipt.get('run_id') != identity or receipt.get('replay_complete') is not True
                or receipt.get('snapshot_count') != count or not receipt.get('completed_at')):
            raise ValueError('Invalid replay completion acknowledgement.')
        return receipt

    def submit(self, payload, sha):
        if not re.fullmatch('[a-z0-9][a-z0-9-]*', payload['run']['run_id']) or not re.fullmatch(r'(origin|f\d{8})', payload['forecast']['forecast_id']):
            raise ValueError('Invalid publication identity')
        if payload['report']['termCode'] != payload['run']['election']:
            raise ValueError('Publication report belongs to another election')
        config = payload['run']
        registration = self._post('/forecast-api/live/v1/runs', json=config, timeout=15)
        registration.raise_for_status()
        if registration.json().get('run_id') != config['run_id']:
            raise ValueError('Run registration acknowledged a different run')
        identity = payload['forecast']['forecast_id']
        response = self._post(f"/forecast-api/live/v1/runs/{config['run_id']}/forecasts/{identity}",
            data=json.dumps(payload, sort_keys=True, ensure_ascii=False, allow_nan=False).encode('utf-8'),
            headers={'Idempotency-Key': config['run_id']+'/'+identity, 'X-Content-SHA256': sha,
                     'Content-Type': 'application/json; charset=utf-8'}, timeout=30)
        if response.status_code == 409:
            existing = response.json().get('existing_forecast_id', '')
            if isinstance(existing, str) and existing != identity and re.fullmatch(r'f[0-9]{8}', existing):
                raise DuplicateObservation(existing)
        response.raise_for_status()
        receipt = response.json()
        if any(receipt.get(k) != v for k, v in {'run_id': config['run_id'], 'forecast_id': identity, 'sha256': sha}.items()):
            raise ValueError('Server receipt does not match the submitted immutable forecast')
        if not receipt.get('published_at') or 'latest_forecast_id' not in receipt or (not receipt['latest_forecast_id'] and not payload['forecast'].get('origin')):
            raise ValueError('Server receipt lacks publication/latest identity')
        return receipt


class UploadQueue:
    def __init__(self, folder, servers):
        self.folder, self.servers = Path(folder), servers

    def fill_older_receipts(self, records, timezone):
        """Recover feed identities for acknowledgements saved by older uploaders."""
        missing = {identity for identity, record in records.items()
                   if identity != 'origin' and record.get('accepted') and 'source_fingerprint' not in record}
        if not missing:
            return
        # A single read of the GUI ledger usually identifies every snapshot.
        # If an older run calculated the same feed twice, only its last ID may
        # be in that ledger. Its other immutable packages carry their own identity.
        try:
            captures = read(self.folder/'state.json').get('captures', {})
        except (FileNotFoundError, ValueError):
            captures = {}
        for capture_hash, capture in captures.items():
            identity = capture.get('forecast_id')
            if identity in missing and capture.get('forecast') == 'completed' and capture.get('source_time'):
                records[identity].update(source_time=instant(capture['source_time'], timezone).isoformat(),
                                         source_fingerprint=capture_hash)
                missing.remove(identity)
        for identity in missing:
            package = self.folder/'packages'/identity/'publication.json'
            if package.is_file():
                metadata = read(package)['forecast']
                records[identity].update(source_time=metadata.get('source_time'),
                                         source_fingerprint=metadata.get('source_fingerprint'))

    def tick_destination(self, name, server):
        state_path = self.folder/'receipts'/f'{name}.json'
        state = read(state_path) if state_path.exists() else {'forecasts': {}}
        accepted = None
        for marker in sorted((self.folder/'packages').glob('*/ready-publication.json'), key=lambda p: (p.parent.name != 'origin', p.parent.name)):
            identity = marker.parent.name
            record = state['forecasts'].get(identity, {})
            if record.get('accepted') or record.get('duplicate_of') or record.get('retry_after', 0) > time.time():
                continue
            try:
                payload, ready = read(marker.parent/'publication.json'), read(marker)
                if identity != payload['forecast']['forecast_id'] or fingerprint(payload) != ready['sha256']:
                    raise ValueError('Released publication package has changed or is incomplete')
                forecast = payload['forecast']
                source = source_observation(forecast)
                if accepted is None:
                    # Idle polls and already acknowledged packages do no migration work.
                    self.fill_older_receipts(state['forecasts'], payload['run']['timezone'])
                    accepted = {source_observation(past): key for key, past in state['forecasts'].items()
                                if key != 'origin' and past.get('accepted') and past.get('source_fingerprint') and past.get('source_time')}
                if source in accepted:
                    raise DuplicateObservation(accepted[source])
                receipt = server.submit(payload, ready['sha256'])
                record = {'accepted': True, 'receipt': receipt}
                if source:
                    accepted[source] = identity
                    record.update(source_time=forecast['source_time'], source_fingerprint=forecast['source_fingerprint'])
                # A late older receipt cannot make the local status go backwards.
                previous = state.get('latest_forecast_id', '')
                state['latest_forecast_id'] = max(previous, receipt['latest_forecast_id'] or '')
                if receipt.get('latest_source_time'):
                    state['latest_source_time'] = max(state.get('latest_source_time',''), receipt['latest_source_time'])
                state['last_receipt_at'] = receipt['published_at']
            except DuplicateObservation as error:
                # This destination already has the observation. Keep its
                # original receipt and leave other destinations independent.
                record = {'accepted': False, 'duplicate_of': error.forecast_id, 'error': str(error)}
                print(f"Held {identity} for {name}: {error}", flush=True)
            except Exception as error:
                attempts = record.get('attempts', 0)+1
                record = {'accepted': False, 'attempts': attempts, 'error': str(error),
                          'retry_after': time.time()+min(300, 2**min(attempts, 8))}
            state['forecasts'][identity] = record
            atomic(state_path, state)
            if record['accepted']:
                print(f"Uploaded {receipt['run_id']}/{identity} to {name}.", flush=True)
        return state
