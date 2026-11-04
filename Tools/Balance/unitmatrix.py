"""Every unit the build menus offer against every other, one on one, as a self-contained HTML table.

Reads the INIs the way the engine loads them: the retail archives in Run (Patch*.big over the rest,
a loose file under Run over every archive), the fork's masters under Code/Data over both,
BalanceReforged.ini then FixesReforged.ini patched over every Object, Weapon and Armor, and Turkiye's
reskins (TurkiyeReforged.ini) and weapons (TurkiyeWeaponReforged.ini) after them. Each cell
is the AI's own matchup arithmetic (AIPlayer.cpp shotPatternAgainst and computeTemplateFramesToKill,
AI.cpp aiFramesToKill): best weapon of the unconditioned WeaponSet against the target's
unconditioned ArmorSet and body MaxHealth, frames to kill both ways. The page divides the two and
can weigh them by price; the AI's aiMatchupScore is the same ratio on a log scale.

    python unitmatrix.py --out <page.html>
    python unitmatrix.py --selfcheck

The game data comes from Run beside the source; ZHR_RUN_DIR points it at another Run, as siege.py does.
The sides and their units come from that data: the lobby's PlayerTemplates and what their buildings
build.
"""

import argparse
import collections
import copy
import json
import math
import os
import re
import struct
import sys
import time
from pathlib import Path

GENERALS = Path(__file__).resolve().parents[2] / "GeneralsMD"
RUN_DIR = Path(os.environ.get("ZHR_RUN_DIR", GENERALS / "Run"))
CODE_DATA = GENERALS / "Code" / "Data"
sys.path.insert(0, str(GENERALS / "Code" / "Tools"))

import bigfile  # noqa: E402
import gametext  # noqa: E402

LOGIC_FPS = 60  # LOGICFRAMES_PER_SECOND
SORTIE_MSEC = 20000
SORTIE_FRAMES = SORTIE_MSEC * LOGIC_FPS / 1000.0      # AI_SORTIE_FRAMES
OVERKILL_TOLERANCE = 0.0001
CANNOT_KILL = -1.0
# EA's placeholder weapons (the Avenger's air laser, the Battle Bus and Troop Crawler triggers, the Angry
# Mob's) do 0.1 or less so the unit has something to aim with; the smallest real one does 1. The AI
# counts them as a kill millions of frames away, which its 16x clamp already scores as no kill.
DUMMY_DAMAGE = 1.0
WEAPON_SLOTS = ("PRIMARY", "SECONDARY", "TERTIARY")
BODIES_WITH_HEALTH = {"activebody", "structurebody", "hivestructurebody", "undeadbody", "highlanderbody", "immortalbody"}
NOT_HEALTH_DAMAGE = {"STATUS", "SUBDUAL_MISSILE", "SUBDUAL_VEHICLE", "SUBDUAL_BUILDING", "SUBDUAL_UNRESISTABLE",
                     "KILL_PILOT", "KILL_GARRISONED", "HEALING"}
UNRESISTED = {"UNRESISTABLE", "SUBDUAL_UNRESISTABLE"}
MODULE_KEYS = ("body", "behavior", "draw", "clientupdate")
SHOWN_KINDS = ("INFANTRY", "VEHICLE", "AIRCRAFT")
# the Generals Challenge copies (GC_) borrow a general's side but are never built in a skirmish
CHALLENGE_PREFIX = "gc_"
# the sides the game ships, with the page's Turkish label and tag; a side keeps these only while its
# lobby name (SIDE:<side>) is still the one written here
VANILLA_SIDES = {
    "America": ("USA", "ABD", "ABD"),
    "AmericaAirForceGeneral": ("USA Air Force General", "ABD Hava Kuvvetleri", "HVK"),
    "AmericaLaserGeneral": ("USA Laser General", "ABD Lazer", "LZR"),
    "AmericaSuperWeaponGeneral": ("USA Super Weapon General", "ABD Süper Silah", "SSL"),
    "China": ("China", "Çin", "ÇİN"),
    "ChinaTankGeneral": ("China Tank General", "Çin Tank", "TNK"),
    "ChinaInfantryGeneral": ("China Infantry General", "Çin Piyade", "PYD"),
    "ChinaNukeGeneral": ("China Nuke General", "Çin Nükleer", "NÜK"),
    "GLA": ("GLA", "GLA", "GLA"),
    "GLAToxinGeneral": ("GLA Toxin General", "GLA Toksin", "TKS"),
    "GLADemolitionGeneral": ("GLA Demolition General", "GLA Yıkım", "YKM"),
    "GLAStealthGeneral": ("GLA Stealth General", "GLA Gizlilik", "GZL"),
    "Turkiye": ("Turkiye (Experimental)", "Türkiye", "TÜR"),
}
VANILLA_FACTIONS = {"usa": "ABD", "china": "Çin", "gla": "GLA", "turkiye": "Türkiye"}
# a side that is a faction of its own on the page although its BaseSide names another one: Turkiye says
# USA so the AI runs the American scripts, and it is the fork's fourth faction, not a US general
OWN_FACTION_SIDES = {"Turkiye"}
# GameEngine::init: Balance and Fixes over EA's objects, then Turkiye's reskins of what they left
PATCH_FILES = ("Data/INI/BalanceReforged.ini", "Data/INI/FixesReforged.ini", "Data/INI/TurkiyeReforged.ini")
# Object::look: a unit sees no further than half as far again as its longest weapon reaches
SIGHT_PER_WEAPON_RANGE = 1.5

Shots = collections.namedtuple("Shots", "damage delay clip reload opening")


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


FRAMES_PER_MSEC = f32(f32(LOGIC_FPS) / f32(1000.0))


def msec_to_frames(msec):
    """INI::parseDurationUnsignedInt: msec to whole frames, rounded up in float."""
    return math.ceil(f32(f32(msec) * FRAMES_PER_MSEC))


# ---- files -----------------------------------------------------------------------------------------

class GameFiles:
    """Data/... paths resolved like the game: Code/Data master, then loose under Run, then the archives."""

    def __init__(self):
        self.members = {}
        for big in sorted(RUN_DIR.glob("*.big"), key=lambda big: big.name.lower()):
            self._index(big, big.name.lower().startswith("patch"))

    def _index(self, big, overwrite):
        with open(big, "rb") as f:
            for path, offset, size in bigfile._read_index(f):
                path = path.replace("\\", "/")
                if overwrite or path.lower() not in self.members:
                    self.members[path.lower()] = (path, big, offset, size)

    def _fork_loose(self, path):
        return [loose for loose in (CODE_DATA / path.split("/", 1)[1], RUN_DIR / path) if loose.is_file()]

    def _archived(self, path):
        _, big, offset, size = self.members[path.lower()]
        with open(big, "rb") as f:
            f.seek(offset)
            return f.read(size)

    def read(self, path):
        for loose in self._fork_loose(path):
            return loose.read_bytes()
        return self._archived(path) if path.lower() in self.members else None

    def list_dir(self, directory):
        """Every *.ini under a Data/... directory, sorted as INI::loadDirectory loads them."""
        found = {path for path, _, _, _ in self.members.values()
                 if path.lower().startswith(directory.lower() + "/") and path.lower().endswith(".ini")}
        for root in (CODE_DATA / directory.split("/", 1)[1], RUN_DIR / directory):
            if root.is_dir():
                found |= {directory + "/" + p.relative_to(root).as_posix() for p in root.rglob("*.ini")}
        # FilenameList is a case-insensitive set of backslashed paths, and the files in subfolders load last
        return sorted({path.lower(): path for path in sorted(found)}.values(),
                      key=lambda path: ("/" in path[len(directory) + 1:], path.lower().replace("/", "\\")))

    def text(self, path):
        return self.read(path).decode("latin-1")


# ---- INI ---------------------------------------------------------------------------------------------

# the lines that open a nested block inside an Object; everything else in one is a field
OPENS_WITH_VALUE = re.compile(r"(Body|Behavior|Draw|ClientUpdate|ConditionState|TransitionState)\s*=", re.I)
OPENS_BARE = re.compile(r"(WeaponSet|ArmorSet|UnitSpecificSounds|UnitSpecificFX|Prerequisites|ReplaceModule|AddModule|"
                        r"InheritableModule|OverrideableByLikeKind|DefaultConditionState|ConditionState|Turret|AltTurret|"
                        r"AttackAreaDecal|TargetingReticleDecal|GridDecalTemplate|DeliveryDecal)\b[^=]*$", re.I)
END = re.compile(r"End\b", re.I)


