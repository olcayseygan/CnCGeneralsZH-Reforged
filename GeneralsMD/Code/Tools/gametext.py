#!/usr/bin/env python3
"""The game's words: EA's compiled string table out to text, and a translation checked against it.

GameText.cpp reads two formats.  Data/English/generals.csf inside EnglishZH.big is the binary table
every label the game fetches comes from, 6422 of them.  A .str is the same table as text, which the
game has always been able to parse and which Data/Patch.str already lays over the CSF.  A translation
is one more .str on top, Data/<Language>/Generals.str: a label it carries overrules the CSF's and
Patch.str's, and a label it leaves out stays English.  The file is UTF-8, which GameText.cpp's
translateCopy decodes.

    python gametext.py dump <generals.csf | EnglishZH.big> <outdir> [--str extra.str ...]
                            [--part-chars N]

writes every line worth translating into outdir/part_NN.str, about N characters of English apiece,
in the order the CSF keeps them.  --str adds the labels of another .str after the CSF's, Patch.str
for one.  Left out: the subtitles EA disabled by starting them with '*' (the game shows none of
them), NUMBER: and LETTER:, and anything with no letter in it.

    python gametext.py merge <out.str> <part.str> ...

joins translated parts into one file under the master header.

    python gametext.py check <translation.str> [<generals.csf | EnglishZH.big> [--str extra.str ...]]

parses the translation the way the game does and refuses what would break it: a label twice, a line
past the game's 10 KB buffer, and, given the English, a label the game does not have or a string
whose printf directives differ from the English one's.  That last one is not cosmetic.  A string
that reaches UnicodeString::format with a directive missing, added or moved reads the wrong
argument, and Turkish writes a percentage as "%10", which is a directive.

    python gametext.py selfcheck

round-trips synthetic strings through the writer and the parser, proves check catches a changed
directive, and checks the tracked Turkish, German, Ukrainian, Russian and French masters: on their
own always, and against the English when Run/EnglishZH.big is there.  Run by CTest as gametext_selfcheck.
"""

import argparse
import os
import re
import struct
import sys
import tempfile

import bigfile


_HERE = os.path.dirname(os.path.abspath(__file__))
_CODE = os.path.dirname(_HERE)

MASTERS = [(language, os.path.join(_CODE, "Data", language, "Generals.str"))
           for language in ("Turkish", "German", "Ukrainian", "Russian", "French")]
PATCH_STRINGS = os.path.join(_CODE, "Data", "Patch.str")
ENGLISH_ARCHIVE = os.path.join(_CODE, "..", "Run", "EnglishZH.big")

CSF_MEMBER = "data/english/generals.csf"
CSF_MAGIC = b" FSC"
LABEL_MAGIC = b" LBL"
STRING_WITH_WAVE_MAGIC = b"WRTS"
CSF_HEADER_BYTES = 24
CSF_LABEL_COUNT_OFFSET = 8
UTF16_UNIT_BYTES = 2
BYTE_MASK = 0xFF

# GameText.cpp's MAX_UITEXT_LENGTH, less the newline and the NUL readLine's caller adds to a line
MAX_LINE_BYTES = 10 * 1024 - 2
DEFAULT_PART_CHARS = 12000

HIDDEN_SUBTITLE_PREFIX = "DIALOGEVENT:"
HIDDEN_SUBTITLE_MARK = "*"
UNTRANSLATED_PREFIXES = ("NUMBER:", "LETTER:")
ANY_LETTER = re.compile(r"[A-Za-z]")

# what the MSVC printf family takes as a directive, as far as any string in this game goes.  The
# space flag is left out on purpose: no formatted string uses it, and with it English prose such as
# "5% enemy value" reads as a "% e" directive.
DIRECTIVE = re.compile(
    r"%(?:%|[-+#0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|I64|I32|L|w)?[diouxXeEfgGaAcsSpn])")

MASTER_HEADER = """\
// MASTER COPY - this is the tracked one.  The build copies it into Run/Data, where GameText.cpp
// lays it over the English string table when Options > Gameplay > Language asks for it.  Written by
// Tools/gametext.py merge and checked by gametext.py check; CTest runs gametext_selfcheck.
//
// UTF-8.  A label, its string in double quotes, END.  \\n is a line break, \\" a quote and \\\\ a
// backslash.  Every % belongs to a directive the English string has, in the same order.
"""


