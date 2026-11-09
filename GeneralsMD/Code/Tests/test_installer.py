#!/usr/bin/env python3
"""Installer behavior with synthetic data and no package, build or game execution."""
import contextlib
import ast
import importlib
import importlib.util
import io
import json
import os
import plistlib
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / 'Tools'
SPEC = importlib.util.spec_from_file_location('guided_installer', TOOLS / 'install.py')
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)
i18n = importlib.import_module('install_i18n')


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir='/tmp')
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name).resolve()
        self.repo = self.home / 'source checkout'
        self.repo.mkdir()
        environment = mock.patch.dict(os.environ, {'ZH_BUILD_CONFIG': 'Release', 'BUILD': '',
                                                  'ZH_TEXT_PERSONNEL': 'unchanged', 'ZH_TEXT_FACTIONS': 'unchanged'})
        environment.start(); self.addCleanup(environment.stop)
        patcher = mock.patch.object(installer, 'ROOT', self.repo)
        patcher.start(); self.addCleanup(patcher.stop)
        # Answers below are English; the catalog tests cover other languages.
        translator = mock.patch.object(installer, '_', i18n.Translator('en'))
        translator.start(); self.addCleanup(translator.stop)

    def fake_app(self, path, commit='synthetic-new'):
        (path / 'Contents/MacOS').mkdir(parents=True)
        program = path / 'Contents/MacOS/generals'
        program.write_text('#!/bin/sh\n# synthetic test fixture, never executed\n')
        program.chmod(0o755)
        info = {'CFBundleExecutable': 'generals', 'CFBundlePackageType': 'APPL',
                'CFBundleShortVersionString': '3.5.1', 'CFBundleVersion': '3.5.1',
                'ZHReforgedCommit': commit}
        with (path / 'Contents/Info.plist').open('wb') as handle:
            plistlib.dump(info, handle)
        (path / 'Contents/synthetic-marker.txt').write_text(commit)
        return path

    def signature_result(self, failed=False):
        return mock.Mock(returncode=1 if failed else 0, stderr='synthetic signature failure' if failed else '')

    def test_app_destination_is_explicit_and_defaults_to_writable_global_folder(self):
        global_apps = self.home / 'All Applications'
        for writable, answer, expected in ((True, '', global_apps), (True, '2', self.home / 'Applications'),
                                           (False, '', self.home / 'Applications')):
            with self.subTest(writable=writable, answer=answer), \
                 mock.patch.object(installer, 'SYSTEM_APPLICATIONS', global_apps), \
                 mock.patch.object(installer.Path, 'home', return_value=self.home), \
                 mock.patch.object(installer, 'writable_app_folder', side_effect=lambda folder: writable or folder != global_apps), \
                 mock.patch('builtins.input', side_effect=[answer]), \
                 contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(installer.choose_app_destination(), expected / installer.APP_NAME)
                self.assertIn(str(global_apps), output.getvalue())
                self.assertIn(str(self.home / 'Applications'), output.getvalue())
                if not writable:
                    self.assertIn('Cannot write', output.getvalue())

    def test_app_destination_retries_unwritable_global_and_allows_skip(self):
        global_apps = self.home / 'All Applications'
        with mock.patch.object(installer, 'SYSTEM_APPLICATIONS', global_apps), \
             mock.patch.object(installer.Path, 'home', return_value=self.home), \
             mock.patch.object(installer, 'writable_app_folder', side_effect=lambda folder: folder != global_apps), \
             mock.patch('builtins.input', side_effect=['invalid', '1', '0']), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertIsNone(installer.choose_app_destination())

    def test_app_install_declined_keeps_existing_and_reports_build_launch_path(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        destination = self.fake_app(self.home / 'Applications' / installer.APP_NAME, 'synthetic-old')
        with mock.patch.object(installer, 'choose_app_destination', return_value=destination), \
             mock.patch('builtins.input', side_effect=['yes', '']), \
             mock.patch.object(installer.subprocess, 'run') as command, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(installer.install_macos_app(source), source)
            command.assert_not_called()
        self.assertIn('existing app', output.getvalue())
        self.assertIn('installation skipped', output.getvalue())
        self.assertIn(str(source), output.getvalue())
        self.assertNotIn('ready and verified', output.getvalue())
        self.assertEqual((destination / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-old')

    def test_app_install_verifies_source_staging_destination_and_reports_exact_version(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        destination = self.home / 'Applications' / installer.APP_NAME
        resources = source / 'Contents/Resources'
        resources.mkdir()
        (resources / 'synthetic.txt').write_text('synthetic resource')
        (resources / 'linked.txt').symlink_to('synthetic.txt')
        with mock.patch.object(installer, 'choose_app_destination', return_value=destination), \
             mock.patch('builtins.input', side_effect=['yes', 'yes']), \
             mock.patch.object(installer.subprocess, 'run', return_value=self.signature_result()) as command, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(installer.install_macos_app(source), destination)
        self.assertEqual(command.call_count, 3)
        for call in command.call_args_list:
            self.assertEqual(call.args[0][:4], ['/usr/bin/codesign', '--verify', '--deep', '--strict'])
        self.assertEqual(Path(command.call_args_list[-1].args[0][-1]), destination)
        self.assertIn(str(destination.resolve()), output.getvalue())
        self.assertIn('Version 3.5.1, build synthetic-new', output.getvalue())
        self.assertTrue((destination / 'Contents/Resources/linked.txt').is_symlink())
        self.assertTrue(source.is_dir())
        self.assertFalse(list(destination.parent.glob('.zhr-app-install-*')))

    def test_existing_app_requires_explicit_replacement_and_can_be_updated(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        destination = self.fake_app(self.home / 'Applications' / installer.APP_NAME, 'synthetic-old')
        with mock.patch.object(installer.subprocess, 'run', return_value=self.signature_result()) as command:
            with self.assertRaises(ValueError):
                installer.install_app_bundle(source, destination)
            command.assert_not_called()
            installed, info = installer.install_app_bundle(source, destination, replace_existing=True)
        self.assertEqual(installed, destination)
        self.assertEqual(info['ZHReforgedCommit'], 'synthetic-new')
        self.assertEqual((destination / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-new')
        self.assertFalse(list(destination.parent.glob('.zhr-app-install-*')))

    def test_main_launches_the_verified_global_or_personal_installed_app(self):
        self.fake_app(self.repo / 'build-mac' / installer.APP_NAME)
        global_apps = self.home / 'All Applications'
        for choice, folder in (('1', global_apps), ('2', self.home / 'Applications')):
            with self.subTest(choice=choice), \
                 mock.patch.object(installer.sys, 'argv', ['install.py']), \
                 mock.patch.object(installer.sys.stdin, 'isatty', return_value=True), \
                 mock.patch.object(installer.platform, 'system', return_value='Darwin'), \
                 mock.patch.object(installer.Path, 'home', return_value=self.home), \
                 mock.patch.object(installer, 'SYSTEM_APPLICATIONS', global_apps), \
                 mock.patch.object(installer, 'prerequisites'), \
                 mock.patch('builtins.input', side_effect=['', '', 'yes', choice, 'yes', 'yes']), \
                 mock.patch.object(installer, 'run') as launch, \
                 mock.patch.object(installer.subprocess, 'run', return_value=self.signature_result()), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(installer.main(), 0)
                launch.assert_called_once_with(['open', str(folder / installer.APP_NAME)])

    def test_app_created_during_confirmation_or_staging_is_not_overwritten(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        for phase in ('confirmation', 'staging'):
            destination = self.home / phase / 'Applications' / installer.APP_NAME
            def signature(command, **kwargs):
                if phase == 'staging' and '.zhr-app-install-' in str(command[-1]):
                    self.fake_app(destination, 'synthetic-concurrent')
                return self.signature_result()
            def confirm(message):
                if message.startswith('Copy and verify'):
                    self.fake_app(destination, 'synthetic-concurrent')
                return True
            with self.subTest(phase=phase), \
                 mock.patch.object(installer.subprocess, 'run', side_effect=signature):
                if phase == 'confirmation':
                    with mock.patch.object(installer, 'choose_app_destination', return_value=destination), \
                         mock.patch.object(installer, 'confirm', side_effect=confirm):
                        with self.assertRaises(ValueError): installer.install_macos_app(source)
                else:
                    with self.assertRaises(FileExistsError): installer.install_app_bundle(source, destination)
            self.assertEqual((destination / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-concurrent')
            self.assertFalse(list(destination.parent.glob('.zhr-app-install-*')))

    def test_app_copy_failure_does_not_touch_the_previous_app(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        destination = self.fake_app(self.home / 'Applications' / installer.APP_NAME, 'synthetic-old')
        with mock.patch.object(installer.subprocess, 'run', return_value=self.signature_result()), \
             mock.patch.object(installer.shutil, 'copytree', side_effect=OSError('synthetic disk full')):
            with self.assertRaises(OSError):
                installer.install_app_bundle(source, destination, replace_existing=True)
        self.assertEqual((destination / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-old')
        self.assertFalse(list(destination.parent.glob('.zhr-app-install-*')))

    def test_app_verification_failures_preserve_or_restore_the_previous_app(self):
        for failure in ('staging', 'destination'):
            with self.subTest(failure=failure):
                source = self.fake_app(self.home / failure / 'build' / installer.APP_NAME)
                destination = self.fake_app(self.home / failure / 'Applications' / installer.APP_NAME, 'synthetic-old')
                def signature(command, **kwargs):
                    path = Path(command[-1])
                    failed = path == destination if failure == 'destination' else '.zhr-app-install-' in str(path)
                    return self.signature_result(failed)
                with mock.patch.object(installer.subprocess, 'run', side_effect=signature):
                    with self.assertRaises(ValueError):
                        installer.install_app_bundle(source, destination, replace_existing=True)
                self.assertEqual((destination / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-old')
                self.assertFalse(list(destination.parent.glob('.zhr-app-install-*')))

    def test_app_publish_failure_restores_previous_app_and_new_failure_leaves_no_app(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        destination = self.fake_app(self.home / 'Applications' / installer.APP_NAME, 'synthetic-old')
        real_replace = os.replace
        def fail_publish(before, after):
            if Path(before).name == installer.APP_NAME and '.zhr-app-install-' in str(before):
                raise OSError('synthetic publish failure')
            return real_replace(before, after)
        with mock.patch.object(installer.subprocess, 'run', return_value=self.signature_result()), \
             mock.patch.object(installer.os, 'replace', side_effect=fail_publish):
            with self.assertRaises(OSError):
                installer.install_app_bundle(source, destination, replace_existing=True)
        self.assertEqual((destination / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-old')
        fresh = self.home / 'Fresh Applications' / installer.APP_NAME
        with mock.patch.object(installer.subprocess, 'run', side_effect=lambda command, **kwargs: self.signature_result(Path(command[-1]) == fresh)):
            with self.assertRaises(ValueError):
                installer.install_app_bundle(source, fresh)
        self.assertFalse(fresh.exists())
        self.assertFalse(list(fresh.parent.glob('.zhr-app-install-*')))

    def test_failed_app_recovery_keeps_the_backup_and_reports_its_location(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        destination = self.fake_app(self.home / 'Applications' / installer.APP_NAME, 'synthetic-old')
        real_replace = os.replace
        def fail_recovery(before, after):
            if Path(before).name == 'previous.app':
                raise OSError('synthetic recovery failure')
            return real_replace(before, after)
        with mock.patch.object(installer.subprocess, 'run', side_effect=lambda command, **kwargs: self.signature_result(Path(command[-1]) == destination)), \
             mock.patch.object(installer.os, 'replace', side_effect=fail_recovery):
            with self.assertRaisesRegex(OSError, 'Recovery files were kept') as error:
                installer.install_app_bundle(source, destination, replace_existing=True)
        recovery = list(destination.parent.glob('.zhr-app-install-*'))
        self.assertEqual(len(recovery), 1)
        self.assertIn(str(recovery[0]), str(error.exception))
        self.assertEqual((recovery[0] / 'previous.app/Contents/synthetic-marker.txt').read_text(), 'synthetic-old')

    def test_post_effect_interruptions_of_backup_publish_and_reservation_roll_back(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        for phase, existing in (('backup', True), ('publish', True), ('publish', False),
                                ('reserve', True), ('reserve', False)):
            with self.subTest(phase=phase, existing=existing):
                destination = self.home / ('%s-%s' % (phase, existing)) / installer.APP_NAME
                if existing:
                    self.fake_app(destination, 'synthetic-old')
                real_replace, real_mkdir = os.replace, Path.mkdir
                interrupted = False
                def replace(before, after):
                    nonlocal interrupted
                    result = real_replace(before, after)
                    if not interrupted and ((phase == 'backup' and Path(after).name == 'previous.app')
                            or (phase == 'publish' and Path(before).name == installer.APP_NAME
                                and '.zhr-app-install-' in str(before))):
                        interrupted = True
                        raise KeyboardInterrupt('synthetic signal after rename committed')
                    return result
                def mkdir(path, *args, **kwargs):
                    nonlocal interrupted
                    result = real_mkdir(path, *args, **kwargs)
                    if not interrupted and phase == 'reserve' and path == destination:
                        interrupted = True
                        raise KeyboardInterrupt('synthetic signal after mkdir committed')
                    return result
                with mock.patch.object(installer.subprocess, 'run', return_value=self.signature_result()), \
                     mock.patch.object(installer.os, 'replace', side_effect=replace), \
                     mock.patch.object(installer.Path, 'mkdir', new=mkdir):
                    with self.assertRaises(OSError if phase == 'reserve' else KeyboardInterrupt):
                        installer.install_app_bundle(source, destination, replace_existing=existing)
                self.assertTrue(interrupted)
                recovery = list(destination.parent.glob('.zhr-app-install-*'))
                if phase == 'reserve':
                    # The interrupted mkdir did not return an identity. Keep its unowned
                    # destination untouched and retain the verified staging/previous app.
                    self.assertTrue(destination.is_dir())
                    self.assertEqual(list(destination.iterdir()), [])
                    self.assertEqual(len(recovery), 1)
                    self.assertTrue((recovery[0] / installer.APP_NAME).is_dir())
                    if existing:
                        self.assertEqual((recovery[0] / 'previous.app/Contents/synthetic-marker.txt').read_text(), 'synthetic-old')
                elif existing:
                    self.assertEqual((destination / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-old')
                else:
                    self.assertFalse(destination.exists())
                self.assertTrue(source.is_dir())
                if phase != 'reserve':
                    self.assertFalse(recovery)

    def test_empty_collision_after_backup_rename_is_kept_with_recovery_copy(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        destination = self.fake_app(self.home / 'Applications' / installer.APP_NAME, 'synthetic-old')
        real_replace = os.replace
        collision_identity = None
        def replace(before, after):
            nonlocal collision_identity
            result = real_replace(before, after)
            if Path(after).name == 'previous.app':
                destination.mkdir() # Another process claimed the now-free path, even if empty.
                collision_identity = destination.lstat()
            return result
        with mock.patch.object(installer.subprocess, 'run', return_value=self.signature_result()), \
             mock.patch.object(installer.os, 'replace', side_effect=replace):
            with self.assertRaisesRegex(OSError, 'Recovery files were kept'):
                installer.install_app_bundle(source, destination, replace_existing=True)
        current = destination.lstat()
        self.assertEqual((current.st_dev, current.st_ino), (collision_identity.st_dev, collision_identity.st_ino))
        self.assertEqual(list(destination.iterdir()), [])
        recovery = list(destination.parent.glob('.zhr-app-install-*'))
        self.assertEqual(len(recovery), 1)
        self.assertEqual((recovery[0] / 'previous.app/Contents/synthetic-marker.txt').read_text(), 'synthetic-old')

    def test_app_destination_links_and_unsafe_entry_points_are_refused(self):
        source = self.fake_app(self.home / 'build' / installer.APP_NAME)
        other = self.fake_app(self.home / 'other' / installer.APP_NAME, 'synthetic-other')
        link = self.home / installer.APP_NAME
        link.symlink_to(other, target_is_directory=True)
        with mock.patch.object(installer.subprocess, 'run') as command:
            with self.assertRaises(ValueError):
                installer.install_app_bundle(source, link, replace_existing=True)
            command.assert_not_called()
        self.assertEqual((other / 'Contents/synthetic-marker.txt').read_text(), 'synthetic-other')
        for index, name in enumerate(('../outside', '/bin/sh', 'nested/generals', 'nested\\generals', '')):
            invalid = self.fake_app(self.home / ('invalid-%d' % index) / installer.APP_NAME)
            plist = invalid / 'Contents/Info.plist'
            info = plistlib.loads(plist.read_bytes())
            info['CFBundleExecutable'] = name
            plist.write_bytes(plistlib.dumps(info))
            with mock.patch.object(installer.subprocess, 'run') as command:
                with self.assertRaises(ValueError): installer.verify_macos_app(invalid)
                command.assert_not_called()
        (source / 'Contents/MacOS/generals').chmod(0o644)
        with mock.patch.object(installer.subprocess, 'run') as command:
            with self.assertRaises(ValueError): installer.verify_macos_app(source)
            command.assert_not_called()

    def test_searches_steam_external_libraries_and_never_follows_descendant_links(self):
        steam = self.home / 'Library/Application Support/Steam'
        (steam / 'steamapps').mkdir(parents=True)
        library = self.home / 'External Library'
        expansion = library / 'steamapps/common/Zero Hour'
        expansion.mkdir(parents=True)
        (expansion / 'INIZH.BIG').write_bytes(b'synthetic marker')
        original = self.home / 'GOG Games/Generals'
        original.mkdir(parents=True)
        (original / 'Textures.big').write_bytes(b'synthetic marker')
        (original / 'cycle').symlink_to(self.home, target_is_directory=True)
        (steam / 'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "1" { "path" "%s" } }' % library)
        found = installer.discover(installer.search_roots(self.home))
        self.assertEqual(found['zero_hour'], [expansion])
        self.assertEqual(found['generals'], [original])

    def test_rejects_checkout_and_symlink_targets_before_creating_them(self):
        alias = self.home / 'checkout alias'
        alias.symlink_to(self.repo, target_is_directory=True)
        for destination in (self.repo, self.repo / 'retail', alias / 'retail', self.home):
            with self.assertRaises(ValueError):
                installer.external_path(str(destination))
        self.assertFalse((self.repo / 'retail').exists())

    def test_copy_preserves_full_tree_and_refuses_overwriting(self):
        source = self.home / 'Owned install with spaces'
        (source / 'Data/Movies').mkdir(parents=True)
        (source / 'INIZH.big').write_bytes(b'synthetic marker')
        (source / 'Data/Movies/synthetic.txt').write_text('synthetic content')
        target = self.home / 'Game data'
        installer.copy_game(source, target)
        self.assertEqual((target / 'Data/Movies/synthetic.txt').read_text(), 'synthetic content')
        self.assertEqual((source / 'INIZH.big').read_bytes(), b'synthetic marker')
        with self.assertRaises(ValueError):
            installer.copy_game(source, target)

    def test_copy_rejects_links_and_supports_a_separately_copied_base(self):
        source = self.home / 'Zero Hour'
        source.mkdir()
        (source / 'INIZH.big').write_bytes(b'synthetic marker')
        (source / 'ZH_Generals').mkdir()
        (source / 'ZH_Generals/Textures.big').write_bytes(b'synthetic marker')
        target = self.home / 'New layout'
        installer.copy_game(source, target, exclude=('zh_generals',))
        installer.copy_game(source / 'ZH_Generals', target / 'ZH_Generals')
        self.assertTrue((target / 'ZH_Generals/Textures.big').is_file())
        (source / 'external').symlink_to(self.repo, target_is_directory=True)
        with self.assertRaises(ValueError):
            installer.copy_game(source, self.home / 'rejected copy')

    def test_all_no_answers_execute_no_commands_and_noninteractive_is_rejected(self):
        with mock.patch.object(installer.sys, 'argv', ['install.py']), \
             mock.patch.object(installer.sys.stdin, 'isatty', return_value=True), \
             mock.patch.object(installer.platform, 'system', return_value='Linux'), \
             mock.patch('builtins.input', side_effect=['', '', '']), \
             mock.patch.object(installer.subprocess, 'run') as run, \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(installer.main(), 0)
            run.assert_not_called()
        with mock.patch.object(installer.sys, 'argv', ['install.py']), \
             mock.patch.object(installer.sys.stdin, 'isatty', return_value=False):
            with self.assertRaises(SystemExit): installer.main()

    def test_guided_copy_from_existing_installation(self):
        zero = self.home / 'owned zero hour'
        base = self.home / 'owned generals'
        zero.mkdir(); base.mkdir()
        (zero / 'INIZH.big').write_bytes(b'synthetic expansion')
        (base / 'Textures.big').write_bytes(b'synthetic original')
        (zero / 'Data').mkdir(); (zero / 'Data/keep.txt').write_text('synthetic')
        target = self.home / 'new installation'
        answers = ['', '', 'y', '', str(zero), str(base), 'y', str(target), 'y']
        with mock.patch.object(installer.sys, 'argv', ['install.py']), \
             mock.patch.object(installer.sys.stdin, 'isatty', return_value=True), \
             mock.patch.object(installer.platform, 'system', return_value='Linux'), \
             mock.patch('builtins.input', side_effect=answers), \
             mock.patch.object(installer.subprocess, 'run') as run, \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(installer.main(), 0)
            run.assert_not_called()
        self.assertEqual((target / 'INIZH.big').read_bytes(), b'synthetic expansion')
        self.assertEqual((target / 'ZH_Generals/Textures.big').read_bytes(), b'synthetic original')
        self.assertEqual((target / 'Data/keep.txt').read_text(), 'synthetic')
        self.assertFalse(list(self.home.glob('.zhr-setup-*')))

    def test_macos_build_keeps_cpu_tuning_automatic_and_preserves_the_override(self):
        with mock.patch.object(installer.sys, 'argv', ['install.py']), \
             mock.patch.object(installer.sys.stdin, 'isatty', return_value=True), \
             mock.patch.object(installer.platform, 'system', return_value='Darwin'), \
             mock.patch.object(installer.platform, 'machine', return_value='arm64'), \
             mock.patch.object(installer, 'prerequisites'), \
             mock.patch.dict(os.environ, {'ZH_MACOS_CPU_TUNING': 'generic'}), \
             mock.patch('builtins.input', side_effect=['yes', '']) as prompt, \
             mock.patch.object(installer, 'run') as run, \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(installer.main(), 0)
            self.assertEqual(prompt.call_count, 2)  # build consent and optional game data
            run.assert_called_once()
            args, kwargs = run.call_args
            self.assertEqual(args[0], [self.repo / 'build-macos.sh', 'Release', 'test'])
            self.assertNotIn('env', kwargs)

    def test_english_sources_are_not_translated_and_german_answers_are_accepted(self):
        english, german = i18n.Translator('en'), i18n.Translator('de')
        self.assertEqual(english('%s is missing from this folder.', 'X'), 'X is missing from this folder.')
        self.assertEqual(english.hint, '[y/N]')
        self.assertEqual(german.hint, '[j/N]')
        self.assertTrue(german.is_yes('Ja') and german.is_yes(' y ') and not german.is_yes(''))
        self.assertFalse(english.is_yes('j'))
        self.assertEqual(i18n.Translator('xx')('Zero Hour'), 'Zero Hour')  # unknown language: English

    def test_language_detection_ignores_c_posix_and_hostile_values(self):
        for environ, expected in (({'LANG': 'de_DE.UTF-8'}, 'de'), ({'LC_ALL': 'C', 'LANG': 'de_DE'}, 'en'),
                                  ({'LANG': 'POSIX'}, 'en'), ({'ZH_INSTALL_LANG': 'fr', 'LANG': 'de'}, 'fr'),
                                  ({}, 'en')):
            self.assertEqual(i18n.language(environ), expected)
        self.assertEqual(i18n.load('../install'), {})

    def test_every_catalog_covers_all_messages_with_matching_placeholders(self):
        keys = set()
        for node in ast.walk(ast.parse((TOOLS / 'install.py').read_text(encoding='utf-8'))):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == '_' \
               and node.args and isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str):
                keys.add(node.args[0].value)
        shell = (TOOLS / 'build-posix.sh').read_text(encoding='utf-8')
        keys.update(re.findall(r'guided_step "([^"]+)"', shell))
        keys.add('Input ended; no further steps will be run.')
        self.assertGreater(len(keys), 30)
        placeholder = re.compile(r'%[sd]')
        for catalog in sorted((TOOLS / 'installer_locales').glob('*.json')):
            messages = i18n.load(catalog.stem)['messages']
            self.assertEqual(sorted(keys - set(messages)), [], catalog.name)
            # Additional shared prompt translations are allowed.
            for english, translated in messages.items():
                self.assertEqual(placeholder.findall(english), placeholder.findall(translated), english)

    def test_shell_prompts_share_the_translator(self):
        import subprocess
        import sys
        command = [sys.executable, str(TOOLS / 'install_i18n.py'), 'confirm', 'Configure CMake and write build files to %s?', '/b']
        for lang, answer, code in (('en', 'y\n', 0), ('en', '\n', 1), ('de', 'ja\n', 0), ('de', 'n\n', 1), ('en', '', 130)):
            result = subprocess.run(command, input=answer, capture_output=True, text=True,
                                    env=dict(os.environ, ZH_INSTALL_LANG=lang))
            self.assertEqual(result.returncode, code, (lang, answer))
            self.assertIn('/b', result.stdout)
        self.assertIn('[j/N]', subprocess.run(command, input='\n', capture_output=True, text=True,
                                              env=dict(os.environ, ZH_INSTALL_LANG='de')).stdout)

    def test_invalid_catalogs_fall_back_to_english(self):
        catalogs = self.home / 'locales'
        catalogs.mkdir()
        invalid = [None, [], True, {'messages': []}, {'messages': {'Question?': 42}},
                   {'yes': 'yes'}, {'yes': [True]}, {'hint': None}, {'hint': ''}]
        with mock.patch.object(i18n, 'LOCALES', catalogs):
            for value in invalid:
                with self.subTest(catalog=value):
                    (catalogs / 'de.json').write_text(json.dumps(value), encoding='utf-8')
                    translated = i18n.Translator('de')
                    self.assertEqual(translated('Question?'), 'Question?')
                    self.assertEqual(translated.hint, '[y/N]')
                    self.assertTrue(translated.is_yes('yes'))
                    self.assertFalse(translated.is_yes('j'))
            (catalogs / 'de.json').write_text('{', encoding='utf-8')
            self.assertEqual(i18n.Translator('de')('Question?'), 'Question?')

    def test_catalogs_cannot_turn_empty_or_negative_answers_into_consent(self):
        catalogs = self.home / 'locales'
        catalogs.mkdir()
        value = {'yes': ['', '  ', 'n', 'no', 'OUI'], 'messages': {}, 'hint': '[o/N]'}
        (catalogs / 'fr.json').write_text(json.dumps(value), encoding='utf-8')
        with mock.patch.object(i18n, 'LOCALES', catalogs):
            translated = i18n.Translator('fr')
            for answer in ('', '  ', 'n', 'NO'):
                self.assertFalse(translated.is_yes(answer), answer)
            self.assertTrue(translated.is_yes(' Oui '))
            self.assertTrue(translated.is_yes('yes'))

    def test_bad_translation_placeholders_fall_back_without_losing_path_arguments(self):
        catalogs = self.home / 'locales'
        catalogs.mkdir()
        key = 'Build %s in %s?'
        for text in ('Build %s?', 'Build %d in %s?', 'Build %q %s in %s?'):
            (catalogs / 'de.json').write_text(json.dumps({'messages': {key: text}}), encoding='utf-8')
            with mock.patch.object(i18n, 'LOCALES', catalogs):
                self.assertEqual(i18n.Translator('de')(key, 'Release', '/a path/100%'),
                                 'Build Release in /a path/100%?')

    def test_invalid_prompt_arguments_are_errors_rather_than_declined_steps(self):
        import subprocess
        import sys
        helper = str(TOOLS / 'install_i18n.py')
        for arguments in (['confirm', 'Build %d?', 'Release'], ['confirm'], ['unknown']):
            result = subprocess.run([sys.executable, helper] + arguments, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)

    def test_bootstrap_uses_the_selected_languages_yes_words(self):
        import subprocess
        bootstrap = self.home / 'bootstrap-mocks.sh'
        bootstrap.write_text("""uname() { printf 'Darwin\\n'; }
command() {
  if [ "${1:-}" = -v ] && [ "${2:-}" = python3 ]; then
    printf '/usr/bin/python3\\n'; return 0
  fi
  builtin command "$@"
}
xcode-select() {
  [ "${1:-}" = --install ] || return 1
  printf 'XCODE_REQUESTED\\n'
}
""", encoding='utf-8')
        for lang, answer, install in (('en', 'j\n', False), ('en', 'yes\n', True),
                                      ('de', 'ja\n', True), ('DE_de.UTF-8', 'Ja\n', True),
                                      ('de', '\n', False), ('en', '', False)):
            result = subprocess.run(['/bin/bash', str(TOOLS / 'install-posix.sh')],
                                    input=answer, capture_output=True, text=True,
                                    env=dict(os.environ, BASH_ENV=str(bootstrap), ZH_INSTALL_LANG=lang))
            self.assertEqual('XCODE_REQUESTED' in result.stdout, install, (lang, answer))
            self.assertEqual(result.returncode, 130 if not answer else 1)
            self.assertIn('[j/N]' if lang.lower().startswith('de') else '[y/N]', result.stdout)

    def test_questions_are_visible_before_any_answer_is_sent(self):
        import select
        import subprocess
        import sys
        import time
        commands = [
            ([sys.executable, str(TOOLS / 'install_i18n.py'), 'confirm', 'Visible question?'], b'Visible question? [y/N]\n'),
            ([sys.executable, '-c',
              'import sys; sys.path.insert(0, sys.argv[1]); import install; '
              'raise SystemExit(0 if install.confirm("Visible question?") else 1)', str(TOOLS)],
             b'Visible question? [y/N]\n')
        ]
        for command, expected in commands:
            child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     env=dict(os.environ, ZH_INSTALL_LANG='en'))
            try:
                visible = b''
                deadline = time.monotonic()+5
                while expected not in visible:
                    remaining = deadline-time.monotonic()
                    self.assertGreater(remaining, 0, visible)
                    ready, _, _ = select.select([child.stdout], [], [], remaining)
                    self.assertTrue(ready, visible)
                    chunk = os.read(child.stdout.fileno(), 4096)
                    self.assertTrue(chunk, visible)
                    visible += chunk
                self.assertIsNone(child.poll())  # the process is still awaiting our first answer
                child.stdin.write(b'n\n'); child.stdin.flush()
                _, errors = child.communicate(timeout=5)
                self.assertEqual(child.returncode, 1, errors)
                self.assertEqual(errors, b'')
            finally:
                if child.poll() is None:
                    child.kill()
                child.communicate()

    def test_child_output_uses_one_ordered_stream(self):
        import subprocess
        import sys
        script = ('import sys; sys.path.insert(0, sys.argv[1]); import install; '
                  'install.run([sys.executable, "-c", sys.argv[2]])')
        child_code = "import os; os.write(1, b'first-out\\n'); os.write(2, b'second-error\\n')"
        result = subprocess.run([sys.executable, '-c', script, str(TOOLS), child_code],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, '')
        self.assertTrue(result.stdout.endswith('first-out\nsecond-error\n'), result.stdout)

    def test_selected_configuration_and_build_directory_reach_build_and_launch(self):
        for directory in ('output with spaces', str(self.home / 'external build')):
            build = Path(directory)
            if not build.is_absolute():
                build = self.repo / build
            app = build / 'Zero Hour Reforged.app'
            app.mkdir(parents=True)
            with mock.patch.object(installer.sys, 'argv', ['install.py']), \
                 mock.patch.object(installer.sys.stdin, 'isatty', return_value=True), \
                 mock.patch.object(installer.platform, 'system', return_value='Darwin'), \
                 mock.patch.object(installer, 'prerequisites'), \
                 mock.patch.dict(os.environ, {'ZH_BUILD_CONFIG': 'Debug', 'BUILD': directory}), \
                 mock.patch('builtins.input', side_effect=['yes', '', '', 'yes']), \
                 mock.patch.object(installer, 'run') as run, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(installer.main(), 0)
                self.assertEqual(run.call_args_list[0].args[0], [self.repo / 'build-macos.sh', 'Debug', 'test'])
                self.assertEqual(run.call_args_list[1].args[0], ['open', str(app)])

    def test_configuration_cannot_select_the_builders_clean_command(self):
        with mock.patch.object(installer.sys, 'argv', ['install.py']), \
             mock.patch.object(installer.sys.stdin, 'isatty', return_value=True), \
             mock.patch.object(installer.platform, 'system', return_value='Darwin'), \
             mock.patch.dict(os.environ, {'ZH_BUILD_CONFIG': 'clean'}), \
             mock.patch.object(installer, 'prerequisites') as prerequisites, \
             mock.patch.object(installer, 'run') as run, \
             contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(ValueError): installer.main()
            prerequisites.assert_not_called()
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
