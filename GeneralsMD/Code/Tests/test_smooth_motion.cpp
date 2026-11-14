/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// R1, smooth motion: the blend of two logic transforms and the per-tick verdicts
// (GameEngineDevice/Include/W3DDevice/GameClient/W3DSmoothMotion.h), without a render object.

#include "test_harness.h"

#include "W3DDevice/GameClient/W3DSmoothMotion.h"
#include "W3DDevice/GameClient/W3DDrawableUpdate.h"

#include <math.h>

static const float EPS = 1.0e-4f;

static Matrix3D at(float x, float y, float z, float z_degrees = 0.0f, float scale = 1.0f)
{
	Matrix3D m(true);
	m.Rotate_Z(z_degrees * 3.14159265358979f / 180.0f);
	m.Scale(scale);
	m.Set_Translation(Vector3(x, y, z));
	return m;
}

static float heading_degrees(const Matrix3D &m)
{
	return atan2f(m[1][0], m[0][0]) * 180.0f / 3.14159265358979f;
}

/* The bones a draw module sets by hand (a turret's turn, a gun's pitch, a barrel's recoil, a road
	 wheel) glide between ticks with the hull: each one the tick moved is blended from last tick's
	 control; one the last tick did not set, one that did not move, one that swung past the snap limit
	 and every bone at a whole tick on are left at the tick's control. */
TEST(smooth_motion_hand_set_bones_glide_between_ticks)
{
	SmoothMotionBone prev[3], cur[4], out[4];
	prev[0].Bone = 5; prev[0].Transform = at(0, 0, 0, 0.0f);			// turret
	prev[1].Bone = 9; prev[1].Transform = at(0, 0, 0);					// barrel, at rest
	prev[2].Bone = 2; prev[2].Transform = at(0, 0, 0, 10.0f);			// a wheel that did not turn
	cur[0].Bone = 9; cur[0].Transform = at(-1.2f, 0, 0);				// the barrel kicks back 1.2
	cur[1].Bone = 5; cur[1].Transform = at(0, 0, 0, 40.0f);				// the turret turns 40 degrees
	cur[2].Bone = 2; cur[2].Transform = at(0, 0, 0, 10.0f);
	cur[3].Bone = 7; cur[3].Transform = at(0, 0, 0, 25.0f);				// a bone last tick did not set

	const int n = SmoothMotion_BlendBones(prev, 3, cur, 4, 0.25f, out);
	CHECK_EQ(n, 2);
	CHECK_EQ(out[0].Bone, 9);
	CHECK_NEAR(out[0].Transform.Get_Translation().X, -0.3f, EPS);
	CHECK_EQ(out[1].Bone, 5);
	CHECK_NEAR(heading_degrees(out[1].Transform), 10.0f, 0.01f);

	// the ends: a whole tick on sets nothing (the tick's own controls stand); a hair past the start is
	// the last tick's, so the blend runs on from where the picture before the tick left the bone
	CHECK_EQ(SmoothMotion_BlendBones(prev, 3, cur, 4, 1.0f, out), 0);
	CHECK_EQ(SmoothMotion_BlendBones(prev, 3, cur, 4, 2.0f, out), 0);
	SmoothMotion_BlendBones(prev, 3, cur, 4, 0.0001f, out);
	CHECK_NEAR(heading_degrees(out[1].Transform), 0.004f, 0.01f);
	CHECK_NEAR(out[0].Transform.Get_Translation().X, 0.0f, 0.001f);

	// a turret that swung past the snap limit in one tick is shown where the tick put it
	SmoothMotionBone swung = cur[1];
	swung.Transform = at(0, 0, 0, 150.0f);
	CHECK_EQ(SmoothMotion_BlendBones(prev, 3, &swung, 1, 0.5f, out), 0);
	// no last tick at all
	CHECK_EQ(SmoothMotion_BlendBones(NULL, 0, cur, 4, 0.5f, out), 0);
}

/* The bone track only blends bones both ticks set on the very model being drawn: a new model (a pooled
	 HLod back at an old address included, which the owner forgets everything for) waits a tick, an index
	 past the model's bones is never set, and a bone a pass sets itself (a recoil) is out of the blend,
	 so a barrel that is also its pitch bone shows the kick as set. */
