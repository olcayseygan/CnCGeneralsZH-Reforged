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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: PopupSaveLoad.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: PopupSaveLoad.cpp
//
// Created:   Chris Brue, June 2002
//
// Desc:      the Save/Load window control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "zhio.h"

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MessageStream.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/SinglePlayerParts.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBackKey					= NAMEKEY_INVALID;
static NameKeyType buttonSaveKey					= NAMEKEY_INVALID;
static NameKeyType buttonLoadKey					= NAMEKEY_INVALID;
static NameKeyType buttonDeleteKey				= NAMEKEY_INVALID;
static NameKeyType listboxGamesKey				= NAMEKEY_INVALID;
static NameKeyType buttonOverwriteCancel	= NAMEKEY_INVALID;
static NameKeyType buttonOverwriteConfirm = NAMEKEY_INVALID;
static NameKeyType buttonLoadCancel				= NAMEKEY_INVALID;
static NameKeyType buttonLoadConfirm			= NAMEKEY_INVALID;
static NameKeyType buttonSaveDescCancel		= NAMEKEY_INVALID;
static NameKeyType buttonSaveDescConfirm	= NAMEKEY_INVALID;
static NameKeyType buttonDeleteConfirm		= NAMEKEY_INVALID;
static NameKeyType buttonDeleteCancel			= NAMEKEY_INVALID;

static GameWindow *buttonFrame = NULL;
static GameWindow *overwriteConfirm = NULL;
static GameWindow *loadConfirm = NULL;
static GameWindow *saveDesc = NULL;
static GameWindow *listboxGames = NULL;
static GameWindow *editDesc = NULL;
static GameWindow *deleteConfirm = NULL;

static GameWindow *parent = NULL;
static SaveLoadLayoutType currentLayoutType = SLLT_INVALID;
static Bool isPopup = FALSE;
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;
static Bool isShuttingDown = false;
// the save a load, overwrite or delete confirm asks about, copied as the box opens: the listbox's item
// data points into TheGameState's list, which every engine reset and repopulate frees under an open box
static AvailableGameInfo confirmPick;
static Bool confirmOpen = FALSE;

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
extern Bool DontShowMainMenu; //KRIS
extern Bool ReplayWasPressed;

//-------------------------------------------------------------------------------------------------
// The menu language (docs/plans/MENU-REDESIGN.md, package 12).  Load Game is a screen in frame A:
// the saves in five columns, two legend lines and the picked save's details.  Save / Load over a
// match is a 1040 box of the same list squeezed, the new save first.  The confirms ask through the
// message box; the save's description keeps a question box of its own.  SinglePlayerLayout.h holds
// every number; the masters (singleplayer_layout.py, boxes_layout.py) stand on them.
//-------------------------------------------------------------------------------------------------
namespace SPL = SinglePlayerLayout;
namespace SPP = SinglePlayerParts;
using SinglePlayerParts::speedText;
using SinglePlayerParts::lengthText;

static MenuBox::TextPool saveText;

static AvailableGameInfo *saveRow( Int row )
{
	return listboxGames && row >= 0 && row < SPP::rowCount( listboxGames ) ? (AvailableGameInfo *)GadgetListBoxGetItemData( listboxGames, row ) : NULL;
}

/// the name the list gives a save: its description, else its map's label
static UnicodeString saveName( const SaveGameInfo &save )
{
	if( !save.description.isEmpty() )
		return save.description;
	Bool exists = FALSE;
	UnicodeString label = TheGameText->fetch( save.mapLabel, &exists );
	if( exists == FALSE )
		label.translate( save.mapLabel );
	return label;
}

static UnicodeString savedWhen( const SaveGameInfo &save )
{
	WallClockTime when;
	when.wYear = save.date.year;
	when.wMonth = save.date.month;
	when.wDayOfWeek = save.date.dayOfWeek;
	when.wDay = save.date.day;
	when.wHour = save.date.hour;
	when.wMinute = save.date.minute;
	when.wSecond = save.date.second;
	when.wMilliseconds = save.date.milliseconds;
	UnicodeString text = getUnicodeDateBuffer( when );
	text.concat( u" " );
	text.concat( getUnicodeTimeBuffer( when ) );
	return text;
}

static Campaign *saveCampaign( const SaveGameInfo &save )
{
	return save.campaignSide.isEmpty() || TheCampaignManager == NULL ? NULL : TheCampaignManager->findCampaign( save.campaignSide );
}

/// the campaign a save is in: Zero Hour USA, Generals China, Challenge: Air Force, Skirmish
static UnicodeString campaignWords( const SaveGameInfo &save )
{
	Campaign *campaign = saveCampaign( save );
	if( campaign == NULL )
		return TheGameText->fetch( "GUI:SPSkirmish" );
	UnicodeString text;
	if( campaign->isChallengeCampaign() )
	{
		const GeneralPersona *general = TheChallengeGenerals ? TheChallengeGenerals->getPlayerGeneralByCampaignName( campaign->m_name ) : NULL;
		const Int index = general ? (Int)( general - TheChallengeGenerals->getChallengeGenerals() ) : -1;
		text.format( TheGameText->fetch( "GUI:SPCampaignChallenge" ), TheGameText->fetch( SPL::armyKey( index ) ).str() );
		return text;
	}
	const AsciiString side = campaign->getSide();
	const char *sideKey = side.compareNoCase( "usa" ) == 0 ? "GUI:SPSideUSA" : side.compareNoCase( "china" ) == 0 ? "GUI:SPSideChina"
		: side.compareNoCase( "gla" ) == 0 ? "GUI:SPSideGLA" : NULL;
	if( sideKey == NULL )
		return TheGameText->fetch( campaign->m_campaignNameLabel );
	text.format( TheGameText->fetch( campaign->m_name.startsWithNoCase( "gen_" ) ? "GUI:SPCampaignGenerals" : "GUI:SPCampaignZH" ),
		TheGameText->fetch( sideKey ).str() );
	return text;
}

static const char *kindKey( const SaveGameInfo &save )
{
	return save.saveFileType == SAVE_FILE_TYPE_MISSION ? "GUI:SPKindMissionStart" : "GUI:SPKindSaved";
}

/// the list's columns, where they stand at 1080: the screen's, or squeezed into the box
static void saveColumns( SPL::Column *columns )
{
	for( Int c = 0; c < SPL::SAVE_COLUMNS; c++ )
		columns[ c ] = isPopup ? SPL::saveColumn( c, SPL::SAVE_BOX_LIST.lo.x, SPL::SAVE_BOX_LIST.hi.x ) : SPL::saveColumn( c );
}

