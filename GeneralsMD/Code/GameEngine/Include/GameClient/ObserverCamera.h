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
// most, a laser followed along its sweep, and two landing far apart at once split a recording's
// picture a pane each.  Hits on infantry count for a quarter, so a skirmish of riflemen rarely
// takes the camera from tanks or buildings.  The camera glides between places on a spring and only cuts across
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
#include "GameClient/Color.h"
#include "GameClient/View.h"

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
	const SpecialPowerTemplate *power;	///< the power used; NULL for a warhead no power sent
	const ThingTemplate *sourceThing;		///< what it was fired from
};

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
/// moment of one frame fired from place at target, peak its weight against other powers; a power
/// that is not a superweapon has 0 until a hit is seen where it was aimed, so a spy satellite or a
/// radar scan stays at 0
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
/// the scouting pass's special powers on frame, each scan up to EVENT_FRAMES after its launch: one a
/// hit of hits lands within the gather radius of the target of has landed
void ObserverCamera_markLandings( std::vector< DirectorMoment > &moments, const std::vector< DirectorFightHit > &hits, UnsignedInt frame );
/// whether the special power fired on frame at at is one the scouting pass saw land; FALSE for one
/// it did not see, so a match it did not scout waits for the hits
Bool ObserverCamera_powerLands( const std::vector< DirectorMoment > &timeline, UnsignedInt frame, const Coord2D &at );
/// whether the timeline has a fight worth filming near place, going on at frame or beginning within
/// the pre-roll, that lasts to until and peaked at least leastPeak, leaving out any that is also near
/// besides, the place another pane already shows (NULL for none)
Bool ObserverCamera_fightLasts( const std::vector< DirectorMoment > &timeline, const Coord2D &place, UnsignedInt frame, UnsignedInt until,
	const Coord2D *besides, Real leastPeak = 0.0f );
/// the scouting pass's fights as its run ends on frame end: one still going was cut short by the end
/// of the match, not by itself, and is counted as lasting, so a last battle that got hot is filmed
/// rather than skipped as a fizzle
void ObserverCamera_closeTimeline( std::vector< DirectorMoment > &moments, UnsignedInt end );
/// the moment the director goes to wait at on frame: of the moments worth filming that begin within the
/// pre-roll after frame, a superweapon first, then any other special power, then the hottest fight,
/// leaving out any within apart of taken, the place another pane already shows (NULL for none).  A
/// power is waited for where it lands.  -1 for none
Int ObserverCamera_prerollMoment( const std::vector< DirectorMoment > &timeline, UnsignedInt frame, const Coord2D *taken, Real apart );
/// whether the fight going on at place on frame is one the scouting pass saw fizzle, which the
/// director does not cut to.  FALSE where the pass saw nothing, so a match it did not scout is
/// filmed as before
Bool ObserverCamera_fizzles( const std::vector< DirectorMoment > &timeline, const Coord2D &place, UnsignedInt frame );
/// a fight worth filming beginning within SPLIT_LEAD_FRAMES after frame, or with keeping, the split up
/// already, one going on at frame, more than apart from first: the hottest such, -1 for none.  A
/// fight already going is not split for anew: where it began is only a guess at where it is now, and
/// a fight that had wandered off left pane 1 on empty ground
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
/// the two halves of a split screen never show the same ground.  FALSE when nothing that far was hit
Bool ObserverCamera_secondPlace( const std::vector< DirectorHeat > &hits, const Coord2D &first, Real needed, Coord2D *place, Real *heat );
/// -directorrecord's split in a match of two, a pane each player: what one player is doing is the hits
/// his own fire made (sources[] has him), and with none anywhere his own sights, his base and his army
/// on the move.  fighting says which
void ObserverCamera_sideCandidates( const std::vector< DirectorHeat > &hits, const std::vector< PlayerMaskType > &sources,
	const std::vector< DirectorHeat > &sights, const std::vector< PlayerMaskType > &owners, PlayerMaskType side,
	std::vector< DirectorHeat > *candidates, Bool *fighting );
