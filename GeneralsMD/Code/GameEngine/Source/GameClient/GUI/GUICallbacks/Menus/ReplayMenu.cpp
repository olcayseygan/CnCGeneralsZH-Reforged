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

// FILE: ReplayMenu.cpp /////////////////////////////////////////////////////////////////////
// Author: Chris The masta Huybregts, December 2001
// Description: Replay Menus
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Lib/BaseType.h"
#include "Common/LocalFileSystem.h"
#if !defined(_WIN32)
#include "Common/EarlyOptions.h"	// findDesktopDirectory
#endif
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/Recorder.h"
#include "Common/version.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MessageBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowTransitions.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/SinglePlayerParts.h"
#include "GameNetwork/GameInfo.h"

#include <algorithm>
#include <map>
#include <vector>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// window ids -------------------------------------------------------------------------------------
static NameKeyType parentReplayMenuID = NAMEKEY_INVALID;
static NameKeyType buttonLoadID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType listboxReplayFilesID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteID = NAMEKEY_INVALID;
static NameKeyType buttonCopyID = NAMEKEY_INVALID;

static Bool isShuttingDown = false;

// window pointers --------------------------------------------------------------------------------
static GameWindow *parentReplayMenu = NULL;
static GameWindow *buttonLoad = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *listboxReplayFiles = NULL;
static GameWindow *buttonDelete = NULL;
static GameWindow *buttonCopy = NULL;
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;


#if defined _DEBUG || defined _INTERNAL
static GameWindow *buttonAnalyzeReplay = NULL;
#endif

void deleteReplay( void );
void copyReplay( void );
static Bool callCopy = FALSE;
static Bool callDelete = FALSE;
void deleteReplayFlag( void ) { callDelete = TRUE;}
void copyReplayFlag( void ) { callCopy = TRUE;}

// The replay the "this was recorded by an older version" prompt is asking about.  Held from the
// moment the prompt goes up, because the menu underneath stays live: delete a replay while it is
// on screen and the list is rebuilt, so the row index the prompt was opened on now names a
// different file, or none.
static AsciiString thePendingReplayFilename;

// The rows' details for every list PopulateReplayFileListbox fills (this screen's and the Save
// Replay box's), in the order the rows stand; a row's item data is its index here plus one.
static std::map< GameWindow *, std::vector< ReplayRowInfo > > theReplayRows;

static bool replayNewerFirst( const ReplayRowInfo &a, const ReplayRowInfo &b )
{
	return a.sortKey > b.sortKey;
}

namespace SPL = SinglePlayerLayout;
namespace SPP = SinglePlayerParts;

static MenuBox::TextPool replayText;

using SinglePlayerParts::speedText;
using SinglePlayerParts::lengthText;

void DrawReplayRows( GameWindow *list, MenuBox::TextPool &pool, const SinglePlayerLayout::Column *columns,
	const Int *kinds, Int count, Int rowRight1080 )
{
	using namespace UIStyle;
	const IRegion2D box = MenuBox::windowRect( list );
	const Int right = SPP::point( (Real)rowRight1080, 0.0f ).x;
	const Int selected = SPP::selectedRow( list ), hovered = SPP::hoveredRow( list );
	const Int rows = SPP::rowCount( list );
	for (Int row = 0; row < rows; ++row)
	{
		const IRegion2D r = SPP::rowRect( list, row, right );
		if (r.lo.y < box.lo.y || r.hi.y > box.hi.y + 1)
			continue;
		const ReplayRowInfo *info = GetReplayRowInfo( list, row );
		if (info == NULL)
			continue;
		menuRow( r, row == hovered && row != selected ? ROW_HOVER : ROW_PLAIN, ( row & 1 ) != 0 );
		if (row == selected)
			SPP::picked( r );
		const Color ink = info->older ? MUTED : INK;
		const Int middle = ( r.lo.y + r.hi.y ) / 2;
		for (Int c = 0; c < count; ++c)
		{
			const IRegion2D cell = SPP::on( SPL::rect( columns[c].left, 0, columns[c].right, 1 ) );
			const Int w = cell.hi.x - cell.lo.x;
			const Int x = columns[c].alignRight ? cell.hi.x : cell.lo.x;
			const MenuBox::Align align = columns[c].alignRight ? MenuBox::RIGHT : MenuBox::LEFT;
			UnicodeString figure;
			switch (kinds[c])
			{
				case SPL::REPLAY_NAME:
					SPP::words( pool, info->label, MENU_TEXT_BODY, FALSE, x, middle, ink, w, align, TRUE );
					break;
				case SPL::REPLAY_RECORDED:
					SPP::words( pool, info->recorded, MENU_TEXT_BODY, FALSE, x, middle, ink, w, align );
					break;
				case SPL::REPLAY_MAP:
					SPP::words( pool, info->map, MENU_TEXT_BODY, FALSE, x, middle, ink, w, align, TRUE );
					break;
				case SPL::REPLAY_PLAYERS:
					figure.format( u"%d", (Int)info->seats.size() );
					SPP::words( pool, figure, MENU_TEXT_BODY, FALSE, x, middle, ink, w, align );
					break;
				case SPL::REPLAY_LENGTH:
					SPP::words( pool, lengthText( info->seconds ), MENU_TEXT_BODY, FALSE, x, middle, ink, w, align );
					break;
				case SPL::REPLAY_VERSION:
				{
					const Int olderRoom = info->older ? SPP::px( 64.0f ) : 0;
					const Int vw = SPP::words( pool, info->version, MENU_TEXT_BODY, FALSE, x, middle, ink, w - olderRoom, align, TRUE );
					if (info->older)
						SPP::capitalsKey( "GUI:SPOlder", x + vw + SPP::px( 10.0f ), middle, MUTED, MenuBox::LEFT, olderRoom - SPP::px( 10.0f ) );
					break;
				}
			}
		}
	}
}

