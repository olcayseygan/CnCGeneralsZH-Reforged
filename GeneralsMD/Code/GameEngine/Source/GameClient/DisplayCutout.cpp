// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

#include "GameClient/DisplayCutout.h"
#include <limits.h>
#include <stdint.h>

DisplayCutoutQuery TheDisplayCutoutQuery = NULL;

Int displayCutoutTopOffset( Int x, Int y, Int width, Int height )
{
	DisplayCutoutQuery query = TheDisplayCutoutQuery;
	if (query == NULL || width <= 0 || height <= 0)
		return 0;
	Int cutoutX = 0, cutoutY = 0, cutoutWidth = 0, cutoutHeight = 0;
	if (!query( cutoutX, cutoutY, cutoutWidth, cutoutHeight )
		|| cutoutX < 0 || cutoutY < 0 || cutoutWidth <= 0 || cutoutHeight <= 0)
		return 0;
	const int64_t right = int64_t(x) + width, bottom = int64_t(y) + height;
	const int64_t cutoutRight = int64_t(cutoutX) + cutoutWidth, cutoutBottom = int64_t(cutoutY) + cutoutHeight;
	if (cutoutRight > INT_MAX || cutoutBottom > INT_MAX)
		return 0;
	if (right <= cutoutX || cutoutRight <= x || bottom <= cutoutY || cutoutBottom <= y)
		return 0;
	const int64_t offset = cutoutBottom - y;
	// Both the offset and the resulting top must fit the UI's coordinate type.
	return offset <= INT_MAX ? Int(offset) : 0;
}
