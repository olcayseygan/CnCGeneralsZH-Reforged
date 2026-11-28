// The Key Grid layout (HUD_LAYOUT_GRID, the owner's of 2026-11-27, after the owner's V21 mockups):
// the Reforged bar with its commands as three staggered keyboard rows of six on a deck that hangs off the
// radar box, the box cut to the map's own shape.  The pure half (where everything stands, which structure
// the rows open with) and ControlBar's half: ControlBar.wnd's windows put on those rectangles after
// layoutPanels' Classic pass, through placeWindowAt only, as the sidebar does, and the console's art
// drawn under them.  Client only: nothing here reaches the logic.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/AudioEventRTS.h"
#include "Common/FunctionLexicon.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/OptionsCatalog.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/Radar.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarKeyGrid.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/ControlBarSidebar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GroupPanel.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include <map>
#include <set>
#include <string>

//-------------------------------------------------------------------------------------------------
// The pure half
//-------------------------------------------------------------------------------------------------

/// today's radar glass at 1080, whose area every map's glass keeps
static const Int KEY_GRID_GLASS_AREA = 322 * 232;

void KeyGrid_glassForAspect( Real aspect, Int *width, Int *height )
{
	if( aspect <= 0.0f )
		aspect = 322.0f / 232.0f;
	aspect = MAX( 0.5f, MIN( aspect, 2.0f ) );
	*width = MAX( 1, REAL_TO_INT( sqrtf( KEY_GRID_GLASS_AREA * aspect ) + 0.5f ) );
	*height = MAX( 1, REAL_TO_INT( (Real)KEY_GRID_GLASS_AREA / *width + 0.5f ) );
}

//-------------------------------------------------------------------------------------------------
void KeyGridLayoutForScreen( Int screenWidth, Int screenHeight, const IRegion2D &area, Real scale, Real mapAspect,
	KeyGridRects *out )
{
	if( out == NULL )
		return;
	memset( out, 0, sizeof( *out ) );
	if( screenWidth <= 0 || screenHeight <= 0 || scale <= 0.0f )
		return;
	out->scale = scale;

	// a 1080 rectangle to the screen: x from the area's left edge, y from the screen's foot
	const Real left = (Real)area.lo.x, foot = (Real)screenHeight;
	auto put = [ & ]( IRegion2D *r, Real x0, Real y0, Real x1, Real y1 )
	{
		r->lo.x = REAL_TO_INT_FLOOR( left + x0 * scale + 0.5f );
		r->lo.y = REAL_TO_INT_FLOOR( foot - ( KEY_GRID_DESIGN_H - y0 ) * scale + 0.5f );
		r->hi.x = MAX( r->lo.x + 1, REAL_TO_INT_FLOOR( left + x1 * scale + 0.5f ) );
		r->hi.y = MAX( r->lo.y + 1, REAL_TO_INT_FLOOR( foot - ( KEY_GRID_DESIGN_H - y1 ) * scale + 0.5f ) );
	};

	// mock_v21.py's v21(), number for number
	Int gw, gh;
	KeyGrid_glassForAspect( mapAspect, &gw, &gh );
	const Real glassTop = 1064.0f - gh;
	const Real bw = gw + 23.0f, boxTop = glassTop - 20.0f;
	const Real sw = MAX( bw, 345.0f ), shelfTop = boxTop - 48.0f;
	const Real sx0 = bw - 6.0f, sx1 = bw + 34.0f;
	const Real kx0 = sx1 + 8.0f;
	put( &out->glass, 5, glassTop, 5.0f + gw, 1064 );
	put( &out->box, 0, boxTop, bw, 1080 );
	put( &out->shelf, 0, shelfTop, sw, boxTop + 2.0f );
	put( &out->signals, 11, shelfTop + 10.0f, 51.0f + 44.0f * ( KEY_GRID_SIGNAL_KEYS - 1 ), shelfTop + 40.0f );
	for( Int i = 0; i < KEY_GRID_SIGNAL_KEYS; i++ )
		put( &out->signalKeys[ i ], 11.0f + 44.0f * i, shelfTop + 10.0f, 51.0f + 44.0f * i, shelfTop + 40.0f );
	for( Int i = 0; i < KEY_GRID_SHELF_KEYS; i++ )
		put( &out->shelfKeys[ i ], 156.0f + 46.0f * i, shelfTop + 9.0f, 200.0f + 46.0f * i, shelfTop + 43.0f );
	put( &out->spine, sx0, shelfTop + 24.0f, sx1, 1080 );
	// the squad switch on the spine between the shelf and the head (hud_key_grid.py's rects(), the
	// artist's: room only while the box's top stands at 740 or above, a map of aspect 0.81 or taller); a
	// lower box (today's glass and wider) puts it on the head's end over the spine, left of the money
	if( boxTop + 2.0f <= 742.0f )
		put( &out->deckSwitch, sx0 + 2.0f, 742, sx0 + 38.0f, 776 );
	else
		put( &out->deckSwitch, sx0 + 2.0f, 784, sx0 + 38.0f, 818 );
	put( &out->head, sx0, 780, sx1 + 186.0f, 822 );
	put( &out->money, sx1 + 18.0f, 787, sx1 + 170.0f, 815 );
	put( &out->meter, sx0 + 15.0f, 832, sx0 + 25.0f, 1066 );

	// the rows: 90x72 keys 100 apart, rows 88 apart from 830, the A row in by a quarter key and Z by three
	static const Real STAGGER[ 3 ] = { 0.0f, 25.0f, 75.0f };
	for( Int place = 0; place < COMMAND_PLACE_COUNT; place++ )
	{
		const Int row = place / COMMAND_PLACE_COLUMNS, column = place % COMMAND_PLACE_COLUMNS;
		const Real x = kx0 + STAGGER[ row ] + 100.0f * column, y = 830.0f + 88.0f * row;
		put( &out->cells[ place ], x, y, x + 90.0f, y + 72.0f );
	}
	const Real rowsRight = kx0 + STAGGER[ 2 ] + 100.0f * ( COMMAND_PLACE_COLUMNS - 1 ) + 90.0f + 10.0f;
	put( &out->deck, sx0, 780, rowsRight, 1080 );
	put( &out->rows, kx0 - 10.0f, 824, rowsRight, 1080 );
}

//-------------------------------------------------------------------------------------------------
// The structures' facts, read from the templates once: how deep each stands in the prerequisites and
// how many objects name it in theirs (order.py's depth_fn and ref_counts)
//-------------------------------------------------------------------------------------------------
static std::map< const ThingTemplate *, Int > s_refs;
static std::map< const ThingTemplate *, Int > s_depth;
static const ThingTemplate *s_factsFor = NULL;

/// a prerequisite line's alternatives, 0 for a science line
static Int prerequisiteAlternatives( const ProductionPrerequisite *line, const ThingTemplate **alternatives, Int most )
{
	return line ? line->getAllPossibleBuildFacilityTemplates( alternatives, most ) : 0;
}

