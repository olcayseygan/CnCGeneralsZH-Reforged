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

// FILE: Credits.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Dec 2002
//
//	Filename: 	Credits.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	This is where all the credit texts is going to be held.
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/INI.h"
#include "GameClient/Credits.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/UIStyle.h"
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
CreditsManager *TheCredits = NULL;

const FieldParse CreditsManager::m_creditsFieldParseTable[] = 
{

	{ "ScrollRate",					INI::parseInt,											NULL, offsetof( CreditsManager, m_scrollRate )	},
	{ "ScrollRateEveryFrames",	INI::parseInt,											NULL, offsetof( CreditsManager, m_scrollRatePerFrames )	},
	{ "ScrollDown",					INI::parseBool,											NULL,	offsetof( CreditsManager, m_scrollDown )  },
	{ "TitleColor",					INI::parseColorInt,									NULL,	offsetof( CreditsManager, m_titleColor )  },
	{ "MinorTitleColor",		INI::parseColorInt,									NULL,	offsetof( CreditsManager, m_positionColor )  },
	{ "NormalColor",				INI::parseColorInt,									NULL,	offsetof( CreditsManager, m_normalColor )  },
	{ "Style",							INI::parseLookupList,								CreditStyleNames,	offsetof( CreditsManager, m_currentStyle )  },
	{ "Blank",							CreditsManager::parseBlank,					NULL,	NULL  },
	{ "Text",								CreditsManager::parseText,					NULL,	NULL  },

	{ NULL,										NULL,													NULL, 0 }  // keep this last

};

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

void INI::parseCredits( INI *ini )
{
	// find existing item if present
	DEBUG_ASSERTCRASH( TheCredits, ("parseCredits: TheCredits has not been ininialized yet.\n") );
	if( !TheCredits )
		return;

	// parse the ini definition
	ini->initFromINI( TheCredits, TheCredits->getFieldParse() );

}  // end parseCommandButtonDefinition


CreditsLine::CreditsLine()
{
	m_useSecond = FALSE;
	m_done = FALSE;
	m_style = CREDIT_STYLE_BLANK;
	m_displayString = NULL;
	m_secondDisplayString = NULL;
}

CreditsLine::~CreditsLine()
{
	if(m_displayString)
		TheDisplayStringManager->freeDisplayString(m_displayString);
	if(m_secondDisplayString)
		TheDisplayStringManager->freeDisplayString(m_secondDisplayString);
	
	m_displayString = NULL;
	m_secondDisplayString = NULL;
}


CreditsManager::CreditsManager(void)
{
	m_scrollRate = 1; // in pixels
	m_scrollCarry = 0.0f;
	m_scrollRatePerFrames = 1;
	m_scrollDown = TRUE;	// if TRUE text will come from the top to the bottom if False, it will go from the bottom up
	m_framesSinceStarted = 0;
	m_titleColor = m_positionColor = m_normalColor = GameMakeColor(255,255,255,255);
	
	m_currentStyle = CREDIT_STYLE_NORMAL;
	m_isFinished = FALSE;
	m_normalFontHeight = 10;
	m_hasRegion = FALSE;
	m_region.lo.x = m_region.lo.y = m_region.hi.x = m_region.hi.y = 0;
}

CreditsManager::~CreditsManager(void)
{

	m_displayedCreditLineList.clear();
	CreditsLineList::iterator it =m_creditLineList.begin();
	while (it != m_creditLineList.end())
	{
		CreditsLine *cLine = *it;
		delete cLine;
		it = m_creditLineList.erase(it);
	}
}

void CreditsManager::init(void )
{
	m_isFinished = FALSE;
	m_creditLineListIt = m_creditLineList.begin();
	m_framesSinceStarted = 0;
}

