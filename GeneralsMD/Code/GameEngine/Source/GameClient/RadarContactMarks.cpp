// The Turkiye radar's contacts on the ground (RadarContactMarks.h): per contact a red ring with a
// bright centre pip lying on the terrain and two ping ripples running out of it, and where the
// sweep before saw something, a faint thin ring fading out.  A SIHA recon contact is drawn as
// recon_v1 has it instead: an amber ring on the ground, a leader line up to a steel chip with its
// class glyph where a health bar sits, a chevron for its heading when it moves; and every recon area
// gets a dashed green ring on the ground and a steel tray with its countdown.  The decals are the
// smoke-signal marks' kind (InGameUI::addSignalMark), so every renderer draws them through the shared
// decal path; the chips and trays are mapped images (Tools/recon_marks.py) drawn in the 2D pass.
// Client side: it reads the logic (the water, the bridges, the frame) and the drawables in update
// only, and writes nothing back.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/RadarContactMarks.h"

#include "Common/GameEngine.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Image.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/Shadow.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

namespace
{
	// ponytail: hard caps on the contacts drawn per sweep; a radar painting more than this is a
	// screen of red already, and the pools would want to grow without bound.  A recon area sees every
	// unit standing in it, a base and its army, so its chips get a cap of their own, taken first
	const size_t MAX_CONTACTS = 64;
	const size_t MAX_RECON_CONTACTS = 160;

	struct Sweep
	{
		UnsignedInt frame;
		std::vector< RadarContact > all;
		std::vector< RadarReconArea > areas;
	};

	Sweep s_sweeps[ 2 ];							///< [0] the last publication, [1] the one before
	Int s_publications = 0;						///< how many s_sweeps hold, 0..2

	/// a recon area's whole run, in frames from the first sweep that reported it to its end: the
	/// logic hands over only the end, and the countdown bar wants the start too
	struct AreaClock
	{
		Int player;
		UnsignedInt endFrame;
		UnsignedInt total;
	};
	std::vector< AreaClock > s_clocks;

	// what RadarContactMarks_update worked out this pass, for the minimap and the 2D draw to read
	// without touching the logic or a drawable while they may run beside the tick (logic thread)
	std::vector< RadarContact > s_fresh;
	std::vector< RadarContact > s_echoes;
	std::vector< RadarReconArea > s_areas;
	Real s_sweepAge = 0.0f;
	Bool s_drawHud = FALSE;				///< icons shown this pass (scripts, -cinema)

	/// a fresh contact the 2D pass draws something over: a recon chip, or a classified ring's glyph
	struct Chip
	{
		Coord3D pos;
		Coord3D ahead;		///< where the heading chevron points, for a recon contact that moved
		Bool mover;
		Bool recon;
		Bool ringLaid;		///< a red ring went down under it (onOpenGround), which a classified glyph sits beside
		UnsignedByte glyph;
		Real alpha;
	};
	std::vector< Chip > s_chips;

	/// a recon area as the 2D pass draws its tray
	struct Tray
	{
		Coord3D pos;
		Real radius;
		Int secondsLeft;	///< -1 for an area with no end: no time, no bar
		Real left;				///< 1 at the start of the run, 0 at its end
	};
	std::vector< Tray > s_trays;

	std::vector< Shadow * > s_contactDecals;	///< ReforgedRadarContact, one per fresh red contact
	std::vector< Shadow * > s_ringDecals;			///< ReforgedRadarRing, two ripples per fresh red contact and one per echo
	std::vector< Shadow * > s_reconDecals;		///< ReforgedRadarRing in amber, one per fresh recon contact
	std::vector< Shadow * > s_areaDecals;			///< ReforgedReconArea in green, one per recon area

	// the 2D pieces, found once the mapped images are loaded
	const Image *s_glyphImages[ RADAR_GLYPH_COUNT ] = { NULL };
	const Image *s_chipImage = NULL;
	const Image *s_trayImage = NULL;
	const Image *s_barImage = NULL;
	DisplayString *s_trayLabel = NULL;
	std::vector< DisplayString * > s_trayTimes;	///< one per tray drawn, kept so a time is laid out once a second
	Int s_trayPoints = 0;						///< the size the strings above were made at; a new interface scale makes them again

