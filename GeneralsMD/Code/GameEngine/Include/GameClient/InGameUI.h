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
#include "Common/SpecialPowerType.h"
#include "Common/Snapshot.h"
#include "Common/STLTypedefs.h"
#include "Common/SubsystemInterface.h"
#include "Common/UnicodeString.h"
#include "GameClient/DisplayString.h"
#include "GameClient/HtmlTemplate.h"
#include "GameLogic/OrderQueue.h"		// the shift queue, which the logic keeps and the UI draws

#include <set>

class HtmlOverlay;
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
enum LegalBuildCode;
enum KindOfType;
enum ShadowType;
enum CanAttackResult;
enum ScienceType;

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
enum RadiusCursorType
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

/** One superweapon countdown as the spectator page lists it: whose, and how long it still has. */
struct SpectatorSuperweapon
{
	Int playerIndex;
	const Image *cameo;
	Int seconds;
	Bool ready;
	const CommandButton *button;	///< the power's own, for its tooltip card; NULL for a power with none
};

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
	void playerMessage( Player *player, const UnicodeString &text );	///< a message about that player, his flag at its head
	void chatMessage( Player *player, const UnicodeString &text );		///< a line of chat, under the middle of the screen
	/** The feed's lines for what a player did: a special power fired, a superweapon or advanced tech
		* building started or `finished`, a promotion bought.  Each decides who is told. */
	void feedSpecialPower( const Object *source, const AsciiString &powerName, const SpecialPowerTemplate *power );
	void feedStructure( Object *structure, Bool finished );
	void feedScience( Player *player, ScienceType science );
	virtual void toggleMessages( void ) { m_messagesOn = 1 - m_messagesOn; }	///< toggle messages on/off
	void openScoreboard( void ) { m_scoreboardOpen = TRUE; }		///< the Tab scoreboard, up while Tab is held
	void closeScoreboard( void ) { m_scoreboardOpen = FALSE; }
	Bool pickSpectatorStat( Int commandSlot );	///< a command bar key while watching: TRUE when it picked a stat
	/** The command bar's page, Window/Html/ControlBar.html, under the bar's windows: its panels are
		* where the plates would have gone, in screen pixels.  FALSE when there is no page to draw. */
	Bool drawControlBarPage( const IRegion2D *panels, const Bool *shown, Int panelCount );
	/** A click that reached the world, on one of that page's own buttons: TRUE when it was one, and
		* when `act` is set the button it names is pressed. */
	Bool handleControlBarPageClick( const ICoord2D *mouse, Bool act );
	/** The general's promotion screen, Window/Html/Promotion.html, drawn as `parent`'s picture: the
		* screen's windows keep their clicks and the promotions' own cameos paint over it. */
	void drawPromotionPage( GameWindow *parent, Bool front );
	/** TRUE once the promotion screen is the page's, so the painted screen's own fade-in stays off. */
	Bool isPromotionPageShown( void ) const { return m_promotionPageLoaded && !m_promotionPage.empty(); }
	/** The promotion screen is coming up: its page and the dimmed screen under it fade in from now. */
	void openPromotionPage( void );
	/** The Esc menu hands its look to Window/Html/QuitMenu.html: `parent` draws the page and its keys
		* draw nothing and keep their clicks.  Left as it is when there is no page. */
	void themeQuitMenu( GameWindow *parent );
	void drawQuitMenuPage( GameWindow *parent );
	/** The command bar's grids of buttons whose cells the page frames in front of the buttons. */
	enum CellGrid { CELL_GRID_COMMAND, CELL_GRID_QUEUE, CELL_GRID_POWERS, CELL_GRID_COUNT };
	/** The steel frames over one grid's buttons, from Window/Html/ControlBar.html, drawn after them. */
	void drawCellGridFront( Int grid );
	void drawScoreboard( void );																						///< that scoreboard, over everything
	void sampleEarnings( void );																						///< every player's money earned, once a game second
	Int earnedPerSecond( Int playerIndex ) const;														///< what that player earned a second over the readings held
	/** Window/Html/Tooltip.html is there to draw with, in a match. */
	Bool isTooltipPageReady( void );
	/** The tooltip page: the command bar's build tooltip over the hovered button while one is up,
		* otherwise `cursorText` beside the pointer, edged in `accent` when it has one.  FALSE when the
		* page is not ready, and the caller draws the old way. */
	Bool drawTooltipPage( const UnicodeString &cursorText, const RGBColor *accent );
	virtual Bool isMessagesOn( void ) { return m_messagesOn; }	///< are the display messages on
	void freeMessageResources( void );				///< empty the event feed
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
	struct OrderHint
	{
		OrderHint( void ) : kind( ORDER_HINT_MOVE ), owner( INVALID_ID ), bornMs( 0 ), step( 0 ), icon( NULL ) {}

		Coord3D from;						///< where the unit is now
		Coord3D to;							///< where it is going
		OrderHintKind kind;
		ObjectID owner;					///< the selected unit this one belongs to
		UnsignedInt bornMs;			///< when the marker first appeared, so it can be slid in
		Int step;								///< its place in the order the unit will get to its points, from 1; 0 when it has only the one
		const Image *icon;			///< the upgrade's own button art on an upgrade step, NULL otherwise
	};
	const std::vector<OrderHint>& getOrderHints( void ) const { return m_drawnOrderHints; }

	// Where each ally's mouse is pointing.  A network game only: the position arrives about ten
	// times a second on a side channel of its own, carries nothing the simulation reads, and is
	// never acked or resent, so a lost one costs a tenth of a second of staleness and nothing else.
	struct AllyCursor
	{
		Coord3D			position;			///< the last spot that ally reported
		Coord3D			shown;				///< eased towards it, so ten updates a second do not read as ten steps
		UnsignedInt	heardMs;			///< wall clock of that report, for the fade when an ally stops sending
		Bool				known;				///< FALSE until the first report of the match arrives
	};
	void noteAllyCursor( Int playerIndex, Real x, Real y );		///< an ally said where their mouse is
	const AllyCursor& getAllyCursor( Int playerIndex ) const { return m_allyCursors[ playerIndex ]; }
	Real getAllyCursorFade( Int playerIndex ) const;					///< 0 when there is nothing to draw, 1 for a live cursor

	// A circle dragged out with the left button while the attack key is armed.  Everything hostile
	// and visible inside it joins the shift queue below, and the selection works down that list one
	// target at a time: the next one is ordered the moment the current one stops existing.
	void beginAttackCircle( const ICoord2D& pt );
	void updateAttackCircle( const ICoord2D& pt );
	Bool issueAttackCircle( void );		///< FALSE when the press never became a drag, so it was an ordinary attack click
	Int issueAttackLine( const std::vector<Coord3D>& line );	///< every enemy a line drawn with the attack key runs across, as a target list in line order
	Bool isAttackListTarget( const Object *obj, const Player *local ) const;	///< would the circle or the attack line take this
	void cancelAttackCircle( void ) { m_isAttackCircling = FALSE; }
	Bool isAttackCircling( void ) const { return m_isAttackCircling; }
	const ICoord2D& getAttackCircleAnchor( void ) const { return m_attackCircleAnchor; }
	const ICoord2D& getAttackCircleCursor( void ) const { return m_attackCircleCursor; }
	Bool getAttackCircleGround( Coord3D& center, Real& radius ) const;	///< the circle in world terms, FALSE while it is still a dot

	/// The order about to go on the message stream is a shift-queued one: MSG_QUEUE_NEXT_ORDER goes
	/// in front of it.  The logic keeps the list; see OrderQueue.h.
	void markNextOrderQueued( OrderQueueMode mode );

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

	/** A shift-dragged row: the step from one structure to the next, and how many fit between the
		* anchor and a cursor 'dx'/'dy' away.  The row runs along the nearest eighth of a turn - a
		* component counts once it is more than tan 22.5 degrees of the other - and packs as tight as
		* the footprint allows along it: the footprint is the box 'halfFacing' by 'halfSide' turned to
		* the heading, and the step is where the row leaves the box two of them would share.  So a
		* structure turned onto the row's own line stands face to face with the next, and one turned
		* across it corner to corner, which is the closest a straight row of those can get.  With
		* the heading on the grid's axes and the build grid on, the step goes up to whole cells and
		* every piece stays on the grid the first one was snapped to; anywhere else nothing lines two
		* edges up exactly, so a hair is left between them.  Never fewer than one, never more than
		* 'most'.  Inline and static so a test can reach it without linking the whole in-game UI. */
	static Int placementRow( Real dx, Real dy, Real headingCos, Real headingSin, Real halfFacing,
													 Real halfSide, Bool grid, Int most, Coord2D *step )
	{
		const Real slope = 0.41421356f;		// tan 22.5 degrees
		const Real diagonal = 0.70710678f;	// each component of a unit step on a diagonal
		const Real cell = (Real)PLACEMENT_CELL;
		const Real slack = 0.01f;					// Cos of a quarter turn is a hair off zero, not a cell's worth
		const Real parallel = 0.0001f;		// a projection this small is a face the row runs along, not into
		const Real clearance = 0.5f;			// world units left between two pieces off the grid

		const Real signX = fabs( dx ) > fabs( dy ) * slope ? ( dx < 0.0f ? -1.0f : 1.0f ) : 0.0f;
		const Real signY = fabs( dy ) > fabs( dx ) * slope ? ( dy < 0.0f ? -1.0f : 1.0f ) : 0.0f;
		const Real along = ( signX != 0.0f && signY != 0.0f ) ? diagonal : 1.0f;
		const Real ux = signX * along;
		const Real uy = signY * along;

		const Real intoFacing = (Real)fabs( ux * headingCos + uy * headingSin );
		const Real intoSide = (Real)fabs( uy * headingCos - ux * headingSin );
		Real reach = 0.0f;
		if( intoFacing > parallel )
			reach = 2.0f * halfFacing / intoFacing;
		if( intoSide > parallel && ( reach == 0.0f || 2.0f * halfSide / intoSide < reach ) )
			reach = 2.0f * halfSide / intoSide;

		Real component = reach * along;
		const Bool onGridAxes = fabs( headingSin * headingCos ) < parallel;
		if( grid && onGridAxes )
			component = (Real)REAL_TO_INT_CEIL( component / cell - slack ) * cell;
		else
			component += clearance;

		step->x = signX * component;
		step->y = signY * component;

		Int count = 1;
		const Real stepSqr = step->x * step->x + step->y * step->y;
		if( stepSqr > 0.0f )
			count += REAL_TO_INT_FLOOR( ( dx * step->x + dy * step->y ) / stepSqr );

		if( count > most )
			count = most;
		if( count < 1 )
			count = 1;
		return count;
	}

	/// would dragging the anchor lay a row of the pending structure, rather than aim one?
	Bool placesRow( void );
	/// the centres of that row from 'start' toward 'end', as many as MaxLineBuildObjects and the money allow
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
	enum { PENDING_PLACEMENT_FRAMES = 60 };			///< logic frames one is remembered for - two seconds,
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
	virtual Int getSelectCount( void ) { return m_selectCount; }		///< Get count of currently selected drawables
	virtual Int getMaxSelectCount( void ) { return m_maxSelectCount; }	///< Get the max number of selected drawables
	virtual UnsignedInt getFrameSelectionChanged( void ) { return m_frameSelectionChanged; }	///< Get the max number of selected drawables
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
	virtual Bool getInputEnabled( void ) { return m_inputEnabled; }	///< Get the current input status

	virtual void disregardDrawable( Drawable *draw );				///< Drawable is being destroyed, clean up any UI elements associated with it

	virtual void preDraw( void );														///< Logic which needs to occur before the UI renders
	virtual void draw( void ) = 0;													///< Render the in-game user interface
	virtual void postDraw( void );													///< Logic which needs to occur after the UI renders

	//
	// One cameo of the global production strip: which producer it belongs to, which entry of that
	// producer's queue it is, and where it was drawn this frame.
	//
	enum { PRODUCTION_STRIP_ROW_MAX = 5 };	///< cameos one column will draw, stacked upward; whatever
																					///  is left over closes it as a sixth cell wearing a "+N"

	//
	// A queue slot is one slot of the general's power bar down in the corner, measurement for
	// measurement: that bar is where a tray of cameos already lives in this game, so the strip is
	// built out of the same tray instead of inventing a second look for the same job.  Every number
	// here is read off GenPowersShortcutBar*.wnd, in the 800x600 the rest of the strip is written in.
	//
	enum
	{
		PRODUCTION_STRIP_TRAY_W		= 48,	///< the tray, at the size that bar draws it - never stretched
		PRODUCTION_STRIP_TRAY_H		= 41,
		PRODUCTION_STRIP_TRAY_X		= 1,	///< where the cameo sits inside it
		PRODUCTION_STRIP_TRAY_Y		= 7,
		PRODUCTION_STRIP_QUEUE_W	= 39,	///< and how big it is there
		PRODUCTION_STRIP_QUEUE_H	= 27		///< a cell steps a whole tray both ways, so no tray lies over the
																		///  cameo in the one beside or below it
	};

	///< does an item go in front of one already in the row?  the selected building's items are a
	///< block of their own at the head of it; inside a block, soonest finished first, ties keeping
	///< the order they were met in so a base of identical barracks does not shuffle frame to frame
	static Bool stripSlotGoesBefore( Bool leads, Int remaining,
																	 Bool otherLeads, Int otherRemaining );
	struct ProductionStripSlot
	{
		ObjectID			producer;						///< the building this item is queued on
		Int						id;									///< a unit's ProductionID, or an upgrade's name key
																			///  (kept as an Int so this header stays clear of GameLogic)
		Bool					isUpgrade;					///< which of those two the id means
		Int						typeKey;						///< what the item is rather than which order it is - a unit's template
																			///  id, an upgrade's name key - so a run of the same thing queued
																			///  back to back becomes one cameo
		Bool					isStructure;				///< the slot is a building going up on the map, not an item in
																			///  a queue: 'producer' is that building and there is no id
		Bool					leads;							///< it belongs to the building you have selected, so it stands at the
																			///  head of the row whatever the rest of the base is finishing
		Int						remaining;					///< logic frames until this item pops, waiting time ahead of it included
		Int						quantity;						///< how many of the same thing this cameo stands for: a queue of five
																			///  Red Guards is one cameo wearing an "x5", not five cameos of
																			///  the same picture pushing everything else off the row
		ICoord2D			pos;								///< top left corner of the cameo on screen
	};

	/// a click landed on the strip: jump to that producer, or cancel the item when 'cancel' is set
	Bool handleProductionStripClick( const ICoord2D *mouse, Bool cancel );
	void foldSpectatorDropDowns( const ICoord2D &mouse );		///< a press off the spectator's page closes its drop-downs

	//
	// One superweapon countdown of the top right strip. The list is rebuilt every frame out of
	// whatever timers are live, so nothing here outlives the draw that filled it in.
	//
	enum { SUPERWEAPON_STRIP_COLS = 6 };		///< cameos in one row, the soonest at the right hand end
	enum { SUPERWEAPON_STRIP_ROWS = 3 };		///< rows of them, and the rest become a "+N"
	enum { SUPERWEAPON_STRIP_MAX = SUPERWEAPON_STRIP_COLS * SUPERWEAPON_STRIP_ROWS };

	enum { SKILL_STRIP_COLS = 6 };			///< bought promotions in one row, under the countdowns
	enum { SKILL_STRIP_ROWS = 8 };			///< a row per player watching the whole match, or one
																			///  player's promotions wrapped over as many as they need
	enum { SKILL_STRIP_MAX = SKILL_STRIP_COLS * SKILL_STRIP_ROWS };
	struct SuperweaponIconSlot
	{
		const Image *	image;							///< the cameo the command bar wears for this power
		Int						seconds;						///< seconds until it can be fired, 0 once it is ready
		Int						percent;						///< how much of the charge is done, for the radial sweep
		Bool					ready;							///< charged: it flashes instead of counting
		Color					color;							///< whose it is, the colour the timer was registered with
	};

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
	virtual Int selectAllUnitsByTypeAcrossScreen(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear);                         
	virtual Int selectAllUnitsByTypeAcrossMap(KindOfMaskType mustBeSet, KindOfMaskType mustBeClear);                            
	virtual Int selectAllUnitsByTypeAcrossRegion( IRegion2D *region, KindOfMaskType mustBeSet, KindOfMaskType mustBeClear );			
	
	virtual void buildRegion( const ICoord2D *anchor, const ICoord2D *dest, IRegion2D *region );  ///< builds a region around the specified coordinates

	virtual Bool getDisplayedMaxWarning( void ) { return m_displayedMaxWarning; }
	virtual void setDisplayedMaxWarning( Bool selected ) { m_displayedMaxWarning = selected; }

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
	
	void toggleAttackMoveToMode( void )				{ m_attackMoveToMode = !m_attackMoveToMode; m_forceAttackArmed = FALSE; m_guardArmed = FALSE; }
	Bool isInAttackMoveToMode( void ) const		{ return m_attackMoveToMode; }
	void clearAttackMoveToMode( void )				{ m_attackMoveToMode = FALSE; m_forceAttackArmed = FALSE; m_guardArmed = FALSE; m_orderKeyKeptByShift = FALSE; }

	// an order click with one of the three keys armed spends the key, unless shift is down: then it
	// stays armed for the next click, so a row of targets is one key and a row of clicks, and it drops
	// when shift comes up
	void spendOrderKey( void )								{ if( m_waypointMode ) m_orderKeyKeptByShift = TRUE; else clearAttackMoveToMode(); }

	// the attack key arms force fire the way the attack move key arms an attack move: the next
	// order click shoots whatever is under it, ground included, and the mode drops again with the
	// same call that drops attack move
	void toggleForceAttackArmed( void )				{ m_forceAttackArmed = !m_forceAttackArmed; m_attackMoveToMode = FALSE; m_guardArmed = FALSE; }
	Bool isForceAttackArmed( void ) const			{ return m_forceAttackArmed; }
	Bool isOrderKeyArmed( void ) const				{ return m_forceAttackArmed || m_attackMoveToMode || m_guardArmed; }	///< the next left click is an attack, an attack move or a guard
	Bool isForceFireOn( void ) const;					///< the next order click force fires: the attack key armed it, or Legacy's ctrl is held

	// and the guard key arms guard the same way: the next order click posts the selection on that
	// spot, or on that object, and a drag posts them along the line instead of stacking them all
	// on one point.  All three modes are one mode at a time
	void toggleGuardArmed( void )							{ m_guardArmed = !m_guardArmed; m_attackMoveToMode = FALSE; m_forceAttackArmed = FALSE; }
	Bool isGuardArmed( void ) const						{ return m_guardArmed; }
	Bool isLineOrderArmed( void ) const				{ return m_attackMoveToMode || m_guardArmed; }	///< a left drag draws an attack move or guard line; force fire's left drag is the attack circle
	
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
	virtual void removeIdleWorker( Object *obj, Int playerNumber );
	virtual void selectNextIdleWorker( void );

	virtual void recreateControlBar( void );
	virtual void notifyResolutionChange( void );

	virtual void disableTooltipsUntil(UnsignedInt frameNum);
	virtual void clearTooltipsDisabled();
	virtual Bool areTooltipsDisabled() const;

	Bool getDrawRMBScrollAnchor() const { return m_drawRMBScrollAnchor; }
	Bool getMoveRMBScrollAnchor() const { return m_moveRMBScrollAnchor; }

	void setDrawRMBScrollAnchor(Bool b) { m_drawRMBScrollAnchor = b; }
	void setMoveRMBScrollAnchor(Bool b) { m_moveRMBScrollAnchor = b; }

	// The camera scroll moved from the right button to the middle one; the two INI fields keep their
	// shipped names because InGameUI.ini sets them and an unknown field is a parse error.
	Bool shouldMoveScrollAnchor( void ) const { return m_moveRMBScrollAnchor; }

