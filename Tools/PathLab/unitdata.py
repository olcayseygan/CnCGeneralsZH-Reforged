"""
unitdata - the ground units PathLab drives, read from the game's own INIs at run time.

Nothing here carries a number of EA's: every value comes out of the install the way the engine
loads it.  The archives in Run (Patch*.big over the rest, a bad archive skipped), a loose file under
Run over every archive, the fork's masters under Code/Data over both (Tools/Balance/unitmatrix.py's
GameFiles); Locomotor.ini, then the four Reforged files patched over it field by field
(GameEngine.cpp:1281-1288); the Object directory, then the same four files over every object.

The values stay as the INI writes them, per game second; pathlab.py converts them to its ticks.
ZHR_RUN_DIR points it at another Run, as it does unitmatrix.py and siege.py.

    python unitdata.py                 # the seven units' values, as read
"""

import re
import sys
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Balance"))

import unitmatrix  # noqa: E402

# The units PathLab knows, by the name a group spec gives them.
UNIT_OBJECTS: dict[str, str] = {
    "crusader": "AmericaTankCrusader",
    "humvee": "AmericaVehicleHumvee",
    "overlord": "ChinaTankOverlord",
    "battlemaster": "ChinaTankBattleMaster",
    "technical": "GLAVehicleTechnicalChassisOne",
    "scorpion": "GLATankScorpion",
    "ranger": "AmericaInfantryRanger",
}
LOCOMOTOR_FILE: str = "Data/INI/Locomotor.ini"
OBJECT_DIRECTORY: str = "Data/INI/Object"
PATCH_FILES: tuple[str, ...] = ("Data/INI/BalanceReforged.ini", "Data/INI/FixesReforged.ini",
                                "Data/INI/TurkiyeReforged.ini", "Data/INI/TalentsReforged.ini")
RETAIL_LOGICFRAMES_PER_SECOND: float = 30.0     # parseFrictionPerSec divides by it
# LocomotorTemplate::LocomotorTemplate's defaults that are not zero, raw INI units
UNSET_DAMAGED: float = -1.0
DEFAULT_BRAKING: float = 39999596.0
DEFAULT_MIN_TURN_SPEED: float = 1999980.0
DEFAULT_CLOSE_ENOUGH: float = 1.0
# PhysicsUpdate.cpp:63-64, the share of speed lost per retail frame
DEFAULT_FORWARD_FRICTION: float = 0.15
DEFAULT_LATERAL_FRICTION: float = 0.15
PHYSICS_MODULE = re.compile(r"Behavior\s*=\s*PhysicsBehavior\b", re.I)


class InstallMissing(RuntimeError):
    """No game data under RUN_DIR to read the units from."""


@dataclass(frozen=True)
class LocomotorData:
    """LocomotorTemplate (Locomotor.cpp:588-658), raw: speeds per second, turns in degrees a second,
       accelerations per second squared, the friction as a share per retail frame."""
    name: str
    appearance: str = "OTHER"
    speed: float = 0.0
    speed_damaged: float = UNSET_DAMAGED
    turn_rate: float = 0.0
    turn_rate_damaged: float = UNSET_DAMAGED
    acceleration: float = 0.0
    acceleration_damaged: float = UNSET_DAMAGED
    braking: float = DEFAULT_BRAKING
    min_speed: float = 0.0
    min_turn_speed: float = DEFAULT_MIN_TURN_SPEED
    turn_pivot_offset: float = 0.0
    can_move_backwards: bool = False
    extra_2d_friction: float = 0.0
    close_enough_dist: float = DEFAULT_CLOSE_ENOUGH
    wander_width_factor: float = 0.0

    def validated(self) -> "LocomotorData":
        """LocomotorTemplate::validate: an omitted damaged value is the undamaged one."""
        return replace(
            self,
            speed_damaged=self.speed if self.speed_damaged < 0.0 else self.speed_damaged,
            turn_rate_damaged=self.turn_rate if self.turn_rate_damaged < 0.0 else self.turn_rate_damaged,
            acceleration_damaged=(self.acceleration if self.acceleration_damaged < 0.0
                                  else self.acceleration_damaged))


