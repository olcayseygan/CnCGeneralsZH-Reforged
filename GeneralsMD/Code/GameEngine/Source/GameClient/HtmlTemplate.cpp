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

// HtmlTemplate.cpp ///////////////////////////////////////////////////////////////////////////////
// {{name}} and data-each, see HtmlTemplate.h.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/HtmlTemplate.h"

#include <ctype.h>

namespace
{

const std::string EACH_ATTRIBUTE = "data-each=";
const std::string OPEN_BRACES = "{{";
const std::string CLOSE_BRACES = "}}";

std::string lowered( const std::string &text )
{
	std::string lower = text;
	for( size_t index = 0; index < lower.size(); index++ )
		lower[ index ] = (char)tolower( (unsigned char)lower[ index ] );
	return lower;
}

/** The page without its <!-- comments -->, which may well describe {{names}} and data-each. */
std::string withoutComments( const std::string &page )
{
	std::string kept;
	size_t at = 0;
	for( size_t open = page.find( "<!--" ); open != std::string::npos; open = page.find( "<!--", at ) )
	{
		kept.append( page, at, open - at );
		const size_t close = page.find( "-->", open );
		if( close == std::string::npos )
			return kept;
		at = close + 3;
	}
	kept.append( page, at, std::string::npos );
	return kept;
}

std::string trimmed( const std::string &text )
{
	size_t first = 0;
	size_t last = text.size();
	while( first < last && isspace( (unsigned char)text[ first ] ) )
		first++;
	while( last > first && isspace( (unsigned char)text[ last - 1 ] ) )
		last--;
	return text.substr( first, last - first );
}

void appendEscaped( std::string &out, const std::string &text )
{
	for( size_t index = 0; index < text.size(); index++ )
	{
		switch( text[ index ] )
		{
			case '&':		out += "&amp;"; break;
			case '<':		out += "&lt;"; break;
			case '>':		out += "&gt;"; break;
			case '"':		out += "&quot;"; break;
			case '\'':	out += "&#39;"; break;
			default:		out += text[ index ]; break;
		}
	}
}

/** A stretch of page: literal text, or the trimmed name inside one {{name}}. */
struct Piece
{
	std::string text;
	Bool name;
};
typedef std::vector< Piece > Pieces;

/** Page text written once, or a data-each element written once per entry of `list`. */
struct Block
{
	Pieces pieces;
	std::string list;
	Bool each;
};
typedef std::vector< Block > Blocks;

void split( const std::string &text, Pieces &pieces )
{
	size_t at = 0;
	for( ;; )
	{
		const size_t open = text.find( OPEN_BRACES, at );
		const size_t close = open == std::string::npos ? std::string::npos : text.find( CLOSE_BRACES, open + OPEN_BRACES.size() );
		if( close == std::string::npos )
		{
			if( at < text.size() )
				pieces.push_back( Piece{ text.substr( at ), FALSE } );
			return;
		}

		if( open > at )
			pieces.push_back( Piece{ text.substr( at, open - at ), FALSE } );
		pieces.push_back( Piece{ trimmed( text.substr( open + OPEN_BRACES.size(), close - open - OPEN_BRACES.size() ) ), TRUE } );
		at = close + CLOSE_BRACES.size();
	}
}

void fill( std::string &out, const Pieces &pieces, const HtmlValues *entry, const HtmlValues &values,
					 const HtmlLookup &lookup, std::string &looked )
{
	for( Pieces::const_iterator piece = pieces.begin(); piece != pieces.end(); ++piece )
	{
		if( !piece->name )
		{
			out += piece->text;
			continue;
		}

		if( entry )
		{
			HtmlValues::const_iterator found = entry->find( piece->text );
			if( found != entry->end() )
			{
				appendEscaped( out, found->second );
				continue;
			}
		}

		HtmlValues::const_iterator found = values.find( piece->text );
		if( found != values.end() )
		{
			appendEscaped( out, found->second );
			continue;
		}

		looked.clear();
		if( lookup && lookup( piece->text, looked ) )
			appendEscaped( out, looked );
	}
}

/** Just past the end tag of the element whose start tag is at `open`, counting elements of the same
	* name nested inside it.
	* ponytail: an element with no end tag (<img data-each>) runs to the end of the page; wrap it in a
	* <div data-each> if that ever matters. */
size_t elementEnd( const std::string &lower, size_t open, const std::string &tag )
{
	Int depth = 0;
	for( size_t at = lower.find( '<', open ); at != std::string::npos; at = lower.find( '<', at + 1 ) )
	{
		const Bool closing = at + 1 < lower.size() && lower[ at + 1 ] == '/';
		const size_t nameAt = at + ( closing ? 2 : 1 );
		const size_t after = nameAt + tag.size();
		if( lower.compare( nameAt, tag.size(), tag ) != 0 || after >= lower.size() || strchr( " \t\r\n/>", lower[ after ] ) == NULL )
			continue;

		depth += closing ? -1 : 1;
		if( depth == 0 )
		{
			const size_t end = lower.find( '>', at );
			return end == std::string::npos ? lower.size() : end + 1;
		}
	}
	return lower.size();
}

/** The page cut into blocks once: everything but filling in the values, which is all that changes
	* from one frame to the next. */
void compile( const std::string &written, Blocks &blocks )
{
	const std::string page = withoutComments( written );
	const std::string lower = lowered( page );
	size_t at = 0;
	for( ;; )
	{
		const size_t attribute = lower.find( EACH_ATTRIBUTE, at );
		const size_t open = attribute == std::string::npos ? std::string::npos : lower.rfind( '<', attribute );
		if( open == std::string::npos || open < at )
		{
			blocks.push_back( Block{ Pieces(), std::string(), FALSE } );
			split( page.substr( at ), blocks.back().pieces );
			return;
		}

		size_t nameAt = attribute + EACH_ATTRIBUTE.size();
		const char quote = nameAt < page.size() ? page[ nameAt ] : 0;
		const Bool quoted = quote == '"' || quote == '\'';
		if( quoted )
			nameAt++;
		size_t nameEnd = nameAt;
		while( nameEnd < page.size() && ( quoted ? page[ nameEnd ] != quote : strchr( " \t\r\n>", page[ nameEnd ] ) == NULL ) )
			nameEnd++;

		size_t tagEnd = open + 1;
		while( tagEnd < lower.size() && isalnum( (unsigned char)lower[ tagEnd ] ) )
			tagEnd++;
		const std::string tag = lower.substr( open + 1, tagEnd - open - 1 );
		const size_t end = elementEnd( lower, open, tag );

		blocks.push_back( Block{ Pieces(), std::string(), FALSE } );
		split( page.substr( at, open - at ), blocks.back().pieces );

		blocks.push_back( Block{ Pieces(), page.substr( nameAt, nameEnd - nameAt ), TRUE } );
		split( page.substr( open, end - open ), blocks.back().pieces );

		at = end;
	}
}

/** A JSON object read straight into a page's values and lists, see HtmlTemplate_readJson. */
class JsonReader
{
public:
	explicit JsonReader( const std::string &text ) : m_text( text ), m_at( 0 ) {}