/// the template as this map plays it: a map.ini's edit is an override hanging off the head the buttons name
static const ThingTemplate *finalOf( const ThingTemplate *thing )
{
	return thing ? (const ThingTemplate *)thing->getFinalOverride() : NULL;
}

/// read again on the next ask: a new map can bring map.ini overrides with prerequisites of their own
static void forgetStructureFacts( void )
{
	s_factsFor = NULL;
	s_refs.clear();
	s_depth.clear();
}

static void readStructureFacts( void )
{
	const ThingTemplate *first = TheThingFactory ? TheThingFactory->firstTemplate() : NULL;
	if( first == s_factsFor )
		return;
	forgetStructureFacts();
	s_factsFor = first;
	for( const ThingTemplate *head = first; head != NULL; head = head->friend_getNextTemplate() )
	{
		const ThingTemplate *thing = finalOf( head );
		std::set< const ThingTemplate * > named;
		for( Int line = 0; line < thing->getPrereqCount(); line++ )
		{
			const ThingTemplate *alternatives[ 32 ];
			const Int count = prerequisiteAlternatives( thing->getNthPrereq( line ), alternatives, 32 );
			for( Int a = 0; a < count; a++ )
				if( alternatives[ a ] != NULL )
					named.insert( finalOf( alternatives[ a ] ) );
		}
		for( std::set< const ThingTemplate * >::const_iterator it = named.begin(); it != named.end(); ++it )
			s_refs[ *it ]++;
	}
}

/// 0 with no Object line, else one more than the deepest line's shallowest alternative; every template
/// met on the way is kept, by its final override, and a cycle counts 0 where it closes
static Int structureDepth( const ThingTemplate *head )
{
	const ThingTemplate *thing = finalOf( head );
	if( thing == NULL )
		return 0;
	std::map< const ThingTemplate *, Int >::const_iterator known = s_depth.find( thing );
	if( known != s_depth.end() )
		return known->second;
	s_depth[ thing ] = 0;
	Int deepest = -1;
	for( Int line = 0; line < thing->getPrereqCount(); line++ )
	{
		const ThingTemplate *alternatives[ 32 ];
		const Int count = prerequisiteAlternatives( thing->getNthPrereq( line ), alternatives, 32 );
		if( count <= 0 )
			continue;
		Int shallowest = 1 << 20;
		for( Int a = 0; a < count; a++ )
			shallowest = MIN( shallowest, structureDepth( alternatives[ a ] ) );
		deepest = MAX( deepest, shallowest );
	}
	const Int depth = deepest < 0 ? 0 : deepest + 1;
	s_depth[ thing ] = depth;
	return depth;
}

/// a GLA fake read as the building it imitates (FakeGLABarracks, Chem_FakeGLABarracks: GLABarracks)
static const ThingTemplate *realStructure( const ThingTemplate *thing )
{
	if( thing == NULL || !thing->isKindOf( KINDOF_FS_FAKE ) || TheThingFactory == NULL )
		return thing;
	const char *name = thing->getName().str();
	const char *underscore = strchr( name, '_' );
	const char *bare = AsciiString( name ).startsWithNoCase( "Fake" ) ? name : ( underscore ? underscore + 1 : name );
	if( !AsciiString( bare ).startsWithNoCase( "Fake" ) )
		return thing;
	const ThingTemplate *real = TheThingFactory->findTemplate( AsciiString( bare + 4 ), FALSE );
	return real ? real : thing;
}

static Int keyGridRole( const CommandButton *command )
{
	if( command == NULL || command->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT )
		return KEY_GRID_ROLE_OTHER;
	const ThingTemplate *thing = realStructure( command->getThingTemplate() );
	if( thing == NULL )
		return KEY_GRID_ROLE_EXTRA;
	if( thing->isKindOf( KINDOF_COMMANDCENTER ) )
		return KEY_GRID_ROLE_COMMAND_CENTER;
	if( thing->isKindOf( KINDOF_FS_BASE_DEFENSE ) || thing->isKindOf( KINDOF_DEMOTRAP ) )
		return KEY_GRID_ROLE_DEFENSE;
	if( thing->isKindOf( KINDOF_FS_POWER ) || thing->isKindOf( KINDOF_FS_SUPPLY_CENTER ) )
		return KEY_GRID_ROLE_ECONOMY;
	if( thing->isKindOf( KINDOF_FS_FACTORY ) || thing->isKindOf( KINDOF_FS_BARRACKS )
			|| thing->isKindOf( KINDOF_FS_WARFACTORY ) || thing->isKindOf( KINDOF_FS_AIRFIELD ) )
		return KEY_GRID_ROLE_BUILD;
	return KEY_GRID_ROLE_EXTRA;
}

/// one slot's facts for ControlBar_keyGridPlaces; NULL is an empty slot
static void keyGridSlotOf( const CommandButton *command, KeyGridSlot *slot )
{
	slot->type = command ? command->getCommandType() : GUI_COMMAND_NONE;
	slot->group = command ? ControlBar_commandGroup( command ) : COMMAND_GROUP_ABILITY;
	slot->pinned = command ? ControlBar_namedCommandPlace( command->getName().str() ) : -1;
	slot->role = keyGridRole( command );
	slot->depth = 0;
	slot->refs = 0;
	slot->mirror = -1;
	if( slot->role == KEY_GRID_ROLE_OTHER )
		return;
	const ThingTemplate *thing = command->getThingTemplate();
	slot->depth = structureDepth( thing );
	const ThingTemplate *real = realStructure( thing );
	if( real == NULL || !real->isKindOf( KINDOF_FS_SUPERWEAPON ) )
	{
		std::map< const ThingTemplate *, Int >::const_iterator refs = s_refs.find( finalOf( thing ) );
		slot->refs = refs != s_refs.end() ? refs->second : 0;
	}
}