@dataclass(frozen=True)
class UnitData:
    """What PathLab needs of one object: its locomotor, its footprint and its PhysicsBehavior."""
    key: str
    object_name: str
    locomotor: LocomotorData
    infantry: bool
    geometry: str = "SPHERE"
    major_radius: float = 0.0
    minor_radius: float = 0.0
    speed_scale: float = 1.0
    forward_friction: float = DEFAULT_FORWARD_FRICTION
    lateral_friction: float = DEFAULT_LATERAL_FRICTION

    @property
    def bounding_radius(self) -> float:
        """GeometryInfo::calcBoundingStuff (Geometry.cpp:500): a box's corner, else the major radius."""
        if self.geometry == "BOX":
            return (self.major_radius ** 2 + self.minor_radius ** 2) ** 0.5
        return self.major_radius


class GameFiles(unitmatrix.GameFiles):
    """unitmatrix's resolver, skipping an archive that is not a BIG (a *.big whose magic is FILE
       stops unitmatrix's whole index on this machine)."""

    def _index(self, big: Path, overwrite: bool) -> None:
        try:
            super()._index(big, overwrite)
        except ValueError:
            pass


LOCOMOTOR_REALS: dict[str, str] = {
    "speed": "speed", "speeddamaged": "speed_damaged", "turnrate": "turn_rate",
    "turnratedamaged": "turn_rate_damaged", "acceleration": "acceleration",
    "accelerationdamaged": "acceleration_damaged", "braking": "braking", "minspeed": "min_speed",
    "minturnspeed": "min_turn_speed", "turnpivotoffset": "turn_pivot_offset",
    "closeenoughdist": "close_enough_dist", "wanderwidthfactor": "wander_width_factor",
}


def patch_locomotor(locomotor: LocomotorData, children: list) -> LocomotorData:
    """One Locomotor block over what the name already holds: a later block changes only its fields."""
    changes: dict[str, object] = {}
    for line, _ in children:
        key, value = unitmatrix.field(line)
        if not value:
            continue
        if key in LOCOMOTOR_REALS:
            changes[LOCOMOTOR_REALS[key]] = unitmatrix.real(value[0])
        elif key == "appearance":
            changes["appearance"] = value[0].upper()
        elif key == "canmovebackwards":
            changes["can_move_backwards"] = unitmatrix.yes(value[0])
        elif key == "extra2dfriction":
            changes["extra_2d_friction"] = unitmatrix.real(value[0]) / RETAIL_LOGICFRAMES_PER_SECOND
    return replace(locomotor, **changes)


def patch_object(fields: dict[str, object], children: list, top: bool) -> None:
    """The object fields PathLab reads, wherever the block nests them: Geometry and SpeedScale at the
       top, `Locomotor = SET_NORMAL` in its AIUpdate module, the frictions in PhysicsBehavior."""
    for line, nested in children:
        if nested is not None:
            if PHYSICS_MODULE.match(line):
                patch_physics(fields, nested)
            else:
                patch_object(fields, nested, False)
            continue
        key, value = unitmatrix.field(line)
        if not value:
            continue
        if key == "locomotor" and value[0].upper() == "SET_NORMAL" and len(value) > 1:
            fields["locomotor"] = value[1].lower()
        elif top and key == "geometry":
            fields["geometry"] = value[0].upper()
        elif top and key == "geometrymajorradius":
            fields["major_radius"] = unitmatrix.real(value[0])
        elif top and key == "geometryminorradius":
            fields["minor_radius"] = unitmatrix.real(value[0])
        elif top and key == "speedscale":
            fields["speed_scale"] = unitmatrix.real(value[0])
        elif top and key == "kindof":
            fields["infantry"] = any(word.upper() == "INFANTRY" for word in value)


