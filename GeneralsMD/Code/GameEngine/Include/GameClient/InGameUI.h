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

// FILE: InGameUI.h ///////////////////////////////////////////////////////////////////////////////
// Defines the in-game user interface singleton
// Author: Michael S. Booth, March 2001
//				 Colin Day August 2001, or so
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _IN_GAME_UI_H_
#define _IN_GAME_UI_H_

#include "Common/GameCommon.h"
#include "Common/GameEngine.h"		// for RateReading
#include "Common/GameType.h"
#include "Common/MessageStream.h"		// for GameMessageTranslator
#include "Common/KindOf.h"
#include "Common/LogicThread.h"
#include "Common/SpecialPowerType.h"
#include "Common/Snapshot.h"
#include "Common/STLTypedefs.h"
#include "Common/SubsystemInterface.h"
#include "Common/UnicodeString.h"
#include "GameClient/DisplayString.h"
#include "GameLogic/OrderQueue.h"		// the shift queue, which the logic keeps and the UI draws

#include <map>
#include <set>
#include <string>
#include <vector>

#include "GameClient/Mouse.h"
#include "GameClient/RadiusDecal.h"
#include "GameClient/View.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
class Drawable;
class Object;
class ThingTemplate;
class GameWindow;
class VideoBuffer;
class VideoStreamInterface;
class CommandButton;
class SpecialPowerTemplate;
class WindowLayout;
class Anim2DTemplate;
class Anim2D;
class Shadow;
class Image;
class GameFont;
class GameSlot;
class Player;
struct SpectatorStats;
struct BroadcastLayout;
enum LegalBuildCode : Int;
enum KindOfType : Int;
enum ShadowType : Int;
enum CanAttackResult : Int;
enum ScienceType : Int;

/** The smoke signals a player drops for their allies, carried as the integer argument of
  * MSG_PLACE_SIGNAL.  The value arrives from another machine, so the receiving side range-checks
  * it against SIGNAL_KIND_COUNT. */
enum SignalKind
{
	SIGNAL_ATTACK,
	SIGNAL_DEFEND,
	SIGNAL_ATTENTION,
	SIGNAL_KIND_COUNT
};

// ------------------------------------------------------------------------------------------------
enum RadiusCursorType : Int
{
	RADIUSCURSOR_NONE = 0,
	RADIUSCURSOR_ATTACK_DAMAGE_AREA,
	RADIUSCURSOR_ATTACK_SCATTER_AREA,
	RADIUSCURSOR_ATTACK_CONTINUE_AREA,
	RADIUSCURSOR_GUARD_AREA,
	RADIUSCURSOR_EMERGENCY_REPAIR,
	RADIUSCURSOR_FRIENDLY_SPECIALPOWER,
	RADIUSCURSOR_OFFENSIVE_SPECIALPOWER,
	RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA,
	
	RADIUSCURSOR_PARTICLECANNON, 
	RADIUSCURSOR_A10STRIKE,
	RADIUSCURSOR_CARPETBOMB,
	RADIUSCURSOR_DAISYCUTTER,
	RADIUSCURSOR_PARADROP,
	RADIUSCURSOR_SPYSATELLITE, 
	RADIUSCURSOR_SPECTREGUNSHIP,
	RADIUSCURSOR_HELIX_NAPALM_BOMB,

	RADIUSCURSOR_NUCLEARMISSILE, 
	RADIUSCURSOR_EMPPULSE,
	RADIUSCURSOR_ARTILLERYBARRAGE,
	RADIUSCURSOR_NAPALMSTRIKE,
	RADIUSCURSOR_CLUSTERMINES,

	RADIUSCURSOR_SCUDSTORM, 
	RADIUSCURSOR_ANTHRAXBOMB,
	RADIUSCURSOR_AMBUSH, 
	RADIUSCURSOR_RADAR,
	RADIUSCURSOR_SPYDRONE,
	RADIUSCURSOR_FRENZY,
	
	RADIUSCURSOR_CLEARMINES,
	RADIUSCURSOR_AMBULANCE,


	RADIUSCURSOR_COUNT	// keep last
};

#ifdef DEFINE_RADIUSCURSOR_NAMES
static const char *TheRadiusCursorNames[] = 
{
	"NONE",
	"ATTACK_DAMAGE_AREA",
	"ATTACK_SCATTER_AREA",
	"ATTACK_CONTINUE_AREA",
	"GUARD_AREA",
	"EMERGENCY_REPAIR",
	"FRIENDLY_SPECIALPOWER",	//green
	"OFFENSIVE_SPECIALPOWER", //red
	"SUPERWEAPON_SCATTER_AREA",//red

	"PARTICLECANNON", 
	"A10STRIKE",
	"CARPETBOMB",
	"DAISYCUTTER",
	"PARADROP",
	"SPYSATELLITE",  
  "SPECTREGUNSHIP",
  "HELIX_NAPALM_BOMB",

	"NUCLEARMISSILE", 
	"EMPPULSE",
	"ARTILLERYBARRAGE",
	"NAPALMSTRIKE",
	"CLUSTERMINES",

	"SCUDSTORM", 
	"ANTHRAXBOMB",
	"AMBUSH", 
	"RADAR",
	"SPYDRONE",
	"FRENZY",
	
	"CLEARMINES",
	"AMBULANCE",

	NULL
};
#endif

// ------------------------------------------------------------------------------------------------
/** For keeping track in the UI of how much build progress has been done */
// ------------------------------------------------------------------------------------------------
enum { MAX_BUILD_PROGRESS = 64 };  ///< interface can support building this many different units
struct BuildProgress
{
	const ThingTemplate *m_thingTemplate;
	Real m_percentComplete;
	GameWindow *m_control;
};

// TYPE DEFINES ///////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
typedef std::list<Drawable *> DrawableList;
typedef std::list<Drawable *>::iterator DrawableListIt;
typedef std::list<Drawable *>::const_iterator DrawableListCIt;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class SuperweaponInfo : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(SuperweaponInfo, "SuperweaponInfo")		

private:
// not saved
	DisplayString *             m_nameDisplayString;						///< display string used to render the message
	DisplayString *             m_timeDisplayString;						///< display string used to render the message
	Color												m_color;
	const SpecialPowerTemplate*	m_powerTemplate;

public:

	SuperweaponInfo(
		ObjectID id,
		UnsignedInt timestamp,
		Bool hiddenByScript,
		Bool hiddenByScience,
		Bool ready,
    Bool evaReadyPlayed,
		const AsciiString& superweaponNormalFont, 
		Int superweaponNormalPointSize, 
		Bool superweaponNormalBold,
		Color c, 
		const SpecialPowerTemplate* spt
	);

	const SpecialPowerTemplate*	getSpecialPowerTemplate() const { return m_powerTemplate; }
	Color												getColor() const { return m_color; }
	void setFont(const AsciiString& superweaponNormalFont, Int superweaponNormalPointSize, Bool superweaponNormalBold);
	void setText(const UnicodeString& name, const UnicodeString& time);
	void drawBackdrop(Int x, Int y);				///< plate behind the name and time, so they stay readable over terrain
	void drawName(Int x, Int y, Color color, Color dropColor);
	void drawTime(Int x, Int y, Color color, Color dropColor);
	Real getHeight() const;

// saved & public
	AsciiString									m_powerName;
	ObjectID										m_id;
	UnsignedInt									m_timestamp;									  ///< seconds shown in display string
	Bool												m_hiddenByScript;
	Bool												m_hiddenByScience;
 	Bool												m_ready;											///< Stores if we were ready last draw, since readyness can change without time changing
  Bool                        m_evaReadyPlayed;             ///< Stores if Eva announced superweapon is ready
// not saved, but public
 	Bool												m_forceUpdateText;

};

// ------------------------------------------------------------------------------------------------
typedef std::list<SuperweaponInfo *> SuperweaponList;
typedef std::map<AsciiString, SuperweaponList> SuperweaponMap;

// ------------------------------------------------------------------------------------------------
// Popup message box
class PopupMessageData : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(PopupMessageData, "PopupMessageData")		
public:
	UnicodeString		message;
	Int							x;
	Int							y;
	Int							width;
	Color						textColor;
	Bool						pause;
	Bool						pauseMusic;
	WindowLayout*	layout;
};
EMPTY_DTOR(PopupMessageData)

// ------------------------------------------------------------------------------------------------
class NamedTimerInfo : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(NamedTimerInfo, "NamedTimerInfo")		
public:
	AsciiString			m_timerName;							///< Timer name, needed on Load to reconstruct Map.
	UnicodeString		timerText;								///< timer text
	DisplayString*	displayString;						///< display string used to render the message
	UnsignedInt			timestamp;									///< seconds shown in display string
	Color						color;
	Bool						isCountdown;
};
EMPTY_DTOR(NamedTimerInfo)

// ------------------------------------------------------------------------------------------------
typedef std::map<AsciiString, NamedTimerInfo *> NamedTimerMap;
typedef NamedTimerMap::iterator NamedTimerMapIt;

// ------------------------------------------------------------------------------------------------
enum {MAX_SUBTITLE_LINES = 4};							///< The maximum number of lines a subtitle can have

// ------------------------------------------------------------------------------------------------
// Floating Text Data
class FloatingTextData : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(FloatingTextData, "FloatingTextData")		
public:
	FloatingTextData(void);
	//~FloatingTextData(void);

	Color						m_color;														///< It's current color
	UnicodeString		m_text;											///< the text we're displaying
	DisplayString*	m_dString;									///< The display string
	Coord3D					m_pos3D;													///< the 3d position in game coords
	Int							m_frameTimeOut;												///< when we want this thing to disappear
	Int							m_frameCount;													///< how many frames have we been displaying text?
};

typedef std::list<FloatingTextData *> FloatingTextList;
typedef FloatingTextList::iterator	FloatingTextListIt;

enum 
{
	DEFAULT_FLOATING_TEXT_TIMEOUT = LOGICFRAMES_PER_SECOND/3,
};

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
enum WorldAnimationOptions
{
	WORLD_ANIM_NO_OPTIONS								= 0x00000000,
	WORLD_ANIM_FADE_ON_EXPIRE						= 0x00000001,
	WORLD_ANIM_PLAY_ONCE_AND_DESTROY		= 0x00000002,
};

// ------------------------------------------------------------------------------------------------
class WorldAnimationData
{

public:

	WorldAnimationData( void );
	~WorldAnimationData( void ) { }

	Anim2D *m_anim;												///< the animation instance
	Coord3D m_worldPos;										///< position in the world
	UnsignedInt m_expireFrame;						///< frame we expire on
	WorldAnimationOptions m_options;			///< options
	Real m_zRisePerSecond;								///< Z units to rise per second

};
typedef std::list< WorldAnimationData *> WorldAnimationList;
typedef WorldAnimationList::iterator WorldAnimationListIterator;

/** A plan waiting for its builder, as the number over its silhouette is worked out. */
struct BuildPlanNumber
{
	ObjectID plan;
	ObjectID builder;					///< the dozer or worker the plan was given to
	Bool current;							///< the builder is already on its way to this one
	Coord3D spot;							///< where the number goes, in the world
	Int step;									///< 1 for the next one built, 0 when its builder has only the one
};

/** Number each builder's plans in the order it will take them: the one it is walking to first, then
	* lowest id first, which is the order findUnfinishedStructureToContinue picks them in.  The list
	* comes back sorted by builder and step. */
