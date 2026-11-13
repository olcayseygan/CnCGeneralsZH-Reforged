"""Checks for pathlab.py - stdlib unittest, no window, no pygame display.

    python -m unittest test_pathlab -v        (from Tools/PathLab)

Each cost rule the search charges has a test that holds it to the C++ number, worked by hand from
the formula in AIPathfind.cpp; the rest are the parts of the group order and the following that a
picture cannot argue with."""

import math
import os
import unittest
from dataclasses import replace

import maploader
import pathlab
import unitdata
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
        units, orders = pathlab.build_scenario(grid, [pathlab.STANDIN_TANK] * 6, "column")
        sim = Simulation(grid, units)
        stats = sim.group_move(*orders[0])
        self.assertIn(stats.trunk_diameter, (6, 4, 2))
        self.assertGreater(stats.lanes, 1)
        self.assertTrue(all(u.moving and u.waiting_for_path for u in units))
        self.assertTrue(any(u.planned for u in units))

    def test_a_lane_from_behind_a_wall_closes_to_the_trunk_start_unless_from_spot(self):
        grid = walled(40)
        for x in range(10, 15):
            for y in range(3, 16):
                grid.block(x, y)
        unit = Unit(50.0, 100.0, id=1)          # the wall hides the first corner, not the second
        sim = Simulation(grid, [unit])
        trunk = [(60.0, 250.0), (250.0, 250.0), (150.0, 330.0), (150.0, 350.0)]
        self.assertEqual(sim.plan_lane(unit, trunk, 0.0, trunk[-1]), trunk)
        with pathlab.overriding(CROWD_LANE_FROM_SPOT=True):
            self.assertEqual(sim.plan_lane(unit, trunk, 0.0, trunk[-1]), trunk[2:])
            unit.x, unit.y = 125.0, 20.0        # sees no corner at all: no lane, its own path
            self.assertIsNone(sim.plan_lane(unit, trunk, 0.0, trunk[-1]))


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


