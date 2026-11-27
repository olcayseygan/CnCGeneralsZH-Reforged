"""Click every control on a shell screen in the running game and say what each one did.

    python ui_walk.py main            the main menu
    python ui_walk.py classic         the skirmish room (-skirmishlobby)
    python ui_walk.py ds              the Direct Strike lobby (Solo Play, Mods, Direct Strike)
    python ui_walk.py lan             the LAN lobby (-lanlobby)
    python ui_walk.py directconnect   Direct Connect (the LAN lobby's DIRECT CONNECT)
    python ui_walk.py options         the options screen (-optionsmenu)
    python ui_walk.py online          the online login against a server nobody answers
    python ui_walk.py observer        a match watched as an observer, its HUD
    python ui_walk.py end             the end screen of an observed match left at once
    python ui_walk.py apocalypse      the Apocalypse room (Solo Play, Mods, Apocalypse)
    python ui_walk.py mappicker       the map picker over the skirmish room (Select Map)
    python ui_walk.py factionpicker   the army screen over the skirmish room (seat 1's army box)
    python ui_walk.py ladder          the online ladder, signed in (ZHR_UI_WALK_ONLINE; also skins, card)
    python ui_walk.py hub             the online hub with nothing open over it (ZHR_UI_WALK_ONLINE)
    python ui_walk.py customgames     the online Custom games list and its Host and Join boxes (ZHR_UI_WALK_ONLINE)
    python ui_walk.py all             every screen above, one launch after another (or main,options,...)

    --res 1280x720   --lang turkish   --only <name part>   --rows <n>   --json <file>
    --no-lock (the caller already holds the build lock)   --port <n>   --keep (do not kill)

One game at a time.  Where several sessions share one tree, each launch waits for the build lock: a
directory made with mkdir and removed afterwards, named by ZHR_BUILD_LOCK_DIR or else by the first
line of tools/build-lock.txt in the checkout's parent folder, and never taken while a build.next file
stands beside it.  With neither, the walk runs without a lock.  The game gets a user data folder of
its own with the Reforged picture and is killed by its own PID in a finally.  The screen is reached by switches and the -control socket, never by the Windows pointer.

For each enabled visible control it clicks through the game's own input path (uiclick: the press goes
down the message stream, so a modal box, a captured pointer or an open drop-down takes it as it would
take the player's), waits for the screen to settle and compares the window tree before and after:

    ok           something changed: a window shown or hidden, a value, a screen, a box
    dead         the click reached the control and nothing changed
    blocked      the click would not reach the control: another window takes that pixel
    locks input  afterwards a visible enabled control that answered before no longer gets its clicks,
                 or the pointer stays captured, grabbed or held by an open list
    crashes      the game went away

A combo box is opened and every row picked in turn (the slot set to Open, the army set to China), a list
box is scrolled with the wheel and its first row clicked, a slider clicked at a quarter and three
quarters, an entry clicked and typed into, a check box clicked twice.  A click that reveals new
controls on the same screen (a tab page, a sub-panel) has those walked as well, one level down; a box
that comes up has its buttons pressed, except the ones that commit (Yes, OK, Accept, Start).  Before
every control the screen is put back the way the walk found it: the box closed, Back or Escape, and
when that does not bring it back, a fresh launch and the same path.

Layout lint on every state walked: siblings that overlap, text wider than its box, gaps between
neighbours off the 8 px grid, edges 1 to 4 px apart that read as misaligned.

Prints one table per screen: control, result, what changed; then the layout faults.  Exit code is the
number of controls that were not ok.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from control_client import Control, ControlError  # noqa: E402

SRC = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
RUN = os.path.join(SRC, "GeneralsMD", "Run")


def lock_dir():
    """The shared build lock's directory, or None: the environment first, then the private checkout beside
    this one (tools/build-lock.txt), the way vendor.ps1 finds the release channel."""
    named = os.environ.get("ZHR_BUILD_LOCK_DIR")
    if not named:
        config = os.path.join(os.path.dirname(SRC), "tools", "build-lock.txt")
        if os.path.exists(config):
            with open(config, encoding="utf-8") as f:
                named = f.readline().strip()
    return named or None


LOCK = lock_dir()
NEXT = os.path.join(os.path.dirname(LOCK), "build.next") if LOCK else None
WORK = os.environ.get("ZHR_UI_WALK_DIR", os.path.join(os.environ.get("TEMP", RUN), "zhr-ui-walk"))

INPUT_KINDS = ("button", "check", "radio", "hslider", "vslider", "list", "entry", "combo", "tab")
GADGET_KINDS = INPUT_KINDS + ("static", "progress")
# box buttons that would leave the walk (quit, start a mission, accept a change): not pressed
COMMITS = re.compile(r"(Yes|Ok|OK|Accept|Start|Play|Quit|Exit)$")
# never pressed anywhere: the player card's COMMUNICATOR opens the web profile in the browser
NEVER = re.compile(r"ButtonCommunicator$")
# buttons a restore may press to step back, in this order
BACKS = ("Cancel", "No", "Back", "Close", "Ok", "OK")

# Direct Strike's lobby is drawn over hidden boxes (DirectStrikeLobby.cpp); its parts are points in the
# design's 1920x1080, worked out from GameClient/DirectStrikeLayout.h for the offline room (seat line,
# army and colour lines beside each card's picture, a list under its chip at 40 a row, the Mixed box at the
# caption's right end, the size and speed chips, START, BACK in the header).  A part is a list of clicks;
# the probe is a click whose hidden box must change, to tell that the room still takes clicks afterwards.
DS_PARTS = [
    ("seat 1 line", [(457, 228)]),
    ("seat 1 Open", [(457, 228), (457, 288)]),
    ("seat 1 Closed", [(457, 228), (457, 328)]),
    ("seat 1 Easy key", [(457, 228), (457, 288), (350, 398)]),
    ("seat 2 line", [(1707, 228)]),
    ("seat 2 Open", [(1707, 228), (1707, 288)]),
    ("seat 3 line", [(457, 490)]),
    ("seat 3 Open", [(457, 490), (457, 550)]),
    ("army 1 chip", [(457, 313)]),
    ("army 1 China", [(457, 313), (335, 484)]),
    ("army 2 chip", [(1707, 313)]),
    ("colour 1 chip", [(457, 398)]),
    ("mixed armies", [(1160, 738)]),
    ("team size", [(796, 798)]),
    ("speed", [(1124, 798)]),
    ("back", [(1782, 70)]),
    ("start", [(960, 901)]),
]
DS_PROBE = (1160, 738)

SCREENS = {
    # nodescend: a control whose new controls have a walk of their own (options)
    "main": dict(args=[], nav=[], nodescend=["MainMenu.wnd:ButtonOptions"]),
    "classic": dict(args=["-skirmishlobby"], nav=[]),
    "ds": dict(args=[], nav=["MainMenu.wnd:ButtonSinglePlayer", "MainMenu.wnd:ButtonMods",
                             "MainMenu.wnd:ButtonModeDirectStrike"], parts=DS_PARTS, probe=DS_PROBE),
    "lan": dict(args=["-lanlobby"], nav=[]),
    "directconnect": dict(args=["-lanlobby"], nav=["LanLobbyMenu.wnd:ButtonDirectConnect"]),
    # persist: (page tab, control) pairs set, accepted, and read back once the screen is opened again
    "options": dict(args=["-optionsmenu"], nav=[], reopen="MainMenu.wnd:ButtonOptions",
                    accept="OptionsMenu.wnd:ButtonAccept",
                    persist=[("OptionsMenu.wnd:TabControls", "OptionsMenu.wnd:CheckCameraShake"),
                             ("OptionsMenu.wnd:TabControls", "OptionsMenu.wnd:CheckGridBuild"),
                             ("OptionsMenu.wnd:TabInterface", "OptionsMenu.wnd:ComboBoxHealthBars"),
                             ("OptionsMenu.wnd:TabControls", "OptionsMenu.wnd:SliderScrollSpeed")]),
    # the login against a server nobody answers comes up under a "could not connect" box: OK it first
    "online": dict(args=["-online", "127.0.0.1:1", "-onlinelogin"], nav=["MessageBox.wnd:ButtonOk"]),
    # the observer HUD is drawn: the five keys down the radar's recess (ObserverHudLayout.h, ObsHud_key)
    "observer": dict(args=["-autoskirmish", "2", "-observer", "-seed", "1", "-map",
                           r"Maps\Tournament Desert\Tournament Desert.map"], nav=[], match=True,
                     parts=[("key standings", [(299, 876)]), ("key vision", [(299, 920)]), ("key chat", [(299, 964)]),
                            ("key options", [(299, 1008)]), ("key quit", [(299, 1052)])]),
    # the end screen: an observed match left at once (the socket's leave), as docs/reference/UI-MAP.md's
    # score screen shots reach it
    "end": dict(args=["-autoskirmish", "2", "-observer", "-seed", "1", "-map",
                      r"Maps\Tournament Desert\Tournament Desert.map"], nav=[], match=True, post=["leave"],
                # its drawn tabs on y 158-190 and the graph and build-order toggles (ScoreScreenLayout.h, UI-MAP)
                parts=[("tab summary", [(167, 174)]), ("tab graphs", [(290, 174)]), ("graphs line", [(290, 174), (1680, 174)]),
                       ("graphs share", [(290, 174), (1770, 174)]), ("tab build order", [(453, 174)]),
                       ("build order all", [(453, 174), (1770, 174)])]),
    "apocalypse": dict(args=[], nav=["MainMenu.wnd:ButtonSinglePlayer", "MainMenu.wnd:ButtonMods",
                                     "MainMenu.wnd:ButtonModeApocalypse"]),
    # package 7's map picker from the skirmish room's Select Map: its tabs, sizes and keys are gadgets the
    # walk clicks; the map grid and the random templates are drawn over their list boxes, so a tile is a
    # point (map tile c, r at 132 + 153c, 275 + 194r; template tile t at 117 + 122t, 380, tile 0 Surprise me)
    "mappicker": dict(args=["-skirmishlobby"], nav=["SkirmishGameOptionsMenu.wnd:ButtonSelectMap"],
                      nodescend=["SkirmishMapSelectMenu.wnd:ButtonBack", "SkirmishMapSelectMenu.wnd:ButtonOK"],
                      prefix="SkirmishMapSelectMenu.wnd:",
                      parts=[("map tile 0,0", [(132, 275)]), ("map tile 1,0", [(285, 275)]),
                             ("map tile 0,1", [(132, 469)]),
                             ("random template 0", [(1092, 146), (117, 380)]),
                             ("random template 1", [(1092, 146), (239, 380)])]),
    # the army screen from seat 1's army box, drawn whole: tabs, a side's RANDOM key and first tile, the tick
    # box on Custom, BACK and CONFIRM; once it closes the room must take clicks again (room_check)
    "factionpicker": dict(args=["-skirmishlobby"], nav=["SkirmishGameOptionsMenu.wnd:ComboBoxPlayerTemplate0"],
                          picker="FactionPicker.wnd:FactionPicker", room_check=True, parts_only=True,
                          parts=[("tab random all", [(258, 146)]), ("tab custom", [(662, 146)]),
                                 ("side 1 random key", [(150, 259)]), ("side 1 tile 1", [(328, 275)]),
                                 ("side 2 tile 1", [(328, 469)]),
                                 ("custom tick side 1", [(662, 146), (72, 198)]),
                                 ("back", [(165, 1002)]), ("confirm", [(328, 275), (1589, 1002)])]),
    # the signed-in online pages: "@online" is the switches in ZHR_UI_WALK_ONLINE (or the file it names)
    # that reach a server and sign in, -online host:port -onlineca ca.pem -onlineuser mail -onlinepass pass,
    # as online/online_check.py's Stack makes them for a local site and server; wait names the hub's window
    # the sign-in lands on, a fresh user folder building its map cache first
    "ladder": dict(args=["@online"], wait="WOLWelcomeMenu.wnd:TabLadder", nav=["WOLWelcomeMenu.wnd:TabLadder"]),
    "skins": dict(args=["@online"], wait="WOLWelcomeMenu.wnd:TabSkins", nav=["WOLWelcomeMenu.wnd:TabSkins"]),
    "card": dict(args=["@online"], wait="WOLWelcomeMenu.wnd:ButtonMyInfo", nav=["WOLWelcomeMenu.wnd:ButtonMyInfo"]),
    # the hub itself with nothing open over it: Play vs AI, Quick Match, Custom, My Info, Back and the Team vs AI
    # keys (team size, leave party, PLAY / FIND TEAM / CANCEL) of a client whose hello says teamai:1
    "hub": dict(args=["@online"], wait="WOLWelcomeMenu.wnd:ButtonPlayVsAI", nav=[],
                nodescend=["WOLWelcomeMenu.wnd:ButtonMyInfo"]),
    # the online Custom games list and its Host and Join boxes
    "customgames": dict(args=["@online"], wait="WOLWelcomeMenu.wnd:TabCustom", nav=["WOLWelcomeMenu.wnd:TabCustom"]),
}


def online_switches():
    """The sign-in switches "@online" stands for: ZHR_UI_WALK_ONLINE, or the file it names."""
    value = os.environ.get("ZHR_UI_WALK_ONLINE", "")
    if value and os.path.isfile(value):
        with open(value, encoding="utf-8") as f:
            value = f.read()
    if not value.strip():
        raise Crashed("ZHR_UI_WALK_ONLINE names no server and account")
    return value.split()


class Crashed(RuntimeError):
    pass


# ---------------------------------------------------------------------------------------------------
# the lock and the game
# ---------------------------------------------------------------------------------------------------

class BuildLock(object):
    def __init__(self, tag, use=True):
        self.tag, self.use, self.held = tag, use and LOCK is not None, False

    def __enter__(self):
        if not self.use:
            return self
        said = False
        while True:
            if not os.path.exists(NEXT):
                try:
                    os.mkdir(LOCK)
                    break
                except OSError:
                    pass
            if not said:
                print("waiting for the build lock (%s)" % LOCK, flush=True)
                said = True
            time.sleep(2)
        self.held = True
        with open(os.path.join(LOCK, "owner.txt"), "w") as f:
            f.write("ui_walk %s %s\n" % (self.tag, time.strftime("%Y-%m-%dT%H:%M:%S")))
        return self

    def __exit__(self, *_):
        if self.held:
            shutil.rmtree(LOCK, ignore_errors=True)


class Game(object):
    """One launch: its process, its socket and the path that reached the screen."""

    def __init__(self, screen, opts):
        self.screen, self.opts = screen, opts
        self.spec = SCREENS[screen]
        self.proc, self.ctl, self.launches = None, None, 0

    def start(self):
        self.stop()
        self.launches += 1
        w, h = self.opts.res
        ud = os.path.join(WORK, "%s-%d" % (self.screen, self.launches))
        shutil.rmtree(ud, ignore_errors=True)
        os.makedirs(ud)
        look = os.path.join(SRC, "reforged-look.ini")
        if os.path.exists(look):
            shutil.copy(look, os.path.join(ud, "Options.ini"))
        # an earlier launch's model checksums and map cache: a fresh folder reads every model and map again,
        # minutes on a busy machine
        for earlier in sorted(os.listdir(WORK), key=lambda d: -os.path.getmtime(os.path.join(WORK, d))):
            cache = os.path.join(WORK, earlier, "ModelChecksums.txt")
            if earlier != os.path.basename(ud) and os.path.exists(cache):
                shutil.copy(cache, ud)
                maps = os.path.join(WORK, earlier, "Maps")
                if os.path.isdir(maps):
                    shutil.copytree(maps, os.path.join(ud, "Maps"))
                break
        args = [os.path.join(RUN, "generals.exe"), "-win", "-xres", str(w), "-yres", str(h), "-msaa", "0",
                "-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit", "-control", str(self.opts.port),
                "-language", self.opts.lang, "-logPrefix", "uiwalk_%s_%d_" % (self.screen, self.launches)]
        for arg in self.spec["args"]:
            args += online_switches() if arg == "@online" else [arg]
        env = dict(os.environ, ZH_USER_DATA_DIR=ud, ZH_UNATTENDED="1")
        self.userdir = ud
        self.proc = subprocess.Popen(args, cwd=RUN, env=env)
        t0 = time.time()
        # a first launch after a build, beside a batch on a busy machine, has taken over three minutes
        while time.time() - t0 < 420:
            self.alive()
            try:
                self.ctl = Control(self.opts.port, timeout=60.0)
                self.ctl.send("ping")
                break
            except (OSError, ControlError):
                self.ctl = None
                time.sleep(1.0)
        if self.ctl is None:
            raise Crashed("the socket never answered")
        if self.spec.get("match"):
            while time.time() - t0 < 600:
                if self.send("status").get("inGame"):
                    break
                time.sleep(1.0)
            time.sleep(3.0)
            for line in self.spec.get("post", []):
                self.send(line)
                time.sleep(1.0)
            if self.spec.get("post"):
                settle(self, minimum=3.0)
        else:
            # the socket answers before the shell is up
            while time.time() - t0 < 600:
                t = self.tree()
                if t.get("screens") and t.get("windows"):
                    break
                time.sleep(1.0)
            settle(self, minimum=2.0)
        if self.spec.get("wait"):
            while self.spec["wait"] not in named(self.tree()):
                if time.time() - t0 > 300:
                    raise Crashed("%s never came up" % self.spec["wait"])
                time.sleep(2.0)
            settle(self, minimum=1.0)
        for name in self.spec["nav"]:
            self.click_until(name)
        return settle(self, minimum=1.0)

    def click_until(self, name, tries=20):
        for _ in range(tries):
            reply = self.send("uiclick " + name)
            if reply.get("ok") and name in reply["at"]["chain"]:
                settle(self, minimum=0.8)
                return True
            time.sleep(0.5)
        raise Crashed("could not reach %s on the way to the screen" % name)

    def alive(self):
        if self.proc is not None and self.proc.poll() is not None:
            raise Crashed("the game exited with code %s" % self.proc.returncode)

    def send(self, line):
        self.alive()
        try:
            return self.ctl.send(line)
        except (OSError, ControlError) as e:
            for _ in range(10):		# a game quitting on purpose closes the socket before the process ends
                if self.proc is None or self.proc.poll() is not None:
                    break
                time.sleep(0.5)
            self.alive()
            raise Crashed("the socket dropped: %s" % e)

    def tree(self, hidden=False):
        t = self.send("uitree all" if hidden else "uitree")
        if not t.get("ok"):
            raise Crashed("uitree refused: %s" % t)
        return t

    def stop(self):
        if self.ctl is not None:
            try:
                self.ctl.close()
            except Exception:
                pass
            self.ctl = None
        if self.proc is not None and not self.opts.keep:
            if self.proc.poll() is None:
                self.proc.kill()
                try:
                    self.proc.wait(20)
                except subprocess.TimeoutExpired:
                    pass
            self.proc = None


# ---------------------------------------------------------------------------------------------------
# reading the tree
# ---------------------------------------------------------------------------------------------------

def windows(tree):
    return tree.get("windows", [])


def centre(w):
    x, y, ww, hh = w["r"]
    return x + ww // 2, y + hh // 2


def is_internal(w, by_index):
    """A gadget's own parts (a list's slider, a combo's edit box) are reached through the gadget."""
    p = w["p"]
    while p >= 0:
        parent = by_index[p]
        if parent["t"] in GADGET_KINDS:
            return True
        p = parent["p"]
    return False