/// the rows over EA's gadget: the new save first in a box that saves, a save this build cannot read MUTED
static void saveListDraw( GameWindow *list, WinInstanceData * )
{
	using namespace UIStyle;
	SPL::Column columns[ SPL::SAVE_COLUMNS ];
	saveColumns( columns );
	const IRegion2D box = MenuBox::windowRect( list );
	const Int right = SPP::point( (Real)( isPopup ? SPL::SAVE_BOX_LIST.hi.x - 8 : SPL::SAVE_ROW_RIGHT ), 0.0f ).x;
	const Int selected = SPP::selectedRow( list ), hovered = SPP::hoveredRow( list );
	const Int rows = SPP::rowCount( list );
	for( Int row = 0; row < rows; row++ )
	{
		const IRegion2D r = SPP::rowRect( list, row, right );
		if( r.lo.y < box.lo.y || r.hi.y > box.hi.y + 1 )
			continue;
		menuRow( r, row == hovered && row != selected ? ROW_HOVER : ROW_PLAIN, ( row & 1 ) != 0 );
		if( row == selected )
			SPP::picked( r );
		const Int middle = ( r.lo.y + r.hi.y ) / 2;
		const AvailableGameInfo *info = saveRow( row );
		const IRegion2D first = SPP::on( SPL::rect( columns[ 0 ].left, 0, columns[ 0 ].right, 1 ) );
		if( info == NULL )
		{
			const Int plus = SPP::words( saveText, UnicodeString( u"+" ), MENU_TEXT_ROW_FIGURE, FALSE, first.lo.x, middle, EDGE_LIT, 0 );
			SPP::wordsKey( saveText, "GUI:SPNewSave", MENU_TEXT_BODY, FALSE, first.lo.x + plus + SPP::px( 10.0f ), middle, INK,
				right - first.lo.x - plus );
			continue;
		}
		const SaveGameInfo &save = info->saveGameInfo;
		const Bool loads = save.isLoadable();
		const Color ink = loads ? INK : MUTED;
		Campaign *campaign = saveCampaign( save );
		for( Int c = 0; c < SPL::SAVE_COLUMNS; c++ )
		{
			const IRegion2D cell = SPP::on( SPL::rect( columns[ c ].left, 0, columns[ c ].right, 1 ) );
			const Int w = cell.hi.x - cell.lo.x;
			const Int x = columns[ c ].alignRight ? cell.hi.x : cell.lo.x;
			const MenuBox::Align align = columns[ c ].alignRight ? MenuBox::RIGHT : MenuBox::LEFT;
			UnicodeString figure;
			switch( c )
			{
				case SPL::SAVE_NAME:
					SPP::words( saveText, saveName( save ), MENU_TEXT_BODY, FALSE, x, middle, ink, w, align, TRUE );
					break;
				case SPL::SAVE_CAMPAIGN:
					SPP::words( saveText, campaignWords( save ), MENU_TEXT_BODY, FALSE, x, middle, ink, w, align, TRUE );
					break;
				case SPL::SAVE_MISSION:
					if( campaign )
					{
						figure.format( u"%d", save.missionNumber + 1 );
						SPP::words( saveText, figure, MENU_TEXT_BODY, FALSE, x, middle, ink, w, align );
					}
					break;
				case SPL::SAVE_KIND:
					if( loads )
						SPP::wordsKey( saveText, kindKey( save ), MENU_TEXT_BODY, FALSE, x, middle, ink, w, align );
					else
						SPP::capitalsKey( "GUI:SPOlder", x, middle, MUTED, align, w );
					break;
				case SPL::SAVE_SAVED:
					SPP::words( saveText, savedWhen( save ), MENU_TEXT_BODY, FALSE, x, middle, ink, w, align );
					break;
			}
		}
	}
	SPP::scrollBar( list, SPP::point( (Real)( isPopup ? SPL::SAVE_BOX_LIST.hi.x - 4 : SPL::SAVE_SCROLL_X ), 0.0f ).x );
}

/// the column heads over the list, SAVED sorted (newest first, as GameState lists them)
static void saveHeads( Int top )
{
	static const char *heads[ SPL::SAVE_COLUMNS ] = { "GUI:SPName", "GUI:SPCampaign", "GUI:SPMission", "GUI:SPKind", "GUI:SPSaved" };
	SPL::Column columns[ SPL::SAVE_COLUMNS ];
	UnicodeString labels[ SPL::SAVE_COLUMNS ];
	saveColumns( columns );
	for( Int c = 0; c < SPL::SAVE_COLUMNS; c++ )
		labels[ c ] = TheGameText->fetch( heads[ c ] );
	SPP::heads( columns, labels, SPL::SAVE_COLUMNS, SPL::SAVE_SAVED, top, isPopup ? SPL::SAVE_BOX_LIST.lo.x : SPL::LEFT,
		isPopup ? SPL::SAVE_BOX_LIST.hi.x : SPL::SAVE_LIST_RIGHT );
}

/// a fact at 1080: its label in capitals, its value under it
static void saveFact( Int x0, Int y0, Int x1, const char *label, const UnicodeString &value, Color ink = UIStyle::INK )
{
	SPP::fact( saveText, SPP::on( SPL::rect( x0, y0, x1, y0 + 46 ) ), TheGameText->fetch( label ), value, ink );
}

