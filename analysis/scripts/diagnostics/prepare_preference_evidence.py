"""Prepare or check observed preference evidence without fitting a forecast.

The maintained catalogue includes candidate primaries, original-source pair
counts, mixed exclusion sequences and explicitly verified contexts. Rebuilding
requires the registered commission files in a local source cache. Normal checks
use the compact committed catalogue, so contributors and CI need no downloaded
feed archive. No network access, Stan fitting or C++ computation is performed.
"""

import argparse
import hashlib
import json
from pathlib import Path

from lib.elections.preference_evidence import (
    catalogue_bytes, catalogue_summary, make_catalogue, read_catalogue,
    require, write_catalogue,
)
from lib.elections.preference_source_counts import SourceCache, read_all_counts
from lib.paths import ANALYSIS_DIRECTORY


DATA_DIRECTORY = ANALYSIS_DIRECTORY / 'Preferences'


def maintained_inputs(folder):
    """Read source identities and published-table transcriptions as one input set.

    JSON fingerprints ignore whitespace and platform line endings. These identify
    the source catalogue used to build the evidence, not a future forecast's
    statistical parameters or its live output folder.
    """
    inputs = {}
    hashes = {}
    for name in ['sources.json', 'aec-alternative-tpp-extracts.json']:
        contents = json.loads((folder / name).read_text(encoding='utf-8'))
        require(contents['schema_version'] == 1, f'Unsupported input schema: {name}')
        inputs[name] = contents
        encoded = json.dumps(contents, ensure_ascii=False, sort_keys=True,
                             separators=(',', ':'), allow_nan=False).encode('utf-8')
        hashes[name] = dict(sha256_json=hashlib.sha256(encoded).hexdigest())
    return inputs, hashes


def prepare(source_directory, data_directory=DATA_DIRECTORY):
    """Translate all registered sources, apply explicit contexts and validate them."""
    inputs, hashes = maintained_inputs(data_directory)
    registry = inputs['sources.json']
    cache = SourceCache(source_directory, registry)
    contests = read_all_counts(cache, registry, inputs['aec-alternative-tpp-extracts.json'])
    require(cache.checked == set(registry['files']),
            f'Registered sources not read: {sorted(set(registry["files"]) - cache.checked)}')
    return make_catalogue(contests, registry, hashes)


def check_maintained(catalogue, data_directory=DATA_DIRECTORY):
    """Ensure facts and advice were built from the current maintained inputs."""
    inputs, hashes = maintained_inputs(data_directory)
    require(catalogue['inputs'] == hashes,
            'Preference evidence inputs changed; rebuild the catalogue from the registered cache.')
    registry = inputs['sources.json']
    require(catalogue['sources'] == registry['files']
            and catalogue['publications'] == registry['supplemental_publications']
            and catalogue['exclusion_rules'] == registry['quarantine_rules']
            and catalogue['coverage_limits'] == registry['coverage_limits'],
            'Preference catalogue source metadata differs from the maintained registry.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path,
                        help='local folder containing the registered source artifacts; no downloads')
    parser.add_argument('--data-dir', type=Path, default=DATA_DIRECTORY,
                        help='maintained input/catalogue folder, normally analysis/Preferences')
    operation = parser.add_mutually_exclusive_group()
    operation.add_argument('--write', action='store_true', help='write the validated compact catalogue')
    operation.add_argument('--check', action='store_true', help='check the committed catalogue without writing')
    args = parser.parse_args(argv)
    if args.write and args.source_dir is None:
        parser.error('--write requires --source-dir; source counts are never invented from summaries')
    path = args.data_dir / 'evidence.json.gz'
    try:
        if args.source_dir is not None:
            catalogue = prepare(args.source_dir, args.data_dir)
            if args.write:
                write_catalogue(path, catalogue)
            elif args.check:
                retained = read_catalogue(path)
                check_maintained(retained, args.data_dir)
                require(catalogue_bytes(retained) == catalogue_bytes(catalogue),
                        'Rebuilt preference evidence differs from the retained catalogue.')
        else:
            catalogue = read_catalogue(path)
            check_maintained(catalogue, args.data_dir)
        print(json.dumps(catalogue_summary(catalogue), indent=2, sort_keys=True))
        return 0
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Preference evidence preparation failed: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
