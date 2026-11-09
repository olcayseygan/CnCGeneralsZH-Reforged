// The group panel (the wrapper's docs/research/RTS-OYUNLARI.md, "Bizim tasarım: grup paneli"): the
// local player's hotkey squads stacked against the left edge, and for a squad picked by its number a
// tray of build buttons against the right edge whose units come out into that squad.  Client side:
// every read of the logic happens at the render's sync point, inside InGameUI::postDraw's recording,
// or on the input path, and the logic hears of it only through messages.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __GROUPPANEL_H__
#define __GROUPPANEL_H__

#include "Lib/BaseType.h"

/** Draws the squad list and lays out the build tray, whose top edge is `rightTop`.  Called from
	* InGameUI::postDraw, which W3DInGameUI::recordDraw runs at the sync point with the drawing recorded. */
void GroupPanel_draw( Int rightTop );

/** A press at `mouse` on a squad row selects that squad as its number key does; TRUE when the press
	* was the list's.  `cancel` is a right press or a ctrl press, as InGameUI::handleHudCellClick has it. */
Bool GroupPanel_click( const ICoord2D *mouse, Bool cancel );

/** A number key picked `squad`, or a row click that went through one: the build tray opens for it. */
void GroupPanel_squadChosen( Int squad );

#endif // __GROUPPANEL_H__
