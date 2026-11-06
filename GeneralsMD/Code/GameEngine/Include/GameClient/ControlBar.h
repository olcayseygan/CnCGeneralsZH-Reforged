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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: ControlBar.h /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Context sensitive command interface
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CONTROLBAR_H_
#define __CONTROLBAR_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "Common/CommandStore.h"
#include "Common/GameType.h"
#include "Common/LogicThread.h"
#include "Common/Overridable.h"
#include "Common/Science.h"
#include "GameClient/Color.h"
#include "Common/BitFlags.h"
#include "GameLogic/WeaponBonusConditionFlags.h"
#include "GameLogic/WeaponSetFlags.h"

#include <functional>

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Drawable;
class GameWindow;
class Image;
class Object;
class ThingTemplate;
class WeaponTemplate;
class WeaponBonus;
class SpecialPowerTemplate;
class WindowVideoManager;
class WindowVideoManager;
class AnimateWindowManager;
class GameWindow;
class WindowLayout;
class Player;
class PlayerTemplate;
class AudioEventRTS;
class ControlBarSchemeManager;
class UpgradeTemplate;
class ProductionUpdateInterface;
class GameWindowTransitionsHandler;
class DisplayString;

enum ProductionID : Int;

enum CommandSourceType : Int;
enum ProductionType : Int;
enum GadgetGameMessage : Int;
enum ScienceType : Int;
enum TimeOfDay : Int;
enum RadiusCursorType : Int;

//-------------------------------------------------------------------------------------------------
/** Command options */
//-------------------------------------------------------------------------------------------------
enum CommandOption : Int
{
	COMMAND_OPTION_NONE					= 0x00000000,
	NEED_TARGET_ENEMY_OBJECT		= 0x00000001, // command now needs user to select enemy target
	NEED_TARGET_NEUTRAL_OBJECT	= 0x00000002, // command now needs user to select neutral target
	NEED_TARGET_ALLY_OBJECT			= 0x00000004, // command now needs user to select ally target
#ifdef ALLOW_SURRENDER
	NEED_TARGET_PRISONER				= 0x00000008, // needs user to now select prisoner object
#endif
	ALLOW_SHRUBBERY_TARGET			= 0x00000010, // allow neutral shrubbery as a target
	NEED_TARGET_POS							= 0x00000020, // command now needs user to select target position
	NEED_UPGRADE								= 0x00000040, // command requires upgrade to be enabled
	NEED_SPECIAL_POWER_SCIENCE	= 0x00000080, // command requires a science in the special power specified
	OK_FOR_MULTI_SELECT					= 0x00000100, // command is ok to show when multiple objects selected
	CONTEXTMODE_COMMAND					= 0x00000200, // a context sensitive command mode that requires code to determine whether cursor is valid or not.
	CHECK_LIKE									= 0x00000400, // dynamically change the UI element push button to be "check like"
	ALLOW_MINE_TARGET						= 0x00000800, // allow (land)mines as a target
	ATTACK_OBJECTS_POSITION			=	0x00001000, // for weapons that need an object target but attack the position indirectly (like burning trees)
	OPTION_ONE									= 0x00002000, // User data -- option 1
	OPTION_TWO									= 0x00004000,	// User data -- option 2
	OPTION_THREE								= 0x00008000,	// User data -- option 3
	NOT_QUEUEABLE								= 0x00010000,	// Option not build queueable meaning you can only build it when queue is empty!
	SINGLE_USE_COMMAND					= 0x00020000, // Once used, it can never be used again!
	COMMAND_FIRED_BY_SCRIPT			= 0x00040000, // Used only by code to tell special powers that they have been fired by a script.
	SCRIPT_ONLY									= 0x00080000, // Only a script can use this command (not by users)
	IGNORES_UNDERPOWERED				= 0x00100000, // this button isn't disabled if its object is merely underpowered
	USES_MINE_CLEARING_WEAPONSET= 0x00200000,	// uses the special mine-clearing weaponset, even if not current
	CAN_USE_WAYPOINTS						= 0x00400000, // button has option to use a waypoint path
	MUST_BE_STOPPED							= 0x00800000, // Unit must be stopped in order to be able to use button.

	NUM_COMMAND_OPTIONS						// keep this last
};

#ifdef DEFINE_COMMAND_OPTION_NAMES
static const char *TheCommandOptionNames[] = 
{
	"NEED_TARGET_ENEMY_OBJECT",
	"NEED_TARGET_NEUTRAL_OBJECT",
	"NEED_TARGET_ALLY_OBJECT",
#ifdef ALLOW_SURRENDER
	"NEED_TARGET_PRISONER",
#else
	"unused-reserved",
#endif
	"ALLOW_SHRUBBERY_TARGET",
	"NEED_TARGET_POS",
	"NEED_UPGRADE",
	"NEED_SPECIAL_POWER_SCIENCE",
	"OK_FOR_MULTI_SELECT",
	"CONTEXTMODE_COMMAND",
	"CHECK_LIKE",
	"ALLOW_MINE_TARGET",
	"ATTACK_OBJECTS_POSITION",
	"OPTION_ONE",
	"OPTION_TWO",
	"OPTION_THREE",
	"NOT_QUEUEABLE",
	"SINGLE_USE_COMMAND",
	"---DO-NOT-USE---", //COMMAND_FIRED_BY_SCRIPT
	"SCRIPT_ONLY",
	"IGNORES_UNDERPOWERED",
	"USES_MINE_CLEARING_WEAPONSET",
	"CAN_USE_WAYPOINTS",
	"MUST_BE_STOPPED",

	NULL
};
#endif  // end DEFINE_COMMAND_OPTION_NAMES

// convenient bit masks to group some command options together
const UnsignedInt COMMAND_OPTION_NEED_TARGET = 
					NEED_TARGET_ENEMY_OBJECT |
					NEED_TARGET_NEUTRAL_OBJECT |
					NEED_TARGET_ALLY_OBJECT |
					NEED_TARGET_POS |
					CONTEXTMODE_COMMAND;

const UnsignedInt COMMAND_OPTION_NEED_OBJECT_TARGET =
					NEED_TARGET_ENEMY_OBJECT |
					NEED_TARGET_NEUTRAL_OBJECT |
					NEED_TARGET_ALLY_OBJECT;

//-------------------------------------------------------------------------------------------------
/** These are the list of commands that can be assigned to buttons that will appear
	* in the context sensitive GUI for a selected unit.  Not all commands are available
	* on all units, in fact, many commands are for a particular single command.  It will
	* be up to the command GUI to translate the command assigned to a button into the
	* appropriate game command and get it across the network to the game logic to perform the
	* actual command logic 
	*
	* IMPORTANT: Make sure the GUICommandType enum and the TheGuiCommandNames[] have the same
	*						 entries in the same order */
//-------------------------------------------------------------------------------------------------
enum GUICommandType : Int
{
	GUI_COMMAND_NONE = 0,									///< invalid command
	GUI_COMMAND_DOZER_CONSTRUCT,					///< dozer construct
	GUI_COMMAND_DOZER_CONSTRUCT_CANCEL,		///< cancel a dozer construction process
	GUI_COMMAND_UNIT_BUILD,								///< build a unit
	GUI_COMMAND_CANCEL_UNIT_BUILD,				///< cancel a unit build
	GUI_COMMAND_PLAYER_UPGRADE,						///< put an upgrade that applies to the player in the queue
	GUI_COMMAND_OBJECT_UPGRADE,						///< put an object upgrade in the queue
	GUI_COMMAND_CANCEL_UPGRADE,						///< cancel an upgrade
	GUI_COMMAND_ATTACK_MOVE,							///< attack move command
	GUI_COMMAND_GUARD,										///< guard command
	GUI_COMMAND_GUARD_WITHOUT_PURSUIT,		///< guard command, no pursuit out of guard area
	GUI_COMMAND_GUARD_FLYING_UNITS_ONLY,	///< guard command, ignore nonflyers
	GUI_COMMAND_STOP,											///< stop moving
	GUI_COMMAND_WAYPOINTS,								///< create a set of waypoints for this unit
	GUI_COMMAND_EXIT_CONTAINER,						///< an inventory box for a container like a structure or transport
	GUI_COMMAND_EVACUATE,									///< dump all our contents
	GUI_COMMAND_EXECUTE_RAILED_TRANSPORT,	///< execute railed transport sequence
	GUI_COMMAND_BEACON_DELETE,						///< delete a beacon
	GUI_COMMAND_SET_RALLY_POINT,					///< set rally point for a structure
	GUI_COMMAND_SELL,											///< sell a structure
	GUI_COMMAND_FIRE_WEAPON,							///< fire a weapon
	GUI_COMMAND_SPECIAL_POWER,						///< do a special power
	GUI_COMMAND_PURCHASE_SCIENCE,					///< purchase science
	GUI_COMMAND_HACK_INTERNET,						///< gain income from the ether (by hacking the internet)
	GUI_COMMAND_TOGGLE_OVERCHARGE,				///< Overcharge command for power plants
#ifdef ALLOW_SURRENDER
	GUI_COMMAND_POW_RETURN_TO_PRISON,			///< POW Truck, return to prison
#endif
	GUI_COMMAND_COMBATDROP,								///< rappel contents to ground or bldg
	GUI_COMMAND_SWITCH_WEAPON,						///< switch weapon use

	//Context senstive command modes
	GUICOMMANDMODE_HIJACK_VEHICLE,
	GUICOMMANDMODE_CONVERT_TO_CARBOMB,
	GUICOMMANDMODE_SABOTAGE_BUILDING,
#ifdef ALLOW_SURRENDER
	GUICOMMANDMODE_PICK_UP_PRISONER,			///< POW Truck assigned to pick up a specific prisoner
#endif

	// context-insensitive command mode(s)
	GUICOMMANDMODE_PLACE_BEACON,

	GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT,			///< do a special power from localPlayer's command center, regardless of selection
	GUI_COMMAND_SPECIAL_POWER_CONSTRUCT,					///< do a special power using the construct building interface
	GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT, ///< do a shortcut special power using the construct building interface
	
	GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE,
	GUI_COMMAND_FORCE_ATTACK,							///< arm force-fire; the next click attacks whatever is under the pointer

	// add more commands here, don't forget to update the string command list below too ...

	GUI_COMMAND_NUM_COMMANDS							// keep this last
};

#ifdef DEFINE_GUI_COMMMAND_NAMES
static const char *TheGuiCommandNames[] = 
{
	"NONE",
	"DOZER_CONSTRUCT",
	"DOZER_CONSTRUCT_CANCEL",
	"UNIT_BUILD",
	"CANCEL_UNIT_BUILD",
	"PLAYER_UPGRADE",
	"OBJECT_UPGRADE",
	"CANCEL_UPGRADE",
	"ATTACK_MOVE",
	"GUARD",
	"GUARD_WITHOUT_PURSUIT",
	"GUARD_FLYING_UNITS_ONLY",
	"STOP",
	"WAYPOINTS",
	"EXIT_CONTAINER",
	"EVACUATE",
	"EXECUTE_RAILED_TRANSPORT",
	"BEACON_DELETE",
	"SET_RALLY_POINT",
	"SELL",
	"FIRE_WEAPON",
	"SPECIAL_POWER",
	"PURCHASE_SCIENCE",
	"HACK_INTERNET",
	"TOGGLE_OVERCHARGE",
#ifdef ALLOW_SURRENDER
	"POW_RETURN_TO_PRISON",
#endif
	"COMBATDROP",
	"SWITCH_WEAPON",
	"HIJACK_VEHICLE",
	"CONVERT_TO_CARBOMB",
	"SABOTAGE_BUILDING",
#ifdef ALLOW_SURRENDER
	"PICK_UP_PRISONER",
#endif
	"PLACE_BEACON",
	"SPECIAL_POWER_FROM_SHORTCUT",
	"SPECIAL_POWER_CONSTRUCT",					
	"SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT", 
	"SELECT_ALL_UNITS_OF_TYPE",
	"FORCE_ATTACK",

	NULL
};
#endif  // end DEFINE_GUI_COMMAND_NAMES

enum CommandButtonMappedBorderType
{
	COMMAND_BUTTON_BORDER_NONE = 0,
	COMMAND_BUTTON_BORDER_BUILD,
	COMMAND_BUTTON_BORDER_UPGRADE,
	COMMAND_BUTTON_BORDER_ACTION,
	COMMAND_BUTTON_BORDER_SYSTEM,

	COMMAND_BUTTON_BORDER_COUNT // keep this last
};

static const LookupListRec CommandButtonMappedBorderTypeNames[] = 
{
	{ "NONE",					COMMAND_BUTTON_BORDER_NONE },
	{ "BUILD",				COMMAND_BUTTON_BORDER_BUILD },
	{ "UPGRADE",			COMMAND_BUTTON_BORDER_UPGRADE },
	{ "ACTION",				COMMAND_BUTTON_BORDER_ACTION },
	{ "SYSTEM",				COMMAND_BUTTON_BORDER_SYSTEM },
	
	{ NULL, 0	}// keep this last!
};
//-------------------------------------------------------------------------------------------------
/** Command buttons are used to load the buttons we place on throughout the command bar 
	* interface in different context sensitive windows depending on the situation and
	* type of the object selected */
//-------------------------------------------------------------------------------------------------
class CommandButton : public Overridable
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( CommandButton, "CommandButton" );

public:

	CommandButton( void );
	// virtual destructor prototype provided by MemoryPoolObject

	/// INI parsing
	const FieldParse *getFieldParse() const { return s_commandButtonFieldParseTable; }
	static const FieldParse s_commandButtonFieldParseTable[];		///< the parse table
	static void parseCommand( INI* ini, void *instance, void *store, const void *userData );

	Bool isContextCommand() const;									///< determines if this is a context sensitive command.
	Bool isValidRelationshipTarget(Relationship r) const;
	Bool isValidObjectTarget(const Player* sourcePlayer, const Object* targetObj) const;
	Bool isValidObjectTarget(const Object* sourceObj, const Object* targetObj) const;
	Bool isValidObjectTarget(const Drawable* source, const Drawable* target) const;
	
	// Note: It is perfectly valid for either (or both!) of targetObj and targetLocation to be NULL.
	// This is a convenience function to make several calls to other functions.
	Bool isValidToUseOn(const Object *sourceObj, const Object *targetObj, const Coord3D *targetLocation, CommandSourceType commandSource) const;
	Bool isReady(const Object *sourceObj) const;

	const AsciiString& getName() const { return m_name; }
	const AsciiString& getCursorName() const { return m_cursorName; }
	const AsciiString& getInvalidCursorName() const { return m_invalidCursorName; }
	const AsciiString& getTextLabel() const { return m_textLabel; }
	const AsciiString& getDescriptionLabel() const { return m_descriptionLabel; }
	const AsciiString& getPurchasedLabel() const { return m_purchasedLabel; }
	const AsciiString& getConflictingLabel() const { return m_conflictingLabel; }
	const AudioEventRTS* getUnitSpecificSound() const { return &m_unitSpecificSound; }

	GUICommandType getCommandType() const { return m_command; }
	UnsignedInt getOptions() const { return m_options; }
	OVERRIDE<ThingTemplate> getThingTemplate() const { return m_thingTemplate; }
	const UpgradeTemplate* getUpgradeTemplate() const { return m_upgradeTemplate; }
	const SpecialPowerTemplate* getSpecialPowerTemplate() const { return m_specialPower; }
	RadiusCursorType getRadiusCursorType() const { return m_radiusCursor; }
	WeaponSlotType getWeaponSlot() const { return m_weaponSlot; }
	Int getMaxShotsToFire() const { return m_maxShotsToFire; }
	const ScienceVec& getScienceVec() const { return m_science; }
	CommandButtonMappedBorderType getCommandButtonMappedBorderType() const { return m_commandButtonBorder; }
	const Image* getButtonImage() const { return m_buttonImage;	}
	void cacheButtonImage();

	GameWindow* getWindow() const { return m_window;	}
	Int getFlashCount() const { return m_flashCount; }

	const CommandButton* getNext() const { return m_next; }

	void setName(const AsciiString& n) { m_name = n; }

	void setButtonImage( const Image *image ) { m_buttonImage = image; }

	// bleah. shouldn't be const, but is. sue me. (srj)
	void copyImagesFrom( const CommandButton *button, Bool markUIDirtyIfChanged ) const;
	
	// bleah. shouldn't be const, but is. sue me. (Kris) -snork!
	void copyButtonTextFrom( const CommandButton *button, Bool shortcutButton, Bool markUIDirtyIfChanged ) const;

	// bleah. shouldn't be const, but is. sue me. (srj)
	void setFlashCount(Int c) const { LOGIC_CENSUS( CENSUS_CONTROLBAR ); m_flashCount = c; }
	
	// only for ControlBar!
	void friend_addToList(CommandButton** list) {	m_next = *list;	*list = this; }
	void friend_setCommandType(GUICommandType c) { m_command = c; }
	void friend_setBorderType(CommandButtonMappedBorderType t) { m_commandButtonBorder = t; }
	CommandButton* friend_getNext() { return m_next; }

private:
	AsciiString										m_name;												///< template name
	GUICommandType								m_command;										///< type of command this button 
	CommandButton*								m_next;
	UnsignedInt										m_options;										///< command options (see CommandOption enum)
	const ThingTemplate*					m_thingTemplate;							///< for commands that use thing templates in command data
	const UpgradeTemplate*				m_upgradeTemplate;						///< for commands that use upgrade templates in command data
	const SpecialPowerTemplate*		m_specialPower;								///< actual special power template
	RadiusCursorType							m_radiusCursor;								///< radius cursor, if any
	AsciiString										m_cursorName;									///< cursor name for placement (NEED_TARGET_POS) or valid version (CONTEXTMODE_COMMAND)
	AsciiString										m_invalidCursorName;					///< cursor name for invalid version

	// bleah. shouldn't be mutable, but is. sue me. (Kris) -snork!
	mutable AsciiString										m_textLabel;									///< string manager text label
	mutable AsciiString										m_descriptionLabel;						///< The description of the current command, read in from the ini
	
	AsciiString										m_purchasedLabel;							///< Description for the current command if it has already been purchased.
	AsciiString										m_conflictingLabel;						///< Description for the current command if it can't be selected due to multually-exclusive choice.
	WeaponSlotType								m_weaponSlot;									///< for commands that refer to a weapon slot
	Int														m_maxShotsToFire;							///< for commands that fire weapons
	ScienceVec										m_science;										///< actual science
	CommandButtonMappedBorderType	m_commandButtonBorder;
	AsciiString										m_buttonImageName;
	GameWindow*										m_window;											///< used during the run-time assignment of a button to a gadget button window
	AudioEventRTS									m_unitSpecificSound;					///< Unit sound played whenever button is clicked.

	// bleah. shouldn't be mutable, but is. sue me. (srj)
	mutable const Image*					m_buttonImage;								///< button image
	// bleah. shouldn't be mutable, but is. sue me. (srj)
	mutable Int										m_flashCount;                 ///< the number of times a cameo is supposed to flash

};

//-------------------------------------------------------------------------------------------------
/** Command sets are collections of configurable command buttons.  They are used in the
	* command context sensitive window in the battle user interface */
