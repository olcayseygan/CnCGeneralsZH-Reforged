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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GUIUtil.h //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#pragma once

#ifndef __GUIUTIL_H__
#define __GUIUTIL_H__

#include "Common/NameKeyGenerator.h"		// NameKeyType, which LobbyTabClicked takes
#include "GameNetwork/NetworkDefs.h"		// MAX_SLOTS, which the room plan holds

class GameWindow;
class GameInfo;
class GameSlot;
class MapMetaData;

// Hides the named windows of a room while a popup (a map list) lies over them; showing brings back
// only the ones the last hide took down, so a box hidden for its own reason stays hidden.
void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide );

// Every seat a lobby can offer - Open, Closed and the six rungs of the AI ladder - in one list,
// each entry tagged with the SlotState it stands for.  allowTakeover adds the empty seat you
// take over in game, which only the skirmish lobby offers.  allowComputers FALSE leaves the ladder out
// (an Apocalypse room: Open, Closed and players only)
void PopulatePlayerSlotComboBox(GameWindow *comboBox, Int color, Bool allowTakeover, Bool allowComputers = TRUE);
void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
// directStrike leaves out the sides that mode does not offer (Turkiye)
void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers, Bool directStrike = FALSE );
void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame);

// The peace time rungs both network lobbies offer, from one list so they cannot drift apart.
// UpdatePeaceTimeComboBox puts the box back on whatever the host last sent and greys it out when
// nobody may touch it - a client, or any lobby with a computer player in it, which has no peace
// time.  PeaceTimeFromComboBox reads the minutes back out of the entry the user picked.
void PopulatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdatePeaceTimeComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int PeaceTimeFromComboBox(GameWindow *comboBox);

// The superweapon rule, which the host picks and which travels in the options string as SR.  EA
// shipped it as one number for everybody, the cap on how many of each superweapon a player may have
// standing; it is a mode now, because one number cannot be fair to the USA Superweapon General,
// whose three superweapons are what he pays for a weaker everything else with.  SuperweaponBuildCap
// in Player.h turns a mode into that player's cap.
enum
{
	SUPERWEAPONS_ALLOW = 0,
	SUPERWEAPONS_LIMIT = 1,
	SUPERWEAPONS_NONE  = 2
};

// What each mode leaves a player, per superweapon type.  Allow leaves no cap at all, and under No
// everybody but the Superweapon General is barred outright.
enum
{
	SUPERWEAPONS_LIMIT_GENERAL = 4,
	SUPERWEAPONS_LIMIT_OTHERS  = 1,
	SUPERWEAPONS_NONE_GENERAL  = 1
};

void PopulateSuperweaponComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateSuperweaponComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int SuperweaponRestrictionFromComboBox(GameWindow *comboBox);
// The dropdown's string label for a mode, the first entry's for a mode it does not have.  The load
// board's facts box words the rule with it.
const char *SuperweaponCaption(Int restriction);

// The unit limit check box, which the host ticks and which travels in the options string as UL.
// UpdateUnitLimitCheckBox puts the box on what the game says and greys it out for anyone who may not
// touch it, and it only sets the box when the box is wrong.
void UpdateUnitLimitCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit);
// The Pro Rules check box, the same thing for PR.
void UpdateProRulesCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit);
// The day and night box, the same thing for DN.
void UpdateDayNightCheckBox(GameWindow *checkBox, GameInfo *myGame, Bool hostMayEdit);

// The income sharing dropdown, one entry per IncomeSharing in GameInfo.h, travelling as IS.
void PopulateIncomeSharingComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateIncomeSharingComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int IncomeSharingFromComboBox(GameWindow *comboBox);
// The dropdown's string label for an IncomeSharing, Off's for one out of range.
const char *IncomeSharingCaption(Int incomeSharing);