def read_csf(data):
    """[(label, text)] out of a compiled string file, keeping the first string of each label the way
    GameTextManager::parseCSF does."""
    if data[:len(CSF_MAGIC)] != CSF_MAGIC:
        raise ValueError("not a CSF (magic is %r, expected %r)" % (data[:len(CSF_MAGIC)], CSF_MAGIC))
    (label_count,) = struct.unpack_from("<I", data, CSF_LABEL_COUNT_OFFSET)
    offset = CSF_HEADER_BYTES
    entries = []
    for label_index in range(label_count):
        magic, string_count, label_length = struct.unpack_from("<4sII", data, offset)
        if magic != LABEL_MAGIC:
            raise ValueError("label %d at 0x%X starts %r, expected %r"
                             % (label_index, offset, magic, LABEL_MAGIC))
        offset += struct.calcsize("<4sII")
        label = data[offset:offset + label_length].decode("latin-1")
        offset += label_length
        text = ""
        for string_index in range(string_count):
            magic, length = struct.unpack_from("<4sI", data, offset)
            offset += struct.calcsize("<4sI")
            inverted = data[offset:offset + length * UTF16_UNIT_BYTES]
            offset += length * UTF16_UNIT_BYTES
            if string_index == 0:
                text = bytes(~byte & BYTE_MASK for byte in inverted).decode("utf-16-le")
            if magic == STRING_WITH_WAVE_MAGIC:
                (wave_length,) = struct.unpack_from("<I", data, offset)
                offset += struct.calcsize("<I") + wave_length
        entries.append((label, text))
    return entries


def read_english(path):
    """The CSF's entries, out of the file itself or out of the archive that carries it."""
    with open(path, "rb") as source:
        if not path.lower().endswith(".big"):
            return read_csf(source.read())
        for member, offset, size in list(bigfile._read_index(source)):
            if member.lower().replace("\\", "/") == CSF_MEMBER:
                source.seek(offset)
                return read_csf(source.read(size))
    raise ValueError("%s carries no %s" % (path, CSF_MEMBER))


def escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")


def unescape(text):
    """translateCopy's escapes: \\n and \\t are the control characters, anything else after a
    backslash is itself."""
    out = []
    characters = iter(text)
    for character in characters:
        if character != "\\":
            out.append(character)
            continue
        following = next(characters, "")
        out.append({"n": "\n", "t": "\t"}.get(following, following))
    return "".join(out)


def unescaped_quote(body):
    """Where readToEndOfQuote stops: the first quote not behind an odd run of backslashes, or -1."""
    slash = False
    for position, character in enumerate(body):
        if character == "\\":
            slash = not slash
            continue
        if character == '"' and not slash:
            return position
        slash = False
    return -1


def decode(raw):
    """A translation is UTF-8; EA's own .str files are ASCII with the odd Latin-1 byte."""
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("latin-1")


def parse_str(path):
    """[(label, text)] the way GameTextManager::parseStringFile reads a .str.  Raises on anything
    that file would silently read wrongly."""
    with open(path, "rb") as source:
        lines = decode(source.read()).splitlines()

    entries = []
    index = 0
    while index < len(lines):
        label = lines[index].strip()
        index += 1
        if not label or label.startswith("//"):
            continue

        text = None
        while True:
            if index >= len(lines):
                raise ValueError("%s: %s has no END" % (path, label))
            line = lines[index]
            index += 1
            stripped = line.strip()
            if stripped.upper() == "END":
                break
            if not stripped.startswith('"'):
                continue
            if len(stripped.encode("utf-8")) > MAX_LINE_BYTES:
                raise ValueError("%s: %s is %d bytes on one line, the game reads %d"
                                 % (path, label, len(stripped.encode("utf-8")), MAX_LINE_BYTES))

            body = stripped[1:]
            while unescaped_quote(body) < 0:
                if index >= len(lines):
                    raise ValueError("%s: the string of %s never closes its quote" % (path, label))
                body += " " + lines[index]
                index += 1
            if text is not None:
                raise ValueError("%s: %s has two strings" % (path, label))
            text = unescape(body[:unescaped_quote(body)])

        entries.append((label, text if text is not None else ""))
    return entries


def write_str(path, entries, header=""):
    lines = [header] if header else []
    for label, text in entries:
        lines.extend([label, '"%s"' % escape(text), "END", ""])
    with open(path, "w", encoding="utf-8", newline="\r\n") as out:
        out.write("\n".join(lines))


def worth_translating(label, text):
    upper = label.upper()
    if not ANY_LETTER.search(text):
        return False
    if upper.startswith(UNTRANSLATED_PREFIXES):
        return False
    return not (upper.startswith(HIDDEN_SUBTITLE_PREFIX) and text.startswith(HIDDEN_SUBTITLE_MARK))


def english_entries(english_path, extra_paths):
    """The CSF, then each extra .str overruling it label by label, in first-seen order."""
    merged = {}
    for label, text in read_english(english_path):
        merged.setdefault(label.lower(), [label, text])
    for extra in extra_paths:
        for label, text in parse_str(extra):
            merged.setdefault(label.lower(), [label, text])[1] = text
    return [tuple(entry) for entry in merged.values()]


