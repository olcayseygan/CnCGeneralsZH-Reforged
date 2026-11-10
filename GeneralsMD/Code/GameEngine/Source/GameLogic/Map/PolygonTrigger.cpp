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

// PolygonTrigger.cpp
// Class to encapsulate polygon trigger areas.
// Author: John Ahlquist, November 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/DataChunk.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/Xfer.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/TerrainLogic.h"

/* ********* PolygonTrigger class ****************************/
PolygonTrigger *PolygonTrigger::ThePolygonTriggerListPtr = NULL;
Int PolygonTrigger::s_currentID = 1;
UnsignedInt PolygonTrigger::s_listRevision = 1;
UnsignedInt PolygonTrigger::s_shapeRevision = 1;
/**
 PolygonTrigger - Constructor.
*/
PolygonTrigger::PolygonTrigger(Int initialAllocation) :
m_nextPolygonTrigger(NULL),
m_points(NULL),
m_numPoints(0),
m_sizePoints(0),
m_exportWithScripts(false),
m_isWaterArea(false),
m_shouldRender(true),
m_selected(false),
//Added By Sadullah Nader
//Initializations inserted
m_isRiver(FALSE),
m_riverStart(0)
//
{
	if (initialAllocation < 2) initialAllocation = 2;
	m_points = NEW ICoord3D[initialAllocation];		// pool[]ify
	m_sizePoints = initialAllocation;
	m_triggerID = s_currentID++;

	m_waterHandle.m_polygon = this;

}


/**
 PolygonTrigger - Destructor - note - if linked, deletes linked items.
*/
PolygonTrigger::~PolygonTrigger(void)
{
	if (m_points) {
		delete [] m_points;
		m_points = NULL;
	}
	if (m_nextPolygonTrigger) {
		PolygonTrigger *cur = m_nextPolygonTrigger;
		PolygonTrigger *next;
		while (cur) {
			next = cur->getNext();
			cur->setNextPoly(NULL); // prevents recursion. 
			cur->deleteInstance();
			cur = next; 
		}
	}
}


/**
 PolygonTrigger::reallocate - increases the size of the points list.
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::reallocate(void)
{	
	DEBUG_ASSERTCRASH(m_numPoints <= m_sizePoints, ("Invalid m_numPoints."));
	if (m_numPoints == m_sizePoints) {
		// Reallocate.
		m_sizePoints += m_sizePoints;
		ICoord3D *newPts = NEW ICoord3D[m_sizePoints];
		Int i;
		for (i=0; i<m_numPoints; i++) {
			newPts[i] = m_points[i];
		}
		delete [] m_points;
		m_points = newPts;
	}
}

/**
* Find the polygon trigger with the matching ID
*/
PolygonTrigger *PolygonTrigger::getPolygonTriggerByID(Int triggerID)
{

	for( PolygonTrigger *poly = PolygonTrigger::getFirstPolygonTrigger();
			 poly; poly = poly->getNext() )
		if( poly->getID() == triggerID )
			return poly;

	// not found
	return NULL;

}

