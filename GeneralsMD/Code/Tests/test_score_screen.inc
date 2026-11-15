// The score screen's standings: teams, order, the header's format and the row fit, walked over every
// team split of two to eight players.  Included into test_gameengine.cpp.

#include "GameClient/ScoreScreenLayout.h"
#include "GameClient/UIStyle.h"
#include "Common/GlobalData.h"

/** Every split of players into teams, largest first: 4 gives 4, 3+1, 2+2, 2+1+1, 1+1+1+1. */
static void scoreSplits( Int left, Int most, std::vector< Int > &sizes, std::vector< std::vector< Int > > &out )
{
	if( left == 0 )
	{
		if( sizes.size() >= 2 )
			out.push_back( sizes );
		return;
	}
	for( Int size = min( left, most ); size >= 1; size-- )
	{
		sizes.push_back( size );
		scoreSplits( left - size, size, sizes, out );
		sizes.pop_back();
	}
}

/** The seats of one split, teams dealt round the seats the way a lobby mixes them (seat 0 team 0,
	* seat 1 team 1, ...), so a team's members are never neighbours by accident. */
static std::vector< Int > scoreSeatTeams( const std::vector< Int > &sizes )
{
	std::vector< Int > left = sizes;
	std::vector< Int > teamOf;
	Bool placed = TRUE;
	while( placed )
	{
		placed = FALSE;
		for( size_t t = 0; t < left.size(); t++ )
		{
			if( left[ t ] > 0 )
			{
				teamOf.push_back( (Int)t );
				left[ t ]--;
				placed = TRUE;
			}
		}
	}
	return teamOf;
}

static Int scoreAt1080( Real board )
{
	return (Int)board;
}

static Int scoreAt720( Real board )
{
	return (Int)( board * 2.0f / 3.0f );
}

