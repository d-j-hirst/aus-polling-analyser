"""Prepare the local rehearsal and election projects from the operator's Victorian project.

The saved project supplies the model, polls, projections and simulations. The
official count and its dress rehearsal use 50,000 draws for each live forecast.
Refresh untouched copies when the source changes and preserve any operator edits.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from vec_2026_feed import ROOT, RUN_IDS, write_complete
from live_pipeline.run_settings import check_trial_options


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
    # project already associated with a run.
    if struct.unpack_from('<i', data)[0] != 71 or not data.endswith(encoded('null')):
        raise ValueError('Expected a version-71 Victorian source project with no saved watch settings.')
    _, position = read_string(data, 4)
    election, position = read_string(data, position)
    _, body = read_string(data, position)
    if election != '2026 Victorian State Election':
        raise ValueError('The source must contain the 2026 Victorian live simulation.')
    # ProjectFiler::saveSimulations stores the iteration count immediately
    # after the simulation name. Verify the surrounding record before changing
    # it so that a report label or a different kind of simulation cannot match.
    name = encoded('Live Simulation')
    start = data.find(name, body)
    if start < 0 or data.find(name, start + 1) >= 0:
        raise ValueError('Expected one named Victorian live simulation.')
    offset = start + len(name)
    iterations, projection, prior_tpp, terms = struct.unpack_from('<iifi', data, offset)
    if iterations <= 0 or projection < 0 or not 0 <= prior_tpp <= 100 or not 0 <= terms <= 100:
        raise ValueError('Invalid Victorian live simulation record.')
    position = offset + 16
    for _ in range(terms):
        _, position = read_string(data, position)
    if struct.unpack_from('<ii', data, position) != (2, 1):
        raise ValueError('Expected automatic live mode and a live forecast report.')
    draws = 50000 if settings['kind'] == 'live' or settings.get('local_trial') else 10000
    return (data[:4] + encoded(settings['display_name']) + encoded(election) +
            encoded('run-model:0;run-projection:0') + data[body:offset] + struct.pack('<i', draws) +
            data[offset + 4:-len(encoded('null'))] +
            encoded(json.dumps(settings, separators=(',', ':'))))


def configure(mode, source):
    run_id = RUN_IDS[mode]
    rehearsal = mode == 'rehearsal'
    folder = ROOT / 'live_runs' / run_id
    (folder / 'inbox').mkdir(parents=True, exist_ok=True)
    settings = dict(version=1, election='2026vic', run_id=run_id,
                    kind='replay' if rehearsal else 'live', visibility='private' if rehearsal else 'public',
                    display_name='Victoria 2026 – VEC trial (fictitious results)' if rehearsal else '2026 Victorian State Election',
                    election_date='2026-11-25' if rehearsal else '2026-11-28',
                    timezone='Australia/Melbourne', night_end_hour=6,
                    source_directory=f'live_runs/{run_id}/inbox')
    if rehearsal:
        # The trial exercises the real forecast and its numerical comparisons.
        # Its fictitious votes are only for our local server and must never be
        # sent to a commentary service or mixed into official election history.
        settings.update(local_trial=True, commentary={'enabled': False, 'compare_writers': False})
    else:
        settings['commentary'] = {'enabled': False, 'rules_only': True}
    manifest = folder / 'run.json'
    saved = None
    if manifest.exists():
        saved = json.loads(manifest.read_bytes())
        if any(saved.get(k) != settings[k] for k in ('election', 'run_id', 'kind', 'source_directory')):
            raise ValueError(f'Existing {run_id} manifest differs from the VEC collector configuration.')
        if rehearsal:
            if saved.get('local_trial') is not True:
                raise ValueError('The VEC trial requires a private local replay with commentary off.')
            check_trial_options(saved)
        # Older prepared election projects had no commentary selection. Give
        # those the election-night setting on their next refresh. A selection
        # explicitly saved by the operator, including Off, always takes precedence.
        settings.update(saved)
    project = folder / (run_id + '.pol2')
    stamp = folder / 'prepared-project.sha256'
    if project.exists() and (not stamp.exists() or (folder / 'state.json').exists()
                             or hashlib.sha256(project.read_bytes()).hexdigest() != stamp.read_text().strip()):
        print(f'Keeping the operator project in {run_id}.')
        return
    data = project_copy(source, settings)
    if saved != settings:
        write_complete(manifest, (json.dumps(settings, indent=2) + '\n').encode())
    write_complete(project, data)
    write_complete(stamp, hashlib.sha256(data).hexdigest().encode())
    print(f'Ready: live_runs/{run_id}/{project.name}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', choices=RUN_IDS, help='Prepare just the selected project.')
    args = parser.parse_args()
    source = (ROOT / 'VIC State Election 2026.pol2').read_bytes()
    for mode in ([args.only] if args.only else RUN_IDS):
        configure(mode, source)