class TtcTests(unittest.TestCase):
    """Steering by time to collision, one rule at a time; the defaults stay main's."""

    def mover(self, x: float, y: float, unit_id: int, goal: tuple[float, float]) -> Unit:
        unit = Unit(x, y, id=unit_id)
        unit.moving, unit.goal = True, goal
        unit.path = Path([unit.pos, goal], [0, 1])
        return unit

    def steer(self, sim: Simulation, unit: Unit) -> tuple[tuple[float, float], float]:
        return sim.ttc_steer(unit, unit.goal, unit.max_speed, unit.goal[0] - unit.x)

    def test_the_default_is_the_game(self):
        self.assertFalse(pathlab.CROWD_TTC_STEER)
        self.assertTrue(pathlab.CONTACT_SPEED_CAP)

    def test_the_push_is_away_from_the_predicted_contact(self):
        with pathlab.overriding(TTC_KEEP_RIGHT=0.0):
            for side in (10.0, -10.0):
                unit = self.mover(100.0, 200.0, 1, (300.0, 200.0))
                other = self.mover(160.0, 200.0 + side, 2, (160.0, 400.0))
                sim = Simulation(walled(40), [unit, other])
                (_, aim_y), speed = self.steer(sim, unit)
                self.assertLess(speed, unit.max_speed)
                self.assertLess(aim_y * math.copysign(1.0, side), 200.0 * math.copysign(1.0, side))

    def test_a_head_on_pair_both_bend_to_their_right(self):
        east = self.mover(100.0, 200.0, 1, (300.0, 200.0))
        west = self.mover(200.0, 200.0, 2, (0.0, 200.0))
        west.facing = math.pi
        west.vx = -west.max_speed
        east.vx = east.max_speed
        sim = Simulation(walled(40), [east, west])
        (_, east_y), _ = self.steer(sim, east)
        (_, west_y), _ = sim.ttc_steer(west, west.goal, west.max_speed, west.x - west.goal[0])
        self.assertLess(east_y, 200.0)          # right of +x is -y in the formula's frame
        self.assertGreater(west_y, 200.0)

    def test_a_parked_unit_beside_my_own_slot_never_pushes(self):
        for parked_x, pushes in ((315.0, False), (330.0, True)):
            unit = self.mover(250.0, 200.0, 1, (300.0, 200.0))
            parked = Unit(parked_x, 200.0, id=2)
            sim = Simulation(walled(40), [unit, parked])
            aim, speed = self.steer(sim, unit)
            self.assertEqual(aim != unit.goal or speed != unit.max_speed, pushes)

    def test_the_cap_switch_drops_only_the_speed_write(self):
        seen = []
        for cap in (True, False):
            with pathlab.overriding(CONTACT_SPEED_CAP=cap):
                unit = self.mover(100.5, 100.5, 1, (350.5, 100.5))
                parked = Unit(115.0, 100.5, id=2)
                sim = Simulation(walled(40), [unit, parked])
                sim.process_collision(unit, parked)
                seen.append((unit.is_blocked, unit.blocked_frames, unit.is_blocked_and_stuck,
                             unit.cur_max_blocked_speed))
        self.assertEqual(seen[0][:3], seen[1][:3])
        self.assertEqual(seen[0][3], 0.0)
        self.assertEqual(seen[1][3], pathlab.FAST_AS_POSSIBLE)

    def test_the_end_latch_keeps_crowd_steer_let_go(self):
        """Inside the last cell crowdSteer lets go; stepping back out takes the unit back into its
           lane unless the latch holds the release until the path is destroyed."""
        aims = []
        for latch in (False, True):
            with pathlab.overriding(CROWD_END_LATCH=latch):
                unit = self.mover(299.0, 200.0, 1, (300.0, 200.0))
                unit.path = Path([(100.0, 200.0), (300.0, 200.0)], [0, 1])
                unit.has_pending_crowd_lat, unit.pending_crowd_lat = True, 10.0
                sim = Simulation(walled(40), [unit])
                goal = (300.0, 200.0)
                self.assertEqual(sim.crowd_steer(unit, goal, 1.0)[0], goal)
                self.assertTrue(unit.crowd_released)
                unit.x = 200.0
                aims.append(sim.crowd_steer(unit, goal, 1.0)[0])
                pathlab.destroy_path(unit)
                self.assertFalse(unit.crowd_released)
        self.assertNotEqual(aims[0], (300.0, 200.0))
        self.assertEqual(aims[1], (300.0, 200.0))


class DitherTests(unittest.TestCase):
    """LADDER_DITHER_FIX: a turn on the spot, or back and forth, is not coming about."""

    def climbs(self, infantry: bool, swings: tuple[float, ...], step: float) -> int:
        unit = Unit(100.0, 100.0, id=1, kind=pathlab.default_kind() if not infantry else
                    pathlab.UnitKind(name="soldier", appearance=pathlab.TWO_LEGS, infantry=True,
                                     major_radius=7.0, radius=7.0, speed=1.0, speed_damaged=1.0,
                                     turn_rate=1.0, turn_rate_damaged=1.0, acceleration=1.0,
                                     acceleration_damaged=1.0, braking=1.0))
        unit.moving, unit.goal = True, (400.0, 100.0)
        unit.path = Path([unit.pos, unit.goal], [0, 1])
        unit.loco_goal = unit.goal
        sim = Simulation(walled(40), [unit])
        for tick in range(30):
            unit.facing = swings[tick % len(swings)]
            unit.x += step
            sim.update_progress(unit)
        return unit.no_progress

    def test_the_game_counts_a_swing_as_progress(self):
        self.assertEqual(self.climbs(False, (1.5, -1.5), 0.0), 0)
        self.assertEqual(self.climbs(True, (1.0, 1.6, 2.2), 0.0), 0)

    def test_the_fix_sees_the_wedge(self):
        with pathlab.overriding(LADDER_DITHER_FIX=True):
            self.assertGreater(self.climbs(False, (1.5, -1.5), 0.0), 20)          # back and forth
            self.assertGreater(self.climbs(True, (1.0, 1.6, 2.2), 0.0), 20)        # a soldier on the spot
            self.assertEqual(self.climbs(False, (0.0, 0.6, 1.2, 1.8, 2.4), 0.0), 0)  # a hull coming about


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


