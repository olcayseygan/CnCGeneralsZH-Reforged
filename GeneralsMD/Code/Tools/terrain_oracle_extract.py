#!/usr/bin/env python3
#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""Writes the ORIGINAL simulation-terrain code, as it stood at a given commit, for terrain_oracle.

T1 moves the height map's data and the height maths out of GameEngineDevice.  The proof is a golden:
the original code, built with mingw-w64 and run under Wine, must sample a real map bit for bit the
way the moved code does on macOS.  This script is how the oracle gets "the original code": it takes
each function's text out of the files at <commit> with `git show`, unchanged, so the oracle cannot
drift from what shipped by somebody re-typing it.

  terrain_oracle_extract.py <commit> <out.inc>

What it takes (by the first line of each definition, then to its closing brace):
  WorldHeightMap.cpp  ParseHeightMapData, ParseBlendTileData (see below), getCliffState,
                      setCliffState, initCliffFlagsFromHeights, setCellCliffFlagFromHeights,
                      and the PATHFIND_CLIFF_SLOPE_LIMIT_F definition
  WorldHeightMap.h    getHeight (inline)
  BaseHeightMap.cpp   getHeightMapHeight, isClearLineOfSight, getMaxCellHeight, isCliffCell
  BaseHeightMap.h     getClipHeight (inline)
  BaseHeightMap.cpp   initHeightData's min/max pass (a block, in TERRAIN_ORACLE_BASEHEIGHTMAP_MINMAX), which
                      sets the m_maxHeight isClearLineOfSight stops at

Each piece sits in an #if section: the two header inlines in TERRAIN_ORACLE_WORLDHEIGHTMAP_MEMBERS and
TERRAIN_ORACLE_BASEHEIGHTMAP_MEMBERS, everything else in TERRAIN_ORACLE_DEFINITIONS, so the oracle can
include the one file inside each stand-in class and again at file scope.

ParseBlendTileData is cut where it starts on the terrain textures (`m_numBitmapTiles = file.readInt();`)
and closed with `return true;`: everything the simulation reads from the chunk - the tile index
arrays, the flip and cliff bitmaps and the cliff bytes - comes before that line.  The cut is written
into the output as a comment.

One change is made on purpose: getHeightMapHeight's normal gets the terrain-normal fix (FIXES below),
each changed line marked in the output.  The golden is "original EA plus the terrain-normal fix".