TEST(score_screen_every_split_of_two_to_eight_groups_orders_and_fits)
{
	Int splitsSeen = 0;
	for( Int players = 2; players <= 8; players++ )
	{
		std::vector< std::vector< Int > > splits;
		std::vector< Int > sizes;
		scoreSplits( players, players, sizes, splits );
		for( size_t s = 0; s < splits.size(); s++ )
		{
			splitsSeen++;
			const std::vector< Int > &split = splits[ s ];
			const std::vector< Int > lobbyTeam = scoreSeatTeams( split );
			const Int count = (Int)lobbyTeam.size();
			CHECK_EQ( count, players );

			// mutual allies give the lobby's teams back, numbered as they first appear
			std::vector< Bool > allied( count * count, FALSE );
			for( Int a = 0; a < count; a++ )
				for( Int b = 0; b < count; b++ )
					allied[ a * count + b ] = a != b && lobbyTeam[ a ] == lobbyTeam[ b ];
			const std::vector< Int > team = ScoreScreen_teamsFromAllies( count, allied );
			for( Int a = 0; a < count; a++ )
				for( Int b = 0; b < count; b++ )
					CHECK_EQ( team[ a ] == team[ b ], lobbyTeam[ a ] == lobbyTeam[ b ] );

			// the last team wins; the others fell in team order, so the second team fell first
			const Int winningTeam = team[ count - 1 ];
			std::vector< ScoreSeat > seats( count );
			for( Int a = 0; a < count; a++ )
			{
				seats[ a ].team = team[ a ];
				seats[ a ].score = 1000 + a * 37 % 11 * 100;
				seats[ a ].winner = team[ a ] == winningTeam;
				seats[ a ].fell = seats[ a ].winner ? SCORE_STILL_STANDING : 3000u + 600u * (UnsignedInt)team[ a ];
			}
			const std::vector< ScoreTeam > order = ScoreScreen_order( seats );
			CHECK_EQ( (Int)order.size(), (Int)split.size() );
			CHECK_EQ( order[ 0 ].team, winningTeam );
			Int listed = 0;
			for( size_t t = 0; t < order.size(); t++ )
			{
				listed += (Int)order[ t ].members.size();
				for( size_t m = 0; m < order[ t ].members.size(); m++ )
				{
					CHECK_EQ( seats[ order[ t ].members[ m ] ].team, order[ t ].team );
					if( m > 0 )
						CHECK( seats[ order[ t ].members[ m - 1 ] ].score >= seats[ order[ t ].members[ m ] ].score );
				}
				if( t > 1 )
					CHECK( seats[ order[ t - 1 ].members[ 0 ] ].fell > seats[ order[ t ].members[ 0 ] ].fell );
			}
			CHECK_EQ( listed, count );

			// the header: sizes largest first, or a free-for-all
			Bool freeForAll = FALSE;
			const std::string format = ScoreScreen_format( order, &freeForAll );
			const Bool allAlone = split[ 0 ] == 1;
			CHECK_EQ( freeForAll, allAlone && players >= 3 );
			if( !freeForAll )
			{
				std::string expected;
				for( size_t t = 0; t < split.size(); t++ )
					expected += ( t > 0 ? "v" : "" ) + std::to_string( split[ t ] );
				CHECK_STR( format.c_str(), expected.c_str() );
			}

			// with any team of two or more every team gets a band over its cards, a player alone too;
			// a free-for-all gets none
			const Int heads = ScoreScreen_bandCount( order );
			CHECK_EQ( heads, allAlone ? 0 : (Int)split.size() );

			// without chat the standings fit at 1080 and 720 with the lobby's pitch to spare for a strip
			for( Int scale = 0; scale < 2; scale++ )
			{
				Int ( *at )( Real ) = scale == 0 ? scoreAt1080 : scoreAt720;
				const Int minCard = max( at( SCORE_MIN_CARD_PITCH ), scale == 0 ? 30 : 25 );
				const Int minHead = max( at( SCORE_MIN_HEAD_PITCH ), scale == 0 ? 22 : 18 );
				const ScoreRowFit open = ScoreScreen_fitRows( count, heads, at( SCORE_CONTENT_BOTTOM ) - at( SCORE_CARDS_TOP ),
					at( SCORE_CARD_PITCH ), at( SCORE_HEAD_PITCH ), minCard, minHead );
				CHECK( open.fits );
				CHECK_EQ( open.cardPitch, at( SCORE_CARD_PITCH ) );

				// with chat they fit above the lobby's chat at 1080; where they cannot, the chat list
				// gives way, never under the cards and never short of its least height, whether the
				// chat line sits under it (LAN) or not (online)
				const ScoreRowFit chat = ScoreScreen_fitRows( count, heads, at( SCORE_CHAT_TOP - SCORE_CHAT_GAP ) - at( SCORE_CARDS_TOP ),
					at( SCORE_CARD_PITCH ), at( SCORE_HEAD_PITCH ), minCard, minHead );
				// at 1080 up to five bands fit; eight players under six or seven bands (2+2+1+1+1+1,
				// 2+1+1+1+1+1+1) push the chat list down
				if( scale == 0 && heads <= 5 )
					CHECK( chat.fits );
				CHECK( chat.cardPitch >= minCard && chat.headPitch >= minHead );
				const Real pixelsPerBoard = scale == 0 ? 1.0f : 2.0f / 3.0f;
				const Real listBottoms[] = { SCORE_CHAT_LIST_BOTTOM, SCORE_CONTENT_BOTTOM };
				for( Int bottom = 0; bottom < 2; bottom++ )
				{
					const Real chatTop = ScoreScreen_chatListTop( chat, pixelsPerBoard, listBottoms[ bottom ] );
					CHECK( chatTop >= SCORE_CHAT_TOP );
					CHECK( chatTop + SCORE_CHAT_MIN_LIST <= listBottoms[ bottom ] );
					// the cards end above the list, give or take the board's rounding to pixels
					CHECK( SCORE_CARDS_TOP + chat.used / pixelsPerBoard + SCORE_CHAT_GAP <= chatTop + 2.0f );
				}
			}
		}
	}
	// 1 + 2 + 4 + 6 + 10 + 14 + 21: the splits of 2 to 8 into at least two teams
	CHECK_EQ( splitsSeen, 58 );
}

