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

    python gametext.py labels [--language Turkish ...] [--english] [--inventory <folder>]

measures every label, a string drawn on a control that does not grow (layout buttons, check boxes,
radio buttons, combo boxes and one-line statics, the options catalog's rows and entries, the keys
code hands to such a control, LABEL_TABLES), against LABEL_LIMIT characters in each language the
fork ships, and fails on any translation over it.  English over the limit is listed with --english
and never fails.  --inventory writes, per language, the labels over the limit and every fork string
the language still shows in English, settings first, with the ones kept English on purpose marked.

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
        # a "%%" reads no argument, so it may stand anywhere ("Yükleniyor %%%d" for "Loading %d%%");
        # every other directive keeps the English order
        wanted, found = DIRECTIVE.findall(original), DIRECTIVE.findall(text)
        if ([d for d in found if d != "%%"] != [d for d in wanted if d != "%%"]
                or found.count("%%") != wanted.count("%%")):
            problems.append("%s: directives %s, the English has %s" % (label, found, wanted))
        # English writes "50%" and never formats those strings; a translation spells it "yüzde 50",
        # because "%50" in front of a letter is a directive the English does not have
        bare = DIRECTIVE.sub("", text).count("%")
        if bare:
            problems.append("%s: %d %% sign(s) outside a directive; write 'yüzde' instead" % (label, bare))
    return problems


# -- labels: strings drawn in a box that does not grow ------------------------------------------
#
# A label is a string the game draws on a control of fixed size: the TEXT of a push button, check
# box, radio button or combo box in a layout, of a static text whose box holds one line of its font,
# a row name or combo entry of the options catalog, and a key code hands straight to a button,
# check box, radio button or combo entry.  Tooltips, message bodies, chat and list rows grow or wrap
# and are not labels.  The class is derived from the layouts and the sources every run, so a new
# button is covered the day it is added; LABEL_TABLES lists the keys code keeps in tables, which no
# pattern can follow.

LABEL_LIMIT = 15
# True: a translated label over the limit fails gametext_selfcheck.  English never fails, see --english.
LABEL_FAILS = True

RUN = os.path.join(_CODE, "..", "Run")
SOURCES = os.path.join(_CODE, "GameEngine", "Source")
CATALOG = os.path.join(SOURCES, "Common", "OptionsCatalog.cpp")
# lowest priority first: retail archives, the build's loose copies (generated layouts among them),
# then the tracked masters
LAYOUT_ARCHIVES = [os.path.join(RUN, name) for name in ("WindowZH.big", "PatchWindow.big")]
LAYOUT_DIRS = [os.path.join(RUN, "Window"), os.path.join(_CODE, "Data", "Window")]
LABEL_CONTROLS = ("PUSHBUTTON", "CHECKBOX", "RADIOBUTTON", "COMBOBOX")
SETTINGS_LAYOUTS = ("menus/optionsmenu.wnd", "menus/optionsmenuclassic.wnd")
SETTINGS_SOURCES = ("OptionsMenu.cpp", "OptionsMenuHotkeys.cpp", "OptionsCatalog.cpp")

LABEL_SETTER = re.compile(
    r"\b(?:GadgetButtonSetText|GadgetCheckBoxSetText|GadgetRadioSetText|GadgetComboBoxAddEntry|winSetText"
    r"|WOLMakeButtonLike)\b[^;]*")
FETCH_LITERAL = re.compile(r'fetch\(\s*"([^"]+)"')
KEY_LITERAL = re.compile(r'"([A-Za-z]+:[A-Za-z0-9_]+)"')
COMPOSED = re.compile(r'ModeNameWithVersion\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)')
CATALOG_ROW = re.compile(r'\{\s*"\w+",\s*OPT_WND\(\s*"(\w+)"\s*\),\s*"([^"]+)"')

