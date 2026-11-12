"""Checks for pathlab.py - stdlib unittest, no window, no pygame display.

    python -m unittest test_pathlab -v        (from Tools/PathLab)

Each cost rule the search charges has a test that holds it to the C++ number, worked by hand from
the formula in AIPathfind.cpp; the rest are the parts of the group order and the following that a
picture cannot argue with."""

import math
import unittest

import pathlab
from pathlab import (CELL, COST_DIAGONAL, COST_ORTHOGONAL, OBSTACLE, Grid, Path, Pathfinder,
                     SearchCell, Simulation, Unit, cell_of, cell_pos, cost_to_goal, crossing_cost,
                     lane_corner, lane_count, make_map, retail_frames, stuck_rung, traffic_cost,
                     turn_cost, wall_hug_cost)


def walled(size: int = 21) -> Grid:
    """Open ground with a wall round the edge."""
    grid = Grid(size, size)
    for i in range(size):
        grid.block(i, 0)
        grid.block(i, size - 1)
        grid.block(0, i)
        grid.block(size - 1, i)
    return grid


def put(pf: Pathfinder, st: dict, cell: tuple[int, int], parent: tuple[int, int] | None, cost: int) -> int:
    w = pf.grid.width
    index = cell[1] * w + cell[0]
    entry = SearchCell()
    entry.cost = cost
    entry.parent = parent[1] * w + parent[0] if parent else None
    st[index] = entry
    return index


class TimeTests(unittest.TestCase):
    def test_a_game_second_is_twenty_ticks_at_150(self):
        self.assertEqual(pathlab.LOGICFRAMES_PER_GAME_SECOND, 20)
        self.assertEqual(pathlab.PF_CLAIM_BUCKET_FRAMES, 10)

    def test_retail_frame_counts_round_like_convert_retail_frames(self):
        self.assertEqual([retail_frames(n) for n in (2, 8, 10, 12, 24, 45, 66, 90, 150)],
                         [1, 5, 7, 8, 16, 30, 44, 60, 100])


