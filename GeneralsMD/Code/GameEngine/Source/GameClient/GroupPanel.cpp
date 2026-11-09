// The group panel (GroupPanel.h).  On the left, every hotkey squad the local player has made, one
// plate each against the screen's left edge between the message lines and the radar: the squad's
// most numerous unit as its picture with the squad's size on it, the squad's health summed under it,
// a soft "+N" for the units still being built for it or walking to it, and an edge that burns red
// while any member fights.  No squad numbers: the picture is the squad.  On the right, while a squad
// picked by its number is still the selection, a tray between the superweapons and the general's
// powers with one build button for every unit any of the player's factories offers, whose units come
// out into that squad; a handle on its open side folds it away and out again, once for the match.
//
// Both panels wear one plate of their own, drawn in the HUD's language (BRAND.md, "The in-game
// HUD"): a dark flange, graphite steel with a brushed upper half, lit from the top left, 45 degree
// cuts on the corners away from the screen edge, and the side's colour as a stripe along the edge it
// is mounted on.
//
// Thread: LOGIC-THREAD.md.  Everything here that reads the logic runs at the render's sync point:
// GroupPanel_draw from InGameUI::postDraw, which W3DInGameUI::recordDraw runs before the overlap
// window opens with the drawing recorded for the render to replay, and the clicks on the input path,
// which runs at the sync point too.  The build buttons are windows whose state is set there and only
// drawn inside the window.  Squad members are read through Squad::getObjectIDs and findObjectByID,
// never getLiveObjects or getAllObjects: those prune the logic's list, and doing it on one machine
// only would make the squads differ between machines.
//
// radarFrameTop and controlBarTop are copies of InGameUI.cpp's statics.
// ponytail: copies, not shared; fold them into one header when InGameUI.cpp is free to change.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GroupPanel.h"

#include "Common/GameCommon.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Squad.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <algorithm>
#include <math.h>
#include <vector>

namespace
{
	// lengths at 800x600, through stripPixels
	enum
	{
		PICTURE_W					= 46,		///< a squad's picture, a little over a HUD cell's cameo (39x27)
		PICTURE_H					= 32,
		PLATE_PAD					= 3,		///< from the plate's edge to what it holds
		PLATE_CUT					= 7,		///< a 45 degree corner cut
		PLATE_ACCENT			= 2,		///< the side's stripe along the mounted edge
		ROW_GAP						= 3,		///< between two squads
		BUTTON_GAP				= 3,		///< between two build buttons
		HANDLE_W					= 10,		///< the tray's fold handle
		HANDLE_H					= 34,
		FLOOR_GAP					= 8,		///< kept clear above the general's powers and their frame
		PRODUCTION_LIFT		= 2,		///< InGameUI.cpp's PRODUCTION_LIST_LIFT: the production list stands under the squads
		PRODUCTION_TAB_H	= 28,		///< and its "+N" tab over it, at 1080 (HUD_TRAY_TAB_H)
	};

	enum
	{
		PANEL_COLUMNS			= 4,		///< the tray's width in buttons, widened only when its rows would not fit
		PANEL_MAX_COLUMNS	= 8,
		MAX_BUTTONS				= 40,		///< build buttons the tray holds
		COMBAT_SECONDS		= 3,		///< a member hit or firing this recently puts its squad in combat
	};

	/// the list's room starts this far down the screen, under the six message lines in the top left
	const Real LIST_TOP_SHARE = 0.22f;

	/// how long a squad picked by its key keeps the tray open before its selection is checked against
	/// it: a squad a key made a moment ago reaches the logic a frame, or a network delay, later
	const UnsignedInt CHOSEN_GRACE = LOGICFRAMES_PER_SECOND;

	/// the rows in key order, 1 to 9 and then 0
	const Int KEY_ORDER[ NUM_HOTKEY_SQUADS ] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };

	// the plate's steel
	const Color PLATE_FILL		= GameMakeColor( 30, 35, 41, 235 );
	const Color PLATE_SHEEN		= GameMakeColor( 255, 255, 255, 12 );	///< the brushed upper half
	const Color KEYLINE				= GameMakeColor( 5, 7, 9, 255 );
	const Color STEEL_LIT			= GameMakeColor( 140, 152, 166, 255 );
	const Color STEEL_SHADE		= GameMakeColor( 14, 16, 19, 255 );
	const Color WELL					= GameMakeColor( 6, 8, 10, 255 );		///< sunk round a picture or a button

	struct Cuts
	{
		Int tl, tr, bl, br;
	};

	struct SquadRow
	{
		Int squad;
		const ThingTemplate *main;	///< the most numerous unit type, the squad's picture
		Int members;
		Real health, maxHealth;
		Bool fighting;
		Int coming;									///< Player::countGroupReinforcements
		IRegion2D box;							///< the whole plate, set by the draw, taken by a click
	};

	struct Buildable
	{
		const ThingTemplate *thing;
		const CommandButton *button;	///< the first factory's, for the picture and the tooltip
		CommandAvailability best;			///< over every factory that offers it
	};

	SquadRow s_rows[ NUM_HOTKEY_SQUADS ];
	Int s_rowCount = 0;
	Int s_rowsBoxed = 0;					///< rows the last draw put a box on
	std::vector< Buildable > s_buildables;
	Bool s_gathered = FALSE;
	UnsignedInt s_gatherFrame = 0;
	Player *s_gatherPlayer = NULL;

	Int s_chosenSquad = -1;				///< the squad a number key picked, -1 once the selection left it
	UnsignedInt s_chosenFrame = 0;
	Int s_shownSquad = -1;				///< the squad the tray is up for
	Bool s_trayOpen = TRUE;				///< folded by its handle; the match remembers it
	Bool s_handleShown = FALSE;
	IRegion2D s_handleBox;

	GameWindow *s_panel = NULL;
	GameWindow *s_buttons[ MAX_BUTTONS ];

	DisplayString *s_sizeText[ NUM_HOTKEY_SQUADS ];
	DisplayString *s_comingText[ NUM_HOTKEY_SQUADS ];
}

