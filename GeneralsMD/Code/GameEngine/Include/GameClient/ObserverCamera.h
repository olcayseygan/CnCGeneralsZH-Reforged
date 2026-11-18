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

// ObserverCamera.h ///////////////////////////////////////////////////////////////////////////////
// Who drives a watcher's camera, and whose fog his screen is drawn in.
//
// The mode and the followed player are picked apart, from two lists.  Free is the camera in the
// watcher's own hands.  Director goes to the fight of the last few seconds with the most at stake,
// each hit counted by what the thing hit cost and more for a kill or a superweapon, and stays there
// a while before it looks for a bigger fight.  A special power used anywhere outranks any fight and
// takes the camera at once: it is shown where it lands until the dust settles, for 30 seconds at the
// most, a laser followed along its sweep, and one landing off the picture while another is shown
// goes into a recording's corner window at the lower right.  A power that only looks or hurts nobody, a spy
// satellite, a radar scan, a paradrop, never takes the camera.  Hits on infantry count for a
// quarter, so a skirmish of riflemen rarely takes the camera from tanks or buildings.  The camera glides between places on a spring and only cuts across
// most of a map, and stops short of a place that would put ground off the map on the screen.  Over
// a wide fight it rises above the watcher's own height, which the wheel still moves, and comes back
// down to it when the camera goes elsewhere or is taken back.  With no fight on it goes round the armies on the
// move, the bases going up and the superweapons, a few seconds each, so it never sits still; with a
// player picked it counts only that player's fights and things.  Player shows what the followed player's own screen
// shows: a player's camera comes over the network a few times a second (MSG_SET_REPLAY_CAMERA), an
// AI, which has no camera, gets the narrowed director, and with nobody picked it does nothing.
// Scrolling with the keys or a drag, turning the camera or clicking the radar hands it back to the
// watcher, who keeps the player he picked.  The screen's edge does not scroll while this drives:
// the page's panel stands on the right one, and reaching for it used to scroll the map and take the
// camera from the director with nobody asking.
//
// The followed player is picked from his own list, not the selection: clicking a unit makes its owner
// the watched player, and the camera jumping to an enemy's screen on a click would be no use.  Fog
// on draws the followed player's fog, what he has seen and what he has not, and hides what he
// cannot see, stealthed units he has not detected included; following nobody it changes nothing.
// Nothing here is logic: the camera and the fog are this machine's picture only.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _OBSERVER_CAMERA_H_
#define _OBSERVER_CAMERA_H_

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/GameType.h"
#include "Common/SpecialPowerType.h"
#include "GameClient/Color.h"
#include "GameClient/View.h"

#include <stdio.h>
#include <vector>

enum ObserverCameraMode
{
	OBSERVER_CAMERA_FREE,
	OBSERVER_CAMERA_DIRECTOR,
	OBSERVER_CAMERA_PLAYER
};

/// one thing hit lately, where it stands and how much it counts for
struct DirectorHeat
{
	Coord2D position;
	Real weight;
};

class Player;
class SpecialPowerTemplate;
class ThingTemplate;
class UpgradeTemplate;

/// a special power used lately: who used it, where it was fired from, where it lands, and until which
/// logic frame it is worth watching
struct DirectorEvent
{
	UnsignedInt id;
	const Player *owner;
	Coord2D source;
	Coord2D target;
	UnsignedInt since;
	UnsignedInt until;
	Real weight;
	Bool superweapon;
	Bool landed;		///< it hurt somebody where it was aimed: a superweapon from the start, any other once a hit is seen there
	Bool looksOnly;	///< a power that hurts nobody, a scan or a paradrop, which never lands
	UnsignedInt landsAt;	///< the frame the scouting pass saw its first hit at its target, 0 when it saw none
	Bool shown;			///< the director's picture or the recording's corner window has been on it
	const SpecialPowerTemplate *power;	///< the power used; NULL for a warhead no power sent
	const ThingTemplate *sourceThing;		///< what it was fired from
	UnsignedInt landedAt;	///< the frame it was first seen hitting its target, 0 before; an instant replay is cut round it
};

/// whether a power of this type only looks or hurts nobody where it is aimed: a spy satellite, a radar
/// scan, a spy drone, a cash hack, a leaflet drop, a paradrop, an ambush.  Such a power never takes the camera,
/// whatever fight goes on round its target; its flag still hangs under its player's card
Bool ObserverCamera_powerLooksOnly( SpecialPowerType type );
/// whether event, not landed yet, lands on frame: never for a power that only looks; with the frame
/// the scouting pass saw it hit, from DIRECTOR_PREROLL_FRAMES before it; with no timeline at all, once
/// a fight is near its target (fightNear).  A power the scouting pass saw land nowhere never does
Bool ObserverCamera_landsNow( const DirectorEvent &event, UnsignedInt frame, Bool timelineKnown, Bool fightNear );
/// whether a power landing at target is off the picture the director shows round place: further than
/// a share of screenGround, the ground the whole picture spans across its middle row
Bool ObserverCamera_offPicture( const Coord2D &target, const Coord2D &place, Real screenGround );
/// -directorrecord: the id of the landed power, not shown, landing off the picture round place, that the
/// corner window shows beside shown.  The heaviest, so a superweapon fired while two powers hold the
/// picture and the window takes the window at once; of equals held, the one the window shows now, then
/// the latest.  0 for none
UnsignedInt ObserverCamera_secondPower( const std::vector< DirectorEvent > &events, UnsignedInt shown, UnsignedInt held,
	const Coord2D &place, Real screenGround );