	/** The player's contacts in a sweep, less any whose unit is drawn now: a red ring left under a unit
		* he can see would only say what he sees already.  It asks the drawable, which GameClient::update's
		* walk has just set for this pass and for this same shown player, so the ring goes on the very
		* frame the unit appears (and stays off through the walk's two seconds of keeping a unit that
		* slipped back into the fog drawn).  Recon contacts go in first, under their own cap, so a
		* crowd of red never pushes a recon chip out. */
	void pickOut( const Sweep &sweep, Int player, std::vector< RadarContact > &out )
	{
		out.clear();
		for( Int recon = 1; recon >= 0; recon-- )
		{
			const size_t cap = recon ? MAX_RECON_CONTACTS : out.size() + MAX_CONTACTS;
			for( size_t index = 0; index < sweep.all.size() && out.size() < cap; index++ )
			{
				const RadarContact &contact = sweep.all[ index ];
				if( contact.player != player || (Int)contact.recon != recon )
					continue;
				const Object *obj = TheGameLogic->findObjectByID( contact.id );
				Drawable *draw = obj != NULL ? obj->getDrawable() : NULL;
				if( draw != NULL && !draw->getFullyObscuredByShroud() )
					continue;
				out.push_back( contact );
			}
		}
	}

	/// the shown player's lists, refilled every pass: what is drawn changes with the pass, not the sweep
	void refreshShown( UnsignedInt now )
	{
		const Int player = TheObserverCamera.getShroudPlayerIndex();
		s_fresh.clear();
		s_echoes.clear();
		s_areas.clear();
		if( s_publications > 0 )
		{
			pickOut( s_sweeps[ 0 ], player, s_fresh );
			const std::vector< RadarReconArea > &areas = s_sweeps[ 0 ].areas;
			for( size_t index = 0; index < areas.size(); index++ )
			{
				// an area ends between sweeps: it goes on its own frame, not the next sweep's.  An end
				// of 0 is an area with no end, which keeps its ring and goes without a countdown
				if( areas[ index ].player == player && ( areas[ index ].endFrame == 0 || areas[ index ].endFrame > now ) )
					s_areas.push_back( areas[ index ] );
			}
		}
		if( s_publications > 1 )
			pickOut( s_sweeps[ 1 ], player, s_echoes );
	}

	/// seconds since the last publication, by the logic's clock and the way into the next tick, so a
	/// ripple runs smooth at a frame rate over the logic's
	Real secondsSinceSweep( UnsignedInt now )
	{
		if( s_publications == 0 )
			return 0.0f;
		const UnsignedInt since = now > s_sweeps[ 0 ].frame ? now - s_sweeps[ 0 ].frame : 0;
		return ( (Real)since + GameEngine_logicTickFraction() ) / LOGICFRAMES_PER_SECOND;
	}

	/** Whether a mark at pos lies where the ground under it is drawn.  A decal without a render object
		* is laid on the terrain heightmap whatever its height, so over water it would sit on the seabed
		* and on a bridge under the deck; those contacts keep only their minimap pip. */
	Bool onOpenGround( const Coord3D &pos )
	{
		return !TheTerrainLogic->isUnderwater( pos.x, pos.y ) && TheTerrainLogic->getLayerForDestination( &pos ) == LAYER_GROUND;
	}

	Shadow *newDecal( const char *texture, UnsignedInt colour )
	{
		Shadow::ShadowTypeInfo decalInfo;
		decalInfo.allowUpdates = FALSE;
		decalInfo.allowWorldAlign = TRUE;		// wrapped over the terrain it lands on
		decalInfo.m_type = SHADOW_ALPHA_DECAL;
		strcpy( decalInfo.m_ShadowName, texture );
		decalInfo.m_sizeX = RadarContactLook::RingSize;
		decalInfo.m_sizeY = RadarContactLook::RingSize;
		decalInfo.m_offsetX = 0.0f;
		decalInfo.m_offsetY = 0.0f;
		Shadow *decal = TheProjectedShadowManager->addDecal( &decalInfo );
		decal->setColor( (Color)colour );
		decal->setOpacity( 0 );
		decal->enableShadowRender( FALSE );
		return decal;
	}

	/// the next decal from a pool, made when the pool has run out; a pool keeps one texture and one tint
	Shadow *take( std::vector< Shadow * > &pool, size_t &used, const char *texture, UnsignedInt colour )
	{
		if( used == pool.size() )
			pool.push_back( newDecal( texture, colour ) );
		return pool[ used++ ];
	}