void CreditsManager::load(void )
{
	// this edition's own section first (NOTICE.md), then EA's roll as Credits.ini has it
	addLine( CREDIT_STYLE_POSITION, TheGameText->fetch( "GUI:CreditsReforged" ) );
	addBlank();
	addLine( CREDIT_STYLE_POSITION, TheGameText->fetch( "GUI:CreditsLead" ) );
	addLine( CREDIT_STYLE_NORMAL, UnicodeString( u"Olcay Seygan" ) );
	addBlank();
	addLine( CREDIT_STYLE_POSITION, TheGameText->fetch( "GUI:CreditsPort" ) );
	// spelled out: the file is read in the machine's code page, so a literal İ and ı came out as bytes
	addLine( CREDIT_STYLE_NORMAL, UnicodeString( u"İlyas Akın" ) );
	addBlank();
	addBlank();

	INI ini;
	// Read from INI all the ControlBarSchemes
	ini.load( AsciiString( "Data\\INI\\Credits.ini" ), INI_LOAD_OVERWRITE, NULL );

	if(m_scrollRatePerFrames <=0)
		m_scrollRatePerFrames = 1;
	if(m_scrollRate <=0)
		m_scrollRate = 1;

	pairRolesWithNames();

	// getFont hands back NULL for a font it could not load
	GameFont *font = styleFont( CREDIT_STYLE_NAME );
	m_normalFontHeight = font ? font->height : 0;
}

void CreditsManager::addLine( Int style, const UnicodeString &text )
{
	CreditsLine *cLine = new CreditsLine;
	cLine->m_style = style;
	cLine->m_text = text;
	m_creditLineList.push_back( cLine );
}

/** A run of lines between two blanks that names a role and then people (a MINORTITLE then NORMAL
	* lines in Credits.ini) is set as the credits column draws it: the role left of the middle on the
	* first name's line, the names right of it.  A run of titles alone stays centred. */
void CreditsManager::pairRolesWithNames( void )
{
	CreditsLineList::iterator it = m_creditLineList.begin();
	while( it != m_creditLineList.end() )
	{
		CreditsLineList::iterator runEnd = it;
		while( runEnd != m_creditLineList.end() && (*runEnd)->m_style != CREDIT_STYLE_BLANK )
			++runEnd;
		CreditsLine *role = NULL;
		for( CreditsLineList::iterator line = it; line != runEnd; )
		{
			CreditsLine *cLine = *line;
			if( cLine->m_style == CREDIT_STYLE_POSITION )
			{
				role = cLine;
				++line;
			}
			else if( cLine->m_style == CREDIT_STYLE_NORMAL && role && role->m_style == CREDIT_STYLE_POSITION )
			{
				role->m_style = CREDIT_STYLE_ROLE;
				role->m_secondText = cLine->m_text;
				delete cLine;
				line = m_creditLineList.erase( line );
			}
			else
			{
				if( cLine->m_style == CREDIT_STYLE_NORMAL && role )
					cLine->m_style = CREDIT_STYLE_NAME;
				++line;
			}
		}
		it = runEnd;
		if( it != m_creditLineList.end() )
			++it;
	}
}

IRegion2D CreditsManager::rollRegion( void )
{
	if( m_hasRegion )
		return m_region;
	IRegion2D whole;
	whole.lo.x = whole.lo.y = 0;
	whole.hi.x = TheDisplay->getWidth();
	whole.hi.y = TheDisplay->getHeight();
	return whole;
}

/// the menu language's sizes at 1080: a title 22 bold, a heading 18 bold, a role 15, a name 18
GameFont *CreditsManager::styleFont( Int style )
{
	switch( style )
	{
		case CREDIT_STYLE_TITLE:		return UIStyle::menuFont( 22, TRUE );
		case CREDIT_STYLE_POSITION:	return UIStyle::menuFont( UIStyle::MENU_TEXT_BODY, TRUE );
		case CREDIT_STYLE_ROLE:			return UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
		default:										return UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	}
}

void CreditsManager::reset( void )
{
	m_displayedCreditLineList.clear();
	m_isFinished = FALSE;
	m_creditLineListIt = m_creditLineList.begin();
	m_framesSinceStarted = 0;
	m_scrollCarry = 0.0f;

}