/// the replay list's own rows and its 3 px bar
static void replayListDraw( GameWindow *window, WinInstanceData * )
{
	SPL::Column columns[ SPL::REPLAY_COLUMNS ];
	Int kinds[ SPL::REPLAY_COLUMNS ];
	for (Int c = 0; c < SPL::REPLAY_COLUMNS; ++c)
	{
		columns[c] = SPL::replayColumn( c );
		kinds[c] = c;
	}
	DrawReplayRows( window, replayText, columns, kinds, SPL::REPLAY_COLUMNS, SPL::REPLAY_ROW_RIGHT );
	SPP::scrollBar( window, SPP::point( (Real)SPL::REPLAY_SCROLL_X, 0.0f ).x );
}

/// the preview: the map's own picture at its shape, every seat's start in its colour, the recorder's ringed gold
static void drawReplayPreview( const ReplayRowInfo &info )
{
	using namespace UIStyle;
	static AsciiString s_previewOf;
	static Image *s_preview = NULL;
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( info.mapName ) : NULL;
	if (s_previewOf.compareNoCase( info.mapName ) != 0)
	{
		s_previewOf = info.mapName;
		s_preview = md ? getMapPreviewImage( info.mapName ) : NULL;
	}
	const Real mw = md ? md->m_extent.hi.x - md->m_extent.lo.x : 0.0f;
	const Real mh = md ? md->m_extent.hi.y - md->m_extent.lo.y : 0.0f;
	const IRegion2D r = SPP::on( SPL::fitShape( SPL::REPLAY_PREVIEW, mw, mh ) );
	MenuBox::fill( r, GameMakeColor( 0, 0, 0, 150 ) );
	if (s_preview == NULL)
	{
		SPP::capitalsKey( "GUI:SPNoPreview", ( r.lo.x + r.hi.x ) / 2, ( r.lo.y + r.hi.y ) / 2, MUTED, MenuBox::CENTRE );
		MenuBox::outline( r, withAlpha( FRAME, 220 ) );
		return;
	}
	SPP::framed( s_preview, r, FALSE );
	if (md == NULL || mw <= 0.0f || mh <= 0.0f)
		return;
	for (size_t each = 0; each < info.seats.size(); ++each)
	{
		const ReplaySeatInfo &seat = info.seats[each];
		if (seat.startPos < 0)
			continue;
		AsciiString waypoint;
		waypoint.format( "Player_%d_Start", seat.startPos + 1 );
		WaypointMap::const_iterator at = md->m_waypoints.find( waypoint );
		if (at == md->m_waypoints.end())
			continue;
		const Int x = r.lo.x + (Int)( ( at->second.x - md->m_extent.lo.x ) / mw * ( r.hi.x - r.lo.x ) );
		const Int y = r.hi.y - (Int)( ( at->second.y - md->m_extent.lo.y ) / mh * ( r.hi.y - r.lo.y ) );
		if (seat.recorder)
			SPP::ring( replayText, x, y, SPP::px( 11.0f ), GOLD, 0, 2 );
		SPP::disc( x, y, SPP::px( 6.0f ), seat.colour );
	}
}