//-------------------------------------------------------------------------------------------------
/** An 800x600 length at the command bar's scale, InGameUI.cpp's stripPixels; never under `least`. */
//-------------------------------------------------------------------------------------------------
static Int stripPixels( Real nominal, Int least = 1 )
{
	return max( REAL_TO_INT_CEIL( nominal * ControlBarHudPageScale() ), least );
}

/// the side's colour for the plate's stripe: Generals blue, red, the GLA's sprayed ochre, Turkiye's team red
static Color sideAccent( void )
{
	const AsciiString side = ThePlayerList->getLocalPlayer()->getSide();
	if( side.startsWith( "China" ) || side.startsWith( "Boss" ) )
		return GameMakeColor( 184, 38, 31, 255 );
	if( side.startsWith( "GLA" ) )
		return GameMakeColor( 176, 140, 62, 255 );
	if( side.startsWith( "Turkiye" ) )
		return GameMakeColor( 190, 32, 40, 255 );
	return GameMakeColor( 68, 89, 182, 255 );
}

/// halfway from `color` to white: the accent as a lit edge
static Color litFrom( Color color )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( color, &r, &g, &b, &a );
	return GameMakeColor( ( r + 255 ) / 2, ( g + 255 ) / 2, ( b + 255 ) / 2, 255 );
}

//-------------------------------------------------------------------------------------------------
// The plate.  `r` is its box; each corner of `cut` is the length of that corner's 45 degree cut,
// 0 for a square one.  Filled a pixel row at a time through the cuts and in one piece between them.
//-------------------------------------------------------------------------------------------------
static void fillPlate( const IRegion2D &r, const Cuts &cut, Color fill )
{
	const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
	const Int topCut = max( cut.tl, cut.tr ), footCut = max( cut.bl, cut.br );
	if( w <= 0 || h <= topCut + footCut )
		return;
	for( Int y = 0; y < topCut; y++ )
	{
		const Int left = max( cut.tl - y, 0 ), right = max( cut.tr - y, 0 );
		TheDisplay->drawFillRect( r.lo.x + left, r.lo.y + y, w - left - right, 1, fill );
	}
	TheDisplay->drawFillRect( r.lo.x, r.lo.y + topCut, w, h - topCut - footCut, fill );
	for( Int y = 0; y < footCut; y++ )
	{
		const Int left = max( cut.bl - y, 0 ), right = max( cut.br - y, 0 );
		TheDisplay->drawFillRect( r.lo.x + left, r.hi.y - 1 - y, w - left - right, 1, fill );
	}
}

/// the plate's outline `width` thick inside its box: the edges facing up or left in `lit`, the rest in `shade`
static void edgePlate( const IRegion2D &r, const Cuts &cut, Int width, Color lit, Color shade )
{
	const Int x0 = r.lo.x, y0 = r.lo.y, x1 = r.hi.x - 1, y1 = r.hi.y - 1;
	if( x1 <= x0 || y1 <= y0 )
		return;
	// clockwise from the top left: top, top right cut, right, bottom right cut, bottom, bottom left cut, left, top left cut
	const ICoord2D p[ 8 ] =
	{
		{ x0 + cut.tl, y0 }, { x1 - cut.tr, y0 }, { x1, y0 + cut.tr }, { x1, y1 - cut.br },
		{ x1 - cut.br, y1 }, { x0 + cut.bl, y1 }, { x0, y1 - cut.bl }, { x0, y0 + cut.tl }
	};
	for( Int i = 0; i < 8; i++ )
	{
		const ICoord2D &a = p[ i ];
		const ICoord2D &b = p[ ( i + 1 ) % 8 ];
		if( a.x == b.x && a.y == b.y )
			continue;
		const Color color = ( i == 0 || i == 1 || i == 6 || i == 7 ) ? lit : shade;
		if( a.y == b.y )
			TheDisplay->drawFillRect( min( a.x, b.x ), i == 0 ? a.y : a.y - width + 1, ( a.x > b.x ? a.x - b.x : b.x - a.x ) + 1, width, color );
		else if( a.x == b.x )
			TheDisplay->drawFillRect( i == 6 ? a.x : a.x - width + 1, min( a.y, b.y ), width, ( a.y > b.y ? a.y - b.y : b.y - a.y ) + 1, color );
		else
			TheDisplay->drawLine( a.x, a.y, b.x, b.y, (Real)width, color );
	}
}

//-------------------------------------------------------------------------------------------------
/** A whole plate: steel, the brushed upper half, the side's stripe down the mounted edge
	* (`mounted` -1 the left, +1 the right), the dark flange and inside it the lit and shaded edge. */
