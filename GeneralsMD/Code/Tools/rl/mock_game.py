"""A fake Zero Hour skirmish that answers the -control training verbs, for testing env.py without the game.

It speaks the same text commands and returns the same JSON shapes as the real socket (status, step,
objects, produce, construct, buildspot, attackmove, move, skirmish, leave, quit), so BuildOrderEnv
cannot tell it from the game. The world is coarse on purpose: build times, costs, prerequisites and
power come from env.ITEMS, Chinooks bring in money while a supply centre stands and the piles last,
and an "Easy AI" base across the map trains a unit every so often and from the fourth game minute
sends its whole army at our CC every two and a half minutes. Combat is hit points equal to value,
damage per second by type, nearest target in range, once a game second. Good enough to see a policy
learn to defend, then attack; not a balance model.
"""

import math
import random

from env import BUILDINGS, FRAMES_PER_GAME_SECOND, ITEMS, KEY_OF_TEMPLATE, base_template

START_MONEY = 10000
PILE_MONEY = 60000
CHINOOK_INCOME_PER_SECOND = 20.0   # 600 a trip, a trip every 30 game seconds
CHINOOKS_PER_SUPPLY_CENTER = 3
HOME, ENEMY_HOME, PILE = (1000.0, 1000.0), (3200.0, 3200.0), (1400.0, 900.0)
SPEED = 40.0                       # world units a game second, everyone
RANGE, PATRIOT_RANGE, SIGHT = 220.0, 450.0, 500.0
DPS = {"ranger": 8, "missile": 12, "humvee": 18, "crusader": 26, "patriot": 45,
       "GLAInfantryRebel": 9, "GLAVehicleTechnical": 18}
ENEMY_BASE = [("GLACommandCenter", 0, 0, 3000), ("GLABarracks", -250, 50, 1000), ("GLASupplyStash", 50, -250, 1000),
              ("GLAArmsDealer", -200, -220, 1000)]
ENEMY_UNITS = [("GLAInfantryRebel", 300), ("GLAVehicleTechnical", 600)]
ENEMY_TRAIN_SECONDS, FIRST_WAVE_SECONDS, WAVE_GAP_SECONDS = 15, 240, 150


def selector_matches(selector, template):
    return selector in ("*", template) or (selector.endswith("*") and template.startswith(selector[:-1]))


