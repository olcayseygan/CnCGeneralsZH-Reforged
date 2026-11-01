#!/usr/bin/env python3
"""Installer behavior with synthetic data and no package, build or game execution."""
import contextlib
import ast
import importlib
import importlib.util
import io
import json
import os
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
        patcher = mock.patch.object(installer, 'ROOT', self.repo)
        patcher.start(); self.addCleanup(patcher.stop)
        # Answers below are English; the catalog tests cover other languages.
        translator = mock.patch.object(installer, '_', i18n.Translator('en'))
        translator.start(); self.addCleanup(translator.stop)

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
            self.assertEqual(prompt.call_count, 2)  # build consent, then optional game data
            run.assert_called_once()
            args, kwargs = run.call_args
            self.assertEqual(args[0], [self.repo / 'build-macos.sh', 'Release', 'test'])
            self.assertEqual(kwargs['env']['ZH_GUIDED_BUILD'], '1')
            self.assertEqual(kwargs['env']['ZH_MACOS_CPU_TUNING'], 'generic')

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
            self.assertEqual(sorted(set(messages) - keys), [], catalog.name)
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

    def test_build_prompt_errors_abort_instead_of_becoming_a_no_answer(self):
        import subprocess
        source = (TOOLS / 'build-posix.sh').read_text(encoding='utf-8')
        function = re.search(r'guided_step\(\) \{\n.*?\n\}', source, re.DOTALL).group()
        for status in (2, 127, 130):
            script = 'ZH_GUIDED_BUILD=1\nSRC=unused\npython3() { return %d; }\n%s\n' % (status, function)
            script += 'if guided_step "Question?"; then exit 42; else exit 43; fi\n'
            result = subprocess.run(['/bin/bash', '-c', script], capture_output=True, text=True)
            self.assertEqual(result.returncode, status)

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


if __name__ == '__main__':
    unittest.main()