//-------------------------------------------------------------------------------------------------
static void drawPlate( const IRegion2D &r, const Cuts &cut, Int mounted, Color accent, Color lit )
{
	fillPlate( r, cut, PLATE_FILL );
	IRegion2D upper = r;
	upper.hi.y = r.lo.y + ( r.hi.y - r.lo.y ) / 2;
	const Cuts upperCut = { cut.tl, cut.tr, 0, 0 };
	fillPlate( upper, upperCut, PLATE_SHEEN );

	const Int stripe = stripPixels( PLATE_ACCENT, 2 );
	if( mounted < 0 )
		TheDisplay->drawFillRect( r.lo.x, r.lo.y + cut.tl, stripe, r.hi.y - r.lo.y - cut.tl - cut.bl, accent );
	else
		TheDisplay->drawFillRect( r.hi.x - stripe, r.lo.y + cut.tr, stripe, r.hi.y - r.lo.y - cut.tr - cut.br, accent );

	edgePlate( r, cut, 1, KEYLINE, KEYLINE );
	IRegion2D inner = r;
	inner.lo.x++;
	inner.lo.y++;
	inner.hi.x--;
	inner.hi.y--;
	edgePlate( inner, cut, 1, lit, STEEL_SHADE );
}

/// a well sunk round a picture: black behind it, the flange round it
static void drawWell( Int x, Int y, Int w, Int h )
{
	TheDisplay->drawFillRect( x - 1, y - 1, w + 2, h + 2, WELL );
}

//-------------------------------------------------------------------------------------------------
/** Where one of the command bar's pieces starts on screen, or the screen's foot while the bar is
	* hidden (InGameUI.cpp's controlBarTop, for any piece). */
//-------------------------------------------------------------------------------------------------
static Int controlBarTop( Int panel )
{
	static NameKeyType controlBarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
	GameWindow *bar = TheWindowManager->winGetWindowFromId( NULL, controlBarKey );
	if( bar == NULL || bar->winIsHidden() )
		return (Int)TheDisplay->getHeight();

	ICoord2D barPos;
	bar->winGetScreenPosition( &barPos.x, &barPos.y );
	return TheControlBar->getPanelRect( panel )->lo.y + barPos.y - TheControlBar->getPanelOrigin()->y;
}

/// the top of the radar's frame, InGameUI.cpp's radarFrameTop
static Int radarFrameTop( void )
{
	static NameKeyType radarKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:LeftHUD" );
	const Int barTop = controlBarTop( ControlBar::CB_PANEL_LEFT );
	const Image *const *frame = TheControlBar->getControlBarSchemeManager()->getRadarFrame();
	GameWindow *radar = TheWindowManager->winGetWindowFromId( NULL, radarKey );
	if( radar == NULL || radar->winIsHidden() || barTop >= (Int)TheDisplay->getHeight() )
		return barTop;
	if( frame == NULL )
		return TheControlBar->getControlBarSchemeManager()->radarArtTop( barTop );

	IRegion2D inner, outer;
	radar->winGetScreenPosition( &inner.lo.x, &inner.lo.y );
	radar->winGetSize( &inner.hi.x, &inner.hi.y );
	inner.hi.x += inner.lo.x;
	inner.hi.y += inner.lo.y;
	ControlBarSchemeManager::radarFrameRect( frame, inner, &outer );
	return outer.lo.y;
}

//-------------------------------------------------------------------------------------------------
/** How far down the right edge the tray may reach: the highest of the general's powers showing
	* (their stepped tray's frame stands a little above the buttons), the general's tab, and the
	* bar's right hand piece. */
//-------------------------------------------------------------------------------------------------
static Int rightTrayFloor( void )
{
	Int floor = controlBarTop( ControlBar::CB_PANEL_RIGHT );
	auto standOn = [ &floor ]( GameWindow *win )
	{
		if( win == NULL || win->winIsHidden() )
			return;
		ICoord2D at;
		win->winGetScreenPosition( &at.x, &at.y );
		floor = min( floor, at.y - stripPixels( FLOOR_GAP ) );
	};

	static NameKeyType generalKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonGeneral" );
	standOn( TheWindowManager->winGetWindowFromId( NULL, generalKey ) );

	// the shortcut layout is the player template's; its slot windows are found by name once a layout
	static AsciiString keyedLayout;
	static NameKeyType barKey = NAMEKEY_INVALID;
	static NameKeyType slotKeys[ MAX_SPECIAL_POWER_SHORTCUTS ];
	static Int slotCount = 0;
	const PlayerTemplate *pt = ThePlayerList->getLocalPlayer()->getPlayerTemplate();
	if( pt == NULL || pt->getSpecialPowerShortcutWinName().isEmpty() )
		return floor;
	if( keyedLayout != pt->getSpecialPowerShortcutWinName() )
	{
		keyedLayout = pt->getSpecialPowerShortcutWinName();
		AsciiString name;
		name.format( "%s:GenPowersShortcutBarParent", keyedLayout.str() );
		barKey = TheNameKeyGenerator->nameToKey( name );
		slotCount = min( pt->getSpecialPowerShortcutButtonCount(), (Int)MAX_SPECIAL_POWER_SHORTCUTS );
		for( Int i = 0; i < slotCount; i++ )
		{
			name.format( "%s:ButtonParent%d", keyedLayout.str(), i + 1 );
			slotKeys[ i ] = TheNameKeyGenerator->nameToKey( name );
		}
	}
	GameWindow *bar = TheWindowManager->winGetWindowFromId( NULL, barKey );
	if( bar == NULL || bar->winIsHidden() )
		return floor;
	for( Int i = 0; i < slotCount; i++ )
		standOn( TheWindowManager->winGetWindowFromId( bar, slotKeys[ i ] ) );
	return floor;
}