def check_translation(translated, english):
    """Problems with a translation, as sentences.  english is None to check the file on its own."""
    problems = []
    seen = set()
    for label, _text in translated:
        if label.lower() in seen:
            problems.append("%s is there twice" % label)
        seen.add(label.lower())

    if english is None:
        return problems

    source = dict((label.lower(), text) for label, text in english)
    for label, text in translated:
        original = source.get(label.lower())
        if original is None:
            problems.append("%s is not a label the game has" % label)
            continue
        wanted, found = DIRECTIVE.findall(original), DIRECTIVE.findall(text)
        if found != wanted:
            problems.append("%s: directives %s, the English has %s" % (label, found, wanted))
        # English writes "50%" and never formats those strings; a translation spells it "yüzde 50",
        # because "%50" in front of a letter is a directive the English does not have
        bare = DIRECTIVE.sub("", text).count("%")
        if bare:
            problems.append("%s: %d %% sign(s) outside a directive; write 'yüzde' instead" % (label, bare))
    return problems


def dump(arguments):
    entries = [entry for entry in english_entries(arguments.english, arguments.str)
               if worth_translating(*entry)]
    os.makedirs(arguments.outdir, exist_ok=True)

    parts = [[]]
    characters = 0
    for entry in entries:
        if characters >= arguments.part_chars:
            parts.append([])
            characters = 0
        parts[-1].append(entry)
        characters += len(entry[1])

    for number, part in enumerate(parts):
        write_str(os.path.join(arguments.outdir, "part_%02d.str" % number), part)
    print("%d strings, %d characters, in %d parts under %s"
          % (len(entries), sum(len(text) for _label, text in entries), len(parts), arguments.outdir))
    return 0


def merge(arguments):
    entries = []
    for part in arguments.parts:
        entries.extend(parse_str(part))
    problems = check_translation(entries, None)
    for problem in problems:
        print("merge: %s" % problem)
    if problems:
        return 1
    write_str(arguments.out, entries, MASTER_HEADER)
    print("%s: %d strings" % (arguments.out, len(entries)))
    return 0


def report(name, translated, english):
    problems = check_translation(translated, english)
    for problem in problems:
        print("%s: %s" % (name, problem))
    if english is not None and not problems:
        wanted = [label for label, text in english if worth_translating(label, text)]
        carried = set(label.lower() for label, _text in translated)
        missing = [label for label in wanted if label.lower() not in carried]
        print("%s: %d strings, %d of %d worth translating are still English%s"
              % (name, len(translated), len(missing), len(wanted),
                 (" (%s ...)" % ", ".join(missing[:5])) if missing else ""))
    return problems


def check(arguments):
    english = english_entries(arguments.english, arguments.str) if arguments.english else None
    return 1 if report(os.path.basename(arguments.translation), parse_str(arguments.translation),
                       english) else 0


def selfcheck(_arguments):
    problems = []

    sample = [
        ("GUI:Plain", "Kışla ve İkmal Merkezi, ğüşöç"),
        ("GUI:Awkward", 'He said "go" \\ now\nnext\tline'),
        ("GUI:Cost", "Bedel: %d"),
    ]
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "sample.str")
        write_str(path, sample, MASTER_HEADER)
        if parse_str(path) != sample:
            problems.append("a written .str does not read back: %r" % parse_str(path))

    english = [("GUI:Cost", "Cost: %d"), ("GUI:Share", "%d percent")]
    if not check_translation([("GUI:Share", "%10 hasar")], english):
        problems.append("check let a Turkish percentage through as if it were the directive")
    if not check_translation([("GUI:Share", "%s yüzde")], english):
        problems.append("check let a changed directive through")
    if check_translation([("GUI:Cost", "Bedel: %d")], english):
        problems.append("check refused a string whose directives are the English ones")

    reference = None
    if os.path.exists(ENGLISH_ARCHIVE):
        reference = english_entries(ENGLISH_ARCHIVE, [PATCH_STRINGS])
    else:
        print("gametext: no %s, so the masters are checked on their own" % ENGLISH_ARCHIVE)
    for language, master in MASTERS:
        if not os.path.exists(master):
            problems.append("there is no %s" % master)
        else:
            problems.extend(report(language, parse_str(master), reference))

    for problem in problems:
        print("gametext: %s" % problem)
    return 1 if problems else 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    dump_command = commands.add_parser("dump")
    dump_command.add_argument("english")
    dump_command.add_argument("outdir")
    dump_command.add_argument("--str", action="append", default=[])
    dump_command.add_argument("--part-chars", type=int, default=DEFAULT_PART_CHARS)
    dump_command.set_defaults(run=dump)

    merge_command = commands.add_parser("merge")
    merge_command.add_argument("out")
    merge_command.add_argument("parts", nargs="+")
    merge_command.set_defaults(run=merge)

    check_command = commands.add_parser("check")
    check_command.add_argument("translation")
    check_command.add_argument("english", nargs="?")
    check_command.add_argument("--str", action="append", default=[])
    check_command.set_defaults(run=check)

    commands.add_parser("selfcheck").set_defaults(run=selfcheck)

    arguments = parser.parse_args(argv[1:])
    return arguments.run(arguments)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