/// the picked save's details: name, campaign, the campaign's missions as tiles, the map, the facts
static void drawSaveDetails( const AvailableGameInfo *info )
{
	using namespace UIStyle;
	if( info == NULL )
		return;
	const SaveGameInfo &save = info->saveGameInfo;
	const Bool loads = save.isLoadable();
	const IRegion2D name = SPP::on( SPL::SAVE_DETAILS_NAME );
	SPP::words( saveText, saveName( save ), MENU_TEXT_ROW_FIGURE, FALSE, name.lo.x, ( name.lo.y + name.hi.y ) / 2, INK,
		name.hi.x - name.lo.x, MenuBox::LEFT, TRUE );
	const IRegion2D line = SPP::on( SPL::SAVE_DETAILS_CAMPAIGN );
	SPP::words( saveText, campaignWords( save ), MENU_TEXT_BODY, FALSE, line.lo.x, ( line.lo.y + line.hi.y ) / 2, MUTED,
		line.hi.x - line.lo.x, MenuBox::LEFT, TRUE );

	Campaign *campaign = saveCampaign( save );
	const Int count = campaign ? (Int)campaign->m_missions.size() : 0;
	if( count > 0 )
	{
		const IRegion2D head = SPP::on( SPL::SAVE_DETAILS_MISSION );
		UnicodeString of;
		of.format( TheGameText->fetch( "GUI:SPMissionOf" ), save.missionNumber + 1, count );
		SPP::capitals( of, head.lo.x, ( head.lo.y + head.hi.y ) / 2, MUTED, MenuBox::LEFT, head.hi.x - head.lo.x );
		for( Int each = 0; each < count; each++ )
		{
			const IRegion2D tile = SPP::on( SPL::missionTile( count, each ) );
			const char *word = each < save.missionNumber ? "GUI:SPDone" : each == save.missionNumber ? "GUI:SPThisSave" : "GUI:SPAhead";
			if( each < save.missionNumber )
			{
				MenuBox::fill( tile, withAlpha( FRAME, 60 ) );
				MenuBox::outline( tile, FRAME );
			}
			else if( each == save.missionNumber )
				SPP::picked( tile );
			else
			{
				MenuBox::fill( tile, GameMakeColor( 0, 0, 0, 150 ) );
				MenuBox::outline( tile, withAlpha( FRAME, 120 ) );
			}
			UnicodeString number;
			number.format( u"%d", each + 1 );
			const Int middle = ( tile.lo.x + tile.hi.x ) / 2;
			SPP::words( saveText, number, MENU_TEXT_ROW_FIGURE, FALSE, middle, ( tile.lo.y + tile.hi.y ) / 2,
				each > save.missionNumber ? MUTED : INK, 0, MenuBox::CENTRE );
			SPP::capitalsKey( word, middle, tile.hi.y + SPP::px( 14.0f ), each == save.missionNumber ? INK : MUTED, MenuBox::CENTRE,
				tile.hi.x - tile.lo.x );
		}
	}

	// the mission's map at its shape, NO PREVIEW where it has no picture (Generals' own missions).  A
	// listed save's SaveGameInfo carries no map path (only the header block is read for the list), so
	// the map comes with the extras, read off the save's map block
	SaveGameExtras extras;
	const Bool haveExtras = GameState_saveExtras( info->filename, extras );
	const AsciiString mapPath = !save.pristineMapName.isEmpty() ? save.pristineMapName : haveExtras ? extras.pristineMap : AsciiString();
	static AsciiString s_previewOf;
	static Image *s_preview = NULL;
	const MapMetaData *md = TheMapCache && !mapPath.isEmpty() ? TheMapCache->findMap( mapPath ) : NULL;
	if( s_previewOf.compareNoCase( mapPath ) != 0 )
	{
		s_previewOf = mapPath;
		s_preview = md ? getMapPreviewImage( mapPath ) : NULL;
	}
	const Real mw = md ? md->m_extent.hi.x - md->m_extent.lo.x : 0.0f;
	const Real mh = md ? md->m_extent.hi.y - md->m_extent.lo.y : 0.0f;
	const IRegion2D preview = SPP::on( SPL::fitShape( SPL::SAVE_PREVIEW, mw, mh ) );
	if( s_preview )
		SPP::framed( s_preview, preview, FALSE );
	else
	{
		MenuBox::fill( preview, GameMakeColor( 0, 0, 0, 150 ) );
		MenuBox::outline( preview, withAlpha( FRAME, 220 ) );
		SPP::capitalsKey( "GUI:SPNoPreview", ( preview.lo.x + preview.hi.x ) / 2, ( preview.lo.y + preview.hi.y ) / 2, MUTED, MenuBox::CENTRE );
	}

	// the facts beside it
	const Int fx = SPL::SAVE_FACTS_LEFT;
	const Int top = SPL::SAVE_PREVIEW.lo.y;
	UnicodeString file;
	file.translate( info->filename );
	const Int speed = save.gameSpeedPercent();
	saveFact( fx, top, SPL::RIGHT, "GUI:SPSaved", savedWhen( save ) );
	saveFact( fx, top + 54, SPL::RIGHT, "GUI:SPKind", TheGameText->fetch( save.saveFileType == SAVE_FILE_TYPE_MISSION ? "GUI:SPKindMissionStart" : "GUI:SPSavedByYou" ) );
	saveFact( fx, top + 108, SPL::RIGHT, "GUI:SPSpeed", speed > 0 ? speedText( speed ) : UnicodeString( u"-" ) );
	saveFact( fx, top + 162, SPL::RIGHT, "GUI:SPLoads", TheGameText->fetch( loads ? "GUI:SPLoadsYes" : "GUI:SPLoadsNo" ), loads ? INK : MUTED );
	saveFact( fx, top + 216, SPL::RIGHT, "GUI:SPFile", file );

	// the mission's place (a skirmish's map), then how hard and how long, or why it will not load
	const Int x0 = SPP::point( (Real)SPL::SAVE_DETAILS_LEFT, 0.0f ).x, x1 = SPP::point( (Real)SPL::RIGHT, 0.0f ).x;
	SPP::section( saveText, x0, x1, SPP::point( 0.0f, (Real)SPL::SAVE_MISSION_TOP ).y,
		TheGameText->fetch( campaign != NULL ? "GUI:SPMission" : "GUI:SPMap" ) );
	Bool labelled = FALSE;
	UnicodeString place = TheGameText->fetch( save.mapLabel, &labelled );
	if( !labelled )
		place.translate( save.mapLabel );
	SPP::wrapped( saveText, place, MENU_TEXT_BODY, x0, SPP::point( 0.0f, (Real)( SPL::SAVE_MISSION_TOP + 36 ) ).y, x1 - x0, 2, INK,
		SPP::px( 26.0f ) );
	if( !loads )
	{
		SPP::capitalsKey( "GUI:SPLoads", x0, SPP::point( 0.0f, (Real)( SPL::SAVE_DIFFICULTY_TOP + 8 ) ).y, MUTED );
		SPP::wrapped( saveText, TheGameText->fetch( "GUI:SPWontLoad" ), MENU_TEXT_BODY, x0,
			SPP::point( 0.0f, (Real)( SPL::SAVE_DIFFICULTY_TOP + 20 ) ).y, x1 - x0, 2, MUTED, SPP::px( 26.0f ) );
		return;
	}
	if( !haveExtras )
		return;
	// the campaign's difficulty; a skirmish has none of its own (each computer seat has its level, which the
	// list does not read), so its PLAYED FOR takes the first place
	const Int half = ( SPL::SAVE_DETAILS_LEFT + SPL::RIGHT ) / 2;
	Int playedAt = SPL::SAVE_DETAILS_LEFT;
	if( campaign != NULL )
	{
		const char *difficulty = extras.difficulty == DIFFICULTY_EASY ? "GUI:Easy" : extras.difficulty == DIFFICULTY_HARD ? "GUI:Hard" : "GUI:Medium";
		saveFact( SPL::SAVE_DETAILS_LEFT, SPL::SAVE_DIFFICULTY_TOP, half - 12, "GUI:SPDifficulty", TheGameText->fetch( difficulty ) );
		playedAt = half;
	}
	if( extras.frames > 0 )
	{
		const Int perSecond = save.framesPerSecond > 0 ? save.framesPerSecond : LOGICFRAMES_PER_SECOND;
		saveFact( playedAt, SPL::SAVE_DIFFICULTY_TOP, SPL::RIGHT, "GUI:SPPlayedFor", lengthText( (Int)( extras.frames / perSecond ) ) );
	}
}