//-------------------------------------------------------------------------------------------------
/** A kept string set to `text` in Arial bold at `points`, laid out again only when either changed. */
//-------------------------------------------------------------------------------------------------
static DisplayString *keptString( DisplayString *&string, const UnicodeString &text, Int points )
{
	if( string == NULL )
		string = TheDisplayStringManager->newDisplayString();
	GameFont *font = TheFontLibrary->getFont( "Arial", max( points, 6 ), TRUE );
	if( string->getFont() != font )
		string->setFont( font );
	if( string->getText().compare( text ) != 0 )
		string->setText( text );
	return string;
}

static void drawCentred( DisplayString *text, const IRegion2D &box, Color color )
{
	Int width = 0, height = 0;
	text->getSize( &width, &height );
	text->draw( ( box.lo.x + box.hi.x - width ) / 2, ( box.lo.y + box.hi.y - height ) / 2, color, GameMakeColor( 0, 0, 0, 255 ) );
}

static const Image *pictureOf( const ThingTemplate *thing )
{
	if( thing == NULL )
		return NULL;
	return thing->getButtonImage() ? thing->getButtonImage() : thing->getSelectedPortraitImage();
}

//-------------------------------------------------------------------------------------------------
/** Each squad's live members, its main type, health and fight, read from the logic's own list. */
//-------------------------------------------------------------------------------------------------
static void gatherSquads( Player *player )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt fight = COMBAT_SECONDS * LOGICFRAMES_PER_SECOND;
	auto recent = [ now, fight ]( UnsignedInt frame ) { return frame != 0 && frame <= now && now - frame <= fight; };

	s_rowCount = 0;
	std::vector< std::pair< const ThingTemplate *, Int > > types;
	for( Int k = 0; k < NUM_HOTKEY_SQUADS; k++ )
	{
		const Int squad = KEY_ORDER[ k ];
		const Squad *members = player->getHotkeySquad( squad );
		if( members == NULL )
			continue;

		SquadRow row;
		row.squad = squad;
		row.main = NULL;
		row.members = 0;
		row.health = 0.0f;
		row.maxHealth = 0.0f;
		row.fighting = FALSE;
		row.coming = 0;
		row.box.lo.x = row.box.lo.y = row.box.hi.x = row.box.hi.y = 0;

		types.clear();
		const VecObjectID &ids = members->getObjectIDs();
		for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
		{
			const Object *obj = TheGameLogic->findObjectByID( *it );
			// one on its way from the factory is the plate's "+N", not one of its members yet
			if( obj == NULL || obj->isEffectivelyDead() || obj->getReinforcementSquad() >= 0 )
				continue;

			row.members++;
			const ThingTemplate *thing = obj->getTemplate();
			size_t at = 0;
			while( at < types.size() && types[ at ].first != thing )
				at++;
			if( at == types.size() )
				types.push_back( std::make_pair( thing, 0 ) );
			types[ at ].second++;

			const BodyModuleInterface *body = obj->getBodyModule();
			if( body != NULL )
			{
				row.health += body->getHealth();
				row.maxHealth += body->getMaxHealth();
				if( recent( body->getLastDamageTimestamp() ) )
					row.fighting = TRUE;
			}
			if( recent( obj->getLastShotFiredFrame() ) )
				row.fighting = TRUE;
		}
		// a squad with nobody left standing is not listed
		if( row.members == 0 )
			continue;

		// the most numerous type, the one that joined first on a tie
		Int best = 0;
		for( size_t t = 0; t < types.size(); t++ )
			if( types[ t ].second > best )
			{
				best = types[ t ].second;
				row.main = types[ t ].first;
			}
		row.coming = player->countGroupReinforcements( squad );
		s_rows[ s_rowCount++ ] = row;
	}
}

//-------------------------------------------------------------------------------------------------
/** One unit type per entry for every unit build button any of the player's finished factories
	* carries, with the best availability the command bar would give it on any of them. */
//-------------------------------------------------------------------------------------------------
static Bool isWorkingFactory( Object *obj )
{
	return !obj->isEffectivelyDead() && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
		&& !obj->testStatus( OBJECT_STATUS_SOLD ) && obj->getProductionUpdateInterface() != NULL;
}

static void gatherBuildable( Object *obj, void *userData )
{
	std::vector< Buildable > *list = (std::vector< Buildable > *)userData;
	if( !isWorkingFactory( obj ) )
		return;
	const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( set == NULL )
		return;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *thing = button->getThingTemplate();
		if( thing == NULL )
			continue;
		const CommandAvailability now = TheControlBar->getButtonAvailability( button, obj );
		if( now == COMMAND_HIDDEN )
			continue;

		size_t at = 0;
		while( at < list->size() && (*list)[ at ].thing != thing )
			at++;
		if( at == list->size() )
		{
			const Buildable added = { thing, button, now };
			list->push_back( added );
		}
		else if( ControlBar::commandAvailabilityRank( now ) > ControlBar::commandAvailabilityRank( (*list)[ at ].best ) )
			(*list)[ at ].best = now;
	}
}

struct FreeSlots
{
	const ThingTemplate *thing;
	UnsignedInt free;
};

/// the room left in the queues of the factories that can take `thing` now
static void countFreeSlots( Object *obj, void *userData )
{
	FreeSlots *slots = (FreeSlots *)userData;
	if( !isWorkingFactory( obj ) )
		return;
	const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( set == NULL )
		return;

	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD )
			continue;
		const ThingTemplate *thing = button->getThingTemplate();
		if( thing != slots->thing )
			continue;
		if( TheControlBar->getButtonAvailability( button, obj ) != COMMAND_AVAILABLE )
			return;
		const ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
		const UnsignedInt cap = getQueueCap( pu );
		if( pu->getProductionCount() < cap )
			slots->free += cap - pu->getProductionCount();
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** A click on a build button: as many MSG_QUEUE_UNIT_FOR_GROUP as the factory buttons' shift and
	* ctrl ask for, cut to what the bank pays for and the queues hold, so a ctrl+shift click does not
	* send a hundred orders the logic would refuse.  The logic picks the factory for each. */
