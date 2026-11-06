#!/usr/bin/env python3
"""Check the real macOS CMake policy without compiling or fetching dependencies."""
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms under GNU GPL section 7 apply: see LICENSE.md.
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

POLICY = Path(__file__).resolve().parents[1] / 'Tools/cmake/MacOSBuildPolicy.cmake'
CMAKE = shutil.which(os.environ.get('CMAKE') or 'cmake')


def literal(value):
    return '[==[%s]==]' % value


@unittest.skipUnless(CMAKE, 'requires CMake')
class MacOSBuildTests(unittest.TestCase):
    def policy(self, body, succeeds=True):
        with tempfile.TemporaryDirectory(prefix='zhr-macos-policy-') as directory:
            script = Path(directory) / 'policy.cmake'
            script.write_text('cmake_minimum_required(VERSION 3.20)\n'
                              'include(%s)\n' % literal(POLICY) + '''
function(require actual expected)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "Expected '${expected}', got '${actual}'")
  endif()
endfunction()
''' + body, encoding='utf-8')
            result = subprocess.run([CMAKE, '-P', str(script)], capture_output=True, text=True, timeout=10)
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_native_families_and_suffixes_select_the_matching_scheduler(self):
        for brand, expected in (('Apple M1', 'apple-m1'), ('Apple M1 Max', 'apple-m1'),
                                ('Apple M2 Pro', 'apple-m2'), ('Apple M2 Max', 'apple-m2'),
                                ('Apple M2 Ultra', 'apple-m2'), ('Apple M3', 'apple-m3'),
                                ('Apple M3 Ultra', 'apple-m3'), ('Apple M4 Max', 'apple-m4'),
                                ('Apple M5', 'apple-m5')):
            with self.subTest(brand=brand):
                self.policy('zh_macos_tuning_candidate(auto arm64 %s FALSE actual)\n'
                            'require("${actual}" %s)\n' % (literal(brand), literal(expected)))

    def test_unknown_brands_do_not_guess_a_cpu(self):
        for brand in ('', 'Intel Core i9', 'Apple M6', 'Apple M20', 'Apple M2Max', 'Virtual Apple M2'):
            with self.subTest(brand=brand):
                self.policy('zh_macos_tuning_candidate(auto arm64 %s FALSE actual)\n'
                            'require("${actual}" generic)\n' % literal(brand))

    def test_cross_and_intel_targets_do_not_inherit_host_tuning(self):
        self.policy('''
zh_macos_tuning_candidate(auto x86_64 "Apple M2 Max" FALSE actual)
require("${actual}" generic)
zh_macos_tuning_candidate(apple-m4 x86_64 "Apple M4" FALSE actual)
require("${actual}" generic)
zh_macos_tuning_candidate(auto arm64 "Apple M4" TRUE actual)
require("${actual}" generic)
zh_macos_tuning_candidate(apple-m3 arm64 "Intel Core i9" TRUE actual)
require("${actual}" apple-m3)
zh_macos_tuning_candidate(generic arm64 "Apple M5" FALSE actual)
require("${actual}" generic)
''')

    def test_both_compilers_must_support_automatic_tuning(self):
        self.policy('''
zh_macos_checked_tuning(auto apple-m4 TRUE TRUE actual)
require("${actual}" apple-m4)
zh_macos_checked_tuning(auto apple-m4 FALSE TRUE actual)
require("${actual}" generic)
zh_macos_checked_tuning(auto apple-m4 TRUE FALSE actual)
require("${actual}" generic)
zh_macos_checked_tuning(auto apple-m4 FALSE FALSE actual)
require("${actual}" generic)
zh_macos_checked_tuning(generic generic FALSE FALSE actual)
require("${actual}" generic)
''')

    def test_explicit_unsupported_tuning_reports_a_toolchain_error(self):
        for c_supported, cxx_supported in (('FALSE', 'TRUE'), ('TRUE', 'FALSE')):
            result = self.policy('zh_macos_checked_tuning(apple-m5 apple-m5 %s %s actual)\n'
                                 % (c_supported, cxx_supported), succeeds=False)
            self.assertIn('Both C and C++ compilers', result.stderr)

    def test_invalid_modes_are_rejected(self):
        for requested in ('native', 'apple-m6', 'apple-m2;bad'):
            with self.subTest(requested=requested):
                result = self.policy('zh_macos_validate_tuning(%s)\n' % literal(requested), succeeds=False)
                self.assertIn('Invalid ZH_MACOS_CPU_TUNING', result.stderr)

    def test_ffmpeg_matches_explicit_and_implicit_cmake_architectures(self):
        for explicit, processor, expected, ffmpeg in (
                ('', 'arm64', 'arm64', 'aarch64'),
                ('', 'aarch64', 'arm64', 'aarch64'),
                ('', 'x86_64', 'x86_64', 'x86_64'),
                ('arm64', 'x86_64', 'arm64', 'aarch64'),
                ('x86_64', 'arm64', 'x86_64', 'x86_64')):
            with self.subTest(explicit=explicit, processor=processor):
                self.policy('zh_macos_target_arch(%s %s arch ffmpeg)\n'
                            'require("${arch}" %s)\nrequire("${ffmpeg}" %s)\n'
                            % tuple(literal(value) for value in (explicit, processor, expected, ffmpeg)))

    def test_multi_architecture_and_unknown_architecture_fail_clearly(self):
        result = self.policy('zh_macos_target_arch("arm64;x86_64" arm64 arch ffmpeg)\n', succeeds=False)
        self.assertIn('one architecture at a time', result.stderr)
        result = self.policy('zh_macos_target_arch("" unknown arch ffmpeg)\n', succeeds=False)
        self.assertIn('Unsupported macOS target architecture', result.stderr)
        for explicit in ('0', 'OFF'):
            result = self.policy('zh_macos_target_arch(%s arm64 arch ffmpeg)\n'
                                 % literal(explicit), succeeds=False)
            self.assertIn('Unsupported macOS target architecture', result.stderr)


if __name__ == '__main__':
    unittest.main()