/// a structure's name in lower case with its first "fake" taken out, which a fake and its real building share
static AsciiString mirrorKey( const ThingTemplate *thing )
{
	if( thing == NULL )
		return AsciiString::TheEmptyString;
	AsciiString lower = thing->getName();
	lower.toLower();
	const char *s = lower.str();
	const char *fake = strstr( s, "fake" );
	if( fake == NULL )
		return lower;
	std::string out( s, fake - s );
	out += fake + 4;
	return AsciiString( out.c_str() );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::getKeyGridPlaces( Int *places ) const
{
	// A transport's seat with nobody in it: EA shows every seat's exit button, the empty ones on the
	// empty-seat picture, and updateContextCommand enables them all each frame, so the window's state
	// says nothing.  A passenger is a window doTransportInventoryUI wrote into m_containData; the rest
	// are empty seats, which take no place under Key Grid and fold away (arrangeKeyGrid)
	auto emptySeat = [ & ]( GameWindow *window ) -> Bool
	{
		const CommandButton *command = (const CommandButton *)GadgetButtonGetData( window );
		if( command == NULL || command->getCommandType() != GUI_COMMAND_EXIT_CONTAINER )
			return FALSE;
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			if( m_containData[ i ].control == window && m_containData[ i ].objectID != INVALID_ID )
				return FALSE;
		return TRUE;
	};
	const Bool groupShown = !m_contextParent[ CP_COMMAND ]->winIsHidden();
	const CommandButton *commands[ MAX_COMMANDS_PER_SET ];
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
	{
		GameWindow *window = m_commandWindows[ slot ];
		commands[ slot ] = ( groupShown && window && !BitTest( window->winGetStatus(), WIN_STATUS_HIDDEN ) && !emptySeat( window ) )
			? (const CommandButton *)GadgetButtonGetData( window ) : NULL;
	}
	const Object *obj = m_currentSelectedDrawable ? m_currentSelectedDrawable->getObject() : NULL;

	// asked a few times a frame (the arrangement, the empty keys' letters, a tooltip): the same buttons
	// for the same object are the same places
	Bool same = m_keyGridPlacesKnown && obj == m_keyGridPlacedOwner;
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET && same; slot++ )
		same = commands[ slot ] == m_keyGridPlacedCommands[ slot ];
	if( same )
	{
		for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
			places[ slot ] = m_keyGridPlaces[ slot ];
		return m_keyGridPlacesFight;
	}

	readStructureFacts();
	KeyGridSlot slots[ MAX_COMMANDS_PER_SET ];
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
		keyGridSlotOf( commands[ slot ], &slots[ slot ] );

	// The GLA worker's fake buildings stand where their real ones do, so H never moves a building: the
	// real set's places, worked out as the bar would show it, lend each fake its real building's
	static const char FAKE_SET[] = "FakeBuildingsCommandSet";
	if( obj != NULL && obj->getCommandSetString().endsWithNoCase( FAKE_SET ) )
	{
		const AsciiString &fakeName = obj->getCommandSetString();
		std::string stem( fakeName.str(), fakeName.getLength() - ( sizeof( FAKE_SET ) - 1 ) );
		stem += "CommandSet";
		const AsciiString realName( stem.c_str() );
		const CommandSet *real = const_cast< ControlBar * >( this )->findCommandSet( realName );
		if( real != NULL )
		{
			KeyGridSlot realSlots[ MAX_COMMANDS_PER_SET ];
			const CommandButton *realCommands[ MAX_COMMANDS_PER_SET ];
			for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
			{
				const CommandButton *command = m_commandWindows[ slot ] ? real->getCommandButton( slot ) : NULL;
				if( command != NULL && BitTest( command->getOptions(), SCRIPT_ONLY ) )
					command = NULL;
				realCommands[ slot ] = command;
				keyGridSlotOf( command, &realSlots[ slot ] );
			}
			Int realPlaces[ MAX_COMMANDS_PER_SET ];
			ControlBar_keyGridPlaces( realSlots, MAX_COMMANDS_PER_SET, realPlaces );
			AsciiString realKeys[ MAX_COMMANDS_PER_SET ];
			for( Int other = 0; other < MAX_COMMANDS_PER_SET; other++ )
				if( realSlots[ other ].role != KEY_GRID_ROLE_OTHER && realPlaces[ other ] >= 0 )
					realKeys[ other ] = mirrorKey( realCommands[ other ]->getThingTemplate() );
			for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
			{
				if( slots[ slot ].role == KEY_GRID_ROLE_OTHER )
					continue;
				const AsciiString key = mirrorKey( commands[ slot ]->getThingTemplate() );
				for( Int other = 0; other < MAX_COMMANDS_PER_SET; other++ )
					if( realKeys[ other ].isNotEmpty() && realKeys[ other ] == key )
						slots[ slot ].mirror = realPlaces[ other ];
			}
		}
	}
	const Bool fights = ControlBar_keyGridPlaces( slots, MAX_COMMANDS_PER_SET, places );
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
	{
		m_keyGridPlacedCommands[ slot ] = commands[ slot ];
		m_keyGridPlaces[ slot ] = places[ slot ];
	}
	m_keyGridPlacedOwner = obj;
	m_keyGridPlacesFight = fights;
	m_keyGridPlacesKnown = TRUE;
	return fights;
}

//-------------------------------------------------------------------------------------------------
// ControlBar's half
//-------------------------------------------------------------------------------------------------

static GameWindow *barWindow( const char *name )
{
	AsciiString full;
	full.format( "ControlBar.wnd:%s", name );
	return TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( full ) );
}

//-------------------------------------------------------------------------------------------------
// The shelf's four tool keys in the side's Key Grid art (<prefix>KeyGridTool<Key>{,H,P,I}), the
// scheme's own put back for every other layout (restoreClassicWindows) unless a scheme set since gave
// the key its own already
//-------------------------------------------------------------------------------------------------
static const char *const s_toolWindow[ KEY_GRID_SHELF_KEYS ] = { "ButtonOptions", "ButtonPlaceBeacon", "ButtonIdleWorker", "PopupCommunicator" };
static const Image *s_toolClassic[ KEY_GRID_SHELF_KEYS ][ 4 ];
static const Image *s_toolWorn[ KEY_GRID_SHELF_KEYS ][ 2 ];		///< the Key Grid art a key wears, and its lit state

static void wearToolArt( Int tool, const ControlBarSchemeManager *schemes )
{
	GameWindow *win = barWindow( s_toolWindow[ tool ] );
	const Image *const *art = schemes ? schemes->getKeyGridPieces() : NULL;
	const Image *const *states = art ? art + ControlBarScheme::KEY_GRID_TOOL_FIRST + 4 * tool : NULL;
	if( win == NULL || states == NULL || states[ 0 ] == NULL )
		return;
	const Image *now = GadgetButtonGetEnabledImage( win );
	const Image *lit = states[ 1 ] ? states[ 1 ] : states[ 0 ];
	if( now != states[ 0 ] && now != lit )
	{
		s_toolClassic[ tool ][ 0 ] = GadgetButtonGetEnabledImage( win );
		s_toolClassic[ tool ][ 1 ] = GadgetButtonGetHiliteImage( win );
		s_toolClassic[ tool ][ 2 ] = GadgetButtonGetHiliteSelectedImage( win );
		s_toolClassic[ tool ][ 3 ] = GadgetButtonGetDisabledImage( win );
	}
	GadgetButtonSetEnabledImage( win, states[ 0 ] );
	GadgetButtonSetHiliteImage( win, lit );
	GadgetButtonSetHiliteSelectedImage( win, states[ 2 ] ? states[ 2 ] : lit );
	GadgetButtonSetDisabledImage( win, states[ 3 ] ? states[ 3 ] : states[ 0 ] );
	s_toolWorn[ tool ][ 0 ] = states[ 0 ];
	s_toolWorn[ tool ][ 1 ] = lit;
}