static Int saveCount( void )
{
	Int saves = 0;
	const Int rows = listboxGames ? SPP::rowCount( listboxGames ) : 0;
	for( Int row = 0; row < rows; row++ )
		if( saveRow( row ) )
			saves++;
	return saves;
}

/// Load Game, the full screen
static void saveScreenDraw( GameWindow *, WinInstanceData * )
{
	using namespace UIStyle;
	saveText.begin();
	const IRegion2D panel = boardRect( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM );
	glass( panel, 168 );
	frameA( panel );
	UnicodeString count;
	count.format( TheGameText->fetch( "GUI:SPSaveCount" ), saveCount() );
	menuHeader( saveText.get( capitals( TheGameText->fetch( "GUI:LoadGame" ) ) ), saveText.get( count ) );
	saveHeads( SPL::SAVE_HEADS.lo.y );
	const Int x = SPP::point( 76.0f, 0.0f ).x, widest = SPP::point( (Real)SPL::SAVE_ROW_RIGHT, 0.0f ).x - x;
	SPP::capitalsKey( "GUI:SPLegendMissionStart", x, SPP::point( 0.0f, (Real)( SPL::SAVE_LEGEND_TOP + 9 ) ).y, MUTED, MenuBox::LEFT, widest );
	SPP::capitalsKey( "GUI:SPLegendOlder", x, SPP::point( 0.0f, (Real)( SPL::SAVE_LEGEND_TOP + 35 ) ).y, MUTED, MenuBox::LEFT, widest );
	drawSaveDetails( saveRow( SPP::selectedRow( listboxGames ) ) );
	const IRegion2D foot = SPP::on( SPL::rect( SPL::LEFT, SPL::FOOT_RULE, SPL::RIGHT, SPL::FOOT_RULE + 1 ) );
	MenuBox::rule( foot.lo.x, foot.hi.x, foot.lo.y, 110 );
}

/// Save / Load, the box over the match
static void saveBoxDraw( GameWindow *window, WinInstanceData * )
{
	using namespace UIStyle;
	saveText.begin();
	const IRegion2D box = MenuBox::windowRect( window );
	const Bool overMatch = TheShell == NULL || !TheShell->isShellActive();
	// the in-game menu it opens from stays up under it: the glass alone let its words read through
	MenuBox::fill( box, GameMakeColor( 0, 0, 0, 190 ) );
	boxShell( box, saveText.get( capitals( TheGameText->fetch( currentLayoutType == SLLT_LOAD_ONLY ? "GUI:LoadGame" : "GUI:SPSaveLoad" ) ) ),
		overMatch );
	saveHeads( SPL::SAVE_BOX_HEADS );
	boxFootRule( box );
	SPP::wordsKey( saveText, "GUI:HintEscCancel", MENU_TEXT_HINT, FALSE, box.lo.x + SPP::px( (Real)BOX_PAD ), box.hi.y - SPP::px( 44.0f ),
		MUTED, SPP::px( 300.0f ) );
}

/// the save's description, a question box with its text box
static void saveDescDraw( GameWindow *window, WinInstanceData * )
{
	using namespace UIStyle;
	const IRegion2D box = MenuBox::windowRect( window );
	MenuBox::fill( box, GameMakeColor( 0, 0, 0, 190 ) );
	boxShell( box, saveText.get( capitals( TheGameText->fetch( "GUI:SaveGame" ) ) ), FALSE );
	SPP::words( saveText, TheGameText->fetch( "GUI:EnterSaveDesc" ), MENU_TEXT_BODY, FALSE, box.lo.x + SPP::px( (Real)BOX_PAD ),
		box.lo.y + SPP::px( (Real)( BOX_HEAD + 24 + BOX_LINE / 2 ) ), INK, box.hi.x - box.lo.x - 2 * SPP::px( (Real)BOX_PAD ) );
	boxFootRule( box );
}

static void saveNothingDraw( GameWindow *, WinInstanceData * )
{
}

/// the windows the language draws, the keys outline only
static void dressSaveLoad( WindowLayout *layout )
{
	if( parent == NULL || listboxGames == NULL )
		return;
	parent->winSetDrawFunc( isPopup ? saveBoxDraw : saveScreenDraw );
	if( !isPopup && parent->winGetParent() )
		parent->winGetParent()->winSetDrawFunc( saveNothingDraw );
	if( buttonFrame )
		buttonFrame->winSetDrawFunc( saveNothingDraw );
	if( saveDesc )
		saveDesc->winSetDrawFunc( saveDescDraw );
	SPP::takeOver( listboxGames, saveListDraw );
	GadgetListBoxSetFont( listboxGames, UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL ) );
	SPP::setEntry( editDesc );
	SPP::key( TheWindowManager->winGetWindowFromId( NULL, buttonBackKey ), "GUI:Back", FALSE );
	SPP::key( TheWindowManager->winGetWindowFromId( NULL, buttonDeleteKey ), "GUI:SPDelete", FALSE );
	SPP::key( TheWindowManager->winGetWindowFromId( NULL, buttonSaveDescCancel ), "GUI:Cancel", FALSE );
	SPP::key( TheWindowManager->winGetWindowFromId( NULL, buttonSaveDescConfirm ), "GUI:SaveGame", TRUE );
	GameWindow *save = TheWindowManager->winGetWindowFromId( NULL, buttonSaveKey );
	if( save && currentLayoutType == SLLT_LOAD_ONLY )
		save->winHide( TRUE );
}

/// the code draws the rows; each takes its height from an empty picture
static void saveRowHeights( void )
{
	// a cell added to a row drops the list's pick, so the one populate made is put back
	const Int picked = SPP::selectedRow( listboxGames );
	const Int rows = SPP::rowCount( listboxGames );
	for( Int row = 0; row < rows; row++ )
		SPP::rowHeight( listboxGames, row, 1, SPL::SAVE_ROW_HEIGHT );
	GadgetListBoxSetSelected( listboxGames, picked >= 0 ? picked : 0 );
}

/// the confirms through the message box: each answer presses the hidden key EA's own box had
static void pressSaveKey( NameKeyType key )
{
	GameWindow *button = TheWindowManager->winGetWindowFromId( parent, key );
	if( parent && button )
		TheWindowManager->winSendSystemMsg( parent, GBM_SELECTED, (WindowMsgData)button, key );
}
static void loadConfirmed( void ) { pressSaveKey( buttonLoadConfirm ); }
static void loadCancelled( void ) { pressSaveKey( buttonLoadCancel ); }
static void overwriteConfirmed( void ) { pressSaveKey( buttonOverwriteConfirm ); }
static void overwriteCancelled( void ) { pressSaveKey( buttonOverwriteCancel ); }
static void deleteConfirmed( void ) { pressSaveKey( buttonDeleteConfirm ); }
static void deleteCancelled( void ) { pressSaveKey( buttonDeleteCancel ); }