/// -directorrecord's corner window at the lower right, the second action beside the full screen
/// picture: a second power off the picture first, else a second fight worth filming off it
enum DirectorWindowKind { DIRECTOR_WINDOW_NONE, DIRECTOR_WINDOW_POWER, DIRECTOR_WINDOW_FIGHT };
DirectorWindowKind ObserverCamera_pickWindow( UnsignedInt secondPower, Bool secondFight );
/// whether the window is up after this frame, given whether it is now, whether there is something for
/// it, and how long ago it last came up or went: it slides all the way in before it goes, and all the
/// way out before it comes again
Bool ObserverCamera_holdWindow( Bool on, Bool wanted, UnsignedInt framesSince );
/// the window's picture is its camera's whole picture made OBSERVER_WINDOW_SCALE times smaller each way
enum { OBSERVER_WINDOW_SCALE = 4 };
/// the plate round the window's picture, in pixels of a picture height pixels high: its sides and foot,
/// the band over the picture that holds the name chip, the trim rule along the top, the top right
/// corner cut at 45 degrees and the highlight line round the picture
struct DirectorWindowFrame
{
	Int border;
	Int header;
	Int rule;
	Int cut;
	Int highlight;
};
DirectorWindowFrame ObserverCamera_windowFrame( Int height );
/// the window's picture on a width by height picture, progress 0 to 1 of the way slid in from past the
/// right edge: a quarter of the picture each way in the lower right corner, its plate a margin in from
/// both edges, so the lower left stays clear.  Empty at progress 0
IRegion2D ObserverCamera_windowRect( Int width, Int height, Real progress );

/// -directorrecord's events file, Videos\<name>.events beside the film: what happened and where, a line
/// each, for whatever is made of the film afterwards.  An event line is its logic frame, its type
/// (built, power, scan, superweapon, lost, defeated, rank), the player, where (-1 -1 for none), the
/// thing's template name and the name the game shows for it, UTF-8, "-" for none
struct DirectorFilmEvent
{
	UnsignedInt frame;
	AsciiString type;
	Int player;
	Real x;
	Real y;
	AsciiString thing;
	AsciiString name;
};
/// "event <frame> <type> <player> <x> <y> <thing> <name>", the name last since it may hold spaces, and
/// back; FALSE for a line that is not one
AsciiString ObserverCamera_formatFilmEvent( const DirectorFilmEvent &event );
Bool ObserverCamera_parseFilmEvent( const char *line, DirectorFilmEvent *event );
/// a name the game shows, as an event's name: UTF-8, the hotkey's '&' dropped, one line, "-" for none
AsciiString ObserverCamera_filmName( const UnicodeString &shown );
/// "map <map file> <loX> <loY> <hiX> <hiY>": the map's file name, which may hold spaces, and its extent
AsciiString ObserverCamera_formatFilmMap( const char *mapFile, Real loX, Real loY, Real hiX, Real hiY );
Bool ObserverCamera_parseFilmMap( const char *line, AsciiString *mapFile, Real *loX, Real *loY, Real *hiX, Real *hiY );
/// "player <index> <template> <RRGGBB> <display name>", the name UTF-8 and last, since it may hold spaces
AsciiString ObserverCamera_formatFilmPlayer( Int index, const char *playerTemplate, UnsignedInt rgb, const char *displayName );
Bool ObserverCamera_parseFilmPlayer( const char *line, Int *index, AsciiString *playerTemplate, UnsignedInt *rgb, AsciiString *displayName );

/// -directorrecord's instant replays.  A superweapon or a general's power that landed and that neither the
/// picture nor the corner window was ever on is shown again once the film is calm: the filming pass writes
/// each to Videos\<name>.replays, a third pass plays the match again undrawn but for those frames and
/// records them back to back into Videos\<name>_replays.mp4, and Videos\<name>_replays.txt says where each
/// clip is in it and where it goes into the film.  A clip is logic frames first to last round landed, the
/// frame the power hit; splice is the first calm logic frame of the film after it, 0 until it is known
struct DirectorReplay
{
	UnsignedInt first;
	UnsignedInt last;
	UnsignedInt landed;
	Coord2D target;
	Int player;
	AsciiString power;	///< the power's name, "-" for a warhead no power sent
	UnsignedInt splice;
};
/// whether a power the director is done with is a missed big moment: a superweapon or a general's power
/// (one a promotion bought) that hit somebody where it was aimed and was never on the picture or in the window
Bool ObserverCamera_missedBigMoment( Bool superweapon, Bool generalsPower, Bool landed, Bool shown );
/// the frames a replay of a power that landed on landed shows: a couple of seconds before it, several after
void ObserverCamera_replayRange( UnsignedInt landed, UnsignedInt *first, UnsignedInt *last );
/// whether frame is the replay's splice: past its last frame, and the director calm there (no power, no
/// fight, nothing in the window), or a while past it whatever the director is doing
Bool ObserverCamera_spliceNow( UnsignedInt frame, UnsignedInt last, Bool calm );
/// "replay <first> <last> <landed> <x> <y> <player> <splice> <power>", and back; FALSE for a line that is not one
AsciiString ObserverCamera_formatReplay( const DirectorReplay &replay );
Bool ObserverCamera_parseReplay( const char *line, DirectorReplay *replay );
/// the replays in the order they are played, by first frame, each cut to start after the one before ends
/// since one pass plays every frame once; one cut past the frame it landed on is dropped
void ObserverCamera_planReplays( std::vector< DirectorReplay > &replays );
/// the planned replay whose frames, or the lead frames drawn unrecorded before them, hold frame; -1 for none
Int ObserverCamera_replayAt( const std::vector< DirectorReplay > &replays, UnsignedInt frame, UnsignedInt lead );
/// the film's picture a logic frame's first picture is, the film starting on logic frame start at pictures
/// a logic frame (-recordfps over the logic rate)
UnsignedInt ObserverCamera_filmPicture( UnsignedInt frame, UnsignedInt start, Int pictures );
/// one line of Videos\<name>_replays.txt: "clip <index> <first picture> <pictures> <splice picture> <first
/// frame> <last frame> <player> <power>", pictures of the replays movie and of the film, frames logic frames
AsciiString ObserverCamera_formatReplayClip( Int index, UnsignedInt firstPicture, UnsignedInt pictures, UnsignedInt splicePicture,
	const DirectorReplay &replay );
