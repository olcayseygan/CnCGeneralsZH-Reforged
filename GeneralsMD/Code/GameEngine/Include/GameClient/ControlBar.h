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
#include "Common/GameType.h"
#include "Common/Overridable.h"
#include "Common/Science.h"
#include "GameClient/Color.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Drawable;
class GameWindow;
class Image;
class Object;
class ThingTemplate;
class WeaponTemplate;
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
class GameWindowTransitionsHandler;
class DisplayString;

enum ProductionID;

enum CommandSourceType;
enum ProductionType;
enum GadgetGameMessage;
enum ScienceType;
enum TimeOfDay;
enum RadiusCursorType;

//-------------------------------------------------------------------------------------------------
/** Command options */
//-------------------------------------------------------------------------------------------------
enum CommandOption
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
enum GUICommandType
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
	GUI_COMMAND_BUILD_PAGE,								///< switch the command bar between the builder's structure pages

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
	"BUILD_PAGE",

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
	void setFlashCount(Int c) const { m_flashCount = c; }
	
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

enum { MAX_COMMANDS_PER_SET = 18 };  // user interface max is 14 (but internally it's 18 for script only buttons!)
enum { MAX_RIGHT_HUD_UPGRADE_CAMEOS = 5};
enum { MAX_MULTI_SELECT_GROUPS = 36 };	///< unit types a multi-selection tells apart (6x6 grid, Tab focus)
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
/** What the build tooltip says, piece by piece, so Window/Html/Tooltip.html can lay each piece out
	* on its own: ControlBarPopupDescription.wnd gets the same pieces run together into one text. */
//-------------------------------------------------------------------------------------------------
/** One line of what an upgrade does: the figure under `label`, a string table label, goes from `from` to `to`. */
struct BuildTooltipChange
{
	const char *label;
	std::string from;
	std::string to;
};

/** An upgrade and what it does to one unit.  On a unit's card `name` is the upgrade's; on an
	* upgrade's card it is the unit's. */
struct BuildTooltipUpgrade
{
	UnicodeString name;
	Bool owned;										///< the player has it already
	std::vector< BuildTooltipChange > changes;
};

struct BuildTooltipCard
{
	UnicodeString name;
	UnicodeString description;		///< the button's own text, its lines split by \n
	UnicodeString warning;				///< why it cannot be bought right now, empty when it can
	UnicodeString requires;				///< "Requires: ..." naming what is still missing
	UnsignedInt cost;							///< 0 for nothing to pay
	Bool costsScience;						///< the cost is promotion points rather than money
	Bool hasStats;								///< DetailedBuildTooltips: the figures below are there
	Int buildSeconds;
	Int health;										///< 0 for a body that cannot be hurt
	Int damage;										///< the main weapon's, with the upgrades the player owns; 0 for no weapon
	Int range;
	Real attacksPerSecond;				///< shots a second over a whole clip and its reload
	Int damagePerSecond;
	std::vector< BuildTooltipUpgrade > upgrades;	///< a unit's upgrades, or the units an upgrade changes
	IRegion2D anchor;							///< the hovered window in screen pixels, its top the grid's top for a command button
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

	/// mark the UI as dirty so the context of everything is re-evaluated
	void markUIDirty( void );

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
	

	/** press a command bar button by its slot index (0..MAX_COMMANDS_PER_SET-1), exactly as
		a mouse click would.  This is what the COMMAND_SLOTnn grid keys are wired to. */
	void pressCommandButton( Int index );

	/** The general's powers are laid out SPECIAL_POWER_SHORTCUT_COLS to a row, so one key press
		cannot reach eleven of them.  The first press picks a row (F1 is the row in the corner,
		F2 the one above it) and the second picks a power inside that row (F1 is the rightmost),
		which puts every power two keystrokes away: F1-F1, F2-F1, F2-F3.  This is what the
		SHORTCUT_SLOTnn keys are wired to. */
	void pressSpecialPowerShortcut( Int index );

	/** forget a half-finished row-then-power keystroke, so the next key picks a row again.
		TRUE when there was one to forget. */
	Bool clearSpecialPowerShortcutRow( void );

