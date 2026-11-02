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

// RandomMapTemplate.h
// A random map built from a shipped map: the generator lays out a skeleton for the players asked
// for, then the shipped map's rivers, bridges, mesa, towns, tech, roads, ground detail, textures,
// trees and light are laid over it. The port of Tools/RandomMapLab/synthmap.py; the generator's
// side of it is RandomMapGenerator.cpp, this is the rest.

#pragma once

#ifndef __RANDOMMAPTEMPLATE_H_
#define __RANDOMMAPTEMPLATE_H_

#include "Common/RandomMapGenerator.h"

/** Is this template name a building a town is made of? The engine answers from the object INI
	(KindOf STRUCTURE, not a bridge, not supply or tech); a test can answer anything. */
typedef Bool (*RMGCivilianTest)( const char *templateName );

/** What the synthesis says about the map it built, for the log and the tests. */
struct RMGTemplateReport
{
	Int m_cutOffStarts;			///< starts the first one cannot walk to, bridges counted
	Int m_cutOffSites;			///< docks and derricks the same
	Int m_cutOffTech;
	Int m_bridges;
	Int m_rivers;
	Int m_towns;
	Int m_tech;
	Int m_roads;
	Int m_trees;
	Int m_scenery;
	Int m_patches;
	Int m_notBuilt;				///< exemplar features with no room on this map
	Int m_pads;					///< starts the exemplar's base was copied round
	Int m_ramps;				///< ramps cut to something the first start could not reach
	AsciiString m_notes;		///< what was not built, one line each
};

/** The CRC of a shipped map's stored bytes as this machine's file system holds them, kept after the
	first read. FALSE when no such map is installed. */
extern Bool rmgTemplateCRC( const AsciiString& templatePath, UnsignedInt *crcOut );

/** A shipped map's playable cells across and down and its number of seats, kept after the first read.
	FALSE when no such map is installed or it is not a skirmish map the reader knows. */
extern Bool rmgTemplateShape( const AsciiString& templatePath, Int *cellsX, Int *cellsY, Int *seats );

/** "maps\twilight flame\twilight flame.map" to "twilight flame" and back; only a shipped map's own
	folder-and-file shape has a token, since the token is what the generated map's name carries. */
extern Bool rmgTemplateToken( const AsciiString& templatePath, AsciiString& tokenOut );
extern void rmgTemplateFromToken( const AsciiString& token, AsciiString& templatePathOut );

/** The map, and when previewBytes is not NULL its preview, from the settings' template, reading the
	exemplar through TheFileSystem. mapBytes comes back empty when the template is not installed or
	its CRC is not the one the settings carry. */
extern void rmgGenerateFromTemplate( const RandomMapSettings& clamped, std::vector<char>& mapBytes,
	std::vector<char> *previewBytes );

/** The same from bytes in hand: the exemplar (a raw or EAR-packed .map) and the civilian test. The
	skeleton is built here. What the engine path and the tests share. */
extern Bool rmgSynthesise( const RandomMapSettings& clamped, const std::vector<char>& exemplarBytes,
	RMGCivilianTest civilian, std::vector<char>& mapBytes, std::vector<char> *previewBytes,
	RMGTemplateReport *report );

/** The share of the height up to the lower of two copied plateau bases a cell between them is raised
	by: sum is the cell's chamfer distances to the two (three a straight step) added, pairLeast the
	least such sum anywhere. A pair further apart than a corridor is not joined at all. */
extern Real rmgJoinShare( Int pairLeast, Int sum );

/** RandomMapGenerator.cpp's half: the generator's map for these settings on plain open ground, with
	no scenery, towns or bunkers. */
extern void rmgBuildSkeleton( const RandomMapSettings& settings, std::vector<char>& mapBytes );

#endif  // __RANDOMMAPTEMPLATE_H_
