// The airlift panel (AirliftPanel.h).  A steel plate just above the spot the power was aimed at, with
// a pointer down to a gold mark on it, following the spot as the camera moves, drawn the way the group
// panel draws its pieces: a title with who the load is for, one row a unit type the power
// carries (its picture, name and price, then - count +), the planes, the fee and the total, and OK
// and Cancel.  A type the player could not build right now (no factory, a missing prerequisite, its
// cap) is greyed and its count kept at 0; + stops where the order would need more planes than the
// power flies; OK is dark while the total is more than the bank.  The rules are the logic's own
// (OCLSpecialPower::canAirlift, checkAirlift, findAirliftRecipient), asked again every frame.
//
// Thread: LOGIC-THREAD.md.  AirliftPanel_draw runs from InGameUI::postDraw at the render's sync
// point and the clicks on the input path, which runs there too.  The logic hears of it only through
// the one message OK sends.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/AirliftPanel.h"

#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/Keyboard.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/OCLSpecialPower.h"

#include <stdio.h>
#include <vector>

namespace
{
	// lengths at 1080, put through the screen's height
	const Real PANEL_W		= 330.0f;
	const Real PAD				= 8.0f;
	const Real TITLE_H		= 26.0f;
	const Real ROW_H			= 36.0f;
	const Real PIC_W			= 44.0f;
	const Real PIC_H			= 32.0f;
	const Real KEY				= 24.0f;	///< the - and + buttons, square
	const Real COUNT_W		= 30.0f;
	const Real LINE_H			= 20.0f;
	const Real BUTTON_W		= 96.0f;
	const Real BUTTON_H		= 28.0f;
	const Real POINTER_H	= 22.0f;	///< the pointer from the panel's foot down to the spot
	const Real POINTER_W	= 20.0f;	///< its base
	const Real MARK				= 10.0f;	///< the diamond on the ground, half its width
	const Real TITLE_POINTS	= 10.0f;
	const Real TEXT_POINTS	= 9.0f;

	enum { MAX_ROWS = 12 };

	const Color STEEL			= GameMakeColor( 58, 62, 66, 235 );
	const Color STEEL_LIT	= GameMakeColor( 112, 118, 122, 255 );
	const Color KEYLINE		= GameMakeColor( 16, 18, 20, 255 );
	const Color WELL			= GameMakeColor( 26, 28, 30, 235 );
	const Color KEY_FACE	= GameMakeColor( 86, 92, 96, 255 );
	const Color KEY_DEAD	= GameMakeColor( 44, 46, 48, 255 );
	const Color TEXT			= GameMakeColor( 232, 232, 226, 255 );
	const Color TEXT_DEAD	= GameMakeColor( 120, 120, 116, 255 );
	const Color GOLD			= GameMakeColor( 236, 204, 92, 255 );
	const Color RED				= GameMakeColor( 226, 76, 60, 255 );
	const Color SHADOW		= GameMakeColor( 0, 0, 0, 255 );

	struct Row
	{
		const ThingTemplate *thing;
		Int count;
		Bool can;			///< the player could build one now
		Bool canMore;	///< + would still fit
		IRegion2D minus;
		IRegion2D plus;
	};

	Bool s_open = FALSE;
	ObjectID s_source = INVALID_ID;
	const SpecialPowerTemplate *s_power = NULL;
	Coord3D s_target;
	std::vector< Row > s_rows;
	IRegion2D s_box;
	IRegion2D s_ok;
	IRegion2D s_cancel;
	Bool s_okEnabled = FALSE;
	Int s_planes = 0;
	Int s_cost = 0;
	Int s_recipient = -1;
	Bool s_drawn = FALSE;		///< the boxes are from a draw since the panel opened

	/// 0 title, 1 for whom, 2 "-", 3 "+", 4 planes, 5 total, 6 OK, 7 Cancel, then name, price, count a row
	enum { ROW_STRINGS = 8 };
	DisplayString *s_strings[ ROW_STRINGS + 3 * MAX_ROWS ];
}

static Int px( Real at1080 )
{
	return max( REAL_TO_INT( at1080 * TheDisplay->getHeight() / 1080.0f + 0.5f ), 1 );
}

static DisplayString *keptString( Int which, const UnicodeString &text, Real points )
{
	DisplayString *&string = s_strings[ which ];
	if( string == NULL )
		string = TheDisplayStringManager->newDisplayString();
	GameFont *font = TheFontLibrary->getFont( "Arial", max( px( points ), 6 ), TRUE );
	if( string->getFont() != font )
		string->setFont( font );
	if( string->getText().compare( text ) != 0 )
		string->setText( text );
	return string;
}

