"""Prepare the local rehearsal and election projects from the operator's Victorian project.

The saved project supplies the model, polls, projections and simulations. Only
the display label, startup macro and saved watch settings differ between copies.
Refresh untouched copies when the source changes and preserve any operator edits.
"""

import hashlib
import json
from pathlib import Path
import struct

from vec_2026_feed import ROOT, RUN_IDS, write_complete


def encoded(value):
    data = value.encode('utf-8')
    return struct.pack('<i', len(data)) + data


def read_string(data, position):
    length, = struct.unpack_from('<i', data, position)
    end = position + 4 + length
    if length < 0 or end > len(data):
        raise ValueError('Invalid saved project header')
    return data[position + 4:end].decode(), end


def project_copy(data, settings):
    # ProjectFiler writes three strings after the version and the saved watch
    # settings as its last string. Refuse an unfamiliar layout or a source
    # project already associated with a run. All intervening bytes are copied.
    if struct.unpack_from('<i', data)[0] != 71 or not data.endswith(encoded('null')):
        raise ValueError('Expected a version-71 Victorian source project with no saved watch settings.')
    _, position = read_string(data, 4)
    election, position = read_string(data, position)
    _, body = read_string(data, position)
    if election != '2026 Victorian State Election' or encoded('Live Simulation') not in data:
        raise ValueError('The source must contain the 2026 Victorian live simulation.')
    return (data[:4] + encoded(settings['display_name']) + encoded(election) +
            encoded('run-model:0;run-projection:0') + data[body:-len(encoded('null'))] +
            encoded(json.dumps(settings, separators=(',', ':'))))


def configure(mode, source):
    run_id = RUN_IDS[mode]
    rehearsal = mode == 'rehearsal'
    folder = ROOT / 'live_runs' / run_id
    (folder / 'inbox').mkdir(parents=True, exist_ok=True)
    settings = dict(version=1, election='2026vic', run_id=run_id,
                    kind='replay' if rehearsal else 'live', visibility='private' if rehearsal else 'public',
                    display_name='Victoria 2026 rehearsal' if rehearsal else '2026 Victorian State Election',
                    election_date='2026-11-25' if rehearsal else '2026-11-28',
                    timezone='Australia/Melbourne', night_end_hour=6,
                    source_directory=f'live_runs/{run_id}/inbox')
    manifest = folder / 'run.json'
    if manifest.exists():
        saved = json.loads(manifest.read_bytes())
        if any(saved.get(k) != settings[k] for k in ('election', 'run_id', 'kind', 'source_directory')):
            raise ValueError(f'Existing {run_id} manifest differs from the VEC collector configuration.')
        if rehearsal and saved.get('visibility') != 'private':
            raise ValueError('VEC rehearsal data requires a private run.')
        settings = saved
    project = folder / (run_id + '.pol2')
    stamp = folder / 'prepared-project.sha256'
    if project.exists() and (not stamp.exists() or (folder / 'state.json').exists()
                             or hashlib.sha256(project.read_bytes()).hexdigest() != stamp.read_text().strip()):
        print(f'Keeping the operator project in {run_id}.')
        return
    data = project_copy(source, settings)
    if not manifest.exists():
        write_complete(manifest, (json.dumps(settings, indent=2) + '\n').encode())
    write_complete(project, data)
    write_complete(stamp, hashlib.sha256(data).hexdigest().encode())
    print(f'Ready: live_runs/{run_id}/{project.name}')


if __name__ == '__main__':
    source = (ROOT / 'VIC State Election 2026.pol2').read_bytes()
    for mode in RUN_IDS:
        configure(mode, source)