/// the .replays file the filming pass wrote, "" when it wrote no replay: WinMain runs the replay pass on it
const char *ObserverCamera_replaysWritten( void );

/// -directorrecord's broadcast shows some things one at a time from a queue: a special power's or a
/// bought upgrade's flag under its player's card in the score bar, and a defeated player's banner.
/// start is the logic frame it began to come in, 0 while it waits behind another; leaving the frame it
/// began to go, 0 while it holds.  A defeat's banner has neither power nor upgrade
struct DirectorShowing
{
	Int player;
	const SpecialPowerTemplate *power;
	const ThingTemplate *sourceThing;
	Bool superweapon;
	UnsignedInt start;
	UnsignedInt leaving;
	const UpgradeTemplate *upgrade;
};

/// one logic frame of such a queue: the first in it comes in over moveFrames, holds holdAlone frames
/// alone, half that with one waiting behind it and a third with more, goes over moveFrames, and the
/// next starts the frame it is gone.  With keepLast the last one holds for good.  TRUE when one started
/// this frame
Bool ObserverCamera_advanceShowing( std::vector< DirectorShowing > &queue, UnsignedInt frame, UnsignedInt moveFrames, UnsignedInt holdAlone,
	Bool keepLast );
/// how far in a queued thing that comes and goes over moveFrames is on frame, 0 to 1, eased in and out.
/// frame and the frames below are a picture's time (GameEngine_pictureFrame), between two logic frames
/// for a -recordfps picture
Real ObserverCamera_showingShown( const DirectorShowing &showing, Real frame, UnsignedInt moveFrames );
/// a defeated player's card on frame: how bright its red flash is, how far the line through it has
/// drawn and how far it has collapsed, each 0 to 1.  defeated is the frame he lost on, collapseFrom the
/// frame his card starts to go
void ObserverCamera_cardExit( Real frame, UnsignedInt defeated, UnsignedInt collapseFrom, Real *flash, Real *struck, Real *collapse );
/// the frame the card of a player who lost on defeated starts to collapse: once it has been seen struck,
/// and not before the card that went before it, from lastCollapse, has gone, so the others slide for
/// one card at a time
UnsignedInt ObserverCamera_collapseFrom( UnsignedInt defeated, UnsignedInt lastCollapse );
/// progress along from to to, eased in and out, 0 before from and 1 after to
Real ObserverCamera_easeBetween( Real progress, Real from, Real to );

/// -directorrecord's scouting pass plays the match headless first and writes down what is worth
/// filming, so the filming pass can be there before it starts.  A fight runs from the scan it was
/// first hot on, start, to the last, starting at place and last seen at target; peak is its hottest
/// scan and sides the mask of every player who dealt or took a hit in it.  A special power is a
/// moment fired on start from place at target, peak its weight against other powers; a power that is
/// not a superweapon has 0 until a hit is seen where it was aimed, and last is the frame that hit was
/// seen on, start until then.  A power that only looks is not written down at all
struct DirectorMoment
{
	UnsignedInt start;
	UnsignedInt last;
	Coord2D place;
	Coord2D target;
	Real peak;
	UnsignedInt sides;
	Bool power;
	Bool superweapon;
};

/// one hit a player dealt another he is at war with, for the scouting pass: where, what it counts
/// for, and the two players' masks together
struct DirectorFightHit
{
	Coord2D position;
	Real weight;
	UnsignedInt sides;
};

/// the scouting pass's fights carried over one scan of the hits.  Each crowd of hits, hottest first,
/// joins the fight still going nearest it, or starts a new one on this frame; a fight with no crowd
/// near it for a few seconds is over and is not joined again
void ObserverCamera_trackFights( std::vector< DirectorMoment > &fights, const std::vector< DirectorFightHit > &hits, UnsignedInt frame );
/// whether a moment is worth the camera's time: a superweapon, any other special power that hurt
/// somebody where it was aimed, and a fight that got hot enough and lasted.  The rest fizzled
Bool ObserverCamera_worthFilming( const DirectorMoment &moment );
/// the scouting pass's special powers on frame, each scan up to EVENT_MOST_FRAMES after its launch:
/// one a hit of hits lands within the gather radius of the target of has landed, on frame.  A strike
/// flown in from the map's edge lands well after its launch, and twelve seconds missed most of them
void ObserverCamera_markLandings( std::vector< DirectorMoment > &moments, const std::vector< DirectorFightHit > &hits, UnsignedInt frame );
/// the frame the scouting pass saw the special power fired on frame at at land, 0 for one it did not
/// see land, so a match it did not scout waits for the hits
UnsignedInt ObserverCamera_powerLandsAt( const std::vector< DirectorMoment > &timeline, UnsignedInt frame, const Coord2D &at );
/// whether the timeline has a fight worth filming near place, going on at frame or beginning within
/// the pre-roll, that lasts to until and peaked at least leastPeak, leaving out any that is also near
/// besides, the place the director already shows (NULL for none)
Bool ObserverCamera_fightLasts( const std::vector< DirectorMoment > &timeline, const Coord2D &place, UnsignedInt frame, UnsignedInt until,
	const Coord2D *besides, Real leastPeak = 0.0f );