TEST(score_screen_with_no_winner_keeps_the_standing_over_the_fallen)
{
	// 2v2v1 left early: nobody won, so nobody goes first, but the two teams still standing rank over
	// the lone player who fell, however high he scored, and between themselves by score
	std::vector< ScoreSeat > seats( 5 );
	const Int teams[] = { 0, 1, 0, 1, 2 };
	const Int scores[] = { 100, 900, 300, 800, 2500 };
	for( Int a = 0; a < 5; a++ )
	{
		seats[ a ].team = teams[ a ];
		seats[ a ].score = scores[ a ];
		seats[ a ].winner = FALSE;
		seats[ a ].fell = a == 4 ? 1200u : SCORE_STILL_STANDING;
	}
	const std::vector< ScoreTeam > order = ScoreScreen_order( seats );
	CHECK_EQ( (Int)order.size(), 3 );
	CHECK_EQ( order[ 0 ].team, 1 );		// standing, 1700
	CHECK_EQ( order[ 1 ].team, 0 );		// standing, 400
	CHECK_EQ( order[ 2 ].team, 2 );		// 2500 alone, but fell at 1200
	CHECK_EQ( order[ 0 ].members[ 0 ], 1 );
	CHECK_EQ( order[ 1 ].members[ 0 ], 2 );
	Bool freeForAll = TRUE;
	CHECK_STR( ScoreScreen_format( order, &freeForAll ).c_str(), "2v2v1" );
	CHECK( !freeForAll );
}

TEST(score_screen_a_team_still_standing_ranks_over_one_that_fell)
{
	// three-way free-for-all, seat 2 won; seat 0 fell last, seat 1 first
	std::vector< ScoreSeat > seats( 3 );
	for( Int a = 0; a < 3; a++ )
	{
		seats[ a ].team = a;
		seats[ a ].score = 500;
		seats[ a ].winner = a == 2;
	}
	seats[ 0 ].fell = 9000;
	seats[ 1 ].fell = 4000;
	seats[ 2 ].fell = SCORE_STILL_STANDING;
	const std::vector< ScoreTeam > order = ScoreScreen_order( seats );
	CHECK_EQ( order[ 0 ].team, 2 );
	CHECK_EQ( order[ 1 ].team, 0 );
	CHECK_EQ( order[ 2 ].team, 1 );
	Bool freeForAll = FALSE;
	CHECK( ScoreScreen_format( order, &freeForAll ).empty() );
	CHECK( freeForAll );
}

TEST(score_screen_income_is_smoothed_over_its_window)
{
	// a sample every 150 frames, 5000 earned a minute (1800 frames) at a steady rate, then nothing
	std::vector< UnsignedInt > frames;
	std::vector< Int > earned;
	for( Int each = 0; each <= 20; each++ )
	{
		frames.push_back( (UnsignedInt)each * 150u );
		earned.push_back( min( each, 12 ) * 5000 * 150 / 1800 );
	}
	const std::vector< Int > income = ScoreScreen_incomePerMinute( frames, earned, 900u, 1800u );
	CHECK_EQ( income[ 0 ], 0 );
	CHECK( income[ 6 ] > 4900 && income[ 6 ] <= 5000 );
	CHECK_EQ( income[ 20 ], 0 );
	// the window reaches back six samples, not to the start
	CHECK( income[ 14 ] > 0 && income[ 14 ] < 5000 );
}

TEST(score_screen_charts_end_where_the_match_did)
{
	// the end a decided match carries wins; without one, the logic's frame; with the logic torn down
	// under the open screen (frame 0), the last sample, never one flat sample at 0
	CHECK_EQ( ScoreScreen_endFrame( 6120u, 6400u, 6300u ), 6120u );
	CHECK_EQ( ScoreScreen_endFrame( 0u, 6400u, 6300u ), 6400u );
	CHECK_EQ( ScoreScreen_endFrame( 0u, 0u, 6300u ), 6300u );
	CHECK_EQ( ScoreScreen_endFrame( 0u, 0u, 0u ), 1u );

	// a sample every 100 frames to 6300: an end of 6120 drops the victory window's two
	std::vector< UnsignedInt > frames;
	for( UnsignedInt frame = 0; frame <= 6300u; frame += 100u )
		frames.push_back( frame );
	CHECK_EQ( ScoreScreen_shownSamples( frames, 6120u ), 62 );
	CHECK_EQ( ScoreScreen_shownSamples( frames, 6300u ), 64 );
	CHECK_EQ( ScoreScreen_shownSamples( frames, ScoreScreen_endFrame( 0u, 0u, frames.back() ) ), 64 );
	// the first sample always stays, and none stays none
	CHECK_EQ( ScoreScreen_shownSamples( frames, 1u ), 1 );
	CHECK_EQ( ScoreScreen_shownSamples( std::vector< UnsignedInt >(), 100u ), 0 );
}