def patch_physics(fields: dict[str, object], children: list) -> None:
    for line, _ in children:
        key, value = unitmatrix.field(line)
        if key == "forwardfriction" and value:
            fields["forward_friction"] = unitmatrix.real(value[0]) / RETAIL_LOGICFRAMES_PER_SECOND
        elif key == "lateralfriction" and value:
            fields["lateral_friction"] = unitmatrix.real(value[0]) / RETAIL_LOGICFRAMES_PER_SECOND


def blocks(files: GameFiles, paths: Iterable[str]) -> Iterator[tuple[str, list[str], list]]:
    for path in paths:
        text = files.read(path)
        if text is not None:
            yield from unitmatrix.ini_blocks(text.decode("latin-1"))


def install_present() -> bool:
    return any(unitmatrix.RUN_DIR.glob("*.big"))


def load_units(keys: Iterable[str] = tuple(UNIT_OBJECTS)) -> dict[str, UnitData]:
    """The named units, read from the install.  InstallMissing when there is none to read."""
    if not install_present():
        raise InstallMissing(f"no game archives in {unitmatrix.RUN_DIR} (set ZHR_RUN_DIR)")
    keys = [key.lower() for key in keys]
    unknown = [key for key in keys if key not in UNIT_OBJECTS]
    if unknown:
        raise KeyError(f"unknown unit {', '.join(unknown)}; PathLab knows {', '.join(UNIT_OBJECTS)}")
    files = GameFiles()
    wanted = {UNIT_OBJECTS[key].lower(): key for key in keys}
    objects: dict[str, dict[str, object]] = {}
    object_files = files.list_dir(OBJECT_DIRECTORY)
    for keyword, args, children in blocks(files, [*object_files, *PATCH_FILES]):
        if keyword.lower() == "object" and args and args[0].lower() in wanted:
            patch_object(objects.setdefault(args[0].lower(), {}), children, True)
    locomotors: dict[str, LocomotorData] = {}
    for keyword, args, children in blocks(files, [LOCOMOTOR_FILE, *PATCH_FILES]):
        if keyword.lower() == "locomotor" and args:
            name = args[0].lower()
            locomotors[name] = patch_locomotor(locomotors.get(name, LocomotorData(args[0])), children)
    units: dict[str, UnitData] = {}
    for object_name, key in wanted.items():
        fields = objects.get(object_name)
        if fields is None or fields.get("locomotor") not in locomotors:
            raise InstallMissing(f"{UNIT_OBJECTS[key]} or its locomotor is not in the data under {unitmatrix.RUN_DIR}")
        locomotor = locomotors[fields.pop("locomotor")].validated()
        units[key] = UnitData(key=key, object_name=UNIT_OBJECTS[key], locomotor=locomotor,
                              infantry=bool(fields.pop("infantry", False)), **fields)
    return units


def main() -> None:
    for key, unit in load_units().items():
        loco = unit.locomotor
        print(f"{key:<13} {loco.name:<28} {loco.appearance:<12} speed {loco.speed:g} ({loco.speed_damaged:g})  "
              f"turn {loco.turn_rate:g}  accel {loco.acceleration:g}  brake {loco.braking:g}  "
              f"minturn {loco.min_turn_speed:g}  pivot {loco.turn_pivot_offset:g}  back {loco.can_move_backwards}  "
              f"{unit.geometry} {unit.major_radius:g}/{unit.minor_radius:g} r {unit.bounding_radius:.2f}  "
              f"fric {unit.forward_friction:.4f}/{unit.lateral_friction:.4f}+{loco.extra_2d_friction:.4f}  "
              f"close {loco.close_enough_dist:g}  wander {loco.wander_width_factor:g}  scale {unit.speed_scale:g}  "
              f"infantry {unit.infantry}")


if __name__ == "__main__":
    main()
