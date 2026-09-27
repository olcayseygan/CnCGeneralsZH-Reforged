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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// SimpleObjectIterator
// Implementation of a simple object iterator
// Author: Steven Johnson, September 2001
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameLogic/ObjectIter.h"

#include "Common/ThingTemplate.h"
#include "GameLogic/Object.h"
#include "Lib/FixBoundary.h"


/// @todo Doxygenize this file

SimpleObjectIterator::ClumpCompareProc SimpleObjectIterator::theClumpCompareProcs[] = 
{ 
	NULL,						// "fastest" gets no proc
	SimpleObjectIterator::sortNearToFar,
	SimpleObjectIterator::sortFarToNear,
	SimpleObjectIterator::sortCheapToExpensive,
	SimpleObjectIterator::sortExpensiveToCheap
};

//=============================================================================
SimpleObjectIterator::Clump::Clump()
{
	m_nextClump = NULL;
}

//=============================================================================
SimpleObjectIterator::Clump::~Clump()
{
}

//=============================================================================
SimpleObjectIterator::SimpleObjectIterator()
{
	m_firstClump = NULL;
	m_curClump = NULL;
	m_clumpCount = 0;
}

//=============================================================================
SimpleObjectIterator::~SimpleObjectIterator()
{
	makeEmpty();
}

//=============================================================================
/* Recycling these through a free list instead of the pool was tried and measured at nothing.

	 A gathering range query keeps every object it finds in one of these and an eight-player match
	 makes 111 such queries a logic frame, so this is thousands of pool allocations and frees a
	 frame, every one of them taking the global memory lock that THREADING-ROADMAP section 1.1 is
	 about. Recycling removes all of them, comes back bit-identical over four matches, and moves the
	 mean logic frame from 1.24ms to 1.26 - which is to say it does nothing. The pool is a free list
	 already and the lock is uncontended in a single-threaded logic frame, so the 16,523
	 critical-section operations a frame are cheap operations. Worth knowing before anybody spends a
	 fortnight on the allocator. */
void SimpleObjectIterator::insert(Object *obj, Real numeric)
{
	insertFix(obj, fixFromReal(numeric));
}

void SimpleObjectIterator::insertFix(Object *obj, Fix numeric)
{
	DEBUG_ASSERTCRASH(obj, ("sorry, no nulls allowed here"));

	Clump *clump = newInstance(Clump)();

	clump->m_nextClump = m_firstClump;
	m_firstClump = clump;

	clump->m_obj = obj;
	clump->m_numeric = numeric;

	++m_clumpCount;
}

//=============================================================================
Object *SimpleObjectIterator::nextWithNumericFix(Fix *num)
{
	Object *obj = NULL;
	if (num)
		*num = Fix(0);

	if (m_curClump)
	{
		obj = m_curClump->m_obj;
		if (num)
			*num = m_curClump->m_numeric;
		m_curClump = m_curClump->m_nextClump;
	}

	return obj;
}

//=============================================================================
Object *SimpleObjectIterator::nextWithNumeric(Real *num)
{
	Fix f;
	Object *obj = nextWithNumericFix(num ? &f : NULL);
	if (num)
		*num = fixToReal(f);
	return obj;
}

//=============================================================================
void SimpleObjectIterator::reset()
{
	m_curClump = m_firstClump;
}

//=============================================================================
void SimpleObjectIterator::makeEmpty()
{
	while (m_firstClump)
	{
		Clump *next = m_firstClump->m_nextClump;
		m_firstClump->deleteInstance();
		m_firstClump = next;
		--m_clumpCount;
	}
	DEBUG_ASSERTCRASH(m_clumpCount == 0, ("hmm"));

	m_firstClump = NULL;
	m_curClump = NULL;
	m_clumpCount = 0;
}

