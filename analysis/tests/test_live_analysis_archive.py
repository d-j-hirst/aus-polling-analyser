"""Check the storage contract without retaining election feeds or running forecasts."""

import copy
import gzip
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from lib import live_analysis_archive as archive
from scripts.diagnostics.live_count_revision_audit import load_frame


def packed_sample():
    # An artificial snapshot covers the distinct label placements consumed by
    # the turnout and preference audits, including unlabelled party indexes.
    party = {'party_ref': 0, 'value': 1234567}
    node = {'fp_votes_current': [party], 'tcp_votes_current': [party],
            'tcp_shares_baseline': [], 'tpp_share_baseline': None}
    data = {
        'identities': {
            'parties': {'0': {'name': 'Example Party', 'abbreviation': 'EX'},
                        '-4': {'name': None, 'abbreviation': None}},
            'seats': ['Example Seat'], 'regions': ['Example Region'],
            'booths': [{'seat_ref': 0, 'name': 'Example PPVC', 'coords': [-33.12, 151.12],
                        'vote_type': 'Ordinary', 'booth_type': 'PPVC', 'same_seat': True}],
        },
        'regions': [{'name_region_ref': 0}],
        'seats': [{'name_seat_ref': 0, 'region_name_ref': 0, 'node': copy.deepcopy(node),
                   'live_independent_party_index': 6}],
        'booths': [{'booth_identity_ref': 0, 'node': copy.deepcopy(node)}],
        'turnout': {'units': [{'seat_ref': 0, 'name_booth_ref': 0,
                               'balanced_remaining_before_release': 12345.679}]},
        'preference_rechecking': {'seats': [{'seat_name_ref': 0,
            'booths': [{'booth_name_ref': 0}]}]},
        'unlabelled_index': {'party_index': 91, 'value': 3},
        'unknown_party': {'party_ref': -4},
        'wide_integer': 18446744073709551600,
        'non_finite_example': {'non_finite': 'negative_infinity'},
    }
    # More than 52 fields exercises the multi-character dictionary codes.
    data.update({f'extra_field_{i}': i for i in range(70)})
    fields = set()

    def collect(value):
        if isinstance(value, dict):
            fields.update(value)
            for item in value.values():
                collect(item)
        elif isinstance(value, list):
            for item in value:
                collect(item)
    collect(data)
    fields = sorted(fields)
    codes = {field: archive._field_code(i) for i, field in enumerate(fields)}

    def encode(value):
        if isinstance(value, dict):
            return {codes[key]: encode(item) for key, item in value.items()}
        if isinstance(value, list):
            return [encode(item) for item in value]
        return value
    return {'format': archive.FORMAT, 'version': 1, 'fields': fields, 'data': encode(data)}


