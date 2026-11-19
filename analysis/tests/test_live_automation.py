"""Unattended delivery/reporting contract checks with fictional votes and mocked APIs."""
from copy import deepcopy
from contextlib import closing
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import io
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'uploads'))
sys.path.insert(0, str(ROOT/'live_scripts'))
from live_pipeline import activity, benchmark, commentary, commentary_format, comparisons, prompt_layout, trial, reporting_days, watch
from live_pipeline.run_settings import load_run
from live_pipeline.protocol import UploadQueue, Server
from live_pipeline.storage import atomic, read, fingerprint, run_lock
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
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        atomic(self.root/'run.json', config())

    def tearDown(self):
        self.temp.cleanup()

    def origin(self):
        put(self.root, numerical('origin', None, 50))

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
        worker.tick()
        worker.close()
        worker.tick()
        self.assertEqual(len(calls), 1) # previous == since_start
        payload = read(path/'publication.json')
        self.assertEqual(payload['commentary']['previous']['status'], 'complete')
        before = (path/'publication.json').read_bytes()
        restarted = Processor(self.root, enabled(), client)
        restarted.tick(); restarted.close()
        self.assertEqual(before, (path/'publication.json').read_bytes())
        self.assertEqual(len(calls), 1)

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
        def initially_invalid(packet, timeout):
            answer, usage = fake_response(packet, timeout); calls.append(1)
            if len(calls) == 1: answer['developments'][0]['seat_changes'][0] += ' **123.4%**'
            return answer, usage
        settings = enabled(); settings['attempts'] = 2
        worker = Processor(self.root, settings, initially_invalid)
        result = worker.perform(packet, time.time()+5); worker.close()
        self.assertEqual(result['status'], 'complete')
        self.assertEqual(result['attempts'], 2)
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
            other = deepcopy(older); other['run']['run_id'] += '-rerun'
            client.submit(other, fingerprint(other))
            self.assertEqual(len(backend.latest), 2)
            self.assertEqual(len(backend.packages), 3)
            origin = read(self.root/'packages'/'origin'/'publication.json')
            receipt = client.submit(origin, fingerprint(origin))
            self.assertEqual(receipt['latest_forecast_id'], 'f00000002')
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
            with self.assertRaises((OSError, BlockingIOError)):
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
            state_reads = [call for call in ledger_reads.call_args_list
                           if call.args[0] == self.root/'state.json']
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
        for bad_point in ('- Already a bullet.', 'First line.\n- Extra line.', '', '  Untrimmed.'):
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
        self.assertEqual(booth['TCPtotal'], [[['0', '2'], '133 votes']])
        self.assertEqual(booth['TCPobserved'], [['0', '62.4%'], ['2', '37.6%']])
        self.assertNotIn('TCPshare', booth)  # No expectation for the replacement pair.
        self.assertEqual(context['']['estimated district FP count completion'], [[None, '1.3%']])
        prose = {'key_changes': ['Local votes arrived in {seat:Light}.'], 'insufficient_evidence': False,
                 'developments': [{'seat': 'Light', 'seat_changes': [], 'booth_evidence': [
                     'At Hall, {candidate:Light:0} polled **27.0%** of **318 first preferences**.',
                     '{candidate:Light:0} had **62.4%** of the **133 TCP votes** counted against {candidate:Light:2}.']}]}
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

    def test_tcp_comparison_normalises_to_counted_size_and_supplies_checked_figures(self):
        previous = observed({'0': 47, '1': 36, '2': 52}, {'0': 0, '1': 0})
        current = observed({'0': 47, '1': 36, '2': 52}, {'0': 83, '1': 50})
        previous['booths']['Light/Hall']['tpp_votes_projected'] = {'0': 79.778938, '1': 55.221062}
        old, new = comparisons.summary(numerical('origin', None)), comparisons.summary(numerical())
        new['seats']['Light']['candidates']['1'] = {'party': 'LIB', 'name': 'Fictional Liberal candidate'}
        group = comparisons.compare(new, old, [], current, previous)
        event = next(e for e in group['evidence'] if e['kind'] == 'booths')
        result = event['tcp_against_previous_forecast']
        self.assertEqual(result['expectation_kind'], 'TPP')
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
        self.assertFalse(any('fp_share_minus_previous_expectation_pp' in e for e in group['evidence']))
        mixed = deepcopy(current)
        mixed['booths']['Light/Hall']['tcp_votes_current'] = {'0': 580, '1': 570}
        self.assertFalse(compare(mixed)['close_tcp_counts'])
        incomplete = deepcopy(current)
        incomplete['booths']['Light/Town']['tcp_votes_projected'] = {}
        details = compare(incomplete)['close_tcp_counts']['Light']
        self.assertEqual(details['lead_after'], 20)
        self.assertIsNone(details['remaining_after'])

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
        group = comparisons.compare(comparisons.summary(raw), comparisons.summary(numerical('origin', None)), [], evidence, prior)
        event = next(e for e in group['evidence'] if e['kind'] == 'booths')
        self.assertAlmostEqual(event['fp_share_minus_previous_expectation_pp']['6'], 100*(10/30-30/50))
        self.assertIn('100002', event['fp_share_minus_previous_expectation_pp'])

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


if __name__ == '__main__':
    unittest.main()