void InGameUI_numberBuildPlans( std::vector<BuildPlanNumber>& plans );

// ------------------------------------------------------------------------------------------------
/** Basic functionality common to all in-game user interfaces */
// ------------------------------------------------------------------------------------------------ 
class InGameUI : public SubsystemInterface, public Snapshot
{
	
friend class Drawable;	// for selection/deselection transactions
		
public:  // ***************************************************************************************

	enum SelectionRules
	{
		SELECTION_ANY, //Only one of the selected units has to qualify
		SELECTION_ALL, //All selected units have to qualify
	};
	enum ActionType
	{
		ACTIONTYPE_NONE,
		ACTIONTYPE_ATTACK_OBJECT,
		ACTIONTYPE_GET_REPAIRED_AT,
		ACTIONTYPE_DOCK_AT,
		ACTIONTYPE_GET_HEALED_AT,
		ACTIONTYPE_REPAIR_OBJECT,
		ACTIONTYPE_RESUME_CONSTRUCTION,
		ACTIONTYPE_ENTER_OBJECT,
		ACTIONTYPE_HIJACK_VEHICLE,
		ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB,
		ACTIONTYPE_CAPTURE_BUILDING,
		ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING,
#ifdef ALLOW_SURRENDER
		ACTIONTYPE_PICK_UP_PRISONER,
#endif
		ACTIONTYPE_STEAL_CASH_VIA_HACKING,
		ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING,
		ACTIONTYPE_MAKE_DEFECTOR,
		ACTIONTYPE_SET_RALLY_POINT,
		ACTIONTYPE_COMBATDROP_INTO,
		ACTIONTYPE_SABOTAGE_BUILDING,

		//Keep last.
		NUM_ACTIONTYPES
	};

	InGameUI( void );
	virtual ~InGameUI( void );
	
	// Inherited from subsystem interface -----------------------------------------------------------
	virtual	void init( void );															///< Initialize the in-game user interface
	virtual void update( void );														///< Update the UI by calling preDraw(), draw(), and postDraw()
	virtual void reset( void );															///< Reset
	//-----------------------------------------------------------------------------------------------

	// interface for the popup messages
	virtual void popupMessage( const AsciiString& message, Int x, Int y, Int width, Bool pause, Bool pauseMusic);
	virtual void popupMessage( const AsciiString& message, Int x, Int y, Int width, Color textColor, Bool pause, Bool pauseMusic);
	PopupMessageData *getPopupMessageData( void ) { return m_popupMessageData; }
	void clearPopupMessageData( void );

	// interface for messages to the user
	// srj sez: passing as const-ref screws up varargs for some reason. dunno why. just pass by value.
	virtual void message( UnicodeString format, ... );				  ///< display a message to the user
	virtual void message( AsciiString stringManagerLabel, ... );///< display a message to the user
	void playerMessage( Player *player, const UnicodeString &text );	///< a message about that player, in his colour
	void chatMessage( Player *player, const UnicodeString &text );		///< a line of chat, "[name] text" in his colour
	virtual void toggleMessages( void ) { m_messagesOn = 1 - m_messagesOn; }	///< toggle messages on/off
	virtual Bool isMessagesOn( void ) { return m_messagesOn; }	///< are the display messages on
	void freeMessageResources( void );				///< empty the message list
	Color getMessageColor(Bool altColor) { return (altColor)?m_messageColor2:m_messageColor1; }
	
	// interface for military style messages
	virtual void militarySubtitle( const AsciiString& label, Int duration );			// time in milliseconds
	virtual void removeMilitarySubtitle( void );
	
	// for can't build messages
	virtual void displayCantBuildMessage( LegalBuildCode lbc ); ///< display message to use as to why they can't build here

	// interface for graphical "hints" which provide visual feedback for user-interface commands
	virtual void beginAreaSelectHint( const GameMessage *msg );	///< Used by HintSpy. An area selection is occurring, start graphical "hint"
	virtual void endAreaSelectHint( const GameMessage *msg );		///< Used by HintSpy. An area selection had occurred, finish graphical "hint"
	// The line the player is dragging out for a formation move, in screen pixels.  It is the curve
	// the cursor actually traced, not the chord: an arc round a rock is a shape a straight segment
	// cannot say.
	enum
	{
		MAX_FORMATION_DRAG_POINTS = 32,		///< the message carries these, so the curve cannot grow forever
		FORMATION_DRAG_MIN_SPACING = 12		///< pixels between kept points, doubled when the curve fills up
	};
	void addFormationDragPoint( const ICoord2D& pt );
	// The markers are not thrown away with the drag.  They are rebuilt from the units every frame
	// anyway, before anything is drawn, and emptying the list here cost every marker on the screen
	// its age: a right click is what ends a drag, so every order the player gave made the whole
	// screen slide in again instead of only the marker that was new.
	void clearFormationDrag( void ) { m_isFormationDragging = FALSE; m_formationDragPoints.clear(); }
	Bool isFormationDragging( void ) const { return m_isFormationDragging; }
	const std::vector<ICoord2D>& getFormationDragPoints( void ) const { return m_formationDragPoints; }

	// Where each selected unit is headed, and what kind of order sent it there.  While a formation
	// line is being drawn these are worked out from the curve, with the same ordering the logic
	// uses, so the preview and the orders agree; the rest of the time they are read straight off
	// the units' own goals, so they last exactly as long as the order does.
	// One kind per cursor the game already owns, because the marker on the destination is that
	// cursor: an order the player can give with a distinct cursor reads back with the same one.
	enum OrderHintKind
	{
		ORDER_HINT_MOVE = 0,				///< go there
		ORDER_HINT_ATTACK_MOVE,			///< go there, shooting whatever gets in the way
		ORDER_HINT_ATTACK,					///< kill that
		ORDER_HINT_FORCE_ATTACK,		///< kill that, whoever owns it
		ORDER_HINT_ATTACK_GROUND,		///< shoot that spot
		ORDER_HINT_ENTER,						///< get inside it
		ORDER_HINT_DOCK,						///< dock with it
		ORDER_HINT_GET_REPAIRED,		///< go and be repaired
		ORDER_HINT_GET_HEALED,			///< go and be healed
		ORDER_HINT_DO_REPAIR,				///< go and repair it
		ORDER_HINT_CAPTURE,					///< go and take it
		ORDER_HINT_HACK,						///< go and hack it
		ORDER_HINT_GUARD,						///< hold that spot
		ORDER_HINT_WAYPOINT,				///< walking a path somebody laid down
		ORDER_HINT_ABILITY,					///< use an ability where it stands, from a shift list
		ORDER_HINT_UPGRADE					///< buy an upgrade where it stands, from a shift list
	};
	// What stands on the end of an order's line, in EA's rally point art: the flag on the last bit of
	// ground a unit is sent to, the line's joint on everything else - a unit or building it is sent
	// at (which the joint then follows), and every point of a shift list short of the last
	enum OrderHintMark
	{
		ORDER_MARK_NONE = 0,				///< nothing of its own: an upgrade or ability used where the step before ends
		ORDER_MARK_JOINT,						///< EA's waypoint node
		ORDER_MARK_FLAG							///< EA's rally flag
	};
	static OrderHintMark markForOrderHint( OrderHintKind kind, Bool onObject, Bool lastPoint );

	struct OrderHint
	{
		OrderHint( void ) : kind( ORDER_HINT_MOVE ), owner( INVALID_ID ), bornMs( 0 ), step( 0 ), icon( NULL ),
			onObject( FALSE ), mark( ORDER_MARK_NONE ) {}

		Coord3D from;						///< where the unit is now
		Coord3D to;							///< where it is going
		OrderHintKind kind;
		ObjectID owner;					///< the selected unit this one belongs to
		UnsignedInt bornMs;			///< when the marker first appeared, so it can be slid in
		Int step;								///< its place in the order the unit will get to its points, from 1; 0 when it has only the one
		const Image *icon;			///< the upgrade's own button art on an upgrade step, NULL otherwise
		Bool onObject;					///< 'to' is a unit or building, read off it every frame
		OrderHintMark mark;			///< what stands on 'to'
	};
	const std::vector<OrderHint>& getOrderHints( void ) const { return m_drawnOrderHints; }

	// A circle dragged out with the left button while the attack key is armed or Ctrl is held.  The
	// enemies inside it are shared across the units that can shoot, in the order both sides stand
	// around the circle, and each unit works down its own share.
	void beginAttackCircle( const ICoord2D& pt );
	void updateAttackCircle( const ICoord2D& pt );
	Bool issueAttackCircle( void );		///< FALSE when the press never became a drag, so it was an ordinary attack click
	Int issueAttackLine( const std::vector<Coord3D>& line );	///< enemies the stroke crosses, shared along it across the units that can shoot
	Bool isAttackListTarget( const Object *obj, const Player *local ) const;	///< would the circle or the attack line take this
	void cancelAttackCircle( void ) { m_isAttackCircling = FALSE; }
	Bool isAttackCircling( void ) const { return m_isAttackCircling; }
	const ICoord2D& getAttackCircleAnchor( void ) const { return m_attackCircleAnchor; }
	const ICoord2D& getAttackCircleCursor( void ) const { return m_attackCircleCursor; }
	Bool getAttackCircleGround( Coord3D& center, Real& radius ) const;	///< the circle in world terms, FALSE while it is still a dot

	/// The order about to go on the message stream is a shift-queued one: MSG_QUEUE_NEXT_ORDER goes
	/// in front of it.  The logic keeps the list; see OrderQueue.h.
	void markNextOrderQueued( OrderQueueMode mode );

	void createMoveHint( const GameMessage *msg );							///< Classic: EA's animated ring where a move order lands
	virtual void createAttackHint( const GameMessage *msg );		///< An attack command has occurred, start graphical "hint"
	virtual void createForceAttackHint( const GameMessage *msg );		///< A force attack command has occurred, start graphical "hint"

	virtual void createMouseoverHint( const GameMessage *msg );	///< An object is mouse hovered over, start hint if any
	virtual void createCommandHint( const GameMessage *msg );		///< Used by HintSpy. Someone is selected so generate the right Cursor for the potential action
	virtual void createGarrisonHint( const GameMessage *msg );  ///< A garrison command has occurred, start graphical "hint"
	