// The tech building respawn dropdown, minutes in each entry's item data, travelling as TR.
void PopulateTechRespawnComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateTechRespawnComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int TechRespawnFromComboBox(GameWindow *comboBox);

// The supply pile limit dropdown, players in each entry's item data, travelling as PL.
void PopulateSupplyPileLimitComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateSupplyPileLimitComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int SupplyPileLimitFromComboBox(GameWindow *comboBox);

// The LAN and custom online rooms' game speed dropdown, a percent of retail in each entry's item data,
// travelling as SP when it is not the room kind's own; no selection reads as that default.
void PopulateGameSpeedComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
void UpdateGameSpeedComboBox(GameWindow *comboBox, GameInfo *myGame, Bool hostMayEdit);
Int GameSpeedFromComboBox(GameWindow *comboBox, GameInfo *myGame);
UnicodeString GameSpeedText(Int percent);	///< 1.0x, 1.25x, 1.5x, 2.0x
// Quick Match's and Play vs AI's preferred speed: the same list, the player's own pick
void PopulatePlayerGameSpeedComboBox(GameWindow *comboBox, Int percent);
Int PlayerGameSpeedFromComboBox(GameWindow *comboBox, Int fallback);

// The lobby's own tab strip: one page of host settings, and the window that page covers - the chat
// log in the two network lobbies, the map info list in the skirmish one.  All three screens share
// these because only one lobby is ever up, and because a tab strip written three times drifts.
// InitLobbyTabs finds the windows and opens on the chat log; LobbyTabClicked answers TRUE when the
// click was one of the two tabs and it has already switched pages.
void InitLobbyTabs(GameWindow *parent, const char *layoutFilename,
									 const char *otherTabName, const char *otherWindowName);
Bool LobbyTabClicked(NameKeyType controlID);
void ShutdownLobbyTabs(void);

// The personality column: one combo box a seat, one entry per personality aiPersonalityOffered keeps,
// in enum order with the value as item data, shown only on a computer player's seat and changed only by the host; on any
// other seat UpdateAIPersonaComboBoxes widens the player box over the empty column.  The
// layout in a stale Run/Window may not have the column, so a box that is not there stays NULL and
// its seat plays Normal.  HandleAIPersonaSelection answers TRUE when the click was one of the boxes
// and changed its slot, so a network lobby knows to send the options round.
void InitAIPersonaComboBoxes(GameWindow *parent, const char *layoutFilename,
														 GameWindow *comboPersona[], NameKeyType comboPersonaID[]);
void UpdateAIPersonaComboBoxes(GameInfo *myGame, GameWindow *comboPersona[]);
Bool HandleAIPersonaSelection(GameInfo *myGame, GameWindow *comboPersona[], NameKeyType comboPersonaID[],
															NameKeyType controlID);

// The handicap column: one combo box a seat, 100 down to 10 percent with the value as item data.
// Whoever may change a seat's team may change its handicap, so UpdateHandicapComboBoxes, called after
// UpdateSlotList, copies the team box's enabled state.  HandleHandicapSelection answers the new
// percent when the click was one of the boxes and is a change, 0 otherwise.  The host's pick goes into
// the slot, unless the options string has no room for it; a guest's waits for the host's slot list.
void InitHandicapComboBoxes(GameWindow *parent, const char *layoutFilename,
														GameWindow *comboHandicap[], NameKeyType comboHandicapID[]);
void UpdateHandicapComboBoxes(GameInfo *myGame, GameWindow *comboHandicap[], GameWindow *comboTeam[]);
Int HandleHandicapSelection(GameInfo *myGame, GameWindow *comboHandicap[], NameKeyType comboHandicapID[],
														NameKeyType controlID);

