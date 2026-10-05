#!/usr/bin/env python3
"""Read, edit and write the game's .wnd window layouts.

Format, straight out of GameWindowManagerScript.cpp's parseWindow / parseChildWindows:

    FILE_VERSION = 2;
    STARTLAYOUTBLOCK
      LAYOUTINIT = <callback>;
      LAYOUTUPDATE = <callback>;
      LAYOUTSHUTDOWN = <callback>;
    ENDLAYOUTBLOCK
    WINDOW
      <KEY = value;>...          one property per statement, continued over lines
      CHILD
      WINDOW ... END             a child, repeated
      ENDALLCHILDREN             only when there is at least one child
    END

Every line is CRLF terminated and indented two spaces per level.  A property runs
until the semicolon that closes it, so a multi-line ENABLEDDRAWDATA is one statement.

The point of this module is not to understand a window.  It is to move one from
here to there without touching a single byte of its art: the properties are kept
as the raw text they were read as, and only SCREENRECT and NAME are ever rewritten.
That is what lets the options screen be re-laid-out into tabs while every button
keeps the exact images, colours, fonts and tooltips EA gave it.

    python wndlayout.py tree   <layout.wnd>          names and rectangles, indented
    python wndlayout.py check  <layout.wnd>          parse and re-emit, byte for byte
"""

import re
import sys

_INDENT = "  "


class Window(object):
    """One WINDOW block: its properties as raw lines, and its children."""

    def __init__(self, indent=_INDENT):
        # Property lines exactly as they were read, whitespace and all.  Five of the eighty
        # layouts the game ships were hand-edited at some point and have property lines indented
        # with a tab, or at the wrong depth; keeping the raw text is what lets those files come
        # back out byte for byte instead of being tidied up by a tool nobody asked to tidy them.
        self.props = []
        self.indent = indent   # the indentation this window's properties were read at
        self.children = []
        # Read without a CHILD in front of it.  The game's parser takes CHILD once and then every
        # WINDOW up to ENDALLCHILDREN, so EA's 1.04 MainMenu.wnd gets away with one sibling that
        # skips the keyword; remembering it keeps that file round tripping.
        self.bare = False

    # -- properties -----------------------------------------------------------

    def prop_index(self, key):
        """Index of the line starting the statement for this key, or -1."""
        for i, line in enumerate(self.props):
            if re.match(r"^%s\s*=" % re.escape(key), line.strip()):
                return i
        return -1

    def prop(self, key):
        """The whole statement for this key as one space-joined string, or None."""
        i = self.prop_index(key)
        if i < 0:
            return None
        out = [self.props[i]]
        while ";" not in out[-1]:
            i += 1
            out.append(self.props[i])
        return " ".join(part.strip() for part in out)

    def set_prop(self, key, statement):
        """Replace the whole statement for this key.  statement is one line, no semicolon."""
        i = self.prop_index(key)
        if i < 0:
            raise KeyError(key)
        end = i
        while ";" not in self.props[end]:
            end += 1
        lead = re.match(r"\s*", self.props[i]).group(0)
        self.props[i:end + 1] = ["%s%s = %s;" % (lead, key, statement)]

    def put_prop(self, key, statement):
        """Set this property, adding it if the template being cloned did not carry one.  The
        parser dispatches on the keyword, so where a new statement lands does not matter."""
        if self.prop_index(key) < 0:
            at = max(self.prop_index("NAME"), 0)
            lead = re.match(r"\s*", self.props[at]).group(0)
            self.props.insert(at + 1, "%s%s = ;" % (lead, key))
        self.set_prop(key, statement)

    # -- the two properties this module is allowed to rewrite -----------------

    @property
    def name(self):
        statement = self.prop("NAME")
        if statement is None:
            return None
        return re.search(r'"([^"]*)"', statement).group(1)

    @name.setter
    def name(self, value):
        self.set_prop("NAME", '"%s"' % value)

    @property
    def rect(self):
        """(left, top, right, bottom, creationWidth, creationHeight)."""
        numbers = [int(n) for n in re.findall(r"-?\d+", self.prop("SCREENRECT"))]
        return tuple(numbers)

    @rect.setter
    def rect(self, value):
        left, top, right, bottom, cw, ch = value
        self.set_prop(
            "SCREENRECT",
            "UPPERLEFT: %d %d, BOTTOMRIGHT: %d %d, CREATIONRESOLUTION: %d %d"
            % (left, top, right, bottom, cw, ch))

    def move_to(self, left, top):
        """Put the top left corner here, keeping the size, bringing the children along."""
        l, t = self.rect[:2]
        self.move_by(left - l, top - t)

    def move_by(self, dx, dy):
        """Shift this window and everything inside it.  Child rectangles in a .wnd are absolute
        screen coordinates and only become parent relative when parseScreenRect loads them, so a
        container that moves without its children moves away from them."""
        for node in self.walk():
            l, t, r, b, cw, ch = node.rect
            node.rect = (l + dx, t + dy, r + dx, b + dy, cw, ch)

    def place(self, left, top, width, height):
        cw, ch = self.rect[4:]
        self.rect = (left, top, left + width, top + height, cw, ch)

    # -- the tree -------------------------------------------------------------

    def walk(self):
        yield self
        for child in self.children:
            for node in child.walk():
                yield node

    def find(self, name):
        """The one window with this name, or None.  Matches the part after the colon too."""
        for node in self.walk():
            if node.name == name or (node.name or "").split(":")[-1] == name:
                return node
        return None

    def parent_of(self, target):
        for node in self.walk():
            if target in node.children:
                return node
        return None

    def emit(self, depth, out):
        pad = _INDENT * depth
        out.append(pad + "WINDOW")
        for line in self.props:
            # re-indent only what was indented the way this window was read.  A window that has
            # not moved therefore comes back out unchanged, down to the byte
            if self.indent and line.startswith(self.indent):
                out.append(pad + _INDENT + line[len(self.indent):])
            else:
                out.append(line)
        for child in self.children:
            if not child.bare:
                out.append(pad + _INDENT + "CHILD")
            child.emit(depth + 1, out)
        if self.children:
            out.append(pad + _INDENT + "ENDALLCHILDREN")
        out.append(pad + "END")