class CostRuleTests(unittest.TestCase):
    def test_the_estimate_is_octile_halves_times_9_over_4_with_flow(self):
        self.assertEqual(cost_to_goal(10, 4, 9, 4), 270)        # (100 + 20) * 9 / 4
        self.assertEqual(cost_to_goal(-4, 10, 3, 2), 180)       # retail's 3/2, no object

    def test_a_turn_costs_the_hull_floored_by_retail_and_capped(self):
        self.assertEqual(turn_cost(math.pi / 4, 19, 28), 14)
        self.assertEqual(turn_cost(math.pi / 2, 19, 28), 28)    # 29 capped mid-route
        self.assertEqual(turn_cost(math.pi, 19, 60), 59)        # the first step's higher cap
        self.assertEqual(turn_cost(math.pi, 0, 60), 0)          # no hull, no charge

    def test_cost_so_far_charges_step_pinched_and_turn(self):
        pf = Pathfinder(walled(10))
        pf.chassis = 19
        st: dict = {}
        put(pf, st, (2, 2), None, 0)
        b = put(pf, st, (3, 2), (2, 2), 10)
        c = put(pf, st, (3, 3), (3, 2), 0)
        self.assertEqual(pf._cost_so_far(st, c, b), 10 + 10 + 28)      # a right angle
        self.assertEqual(pf._cost_so_far(st, put(pf, st, (4, 3), (3, 2), 0), b), 10 + 14 + 14)
        pf.chassis = 0
        self.assertEqual(pf._cost_so_far(st, c, b), 10 + 10 + 8)       # retail's flat 8

    def test_the_first_step_is_charged_against_the_facing(self):
        pf = Pathfinder(walled(10))
        pf.chassis, pf.start_dir = 19, (1.0, 0.0)
        st: dict = {}
        a = put(pf, st, (4, 4), None, 0)
        self.assertEqual(pf._cost_so_far(st, put(pf, st, (5, 4), (4, 4), 0), a), 10)
        self.assertEqual(pf._cost_so_far(st, put(pf, st, (3, 4), (4, 4), 0), a), 10 + 59)

    def test_a_pinched_cell_costs_fourteen_in_cost_so_far(self):
        grid = Grid(9, 9)
        for x in range(9):
            for y in range(9):
                if y != 4:
                    grid.block(x, y)
        pf = Pathfinder(grid)
        st: dict = {}
        a = put(pf, st, (3, 4), None, 0)
        self.assertEqual(pf._cost_so_far(st, put(pf, st, (4, 4), (3, 4), 0), a), 10 + 14)

    def test_wall_hug_is_the_square_of_the_lost_slack(self):
        self.assertEqual(wall_hug_cost(10, 7, 7), 16)            # no slack: 8/5 of a step
        self.assertEqual(wall_hug_cost(10, 0, 7), 16)            # inside the body is the same
        self.assertEqual(wall_hug_cost(10, 13, 7), 4)            # slack 6
        self.assertEqual(wall_hug_cost(10, 20, 7), 0)            # slack 13: open ground

    def test_traffic_is_four_steps_at_full(self):
        self.assertEqual([traffic_cost(10, t) for t in (0, 96, 192, 255)], [0, 20, 40, 40])

    def test_crossing_is_half_a_step_a_whole_unit_capped_at_four(self):
        self.assertEqual([crossing_cost(10, h) for h in (1, 2, 5, 8, 20)], [0, 5, 10, 20, 20])

    def test_flow_step_cost_adds_the_three_charges(self):
        pf = Pathfinder(walled(21))
        unit = Unit(*cell_pos(10, 10, False), id=1)
        pf.begin_flow_search(unit)
        self.assertEqual(pf.flow_speed, int(unit.max_speed * 0.8 * 256))
        self.assertEqual((pf.h_num, pf.h_den), (9, 4))
        self.assertEqual(pf.flow_step_cost(10, 10, 10, 1, 0), 0)
        self.assertEqual(pf.flow_step_cost(10, 1, 10, 1, 0), 16)     # beside the wall: need 7, have 5
        pf.note_traffic(*cell_pos(10, 10, True), 0)
        self.assertEqual(pf.flow_step_cost(10, 10, 10, 1, 0), traffic_cost(10, 24))
        pf.stamp_claim_at(*cell_pos(10, 10, True), 0, 0)
        self.assertEqual(pf.flow_step_cost(10, 10, 10, 1, 0), traffic_cost(10, 24) + 5)

    def test_without_an_object_the_search_is_retail(self):
        pf = Pathfinder(walled(10))
        pf.begin_flow_search(None)
        self.assertEqual((pf.h_num, pf.h_den, pf.flow_speed, pf.chassis), (3, 2, 0, 0))


class MapTests(unittest.TestCase):
    def test_pinched_is_a_narrow_cell_or_one_beside_a_structure(self):
        corridor = Grid(9, 9)
        for x in range(9):
            for y in range(9):
                if y != 4:
                    corridor.block(x, y)
        self.assertTrue(corridor.pinched[4 * 9 + 4])
        grid = Grid(9, 9)
        self.assertFalse(grid.pinched[4 * 9 + 4])
        grid.set_kind(4, 4, OBSTACLE)
        self.assertTrue(grid.pinched[3 * 9 + 4])                    # orthogonal to the obstacle
        self.assertFalse(grid.pinched[3 * 9 + 3])                   # diagonal is not

    def test_clearance_is_a_5_7_chamfer_from_the_edge(self):
        grid = walled(11)
        c = grid.clearance
        self.assertEqual((c[1 * 11 + 1], c[2 * 11 + 2], c[5 * 11 + 2]), (5, 10, 10))
        self.assertEqual(c[5 * 11 + 5], 25)

    def test_the_footprint_comes_from_the_bounding_circle(self):
        self.assertEqual(pathlab.radius_and_center(11.0), (1, False))     # a tank: 2x2 on a corner
        self.assertEqual(pathlab.radius_and_center(4.0), (0, True))       # a soldier: his own cell
        self.assertEqual(pathlab.radius_and_center(40.0), (2, True))      # capped at 2

    def test_a_block_split_by_a_wall_is_two_zones(self):
        grid = Grid(10, 10)
        for y in range(10):
            grid.block(5, y)
        zones = grid.block_zones(10)
        self.assertNotEqual(zones[2], zones[8])
        self.assertEqual(zones[5], 0)

    def test_the_coarse_route_steps_block_to_neighbouring_block(self):
        grid = Grid(60, 60)
        for y in range(60):
            grid.block(30, y)
            grid.block(31, y)
        grid.open_span(30, 39, 40)
        nodes, pushes = pathlab.find_block_route(grid, (5, 40), (55, 40))
        self.assertGreater(pushes, 0)
        self.assertEqual((nodes[0], nodes[-1]), ((5, 40), (55, 40)))
        corridor = pathlab.BlockCorridor(nodes)
        self.assertTrue(corridor.is_open(35, 40))
        self.assertFalse(corridor.is_open(35, 5))


