// The keyboard schemes ("HotkeyScheme", GlobalData m_hotkeyScheme, OptionsCatalog.h's HOTKEY_SCHEME_*):
// Classic, the keys CommandMap.ini and Hotkeys.ini give today, and Red Alert, Red Alert 2's defaults
// fitted to this game, read from a player file of its own (docs/plans/HOTKEY-SCHEME-RA.md).  One table
// says what Red Alert changes; MetaMap::applyScheme puts it over the records before the player's file
// is read, and the options menu's Hotkeys page shows it as that scheme's defaults.  Client only.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __HOTKEYSCHEME_H__
#define __HOTKEYSCHEME_H__

#include <vector>

#include "Lib/BaseType.h"

struct HotkeyCommandRow;

/** One command the scheme binds: a CommandMap.ini name, a KEY_ name ("" takes the command's key away)
	* and a ModifierNames name.  A BEGIN_ row's END_ fires on the same key's release.  fromRa2 marks a
	* key Red Alert 2 itself gave; the rest are this game's. */
struct HotkeySchemeKey
{
	const char *command;
	const char *key;
	const char *modifiers;
	Bool fromRa2;
};

/** The scheme's table and its length; Classic has none (NULL, 0). */
const HotkeySchemeKey *HotkeyScheme_keys( Int scheme, Int *count );

/** The player file the scheme's keys live in, in the user data folder: Hotkeys.ini for Classic, which
	* the launcher reads too, HotkeysRedAlert.ini for Red Alert, which only the game reads. */
const char *HotkeyScheme_fileName( Int scheme );

/** The bar's fixed letters in FIXED_KEYS order (attack move, stop, guard, rally, evacuate, sell,
	* combat drop, switch): "ASGYVNCH", and "ASGYVLCH" under Red Alert, whose sell is L. */
const char *HotkeyScheme_fixedLetters( Int scheme );

/** The Hotkeys page's rows with the scheme's defaults in place of the game's own keys. */
void HotkeyScheme_applyToRows( std::vector< HotkeyCommandRow > &rows, Int scheme );

/** Pure: the first of `count` command bar slots whose GUICommandType (`types`) is one Deploy presses
	* and whose window is showing and enabled (`pressable`), or -1. */
Int HotkeyScheme_firstOwnSlot( const Int *types, const Bool *pressable, Int count );

/** Deploy (MSG_META_DEPLOY): press the selection's first own command on the bar; TRUE when one was. */
Bool HotkeyScheme_pressDeploy( void );

#endif // __HOTKEYSCHEME_H__