def ini_blocks(text):
    """(keyword, arguments, children) for each top-level block; a child is (line, its children or None)."""
    stack = []
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].split("//", 1)[0].strip()
        if not line:
            continue
        if not stack:
            stack.append((line, []))
        elif END.match(line):
            header, children = stack.pop()
            if not stack:
                words = header.split()
                yield words[0], words[1:], children
        elif OPENS_WITH_VALUE.match(line) or OPENS_BARE.match(line):
            node = (line, [])
            stack[-1][1].append(node)
            stack.append(node)
        else:
            stack[-1][1].append((line, None))
    if stack:
        raise ValueError("INI block %r never ends" % stack[0][0])


def field(line):
    key, equals, value = line.partition("=")
    if not equals:
        key, _, value = line.partition(" ")
    return key.strip().lower(), value.split()


def yes(token):
    return token.lower() in ("yes", "true", "1")


LEADING_NUMBER = re.compile(r"[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?")


def real(token):
    """INI::scanReal is sscanf %f: the number at the front, and whatever follows it ("75.0.") is ignored."""
    return float(LEADING_NUMBER.match(token).group(0))


def percent(token):
    return real(token) / 100.0


# ---- weapons, armor, locomotors ------------------------------------------------------------------------

def new_weapon(name):
    return {"name": name, "damage": 0.0, "type": "EXPLOSION", "range": 0.0, "min_range": 0.0, "delay": (0, 0), "clip": 0,
            "reload": 0, "anti_air": False, "anti_ground": True, "reload_type": "YES", "pre_attack": 0,
            "prefire": "PER_SHOT", "fire_one": None, "fire_two": None, "bonus": {}}


def apply_weapon(weapon, children):
    for line, _ in children:
        key, value = field(line)
        if key == "primarydamage":
            weapon["damage"] = real(value[0])
        elif key == "damagetype":
            weapon["type"] = value[0].upper()
        elif key == "attackrange":
            weapon["range"] = real(value[0])
        elif key == "minimumattackrange":
            weapon["min_range"] = real(value[0])
        elif key == "delaybetweenshots":
            # WeaponTemplate::parseShotDelay: one number, or Min:a Max:b
            parts = re.split(r"[\s:=]+", " ".join(value))
            low = high = int(real(parts[1] if parts[0].lower() == "min" else parts[0]))
            if parts[0].lower() == "min" and len(parts) > 3 and parts[2].lower() == "max":
                high = int(real(parts[3]))
            weapon["delay"] = (msec_to_frames(low), msec_to_frames(high))
        elif key == "clipsize":
            weapon["clip"] = int(real(value[0]))
        elif key == "clipreloadtime":
            weapon["reload"] = msec_to_frames(int(real(value[0])))
        elif key == "antiairbornevehicle":
            weapon["anti_air"] = yes(value[0])
        elif key == "antiground":
            weapon["anti_ground"] = yes(value[0])
        elif key == "autoreloadsclip":
            weapon["reload_type"] = value[0].upper()
        elif key == "preattackdelay":
            weapon["pre_attack"] = msec_to_frames(int(real(value[0])))
        elif key == "preattacktype":
            weapon["prefire"] = value[0].upper()
        elif key == "continuousfireone":
            weapon["fire_one"] = int(real(value[0]))
        elif key == "continuousfiretwo":
            weapon["fire_two"] = int(real(value[0]))
        elif key == "weaponbonus":
            weapon["bonus"].setdefault(value[0].upper(), {})[value[1].upper()] = percent(value[2])


def parse_armor(children):
    """ArmorTemplate: every type at 100% until a line says otherwise; DEFAULT sets them all."""
    armor = {"DEFAULT": 1.0}
    for line, _ in children:
        key, value = field(line)
        if key == "armor":
            if value[0].upper() == "DEFAULT":
                armor = {"DEFAULT": percent(value[1])}
            else:
                armor[value[0].upper()] = percent(value[1])
    return armor


def adjust_damage(armor, damage_type, damage):
    if armor is None or damage_type in UNRESISTED:
        return damage
    return max(0.0, damage * armor.get(damage_type, armor["DEFAULT"]))


# ---- objects -------------------------------------------------------------------------------------------

class Thing:
    def __init__(self, name):
        self.name = name
        self.kinds = []
        self.side = ""
        self.cost = 0.0
        self.display = ""
        self.locomotor = None
        self.speed_scale = 1.0              # SpeedScale: Turkiye's speeds on the American locomotors
        self.sight = 0.0                    # ShroudClearingRange, before Object::look caps it
        self.spotter = False                # SpotterSight: the cap does not apply
        self.beyond_vision = False          # AcquiresBeyondVision: picks targets out to its range from what the team sees
        self.variations = []                # BuildVariations: what the factory really makes
        self.command_sets = [""]            # its CommandSet, then any a CommandSetUpgrade switches to
        self.body = None                   # (tag, MaxHealth or 0 for a body that cannot be hurt)
        self.modules = {}                  # module tag -> module name, for ReplaceModule
        self.sets = {"weapon": [], "armor": []}
        self.sets_copied = {"weapon": True, "armor": True}

    def copy_as(self, name):
        thing = copy.deepcopy(self)
        thing.name = name
        thing.sets_copied = {"weapon": True, "armor": True}
        return thing

    def health(self):
        return self.body[1] if self.body else 0.0

    def unconditioned(self, kind):
        """findWeaponTemplateSet / findArmorTemplateSet with no flags: the set asking for the fewest."""
        sets = self.sets[kind]
        return min(sets, key=lambda entry: len(entry[0]))[1] if sets else None

    def apply(self, children, multifile, weapons=None):
        for line, nested in children:
            key, value = field(line)
            if key == "kindof":
                self.apply_kinds(value)
            elif key == "side":
                self.side = value[0]
            elif key == "buildcost":
                self.cost = real(value[0])
            elif key == "displayname":
                self.display = value[0]
            elif key == "locomotor" and value[0].upper() == "SET_NORMAL":
                self.locomotor = value[1].lower()
            elif key == "speedscale":
                self.speed_scale = real(value[0])
            elif key == "shroudclearingrange":
                self.sight = real(value[0])
            elif key == "spottersight":
                self.spotter = yes(value[0])
            elif key == "acquiresbeyondvision":
                self.beyond_vision = yes(value[0])
            elif key == "buildvariations":
                self.variations = [name.lower() for name in value]
            elif key == "commandset":
                self.command_sets = [value[0].lower()] + self.command_sets[1:]
            elif key in ("weaponset", "armorset"):
                self.add_set(key[:-3], nested, multifile, weapons)
            elif key == "removemodule":
                self.drop_module(value[0])
            elif key == "replacemodule":
                # ThingTemplate::parseModuleName: a tag that holds a module of another type means the data
                # numbered the modules differently, and the block is dropped (Shockwave's Tank_ChinaBunker)
                replaced = self.modules.get(value[0].lower())
                if replaced is None or not nested or field(nested[0][0])[1][0].lower() == replaced:
                    self.drop_module(value[0])
                    self.apply_modules(nested)
            elif key in ("addmodule", "inheritablemodule", "overrideablebylikekind"):
                self.apply_modules(nested)
            elif key in MODULE_KEYS:
                self.add_module(key, value, nested)

    def apply_kinds(self, tokens):
        if tokens[0][0] not in "+-":
            self.kinds = []
        for token in tokens:
            bit = token.lstrip("+-").upper()
            if token.startswith("-"):
                self.kinds = [kind for kind in self.kinds if kind != bit]
            elif bit != "NONE" and bit not in self.kinds:
                self.kinds.append(bit)

    def add_set(self, kind, children, multifile, weapons):
        conditions = frozenset()
        content = {} if kind == "weapon" else None
        for line, _ in children:
            key, value = field(line)
            if key == "conditions":
                conditions = frozenset(token.upper() for token in value if token.upper() != "NONE")
            elif kind == "weapon" and key == "weapon" and len(value) > 1:
                content[value[0].upper()] = None if value[1].upper() == "NONE" else value[1].lower()
            elif kind == "armor" and key == "armor":
                content = value[0].lower()
        # ThingTemplate::parseWeaponTemplateSet: a patch file's set naming a weapon the data lacks is
        # skipped (Turkiye's Korkut keeps the Avenger's dummy laser, which ShockWave has not)
        if multifile and kind == "weapon" and any(name and name not in weapons for name in content.values()):
            return
        sets = self.sets[kind]
        if self.sets_copied[kind]:
            self.sets_copied[kind] = False
            sets.clear()
        if multifile:
            # a patch file's set with conditions the template already has takes that set's place
            for index, (existing, _) in enumerate(sets):
                if existing == conditions:
                    sets[index] = (conditions, content)
                    return
        sets.append((conditions, content))

    def drop_module(self, tag):
        self.modules.pop(tag.lower(), None)
        if self.body and self.body[0].lower() == tag.lower():
            self.body = None

    def apply_modules(self, children):
        for line, nested in children:
            key, value = field(line)
            if key in MODULE_KEYS:
                self.add_module(key, value, nested)

    def add_module(self, key, value, nested):
        if len(value) > 1:
            self.modules[value[1].lower()] = value[0].lower()
        if value[0].lower() == "commandsetupgrade":
            self.command_sets += [words[0].lower() for name, words in (field(line) for line, _ in nested)
                                  if name in ("commandset", "commandsetalt") and words]
        if key == "body":
            self.set_body(value, nested)

    def set_body(self, value, children):
        health = 0.0
        if value[0].lower() in BODIES_WITH_HEALTH:
            for line, _ in children:
                key, number = field(line)
                if key == "maxhealth":
                    health = real(number[0])
        self.body = (value[1] if len(value) > 1 else "", health)