class SearchTests(unittest.TestCase):
    def search(self, grid, unit, start, goal, **kw):
        pf = Pathfinder(grid)
        pf.begin_flow_search(unit)
        return pf, pf.search(unit, start, goal, corridor=kw.get("corridor"), cap=kw.get("cap", 40000),
                             partial=kw.get("partial", True))

    def test_a_diagonal_needs_one_open_side(self):
        grid = walled(5)
        grid.block(2, 1)
        grid.block(1, 2)
        _, (reached, _, _) = self.search(grid, None, (1, 1), (2, 2))
        self.assertIsNone(reached)                                    # both sides shut: no corner cut
        grid.unblock(2, 1)
        _, (reached, st, _) = self.search(grid, None, (1, 1), (2, 2))
        self.assertEqual(st[reached].parent, 1 * 5 + 1)              # straight across the corner

    def test_a_capped_search_hands_back_the_closest_cell(self):
        unit = Unit(*cell_pos(2, 2, False), id=1)
        grid = walled(41)
        for y in range(1, 39):
            grid.block(20, y)                                         # one long wall to go round
        _, (reached, _, cells) = self.search(grid, unit, (2, 2), (38, 2), cap=50)
        self.assertGreater(cells, 50)
        self.assertIsNotNone(reached)
        self.assertNotEqual(reached, 2 * 41 + 38)

    def test_find_path_straightens_open_ground_to_two_nodes(self):
        sim = Simulation(walled(30), [])
        unit = Unit(*cell_pos(5, 15, False), id=1)
        path = sim.pf.find_path(unit, unit.pos, cell_pos(25, 15, False))
        self.assertEqual(len(path.route), 2)
        self.assertEqual(path.last, cell_pos(25, 15, False))

    def test_traffic_sends_the_route_through_the_other_gap(self):
        def two_gaps() -> Grid:
            grid = walled(30)
            for y in range(1, 29):
                grid.block(15, y)
                grid.block(16, y)
            grid.open_span(15, 8, 12)
            grid.open_span(15, 17, 21)
            return grid

        def gap_rows(path: Path) -> set[int]:
            return {cy for cx, cy in (cell_of(*p) for p in path.nodes) if cx in (15, 16)}

        unit = Unit(*cell_pos(5, 19, False), id=1)
        sim = Simulation(two_gaps(), [])
        self.assertTrue(all(y >= 17 for y in gap_rows(sim.pf.find_path(unit, unit.pos, cell_pos(26, 19, False)))))
        sim = Simulation(two_gaps(), [])
        # a whole queue's worth: the 9/4 estimate only promises a route within 9/4 of the best, so a
        # small jam is driven through, which is the game's answer too
        for y in range(14, 28):
            for x in range(6, 26):
                for _ in range(8):
                    sim.pf.note_traffic(*cell_pos(x, y, True), 0)
        rows = gap_rows(sim.pf.find_path(unit, unit.pos, cell_pos(26, 19, False)))
        self.assertTrue(rows and all(y <= 12 for y in rows), rows)

    def test_a_parked_ally_is_charged_and_marks_the_route(self):
        parked = Unit(*cell_pos(10, 10, False), id=2)
        sim = Simulation(walled(21), [parked])
        info = sim.pf.check_for_movement(Unit(0, 0, id=1), 10, 10, False)
        self.assertEqual(info.ally_fixed, 1)
        moving = Unit(*cell_pos(10, 10, False), id=3)
        sim.units.append(moving)
        sim.pf.update_pos(moving)
        sim.pf.remove_goal(parked)
        info = sim.pf.check_for_movement(Unit(0, 0, id=1), 10, 10, False)
        self.assertTrue(info.ally_moving)
        self.assertEqual(info.ally_fixed, 0)                         # moving units are not checked

    def test_a_tank_stays_off_a_parked_one(self):
        parked = Unit(*cell_pos(15, 15, False), id=2)
        sim = Simulation(walled(30), [parked])
        unit = Unit(*cell_pos(5, 15, False), id=1)
        path = sim.pf.find_path(unit, unit.pos, cell_pos(25, 15, False))
        cells = {cell_of(*p) for p in path.nodes}
        self.assertFalse(cells & {(14, 14), (15, 14), (14, 15), (15, 15)})