	virtual void addSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate);
	virtual Bool removeSuperweapon(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate);
	virtual void objectChangedTeam(const Object *obj, Int oldPlayerIndex, Int newPlayerIndex);	// notification for superweapons, etc

	virtual void setSuperweaponDisplayEnabledByScript( Bool enable );	///< Set the superweapon display enabled or disabled
	virtual Bool getSuperweaponDisplayEnabledByScript( void ) const;				///< Get the current superweapon display status

	virtual void hideObjectSuperweaponDisplayByScript(const Object *obj);
	virtual void showObjectSuperweaponDisplayByScript(const Object *obj);
	void setObjectSuperweaponHiddenByScript( ObjectID objID, Bool hidden );	///< both of the above, after the tick when a tick asks

	void addNamedTimer( const AsciiString& timerName, const UnicodeString& text, Bool isCountdown );
	void removeNamedTimer( const AsciiString& timerName );
	void showNamedTimerDisplay( Bool show );

	// mouse mode interface
	virtual void setScrolling( Bool isScrolling, Bool moveCursor = TRUE );	///< set scroll mode; moveCursor puts up the scroll arrows and takes them down
	virtual Bool isScrolling( void );														///< are we scrolling?
	virtual void setSelecting( Bool isSelecting );							///< set drag select mode
	virtual Bool isSelecting( void );														///< are we selecting?
	virtual void setScrollAmount( Coord2D amt );								///< set scroll amount
	virtual Coord2D getScrollAmount( void );										///< get scroll amount

	// gui command interface
	virtual void setGUICommand( const CommandButton *command );				///< the command has been clicked in the UI and needs additional data
	virtual const CommandButton *getGUICommand( void ) const;								///< get the pending gui command

	// build interface
	virtual void placeBuildAvailable( const ThingTemplate *build, Drawable *buildDrawable );				///< built thing being placed
	virtual const ThingTemplate *getPendingPlaceType( void );					///< get item we're trying to place
	virtual const Drawable *getPendingPlaceDrawable( void ) const;		///< the ghost riding the cursor, NULL when not placing
	virtual const ObjectID getPendingPlaceSourceObjectID( void );			///< get producing object
	virtual Bool getPreventLeftClickDeselectionInAlternateMouseModeForOneClick() const { return m_preventLeftClickDeselectionInAlternateMouseModeForOneClick; }
	virtual void setPreventLeftClickDeselectionInAlternateMouseModeForOneClick( Bool set ) { m_preventLeftClickDeselectionInAlternateMouseModeForOneClick = set; }
	virtual void setPlacementStart( const ICoord2D *start );					///< placement anchor point (for choosing angles)
	virtual void setPlacementEnd( const ICoord2D *end );							///< set target placement point (for choosing angles)
	virtual Bool isPlacementAnchored( void );													///< is placement arrow anchor set
	virtual void getPlacementPoints( ICoord2D *start, ICoord2D *end );///< get the placemnt arrow points
	virtual Real getPlacementAngle( void );														///< placement angle of drawable at cursor when placing down structures
	virtual Real computePlacementAngle( const ICoord2D *start, const ICoord2D *end );	///< heading a drag from start to end aims a structure at

	/** Round a heading to the nearest eighth of a turn.  Nearest, not toward or away from zero:
		* toAngle returns -PI..PI and the drag has to feel the same on both halves of the circle, so
		* every heading is at most 22.5 degrees of mouse travel from the one it snaps to.  Inline so a
		* test can reach it without linking the whole in-game UI. */
	static Real snapAngleTo45( Real angle )
	{
		const Real step = PI / 4.0f;

		return ((Real)REAL_TO_INT_FLOOR( angle / step + 0.5f )) * step;
	}
	/** Experimental GridBuildPlacement: quantize a structure's position to the pathfinder's own
		* build grid, so that structures put down by eye line up with each other and with the cells
		* the pathfinder actually reasons about. */
	virtual void snapPlacementToGrid( Coord3D *world, const ThingTemplate *what, Real angle ) const;
	/// is that snap on right now: the option, turned the other way while ctrl is held (not in Classic)
	Bool gridPlacementOn( void ) const;

	/** Where the pathfinder's cell boundaries actually are.  It files a world position under
		* floor( (v + 0.5) / PATHFIND_CELL_SIZE ) (see Pathfinder::internal_classifyObjectFootprint),
		* so cell k spans [10k - 0.5, 10k + 9.5) - the lines are at k*10 - 0.5, half a unit off the
		* round numbers.  Snapping to the round numbers left every footprint edge half a unit inside
		* the neighbouring cell, so two buildings that looked flush were both claiming it. */
	enum { PLACEMENT_CELL = 10 };										///< PATHFIND_CELL_SIZE, without the pathfinder header
	static Real placementGridLine( Int k ) { return k * (Real)PLACEMENT_CELL - 0.5f; }

	/** One axis of that snap.  'extent' is the structure's half-size along this axis, so a footprint
		* that is an odd number of cells wide is centred on a cell and an even one on the line between
		* two - either way its edges land on grid lines and two neighbours can share one.  Inline and
		* static so a test can reach it without linking the whole in-game UI. */
	static Real snapPlacementAxis( Real v, Real extent )
	{
		const Real cell = (Real)PLACEMENT_CELL;		// one pathfinder cell, one grid square
		const Real line = placementGridLine( 0 );	// where cell 0 starts

		Int cells = REAL_TO_INT_FLOOR( 2.0f * extent / cell + 0.5f );
		if( cells < 1 )
			cells = 1;

		const Real offset = ((cells & 1) ? cell * 0.5f : 0.0f) + line;

		return ((Real)REAL_TO_INT_FLOOR( (v - offset) / cell + 0.5f )) * cell + offset;
	}

	/** How far apart the centres of two boxes squared up with each other stand when one is moved off
		* the other along the unit direction 'ux'/'uy' until they no longer share ground.  'c'/'s' is
		* one box's heading; 'touchAlong' and 'touchAcross' are the two boxes' half-sizes added along
		* that heading and across it.  The row leaves the box the pair would share through whichever
		* face it meets first.  Plus a hair: the logic's clearance test counts a corner lying exactly on
		* the other box's edge as inside it (testRotatedPointsAgainstRect's <=), so two boxes that
		* touch to the unit are refused and the nudge throws the second a cell further off. */
	static Real placementTouchDistance( Real ux, Real uy, Real c, Real s, Real touchAlong,
																			Real touchAcross )
	{
		const Real parallel = 0.0001f;		// a projection this small is a face the move runs along, not into
		const Real clearance = 0.5f;			// world units left between two structures that touch

		const Real intoAlong = (Real)fabs( ux * c + uy * s );
		const Real intoAcross = (Real)fabs( uy * c - ux * s );
		Real touch = 0.0f;
		if( intoAlong > parallel )
			touch = touchAlong / intoAlong;
		if( intoAcross > parallel && ( touch == 0.0f || touchAcross / intoAcross < touch ) )
			touch = touchAcross / intoAcross;
		return touch + clearance;
	}

	/** A shift-dragged row: the step from one structure to the next, and how many fit between the
		* anchor and a cursor 'dx'/'dy' away.  The row runs the way the drag does, at any angle; it
		* went along the nearest eighth of a turn until the owner asked for 2026-09-28, and a line
		* dragged at 20 degrees came out flat.  It packs as tight as the footprint allows along it:
		* the footprint is the box 'halfFacing' by 'halfSide' turned to the heading.  So a structure
		* turned onto the row's own line stands face to face with the next, and one turned across it
		* corner to corner, which is the closest a straight row of those can get.  The step is not
		* rounded to the build grid: a Power Plant is 44 across and the grid is 10, and rounding it
		* up left 6 of dirt at every joint.  'extraGap' is world units the wheel adds on top of that
		* touch while the line is being drawn.  Zero packs them flush, and a negative value is
		* ignored, so the row never overlaps.  The first piece stands on the anchor and the last on
		* the cursor.  As many as that spacing allows go between them, and any slack is spread along
		* the line so the last one still lands on the cursor.  Opening the gap takes pieces out of
		* the middle.  A drag keeps both ends even when the asked gap is longer than the line, unless
		* 'most' allows only one.  Never fewer than one, never more than 'most'.  Inline and static
		* so a test can reach it without linking the whole in-game UI. */
	static Int placementRow( Real dx, Real dy, Real headingCos, Real headingSin, Real halfFacing,
													 Real halfSide, Int most, Coord2D *step, Real extraGap = 0.0f )
	{
		step->x = 0.0f;
		step->y = 0.0f;
		const Real length = (Real)sqrt( dx * dx + dy * dy );

		Int count = 1;
		if( length > 0.0f )
		{
			Real span = placementTouchDistance( dx / length, dy / length, headingCos, headingSin,
																					2.0f * halfFacing, 2.0f * halfSide );
			if( extraGap > 0.0f )
				span += extraGap;
			if( span < 0.01f )
				span = 0.01f;

			// one gap per step, and a drag always has the piece under the cursor as well as the anchor.
			// the 0.001 covers a quotient that lands a hair under a whole number: the drag and the
			// touch are the same geometry, and a table sine leaves it at 1.999
			Int gaps = REAL_TO_INT_FLOOR( length / span + 0.001f );
			if( gaps < 1 )
				gaps = 1;
			count = gaps + 1;
		}

		if( count > most )
			count = most;
		if( count < 1 )
			count = 1;

		if( count >= 2 )
		{
			step->x = dx / (Real)( count - 1 );
			step->y = dy / (Real)( count - 1 );
		}
		return count;
	}

	/** An alt-dragged grid, the row's twin in two directions.  The anchor and a cursor 'dx'/'dy'
		* away are opposite corners of a rectangle squared up with the structure's heading, and the
		* pieces fill it line by line: 'perLine' of them stand along the heading 'alongStep' apart, and
		* the lines stand 'acrossStep' apart.  Piece i is alongStep * ( i % perLine ) + acrossStep *
		* ( i / perLine ) from the anchor, which is the order they are drawn and ordered in.  Each
		* side packs the way the row does, flush through the face it meets plus 'extraGap', with the
		* slack spread so the far corner keeps a piece.  The one difference: a side shorter than a
		* step holds one piece, not two, or a drag that is nearly a line would come out as two lines
		* lying on each other.  'most' caps the total, so the last line can come up short, and the
		* lines are counted from what it leaves: five pieces three to a line are two lines, the
		* second on the far side.  Never fewer than one.  Inline and static for the same test. */
	static Int placementGrid( Real dx, Real dy, Real headingCos, Real headingSin, Real halfFacing,
														Real halfSide, Int most, Coord2D *alongStep, Coord2D *acrossStep,
														Int *perLine, Real extraGap = 0.0f )
	{
		alongStep->x = 0.0f;
		alongStep->y = 0.0f;
		acrossStep->x = 0.0f;
		acrossStep->y = 0.0f;

		// the drag, measured along the heading and across it
		const Real along = dx * headingCos + dy * headingSin;
		const Real across = dy * headingCos - dx * headingSin;

		const Real gap = extraGap > 0.0f ? extraGap : 0.0f;
		const Real alongSpan = placementTouchDistance( headingCos, headingSin, headingCos, headingSin,
																									 2.0f * halfFacing, 2.0f * halfSide ) + gap;
		const Real acrossSpan = placementTouchDistance( -headingSin, headingCos, headingCos, headingSin,
																										2.0f * halfFacing, 2.0f * halfSide ) + gap;

		if( most < 1 )
			most = 1;

		// the same hair under a whole number as the row's
		Int count = REAL_TO_INT_FLOOR( (Real)fabs( along ) / alongSpan + 0.001f ) + 1;
		if( count > most )
			count = most;

		Int lines = REAL_TO_INT_FLOOR( (Real)fabs( across ) / acrossSpan + 0.001f ) + 1;
		const Int linesPaidFor = ( most + count - 1 ) / count;
		if( lines > linesPaidFor )
			lines = linesPaidFor;

		if( count >= 2 )
		{
			alongStep->x = along * headingCos / (Real)( count - 1 );
			alongStep->y = along * headingSin / (Real)( count - 1 );
		}
		if( lines >= 2 )
		{
			acrossStep->x = -across * headingSin / (Real)( lines - 1 );
			acrossStep->y = across * headingCos / (Real)( lines - 1 );
		}

		*perLine = count;
		const Int total = count * lines;
		return total > most ? most : total;
	}

	/// would dragging the anchor lay pieces of the pending structure, a row or a grid, rather than aim one?
	Bool placesRow( void );
	/// of those two, the grid: alt is held.  Alt with shift is the grid as well
	Bool placesGrid( void ) const;
	/// wheel notches while that row is being drawn: one grid square of gap a notch, never below flush
	void adjustPlacementRowGap( Real spin );
	/// the centres of that row from 'start' toward 'end', or of the grid between those two corners,
	/// as many as MaxLineBuildObjects and the money allow
	void computePlacementRow( const ThingTemplate *what, Real angle, const Coord3D *start,
														const Coord3D *end, std::vector<Coord3D> *positions ) const;

	/** NudgeBuildPlacement: when the spot under the cursor is blocked, slide the structure to the
		* nearest one it does fit and show it there, so the ghost answers "here, then" instead of just
		* going red.  Moves 'world' and returns TRUE if it found somewhere; leaves it alone otherwise. */
	virtual Bool nudgePlacementToLegal( Coord3D *world, const ThingTemplate *what, Real angle,
																			Object *builderObject ) const;

	/** What the ghost asks of a spot before it goes green.  One place, because the nudge search has
		* to ask exactly the same question or it would offer a spot the ghost then paints red. */
	static UnsignedInt placementCheckOptions( void );

	//
	// Structures ordered but not standing yet.  A build order is a message: it crosses the network
	// and only becomes an object on the ground several frames later, and until it does the spot it
	// was ordered on is empty ground to every check that decides whether the next click is legal.
	// Holding shift on a laggy link put two structures on the same square that way, and the ghost
	// stayed green over a building that was already paid for.  So the client remembers its own
	// orders for a couple of seconds and treats them as standing.
	//
	// It is a client-side courtesy, not the rule: the rule is re-asked on the logic side, where the
	// first structure really does exist by the time the second order arrives.
	//
	enum { PENDING_PLACEMENTS = 64 };						///< orders in flight at once, a whole shift-dragged row among them; the oldest is overwritten
	enum { PENDING_PLACEMENT_FRAMES = LOGICFRAMES_PER_SECOND * 2 };			///< logic frames one is remembered for - two seconds,
																							///  comfortably longer than any network delay

	/// the ground a structure stands on: its box with the bib, turned to its heading
	struct PlacementBox
	{
		Real x, y;							///< centre
		Real c, s;							///< cos and sin of the heading
		Real halfMajor;					///< half-length along the heading, bib included
		Real halfMinor;					///< half-width across it, bib included
	};

	/// the ground a structure of this template put down here would stand on
	static void placementFootprint( const ThingTemplate *what, const Coord3D *world, Real angle,
																	PlacementBox *footprint );

	/** Do two of those share any ground?  The same turned boxes the logic's own clearance check
		* compares, so a structure the logic would take next to one still on its way is not refused
		* here: a box around a turned box is far bigger than the box, and two diagonal buildings
		* clicked side by side used to find their neighbour in the way and slide off it.  Edge to edge
		* is not sharing - structures are built flush.  Separating axes, the four sides of the two. */
	static Bool footprintsOverlap( const PlacementBox *a, const PlacementBox *b )
	{
		const Real slack = 0.01f;		// edges this close are touching, not crossing
		const Real dx = b->x - a->x;
		const Real dy = b->y - a->y;
		const PlacementBox *boxes[ 2 ] = { a, b };

		for( Int k = 0; k < 2; k++ )
		{
			const Real axes[ 2 ][ 2 ] = { { boxes[ k ]->c, boxes[ k ]->s }, { -boxes[ k ]->s, boxes[ k ]->c } };
			for( Int i = 0; i < 2; i++ )
			{
				const Real ax = axes[ i ][ 0 ];
				const Real ay = axes[ i ][ 1 ];
				const Real reachA = a->halfMajor * (Real)fabs( ax * a->c + ay * a->s ) +
														a->halfMinor * (Real)fabs( ay * a->c - ax * a->s );
				const Real reachB = b->halfMajor * (Real)fabs( ax * b->c + ay * b->s ) +
														b->halfMinor * (Real)fabs( ay * b->c - ax * b->s );
				if( (Real)fabs( ax * dx + ay * dy ) >= reachA + reachB - slack )
					return FALSE;
			}
		}
		return TRUE;
	}

	enum { PLACEMENT_FLUSH_REACH = 15 };	///< world units of gap a structure is pulled across onto its neighbour - a cell and a half

	/** Put 'mine' flush against 'neighbour' when that is plainly what the click meant: squared up
		* with it (the same heading, or a quarter or half turn off), and either sinking into it by less
		* than half the touching distance or short of touching by less than PLACEMENT_FLUSH_REACH.  It
		* slides along the line from the neighbour's centre to its own, keeping the direction the player
		* stepped in, until the two stand touching: a row clicked out sideways past diagonal buildings
		* nests them corner into corner and stays a line, where pushing each one off the face it met
		* sent every other building up or down.  A step within 15 degrees of an eighth of a turn, on
		* the map or on the neighbour's own heading, is put on that eighth, so the row comes out
		* straight.  This is the gap the build grid cannot close by itself: a structure standing
		* diagonal to the grid touches its neighbour at a spot that is no cell's centre.  Returns
		* whether it moved.  Inline and static so a test can reach it without linking the whole
		* in-game UI. */
	static Bool flushAgainst( const PlacementBox *neighbour, PlacementBox *mine )
	{
		const Real squared = 0.01f;				// how far off a quarter turn two headings may be and still line up
		const Real diagonal = 0.70710678f;
		const Real lineUp = 0.96592583f;		// cos 15 degrees
		const Real reach = (Real)PLACEMENT_FLUSH_REACH;
		const Real eighths[ 8 ][ 2 ] = { { 1.0f, 0.0f }, { diagonal, diagonal }, { 0.0f, 1.0f },
																		 { -diagonal, diagonal }, { -1.0f, 0.0f }, { -diagonal, -diagonal },
																		 { 0.0f, -1.0f }, { diagonal, -diagonal } };

		const Real turnCos = mine->c * neighbour->c + mine->s * neighbour->s;
		const Real turnSin = mine->s * neighbour->c - mine->c * neighbour->s;
		if( fabs( turnSin * turnCos ) > squared )
			return FALSE;

		const Real dx = mine->x - neighbour->x;
		const Real dy = mine->y - neighbour->y;
		const Real length = (Real)sqrt( dx * dx + dy * dy );
		if( length < 1.0f )
			return FALSE;

		Real ux = dx / length;
		Real uy = dy / length;
		Real closest = lineUp;
		for( Int k = 0; k < 8; k++ )
		{
			const Real onMap[ 2 ] = { eighths[ k ][ 0 ], eighths[ k ][ 1 ] };
			const Real onNeighbour[ 2 ] = { eighths[ k ][ 0 ] * neighbour->c - eighths[ k ][ 1 ] * neighbour->s,
																			eighths[ k ][ 0 ] * neighbour->s + eighths[ k ][ 1 ] * neighbour->c };
			const Real *candidates[ 2 ] = { onMap, onNeighbour };
			for( Int i = 0; i < 2; i++ )
			{
				const Real cosine = ( candidates[ i ][ 0 ] * dx + candidates[ i ][ 1 ] * dy ) / length;
				if( cosine > closest )
				{
					closest = cosine;
					ux = candidates[ i ][ 0 ];
					uy = candidates[ i ][ 1 ];
				}
			}
		}

		// mine's half-sizes along the neighbour's own two axes
		const Real mineAlong = (Real)fabs( turnCos ) * mine->halfMajor + (Real)fabs( turnSin ) * mine->halfMinor;
		const Real mineAcross = (Real)fabs( turnSin ) * mine->halfMajor + (Real)fabs( turnCos ) * mine->halfMinor;
		const Real touch = placementTouchDistance( ux, uy, neighbour->c, neighbour->s,
																							 neighbour->halfMajor + mineAlong,
																							 neighbour->halfMinor + mineAcross );

		const Real gap = dx * ux + dy * uy - touch;
		if( gap >= reach || gap <= -0.5f * touch )
			return FALSE;

		mine->x = neighbour->x + ux * touch;
		mine->y = neighbour->y + uy * touch;
		return TRUE;
	}

	/** Pull the structure being placed flush against the nearest structure beside it, standing or
		* ordered and still on its way (see flushAgainst).  Walls tile from their own line and are
		* left alone. */
	void snapPlacementToNeighbour( Coord3D *world, const ThingTemplate *what, Real angle ) const;

	/// remember a structure just ordered here, so the next click can see it
	void recordPendingPlacement( const ThingTemplate *what, const Coord3D *world, Real angle );
	/// drop the lot - a new game is not the old one's orders
	void forgetPendingPlacements( void );
	/// would a structure here land on one of those?
	Bool overlapsPendingPlacement( const Coord3D *world, const ThingTemplate *what, Real angle ) const;
	enum { PLACEMENT_NUDGE_PATHFINDS = 12 };						///< unreachable candidates tolerated before the search gives up

	/** The offsets that search tries: every cell of a square this many rings across, ordered by
		* true distance so that "the first one that fits" is "the nearest one that fits".  Ring by
		* ring would not be that ordering - from three rings out, a ring's own diagonal (1.41 cells)
		* is farther than the next ring's straight neighbour - and sampling only the eight compass
		* directions, as this first did, walks straight past the gap one cell to the side of them.
		* The table is built and sorted once, on the first placement of the session.  Inline and
		* static so a test can check the ordering without linking the whole in-game UI. */
	enum { PLACEMENT_NUDGE_RINGS = 10 };								///< how far out to look, in grid cells
	enum { PLACEMENT_NUDGE_SPAN = 2 * PLACEMENT_NUDGE_RINGS + 1 };
	enum { PLACEMENT_NUDGE_TRIES = PLACEMENT_NUDGE_SPAN * PLACEMENT_NUDGE_SPAN - 1 };
	static void placementNudgeOffset( Int index, Real step, Real *dx, Real *dy )
	{
		static Int cellX[ PLACEMENT_NUDGE_TRIES ];
		static Int cellY[ PLACEMENT_NUDGE_TRIES ];
		static Bool ordered = FALSE;

		if( ordered == FALSE )
		{
			Int n = 0;
			for( Int y = -PLACEMENT_NUDGE_RINGS; y <= PLACEMENT_NUDGE_RINGS; y++ )
				for( Int x = -PLACEMENT_NUDGE_RINGS; x <= PLACEMENT_NUDGE_RINGS; x++ )
					if( x != 0 || y != 0 )		// the spot the player is pointing at is the one that failed
					{
						cellX[ n ] = x;
						cellY[ n ] = y;
						n++;
					}

			// insertion sort by distance; it runs once for the whole session, and being in distance
			// order is the entire contract of this list
			for( Int i = 1; i < PLACEMENT_NUDGE_TRIES; i++ )
			{
				const Int keyX = cellX[ i ];
				const Int keyY = cellY[ i ];
				const Int keyD = keyX * keyX + keyY * keyY;
				Int j = i - 1;

				while( j >= 0 && cellX[ j ] * cellX[ j ] + cellY[ j ] * cellY[ j ] > keyD )
				{
					cellX[ j + 1 ] = cellX[ j ];
					cellY[ j + 1 ] = cellY[ j ];
					j--;
				}
				cellX[ j + 1 ] = keyX;
				cellY[ j + 1 ] = keyY;
			}

			ordered = TRUE;
		}

		*dx = cellX[ index ] * step;
		*dy = cellY[ index ] * step;
	}
	virtual void setPlacementAngle( Real angle );											///< aim the structure on the cursor, and keep that heading for the next one
	virtual Bool rotatePendingPlacement( Int steps );									///< turn the structure on the cursor by 45 degrees a step

	// Drawable selection mechanisms
	virtual void selectDrawable( Drawable *draw );					///< Mark given Drawable as "selected"
	virtual void deselectDrawable( Drawable *draw );				///< Clear "selected" status from Drawable
	virtual void deselectAllDrawables( void );							///< Clear the "select" flag from all drawables
	void holdSelectionThroughTunnel( Drawable *draw );			///< a selected unit going down a tunnel on its own is selected again when it comes up
	void restoreSelectionAfterTunnel( Drawable *draw );			///< reselect a unit held by holdSelectionThroughTunnel
	virtual Int getSelectCount( void ) { LOGIC_CENSUS( CENSUS_CLIENT_READ ); return m_selectCount; }		///< Get count of currently selected drawables
	virtual Int getMaxSelectCount( void ) { return m_maxSelectCount; }	///< Get the max number of selected drawables
	virtual UnsignedInt getFrameSelectionChanged( void ) { LOGIC_CENSUS( CENSUS_CLIENT_READ ); return m_frameSelectionChanged; }	///< Get the max number of selected drawables
	virtual const DrawableList *getAllSelectedDrawables( void ) const;	///< Return the list of all the currently selected Drawable IDs.
	virtual const DrawableList *getAllSelectedLocalDrawables( void );		///< Return the list of all the currently selected Drawable IDs owned by the current player.
	virtual Drawable *getFirstSelectedDrawable( void );							///< get the first selected drawable (if any)
	virtual DrawableID getSoloNexusSelectedDrawableID( void ) { return m_soloNexusSelectedDrawableID; }  ///< Return the one drawable of the nexus if only 1 angry mob is selected 
	virtual Bool isDrawableSelected( DrawableID idToCheck ) const;	///< Return true if the selected ID is in the drawable list
	virtual Bool isAnySelectedKindOf( KindOfType kindOf ) const;		///< is any selected object a kind of 
	virtual Bool isAllSelectedKindOf( KindOfType kindOf ) const;		///< are all selected objects a kind of

	virtual void setRadiusCursor(RadiusCursorType r, const SpecialPowerTemplate* sp, WeaponSlotType wslot);
	virtual void setRadiusCursorNone() { setRadiusCursor(RADIUSCURSOR_NONE, NULL, PRIMARY_WEAPON); }

	virtual void setInputEnabled( Bool enable );										///< Set the input enabled or disabled
	virtual void clearModifierModes( void );												///< forget every mode a held key puts us in (see the definition)
	virtual Bool getInputEnabled( void ) { LOGIC_CENSUS( CENSUS_CLIENT_READ ); return m_inputEnabled; }	///< Get the current input status

	virtual void disregardDrawable( Drawable *draw );				///< Drawable is being destroyed, clean up any UI elements associated with it

	virtual void preDraw( void );														///< Logic which needs to occur before the UI renders
	virtual void draw( void ) = 0;													///< Render the in-game user interface
	virtual void recordDraw( void ) { }											///< what draw asks of logic, at the sync point (W3DInGameUI)
	virtual void postDraw( void );													///< Logic which needs to occur after the UI renders
	/// -directorrecord's broadcast over the director's picture: a score bar along the top with every
	/// player's name, side, cash and army and the match clock, the armies' tug of war under it, and a
	/// label in each pane of a split.  Pane 0's draw alone; the recording takes these pixels from it
	void drawDirectorBroadcast( void );
	/// the opening's plate over the command centre of the player in the pane being drawn, in every
	/// pane's own draw
	void drawDirectorIntroPlate( void );

	//
	// The production list on the left: one cameo per item the local player has coming, queued in a
	// factory or going up on the map, soonest finished first, in one row.  Past PRODUCTION_LIST_CELLS a
	// tab sticking up from the tray says "+N" for the rest.  The superweapon cameos in the top right,
	// SUPERWEAPON_LIST_COLUMNS to a row, stop at SUPERWEAPON_LIST_CELLS with a tab hanging below.
	//
	/// one row, over the radar's piece of the bar: four cells and the tray's edge are 316 wide at 1080,
	/// inside the side bars' 345 wide radar box
	enum { PRODUCTION_LIST_COLUMNS = 4, PRODUCTION_LIST_CELLS = 4 };
	enum { SUPERWEAPON_LIST_COLUMNS = 6, SUPERWEAPON_LIST_CELLS = 12 };	///< two rows at most
	struct ProductionListSlot
	{
		ObjectID			producer;						///< the building it is queued on, or the building going up
		Int						typeKey;						///< what the item is, so a run of the same thing is one cell
		Bool					isUpgrade;
		Int						remaining;					///< logic frames until it is done, waiting time ahead of it included
		Int						quantity;						///< how many of the same thing the cell stands for
		Int						percent;						///< of its own build done; 0 while it waits behind another
		const Image *	cameo;							///< its picture, NULL for a template that carries none
		Int						id;									///< what a cancel names: the run's newest production ID, or the upgrade's key
		Bool					isStructure;				///< going up on the map: a click looks at it, nothing cancels it
	};

	/// A click on a production or superweapon cell, at `mouse` in screen pixels: TRUE when a cell took it.
	/// A production cell takes the camera to its building, or with `cancel` takes the newest of its
	/// items off that building's queue; one of your own superweapons takes the camera to it
	Bool handleHudCellClick( const ICoord2D *mouse, Bool cancel );

	/// A press on the spectator's rack, at `mouse` in screen pixels: TRUE when the rack is under it,
	/// whatever button it was; only `act`, the left button, works its controls
	Bool handleSpectatorRackClick( const ICoord2D *mouse, Bool act );
	/// The wheel over the rack: TRUE when the rack is under it; over the standings' head it steps the stat
	Bool handleSpectatorRackWheel( const ICoord2D *mouse, Real notches );
	/// a command bar key while watching: TRUE when it picked a stat, Q W E R T Y A as shipped
	Bool pickSpectatorStat( Int commandSlot );
	/// a raw Q W E R T Y A press or release while watching: TRUE when the rack's stat keys took it
	Bool pickSpectatorStatKey( Int key, Int keyState );

	/// Ingame video playback 
	virtual void playMovie( const AsciiString& movieName );
	virtual void stopMovie( void );
	virtual VideoBuffer* videoBuffer( void );

	/// Ingame cameo video playback 
	virtual void playCameoMovie( const AsciiString& movieName );
	virtual void stopCameoMovie( void );
	virtual VideoBuffer* cameoVideoBuffer( void );

  // mouse over information	
	virtual DrawableID getMousedOverDrawableID( void ) const;	///< Get drawble ID of drawable under cursor
	
	/// Set the ingame flag as to if we have the Quit menu up or not
	virtual void setQuitMenuVisible( Bool t ) { m_isQuitMenuVisible = t; }
	virtual Bool isQuitMenuVisible( void ) const { return m_isQuitMenuVisible; }

	// INI file parsing
	virtual const FieldParse* getFieldParse( void ) const { return s_fieldParseTable; }


	//Provides a global way to determine whether or not we can issue orders to what we have selected.
	Bool areSelectedObjectsControllable() const;
	//Wrapper function that includes any non-attack canSelectedObjectsXXX checks.
	Bool canSelectedObjectsNonAttackInteractWithObject( const Object *objectToInteractWith, SelectionRules rule ) const;
	//Wrapper function that checks a specific action.
	CanAttackResult getCanSelectedObjectsAttack( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking = FALSE ) const;
	Bool canSelectedObjectsDoAction( ActionType action, const Object *objectToInteractWith, SelectionRules rule, Bool additionalChecking = FALSE ) const;
	Bool canSelectedObjectsDoSpecialPower( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule, UnsignedInt commandOptions, Object* ignoreSelObj ) const;
	Bool canSelectedObjectsEffectivelyUseWeapon( const CommandButton *command, const Object *objectToInteractWith, const Coord3D *position, SelectionRules rule ) const;
	Bool canSelectedObjectsOverrideSpecialPowerDestination( const Coord3D *loc, SelectionRules rule, SpecialPowerType spType = SPECIAL_INVALID ) const;

	// Selection Methods
	virtual Int selectUnitsMatchingCurrentSelection();                        ///< selects matching units
	virtual Int selectMatchingAcrossScreen();                         ///< selects matching units across screen
	virtual Int selectMatchingAcrossMap();                            ///< selects matching units across map
	virtual Int selectMatchingAcrossRegion( IRegion2D *region );			// -1 = no locally-owned selection, 0+ = # of units selected

	virtual Int selectAllUnitsByType(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear);                
	virtual Int selectAllUnitsByTypeAcrossScreen(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear, Bool idleOnly);
	virtual Int selectAllUnitsByTypeAcrossMap(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear, Bool idleOnly);
	virtual Int selectAllUnitsByTypeAcrossRegion( IRegion2D *region, KindOfMaskType mustBeSet, KindOfMaskType mustBeClear, Bool idleOnly );	///< idleOnly: only isIdleCombatUnit
	
	virtual void buildRegion( const ICoord2D *anchor, const ICoord2D *dest, IRegion2D *region );  ///< builds a region around the specified coordinates

	virtual Bool getDisplayedMaxWarning( void ) { return m_displayedMaxWarning; }
	virtual void setDisplayedMaxWarning( Bool selected );

	// Floating Test Methods
	/// the text it added, for a caller that wants to hold it longer; NULL while icons are not drawn
	virtual FloatingTextData *addFloatingText(const UnicodeString& text,const Coord3D * pos, Color color);
	/// a smoke signal's mark laid on the ground in the sender's colour, there until its smoke is gone
	void addSignalMark( SignalKind kind, const Coord3D &pos, Color color, ParticleSystemID smoke );
	/// a signal button pressed: the next left click on the ground or the radar drops that signal there
	void armSignal( SignalKind kind ) { m_armedSignal = kind; }
	void disarmSignal( void ) { m_armedSignal = SIGNAL_KIND_COUNT; }
	Bool isSignalArmed( void ) const { return m_armedSignal != SIGNAL_KIND_COUNT; }
	/// sends the armed signal to `world` and disarms; FALSE, doing nothing, when none is armed
	Bool placeArmedSignal( const Coord3D &world );

	// Drawable caption stuff
	AsciiString	getDrawableCaptionFontName( void )	{ return m_drawableCaptionFont; }
	Int					getDrawableCaptionPointSize( void )	{ return m_drawableCaptionPointSize; }
	Bool				isDrawableCaptionBold( void )				{ return m_drawableCaptionBold; }
	Color				getDrawableCaptionColor( void )			{ return m_drawableCaptionColor; }

	Bool isClientQuiet( void ) const			{ return m_clientQuiet; }
	Bool isInWaypointMode( void ) const			{ return m_waypointMode; }
	Bool isInForceAttackMode( void ) const	{ return m_forceAttackMode; }
	Bool isInForceMoveToMode( void ) const	{ return m_forceMoveToMode; }
	Bool isInPreferSelectionMode( void ) const { return m_preferSelection; }

	void setClientQuiet( Bool enabled )  { m_clientQuiet = enabled; }
	void setWaypointMode( Bool enabled )		{ m_waypointMode = enabled; if( !enabled && m_orderKeyKeptByShift ) clearAttackMoveToMode(); }
	void setForceMoveMode( Bool enabled )		{ m_forceMoveToMode = enabled; }
	void setForceAttackMode( Bool enabled )		{ m_forceAttackMode = enabled; }
	void setPreferSelectionMode( Bool enabled )		{ m_preferSelection = enabled; }
	
	void toggleAttackMoveToMode( void )				{ m_attackMoveToMode = !m_attackMoveToMode; m_forceAttackArmed = FALSE; m_guardArmed = FALSE; m_moveArmed = FALSE; }
	Bool isInAttackMoveToMode( void ) const		{ return m_attackMoveToMode; }
	void clearAttackMoveToMode( void )				{ m_attackMoveToMode = FALSE; m_forceAttackArmed = FALSE; m_guardArmed = FALSE; m_moveArmed = FALSE; m_orderKeyKeptByShift = FALSE; }

	// an order click with one of the three keys armed spends the key, unless shift is down: then it
	// stays armed for the next click, so a row of targets is one key and a row of clicks, and it drops
	// when shift comes up
	void spendOrderKey( void )								{ if( m_waypointMode ) m_orderKeyKeptByShift = TRUE; else clearAttackMoveToMode(); }

	// the attack key arms force fire the way the attack move key arms an attack move: the next
	// order click shoots whatever is under it, ground included, and the mode drops again with the
	// same call that drops attack move
	void toggleForceAttackArmed( void )				{ m_forceAttackArmed = !m_forceAttackArmed; m_attackMoveToMode = FALSE; m_guardArmed = FALSE; m_moveArmed = FALSE; }
	Bool isForceAttackArmed( void ) const			{ return m_forceAttackArmed; }
	Bool isOrderKeyArmed( void ) const				{ return m_forceAttackArmed || m_attackMoveToMode || m_guardArmed || m_moveArmed; }	///< the next left click is an attack, an attack move, a guard or a move
	Bool isForceFireOn( void ) const;					///< the next order click force fires: the attack key armed it

	// and the guard key arms guard the same way: the next order click posts the selection on that
	// spot, or on that object, and a drag posts them along the line instead of stacking them all
	// on one point.  All the armed keys are one mode at a time
	void toggleGuardArmed( void )							{ m_guardArmed = !m_guardArmed; m_guardArmedMode = GUARDMODE_GUARD_WITHOUT_PURSUIT; m_attackMoveToMode = FALSE; m_forceAttackArmed = FALSE; m_moveArmed = FALSE; }
	Bool isGuardArmed( void ) const						{ return m_guardArmed; }
	GuardMode getGuardArmedMode( void ) const	{ return m_guardArmedMode; }	///< what the armed guard's click gives: the key's, or the EA button's own

	// While a guard is armed the wheel grows and shrinks the circle it will hold instead of zooming,
	// the guard marker's ring follows it and the order carries it.  0 until the wheel has touched
	// it: the click is then the retail guard, each unit on its own range
	void adjustArmedGuardRadius( Real notches );
	Real getArmedGuardRadius( void );

	/** One of the page's keys past attack, hold position and move, an OrderKeyExtra (ControlBar.h),
		* pressed with the mouse or its grid key: the stance key puts the selection on the other
		* stance from the first unit's */
	void pressOrderKey( Int key );

	// the move key arms the order a right click gives, for the left button: the next order click is
	// that move, and a left drag draws the formation line
	void toggleMoveArmed( void )							{ m_moveArmed = !m_moveArmed; m_attackMoveToMode = FALSE; m_forceAttackArmed = FALSE; m_guardArmed = FALSE; }
	Bool isMoveArmed( void ) const						{ return m_moveArmed; }
	Bool isLineOrderArmed( void ) const				{ return m_attackMoveToMode || m_guardArmed || m_moveArmed; }	///< a left drag draws a move, attack move or guard line; force fire's left drag is the attack circle
	Bool isForceLineHeld( void ) const;				///< the Force Attack hold with shift: a left drag draws the attack line, not the circle
	Bool isMoveLineHeld( void ) const;				///< shift alone over a selection that can walk: a left drag draws the move line, not the box
	Bool wantsDragLine( void ) const;					///< a drag of the order button now draws an order line
	Bool wantsDragCircle( void ) const;				///< a drag of the order button now draws the attack circle
	Bool isOrderDragButton( Bool rightButton ) const;	///< which button draws the line or circle wanted now: the left for an armed key or Ctrl, else the one that moves
	
	// zeroing the repeat clock makes the first quantized step happen on the very next update, so a
	// tap of the key is one eighth and a hold is one eighth every CAMERA_SNAP_REPEAT_MS.
	void setCameraRotateLeft( Bool set )		{ m_cameraRotatingLeft = set; m_cameraSnapRepeatMs = 0; }
	void setCameraRotateRight( Bool set )		{ m_cameraRotatingRight = set; m_cameraSnapRepeatMs = 0; }
	void setCameraZoomIn( Bool set )				{ m_cameraZoomingIn = set; }
	void setCameraZoomOut( Bool set )				{ m_cameraZoomingOut = set; }
  void setCameraTrackingDrawable( Bool set ) { m_cameraTrackingDrawable = set; }
	Bool isCameraRotatingLeft() const { return m_cameraRotatingLeft; }
	Bool isCameraRotatingRight() const { return m_cameraRotatingRight; }
	Bool isCameraZoomingIn() const { return m_cameraZoomingIn; }
	Bool isCameraZoomingOut() const { return m_cameraZoomingOut; }
  Bool isCameraTrackingDrawable() const { return m_cameraTrackingDrawable; }
	void resetCamera();

	virtual void addIdleWorker( Object *obj );
	void addIdleWorkerFor( Object *obj, Int playerIndex );	///< addIdleWorker with the owner read at the call
	virtual void removeIdleWorker( Object *obj, Int playerNumber );
	virtual Object *selectNextIdleWorker( void );	///< what it selected, NULL with no idle worker; never moves the camera
	Object *selectNextIdleUnit( KindOfMaskType mustBeSet, KindOfMaskType mustBeClear );	///< the same cycle for fighting units of those kinds standing with no order; what it selected, or NULL
	static Bool isIdleCombatUnit( const Object *obj );	///< the local player's fighting unit standing with no order
	/// the kinds a MSG_META_SELECT_NEXT_IDLE_* key cycles over: vehicles are ground vehicles only
	static void idleUnitKinds( GameMessage::Type t, KindOfMaskType *mustBeSet, KindOfMaskType *mustBeClear );
	void updateIdleUnitButtons( void );	///< the idle infantry, vehicle, aircraft and worker buttons lit while one of their kind stands idle

	virtual void recreateControlBar( void );
	virtual void notifyResolutionChange( void );

	virtual void disableTooltipsUntil(UnsignedInt frameNum);
	virtual void clearTooltipsDisabled();
	virtual Bool areTooltipsDisabled() const;

	Bool getDrawRMBScrollAnchor() const { return m_drawRMBScrollAnchor; }
	Bool getMoveRMBScrollAnchor() const { return m_moveRMBScrollAnchor; }

	void setDrawRMBScrollAnchor(Bool b) { m_drawRMBScrollAnchor = b; }
	void setMoveRMBScrollAnchor(Bool b) { m_moveRMBScrollAnchor = b; }

	// The right-drag camera scroll these two fields were named for is back on the right button.
	Bool shouldMoveScrollAnchor( void ) const { return m_moveRMBScrollAnchor; }

