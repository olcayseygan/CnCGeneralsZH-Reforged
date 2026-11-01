// test_fire_lights.cpp: the grouping and the cap of the Firelight option.

#include "test_harness.h"

#include "W3DDevice/GameClient/FireLights.h"

using namespace FireLights;

static Flames flamesAt(float weight, float x, float y, float z)
{
	Flames flames;
	flames.add(weight, x, y, z);
	return flames;
}

static const float VIEW_CENTER[3] = { 0.0f, 0.0f, 0.0f };
static const float VIEW_EXTENT[3] = { 1000.0f, 1000.0f, 1000.0f };

TEST(flames_near_each_other_are_one_light_and_far_ones_are_two)
{
	Gather gather;
	gather.add(flamesAt(FULL_WEIGHT, 0.0f, 0.0f, 0.0f));
	gather.add(flamesAt(FULL_WEIGHT, 20.0f, 0.0f, 0.0f));
	CHECK_EQ(gather.count(), 1);
	gather.add(flamesAt(FULL_WEIGHT, 500.0f, 0.0f, 0.0f));
	CHECK_EQ(gather.count(), 2);

	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 2);
	CHECK_NEAR(lights[0].x, 10.0f, 0.01f);
	CHECK_NEAR(lights[0].z, LIFT, 0.01f);
	// two systems ten units either side of the middle widen the reach past a lone one's
	CHECK(lights[0].reach > lights[1].reach);
}

TEST(a_muzzle_flare_lights_nothing_and_a_fire_comes_on_from_nothing)
{
	CHECK_EQ(strength(MIN_WEIGHT), 0.0f);
	CHECK(strength(MIN_WEIGHT + 1.0f) < 0.01f);
	CHECK_EQ(strength(FULL_WEIGHT), 1.0f);
	CHECK_EQ(strength(FULL_WEIGHT * 10.0f), 1.0f);

	Gather gather;
	gather.add(flamesAt(MIN_WEIGHT - 1.0f, 0.0f, 0.0f, 0.0f));
	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 0);
}

TEST(the_strongest_fires_take_the_capped_lights)
{
	Gather gather;
	for (int i = 0; i < MAX_LIGHTS * 2; ++i)
		gather.add(flamesAt(MIN_WEIGHT + 10.0f * (float)(i + 1), (float)i * 200.0f - 2000.0f, 0.0f, 0.0f));
	const float wide[3] = { 10000.0f, 10000.0f, 10000.0f };
	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, wide, 0, lights), (int)MAX_LIGHTS);
	const float weakestKept = strength(MIN_WEIGHT + 10.0f * (float)(MAX_LIGHTS + 1));
	for (int i = 0; i < MAX_LIGHTS; ++i)
		CHECK(lights[i].strength >= weakestKept - 0.001f);
}

TEST(a_fire_out_of_reach_of_the_view_is_no_light)
{
	Gather gather;
	gather.add(flamesAt(FULL_WEIGHT, 5000.0f, 0.0f, 0.0f));
	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 0);
}

TEST(the_flicker_stays_in_its_band_and_moves)
{
	float low = 2.0f, high = 0.0f;
	for (unsigned int frame = 0; frame < 2000; ++frame) {
		const float f = flicker(frame, 300.0f, 700.0f);
		low = f < low ? f : low;
		high = f > high ? f : high;
	}
	CHECK(low >= 0.78f - 0.001f);
	CHECK(high <= 1.0f + 0.001f);
	CHECK(high - low > 0.1f);
}
