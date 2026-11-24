"""The flow field's repair must land where a full rebuild lands, and its routes must end on a source."""

import random
import unittest

import apocflow as af
from pathlab import CLEAR, IMPASSABLE, OBSTACLE, Grid


def small_world(seed: int) -> tuple[Grid, list[af.Structure]]:
    rng = random.Random(seed)
    g = Grid(60, 60)
    for y in range(60):
        g.kind[y * 60 + 30] = IMPASSABLE            # a wall down the middle with two gaps
    for y in (12, 13, 44, 45, 46):
        g.kind[y * 60 + 30] = CLEAR
    structures = []
    while len(structures) < 12:
        x, y, w, h = rng.randrange(2, 55), rng.randrange(2, 55), rng.randint(1, 3), rng.randint(1, 3)
        cells = [(j * 60 + i) for j in range(y, min(60, y + h)) for i in range(x, min(60, x + w))]
        if all(g.kind[k] == CLEAR for k in cells):
            for k in cells:
                g.kind[k] = OBSTACLE
            structures.append(af.Structure(len(structures), "b", cells, 1.0))
    g.set_kind(0, 0, g.kind[0])
    return g, structures


class FieldTests(unittest.TestCase):
    def test_repair_matches_rebuild(self):
        for seed in range(6):
            g, structures = small_world(seed)
            for radius in (0, 1):
                f = af.FlowField(g, radius)
                f.build(structures)
                for dead in structures[:4]:
                    dead.alive = False
                    for k in dead.cells:
                        g.kind[k] = CLEAR
                    g.set_kind(0, 0, g.kind[0])
                    f.repair(dead, structures)
                    fresh = af.FlowField(g, radius)
                    fresh.build(structures)
                    self.assertEqual(f.dist, fresh.dist, f"seed {seed} radius {radius} after {dead.sid}")
                g, structures = small_world(seed)

    def test_route_ends_on_a_live_source(self):
        g, structures = small_world(1)
        f = af.FlowField(g, 0)
        f.build(structures)
        for start in (5 * 60 + 5, 50 * 60 + 55, 30 * 60 + 10):
            if f.dist[start] < af.INF:
                end = f.route(start)[-1]
                self.assertEqual(f.dist[end], 0)
                self.assertTrue(structures[f.label[end]].alive)


if __name__ == "__main__":
    unittest.main()