/**
* PolygonTrigger::ParsePolygonTriggersDataChunk - read a polygon triggers chunk.
* Format is the newer CHUNKY format.
*	See PolygonTrigger::WritePolygonTriggersDataChunk for the writer.
*	Input: DataChunkInput 
*		
*/
Bool PolygonTrigger::ParsePolygonTriggersDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	Int count;
	Int numPoints;
	Int triggerID;
	Int maxTriggerId = 0;
	Bool isWater;
	Bool isRiver;
	Int riverStart;
	AsciiString triggerName;
	AsciiString layerName;
	// Remove any existing polygon triggers, if any.
	PolygonTrigger::deleteTriggers(); // just in case.
	PolygonTrigger *pPrevTrig = NULL;
	ICoord3D loc;
	count = file.readInt(); 
	while (count>0) {
		count--;
		triggerName = file.readAsciiString();
		if (info->version >= K_TRIGGERS_VERSION_4) {
			layerName = file.readAsciiString();
		}
		triggerID = file.readInt();
		isWater = false;
		if (info->version >= K_TRIGGERS_VERSION_2) {
			isWater = file.readByte();
		}
		isRiver = false;
		riverStart = 0;
		if (info->version >= K_TRIGGERS_VERSION_3) {
			isRiver = file.readByte();
			riverStart = file.readInt();
		}

		// the points are allocated from this count before they are read, three Ints each
		numPoints = file.readCount(3 * sizeof(Int));
		PolygonTrigger *pTrig = newInstance(PolygonTrigger)(numPoints+1);	
		pTrig->setTriggerName(triggerName);
		if (info->version >= K_TRIGGERS_VERSION_4) {
			pTrig->setLayerName(layerName);
		}
		pTrig->setWaterArea(isWater);
		pTrig->setRiver(isRiver);
		pTrig->setRiverStart(riverStart);
		pTrig->m_triggerID = triggerID;
		if (triggerID > maxTriggerId) {
			maxTriggerId = triggerID;
		}
		Int i;
		for (i=0; i<numPoints; i++) {
			loc.x = file.readInt();
			loc.y = file.readInt();
			loc.z = file.readInt();
			pTrig->addPoint(loc);
		}
		if (numPoints<2) {
			DEBUG_LOG(("Deleting polygon trigger '%s' with %d points.\n", 
					pTrig->getTriggerName().str(), numPoints));
			pTrig->deleteInstance();
			continue;
		}
		if (pPrevTrig) {
			pPrevTrig->setNextPoly(pTrig);
		} else {
			PolygonTrigger::addPolygonTrigger(pTrig);
		}
		pPrevTrig = pTrig;
	}
	if (info->version == K_TRIGGERS_VERSION_1) 
	{
		// before water areas existed, so create a default one.
		PolygonTrigger *pTrig = newInstance(PolygonTrigger)(4);
		pTrig->setWaterArea(true);
#ifdef _DEBUG
		pTrig->setTriggerName("AutoAddedWaterAreaTrigger");
#endif
		pTrig->m_triggerID = maxTriggerId++;
		loc.x = -30*MAP_XY_FACTOR;
		loc.y = -30*MAP_XY_FACTOR;
		loc.z = 7;  // The old water position.
		pTrig->addPoint(loc);
		loc.x = 30*MAP_XY_FACTOR + TheGlobalData->m_waterExtentX;
		pTrig->addPoint(loc);
		loc.y = 30*MAP_XY_FACTOR + TheGlobalData->m_waterExtentY;
		pTrig->addPoint(loc);
		loc.x = -30*MAP_XY_FACTOR;
		pTrig->addPoint(loc);
		if (pPrevTrig) {
			pPrevTrig->setNextPoly(pTrig);
		} else {
			PolygonTrigger::addPolygonTrigger(pTrig);
		}
		pPrevTrig = pTrig;
	}
	s_currentID = maxTriggerId+1;
	DEBUG_ASSERTCRASH(file.atEndOfChunk(), ("Incorrect data file length."));
	return true;
}

/**
* PolygonTrigger::WritePolygonTriggersDataChunk - Writes a Polygon triggers chunk.
* Format is the newer CHUNKY format.
*	See PolygonTrigger::ParsePolygonTriggersDataChunk for the reader.
*	Input: DataChunkInput 
*		
*/
void PolygonTrigger::WritePolygonTriggersDataChunk(DataChunkOutput &chunkWriter)
{
	chunkWriter.openDataChunk("PolygonTriggers", 	K_TRIGGERS_VERSION_4);
		
		PolygonTrigger *pTrig;
		Int count = 0;
		for (pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
			count++;
		}
		chunkWriter.writeInt(count); 
		for (pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
			chunkWriter.writeAsciiString(pTrig->getTriggerName());	
			chunkWriter.writeAsciiString(pTrig->getLayerName());	
			chunkWriter.writeInt(pTrig->getID()); 
			chunkWriter.writeByte(pTrig->isWaterArea());
			chunkWriter.writeByte(pTrig->isRiver());
			chunkWriter.writeInt(pTrig->getRiverStart());
			chunkWriter.writeInt(pTrig->getNumPoints()); 
			Int i;
			for (i=0; i<pTrig->getNumPoints(); i++) {
				ICoord3D loc = *pTrig->getPoint(i);
				chunkWriter.writeInt( loc.x);
				chunkWriter.writeInt( loc.y);
				chunkWriter.writeInt( loc.z);
			}
		}

	chunkWriter.closeDataChunk();
}

