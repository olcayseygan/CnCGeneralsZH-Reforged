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