class Layout(object):
    """A whole .wnd: the header block and the one root window."""

    def __init__(self):
        self.header = []      # everything up to and including ENDALLLAYOUTBLOCK
        self.root = None

    def find(self, name):
        return self.root.find(name)

    def text(self):
        out = list(self.header)
        self.root.emit(0, out)
        return "\r\n".join(out) + "\r\n"


def parse(text):
    """Parse a .wnd.  Round trips byte for byte on every layout the game ships."""
    lines = text.replace("\r\n", "\n").split("\n")
    if lines and lines[-1] == "":
        lines.pop()

    layout = Layout()
    at = 0
    while lines[at].strip() != "WINDOW":
        layout.header.append(lines[at])
        at += 1

    def parse_window(at, depth):
        assert lines[at].strip() == "WINDOW", lines[at]
        at += 1
        window = Window(_INDENT * (depth + 1))
        while True:
            line = lines[at]
            token = line.strip()
            if token == "CHILD":
                child, at = parse_window(at + 1, depth + 1)
                window.children.append(child)
            elif token == "WINDOW":
                child, at = parse_window(at, depth + 1)
                child.bare = True
                window.children.append(child)
            elif token == "ENDALLCHILDREN":
                at += 1
            elif token == "END":
                return window, at + 1
            else:
                window.props.append(line)
                at += 1

    layout.root, at = parse_window(at, 0)
    return layout


def load(path):
    with open(path, "rb") as fp:
        return parse(fp.read().decode("latin-1"))


def save(layout, path):
    with open(path, "wb") as fp:
        fp.write(layout.text().encode("latin-1"))


def clone(window, name=None):
    """A deep copy, optionally renamed.  How a new control inherits EA's art."""
    copy = Window(window.indent)
    copy.props = list(window.props)
    copy.children = [clone(child) for child in window.children]
    if name is not None:
        copy.name = name
    return copy


def _tree(window, depth=0):
    l, t, r, b = window.rect[:4]
    print("%s%-40s %4d %4d  %3dx%-3d" % (
        "  " * depth, (window.name or "").split(":")[-1] or "(unnamed)",
        l, t, r - l, b - t))
    for child in window.children:
        _tree(child, depth + 1)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2

    command, path = argv[1], argv[2]
    layout = load(path)

    if command == "tree":
        _tree(layout.root)
        return 0

    if command == "check":
        with open(path, "rb") as fp:
            original = fp.read().decode("latin-1")
        produced = layout.text()
        if produced == original:
            print("%s: round trips, %d bytes" % (path, len(original)))
            return 0
        for i, (a, b) in enumerate(zip(original.split("\r\n"), produced.split("\r\n"))):
            if a != b:
                print("%s: first difference at line %d\n  read:    %r\n  written: %r"
                      % (path, i + 1, a, b))
                break
        else:
            print("%s: differs in length, %d read, %d written"
                  % (path, len(original), len(produced)))
        return 1

    print("unknown command %r" % command)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