/// copies the pick a confirm asks about; FALSE while a confirm is already up, so a second press
/// (a double-click and the key both) opens no second box
static Bool openConfirm( const AvailableGameInfo *info )
{
	if( confirmOpen || info == NULL )
		return FALSE;
	confirmPick = *info;
	confirmPick.next = confirmPick.prev = NULL;
	confirmOpen = TRUE;
	return TRUE;
}

/// the answered confirm's pick, FALSE when none was up; either answer clears it
static Bool takeConfirm( AvailableGameInfo &info )
{
	if( !confirmOpen )
		return FALSE;
	info = confirmPick;
	confirmPick = AvailableGameInfo();
	confirmOpen = FALSE;
	return TRUE;
}
// ------------------------------------------------------------------------------------------------
/** Given the current layout and selection in the game listbox, update the main save/load
	* menu buttons to be enabled or disabled */
// ------------------------------------------------------------------------------------------------
static void updateMenuActions( void )
{

	// for loading only, disable the save button, otherwise enable it
	GameWindow *saveButton = TheWindowManager->winGetWindowFromId( NULL, buttonSaveKey );
	DEBUG_ASSERTCRASH( saveButton, ("SaveLoadMenuInit: Unable to find save button\n") );
	if( currentLayoutType == SLLT_LOAD_ONLY )
		saveButton->winEnable( FALSE );
	else
		saveButton->winEnable( TRUE );

	// get the games listbox
	//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );

	// if something with a game file is selected we can use load and delete
	Int selected;
	GadgetListBoxGetSelected( listboxGames, &selected );
	AvailableGameInfo *selectedGameInfo;
	selectedGameInfo = (AvailableGameInfo *)GadgetListBoxGetItemData( listboxGames, selected );
	// LOAD GAME dimmed for a save this build cannot read; the lit key is SAVE GAME while the new
	// save is picked, LOAD GAME while a save is
	GameWindow *buttonLoad = TheWindowManager->winGetWindowFromId( NULL, buttonLoadKey );
	buttonLoad->winEnable( selectedGameInfo != NULL && selectedGameInfo->saveGameInfo.isLoadable() );
	const Bool saving = currentLayoutType != SLLT_LOAD_ONLY && selectedGameInfo == NULL;
	SPP::key( buttonLoad, "GUI:LoadGame", !saving );
	SPP::key( saveButton, "GUI:SaveGame", saving );
	GameWindow *buttonDelete = TheWindowManager->winGetWindowFromId( NULL, buttonDeleteKey );
	buttonDelete->winEnable( selectedGameInfo != NULL );
	
}  // end updateMenuActions

//-------------------------------------------------------------------------------------------------
/** Initialize the SaveLoad menu */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuInit( WindowLayout *layout, void *userData )
{

	// set default behavior for this menu
	currentLayoutType = SLLT_SAVE_AND_LOAD;
	isPopup = TRUE;
	confirmOpen = FALSE;
	// get layout type if present
	if( userData )
		currentLayoutType = *((SaveLoadLayoutType *)userData);

  // get ids for our children controls
	buttonBackKey					 = NAMEKEY( "PopupSaveLoad.wnd:ButtonBack" );
	buttonSaveKey					 = NAMEKEY( "PopupSaveLoad.wnd:ButtonSave" );
	buttonLoadKey					 = NAMEKEY( "PopupSaveLoad.wnd:ButtonLoad" );
	buttonDeleteKey        = NAMEKEY( "PopupSaveLoad.wnd:ButtonDelete" );
	listboxGamesKey				 = NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" );
	buttonOverwriteCancel	 = NAMEKEY( "PopupSaveLoad.wnd:ButtonOverwriteCancel" );
	buttonOverwriteConfirm = NAMEKEY( "PopupSaveLoad.wnd:ButtonOverwriteConfirm" );
	buttonLoadCancel			 = NAMEKEY( "PopupSaveLoad.wnd:ButtonLoadCancel" );
	buttonLoadConfirm      = NAMEKEY( "PopupSaveLoad.wnd:ButtonLoadConfirm" );
	buttonSaveDescCancel   = NAMEKEY( "PopupSaveLoad.wnd:ButtonSaveDescCancel" );
	buttonSaveDescConfirm  = NAMEKEY( "PopupSaveLoad.wnd:ButtonSaveDescConfirm" );
	buttonDeleteConfirm		 = NAMEKEY( "PopupSaveLoad.wnd:ButtonDeleteConfirm" );
	buttonDeleteCancel		 = NAMEKEY( "PopupSaveLoad.wnd:ButtonDeleteCancel" );

	//set keyboard focus to main parent and set modal
	NameKeyType parentID = TheNameKeyGenerator->nameToKey("PopupSaveLoad.wnd:SaveLoadMenu");
	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	TheWindowManager->winSetFocus( parent );
	TheWindowManager->winSetModal( parent );

	// enable the menu action buttons
	buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
	buttonFrame->winEnable( TRUE );

	// get confirmation windows and hide
	overwriteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:OverwriteConfirmParent" ) );
	overwriteConfirm->winHide( TRUE );
	loadConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:LoadConfirmParent" ) );
	loadConfirm->winHide( TRUE );
	saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:SaveDescParent" ) );
	saveDesc->winHide( TRUE );
	deleteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:DeleteConfirmParent" ) );
	editDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:EntryDesc" ) );
	// get the listbox that will have the save games in it
	listboxGames = TheWindowManager->winGetWindowFromId( NULL, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != NULL, ("SaveLoadMenuInit - Unable to find games listbox\n") );

	// populate the listbox with the save games on disk
	TheGameState->populateSaveGameListbox( listboxGames, currentLayoutType );
	dressSaveLoad( layout );
	saveRowHeights();

	// update the availability of the menu buttons
	updateMenuActions();

}  // end SaveLoadMenuInit

