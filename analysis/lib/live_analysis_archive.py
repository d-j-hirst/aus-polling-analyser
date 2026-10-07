"""Read retained live diagnostics without exposing their storage encoding.

New archives use gzip, shared identity tables and a per-file field dictionary.
Analysis callers receive the same descriptive fields as older plain JSON files.
This module can also expand an archive to a readable JSON file for inspection.
It does not generate forecasts, repair counts or recover rounded-off digits.
"""

import argparse
import gzip
import json
from pathlib import Path


FORMAT = 'polling-analyser-live-analysis'
VERSION = 1
ALPHABET = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'
SUFFIXES = ('.analysis.json.gz', '.analysis.json')


def _field_code(index):
    """Match the exporter's short names using the archive's own ordered table."""
    result = ''
    while True:
        result = ALPHABET[index % len(ALPHABET)] + result
        if index < len(ALPHABET):
            return result
        index = index // len(ALPHABET) - 1


def _expand_fields(value, names):
    # Rename objects in place so reading a large archive does not retain a
    # second complete tree of all intermediate diagnostic calculations.
    if isinstance(value, list):
        for item in value:
            _expand_fields(item, names)
    elif isinstance(value, dict):
        renamed = []
        for code, item in value.items():
            if code not in names:
                raise ValueError(f'Unknown live-analysis field code: {code!r}')
            _expand_fields(item, names)
            renamed.append((names[code], item))
        value.clear()
        value.update(renamed)


def _reference(table, index, kind):
    if isinstance(index, bool) or not isinstance(index, int) or not 0 <= index < len(table):
        raise ValueError(f'Invalid live-analysis {kind} reference: {index!r}')
    return table[index]


def _expand_identities(document):
    """Restore labels and metadata at the places where callers previously found them."""
    catalogs = document.pop('identities')
    seats, regions = catalogs['seats'], catalogs['regions']
    parties, booths = catalogs['parties'], catalogs['booths']
    # Booth identity records use a seat reference too. Expanding that here lets
    # each diagnostic section recover its own original seat/name field names.
    for booth in booths:
        booth['seat_name'] = _reference(seats, booth.pop('seat_ref'), 'seat')

    def visit(value):
        if isinstance(value, list):
            for item in value:
                visit(item)
        elif isinstance(value, dict):
            if 'party_ref' in value:
                index = value.pop('party_ref')
                if isinstance(index, bool) or not isinstance(index, int) or str(index) not in parties:
                    raise ValueError(f'Invalid live-analysis party reference: {index!r}')
                value['party_index'] = index
                value.update(parties[str(index)])
            if 'booth_identity_ref' in value:
                value.update(_reference(booths, value.pop('booth_identity_ref'), 'booth'))
            for reference, field in (('seat_ref', 'seat'), ('seat_name_ref', 'seat_name'),
                                     ('name_seat_ref', 'name')):
                if reference in value:
                    value[field] = _reference(seats, value.pop(reference), 'seat')
            for reference, field in (('region_name_ref', 'region_name'), ('name_region_ref', 'name')):
                if reference in value:
                    value[field] = _reference(regions, value.pop(reference), 'region')
            for reference, field in (('booth_name_ref', 'booth'), ('name_booth_ref', 'name')):
                if reference in value:
                    value[field] = _reference(booths, value.pop(reference), 'booth')['name']
            for item in value.values():
                visit(item)
    visit(document)


def expand_document(document):
    """Accept legacy JSON or expand the explicit, versioned archive envelope."""
    if not isinstance(document, dict) or document.get('format') != FORMAT:
        return document
    if type(document.get('version')) is not int or document.get('version') != VERSION:
        raise ValueError(f"Unsupported live-analysis archive version: {document.get('version')!r}")
    fields = document.get('fields')
    data = document.get('data')
    if not isinstance(fields, list) or not all(isinstance(k, str) for k in fields) or len(set(fields)) != len(fields):
        raise ValueError('Invalid live-analysis field dictionary.')
    if not isinstance(data, dict):
        raise ValueError('Live-analysis archive data must be an object.')
    names = {_field_code(i): name for i, name in enumerate(fields)}
    _expand_fields(data, names)
    _expand_identities(data)
    return data


def loads(payload, *, compressed=False):
    """Decode supplied bytes while allowing callers to hash the original archive."""
    if compressed:
        payload = gzip.decompress(payload)
    return expand_document(json.loads(payload))


def load_json(path):
    """Load a packed gzip archive or an ordinary JSON input through one interface."""
    path = Path(path)
    return loads(path.read_bytes(), compressed=path.suffix == '.gz')


def analysis_path_for_snapshot(path):
    """Prefer the current sidecar; retain the legacy path when neither exists."""
    path = Path(path)
    legacy = path.with_name(path.stem + '.analysis.json')
    compressed = legacy.with_name(legacy.name + '.gz')
    return compressed if compressed.is_file() else legacy


def main_path_for_analysis(path):
    path = Path(path)
    for suffix in SUFFIXES:
        if path.name.endswith(suffix):
            return path.with_name(path.name[:-len(suffix)] + '.json')
    raise ValueError(f'Not a live-analysis sidecar filename: {path.name}')


def analysis_files(directory):
    """List both generations, counting a plain/compressed copy of one run once."""
    files = {}
    for suffix in reversed(SUFFIXES):
        for path in Path(directory).glob('*' + suffix):
            key = path.name[:-3] if path.suffix == '.gz' else path.name
            files[key] = path
    return [files[key] for key in sorted(files)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output', type=Path, required=True,
                        help='New readable JSON file; an existing file is never overwritten.')
    args = parser.parse_args(argv)
    document = load_json(args.archive)
    # Inspection output is explicit and separate from the retained archive.
    # Exclusive creation protects both original exports and earlier inspections.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(document, output, indent=2, ensure_ascii=False, allow_nan=False)
        output.write('\n')
    print(f'Expanded live analysis to {args.output}')


if __name__ == '__main__':
    main()