//-------------------------------------------------------------------------------------------------
//
// How many units one click on a build button queues, and how many a right-click takes back out.
// Shift is five, control is twenty, the two together mean "as many as will go" - the queue is nine
// deep by default and the batch loop stops on its own at a full queue or an empty bank, so the
// third rung fills every selected factory and spends whatever is left.
//
enum { SHIFT_BUILD_QUEUE_COUNT = 5 };
enum { CTRL_BUILD_QUEUE_COUNT = 20 };
enum { CTRL_SHIFT_BUILD_QUEUE_COUNT = 100 };

// the count the modifiers currently held ask for; 1 with nothing held
Int getBuildBatchCount( void );

// how deep a producer's queue may go from the bar: its own MaxQueueEntries, and no deeper than
// the nine queue buttons that show it
UnsignedInt getQueueCap( const ProductionUpdateInterface *pu );

enum { MAX_COMMANDS_PER_SET = 18 };  // user interface max is 14 (but internally it's 18 for script only buttons!)

/** The command grid, six places across and three down, read along the rows.  A place is its key:
	* COMMAND_SLOTnn is bound to place nn - 1, Q W E R T Y over A S D F G H over Z X C V B N as shipped.
	* See ControlBar_commandPlaces. */
enum CommandPlace
{
	COMMAND_PLACE_Q = 0,
	COMMAND_PLACE_W,
	COMMAND_PLACE_E,
	COMMAND_PLACE_R,
	COMMAND_PLACE_T,
	COMMAND_PLACE_Y,
	COMMAND_PLACE_A,
	COMMAND_PLACE_S,
	COMMAND_PLACE_D,
	COMMAND_PLACE_F,
	COMMAND_PLACE_G,
	COMMAND_PLACE_H,
	COMMAND_PLACE_Z,
	COMMAND_PLACE_X,
	COMMAND_PLACE_C,
	COMMAND_PLACE_V,
	COMMAND_PLACE_B,
	COMMAND_PLACE_N,
	COMMAND_PLACE_COUNT,
	COMMAND_PLACE_COLUMNS = 6,

	// the owner's fixed places, 2026-09-28: a unit's orders under the left hand, a building's on the right
	COMMAND_PLACE_ATTACK = COMMAND_PLACE_A,
	COMMAND_PLACE_STOP = COMMAND_PLACE_S,
	COMMAND_PLACE_ATTACK_MOVE = COMMAND_PLACE_D,
	COMMAND_PLACE_EJECT = COMMAND_PLACE_Z,		///< a transport's passengers out, or a building's garrison
	COMMAND_PLACE_GUARD = COMMAND_PLACE_X,
	COMMAND_PLACE_HOLD = COMMAND_PLACE_C,
	COMMAND_PLACE_MOVE = COMMAND_PLACE_V,
	COMMAND_PLACE_RALLY = COMMAND_PLACE_B,
	COMMAND_PLACE_SELL = COMMAND_PLACE_N,
	COMMAND_PLACE_CLEAR_MINES = COMMAND_PLACE_N,	///< a dozer's or worker's
	COMMAND_PLACE_FAKE_STRUCTURES = COMMAND_PLACE_H,	///< the GLA worker's switch to and from the fakes, the end of the defense row
	COMMAND_PLACE_EXPLOSIVE = COMMAND_PLACE_B			///< the Demolition worker's suicide charge
};

/** The place a command button always stands at by its name, -1 for none: the worker's three, which
	* share a command type with things that flow.  See ControlBar_commandPlaces. */
Int ControlBar_namedCommandPlace( const char *buttonName );

/** What a command button is for, which picks its row on the grid.  See ControlBar_commandPlaces. */
enum CommandGroup
{
	COMMAND_GROUP_PRODUCTION = 0,	///< a unit to train, or a structure that makes units (a factory, a command center, a fake)
	COMMAND_GROUP_DEFENSE,				///< a base defense to build
	COMMAND_GROUP_UTILITY,				///< any other structure to build: power, supply, tech, superweapons
	COMMAND_GROUP_ABILITY,				///< upgrades, special powers, weapon switches and the rest
	COMMAND_GROUP_PASSENGER				///< one passenger's way out of a transport or bunker
};

/** The group of a command button; GUI_COMMAND_DOZER_CONSTRUCT reads the structure's KindOf. */
Int ControlBar_commandGroup( const class CommandButton *command );

/** Where each of `count` command slots stands: `types` is what each slot holds, GUI_COMMAND_NONE for
	* an empty one, `groups` its CommandGroup, `pinned` a place a slot's button always takes by name or
	* -1, and `places` gets a CommandPlace for each or -1.  Pinned buttons, stop, attack move, guard,
	* evacuate, rally point and sell go to their own places.  A set with an attack move has attack, hold
	* position and move too, keys no command set has a button for, so their places are kept for them;
	* the return is TRUE then.  Passengers gather in the bottom right corner.  The rest go by rows, the
	* owner's of 2026-09-30: production along Q, defenses along A, other structures along Z, and
	* abilities along Q, or along A under a set's production.  A row fills left to right in slot order;
	* what does not fit goes on in the next row down with room, then in the rows above. */
Bool ControlBar_commandPlaces( const Int *types, const Int *groups, const Int *pinned, Int count, Int *places );

/** The page's keys past attack, hold position and move, which no command set has a button for either. */
enum OrderKeyExtra
{
	ORDER_KEY_STANCE = 0,		///< aggressive or defensive, for one that attack moves
	ORDER_KEY_EXTRAS
};

/** Where the ORDER_KEY_EXTRAS keys stand once `count` slots are at `places` (ControlBar_commandPlaces'
	* answer, `fights` its return), or -1 for a key that is not offered or finds no room.  They come
	* after every button, so none of them moves one.  Each has its own place, the stance H; one that
	* finds its own taken goes on B, then N, then the first free place in reading order. */
void ControlBar_orderKeyPlaces( const Int *places, Int count, Bool fights, Int *keys );
enum { MAX_RIGHT_HUD_UPGRADE_CAMEOS = 5};
enum { MAX_MULTI_SELECT_GROUPS = 36 };	///< unit types a multi-selection tells apart (6x6 grid)
enum { 
			 MAX_PURCHASE_SCIENCE_RANK_1 = 4,
			 MAX_PURCHASE_SCIENCE_RANK_3 = 15,
			 MAX_PURCHASE_SCIENCE_RANK_8 = 4,
			};
//
// GeneralsExpPoints.wnd lays the promotion screen out in five columns: the 1 point row across the
// top (x 247, 315, 383, 451), the 3 point block three deep under it (the same four x plus 520) and
// the 5 point row along the bottom.  So Rank3Number0..2 sit in the first column, 3..5 in the
// second, and a column is one science chain read top to bottom.
//
enum { PURCHASE_SCIENCE_COLUMNS = 5 };
enum { PURCHASE_SCIENCE_RANK_3_PER_COLUMN = 3 };
enum { PURCHASE_SCIENCE_COLUMN_DEPTH = 1 + PURCHASE_SCIENCE_RANK_3_PER_COLUMN + 1 };
enum { MAX_STRUCTURE_INVENTORY_BUTTONS = 10 }; // there are this many physical buttons in "inventory" windows for structures
enum { MAX_BUILD_QUEUE_BUTTONS = 9 };// physical button count for the build queue
enum { MAX_SPECIAL_POWER_SHORTCUTS = 11};
enum { SPECIAL_POWER_SHORTCUT_COLS = 3 };	///< general's powers side by side in one row; the rest wrap upward
class CommandSet : public Overridable
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( CommandSet, "CommandSet" )

public:

	CommandSet( const AsciiString& name );
	// virtual destructor prototype provided by MemoryPoolObject

	const AsciiString& getName() const { return m_name; }
	const CommandButton* getCommandButton(Int i) const;

	// only for the control bar.
	CommandSet* friend_getNext() { return m_next; }
	const FieldParse* friend_getFieldParse() const { return m_commandSetFieldParseTable; }
	void friend_addToList(CommandSet** listHead);
	void friend_clearCommands();

private:

	static const FieldParse m_commandSetFieldParseTable[];		///< the parse table
	static void parseCommandButton( INI* ini, void *instance, void *store, const void *userData );

	AsciiString m_name;															  ///< name of this command set
	const CommandButton *m_command[ MAX_COMMANDS_PER_SET ]; ///< the set of command buttons that make this set

	CommandSet *m_next;

};

//-------------------------------------------------------------------------------------------------
/** The Side selece window data is used to animate on the proper generals select */
//-------------------------------------------------------------------------------------------------
class SideSelectWindowData
{
public:
	SideSelectWindowData(void)
	{
		//Added By Sadullah Nader
		//Initializations
		generalSpeak = NULL;
		m_currColor = 0;
		m_gereralsNameWin = NULL;
		m_lastTime = 0;
		m_pTemplate = NULL;
		m_sideNameWin = NULL;
		m_startTime = 0;
		m_state = 0;
		m_upgradeImage1 = NULL;
		m_upgradeImage1Win = NULL;
		m_upgradeImage2 = NULL;
		m_upgradeImage2Win = NULL;
		m_upgradeImage3 = NULL;
		m_upgradeImage3Win = NULL;
		m_upgradeImage4 = NULL;
		m_upgradeImage4Win = NULL;
		m_upgradeImageSize.x = m_upgradeImageSize.y = 0;

		m_upgradeLabel1Win = NULL;
		m_upgradeLabel2Win = NULL;
		m_upgradeLabel3Win = NULL;
		m_upgradeLabel4Win = NULL;
		sideWindow = NULL;
		//
	}
	~SideSelectWindowData(void);
	
	void init( ScienceType science, GameWindow *control );
	void reset( void );
	void update( void );
	void draw( void );

	GameWindow *sideWindow;
	GameWindow *m_animWindowWin;
	AudioEventRTS *generalSpeak;
private:
	enum
	{
		STATE_NONE = 0,
		STATE_1,
		STATE_2,
		STATE_3,
		STATE_4,
		STATE_5,
		STATE_6
	};