//-------------------------------------------------------------------------------------------------
/** Initialize the SaveLoad menu */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuFullScreenInit( WindowLayout *layout, void *userData )
{

	TheShell->showShellMap(TRUE);

	isPopup = FALSE;
	confirmOpen = FALSE;
	// set default behavior for this menu
	currentLayoutType = SLLT_LOAD_ONLY;
	
	// get layout type if present
	if( userData )
		currentLayoutType = *((SaveLoadLayoutType *)userData);

  // get ids for our children controls
	buttonBackKey					 = NAMEKEY( "SaveLoad.wnd:ButtonBack" );
	buttonSaveKey					 = NAMEKEY( "SaveLoad.wnd:ButtonSave" );
	buttonLoadKey					 = NAMEKEY( "SaveLoad.wnd:ButtonLoad" );
	buttonDeleteKey        = NAMEKEY( "SaveLoad.wnd:ButtonDelete" );
	listboxGamesKey				 = NAMEKEY( "SaveLoad.wnd:ListboxGames" );
	buttonOverwriteCancel	 = NAMEKEY( "SaveLoad.wnd:ButtonOverwriteCancel" );
	buttonOverwriteConfirm = NAMEKEY( "SaveLoad.wnd:ButtonOverwriteConfirm" );
	buttonLoadCancel			 = NAMEKEY( "SaveLoad.wnd:ButtonLoadCancel" );
	buttonLoadConfirm      = NAMEKEY( "SaveLoad.wnd:ButtonLoadConfirm" );
	buttonSaveDescCancel   = NAMEKEY( "SaveLoad.wnd:ButtonSaveDescCancel" );
	buttonSaveDescConfirm  = NAMEKEY( "SaveLoad.wnd:ButtonSaveDescConfirm" );
	buttonDeleteConfirm		 = NAMEKEY( "SaveLoad.wnd:ButtonDeleteConfirm" );
	buttonDeleteCancel		 = NAMEKEY( "SaveLoad.wnd:ButtonDeleteCancel" );

	//set keyboard focus to main parent and set modal
	NameKeyType parentID = TheNameKeyGenerator->nameToKey("SaveLoad.wnd:SaveLoadMenu");
	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	TheWindowManager->winSetFocus( parent );
//	TheWindowManager->winSetModal( parent );

	// enable the menu action buttons
	buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:MenuButtonFrame" ) );
	buttonFrame->winEnable( TRUE );

	// get confirmation windows and hide
	overwriteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:OverwriteConfirmParent" ) );
	overwriteConfirm->winHide( TRUE );
	loadConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:LoadConfirmParent" ) );
	loadConfirm->winHide( TRUE );
	saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:SaveDescParent" ) );
	saveDesc->winHide( TRUE );

	editDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:EntryDesc" ) );
	deleteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "SaveLoad.wnd:DeleteConfirmParent" ) );
	// get the listbox that will have the save games in it
	listboxGames = TheWindowManager->winGetWindowFromId( NULL, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != NULL, ("SaveLoadMenuInit - Unable to find games listbox\n") );

	// populate the listbox with the save games on disk
	TheGameState->populateSaveGameListbox( listboxGames, currentLayoutType );
	dressSaveLoad( layout );
	saveRowHeights();

	// update the availability of the menu buttons
	updateMenuActions();

	layout->hide(FALSE);
	justEntered = TRUE;
	initialGadgetDelay = 2;
	if(parent)
		parent->winHide(TRUE);
	isShuttingDown = false;
}  // end SaveLoadMenuInit

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu shutdown method */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuShutdown( WindowLayout *layout, void *userData )
{
	
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		layout->hide( TRUE );
		TheShell->shutdownComplete( layout );
		return;

	}  //end if

	// our shutdown is complete
	TheTransitionHandler->reverse("SaveLoadMenuFade");
	isShuttingDown = TRUE;
	saveText.clear();
}  // end SaveLoadMenuShutdown

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu update method */
//-------------------------------------------------------------------------------------------------
void SaveLoadMenuUpdate( WindowLayout *layout, void *userData )
{

	if(DontShowMainMenu && justEntered)
		justEntered = FALSE;
	if(ReplayWasPressed && justEntered)
	{
		justEntered = FALSE;
		ReplayWasPressed = FALSE;
	}
	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->remove("MainMenuDefaultMenuLogoFade");
			TheTransitionHandler->setGroup("SaveLoadMenuFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	if(isShuttingDown && TheShell->isAnimFinished()&& TheTransitionHandler->isFinished())
		TheShell->shutdownComplete( layout );

}  // end SaveLoadMenuUpdate 

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
WindowMsgHandledType SaveLoadMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						GameWindow *button = TheWindowManager->winGetWindowFromId( parent, buttonBackKey );
						
						//Kris: Patch 1.01 - November 12, 2003
						//If you are in the game, then bring up the popup save menu, select a save game, click delete,
						//hit ESC (brings you back to menu), then hit save/load again, the delete confirmation is still up
						//and clicking on yes causes it to crash. So whenever we hit esc to leave this interface, kill
						//the confirmation, and re-enable the listbox and buttonFrame.
						deleteConfirm->winHide( TRUE );
						listboxGames->winEnable( TRUE );
						buttonFrame->winEnable( TRUE );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)button, buttonBackKey );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;
}

// ------------------------------------------------------------------------------------------------
/** Get the file info of the selected savegame file in the listbox */
// ------------------------------------------------------------------------------------------------
static AvailableGameInfo *getSelectedSaveFileInfo( GameWindow *window )
{

	// get the listbox
	//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( window, listboxGamesKey );
	DEBUG_ASSERTCRASH( listboxGames != NULL, ("SaveLoadMenuInit - Unable to find games listbox\n") );

	// which item is selected
	Int selected;
	GadgetListBoxGetSelected( listboxGames, &selected );

	// get the item data of the selection
	AvailableGameInfo *selectedGameInfo;
	selectedGameInfo = (AvailableGameInfo *)GadgetListBoxGetItemData( listboxGames, selected );

	return selectedGameInfo;

}  // end getSelectedSaveFileInfo

// ---------------------------------------------------con------------------------------------------
// ------------------------------------------------------------------------------------------------				// close the save/load menu
// the save comes by value: a listbox row's item data dies with the first engine reset
static void doLoadGame( AvailableGameInfo selectedGameInfo )
{

	// when loading a game we also close the quit/esc menu for the user when in-game
	if( TheShell->isShellActive() == FALSE )
	{
		destroyQuitMenu();		
//		ToggleQuitMenu();
//		TheTransitionHandler->remove("QuitNoSave");
//		TheTransitionHandler->remove("QuitFull");
	}
	else
	{
		TheTransitionHandler->remove("MainMenuLoadReplayMenu");
		TheTransitionHandler->remove("MainMenuLoadReplayMenuBack");
		TheGameLogic->prepareNewGame( GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0 );
	}

	//
	// load game, note the *copy* of the selected game info is passed here because we will
	// loose these allocated user data pointers attached as listbox item data when the
	// engine resets
	//
	if (TheGameState->loadGame( selectedGameInfo ) != SC_OK)
	{
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData( FALSE );
		TheGameEngine->reset();
		TheShell->showShell(TRUE);
	}

}  // end doLoadGame

