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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: ChallengeMenu.cpp
// Author: Steve Copeland, May 2003
// Description: General's Challenge Mode Menu
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
//#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/version.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameClient/SinglePlayerParts.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/WindowVideoManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"

#include <algorithm>
#include <vector>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

SkirmishGameInfo *TheChallengeGameInfo = NULL;


// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType buttonPlayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType bioPortraitID = NAMEKEY_INVALID;
static NameKeyType bioNameEntryID = NAMEKEY_INVALID;
static NameKeyType bioDOBEntryID = NAMEKEY_INVALID;
static NameKeyType bioBirthplaceEntryID = NAMEKEY_INVALID;
static NameKeyType bioStrategyEntryID = NAMEKEY_INVALID;
static NameKeyType buttonGeneralPositionID[NUM_GENERALS] = {NAMEKEY_INVALID};
static NameKeyType backdropID = NAMEKEY_INVALID;
static NameKeyType bioParentID = NAMEKEY_INVALID;

// window pointers --------------------------------------------------------------------------------
static GameWindow *parentMenu = NULL;
static GameWindow *buttonPlay = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *bioPortrait = NULL;
static GameWindow *bioLine1Entry = NULL;
static GameWindow *bioLine2Entry = NULL;
static GameWindow *bioLine3Entry = NULL;
static GameWindow *bioLine4Entry = NULL;
static GameWindow *buttonGeneralPosition[NUM_GENERALS] = {NULL};
static GameWindow *backdrop = NULL;
static GameWindow *bioParent = NULL;

//static NameKeyType testWinID = NAMEKEY_INVALID;
//static GameWindow *testWin = NULL;
static WindowVideoManager *wndVideoManager = NULL;

//
static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;
static Bool isShuttingDown = FALSE;

static Int lastButtonIndex = -1;
Int lastHilitedIndex = -1;
Bool isAutoSelecting = FALSE;
/// the general the lower half shows with nothing picked and no card under the pointer: the last one
/// previewed, the first card when the screen opens
static Int previewIndex = 0;

// audio
AudioHandle lastSelectionSound = NULL;
AudioHandle lastPreviewSound = NULL;
static Int introAudioMagicNumber = 0;
static Bool hasPlayedIntroAudio = FALSE;


//-------------------------------------------------------------------------------------------------
// returns the index of the General Position button selected, or -1 if not found
//-------------------------------------------------------------------------------------------------
Int findPositionButton( Int controlID )
{
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		if (controlID == buttonGeneralPositionID[i]) 
			return i;
	}
	return -1;
}


//-------------------------------------------------------------------------------------------------
// A general the loaded data cannot field: a ChallengeMode.ini that names a player template, a
// campaign, a map or an opponent its files lack, and the game found out only at the start, in a
// crash.  The data does not change while the game runs, so each is decided and logged once.
//-------------------------------------------------------------------------------------------------
static Bool canPlayGeneral( Int index )
{
	static Bool s_checked[NUM_GENERALS];
	static Bool s_playable[NUM_GENERALS];
	if (s_checked[index])
		return s_playable[index];
	s_checked[index] = TRUE;

	const GeneralPersona &general = TheChallengeGenerals->getChallengeGenerals()[index];
	const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(ThePlayerTemplateStore->getTemplateNumByName(general.getPlayerTemplateName()));
	Campaign *campaign = TheCampaignManager->findCampaign(general.getCampaign());
	AsciiString missing;
	if (playerTemplate == NULL || playerTemplate->getStartingBuilding().isEmpty())
		missing.format("player template %s", general.getPlayerTemplateName().str());
	else if (campaign == NULL || campaign->getNextMission(NULL) == NULL)
		missing.format("campaign %s", general.getCampaign().str());
	else
	{
		for (Campaign::MissionListIt it = campaign->m_missions.begin(); missing.isEmpty() && it != campaign->m_missions.end(); ++it)
		{
			if (!TheFileSystem->doesFileExist((*it)->m_mapName.str()))
				missing.format("map %s", (*it)->m_mapName.str());
			else if (TheChallengeGenerals->getGeneralByGeneralName((*it)->m_generalName) == NULL)
				missing.format("opponent %s", (*it)->m_generalName.str());
		}
	}
	if (missing.isNotEmpty())
		DEBUG_LOG(("ChallengeMenu: general %d is left out, the data has no %s\n", index, missing.str()));

	s_playable[index] = missing.isEmpty();
	return s_playable[index];
}

