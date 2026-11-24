"""A fake Zero Hour skirmish that answers the -control training verbs, for testing env.py without the game.

It speaks the same text commands and returns the same JSON shapes as the real socket (status, step,
objects, produce, construct, buildspot, sell, science, attackmove, attack, move, stop, skirmish, leave,
quit, `#id` selectors included), so BuildOrderEnv cannot tell it from the game. The world is coarse
on purpose: build times, costs, prerequisites and power come from env.ITEMS, Chinooks bring in money
from the pile their supply centre stands by until its boxes run out, the first general's point comes
at five minutes, and an "Easy AI" base across the map
trains a unit every so often, sends its whole army at our CC every two and a half minutes from the
fourth game minute, and leaves a hole where a structure falls that rebuilds it unless destroyed.
Combat is hit points equal to value, damage per second by type, nearest target in range, once a game
second. Good enough to see a policy learn to defend, then attack; not a balance model.
"""

import math
import random

from env import FRAMES_PER_GAME_SECOND, ITEMS, KEY_OF_TEMPLATE, NEEDS_SCIENCE, STRUCTURES, base_template

START_MONEY = 10000
CHINOOK_INCOME_PER_SECOND = 20.0   # 600 a trip, a trip every 30 game seconds
CHINOOKS_PER_SUPPLY_CENTER = 3
PILE_RADIUS = 500.0
SELL_REFUND = 0.5
HOME, ENEMY_HOME = (1000.0, 1000.0), (3200.0, 3200.0)
# the home pile, two expansions and one by the enemy base
PILES = [(1400.0, 900.0, 30000.0), (2300.0, 1100.0, 40000.0), (1100.0, 2300.0, 40000.0), (2900.0, 3500.0, 40000.0)]
PILE_ID = 9000
BOX_MONEY = 100.0
SCIENCE_POINT_SECONDS = 300        # the first general's point comes with the first promotion, about here
SPEED = 40.0                       # world units a game second, everyone
RANGE, LONG_RANGE, SIGHT = 220.0, 450.0, 500.0
LONG_RANGED = {"patriot", "tomahawk"}
DPS = {"ranger": 8, "missile": 12, "humvee": 18, "crusader": 26, "paladin": 32, "tomahawk": 30, "comanche": 30,
       "patriot": 45, "GLAInfantryRebel": 9, "GLAVehicleTechnical": 18}
ENEMY_BASE = [("GLACommandCenter", 0, 0, 3000), ("GLABarracks", -250, 50, 1000), ("GLASupplyStash", 50, -250, 1000),
              ("GLAArmsDealer", -200, -220, 1000)]
ENEMY_UNITS = [("GLAInfantryRebel", 300), ("GLAVehicleTechnical", 600)]
ENEMY_TRAIN_SECONDS, FIRST_WAVE_SECONDS, WAVE_GAP_SECONDS = 15, 240, 150
HOLE_HP, HOLE_REBUILD_SECONDS = 300, 45


def selector_matches(selector, o):
    if selector.startswith("#"):
        return str(o["id"]) in selector[1:].split(",")
    template = o["template"]
    return selector in ("*", template) or (selector.endswith("*") and template.startswith(selector[:-1]))