def kind(**changes) -> pathlab.UnitKind:
    """A synthetic unit: the stand-in tank with these fields changed; nothing in it is EA's."""
    return replace(pathlab.STANDIN_TANK, **changes)


QUICK = dict(acceleration=50.0, acceleration_damaged=50.0)      # reaches any speed in one tick


def driver(unit_kind: pathlab.UnitKind = pathlab.STANDIN_TANK, facing: float = 0.0) -> tuple[Simulation, Unit]:
    unit = Unit(200.5, 200.5, id=1, kind=unit_kind, facing=facing)
    return Simulation(walled(40), [unit]), unit


def toward(unit: Unit, angle: float, dist: float) -> tuple[float, float]:
    return unit.x + math.cos(angle) * dist, unit.y + math.sin(angle) * dist


class LocomotorTests(unittest.TestCase):
    """Locomotor.cpp's tread, wheel and leg drives and PhysicsUpdate.cpp's friction, one rule each."""

    def test_a_ground_drive_turns_half_as_fast_again_and_no_slower_than_100_degrees(self):
        per_tick = 1.0 / pathlab.LOGICFRAMES_PER_GAME_SECOND
        fast = kind(turn_rate=math.radians(90.0) * per_tick, turn_rate_damaged=math.radians(45.0) * per_tick)
        self.assertAlmostEqual(fast.max_turn_rate(False), math.radians(135.0) * per_tick)
        self.assertAlmostEqual(fast.max_turn_rate(True), math.radians(67.5) * per_tick)
        slow = kind(turn_rate=math.radians(60.0) * per_tick, turn_rate_damaged=math.radians(30.0) * per_tick)
        self.assertAlmostEqual(slow.max_turn_rate(False), math.radians(100.0) * per_tick)
        self.assertAlmostEqual(slow.max_turn_rate(True), math.radians(50.0) * per_tick)
        legs = kind(appearance=pathlab.TWO_LEGS, turn_rate=math.radians(60.0) * per_tick)
        self.assertAlmostEqual(legs.max_turn_rate(False), math.radians(60.0) * per_tick)

    def test_damaged_values_change_speed_turn_and_acceleration(self):
        hurt = kind(speed_damaged=0.5, acceleration_damaged=0.01)
        whole, broken = Unit(0, 0, id=1, kind=hurt), Unit(0, 0, id=2, kind=hurt, damaged=True)
        self.assertEqual((whole.max_speed, broken.max_speed), (hurt.speed, 0.5))
        self.assertEqual((whole.acceleration, broken.acceleration), (hurt.acceleration, 0.01))

    def test_treads_pivot_on_the_spot_for_a_goal_behind(self):
        sim, unit = driver(kind(**QUICK))
        sim.move_treads(unit, toward(unit, math.pi, 100.0), 100.0, unit.max_speed)
        self.assertAlmostEqual(abs(unit.facing), unit.turn_rate)
        self.assertEqual((unit.ax, unit.ay), (0.0, 0.0))        # (1 - rollCoeff) is 0 past 135 degrees

    def test_treads_roll_through_a_turn_slower_the_tighter_it_is(self):
        sim, unit = driver(kind(**QUICK))
        sim.move_treads(unit, toward(unit, math.pi / 4, 100.0), 100.0, unit.max_speed)
        self.assertAlmostEqual(math.hypot(unit.ax, unit.ay), unit.max_speed * (1.0 - 1.0 / 3.0))
        self.assertAlmostEqual(math.atan2(unit.ay, unit.ax), unit.facing)

    def test_treads_read_retail_speed_so_a_diagonal_runs_fast(self):
        sim, unit = driver(facing=math.pi / 4)
        unit.vx = unit.vy = 1.0
        self.assertAlmostEqual(pathlab.retail_forward_speed(unit), 1.0)
        self.assertAlmostEqual(pathlab.forward_speed(unit), math.sqrt(2.0))

    def test_the_braking_latch_slides_onto_the_goal_and_physics_leaves_it(self):
        sim, unit = driver()
        unit.vx = unit.max_speed
        goal = (unit.x + 0.4, unit.y)
        sim.locomote(unit, goal, unit.max_speed, 0.4, False)
        self.assertTrue(unit.is_braking)
        self.assertTrue(unit.braking_status)
        self.assertAlmostEqual(unit.x, goal[0])                  # the slide is capped at what is left
        sim.physics_update(unit)
        self.assertAlmostEqual(unit.x, goal[0])

    def test_the_latch_lets_go_far_from_the_goal(self):
        sim, unit = driver()
        unit.is_braking = True
        sim.locomote(unit, toward(unit, 0.0, 200.0), unit.max_speed, 200.0, False)
        self.assertFalse(unit.is_braking)

    def test_wheels_turn_only_while_moving(self):
        wheels = kind(appearance=pathlab.FOUR_WHEELS, min_turn_speed=0.5, **QUICK)
        sim, unit = driver(wheels)
        sim.move_wheels(unit, toward(unit, math.radians(30.0), 200.0), 200.0, unit.max_speed)
        self.assertEqual(unit.facing, 0.0)
        self.assertGreater(unit.ax, 0.0)
        sim, unit = driver(wheels)
        unit.vx = 0.25                                           # half the turn speed
        sim.move_wheels(unit, toward(unit, math.radians(30.0), 200.0), 200.0, unit.max_speed)
        self.assertAlmostEqual(unit.facing, 0.5 * unit.turn_rate)

    def test_wheels_hold_to_the_turn_speed_through_a_bend(self):
        sim, unit = driver(kind(appearance=pathlab.FOUR_WHEELS, min_turn_speed=0.5, **QUICK))
        sim.move_wheels(unit, toward(unit, math.radians(20.0), 200.0), 200.0, unit.max_speed)
        self.assertAlmostEqual(unit.ax, max(0.5, unit.max_speed * pathlab.WHEEL_TURN_SPEED_SHARE))

    def test_wheels_back_up_to_a_goal_close_behind_and_turn_three_points_for_a_far_one(self):
        backer = kind(appearance=pathlab.FOUR_WHEELS, min_turn_speed=0.5, can_move_backwards=True,
                      major_radius=10.0, **QUICK)
        sim, unit = driver(backer)
        sim.move_wheels(unit, toward(unit, math.pi, 30.0), 30.0, unit.max_speed)
        self.assertTrue(unit.moving_backwards)
        self.assertFalse(unit.three_point_turn)
        self.assertLess(unit.ax, 0.0)
        sim, unit = driver(backer)
        sim.move_wheels(unit, toward(unit, math.pi, 300.0), 300.0, unit.max_speed)
        self.assertTrue(unit.three_point_turn)
        sim, unit = driver(replace(backer, can_move_backwards=False))
        sim.move_wheels(unit, toward(unit, math.pi, 30.0), 30.0, unit.max_speed)
        self.assertFalse(unit.moving_backwards)

    def test_wheels_stop_to_turn_where_the_arc_runs_onto_rock(self):
        sim, unit = driver(kind(appearance=pathlab.FOUR_WHEELS, min_turn_speed=0.5, **QUICK))
        for row in range(40):
            sim.grid.block(21, row)                              # a wall just ahead, x 210 to 220
        unit.vx = unit.max_speed
        sim.move_wheels(unit, toward(unit, math.radians(60.0), 200.0), 200.0, unit.max_speed)
        self.assertAlmostEqual(unit.facing, unit.turn_rate)      # the whole rate, not the speed's share
        self.assertEqual((unit.ax, unit.ay), (0.0, 0.0))

    def test_the_turn_pivot_moves_the_body(self):
        sim, unit = driver(kind(turn_pivot_offset=-0.5))
        x, y = unit.pos
        pathlab.turn_around_pivot(unit, math.pi / 2)
        reach = -0.5 * unit.radius
        self.assertAlmostEqual(unit.x, x + reach)
        self.assertAlmostEqual(unit.y, y - reach)

    def test_legs_ease_into_the_turn(self):
        legs = kind(appearance=pathlab.TWO_LEGS, infantry=True, turn_rate=0.3, turn_rate_damaged=0.3)
        sim, unit = driver(legs)
        goal = toward(unit, math.pi * 0.9, 200.0)
        yaw_accel = unit.turn_rate / pathlab.INFANTRY_YAW_EASE_FRAMES
        rates = []
        for _ in range(3):
            sim.move_legs(unit, goal, 200.0, unit.max_speed)
            rates.append(unit.yaw_rate)
        self.assertAlmostEqual(rates[0], yaw_accel)
        self.assertAlmostEqual(rates[1], min(2 * yaw_accel, unit.turn_rate))
        self.assertAlmostEqual(rates[2], unit.turn_rate)

    def test_legs_walk_at_the_cosine_of_what_is_left_to_turn(self):
        legs = kind(appearance=pathlab.TWO_LEGS, infantry=True, turn_rate=0.0, turn_rate_damaged=0.0, **QUICK)
        sim, unit = driver(legs)
        sim.move_legs(unit, toward(unit, math.radians(60.0), 200.0), 200.0, unit.max_speed)
        self.assertAlmostEqual(unit.ax, unit.max_speed * 0.5)
        sim, unit = driver(legs)
        sim.move_legs(unit, toward(unit, math.radians(100.0), 200.0), 200.0, unit.max_speed)
        self.assertEqual(unit.ax, 0.0)

    def test_legs_slow_for_the_stop_at_the_end(self):
        legs = kind(appearance=pathlab.TWO_LEGS, infantry=True, **QUICK)
        sim, unit = driver(legs)
        sim.move_legs(unit, toward(unit, 0.0, 0.2), 0.2, unit.max_speed)
        stop = max(unit.max_speed / pathlab.INFANTRY_STOP_FRAMES, legs.braking)
        self.assertAlmostEqual(unit.ax, math.sqrt(2.0 * stop * 0.2))

    def test_legs_carry_the_walk_round_with_the_nose_where_a_tread_skids(self):
        yaw = 0.1
        sim, soldier = driver(kind(appearance=pathlab.TWO_LEGS, infantry=True))
        soldier.vx = 1.0
        sim.turn_vehicle(soldier, yaw)
        sim.physics_update(soldier)
        sim, tank = driver()
        tank.vx = 1.0
        tank.motive_until = sim.frame + pathlab.MOTIVE_FRAMES
        pathlab.turn_around_pivot(tank, yaw)
        sim.physics_update(tank)
        self.assertAlmostEqual(pathlab.forward_speed(soldier), 1.0)
        self.assertLess(pathlab.forward_speed(tank), 1.0)
        sideways = [abs(-u.vx * u.dir[1] + u.vy * u.dir[0]) for u in (soldier, tank)]
        self.assertLess(sideways[0], sideways[1] / 3.0)

    def test_friction_skids_a_tread_off_its_old_line(self):
        sim, unit = driver(facing=math.pi / 2)
        unit.vx = 1.0
        unit.motive_until = sim.frame + pathlab.MOTIVE_FRAMES
        x = unit.x
        sim.physics_update(unit)
        share = 1.0 - (1.0 - 0.15) ** 1.5
        self.assertAlmostEqual(unit.vx, 1.0 - share)
        self.assertAlmostEqual(unit.x, x + 1.0 - share)
        unit.vx, unit.vy, unit.facing = 0.0, 1.0, math.pi / 2
        sim.physics_update(unit)
        self.assertEqual(unit.vy, 1.0)                           # driven: no forward friction
        sim.pf.frame += pathlab.MOTIVE_FRAMES
        sim.physics_update(unit)
        self.assertAlmostEqual(unit.vy, 1.0 - share)

    def test_a_crumb_of_velocity_is_zero(self):
        sim, unit = driver()
        unit.vx = pathlab.VELOCITY_ZERO * 0.5
        unit.motive_until = sim.frame + 1
        sim.physics_update(unit)
        self.assertEqual(unit.vx, 0.0)

    def test_a_blocked_unit_scrubs_its_speed_and_only_turns(self):
        sim, unit = driver()
        unit.vx = unit.max_speed
        blocked = sim.locomote(unit, toward(unit, math.pi / 2, 200.0), 0.5, 200.0, True)
        self.assertTrue(blocked)
        self.assertAlmostEqual(unit.vx, 0.5)
        self.assertAlmostEqual(unit.facing, unit.turn_rate)
        self.assertFalse(sim.locomote(unit, toward(unit, 0.0, 200.0), unit.max_speed, 200.0, True))

    def test_a_parked_vehicle_stops_and_a_soldier_brakes(self):
        sim, unit = driver()
        unit.vx, unit.motive_until = 1.0, sim.frame + 1
        sim.maintain_position(unit)
        self.assertEqual(unit.vx, 0.0)
        legs = kind(appearance=pathlab.TWO_LEGS, infantry=True)
        sim, soldier = driver(legs)
        soldier.vx, soldier.motive_until = 1.0, sim.frame + 1
        sim.maintain_position(soldier)
        self.assertAlmostEqual(soldier.vx, 1.0 - pathlab.legs_stop_decel(soldier))

    def test_boxes_touch_by_their_corners_not_their_circles(self):
        box = kind(geometry="BOX", major_radius=15.0, minor_radius=5.0, radius=math.hypot(15.0, 5.0))
        a = Unit(100.0, 100.0, id=1, kind=box)
        b = Unit(100.0, 120.0, id=2, kind=box)                   # side by side, 10 apart
        self.assertLess(math.hypot(b.x - a.x, b.y - a.y), a.radius + b.radius)
        self.assertFalse(pathlab.bodies_touch(a, b))
        b.y = 109.0
        self.assertTrue(pathlab.bodies_touch(a, b))