//-------------------------------------------------------------------------------------------------
// enable the appropriate buttons, make sure they aren't hidden, and set the correct images
//-------------------------------------------------------------------------------------------------
void setEnabledButtons()
{
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		const GeneralPersona* generals = TheChallengeGenerals->getChallengeGenerals();
		const Bool playable = generals[i].isStartingEnabled() && canPlayGeneral(i);
		buttonGeneralPosition[i]->winEnable(playable);
		buttonGeneralPosition[i]->winHide(!playable);
	}
}


//-------------------------------------------------------------------------------------------------
// sets the appropriate campaign for the chosen general
//-------------------------------------------------------------------------------------------------
void setGeneralCampaign( Int buttonIndex )
{
	if (buttonIndex < 0 || buttonIndex >= NUM_GENERALS)
		return;

	// determine which general and player template is selected and store it
	const GeneralPersona* generals = TheChallengeGenerals->getChallengeGenerals();
	TheCampaignManager->setCampaign(generals[buttonIndex].getCampaign());
	Int templateNum = ThePlayerTemplateStore->getTemplateNumByName(generals[buttonIndex].getPlayerTemplateName());
	TheChallengeGenerals->setCurrentPlayerTemplateNum(templateNum);

	// set up the skirmish games single player slot
	GameSlot slot;
	const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
	slot.setState(SLOT_PLAYER, playerTemplate->getDisplayName());
	slot.setPlayerTemplate(templateNum);
	TheChallengeGameInfo->setSlot(0, slot);
}


//-------------------------------------------------------------------------------------------------
// The screen in the menu language (docs/plans/MENU-REDESIGN.md, package 12): nine cards under their
// sides, the dossier of the general picked or under the pointer, the places map, the ladder of his
// challenge and the difficulty readout.  EA's bio windows and world map stay in the layout, hidden.
// SinglePlayerLayout.h holds every number at 1080.
//-------------------------------------------------------------------------------------------------
namespace SPL = SinglePlayerLayout;
namespace SPP = SinglePlayerParts;

static MenuBox::TextPool challengeText;

static const GeneralPersona *persona( Int index )
{
	return &TheChallengeGenerals->getChallengeGenerals()[ index ];
}

/// the general the lower half shows: the picked one, else the one under the pointer, else the last
static Int shownGeneral( void )
{
	if( lastButtonIndex >= 0 )
		return lastButtonIndex;
	if( lastHilitedIndex >= 0 )
		return lastHilitedIndex;
	return previewIndex;
}

/// a general's challenge: his opponents in the order he meets them, Leang last
static void ladderOf( Int index, std::vector< Int > &out )
{
	out.clear();
	Campaign *campaign = TheCampaignManager->findCampaign( persona( index )->getCampaign() );
	if( campaign == NULL )
		return;
	const GeneralPersona *first = TheChallengeGenerals->getChallengeGenerals();
	for( Campaign::MissionListIt it = campaign->m_missions.begin(); it != campaign->m_missions.end(); ++it )
	{
		const GeneralPersona *opponent = TheChallengeGenerals->getGeneralByGeneralName( (*it)->m_generalName );
		if( opponent )
			out.push_back( (Int)( opponent - first ) );
	}
}

static UnicodeString bioText( const AsciiString &key )
{
	return TheGameText->fetch( key );
}

/// a name's last word, for a tile too narrow for the whole
static UnicodeString surname( const UnicodeString &name )
{
	const WideChar *text = name.str();
	const WideChar *last = text;
	for( const WideChar *at = text; *at; at++ )
		if( *at == L' ' && at[ 1 ] )
			last = at + 1;
	return UnicodeString( last );
}

