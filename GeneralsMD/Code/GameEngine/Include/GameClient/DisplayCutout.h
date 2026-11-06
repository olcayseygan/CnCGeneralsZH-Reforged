// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once

#include "Lib/BaseType.h"

// Camera housing in game-display pixels. The platform supplies this optional borrowed query;
// no callback means an unobscured display, including the existing Windows/Linux paths.
typedef Bool (*DisplayCutoutQuery)( Int &x, Int &y, Int &width, Int &height );
extern DisplayCutoutQuery TheDisplayCutoutQuery;

// Move only an overlapping widget below the housing. Other screen areas keep their layout.
Int displayCutoutTopOffset( Int x, Int y, Int width, Int height );
