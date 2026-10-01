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

// HtmlTemplate.h /////////////////////////////////////////////////////////////////////////////////
// Filling a page under Window/Html with what the game knows before it is laid out.
//
// A page is written as plain HTML and CSS.  The game's numbers go into it two ways:
//
//   {{name}}             replaced by that value, HTML-escaped; a name with no value becomes nothing,
//                        so class="row {{option:Key}}" is "row on" or "row ".
//   data-each="list"     on an element: the element is written once for every entry of the list,
//                        and inside it {{name}} reads that entry's values first.
//
// Everything else is left for the layout engine, so CSS decides what "on" or "flipped" looks like.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _HTML_TEMPLATE_H_
#define _HTML_TEMPLATE_H_

#include "Lib/BaseType.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

typedef std::map< std::string, std::string > HtmlValues;
typedef std::map< std::string, std::vector< HtmlValues > > HtmlLists;

/** Asked for a name neither the entry nor the page's values hold; TRUE with `value` set if it knows it. */
typedef std::function< Bool( const std::string &name, std::string &value ) > HtmlLookup;

extern std::string HtmlTemplate_expand( const std::string &page, const HtmlValues &values,
																				const HtmlLists &lists, const HtmlLookup &lookup );

/** A JSON object as a page's values and lists, for a page whose numbers come from outside the game
	* (the control socket's overlay).  A scalar is a value by its key, written as the text a page shows
	* (true is "on", false and null nothing); an object inside is values under "key."; an array is the
	* list by its key, each entry an object's values or a scalar as {{value}}.  FALSE, with both left
	* empty, for anything else, an array inside a list's entry included. */
extern Bool HtmlTemplate_readJson( const std::string &json, HtmlValues &values, HtmlLists &lists );

/** The text with & < > " and ' written as entities, so a player's name cannot open a tag. */
extern std::string HtmlTemplate_escape( const std::string &text );

/** A filled-in page split for litehtml: the <style> block's text into `styles`, for its CSS parser,
	* and the rest into `body` with every run of white space outside a tag one space, which is all CSS
	* draws of it while no page uses white-space: pre. */
extern void HtmlTemplate_compact( const std::string &page, std::string &body, std::string &styles );

#endif // _HTML_TEMPLATE_H_