/// a card: plain, under the pointer (a white lift, the cyan edge) or picked (lit, his name gold)
static void challengeCardDraw( GameWindow *window, WinInstanceData * )
{
	const Int index = findPositionButton( window->winGetWindowId() );
	if( index < 0 )
		return;
	const IRegion2D r = MenuBox::windowRect( window );
	MenuBox::fill( r, GameMakeColor( 0, 0, 0, 110 ) );
	const Bool picked = index == lastButtonIndex;
	if( picked )
		SPP::picked( r );
	else if( index == lastHilitedIndex )
	{
		MenuBox::fill( r, GameMakeColor( 255, 255, 255, 22 ) );
		UIStyle::litEdge( r, UIStyle::EDGE_LIT );
	}
	else
		MenuBox::outline( r, UIStyle::FRAME );
	const Int inset = SPP::px( (Real)SPL::CARD_PORTRAIT_INSET );
	const Int side = r.hi.x - r.lo.x - 2 * inset;
	SPP::framed( persona( index )->getBioPortraitLarge(),
		MenuBox::region( r.lo.x + inset, r.lo.y + inset, r.lo.x + inset + side, r.lo.y + inset + side ) );
	const Int top = r.lo.y + inset + side + SPP::px( 8.0f );
	const Int middle = ( r.lo.x + r.hi.x ) / 2;
	const Int widest = r.hi.x - r.lo.x - SPP::px( 16.0f );
	SPP::words( challengeText, bioText( persona( index )->getBioName() ), UIStyle::MENU_TEXT_BODY, FALSE, middle,
		top + SPP::px( 13.0f ), picked ? UIStyle::GOLD : UIStyle::INK, widest, MenuBox::CENTRE, TRUE );
	SPP::capitalsKey( SPL::armyKey( index ), middle, top + SPP::px( 38.0f ), UIStyle::MUTED, MenuBox::CENTRE, widest );
}

/// the dossier: name, rank and branch, the portrait to the foot, tactics, station and his challenge
static void drawDossier( Int who, Bool mine, const std::vector< Int > &ladder )
{
	using namespace UIStyle;
	const GeneralPersona *general = persona( who );
	const IRegion2D name = SPP::on( SPL::DOSSIER_NAME );
	SPP::words( challengeText, bioText( general->getBioName() ), MENU_TEXT_TITLE, FALSE, name.lo.x,
		( name.lo.y + name.hi.y ) / 2, mine ? GOLD : INK, name.hi.x - name.lo.x, MenuBox::LEFT, TRUE );
	const IRegion2D rank = SPP::on( SPL::DOSSIER_RANK );
	UnicodeString line = bioText( general->getBioRank() );
	line.concat( u"  \x00b7  " );
	line.concat( bioText( general->getBioBranch() ) );
	SPP::words( challengeText, line, MENU_TEXT_BODY, FALSE, rank.lo.x, ( rank.lo.y + rank.hi.y ) / 2, MUTED,
		rank.hi.x - rank.lo.x, MenuBox::LEFT, TRUE );
	SPP::framed( general->getBioPortraitLarge(), SPP::on( SPL::DOSSIER_PORTRAIT ) );

	const IRegion2D facts = SPP::on( SPL::DOSSIER_FACTS );
	const Int x = facts.lo.x, widest = facts.hi.x - facts.lo.x;
	const Int pitch = SPP::px( 26.0f );
	Int y = facts.lo.y;
	const char *labels[ 2 ] = { "GUI:SPTactics", "GUI:SPStationed" };
	const AsciiString *values[ 2 ] = { &general->getBioStrategy(), &general->getBioBirthplace() };
	for( Int each = 0; each < 2; each++ )
	{
		SPP::capitalsKey( labels[ each ], x, y + SPP::px( 9.0f ), MUTED, MenuBox::LEFT, widest );
		y = SPP::wrapped( challengeText, bioText( *values[ each ] ), MENU_TEXT_BODY, x, y + SPP::px( 24.0f ), widest, 3, INK, pitch )
			+ SPP::px( 20.0f );
	}
	y = max( y + SPP::px( 8.0f ), SPP::point( 0.0f, (Real)SPL::DOSSIER_CHALLENGE_TOP ).y );
	SPP::section( challengeText, x, facts.hi.x, y, TheGameText->fetch( "GUI:SPTheChallenge" ) );
	y += SPP::px( 36.0f );
	Campaign *campaign = TheCampaignManager->findCampaign( general->getCampaign() );
	if( campaign )
		y = SPP::wrapped( challengeText, TheGameText->fetch( campaign->m_campaignNameLabel ), MENU_TEXT_BODY, x, y, widest, 2,
			INK, pitch );
	if( !ladder.empty() )
	{
		UnicodeString battles;
		battles.format( TheGameText->fetch( "GUI:SPBattles" ), (Int)ladder.size(), bioText( persona( ladder.back() )->getBioName() ).str() );
		SPP::wrapped( challengeText, battles, MENU_TEXT_BODY, x, y, widest, 2, MUTED, pitch );
	}
}

