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

// W3DDynamicLight.cpp
// Class to handle dynamic lights.
// Author: John Ahlquist, April 2001
#include <stdlib.h>

#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DSmoothMotion.h"
#include "Common/GlobalData.h"
#include "GameClient/GameClient.h"		// for the logic-frame gate in On_Frame_Update

W3DDynamicLight::W3DDynamicLight(void):
LightClass(LightClass::POINT)
{

	m_priorEnable = false;
	m_enabled = true;
	m_lastFadeFrame = 0xffffffff;	// so the first On_Frame_Update always steps
	m_fadeFrom = m_fadeTo = 1.0f;
	m_fadeStepped = false;
	m_fadeBirth = false;
	m_fadeBirthSpan = 1.0f;

}

W3DDynamicLight::~W3DDynamicLight(void)
{
}

void W3DDynamicLight::On_Frame_Update(void)
{	
	if (!m_enabled) {
		return;
	}

	//
	// m_curIncreaseFrameCount/m_curDecayFrameCount are LOGIC frame counts, but this is called
	// once per RENDER frame from RTS3DScene::Customized_Render. With rendering uncapped that
	// made muzzle flashes and explosion lights fade at (fps/30)x speed. Step once per logic frame.
	//
	UnsignedInt now = TheGameClient ? TheGameClient->getFrame() : 0;
	if (now == m_lastFadeFrame) {
		// Between ticks the Reforged look shows the strength part way from the last tick's, on the
		// picture's clock as the models glide (W3DSmoothMotion.h); a whole tick on it is the tick's own.
		if (m_fadeStepped && TheSmoothMotionActive && !TheGlobalData->m_classicGraphics && TheSmoothMotionAlpha < 1.0f) {
			applyFade(SmoothMotion_Lerp(m_fadeFrom, m_fadeTo, fadeAlpha()));
		}
		else if (m_fadeStepped) {
			// a whole tick on after a blended picture (the logic late, smooth motion switched off): the
			// tick's own strength again, the step's arithmetic, so it does not stay at the blend
			applyFade(m_fadeTo);
		}
		return;
	}
	m_lastFadeFrame = now;

	Real factor = 1.0f;
	if (m_curIncreaseFrameCount>0 && m_increaseFrameCount>0) {
		// increasing 
		m_curIncreaseFrameCount--;
		factor = (m_increaseFrameCount-m_curIncreaseFrameCount)/(Real)m_increaseFrameCount;

	}	else if (m_decayFrameCount==0) {
		factor = 1.0;  // never decays,
	}	else {
		m_curDecayFrameCount--;
		if (m_curDecayFrameCount == 0) {
			m_enabled = false;
			return;
		}
		factor = m_curDecayFrameCount/(Real)m_decayFrameCount;
	}
	m_fadeFrom = m_fadeTo;
	m_fadeTo = factor;
	m_fadeBirth = !m_fadeStepped;
	m_fadeStepped = true;
	// the tick's own strength, unless this picture is part way into the tick
	if (TheSmoothMotionActive && !TheGlobalData->m_classicGraphics && TheSmoothMotionAlpha < 1.0f)
		factor = SmoothMotion_Lerp(m_fadeFrom, m_fadeTo, fadeAlpha());
	applyFade(factor);
}

/// How far between the last tick's strength and this one's the picture shows it: the smooth motion
/// alpha, run faster through a short pulse's first tick (SmoothMotion_LightBirthSpan).
Real W3DDynamicLight::fadeAlpha(void) const
{
	return m_fadeBirth ? SmoothMotion_Hurry(TheSmoothMotionAlpha, m_fadeBirthSpan) : TheSmoothMotionAlpha;
}

void W3DDynamicLight::applyFade(Real factor)
{
	if (m_decayRange) {
		this->FarAttenEnd = factor*m_targetRange;
		if (FarAttenEnd < FarAttenStart) {
			FarAttenEnd = FarAttenStart;
		}
	}
	if (m_decayColor) {
		this->Ambient = m_targetAmbient*factor;
		this->Diffuse = m_targetDiffuse*factor;
	}
}

void W3DDynamicLight::setFrameFade(UnsignedInt frameIncreaseTime, UnsignedInt decayFrameTime)
{
	m_decayFrameCount = decayFrameTime;
	m_curDecayFrameCount = decayFrameTime;
	m_curIncreaseFrameCount = frameIncreaseTime;
	m_increaseFrameCount = frameIncreaseTime;
	m_targetAmbient = Ambient;
	m_targetDiffuse = Diffuse;
	m_targetRange = FarAttenEnd;
	// the strength the first tick's step fades from: none, so a pulse comes up over the tick that brings
	// it rather than lighting the ground on the tick, faster for a short flash; full for one that lives a
	// tick (it would not show at all)
	m_fadeBirthSpan = SmoothMotion_LightBirthSpan(frameIncreaseTime, decayFrameTime);
	m_fadeTo = m_fadeBirthSpan > 0.0f ? 0.0f : 1.0f;
	m_fadeFrom = m_fadeTo;
	m_fadeStepped = false;
	m_fadeBirth = false;
}