	/** What a press would do this instant, for whoever shows the bar somewhere other than the
		screen - the hardware lighting is the one that does.  These run the same resolution the two
		press calls above run, chords and all, and stop short of pressing: a second copy of that
		logic kept elsewhere is a copy that goes stale, and this file has been bitten by one already.
		button comes back as the window the press would reach.  For a press that only arms a chord
		it is the first window in the armed group that would take the second key, or NULL when the
		group holds nothing to press, so a caller can tell a chord worth starting from an empty one. */
	enum PressOutcome
	{
		PRESS_DOES_NOTHING,		///< no button behind this key right now
		PRESS_ARMS_CHORD,			///< first half of a two key chord
		PRESS_IS_REFUSED,			///< a button is there and it is disabled
		PRESS_FIRES						///< the button would take it
	};
	PressOutcome peekCommandButtonPress( Int index, GameWindow **button );
	PressOutcome peekSpecialPowerShortcutPress( Int index, GameWindow **button );

	/** Where a press lands: a slot index, or one of these two.  The two functions below are the
		whole decision and touch no window, so the tests can hold every row of it; the press calls
		and the peek calls gather what is on the bar and ask them. */
	enum { SLOT_NOTHING = -1, SLOT_ARMS_CHORD = -2 };
	static Int resolveGridPress( Int index, Int chordGroup, Bool hasStructures, Bool indexIsStructure );
	static Int resolveTrayPress( Int index, Int armedRow, Int visibleSlots );

	/** The promotion screen answers the group keys while it is open: 1 to 5 name its five columns.
		A promotion point is spent for good, so it takes two presses like the general's powers do -
		the first marks the next science the column will sell you, the same key again buys it.  A
		different number marks that column instead, and CHORD_TIMEOUT_MS of nothing drops the mark. */
	void pressPurchaseScienceColumn( Int column );

	/** forget a marked column.  TRUE when there was one to forget. */
	Bool clearPurchaseScienceColumn( void );

	/** A builder with more structures than BUILD_PAGE_ONE_SIZE does not show them all at once.
		The command bar opens on two menu buttons instead - Q and W - and each one opens its
		own page of structures, so a structure is two keystrokes away (Q-Q, Q-A, ... W-Q, ...)
		instead of competing for the fourteen slots.  Page one holds the first
		BUILD_PAGE_ONE_SIZE structures of the command set (the ones that sit on Q W E / A S D
		without paging), page two holds the rest (the R T Y / F G H region). */
	enum { BUILD_PAGE_ROOT = -1, BUILD_PAGE_COUNT = 2, BUILD_PAGE_ONE_SIZE = 6 };
	void setBuildPage( Int page );

	/** with multiple units selected the right HUD shows the unit types; Tab (+1) and
		Shift-Tab (-1) walk the focus between them */
	void cycleMultiSelectFocus( Int direction );

	/** a builder's structures are reached by a two-key chord: the Q cell (slot 0) arms the
		structures in columns 1-4 (slots 0..7), the W cell (slot 2) the ones in columns 5-7
		(slots 8..), then the next grid key picks the cell within the group by its own position */
	enum { CHORD_SLOT_Q = 0, CHORD_SLOT_W = 2, CHORD_GROUP_SIZE = 8 };

	/** a raw key while a chord is armed: the key of one of the group's own cells (the grid's
		slots 0..7 - Q Z W X E C R V as shipped) resolves the chord and returns TRUE (the key is
		eaten), anything else drops it */
	Bool handleChordKey( Int mappableKey );
	Bool isChordArmed( void ) const { return m_chordGroup >= 0; }

	/** forget a half-typed chord.  An armed chord swallows the next grid key and turns it into
		a structure to place, so anything that says the player has moved on
		(a click, a new selection, a key that is not part of the chord, or simply time passing)
		must drop it. */
	void dropChord( void );
	enum { CHORD_TIMEOUT_MS = 4000 };			///< real time, not frames: the client frame rate is uncapped

	/// is the drawable the currently selected drawable for the context sensitive UI?
	Bool isDrivingContextUI( Drawable *draw ) const { return draw == m_currentSelectedDrawable; }

	//-----------------------------------------------------------------------------------------------
	// the remaining methods are used to construct the command buttons and command sets for
	// the command bar
	//-----------------------------------------------------------------------------------------------

	/// find existing command button if present
	const CommandButton *findCommandButton( const AsciiString& name );

	/// find existing command set
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
	Player *getObserverLookAtPlayer (void ) { return m_observerLookAtPlayer;}
	void populateObserverInfoWindow ( void );
	void populateObserverList( void );
	Bool isObserverControlBarOn( void ) { return m_isObserverCommandBar;}
	