/// the places: his station a gold ring (white while only under the pointer), each opponent his
/// battle's number at his station, rings spread apart with leader lines, the generals stationed
/// nowhere in a box of their own, and a legend
static void drawPlaces( Int who, Bool mine, const std::vector< Int > &ladder )
{
	using namespace UIStyle;
	const IRegion2D r = SPP::on( SPL::PLACES );
	const Image *map = TheMappedImageCollection->findImageByName( "SPPlacesMap" );
	MenuBox::fill( r, GameMakeColor( 6, 10, 20, 150 ) );
	if( map )
		TheDisplay->drawImage( map, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
	MenuBox::outline( MenuBox::region( r.lo.x - 1, r.lo.y - 1, r.hi.x + 1, r.hi.y + 1 ), FRAME );
	const Color you = mine ? GOLD : EDGE_LIT;

	std::vector< Int > members( 1, who ), placed, unknown;
	members.insert( members.end(), ladder.begin(), ladder.end() );
	std::vector< Real > ax, ay;
	for( size_t each = 0; each < members.size(); each++ )
	{
		Real lon, lat, x, y;
		if( SPL::station( members[ each ], lon, lat ) )
		{
			SPL::placesPoint( lon, lat, x, y );
			placed.push_back( members[ each ] );
			ax.push_back( x );
			ay.push_back( y );
		}
		else
			unknown.push_back( members[ each ] );
	}
	std::vector< Real > px = ax, py = ay;
	SPL::spreadRings( px, py, SPL::PLACES );
	const Int radius = SPP::px( (Real)SPL::RING_RADIUS );
	for( size_t k = 0; k < placed.size(); k++ )
	{
		const ICoord2D a = SPP::point( ax[ k ], ay[ k ] ), p = SPP::point( px[ k ], py[ k ] );
		if( ( a.x - p.x ) * ( a.x - p.x ) + ( a.y - p.y ) * ( a.y - p.y ) > 9 )
			TheDisplay->drawLine( a.x, a.y, p.x, p.y, 1.0f, withAlpha( INK, 200 ) );
		SPP::disc( a.x, a.y, max( 2, SPP::px( 2.5f ) ), placed[ k ] == who ? you : INK );
	}
	for( size_t k = 0; k < placed.size(); k++ )
	{
		const ICoord2D p = SPP::point( px[ k ], py[ k ] );
		if( placed[ k ] == who )
			SPP::ring( challengeText, p.x, p.y, radius + 1, you, 0, 3 );
		else
			SPP::ring( challengeText, p.x, p.y, radius, INK, (Int)( std::find( ladder.begin(), ladder.end(), placed[ k ] ) - ladder.begin() ) + 1 );
	}
	if( !unknown.empty() )
	{
		const IRegion2D box = SPP::on( SPL::unknownBox( (Int)unknown.size() ) );
		MenuBox::fill( box, GameMakeColor( 4, 6, 12, 225 ) );
		MenuBox::outline( box, withAlpha( FRAME, 230 ) );
		SPP::capitalsKey( "GUI:SPLocationUnknown", box.lo.x + SPP::px( 12.0f ), box.lo.y + SPP::px( 16.0f ), MUTED, MenuBox::LEFT,
			box.hi.x - box.lo.x - SPP::px( 20.0f ) );
		for( size_t k = 0; k < unknown.size(); k++ )
		{
			const Int cy = box.lo.y + SPP::px( 46.0f + 36.0f * k );
			const Int cx = box.lo.x + SPP::px( 26.0f );
			if( unknown[ k ] == who )
				SPP::ring( challengeText, cx, cy, radius, you, 0, 3 );
			else
				SPP::ring( challengeText, cx, cy, radius, INK, (Int)( std::find( ladder.begin(), ladder.end(), unknown[ k ] ) - ladder.begin() ) + 1 );
			SPP::words( challengeText, bioText( persona( unknown[ k ] )->getBioName() ), MENU_TEXT_BODY, FALSE,
				box.lo.x + SPP::px( 48.0f ), cy, INK, box.hi.x - box.lo.x - SPP::px( 58.0f ), MenuBox::LEFT, TRUE );
		}
	}
	const IRegion2D legend = SPP::on( SPL::PLACES_LEGEND );
	MenuBox::fill( legend, GameMakeColor( 4, 6, 12, 225 ) );
	MenuBox::outline( legend, withAlpha( FRAME, 230 ) );
	const Int cy = ( legend.lo.y + legend.hi.y ) / 2;
	SPP::ring( challengeText, legend.lo.x + SPP::px( 22.0f ), cy, SPP::px( 10.0f ), you, 0, 3 );
	const Int w = SPP::wordsKey( challengeText, mine ? "GUI:SPYourGeneral" : "GUI:SPThisGeneral", MENU_TEXT_BODY, FALSE,
		legend.lo.x + SPP::px( 38.0f ), cy, INK, SPP::px( 152.0f ) );
	const Int rx = legend.lo.x + SPP::px( 38.0f + 24.0f ) + w;
	SPP::ring( challengeText, rx + SPP::px( 12.0f ), cy, radius, INK, 1 );
	SPP::wordsKey( challengeText, "GUI:SPBattleInOrder", MENU_TEXT_BODY, FALSE, rx + SPP::px( 34.0f ), cy, INK,
		legend.hi.x - rx - SPP::px( 42.0f ) );
}

/// the ladder, one tile a battle: the opponent's small portrait, the battle's number, his name and
/// his army, FINAL BATTLE on the last
static void drawLadder( const std::vector< Int > &ladder )
{
	using namespace UIStyle;
	const Int count = (Int)ladder.size();
	const Bool surnames = SPL::ladderTileWidth( count ) < SPL::LADDER_SURNAMES_UNDER;
	for( Int each = 0; each < count; each++ )
	{
		const Int who = ladder[ each ];
		const IRegion2D tile = SPP::on( SPL::ladderTile( count, each ) );
		MenuBox::fill( tile, GameMakeColor( 0, 0, 0, 110 ) );
		MenuBox::outline( tile, withAlpha( FRAME, 230 ) );
		const Int in = SPP::px( 8.0f ), side = SPP::px( (Real)SPL::LADDER_PORTRAIT );
		SPP::framed( persona( who )->getBioPortraitSmall(), MenuBox::region( tile.lo.x + in, tile.lo.y + in, tile.lo.x + in + side,
			tile.lo.y + in + side ) );
		UnicodeString number;
		number.format( u"%d", each + 1 );
		SPP::words( challengeText, number, MENU_TEXT_ROW_FIGURE, FALSE, tile.lo.x + in + side + SPP::px( 6.0f ),
			tile.lo.y + SPP::px( 23.0f ), INK, 0 );
		const Int widest = tile.hi.x - tile.lo.x - in - SPP::px( 6.0f );
		const UnicodeString name = bioText( persona( who )->getBioName() );
		SPP::words( challengeText, surnames ? surname( name ) : name, MENU_TEXT_BODY, FALSE, tile.lo.x + in,
			tile.lo.y + SPP::px( 8.0f + 84.0f + 21.0f ), INK, widest, MenuBox::LEFT, TRUE );
		SPP::capitalsKey( SPL::armyKey( who ), tile.lo.x + in, tile.lo.y + SPP::px( 8.0f + 84.0f + 46.0f ),
			who == SPL::LEANG ? INK : MUTED, MenuBox::LEFT, widest );
	}
}

/// DIFFICULTY and the main menu's pick, read only, after BACK
static void drawDifficulty( void )
{
	using namespace UIStyle;
	const IRegion2D keys = SPP::on( SPL::key( SPL::DIFFICULTY_LEFT, SPL::CHALLENGE_PLAY.lo.x - 30 ) );
	const Int middle = ( keys.lo.y + keys.hi.y ) / 2;
	const Int w = SPP::capitalsKey( "GUI:SPDifficulty", keys.lo.x, middle, MUTED );
	const GameDifficulty difficulty = TheChallengeGenerals->getCurrentDifficulty();
	const char *key = difficulty == DIFFICULTY_EASY ? "GUI:Easy" : difficulty == DIFFICULTY_HARD ? "GUI:Hard" : "GUI:Medium";
	const Int x = keys.lo.x + w + SPP::px( 14.0f );
	const Int vw = SPP::words( challengeText, TheGameText->fetch( key ), MENU_TEXT_KEY, TRUE, x, middle, INK, 0 );
	SPP::wordsKey( challengeText, "GUI:SPPickedOnMenu", MENU_TEXT_BODY, FALSE, x + vw + SPP::px( 16.0f ), middle, MUTED,
		keys.hi.x - x - vw - SPP::px( 16.0f ) );
}

/// the screen: frame A over the shell map, the title row, the side heads; the cards draw themselves
static void challengeDraw( GameWindow *, WinInstanceData * )
{
	using namespace UIStyle;
	challengeText.begin();
	const IRegion2D panel = boardRect( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM );
	glass( panel, 168 );
	frameA( panel );
	menuHeader( challengeText.get( capitals( TheGameText->fetch( "GUI:SPChallengeTitle" ) ) ),
		challengeText.get( TheGameText->fetch( "GUI:SPChooseGeneral" ) ) );
	static const char *heads[ SPL::SIDES ] = { "GUI:SPSideUSA", "GUI:SPSideChina", "GUI:SPSideGLA" };
	for( Int side = 0; side < SPL::SIDES; side++ )
	{
		const IRegion2D head = SPP::on( SPL::sideHead( side ) );
		SPP::capitalsKey( heads[ side ], head.lo.x, head.lo.y + SPP::px( 9.0f ), ACTIVE, MenuBox::LEFT, head.hi.x - head.lo.x );
		MenuBox::rule( head.lo.x, head.hi.x, head.lo.y + SPP::px( 24.0f ), 150 );
	}
	if( TheChallengeGenerals == NULL || TheCampaignManager == NULL )
		return;
	const Int who = shownGeneral();
	std::vector< Int > ladder;
	ladderOf( who, ladder );
	drawDossier( who, lastButtonIndex >= 0, ladder );
	drawPlaces( who, lastButtonIndex >= 0, ladder );
	drawLadder( ladder );
	drawDifficulty();
	const IRegion2D foot = SPP::on( SPL::rect( SPL::LEFT, SPL::FOOT_RULE, SPL::RIGHT, SPL::FOOT_RULE + 1 ) );
	MenuBox::rule( foot.lo.x, foot.hi.x, foot.lo.y, 110 );
}

static void challengeNothingDraw( GameWindow *, WinInstanceData * )
{
}

/// the general the lower half previews while nobody is picked
void setGeneralBio( Int buttonIndex )
{
	if (buttonIndex < 0 || buttonIndex >= NUM_GENERALS)
		return;
	previewIndex = buttonIndex;
}


//-------------------------------------------------------------------------------------------------
/** init the challenge mode menu */
//-------------------------------------------------------------------------------------------------
void ChallengeMenuInit( WindowLayout *layout, void *userData )
{
	if( !TheChallengeGameInfo )
		TheChallengeGameInfo = NEW SkirmishGameInfo;

	TheChallengeGameInfo->init();  
	TheChallengeGameInfo->clearSlotList();
	TheChallengeGameInfo->reset();
	TheChallengeGameInfo->enterGame();


	TheShell->showShellMap(TRUE);

	// init window ids and pointers
	parentID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:ParentChallengeMenu") );
	parentMenu = TheWindowManager->winGetWindowFromId( NULL, parentID );
	buttonPlayID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:ButtonPlay") );
	buttonPlay = TheWindowManager->winGetWindowFromId( parentMenu, buttonPlayID );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:ButtonBack") );
	buttonBack = TheWindowManager->winGetWindowFromId( parentMenu, buttonBackID );
	bioPortraitID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:BioPortrait") );
	bioPortrait = TheWindowManager->winGetWindowFromId( parentMenu, bioPortraitID );
	bioNameEntryID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:BioNameEntry") );
	bioLine1Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioNameEntryID ); // this window has been repurposed
	bioDOBEntryID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:BioDOBEntry") );
	bioLine2Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioDOBEntryID ); // this window has been repurposed
	bioBirthplaceEntryID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:BioBirthplaceEntry") );
	bioLine3Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioBirthplaceEntryID ); // this window has been repurposed
	bioStrategyEntryID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:BioStrategyEntry") );
	bioLine4Entry = TheWindowManager->winGetWindowFromId( parentMenu, bioStrategyEntryID ); // this window has been repurposed
	backdropID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:MainBackdrop") );
	backdrop = TheWindowManager->winGetWindowFromId( parentMenu, backdropID);
	bioParentID = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeMenu.wnd:GeneralsBioParent") );
	bioParent = TheWindowManager->winGetWindowFromId( parentMenu, bioParentID);

	AsciiString strButtonName;
	for (Int i = 0; i < NUM_GENERALS; i++)
	{
		strButtonName.format("ChallengeMenu.wnd:GeneralPosition%d", i);
		buttonGeneralPositionID[i] = TheNameKeyGenerator->nameToKey( strButtonName );
		buttonGeneralPosition[i] = TheWindowManager->winGetWindowFromId( parentMenu, buttonGeneralPositionID[i] );
		DEBUG_ASSERTCRASH(buttonGeneralPosition[i], ("Could not find the ButtonGeneralPosition[%d]",i ));

		// start all buttons hidden, then expose them later if there is a general for this spot
		buttonGeneralPosition[i]->winHide( TRUE );
	}

	// set defaults
	bioParent->winHide(TRUE);
	isAutoSelecting = FALSE;
	setEnabledButtons();

	// the menu language: the screen draws itself on the parent, each card on its own window; PLAY
	// GAME stands dimmed until a general is picked, and the first card is previewed
	parentMenu->winSetDrawFunc( challengeDraw );
	GameWindow *gadgets = TheWindowManager->winGetWindowFromId( parentMenu, TheNameKeyGenerator->nameToKey( "ChallengeMenu.wnd:GadgetParent" ) );
	if( gadgets )
		gadgets->winSetDrawFunc( challengeNothingDraw );
	previewIndex = -1;
	lastHilitedIndex = -1;
	for( Int side = 0; side < SinglePlayerLayout::SIDES; side++ )
		for( Int slot = 0; slot < SinglePlayerLayout::CARDS_PER_SIDE; slot++ )
		{
			const Int index = SinglePlayerLayout::sidePersona( side, slot );
			buttonGeneralPosition[index]->winSetDrawFunc( challengeCardDraw );
			buttonGeneralPosition[index]->winSetTooltip( UnicodeString::TheEmptyString );
			if( previewIndex < 0 && !buttonGeneralPosition[index]->winIsHidden() )
				previewIndex = index;
		}
	if( previewIndex < 0 )
		previewIndex = 0;
	SinglePlayerParts::key( buttonBack, "GUI:Back", FALSE );
	SinglePlayerParts::key( buttonPlay, "GUI:Start", TRUE );
	buttonPlay->winHide( FALSE );
	buttonPlay->winEnable( lastButtonIndex >= 0 );

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentMenu );
	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *winGadgetParent = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ChallengeMenu.wnd:GadgetParent"));
	if(winGadgetParent)
		winGadgetParent->winHide(TRUE);
	isShuttingDown = FALSE;

	if(!wndVideoManager)
		wndVideoManager = NEW WindowVideoManager;
	wndVideoManager->init();

	lastSelectionSound = NULL;
	lastPreviewSound = NULL;
	hasPlayedIntroAudio = FALSE;

}