	void place( Shadow *decal, const Coord3D &pos, Real size, Real alpha )
	{
		const Int opacity = REAL_TO_INT( clamp( 0.0f, alpha, 1.0f ) * 255.0f );
		decal->setPosition( pos.x, pos.y, pos.z );
		decal->setSize( size, size );
		decal->setOpacity( opacity );
		decal->enableShadowRender( opacity > 0 );
	}

	void hideFrom( std::vector< Shadow * > &pool, size_t used )
	{
		for( size_t index = used; index < pool.size(); index++ )
		{
			pool[ index ]->setOpacity( 0 );
			pool[ index ]->enableShadowRender( FALSE );
		}
	}

	void releaseAll( std::vector< Shadow * > &pool )
	{
		for( size_t index = 0; index < pool.size(); index++ )
			pool[ index ]->release();
		pool.clear();
	}

	/// the same unit in the sweep before, for its heading; NULL when that sweep did not see it
	const RadarContact *lastSeen( const RadarContact &contact )
	{
		if( s_publications < 2 )
			return NULL;
		const std::vector< RadarContact > &all = s_sweeps[ 1 ].all;
		for( size_t index = 0; index < all.size(); index++ )
		{
			if( all[ index ].id == contact.id && all[ index ].player == contact.player )
				return &all[ index ];
		}
		return NULL;
	}

	/// the chips' draw list: alpha by the sweep, a heading for a recon contact that moved since the last
	void fillChips( Real alpha )
	{
		using namespace RadarContactLook;
		s_chips.clear();
		for( size_t index = 0; index < s_fresh.size(); index++ )
		{
			const RadarContact &contact = s_fresh[ index ];
			if( !contact.recon && contact.glyph == RADAR_GLYPH_NONE )
				continue;
			Chip chip;
			chip.pos = contact.pos;
			chip.ahead = contact.pos;
			chip.mover = FALSE;
			chip.recon = contact.recon;
			chip.ringLaid = !contact.recon && onOpenGround( contact.pos );
			chip.glyph = contact.glyph;
			chip.alpha = alpha;
			const RadarContact *last = contact.recon ? lastSeen( contact ) : NULL;
			if( last != NULL )
			{
				const Real dx = contact.pos.x - last->pos.x;
				const Real dy = contact.pos.y - last->pos.y;
				const Real length = sqrtf( dx * dx + dy * dy );
				if( length > MoverDistance )
				{
					chip.mover = TRUE;
					chip.ahead.x += dx / length * ChevronAhead;
					chip.ahead.y += dy / length * ChevronAhead;
				}
			}
			s_chips.push_back( chip );
		}
	}

	/// the trays' draw list, from the areas still running at now
	void fillTrays( UnsignedInt now )
	{
		s_trays.clear();
		for( size_t index = 0; index < s_areas.size(); index++ )
		{
			const RadarReconArea &area = s_areas[ index ];
			Tray tray;
			tray.pos = area.pos;
			tray.radius = area.radius;
			tray.secondsLeft = -1;
			tray.left = 1.0f;
			if( area.endFrame != 0 )
			{
				UnsignedInt total = 0;
				for( size_t clock = 0; clock < s_clocks.size(); clock++ )
				{
					if( s_clocks[ clock ].player == area.player && s_clocks[ clock ].endFrame == area.endFrame )
						total = s_clocks[ clock ].total;
				}
				const UnsignedInt framesLeft = area.endFrame - now;
				tray.secondsLeft = (Int)( ( framesLeft + LOGICFRAMES_PER_SECOND - 1 ) / LOGICFRAMES_PER_SECOND );
				tray.left = clamp( 0.0f, (Real)framesLeft / (Real)total, 1.0f );
			}
			s_trays.push_back( tray );
		}
	}

