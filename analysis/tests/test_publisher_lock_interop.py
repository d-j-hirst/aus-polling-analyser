"""Opt-in test for two upload managers sharing a Windows run folder through WSL.

From Windows, set PUBLISHER_LOCK_WSL_PYTHON to the Linux path of a Python with
the upload manager dependencies and select test_publisher_lock_interop in run_tests.py.
Both clients use --prepare-only and temporary runs. No server or API key is used.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]


def wsl_path(path):
    return subprocess.check_output(
        ['wsl.exe', '--exec', 'wslpath', '-u', path.as_posix()], text=True, timeout=15).strip()


@unittest.skipUnless(os.name == 'nt' and os.environ.get('PUBLISHER_LOCK_WSL_PYTHON'),
                     'requires Windows and a configured WSL publisher')
class PublisherInteropTests(unittest.TestCase):
    def test_each_host_excludes_the_other_then_releases_the_run(self):
        script = ROOT/'uploads/upload_manager.py'
        for first in ('Windows', 'WSL'):
            with self.subTest(first=first), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                (folder/'run.json').write_text(json.dumps({
                    'version': 1, 'run_id': 'interop-test', 'election': '2022vic',
                    'display_name': 'Lock test', 'visibility': 'private', 'kind': 'replay',
                    'timezone': 'Australia/Melbourne', 'election_date': '2022-11-26',
                }), encoding='utf-8')
                commands = {
                    'Windows': [sys.executable, '-B', str(script), '--watch', str(folder), '--prepare-only'],
                    'WSL': ['wsl.exe', '--exec', os.environ['PUBLISHER_LOCK_WSL_PYTHON'], '-B',
                            wsl_path(script), '--watch', wsl_path(folder), '--prepare-only'],
                }
                second = 'WSL' if first == 'Windows' else 'Windows'
                contender = commands[second] + ['--once']
                # The first status write proves the real manager owns the lock.
                # A short idle timeout also bounds its lifetime if a check fails.
                with subprocess.Popen(commands[first] + ['--idle-hours', '0.005'],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as holder:
                    deadline = time.monotonic() + 45
                    while not (folder/'reporting-status.json').exists():
                        if holder.poll() is not None:
                            self.fail(holder.communicate()[0])
                        self.assertLess(time.monotonic(), deadline, 'publisher did not start')
                        time.sleep(0.1)
                    blocked = subprocess.run(contender, capture_output=True, text=True, timeout=30)
                    self.assertNotEqual(blocked.returncode, 0, blocked.stdout)
                    self.assertIn('publisher.lock', blocked.stderr)
                    self.assertNotIn('Traceback', blocked.stderr)
                    output, _ = holder.communicate(timeout=45)
                    self.assertEqual(holder.returncode, 0, output)
                    self.assertIn('hours without a new forecast', output)
                takeover = subprocess.run(contender, capture_output=True, text=True, timeout=30)
                self.assertEqual(takeover.returncode, 0, takeover.stderr)