/// the details column: the name, the map and teams, the preview, six facts, the players
static void drawReplayDetails( const ReplayRowInfo *info )
{
	using namespace UIStyle;
	if (info == NULL)
		return;
	const IRegion2D name = SPP::on( SPL::REPLAY_DETAILS_NAME );
	SPP::words( replayText, info->label, MENU_TEXT_ROW_FIGURE, FALSE, name.lo.x, ( name.lo.y + name.hi.y ) / 2, INK,
		name.hi.x - name.lo.x, MenuBox::LEFT, TRUE );
	const IRegion2D line = SPP::on( SPL::REPLAY_DETAILS_LINE );
	UnicodeString mapLine = info->map;
	mapLine.concat( u"  \x00b7  " );
	mapLine.concat( info->teams );
	SPP::words( replayText, mapLine, MENU_TEXT_BODY, FALSE, line.lo.x, ( line.lo.y + line.hi.y ) / 2, MUTED,
		line.hi.x - line.lo.x, MenuBox::LEFT, TRUE );
	drawReplayPreview( *info );

	UnicodeString version = info->version;
	version.concat( TheGameText->fetch( info->older ? "GUI:SPOlderBuild" : "GUI:SPThisBuild" ) );
	const char *labels[ 6 ] = { "GUI:SPRecorded", "GUI:SPLength", "GUI:SPSpeed", "GUI:SPVersion", "GUI:SPGame", "GUI:SPEnded" };
	const UnicodeString values[ 6 ] =
	{
		info->recorded, lengthText( info->seconds ), speedText( info->speedPercent ), version,
		TheGameText->fetch( info->multiplayer ? "GUI:SPMultiplayer" : "GUI:SPSinglePlayer" ),
		TheGameText->fetch( info->desync ? "GUI:SPOutOfSync" : info->quitEarly ? "GUI:SPQuitEarly" : "GUI:SPPlayedOut" )
	};
	for (Int each = 0; each < 6; ++each)
	{
		const IRegion2D cell = SPP::on( SPL::replayFact( each ) );
		SPP::capitalsKey( labels[each], cell.lo.x, cell.lo.y + SPP::px( 8.0f ), MUTED, MenuBox::LEFT, cell.hi.x - cell.lo.x );
		SPP::words( replayText, values[each], MENU_TEXT_BODY, FALSE, cell.lo.x, cell.lo.y + SPP::px( 33.0f ),
			each == 3 && info->older ? MUTED : INK, cell.hi.x - cell.lo.x, MenuBox::LEFT, TRUE );
	}

	const Int x0 = SPP::point( (Real)SPL::REPLAY_DETAILS_LEFT, 0.0f ).x, x1 = SPP::point( (Real)SPL::RIGHT, 0.0f ).x;
	const Int top = SPP::point( 0.0f, (Real)SPL::REPLAY_PLAYERS_TOP ).y;
	SPP::section( replayText, x0, x1, top, TheGameText->fetch( "GUI:SPPlayers" ) );
	const Int seats = (Int)info->seats.size();
	const Int pitch = SPL::replayPlayerRow( seats );
	const Int columns = SPL::replayPlayerColumns( seats );
	const Int rows = ( seats + columns - 1 ) / columns;
	const Int columnWidth = ( SPL::RIGHT - SPL::REPLAY_DETAILS_LEFT - ( columns - 1 ) * 16 ) / columns;
	// one column has room for the army's name; two keep the name, LEFT and the team
	const Int size = pitch >= 28 ? MENU_TEXT_BODY : MENU_TEXT_LABEL;
	for (Int each = 0; each < seats; ++each)
	{
		const ReplaySeatInfo &seat = info->seats[each];
		const Int y0 = SPL::REPLAY_PLAYERS_TOP + 34 + ( each % rows ) * pitch;
		const Int cx0 = SPL::REPLAY_DETAILS_LEFT + ( each / rows ) * ( columnWidth + 16 );
		const IRegion2D r = SPP::on( SPL::rect( cx0, y0, cx0 + columnWidth, y0 + pitch ) );
		if (seat.recorder)
			menuRow( r, ROW_LOCAL );
		const Int middle = ( r.lo.y + r.hi.y ) / 2;
		const Int h = r.hi.y - r.lo.y;
		MenuBox::stripe( r.lo.x + SPP::px( 6.0f ), r.lo.y + 2, r.hi.y - 2, max( 2, SPP::px( 4.0f ) ), seat.colour );
		const Int pic = max( 4, h - SPP::px( 6.0f ) );
		const Int px0 = r.lo.x + SPP::px( 18.0f );
		const Image *portrait = seat.army ? UIStyle::armyPortrait( seat.army ) : NULL;
		if (portrait)
			UIStyle::coverImage( portrait, px0, middle - pic / 2, pic, pic );
		const Int nx = px0 + pic + SPP::px( 10.0f );
		const Int tagsX = r.hi.x - SPP::px( columns > 1 ? 130.0f : 170.0f );
		const Int armyX = columns > 1 ? tagsX : r.lo.x + SPP::px( 300.0f );
		SPP::words( replayText, seat.shown, size, seat.recorder, nx, middle, seat.recorder ? GOLD : INK,
			armyX - nx - SPP::px( 10.0f ), MenuBox::LEFT, TRUE );
		if (seat.army && columns == 1)
			SPP::words( replayText, seat.army->getDisplayName(), size, FALSE, armyX, middle, MUTED,
				tagsX - armyX - SPP::px( 10.0f ), MenuBox::LEFT, TRUE );
		if (seat.left)
			SPP::capitalsKey( "GUI:SPLeft", tagsX, middle, MUTED, MenuBox::LEFT, SPP::px( 60.0f ) );
		if (seat.team >= 0)
		{
			UnicodeString team;
			team.format( TheGameText->fetch( "GUI:SPTeam" ), seat.team + 1 );
			SPP::capitals( team, r.hi.x - SPP::px( 6.0f ), middle, MUTED, MenuBox::RIGHT, SPP::px( columns > 1 ? 60.0f : 80.0f ) );
		}
	}
}

