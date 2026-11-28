// test_terrain_skirt.cpp: the ground past the heightmap's edge (W3DDevice/GameClient/TerrainSkirt.h).
// It meets the edge at the edge's own height, keeps its hills low and the same on every run, goes
// black by the rim, and its ring walks every edge sample once.

#include "test_harness.h"

#include "W3DDevice/GameClient/TerrainSkirt.h"

#include <vector>

using namespace TerrainSkirt;

TEST(the_skirt_meets_the_edge_at_the_edge_height)
{
	for (int i = 0; i < 50; ++i)
	{
		const float edge = 10.0f + 3.0f * i;
		CHECK_NEAR(height(edge, edge, edge, 80.0f, 40.0f, 0.0f, 37.0f * i, -11.0f * i), edge, 1e-4f);
		// an edge under water (a river with no sea strip past it) meets its own bed, not its surface
		CHECK_NEAR(height(edge, edge, edge + 12.0f, 80.0f, 40.0f, 0.0f, 37.0f * i, -11.0f * i), edge, 1e-4f);
	}
}

TEST(a_river_bed_at_the_edge_digs_no_trench_past_it)
{
	// bed 10 on a flat floor, surface 20 over it: the ground past the edge climbs from the bed to just
	// over the water and never dips under the bed, where mixing bed and surface in one mirror dug a trench
	const float bed = 10.0f, dry = 20.0f + SHORE_RISE;
	float last = bed;
	for (int i = 1; i <= 30; ++i)
	{
		const float z = carried(bed, bed, dry, CELL * i, 120.0f, -340.0f);
		CHECK(z >= bed - 1e-4f);
		CHECK(z >= last - 1e-4f);
		CHECK(z <= dry + 1e-4f);
		last = z;
	}
	CHECK_NEAR(last, dry, 1e-4f);
}

TEST(far_out_it_settles_on_the_edge_mean_plus_low_hills)
{
	for (int i = 0; i < 200; ++i)
	{
		const float x = -500.0f - 13.7f * i, y = 2000.0f + 7.3f * i;
		const float z = height(150.0f, 200.0f, 200.0f, 120.0f, 50.0f, REACH, x, y);
		CHECK(z >= 50.0f - 1e-3f);
		CHECK(z <= 50.0f + HILL_HEIGHT + 1e-3f);
		CHECK_EQ(z, height(150.0f, 200.0f, 200.0f, 120.0f, 50.0f, REACH, x, y));
		// the mirrored map no longer counts past the carried relief's reach
		CHECK_EQ(height(150.0f, 0.0f, 0.0f, 120.0f, 50.0f, CARRY_HOLD + CARRY_RAMP, x, y),
			height(150.0f, 255.0f, 255.0f, 120.0f, 50.0f, CARRY_HOLD + CARRY_RAMP, x, y));
	}
}

TEST(the_mirror_folds_every_sample_onto_the_map)
{
	const int last = 10;
	CHECK_EQ(mirrorSample(0, last), 0);
	CHECK_EQ(mirrorSample(last, last), last);
	CHECK_EQ(mirrorSample(-3, last), 3);
	CHECK_EQ(mirrorSample(last + 3, last), last - 3);
	CHECK_EQ(mirrorSample(-last - 4, last), last - 4);	// past the far side too, on a small map
	for (int x = -200; x <= 200; ++x)
	{
		const int m = mirrorSample(x, last);
		CHECK(m >= 0 && m <= last);
		const int step = mirrorSample(x + 1, last) - m;
		CHECK(step == 1 || step == -1);	// neighbours stay neighbours: no step
	}
}

// A map 61 samples square, the south edge at y 0: a ridge 40 samples wide in x runs north-south
// across the edge, 120 high on a floor of 20, sloping up toward the edge by 0.5 a sample.  Its height
// past the south edge as skirtHeight works it out (no water).
static float ridgeMap(int x, int y)
{
	const float across = (float)(x - 30) / 6.0f;
	return 20.0f + 120.0f * expf(-across * across) + 0.5f * (60 - y) * 0.25f;
}

