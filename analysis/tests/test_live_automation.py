"""Unattended delivery/reporting contract checks with fictional votes and mocked APIs."""
from copy import deepcopy
from contextlib import closing
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import io
import math
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'uploads'))
sys.path.insert(0, str(ROOT/'live_scripts'))
from live_pipeline import activity, benchmark, commentary, commentary_format, comparisons, prompt_layout, trial, reporting_days, rule_commentary, watch
from live_pipeline.run_settings import check_trial_options, load_run
from live_pipeline.vote_type_names import VoteTypeNames
from live_pipeline import booth_fp, booth_tcp, pairing_commentary, rule_evidence, seat_alerts
from live_pipeline.protocol import UploadQueue, Server, DuplicateObservation, replay_manifest
from live_pipeline.storage import atomic, read, fingerprint, run_lock, RunLockBusy
from live_pipeline.worker import Processor, utc
from live_pipeline.mock_server import MockServer
import replay_feeder
import compare_sa_sol


def config():
    return {'version': 1, 'run_id': '2026sa-engineering-test', 'election': '2026sa',
            'kind': 'replay', 'visibility': 'private', 'display_name': 'Fictional SA test',
            'timezone': 'Australia/Adelaide', 'election_date': '2026-03-21',
            'night_end_hour': 6, 'commentary_timeout_seconds': 90}


def numerical(identity='f00000001', source='2026-03-21T18:20:00', chance=55, complete=None):
    report = {'termCode': '2026sa', 'reportMode': 'LF',
        'seatNames': ['Light', 'Hammond', 'Schubert', 'Finniss'],
        'partyAbbr': [[0, 'ALP'], [1, 'LIB'], [2, 'ON'], [3, 'IND']],
        'probabilityBands': [5, 50, 95], 'voteTotalThresholds': [5, 50, 95],
        'seatPartyWinFrequencies': [[[0, chance], [2, 100-chance]]] * 4,
        'seatCandidateNames': [[[0, 'Fictional Labor candidate'], [2, 'Fictional One Nation candidate']]] * 4,
        'seatFpBands': [[[0, [30, 35, 40]], [2, [31, 36, 41]]]] * 4,
        'seatTcpBands': [[[[0, 2], [45, 51, 57]]]] * 4,
        'seatTcpScenarios': [[[[0, 2], 1.0]]] * 4,
        'seatTppBands': [[48, 53, 58]] * 4,
        'expectedSeats': [[0, 2.2], [2, 1.8]], 'majorityWinPc': [[0, 50], [2, 40]],
        'minorityWinPc': [[0, 5], [2, 5]], 'mostSeatsWinPc': [[0, 55], [2, 45]],
        'overallWinPc': [[0, 55], [2, 45]], 'tppFrequencies': [48, 53, 58],
        'countProgress': {'fp_votes': 20000, 'tcp_votes': 10000, 'expected_formal_votes': 50000}}
    return {'version': 1, 'run_id': config()['run_id'], 'forecast_id': identity, 'source_time': source,
            'input_revision': 'inputs-1', 'input_fingerprint': 'config-hash', 'iterations': 1000,
            'delivered_at': utc(), 'completed_at': complete or utc(), 'origin': identity == 'origin',
            'report': report, 'simulation': 'Live Simulation', 'source_fingerprint': 'raw-hash'}


def put(folder, value, ready=True):
    path = folder/'packages'/value['forecast_id']
    atomic(path/'numerical.json', value)
    if ready:
        atomic(path/'ready-numerical.json', {'version': 1, 'forecast_id': value['forecast_id']})
    return path


def fake_response(packet, timeout):
    figure = packet['display_figures'][0]
    seat = figure['seat']
    return {'key_changes': ['The outlook changed in {seat:'+seat+'}.'],
            'developments': [{'seat': seat, 'seat_changes': [figure['candidate']+' now has a win chance of **'+figure['current_with_change']+'**.'], 'booth_evidence': []}],
            'insufficient_evidence': True}, {'input_tokens': 100, 'output_tokens': 80}


def enabled():
    return {'enabled': True, 'model': 'mock', 'spending_limit_usd': 1,
            'input_usd_per_million': 1, 'output_usd_per_million': 2,
            'max_output_tokens': 500, 'concurrency': 4, 'attempts': 1}


def observed(fp, tcp=None, seat='Light'):
    return {'observed_counts_available': True, 'booths': {seat+'/Hall': {
        'seat_name': seat, 'name': 'Hall', 'fp_votes_current': fp,
        'tcp_votes_current': tcp or {}, 'fp_votes_projected': fp}}}


class AutomationTests(unittest.TestCase):
    def assert_same_comparison_figures(self, earlier, current):
        # Newer requests can represent individual raw booth ledgers more
        # compactly while preserving the election reference and every figure.
        def comparison(payload):
            blocks = payload['input'][1]['content']
            window = json.loads(blocks[-1]['text'].split('\n', 1)[1])
            window.pop('evidence')
            return blocks[:-1], window
        self.assertEqual(comparison(earlier), comparison(current))

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        atomic(self.root/'run.json', config())

    def tearDown(self):
        self.temp.cleanup()

    def origin(self):
        put(self.root, numerical('origin', None, 50))

    def test_local_trial_uploads_tied_fp_and_missing_tcp_without_commentary(self):
        cfg = dict(config(), election='2026vic', run_id='2026vic-trial-1', election_date='2026-11-25',
                   timezone='Australia/Melbourne', local_trial=True, commentary={'enabled': False})
        atomic(self.root/'run.json', cfg)
        for identity, source, count in [('origin', None, 0), ('f00000001', '2026-11-25T17:30:01', 0),
                                        ('f00000002', '2026-11-25T18:00:01', 60000)]:
            handoff = numerical(identity, source, 50)
            handoff['run_id'] = cfg['run_id']
            handoff['report'].update(termCode='2026vic', seatNames=['Trial district'],
                                     countProgress={'fp_votes': count, 'tcp_votes': 0, 'expected_formal_votes': 50000})
            put(self.root, handoff)
        def evidence(handoff, folder):
            votes = handoff['report']['countProgress']['fp_votes'] // 2
            return observed({'0': votes, '2': votes}, seat='Trial district')
        with patch.object(comparisons, 'evidence', side_effect=evidence), MockServer() as local, \
                patch.dict(watch.DESTINATIONS, local=local.url), \
                patch.object(comparisons, 'prompt_packet', side_effect=AssertionError('prepared commentary evidence')):
            watch.run(self.root, ['local'], once=True)
            delivery = read(self.root/'receipts/local.json')['forecasts']['f00000002']
            self.assertTrue(delivery['accepted'], delivery.get('error'))
            pub = local.packages[(cfg['run_id'], 'f00000002')]
            self.assertEqual(local.latest[cfg['run_id']], 'f00000002')
            self.assertEqual(pub['run']['visibility'], 'private')
            self.assertNotIn('local_trial', pub['run'])
            for key in ('commentary', 'commentary_requests', 'commentary_evidence'):
                self.assertEqual(pub[key], {})
            self.assertTrue(pub['comparisons']['previous']['available'])
            prepared = read(self.root/'packages/f00000002/comparisons.json')
            for windows in (prepared, pub['comparisons']):
                self.assertNotIn('seat_changes', windows['previous'])
                self.assertIn('win_changes', windows['previous'])
            self.assertEqual(pub['report']['countProgress']['fp_votes'], 60000)
        self.assertFalse((self.root/'commentary-budget.sqlite').exists())

    def test_local_trial_refuses_external_destinations_and_commentary_overrides(self):
        cfg = dict(config(), local_trial=True)
        atomic(self.root/'run.json', cfg)
        preset = self.root/'paid.json'
        atomic(preset, enabled())
        with patch.object(watch, 'Server', side_effect=AssertionError('contacted server')):
            for destinations in (['local', 'test', 'remote'], ['test'], ['remote']):
                with self.subTest(destinations=destinations), self.assertRaisesRegex(ValueError, 'only.*--local'):
                    watch.run(self.root, destinations, once=True)
            with self.assertRaisesRegex(ValueError, 'Commentary must stay off'):
                watch.run(self.root, ['local'], preset, once=True)
        with self.assertRaisesRegex(ValueError, 'private replay'):
            check_trial_options(dict(cfg, kind='live'), ['local'])
        with self.assertRaisesRegex(ValueError, 'private replay'):
            check_trial_options(dict(cfg, visibility='public'), ['local'])
        with self.assertRaisesRegex(ValueError, 'Commentary must stay off'):
            check_trial_options(dict(cfg, commentary={'compare_writers': True}), ['local'])
        with self.assertRaisesRegex(ValueError, 'Commentary must stay off'):
            check_trial_options(dict(cfg, commentary={'rules_only': True}), ['local'])

    def test_official_rules_are_primary_and_all_destinations_receive_the_same_report(self):
        cfg = dict(config(), run_id='2026vic-live', election='2026vic', kind='live', visibility='public',
                   election_date='2026-11-28', timezone='Australia/Melbourne',
                   commentary={'enabled': False, 'rules_only': True})
        atomic(self.root/'run.json', cfg)
        for identity, source, chance, votes in [('origin', None, 50, 0),
                ('f00000001', '2026-11-28T18:00:01', 50, 0),
                ('f00000002', '2026-11-28T18:05:01', 72, 340)]:
            handoff = numerical(identity, source, chance)
            handoff['run_id'] = cfg['run_id']
            handoff['report'].update(termCode=cfg['election'], seatNames=['Richmond', 'Melton', 'Mildura', 'Croydon'],
                                     countProgress={'fp_votes': votes, 'tcp_votes': 0, 'expected_formal_votes': 50000})
            put(self.root, handoff)

        def evidence(handoff, folder):
            votes = handoff['report']['countProgress']['fp_votes']
            return observed({'0': votes//2, '2': votes//2}, seat='Richmond')

        with MockServer() as local, MockServer() as test, MockServer() as production, \
                patch.dict(watch.DESTINATIONS, local=local.url, test=test.url, remote=production.url), \
                patch.object(comparisons, 'evidence', side_effect=evidence), \
                patch.object(Processor, 'request', side_effect=AssertionError('started an AI request')), \
                patch.object(commentary, 'Budget', side_effect=AssertionError('opened a paid budget')):
            watch.run(self.root, ['local', 'test', 'remote'], once=True)
            key = cfg['run_id'], 'f00000002'
            for name in ('local', 'test', 'remote'):
                receipt = read(self.root/'receipts'/f'{name}.json')['forecasts'][key[1]]
                self.assertTrue(receipt['accepted'], receipt.get('error'))
            publication = local.packages[key]
            self.assertEqual(publication, test.packages[key])
            self.assertEqual(publication, production.packages[key])
            self.assertEqual(publication['run']['kind'], 'live')
            self.assertTrue(publication['comparisons']['previous']['available'])
            self.assertEqual(publication['comparisons']['previous']['commentary_decision'], 'rules_based')
            window = publication['commentary']['previous']
            self.assertEqual((window['status'], window['source'], window['kind'], window['format']),
                             ('complete', 'local', 'rules_based', 'seat-bullets-v1'))
            self.assertTrue(window['text']['summary'])
            self.assertNotIn(rule_commentary.MARKER, str(window))
            self.assertEqual(publication['commentary']['1h']['status'], 'unavailable')
            self.assertEqual(publication['commentary_requests'], {})
            self.assertEqual(publication['commentary_evidence'], {})
            quiet = local.packages[(key[0], 'f00000001')]['commentary']['previous']
            self.assertEqual(quiet['kind'], 'no_new_counting')
        for conflicting in ({'enabled': True, 'rules_only': True}, {'compare_writers': True, 'rules_only': True}):
            with self.assertRaisesRegex(ValueError, 'rules_only'):
                Processor(self.root, conflicting)

    def test_seat_calls_survive_restart_and_are_published_even_without_ai(self):
        self.origin()
        captures = [
            ('f00000001', 100, 0),       # No local votes anywhere.
            ('f00000002', 99.995, 200),  # Exactly the opening threshold.
            ('f00000003', 99.9975, 201),
            ('f00000004', 99.0, 202),   # Exactly the retention threshold.
            ('f00000005', 98.99, 202),  # Even a quiet window carries a withdrawal.
        ]
        counts = {identity: count for identity, _, count in captures}
        def evidence(handoff, folder):
            return observed({'0': counts.get(handoff['forecast_id'], 0)})
        results = []
        with patch.object(comparisons, 'evidence', side_effect=evidence):
            for i, (identity, chance, _) in enumerate(captures):
                package = put(self.root, numerical(identity, f'2026-03-21T19:0{i}:00', chance))
                # Starting a fresh worker between snapshots also exercises recovery
                # from the saved summaries and publication readiness markers.
                worker = Processor(self.root)
                try:
                    worker.tick()
                finally:
                    worker.close()
                results.append(read(package/'publication.json'))
        called = {'Light': {'party': '0', 'since': 'f00000003'}}
        self.assertEqual([r['report'].get('seatCalls', {}) for r in results], [{}, {}, called, called, {}])
        for i, action in ((2, 'called'), (4, 'withdrawn')):
            section = [dict(action=action, party='0', seats=['Light'], others=0)]
            self.assertEqual(results[i]['commentary']['previous']['seat_call_changes'], section)
            self.assertEqual(results[i]['comparisons']['previous']['seat_call_changes'], section)
        self.assertNotIn('seat_call_changes', results[-1]['commentary']['since_start'])
        self.assertNotIn('seat_call_changes', results[-1]['comparisons']['since_start'])

    def test_long_call_bulletin_names_every_latest_call_and_groups_earlier_ones(self):
        def record(party, n):
            return {'party': party, 'since': f'f{n:08}'}
        old = {'seat_calls': {'Retained': record('1', 1), 'Withdrawn': record('2', 2)}}
        calls = {'Retained': record('1', 1), 'New': record('0', 9),
                 **{f'Older {i}': record(str(i % 2), i) for i in range(3, 9)}}
        current = {'forecast_id': 'f00000009', 'seat_calls': calls}
        groups = seat_alerts.call_changes(old, current)
        by_party = {(row['action'], row['party']): row for row in groups}
        self.assertEqual(by_party['called', '0']['seats'], ['New', 'Older 8', 'Older 6', 'Older 4'])
        self.assertEqual(by_party['called', '1']['seats'], ['Older 7', 'Older 5', 'Older 3'])
        self.assertEqual([by_party['called', party]['others'] for party in ('0', '1')], [0, 0])
        self.assertEqual(by_party['withdrawn', '2']['seats'], ['Withdrawn'])
        calls.update({f'New {i}': record('1', 9) for i in range(6)})
        groups = seat_alerts.call_changes(old, current)
        newly_named = [seat for row in groups if row['action'] == 'called' for seat in row['seats']]
        self.assertEqual(len(newly_named), 10)
        self.assertEqual(next(g for g in groups if g['party'] == '1')['others'], 3)
        self.assertEqual(sum(s.startswith('New') for s in newly_named), 7)

    def test_call_can_reopen_for_the_same_party_or_switch_on_a_tcp_only_count(self):
        before = {'Light': {'party': '0', 'since': 'f00000001'}}
        current = {'forecast_id': 'f00000004', 'seats': {'Light': {'win_percent': {'1': 100}}}}
        evidence = observed({}, {'0': 12, '1': 18})
        current['seat_calls'] = seat_alerts.active_calls(current, evidence, before)
        changes = seat_alerts.call_changes({'seat_calls': before}, current)
        self.assertEqual([(row['action'], row['party']) for row in changes], [('called', '1'), ('withdrawn', '0')])
        current['seats']['Light']['win_percent'] = {'0': 100}
        current['seat_calls'] = seat_alerts.active_calls(current, evidence, {})
        self.assertEqual(current['seat_calls']['Light']['since'], 'f00000004')
        self.assertEqual(seat_alerts.call_changes({'seat_calls': before}, current),
                         [dict(action='called', party='0', seats=['Light'], others=0)])

    def test_booth_warning_bands_use_only_the_discrepancy_driven_probability(self):
        probabilities = [None, .4999999, .5, .7999999, .8, .98, .9800001, 1.]
        diagnostics = {'preference_rechecking': {'seats': [
            {'seat': 'Close', 'booths': [dict(booth=str(i), routine_probability=.99, broad_probability=p)
                                        for i, p in enumerate(probabilities)]},
            {'seat': 'Safe', 'booths': [dict(booth='Hall', broad_probability=1.)]}]}}
        warnings = seat_alerts.booth_warnings(diagnostics)
        levels = {row['booth']: row['level'] for row in warnings['Close']}
        self.assertEqual(levels, {'2': 'low', '3': 'low', '4': 'medium', '5': 'medium', '6': 'high', '7': 'high'})
        earlier = {'booth_warnings': {'Close': [dict(booth='7', level='high')]}}
        current = {'booth_warnings': warnings, 'seats': {
            'Close': {'win_percent': {'0': 52, '1': 48}}, 'Safe': {'win_percent': {'0': 99}}}}
        selected = seat_alerts.noteworthy_warnings(earlier, current)
        self.assertEqual([(row['seat'], row['booth']) for row in selected], [('Close', '6'), ('Close', '5')])

    def test_both_writers_receive_the_current_booth_warning(self):
        self.origin()
        path = put(self.root, numerical())
        warning = dict(booth='Hall', level='medium', confidencePercent=84.2)
        def evidence(handoff, folder):
            if handoff['origin']:
                return observed({})
            return dict(observed({'0': 150}), booth_warnings={'Light': [warning]})
        with patch.object(comparisons, 'evidence', side_effect=evidence):
            with closing(Processor(self.root, {'enabled': False, 'compare_writers': True})) as worker:
                worker.tick()
        publication = read(path/'publication.json')
        self.assertEqual(publication['report']['possibleBoothErrors'], {'Light': [warning]})
        packet = read(path/'requests.json')['previous']['packet']
        self.assertEqual(packet['possible_booth_errors'], [dict(warning, seat='Light')])
        rule_text = publication['commentary']['previous']['text']['developments'][0]['text']
        self.assertIn('reported preference split at Hall looks unusual', rule_text)
        self.assertIn('**84.2%**', rule_text)

    def test_locked_gui_status_retries_without_delaying_forecast_release(self):
        self.origin()
        status_file = self.root/'reporting-status.json'
        atomic(status_file, {'previous': True})
        worker = Processor(self.root)
        def locked_status(path, value):
            if path == status_file:
                raise PermissionError('Windows reader still has the file open')
            atomic(path, value)
        with patch('live_pipeline.worker.atomic', side_effect=locked_status), patch('sys.stdout', new_callable=io.StringIO) as log:
            worker.tick()
            worker.tick()
            self.assertEqual(log.getvalue().count('Could not update'), 1)
            self.assertTrue((self.root/'packages/origin/ready-publication.json').exists())
            self.assertEqual(read(status_file), {'previous': True})
        with patch('live_pipeline.worker.atomic', wraps=atomic) as writes:
            worker.tick()
            worker.tick()
            self.assertEqual(writes.call_count, 1)
        self.assertEqual(read(status_file)['packages']['origin'], 'released')
        worker.close()

    def test_locked_gui_status_preserves_underlying_reporting_error(self):
        def fail_writes(path, value):
            # The watcher resolves the run folder. Windows can give the same
            # temp directory a different spelling, so match the two filenames.
            if path.name == 'reporting-status.json':
                raise PermissionError('status locked')
            if path.name == 'evidence.json.gz':
                raise OSError('evidence storage unavailable')
            return atomic(path, value)
        self.origin()
        with patch('live_pipeline.worker.atomic', side_effect=fail_writes), patch('sys.stdout', new_callable=io.StringIO) as log:
            with self.assertRaisesRegex(OSError, 'evidence storage unavailable'):
                watch.run(self.root, [], once=True, prepare_only=True)
        self.assertIn('status locked (reporting error: evidence storage unavailable)', log.getvalue())
        self.assertFalse((self.root/'packages/origin/ready-publication.json').exists())

    def test_watch_idle_deadline_resets_only_for_a_new_ready_forecast(self):
        # Start at hour zero. A handoff in hour 13 extends this watch to hour
        # 27. Touching its marker and writing a partial package do not extend it.
        hour = 0
        def advance(_):
            nonlocal hour
            hour += 1
            self.assertLessEqual(hour, 27)
            if hour == 13:
                self.origin()
            elif hour == 15:
                (self.root/'packages/origin/ready-numerical.json').touch()
                put(self.root, numerical(), ready=False)
        with patch.object(watch, 'time') as clock, patch('sys.stdout', new_callable=io.StringIO) as log:
            clock.monotonic.side_effect = lambda: hour * 3600
            clock.sleep.side_effect = advance
            watch.run(self.root, [], prepare_only=True)
        self.assertEqual(hour, 27)
        self.assertIn('14 hours without a new forecast', log.getvalue())
        self.assertTrue((self.root/'packages/origin/ready-publication.json').is_file())
        with run_lock(self.root):
            pass

    def test_watch_idle_deadline_can_be_disabled(self):
        with patch.object(watch, 'time') as clock, patch('sys.stdout', new_callable=io.StringIO) as log:
            clock.monotonic.side_effect = [0, 24 * 3600, 48 * 3600]
            clock.sleep.side_effect = [None, KeyboardInterrupt()]
            watch.run(self.root, [], prepare_only=True, idle_hours=0)
        self.assertEqual(clock.sleep.call_count, 2)
        self.assertNotIn('hours without a new forecast', log.getvalue())

    def test_failed_upload_retries_still_allow_idle_shutdown(self):
        self.origin()
        failed = Mock()
        failed.done.return_value = True
        failed.result.side_effect = OSError('server unavailable')
        with patch.object(watch, 'ThreadPoolExecutor') as executor, patch.object(watch, 'time') as clock:
            executor.return_value.submit.return_value = failed
            clock.monotonic.side_effect = [0, 0, 0, 3600, 7200]
            watch.run(self.root, ['local'], idle_hours=2)
        self.assertEqual(executor.return_value.submit.call_count, 2)
        executor.return_value.shutdown.assert_called_once_with(wait=True)
        self.assertEqual(read(self.root/'receipts/local-error.json')['error'], 'server unavailable')

    def test_named_replay_resolves_saved_inbox_and_commentary(self):
        run = config()
        name = run['run_id']
        folder = self.root/'live_runs'/name
        inbox = folder/'inbox'
        run.update(source_directory=f'live_runs/{name}/inbox', commentary=enabled())
        atomic(folder/'run.json', run)
        self.assertEqual(load_run(self.root, name), (folder, run))
        self.assertEqual(load_run(self.root, str(folder), allow_path=True), (folder, run))
        with self.assertRaisesRegex(ValueError, 'No saved run'):
            load_run(self.root, 'unknown')
        source = self.root/'forecasts/2026sa/live-snapshots/feeds'
        source.mkdir(parents=True)
        capture = source/'el2026260321183853.xml'
        capture.write_text('<fixture/>', encoding='utf-8')
        with patch.object(replay_feeder, 'WORKSPACE', self.root):
            replay_feeder.main(['--run-id', name, '--step'])
        self.assertEqual((inbox/capture.name).read_bytes(), capture.read_bytes())
        with patch.object(watch, 'Processor', wraps=Processor) as processor:
            watch.run(folder, [], once=True)
            self.assertEqual(processor.call_args.args[1], run['commentary'])
            watch.run(folder, [], once=True, prepare_only=True)
            self.assertIsNone(processor.call_args.args[1])
            override = self.root/'override.json'
            atomic(override, {'enabled': False})
            watch.run(folder, [], settings_path=override, once=True)
            self.assertEqual(processor.call_args.args[1], {'enabled': False})

    def test_preset_edit_does_not_reuse_a_prepared_request_for_another_model(self):
        self.origin()
        package = put(self.root, numerical())
        original = Processor(self.root, enabled())
        try:
            with patch.object(comparisons, 'evidence', side_effect=lambda value, folder:
                              observed({'0': 0 if value['origin'] else 100})):
                with patch.object(original, 'request') as request:
                    original.tick()
                    self.assertTrue(request.called)
            self.assertFalse((package/'ready-publication.json').exists())
        finally:
            original.close()
        updated = Processor(self.root, dict(enabled(), model='another-model'))
        try:
            with patch.object(updated, 'request') as request:
                updated.tick()
                request.assert_not_called()
            commentary_status = read(package/'publication.json')['commentary']['previous']
            self.assertEqual(commentary_status['status'], 'unavailable')
            self.assertIn('settings changed', commentary_status['reason'])
        finally:
            updated.close()

    @unittest.skipIf(sys.platform == 'win32', 'A POSIX worker consumes this Windows handoff')
    def test_windows_diagnostic_in_a_wsl_run(self):
        handoff = numerical()
        run_id = handoff['run_id']
        handoff['diagnostic_file'] = rf'C:\saved project\live_runs\{run_id}\snapshot_1.json'
        atomic(self.root/'snapshot_1.analysis.json.gz', {'booths': [], 'seats': []})
        package = put(self.root, handoff)
        worker = Processor(self.root)
        try:
            worker.prepare([package/'ready-numerical.json'])
        finally:
            worker.close()
        self.assertTrue(read(package/'evidence.json.gz')['observed_counts_available'])
        handoff['diagnostic_file'] = r'C:\saved project\live_runs\other-replay\snapshot_1.json'
        with self.assertRaisesRegex(ValueError, 'outside this forecast run'):
            comparisons.evidence(handoff, self.root)

    def test_windows_identity_units_and_revision(self):
        values = [numerical('origin', None, 50),
                  numerical('f00000001', '2026-03-21T18:00:00', 70),
                  numerical('f00000002', '2026-03-21T18:07:00', 90),
                  numerical('f00000003', '2026-03-21T18:29:00', 55),
                  numerical('f00000004', '2026-03-21T18:29:00', 1)]
        values[3]['input_revision'] = 'inputs-2'
        summaries = list(map(comparisons.summary, values))
        result = comparisons.comparisons(summaries[3], summaries, lambda _: {}, config())
        self.assertEqual(result['previous']['reference_forecast_id'], 'f00000002')
        self.assertEqual(result['15m']['reference_forecast_id'], 'f00000002')
        self.assertEqual(result['15m']['actual_seconds'], 22*60)
        self.assertFalse(result['1h']['available'])
        self.assertEqual(result['since_start']['reference_forecast_id'], 'origin')
        self.assertIsNone(result['since_start']['actual_seconds'])
        self.assertTrue(result['since_start']['input_revision_changed'])
        self.assertEqual(summaries[3]['seats']['Light']['pairing_percent']['0/2'], 100)
        self.assertEqual(comparisons.bands([45, 50, 55], [5.0, 50.0, 95.0]), {'5': 45, '50': 50, '95': 55})
        row = next(r for r in result['since_start']['win_changes'] if r['seat'] == 'Light' and r['party_id'] == '0')
        self.assertEqual(row['maximum_within_window'], 90)
        self.assertEqual(row['change_pp'], 5)
        self.assertEqual(row['minimum_within_window'], 50) # Future same-time run excluded.
        morning = comparisons.summary(numerical('f00000005', '2026-03-22T06:00:00'))
        after = comparisons.comparisons(morning, summaries, lambda _: {}, config())
        self.assertNotIn('3h', after)
        self.assertNotIn('24h', after)
        self.assertIn('today', after)
        self.assertNotIn('15m', after)
        self.assertEqual(comparisons.instant('2026-03-22T06:00:00', 'Australia/Adelaide').isoformat(),
                         '2026-03-21T19:30:00+00:00')

    def test_partial_handoff_is_not_published(self):
        self.origin()
        path = put(self.root, numerical(), ready=False)
        worker = Processor(self.root)
        worker.tick()
        self.assertFalse((path/'publication.json').exists())
        atomic(path/'ready-numerical.json', {'forecast_id': path.name})
        worker.tick()
        payload = read(path/'publication.json')
        self.assertTrue(payload['frozen'])
        self.assertEqual(payload['run']['kind'], 'replay')
        self.assertNotIn('diagnostic_file', json.dumps(payload))
        self.assertEqual(payload['commentary']['previous']['status'], 'unavailable')
        worker.close()

    def test_today_anchor_and_afternoon_gate_use_sydney_not_feed_clock(self):
        # SA's 04:30 is Sydney's 05:00 in March. A capture on the boundary
        # belongs to today, while its starting point remains the 04:20 capture.
        times = ['2026-03-23T04:20:00', '2026-03-23T04:30:00',
                 '2026-03-23T06:30:00', '2026-03-23T09:29:00',
                 '2026-03-23T09:30:00', '2026-03-23T12:29:59', '2026-03-23T12:30:00']
        summaries = [comparisons.summary(numerical('origin', None))]+[
            comparisons.summary(numerical(f'f{i:08}', t)) for i, t in enumerate(times, 1)]
        for current in summaries[2:]:
            groups = comparisons.comparisons(current, summaries, lambda _: {}, config())
            self.assertEqual(groups['today']['reference_forecast_id'], 'f00000001')
            self.assertEqual(groups['today']['requested_reference_time'], '2026-03-23T05:00:00+11:00')
            self.assertEqual(groups['today']['report_date'], '2026-03-23')
            self.assertNotIn('24h', groups)
        morning = comparisons.comparisons(summaries[-2], summaries, lambda _: {}, config())
        self.assertEqual(set(morning), {'previous', 'since_start', 'today'})
        afternoon = comparisons.comparisons(summaries[-1], summaries, lambda _: {}, config())
        self.assertEqual(afternoon['3h']['reference_forecast_id'], 'f00000005')
        self.assertEqual(afternoon['3h']['actual_seconds'], 10800)
        self.assertNotEqual(afternoon['3h']['reference_forecast_id'], afternoon['today']['reference_forecast_id'])
        # If the archive starts later, neither origin nor a future capture can
        # silently replace the missing daily reference.
        missing = comparisons.comparisons(summaries[-2], [summaries[0], summaries[-2], summaries[-1]], lambda _: {}, config())
        self.assertFalse(missing['today']['available'])
        self.assertEqual(missing['today']['report_date'], '2026-03-23')

    def test_reporting_days_rollover_dst_and_configurable_hours(self):
        before = reporting_days.period(comparisons.instant('2026-03-24T04:29:59', 'Australia/Adelaide'), config())
        after = reporting_days.period(comparisons.instant('2026-03-24T04:30:00', 'Australia/Adelaide'), config())
        self.assertEqual(before['report_date'], '2026-03-23')
        self.assertEqual(after['report_date'], '2026-03-24')
        self.assertFalse(before['three_hour_enabled'])
        for date, offset, hours in [('2026-04-05', '+10:00', 25), ('2026-10-04', '+11:00', 23)]:
            day = reporting_days.period(datetime.fromisoformat(date+'T08:00:00'+offset), config())
            difference = day['start'].astimezone(timezone.utc)-day['previous_start'].astimezone(timezone.utc)
            self.assertEqual(difference.total_seconds(), hours*3600)
        custom = dict(config(), reporting_timezone='Australia/Perth', day_start_hour=4, three_hour_start_hour=12)
        self.assertTrue(reporting_days.period(datetime.fromisoformat('2026-03-23T12:00:00+08:00'), custom)['three_hour_enabled'])
        with self.assertRaises(ValueError):
            reporting_days.schedule(dict(config(), three_hour_start_hour=2))

    def test_yesterday_reuses_night_and_daily_reports_without_api_calls(self):
        self.origin()
        times = ['2026-03-21T23:00:00', '2026-03-22T06:00:00', '2026-03-22T22:00:00',
                 '2026-03-23T06:00:00', '2026-03-23T11:00:00']
        paths = [put(self.root, numerical(f'f{i:08}', t)) for i, t in enumerate(times, 1)]
        with patch('live_pipeline.comparisons.evidence', return_value=observed({'a': 0})):
            worker = Processor(self.root, enabled(), lambda *_: self.fail('No counting means no requests'))
            worker.tick(); worker.close()
        night = read(paths[1]/'publication.json')['retained_reports']['yesterday']
        self.assertTrue(night['available'])
        self.assertEqual(night['source_forecast_id'], 'f00000001')
        self.assertEqual(night['source_window'], 'since_start')
        first = read(paths[3]/'publication.json')['retained_reports']['yesterday']
        second = read(paths[4]/'publication.json')['retained_reports']['yesterday']
        self.assertEqual(first, second)
        self.assertEqual(first['source_forecast_id'], 'f00000003')
        self.assertEqual(first['reference_forecast_id'], 'f00000001')
        self.assertEqual(first['source_window'], 'today')
        self.assertEqual(first['commentary_status'], 'complete')
        self.assertEqual(read(paths[4]/'requests.json'), {})
        frozen = (paths[4]/'publication.json').read_bytes()
        restarted = Processor(self.root, enabled(), lambda *_: self.fail('No repeated calls'))
        restarted.tick(); restarted.close()
        self.assertEqual(frozen, (paths[4]/'publication.json').read_bytes())
        # A day without forecasts cannot be replaced by an older day's report.
        late = put(self.root, numerical('f00000006', '2026-03-25T07:00:00'))
        with patch('live_pipeline.comparisons.evidence', return_value=observed({'a': 0})):
            resumed = Processor(self.root); resumed.tick(); resumed.close()
        self.assertFalse(read(late/'publication.json')['retained_reports']['yesterday']['available'])

    def test_yesterday_selection_is_fixed_with_pending_and_late_reports(self):
        early = numerical('f00000001', '2026-03-22T21:00:00')
        chosen = numerical('f00000002', '2026-03-22T22:00:00')
        current = numerical('f00000003', '2026-03-23T08:00:00')
        summaries = [comparisons.summary(v) for v in (early, chosen, current)]
        waiting = reporting_days.yesterday(self.root, current, summaries, config())
        self.assertFalse(waiting['available'])
        self.assertEqual(waiting['source_forecast_id'], 'f00000002')
        # Only the selected forecast becoming ready makes it available; there
        # is no fallback to an earlier report or a newly generated explanation.
        package = self.root/'packages'/'f00000002'
        group = {'available': True, 'reference_forecast_id': 'f00000001',
                 'reference_source_time': early['source_time'], 'actual_seconds': 3600}
        atomic(package/'publication.json', {'comparisons': {'today': group},
               'commentary': {'today': {'status': 'unavailable', 'reason': 'Deadline'}}})
        atomic(package/'ready-publication.json', {})
        late = comparisons.summary(numerical('f00000004', '2026-03-22T23:00:00'))
        newer = numerical('f00000005', '2026-03-23T09:00:00')
        retained = reporting_days.yesterday(self.root, newer, summaries+[late], config())
        self.assertTrue(retained['available'])
        self.assertEqual(retained['source_forecast_id'], 'f00000002')
        self.assertEqual(retained['commentary_status'], 'unavailable')
        # An earlier package finishing late must not use a future selection.
        older = numerical('f00000002', '2026-03-23T06:00:00')
        old = reporting_days.yesterday(self.root, older, summaries, config())
        self.assertEqual(old['source_forecast_id'], 'f00000001')

    def test_shared_request_and_restart_reuses_result(self):
        self.origin()
        path = put(self.root, numerical())
        calls = []
        def client(packet, timeout):
            calls.append(packet)
            return fake_response(packet, timeout)
        worker = Processor(self.root, enabled(), client)
        with patch('sys.stdout', new_callable=io.StringIO) as console:
            worker.tick()
            worker.close()
            worker.tick()
            worker.tick()
        self.assertEqual(console.getvalue().count('Received forecast '), 2)  # Reference and forecast.
        self.assertRegex(console.getvalue(), r'Received forecast 2026sa-engineering-test/f00000001 at \S+ '
                                             r'\(source 2026-03-21T18:20:00\)\.')
        self.assertEqual(console.getvalue().count('First request to last response:'), 1)
        self.assertIn('1/1 distinct comparisons completed.', console.getvalue())
        self.assertEqual(len(calls), 1) # previous == since_start
        saved_input = read(next((path/'commentary').glob('*.input.json')))
        self.assertEqual(saved_input['input'], commentary.prompt_messages(calls[0], dict(enabled(), cache_overview=False)))
        payload = read(path/'publication.json')
        self.assertEqual(payload['commentary']['previous']['status'], 'complete')
        self.assertNotIn('request_timing', payload['commentary']['previous'])
        before = (path/'publication.json').read_bytes()
        restarted = Processor(self.root, enabled(), client)
        with patch('sys.stdout', new_callable=io.StringIO) as console:
            restarted.tick(); restarted.close()
        self.assertEqual(console.getvalue(), '')
        self.assertEqual(before, (path/'publication.json').read_bytes())
        self.assertEqual(len(calls), 1)

    def test_two_writers_share_one_request_and_keep_the_original_publication_identities(self):
        self.origin()
        path = put(self.root, numerical())
        settings = dict(enabled(), compare_writers=True)
        with patch('live_pipeline.rule_commentary.write', wraps=rule_commentary.write) as writer:
            with patch('sys.stdout', new_callable=io.StringIO):
                with closing(Processor(self.root, settings, fake_response)) as worker:
                    worker.tick()
                worker.tick()
        writer.assert_called_once()
        requests = read(path/'requests.json')
        key = requests['previous']['key']
        self.assertEqual(key, requests['since_start']['key'])
        self.assertEqual(key, fingerprint({'packet': requests['previous']['packet'], 'settings': enabled(),
                                           'prompt': commentary.PROMPT_VERSION}))
        receipt = read(path/'commentary'/f'{key}.json')
        primary = receipt['text']
        payload = read(path/'publication.json')
        item = payload['commentary']['previous']
        self.assertEqual('complete', item['status'])
        self.assertEqual(item, payload['commentary']['since_start'])
        self.assertEqual(primary['summary'], item['text']['summary'])
        original_count = len(primary['developments'])
        rows = item['text']['developments']
        self.assertEqual(primary['developments'], rows[:original_count])
        self.assertEqual([], rows[original_count]['seats'])
        self.assertTrue(rows[original_count]['text'].startswith(rule_commentary.MARKER+'\nKey changes:\n- '))
        self.assertNotIn(rule_commentary.MARKER, str(receipt))
        self.assertNotIn(rule_commentary.MARKER, str(payload['commentary']['1h']))
        self.assertTrue(all(token in item['entities'] for token in commentary_format.TOKEN.findall(str(rows))))
        frozen = (path/'publication.json').read_bytes()
        with closing(Processor(self.root, enabled(), self.fail)) as restarted:
            restarted.tick()
        self.assertEqual((path/'publication.json').read_bytes(), frozen)

    def test_rule_report_is_available_at_ai_deadline_but_no_extra_report_for_quiet_counts(self):
        self.origin()
        path = put(self.root, numerical(complete='2026-01-01T00:00:00+00:00'))
        with closing(Processor(self.root, dict(enabled(), compare_writers=True), self.fail)) as worker:
            worker.tick()
        item = read(path/'publication.json')['commentary']['previous']
        self.assertEqual(item['status'], 'unavailable')
        self.assertIn('deadline', item['reason'].lower())
        self.assertTrue(item['text']['developments'][0]['text'].startswith(rule_commentary.MARKER+'\n'))
        self.assertFalse((path/'commentary').exists())
        shared = {'previous': activity.notice({'commentary_decision':'no_new_counting'}, 'previous')}
        self.assertEqual(shared, rule_commentary.append_editions(shared, {}, 'quiet'))

    def test_quiet_rule_report_needs_a_substantial_matched_local_count(self):
        before = comparisons.summary(numerical('f00000001', chance=98.5))
        after = comparisons.summary(numerical('f00000002', chance=99.25))
        earlier = observed({'0': 200, '2': 200})

        def comparison(added):
            current = observed({'0': 200+added, '2': 200})
            return comparisons.prompt_packet(after, comparisons.compare(after, before, [], current, earlier))

        packet = comparison(100)
        summary, sections, _ = rule_commentary.write(packet)
        self.assertIn('In {seat:Light}', summary)
        self.assertIn('{candidate:Light:0}', summary)
        self.assertIn('At Hall', summary)
        self.assertEqual(summary.count('\n- '), 1)
        self.assertEqual(sections, [])
        # An administrative story already gives readers a substantive headline.
        packet['category_transfers'] = [{'seat': 'Hammond', 'from': 'Absent', 'to': 'Postal',
                                         'fp_votes': 300, 'tcp_votes': 300}]
        self.assertNotIn('Hall', rule_commentary.write(packet)[0])
        self.assertNotIn('Hall', rule_commentary.write(comparison(99))[0])
        packet = comparison(99)
        shared = dict(key='same-pair', packet=packet)
        reports = rule_commentary.append_editions({}, {'previous': shared, 'since_start': shared},
                                                  'f00000002', rules_only=True)
        for window, report in reports.items():
            minor = activity.notice({'commentary_decision': 'minor_changes'}, window)
            self.assertEqual(report['text']['summary'], minor['text']['summary'])
            self.assertEqual(report['format'], minor['format'])
        earlier['booths']['Light/Hall']['fp_votes_projected'] = {}
        self.assertNotIn('Hall', rule_commentary.write(comparison(100))[0])

    def test_quiet_first_booth_names_the_count_even_when_another_seat_moves_most(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = deepcopy(old)
        new['forecast_id'] = 'later'
        new['statewide']['countProgress'] = dict(fp_votes=62, tcp_votes=0, expected_formal_votes=50000)
        new['seats']['Hammond']['win_percent'] = {'0':50.54, '2':49.46}
        previous, current = observed({}), observed({'0':32,'2':30})
        def report(history=()):
            group = comparisons.compare(new, old, history, current, previous)
            packet = comparisons.prompt_packet(new, group)
            context = rule_commentary.comparison_context(group, packet)
            return rule_commentary.write(packet, (old,new), context, 'previous')
        summary, sections, _ = report()
        self.assertIn('Hall in {seat:Light} reported **62** FP votes.', summary)
        self.assertIn('less than **1 pp**', summary)
        self.assertNotIn('Hammond', summary)
        self.assertFalse(sections)
        # A reversal elsewhere can be hidden by comparing only endpoints.
        mid = deepcopy(old)
        mid['seats']['Schubert']['win_percent'] = {'0':51, '2':49}
        self.assertNotIn('less than **1 pp**', report([mid])[0])
        new['statewide']['countProgress']['fp_votes'] = 501
        self.assertNotIn('Hall', report()[0])

    def test_quiet_early_count_lists_largest_additions_and_remainder(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = deepcopy(old)
        new['seats']['Light']['candidates']['4'] = {'party':'NAT', 'name':None}
        new['seats']['Light']['win_percent']['4'] = 0
        new['seats']['Light']['win_percent'].update({'0':50.99,'2':49.01})
        new['statewide']['countProgress'] = dict(fp_votes=600, tcp_votes=200, expected_formal_votes=100000)
        previous, current = observed({}), observed({})
        for i, (fp,tcp) in enumerate([(88,0),(0,73),(60,60),(40,0),(30,0),(20,0),(10,0),(9,0),(2,0)]):
            name = 'Booth '+str(i)
            key = 'Light/'+name
            # The second location already had FP votes, and the last had a
            # large previous count. Only new ballots determine their rank.
            earlier = {'0':100} if i in (1,8) else {}
            previous['booths'][key] = dict(previous['booths']['Light/Hall'], name=name, fp_votes_current=earlier)
            current['booths'][key] = dict(previous['booths'][key], fp_votes_current={'0':earlier.get('0',0)+fp},
                                          tcp_votes_current={'0':tcp,'2':0} if tcp else {})
        def report():
            group = comparisons.compare(new,old,[],current,previous)
            packet = comparisons.prompt_packet(new,group)
            return rule_commentary.write(packet,(old,new),rule_commentary.comparison_context(group,packet))[0]
        text = report()
        self.assertIn('Booth 0 in {seat:Light} (**88** FP votes), Booth 1 in {seat:Light} (**73** TCP votes), '
                      'Booth 2 in {seat:Light} (**60** FP votes and **60** TCP votes) and 6 other booths',text)
        self.assertIn('less than **1 pp**',text)
        for i in range(4,9):
            current['booths']['Light/Booth '+str(i)] = previous['booths']['Light/Booth '+str(i)]
        self.assertIn('and 1 other booth.',report())
        # A new label on a slot with a real winning draw cannot be ignored.
        new['seats']['Light']['win_percent']['4'] = .01
        self.assertNotIn('less than **1 pp**',report())

    def test_rule_headline_gives_size_and_names_a_substantial_local_count(self):
        before = comparisons.summary(numerical('earlier', chance=52))
        after = comparisons.summary(numerical('later', chance=60))
        old = observed({'0': 100, '2': 100})
        new = observed({'0': 180, '2': 120})
        packet = comparisons.prompt_packet(after, comparisons.compare(after, before, [], new, old))
        summary, details, _ = rule_commentary.write(packet)
        self.assertIn('rose **8.0 pp** as the result at Hall was updated', summary)
        self.assertIn('**60.0% (+8.0 pp)**', details[0]['text'])
        self.assertIn('At Hall,', details[0]['text'])
        # The same booth can still be worth a detailed comparison even when
        # it is a small part of the local activity over a longer window.
        packet['observed_count_changes_by_seat']['Light']['affected_votes'] = 401
        summary, details, _ = rule_commentary.write(packet)
        self.assertNotIn('Hall', summary)
        self.assertIn('At Hall,', details[0]['text'])
        packet['observed_count_changes_by_seat']['Light']['affected_votes'] = 100
        packet['input_revision_changed'] = True
        self.assertNotIn('Hall', rule_commentary.write(packet)[0])
        packet['input_revision_changed'] = False
        old['booths']['Light/Hall']['fp_votes_projected'] = {}
        packet = comparisons.prompt_packet(after, comparisons.compare(after, before, [], new, old))
        self.assertIn('Counting was updated at Hall.', rule_commentary.write(packet)[0])

    def test_early_tcp_uses_same_booth_expectation_and_same_opponents_district_projection(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = comparisons.summary(numerical('later', chance=60))
        new['seats']['Light']['tcp_percentiles'] = {'2/0': {'50':47}}
        earlier = observed({'0':70, '2':70, '3':60})
        hall = earlier['booths']['Light/Hall']
        hall['tcp_votes_projected'] = {'0':100, '2':100}
        earlier['booths']['Light/Town'] = dict(hall, name='Town')
        later = deepcopy(earlier)
        later['booths']['Light/Hall']['tcp_votes_current'] = {'0':60, '2':40}
        later['booths']['Light/Town']['tcp_votes_current'] = {'0':80, '2':120}
        packet = comparisons.prompt_packet(new, comparisons.compare(new, old, [], later, earlier))
        summary, details, _ = rule_commentary.write(packet, (old, new))
        text = next(section['text'] for section in details if section['seats'] == ['Light'])
        self.assertIn('Across Town and Hall,', text)
        self.assertIn('**46.7%** of **300** TCP votes', text)
        self.assertIn('**4.3 pp** below the earlier estimate', text)
        self.assertIn('rose to **53.0% (+2.0 pp)**', text)
        self.assertIn('Counting was updated at Town and Hall.', summary)
        self.assertNotIn('still to come', text)

    def test_partial_aggregate_tcp_stays_separate_from_a_complete_booth(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = comparisons.summary(numerical('later', chance=60))
        before = observed({'0':60, '2':40}, {'0':0, '2':0})
        hall = before['booths']['Light/Hall']
        hall.update(tcp_votes_projected={'0':55, '2':45}, vote_type='Ordinary')
        before['booths']['Light/Early'] = dict(hall, name='Early', vote_type='Early',
            fp_votes_current={'0':400,'2':400,'3':200}, tcp_votes_projected={'0':550,'2':450})
        after = deepcopy(before)
        after['booths']['Light/Early']['tcp_votes_current'] = {'0':40,'2':60}
        after['booths']['Light/Hall']['tcp_votes_current'] = {'0':70,'2':30}
        result = comparisons.compare(new,old,[],after,before)
        units = rule_evidence.booth_units(result['evidence'])
        group, = rule_evidence.pool(units['Light'], '0')
        self.assertEqual((group['locations'], group['ballots']), ('Hall', 100))
        self.assertEqual(group['partial_categories'], {'Early': (100,1000)})
        packet = comparisons.prompt_packet(new,result)
        _, sections, _ = rule_commentary.write(packet,(old,new),rule_commentary.comparison_context(result,packet))
        self.assertIn('TCP results for **100** of **1,000** FP votes', sections[0]['text'])
        early = after['booths']['Light/Early']
        prior = before['booths']['Light/Early']
        early['tcp_votes_current'] = {'0':500,'2':480}
        self.assertTrue(comparisons.tcp_against_previous_forecast(prior,early)['available'])
        early['tcp_votes_current'] = {'0':40,'2':60}
        # An identified ranked benchmark for the counted batch takes priority.
        pair = frozenset(('0','2'))
        ranked = comparisons.tcp_against_previous_forecast(prior,early,booth_key='Early',
            ranked_pairs={('Light',pair)}, ranked_estimates={('Early',pair):({'0':42,'2':58},'ranked TCP')})
        self.assertAlmostEqual(ranked['shares']['0']['difference_pp'], -2)

    def test_tcp_sources_without_a_booth_benchmark_and_restored_votes(self):
        old = comparisons.summary(numerical('earlier', chance=76))
        new = comparisons.summary(numerical('later', chance=90))
        before = observed({'0':500,'2':500}, {'0':100,'2':100})
        hall = before['booths']['Light/Hall']
        before['booths']['Light/Postal'] = dict(hall, name='Postal', vote_type='Postal', tcp_votes_current={})
        after = deepcopy(before)
        after['booths']['Light/Hall']['tcp_votes_current'] = {'0':130,'2':120}
        postal = {'0':550,'2':450}
        after['booths']['Light/Postal']['tcp_votes_current'] = postal
        def report():
            group = comparisons.compare(new,old,[],after,before)
            packet = comparisons.prompt_packet(new,group)
            ctx = rule_commentary.comparison_context(group,packet)
            return next(s['text'] for s in rule_commentary.write(packet,(old,new),ctx)[1] if s['seats'] == ['Light'])
        text = report()
        self.assertIn('**1,000** votes in the Postal count and **50** at Hall', text)
        self.assertNotIn('above the earlier estimate', text)
        before['booth_tcp_bounds'] = {'Light/Postal':[postal]}
        text = report()
        self.assertIn('**50** votes at Hall', text)
        self.assertIn('**1,000** TCP votes from the Postal count reappeared', text)
        # A different old allocation with the same total does not establish
        # restoration. A reallocation at Hall is also not fifty new ballots.
        before['booth_tcp_bounds']['Light/Postal'] = [{'0':551,'2':449}]
        after['booths']['Light/Hall']['tcp_votes_current'] = {'0':160,'2':90}
        text = report()
        self.assertNotIn('reappeared', text)
        self.assertNotIn('at Hall', text)

    def test_rules_pool_all_matched_booths_and_name_small_declarations(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = comparisons.summary(numerical('later', chance=60))
        earlier, later = observed({}), observed({})
        earlier['booths'].clear()
        later['booths'].clear()
        for name, votes in [('Town',400), ('East',300), ('West',200), ('Hill',80), ('Postal',20)]:
            key = 'Light/'+name
            record = dict(name=name, seat_name='Light', vote_type='Postal' if name=='Postal' else 'Ordinary',
                          fp_votes_current={}, fp_votes_projected={'0':40,'2':40,'3':20}, tcp_votes_current={})
            earlier['booths'][key] = record
            later['booths'][key] = dict(record, fp_votes_current={'0':votes*35//100,'2':votes*20//100,'3':votes*45//100})
        # A small recheck to an already large tally has a different denominator.
        for evidence, first in [(earlier, 3000), (later, 3001)]:
            evidence['booths']['Light/Old'] = dict(record, name='Old', vote_type='Ordinary',
                fp_votes_current={'0':first, '2':1000}, fp_votes_projected={'0':3000,'2':1000})
            evidence['booths']['Light/Absent'] = dict(record, name='Absent', vote_type='Absent',
                fp_votes_current={'0':first, '2':1000}, fp_votes_projected={})
        del earlier['booths']['Light/Absent']  # New category, no reference estimate.
        old['seats']['Light']['fp_mean_percent'] = {p:100*n/4500 for p,n in [('0',3200),('2',1200),('3',100)]}
        group = comparisons.compare(new, old, [], later, earlier)
        packet = comparisons.prompt_packet(new, group)
        packet['evidence'] = []  # Deliberately remove every booth from the AI shortlist.
        local = rule_commentary.comparison_context(group, packet)
        _, sections, _ = rule_commentary.write(packet, (old, new), local)
        text = next(s['text'] for s in sections if s['seats'] == ['Light'])
        self.assertIn('Town, East, the Postal count and 2 other booths', text)
        self.assertIn('**35.0%** of **1,000** FP votes', text)
        self.assertIn('**20.0%**, **20.0 pp** below', text)
        self.assertIn('FP gap was **15.0 pp** more favourable', text)
        self.assertIn('Separate FP updates also came from the Absent count', text)
        self.assertNotIn('Old', text)

    def test_small_win_change_compares_the_projected_opponents_at_the_same_booth(self):
        old = comparisons.summary(numerical('earlier', chance=63.5))
        new = comparisons.summary(numerical('later', chance=65.1))
        for side, share in [(old,49.7), (new,50.4)]:
            side['seats']['Light']['tcp_percentiles']['0/2']['50'] = share
        before = observed({})
        before['booths']['Light/Hall']['fp_votes_projected'] = {'0':57,'2':27,'3':16}
        old['seats']['Light']['fp_mean_percent'] = {'0':57,'2':27,'3':16}
        after = observed({'0':55,'2':22,'3':23})
        group = comparisons.compare(new,old,[],after,before)
        packet = comparisons.prompt_packet(new,group)
        _, sections, _ = rule_commentary.write(packet,(old,new),rule_commentary.comparison_context(group,packet))
        report = next(s['text'] for s in sections if s['seats'] == ['Light'])
        self.assertIn('{candidate:Light:2} received **22.0%**, **5.0 pp** below',report)
        self.assertIn('FP gap was **3.0 pp** more favourable to {candidate:Light:0}',report)
        self.assertIn('rose to **50.4% (+0.7 pp)**',report)
        self.assertNotIn('Despite that local',report)

    def test_headline_rivals_get_both_fp_shares_and_distinctive_fresh_booths(self):
        old = comparisons.summary(numerical('earlier', chance=87))
        new = comparisons.summary(numerical('later', chance=95))
        group = comparisons.compare(new, old, [], observed({'0':452,'2':201}), observed({'0':40,'2':20}))
        packet = comparisons.prompt_packet(new, group)
        context = rule_commentary.comparison_context(group, packet)
        ongoing = dict(ledger='FP', first=False, units=1, declaration=True, locations='the Early count',
                       changed=800, ballots=1000, rates={'0':(45.2,.1), '2':(20.1,-.2)})
        fresh = dict(ledger='FP', first=True, units=1, declaration=False, locations='North',
                     changed=541, ballots=541, rates={'0':(47.9,6.4), '2':(20.7,-4.8)})
        context['pools']['Light']['0'] = [ongoing, fresh]

        def report():
            _, sections, _ = rule_commentary.write(packet, (old,new), context, 'previous')
            return next(s['text'] for s in sections if s['seats'] == ['Light'])

        text = report()
        self.assertIn('{candidate:Light:2} received **20.1%**, **0.2 pp** below', text)
        self.assertIn('At North, {candidate:Light:0} received **47.9%** of **541** FP votes', text)
        self.assertIn('{candidate:Light:2} received **20.7%**, **4.8 pp** below', text)
        fresh['ballots'] = fresh['changed'] = 20
        self.assertNotIn('At North,', report())
        fresh['ballots'] = fresh['changed'] = 541
        del fresh['rates']['2']
        self.assertNotIn('At North,', report())

    def test_booth_fp_prefers_the_changing_likely_opponent_to_a_larger_minor_party_surprise(self):
        seat = 'Mornington'
        row = {'seat':seat, 'party_id':'1'}
        rates = {'1':(39.2,-7.8), '3':(19.6,.2), '2':(9.8,4.2), '0':(25.5,1.7)}
        group = {'first':True, 'rates':rates}
        entities = {rule_commentary.candidate(seat,p) for p in rates}
        def forecast(chance):
            district = {'candidates':{p:{'name':p} for p in rates}, 'pairing_percent':{'0/1':chance}}
            return {'iterations':10000, 'seats':{seat:district}, 'conditional_wins':{seat:{'0/1':5}}}
        def extra(chance):
            return rule_commentary.other_opponent_primary(group,row,'3','0',(forecast(54.3),forecast(chance)),entities)
        self.assertIn('{candidate:Mornington:0} received **25.5%**, **1.7 pp** above',extra(58.3))
        self.assertIn('{candidate:Mornington:2}',extra(54.3))

    def test_choose_mixed_counts_using_the_district_account(self):
        seat = 'Ripon'
        row = dict(seat=seat, party_id='1', change_pp=-5)
        parts = dict(primary_change_pp=.50, measured_change_pp=-.015, learned_change_pp=.004)
        packet = {'rule_context': {'tpp_components': {seat:parts}}}
        district = dict(candidates={p:{'name':p} for p in ('0','1')}, pairing_percent={'0/1':99})
        endpoint = {'seats':{seat:district}}
        groups = [dict(ledger='FP', first=True, changed=222, ballots=222, locations='East',
                       rates={'1':(55.9,-4.7),'0':(25.2,.9)}),
                  dict(ledger='TCP', first=True, changed=208, ballots=208, locations='West',
                       rates={'1':(59.1,2.4),'0':(40.9,-2.4)})]
        def select(after):
            return rule_commentary.attributed_count(packet, row, rule_commentary.PairProjection('0',51.1,after),
                                                    '0', (endpoint,endpoint), groups)
        lead, _, contrast = select(50.6)
        self.assertEqual(lead['ledger'], 'FP')
        self.assertIn('5** extra votes', contrast)
        parts.update(primary_change_pp=.015, measured_change_pp=-.50)
        row['change_pp'] = 5
        self.assertEqual(select(51.6)[0]['ledger'], 'TCP')
        self.assertIn('FP returns at East', select(51.6)[2])
        parts['learned_change_pp'] = -.8
        self.assertIsNone(select(51.6))
        parts['learned_change_pp'] = .004
        district['pairing_percent'] = {'0/1':60}
        self.assertIsNone(select(51.6))

    def test_nonclassic_count_reports_stronger_fp_return_and_contrary_measured_tcp(self):
        party, rival, seat = '3', '1', 'District'
        row = dict(seat=seat, party_id=party, change_pp=6.7)
        def endpoint(primary):
            return {'seats': {seat: {'pairing_percent': {'1/3': 100},
                                     'candidates': {p: {'name':p} for p in (party, rival)},
                                     'fp_mean_percent': {party:primary, rival:40}}}}
        endpoints = endpoint(32), endpoint(33)
        projection = rule_commentary.PairProjection(rival,50.4,50.7,100)
        fp = dict(ledger='FP', first=True, ballots=902, rates={party:(48.9,16.9),rival:(30.9,-7.8)},
                  tcp_estimates={party:(54.9,64.7),rival:(45.1,35.3)})
        tcp = dict(ledger='TCP', first=True, pair=[party,rival], units=1, declaration=False, locations='West',
                   ballots=1232, rates={party:(48.2,-1.6),rival:(51.8,1.6)})
        def select(first=fp, second=tcp, forecasts=endpoints, **flags):
            return rule_commentary.mixed_pair_returns(dict(rule_context={}, **flags), row, projection, rival,
                                                       forecasts, [second,first])
        lead, district, contrary = select()
        self.assertIs(lead,fp)
        self.assertIn('1.0 pp',district)
        self.assertIn('At West',contrary)
        self.assertIn('1.6 pp** below',contrary)
        self.assertIsNone(select(dict(fp,first=False)))
        self.assertIsNone(select(dict(fp,ballots=100)))  # Large share difference on too few ballots.
        self.assertIsNone(select(dict(fp,tcp_estimates={})))
        self.assertIsNone(select(second=dict(tcp,pair=['3','0'])))
        self.assertIsNone(select(forecasts=(endpoint(33),endpoint(32))))
        self.assertIsNone(select(input_revision_changed=True))

    def test_tpp_attribution_compares_complete_draw_accounts(self):
        before, after = dict(iterations=100), dict(iterations=100)
        row = dict(accounted_draws=100, primary_change_pp=1., measured_change_pp=.1, learned_change_pp=.2)
        old = {'fp_based_tpp':{'Ripon':row}}
        new = {'fp_based_tpp':{'Ripon':dict(row, primary_change_pp=1.5)}}
        delta = rule_evidence.tpp_component_changes(before,after,old,new)
        self.assertEqual(delta['Ripon'], dict(primary_change_pp=.5, measured_change_pp=0, learned_change_pp=0))
        row['accounted_draws'] = 75
        self.assertEqual(rule_evidence.tpp_component_changes(before,after,old,new), {})
        self.assertEqual(rule_evidence.tpp_component_changes(before,after,{},new), {})
        before['origin'] = True
        self.assertEqual(rule_evidence.tpp_component_changes(before,after,{},new)['Ripon']['primary_change_pp'],1.5)

    def test_count_weight_explanation_requires_a_material_effect_on_the_fp_gap(self):
        # The local estimate itself shifts against party 0. More weight on
        # that estimate still widens its advantage over party 2. Holding the
        # old weights distinguishes this from new figures alone doing so.
        def model(weight, count):
            def values(pair):
                return [dict(party_index=p, value=v) for p, v in zip((0, 2), pair)]
            return dict(method='local_precision', baseline_log_odds_mean=values((-.5, -1.0)),
                        shared_deviation=values((0, 0)), count_log_odds_mean=values(count),
                        individual_count_weights=values((weight, weight)))
        event = {'before': {'fp_integration': model(.2, (-.4, -1.2))},
                 'after': {'fp_integration': model(.7, (-.46, -1.2))}}
        check = rule_evidence.primary_weight_check(event)
        gap = lambda key: check['0'][key] - check['2'][key]
        self.assertLess(gap('previous_weight'), gap('before'))
        self.assertGreater(gap('after'), gap('before'))
        old, new = [comparisons.summary(numerical(key)) for key in ('old', 'new')]
        for side, primary in [(old, {'0':38, '2':28}), (new, {'0':37.5, '2':26.3})]:
            side['seats']['Light']['fp_mean_percent'] = primary
        packet = {'rule_context': {'primary_weight_checks': {'Light': check}}}
        def explain():
            return rule_commentary.district_primary_evidence(packet, {'seat':'Light','party_id':'0'},
                rule_commentary.PairProjection('2', 51, 52), (old,new))[1]
        self.assertIn('the forecast relied more on the results counted so far', explain())
        self.assertIn('fell more for {candidate:Light:2} (**28.0%** to **26.3%**) than for '
                      '{candidate:Light:0} (**38.0%** to **37.5%**)', explain())
        check['0']['previous_weight'], check['2']['previous_weight'] = check['0']['after'], check['2']['after']
        self.assertIsNone(explain())
        packet['rule_context']['primary_weight_checks']['Light'] = rule_evidence.primary_weight_check(event)
        new['seats']['Light']['fp_mean_percent']['2'] = 29
        self.assertIsNone(explain())

    def test_ordinary_fp_weight_check_keeps_the_booth_and_counted_endpoints(self):
        def snapshot(weight):
            field = lambda v: [{'party_index': 3, 'value': v}]
            return {'fp_integration': dict(method='ordinary_vote_blend', remaining_fraction=.9,
                baseline_log_odds_mean=field(0), count_log_odds_mean=field(0),
                individual_count_weights=field(weight), ordinary_comparisons=[dict(party_index=3,
                fixed_fraction=.04, ordinary_fraction=1, matched_fraction=.5, breadth=.5,
                booth_remaining_share=.3, prior_remaining_log_odds=0, local_log_odds_change=1)])}
        before, after = snapshot(.2), snapshot(.8)
        result = rule_evidence.primary_weight_check({'before': before, 'after': after})['3']
        self.assertAlmostEqual(result['before'], result['previous_weight'])
        # With two equal booths, the tapered responses are .36 and .81.
        for key, response in [('previous_weight', .36), ('after', .81)]:
            expected = 100*(.04+.9*(.5*.3 + .5/(1+math.exp(-response))))
            self.assertAlmostEqual(result[key], expected)
        # The first booth had no breadth bonus. Keep the second booth's
        # ballots but remove its breadth bonus as well as the extra precision.
        before['fp_integration']['ordinary_comparisons'][0]['breadth'] = 0.
        changed = rule_evidence.primary_weight_check({'before': before, 'after': after})['3']
        self.assertAlmostEqual(changed['previous_weight'], changed['before'])
        self.assertEqual(changed['after'], result['after'])

        # New fitted centres and earlier cautious-fit archives already include
        # the response to ordinary votes. Changing the precision alone must not
        # create a second movement or an explanation about agreement among booths.
        for method in ('cautious_ordinary_fit', 'split_ordinary_fit'):
            for side, centre in [(before, .48), (after, .52)]:
                side['fp_integration']['method'] = method
                side['fp_integration']['ordinary_comparisons'][0]['fitted_remaining_share'] = centre
            fitted = rule_evidence.primary_weight_check({'before': before, 'after': after})['3']
            self.assertAlmostEqual(fitted['after'], 100*(.04+.9*.52))
            self.assertAlmostEqual(fitted['previous_weight'], fitted['after'])
            self.assertNotIn('earlier_signal', fitted)

    def test_new_booths_can_reinforce_soften_or_disagree_on_a_primary_shortfall(self):
        def booth(name, ballots, gap):
            return dict(before={}, after=dict(name=name, booth_type='Normal', vote_type='Ordinary',
                                               fp_votes_current={'0':ballots}),
                        fp_share_minus_previous_expectation_pp={'0':gap})
        def explanation(events, earlier_count=None):
            check = {'0':dict(after=32, previous_weight=34, earlier_signal=-.5, current_signal=-.3,
                              earlier_booth='Town Hall', new_booths=rule_evidence.fresh_booth_pattern(events,'0')),
                     '1':dict(after=36, previous_weight=36)}
            if earlier_count:
                check['0']['dominant_earlier_count'] = earlier_count
                check['1'].update(earlier_signal=.3, current_signal=.15)
            return rule_commentary.accumulating_primary_point('Test', '1', '0', check,
                                                              [{'0':34,'1':36}, {'0':32,'1':36}])
        first, second = booth('East', 100, -5), booth('West', 150, 1)
        self.assertIn('reduced the overall FP shortfall', explanation([first]))
        self.assertIn('softened the earlier FP shortfall', explanation([second]))
        concentrated = explanation([second], dict(largest=747, total=784, name='Town Hall'))
        self.assertIn('**747** of **784** local FP votes came from Town Hall', concentrated)
        self.assertIn('{candidate:Test:1} remained above expectations and {candidate:Test:0} below', concentrated)
        self.assertIn('mixed for {candidate:Test:0}, with the largest FP shortfall at East', explanation([first,second]))
        first['before'] = {'fp_votes_current':{'0':80}}
        self.assertIsNone(explanation([first, second]))
        second['fp_share_minus_previous_expectation_pp'] = {}
        self.assertIsNone(explanation([second]))

    def test_broader_count_can_give_more_weight_to_a_smaller_shortfall(self):
        check = {'0':dict(after=34.6, previous_weight=34.3, earlier_signal=.32, current_signal=.06,
                          earlier_booth='First Hall', new_booths={'low':-8.8,'high':-8.8}),
                 '1':dict(after=35.6, previous_weight=38, earlier_signal=-.64, current_signal=-.46,
                          earlier_booth='First Hall', new_booths={'low':-5.2,'high':-5.2},
                          dominant_earlier_count=dict(name='First Hall', largest=181, total=181))}
        def explain():
            return rule_commentary.accumulating_primary_point('Test','0','1',check,
                [{'0':34.7,'1':37.8}, {'0':34.9,'1':35.9}])
        self.assertIn('reduced the overall FP shortfall for {candidate:Test:1}', explain())
        self.assertIn('applied more of that smaller shortfall', explain())
        check['1']['current_signal'] = -.7
        self.assertNotIn('smaller shortfall', explain())

    def test_divergent_fp_can_reflect_a_different_opponent_reaching_second(self):
        old, new = [comparisons.summary(numerical(key)) for key in ('old', 'new')]
        for side, pair, other, win in [(old, 47.62, 38.99, 48.026), (new, 51.76, 37.37, 51.507)]:
            district = side['seats']['Light']
            district['candidates']['1'] = {'name':'Other candidate', 'party':'LIB'}
            district['pairing_percent'] = {'0/2':pair, '0/1':other}
            side['conditional_wins'] = {'Light':{'0/2':win, '0/1':99}}
        def explanation():
            return pairing_commentary.possible_opponent_change((old, new), 'Light', '0', '2')
        self.assertIn('{candidate:Light:2} may be edging out {candidate:Light:1}', explanation())
        new['conditional_wins']['Light']['0/2'] = 47
        self.assertIsNone(explanation())
        new['conditional_wins']['Light']['0/2'] = 51.507
        new['seats']['Light']['pairing_percent']['0/1'] = 39
        self.assertIsNone(explanation())

    def test_fp_return_favours_final_opponent_while_candidate_qualifies_more_often(self):
        old, new = [comparisons.summary(numerical(key)) for key in ('old', 'new')]
        for side, chance, win, frequency in [
                (old, 58.91, 72.833, 80.87), (new, 60.60, 73.558, 82.37)]:
            district = side['seats']['Light']
            district['candidates']['1'] = {'name':'Other finalist', 'party':'LIB'}
            district['win_percent'] = {'2':chance, '0':100-chance, '1':0}
            district['pairing_percent'] = {'0/2':frequency, '1/0':100-frequency}
            side['conditional_wins'] = {'Light':{'0/2':100-win}}
        before, after = observed({}), observed({'0':128, '1':61, '2':117, '3':44})
        before['booths']['Light/Hall']['fp_votes_projected'] = {'0':112,'1':58,'2':113,'3':67}
        # Match the district FP to the earlier booth estimate for this one-booth fixture.
        old['seats']['Light']['fp_mean_percent'] = {p:n/3.5 for p,n in
            before['booths']['Light/Hall']['fp_votes_projected'].items()}
        new['seats']['Light']['fp_mean_percent'] = dict(old['seats']['Light']['fp_mean_percent'])
        new['seats']['Light']['fp_mean_percent']['2'] += .4
        def report():
            group = comparisons.compare(new, old, [], after, before)
            packet = comparisons.prompt_packet(new, group)
            _, sections, _ = rule_commentary.write(packet, (old,new), rule_commentary.comparison_context(group,packet))
            return next(s['text'] for s in sections if s['seats'] == ['Light'])
        text = report()
        self.assertIn('more favourable to {candidate:Light:0}', text)
        self.assertIn('**0.4 pp** towards {candidate:Light:2} relative to {candidate:Light:1}', text)
        self.assertIn('getting past {candidate:Light:1} to face {candidate:Light:0}', text)
        self.assertIn('**80.9%** to **82.4%**', text)
        self.assertIn('**72.8%** to **73.6%**', text)
        # Unreported pairings could now account for the change in second place.
        new['seats']['Light']['pairing_percent']['1/0'] = 15
        self.assertNotIn('getting past', report())

    def test_both_finalists_gain_despite_a_disappointing_booth_and_level_displayed_tcp(self):
        old, new = [comparisons.summary(numerical(key)) for key in ('old', 'new')]
        for side, median, frequency, strength, wins in [
                (old,49.978,97.88,49.775,[48.94,49.32,1.74]), (new,50.021,99.17,50.227,[50.07,49.56,.37])]:
            district = side['seats']['Light']
            district['candidates']['3'] = {'name':'Independent candidate','party':'IND'}
            district['win_percent'] = dict(zip(('0','2','3'), wins))
            district['pairing_percent'] = {'0/2':frequency,'0/3':100-frequency}
            district['tcp_percentiles']['0/2']['50'] = median
            side['conditional_wins'] = {'Light':{'0/2':strength}}
        before = observed({})
        before['optional_preferential'] = False
        before['booths']['Light/Hall']['fp_votes_projected'] = {'0':50,'2':33.2,'3':16.8}
        old['seats']['Light']['fp_mean_percent'] = {'0':50,'2':33.2,'3':16.8}
        after = observed({'0':39,'2':34,'3':27})
        before['booths']['Light/Hall']['tcp_votes_projected'] = {'0':54.7,'2':45.3}
        after['booths']['Light/Hall']['tcp_votes_projected'] = {'0':53.4,'2':46.6}
        group = comparisons.compare(new, old, [], after, before)
        packet = comparisons.prompt_packet(new, group)
        _, sections, _ = rule_commentary.write(packet,(old,new),rule_commentary.comparison_context(group,packet))
        text = next(s['text'] for s in sections if s['seats'] == ['Light'])
        self.assertIn('{candidate:Light:2} received **34.0%**, **0.8 pp** above', text)
        # The earlier median is below the finalist's own projected primaries.
        # No feasible transfer split connects those benchmarks.
        self.assertNotIn('Those FP votes point to', text)
        self.assertIn('held at **50.0%**', text)
        self.assertIn('win chance for {candidate:Light:3} fell', text)
        self.assertIn('**97.9%** to **99.2%**', text)
        self.assertIn('won **50.2%** of those simulations, up from **49.8%**', text)
        new['seats']['Light']['win_percent']['2'] = 49
        self.assertIsNone(pairing_commentary.both_finalists_gain(
            {'seat':'Light','party_id':'0'}, '2', (old,new), group['win_changes']))

    def test_booth_tpp_requires_one_identifiable_coalition_opponent(self):
        old, new = [comparisons.summary(numerical(key)) for key in ('old', 'new')]
        for s in (old, new):
            s['parties']['4'] = 'NAT'
            d = s['seats']['Light']
            d['candidates']['4'] = {'party':'NAT','name':'Local National'}
            d['pairing_percent'] = {'0/4':95}
            d['tcp_percentiles'] = {'0/4':{'50':55}}
            d['fp_mean_percent'] = {'0':50,'4':40,'2':10}
        booth = dict(seat_name='Light', fp_votes_projected={'0':50, '4':40, '2':10},
                     tpp_votes_projected={'0':55, '1':45})
        def figures(later, ranked=()):
            wanted = {'Light':{frozenset(('0','4'))}}
            evidence = {'booths':{'Hall':booth}, 'optional_preferential':False}
            earlier_tcp = booth_tcp.expectations(old, evidence, wanted, ranked)
            return rule_evidence.booth_projection_after_preferences(booth, later, 'Light', old, new, ranked,
                earlier_tcp=earlier_tcp, booth_key='Hall', expected_fp=booth['fp_votes_projected'],
                measured_fp=later['fp_votes_projected'], reference_evidence=evidence, current_evidence={})
        self.assertAlmostEqual(figures(booth)['4'][0],45)
        self.assertIsNone(figures(dict(booth,fp_votes_projected={'0':50,'1':10,'4':40})))
        self.assertIsNone(figures(booth,{('Light',frozenset(('0','4')))}))
        self.assertIsNone(figures(dict(booth,tcp_votes_current={'0':9,'4':4})))
        # An exported opponent identifies which Coalition partner the
        # anonymous TPP represents. Mixed references stay ambiguous.
        ballot = {'1', '4'}
        rates = {'classic_benchmark_labor_rates': {'Light': {'0/4': {}}}}
        self.assertEqual(booth_tcp.coalition_opponents(rates, 'Light', ballot), {'4'})
        rates['classic_benchmark_labor_rates']['Light']['0/1'] = {}
        self.assertEqual(booth_tcp.coalition_opponents(rates, 'Light', ballot), ballot)

    def test_only_matched_new_booths_contribute_to_the_ordinary_transfer_comparison(self):
        earlier = observed({'0':100, '2':50, '3':50}, {'0':130, '2':70})
        earlier['optional_preferential'] = False
        hall = earlier['booths']['Light/Hall']
        hall.update(vote_type='Ordinary', booth_type='Normal')
        earlier['booths']['Light/Second'] = dict(hall, name='Second')
        earlier['booths']['Light/New'] = dict(hall, name='New', tcp_votes_current={})
        current = deepcopy(earlier)
        current['booths']['Light/New']['tcp_votes_current'] = {'0':110, '2':90}
        events = [dict(kind='booths', record='Light/New', seat='Light', changed_votes=200,
                       tcp_vote_change=200, after=current['booths']['Light/New'])]
        def comparison():
            return rule_evidence.tcp_update_context(earlier, current, events, ()).get('Light', {})
        self.assertEqual(comparison()['transfers'], [{'0':60, '2':40}, {'0':10, '2':40}])
        current['booths']['Light/New']['tcp_votes_current']['0'] -= 1
        self.assertNotIn('transfers', comparison())

    def test_coalition_benchmark_requires_the_same_previously_known_fp_voters(self):
        before = dict(vote_type='Ordinary', fp_votes_current={'0':30,'1':40,'4':30}, tcp_votes_current={})
        after = dict(before, tcp_votes_current={'0':35,'1':65})
        def compare(latest):
            return comparisons.tcp_against_previous_forecast(before,latest,booth_key='Hall',
                blended_estimates={('Hall',frozenset(('0','1'))):{'0':40,'1':60}},
                pending_coalition={'1':{'votes':100,'labor':36}})
        self.assertEqual(compare(after)['expectation_kind'], 'local Coalition preference fit')
        self.assertAlmostEqual(compare(after)['shares']['0']['difference_pp'], -1)
        revised = dict(after, fp_votes_current={'0':31,'1':39,'4':30})
        self.assertEqual(compare(revised)['expectation_kind'], 'district-aligned TCP')
        self.assertEqual(compare(dict(after,tcp_votes_current={'0':36,'1':65}))['expectation_kind'], 'district-aligned TCP')

    def test_pending_ordinary_tcp_uses_the_fit_at_the_start_of_the_selected_window(self):
        key = 'Light/Hall'
        prior = observed({'0':30,'1':40,'4':30})
        prior['booths'][key]['vote_type'] = 'Ordinary'
        current = deepcopy(prior)
        current['booths'][key]['tcp_votes_current'] = {'0':35,'1':65}
        current['pending_coalition_tcp'] = {key:{'1':{'votes':100,'labor':50}}}
        new = comparisons.summary(numerical('latest'))
        for ref, expected, gap in [('previous',37,-2), ('opening',31,4)]:
            prior['pending_coalition_tcp'] = {key:{'1':{'votes':100,'labor':expected}}}
            group = comparisons.compare(new,comparisons.summary(numerical(ref)),[],current,prior)
            event = next(e for e in group['evidence'] if e['kind'] == 'booths')
            self.assertAlmostEqual(event['tcp_against_previous_forecast']['shares']['0']['difference_pp'],gap)

    def test_outlook_after_a_weak_primary_requires_a_material_swap_to_an_easier_opponent(self):
        a,b = [comparisons.summary(numerical(k)) for k in ('previous','latest')]
        for summary, hard in [(a,45),(b,36.5)]:
            seat = summary['seats']['Light']
            seat['candidates']['3'] = {'name':'Independent'}
            seat['pairing_percent'] = {'0/2':hard,'0/3':100-hard}
            summary['conditional_wins'] = {'Light':{'0/2':95,'0/3':99}}
        row = dict(seat='Light',party_id='0',change_pp=.6)
        rival, line = pairing_commentary.changed_opponents_after_weak_primary(row,(a,b))
        self.assertEqual(rival,'2')
        self.assertIn('fell from **45.0%** to **36.5%**',line)
        self.assertIn('**99.0%** of those simulations',line)
        b['conditional_wins']['Light']['0/3'] = 94
        self.assertIsNone(pairing_commentary.changed_opponents_after_weak_primary(row,(a,b)))

    def test_declaration_addition_compares_only_the_added_votes_with_the_remaining_votes(self):
        before = dict(tcp_votes_current={'0':60,'1':40})
        after = dict(tcp_votes_current={'0':95,'1':55})
        # Labor received 35/50 in the new batch. Its benchmark is the
        # 90/200 expected among the outstanding ballots after the first 100.
        expected = {'0':150,'1':150}
        batch = rule_evidence.additional_return(before, after, 'TCP', expected)
        self.assertEqual((batch['previous'],batch['added']), (100,50))
        self.assertAlmostEqual(batch['shares']['0'],70)
        self.assertAlmostEqual(batch['remaining_shares']['0'],45)
        for new in ({'0':59,'1':90},{'0':50,'1':30},{'0':95,'2':55}):
            self.assertIsNone(rule_evidence.additional_return(before,dict(tcp_votes_current=new),'TCP',expected))
        self.assertNotIn('remaining_shares',rule_evidence.additional_return(before,after,'TCP',{}))

    def test_fp_addition_uses_the_local_expectation_at_each_comparison_start(self):
        # Two reference snapshots have the same local count, but other returns
        # have changed the forecast for the remaining votes. The latest fit
        # must never leak into either comparison.
        key = 'Light/Hall'
        old, recent, new = [comparisons.summary(numerical(k)) for k in ('start','previous','latest')]
        def evidence(count, local):
            data = observed(count)
            data['booths'][key].update(vote_type='Early', fp_votes_projected={'0':150,'1':150})
            data['fit_remaining_fp_percent'] = {key:{'0':local,'1':100-local}}
            return data
        first = evidence({'0':60,'1':40},30)
        later = evidence({'0':60,'1':40},45)
        current = evidence({'0':95,'1':55},80)
        for reference, before, expected in ((old,first,30), (recent,later,45)):
            comparison = comparisons.compare(new,reference,[],current,before)
            event = next(e for e in comparison['evidence'] if e['kind'] == 'booths')
            batch = event['rule_increment']['FP']
            self.assertAlmostEqual(batch['shares']['0'],70)
            self.assertAlmostEqual(batch['remaining_shares']['0'],expected)
            self.assertEqual(batch['previous'],100)

    def test_local_fit_keeps_counted_votes_and_divides_the_unnamed_remainder(self):
        booth = dict(fp_votes_current={'61':30,'7':20,'8':10},
                     count_to_projection_ids={'61':'0'}, fp_votes_projected={'0':80,'7':50,'8':30})
        result = booth_fp.local_expectation(booth,{'0':60},booth['fp_votes_projected'])
        self.assertEqual(result, {'0':90,'7':44,'8':26})
        self.assertIsNone(booth_fp.local_expectation(booth,{'0':101},booth['fp_votes_projected']))
        self.assertIsNone(booth_fp.local_expectation(booth,{'0':50},{'0':160}))

    def test_joint_early_and_postal_update_weights_only_the_added_ballots(self):
        def unit(name, prior, added, actual, expected):
            return dict(name=name, increment=dict(previous=prior, added=added,
                        shares={'0':actual,'1':100-actual}, remaining_shares={'0':expected}))
        rows = [unit('the Early count',9000,20,40,30),unit('the Postal count',1000,80,60,50)]
        combined = rule_evidence.pooled_increment(rows)
        self.assertEqual(combined['remaining_shares'],{'0':46})
        self.assertEqual(combined['shares'],{'0':56,'1':44})
        line, gap = rule_commentary.additional_count_point(combined,'','Light','0','TCP','1')
        self.assertEqual(gap,10)
        self.assertIn('The Postal count added **80** and the Early count added **20**',line)
        self.assertIn('from **10,000** to **10,100**. Of the **100** additional TCP votes',line)
        rows[0]['increment'].pop('remaining_shares')
        self.assertFalse(rule_evidence.pooled_increment(rows)['remaining_shares'])

    def test_new_tcp_after_primary_revision_does_not_describe_guaranteed_votes_as_a_published_tally(self):
        old, new = [comparisons.summary(numerical(key)) for key in ('old', 'new')]
        earlier = observed({'0':60, '1':80}, {'0':50, '1':40})
        current = observed({'0':60, '1':80}, {'0':53, '1':48})
        earlier['booth_tcp_bounds'] = {'Light/Hall':[{'0':60,'1':80}]}
        def group():
            return comparisons.compare(new, old, [], current, earlier)
        def event(g):
            return next(e for e in g['evidence'] if e['kind'] == 'booths')
        self.assertNotIn('rule_updated_tcp_after_fp_revision', event(group()))
        earlier['issues'] = [dict(type='tcp_fp_revision', district='Light', record='Hall')]
        g = group()
        self.assertTrue(event(g)['rule_updated_tcp_after_fp_revision'])
        context = rule_evidence.context(g, g['win_changes'], [])
        text = rule_commentary.tcp_source_point({'rule_context':context}, 'Light', ('0','1'), '1',
                                                include_additions=True)
        self.assertIn('from **90** to **101**. Of the **11** additional TCP votes', text)
        self.assertIn('received **72.7%** against', text)
        self.assertNotIn('140', text)

    def test_ranked_recheck_requires_known_rates_and_bounds_candidate_movements(self):
        from live_pipeline import ranked_tcp
        old = dict(fp_votes_current={'0':700,'2':400,'3':400}, tcp_votes_current={})
        new = dict(fp_votes_current={'0':700,'2':401,'3':400}, tcp_votes_current={'0':900,'2':601})
        rates = {'0':1., '2':0., '3':.5}
        def compare(source_rates):
            return ranked_tcp.new_booth_references(('0','2'), [('Hall',old,new)], source_rates, .5, {})
        self.assertEqual(compare(rates)['Hall', frozenset(('0','2'))],
                         ({'0':900, '2':601}, ranked_tcp.RECHECK_BASIS))
        self.assertFalse(compare(None))
        new['fp_votes_current'].update({'0':670, '2':431})
        self.assertFalse(compare(rates))

    def test_booth_tcp_context_keeps_fp_order_when_the_opponent_triggers_it(self):
        group = dict(ledger='FP', units=1, rates={'1':[34.3,1.7], '0':[38.7,-11.8]},
                     tcp_estimates={'1':[44.2,46.3], '0':[55.8,53.7]})
        district = rule_commentary.PairProjection('0',50,50.1)
        def point():
            return rule_commentary.booth_tcp_context('Ripon', group, district, '1', ['1','0'])
        self.assertEqual(point(), ' Those FP votes point to **46.3%** TCP '
                         'for {candidate:Ripon:1} against {candidate:Ripon:0} at that booth, **2.1 pp** above the earlier estimate.')
        group['declaration'] = True
        self.assertIn('in that count, **2.1 pp** above', point())
        self.assertIn('**2.1 pp** below the earlier estimate.', rule_commentary.booth_tcp_context(
            'Ripon', group, rule_commentary.PairProjection('1',50,49.9), '0', ['0','1']))
        # If the FP movement carries through locally, the booth TCP still
        # provides context for a much smaller shift in the district.
        group['tcp_estimates'] = {'1':[44.2,54.2], '0':[55.8,45.8]}
        self.assertIn(', **10.0 pp** above', point())
        district = rule_commentary.PairProjection('0',50,58)
        self.assertEqual(point(),'')

    def test_dominant_pair_adds_tcp_context_for_modest_pooled_fp_changes(self):
        group = dict(ledger='FP', units=2, all_booths=True, rates={'1':[44.3,-1.6], '0':[35.7,-2.0]},
                     tcp_estimates={'1':[55.2,52.3], '0':[44.8,47.7]})
        def point(chance):
            district = rule_commentary.PairProjection('0',50.1,50.6,chance)
            return rule_commentary.booth_tcp_context('Ripon', group, district, '1', ['1','0'])
        for chance in (None, 55, 65):
            self.assertEqual(point(chance), '')
        self.assertIn('Those FP votes point to **52.3%** TCP for {candidate:Ripon:1} '
                      'against {candidate:Ripon:0} at those booths, **2.9 pp** below the earlier estimate.', point(65.01))
        del group['all_booths']
        self.assertIn('across those counts, **2.9 pp** below', point(65.01))
        group['tcp_estimates'] = {'1':[55.2,52.3], '2':[44.8,47.7]}
        self.assertEqual(point(100), '')

    def test_booth_tcp_projection_uses_same_fp_ballots_and_requires_full_coverage(self):
        unit = dict(ledger='FP', pair=(), first=True, category='Ordinary', declaration=False,
                    ballots=100, changed=100, name='Small', rates={'0':(39,-11)},
                    tcp_estimates={'0':[60,55],'1':[40,45]})
        large = dict(unit, name='Large', ballots=300, changed=300,
                     tcp_estimates={'0':[40,45],'1':[60,55]})
        result = rule_evidence.pool([unit,large],'0')[0]
        self.assertTrue(result['all_booths'])
        mixed = rule_evidence.pool([unit,dict(large,declaration=True,category='Postal')],'0')[0]
        self.assertFalse(mixed['all_booths'])
        self.assertEqual(result['tcp_estimates']['0'],[45,47.5])
        self.assertNotIn('tcp_estimates',rule_evidence.pool([unit,dict(large,tcp_estimates=None)],'0')[0])

    def test_fp_only_keeps_earlier_booth_preferences_by_source(self):
        # At the reference date the 20 Greens and 20 other voters contribute
        # 16 and 4 Labor preferences. With 30 Greens and 10 others they
        # contribute 24 and 2, with exactly the same rates.
        original = {'0':30, '1':30, '2':20, '3':20}
        actual = {'0':30, '1':30, '2':30, '3':10}
        def implied(fp, rates, benchmark=50):
            return booth_tcp.fixed_flow_shares(['0','1'], {'0':benchmark,'1':100-benchmark},
                                               original, fp, rates)
        rates = {'2':.8,'3':.2}
        self.assertAlmostEqual(implied(actual, rates)['0'][1], 56)
        # The local benchmark can imply an overall lean different from the
        # district party assumptions. Unchanged shares still give no movement.
        larger = {p:n*2 for p,n in original.items()}
        self.assertAlmostEqual(implied(larger, rates, 55)['0'][1], 55)
        self.assertEqual(implied(actual, None)['0'], [50,50])
        self.assertIsNone(implied(actual, rates, 28))
        self.assertIsNone(implied(actual, {'2':.8}))

    def test_district_tcp_benchmark_preserves_measured_and_retained_votes(self):
        def booth(projected, counted=None):
            return dict(tcp_votes_projected=projected, tcp_votes_current=counted or {}, fp_votes_projected=projected)
        booths = {'counted':booth({'0':60,'2':40}, {'0':60,'2':40}),
                  'pending':booth({'0':35,'2':65}), 'realigned':booth({'0':30,'2':70}, {'0':35,'1':65})}
        booths['pending']['fp_votes_current'] = {'0':30,'2':45,'3':25}
        bounds = {'realigned':[{'0':27,'2':65}]}
        def fitted(target):
            return booth_tcp.fit(booths, {}, ['0','2'], target, set(), bounds)
        result = fitted(45)
        self.assertAlmostEqual(sum(row['0'] for row in result.values()), 135, delta=.01)
        self.assertEqual(result['counted'], {'0':60,'2':40})
        self.assertGreaterEqual(result['realigned']['2'],65)
        self.assertGreaterEqual(result['pending']['0'],30)
        for row in result.values():
            self.assertAlmostEqual(sum(row.values()),100)
        self.assertFalse(fitted(10))
        booths['pending']['tcp_votes_projected'] = {'0':30,'1':70}
        self.assertFalse(fitted(45))

    def test_booth_tcp_follows_aligned_primaries_and_still_sums_to_district(self):
        pair = ['0','1']
        def booth(fp, labor_tcp):
            return dict(fp_votes_projected=fp, tcp_votes_projected={'0':labor_tcp,'1':100-labor_tcp})
        seed = booth({'0':30,'1':30,'2':20,'3':20}, 50)
        opening = {k:seed for k in ('east','west')}
        booths = dict(opening, counted=dict(seed,tcp_votes_current={'0':60,'1':40}),
                      retained=dict(seed,tcp_votes_current={'1':55,'3':45}))
        fp = {'east':{'0':30,'1':30,'2':30,'3':10}, 'west':{'0':30,'1':30,'2':10,'3':30},
              'counted':seed['fp_votes_projected'], 'retained':seed['fp_votes_projected']}
        bounds = {'retained':[{'0':55,'1':45}]}
        rates = {'2':.8,'3':.2}
        def fit(target):
            return booth_tcp.fit(booths, opening, pair, target, set(), bounds, fp, rates)
        # The two outstanding booths have the same major-party primaries,
        # but different minor parties. At the original rates, Labor receives
        # 56 and 44, alongside 115 already counted or retained elsewhere.
        expected = fit(53.75)
        for key, labor in {'east':56,'west':44,'counted':60,'retained':55}.items():
            self.assertAlmostEqual(expected[key]['0'],labor,places=5)
        # An FP-only return equal to the benchmark supplies no new preference
        # information, even if the stand-alone TCP projection has changed.
        booths['east'] = dict(booth(fp['east'],40), fp_votes_current=fp['east'])
        self.assertEqual(fit(53.75),expected)
        revised = fit(55)
        self.assertAlmostEqual(sum(row['0'] for row in revised.values()),220,delta=.01)
        for key in ('counted','retained'):
            self.assertEqual(revised[key],expected[key])
        for key in ('east','west'):
            self.assertGreater(revised[key]['0'],expected[key]['0'])
            for p in pair:
                self.assertGreaterEqual(revised[key][p],fp[key][p])
        self.assertFalse(fit(30))
        # A partial retained count sets a floor while the remaining votes
        # can still move. An unusually weak district share hits that floor.
        bounds['retained'] = [{'0':51,'1':20}]
        revised = fit(44)
        self.assertEqual(revised['retained']['0'],51)
        self.assertAlmostEqual(sum(row['0'] for row in revised.values()),176,delta=.01)

    def test_archived_booth_with_incompatible_seed_uses_district_transfer_share(self):
        # The opening estimate gives Labor 25 TCP from 30 Labor primaries.
        # With no credible local split, the district's 60% TCP and expected
        # 40/40/20 primaries imply all 20 donor votes flow to Labor.
        booth = dict(fp_votes_projected={'0':30,'1':40,'2':30},tcp_votes_projected={'0':25,'1':75})
        fp = {'Hall':{'0':40,'1':40,'2':20}}
        result = booth_tcp.fit({'Hall':booth}, {}, ['0','1'], 60, set(), {}, fp)
        self.assertEqual(result['Hall'],{'0':60,'1':40})
        self.assertFalse(booth_tcp.fit({'Hall':booth}, {}, ['0','1'], 65, set(), {}, fp))

    def test_rules_prefer_likely_pair_when_another_is_counted(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = comparisons.summary(numerical('later', chance=59))
        for s, share in [(old,53), (new,55)]:
            d = s['seats']['Light']
            d['candidates']['1'] = {'name':'Local Liberal', 'party':'LIB'}
            d['pairing_percent'] = {'0/1':98,'0/2':2}
            d['tcp_percentiles']['0/1'] = {'50':share}
        previous, current = observed({'0':40,'2':30}), observed({'0':80,'2':40}, {'0':70,'2':50})
        group = comparisons.compare(new, old, [], current, previous)
        packet = comparisons.prompt_packet(new, group)
        packet['close_tcp_counts']['Light'] = {'pair':['0','2'], 'counted_after':1000, 'remaining_after':6,
                                              'lead_before':20, 'lead_after':25}
        _, sections, _ = rule_commentary.write(packet, (old, new))
        text = next(s['text'] for s in sections if s['seats'] == ['Light'])
        self.assertIn('against {candidate:Light:1} rose to **55.0% (+2.0 pp)**', text)
        self.assertIn('**98.0%** of simulations', text)
        self.assertNotIn('votes projected to remain', text)

    def test_rules_regional_swings_match_booths_candidates_and_turnout(self):
        seats, booths = [], []
        def tally(labor, rival, name):
            return [{'abbreviation':'ALP','value':labor}, {'abbreviation':name,'value':rival}]
        for region, labor in [('East',60),('West',40)]:
            for i in range(6):
                seat = region+str(i%3)
                seats.append({'name':seat,'region_name':region})
                node = dict(tcp_votes_previous=tally(25,25,'LNP'), tcp_votes_current=tally(labor*3,300-labor*3,'LNP'))
                unit = dict(seat_name=seat, booth_type='Normal', vote_type='Ordinary', same_seat=True, node=node)
                booths.extend([unit, dict(unit, same_seat=False), dict(unit, vote_type='Postal'),
                               dict(unit, node=dict(node, tcp_votes_current=tally(295,5,'GRN')))])
        regions = rule_evidence.regional_counts(dict(seats=seats, booths=booths))
        rows = rule_evidence.regional_contrast({}, regions)
        self.assertEqual([(r['region'],r['swing'],r['booths']) for r in rows], [('East',10,6),('West',-10,6)])
        self.assertEqual(rule_evidence.regional_contrast(regions, regions), [])
        self.assertFalse(rule_evidence.regional_updates({}, regions))
        later = deepcopy(regions)
        east = later['tcp']['East']
        east.update(votes=4300, gain=430, booths=12)
        self.assertFalse(rule_evidence.regional_updates(regions, later))  # More booths, same swing.
        east.update(votes=4299, gain=258)
        self.assertFalse(rule_evidence.regional_updates(regions, later))  # 2,499 additional matched ballots.
        east['votes'] = 4300
        revisions = rule_evidence.regional_updates(regions, later)
        self.assertNotIn('previous', revisions)
        change = revisions['period']
        self.assertEqual((change['region'], change['before'], change['after']), ('East', 10, 6))

        old, new = [comparisons.summary(numerical(k)) for k in ('old', 'new')]
        group = comparisons.compare(new, old, [], observed({'0':200}), observed({'0':100}))
        group.update(rule_regional=rows, rule_regional_changes=revisions)
        packet = comparisons.prompt_packet(new, group)
        request = dict(key='same endpoints', packet=packet, rule_context=rule_commentary.comparison_context(group, packet))
        items = {w:{'status':'unavailable'} for w in ('previous','15m','since_start')}
        reports = rule_commentary.append_editions(items, dict.fromkeys(items,request), 'new')
        self.assertNotIn('at matched election-day booths', reports['previous']['text']['developments'][0]['text'])
        for w, expected in [('15m','changed from **10.0 pp** towards'), ('since_start','compared with the previous election')]:
            self.assertIn(expected, reports[w]['text']['developments'][0]['text'])
        # With sufficient new counting the short report gives the additions;
        # the rolling report for the same endpoints gives the cumulative total.
        regions['tcp']['East'].update(votes=3000, gain=300)
        east.update(votes=8000, gain=480)
        group['rule_regional_changes'] = rule_evidence.regional_updates(regions, later)
        request['rule_context'] = rule_commentary.comparison_context(group, packet)
        reports = rule_commentary.append_editions(items, dict.fromkeys(items,request), 'new')
        self.assertIn('grew by **6 booths** and **5,000 votes**', reports['previous']['text']['developments'][0]['text'])
        self.assertIn('reached **12 booths**', reports['15m']['text']['developments'][0]['text'])

    def test_single_update_regional_swing_requires_size_and_new_booths(self):
        def counts(votes, booths, swing):
            return dict(votes=votes, booths=booths, gain=votes*swing/100, seats=3)
        before = {'fp': {'East':counts(2500, 6, 0), 'West':counts(4000, 10, 0)}}
        def changes(votes=7500, booths=11, swing=-2.5):
            # West has a larger shift on just two new booths. East can earn
            # the single-update headline independently of the rolling headline.
            after = {'fp': {'East':counts(votes, booths, swing), 'West':counts(10000, 12, 5)}}
            return rule_evidence.regional_updates(before, after)
        revisions = changes()
        self.assertEqual(revisions['period']['region'], 'West')
        self.assertEqual((revisions['previous']['region'], revisions['previous']['added_votes'],
                          revisions['previous']['added_booths']), ('East', 5000, 5))
        for settings in (dict(votes=7499), dict(booths=10), dict(swing=2.49)):
            with self.subTest(settings=settings):
                self.assertNotIn('previous', changes(**settings))
        before['fp']['East'] = counts(2499, 6, 0)
        self.assertNotIn('previous', changes())
        before['fp']['East'] = counts(50000, 60, 0)
        self.assertNotIn('previous', changes(votes=55000, booths=66))

    def test_rules_shorten_crowded_long_window_but_keep_short_update_detail(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = comparisons.summary(numerical('later', chance=60))
        for s in (old,new):
            s['seats']['Extra'] = deepcopy(s['seats']['Light'])
        def counts(fp):
            return dict(observed_counts_available=True, booths={seat+'/Hall': observed(fp, seat=seat)['booths'][seat+'/Hall']
                        for seat in new['seats']})
        group = comparisons.compare(new, old, [], counts({'0':150,'2':100}), counts({'0':100,'2':100}))
        packet = comparisons.prompt_packet(new, group)
        local = rule_commentary.comparison_context(group, packet)
        packet['actual_seconds'] = 900
        self.assertEqual(len(rule_commentary.write(packet, (old,new), local)[1]), 3)
        packet['actual_seconds'] = 3600
        summary, sections, _ = rule_commentary.write(packet, (old,new), local)
        self.assertIn('**5 districts**', summary)
        self.assertEqual(len(sections), 2)
        self.assertLessEqual(sections[1]['text'].count('\n- '), 2)

    def test_rules_require_fresh_matched_donor_ballots_for_a_flow_headline(self):
        flow = {'parties':['2','0','1'], 'change_pp':4}
        def event(seat, earlier, fp, tcp):
            return dict(kind='booths', seat=seat, changed_votes=500, before=earlier,
                        after=dict(fp_votes_current=fp,tcp_votes_current=tcp))
        fp, tcp = {'0':50,'1':50,'2':100}, {'0':140,'1':60}
        first = event('East', {}, fp, tcp)
        self.assertFalse(rule_evidence.supported_flows([first], [flow]))
        self.assertFalse(rule_evidence.supported_flows([first,event('West',{},fp,{'0':140,'3':60})],[flow]))
        second = event('West', {}, fp, tcp)
        self.assertEqual(rule_evidence.supported_flows([first,second],[flow]), [flow])
        for e in (first,second):
            e['before'] = dict(fp_votes_current=fp,tcp_votes_current={'0':139,'1':61})
        self.assertFalse(rule_evidence.supported_flows([first,second],[flow]))

    def test_overview_counts_both_sides_of_six_district_moves_while_previous_keeps_detail(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = comparisons.summary(numerical('later', chance=63))
        for side in (old, new):
            for seat in ('Five','Six'):
                side['seats'][seat] = deepcopy(side['seats']['Light'])
        group = comparisons.compare(new, old, [], observed({'0':400}), observed({'0':200}))
        packet = comparisons.prompt_packet(new, group)
        local = rule_commentary.comparison_context(group, packet)
        self.assertEqual(len(local['major_moves']), 6)
        one = {'key':'shared','packet':packet,'rule_context':local}
        reports = rule_commentary.append_editions({w:{} for w in ('previous','15m')},
                        {w:one for w in ('previous','15m')}, 'later', {'earlier':old,'later':new}.__getitem__)
        self.assertGreater(len(reports['previous']['text']['developments']), 1)
        overview, = reports['15m']['text']['developments']
        self.assertIn('**6 districts**', overview['text'])
        self.assertIn('**+13.0 pp**', overview['text'])
        # At exactly 25 points a district does not qualify. Removing the sixth
        # also restores detail; the number of party changes is immaterial.
        rows = [dict(row, change_pp=12.5 if row['change_pp'] > 0 else -12.5)
                if row['seat'] == 'Six' else row for row in group['win_changes']]
        local['major_moves'] = rule_evidence.major_district_moves(rows)
        self.assertEqual(len(local['major_moves']), 5)
        self.assertTrue(rule_commentary.write(packet, (old,new), local, '1h')[1])

    def test_local_tcp_discrepancy_checks_actual_fp_reallocations(self):
        old = comparisons.summary(numerical('earlier', chance=50))
        new = comparisons.summary(numerical('later', chance=66))
        for side, fp, tcp in [(old,33.5,49), (new,34.8,51)]:
            seat = side['seats']['Light']
            seat['fp_percentiles']['0']['50'] = fp
            seat['tcp_percentiles']['0/2']['50'] = tcp
        previous = observed({'0':55,'2':45})
        previous['booths']['Light/Hall']['tcp_votes_projected'] = {'0':55,'2':45}
        previous['booths']['Light/Postal'] = dict(name='Postal', seat_name='Light', fp_votes_current={},
            tcp_votes_current={}, fp_votes_projected={'0':43,'2':57}, tcp_votes_projected={'0':43,'2':57})
        for fp, unchanged in [({'0':55,'2':45}, True), ({'0':54,'2':46}, False)]:
            current = observed(fp, {'0':50,'2':50})
            current['booths']['Light/Postal'] = previous['booths']['Light/Postal']
            group = comparisons.compare(new,old,[],current,previous)
            packet = comparisons.prompt_packet(new,group)
            local = rule_commentary.comparison_context(group,packet)
            flags = []
            _, sections, _ = rule_commentary.write(packet,(old,new),local,'previous',flags)
            text = next(s['text'] for s in sections if s['seats']==['Light'])
            self.assertTrue(any(f['seat']=='Light' for f in flags))
            self.assertIn('alone does not explain',text)
            self.assertEqual('local FP counts were unchanged' in text,unchanged)

    def test_regional_spread_measures_distances_even_when_all_swings_favour_one_side(self):
        seats, booths = [], []
        for name, gain in [('A',1),('B',5),('C',13)]:
            seats.append(dict(name=name,region_name='East'))
            def tally(a):
                return [dict(abbreviation='ALP',value=a),dict(abbreviation='LIB',value=400-a)]
            unit = dict(seat_name=name, same_seat=True, booth_type='Normal', vote_type='Ordinary',
                        node=dict(tcp_votes_previous=tally(200), tcp_votes_current=tally(200+4*gain)))
            booths.extend([unit]*3)
        row = rule_evidence.regional_counts(dict(seats=seats, booths=booths))['tcp']['East']
        self.assertAlmostEqual(row['mean_absolute_deviation_pp'], 40/9)

    def test_firmer_tcp_lead_explains_quiet_win_gain_despite_weaker_primary(self):
        old = comparisons.summary(numerical('earlier', chance=99.36))
        new = comparisons.summary(numerical('later', chance=99.9575))
        before = observed({'0': 300, '2': 300}, {'0': 315, '2': 285})
        after = observed({'0': 340, '2': 360}, {'0': 367, '2': 333})
        for snapshot, ballots in [(before, 1600), (after, 1500)]:
            snapshot['booths']['Light/Hall']['tcp_votes_projected'] = {'0': ballots//2, '2': ballots//2}
        old['seats']['Light']['tcp_percentiles'] = {'0/2': {'5':50.5, '50':51.4, '95':52.2}}
        # The later export lists the opponent first. Its upper bound is
        # Labor's lower bound, so both intervals still describe Labor.
        district = new['seats']['Light']
        district['pairing_percent'] = {'2/0':100.}
        bands = {'5':48.3, '50':48.7, '95':49.2}
        district['tcp_percentiles'] = {'2/0': bands}
        packet = comparisons.prompt_packet(new, comparisons.compare(new, old, [], after, before))
        def write():
            return rule_commentary.write(packet, (old, new))[:2]
        headline, sections = write()
        self.assertIn('**99.96% (+0.6 pp)**', headline)
        detail = sections[0]['text']
        self.assertNotIn('**30** to **34** votes', detail)
        self.assertNotIn('TCP votes still to come', detail)
        self.assertIn('At Hall,', detail)
        self.assertIn('**51.3% (-0.1 pp)**', detail)
        self.assertIn('range of likely results narrowed', detail)
        # A close race can also reach the ordinary district-story threshold.
        packet['win_changes'][0].update(before=98.5, change_pp=1.4575)
        self.assertIn('more confident', write()[1][0]['text'])
        bands['50'] = 48.5
        self.assertIn('rose to **51.5% (+0.1 pp)**, and the range of likely results narrowed', write()[1][0]['text'])
        bands['50'] = 48.7
        packet['win_changes'][0].update(before=99.36, change_pp=.5975)

        # A primary result below expectations supplies no explanation by
        # itself. A different possible final pair, a missing bound or a range
        # shifting down towards defeat should leave only the local update.
        for frequencies, upper in [({'2/0': 75.}, 49.2), ({'2/0':100.}, None), ({'2/0':100.}, 50.1)]:
            district['pairing_percent'] = frequencies
            bands['95'] = upper
            headline, sections = write()
            self.assertIn('Hall', headline)
            self.assertNotIn('below the earlier estimate', headline)
            self.assertEqual(sections, [])
        district['pairing_percent'] = {'2/0':100.}
        bands['95'] = 49.2
        packet['input_revision_changed'] = True
        self.assertNotIn('more confident', str(write()))

    def test_narrower_tcp_range_when_two_final_opponents_remain_plausible(self):
        old, new = [comparisons.summary(numerical(key)) for key in ('earlier', 'later')]
        for summary, frequency, win, overall, bands in [
                (old, 50.41, 85.796, 92.67, [48.13, 53.42, 58.48]),
                (new, 50.09, 96.666, 98.16, [50.22, 52.31, 54.24])]:
            district = summary['seats']['Light']
            district['candidates']['1'] = {'party':'LIB', 'name':'Liberal candidate'}
            district['win_percent'] = {'0':.17, '1':overall, '2':100-overall-.17}
            # The new export lists the challenger first in this matchup.
            key = '1/2' if summary is old else '2/1'
            district['pairing_percent'] = {key:frequency, '0/1':100-frequency}
            shown = bands if summary is old else [100-v for v in reversed(bands)]
            district['tcp_percentiles'] = {key:dict(zip(('5','50','95'), shown))}
            summary['conditional_wins'] = {'Light':{key:win if summary is old else 100-win}}
        before = observed({'1':4000,'2':3500}, {'1':520,'2':480})
        after = observed({'1':4000,'2':3500}, {'1':4070,'2':3430})
        before['booths']['Light/Hall']['tcp_votes_projected'] = {'1':4125,'2':3375}
        group = comparisons.compare(new, old, [], after, before)
        packet = comparisons.prompt_packet(new, group)
        def report():
            sections = rule_commentary.write(packet, (old,new), rule_commentary.comparison_context(group,packet))[1]
            return next(s['text'] for s in sections if s['seats'] == ['Light'])
        text = report()
        self.assertIn('**52.3% (-1.1 pp)**', text)
        self.assertIn('range of likely results narrowed', text)
        self.assertIn('more confident that {candidate:Light:1} will hold the lead', text)
        self.assertIn('**85.8%** to **96.7%**', text)
        # A narrower interval alone does not establish how this contestant's
        # conditional win rate contributed to the movement across all draws.
        new['conditional_wins']['Light']['2/1'] = 12
        self.assertNotIn('range of likely results narrowed', report())
        new['conditional_wins']['Light'].clear()
        self.assertNotIn('range of likely results narrowed', report())

    def test_win_outlook_distinguishes_rare_results_from_simulation_endpoints(self):
        def text(after, change=.6):
            return rule_commentary.win_outlook({'seat':'Light', 'party_id':'0', 'after':after, 'change_pp':change})
        self.assertIn('**100%** of simulations **(+13.0 pp)**.', text(100, 13.0))
        self.assertIn('**0%** of simulations **(-12.7 pp)**.', text(0, -12.7))
        self.assertTrue(text(100, .01).endswith('**100%** of simulations.'))
        self.assertIn('99.96%', text(99.9575))
        self.assertIn('over 99.99%', text(99.9975))
        self.assertIn('less than 0.01%', text(.0025))
        self.assertIn('0.04%', text(.0425))

    def test_rule_headline_checks_the_chance_of_reaching_the_higher_seat_total(self):
        def forecast(identity, median, counts):
            value = numerical(identity)
            value['report'].update(partyAbbr=[[0,'ALP'], [1,'LNP'], [-4,'LNP']],
                seatCountFrequencies=[[0,[1,median,4]], [1,[0,0,1]]],
                coalitionSeatCountFrequencies=[1,median,4])
            snapshot = self.root/(identity+'.json')
            value['diagnostic_file'] = str(snapshot)
            atomic(snapshot, {'simulation_report': {
                'party_seat_win_frequency': [{'party_index':0, 'values':counts},
                                             {'party_index':1, 'values':[900,100]}],
                'coalition_seat_win_frequency': counts}})
            package = put(self.root, value)
            saved = comparisons.summary(value, self.root)
            del saved['chance_at_least_seats']
            atomic(package/'summary.json', saved)
            return comparisons.comparison_summary(package)

        before = forecast('earlier', 2, [0, 101, 500, 320, 79])  # 39.9% win at least three
        after = forecast('later', 3, [0, 100, 399, 440, 61])     # 50.1% win at least three
        self.assertEqual(after['chance_at_least_seats']['-4']['3'], 50.1)
        self.assertNotIn('1', after['chance_at_least_seats'])
        def headline(first, second):
            medians = [s['statewide']['medianSeats'] for s in (first, second)]
            return rule_commentary.seat_total_headline(*medians, second['parties'], (first, second))
        self.assertIn('{party:-4} rose from **2** to **3**', headline(before, after))
        self.assertIn('{party:0} fell from **3** to **2**', headline(after, before))
        narrow = forecast('narrow', 2, [0, 101, 400, 420, 79])   # 49.9% win at least three
        self.assertIsNone(headline(narrow, after))
        del after['chance_at_least_seats']
        self.assertIsNone(headline(before, after))
        self.assertIn('rose from **1** to **3**',
                      rule_commentary.seat_total_headline({'0': 1}, {'0': 3}, before['parties'], None))

    def test_headlines_acknowledge_other_reporting_units_in_the_same_district(self):
        before = comparisons.summary(numerical('earlier', chance=60))
        old = observed({'0': 100, '2': 100}, {'0': 100, '2': 100})
        new = observed({'0': 180, '2': 120}, {'0': 190, '2': 110})
        for evidence in (old, new):
            evidence['booths']['Light/Postal'] = dict(old['booths']['Light/Hall'], name='Postal', vote_type='Postal')
        old['booths'].update(observed({'0': 10}, seat='Hammond')['booths'])
        new['booths'].update(observed({'0': 100}, seat='Hammond')['booths'])
        selected = {'outcome': 'In {seat:Light}, {candidate:Light:0} gained ground.',
                    'booth_record': 'Light/Hall', 'candidate': '{candidate:Light:0}', 'count_type': 'FP'}

        def comparison(chance=70):
            after = comparisons.summary(numerical('later', chance=chance))
            packet = comparisons.prompt_packet(after, comparisons.compare(after, before, [], new, old))
            # Only Hall's figures are needed to compose the headline. The
            # other unit can be below both the booth list and evidence cutoffs.
            packet['evidence'] = [e for e in packet['evidence'] if e['record'] != 'Light/Postal']
            return packet

        def ai_headline(packet):
            reply = {'key_changes': [selected], 'developments': [], 'insufficient_evidence': False}
            return commentary.format_points(reply, commentary.prepare_packet(packet), link_local_headlines=True)['summary']

        packet = comparison()
        self.assertIn('following the result at Hall.', ai_headline(packet))
        self.assertIn('as the result at Hall was updated.', rule_commentary.write(packet)[0])
        # Both FP and TCP were revised at Hall, and Hammond reported too.
        # Only a separate Light count should change the headline wording.
        new['booths']['Light/Postal']['fp_votes_current'] = {'0': 101, '2': 100}
        packet = comparison()
        self.assertEqual(packet['booth_changes']['by_seat']['Light']['updated']['total'], 1)
        self.assertIn('following the result at Hall and other counting in the district.', ai_headline(packet))
        self.assertIn('with updates to the result at Hall and other local counts.', rule_commentary.write(packet)[0])
        self.assertIn('Other local counts also changed.', rule_commentary.write(comparison(60.75))[0])

        # In an older packet the booth lists alone can establish that two
        # units changed, even if only one has a detailed comparison.
        new['booths']['Light/Postal']['fp_votes_current'] = {'0': 130, '2': 100}
        packet = comparison()
        del packet['booth_changes']['seats_with_multiple_changed_units']
        self.assertIn('and other counting in the district.', ai_headline(packet))

    def test_headline_follows_the_leader_in_two_way_races_and_largest_mover_elsewhere(self):
        def focus(before, after):
            rows = [{'seat': 'Light', 'party_id': p, 'change_pp': after[p]-v, 'after': after[p],
                     'maximum_within_window': max(v, after[p]), 'minimum_within_window': min(v, after[p])}
                    for p, v in before.items()]
            packet = {'win_changes': rows, 'all_seats': {'Light': after},
                      'observed_count_changes_by_seat': {'Light': {'affected_votes': 200}}}
            entities = {rule_commentary.candidate('Light', p) for p in before}
            return rule_commentary.district_order(packet, entities)[0]['party_id']
        self.assertEqual(focus({'1': 5, '4': 91.4, '3': 3.6}, {'1': 16.1, '4': 80.4, '3': 3.5}), '4')
        self.assertEqual(focus({'0': 45, '1': 49, '4': 6}, {'0': 50, '1': 45, '4': 5}), '0')
        self.assertEqual(focus({'0': 48, '1': 40, '4': 12}, {'0': 50, '1': 25, '4': 25}), '1')

    def test_rules_include_both_material_rivals_in_a_three_way_change(self):
        def rivals(moves, changed_identity=None):
            rows = [dict(seat='Shepparton', party_id=p, change_pp=d, identity_changed=p==changed_identity)
                    for p, d in moves.items()]
            entities = {rule_commentary.candidate(r['seat'], r['party_id']) for r in rows}
            result = rule_commentary.opposing_win_moves(dict(win_changes=rows, entities=entities), rows[0])
            return [r['party_id'] for r in result]
        self.assertEqual(rivals({'4':17.26,'3':-7.15,'1':-10.11}), ['1','3'])
        self.assertEqual(rivals({'4':-17.26,'3':7.15,'1':10.11}), ['1','3'])
        self.assertEqual(rivals({'4':17.26,'3':-7.15,'1':-10.11}, changed_identity='3'), ['1'])
        self.assertEqual(rivals({'4':17,'3':-2,'1':-15}), ['1'])
        self.assertEqual(rivals({'4':24,'3':-4,'1':-20}), ['1'])

    def test_quiet_third_contender_and_extra_local_stories_on_a_busy_step(self):
        rows = [dict(seat=str(i), party_id='0', after=50, change_pp=change,
                     minimum_within_window=50-change, maximum_within_window=50)
                for i, change in enumerate([25, 18, 16, 12, 10, 9, 7])]
        entities = {rule_commentary.candidate(r['seat'], '0') for r in rows}
        packet = dict(win_changes=rows, entities=entities, all_seats={r['seat']:{'0':50} for r in rows},
                      observed_count_changes_by_seat={r['seat']:{'affected_votes':100} for r in rows})
        self.assertEqual(len(rule_commentary.district_order(packet, entities)), 3)
        def extended():
            return [r['seat'] for r in rule_commentary.district_order(packet, entities, single_update=True)]
        self.assertEqual(extended(), ['0','1','2','3','4'])
        del packet['observed_count_changes_by_seat']['3'], packet['observed_count_changes_by_seat']['4']
        self.assertEqual(extended(), ['0','1','2','5'])
        for party in ('1','4'):
            entities.add(rule_commentary.candidate('0', party))
        packet['all_seats']['0'] = {'0':50, '1':33, '4':17}
        text = rule_commentary.other_contenders(packet, rows[0], [dict(party_id='1')])
        self.assertIn('{candidate:0:4} also has a win chance of **17.0%**', text)
        packet['all_seats']['0']['4'] = 17.05
        rows.append(dict(seat='0', party_id='4', after=17.049))
        self.assertIn('**17.0%**', rule_commentary.other_contenders(packet, rows[0], [dict(party_id='1')]))
        rows.pop()
        packet['all_seats']['0']['4'] = 3
        self.assertEqual(rule_commentary.other_contenders(packet, rows[0], [dict(party_id='1')]), '')

    def test_broader_preferences_explain_only_a_matching_classic_district_revision(self):
        endpoints = [comparisons.summary(numerical(key)) for key in ('old', 'new')]
        for side in endpoints:
            side['seats']['Light']['pairing_percent'] = {'0/1':83}
            side['conditional_wins'] = {'Light':{'0/1':55}}
        parts = dict(primary_change_pp=.02, measured_change_pp=.06, learned_change_pp=-.53,
                     broad_preference_change_pp=-.54)
        packet = {'rule_context':{'tpp_components':{'Light':parts}}}
        row = dict(seat='Light', party_id='0')
        projection = rule_commentary.PairProjection('1', 50.9, 50.4)
        def explanation():
            return rule_commentary.preference_offset(packet, row, projection, endpoints)
        self.assertIn('broader counting trends', explanation())
        del parts['broad_preference_change_pp']
        self.assertNotIn('broader', explanation())
        parts['measured_change_pp'] = .8
        self.assertIsNone(explanation())
        parts['measured_change_pp'] = .06
        self.assertIsNone(rule_commentary.preference_offset(packet, row, projection._replace(opponent='2'), endpoints))

    def test_further_published_count_for_an_earlier_realignment(self):
        from live_pipeline import tcp_reporting
        def forecast(identity, votes, units, chance):
            full = comparisons.summary(numerical(identity, chance=chance))
            full['published_district_tcp'] = {'Light': {'candidates': votes, 'positive_units': units}}
            return full
        original = forecast('f00000001', {'100': 63, '300': 37}, 1, 50)
        earlier = forecast('f00000002', {'100': 58, '200': 42}, 1, 60)
        current = forecast('f00000003', {'100': 158, '200': 142}, 2, 70)
        old = observed({'0': 58, '2': 42}, {'0': 58, '2': 42})
        new = deepcopy(old)
        new['booths']['Light/East'] = dict(old['booths']['Light/Hall'], name='East',
                                           tcp_votes_current={'0': 100, '2': 100})
        def packet(history):
            pairs = tcp_reporting.continuing_realigned_pairs(earlier, current, history)
            return comparisons.prompt_packet(current, comparisons.compare(current, earlier, [], new, old,
                                                                           continuing_pairs=pairs))
        data = packet([original, earlier])
        account = data['close_tcp_counts']['Light']
        self.assertEqual(account['continued_realigned_count'], {'net_added':200, 'units_before':1, 'units_after':2})
        rules = rule_commentary.write(data)[1][0]['text']
        self.assertIn('grew by **200 votes** and now covers **2 reporting units**, up from **1**', rules)
        reply = {'key_changes': ['In {seat:Light}, the outlook changed.'], 'developments': [
            {'seat': 'Light', 'seat_changes': ['The win chance for {candidate:Light:0} rose.'], 'booth_evidence': []}],
            'insufficient_evidence': False}
        ai = commentary.format_points(reply, commentary.prepare_packet(data), contextualise_counts=True)
        self.assertIn('The revised TCP count', ai['developments'][0]['text'])
        self.assertNotIn('continued_realigned_count', packet([earlier, original])['close_tcp_counts']['Light'])
        # Equal district totals cannot establish which candidates got the votes.
        # An inconsistent feed or a set of retained booths supplies no such claim.
        current['published_district_tcp']['Light']['candidates'] = {'100': 159, '200': 141}
        self.assertNotIn('continued_realigned_count', packet([original, earlier])['close_tcp_counts']['Light'])

    def test_rules_explain_both_a_better_opponent_and_improvement_against_another(self):
        old, new = [comparisons.summary(numerical(key)) for key in ('old','new')]
        for side, frequency, strength in [(old,50,25),(new,60,35)]:
            d = side['seats']['Light']
            d['candidates']['1'] = {'name':'Liberal candidate','party':'LIB'}
            d['pairing_percent'] = {'0/1':frequency,'1/2':100-frequency}
            d['win_percent']['1'] = frequency*.8 + (100-frequency)*strength/100
            side['conditional_wins'] = {'Light':{'0/1':20,'1/2':strength}}
        def explain(a,b):
            delta = b['seats']['Light']['win_percent']['1'] - a['seats']['Light']['win_percent']['1']
            return pairing_commentary.explain(dict(seat='Light',party_id='1',change_pp=delta),(a,b))
        self.assertIn('**50.0%** to **60.0%**',explain(old,new))
        self.assertIn('**25.0%** to **35.0%**',explain(old,new))
        self.assertIn('also fell',explain(new,old))
        new['seats']['Light']['candidates']['2']['name'] = 'Replacement'
        self.assertIsNone(explain(old,new))

    def test_rule_writer_explains_a_favoured_opponent_disappearing_from_the_final_two(self):
        def forecast(identity, frequency, strength, reverse=False):
            summary = comparisons.summary(numerical(identity))
            summary['iterations'] = 40000
            seat = summary['seats']['Light']
            seat['candidates']['1'] = {'party': 'LIB', 'name': 'Fictional Liberal candidate'}
            pair = '2/1' if reverse else '1/2'
            # Candidate 2 always wins the alternative contest against 0.
            # Within 1 vs 2, candidate 1 wins the stated share of simulations.
            seat['pairing_percent'] = {pair: frequency, '0/2': 100-frequency}
            seat['win_percent'] = {'1': frequency*strength/100, '2': 100-frequency*strength/100, '0': 0}
            summary['conditional_wins'] = {'Light': {pair: 100-strength if reverse else strength, '0/2': 0}}
            return summary

        old = forecast('f00000001', 20, 80)
        new = forecast('f00000002', 5, 80, reverse=True)
        group = comparisons.compare(new, old, [], observed({'0': 200}), observed({'0': 80}))
        packet = comparisons.prompt_packet(new, group)
        summaries = {s['forecast_id']: s for s in (old, new)}
        requests = {'previous': {'key': 'example', 'packet': packet}}
        ai = {'previous': {'status': 'complete', 'text': {'summary': 'AI report', 'developments': []}}}
        editions = rule_commentary.append_editions(ai, requests, new['forecast_id'], summaries.__getitem__)
        section = next(s['text'] for s in editions['previous']['text']['developments'] if s['seats'] == ['Light'])
        self.assertIn('The latest forecast still favours {candidate:Light:1} to beat '
                      '{candidate:Light:2} if they reach the final two.', section)
        self.assertIn('fell from **20.0%** to **5.0%**', section)
        self.assertIn('gain in win chance for {candidate:Light:2}', section)
        self.assertEqual(ai['previous']['text']['developments'], [])
        missing = unittest.mock.Mock(side_effect=FileNotFoundError('summary'))
        with patch('sys.stdout', new_callable=io.StringIO):
            fallback = rule_commentary.append_editions(ai, requests, new['forecast_id'], missing)
        ordinary = next(s['text'] for s in fallback['previous']['text']['developments'] if s['seats'] == ['Light'])
        self.assertNotIn('accounting for most', ordinary)

        def explain(previous, current, subject='1'):
            movement = current['seats']['Light']['win_percent'][subject] - previous['seats']['Light']['win_percent'][subject]
            return pairing_commentary.explain({'seat': 'Light', 'party_id': subject, 'change_pp': movement}, (previous, current))

        self.assertIn('drop in win chance for {candidate:Light:1}', explain(old, new))
        self.assertIn('rose from **5.0%** to **20.0%**', explain(new, old))
        # A shift predominantly in who wins the matchup is a different explanation.
        self.assertIsNone(explain(old, forecast('f00000002', 18, 60)))
        for change in ('few draws', 'missing rate', 'missing pair', 'changed identity', 'changed inputs'):
            changed = deepcopy(new)
            if change == 'few draws':
                changed['iterations'] = 100
            elif change == 'missing rate':
                del changed['conditional_wins']['Light']['2/1']
            elif change == 'missing pair':
                del changed['seats']['Light']['pairing_percent']['2/1']
            elif change == 'changed identity':
                changed['seats']['Light']['candidates']['1']['name'] = 'Another candidate'
            else:
                changed['input_revision'] = 'later revision'
            with self.subTest(change=change):
                self.assertIsNone(explain(old, changed))
        # Candidate 1 now always qualifies, but faces different opponents.
        # Losing their favoured matchup no longer means missing the final two.
        for endpoint in (old, new):
            pairs = endpoint['seats']['Light']['pairing_percent']
            pairs['0/1'] = pairs.pop('0/2')
            del endpoint['conditional_wins']['Light']['0/2']
            endpoint['conditional_wins']['Light']['0/1'] = 100
            wins = endpoint['seats']['Light']['win_percent']
            wins['0'] = pairs['0/1']
            wins['2'] = 100-wins['1']-wins['0']
        self.assertIn('still favours {candidate:Light:0} to beat {candidate:Light:1} if they reach the final two',
                      explain(old, new))

    def test_smaller_projected_remainder_explains_gain_in_continuing_close_tcp(self):
        before = comparisons.summary(numerical('f00000001', chance=88.5))
        after = comparisons.summary(numerical('f00000002', chance=90.2))
        old = observed({'0': 2250, '2': 2200}, {'0': 2077, '2': 2000})
        new = deepcopy(old)
        old['booths']['Light/Hall']['tcp_votes_projected'] = {'0': 2300, '2': 2253}
        new['booths']['Light/Hall']['tcp_votes_projected'] = {'0': 2210, '2': 2174}
        # Something was counted elsewhere in the election. In Light only the
        # projection changed. The counted pair meets in about two thirds of
        # draws, and the order of the IDs in the diagnostics can change.
        new['booths'].update(observed({'0': 61}, seat='Hammond')['booths'])
        for summary, key, win in ((before, '0/2', 88.3), (after, '2/0', 9.2)):
            summary['seats']['Light']['pairing_percent'] = {key: 68, '0/1': 32}
            summary['conditional_wins'] = {'Light': {key: win}}

        def comparison(previous, current):
            return comparisons.compare(current, previous, [], new, old)

        flag = 'fewer_votes_favour_counted_leader'
        group = comparison(before, after)
        count = group['close_tcp_counts']['Light']
        self.assertTrue(count[flag])
        packet = commentary.prepare_packet(comparisons.prompt_packet(after, group))
        headline, sections, _ = rule_commentary.write(packet, (before, after))
        text = next(s['text'] for s in sections if s['seats'] == ['Light'])
        for phrase in ('90.2% (+1.7 pp)', 'at both updates', '77 votes', '307 votes',
                       '476 votes', '{candidate:Light:2} needing a larger share'):
            self.assertIn(phrase, text)
        self.assertIn('fewer TCP votes projected to remain', headline)
        commentary_format.check_text(text, ['Light'], packet)
        messages = prompt_layout.input_messages(packet, commentary.INSTRUCTIONS, {})
        ai = json.loads(messages[1]['content'][-1]['text'].split('\n', 1)[1])
        self.assertEqual(ai[flag], {'Light': ['0', '2']})
        for overrides in ({'remaining_before': None}, {'remaining_after': 0}, {'remaining_after': 475},
                          {'remaining_after': 600}, {'lead_before': -10}, {'lead_after': 30}):
            with self.subTest(overrides=overrides):
                self.assertFalse(pairing_commentary.fewer_votes_favour_counted_leader(
                    'Light', dict(count, **overrides), before, after))
        for reason in ('different inputs', 'missing conditional wins', 'gain from a different matchup',
                       'tiny win change', 'trailer gains', 'published reset'):
            current = deepcopy(after)
            if reason == 'different inputs':
                current['input_revision'] = 'new'
            elif reason == 'missing conditional wins':
                del current['conditional_wins']
            elif reason == 'gain from a different matchup':
                current['conditional_wins']['Light']['2/0'] = 11.7
            elif reason == 'published reset':
                current['published_district_tcp'] = {'Light': {'candidates': {'100': 0, '200': 0}, 'positive_units': 0}}
            else:
                current['seats']['Light']['win_percent']['0'] = 88.6 if reason == 'tiny win change' else 85
            with self.subTest(reason=reason):
                self.assertNotIn(flag, comparison(before, current)['close_tcp_counts'].get('Light', {}))
        # With a certain pairing, the overall win percentages already
        # describe this one contest even without separate matchup diagnostics.
        for summary in (before, after):
            summary['seats']['Light']['pairing_percent'] = {'0/2': 100}
            del summary['conditional_wins']
        self.assertTrue(comparison(before, after)['close_tcp_counts']['Light'][flag])

    def test_rule_writer_separates_counted_votes_from_estimates_for_the_same_ballots(self):
        before = comparisons.summary(numerical('f00000001', '2026-03-21T18:00:00', 90))
        after = comparisons.summary(numerical('f00000002', '2026-03-21T18:20:00', 97))
        group = comparisons.compare(after, before, [], observed({'0': 500, '2': 400}), observed({'0': 45, '2': 30}))
        packet = comparisons.prompt_packet(after, group)
        packet['close_tcp_counts'] = {'Light': {'pair':['0','2'], 'lead_before':-692, 'lead_after':58,
            'net_by_candidate':{'0':850, '2':100}, 'remaining_before':17132,
            'remaining_after':16182, 'approx_remaining_tcp_percent_for_first':63.4, 'count_and_projection_diverge':True}}
        packet['ranked_tcp_groups'] = {'Light': [{'category':'Early', 'pair':['2','0'], 'ballots':2453,
            'newly_counted':2338, 'units':1, 'largest_reporting_booths':['Hall'], 'first_share_before':48.8,
            'first_share_after':46.8}]}
        packet['category_transfers'] = [{'seat':'Hammond', 'from':'Absent', 'to':'Postal', 'fp_votes':500, 'tcp_votes':500}]
        packet['input_revision_changed'] = True
        summary, districts, _ = rule_commentary.write(packet)
        self.assertIn('Forecast inputs also changed', summary)
        self.assertIn('**500** FP votes appear to have been moved', summary)
        text = next(d['text'] for d in districts if d['seats'] == ['Light'])
        self.assertIn('97.0% (+7.0 pp)', text)
        self.assertNotIn('2,453', text)
        self.assertNotIn('692 votes', text)
        self.assertNotIn('16,182', text)
        self.assertNotIn('2,338', text)
        self.assertEqual(rule_commentary.write(packet), rule_commentary.write(packet))

        # With all the earlier ballots now counted at two booths, the second
        # percentage is a measured result. It must not be described as an estimate.
        completed = dict(packet['ranked_tcp_groups']['Light'][0], newly_counted=2453,
                         units=2, largest_reporting_booths=['Hall','West'])
        point = rule_commentary.ranked_count_detail({'ranked_tcp_groups':{'Light':[completed]}}, 'Light', '2').point
        self.assertIn('Across Hall and West', point)
        self.assertIn('**2,453** new TCP votes', point)
        self.assertIn('received **46.8%**', point)
        self.assertIn('**2.0 pp** below the earlier estimate', point)
        self.assertNotIn('still estimated', point)

    def test_parallel_windows_retry_and_crash_reservations(self):
        self.origin()
        put(self.root, numerical('f00000001', '2026-03-21T17:10:00', 60))
        put(self.root, numerical('f00000002', '2026-03-21T18:00:00', 70))
        put(self.root, numerical('f00000003', '2026-03-21T18:18:00', 80))
        target = put(self.root, numerical('f00000004', '2026-03-21T18:20:00', 75))
        release, started = threading.Event(), threading.Event()
        active, maximum = 0, 0
        lock = threading.Lock()
        def client(packet, timeout):
            nonlocal active, maximum
            with lock:
                active += 1; maximum = max(maximum, active)
                if active == 2: started.set()
            release.wait(3)
            with lock: active -= 1
            return fake_response(packet, timeout)
        settings = enabled(); settings['concurrency'] = 2; settings['spending_limit_usd'] = 20
        worker = Processor(self.root, settings, client)
        worker.tick()
        self.assertTrue(started.wait(3))
        release.set(); worker.close(); worker.tick()
        self.assertEqual(maximum, 2)
        self.assertEqual(len(set(r['key'] for r in read(target/'requests.json').values())), 4)
        self.assertTrue(all(r['status'] == 'complete' for r in read(target/'publication.json')['commentary'].values()))

        # Reservations from interrupted calls still consume the aggregate limit.
        budget = commentary.Budget(self.root/'concurrent.sqlite', .2)
        def reserve(i):
            try: return budget.reserve(str(i), .1)
            except ValueError: return None
        with ThreadPoolExecutor(max_workers=8) as executor:
            results = list(executor.map(reserve, range(8)))
        self.assertEqual(sum(r is not None for r in results), 2)
        with self.assertRaisesRegex(ValueError, 'budget'):
            commentary.Budget(self.root/'concurrent.sqlite', .2).reserve('restart', .01)

    def test_console_uses_earliest_dispatch_and_last_reply_across_distinct_comparisons(self):
        path = self.root/'packages'/'f00000001'
        worker = Processor(self.root, enabled())
        try:
            # The second request completes first. The elapsed interval is 14s,
            # including any retry delays, irrespective of validation and polling.
            for key, start, end in [('previous', 10, 24), ('day', 14, 18)]:
                atomic(path/'commentary'/f'{key}.json', {'status': 'complete', 'finished_at': utc(27),
                       'request_timing': {'first_sent_at': start, 'last_received_at': end}})
            worker.awaiting_timing[path] = ({'previous', 'day'}, 22)
            with patch('sys.stdout', new_callable=io.StringIO) as console:
                worker.log_finished_commentary()
                worker.log_finished_commentary()
            self.assertEqual(console.getvalue().splitlines(), [
                'Commentary for 2026sa-engineering-test/f00000001: 2/2 distinct comparisons completed. '
                'First request to last response: 14.0s. Last response arrived after the publication deadline.'])
        finally:
            worker.close()

    def test_transport_timer_excludes_request_preparation_and_response_validation(self):
        packet = comparisons.prompt_packet(comparisons.summary(numerical()),
            comparisons.compare(comparisons.summary(numerical()), comparisons.summary(numerical('origin', None)), [], {}, {}))
        reply, usage = fake_response(commentary.prepare_packet(packet), 5)
        with patch('time.time', return_value=100) as clock:
            class Response:
                ok = True
                def json(self):
                    clock.return_value = 130
                    return {'status': 'completed', 'output': [{'type': 'message', 'content': [
                        {'type': 'output_text', 'text': json.dumps(reply)}]}], 'usage': usage}

            def reserve(*args):
                clock.return_value = 110
                return 1

            def post(*args, **kwargs):
                clock.return_value = 116
                return Response()

            worker = Processor(self.root, enabled())
            try:
                with patch.object(worker.budget, 'reserve', side_effect=reserve), patch('requests.post', side_effect=post), \
                        patch.dict('os.environ', {'OPENAI_API_KEY': 'fictional-test-key'}):
                    result = worker.perform(packet, 300)
                self.assertEqual(result['status'], 'complete')
                self.assertEqual(result['request_timing'], {'first_sent_at': 110, 'last_received_at': 116})
            finally:
                worker.close()

    def test_structured_api_and_invalid_retry(self):
        self.origin()
        current = comparisons.summary(numerical())
        packet = comparisons.prompt_packet(current,
            comparisons.compare(current, comparisons.summary(numerical('origin', None)), [], {}, {}))
        supplied = commentary.prepare_packet(packet)
        response, usage = fake_response(supplied, 1)
        class Response:
            ok = True
            def json(self):
                return {'status': 'completed', 'output': [{'type': 'message', 'content': [
                    {'type': 'output_text', 'text': json.dumps(response)}]}], 'usage': usage}
        with patch.dict('os.environ', {'OPENAI_API_KEY': 'fictional-test-key'}), patch('requests.post', return_value=Response()) as request:
            actual, _ = commentary.OpenAIClient(enabled())(supplied, 1)
            self.assertEqual(actual, response)
            payload = request.call_args.kwargs['json']
            self.assertFalse(payload['store'])
            self.assertTrue(payload['text']['format']['strict'])
        calls = []
        dispatch_times = []
        def initially_invalid(packet, timeout):
            dispatch_times.append(time.time())
            answer, usage = fake_response(packet, timeout); calls.append(1)
            if len(calls) == 1: answer['developments'][0]['seat_changes'][0] += ' **123.4%**'
            return answer, usage
        settings = enabled(); settings['attempts'] = 2
        worker = Processor(self.root, settings, initially_invalid)
        result = worker.perform(packet, time.time()+5); worker.close()
        self.assertEqual(result['status'], 'complete')
        self.assertEqual(result['attempts'], 2)
        self.assertLessEqual(result['request_timing']['first_sent_at'], dispatch_times[0])
        self.assertGreaterEqual(result['request_timing']['last_received_at'], dispatch_times[1])
        wrong_seat = deepcopy(response); wrong_seat['developments'][0]['seat'] = 'Schubert'
        with self.assertRaisesRegex(ValueError, 'different seat'):
            commentary.format_points(wrong_seat, supplied)

    def test_deadline_includes_queue_and_late_response_stays_local(self):
        self.origin()
        settings = config(); settings['commentary_timeout_seconds'] = .15
        atomic(self.root/'run.json', settings)
        path = put(self.root, numerical())
        event = threading.Event()
        entered = threading.Event()
        def client(packet, timeout):
            entered.set()
            event.wait(5)
            return fake_response(packet, timeout)
        worker = Processor(self.root, enabled(), client)
        # Disk writes can exceed this deliberately tiny deadline on a busy
        # forecasting machine. Advance the clock explicitly after dispatch so
        # the test always checks a late response, rather than a missed start.
        clock = comparisons.instant(read(path/'numerical.json')['completed_at'], 'UTC').timestamp()
        with patch('time.time', return_value=clock) as now:
            try:
                worker.tick()
                self.assertTrue(entered.wait(5))
                now.return_value = clock+1
                worker.tick()
                frozen = (path/'publication.json').read_bytes()
                self.assertEqual(read(path/'publication.json')['commentary']['previous']['status'], 'unavailable')
            finally:
                event.set()
                worker.close()
            worker.tick()
        self.assertEqual(frozen, (path/'publication.json').read_bytes())
        results = [read(p) for p in (path/'commentary').glob('*.json') if not p.name.endswith(('.input.json', '.started.json'))]
        self.assertTrue(any(r['status'] == 'complete' for r in results))

    def test_both_deadlines_budget_and_invalid_output(self):
        self.origin()
        packet = comparisons.prompt_packet(comparisons.summary(numerical()),
            comparisons.compare(comparisons.summary(numerical()), comparisons.summary(numerical('origin')), [], {}, {}))
        with self.assertRaisesRegex(ValueError, 'budget'):
            commentary.generate(packet, enabled(), commentary.Budget(self.root/'zero.sqlite', 0), time.time()+1, fake_response)
        supplied = commentary.prepare_packet(packet)
        response, _ = fake_response(supplied, 1)
        response['developments'][0]['seat_changes'][0] += ' **123.4%**'
        with self.assertRaisesRegex(ValueError, 'numerical'):
            commentary.format_points(response, supplied)
        response, _ = fake_response(supplied, 1)
        response['developments'][0]['seat_changes'][0] += ' {candidate:Light:999}'
        with self.assertRaisesRegex(ValueError, 'entity'):
            commentary.format_points(response, supplied)
        response, _ = fake_response(supplied, 1)
        response['developments'][0]['seat'] = 'Not a seat'
        with self.assertRaisesRegex(ValueError, 'seat'):
            commentary.format_points(response, supplied)
        for term, seconds in [('2026sa', 90), ('2025fed', 30)]:
            sub = self.root/term
            cfg = config(); cfg['election'] = term; del cfg['commentary_timeout_seconds']
            atomic(sub/'run.json', cfg)
            put(sub, numerical('origin', '2026-03-21T00:00:00'))
            path = put(sub, numerical(complete=utc(time.time()-seconds-1)))
            worker = Processor(sub, enabled(), lambda *_: self.fail('Expired request must not be sent'))
            worker.tick(); worker.close()
            self.assertTrue((path/'ready-publication.json').exists())

    def test_destination_receipts_retry_and_immutable_hash(self):
        self.origin(); path = put(self.root, numerical())
        worker = Processor(self.root); worker.tick(); worker.close()
        class FakeServer:
            calls = 0
            def submit(inner, payload, sha):
                inner.calls += 1
                return {'run_id': config()['run_id'], 'forecast_id': payload['forecast']['forecast_id'],
                        'sha256': sha, 'published_at': utc(), 'latest_forecast_id': 'f00000009'}
        class Unavailable:
            def submit(inner, *_):
                raise ConnectionError('Server unavailable')
        good = FakeServer()
        queue = UploadQueue(self.root, {})
        with patch('sys.stdout', new_callable=io.StringIO) as console:
            queue.tick_destination('local', good)
            queue.tick_destination('remote', Unavailable())
            queue.tick_destination('local', good)
        self.assertEqual(console.getvalue().splitlines(), [
            'Uploaded 2026sa-engineering-test/origin to local.',
            'Uploaded 2026sa-engineering-test/f00000001 to local.',
        ])
        self.assertEqual(good.calls, 2) # Saved zero-count reference and current forecast.
        receipt = read(self.root/'receipts'/'local.json')
        self.assertEqual(receipt['latest_forecast_id'], 'f00000009')
        self.assertFalse(read(self.root/'receipts'/'remote.json')['forecasts'][path.name]['accepted'])
        payload = read(path/'publication.json'); payload['report']['termCode'] = 'different'
        atomic(path/'publication.json', payload)
        queue.tick_destination('test', good)
        self.assertEqual(good.calls, 3) # The reference is valid; the edited forecast is refused.
        self.assertIn('changed', read(self.root/'receipts'/'test.json')['forecasts'][path.name]['error'])

    def test_crash_after_freezing_keeps_exact_payload_and_checks_the_destination_digest(self):
        self.origin()
        package = put(self.root, numerical())
        with closing(Processor(self.root)) as worker:
            worker.tick()
        marker = package/'ready-publication.json'
        frozen = package/'publication.json'
        before, digest = frozen.read_bytes(), read(marker)['sha256']
        receipts = self.root/'receipts'/'local.json'
        atomic(receipts, {'forecasts': {package.name: {'accepted': True, 'receipt': {'sha256': digest}}}})
        marker.unlink()  # Interrupted after the immutable body was written.
        with closing(Processor(self.root, enabled(), self.fail)) as restarted:
            restarted.tick()
        self.assertEqual(frozen.read_bytes(), before)
        self.assertEqual(read(marker)['sha256'], digest)
        marker.unlink()
        changed = read(frozen)
        changed['released_at'] = '2026-03-21T23:00:00Z'
        atomic(frozen, changed)
        with closing(Processor(self.root, enabled(), self.fail)) as restarted:
            with self.assertRaisesRegex(ValueError, 'differs from the acknowledged upload'):
                restarted.tick()
        self.assertFalse(marker.exists())
        frozen.unlink()
        with closing(Processor(self.root, enabled(), self.fail)) as restarted:
            with self.assertRaisesRegex(ValueError, 'Restore the already uploaded publication'):
                restarted.tick()
        self.assertFalse(frozen.exists())

    def test_each_destination_remembers_the_capture_across_a_publisher_restart(self):
        self.origin()
        package = put(self.root, numerical())
        with closing(Processor(self.root)) as worker:
            worker.tick()
        payload = read(package/'publication.json')
        duplicate = deepcopy(payload)
        duplicate['forecast']['forecast_id'] = 'f00000002'
        duplicate['forecast']['input_revision'] = 'regenerated'
        folder = self.root/'packages'/'f00000002'
        server = Mock()
        server.submit.side_effect = lambda body, sha: dict(
            run_id=config()['run_id'], forecast_id=body['forecast']['forecast_id'],
            sha256=sha, latest_forecast_id=body['forecast']['forecast_id'], published_at=utc())
        queue = UploadQueue(self.root, {})
        queue.tick_destination('local', server)
        # Pre-upgrade receipts stored the server acknowledgement alone. Both
        # the running GUI and its complete saved packages can identify this feed.
        receipt_path = self.root/'receipts'/'local.json'
        old_receipts = read(receipt_path)
        for key in ('source_time', 'source_fingerprint'):
            old_receipts['forecasts'][package.name].pop(key)
        atomic(receipt_path, old_receipts)
        atomic(folder/'publication.json', duplicate)
        atomic(folder/'ready-publication.json', {'forecast_id': folder.name, 'sha256': fingerprint(duplicate)})
        with patch('sys.stdout', new_callable=io.StringIO) as console:
            restarted = UploadQueue(self.root, {})
            for _ in range(2):
                held = restarted.tick_destination('local', server)
        self.assertEqual(server.submit.call_count, 2)  # origin and first forecast only
        self.assertEqual(held['forecasts'][folder.name]['duplicate_of'], 'f00000001')
        self.assertEqual(console.getvalue().count('Held'), 1)
        restarted.tick_destination('test', server)
        self.assertEqual(server.submit.call_count, 4)

    def test_upload_refreshes_expired_registration_but_respects_permission_denial(self):
        # Long-running uploaders can outlive their JWT. Exercise the actual
        # registration path with both 403 replies given by the website.
        package = read(ROOT/'tests'/'fixtures'/'live-publication-v1'/'package.json')
        run = package['run']['run_id']
        identity = package['forecast']['forecast_id']
        sha = fingerprint(package)
        accepted = {'run_id': run, 'forecast_id': identity, 'sha256': sha,
                    'latest_forecast_id': identity, 'published_at': utc()}
        for detail, renew in [('Token has expired.', True), ('Permission denied.', False)]:
            with self.subTest(detail=detail):
                denied = Mock(status_code=403)
                denied.json.return_value = {'detail': detail}
                denied.raise_for_status.side_effect = ValueError(detail)
                registered = Mock(status_code=200)
                registered.json.return_value = {'run_id': run}
                published = Mock(status_code=200)
                published.json.return_value = accepted
                client = Server('http://mock', '', '', Mock())
                client.connected = True
                client.session.post.side_effect = [denied, registered, published]
                with patch.object(client, 'connect', side_effect=lambda: setattr(client, 'connected', True)) as login:
                    if renew:
                        self.assertEqual(client.submit(package, sha), accepted)
                        login.assert_called_once()
                        calls = client.session.post.call_args_list
                        self.assertEqual(calls[0], calls[1])
                        self.assertEqual(len(calls), 3)
                    else:
                        with self.assertRaisesRegex(ValueError, 'Permission denied'):
                            client.submit(package, sha)
                        login.assert_not_called()
                        self.assertEqual(client.session.post.call_count, 1)

    def test_replay_requires_explicit_capabilities(self):
        class Response:
            def raise_for_status(self): pass
            def json(self): return {'schema_version': 1}
        class Legacy:
            def get(self, *_, **__): return Response()
            def post(self, *_, **__): self.fail('Must not send replay to unsupported server')
        with self.assertRaisesRegex(ValueError, 'explicitly support'):
            Server('http://mock', '', '', Legacy()).connect()

    def test_http_contract_duplicate_out_of_order_and_replay_isolation(self):
        self.origin()
        first = put(self.root, numerical())
        second = put(self.root, numerical('f00000002', '2026-03-21T18:40:00'))
        worker = Processor(self.root); worker.tick(); worker.close()
        older, newer = read(first/'publication.json'), read(second/'publication.json')
        with MockServer() as backend:
            client = Server(backend.url, 'fictional@example.invalid', 'test-only')
            client.submit(newer, fingerprint(newer))
            original = client.submit(older, fingerprint(older))
            duplicate = client.submit(older, fingerprint(older))
            self.assertEqual(original['published_at'], duplicate['published_at'])
            self.assertEqual(duplicate['latest_forecast_id'], 'f00000002')
            reassigned = deepcopy(older)
            reassigned['forecast']['forecast_id'] = 'f00000009'
            with self.assertRaises(DuplicateObservation) as conflict:
                client.submit(reassigned, fingerprint(reassigned))
            self.assertEqual(conflict.exception.forecast_id, 'f00000001')
            other = deepcopy(older); other['run']['run_id'] += '-rerun'
            client.submit(other, fingerprint(other))
            self.assertEqual(len(backend.latest), 2)
            self.assertEqual(len(backend.packages), 3)
            manifest = replay_manifest(self.root, older['run'])
            with self.assertRaisesRegex(ValueError, 'manifest mismatch'):
                client.complete_replay(older['run'], manifest)
            origin = read(self.root/'packages'/'origin'/'publication.json')
            receipt = client.submit(origin, fingerprint(origin))
            self.assertEqual(receipt['latest_forecast_id'], 'f00000002')
            completed = client.complete_replay(older['run'], manifest)
            self.assertEqual(completed['snapshot_count'], 2)
            self.assertEqual(client.complete_replay(older['run'], manifest), completed)
            (self.root/'packages'/'f00000003').mkdir()
            with self.assertRaisesRegex(ValueError, 'awaiting publication'):
                replay_manifest(self.root, older['run'])
            conflicting = deepcopy(older); conflicting['report']['reportLabel'] = 'changed'
            with self.assertRaises(Exception):
                client.submit(conflicting, fingerprint(conflicting))

    def test_contract_fixtures_and_compact_shared_evidence(self):
        fixture = ROOT/'tests'/'fixtures'/'live-publication-v1'
        package, receipt = read(fixture/'package.json'), read(fixture/'acknowledgement.json')
        self.assertEqual(fingerprint(package), receipt['sha256'])
        self.assertEqual(package['forecast']['forecast_id'], receipt['forecast_id'])
        self.assertEqual(package['run']['kind'], 'replay')
        complete = package['commentary']['previous']
        # Previously released packages keep their original fact-based format.
        # Publication reads do not revalidate them against today's LLM schema.
        self.assertIn('facts', complete['text']['developments'][0])
        benchmark.write_review(self.root, [complete], 10)
        self.assertIn('Finniss', (self.root/'review.html').read_text(encoding='utf8'))
        inline = read(fixture/'inline-commentary.json')
        current = comparisons.summary(numerical())
        supplied = commentary.prepare_packet(comparisons.prompt_packet(current,
            comparisons.compare(current, comparisons.summary(numerical('origin', None, 50)), [], {}, {})))
        commentary.validate(inline['text'], supplied)
        benchmark.write_review(self.root, [inline], 10)
        display = (self.root/'review.html').read_text(encoding='utf8')
        self.assertIn('Fictional Labor candidate', display)
        self.assertIn('<strong>55.0% (+5.0 pp)</strong>', display)
        self.assertIn('3.5%', display)
        self.assertEqual(read(ROOT/'schemas/live-publication-v1.json')['$defs']['inlineCommentaryText'], commentary.SCHEMA)
        self.assertEqual(package['commentary']['1h']['status'], 'unavailable')
        before = comparisons.summary(numerical('origin', None, 50))
        after = comparisons.summary(numerical())
        shared = {'election': {}, 'category_biases': comparisons._category_evidence({'tpp': {
            'booth_type': [{'category': 'PPVC', 'bias': 1.5, 'std_dev': .5, 'vote_count': 10000}]}})}
        group = comparisons.compare(after, before, [], shared, {})
        packet = comparisons.prompt_packet(after, group)
        self.assertTrue(any(e['kind'] == 'category_biases' for e in packet['evidence']))

    def test_statewide_preference_intervals_reach_live_replay_publication(self):
        fixture = ROOT/'tests'/'fixtures'/'live-publication-v1'/'statewide-preference-flows.json'
        report = read(fixture)
        report['countProgress'] = {'fp_votes': 0, 'tcp_votes': 0, 'expected_formal_votes': 1000}
        self.origin()
        folder = put(self.root, dict(numerical(), report=report))
        worker = Processor(self.root)
        try:
            worker.tick()
            later = deepcopy(report)
            later['countProgress']['fp_votes'] = 80
            later['statewidePreferenceFlows'][0]['percentiles'] = [
                x+4 for x in later['statewidePreferenceFlows'][0]['percentiles']]
            updated = put(self.root, dict(numerical('f00000002', '2026-03-21T18:40:00'), report=later))
            worker.tick()
        finally:
            worker.close()
        payload = read(folder/'publication.json')
        start = read(updated/'publication.json')['comparisons']['since_start']
        flows = start['statewide_preference_changes']
        self.assertEqual(start['reference_forecast_id'], 'origin')
        self.assertTrue(flows['from_first_zero_count'])
        self.assertEqual(flows['reference_source_time'], payload['forecast']['source_time'])
        self.assertEqual(flows['rows'], [{'parties': ['0','1','2'], 'before': 60, 'after': 64, 'change_pp': 4}])
        with MockServer() as backend:
            client = Server(backend.url, 'fictional@example.invalid', 'test-only')
            client.submit(payload, fingerprint(payload))
            received = backend.packages[config()['run_id'], folder.name]['report']
        self.assertEqual(received, report)
        row, = received['statewidePreferenceFlows']
        abbreviations = dict(received['partyAbbr'])
        self.assertEqual([abbreviations[row[key]] for key in ('sourceParty', 'destinationParty', 'otherFinalist')],
                         ['ALP', 'LIB', 'ON'])
        levels = dict(zip(received['voteTotalThresholds'], row['percentiles']))
        self.assertEqual(levels[50], 60)
        self.assertEqual([(levels[q], levels[100-q]) for q in (25, 5, 1, 0.1)],
                         [(54, 65), (46, 73), (40, 79), (35, 85)])
        origin = read(self.root/'packages'/'origin'/'publication.json')
        self.assertNotIn('statewidePreferenceFlows', origin['report'])

    def test_commentary_sees_only_material_comparable_statewide_flows(self):
        report = read(ROOT/'tests'/'fixtures'/'live-publication-v1'/'statewide-preference-flows.json')
        def snapshot(identity, time, change=0, counted=0):
            base = deepcopy(report)
            base['countProgress'] = {'fp_votes': counted, 'tcp_votes': counted, 'expected_formal_votes': 1000}
            base['statewidePreferenceFlows'][0]['percentiles'] = [
                value+change for value in base['statewidePreferenceFlows'][0]['percentiles']]
            return comparisons.summary(dict(numerical(identity, time), report=base))
        first = snapshot('f00000001', '2026-03-21T18:20:00')
        now = snapshot('f00000002', '2026-03-21T18:40:00', 4, 100)
        origin = dict(first, forecast_id='origin', origin=True, source_time=None, statewide={})
        def compare(current):
            return comparisons.comparisons(current, [origin, first], lambda _: {}, config())
        groups = compare(now)
        start = groups['since_start']
        self.assertEqual(start['reference_forecast_id'], 'origin')
        self.assertEqual(start['statewide_preference_changes']['reference_forecast_id'], first['forecast_id'])
        packet = comparisons.prompt_packet(now, start)
        self.assertIn('preference_flow_reference', packet)
        self.assertEqual(len(packet['statewide_preference_changes']), 1)
        display = [r for r in commentary_format.figures(packet) if r['quantity'] == 'estimated statewide preference flow']
        self.assertEqual([(r['party'], r['current_with_change']) for r in display],
                         [('ALP first preferences to LIB over ON', '64.0% (+4.0 pp)')])
        messages = prompt_layout.input_messages(packet, commentary.INSTRUCTIONS, {})
        compact = json.loads(messages[1]['content'][-1]['text'].split('\n', 1)[1])
        self.assertIn('split by source party is estimated', compact['preference_flow_basis'])
        self.assertNotIn('statewide_preference_changes', compact)
        self.assertIn('ALP first preferences to LIB over ON', json.dumps(compact))
        self.assertNotIn('preference_flow_reference', comparisons.prompt_packet(now, groups['previous']))
        quiet = snapshot('f00000003', '2026-03-21T19:00:00', 1)
        self.assertNotIn('statewide_preference_changes', comparisons.prompt_packet(quiet, compare(quiet)['previous']))
        # A later change of inputs or a first delivery with counting already
        # underway supplies no comparable pre-count flow for commentary.
        revised = dict(now, input_revision='revised')
        self.assertNotIn('statewide_preference_changes', comparisons.prompt_packet(revised, compare(revised)['previous']))
        first['statewide']['countProgress']['fp_votes'] = 1
        self.assertNotIn('statewide_preference_changes', compare(now)['since_start'])

    def test_morning_handoff_retains_prior_report_with_out_of_order_uploads(self):
        fixtures = ROOT/'tests'/'fixtures'/'live-publication-v1'
        older = read(fixtures/'package.json')
        morning = read(fixtures/'morning-package.json')
        self.assertEqual(set(morning['comparisons']), {'previous', 'since_start', 'today'})
        retained = morning['retained_reports']['yesterday']
        self.assertEqual(retained['source_forecast_id'], older['forecast']['forecast_id'])
        self.assertIn(retained['source_window'], older['commentary'])
        self.assertNotIn('yesterday', morning['commentary_requests'])
        with MockServer() as backend:
            client = Server(backend.url, 'fictional@example.invalid', 'test-only')
            client.submit(morning, fingerprint(morning))
            acknowledgement = client.submit(older, fingerprint(older))
            self.assertEqual(acknowledgement['latest_forecast_id'], morning['forecast']['forecast_id'])
            preserved = backend.packages[morning['run']['run_id'], morning['forecast']['forecast_id']]
            self.assertEqual(preserved['retained_reports']['yesterday'], retained)

    def test_atomic_feeder_and_copy_conflicts(self):
        archive, inbox = self.root/'archive', self.root/'inbox'
        archive.mkdir(); inbox.mkdir()
        first = archive/'el2026260321180000.xml'
        first.write_text('<synthetic/>')
        self.assertEqual(replay_feeder.catalogue(archive, '2026sa')[0][0], '20260321180000')
        replay_feeder.deliver(first, inbox)
        replay_feeder.deliver(first, inbox)
        self.assertEqual((inbox/first.name).read_text(), '<synthetic/>')
        self.assertFalse(list(inbox.glob('*.part')))
        first.write_text('<changed/>')
        with self.assertRaisesRegex(ValueError, 'immutable'):
            replay_feeder.deliver(first, inbox)
        with run_lock(inbox):
            with self.assertRaises(RunLockBusy):
                with run_lock(inbox): pass

    def test_count_gate_corrections_threshold_and_missing_evidence(self):
        before = observed({'a': 100, 'b': 50}, {'a': 100, 'b': 50})
        for ballots, expected in [(0, 'no_new_counting'), (9, 'minor_changes'), (10, 'request')]:
            after = observed({'a': 100-ballots, 'b': 50+ballots}, {'a': 100-ballots, 'b': 50+ballots})
            changes = activity.count_changes(before, after)
            self.assertEqual(changes['changed_votes'], ballots)
            self.assertEqual(activity.decision({'available': True, 'count_activity': changes}), expected)
        unknown = activity.count_changes({}, before)
        self.assertFalse(unknown['known'])
        self.assertEqual(activity.decision({'available': True, 'count_activity': unknown}), 'request')
        empty = {'observed_counts_available': True, 'booths': {}}
        self.assertEqual(activity.count_changes(empty, observed({'a': 0}))['changed_votes'], 0)
        self.assertEqual(activity.count_changes(before, empty)['changed_votes'], 150)

    def test_local_notice_skips_api_and_keeps_configuration_change(self):
        self.origin()
        first = put(self.root, numerical(chance=95))
        evidence = observed({'a': 0})
        with patch('live_pipeline.comparisons.evidence', return_value=evidence):
            worker = Processor(self.root, enabled(), lambda *_: self.fail('Unchanged votes must not call API'))
            worker.tick(); worker.close()
        report = read(first/'publication.json')
        self.assertEqual(read(first/'requests.json'), {})
        text = report['commentary']['previous']
        self.assertEqual(text['kind'], 'no_new_counting')
        self.assertEqual(text['estimated_usd'], 0)
        self.assertIn('No new counting', text['text']['summary'])
        group = {'commentary_decision': 'minor_changes', 'actual_seconds': 1200, 'input_revision_changed': True}
        self.assertIn('20-minute', activity.notice(group, '15m')['text']['summary'])
        self.assertIn('inputs changed', activity.notice(group, '15m')['text']['summary'])

    def test_each_window_uses_its_own_counts_and_restorations(self):
        self.origin()
        put(self.root, numerical('f00000001', '2026-03-21T18:00:00'))
        latest = put(self.root, numerical('f00000002', '2026-03-21T18:20:00'))
        def evidence(value, run_folder):
            return observed({'a': 0 if value['origin'] else 100})
        with patch('live_pipeline.comparisons.evidence', side_effect=evidence):
            worker = Processor(self.root); worker.tick(); worker.close()
        requests = read(latest/'requests.json')
        self.assertEqual(set(requests), {'since_start'})
        result = read(latest/'publication.json')
        self.assertEqual(result['commentary']['previous']['kind'], 'no_new_counting')
        self.assertEqual(result['commentary']['15m']['kind'], 'no_new_counting')
        before = comparisons.summary(numerical('f00000001'))
        middle = comparisons.summary(numerical('f00000002'))
        after = comparisons.summary(numerical('f00000003'))
        middle['count_activity_since_previous'] = {'known': True, 'changed_votes': 20}
        after['count_activity_since_previous'] = {'known': True, 'changed_votes': 20}
        same = observed({'a': 100})
        group = comparisons.compare(after, before, [before, middle], same, same)
        self.assertEqual(group['count_activity']['endpoint_changed_votes'], 0)
        self.assertEqual(group['count_activity']['changed_votes'], 40)
        self.assertEqual(activity.decision(group), 'request')

    def test_major_correction_selected_without_win_movement(self):
        before = comparisons.summary(numerical('f00000001', chance=100))
        after = comparisons.summary(numerical('f00000002', chance=100))
        group = comparisons.compare(after, before, [], observed({'a': 200, 'b': 300}), observed({'a': 400, 'b': 100}))
        packet = comparisons.prompt_packet(after, group)
        event = next(e for e in packet['evidence'] if e['kind'] == 'booths')
        self.assertEqual(event['fp_vote_change'], 0)
        self.assertEqual(event['changed_votes'], 200)
        self.assertEqual(activity.decision(group), 'request')
        summary, _, _ = rule_commentary.write(packet)
        self.assertIn('allocation of **200** FP votes', summary)
        self.assertIn('total stayed at **500 votes**', summary)

    def test_manual_count_is_observed_evidence_without_new_fp_tcp(self):
        before = observed({'a': 100})
        after = deepcopy(before)
        after['manual_count_observations'] = {'Finniss': {'seat': 'Finniss', 'transferred_votes': {'a': 60, 'b': 40}}}
        changes = activity.count_changes(before, after)
        self.assertEqual(changes['changed_votes'], 100)
        self.assertEqual(activity.decision({'available': True, 'count_activity': changes}), 'request')
        unchanged = deepcopy(after)
        unchanged['manual_preferences'] = {'Finniss': {'ranked_draws': 500}}
        self.assertEqual(activity.count_changes(after, unchanged)['changed_votes'], 0)

    def test_preference_sample_reaches_both_writers_only_when_observed_counts_change(self):
        sample = {'district': 'Finniss', 'sample': 'Victor Harbor Early Voting Centre, Greens and Bron Lewis',
                  'available_at': '2026-03-30T12:10:00+11:00', 'sampled_preference_ballots': 1053,
                  'sampled_first_preferences': {'GRN': 560, 'IND:Bron Lewis': 493},
                  'flow_percent': {'IND:Lou Nicholson': 58, 'ALP': 22, 'LIB': 13, 'ON': 7}}
        old = numerical('f00000001')
        new = numerical('f00000002')
        for data in (old, new):
            data['report']['seatCandidateNames'][3].append([3, 'Lou Nicholson'])
            data['report']['seatPartyWinFrequencies'][3] = [[0, 30], [1, 15], [2, 0], [3, 55]]
        before, after = map(comparisons.summary, (old, new))
        def evidence(rows, used):
            analysis = {'booths': [], 'preference_count_observations': rows, 'manual_preference_evidence': used}
            with patch.object(comparisons, 'load_json', return_value=analysis), patch.object(
                    comparisons, 'analysis_path_for_snapshot', return_value='unused'):
                return comparisons.evidence(dict(new, diagnostic_file='snapshot.json'))
        blank = evidence([], [])
        observation = evidence([sample], [])
        with_use = evidence([sample], [{'seat': 'Finniss', 'sample_conditioned_draws': 900}])
        def packet(current, earlier):
            return commentary.prepare_packet(comparisons.prompt_packet(after,
                comparisons.compare(after, before, [], current, earlier)))
        for earlier, current in ((blank, blank), (observation, with_use), (with_use, blank)):
            self.assertNotIn('reported_preference_samples', packet(current, earlier))
        for current, used in ((observation, False), (with_use, True)):
            p = packet(current, blank)
            model_input = prompt_layout.input_messages(p, commentary.INSTRUCTIONS, {})[1]['content'][-1]['text']
            window = json.loads(model_input.split('\n', 1)[1])
            reported = window['reported_preference_samples']['Finniss']
            self.assertEqual(set(reported['source_first_preferences']), {'GRN', 'IND:Bron Lewis'})
            words = reported['observed']
            self.assertIn('four-candidate', words)
            self.assertIn('Victor Harbor Early Voting Centre', words)
            self.assertIn('**1,053 ballots**', words)
            self.assertIn('**58%** went to {candidate:Finniss:3}', words)
            self.assertEqual('used this sample' in words, used)
            self.assertNotIn('610.74', model_input)
            _, sections, _ = rule_commentary.write(p)
            self.assertTrue(any(words in s['text'] for s in sections))
            commentary_format.check_text(words, ['Finniss'], p)
            commentary_format.check_text('The 4CP sample covered **1,053** ballots.', ['Finniss'], p)
            with self.assertRaises(ValueError):
                commentary_format.check_text('The sample covered **1,054 ballots**.', ['Finniss'], p)
            with self.assertRaises(ValueError):
                commentary_format.check_text('The sample covered **1,053 ballots**.', ['Light'], p)
            p['seat_changes'] = [{'seat': 'Finniss', 'changes': [
                {'metric': '/finalist_percent/3', 'before': 87.7, 'after': 97.6, 'change': 9.9}]}]
            self.assertIn('reaching the final two rose from **87.7%** to **97.6%**',
                          rule_commentary.preference_sample_point(p, 'Finniss'))
            self.assertEqual(rule_commentary.preference_sample_point(p, 'Finniss', 'separate pairing explanation'), words)
            p['seat_changes'][0]['changes'].append(
                {'metric': '/finalist_percent/0', 'before': 30, 'after': 20, 'change': -10})
            split = commentary_format.preference_sample_detail(p, 'Finniss')
            self.assertEqual('outlast {candidate:Finniss:0}' in split, used)
            self.assertNotIn('outlast', commentary_format.preference_sample_detail(
                dict(p, input_revision_changed=True), 'Finniss'))
            p['reported_preference_samples']['Finniss']['revised'] = True
            self.assertNotIn('outlast', commentary_format.preference_sample_detail(p, 'Finniss'))
        revised = evidence([dict(sample, flow_percent=dict(sample['flow_percent'], ALP=23, ON=6))], [])
        self.assertTrue(packet(revised, with_use)['reported_preference_samples']['Finniss']['revised'])

    def test_sample_from_older_package_and_federal_contest_for_second(self):
        # Ryan's largest flow went to Liberal. The contest for the second
        # finalist was Greens versus Labor, which the published tallies measure.
        old, new = numerical('f00000001'), numerical('f00000002')
        for data in (old, new):
            report = data['report']
            report['seatNames'][3] = 'Ryan'
            report['partyAbbr'][2][1] = 'GRN'
            report['seatPartyWinFrequencies'][3] = [[0, 14], [1, 0], [2, 86]]
        row = {'district': 'Ryan', 'sample': 'AEC indicative 3CP', 'sampled_preference_ballots': 7487,
               'flow_percent': {'ALP': 20.715908, 'GRN': 21.076533, 'LIB': 58.20756},
               'sampled_first_preferences': {'ON': 2048, 'OTH': 5439},
               'sampled_candidate_totals': {'ALP': 28369, 'GRN': 28949, 'LIB': 37906}}
        diagnostic = self.root/'snapshot.json'
        atomic(diagnostic.with_suffix('.analysis.json'), {'preference_count_observations': [row]})
        package = put(self.root, dict(new, diagnostic_file=str(diagnostic)))
        recorded = comparisons.preference_count_observations({'preference_count_observations': [row]})['Ryan']
        compact = {k: recorded[k] for k in ('seat', 'transferred_votes', 'sampled_first_preferences', 'sampled_candidate_totals')}
        empty = {'booths': {}, 'seats': {}, 'preferences': {}, 'issues': [], 'statewide': {},
                 'observed_counts_available': True}
        atomic(package/'evidence.json.gz', dict(empty, manual_count_observations={'Ryan': compact}))
        recovered = comparisons.comparison_evidence(package)
        before, after = map(comparisons.summary, (old, new))
        group = comparisons.compare(after, before, [], recovered, empty)
        p = commentary.prepare_packet(comparisons.prompt_packet(after, group))
        words = commentary_format.preference_sample_detail(p, 'Ryan')
        self.assertIn('{candidate:Ryan:2} led {candidate:Ryan:0} by **580 votes** for second place', words)
        self.assertIn('**58.2%** went to {candidate:Ryan:1}', words)
        self.assertNotIn('used this sample', words)
        commentary_format.check_text(words, ['Ryan'], p)
        _, sections, _ = rule_commentary.write(p)
        self.assertTrue(any(words in section['text'] for section in sections))
        message = prompt_layout.input_messages(p, commentary.INSTRUCTIONS, {})[1]['content'][-1]['text']
        self.assertIn(words, message)
        self.assertEqual(read(package/'evidence.json.gz')['manual_count_observations']['Ryan'], compact)

    def test_step_delivery_follows_existing_inbox_and_stops(self):
        archive, inbox = self.root/'archive', self.root/'inbox'
        archive.mkdir(); inbox.mkdir()
        for code in ('180000', '190000'):
            (archive/f'el2026260321{code}.xml').write_text('<synthetic/>')
        records = replay_feeder.catalogue(archive, '2026sa')
        self.assertEqual(replay_feeder.next_delivery(records, inbox), records[:1])
        replay_feeder.deliver(records[0][2], inbox)  # Also models a manual first delivery.
        self.assertEqual(replay_feeder.next_delivery(records, inbox), records[1:])
        replay_feeder.deliver(records[1][2], inbox)
        self.assertEqual(replay_feeder.next_delivery(records, inbox), [])
        (inbox/records[0][2].name).write_text('<different/>')
        with self.assertRaisesRegex(ValueError, 'conflicting'):
            replay_feeder.next_delivery(records, inbox)

    def test_step_count_is_relative_to_inbox_and_waits_for_each_local_upload(self):
        run = config()
        folder = self.root/'live_runs'/run['run_id']
        inbox = folder/'inbox'
        run['source_directory'] = str(inbox)
        atomic(folder/'run.json', run)
        archive = self.root/'forecasts/2026sa/live-snapshots/feeds'
        archive.mkdir(parents=True)
        for minute in range(7):
            (archive/f'el202626032118{minute:02d}00.xml').write_text(str(minute))
        records = replay_feeder.catalogue(archive, '2026sa')
        codes = [code for code, _, _ in records]
        args = ['--run-id', run['run_id'], '--step']
        waits = []

        def receipt(run_folder, code):
            self.assertEqual(run_folder, folder)
            delivered = [c for c, _, p in records if (inbox/p.name).exists()]
            waits.append((code, delivered[-1] if delivered else None))

        with patch.object(replay_feeder, 'WORKSPACE', self.root), \
                patch.object(replay_feeder, 'wait_for_local_upload', side_effect=receipt):
            replay_feeder.main(args + ['2', '--dry-run'])
            self.assertFalse(inbox.exists())
            replay_feeder.main(args + ['2'])
            self.assertEqual(waits, [(None, None)] + [(c, c) for c in codes[:2]])
            replay_feeder.main(args)
            self.assertTrue((inbox/records[2][2].name).exists())
            self.assertEqual(len(waits), 3)

            # A manually copied fourth capture has no feeder receipt of its own.
            # Check its website upload before delivering the fifth and sixth.
            replay_feeder.deliver(records[3][2], inbox)
            replay_feeder.main(args + ['2'])
            self.assertEqual(waits[3:], [(c, c) for c in codes[3:6]])
            self.assertFalse((inbox/records[6][2].name).exists())
            replay_feeder.main(args + ['10'])
            replay_feeder.main(args + ['10'])
            self.assertEqual(waits[6:], [(c, c) for c in codes[5:7]])

    def test_fixed_interval_feeder_uses_real_elapsed_time_and_resumes(self):
        archive, inbox = self.root/'archive', self.root/'inbox'
        archive.mkdir()
        names = ['el2026260321180000.xml', 'el2026260321180030.xml', 'el2026260331120000.xml']
        for name in names:
            (archive/name).write_text(name)
        args = ['--election', '2026sa', '--source', str(archive), '--inbox', str(inbox), '--interval-seconds', '30']
        instant = 1000.
        def wait(seconds):
            nonlocal instant
            instant += seconds
        with patch.object(time, 'time', side_effect=lambda: instant), patch.object(time, 'monotonic', side_effect=lambda: instant), \
                patch.object(time, 'sleep', side_effect=wait) as slept:
            replay_feeder.main(args + ['--limit', '1'])
            slept.assert_not_called()
            instant += 5
            replay_feeder.main(args)
            self.assertEqual([c.args[0] for c in slept.call_args_list], [25, 30])
            replay_feeder.main(args)
            self.assertEqual(slept.call_count, 2)
        self.assertEqual(sorted(p.name for p in inbox.glob('*.xml')), names)
        records = read(inbox/'feeder-state.json')['delivered']
        self.assertEqual(sorted(row['delivered_at_unix'] for row in records.values()), [1000, 1030, 1060])

    def test_receipt_paced_feeder_waits_for_each_local_upload_including_on_restart(self):
        run = config()
        name = run['run_id']
        folder = self.root/'live_runs'/name
        inbox = folder/'inbox'
        run['source_directory'] = str(inbox)
        atomic(folder/'run.json', run)
        source = self.root/'forecasts/2026sa/live-snapshots/feeds'
        source.mkdir(parents=True)
        for suffix in ('180000', '190000'):
            (source/f'el2026260321{suffix}.xml').write_text(suffix)
        first, second = replay_feeder.catalogue(source, '2026sa')
        receipts = folder/'receipts/local.json'
        ledger = {'state': 'armed', 'captures': {}}
        atomic(folder/'state.json', ledger)
        events = []

        def advance(_):
            if not events:
                self.assertFalse((inbox/first[2].name).exists())
                atomic(receipts, {'forecasts': {'origin': {'accepted': True}}})
            elif len(events) == 1:
                self.assertTrue((inbox/first[2].name).exists())
                self.assertFalse((inbox/second[2].name).exists())
                ledger['captures']['one'] = {'capture_code': first[0], 'state': 'accepted',
                                             'forecast': 'completed', 'forecast_id': 'f00000001'}
                atomic(folder/'state.json', ledger)
                # Stop after delivery, before the website acknowledges this forecast.
                events.append('interrupted')
                raise KeyboardInterrupt
            elif len(events) == 2:
                self.assertFalse((inbox/second[2].name).exists())
                atomic(receipts, {'forecasts': {'origin': {'accepted': True}, 'f00000001': {'accepted': True}}})
            else:
                self.assertEqual(len(events), 3)
                self.assertTrue((inbox/second[2].name).exists())
                ledger['captures']['two'] = {'capture_code': second[0], 'state': 'accepted',
                                             'forecast': 'completed', 'forecast_id': 'f00000002'}
                atomic(folder/'state.json', ledger)
                accepted = read(receipts)
                accepted['forecasts']['f00000002'] = {'accepted': True}
                atomic(receipts, accepted)
            events.append('advanced')

        args = ['--run-id', name, '--after-local-upload']
        with patch.object(replay_feeder, 'WORKSPACE', self.root), patch.object(time, 'sleep', side_effect=advance):
            with self.assertRaises(KeyboardInterrupt):
                replay_feeder.main(args)
            replay_feeder.main(args)
        self.assertEqual(len(events), 4)
        self.assertEqual(set(read(inbox/'feeder-state.json')['delivered']), {first[0], second[0]})
        ledger['captures']['two']['forecast'] = 'failed'
        ledger['captures']['two']['forecast_message'] = 'test failure'
        atomic(folder/'state.json', ledger)
        with self.assertRaisesRegex(ValueError, 'test failure'):
            replay_feeder.wait_for_local_upload(folder, second[0])

    def test_speed_selection_cached_usage_and_benchmark_dry_run(self):
        current = comparisons.summary(numerical())
        packet = comparisons.prompt_packet(current,
            comparisons.compare(current, comparisons.summary(numerical('origin', None)), [], {}, {}))
        response, usage = fake_response(commentary.prepare_packet(packet), 1)
        class Response:
            ok = True
            def json(self):
                return {'status': 'completed', 'service_tier': 'default', 'usage': usage,
                        'output': [{'type': 'message', 'content': [{'type': 'output_text', 'text': json.dumps(response)}]}]}
        for tier in ('default', 'fast', 'ultrafast'):
            settings = dict(enabled(), service_tier=tier)
            with patch.dict('os.environ', {'OPENAI_API_KEY': 'fictional'}), patch('requests.post', return_value=Response()) as request:
                _, receipt = commentary.OpenAIClient(settings)(commentary.prepare_packet(packet), 1)
                self.assertEqual(request.call_args.kwargs['json']['service_tier'], tier)
                self.assertEqual(receipt['service_tier'], 'default')
        settings = dict(enabled(), input_usd_per_million=2, cached_input_usd_per_million=.1, output_usd_per_million=10)
        def cached(packet, timeout):
            answer, receipt = fake_response(packet, timeout)
            receipt['input_tokens_details'] = {'cached_tokens': 80}
            return answer, receipt
        result = commentary.generate(packet, settings, commentary.Budget(self.root/'cost.sqlite', 1), time.time()+5, cached)
        self.assertAlmostEqual(result['estimated_usd'], (20*2+80*.1+80*10)/1_000_000)
        atomic(self.root/'packages'/'f00000001'/'requests.json', {'previous': {'key': 'test', 'packet': packet}})
        output = self.root/'benchmark'
        with patch('sys.stdout', new_callable=io.StringIO) as printed, patch('live_pipeline.commentary.generate', side_effect=AssertionError('Dry run must be free')):
            benchmark.main(['--run', str(self.root), '--models', str(ROOT/'uploads/live-commentary-benchmark.example.json'),
                            '--output', str(output), '--limit', '1', '--dry-run'])
        self.assertIn('ultrafast', printed.getvalue())
        self.assertIn('No requests sent', printed.getvalue())
        self.assertFalse(output.exists())

    def test_cache_prefix_and_compact_facts_preserve_information(self):
        current = comparisons.summary(numerical())
        def packet(chance):
            return commentary.prepare_packet(comparisons.prompt_packet(current,
                comparisons.compare(current, comparisons.summary(numerical('origin', None, chance)), [], {}, {})))
        first, second = packet(50), packet(30)
        messages = prompt_layout.input_messages(first, commentary.INSTRUCTIONS, enabled())
        other = prompt_layout.input_messages(second, commentary.INSTRUCTIONS, enabled())
        self.assertEqual(messages[0], other[0])
        self.assertEqual(messages[1]['content'][:2], other[1]['content'][:2])
        self.assertNotEqual(messages[1]['content'][2], other[1]['content'][2])
        compact = json.loads(messages[1]['content'][2]['text'].split('\n', 1)[1])
        levels = [r for measures in compact['figures'].values() for group in measures.values() for r in group]
        contexts = [r for booths in compact['context_figures'].values() for measures in booths.values()
                    for group in measures.values() for r in group]
        self.assertEqual(len(levels)+len(contexts), len(first['display_figures']))
        for source in first['display_figures']:
            if 'before' in source:
                group = compact['figures'][source['seat']][source['quantity']]
                decoded = []
                for row in group:
                    if len(row) == 3:
                        level = row[2]
                        zero = '+0.0 pp' if level.endswith('%') else '+0 votes' if level.endswith(' votes') else '+0.0 seats'
                        decoded.append([level, level, zero])
                    else:
                        decoded.append(row[2:])
                self.assertIn([source['before'], source['after'], source['change']], decoded)
        self.assertNotIn('win_changes', compact)
        self.assertNotIn('prompt_cache_breakpoint', messages[1]['content'][2])
        benchmark_input = commentary.request_payload(first, dict(enabled(), cache_comparison=True))
        self.assertIn('prompt_cache_breakpoint', benchmark_input['input'][1]['content'][2])
        single_window = commentary.request_payload(first, dict(enabled(), cache_overview=False))['input'][1]['content']
        self.assertIn('prompt_cache_breakpoint', single_window[0])
        self.assertNotIn('prompt_cache_breakpoint', single_window[1])
        # A successive forecast changes the overview and comparison, while
        # election labels and editorial instructions remain reusable.
        later = deepcopy(second)
        later['source_time'] = '2026-03-21T19:00:39'
        later_messages = prompt_layout.input_messages(later, commentary.INSTRUCTIONS, enabled())
        self.assertEqual(messages[0], later_messages[0])
        self.assertEqual(messages[1]['content'][0], later_messages[1]['content'][0])
        self.assertNotEqual(messages[1]['content'][1], later_messages[1]['content'][1])

    def test_short_levels_preserve_independently_rounded_changes(self):
        values = [('5.8 seats', '5.8 seats', '+0.0 seats'), ('5.8 seats', '5.9 seats', '+0.0 seats'),
                  ('47.9%', '47.9%', '+0.1 pp'), ('47.9%', '47.9%', '+0.0 pp'), ('0 votes', '0 votes', '+0 votes')]
        rows = [dict(seat='Light', quantity='test', before=a, after=b, change=d) for a,b,d in values]
        actual = prompt_layout._figure_tables(rows)['figures']['Light']['test']
        self.assertEqual([r[2:] for r in actual],
                         [['5.8 seats'], list(values[1]), list(values[2]), ['47.9%'], ['0 votes']])

    def test_compact_roster_and_shared_observations_preserve_identities_and_withdrawals(self):
        roster = prompt_layout._district_roster({'all_seats': ['Light', 'Old seat'],
            'all_candidate_identities': {'Light': {'-1': {'name': 'First independent'}, '-2': {'name': 'Second independent'}}},
            'seat_holders': {'Light': {'party_id': '0', 'margin': 0}, 'Older seat': {'party_id': '3'}}})
        self.assertEqual(roster, {'Light': [{'-1': 'First independent', '-2': 'Second independent'}, '0', 0],
                                  'Old seat': [None, None, None], 'Older seat': [None, '3', None]})
        common = {'fp_votes_current': {'0': 407, '2': 19}, 'count_to_projection_ids': {'0': '0', '2': '2'}}
        before = dict(common, tcp_votes_current={'0': 0}, expected_remaining_fp=1)
        after = dict(common, expected_remaining_fp=True)
        for kind in ('booths', 'preferences'):
            event = prompt_layout._prompt_event(dict(kind=kind, record='Hall', before=before, after=after))
            self.assertEqual(event['common'], common)
            self.assertEqual(event['before']['tcp_votes_current'], {'0': 0})
            self.assertNotIn('tcp_votes_current', event['after'])
            for label, original in [('before', before), ('after', after)]:
                rebuilt = dict(event['common'], **event[label])
                self.assertEqual(prompt_layout.canonical(rebuilt), prompt_layout.canonical(original))
        self.assertNotIn('common', prompt_layout._prompt_event(dict(kind='preferences', before=None, after=after)))

    def test_cache_writes_and_incomplete_responses_are_billed(self):
        settings = dict(enabled(), input_usd_per_million=2, cached_input_usd_per_million=.1, output_usd_per_million=10)
        usage = {'input_tokens': 1000, 'output_tokens': 200,
                 'input_tokens_details': {'cached_tokens': 500, 'cache_write_tokens': 300,
                    'cache_write_tokens_details': {'ttl_30m_tokens': 200, 'ttl_12h_tokens': 100}}}
        self.assertAlmostEqual(commentary.usage_cost(usage, settings), (200*2+500*.1+200*2*1.25+100*2*2.5+200*10)/1e6)
        packet = comparisons.prompt_packet(comparisons.summary(numerical()),
            comparisons.compare(comparisons.summary(numerical()), comparisons.summary(numerical('origin', None)), [], {}, {}))
        def incomplete(*_):
            raise commentary.ResponseFailure('Output token limit reached', usage)
        with self.assertRaises(commentary.ResponseFailure):
            commentary.generate(packet, settings, commentary.Budget(self.root/'incomplete.sqlite', 1), time.time()+5, incomplete)
        import sqlite3
        with closing(sqlite3.connect(self.root/'incomplete.sqlite')) as database:
            cost = database.execute('SELECT cost FROM calls').fetchone()[0]
        self.assertAlmostEqual(cost, commentary.usage_cost(usage, settings))

    def test_benchmark_twelve_variants_once_per_pair_and_restart(self):
        models = read(ROOT/'uploads/sa-commentary-benchmark.json')
        self.assertEqual(len(models), 12)
        self.assertEqual({m['reasoning_effort'] for m in models}, {'low', 'medium'})
        self.assertEqual(len({fingerprint(benchmark.variant_identity(m)) for m in models}), 12)
        self.assertFalse(any(m['model'] == 'gpt-6-astra' and m['service_tier'] != 'default' for m in models))
        current = comparisons.summary(numerical())
        packet = comparisons.prompt_packet(current,
            comparisons.compare(current, comparisons.summary(numerical('origin', None)), [], {}, {}))
        atomic(self.root/'packages'/'f00000001'/'requests.json', {
            'previous': {'packet': packet}, '15m': {'packet': packet}, 'since_start': {'packet': packet}})
        selected = benchmark.select_packets(self.root, reference_id='origin')
        self.assertEqual(len(selected), 1)
        calls = []
        def factory(model):
            def request(packet, timeout):
                calls.append(benchmark.variant_identity(model))
                return fake_response(packet, timeout)
            return request
        output = self.root/'benchmark'
        results = benchmark.run_benchmark(selected, models, output, client_factory=factory)
        self.assertEqual(len(calls), 12)
        self.assertTrue(all(r['status'] == 'complete' for r in results))
        benchmark.run_benchmark(selected, models, output, client_factory=lambda _: self.fail('Restart must not call'))
        self.assertEqual(len(calls), 12)
        self.assertTrue((output/'review.html').exists())
        # A crash after dispatch is deliberately not retried: the provider may
        # have billed that attempt even if no response survived locally.
        receipt = next(p for p in (output/'calls').glob('*.json') if not p.name.endswith('.started.json'))
        receipt.unlink()
        results = benchmark.run_benchmark(selected, models, output, client_factory=lambda _: self.fail('Interrupted call must not repeat'))
        self.assertEqual(sum(r['status'] == 'unavailable' for r in results), 1)

    def test_trial_delivers_serially_waits_for_key_and_benchmarks_adjacent_pairs(self):
        workspace = self.root/'workspace'
        archive = workspace/'archive'; archive.mkdir(parents=True)
        codes = ['20260321183853', '20260321192846', '20260321193347', '20260321193448']
        names = ['el2026260321183853.xml', 'el2026260321192846.xml', 'el2026260321193347.xml', 'el2026260321193448.xml']
        for name in names:
            (archive/name).write_text('<fictional/>')
        models = read(ROOT/'uploads/sa-commentary-benchmark.json')
        atomic(workspace/'models.json', models)
        conf = dict(config(), source_directory='inbox', commentary_trial={
            'enabled': True, 'paid_benchmark_requested': True, 'budget_usd': 10,
            'archive': 'archive', 'capture_codes': codes, 'models_file': 'models.json'})
        atomic(self.root/'run.json', conf)
        state = {'state': 'armed', 'captures': {}}
        atomic(self.root/'state.json', state)
        calls = []
        def factory(model):
            def request(packet, timeout):
                calls.append((packet['reference_forecast_id'], packet['forecast_id']))
                return fake_response(packet, timeout)
            return request
        runner = trial.Trial(workspace, self.root, key_available=lambda: False, client_factory=factory)
        try:
            self.assertFalse(runner.tick())
            self.assertTrue((workspace/'inbox'/names[0]).exists())
            self.assertFalse((workspace/'inbox'/names[1]).exists())
            first = numerical('f00000001', '2026-03-21T18:06:32', 50)
            state['captures']['a'] = {'file': names[0], 'state': 'accepted', 'forecast': 'completed', 'forecast_id': first['forecast_id']}
            put(self.root, first); atomic(self.root/'state.json', state)
            with patch('live_pipeline.comparisons.evidence', return_value=observed({'a': 0})):
                self.assertFalse(runner.tick())
            self.assertTrue((workspace/'inbox'/names[1]).exists())
            second = numerical('f00000002', '2026-03-21T18:57:46', 70)
            state['captures']['b'] = {'file': names[1], 'state': 'accepted', 'forecast': 'completed', 'forecast_id': second['forecast_id']}
            put(self.root, second); atomic(self.root/'state.json', state)
            with patch('live_pipeline.comparisons.evidence', return_value=observed({'a': 100})):
                self.assertFalse(runner.tick())
            self.assertTrue((workspace/'inbox'/names[2]).exists())
            self.assertEqual(calls, [])
            third = numerical('f00000003', '2026-03-21T19:00:39', 75)
            state['captures']['c'] = {'file': names[2], 'state': 'accepted', 'forecast': 'completed', 'forecast_id': third['forecast_id']}
            put(self.root, third); atomic(self.root/'state.json', state)
            with patch('live_pipeline.comparisons.evidence', return_value=observed({'a': 150})), \
                    patch('live_pipeline.trial.read', wraps=read) as ledger_reads:
                self.assertFalse(runner.tick())
            # Advancing through already completed captures should not reopen
            # the GUI's growing ledger once for every earlier forecast.
            # Windows can supply a short temp path while Trial uses its resolved name.
            state_file = self.root/'state.json'
            state_reads = [call for call in ledger_reads.call_args_list
                           if state_file.samefile(call.args[0])]
            self.assertEqual(len(state_reads), 1)
            self.assertTrue((workspace/'inbox'/names[3]).exists())
            self.assertEqual(calls, [])
            fourth = numerical('f00000004', '2026-03-21T19:03:09', 77)
            state['captures']['d'] = {'file': names[3], 'state': 'accepted', 'forecast': 'completed', 'forecast_id': fourth['forecast_id']}
            put(self.root, fourth); atomic(self.root/'state.json', state)
            with patch('live_pipeline.comparisons.evidence', return_value=observed({'a': 180})):
                self.assertFalse(runner.tick())
            self.assertEqual(read(self.root/'trial-status.json')['stage'], 'waiting_for_key')
            self.assertEqual(calls, [])
            runner.key_available = lambda: True
            state['state'] = 'paused'; atomic(self.root/'state.json', state)
            self.assertFalse(runner.tick()); self.assertEqual(calls, [])
            state['state'] = 'armed'; atomic(self.root/'state.json', state)
            self.assertTrue(runner.tick())
            self.assertEqual(calls.count(('f00000001', 'f00000002')), 12)
            self.assertEqual(calls.count(('f00000002', 'f00000003')), 12)
            self.assertEqual(calls.count(('f00000003', 'f00000004')), 12)
            self.assertEqual(len(calls), 36)
            runner.key_available = lambda: False
            self.assertTrue(runner.tick())
            self.assertEqual(len(calls), 36)
            review = (self.root/'benchmark'/'review.html').read_text(encoding='utf-8')
            self.assertIn('Update 1:', review)
            self.assertIn('Update 2:', review)
            self.assertIn('Update 3:', review)
            self.assertIn('f00000002 to f00000003', review)
        finally:
            runner.close()

    def test_trial_finishes_without_paid_opt_in(self):
        codes = ['20260321183853', '20260321192846']
        archive = self.root/'archive'
        archive.mkdir()
        records = {}
        for i, code in enumerate(codes, 1):
            filename = 'el2026'+code[2:]+'.xml'
            (archive/filename).write_text('<fictional/>')
            identity = f'f{i:08}'
            put(self.root, numerical(identity, f'2026-03-21T18:0{i}:00', 40+i))
            records[code] = {'file': filename, 'forecast': 'completed', 'forecast_id': identity}
        atomic(self.root/'state.json', {'state': 'armed', 'captures': records})
        for choice in ({'paid_benchmark_requested': False}, {}, {'paid_authorized': True}):
            with self.subTest(choice=choice):
                settings = dict(choice, enabled=True, archive='archive', capture_codes=codes)
                atomic(self.root/'run.json', dict(config(), source_directory='inbox', commentary_trial=settings))
                runner = trial.Trial(self.root, self.root, key_available=lambda: self.fail('Unexpected key lookup'))
                try:
                    with patch.object(benchmark, 'run_benchmark') as paid:
                        self.assertTrue(runner.tick())
                        paid.assert_not_called()
                    self.assertEqual(read(self.root/'trial-status.json')['stage'], 'complete')
                    self.assertTrue((self.root/'packages'/'f00000002'/'publication.json').is_file())
                finally:
                    runner.close()

    def test_benchmark_keeps_successive_updates_together_per_configuration(self):
        models = read(ROOT/'uploads/sa-commentary-benchmark.json')
        old, middle, new = [comparisons.summary(numerical(f'f{i:08}', f'2026-03-21T18:{i:02}:00', 40+i))
                            for i in range(1, 4)]
        selected = [(str(i), comparisons.prompt_packet(after, comparisons.compare(after, before, [], {}, {})))
                    for i, (before, after) in enumerate([(old, middle), (middle, new)])]
        calls = []
        def factory(model):
            def request(packet, timeout):
                calls.append((benchmark.variant_identity(model), packet['forecast_id']))
                return fake_response(packet, timeout)
            return request
        # One worker makes ordering observable without imposing an order across
        # independent concurrent model groups in the actual benchmark.
        results = benchmark.run_benchmark(selected, models, self.root/'benchmark', concurrency=1, client_factory=factory)
        self.assertEqual(len(calls), 24)
        for first, second in zip(calls[::2], calls[1::2]):
            self.assertEqual(first[0], second[0])
            self.assertEqual((first[1], second[1]), ('f00000002', 'f00000003'))
        self.assertTrue(all(r['status'] == 'complete' for r in results))

    def test_full_benchmark_budget_progress_and_restart(self):
        models = read(ROOT/'uploads/sa-commentary-full-benchmark.json')
        self.assertEqual([benchmark.variant_identity(model) for model in models], [
            {'model': name, 'service_tier': 'default', 'reasoning_effort': 'low'}
            for name in ('gpt-6.1-terra', 'gpt-6.1-sol')])
        selected = []
        for index in range(1, 13):
            before = comparisons.summary(numerical(f'f{index:08}', f'2026-03-21T18:{index:02}:00', 40+index))
            after = comparisons.summary(numerical(f'f{index+1:08}', f'2026-03-21T18:{index+1:02}:00', 41+index))
            selected.append((str(index), comparisons.prompt_packet(after,
                comparisons.compare(after, before, [], {}, {}))))
        calls, progress = [], []
        def factory(model):
            def request(packet, timeout):
                calls.append((model['model'], packet['forecast_id']))
                return fake_response(packet, timeout)
            return request
        output = self.root/'full-benchmark'
        results = benchmark.run_benchmark(selected, models, output, budget_usd=15,
            client_factory=factory, on_progress=lambda done, total: progress.append((done, total)))
        self.assertEqual(len(results), 24)
        self.assertEqual(len(set(calls)), 24)
        self.assertTrue(all(result['status'] == 'complete' for result in results))
        self.assertEqual(progress, [(i, 24) for i in range(1, 25)])
        self.assertEqual(read(output/'benchmark.json')['budget_ceiling_usd'], 15)
        resumed = benchmark.run_benchmark(selected, models, output, budget_usd=15,
            client_factory=lambda _: self.fail('Full replay restart must not repeat paid calls'))
        self.assertEqual(len(resumed), 24)
        for invalid in (0, -1, 15.01, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                benchmark.run_benchmark(selected, models, self.root/'invalid', budget_usd=invalid)
        exhausted = benchmark.run_benchmark(selected, models, self.root/'exhausted', budget_usd=1e-9,
            client_factory=lambda _: lambda *_: self.fail('Exhausted budget must not dispatch'))
        self.assertTrue(all(result['status'] == 'unavailable' and 'budget' in result['reason'].lower()
                            for result in exhausted))

    def test_seat_bullets_publication_and_previous_paragraph_preset(self):
        current = comparisons.summary(numerical())
        packet = comparisons.prompt_packet(current, comparisons.compare(
            current, comparisons.summary(numerical('origin', None)), [], {}, {}))
        supplied = commentary.prepare_packet(packet)
        response, usage = fake_response(supplied, 1)
        seat = response['developments'][0]['seat']
        response['developments'][0]['booth_evidence'] = ['Hall reported its first votes.']
        budget = commentary.Budget(self.root/'test.sqlite', 1)
        def generate(settings, reply):
            return commentary.generate(packet, settings, budget, time.time()+5, lambda *_: (reply, usage))
        result = generate(enabled(), response)
        self.assertEqual(result['format'], 'seat-bullets-v1')
        text = result['text']
        self.assertEqual(text['summary'], 'Key changes:\n- '+response['key_changes'][0])
        self.assertEqual(text['developments'], [{'seats': [seat], 'text':
            'In {seat:'+seat+'}:\n- '+response['developments'][0]['seat_changes'][0]+'\n- Hall reported its first votes.'}])
        benchmark.write_review(self.root, [result], 1)
        page = (self.root/'review.html').read_text(encoding='utf8')
        self.assertEqual(page.count('<ul>'), 2)
        self.assertEqual(page.count('<li>'), 3)
        self.assertIn('<h3>Key changes:</h3>', page)
        padded = deepcopy(response)
        padded['developments'][0]['booth_evidence'] = [' \tHall reported its first votes. ']
        self.assertEqual(commentary.format_points(padded, supplied), text)
        for bad_point in ('  - Already a bullet.', '  Key changes:', 'First line.\n- Extra line.',
                          'Single line followed by a newline.\r\n', '', ' \t'):
            invalid = deepcopy(response)
            invalid['developments'][0]['booth_evidence'] = [bad_point]
            with self.subTest(bad_point=bad_point), self.assertRaises(ValueError):
                commentary.format_points(invalid, supplied)
        duplicate = deepcopy(response)
        duplicate['developments'] *= 2
        with self.assertRaisesRegex(ValueError, 'repeated'):
            commentary.format_points(duplicate, supplied)
        quiet = generate(enabled(), dict(response, key_changes=['There were no significant changes to the forecast.'], developments=[]))
        self.assertEqual(quiet['format'], 'inline-figures-v1')
        self.assertEqual(quiet['text']['summary'], 'There were no significant changes to the forecast.')
        original_bullets = dict(enabled(), prompt_version='live-changes-9')
        self.assertEqual(generate(original_bullets, response)['format'], 'seat-bullets-v1')
        previous = dict(enabled(), prompt_version='live-changes-8')
        self.assertEqual(commentary.request_payload(supplied, previous)['text']['format']['schema'], commentary.SCHEMA)
        legacy = dict(text, summary='Earlier paragraph report.', developments=[])
        restored = generate(previous, legacy)
        self.assertEqual(restored['text'], legacy)
        self.assertEqual(restored['prompt_version'], 'live-changes-8')
        self.assertEqual(restored['format'], 'inline-figures-v1')

    def test_local_headline_publishes_its_own_verified_comparison_or_only_the_outcome(self):
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        before = observed({'0': 50, '2': 50}, {'0': 0, '2': 0})
        after = observed({'0': 65, '2': 35}, {'0': 70, '2': 30})
        before['booths']['Light/Hall']['tcp_votes_projected'] = {'0': 55, '2': 45}
        # The partly counted Hall still has enough unallocated votes for
        # the district median. Its projected FP mix is evenly divided.
        before['booths']['Light/Hall'].update(fp_votes_projected={'0':100,'2':100}, tcp_votes_projected={'0':110,'2':90})
        old['seats']['Light']['fp_mean_percent'] = {'0':50,'2':50}
        old['seats']['Light']['tcp_percentiles']['0/2']['50'] = 55
        packet = commentary.prepare_packet(comparisons.prompt_packet(new, comparisons.compare(new, old, [], after, before)))
        outcome = 'In {seat:Light}, {candidate:Light:0} became more likely to win.'
        selected = {'outcome': outcome, 'booth_record': 'Light/Hall', 'candidate': '{candidate:Light:0}', 'count_type': 'TCP'}
        def render(headline, seats=None, source=None):
            return commentary.format_points({'key_changes': [headline], 'developments': seats or [],
                'insufficient_evidence': False}, source or packet, link_local_headlines=True)
        plain = 'Key changes:\n- '+outcome
        published = render(selected)
        self.assertEqual(render(outcome+' ')['summary'], plain)
        self.assertEqual(render(dict(selected, outcome=outcome+' ')), published)
        self.assertIn('following the result at Hall', published['summary'])
        detail = published['developments'][0]['text']
        self.assertIn('At Hall, {candidate:Light:0} received **70.0%** of **100** two-candidate-preferred votes', detail)
        self.assertIn('against {candidate:Light:2}, **15.0 pp** above the earlier estimate', detail)
        self.assertIn('**65.0%** of **100** first preferences, **15.0 pp** above the earlier estimate',
                      render(dict(selected, count_type='FP'))['developments'][0]['text'])
        district = {'seat': 'Light', 'seat_changes': ['The district projection moved.'], 'booth_evidence': []}
        self.assertEqual(len(render(selected, [district])['developments']), 1)
        # A TCP quote with no matching expectation is filtered from the body.
        # A district projection or another candidate's count cannot replace it.
        district['booth_evidence'] = ['At Hall, {candidate:Light:0} received **70.0%** against {candidate:Light:2}.']
        for bad in (dict(selected, booth_record='Other/Hall'), dict(selected, candidate='{candidate:Schubert:0}'),
                    dict(selected, candidate='{candidate:Light:9}')):
            fallback = render(bad, [district])
            self.assertEqual(fallback['summary'], plain)
            self.assertEqual(fallback['developments'][0]['text'], 'In {seat:Light}:\n- The district projection moved.')
        missing = deepcopy(packet)
        for e in missing['evidence']:
            if e['kind'] == 'booths':
                e.pop('tcp_against_previous_forecast', None)
        missing.pop('display_figures')
        self.assertEqual(render(selected, [district], missing)['summary'], plain)
        empty = {'seat': 'Light', 'seat_changes': [], 'booth_evidence': []}
        self.assertEqual(render(selected, [empty], missing)['developments'], [])
        # A declaration category can carry the same comparison as an ordinary
        # booth, including the case where the observed share matches exactly.
        postal = deepcopy(packet)
        booth = next(e for e in postal['evidence'] if e['kind'] == 'booths')
        booth['record'] = 'Light/Postal'
        booth['after'].update(name='Postal', vote_type='Postal')
        booth['fp_share_minus_previous_expectation_pp']['0'] = 0
        postal.pop('display_figures')
        generated = render(dict(selected, booth_record='Light/Postal', count_type='FP'), source=postal)
        self.assertIn('following the Postal count', generated['summary'])
        self.assertIn('matching the earlier estimate', generated['developments'][0]['text'])
        self.assertEqual(render(outcome[:-1]+' following strong local counting.')['summary'], plain)
        broader = outcome[:-1]+' in line with broader trends.'
        self.assertEqual(render(broader)['summary'], 'Key changes:\n- '+broader)
        legacy = commentary.request_payload(packet, dict(enabled(), prompt_version='live-changes-22'))
        current = commentary.request_payload(packet, enabled())
        self.assertEqual(legacy['text']['format']['schema'], commentary.POINT_SCHEMA)
        self.assertEqual(current['text']['format']['schema'], commentary.LINKED_POINT_SCHEMA)
        self.assert_same_comparison_figures(legacy, current)

    def test_headline_and_writer_can_describe_one_primary_count_together(self):
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        before = observed({'0': 50, '2': 50})
        after = observed({'0': 65, '2': 35})
        packet = commentary.prepare_packet(comparisons.prompt_packet(new, comparisons.compare(new, old, [], after, before)))
        headline = {'outcome': 'In {seat:Light}, the outlook changed.', 'booth_record': 'Light/Hall',
                    'candidate': '{candidate:Light:0}', 'count_type': 'FP'}
        other = 'At Hall, {candidate:Light:2} received **35.0%** of **100 votes** in first preferences, **15.0 pp** below expectations.'
        correction = 'At Hall, an earlier count was revised.'
        district = {'seat': 'Light', 'seat_changes': [], 'booth_evidence': [other, correction]}
        reply = {'key_changes': [headline], 'developments': [district], 'insufficient_evidence': False}
        rendered = commentary.format_points(reply, packet, link_local_headlines=True)
        lines = rendered['developments'][0]['text'].splitlines()
        self.assertEqual(lines, ['In {seat:Light}:',
            '- At Hall, {candidate:Light:0} received **65.0%** of **100** first preferences, **15.0 pp** above the earlier estimate. '
            '{candidate:Light:2} received **35.0%**, **15.0 pp** below the earlier estimate.',
            '- '+correction])
        # An ambiguous place name cannot establish that these are the same ballots.
        booth = next(e for e in packet['evidence'] if e['kind'] == 'booths')
        packet['evidence'].append(dict(booth, record='Light/PPVC/Hall'))
        separate = commentary.format_points(reply, packet, link_local_headlines=True)['developments'][0]['text']
        self.assertIn('\n- '+other, separate)
        self.assertIn('Group first preferences by booth', commentary.instructions(commentary.PROMPT_VERSION))
        self.assertNotIn('Group first preferences by booth', commentary.instructions('live-changes-23'))

    def test_postal_headline_and_writer_share_one_verified_primary_count(self):
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        before, after = observed({'0': 50, '2': 50}), observed({'0': 65, '2': 35})
        record = 'Light/Other/Postal/Postal'
        for evidence in (before, after):
            unit = evidence['booths'].pop('Light/Hall')
            unit.update(name='Postal', vote_type='Postal', booth_type='Other')
            evidence['booths'][record] = unit
        packet = commentary.prepare_packet(comparisons.prompt_packet(new, comparisons.compare(new, old, [], after, before)))
        outcome = 'In {seat:Light}, the outlook changed.'
        headline = {'outcome': outcome, 'booth_record': record, 'candidate': '{candidate:Light:0}', 'count_type': 'FP'}
        writer = 'At Postal, {candidate:Light:2} received **35.0%** of **100 votes** in first preferences, **15.0 pp** below expectations.'
        correction = 'At Postal, an earlier count was revised.'

        def render(key_change, points):
            reply = {'key_changes': [key_change], 'insufficient_evidence': False,
                     'developments': [{'seat': 'Light', 'seat_changes': [], 'booth_evidence': points}]}
            result = commentary.format_points(reply, packet, link_local_headlines=True)
            return result['developments'][0]['text'].splitlines()[1:]

        self.assertEqual(render(headline, [writer, correction]), [
            '- In the Postal count, {candidate:Light:0} received **65.0%** of **100** first preferences, **15.0 pp** above the earlier estimate. '
            '{candidate:Light:2} received **35.0%**, **15.0 pp** below the earlier estimate.',
            '- In the Postal count, an earlier count was revised.'])
        self.assertEqual(render(outcome, [writer]), [
            '- In the Postal count, {candidate:Light:2} received **35.0%** of **100** first preferences, **15.0 pp** below the earlier estimate.'])
        # A physical booth with this name makes “At Postal” ambiguous. The
        # explicit declaration label can still identify the headline's count.
        event = next(e for e in packet['evidence'] if e['kind'] == 'booths')
        physical = deepcopy(event)
        physical['record'] = 'Light/Normal/Ordinary/Postal'
        physical['after'].update(booth_type='Normal', vote_type='Ordinary')
        packet['evidence'].append(physical)
        self.assertEqual(render(headline, [writer])[-1], '- '+writer)

    def test_local_win_movements_in_commentary_request(self):
        current = comparisons.summary(numerical())
        packet = comparisons.prompt_packet(current, comparisons.compare(
            current, comparisons.summary(numerical('origin', None, chance=40)), [], {}, {}))
        # One locally counted district has the same total after a correction.
        # Another district moves without receiving votes. Replacing a candidate
        # also breaks the comparison for that candidate's win chance.
        packet['observed_count_changes_by_seat'] = {
            'Schubert': {'affected_votes': 12, 'fp_net_change': 0},
            'Finniss': {'affected_votes': 100}, 'Light': {'affected_votes': 30}}
        for row in packet['win_changes']:
            row['identity_changed'] = row['seat'] == 'Finniss'
        packet = commentary.prepare_packet(packet)
        def evidence(version):
            payload = commentary.request_payload(packet, dict(enabled(), prompt_version=version))
            return json.loads(payload['input'][1]['content'][-1]['text'].split('\n', 1)[1])
        self.assertEqual(evidence(commentary.PROMPT_VERSION)['local_seats_by_win_movement'], ['Light', 'Schubert'])
        self.assertNotIn('local_seats_by_win_movement', evidence('live-changes-9'))
        unchanged = 'win_movements_without_changed_local_counts'
        packet['count_activity'] = {'known': True, 'changed_votes': 142, 'endpoint_changed_votes': 142}
        self.assertIn('Hammond', evidence(commentary.PROMPT_VERSION)[unchanged])
        self.assertNotIn('Schubert', evidence(commentary.PROMPT_VERSION)[unchanged])
        self.assertNotIn(unchanged, evidence('live-changes-13'))
        # A within-window reversal or an incomplete account of the votes
        # prevents us from placing all reported activity in named districts.
        for changed, endpoint in [(150, 142), (150, 150)]:
            packet['count_activity'].update(changed_votes=changed, endpoint_changed_votes=endpoint)
            self.assertNotIn(unchanged, evidence(commentary.PROMPT_VERSION))
        del packet['observed_count_changes_by_seat']
        self.assertNotIn('local_seats_by_win_movement', evidence(commentary.PROMPT_VERSION))

    def test_statewide_seats_use_the_displayed_medians_and_party_badges(self):
        earlier = numerical('earlier', chance=50)
        later = numerical()
        earlier['report']['seatCountFrequencies'] = [[0, [0, 2, 4]], [2, [0, 1, 3]]]
        later['report']['seatCountFrequencies'] = [[0, [1, 2, 4]], [2, [0, 2, 3]]]
        earlier['report']['expectedSeats'] = [[0, 2.2], [2, 1.1]]
        package = put(self.root, earlier)
        old = comparisons.summary(earlier)
        del old['statewide']['medianSeats']
        atomic(package/'summary.json', old)
        original = (package/'summary.json').read_bytes()
        old = comparisons.comparison_summary(package)
        self.assertEqual((package/'summary.json').read_bytes(), original)
        new = comparisons.summary(later)
        packet = comparisons.prompt_packet(new, comparisons.compare(new, old, [], {}, {}))
        supplied = commentary.prepare_packet(packet)
        rows = {r['party']: r for r in supplied['display_figures'] if r['quantity'] == 'median seats'}
        self.assertEqual((rows['{party:2}']['before'], rows['{party:2}']['after']), ('1 seat', '2 seats'))
        self.assertEqual(rows['{party:0}']['change'], '+0 seats')
        self.assertFalse(any(r['quantity'] == 'expectedSeats' for r in supplied['display_figures']))
        commentary_format.check_text('The median for {party:2} rose from **1 seat** to **2 seats**.', [], supplied)
        with self.assertRaisesRegex(ValueError, 'Unsupported numerical statement'):
            commentary_format.check_text('{party:2} rose to **1.8 seats**.', [], supplied)
        legacy = commentary.prepare_packet(packet, 'live-changes-25')
        self.assertTrue(any(r['quantity'] == 'expectedSeats' for r in legacy['display_figures']))
        del packet['statewide']['before']['medianSeats']
        self.assertFalse(any(r['quantity'] == 'median seats' for r in commentary.prepare_packet(packet)['display_figures']))

        # A combined Coalition median, when available, comes from its own
        # distribution. The component coded LNP cannot supply the combined row.
        mixed = dict(later['report'], partyAbbr=[[0, 'ALP'], [1, 'LNP'], [2, 'NAT'], [-4, 'LNP']],
                     seatCountFrequencies=[[0, [0, 1, 3]], [1, [1, 2, 3]], [2, [1, 2, 3]]],
                     coalitionSeatCountFrequencies=[2, 3, 5])
        self.assertEqual(comparisons.median_seats(mixed), {'0': 1, '2': 2, '-4': 3})

    def test_inline_commentary_bold_figures_and_entities(self):
        before = comparisons.summary(numerical('origin', None, 40.38))
        after = comparisons.summary(numerical(chance=47.87))
        packet = commentary.prepare_packet(comparisons.prompt_packet(after,
            comparisons.compare(after, before, [], {}, {})))
        text = {'summary': 'The outlook changed in {seat:Light}.',
                'developments': [{'seats': ['Light'], 'text': '{candidate:Light:0} rose to **47.9% (+7.5 pp)**.'}],
                'insufficient_evidence': False}
        commentary.validate(text, packet)
        rendered = commentary_format.render(text['developments'][0]['text'], packet['entities'])
        self.assertIn('<strong>47.9% (+7.5 pp)</strong>', rendered)
        self.assertIn('Fictional Labor candidate', rendered)
        self.assertEqual(commentary_format.render('{party:2}', packet['entities']), '<small class="badge">ON</small>')
        for invalid in ('**47.9% (+7.5%)**', '**47.9% (-7.5 pp)**', '47.9% (+7.5 pp)'):
            bad = deepcopy(text); bad['developments'][0]['text'] = invalid
            with self.assertRaises(ValueError): commentary.validate(bad, packet)
        text['developments'][0]['seats'].append('Hammond')
        text['developments'][0]['text'] += ' The same outlook applies to {candidate:Hammond:0}.'
        commentary.validate(text, packet)
        packet['entities']['{candidate:Light:0}']['label'] = '<script>fictional</script>'
        rendered = commentary_format.render('{candidate:Light:0}', packet['entities'])
        self.assertNotIn('<script>', rendered)
        self.assertIn('&lt;script&gt;', rendered)
        benchmark.write_review(self.root, [{'status': 'complete', 'text': text, 'entities': packet['entities']}], 10)
        self.assertIn('<strong>47.9%', (self.root/'review.html').read_text(encoding='utf8'))

    def test_booth_comparisons_supply_unsigned_magnitudes_and_direction(self):
        before = comparisons.summary(numerical('origin', None))
        after = comparisons.summary(numerical())
        packet = comparisons.prompt_packet(after, comparisons.compare(after, before, [], {}, {}))
        packet['evidence'] = [{'kind': 'booths', 'seat': 'Light', 'record': 'Hall',
                              'fp_share_minus_previous_expectation_pp': {'0': -13.74, '1': 11.63}}]
        packet = commentary.prepare_packet(packet)
        rows = [r for r in packet['display_figures'] if r.get('booth') == 'Hall']
        self.assertEqual([(r['value'], r['magnitude'], r['direction']) for r in rows],
                         [('-13.7 pp', '13.7 pp', 'below expectations'), ('+11.6 pp', '11.6 pp', 'above expectations')])
        response = {'summary': 'The count changed in {seat:Light}.', 'insufficient_evidence': False,
                    'developments': [{'seats': ['Light'], 'text': 'In {seat:Light}, the primary share for '
                        '{candidate:Light:0} was **13.7 pp** below expectations at Hall.'}]}
        commentary.validate(response, packet)
        response['developments'][0]['text'] = response['developments'][0]['text'].replace('13.7', '13.8')
        with self.assertRaisesRegex(ValueError, 'Unsupported numerical'):
            commentary.validate(response, packet)
        messages = prompt_layout.input_messages(packet, commentary.INSTRUCTIONS, enabled())
        compact = json.loads(messages[1]['content'][2]['text'].split('\n', 1)[1])
        measure = 'FPdiff'
        booth_row = compact['context_figures']['Light']['Hall'][measure][0]
        self.assertEqual(booth_row[0], '0')
        values = dict(zip(compact['context_columns'][measure], booth_row[1:]))
        self.assertNotIn('value', values)
        self.assertEqual(values['magnitude'], '13.7 pp')
        self.assertEqual(values['direction'], 'below expectations')

    def test_explicit_candidate_counts_validate_without_admitting_projected_votes(self):
        before = comparisons.summary(numerical('origin', None))
        after = comparisons.summary(numerical())
        packet = comparisons.prompt_packet(after, comparisons.compare(after, before, [], {}, {}))
        packet['evidence'] = [
            {'kind': 'booths', 'seat': 'Light', 'record': 'Hall',
             'before': {'fp_votes_current': {'0': 47, '2': 36}, 'tcp_votes_current': {'0': 0, '2': 0}},
             'after': {'fp_votes_current': {'0': 47, '2': 36}, 'tcp_votes_current': {'0': 83, '2': 50},
                       'tcp_votes_projected': {'0': 859, '2': 500}}},
            {'kind': 'booths', 'seat': 'Hammond', 'record': 'Other', 'after': {'tcp_votes_current': {'0': 721}}}]
        packet = commentary.prepare_packet(packet)
        answer = {'summary': 'More votes were counted in {seat:Light}.', 'insufficient_evidence': False,
                  'developments': [{'seats': ['Light'], 'text': 'In {seat:Light}, {candidate:Light:0} received **83 votes** and {candidate:Light:2} received **50 votes** in the TCP count at Hall.'}]}
        commentary.validate(answer, packet)
        # Projected votes, unseen numbers and another district's count do not
        # become valid merely because the prompt contains some vote numbers.
        for unsupported in ['859 votes', '84 votes', '721 votes']:
            invalid = deepcopy(answer)
            invalid['developments'][0]['text'] = invalid['developments'][0]['text'].replace('83 votes', unsupported)
            with self.assertRaisesRegex(ValueError, 'Unsupported numerical'):
                commentary.validate(invalid, packet)

    def test_booth_percentages_use_the_current_count_and_its_own_denominator(self):
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        before = observed({'0': 60, 'ind': 10, 'other': 10}, {'0': 20, '1': 10})
        after = observed({'0': 86, 'ind': 130, 'other': 102}, {'0': 83, 'ind': 50})
        after['booths']['Light/Hall']['count_to_projection_ids'] = {'ind': '2'}
        after['seats'] = {'Light': {'count_progress': {'fp_completion': .0134084}}}
        packet = commentary.prepare_packet(comparisons.prompt_packet(new, comparisons.compare(new, old, [], after, before)))
        payload = commentary.request_payload(packet, enabled())
        context = json.loads(payload['input'][1]['content'][-1]['text'].split('\n', 1)[1])['context_figures']['Light']
        booth = context['Light/Hall']
        self.assertEqual(booth['FPtotal'], [[None, '318 votes']])
        # Unnamed ballots count towards all 318; the net addition was only 238.
        self.assertEqual(booth['FPshare'], [['0', '27.0%'], ['2', '40.9%']])
        self.assertNotIn('TCPobserved', booth)
        self.assertNotIn('TCPtotal', booth)
        self.assertNotIn('TCPshare', booth)  # No expectation for the replacement pair.
        previous = commentary.request_payload(packet, dict(enabled(), prompt_version='live-changes-16'))
        legacy = commentary.request_payload(packet, dict(enabled(), prompt_version='live-changes-30'))
        self.assert_same_comparison_figures(previous, legacy)
        # New requests change the district replacement table. The checked
        # individual-booth figures still have exactly the same denominators.
        previous_window = json.loads(previous['input'][1]['content'][-1]['text'].split('\n', 1)[1])
        self.assertEqual(previous_window['context_figures']['Light'], context)
        self.assertIn('[total] first preferences', payload['input'][0]['content'][0]['text'])
        self.assertNotIn('[total] first preferences', previous['input'][0]['content'][0]['text'])
        old_payload = commentary.request_payload(packet, dict(enabled(), prompt_version='live-changes-15'))
        old_booth = json.loads(old_payload['input'][1]['content'][-1]['text'].split('\n', 1)[1])['context_figures']['Light']['Light/Hall']
        self.assertEqual(old_booth['TCPtotal'], [[['0', '2'], '133 votes']])
        self.assertEqual(old_booth['TCPobserved'], [['0', '62.4%'], ['2', '37.6%']])
        self.assertEqual(context['']['estimated district FP count completion'], [[None, '1.3%']])
        prose = {'key_changes': ['Local votes arrived in {seat:Light}.'], 'insufficient_evidence': False,
                 'developments': [{'seat': 'Light', 'seat_changes': [], 'booth_evidence': [
                     'At Hall, {candidate:Light:0} polled **27.0%** of **318 first preferences**.',
                     '{candidate:Light:0} had **62.4%** of the **133 TCP votes** counted against {candidate:Light:2}.']}]}
        commentary.format_points(prose, packet, require_tcp_comparison=False)
        rendered = commentary.format_points(prose, packet)
        self.assertNotIn('62.4%', rendered['developments'][0]['text'])
        self.assertIn('27.0%', rendered['developments'][0]['text'])
        prose['developments'][0]['booth_evidence'].pop()
        commentary.format_points(prose, packet)
        prose['developments'][0]['booth_evidence'][0] = 'At Hall, {candidate:Light:0} polled **72.3%**.'
        with self.assertRaisesRegex(ValueError, 'Unsupported numerical'):
            commentary.format_points(prose, packet)

    def test_booth_shares_handle_withdrawals_unchanged_ledgers_and_ambiguous_aliases(self):
        def shares(before, after, aliases=None):
            event = {'seat':'Light', 'record':'Hall', 'before': {'fp_votes_current': before},
                     'after': {'fp_votes_current': after, 'count_to_projection_ids': aliases or {}}}
            return commentary_format.observed_booth_result(event, 'fp', {'0', '2'})
        for after in ({}, {'0':0,'2':0}, {'0':7,'2':5}):
            self.assertEqual(shares({'0':7,'2':5}, after), [])
        rows = shares({}, {'0':0,'a':6,'b':6}, {'a':'2','b':'2'})
        self.assertEqual(rows[0]['value'], '12 votes')
        self.assertEqual([(r['candidate'], r['observed_share']) for r in rows[1:]], [('{candidate:Light:0}', '0.0%')])

    def test_interval_labels_and_ranges_keep_their_pair_and_forecast(self):
        packet = {'all_seats': ['Light'], 'entities': {'{seat:Light}': {'seat': 'Light'}}, 'evidence': [],
                  'display_figures': [
                      {'seat': 'Light', 'quantity': 'TCP', 'pair': pair, 'percentile': q, 'before': old, 'after': new}
                      for pair, q, old, new in [
                          (['A','B'], '5', '39.1%', '41.7%'), (['A','B'], '95', '55.3%', '51.6%'),
                          (['A','C'], '5', '49.0%', '49.1%'), (['A','C'], '95', '57.3%', '56.8%')]]}
        check = lambda text: commentary_format.check_text(text, ['Light'], packet)
        check('The 90% TCP interval narrowed from **39.1%–55.3%** to **41.7%–51.6%**.')
        check('The **90%** interval starts at **41.7%**.')
        for invalid in ['**41.7%–55.3%**', '**41.7%–56.8%**', '**90%** win chance', 'a 91% interval']:
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                check(invalid)

    def test_booth_count_wording_remains_tied_to_its_evidence(self):
        packet = {'all_seats': ['Light'], 'entities': {'{candidate:Light:0}': {'seat':'Light'}},
                  'display_figures': [{'seat':'Light','quantity':'fp_net_change','value':'17 votes'}],
                  'evidence': [{'kind':'booths', 'seat':'Light', 'record':'Postal', 'fp_vote_change':17,
                     'before':{'fp_votes_current':{'0':107}}, 'after':{'vote_type':'Postal','fp_votes_current':{'0':80}}}]}
        check = lambda text: commentary_format.check_text(text, ['Light'], packet)
        check('**17 postal votes** were added. **27 votes** were removed from {candidate:Light:0}.')
        check('The first-preference count grew by **+17 votes**.')
        for invalid in ['**17 absent votes**', '**17 TCP votes**', '**+80 votes**',
                        '**28 votes** were removed from {candidate:Light:0}.', 'a **17** point lead']:
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                check(invalid)

    def test_reported_statewide_progress_uses_its_own_count_and_window(self):
        packet = {'statewide': {
            'before': {'countProgress': {'fp_votes': 930, 'tcp_votes': 800}},
            'after': {'countProgress': {'fp_votes': 935, 'tcp_votes': 590}}},
            'display_figures': [{'seat': 'Statewide', 'quantity': 'unrelated', 'value': '935 votes'}],
            'entities': {'{seat:Light}': {'seat': 'Light'}}, 'evidence': []}
        def check(point):
            commentary_format.check_text(point, [], packet)
        claim = 'The reported statewide two-candidate-preferred count fell by **210 votes** to **590 votes**.'
        check(claim)
        with_context = claim[:-1] + ', with reporting reset in several districts.'
        check(with_context)
        check('Statewide, the primary vote tally rose by **5** votes.')
        check('Nationwide, the reported TCP count dropped by **210 votes**; the reason is unclear.')
        for invalid in (claim.replace('fell', 'rose'), claim.replace('two-candidate-preferred', 'FP'),
                        claim.replace('210 votes', '211 votes'), claim.replace('590 votes', '935 votes'),
                        claim.replace('statewide', '{seat:Light}'), claim.replace('to **590 votes**.', 'at Hall.'),
                        with_context.replace('590 votes', '935 votes'), with_context.replace('fell', 'rose')):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                check(invalid)
        del packet['statewide']['before']['countProgress']['tcp_votes']
        with self.assertRaises(ValueError):
            check(claim)

    def test_tcp_reset_breakdown_carries_the_district_unit_only_to_its_own_groups(self):
        # Only three districts retained earlier booth counts here. They overlap
        # the two reset groups, so they cannot supply or be added to their total.
        reporting = {'reset_different_candidates': {'count': 17}, 'reset_same_candidates': {'count': 5},
                     'reset_districts_with_earlier_booth_counts_in_forecast': {'count': 3},
                     'different_candidates_with_votes': {'count': 11}}
        packet = {'tcp_reporting_changes': reporting, 'evidence': [],
                  'entities': {'{seat:Light}': {'seat': 'Light'}},
                  'display_figures': [{'seat': 'Statewide', 'quantity': kind, 'value': f"{data['count']} districts"}
                                     for kind, data in reporting.items()]}
        def check(point):
            commentary_format.check_text(point, [], packet)
        today = ('Counts reset in **22 districts**: the feed names different candidates in **17**, '
                 'while a change of pairing is likely but unconfirmed in the other **5**.')
        three_hours = ('With reporting reset in **22** districts. Replacement candidates are named in **17 districts**; '
                       'in the other **5**, a change of pairing is likely but not yet confirmed.')
        check(today)
        check(three_hours)
        # The two final daily bulletins used different word orders, both
        # carrying the initial "districts" through the remainder of the list.
        subject_first = ('Counts reset in **22 districts**: **17** named different candidates, '
                         'while a pairing change is likely in the other **5**.')
        location_last = ('Counts reset in **22 districts**; replacements were named in **17**, '
                         'while a change of pairing is likely in the other **5**, where the original candidates remain listed.')
        for claim in (subject_first, location_last):
            check(claim)
            for invalid in (claim.replace('**17**', '**11**'), claim.replace('**5**', '**3**'),
                            claim.replace('**17**', '**5 districts**').replace('other **5**', 'other **17 districts**'),
                            claim.replace('**17**', '**17** booths'), claim + ' Turnout rose by **17**.'):
                with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                    check(invalid)
        for invalid in (today.replace('22 districts', '3 districts'), today.replace('**17**', '**11**'),
                        three_hours.replace('17 districts', '5 districts').replace('other **5**', 'other **17**'),
                        today.replace('the other **5**', 'the other **17**'),
                        today.replace('**17**,', '**17** booths,'), today + ' Turnout rose by **5**.',
                        today.replace('Counts reset', 'First-preference counts reset'),
                        today.replace('Counts reset in **22 districts**:', 'Elsewhere,')):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                check(invalid)

    def test_retained_tcp_change_is_separate_from_published_booth_activity(self):
        from live_pipeline import tcp_reporting
        def observation(votes):
            return {'observed_counts_available': True, 'booths': {'A/Hall': {
                'seat_name': 'A', 'name': 'Hall', 'fp_votes_current': {'0': 60},
                'fp_votes_projected': {'0': 600, '1': 400}, 'tcp_votes_current': votes}},
                'seats': {'A': {'count_progress': {'fp_completion': .98}}}}
        old, new = observation({'0': 61, '1': 39}), observation({})
        zero = tcp_reporting.zero_districts({'published_district_tcp': {'A': {'candidates': {'x': 0, 'y': 0}}}})
        before, after, changes = tcp_reporting.reporting_views(old, new, zero, zero)
        self.assertEqual(activity.count_changes(before, after)['changed_votes'], 0)
        self.assertEqual(changes['A'][0]['before'], {'0': 61, '1': 39})
        self.assertEqual(old['booths']['A/Hall']['tcp_votes_current'], changes['A'][0]['before'])
        seat = {'pairing_percent': {'0/1': 92}, 'tcp_percentiles': {'0/1': {'50': 50.2}}}
        def context():
            return tcp_reporting.close_retained_contests(changes, {'seats': {'A': seat}}, new)
        self.assertEqual(context()['A']['units'], {'Hall': 'fewer earlier votes usable'})
        seat['pairing_percent'] = {'0/4': 95, '0/1': 5}
        self.assertEqual(context(), {})
        seat['pairing_percent'] = {'0/1': 90}
        new['seats']['A']['count_progress']['fp_completion'] = .7
        self.assertEqual(context(), {})
        for first, last in [(set(), zero), (zero, set())]:
            before, after, changes = tcp_reporting.reporting_views(old, new, first, last)
            self.assertEqual(activity.count_changes(before, after)['changed_votes'], 100)
            self.assertEqual(changes, {})

    def test_lost_retained_booths_explain_a_material_change_for_the_same_opponents(self):
        def booth(name, counts, first=45):
            return {'seat_name': 'Finniss', 'name': name, 'vote_type': 'Ordinary',
                    'count_to_projection_ids': {'21000': '2'}, 'tcp_votes_current': counts,
                    'fp_votes_current': {'0': 20, '21000': 30}, 'fp_votes_projected': {'0': 40, '2': 60},
                    'tcp_votes_projected': {'0': first, '2': 100-first}}

        before, after = [comparisons.summary(numerical()) for _ in range(2)]
        for snapshot, median in ((before, 45.5), (after, 46.7)):
            snapshot['published_district_tcp'] = {'Finniss': {'candidates': {'x': 0, 'y': 0}, 'positive_units': 0}}
            snapshot['seats']['Finniss']['tcp_percentiles']['0/2']['50'] = median
        old = {'observed_counts_available': True, 'booths': {'A': booth('Hall', {'0': 40, '21000': 60}),
                                                            'B': booth('School', {'0': 48, '21000': 52})},
               'local_tcp_preference_flows': {'Finniss': {'focus': '21000', 'raw_pair': ['0','21000'], 'share': 65}}}
        new = deepcopy(old)
        new['booths']['A'] = booth('Hall', {}, 47)
        new['booths']['A']['fp_votes_current']['0'] += 1
        new['local_tcp_preference_flows']['Finniss']['share'] = 62
        def compare():
            return comparisons.compare(after, before, [], new, old)
        group = compare()
        packet = comparisons.prompt_packet(after, group)
        context = packet['retained_pair_revisions']['Finniss']
        self.assertEqual(context['usable_pair_votes_before_after'], [200, 100])
        self.assertEqual(context['booths_no_longer_usable_examples'], ['Hall'])
        self.assertTrue(context['fp_revised_at_all_lost_booths'])
        self.assertEqual(context['only_remaining_booth'], 'School')
        self.assertEqual(context['estimated_preference_share_to_first_before_after'], [35, 38])
        self.assertEqual(packet['observed_count_changes_by_seat']['Finniss']['tcp_net_change'], 0)
        self.assertNotIn('Finniss', packet['close_tcp_counts'])
        del old['local_tcp_preference_flows']
        self.assertNotIn('estimated_preference_share_to_first_before_after', compare()['retained_pair_revisions']['Finniss'])
        for field, replacement in [('tcp_votes_projected', {'0': 42, '2': 58}), ('tcp_votes_projected', {'0': 47, '1': 53})]:
            with patch.dict(new['booths']['A'], {field: replacement}):
                self.assertNotIn('retained_pair_revisions', compare())
        with patch.dict(new['booths']['B']['tcp_votes_current'], {'0': 49}):
            self.assertNotIn('retained_pair_revisions', compare())
        with patch.dict(after, input_revision='changed'):
            self.assertNotIn('retained_pair_revisions', compare())

    def test_preference_source_must_reproduce_both_district_estimates(self):
        from live_pipeline import preference_context as pc
        types = {'Target': 3, 'Near': 2, 'Far': 0}
        old = {'Near': (.25, .5, 2), 'Far': (.5, -.2, 0)}
        new = {'Near': (.75, .5, 2), 'Far': (.5, -.35, 0)}
        def rows(inputs, direct):
            result = {}
            for district in types:
                offset, weight = pc.shared(district, types[district], inputs)
                row = dict(ordinary_offset=offset, other_district_weight=weight, local_offset=0)
                if district in inputs:
                    w, mean, _ = inputs[district]
                    row.update(local_matched_units=1, local_offset=w*(mean-offset))
                    if direct:
                        row.update(ordinary_district_weight=w, ordinary_district_mean_offset=mean)
                result[district] = row
            return result
        for direct in (True, False):
            first, second = rows(old, direct), rows(new, direct)
            inputs = [pc.district_inputs(r, types) for r in (first, second)]
            def influences():
                return pc.source_changes('Target', first['Target'], second['Target'], *inputs, types)
            shifts = {name: shift for shift, name in influences()}
            self.assertGreater(shifts['Near'], 0)
            self.assertLess(shifts['Far'], 0)
            self.assertGreater(shifts['Near'], second['Target']['ordinary_offset']-first['Target']['ordinary_offset'])
            def evidence(rows, extra):
                return {'preferences': {s: dict(row, seat=s, category='Ordinary', pair=[0, 4],
                    geographic_type=types[s], prior_first_votes_from_pending_fp=100,
                    adjusted_first_votes_from_pending_fp=100+extra) for s, row in rows.items()}}
            def summary(chance):
                return {'conditional_wins': {'Target': {'0/4': chance}}, 'seats': {'Target': {
                    'pairing_percent': {'0/4': 90}, 'candidates': {'0': {'name': 'One'}, '4': {'name': 'Two'}}}}}
            old_evidence, new_evidence = evidence(first, 10), evidence(second, 30)
            explain = pc.compare(summary(41), summary(46), old_evidence, new_evidence)
            self.assertEqual(explain['Target']['strongest_supporting_district'], 'Near')
            self.assertEqual(explain['Target']['strongest_offsetting_district'], 'Far')
            self.assertTrue(explain['Target']['holding_source_at_previous_evidence_reverses_direction'])
            self.assertEqual(pc.compare(summary(46), summary(41), old_evidence, new_evidence), {})
            first['Target']['ordinary_offset'] += .02
            self.assertIsNone(influences())

    def test_only_observed_hammond_corrections_reach_the_published_counts(self):
        before = comparisons.summary(numerical())
        after = comparisons.summary(numerical(identity='f00000002'))
        for endpoint in (before, after):
            endpoint['published_district_tcp'] = {'Hammond': {'candidates': {'300': 0, '301': 0}, 'positive_units': 0}}
        old = observed({'0': 100, '2': 200}, {'0': 150, '1': 149}, 'Hammond')
        new = observed({'0': 99, '2': 200}, {}, 'Hammond')
        group = comparisons.compare(after, before, [], new, old)
        packet = comparisons.prompt_packet(after, group)
        self.assertEqual(group['count_activity']['changed_votes'], 1)
        self.assertEqual(activity.decision(group), 'minor_changes')
        self.assertEqual(packet['observed_count_changes_by_seat']['Hammond'],
                         {'affected_votes': 1, 'fp_net_change': -1, 'tcp_net_change': 0})
        self.assertNotIn('Hammond', packet['close_tcp_counts'])
        self.assertIn('Hammond', packet['retained_tcp_only_published_zero_at_both_ends'])
        self.assertEqual(group['retained_tcp_changes_while_published_zero']['Hammond'][0]['before'], {'0': 150, '1': 149})

    def test_only_verified_external_district_names_may_cross_seat_sections(self):
        packet = {'entities': {'{seat:Source}': {'kind': 'seat', 'seat': 'Source'},
                               '{seat:Other}': {'kind': 'seat', 'seat': 'Other'},
                               '{candidate:Source:1}': {'kind': 'candidate', 'seat': 'Source'}},
            'district_preference_evidence': {'Target': {'strongest_supporting_district': 'Source'}},
            'display_figures': [{'seat': 'Source', 'quantity': 'win chance', 'value': '33.3%'}], 'evidence': []}
        commentary_format.check_text('Ordinary preferences in {seat:Source} helped shift the estimate.', ['Target'], packet)
        for unsupported in ('{seat:Other}', '{candidate:Source:1}', '{seat:Source} **33.3%**'):
            with self.assertRaises(ValueError):
                commentary_format.check_text(unsupported, ['Target'], packet)

    def test_tcp_reset_is_visible_when_earlier_booth_counts_still_inform_the_forecast(self):
        from live_pipeline import tcp_reporting
        from xml.etree import ElementTree as ET

        def district(name, pair, votes):
            result = ET.Element('district')
            ET.SubElement(result, 'district_name').text = name
            tcp = ET.SubElement(result, 'two_candidate_preferred')
            for candidate, count in zip(pair, votes):
                row = ET.SubElement(tcp, 'preferred_candidate')
                ET.SubElement(row, 'candidate_id').text = candidate
                ET.SubElement(row, 'district_total_ballot_papers').text = str(count)
                units = ET.SubElement(row, 'polling_places')
                for unit_id, ballots in enumerate((count // 2, count - count // 2)):
                    unit = ET.SubElement(units, 'PreferredPollingPlace')
                    ET.SubElement(unit, 'polling_place_id').text = str(unit_id)
                    ET.SubElement(unit, 'ballot_papers').text = str(ballots)
            return result

        def capture(index, votes):
            identity = f'{index:016x}'
            value = dict(numerical(f'f{index:08}', f'2026-03-21T18:{index:02}:00'), source_fingerprint=identity)
            value['report']['countProgress']['tcp_votes'] = sum(votes)
            root = ET.Element('HouseOfAssemblyDetail', xmlns='http://www.ecsa.sa.gov.au/schema/HofADetail')
            ET.SubElement(root, 'last_updated').text = value['source_time'].replace('T', ' ')
            districts = ET.SubElement(root, 'districts')
            for seat in value['report']['seatNames']:
                districts.append(district(seat, ['17', '42'], votes if seat == 'Light' else [0, 0]))
            filename = 'capture.xml'
            source = self.root/'feeds'/identity/filename
            source.parent.mkdir(parents=True)
            ET.ElementTree(root).write(source)
            atomic(self.root/'arrivals'/(identity+'.json'), {'hash': identity, 'content_hash': identity,
                'file': 'C:\\archived\\'+filename})
            return value

        first, second = capture(1, [60, 40]), capture(2, [0, 0])
        anchor = put(self.root, first)
        old_summary = comparisons.summary(first)
        del old_summary['published_district_tcp']
        atomic(anchor/'summary.json', old_summary)
        old = comparisons.comparison_summary(anchor)
        self.assertNotIn('published_district_tcp', read(anchor/'summary.json'))
        new = comparisons.summary(second, self.root)
        evidence = {'observed_counts_available': True, 'booths': {'Light/A': {
            'seat_name': 'Light', 'tcp_votes_current': {'0': 60, '1': 40}}}}
        comparison = comparisons.compare(new, old, [], evidence, evidence)
        self.assertEqual(comparison['count_activity']['changed_votes'], 0)
        self.assertEqual(activity.decision(comparison), 'request')
        packet = commentary.prepare_packet(comparisons.prompt_packet(new, comparison))
        self.assertIn('tcp_reporting_reconciliation', packet)
        self.assertEqual(packet['tcp_reporting_changes'], {
            'reset_same_candidates': {'count': 1, 'districts': ['Light']},
            'reset_districts_with_earlier_booth_counts_in_forecast': {'count': 1, 'districts': ['Light']}})
        concurrent = deepcopy(new)
        concurrent['statewide']['countProgress']['tcp_votes'] = 7
        incomplete = tcp_reporting.changes(old, concurrent, evidence, evidence)
        self.assertNotIn('earlier_booth_counts_match_statewide_drop', incomplete)
        claim = ('The reported statewide TCP count fell by **100 votes** to **0 votes** after '
                 'the count reset in **1 district**. The forecast still uses the earlier booth count.')
        for text in (claim, claim.replace('**1 district**', '**1** district')):
            commentary_format.check_text(text, [], packet)
        for text in (claim.replace('1 district', '2 districts'), claim.replace('fell', 'rose')):
            with self.assertRaises(ValueError):
                commentary_format.check_text(text, [], packet)
        self.assertIn('likely', commentary.instructions(commentary.PROMPT_VERSION).split('Explain material TCP reporting resets')[1])
        self.assertNotIn('Explain material TCP reporting resets', commentary.instructions('live-changes-26'))

        # A different candidate ID identifies a new pairing even if no votes
        # have arrived for it. A missing section or a single withdrawn booth
        # under unchanged candidates cannot support the same inference.
        def observed(before, after):
            prior = dict(old, published_district_tcp={'Light': before} if before else {})
            latest = dict(new, published_district_tcp={'Light': after} if after else {})
            return tcp_reporting.changes(prior, latest, {}, {})
        original = old['published_district_tcp']['Light']
        zero = new['published_district_tcp']['Light']
        replacement = dict(zero, candidates={'17': 0, '99': 0})
        self.assertIn('reset_different_candidates', observed(original, replacement)['districts'])
        self.assertIn('different_candidates_still_on_zero', observed(zero, replacement)['districts'])
        counted = dict(replacement, candidates={'17': 2, '99': 4}, positive_units=1)
        self.assertIn('different_candidates_with_votes', observed(zero, counted)['districts'])
        self.assertIsNone(observed(original, None))
        self.assertIsNone(observed(dict(original, positive_units=1), zero))
        self.assertIsNone(observed(original, dict(original, candidates={'42': 39, '17': 60})))
        self.assertIsNone(observed(zero, original))
        malformed = district('Light', ['17', '42'], [0, 0])
        malformed.find('.//district_total_ballot_papers').text = ''
        self.assertIsNone(tcp_reporting.district_count(malformed))
        malformed.find('.//district_total_ballot_papers').text = '2'
        self.assertIsNone(tcp_reporting.district_count(malformed))
        self.assertFalse(tcp_reporting.published_districts(self.root, dict(second, source_time=first['source_time'])))

    def test_deficit_uses_the_named_candidates_and_the_correct_snapshot(self):
        packet = {'display_figures': [{'seat': 'Light', 'quantity': 'unrelated', 'value': '23 votes'}],
                  'entities': {f'{{candidate:Light:{p}}}': {'seat': 'Light'} for p in ('0', '1', '2')},
                  'evidence': [], 'close_tcp_counts': {'Light': {'pair': ['1', '0'], 'lead_before': -23,
                                                             'lead_after': 60, 'net_by_candidate': {}}}}
        def check(text):
            commentary_format.check_text(text, ['Light'], packet)
        claim = '{candidate:Light:1} moved from **23 votes** behind to **60 votes** ahead of {candidate:Light:0}.'
        check(claim)
        check('{candidate:Light:0} went from **23 votes** ahead to **60 votes** behind {candidate:Light:1}.')
        check('{candidate:Light:1} is **60 votes** ahead of {candidate:Light:0}, with more ballots to count.')
        for invalid in (claim.replace('behind','ahead'), claim.replace('Light:1','Light:0'),
                        claim.replace('Light:0', 'Light:2'), claim.replace('23 votes', '60 votes')):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                check(invalid)

    def test_hyphenated_counted_lead_uses_named_opponent_and_both_endpoints(self):
        # A generic count of 45 votes must not validate a lead with its
        # direction, district, opponent or observation time reversed.
        packet = {'display_figures': [{'seat': 'Morphett', 'quantity': 'unrelated', 'value': '45 votes'}],
                  'entities': {f'{{candidate:Morphett:{p}}}': {'seat': 'Morphett'} for p in ('0', '1', '2')},
                  'evidence': [], 'close_tcp_counts': {'Morphett': {
                      'pair': ['0', '1'], 'lead_before': -45, 'lead_after': 81, 'net_by_candidate': {}}}}
        def check(claim):
            commentary_format.check_text(claim, ['Morphett'], packet)

        claim = ('The counted TCP lead over {candidate:Morphett:1} changed '
                 'from a **45-vote** deficit to an **81-vote** lead.')
        check(claim)
        check(claim.replace('-vote', '‑vote'))
        check('{candidate:Morphett:1} went from a **45-vote** lead to an **81-vote** deficit.')
        check('{candidate:Morphett:0} has an **81-vote** lead over {candidate:Morphett:1}.')
        check('There was a **45-vote** batch.')
        for wrong in (claim.replace('deficit', 'lead'), claim.replace('81-vote', '45-vote'),
                      claim.replace('from a', 'to a'), claim.replace('Morphett:1', 'Morphett:2'),
                      claim.replace('Morphett:1', 'Morphett:0'), claim.replace('81-vote', '82-vote'),
                      claim.replace('45-vote', '-45-vote')):
            with self.subTest(wrong=wrong), self.assertRaises(ValueError):
                check(wrong)
        del packet['close_tcp_counts']['Morphett']
        with self.assertRaisesRegex(ValueError, 'Unsupported counted lead'):
            check(claim)

    def test_candidate_booth_counts_can_share_a_unit_across_a_correction(self):
        def counts(liberal, labor, tcp):
            return {'fp_votes_current': {'10001': liberal, '0': labor},
                    'tcp_votes_current': {'10001': tcp}, 'count_to_projection_ids': {'10001': '1'}}
        hall = {'kind': 'booths', 'seat': 'Light', 'record': 'Light/Ordinary/Hall',
                'before': counts(419, 991, 419), 'after': counts(246, 901, 315),
                'fp_vote_change': -263}
        other = {'kind': 'booths', 'seat': 'Light', 'record': 'Light/Ordinary/School',
                 'before': counts(150, 40, 19), 'after': counts(246, 70, 246)}
        packet = {'entities': {f'{{candidate:Light:{p}}}': {'seat': 'Light'} for p in ('0', '1')},
                  'display_figures': [{'seat': 'Light', 'quantity': 'fp_vote_change', 'value': '-263 votes'}],
                  'evidence': [hall, other]}
        def check(text):
            commentary_format.check_text(text, ['Light'], packet)
        claim = ('At Hall, a correction reduced the first-preference count by **263 votes**; '
                 'the count for {candidate:Light:1} fell from **419** to **246 votes**.')
        check(claim)
        check(claim.replace('**246 votes**', '**246** votes'))
        short = 'At Hall, the FP count for {candidate:Light:1} fell from **419** to **246 votes**.'
        two = ('At Hall, the FP count for {candidate:Light:1} was revised from **419** to **246 votes**, '
               'and that for {candidate:Light:0} from **991** to **901 votes**.')
        check(two)
        for invalid in (two.replace('991', '992'), two.replace('901', '900'), two.replace('Light:0', 'Light:1')):
            with self.assertRaises(ValueError):
                check(invalid)
        for invalid in (short.replace('Hall', 'School'), short.replace('Light:1', 'Light:0'),
                        short.replace('FP', 'TCP'), short.replace('419', '418'), short.replace('246', '315'),
                        short.replace('fell', 'rose'), short.replace('fell from **419** to **246', 'rose from **246** to **419'),
                        'At Hall, {candidate:Light:1} had **419**.'):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, 'Unsupported numerical'):
                check(invalid)
        hall['before']['count_to_projection_ids']['10001'] = '0'
        with self.assertRaisesRegex(ValueError, 'Unsupported numerical'):
            check(short)

    def test_followup_booth_revision_uses_the_named_booth_and_current_vote_type(self):
        park = 'Morphett/Ordinary/Park Holme'
        glenelg = 'Morphett/Ordinary/Glenelg East'
        packet = {'entities': {'{seat:Morphett}': {'seat': 'Morphett'}},
                  'display_figures': [{'seat': 'Morphett', 'quantity': 'unrelated', 'value': '263 votes'}],
                  'observed_count_changes_by_seat': {'Morphett': {'fp_net_change': -203, 'tcp_net_change': 1978}},
                  'evidence': [{'kind': 'booths', 'seat': 'Morphett', 'record': park,
                                'fp_vote_change': -263, 'tcp_vote_change': 1129}],
                  'booth_changes': {'by_seat': {'Morphett': {'updated': {'shown': [
                      {'name': 'Park Holme', 'record': park}, {'name': 'Glenelg East', 'record': glenelg}]}}}}}
        def check(point):
            commentary_format.check_text(point, ['Morphett'], packet)
        claim = ('At Park Holme, the TCP share was above the earlier estimate. '
                 'The booth’s first-preference count was also revised down by **263 votes**.')
        check(claim)
        check(claim.replace('The booth’s', "This polling place's"))
        for invalid in (claim.replace('263', '203'), claim.replace('down', 'up'),
                        claim.replace('first-preference count', 'TCP count'),
                        claim.replace('Park Holme', 'Glenelg East'), claim.replace('Park Holme', 'an unnamed booth'),
                        claim.replace('Park Holme', 'Park Holme and Glenelg East'),
                        claim.replace('The booth', 'Another result also came in. The booth'),
                        claim.split('. ', 1)[1]):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, 'Unsupported numerical'):
                check(invalid)

    def test_booth_revision_with_an_included_change_for_each_candidate(self):
        def counts(first, second, third):
            return {'0': first, '1': second, '2': third}
        event = {'kind': 'booths', 'seat': 'Bragg', 'record': 'Bragg/Ordinary/Hall',
                 'fp_vote_change': -502, 'tcp_vote_change': -250,
                 'before': {'fp_votes_current': counts(550, 743, 181), 'tcp_votes_current': {'0': 500, '2': 600}},
                 'after': {'fp_votes_current': counts(300, 493, 179), 'tcp_votes_current': {'0': 500, '2': 350}}}
        school = dict(event, record='Bragg/Ordinary/School', after=dict(event['after'], fp_votes_current=counts(300, 491, 181)))
        packet = {'entities': {f'{{candidate:Bragg:{p}}}': {'seat': 'Bragg'} for p in ('0','1','2')},
                  'display_figures': [{'seat': 'Bragg', 'quantity': 'unrelated', 'value': '250 votes'}],
                  'evidence': [event, school]}
        claim = ('At Hall, the first-preference count was revised down by **502 votes**, '
                 'including **250 votes** each for {candidate:Bragg:0} and {candidate:Bragg:1}.')
        def check(text):
            commentary_format.check_text(text, ['Bragg'], packet)
        check(claim)
        check(claim.replace('was revised down by **502 votes**', 'fell by **502 votes**'))
        check(claim.replace('was revised down by **502 votes**', 'changed as **502 votes** were removed'))
        check(claim.replace('each for {candidate:Bragg:0} and {candidate:Bragg:1}',
                            'for {candidate:Bragg:0}, **250 votes** for {candidate:Bragg:1} and **2 votes** for {candidate:Bragg:2}'))
        for invalid in (claim.replace('Bragg:1', 'Bragg:2'), claim.replace('Hall', 'School'),
                        claim.replace('250 votes', '251 votes'), claim.replace('250 votes', '+250 votes'),
                        claim.replace('each for', 'for'), claim.replace('Bragg:1}.', 'Bragg:1} at School.'),
                        claim.replace('250 votes', '250 TCP votes'), claim.replace('Bragg:1', 'Bragg:0'),
                        claim.replace('Bragg:1', 'Light:1'), claim.replace('Bragg:1}', 'Bragg:1} and {candidate:Bragg:2}'),
                        claim.replace('each for {candidate:Bragg:0} and {candidate:Bragg:1}', 'for {candidate:Bragg:2}')):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                check(invalid)
        event['before'], event['after'] = event['after'], event['before']
        event['fp_vote_change'] = 502
        check(claim.replace('down by', 'up by'))

    def test_booth_addition_split_inherits_votes_after_a_colon(self):
        def tcp(first, second):
            return {'tcp_votes_current': {'4': first, '1': second}}
        booth = {'kind': 'booths', 'seat': 'MacKillop', 'record': 'MacKillop/Ordinary/Millicent',
                 'before': tcp(20, 15), 'after': tcp(353, 258), 'tcp_vote_change': 576, 'fp_vote_change': 8}
        other = dict(booth, record='MacKillop/Ordinary/School', after=tcp(263, 348))
        packet = {'entities': {f'{{candidate:MacKillop:{p}}}': {'seat': 'MacKillop'} for p in ('4', '1')},
                  'display_figures': [{'seat': 'MacKillop', 'quantity': 'tcp_vote_change', 'value': '576 votes'}],
                  'evidence': [booth, other]}
        claim = ('Millicent added **576 votes** to the two-candidate count: **333** for {candidate:MacKillop:4} '
                 'and **243** for {candidate:MacKillop:1}.')
        def check(text):
            commentary_format.check_text(text, ['MacKillop'], packet)
        check(claim)
        check(claim.replace('votes** to the two-candidate count:', 'TCP votes**:'))
        for wrong in (claim.replace('333', '353'), claim.replace('243', '242'), claim.replace('576', '575'),
                      claim.replace('MacKillop:4', 'MacKillop:1'), claim.replace('Millicent', 'School'),
                      claim.replace('two-candidate', 'first-preference'), claim.replace('added', 'removed'),
                      claim.replace('243** for {candidate:MacKillop:1}', '333** for {candidate:MacKillop:4}'),
                      claim.replace('two-candidate count:', 'two-candidate count. Elsewhere there were')):
            with self.subTest(wrong=wrong), self.assertRaises(ValueError):
                check(wrong)

    def test_vote_revisions_preserve_district_ledger_candidate_and_polarity(self):
        districts = ['Light', 'Finniss']
        packet = {'entities': {f'{{seat:{s}}}': {'seat': s} for s in districts},
                  'display_figures': [{'seat':'Light','quantity':'unrelated','value':'52 votes'}],
                  'observed_count_changes_by_seat': {'Light': {'fp_net_change': 10,'tcp_net_change': -52},
                                                     'Finniss': {'fp_net_change': 52, 'tcp_net_change': -10}},
                  'evidence': [{'kind': 'booths', 'seat': 'Light', 'record': 'Light/Ordinary/Hall',
                                'fp_vote_change': 10, 'tcp_vote_change': -52,
                                'before': {'tcp_votes_current': {'10001': 104, '1': 52},
                                           'count_to_projection_ids': {'10001':'6'}},
                                'after': {'tcp_votes_current': {'10001': 52, '1': 52},
                                          'count_to_projection_ids': {'10001':'6'}}}]}
        packet['entities'].update({f'{{candidate:Light:{p}}}': {'seat':'Light'} for p in ('1', '6')})
        def check(text):
            commentary_format.check_text(text, districts, packet)
        candidate = 'In {seat:Light}, a revision at Hall removed **52 votes** from the TCP count for {candidate:Light:6}.'
        check(candidate)
        check('In {seat:Light}, the district TCP count fell by **52 votes**.')
        check('In {seat:Light} and {seat:Finniss}, TCP counts fell by **52 votes** and **10 votes** respectively.')
        check('In {seat:Light}, the district changed by **−52 TCP votes**.')
        for invalid in (candidate.replace('Light:6','Light:1'), candidate.replace('removed','added'),
                        candidate.replace('TCP','FP'), candidate.replace('52 votes','53 votes'),
                        'In {seat:Light}, the district FP count fell by **52 votes**.',
                        'In {seat:Finniss}, the district TCP count fell by **52 votes**.',
                        'In {seat:Light}, both counts fell by **52 votes**.',
                        'In {seat:Light}, both the first-preference and TCP counts fell by **52 votes**.',
                        'In {seat:Light}, the TCP count rose by **52 votes**.',
                        'In {seat:Light}, **52 postal votes** were added.'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                check(invalid)
        packet['evidence'][0]['before']['count_to_projection_ids'] = {'10001':'1'}
        with self.assertRaises(ValueError):
            check(candidate)
        for snapshot in ('before', 'after'):
            record = packet['evidence'][0][snapshot]
            record['count_to_projection_ids'] = {'10001': '6', '10002': '6'}
            record['tcp_votes_current']['10002'] = 3
        with self.assertRaises(ValueError):
            check(candidate)

    def test_ranked_tcp_compares_only_reporting_booths_with_their_earlier_fp(self):
        old, new = comparisons.summary(numerical('f00000001')), comparisons.summary(numerical('f00000002'))
        prior = observed({'0': 60, '2': 20, '3': 20}, {'0': 0, '2': 0})
        hall = prior['booths']['Light/Hall']
        hall.update(booth_type='PPVC', vote_type='Ordinary', tcp_votes_projected={'0': 70, '2': 30})
        prior['booths']['Light/East'] = dict(hall, name='East', fp_votes_current={'0': 5, '4': 5})
        row = {'seat': 'Light', 'pair': [0, 2], 'category': 'Early',
               'fp_awaiting_tcp': 110, 'adjusted_first_votes_from_pending_fp': 83}
        prior['preferences'] = {'local': row}
        current = deepcopy(prior)
        current['booths']['Light/Hall']['tcp_votes_current'] = {'0': 75, '2': 25}
        current['preferences']['local'].update(fp_awaiting_tcp=10, adjusted_first_votes_from_pending_fp=5)

        def check():
            result = comparisons.compare(new, old, [], current, prior)
            booth = next(e for e in result['evidence'] if e['record'] == 'Light/Hall')
            return result, booth['tcp_against_previous_forecast']

        result, booth = check()
        self.assertFalse(booth['available'])
        self.assertNotIn('ranked_tcp_groups', result)
        prior['optional_preferential'] = False
        result, booth = check()
        self.assertIn('category-average', booth['expectation_kind'])
        # 18 expected transfers out of 25 across the two booths. Only Hall's
        # 20 transferable ballots contribute to the estimate being compared.
        self.assertAlmostEqual(booth['shares']['0']['expected_percent'], 74.4)
        packet = comparisons.prompt_packet(new, result)
        self.assertNotIn('ranked_tcp_groups', packet)
        self.assertTrue(any(r['quantity'] == commentary_format.APPROXIMATE_TCP_SHARE
                            for r in commentary_format.figures(packet)))
        # New exports record the earlier rates by FP source. Changes in the
        # other booth or a subsequent change in rates cannot alter Hall's benchmark.
        prior['ranked_source_rates'] = {'local': {'0': 1, '2': 0, '3': .9, '4': 0}}
        current['ranked_source_rates'] = {'local': {'0': 1, '2': 0, '3': .3, '4': 0}}
        east = current['booths']['Light/East']
        east['tcp_votes_current'] = {'0': 6, '2': 4}
        current['preferences']['local'].update(fp_awaiting_tcp=0, adjusted_first_votes_from_pending_fp=0)
        result, _ = check()
        pool, = rule_evidence.pool(rule_evidence.booth_units(result['evidence'])['Light'], '0')
        self.assertEqual((pool['units'], pool['ballots']), (2, 110))
        self.assertAlmostEqual(pool['rates']['0'][0], 100*81/110)
        self.assertAlmostEqual(pool['rates']['0'][1], 100*(81-83)/110)
        east['tcp_votes_current'] = {'0': 0, '2': 0}
        current['preferences']['local'].update(fp_awaiting_tcp=10, adjusted_first_votes_from_pending_fp=5)
        current['booths']['Light/East']['fp_votes_current'] = {'0': 4, '2': 6}
        result, booth = check()
        self.assertNotIn('ranked_tcp_groups', result)
        self.assertEqual(booth['expectation_kind'], 'ranked TCP')
        self.assertAlmostEqual(booth['shares']['0']['expected_percent'], 78)
        current['booths']['Light/Hall']['tcp_votes_current'] = {'0': 74, '2': 25}
        self.assertFalse(check()[1]['available'])
        current['booths']['Light/Hall']['tcp_votes_current'] = {'0': 75, '2': 25}
        del prior['ranked_source_rates']
        del current['booths']['Light/East'], prior['booths']['Light/East']
        prior['preferences']['local']['fp_awaiting_tcp'] = 100
        prior['preferences']['local']['adjusted_first_votes_from_pending_fp'] = 78
        current['preferences']['local'].update(fp_awaiting_tcp=0, adjusted_first_votes_from_pending_fp=0)
        result, booth = check()
        self.assertNotIn('ranked_tcp_groups', result)
        self.assertEqual(booth['expectation_kind'], 'ranked TCP')
        self.assertAlmostEqual(booth['shares']['0']['difference_pp'], -3)
        # Restoring old TCP to the simulator may change booth diagnostics
        # while the commission still shows exactly the same district count.
        unchanged = {'Light': {'candidates': {'a': 250, 'b': 300}, 'positive_units': 2}}
        old['published_district_tcp'] = new['published_district_tcp'] = unchanged
        self.assertFalse(check()[1]['available'])
        del old['published_district_tcp'], new['published_district_tcp']
        new['input_revision'] = 'changed'
        self.assertFalse(check()[1]['available'])
        new['input_revision'] = old['input_revision']
        current['booths']['Light/Hall']['tcp_votes_current'] = {'0': 75, '1': 25}
        self.assertFalse(check()[1]['available'])
        current['booths']['Light/Hall']['tcp_votes_current'] = {'0': 75, '2': 25}
        prior.pop('preferences')
        # Later learned flows cannot supply a ranked estimate at the earlier date.
        result, booth = check()
        self.assertNotIn('ranked_tcp_groups', result)
        self.assertFalse(booth['available'])

    def test_ranked_early_tcp_addition_uses_only_the_new_ballots(self):
        old = comparisons.summary(numerical('earlier', chance=58.15))
        new = comparisons.summary(numerical('later', chance=80.31))
        for summary, levels in ((old, [48.39, 50.25, 52.11]), (new, [49.35, 50.67, 51.95])):
            summary['seats']['Light']['tcp_percentiles']['0/2'] = dict(zip(('5','50','95'), levels))
        before = observed({'0':7703, '2':4896, '1':5363}, {'0':1021, '2':1171})
        unit = 'Light/Hall'
        before['booths'][unit].update(name='Early', vote_type='Early')
        before['optional_preferential'] = False
        before['booth_tcp_bounds'] = {unit:[{'0':7868, '2':5384}]}
        row = {'seat':'Light', 'category':'Early', 'pair':[0,2],
               'fp_awaiting_tcp':4710, 'adjusted_first_votes_from_pending_fp':1418.7576}
        before['preferences'] = {'early':row}
        after = deepcopy(before)
        after['booth_tcp_bounds'] = {}
        after['booths'][unit]['tcp_votes_current'] = {'0':9482, '2':8472}
        after['preferences']['early'].update(fp_awaiting_tcp=8, adjusted_first_votes_from_pending_fp=4)

        def report(latest):
            group = comparisons.compare(new, old, [], latest, before)
            packet = comparisons.prompt_packet(new, group)
            self.assertNotIn('rule_ranked_tcp_addition', str(packet['evidence']))
            ctx = rule_commentary.comparison_context(group, packet)
            sections = rule_commentary.write(packet, (old, new), ctx, 'previous')[1]
            return next(s['text'] for s in sections if s['seats'] == ['Light'])

        text = report(after)
        self.assertIn('from **2,192** to **17,954**. Of the **15,762** additional TCP votes', text)
        self.assertIn('received **53.7%** against', text)
        self.assertIn('**1.3 pp** above the earlier estimate', text)
        self.assertIn('range of likely results narrowed', text)
        # The later preference estimate is immaterial. A larger unfinished
        # batch, changing FP voters or incompatible bounds prevent identification.
        after['preferences']['early']['adjusted_first_votes_from_pending_fp'] = 0
        self.assertEqual(report(after), text)
        for case in ('unfinished', 'primary_revision', 'new_bounds', 'earlier_account_mismatch'):
            with self.subTest(case=case):
                changed = deepcopy(after)
                if case == 'unfinished':
                    changed['booths'][unit]['tcp_votes_current']['2'] -= 100
                    changed['preferences']['early']['fp_awaiting_tcp'] += 100
                elif case == 'primary_revision':
                    changed['booths'][unit]['fp_votes_current'].update({'0':7702, '2':4897})
                elif case == 'new_bounds':
                    changed['booth_tcp_bounds'] = before['booth_tcp_bounds']
                else:
                    before['preferences']['early']['fp_awaiting_tcp'] -= 1
                fallback = report(changed)
                self.assertIn('in the early count', fallback.lower())
                self.assertIn('additional TCP votes', fallback)
                self.assertNotIn('**1.3 pp** above the earlier estimate', fallback)

    def test_pending_category_estimates_and_actual_booth_counts_are_checked_separately(self):
        old = comparisons.summary(numerical('f00000001'))
        new = comparisons.summary(numerical('f00000002'))
        earlier = observed({'0': 500, '1': 300, '2': 772})
        counted = deepcopy(earlier)
        for source in (earlier, counted):
            source['booths']['Light/Hall'].update(name='Provisional', vote_type='Provisional')
        counted['booths']['Light/Hall']['tcp_votes_current'] = {'0': 714, '2': 814}
        packet = comparisons.prompt_packet(new, comparisons.compare(new, old, [], counted, earlier))
        group = {'category': 'Provisional', 'pair': ['0', '2'], 'ballots': 1572, 'newly_counted': 1528,
                 'units': 1, 'largest_reporting_booths': ['Provisional'],
                 'first_share_before': 48.2345, 'first_share_after': 46.7660}
        packet['ranked_tcp_groups'] = {'Light': [group]}
        packet = commentary.prepare_packet(packet)
        subject, other = '{candidate:Light:0}', '{candidate:Light:2}'
        change = 'fell from **48.2%** to **46.8% (-1.5 pp)**.'
        ballots = 'provisional ballots awaiting a two-candidate count'
        subject_first = f'The estimated final share for {subject} among {ballots} {change}'
        ballots_first = f'For {ballots} at the earlier forecast, the estimated final share for {subject} against {other} {change}'
        raw = f'In the provisional count, {other} received **53.3%** of **1,528 votes** against {subject}.'
        # The 1,528 measured ballots do not have their own earlier benchmark.
        # Omitting this optional bullet should preserve the district update.
        for projected in (subject_first, ballots_first):
            response = {'key_changes': ['The forecast changed in {seat:Light}.'], 'insufficient_evidence': False,
                        'developments': [{'seat': 'Light', 'seat_changes': [projected], 'booth_evidence': [raw]}]}
            result = commentary.format_points(response, packet, link_local_headlines=True)
            self.assertEqual(result['developments'][0]['text'], 'In {seat:Light}:\n- '+projected)
        def check(text):
            commentary_format.check_text(text, ['Light'], packet, booth_points=False)
        check(f'For {ballots}, the projected share for {other} against {subject} rose from '
              '**51.8%** to **53.2% (+1.5 pp)**.')
        with self.assertRaisesRegex(ValueError, 'same-pair'):
            check(ballots_first.rstrip('.') + ', while '+raw)
        for wrong in (ballots_first.replace('provisional', 'postal'), ballots_first.replace(other, '{candidate:Light:1}'),
                      ballots_first.replace('Light:0', 'Finniss:0'), ballots_first.replace('48.2%', '48.1%'),
                      ballots_first.replace('46.8%', '46.7%'), ballots_first.replace('-1.5 pp', '-1.4 pp'),
                      ballots_first.replace('fell', 'rose')):
            with self.subTest(wrong=wrong), self.assertRaises(ValueError):
                check(wrong)
        packet['ranked_tcp_groups']['Light'].append(dict(group, pair=['0', '1']))
        with self.assertRaisesRegex(ValueError, 'unique'):
            check(subject_first)
        check(ballots_first)

    def test_tcp_comparison_normalises_to_counted_size_and_supplies_checked_figures(self):
        previous = observed({'0': 47, '1': 36, '2': 52}, {'0': 0, '1': 0})
        current = observed({'0': 47, '1': 36, '2': 52}, {'0': 83, '1': 50})
        previous['booths']['Light/Hall']['tpp_votes_projected'] = {'0': 79.778938, '1': 55.221062}
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        new['seats']['Light']['candidates']['1'] = {'party': 'LIB', 'name': 'Fictional Liberal candidate'}
        old['seats']['Light']['candidates']['1'] = new['seats']['Light']['candidates']['1']
        old['seats']['Light']['tcp_percentiles']['0/1'] = {'50':100*79.778938/135}
        group = comparisons.compare(new, old, [], current, previous)
        event = next(e for e in group['evidence'] if e['kind'] == 'booths')
        result = event['tcp_against_previous_forecast']
        self.assertEqual(result['expectation_kind'], 'district-aligned TCP')
        self.assertEqual(result['counted_votes'], 133)
        self.assertAlmostEqual(result['shares']['0']['difference_pp'], 3.31050540796436)
        self.assertAlmostEqual(result['shares']['0']['equivalent_vote_difference'], 4.4029721925926)
        packet = commentary.prepare_packet(comparisons.prompt_packet(new, group))
        row = next(r for r in packet['display_figures'] if r.get('candidate') == '{candidate:Light:0}' and 'expected_share' in r)
        self.assertEqual((row['observed_share'], row['expected_share'], row['magnitude'], row['equivalent_vote_difference']),
                         ('62.4%', '59.1%', '3.3 pp', '+4 votes'))
        reader_wording = ('At Hall, {candidate:Light:0} polled **62.4%** '
                          'of **133** two-candidate-preferred votes against '
                          '{candidate:Light:1}, **3.3 pp** above the earlier estimate.')
        commentary_format.check_text(reader_wording, ['Light'], packet)
        def check_context(text):
            commentary_format.check_booth_tcp_context(text, ['Light'], packet, booth_points=True)
        check_context(reader_wording)
        check_context(reader_wording.replace('earlier estimate', 'estimate before counting began'))
        later = dict(packet, reference_forecast_id='f00000008', statewide={'before': {'countProgress': {'fp_votes': 100}}})
        with self.assertRaisesRegex(ValueError, 'same-pair'):
            commentary_format.check_booth_tcp_context(reader_wording.replace('earlier estimate',
                'estimate before counting began'), ['Light'], later, booth_points=True)
        for bad in (reader_wording.split(', **3.3 pp**')[0], reader_wording.replace('above', 'below'),
                    reader_wording.replace('3.3 pp', '4.3 pp'), reader_wording.replace('Light:1', 'Light:2')):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, 'same-pair'):
                check_context(bad)
        other = deepcopy(packet)
        counted = next(e for e in other['evidence'] if e['kind'] == 'booths')['tcp_against_previous_forecast']
        counted['shares']['0']['difference_pp'] = .01
        matching = reader_wording.replace('**3.3 pp** above', 'matching')
        commentary_format.check_booth_tcp_context(matching, ['Light'], other, booth_points=True)
        with self.assertRaisesRegex(ValueError, 'same-pair'):
            check_context(matching)
        with self.assertRaisesRegex(ValueError, 'Unsupported numerical'):
            commentary_format.check_text(reader_wording.replace('3.3 pp', '8.3 pp'), ['Light'], packet)
        commentary.validate({'summary': 'More votes were counted in {seat:Light}.', 'insufficient_evidence': False,
            'developments': [{'seats': ['Light'], 'text': 'In {seat:Light}, {candidate:Light:0} received **62.4%** of the TCP count, **3.3 pp** above expectations.'}]}, packet)
        messages = prompt_layout.input_messages(packet, commentary.INSTRUCTIONS, enabled())
        self.assertIn('above the earlier estimate', commentary.INSTRUCTIONS)
        self.assertIn('above our earlier estimate', commentary.instructions('live-changes-12'))
        self.assertIn('expected_share', messages[1]['content'][2]['text'])

    def test_tcp_comparison_requires_same_pair_and_prior_evidence(self):
        prior = {'tcp_votes_current': {'0': 40, '1': 30},
                 'tcp_votes_projected': {'0': 60, '1': 40},
                 'tpp_votes_projected': {'0': 90, '1': 10}}
        current = {'tcp_votes_current': {'0': 83, '1': 50}}
        result = comparisons.tcp_against_previous_forecast(prior, current)
        self.assertEqual(result['expectation_kind'], 'TCP')
        self.assertEqual(result['shares']['0']['expected_percent'], 60)
        self.assertAlmostEqual(result['shares']['0']['observed_percent'], 100*83/133)
        # A different pair, a withdrawal, missing earlier projections and a
        # zero-total earlier projection must not be described as agreement.
        for old, new in [
            (prior, {'tcp_votes_current': {'0': 83, '2': 50}}),
            (prior, {'tcp_votes_current': {'0': 0, '1': 0}}),
            (None, dict(current, tcp_votes_projected={'0': 83, '1': 50})),
            ({'tcp_votes_projected': {'0': 0, '1': 0}}, current)]:
            self.assertFalse(comparisons.tcp_against_previous_forecast(old, new)['available'])
        unchanged = dict(prior, tcp_votes_projected={'0': 90, '1': 10})
        self.assertIsNone(comparisons.tcp_against_previous_forecast(prior, unchanged))
        revision = comparisons.tcp_against_previous_forecast(prior, {'tcp_votes_current': {'0': 30, '1': 40}})
        self.assertLess(revision['shares']['0']['difference_pp'], 0)
        zero_candidate = comparisons.tcp_against_previous_forecast(prior, {'tcp_votes_current': {'0': 10, '1': 0}})
        self.assertEqual(zero_candidate['shares']['0']['observed_percent'], 100)

    def test_tcp_comparison_uses_independent_alias_without_combining_candidates(self):
        prior = {'tcp_votes_projected': {'1': 55, '3': 45},
                 'count_to_projection_ids': {'100001': '3'}}
        current = {'tcp_votes_current': {'1': 60, '100001': 40},
                   'count_to_projection_ids': {'100001': '3'}}
        result = comparisons.tcp_against_previous_forecast(prior, current)
        self.assertEqual(result['pair'], ['1', '3'])
        self.assertEqual(result['shares']['3']['observed_percent'], 40)
        self.assertEqual(current['tcp_votes_current'], {'1': 60, '100001': 40})
        current['count_to_projection_ids'] = {'100001': '1'}
        self.assertFalse(comparisons.tcp_against_previous_forecast(prior, current)['available'])

    def test_declaration_transfer_preserves_district_net_and_other_additions(self):
        def ledger(absent, postal, provisional):
            result = {'observed_counts_available': True, 'booths': {}}
            for category, votes in [('Absent', absent), ('Postal', postal), ('Provisional', provisional)]:
                result['booths'][category] = {'seat_name': 'Light', 'name': category, 'vote_type': category,
                    'fp_votes_current': votes, 'tcp_votes_current': votes, 'fp_votes_projected': votes}
            return result
        moved, zero, extra = {'0': 60, '2': 50}, {}, {'0': 9, '2': 3}
        previous, current = ledger(moved, zero, zero), ledger(zero, moved, extra)
        baseline, forecast = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        group = comparisons.compare(forecast, baseline, [], current, previous)
        packet = commentary.prepare_packet(comparisons.prompt_packet(forecast, group))
        self.assertEqual(packet['category_transfers'], [
            {'seat':'Light', 'from':'Absent', 'to':'Postal', 'fp_votes':110, 'tcp_votes':110}])
        self.assertEqual(packet['observed_count_changes_by_seat']['Light']['tcp_net_change'], 12)
        self.assertEqual([e['record'] for e in packet['evidence'] if e['kind']=='booths'], ['Provisional'])
        public = group['booth_changes']['by_seat']['Light']
        self.assertEqual((public['new']['total'], public['updated']['total']), (2, 1))
        commentary_format.check_text('**110 TCP votes** moved from Absent to Postal.', ['Light'], packet)
        commentary_format.check_text('**110 first preferences** removed from absent matched those added to postal.',
                                     ['Light'], packet)
        with self.assertRaises(ValueError):
            commentary_format.check_text('**110 TCP votes** moved from Absent to Postal.', ['Hammond'], packet)
        # The total alone can coincide. A second possible destination, differing
        # candidate allocations on either ledger or a different seat disallows a match.
        for variant in ('ambiguous', 'different_tcp', 'different_seat', 'ordinary', 'same_type'):
            changed = deepcopy(current)
            booths = changed['booths']
            if variant == 'ambiguous':
                booths['Provisional'].update(fp_votes_current=moved, tcp_votes_current=moved)
            elif variant == 'different_tcp':
                booths['Postal']['tcp_votes_current'] = {'0': 59, '2': 51}
            elif variant == 'different_seat':
                booths['Postal']['seat_name'] = 'Hammond'
            else:
                booths['Postal']['vote_type'] = 'Ordinary' if variant == 'ordinary' else 'Absent'
            with self.subTest(variant=variant):
                self.assertFalse(comparisons.compare(forecast, baseline, [], changed, previous)['category_transfers'])

    def test_nearly_matching_declaration_counts_check_each_category_and_ledger(self):
        def unit(name, fp, tcp):
            return {'name': name, 'vote_type': name, 'seat_name': 'Light', 'fp_votes_current': fp,
                    'fp_votes_projected': fp, 'tcp_votes_current': tcp}
        prior = {'Provisional': unit('Provisional', {'0': 900, '2': 672}, {'0': 814, '2': 714}),
                 'Absent': unit('Absent', {}, {})}
        later = {'Provisional': unit('Provisional', {}, {}),
                 'Absent': unit('Absent', {'0': 899, '2': 671}, {'0': 814, '2': 714})}
        before, after = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        group = comparisons.compare(after, before, [], {'observed_counts_available':True, 'booths':later},
                                     {'observed_counts_available':True, 'booths':prior})
        packet = commentary.prepare_packet(comparisons.prompt_packet(after, group))
        self.assertEqual(packet['category_transfers'], [])
        fp = '**1,572 first preferences** were removed from provisional and **1,570** appeared as absent.'
        tcp = 'The **1,528** two-candidate votes removed from provisional matched those added to absent.'
        def check(text, seats=('Light',)):
            commentary_format.check_text(text, seats, packet, booth_points=True)
        check(fp+' '+tcp)
        check('**1,572 first preferences** were removed from provisional.')
        for wrong in (fp.replace('1,570', '1,572'), fp.replace('provisional', 'postal'),
                      fp.replace('absent', 'provisional'), fp.replace('first preferences', 'TCP votes'),
                      tcp.replace('two-candidate votes', 'first preferences'), tcp.replace('absent', 'postal')):
            with self.subTest(claim=wrong), self.assertRaises(ValueError):
                check(wrong)
        with self.assertRaises(ValueError):
            check(fp, ('Hammond',))
        # Larger changes elsewhere can displace one category from the prompt.
        # Both signed amounts can still be verified from the mechanical list.
        packet['evidence'] = []
        check(fp+' '+tcp)
        packet['booth_changes'] = {}
        with self.assertRaises(ValueError):
            check(fp)

    def test_close_tcp_counts_use_whole_seat_and_require_matching_opponents(self):
        def ledger(votes):
            result = observed({'0': 600, '2': 600}, votes)
            result['booths']['Light/Hall']['tcp_votes_projected'] = {'0': 600, '2': 600}
            # This unchanged booth must count towards the lead at both endpoints.
            other = dict(result['booths']['Light/Hall'], name='Town', tcp_votes_current={'0': 200, '2': 190},
                         tcp_votes_projected={'0': 200, '2': 190})
            result['booths']['Light/Town'] = other
            return result
        baseline, forecast = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        previous, current = ledger({'0': 480, '2': 500}), ledger({'0': 580, '2': 570})
        def compare(after):
            return comparisons.compare(forecast, baseline, [], after, previous)
        group = compare(current)
        account = group['close_tcp_counts']['Light']
        self.assertEqual((account['pair'], account['lead_before'], account['lead_after']), (['0','2'], -10, 20))
        self.assertEqual(account['net_by_candidate'], {'0':100, '2':70})
        self.assertEqual((account['remaining_before'], account['remaining_after']), (220, 50))
        packet = commentary.prepare_packet(comparisons.prompt_packet(forecast, group))
        commentary_format.check_text('The counted lead is **20 votes** with **50 votes** estimated to come.',
                                     ['Light'], packet)
        prompt = json.loads(prompt_layout.input_messages(packet, commentary.INSTRUCTIONS, {})[1]['content'][-1]['text'].split('\n',1)[1])
        self.assertIn('counted TCP lead for FIRST candidate (negative means behind)', prompt['figures']['Light'])
        self.assertIn('Hall', account['lead_source'])
        self.assertIn(account['lead_source'], rule_commentary.count_detail(packet, 'Light'))
        self.assertEqual(prompt['when_reporting_a_counted_lead_change_name_its_source']['Light'], account['lead_source'])
        self.assertFalse(any('fp_share_minus_previous_expectation_pp' in e for e in group['evidence']))
        mixed = deepcopy(current)
        mixed['booths']['Light/Hall']['tcp_votes_current'] = {'0': 580, '1': 570}
        self.assertFalse(compare(mixed)['close_tcp_counts'])
        incomplete = deepcopy(current)
        incomplete['booths']['Light/Town']['tcp_votes_projected'] = {}
        details = compare(incomplete)['close_tcp_counts']['Light']
        self.assertEqual(details['lead_after'], 20)
        self.assertIsNone(details['remaining_after'])
        self.assertNotIn('approx_remaining_tcp_percent_for_first', details)

    def test_counted_lead_names_the_net_sources_across_all_reporting_units(self):
        from live_pipeline.count_context import _lead_source_text
        old = {name: {'name': name, 'vote_type': 'Ordinary', 'tcp_votes_current': {'a': 100, 'b': 100}}
               for name in ('Town', 'Hill', 'Hall', 'EVM', 'Absent', 'Postal')}
        old['EVM']['vote_type'] = 'EVM'
        def sources(deltas, excluded=()):
            new = dict(old)
            for name, delta in deltas.items():
                new[name] = dict(old[name], tcp_votes_current={'a': 100+delta, 'b': 100})
            return _lead_source_text(old, new, ['a', 'b'], sum(deltas.values()), excluded)
        self.assertEqual(sources({'EVM': -88}), 'That change came entirely from the EVM count.')
        self.assertIn('mainly from Town, partly offset', sources({'Town': 50, 'Hill': -20}))
        self.assertIn('other counting also contributing', sources({'Town': 50, 'Hill': 20}))
        self.assertIn('Hall, Hill, Town and other counting', sources({'Town': 20, 'Hill': 20, 'Hall': -20, 'EVM': 2}))
        self.assertIsNone(sources({'Town': 20, 'Hill': -20}))
        self.assertNotIn('Postal', sources({'EVM': -88, 'Absent': 100, 'Postal': -100}, {'Absent', 'Postal'}))
        self.assertIsNone(sources({'Town': 25, 'Hill': 10}, {'Hill'}))

    def test_classic_tcp_remainder_and_previous_lead_reach_both_writers(self):
        from live_pipeline.count_context import _tcp_account

        hall = {'tcp_votes_current': {'0': 195, '1': 152}, 'tpp_votes_projected': {'0': 201, '1': 160}}
        pending = {'tcp_votes_current': {}, 'fp_votes_projected': {'0': 120, '1': 70},
                   'tpp_votes_projected': {'0': 100, '1': 90}}
        totals, remaining = _tcp_account([pending, hall])
        self.assertEqual((dict(totals), remaining), ({'0': 195, '1': 152}, 204))
        # Prefer a separate TCP when available and keep a different pair or
        # missing coverage at even one booth from supplying the remainder.
        direct = dict(hall, tcp_votes_projected={'0': 200, '1': 150})
        self.assertEqual(_tcp_account([direct, pending])[1], 193)
        for wrong in (dict(pending, tpp_votes_projected={'0': 100, '3': 90}),
                      dict(pending, tcp_votes_projected={'0': 100, '3': 90}),
                      dict(pending, tpp_votes_projected={})):
            self.assertIsNone(_tcp_account([hall, wrong])[1])

        account = {'pair': ['0','1'], 'lead_before': 29, 'lead_after': 43, 'remaining_after': remaining,
                   'projected_median_percent_for_first': 50.6}
        packet = {'close_tcp_counts': {'Morphett': account}}
        # With no coverage figure, or at exactly 90%, leave out both the raw
        # lead and the projected remainder. One more counted vote takes the
        # same pair over the editorial threshold.
        self.assertIsNone(rule_commentary.count_detail(packet, 'Morphett'))
        account['counted_after'] = 9 * remaining
        self.assertIsNone(rule_commentary.count_detail(packet, 'Morphett'))
        account['counted_after'] += 1
        rules = rule_commentary.count_detail(packet, 'Morphett')
        self.assertIn('from **29** to **43** votes', rules)
        self.assertIn('The forecast projects about **204** TCP votes', rules)
        self.assertIn('**50.6%**', rules)
        point = 'The counted lead for {candidate:Morphett:0} rose from **29 votes** to **43 votes**.'
        checked = commentary_format.add_remaining_to_lead('Morphett', [point], packet)
        self.assertIn('The forecast projects about **204** TCP votes', checked[0])
        own = point+' An estimated **204 votes** remain, including possible revisions.'
        self.assertEqual(commentary_format.add_remaining_to_lead('Morphett', [own], packet), [own])
        self.assertEqual(commentary_format.add_remaining_to_lead('Morphett', checked, packet), checked)
        other_pair = point.replace('Morphett:0', 'Morphett:3')
        self.assertEqual(commentary_format.add_remaining_to_lead('Morphett', [other_pair], packet), [other_pair])

    def test_election_vote_type_names_in_both_public_editions(self):
        names = VoteTypeNames('2026sa')
        ai = 'In {seat:Morphett}, the PrePoll count changed.'
        rules = rule_commentary.MARKER+'\n- In the Early Provisional count, more votes were reported.'
        report = {'text': {'summary': ai, 'developments': [{'seats': [], 'text': rules}]}}
        failed = {'status': 'unavailable', 'text': {'developments': [{'seats': [], 'text': rules}]}}
        original = {'previous': report, 'today': failed}
        public = names.reports(original)
        self.assertEqual(public['previous']['text']['summary'], 'In {seat:Morphett}, the count of Early Voting Absent Ordinary Votes changed.')
        self.assertIn('count of Early Voting Declaration Votes', public['today']['text']['developments'][0]['text'])
        self.assertNotIn('summary', public['today']['text'])
        self.assertEqual(report['text']['summary'], ai)
        self.assertEqual(names.reports(public), public)
        self.assertEqual(VoteTypeNames('2022vic').reports(original), original)
        ordinary = '{seat:Absent} and Glenelg North Early Voting Centre were absent from the feed. A provisional estimate followed.'
        self.assertEqual(names.text(ordinary), ordinary)
        self.assertEqual(names.text('TIO reported votes.'),
                         'Telephone/Interstate/Overseas Declaration Votes reported votes.')
        headline = 'The reported first-preference total for Postal fell by **2,487 votes**.'
        self.assertEqual(names.text(headline), headline.replace('Postal', 'Postal Votes'))
        self.assertEqual(names.text('The total for Postal Hill fell.'), 'The total for Postal Hill fell.')

    def test_close_count_story_uses_the_district_outlook_and_same_pair_remainder(self):
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        before = observed({'0': 120, '2': 110}, {'0': 54, '2': 59})
        after = observed({'0': 120, '2': 110}, {'0': 63, '2': 59})
        # The raw lead is four, yet the electoral forecast favours candidate
        # 0. The district median uses the reverse candidate order: 40% for
        # candidate 2 means 60% of 230 votes for 0. That leaves 75 of 108.
        for forecast in (old, new):
            forecast['seats']['Light']['win_percent'] = {'0': 100., '2': 0.}
            forecast['seats']['Light']['tcp_percentiles'] = {'2/0': {'50': 40.}}
        for obs in (before, after):
            obs['booths']['Light/Hall']['tcp_votes_projected'] = {'0': 120, '2': 110}
        def packet():
            group = comparisons.compare(new, old, [], after, before)
            return commentary.prepare_packet(comparisons.prompt_packet(new, group))
        data = packet()
        account = data['close_tcp_counts']['Light']
        self.assertAlmostEqual(account['approx_remaining_tcp_percent_for_first'], 100*75/108)
        self.assertTrue(account['count_and_projection_diverge'])
        token, opponent = '{candidate:Light:0}', '{candidate:Light:2}'
        response = {'key_changes': ['In {seat:Light}, '+token+' moved ahead in the count.'],
                    'developments': [{'seat': 'Light', 'seat_changes': [
                        'In the two-candidate count, '+token+' went from **5 votes** behind to **4 votes** ahead of '+opponent+'.'],
                        'booth_evidence': []}], 'insufficient_evidence': False}
        def render():
            return commentary.format_points(response, data, contextualise_counts=True)
        result = render()
        self.assertIn('overwhelmingly favours', result['summary'])
        self.assertIn('**69.4%** of the remaining', result['developments'][0]['text'])
        self.assertIn('The forecast projects about **108** TCP votes still to come.', result['developments'][0]['text'])
        # Keep an author's own outcome and remaining-vote explanation once.
        response['developments'][0]['seat_changes'].append(token+' is favoured to win. The remaining votes favour '+token+'.')
        self.assertNotIn('69.4%', render()['developments'][0]['text'])
        response['developments'][0]['seat_changes'][-1] = (
            token+' is favoured to win and projected to receive about **69.4%** of the remaining two-candidate vote.')
        self.assertEqual(render()['developments'][0]['text'].count('69.4%'), 1)
        response['key_changes'] = ['In {seat:Light}, '+opponent+' is the favourite to finish second on the current count.']
        self.assertIn('overwhelmingly favours', render()['summary'])
        response['developments'][0]['seat_changes'] = []
        response['developments'][0]['booth_evidence'] = ['Hall reported more first preferences.']
        fp_text = render()['developments'][0]['text']
        self.assertIn('overwhelmingly favours', fp_text)
        self.assertNotIn('remaining', fp_text)
        self.assertNotIn('still to come', fp_text)
        self.assertNotIn('overwhelmingly favours', commentary.format_points(response, data)['summary'])
        for bands, remaining in [({'2/0': {'50': 40}}, 0), ({'2/0': {'50': 1}}, 2), ({'0/1': {'50': 60}}, 108)]:
            new['seats']['Light']['tcp_percentiles'] = bands
            after['booths']['Light/Hall']['tcp_votes_projected'] = {'0': 63+remaining, '2': 59}
            self.assertNotIn('approx_remaining_tcp_percent_for_first', packet()['close_tcp_counts']['Light'])

    def test_refresh_evidence_preserves_archived_window_and_requests(self):
        baseline = comparisons.summary(numerical('f00000001', chance=60))
        forecast = comparisons.summary(numerical('f00000003', chance=40))
        middle = comparisons.summary(numerical('f00000002', chance=80))
        previous, current = observed({'0':80}), observed({'0':100})
        group = comparisons.compare(forecast, baseline, [middle], current, previous)
        group['actual_seconds'] = 3600
        original = comparisons.prompt_packet(forecast, group)
        original.pop('category_transfers')
        original.pop('close_tcp_counts')
        packages = self.root/'packages'
        for summary, evidence in [(baseline, previous), (forecast, current)]:
            folder = packages/summary['forecast_id']
            atomic(folder/'summary.json', summary)
            atomic(folder/'evidence.json.gz', evidence)
        atomic(folder/'comparisons.json', {'1h': group})
        atomic(folder/'requests.json', {'1h': {'packet': original}})
        stored = (folder/'requests.json').read_bytes()
        [(key, refreshed)] = benchmark.select_packets(self.root, refresh_evidence=True)
        self.assertEqual(key, fingerprint(['f00000001','f00000003']))
        self.assertIn('category_transfers', refreshed)
        self.assertEqual(refreshed['actual_seconds'], 3600)
        self.assertEqual(refreshed['win_changes'], original['win_changes'])
        self.assertTrue(any(r['maximum_within_window'] == 80 for r in refreshed['win_changes']))
        self.assertEqual((folder/'requests.json').read_bytes(), stored)

    def test_two_model_prompt_rerun_is_offline_until_started_and_uses_separate_budget(self):
        previous = comparisons.summary(numerical('origin', None))
        current = comparisons.summary(numerical())
        group = comparisons.compare(current, previous, [], {}, {})
        group['actual_seconds'] = None
        original = comparisons.prompt_packet(current, group)
        key = fingerprint([previous['forecast_id'], current['forecast_id']])
        packages = self.root / 'packages'
        for summary in (previous, current):
            folder = packages / summary['forecast_id']
            atomic(folder/'summary.json', summary)
            atomic(folder/'evidence.json.gz', {})
        atomic(folder/'comparisons.json', {'previous': group})
        baseline = self.root / 'benchmark'
        atomic(baseline/'manifest.json', {'cases': {key: fingerprint(original)}, 'prompt_version': 'earlier'})
        atomic(baseline/'evidence'/f'{key}.json', original)
        for model, directory in [('gpt-6.1-terra', baseline), ('gpt-6.2-sol', self.root/'benchmark-sol62')]:
            settings = dict(enabled(), model=model, reasoning_effort='low', service_tier='default',
                            max_output_tokens=8192, cached_input_usd_per_million=0.05)
            atomic(directory/'benchmark.json', {'results': [dict(configuration=settings, model=model, comparison=key)]})
        output = self.root / ('benchmark-' + commentary.PROMPT_VERSION)
        with patch.object(compare_sa_sol, 'load_key') as key_lookup, \
                patch.object(benchmark, 'run_benchmark') as dispatch, patch('sys.stdout', new_callable=io.StringIO):
            compare_sa_sol.revised_prompt_trial(self.root, start=False)
            key_lookup.assert_not_called()
            dispatch.assert_not_called()
            self.assertFalse(output.exists())
            compare_sa_sol.revised_prompt_trial(self.root, start=True)
            selected, models, directory = dispatch.call_args.args
            self.assertEqual(selected[0][0], key)
            self.assertEqual([benchmark.variant_identity(m) for m in models],
                             [dict(model=m, service_tier='default', reasoning_effort='low')
                              for m in ('gpt-6.1-terra', 'gpt-6.2-sol')])
            self.assertEqual(directory, output)
            self.assertEqual(dispatch.call_args.kwargs['budget_usd'], 15)

    def test_quiet_summary_is_valid_with_real_count_activity(self):
        previous, current = observed({'0': 80}), observed({'0': 100})
        baseline, forecast = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        group = comparisons.compare(forecast, baseline, [], current, previous)
        self.assertEqual(activity.decision(group), 'request')
        packet = commentary.prepare_packet(comparisons.prompt_packet(forecast, group))
        answer = {'summary': 'There were no significant changes to the forecast.', 'developments': [],
                  'insufficient_evidence': False}
        self.assertEqual(commentary.validate(answer, packet), answer)

    def test_booth_lists_new_revisions_declarations_and_size_order(self):
        before, after = observed({}), observed({})
        before['booths'], after['booths'] = {}, {}
        for i, size in enumerate([50, 400, 100, 200, 300]):
            after['booths'][f'Light/New{i}'] = {'name': f'New{i}', 'seat_name': 'Light',
                'fp_votes_current': {'0': size}, 'tcp_votes_current': {'0': size}, 'fp_votes_projected': {'0': size}}
        for name, revision in [('Postals', 120), ('Absent', 10), ('Hall', 50), ('Tiny', 9)]:
            base = {'name': name, 'seat_name': 'Light', 'vote_type': name,
                    'fp_votes_current': {'0': 200, '1': 200}, 'tcp_votes_current': {}, 'fp_votes_projected': {'0': 200, '1': 200}}
            before['booths']['Light/'+name] = base
            after['booths']['Light/'+name] = dict(base, fp_votes_current={'0': 200-revision, '1': 200+revision})
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        group = comparisons.compare(new, old, [], after, before)
        changes = group['booth_changes']['by_seat']['Light']
        self.assertEqual(changes['new']['text'], 'New1, New4, New3 and 2 others')
        self.assertEqual(changes['updated']['text'], 'Postals, Hall and 1 other')
        self.assertEqual(changes['new']['shown'][0]['affected_votes'], 400)  # Not FP+TCP=800.
        retained = group['booth_changes']['retained_events']
        self.assertEqual({r['name'] for r in retained}, {'New0','New1','New2','New3','New4','Postals','Hall'})
        self.assertNotIn('retained_events', comparisons.prompt_packet(new, group)['booth_changes'])
        self.assertEqual(changes['new']['shown'][0]['fp_net_change'], 400)
        self.assertEqual(changes['updated']['shown'][0]['fp_net_change'], 0)
        self.assertFalse(comparisons.booth_changes(group['evidence'], False)['available'])
        packet = comparisons.prompt_packet(new, group)
        html = benchmark.mechanical_review(packet)
        self.assertIn('Postals, Hall and 1 other', html)

    def test_replacement_summary_uses_every_new_pair_unit_and_only_matched_preferences(self):
        before = observed({'0': 40, '2': 30, 'raw': 30}, {'0': 40, 'raw': 60})
        after = observed({'0': 40, '2': 30, 'raw': 30}, {'2': 40, 'raw': 60})
        for source in (before, after):
            booth = source['booths']['Light/Hall']
            booth['count_to_projection_ids'] = {'raw': '3'}
            booth['vote_type'] = 'Ordinary'
            source['booths']['Light/Postal'] = dict(booth, name='Postal', vote_type='Postal')
        # This third unit retains the former pair. It supplies no ballots to
        # the new count or its denominator for transferred first preferences.
        after['booths']['Light/Park'] = before['booths']['Light/Hall']
        side = {'candidates': {p: {'name': p} for p in ('0', '2', '3')},
                'tcp_percentiles': {'2/3': {'50': 43.14819}}}
        district = {'seat': 'Light', 'before': side, 'after': side}
        def replacement():
            return comparisons.count_context.district_tcp_changes(before, after, [district])[1]
        counts = replacement()['Light']
        self.assertEqual((counts['units'], counts['pair'], counts['counts']), (2, ['3', '2'], {'3': 120, '2': 80}))
        self.assertEqual(counts['transfers_from_other_first_preferences'], {'3': 60, '2': 20})
        self.assertAlmostEqual(counts['previous_district_median_first_percent'], 56.85181)
        # The district also has a booth named Light. Its name inside a candidate
        # token must not turn the district aggregate into a single-booth quote.
        summary = comparisons.summary(numerical())
        district_packet = comparisons.prompt_packet(summary, comparisons.compare(summary, summary, [], after, before))
        district_packet['replacement_tcp_counts'] = {'Light': counts}
        district_packet['all_candidate_identities']['Light'] = side['candidates']
        district_packet['evidence'].append({'kind': 'booths', 'seat': 'Light', 'record': 'Light/Light', 'after': {'name': 'Light'}})
        district_packet = commentary.prepare_packet(district_packet)
        text = ('Across **2** reporting units, {candidate:Light:3} received **60.0%** of **200 TCP votes** '
                'against {candidate:Light:2}.')
        commentary_format.check_text(text, ['Light'], district_packet)
        commentary_format.check_booth_tcp_context(text, ['Light'], district_packet, False)
        after['booths']['Light/Postal']['fp_votes_current'] = {'0': 39, '2': 30, 'raw': 30}
        self.assertNotIn('transfers_from_other_first_preferences', replacement()['Light'])
        self.assertEqual(replacement()['Light']['matched_transfer_sample'],
                         {'units': 1, 'transfers': {'3': 30, '2': 10}, 'unit_name': 'Hall'})
        self.assertEqual(replacement()['Light']['counted_votes'], 200)
        for booth in before['booths'].values():
            booth['tcp_votes_current'] = {'0': 0, 'raw': 0}
        self.assertEqual(replacement(), {})

    def test_new_party_pair_makes_highlights_with_stable_winners(self):
        old = comparisons.summary(numerical('origin', None))
        new = comparisons.summary(numerical())
        previous, current = observed({}), observed({})
        for seat, fp, tcp in [('Light', {'0': 100, '1': 100, '2': 100}, {'0': 170, '2': 130}),
                              ('Schubert', {'0': 100, '1': 200, '2': 300}, {'0': 180, '2': 420})]:
            previous['booths'].update(observed(fp, {'0': 100, '1': 200}, seat)['booths'])
            current['booths'].update(observed(fp, tcp, seat)['booths'])
        group = comparisons.compare(new, old, [], current, previous)
        packet = comparisons.prompt_packet(new, group)
        group['district_preference_evidence'] = {'Hammond': {
            'pair': ['0', '2'], 'ordinary_preference_shift_favours': '2',
            'strongest_supporting_district': 'Schubert', 'first_candidate_conditional_win_percent': [40, 37]}}
        context = rule_commentary.comparison_context(group, packet)
        summary, _, entities = rule_commentary.write(packet, (old, new), context, 'previous')
        self.assertIn('**900** TCP votes', summary)
        self.assertIn('**50.0%** of the **300** ballots', summary)
        self.assertIn('**63.0% (+3.0 pp)**', summary)
        self.assertIn('{seat:Hammond}', entities)
        changed_inputs = dict(packet, input_revision_changed=True)
        self.assertEqual(len(rule_commentary.new_pair_headlines(group, changed_inputs, set())), 1)
        group['district_preference_evidence']['Hammond']['first_candidate_conditional_win_percent'] = [40, 39]
        self.assertEqual(len(rule_commentary.new_pair_headlines(group, packet, set())), 1)
        # One district can have an incomplete ledger. Keep its TCP votes in
        # the headline, but identify the coverage of the narrower flow sample.
        current['booths']['Light/Hall']['fp_votes_current']['1'] -= 1
        group = comparisons.compare(new, old, [], current, previous)
        partial = rule_commentary.new_pair_headlines(group, packet, set())[0]
        self.assertIn('**1 of 2** reporting units', partial)
        self.assertIn('**40.0%** of the **200** ballots', partial)
        # Two different independents sharing a display abbreviation do not
        # represent a common party flow across districts.
        old['parties']['2'] = new['parties']['2'] = 'IND'
        group = comparisons.compare(new, old, [], current, previous)
        packet = comparisons.prompt_packet(new, group)
        self.assertEqual(rule_commentary.new_pair_headlines(group, packet, set()), [])

    def test_first_nonclassic_count_can_cross_threshold_over_two_updates_in_one_seat(self):
        old = comparisons.summary(numerical('origin', None))
        new = comparisons.summary(numerical())
        def headline(before, after):
            group = comparisons.compare(new, old, [], after, before)
            return rule_commentary.new_pair_headlines(group, comparisons.prompt_packet(new, group), set())
        fp = {'0': 100, '1': 160, '2': 250}
        small = observed(fp, {'0': 170, '2': 300})
        substantial = observed(fp, {'0': 200, '2': 310})
        self.assertEqual(headline(observed({}), small), [])
        first = headline(small, substantial)[0]
        self.assertIn('In {seat:Light}, the {party:0}–{party:2} TCP count reached **510** votes', first)
        self.assertIn('**62.5%** of the **160** ballots', first)
        self.assertEqual(headline(substantial, substantial), [])
        self.assertEqual(headline(observed({}), observed(fp, {'0': 200, '1': 310})), [])

    def test_replacement_booth_expectation_requires_matching_coverage_and_weights(self):
        old = comparisons.summary(numerical('origin', None))
        new = comparisons.summary(numerical())
        new['seats']['Light']['tcp_percentiles']['0/2']['50'] = 55
        before = observed({'0': 40, '2': 30}, {'0': 60, '1': 40})
        after = observed({'0': 40, '2': 30}, {'0': 80, '2': 20})
        hall = 'Light/Hall'
        postal = 'Light/Postal'
        before['booths'][hall]['tcp_votes_projected'] = {'0': 60, '2': 40}
        before['booths'][postal] = dict(before['booths'][hall], name='Postal', tcp_votes_projected={'0': 450, '2': 450})
        after['booths'][postal] = dict(after['booths'][hall], name='Postal', tcp_votes_current={'0': 160, '2': 140})

        def request(version):
            group = comparisons.compare(new, old, [], after, before)
            packet = commentary.prepare_packet(comparisons.prompt_packet(new, group), version)
            message = commentary.prompt_messages(packet, {'prompt_version': version})[1]['content'][-1]['text']
            return json.loads(message.split('\n', 1)[1])

        # Today's ballots give Hall a quarter of the sample. Weight its earlier
        # 60% projection accordingly, even though the two projections used very
        # different turnout totals (100 and 900).
        current = request(commentary.PROMPT_VERSION)
        row = current['replacement_tcp']['by_district']['Light']
        self.assertEqual(row[3:6], ['400 votes', '60.0%', '52.5%'])
        # The other booth was previously projected against a different opponent.
        # Keep the complete measured count. Neither Hall's forecast on its own
        # nor the 51% whole-seat estimate can replace the missing booth estimate.
        before['booths'][postal]['tcp_votes_projected'] = {'0': 450, '1': 450}
        current = request(commentary.PROMPT_VERSION)
        self.assertEqual(current['replacement_tcp']['by_district']['Light'][3:6], ['400 votes', '60.0%', None])
        district = current['figures']['Light']['projected TCP share for FIRST candidate, conditional on this pairing']
        self.assertIn([['0', '2'], '50', '51.0%', '55.0%', '+4.0 pp'], district)
        self.assertEqual(request('live-changes-30')['replacement_tcp']['by_district']['Light'][5], '51.0%')

    def test_large_replacement_booths_compete_by_count_size_across_seats(self):
        seats = {s: {'candidates': {p: {} for p in ('0', '1', '2')}} for s in ('First', 'Second')}
        def event(seat, name, total, old):
            before = {'fp_votes_current': {'0': total}, 'tcp_votes_current': old, 'fp_votes_projected': {'0': total}}
            after = dict(before, tcp_votes_current={'0': total//2, '2': total-total//2})
            return dict(kind='booths', seat=seat, id=name, record=seat+'/'+name, before=before, after=after,
                        changed_votes=total, fp_vote_change=0, tcp_vote_change=total-sum(old.values()))
        events = [event('First', str(n), n, {'0': 80, '1': 4000}) for n in (1000, 950, 800, 700, 600)]
        events.extend([event('Second', 'first TCP', 900, {}), event('Second', 'trivial', 2, {})])
        selected = comparisons._selected_evidence(events[::-1], list(seats), seats, {'First'})
        self.assertEqual([e['id'] for e in selected], ['1000', 'first TCP', '950', '800', '700', '600', 'trivial'])
        self.assertEqual(events[0]['before']['fp_votes_projected'], {'0': 1000})
        first = selected[0]
        self.assertNotIn('fp_votes_current', first['after'])
        self.assertNotIn('fp_votes_projected', first['after'])
        self.assertNotIn('tcp_vote_change', first)
        self.assertEqual(first['replacement_tcp_votes'], 1000)
        self.assertEqual(comparisons._booth_count_sizes(events[0])[1], 1000)
        unchanged_tally = observed({'0': 100}, {'0': 100, '1': 0})
        different_opponent = observed({'0': 100}, {'0': 100, '2': 0})
        self.assertEqual(activity.count_changes(unchanged_tally, different_opponent)['changed_votes'], 100)
        compact = prompt_layout._prompt_event(first, compact_booth=True)
        self.assertEqual(compact['after'], {'TCP': {'0': 500, '2': 500}})
        original = {'expected_formal': 1076538.893156051, 'fp_completion': .22499999031424522,
                    'projected_tpp_votes': {'0': 445.91058}, 'observed': 1981}
        self.assertEqual(prompt_layout.display_precision(original),
                         {'expected_formal': 1076539, 'fp_completion': .225, 'projected_tpp_votes': {'0': 446}, 'observed': 1981})

    def test_first_tcp_and_withdrawal_are_booth_updates(self):
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        first = observed({'0': 100})
        tcp = observed({'0': 100}, {'0': 60, '1': 40})
        group = comparisons.compare(new, old, [], tcp, first)
        self.assertEqual(group['booth_changes']['updated']['shown'][0]['affected_votes'], 100)
        self.assertEqual(group['booth_changes']['new']['total'], 0)
        withdrawn = comparisons.compare(new, old, [], observed({}), first)
        self.assertEqual(withdrawn['booth_changes']['updated']['shown'][0]['fp_net_change'], -100)

    def test_independent_comparison_uses_explicit_alias_without_rewriting_counts(self):
        raw = numerical(); raw['diagnostic_file'] = 'fictional.json'
        raw['report']['partyAbbr'].append([6, 'IND'])
        raw['report']['partyAbbr'] = [p for p in raw['report']['partyAbbr'] if p[0] != 3]
        analysis = {'seats': [{'name': 'Light', 'live_independent_party_index': 100001}],
            'booths': [{'seat_name': 'Light', 'name': 'Hall', 'node': {
                'fp_votes_current': [{'party_index': 100001, 'value': 10}, {'party_index': 100002, 'value': 20}],
                'fp_votes_projected': [{'party_index': 6, 'value': 30}, {'party_index': 100002, 'value': 20}]}}]}
        with patch('live_pipeline.comparisons.load_json', return_value=analysis), patch('live_pipeline.comparisons.analysis_path_for_snapshot', return_value='unused'):
            evidence = comparisons.evidence(raw)
        booth = next(iter(evidence['booths'].values()))
        self.assertEqual(booth['fp_votes_current'], {'100001': 10, '100002': 20})
        self.assertEqual(booth['count_to_projection_ids'], {'100001': '6'})
        prior = deepcopy(evidence)
        next(iter(prior['booths'].values()))['fp_votes_current'] = {'100001': 0, '100002': 0}
        previous = comparisons.summary(numerical('origin', None))
        previous['seats']['Light']['fp_mean_percent'] = {'6':60,'-1':40}
        group = comparisons.compare(comparisons.summary(raw), previous, [], evidence, prior)
        event = next(e for e in group['evidence'] if e['kind'] == 'booths')
        self.assertAlmostEqual(event['fp_share_minus_previous_expectation_pp']['6'], 100*(10/30-30/50))
        self.assertIn('100002', event['fp_share_minus_previous_expectation_pp'])

    def test_blended_fp_preserves_counts_turnout_and_geography(self):
        booths = {
            'Hall': dict(fp_votes_projected={'0':60, '2':40}, fp_votes_current={'0':60,'2':40}),
            'East': dict(fp_votes_projected={'0':90,'2':100,'8':6,'9':4}),
            'West': dict(fp_votes_projected={'0':45,'2':50,'8':3,'9':2}),
            'Postal': dict(fp_votes_projected={'0':55,'2':35,'8':6,'9':4}, fp_votes_current={'0':45,'2':10})}
        opening = {'East':{'0':40,'2':40,'8':15,'9':5}, 'West':{'0':20,'2':60,'8':15,'9':5}}
        targets = {'0':45,'2':45,'-1':9,'-3':1}
        fitted = booth_fp.district_expectations(booths, targets, opening)
        self.assertEqual(fitted['Hall'], booths['Hall']['fp_votes_current'])
        for key, row in fitted.items():
            self.assertAlmostEqual(sum(row.values()), sum(booths[key]['fp_votes_projected'].values()))
            for party, ballots in booths[key].get('fp_votes_current', {}).items():
                self.assertGreaterEqual(row[party], ballots)
        for party in ('0','2'):
            self.assertAlmostEqual(sum(r.get(party,0) for r in fitted.values()), 225, delta=.01)
        # The opening booths had different party mixes even though the later
        # unblended estimates gave them identical shares. Both booth sizes
        # and the local split within grouped Others survive the district fit.
        self.assertGreater(fitted['East']['0']/200, fitted['West']['0']/100)
        self.assertAlmostEqual(fitted['East']['8']/fitted['East']['9'], 3)
        self.assertFalse(booth_fp.district_expectations(booths, {'0':10,'2':90}, opening))
        self.assertFalse(booth_fp.district_expectations(booths, {}, opening))

    def test_finalist_totals_and_rare_pairing_selection(self):
        before, after = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        before['seats']['Light']['pairing_percent'] = {'0/1': 40, '0/2': 55, '1/2': 5}
        after['seats']['Light']['pairing_percent'] = {'0/1': 30, '0/2': 60, '1/2': 10}
        self.assertEqual(comparisons.finalist_probabilities(after['seats']['Light'])['2'], 70)
        for s in (before, after):
            s['seats']['Light']['pairing_percent']['1/3'] = .001
        before['seats']['Light']['tcp_percentiles']['1/3'] = {'50': 10, '5': 5, '95': 15}
        after['seats']['Light']['tcp_percentiles']['1/3'] = {'50': 50, '5': 45, '95': 55}
        before['seats']['Light']['fp_mean_percent'] = {'0': 40}
        after['seats']['Light']['fp_mean_percent'] = {'0': 42}
        packet = comparisons.prompt_packet(after, comparisons.compare(after, before, [], {}, {}))
        changes = next(s['changes'] for s in packet['seat_changes'] if s['seat'] == 'Light')
        finalist = next(c for c in changes if c['metric'] == '/finalist_percent/2')
        self.assertEqual((finalist['before'], finalist['after']), (60, 70))
        self.assertNotIn('/tcp_percentiles/1/3/50', [c['metric'] for c in changes])

    def test_interval_selection_compares_both_bounds_for_the_same_candidates(self):
        before = {'0/2': {'5': 45, '95': 58}, '0/1': {'5': 25, '95': 40}, '0/3': {'5': 10}}
        after = {'0/2': {'5': 47, '95': 58}, '0/1': {'5': 55, '95': 70}, '0/3': {'5': 40}}
        probabilities = {'0/2': 85, '0/1': 1, '0/3': 14}
        seat = {'before': dict(tcp_percentiles=before, pairing_percent=probabilities),
                'after': dict(tcp_percentiles=after, pairing_percent=probabilities)}
        selected = comparisons._tcp_interval_rows(seat)
        self.assertEqual([(r['metric'], r['change']) for r in selected],
                         [('/tcp_percentiles/0/2/5', 2), ('/tcp_percentiles/0/2/95', 0)])
        seat['before']['candidates'] = {'2': {'name': 'First independent'}}
        seat['after']['candidates'] = {'2': {'name': 'Another independent'}}
        self.assertEqual([r['metric'] for r in comparisons._tcp_interval_rows(seat)],
                         ['/tcp_percentiles/0/1/5', '/tcp_percentiles/0/1/95'])
        del before['0/1']
        self.assertEqual([r['metric'] for r in comparisons._tcp_interval_rows(seat)], ['/tcp_percentiles/0/3/5'])
        after['0/3'] = {'95': 40}
        self.assertEqual(comparisons._tcp_interval_rows(seat), [])

    def test_story_priority_preserves_local_win_movements_in_a_busy_count(self):
        self.assertEqual([comparisons._booth_score(n) for n in (0, 25, 50, 100, 200, 800, 100000)],
                         [0, 0, 1, 2, 3, 5, 10])
        def win(seat, movement, changed=False):
            return dict(seat=seat, change_pp=movement, minimum_within_window=20,
                        maximum_within_window=20+movement, identity_changed=changed)
        rows = [win('Changed independent', 35, True), win('State swing', 15),
                win('Local A', 5), win('Local B', 4), win('Local C', 3)]
        events = [dict(seat='Booth '+str(i), changed_votes=30000) for i in range(12)]
        events += [dict(seat=name, changed_votes=100) for name in ('Local A', 'Local B', 'Local C')]
        group = dict(win_changes=rows, seat_changes=[], evidence=events)
        counted = {e['seat']: e['changed_votes'] for e in events}
        ranked = comparisons._district_evidence_order(group, counted)
        self.assertEqual(ranked[:4], ['Local A', 'Local B', 'Local C', 'State swing'])
        self.assertEqual(len(set(ranked)), 10)
        self.assertNotIn('Changed independent', ranked)
        # Even if a crowded race occupies the first 24 candidate rows, a
        # selected district keeps its largest comparable win change.
        group['win_changes'] = [dict(win('Crowded', 26-i), party_id=str(i), after=4) for i in range(25)]
        group['win_changes'].append(dict(win('Local C', 1), party_id='1', after=60))
        seats = {'Crowded': {'win_percent': {str(i): 4 for i in range(25)}}, 'Local C': {'win_percent': {'1':60,'2':40}}}
        kept = comparisons._district_win_rows(group, {'Crowded', 'Local C'}, seats)
        self.assertEqual(len(kept), 24)
        self.assertEqual(kept[-1]['seat'], 'Local C')

    def test_commentary_priority_for_close_tcp_and_runner_up_changes(self):
        def side(favourite=50, pairing=100, median=56, low=48, high=62, tail=36):
            return dict(win_percent={'0': favourite}, pairing_percent={'0/1': pairing, '0/2': 100-pairing},
                        tcp_percentiles={'0/1': {'50': median, '5': low, '95': high, '0.1': tail}})
        def score(before, after):
            return comparisons._contest_score({'before': before, 'after': after})
        self.assertEqual(score(side(), side(tail=5)), 0)
        self.assertEqual(score(side(), side(median=53)), 3)
        self.assertEqual(score(side(), side(low=52, high=58)), 2)
        self.assertEqual(score(side(pairing=5), side(pairing=5, median=51)), .25)
        self.assertEqual(score(side(median=72), side(median=68)), 0)
        self.assertEqual(score(side(median=65), side(median=35)), 8)
        self.assertEqual(score(side(pairing=30), side(pairing=70)), 8)
        self.assertAlmostEqual(score(side(95, 30), side(96, 70)), .8)
        missing = side()
        missing['pairing_percent'] = {}
        self.assertEqual(score(missing, side()), 0)
        before, after = side(), side(median=51)
        before['candidates'] = {'1': {'name': 'Former candidate'}}
        after['candidates'] = {'1': {'name': 'Current candidate'}}
        self.assertEqual(score(before, after), 0)
        # An unchanged projected lead and diminishing downside are separate
        # facts that the writer needs in order to explain rising confidence.
        rows = comparisons._tcp_interval_rows({'before': side(), 'after': side(low=52)})
        self.assertEqual([(r['metric'].split('/')[-1],r['change']) for r in rows],
                         [('5', 4), ('95', 0), ('50', 0)])
        self.assertIn(commentary.LEAD_AND_RANGE_GUIDANCE, commentary.INSTRUCTIONS)
        self.assertNotIn(commentary.LEAD_AND_RANGE_GUIDANCE, commentary.instructions('live-changes-19'))
        self.assertIn('more confident the lead would hold', commentary.instructions('live-changes-19'))
        self.assertNotIn('more confident the lead would hold', commentary.instructions('live-changes-17'))
        self.assertIn(commentary.FP_WORDING, commentary.instructions('live-changes-17'))


if __name__ == '__main__':
    unittest.main()
