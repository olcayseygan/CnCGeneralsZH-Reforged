#!/usr/bin/env python3
"""Read and write the game's BIG archives.

Format, straight out of Win32BIGFileSystem::openArchiveFile:

    0x00  "BIGF"
    0x04  archive size, little endian (the game reads it and ignores it)
    0x08  file count, BIG endian
    0x0C  offset of the first file's data, big endian (the game skips this and
          seeks to 0x10 for the index, so it is only advisory)
    0x10  index: for each file, offset (BE), size (BE), NUL-terminated path
          then the file data at the offsets the index gave

File data is stored raw.  Refpack-compressed members exist in some EA archives
(they start with 0x10FB) and are reported, not decoded - nothing in this game's
archives needs it.

    python bigfile.py list   <archive.big> [pattern]
    python bigfile.py extract <archive.big> [pattern] [-o outdir]
    python bigfile.py pack   <archive.big> <indir>

'pattern' is a case-insensitive fnmatch over the stored path, e.g. "*.ini" or
"data/ini/particlesystem.ini".  extract writes each member under outdir keeping
its stored path (default: the archive name without its extension).

pack rebuilds an archive from a directory laid out the way extract leaves one -
enough to put an edited INI back into a copy of the .big, though loose files
under Run/ override archives anyway and are the easier route for one file.
"""

import fnmatch
import os
import struct
import sys


def _read_index(f):
    """Yield (path, offset, size) for every member, leaving f's position undefined."""
    magic = f.read(4)
    if magic != b"BIGF":
        raise ValueError("not a BIG archive (magic is %r, expected b'BIGF')" % magic)
    f.read(4)                                             # archive size, unused
    (count,) = struct.unpack(">I", f.read(4))
    f.seek(0x10)
    for _ in range(count):
        offset, size = struct.unpack(">II", f.read(8))
        name = bytearray()
        while True:
            ch = f.read(1)
            if not ch:
                raise ValueError("archive ends inside the index")
            if ch == b"\0":
                break
            name += ch
        yield name.decode("latin-1"), offset, size


def _matches(path, pattern):
    if not pattern:
        return True
    return fnmatch.fnmatch(path.lower().replace("\\", "/"),
                           pattern.lower().replace("\\", "/"))


def cmd_list(archive, pattern=None):
    with open(archive, "rb") as f:
        entries = list(_read_index(f))
        shown = 0
        for path, offset, size in entries:
            if not _matches(path, pattern):
                continue
            f.seek(offset)
            note = "  (refpack)" if f.read(2) == b"\x10\xfb" else ""
            print("%10d  %s%s" % (size, path, note))
            shown += 1
    print("%d of %d members" % (shown, len(entries)))


def cmd_extract(archive, pattern=None, outdir=None):
    if outdir is None:
        outdir = os.path.splitext(os.path.basename(archive))[0]
    written = 0
    with open(archive, "rb") as f:
        for path, offset, size in list(_read_index(f)):
            if not _matches(path, pattern):
                continue
            dest = os.path.join(outdir, path.replace("\\", os.sep))
            parent = os.path.dirname(dest)
            if parent:
                os.makedirs(parent, exist_ok=True)
            f.seek(offset)
            data = f.read(size)
            if len(data) != size:
                raise ValueError("%s: wanted %d bytes, archive had %d" % (path, size, len(data)))
            with open(dest, "wb") as out:
                out.write(data)
            print(dest)
            written += 1
    print("%d file(s) written" % written)


def cmd_pack(archive, indir):
    members = []
    for root, _dirs, files in os.walk(indir):
        for name in sorted(files):
            full = os.path.join(root, name)
            stored = os.path.relpath(full, indir).replace(os.sep, "\\")
            members.append((stored, full))
    write_archive(archive, members)


def write_archive(archive, members):
    """members: (stored path, file on disk). Sorted here, so the same members give the same bytes."""
    members = sorted(members, key=lambda m: m[0].lower())

    # index size first: the data offsets depend on it
    index_size = 0x10 + sum(8 + len(s.encode("latin-1")) + 1 for s, _ in members)

    offset = index_size
    placed = []
    for stored, full in members:
        size = os.path.getsize(full)
        placed.append((stored, full, offset, size))
        offset += size
    total = offset

    with open(archive, "wb") as out:
        out.write(b"BIGF")
        out.write(struct.pack("<I", total))
        out.write(struct.pack(">I", len(placed)))
        out.write(struct.pack(">I", index_size))
        for stored, _full, off, size in placed:
            out.write(struct.pack(">II", off, size))
            out.write(stored.encode("latin-1") + b"\0")
        assert out.tell() == index_size, (out.tell(), index_size)
        for _stored, full, _off, _size in placed:
            with open(full, "rb") as src:
                out.write(src.read())
    print("%s: %d file(s), %d bytes" % (archive, len(placed), total))


def cmd_selfcheck():
    """Round-trip pack -> read -> extract on synthetic files, so the check needs no game data."""
    import shutil
    import tempfile

    work = tempfile.mkdtemp(prefix="bigfile_selfcheck_")
    try:
        src = os.path.join(work, "src")
        cases = {
            "Data\\INI\\Alpha.ini": b"ParticleSystem Test\r\n  Gravity = -1.0\r\nEnd\r\n",
            "Data\\INI\\Sub\\Beta.ini": b"",                       # empty member
            "Loose.txt": bytes(range(256)) * 40,                   # every byte value, > 8k
        }
        for stored, data in cases.items():
            path = os.path.join(src, stored.replace("\\", os.sep))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)

        archive = os.path.join(work, "test.big")
        cmd_pack(archive, src)

        with open(archive, "rb") as f:
            index = {p: (o, s) for p, o, s in _read_index(f)}
            assert set(index) == set(cases), (sorted(index), sorted(cases))
            for stored, data in cases.items():
                offset, size = index[stored]
                assert size == len(data), (stored, size, len(data))
                f.seek(offset)
                assert f.read(size) == data, stored
            # every member must start after the index and end inside the file
            end = os.path.getsize(archive)
            for stored, (offset, size) in index.items():
                assert 0x10 <= offset and offset + size <= end, (stored, offset, size, end)

        out = os.path.join(work, "out")
        cmd_extract(archive, None, out)
        for stored, data in cases.items():
            with open(os.path.join(out, stored.replace("\\", os.sep)), "rb") as f:
                assert f.read() == data, stored

        # and a non-archive must be refused rather than read as garbage
        junk = os.path.join(work, "junk.big")
        with open(junk, "wb") as f:
            f.write(b"NOPE" + b"\0" * 64)
        try:
            with open(junk, "rb") as f:
                list(_read_index(f))
        except ValueError:
            pass
        else:
            raise AssertionError("a file without the BIGF magic was accepted")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("bigfile selfcheck OK")


def main(argv):
    if len(argv) == 2 and argv[1] == "selfcheck":
        cmd_selfcheck()
        return 0

    if len(argv) < 3:
        print(__doc__.strip())
        return 2

    cmd, archive = argv[1], argv[2]
    rest = argv[3:]

    outdir = None
    if "-o" in rest:
        i = rest.index("-o")
        if i + 1 >= len(rest):
            print("-o needs a directory")
            return 2
        outdir = rest[i + 1]
        rest = rest[:i] + rest[i + 2:]
    pattern = rest[0] if rest else None

    if cmd == "list":
        cmd_list(archive, pattern)
    elif cmd == "extract":
        cmd_extract(archive, pattern, outdir)
    elif cmd == "pack":
        if not pattern:
            print("pack needs a source directory")
            return 2
        cmd_pack(archive, pattern)
    else:
        print(__doc__.strip())
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