class GameData:
    def __init__(self, files):
        self.weapons = {}
        self.armors = {}
        self.locomotors = {}
        self.things = {}
        self.skipped = []                  # patch blocks naming what this data does not have

        # WeaponReforged.ini and TurkiyeWeaponReforged.ini right after Weapon.ini, as GameEngine::init
        # loads them: the fork's own names
        for path in ("Data/INI/Weapon.ini", "Data/INI/WeaponReforged.ini", "Data/INI/TurkiyeWeaponReforged.ini"):
            for keyword, args, children in ini_blocks(files.text(path)):
                if keyword.lower() == "weapon" and args[0].lower() not in self.weapons:
                    self.weapons[args[0].lower()] = new_weapon(args[0])
                    apply_weapon(self.weapons[args[0].lower()], children)
        for keyword, args, children in ini_blocks(files.text("Data/INI/Locomotor.ini")):
            if keyword.lower() == "locomotor":
                speeds = [real(value[0]) for key, value in (field(line) for line, _ in children) if key == "speed"]
                self.locomotors[args[0].lower()] = speeds[-1] if speeds else 0.0
        self.armor_names = {}
        for keyword, args, children in ini_blocks(files.text("Data/INI/Armor.ini")):
            if keyword.lower() == "armor":
                self.armors[args[0].lower()] = parse_armor(children)
                self.armor_names[args[0].lower()] = args[0]

        self.default = Thing("DefaultThingTemplate")
        for keyword, args, children in ini_blocks(files.text("Data/INI/Default/Object.ini")):
            self.default.apply(children, False)
        for path in files.list_dir("Data/INI/Object"):
            self.load_objects(files.text(path), False)
        for path in PATCH_FILES:
            self.load_objects(files.text(path), True)

        buttons = {}
        for path in ("Data/INI/CommandButton.ini", "Data/INI/CommandSetReforged.ini"):
            for keyword, args, children in ini_blocks(files.text(path)):
                if keyword.lower() == "commandbutton":
                    buttons.setdefault(args[0].lower(), {}).update(field(line) for line, _ in children)
        self.buildable = {button["object"][0].lower() for button in buttons.values()
                          if button.get("command", [""])[0].upper() == "UNIT_BUILD" and "object" in button}
        # CommandSet blocks: a later one, CommandSetReforged.ini's included, replaces the whole set
        self.command_sets = {}
        for path in ("Data/INI/CommandSet.ini", "Data/INI/CommandSetReforged.ini"):
            for keyword, args, children in ini_blocks(files.text(path)):
                if keyword.lower() == "commandset":
                    self.command_sets[args[0].lower()] = [buttons[value[0].lower()] for key, value in map(field, (
                        line for line, _ in children)) if value and value[0].lower() in buttons]

    def reachable(self, starts):
        """Every object a player starting with these can build: the command sets of what it has, the
        buttons in them that build or construct, and the sets of what those make, until nothing is new."""
        seen, units, todo = set(), set(), [name.lower() for name in starts]
        while todo:
            name = todo.pop()
            if name in seen or name not in self.things:
                continue
            seen.add(name)
            for command_set in self.things[name].command_sets:
                for button in self.command_sets.get(command_set, ()):
                    command = button.get("command", [""])[0].upper()
                    if command in ("UNIT_BUILD", "DOZER_CONSTRUCT") and "object" in button:
                        todo.append(button["object"][0].lower())
                        if command == "UNIT_BUILD":
                            units.add(button["object"][0].lower())
        # a button can name what this data never made
        return units & self.things.keys()

    def load_objects(self, text, multifile):
        # a patch of an Object, Weapon or Armor the data never built, and a reskin of a missing original,
        # is read and dropped (ThingFactory::parseObjectDefinition, WeaponStore, ArmorStore)
        for keyword, args, children in ini_blocks(text):
            keyword = keyword.lower()
            if keyword == "objectreskin":
                if args[1].lower() not in self.things:
                    self.skipped.append(" ".join([keyword] + args))
                    continue
                thing = self.things[args[1].lower()].copy_as(args[0])
                self.things[args[0].lower()] = thing
                thing.apply(children, False)
            elif keyword in ("object", "weapon", "armor") and multifile and args[0].lower() not in (
                    self.things if keyword == "object" else self.weapons if keyword == "weapon" else self.armors):
                self.skipped.append(" ".join([keyword] + args))
            elif keyword == "object":
                if args[0].lower() not in self.things:
                    self.things[args[0].lower()] = self.default.copy_as(args[0])
                self.things[args[0].lower()].apply(children, multifile, self.weapons)
            elif keyword == "weapon" and multifile:
                apply_weapon(self.weapons[args[0].lower()], children)
            elif keyword == "armor" and multifile:
                self.armors[args[0].lower()] = parse_armor(children)


# ---- the AI's matchup arithmetic -----------------------------------------------------------------------

def shot_pattern(weapon, target, armors):
    """AIPlayer.cpp shotPatternAgainst: None when the weapon may not aim at the target."""
    if not weapon["anti_air" if "AIRCRAFT" in target.kinds else "anti_ground"]:
        return None

    # a gattling is read at the fastest step its own WeaponBonus block gives it
    bonus = {"DAMAGE": 1.0, "RATE_OF_FIRE": 1.0, "PRE_ATTACK": 1.0}
    if weapon["bonus"]:
        spun_up = ("CONTINUOUS_FIRE_FAST" if weapon["fire_two"] is not None
                   else "CONTINUOUS_FIRE_MEAN" if weapon["fire_one"] is not None else None)
        for name, value in weapon["bonus"].get(spun_up, {}).items():
            bonus[name] = bonus.get(name, 1.0) + value - 1.0

    armor_name = target.unconditioned("armor")
    armor = armors[armor_name] if armor_name else None
    damage_type = weapon["type"]
    if damage_type == "KILL_PILOT":
        damage = adjust_damage(armor, damage_type, target.health()) if "VEHICLE" in target.kinds else 0.0
    elif damage_type in NOT_HEALTH_DAMAGE:
        damage = 0.0
    else:
        damage = adjust_damage(armor, damage_type, weapon["damage"] * bonus["DAMAGE"])

    rate = bonus["RATE_OF_FIRE"]
    pre_attack = int(weapon["pre_attack"] * bonus["PRE_ATTACK"])
    delay = (weapon["delay"][0] + weapon["delay"][1]) * 0.5 / rate
    reload = math.floor(weapon["reload"] / rate)
    if weapon["reload_type"] == "RETURN_TO_BASE":
        reload += SORTIE_FRAMES
    opening = 0
    if weapon["prefire"] == "PER_SHOT":
        delay += pre_attack
        reload += pre_attack
    elif weapon["prefire"] == "PER_CLIP":
        opening = pre_attack
        reload += pre_attack
    elif weapon["prefire"] == "PER_ATTACK":
        opening = pre_attack
    return Shots(damage, delay, weapon["clip"], reload, opening)


def kill_frames(health, shots):
    """AI.cpp aiFramesToKill: the first shot pays one delay, every later one the wait before it."""
    if shots.damage <= 0.0 or health <= 0.0:
        return CANNOT_KILL
    needed = max(1, math.ceil(health / shots.damage - OVERKILL_TOLERANCE))
    reloads = (needed - 1) // shots.clip if shots.clip > 0 else 0
    first = max(shots.delay, 1.0)
    return shots.opening + first + (needed - 1 - reloads) * shots.delay + reloads * shots.reload


def best_attack(data, attacker, target):
    """computeTemplateFramesToKill, with the weapon that wins: (frames, weapon) or None."""
    weapon_set = attacker.unconditioned("weapon") or {}
    best = None
    for slot in WEAPON_SLOTS:
        if not weapon_set.get(slot):
            continue
        weapon = data.weapons[weapon_set[slot]]
        if weapon["damage"] < DUMMY_DAMAGE:
            continue
        shots = shot_pattern(weapon, target, data.armors)
        frames = kill_frames(target.health(), shots) if shots else CANNOT_KILL
        if frames >= 0.0 and (best is None or frames < best[0]):
            best = (frames, weapon)
    return best