static float pastSouthEdge(int x, int y)
{
	const int last = 60;
	const int cx = clampCell(x, last), cy = clampCell(y, last);
	const float edge = ridgeMap(cx, cy);
	const float mirrored = ridgeMap(mirrorSample(x, last), mirrorSample(y, last));
	const float dx = (float)(x - cx), dy = (float)(y - cy);
	const float outside = sqrtf(dx * dx + dy * dy) * CELL;
	float blurred = 0.0f;	// blurRing along the south row, near enough
	for (int k = -PROFILE_RADIUS; k <= PROFILE_RADIUS; ++k)
		blurred += ridgeMap(clampCell(cx + k, last), cy) / (2 * PROFILE_RADIUS + 1);
	return height(edge, mirrored, mirrored, blurred, 40.0f, outside, x * CELL, y * CELL);
}

TEST(a_ridge_at_the_edge_runs_on_past_it)
{
	// it meets the map with no step: one cell out moves no more than a cell in does, either side of the
	// ridge, and the slope into the edge carries on rather than folding back
	for (int x = 0; x <= 60; ++x)
	{
		const float in = ridgeMap(x, 1) - ridgeMap(x, 0);
		const float out = pastSouthEdge(x, -1) - ridgeMap(x, 0);
		CHECK(fabsf(out) <= fabsf(in) + 0.5f);
		if (fabsf(in) > 0.05f)
			CHECK(out * in <= 0.0f);	// down where the map climbs away from the edge: the same slope on
	}
	// the ridge still stands 250 units out, most of its height over the floor beside it, where the old
	// pull to the blurred edge had flattened it within 150
	for (int y = -5; y >= -25; --y)
	{
		const float crest = pastSouthEdge(30, y), flank = pastSouthEdge(5, y);
		CHECK(crest - flank > 0.8f * 120.0f);
	}
	// and no step anywhere past the edge, round the corners too
	for (int y = -40; y <= 0; ++y)
		for (int x = -40; x <= 100; ++x)
		{
			if (x >= 0 && x <= 60 && y >= 0)
				continue;
			const float z = pastSouthEdge(x, y);
			CHECK(fabsf(pastSouthEdge(x + 1, y) - z) < 45.0f);
			CHECK(fabsf(pastSouthEdge(x, y - 1) - z) < 45.0f);
		}
}

TEST(past_a_sea_edge_it_stays_under_the_water)
{
	const float waterZ = 20.0f;
	for (int i = 0; i <= 160; ++i)
	{
		const float outside = 10.0f * i;
		// a blurred edge up on the beach still keeps the floor under the water...
		const float z = seaHeight(12.0f, 35.0f, waterZ, outside);
		CHECK(z <= waterZ - SEA_MARGIN + 1e-4f || outside == 0.0f);
		// ...and a deep one stays deep, with no hills and no climb to any mean
		CHECK(seaHeight(3.0f, 2.0f, waterZ, outside) <= 3.0f + 1e-4f);
	}
	CHECK_NEAR(seaHeight(12.0f, 35.0f, waterZ, 0.0f), 12.0f, 1e-4f);
	CHECK_NEAR(seaHeight(12.0f, 35.0f, waterZ, REACH), waterZ - SEA_MARGIN, 1e-4f);
	// a flat shallow sea (Tournament Island: floor 0, water 4) keeps the map's own depth out to the rim
	for (int i = 0; i <= 160; ++i)
		CHECK_NEAR(seaHeight(0.0f, 0.0f, 4.0f, 10.0f * i), 0.0f, 1e-4f);
	// the floor follows the blur's depth with no step, from a beach (the whole margin) to deep water
	float last = seaHeight(5.0f, 25.0f, waterZ, REACH);
	CHECK_NEAR(last, waterZ - SEA_MARGIN, 1e-4f);
	for (int i = 1; i <= 300; ++i)
	{
		const float blurred = 25.0f - 0.1f * i;
		const float z = seaHeight(5.0f, blurred, waterZ, REACH);
		CHECK(fabsf(z - last) < 0.25f);
		CHECK(z <= waterZ - SEA_MARGIN_LEAST + 1e-4f);
		last = z;
	}
	// a rock on a shallow edge does not carry its own depth out: the blur round it sets the floor
	CHECK_NEAR(seaHeight(3.5f, 0.0f, 4.0f, REACH), seaHeight(0.0f, 0.0f, 4.0f, REACH), 1e-4f);
}