class OptimizeTests(unittest.TestCase):
    def test_collinear_nodes_on_open_ground_collapse(self):
        pf = Pathfinder(walled(30))
        nodes = [cell_pos(x, 10, True) for x in range(3, 20)]
        self.assertEqual(pf.optimize(None, nodes, False), [0, len(nodes) - 1])

    def test_a_wall_keeps_the_corner(self):
        grid = walled(30)
        for y in range(1, 10):
            grid.block(10, y)
        pf = Pathfinder(grid)
        nodes = ([cell_pos(5, y, True) for y in range(3, 13)] + [cell_pos(x, 12, True) for x in range(6, 16)]
                 + [cell_pos(15, y, True) for y in range(11, 2, -1)])
        route = pf.optimize(None, nodes, False)
        self.assertGreater(len(route), 2)
        for a, b in zip(route, route[1:]):
            self.assertTrue(all(grid.passable(*c) for _, c in pathlab.line_cells(*cell_of(*nodes[a]), *cell_of(*nodes[b])))
                            or a + 1 == b)

    def test_a_straight_run_passes_through_pinched_ground(self):
        grid = Grid(20, 9)
        for x in range(20):
            for y in range(9):
                if y != 4:
                    grid.block(x, y)
        pf = Pathfinder(grid)
        nodes = [cell_pos(x, 4, True) for x in range(2, 18)]
        self.assertFalse(pf.is_line_passable(None, nodes[0], nodes[-1], blocked=False, allow_pinched=False))
        self.assertEqual(pf.optimize(None, nodes, False), [0, len(nodes) - 1])     # dy == 0 all the way

    def test_the_lookahead_is_32_nodes(self):
        pf = Pathfinder(walled(60))
        nodes = [cell_pos(x, 30, True) for x in range(2, 50)]
        self.assertEqual(pf.optimize(None, nodes, False), [0, 32, len(nodes) - 1])

    def test_a_line_check_sees_a_parked_unit_and_moving_ones_only_when_blocked(self):
        sim = Simulation(walled(30), [Unit(*cell_pos(15, 15, False), id=2)])
        me = Unit(*cell_pos(5, 15, False), id=1)
        a, b = me.pos, cell_pos(25, 15, False)
        self.assertFalse(sim.pf.is_line_passable(me, a, b, blocked=False, allow_pinched=True))
        sim.pf.remove_goal(sim.units[0])                             # now it reads as moving
        self.assertTrue(sim.pf.is_line_passable(me, a, b, blocked=False, allow_pinched=True))
        self.assertFalse(sim.pf.is_line_passable(me, a, b, blocked=True, allow_pinched=True))