/// a place for each of the two players from his candidates, more than apart from each other.
/// Whichever player's hottest place is taken first, the other's is the hottest of his beyond it; of
/// the two ways round, the one whose cooler pane is hotter.  FALSE, both heats 0, when there are none
Bool ObserverCamera_sidePlaces( const std::vector< DirectorHeat > *candidates, Real apart, Coord2D *places, Real *heats );
/// whether the recording's picture is split, given whether it is now, how far apart the two places
/// are against the needed distance, and how long ago it last went on or off: on, after a rest, for a
/// second fight more than needed away that is big on its own and next to a first one; held through
/// SPLIT_LEAST_FRAMES whatever the second fight does, then while it keeps a quarter of the first; off
/// at once, however new, when the two places come within two thirds of needed and the two panes
/// would show the same ground
Bool ObserverCamera_holdSplit( Bool split, Real firstHeat, Real secondHeat, Real apart, Real needed, UnsignedInt framesSince );
/// the same in a match of two, a pane each player, with apart 0 when no place was found for either.
/// The opening's split goes on after SPLIT_REST_FRAMES whenever the two are further apart than needed
/// and stays through SPLIT_LEAST_FRAMES, then up to SPLIT_MOST_FRAMES while fighting has either player
/// in a fight and twice that while it has neither.  Past the opening it goes on only for big, each
/// player in a big fight of his own, and only SIDE_SPLIT_REST_FRAMES after the last one went off; it
/// stays through SIDE_SPLIT_LEAST_FRAMES whatever happens, then while both players fight, up to
/// SIDE_SPLIT_MOST_FRAMES
Bool ObserverCamera_holdSideSplit( Bool split, Bool opening, Bool big, Int fighting, Real apart, Real needed, UnsignedInt framesSince );

/// -directorrecord's panes.  The picture is cut by rays from one point, and pane i is the wedge from
/// ray i counterclockwise to ray i + 1.  Angles are degrees, 0 to the right and 90 up the screen
enum { OBSERVER_MOST_PANES = 8 };
/// the rays for count panes on a width by height picture, ascending from 0 to 360, so that every
/// pane's wedge of it is as big as every other's; the number of rays, which is count, or 0 for fewer
/// than two panes.  Pane 0 is the one the radar belongs to and the one left when the panes go
Int ObserverCamera_paneLayout( Int count, Real *rays, Int width, Int height );
/// the angle from the middle of a width by height picture at which a ray sweeping counterclockwise
/// from 0 has covered share of it, 0 to 1
Real ObserverCamera_angleForShare( Real share, Int width, Int height );
/// the pane pixel x, y falls in, the rays meeting at origin x, y (pixels, y down)
Int ObserverCamera_paneOf( Real x, Real y, Real originX, Real originY, const Real *rays, Int count );
/// a stretch of row y from column x0 up to, not including, x1 that lies in one pane
struct ObserverPaneRun
{
	Int y;
	Int x0;
	Int x1;
	Int pane;
};
/// a width by height picture as runs, row by row and left to right, every pixel in the pane
/// ObserverCamera_paneOf gives its centre
void ObserverCamera_paneRuns( Int width, Int height, Real originX, Real originY, const Real *rays, Int count,
	std::vector< ObserverPaneRun > &runs );
/// how far the rays' meeting point has to move, away from pane 0, before pane 0 is the whole of a
/// width by height picture: the panes come in from there and go back out to it
Real ObserverCamera_paneExit( const Real *rays, Int count, Int width, Int height );
/// the direction, in pixels with y down, that meeting point moves in to take the other panes away
Coord2D ObserverCamera_paneExitDirection( const Real *rays );
/// the largest circle that fits in each pane with the rays meeting in the middle of the picture, kept
/// off the screen's edges, the rays and the radar's frame, a rectangle radarHalf wide and high each
/// way from the middle, and below the top rows the broadcast's score bar takes.  Its centre is where
/// the pane's subject is put and its radius, in pixels, what the subject is fitted into; centres and
/// radii have count entries
void ObserverCamera_paneCircles( const Real *rays, Int count, Int width, Int height, const Coord2D &radarHalf, Real top,
	Coord2D *centres, Real *radii );
