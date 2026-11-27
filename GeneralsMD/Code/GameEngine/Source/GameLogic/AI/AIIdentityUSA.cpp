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

// AIIdentityUSA.cpp
// The USA generals' own styles: USA, Laser, Superweapon and Air Force.  AIIdentity.h has the hooks.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AIIdentity.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/Module/SpecialPowerUpdateModule.h"

class AIIdentityUSA : public AIIdentity
{
public:
	enum
	{
		MOVE_BATTLE_PLANS = 1,	///< USA: the Strategy Center's plan follows the war (doBattlePlans)
	};

	AIIdentityUSA( AIPlayer *ai, AIGeneral general ) : AIIdentity( ai, general ), m_planWaveFrame( 0 ), m_planWanted( PLANSTATUS_NONE ), m_planSwitchFrame( 0 ) {}

	virtual void onStart( void );
	virtual void onTick( void );
	virtual void onWaveSent( AIGroup *wave, Real power );
	virtual void xfer( Xfer *xfer );

private:
	void doBattlePlans( void );

	UnsignedInt		m_planWaveFrame;			///< the last wave sent, 0 for none
	Int						m_planWanted;					///< the plan (PLANSTATUS_*) that wave wants
	UnsignedInt		m_planSwitchFrame;		///< the last plan change ordered, 0 for none
};

AIIdentity *createUSAIdentity( AIPlayer *ai, AIGeneral general )
{
	return new AIIdentityUSA( ai, general );
}

void AIIdentityUSA::onStart( void )
{
	if( m_general == AIGENERAL_USA )
		drawMove( MOVE_BATTLE_PLANS, 100 );
	DEBUG_LOG(("AI IDENTITY frame %d player %d '%s' moves 0x%x\n", TheGameLogic->getFrame(), m_ai->m_player->getPlayerIndex(),
		m_ai->m_player->getSide().str(), m_moves));
}

void AIIdentityUSA::onTick( void )
{
	doBattlePlans();
}

void AIIdentityUSA::xfer( Xfer *xfer )
{
	AIIdentity::xfer( xfer );
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	xfer->xferUnsignedInt( &m_planWaveFrame );
	xfer->xferInt( &m_planWanted );
	xfer->xferUnsignedInt( &m_planSwitchFrame );
}

/** The scripts set Hold the Line once and leave it.  A plan change stuns every unit it covers for five seconds
	* (BattlePlanChangeParalyzeTime), so the plan moves only on the turns that are quiet anyway: when a wave sets out
	* (Bombardment's 20% damage when it walks into known guns, Search and Destroy's 20% range and sight otherwise)
	* and when the pressure turns to defence (Hold the Line's tenth off the damage taken).  Under attack it changes
	* nothing, and it waits BATTLE_PLAN_SWITCH_FRAMES between two changes. */
static const UnsignedInt BATTLE_PLAN_CHECK_FRAMES = (UnsignedInt)( 2 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt BATTLE_PLAN_SWITCH_FRAMES = (UnsignedInt)( 45 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt BATTLE_PLAN_WAVE_FRAMES = (UnsignedInt)( 20 * LOGICFRAMES_PER_GAME_SECOND );	///< how long after a send its plan may still be put up
static const Real BATTLE_PLAN_SIEGE_SHARE = 0.25f;	///< known guns on its road worth this share of the wave make it a siege

void AIIdentityUSA::onWaveSent( AIGroup *wave, Real power )
{
	if( !hasMove( MOVE_BATTLE_PLANS ) )
		return;
	const Bool siege = m_ai->m_gunHoldSince != 0 || m_ai->guardedRoad() >= BATTLE_PLAN_SIEGE_SHARE * power;
	m_planWaveFrame = TheGameLogic->getFrame();
	m_planWanted = siege ? PLANSTATUS_BOMBARDMENT : PLANSTATUS_SEARCHANDDESTROY;
}

static void collectStrategyCenters( Object *obj, void *userData )
{
	if( obj->isKindOf( KINDOF_FS_STRATEGY_CENTER ) )
		((std::vector<Object *> *)userData)->push_back( obj );
}

void AIIdentityUSA::doBattlePlans( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now % BATTLE_PLAN_CHECK_FRAMES != 0 || !hasMove( MOVE_BATTLE_PLANS ) )
		return;
	if( m_planSwitchFrame != 0 && now - m_planSwitchFrame < BATTLE_PLAN_SWITCH_FRAMES )
		return;
	Int want = PLANSTATUS_NONE;
	if( m_planWaveFrame != 0 && now - m_planWaveFrame < BATTLE_PLAN_WAVE_FRAMES )
		want = m_planWanted;
	else if( m_ai->m_pressure == AIPRESSURE_DEFEND && !m_ai->isBaseUnderAttack() )
		want = PLANSTATUS_HOLDTHELINE;
	if( want == PLANSTATUS_NONE )
		return;
	const UnsignedInt option = want == PLANSTATUS_BOMBARDMENT ? OPTION_ONE : ( want == PLANSTATUS_HOLDTHELINE ? OPTION_TWO : OPTION_THREE );

	Player *player = m_ai->m_player;
	std::vector<Object *> centers;
	player->iterateObjects( collectStrategyCenters, &centers );
	for( size_t o = 0; o < centers.size(); ++o )
	{
		Object *center = centers[ o ];
		if( center->isEffectivelyDead() || center->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || center->testStatus( OBJECT_STATUS_SOLD ) )
			continue;
		// the plan it was last told, whoever told it
		UnsignedInt current = 0;
		for( BehaviorModule **m = center->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerUpdateInterface *sp = (*m)->getSpecialPowerUpdateInterface();
			if( sp && sp->getCommandOption() != 0 )
				current = (UnsignedInt)sp->getCommandOption();
		}
		if( current == option )
			continue;
		const CommandSet *commandSet = TheCommandStore->findCommandSet( center->getCommandSetString() );
		if( commandSet == NULL )
			continue;
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = commandSet->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_SPECIAL_POWER || button->getSpecialPowerTemplate() == NULL ||
					button->getSpecialPowerTemplate()->getSpecialPowerType() != SPECIAL_CHANGE_BATTLE_PLANS || ( button->getOptions() & option ) == 0 )
				continue;
			DEBUG_LOG(("AI PLAN frame %d player %d sets plan %d (was option %d)%s\n", now, player->getPlayerIndex(), want, current,
				want == PLANSTATUS_HOLDTHELINE ? ", outmatched" : ( want == PLANSTATUS_BOMBARDMENT ? ", wave into guns" : ", wave out" )));
			center->doCommandButton( button, CMD_FROM_AI );
			m_planSwitchFrame = now;
			break;
		}
	}
}