private:
	virtual void resetIdleWorker( void );

public:
	void registerWindowLayout(WindowLayout *layout); // register a layout for updates
	void unregisterWindowLayout(WindowLayout *layout); // stop updates for this layout

  void triggerDoubleClickAttackMoveGuardHint( void );
  

public:
	// World 2D animation methods
	void addWorldAnimation( Anim2DTemplate *animTemplate, 
													const Coord3D *pos,
													WorldAnimationOptions options,
													Real durationInSeconds,
													Real zRisePerSecond );

#if defined(_DEBUG) || defined(_INTERNAL)
	virtual void DEBUG_addFloatingText(const AsciiString& text,const Coord3D * pos, Color color);
#endif

protected:
	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

protected: 

	// ----------------------------------------------------------------------------------------------
	// Protected Types ------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	// mouse mode interface
	enum MouseMode 
	{
		MOUSEMODE_DEFAULT = 0,
		MOUSEMODE_BUILD_PLACE,
		MOUSEMODE_GUI_COMMAND,
		MOUSEMODE_MAX
	};

	struct MilitarySubtitleData
	{
		UnicodeString subtitle;										///< The complete subtitle to be drawn, each line is separated by L"\n"
		UnsignedInt index;												///< the current index that we are at through the sibtitle
		ICoord2D position;												///< Where on the screen the subtitle should be drawn
		DisplayString *displayStrings[MAX_SUBTITLE_LINES];	///< We'll only allow MAX_SUBTITLE_LINES worth of display strings
		UnsignedInt currentDisplayString;					///< contains the current display string we're on. (also lets us know the last display string allocated
		UnsignedInt lifetime;											///< the Lifetime of the Military Subtitle in frames
		Bool blockDrawn;													///< True if the block is drawn false if it's blank
		UnsignedInt blockBeginFrame;							///< The frame at which the block started it's current state
		ICoord2D blockPos;												///< where the upper left of the block should begin
		UnsignedInt incrementOnFrame;							///< if we're currently on a frame greater then this, increment our position
		UnsignedInt fadeStep;											///< retail frames past its lifetime when the fade was last applied
		Color color;														///< what color should we display the military subtitles
	};

	typedef std::list<Object *> ObjectList;
	typedef std::list<Object *>::iterator ObjectListIt;
	
	// ----------------------------------------------------------------------------------------------
	// Protected Methods ----------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	void destroyPlacementIcons( void );													///< Destroy placement icons
	void handleBuildPlacements( void );													///< handle updating of placement icons based on mouse pos
	void handleRadiusCursor();																	///< handle updating of "radius cursors" that follow the mouse pos
	/// frozen time runs the scripts with no new logic frame, so an overlay laid once a frame is laid every pass
	Bool isTimeFrozenWithoutTick( void ) const;

	void incrementSelectCount( void ) { ++m_selectCount; }			///< Increase by one the running total of "selected" drawables
	void decrementSelectCount( void ) { --m_selectCount; }			///< Decrease by one the running total of "selected" drawables
	virtual View *createView( void ) = 0;												///< Factory for Views
	void evaluateSoloNexus( Drawable *newlyAddedDrawable = NULL );

	void createControlBar( void );			///< create the control bar user interface
	void createReplayControl( void );		///< create the replay control window

	void setMouseCursor(Mouse::MouseCursor c);

	
	void addMessageText( const UnicodeString& formattedMessage );  ///< a plain message, a line of EA's list

	void updateFloatingText( void );						///< Update function to move our floating text
	void drawFloatingText( void );							///< Draw all our floating text
	void clearFloatingText( void );							///< clear the floating text list