TEST(smooth_motion_bone_track_stays_on_its_model)
{
	int modelA = 0, modelB = 0;
	SmoothMotionBoneTrack track;
	track.set(&modelA, 3, at(0, 0, 0, 0.0f));
	track.set(&modelA, 3, at(0, 0, 0, 5.0f));		// the latest call for a bone wins
	track.capture(&modelA);
	CHECK_EQ(track.blend(&modelA, 10, 0.5f), 0);	// one tick only: nothing to blend from
	track.set(&modelA, 3, at(0, 0, 0, 45.0f));
	track.set(&modelA, 12, at(0, 0, 0, 10.0f));		// an index past the model's ten bones
	track.capture(&modelA);
	track.Prev.push_back(track.Prev[0]);
	track.Prev.back().Bone = 12;
	CHECK_EQ(track.blend(&modelA, 10, 0.5f), 1);
	CHECK_EQ(track.Shown[0].Bone, 3);
	CHECK_NEAR(heading_degrees(track.Shown[0].Transform), 25.0f, 0.01f);
	CHECK(track.tickControl(&modelA, 3) != NULL);
	CHECK_NEAR(heading_degrees(*track.tickControl(&modelA, 3)), 45.0f, 0.01f);
	CHECK(track.tickControl(&modelB, 3) == NULL);
	CHECK_EQ(track.blend(&modelB, 10, 0.5f), 0);	// another model at draw time

	// the model replaced: forgotten, then the new one waits a whole tick before anything blends
	track.forget();
	track.set(&modelA, 3, at(0, 0, 0, 90.0f));		// a pooled object back at the same address
	track.capture(&modelA);
	CHECK_EQ(track.blend(&modelA, 10, 0.5f), 0);
	// bones set on another model in the pending tick are dropped, not mixed in
	track.set(&modelB, 4, at(0, 0, 0, 0.0f));
	track.capture(&modelA);
	CHECK_EQ(track.blend(&modelA, 10, 0.5f), 0);

	// a recoil bone set every pass is taken out of the blend
	SmoothMotionBoneTrack gun;
	gun.set(&modelA, 6, at(0, 0, 0, 0.0f));
	gun.capture(&modelA);
	gun.set(&modelA, 6, at(0, 0, 0, 20.0f));
	gun.capture(&modelA);
	CHECK_EQ(gun.blend(&modelA, 10, 0.5f), 1);
	gun.unsmoothed(6);
	CHECK_EQ(gun.blend(&modelA, 10, 0.5f), 0);
	CHECK(gun.tickControl(&modelA, 6) == NULL);
}

/* A light pulse's strength steps once a tick; a picture between ticks shows it part way from the last
	 tick's.  The tick itself (alpha 1, smooth motion off) is the tick's value bit for bit, and the end
	 of one tick is the start of the next, so a decay runs down without steps. */
TEST(smooth_motion_light_strength_between_ticks)
{
	CHECK_EQ(SmoothMotion_Lerp(0.8f, 0.6f, 1.0f), 0.6f);
	CHECK_EQ(SmoothMotion_Lerp(0.8f, 0.6f, 1.5f), 0.6f);
	CHECK_EQ(SmoothMotion_Lerp(0.8f, 0.6f, 0.0f), 0.8f);
	CHECK_EQ(SmoothMotion_Lerp(0.8f, 0.6f, -1.0f), 0.8f);
	CHECK_NEAR(SmoothMotion_Lerp(0.8f, 0.6f, 0.25f), 0.75f, EPS);
	// a pulse decaying over 5 ticks: 5/5 at its birth, 4/5 after the first step, ...
	float last = 1.0f;
	for (int tick = 1; tick <= 4; ++tick)
	{
		const float from = (5.0f - (float)(tick - 1)) / 5.0f, to = (5.0f - (float)tick) / 5.0f;
		CHECK_EQ(SmoothMotion_Lerp(from, to, 0.0f), last);		// continues from the last picture of the tick before
		for (int q = 1; q <= 4; ++q)
		{
			const float shown = SmoothMotion_Lerp(from, to, (float)q / 4.0f);
			CHECK(shown <= last + EPS && shown >= to - EPS);
			last = shown;
		}
		CHECK_EQ(last, to);
	}
	// a growing pulse starts from nothing
	CHECK_NEAR(SmoothMotion_Lerp(0.0f, 1.0f / 3.0f, 0.5f), 1.0f / 6.0f, EPS);
}

/* A pulse at full strength from its birth comes up from nothing over the tick that brings it rather
	 than lighting a wreck and the ground on the tick: over the whole tick if it lives five ticks or
	 more, faster the shorter it lives, not at all if it lives one; one that grows already starts from
	 nothing and takes its growth as it was. */