void CreditsManager::update( void )
{
	if(m_isFinished)
		return;
	m_framesSinceStarted++;
	
	if(m_framesSinceStarted%m_scrollRatePerFrames != 0)
		return;

	// ScrollRate is in 800x600 pixels. The lines are set in fonts that grow with the screen, so a
	// step of the same pixel count took minutes longer at 1600x1200; the step grows with the fonts.
	const Int FONT_SCALE_PROBE = 1000;
	const Real fontScale = (Real)TheGlobalLanguageData->adjustFontSize( FONT_SCALE_PROBE ) / FONT_SCALE_PROBE;
	const Real scrollStep = m_scrollRate * fontScale + m_scrollCarry;
	const Int scrollPixels = (Int)scrollStep;
	m_scrollCarry = scrollStep - scrollPixels;

	Int y = 0;
	Int yTest = 0;
	Int lastHeight = 0;
	const IRegion2D roll = rollRegion();
	Int start = m_scrollDown? roll.lo.y:roll.hi.y;
	Int end =m_scrollDown? roll.hi.y:roll.lo.y;
	Int offsetStartMultiplyer = m_scrollDown? -1:0;  // if we're scrolling from the top, we need to subtract the height
	Int offsetEndMultiplyer = m_scrollDown? 0:1;
	Int directionMultiplyer = m_scrollDown? 1:-1;
	CreditsLineList::iterator drawIt = m_displayedCreditLineList.begin();
	while (drawIt != m_displayedCreditLineList.end())
	{
		CreditsLine *cLine = *drawIt;
		y = cLine->m_pos.y = cLine->m_pos.y + (scrollPixels * directionMultiplyer);
		lastHeight = cLine->m_height;
		yTest = y + ((lastHeight + CREDIT_SPACE_OFFSET) * offsetEndMultiplyer);
		if(((m_scrollDown && (yTest > end)) || (!m_scrollDown && (yTest < end))))
		{
			TheDisplayStringManager->freeDisplayString(cLine->m_displayString);
			TheDisplayStringManager->freeDisplayString(cLine->m_secondDisplayString);
			cLine->m_displayString = NULL;
			cLine->m_secondDisplayString = NULL;
			drawIt = m_displayedCreditLineList.erase(drawIt);
		}
		else
			drawIt++;
	}
	
	y= y + ((lastHeight + CREDIT_SPACE_OFFSET) * offsetStartMultiplyer);
	
	// is it time to add a new string?
	if(!((m_scrollDown && (yTest >= start)) || (!m_scrollDown && (yTest  <= start))))
		return;
	
	if(m_displayedCreditLineList.size() == 0 && m_creditLineListIt == m_creditLineList.end())
		m_isFinished = TRUE;
	
	if(m_creditLineListIt == m_creditLineList.end())
		return;

	// a line's words in its style's face; a role's name in a name's.  Its height is its tallest
	// word's, a blank's a name's.
	CreditsLine *cLine = *m_creditLineListIt;
	ICoord2D pos;
	cLine->m_height = m_normalFontHeight;
	if( cLine->m_style != CREDIT_STYLE_BLANK )
	{
		for( Int part = 0; part < 2; ++part )
		{
			const UnicodeString &words = part == 0 ? cLine->m_text : cLine->m_secondText;
			if( words.isEmpty() )
				continue;
			DisplayString *ds = TheDisplayStringManager->newDisplayString();
			if( !ds )
				return;
			ds->setFont( styleFont( part == 0 ? cLine->m_style : CREDIT_STYLE_NAME ) );
			ds->setText( words );
			ds->getSize( &pos.x, &pos.y );
			cLine->m_height = part == 0 ? pos.y : max( cLine->m_height, pos.y );
			( part == 0 ? cLine->m_displayString : cLine->m_secondDisplayString ) = ds;
		}
	}
	cLine->m_pos.y = start + (cLine->m_height * offsetStartMultiplyer);

	m_displayedCreditLineList.push_back(cLine);

	if(m_creditLineListIt != m_creditLineList.end())
		m_creditLineListIt++;
	
}