# keys code keeps in tables and draws on a fixed control
LABEL_TABLES = [
    ("GameClient/DirectStrikeLobby.cpp ARMY_LABELS",
     ["SIDE:America", "SIDE:China", "SIDE:GLA", "GUI:OnlineGeneralAir", "GUI:OnlineGeneralLaser",
      "GUI:OnlineGeneralSuper", "GUI:OnlineGeneralTank", "GUI:OnlineGeneralInf", "GUI:OnlineGeneralNuke",
      "GUI:OnlineGeneralToxin", "GUI:OnlineGeneralStealth", "GUI:OnlineGeneralDemo"]),
    ("GameClient/DirectStrikeLobby.cpp SEAT_LABELS",
     ["GUI:Open", "GUI:Closed", "GUI:DirectStrikeEasy", "GUI:DirectStrikeMedium", "GUI:DirectStrikeHard",
      "GUI:DirectStrikeBrutal", "GUI:DirectStrikeReady", "GUI:DirectStrikeStart"]),
    ("GUICallbacks/Menus/WOLLadderScreen.cpp HEAD",
     ["GUI:OnlineLadderRank", "GUI:OnlineLadderPlayer", "GUI:OnlineLadderTier", "GUI:OnlineLadderRating",
      "GUI:OnlineLadderWins", "GUI:OnlineLadderLosses", "GUI:OnlineLadderWinPct"]),
    ("GUICallbacks/Menus/WOLLadderScreen.cpp SKIN_SIDE_KEY, FILTER_KEY",
     ["GUI:OnlineSkinsSideUsa", "GUI:OnlineSkinsSideChina", "GUI:OnlineSkinsSideGla", "GUI:OnlineSkinsAll",
      "GUI:OnlineSkinsOwned", "GUI:OnlineSkinsFilterLocked"]),
]

# strings that stay as they are in every language; report() does not count them as still English
KEEP_ENGLISH = {
    "GUI:DirectStrike": "mode name the owner keeps in English",
    "GUI:DirectStrikeLower": "mode name the owner keeps in English",
    "GUI:Apocalypse": "mode name the owner keeps in English",
    "GUI:ApocalypseLower": "mode name the owner keeps in English",
}
KEEP_ENGLISH.update(("GUI:Language%d" % n, "a language's own name, the same in every menu") for n in range(6))
NUMBER_TAG = re.compile(r"^v?\d+(\.\d+)*x?$", re.IGNORECASE)


def keep_reason(label, text):
    """Why this string is left English everywhere, or None."""
    reason = KEEP_ENGLISH.get(label)
    if reason:
        return reason
    if NUMBER_TAG.match(text.strip()):
        return "a version, speed or sample count tag (v1.0, 1.5x, 4x)"
    if not ANY_LETTER.search(DIRECTIVE.sub("", text)):
        return "only directives, numbers and punctuation"
    return None


def _sources(root):
    for folder, _dirs, files in os.walk(root):
        for name in files:
            if name.endswith(".cpp"):
                yield os.path.join(folder, name)


def _source_lines(path):
    with open(path, "rb") as source:
        for number, line in enumerate(decode(source.read()).splitlines(), 1):
            if not line.lstrip().startswith("//"):
                yield number, line


def load_layouts():
    """{relative path under Window/, lowercase: (where it came from, wndlayout.Layout)}"""
    import wndlayout
    layouts = {}
    for archive in LAYOUT_ARCHIVES:
        if not os.path.exists(archive):
            continue
        with open(archive, "rb") as source:
            for member, offset, size in list(bigfile._read_index(source)):
                path = member.replace("\\", "/").lower()
                if path.endswith(".wnd") and path.startswith("window/"):
                    source.seek(offset)
                    layouts[path[len("window/"):]] = (os.path.basename(archive),
                                                     wndlayout.parse(source.read(size).decode("latin-1")))
    for root in LAYOUT_DIRS:
        for folder, _dirs, files in os.walk(root):
            for name in files:
                if name.lower().endswith(".wnd"):
                    path = os.path.relpath(os.path.join(folder, name), root).replace("\\", "/").lower()
                    layouts[path] = (os.path.basename(os.path.dirname(root)) + "/" + os.path.basename(root),
                                     wndlayout.load(os.path.join(folder, name)))
    return layouts


def _statement(window, key):
    value = window.prop(key)
    return None if value is None else value.split("=", 1)[1].strip().rstrip(";").strip()


def layout_strings(layouts):
    """(key, 'label' | 'text' | 'tooltip', where, layout path) for every key a layout names."""
    for path, (_origin, layout) in sorted(layouts.items()):
        for window in layout.root.walk():
            kind = (_statement(window, "WINDOWTYPE") or "").upper()
            where = "%s %s" % (path, (window.name or "").split(":")[-1])
            tip = re.match(r'"([A-Za-z]+:[^"\s]+)"', _statement(window, "TOOLTIPTEXT") or "")
            if tip:
                yield tip.group(1), "tooltip", where, path
            text = re.match(r'"([A-Za-z]+:[^"\s]+)"', _statement(window, "TEXT") or "")
            if not text:
                continue
            label = kind in LABEL_CONTROLS
            if kind == "STATICTEXT":
                size = re.search(r"SIZE:\s*(\d+)", _statement(window, "FONT") or "")
                left, top, right, bottom = window.rect[:4]
                label = bool(size) and bottom - top < 2 * int(size.group(1))
            yield text.group(1), "label" if label else "text", "%s %s" % (where, kind.lower()), path