void ControlBar_restoreKeyGridKeys( void )
{
	for( Int tool = 0; tool < KEY_GRID_SHELF_KEYS; tool++ )
	{
		GameWindow *win = s_toolWorn[ tool ][ 0 ] ? barWindow( s_toolWindow[ tool ] ) : NULL;
		const Image *now = win != NULL ? GadgetButtonGetEnabledImage( win ) : NULL;
		if( win != NULL && ( now == s_toolWorn[ tool ][ 0 ] || now == s_toolWorn[ tool ][ 1 ] ) )
		{
			GadgetButtonSetEnabledImage( win, s_toolClassic[ tool ][ 0 ] );
			GadgetButtonSetHiliteImage( win, s_toolClassic[ tool ][ 1 ] );
			GadgetButtonSetHiliteSelectedImage( win, s_toolClassic[ tool ][ 2 ] );
			GadgetButtonSetDisabledImage( win, s_toolClassic[ tool ][ 3 ] );
		}
		s_toolWorn[ tool ][ 0 ] = s_toolWorn[ tool ][ 1 ] = NULL;
	}
}

/// the frame's travel since layoutPanels put it (a minimised or sliding bar), which everything here follows
static ICoord2D frameTravel( GameWindow *frame, const ICoord2D &origin )
{
	ICoord2D d = { 0, 0 };
	if( frame == NULL )
		return d;
	frame->winGetScreenPosition( &d.x, &d.y );
	d.x -= origin.x;
	d.y -= origin.y;
	return d;
}

static IRegion2D moved( const IRegion2D &r, const ICoord2D &d )
{
	IRegion2D out = r;
	out.lo.x += d.x;
	out.hi.x += d.x;
	out.lo.y += d.y;
	out.hi.y += d.y;
	return out;
}

/// a rectangle of no size at r's bottom left corner
static IRegion2D folded( const IRegion2D &r )
{
	IRegion2D out;
	out.lo.x = out.hi.x = r.lo.x;
	out.lo.y = out.hi.y = r.hi.y;
	return out;
}

static IRegion2D joined( const IRegion2D &a, const IRegion2D &b )
{
	IRegion2D out;
	out.lo.x = MIN( a.lo.x, b.lo.x );
	out.lo.y = MIN( a.lo.y, b.lo.y );
	out.hi.x = MAX( a.hi.x, b.hi.x );
	out.hi.y = MAX( a.hi.y, b.hi.y );
	return out;
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::wantsKeyGridLayout( void ) const
{
	if( TheGlobalData == NULL || TheGlobalData->m_hudLayout != HUD_LAYOUT_GRID )
		return FALSE;
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame()
			|| TheGameLogic->isInReplayGame() )
		return FALSE;
	// the windows are looked up after initWindows' first layout; update() lays Key Grid out after
	if( m_rightHUDWindow == NULL || m_contextParent[ CP_COMMAND ] == NULL || m_contextParent[ CP_MASTER ] == NULL )
		return FALSE;
	// observers and a defeated player keep the Reforged bar, as under the sidebar
	if( m_isObserverCommandBar )
		return FALSE;
	if( m_controlBarSchemeManager != NULL && m_controlBarSchemeManager->getCurrentSide().compareNoCase( "Observer" ) == 0 )
		return FALSE;
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	return local != NULL && local->isPlayerActive() && !local->isPlayerObserver();
}

//-------------------------------------------------------------------------------------------------
void ControlBar::getKeyGridRects( KeyGridRects *out ) const
{
	const Int w = TheDisplay ? TheDisplay->getWidth() : 0, h = TheDisplay ? TheDisplay->getHeight() : 0;
	const IRegion2D area = HudAreaForScreen( w, h, TheGlobalData ? TheGlobalData->m_hudWidth : (Int)HUD_WIDTH_FULL );
	const Real aspect = m_radarFitHeight > 0.0f ? m_radarFitWidth / m_radarFitHeight : 0.0f;
	// ControlBarUniformScale is 1.8 at 1080 (800x600 design units); the mockups are 1080 pixels
	KeyGridLayoutForScreen( w, h, area, ControlBarUniformScale() / 1.8f, aspect, out );
}