TEST(a_coast_mixes_land_and_sea_by_the_sea_share)
{
	const float waterZ = 20.0f;
	CHECK_EQ(coastHeight(80.0f, 10.0f, 0.0f, waterZ), 80.0f);
	CHECK_NEAR(coastHeight(80.0f, 10.0f, 0.5f, waterZ), 45.0f, 1e-4f);
	// no sea strip over it, so lowered land stops just over the water instead of a dry trench...
	CHECK_NEAR(coastHeight(80.0f, 10.0f, 1.0f, waterZ), waterZ + SHORE_RISE, 1e-4f);
	CHECK_NEAR(coastHeight(80.0f, 10.0f, 0.9f, waterZ), waterZ + SHORE_RISE, 1e-4f);
	// ...and land already lower than that is left where it is
	CHECK_EQ(coastHeight(15.0f, 10.0f, 0.5f, waterZ), 15.0f);
	// the sea flag blurred along the ring turns over a coast, never in one step
	std::vector<float> sea(60, 0.0f);
	for (int i = 30; i < 60; ++i)
		sea[i] = 1.0f;
	std::vector<float> share = blurRing(sea);
	for (int i = 1; i < 60; ++i)
		CHECK(fabsf(share[i] - share[i - 1]) <= 1.0f / (2 * PROFILE_RADIUS + 1) + 1e-5f);
}

TEST(the_hills_have_no_steps)
{
	// one cell apart, the noise moves a little: a lattice seam would jump
	for (int i = 0; i < 2000; ++i)
	{
		const float x = -3000.0f + 3.1f * i, y = 1234.0f - 1.7f * i;
		const float step = fabsf(hills(x + CELL, y) - hills(x, y));
		CHECK(step < 0.1f);
	}
}

// What each layer of a tile comes to at corner k once all are laid, keyed by patch (the band is 0).
static void cornerWeights(const PatchLayer *layers, float alpha[MOST_LAYERS][4], int count, int k, unsigned *patch, float *weight)
{
	for (int l = 0; l < count; ++l)
	{
		float w = alpha[l][k];
		for (int m = l + 1; m < count; ++m)
			w *= 1.0f - alpha[m][k];
		patch[l] = layers[l].patch;
		weight[l] = w;
	}
}

// The layer a tile shows most of, over its four corners.
static PatchLayer mainLayer(int x, int y, int w, int h)
{
	PatchLayer layers[MOST_LAYERS];
	float alpha[MOST_LAYERS][4];
	const int count = tileLayers(x, y, w, h, layers, alpha);
	float total[MOST_LAYERS] = {};
	for (int k = 0; k < 4; ++k)
	{
		unsigned patch[MOST_LAYERS];
		float weight[MOST_LAYERS];
		cornerWeights(layers, alpha, count, k, patch, weight);
		for (int l = 0; l < count; ++l)
			total[l] += weight[l];
	}
	int best = 0;
	for (int l = 1; l < count; ++l)
		best = total[l] > total[best] ? l : best;
	return layers[best];
}