public:
	/** The pathfinder's build grid under a pending structure.  Public and called from the terrain
		* renderer rather than from preDraw: it is geometry lying on the ground, and it has to go down
		* with the terrain so that everything drawn after the terrain covers it. */
	virtual void drawBuildGrid( void ) { }

	/** The wash inside the attack circle, on the ground for the same reason: the units being swept
		* up stand on top of it instead of being painted over. */
	virtual void drawAttackCircleFill( void ) { }

	/// the control socket's "spectator" verb: the camera, the followed player, the fog, and replay
	/// pause, seek and speed
	void runSpectatorAction( const std::string &action );
protected:

	void clearWorldAnimations( void );					///< delete all world animations
	void updateAndDrawWorldAnimations( void );	///< update and draw visible world animations

	SuperweaponInfo* findSWInfo(Int playerIndex, const AsciiString& powerName, ObjectID id, const SpecialPowerTemplate *powerTemplate);

	// ----------------------------------------------------------------------------------------------
	// Protected Data THAT IS SAVED/LOADED ----------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	Bool												m_superweaponHiddenByScript;
	Bool												m_inputEnabled;		/// sort of

	// ----------------------------------------------------------------------------------------------
	// Protected Data -------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	std::list<WindowLayout *>		m_windowLayouts;
	AsciiString									m_currentlyPlayingMovie;											///< Used to push updates to TheScriptEngine
	DrawableList								m_selectedDrawables;													///< A list of all selected drawables.

	// Classic's move rings, EA's own: where each move order landed and the client frame it did
	enum { MAX_MOVE_HINTS = 256 };
	struct MoveHintStruct
	{
		Coord3D pos;						///< World coords of destination point
		UnsignedInt frame;			///< frame the command was issued on
	};
	MoveHintStruct							m_moveHint[ MAX_MOVE_HINTS ];
	Int													m_nextMoveHint;
	DrawableList								m_selectedLocalDrawables;											///< A list of all selected drawables owned by the local player
	std::vector<ObjectID>				m_tunnelTripRiders;														///< selected units inside the tunnel network on a move order, selected again when they come out
	Bool												m_isDragSelecting;														///< If TRUE, an area selection is in progress
	IRegion2D										m_dragSelectRegion;														///< if isDragSelecting is TRUE, this contains select region
	Bool												m_isFormationDragging;												///< TRUE while a formation line is being drawn (fork)
	std::vector<ICoord2D>				m_formationDragPoints;												///< the traced curve, in pixels, first point is where it started
	Int													m_formationDragSpacing;												///< pixels a new point has to earn, doubled each time the curve fills up
	std::vector<OrderHint>			m_orderHints;																	///< who is going where, one per unit and queued point
	std::vector<OrderHint>			m_drawnOrderHints;														///< the same, units headed the same way bunched into one

	Bool												m_isAttackCircling;														///< TRUE while an attack circle is being dragged (fork)
	ICoord2D										m_attackCircleAnchor;													///< where the circle was started, in pixels
	ICoord2D										m_attackCircleCursor;													///< where the cursor is now, which is the rim
	Coord3D											m_attackCircleCenter;													///< the ground the press landed on, held while the camera moves

	void updateFormationHints( void );													///< recompute who goes where from the curve being drawn
	void updateOrderHints( void );															///< read the selection's own goals, once a frame
	void collectOrderHints( void );															///< one hint per selected unit and queued point
	void bunchOrderHints( void );																///< merge the hints of units going the same way
	void addOrderHint( OrderHint& hint, const std::vector<OrderHint>& previous );	///< keep a marker's age across the frame the list is rebuilt on
	Bool getHeldAircraftOrder( const Object *obj, OrderHintKind& kind, Coord3D& to, Bool& onObject ) const;	///< the order an aircraft is sitting on until it is airborne
	void addQueuedOrderTail( OrderHint& hint, const OrderChain& chain, const std::vector<OrderHint>& previous );	///< every order still owed, drawn on from where the hint leaves off
	Bool getQueuedOrderHint( const QueuedOrder& order, OrderHint& hint ) const;	///< the marker a queued order draws, FALSE for none
	void numberOrderHints( void );															///< a unit with more than one place to go numbers them
	void markOrderHints( void );																///< flag or joint on each hint's end
	Bool isHiddenByShroud( const Object *obj ) const;						///< is the shroud over this, for the player at this machine
	Bool												m_displayedMaxWarning;                        ///< keeps the warning from being shown over and over
	const CommandButton *				m_pendingGUICommand;										///< GUI command that needs additional interaction from the user
	BuildProgress								m_buildProgress[ MAX_BUILD_PROGRESS ];	///< progress for building units
	const ThingTemplate *				m_pendingPlaceType;											///< type of built thing we're trying to place
	ObjectID										m_pendingPlaceSourceObjectID;						///< source object of the thing constructing the item
	Bool										m_preventLeftClickDeselectionInAlternateMouseModeForOneClick;
	Drawable **									m_placeIcon;														///< array for drawables to appear at the cursor when building in the world
	Bool												m_placeAnchorInProgress;								///< is place angle interface for placement active
	ICoord2D										m_placeAnchorStart;											///< place angle anchor start
	ICoord2D										m_placeAnchorEnd;												///< place angle anchor end
	Real												m_placeAngleOffset;											///< wheel-chosen heading, added to every structure's own view angle
	const ThingTemplate *				m_placeAngleType;												///< the structure that heading was chosen for; another type starts square again
	Bool												m_placementLegal;												///< last legality verdict for the spot under the structure being placed
	Coord3D											m_placementNudge;												///< how far the last legality check had to slide the structure to make it fit
	Real												m_placementRowGap;											///< extra world units between a shift-dragged row, on top of the buildings touching
	Real												m_placementRowGapWheel;									///< fractional notches not yet worth a whole grid square

	/// a structure ordered here, and the logic frame it was ordered on - see recordPendingPlacement
	struct PendingPlacement
	{
		PlacementBox	footprint;
		UnsignedInt		frame;				///< 0 for a slot nothing has been written to yet
	};
	PendingPlacement						m_pendingPlacement[ PENDING_PLACEMENTS ];
	Int													m_pendingPlacementAt;										///< where the next order is written, round the ring

	Int													m_selectCount;													///< Number of objects currently "selected"
	Int													m_maxSelectCount;												///< Max number of objects to select
	UnsignedInt									m_frameSelectionChanged;								///< Frame when the selection last changed.

  Int                         m_duringDoubleClickAttackMoveGuardHintTimer; ///< Frames left to draw the doubleClickFeedbackTimer 
  Coord3D                     m_duringDoubleClickAttackMoveGuardHintStashedPosition; 
  
	// Video playback data
	VideoBuffer*								m_videoBuffer;			///< video playback buffer
	VideoStreamInterface*				m_videoStream;			///< Video stream;

	// Video playback data
	VideoBuffer*								m_cameoVideoBuffer;///< video playback buffer
	VideoStreamInterface*				m_cameoVideoStream;///< Video stream;

	// superweapon timer data
	SuperweaponMap							m_superweapons[MAX_PLAYER_COUNT];
	enum { HUD_OVERLAY_POINT_SIZE = 9 };	///< small: this sits over the battlefield, not in a panel
	enum { HUD_CLOCK_POINT_SIZE = 7 };		///< smaller still, and bold: the clock/rate plate is a glance, not a read

	void drawHudOverlay( void );					///< the top right info strip (ShowNetBox), peace time at its end
	Int hudStripBottom( void ) const;			///< first screen row below that strip, 0 while it is not drawn
	void drawKickVoteBanner( void );			///< the open vote kick, top middle, while there is one
	void drawProductionList( void );			///< the production list, cameos on the left above the control bar
	DisplayString *cellString( DisplayString *&string, const UnicodeString &text );	///< a cell's kept readout string
	void drawCellSeconds( DisplayString *&string, Int seconds, const IRegion2D &cameo, const Image *plate );	///< "45s" in a cameo's corner

	SignalKind									m_armedSignal;						///< the signal the next click drops; SIGNAL_KIND_COUNT for none
	struct SignalMark
	{
		Shadow *decal;
		ParticleSystemID smoke;			///< the signal's smoke, which the mark lasts exactly as long as
	};
	std::vector< SignalMark >		m_signalMarks;
	void updateSignalMarks( void );
	void clearSignalMarks( void );

	//
	// The message list: EA's column of six lines in the top left corner, each in its own colour,
	// fading out from the moment it is written.
	//
	enum { CLASSIC_MESSAGES = 6 };
	struct ClassicMessage
	{
		DisplayString *text;
		UnsignedInt frame;					///< the logic frame it was written on
		Color color;
	};
	ClassicMessage							m_classicMessages[ CLASSIC_MESSAGES ];	///< newest first
	UnsignedInt									m_classicMessageFadeFrame;			///< the last retail 30 Hz frame the fade was stepped on
	void addClassicMessage( const UnicodeString &text, const Color *color );
	void fadeClassicMessages( void );
	void drawClassicMessages( void );
	void freeClassicMessages( void );
	void watchDozers( void );
	UnsignedInt									m_dozerCheckFrame;				///< the logic frame watchDozers last looked on
	Bool												m_hadDozer[ MAX_PLAYER_COUNT ];	///< that player had a dozer or worker then

	DisplayString *							m_hudDisplayString;			///< the info strip's readout (clock / rates)
	DisplayString *							m_peaceTimeDisplayString;	///< the info strip's red peace time segment
	/// the broadcast's string for what key names, in font at points, holding text: one string per
	/// thing written, so a text is laid out again only when it changes
	DisplayString *broadcastText( const std::string &key, const UnicodeString &text, const char *font, Int points, Bool bold );
	/// broadcastText of what the broadcast calls player, at points, his own name cut with an ellipsis
	/// past widest (both for a 720 line picture)
	DisplayString *broadcastNameText( const std::string &key, Player *player, Int points, Real widest );
	std::map< std::string, DisplayString * > m_broadcastTexts;
	/// the score bar laid out for players, under the clock's tab, team letters taken from named
	void layOutBroadcast( const std::vector< SpectatorStats > &players, const std::vector< SpectatorStats > &named,
		const std::string &tag, Int clockBox, Int clockBoxHeight, BroadcastLayout &layout );
	Int													m_lastMoneyDisplayed;		///< so the money gadget is only written when the amount changes
	UnsignedInt									m_hudDrawCount;					///< rendered frames counted by drawHudOverlay itself
	UnsignedInt									m_hudLastSampleFrame;		///< m_hudDrawCount the fps sample was last refreshed on
	UnsignedInt									m_hudLastSampleMs;			///< wall clock of that sample
	RateReading									m_hudFps;								///< render rate
	UnsignedInt									m_hudLastSampleLogicFrame;	///< logic frame at that same sample
	RateReading									m_hudLogicHz;						///< logic frames actually simulated per real second
	UnsignedInt									m_hudRealClockBaseMs;		///< wall clock the two elapsed-time readouts were aligned at
	UnsignedInt									m_hudLastDrawMs;				///< wall clock of the previous overlay draw, so a pause can be taken back out of it
	// A script time freeze stops the logic clock, and the military subtitle's counters are
	// logic frames, so they have to be stepped by hand while it lasts.  These two turn the
	// wall clock into that step without drift: every update works out how many 30Hz frames
	// have passed since the freeze began and applies only the ones not applied yet.
	UnsignedInt									m_subtitleFreezeStartMs;	///< wall clock the current freeze started on, 0 = not frozen
	UnsignedInt									m_subtitleFreezeSteps;		///< frames already handed to the subtitle during it

	//
	// The production list: everything the local player has coming, gathered once a logic frame, and
	// kept strings per cell, its seconds at 2n and its "xN" at 2n + 1, so a readout is laid out again
	// only when its text changes.  The superweapon cameos keep a seconds string each; each tray's
	// "+N" tab has one of its own.
	//
	ProductionListSlot					m_productionList[ PRODUCTION_LIST_CELLS ];
	Int													m_productionListCount;		///< cells filled
	Int													m_productionListTotal;		///< items coming, shown or not
	DisplayString *							m_productionListText[ 2 * PRODUCTION_LIST_CELLS ];
	DisplayString *							m_productionListMoreText;
	DisplayString *							m_superweaponCellText[ SUPERWEAPON_LIST_CELLS ];
	DisplayString *							m_superweaponMoreText;
	// where the last draw put the cells and the tabs, for handleHudCellClick; a tab not drawn is empty
	IRegion2D										m_productionListPlace[ PRODUCTION_LIST_CELLS ];
	Int													m_productionListCells;		///< cells drawn
	IRegion2D										m_productionListTab;
	IRegion2D										m_superweaponPlace[ SUPERWEAPON_LIST_CELLS ];
	ObjectID										m_superweaponJump[ SUPERWEAPON_LIST_CELLS ];	///< the building a click looks at, INVALID_ID for none
	Int													m_superweaponCells;				///< cells drawn
	IRegion2D										m_superweaponTab;
	UnsignedInt									m_hudCellPass;						///< client passes, counted by update()
	UnsignedInt									m_hudCellDrawnPass;				///< m_hudCellPass when postDraw last laid the cells out

	//
	// The spectator's rack (drawSpectatorRack), drawers hung off the right edge under the superweapon
	// tray while the local player watches: the camera, the fog and the armies drawer, then every
	// player ranked by the stat its keys pick.  Where the last draw put it, for the clicks.
	//
	enum { RACK_ROWS = 8, RACK_ARMY_UNITS = 6 };
	enum
	{
		RACK_TEXT_WATCH, RACK_TEXT_STANDINGS, RACK_TEXT_STOP, RACK_TEXT_FOG = RACK_TEXT_STOP + 3, RACK_TEXT_ARMIES,
		RACK_TEXT_KEY, RACK_TEXT_STAT = RACK_TEXT_KEY + 7, RACK_TEXT_HINT, RACK_TEXT_PLACE_HEAD, RACK_TEXT_PLAYER_HEAD,
		RACK_TEXT_VALUE_HEAD, RACK_TEXT_PLACE, RACK_TEXT_NAME = RACK_TEXT_PLACE + RACK_ROWS,
		RACK_TEXT_VALUE = RACK_TEXT_NAME + RACK_ROWS, RACK_TEXT_UNITS = RACK_TEXT_VALUE + RACK_ROWS,
		RACK_TEXT_COUNT = RACK_TEXT_UNITS + RACK_ROWS * RACK_ARMY_UNITS
	};
	void drawSpectatorRack( Int plateTop );	///< plateTop: the watch drawer's plate, 8 under the superweapon tray's foot
	/// the kept string `slot` holding text in the bold face at px pixels at 1080, a pixel smaller at a time
	/// down to 9 until it is no wider than room (0: any width)
	DisplayString *rackText( Int slot, const UnicodeString &text, Int px, Int room );
	Bool												m_rackShown;							///< the last postDraw drew it
	Int													m_rackX, m_rackY;					///< its pieces' left and the watch drawer's top
	Int													m_rackRows;
	Int													m_rackRowPlayer[ RACK_ROWS ];	///< the player index on each row, top down
	Int													m_rackStat;								///< the picked SPECTATOR_STATS index
	Bool												m_rackArmies;							///< the armies drawer is out
	DisplayString *							m_rackText[ RACK_TEXT_COUNT ];
	Int													m_rackTextPx[ RACK_TEXT_COUNT ];		///< the size and room each was laid out for
	Int													m_rackTextRoom[ RACK_TEXT_COUNT ];

	Coord2D											m_superweaponPosition;
	Real												m_superweaponFlashDuration;
	
	// superweapon timer font info
	AsciiString									m_superweaponNormalFont;
	Int													m_superweaponNormalPointSize;
	Bool												m_superweaponNormalBold;
	AsciiString									m_superweaponReadyFont;
	Int													m_superweaponReadyPointSize;
	Bool												m_superweaponReadyBold;

	Int													m_superweaponLastFlashFrame;										///< for flashing the text when the weapon is ready
	Color												m_superweaponFlashColor;
	Bool												m_superweaponUsedFlashColor;

	NamedTimerMap								m_namedTimers;
	Coord2D											m_namedTimerPosition;
	Real												m_namedTimerFlashDuration;
	Int													m_namedTimerLastFlashFrame;
	Color												m_namedTimerFlashColor;
	Bool												m_namedTimerUsedFlashColor;
	Bool												m_showNamedTimers;

	AsciiString									m_namedTimerNormalFont;
	Int													m_namedTimerNormalPointSize;
	Bool												m_namedTimerNormalBold;
	Color												m_namedTimerNormalColor;
	AsciiString									m_namedTimerReadyFont;
	Int													m_namedTimerReadyPointSize;
	Bool												m_namedTimerReadyBold;
	Color												m_namedTimerReadyColor;

	// Drawable caption data
	AsciiString									m_drawableCaptionFont;
	Int													m_drawableCaptionPointSize;
	Bool												m_drawableCaptionBold;
	Color												m_drawableCaptionColor;

	UnsignedInt									m_tooltipsDisabledUntil;

	// Military Subtitle data
	MilitarySubtitleData *			m_militarySubtitle;		///< The pointer to subtitle class, if it's present then draw it.
	Bool												m_isScrolling;
	Bool												m_isSelecting;
	MouseMode										m_mouseMode;
	Int													m_mouseModeCursor;
	DrawableID									m_mousedOverDrawableID;
	Coord2D											m_scrollAmt;
	Bool												m_isQuitMenuVisible;
	Bool												m_messagesOn;

	Color												m_messageColor1;
	Color												m_messageColor2;
	ICoord2D										m_messagePosition;
	AsciiString									m_messageFont;
	Int													m_messagePointSize;
	Bool												m_messageBold;
	Int													m_messageDelayMS;

	RGBAColorInt								m_militaryCaptionColor;				///< color for the military-style caption
	ICoord2D										m_militaryCaptionPosition;					///< position for the military-style caption

	AsciiString									m_militaryCaptionTitleFont;
	Int													m_militaryCaptionTitlePointSize;
	Bool												m_militaryCaptionTitleBold;

	AsciiString									m_militaryCaptionFont;
	Int													m_militaryCaptionPointSize;
	Bool												m_militaryCaptionBold;

	Bool												m_militaryCaptionRandomizeTyping;
	Int													m_militaryCaptionSpeed;

	RadiusDecalTemplate					m_radiusCursors[RADIUSCURSOR_COUNT];
	RadiusDecal									m_curRadiusCursor;
	RadiusDecal									m_curRadiusCursorCentre;	///< the guard marker's shield, apart from its ring so it never stretches
	RadiusCursorType						m_curRcType;

	//Floating Text Data
	FloatingTextList						m_floatingTextList;				///< Our list of floating text
	UnsignedInt									m_floatingTextTimeOut;									///< Ini value of our floating text timeout
	Real												m_floatingTextMoveUpSpeed;							///< INI value of our Move up speed
	Real												m_floatingTextMoveVanishRate;					///< INI value of our move vanish rate

	PopupMessageData *					m_popupMessageData;
	Color												m_popupMessageColor;
	
 	Bool												m_waypointMode;			///< are we in waypoint plotting mode?
	Bool												m_forceAttackMode;		///< are we in force attack mode?
	Bool												m_forceMoveToMode;		///< are we in force move mode?
	Bool												m_attackMoveToMode;	///< are we in attack move mode?
	Bool												m_forceAttackArmed;	///< is the attack key holding force fire for the next click?
	Bool												m_guardArmed;				///< is the guard key holding a guard order for the next click?
	GuardMode										m_guardArmedMode;		///< the mode that guard order carries
	Real												m_guardRadiusScale;	///< what the wheel has made of the armed guard's range, 1 untouched
	Bool												m_moveArmed;				///< is the move key holding a move for the next click?
	Bool												m_orderKeyKeptByShift;	///< an armed key was clicked with under shift, and drops when shift comes up
	Bool												m_preferSelection;		///< the shift key has been depressed.

	// wall clock of the previous update(), so a held camera key can be stepped by elapsed
	// time instead of by render frames.  0 = no previous update yet.
	UnsignedInt									m_cameraKeyLastMs;

	// wall clock of the last quantized rotate step under SnapCameraRotateTo45.  0 = step now.
	UnsignedInt									m_cameraSnapRepeatMs;

	Bool												m_cameraRotatingLeft; 
	Bool 												m_cameraRotatingRight;
	Bool 												m_cameraZoomingIn;
	Bool 												m_cameraTrackingDrawable;
	Bool 												m_cameraZoomingOut;
	
	Bool												m_drawRMBScrollAnchor;
	Bool												m_moveRMBScrollAnchor;
	Bool												m_clientQuiet;         ///< When the user clicks exit,restart, etc. this is set true 
																												///< to skip some client sounds/fx during shutdown

	// World Animation Data
	WorldAnimationList					m_worldAnimationList;		///< the list of world animations

	// Idle worker animation
	ObjectList									m_idleWorkers[MAX_PLAYER_COUNT];

	DrawableID									m_soloNexusSelectedDrawableID;  ///< The drawable of the nexus, if only one angry mob is selected, otherwise, null

	// ----------------------------------------------------------------------------------------------
	// STATIC Protected Data -------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	static const FieldParse s_fieldParseTable[];

};

