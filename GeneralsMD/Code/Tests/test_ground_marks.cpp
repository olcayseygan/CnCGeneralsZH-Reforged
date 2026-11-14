// test_ground_marks.cpp: the cap and the fade of the Battle Marks option's ring.

#include "test_harness.h"

#include "W3DDevice/GameClient/GroundMarks.h"
#include "W3DDevice/GameClient/StructureCollapse.h"

using namespace GroundMarks;

TEST(a_collapsing_building_waits_a_beat_then_gets_faster_and_is_under_by_its_time)
{
	using namespace StructureCollapse;
	CHECK_EQ(sunk(0.1f, 1.5f), 0.0f);
	CHECK(sunk(0.6f, 1.5f) < 0.5f * sunk(1.2f, 1.5f));
	CHECK_EQ(sunk(1.5f, 1.5f), 1.0f);
	CHECK(sinkSeconds(20.0f) < sinkSeconds(60.0f));
	CHECK_EQ(sinkSeconds(1000.0f), 2.5f);
}

TEST(a_mark_holds_then_fades_to_nothing)
{
	CHECK_EQ(strength(0), 1.0f);
	CHECK_EQ(strength(HOLD_FRAMES), 1.0f);
	CHECK_NEAR(strength(HOLD_FRAMES + FADE_FRAMES / 2), 0.5f, 0.001f);
	CHECK_EQ(strength(HOLD_FRAMES + FADE_FRAMES), 0.0f);
	CHECK_EQ(strength(HOLD_FRAMES + FADE_FRAMES + 1), 0.0f);
}

TEST(the_ring_never_holds_more_than_its_cap_and_the_oldest_goes_first)
{
	Ring ring;
	CHECK(ring.empty());
	const int extra = 10;
	for (int i = 0; i < CAPACITY + extra; ++i)
		ring.add((float)i * 1000.0f, 0.0f, 10.0f, 0, (unsigned int)i);
	CHECK_EQ(ring.newest(0).born, (unsigned int)(CAPACITY + extra - 1));
	CHECK_EQ(ring.newest(CAPACITY - 1).born, (unsigned int)extra);
}

TEST(a_mark_on_a_like_one_renews_it_and_takes_no_slot)
{
	Ring ring;
	ring.add(100.0f, 100.0f, 10.0f, 0, 1);
	ring.add(102.0f, 99.0f, 11.0f, 3, 50);	// another burn on the same spot
	CHECK_EQ(ring.newest(0).born, 50u);
	CHECK_EQ(ring.newest(1).radius, 0.0f);
	ring.add(100.0f, 100.0f, 10.0f, CRACKS, 60);	// not a burn: both stay
	CHECK_EQ(ring.newest(0).type, (int)CRACKS);
	CHECK_EQ(ring.newest(1).born, 50u);
}

TEST(a_ring_of_spent_marks_says_so)
{
	Ring ring;
	CHECK(!ring.anyLeft(0));
	ring.add(0.0f, 0.0f, 10.0f, 0, 5);
	CHECK(ring.anyLeft(5));
	CHECK(!ring.anyLeft(5 + HOLD_FRAMES + FADE_FRAMES));
	ring.clear();
	CHECK(ring.empty());
}