/**
 PolygonTrigger::updateBounds - Updates the bounds.
*/
void PolygonTrigger::updateBounds(void)	const
{	
	const Int BIG_INT=0x7ffff0;
	m_bounds.lo.x = m_bounds.lo.y = BIG_INT;
	m_bounds.hi.x = m_bounds.hi.y = -BIG_INT;
	Int i;
	for (i=0; i<m_numPoints; i++) {
		if (m_points[i].x < m_bounds.lo.x) m_bounds.lo.x = m_points[i].x;
		if (m_points[i].y < m_bounds.lo.y) m_bounds.lo.y = m_points[i].y;
		if (m_points[i].x > m_bounds.hi.x) m_bounds.hi.x = m_points[i].x;
		if (m_points[i].y > m_bounds.hi.y) m_bounds.hi.y = m_points[i].y;
	}
	m_boundsNeedsUpdate = 0;
	Real halfWidth = (m_bounds.hi.x - m_bounds.lo.x) / 2.0f;
	Real halfHeight = (m_bounds.hi.y - m_bounds.lo.y) / 2.0f;

	m_radius = sqrt(halfHeight*halfHeight + halfWidth*halfWidth);
}

/**
 PolygonTrigger::triggersNear - the triggers whose bounds hold a point, in list order.

 Every object that moves a whole unit asks every trigger of the map whether it stands inside
 (Object::setTriggerAreaFlagsForChangeInPosition), nearly all of them answering no on the bounds
 check alone: 14% of physics late in a 32-player match.  The answers come from a grid of the bounds
 instead, rebuilt when the list or a trigger's outline changes.  pointInTrigger still decides; a
 trigger missing from the list here is one whose bounds would have refused the point.
*/
const std::vector<const PolygonTrigger*> &PolygonTrigger::triggersNear(const ICoord3D &point)
{
	typedef std::vector<const PolygonTrigger*> TriggerList;
	static const Int CELL = 256;	// world units a side
	static std::vector<TriggerList> cells;
	static Int loX = 0, loY = 0, cellsX = 0, cellsY = 0;
	static UnsignedInt builtList = 0, builtShape = 0;
	static const TriggerList none;

	if (builtList != s_listRevision || builtShape != s_shapeRevision)
	{
		builtList = s_listRevision;
		builtShape = s_shapeRevision;
		cells.clear();
		cellsX = cellsY = 0;

		Int hiX = 0, hiY = 0;
		Bool any = false;
		for (const PolygonTrigger *t = getFirstPolygonTrigger(); t; t = t->getNext())
		{
			if (t->m_numPoints == 0)
				continue;		// bounds lo past hi: refuses every point
			if (t->m_boundsNeedsUpdate)
				t->updateBounds();
			if (!any || t->m_bounds.lo.x < loX) loX = t->m_bounds.lo.x;
			if (!any || t->m_bounds.lo.y < loY) loY = t->m_bounds.lo.y;
			if (!any || t->m_bounds.hi.x > hiX) hiX = t->m_bounds.hi.x;
			if (!any || t->m_bounds.hi.y > hiY) hiY = t->m_bounds.hi.y;
			any = true;
		}
		if (!any)
			return none;

		cellsX = (hiX - loX) / CELL + 1;
		cellsY = (hiY - loY) / CELL + 1;
		cells.resize((size_t)cellsX * cellsY);
		for (const PolygonTrigger *t = getFirstPolygonTrigger(); t; t = t->getNext())
		{
			if (t->m_numPoints == 0)
				continue;
			const Int x0 = (t->m_bounds.lo.x - loX) / CELL, x1 = (t->m_bounds.hi.x - loX) / CELL;
			const Int y0 = (t->m_bounds.lo.y - loY) / CELL, y1 = (t->m_bounds.hi.y - loY) / CELL;
			for (Int y = y0; y <= y1; ++y)
				for (Int x = x0; x <= x1; ++x)
					cells[(size_t)y * cellsX + x].push_back(t);
		}
	}

	// in 64 bits: a position near the edge of an Int would wrap the difference into a cell
	const Int64 dx = (Int64)point.x - loX, dy = (Int64)point.y - loY;
	if (cellsX == 0 || dx < 0 || dy < 0 || dx / CELL >= cellsX || dy / CELL >= cellsY)
		return none;
	return cells[(size_t)(dy / CELL) * cellsX + (size_t)(dx / CELL)];
}