class LiveAnalysisArchiveTests(unittest.TestCase):
    def test_restores_labels_metadata_and_all_intermediate_fields(self):
        data = archive.expand_document(packed_sample())
        self.assertNotIn('identities', data)
        self.assertEqual(data['regions'][0]['name'], 'Example Region')
        self.assertEqual(data['seats'][0]['name'], 'Example Seat')
        self.assertEqual(data['seats'][0]['region_name'], 'Example Region')
        booth = data['booths'][0]
        self.assertEqual((booth['name'], booth['seat_name']), ('Example PPVC', 'Example Seat'))
        self.assertEqual(booth['coords'], [-33.12, 151.12])
        party = booth['node']['fp_votes_current'][0]
        self.assertEqual(party, {'party_index': 0, 'name': 'Example Party',
                                 'abbreviation': 'EX', 'value': 1234567})
        self.assertEqual(booth['node']['tcp_shares_baseline'], [])
        self.assertIsNone(booth['node']['tpp_share_baseline'])
        self.assertEqual(data['turnout']['units'][0]['name'], 'Example PPVC')
        self.assertEqual(data['turnout']['units'][0]['seat'], 'Example Seat')
        seat = data['preference_rechecking']['seats'][0]
        self.assertEqual(seat['seat_name'], 'Example Seat')
        self.assertEqual(seat['booths'][0]['booth'], 'Example PPVC')
        self.assertEqual(data['unknown_party'], {'party_index': -4, 'name': None, 'abbreviation': None})
        self.assertEqual(data['unlabelled_index'], {'party_index': 91, 'value': 3})
        self.assertEqual(data['wide_integer'], 18446744073709551600)
        self.assertEqual(data['non_finite_example'], {'non_finite': 'negative_infinity'})
        self.assertEqual(data['extra_field_69'], 69)

    def test_legacy_and_compressed_packed_documents(self):
        with TemporaryDirectory() as temporary:
            folder = Path(temporary)
            plain = folder / 'old.analysis.json'
            plain.write_text(json.dumps({'booths': [], 'count': 123}), encoding='utf-8')
            self.assertEqual(archive.load_json(plain), {'booths': [], 'count': 123})
            compressed = folder / 'new.analysis.json.gz'
            compressed.write_bytes(gzip.compress(json.dumps(packed_sample()).encode()))
            self.assertEqual(archive.load_json(compressed)['wide_integer'], 18446744073709551600)
            compressed.write_bytes(gzip.compress(plain.read_bytes()))
            self.assertEqual(archive.load_json(compressed), archive.load_json(plain))

    def test_mixed_series_paths_prefer_compressed_copy_of_same_run(self):
        with TemporaryDirectory() as temporary:
            folder = Path(temporary)
            names = ('snapshot_1__run_a.analysis.json', 'snapshot_1__run_a.analysis.json.gz',
                     'snapshot_1__run_b.analysis.json', 'snapshot_2__run_a.analysis.json.gz',
                     'snapshot_2__run_a.analysis.json.gz.tmp', 'snapshot_1__run_a.json')
            for name in names:
                (folder / name).touch()
            selected = archive.analysis_files(folder)
            self.assertEqual([p.name for p in selected], [names[1], names[2], names[3]])
            main = folder / names[-1]
            self.assertEqual(archive.analysis_path_for_snapshot(main), folder / names[1])
            self.assertEqual(archive.main_path_for_analysis(folder / names[0]), main)
            self.assertEqual(archive.main_path_for_analysis(folder / names[1]), main)
            self.assertEqual(archive.analysis_path_for_snapshot(folder / 'missing.json'),
                             folder / 'missing.analysis.json')

    def test_explicit_version_and_field_dictionary_are_checked(self):
        sample = packed_sample()
        sample['version'] = 2
        with self.assertRaisesRegex(ValueError, 'version'):
            archive.expand_document(sample)
        sample = packed_sample()
        sample['data']['unknown!'] = 1
        with self.assertRaisesRegex(ValueError, 'field code'):
            archive.expand_document(sample)

    def test_tcp_audit_reads_compressed_packed_sidecar(self):
        with TemporaryDirectory() as temporary:
            folder = Path(temporary)
            main = folder / 'snapshot_1__run_a.json'
            main.write_text(json.dumps({'run': {'snapshot_code': '1', 'term_code': 'example'}}))
            sidecar = folder / 'snapshot_1__run_a.analysis.json.gz'
            sidecar.write_bytes(gzip.compress(json.dumps(packed_sample()).encode()))
            frame = load_frame(sidecar)
            self.assertEqual(frame['source'], '1')
            self.assertEqual(frame['seats']['Example Seat']['fp'], {0: 1234567})
            self.assertEqual(frame['booths'][('Example Seat', 'Example PPVC', 'Ordinary')]['tcp'], {0: 1234567})

    def test_inspection_command_keeps_archive_and_existing_outputs(self):
        with TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source, output = folder / 'sample.analysis.json.gz', folder / 'readable.json'
            content = gzip.compress(json.dumps(packed_sample()).encode())
            source.write_bytes(content)
            archive.main([str(source), '--output', str(output)])
            self.assertEqual(json.loads(output.read_text())['seats'][0]['name'], 'Example Seat')
            self.assertEqual(source.read_bytes(), content)
            with self.assertRaises(FileExistsError):
                archive.main([str(source), '--output', str(output)])
