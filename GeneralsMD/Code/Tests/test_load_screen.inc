// The load board's cards: one to thirty-two to a column at 1080 and 720, with and without load bars,
// with no team bands, a band per two cards and a band per card; and the team numbers the load board
// and the score screen share.  Included into test_gameengine.cpp.

#include "GameClient/LoadScreenLayout.h"
#include "GameClient/ScoreScreenLayout.h"
#include "GameClient/UIStyle.h"

/** The line heights the board measures, as Arial's cell is about 1.15 of the em UI-STYLE.md gives
	* for each size: names 25 and 18 px of em at 1080 and 720, the general 21 at 1080.  The general's
	* line at 720 and the small type (a computer's third line, the percentage, a band's label, a disc's
	* number) grow until their capitals stand 12 px tall, a cell of about 20.  The disc is the map's start spot, never under
	* 27 board pixels (18 at 720) and two over its number's cell. */
static LoadCardLines loadLines( Bool at720, Bool bars )
{
	LoadCardLines lines;
	lines.scale = at720 ? 2.0f / 3.0f : 1.0f;
	lines.name = at720 ? 21 : 29;
	lines.army = at720 ? 20 : 24;
	lines.detail = at720 ? 20 : 21;
	lines.percent = at720 ? 20 : 21;
	lines.band = lines.percent;
	lines.disc = at720 ? 22 : 27;
	lines.bars = bars;
	return lines;
}

/** A column's height on the board: from the band's top to a gap over the two-line tip panel. */
static Int loadColumnHeight( const LoadCardLines &lines, Int tipLine )
{
	const Real s = lines.scale;
	const Int bottom = LoadScreen_px( UIStyle::ROOM_BUTTON_TOP, s ) - LoadScreen_px( LOAD_FOOT_GAP, s )
		- LoadScreen_tipPanelHeight( tipLine, s ) - LoadScreen_px( LOAD_FOOT_GAP, s );
	return bottom - LoadScreen_px( LOAD_BAND_TOP, s );
}

/** Nothing inside one card runs over its edge or into another thing in it. */
static void loadCheckInside( const LoadCardPlan &plan, const LoadCardLines &lines )
{
	const Int h = plan.cardHeight;
	CHECK( plan.emblem > 0 );
	CHECK( plan.emblemTop >= 0 && plan.emblemTop + plan.emblem <= h );
	CHECK( plan.disc > 0 );
	CHECK( plan.disc >= lines.disc );
	CHECK( plan.discTop >= 0 && plan.discTop + plan.disc <= h );
	if( plan.mode <= LOAD_CARD_SHORT )
	{
		CHECK( plan.nameTop >= 0 );
		CHECK( plan.nameTop + lines.name <= plan.armyTop );
		Int last = plan.armyTop + lines.army;
		if( plan.mode == LOAD_CARD_FULL )
		{
			CHECK( last <= plan.detailTop );
			last = plan.detailTop + lines.detail;
		}
		else
			CHECK_EQ( plan.detailTop, -1 );
		CHECK( last <= h );
		if( lines.bars )
		{
			CHECK( plan.barTop >= last );
			CHECK( plan.barTop + plan.barHeight <= h );
			// the disc and the percentage share the inner column: the disc above
			CHECK( plan.discTop + plan.disc <= plan.percentTop );
			CHECK( plan.percentTop + lines.percent <= h );
		}
		else
			CHECK_EQ( plan.barTop, -1 );
	}
	else
	{
		const Int line = LoadScreen_lineHeight( plan.mode, lines );
		CHECK( plan.disc <= line );
		CHECK_EQ( plan.detailTop, -1 );
		CHECK( plan.nameTop >= 0 && plan.armyTop >= 0 && plan.percentTop >= 0 );
		CHECK( plan.emblemTop + line <= h );
		CHECK( plan.percentTop + lines.percent <= h );
		if( plan.barTop >= 0 )
		{
			CHECK( lines.bars );
			CHECK( plan.barTop >= plan.emblemTop + line );
			CHECK( plan.barTop + plan.barHeight <= h );
		}
	}
}