	const PlayerTemplate *m_pTemplate;

	GameWindow *m_gereralsNameWin;
	GameWindow *m_sideNameWin;

	
	GameWindow *m_upgradeLabel1Win;
	GameWindow *m_upgradeLabel2Win;
	GameWindow *m_upgradeLabel3Win;
	GameWindow *m_upgradeLabel4Win;

	GameWindow *m_upgradeImage1Win;
	GameWindow *m_upgradeImage2Win;
	GameWindow *m_upgradeImage3Win;
	GameWindow *m_upgradeImage4Win;
	
	Image *m_upgradeImage1;
	Image *m_upgradeImage2;
	Image *m_upgradeImage3;
	Image *m_upgradeImage4;

	IRegion2D m_leftLineFromButton;
	IRegion2D m_rightLineFromButton;
	
	IRegion2D m_upgradeLine1a;
	IRegion2D m_upgradeLine2a;
	IRegion2D m_upgradeLine3a;
	IRegion2D m_upgradeLine4a;

	IRegion2D m_upgradeLine1;
	IRegion2D m_upgradeLine2;
	IRegion2D m_upgradeLine3;
	IRegion2D m_upgradeLine4;
	
	IRegion2D m_upgradeLine1MidReg;
	IRegion2D m_upgradeLine2MidReg;
	IRegion2D m_upgradeLine3MidReg;
	IRegion2D m_upgradeLine4MidReg;

	IRegion2D m_upgrade1Clip;
	IRegion2D m_upgrade2Clip;
	IRegion2D m_upgrade3Clip;
	IRegion2D m_upgrade4Clip;

	Color m_currColor;
	ICoord2D m_line1End;
	ICoord2D m_line2End;

	
	ICoord2D m_upgradeLine1Mid;
	ICoord2D m_upgradeLine2Mid;
	ICoord2D m_upgradeLine3Mid;
	ICoord2D m_upgradeLine4Mid;
	
	ICoord2D m_upgradeLine1End;
	ICoord2D m_upgradeLine2End;
	ICoord2D m_upgradeLine3End;
	ICoord2D m_upgradeLine4End;

	ICoord2D m_upgradeImagePos1;
	ICoord2D m_upgradeImagePos2;
	ICoord2D m_upgradeImagePos3;
	ICoord2D m_upgradeImagePos4;

	ICoord2D m_upgradeImageSize;

	Int m_state;
	UnsignedInt m_lastTime;
	UnsignedInt m_startTime;
};

//-------------------------------------------------------------------------------------------------
/** A command bar context is a window or set of windows that make up a context sensitive
	* display of commands and information to the user based on what objects are selected
	* and their capabilities */
//-------------------------------------------------------------------------------------------------
enum ControlBarContext
{
	CB_CONTEXT_NONE,									///< default view for center bar and portrait window
//	CB_CONTEXT_PURCHASE_SCIENCE,
	CB_CONTEXT_COMMAND,								///< set of commands (attack-move,stop,deploy etc)
	CB_CONTEXT_STRUCTURE_INVENTORY,		///< garrisonable building inventory interface
	CB_CONTEXT_BEACON,								///< beacon interface
	CB_CONTEXT_UNDER_CONSTRUCTION,		///< building under construction
	CB_CONTEXT_MULTI_SELECT,					///< for when we have multiple objects selected
	CB_CONTEXT_OBSERVER_INFO,					///< for when we want to populate the player info
	CB_CONTEXT_OBSERVER_LIST,					///< for when we want to update the observer list
	CB_CONTEXT_OCL_TIMER,							///< Countdown for OCL spewers

	NUM_CB_CONTEXTS
};

//-------------------------------------------------------------------------------------------------
/** Context parents, are parent windows in the control bar interface that are the key
	* parts we care about to make the whole interface a context sensitive one.  We will
	* hide and un-hide these windows and their interface controls in order to make
	* the control bar context sensitive to the object that is selected */
//-------------------------------------------------------------------------------------------------
enum ContextParent
{
	CP_MASTER,									///< *The* control bar window as a whole
	CP_PURCHASE_SCIENCE,
	CP_COMMAND,									///< configurable command buttons parent
	CP_BUILD_QUEUE,							///< build queue parent
	CP_BEACON,									///< beacon parent
	CP_UNDER_CONSTRUCTION,			///< building under construction parent
	CP_OBSERVER_INFO,						///< Observer Info window parent
	CP_OBSERVER_LIST,						///< Observer player list parent
	CP_OCL_TIMER,								///< Countdown for OCL spewers

	NUM_CONTEXT_PARENTS
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
enum CBCommandStatus
{
	CBC_COMMAND_NOT_USED = 0,		///< gui control message was *not* used
	CBC_COMMAND_USED						///< gui control message was used
};

// ------------------------------------------------------------------------------------------------
/** Command availability is used during the context update so that we can set the
	* GUI button to be enabled/disabled/checked/unchecked to represent the current
	* state of that command availability */
// ------------------------------------------------------------------------------------------------
enum CommandAvailability
{
	COMMAND_RESTRICTED,
	COMMAND_AVAILABLE,
	COMMAND_ACTIVE,
  COMMAND_HIDDEN,
	COMMAND_NOT_READY,
	COMMAND_CANT_AFFORD,
};

enum ControlBarStages
{
	CONTROL_BAR_STAGE_DEFAULT = 0,		///< full view for the world to see
	CONTROL_BAR_STAGE_SQUISHED,				///< squished just for expeirenced players
	CONTROL_BAR_STAGE_LOW,						///< control bar a la minimalist
	CONTROL_BAR_STAGE_HIDDEN,					///< yo, where be da control bar at?
	