//-------------------------------------------------------------------------------------------------
static void queueForSquad( const ThingTemplate *thing, Int squad )
{
	Player *player = ThePlayerList->getLocalPlayer();
	FreeSlots slots = { thing, 0 };
	player->iterateObjects( countFreeSlots, &slots );

	Int wanted = min( getBuildBatchCount(), (Int)slots.free );
	const Int cost = thing->calcCostToBuild( player );
	if( cost > 0 )
		wanted = min( wanted, (Int)( player->getMoney()->countMoney() / (UnsignedInt)cost ) );

	for( Int each = 0; each < wanted; each++ )
	{
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_UNIT_FOR_GROUP );
		msg->appendIntegerArgument( thing->getTemplateID() );
		msg->appendIntegerArgument( squad );
	}
}

//-------------------------------------------------------------------------------------------------
// The tray's windows: a parent over the plate that takes the clicks between the buttons and hears
// the buttons' presses, and MAX_BUTTONS push buttons in it drawn as the command bar draws its own
// (the cameo, the build border, greyed when the bar would grey it) with the bar's build tooltip.
// They are made on first use and again whenever the window manager has destroyed them.
//-------------------------------------------------------------------------------------------------
static Int panelId( void )
{
	static const Int id = (Int)TheNameKeyGenerator->nameToKey( "GroupPanel:BuildTray" );
	return id;
}

static Bool panelAlive( void )
{
	return s_panel != NULL && TheWindowManager->winGetWindowFromId( NULL, panelId() ) == s_panel;
}

static void drawNothing( GameWindow *window, WinInstanceData *instData )
{
}

static void buildTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	TheControlBar->showBuildTooltipLayout( window );
}

static WindowMsgHandledType panelSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GBM_SELECTED )
		return GameWinDefaultSystem( window, msg, mData1, mData2 );

	const CommandButton *command = (const CommandButton *)GadgetButtonGetData( (GameWindow *)mData1 );
	if( command != NULL && command->getThingTemplate() != NULL && isValidHotkeySquadIndex( s_shownSquad ) )
		queueForSquad( command->getThingTemplate(), s_shownSquad );
	return MSG_HANDLED;
}

static GameWindow *panelWindow( void )
{
	if( panelAlive() )
		return s_panel;

	s_panel = TheWindowManager->winCreate( NULL, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, 0, 0, 1, 1, panelSystem );
	if( s_panel == NULL )
		return NULL;
	s_panel->winSetWindowId( panelId() );
	s_panel->winSetDrawFunc( drawNothing );
	s_panel->winSetInputFunc( GameWinBlockInput );

	for( Int i = 0; i < MAX_BUTTONS; i++ )
	{
		WinInstanceData inst;
		inst.init();
		BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
		GameWindow *button = TheWindowManager->gogoGadgetPushButton( s_panel,
			WIN_STATUS_ENABLED | WIN_STATUS_IMAGE | WIN_STATUS_USE_OVERLAY_STATES | WIN_STATUS_HIDDEN,
			0, 0, 1, 1, &inst, NULL, TRUE );
		s_buttons[ i ] = button;
		if( button == NULL )
			continue;
		button->winSetTooltipFunc( buildTooltip );
		GadgetButtonSetData( button, NULL );
		GadgetButtonSetBorder( button, TheControlBar->getBuildBorderColor() );
		GadgetButtonSetAltSound( button, "GUICommandBarClick" );
	}
	return s_panel;
}

static void hideTrayWindows( void )
{
	if( panelAlive() )
		s_panel->winHide( TRUE );
}

static void hidePanel( void )
{
	s_shownSquad = -1;
	s_handleShown = FALSE;
	hideTrayWindows();
}

//-------------------------------------------------------------------------------------------------
/** Whether the selection is still the squad a key picked: nothing selected that is not in it. */
//-------------------------------------------------------------------------------------------------
static Bool chosenSquadSelected( Player *player )
{
	if( !isValidHotkeySquadIndex( s_chosenSquad ) )
		return FALSE;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if( selected == NULL || selected->empty() )
		return FALSE;
	// ponytail: a fixed second of grace for a squad still on its way to the logic; the pending list
	// SelectionTranslator keeps would be exact if a longer network delay ever closes the tray early
	if( TheGameLogic->getFrame() - s_chosenFrame < CHOSEN_GRACE )
		return TRUE;

	const Squad *squad = player->getHotkeySquad( s_chosenSquad );
	if( squad == NULL )
		return FALSE;
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj == NULL || !squad->isOnSquad( obj ) )
			return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The squad list: a plate per squad against the left edge, centred in the room between the message
	* lines and the production list over the radar.  Plates that do not fit shrink together.
	*
	*   | stripe | picture "xN" |  "+N"  \
	*   |        | health bar   |        /
	*/