	// Functions for repositioning/resizing the control bar
	void switchControlBarStage( ControlBarStages stage );
	void restoreStageAfterScheme( void );		///< a new scheme keeps a minimised bar minimised
	void toggleControlBarStage( void );

	const Image *getStarImage( void );

	Color getBorderColor( void ){return m_commandBarBorderColor;}
	/// the two button border colours the bar itself uses, so anything drawn outside it can match
	Color getBuildBorderColor( void ){return m_commandButtonBorderBuildColor;}
	Color getUpgradeBorderColor( void ){return m_commandButtonBorderUpgradeColor;}
	void updateBorderColor( Color color) {m_commandBarBorderColor = color;	}

	/** The tray one of the general's power shortcuts stands in, down in the corner - the tray of
		* whichever side this bar is showing, taken off the bar itself rather than named, so a mod's
		* bar brings its own.  Watching a match the bar has no side of its own to read, and the tray
		* comes out of the layout of the player the bar is looking at instead. */
	const Image *getSpecialPowerTrayImage( void );

	/** That tray at the size the bar draws it, the hole in it the cameo fills, and the step a row of
		* them runs at - measured off the same layout the tray came out of, so it is the size of the
		* powers' own at any resolution.  Any out-parameter may be NULL; FALSE when there is no bar
		* and no layout to borrow one from. */
	Bool getSpecialPowerTrayLayout( ICoord2D *traySize, ICoord2D *cameoSize,
																	ICoord2D *cameoOffset, Int *columnStep );

	/** The tray geometry from one slot's size.  Public because it is the single home of the artwork's
		* proportions: the bar itself, anything borrowing the look, and the tests all read it here. */
	static Bool trayLayoutFromSlot( Int slotWidth, Int slotHeight, ICoord2D *traySize,
																	ICoord2D *cameoSize, ICoord2D *cameoOffset, Int *columnStep );

	/// set the command data into the button
	void setControlCommand( GameWindow *button, const CommandButton *commandButton );

	void getForegroundMarkerPos(Int *x, Int *y);
	void getBackgroundMarkerPos(Int *x, Int *y);


	static void parseCommandSetDefinition( INI *ini );
	static void parseCommandButtonDefinition( INI *ini );

	void drawTransitionHandler( void );
	const Image *getArrowImage( void ){ return m_genArrow;	}
	void setArrowImage( const Image *arrowImage ){ m_genArrow = arrowImage;	}
	
	void initSpecialPowershortcutBar( Player *player);
	///< put the general's powers bar on screen now, filled in, with no slide-in (control changed hands)
	void showSpecialPowerShortcutInstantly( Player *player );

	void triggerRadarAttackGlow( void );

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

	/** Take the slide off and declare all three panels home.  Anything that reads a window's screen
		* position and writes another window's position from it has to run after this, or it writes
		* the answer for a bar that is on its way down and layoutPanels then reads that as the
		* rectangle the window was authored at.  ControlBarScheme::init is that caller. */
	void clearPanelSlide( void );

	/// top edge of the highest visible window the bar owns, and where MoneyDisplay is; see -uidrill
	void forEachPlacedWindow( Int *topOut, Int *moneyOut );

	/// the window every other one on the bar hangs off, so -uidrill can walk it looking for a button
	GameWindow *getMasterParent( void ) { return m_contextParent[ CP_MASTER ]; }

	/** Send one whole panel off the bottom of the screen, or bring it back.  It slides: the panel is
		* a picture of a thing, and a thing that is going away should be seen going.  'immediate' puts
		* it where it is going this instant, which is what a rebuild wants. */
	void showPanel( Int panel, Bool show, Bool immediate = FALSE );

	/// is that panel all the way off screen?  the plate art asks before it paints
	Bool isPanelHidden( Int panel ) const { return m_panelHidden[ panel ]; }

	/// how far down that panel has slid, in pixels - the plate art travels with its windows
	Int getPanelSlideOffset( Int panel ) const;

	/** TRUE when a click at this screen point, over this window, belongs to the world after all: the
		* window is one of the bar's input-blocking panes or CenterBackground, no window inside it is
		* under the point, and every plate painted there is transparent at that texel.  The panes are
		* rectangles and the plates are not, so without this a click on the sky above a sloped edge
		* went nowhere.  GameWindow::winPointInChild asks, so clicking and unit picking agree. */
	Bool letsClickThrough( GameWindow *window, Int x, Int y );