//-------------------------------------------------------------------------------------------------
/** layoutPanels' last step under Key Grid, and fitRadarToMap's whole answer: the Classic pass has put
	* every window where the Reforged bar has it and written that down; this moves the left half onto the
	* console through placeWindowAt only, so the record stays true and a later Classic pass finds every
	* window where it was left.  The right half (portrait, rank, minimise, General) stays the Reforged
	* bar's.  Run again for every map, whose shape the radar box takes. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutKeyGrid( void )
{
	GameWindow *frame = m_contextParent[ CP_MASTER ];
	if( frame == NULL || TheDisplay == NULL )
		return;
	KeyGridRects kg;
	getKeyGridRects( &kg );
	const ICoord2D d = frameTravel( frame, m_panelOrigin );
	ICoord2D origin;
	frame->winGetScreenPosition( &origin.x, &origin.y );
	// a new map (this runs for each) may bring map.ini overrides of the structures' prerequisites
	forgetStructureFacts();
	m_keyGridPlacesKnown = FALSE;

	// the radar's picture fills the glass, the map's own shape, a pixel of bezel round it
	// (W3DLeftHUDDraw paints a pixel inside the window)
	IRegion2D radar = moved( kg.glass, d );
	radar.lo.x -= 1;
	radar.lo.y -= 1;
	radar.hi.x += 1;
	radar.hi.y += 1;
	placeWindowAt( barWindow( "LeftHUD" ), radar );
	// the side's grown frame round the glass (getGrownRadarFrame, drawGrownRadarFrame) is the box: the
	// glass well is the glass alone
	m_radarFitBox[ RADAR_FIT_RADAR ] = radar;
	m_radarFitBox[ RADAR_FIT_RADAR ].lo.x -= origin.x;
	m_radarFitBox[ RADAR_FIT_RADAR ].hi.x -= origin.x;
	m_radarFitBox[ RADAR_FIT_RADAR ].lo.y -= origin.y;
	m_radarFitBox[ RADAR_FIT_RADAR ].hi.y -= origin.y;
	// the box is the console's own nine-slice (drawKeyGridConsole), not the grown radar's frame
	m_radarSquareGrown = FALSE;
	m_radarRaised = FALSE;
	const IRegion2D box = moved( kg.box, d );
	setRaisedRadarRect( box );
	if( m_radarAttackGlowWindow != NULL )
	{
		// EA's lamp on the box's top right corner
		ICoord2D size;
		m_radarAttackGlowWindow->winGetSize( &size.x, &size.y );
		IRegion2D lamp;
		lamp.lo.x = box.hi.x - size.x;
		lamp.lo.y = box.lo.y;
		lamp.hi.x = box.hi.x;
		lamp.hi.y = box.lo.y + size.y;
		placeWindowAt( m_radarAttackGlowWindow, lamp );
		m_radarAttackGlowWindow->winHide( FALSE );
	}

	// the blockers: the box, shelf and spine; the money head; the rows (ControlBar.wnd has the production
	// one disabled, the command grid blocking there, but the deck must block with nothing selected too)
	placeWindowAt( barWindow( "ControlBarProBlockInputRadar" ), moved( joined( joined( kg.box, kg.shelf ), kg.spine ), d ) );
	placeWindowAt( barWindow( "ControlBarProBlockInputMoney" ), moved( kg.head, d ) );
	GameWindow *rowsBlock = barWindow( "ControlBarProBlockInputProduction" );
	if( rowsBlock != NULL )
	{
		if( !m_sidebarBlockSaved )
		{
			m_sidebarBlockEnabled = BitTest( rowsBlock->winGetStatus(), WIN_STATUS_ENABLED ) ? TRUE : FALSE;
			m_sidebarBlockSaved = TRUE;
		}
		rowsBlock->winEnable( TRUE );
		placeWindowAt( rowsBlock, moved( kg.rows, d ) );
	}

	// the shelf: Options, Beacon, Worker, Chat in the side's Key Grid art; the signal keys are
	// updateSignalKeys'
	for( Int i = 0; i < KEY_GRID_SHELF_KEYS; i++ )
	{
		placeWindowAt( barWindow( s_toolWindow[ i ] ), moved( kg.shelfKeys[ i ], d ) );
		wearToolArt( i, m_controlBarSchemeManager );
	}

	// the money on the spine's head after the well's painted "$", the power stood on end in the spine
	// (W3DPowerDrawV)
	IRegion2D money = kg.money;
	money.lo.x = MIN( money.lo.x + REAL_TO_INT( 25.0f * kg.scale + 0.5f ), money.hi.x - 1 );
	placeWindowAt( barWindow( "MoneyDisplay" ), moved( money, d ) );
	GameWindow *power = barWindow( "PowerWindow" );
	if( power != NULL )
	{
		placeWindowAt( power, moved( kg.meter, d ) );
		ControlBar_standPowerOnEnd( power );
	}

	// the commands over the rows, each window on its place's cell (arrangeKeyGrid), and the panes that
	// stand in for them (a building going up, a timer, the beacon's text) over the rows
	IRegion2D rows = kg.cells[ 0 ];
	for( Int place = 1; place < COMMAND_PLACE_COUNT; place++ )
		rows = joined( rows, kg.cells[ place ] );
	placeWindowAt( m_contextParent[ CP_COMMAND ], moved( rows, d ) );
	ControlBar_fitWindowTree( this, m_contextParent[ CP_UNDER_CONSTRUCTION ], moved( rows, d ) );
	ControlBar_fitWindowTree( this, m_contextParent[ CP_OCL_TIMER ], moved( rows, d ) );
	ControlBar_fitWindowTree( this, m_contextParent[ CP_BEACON ], moved( rows, d ) );

	// a factory's queue stays where the Classic pass put it, in the right box over the portrait, as on
	// Reforged.  It stood on a tray over the shelf once, under the production list (InGameUI's
	// drawProductionList, drawn over the windows at the same left edge): a click took the hidden queue
	// button and a right-click cancelled from the list

	// no select army and idle unit keys (the owner's, 2026-11-27); their hotkeys are the command map's.
	// The worker key stands on the shelf
	static const char *const hiddenNames[] = { "ButtonIdleInfantry", "ButtonIdleVehicle", "ButtonIdleAircraft", "ButtonSelectArmy" };
	for( Int i = 0; i < (Int)( sizeof( hiddenNames ) / sizeof( hiddenNames[ 0 ] ) ); i++ )
		hideForSidebar( barWindow( hiddenNames[ i ] ) );

	// what the left panel blocks the world under, for the group panel, the airlift panel, InGameUI
	m_panelRect[ CB_PANEL_LEFT ] = moved( joined( joined( kg.box, kg.shelf ), kg.deck ), d );
	m_panelRect[ CB_PANEL_CENTER ] = moved( kg.rows, d );

	arrangeKeyGrid();
}

//-------------------------------------------------------------------------------------------------
/** Each shown command window on its place's cell, every frame and after every context switch, since a
	* selection changes what stands where without a layout.  Under Key Grid's keys (HOTKEY_SCHEME_GRID) a
	* window wears its place's letter, which COMMAND_SLOTnn presses; under the Classic keys it keeps the
	* letter setControlCommand gave it.  placeWindowAt does nothing for a window already on its cell. */