/// the screen: frame A over the shell map, the title with the count, the heads, the legend, the details
static void replayMenuDraw( GameWindow *, WinInstanceData * )
{
	using namespace UIStyle;
	replayText.begin();
	const IRegion2D panel = boardRect( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM );
	glass( panel, 168 );
	frameA( panel );
	GameWindow *list = listboxReplayFiles;
	UnicodeString count;
	count.format( TheGameText->fetch( "GUI:SPReplayCount" ), list ? SPP::rowCount( list ) : 0 );
	menuHeader( replayText.get( capitals( TheGameText->fetch( "GUI:SPReplays" ) ) ), replayText.get( count ) );
	static const char *heads[ SPL::REPLAY_COLUMNS ] = { "GUI:SPName", "GUI:SPRecorded", "GUI:SPMap", "GUI:SPPlayers", "GUI:SPLength", "GUI:SPVersion" };
	SPL::Column columns[ SPL::REPLAY_COLUMNS ];
	UnicodeString labels[ SPL::REPLAY_COLUMNS ];
	for (Int c = 0; c < SPL::REPLAY_COLUMNS; ++c)
	{
		columns[c] = SPL::replayColumn( c );
		labels[c] = TheGameText->fetch( heads[c] );
	}
	SPP::heads( columns, labels, SPL::REPLAY_COLUMNS, SPL::REPLAY_RECORDED, SPL::REPLAY_HEADS.lo.y, SPL::LEFT, SPL::REPLAY_LIST_RIGHT );
	const IRegion2D legend = SPP::on( SPL::REPLAY_LEGEND );
	SPP::capitalsKey( "GUI:SPOlderLine", legend.lo.x, ( legend.lo.y + legend.hi.y ) / 2, MUTED, MenuBox::LEFT, legend.hi.x - legend.lo.x );
	if (list)
		drawReplayDetails( GetReplayRowInfo( list, SPP::selectedRow( list ) ) );
	const IRegion2D foot = SPP::on( SPL::rect( SPL::LEFT, SPL::FOOT_RULE, SPL::RIGHT, SPL::FOOT_RULE + 1 ) );
	MenuBox::rule( foot.lo.x, foot.hi.x, foot.lo.y, 110 );
}

static void replayNothingDraw( GameWindow *, WinInstanceData * )
{
}

UnicodeString GetReplayFilenameFromListbox(GameWindow *listbox, Int index)
{
	UnicodeString fname = GadgetListBoxGetText(listbox, index);

	if (fname == TheGameText->fetch("GUI:LastReplay"))
	{
		fname.translate(TheRecorder->getLastReplayFileName());
	}

	UnicodeString ext;
	ext.translate(TheRecorder->getReplayExtention());
	fname.concat(ext);

	return fname;
}