	void findImages( void )
	{
		if( s_chipImage != NULL )
			return;
		static const char *const glyphNames[ RADAR_GLYPH_COUNT ] =
		{
			NULL, "ReforgedReconGlyphInfantry", "ReforgedReconGlyphVehicle", "ReforgedReconGlyphAntiAir",
			"ReforgedReconGlyphArtillery", "ReforgedReconGlyphAircraft", "ReforgedReconGlyphDefence"
		};
		for( Int glyph = RADAR_GLYPH_INFANTRY; glyph < RADAR_GLYPH_COUNT; glyph++ )
			s_glyphImages[ glyph ] = TheMappedImageCollection->findImageByName( glyphNames[ glyph ] );
		s_chipImage = TheMappedImageCollection->findImageByName( "ReforgedReconChip" );
		s_trayImage = TheMappedImageCollection->findImageByName( "ReforgedReconTray" );
		s_barImage = TheMappedImageCollection->findImageByName( "ReforgedReconBar" );
	}

	DisplayString *newTrayString( void )
	{
		DisplayString *string = TheDisplayStringManager->newDisplayString();
		string->setFont( TheFontLibrary->getFont( "Arial", s_trayPoints, TRUE ) );
		return string;
	}

	void freeTrayStrings( void )
	{
		if( s_trayLabel != NULL )
			TheDisplayStringManager->freeDisplayString( s_trayLabel );
		s_trayLabel = NULL;
		for( size_t index = 0; index < s_trayTimes.size(); index++ )
			TheDisplayStringManager->freeDisplayString( s_trayTimes[ index ] );
		s_trayTimes.clear();
	}

	/// the tray's lettering at this pass's interface scale, with the tray it sits in: made again
	/// whenever the resolution or the scale has moved it
	void sizeTrayStrings( Real px )
	{
		const Int points = max( 1, REAL_TO_INT( RadarContactLook::TrayPixels * px ) );
		if( points == s_trayPoints && s_trayLabel != NULL )
			return;
		freeTrayStrings();
		s_trayPoints = points;
		s_trayLabel = newTrayString();
		s_trayLabel->setText( TheGameText->fetch( "GUI:HudReconSiha" ) );
	}

	Color withAlpha( UnsignedInt colour, Real alpha )
	{
		return (Color)( ( colour & 0x00FFFFFF ) | ( (UnsignedInt)REAL_TO_INT( clamp( 0.0f, alpha, 1.0f ) * 255.0f ) << 24 ) );
	}

	/// a stroke over a dark keyline, so it reads on sand and in shadow alike
	void strokeKeylined( Int x0, Int y0, Int x1, Int y1, Real width, Color colour, Real alpha )
	{
		TheDisplay->drawLine( x0, y0, x1, y1, width + 2.0f, withAlpha( 0xFF080A08, 0.65f * alpha ) );
		TheDisplay->drawLine( x0, y0, x1, y1, width, colour );
	}

	void drawImageCentred( const Image *image, Int x, Int y, Int width, Int height, Color colour )
	{
		const Int left = x - width / 2;
		const Int top = y - height / 2;
		TheDisplay->drawImage( image, left, top, left + width, top + height, colour );
	}