class InfantryTests(unittest.TestCase):
    """What the game does for foot soldiers: through each other, no lane, round a hull."""

    def soldiers(self) -> tuple[Simulation, Unit, Unit]:
        legs = kind(appearance=pathlab.TWO_LEGS, infantry=True, radius=5.0, major_radius=5.0)
        a = Unit(100.5, 100.5, id=1, kind=legs)
        b = Unit(108.5, 100.5, id=2, kind=legs)
        sim = Simulation(walled(40), [a, b])
        for u in (a, b):
            u.moving, u.goal = True, (350.5, 100.5)
            u.path = Path([u.pos, u.goal], [0, 1])
        return sim, a, b

    def test_infantry_never_blocks_infantry(self):
        sim, a, b = self.soldiers()
        self.assertFalse(sim.blocked_by(a, b))
        b.moving, b.path = False, None
        self.assertFalse(sim.blocked_by(a, b))

    def test_a_soldier_marching_our_way_does_not_block_a_vehicle(self):
        sim, soldier, _ = self.soldiers()
        tank = Unit(90.5, 100.5, id=3)
        tank.moving, tank.goal = True, (350.5, 100.5)
        tank.path = Path([tank.pos, tank.goal], [0, 1])
        sim = Simulation(walled(40), [tank, soldier])
        self.assertFalse(sim.blocked_by(tank, soldier))
        soldier.facing = math.pi / 2
        self.assertTrue(sim.blocked_by(tank, soldier))
        self.assertTrue(sim.higher_priority(tank, soldier))

    def test_a_soldier_against_a_hull_steps_along_it(self):
        sim, soldier, _ = self.soldiers()
        tank = Unit(110.5, 100.5, id=3)
        sim.process_collision(soldier, tank)
        self.assertTrue(soldier.side_step_seen)
        self.assertFalse(soldier.is_blocked)
        goal = pathlab.side_step_goal(soldier, (200.5, 100.5), (soldier.side_step_x, soldier.side_step_y))
        self.assertLess(goal[0], soldier.x)                      # out from the hull, never into it

    def test_a_soldier_walks_the_middle_of_his_band_and_reads_nobody(self):
        sim, a, b = self.soldiers()
        a.pending_crowd_lat, a.has_pending_crowd_lat = 30.0, True
        sim.crowd_steer(a, a.goal, a.max_speed)
        self.assertEqual(a.crowd_scan["near"], [])
        self.assertLessEqual(abs(a.crowd_lat - 30.0), a.max_speed)  # heading for 0 at the lane rate
        self.assertLess(a.crowd_lat, 30.0)

    def test_the_search_walks_infantry_through_parked_infantry(self):
        legs = kind(appearance=pathlab.TWO_LEGS, infantry=True, radius=5.0, major_radius=5.0)
        parked = Unit(*cell_pos(10, 10, False), id=2, kind=legs)
        sim = Simulation(walled(30), [parked])
        soldier = Unit(0, 0, id=1, kind=legs)
        sim.pf.update_pos(soldier)
        self.assertEqual(sim.pf.check_for_movement(soldier, 10, 10, False).ally_fixed, 0)
        self.assertEqual(sim.pf.check_for_movement(Unit(0, 0, id=3), 10, 10, False).ally_fixed, 1)

    def test_a_group_lays_no_lane_for_a_soldier(self):
        grid = make_map("open", seed=3)
        legs = kind(appearance=pathlab.TWO_LEGS, infantry=True, radius=5.0, major_radius=5.0)
        units, orders = pathlab.build_scenario(grid, [pathlab.STANDIN_TANK, legs] * 3, "column")
        sim = Simulation(grid, units)
        sim.group_move(*orders[0])
        self.assertTrue(all(not u.planned for u in units if u.is_infantry))
        self.assertTrue(any(u.planned for u in units if not u.is_infantry))


