#!/usr/bin/env python3
"""Synthetic developer-tool regressions; no retail game data is used."""

import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bigfile", CODE / "Tools/bigfile.py")
bigfile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bigfile)


def archive(path, name, data=b"synthetic"):
    name = name.encode("latin-1") + b"\0"
    offset = 24 + len(name)
    path.write_bytes(b"BIGF" + struct.pack("<I", offset + len(data))
                     + struct.pack(">IIII", 1, offset, offset, len(data)) + name + data)


class BigfileExtraction(unittest.TestCase):
    def test_archive_paths_cannot_escape_output(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            outside = root / "outside.txt"
            for name in ("../outside.txt", "..\\outside.txt", str(outside),
                         "C:\\outside.txt", "C:outside.txt", "\\\\server\\share\\file"):
                with self.subTest(name=name):
                    outside.write_bytes(b"untouched")
                    source = root / "test.big"
                    archive(source, name)
                    with self.assertRaises((ValueError, OSError)):
                        with contextlib.redirect_stdout(io.StringIO()):
                            bigfile.cmd_extract(str(source), outdir=str(root / "out"))
                    self.assertEqual(outside.read_bytes(), b"untouched")

    def test_archive_rejects_truncated_headers_and_members(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            source = root / "test.big"
            for data in (b"BIGF", b"BIGF" + b"\0" * 7):
                source.write_bytes(data)
                with self.assertRaises(ValueError):
                    with source.open("rb") as stream:
                        list(bigfile._read_index(stream))
            archive(source, "Data\\safe.txt")
            source.write_bytes(source.read_bytes()[:-1])
            with self.assertRaises(ValueError):
                with contextlib.redirect_stdout(io.StringIO()):
                    bigfile.cmd_extract(str(source), outdir=str(root / "out"))
            self.assertFalse((root / "out/Data/safe.txt").exists())

    def test_extraction_writes_a_member_under_its_folders(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            archive(root / "test.big", "Data\\safe.txt", b"portable")
            with contextlib.redirect_stdout(io.StringIO()):
                bigfile.cmd_extract(str(root / "test.big"), outdir=str(root / "out"))
            self.assertEqual((root / "out/Data/safe.txt").read_bytes(), b"portable")


if __name__ == "__main__":
    unittest.main()