def label_class(layouts):
    """{key: [where it is drawn]} for every label, and [(name, version, where)] for composed ones."""
    found = {}

    def add(key, where):
        found.setdefault(key, [])
        if where not in found[key]:
            found[key].append(where)

    for key, kind, where, _path in layout_strings(layouts):
        if kind == "label":
            add(key, where)
    composed = []
    for path in _sources(SOURCES):
        short = os.path.relpath(path, SOURCES).replace("\\", "/")
        for number, line in _source_lines(path):
            setter = LABEL_SETTER.search(line)
            if setter:
                for key in FETCH_LITERAL.findall(setter.group(0)) + (
                        KEY_LITERAL.findall(setter.group(0)) if "WOLMakeButtonLike" in setter.group(0) else []):
                    add(key, "%s:%d" % (short, number))
            for name, version in COMPOSED.findall(line):
                composed.append((name, version, "%s:%d" % (short, number)))
    with open(CATALOG, "rb") as source:
        for control, key in CATALOG_ROW.findall(decode(source.read())):
            add(key, "OptionsCatalog %s" % control)
            add(key + "#", "OptionsCatalog %s entries" % control)
    for where, keys in LABEL_TABLES:
        for key in keys:
            add(key, where)
    return found, composed


def settings_keys(layouts, english):
    """{key: (label | text | tooltip, where)} for every string the settings screens show."""
    found = {}
    for key, kind, where, path in layout_strings(layouts):
        if path in SETTINGS_LAYOUTS:
            found.setdefault(key, (kind, where))
    for name in SETTINGS_SOURCES:
        for folder, _dirs, files in os.walk(SOURCES):
            if name in files:
                path = os.path.join(folder, name)
                for number, line in _source_lines(path):
                    for key in KEY_LITERAL.findall(line):
                        if key.lower() in english:
                            kind = "tooltip" if key.upper().startswith("TOOLTIP:") else "label"
                            found.setdefault(key, (kind, "%s:%d" % (name, number)))
    with open(CATALOG, "rb") as source:
        for control, key in CATALOG_ROW.findall(decode(source.read())):
            found[key] = ("label", "OptionsCatalog %s" % control)
            for label in english:
                if re.match(re.escape(key.lower()) + r"\d+$", label):
                    found.setdefault(english[label][0], ("label", "OptionsCatalog %s entry" % control))
    return found


def expand_class(found, english):
    """The catalog's "KEY#" stands for its combo entries, KEY0 to KEYn."""
    out = {}
    for key, where in found.items():
        if key.endswith("#"):
            stem = key[:-1].lower()
            for label in english:
                if re.match(re.escape(stem) + r"\d+$", label):
                    out.setdefault(english[label][0], []).extend(where)
        else:
            out.setdefault(key, []).extend(where)
    return out


class TextSet(object):
    """Everything label work needs: the English (CSF and Patch.str), and each master."""

    def __init__(self):
        self.csf = {}
        if os.path.exists(ENGLISH_ARCHIVE):
            self.csf = dict((label.lower(), (label, text)) for label, text in read_english(ENGLISH_ARCHIVE))
        self.patch = dict((label.lower(), (label, text)) for label, text in parse_str(PATCH_STRINGS))
        self.english = dict(self.csf)
        self.english.update(self.patch)
        self.languages = [("English", self.patch)] + [
            (language, dict((label.lower(), (label, text)) for label, text in parse_str(master)))
            for language, master in MASTERS if os.path.exists(master)]
        self.layouts = load_layouts()
        found, self.composed = label_class(self.layouts)
        self.labels = expand_class(found, self.english)
        # the first place each key is named, for the inventory's "where drawn"
        self.sites = {}
        for key, kind, where, _path in layout_strings(self.layouts):
            self.sites.setdefault(key.lower(), "%s (%s)" % (where, kind))
        for path in _sources(SOURCES):
            short = os.path.relpath(path, SOURCES).replace("\\", "/")
            for number, line in _source_lines(path):
                for key in KEY_LITERAL.findall(line):
                    self.sites.setdefault(key.lower(), "%s:%d" % (short, number))
        ini_key = re.compile(r"=\s*([A-Za-z]+:[A-Za-z0-9_]+)")
        for folder, _dirs, files in os.walk(os.path.join(_CODE, "Data", "INI")):
            for name in files:
                if name.lower().endswith(".ini"):
                    for number, line in _source_lines(os.path.join(folder, name)):
                        for key in ini_key.findall(line.split(";")[0]):
                            self.sites.setdefault(key.lower(), "INI %s:%d" % (name, number))

    def english_text(self, key):
        return self.english.get(key.lower(), (key, ""))[1]