//-------------------------------------------------------------------------------------------------
/** Populate the listbox with the names of the available replay files */
//-------------------------------------------------------------------------------------------------
void PopulateReplayFileListbox(GameWindow *listbox)
{
	if (!TheMapCache)
		return;
	
	GadgetListBoxReset(listbox);
	std::vector< ReplayRowInfo > &rows = theReplayRows[ listbox ];
	rows.clear();

	AsciiString asciistr;
	AsciiString asciisearch;
	asciisearch = "*";
	asciisearch.concat(TheRecorder->getReplayExtention());

	FilenameList replayFilenames;
	FilenameListIter it;

	TheFileSystem->getFileListInDirectory(TheRecorder->getReplayDir(), asciisearch, replayFilenames, TRUE);

	TheMapCache->updateCache();


	for (it = replayFilenames.begin(); it != replayFilenames.end(); ++it)
	{
		// just want the filename
		asciistr.set((*it).reverseFind('\\') + 1);

		// lets get some info about the replay
		RecorderClass::ReplayHeader header;
		header.forPlayback = FALSE;
		header.filename = asciistr;
		Bool success = TheRecorder && TheMapCache && TheRecorder->readReplayHeader( header );
		if (success)
		{
			ReplayGameInfo info;
			if (ParseAsciiStringToGameInfo( &info, header.gameOptions ))
			{

				// columns are: name, date, version, map, extra

				// name
				header.replayName.translate(asciistr);
				for (Int tmp=0; tmp < TheRecorder->getReplayExtention().getLength(); ++tmp)
					header.replayName.removeLastChar();

				UnicodeString replayNameToShow = header.replayName;

				AsciiString lastReplayFName = TheRecorder->getLastReplayFileName();
				lastReplayFName.concat(TheRecorder->getReplayExtention());
				if (lastReplayFName.compareNoCase(asciistr) == 0)
					replayNameToShow = TheGameText->fetch("GUI:LastReplay");

				// The column is headed "date" and only ever held the clock time, so a folder of
				// replays recorded over months all read as the same handful of afternoons and there
				// was no way to tell last night's game from one in April. Date first, then the time,
				// both in whatever format the player's own Region settings ask for.
				UnicodeString displayTimeBuffer = getUnicodeDateBuffer(header.timeVal);
				displayTimeBuffer.concat( u" " );
				displayTimeBuffer.concat( getUnicodeTimeBuffer(header.timeVal) );

				// map
				UnicodeString mapStr;
				const MapMetaData *md = TheMapCache->findMap(info.getMap());
				if (!md)
				{
					// a map this install lacks: its file's name, not the folder it was played from
					AsciiString leaf = info.getMap();
					const char *slash = leaf.reverseFind('\\');
					if (slash)
						leaf.set(slash + 1);
					if (leaf.getLength() > 4 && leaf.getCharAt(leaf.getLength() - 4) == '.')
						for (Int cut = 0; cut < 4; ++cut)
							leaf.removeLastChar();
					mapStr.translate(leaf);
				}
				else
				{
					mapStr = md->m_displayName;
				}

				// the row and its details, read off the header the list already reads
				ReplayRowInfo row;
				row.label = replayNameToShow;
				row.recorded = displayTimeBuffer;
				row.map = mapStr;
				row.mapName = info.getMap();
				row.version = header.versionString;
				row.older = !( header.versionString == TheVersion->getUnicodeVersion() && header.versionNumber == TheVersion->getVersionNumber() &&
					header.exeCRC == TheGlobalData->m_exeCRC && header.iniCRC == TheGlobalData->m_iniCRC );
				row.seconds = (Int)( header.endTime - header.startTime );
				row.speedPercent = header.gameSpeedPercent > 0 ? header.gameSpeedPercent : RETAIL_GAME_SPEED_PERCENT;
				row.multiplayer = header.localPlayerIndex >= 0;
				row.quitEarly = header.quitEarly;
				row.desync = header.desyncGame;
				row.sortKey = ( ( ( ( (Int64)header.timeVal.wYear * 13 + header.timeVal.wMonth ) * 32 + header.timeVal.wDay ) * 24
					+ header.timeVal.wHour ) * 60 + header.timeVal.wMinute ) * 60 + header.timeVal.wSecond;
				Int recorder = header.localPlayerIndex;
				std::vector< Int > teams;
				for (Int i = 0; i < MAX_SLOTS; ++i)
				{
					const GameSlot *slot = info.getConstSlot(i);
					if (slot == NULL || !slot->isOccupied())
						continue;
					if (recorder < 0 && slot->isHuman())
						recorder = i;
					ReplaySeatInfo seat;
					seat.name = slot->isAI() ? SlotStateName( slot->getState(), slot->getAIRating() ) : slot->getName();
					const MultiplayerColorDefinition *colour = slot->getColor() >= 0 ? TheMultiplayerSettings->getColor( slot->getColor() ) : NULL;
					seat.colour = colour ? colour->getColor() : UIStyle::MUTED;
					seat.army = slot->getPlayerTemplate() >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( slot->getPlayerTemplate() ) : NULL;
					seat.team = slot->getTeamNumber();
					seat.startPos = slot->getStartPos();
					seat.left = header.playerDiscons[i];
					seat.recorder = i == recorder && slot->isHuman();
					row.seats.push_back( seat );
					teams.push_back( seat.team );
				}
				// computer seats that share a level's name are numbered
				for (size_t a = 0; a < row.seats.size(); ++a)
				{
					Int same = 0, before = 0;
					for (size_t b = 0; b < row.seats.size(); ++b)
						if (row.seats[b].name == row.seats[a].name)
						{
							same++;
							if (b < a)
								before++;
						}
					if (same > 1)
					{
						UnicodeString numbered;
						numbered.format( u"%s %d", row.seats[a].name.str(), before + 1 );
						row.seats[a].shown = numbered;
					}
					else
						row.seats[a].shown = row.seats[a].name;
				}
				char teamText[64];
				SinglePlayerLayout::teamsText( teams, teamText, sizeof( teamText ) );
				row.teams.translate( AsciiString( teamText ) );
				rows.push_back( row );

//				// extra
//				UnicodeString extraStr;
//				if (header.localPlayerIndex >= 0)
//				{
//					// MP game
//					time_t totalSeconds = header.endTime - header.startTime;
//					Int mins = totalSeconds/60;
//					Int secs = totalSeconds%60;
//					Real fps = header.frameDuration/totalSeconds;
//					extraStr.format(L"%d:%d (%g fps) %hs", mins, secs, fps, header.desyncGame?"OOS ":"");
//
//					for (Int i=0; i<MAX_SLOTS; ++i)
//					{
//						const GameSlot *slot = info.getConstSlot(i);
//						if (slot && slot->isHuman())
//						{
//							if (i)
//								extraStr.concat(L", ");
//							if (header.playerDiscons[i])
//								extraStr.concat(L'*');
//							extraStr.concat(info.getConstSlot(i)->getName());
//						}
//					}
//				}
//				else
//				{
//					// solo game
//					time_t totalSeconds = header.endTime - header.startTime;
//					Int mins = totalSeconds/60;
//					Int secs = totalSeconds%60;
//					Real fps = header.frameDuration/totalSeconds;
//					extraStr.format(L"%d:%d (%g fps)", mins, secs, fps);
//				}

			}
		}
	}

	// newest first, the list sorted by RECORDED; the code draws the rows, the gadget keeps the name
	// in column 0 (GetReplayFilenameFromListbox reads it) and a row height in column 1
	std::stable_sort( rows.begin(), rows.end(), replayNewerFirst );
	for (size_t each = 0; each < rows.size(); ++each)
	{
		Int index = GadgetListBoxAddEntryText(listbox, rows[each].label, UIStyle::INK, -1, 0);
		SinglePlayerParts::rowHeight( listbox, index, 1, SinglePlayerLayout::REPLAY_ROW_HEIGHT );
		GadgetListBoxSetItemData( listbox, (void *)( each + 1 ), index );
	}
	GadgetListBoxSetSelected(listbox, 0);
}

