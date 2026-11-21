/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// test_model_preview.cpp: the model preview's off-centre frustum (W3DDevice/GameClient/ModelPreviewFrame.h),
// through WWMath's Init_Perspective the way CameraClass::Update_Frustum builds it: the rectangle's
// corners land on its pixels, a square stays square on a wide screen, and a sphere framed by
// fitDistance stays inside the rectangle.

#include "test_harness.h"

#include "W3DDevice/GameClient/ModelPreviewFrame.h"
#include "W3DDevice/GameClient/SubObjectBoneWalk.h"
#include "matrix4.h"

#include <math.h>

using namespace ModelPreviewFrame;

namespace {

const float ZNEAR = 2.0f;
const float ZFAR = 500.0f;

Matrix4x4 projectionFor(const ViewPlane &v)
{
	Matrix4x4 m;
	m.Init_Perspective(v.minX * ZNEAR, v.maxX * ZNEAR, v.minY * ZNEAR, v.maxY * ZNEAR, ZNEAR, ZFAR);
	return m;
}

// a camera space point (looking down -Z) to screen pixels, as CameraClass::Project divides it
void toPixel(const Matrix4x4 &m, const Vector3 &p, float w, float h, float *px, float *py)
{
	const Vector4 c = m * p;
	ndcToPixel(c.X / c.W, c.Y / c.W, w, h, px, py);
}

}	// namespace

TEST(model_preview_panel_corners_land_on_the_panel)
{
	// an off-centre, non-square panel on a 16:9 screen
	const float W = 1920, H = 1080, X = 1250, Y = 210, PW = 520, PH = 640, T = 0.36f;
	const ViewPlane v = forPanel(W, H, X, Y, PW, PH, T);
	const Matrix4x4 m = projectionFor(v);

	// the narrower side (width) spans 2T on the view plane, the height in proportion
	const float hx = T, hy = T * PH / PW;
	const float depth = 37.0f;
	float px, py;
	toPixel(m, Vector3(-hx * depth, hy * depth, -depth), W, H, &px, &py);
	CHECK_NEAR(px, X, 0.01f);
	CHECK_NEAR(py, Y, 0.01f);
	toPixel(m, Vector3(hx * depth, -hy * depth, -depth), W, H, &px, &py);
	CHECK_NEAR(px, X + PW, 0.01f);
	CHECK_NEAR(py, Y + PH, 0.01f);
	// the view axis goes through the panel's middle
	toPixel(m, Vector3(0, 0, -depth), W, H, &px, &py);
	CHECK_NEAR(px, X + PW / 2, 0.01f);
	CHECK_NEAR(py, Y + PH / 2, 0.01f);

	// the whole screen's corners stay the screen's: NDC -1 and +1 at the view plane's ends
	Vector4 c = m * Vector3(v.minX * depth, v.minY * depth, -depth);
	CHECK_NEAR(c.X / c.W, -1.0f, 1e-4f);
	CHECK_NEAR(c.Y / c.W, -1.0f, 1e-4f);
	c = m * Vector3(v.maxX * depth, v.maxY * depth, -depth);
	CHECK_NEAR(c.X / c.W, 1.0f, 1e-4f);
	CHECK_NEAR(c.Y / c.W, 1.0f, 1e-4f);
}

TEST(model_preview_square_panel_keeps_a_square_square)
{
	const float W = 2560, H = 1080;	// 21:9, so a frustum stretched to the screen would show it
	const ViewPlane v = forPanel(W, H, 300, 500, 400, 400, 0.4f);
	const Matrix4x4 m = projectionFor(v);
	float x0, y0, x1, y1;
	toPixel(m, Vector3(-3, -3, -20), W, H, &x0, &y0);
	toPixel(m, Vector3(3, 3, -20), W, H, &x1, &y1);
	CHECK_NEAR(x1 - x0, y0 - y1, 0.01f);
	CHECK(x1 - x0 > 10.0f);
}

TEST(model_preview_fitted_points_fill_the_panel)
{
	// a long low body seen from three quarters, and one wide thin disc over it
	const float x[] = { -30, 30, -28, 28, 5, -5, 0, 44 };
	const float y[] = { -6, 4, 9, -9, 12, -12, 3, 14 };
	const float d[] = { 8, -8, 3, -3, 10, -10, 0, 2 };
	const int count = 8, held = 7;
	const float tanX = 0.36f * 1.5f, tanY = 0.36f;
	const Fit f = fitPoints(x, y, d, count, held, tanX, tanY, 0.9f, 1.1f);
	float widest = 0.0f;
	for (int i = 0; i < count; ++i)
	{
		const float px = fabsf(x[i] - f.centerX) / (f.distance - d[i]) / tanX;
		const float py = fabsf(y[i] - f.centerY) / (f.distance - d[i]) / tanY;
		const float share = i < held ? 0.9f : 1.1f;
		CHECK(px <= share + 1e-4f);
		CHECK(py <= share + 1e-4f);
		const float most = (px > py ? px : py) / share;
		widest = most > widest ? most : widest;
	}
	CHECK_NEAR(widest, 1.0f, 1e-4f);		// something touches its edge: the frame is tight
	CHECK_NEAR(f.centerX, 0.0f, 1e-4f);		// the held points' middle, not the disc's
	CHECK_NEAR(f.centerY, 0.0f, 1e-4f);
}

