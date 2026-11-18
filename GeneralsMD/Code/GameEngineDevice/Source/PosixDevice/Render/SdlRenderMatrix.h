// Render-only row-major matrix operations. No simulation arithmetic uses this header.
#pragma once
#include <math.h>

static void multiply(const float left[16], const float right[16], float result[16])
{
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) {
				sum += left[row * 4 + index] * right[index * 4 + column];
			}
			result[row * 4 + column] = sum;
		}
	}
}

// Visual-only inverse for reconstructing render positions from depth. Double intermediates keep
// distant camera translations stable; this does not reach simulation or replay arithmetic.
static bool render_inverse(const float source[16], float result[16])
{
	double rows[4][8];
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 8; ++c)
			rows[r][c] = c < 4 ? source[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
	for (unsigned c = 0; c < 4; ++c) {
		unsigned pivot = c;
		for (unsigned r = c + 1; r < 4; ++r)
			if (fabs(rows[r][c]) > fabs(rows[pivot][c])) pivot = r;
		if (!isfinite(rows[pivot][c]) || fabs(rows[pivot][c]) < 1e-15) return false;
		for (unsigned k = 0; k < 8; ++k) {
			const double saved = rows[c][k]; rows[c][k] = rows[pivot][k]; rows[pivot][k] = saved;
		}
		const double scale = rows[c][c];
		for (unsigned k = 0; k < 8; ++k) rows[c][k] /= scale;
		for (unsigned r = 0; r < 4; ++r) {
			if (r == c) continue;
			const double factor = rows[r][c];
			for (unsigned k = 0; k < 8; ++k) rows[r][k] -= factor * rows[c][k];
		}
	}
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 4; ++c) result[r * 4 + c] = (float)rows[r][c + 4];
	return true;
}

// Cosmetic model reconstruction samples the full attachment; its viewport can exclude the HUD.
// Only this render-only conversion changes: the authoritative camera matrices remain untouched.
static bool render_full_texture_to_world(const float view[16], const float projection[16],
	unsigned width, unsigned height, const float viewport[6], float result[16], float projectionScale[2])
{
	if (!width || !height || viewport[0] < 0 || viewport[1] < 0 || viewport[2] <= 0 || viewport[3] <= 0 ||
		viewport[0] + viewport[2] > width || viewport[1] + viewport[3] > height || viewport[5] <= viewport[4]) return false;
	float clip[16], inverse[16];
	multiply(view, projection, clip);
	if (!render_inverse(clip, inverse)) return false;
	float cameraNdc[16] = {};
	cameraNdc[0] = float(width) / viewport[2]; cameraNdc[5] = float(height) / viewport[3];
	cameraNdc[10] = 1 / (viewport[5] - viewport[4]); cameraNdc[15] = 1;
	cameraNdc[12] = (float(width) - 2 * viewport[0] - viewport[2]) / viewport[2];
	cameraNdc[13] = (2 * viewport[1] + viewport[3] - height) / viewport[3];
	cameraNdc[14] = -viewport[4] * cameraNdc[10];
	multiply(cameraNdc, inverse, result);
	projectionScale[0] = fabsf(projection[0]) * viewport[2] / width;
	projectionScale[1] = fabsf(projection[5]) * viewport[3] / height;
	return projectionScale[0] > 0 && projectionScale[1] > 0;
}