//-------------------------------------------------------------------------------------------------
void ControlBar::arrangeKeyGrid( void )
{
	if( !m_keyGridLayout || m_contextParent[ CP_MASTER ] == NULL || m_contextParent[ CP_COMMAND ] == NULL )
		return;
	KeyGridRects kg;
	getKeyGridRects( &kg );
	const ICoord2D d = frameTravel( m_contextParent[ CP_MASTER ], m_panelOrigin );
	Int places[ MAX_COMMANDS_PER_SET ];
	getKeyGridPlaces( places );
	const Bool lettered = TheGlobalData->m_hotkeyScheme == HOTKEY_SCHEME_GRID;
	const Bool tray = keyGridTrayView();
	const Bool standIn = m_standInBuilderID != INVALID_DRAWABLE_ID && m_currentSelectedDrawable
		&& m_currentSelectedDrawable->getID() == m_standInBuilderID;
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
	{
		GameWindow *win = m_commandWindows[ slot ];
		if( win == NULL )
			continue;
		const Int place = places[ slot ];
		if( place < 0 || tray )
		{
			// a shown window with no place (an empty seat), or any while the squad's tray has the deck,
			// folds to nothing at the deck's corner, where it neither draws nor takes a click; it opens on
			// its cell the frame it gets a place back
			if( !BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
				placeWindowAt( win, folded( moved( kg.deck, d ) ) );
			continue;
		}
		placeWindowAt( win, moved( kg.cells[ place ], d ) );
		if( !lettered )
			continue;
		// a builder standing in for an empty selection lends its buildings to the keys and nothing else
		const CommandButton *command = (const CommandButton *)GadgetButtonGetData( win );
		const Bool keyed = !standIn || ( command != NULL && command->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT );
		const WideChar letter[ 2 ] = { keyed ? ControlBar_commandPlaceKey( place ) : (WideChar)0, 0 };
		if( win->winGetText().compare( UnicodeString( letter ) ) != 0 )
			GadgetButtonSetText( win, UnicodeString( letter ) );
		if( keyed )
			win->winSetStatus( WIN_STATUS_SHORTCUT_BUTTON );
		else
			win->winClearStatus( WIN_STATUS_SHORTCUT_BUTTON );
	}
}

//-------------------------------------------------------------------------------------------------
WideChar ControlBar::keyGridLetter( const GameWindow *win ) const
{
	if( !m_keyGridLayout || win == NULL || TheGlobalData->m_hotkeyScheme != HOTKEY_SCHEME_GRID )
		return 0;
	Int places[ MAX_COMMANDS_PER_SET ];
	getKeyGridPlaces( places );
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
		if( m_commandWindows[ slot ] == win )
			return ControlBar_commandPlaceKey( places[ slot ] );
	return 0;
}

//-------------------------------------------------------------------------------------------------
// The console's art, Tools/hud_key_grid.py's compose() line for line: the side's <prefix>KeyGrid pieces
// at their own size at 1080 times the console's scale, laid round the glass from the HudWidth area's
// left edge and the screen's foot, the right console from the area's right edge.  A side without the
// pieces gets plain steel plates.  Under the windows, over the world.
//-------------------------------------------------------------------------------------------------
/// compose()'s numbers: the box slice's overhang above the box and past its right, the deck's top left
/// (the spine's left less 44, y 760), the shelf's rise over its top and its right end's width, the run
/// ShelfMid goes on under that end's faded edge, the shoulder's reach either side of the shelf's right,
/// the spine's and shoulder's foot
static const Real KG_BOX_UP = 20.0f, KG_BOX_RIGHT = 12.0f, KG_DECK_X = 44.0f, KG_DECK_Y = 760.0f;
static const Real KG_SHELF_UP = 8.0f, KG_SHELF_RIGHT = 40.0f, KG_FEATHER = 12.0f;
static const Real KG_SHOULDER_IN = 152.0f, KG_SHOULDER_OUT = 8.0f, KG_SPINE_FOOT = 790.0f;

static void steelPlate( const IRegion2D &r )
{
	if( r.hi.x <= r.lo.x || r.hi.y <= r.lo.y )
		return;
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, GameMakeColor( 10, 12, 14, 255 ) );
	if( r.hi.x - r.lo.x > 2 && r.hi.y - r.lo.y > 2 )
		TheDisplay->drawFillRect( r.lo.x + 1, r.lo.y + 1, r.hi.x - r.lo.x - 2, r.hi.y - r.lo.y - 2, GameMakeColor( 62, 68, 74, 255 ) );
}

static void darkWell( const IRegion2D &r )
{
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, GameMakeColor( 8, 22, 30, 255 ) );
}

static DisplayString *s_emptyLetter[ COMMAND_PLACE_COUNT ];

void ControlBar::drawKeyGridConsole( void )
{
	if( !m_keyGridLayout || m_contextParent[ CP_MASTER ] == NULL || m_contextParent[ CP_MASTER ]->winIsHidden() )
		return;
	KeyGridRects kg;
	getKeyGridRects( &kg );
	const ICoord2D d = frameTravel( m_contextParent[ CP_MASTER ], m_panelOrigin );
	const Image *const *art = m_controlBarSchemeManager ? m_controlBarSchemeManager->getKeyGridPieces() : NULL;
	const Image *deck = art ? art[ ControlBarScheme::KEY_GRID_DECK ] : NULL;

	// the glass and what compose() derives from it, at 1080
	Int gw, gh;
	KeyGrid_glassForAspect( m_radarFitHeight > 0.0f ? m_radarFitWidth / m_radarFitHeight : 0.0f, &gw, &gh );
	const Real bw = gw + 23.0f, btop = 1064.0f - gh - 20.0f, stop = btop - 48.0f;
	const Real sw = MAX( bw, 345.0f ), sx0 = bw - 6.0f, sx1 = sx0 + 40.0f;
	const Int screenW = TheDisplay->getWidth(), screenH = TheDisplay->getHeight();
	const IRegion2D area = HudAreaForScreen( screenW, screenH, TheGlobalData->m_hudWidth );
	const Real k = kg.scale;
	const Real left = (Real)( area.lo.x + d.x ), right = (Real)( area.hi.x + d.x ), foot = (Real)( screenH + d.y );
	auto X = [ & ]( Real x ) { return REAL_TO_INT_FLOOR( left + x * k + 0.5f ); };
	auto Y = [ & ]( Real y ) { return REAL_TO_INT_FLOOR( foot - ( KEY_GRID_DESIGN_H - y ) * k + 0.5f ); };
	auto piece = [ & ]( Int which ) -> const Image * { return art ? art[ which ] : NULL; };
	auto W = [ & ]( Int which ) { return piece( which ) ? (Real)piece( which )->getImageWidth() : 0.0f; };
	auto H = [ & ]( Int which ) { return piece( which ) ? (Real)piece( which )->getImageHeight() : 0.0f; };
	auto stretch = [ & ]( Int which, Real x0, Real y0, Real x1, Real y1 )
	{
		if( piece( which ) != NULL && x1 > x0 && y1 > y0 )
			TheDisplay->drawImage( piece( which ), X( x0 ), Y( y0 ), X( x1 ), Y( y1 ) );
	};
	auto put = [ & ]( Int which, Real x, Real y ) { stretch( which, x, y, x + W( which ), y + H( which ) ); };

	if( deck != NULL )
	{
		typedef ControlBarScheme S;
		if( btop < KG_SPINE_FOOT )
			stretch( S::KEY_GRID_SHOULDER, sw - KG_SHOULDER_IN, btop - 8.0f, sw + KG_SHOULDER_OUT, KG_SPINE_FOOT );
		// the box's nine-slice round the glass
		const Real x0 = 0.0f, y0 = btop - KG_BOX_UP, x1 = bw + KG_BOX_RIGHT, y1 = KEY_GRID_DESIGN_H;
		const Real L = W( S::KEY_GRID_BOX_TL ), T = H( S::KEY_GRID_BOX_TL ), R = W( S::KEY_GRID_BOX_TR ), B = H( S::KEY_GRID_BOX_BL );
		stretch( S::KEY_GRID_BOX_T, x0 + L, y0, x1 - R, y0 + T );
		stretch( S::KEY_GRID_BOX_B, x0 + L, y1 - B, x1 - R, y1 );
		stretch( S::KEY_GRID_BOX_L, x0, y0 + T, x0 + L, y1 - B );
		stretch( S::KEY_GRID_BOX_R, x1 - R, y0 + T, x1, y1 - B );
		stretch( S::KEY_GRID_BOX_C, x0 + L, y0 + T, x1 - R, y1 - B );
		put( S::KEY_GRID_BOX_TL, x0, y0 );
		put( S::KEY_GRID_BOX_TR, x1 - R, y0 );
		put( S::KEY_GRID_BOX_BL, x0, y1 - B );
		put( S::KEY_GRID_BOX_BR, x1 - R, y1 - B );
		if( btop < KG_SPINE_FOOT )
		{
			const Real w = W( S::KEY_GRID_SPINE );
			const Real at = sx0 - (Real)( (Int)( w - ( sx1 - sx0 ) ) / 2 );
			stretch( S::KEY_GRID_SPINE, at, btop, at + w, KG_SPINE_FOOT );
		}
		put( S::KEY_GRID_DECK, sx0 - KG_DECK_X, KG_DECK_Y );
		const Real sy = stop - KG_SHELF_UP;
		put( S::KEY_GRID_SHELF_LEFT, 0.0f, sy );
		const Real lw = W( S::KEY_GRID_SHELF_LEFT );
		stretch( S::KEY_GRID_SHELF_MID, lw, sy, MAX( lw, sw - KG_SHELF_RIGHT ) + KG_FEATHER, sy + H( S::KEY_GRID_SHELF_MID ) );
		put( S::KEY_GRID_SHELF_RIGHT, sw - KG_SHELF_RIGHT, sy );
		// the right console from the area's right edge (drawPainting leaves the Reforged right piece off)
		const Image *rightArt = piece( S::KEY_GRID_RIGHT );
		if( rightArt != NULL )
		{
			const Int rw = REAL_TO_INT( W( S::KEY_GRID_RIGHT ) * k + 0.5f ), rh = REAL_TO_INT( H( S::KEY_GRID_RIGHT ) * k + 0.5f );
			const Int rx = REAL_TO_INT_FLOOR( right + 0.5f );
			TheDisplay->drawImage( rightArt, rx - rw, Y( KEY_GRID_DESIGN_H ) - rh, rx, Y( KEY_GRID_DESIGN_H ) );
		}
	}
	else
	{
		// no pieces: plain steel where they would stand
		steelPlate( moved( kg.box, d ) );
		steelPlate( moved( kg.spine, d ) );
		IRegion2D lower = moved( kg.spine, d );
		lower.lo.y = moved( kg.deck, d ).lo.y;
		steelPlate( lower );
		steelPlate( moved( kg.head, d ) );
		for( Int row = 0; row < 3; row++ )
		{
			IRegion2D back = joined( kg.cells[ row * COMMAND_PLACE_COLUMNS ], kg.cells[ row * COMMAND_PLACE_COLUMNS + COMMAND_PLACE_COLUMNS - 1 ] );
			const Int lip = MAX( 1, REAL_TO_INT( 8.0f * kg.scale + 0.5f ) );
			back.lo.x -= lip;
			back.lo.y -= lip;
			back.hi.x += lip;
			back.hi.y = row == 2 ? screenH : back.hi.y + lip;
			steelPlate( moved( back, d ) );
		}
		for( Int place = 0; place < COMMAND_PLACE_COUNT; place++ )
			darkWell( moved( kg.cells[ place ], d ) );
		darkWell( moved( kg.money, d ) );
		darkWell( moved( kg.meter, d ) );
		steelPlate( moved( kg.shelf, d ) );
	}

	// an empty place wears the side's empty key, and under Key Grid's keys its letter dim, so the whole
	// keyboard reads (under the Classic keys the place is not what a letter presses).  A, C and V on a
	// unit that attack moves are live keys (force attack, hold position, move) and a shade brighter
	if( m_contextParent[ CP_COMMAND ]->winIsHidden() || keyGridTrayView() )
		return;
	Int places[ MAX_COMMANDS_PER_SET ];
	const Bool fights = getKeyGridPlaces( places );
	Bool used[ COMMAND_PLACE_COUNT ] = { FALSE };
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
		if( places[ slot ] >= 0 )
			used[ places[ slot ] ] = TRUE;
	for( Int place = 0; place < COMMAND_PLACE_COUNT; place++ )
		if( !used[ place ] )
			drawKeyGridEmptyKey( place, moved( kg.cells[ place ], d ),
				fights && ( place == COMMAND_PLACE_ATTACK || place == COMMAND_PLACE_HOLD || place == COMMAND_PLACE_MOVE ) );
}