TEST(model_preview_framed_sphere_stays_in_the_panel)
{
	const float W = 1920, H = 1080, X = 100, Y = 100, PW = 300, PH = 500, T = 0.3f;
	const ViewPlane v = forPanel(W, H, X, Y, PW, PH, T);
	const Matrix4x4 m = projectionFor(v);
	const float r = 12.0f;
	const float d = fitDistance(r, T);
	// the sphere's silhouette edge sideways: the tangent point from the eye
	const float s = r / d;					// sine of the half angle the sphere fills
	const float c = sqrtf(1.0f - s * s);
	const float tx = r * c, tz = -d + r * s;	// tangent point on the x axis side, camera space
	float px, py;
	toPixel(m, Vector3(tx, 0, tz), W, H, &px, &py);
	CHECK_NEAR(px, X + PW, 0.05f);			// just touches the narrow side
	toPixel(m, Vector3(-tx, 0, tz), W, H, &px, &py);
	CHECK_NEAR(px, X, 0.05f);
	toPixel(m, Vector3(0, tx, tz), W, H, &px, &py);
	CHECK(py > Y);							// the long side has room
}

// ---- W3DDevice/GameClient/SubObjectBoneWalk.h: what a ShowSubObject or HideSubObject line carries
// along.  The bones are the rebuilt GLA Worker's (UIWRKR_SKL): the crate BOX is a skin on the root,
// the suicide vest SUICIDE01 rides PARA_MAN and the shoes SHOES01 a foot, both written hidden.

namespace {

struct WorkerBones
{
	int operator()(int bone) const
	{
		switch (bone)
		{
			case 1: return 0;		// UIWRKR_MAINRT
			case 2: return 1;		// UIWRKR
			case 3: return 2;		// PELVIS
			case 4: return 3;		// SPINE
			case 5: return 4;		// SPINE1
			case 16: return 5;		// PARA_MAN
			case 17: return 4;		// L THIGH
			case 18: return 17;		// L CALF
			case 19: return 18;		// L FOOT
			default: return 0;
		}
	}
};

struct Part { int bone; bool hiddenInFile; bool hidden; };

enum { BOX, WORKER, PICKBOX, SUICIDE, SHOES, PART_COUNT };

void loadWorker(Part *p)
{
	const Part parts[PART_COUNT] = {
		{ 0, false, false },		// BOX, a skin
		{ 0, false, false },		// WORKER, the body skin
		{ 2, false, false },		// a rigid retail piece on a bone
		{ 16, true, true },			// SUICIDE01
		{ 19, true, true },			// SHOES01
	};
	for (int i = 0; i < PART_COUNT; ++i)
		p[i] = parts[i];
}

// hideShowSubObject on part `which`: the part itself, then the walk, as W3DModelDraw does it
void hideShow(Part *p, int which, bool hide)
{
	p[which].hidden = hide;
	for (int i = 0; i < PART_COUNT; ++i)
		if (SubObjectBoneWalk::isBelow(WorkerBones(), p[which].bone, p[i].bone)
				&& !SubObjectBoneWalk::keepsFileHidden(p[which].bone, hide, p[i].hiddenInFile))
			p[i].hidden = hide;
}

}	// namespace

TEST(bone_walk_root_show_leaves_upgrade_parts_hidden)
{
	Part p[PART_COUNT];
	loadWorker(p);
	hideShow(p, BOX, false);				// ShowSubObject = BOX, a worker carrying a crate
	CHECK(!p[BOX].hidden);
	CHECK(!p[PICKBOX].hidden);				// a part the file shows is still carried along
	CHECK(p[SUICIDE].hidden);				// the vest and shoes stay as the file wrote them
	CHECK(p[SHOES].hidden);
}

TEST(bone_walk_root_hide_still_reaches_every_part)
{
	Part p[PART_COUNT];
	loadWorker(p);
	p[SUICIDE].hidden = false;				// the upgrade showed it
	hideShow(p, BOX, true);
	CHECK(p[PICKBOX].hidden);
	CHECK(p[SUICIDE].hidden);				// as EA's walk did; the upgrade's list puts it back after
	hideShow(p, BOX, false);
	CHECK(!p[PICKBOX].hidden);				// a show from the root undoes the hide for the file's parts
	CHECK(p[SUICIDE].hidden);
}

TEST(bone_walk_root_show_keeps_an_upgraded_part_shown)
{
	Part p[PART_COUNT];
	loadWorker(p);
	p[SUICIDE].hidden = false;				// Demo_Upgrade_SuicideBomb shows SUICIDE01 by name
	hideShow(p, BOX, false);
	CHECK(!p[SUICIDE].hidden);
	CHECK(p[SHOES].hidden);
}

TEST(bone_walk_show_below_the_root_reaches_hidden_parts_as_before)
{
	// a retail turret: TURRET01 on bone 5 brings its muzzle flash mesh, written hidden, on bone 16
	Part p[PART_COUNT];
	loadWorker(p);
	p[PICKBOX].bone = 5;
	hideShow(p, PICKBOX, false);
	CHECK(!p[SUICIDE].hidden);
	CHECK(p[SHOES].hidden);					// not below bone 5
}

TEST(bone_walk_matches_eas_parent_test)
{
	CHECK(SubObjectBoneWalk::isBelow(WorkerBones(), 0, 19));
	CHECK(SubObjectBoneWalk::isBelow(WorkerBones(), 4, 19));
	CHECK(!SubObjectBoneWalk::isBelow(WorkerBones(), 5, 19));
	CHECK(!SubObjectBoneWalk::isBelow(WorkerBones(), 0, 0));	// a root part is no one's child
	CHECK(!SubObjectBoneWalk::isBelow(WorkerBones(), 19, 19));
}