def off_screen(w, tree):
    """Parked out of sight (a room's spare seat rows wait at -20000, -20000): neither clicked nor linted."""
    x, y, ww, hh = w["r"]
    dw, dh = tree.get("display") or (1 << 16, 1 << 16)
    return x + ww <= 0 or y + hh <= 0 or x >= dw or y >= dh


def controls(tree, screen_area):
    """The enabled visible controls a player can click, front first."""
    ws = windows(tree)
    by_index = {w["i"]: w for w in ws}
    out, seen = [], set()
    for w in ws:
        if not w["n"] or w.get("h") or w["n"] in seen or is_internal(w, by_index):
            continue
        x, y, ww, hh = w["r"]
        if ww <= 0 or hh <= 0 or off_screen(w, tree):
            continue
        own_input = w["t"] == "window" and w.get("in") and ww * hh < screen_area // 4
        if w["t"] in INPUT_KINDS or own_input:
            seen.add(w["n"])
            out.append(w)
    return out


def named(tree):
    return {w["n"]: w for w in windows(tree) if w["n"] and not w.get("h")}


def signature(tree):
    return (tree.get("screen"), tree.get("modal"), tree.get("lone"),
            tuple(sorted((w["n"], tuple(w["r"])) for w in windows(tree) if w["n"] and w["t"] in INPUT_KINDS)))