TEST(a_tile_past_the_edge_takes_its_texture_from_its_patch)
{
	const int w = 129, h = 97;
	PatchLayer layers[MOST_LAYERS];
	float alpha[MOST_LAYERS][4];
	// on the map a tile is the map's own, and along the edge line the band takes it all
	CHECK_EQ(tileLayers(10, 20, w, h, layers, alpha), 1);
	CHECK_EQ(layers[0].patch, 0u);
	CHECK_EQ(layers[0].cellX, 20);
	CHECK_EQ(layers[0].cellY, 40);
	for (int x = 0; x < 64; ++x)
	{
		const int count = tileLayers(x, -1, w, h, layers, alpha);
		unsigned patch[MOST_LAYERS];
		float weight[MOST_LAYERS];
		for (int k = 2; k < 4; ++k)
		{
			cornerWeights(layers, alpha, count, k, patch, weight);
			for (int l = 0; l < count; ++l)
				CHECK_NEAR(weight[l], patch[l] == 0u ? 1.0f : 0.0f, 1e-5f);
		}
	}
	// from the fourth tile out (the edge carried on no further) straight out from the bottom edge the
	// cell it reads changes from patch to patch, and a patch reads one cell, along the edge and never
	// deeper in than PATCH_SHIFT
	unsigned lastPatch = 0;
	int lastX = -1, lastY = -1, patches = 0, moved = 0, regrained = 0;
	for (int s = -4; s >= -80; --s)
	{
		const PatchLayer base = mainLayer(30, s, w, h);
		CHECK(base.patch != 0u);
		CHECK(base.cellX >= 0 && base.cellX <= w - 2);
		CHECK(base.cellY >= 0 && base.cellY <= PATCH_SHIFT);
		if (base.patch == lastPatch)
		{
			CHECK_EQ(base.cellX, lastX);
			CHECK_EQ(base.cellY, lastY);
		}
		else
		{
			patches++;
			if (base.cellX != lastX)
				moved++;
			else if (base.cellY == lastY)
				regrained++;	// the same class, another grain: the block turned again inside the patch
		}
		lastPatch = base.patch;
		lastX = base.cellX;
		lastY = base.cellY;
	}
	CHECK(patches >= 4);
	CHECK(moved >= 3);
	CHECK(regrained >= 4);
}

// The weight of `patch` at corner k of a tile, 0 if it wears none of it.
static float weightOf(unsigned patch, const unsigned *patches, const float *weights, int count)
{
	for (int l = 0; l < count; ++l)
		if (patches[l] == patch)
			return weights[l];
	return 0.0f;
}

TEST(patches_blend_where_they_meet_and_agree_on_either_side)
{
	// At a corner two tiles share, each tile's own layers come to the same mix, however many patches
	// meet there and whichever each tile lays, so the blend has no seam; and the mix is often well
	// between, so no border is cut hard.  Past a corner of the map too.
	const int w = 129, h = 97;
	int compared = 0, between = 0, mismatched = 0, three = 0;
	for (int y = -70; y < 10; ++y)
		for (int x = -70; x < 80; ++x)
		{
			if (tilesOutside(x + 0.5f, y + 0.5f, w, h) <= 0.0f)
				continue;
			PatchLayer layersA[MOST_LAYERS], layersB[MOST_LAYERS];
			float alphaA[MOST_LAYERS][4], alphaB[MOST_LAYERS][4];
			const int countA = tileLayers(x, y, w, h, layersA, alphaA);
			three += countA >= 3;
			// the right neighbour shares corners 1, 2 as its 0, 3; the one above corners 3, 2 as its 0, 1
			for (int side = 0; side < 2; ++side)
			{
				const int nx = side == 0 ? x + 1 : x, ny = side == 0 ? y : y + 1;
				if (tilesOutside(nx + 0.5f, ny + 0.5f, w, h) <= 0.0f)
					continue;
				const int countB = tileLayers(nx, ny, w, h, layersB, alphaB);
				const int mine[2][2] = { { 1, 2 }, { 3, 2 } }, theirs[2][2] = { { 0, 3 }, { 0, 1 } };
				for (int c = 0; c < 2; ++c)
				{
					unsigned patchA[MOST_LAYERS], patchB[MOST_LAYERS];
					float weightA[MOST_LAYERS], weightB[MOST_LAYERS];
					cornerWeights(layersA, alphaA, countA, mine[side][c], patchA, weightA);
					cornerWeights(layersB, alphaB, countB, theirs[side][c], patchB, weightB);
					bool same = true;
					for (int l = 0; l < countA; ++l)
					{
						same = same && fabsf(weightA[l] - weightOf(patchA[l], patchB, weightB, countB)) < 1e-4f;
						between += weightA[l] > 0.2f && weightA[l] < 0.8f;
					}
					for (int l = 0; l < countB; ++l)
						same = same && fabsf(weightB[l] - weightOf(patchB[l], patchA, weightA, countA)) < 1e-4f;
					compared++;
					mismatched += !same;				}
			}
		}
	CHECK(compared > 10000);
	CHECK_EQ(mismatched, 0);
	CHECK(between > 500);
	CHECK(three > 50);
}