	Bool whole( HtmlValues &values, HtmlLists &lists )
	{
		if( !object( std::string(), values, &lists ) )
			return FALSE;
		space();
		return m_at == m_text.size();
	}

private:
	void space( void )
	{
		while( m_at < m_text.size() && isspace( (unsigned char)m_text[ m_at ] ) )
			m_at++;
	}

	Bool take( char wanted )
	{
		space();
		if( m_at >= m_text.size() || m_text[ m_at ] != wanted )
			return FALSE;
		m_at++;
		return TRUE;
	}

	Bool peek( char wanted )
	{
		space();
		return m_at < m_text.size() && m_text[ m_at ] == wanted;
	}

	/** \u escapes come out as UTF-8, which is what the page is drawn from. */
	static void appendUtf8( std::string &out, unsigned int code )
	{
		if( code < 0x80 )
			out += (char)code;
		else if( code < 0x800 )
		{
			out += (char)( 0xC0 | ( code >> 6 ) );
			out += (char)( 0x80 | ( code & 0x3F ) );
		}
		else
		{
			out += (char)( 0xE0 | ( code >> 12 ) );
			out += (char)( 0x80 | ( ( code >> 6 ) & 0x3F ) );
			out += (char)( 0x80 | ( code & 0x3F ) );
		}
	}

	Bool string( std::string &out )
	{
		if( !take( '"' ) )
			return FALSE;
		out.clear();
		while( m_at < m_text.size() && m_text[ m_at ] != '"' )
		{
			const char character = m_text[ m_at++ ];
			if( character != '\\' )
			{
				out += character;
				continue;
			}
			if( m_at >= m_text.size() )
				return FALSE;
			const char escaped = m_text[ m_at++ ];
			switch( escaped )
			{
				case 'n': out += '\n'; break;
				case 't': out += '\t'; break;
				case 'r': out += '\r'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'u':
				{
					if( m_at + 4 > m_text.size() )
						return FALSE;
					char *end = NULL;
					const std::string digits = m_text.substr( m_at, 4 );
					const unsigned long code = strtoul( digits.c_str(), &end, 16 );
					if( end != digits.c_str() + 4 )
						return FALSE;
					appendUtf8( out, (unsigned int)code );
					m_at += 4;
					break;
				}
				default: out += escaped; break;
			}
		}
		return take( '"' );
	}

	/** A string, or a number, true, false or null as the text a page shows for it: the number as
		* written, true as "on" so it can switch a class, false and null as nothing. */
	Bool scalar( std::string &out )
	{
		if( peek( '"' ) )
			return string( out );
		const size_t start = m_at;
		while( m_at < m_text.size() && ( isalnum( (unsigned char)m_text[ m_at ] ) || strchr( "+-.", m_text[ m_at ] ) ) )
			m_at++;
		const std::string word = m_text.substr( start, m_at - start );
		if( word == "true" )
			out = "on";
		else if( word == "false" || word == "null" )
			out.clear();
		else if( !word.empty() && strchr( "+-.0123456789", word[ 0 ] ) )
			out = word;
		else
			return FALSE;
		return TRUE;
	}

