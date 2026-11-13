"""Checks for maploader.py - stdlib unittest, synthetic data only, except one test that loads a
retail map and skips when the install is not there.

    python -m unittest test_maploader -v        (from Tools/PathLab)

The footprint cases are worked by hand from internal_classifyObjectFootprint (AIPathfind.cpp)."""

import math
import os
import tempfile
import unittest
from collections import Counter

import maploader
import pathlab
from maploader import (CLEAR, ENGINE_CLEAR, ENGINE_CLIFF, ENGINE_OBSTACLE, ENGINE_WATER, IMPASSABLE,
                       OBSTACLE, Bounds, Placement, Template, classify_map, parse_templates,
                       point_in_polygon, repinch, skip_reason, stamp_circle, stamp_object,
                       stamp_rectangle)

SIZE = 20


def cells_of(types: bytearray, width: int, kind: int) -> set[tuple[int, int]]:
    return {(index % width, index // width) for index, value in enumerate(types) if value == kind}


def open_ground(size: int = SIZE) -> tuple[bytearray, bytearray]:
    return bytearray(size * size), bytearray(size * size)


def structure(geometry: str, major: float, minor: float = 1.0) -> Template:
    return Template({"STRUCTURE", "IMMOBILE"}, geometry, major, minor, False)


class CellTypes(unittest.TestCase):
    def test_match_pathlab(self) -> None:
        self.assertEqual((CLEAR, OBSTACLE, IMPASSABLE), (pathlab.CLEAR, pathlab.OBSTACLE, pathlab.IMPASSABLE))


class Templates(unittest.TestCase):
    DEFAULT = "Object DefaultThingTemplate\n  Geometry = SPHERE\n  GeometryIsSmall = Yes\nEnd\n"

    def test_fields_of_a_module_are_not_the_objects(self) -> None:
        text = self.DEFAULT + """
Object Hangar
  KindOf = STRUCTURE IMMOBILE
  Draw = W3DModelDraw ModuleTag_01
    ConditionState = NONE
      Model = Hangar
    End
  End
  Behavior = AutoFindHealingUpdate ModuleTag_02
    KindOf = VEHICLE
  End
  Turret
    TurretTurnRate = 60
  End
  Geometry = BOX         ; the object's own, after the modules
  GeometryMajorRadius = 40.0
  GeometryMinorRadius = 20.0
  GeometryIsSmall = No
End
"""
        hangar = parse_templates([text])["hangar"]
        self.assertEqual(hangar.kinds, {"STRUCTURE", "IMMOBILE"})
        self.assertEqual((hangar.geometry, hangar.major_radius, hangar.minor_radius, hangar.is_small),
                         ("BOX", 40.0, 20.0, False))

    def test_kindof_lists_replace_and_edits_edit(self) -> None:
        text = "Object Ruin\n  KindOf = IMMOBILE\n  KindOf = STRUCTURE SELECTABLE\nEnd\n"
        templates = parse_templates([text])
        self.assertEqual(templates["ruin"].kinds, {"STRUCTURE", "SELECTABLE"})
        parse_templates(["Object Ruin\n  KindOf = +IMMOBILE -SELECTABLE\nEnd\n"], templates)
        self.assertEqual(templates["ruin"].kinds, {"STRUCTURE", "IMMOBILE"})

    def test_new_objects_start_from_the_default_and_reskins_copy(self) -> None:
        text = self.DEFAULT + """
Object Silo
  KindOf = STRUCTURE IMMOBILE
  GeometryMajorRadius = 12
End
ObjectReskin BigSilo Silo
  GeometryMajorRadius = 30
End
ObjectReskin Ghost Nothing
  GeometryMajorRadius = 99
End
"""
        templates = parse_templates([text])
        self.assertTrue(templates["silo"].is_small)
        self.assertEqual(templates["bigsilo"].kinds, {"STRUCTURE", "IMMOBILE"})
        self.assertEqual((templates["silo"].major_radius, templates["bigsilo"].major_radius), (12.0, 30.0))
        self.assertNotIn("ghost", templates)


class Terrain(unittest.TestCase):
    def test_a_cliff_grows_one_cell_and_rings_itself_with_pinched_ground(self) -> None:
        size = 9
        cliff, water = bytearray(size * size), bytearray(size * size)
        cliff[4 * size + 4] = 1
        types, pinched = classify_map(cliff, water, size, size)
        self.assertEqual(cells_of(types, size, ENGINE_CLIFF), {(x, y) for x in range(3, 6) for y in range(3, 6)})
        ring = {(x, y) for x in range(2, 7) for y in range(2, 7)} - cells_of(types, size, ENGINE_CLIFF)
        self.assertEqual(cells_of(pinched, size, 1) & cells_of(types, size, ENGINE_CLEAR), ring)

    def test_water_beats_cliff_and_does_not_grow(self) -> None:
        size = 5
        cliff, water = bytearray(size * size), bytearray(size * size)
        cliff[12] = water[12] = 1
        types, pinched = classify_map(cliff, water, size, size)
        self.assertEqual(cells_of(types, size, ENGINE_WATER), {(2, 2)})
        self.assertEqual(sum(pinched), 0)

    def test_point_in_polygon(self) -> None:
        square = [(0, 0), (100, 0), (100, 100), (0, 100)]
        self.assertTrue(point_in_polygon(square, 50, 50))
        self.assertFalse(point_in_polygon(square, 150, 50))
        self.assertFalse(point_in_polygon(square, 50, -1))


class Footprints(unittest.TestCase):
    def test_box(self) -> None:
        """6 x 2 samples five units apart from (40, 60): x snaps to cells 4-6, y to rows 6 and 5."""
        types, _ = open_ground()
        stamp_rectangle(types, SIZE, SIZE, 55.0, 55.0, 0.0, 15.0, 5.0, 15.0, Bounds(5, 5, 5, 5))
        self.assertEqual(cells_of(types, SIZE, ENGINE_OBSTACLE), {(x, y) for x in range(4, 7) for y in (5, 6)})

    def test_box_turns_with_the_object(self) -> None:
        types, _ = open_ground()
        stamp_rectangle(types, SIZE, SIZE, 55.0, 55.0, math.pi / 2, 15.0, 5.0, 15.0, Bounds(5, 5, 5, 5))
        self.assertEqual(cells_of(types, SIZE, ENGINE_OBSTACLE), {(5, 4), (5, 5), (5, 6)})

    def test_last_column_is_never_stamped(self) -> None:
        types, _ = open_ground()
        stamp_rectangle(types, SIZE, SIZE, SIZE * 10.0 - 5.0, 55.0, 0.0, 15.0, 5.0, 15.0, Bounds(0, 0, 0, 0))
        self.assertEqual({x for x, _ in cells_of(types, SIZE, ENGINE_OBSTACLE)}, {SIZE - 2})

    def test_cylinder(self) -> None:
        """Radius 1.4 cells round (5, 5): only the four centres half a cell off on both axes."""
        types, _ = open_ground()
        stamp_circle(types, SIZE, SIZE, 50.0, 50.0, 10.0, Bounds(5, 5, 5, 5))
        self.assertEqual(cells_of(types, SIZE, ENGINE_OBSTACLE), {(4, 4), (5, 4), (4, 5), (5, 5)})

    def test_obstacles_land_only_on_clear_ground(self) -> None:
        types, _ = open_ground()
        types[5 * SIZE + 5] = ENGINE_CLIFF
        stamp_circle(types, SIZE, SIZE, 50.0, 50.0, 10.0, Bounds(5, 5, 5, 5))
        self.assertEqual(types[5 * SIZE + 5], ENGINE_CLIFF)

    def test_ground_beside_a_footprint_is_pinched(self) -> None:
        types, pinched = open_ground()
        stamp_object(types, pinched, SIZE, SIZE, Placement("Silo", 50.0, 50.0, 0.0, 0.0, 0),
                     structure("CYLINDER", 10.0))
        for cell in ((3, 4), (6, 5), (4, 3), (5, 6)):
            self.assertTrue(pinched[cell[1] * SIZE + cell[0]], cell)
        self.assertFalse(pinched[5 * SIZE + 8])

    def test_repinch_clears_flags_it_no_longer_earns(self) -> None:
        types, pinched = open_ground()
        pinched[10 * SIZE + 10] = 1
        repinch(types, pinched, SIZE, SIZE, Bounds(10, 10, 10, 10))
        self.assertEqual(sum(pinched), 0)

    def test_fence_takes_no_pinched_pass(self) -> None:
        types, pinched = open_ground()
        fence = Template({"IMMOBILE", "CLEARED_BY_BUILD"}, "BOX", 15.0, 2.0, False, 30.0, 0.0)
        stamp_object(types, pinched, SIZE, SIZE, Placement("Wall", 100.0, 100.0, 0.0, 0.0, 0), fence)
        self.assertTrue(cells_of(types, SIZE, ENGINE_OBSTACLE))
        self.assertEqual(sum(pinched), 0)


class Screening(unittest.TestCase):
    def test_what_leaves_no_footprint(self) -> None:
        on_ground = Placement("X", 50.0, 50.0, 0.0, 0.0, 0)
        building = structure("BOX", 20.0, 20.0)
        self.assertIsNone(skip_reason(on_ground, building))
        cases = {
            "no template": (on_ground, None),
            "road or bridge piece": (Placement("Road", 0.0, 0.0, 0.0, 0.0, maploader.FLAG_ROAD_FLAGS), building),
            "prop": (on_ground, Template({"PROP", "STRUCTURE", "IMMOBILE"}, "BOX", 20.0, 20.0, False)),
            "fluff": (on_ground, Template({"CLEARED_BY_BUILD", "STRUCTURE", "IMMOBILE"}, "BOX", 20.0, 20.0, False)),
            "not a structure": (on_ground, Template({"IMMOBILE"}, "BOX", 20.0, 20.0, False)),
            "mobile": (on_ground, Template({"STRUCTURE"}, "BOX", 20.0, 20.0, False)),
            "small": (on_ground, Template({"STRUCTURE", "IMMOBILE"}, "BOX", 20.0, 20.0, True)),
            "in the air": (Placement("X", 50.0, 50.0, 11.0, 0.0, 0), building),
        }
        for reason, (placement, template) in cases.items():
            self.assertEqual(skip_reason(placement, template), reason)
        fence = Template({"CLEARED_BY_BUILD"}, "BOX", 15.0, 2.0, True, 30.0)
        self.assertIsNone(skip_reason(on_ground, fence))


class Picture(unittest.TestCase):
    def test_png_header(self) -> None:
        size = 4
        kinds = bytearray([ENGINE_CLEAR, ENGINE_WATER, ENGINE_CLIFF, ENGINE_OBSTACLE] * size)
        loaded = maploader.LoadedMap("synthetic", size, size, bytearray(size * size), bytearray(size * size),
                                     kinds, [(15.0, 15.0)], {"Center": (20.0, 20.0)}, Counter(), Counter(),
                                     Counter(), 0.0)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "grid.png")
            maploader.write_png(loaded, path, 2)
            with open(path, "rb") as handle:
                head = handle.read(24)
        self.assertEqual(head[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(int.from_bytes(head[16:20], "big"), size * 2)


GAME_DIR = maploader.game_dir_of(None)
HAVE_INSTALL = all(os.path.isfile(os.path.join(GAME_DIR, name))
                   for name in (maploader.MAPS_ARCHIVE, maploader.INI_ARCHIVE))


@unittest.skipUnless(HAVE_INSTALL, "no Zero Hour install at %s" % GAME_DIR)
class RetailMap(unittest.TestCase):
    def test_default_map_loads(self) -> None:
        loaded = maploader.load_map()
        self.assertGreater(loaded.width, 0)
        self.assertEqual(len(loaded.kind), loaded.width * loaded.height)
        self.assertGreaterEqual(len(loaded.starts), 2)
        for x, y in loaded.starts:
            cell = int(y // pathlab.CELL) * loaded.width + int(x // pathlab.CELL)
            self.assertEqual(loaded.kind[cell], CLEAR)
        self.assertTrue({CLEAR, OBSTACLE, IMPASSABLE} <= set(loaded.kind))
        self.assertGreater(sum(loaded.stamped.values()), 0)
        grid = pathlab.Grid(loaded.width, loaded.height)
        grid.kind[:] = loaded.kind
        self.assertTrue(grid.passable(*pathlab.cell_of(*loaded.starts[0])))


if __name__ == "__main__":
    unittest.main()