class GroupTests(unittest.TestCase):
    def test_lane_count_is_the_ground_capped_at_five_and_the_members(self):
        self.assertEqual([lane_count(0, 25, 0), lane_count(25, 25, 0), lane_count(1000, 25, 0),
                          lane_count(1000, 25, 3)], [1, 2, 5, 3])

    def test_a_lane_corner_moves_along_the_bisector(self):
        sx, sy = lane_corner((0, 0), (10, 0), (20, 0), 5.0)
        self.assertAlmostEqual(sx, 0.0)
        self.assertAlmostEqual(sy, 5.0)
        sx, sy = lane_corner((0, 0), (10, 0), (10, 10), 5.0)
        self.assertAlmostEqual(sx, -5.0)
        self.assertAlmostEqual(sy, 5.0)

    def test_the_flood_hands_every_member_its_own_spot(self):
        grid = walled(40)
        units = [Unit(*cell_pos(5 + 3 * k, 5, False), id=k + 1) for k in range(6)]
        sim = Simulation(grid, units)
        click = cell_pos(30, 30, True)
        goals = sim.flood_group_goals(click, units)
        cells = [pathlab.unit_cell(*g, False) for g in goals]
        for i, a in enumerate(cells):
            for b in cells[i + 1:]:
                self.assertTrue(abs(a[0] - b[0]) >= 2 or abs(a[1] - b[1]) >= 2, (a, b))
        nearest = min(range(6), key=lambda k: math.hypot(units[k].x - click[0], units[k].y - click[1]))
        self.assertEqual(min(range(6), key=lambda k: math.hypot(goals[k][0] - click[0], goals[k][1] - click[1])),
                         nearest)

    def test_a_group_order_lays_a_trunk_and_lanes(self):
        grid = make_map("open", seed=3)
        units, orders = pathlab.build_scenario(grid, 6, "column")
        sim = Simulation(grid, units)
        stats = sim.group_move(*orders[0])
        self.assertIn(stats.trunk_diameter, (6, 4, 2))
        self.assertGreater(stats.lanes, 1)
        self.assertTrue(all(u.moving and u.waiting_for_path for u in units))
        self.assertTrue(any(u.planned for u in units))