class UnitDataTests(unittest.TestCase):
    """The loader on synthetic INI blocks, and on the install when there is one."""

    def test_a_later_locomotor_block_patches_only_its_fields(self):
        base = unitdata.patch_locomotor(unitdata.LocomotorData("Test"), [
            ("Speed = 40", None), ("TurnRate = 90", None), ("Appearance = TREADS", None)])
        patched = unitdata.patch_locomotor(base, [("Speed = 50", None), ("CanMoveBackwards = Yes", None)])
        self.assertEqual((patched.speed, patched.turn_rate, patched.appearance, patched.can_move_backwards),
                         (50.0, 90.0, "TREADS", True))
        valid = patched.validated()
        self.assertEqual((valid.speed_damaged, valid.turn_rate_damaged), (50.0, 90.0))

    def test_an_object_names_its_locomotor_footprint_and_friction_wherever_they_nest(self):
        fields: dict = {}
        unitdata.patch_object(fields, [
            ("KindOf = SELECTABLE VEHICLE", None), ("Geometry = BOX", None),
            ("GeometryMajorRadius = 12.0", None), ("GeometryMinorRadius = 5.0", None),
            ("Behavior = AIUpdateInterface ModuleTag_01", [("Locomotor = SET_NORMAL TestLocomotor", None)]),
            ("Behavior = PhysicsBehavior ModuleTag_02", [("LateralFriction = 6", None)]),
        ], True)
        self.assertEqual(fields, {"infantry": False, "geometry": "BOX", "major_radius": 12.0,
                                  "minor_radius": 5.0, "locomotor": "testlocomotor", "lateral_friction": 0.2})
        unit = unitdata.UnitData("test", "Test", unitdata.LocomotorData("TestLocomotor"), False, "BOX", 3.0, 4.0)
        self.assertEqual(unit.bounding_radius, 5.0)

    def test_the_ini_values_become_ticks(self):
        loco = unitdata.LocomotorData("Test", appearance="TREADS", speed=40.0, turn_rate=180.0,
                                      acceleration=800.0, braking=400.0).validated()
        unit = pathlab.unit_kind(unitdata.UnitData("test", "Test", loco, False, "CYLINDER", 6.0))
        per_tick = pathlab.LOGICFRAMES_PER_GAME_SECOND
        self.assertAlmostEqual(unit.speed, 40.0 / per_tick)
        self.assertAlmostEqual(unit.turn_rate, math.pi / per_tick)
        self.assertAlmostEqual(unit.acceleration, 800.0 / per_tick ** 2)
        self.assertAlmostEqual(unit.braking, 400.0 / per_tick ** 2)
        self.assertEqual(unit.radius, 6.0)

    def test_a_group_spec_spreads_its_kinds_through_the_list(self):
        a, b = kind(name="a"), kind(name="b")
        group = pathlab.parse_group("a:4,b:2", {"a": a, "b": b})
        self.assertEqual([k.name for k in group], ["a", "b", "a", "a", "b", "a"])
        with self.assertRaises(SystemExit):
            pathlab.parse_group("c:1", {"a": a})

    @unittest.skipUnless(unitdata.install_present(), "no game install under RUN_DIR")
    def test_the_install_has_the_seven_units(self):
        units = unitdata.load_units()
        self.assertEqual(set(units), set(unitdata.UNIT_OBJECTS))
        for unit in units.values():
            self.assertGreater(unit.locomotor.speed, 0.0)
            self.assertGreater(unit.bounding_radius, 0.0)
            self.assertEqual(unit.infantry, unit.locomotor.appearance == pathlab.TWO_LEGS)
        self.assertIn(units["humvee"].locomotor.appearance, (pathlab.FOUR_WHEELS, pathlab.MOTORCYCLE))
        self.assertEqual(units["crusader"].locomotor.appearance, pathlab.TREADS)