//-------------------------------------------------------------------------------------------------
/** A place with no button: the side's KeyEmpty (a dark well without it), and under Key Grid's keys its
	* letter dim in the middle, a shade brighter when `live`.  The deck's commands and GroupPanel's squad
	* tray both draw their empty places so. */
//-------------------------------------------------------------------------------------------------
void ControlBar::drawKeyGridEmptyKey( Int place, const IRegion2D &cell, Bool live ) const
{
	if( place < 0 || place >= COMMAND_PLACE_COUNT || TheDisplay == NULL )
		return;
	const Image *const *art = m_controlBarSchemeManager ? m_controlBarSchemeManager->getKeyGridPieces() : NULL;
	const Image *empty = art ? art[ ControlBarScheme::KEY_GRID_KEY_EMPTY ] : NULL;
	if( empty != NULL )
		TheDisplay->drawImage( empty, cell.lo.x, cell.lo.y, cell.hi.x, cell.hi.y );
	else
		darkWell( cell );
	if( TheGlobalData->m_hotkeyScheme != HOTKEY_SCHEME_GRID || TheFontLibrary == NULL )
		return;
	// 15 points on a 72 high key at 1080
	GameFont *font = TheFontLibrary->getFont( "Arial", MAX( 8, REAL_TO_INT( 15.0f * ( cell.hi.y - cell.lo.y ) / 72.0f + 0.5f ) ), TRUE );
	if( font == NULL )
		return;
	DisplayString *&text = s_emptyLetter[ place ];
	if( text == NULL )
	{
		text = TheDisplayStringManager->newDisplayString();
		if( text == NULL )
			return;
		const WideChar letter[ 2 ] = { ControlBar_commandPlaceKey( place ), 0 };
		text->setText( UnicodeString( letter ) );
	}
	if( text->getFont() != font )
		text->setFont( font );
	Int w, h;
	text->getSize( &w, &h );
	text->draw( ( cell.lo.x + cell.hi.x - w ) / 2, ( cell.lo.y + cell.hi.y - h ) / 2,
		live ? GameMakeColor( 150, 182, 204, 255 ) : GameMakeColor( 70, 104, 128, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
// The squad tray on the deck (the owner's, 2026-11-27): with a squad that has a build tray chosen, a
// switch on the spine flips the deck between the selection's commands and the squad's tray on the same
// eighteen keys, Q W E R T Y then the A row then the Z row; Ctrl+Tab does the same in every scheme
// (MSG_META_TOGGLE_KEY_GRID_TRAY, MetaMap's fork defaults; Caps Lock was turned down: it flips the
// caps state chat types in, and SDL sends it as a toggle).  GroupPanel lays the tray's buttons on the cells
// (getKeyGridCells); a press of a place's key in tray view builds that entry into the squad.
//-------------------------------------------------------------------------------------------------
Bool ControlBar::getKeyGridCells( IRegion2D *cells ) const
{
	if( !m_keyGridLayout || m_contextParent[ CP_MASTER ] == NULL )
		return FALSE;
	KeyGridRects kg;
	getKeyGridRects( &kg );
	const ICoord2D d = frameTravel( m_contextParent[ CP_MASTER ], m_panelOrigin );
	for( Int place = 0; place < COMMAND_PLACE_COUNT; place++ )
		cells[ place ] = moved( kg.cells[ place ], d );
	return TRUE;
}

Bool ControlBar::keyGridTrayView( void ) const
{
	return m_keyGridLayout && m_keyGridTrayView && GroupPanel_trayAvailable();
}

void ControlBar::toggleKeyGridTray( void )
{
	if( !m_keyGridLayout || !GroupPanel_trayAvailable() )
		return;
	m_keyGridTrayView = !m_keyGridTrayView;
	AudioEventRTS click( "GUIClick" );
	if( TheAudio )
		TheAudio->addAudioEvent( &click );
}

static Int keyGridSwitchId( void )
{
	static const Int id = (Int)TheNameKeyGenerator->nameToKey( "ControlBar:KeyGridSwitch" );
	return id;
}

static WindowMsgHandledType keyGridSwitchInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_LEFT_UP:
			if( TheControlBar )
				TheControlBar->toggleKeyGridTray();
			return MSG_HANDLED;
		case GWM_MOUSE_ENTERING:
			BitSet( window->winGetInstanceData()->m_state, WIN_STATE_HILITED );
			return MSG_HANDLED;
		case GWM_MOUSE_LEAVING:
			BitClear( window->winGetInstanceData()->m_state, WIN_STATE_HILITED );
			return MSG_HANDLED;
		case GWM_LEFT_DOWN:
		case GWM_RIGHT_DOWN:
		case GWM_RIGHT_UP:
		case GWM_WHEEL_UP:
		case GWM_WHEEL_DOWN:
			return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

/// the switch: the side's <prefix>KeyGridSwitchGroup (it leads to the squad's tray) or SwitchBack (back to
/// the commands), its H state under the pointer; else a steel key with three heads or a 3x3 grid on it
static void drawKeyGridSwitch( GameWindow *window, WinInstanceData *instData )
{
	if( TheControlBar == NULL )
		return;
	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	const Bool tray = TheControlBar->keyGridTrayView();
	const ControlBarSchemeManager *schemes = TheControlBar->getControlBarSchemeManager();
	const Image *const *art = schemes ? schemes->getKeyGridPieces() : NULL;
	const Int first = tray ? ControlBarScheme::KEY_GRID_SWITCH_BACK : ControlBarScheme::KEY_GRID_SWITCH_GROUP;
	const Bool hover = instData != NULL && BitTest( instData->getState(), WIN_STATE_HILITED );
	const Image *key = art ? art[ first + ( hover ? 1 : 0 ) ] : NULL;
	if( key == NULL && art != NULL )
		key = art[ first ];
	if( key != NULL )
	{
		TheDisplay->drawImage( key, at.x, at.y, at.x + size.x, at.y + size.y );
		return;
	}
	const IRegion2D r = { { at.x, at.y }, { at.x + size.x, at.y + size.y } };
	steelPlate( r );
	const Color ink = tray ? GameMakeColor( 255, 210, 60, 255 ) : GameMakeColor( 205, 205, 205, 255 );
	const Int u = MAX( 2, MIN( size.x, size.y ) / 7 );
	const Int cx = at.x + size.x / 2, cy = at.y + size.y / 2;
	if( tray )
	{
		for( Int row = -1; row <= 1; row++ )
			for( Int column = -1; column <= 1; column++ )
				TheDisplay->drawFillRect( cx + column * 2 * u - u / 2, cy + row * 2 * u - u / 2, u, u, ink );
	}
	else
	{
		for( Int head = -1; head <= 1; head++ )
		{
			TheDisplay->drawFillRect( cx + head * 2 * u - u / 2, cy - u, u, u, ink );
			TheDisplay->drawFillRect( cx + head * 2 * u - u, cy + u / 2, 2 * u, u, ink );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ControlBar::updateKeyGridSwitch( void )
{
	GameWindow *window = m_keyGridSwitchParent;
	if( window != NULL && TheWindowManager->winGetWindowFromId( NULL, keyGridSwitchId() ) != window )
		window = m_keyGridSwitchParent = NULL;

	const Bool available = m_keyGridLayout && GroupPanel_trayAvailable();
	// the tray view ends with the squad's tray, so the next squad opens on its commands
	if( !available )
		m_keyGridTrayView = FALSE;
	const Bool shown = available && m_contextParent[ CP_MASTER ] != NULL && !m_contextParent[ CP_MASTER ]->winIsHidden()
		&& !TheInGameUI->isQuitMenuVisible();
	if( !shown )
	{
		if( window != NULL )
			window->winHide( TRUE );
		return;
	}
	if( window == NULL )
	{
		// ABOVE, as the signal keys' plate: a top-level window drawn after the bar, over the spine
		window = TheWindowManager->winCreate( NULL, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN | WIN_STATUS_ABOVE, 0, 0, 1, 1, GameWinDefaultSystem );
		if( window == NULL )
			return;
		window->winSetWindowId( keyGridSwitchId() );
		window->winSetDrawFunc( drawKeyGridSwitch );
		window->winSetInputFunc( keyGridSwitchInput );
		m_keyGridSwitchParent = window;
	}
	KeyGridRects kg;
	getKeyGridRects( &kg );
	const IRegion2D at = moved( kg.deckSwitch, frameTravel( m_contextParent[ CP_MASTER ], m_panelOrigin ) );
	window->winSetPosition( at.lo.x, at.lo.y );
	window->winSetSize( at.hi.x - at.lo.x, at.hi.y - at.lo.y );
	window->winHide( FALSE );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::keyGridTrayCovers( const GameWindow *win ) const
{
	if( win == NULL || m_contextParent[ CP_COMMAND ] == NULL || !keyGridTrayView() )
		return FALSE;
	return const_cast< GameWindow * >( win )->winGetParent() == m_contextParent[ CP_COMMAND ];
}

//-------------------------------------------------------------------------------------------------
void ControlBar::shutdownKeyGrid( void )
{
	// the switch is a top-level window of its own, which the bar's layout does not take down with it
	if( m_keyGridSwitchParent != NULL && TheWindowManager != NULL
			&& TheWindowManager->winGetWindowFromId( NULL, keyGridSwitchId() ) == m_keyGridSwitchParent )
		TheWindowManager->winDestroy( m_keyGridSwitchParent );
	m_keyGridSwitchParent = NULL;
	m_keyGridTrayView = FALSE;
	m_keyGridPlacesKnown = FALSE;
	m_keyGridPlacedOwner = NULL;
}