// the singleton
extern InGameUI *TheInGameUI;

//
// A readout on a cell of the HUD: a countdown, a key, a price, a power figure, a count.  Every one
// of them stands on a solid plate in a corner of the cell's inner rectangle, which is the cell less
// its frame, so no plate lies under a border and no digit is cut by one.  The command buttons'
// badges in W3DPushButton.cpp draw through these.
//
enum HudReadoutCorner
{
	HUD_READOUT_TOP_LEFT,
	HUD_READOUT_TOP_RIGHT,
	HUD_READOUT_BOTTOM_LEFT,
	HUD_READOUT_BOTTOM_RIGHT,
	HUD_READOUT_CENTRE
};

/** Whether `text` as drawn is no wider than a readout on `cell` may be: the inner rectangle less the
	* plate's own margins.  Text that does not fit is set a point smaller until it does. */
extern Bool HudReadout_fits( DisplayString *text, const IRegion2D &cell );

/** The plate in `corner` of `cell`'s inner rectangle and `text` on it: `plate` stretched to it when
	* given (a sheet piece), a solid one otherwise. */
extern void HudReadout_draw( DisplayString *text, const IRegion2D &cell, HudReadoutCorner corner, Color textColor,
														 const Image *plate = NULL );

#endif // _IN_GAME_UI_H_