//-------------------------------------------------------------------------------------------------
static void drawList( Color accent )
{
	if( s_rowCount == 0 )
		return;

	const Int pad = stripPixels( PLATE_PAD, 2 );
	const Int cut = stripPixels( PLATE_CUT, 4 );
	const Int stripe = stripPixels( PLATE_ACCENT, 2 );
	const Int gap = stripPixels( ROW_GAP, 2 );
	const Int screenH = (Int)TheDisplay->getHeight();
	const Int top = REAL_TO_INT( screenH * LIST_TOP_SHARE );
	const Int baseW = stripPixels( PICTURE_W ), baseH = stripPixels( PICTURE_H );
	auto barOf = []( Int picH ) { return max( picH / 8, 3 ); };
	auto rowOf = [ pad, barOf ]( Int picH ) { return pad + picH + pad + barOf( picH ) + pad; };
	const Int bottom = radarFrameTop() - stripPixels( PRODUCTION_LIFT ) - stripPixels( 39 )
										 - REAL_TO_INT_CEIL( PRODUCTION_TAB_H * screenH / 1080.0f ) - gap;
	const Int room = max( bottom - top, 1 );

	Int picW = baseW, picH = baseH;
	Int total = s_rowCount * ( rowOf( picH ) + gap ) - gap;
	for( Real scale = 1.0f; total > room && scale > 0.41f; )
	{
		scale = max( scale * min( (Real)room / total, 0.95f ), 0.4f );
		picW = REAL_TO_INT_FLOOR( baseW * scale );
		picH = REAL_TO_INT_FLOOR( baseH * scale );
		total = s_rowCount * ( rowOf( picH ) + gap ) - gap;
	}

	const Int tailW = max( picW * 9 / 20, stripPixels( 14 ) );
	const Int plateW = stripe + pad + picW + pad + tailW + cut / 2;
	const Int firstTop = top + max( ( room - total ) / 2, 0 );
	const Int countPoints = max( REAL_TO_INT( picH * 0.3f ), 7 );
	const Color white = GameMakeColor( 245, 245, 245, 255 );
	const Color soft = GameMakeColor( 165, 172, 180, 255 );
	const Color chosenLit = litFrom( accent );
	const Real phase = (Real)( TheGameLogic->getFrame() % LOGICFRAMES_PER_SECOND ) / LOGICFRAMES_PER_SECOND;
	const Int fightAlpha = REAL_TO_INT( 150.0f + 105.0f * ( 0.5f + 0.5f * cosf( 2.0f * PI * phase ) ) );
	const Cuts cuts = { 0, cut, 0, cut };

	for( Int r = 0; r < s_rowCount; r++ )
	{
		SquadRow &row = s_rows[ r ];
		const Bool chosen = row.squad == s_shownSquad;
		row.box.lo.x = 0;
		row.box.lo.y = firstTop + r * ( rowOf( picH ) + gap );
		row.box.hi.x = plateW;
		row.box.hi.y = row.box.lo.y + rowOf( picH );

		IRegion2D picture;
		picture.lo.x = stripe + pad;
		picture.lo.y = row.box.lo.y + pad;
		picture.hi.x = picture.lo.x + picW;
		picture.hi.y = picture.lo.y + picH;
		IRegion2D tail;
		tail.lo.x = picture.hi.x + pad;
		tail.hi.x = tail.lo.x + tailW;
		tail.lo.y = picture.lo.y;
		tail.hi.y = picture.hi.y;

		TheDisplay->beginBatch2D();
		drawPlate( row.box, cuts, -1, chosen ? chosenLit : accent, chosen ? chosenLit : STEEL_LIT );
		drawWell( picture.lo.x, picture.lo.y, picW, picH );
		const Image *image = pictureOf( row.main );
		if( image != NULL )
			TheDisplay->drawImage( image, picture.lo.x, picture.lo.y, picture.hi.x, picture.hi.y );

		// the squad's health, summed, under its picture
		const Int barH = barOf( picH );
		const Int barTop = picture.hi.y + pad / 2 + 1;
		TheDisplay->drawFillRect( picture.lo.x, barTop, picW, barH, WELL );
		if( row.maxHealth > 0.0f )
		{
			const Real share = max( min( row.health / row.maxHealth, 1.0f ), 0.0f );
			const Color fill = share > 0.5f ? GameMakeColor( 70, 200, 60, 255 )
											 : ( share > 0.25f ? GameMakeColor( 230, 200, 40, 255 ) : GameMakeColor( 225, 45, 30, 255 ) );
			const Int filled = REAL_TO_INT_CEIL( ( picW - 2 ) * share );
			if( filled > 0 )
				TheDisplay->drawFillRect( picture.lo.x + 1, barTop + 1, filled, max( barH - 2, 1 ), fill );
		}

		// a member hit or firing: the flange burns red, breathing on the logic clock
		if( row.fighting )
		{
			const Color red = GameMakeColor( 230, 40, 30, (UnsignedByte)fightAlpha );
			edgePlate( row.box, cuts, 2, red, red );
		}
		TheDisplay->endBatch2D();

		UnicodeString text;
		text.format( u"x%d", row.members );
		HudReadout_draw( keptString( s_sizeText[ row.squad ], text, countPoints ), picture, HUD_READOUT_TOP_RIGHT, white );
		if( row.coming > 0 )
		{
			text.format( u"+%d", row.coming );
			drawCentred( keptString( s_comingText[ row.squad ], text, countPoints ), tail, soft );
		}
	}
	s_rowsBoxed = s_rowCount;
}

/// the fold handle's chevron, pointing right (`right` TRUE) or left
static void drawChevron( const IRegion2D &box, Bool right, Color color )
{
	const Int cx = ( box.lo.x + box.hi.x ) / 2, cy = ( box.lo.y + box.hi.y ) / 2;
	const Int half = max( ( box.hi.x - box.lo.x ) / 4, 2 );
	const Int tipX = right ? cx + half : cx - half;
	const Int backX = right ? cx - half : cx + half;
	TheDisplay->drawLine( backX, cy - 2 * half, tipX, cy, 2.0f, color );
	TheDisplay->drawLine( tipX, cy, backX, cy + 2 * half, 2.0f, color );
}

