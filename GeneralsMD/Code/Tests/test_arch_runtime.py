#!/usr/bin/env python3
"""Runtime-canary supervision with ordinary Python children, never Rosetta."""
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import unittest
from unittest import mock

HELPER = Path(__file__).resolve().parent / 'arch_diff/probe_runtime.py'
SPEC = importlib.util.spec_from_file_location('arch_runtime_probe', HELPER)
runtime = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runtime
SPEC.loader.exec_module(runtime)


@unittest.skipUnless(os.name == 'posix' and hasattr(os, 'fork'), 'requires POSIX fork/exec')
class RuntimeCanaryTests(unittest.TestCase):
    def test_success_and_nonzero_exit_are_preserved(self):
        for code in (0, 7):
            with self.subTest(code=code):
                result = runtime.run_canary([sys.executable, '-c', 'raise SystemExit(%d)' % code], timeout=2)
                self.assertEqual(result.returncode, code)

    def test_missing_executable_is_a_runtime_failure(self):
        self.assertEqual(runtime.run_canary(['/no-such-arch-runtime-canary'], timeout=2).returncode, 126)

    def test_timeout_returns_without_a_blocking_wait(self):
        started = time.monotonic()
        result = runtime.run_canary([sys.executable, '-c', 'import time; time.sleep(5)'], timeout=0.1)
        self.assertIsNone(result.returncode)
        self.assertLess(time.monotonic() - started, 2)
        # Collect the ordinary sleeping test child if SIGKILL completed just
        # after the helper's last nonblocking wait. Never block here either.
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            try:
                completed, _ = os.waitpid(result.pid, os.WNOHANG)
            except ChildProcessError:
                break
            if completed:
                break
            time.sleep(0.01)

    def test_uninterruptible_child_never_causes_a_blocking_reap(self):
        with mock.patch.object(runtime.os, 'fork', return_value=12345), \
             mock.patch.object(runtime.os, 'waitpid', return_value=(0, 0)) as wait, \
             mock.patch.object(runtime.os, 'kill') as kill, \
             mock.patch.object(runtime.time, 'monotonic', side_effect=[0, 1]):
            result = runtime.run_canary(['unused'], timeout=0.1)
        self.assertIsNone(result.returncode)
        kill.assert_called_once_with(12345, signal.SIGKILL)
        self.assertTrue(wait.call_args_list)
        self.assertTrue(all(call.args == (12345, os.WNOHANG) for call in wait.call_args_list))

    def test_cli_disconnects_canary_output_and_reports_timeout(self):
        noisy = subprocess.run([sys.executable, str(HELPER), '--timeout', '2', '--', sys.executable, '-c',
                                'import os; os.write(1, b"out"); os.write(2, b"err")'],
                               capture_output=True, text=True, timeout=5)
        self.assertEqual(noisy.returncode, 0, noisy.stderr)
        self.assertEqual(noisy.stdout, '')
        self.assertEqual(noisy.stderr, '')
        timed = subprocess.run([sys.executable, str(HELPER), '--timeout', '0.1', '--', sys.executable, '-c',
                                'import time; time.sleep(5)'], capture_output=True, text=True, timeout=5)
        self.assertEqual(timed.returncode, runtime.TIMEOUT, timed.stderr)
        self.assertIn('did not respond', timed.stderr)


if __name__ == '__main__':
    unittest.main()