const ReplayRowInfo *GetReplayRowInfo( GameWindow *listbox, Int row )
{
	if (listbox == NULL || row < 0 || row >= GadgetListBoxGetNumEntries( listbox ))
		return NULL;
	const size_t index = (size_t)GadgetListBoxGetItemData( listbox, row );
	std::map< GameWindow *, std::vector< ReplayRowInfo > >::const_iterator it = theReplayRows.find( listbox );
	if (index == 0 || it == theReplayRows.end() || index > it->second.size())
		return NULL;
	return &it->second[ index - 1 ];
}

//-------------------------------------------------------------------------------------------------
/** Initialize the single player menu */
//-------------------------------------------------------------------------------------------------
void ReplayMenuInit( WindowLayout *layout, void *userData )
{
	TheShell->showShellMap(TRUE);

	// get ids for our children controls
	parentReplayMenuID = TheNameKeyGenerator->nameToKey( AsciiString("ReplayMenu.wnd:ParentReplayMenu") );
	buttonLoadID = TheNameKeyGenerator->nameToKey( AsciiString("ReplayMenu.wnd:ButtonLoadReplay") );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString("ReplayMenu.wnd:ButtonBack") );
	listboxReplayFilesID = TheNameKeyGenerator->nameToKey( AsciiString("ReplayMenu.wnd:ListboxReplayFiles") );
	buttonDeleteID = TheNameKeyGenerator->nameToKey( AsciiString("ReplayMenu.wnd:ButtonDeleteReplay") );
	buttonCopyID = TheNameKeyGenerator->nameToKey( AsciiString("ReplayMenu.wnd:ButtonCopyReplay") );

	parentReplayMenu = TheWindowManager->winGetWindowFromId( NULL, parentReplayMenuID );
	buttonLoad = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonLoadID );
	buttonBack = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonBackID );
	listboxReplayFiles = TheWindowManager->winGetWindowFromId( parentReplayMenu, listboxReplayFilesID );
	buttonDelete = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonDeleteID );
	buttonCopy = TheWindowManager->winGetWindowFromId( parentReplayMenu, buttonCopyID );

	// the menu language: the parent draws the screen, the list its own rows, the keys outline only
	if (parentReplayMenu)
		parentReplayMenu->winSetDrawFunc( replayMenuDraw );
	GameWindow *gadgets = TheWindowManager->winGetWindowFromId( parentReplayMenu, TheNameKeyGenerator->nameToKey( "ReplayMenu.wnd:GadgetParent" ) );
	if (gadgets)
		gadgets->winSetDrawFunc( replayNothingDraw );
	SPP::takeOver( listboxReplayFiles, replayListDraw );
	GadgetListBoxSetFont( listboxReplayFiles, UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL ) );
	SPP::key( buttonBack, "GUI:Back", FALSE );
	SPP::key( buttonCopy, "GUI:SPCopyToDesktop", FALSE );
	SPP::key( buttonDelete, "GUI:SPDelete", FALSE );
	SPP::key( buttonLoad, "GUI:SPWatchReplay", TRUE );

	//Load the listbox shiznit
	GadgetListBoxReset(listboxReplayFiles);
	PopulateReplayFileListbox(listboxReplayFiles);

#if defined _DEBUG || defined _INTERNAL
	WinInstanceData instData;
	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	instData.m_textLabelString = "Debug: Analyze Replay";
	instData.setTooltipText(UnicodeString(u"Only Used in Debug and Internal!"));
	buttonAnalyzeReplay = TheWindowManager->gogoGadgetPushButton( parentReplayMenu, 
																									 WIN_STATUS_ENABLED | WIN_STATUS_IMAGE, 
																									 4, 4, 
																									 180, 26, 
																									 &instData, NULL, TRUE );