//-------------------------------------------------------------------------------------------------
/** The build tray under the superweapons and over the general's powers, on the plate the squad
	* list wears: the squad's picture in a strip along its top, a button for each unit under it, as big
	* as the command bar's own buttons when they fit.  Its handle hangs off the open side and folds it
	* away against the right edge. */
//-------------------------------------------------------------------------------------------------
static void drawTray( Int top, Color accent )
{
	s_handleShown = FALSE;
	if( !isValidHotkeySquadIndex( s_shownSquad ) || s_buildables.empty() )
	{
		hideTrayWindows();
		return;
	}

	const Int right = (Int)TheDisplay->getWidth();
	const Int pad = stripPixels( PLATE_PAD, 2 );
	const Int cut = stripPixels( PLATE_CUT, 4 );
	const Int stripe = stripPixels( PLATE_ACCENT, 2 );
	const Int gap = stripPixels( BUTTON_GAP, 2 );
	const Int handleW = stripPixels( HANDLE_W, 8 );
	const Int handleH = stripPixels( HANDLE_H, 20 );
	const Int floor = rightTrayFloor();

	const ThingTemplate *main = NULL;
	for( Int r = 0; r < s_rowCount; r++ )
		if( s_rows[ r ].squad == s_shownSquad )
			main = s_rows[ r ].main;

	// the handle, alone against the edge while the tray is folded
	if( !s_trayOpen )
	{
		hideTrayWindows();
		s_handleBox.lo.x = right - handleW - stripe;
		s_handleBox.hi.x = right;
		s_handleBox.lo.y = top;
		s_handleBox.hi.y = top + handleH;
		const Cuts handleCuts = { handleW / 2, 0, handleW / 2, 0 };
		TheDisplay->beginBatch2D();
		drawPlate( s_handleBox, handleCuts, 1, accent, STEEL_LIT );
		IRegion2D mark = s_handleBox;
		mark.hi.x -= stripe;
		drawChevron( mark, FALSE, STEEL_LIT );
		TheDisplay->endBatch2D();
		s_handleShown = TRUE;
		return;
	}

	GameWindow *panel = panelWindow();
	if( panel == NULL )
		return;

	Int buttonW = stripPixels( 39 ), buttonH = stripPixels( 27 );
	static NameKeyType barButtonKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonCommand01" );
	GameWindow *barButton = TheWindowManager->winGetWindowFromId( NULL, barButtonKey );
	if( barButton != NULL )
	{
		Int w = 0, h = 0;
		barButton->winGetSize( &w, &h );
		if( w > 0 && h > 0 )
		{
			buttonW = w;
			buttonH = h;
		}
	}

	const Int shown = min( (Int)s_buildables.size(), (Int)MAX_BUTTONS );
	auto stripH = []( Int bH ) { return max( bH * 3 / 5, 12 ); };
	auto plateH = [ & ]( Int columns, Int bH )
	{
		const Int rows = ( shown + columns - 1 ) / columns;
		return pad + stripH( bH ) + pad + rows * bH + ( rows - 1 ) * gap + pad;
	};
	// wider first, then smaller buttons, down to half the bar's
	Int columns = min( (Int)PANEL_COLUMNS, shown );
	while( columns < min( (Int)PANEL_MAX_COLUMNS, shown ) && top + plateH( columns, buttonH ) > floor )
		columns++;
	for( Real scale = 1.0f; top + plateH( columns, buttonH ) > floor && scale > 0.51f; )
	{
		scale = max( scale * 0.92f, 0.5f );
		buttonW = REAL_TO_INT_FLOOR( buttonW * 0.92f );
		buttonH = REAL_TO_INT_FLOOR( buttonH * 0.92f );
	}

	const Int gridW = columns * buttonW + ( columns - 1 ) * gap;
	IRegion2D plate;
	plate.hi.x = right;
	plate.lo.x = right - stripe - pad - gridW - pad - cut / 2;
	plate.lo.y = top;
	plate.hi.y = top + plateH( columns, buttonH );
	const Cuts plateCuts = { cut, 0, cut, 0 };

	s_handleBox.hi.x = plate.lo.x + 1;
	s_handleBox.lo.x = s_handleBox.hi.x - handleW;
	s_handleBox.lo.y = plate.lo.y + cut;
	s_handleBox.hi.y = s_handleBox.lo.y + handleH;
	s_handleShown = TRUE;
	const Cuts handleCuts = { handleW / 2, 0, handleW / 2, 0 };

	const Int gridX = right - stripe - pad - gridW;
	const Int gridY = top + pad + stripH( buttonH ) + pad;

	TheDisplay->beginBatch2D();
	drawPlate( s_handleBox, handleCuts, 1, STEEL_SHADE, STEEL_LIT );
	drawChevron( s_handleBox, TRUE, STEEL_LIT );
	drawPlate( plate, plateCuts, 1, accent, STEEL_LIT );

	// the strip along the top: the squad's picture against the stripe, a groove under it
	const Image *image = pictureOf( main );
	const Int picH = stripH( buttonH ) - 2;
	const Int picW = picH * buttonW / max( buttonH, 1 );
	const Int picX = right - stripe - pad - picW, picY = top + pad + 1;
	if( image != NULL )
	{
		drawWell( picX, picY, picW, picH );
		TheDisplay->drawImage( image, picX, picY, picX + picW, picY + picH );
	}
	const Int grooveY = gridY - pad / 2 - 1;
	TheDisplay->drawFillRect( plate.lo.x + cut, grooveY, right - stripe - plate.lo.x - cut, 1, KEYLINE );
	TheDisplay->drawFillRect( plate.lo.x + cut, grooveY + 1, right - stripe - plate.lo.x - cut, 1, STEEL_LIT );

	// a well under each button, the buttons themselves are windows drawn over the plate
	for( Int i = 0; i < shown; i++ )
		drawWell( gridX + ( columns - 1 - i % columns ) * ( buttonW + gap ), gridY + i / columns * ( buttonH + gap ), buttonW, buttonH );
	TheDisplay->endBatch2D();

	// the parent covers the plate, so a click between the buttons stays off the ground
	panel->winSetPosition( plate.lo.x, plate.lo.y );
	panel->winSetSize( plate.hi.x - plate.lo.x, plate.hi.y - plate.lo.y );
	for( Int i = 0; i < MAX_BUTTONS; i++ )
	{
		GameWindow *button = s_buttons[ i ];
		if( button == NULL )
			continue;
		if( i >= shown )
		{
			button->winHide( TRUE );
			continue;
		}

		// filled from the right edge in, as the superweapons are
		const Buildable &entry = s_buildables[ i ];
		const Int x = gridX + ( columns - 1 - i % columns ) * ( buttonW + gap );
		const Int y = gridY + i / columns * ( buttonH + gap );
		button->winSetPosition( x - plate.lo.x, y - plate.lo.y );
		button->winSetSize( buttonW, buttonH );
		if( GadgetButtonGetData( button ) != (void *)entry.button )
		{
			GadgetButtonSetEnabledImage( button, entry.button->getButtonImage() );
			GadgetButtonSetData( button, (void *)entry.button );
		}

		// the command bar's own reading of an availability (ControlBar::updateContextCommand)
		button->winClearStatus( WIN_STATUS_NOT_READY );
		button->winClearStatus( WIN_STATUS_ALWAYS_COLOR );
		switch( entry.best )
		{
			case COMMAND_AVAILABLE:
			case COMMAND_ACTIVE:
				button->winEnable( TRUE );
				break;
			case COMMAND_NOT_READY:
				button->winEnable( FALSE );
				button->winSetStatus( WIN_STATUS_NOT_READY );
				break;
			case COMMAND_CANT_AFFORD:
				button->winEnable( FALSE );
				button->winSetStatus( WIN_STATUS_ALWAYS_COLOR );
				break;
			default:
				button->winEnable( FALSE );
				break;
		}
		button->winHide( FALSE );
	}
	panel->winHide( FALSE );
}