//-------------------------------------------------------------------------------------------------
/** update the challenge mode menu */
//-------------------------------------------------------------------------------------------------
void ChallengeMenuUpdate( WindowLayout *layout, void *userData )
{
	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->setGroup("ChallengeMenuFade");
//			TheTransitionHandler->setGroup("ChallengeButtonsIntro");


			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

//	if (TheTransitionHandler->isFinished())
//		updateButtonSequence( 1 );

	// delay the voice for N updates after the transition is done
	if (!hasPlayedIntroAudio && TheTransitionHandler->isFinished())
	{
		introAudioMagicNumber++;
		if (introAudioMagicNumber == 10)
		{
			// "Choose your general."
			AudioEventRTS event( "Taunts_GCAnnouncer01" );
			TheAudio->addAudioEvent( &event );
			hasPlayedIntroAudio = TRUE;
		}
	}

	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		TheShell->shutdownComplete( layout );
	}

	if(wndVideoManager)
		wndVideoManager->update();
}


//-------------------------------------------------------------------------------------------------
/** shutdown the challenge mode menu */
//-------------------------------------------------------------------------------------------------
void ChallengeMenuShutdown( WindowLayout *layout, void *userData )
{
	if(wndVideoManager)
		delete wndVideoManager;
	wndVideoManager = NULL;

	lastButtonIndex = -1;
	lastHilitedIndex = -1;
	challengeText.clear();

	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{
		layout->hide( TRUE );
		TheShell->shutdownComplete( layout );
		return;
	}

	TheTransitionHandler->reverse("ChallengeMenuFade");
	isShuttingDown = TRUE;

	if(TheChallengeGameInfo)
		delete TheChallengeGameInfo;
	TheChallengeGameInfo = NULL;

	TheAudio->removeAudioEvent( lastSelectionSound );
	TheAudio->removeAudioEvent( lastPreviewSound );
	lastSelectionSound = NULL;
	lastPreviewSound = NULL;
	introAudioMagicNumber = 0;
}