	/** The screen rectangles the bar's CSS page drew solid, which take the place of the plates in
		* letsClickThrough while the page is drawing; NULL when it is not, and the plates decide again.
		* `holes` are the page's own buttons: a click on one goes through to the page even inside a
		* solid panel. */
	void setPageSolids( const std::vector< IRegion2D > *solids, const std::vector< IRegion2D > *holes = NULL );

	/** The HUD page's place for the general's powers: the first in `corner`, the bottom right of the
		* grid, each `cell` big with `gap` between them, a row of SPECIAL_POWER_SHORTCUT_COLS running
		* left from it and the next row over it, the order the row keys count in.  The slots' own tray
		* art is not drawn; the page draws their cells.  NULL hides the bar.  Screen pixels.  Asked every
		* frame the page draws, so it holds against the bar's own layout.  Returns how many powers are
		* shown, each in its place. */
	Int placeSpecialPowerShortcutGrid( const ICoord2D *corner, const ICoord2D &cell, Int gap );

	/** Puts one of the bar's windows `inset` pixels inside the rectangle layoutPanels gave it, across
		* on both sides and down on both, or outside it where the inset is less than nought; nought gives
		* it the rectangle back.  A rebuild of the layout reads the window as that rectangle. */
	void insetPlacedWindow( GameWindow *window, const ICoord2D &inset );
	ICoord2D getPlacedInset( GameWindow *window ) const;	///< what insetPlacedWindow last put it in by
	/** Moves one of the bar's windows `shift` pixels down, and the places layoutPanels recorded for it
		* and everything inside it with it, so a rebuild of the layout reads it as placed there. */
	void lowerPlacedWindow( GameWindow *window, Int shift );
	GameWindow *getSpecialPowerShortcutParent( void ) { return m_specialPowerShortcutParent; }

	/// the general's stars are asking to be spent, so the button blinks; see getStarImage
	Bool isGeneralStarFlashing( void ) const { return m_genStarFlash; }

	/// the same journey as a fraction, 0 home and 1 gone.  Needs no display, which is why the test
	/// for the rebuild ordering asks this one; see clearPanelSlide
	Real getPanelSlideFraction( Int panel ) const
	{
		return ( panel < 0 || panel >= CB_PANEL_COUNT ) ? 0.0f : m_panelSlide[ panel ];
	}

protected:
	/// place one window and its descendants inside 'panel'; see layoutPanels
	void placeInPanel( GameWindow *win, Int panel,
										 Int oldParentX, Int oldParentY, Int newParentX, Int newParentY,
										 Int shiftX = 0 );

	void updateRadarAttackGlow ( void );
	
	void setDefaultControlBarConfig( void );
	void setSquishedControlBarConfig( void );
	void setLowControlBarConfig( void );
	void setHiddenControlBar( void );

	/// find existing command button if present
	CommandButton* findNonConstCommandButton( const AsciiString& name );
	
	/// allocate a new command button, link to list, initialize to default, and return
	CommandButton *newCommandButton( const AsciiString& name );
	CommandButton *newCommandButtonOverride( CommandButton *buttonToOverride );

	/// allocate a new command set, link to list, initialize to default, and return it
	CommandSet *newCommandSet( const AsciiString& name );
	CommandSet *newCommandSetOverride( CommandSet *setToOverride );


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

	/// post process step, after all commands and command sets are loaded
	void postProcessCommands( void );

	// the following methods are for resetting data for vaious contexts
	void resetContainData( void );			/// reset container data we use to tie controls to objects IDs for containment
	void resetBuildQueueData( void );			/// reset the build queue data we use to die queue entires to control
	void resetBuildQueueButtons( void );	/// attach the queue button windows and wipe them to empty

	// the following methods are for populating the context GUI controls for a particular context
	static void populateButtonProc( Object *obj, void *userData );
	void populatePurchaseScience(Player* player);
	void populateCommand( Object *obj );
	void buildCommandLayout( Object *obj, const CommandSet *commandSet, const CommandButton **slot );
	void makeBuildPageButtons( void );
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

	/** Where a press lands with the chords as they stand.  The press calls and the peek calls both
		go through these, which is the point of them. */
	Int resolveCommandSlot( Int index, Bool *hasStructures ) const;
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
	void setFlash( Bool b ) { m_flash = b; }

	// get method for list of commandbuttons
	const CommandButton *getCommandButtons( void ) { return m_commandButtons; }

protected:

	ICoord2D m_defaultControlBarPosition;				///< Stored the original position of the control bar on the screen
	ControlBarStages m_currentControlBarStage;

	IRegion2D m_panelRect[ CB_PANEL_COUNT ];				///< screen rect of each panel, filled by layoutPanels()
	ICoord2D m_panelOrigin;													///< where the frame was put, so the plates can follow it
	Bool m_panelHidden[ CB_PANEL_COUNT ];						///< minimised: the radar and the middle stand down
	Real m_panelSlide[ CB_PANEL_COUNT ];						///< 0 home, 1 clear of the bottom edge
	Real m_panelSlideTo[ CB_PANEL_COUNT ];					///< where each one is heading
	Int m_panelDropCap[ CB_PANEL_COUNT ];						///< pixels a panel may travel; 0 = all the way off
	UnsignedInt m_panelSlideMs;											///< wall clock of the last slide step

	void updatePanelSlide( void );									///< step the slide and put the windows where it says
	void applyPanelSlide( void );										///< the placing half of it, on its own so a rebuild can call it

	Bool m_UIDirty;																///< the context UI must be re-evaluated

	CommandButton *m_commandButtons;							///< list of possible commands to have
	CommandSet *m_commandSets;										///< list of all command sets defined
	ControlBarSchemeManager *m_controlBarSchemeManager;		///< The Scheme singleton

	GameWindow *m_contextParent[ NUM_CONTEXT_PARENTS ];		///< "parent" window for buttons that are part of the context sensitive interface

	Drawable *m_currentSelectedDrawable;					///< currently selected drawable for the context sensitive interface
	ControlBarContext m_currContext;							///< our current displayed context

	DrawableID m_rallyPointDrawableID;						///< rally point drawable for visual rally point 

	Real m_displayedConstructPercent;							///< construct percent last displayed to user
	UnsignedInt m_displayedOCLTimerSeconds;				///< OCL Timer seconds remaining last displayed to user
	UnsignedInt m_displayedQueueCount;						///< queue count last displayed to user
	UnsignedInt m_lastRecordedInventoryCount;			///< last known UI state of an inventory count

	GameWindow *m_rightHUDWindow;									///< window of the right HUD display
	GameWindow *m_rightHUDCameoWindow;									///< window of the right HUD display
	GameWindow *m_rightHUDUpgradeCameos[MAX_RIGHT_HUD_UPGRADE_CAMEOS];
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

	/** A tray taken out of a side's shortcut layout without standing that side's bar up: what the
		* HUD needs while watching somebody else play.  The layout is created, measured and destroyed
		* on the spot - the image itself belongs to the mapped image collection and outlives it - and
		* the answer is kept, since a spectator's bar asks for the same side every frame. */
	struct BorrowedTray
	{
		AsciiString layout;					///< the .wnd it came out of, and the key this is found by
		const Image *image;					///< that layout's slot background, NULL when it has none
		ICoord2D size;							///< the size the loader gave that slot
	};
	enum { MAX_BORROWED_TRAYS = 8 };
	BorrowedTray m_borrowedTrays[ MAX_BORROWED_TRAYS ];
	Int m_borrowedTrayCount;
	const BorrowedTray *borrowTray( const PlayerTemplate *pt );

	/// whose tray this bar shows when it has no bar of its own: the side the bar itself is wearing
	const PlayerTemplate *specialPowerTraySide( void );

	/// a template of this side that ships a general's power bar, so its tray can be read off it
	const PlayerTemplate *templateForSide( AsciiString side );

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

	GameWindow *m_commandWindows[ MAX_COMMANDS_PER_SET ];			///< command window controls for easy access

	CommandButton *m_buildPageButton[ BUILD_PAGE_COUNT ];	///< the two menu buttons a paged builder opens on
	CommandButton *m_buildPageBackButton;									///< takes a page back to the menu buttons
	Int m_buildPage;																			///< BUILD_PAGE_ROOT, or the page being shown
	ObjectID m_buildPageObjectID;													///< builder the page belongs to; a new one starts at the menu
	Int m_chordGroup;																			///< -1, or the structure group (0 = Q, 1 = W) armed by the first chord key
	UnsignedInt m_chordStartMs;														///< millisecond the chord was armed on, for CHORD_TIMEOUT_MS
	DrawableID m_chordDrawableID;													///< builder the armed chord addresses; the chord dies if the bar moves to another one

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
	Int m_multiSelectFocus;																			///< the group Tab currently focuses

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
	AsciiString m_watchedSide;													///< the side the bar is wearing while watching, so a selection change only lays it out again when it really changes side
	std::vector< IRegion2D > m_pageSolids;							///< what the CSS page drew solid, in screen pixels; see setPageSolids
	std::vector< IRegion2D > m_pageHoles;								///< the page's own buttons, which let their clicks through
	Bool m_pageSolidsActive;														///< the page is drawing, so m_pageSolids decides clicks and not the plates
	Player *m_watchedSelection;													///< the player the selection last named while watching, NULL for nobody