// ------------------------------------------------------------------------------------------------
/** Close the save/load menu */
// ------------------------------------------------------------------------------------------------
static void closeSaveMenu( GameWindow *window )
{
	
	if(isPopup)
	{
		WindowLayout *saveLoadMenuLayout = window->winGetLayout();
		if( saveLoadMenuLayout )
			saveLoadMenuLayout->hide( TRUE );	
	}
	else
		TheShell->hideShell();

}  // end closeSaveMenu

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void setEditDescription( GameWindow *editControl )
{
	UnicodeString defaultDesc;
	Campaign *campaign = TheCampaignManager->getCurrentCampaign();

	//
	// if we have a campaign we will use a default description that describes the 
	// location and map in the campaign nicely, otherwise we will default to just
	// the map name (which is really only used in debug)
	//
	if( campaign )
		defaultDesc.format( u"%s %d", 
												TheGameText->fetch( campaign->m_campaignNameLabel ).str(),
												TheCampaignManager->getCurrentMissionNumber() + 1 );
	else
	{
		const char *mapName = TheGlobalData->m_mapName.reverseFind( '\\' );

		if( mapName )
			defaultDesc.format( u"%S", mapName + 1 );
		else
			defaultDesc.format( u"%S", TheGlobalData->m_mapName.str() );
		
		//Keep the extension out of the descriptive name.
		if( (defaultDesc.getLength() >= 4)  &&  (defaultDesc.getCharAt(defaultDesc.getLength()-4) == '.') )
		{
			for( Int stripIndex = 0; stripIndex < 4; stripIndex++ )
			{
				defaultDesc.removeLastChar();
			}
		}

	}  // end else

	// set into edit control
	GadgetTextEntrySetText( editControl, defaultDesc );

}  // end setEditDescription

//----------------------------------------------------------------------------------------------
static void processLoadButtonPress(GameWindow *window) 
{
	// get the filename of the selected savegame in the listbox
	AvailableGameInfo *selectedGameInfo = getSelectedSaveFileInfo( window );
	if( selectedGameInfo && !confirmOpen )
	{

		// refused here, before the confirm and before the game or menu is torn down for the load
		if( TheGameState->refuseUnloadableSave( selectedGameInfo->saveGameInfo ) )
			return;

		//
		// if we're in the shell we do not need a confirmation dialog that states we will
		// lose the current loaded game data cause we're not in a game
		//
		if( TheShell->isShellActive() == TRUE )
		{

			// just close the menu and do the load game logic
			closeSaveMenu( window );
			doLoadGame( *selectedGameInfo );

		}  // end if
		else
		{

			//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
			//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
			//GameWindow *loadConfirm  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:LoadConfirmParent" ) );

			openConfirm( selectedGameInfo );

			// disable listbox and buttons
			listboxGames->winEnable( FALSE );
			buttonFrame->winEnable( FALSE );

			// ask through the message box; its answer presses EA's confirm or cancel key
			MessageBoxYesNo( TheGameText->fetch( "GUI:LoadGame" ), TheGameText->fetch( "GUI:LoadGameConfirmation" ), loadConfirmed, loadCancelled );

		}  // end else

	}  // end if
}