It fails, rather than writing something plausible, if any piece or fix line is not found exactly once.
"""
import subprocess
import sys

DEVICE = 'GeneralsMD/Code/GameEngineDevice'
FILES = {
    'whm_cpp': f'{DEVICE}/Source/W3DDevice/GameClient/WorldHeightMap.cpp',
    'whm_h': f'{DEVICE}/Include/W3DDevice/GameClient/WorldHeightMap.h',
    'bhm_cpp': f'{DEVICE}/Source/W3DDevice/GameClient/BaseHeightMap.cpp',
    'bhm_h': f'{DEVICE}/Include/W3DDevice/GameClient/BaseHeightMap.h',
}

# (file, first line of the definition as it appears, stripped of leading whitespace)
FUNCTIONS = [
    ('whm_cpp', 'Bool WorldHeightMap::ParseHeightMapData(DataChunkInput &file, DataChunkInfo *info, void *userData)'),
    ('whm_cpp', 'Bool WorldHeightMap::ParseBlendTileData(DataChunkInput &file, DataChunkInfo *info, void *userData)'),
    ('whm_cpp', 'Bool WorldHeightMap::getCliffState(Int xIndex, Int yIndex) const'),
    ('whm_cpp', 'void WorldHeightMap::setCliffState(Int xIndex, Int yIndex, Bool state) '),
    ('whm_cpp', 'void WorldHeightMap::initCliffFlagsFromHeights()'),
    ('whm_cpp', 'void WorldHeightMap::setCellCliffFlagFromHeights(Int xIndex, Int yIndex)'),
    ('whm_h', 'inline UnsignedByte getHeight(Int xIndex, Int yIndex) '),
    ('bhm_cpp', 'Real BaseHeightMapRenderObjClass::getHeightMapHeight(Real x, Real y, Coord3D* normal) const'),
    ('bhm_cpp', 'Bool BaseHeightMapRenderObjClass::isClearLineOfSight(const Coord3D& pos, const Coord3D& posOther) const'),
    ('bhm_cpp', 'Real BaseHeightMapRenderObjClass::getMaxCellHeight(Real x, Real y) const'),
    ('bhm_cpp', 'Bool BaseHeightMapRenderObjClass::isCliffCell(Real x, Real y)'),
    ('bhm_h', 'inline UnsignedByte getClipHeight(Int x, Int y) const'),
]
CUT = ('Bool WorldHeightMap::ParseBlendTileData(', '\tm_numBitmapTiles = file.readInt();')
# The two header inlines are class members; the oracle includes the output inside each class with the
# section's macro defined, and once more at file scope for the out-of-class definitions.
SECTIONS = {
    'inline UnsignedByte getHeight(Int xIndex, Int yIndex) ': 'TERRAIN_ORACLE_WORLDHEIGHTMAP_MEMBERS',
    'inline UnsignedByte getClipHeight(Int x, Int y) const': 'TERRAIN_ORACLE_BASEHEIGHTMAP_MEMBERS',
}
DEFINES = [('whm_cpp', '#define PATHFIND_CLIFF_SLOPE_LIMIT_F')]
# Statement blocks inside a larger function, by their first and last lines: the oracle wraps each in a
# function of its own.  initHeightData's min/max pass sets the m_maxHeight that isClearLineOfSight
# stops at; the rest of initHeightData is rendering.
BLOCKS = [('bhm_cpp', '//Find min/max values for all terrain heights, useful for rendering optimization',
           'm_maxHeight = maxHt * MAP_HEIGHT_SCALE;', 'TERRAIN_ORACLE_BASEHEIGHTMAP_MINMAX')]
# The one deliberate change to the original text, the terrain-normal fix that TerrainHeightSampling.cpp
# carries: corner 3's X slope is d2-d10, not corner 1's d6-d0, and the bilinear blend pairs corner 0
# with 1 and 3 with 2 along x (EA's paired 0 with 3).  Each line, stripped, must occur exactly once in
# getHeightMapHeight; the operands are in the moved code's order, so both compute the same bits.
FIXES = ('Real BaseHeightMapRenderObjClass::getHeightMapHeight(', [
    ('Real deltaZ_X3 = d6-d0;', 'Real deltaZ_X3 = d2-d10;'),
    ('Real deltaZ_X_Left = deltaZ_X0*(1.0f-fx) + fx*deltaZ_X3;', 'Real deltaZ_X_Left = deltaZ_X0*(1.0f-fx) + fx*deltaZ_X1;'),
    ('Real deltaZ_X_Right = deltaZ_X1*(1.0f-fx) + fx*deltaZ_X2;', 'Real deltaZ_X_Right = deltaZ_X3*(1.0f-fx) + fx*deltaZ_X2;'),
    ('Real deltaZ_Y_Left = deltaZ_Y0*(1.0f-fx) + fx*deltaZ_Y3;', 'Real deltaZ_Y_Left = deltaZ_Y0*(1.0f-fx) + fx*deltaZ_Y1;'),
    ('Real deltaZ_Y_Right = deltaZ_Y1*(1.0f-fx) + fx*deltaZ_Y2;', 'Real deltaZ_Y_Right = deltaZ_Y3*(1.0f-fx) + fx*deltaZ_Y2;'),
])


def fail(message):
    print(f'terrain_oracle_extract: FAIL - {message}')
    sys.exit(1)


def show(commit, path):
    run = subprocess.run(['git', 'show', f'{commit}:{path}'], capture_output=True)
    if run.returncode != 0:
        fail(f'git show {commit}:{path}: {run.stderr.decode().strip()}')
    return run.stdout.decode('latin-1').replace('\r\n', '\n').split('\n')


def extract(lines, first, name):
    starts = [i for i, line in enumerate(lines) if line.strip() == first.strip()]
    if len(starts) != 1:
        fail(f'{name}: "{first}" found {len(starts)} times, want once')
    start = starts[0]
    depth, seen, out = 0, False, []
    for line in lines[start:]:
        out.append(line)
        code = line.split('//')[0]
        depth += code.count('{') - code.count('}')
        seen = seen or '{' in code
        if seen and depth == 0:
            return out
    fail(f'{name}: no closing brace')


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    commit, output = sys.argv[1], sys.argv[2]
    full = subprocess.run(['git', 'rev-parse', commit], capture_output=True).stdout.decode().strip()
    text = {key: show(commit, path) for key, path in FILES.items()}
    pieces = [f'// GENERATED by Tools/terrain_oracle_extract.py from {full}.  Do not edit: regenerate.\n'
              f'// Each piece is that commit\'s text, unchanged except the lines marked FIX (the terrain-normal\n'
              f'// fix, FIXES in the script); see terrain_oracle.cpp for what wraps it.\n']
    for key, first in DEFINES:
        found = [line for line in text[key] if line.startswith(first)]
        if len(found) != 1:
            fail(f'{first} found {len(found)} times')
        pieces.append(f'#if defined(TERRAIN_ORACLE_DEFINITIONS)\n// {FILES[key]}\n{found[0]}\n#endif\n')
    for key, first in FUNCTIONS:
        body = extract(text[key], first, first.split('(')[0])
        if first.startswith(CUT[0]):
            cut = [i for i, line in enumerate(body) if line == CUT[1]]
            if len(cut) != 1:
                fail(f'ParseBlendTileData: the cut line was found {len(cut)} times')
            body = body[:cut[0]] + [
                '\t/* terrain_oracle_extract.py CUT HERE: the rest of the chunk is the terrain textures, which the',
                '\t   simulation does not read.  Everything above is the original text. */',
                '\treturn true;', '}']
        if first.startswith(FIXES[0]):
            for old, new in FIXES[1]:
                at = [i for i, line in enumerate(body) if line.strip() == old]
                if len(at) != 1:
                    fail(f'getHeightMapHeight: fix line "{old}" found {len(at)} times, want once')
                body[at[0]] = body[at[0]].replace(old, new) + '\t// terrain_oracle_extract.py FIX: terrain normal'
        section = SECTIONS.get(first, 'TERRAIN_ORACLE_DEFINITIONS')
        pieces.append(f'#if defined({section})\n// {FILES[key]}\n' + '\n'.join(body) + f'\n#endif // {section}\n')
    for key, first, last, section in BLOCKS:
        lines = text[key]
        starts = [i for i, line in enumerate(lines) if line.strip() == first]
        if len(starts) != 1:
            fail(f'block "{first}" found {len(starts)} times')
        ends = [i for i in range(starts[0], len(lines)) if lines[i].strip() == last]
        if not ends:
            fail(f'block "{first}": no "{last}"')
        pieces.append(f'#if defined({section})\n// {FILES[key]}, a block inside initHeightData\n'
                      + '\n'.join(lines[starts[0]:ends[0] + 1]) + f'\n#endif // {section}\n')
    with open(output, 'w') as out:
        out.write('\n'.join(pieces))
    print(f'terrain_oracle_extract: {len(FUNCTIONS)} functions from {full[:10]} -> {output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