//-------------------------------------------------------------------------------------------------
/** challenge mode menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ChallengeMenuInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
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

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, (WindowMsgData)buttonBack, buttonBackID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}


//-------------------------------------------------------------------------------------------------
/** challenge mode menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ChallengeMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{
		case GWM_CREATE: break;

		case GWM_DESTROY: break;

		case GWM_INPUT_FOCUS:
		{
			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;
		}

		case GBM_MOUSE_ENTERING:
		{
			// preview the bio for this position
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			Int buttonIndex = findPositionButton(controlID);
			if( buttonIndex != -1 )
			{
				// the card lifts; with nobody picked the lower half previews him
				lastHilitedIndex = buttonIndex;
				setGeneralBio(buttonIndex);
				if( buttonIndex != lastButtonIndex )
				{
					// special sound for Harvard
					AudioEventRTS event( "GUILogoMouseOver" );
					TheAudio->addAudioEvent( &event );
				}
			}

			break;
		}

		case GBM_MOUSE_LEAVING:
		{
			// the lower half keeps the general last previewed, or the one picked
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			Int buttonIndex = findPositionButton(controlID);
			if( buttonIndex != -1 && buttonIndex == lastHilitedIndex )
				lastHilitedIndex = -1;
			break;
		}

		case GBM_SELECTED:
		{
			UnicodeString filename;
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

 			// we don't need to handle these
 			if ( isAutoSelecting )
 			{
 				isAutoSelecting = FALSE;
 				break;
 			}

			Int buttonIndex = findPositionButton(controlID);
			if( buttonIndex != -1)
			{
				// partial radio button behavior (I'm not using actual radio buttons
				// because it limits options on how the interface can work, which is a
				// bad thing when the design is constantly in flux.)
				// ...basically this just makes you have exactly one button selected
				// once the first choice has been made.
				if (lastButtonIndex != -1)
				{
					isAutoSelecting = TRUE;
					GameWindow *lastControl = TheWindowManager->winGetWindowFromId( NULL, buttonGeneralPositionID[lastButtonIndex]);
					GadgetCheckBoxToggle(lastControl);
				}

				// play audio to indicate selection
				TheAudio->removeAudioEvent(lastSelectionSound);
				TheAudio->removeAudioEvent(lastPreviewSound);
				const GeneralPersona* generals = TheChallengeGenerals->getChallengeGenerals();
				AudioEventRTS event( generals[buttonIndex].getPreviewSound() );
				lastPreviewSound = TheAudio->addAudioEvent( &event );

/*
				// play audio to indicate selection
				TheAudio->removeAudioEvent(lastSelectionSound);
				TheAudio->removeAudioEvent(lastPreviewSound);
				const GeneralPersona* generals = TheChallengeGenerals->getChallengeGenerals();
				AudioEventRTS event( generals[buttonIndex].getSelectionSound() );
				lastSelectionSound = TheAudio->addAudioEvent( &event );
*/

				lastButtonIndex = buttonIndex;

				buttonPlay->winEnable(TRUE);

				//@todo: outro transitions
			}
			else if( controlID == buttonPlayID )
 			{
				if( TheChallengeGameInfo == NULL )
				{
					// If this is NULL, then we must be on the way back out of this menu.  
					// Don't crash, just eat the button click message.
					return MSG_HANDLED;
				}

				setGeneralCampaign(lastButtonIndex);
				TheWritableGlobalData->m_pendingFile = TheCampaignManager->getCurrentMap();
				TheChallengeGameInfo->setMap(TheCampaignManager->getCurrentMap());

				// turn off the last button so the screen will be pristine when the user returns
				isAutoSelecting = TRUE;
				GameWindow *lastControl = TheWindowManager->winGetWindowFromId( NULL, buttonGeneralPositionID[lastButtonIndex]);
				GadgetCheckBoxSetChecked(lastControl, FALSE);
				lastButtonIndex = -1;
				buttonPlay->winEnable(FALSE);
//				introAudioHasPlayed = FALSE;

				if (TheGameLogic->isInGame())
					TheGameLogic->clearGameData();

				// If the campaign has been reset, so has the campaign difficulty.  Restore it, just in case.
				DEBUG_ASSERTCRASH(TheChallengeGenerals, ("TheChallengeGenerals are not initialized."));
				if (TheChallengeGenerals) 
				{
	        TheCampaignManager->setGameDifficulty(TheChallengeGenerals->getCurrentDifficulty());
					TheScriptEngine->setGlobalDifficulty(TheChallengeGenerals->getCurrentDifficulty());
				}

				// put a request in the message stream for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
				msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	
        
        // Added so that, even though a ChallengeGame is really a SkirmishGame in SinglePlayerGame's clothing,
        // GameEngine will still apply the default "FRAME CAP" as it does during "Solo Missions."
        GameEngine_appendNewGameRates( msg, TheGlobalData->m_missionGameSpeedPercent );	// FPS limit and the missions' game speed
				
				InitRandom(0);
			}
			else if( controlID == buttonBackID )
			{
				TheShell->pop();
			}
			break;
		}

		default: return MSG_IGNORED;
	}

	return MSG_HANDLED;
}