#!/usr/bin/env python3
"""Installer behavior with synthetic data and no package, build or game execution."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('guided_installer', Path(__file__).resolve().parents[1] / 'Tools/install.py')
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir='/tmp')
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name).resolve()
        self.repo = self.home / 'source checkout'
        self.repo.mkdir()
        patcher = mock.patch.object(installer, 'ROOT', self.repo)
        patcher.start(); self.addCleanup(patcher.stop)

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
        answers = ['', '', 'j', '', str(zero), str(base), 'j', str(target), 'j']
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


if __name__ == '__main__':
    unittest.main()