def over_limit(texts, language, strings):
    """[(key, text, length, where, english)] for every label this language supplies over the limit."""
    over = []
    for key, where in sorted(texts.labels.items(), key=lambda item: item[0].lower()):
        entry = strings.get(key.lower())
        if entry is None or len(entry[1]) <= LABEL_LIMIT:
            continue
        over.append((entry[0], entry[1], len(entry[1]), "; ".join(where), texts.english_text(key)))
    composed = {}
    for name, version, where in texts.composed:
        composed.setdefault((name, version), []).append(where)
    for (name, version), wheres in sorted(composed.items()):
        where = "; ".join(wheres)
        if name.lower() not in strings and version.lower() not in strings:
            continue
        # a mode name kept English with its version tag is the English list's to cut, not a writer's
        if language != "English" and all(keep_reason(key, texts.english_text(key)) for key in (name, version)):
            continue

        def pick(key):
            return strings.get(key.lower(), texts.english.get(key.lower(), (key, "")))[1]

        text = pick(name) + (" " + pick(version) if pick(version) else "")
        if len(text) > LABEL_LIMIT:
            english = texts.english_text(name) + " " + texts.english_text(version)
            over.append(("%s+%s" % (name, version), text, len(text), "%s (ModeNameWithVersion)" % where,
                         english.strip()))
    return over


def missing_strings(texts, strings, settings):
    """[(key, english, class, where, keep reason)] for every fork string this language leaves English,
    settings first, then by key prefix."""
    rows = []
    candidates = dict(texts.patch)
    for key in settings:
        if key.lower() in texts.english:
            candidates.setdefault(key.lower(), texts.english[key.lower()])
    for lower, (key, english) in candidates.items():
        if lower in strings or not worth_translating(key, english):
            continue
        kind, where = settings.get(key, (None, ""))
        if key in texts.labels:
            kind, where = "label", where or "; ".join(texts.labels[key])
        where = where or texts.sites.get(lower, "not found in layouts, sources or INI")
        rows.append((0 if key in settings else 1, key.split(":")[0].upper(), key, english,
                     "label" if kind == "label" else "free text", where, keep_reason(key, english)))
    rows.sort(key=lambda row: (row[0], row[1], row[2].lower()))
    return rows


def label_problems(texts, languages=None, english=False):
    problems = []
    for language, strings in texts.languages:
        if language == "English" and not english:
            continue
        if languages and language.lower() not in languages and language != "English":
            continue
        for key, text, length, where, _english in over_limit(texts, language, strings):
            problems.append("%s: %s is %d characters, the limit is %d: \"%s\" (%s)"
                            % (language, key, length, LABEL_LIMIT, text, where))
    return problems