//=============================================================================
void SimpleObjectIterator::sort(IterOrderType order)
{
	if (m_clumpCount == 0)
		return;

#ifdef INTENSE_DEBUG
{
	DEBUG_LOG(("\n\n---------- BEFORE sort for %d -----------\n",order));
	for (Clump *p = m_firstClump; p; p = p->m_nextClump)
	{
		DEBUG_LOG(("    obj %08lx numeric %f\n",p->m_obj,p->m_numeric));
	}
}
#endif

	ClumpCompareProc cmpProc = theClumpCompareProcs[order];

	if (!cmpProc)
		return;	// my, that was easy

	// do a basic mergesort, which works nicely for linked lists,
	// and is reasonably efficient (N log N).

  for ( Int n = 1 ; ; n *= 2 ) 
	{
		Clump *to_do = m_firstClump;
		Clump *tail = NULL;
		m_firstClump = NULL;

		Int mergeCount = 0;

		while (to_do) 
		{
			++mergeCount;

			Int to_do_count = 0;
			
			// make two lists of length 'n' (to_do is one, sub is the other)
			Clump *sub = to_do;
			for (Int i = 0; i < n; i++) 
			{
				++to_do_count;
				sub = sub->m_nextClump;
				if (!sub) 
					break;
			}
			Int subCount = sub ? n : 0;

			// merge the two lists.
			DEBUG_ASSERTCRASH(to_do_count + subCount >= 0, ("uhoh"));
			while (to_do_count + subCount > 0) {

				DEBUG_ASSERTCRASH(to_do_count + subCount >= 0, ("uhoh"));
	
				Clump *tmp;
				
				// bleah, coalesce into more elegant test case
				if (subCount == 0)
				{
					DEBUG_ASSERTCRASH(to_do_count > 0, ("hmm, expected nonzero to_do_count"));
					tmp = to_do; 
					to_do = to_do->m_nextClump; 
					--to_do_count;
				}
				else if (to_do_count == 0)
				{
					DEBUG_ASSERTCRASH(subCount > 0, ("hmm, expected nonzero subCount"));
					tmp = sub; 
					sub = sub->m_nextClump; 
					--subCount;
				} 
				else if ((*cmpProc)(to_do, sub) <= 0)
				{
					DEBUG_ASSERTCRASH(to_do_count > 0, ("hmm, expected nonzero to_do_count"));
					tmp = to_do; 
					to_do = to_do->m_nextClump; 
					--to_do_count;
				} 
				else 
				{
					DEBUG_ASSERTCRASH(subCount > 0, ("hmm, expected nonzero subCount"));
					tmp = sub; 
					sub = sub->m_nextClump; 
					--subCount;
				}
				if (!sub) subCount = 0;
				if (!to_do) to_do_count = 0;

				if (tail) 
					tail->m_nextClump = tmp;
				else 
					m_firstClump = tmp;
				tail = tmp;
			}

			to_do = sub;
		}
		if (tail)
			tail->m_nextClump = NULL;

		if (mergeCount <= 1)	// when we have done just one (or none) swap, we're done
			break;
	}

#ifdef INTENSE_DEBUG
{
	DEBUG_LOG(("\n\n---------- sort for %d -----------\n",order));
	for (Clump *p = m_firstClump; p; p = p->m_nextClump)
	{
		DEBUG_LOG(("    obj %08lx numeric %f\n",p->m_obj,p->m_numeric));
	}
}
#endif

	// always reset after sorting, to prevent weirdness
	reset();
}

//-----------------------------------------------------------------------------
Int SimpleObjectIterator::sortNearToFar(Clump *a, Clump *b)
{
	return a->m_numeric < b->m_numeric ? -1 : (a->m_numeric > b->m_numeric ? 1 : 0);
}

//-----------------------------------------------------------------------------
Int SimpleObjectIterator::sortFarToNear(Clump *a, Clump *b)
{
	return sortNearToFar(b, a);
}

//-----------------------------------------------------------------------------
Int SimpleObjectIterator::sortCheapToExpensive(Clump *a, Clump *b)
{
	return a->m_obj->getTemplate()->friend_getBuildCost() -
				 b->m_obj->getTemplate()->friend_getBuildCost();
}

//-----------------------------------------------------------------------------
Int SimpleObjectIterator::sortExpensiveToCheap(Clump *a, Clump *b)
{
	return b->m_obj->getTemplate()->friend_getBuildCost() -
				 a->m_obj->getTemplate()->friend_getBuildCost();
}

