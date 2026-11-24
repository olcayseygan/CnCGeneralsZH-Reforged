"""A fake Zero Hour skirmish that answers the -control training verbs, for testing env.py without the game.

It speaks the same text commands and returns the same JSON shapes as the real socket (status, step,
objects, produce, construct, buildspot, skirmish, leave, quit), so BuildOrderEnv cannot tell it from
the game. The world is coarse on purpose: build times, costs, prerequisites and power come from
env.ITEMS, Chinooks bring in money while a supply centre stands and the piles last, and from the
sixth game minute the "Easy AI" sends a wave every two minutes that the army has to absorb or the
base pays for. Good enough to see a policy learn to put economy first; not a balance model.
"""

import random

from env import ARMY, BUILDINGS, FRAMES_PER_GAME_SECOND, ITEMS, KEY_OF_TEMPLATE, base_template

START_MONEY = 10000
PILE_MONEY = 60000
CHINOOK_INCOME_PER_SECOND = 20.0   # 600 a trip, a trip every 30 game seconds
CHINOOKS_PER_SUPPLY_CENTER = 3
FIRST_WAVE_SECONDS, WAVE_GAP_SECONDS = 360, 120


class MockGame:
    def __init__(self):
        self.in_game = False

    def close(self):
        pass

    def exchange(self, commands):
        # the game answers produce and construct inside the next step; run in order, that is the same thing here
        return [self.send(command) for command in commands]

    # -- the world ----------------------------------------------------------------------------------

    def _start(self, seed):
        self.rng = random.Random(seed)
        self.in_game, self.frame, self.money, self.pile, self.next_id = True, 1, float(START_MONEY), float(PILE_MONEY), 1
        self.enemy_units, self.waves = 10, 0
        self.objects = []
        self._add("cc", 1000.0, 1000.0, built=True)
        self._add("dozer", 1100.0, 1000.0, built=True)

    def _add(self, key, x, y, built):
        kind = "structure" if key == "cc" or key in BUILDINGS else "builder" if key == "dozer" else "unit"
        o = {"template": ITEMS[key].template, "id": self.next_id, "x": x, "y": y, "kind": kind,
             "underConstruction": not built, "built": built, "progress": 0.0, "queue": [], "headPercent": 0}
        if kind == "builder":
            o.update(idle=True, task="idle")
        self.next_id += 1
        self.objects.append(o)
        return o

    def _of(self, key, built=True):
        return [o for o in self.objects if KEY_OF_TEMPLATE[base_template(o["template"])] == key and o["built"] == built]

    def _tick(self):
        produced = sum(ITEMS["power"].power for _ in self._of("power"))
        consumed = sum(-ITEMS[k].power for k in ("supply", "factory") for _ in self._of(k))
        speed = 1.0 if consumed <= produced else 0.5
        for o in list(self.objects):
            key = KEY_OF_TEMPLATE[base_template(o["template"])]
            frames = ITEMS[key].seconds * FRAMES_PER_GAME_SECOND
            if o["underConstruction"]:
                o["progress"] += 1.0 / frames
                if o["progress"] >= 1.0:
                    o.update(underConstruction=False, built=True)
                    for dozer in self.objects:
                        if dozer.get("task") == o["id"]:
                            dozer.update(idle=True, task="idle")
                    if key == "supply":
                        self._add("chinook", o["x"], o["y"], built=True)  # the free one it comes with
            elif o["queue"]:
                unit = o["queue"][0]
                o["progress"] += speed / (ITEMS[unit].seconds * FRAMES_PER_GAME_SECOND)
                if o["progress"] >= 1.0:
                    o["queue"].pop(0)
                    o["progress"] = 0.0
                    self._add(unit, o["x"] + 50, o["y"], built=True)
                o["headPercent"] = int(o["progress"] * 100)
        gatherers = min(len(self._of("chinook")), CHINOOKS_PER_SUPPLY_CENTER * len(self._of("supply")))
        gain = min(self.pile, gatherers * CHINOOK_INCOME_PER_SECOND / FRAMES_PER_GAME_SECOND)
        self.pile -= gain
        self.money += gain
        self.frame += 1
        if self.frame >= (FIRST_WAVE_SECONDS + WAVE_GAP_SECONDS * self.waves) * FRAMES_PER_GAME_SECOND:
            self._wave()

    def _wave(self):
        strength = (1500 + 700 * self.waves) * self.rng.uniform(0.8, 1.2)
        self.waves += 1
        army = [o for o in self.objects if KEY_OF_TEMPLATE[base_template(o["template"])] in ARMY]
        value = sum(ITEMS[KEY_OF_TEMPLATE[o["template"]]].cost for o in army)
        if value >= 3 * strength:
            self.enemy_units = max(0, self.enemy_units - 5)
        loss = strength * (0.5 if value >= strength else 1.0)
        self.rng.shuffle(army)
        # army first, then the base, buildings before the CC and the dozers
        victims = army + sorted((o for o in self.objects if o not in army),
                                key=lambda o: (o["kind"] != "structure", o["template"] == ITEMS["cc"].template))
        for o in victims:
            if loss <= 0:
                break
            loss -= ITEMS[KEY_OF_TEMPLATE[o["template"]]].cost
            self.objects.remove(o)
            for dozer in self.objects:
                if dozer.get("task") == o["id"]:
                    dozer.update(idle=True, task="idle")

    # -- the socket ---------------------------------------------------------------------------------

    def send(self, command):
        words = command.split()
        verb = words[0]
        if verb == "status":
            players = [{"slot": 0, "units": len(self.objects)}, {"slot": 1, "units": self.enemy_units}] if self.in_game else []
            return {"ok": True, "frame": self.frame if self.in_game else 0, "inGame": self.in_game, "players": players}
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
            return {"ok": True, "frame": self.frame}
        slot = int(words[1])
        if verb == "objects":
            if slot == -1:
                piles = [{"template": "SupplyPile", "id": 0, "x": 1400.0, "y": 900.0, "kind": "structure"}]
                return {"ok": True, "frame": self.frame, "slot": -1, "objects": piles, "count": 1, "truncated": False}
            if slot != 0:
                return {"ok": False, "error": "no such slot"}
            produced = sum(ITEMS["power"].power for _ in self._of("power"))
            consumed = sum(-ITEMS[k].power for k in ("supply", "factory") for _ in self._of(k))
            shown = [{k: v for k, v in o.items() if k != "progress"} | {"queue": len(o["queue"])} for o in self.objects]
            return {"ok": True, "frame": self.frame, "slot": 0, "money": int(self.money), "defeated": not self.objects,
                    "power": {"produced": produced, "consumed": consumed}, "objects": shown,
                    "count": len(shown), "truncated": False}
        if verb == "produce":
            building, unit, count = words[2], KEY_OF_TEMPLATE.get(base_template(words[3])), int(words[4])
            producers = [o for o in self.objects if building in (o["template"], str(o["id"])) and o["built"]]
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
            idle = [o for o in self.objects if o["kind"] == "builder" and o["idle"]]
            if not idle or self.money < ITEMS[key].cost:
                return {"ok": False, "error": "no idle builder" if not idle else "not enough money"}
            self.money -= ITEMS[key].cost
            site = self._add(key, x, y, built=False)
            idle[0].update(idle=False, task=site["id"])
            return {"ok": True, "id": site["id"]}
        return {"ok": False, "error": "unknown command"}