class RunTests(unittest.TestCase):
    def test_a_small_column_crosses_the_choke(self):
        result = pathlab.measure("choke", [pathlab.STANDIN_TANK] * 4, 1, "column")
        self.assertEqual(result.stuck, 0)
        self.assertLess(result.frames, pathlab.MEASURE_MAX_FRAMES)

    @unittest.skipUnless(os.path.isfile(os.path.join(maploader.game_dir_of(None), maploader.MAPS_ARCHIVE)),
                         "no MapsZH.big in the install")
    def test_gamemap_cross_starts_each_half_at_a_start_bound_for_the_other(self):
        kinds = [pathlab.STANDIN_TANK] * 6
        grid, units, orders = pathlab.scenario_setup("choke", "Tournament Desert", 0, kinds, "cross")
        first, second = pathlab.loaded_game_map("Tournament Desert").starts[:2]
        self.assertEqual(len(units), 6)
        self.assertEqual([goal for _, goal in orders], [second, first])
        for (group, goal), home in zip(orders, (first, second)):
            for unit in group:
                self.assertTrue(grid.passable(*pathlab.cell_of(*unit.pos)))
                self.assertLess(math.dist(unit.pos, home), 100.0)

    @unittest.skipUnless(os.path.isfile(os.path.join(maploader.game_dir_of(None), maploader.MAPS_ARCHIVE)),
                         "no MapsZH.big in the install")
    def test_gamemap_route_replaces_the_starts(self):
        route = pathlab.parse_route("120,160,190,160")
        self.assertEqual(route, (pathlab.cell_pos(120, 160, True), pathlab.cell_pos(190, 160, True)))
        _, units, orders = pathlab.scenario_setup("choke", "Tournament Urban", 0, [pathlab.STANDIN_TANK] * 4,
                                                  "column", route)
        self.assertEqual(orders[0][1], route[1])
        self.assertTrue(all(math.dist(unit.pos, route[0]) < 100.0 for unit in units))

    def test_a_sweep_names_one_of_the_game_constants(self):
        self.assertEqual(pathlab.parse_sweep("PF_TRAFFIC_NUM=0,4"), ("PF_TRAFFIC_NUM", [0, 4]))
        with self.assertRaises(SystemExit):
            pathlab.parse_sweep("PATH_CONGESTION_COST=5")


if __name__ == "__main__":
    unittest.main()
