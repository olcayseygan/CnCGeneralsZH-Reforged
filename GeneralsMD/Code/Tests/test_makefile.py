#!/usr/bin/env python3
"""Exercise Make/build entry points with recording stubs, never real build tools."""
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
ROOT = CODE.parents[1]
MAKE = shutil.which('make')
OVERRIDES = ('CONFIG', 'CPU_TUNING', 'JOBS', 'TEST_JOBS', 'BUILD', 'CMAKE', 'CTEST',
             'ZH_BUILD_CONFIG', 'ZH_MACOS_CPU_TUNING', 'ZH_BUILD_JOBS', 'ZH_TEST_JOBS',
             'CMAKE_BUILD_PARALLEL_LEVEL', 'CTEST_PARALLEL_LEVEL', 'ZH_TEST_ONLY',
             'ZH_GUIDED_BUILD', 'MAKEFLAGS', 'MFLAGS', 'BASH_ENV', 'ENV')
RECORDED_ENV = ('ZH_BUILD_CONFIG', 'ZH_MACOS_CPU_TUNING', 'ZH_BUILD_JOBS',
                'ZH_TEST_JOBS', 'ZH_TEST_ONLY', 'BUILD', 'CMAKE')


@unittest.skipUnless(os.name == 'posix' and MAKE, 'requires POSIX and Make')
class MakefileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/tmp')
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name).resolve()
        self.repo = self.work / 'source checkout'
        self.tools = self.repo / 'GeneralsMD/Code/Tools'
        self.tools.mkdir(parents=True)
        self.bin = self.work / 'toolchain with spaces'
        self.bin.mkdir()
        self.log = self.work / 'calls.jsonl'
        self.env = dict(os.environ)
        for name in OVERRIDES:
            self.env.pop(name, None)
        self.env.update(PATH=str(self.bin) + os.pathsep + self.env.get('PATH', ''),
                        ZH_ENTRY_LOG=str(self.log), ZH_FAKE_SYSTEM='Darwin')
        self.trace = self.work / 'record.py'
        self.trace.write_text('''import json, os, pathlib, sys, time
program = sys.argv[1]
keys = %r
record = {'program': program, 'args': sys.argv[2:],
          'env': {key: os.environ[key] for key in keys if key in os.environ}}
with open(os.environ['ZH_ENTRY_LOG'], 'a', encoding='utf-8') as stream:
    stream.write(json.dumps(record) + '\\n')
lock = pathlib.Path(os.environ['ZH_ENTRY_LOG'] + '.lock')
if os.environ.get('ZH_CHECK_OVERLAP'):
    try:
        descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        sys.exit('overlapping build workflows')
    os.close(descriptor)
    try:
        time.sleep(0.1)
    finally:
        lock.unlink()
sys.exit(int(os.environ.get('ZH_STUB_EXIT', '0')))
''' % (RECORDED_ENV,), encoding='utf-8')
        self.write_shell(self.bin / 'uname', 'printf \'%s\\n\' "$ZH_FAKE_SYSTEM"\n')
        shutil.copy2(ROOT / 'Makefile', self.repo / 'Makefile')
        self.stub(self.repo / 'build-macos.sh', 'macos')
        self.stub(self.repo / 'build-linux.sh', 'linux')
        self.stub(self.tools / 'install-posix.sh', 'install')
        # Make's check target intentionally invokes this through Python.
        (self.tools / 'check_game_assets.py').write_text(
            'import runpy, sys\nsys.argv = [%r, "check"]\nrunpy.run_path(%r, run_name="__main__")\n'
            % (str(self.trace), str(self.trace)), encoding='utf-8')

    def write_shell(self, path, body):
        path.write_text('#!/bin/sh\n' + body, encoding='utf-8')
        path.chmod(0o755)

    def stub(self, path, name):
        self.write_shell(path, 'exec %s %s %s "$@"\n' %
                         (shlex.quote(sys.executable), shlex.quote(str(self.trace)), shlex.quote(name)))

    def calls(self):
        return [json.loads(line) for line in self.log.read_text(encoding='utf-8').splitlines()] if self.log.exists() else []

    def make(self, *arguments, env=None):
        return subprocess.run([MAKE, '--no-print-directory'] + list(arguments), cwd=self.repo,
                              env=self.env if env is None else env, capture_output=True, text=True)

    def test_default_is_help_and_does_not_run_a_backend(self):
        result = self.make()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('make install', result.stdout)
        self.assertIn('test-only', result.stdout)
        self.assertEqual(self.calls(), [])

    def test_platform_and_target_dispatch_preserve_arguments(self):
        cases = (('Darwin', 'build', 'macos', ['Release']),
                 ('Linux', 'test', 'linux', ['Release', 'test']),
                 ('Darwin', 'test-only', 'macos', ['Release', 'test']),
                 ('Linux', 'install', 'install', []),
                 ('Darwin', 'setup', 'install', []),
                 ('Linux', 'check', 'check', []))
        for system, target, program, arguments in cases:
            with self.subTest(system=system, target=target):
                result = self.make(target, env=dict(self.env, ZH_FAKE_SYSTEM=system))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                record = self.calls()[-1]
                self.assertEqual((record['program'], record['args']), (program, arguments))
                self.assertEqual(record['env'].get('ZH_TEST_ONLY'), '1' if target == 'test-only' else None)

    def test_windows_guidance_does_not_run_a_posix_backend(self):
        result = self.make('build', env=dict(self.env, ZH_FAKE_SYSTEM='MSYS_NT-10.0'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('build.bat', result.stdout + result.stderr)
        self.assertEqual(self.calls(), [])

    def test_defaults_do_not_erase_build_local_paths(self):
        result = self.make('build')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        settings = self.calls()[0]['env']
        self.assertEqual(settings['ZH_BUILD_CONFIG'], 'Release')
        self.assertEqual(settings['ZH_MACOS_CPU_TUNING'], 'auto')
        self.assertEqual(settings['ZH_BUILD_JOBS'], 'auto')
        self.assertEqual(settings['ZH_TEST_JOBS'], 'auto')
        self.assertNotIn('BUILD', settings)
        self.assertNotIn('CMAKE', settings)

    def test_existing_environment_settings_are_preserved(self):
        settings = {'ZH_BUILD_CONFIG': 'Debug', 'ZH_MACOS_CPU_TUNING': 'generic',
                    'ZH_BUILD_JOBS': '2', 'ZH_TEST_JOBS': '3',
                    'BUILD': str(self.work / 'existing build'), 'CMAKE': str(self.bin / 'custom cmake')}
        result = self.make('build', env=dict(self.env, **settings))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        record = self.calls()[0]
        self.assertEqual(record['args'], ['Debug'])
        for name, value in settings.items():
            self.assertEqual(record['env'][name], value, name)

    def test_standard_parallel_environment_supplies_defaults(self):
        result = self.make('test', env=dict(self.env, CMAKE_BUILD_PARALLEL_LEVEL='2', CTEST_PARALLEL_LEVEL='3'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.calls()[0]['env']['ZH_BUILD_JOBS'], '2')
        self.assertEqual(self.calls()[0]['env']['ZH_TEST_JOBS'], '3')

    def test_explicit_make_settings_override_environment(self):
        result = self.make('test', 'CONFIG=RelWithDebInfo', 'CPU_TUNING=apple-m1', 'JOBS=4', 'TEST_JOBS=2',
                           'BUILD=' + str(self.work / 'explicit build'), 'CMAKE=' + str(self.bin / 'chosen cmake'),
                           env=dict(self.env, ZH_BUILD_CONFIG='Debug', ZH_MACOS_CPU_TUNING='generic',
                                    ZH_BUILD_JOBS='1', ZH_TEST_JOBS='1'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        record = self.calls()[0]
        self.assertEqual(record['args'], ['RelWithDebInfo', 'test'])
        self.assertEqual(record['env'], {'ZH_BUILD_CONFIG': 'RelWithDebInfo', 'ZH_MACOS_CPU_TUNING': 'apple-m1',
                                        'ZH_BUILD_JOBS': '4', 'ZH_TEST_JOBS': '2',
                                        'BUILD': str(self.work / 'explicit build'), 'CMAKE': str(self.bin / 'chosen cmake')})

    def test_backend_failure_fails_make(self):
        result = self.make('build', env=dict(self.env, ZH_STUB_EXIT='23'))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(self.calls()), 1)

    def test_parallel_make_does_not_overlap_build_workflows(self):
        result = self.make('-j8', 'build', 'test', env=dict(self.env, ZH_CHECK_OVERLAP='1'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([record['args'] for record in self.calls()], [['Release'], ['Release', 'test']])

    def prepare_real_builder(self):
        shutil.copy2(CODE / 'Tools/build-posix.sh', self.tools / 'build-posix.sh')
        shutil.copy2(CODE / 'Tools/build-jobs.sh', self.tools / 'build-jobs.sh')
        self.write_shell(self.tools / 'vendor.sh', 'ZH_STUB_EXIT=93 exec %s %s vendor "$@"\n' %
                         (shlex.quote(sys.executable), shlex.quote(str(self.trace))))
        # These satisfy only discovery if a regression falls through preflight.
        # The fake cmake/vendor still stop it before any real action is possible.
        for tool in ('cc', 'c++', 'git', 'curl', 'unzip', 'tar', 'xz', 'make', 'ninja',
                     'pkg-config', 'ld.lld', 'unifdef', 'xcode-select'):
            self.write_shell(self.bin / tool, 'exit 0\n')
        self.stub(self.bin / 'cmake', 'cmake')
        self.stub(self.bin / 'ctest', 'ctest')
        build = self.work / 'configured build'
        build.mkdir()
        (build / 'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Release\n'
                                              'ZH_MACOS_CPU_TUNING:STRING=auto\n', encoding='utf-8')
        (build / 'CTestTestfile.cmake').write_text('# Synthetic test project\n', encoding='utf-8')
        return build

    def builder(self, build, platform='macos', **settings):
        env = dict(self.env, BUILD=str(build), CMAKE=str(self.bin / 'cmake'),
                   ZH_TEST_ONLY='1', ZH_BUILD_JOBS='2', ZH_TEST_JOBS='2',
                   ZH_FAKE_SYSTEM='Linux' if platform == 'linux' else 'Darwin')
        env.update(settings)
        return subprocess.run(['/bin/bash', str(self.tools / 'build-posix.sh'), platform, 'Release', 'test'],
                              env=env, capture_output=True, text=True)

    def test_test_only_uses_existing_build_and_never_configures_or_packages(self):
        build = self.prepare_real_builder()
        for cmake in (str(self.bin / 'cmake'), 'cmake'):
            with self.subTest(cmake=cmake):
                result = self.builder(build, CMAKE=cmake)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                record = self.calls()[-1]
                self.assertEqual(record['program'], 'ctest')
                self.assertIn('--test-dir', record['args'])
                self.assertEqual(record['args'][record['args'].index('--test-dir') + 1], str(build))
                self.assertIn('--parallel', record['args'])
                self.assertEqual(record['args'][record['args'].index('--parallel') + 1], '2')
        self.assertEqual([record['program'] for record in self.calls()], ['ctest', 'ctest'])
        self.assertFalse((build / 'Zero Hour Reforged.app').exists())

    def test_test_only_linux_uses_the_same_existing_build_path(self):
        build = self.prepare_real_builder()
        result = self.builder(build, platform='linux', CMAKE='cmake')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        records = self.calls()
        self.assertEqual([record['program'] for record in records], ['ctest'])
        self.assertEqual(records[0]['args'][records[0]['args'].index('--test-dir') + 1], str(build))

    def test_automatic_test_parallelism_is_positive_and_capped(self):
        build = self.prepare_real_builder()
        result = self.builder(build, ZH_BUILD_JOBS='8', ZH_TEST_JOBS='auto')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        record = self.calls()[0]
        self.assertEqual(record['program'], 'ctest')
        jobs = int(record['args'][record['args'].index('--parallel') + 1])
        self.assertGreaterEqual(jobs, 1)
        self.assertLessEqual(jobs, 4)

    def test_test_only_can_use_ctest_recorded_in_the_cache(self):
        build = self.prepare_real_builder()
        cached = self.work / 'cached CMake tools/ctest'
        cached.parent.mkdir()
        self.stub(cached, 'cached-ctest')
        with (build / 'CMakeCache.txt').open('a', encoding='utf-8') as stream:
            stream.write('CMAKE_CTEST_COMMAND:INTERNAL=%s\n' % cached)
        result = self.builder(build, CMAKE='')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([record['program'] for record in self.calls()], ['cached-ctest'])

    def test_explicit_cmake_command_selects_its_companion_ctest_before_cache(self):
        build = self.prepare_real_builder()
        cached = self.work / 'cached CMake tools/ctest'
        cached.parent.mkdir()
        self.stub(cached, 'cached-ctest')
        with (build / 'CMakeCache.txt').open('a', encoding='utf-8') as stream:
            stream.write('CMAKE_CTEST_COMMAND:INTERNAL=%s\n' % cached)
        result = self.builder(build, CMAKE='cmake')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([record['program'] for record in self.calls()], ['ctest'])

    def test_test_only_failure_stops_the_workflow(self):
        build = self.prepare_real_builder()
        result = self.builder(build, ZH_STUB_EXIT='23')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual([record['program'] for record in self.calls()], ['ctest'])

    def test_test_only_rejects_an_unconfigured_directory(self):
        build = self.prepare_real_builder()
        (build / 'CMakeCache.txt').unlink()
        result = self.builder(build)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_invalid_job_counts_fail_before_any_backend_action(self):
        build = self.prepare_real_builder()
        for name in ('ZH_BUILD_JOBS', 'ZH_TEST_JOBS'):
            for value in ('0', '-1', 'two'):
                with self.subTest(setting=name, value=value):
                    result = self.builder(build, **{name: value})
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(self.calls(), [])


if __name__ == '__main__':
    unittest.main()