class MockGame:
    def __init__(self, passive=False):
        # passive: the seat the game leaves driverless under -takeover 1, a CC and a worker that never move
        self.in_game, self.passive = False, passive

    def close(self):
        pass

    def exchange(self, commands):
        # the game answers produce and construct inside the next step; run in order, that is the same thing here
        return [self.send(command) for command in commands]

    # -- the world ----------------------------------------------------------------------------------

    def _start(self, seed):
        self.rng = random.Random(seed)
        self.in_game, self.frame, self.money, self.pile, self.next_id = True, 1, float(START_MONEY), float(PILE_MONEY), 1
        self.waves = 0
        self.objects = []
        self._add(0, "AmericaCommandCenter", "structure", HOME[0], HOME[1], 2000)
        self._add(0, "AmericaVehicleDozer", "builder", HOME[0] + 100, HOME[1], 1000)
        if self.passive:
            self._add(1, "GLACommandCenter", "structure", ENEMY_HOME[0], ENEMY_HOME[1], 3000)
            self._add(1, "GLAInfantryWorker", "builder", ENEMY_HOME[0] + 100, ENEMY_HOME[1], 200)
            return
        for template, dx, dy, hp in ENEMY_BASE:
            self._add(1, template, "structure", ENEMY_HOME[0] + dx, ENEMY_HOME[1] + dy, hp)
        for _ in range(3):
            self._enemy_unit()

    def _add(self, slot, template, kind, x, y, hp, built=True):
        key = KEY_OF_TEMPLATE.get(template)
        o = {"slot": slot, "template": template, "id": self.next_id, "x": x, "y": y, "kind": kind,
             "underConstruction": not built, "built": 100 if built else 0, "progress": 0.0, "queue": [],
             "headPercent": 0, "hp": float(hp), "goal": None, "dps": DPS.get(key or template, 0),
             "range": PATRIOT_RANGE if key == "patriot" else RANGE}
        if kind == "builder":
            o.update(idle=True, task="idle")
        self.next_id += 1
        self.objects.append(o)
        return o

    def _add_ours(self, key, x, y, built=True):
        kind = "structure" if key == "cc" or key in BUILDINGS else "builder" if key == "dozer" else "unit"
        return self._add(0, ITEMS[key].template, kind, x, y, ITEMS[key].cost, built)

    def _enemy_unit(self):
        template, hp = self.rng.choice(ENEMY_UNITS)
        self._add(1, template, "unit", ENEMY_HOME[0] - 300 + self.rng.uniform(-80, 80),
                  ENEMY_HOME[1] - 300 + self.rng.uniform(-80, 80), hp)

    def _mine(self, slot=0):
        return [o for o in self.objects if o["slot"] == slot]

    def _of(self, key, built=True):
        return [o for o in self._mine() if KEY_OF_TEMPLATE[base_template(o["template"])] == key and (o["built"] == 100) == built]

    def _power(self):
        produced = ITEMS["power"].power * len(self._of("power"))
        consumed = sum(-ITEMS[k].power * len(self._of(k)) for k in ("supply", "factory", "patriot"))
        return produced, consumed

    def _remove(self, o):
        self.objects.remove(o)
        for dozer in self.objects:
            if dozer.get("task") == o["id"]:
                dozer.update(idle=True, task="idle")

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
        gatherers = min(len(self._of("chinook")), CHINOOKS_PER_SUPPLY_CENTER * len(self._of("supply")))
        gain = min(self.pile, gatherers * CHINOOK_INCOME_PER_SECOND / FRAMES_PER_GAME_SECOND)
        self.pile -= gain
        self.money += gain
        self.frame += 1
        if self.frame % FRAMES_PER_GAME_SECOND == 0:
            self._second()

    def _second(self):
        seconds = self.frame // FRAMES_PER_GAME_SECOND
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
                piles = [{"template": "SupplyDock", "id": 0, "x": PILE[0], "y": PILE[1], "kind": "structure"}]
                return {"ok": True, "frame": self.frame, "slot": -1, "objects": piles, "count": 1, "truncated": False}
            if slot not in (0, 1):
                return {"ok": False, "error": "nobody is sitting in that slot"}
            produced, consumed = self._power() if slot == 0 else (0, 0)
            hidden = ("progress", "hp", "goal", "dps", "range", "slot")
            shown = [{k: v for k, v in o.items() if k not in hidden} | {"queue": list(o["queue"])} for o in self._mine(slot)]
            return {"ok": True, "frame": self.frame, "slot": slot, "money": int(self.money) if slot == 0 else 0,
                    "defeated": not shown, "power": {"produced": produced, "consumed": consumed},
                    "objects": shown, "count": len(shown), "truncated": False}
        if verb in ("attackmove", "move"):
            for o in self._mine(slot):
                if o["kind"] != "structure" and selector_matches(words[2], o["template"]):
                    o["goal"] = (float(words[3]), float(words[4]))
            return {"ok": True, "queued": True}
        if verb == "produce":
            building, unit, count = words[2], KEY_OF_TEMPLATE.get(base_template(words[3])), int(words[4])
            producers = [o for o in self._mine() if building in (o["template"], str(o["id"])) and o["built"] == 100]
            if unit is None or not producers or KEY_OF_TEMPLATE[base_template(producers[0]["template"])] != ITEMS[unit].source:
                return {"ok": False, "error": "nothing here can build that"}
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
            if key not in BUILDINGS or ITEMS[key].source and not self._of(ITEMS[key].source):
                return {"ok": False, "error": "can't build that"}
            if verb == "buildspot":
                return {"ok": True, "x": x + self.rng.uniform(-20, 20), "y": y + self.rng.uniform(-20, 20)}
            idle = [o for o in self._mine() if o["kind"] == "builder" and o["idle"]]
            if not idle or self.money < ITEMS[key].cost:
                return {"ok": False, "error": "no idle builder" if not idle else "not enough money"}
            self.money -= ITEMS[key].cost
            site = self._add_ours(key, x, y, built=False)
            idle[0].update(idle=False, task=site["id"])
            return {"ok": True, "id": site["id"]}
        return {"ok": False, "error": "unknown command"}