/// the top row of a label width by height pixels centred across a pane's circle as high as the circle
/// holds all of it, so it stays inside the pane and clear of its lines; the circle's middle row when
/// it is too wide for that
Real ObserverCamera_paneLabelTop( const Coord2D &centre, Real radius, Real width, Real height );
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
/// the order to take things in so the ones of one team sit together, the teams in the order they
/// first appear and each team's members in their own order
std::vector< Int > ObserverCamera_teamOrder( const std::vector< Int > &teams );
/// a player's colour lifted towards white until it reads on the broadcast's ground at 4.5 to 1;
/// a colour that already does comes back unchanged
Color ObserverCamera_readableColor( Color color );
/// whether a player's colour is close enough to the brand gold of the lines to need an edge
Bool ObserverCamera_nearBrandGold( Color color );
/// how far from around the farthest of the things within reach of it lie, on the ground, and never less
/// than least
Real ObserverCamera_extentAround( const std::vector< DirectorHeat > &things, const Coord2D &around, Real reach, Real least );
/// the zoom from nearest to farthest at which a pane's circle reaches reach on the ground, from the
/// reach measured at two zooms: the ground a picture covers grows in a straight line with the zoom,
/// which is how far the eye stands back
Real ObserverCamera_zoomForReach( Real zoomA, Real reachA, Real zoomB, Real reachB, Real reach, Real nearest, Real farthest );
/// the ground a pane spans across its middle row once its circle reaches reach: screenGround is what
/// the whole picture spans at the director's zoom and circleGround what the circle reaches there.
/// The pane zooms out from the director's zoom, never past PANE_FIT_FARTHEST times it, and the ground
/// grows with the zoom
Real ObserverCamera_paneGround( Real screenGround, Real circleGround, Real reach );
/// the corners of what pane shows of a width by height picture, the rays meeting at origin: its wedge
/// cut by the screen's edges, at most eight points; the number of them
Int ObserverCamera_panePolygon( const Real *rays, Int count, Int pane, const Coord2D &origin, Int width, Int height,
	Coord2D *vertices );
