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
#include "GameClient/ControlBar.h"

#include <vector>

class Player;
class ThingTemplate;
class CommandButton;
class GameWindow;

/** Draws the squad list and lays out the build tray, whose top edge is `rightTop`.  Called from
	* InGameUI::postDraw, which W3DInGameUI::recordDraw runs at the sync point with the drawing recorded. */
void GroupPanel_draw( Int rightTop );

/** A press at `mouse` on a squad row selects that squad as its number key does; TRUE when the press
	* was the list's.  `cancel` is a right press or a ctrl press, as InGameUI::handleHudCellClick has it. */
Bool GroupPanel_click( const ICoord2D *mouse, Bool cancel );

/** A number key picked `squad`, or a row click that went through one: the build tray opens for it. */
void GroupPanel_squadChosen( Int squad );

/** One unit type any of the player's working factories offers, with the best availability the
	* command bar would give it on any of them.  The build tray and the command bar's unit tabs share it. */
struct GroupPanelBuildable
{
	const ThingTemplate *thing;
	const CommandButton *button;	///< the first factory's, for the picture and the tooltip
	CommandAvailability best;			///< over every factory that offers it
};

/** Whether a factory's command set slot belongs in the lists: a unit build button the player sees on
	* the bar (populateCommand's rule: a SCRIPT_ONLY button, a mission's script-only build, and a slot
	* with no window on the bar are not shown).  What the button's availability then hides or greys,
	* buildable status, prerequisites, caps, a script-disabled factory, is ControlBar's to say. */
Bool GroupPanel_isPlayerBuildButton( Int commandType, UnsignedInt options, Bool onBar );

/** Two build buttons for what the player sees as one unit or structure: equivalent templates (a
	* reskin, a build variation), or the same name on the same picture, which is how a general's copy
	* of a side's unit (Lazr_AmericaVehicleHumvee) stands to the original. */
Bool GroupPanel_isSameBuild( const CommandButton *a, const CommandButton *b );

/** `thing` belongs to `player`'s own side or general, not a captured or foreign building's. */
Bool GroupPanel_isOwnSide( const ThingTemplate *thing, const Player *player );

/** Every unit build button `player`'s working factories carry, one entry a unit type, in the order
	* the factories and their command sets first name them.  Captured and foreign factories add what
	* the player's own do not make; a unit both make is listed once, the player's own side's copy. */
void GroupPanel_gatherBuildable( Player *player, std::vector< GroupPanelBuildable > &out );

/** A build button's look for an availability, as ControlBar::updateContextCommand reads one. */
void GroupPanel_showAvailability( GameWindow *button, CommandAvailability availability );

/** A click on a build button for `thing`: as many MSG_QUEUE_UNIT_FOR_GROUP as shift and ctrl ask for,
	* cut to the bank and the free queue slots.  `squad` 0-9 sends the units to that hotkey squad, -1
	* to no squad. */
void GroupPanel_queueUnit( const ThingTemplate *thing, Int squad );

#endif // __GROUPPANEL_H__