//-------------------------------------------------------------------------------------------------
void GroupPanel_draw( Int rightTop )
{
	s_rowsBoxed = 0;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
	{
		s_chosenSquad = -1;
		s_gathered = FALSE;
		s_trayOpen = TRUE;		// a new match starts with the tray out
		hidePanel();
		return;
	}

	// a watcher, a replay and a hidden command bar (cinematics) get neither half
	Player *player = ThePlayerList->getLocalPlayer();
	if( player == NULL || !player->isPlayerActive() || TheGameLogic->isInReplayGame() || TheInGameUI->isQuitMenuVisible()
			|| controlBarTop( ControlBar::CB_PANEL_LEFT ) >= (Int)TheDisplay->getHeight() )
	{
		hidePanel();
		return;
	}

	// once a logic frame: nothing read here changes in between
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( !s_gathered || s_gatherFrame != frame || s_gatherPlayer != player )
	{
		gatherSquads( player );
		s_buildables.clear();
		player->iterateObjects( gatherBuildable, &s_buildables );
		s_gathered = TRUE;
		s_gatherFrame = frame;
		s_gatherPlayer = player;
	}

	// the tray closes for good once the selection leaves the squad, so a box round the same units
	// does not open it again
	s_shownSquad = chosenSquadSelected( player ) ? s_chosenSquad : -1;
	if( s_shownSquad < 0 )
		s_chosenSquad = -1;

	const Color accent = sideAccent();
	drawList( accent );
	drawTray( rightTop, accent );
}

//-------------------------------------------------------------------------------------------------
Bool GroupPanel_click( const ICoord2D *mouse, Bool cancel )
{
	auto inside = [ mouse ]( const IRegion2D &box )
	{
		return mouse->x >= box.lo.x && mouse->x < box.hi.x && mouse->y >= box.lo.y && mouse->y < box.hi.y;
	};

	// the tray's handle folds it away or brings it out; a right press on it does nothing
	if( s_handleShown && inside( s_handleBox ) )
	{
		if( !cancel || TheKeyboard->isCtrl() )
			s_trayOpen = !s_trayOpen;
		return TRUE;
	}

	for( Int r = 0; r < s_rowsBoxed; r++ )
	{
		if( !inside( s_rows[ r ].box ) )
			continue;

		// a right press is the list's and does nothing, so no order lands on the ground under it; while
		// the promotion screen is up the number keys are its columns, and a plate must not buy a science
		if( ( cancel && !TheKeyboard->isCtrl() ) || TheControlBar->isPurchaseScienceVisible() )
			return TRUE;

		// the number key's own message: SelectionTranslator selects the squad, and a second press inside
		// its double-tap time takes the camera there, exactly as the key does
		TheMessageStream->appendMessage( (GameMessage::Type)( GameMessage::MSG_META_SELECT_TEAM0 + s_rows[ r ].squad ) );
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void GroupPanel_squadChosen( Int squad )
{
	s_chosenSquad = isValidHotkeySquadIndex( squad ) ? squad : -1;
	s_chosenFrame = TheGameLogic->getFrame();
}