class BlockingTests(unittest.TestCase):
    def pair(self):
        a = Unit(100.5, 100.5, id=1, facing=0.0)
        b = Unit(120.5, 100.5, id=2, facing=math.pi)
        sim = Simulation(walled(40), [a, b])
        for u, far in ((a, (350.5, 100.5)), (b, (30.5, 100.5))):
            u.moving, u.goal = True, far
            u.path = Path([u.pos, far], [0, 1])
        return sim, a, b

    def test_head_on_passes_through_after_its_frames(self):
        sim, a, b = self.pair()
        self.assertTrue(sim.blocked_by(a, b))
        self.assertTrue(a.head_on_seen)
        a.head_on_frames = pathlab.HEAD_ON_PASS_FRAMES + 1
        self.assertFalse(sim.blocked_by(a, b))

    def test_near_its_goal_nothing_blocks(self):
        sim, a, b = self.pair()
        a.goal = (105.0, 100.5)
        sim.pf.update_goal(a, a.goal)
        self.assertFalse(sim.blocked_by(a, b))

    def test_the_blocked_speed_is_the_other_one_going_our_way(self):
        sim, a, b = self.pair()
        b.facing = 0.0
        self.assertEqual(sim.max_blocked_speed(a, b), 0.0)
        b.vx = 1.0
        self.assertAlmostEqual(sim.max_blocked_speed(a, b), 1.0)

    def test_the_ladder_climbs_in_order_and_starts_again(self):
        r = pathlab.STUCK_REPATH_FRAMES
        self.assertEqual([stuck_rung(r - 1, 0), stuck_rung(r, 0), stuck_rung(r, 1), stuck_rung(r, 2),
                          stuck_rung(r, 3), stuck_rung(pathlab.STUCK_CYCLE_FRAMES + 1, 3)], [0, 1, 2, 3, 0, -1])

    def test_traffic_decays_54_over_64_every_three_frames(self):
        pf = Pathfinder(walled(10))
        pf.note_traffic(55.0, 55.0, 0)
        self.assertEqual(pf.traffic[5 * 10 + 5], 24)
        pf.frame = 2
        pf.decay_traffic()
        self.assertEqual(pf.traffic[5 * 10 + 5], 24)
        pf.frame = 3
        pf.decay_traffic()
        self.assertEqual(pf.traffic[5 * 10 + 5], 24 * 54 // 64)

    def test_a_claim_spills_half_into_the_buckets_either_side(self):
        pf = Pathfinder(walled(10))
        pf.stamp_claim_at(55.0, 55.0, 25, 0)
        self.assertEqual([pf.get_claims(5, 5, f) for f in (15, 25, 35, 45)], [1, 2, 1, 0])


class LadderTests(unittest.TestCase):
    """The proposed stuck ladder, one switch at a time; the defaults stay main's."""

    def driving(self, *positions: tuple[float, float]) -> tuple[Simulation, list[Unit]]:
        units = [Unit(x, y, id=i + 1) for i, (x, y) in enumerate(positions)]
        sim = Simulation(walled(40), units)
        for unit in units:
            far = (350.5, unit.y)
            unit.moving, unit.goal = True, far
            unit.path = Path([unit.pos, far], [0, 1])
            unit.dist_to_goal = far[0] - unit.x
        return sim, units

    def test_the_default_ladder_is_the_game(self):
        self.assertFalse(pathlab.LADDER_REPATH_FLAG)
        self.assertEqual(pathlab.LADDER_STAGE_HOLD, 0)
        self.assertFalse(pathlab.LADDER_ALLY_FILTER)

    def test_a_crowd_repath_survives_the_collision_that_resets_the_blocked_count(self):
        for flag in (False, True):
            with pathlab.overriding(LADDER_REPATH_FLAG=flag):
                sim, (unit,) = self.driving((100.5, 100.5))
                sim.crowd_repath(unit)
                unit.blocked_frames = 1             # processCollision's needToRotate branch
                sim.move_state(unit)
                self.assertEqual(unit.waiting_for_path, flag)

    def test_the_rung_survives_the_path_wait_and_drops_after_progress(self):
        hold = pathlab.PROPOSED_LADDER["LADDER_STAGE_HOLD"]
        with pathlab.overriding(LADDER_STAGE_HOLD=hold):
            sim, (unit,) = self.driving((100.5, 100.5))
            unit.rescue_stage = 1
            unit.waiting_for_path = True
            sim.update_progress(unit)
            self.assertEqual(unit.rescue_stage, 1)
            unit.waiting_for_path = False
            for _ in range(hold - 1):
                unit.x += unit.max_speed
                sim.update_progress(unit)
            self.assertEqual(unit.rescue_stage, 1)
            unit.x += unit.max_speed
            sim.update_progress(unit)
            self.assertEqual(unit.rescue_stage, 0)

    def test_rung_two_asks_parked_allies_and_leaves_moving_ones(self):
        with pathlab.overriding(LADDER_ALLY_FILTER=True):
            sim, (stuck, moving, parked) = self.driving((100.5, 100.5), (100.5, 125.5), (75.5, 100.5))
            parked.moving, parked.path = False, None
            stuck.no_progress, stuck.rescue_stage = pathlab.STUCK_REPATH_FRAMES, 1
            sim.stuck_rescue(stuck)
            self.assertEqual(stuck.rungs_fired, [0, 1, 0])
            self.assertIsNone(moving.move_away_from)
            self.assertEqual(parked.move_away_from, stuck.id)


class CrowdTests(unittest.TestCase):
    def test_the_band_measures_the_ground_either_side(self):
        corr = pathlab.CrowdCorridor()
        self.assertTrue(corr.build(walled(30), 11.0, [(50.0, 150.0), (250.0, 150.0)]))
        s = corr.samples[5]
        # half-cell probes to the last clear cell, less the radius: up to 285, down to 10
        self.assertEqual((s.left, s.right), (135.0 - 11.0, 140.0 - 11.0))
        self.assertEqual(corr.clamp_lat(5, 1000.0), s.left)
        x, y = corr.point_at(100.0, 10.0)
        self.assertAlmostEqual(x, 150.0)
        self.assertAlmostEqual(y, 160.0)

    def test_brake_and_release(self):
        self.assertAlmostEqual(pathlab.brake_speed(1.5, 0.5, 4.0, 5), 1.3)
        self.assertEqual(pathlab.release_cap(1.0, 0.2, 0.25), 0.2)
        self.assertAlmostEqual(pathlab.release_cap(0.2, 1.0, 0.25), 0.4)


class RunTests(unittest.TestCase):
    def test_a_small_column_crosses_the_choke(self):
        result = pathlab.measure("choke", 4, 1, "column")
        self.assertEqual(result.stuck, 0)
        self.assertLess(result.frames, pathlab.MEASURE_MAX_FRAMES)

    def test_a_sweep_names_one_of_the_game_constants(self):
        self.assertEqual(pathlab.parse_sweep("PF_TRAFFIC_NUM=0,4"), ("PF_TRAFFIC_NUM", [0, 4]))
        with self.assertRaises(SystemExit):
            pathlab.parse_sweep("PATH_CONGESTION_COST=5")


if __name__ == "__main__":
    unittest.main()