// What a seat row shows beside its combo boxes: a bar and a swatch in the seat's colour, a swatch in
// the team's colour (both framed like the check boxes), and in the network rooms a green card for
// a player who is ready and a gold edge round the host's.  All of it is drawn from the slot on every
// update and none of it takes input.  A layout without the windows (a stale Run/Window) leaves them
// NULL and the room looks as it did.
void InitLobbySeatVisuals(GameWindow *parent, const char *layoutFilename);
// Every combo box on the room's rules page opens a list that ends on the foot of a rules row below
// it, the rows' pitch read from the boxes themselves; none shows a box cut in half under it.
void SnapLobbyRulesLists(GameWindow *parent, const char *layoutFilename);
void UpdateLobbySeatVisuals(GameInfo *myGame);
void ShutdownLobbySeatVisuals(void);
// Which of the layout's seat rows shows each seat, -1 for a seat on no row. Every room's rows are its
// seats in order (the init calls above set that); the skirmish room scrolls more seats than it has
// rows over them and says here which row now shows which seat, for the card, bar and swatch windows
// and the personality column's widths. seatRow has MAX_SLOTS entries.
void SetLobbySeatRows(const Int seatRow[]);
// Team 0 to 3 (shown as 1 to 4) each have a colour none of the players can pick; no team is the
// empty seat's slate.  The map's start spots wear
// them too.
Color LobbyTeamColor(Int team);
Color LobbySeatColor(const GameSlot *slot);
// Black or white, whichever has the higher WCAG contrast on this fill.
Color LobbyTextColorOn(Color fill);
// A taken start spot over EA's PlayerStart drawn in this rectangle: its middle in the colour and its
// ring as EA drew it.  The room's spot buttons and the load screen's map both draw it.
void LobbyStartSpotFill(Int x, Int y, Int width, Int height, Color fill);
// A seat's colour or team swatch as the room draws it: the rules page's empty check box with the
// colour inside its hole.  The score screen's team bands draw it too.
void LobbySwatchFill(Int x, Int y, Int width, Int height, Color fill);
// The map pickers' Official / Unofficial radio buttons drawn as UIStyle tabs: the open one's label
// ACTIVE, a disabled one's MUTED.  The pickers' init sets it as both buttons' draw function.
void DrawMapListTab(GameWindow *window, class WinInstanceData *instData);
// The pickers' third tab, Random: a map type and a size.  The preview shows the shipped map the type
// names, as the Official tab would; the generated map is made on Accept, with a fresh seed, for the
// players seated then.  The picker's init calls RandomMapTabInit after filling its list (filling it
// moves the preview), its system callback offers every message to RandomMapTabHandle first, and
// Accept takes RandomMapTabMap(), which stages that map, whenever that is not empty.  allowed FALSE greys the tab out (a ranked
// online room plays official maps only).  A room seating more than SHIPPED_MAP_MAX_PLAYERS on a
// generated map opens on the tab; its picker lists only the maps with a start spot for every seat.
// RandomMapTabShutdown forgets the windows.
void RandomMapTabInit(const char *layoutFilename, GameWindow *parent, GameWindow *preview,
										GameWindow *startSpots[], const GameInfo *game, Bool allowed);
Bool RandomMapTabHandle(UnsignedInt msg, UnsignedIntPtr mData1, UnsignedIntPtr mData2);
AsciiString RandomMapTabMap(void);
void RandomMapTabShutdown(void);
// A generated map is for the players in the room, humans and AI, observers left out.  The host's
// room calls this every frame and again on Start: when the seat count no longer matches the map,
// the same seed, template and size are staged for the new count and become the room's map, CRC,
// size and start spots, so joiners rebuild it from the name.  TRUE when the map changed; the room
// then repaints and sends its options.
Bool FitRandomMapToRoom(GameInfo *game);
/// The most start positions a shipped or user map has: WorldBuilder places eight.
enum { SHIPPED_MAP_MAX_PLAYERS = 8 };
// A room seating more than eight plays a map with a start spot for every seat: a custom map with
// that many, or a generated one. When its map has too few, the Random tab's remembered choice
// (RandomMap.ini) is staged for them and becomes the room's map, as FitRandomMapToRoom does; TRUE
// when the map changed. Only the skirmish room seats that many. whenTooSmall does the same at eight
// or fewer (a preset's eight on a four-player map); a map that holds them is kept.
Bool GenerateMapForCrowdedRoom(GameInfo *game, Bool whenTooSmall = FALSE);