	MAX_CONTROL_BAR_STAGES
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class ControlBar : public SubsystemInterface
{

public:

	ControlBar( void );
	virtual ~ControlBar( void );

	virtual void init( void );					///< from subsystem interface
	virtual void reset( void );					///< from subsystem interface
	virtual void update( void );				///< from subsystem interface

	//
	// The window half of init, on its own, because a resolution change throws every window away and
	// builds it again while the bar's command buttons and command sets have to stay where they are:
	// an object holding a const CommandButton * would be left pointing at a freed one.  initWindows
	// drops what it is holding (shutdownWindows) and looks the new windows up.
	//
	void initWindows( void );
	void shutdownWindows( void );

	/// mark the UI as dirty so the context of everything is re-evaluated; inside a tick it waits for the sync point
	void markUIDirty( void );

	/** LOGIC-THREAD.md phase 2, M2: a call the tick makes on the bar.  Inside GameLogic::update it is
		kept and run by flushLogicCalls on the main thread right after the tick, at both -logicthread
		settings; outside a tick it runs at once.  The map start calls in startNewGame stay direct. */
	static void callFromLogic( const std::function<void()>& call );
	static void flushLogicCalls( void );		///< LogicThread::runTick, after every tick
	Bool selectionRepeatsUnit( const ThingTemplate *thing );	///< a selected factory of ours builds it on repeat
	void toggleRepeatUnit( const ThingTemplate *thing );		///< Ctrl+Alt on a build button: repeat on or off

	/// a drawable has just become selected
	void onDrawableSelected( Drawable *draw );

	/// a drawable has just become de-selected
	void onDrawableDeselected( Drawable *draw );

	void onPlayerRankChanged(const Player *p);
	void onPlayerSciencePurchasePointsChanged(const Player *p);

	/** if this button is part of the context sensitive command system, process a button click
	the gadgetMessage is either a GBM_SELECTED or GBM_SELECTED_RIGHT */
	CBCommandStatus processContextSensitiveButtonClick( GameWindow *button, 
																											GadgetGameMessage gadgetMessage );

	/** if this button is part of the context sensitive command system, process the Transition
	gadgetMessage is either a GBM_MOUSE_LEAVING or GBM_MOUSE_ENTERING */
	CBCommandStatus processContextSensitiveButtonTransition( GameWindow *button, 
																											GadgetGameMessage gadgetMessage );
	

	/** press the command at grid place `place` (a CommandPlace), exactly as a mouse click would.
		This is what the COMMAND_SLOTnn grid keys are wired to.  Where the place holds one of the
		orders every unit shares, or nothing, the key sends that order's own message, the one its
		key sent before the grid took it: A force fire, S stop, D attack move, X guard, C hold.
		A building going up and a building counting down to a unit answer from their own button
		when the command group is hidden. */
	void pressCommandButton( Int place );

	/** The place each command window stands at right now, -1 for a hidden one, and whether the
		attack and hold places hold the page's two orders.  `places` has MAX_COMMANDS_PER_SET. */
	Bool getCommandPlaces( Int *places ) const;

	/** Where the page's stance key stands for what is selected now,
		ORDER_KEY_EXTRAS of them, -1 for one that is not offered.  See ControlBar_orderKeyPlaces. */
	void getOrderKeyPlaces( Int *keys ) const;

	/** The general's powers are laid out SPECIAL_POWER_SHORTCUT_COLS to a group, so one key press
		cannot reach eleven of them.  The first press picks a group, which stands on the HUD as a
		column (F1 is the column against the console, F2 the next) and the second picks a power
		inside it (F1 is the bottom one),
		which puts every power two keystrokes away: F1-F1, F2-F1, F2-F3.  This is what the
		SHORTCUT_SLOTnn keys are wired to. */
	void pressSpecialPowerShortcut( Int index );

	/** forget a half-finished row-then-power keystroke, so the next key picks a row again.
		TRUE when there was one to forget. */
	Bool clearSpecialPowerShortcutRow( void );

	/** What a press would do this instant, for whoever shows the bar somewhere other than the
		screen - the hardware lighting is the one that does.  These run the same resolution the two
		press calls above run and stop short of pressing: a second copy of that logic kept elsewhere
		is a copy that goes stale, and this file has been bitten by one already.  button comes back
		as the window the press would reach.  For a tray key that only picks a row it is the first
		window in that row, or NULL when the row holds nothing to press. */
	enum PressOutcome
	{
		PRESS_DOES_NOTHING,		///< no button behind this key right now
		PRESS_ARMS_CHORD,			///< first half of the powers tray's two key press
		PRESS_IS_REFUSED,			///< a button is there and it is disabled
		PRESS_FIRES						///< the button would take it
	};
	PressOutcome peekCommandButtonPress( Int index, GameWindow **button );
	PressOutcome peekSpecialPowerShortcutPress( Int index, GameWindow **button );

	/** Where a tray key lands: a slot index, or one of these two.  resolveTrayPress is the whole
		decision and touches no window, so the tests can hold every row of it; the press and the peek
		calls gather what is on the tray and ask it. */
	enum { SLOT_NOTHING = -1, SLOT_ARMS_CHORD = -2 };
	static Int resolveTrayPress( Int index, Int armedRow, Int visibleSlots );

	/** The promotion screen answers the group keys while it is open: 1 to 5 name its five columns.
		A promotion point is spent for good, so it takes two presses like the general's powers do -
		the first marks the next science the column will sell you, the same key again buys it.  A
		different number marks that column instead, and CHORD_TIMEOUT_MS of nothing drops the mark. */
	void pressPurchaseScienceColumn( Int column );

	/** forget a marked column.  TRUE when there was one to forget. */
	Bool clearPurchaseScienceColumn( void );

	/** with multiple units selected the right HUD shows the unit types; a left click at
		screen (x, y) on one of those type tiles keeps only that type selected, and
		with shift drops that type from the selection; FALSE when (x, y) is on no tile */
	Bool clickMultiSelectTile( Int x, Int y );

	/** how long a half-finished two key press waits for its second key, the powers tray's row and
		the promotion screen's marked column */
	enum { CHORD_TIMEOUT_MS = 4000 };			///< real time, not frames: the client frame rate is uncapped

	/// is the drawable the currently selected drawable for the context sensitive UI?
	Bool isDrivingContextUI( Drawable *draw ) const { return draw == m_currentSelectedDrawable; }
	/// the drawable whose portrait and commands the bar is showing, NULL for none
	Drawable *getContextDrawable( void ) const { return m_currentSelectedDrawable; }

	//-----------------------------------------------------------------------------------------------
	// the remaining methods are used to construct the command buttons and command sets for
	// the command bar
	//-----------------------------------------------------------------------------------------------

	/// TheCommandStore's lookups, for the client; logic asks the store itself
	const CommandButton *findCommandButton( const AsciiString& name );
	const CommandSet *findCommandSet( const AsciiString& name );

	/// whose promotion screen and skills are shown: your own while playing, the selected or
	/// observed player's while watching
	Player *getWatchedPlayer( void );

	/// watching, the player whose thing is selected; NULL for nothing selected, which is the
	/// whole-match view
	Player *getSelectedPlayer( void );

	/// watching, follow the selection: the player's readouts, his money and his side's metal
	void updateWatchedPlayer( void );
	/// watching, look at this player - what pressing his button in the player list did - or at
	/// everybody for NULL, the list's cancel; the portrait's panel follows whatever is selected
	void watchPlayer( Player *player );
	void showObserverPlayerInfo( void );		///< his readouts up, the player list down
	void showObserverPlayerList( void );		///< and back again

	void showPurchaseScience( void );
	void hidePurchaseScience( void );
	void togglePurchaseScience( void );
	Bool isPurchaseScienceVisible( void );

	void showSpecialPowerShortcut( void );
	void hideSpecialPowerShortcut( void );
	void animateSpecialPowerShortcut( Bool isOn );
	

	/// set the control bar to the proper scheme based off a player template that's passed in
	ControlBarSchemeManager *getControlBarSchemeManager( void ) { return m_controlBarSchemeManager; }
	void setControlBarSchemeByPlayer(Player *p);
	void setControlBarSchemeByName(const AsciiString& name);
	void setControlBarSchemeByPlayerTemplate(const PlayerTemplate *pt);

	/// We need to sometime change what the images look like depending on what scheme we're using
	void updateBuildQueueDisabledImages( const Image *image );

	/// We need to sometime change what the images look like depending on what scheme we're using
	void updateRightHUDImage( const Image *image );
	
	/// We need to be able to update the command marker image based on which scheme we're using.
	void updateCommandMarkerImage( const Image *image );
	void updateSlotExitImage( const Image *image);

	void updateUpDownImages( const Image *toggleButtonUpIn, const Image *toggleButtonUpOn, const Image *toggleButtonUpPushed, const Image *toggleButtonDownIn, const Image *toggleButtonDownOn, const Image *toggleButtonDownPushed,const Image *generalButtonEnable, const Image *generalButtonHighlight  );

	void preloadAssets( TimeOfDay timeOfDay );		///< preload the assets

	/// We want to be able to have the control bar scheme set the color of the build up clock
	void updateBuildUpClockColor( Color color);

	WindowVideoManager *m_videoManager;						///< Video manager to take care of all animations on screen.
	AnimateWindowManager *m_animateWindowManager; ///< The animate window manager
	AnimateWindowManager *m_animateWindowManagerForGenShortcuts; ///< The animate window manager
	void updatePurchaseScience( void );
	AnimateWindowManager *m_generalsScreenAnimate; ///< The animate window manager

	// Initialize the Observer controls Must be called after we've already loaded the window
	void initObserverControls( void );
	void setObserverLookAtPlayer (Player *p) { m_observerLookAtPlayer = p;}
	Player *getObserverLookAtPlayer (void ) { LOGIC_CENSUS( CENSUS_OBSERVER_LOOKAT ); return m_observerLookAtPlayer;}
	void populateObserverInfoWindow ( void );
	void populateObserverList( void );
	Bool isObserverControlBarOn( void ) { return m_isObserverCommandBar;}
	

	// Functions for repositioning/resizing the control bar
	void switchControlBarStage( ControlBarStages stage );
	void restoreStageAfterScheme( void );		///< a new scheme keeps a minimised bar minimised
	void toggleControlBarStage( void );

	const Image *getStarImage( void );

	Color getBorderColor( void ){return m_commandBarBorderColor;}
	/// the button border colours the bar itself uses, so anything drawn outside it can match
	Color getBuildBorderColor( void ){return m_commandButtonBorderBuildColor;}
	Color getUpgradeBorderColor( void ){return m_commandButtonBorderUpgradeColor;}
	Color getActionBorderColor( void ){return m_commandButtonBorderActionColor;}
	Color getSystemBorderColor( void ){return m_commandButtonBorderSystemColor;}
	void updateBorderColor( Color color) {m_commandBarBorderColor = color;	}

	/// set the command data into the button
	void setControlCommand( GameWindow *button, const CommandButton *commandButton );

	void getForegroundMarkerPos(Int *x, Int *y);
	void getBackgroundMarkerPos(Int *x, Int *y);

	void drawTransitionHandler( void );
	const Image *getArrowImage( void ){ return m_genArrow;	}
	void setArrowImage( const Image *arrowImage ){ m_genArrow = arrowImage;	}
	
	void initSpecialPowershortcutBar( Player *player);
	///< put the general's powers bar on screen now, filled in, with no slide-in (control changed hands)
	void showSpecialPowerShortcutInstantly( Player *player );

	void drawSpecialPowerShortcutMultiplierText();

	Bool hasAnyShortcutSelection() const;

	//
	// The bar is three independently anchored panels rather than one strip stretched across the
	// bottom edge.  layoutPanels() re-places every window in ControlBar.wnd at a single uniform
	// scale inside one of them; the plate art is drawn around the content rectangles it records.
	//
	enum ControlBarPanel
	{
		CB_PANEL_LEFT = 0,		///< radar, hard against the left edge
		CB_PANEL_CENTER,			///< money, power, the toolbar column and the command grid, centred
		CB_PANEL_RIGHT,				///< selection portrait and the general's tabs, hard against the right edge

		CB_PANEL_COUNT
	};

	/// the whole panel, art included - this is what blocks a click from reaching the world
	const IRegion2D *getPanelRect( Int which ) const { return &m_panelRect[ which ]; }

	/** Where ControlBarParent stood when layoutPanels last ran.  Everything the panels hold is a
		* child of that window and travels with it - the minimise stage drops it a tenth of the
		* screen, the intro slides it up from below - but the plate art is drawn from the design
		* rectangles rather than from a window, so it has to be told by how much.  Draw offset is
		* the frame's screen position now, minus this. */
	const ICoord2D *getPanelOrigin( void ) const { return &m_panelOrigin; }

	/** Re-anchor the three panels at one uniform scale.  Idempotent, and safe to call again after
		* something else has moved a window - ControlBarScheme::init does, every time a scheme is
		* set - because it remembers both the authored rectangle and the one it last handed out. */
	void layoutPanels( void );

	/// Whether command set slot `slot` has a window on this bar, for what lays a set out without an object.
	Bool hasCommandWindow( Int slot ) const { return m_commandWindows[ slot ] != NULL; }

	/// top edge of the highest visible window the bar owns, and where MoneyDisplay is; see -uidrill
	void forEachPlacedWindow( Int *topOut, Int *moneyOut );
	ControlBarContext getCurrentContext( void ) const { return m_currContext; }	///< for -uidrill's log

	/// the window every other one on the bar hangs off, so -uidrill can walk it looking for a button
	GameWindow *getMasterParent( void ) { return m_contextParent[ CP_MASTER ]; }

	/** TRUE when a click at this screen point, over this window, belongs to the world after all: the
		* window is one of the bar's input-blocking panes or CenterBackground, no window inside it is
		* under the point, and every plate painted there is transparent at that texel.  The panes are
		* rectangles and the plates are not, so without this a click on the sky above a sloped edge
		* went nowhere.  GameWindow::winPointInChild asks, so clicking and unit picking agree. */
	Bool letsClickThrough( GameWindow *window, Int x, Int y );

	/// the radar's under-attack lamp, EA's
	void triggerRadarAttackGlow( void );

	/** Puts one of the bar's windows on `rect`, screen pixels, and leaves its children standing where
		* they were on screen.  The place layoutPanels recorded for it moves with it, so a rebuild of the
		* layout reads the window as put there and not as moved in the loader's stretched space. */
	void placeWindowAt( GameWindow *window, const IRegion2D &rect );

	/// the portrait bar's upgrade cameos and type tiles, which have no command button: TRUE and their
	/// tooltip's name and description for one of them, FALSE for any other window
	Bool describePortraitBarWindow( GameWindow *window, UnicodeString &name, UnicodeString &description ) const;

	/// post process step, after all commands and command sets are loaded - and again after a map.ini
	void postProcessCommands( void );

protected:
	/// place one window and its descendants inside 'panel'; see layoutPanels
	void placeInPanel( GameWindow *win, Int panel,
										 Int oldParentX, Int oldParentY, Int newParentX, Int newParentY,
										 Int shiftX = 0 );

	void setDefaultControlBarConfig( void );
	void setSquishedControlBarConfig( void );
	void setLowControlBarConfig( void );
	void setHiddenControlBar( void );

	/// evaluate what the user should see based on what selected drawables we have in our UI
	void evaluateContextUI( void );

	/// add the common commands of this drawable to the common command set

	/// switch the interface context to the new mode and populate as needed
	void switchToContext( ControlBarContext context, Drawable *draw );

	/// set the command data into the button
	void setControlCommand( const AsciiString& buttonWindowName, GameWindow *parent,
											 const CommandButton *commandButton );

	/// show/hide the portrait window image using the image pointer to set
	void setPortraitByImage( const Image *image );

	/// show/hide the portrait window image using the image from the object
	void setPortraitByObject( Object *obj );

	/// show rally point at world location, a NULL location will hide any visible rally point marker
	void showRallyPoint( const Coord3D *loc );

	// the following methods are for resetting data for vaious contexts
	void resetContainData( void );			/// reset container data we use to tie controls to objects IDs for containment
	void resetBuildQueueData( void );			/// reset the build queue data we use to die queue entires to control
	void resetBuildQueueButtons( void );	/// attach the queue button windows and wipe them to empty
	void populateBuildQueue( Object *producer );	///< the producer's queue into the queue buttons
	void updateBuildQueue( Object *obj, ProductionUpdateInterface *pu );	///< the queue over the portrait while it holds anything

	// the following methods are for populating the context GUI controls for a particular context
	static void populateButtonProc( Object *obj, void *userData );
	void populatePurchaseScience(Player* player);
	/// one rank's section of the promotion screen: slot i of the command set goes on windows[i]
	void populatePurchaseScienceRank( Player *player, const CommandSet *commandSet, GameWindow **windows, Int windowCount );
	void populateCommand( Object *obj );
	void populateMultiSelect( void );
	void populateMultiSelectUnitList( void );
	void setMultiSelectFocus( Int index );
	void updateMultiSelectStrip( void );
	void layoutMultiSelectTiles( Int count );		///< make sure count cells exist, laid out n x n over the right HUD
	Bool cancelLastQueuedUnit( const ThingTemplate *thing );	///< returns FALSE when there was nothing left to cancel	///< right-click on a build button
	Bool cancelQueuedUpgrade( const UpgradeTemplate *upgrade );	///< right-click on an upgrade button
	void populateStructureInventory( Object *building );
	void populateBeacon( Object *beacon );
	void populateUnderConstruction( Object *objectUnderConstruction );
	void populateOCLTimer( Object *creatorObject );
	void doTransportInventoryUI( Object *transport, const CommandSet *commandSet );
	static void populateInvDataCallback( Object *obj, void *userData );

	// the following methods are for updating the currently showing context
	CommandAvailability getCommandAvailability( const CommandButton *command, Object *obj, GameWindow *win, GameWindow *applyToWin = NULL, Bool forceDisabledEvaluation = FALSE ) const;

public:
	/** How permissive an availability is.  The enum is not written in that order - COMMAND_RESTRICTED
		* is 0 and COMMAND_AVAILABLE is 1, with HIDDEN and the two "nearly" states after them - so a
		* group cannot just take the numeric max.  Inline and static so a test can reach it. */
	static Int commandAvailabilityRank( CommandAvailability a )
	{
		switch( a )
		{
			case COMMAND_ACTIVE:			return 5;
			case COMMAND_AVAILABLE:		return 4;
			case COMMAND_CANT_AFFORD:	return 3;		// the money is the player's; the button reads live
			case COMMAND_NOT_READY:		return 2;
			case COMMAND_RESTRICTED:	return 1;
			default:									return 0;		// COMMAND_HIDDEN
		}
	}

protected:
	/** The same question asked of a whole multi-selection: a command one of the selected buildings
		* can carry out is offered, not greyed out because the group's representative happens to have
		* it already - or to be the one airfield of four that is already full. Returns the best
		* availability over the whole selection (the focused type first, so the member handed back is
		* the one the player is looking at whenever it can do the job), and hands back the member that
		* earned it so the press can be sent there. Outside a multi-selection it is just
		* getCommandAvailability on the one selected object. */
	CommandAvailability getGroupCommandAvailability( const CommandButton *command, Object *obj, GameWindow *win, Object **ableObj = NULL ) const;
	void updateContextMultiSelect( void );
	void updateContextPurchaseScience( void );
	void updateContextCommand( void );
	void updateContextStructureInventory( void );
	void updateContextBeacon( void );
	void updateContextUnderConstruction( void );
	void updateContextOCLTimer( void );
	
	// the following methods are for the special power shortcut window

	void populateSpecialPowerShortcut( Player *player);
	void updateSpecialPowerShortcut( void );
	void arrangeSpecialPowerShortcutGrid( void );	///< re-lay the layout's single column as rows of SPECIAL_POWER_SHORTCUT_COLS
	Int countVisibleSpecialPowerShortcuts( void );	///< how many slots carry a power right now, which is not the command set's size
	UnsignedInt visibleSpecialPowerShortcutMask( void );	///< the slots showing a power, one bit a slot
	void rearrangeSpecialPowerShortcutGrid( void );	///< arrangeSpecialPowerShortcutGrid when the slots shown or the scheme's pieces changed
	static const UnsignedInt TRAY_NOT_LAID_OUT = 0xFFFFFFFFu;	///< no mask of MAX_SPECIAL_POWER_SHORTCUTS bits is this
	UnsignedInt m_trayLaidOutMask;								///< the slots arrangeSpecialPowerShortcutGrid stepped the tray for
	const Image *const *m_trayLaidOutFor;				///< and the scheme pieces it used, NULL for the shipped column
	Bool m_trayFramePlaced;												///< the frame stands at the stepped tray's box for this layout
	enum { POWER_TRAY_FOOT_Y = 790 };							///< at 1080, where the tray stands: the top of the general's tab
	void powerTrayMetrics( const Image *const *tray, ICoord2D *origin, ICoord2D *cell,
												 Int *left, Int *top, Int *foot, Int *bottom );	///< see arrangeSpecialPowerShortcutGrid

	void fitRadarToMap( Bool fromLayout );				///< the radar window, its frame and its blocker take the map's shape; see layoutPanels
	Real m_radarFitWidth, m_radarFitHeight;				///< the map extent fitRadarToMap last fitted to
	/// the observer's player column (its list, the watched player's panel, Options and Chat) stands
	/// against the frame's right edge, so it is fitted with the radar
	enum { RADAR_FIT_RADAR, RADAR_FIT_BLOCK, RADAR_FIT_LAMP, RADAR_FIT_LIST, RADAR_FIT_INFO, RADAR_FIT_OPTIONS,
				 RADAR_FIT_CHAT, RADAR_FIT_COUNT, RADAR_FIT_COLUMN_FIRST = RADAR_FIT_LIST };
	IRegion2D m_radarFitBox[ RADAR_FIT_COUNT ];		///< where layoutPanels put each, against the bar's frame
	Int m_radarColumnShift;												///< how far the column stands left of where the square map has it
public:
	/// drawPainting moves the left piece of the painting, the column's plate, by as much
	Int getRadarColumnShift( void ) const { return m_radarColumnShift; }
protected:
	void showIdleButtons( Bool show );						///< the idle worker, infantry, vehicle and aircraft buttons, hidden while watching

	/** Where a key lands: the command slot at grid place `place`, or SLOT_NOTHING; and the tray slot
		a shortcut key reaches with its row as it stands.  The press calls and the peek calls both go
		through these, which is the point of them. */
	Int resolveCommandSlot( Int place ) const;
	Int resolveSpecialPowerShortcutSlot( Int index );

	static const Image* calculateVeterancyOverlayForThing( const ThingTemplate *thingTemplate );
	static const Image* calculateVeterancyOverlayForObject( const Object *obj );

	// the following methods do command processing for GUI selections
	CBCommandStatus processCommandUI( GameWindow *control, GadgetGameMessage gadgetMessage );
	CBCommandStatus processCommandTransitionUI( GameWindow *control, GadgetGameMessage gadgetMessage );

	// methods to help out with each context
	void updateConstructionTextDisplay( Object *obj );
	void updateOCLTimerTextDisplay( UnsignedInt totalSeconds, Real percent );

	void setUpDownImages( void );
		// methods for flashing cameos
public:
	void setFlash( Bool b ) { LOGIC_CENSUS( CENSUS_CONTROLBAR ); m_flash = b; }

	// get method for list of commandbuttons
	const CommandButton *getCommandButtons( void ) { return TheCommandStore->getCommandButtons(); }

	Drawable *findStandInBuilder( Bool freeOnly );				///< with BuildWithoutWorker on, the local player's free builder (or, unless freeOnly, any builder) to stand in for an empty selection

protected:

	ICoord2D m_defaultControlBarPosition;				///< Stored the original position of the control bar on the screen
	ControlBarStages m_currentControlBarStage;

	IRegion2D m_panelRect[ CB_PANEL_COUNT ];				///< screen rect of each panel, filled by layoutPanels()
	ICoord2D m_panelOrigin;													///< where the frame was put, so the plates can follow it
	Bool m_UIDirty;																///< the context UI must be re-evaluated

	ControlBarSchemeManager *m_controlBarSchemeManager;		///< The Scheme singleton

	GameWindow *m_contextParent[ NUM_CONTEXT_PARENTS ];		///< "parent" window for buttons that are part of the context sensitive interface

	Drawable *m_currentSelectedDrawable;					///< currently selected drawable for the context sensitive interface
	ControlBarContext m_currContext;							///< our current displayed context

	DrawableID m_rallyPointDrawableID;						///< rally point drawable for visual rally point
	DrawableID m_standInBuilderID;								///< with nothing selected, the builder whose command bar is shown (INVALID_DRAWABLE_ID otherwise)

	Real m_displayedConstructPercent;							///< construct percent last displayed to user
	UnsignedInt m_displayedOCLTimerSeconds;				///< OCL Timer seconds remaining last displayed to user
	UnsignedInt m_displayedQueueCount;						///< queue count last displayed to user
	UnsignedInt m_lastRecordedInventoryCount;			///< last known UI state of an inventory count

	GameWindow *m_rightHUDWindow;									///< window of the right HUD display
	GameWindow *m_rightHUDCameoWindow;									///< window of the right HUD display
	GameWindow *m_rightHUDUpgradeCameos[MAX_RIGHT_HUD_UPGRADE_CAMEOS];
	const UpgradeTemplate *m_rightHUDUpgrades[MAX_RIGHT_HUD_UPGRADE_CAMEOS];	///< what each upgrade cameo shows, for its tooltip
	GameWindow *m_rightHUDUnitSelectParent;

	GameWindow *m_communicatorButton;             ///< button for the communicator
	
	WindowLayout *m_scienceLayout;								///< the Science window layout
	GameWindow *m_sciencePurchaseWindowsRank1[ MAX_PURCHASE_SCIENCE_RANK_1 ];			///< command window controls for easy access
	GameWindow *m_sciencePurchaseWindowsRank3[ MAX_PURCHASE_SCIENCE_RANK_3 ];			///< command window controls for easy access
	GameWindow *m_sciencePurchaseWindowsRank8[ MAX_PURCHASE_SCIENCE_RANK_8 ];			///< command window controls for easy access
	GameWindow *m_specialPowerShortcutButtons[ MAX_SPECIAL_POWER_SHORTCUTS ];
	GameWindow *m_specialPowerShortcutButtonParents[ MAX_SPECIAL_POWER_SHORTCUTS ];
	DisplayString *m_shortcutDisplayStrings[ MAX_SPECIAL_POWER_SHORTCUTS ];
	Int m_currentlyUsedSpecialPowersButtons; ///< Value will be <= MAX_SPECIAL_POWER_SHORTCUTS;
	Int m_specialPowerShortcutRow;					 ///< row a first key press picked, -1 when none is pending
	UnsignedInt m_specialPowerShortcutRowMs;  ///< millisecond that row was picked on, for CHORD_TIMEOUT_MS

	Int m_purchaseScienceColumn;						 ///< promotion screen column a first key press marked, -1 when none
	UnsignedInt m_purchaseScienceColumnMs;	 ///< millisecond it was marked on, for CHORD_TIMEOUT_MS

	/** the window a press on that column would buy from: the topmost science in it that is on
		screen and can be bought right now.  NULL when the column has nothing to sell. */
	GameWindow *purchaseScienceCandidate( Int column );

	/** the window sitting 'depth' rows down column 'column', NULL where the layout has no button */
	GameWindow *purchaseScienceWindow( Int column, Int depth );

	/** paint the column numbers onto the sciences those keys would buy, and mark the armed one */
	void updatePurchaseScienceHotKeys( void );


	WindowLayout *m_specialPowerLayout;
	GameWindow *m_specialPowerShortcutParent;

public:
	/** Where one slot of the stepped tray draws its plate, in its own window's pixels: the cell
		* under the power, and the thickness of each edge the tray's outline runs along it (0 where a
		* neighbour stands).  stepRight is how much of the top edge the step above takes off its outer
		* end; stepBelow how much of the left edge the step at its foot takes. */
	struct TraySlot
	{
		IRegion2D cell;
		Int edgeLeft, edgeTop, edgeBottom;
		Int stepRight, stepBelow;
	};
protected:
	TraySlot m_traySlot[ MAX_SPECIAL_POWER_SHORTCUTS ];

	GameWindow *m_commandWindows[ MAX_COMMANDS_PER_SET ];			///< command window controls for easy access
	Int m_commandUpgradeRank[ MAX_COMMANDS_PER_SET ];					///< each window's upgradeRank on the bar populateCommand last filled (HotKeyManager::playerButtonKey)


	/** A player upgrade is researched once, so it goes to exactly one of the selected buildings -
		* and the bar cannot see the queue an earlier click in this same frame just filled, because
		* the message only reaches the logic on the next one. Clicking three upgrades on four selected
		* barracks therefore stacked all three on whichever building the bar was drawn from, and the
		* other three sat idle. These remember what has been handed out this frame so the next click
		* can pick a building that is genuinely free. */
	enum { UPGRADE_SPREAD_MAX = 32 };										///< selections past this fall back to the plain queue count
	UnsignedInt m_upgradeSpreadFrame;										///< logic frame the tallies below describe; a new frame clears them
	ObjectID m_upgradeSpreadID[ UPGRADE_SPREAD_MAX ];		///< building handed an upgrade this frame
	Int m_upgradeSpreadCount[ UPGRADE_SPREAD_MAX ];			///< how many, since one building can take several
	Int m_upgradeSpreadEntries;													///< used entries in the two arrays above
	Int upgradesSpreadTo( ObjectID id );								///< upgrades already sent to this building this frame
	void noteUpgradeSpread( ObjectID id );							///< record one more upgrade sent to this building
	Object *pickUpgradeProducer( const UpgradeTemplate *upgradeT, Object *fallback );	///< freest selected building that can research it

		// removed from multiplayer branch
	//GameWindow *m_commandMarkers[ MAX_COMMANDS_PER_SET ];			///< When we don't have a command, they want to show an image	
// removed from multiplayer branch
	//void showCommandMarkers( void );													///< function that compare's what's being shown in m_commandWindows and shows the ones that are hidden.


public:
	// method for hiding communicator window CCB
	void hideCommunicator( Bool b );

protected:

	struct ContainEntry
	{
	  GameWindow *control;
		ObjectID objectID;
	};
	static ContainEntry m_containData[ MAX_COMMANDS_PER_SET ];  ///< inventory buttons integrated into the regular command set for buildings/transports

	struct QueueEntry
	{
		GameWindow *control;											///< window that the GUI control is tied to
		ProductionType type;											///< type of queue data
		union
		{
			ProductionID productionID;										///< production id for unit productions
			const UpgradeTemplate *upgradeToResearch;			///< upgrade template for upgrade productions
		};

	};
	QueueEntry m_queueData[ MAX_BUILD_QUEUE_BUTTONS ];	///< what the build queue represents

	// a multi-selection grouped by unit type: one cameo cell per type, with its count, in an
	// n x n grid over the right HUD (n = ceil(sqrt(types))); Tab walks the focus over the types
	std::vector<GameWindow *> m_multiSelectTiles;		///< grid cells, created in code on demand over the right HUD
	const ThingTemplate *m_multiSelectGroupTemplate[ MAX_MULTI_SELECT_GROUPS ];	///< one entry per selected unit type
	Int m_multiSelectGroupSize[ MAX_MULTI_SELECT_GROUPS ];	///< how many of that type are selected
	DrawableID m_multiSelectGroupFirst[ MAX_MULTI_SELECT_GROUPS ];	///< a member of the group, the context representative
	Int m_multiSelectGroupCount;																///< number of type groups
	Int m_multiSelectFocus;																			///< the group that drives the context

	//cameo flash
	Bool m_flash;                                       ///< tells update whether or not to check for flash

	Bool m_sideSelectAnimateDown;
	ICoord2D m_animateDownWin1Size;
	ICoord2D m_animateDownWin2Size;
	ICoord2D m_animateDownWin1Pos;
	ICoord2D m_animateDownWin2Pos;
	GameWindow *m_animateDownWindow;
	UnsignedInt m_animTime;

	Color m_buildUpClockColor;

	Bool m_isObserverCommandBar;												///< If this is true, the command bar behaves greatly differnt
	Player *m_observerLookAtPlayer;											///< The current player we're looking at, Null if we're not looking at anyone.
	GameWindow *m_radarAttackGlowWindow;										///< WinUAttack
	Int m_remainingRadarAttackGlowFrames;										///< logic frames left on the lamp's flashing
	Player *m_watchedSelection;													///< the player the selection last named while watching, NULL for nobody

	WindowLayout *m_buildToolTipLayout;										///< The window that will slide on/display tooltips
	Bool m_showBuildToolTipLayout;											///< every frame we test to see if we aregoing to continue showing this or not.
public:
	void showBuildTooltipLayout( GameWindow *cmdButton );
	void hideBuildTooltipLayout( void );
	void deleteBuildTooltipLayout( void );
	Bool getShowBuildTooltipLayout( void ){return m_showBuildToolTipLayout;	}
	void populateBuildTooltipLayout( const CommandButton *commandButton, GameWindow *tooltipWin = NULL );
	/// the same with the prices and figures counted for player rather than the local one
	void populateBuildTooltipLayout( const CommandButton *commandButton, GameWindow *tooltipWin, Player *player );
	void repopulateBuildTooltipLayout( void );
	/// where the popup lives, so -uidrill can read the rectangle it landed in.  The tooltip stands on
	/// the radar's frame, so it goes wherever the bar's windows went
	WindowLayout *getBuildTooltipLayout( void ) { return m_buildToolTipLayout; }
private:


	// Command Bar button border bars stuff
	Color m_commandButtonBorderBuildColor;
	Color m_commandButtonBorderActionColor;
	Color m_commandButtonBorderUpgradeColor;
	Color m_commandButtonBorderSystemColor;
	
	Color m_commandBarBorderColor;

	void setCommandBarBorder( GameWindow *button, CommandButtonMappedBorderType type);
public:
	void updateCommanBarBorderColors(Color build, Color action, Color upgrade, Color system );

private:

	const Image *m_genStarOn;
	const Image *m_genStarOff;

	const Image *m_toggleButtonUpIn;
	const Image *m_toggleButtonUpOn;
	const Image *m_toggleButtonUpPushed;
	const Image *m_toggleButtonDownIn;
	const Image *m_toggleButtonDownOn;
	const Image *m_toggleButtonDownPushed;

	GameWindowTransitionsHandler *m_transitionHandler;
	const Image *m_genArrow;

	static const Image *m_rankVeteranIcon;
	static const Image *m_rankEliteIcon;
	static const Image *m_rankHeroicIcon;

	const Image *m_generalButtonEnable;
	const Image *m_generalButtonHighlight;


	Bool m_genStarFlash;

	/** Is the promotion screen open?  Not the same question as "is its window hidden": the fade it
		* opens with drives that window's hidden flag itself, several frames either way, so the window
		* answers no while the screen is opening and yes for a frame after it was told to close. */
	Bool m_purchaseScienceOpen;
	Int m_lastFlashedAtPointValue;
	
	ICoord2D m_controlBarForegroundMarkerPos;
	ICoord2D m_controlBarBackgroundMarkerPos;

#if defined( _INTERNAL ) || defined( _DEBUG )
	UnsignedInt m_lastFrameMarkedDirty;
	UnsignedInt m_consecutiveDirtyFrames;
#endif

}; 

// EXTERNALS //////////////////////////////////////////////////////////////////////////////////////
extern ControlBar *TheControlBar;

/// -uidrill: write one line saying where the bar's windows actually are, so a long run says whether
/// they hold still.
extern void ControlBar_logPlacement( const char *tag, Int frame );

//-------------------------------------------------------------------------------------------------
/** The plate one panel wears.
	*
	* The scheme art (ControlBarScheme) is one wide painting of the whole bar, drawn from a single
	* anchor and stretched by the display over 800x600.  The bar is three separately anchored panels
	* now, so that painting has nowhere to go and is not drawn at all: each panel wears a plate.
	*
	* The nine plates are the three shipped paintings cut into three, so each one has an exact
	* rectangle in the .wnd's own 800x600 space - the rectangle of the painting it came from.  That
	* is what a plate carries, and drawing it is that rectangle put through the same anchor and the
	* same uniform scale layoutPanels puts every window through.  Nothing is measured by eye, so the
	* money slot, the power rail and the toolbar column land on the windows the scheme puts there,
	* and at 4:3 each side's three plates slide back together into the strip they came from. */
//-------------------------------------------------------------------------------------------------
struct ControlBarPlate
{
	const char *filename;			///< the targa, which is all the Image needs
	IRegion2D design;					///< the 800x600 rectangle of the painting this piece is
	Int readoutShiftY;				///< design units the money readout moves to sit in this art's box
	Int powerShiftY;					///< design units the power bar moves to sit in this art's rail
	Int gridShiftX;						///< design units the command grid moves to centre in this art's field
	Int artW, artH;						///< texels of painting in the targa; the rest is power-of-two padding
	IRegion2D minTab;					///< the minimise tab this art paints, in design units, bezel and all;
														///< a minimised panel stops with this row standing on the bottom edge
};

/** The plate 'panel' wears while the bar is dressed in 'side'.  A general's scheme names a side
	* like "ChinaTankGeneral", so the match is on the front of the name.  NULL when nothing fits. */
extern const ControlBarPlate *ControlBarPlateForSide( const AsciiString& side, Int panel );

/** One flag per texel of a plate's painting, read out of its targa (32 bits, plain or run-length
	* encoded), row 0 at the top: 1 where the painting is solid enough to catch a click.  FALSE, and
	* nothing to rely on in 'mask', for anything that is not a 32-bit targa at least artW by artH. */
extern Bool ControlBarPlateMaskFromTarga( const UnsignedByte *data, Int length, Int artW, Int artH,
																				 std::vector<UnsignedByte> &mask );

/** Where a design rectangle belonging to 'panel' lands on a display this size.  This is the one
	* piece of arithmetic the whole layout is built out of: one uniform scale, and an anchor that
	* pins the left panel to the left edge, the right panel to the right edge and the middle one to
	* the middle.  FALSE for a panel that does not exist. */
extern Bool ControlBarPanelDesignToScreen( Int panel, const IRegion2D *design,
																					 Int displayWidth, Int displayHeight,
																					 IRegion2D *rectOut );

/** ControlBar Pro's bar in three pieces, each at the screen's height alone and pinned to its own
	* anchor: 800x600 design units to the screen for 'panel' (CB_PANEL_LEFT, _CENTER, _RIGHT). */
extern void ControlBarHudPieceTransform( Int panel, Real dispW, Real dispH, Real *sx, Real *sy, Real *originX );

/// which of those pieces a window whose middle is at this design x belongs to
extern Int ControlBarHudPieceForDesignX( Real designCenterX );

/** The one scale the whole HUD is drawn at: the display over the 800x600 everything was authored
	* at, the smaller of the two axes so nothing is distorted.  The .wnd loader multiplies x and y
	* separately, which on a 16:9 screen is a 1.33x horizontal smear; anything that wants to sit
	* beside the command bar without being smeared measures itself with this instead.  Never below
	* 1 - nothing shrinks under 800 wide. */
extern Real ControlBarUniformScale( void );
extern Real ControlBarUniformScaleFor( Int displayWidth, Int displayHeight );	///< ...for a screen you name

/** The scale the HUD's own drawing is done at: the screen's height over 600, cut to
	* CONTROL_BAR_HUD_PERCENT of itself, the owner's "too big" of 2026-09-28.  It follows the height
	* alone and is not rounded or floored, the owner's rule of 2026-10-01: the HUD covers the same part
	* of the screen's height at every resolution, and a wider screen does not make it bigger.  1.26 at
	* 1920x1080; 0.84 at 1280x720. */
enum { CONTROL_BAR_HUD_PERCENT = 70 };
extern Real ControlBarHudScale( void );
extern Real ControlBarHudScaleFor( Int displayWidth, Int displayHeight );
/** The text lists drawn over the battlefield, authored at the uniform scale's size: the HUD scale
	* over CONTROL_BAR_HUD_PERCENT, so they are what they were at 1920x1080 at every resolution. */
extern Real ControlBarHudPageScale( void );

/** Undo the .wnd loader's separate-axis stretch over a whole layout: every window under 'root' is
	* recovered to its authored 800x600 rectangle and put back at ControlBarUniformScale(), anchored
	* at the given fraction of the screen (0 = left/top edge, 1 = right/bottom, 0.5 = centred) with
	* the matching fraction of the design space as its fixed point.  This is layoutPanels for a
	* layout that is one piece rather than three: the generals' power bar hangs off the right edge
	* with (1,1) and the science screen is centred at the top with (0.5,0).  Both interfaces.
	*
	* Call it once, on a layout straight out of winCreateLayout.  It reads the authored rectangle
	* back out of where the loader put each window, so a second call would divide a scale out that
	* is no longer there.  layoutPanels is the idempotent one, and it is idempotent because the
	* scheme rewrites the command bar's windows behind it; nothing rewrites these. */
extern void ControlBarLayoutUniform( GameWindow *root, Real anchorFracX, Real anchorFracY );

//-------------------------------------------------------------------------------------------------
/** Logic frames as the whole seconds they will really take at the current logic rate, rounded up
	* and never down to zero. Defined in ControlBarCommand.cpp; every countdown and build time the
	* UI prints goes through it, so they all answer the game speed the same way. */
//-------------------------------------------------------------------------------------------------
extern Int ControlBar_secondsFromFrames( Real frames );
extern Int ControlBar_secondsFromFramesAt( Real frames, Int logicFps );	///< ...at a rate you name

//-------------------------------------------------------------------------------------------------
/** What the build card's draw (W3DCommandBarHelpPopupDraw) puts beside the .wnd's texts: the price and
	* build time at the header's right, then a row of figure cells, each an icon and its value.  Filled by
	* ControlBar::populateBuildTooltipLayout. */
//-------------------------------------------------------------------------------------------------
struct BuildTooltipFigures
{
	enum { MAX_CELLS = 7 };
	/// a card with no figures wears the short top row (<prefix>TipHead*, 38 high at 1080) and its
	/// description starts here (the generator's TIP_TEXT_HEAD)
	enum { HEAD_HEIGHT = 36 };
	UnicodeString cost;									///< empty when there is nothing to pay
	Bool costIsMoney;										///< the money icon goes before it; a science's points go without
	UnicodeString time;									///< empty for none
	Int cellCount;											///< 0: no row, and the description moves up into it
	Int cellIcon[ MAX_CELLS ];					///< ControlBarTooltipImages::ICON_*
	UnicodeString cellValue[ MAX_CELLS ];
};
extern const BuildTooltipFigures& ControlBarBuildTooltipFigures( void );

#endif  // end __CONTROLBAR_H_