//-------------------------------------------------------------------------------------------------
/** SaveLoad menu system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType SaveLoadMenuSystem( GameWindow *window, UnsignedInt msg, 
																				 WindowMsgData mData1, WindowMsgData mData2 )
{

  switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			break;

		}  // end create
    //---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

    //----------------------------------------------------------------------------------------------
    case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}  // end input

    //----------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( window, listboxGamesKey );
				DEBUG_ASSERTCRASH( listboxGames != NULL, ("SaveLoadMenuInit - Unable to find games listbox\n") );

				if (listboxGames != NULL) {
					int rowSelected = mData2;
					GadgetListBoxSetSelected(listboxGames, rowSelected);

					if (control == listboxGames)
					{
						processLoadButtonPress(window);
					}
				}
				break;
			}

		// --------------------------------------------------------------------------------------------
		case GLM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;

			GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( window, listboxGamesKey );
			DEBUG_ASSERTCRASH( listboxGames != NULL, ("SaveLoadMenuInit - Unable to find games listbox\n") );

			//
			// handle games listbox, when certain items are selected in the listbox only some
			// commands are available
			//
			if( control == listboxGames )
				updateMenuActions();

			break;

		}  // end selected

    //---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
      
      if( controlID == buttonLoadKey )
      {
				processLoadButtonPress(window);
      }  // end if
      else if( controlID == buttonSaveKey )
      {

				// sanity
				DEBUG_ASSERTCRASH( currentLayoutType == SLLT_SAVE_AND_LOAD ||
													 currentLayoutType == SLLT_SAVE_ONLY,
													 ("SaveLoadMenuSystem - layout type '%d' does not allow saving\n",
													 currentLayoutType) );
													 	
				// get save file info
				AvailableGameInfo *selectedGameInfo = getSelectedSaveFileInfo( window );

				// if there is no file info, this is a new game
				if( selectedGameInfo == NULL )
				{

					// show the save description window
					//GameWindow *saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:SaveDescParent" ) );
					saveDesc->winHide( FALSE );
					// the box's SAVE GAME is the lit key while it is up
					SPP::key( control, "GUI:SaveGame", FALSE );

					// set the description text entry field to default value
					//GameWindow *editDesc = TheWindowManager->winGetWindowFromId( saveDesc, NAMEKEY( "PopupSaveLoad.wnd:EntryDesc" ) );
					setEditDescription( editDesc );

					// disable the listbox of games
					//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
					listboxGames->winEnable( FALSE );

					// disable the frame window of buttons for the main save/load menu
					//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
					//buttonFrame->winEnable( FALSE );

					TheWindowManager->winSetFocus(editDesc);

				}  // end if
				else if( openConfirm( selectedGameInfo ) )
				{

					// disable listbox of games
					//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
					listboxGames->winEnable( FALSE );

					// disable and therefore lock out main save/load menu buttons
					//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
					buttonFrame->winEnable( FALSE );

					// ask through the message box; its answer presses EA's confirm or cancel key
					MessageBoxYesNo( TheGameText->fetch( "GUI:SaveGame" ), TheGameText->fetch( "GUI:OverwriteSaveConfirmation" ),
						overwriteConfirmed, overwriteCancelled );

				}  // end else

      }
			else if( controlID == buttonDeleteKey )
			{

				// which item is selected in the game listbox
				//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
				Int selected;
				GadgetListBoxGetSelected( listboxGames, &selected );
				AvailableGameInfo *selectedGameInfo;
				selectedGameInfo = (AvailableGameInfo *)GadgetListBoxGetItemData( listboxGames, selected );

				// delete file
				if( openConfirm( selectedGameInfo ) )
				{

					// disable games listbox
					listboxGames->winEnable( FALSE );

					// disable menu buttons
					//GameWindow *buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
					buttonFrame->winEnable( FALSE );

					// ask through the message box, No lit so Enter never deletes
					MessageBoxYesNo( TheGameText->fetch( "GUI:DeleteGame" ), TheGameText->fetch( "GUI:ConfirmDelete" ),
						deleteConfirmed, deleteCancelled, MSG_BOX_NO );

				}  // end if
								
			}  // end else if
			else if( controlID == buttonBackKey )
			{
				if(isPopup)
				{				
					// close the save/load menu
					closeSaveMenu( window );
				}
				else
				{
					TheShell->pop();
				}

			}  // end if
			else if( controlID == buttonDeleteConfirm || controlID == buttonDeleteCancel )
			{
				//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );

				// delete if confirm: the save picked as the box opened, not whatever row is lit now
				AvailableGameInfo picked;
				if( takeConfirm( picked ) && controlID == buttonDeleteConfirm )
				{

					// construct path to filename
					AsciiString filepath = TheGameState->getFilePathInSaveDirectory(picked.filename);

					// delete the file
					zh_remove( filepath.str() );

					// repopulate the listbox
					TheGameState->populateSaveGameListbox( listboxGames, currentLayoutType );
					saveRowHeights();

				}  // end if

				// hide the confirm dialog
				//GameWindow *deleteConfirm = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:DeleteConfirmParent" ) );
				deleteConfirm->winHide( TRUE );

				// enable listbox of games
				listboxGames->winEnable( TRUE );

				// enable menu actions pane
				//GameWindow *buttonFrame = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
				buttonFrame->winEnable( TRUE );
				updateMenuActions();

			}  // end if
			else if( controlID == buttonOverwriteCancel || controlID == buttonOverwriteConfirm )
			{
				//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );

				// hide save confirm dialog
				//GameWindow *overwriteConfirm  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:OverwriteConfirmParent" ) );
				overwriteConfirm->winHide( TRUE );

				// if saving, do the save for the save picked as the box opened
				AvailableGameInfo picked;
				const Bool pickedOpen = takeConfirm( picked );
				if( controlID == buttonOverwriteConfirm && pickedOpen )
				{

					// show the save description window
//					GameWindow *saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:SaveDescParent" ) );
//					saveDesc->winHide( FALSE );

					// enable the listbox of games
					//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
					listboxGames->winEnable( TRUE );

					// enable the frame window of buttons for the main save/load menu
					//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
					buttonFrame->winEnable( TRUE );
					updateMenuActions();
					
					//Added By Sadullah Nader
					//Fix for bug
					// close save menuu
					closeSaveMenu( window );
					
					//
					// given the context of this menu figure out which type of save game we're acutally
					// saving right now.  As it turns out, when this menu is used in the save only
					// mode it means that the save is a mission save between maps because you can only
					// save the game between maps and can of course not load one
					//
					SaveFileType fileType;
					if( currentLayoutType == SLLT_SAVE_AND_LOAD )
						fileType = SAVE_FILE_TYPE_NORMAL;
					else
						fileType = SAVE_FILE_TYPE_MISSION;

					// save the game
					TheGameState->saveGame( picked.filename, picked.saveGameInfo.description, fileType );

/*
					// set the description text entry field to default value
					GameWindow *editDesc = TheWindowManager->winGetWindowFromId( saveDesc, NAMEKEY( "PopupSaveLoad.wnd:EntryDesc" ) );
					setEditDescription( editDesc );
*/
				}  // end if
				else
				{
					//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );

					// cancelled, or a Yes with no confirm up: enable buttons and list box on main parent
					buttonFrame->winEnable( TRUE );
					updateMenuActions();
					listboxGames->winEnable( TRUE );

				}  // end else if

			}  // end else if
			else if( controlID == buttonSaveDescConfirm )
			{

				// get description window
				//GameWindow *saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:SaveDescParent" ) );

				// get description text
				//GameWindow *entryDesc = TheWindowManager->winGetWindowFromId( saveDesc, NAMEKEY( "PopupSaveLoad.wnd:EntryDesc" ) );
				UnicodeString desc = GadgetTextEntryGetText( editDesc );

				// hide desc window
				saveDesc->winHide( TRUE );

				// enable the listbox of games
				//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
				listboxGames->winEnable( TRUE );

				// enable the frame window of buttons for the main save/load menu
				//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
				buttonFrame->winEnable( TRUE );
				updateMenuActions();

				// close save menuu
				closeSaveMenu( window );

				// get save filename
				AvailableGameInfo *selectedGameInfo = getSelectedSaveFileInfo( listboxGames );

				//
				// given the context of this menu figure out which type of save game we're acutally
				// saving right now.  As it turns out, when this menu is used in the save only
				// mode it means that the save is a mission save between maps because you can only
				// save the game between maps and can of course not load one
				//
				SaveFileType fileType;
				if( currentLayoutType == SLLT_SAVE_AND_LOAD )
					fileType = SAVE_FILE_TYPE_NORMAL;
				else
					fileType = SAVE_FILE_TYPE_MISSION;

				// save the game
				AsciiString filename;
				if( selectedGameInfo )
					filename = selectedGameInfo->filename;
				TheGameState->saveGame( filename, desc, fileType );

			}  // end else if
			else if( controlID == buttonSaveDescCancel )
			{

				// hide the desc window
				//GameWindow *saveDesc = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:SaveDescParent" ) );
				saveDesc->winHide( TRUE );

				// enable the listbox of games
				//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
				listboxGames->winEnable( TRUE );

				// enable the frame window of buttons for the main save/load menu
				//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
				buttonFrame->winEnable( TRUE );
				updateMenuActions();

			}  // end else if
			else if( controlID == buttonLoadConfirm || controlID == buttonLoadCancel )
			{

				// hide confirm dialog
				//GameWindow *loadConfirm  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:LoadConfirmParent" ) );
				loadConfirm->winHide( TRUE );

				// enable the listbox of games again
				//GameWindow *listboxGames = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:ListboxGames" ) );
				listboxGames->winEnable( TRUE );

				// enable the main save/load menu button controls
				//GameWindow *buttonFrame  = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "PopupSaveLoad.wnd:MenuButtonFrame" ) );
				buttonFrame->winEnable( TRUE );
				updateMenuActions();

				
				

				// do the load game, of the save picked as the box opened
				AvailableGameInfo picked;
				if( takeConfirm( picked ) && controlID == buttonLoadConfirm )
				{
					//Moved by Sadullah Nader
					//moved to fix the
					// close save/load layout menu
					closeSaveMenu( window );
					doLoadGame( picked );
				}

			}  // end else if
	
			break;

		}  // end selected

		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}