# ---- the page ------------------------------------------------------------------------------------------

def game_strings(files):
    """GameTextManager::init: the English CSF, and Data/Patch.str over it, which names the fork's own
    units and sides (Turkiye's among them)."""
    names = {}
    for label, text in gametext.read_csf(files.read("Data/English/generals.csf")):
        names.setdefault(label.lower(), text)
    names.update((label.lower(), text) for label, text in gametext.parse_str(CODE_DATA / "Patch.str"))
    return names


def playable_sides(files, names):
    """The sides the skirmish lobby offers (PopulatePlayerTemplateComboBox): a PlayerTemplate with a
    StartingBuilding whose general is not locked in ChallengeMode.ini, one per Side, in file order."""
    locked = set()
    for persona in re.split(r"GeneralPersona\d+", files.text("Data/INI/ChallengeMode.ini"))[1:]:
        template = re.search(r"^\s*PlayerTemplate\s*=\s*(\S+)", persona, re.M | re.I)
        enabled = re.search(r"^\s*StartsEnabled\s*=\s*(\S+)", persona, re.M | re.I)
        if template and enabled and not yes(enabled.group(1)):
            locked.add(template.group(1).lower())
    templates = {}
    # PlayerTemplateReforged.ini after EA's, MULTIFILE: a new name (FactionTurkiye) is added, an old one edited
    for path in ("Data/INI/Default/PlayerTemplate.ini", "Data/INI/PlayerTemplate.ini", "Data/INI/PlayerTemplateReforged.ini"):
        for keyword, args, children in ini_blocks(files.text(path)) if files.read(path) else ():
            if keyword.lower() == "playertemplate":
                templates.setdefault(args[0].lower(), {}).update(field(line) for line, _ in children)

    sides = {}
    factions = []
    starts = {}
    for template, fields in templates.items():
        if not fields.get("startingbuilding") or template in locked or fields["side"][0] in sides:
            continue
        side = fields["side"][0]
        starts[side] = [words[0] for key, words in fields.items()
                        if words and (key == "startingbuilding" or key.startswith("startingunit"))]
        faction = (side if side in OWN_FACTION_SIDES else fields.get("baseside", [side])[0]).lower()
        lobby = names.get("side:" + side.lower()) or names.get(fields.get("displayname", [""])[0].lower(), side)
        vanilla = VANILLA_SIDES.get(side)
        if vanilla and vanilla[0] == lobby:
            label, tag = vanilla[1], vanilla[2]
        else:
            # the lobby's own name, tagged by the word that tells it from its faction's other sides
            words = [word for word in re.findall(r"\w+", lobby) if word.lower() not in ("general", faction)]
            label, tag = lobby, (words[0] if words else lobby)[:3].upper()
        if faction not in factions:
            factions.append(faction)
        sides[side] = {"label": label, "tag": tag, "faction": faction, "main": not any(
            info["faction"] == faction for info in sides.values())}
    vanilla_order = list(VANILLA_SIDES)
    ordered = sorted(sides, key=lambda side: (factions.index(sides[side]["faction"]),
                                              vanilla_order.index(side) if side in vanilla_order else len(vanilla_order)))
    for order, side in enumerate(ordered):
        sides[side]["order"] = order
    faction_labels = [{"value": faction, "label": VANILLA_FACTIONS.get(faction) or next(
        info["label"] for info in sides.values() if info["faction"] == faction)} for faction in factions]
    return sides, faction_labels, starts


def build(files):
    data = GameData(files)
    names = game_strings(files)
    sides, factions, starts = playable_sides(files, names)

    # what each side's buildings and dozers offer; a unit keeps its own Side when that is one of the
    # lobby's, and takes the first side that builds it otherwise
    owner = {}
    for side in sorted(sides, key=lambda side: sides[side]["order"]):
        for name in data.reachable(starts[side]):
            owner.setdefault(name, data.things[name].side if data.things[name].side in sides else side)
    # ...and every build button whose object says it is a lobby side's, in no set or not: the page has always
    # carried those (Lazr_AmericaTankPaladin, Slth_GLAVehicleScudLauncher), so the retail page stays as it was
    for name in data.buildable:
        if name in data.things and data.things[name].side in sides:
            owner.setdefault(name, data.things[name].side)
    candidates = sorted((data.things[name] for name in owner if not name.startswith(CHALLENGE_PREFIX)),
                        key=lambda thing: thing.name)
    # the Technical's button builds a random chassis; the first one stands for all three, as it does in EA's data
    fighters = [data.things[thing.variations[0]] if thing.variations else thing for thing in candidates]
    fights = [[best_attack(data, attacker, target) for target in fighters] for attacker in fighters]
    # the carrier's Raptor costs nothing, so "the same money" has no answer for it
    kept = [index for index, row in enumerate(fights) if any(row) and candidates[index].cost > 0]

    def describe(index):
        thing = candidates[index]
        label = (thing.display or fighters[index].display).lower()
        return {"id": thing.name, "name": names.get(label, thing.name), "side": owner[thing.name.lower()]}

    def unit(index):
        fighter = fighters[index]
        weapon_set = fighter.unconditioned("weapon") or {}
        weapons = [data.weapons[weapon_set[slot]] for slot in WEAPON_SLOTS if weapon_set.get(slot)]
        armor = fighter.unconditioned("armor")
        # Object::look: the weapon caps the sight of all but a structure, a dozer and a spotter; the dummy
        # weapons count, as getLargestWeaponRange counts every slot
        reach = max((weapon["range"] for weapon in weapons), default=-1.0)
        sight = fighter.sight
        if reach > 0.0 and not fighter.spotter and not {"STRUCTURE", "DOZER"} & set(fighter.kinds):
            sight = min(sight, reach * SIGHT_PER_WEAPON_RANGE)
        return dict(describe(index), cost=candidates[index].cost, health=fighter.health(),
                    kinds=sorted(kind for kind in fighter.kinds if kind in SHOWN_KINDS),
                    armor=data.armor_names[armor] if armor else None,
                    speed=data.locomotors.get(fighter.locomotor, 0.0) * fighter.speed_scale,
                    sight=sight, beyondVision=fighter.beyond_vision,
                    weapons=[weapon["name"] for weapon in weapons if weapon["damage"] >= DUMMY_DAMAGE])

    def attack(fight):
        frames, weapon = fight
        return {"frames": round(frames, 3), "weapon": weapon["name"], "range": weapon["range"],
                "minRange": weapon["min_range"]}

    # what the reader could not make a fighter of: a body with no health it knows
    unread = ["%s (%s)" % (fighter.name, fighter.modules.get(fighter.body[0].lower(), "?") if fighter.body else "no body")
              for fighter in fighters if fighter.health() <= 0.0]
    return {
        "units": [unit(index) for index in kept],
        "attacks": [[attack(fights[a][t]) if fights[a][t] else None for t in kept] for a in kept],
        "excluded": [describe(index) for index in range(len(candidates)) if index not in kept],
        "fps": LOGIC_FPS,
        "sortieMsec": SORTIE_MSEC,
        "sides": sides,
        "factions": factions,
    }, {"skipped": data.skipped, "unread": unread}


def selfcheck():
    """The numbers test_gameengine's frames_to_kill_counts_shots_delays_and_reloads checks in C++."""
    gun = Shots(60.0, 60.0, 0, 0.0, 0.0)
    assert kill_frames(480.0, gun) == 480.0
    assert kill_frames(120.0, gun) == 120.0
    clip = Shots(50.0, 10.0, 2, 100.0, 0.0)
    assert kill_frames(150.0, clip) == 120.0
    assert kill_frames(150.0, clip._replace(opening=15.0)) == 135.0
    assert kill_frames(10.0, gun) == 60.0
    assert kill_frames(480.0, Shots(0.0, 30.0, 0, 0.0, 0.0)) == CANNOT_KILL
    assert msec_to_frames(2000) == 120 and msec_to_frames(100) == 6 and msec_to_frames(1) == 1
    assert real("75.0.") == 75.0 and real("-2e1x") == -20.0 and percent("50%") == 0.5
    print("unitmatrix selfcheck: ok")