// Abbreviation (UIStyle::abbreviate), measured as ten pixels a character so a width is a length.
// Letters by number: a u"" literal is read in the machine's code page by MSVC.

static Int abbreviationWidth( const UnicodeString &text, void * )
{
	return text.getLength() * 10;
}

static UnicodeString abbreviationWord( std::initializer_list< Int > letters )
{
	UnicodeString word;
	for( Int letter : letters )
		word.concat( (WideChar)letter );
	return word;
}

static UnicodeString abbreviated( const UnicodeString &text, Int widest, Int language, Bool words = TRUE,
	const UnicodeString &shortForm = UnicodeString::TheEmptyString, const UnicodeString &keep = UnicodeString::TheEmptyString )
{
	return UIStyle::abbreviate( text, shortForm, widest, language, abbreviationWidth, NULL, words, keep );
}

static const Int ELLIPSIS_CHAR = 0x2026;

TEST(abbreviation_cuts_after_a_consonant_before_a_vowel)
{
	// Kazanılan: Kaz., Kazan., whole; nothing shorter than the first syllable, then the ellipsis
	const UnicodeString kazanilan = abbreviationWord( { 'K', 'a', 'z', 'a', 'n', 0x131, 'l', 'a', 'n' } );
	CHECK( abbreviated( kazanilan, 90, TEXT_LANGUAGE_TURKISH ) == kazanilan );
	CHECK( abbreviated( kazanilan, 60, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"Kazan." ) );
	CHECK( abbreviated( kazanilan, 40, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"Kaz." ) );
	CHECK( abbreviated( kazanilan, 30, TEXT_LANGUAGE_TURKISH ) == abbreviationWord( { 'K', 'a', ELLIPSIS_CHAR } ) );
	// never past the first letter, however little room
	CHECK( abbreviated( kazanilan, 5, TEXT_LANGUAGE_TURKISH ) == abbreviationWord( { 'K', ELLIPSIS_CHAR } ) );

	// German Gebäude
	CHECK( abbreviated( abbreviationWord( { 'G', 'e', 'b', 0xE4, 'u', 'd', 'e' } ), 40, TEXT_LANGUAGE_GERMAN ) == UnicodeString( u"Geb." ) );

	// Russian Уничтожено: Уничт., Уничтож.
	const UnicodeString destroyed = abbreviationWord( { 0x423, 0x43D, 0x438, 0x447, 0x442, 0x43E, 0x436, 0x435, 0x43D, 0x43E } );
	CHECK( abbreviated( destroyed, 60, TEXT_LANGUAGE_RUSSIAN ) == abbreviationWord( { 0x423, 0x43D, 0x438, 0x447, 0x442, '.' } ) );
	CHECK( abbreviated( destroyed, 80, TEXT_LANGUAGE_RUSSIAN ) == abbreviationWord( { 0x423, 0x43D, 0x438, 0x447, 0x442, 0x43E, 0x436, '.' } ) );

	// Turkish Başlangıç: Başl., Başlang.; ş, ı and ç are letters like any other
	const UnicodeString start = abbreviationWord( { 'B', 'a', 0x15F, 'l', 'a', 'n', 'g', 0x131, 0xE7 } );
	CHECK( abbreviated( start, 50, TEXT_LANGUAGE_TURKISH ) == abbreviationWord( { 'B', 'a', 0x15F, 'l', '.' } ) );
	CHECK( abbreviated( start, 80, TEXT_LANGUAGE_TURKISH ) == abbreviationWord( { 'B', 'a', 0x15F, 'l', 'a', 'n', 'g', '.' } ) );

	// Dağıtılan is never cut after its soft g: Dağıt. fits from 60, under that the ellipsis
	const UnicodeString dealt = abbreviationWord( { 'D', 'a', 0x11F, 0x131, 't', 0x131, 'l', 'a', 'n' } );
	CHECK( abbreviated( dealt, 60, TEXT_LANGUAGE_TURKISH ) == abbreviationWord( { 'D', 'a', 0x11F, 0x131, 't', '.' } ) );
	CHECK( abbreviated( dealt, 50, TEXT_LANGUAGE_TURKISH ) == abbreviationWord( { 'D', 'a', 0x11F, 0x131, ELLIPSIS_CHAR } ) );
}