def settle(game, minimum=0.35, timeout=10.0):
    """The tree once the menu has stopped moving: no animation, two reads alike."""
    time.sleep(minimum)
    t0, last, tree = time.time(), None, None
    while time.time() - t0 < timeout:
        tree = game.tree()
        sig = signature(tree)
        if not tree.get("anim") and sig == last:
            return tree
        last = sig
        time.sleep(0.2)
    return tree


def diff(before, after):
    """What changed, in words, between two trees."""
    out = []
    for key in ("screen", "modal", "lone"):
        if before.get(key) != after.get(key):
            out.append("%s %s" % (key, after.get(key) or "-"))
    b, a = named(before), named(after)
    shown = [n for n in a if n not in b]
    hidden = [n for n in b if n not in a]
    if shown:
        out.append("shown %d (%s)" % (len(shown), short(shown)))
    if hidden:
        out.append("hidden %d (%s)" % (len(hidden), short(hidden)))
    changed = []
    for n, w in a.items():
        old = b.get(n)
        if old is None:
            continue
        for key in ("v", "sc", "tx", "e", "rows"):
            if old.get(key) != w.get(key):
                changed.append("%s.%s" % (leaf(n), key))
    if changed:
        out.append("changed " + short(changed))
    return out


def hidden_diff(before, after):
    """Changes to hidden boxes, which a drawn screen works in place of its own parts."""
    b = {w["n"]: w for w in windows(before) if w["n"]}
    out = []
    for w in windows(after):
        old = b.get(w["n"])
        if w["n"] and old is not None:
            for key in ("v", "tx", "e", "rows"):
                if old.get(key) != w.get(key):
                    out.append("%s.%s" % (leaf(w["n"]), key))
    return out