/// how far an animation of length frames that started on start is on frame, eased in and out, 0 to 1
Real ObserverCamera_easeFrames( Real frame, UnsignedInt start, UnsignedInt length );
/// the dark edge each side of the gold of a line between panes and of the radar's frame, in pixels
enum { OBSERVER_PANE_LINE_EDGE = 2 };
/// the gold of a line between panes, in pixels, for a picture height pixels high: 6 at 720, 9 at 1080
Int ObserverCamera_paneLineWidth( Int height );
/// the width of the soft band of the brand's blue under every line between panes and round the radar
Int ObserverCamera_paneBandWidth( Int height );
/// how much of its length a line between panes has grown out from the meeting point at progress, the
/// lines' draw on the settled panes, 0 to 1; whole before the draw is
Real ObserverCamera_lineDrawn( Real progress );
/// how far a line's band has faded in at progress, behind the line
Real ObserverCamera_bandShown( Real progress );
/// where along the gold lines the travelling light is on logic frame frame, 0 at the meeting point and
/// 1 at the far end, eased; below 0 between two runs
Real ObserverCamera_shimmerAt( Real frame );

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

	/// -directorrecord's panes: the players' bases for the first seconds of a match, then two fights
	/// far apart side by side.  Each pane past the first is drawn first, for the recording only and
	/// never presented, then the frame everybody sees; the recording joins them along the rays
	Bool isSplit( void ) const { return m_split; }
	/// how many panes the next frame is drawn in, 1 when the picture is whole
	Int getDrawnPaneCount( void ) const { return m_paneProgress > 0.0f ? m_paneCount : 1; }
	const Real *getPaneRays( void ) const { return m_paneRays; }
	/// where the rays meet on the screen, in pixels, which slides in from off the screen and back out
	Coord2D getPaneOrigin( void ) const { return m_paneOrigin; }
	/// how far the radar in the middle of the bottom edge is slid down off the screen, 0 to 1
	Real getCornerRadarSlide( void ) const { return m_cornerRadarSlide; }
	/// whether there are panes on the screen, which the radar is not drawn over
	Bool isRadarFramed( void ) const { return m_paneCount >= 2 && m_paneProgress > 0.0f; }
	/// the framed radar's rectangle as it was drawn, frame included: the recording takes it from pane 0
	void setRadarFrame( const IRegion2D &frame ) { m_radarFrame = frame; }
	const IRegion2D &getRadarFrame( void ) const { return m_radarFrame; }
	/// the broadcast drawn over pane 0, its score bar and its labels: the recording takes these
	/// rectangles from pane 0 whichever pane they lie over.  Cleared at the start of each of its draws
	void clearBroadcast( void ) { m_broadcast.clear(); }
	void addBroadcast( const IRegion2D &region ) { m_broadcast.push_back( region ); }
	const std::vector< IRegion2D > &getBroadcast( void ) const { return m_broadcast; }
	/// the rows the score bar takes at the top of the picture, which every pane's circle stays below
	void setBroadcastTop( Real rows ) { m_broadcastTop = rows; }
	/// a pane's circle round its subject where that is drawn this frame, the panes part way in or out
	void getPaneCircle( Int pane, Coord2D *centre, Real *radius ) const;
	/// how far in the panes are, 0 to 1
	Real getPaneProgress( void ) const { return m_paneProgress; }
	/// how far the gold lines have drawn out on the settled panes, 0 to 1, not eased
	Real getLineProgress( void ) const { return m_lineProgress; }
	/// who a split's pane shows: everybody dealing or taking hits in its fight, and in a match of two
	/// the pane's own player whatever he does, or for a pane a special power holds the player who used
	/// it; 0 when nobody is
	PlayerMaskType getPaneSides( Int pane ) const;
	/// the panes are the match's opening, each one player's: his index, and the point over his command
	/// centre his plate hangs from, on the ground where his things crowd when he has none
	Bool isIntro( void ) const { return m_intro; }
	Int getIntroPlayerIndex( Int pane ) const;
	const Coord3D &getIntroMark( Int pane ) const { return m_paneMark[ pane ]; }
	Bool isDrawingSecond( void ) const { return m_drawingPane != 0; }
	Int getDrawingPane( void ) const { return m_drawingPane; }
	/// the view moved to a pane's camera for one draw, and put back after it
	void beginPanePass( Int pane );
	void endPanePass( void );

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

	enum { NO_PLAYER = -1 };

