// The group panel (GroupPanel.h).  On the left, every hotkey squad the local player has made, one row
// each against the screen's left edge between the message lines and the radar: its number, a cameo
// and a count for each unit type in it, the squad's health summed along the foot, a soft "+N" for the
// units still being built for it or walking to it, and a red edge while any member fights.  On the
// right, while a squad picked by its number is still the selection, a tray under the superweapons
// with one build button for every unit any of the player's factories offers, whose units come out
// into that squad.
//
// Thread: LOGIC-THREAD.md.  Everything here that reads the logic runs at the render's sync point:
// GroupPanel_draw from InGameUI::postDraw, which W3DInGameUI::recordDraw runs before the overlap
// window opens with the drawing recorded for the render to replay, and the clicks on the input path,
// which runs at the sync point too.  The build buttons are windows whose state is set there and only
// drawn inside the window.  Squad members are read through Squad::getObjectIDs and findObjectByID,
// never getLiveObjects or getAllObjects: those prune the logic's list, and doing it on one machine
// only would make the squads differ between machines.
//
// The tray drawing, the cell sizes and the radar's top are copies of InGameUI.cpp's (drawHudTray,
// hudCellStyle, radarFrameTop), which keeps them static.
// ponytail: copies, not shared; fold them into one header when InGameUI.cpp is free to change.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GroupPanel.h"