def write_inventory(texts, folder):
    os.makedirs(folder, exist_ok=True)
    settings = settings_keys(texts.layouts, texts.english)
    lines = ["Labels over %d characters, and fork strings still English, per language." % LABEL_LIMIT,
             "Written by: python Tools/gametext.py labels --inventory <this folder>",
             "Label class: %d keys (layouts, options catalog, code setters and LABEL_TABLES), %d composed sites."
             % (len(texts.labels), len(texts.composed)),
             "Settings screens show %d keys." % len(settings), ""]
    lines.append("%-10s %8s %16s %15s %8s %12s" % ("language", "over15", "missing settings", "missing others",
                                                   "keeps", "still English (all CSF+Patch, keeps skipped)"))
    for language, strings in texts.languages:
        over = over_limit(texts, language, strings)
        write_rows(os.path.join(folder, "over15_%s.txt" % language.lower()),
                   "%s labels over %d characters: %d" % (language, LABEL_LIMIT, len(over)),
                   "key | text | length | where drawn | English",
                   sorted(over, key=lambda row: -row[2]))
        if language == "English":
            lines.append("%-10s %8d %16s %15s %8s %12s" % (language, len(over), "-", "-", "-", "-"))
            continue
        missing = missing_strings(texts, strings, settings)
        keeps = [row for row in missing if row[6]]
        work = [row for row in missing if not row[6]]
        still = [label for label, (key, text) in texts.english.items()
                 if worth_translating(key, text) and label not in strings and not keep_reason(key, text)]
        write_rows(os.path.join(folder, "missing_%s.txt" % language.lower()),
                   "%s: %d fork strings to translate (%d on the settings screens), %d kept English on purpose."
                   " Labels must fit %d characters; free text has no limit.  Edit Code/Data/%s/Generals.str only."
                   % (language, len(work), sum(1 for row in work if row[0] == 0), len(keeps), LABEL_LIMIT, language),
                   "area | key | English | class | where drawn\n"
                   "area is 'settings' for the Options screens, else the key's prefix; 'KEEP: <why>' rows stay"
                   " English on purpose (gametext.py KEEP_ENGLISH and keep_reason) and need no line.\n"
                   "A new line goes into the language file as: KEY, the string in double quotes, END."
                   "  \\n here is a line break; keep every %d, %s, %ls and %% of the English, in order.",
                   [("settings" if row[0] == 0 else row[1], row[2], row[3], row[4], row[5]) for row in work]
                   + [("KEEP: " + row[6], row[2], row[3], row[4], row[5]) for row in keeps])
        write_rows(os.path.join(folder, "missing_settings_%s.txt" % language.lower()),
                   "%s: %d settings strings still English" % (language, sum(1 for row in work if row[0] == 0)),
                   "key | English | class | where drawn",
                   [(row[2], row[3], row[4], row[5]) for row in work if row[0] == 0])
        lines.append("%-10s %8d %16d %15d %8d %12d" % (
            language, len(over), sum(1 for row in work if row[0] == 0),
            sum(1 for row in work if row[0] == 1), len(keeps), len(still)))
    lines += ["", "English over %d is reported, never failed: the owner decides whether English names are cut."
              % LABEL_LIMIT, "", "Composed labels (name plus version tag, measured joined):"]
    lines += ["  %s + %s  %s" % composed for composed in texts.composed]
    with open(os.path.join(folder, "INVENTORY.txt"), "w", encoding="utf-8", newline="\r\n") as out:
        out.write("\n".join(lines) + "\n")


def write_rows(path, title, columns, rows):
    with open(path, "w", encoding="utf-8", newline="\r\n") as out:
        out.write("%s\n%s\n\n" % (title, columns))
        for row in rows:
            out.write(" | ".join(escape(str(cell)) for cell in row) + "\n")


def labels(arguments):
    texts = TextSet()
    if arguments.inventory:
        write_inventory(texts, arguments.inventory)
        print("inventory in %s" % arguments.inventory)
    chosen = [language.lower() for language in arguments.language]
    problems = label_problems(texts, chosen, arguments.english)
    for problem in problems:
        print(problem)
    english = len(over_limit(texts, "English", texts.patch))
    print("labels: %d keys in the class, %d over %d characters%s; English has %d over (reported, never failed)"
          % (len(texts.labels), len([p for p in problems if not p.startswith("English")]), LABEL_LIMIT,
             " in " + ", ".join(arguments.language) if chosen else "", english))
    return 1 if any(not p.startswith("English") for p in problems) else 0


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
        wanted = [label for label, text in english
                  if worth_translating(label, text) and not keep_reason(label, text)]
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
    percent = [("GUI:Load", "Loading %d%%")]
    if check_translation([("GUI:Load", "Yükleniyor %%%d")], percent):
        problems.append("check refused a percent sign moved before the number")
    if not check_translation([("GUI:Load", "Yükleniyor %d")], percent):
        problems.append("check let a percent sign go missing")

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

    texts = TextSet()
    if "GUI:Accept" not in texts.labels and texts.layouts:
        problems.append("the label class lost the buttons of the layouts")
    over = label_problems(texts)
    print("gametext: %d labels over %d characters across the translations%s"
          % (len(over), LABEL_LIMIT, "" if LABEL_FAILS else " (reported; LABEL_FAILS makes them fail)"))
    if LABEL_FAILS:
        problems.extend(over)

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

    labels_command = commands.add_parser("labels")
    labels_command.add_argument("--language", action="append", default=[])
    labels_command.add_argument("--english", action="store_true")
    labels_command.add_argument("--inventory")
    labels_command.set_defaults(run=labels)

    commands.add_parser("selfcheck").set_defaults(run=selfcheck)

    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    arguments = parser.parse_args(argv[1:])
    return arguments.run(arguments)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
