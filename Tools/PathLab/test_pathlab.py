"""Checks for pathlab.py - stdlib unittest, no window, no pygame display.

    python -m unittest test_pathlab -v        (from Tools/PathLab)

The point of these is the parts a screenshot cannot argue with: the A* costs, the two penalty
functions and their caps, and the book-keeping in the usage and reservation maps - which is where a
mistake is silent, because a mis-released cell only shows up as a number in a table being slightly
wrong."""

import math
import unittest

import pathlab
from pathlab import (CELL, COST_DIAGONAL, COST_ORTHOGONAL, PATH_CONGESTION_COST,
                     PATH_CONGESTION_MAX_PATHS, PATH_CROSSING_COST, PATH_CROSSING_MAX,
                     PATH_JAM_COST, PATH_JAM_DECAY_FRAMES, PATH_JAM_FLOOR, PATH_JAM_MAX,
                     PATH_JAM_MAX_CHARGED, PATH_JAM_RANGE,
                     Grid, JamMap, OrderStats, ReservationMap, TrafficModel, Unit, UsageMap,
                     astar, build_scenario, cell_of, compute_centroid, count_planned_crossings,
                     find_crossing_cells, make_map, order_band, order_corridor, order_individual,
                     parse_sweep, price_congestion, price_crossing, price_jam, simplify_raycast,
                     spawn_block, track_reservations, tuning, update_band, world_of)


def open_grid(size: int = 21) -> Grid:
    """No border wall: the tests that want one build it themselves."""
    return Grid(size, size)