TEST(smooth_motion_light_pulse_comes_up_over_its_first_tick)
{
	CHECK_EQ(SmoothMotion_LightBirthSpan(0, 30), 1.0f);
	CHECK_EQ(SmoothMotion_LightBirthSpan(0, 5), 1.0f);
	CHECK_NEAR(SmoothMotion_LightBirthSpan(0, 3), 0.5f, EPS);
	CHECK_NEAR(SmoothMotion_LightBirthSpan(0, 2), 0.25f, EPS);
	CHECK_EQ(SmoothMotion_LightBirthSpan(0, 1), 0.0f);
	CHECK_EQ(SmoothMotion_LightBirthSpan(0, 0), 1.0f);		// never decays: the whole tick
	CHECK_EQ(SmoothMotion_LightBirthSpan(4, 3), 1.0f);		// grows: its own ramp

	CHECK_NEAR(SmoothMotion_Hurry(0.25f, 1.0f), 0.25f, EPS);
	CHECK_NEAR(SmoothMotion_Hurry(0.25f, 0.5f), 0.5f, EPS);
	CHECK_EQ(SmoothMotion_Hurry(0.75f, 0.5f), 1.0f);
	CHECK_EQ(SmoothMotion_Hurry(1.0f, 1.0f), 1.0f);
	CHECK_EQ(SmoothMotion_Hurry(0.1f, 0.0f), 1.0f);
	// a three-tick flash (1 at birth, 2/3 after its first step) reaches the step's strength half way
	// into the tick, and a picture a quarter in shows half of it
	const float to = 2.0f / 3.0f;
	CHECK_NEAR(SmoothMotion_Lerp(0.0f, to, SmoothMotion_Hurry(0.25f, SmoothMotion_LightBirthSpan(0, 3))), to * 0.5f, EPS);
	CHECK_EQ(SmoothMotion_Lerp(0.0f, to, SmoothMotion_Hurry(0.5f, SmoothMotion_LightBirthSpan(0, 3))), to);
}

TEST(smooth_motion_the_same_transform_blends_to_itself)
{
	Matrix3D out;
	const Matrix3D m = at(10.0f, 20.0f, 5.0f, 30.0f);
	CHECK_EQ((int)SmoothMotion_Blend(m, m, 0.37f, out), (int)SMOOTH_BLENDED);
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 4; ++c)
			CHECK_NEAR(out[r][c], m[r][c], EPS);
}

TEST(smooth_motion_translation_is_lerped)
{
	Matrix3D out;
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0), at(8, -4, 2), 0.25f, out), (int)SMOOTH_BLENDED);
	CHECK_NEAR(out.Get_Translation().X, 2.0f, EPS);
	CHECK_NEAR(out.Get_Translation().Y, -1.0f, EPS);
	CHECK_NEAR(out.Get_Translation().Z, 0.5f, EPS);
}

TEST(smooth_motion_alpha_zero_and_one_are_the_ends)
{
	Matrix3D out;
	const Matrix3D a = at(0, 0, 0, 10.0f), b = at(3, 4, 0, 50.0f);
	SmoothMotion_Blend(a, b, 0.0f, out);
	CHECK_NEAR(out.Get_Translation().X, 0.0f, EPS);
	CHECK_NEAR(heading_degrees(out), 10.0f, 1.0e-3f);
	SmoothMotion_Blend(a, b, 1.0f, out);
	CHECK_NEAR(out.Get_Translation().X, 3.0f, EPS);
	CHECK_NEAR(heading_degrees(out), 50.0f, 1.0e-3f);
}

TEST(smooth_motion_rotation_is_slerped_the_short_way)
{
	Matrix3D out;
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0, 0.0f), at(0, 0, 0, 90.0f), 0.5f, out), (int)SMOOTH_BLENDED);
	CHECK_NEAR(heading_degrees(out), 45.0f, 1.0e-3f);
	// Across the +-180 seam: 170 to -170 is 20 degrees, not 340.
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0, 170.0f), at(0, 0, 0, -170.0f), 0.5f, out), (int)SMOOTH_BLENDED);
	CHECK_NEAR(fabsf(heading_degrees(out)), 180.0f, 1.0e-2f);
}

TEST(smooth_motion_scale_is_lerped_per_column)
{
	Matrix3D out;
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0, 0.0f, 1.0f), at(0, 0, 0, 0.0f, 2.0f), 0.5f, out), (int)SMOOTH_BLENDED);
	CHECK_NEAR(out.Get_X_Vector().Length(), 1.5f, EPS);
	CHECK_NEAR(out.Get_Z_Vector().Length(), 1.5f, EPS);
}