// Direct Strike, the Direct Strike director's mode, in a staging room.  The mode is the map: a room whose
// map is a generated lane map ("-randommap <seed> lane", RMG_v<n>_<seed>_6p_..._lane) is a Direct
// Strike room on every machine, so nothing new travels in the options string and a joiner, a replay
// and the game list all read the mode off the map's name.  There is no map choice: the host stages a
// fresh lane map from a new seed (SetDirectStrikeMap), the skirmish room on Start and a network room
// when it opens, and the seed reaches the other seats in the name.
Bool IsDirectStrikeMap(const AsciiString &map);
// a lane map from a seed off the clock, staged, in the map cache and the game's, CRC and size with it
Bool SetDirectStrikeMap(GameInfo *game);
// The seats under the mode's rules, for a host to call before it shows or sends the slot list.  The
// seat is the place: seats 1, 3 and 5 are team 1, 2, 4 and 6 team 2, and seat n is start n, which is
// how the director pairs starts with teams.  A player sits where his start says (he moves by asking
// for another start, the rooms' own start spot path), every other seated slot takes the seat it was
// shown at (DirectStrikeSeats) as its start, and the team follows the seat.  Seats past six are
// closed, Turkiye and the observer become Random (a Random pool loses Turkiye), and the rules that
// mean nothing here (unit limit, peace time, income sharing, the tech and supply rules, Pro Rules) go
// off.  TRUE when it changed anything, so a network host sends the options again.
Bool FitDirectStrikeRoom(GameInfo *game);
// which of the first six slots shows at which seat, DirectStrikeLobby_seats over the slot list: the
// same answer on every machine of the room
void DirectStrikeSeats(const GameInfo *game, Int seatOf[], Int slotAt[]);
// TRUE when the player in `slot` may move to `seat`: a seat of the six whose slot is open or holds a
// computer.  DirectStrikeTakeSeat moves him: an open seat's start becomes his, a computer there swaps
// starts with him.  The host and a skirmish take a seat with it, and the host takes a joiner's request
// ("StartPos=") with it, so a seat closed or taken by a human meanwhile is refused.  TRUE when it moved
Bool DirectStrikeCanTakeSeat(const GameInfo *game, Int slot, Int seat);
Bool DirectStrikeTakeSeat(GameInfo *game, Int slot, Int seat);
// The room's look (SkirmishGameOptionsMenu, LanGameOptionsMenu, WOLGameSetupMenu): the Direct Strike
// lobby over the room's layout, DirectStrikeLobby.h, its three seat cards a team facing each other.  A
// room calls it after every slot list repaint; ShutdownLobbySeatVisuals takes it off.
void LayoutDirectStrikeRoom(GameWindow *parent, const char *layoutFilename, const GameInfo *game);
enum
{
	APOCALYPSE_SEATS = 4,					///< a network room's co-op seats: RMG_APOC_SEATS_MOST, which clampSettings holds the map to
	APOCALYPSE_SKIRMISH_SEATS = 1,		///< the skirmish room's: the player alone (the owner, 2026-11-27)
};
// Apocalypse in the skirmish room: like Direct Strike the map's name carries the mode
// (ApocalypseDirector wakes on its waypoints), so nothing travels in the options string.  A fresh
// Apocalypse map for the room's seats, staged, in the cache and the game's, CRC and size with it
Bool SetApocalypseMap(GameInfo *game, Bool allSeats = FALSE);	// allSeats: four starts, so a network room opens four seats
// No computer seats (the owner, 2026-11-27): one becomes Open within the room's seats and Closed past
// them, as does any empty seat past them; the seated players on starts 1..n in seat order (the day
// waves' first front faces start 1), and the rules that mean nothing in a co-op survival off.  seats is
// APOCALYPSE_SEATS for a network room, APOCALYPSE_SKIRMISH_SEATS for the skirmish room; TRUE when it
// changed anything
Bool FitApocalypseRoom(GameInfo *game, Int seats = APOCALYPSE_SEATS);
// the SlotState FitApocalypseRoom gives seat `seat` of a room of `seats` that holds SlotState `state`
Int ApocalypseSeatState(Int seat, Int seats, Int state);
// a generated Apocalypse map ("rmg_v<n>_<seed>_<n>p_<cells>c_apocalypse"), by its name or its folder's,
// '\' or '/' paths alike: how a joiner, a game list and a network room know the mode
Bool IsApocalypseMap(const AsciiString &map);
// An Apocalypse room shows its four seats only: every window of the layout's seat rows from this row on
// hidden.  A room calls it after each slot list repaint, which would show them again
void HideLobbySeatRowsFrom(GameWindow *parent, const char *layoutFilename, Int firstHiddenRow);
// The Apocalypse room's figures (MENU-REDESIGN.md package 3b, README-apocalypse-room.md), read from the
// director's constants and roster so the room never says what the match does not do.  Game seconds from
// the start to the fall of night `night` (from 1): day and dusk, then a whole cycle a night
Int ApocalypseNightFallsAt(Int night);
/// game seconds as the clock on the wall reads them at that game speed percent (150 plays them 1.5x)
Int ApocalypseWallSeconds(Int gameSeconds, Int speedPercent);
/// the roster rows a roll may draw that first come on night `night` (0 for day one), in roster order,
/// at most `most` into kinds; how many there are.  Bosses and what only a brood leaves are not rolled
Int ApocalypseNewKindsOfNight(Int night, Int kinds[], Int most);