	/** A recon contact: the leader line from its ground ring up to the chip, the chevron, the chip with
		* its glyph.  A classified red contact: its glyph beside the ring, up and to the right. */
	void drawChip( const Chip &chip, Real px )
	{
		using namespace RadarContactLook;
		ICoord2D ground;
		if( !TheTacticalView->worldToScreen( &chip.pos, &ground ) )
			return;
		const Image *glyph = s_glyphImages[ chip.glyph ];
		if( !chip.recon )
		{
			// beside the ring that went down, at the ring's own size on the screen: by its projected
			// radius, so it stays off the ring zoomed in and does not drift away zoomed out
			if( !chip.ringLaid )
				return;
			Coord3D rim = chip.pos;
			rim.x += RingSize * 0.5f;
			ICoord2D edge;
			if( TheTacticalView->worldToScreenTriReturn( &rim, &edge ) == View::WTS_INVALID )
				return;
			const Real ex = (Real)( edge.x - ground.x );
			const Real ey = (Real)( edge.y - ground.y );
			const Int size = REAL_TO_INT( 14.0f * px );
			const Int reach = REAL_TO_INT( 0.71f * sqrtf( ex * ex + ey * ey ) ) + size / 2;
			drawImageCentred( glyph, ground.x + reach, ground.y - reach, size, size, withAlpha( Colour, chip.alpha ) );
			return;
		}

		const Color amber = withAlpha( Amber, chip.alpha );
		Coord3D up = chip.pos;
		up.z += ChipHeight;
		ICoord2D top;
		if( TheTacticalView->worldToScreenTriReturn( &up, &top ) == View::WTS_INVALID )
			return;
		const Int chipW = REAL_TO_INT( 32.0f * px );
		const Int chipH = REAL_TO_INT( 24.0f * px );
		strokeKeylined( ground.x, ground.y - REAL_TO_INT( 3.0f * px ), top.x, top.y + chipH / 2, 1.0f,
			withAlpha( Amber, 0.7f * chip.alpha ), chip.alpha );

		ICoord2D tip;
		if( chip.mover && TheTacticalView->worldToScreenTriReturn( &chip.ahead, &tip ) != View::WTS_INVALID )
		{
			// the chevron's arms run back from its tip along the heading as the screen sees it
			Real dx = (Real)( tip.x - ground.x );
			Real dy = (Real)( tip.y - ground.y );
			const Real length = sqrtf( dx * dx + dy * dy );
			if( length > 1.0f )
			{
				dx /= length;
				dy /= length;
				const Real back = 5.0f * px;
				const Real side = 4.0f * px;
				const Int ax = tip.x - REAL_TO_INT( dx * back - dy * side );
				const Int ay = tip.y - REAL_TO_INT( dy * back + dx * side );
				const Int bx = tip.x - REAL_TO_INT( dx * back + dy * side );
				const Int by = tip.y - REAL_TO_INT( dy * back - dx * side );
				strokeKeylined( ax, ay, tip.x, tip.y, 2.0f, amber, chip.alpha );
				strokeKeylined( tip.x, tip.y, bx, by, 2.0f, amber, chip.alpha );
			}
		}

		drawImageCentred( s_chipImage, top.x, top.y, chipW, chipH, withAlpha( 0xFFFFFFFF, chip.alpha ) );
		if( glyph != NULL )
		{
			const Int size = REAL_TO_INT( 20.0f * px );
			drawImageCentred( glyph, top.x, top.y, size, size, amber );
		}
	}

	/** A recon area's tray, centred under the lowest point of its ring on the screen: the label, the
		* time left and the bar running down.  The point is the lowest of the whole ring as projected,
		* in the frame or not, so the tray does not jump as ring points leave the frame; the tray is then
		* kept inside the tactical view and above the command bar (bottom), so an area half off the
		* screen still shows its time. */
	void drawTray( const Tray &tray, size_t index, Real px, Int bottom )
	{
		using namespace RadarContactLook;
		ICoord2D lowest = { 0, 0 };
		Bool found = FALSE;
		const Int points = 16;
		for( Int point = 0; point < points; point++ )
		{
			const Real angle = 2.0f * PI * point / points;
			Coord3D edge = tray.pos;
			edge.x += Cos( angle ) * tray.radius;
			edge.y += Sin( angle ) * tray.radius;
			ICoord2D screen;
			if( TheTacticalView->worldToScreenTriReturn( &edge, &screen ) != View::WTS_INVALID && ( !found || screen.y > lowest.y ) )
			{
				lowest = screen;
				found = TRUE;
			}
		}
		if( !found )
			return;

		const Int width = REAL_TO_INT( 132.0f * px );
		const Int height = REAL_TO_INT( 32.0f * px );
		Int viewX, viewY;
		TheTacticalView->getOrigin( &viewX, &viewY );
		const Int viewRight = viewX + TheTacticalView->getWidth();
		const Int viewBottom = min( viewY + TheTacticalView->getHeight(), bottom );
		const Int x = clamp( viewX, lowest.x - width / 2, viewRight - width );
		const Int y = clamp( viewY, lowest.y + REAL_TO_INT( 6.0f * px ), viewBottom - height );
		TheDisplay->drawImage( s_trayImage, x, y, x + width, y + height );

		const Color drop = GameMakeColor( 0, 0, 0, 255 );
		s_trayLabel->draw( x + REAL_TO_INT( 7.0f * px ), y + REAL_TO_INT( 4.0f * px ), (Color)AreaGreen, drop );
		if( tray.secondsLeft < 0 )
			return;		// an area with no end: no time to show

		const Int barLeft = x + REAL_TO_INT( 8.0f * px );
		const Int barRun = REAL_TO_INT( 116.0f * px * tray.left );
		if( barRun > 0 )
			TheDisplay->drawImage( s_barImage, barLeft, y + REAL_TO_INT( 22.0f * px ), barLeft + barRun, y + REAL_TO_INT( 26.0f * px ) );

		while( s_trayTimes.size() <= index )
			s_trayTimes.push_back( newTrayString() );
		DisplayString *time = s_trayTimes[ index ];
		UnicodeString text;
		text.format( u"%d:%02d", tray.secondsLeft / 60, tray.secondsLeft % 60 );
		if( time->getText().compare( text ) != 0 )
			time->setText( text );
		time->draw( x + REAL_TO_INT( 125.0f * px ) - time->getWidth(), y + REAL_TO_INT( 4.0f * px ), (Color)TrayText, drop );
	}
}