/**
 PolygonTrigger::addPolygonTrigger adds a trigger to the list of triggers.
*/
void PolygonTrigger::addPolygonTrigger(PolygonTrigger *pTrigger)
{	
	for (PolygonTrigger *pTrig=getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
		DEBUG_ASSERTCRASH(pTrig != pTrigger, ("Attempting to add trigger already in list."));
		if (pTrig==pTrigger) return;
	}
	pTrigger->m_nextPolygonTrigger = ThePolygonTriggerListPtr;
	ThePolygonTriggerListPtr = pTrigger;
	++s_listRevision;
}

/**
 PolygonTrigger::removePolygonTrigger removes a trigger to the list of 
	triggers.  note - does NOT delete pTrigger.
*/
void PolygonTrigger::removePolygonTrigger(PolygonTrigger *pTrigger)
{	
	PolygonTrigger *pPrev = NULL;
	// pTrig is used after the loop; VC6 for-scope let it escape.
	PolygonTrigger *pTrig;
	for (pTrig=getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
		if (pTrig==pTrigger) break;
		pPrev = pTrig;
	}
	DEBUG_ASSERTCRASH(pTrig, ("Attempting to remove a polygon not in the list."));
	if (pTrig) {
		if (pPrev) {
			DEBUG_ASSERTCRASH(pTrigger==pPrev->m_nextPolygonTrigger, ("Logic errror.  jba."));
			pPrev->m_nextPolygonTrigger = pTrig->m_nextPolygonTrigger;
		} else {
			DEBUG_ASSERTCRASH(pTrigger==ThePolygonTriggerListPtr, ("Logic errror.  jba."));
			ThePolygonTriggerListPtr = pTrig->m_nextPolygonTrigger;
		}
	}
	pTrigger->m_nextPolygonTrigger = NULL;
	++s_listRevision;
}

/**
 PolygonTrigger::deleteTriggers Deletes list of triggers.
*/
void PolygonTrigger::deleteTriggers(void)
{
	PolygonTrigger *pList = ThePolygonTriggerListPtr;	
	ThePolygonTriggerListPtr = NULL;
	s_currentID = 1;
	++s_listRevision;
	pList->deleteInstance();
}

/**
 PolygonTrigger::addPoint adds a point at the end of the polygon.
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::addPoint(const ICoord3D &point)
{	
	DEBUG_ASSERTCRASH(m_numPoints <= m_sizePoints, ("Invalid m_numPoints."));
	if (m_numPoints == m_sizePoints) {
		reallocate();
	}
	m_points[m_numPoints] = point;
	m_numPoints++;
	m_boundsNeedsUpdate = true;
	++s_shapeRevision;
}

/**
 PolygonTrigger::setPoint sets the point at index ndx.
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::setPoint(const ICoord3D &point, Int ndx)
{	
	DEBUG_ASSERTCRASH(ndx>=0 && ndx <= m_numPoints, ("Invalid ndx."));
	if (ndx<0) return;
	if (ndx == m_numPoints) {	// we are setting first available unused point
		addPoint(point);
		return;
	}
	if (ndx>m_numPoints) { // Can't skip points.
		return;
	}
	if (m_points[ndx].x != point.x || m_points[ndx].y != point.y)
		++s_shapeRevision;	// a water level change moves z alone
	m_points[ndx] = point;
	m_boundsNeedsUpdate = true;
}

/**
 PolygonTrigger::insertPoint .
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::insertPoint(const ICoord3D &point, Int ndx)
{	
	DEBUG_ASSERTCRASH(ndx>=0 && ndx <= m_numPoints, ("Invalid ndx."));
	if (ndx<0) return;
	if (ndx == m_numPoints) {	// we are setting first available unused point
		addPoint(point);
		return;
	}
	if (m_numPoints == m_sizePoints) {
		reallocate();
	}
	Int i;
	for (i=m_numPoints; i>ndx; i--) {
		m_points[i] = m_points[i-1];
	}
	m_points[ndx] = point;
	m_numPoints++;
	m_boundsNeedsUpdate = true;
	++s_shapeRevision;
}

/**
 PolygonTrigger::deletePoint .
 NOTE: It is expected that this will only get called in the editor, as in the game
 the poly triggers don't change.
*/
void PolygonTrigger::deletePoint(Int ndx)
{	
	DEBUG_ASSERTCRASH(ndx>=0 && ndx < m_numPoints, ("Invalid ndx."));
	if (ndx<0 || ndx>=m_numPoints) return;
	Int i;
	for (i=ndx; i<m_numPoints-1; i++) {
		m_points[i] = m_points[i+1];
	}
	m_numPoints--;
	m_boundsNeedsUpdate = true;
	++s_shapeRevision;
}