def leaf(name):
    return name.split(":", 1)[-1]


def short(names, keep=4):
    names = [leaf(n) for n in names]
    return ", ".join(names[:keep]) + (" +%d" % (len(names) - keep) if len(names) > keep else "")


def unreachable(game, tree, screen_area):
    """{control: what takes its centre instead} for every control a click at its centre misses."""
    ctls = [w for w in controls(tree, screen_area) if w["e"]]
    out = {}
    for start in range(0, len(ctls), 300):
        chunk = ctls[start:start + 300]
        line = "uihit " + " ".join("%d %d" % centre(w) for w in chunk)
        reply = game.send(line)
        for w, hit in zip(chunk, reply.get("hits", [])):
            if w["n"] not in hit["chain"]:
                # a chain of one unnamed window is a box built by winCreate(NULL), the army picker's kind;
                # an empty chain is a click that reaches no window at all
                out[w["n"]] = hit["hit"] or (UNNAMED if hit["chain"] else "(nothing)")
    return out


UNNAMED = "(unnamed window)"


def owners(tree):
    held = []
    for key in ("captor", "grab"):
        if tree.get(key):
            held.append("%s %s" % (key, leaf(tree[key])))
    return held


# ---------------------------------------------------------------------------------------------------
# layout lint
# ---------------------------------------------------------------------------------------------------

def lint(tree):
    ws = [w for w in windows(tree) if not w.get("h")]
    by_index = {w["i"]: w for w in ws}
    faults = set()
    items = [w for w in ws if w["n"] and w["t"] in GADGET_KINDS and not is_internal(w, by_index)
             and not off_screen(w, tree)]
    for w in items:
        x, y, ww, hh = w["r"]
        tw, wh = w.get("tw", 0), w.get("wh", 0)
        if w["t"] == "button" and tw > ww:
            faults.add("text wider than box: %s (%d > %d)" % (leaf(w["n"]), tw, ww))
        elif w["t"] in ("check", "radio") and tw + hh > ww + 2:
            faults.add("text wider than box: %s (%d+%d box > %d)" % (leaf(w["n"]), tw, hh, ww))
        elif w["t"] == "static" and wh > hh + 2 and tw > ww:
            faults.add("text taller than box: %s (%d > %d)" % (leaf(w["n"]), wh, hh))
    siblings = {}
    for w in items:
        siblings.setdefault(w["p"], []).append(w)
    for group in siblings.values():
        for i, a in enumerate(group):
            ax, ay, aw, ah = a["r"]
            for b in group[i + 1:]:
                bx, by, bw, bh = b["r"]
                ix = min(ax + aw, bx + bw) - max(ax, bx)
                iy = min(ay + ah, by + bh) - max(ay, by)
                contains = (ax <= bx and ay <= by and ax + aw >= bx + bw and ay + ah >= by + bh) or \
                           (bx <= ax and by <= ay and bx + bw >= ax + aw and by + bh >= ay + ah)
                both_input = a["t"] in INPUT_KINDS and b["t"] in INPUT_KINDS
                if ix > 1 and iy > 1 and (not contains or both_input):
                    faults.add("overlap: %s / %s (%dx%d)" % (leaf(a["n"]), leaf(b["n"]), ix, iy))
                for edge, va, vb in (("left", ax, bx), ("right", ax + aw, bx + bw),
                                     ("top", ay, by), ("bottom", ay + ah, by + bh)):
                    if 1 <= abs(va - vb) <= 4:
                        # only neighbours: the other axis must line up or touch
                        near = (iy > -24) if edge in ("left", "right") else (ix > -24)
                        if edge in ("left", "right") and abs(ay - by) > 3 * max(ah, bh):
                            near = False
                        if edge in ("top", "bottom") and abs(ax - bx) > 3 * max(aw, bw):
                            near = False
                        if near or edge in ("left", "right") and abs(ax - bx) <= 4 or edge in ("top", "bottom") and abs(ay - by) <= 4:
                            faults.add("%s edges %d px apart: %s / %s" % (edge, abs(va - vb), leaf(a["n"]), leaf(b["n"])))
        # gaps between neighbours in a row or a column
        for axis in (0, 1):
            lo, hi = (0, 2) if axis == 0 else (1, 3)
            o_lo, o_hi = (1, 3) if axis == 0 else (0, 2)
            ordered = sorted(group, key=lambda w: w["r"][lo])
            for i, a in enumerate(ordered):
                best = None
                for b in ordered[i + 1:]:
                    overlap = min(a["r"][o_lo] + a["r"][o_hi], b["r"][o_lo] + b["r"][o_hi]) - max(a["r"][o_lo], b["r"][o_lo])
                    if overlap <= 0:
                        continue
                    gap = b["r"][lo] - (a["r"][lo] + a["r"][hi])
                    if gap > 0 and (best is None or gap < best[0]):
                        best = (gap, b)
                if best and best[0] <= 64 and best[0] % 8:
                    faults.add("gap %d px off the 8 grid: %s %s %s" % (best[0], leaf(a["n"]), "|" if axis == 0 else "/",
                                                                     leaf(best[1]["n"])))
    return sorted(faults)