#include "Common/GameCommon.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
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
	enum
	{
		CAMEO_W						= 39,		///< a HUD cell's cameo at 800x600, InGameUI.cpp's HUD_CELL_CAMEO_W and _H
		CAMEO_H						= 27,
		TRAY_CELL_W				= 80,		///< the general's powers tray's cell and button at 1080: a cell keeps their margins
		TRAY_CELL_H				= 70,
		TRAY_BUTTON_W			= 75,
		TRAY_BUTTON_H			= 60,
		ROW_TYPES					= 5,		///< unit types one squad row shows, the most numerous first
		ROW_GAP						= 2,		///< between two rows, at 800x600
		PRODUCTION_LIFT		= 2,		///< InGameUI.cpp's PRODUCTION_LIST_LIFT: the production list stands under this one
		PRODUCTION_TAB_H	= 28,		///< and its "+N" tab over it, at 1080 (HUD_TRAY_TAB_H)
		PANEL_COLUMNS			= 4,		///< the build tray's width in cells, widened only when its rows would reach the bar
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

	struct Tray
	{
		const Image *cell, *open, *foot, *uncovered, *cornerFoot, *cornerUncovered, *step;
		Int openW, footH, uncoveredH;
	};

	struct Cells
	{
		Int cameoW, cameoH;			///< the picture or the button
		Int marginX, marginY;		///< from a cell's edge to the picture's
		Int cellW, cellH;
		const Image *plate;			///< under a readout
		Tray tray;
	};

	struct SquadType
	{
		const ThingTemplate *thing;
		Int count;
	};

	struct SquadRow
	{
		Int squad;
		SquadType types[ ROW_TYPES ];
		Int typeCount;
		Real health, maxHealth;
		Bool fighting;
		Int coming;							///< Player::countGroupReinforcements
		IRegion2D box;					///< the whole row, set by the draw, taken by a click
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
	Int s_shownSquad = -1;				///< the squad the build tray is open for

	GameWindow *s_panel = NULL;
	GameWindow *s_buttons[ MAX_BUTTONS ];

	DisplayString *s_numberText[ NUM_HOTKEY_SQUADS ];
	DisplayString *s_comingText[ NUM_HOTKEY_SQUADS ];
	DisplayString *s_countText[ NUM_HOTKEY_SQUADS ][ ROW_TYPES ];
	DisplayString *s_headerText = NULL;
}

//-------------------------------------------------------------------------------------------------
/** An 800x600 length at the command bar's scale, InGameUI.cpp's stripPixels. */
//-------------------------------------------------------------------------------------------------
static Int stripPixels( Real nominal )
{
	return REAL_TO_INT_CEIL( nominal * ControlBarHudPageScale() );
}

static const char *sidePrefix( void )
{
	const AsciiString side = ThePlayerList->getLocalPlayer()->getSide();
	if( side.startsWith( "China" ) || side.startsWith( "Boss" ) )
		return "China";
	if( side.startsWith( "GLA" ) )
		return "Gla";
	if( side.startsWith( "Turkiye" ) )
		return "Turkiye";
	return "America";
}

static const Image *mirroredPiece( const char *prefix, const char *piece, const Image *standIn )
{
	AsciiString name;
	name.format( "%sProTray%s", prefix, piece );
	const Image *image = TheMappedImageCollection->findImageByName( name );
	return image != NULL ? image : standIn;
}

//-------------------------------------------------------------------------------------------------
/** Cells round a picture cameoW by cameoH in the general's powers tray's steel, the way
	* InGameUI.cpp's hudCellStyle builds them: the production list's tray against the left edge, the
	* superweapons' against the right. */
//-------------------------------------------------------------------------------------------------
static Cells cellsFor( Int cameoW, Int cameoH, Bool leftEdge )
{
	Cells c;
	c.cameoW = cameoW;
	c.cameoH = cameoH;
	c.marginX = REAL_TO_INT_CEIL( cameoW * ( TRAY_CELL_W - TRAY_BUTTON_W ) / ( 2.0f * TRAY_BUTTON_W ) );
	c.marginY = REAL_TO_INT_CEIL( cameoH * ( TRAY_CELL_H - TRAY_BUTTON_H ) / ( 2.0f * TRAY_BUTTON_H ) );
	c.cellW = cameoW + 2 * c.marginX;
	c.cellH = cameoH + 2 * c.marginY;

	const char *prefix = sidePrefix();
	AsciiString name;
	name.format( "%sProHudCellPlate", prefix );
	c.plate = TheMappedImageCollection->findImageByName( name );

	const Tray none = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0 };
	c.tray = none;
	const Image *const *tray = TheControlBar->getControlBarSchemeManager()->getPowerTray();
	if( tray == NULL || tray[ ControlBarScheme::TRAY_CELL ] == NULL )
		return c;

	const Image *cell = tray[ ControlBarScheme::TRAY_CELL ];
	if( leftEdge )
	{
		const Tray production =
		{
			cell,
			mirroredPiece( prefix, "EdgeRight", tray[ ControlBarScheme::TRAY_EDGE_LEFT ] ),
			tray[ ControlBarScheme::TRAY_EDGE_BOTTOM ],
			tray[ ControlBarScheme::TRAY_EDGE_TOP ],
			mirroredPiece( prefix, "CornerBottomRight", tray[ ControlBarScheme::TRAY_CORNER_BL ] ),
			mirroredPiece( prefix, "CornerTopRight", tray[ ControlBarScheme::TRAY_CORNER_TL ] ),
			mirroredPiece( prefix, "StepRight", tray[ ControlBarScheme::TRAY_STEP ] ),
			0, 0, 0
		};
		c.tray = production;
	}
	else
	{
		const Tray superweapon =
		{
			cell,
			tray[ ControlBarScheme::TRAY_EDGE_LEFT ],
			tray[ ControlBarScheme::TRAY_EDGE_TOP ],
			tray[ ControlBarScheme::TRAY_EDGE_BOTTOM ],
			tray[ ControlBarScheme::TRAY_CORNER_TL ],
			tray[ ControlBarScheme::TRAY_CORNER_BL ],
			mirroredPiece( prefix, "StepDown", tray[ ControlBarScheme::TRAY_STEP ] ),
			0, 0, 0
		};
		c.tray = superweapon;
	}

	const Real scaleX = (Real)c.cellW / cell->getImageWidth();
	const Real scaleY = (Real)c.cellH / cell->getImageHeight();
	c.tray.openW = c.tray.open ? REAL_TO_INT_CEIL( c.tray.open->getImageWidth() * scaleX ) : 0;
	c.tray.footH = c.tray.foot ? REAL_TO_INT_CEIL( c.tray.foot->getImageHeight() * scaleY ) : 0;
	c.tray.uncoveredH = c.tray.uncovered ? REAL_TO_INT_CEIL( c.tray.uncovered->getImageHeight() * scaleY ) : 0;
	return c;
}