void PolygonTrigger::getCenterPoint(Coord3D* pOutCoord)	const
{
	DEBUG_ASSERTCRASH(pOutCoord != NULL, ("pOutCoord was null. Non-Fatal, but shouldn't happen."));
	if (!pOutCoord) {
		return;
	}

	getCenterXY(pOutCoord);
	(*pOutCoord).z = TheTerrainLogic->getGroundHeight(pOutCoord->x, pOutCoord->y);
}

/** The middle of the bounds, z left alone: the render asks its own copy of the ground there */
void PolygonTrigger::getCenterXY(Coord3D* pOutCoord)	const
{
	if (m_boundsNeedsUpdate) {
		updateBounds();
	}
	(*pOutCoord).x = (m_bounds.lo.x + m_bounds.hi.x) / 2.0f;
	(*pOutCoord).y = (m_bounds.lo.y + m_bounds.hi.y) / 2.0f;
}

Real PolygonTrigger::getRadius(void)	const
{
	if (m_boundsNeedsUpdate) {
		updateBounds();
	}
	return m_radius;
}


/**
 PolygonTrigger - pointInTrigger.
*/
Bool PolygonTrigger::pointInTrigger(ICoord3D &point) const
{	
	if (m_boundsNeedsUpdate) {
		updateBounds();
	}
	if (point.x < m_bounds.lo.x) return false;
	if (point.y < m_bounds.lo.y) return false;
	if (point.x > m_bounds.hi.x) return false;
	if (point.y > m_bounds.hi.y) return false;

	Bool inside = false;
	Int i;
	for (i=0; i<m_numPoints; i++) {
		ICoord3D pt1 = m_points[i];
		ICoord3D pt2;
		if (i==m_numPoints-1) {
			pt2 = m_points[0];
		} else {
			pt2 = m_points[i+1];
		}
		if (pt1.y == pt2.y) {
			continue; // ignore horizontal lines.
		}
		if (pt1.y < point.y && pt2.y < point.y) continue;
		if (pt1.y >= point.y && pt2.y >= point.y) continue;
		if (pt1.x<point.x && pt2.x < point.x) continue;
		// Line segment crosses ray from point x->infinity.
		Int dy = pt2.y-pt1.y;
		Int dx = pt2.x-pt1.x;

		Real intersectionX = pt1.x + (dx * (point.y-pt1.y)) / ((Real)dy);
		if (intersectionX >= point.x) {
			inside = !inside;
		}
	}
	return inside;
}

// ------------------------------------------------------------------------------------------------
const WaterHandle* PolygonTrigger::getWaterHandle(void)	const
{

	if( isWaterArea() )
		return &m_waterHandle;

	return NULL;  // this polygon trigger is not a water area

}

Bool PolygonTrigger::isValid(void) const
{
	if (m_numPoints == 0) {
		return FALSE;
	}

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PolygonTrigger::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void PolygonTrigger::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// number of data points
	xfer->xferInt( &m_numPoints );

	// xfer all data points
	ICoord3D *point;
	for( Int i = 0; i < m_numPoints; ++i )
	{

		// get this point
		point = &m_points[ i ];

		// xfer point
		xfer->xferICoord3D( point );

	}  // end for, i

	// bounds
	xfer->xferIRegion2D( &m_bounds );

	// radius
	xfer->xferReal( &m_radius );

	// bounds need update
	xfer->xferBool( &m_boundsNeedsUpdate );

	// the radius is derived from the points, and a save from before the half-height fix holds a wrong one
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_boundsNeedsUpdate = TRUE;
		++s_shapeRevision;
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PolygonTrigger::loadPostProcess( void )
{

}  // end loadPostProcess
