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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: StructureCollapse.cpp
// See StructureCollapse.h.  The list is empty unless the option is on and something just fell.

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/GameClient.h"
#include "GameClient/ObserverCamera.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "W3DDevice/GameClient/StructureCollapse.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/scene.h"

#include <vector>

namespace StructureCollapse {

namespace {

struct Piece
{
	RenderObjClass *robj;
	Matrix3D base;			///< where it stood
	Vector3 axis;				///< what it leans over, level
	Real depth;					///< how far it has to go
	Real seconds;				///< sinkSeconds
	Real born;					///< logic frame
	Bool started;
};

std::vector<Piece> s_pieces;

const Real MAX_LEAN = 0.12f;		// radians, enough to read as falling rather than a lift going down

/// How far down the whole model is out of sight at full lean, which lifts the side away from the fall.
Real depthUnderGround(const RenderObjClass &robj, const Vector3 &foot)
{
	const Real BELOW_THE_GROUND = 5.0f;
	const AABoxClass &box = robj.Get_Bounding_Box();
	const Real reachX = WWMath::Fabs(box.Center.X - foot.X) + box.Extent.X;
	const Real reachY = WWMath::Fabs(box.Center.Y - foot.Y) + box.Extent.Y;
	const Real top = box.Center.Z + box.Extent.Z - foot.Z;
	return top + BELOW_THE_GROUND + MAX_LEAN * WWMath::Sqrt(reachX * reachX + reachY * reachY);
}

/// First sight of it, outside the draw: what the fog hides goes without being seen.
Bool start(Piece &piece)
{
	piece.started = TRUE;
	const Vector3 foot = piece.base.Get_Translation();
	const Coord3D at = { foot.X, foot.Y, foot.Z };
	if (ThePartitionManager->getShroudStatusForPlayer(TheObserverCamera.getShroudPlayerIndex(), &at) != CELLSHROUD_CLEAR)
		return FALSE;
	const Real heading = GameClientRandomValueReal(0.0f, 2.0f * PI);
	piece.axis.Set(WWMath::Cos(heading), WWMath::Sin(heading), 0.0f);
	((SimpleSceneClass *)W3DDisplay::m_3DScene)->Add_Render_Object(piece.robj);
	return TRUE;
}

/// Where it is at this many seconds, or FALSE when it is under.
Bool place(Piece &piece, Real seconds)
{
	if (seconds >= piece.seconds)
		return FALSE;
	const Real MAX_SHUDDER = 0.5f;	// units
	const Real SHUDDER_RATE = 45.0f;	// radians a second, of the game's time so a pause holds it still
	const Real fraction = sunk(seconds, piece.seconds);
	const Real shudder = MAX_SHUDDER * (1.0f - fraction);
	const Vector3 foot = piece.base.Get_Translation();

	Matrix3D turn(piece.axis, MAX_LEAN * fraction);
	const Vector3 slide(shudder * WWMath::Sin(SHUDDER_RATE * seconds), shudder * WWMath::Cos(SHUDDER_RATE * seconds),
		-piece.depth * fraction);
	turn.Set_Translation(foot + slide - turn.Rotate_Vector(foot));
	Matrix3D moved;
	Matrix3D::Multiply(turn, piece.base, &moved);
	piece.robj->Set_Transform(moved);
	return TRUE;
}

void release(Piece &piece)
{
	piece.robj->Remove();
	REF_PTR_RELEASE(piece.robj);
}

}  // namespace

void add(RenderObjClass *copy, float height)
{
	if (s_pieces.size() >= MAX_PIECES)
	{
		REF_PTR_RELEASE(copy);
		return;
	}
	Piece piece;
	piece.robj = copy;
	piece.base = copy->Get_Transform();
	piece.axis.Set(1.0f, 0.0f, 0.0f);
	piece.depth = depthUnderGround(*copy, piece.base.Get_Translation());
	piece.seconds = sinkSeconds(height);
	piece.born = (Real)TheGameLogic->getFrame();
	piece.started = FALSE;
	s_pieces.push_back(piece);
}

void update()
{
	if (s_pieces.empty())
		return;
	const Real now = (Real)TheGameLogic->getFrame() + WWMath::Clamp(GameEngine_logicTickFraction(), 0.0f, 1.0f);
	size_t kept = 0;
	for (size_t i = 0; i < s_pieces.size(); ++i)
	{
		Piece &piece = s_pieces[i];
		if ((piece.started || start(piece)) && place(piece, (now - piece.born) / LOGICFRAMES_PER_SECOND))
			s_pieces[kept++] = piece;
		else
			release(piece);
	}
	s_pieces.erase(s_pieces.begin() + kept, s_pieces.end());
}

void clear()
{
	for (size_t i = 0; i < s_pieces.size(); ++i)
		release(s_pieces[i]);
	s_pieces.clear();
}

}  // namespace StructureCollapse