/// the scouting pass's fights as its run ends on frame end: one still going was cut short by the end
/// of the match, not by itself, and is counted as lasting, so a last battle that got hot is filmed
/// rather than skipped as a fizzle
void ObserverCamera_closeTimeline( std::vector< DirectorMoment > &moments, UnsignedInt end );
/// the moment the director goes to wait at on frame: of the moments worth filming that begin within the
/// pre-roll after frame, a superweapon first, then any other special power, then the hottest fight,
/// leaving out any within apart of taken, the place the corner window already shows (NULL for none).  A
/// power is waited for where and when it lands.  -1 for none
Int ObserverCamera_prerollMoment( const std::vector< DirectorMoment > &timeline, UnsignedInt frame, const Coord2D *taken, Real apart );
/// whether the fight going on at place on frame is one the scouting pass saw fizzle, which the
/// director does not cut to.  FALSE where the pass saw nothing, so a match it did not scout is
/// filmed as before
Bool ObserverCamera_fizzles( const std::vector< DirectorMoment > &timeline, const Coord2D &place, UnsignedInt frame );
/// a fight worth filming beginning within SPLIT_LEAD_FRAMES after frame, or with keeping, the split up
/// already, one going on at frame, more than apart from first: the hottest such, -1 for none.  A
/// fight already going is not split for anew: where it began is only a guess at where it is now, and
/// a fight that had wandered off left the second picture on empty ground
Int ObserverCamera_plannedSecond( const std::vector< DirectorMoment > &timeline, const Coord2D &first, UnsignedInt frame, Real apart,
	Bool keeping );
/// a moment as one line of the timeline file, and back; FALSE for a line that is not one
AsciiString ObserverCamera_formatMoment( const DirectorMoment &moment );
Bool ObserverCamera_parseMoment( const char *line, DirectorMoment *moment );

/// how fast the camera is going on each of its six axes, carried from one frame's step to the next
struct ObserverCameraVelocity
{
	Real x, y, z, angle, pitch, zoom;
};

/// the place the hits crowd most: each hit's weight summed over those within DIRECTOR_GATHER_RADIUS
/// of it, and the best one's neighbours averaged by weight.  FALSE when nothing was hit
Bool ObserverCamera_hottestPlace( const std::vector< DirectorHeat > &hits, Coord2D *place, Real *heat );
/// the weight of the hits within DIRECTOR_GATHER_RADIUS of a place, and their weighted middle
Real ObserverCamera_heatAround( const std::vector< DirectorHeat > &hits, const Coord2D &around, Coord2D *middle );
/// whether a director holding a place with heatHere for framesHere should cut to one with heatThere;
/// peakHere is the hottest the place has been while held, and a place burnt down from it lets go sooner
Bool ObserverCamera_shouldMove( Real heatHere, Real heatThere, UnsignedInt framesHere, Real peakHere );
/// what one recent hit counts for: more the dearer the thing hit, more again if it died or is a
/// superweapon, and a quarter of that for infantry
Real ObserverCamera_hitWeight( Int cost, Bool killed, Bool superweapon, Bool infantry );
/// what one thing is worth looking at with no fight on: its cost, doubled while it marches or is
/// being built, halved for a building that is only standing there, tripled for a superweapon
Real ObserverCamera_sightWeight( Int cost, Bool structure, Bool busy, Bool superweapon );
/// the best place among the sights away from the ones in seen; FALSE when every sight was seen
Bool ObserverCamera_nextSight( const std::vector< DirectorHeat > &sights, const std::vector< Coord2D > &seen, Coord2D *place );
/// whether the director stays on the event it is showing, current, rather than go to best, the
/// biggest and latest one it may show now: it stays while best lands in the same picture or weighs
/// less, and goes at once, settled or not, to a power as big landing anywhere else.  best is NULL when
/// none may be shown: narrowed to a player whose things round the target are all gone, the current
/// one is filtered out and is still kept
Bool ObserverCamera_stayOnEvent( const DirectorEvent *current, const DirectorEvent *best );
/// whether owner using power on target at frame is more of event, a use still shown, rather than a new one
Bool ObserverCamera_sameUse( const DirectorEvent &event, const Player *owner, const SpecialPowerTemplate *power, const Coord2D &target,
	UnsignedInt frame );