private:
	void updateShroudViewer( void );
	void holdHeight( Bool hold );
	void driveHeight( Real extra );
	Bool takenByHand( const ViewLocation &current ) const;
	Coord2D keepInMap( const Coord2D &place, const ViewLocation &current ) const;
	void aimView( const Coord2D &look, Real zoom );
	Coord2D placeOnPixel( const Coord2D &subject, const Coord2D &pixel, Real zoom );
	Real pixelError( const Coord2D &subject, const Coord2D &pixel );
	Coord2D mapShift( Int pane, const Coord2D &origin, const Coord2D &look, Coord2D *worst, Real *past );
	Real groundReach( const Coord2D &subject, const Coord2D &centre, Real radius, Real zoom );
	Real zoomInMap( Int pane, const Coord2D &subject, Real nearest, Real wanted );
	Coord2D placePane( Int pane, const Coord2D &subject, const Coord2D &pixel, Real zoom );
	void logPanes( UnsignedInt frame ) const;
	void logHandover( const ViewLocation &step, const ViewLocation &placed );
	void pickIntroBases( void );
	Region2D mapRegion( void ) const;
	Bool updateSplit( void );
	Bool updateEventSplit( void );
	Bool updateSideSplit( void );
	/// everybody dealing or taking the last scan's hits in the fight at subject
	PlayerMaskType sidesAround( const Coord2D &subject ) const;
	void measureScreenGround( void );
	Real paneGround( const Coord2D &subject ) const;
	Real splitApart( const Coord2D &second ) const;
	void updateIntroPlaces( void );
	void advancePanes( UnsignedInt frame );
	void easePanes( Real at );
	void fitPanes( UnsignedInt frame );
	Real paneZoom( Int pane ) const;
	void stepPaneCameras( const ViewLocation &step, Real elapsedSeconds );
	Bool isShowingPlayerView( void ) const;
	Bool chooseTarget( const ViewLocation &current, ViewLocation *target );
	Bool directorPlace( const Player *narrowTo, Coord2D *place );
	DirectorEvent *findEvent( UnsignedInt id );
	void dropOldEvents( UnsignedInt frame );
	void loadTimeline( void );
	void checkTimeline( UnsignedInt frame );
	void noteFlag( const DirectorEvent &event );
	void updateBroadcastMoments( UnsignedInt frame );

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
	Int m_placeMoment;													///< the timeline's moment the director waits at, while the place is one
	Coord2D m_skippedFight;											///< the last fizzling fight the director passed over, so it is logged once

	ObserverCameraMode m_mode;
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
	Bool m_panesHeldZoom;						///< last frame's panes set the zoom outright
	Real m_lastZoom;								///< the view's zoom at the last update, for the zoom jump log
	Bool m_lastCut;									///< the last update cut
	UnsignedInt m_handoverLogUntil;	///< the frame the per-frame hand-over log stops at
	Coord2D m_paneLookLast[ 2 ];		///< each pane's subject at the last frame panes were up, for the jump check
	UnsignedInt m_paneLookFrame;		///< that frame, 0 while no panes are up
	Real m_paneLookStepMost[ 2 ];		///< each pane's largest subject step a logic frame while these panes are up
	Int m_paneSurvivor;							///< the pane that fills the screen as the panes go out: 1 when the split ended on the director taking pane 1's fight
	Int m_spentMoment;							///< the timeline moment whose split was ended early, so it does not open again
	Bool m_survivorHandover;				///< the panes have just gone out on pane 1, whose camera the single view takes over this update
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
	UnsignedInt m_eventSecond;			///< another event landing far from that one at the same time, 0 for none; the split's pane 1
	Bool m_eventSplit;							///< the split up is the two events', which no rest or hold of a fight's split keeps down
	PlayerMaskType m_eventPaneSides[ 2 ];	///< who used each of those two
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

	std::vector< DirectorHeat > m_fights;	///< the last scan's hits one player dealt another he is at war with; the split counts only these
	std::vector< PlayerMaskType > m_fightSides;	///< and the two players each of them was between
	std::vector< PlayerMaskType > m_fightSources;	///< and the player who dealt each of them
	std::vector< DirectorHeat > m_sights;	///< the last scan's things worth seeing, unhurt or not
	std::vector< PlayerMaskType > m_sightOwners;	///< and whose each of them is, 0 for nobody's
	const Player *m_sidePlayers[ 2 ];			///< a match of two: the opening's pane players, each split's pane 0 and pane 1 follow the same one
	Coord2D m_sideFirst;									///< where pane 0's player is, the place the director holds while that split is up
	Bool m_sideOpening;										///< the split up is the one the opening's panes went on as
	std::vector< IRegion2D > m_broadcast;	///< the rectangles the broadcast drew over pane 0 this frame
	Real m_broadcastTop;									///< the rows the score bar took
	Real m_screenGround;									///< the ground the whole picture spans across its middle row, last measured with no panes up
	Real m_circleGround;									///< and the least a split's pane circle reaches on the ground with it
	Bool m_split;										///< -directorrecord wants two fights side by side
	Real m_splitApart;							///< how far apart the split's two fights had to be when it last went on or off
	UnsignedInt m_splitChanged;			///< the logic frame the split last went on or off
	Coord2D m_secondPlace;					///< the second fight, shown in the second pane

	enum PanePhase { PANES_NONE, PANES_RADAR_OUT, PANES_IN, PANES_DRAW, PANES_HELD, PANES_UNDRAW, PANES_OUT, PANES_RADAR_IN };
	/// the panes all in, their lines drawing, drawn or going back
	Bool panesSettled( void ) const { return m_panePhase == PANES_DRAW || m_panePhase == PANES_HELD || m_panePhase == PANES_UNDRAW; }
	PanePhase m_panePhase;
	UnsignedInt m_panePhaseStart;		///< the logic frame the phase began on
	Bool m_intro;										///< the panes are the match's opening, one a player
	Bool m_introDone;
	Bool m_introGlide;								///< the opening has gone and the camera glides to the director's place, however far
	Coord2D m_panesLeftPlace;						///< pane 0's place while its panes are up, kept as they go
	Int m_paneCount;								///< 0 with no panes
	Real m_paneRays[ OBSERVER_MOST_PANES ];
	Real m_paneProgress;						///< 0 for no panes on the screen, 1 for all of them, eased
	Real m_lineProgress;						///< 0 for no gold on the lines, 1 for all of it, a straight share of the draw
	Real m_paneExit;								///< how far off the meeting point goes, in pixels
	Real m_paneBaseZoom;							///< the view's zoom when the panes started, which they rise from
	Coord2D m_paneCentres[ OBSERVER_MOST_PANES ];	///< where each pane's subject is put, the rays meeting in the middle
	Real m_paneRadii[ OBSERVER_MOST_PANES ];		///< how much of its pane each subject is fitted into, in pixels
	Real m_paneExtent[ OBSERVER_MOST_PANES ];		///< how far each pane's subject spreads on the ground
	Real m_paneFit[ OBSERVER_MOST_PANES ];			///< each pane's zoom with the panes all in, eased on logic frames
	Bool m_paneFitValid;							///< m_paneFit holds a fit of these panes
	UnsignedInt m_paneFitFrame;						///< the logic frame m_paneFit last moved on
	Coord2D m_radarHalf;							///< half the framed radar's size, taken from its last draw
	ViewLocation m_aimFrom;							///< the angle, pitch and height every pane camera is tried at this update
	Real m_paneShifted[ OBSERVER_MOST_PANES ];		///< how far the map moved each pane's look point off its subject last frame
	Real m_paneError[ OBSERVER_MOST_PANES ];		///< how far from its pixel the view drew each pane's subject last frame, before the map's shift
	Coord2D m_paneShown[ OBSERVER_MOST_PANES ];		///< the subject each pane was placed on last frame
	Coord2D m_paneWorst[ OBSERVER_MOST_PANES ];		///< the ground under each pane's vertex furthest past the map last frame
	Real m_paneOutside[ OBSERVER_MOST_PANES ];		///< how far past the map that vertex still was once the look point moved
	Coord2D m_paneOrigin;						///< where the rays meet now, in pixels
	Real m_cornerRadarSlide;
	IRegion2D m_radarFrame;
	const Player *m_panePlayers[ OBSERVER_MOST_PANES ];	///< the intro's player for each pane
	Coord2D m_paneSubject[ OBSERVER_MOST_PANES ];			///< what each pane past the first looks at
	Coord3D m_paneMark[ OBSERVER_MOST_PANES ];				///< the top of each opening player's command centre
	ViewLocation m_paneGlide[ OBSERVER_MOST_PANES ];		///< where each pane's subject glide has got to
	ObserverCameraVelocity m_paneVelocity[ OBSERVER_MOST_PANES ];
	ViewLocation m_paneView[ OBSERVER_MOST_PANES ];		///< each pane's camera for its draw
	Coord2D m_mainOffset;						///< how far the camera's look point sat from its subject last frame, on the ground
	ViewLocation m_firstView;				///< the camera's own place, put back after a pane is drawn
	Int m_drawingPane;
};

extern ObserverCamera TheObserverCamera;

#endif