static Bool inside( const ICoord2D *at, const IRegion2D &box )
{
	return at->x >= box.lo.x && at->x < box.hi.x && at->y >= box.lo.y && at->y < box.hi.y;
}

static void setBox( IRegion2D *box, Int x, Int y, Int w, Int h )
{
	box->lo.x = x;
	box->lo.y = y;
	box->hi.x = x + w;
	box->hi.y = y + h;
}

static void drawSteel( const IRegion2D &r, Color face )
{
	const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
	if( w <= 2 || h <= 2 )
		return;
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, w, h, face );
	TheDisplay->drawFillRect( r.lo.x + 1, r.lo.y + 1, w - 2, 1, STEEL_LIT );
	TheDisplay->drawOpenRect( r.lo.x, r.lo.y, w, h, 1.0f, KEYLINE );
}

/// text at x, centred on y; `right` puts its right edge at x instead
static void drawText( DisplayString *s, Int x, Int y, Color color, Bool right = FALSE )
{
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( right ? x - w : x, y - h / 2, color, SHADOW );
}

static void drawTextCentred( DisplayString *s, const IRegion2D &r, Color color )
{
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( ( r.lo.x + r.hi.x - w ) / 2, ( r.lo.y + r.hi.y - h ) / 2, color, SHADOW );
}

//-------------------------------------------------------------------------------------------------
/** The power behind the panel, or NULL once it is gone: the source dead or handed over, the power
	* fired from elsewhere, the match over. */
//-------------------------------------------------------------------------------------------------
static OCLSpecialPower *airlift( Player **player )
{
	*player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( !s_open || TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || *player == NULL )
		return NULL;
	Object *source = TheGameLogic->findObjectByID( s_source );
	if( source == NULL || source->isEffectivelyDead() || source->getControllingPlayer() != *player )
		return NULL;
	OCLSpecialPower *sp = OCLSpecialPower::findAirlift( source, s_power );
	if( sp == NULL || !sp->isReady() )
		return NULL;
	return sp;
}

static OCLSpecialPower::AirliftOrder orderOf( Int bump = -1 )
{
	OCLSpecialPower::AirliftOrder order;
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		const Int count = s_rows[ i ].count + ( (Int)i == bump ? 1 : 0 );
		if( count <= 0 )
			continue;
		OCLSpecialPower::AirliftLine line;
		line.thing = s_rows[ i ].thing;
		line.count = count;
		order.push_back( line );
	}
	return order;
}

/// the rows' availability and the order's planes, cost and recipient, from the logic as it stands
static void refresh( OCLSpecialPower *sp, Player *player )
{
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		Row &row = s_rows[ i ];
		row.can = sp->canAirlift( player, row.thing, 1 );
		if( !row.can || !sp->canAirlift( player, row.thing, max( row.count, 1 ) ) )
			row.count = 0;
	}

	OCLSpecialPower::AirliftOrder order = orderOf();
	const OCLSpecialPower::AirliftCheck check = sp->checkAirlift( player, order, &s_planes, &s_cost, NULL );
	s_okEnabled = check == OCLSpecialPower::AIRLIFT_OK;
	Player *recipient = sp->findAirliftRecipient( &s_target );
	s_recipient = recipient ? recipient->getPlayerIndex() : -1;

	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		Row &row = s_rows[ i ];
		Int planes = 0, cost = 0;
		const OCLSpecialPower::AirliftCheck more = sp->checkAirlift( player, orderOf( (Int)i ), &planes, &cost, NULL );
		row.canMore = row.can && more != OCLSpecialPower::AIRLIFT_TOO_MANY_PLANES && more != OCLSpecialPower::AIRLIFT_NOT_BUILDABLE;
	}
}

//-------------------------------------------------------------------------------------------------
void AirliftPanel_open( const Object *source, const SpecialPowerTemplate *power, const Coord3D *target )
{
	if( source == NULL || power == NULL || target == NULL )
		return;
	OCLSpecialPower *sp = OCLSpecialPower::findAirlift( const_cast< Object * >( source ), power );
	if( sp == NULL )
		return;

	s_open = TRUE;
	s_drawn = FALSE;
	s_source = source->getID();
	s_power = power;
	s_target = *target;
	s_rows.clear();
	const std::vector< AsciiString > &units = sp->getAirliftData()->m_airliftUnits;
	for( std::vector< AsciiString >::const_iterator it = units.begin(); it != units.end() && s_rows.size() < MAX_ROWS; ++it )
	{
		const ThingTemplate *thing = TheThingFactory->findTemplate( *it );
		if( thing == NULL )
			continue;
		Row row;
		row.thing = thing;
		row.count = 0;
		row.can = FALSE;
		row.canMore = FALSE;
		row.minus.lo.x = row.minus.lo.y = row.minus.hi.x = row.minus.hi.y = 0;
		row.plus = row.minus;
		s_rows.push_back( row );
	}
}