/// the frame an event shown until until is kept to when something more happens at it on frame: a few
/// seconds past frame, never past EVENT_MOST_FRAMES after it began.  Kept going by every hit round its
/// target, a particle cannon fired into the middle of a map held the camera there for eight minutes
UnsignedInt ObserverCamera_eventKeptTo( const DirectorEvent &event, UnsignedInt frame, UnsignedInt more );
/// how widely the hits within DIRECTOR_GATHER_RADIUS of a place lie round it, by weight
Real ObserverCamera_spreadAround( const std::vector< DirectorHeat > &hits, const Coord2D &around );
/// how much higher than the watcher's own the director takes the camera over a fight this spread
Real ObserverCamera_fightHeight( Real spread );
/// the place moved the least that keeps the screen's ground over the map: corners are where the
/// screen's corners fall on the ground, taken from the point looked at.  Where the screen is wider
/// than the map, the place stays between the two edges
Coord2D ObserverCamera_keepInMap( const Coord2D &place, const Coord2D *corners, Int count, const Region2D &map );
/// a step of the camera towards where it is going on a critically damped spring: it gathers speed,
/// never goes faster than topSpeed across the ground, and slows into place without overshooting.
/// smoothSeconds is roughly how long it takes to arrive.  Further apart than a glide should cross,
/// it jumps there and stops
ViewLocation ObserverCamera_approach( const ViewLocation &from, const ViewLocation &to, Real elapsedSeconds, Real smoothSeconds,
	Real topSpeed, ObserverCameraVelocity *velocity );
/// the director's height over the watcher's own, one step from from towards to on a spring that
/// gathers and slows over about 1.6 s; a cut takes it there at once
Real ObserverCamera_easeHeight( Real from, Real to, Real *velocity, Real elapsedSeconds, Bool cut );
/// -directorrecord's second fight: the hottest place among the hits more than needed from first, so
/// the corner window never shows the director's own ground.  FALSE when nothing that far was hit
Bool ObserverCamera_secondPlace( const std::vector< DirectorHeat > &hits, const Coord2D &first, Real needed, Coord2D *place, Real *heat );
/// whether there is a second fight for the corner window, given whether there is now, how far apart
/// the two places are against the needed distance, and how long ago it last came or went: on, after a
/// rest, for a second fight more than needed away that is big on its own and next to a first one;
/// held through SPLIT_LEAST_FRAMES whatever the second fight does, then while it keeps a quarter of the
/// first; off at once, however new, when the two places come within two thirds of needed and the
/// window would show the director's own ground
Bool ObserverCamera_holdSplit( Bool split, Real firstHeat, Real secondHeat, Real apart, Real needed, UnsignedInt framesSince );

/// a recording draws the main picture as draw 0 and the corner window as draw OBSERVER_WINDOW_PANE,
/// so it keeps OBSERVER_DRAWN_PANES pictures a frame at the most
enum { OBSERVER_WINDOW_PANE = 1, OBSERVER_DRAWN_PANES = 2 };
/// the score bar's one row of cards, a card a player, each block of players (a team, or one alone)
/// side by side with versusWidth between two blocks for the "vs" and cardGap between two cards of a
/// block: every card's left and every block's left from the row's, and the row's whole width
Int ObserverCamera_cardRow( const std::vector< Int > &sizes, Int cardWidth, Int cardGap, Int versusWidth,
	std::vector< Int > *cardLefts, std::vector< Int > *blockLefts );
/// the score bar's row, 0 or 1, for each block of players: one row under five players, else blocks
/// kept whole and in order, the first row taking them until it holds about half the cards
std::vector< Int > ObserverCamera_cardRows( const std::vector< Int > &sizes );
/// the first of steps sizes, largest first, whose row of cards is no wider than room; the last when
/// none is
Int ObserverCamera_cardStep( const std::vector< Int > &sizes, const Int *cardWidths, const Int *cardGaps,
	const Int *versusWidths, Int steps, Int room );
/// the widest card a row of these blocks can have and still fit room
Int ObserverCamera_cardWidthIn( const std::vector< Int > &sizes, Int cardGap, Int versusWidth, Int room );
/// how many of a text's characters to keep in widest pixels, from prefixWidths, the width of its first
/// n characters at n (0 up to the whole text): all of them when the whole text fits, else the most
/// that fit with an ellipsis of ellipsisWidth after them, 0 when not even one does
Int ObserverCamera_fitCount( const std::vector< Int > &prefixWidths, Int ellipsisWidth, Int widest );
/// values cut into width pixels in proportion, the rounding handed to the largest remainders so the
/// pieces fill width exactly; all zero when the values add up to nothing
std::vector< Int > ObserverCamera_barShares( const std::vector< Int > &values, Int width );
/// a player's colour lifted towards white until it reads on the broadcast's ground at 4.5 to 1;
/// a colour that already does comes back unchanged
Color ObserverCamera_readableColor( Color color );
/// whether a player's colour is close enough to the brand gold of the score bar's frame to need an edge
Bool ObserverCamera_nearBrandGold( Color color );
/// how far an animation of length frames that started on start is on frame, eased in and out, 0 to 1
Real ObserverCamera_easeFrames( Real frame, UnsignedInt start, UnsignedInt length );
/// the score bar's frame, named for the lines between the split screen's panes it was cut to match:
/// the dark edge each side of its gold, in pixels
enum { OBSERVER_PANE_LINE_EDGE = 2 };
/// the gold line its sizes are taken from, in pixels, for a picture height pixels high: 6 at 720, 9 at 1080
Int ObserverCamera_paneLineWidth( Int height );
/// the width of the soft band of the brand's blue its halo is taken from
Int ObserverCamera_paneBandWidth( Int height );

class ObserverCamera
{
public:
	ObserverCamera();