// The Classic room in the menu language (docs/plans/MENU-REDESIGN.md package 3a in the wrapper, the
// design README-classic-room.md).  The three rooms keep their layouts' windows; InitLobbySeatVisuals
// gives them frame A, the heads, the drawn rows and the menu's keys, and UpdateLobbySeatVisuals lays
// the seat rows out from a team-grouped order: one head a team, teams from the fewest seats to the
// most (seated players with no team first, open and closed seats with no team last), a scroll past
// what the seat column holds, every row the same size at 32 seats as at 8.  A team change takes its
// row to its new group only once the team list has closed.  Everything at 1080, in board pixels.
enum
{
	LOBBY_ROW_SEAT = 56,				///< every seat's row, seated, open or closed: one line of 40 tall boxes, 16 between rows
	LOBBY_ROW_HEAD = 32,				///< a team's head: a multiple of 8, so the rows' boxes stay on the grid across it (34 left 50 between them)
	LOBBY_ROW_STRIP = 26,				///< "n SEATS ABOVE" or "BELOW" at the window's edge
	LOBBY_SEATS_TOP = 160,
	LOBBY_SEATS_BOTTOM = 952,
	LOBBY_GROUP_ALONE = -1,			///< seated players with no team, under NO TEAM, EACH ALONE
	LOBBY_GROUP_UNSEATED = -2		///< open and closed seats with no team, under NO TEAM YET
};
struct LobbySeatState
{
	Int team;			///< the team the order sorts the seat by, -1 for none
	Bool seated;	///< a human or a computer sits there
};
struct LobbyRoomItem
{
	Bool head;		///< a team's head; else a seat's row
	Int group;		///< the team, or LOBBY_GROUP_ALONE / LOBBY_GROUP_UNSEATED
	Int slot;			///< the seat a row shows, -1 for a head
	Int count;		///< the seats in the group, for a head
	Int top;			///< from the window's top
	Int height;
};
struct LobbyRoomPlan
{
	LobbyRoomItem items[ 2 * MAX_SLOTS ];
	Int itemCount;
	Int order[ MAX_SLOTS ];		///< every seat, in the order the room shows them
	Int orderCount;
	Int scroll;								///< the first seat shown, an index into order
	Int maxScroll;
	Int above, below;					///< seats past the window's top and foot
};
/// the seats of count slots in the room's order: groups as above, slots in order inside a group;
/// group[] is each ordered seat's group
void LobbyOrderSeats( const LobbySeatState seats[], Int count, Int order[], Int group[] );
/// the window of that order from scroll (clamped) in height pixels, at most mostShown rows: the first
/// seat's head always (a group the window cuts repeats it), whole rows only, a strip for what is past
/// either edge
void LobbyPlanRoom( const LobbySeatState seats[], Int count, Int scroll, Int height, Int mostShown, LobbyRoomPlan &plan );
/// the Apocalypse room's window instead: the survivors' head, then `count` seats in seat order, every
/// row a seated row's height (an open seat says what it adds), at most mostShown, no scroll
void ApocalypsePlanRoom( Int count, Int mostShown, LobbyRoomPlan &plan );
/// FALSE while a Direct Strike lobby lies over the room, which reads its seats by the layout's rows:
/// the room then binds rows to seats one to one and lays nothing out.  Each room says it at init and
/// a network room again once a joiner learns the mode.
void SetLobbyRoomClassic( Bool classic );
/// The skirmish room has fewer rows than seats: the plan hands it the seats the window shows (seat 0
/// first when shown), and it points its rows at them and repaints them.  The network rooms have a row
/// a seat and set none.
typedef void (*LobbyRowBinder)( const Int seats[], Int count );
void SetLobbyRowBinder( LobbyRowBinder binder );
/// The room's map at its own shape in the right column: a wide map 542 wide and at most 278 tall
/// with its name under it, a squarer one 324 tall at the left with its name beside it.  Only the
/// room's own map window moves; a picker's passes through.  positionStartSpots calls it first.
void FitLobbyRoomMap( GameWindow *mapWindow, const MapMetaData *map );
/// a room's Update: lays the rows out again when a held team change or a scroll is owed
void LobbyRoomUpdate( void );
/// a room's Input: the wheel over the seats and a click on a strip or the track scroll them, a press
/// on the track's thumb drags it (the room passes its GWM_LEFT_DRAG and GWM_LEFT_UP too); TRUE when
/// it took the message
Bool LobbyRoomInput( UnsignedInt msg, UnsignedIntPtr mData1 );