class MockGame:
    def __init__(self, passive=False):
        # passive: the seat the game leaves driverless under -takeover 1, a CC and a worker that never move
        self.in_game, self.passive = False, passive

    def close(self):
        pass

    def exchange(self, commands):
        # the game answers produce, construct and sell inside the next step; run in order, that is the same here
        return [self.send(command) for command in commands]

    # -- the world ----------------------------------------------------------------------------------

    def _start(self, seed):
        self.rng = random.Random(seed)
        self.in_game, self.frame, self.money, self.next_id = True, 1, float(START_MONEY), 1
        self.piles = [{"id": PILE_ID + n, "x": x, "y": y, "money": money} for n, (x, y, money) in enumerate(PILES)]
        self.waves = 0
        self.objects = []
        self.sciences = set()
        self._add(0, "AmericaCommandCenter", "structure", HOME[0], HOME[1], 2000)
        self._add(0, "AmericaVehicleDozer", "builder", HOME[0] + 100, HOME[1], 1000)
        self._add(1, "GLACommandCenter", "structure", ENEMY_HOME[0], ENEMY_HOME[1], 3000)
        self._add(1, "GLAInfantryWorker", "builder", ENEMY_HOME[0] + 100, ENEMY_HOME[1], 200)
        if self.passive:
            return
        for template, dx, dy, hp in ENEMY_BASE[1:]:
            self._add(1, template, "structure", ENEMY_HOME[0] + dx, ENEMY_HOME[1] + dy, hp)
        for _ in range(3):
            self._enemy_unit()

    def _add(self, slot, template, kind, x, y, hp, built=True):
        key = KEY_OF_TEMPLATE.get(template)
        o = {"slot": slot, "template": template, "id": self.next_id, "x": x, "y": y, "kind": kind,
             "underConstruction": not built, "built": 100 if built else 0, "progress": 0.0, "queue": [],
             "headPercent": 0, "hp": float(hp), "maxhp": float(hp), "goal": None, "target": None,
             "dps": DPS.get(key or template, 0), "range": LONG_RANGE if key in LONG_RANGED else RANGE}
        if kind == "builder":
            o.update(idle=True, task="idle")
        self.next_id += 1
        self.objects.append(o)
        return o

    def _add_ours(self, key, x, y, built=True):
        kind = "structure" if key == "cc" or key in STRUCTURES else "builder" if key == "dozer" else "unit"
        return self._add(0, ITEMS[key].template, kind, x, y, ITEMS[key].cost, built)

    def _enemy_unit(self):
        template, hp = self.rng.choice(ENEMY_UNITS)
        self._add(1, template, "unit", ENEMY_HOME[0] - 300 + self.rng.uniform(-80, 80),
                  ENEMY_HOME[1] - 300 + self.rng.uniform(-80, 80), hp)

    def _mine(self, slot=0):
        return [o for o in self.objects if o["slot"] == slot]

    def _of(self, key, built=True):
        return [o for o in self._mine() if KEY_OF_TEMPLATE[base_template(o["template"])] == key and (o["built"] == 100) == built]

    def _ready(self, item):
        return not item.needs or any(self._of(need) for need in item.needs)

    def _power(self):
        produced = ITEMS["power"].power * len(self._of("power"))
        consumed = sum(-ITEMS[k].power * len(self._of(k)) for k in STRUCTURES if ITEMS[k].power < 0)
        return produced, consumed

    def _remove(self, o):
        self.objects.remove(o)
        for dozer in self.objects:
            if dozer.get("task") == o["id"]:
                dozer.update(idle=True, task="idle")
        if o["slot"] == 1 and o["kind"] == "structure" and o["template"] != "GLAHole" and not self.passive:
            hole = self._add(1, "GLAHole", "structure", o["x"], o["y"], HOLE_HP)
            hole["rebuild"] = (o["template"], o["maxhp"], self.frame // FRAMES_PER_GAME_SECOND + HOLE_REBUILD_SECONDS)

    def _gather(self):
        chinooks = len(self._of("chinook"))
        for centre in self._of("supply"):
            near = [p for p in self.piles if p["money"] > 0 and math.hypot(p["x"] - centre["x"], p["y"] - centre["y"]) <= PILE_RADIUS]
            if not near:
                continue
            working = min(chinooks, CHINOOKS_PER_SUPPLY_CENTER)
            chinooks -= working
            pile = near[0]
            gain = min(pile["money"], working * CHINOOK_INCOME_PER_SECOND / FRAMES_PER_GAME_SECOND)
            pile["money"] -= gain
            self.money += gain

    def _tick(self):
        produced, consumed = self._power()
        speed = 1.0 if consumed <= produced else 0.5
        for o in self._mine():
            key = KEY_OF_TEMPLATE[base_template(o["template"])]
            if o["underConstruction"]:
                o["progress"] += 1.0 / (ITEMS[key].seconds * FRAMES_PER_GAME_SECOND)
                if o["progress"] >= 1.0:
                    o.update(underConstruction=False, built=100)
                    for dozer in self.objects:
                        if dozer.get("task") == o["id"]:
                            dozer.update(idle=True, task="idle")
                    if key == "supply":
                        self._add_ours("chinook", o["x"], o["y"])  # the free one it comes with
            elif o["queue"]:
                unit = o["queue"][0]
                o["progress"] += speed / (ITEMS[unit].seconds * FRAMES_PER_GAME_SECOND)
                if o["progress"] >= 1.0:
                    o["queue"].pop(0)
                    o["progress"] = 0.0
                    self._add_ours(unit, o["x"] + 60, o["y"] + 60)
                o["headPercent"] = int(o["progress"] * 100)
        self._gather()
        self.frame += 1
        if self.frame % FRAMES_PER_GAME_SECOND == 0:
            self._second()

    def _second(self):
        seconds = self.frame // FRAMES_PER_GAME_SECOND
        for hole in [o for o in self._mine(1) if o.get("rebuild") and o["rebuild"][2] <= seconds]:
            template, hp, _ = hole["rebuild"]
            self.objects.remove(hole)
            self._add(1, template, "structure", hole["x"], hole["y"], hp)
        enemy_army = [o for o in self._mine(1) if o["kind"] == "unit"]
        if not self.passive and seconds % ENEMY_TRAIN_SECONDS == 0 and self._mine(1) and len(enemy_army) < 4 + seconds // 60:
            self._enemy_unit()
        if not self.passive and seconds >= FIRST_WAVE_SECONDS + WAVE_GAP_SECONDS * self.waves:
            self.waves += 1
            for o in enemy_army:
                o["goal"] = HOME
        # everyone armed shoots the nearest enemy in range, else walks to its goal, else goes for one in sight
        damage = []
        for o in self.objects:
            if not o["dps"] or o["underConstruction"]:
                continue
            foes = [(math.hypot(f["x"] - o["x"], f["y"] - o["y"]), f) for f in self.objects if f["slot"] != o["slot"]]
            if not foes:
                continue
            distance, foe = min(foes, key=lambda pair: pair[0])
            if distance <= o["range"]:
                damage.append((foe, o["dps"]))
                continue
            goal = o["goal"] or ((foe["x"], foe["y"]) if distance <= SIGHT else None)
            if goal and o["kind"] != "structure":
                dx, dy = goal[0] - o["x"], goal[1] - o["y"]
                length = math.hypot(dx, dy)
                step = min(SPEED, length)
                if length > 0:
                    o["x"] += dx / length * step
                    o["y"] += dy / length * step
                if o["goal"] and length <= SPEED:
                    o["goal"] = None
        for foe, dps in damage:
            foe["hp"] -= dps
        for o in [o for o in self.objects if o["hp"] <= 0]:
            self._remove(o)

    # -- the socket ---------------------------------------------------------------------------------

    def _describe(self, o):
        hidden = ("progress", "hp", "maxhp", "goal", "target", "dps", "range", "slot", "rebuild")
        shown = {k: v for k, v in o.items() if k not in hidden} | {"queue": list(o["queue"])}
        if o["kind"] != "structure":
            shown["health"] = round(100.0 * o["hp"] / o["maxhp"])
        return shown

    def send(self, command):
        words = command.split()
        verb = words[0]
        if verb == "status":
            players = [{"slot": slot, "units": len(self._mine(slot))} for slot in (0, 1)] if self.in_game else []
            return {"ok": True, "cmd": verb, "frame": self.frame if self.in_game else 0, "inGame": self.in_game, "players": players}
        if verb == "skirmish":
            if self.in_game:
                return {"ok": False, "error": "a match is already running; quit it first"}
            self._start(int(words[2]))
            return {"ok": True, "starting": True}
        if verb in ("leave", "quit"):
            self.in_game = False
            return {"ok": True}
        if not self.in_game:
            return {"ok": False, "error": "no match"}
        if verb == "step":
            if words[1] == "release":
                return {"ok": True, "frame": self.frame}
            frames = int(words[1])
            if not 1 <= frames <= 300:
                return {"ok": False, "error": "step wants 1..300 frames"}
            for _ in range(frames):
                self._tick()
            return {"ok": True, "cmd": verb, "frame": self.frame}
        slot = int(words[1])
        if verb == "objects":
            if slot == -1:
                # a spent dock stays on the map with no boxes, as the game's warehouse docks do
                piles = [{"template": "SupplyDock", "id": p["id"], "x": p["x"], "y": p["y"], "kind": "structure",
                          "boxes": math.ceil(p["money"] / BOX_MONEY)} for p in self.piles]
                return {"ok": True, "frame": self.frame, "slot": -1, "objects": piles, "count": len(piles), "truncated": False}
            if slot not in (0, 1):
                return {"ok": False, "error": "nobody is sitting in that slot"}
            produced, consumed = self._power() if slot == 0 else (0, 0)
            shown = [self._describe(o) for o in self._mine(slot)]
            return {"ok": True, "frame": self.frame, "slot": slot, "money": int(self.money) if slot == 0 else 0,
                    "defeated": not shown, "power": {"produced": produced, "consumed": consumed},
                    "objects": shown, "count": len(shown), "truncated": False}
        if verb in ("attackmove", "move", "attack", "stop"):
            group = [o for o in self._mine(slot) if o["kind"] != "structure" and selector_matches(words[2], o)]
            if verb == "attack":
                targets = [o for o in self._mine(int(words[3])) if selector_matches(words[4], o)]
                goal, target = ((targets[0]["x"], targets[0]["y"]), targets[0]["id"]) if targets else (None, None)
            else:
                goal, target = ((float(words[3]), float(words[4])) if verb != "stop" else None), None
            for o in group:
                o["goal"], o["target"] = goal, target
            return {"ok": True, "queued": True}
        if verb == "science":
            if words[2] in self.sciences:
                return {"ok": False, "cmd": verb, "error": "%s is already owned" % words[2]}
            if self.frame < SCIENCE_POINT_SECONDS * FRAMES_PER_GAME_SECOND:
                return {"ok": False, "cmd": verb, "error": "%s costs 1 points, slot %d has 0" % (words[2], slot)}
            self.sciences.add(words[2])
            return {"ok": True, "cmd": verb, "science": words[2], "cost": 1, "pointsLeft": 0}
        if verb == "sell":
            site = next((o for o in self._mine(slot) if str(o["id"]) == words[2].lstrip("#")), None)
            if site is None or site["kind"] != "structure" or site["underConstruction"]:
                return {"ok": False, "error": "no living structure %s of slot %d" % (words[2], slot)}
            self._remove(site)
            self.money += SELL_REFUND * ITEMS[KEY_OF_TEMPLATE[base_template(site["template"])]].cost
            return {"ok": True, "id": site["id"], "template": site["template"]}
        if verb == "produce":
            building, unit, count = words[2], KEY_OF_TEMPLATE.get(base_template(words[3])), int(words[4])
            producers = [o for o in self._mine() if building in (o["template"], str(o["id"])) and o["built"] == 100]
            if unit is None or not producers or KEY_OF_TEMPLATE[base_template(producers[0]["template"])] != ITEMS[unit].source:
                return {"ok": False, "error": "nothing here can build that"}
            if NEEDS_SCIENCE.get(unit, "") not in self.sciences | {""} or not self._ready(ITEMS[unit]):
                return {"ok": False, "error": "%s: prerequisites not met" % words[3]}
            queued = 0
            for _ in range(count):
                if self.money < ITEMS[unit].cost:
                    break
                min(producers, key=lambda o: len(o["queue"]))["queue"].append(unit)
                self.money -= ITEMS[unit].cost
                queued += 1
            return {"ok": True, "queued": queued} if queued else {"ok": False, "error": "not enough money"}
        if verb in ("construct", "buildspot"):
            key = KEY_OF_TEMPLATE.get(base_template(words[2]))
            x, y = float(words[3]), float(words[4])
            if key not in STRUCTURES + ["cc"] or not self._ready(ITEMS[key]):
                return {"ok": False, "error": "can't build that"}
            if verb == "buildspot":
                return {"ok": True, "x": x + self.rng.uniform(-20, 20), "y": y + self.rng.uniform(-20, 20)}
            idle = [o for o in self._mine() if o["kind"] == "builder" and o["idle"]]
            if not idle or self.money < ITEMS[key].cost:
                return {"ok": False, "error": "no idle builder" if not idle else "not enough money"}
            self.money -= ITEMS[key].cost
            site = self._add_ours(key, x, y, built=False)
            idle[0].update(idle=False, task=site["id"])
            return {"ok": True, "id": site["id"], "cost": ITEMS[key].cost}
        return {"ok": False, "error": "unknown command"}