	void reset( void );
	/// once a frame on a watcher's machine: move the camera and swap the fog when it has to
	void update( UnsignedInt nowMilliseconds );

	/// a player's camera as it came over the network or out of a replay
	void notePlayerView( Int playerIndex, const ViewLocation &view );

	ObserverCameraMode getMode( void ) const { return m_mode; }
	void setMode( ObserverCameraMode mode );
	/// the player whose screen the player mode shows, whose fights the director keeps to and whose
	/// fog is drawn while fog is on; NO_PLAYER for nobody
	void followPlayer( Int playerIndex );
	/// the player being followed, still while the watcher has the camera in his own hands, or
	/// NO_PLAYER
	Int getFollowedPlayerIndex( void ) const { return m_followed; }
	/// the camera is where this put it last frame, the director's or a player's
	Bool isDriving( void ) const { return m_driving; }
	Bool isFogOn( void ) const { return m_fog; }
	void setFog( Bool fog ) { m_fog = fog; }
	/// a player picked on the observer bar or the rack: his camera driving the screen, and with takeFog
	/// his fog on it, until stopLooking puts back the camera the watcher had before.  The mode before is
	/// remembered once a look: a second player picked, or the same one taken again after the watcher
	/// scrolled off him, keeps it
	void lookThrough( Int playerIndex, Bool takeFog = TRUE );
	/// nobody followed, the camera the watcher had before the look, and the fog off only when the look
	/// turned it on; FALSE when nobody was followed and no look was on, so there was nothing to take back
	Bool stopLooking( void );
	/// a look is on: lookThrough since the last stopLooking, the lever not moved to Free or Director
	Bool isLooking( void ) const { return m_looking; }
	/// the look turned the fog on, so ending it turns the fog off
	Bool lookTookFog( void ) const { return m_lookTookFog; }
	/// a replay rewind's reset forgets the look; the rewind gives it back with the mode it kept
	void keepLooking( Bool looking, Bool tookFog ) { m_looking = looking; m_lookTookFog = looking && tookFog; }
	/// the player mode with a camera of the followed player's to show, which is what is on the screen
	Bool isShowingPlayerView( void ) const;

	/// the player whose fog the screen is drawn in: the local player, or whoever a watcher with fog
	/// on is following
	Int getShroudPlayerIndex( void ) const;

	/// a special power was used: logic tells the director, and never asks it anything back.  power is
	/// the power and sourceThing what fired it, both NULL for a warhead no power sent
	void noteSpecialPower( const Player *owner, const Coord3D *from, const Coord3D *at, Bool superweapon,
		const SpecialPowerTemplate *power, const ThingTemplate *sourceThing );
	/// a superweapon is hitting the ground here this frame, a beam or a warhead.  Keeps the event it
	/// belongs to going a few seconds more, and with follow the event's target moves with it
	void noteSuperweaponHit( const Player *owner, const Coord3D *at, Bool follow );
	/// a player finished buying an upgrade, for himself or for one of his things: -directorrecord hangs
	/// its flag under his card.  Logic tells, and never asks back
	void noteUpgrade( const Player *owner, const UpgradeTemplate *upgrade );

	/// the watcher's own height back on the view, if the director had raised it over a fight
	void releaseHeight( void );

	/// -directorrecord's corner window at the lower right: a second power, or a second fight, off the
	/// picture.  Drawn before the frame everybody sees, for the recording only and never presented, and
	/// the recording shrinks it into the window's picture; the plate round it is the main draw's broadcast
	Bool isWindowDrawn( void ) const { return m_windowProgress > 0.0f; }
	Bool isDrawingWindow( void ) const { return m_drawingPane == OBSERVER_WINDOW_PANE; }
	/// where the window's picture lies now, empty while it is not drawn
	IRegion2D getWindowRect( void ) const;
	/// who the window shows: the power's user, or everybody dealing or taking hits in its fight
	PlayerMaskType getWindowSides( void ) const;
	Bool isWindowPower( void ) const { return m_windowKind == DIRECTOR_WINDOW_POWER; }
	/// the view moved to the window's camera for one draw, and put back after it
	void beginWindowPass( void );
	void endWindowPass( void );
	/// the window's draw is under way, not the main one
	Bool isDrawingSecond( void ) const { return m_drawingPane != 0; }
	/// which draw is under way: 0 for the main picture, OBSERVER_WINDOW_PANE for the window
	Int getDrawingPane( void ) const { return m_drawingPane; }

	/// the broadcast drawn over the main picture, its score bar, the window's plate and its banners: the
	/// recording keeps these rectangles over the window's picture.  Cleared at the start of each of its draws
	void clearBroadcast( void ) { m_broadcast.clear(); }
	void addBroadcast( const IRegion2D &region ) { m_broadcast.push_back( region ); }
	const std::vector< IRegion2D > &getBroadcast( void ) const { return m_broadcast; }

	/// -directorrecord's broadcast: the flag of a special power hanging under a player's card now, NULL
	/// for none, and how far it has dropped, 0 to 1
	const DirectorShowing *getPowerFlag( Int playerIndex, Real *drop ) const;
	/// the defeated player's banner up now, NULL for none, and how far in it is
	const DirectorShowing *getDefeatBanner( Real *shown ) const;
	/// every player seen playing in this match, the ones who have lost included
	PlayerMaskType getPlayedMask( void ) const { return m_playedMask; }
	/// the logic frame a player lost on, 0 while he has not, and the frame his card starts to collapse
	UnsignedInt getDefeatFrame( Int playerIndex ) const { return m_defeatFrame[ playerIndex ]; }
	UnsignedInt getCollapseFrame( Int playerIndex ) const { return m_collapseFrame[ playerIndex ]; }
	/// how far in the winner's banner is, 0 until a while after the match is decided
	Real getWinnerShown( void ) const;