def corridor_grid(size: int = 21) -> Grid:
    """A wall down the middle with a single-cell gap - too narrow for a clearance of 1."""
    grid = open_grid(size)
    for y in range(size):
        if y != size // 2:
            grid.block(size // 2, y)
    return grid


class GeometryTests(unittest.TestCase):
    def test_world_of_is_the_centre_of_the_cell_it_names(self):
        self.assertEqual(cell_of(*world_of(4, 7)), (4, 7))
        self.assertEqual(world_of(0, 0), (CELL / 2, CELL / 2))

    def test_line_passable_sees_a_wall_the_endpoints_miss(self):
        grid = corridor_grid()
        left, right = world_of(2, 2), world_of(18, 2)
        self.assertFalse(grid.line_passable(*left, *right))
        through_gap = world_of(18, 10)
        self.assertTrue(grid.line_passable(*world_of(2, 10), *through_gap))

    def test_has_room_is_false_in_a_doorway(self):
        grid = corridor_grid()
        self.assertFalse(grid.has_room(10, 10))         # the gap itself
        self.assertTrue(grid.has_room(3, 3))            # open ground

    def test_make_map_names_the_kinds_it_knows(self):
        with self.assertRaises(ValueError) as raised:
            make_map("cliff")
        self.assertIn("cliff", str(raised.exception))
        self.assertIn("twogaps", str(raised.exception))


class AStarTests(unittest.TestCase):
    def test_straight_line_costs_one_orthogonal_step_per_cell(self):
        cells, expansions = astar(open_grid(), (2, 2), (2, 8))
        self.assertEqual(cells[0], (2, 2))
        self.assertEqual(cells[-1], (2, 8))
        self.assertEqual(len(cells), 7)
        self.assertGreater(expansions, 0)

    def test_diagonal_is_cheaper_than_going_round_the_corner(self):
        self.assertLess(COST_DIAGONAL, 2 * COST_ORTHOGONAL)
        cells, _ = astar(open_grid(), (2, 2), (6, 6))
        self.assertEqual(len(cells), 5)                 # four diagonal steps

    def test_no_path_through_a_blocked_goal(self):
        grid = open_grid()
        grid.block(5, 5)
        self.assertEqual(astar(grid, (2, 2), (5, 5))[0], [])

    def test_clearance_rejects_a_gap_a_wide_group_cannot_use(self):
        grid = corridor_grid()
        self.assertTrue(astar(grid, (2, 10), (18, 10))[0])
        self.assertEqual(astar(grid, (2, 10), (18, 10), clearance=1)[0], [])

    def test_budget_exhaustion_returns_no_path_rather_than_running_for_ever(self):
        cells, expansions = astar(open_grid(41), (2, 2), (38, 38), cell_budget=5)
        self.assertEqual(cells, [])
        self.assertLessEqual(expansions, 6)


class PricingTests(unittest.TestCase):
    def test_congestion_is_linear_then_capped(self):
        self.assertEqual(price_congestion(0), 0)
        self.assertEqual(price_congestion(2), 2 * PATH_CONGESTION_COST)
        self.assertEqual(price_congestion(PATH_CONGESTION_MAX_PATHS + 5),
                         PATH_CONGESTION_MAX_PATHS * PATH_CONGESTION_COST)

    def test_crossing_is_linear_then_capped(self):
        self.assertEqual(price_crossing(0), 0)
        self.assertEqual(price_crossing(1), PATH_CROSSING_COST)
        self.assertEqual(price_crossing(PATH_CROSSING_MAX + 5),
                         PATH_CROSSING_MAX * PATH_CROSSING_COST)

    def test_a_priced_route_goes_round_a_claimed_gap(self):
        """Four diagonal steps instead of four orthogonal ones cost 16; crossing the claimed cell
           costs 20.  So the detour has to win, and it is the cost that made it, not the map."""
        grid = open_grid()
        claimed = [(10, 9), (10, 10), (10, 11)]
        usage = UsageMap()
        usage.register(0, claimed)
        pricing = pathlab.PathPricing(usage=usage, congestion_range=1000, owner=1)
        plain, _ = astar(grid, (2, 10), (18, 10))
        priced, _ = astar(grid, (2, 10), (18, 10), pricing=pricing)
        self.assertIn((10, 10), plain)
        self.assertFalse(set(priced) & set(claimed))

    def test_jam_costs_nothing_until_it_has_lasted(self):
        """The floor is the whole design: a unit pausing for a frame stamps 1 or 2 and that is
           traffic.  Only ground several units have been stuck on gets past the floor."""
        self.assertEqual(price_jam(0), 0)
        self.assertEqual(price_jam(PATH_JAM_FLOOR), 0)
        self.assertEqual(price_jam(PATH_JAM_FLOOR + 1), PATH_JAM_COST)
        self.assertEqual(price_jam(PATH_JAM_FLOOR + PATH_JAM_MAX_CHARGED + 9),
                         PATH_JAM_MAX_CHARGED * PATH_JAM_COST)

    def test_a_jammed_doorway_is_worth_going_round_and_a_busy_one_is_not(self):
        """The same map twice: stamps below the floor leave the straight line alone, and stamps
           above it move the route off it.  Without both halves this cost fires on traffic."""
        grid = corridor_grid()
        door = (10, 10)
        jam = JamMap()
        for _ in range(PATH_JAM_FLOOR):
            jam.note(door)
        pricing = pathlab.PathPricing(jam=jam)
        # a wall with one gap: there is no way round, so the route runs through it either way
        self.assertIn(door, astar(grid, (2, 10), (18, 10), pricing=pricing)[0])

        # now on open ground, where there is somewhere else to go
        grid = open_grid()
        jam = JamMap()
        for _ in range(PATH_JAM_FLOOR):
            jam.note(door)
        plain = astar(grid, (2, 10), (18, 10), pricing=pathlab.PathPricing(jam=jam))[0]
        self.assertIn(door, plain)          # still traffic: at the floor, nothing is charged
        for _ in range(PATH_JAM_MAX):
            jam.note(door)
        priced = astar(grid, (2, 10), (18, 10), pricing=pathlab.PathPricing(jam=jam))[0]
        self.assertNotIn(door, priced)

    def test_the_jam_penalty_is_only_read_near_the_start(self):
        """The engine charges it within PATH_QUEUE_RANGE of where the repath began, because it is
           the unit leaving the jam that pays, not every search that crosses the map later."""
        grid = open_grid(60)
        far = (2 + PATH_JAM_RANGE + 4, 10)
        jam = JamMap()
        for _ in range(PATH_JAM_MAX):
            jam.note(far)
        route = astar(grid, (2, 10), (50, 10), pricing=pathlab.PathPricing(jam=jam))[0]
        self.assertIn(far, route)           # out of range: not priced, so not avoided

    def test_the_jam_map_forgets_on_the_frame_number(self):
        """The decay runs off the frame it is given rather than a call count, which is what makes
           it the same on two machines - and what makes two sweep rows comparable here."""
        jam = JamMap()
        cell = (4, 4)
        for _ in range(PATH_JAM_MAX + 5):
            jam.note(cell)
        self.assertEqual(jam.at(cell), PATH_JAM_MAX)        # the stamp saturates

        jam.decay(1)                                        # too soon: nothing moves
        jam.decay(PATH_JAM_DECAY_FRAMES - 1)
        self.assertEqual(jam.at(cell), PATH_JAM_MAX)

        jam.decay(PATH_JAM_DECAY_FRAMES)
        self.assertEqual(jam.at(cell), PATH_JAM_MAX - 1)

        # and it empties rather than keeping zeroes about, so a quiet map costs nothing to carry
        for frame in range(PATH_JAM_DECAY_FRAMES, PATH_JAM_DECAY_FRAMES * (PATH_JAM_MAX + 2),
                           PATH_JAM_DECAY_FRAMES):
            jam.decay(frame)
        self.assertEqual(jam.jam, {})
        self.assertEqual(jam.at(cell), 0)

    def test_tuning_hands_the_constants_back(self):
        before = (pathlab.PATH_CONGESTION_COST, pathlab.PATH_CONGESTION_MAX_PATHS,
                  pathlab.PATH_CROSSING_COST, pathlab.PATH_CROSSING_WINDOW)
        with tuning(congestion_cost=99, congestion_cap=9, crossing_cost=98, crossing_window=97):
            self.assertEqual(pathlab.PATH_CONGESTION_COST, 99)
            self.assertEqual(pathlab.PATH_CROSSING_WINDOW, 97)
        self.assertEqual((pathlab.PATH_CONGESTION_COST, pathlab.PATH_CONGESTION_MAX_PATHS,
                          pathlab.PATH_CROSSING_COST, pathlab.PATH_CROSSING_WINDOW), before)


class UsageMapTests(unittest.TestCase):
    def test_registering_twice_does_not_double_count(self):
        usage = UsageMap()
        usage.register(0, [(1, 1), (2, 2)])
        usage.register(0, [(1, 1), (3, 3)])
        self.assertEqual(usage.count, {(1, 1): 1, (3, 3): 1})

    def test_full_release_leaves_nothing_behind(self):
        usage = UsageMap()
        usage.register(0, [(1, 1), (2, 2)])
        usage.register(1, [(1, 1)])
        usage.unregister(0)
        usage.unregister(1)
        self.assertEqual(usage.count, {})

    def test_release_before_drops_only_what_is_driven_past(self):
        usage = UsageMap()
        usage.register(0, [(1, 1), (2, 2), (3, 3)])
        usage.release_before(0, 2)
        self.assertEqual(usage.count, {(3, 3): 1})
        self.assertEqual(usage.owner[0], [(3, 3)])

    def test_a_unit_is_not_charged_for_its_own_path(self):
        usage = UsageMap()
        usage.register(0, [(4, 4)])
        usage.register(1, [(4, 4)])
        self.assertEqual(usage.count_paths_on((4, 4)), 2)
        self.assertEqual(usage.count_paths_on((4, 4), exclude_owner=0), 1)


class ReservationMapTests(unittest.TestCase):
    def reserve(self, reservations, owner, arrival, heading):
        reservations.reserve(owner, [(5, 5)], [arrival], [heading])

    def test_following_traffic_is_not_a_crossing(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.assertEqual(reservations.count_conflicts((5, 5), 101.0, 1, (1.0, 0.0)), 0)

    def test_opposing_traffic_at_the_same_moment_is(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.assertEqual(reservations.count_conflicts((5, 5), 101.0, 1, (0.0, 1.0)), 1)

    def test_the_same_cell_much_later_is_not(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        far = 100.0 + pathlab.PATH_CROSSING_WINDOW + 1
        self.assertEqual(reservations.count_conflicts((5, 5), far, 1, (0.0, 1.0)), 0)

    def test_release_before_keeps_the_future_and_drops_the_past(self):
        reservations = ReservationMap()
        reservations.reserve(0, [(1, 1), (2, 2)], [10.0, 30.0], [(1.0, 0.0), (1.0, 0.0)])
        reservations.release_before(0, 20.0)
        self.assertEqual(list(reservations.at), [(2, 2)])
        self.assertEqual(len(reservations.owner[0]), 1)

    def test_full_release_leaves_nothing_behind(self):
        reservations = ReservationMap()
        reservations.reserve(0, [(1, 1)], [10.0], [(1.0, 0.0)])
        reservations.reserve(1, [(1, 1)], [11.0], [(0.0, 1.0)])
        reservations.release(0)
        reservations.release(1)
        self.assertEqual(reservations.at, {})

    def test_crossing_cells_counts_an_opposed_pair_once(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.reserve(reservations, 1, 101.0, (0.0, 1.0))
        self.assertEqual(find_crossing_cells(reservations), {(5, 5): 1})
        self.assertEqual(count_planned_crossings(reservations), 1)

    def test_a_queue_produces_no_crossing_cells(self):
        reservations = ReservationMap()
        self.reserve(reservations, 0, 100.0, (1.0, 0.0))
        self.reserve(reservations, 1, 101.0, (1.0, 0.0))
        self.assertEqual(find_crossing_cells(reservations), {})


class TrackTests(unittest.TestCase):
    def test_arrival_times_only_ever_grow(self):
        track = track_reservations((5.0, 5.0), [(5.0, 205.0)], start_frame=7.0)
        self.assertTrue(all(later >= earlier
                            for earlier, later in zip(track.times, track.times[1:])))
        self.assertGreaterEqual(track.times[0], 7.0)

    def test_headings_are_unit_length_and_point_along_the_leg(self):
        track = track_reservations((5.0, 5.0), [(205.0, 5.0)])
        for hx, hy in track.headings:
            self.assertAlmostEqual(math.hypot(hx, hy), 1.0, places=6)
            self.assertAlmostEqual(hx, 1.0, places=6)

    def test_every_cell_on_the_drive_line_is_claimed_once(self):
        track = track_reservations((5.0, 5.0), [(95.0, 5.0)])
        self.assertEqual(track.cells, [(cx, 0) for cx in range(1, 10)])
        self.assertEqual(len(track.times), len(track.cells))
        self.assertEqual(len(track.headings), len(track.cells))


class StraighteningTests(unittest.TestCase):
    def test_collinear_waypoints_on_open_ground_collapse(self):
        grid = open_grid()
        points = [world_of(cx, 2) for cx in range(3, 12)]
        simplified, rays = simplify_raycast(grid, world_of(2, 2), points, radius=11.0,
                                            lane_spacing=22.0)
        self.assertLess(len(simplified), len(points))
        self.assertEqual(simplified[-1], points[-1])
        self.assertTrue(rays)

    def test_a_wall_stops_the_shortcut(self):
        grid = corridor_grid()
        points = [world_of(cx, 2) for cx in range(3, 20)]
        simplified, rays = simplify_raycast(grid, world_of(2, 2), points, radius=11.0,
                                            lane_spacing=22.0)
        self.assertTrue(any(not is_clear for *_, is_clear in rays))
        self.assertEqual(simplified[-1], points[-1])


class BandTests(unittest.TestCase):
    """The ribbon's pressure term.  Every one of these is a case the headless table cannot show:
       a number moving there says something happened, not that the right thing happened."""

    def on_a_band(self, grid, x, y, *, half=20.0, t=0.0, line=None):
        unit = Unit(x, y, id=0)
        unit.path = line[1:] if line else [world_of(10, 5), world_of(18, 5)]
        unit.band_line = line or [world_of(2, 5), *unit.path]
        unit.band_half = half
        unit.band_t = t
        unit.band_step = pathlab.BAND_STEP
        return unit

    def test_the_band_frame_is_the_line_and_not_where_the_unit_drifted_to(self):
        # the unit sits well off the centre line; sideways is still the line's sideways
        unit = self.on_a_band(open_grid(), *world_of(4, 9))
        (tx, ty), (nx, ny) = pathlab.band_frame(unit)
        self.assertAlmostEqual(math.hypot(tx, ty), 1.0)
        self.assertAlmostEqual(tx * nx + ty * ny, 0.0)
        self.assertAlmostEqual(ty, 0.0)                 # the line runs east

    def test_a_unit_with_no_band_has_no_frame(self):
        unit = self.on_a_band(open_grid(), *world_of(4, 5), half=0.0)
        self.assertIsNone(pathlab.band_frame(unit))

    def test_the_last_waypoint_is_driven_without_the_offset(self):
        """The goals were spread and reserved when the order was given; offsetting them again puts
           two units on one spot."""
        unit = self.on_a_band(open_grid(), *world_of(4, 5), t=15.0)
        first = unit.path[0]
        self.assertNotEqual(pathlab.band_target(unit, first), first)
        unit.index = len(unit.path) - 1
        last = unit.path[-1]
        self.assertEqual(pathlab.band_target(unit, last), last)

    def test_the_pressure_points_away_from_the_busier_side(self):
        grid = open_grid()
        mover = self.on_a_band(grid, *world_of(4, 5))
        tangent, normal = (1.0, 0.0), (0.0, 1.0)
        ahead = Unit(*world_of(6, 5), id=1)             # straight in front
        crowd = Unit(*world_of(6, 8), id=2)             # ... and one more, a bucket over
        self.assertEqual(
            pathlab.band_pressure([mover, ahead, crowd], mover, tangent, normal), -1.0)

    def test_nothing_in_front_means_nobody_moves(self):
        grid = open_grid()
        mover = self.on_a_band(grid, *world_of(4, 5))
        beside = Unit(*world_of(4, 6), id=1)            # abreast, not ahead
        behind = Unit(*world_of(2, 5), id=2)
        self.assertEqual(
            pathlab.band_pressure([mover, beside, behind], mover, (1.0, 0.0), (0.0, 1.0)), 0.0)
        update_band(grid, [mover, beside, behind], mover)
        self.assertEqual(mover.band_t, 0.0)

    def test_the_drift_is_clamped_to_the_band(self):
        grid = open_grid()
        mover = self.on_a_band(grid, *world_of(4, 5), half=5.0, t=5.0)
        crowd = [Unit(*world_of(6, 4), id=1), Unit(*world_of(6, 5), id=2)]
        for _ in range(20):
            update_band(grid, [mover, *crowd], mover)
        self.assertLessEqual(abs(mover.band_t), 5.0)

    def test_a_drift_into_a_wall_is_refused(self):
        """The band's width was measured on the centre line, so a lateral step is still a blind
           sideways step - the same reason the lane ladder probes before it commits."""
        grid = open_grid()
        for cx in range(grid.width):
            grid.block(cx, 6)
        mover = self.on_a_band(grid, *world_of(4, 5), half=40.0)
        crowd = [Unit(*world_of(6, 5), id=1), Unit(*world_of(6, 4), id=2)]
        for _ in range(20):
            update_band(grid, [mover, *crowd], mover)
        self.assertEqual(mover.band_t, 0.0)             # the only way out is under the wall

    def test_a_repath_takes_a_unit_off_the_band(self):
        unit = self.on_a_band(open_grid(), *world_of(4, 5), t=12.0)
        traffic = TrafficModel(UsageMap(), ReservationMap())
        pathlab.apply_path(unit, [world_of(8, 5)], [(8, 5)], traffic, 0.0)
        self.assertEqual((unit.band_half, unit.band_t, unit.band_line), (0.0, 0.0, []))


class OrderTests(unittest.TestCase):
    def plan(self, planner, map_kind="open", unit_count=6):
        grid = make_map(map_kind, seed=3)
        traffic = TrafficModel(UsageMap(), ReservationMap())
        units, orders = build_scenario(grid, unit_count, "column")
        stats = OrderStats()
        for group, goal in orders:
            planner(grid, group, goal, traffic=traffic, stats=stats, now=0.0)
        return grid, units, traffic, stats

    def test_the_corridor_gives_every_unit_its_own_goal(self):
        _, units, _, stats = self.plan(order_corridor)
        self.assertTrue(all(unit.path and unit.goal for unit in units))
        self.assertIn(stats.diameter, pathlab.GROUP_PATH_DIAMETERS)
        self.assertEqual(len({unit.goal for unit in units}), len(units))

    def test_individual_paths_claim_ground_in_both_shared_maps(self):
        _, units, traffic, _ = self.plan(order_individual)
        self.assertTrue(all(unit.cells for unit in units))
        self.assertTrue(traffic.usage.count)
        self.assertTrue(traffic.reservations.at)
        for unit in units:
            self.assertEqual(traffic.usage.owner[unit.id], unit.cells)

    def test_the_corridor_is_searched_once_for_the_whole_group(self):
        _, _, _, corridor_stats = self.plan(order_corridor)
        _, units, _, single_stats = self.plan(order_individual)
        self.assertEqual(single_stats.searches, len(units))
        self.assertLess(corridor_stats.searches, single_stats.searches)

    def test_the_band_gives_every_unit_one_line_and_its_own_goal(self):
        _, units, _, stats = self.plan(order_band)
        self.assertTrue(all(unit.band_half > 0.0 for unit in units))
        self.assertEqual(len({id(unit.band_line) for unit in units}), 1)
        self.assertEqual(len({unit.goal for unit in units}), len(units))
        self.assertTrue(all(abs(unit.band_t) <= unit.band_half for unit in units))
        # the group keeps the shape it had: one t per rank of the spawn block, not one per unit -
        # units abreast along the direction of travel are a column and belong on one line
        self.assertGreater(len({unit.band_t for unit in units}), 1)
        self.assertIn(stats.diameter, pathlab.GROUP_PATH_DIAMETERS)

    def test_the_band_is_one_search_for_the_group_like_the_corridor(self):
        _, _, _, band_stats = self.plan(order_band)
        _, units, _, single_stats = self.plan(order_individual)
        self.assertLess(band_stats.searches, len(units))
        self.assertLess(band_stats.searches, single_stats.searches)

    def test_the_flat_band_is_the_same_plan_with_nobody_drifting(self):
        _, band_units, _, _ = self.plan(pathlab.PLANNERS["band"])
        _, flat_units, _, _ = self.plan(pathlab.PLANNERS["flat"])
        self.assertEqual([unit.path for unit in band_units], [unit.path for unit in flat_units])
        self.assertTrue(all(unit.band_step > 0.0 for unit in band_units))
        self.assertTrue(all(unit.band_step == 0.0 for unit in flat_units))

    def test_a_group_ordered_onto_itself_says_so_instead_of_moving(self):
        grid = make_map("open", seed=3)
        traffic = TrafficModel(UsageMap(), ReservationMap())
        units = spawn_block(grid, 4, *pathlab.SPAWN_CELL)
        stats = OrderStats()
        order_corridor(grid, units, compute_centroid(units), traffic=traffic, stats=stats, now=0.0)
        self.assertTrue(stats.notes)
        self.assertFalse(any(unit.path for unit in units))


class CorridorTests(unittest.TestCase):
    def two_doors(self) -> Grid:
        """A wall across the middle with a 2-cell door on the straight line and an 8-cell door far
           up it, more than a block clear of the corridor the straight line opens."""
        grid = open_grid(60)
        for y in range(60):
            grid.block(30, y)
            grid.block(31, y)
        grid.open_span(30, 39, 40)
        grid.open_span(30, 1, 8)
        return grid

    def test_a_block_split_by_a_wall_is_two_zones(self):
        grid = open_grid(10)
        for y in range(10):
            grid.block(5, y)
        zones = grid.block_zones(10)
        self.assertNotEqual(zones[2], zones[8])
        self.assertEqual(zones[5], 0)
        self.assertEqual(zones[2], zones[9 * 10 + 2])

    def test_the_coarse_route_steps_block_to_neighbouring_block(self):
        grid = self.two_doors()
        nodes, pushes = pathlab.find_block_route(grid, (5, 40), (55, 40), 10)
        self.assertGreater(pushes, 0)
        self.assertEqual((nodes[0], nodes[-1]), ((5, 40), (55, 40)))
        self.assertTrue(all(grid.passable(*node) for node in nodes))
        for first, second in zip(nodes[1:-1], nodes[2:-1]):
            blocks = (abs(first[0] // 10 - second[0] // 10), abs(first[1] // 10 - second[1] // 10))
            self.assertIn(blocks, ((1, 0), (0, 1)))

    def test_the_hard_corridor_refuses_the_door_only_a_group_fits_through(self):
        grid = self.two_doors()
        for mode, is_found in (("none", True), ("hard", False), ("flat", True), ("soft", True)):
            with pathlab.overriding(CORRIDOR=mode):
                cells, _ = astar(grid, (5, 40), (55, 40), clearance=3)
            self.assertEqual(bool(cells), is_found, mode)
        with pathlab.overriding(CORRIDOR="hard"):
            self.assertTrue(astar(grid, (5, 40), (55, 40))[0])     # a single unit gets through

    def test_a_capped_search_can_hand_back_the_route_it_has(self):
        cells, _ = astar(open_grid(41), (2, 2), (38, 38), cell_budget=5, partial=True)
        self.assertEqual(cells[0], (2, 2))
        self.assertNotEqual(cells[-1], (38, 38))

    def test_a_soft_point_keeps_the_defaults_it_does_not_name(self):
        point = pathlab.parse_soft("50/6")[0]
        self.assertEqual(point, (50, 6, pathlab.SOFT_NEAR, pathlab.SOFT_BONUS))


class SweepTests(unittest.TestCase):
    def test_cap_and_crossing_default_when_left_out(self):
        self.assertEqual(parse_sweep("5"),
                         [(5, PATH_CONGESTION_MAX_PATHS, 0)])

    def test_a_full_triple_is_read_in_order(self):
        self.assertEqual(parse_sweep("20/4/60,40/2/0"), [(20, 4, 60), (40, 2, 0)])

    def test_a_sweep_point_is_addressed_by_name(self):
        point = parse_sweep("20/4/60")[0]
        self.assertEqual((point.cost, point.cap, point.crossing), (20, 4, 60))


if __name__ == "__main__":
    unittest.main()