void CreditsManager::draw( void )
{
	// in the roll's rectangle, fading out over 64 px at 1080 at its top and foot; a heading centred,
	// a role MUTED ending 18 px left of the middle with its names starting 18 px right of it
	IRegion2D roll = rollRegion();
	const Real scale = UIStyle::boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() ).scale;
	const Int middle = ( roll.lo.x + roll.hi.x ) / 2;
	const Int gap = REAL_TO_INT( 18 * scale + 0.5f );
	const Int fade = m_hasRegion ? max( 1, REAL_TO_INT( 64 * scale + 0.5f ) ) : max( 1, (Int)TheDisplay->getHeight() / 3 );
	CreditsLineList::iterator drawIt = m_displayedCreditLineList.begin();
	while (drawIt != m_displayedCreditLineList.end())
	{
		CreditsLine *cLine = *drawIt;
		drawIt++;
		const Int inside = min( cLine->m_pos.y - roll.lo.y, roll.hi.y - ( cLine->m_pos.y + cLine->m_height ) );
		const Real perc = inside <= 0 ? 0.0f : min( 1.0f, INT_TO_REAL( inside ) / fade );
		if( perc <= 0.0f )
			continue;
		const Int alpha = REAL_TO_INT( 255 * perc );
		const Color ink = GameMakeColor( 255, 255, 255, alpha );
		const Color muted = UIStyle::withAlpha( UIStyle::MUTED, alpha );
		const Color shadow = GameMakeColor( 0, 0, 0, REAL_TO_INT( 160 * perc ) );
		ICoord2D pos;
		DisplayString *first = cLine->m_displayString, *second = cLine->m_secondDisplayString;
		if( first )
			first->setClipRegion( &roll );
		if( second )
			second->setClipRegion( &roll );
		switch( cLine->m_style )
		{
			case CREDIT_STYLE_COLUMN:
			case CREDIT_STYLE_ROLE:
				if( first )
				{
					first->getSize( &pos.x, &pos.y );
					first->draw( middle - gap - pos.x, cLine->m_pos.y, cLine->m_style == CREDIT_STYLE_ROLE ? muted : ink, shadow );
				}
				if( second )
					second->draw( middle + gap, cLine->m_pos.y, ink, shadow );
				break;
			case CREDIT_STYLE_NAME:
				if( first )
					first->draw( middle + gap, cLine->m_pos.y, ink, shadow );
				break;
			default:
				if( first )
				{
					first->getSize( &pos.x, &pos.y );
					first->draw( middle - pos.x / 2, cLine->m_pos.y, ink, shadow );
				}
				break;
		}
	}
}
void CreditsManager::addBlank( void )
{
	CreditsLine *cLine = new CreditsLine;
	cLine->m_style = CREDIT_STYLE_BLANK;
	m_creditLineList.push_back(cLine);
}



void CreditsManager::parseBlank( INI* ini, void *instance, void *store, const void *userData )
{
	CreditsManager *cManager = (CreditsManager *)instance;
	cManager->addBlank();
}

void CreditsManager::parseText( INI* ini, void *instance, void *store, const void *userData )
{
	
	AsciiString asciiString = ini->getNextQuotedAsciiString();
	CreditsManager *cManager = (CreditsManager *)instance;
	cManager->addText(asciiString);
}
void CreditsManager::addText( AsciiString text )
{
	CreditsLine *cLine = new CreditsLine;

	switch (m_currentStyle) 
	{
		case CREDIT_STYLE_TITLE:
		case CREDIT_STYLE_POSITION:
		case CREDIT_STYLE_NORMAL:
			{
				cLine->m_text = getUnicodeString(text);
				cLine->m_style = m_currentStyle;
				m_creditLineList.push_back(cLine);
			}
			break;
		case CREDIT_STYLE_COLUMN:
			{
				CreditsLineList::reverse_iterator rIt = m_creditLineList.rbegin();
				CreditsLine *rcLine = *rIt;
				if(rIt == m_creditLineList.rend() || rcLine->m_style != CREDIT_STYLE_COLUMN 
				   || (rcLine->m_style == CREDIT_STYLE_COLUMN && rcLine->m_done == TRUE))
				{
					cLine->m_text = getUnicodeString(text);
					cLine->m_style = CREDIT_STYLE_COLUMN;
					cLine->m_useSecond = TRUE;
					m_creditLineList.push_back(cLine);
				}
				else
				{
					rcLine->m_secondText = getUnicodeString(text);
					rcLine->m_done = TRUE;
					delete cLine;
				}

			}
			break;
		default:
			DEBUG_ASSERTCRASH( FALSE, ("CreditsManager::addText we tried to add a credit text with the wrong style before it.  Style is %d\n", m_currentStyle) );
			delete cLine;
	}

}

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

UnicodeString CreditsManager::getUnicodeString(AsciiString str)
{
	UnicodeString uStr;
	if(str.compare("<BLANK>") == 0)
		return UnicodeString::TheEmptyString;

	if(str.find(':'))
		uStr = TheGameText->fetch(str);
	else
		uStr.translate(str);

	return uStr;
}