//-------------------------------------------------------------------------------------------------
/** InGameUI.cpp's drawHudTray: cell i is row i / columns, place i % columns in from the screen edge;
	* `openSide` +1 puts the open side on the right, `footSide` +1 puts row 0's outer edge below. */
//-------------------------------------------------------------------------------------------------
static void drawTray( const Tray &tray, const IRegion2D *cells, Int count, Int columns, Int openSide, Int footSide )
{
	if( tray.cell == NULL || count == 0 )
		return;

	const Int rows = ( count + columns - 1 ) / columns;
	auto rowLength = [ & ]( Int row ) { return row >= 0 && row < rows ? min( columns, count - row * columns ) : 0; };
	auto piece = []( const Image *image, Int lx, Int ly, Int hx, Int hy )
	{
		if( image != NULL && hx > lx && hy > ly )
			TheDisplay->drawImage( image, lx, ly, hx, hy );
	};

	for( Int each = 0; each < count; each++ )
	{
		const IRegion2D &c = cells[ each ];
		const Int row = each / columns, place = each % columns;
		const Bool innermost = place == rowLength( row ) - 1;
		const Bool covered = place < rowLength( row + 1 );
		const Bool stepHere = innermost && row > 0 && place + 1 < rowLength( row - 1 );
		const Bool stepBeside = !covered && place > 0 && place - 1 < rowLength( row + 1 );

		const Int openLo = openSide > 0 ? c.hi.x : c.lo.x - tray.openW;
		const Int openHi = openLo + tray.openW;
		const Int footLo = footSide > 0 ? c.hi.y : c.lo.y - tray.footH;
		const Int footHi = footLo + tray.footH;
		const Int farLo = footSide > 0 ? c.lo.y - tray.uncoveredH : c.hi.y;
		const Int farHi = farLo + tray.uncoveredH;
		const Int stepLo = footSide > 0 ? c.hi.y - tray.uncoveredH : c.lo.y;
		const Int stepHi = stepLo + tray.uncoveredH;
		const Int notch = stepHere ? tray.uncoveredH : 0;
		const Int shorten = stepBeside ? tray.openW : 0;

		piece( tray.cell, c.lo.x, c.lo.y, c.hi.x, c.hi.y );
		if( !covered )
			piece( tray.uncovered, c.lo.x + ( openSide > 0 ? shorten : 0 ), farLo, c.hi.x - ( openSide > 0 ? 0 : shorten ), farHi );
		if( innermost )
		{
			piece( tray.open, openLo, c.lo.y + ( footSide > 0 ? 0 : notch ), openHi, c.hi.y - ( footSide > 0 ? notch : 0 ) );
			if( stepHere )
				piece( tray.step, openLo, stepLo, openHi, stepHi );
			if( !covered )
				piece( tray.cornerUncovered, openLo, farLo, openHi, farHi );
		}
		if( row == 0 )
		{
			piece( tray.foot, c.lo.x, footLo, c.hi.x, footHi );
			if( innermost )
				piece( tray.cornerFoot, openLo, footLo, openHi, footHi );
		}
	}
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

//-------------------------------------------------------------------------------------------------
/** Each squad's live members by type, health and fight, read from the logic's own list. */
//-------------------------------------------------------------------------------------------------
static void gatherSquads( Player *player )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt fight = COMBAT_SECONDS * LOGICFRAMES_PER_SECOND;
	auto recent = [ now, fight ]( UnsignedInt frame ) { return frame != 0 && frame <= now && now - frame <= fight; };

	s_rowCount = 0;
	for( Int k = 0; k < NUM_HOTKEY_SQUADS; k++ )
	{
		const Int squad = KEY_ORDER[ k ];
		const Squad *members = player->getHotkeySquad( squad );
		if( members == NULL )
			continue;

		SquadRow row;
		row.squad = squad;
		row.typeCount = 0;
		row.health = 0.0f;
		row.maxHealth = 0.0f;
		row.fighting = FALSE;
		row.coming = 0;
		row.box.lo.x = row.box.lo.y = row.box.hi.x = row.box.hi.y = 0;

		std::vector< SquadType > types;
		const VecObjectID &ids = members->getObjectIDs();
		for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
		{
			const Object *obj = TheGameLogic->findObjectByID( *it );
			// one on its way from the factory is the row's "+N", not one of its pictures yet
			if( obj == NULL || obj->isEffectivelyDead() || obj->getReinforcementSquad() >= 0 )
				continue;

			const ThingTemplate *thing = obj->getTemplate();
			size_t at = 0;
			while( at < types.size() && types[ at ].thing != thing )
				at++;
			if( at == types.size() )
			{
				const SquadType added = { thing, 0 };
				types.push_back( added );
			}
			types[ at ].count++;

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
		if( types.empty() )
			continue;

		// ponytail: a sixth type and beyond go unshown; a "+" cell for them if players build mixed squads that wide
		std::stable_sort( types.begin(), types.end(), []( const SquadType &a, const SquadType &b ) { return a.count > b.count; } );
		row.typeCount = min( (Int)types.size(), (Int)ROW_TYPES );
		for( Int t = 0; t < row.typeCount; t++ )
			row.types[ t ] = types[ t ];
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
		if( button == NULL || button->getCommandType() != GUI_COMMAND_UNIT_BUILD || button->getThingTemplate() != slots->thing )
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
// The build tray's windows: a parent over the whole tray that takes the clicks between the buttons
// and hears the buttons' presses, and MAX_BUTTONS push buttons in it drawn as the command bar draws
// its own (the cameo, the build border, greyed when the bar would grey it) with the bar's build
// tooltip.  They are made on first use and again whenever the window manager has destroyed them.
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

static void hidePanel( void )
{
	s_shownSquad = -1;
	if( panelAlive() )
		s_panel->winHide( TRUE );
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
/** The squad list, rows against the left edge centred in the room between the message lines and the
	* production list over the radar; rows that do not fit shrink together. */
//-------------------------------------------------------------------------------------------------
static void drawList( void )
{
	if( s_rowCount == 0 )
		return;

	const Cells base = cellsFor( stripPixels( CAMEO_W ), stripPixels( CAMEO_H ), TRUE );
	auto rowHeight = []( const Cells &c ) { return c.tray.uncoveredH + c.cellH + c.tray.footH; };
	const Int screenH = (Int)TheDisplay->getHeight();
	const Int gap = stripPixels( ROW_GAP );
	const Int top = REAL_TO_INT( screenH * LIST_TOP_SHARE );
	const Int bottom = radarFrameTop() - stripPixels( PRODUCTION_LIFT ) - rowHeight( base )
										 - REAL_TO_INT_CEIL( PRODUCTION_TAB_H * screenH / 1080.0f ) - gap;
	const Int room = max( bottom - top, 1 );

	Cells cells = base;
	Int total = s_rowCount * ( rowHeight( cells ) + gap ) - gap;
	for( Real scale = 1.0f; total > room && scale > 0.34f; )
	{
		scale = max( scale * min( (Real)room / total, 0.95f ), 0.33f );
		cells = cellsFor( REAL_TO_INT_FLOOR( base.cameoW * scale ), REAL_TO_INT_FLOOR( base.cameoH * scale ), TRUE );
		total = s_rowCount * ( rowHeight( cells ) + gap ) - gap;
	}

	const Int firstTop = top + max( ( room - total ) / 2, 0 );
	const Int numberPoints = REAL_TO_INT( cells.cameoH * 0.45f );
	const Int countPoints = REAL_TO_INT( cells.cameoH * 0.26f );
	const Color white = GameMakeColor( 245, 245, 245, 255 );
	const Color gold = GameMakeColor( 255, 210, 60, 255 );
	const Color soft = GameMakeColor( 165, 172, 180, 255 );
	const Real phase = (Real)( TheGameLogic->getFrame() % LOGICFRAMES_PER_SECOND ) / LOGICFRAMES_PER_SECOND;
	const Int fightAlpha = REAL_TO_INT( 150.0f + 105.0f * ( 0.5f + 0.5f * cosf( 2.0f * PI * phase ) ) );

	for( Int r = 0; r < s_rowCount; r++ )
	{
		SquadRow &row = s_rows[ r ];
		const Int rowTop = firstTop + r * ( rowHeight( cells ) + gap );
		const Int count = 1 + row.typeCount;
		IRegion2D box[ 1 + ROW_TYPES ], cameo[ 1 + ROW_TYPES ];
		for( Int c = 0; c < count; c++ )
		{
			box[ c ].lo.x = c * cells.cellW;
			box[ c ].hi.x = box[ c ].lo.x + cells.cellW;
			box[ c ].lo.y = rowTop + cells.tray.uncoveredH;
			box[ c ].hi.y = box[ c ].lo.y + cells.cellH;
			cameo[ c ].lo.x = box[ c ].lo.x + cells.marginX;
			cameo[ c ].hi.x = box[ c ].hi.x - cells.marginX;
			cameo[ c ].lo.y = box[ c ].lo.y + cells.marginY;
			cameo[ c ].hi.y = box[ c ].hi.y - cells.marginY;
		}
		row.box.lo.x = 0;
		row.box.lo.y = rowTop;
		row.box.hi.x = box[ count - 1 ].hi.x + cells.tray.openW;
		row.box.hi.y = rowTop + rowHeight( cells );

		TheDisplay->beginBatch2D();
		drawTray( cells.tray, box, count, count, 1, 1 );
		for( Int t = 0; t < row.typeCount; t++ )
		{
			const ThingTemplate *thing = row.types[ t ].thing;
			const Image *portrait = thing->getButtonImage() ? thing->getButtonImage() : thing->getSelectedPortraitImage();
			const IRegion2D &at = cameo[ t + 1 ];
			if( portrait != NULL )
				TheDisplay->drawImage( portrait, at.lo.x, at.lo.y, at.hi.x, at.hi.y );
		}
		// the squad's health, summed, along the foot of its pictures
		if( row.maxHealth > 0.0f )
		{
			const Real share = max( min( row.health / row.maxHealth, 1.0f ), 0.0f );
			const Int barH = max( cells.cameoH / 9, 2 );
			const Int left = cameo[ 1 ].lo.x, width = cameo[ count - 1 ].hi.x - left, barTop = cameo[ 1 ].hi.y - barH;
			const Color fill = share > 0.5f ? GameMakeColor( 70, 200, 60, 255 )
											 : ( share > 0.25f ? GameMakeColor( 230, 200, 40, 255 ) : GameMakeColor( 225, 45, 30, 255 ) );
			TheDisplay->drawFillRect( left, barTop, width, barH, GameMakeColor( 0, 0, 0, 200 ) );
			const Int filled = REAL_TO_INT_CEIL( width * share );
			if( filled > 0 )
				TheDisplay->drawFillRect( left, barTop, filled, barH, fill );
		}
		TheDisplay->endBatch2D();

		// a member hit or firing: the row's edge burns red, breathing on the logic clock
		if( row.fighting )
			TheDisplay->drawOpenRect( row.box.lo.x, row.box.lo.y, row.box.hi.x - row.box.lo.x, row.box.hi.y - row.box.lo.y,
																(Real)max( cells.marginX / 2, 2 ), GameMakeColor( 230, 40, 30, (UnsignedByte)fightAlpha ) );

		UnicodeString text;
		text.format( u"%d", row.squad );
		drawCentred( keptString( s_numberText[ row.squad ], text, numberPoints ), cameo[ 0 ],
								 row.squad == s_shownSquad ? gold : white );
		if( row.coming > 0 )
		{
			text.format( u"+%d", row.coming );
			HudReadout_draw( keptString( s_comingText[ row.squad ], text, countPoints ), cameo[ 0 ], HUD_READOUT_TOP_RIGHT,
											 soft, cells.plate );
		}
		for( Int t = 0; t < row.typeCount; t++ )
		{
			if( row.types[ t ].count < 2 )
				continue;
			text.format( u"x%d", row.types[ t ].count );
			HudReadout_draw( keptString( s_countText[ row.squad ][ t ], text, countPoints ), cameo[ t + 1 ], HUD_READOUT_TOP_RIGHT,
											 white, cells.plate );
		}
	}
	s_rowsBoxed = s_rowCount;
}

//-------------------------------------------------------------------------------------------------
/** The build tray under the superweapons: the squad's number in the cell at the screen edge, then a
	* button for each unit, the cells as big as the command bar's own buttons. */
//-------------------------------------------------------------------------------------------------
static void layoutPanel( Int top )
{
	if( !isValidHotkeySquadIndex( s_shownSquad ) || s_buildables.empty() )
	{
		hidePanel();
		return;
	}
	GameWindow *panel = panelWindow();
	if( panel == NULL )
		return;

	Int cameoW = stripPixels( CAMEO_W ), cameoH = stripPixels( CAMEO_H );
	static NameKeyType barButtonKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonCommand01" );
	GameWindow *barButton = TheWindowManager->winGetWindowFromId( NULL, barButtonKey );
	if( barButton != NULL )
	{
		Int w = 0, h = 0;
		barButton->winGetSize( &w, &h );
		if( w > 0 && h > 0 )
		{
			cameoW = w;
			cameoH = h;
		}
	}
	const Cells cells = cellsFor( cameoW, cameoH, FALSE );

	const Int shown = min( (Int)s_buildables.size(), (Int)MAX_BUTTONS );
	const Int count = 1 + shown;
	const Int bottom = controlBarTop( ControlBar::CB_PANEL_RIGHT );
	auto height = [ & ]( Int columns ) { return cells.tray.footH + ( count + columns - 1 ) / columns * cells.cellH + cells.tray.uncoveredH; };
	Int columns = PANEL_COLUMNS;
	while( columns < PANEL_MAX_COLUMNS && top + height( columns ) > bottom )
		columns++;
	columns = min( columns, count );

	const Int right = (Int)TheDisplay->getWidth();
	IRegion2D box[ 1 + MAX_BUTTONS ];
	for( Int each = 0; each < count; each++ )
	{
		box[ each ].hi.x = right - each % columns * cells.cellW;
		box[ each ].lo.x = box[ each ].hi.x - cells.cellW;
		box[ each ].lo.y = top + cells.tray.footH + each / columns * cells.cellH;
		box[ each ].hi.y = box[ each ].lo.y + cells.cellH;
	}

	TheDisplay->beginBatch2D();
	drawTray( cells.tray, box, count, columns, -1, -1 );
	TheDisplay->endBatch2D();

	IRegion2D header = box[ 0 ];
	header.lo.x += cells.marginX;
	header.hi.x -= cells.marginX;
	header.lo.y += cells.marginY;
	header.hi.y -= cells.marginY;
	UnicodeString number;
	number.format( u"%d", s_shownSquad );
	drawCentred( keptString( s_headerText, number, REAL_TO_INT( cells.cameoH * 0.5f ) ), header, GameMakeColor( 255, 210, 60, 255 ) );

	// the parent covers the tray, open edge and all, so a click between the buttons stays off the ground
	const Int panelX = right - columns * cells.cellW - cells.tray.openW;
	panel->winSetPosition( panelX, top );
	panel->winSetSize( right - panelX, height( columns ) );
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

		const Buildable &entry = s_buildables[ i ];
		const IRegion2D &cell = box[ i + 1 ];
		button->winSetPosition( cell.lo.x + cells.marginX - panelX, cell.lo.y + cells.marginY - top );
		button->winSetSize( cells.cameoW, cells.cameoH );
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

	drawList();
	layoutPanel( rightTop );
}

//-------------------------------------------------------------------------------------------------
Bool GroupPanel_click( const ICoord2D *mouse, Bool cancel )
{
	for( Int r = 0; r < s_rowsBoxed; r++ )
	{
		const IRegion2D &box = s_rows[ r ].box;
		if( mouse->x < box.lo.x || mouse->x >= box.hi.x || mouse->y < box.lo.y || mouse->y >= box.hi.y )
			continue;

		// a right press is the list's and does nothing, so no order lands on the ground under it; while
		// the promotion screen is up the number keys are its columns, and a row must not buy a science
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