void RadarContactMarks_publish( UnsignedInt sweepFrame, const std::vector<RadarContact> &contacts, const std::vector<RadarReconArea> &areas )
{
	std::swap( s_sweeps[ 0 ], s_sweeps[ 1 ] );
	s_sweeps[ 0 ].frame = sweepFrame;
	s_sweeps[ 0 ].all = contacts;
	s_sweeps[ 0 ].areas = areas;
	s_publications = min( s_publications + 1, 2 );

	// a clock per area, started by the first sweep that names it and dropped once it has run out
	for( size_t index = 0; index < s_clocks.size(); )
	{
		if( s_clocks[ index ].endFrame <= sweepFrame )
			s_clocks.erase( s_clocks.begin() + index );
		else
			index++;
	}
	for( size_t index = 0; index < areas.size(); index++ )
	{
		const RadarReconArea &area = areas[ index ];
		Bool known = FALSE;
		for( size_t clock = 0; clock < s_clocks.size(); clock++ )
			known = known || ( s_clocks[ clock ].player == area.player && s_clocks[ clock ].endFrame == area.endFrame );
		if( !known && area.endFrame > sweepFrame )
		{
			AreaClock clock;
			clock.player = area.player;
			clock.endFrame = area.endFrame;
			clock.total = area.endFrame - sweepFrame;
			s_clocks.push_back( clock );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Lays this frame's marks.  Ripples leave the ring at the sweep and run out to RippleTo, fading;
	* the ring holds through its sweep and fades over the next if no sweep follows, so a radar that
	* stops publishing does not leave its last paint on the ground.  A recon contact's amber ring and
	* chip hold and fade the same way.  Hidden while scripts or -cinema hide the icons, like the
	* signal marks. */
//-------------------------------------------------------------------------------------------------
void RadarContactMarks_update( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	refreshShown( now );
	const Real seconds = secondsSinceSweep( now );
	const Real age = seconds / RadarContactLook::SweepSeconds;
	s_sweepAge = min( age, 1.0f );
	s_drawHud = TheGameLogic->getDrawIconUI() && !CinemaDirector_hidesHud();
	const Real ringAlpha = RadarContactLook::RingAlpha * clamp( 0.0f, 2.0f - age, 1.0f );
	fillChips( ringAlpha );
	fillTrays( now );

	// no decal manager without a renderer (headless): nothing to lay
	if( TheProjectedShadowManager == NULL )
		return;

	size_t contactsUsed = 0;
	size_t ringsUsed = 0;
	size_t reconUsed = 0;
	size_t areasUsed = 0;
	if( s_drawHud )
	{
		using namespace RadarContactLook;
		// the area ring lies where it lands, over water or under a bridge too: it spans far more than
		// the ground at its centre, and a ring that vanished with its centre would hide the area whole
		for( size_t index = 0; index < s_areas.size(); index++ )
			place( take( s_areaDecals, areasUsed, "ReforgedReconArea", AreaGreen ), s_areas[ index ].pos,
				2.0f * s_areas[ index ].radius * AreaDecalSpan, AreaAlpha );
		for( size_t index = 0; index < s_fresh.size(); index++ )
		{
			const Coord3D &pos = s_fresh[ index ].pos;
			if( !onOpenGround( pos ) )
				continue;
			if( s_fresh[ index ].recon )
			{
				place( take( s_reconDecals, reconUsed, "ReforgedRadarRing", Amber ), pos, ReconRingSize, ringAlpha );
				continue;
			}
			place( take( s_contactDecals, contactsUsed, "ReforgedRadarContact", Colour ), pos, RingSize, ringAlpha );
			for( Int ripple = 0; ripple < 2; ripple++ )
			{
				// a square-root fade keeps a ripple seen until it is nearly out, as the approved still has it
				const Real t = ( seconds - ripple * RippleGap ) / RippleSeconds;
				const Bool running = t >= 0.0f && t < 1.0f;
				place( take( s_ringDecals, ringsUsed, "ReforgedRadarRing", Colour ), pos,
					RippleFrom + ( RippleTo - RippleFrom ) * clamp( 0.0f, t, 1.0f ), running ? RippleAlpha * sqrtf( 1.0f - t ) : 0.0f );
			}
		}
		// a recon contact leaves no echo: the area sees it again next sweep, and its chip says where it went
		const Real echoAlpha = EchoAlpha * ( 1.0f - s_sweepAge );
		for( size_t index = 0; index < s_echoes.size(); index++ )
		{
			if( !s_echoes[ index ].recon && onOpenGround( s_echoes[ index ].pos ) )
				place( take( s_ringDecals, ringsUsed, "ReforgedRadarRing", Colour ), s_echoes[ index ].pos, RingSize, echoAlpha );
		}
	}
	hideFrom( s_contactDecals, contactsUsed );
	hideFrom( s_ringDecals, ringsUsed );
	hideFrom( s_reconDecals, reconUsed );
	hideFrom( s_areaDecals, areasUsed );
}

//-------------------------------------------------------------------------------------------------
/** The 2D half: the chips and glyphs over the contacts, then the trays.  Sizes are recon_v1's at
	* 1920x1080, scaled with the rest of the interface.  A tray stays above bottom, the command bar's
	* top on the screen. */
//-------------------------------------------------------------------------------------------------
void RadarContactMarks_draw( Int bottom )
{
	if( !s_drawHud || ( s_chips.empty() && s_trays.empty() ) )
		return;
	findImages();
	const Real px = TheUIScale() / 1.8f;	// TheUIScale is over 800x600, and 1080 lines are 1.8 of that

	for( size_t index = 0; index < s_chips.size(); index++ )
		drawChip( s_chips[ index ], px );

	if( s_trays.empty() )
		return;
	sizeTrayStrings( px );
	for( size_t index = 0; index < s_trays.size(); index++ )
		drawTray( s_trays[ index ], index, px, bottom );
}

void RadarContactMarks_reset( void )
{
	releaseAll( s_contactDecals );
	releaseAll( s_ringDecals );
	releaseAll( s_reconDecals );
	releaseAll( s_areaDecals );
	s_sweeps[ 0 ].all.clear();
	s_sweeps[ 1 ].all.clear();
	s_sweeps[ 0 ].areas.clear();
	s_sweeps[ 1 ].areas.clear();
	s_publications = 0;
	s_clocks.clear();
	s_fresh.clear();
	s_echoes.clear();
	s_areas.clear();
	s_chips.clear();
	s_trays.clear();
	s_sweepAge = 0.0f;
	s_drawHud = FALSE;
	freeTrayStrings();
	s_trayPoints = 0;
}

const std::vector<RadarContact> &RadarContactMarks_fresh( void )
{
	return s_fresh;
}

const std::vector<RadarContact> &RadarContactMarks_echoes( void )
{
	return s_echoes;
}

const std::vector<RadarReconArea> &RadarContactMarks_areas( void )
{
	return s_areas;
}

Real RadarContactMarks_sweepAge( void )
{
	return s_sweepAge;
}

ObjectID RadarContactMarks_pick( const Coord3D &pos )
{
	const Int player = ThePlayerList->getLocalPlayer()->getPlayerIndex();
	ObjectID best = INVALID_ID;
	Real bestSquared = RadarContactLook::PickRadius * RadarContactLook::PickRadius;
	if( s_publications == 0 )
		return best;
	const std::vector< RadarContact > &all = s_sweeps[ 0 ].all;
	for( size_t index = 0; index < all.size(); index++ )
	{
		if( all[ index ].player != player )
			continue;
		const Real dx = all[ index ].pos.x - pos.x;
		const Real dy = all[ index ].pos.y - pos.y;
		const Real squared = dx * dx + dy * dy;
		if( squared <= bestSquared )
		{
			bestSquared = squared;
			best = all[ index ].id;
		}
	}
	return best;
}