def write_page(out):
    started = time.time()
    files = GameFiles()
    if files.read("Data/INI/Weapon.ini") is None or not files.list_dir("Data/INI/Object"):
        sys.exit("unitmatrix: no game data in %s. Put a Zero Hour install's *.big there (INIZH.big carries "
                 "the INIs, EnglishZH.big the names) or point ZHR_RUN_DIR at a Run that has them." % RUN_DIR)
    if files.read("Data/English/generals.csf") is None:
        sys.exit("unitmatrix: %s has no EnglishZH.big, which carries the unit names." % RUN_DIR)

    page, report = build(files)
    out.write_text(TEMPLATE.replace("__UNIT_DATA__", json.dumps(page, ensure_ascii=False)), encoding="utf-8", newline="\n")
    print("%d units over %d sides, %d without a weapon that kills, %d patch blocks skipped, %s in %.1f s"
          % (len(page["units"]), len({unit["side"] for unit in page["units"]}), len(page["excluded"]),
             len(report["skipped"]), out, time.time() - started))
    if report["unread"]:
        print("  no health read: " + ", ".join(report["unread"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--out", type=Path, help="where to write the HTML page")
    parser.add_argument("--selfcheck", action="store_true", help="check the frames-to-kill arithmetic and stop")
    args = parser.parse_args()
    if args.selfcheck:
        selfcheck()
        return
    if args.out is None:
        parser.error("--out is required")
    write_page(args.out)


TEMPLATE = r'''<!doctype html>
<html lang="tr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Zero Hour Karşılaşma Matrisi</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Saira+Semi+Condensed:wght@500;700&family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
:root {
  color-scheme: light;
  --ground: #e6e9e7;
  --surface: #f6f7f5;
  --surface-raised: #ffffff;
  --ink: #141c18;
  --ink-2: #4a5550;
  --ink-muted: #7a8580;
  --rule: #cdd3cf;
  --rule-strong: #aab2ad;
  --neutral-cell: #e4e5e1;
  --na-cell: #eef0ed;
  --na-hatch: #d6dad6;
  --focus: #1c5cab;
  --tip-ground: #141c18;
  --tip-ink: #eef2ef;
  --tip-muted: #9aa6a0;

  --w1: #cde2fb; --w2: #9ec5f4; --w3: #6da7ec; --w4: #3987e5; --w5: #256abf; --w6: #184f95; --w7: #0d366b;
  --l1: #fbd9d7; --l2: #f5b3b0; --l3: #ec8a86; --l4: #e05553; --l5: #c23a39; --l6: #962b2b; --l7: #6b1f1f;

  --font-display: "Saira Semi Condensed", "Arial Narrow", "Roboto Condensed", sans-serif;
  --font-body: "IBM Plex Sans", "Segoe UI", system-ui, sans-serif;
  --font-data: "IBM Plex Mono", ui-monospace, "Cascadia Mono", Consolas, monospace;

  --cell-w: 34px;
  --row-head-w: 190px;
  --cell-h: 24px;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    color-scheme: dark;
    --ground: #0f1311;
    --surface: #161b18;
    --surface-raised: #1d2320;
    --ink: #e7ece9;
    --ink-2: #b3bdb8;
    --ink-muted: #7f8a85;
    --rule: #2a322e;
    --rule-strong: #3d4742;
    --neutral-cell: #3a3d3a;
    --na-cell: #1b201d;
    --na-hatch: #262d29;
    --focus: #6da7ec;
    --tip-ground: #e7ece9;
    --tip-ink: #141c18;
    --tip-muted: #4a5550;
  }
}
:root[data-theme="dark"] {
  color-scheme: dark;
  --ground: #0f1311;
  --surface: #161b18;
  --surface-raised: #1d2320;
  --ink: #e7ece9;
  --ink-2: #b3bdb8;
  --ink-muted: #7f8a85;
  --rule: #2a322e;
  --rule-strong: #3d4742;
  --neutral-cell: #3a3d3a;
  --na-cell: #1b201d;
  --na-hatch: #262d29;
  --focus: #6da7ec;
  --tip-ground: #e7ece9;
  --tip-ink: #141c18;
  --tip-muted: #4a5550;
}

* { box-sizing: border-box; }
body {
  background: var(--ground);
  color: var(--ink);
  font-family: var(--font-body);
  font-size: 14px;
  line-height: 1.5;
  padding-inline: clamp(16px, 3vw, 40px);
  padding-block: 28px 56px;
}
.page { max-width: 1600px; margin-inline: auto; display: grid; gap: 20px; }

header.masthead {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  gap: 8px 32px;
  align-items: end;
  border-bottom: 2px solid var(--ink);
  padding-bottom: 14px;
}
.eyebrow {
  font-family: var(--font-data);
  font-size: 11px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--ink-muted);
  margin: 0 0 4px;
}
h1 {
  font-family: var(--font-display);
  font-weight: 700;
  font-size: clamp(30px, 4.2vw, 46px);
  line-height: 1;
  letter-spacing: -0.01em;
  margin: 0;
  text-wrap: balance;
}
.lede { margin: 8px 0 0; color: var(--ink-2); max-width: 68ch; }
.readout {
  font-family: var(--font-data);
  font-size: 12px;
  color: var(--ink-2);
  text-align: right;
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}
.readout b { font-weight: 500; color: var(--ink); }

.controls {
  display: flex;
  flex-wrap: wrap;
  gap: 12px 20px;
  align-items: end;
}
.control { display: grid; gap: 4px; }
.control > label, .control > .label {
  font-family: var(--font-data);
  font-size: 11px;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--ink-muted);
}
select {
  font: 500 13px var(--font-body);
  color: var(--ink);
  background: var(--surface-raised);
  border: 1px solid var(--rule-strong);
  border-radius: 3px;
  padding: 6px 28px 6px 10px;
  min-height: 34px;
}
.segmented {
  display: inline-flex;
  border: 1px solid var(--rule-strong);
  border-radius: 3px;
  overflow: hidden;
  background: var(--surface-raised);
}
.segmented input { position: absolute; opacity: 0; pointer-events: none; }
.segmented label {
  font: 500 13px var(--font-body);
  padding: 6px 12px;
  min-height: 32px;
  display: inline-flex;
  align-items: center;
  cursor: pointer;
  color: var(--ink-2);
}
.segmented label + input + label { border-left: 1px solid var(--rule); }
.segmented input:checked + label { background: var(--ink); color: var(--surface); }
.segmented input:focus-visible + label { outline: 2px solid var(--focus); outline-offset: -2px; }
select:focus-visible, .toggle input:focus-visible { outline: 2px solid var(--focus); outline-offset: 2px; }
.toggle { display: inline-flex; gap: 8px; align-items: center; min-height: 34px; cursor: pointer; font-weight: 500; }
.toggle input { width: 16px; height: 16px; accent-color: var(--ink); }

.legend { display: grid; gap: 4px; margin-left: auto; }
.legend-bar { display: grid; grid-template-columns: repeat(15, 22px); height: 14px; }
.legend-bar span { display: block; }
.legend-ticks {
  display: flex;
  justify-content: space-between;
  font-family: var(--font-data);
  font-size: 10.5px;
  color: var(--ink-2);
  font-variant-numeric: tabular-nums;
}

.board {
  background: var(--surface);
  border: 1px solid var(--rule);
  border-radius: 4px;
  overflow: hidden;
}
.board-head {
  display: flex;
  flex-wrap: wrap;
  justify-content: space-between;
  gap: 4px 16px;
  padding: 10px 14px;
  border-bottom: 1px solid var(--rule);
  font-size: 12.5px;
  color: var(--ink-2);
}
.axis-note { font-family: var(--font-data); font-size: 11.5px; }
.matrix-frame {
  display: grid;
  grid-template-columns: 30px minmax(0, 1fr);
  grid-template-rows: auto minmax(0, 1fr);
}
.axis {
  font-family: var(--font-display);
  font-weight: 700;
  font-size: 14px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  background: var(--ink);
  color: var(--surface);
  display: flex;
  align-items: center;
  gap: 10px;
  white-space: nowrap;
}
.axis small {
  font-family: var(--font-body);
  font-weight: 400;
  font-size: 12px;
  letter-spacing: 0;
  text-transform: none;
  opacity: 0.75;
}
.axis-target {
  grid-column: 2;
  padding: 5px 12px 5px calc(var(--row-head-w) + 12px);
  border-left: 1px solid var(--surface);
}
.axis-attacker {
  grid-column: 1;
  grid-row: 1 / span 2;
  writing-mode: vertical-rl;
  transform: rotate(180deg);
  justify-content: flex-end;
  padding: 12px 0;
}
.matrix-scroll {
  grid-column: 2;
  overflow: auto;
  max-height: calc(100vh - 150px);
  min-height: 320px;
  overscroll-behavior: contain;
}
table.matrix {
  border-collapse: separate;
  border-spacing: 0;
  font-family: var(--font-data);
  font-variant-numeric: tabular-nums;
}
.matrix caption {
  position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0);
}
.matrix thead th {
  position: sticky;
  top: 0;
  z-index: 2;
  background: var(--surface);
  vertical-align: bottom;
  height: 150px;
  width: var(--cell-w);
  min-width: var(--cell-w);
  padding: 0 0 6px;
  border-bottom: 1px solid var(--rule-strong);
  font-weight: 400;
}
.matrix thead th .col-label {
  writing-mode: vertical-rl;
  transform: rotate(180deg);
  display: inline-flex;
  align-items: center;
  gap: 6px;
  max-height: 142px;
  overflow: hidden;
  white-space: nowrap;
  font-family: var(--font-body);
  font-size: 12px;
  font-weight: 500;
  color: var(--ink);
  line-height: var(--cell-w);
}
.matrix .corner {
  left: 0;
  z-index: 4;
  text-align: left;
  vertical-align: bottom;
  min-width: 190px;
  width: 190px;
  padding: 0 12px 8px;
  border-right: 1px solid var(--rule-strong);
}
.corner-key {
  font-family: var(--font-data);
  font-size: 10.5px;
  line-height: 1.45;
  color: var(--ink-muted);
  text-transform: uppercase;
  letter-spacing: 0.05em;
}
.corner-key strong { font-weight: 500; color: var(--ink); }
.matrix tbody th {
  position: sticky;
  left: 0;
  z-index: 1;
  background: var(--surface);
  text-align: left;
  font-family: var(--font-body);
  font-size: 12.5px;
  font-weight: 500;
  white-space: nowrap;
  padding: 0 12px;
  height: var(--cell-h);
  border-right: 1px solid var(--rule-strong);
  max-width: 190px;
  overflow: hidden;
  text-overflow: ellipsis;
}
.tag {
  font-family: var(--font-data);
  font-size: 9.5px;
  font-weight: 500;
  letter-spacing: 0.04em;
  color: var(--ink-muted);
}
.matrix tbody th .tag { margin-left: 6px; }
.matrix tbody tr.group-start th, .matrix tbody tr.group-start td { border-top: 1px solid var(--rule-strong); }
.matrix thead th.group-start, .matrix td.group-start { box-shadow: inset 1px 0 0 var(--rule-strong); }
.matrix th.hot { background: var(--surface-raised); color: var(--focus); box-shadow: inset 3px 0 0 var(--focus); }
.matrix thead th.hot { box-shadow: inset 0 -3px 0 var(--focus); }

.matrix td {
  width: var(--cell-w);
  min-width: var(--cell-w);
  height: var(--cell-h);
  padding: 0;
  text-align: center;
  font-size: 10.5px;
  line-height: var(--cell-h);
  color: var(--ink);
  border-right: 1px solid var(--surface);
  border-bottom: 1px solid var(--surface);
  cursor: crosshair;
}
.matrix td.n { background: var(--neutral-cell); color: var(--ink-2); }
.matrix td.na {
  background: repeating-linear-gradient(135deg, var(--na-cell) 0 4px, var(--na-hatch) 4px 5px);
  color: var(--ink-muted);
}
.matrix td.diag { background: var(--surface-raised); box-shadow: inset 0 0 0 1px var(--ink); font-weight: 500; color: var(--ink); }
.matrix td.diag.na { background: repeating-linear-gradient(135deg, var(--na-cell) 0 4px, var(--na-hatch) 4px 5px); }
.w1 { background: var(--w1); color: #0d2a4d; } .w2 { background: var(--w2); color: #0d2a4d; }
.w3 { background: var(--w3); color: #08203d; } .w4 { background: var(--w4); color: #ffffff; }
.w5 { background: var(--w5); color: #ffffff; } .w6 { background: var(--w6); color: #ffffff; }
.w7 { background: var(--w7); color: #ffffff; }
.l1 { background: var(--l1); color: #4a1414; } .l2 { background: var(--l2); color: #4a1414; }
.l3 { background: var(--l3); color: #3a0f0f; } .l4 { background: var(--l4); color: #ffffff; }
.l5 { background: var(--l5); color: #ffffff; } .l6 { background: var(--l6); color: #ffffff; }
.l7 { background: var(--l7); color: #ffffff; }
.matrix td.cross { filter: brightness(0.93); }
.matrix td:hover { outline: 2px solid var(--ink); outline-offset: -2px; }

.tooltip {
  position: fixed;
  z-index: 10;
  pointer-events: none;
  max-width: 340px;
  background: var(--tip-ground);
  color: var(--tip-ink);
  border-radius: 4px;
  padding: 10px 12px;
  font-size: 12.5px;
  line-height: 1.45;
  box-shadow: 0 6px 24px rgba(0, 0, 0, 0.25);
}
.tooltip .tip-title { font-family: var(--font-display); font-weight: 700; font-size: 16px; line-height: 1.15; }
.tooltip .tip-ratio { font-family: var(--font-data); font-size: 22px; font-weight: 500; margin: 4px 0 6px; font-variant-numeric: tabular-nums; }
.tooltip .tip-row { display: grid; grid-template-columns: auto 1fr; gap: 0 10px; font-variant-numeric: tabular-nums; }
.tooltip .tip-row span:nth-child(odd) { color: var(--tip-muted); font-family: var(--font-data); font-size: 11px; padding-top: 1px; }
.tooltip .tip-weapon { font-family: var(--font-data); font-size: 10.5px; color: var(--tip-muted); }

.notes {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  gap: 20px 40px;
  padding-top: 8px;
}
.notes h2 {
  font-family: var(--font-display);
  font-weight: 700;
  font-size: 19px;
  margin: 0 0 6px;
}
.notes p { margin: 0 0 8px; color: var(--ink-2); max-width: 65ch; }
.notes code { font-family: var(--font-data); font-size: 12px; color: var(--ink); }
.excluded { font-size: 12.5px; color: var(--ink-2); }

@media (max-width: 720px) {
  header.masthead { grid-template-columns: 1fr; }
  .readout { text-align: left; }
  .legend { margin-left: 0; }
  :root { --cell-w: 30px; --row-head-w: 132px; }
  .matrix .corner, .matrix tbody th { min-width: 132px; width: 132px; max-width: 132px; }
}
@media (prefers-reduced-motion: reduce) { * { transition: none !important; } }
</style>

</head>
<body>
<div class="page">
  <header class="masthead">
    <div>
      <p class="eyebrow">Zero Hour Reforged · INIZH.big ve BalanceReforged.ini · birebir çarpışma</p>
      <h1>Kim kime karşı</h1>
      <p class="lede">Satırdaki birim, sütundaki birimle tek başına karşılaşıyor. Hücre kaç kat üstün olunduğunu söylüyor: 2, saldıranın hedefi hedefin onu öldürdüğü sürenin yarısında öldürdüğü demek. −2 tersi, hedef saldıranı yarı sürede öldürüyor. Köşegen kendi kendine karşı, yani 1; Comanche gibi kendi türünü vuramayan birimlerde taralı.</p>
    </div>
    <div class="readout" id="readout"></div>
  </header>

  <form class="controls" id="controls" onsubmit="return false">
    <div class="control">
      <label for="rowFilter">Satırlar</label>
      <select id="rowFilter"></select>
    </div>
    <div class="control">
      <label for="colFilter">Sütunlar</label>
      <select id="colFilter"></select>
    </div>
    <div class="control">
      <span class="label" id="modeLabel">Ölçü</span>
      <div class="segmented" role="radiogroup" aria-labelledby="modeLabel">
        <input type="radio" name="mode" id="modeRaw" value="raw" checked><label for="modeRaw">Birim başına</label>
        <input type="radio" name="mode" id="modeCost" value="cost"><label for="modeCost">Aynı para ile</label>
      </div>
    </div>
    <div class="control">
      <span class="label" id="sortLabel">Sıra</span>
      <div class="segmented" role="radiogroup" aria-labelledby="sortLabel">
        <input type="radio" name="sort" id="sortSide" value="side" checked><label for="sortSide">Taraf</label>
        <input type="radio" name="sort" id="sortPower" value="power"><label for="sortPower">Genel güç</label>
      </div>
    </div>
    <div class="control">
      <span class="label">Menzil</span>
      <label class="toggle" for="useRange"><input type="checkbox" id="useRange">Kısa menzilli yaklaşırken ateş yer</label>
    </div>
    <div class="control">
      <span class="label">Görüş</span>
      <label class="toggle" for="spotted"><input type="checkbox" id="spotted">Gözcü var, herkes menzilinin sonuna kadar görüyor</label>
    </div>
    <div class="legend" aria-hidden="true">
      <div class="legend-bar" id="legendBar"></div>
      <div class="legend-ticks"><span>−20 zayıf</span><span>−3</span><span>eşit</span><span>3</span><span>20 güçlü</span></div>
    </div>
  </form>

  <section class="board">
    <div class="board-head">
      <span>Mavi, 3: soldaki saldıran üstteki hedefi 3 kat hızlı öldürüyor. Kırmızı, −3: hedef saldıranı 3 kat hızlı öldürüyor. Hücrenin üstüne gelince iki yönün öldürme süresi ve silah çıkar.</span>
      <span class="axis-note">∞ hedef karşılık veremiyor · 0 saldıran hedefe zarar veremiyor · taralı: ikisi de veremiyor</span>
    </div>
    <div class="matrix-frame">
      <div class="axis axis-attacker" aria-hidden="true">Saldıran ↓ <small>soldaki birim ateş ediyor</small></div>
      <div class="axis axis-target" aria-hidden="true">Hedef → <small>üstteki birim vuruluyor</small></div>
      <div class="matrix-scroll" id="scroll">
        <table class="matrix" id="matrix"></table>
      </div>
    </div>
  </section>

  <section class="notes">
    <div>
      <h2>Sayı nereden çıkıyor</h2>
      <p>Her yön için öldürme süresi hesaplanıyor: hedefin <code>MaxHealth</code> değeri, silahın <code>PrimaryDamage</code> değerinin hedef zırhındaki yüzdesiyle çarpımına bölünüyor, çıkan atış sayısı <code>DelayBetweenShots</code>, şarjör ve <code>ClipReloadTime</code> ile saniyeye çevriliyor. Hücre, sütunun süresinin satırın süresine oranı. Birim birden fazla silah taşıyorsa o hedefe en hızlı öldüreni seçiliyor.</p>
      <p>Oyundaki yapay zekâ karşı birim seçerken aynı hesabı yapıyor (<code>aiFramesToKill</code>), bu tablo onun gördüğü sayıları gösteriyor.</p>
      <p>"Aynı para ile" oranı iki birimin fiyat oranıyla çarpıyor. Doğrusal bir düzeltme bu: 900'lük Crusader'a karşı 600'lük birimden bir buçuk tane geldiğini varsayıyor, kalabalığın kendi ateş gücünü katlamasını hesaba katmıyor.</p>
      <p>Menzil açıkken kısa menzilli taraf aradaki farkı kendi <code>Speed</code> değeriyle kapatana kadar ateş yiyor ve bu süre onun öldürme süresine ekleniyor. Kaçıp ateş etme hesapta yok.</p>
      <p>Menzil açıkken bir birim gördüğünden uzağa ateş edemiyor. Görüş oyunun hesapladığı gibi: <code>ShroudClearingRange</code>, en uzun silah menzilinin bir buçuk katıyla sınırlı (gözcüler ve inşaatçılar hariç). Türkiye'nin <code>GÖZ</code> etiketli topçusu (Firtina ve Sakarya) 100 görüyor, 500-600 atıyor ve 100'den yakına ateş edemiyor: tek başına hiç atış bulamıyor, bunu bir gözcünün ya da başka bir birimin gözü için yapıyor. "Gözcü var" açılınca herkes menzilinin sonuna kadar görüyor sayılıyor, topçu ancak o zaman menzilinden ateş ediyor.</p>
    </div>
    <div>
      <h2>Hesapta olmayanlar</h2>
      <p>Yalnızca yükseltmesiz silah seti (<code>Conditions = None</code>) kullanılıyor: kompozit zırh, uranyum mermi, terfi ve general güçleri yok. Her atış isabet ediyor, alan hasarı ve zehrin sonradan işleyen hasarı sayılmıyor, bu yüzden Toxin Tractor ve Dragon gerçekte olduğundan zayıf görünüyor. Gatling silahları sürekli ateşin en yüksek kademesinden hesaplanıyor.</p>
      <p>Jarmen Kell'in pilot vuruşu araçlara karşı tek atışlık öldürme sayılıyor, çünkü araç tarafsızlaşıp savaştan çıkıyor. Şarjörü bitince üsse dönen uçaklara her yeni şarjör için 20 saniyelik bir sefer süresi yazıldı; bu bir tahmin, oyundan okunmadı.</p>
      <p class="excluded" id="excluded"></p>
    </div>
  </section>
</div>

<div class="tooltip" id="tooltip" hidden></div>

<script>
window.UNIT_DATA = __UNIT_DATA__;
</script>
<script>
const DATA = window.UNIT_DATA;
const UNITS = DATA.units;
const ATTACKS = DATA.attacks;
const FPS = DATA.fps;

// the sides the game's lobby offers, read from its PlayerTemplate.ini; "main" is each faction's first
const SIDES = DATA.sides;
const FILTERS = [
  { value: "base", label: `Ana taraflar (${DATA.factions.map(f => f.label).join(", ")})`, test: u => SIDES[u.side].main },
  { value: "all", label: "Bütün birimler, generaller dahil", test: () => true },
  ...DATA.factions.map(f => ({ value: "faction:" + f.value, label: `${f.label}, bütün generaller`, test: u => SIDES[u.side].faction === f.value })),
  ...Object.entries(SIDES).sort((a, b) => a[1].order - b[1].order).map(([side, info]) => ({ value: side, label: info.label, test: u => u.side === side })),
];
const KIND_ORDER = { INFANTRY: 0, VEHICLE: 1, AIRCRAFT: 2 };
const LOG_STEPS = [0.15, 0.5, 1, 1.585, 2.322, 3.322, 4.322];
const STRENGTH_CLAMP = 64;

const $ = id => document.getElementById(id);

function kindRank(unit) {
  return unit.kinds.includes("AIRCRAFT") ? KIND_ORDER.AIRCRAFT : unit.kinds.includes("INFANTRY") ? KIND_ORDER.INFANTRY : KIND_ORDER.VEHICLE;
}

function readState() {
  return {
    rowFilter: $("rowFilter").value,
    colFilter: $("colFilter").value,
    mode: document.querySelector("input[name=mode]:checked").value,
    sort: document.querySelector("input[name=sort]:checked").value,
    useRange: $("useRange").checked,
    spotted: $("spotted").checked,
  };
}

// how far a unit shoots in a fight of its own: no further than it sees, unless a spotter sees for it
function reach(unitIndex, attack, state) {
  return state.spotted ? attack.range : Math.min(attack.range, UNITS[unitIndex].sight);
}

function killFrames(attackerIndex, targetIndex, state) {
  const attack = ATTACKS[attackerIndex][targetIndex];
  const reply = ATTACKS[targetIndex][attackerIndex];
  if (!attack) return { frames: Infinity, closing: 0 };
  if (!state.useRange) return { frames: attack.frames, closing: 0 };
  const own = reach(attackerIndex, attack, state);
  // what it can see is inside the distance it may not fire at: alone it never gets a shot
  if (own <= attack.minRange) return { frames: Infinity, closing: 0, blind: true };
  let closing = 0;
  const theirs = reply ? reach(targetIndex, reply, state) : 0;
  if (reply && theirs > own) {
    const attacker = UNITS[attackerIndex];
    closing = attacker.speed > 0 ? (theirs - own) / attacker.speed * FPS : Infinity;
  }
  return { frames: attack.frames + closing, closing };
}

function ratio(rowIndex, colIndex, state) {
  if (rowIndex === colIndex) return ATTACKS[rowIndex][rowIndex] ? 1 : NaN;
  const rowKill = killFrames(rowIndex, colIndex, state).frames;
  const colKill = killFrames(colIndex, rowIndex, state).frames;
  if (rowKill === Infinity && colKill === Infinity) return NaN;
  let value = colKill / rowKill;
  if (state.mode === "cost") value *= UNITS[colIndex].cost / UNITS[rowIndex].cost;
  return value;
}

function bin(value) {
  if (Number.isNaN(value)) return "na";
  const magnitude = Math.abs(Math.log2(value));
  if (magnitude < LOG_STEPS[0]) return "n";
  const step = LOG_STEPS.filter(threshold => magnitude >= threshold).length;
  return (value > 1 ? "w" : "l") + step;
}

function format(value) {
  if (Number.isNaN(value)) return "";
  if (value === Infinity) return "∞";
  if (value === 0) return "0";
  const magnitude = value >= 1 ? value : 1 / value;
  const text = magnitude >= 99.5 ? "99+" : magnitude >= 9.95 ? magnitude.toFixed(0) : magnitude.toFixed(1);
  return value < 1 && text !== "1.0" ? "−" + text : text;
}

function seconds(frames) {
  return frames === Infinity ? "hiç" : (frames / FPS).toFixed(1) + " sn";
}

function strength(index, opponents, state) {
  let sum = 0;
  let count = 0;
  for (const opponent of opponents) {
    if (opponent === index) continue;
    const value = ratio(index, opponent, state);
    if (Number.isNaN(value)) continue;
    sum += Math.log(Math.min(STRENGTH_CLAMP, Math.max(1 / STRENGTH_CLAMP, value)));
    count += 1;
  }
  return count ? sum / count : -Infinity;
}

function pickIndices(filterValue) {
  const filter = FILTERS.find(f => f.value === filterValue);
  return UNITS.map((unit, index) => index).filter(index => filter.test(UNITS[index]));
}

function sideOrder(indices) {
  return indices.slice().sort((a, b) => {
    const ua = UNITS[a], ub = UNITS[b];
    return SIDES[ua.side].order - SIDES[ub.side].order || kindRank(ua) - kindRank(ub) || ua.cost - ub.cost || ua.name.localeCompare(ub.name);
  });
}

// AcquiresBeyondVision: artillery that shoots far past its own sight at what a spotter shows its team
function unitTags(unit) {
  return SIDES[unit.side].tag + (unit.beyondVision ? " GÖZ" : "");
}

function spotterNote(unit) {
  return unit.beyondVision ? ` · kendi görüşü ${unit.sight}, menzilinin gerisini gözcü görür` : "";
}

let view = { rows: [], cols: [], state: null };

function render() {
  const state = readState();
  let rows = sideOrder(pickIndices(state.rowFilter));
  let cols = sideOrder(pickIndices(state.colFilter));
  if (state.sort === "power") {
    const rowPower = new Map(rows.map(i => [i, strength(i, cols, state)]));
    const colPower = new Map(cols.map(i => [i, -strength(i, rows, state)]));
    rows.sort((a, b) => rowPower.get(b) - rowPower.get(a));
    cols.sort((a, b) => colPower.get(a) - colPower.get(b));
  }
  view = { rows, cols, state };

  const grouped = state.sort === "side";
  const groupStart = (list, position) => grouped && position > 0 && UNITS[list[position]].side !== UNITS[list[position - 1]].side;
  const parts = [];
  parts.push('<caption>Satırdaki birimin sütundaki birime karşı güç oranı</caption><thead><tr>');
  parts.push('<th class="corner" scope="col"><div class="corner-key">Hedef →<br><strong>Saldıran</strong> ↓</div></th>');
  cols.forEach((index, position) => {
    const unit = UNITS[index];
    parts.push(`<th scope="col" class="${groupStart(cols, position) ? "group-start" : ""}" title="${unit.name} (${SIDES[unit.side].label})${spotterNote(unit)}"><span class="col-label">${unit.name}<span class="tag">${unitTags(unit)}</span></span></th>`);
  });
  parts.push("</tr></thead><tbody>");
  rows.forEach((rowIndex, rowPosition) => {
    const unit = UNITS[rowIndex];
    parts.push(`<tr class="${groupStart(rows, rowPosition) ? "group-start" : ""}"><th scope="row" title="${unit.name} (${SIDES[unit.side].label})${spotterNote(unit)}">${unit.name}<span class="tag">${unitTags(unit)}</span></th>`);
    cols.forEach((colIndex, colPosition) => {
      const value = ratio(rowIndex, colIndex, state);
      const classes = rowIndex === colIndex ? ["diag", bin(value)] : [bin(value)];
      if (groupStart(cols, colPosition)) classes.push("group-start");
      parts.push(`<td class="${classes.join(" ")}" data-r="${rowPosition}" data-c="${colPosition}">${format(value)}</td>`);
    });
    parts.push("</tr>");
  });
  parts.push("</tbody>");
  $("matrix").innerHTML = parts.join("");

  const cells = rows.length * cols.length;
  $("readout").innerHTML = `<b>${rows.length}</b> × <b>${cols.length}</b> birim · <b>${cells.toLocaleString("tr-TR")}</b> eşleşme<br>toplam ${UNITS.length} savaşan birim okundu`;
}

function describeTooltip(rowPosition, colPosition) {
  const { rows, cols, state } = view;
  const rowIndex = rows[rowPosition];
  const colIndex = cols[colPosition];
  const rowUnit = UNITS[rowIndex];
  const colUnit = UNITS[colIndex];
  const value = ratio(rowIndex, colIndex, state);
  const rowKill = killFrames(rowIndex, colIndex, state);
  const colKill = killFrames(colIndex, rowIndex, state);
  const rowAttack = ATTACKS[rowIndex][colIndex];
  const colAttack = ATTACKS[colIndex][rowIndex];

  let verdict;
  if (rowIndex === colIndex) verdict = rowAttack ? "1× · kendine karşı" : "kendi türüne saldıramıyor";
  else if (Number.isNaN(value)) verdict = "ikisi de birbirine zarar veremiyor";
  else if (value === Infinity) verdict = "∞ · hedef karşılık veremiyor";
  else if (value === 0) verdict = "0 · saldıran zarar veremiyor";
  else if (value >= 1) verdict = `saldıran ${value.toFixed(2)}× güçlü`;
  else verdict = `saldıran ${(1 / value).toFixed(2)}× zayıf`;

  const line = (unit, kill, attack) => {
    const closing = kill.closing > 0 && kill.closing !== Infinity ? ` <span class="tip-weapon">(${(kill.closing / FPS).toFixed(1)} sn yaklaşma dahil)</span>` : "";
    const sight = attack && attack.range > unit.sight
      ? ` · görüş ${unit.sight}, ötesini ${unit.beyondVision ? "bir gözcü görünce" : "ancak başkası görünce"} vurur`
      : "";
    const blind = kill.blind ? ` · en az ${attack.minRange} uzağa ateş ediyor, tek başına hiç atış bulamaz` : "";
    const weapon = attack ? `<br><span class="tip-weapon">${attack.weapon} · menzil ${attack.range}${sight}${blind}</span>` : "";
    return `<span>${unit.name}</span><span>${seconds(kill.frames)}${closing}${weapon}</span>`;
  };
  const costLine = state.mode === "cost" ? `<span>fiyat</span><span>${rowUnit.cost} / ${colUnit.cost}</span>` : "";

  return `<div class="tip-row">
      <span>saldıran</span><span class="tip-title">${rowUnit.name} <span class="tag">${unitTags(rowUnit)}</span></span>
      <span>hedef</span><span class="tip-title">${colUnit.name} <span class="tag">${unitTags(colUnit)}</span></span>
    </div>
    <div class="tip-ratio">${verdict}</div>
    <div class="tip-row">
      <span>öldürür</span><span></span>
      ${line(rowUnit, rowKill, rowAttack)}
      ${line(colUnit, colKill, colAttack)}
      <span>can</span><span>${rowUnit.health} / ${colUnit.health}</span>
      ${costLine}
    </div>`;
}

let hot = [];
function clearHot() {
  hot.forEach(element => element.classList.remove("hot"));
  hot = [];
}

function onPointer(event) {
  const cell = event.target.closest("td");
  const tooltip = $("tooltip");
  if (!cell) {
    tooltip.hidden = true;
    clearHot();
    return;
  }
  const rowPosition = Number(cell.dataset.r);
  const colPosition = Number(cell.dataset.c);
  tooltip.innerHTML = describeTooltip(rowPosition, colPosition);
  tooltip.hidden = false;
  const gap = 16;
  const box = tooltip.getBoundingClientRect();
  let x = event.clientX + gap;
  let y = event.clientY + gap;
  if (x + box.width > window.innerWidth - 8) x = event.clientX - box.width - gap;
  if (y + box.height > window.innerHeight - 8) y = event.clientY - box.height - gap;
  tooltip.style.left = Math.max(8, x) + "px";
  tooltip.style.top = Math.max(8, y) + "px";

  clearHot();
  const table = $("matrix");
  hot = [cell.parentElement.firstElementChild, table.tHead.rows[0].cells[colPosition + 1]];
  hot.forEach(element => element.classList.add("hot"));
}

function buildControls() {
  const options = FILTERS.map(f => `<option value="${f.value}">${f.label}</option>`).join("");
  $("rowFilter").innerHTML = options;
  $("colFilter").innerHTML = options;
  const steps = ["l7", "l6", "l5", "l4", "l3", "l2", "l1", "n", "w1", "w2", "w3", "w4", "w5", "w6", "w7"];
  $("legendBar").innerHTML = steps.map(step => `<span class="${step}" style="background: var(--${step === "n" ? "neutral-cell" : step})"></span>`).join("");
  const excluded = DATA.excluded.map(unit => unit.name);
  const uniqueExcluded = [...new Set(excluded)].sort((a, b) => a.localeCompare(b, "tr"));
  $("excluded").textContent = `Matrise girmeyen ${excluded.length} kayıt, silahı öldürücü olmayan ya da hasarını başka nesnelere yaptıran birimler: ${uniqueExcluded.join(", ")}. Avenger'ın lazerleri, Korkut'un topu, Battle Bus ve Troop Crawler'ın yolcuları ayrı nesneler olduğu için bu hesabın dışında kaldı.`;
}

buildControls();
render();
$("controls").addEventListener("change", render);
$("matrix").addEventListener("pointermove", onPointer);
$("scroll").addEventListener("pointerleave", () => { $("tooltip").hidden = true; clearHot(); });
$("scroll").addEventListener("scroll", () => { $("tooltip").hidden = true; });
</script>
</body>
</html>
'''

if __name__ == "__main__":
    main()