TEST(a_patch_keeps_its_class_whole_and_turns_the_block)
{
	const int width = 4;
	int a = -1, d = -1;
	float s[4], t[4];
	// patch 0 is the map's own order, upright
	scatterTile(13, -6, width, 0u, &a, &d, s, t);
	CHECK_EQ(a, 1);
	CHECK_EQ(d, 2);
	const float squareS[4] = { 0.0f, 1.0f, 1.0f, 0.0f }, squareT[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	for (int k = 0; k < 4; ++k)
	{
		CHECK_EQ(s[k], squareS[k]);
		CHECK_EQ(t[k], squareT[k]);
	}
	// inside one patch the corner two neighbours share is one point of the class's picture, however
	// the block is turned, and the turns differ from patch to patch
	int turned = 0;
	for (unsigned patch = 1; patch < 40; ++patch)
	{
		int a1 = -1, d1 = -1, a2 = -1, d2 = -1;
		float s1[4], t1[4], s2[4], t2[4];
		scatterTile(20, -30, width, patch * 2654435761u, &a1, &d1, s1, t1);
		scatterTile(21, -30, width, patch * 2654435761u, &a2, &d2, s2, t2);
		const int left[2] = { 1, 2 }, right[2] = { 0, 3 };
		for (int k = 0; k < 2; ++k)
		{
			const int x1 = ((int)(a1 + s1[left[k]])) % width, y1 = ((int)(d1 + t1[left[k]])) % width;
			const int x2 = ((int)(a2 + s2[right[k]])) % width, y2 = ((int)(d2 + t2[right[k]])) % width;
			CHECK_EQ(x1, x2);
			CHECK_EQ(y1, y2);
		}
		if (s1[0] != 0.0f || t1[0] != 0.0f || s1[1] != 1.0f)
			turned++;
	}
	CHECK(turned > 20);
}

TEST(the_baked_shade_is_the_maps_and_averages_one)
{
	// the same hash as the shader's terrain_hash: the cell (0, 0) hashes to nought, (1, 0) does not
	CHECK_EQ(variationHash(0, 0), 0.0f);
	CHECK(variationHash(1, 0) > 0.0f && variationHash(1, 0) < 1.0f);
	double sum = 0.0;
	const int samples = 4000;
	for (int i = 0; i < samples; ++i)
	{
		float rgb[3];
		variation(-2000.0f + 37.3f * i, 900.0f - 23.9f * i, rgb);
		for (int c = 0; c < 3; ++c)
			CHECK(rgb[c] > 0.6f && rgb[c] < 1.4f);
		sum += rgb[1];
	}
	CHECK_NEAR(sum / samples, 1.0, 0.05);
}

TEST(the_light_is_whole_near_the_map_and_gone_at_the_rim)
{
	CHECK_EQ(fade(0.0f), 1.0f);
	CHECK_EQ(fade(FADE_START * REACH), 1.0f);
	CHECK_EQ(fade(REACH), 0.0f);
	float last = 1.0f;
	for (int i = 0; i <= 100; ++i)
	{
		const float f = fade(REACH * i / 100.0f);
		CHECK(f <= last + 1e-6f);
		last = f;
	}
}

TEST(the_ring_takes_every_edge_sample_once)
{
	const int w = 9, h = 6;
	std::vector<int> seen(ringLength(w, h), 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const int i = ringIndex(x, y, w, h);
			const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
			CHECK_EQ(i >= 0, edge);
			if (i >= 0 && i < (int)seen.size())
				seen[i]++;
		}
	for (size_t i = 0; i < seen.size(); ++i)
		CHECK_EQ(seen[i], 1);
	// and neighbours on the ring are neighbours on the map, round the corners too
	CHECK_EQ(ringIndex(w - 1, 1, w, h), ringIndex(w - 1, 0, w, h) + 1);
	CHECK_EQ(ringIndex(w - 2, h - 1, w, h), ringIndex(w - 1, h - 1, w, h) + 1);
	CHECK_EQ(ringIndex(0, h - 2, w, h), ringIndex(0, h - 1, w, h) + 1);
}

TEST(the_blur_keeps_a_flat_edge_flat_and_wraps)
{
	std::vector<float> ring(40, 30.0f);
	std::vector<float> out = blurRing(ring);
	for (size_t i = 0; i < out.size(); ++i)
		CHECK_NEAR(out[i], 30.0f, 1e-4f);
	ring[0] = 30.0f + (2 * PROFILE_RADIUS + 1);
	out = blurRing(ring);
	CHECK_NEAR(out[39], 31.0f, 1e-4f);
	CHECK_NEAR(out[PROFILE_RADIUS], 31.0f, 1e-4f);
	CHECK_NEAR(out[PROFILE_RADIUS + 1], 30.0f, 1e-4f);
}

TEST(scenery_stands_off_the_edge_and_stops_before_the_rim)
{
	CHECK_EQ(sceneryKeep(0.0f), 0.0f);
	CHECK_EQ(sceneryKeep(SCENERY_NEAR), 0.0f);
	CHECK_EQ(sceneryKeep(SCENERY_FAR), 0.0f);
	CHECK_EQ(sceneryKeep(REACH), 0.0f);
	// denser a little way out than beside the edge
	CHECK(sceneryKeep(SCENERY_NEAR + 30.0f) < sceneryKeep(SCENERY_NEAR + SCENERY_RAMP));
	CHECK(sceneryKeep(SCENERY_NEAR + SCENERY_RAMP) > 0.8f);
	for (int i = 0; i < 1000; ++i)
	{
		const float x = -3000.0f + 7.3f * i, y = 500.0f - 3.1f * i;
		CHECK_EQ(growth(x, y, 10.0f, 0.0f), GROWTH_NONE);
		CHECK_EQ(growth(x, y, SCENERY_FAR + 1.0f, 0.0f), GROWTH_NONE);
		CHECK(!rockAt(x, y, SCENERY_FAR, 0.0f));
		CHECK(!cragAt(x, y, CRAG_NEAR - 1.0f, 0.0f));
	}
}

TEST(scenery_clumps_into_woods_and_open_ground_the_same_every_run)
{
	// a field of sites at full density: some woods, much open ground, and the same answer twice
	int trees = 0, open = 0, sites = 0;
	int woodsRows = 0;
	for (int j = 0; j < 120; ++j)
	{
		int rowTrees = 0;
		for (int i = 0; i < 120; ++i)
		{
			const ScenerySite site = scenerySite(i, j, TREE_STEP, 1);
			const ScenerySite again = scenerySite(i, j, TREE_STEP, 1);
			CHECK_EQ(site.x, again.x);
			CHECK_EQ(site.u, again.u);
			CHECK(site.x >= i * TREE_STEP && site.x <= (i + 1) * TREE_STEP);
			const Growth g = growth(site.x, site.y, SCENERY_NEAR + SCENERY_RAMP, site.u);
			CHECK_EQ(g, growth(site.x, site.y, SCENERY_NEAR + SCENERY_RAMP, site.u));
			++sites;
			if (g == GROWTH_TREE)
			{
				++trees;
				++rowTrees;
			}
			else if (g == GROWTH_NONE)
				++open;
		}
		if (rowTrees > 40)
			++woodsRows;
	}
	CHECK(trees > sites / 8);
	CHECK(trees < sites * 3 / 5);
	CHECK(open > sites / 4);
	CHECK(woodsRows > 0);
}

TEST(a_stand_is_mostly_one_kind_and_every_kind_comes_up)
{
	const float weights[3] = { 1.0f, 4.0f, 2.0f };
	int seen[3] = { 0, 0, 0 };
	int same = 0, pairs = 0;
	for (int i = 0; i < 4000; ++i)
	{
		const float x = 37.0f * i, y = -11.0f * i;
		const int k = pickKind(weights, 3, x, y, lattice(i, 7));
		CHECK(k >= 0 && k < 3);
		seen[k]++;
		// a neighbour a tree away
		const int n = pickKind(weights, 3, x + TREE_STEP, y, lattice(i, 9));
		++pairs;
		if (n == k)
			++same;
	}
	CHECK(seen[0] > 0 && seen[1] > 0 && seen[2] > 0);
	CHECK(seen[1] > seen[0]);
	CHECK(same * 10 > pairs * 6);
	CHECK_EQ(pickKind(weights, 0, 0.0f, 0.0f, 0.5f), -1);
}
