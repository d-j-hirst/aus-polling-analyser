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
from .worker import public_run, utc

DESTINATIONS = {'local': 'http://localhost:8000', 'test': 'https://dendrite.pythonanywhere.com',
                'remote': 'https://www.aeforecasts.com'}


class Server:
    def __init__(self, base, email, password, session=None):
        self.base, self.email, self.password = base.rstrip('/'), email, password
        self.session = session or requests.Session()
        self.connected = False

    def connect(self):
        capability = self.session.get(self.base+'/forecast-api/live/v1/capabilities', timeout=10)
        capability.raise_for_status()
        value = capability.json()
        if value.get('schema_version') != 1 or not all(value.get(k) is True for k in
                ('replay_isolation', 'idempotent_submission', 'monotonic_latest')):
            raise ValueError('Server does not explicitly support isolated version-one live/replay publication')
        login = self.session.post(self.base+'/auth-api/v1/auth/login/',
                                  data={'email': self.email, 'password': self.password}, timeout=10)
        login.raise_for_status()
        token = login.cookies.get('jwt_token')
        if not token:
            raise ValueError('Server did not return an authentication token')
        self.session.headers.update({'Authorization': 'JWT '+token})
        self.connected = True

    def submit(self, payload, sha):
        if not re.fullmatch('[a-z0-9][a-z0-9-]*', payload['run']['run_id']) or not re.fullmatch(r'(origin|f\d{8})', payload['forecast']['forecast_id']):
            raise ValueError('Invalid publication identity')
        if payload['report']['termCode'] != payload['run']['election']:
            raise ValueError('Publication report belongs to another election')
        if not self.connected:
            self.connect()
        config = payload['run']
        registration = self.session.post(self.base+'/forecast-api/live/v1/runs', json=config, timeout=15)
        if registration.status_code == 401:
            self.connected = False
        registration.raise_for_status()
        if registration.json().get('run_id') != config['run_id']:
            raise ValueError('Run registration acknowledged a different run')
        identity = payload['forecast']['forecast_id']
        response = self.session.post(self.base+f"/forecast-api/live/v1/runs/{config['run_id']}/forecasts/{identity}",
            data=json.dumps(payload, sort_keys=True, ensure_ascii=False, allow_nan=False).encode('utf-8'),
            headers={'Idempotency-Key': config['run_id']+'/'+identity, 'X-Content-SHA256': sha,
                     'Content-Type': 'application/json; charset=utf-8'}, timeout=30)
        if response.status_code == 401:
            self.connected = False
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

    def tick_destination(self, name, server):
        state_path = self.folder/'receipts'/f'{name}.json'
        state = read(state_path) if state_path.exists() else {'forecasts': {}}
        for marker in sorted((self.folder/'packages').glob('*/ready-publication.json'), key=lambda p: (p.parent.name != 'origin', p.parent.name)):
            identity = marker.parent.name
            record = state['forecasts'].get(identity, {})
            if record.get('accepted') or record.get('retry_after', 0) > time.time():
                continue
            try:
                payload, ready = read(marker.parent/'publication.json'), read(marker)
                if identity != payload['forecast']['forecast_id'] or fingerprint(payload) != ready['sha256']:
                    raise ValueError('Released publication package has changed or is incomplete')
                receipt = server.submit(payload, ready['sha256'])
                record = {'accepted': True, 'receipt': receipt}
                # A late older receipt cannot make the local status go backwards.
                previous = state.get('latest_forecast_id', '')
                state['latest_forecast_id'] = max(previous, receipt['latest_forecast_id'] or '')
                if receipt.get('latest_source_time'):
                    state['latest_source_time'] = max(state.get('latest_source_time',''), receipt['latest_source_time'])
                state['last_receipt_at'] = receipt['published_at']
            except Exception as error:
                attempts = record.get('attempts', 0)+1
                record = {'accepted': False, 'attempts': attempts, 'error': str(error),
                          'retry_after': time.time()+min(300, 2**min(attempts, 8))}
            state['forecasts'][identity] = record
            atomic(state_path, state)
            if record['accepted']:
                print(f"Uploaded {receipt['run_id']}/{identity} to {name}.", flush=True)
        return state