TEST(abbreviation_reads_turkish_y_as_a_consonant)
{
	// OYUNCU: with Y taken for a vowel no cut was left (no consonant stood before a vowel past the
	// first syllable) and the rule fell through to OYU...; Y is a consonant in Turkish, so OY. is there
	CHECK( abbreviated( UnicodeString( u"OYUNCU" ), 60, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"OYUNCU" ) );
	CHECK( abbreviated( UnicodeString( u"OYUNCU" ), 50, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"OY." ) );
	CHECK( abbreviated( UnicodeString( u"OYUNCU" ), 30, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"OY." ) );
}

TEST(abbreviation_drops_english_vowels_before_cutting)
{
	CHECK( abbreviated( UnicodeString( u"Destroyed" ), 60, TEXT_LANGUAGE_ENGLISH ) == UnicodeString( u"Dstryd" ) );
	// the same word in a language that counts y a vowel is only ever cut
	CHECK( abbreviated( UnicodeString( u"Destroyed" ), 60, TEXT_LANGUAGE_FRENCH ) == UnicodeString( u"Destr." ) );
	CHECK( abbreviated( UnicodeString( u"Experience" ), 70, TEXT_LANGUAGE_ENGLISH ) == UnicodeString( u"Exprnce" ) );
	CHECK( abbreviated( UnicodeString( u"Experience" ), 60, TEXT_LANGUAGE_ENGLISH ) == UnicodeString( u"Exper." ) );
}

TEST(abbreviation_shortens_the_longest_word_first_and_the_last_word_last)
{
	CHECK( abbreviated( UnicodeString( u"Value destroyed" ), 150, TEXT_LANGUAGE_ENGLISH ) == UnicodeString( u"Value destroyed" ) );
	CHECK( abbreviated( UnicodeString( u"Value destroyed" ), 140, TEXT_LANGUAGE_ENGLISH ) == UnicodeString( u"Val. destroyed" ) );
	CHECK( abbreviated( UnicodeString( u"Value destroyed" ), 130, TEXT_LANGUAGE_ENGLISH ) == UnicodeString( u"Val. destroy." ) );
	// a general's side at its shortest still too wide: its words go to their initials, the longest
	// first and the last last, before any ellipsis ("ABD Super Silah Generali", 18 at its shortest)
	const UnicodeString general( u"ABD Super Silah Generali" );
	CHECK( abbreviated( general, 180, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"ABD Sup. Sil. Gen." ) );
	CHECK( abbreviated( general, 160, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"ABD S. Sil. Gen." ) );
	CHECK( abbreviated( general, 150, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"ABD S. S. Gen." ) );
	CHECK( abbreviated( general, 120, TEXT_LANGUAGE_TURKISH ) == UnicodeString( u"ABD S. S. G." ) );
	// a name inside the sentence keeps every letter
	CHECK( abbreviated( UnicodeString( u"Return to Bartholomew" ), 190, TEXT_LANGUAGE_ENGLISH, TRUE, UnicodeString::TheEmptyString,
		UnicodeString( u"Bartholomew" ) ) == UnicodeString( u"Rtrn to Bartholomew" ) );
}

TEST(abbreviation_takes_the_short_form_first_and_leaves_names_to_the_ellipsis)
{
	CHECK( abbreviated( UnicodeString( u"Harcanan" ), 50, TEXT_LANGUAGE_TURKISH, TRUE, UnicodeString( u"Harc." ) ) == UnicodeString( u"Harc." ) );
	// a short form that does not fit either is passed over for the rule, and past the rule's shortest
	// cut for the ellipsis
	CHECK( abbreviated( UnicodeString( u"Harcanan" ), 40, TEXT_LANGUAGE_TURKISH, TRUE, UnicodeString( u"Harc." ) )
		== abbreviationWord( { 'H', 'a', 'r', ELLIPSIS_CHAR } ) );
	// a name is whole or cut with the ellipsis, never abbreviated
	CHECK( abbreviated( UnicodeString( u"Kazanova" ), 50, TEXT_LANGUAGE_TURKISH, FALSE ) == abbreviationWord( { 'K', 'a', 'z', 'a', ELLIPSIS_CHAR } ) );
}