	/** An object's scalars become values under `prefix`, an object inside it a deeper prefix, and an
		* array a list of entries, each an object's values or a scalar's {{value}}.  `lists` is NULL
		* inside a list's entry, where a further list has nowhere to go. */
	Bool object( const std::string &prefix, HtmlValues &values, HtmlLists *lists )
	{
		if( !take( '{' ) )
			return FALSE;
		if( take( '}' ) )
			return TRUE;
		do
		{
			std::string key;
			if( !string( key ) || !take( ':' ) )
				return FALSE;
			const std::string name = prefix + key;
			if( peek( '{' ) )
			{
				if( !object( name + ".", values, lists ) )
					return FALSE;
			}
			else if( peek( '[' ) )
			{
				if( lists == NULL || !list( ( *lists )[ name ] ) )
					return FALSE;
			}
			else if( !scalar( values[ name ] ) )
				return FALSE;
		}
		while( take( ',' ) );
		return take( '}' );
	}

	Bool list( std::vector< HtmlValues > &entries )
	{
		take( '[' );
		entries.clear();
		if( take( ']' ) )
			return TRUE;
		do
		{
			entries.push_back( HtmlValues() );
			if( peek( '{' ) ? !object( std::string(), entries.back(), NULL ) : !scalar( entries.back()[ "value" ] ) )
				return FALSE;
		}
		while( take( ',' ) );
		return take( ']' );
	}

	const std::string &m_text;
	size_t m_at;
};

}	// namespace

//-------------------------------------------------------------------------------------------------
Bool HtmlTemplate_readJson( const std::string &json, HtmlValues &values, HtmlLists &lists )
{
	values.clear();
	lists.clear();
	if( JsonReader( json ).whole( values, lists ) )
		return TRUE;
	values.clear();
	lists.clear();
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
std::string HtmlTemplate_escape( const std::string &text )
{
	std::string escaped;
	appendEscaped( escaped, text );
	return escaped;
}

//-------------------------------------------------------------------------------------------------
std::string HtmlTemplate_expand( const std::string &written, const HtmlValues &values,
																 const HtmlLists &lists, const HtmlLookup &lookup )
{
	// the command bar's page alone is 38 KB and was lowercased and searched four times a frame;
	// keyed by its text, so a page that changes is cut again rather than served stale.
	// ponytail: never evicted; the game has a dozen fixed pages, add a bound if pages become generated
	static std::map< std::string, Blocks > compiled;
	std::map< std::string, Blocks >::iterator found = compiled.find( written );
	if( found == compiled.end() )
	{
		found = compiled.insert( std::make_pair( written, Blocks() ) ).first;
		compile( written, found->second );
	}

	std::string out;
	out.reserve( written.size() );
	std::string looked;
	for( Blocks::const_iterator block = found->second.begin(); block != found->second.end(); ++block )
	{
		if( !block->each )
		{
			fill( out, block->pieces, NULL, values, lookup, looked );
			continue;
		}

		HtmlLists::const_iterator list = lists.find( block->list );
		if( list != lists.end() )
			for( size_t entry = 0; entry < list->second.size(); entry++ )
				fill( out, block->pieces, &list->second[ entry ], values, lookup, looked );
	}
	return out;
}

//-------------------------------------------------------------------------------------------------
static Bool isPageSpace( char character )
{
	return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f';
}

//-------------------------------------------------------------------------------------------------
void HtmlTemplate_compact( const std::string &page, std::string &body, std::string &styles )
{
	static const std::string STYLE_OPEN = "<style";
	static const std::string STYLE_CLOSE = "</style>";

	body.clear();
	styles.clear();
	body.reserve( page.size() );
	size_t at = 0;
	while( at < page.size() )
	{
		if( page.compare( at, STYLE_OPEN.size(), STYLE_OPEN ) == 0 )
		{
			const size_t open = page.find( '>', at );
			const size_t close = open == std::string::npos ? std::string::npos : page.find( STYLE_CLOSE, open );
			if( close == std::string::npos )
			{
				body.append( page, at, std::string::npos );
				break;
			}
			styles.append( page, open + 1, close - open - 1 );
			at = close + STYLE_CLOSE.size();
		}
		else if( page[ at ] == '<' )
		{
			// a tag goes over whole, the white space in its attribute values with it
			char quote = 0;
			const size_t start = at;
			for( ; at < page.size(); at++ )
			{
				const char character = page[ at ];
				if( quote != 0 )
					quote = character == quote ? 0 : quote;
				else if( character == '"' || character == '\'' )
					quote = character;
				else if( character == '>' )
				{
					at++;
					break;
				}
			}
			body.append( page, start, at - start );
		}
		else if( isPageSpace( page[ at ] ) )
		{
			while( at < page.size() && isPageSpace( page[ at ] ) )
				at++;
			body += ' ';
		}
		else
			body += page[ at++ ];
	}
}