TEST(smooth_motion_snaps_a_long_step_a_big_turn_and_a_shear)
{
	Matrix3D out;
	const Matrix3D b = at(61, 0, 0);
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0), b, 0.5f, out), (int)SMOOTH_SNAP_DISTANCE);
	CHECK_NEAR(out.Get_Translation().X, 61.0f, EPS);	// shown where the logic is
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0), at(59, 0, 0), 0.5f, out), (int)SMOOTH_BLENDED);
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0, 0.0f), at(0, 0, 0, 121.0f), 0.5f, out), (int)SMOOTH_SNAP_ROTATION);
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0, 0.0f), at(0, 0, 0, 119.0f), 0.5f, out), (int)SMOOTH_BLENDED);
	Matrix3D shear(true);
	shear[0][1] = 0.5f;	// the Y column leans on X
	CHECK_EQ((int)SmoothMotion_Blend(at(0, 0, 0), shear, 0.5f, out), (int)SMOOTH_SNAP_NOT_RIGID);
}

TEST(smooth_motion_track_verdicts_per_tick)
{
	unsigned long long *counts = SmoothMotion_Counts();
	unsigned long long before[SMOOTH_SNAP_COUNT];
	for (int i = 0; i < SMOOTH_SNAP_COUNT; ++i) before[i] = counts[i];
	const int model_a = 0, model_b = 0;
	SmoothMotionTrack t;
	t.capture(at(0, 0, 0), &model_a, false, 10, false);
	CHECK_EQ((int)t.Snap, (int)SMOOTH_SNAP_FIRST);
	t.capture(at(1, 0, 0), &model_a, false, 11, false);
	CHECK_EQ((int)t.Snap, (int)SMOOTH_BLENDED);
	CHECK_NEAR(t.Prev.Get_Translation().X, 0.0f, EPS);
	CHECK_NEAR(t.Cur.Get_Translation().X, 1.0f, EPS);
	t.capture(at(2, 0, 0), &model_a, false, 13, false);	// a tick not seen
	CHECK_EQ((int)t.Snap, (int)SMOOTH_SNAP_FRAME_GAP);
	t.capture(at(3, 0, 0), &model_a, true, 14, false);	// hidden now
	CHECK_EQ((int)t.Snap, (int)SMOOTH_SNAP_HIDDEN);
	t.capture(at(4, 0, 0), &model_a, false, 15, false);	// was hidden
	CHECK_EQ((int)t.Snap, (int)SMOOTH_SNAP_HIDDEN);
	t.capture(at(5, 0, 0), &model_a, false, 16, true);	// a teleport the logic named
	CHECK_EQ((int)t.Snap, (int)SMOOTH_SNAP_MARKED);
	t.capture(at(6, 0, 0), &model_b, false, 17, false);	// a new render object
	CHECK_EQ((int)t.Snap, (int)SMOOTH_SNAP_NEW_MODEL);
	t.capture(at(100, 0, 0), &model_b, false, 18, false);
	CHECK_EQ((int)t.Snap, (int)SMOOTH_SNAP_DISTANCE);
	CHECK_EQ((int)(counts[SMOOTH_BLENDED] - before[SMOOTH_BLENDED]), 1);
	CHECK_EQ((int)(counts[SMOOTH_SNAP_HIDDEN] - before[SMOOTH_SNAP_HIDDEN]), 2);
}

// A model that did not move between two ticks is blended in name only: W3DModelDraw leaves its
// transform alone rather than setting the same matrix every picture.
TEST(smooth_motion_track_marks_an_unmoved_model_still)
{
	const int model = 0;
	SmoothMotionTrack t;
	t.capture(at(5, 5, 0), &model, false, 20, false);
	CHECK(!t.Still);	// nothing before it
	t.capture(at(5, 5, 0), &model, false, 21, false);
	CHECK_EQ((int)t.Snap, (int)SMOOTH_BLENDED);
	CHECK(t.Still);
	t.capture(at(6, 5, 0), &model, false, 22, false);
	CHECK_EQ((int)t.Snap, (int)SMOOTH_BLENDED);
	CHECK(!t.Still);
	t.capture(at(6, 5, 0), &model, false, 24, false);	// unmoved over a tick not seen: a snap, not still
	CHECK(!t.Still);
}

