"""Make the Easy and Medium skirmish AI ask for the plan buildings EA's scripts forget.

A map's skirmish base plan only goes up where a script asks for it with SKIRMISH_BUILD_BUILDING.
Two asks are missing from the retail file:

- The Superweapon General's "USA Barracks - N" waits for BUILT_BY_PLAYER(SupW_AmericaBarracks),
  the building it is meant to put up, so a Medium Superweapon General never builds a barracks.
  Every other side's barracks script, and this side's Easy one, waits for the power plant; this
  points it at SupW_AmericaPowerPlant. The Air Force General's "USA Strategy Center - N" waits for
  its own AirF_AmericaStrategyCenter the same way; every other side's waits for the war factory,
  and so does this one now.
- No "Base Building - Easy" group asks for an airfield, so an Easy AI never builds one. Every side
  whose Normal group has "<Side> Air Field - N" gets a copy of it, renamed "- E" and flagged Easy
  only, at the end of its Easy group. The Tank General and the GLA have no airfield script and get
  none.

usage: plan_asks.py <in.scb> <out.scb>

The master is the retail file through opening.py H 1 4 5 6, then infantry_drop.py, then
ai_bonus.py, then sell_off.py, then this.
"""
import copy
import sys

import scbtool

EASY, NORMAL, HARD = 6, 7, 8
OBJECT_TYPE = 15
EXPECTED_AIRFIELDS = 7
# Normal-rung first copies that wait on themselves. Second copies ("Supply Drop 2", "Black
# Market2", "Barracks 2", "Nuke Silo 2", "2nd Power Plant") wait on the first on purpose.
SELF_GATES = {
    "SkirmishAmericaSuperWeaponGeneral": [("USA Barracks - N", "SupW_AmericaBarracks", "SupW_AmericaPowerPlant")],
    "SkirmishAmericaAirForceGeneral": [("USA Strategy Center - N", "AirF_AmericaStrategyCenter", "AirF_AmericaWarFactory")],
}


def groups(chunks):
    for chunk in chunks:
        if chunk.name == "ScriptGroup":
            yield chunk
        yield from groups(chunk.children)


def fix_self_gate(script_list, name, wrong, right):
    """Point the script's one BUILT_BY_PLAYER(wrong) at right."""
    for group, script in scbtool.walk_scripts(script_list.children):
        if group != "USA Base Building - Normal" or script.fields[0] != name:
            continue
        gates = [p for orc in script.children if orc.name == "OrCondition" for c in orc.children
                 if c.fields[1] == "BUILT_BY_PLAYER" for p in c.fields[2] if p[0] == OBJECT_TYPE]
        if len(gates) != 1 or gates[0][3] != wrong:
            raise SystemExit(f"{name!r} waits for {[p[3] for p in gates]}, not {wrong!r}; already fixed?")
        gates[0][3] = right
        return f"{name} waits for {right}"
    raise SystemExit(f"no {name!r} in the list")


def add_easy_airfield(index, script_list):
    by_name = {group.fields[0]: group for group in groups(script_list.children)}
    for family in ("USA", "China"):
        normal, easy = by_name.get(f"{family} Base Building - Normal"), by_name.get(f"{family} Base Building - Easy")
        if not normal or not easy:
            continue
        source = next((s for s in normal.children if s.name == "Script" and s.fields[0] == f"{family} Air Field - N"), None)
        if source is None:
            return None
        name = f"{family} Air Field - E"
        if any(s.name == "Script" and s.fields[0] == name for s in easy.children):
            raise SystemExit(f"[{index}] already has {name!r}")
        script = copy.deepcopy(source)
        script.fields[0] = name
        script.fields[EASY], script.fields[NORMAL], script.fields[HARD] = 1, 0, 0
        easy.children.append(script)
        return f"[{index}] {name}"
    return None


def main():
    source, target = sys.argv[1], sys.argv[2]
    table, chunks = scbtool.load(source)
    players = next(chunk for chunk in chunks if chunk.name == "ScriptsPlayers").fields[1]
    list_chunk = next(chunk for chunk in chunks if chunk.name == "PlayerScriptsList")
    added = []
    fixed = 0
    for index, script_list in enumerate(list_chunk.children):
        for name, wrong, right in SELF_GATES.get(players[index][0], ()):
            print(f"[{index}] {fix_self_gate(script_list, name, wrong, right)}")
            fixed += 1
        line = add_easy_airfield(index, script_list)
        if line:
            added.append(line)
    if fixed != sum(len(gates) for gates in SELF_GATES.values()):
        raise SystemExit(f"fixed {fixed} self-gated scripts; a player list is missing")
    if len(added) != EXPECTED_AIRFIELDS:
        raise SystemExit(f"added {len(added)} Easy airfield scripts, expected {EXPECTED_AIRFIELDS}")
    print("\n".join(added))
    scbtool.save(target, table, chunks)


if __name__ == "__main__":
    main()