private:
	virtual Int getIdleWorkerCount( void );
	virtual Object *findIdleWorker( Object *obj);
	virtual void showIdleWorkerLayout( void );
	virtual void hideIdleWorkerLayout( void );
	virtual void updateIdleWorker( void );
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
		Color color;															///< what color should we display the military subtitles
	};

	typedef std::list<Object *> ObjectList;
	typedef std::list<Object *>::iterator ObjectListIt;
	
	// ----------------------------------------------------------------------------------------------
	// Protected Methods ----------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	void destroyPlacementIcons( void );													///< Destroy placement icons
	void handleBuildPlacements( void );													///< handle updating of placement icons based on mouse pos
	void handleRadiusCursor();																	///< handle updating of "radius cursors" that follow the mouse pos

	void incrementSelectCount( void ) { ++m_selectCount; }			///< Increase by one the running total of "selected" drawables
	void decrementSelectCount( void ) { --m_selectCount; }			///< Decrease by one the running total of "selected" drawables
	virtual View *createView( void ) = 0;												///< Factory for Views
	void evaluateSoloNexus( Drawable *newlyAddedDrawable = NULL );

	void createControlBar( void );			///< create the control bar user interface
	void createReplayControl( void );		///< create the replay control window

	void setMouseCursor(Mouse::MouseCursor c);

	
	void addMessageText( const UnicodeString& formattedMessage );  ///< a plain message, a line of the event feed

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

	/** The patch of an ally's own colour lying under their cursor.  On the ground so that it reads
		* as light falling on the map rather than as a disc floating over it. */
	virtual void drawAllyCursorLights( void ) { }

	/// what a data-click on the spectator's page does, by its text: a click on the page, or the
	/// control socket's "spectator" verb, which a script uses instead of finding the pixel
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
	DrawableList								m_selectedLocalDrawables;											///< A list of all selected drawables owned by the local player
	std::vector<ObjectID>				m_tunnelTripRiders;														///< selected units inside the tunnel network on a move order, selected again when they come out
	Bool												m_isDragSelecting;														///< If TRUE, an area selection is in progress
	IRegion2D										m_dragSelectRegion;														///< if isDragSelecting is TRUE, this contains select region
	Bool												m_isFormationDragging;												///< TRUE while a formation line is being drawn (fork)
	std::vector<ICoord2D>				m_formationDragPoints;												///< the traced curve, in pixels, first point is where it started
	Int													m_formationDragSpacing;												///< pixels a new point has to earn, doubled each time the curve fills up
	std::vector<OrderHint>			m_orderHints;																	///< who is going where, one per unit and queued point
	std::vector<OrderHint>			m_drawnOrderHints;														///< the same, units headed the same way bunched into one

	AllyCursor									m_allyCursors[ MAX_PLAYER_COUNT ];						///< where every ally's mouse was last seen (fork)
	UnsignedInt									m_allyCursorSentMs;														///< wall clock of the last position this machine sent
	Coord3D											m_allyCursorSentPosition;											///< and what it said, so a still mouse sends nothing
	UnsignedInt									m_allyCursorEasedMs;													///< wall clock of the last easing pass, which sets its step

	void clearAllyCursors( void );															///< forget every ally's marker, for the start of a match
	void updateAllyCursors( void );															///< ease every ally's marker towards where they said it was
	void sendLocalAllyCursor( void );														///< tell the allies where this machine's mouse is, at most ten times a second
	Int allyPlayerMask( void ) const;														///< the slots this machine is allied with
	Bool isAllyOfLocalPlayer( Int playerIndex ) const;					///< is this player mutually allied with the one at this machine

	Bool												m_isAttackCircling;														///< TRUE while an attack circle is being dragged (fork)
	ICoord2D										m_attackCircleAnchor;													///< where the circle was started, in pixels
	ICoord2D										m_attackCircleCursor;													///< where the cursor is now, which is the rim

	void updateFormationHints( void );													///< recompute who goes where from the curve being drawn
	void updateOrderHints( void );															///< read the selection's own goals, once a frame
	void collectOrderHints( void );															///< one hint per selected unit and queued point
	void bunchOrderHints( void );																///< merge the hints of units going the same way
	void addOrderHint( OrderHint& hint, const std::vector<OrderHint>& previous );	///< keep a marker's age across the frame the list is rebuilt on
	Bool getHeldAircraftOrder( const Object *obj, OrderHintKind& kind, Coord3D& to ) const;	///< the order an aircraft is sitting on until it is airborne
	void addQueuedOrderTail( OrderHint& hint, const OrderChain& chain, const std::vector<OrderHint>& previous );	///< every order still owed, drawn on from where the hint leaves off
	Bool getQueuedOrderHint( const QueuedOrder& order, OrderHint& hint ) const;	///< the marker a queued order draws, FALSE for none
	void numberOrderHints( void );															///< a unit with more than one place to go numbers them
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
	enum { PEACE_TIMER_POINT_SIZE = 24 };	///< the peace time clock is the one number you watch, so it is read from across the room
	enum { PEACE_TIMER_LABEL_POINT_SIZE = 12 };	///< the word over it, which is read once and then only glanced at
	enum { PEACE_TIMER_TOP_PAD = 16 };	///< how far its plate hangs below the top edge, an 800x600 number like the strip's
	enum { PEACE_COUNTDOWN_POINT_SIZE = 96 };	///< the last ten seconds, written across the middle of the battlefield
	enum { PEACE_COUNTDOWN_SECONDS = 10 };	///< how much of the peace time is counted out in the middle of the screen
	enum { PEACE_COUNTDOWN_SIZE_STEP = 12 };	///< the popping digit's point sizes are rounded to this, so the font library caches four of them rather than one per frame
	enum { PEACE_COUNTDOWN_LABEL_SHARE = 4 };	///< the word over that digit is this fraction of it

	void drawPeaceTimer( void );					///< the lobby's peace time, counting down at the top of the screen
	void drawPeaceCountdown( UnsignedInt framesLeft );	///< the last seconds of it, one big digit in the middle of the screen
	void drawHudOverlay( void );					///< the small elapsed-time / fps plate (ShowHudOverlay)
	void drawProductionStrip( void );			///< the production queue rows above the control bar
	///< the run of cells, a column, with its left edge at 'left' and its first cell's top edge at 'bottomY'
	void drawProductionStripColumn( Int left, Int bottomY );
	void drawQueueTray( void );		///< the playing strip as a row in Window/Html/Queue.html's tray
	void drawNetPage( void );			///< the network box, Window/Html/Net.html, with the command bar's page
	std::string scoreboardHtml( void );	///< Window/Html/Scoreboard.html filled in for this frame
	const Image *productionStripTray( void );	///< the bar's tray, mirrored, kept until the bar changes side
	void stripTrayMetrics( ICoord2D *tray, ICoord2D *cameo, ICoord2D *hole, Int *step );	///< that tray's size, its cameo hole, and the column step
	void drawStripSeconds( Int which, Int x, Int y, Int w, Int h, Int seconds );	///< countdown written inside a cameo
	void drawStripQuantity( Int which, Int x, Int y, Int w, Int quantity );	///< the "xN" in a cameo's top right corner
	void addSuperweaponIcon( const Image *image, Int seconds, Int percent, Bool ready, Color color );
	void drawSuperweaponStrip( void );		///< those icons, top right, soonest at the right hand end
	void drawSkillStrip( void );					///< the watched player's bought promotions, under those

	//
	// The scoreboard on Tab, Window/Html/Scoreboard.html: every seat in the match, your side in full
	// and the other side as name and team only; watching, every seat in full.  Drawn over everything.
	//
	Bool												m_scoreboardOpen;
	HtmlOverlay *								m_scoreboardOverlay;
	Bool												m_scoreboardPageLoaded;		///< read once a match, like the spectator's page
	std::string									m_scoreboardPage;
	std::string									m_scoreboardHtml;					///< the page filled in, kept for NET_WORTH_REFRESH_FRAMES
	UnsignedInt									m_scoreboardHtmlFrame;		///< the logic frame it was filled in on
	// The last half minute of every player's money earned, one reading a game second, oldest first, so
	// a seat can say what it earns now beside its average over the match.  The reading's own second
	// goes with it: a pass that runs several logic frames at once skips seconds.
	struct EarnedReading
	{
		UnsignedInt second;
		Int earned[ MAX_PLAYER_COUNT ];
	};
	enum
	{
		EARNINGS_WINDOW_SECONDS = 30,											///< what the per second figure is measured over
		EARNINGS_READINGS = EARNINGS_WINDOW_SECONDS + 1		///< a reading at each end of that window and every second between
	};
	EarnedReading								m_earnedReadings[ EARNINGS_READINGS ];
	Int													m_earnedReadingCount;		///< how many of those hold a reading
	HtmlOverlay *								m_controlBarOverlay;
	Bool												m_controlBarPageLoaded;
	Bool												m_controlBarPageShown;		///< drawn this frame, so its buttons can be clicked
	std::string									m_controlBarPage;
	Bool												m_controlBarPageHovered;	///< the pointer was on something the page drew, last frame
	HtmlOverlay *								m_netOverlay;
	Bool												m_netPageLoaded;
	std::string									m_netPage;
	HtmlOverlay *								m_tooltipOverlay;
	Bool												m_tooltipPageLoaded;
	std::string									m_tooltipPage;
	ICoord2D										m_tooltipSize;						///< the box as last laid out, in screen pixels, to place the next one by
	HtmlOverlay *								m_promotionOverlay;
	HtmlOverlay *								m_promotionFrontOverlay;		///< the grid's frames, drawn over the promotions
	HtmlOverlay *								m_cellFrontOverlay[ CELL_GRID_COUNT ];
	std::vector< HtmlValues >		m_cellFrontCells[ CELL_GRID_COUNT ];	///< each grid's cells as the bar's page last placed them
	Bool												m_promotionPageLoaded;
	std::string									m_promotionPage;
	Int													m_promotionShownMs;				///< how far the promotion screen has come up, -1 before its first picture
	UnsignedInt									m_promotionDrawnAt;				///< the wall clock at its last picture
	HtmlOverlay *								m_quitMenuOverlay;
	std::vector< HtmlOverlay * >	m_quitMenuKeyOverlays;	///< one for each of the menu's keys, each fading in on its own
	Bool												m_quitMenuPageLoaded;
	std::string									m_quitMenuPage;
	Int													m_quitMenuShownMs;			///< how far the menu's coming up has run, -1 until its first picture
	UnsignedInt									m_quitMenuDrawnAt;			///< the wall clock at its last picture: the game is paused under it
	Bool												m_signalsWereShown;				///< the smoke signal column was up last frame
	UnsignedInt									m_signalsRiseStartMs;			///< when it last came up, the start of its buttons' rise

	//
	// The spectator's page over the battlefield, Window/Html/Spectator.html: the drop-down that
	// switches the strips on and off, every player ranked by a chosen number, the net worth lead
	// over time and what each army is made of.  Only while watching.
	//
	void drawSpectatorPage( void );
	Bool handleSpectatorPageClick( const ICoord2D *mouse, Bool act );	///< TRUE when the click landed on it
	HtmlOverlay *								m_spectatorOverlay;
	Bool												m_spectatorPageLoaded;		///< read once a match, so an edited page shows in the next one
	Bool												m_spectatorPageShown;			///< drawn this frame, so clicks are its to take
	std::string									m_spectatorPage;					///< the page as written, before its {{values}} are filled
	std::set< std::string >			m_spectatorFlipped;				///< names a data-click="flip:name" has flipped
	std::map< std::string, std::string > m_spectatorPicked;	///< group to choice, from data-click="pick:group:choice"
	HtmlLists										m_spectatorLists;
	HtmlValues									m_spectatorTotals;				///< the page's values that are not per player, gathered with the lists
	UnsignedInt									m_spectatorListsFrame;		///< the logic frame the lists were last gathered on
	const Player								*m_spectatorListsWatched;	///< the player being watched when they were, whose seat they mark

	std::vector< SpectatorSuperweapon > m_spectatorSuperweapons;	///< every countdown the superweapon pass found, rebuilt each pass

	//
	// The event feed over the radar, Window/Html/Feed.html: every message, a superweapon ready or
	// fired, a player beaten or gone, the newest at the bottom.  A player's and a watcher's alike.
	//
	struct FeedLine
	{
		HtmlValues values;
		UnsignedInt until;					///< the logic frame it leaves on
	};
	std::vector< FeedLine >			m_feedLines;
	SignalKind									m_armedSignal;						///< the signal the next click drops; SIGNAL_KIND_COUNT for none
	struct SignalMark
	{
		Shadow *decal;
		ParticleSystemID smoke;			///< the signal's smoke, which the mark lasts exactly as long as
	};
	std::vector< SignalMark >		m_signalMarks;
	void updateSignalMarks( void );
	void clearSignalMarks( void );
	void addFeedLine( HtmlValues line );
	void feedAct( Player *player, const Image *cameo, const std::string &what, const char *tag, const char *label );
	void watchDozers( void );
	void drawFeed( void );
	Int feedFloor( void ) const;
	UnsignedInt									m_dozerCheckFrame;				///< the logic frame watchDozers last looked on
	Bool												m_hadDozer[ MAX_PLAYER_COUNT ];	///< that player had a dozer or worker then
	// the chat over the feed, Window/Html/Chat.html, the same lines kept a while
	std::vector< FeedLine >			m_chatLines;
	void drawChat( void );
	HtmlOverlay *								m_chatOverlay;
	Bool												m_chatPageLoaded;
	std::string									m_chatPage;
	HtmlOverlay *								m_feedOverlay;
	Bool												m_feedPageLoaded;
	std::string									m_feedPage;
	Int													m_feedFloor;							///< the radar's tab top on screen, from the bar's page
	Int													m_queueTrayTop;						///< the queue row's top on screen, while it is drawn

	Bool												m_placementRangeRingUp;	///< the structure on the cursor is armed, so its reach is drawn
	Real												m_placementRingRadius;	///< how far from its centre it hits
	void drawPlacementReach( void );			///< while placing, the reach of every armed building in sight, as one outline
	void drawBlindSpots( void );					///< shade the ground a placed or selected defence cannot shoot into

	DisplayString *							m_hudDisplayString;			///< the ShowHudOverlay line (fps / clock)
	HtmlValues									m_hudValues;						///< that line's readings one by one, for Window/Html/Net.html
	DisplayString *							m_peaceTimeDisplayString;	///< the peace time clock at the top of the screen
	DisplayString *							m_peaceTimeLabelDisplayString;	///< the word written over that clock
	DisplayString *							m_peaceCountdownDisplayString;	///< the big digit of its last ten seconds
	Int													m_lastMoneyDisplayed;		///< so the money gadget is only written when the amount changes
	UnsignedInt									m_hudDrawCount;					///< rendered frames counted by drawHudOverlay itself
	UnsignedInt									m_hudLastSampleFrame;		///< m_hudDrawCount the fps sample was last refreshed on
	UnsignedInt									m_hudLastSampleMs;			///< wall clock of that sample
	RateReading									m_hudFps;								///< render rate
	UnsignedInt									m_hudLastSampleLogicFrame;	///< logic frame at that same sample
	RateReading									m_hudLogicHz;						///< logic frames actually simulated per real second
	UnsignedInt									m_hudRealClockBaseMs;		///< wall clock the two elapsed-time readouts were aligned at
	UnsignedInt									m_hudLastDrawMs;				///< wall clock of the previous overlay draw, so a pause can be taken back out of it
	Int													m_hudOverlayBottom;			///< bottom of everything drawn in the top right corner, so the superweapon timers start under it
	// A script time freeze stops the logic clock, and the military subtitle's counters are
	// logic frames, so they have to be stepped by hand while it lasts.  These two turn the
	// wall clock into that step without drift: every update works out how many 30Hz frames
	// have passed since the freeze began and applies only the ones not applied yet.
	UnsignedInt									m_subtitleFreezeStartMs;	///< wall clock the current freeze started on, 0 = not frozen
	UnsignedInt									m_subtitleFreezeSteps;		///< frames already handed to the subtitle during it

	//
	// The global production strip: everything the local player has coming - queued in any factory,
	// or going up on the ground - one cameo each, soonest to finish first, in a column standing on
	// the corner above the control bar. drawProductionStrip() lays it out and records where each
	// cameo landed; handleProductionStripClick() reads those back.  What a factory is turning out
	// and what a dozer is raising stand in the same run, because the question is when the next thing
	// lands and not which of the two kinds it is, and the building you have selected leads it.
	// Watching, the queues are on the Tab scoreboard instead.
	//
	ProductionStripSlot					m_productionStrip[ PRODUCTION_STRIP_ROW_MAX ];
	Int													m_productionStripCount;		///< cameos drawn
	Int													m_productionStripTotal;		///< items queued, drawn or not
	Int													m_productionStripCameoW;		///< cameo size this frame, in the control bar's aspect
	Int													m_productionStripCameoH;
	Bool												m_productionStripThemed;		///< playing under the bar's page: a row in Queue.html's steel tray
	Int													m_productionStripStep;			///< from one themed cameo to the next, across
	HtmlOverlay *								m_queueOverlay;							///< Window/Html/Queue.html under the cameos
	HtmlOverlay *								m_queueFrontOverlay;				///< and its frames over them
	Bool												m_queuePageLoaded;
	std::string									m_queuePage;

	//
	// The strip's cameos face the other way from the power bar's, so the tray behind them is a
	// mirrored copy of it.  The whole strip wears one side's metal - the side the bar underneath it
	// is showing - so one is kept at a time and remade when the bar changes side, which watching a
	// match it does every time the observer picks a different player out of the list.
	//
	Image *											m_productionStripTray;			///< our mirrored copy of the bar's tray
	const Image *								m_productionStripTraySource;	///< the tray it was made from, so a side change remakes it
	//
	// One kept string per cameo, not one string walked across all of them.  A DisplayString
	// rebuilds its sentence whenever the text it holds changes, and a rebuild is a fresh font
	// surface, a fresh texture and a surface copy - so a single shared string paid that once per
	// cameo per frame, and watching a match the strips cost more to draw than the terrain under
	// them.  Kept per cameo, a countdown rebuilds when its own second changes: once a second.
	//
	enum { STRIP_SECONDS_STRINGS = PRODUCTION_STRIP_ROW_MAX + SUPERWEAPON_STRIP_MAX };
	enum
	{
		STRIP_OVERFLOW_PRODUCTION = 0,		///< the "+N" closing the production column
		STRIP_OVERFLOW_SUPERWEAPON,				///< and the superweapon strip's
		STRIP_OVERFLOW_STRINGS
	};

	enum { STRIP_QUANTITY_STRINGS = PRODUCTION_STRIP_ROW_MAX };

	DisplayString *							m_productionStripOverflow[ STRIP_OVERFLOW_STRINGS ];	///< the "+N" that stands for the rest of a row
	DisplayString *							m_stripSecondsString[ STRIP_SECONDS_STRINGS ];			///< the countdown written inside a cameo, either strip's
	DisplayString *							m_stripQuantityString[ STRIP_QUANTITY_STRINGS ];		///< the "xN" a run of the same item wears

	//
	// The superweapon strip: the same cameos the command bar fires them from, in the top right
	// under the clock plate, rebuilt every frame from the timers below and laid out soonest first.
	//
	SuperweaponIconSlot					m_superweaponIcons[ SUPERWEAPON_STRIP_MAX ];
	Int													m_superweaponIconCount;		///< icons drawn
	Int													m_superweaponIconTotal;		///< timers live, drawn or not; the difference is the "+N"

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
	GameWindow *								m_idleWorkerWin;
	Int													m_currentIdleWorkerDisplay;

	DrawableID									m_soloNexusSelectedDrawableID;  ///< The drawable of the nexus, if only one angry mob is selected, otherwise, null

	// ----------------------------------------------------------------------------------------------
	// STATIC Protected Data -------------------------------------------------------------------------------
	// ----------------------------------------------------------------------------------------------

	static const FieldParse s_fieldParseTable[];

};

// the singleton
extern InGameUI *TheInGameUI;

#endif // _IN_GAME_UI_H_