// The parts are taken once a tick at capture, and a picture only mixes them: off the midpoint, so
// swapped slerp weights show, and against WWMath's own Slerp, which every picture used to call.
TEST(smooth_motion_track_poses_from_parts_taken_at_capture)
{
	const int model = 0;
	SmoothMotionTrack t;
	t.capture(at(0, 0, 0, 0.0f, 1.0f), &model, false, 30, false);
	t.capture(at(8, 4, 0, 90.0f, 2.0f), &model, false, 31, false);
	CHECK_EQ((int)t.Snap, (int)SMOOTH_BLENDED);
	Matrix3D out;
	SmoothMotion_Pose(t.Prev, t.Cur, t.Parts, 0.25f, out);
	CHECK_NEAR(heading_degrees(out), 22.5f, 1.0e-3f);
	CHECK_NEAR(out.Get_Translation().X, 2.0f, EPS);
	CHECK_NEAR(out.Get_Translation().Y, 1.0f, EPS);
	CHECK_NEAR(out.Get_X_Vector().Length(), 1.25f, EPS);

	const float pairs[][2] = { { 0.0f, 90.0f }, { 170.0f, -170.0f }, { 10.0f, 10.001f } };
	const float alphas[] = { 0.1f, 0.37f, 0.8f };
	for (int p = 0; p < 3; ++p) {
		const Matrix3D from = at(0, 0, 0, pairs[p][0]), to = at(0, 0, 0, pairs[p][1]);
		SmoothMotionParts parts;
		CHECK_EQ((int)SmoothMotion_Prepare(from, to, parts), (int)SMOOTH_BLENDED);
		Matrix3D from_rotation, to_rotation;
		float scale[3];
		SmoothMotion_Split(from, from_rotation, scale);
		SmoothMotion_Split(to, to_rotation, scale);
		for (int a = 0; a < 3; ++a) {
			Quaternion q;
			Slerp(q, Build_Quaternion(from_rotation), Build_Quaternion(to_rotation), alphas[a]);
			Matrix3D expected;
			Build_Matrix3D(q, expected);
			SmoothMotion_Pose(from, to, parts, alphas[a], out);
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					CHECK_NEAR(out[r][c], expected[r][c], 1.0e-6f);
		}
	}
}

TEST(drawable_update_composes_flushed_client_frame_when_pause_stops_animation)
{
	W3DDrawableUpdate update;
	CHECK(update.needsUpdate(42, true, 0));
	CHECK(!update.needsUpdate(42, false, 0));
	// The overlapping tick flushes frame 43 after the render, replacing client poses with
	// raw transforms. Opening the pause menu freezes animation before the next view update.
	CHECK(update.needsUpdate(43, false, 0));
	for (int picture = 0; picture < 8; ++picture)
		CHECK(!update.needsUpdate(43, false, 0));
	// On resume an advancing animation clock still requests an update within the same frame.
	CHECK(update.needsUpdate(43, true, 0));
	CHECK(update.needsUpdate(44, true, 0));
}

TEST(drawable_update_first_view_and_reset_do_not_require_animation_time)
{
	W3DDrawableUpdate update;
	CHECK(update.needsUpdate(0, false, 0));
	CHECK(!update.needsUpdate(0, false, 0));
	// A new map can start at the same frame as the previous view's last picture.
	update.reset();
	CHECK(update.needsUpdate(0, false, 0));
	CHECK(update.needsUpdate(90, false, 0));
	update.reset();
	CHECK(update.needsUpdate(0, false, 0));
	CHECK(!update.needsUpdate(0, false, 0));
}

TEST(drawable_update_views_consume_client_frames_independently)
{
	W3DDrawableUpdate primary, observer;
	CHECK(primary.needsUpdate(25, false, 0));
	CHECK(observer.needsUpdate(25, false, 0));
	CHECK(!primary.needsUpdate(25, false, 0));
	CHECK(!observer.needsUpdate(25, false, 0));
	CHECK(primary.needsUpdate(26, false, 0));
	CHECK(!primary.needsUpdate(26, false, 0));
	CHECK(observer.needsUpdate(26, false, 0));
}

TEST(drawable_update_capture_pictures_skip_without_consuming_client_frame)
{
	W3DDrawableUpdate update;
	CHECK(!update.needsUpdate(0, true, 1));
	CHECK(update.needsUpdate(0, false, 0));
	CHECK(!update.needsUpdate(1, true, 1));
	CHECK(!update.needsUpdate(1, false, 2));
	CHECK(update.needsUpdate(1, false, 0));
	CHECK(!update.needsUpdate(1, false, 0));
}