// The mode a host's room is for, the Create Game and Host buttons' question.  Set by the lobby that
// asked, read by the room it opens: LAN spends it at room init, online keeps it while hosting
// (OnlinePeer's game.host and the options it holds back until the mode's map is staged).
enum HostGameMode { HOST_MODE_CLASSIC = 0, HOST_MODE_DIRECT_STRIKE, HOST_MODE_APOCALYPSE };
extern Int TheHostGameMode;
// The mode question, a box in the menu language (GameModeBox.wnd's windows, drawn by code): three
// cards, Classic lit as Enter's, and Cancel, which answers nothing.  `modsOnly` TRUE is the old Mods
// buttons' question without the Classic card; FALSE asks all three, Create Game's.  `solo` words it
// for Solo Play (its facts count the computer seats).  The answer gets the HostGameMode picked
void AskGameMode(void (*answer)(Int mode), Bool modsOnly = TRUE, Bool solo = FALSE);
/// a mode's name with its version tag after it ("Direct Strike v1.0"): the name's key, then the one key
/// that holds the version for every place the mode is named; the bare name when that key is missing or empty
UnicodeString ModeNameWithVersion(const char *nameKey, const char *versionKey);

void EnableSlotListUpdates( Bool val );
Bool AreSlotListUpdatesEnabled( void );

void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], 
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] );

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum = -1);

#endif // __GUIUTIL_H__