//-------------------------------------------------------------------------------------------------
Bool AirliftPanel_openForLocalPlayer( const Coord3D *target )
{
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( player == NULL || TheGameLogic == NULL )
		return FALSE;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->getControllingPlayer() != player || obj->isEffectivelyDead() )
			continue;
		for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerModuleInterface *sp = (*m)->getSpecialPower();
			if( sp && OCLSpecialPower::findAirlift( obj, sp->getSpecialPowerTemplate() ) )
			{
				AirliftPanel_open( obj, sp->getSpecialPowerTemplate(), target );
				return TRUE;
			}
		}
	}
	return FALSE;
}

void AirliftPanel_close( void )
{
	s_open = FALSE;
	s_drawn = FALSE;
	s_rows.clear();
}

Bool AirliftPanel_isOpen( void )
{
	return s_open;
}

//-------------------------------------------------------------------------------------------------
void AirliftPanel_draw( void )
{
	Player *player = NULL;
	OCLSpecialPower *sp = airlift( &player );
	if( sp == NULL )
	{
		if( s_open )
			AirliftPanel_close();
		return;
	}
	refresh( sp, player );

	// just above the spot, with a pointer down to it and a mark on the ground there.  The spot is projected
	// every frame, so the panel rides with the camera; when it leaves the screen the panel stays clamped
	// at the edge, still usable, and the pointer and the mark go until the spot comes back
	const Int pad = px( PAD ), rowH = px( ROW_H ), titleH = px( TITLE_H ), lineH = px( LINE_H );
	const Int w = px( PANEL_W ), tip = px( POINTER_H ), halfBase = px( POINTER_W ) / 2;
	const Int h = pad + titleH + (Int)s_rows.size() * rowH + pad + 2 * lineH + pad + px( BUTTON_H ) + pad;
	const Int screenW = (Int)TheDisplay->getWidth(), screenH = (Int)TheDisplay->getHeight();
	ICoord2D spot;
	spot.x = screenW / 2;
	spot.y = screenH / 2;
	Bool onScreen = FALSE;
	if( TheTacticalView )
	{
		ICoord2D projected;
		const View::WorldToScreenReturn seen = TheTacticalView->worldToScreenTriReturn( &s_target, &projected );
		if( seen != View::WTS_INVALID )
			spot = projected;
		onScreen = seen == View::WTS_INSIDE_FRUSTUM;
	}
	ICoord2D at;
	at.x = max( 0, min( spot.x - w / 2, screenW - w ) );
	at.y = max( 0, min( spot.y - tip - h, screenH - h ) );
	setBox( &s_box, at.x, at.y, w, h );

	if( onScreen )
	{
		// the mark: a gold diamond round the spot
		const Int m = px( MARK );
		TheDisplay->drawLine( spot.x - m, spot.y, spot.x, spot.y - m / 2, 2.0f, GOLD );
		TheDisplay->drawLine( spot.x, spot.y - m / 2, spot.x + m, spot.y, 2.0f, GOLD );
		TheDisplay->drawLine( spot.x + m, spot.y, spot.x, spot.y + m / 2, 2.0f, GOLD );
		TheDisplay->drawLine( spot.x, spot.y + m / 2, spot.x - m, spot.y, 2.0f, GOLD );

		// the pointer, from the panel's foot to the spot, while the panel is above it
		if( spot.y > s_box.hi.y )
		{
			const Int baseX = max( s_box.lo.x + halfBase, min( spot.x, s_box.hi.x - halfBase ) );
			for( Int bx = baseX - halfBase; bx <= baseX + halfBase; ++bx )
				TheDisplay->drawLine( bx, s_box.hi.y - 1, spot.x, spot.y - m / 2, 1.0f, STEEL );
			TheDisplay->drawLine( baseX - halfBase, s_box.hi.y - 1, spot.x, spot.y - m / 2, 1.0f, KEYLINE );
			TheDisplay->drawLine( baseX + halfBase, s_box.hi.y - 1, spot.x, spot.y - m / 2, 1.0f, KEYLINE );
		}
	}

	drawSteel( s_box, STEEL );

	// the title: the power's name, and who the load lands for
	UnicodeString title, description;
	if( s_power->getRequiredScience() == SCIENCE_INVALID || !TheScienceStore->getNameAndDescription( s_power->getRequiredScience(), title, description ) )
		title = TheGameText->fetch( "GUI:AirliftTitle" );
	Player *recipient = s_recipient >= 0 ? ThePlayerList->getNthPlayer( s_recipient ) : NULL;
	UnicodeString forWhom;
	if( recipient )
		forWhom.format( TheGameText->fetch( "GUI:AirliftFor" ).str(), recipient->getPlayerDisplayName().str() );

	Int y = at.y + pad;
	TheDisplay->flushBatch2D();
	drawText( keptString( 0, title, TITLE_POINTS ), at.x + pad, y + titleH / 2, GOLD );
	drawText( keptString( 1, forWhom, TEXT_POINTS ), at.x + w - pad, y + titleH / 2, recipient == player ? TEXT : GOLD, TRUE );
	y += titleH;

	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		Row &row = s_rows[ i ];
		IRegion2D well;
		setBox( &well, at.x + pad, y + 2, w - 2 * pad, rowH - 4 );
		TheDisplay->drawFillRect( well.lo.x, well.lo.y, well.hi.x - well.lo.x, well.hi.y - well.lo.y, WELL );

		const Image *picture = row.thing->getButtonImage() ? row.thing->getButtonImage() : row.thing->getSelectedPortraitImage();
		const Int picW = px( PIC_W ), picH = px( PIC_H );
		const Int picX = well.lo.x + 2, picY = y + ( rowH - picH ) / 2;
		if( picture )
			TheDisplay->drawImage( picture, picX, picY, picX + picW, picY + picH, row.can ? 0xFFFFFFFF : GameMakeColor( 90, 90, 90, 255 ) );

		const Int key = px( KEY ), countW = px( COUNT_W );
		const Int plusX = well.hi.x - 4 - key;
		const Int minusX = plusX - countW - key;
		const Int keyY = y + ( rowH - key ) / 2;
		setBox( &row.minus, minusX, keyY, key, key );
		setBox( &row.plus, plusX, keyY, key, key );
		drawSteel( row.minus, row.count > 0 ? KEY_FACE : KEY_DEAD );
		drawSteel( row.plus, row.canMore ? KEY_FACE : KEY_DEAD );

		TheDisplay->flushBatch2D();
		const Color ink = row.can ? TEXT : TEXT_DEAD;
		drawText( keptString( ROW_STRINGS + 3 * (Int)i, row.thing->getDisplayName(), TEXT_POINTS ), picX + picW + pad, y + rowH / 2 - px( 7 ), ink );
		UnicodeString price;
		price.format( u"$%d", row.thing->calcCostToBuild( player ) );
		drawText( keptString( ROW_STRINGS + 1 + 3 * (Int)i, price, TEXT_POINTS ), picX + picW + pad, y + rowH / 2 + px( 8 ), row.can ? GOLD : TEXT_DEAD );
		UnicodeString count;
		count.format( u"%d", row.count );
		IRegion2D countBox;
		setBox( &countBox, minusX + key, keyY, countW, key );
		drawTextCentred( keptString( ROW_STRINGS + 2 + 3 * (Int)i, count, TEXT_POINTS ), countBox, ink );
		drawTextCentred( keptString( 2, UnicodeString( u"-" ), TITLE_POINTS ), row.minus, row.count > 0 ? TEXT : TEXT_DEAD );
		drawTextCentred( keptString( 3, UnicodeString( u"+" ), TITLE_POINTS ), row.plus, row.canMore ? TEXT : TEXT_DEAD );
		y += rowH;
	}

	// the planes and the fee, the total against the bank
	y += pad;
	const Int fee = s_planes * sp->getAirliftData()->m_airliftFeePerPlane;
	UnicodeString planes, total;
	planes.format( TheGameText->fetch( "GUI:AirliftPlanes" ).str(), s_planes, sp->getAirliftData()->m_airliftMaxPlanes, fee );
	total.format( TheGameText->fetch( "GUI:AirliftTotal" ).str(), s_cost );
	const Bool broke = s_cost > (Int)player->getMoney()->countMoney();
	drawText( keptString( 4, planes, TEXT_POINTS ), at.x + pad, y + lineH / 2, TEXT );
	drawText( keptString( 5, total, TITLE_POINTS ), at.x + pad, y + lineH + lineH / 2, broke ? RED : GOLD );
	y += 2 * lineH + pad;

	// OK and Cancel, flush right
	const Int bw = px( BUTTON_W ), bh = px( BUTTON_H );
	setBox( &s_cancel, at.x + w - pad - bw, y, bw, bh );
	setBox( &s_ok, s_cancel.lo.x - pad - bw, y, bw, bh );
	drawSteel( s_ok, s_okEnabled ? KEY_FACE : KEY_DEAD );
	drawSteel( s_cancel, KEY_FACE );
	TheDisplay->flushBatch2D();
	drawTextCentred( keptString( 6, TheGameText->fetch( "GUI:AirliftOK" ), TITLE_POINTS ), s_ok, s_okEnabled ? TEXT : TEXT_DEAD );
	drawTextCentred( keptString( 7, TheGameText->fetch( "GUI:AirliftCancel" ), TITLE_POINTS ), s_cancel, TEXT );
	s_drawn = TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool AirliftPanel_click( const ICoord2D *mouse, Bool cancel )
{
	if( !s_open || !s_drawn || !inside( mouse, s_box ) )
		return FALSE;

	// a right press on the panel closes it, so no order lands on the ground under it
	if( cancel )
	{
		AirliftPanel_close();
		return TRUE;
	}

	Player *player = NULL;
	OCLSpecialPower *sp = airlift( &player );
	if( sp == NULL )
	{
		AirliftPanel_close();
		return TRUE;
	}

	if( inside( mouse, s_cancel ) )
	{
		AirliftPanel_close();
		return TRUE;
	}

	const Int step = TheKeyboard && TheKeyboard->isShift() ? 5 : 1;
	for( size_t i = 0; i < s_rows.size(); ++i )
	{
		Row &row = s_rows[ i ];
		if( inside( mouse, row.minus ) )
			row.count = max( 0, row.count - step );
		else if( inside( mouse, row.plus ) )
		{
			// shift adds up to five, as far as the planes go
			for( Int n = 0; n < step && row.canMore; ++n )
			{
				++row.count;
				refresh( sp, player );
			}
		}
		else
			continue;
		refresh( sp, player );
		return TRUE;
	}

	if( inside( mouse, s_ok ) && s_okEnabled )
	{
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_AIRLIFT_ORDER );
		msg->appendIntegerArgument( s_power->getID() );
		msg->appendLocationArgument( s_target );
		msg->appendObjectIDArgument( s_source );
		for( size_t i = 0; i < s_rows.size(); ++i )
		{
			if( s_rows[ i ].count <= 0 )
				continue;
			msg->appendIntegerArgument( s_rows[ i ].thing->getTemplateID() );
			msg->appendIntegerArgument( s_rows[ i ].count );
		}
		AirliftPanel_close();
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AirliftPanel_describe( char *out, Int size )
{
	auto centre = []( const IRegion2D &r, char *buf, size_t n )
	{
		snprintf( buf, n, "[%d,%d]", ( r.lo.x + r.hi.x ) / 2, ( r.lo.y + r.hi.y ) / 2 );
	};

	Int used = snprintf( out, size, "\"open\":%s", s_open && s_drawn ? "true" : "false" );
	if( !s_open || !s_drawn || used >= size )
		return;

	char ok[ 32 ], cancelAt[ 32 ];
	centre( s_ok, ok, sizeof( ok ) );
	centre( s_cancel, cancelAt, sizeof( cancelAt ) );
	used += snprintf( out + used, size - used, ",\"forPlayer\":%d,\"planes\":%d,\"cost\":%d,\"okEnabled\":%s,\"okButton\":%s,\"cancelButton\":%s,\"rows\":[",
										s_recipient, s_planes, s_cost, s_okEnabled ? "true" : "false", ok, cancelAt );
	for( size_t i = 0; i < s_rows.size() && used < size; ++i )
	{
		char minus[ 32 ], plus[ 32 ];
		centre( s_rows[ i ].minus, minus, sizeof( minus ) );
		centre( s_rows[ i ].plus, plus, sizeof( plus ) );
		used += snprintf( out + used, size - used, "%s{\"unit\":\"%s\",\"count\":%d,\"buildable\":%s,\"minus\":%s,\"plus\":%s}",
											i ? "," : "", s_rows[ i ].thing->getName().str(), s_rows[ i ].count, s_rows[ i ].can ? "true" : "false", minus, plus );
	}
	if( used < size )
		snprintf( out + used, size - used, "]" );
}