	/// -directorrecord's scouting pass, once a pass of a headless run: count the fights on every scan
	/// and a checkpoint of the logic's CRC now and then
	void scout( void );
	/// and at its end, the timeline written to the -directorscout file
	void finishScout( void );

	/// -directorreplays: this run is -directorrecord's replay pass, the match played again for the
	/// missed moments' frames alone
	Bool isReplaying( void ) const;
	/// the logic frame is one of a replay's, recorded
	Bool replayRecords( UnsignedInt frame );
	/// the logic frame is drawn by nobody: no replay's frames, nor the lead before them
	Bool replaySkipsDraw( UnsignedInt frame );
	/// every replay is recorded, or the match went another way and the pass gave up: the run quits
	Bool replaysFinished( UnsignedInt frame );

	enum { NO_PLAYER = -1 };

private:
	void updateShroudViewer( void );
	void endLook( void );
	void holdHeight( Bool hold );
	void driveHeight( Real extra );
	Bool takenByHand( const ViewLocation &current ) const;
	Coord2D keepInMap( const Coord2D &place, const ViewLocation &current ) const;
	void aimView( const Coord2D &look, Real zoom );
	Coord2D placeOnPixel( const Coord2D &subject, const Coord2D &pixel, Real zoom );
	Region2D mapRegion( void ) const;
	void updateSecondFight( void );
	void updateWindow( UnsignedInt frame );
	void easeWindow( Real at );
	void stepWindowCamera( const ViewLocation &step, Real elapsedSeconds );
	void openFilmEvents( void );
	void noteFilmEvent( UnsignedInt frame, const char *type, Int player, Real x, Real y, const char *thing, const UnicodeString &name );
	void scanFilmEvents( UnsignedInt frame );
	void flushFilmEvents( void );
	/// everybody dealing or taking the last scan's hits in the fight at subject
	PlayerMaskType sidesAround( const Coord2D &subject ) const;
	void measureScreenGround( void );
	Real splitApart( const Coord2D &second ) const;
	Bool chooseTarget( const ViewLocation &current, ViewLocation *target );
	Bool directorPlace( const Player *narrowTo, Coord2D *place );
	DirectorEvent *findEvent( UnsignedInt id );
	void dropOldEvents( UnsignedInt frame );
	void loadTimeline( void );
	void checkTimeline( UnsignedInt frame );
	void noteFlag( const DirectorEvent &event );
	void updateBroadcastMoments( UnsignedInt frame );
	void noteMissedMoment( const DirectorEvent &event );
	void updateReplaysFile( UnsignedInt frame );
	void loadReplays( void );
	void updateReplay( void );

	/// a sight, a fight going on, a special power, or a fight the timeline says is about to begin
	enum PlaceKind { PLACE_SIGHT, PLACE_FIGHT, PLACE_EVENT, PLACE_UPCOMING };

	/// a logic frame and the logic's CRC on it, which the two passes compare to know they played one match
	struct CrcCheckpoint
	{
		UnsignedInt frame;
		UnsignedInt crc;
	};

	std::vector< DirectorMoment > m_scouted;		///< the scouting pass's fights and special powers so far
	std::vector< CrcCheckpoint > m_scoutCrcs;		///< and its checkpoints
	UnsignedInt m_scoutScanned;									///< the logic frame it last counted the hits on
	std::vector< DirectorMoment > m_timeline;		///< what the scouting pass saw, for the filming pass
	std::vector< CrcCheckpoint > m_timelineCrcs;
	Bool m_timelineLoaded;
	/// -directorrecord's replays, the whole process's like the timeline: the filming pass's missed moments
	/// waiting for a splice, or the replay pass's plan
	std::vector< DirectorReplay > m_replays;
	std::vector< CrcCheckpoint > m_replayCrcs;	///< the filming pass's own CRC checkpoints, written beside the replays and checked by the replay pass
	size_t m_replayCrcsWritten;									///< how many of them the file has
	FILE *m_replayFile;													///< the filming pass's Videos\<name>.replays, NULL until its first replay
	Bool m_replayFileTried;
	Bool m_replaysLoaded;												///< the replay pass has read its file
	Bool m_replayGaveUp;												///< the replay pass's match went another way than the film's
	Bool m_replaysDone;													///< the replay pass has written its clip list or given up
	Int m_replayClip;														///< the replay the camera is on, -1 for none
	UnsignedInt m_replayFrame;									///< the logic frame the replay pass last checked
	Int m_placeMoment;													///< the timeline's moment the director waits at, while the place is one
	Coord2D m_skippedFight;											///< the last fizzling fight the director passed over, so it is logged once