#endif

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentReplayMenu );
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ReplayMenu.wnd:GadgetParent"));
	if(win)
		win->winHide(TRUE);
	isShuttingDown = FALSE;

}  // end ReplayMenuInit

//-------------------------------------------------------------------------------------------------
/** single player menu shutdown method */
//-------------------------------------------------------------------------------------------------
void ReplayMenuShutdown( WindowLayout *layout, void *userData )
{

	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		layout->hide( TRUE );
		TheShell->shutdownComplete( layout );
		return;

	}  //end if

	// our shutdown is complete
	replayText.clear();
	TheTransitionHandler->reverse("ReplayMenuFade");
	isShuttingDown = TRUE;
}  // end ReplayMenuShutdown

//-------------------------------------------------------------------------------------------------
/** single player menu update method */
//-------------------------------------------------------------------------------------------------
void ReplayMenuUpdate( WindowLayout *layout, void *userData )
{
	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->remove("MainMenuDefaultMenuLogoFade");
			TheTransitionHandler->setGroup("ReplayMenuFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	if(callCopy)
		copyReplay();
	if(callDelete)
		deleteReplay();
		// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished()&& TheTransitionHandler->isFinished())
		TheShell->shutdownComplete( layout );

}  // end ReplayMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Replay menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ReplayMenuInput( GameWindow *window, UnsignedInt msg,
																						WindowMsgData mData1, WindowMsgData mData2 )
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

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)buttonBack, buttonBackID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end ReplayMenuInput

void reallyLoadReplay(void)
{
	// The name was taken when the prompt went up, not now: the list behind it can have been
	// rebuilt since, and re-reading the selected row would play whatever moved into it.
	AsciiString asciiFilename = thePendingReplayFilename;
	thePendingReplayFilename.clear();

	if(asciiFilename.isEmpty())
	{
		MessageBoxOk(TheGameText->fetch("GUI:NoFileSelected"),TheGameText->fetch("GUI:PleaseSelectAFile"), NULL);
		return;
	}

	TheRecorder->playbackFile(asciiFilename);

	if(parentReplayMenu != NULL)
	{
		parentReplayMenu->winHide(TRUE);
	}	
}