	WindowLayout *m_buildToolTipLayout;										///< The window that will slide on/display tooltips
	Bool m_showBuildToolTipLayout;											///< every frame we test to see if we aregoing to continue showing this or not.
	BuildTooltipCard m_buildTooltipCard;								///< what the layout says, piece by piece
	BuildTooltipCard m_boardCard;												///< the card for what the pointer is on in a page, the Tab scoreboard's
	const CommandButton *m_boardCardButton;							///< the button m_boardCard was filled in from
	Player *m_boardCardOwner;														///< and the player its figures were counted for
	Bool m_boardCardShown;															///< a page asked for m_boardCard this frame
	Bool m_boardCardWasShown;														///< and the frame before, so a card pointed at again is filled in again
public:
	/** What the build tooltip up on screen says, NULL while none is. */
	const BuildTooltipCard *getBuildTooltipCard( void );
	/** The build card for a button a page draws rather than a window, its prices and figures counted
		* for owner, standing against anchor, for this frame: a page calls hideBoardCard every frame and
		* this while the pointer is on one. */
	void showBoardCard( const CommandButton *button, Player *owner, const IRegion2D &anchor );
	void hideBoardCard( void );
	void showBuildTooltipLayout( GameWindow *cmdButton );
	void hideBuildTooltipLayout( void );
	void deleteBuildTooltipLayout( void );
	Bool getShowBuildTooltipLayout( void ){return m_showBuildToolTipLayout;	}
	void populateBuildTooltipLayout( const CommandButton *commandButton, GameWindow *tooltipWin = NULL );
	/// the same with the prices and figures counted for player rather than the local one
	void populateBuildTooltipLayout( const CommandButton *commandButton, GameWindow *tooltipWin, Player *player );
	void repopulateBuildTooltipLayout( void );
	/// where the popup lives, so -uidrill can read the rectangle it landed in.  The tooltip is placed
	/// against BackgroundMarker's screen position, so it goes wherever the bar's windows went
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

	/// find existing command set
	CommandSet *findNonConstCommandSet( const AsciiString& name );

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
	
	Bool m_radarAttackGlowOn;
	Int m_remainingRadarAttackGlowFrames;
	GameWindow *m_radarAttackGlowWindow;

#if defined( _INTERNAL ) || defined( _DEBUG )
	UnsignedInt m_lastFrameMarkedDirty;
	UnsignedInt m_consecutiveDirtyFrames;
#endif

}; 

// EXTERNALS //////////////////////////////////////////////////////////////////////////////////////
extern ControlBar *TheControlBar;

/// -uidrill: write one line saying where the bar's windows actually are, so a long run says whether
/// they hold still.  See ControlBar::clearPanelSlide for what they used to do instead.
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

/** The one scale the whole HUD is drawn at: the display over the 800x600 everything was authored
	* at, the smaller of the two axes so nothing is distorted.  The .wnd loader multiplies x and y
	* separately, which on a 16:9 screen is a 1.33x horizontal smear; anything that wants to sit
	* beside the command bar without being smeared measures itself with this instead.  Never below
	* 1 - nothing shrinks under 800 wide. */
extern Real ControlBarUniformScale( void );
extern Real ControlBarUniformScaleFor( Int displayWidth, Int displayHeight );	///< ...for a screen you name

/** Undo the .wnd loader's separate-axis stretch over a whole layout: every window under 'root' is
	* recovered to its authored 800x600 rectangle and put back at ControlBarUniformScale(), anchored
	* at the given fraction of the screen (0 = left/top edge, 1 = right/bottom, 0.5 = centred) with
	* the matching fraction of the design space as its fixed point.  This is layoutPanels for a
	* layout that is one piece rather than three: the generals' power bar hangs off the right edge
	* with (1,1) and the science screen is centred at the top with (0.5,0).
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

#endif  // end __CONTROLBAR_H_

