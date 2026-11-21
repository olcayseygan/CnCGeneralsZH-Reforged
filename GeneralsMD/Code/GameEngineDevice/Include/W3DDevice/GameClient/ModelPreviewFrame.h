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

// ModelPreviewFrame.h: the camera maths that puts a model preview (Display::drawModelPreview) inside
// one rectangle of the screen.
//
// The camera's viewport sub-rectangle never reaches Direct3D 11 (its backend sets the viewport to the
// whole target), so the viewport stays the whole screen and the rectangle goes into the view plane
// instead: an off-centre frustum whose axis passes through the rectangle's middle, the same on every
// backend at any resolution and aspect.  The view plane is CameraClass's, at a distance of one from
// the eye, min the lower left and max the upper right (Init_Perspective's left, bottom and right,
// top), and a pixel covers the same span of it both ways, so a square stays square.  The narrower side
// of the rectangle spans 2 * tanHalf.  No WW3D types here: test_model_preview checks it against
// WWMath's own projection.

#ifndef MODEL_PREVIEW_FRAME_H
#define MODEL_PREVIEW_FRAME_H

#include <math.h>

namespace ModelPreviewFrame
{

struct ViewPlane
{
	float minX, minY, maxX, maxY;
};

/// the whole screen's view plane for a rectangle at x, y (top left, pixels) of w by h
inline ViewPlane forPanel(float screenW, float screenH, float x, float y, float w, float h, float tanHalf)
{
	const float narrow = w < h ? w : h;
	const float pixel = 2.0f * tanHalf / narrow;
	const float cx = x + 0.5f * w;
	const float cy = y + 0.5f * h;
	ViewPlane v;
	v.minX = -cx * pixel;
	v.maxX = (screenW - cx) * pixel;
	v.maxY = cy * pixel;					// the screen's top row, y up on the view plane
	v.minY = (cy - screenH) * pixel;
	return v;
}

/// the eye's distance from a sphere's centre at which the sphere just touches the view's narrower sides
inline float fitDistance(float radius, float tanHalf)
{
	return radius * sqrtf(1.0f + tanHalf * tanHalf) / tanHalf;
}

/// where the eye goes for fitPoints: the aim point moved across the view by centerX, centerY, and the
/// eye that far back from it along the view axis
struct Fit
{
	float centerX, centerY, distance;
};

/** The tightest frame round a set of points, for a model seen at one angle rather than every angle
	* its turn sweeps.  x and y are each point across the view (right, up), d toward the eye, all from
	* the aim point; tanX and tanY the view's half spans on the view plane.  The first held points set
	* the centre and project inside heldShare of the half spans; the rest (a rotor's disc) only inside
	* looseShare, so a thin wide part can poke out a little rather than shrink the body. */
inline Fit fitPoints(const float *x, const float *y, const float *d, int count, int held,
	float tanX, float tanY, float heldShare, float looseShare)
{
	Fit f = { 0.0f, 0.0f, 0.0f };
	if (count <= 0)
		return f;
	if (held <= 0 || held > count)
		held = count;
	float minX = x[0], maxX = x[0], minY = y[0], maxY = y[0];
	for (int i = 1; i < held; ++i)
	{
		minX = x[i] < minX ? x[i] : minX;
		maxX = x[i] > maxX ? x[i] : maxX;
		minY = y[i] < minY ? y[i] : minY;
		maxY = y[i] > maxY ? y[i] : maxY;
	}
	f.centerX = 0.5f * (minX + maxX);
	f.centerY = 0.5f * (minY + maxY);
	// a point at depth d toward the eye projects at offset / (distance - d)
	for (int i = 0; i < count; ++i)
	{
		const float share = i < held ? heldShare : looseShare;
		const float dx = fabsf(x[i] - f.centerX) / (tanX * share);
		const float dy = fabsf(y[i] - f.centerY) / (tanY * share);
		const float need = d[i] + (dx > dy ? dx : dy);
		f.distance = need > f.distance ? need : f.distance;
	}
	return f;
}

/// a projected point (normalised device coordinates, y up) in screen pixels over a whole-screen viewport
inline void ndcToPixel(float ndcX, float ndcY, float screenW, float screenH, float *px, float *py)
{
	*px = (ndcX + 1.0f) * 0.5f * screenW;
	*py = (1.0f - ndcY) * 0.5f * screenH;
}

}	// namespace ModelPreviewFrame

#endif