	ObserverCameraMode m_mode;
	ObserverCameraMode m_modeBeforeLook;	///< what stopLooking gives back; kept across a rewind's reset
	Bool m_looking;									///< isLooking
	Bool m_lookTookFog;							///< the look turned the fog on, so letting go turns it off
	Int m_followed;
	Bool m_fog;
	Int m_shroudViewer;
	Bool m_driving;									///< the camera was put where it is by this, last frame
	Bool m_holdingHeight;						///< the view's own height easing is off while a player's zoom is shown
	Bool m_heightDriven;						///< the director has the camera's height above the ground
	Real m_handHeight;							///< the watcher's own height, the wheel's turns while driven added in
	Real m_drivenHeight;						///< the height the director left the view at last frame
	Real m_heightExtra;							///< the height over the watcher's own the director has eased to so far
	Real m_heightExtraVelocity;			///< how fast that height is moving, for its spring
	Real m_lastZoom;								///< the view's zoom at the last update, for the zoom jump log
	Bool m_lastCut;									///< the last update cut
	Int m_spentMoment;							///< the timeline moment whose second fight was ended early, so it does not come again
	Coord3D m_drivenTo;							///< where this put the camera last frame, inside the view's constraint
	UnsignedInt m_lastUpdate;
	UnsignedInt m_directedFrame;		///< the logic frame -directorrecord's once-a-frame work last ran on; -recordfps updates a frame more than once
	Bool m_directing;								///< this update is the logic frame's first, or there is no -directorrecord: the director decides on it
	ObserverCameraVelocity m_velocity;

	ViewLocation m_playerViews[ MAX_PLAYER_COUNT ];

	Bool m_placeValid;
	Coord2D m_place;								///< where the director is looking
	UnsignedInt m_placeSince;				///< the logic frame it went there
	UnsignedInt m_placeScanned;			///< the logic frame the hits were last counted
	const Player *m_placeFor;				///< whose fights the place was picked from, NULL for everybody's
	PlaceKind m_placeKind;					///< a fight, a special power, or a sight picked while nothing was hit
	Real m_placeHeight;							///< how much higher than the watcher's own the place is watched from
	Real m_placePeak;								///< the hottest the fight held has been since the director came to it
	UnsignedInt m_placeEvent;				///< the id of the event the place is, while it is one
	UnsignedInt m_eventSecond;			///< another event landing off that one's picture at the same time, 0 for none; the window's power
	DirectorWindowKind m_windowKind;	///< what the corner window shows, kept while it slides out
	Bool m_windowOn;								///< the window is coming in or in, rather than going or gone
	UnsignedInt m_windowChanged;		///< the logic frame it last came up or went
	Coord2D m_windowSubject;				///< where its camera looks, kept while it goes
	PlayerMaskType m_windowSides;		///< who it shows, kept while it goes
	Real m_windowProgress;					///< how far in it is, 0 to 1, eased
	ViewLocation m_windowGlide;			///< where its camera's glide to its subject has got to
	ObserverCameraVelocity m_windowVelocity;
	ViewLocation m_windowView;			///< its camera for its draw
	FILE *m_filmEvents;							///< the events file, NULL until the filming pass opens it
	Bool m_filmEventsTried;					///< opened or failed once this run; a later match does not write over it
	std::vector< AsciiString > m_filmPending;	///< lines still to write: a power is noted from the logic, the file is written from update
	std::vector< ObjectID > m_filmBuilding;	///< the structures under construction at the last look
	std::vector< ObjectID > m_filmLost;			///< the dear things dead at the last look, already written
	Int m_filmRank[ MAX_PLAYER_COUNT ];			///< each player's rank at the last look, -1 before the first
	std::vector< Coord2D > m_seen;	///< the last few sights, oldest first, not gone back to while there is another
	std::vector< DirectorEvent > m_events;	///< the special powers still worth watching, oldest first
	UnsignedInt m_nextEventId;

	std::vector< DirectorShowing > m_flags[ MAX_PLAYER_COUNT ];	///< each player's special powers waiting to hang under his card, the first up now
	std::vector< DirectorShowing > m_defeatBanners;	///< the defeated players' banners, the first up now
	PlayerMaskType m_playedMask;										///< every player seen playing in this match
	UnsignedInt m_defeatFrame[ MAX_PLAYER_COUNT ];	///< the frame each player lost on, 0 while he has not
	UnsignedInt m_collapseFrame[ MAX_PLAYER_COUNT ];	///< the frame his card starts to collapse
	UnsignedInt m_lastCollapse;											///< the last card's, which the next one waits out
	UnsignedInt m_winnerFrame;											///< the frame the winner's banner starts to come in, 0 before the match is decided

	std::vector< DirectorHeat > m_fights;	///< the last scan's hits one player dealt another he is at war with; the second fight counts only these
	std::vector< PlayerMaskType > m_fightSides;	///< and the two players each of them was between
	std::vector< IRegion2D > m_broadcast;	///< the rectangles the broadcast drew over the main picture this frame
	Real m_screenGround;									///< the ground the whole picture spans across its middle row
	Bool m_split;										///< -directorrecord has a second fight off the picture for the corner window
	Real m_splitApart;							///< how far from the director's place the second fight had to be when it last came or went
	UnsignedInt m_splitChanged;			///< the logic frame the second fight last came or went
	Coord2D m_secondPlace;					///< the second fight

	ViewLocation m_aimFrom;							///< the angle, pitch and height the director's and the window's cameras are tried at this update
	Coord2D m_mainOffset;						///< how far the camera's look point sat from its subject last frame, on the ground
	ViewLocation m_firstView;				///< the camera's own place, put back after the window is drawn
	Int m_drawingPane;							///< 0, or OBSERVER_WINDOW_PANE while the window is drawn
};

extern ObserverCamera TheObserverCamera;

#endif