//-------------------------------------------------------------------------------------------------
/** single player menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ReplayMenuSystem( GameWindow *window, UnsignedInt msg, 
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

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}  // end input
		//---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxReplayFilesID ) 
				{
					int rowSelected = mData2;
				
					if (rowSelected >= 0)
					{
						UnicodeString filename;
						filename = GetReplayFilenameFromListbox(listboxReplayFiles, rowSelected);

						AsciiString asciiFilename;
						asciiFilename.translate(filename);
						TheRecorder->playbackFile(asciiFilename);

						if(parentReplayMenu != NULL)
						{
							parentReplayMenu->winHide(TRUE);
						}
					}
				}
				break;
			}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			UnicodeString filename;
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

#if defined _DEBUG || defined _INTERNAL
			if( controlID == buttonAnalyzeReplay->winGetWindowId() )
			{
				if(listboxReplayFiles)
				{
					Int selected;
					GadgetListBoxGetSelected( listboxReplayFiles,  &selected );
					if(selected < 0)
					{
						MessageBoxOk(UnicodeString(u"Blah Blah"),UnicodeString(u"Please select something munkee boy"), NULL);
						break;
					}

					filename = GetReplayFilenameFromListbox(listboxReplayFiles, selected);

					AsciiString asciiFilename;
					asciiFilename.translate(filename);
					if (TheRecorder->analyzeReplay(asciiFilename))
					{
						do
						{
							TheRecorder->update();
						} while (TheRecorder->isAnalysisInProgress());
					}
				}
			}
			else 
#endif
			if( controlID == buttonLoadID )
			{
				if(listboxReplayFiles)
				{
					Int selected;
					GadgetListBoxGetSelected( listboxReplayFiles,  &selected );
					if(selected < 0)
					{
						MessageBoxOk(TheGameText->fetch("GUI:NoFileSelected"),TheGameText->fetch("GUI:PleaseSelectAFile"), NULL);
						break;
					}

					filename = GetReplayFilenameFromListbox(listboxReplayFiles, selected);

					AsciiString asciiFilename;
					asciiFilename.translate(filename);

					if(TheRecorder->testVersionPlayback(asciiFilename))
					{
						thePendingReplayFilename = asciiFilename;
						MessageBoxOkCancel(TheGameText->fetch("GUI:OlderReplayVersionTitle"), TheGameText->fetch("GUI:OlderReplayVersion"),reallyLoadReplay ,NULL);
					}
					else
					{
						TheRecorder->playbackFile(asciiFilename);

						if(parentReplayMenu != NULL)
						{
							parentReplayMenu->winHide(TRUE);
						}	
					}
					
				}				
			}  // end else if
			else if( controlID == buttonBackID )
			{

				// thou art directed to return to thy known solar system immediately!
				TheShell->pop();

			}  // end else if
			else if( controlID == buttonDeleteID )
			{
				Int selected;
				GadgetListBoxGetSelected( listboxReplayFiles,  &selected );
				if(selected < 0)
				{
					MessageBoxOk(TheGameText->fetch("GUI:NoFileSelected"),TheGameText->fetch("GUI:PleaseSelectAFile"), NULL);
					break;
				}
				filename = GetReplayFilenameFromListbox(listboxReplayFiles, selected);
				// a delete lights No, so Enter never deletes
				MessageBoxYesNo(TheGameText->fetch("GUI:DeleteFile"), TheGameText->fetch("GUI:AreYouSureDelete"), deleteReplayFlag, NULL, MSG_BOX_NO);
			}
			else if( controlID == buttonCopyID )
			{
				Int selected;
				GadgetListBoxGetSelected( listboxReplayFiles,  &selected );
				if(selected < 0)
				{
					MessageBoxOk(TheGameText->fetch("GUI:NoFileSelected"),TheGameText->fetch("GUI:PleaseSelectAFile"), NULL);
					break;
				}
				filename = GetReplayFilenameFromListbox(listboxReplayFiles, selected);
				MessageBoxYesNo(TheGameText->fetch("GUI:CopyReplay"), TheGameText->fetch("GUI:AreYouSureCopy"), copyReplayFlag, NULL);
			}
			break;
		}  // end selected

		default:
			return MSG_IGNORED;
	}  // end switch

	return MSG_HANDLED;
}  // end ReplayMenuSystem

void deleteReplay( void )
{
	callDelete = FALSE;
	Int selected;
	GadgetListBoxGetSelected( listboxReplayFiles,  &selected );
	if(selected < 0)
	{
		MessageBoxOk(TheGameText->fetch("GUI:NoFileSelected"),TheGameText->fetch("GUI:PleaseSelectAFile"), NULL);
		return;
	}
	AsciiString filename, translate;
	filename = TheRecorder->getReplayDir();
	translate.translate(GetReplayFilenameFromListbox(listboxReplayFiles, selected));
	filename.concat(translate);
	if(!TheLocalFileSystem->deleteFile(filename.str()))
	{
#if defined(_WIN32)
		char buffer[1024];
		FormatMessage ( FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, buffer, sizeof(buffer), NULL);
		translate.set(buffer);
#else
		translate.set(strerror(errno));		// ASCII in the "C" locale the game keeps
#endif
		UnicodeString errorStr;
		errorStr.translate(translate);
		MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, NULL);
	}
	//Load the listbox shiznit
	GadgetListBoxReset(listboxReplayFiles);
	PopulateReplayFileListbox(listboxReplayFiles);
}


void copyReplay( void )
{
	callCopy = FALSE;
	Int selected;
	GadgetListBoxGetSelected( listboxReplayFiles,  &selected );
	if(selected < 0)
	{
		MessageBoxOk(TheGameText->fetch("GUI:NoFileSelected"),TheGameText->fetch("GUI:PleaseSelectAFile"), NULL);
		return;
	}
	AsciiString filename, translate;
	filename = TheRecorder->getReplayDir();
	translate.translate(GetReplayFilenameFromListbox(listboxReplayFiles, selected));
	filename.concat(translate);
	
	// The "copy" button puts a copy of the selected replay on the player's Desktop.
	char path[1024];
#if defined(_WIN32)
	LPITEMIDLIST pidl;
	SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOPDIRECTORY, &pidl);
	SHGetPathFromIDList(pidl,path);
#else
	// ~/Desktop on macOS, xdg-user-dirs' Desktop on Linux (EarlyOptions.h, C1 (e))
	if (!findDesktopDirectory(path, sizeof(path)))
	{
		UnicodeString errorStr;
		errorStr.translate(AsciiString(strerror(ENOENT)));
		MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, NULL);
		return;
	}
#endif
	AsciiString newFilename;
	newFilename.set(path);
	newFilename.concat("\\");
	newFilename.concat(translate);
	if(!TheLocalFileSystem->copyFile(filename.str(),newFilename.str(), FALSE))
	{
#if defined(_WIN32)
		// Win32 only: WideChar and WCHAR are the same two bytes there, which is what makes the cast
		// honest.  The size is in characters, as FormatMessageW takes it; it used to be
		// sizeof(buffer), twice that.
		WideChar buffer[1024];
		FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, reinterpret_cast<LPWSTR>( buffer ),
			sizeof( buffer ) / sizeof( buffer[0] ), NULL );
		UnicodeString errorStr;
		errorStr.set(buffer);
#else
		UnicodeString errorStr;
		errorStr.translate(AsciiString(strerror(errno)));		// ASCII in the "C" locale the game keeps
#endif
		errorStr.trim();
		MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, NULL);
	}

}