TEST(load_screen_and_score_screen_call_teams_by_one_number)
{
	// lobby teams 2 and 1, a player alone, lobby team 2 again (allies broken up), another alone:
	// the first two keep their lobby numbers, the rest take the lowest free ones in seat order
	std::vector< Int > shared, first;
	const Int lobby[ 5 ] = { 1, 0, -1, 1, -1 };
	for( Int t = 0; t < 5; t++ )
	{
		shared.push_back( lobby[ t ] );
		first.push_back( t );
	}
	const std::vector< Int > number = ScoreScreen_teamNumbers( shared, first, 33 );
	CHECK_EQ( (Int)number.size(), 5 );
	CHECK_EQ( number[ 0 ], 2 );
	CHECK_EQ( number[ 1 ], 1 );
	CHECK_EQ( number[ 2 ], 3 );
	CHECK_EQ( number[ 3 ], 4 );
	CHECK_EQ( number[ 4 ], 5 );
	// a lobby number past the highest is no lobby number
	std::vector< Int > high( 1, 40 ), zero( 1, 0 );
	CHECK_EQ( ScoreScreen_teamNumbers( high, zero, 33 )[ 0 ], 1 );

	// a 2v1v1: seats 0 and 1 lobby team 1, seats 2 and 3 alone.  The load board lists the teams in
	// seat order, the score screen in standings order (seat 3 won, then the pair, then seat 2):
	// each team gets one number on both
	std::vector< Int > loadShared, loadFirst;
	loadShared.push_back( 0 );
	loadFirst.push_back( 0 );
	loadShared.push_back( -1 );
	loadFirst.push_back( 2 );
	loadShared.push_back( -1 );
	loadFirst.push_back( 3 );
	const std::vector< Int > onLoad = ScoreScreen_teamNumbers( loadShared, loadFirst, 33 );
	const Int standings[ 3 ] = { 2, 0, 1 };	// the score screen's rows as load board teams
	std::vector< Int > scoreShared, scoreFirst;
	for( Int row = 0; row < 3; row++ )
	{
		scoreShared.push_back( loadShared[ standings[ row ] ] );
		scoreFirst.push_back( loadFirst[ standings[ row ] ] );
	}
	const std::vector< Int > onScore = ScoreScreen_teamNumbers( scoreShared, scoreFirst, 33 );
	for( Int row = 0; row < 3; row++ )
		CHECK_EQ( onScore[ row ], onLoad[ standings[ row ] ] );
	CHECK_EQ( onLoad[ 0 ], 1 );
	CHECK_EQ( onLoad[ 1 ], 2 );
	CHECK_EQ( onLoad[ 2 ], 3 );
}

/** One column as the board lays it: one sub-column, or two of one-line cards. */
static LoadCardPlan loadPlanColumn( Int cards, Int bands, Int column, const LoadCardLines &lines, Int *subColumns )
{
	*subColumns = LoadScreen_subColumns( cards, bands, column, lines );
	if( *subColumns == 1 )
		return LoadScreen_planCards( cards, bands, column, lines );
	const Int per = ( cards + 1 ) / 2;
	return LoadScreen_planCards( per, min( bands, per ), column, lines, LOAD_CARD_LINE );
}

TEST(load_screen_cards_one_to_thirty_two_a_column_fit_at_1080_and_720)
{
	for( Int res = 0; res < 2; res++ )
	{
		for( Int withBars = 0; withBars < 2; withBars++ )
		{
			const LoadCardLines lines = loadLines( res == 1, withBars == 1 );
			const Int column = loadColumnHeight( lines, lines.army );
			LoadCardMode last = LOAD_CARD_FULL;
			for( Int cards = 1; cards <= 32; cards++ )
			{
				for( Int bandKind = 0; bandKind < 3; bandKind++ )
				{
					const Int bands = bandKind == 0 ? 0 : bandKind == 1 ? ( cards + 1 ) / 2 : cards;
					Int subColumns = 0;
					const LoadCardPlan plan = loadPlanColumn( cards, bands, column, lines, &subColumns );
					const Int perColumn = subColumns == 1 ? cards : ( cards + 1 ) / 2;
					const Int perBands = subColumns == 1 ? bands : min( bands, perColumn );
					CHECK( subColumns == 1 || subColumns == 2 );
					// past sixteen a column always splits; a split column is one-line cards
					if( cards > LOAD_COLUMN_MOST )
						CHECK_EQ( subColumns, 2 );
					if( subColumns == 2 )
						CHECK( plan.mode >= LOAD_CARD_LINE );
					// a column with no bands always fits, and nothing ever leaves the column
					if( bands == 0 )
						CHECK( plan.fits );
					CHECK( plan.used <= column );
					const Int rows = perColumn + ( plan.bandHeight > 0 ? perBands : 0 );
					CHECK( plan.used == plan.cardHeight * perColumn + plan.bandHeight * ( plan.bandHeight > 0 ? perBands : 0 )
						+ plan.gap * ( rows - 1 ) );
					if( plan.fits )
						loadCheckInside( plan, lines );
					if( plan.bandLabels )
						CHECK( plan.bandHeight >= lines.band );
					if( bandKind == 0 && subColumns == 1 )
					{
						// more cards in one sub-column never bring a line back
						CHECK( plan.mode >= last );
						last = plan.mode;
					}
				}
			}
		}
	}

	// a 4v4 keeps the computer's third line at 1080, a 1v1 its full card, and a 32-player match
	// still fits at 720 with every card's bar, sixteen to a side in two sub-columns
	const LoadCardLines at1080 = loadLines( FALSE, TRUE );
	const LoadCardLines at720 = loadLines( TRUE, TRUE );
	Int subColumns = 0;
	CHECK_EQ( (Int)loadPlanColumn( 4, 1, loadColumnHeight( at1080, at1080.army ), at1080, &subColumns ).mode, (Int)LOAD_CARD_FULL );
	CHECK_EQ( loadPlanColumn( 1, 0, loadColumnHeight( at1080, at1080.army ), at1080, &subColumns ).cardHeight, 220 );
	CHECK( loadPlanColumn( 16, 1, loadColumnHeight( at720, at720.army ), at720, &subColumns ).fits );
	CHECK( loadPlanColumn( 32, 0, loadColumnHeight( at720, at720.army ), at720, &subColumns ).fits );
	// two teams of four to a column keep their labelled bands at 720
	CHECK( loadPlanColumn( 8, 2, loadColumnHeight( at720, at720.army ), at720, &subColumns ).bandLabels );
}