# ---------------------------------------------------------------------------------------------------
# the walk
# ---------------------------------------------------------------------------------------------------

class Walk(object):
    def __init__(self, screen, opts):
        self.screen, self.opts = screen, opts
        self.game = Game(screen, opts)
        self.rows, self.faults, self.boxes = [], set(), set()
        self.done = set()
        self.root = None
        self.area = opts.res[0] * opts.res[1]

    # -- getting back to a state -----------------------------------------------------------------

    def launch(self):
        self.root = self.game.start()
        return self.root

    def back_one(self, tree):
        """One step towards the root: close a box, press a Back, or Escape."""
        n = named(tree)
        ctls = [w for w in controls(tree, self.area) if w["t"] == "button"]
        modal = tree.get("modal")
        pool = [w for w in ctls if modal and w["n"].split(":")[0] == modal.split(":")[0]] or ctls
        for word in BACKS:
            for w in pool:
                if leaf(w["n"]).endswith(word) or leaf(w["n"]).endswith("Button" + word):
                    self.game.send("uiclick " + w["n"])
                    return True
        if tree.get("lone"):
            # a second click on an open combo box folds its list away
            self.game.send("uiclick " + tree["lone"])
            return True
        self.game.send("key KEY_ESC")
        return bool(n)

    def reach(self, path, target=None):
        """The state at the end of path, with target visible and enabled; launches again if it must."""
        root_screen = self.root.get("screen") if self.root else None
        for attempt in range(3):
            tree = self.game.tree() if self.game.ctl else None
            if tree and self.ready(tree, path, target):
                return tree
            if tree and attempt == 0:
                # back towards the root, then the path again
                for _ in range(4):
                    if tree.get("screen") == root_screen and not tree.get("modal") and not tree.get("lone") \
                            and self.ready(tree, [], path[0] if path else target):
                        break
                    self.back_one(tree)
                    tree = settle(self.game, minimum=0.8)
            else:
                tree = self.launch()
            ok = True
            for step in path:
                if step not in named(tree):
                    ok = False
                    break
                self.game.send("uiclick " + step)
                tree = settle(self.game, minimum=0.8)
            if ok and self.ready(tree, path, target):
                return tree
        raise Crashed("could not get back to %s" % (" > ".join(leaf(p) for p in path) or "the screen"))

    def ready(self, tree, path, target):
        if self.root is None or tree.get("lone") or tree.get("screen") is None:
            return False
        modal = tree.get("modal") or ""
        picker = self.game.spec.get("picker")
        if picker and picker not in named(tree):		# a picker's screen is the picker, not the room under it
            return False
        if target is None:
            return not modal or modal == (self.root.get("modal") or "")
        w = named(tree).get(target)
        if w is None or w["e"] != 1:
            return False
        # a box up is right only when the target is one of its buttons
        return not modal or modal == (self.root.get("modal") or "") or target.split(":")[0] == modal.split(":")[0]

    # -- one control -----------------------------------------------------------------------------

    def record(self, label, result, change, path):
        where = " > ".join(leaf(p) for p in path)
        self.rows.append(dict(control=label, result=result, change=change, path=where))
        print("  %-44s %-12s %s" % (label[:44], result, change[:120]), flush=True)

    def scroll_into_view(self, tree, name):
        """A row below a scrolling page's fold (options' Controls): turn the wheel over the page until a click
        at the row's centre reaches it, six notches a turn, eight turns at most."""
        ws = windows(tree)
        by_index = {w["i"]: w for w in ws}
        w = named(tree).get(name)
        page = None
        while w is not None and w["p"] >= 0:
            w = by_index.get(w["p"])
            if w is not None and w["n"] and leaf(w["n"]).startswith("Page"):
                page = w["n"]
                break
        if page is None:
            return False
        for _ in range(8):
            target = named(self.game.tree()).get(name)
            if target is None:
                return False
            x, y = centre(target)
            hit = self.game.send("uihit %d %d" % (x, y))["hits"][0]
            if name in hit["chain"]:
                return True
            dh = (tree.get("display") or [0, 1080])[1]
            self.game.send("uiscroll %s %d" % (page, -6 if y > dh // 2 else 6))
            settle(self.game, minimum=0.3)
        return False

    def act(self, path, w):
        """Click one control (and its rows, its wheel, its text) in the state at the end of path."""
        name, kind = w["n"], w["t"]
        tree = self.reach(path, name)
        before_bad = unreachable(self.game, tree, self.area)
        if name in before_bad and self.scroll_into_view(tree, name):
            tree = settle(self.game)
            before_bad = unreachable(self.game, tree, self.area)
        if name in before_bad:
            self.record(leaf(name), "blocked", "covered by %s" % leaf(before_bad[name]), path)
            return None
        cur = named(tree)[name]

        if kind == "combo":
            return self.act_combo(path, cur, tree, before_bad)
        if kind == "list":
            self.act_list(path, cur, tree, before_bad)
            return None
        if kind in ("hslider", "vslider"):
            x, y, ww, hh = cur["r"]
            changes = []
            for frac in (0.25, 0.75):
                dx, dy = (int(ww * frac), hh // 2) if kind == "hslider" else (ww // 2, int(hh * frac))
                self.game.send("uiclick %s %d %d" % (name, dx, dy))
                after = settle(self.game)
                changes += diff(tree, after)
                tree = after
            self.judge(leaf(name), path, tree, changes, before_bad)
            return None
        if kind == "entry":
            self.game.send("uiclick " + name)
            start = named(settle(self.game, minimum=0.3)).get(name, {}).get("tx", "")
            # characters off first: a box at or near its length (a 12 letter name) takes nothing more, so a
            # long text goes down to four
            for _ in range(max(1, len(start) - 4) if len(start) >= 11 else 1):
                self.game.send("key KEY_BACKSPACE")
            tree = settle(self.game, minimum=0.3)
            cut = named(tree).get(name, {}).get("tx", "")
            typed = self.game.send("uitype 12")		# digits: a port box takes nothing else
            after = settle(self.game)
            change = diff(tree, after)
            # the text at each step, for a box that refuses: "start" (length), after Backspace, after "12";
            # added to the row once judged, so it does not count as a change itself
            steps = "text %r (%d) -> %r -> %r" % (start, len(start), cut, named(after).get(name, {}).get("tx", ""))
            if not typed.get("ok"):
                change.append("no keyboard: " + typed.get("error", ""))
            elif leaf(typed.get("to", "")) != leaf(name):
                change.append("keys went to %s" % (leaf(typed.get("to", "")) or "nobody"))
            # judged before the two characters come off: the hit test is live, and a key the text
            # enabled (Save Replay's SAVE) would be disabled again under the tree that lists it
            self.judge(leaf(name), path, after, change, before_bad)
            self.rows[-1]["change"] = "; ".join(c for c in (self.rows[-1]["change"], steps) if c)
            print("    %s" % steps, flush=True)
            for _ in range(2):
                self.game.send("key KEY_BACKSPACE")
            return None

        if leaf(name).startswith("Fader"):
            # a fader is dragged: down its middle from a quarter to three quarters and back up
            # judged by the hidden slider behind it too: its readout is a static the tree cannot read
            x, y, ww, hh = cur["r"]
            hb = self.game.tree(hidden=True)
            self.game.send("uidrag %s %d %d %d %d" % (name, ww // 2, hh // 4, ww // 2, hh * 3 // 4))
            mid = settle(self.game)
            hm = self.game.tree(hidden=True)
            change = diff(tree, mid) + hidden_diff(hb, hm)
            self.game.send("uidrag %s %d %d %d %d" % (name, ww // 2, hh * 3 // 4, ww // 2, hh // 4))
            after = settle(self.game)
            change += diff(mid, after) + hidden_diff(hm, self.game.tree(hidden=True))
            self.judge(leaf(name) + " (drag)", path, after, change, before_bad)
            return None

        self.game.send("uiclick " + name)
        after = settle(self.game)
        change = diff(tree, after)
        if self.game.spec.get("match") is None and not after.get("screen") and tree.get("screen"):
            change.append("left the shell")
        result = self.judge(leaf(name), path, after, change, before_bad)
        if kind == "check" and result != "crashes":
            self.game.send("uiclick " + name)
            settle(self.game)
        return (tree, after, change)

    def judge(self, label, path, after, change, before_bad):
        held = owners(after)
        bad = unreachable(self.game, after, self.area) if not after.get("lone") else {}
        new_bad = {n: by for n, by in bad.items() if n not in before_bad}
        if new_bad and set(new_bad.values()) == {UNNAMED}:
            # an unnamed full-screen box (the army picker): it should take the clicks, and Escape close it
            self.game.send("key KEY_ESC")
            closed = settle(self.game)
            still = {n for n in unreachable(self.game, closed, self.area) if n not in before_bad}
            if still:
                result, change = "locks input", change + ["an unnamed box came up and Escape left %d controls under it" % len(still)]
            else:
                result, change = "ok", change + ["an unnamed box came up over %d controls; Escape closed it" % len(new_bad)]
            self.record(label, result, "; ".join(change), path)
            return result
        if held:
            result, change = "locks input", change + held
        elif new_bad and not after.get("modal"):
            by = sorted(set(new_bad.values()))
            note = "%d controls miss their clicks, taken by %s (%s)" % (len(new_bad), short(by, 2), short(list(new_bad), 3))
            # on a screen of its own the misses are that screen's layout, which its own walk lists
            result = "ok" if any(c.startswith("screen ") for c in change) else "locks input"
            change = change + [note]
        elif change:
            result = "ok"
        else:
            result = "dead"
        self.record(label, result, "; ".join(change), path)
        return result

    def act_combo(self, path, cur, tree, before_bad):
        name = cur["n"]
        self.game.send("uiclick " + name)
        opened = settle(self.game)
        if not opened.get("lone"):
            # an editable box takes the click in its text; its arrow opens the list (no Escape: on a
            # screen of its own that is Back)
            x, y, ww, hh = cur["r"]
            self.game.send("uiclick %s %d %d" % (name, ww - hh // 2, hh // 2))
            opened = settle(self.game)
        if not opened.get("lone"):
            # no list: a box of its own instead (the army picker), or nothing at all
            self.judge(leaf(name) + " (open, no list)", path, opened, diff(tree, opened), before_bad)
            return None
        self.record(leaf(name) + " (open)", "ok", "lone %s" % leaf(opened["lone"]), path)
        rows = cur.get("rows", 0) or 0
        original = cur.get("v", -1)
        for row in range(min(rows, self.opts.rows)):
            if row == original:		# the row already picked changes nothing
                continue
            state = self.reach(path, name)
            label = "%s row %d" % (leaf(name), row)
            if not state.get("lone"):
                self.game.send("uiclick " + name)
                if not settle(self.game, minimum=0.4).get("lone"):
                    # the box just picked from snaps shut on the release that opens it again (its lone
                    # pointer is let go with a GGM_CLOSE): a second click opens it
                    self.record(label, "snaps shut", "reopened right after a pick, the list closed on the release", path)
                    self.game.send("uiclick " + name)
                    settle(self.game, minimum=0.4)
            at = self.game.send("uirow %s %d" % (name, row))
            label = "%s = %s" % (leaf(name), at.get("tx", row) if at.get("ok") else row)
            if not at.get("ok"):
                self.record(label, "blocked", at.get("error", ""), path)
                self.game.send("uiclick " + name)		# fold the list (Escape is Back on some screens)
                settle(self.game, minimum=0.3)
                continue
            hit = self.game.send("uihit %d %d" % (at["x"], at["y"]))["hits"][0]
            if name not in hit["chain"] and not at.get("shown"):
                # uirow's "shown" read every other row of a 1080 list as past its end: the hit test decides
                self.record(label, "n/a", "row below the open list's end, behind its scroll bar", path)
                self.game.send("uiclick " + name)
                settle(self.game, minimum=0.3)
                continue
            if name not in hit["chain"]:
                self.record(label, "blocked", "row covered by %s" % leaf(hit["hit"] or "(nothing)"), path)
                self.game.send("key KEY_ESC")
                settle(self.game, minimum=0.3)
                continue
            self.game.send("uiclickat %d %d" % (at["x"], at["y"]))
            after = settle(self.game)
            change = diff(state, after)
            if after.get("lone"):
                change.append("list stayed open")
            self.judge(label, path, after, change, before_bad)
        if original is not None and original >= 0 and rows:
            # put the first choice back, so the next control sees the room as it was
            state = self.reach(path, name)
            self.game.send("uiclick " + name)
            settle(self.game, minimum=0.4)
            at = self.game.send("uirow %s %d" % (name, original))
            if at.get("ok") and at.get("shown"):
                self.game.send("uiclickat %d %d" % (at["x"], at["y"]))
            else:
                self.game.send("key KEY_ESC")
            settle(self.game)
        return None

    def act_list(self, path, cur, tree, before_bad):
        name = cur["n"]
        if (cur.get("sh") or 0) > (cur.get("dh") or 0) + 8:		# a list that really overflows
            # down first, then up when down does not move it (a list already at its foot cannot go further)
            back = 3
            self.game.send("uiscroll %s -3" % name)
            after = settle(self.game)
            moved = named(after).get(name, {}).get("sc") != cur.get("sc")
            if not moved:
                self.game.send("uiscroll %s 3" % name)
                after = settle(self.game)
                moved = named(after).get(name, {}).get("sc") != cur.get("sc")
                back = -3
            self.judge(leaf(name) + " (wheel)", path, after, diff(tree, after) if moved else [], before_bad)
            if moved:
                self.game.send("uiscroll %s %d" % (name, back))
            tree = settle(self.game)
        else:
            self.record(leaf(name) + " (wheel)", "n/a", "%d rows fit" % (cur.get("rows") or 0), path)
        if cur.get("rows"):
            at = self.game.send("uirow %s 0" % name)
            if at.get("ok") and at.get("shown"):
                self.game.send("uiclickat %d %d" % (at["x"], at["y"]))
                after = settle(self.game)
                self.judge(leaf(name) + " (row 0)", path, after, diff(tree, after), before_bad)

    # -- boxes -------------------------------------------------------------------------------------

    def walk_box(self, path, trigger, tree):
        # read again once the box has finished coming up: the Exit box's NO was missing from the first read
        tree = settle(self.game, minimum=1.0)
        box = tree.get("modal") or ""
        if not box:
            return
        layout = box.split(":")[0]
        key = (layout, tuple(sorted(w.get("tx", "") for w in windows(tree) if w["n"].startswith(layout))))
        if key in self.boxes:
            return
        self.boxes.add(key)
        texts = [w.get("tx", "") for w in windows(tree) if w["n"].startswith(layout) and w["t"] == "static" and w.get("tx")]
        print("  box %s: %s" % (layout, " | ".join(texts)[:120]), flush=True)
        self.faults.update("%s: %s" % (layout, f) for f in lint(tree))
        buttons = [w for w in controls(tree, self.area) if w["n"].startswith(layout + ":")]
        print("    buttons: %s" % (short([b["n"] for b in buttons], 8) or "none"), flush=True)
        for b in buttons:
            if NEVER.search(b["n"]):
                self.record("  [%s] %s" % (layout, leaf(b["n"])), "skipped", "opens the browser", path + [trigger])
                continue
            if COMMITS.search(leaf(b["n"])):
                self.record("  [%s] %s" % (layout, leaf(b["n"])), "skipped", "commits", path + [trigger])
                continue
            state = self.reach(path + [trigger], b["n"])
            self.game.send("uiclick " + b["n"])
            after = settle(self.game)
            change = diff(state, after)
            if after.get("modal") == state.get("modal"):
                change.append("box stayed up")
            self.judge("  [%s] %s" % (layout, leaf(b["n"])), path + [trigger], after, change, {})

    # -- a state -------------------------------------------------------------------------------------

    def fold(self, trigger, revealed):
        """Put away a panel a sub-walk opened (the friends box): its trigger clicked again, then a fresh launch if
        it is still up, so the next control on the root is not walked under it."""
        try:
            if not self.game.ctl or not (revealed & set(named(self.game.tree()))):
                return
            if trigger in named(self.game.tree()):
                self.game.send("uiclick " + trigger)
                if not (revealed & set(named(settle(self.game)))):
                    return
            self.game.stop()
            self.root = None
        except Crashed:
            self.game.stop()
            self.root = None

    def walk(self, path, depth, only=None):
        """The controls of the state at the end of path; below the root only the ones its last click revealed
        (only), so the screen under an open panel is not walked through the panel."""
        tree = self.reach(path)
        self.faults.update(lint(tree))
        base_bad = unreachable(self.game, tree, self.area)
        for n, by in base_bad.items():
            self.faults.add("covered: %s under %s" % (leaf(n), leaf(by)))
        todo = [w for w in controls(tree, self.area) if w["n"] not in self.done
                and (not self.opts.only or self.opts.only.lower() in w["n"].lower())
                and w["n"].startswith(self.game.spec.get("prefix", ""))		# a picker's own windows, not the room's
                and not NEVER.search(w["n"])
                and (only is None or w["n"] in only)]
        for w in todo:
            if w["n"] in self.done:		# a deeper walk got to it first
                continue
            if not w["e"]:		# not done: it may be live back on the root
                continue
            self.done.add(w["n"])
            try:
                outcome = self.act(path, w)
            except Crashed as e:
                # a clean exit is the game quitting on purpose (Exit Game asks nothing)
                self.record(leaf(w["n"]), "quits" if "code 0" in str(e) else "crashes", str(e), path)
                self.game.stop()
                self.root = None
                continue
            if not outcome:
                continue
            before, after, change = outcome
            if after.get("modal") and after.get("modal") != before.get("modal"):
                try:
                    self.walk_box(path, w["n"], after)
                except Crashed as e:
                    self.record("  box after " + leaf(w["n"]), "crashes", str(e), path)
                    self.game.stop()
                    self.root = None
            elif depth < 2 and after.get("screen") == before.get("screen") and not after.get("modal") \
                    and w["n"] not in self.game.spec.get("nodescend", []):
                fresh = [c for c in controls(after, self.area) if c["n"] not in self.done and c["n"] not in named(before)]
                if fresh and (w["t"] == "tab" or leaf(w["n"]).startswith("Tab")):
                    # a tab's page is reached from the root by that tab alone, not through the tab before it, so
                    # eight tabs do not use up the depth on the way
                    self.walk([w["n"]], 1, {c["n"] for c in fresh})
                elif fresh:
                    self.walk(path + [w["n"]], depth + 1, {c["n"] for c in fresh})
                    self.fold(w["n"], {c["n"] for c in fresh})
        disabled = [w for w in controls(tree, self.area) if not w["e"]]
        if disabled and depth == 0 and not path:
            self.record("(disabled)", "-", short([w["n"] for w in disabled], 12), path)

    def walk_parts(self):
        """Direct Strike's drawn parts: clicks at points, judged by its hidden boxes and the picture."""
        k = min(self.opts.res[0] / 1920.0, self.opts.res[1] / 1080.0)
        ox, oy = (self.opts.res[0] - 1920 * k) / 2, (self.opts.res[1] - 1080 * k) / 2
        px = lambda p: (int(ox + p[0] * k), int(oy + p[1] * k))  # noqa: E731
        for label, clicks in self.game.spec["parts"]:
            if self.opts.only and self.opts.only.lower() not in label:
                continue
            try:
                tree = self.reach([])
                hb = self.game.tree(hidden=True)
                for p in clicks:
                    self.game.send("uiclickat %d %d" % px(p))
                    settle(self.game, minimum=0.6)
                after = settle(self.game)
                ha = self.game.tree(hidden=True)
                change = diff(tree, after) + hidden_diff(hb, ha)
                # still takes clicks: the probe's hidden box must answer
                probe_ok = None
                if self.game.spec.get("probe") and after.get("screen") == tree.get("screen") and not after.get("modal"):
                    self.game.send("uiclickat 960 40")
                    settle(self.game, minimum=0.4)
                    p0 = self.game.tree(hidden=True)
                    self.game.send("uiclickat %d %d" % px(self.game.spec["probe"]))
                    p1 = settle(self.game, minimum=0.6)
                    p1 = self.game.tree(hidden=True)
                    probe_ok = bool(hidden_diff(p0, p1))
                    self.game.send("uiclickat %d %d" % px(self.game.spec["probe"]))
                    settle(self.game, minimum=0.4)
                picker = self.game.spec.get("picker")
                if picker and self.game.spec.get("room_check") and picker not in named(after):
                    # the picker closed: the room under it must take its clicks again
                    bad = unreachable(self.game, after, self.area)
                    change.append("picker closed")
                    if bad:
                        probe_ok = False
                        change.append("%d room controls then miss their clicks, taken by %s" % (
                            len(bad), short(sorted(set(bad.values())), 2)))
                if probe_ok is False and picker:
                    result = "locks input"
                elif probe_ok is False and label != "mixed armies":
                    result = "locks input"
                    change.append("the Mixed armies box no longer answers")
                elif change:
                    result = "ok"
                else:
                    # a drawn part changes only the picture: a still after the build says whether it worked
                    result = "dead" if self.game.spec.get("probe") else "drawn, unverified"
                self.record(label, result, "; ".join(change), [])
                # a picker that closed is a state the next part cannot start from: the room under it would take
                # that part's clicks (CONFIRM's spot is the room's START)
                if after.get("screen") != tree.get("screen") or after.get("modal") or (self.game.spec.get("match") and change) \
                        or (picker and picker not in named(after)):
                    self.game.stop()
                    self.root = None
            except Crashed as e:
                self.record(label, "crashes", str(e), [])
                self.game.stop()
                self.root = None

    def change_value(self, name):
        """Move one control off its value: a check clicked, a combo's next row, a slider at a quarter."""
        tree = self.game.tree()
        w = named(tree).get(name)
        if w is None or not w["e"]:
            return None
        if w["t"] == "combo":
            self.game.send("uiclick " + name)
            settle(self.game, minimum=0.4)
            row = 1 if (w.get("v") or 0) == 0 else 0
            at = self.game.send("uirow %s %d" % (name, row))
            if not (at.get("ok") and at.get("shown")):
                self.game.send("key KEY_ESC")
                return None
            self.game.send("uiclickat %d %d" % (at["x"], at["y"]))
        elif w["t"] in ("hslider", "vslider"):
            x, y, ww, hh = w["r"]
            frac = 0.25 if (w.get("v") or 0) > 50 else 0.75
            if w["t"] == "hslider":
                self.game.send("uiclick %s %d %d" % (name, int(ww * frac), hh // 2))
            else:
                self.game.send("uiclick %s %d %d" % (name, ww // 2, int(hh * frac)))
        else:
            self.game.send("uiclick " + name)
        after = settle(self.game)
        return w.get("v"), named(after).get(name, {}).get("v")

    def walk_persist(self):
        """Values set, accepted, and read back once the screen is opened again."""
        spec = self.game.spec
        for tab, name in spec.get("persist", []):
            label = "%s survives %s" % (leaf(name), leaf(spec["accept"]))
            try:
                self.reach([])
                self.game.send("uiclick " + tab)
                settle(self.game)
                values = self.change_value(name)
                if values is None:
                    self.record(label, "blocked", "not on the page or disabled", [leaf(tab)])
                    continue
                old, new = values
                if old == new:
                    self.record(label, "dead", "value stayed %s" % old, [leaf(tab)])
                    continue
                self.game.send("uiclick " + spec["accept"])
                settle(self.game, minimum=1.5)
                self.game.click_until(spec["reopen"])
                self.game.send("uiclick " + tab)
                back = settle(self.game)
                read = named(back).get(name, {}).get("v")
                if read == new:
                    self.record(label, "ok", "%s -> %s, read back %s" % (old, new, read), [leaf(tab)])
                else:
                    self.record(label, "lost", "%s -> %s, read back %s" % (old, new, read), [leaf(tab)])
            except Crashed as e:
                self.record(label, "crashes", str(e), [leaf(tab)])
                self.game.stop()
                self.root = None

    def run(self):
        print("== %s at %dx%d, %s" % (self.screen, self.opts.res[0], self.opts.res[1], self.opts.lang), flush=True)
        try:
            self.launch()
            print("  screen %s" % self.root.get("screen"), flush=True)
            if self.game.spec.get("parts"):
                self.walk_parts()
            if self.game.spec.get("persist"):
                self.walk_persist()
                self.game.stop()
                self.root = None
            if not self.game.spec.get("parts_only"):		# a picker drawn whole has nothing else to walk
                self.walk([], 0)
        except Crashed as e:
            self.record("(screen)", "crashes", str(e), [])
        finally:
            self.game.stop()
        return self.report()

    def report(self):
        lines = ["", "### %s (%dx%d %s, %d launches)" % (self.screen, self.opts.res[0], self.opts.res[1],
                                                         self.opts.lang, self.game.launches), "",
                 "| control | result | what changed |", "| --- | --- | --- |"]
        for r in self.rows:
            label = r["control"] if not r["path"] else "%s > %s" % (r["path"], r["control"])
            lines.append("| %s | %s | %s |" % (label, r["result"], r["change"].replace("|", "/")))
        lines += ["", "Layout faults (%d):" % len(self.faults)]
        lines += ["- " + f for f in sorted(self.faults)]
        text = "\n".join(lines)
        print(text, flush=True)
        return dict(screen=self.screen, rows=self.rows, faults=sorted(self.faults), launches=self.game.launches)


def main():
    # a language list names itself in every script: a console code page that has no such letters must not end the walk
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("screen", help="one of %s, several joined by commas, or all" % ", ".join(sorted(SCREENS)))
    ap.add_argument("--res", default="1920x1080")
    ap.add_argument("--lang", default="english")
    ap.add_argument("--port", type=int, default=8871, help="two walkers on one machine want two ports")
    ap.add_argument("--only", default="")
    ap.add_argument("--rows", type=int, default=5, help="combo rows picked per box (Open, Closed and the AIs fit in 5)")
    ap.add_argument("--json", default="")
    ap.add_argument("--no-lock", action="store_true")
    ap.add_argument("--keep", action="store_true")
    opts = ap.parse_args()
    opts.res = tuple(int(v) for v in opts.res.lower().split("x"))
    screens = sorted(SCREENS) if opts.screen == "all" else opts.screen.split(",")
    unknown = [s for s in screens if s not in SCREENS]
    if unknown:
        ap.error("no screen called %s" % ", ".join(unknown))
    results = []
    for s in screens:
        # the lock for one screen at a time: builds land in between, and the newest exe is the one to walk
        with BuildLock(s, use=not opts.no_lock):
            results.append(Walk(s, opts).run())
        if opts.json:		# after every screen, so a long run that is cut off keeps what it has
            with open(opts.json, "w", encoding="utf-8") as f:
                json.dump(results, f, indent=1)
    return sum(1 for r in results for row in r["rows"] if row["result"] not in ("ok", "n/a", "-", "skipped", "quits"))


if __name__ == "__main__":
    sys.exit(min(main(), 125))
