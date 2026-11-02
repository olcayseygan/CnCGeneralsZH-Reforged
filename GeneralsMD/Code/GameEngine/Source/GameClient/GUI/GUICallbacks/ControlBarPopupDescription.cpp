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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: ControlBarPopupDescription.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Sep 2002
//
//	Filename: 	ControlBarPopupDescription.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------


// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Common/GlobalData.h"
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/ControlBar.h"
#include "GameClient/DisplayStringManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/FireWeaponWhenDeadBehavior.h"
#include "GameLogic/Module/MaxHealthUpgrade.h"
#include "GameLogic/Module/OverchargeBehavior.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/ScriptEngine.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"		// WideCharStringToMultiByte
#include "GameLogic/Weapon.h"		// WeaponTemplate/WeaponBonus for the build tooltip's figures
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

static WindowLayout *theLayout = NULL;
static GameWindow *theWindow = NULL;
static AnimateWindowManager *theAnimateWindowManager = NULL;
static GameWindow *prevWindow = NULL;
static Bool useAnimation = FALSE;

//-------------------------------------------------------------------------------------------------
/** " (Ctrl+Shift+I)" for the key the player's command map gives a meta event in a match, empty for
	* one with no key.  The Idle Worker button has no letter of its own: its key is a chord in the
	* command map, so the tooltip is the only place the button can say what it is. */
//-------------------------------------------------------------------------------------------------
static UnicodeString metaKeySuffix( GameMessage::Type meta )
{
	UnicodeString suffix;
	for( const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next )
	{
		if( rec->m_meta != meta || rec->m_transition != DOWN || !( rec->m_usableIn & COMMANDUSABLE_GAME ) )
			continue;

		const char *keyName = NULL;
		for( const LookupListRec *k = KeyNames; k->name; ++k )
			if( k->value == rec->m_key )
				keyName = k->name + 4;	// past "KEY_"
		if( keyName == NULL || rec->m_key == MK_NONE )
			continue;

		AsciiString text( " (" );
		if( rec->m_modState & CTRL )
			text.concat( "Ctrl+" );
		if( rec->m_modState & ALT )
			text.concat( "Alt+" );
		if( rec->m_modState & SHIFT )
			text.concat( "Shift+" );
		text.concat( keyName );
		text.concat( ")" );
		suffix.translate( text );
		break;
	}
	return suffix;
}

//-------------------------------------------------------------------------------------------------
/** What the build tooltip says beyond retail's name, price and description, piece by piece, the
	* way Reforged's card laid it out; ControlBarPopupDescription.wnd gets it as lines of its text. */
//-------------------------------------------------------------------------------------------------
/** One line of what an upgrade does: the figure under `label`, a string table label, goes from `from` to `to`. */
struct BuildTooltipChange
{
	const char *label;
	std::string from;
	std::string to;
};

/** An upgrade and what it does to one unit.  On a unit's card `name` is the upgrade's; on an
	* upgrade's card it is the unit's. */
struct BuildTooltipUpgrade
{
	UnicodeString name;
	Bool owned;										///< the player has it already
	std::vector< BuildTooltipChange > changes;
};

struct BuildTooltipCard
{
	Bool hasStats;								///< a unit's card: the figures below are there
	Int buildSeconds;
	Int health;										///< 0 for a body that cannot be hurt
	Int damage;										///< the main weapon's, with the upgrades the player owns; 0 for no weapon
	Int range;
	Real attacksPerSecond;				///< shots a second over a whole clip and its reload
	Int damagePerSecond;
	std::vector< BuildTooltipUpgrade > upgrades;	///< a unit's upgrades, or the units an upgrade changes
};

/** One weapon slot's figures; `weapon` NULL for a slot with nothing in it that hurts. */
struct WeaponFigures
{
	const WeaponTemplate *weapon;
	Real damage;
	Real range;
	Real attacksPerSecond;
};

/** A unit's health and its weapons, slot by slot. */
struct UnitFigures
{
	Real health;
	WeaponFigures slots[ WEAPONSLOT_COUNT ];

	/** The slot with the most damage a second, WEAPONSLOT_COUNT for an unarmed unit. */
	Int mainSlot( void ) const
	{
		Int best = WEAPONSLOT_COUNT;
		for( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot )
			if( slots[ slot ].weapon && ( best == WEAPONSLOT_COUNT || slots[ slot ].damage * slots[ slot ].attacksPerSecond
																																> slots[ best ].damage * slots[ best ].attacksPerSecond ) )
				best = slot;
		return best;
	}
};

//-------------------------------------------------------------------------------------------------
/** What an upgrade does to a unit, as far as the build tooltip counts it: the modules it triggers
	* on that unit, added together. */
//-------------------------------------------------------------------------------------------------
struct UpgradeEffect
{
	Bool weaponSet;				///< WeaponSetUpgrade: the PLAYER_UPGRADE weapon set
	Bool weaponBonus;			///< WeaponBonusUpgrade: the PLAYER_UPGRADE weapon bonus
	Bool armor;						///< ArmorUpgrade, which the tooltip names without a figure
	Real addHealth;				///< MaxHealthUpgrade
};

static void addEffect( UpgradeEffect &into, const UpgradeEffect &effect )
{
	into.weaponSet = into.weaponSet || effect.weaponSet;
	into.weaponBonus = into.weaponBonus || effect.weaponBonus;
	into.armor = into.armor || effect.armor;
	into.addHealth += effect.addHealth;
}

typedef std::map< std::string, UpgradeEffect > UpgradeEffects;

//-------------------------------------------------------------------------------------------------
/** Every upgrade that triggers one of `thing`'s weapon, armour or health upgrade modules, by the
	* upgrade's name, and what it does.  A module that wants all its triggers at once is counted under
	* each of them alone. */
// ponytail: RequiresAllTriggers is read as any trigger; no stock unit hangs a figure on two upgrades at once
//-------------------------------------------------------------------------------------------------
static void findUpgradeEffects( const ThingTemplate *thing, UpgradeEffects &effects )
{
	const ModuleInfo &modules = thing->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
	{
		const AsciiString &module = modules.getNthName( m );
		UpgradeEffect effect = {};
		if( module.compareNoCase( "WeaponSetUpgrade" ) == 0 )
			effect.weaponSet = TRUE;
		else if( module.compareNoCase( "WeaponBonusUpgrade" ) == 0 )
			effect.weaponBonus = TRUE;
		else if( module.compareNoCase( "ArmorUpgrade" ) == 0 )
			effect.armor = TRUE;
		else if( module.compareNoCase( "MaxHealthUpgrade" ) == 0 )
			effect.addHealth = static_cast< const MaxHealthUpgradeModuleData * >( modules.getNthData( m ) )->m_addMaxHealth;
		else
			continue;

		const UpgradeMuxData &mux = static_cast< const UpgradeModuleData * >( modules.getNthData( m ) )->m_upgradeMuxData;
		for( size_t trigger = 0; trigger < mux.m_activationUpgradeNames.size(); ++trigger )
			addEffect( effects[ mux.m_activationUpgradeNames[ trigger ].str() ], effect );
	}
}

//-------------------------------------------------------------------------------------------------
/** The bomb `thing` sets off when it kills itself: the largest death weapon of a FireWeaponWhenDead
	* module that is on from the start and fires on a SUICIDED death.  The Terrorist, and a bike with one
	* riding it, shoot TerroristSuicideWeapon, 999999 damage that reaches nobody but the shooter, so that
	* his death fires the real bomb. */
// ponytail: a bomb an upgrade swaps in (Demo's fire pack, Anthrax Beta and Gamma) is not read; the object's live modules would have it
//-------------------------------------------------------------------------------------------------
static const WeaponTemplate *suicideBomb( const ThingTemplate *thing )
{
	const WeaponTemplate *best = NULL;
	const ModuleInfo &modules = thing->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
	{
		if( modules.getNthName( m ).compareNoCase( "FireWeaponWhenDeadBehavior" ) != 0 )
			continue;
		const FireWeaponWhenDeadBehaviorModuleData *data = static_cast< const FireWeaponWhenDeadBehaviorModuleData * >( modules.getNthData( m ) );
		if( !data->m_initiallyActive || !data->m_upgradeMuxData.m_activationUpgradeNames.empty() || data->m_deathWeapon == NULL
				|| !getDeathTypeFlag( data->m_dieMuxData.m_deathTypes, DEATH_SUICIDED ) )
			continue;
		const WeaponBonus none;
		if( best == NULL || data->m_deathWeapon->getPrimaryDamage( none ) > best->getPrimaryDamage( none ) )
			best = data->m_deathWeapon;
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
/** A clip fires its shots a delay apart and then reloads in place of the last delay.  A weapon that
	* does no damage, the dummies that hold a slot until an upgrade fills it, is none, and so is one
	* that heals, clears or unloads: a dozer's mine disarming "weapon" does 1 damage at a range of 5
	* so that it can be aimed, and an Ambulance's cleanup is the same.  A weapon that reaches nobody but
	* its user stands for the bomb his death fires, and one whose blast kills its user goes off once,
	* so it has no rate and no damage a second. */
//-------------------------------------------------------------------------------------------------
static WeaponFigures weaponFigures( const ThingTemplate *thing, const WeaponTemplate *weapon, const WeaponBonus &bonus )
{
	WeaponFigures figure = {};
	if( ( weapon->getAffectsMask() & ( WEAPON_AFFECTS_ALLIES | WEAPON_AFFECTS_ENEMIES | WEAPON_AFFECTS_NEUTRALS ) ) == 0 )
	{
		weapon = suicideBomb( thing );
		if( weapon == NULL )
			return figure;
	}
	const DamageType type = weapon->getDamageType();
	if( type == DAMAGE_HEALING || type == DAMAGE_DISARM || type == DAMAGE_HAZARD_CLEANUP || type == DAMAGE_DEPLOY
			|| !IsHealthDamagingDamage( type ) )
		return figure;

	const Real damage = weapon->getPrimaryDamage( bonus );
	if( damage <= 0.0f )
		return figure;
	const Real delay = max( 1.0f, INT_TO_REAL( weapon->getMinDelayBetweenShots() + weapon->getMaxDelayBetweenShots() ) * 0.5f
																	/ bonus.getField( WeaponBonus::RATE_OF_FIRE ) );
	const Int clip = weapon->getClipSize();
	const Real cycle = clip > 0 ? ( clip - 1 ) * delay + max( 1, weapon->getClipReloadTime( bonus ) ) : delay;

	figure.weapon = weapon;
	figure.damage = damage;
	figure.range = weapon->getAttackRange( bonus );
	figure.attacksPerSecond = weapon->getAffectsMask() & WEAPON_KILLS_SELF ? 0.0f : ( clip > 0 ? clip : 1 ) * LOGICFRAMES_PER_SECOND / cycle;
	return figure;
}

//-------------------------------------------------------------------------------------------------
/** `thing`'s figures with `effect` applied, read off the template the way Weapon::computeBonus
	* reads an object: the game's own weapon bonuses and the weapon's extra ones. */
//-------------------------------------------------------------------------------------------------
static UnitFigures figuresOf( const ThingTemplate *thing, const UpgradeEffect &effect )
{
	UnitFigures figures = {};
	figures.health = thing->calcMaxHealth() + effect.addHealth;

	WeaponSetFlags setFlags;
	if( effect.weaponSet )
		setFlags.set( WEAPONSET_PLAYER_UPGRADE );
	const WeaponBonusConditionFlags bonusFlags = effect.weaponBonus ? ( 1 << WEAPONBONUSCONDITION_PLAYER_UPGRADE ) : 0;
	const WeaponTemplateSet *set = thing->findWeaponTemplateSet( setFlags );
	for( Int slot = PRIMARY_WEAPON; set && slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponTemplate *weapon = set->getNth( (WeaponSlotType)slot );
		if( weapon == NULL )
			continue;

		WeaponBonus bonus;
		TheGlobalData->m_weaponBonusSet->appendBonuses( bonusFlags, bonus );
		if( weapon->getExtraBonus() )
			weapon->getExtraBonus()->appendBonuses( bonusFlags, bonus );
		figures.slots[ slot ] = weaponFigures( thing, weapon, bonus );
	}
	return figures;
}

static std::string wholeFigure( Real value )
{
	return std::to_string( REAL_TO_INT( value ) );
}

/** Attacks a second, two decimals and the string table's unit: "1.25/s". */
static std::string rateFigure( Real value )
{
	char text[ 32 ];
	snprintf( text, sizeof( text ), "%.2f", value );
	return text + WideCharStringToMultiByte( TheGameText->fetch( "TOOLTIP:StatPerSecond" ).str() );
}

//-------------------------------------------------------------------------------------------------
/** The lines that tell `from` and `to` apart.  The main weapon is compared figure by figure when
	* the upgrade leaves it in its slot; any other weapon the upgrade puts in, a Ranger's grenade or a
	* Humvee's missile, is a line of its own with its damage and range.  Then the armour, when
	* `effect` changes it. */
//-------------------------------------------------------------------------------------------------
static void putChanges( const UnitFigures &from, const UnitFigures &to, const UpgradeEffect &effect,
												std::vector< BuildTooltipChange > &changes )
{
	if( wholeFigure( from.health ) != wholeFigure( to.health ) )
	{
		const BuildTooltipChange health = { "TOOLTIP:StatHealth", wholeFigure( from.health ), wholeFigure( to.health ) };
		changes.push_back( health );
	}

	const Int main = from.mainSlot();
	if( main != WEAPONSLOT_COUNT && to.slots[ main ].weapon )
	{
		const WeaponFigures &before = from.slots[ main ];
		const WeaponFigures &after = to.slots[ main ];
		const BuildTooltipChange damage = { "TOOLTIP:StatDamage", wholeFigure( before.damage ), wholeFigure( after.damage ) };
		const BuildTooltipChange speed = { "TOOLTIP:StatAttackSpeed", rateFigure( before.attacksPerSecond ), rateFigure( after.attacksPerSecond ) };
		const BuildTooltipChange perSecond = { "TOOLTIP:StatDamagePerSecond", wholeFigure( before.damage * before.attacksPerSecond ),
																				 wholeFigure( after.damage * after.attacksPerSecond ) };
		const BuildTooltipChange range = { "TOOLTIP:StatRange", wholeFigure( before.range ), wholeFigure( after.range ) };

		const BuildTooltipChange *candidates[] = { &damage, &speed, &perSecond, &range };
		for( Int each = 0; each < (Int)ARRAY_SIZE( candidates ); ++each )
			if( candidates[ each ]->from != candidates[ each ]->to )
				changes.push_back( *candidates[ each ] );
	}

	for( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponFigures &after = to.slots[ slot ];
		if( ( slot == main && main != WEAPONSLOT_COUNT && after.weapon ) || after.weapon == NULL || after.weapon == from.slots[ slot ].weapon )
			continue;

		// named by what it aims at, since TOW puts in one missile for the ground and one for the air
		const Int aims = after.weapon->getAntiMask();
		const Bool atGround = ( aims & WEAPON_ANTI_GROUND ) != 0;
		const Bool atAir = ( aims & WEAPON_ANTI_AIRBORNE_VEHICLE ) != 0;
		const char *label = atGround == atAir ? "TOOLTIP:StatNewWeapon" : ( atAir ? "TOOLTIP:StatNewWeaponAir" : "TOOLTIP:StatNewWeaponGround" );

		UnicodeString figures;
		figures.format( TheGameText->fetch( "TOOLTIP:StatNewWeaponFigures" ), REAL_TO_INT( after.damage ), REAL_TO_INT( after.range ) );
		const BuildTooltipChange weapon = { label, "", WideCharStringToMultiByte( figures.str() ) };
		changes.push_back( weapon );
	}

	if( effect.armor )
	{
		const BuildTooltipChange armor = { "TOOLTIP:StatArmor", "", WideCharStringToMultiByte( TheGameText->fetch( "TOOLTIP:StatArmorStronger" ).str() ) };
		changes.push_back( armor );
	}
}

//-------------------------------------------------------------------------------------------------
/** The effect of every upgrade in `effects` the player has, leaving out `leftOut`. */
//-------------------------------------------------------------------------------------------------
static UpgradeEffect ownedEffect( const UpgradeEffects &effects, const Player *player, const std::string &leftOut )
{
	UpgradeEffect owned = {};
	for( UpgradeEffects::const_iterator upgrade = effects.begin(); upgrade != effects.end(); ++upgrade )
	{
		const UpgradeTemplate *known = TheUpgradeCenter->findUpgrade( upgrade->first.c_str() );
		if( upgrade->first != leftOut && known && player->hasUpgradeComplete( known ) )
			addEffect( owned, upgrade->second );
	}
	return owned;
}

//-------------------------------------------------------------------------------------------------
/** What the build button that sells `upgrade` calls it, so a unit's card and the upgrade's own card
	* say the same name: the upgrade's own label reads "Ranger Flash-Bang Grenades" where its button
	* reads "Flash-Bang Grenades".  The selection's own buttons first, since several buttons sell one
	* upgrade under labels that differ ("Flash-Bang Grenade" on one, "Grenades" on the Barracks'); then
	* any button; the upgrade's label when no button sells it. */
//-------------------------------------------------------------------------------------------------
static UnicodeString upgradeButtonName( const UpgradeTemplate *upgrade )
{
	const Drawable *selected = TheInGameUI->getFirstSelectedDrawable();
	const Object *seller = selected ? selected->getObject() : NULL;
	const CommandSet *set = seller ? TheControlBar->findCommandSet( seller->getCommandSetString() ) : NULL;
	for( Int slot = 0; set && slot < MAX_COMMANDS_PER_SET; ++slot )
	{
		const CommandButton *button = set->getCommandButton( slot );
		if( button && button->getUpgradeTemplate() == upgrade && button->getTextLabel().isNotEmpty() )
			return TheGameText->fetch( button->getTextLabel() );
	}

	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
		if( button->getUpgradeTemplate() == upgrade && button->getTextLabel().isNotEmpty() )
			return TheGameText->fetch( button->getTextLabel() );
	return TheGameText->fetch( upgrade->getDisplayNameLabel() );
}

//-------------------------------------------------------------------------------------------------
/** A unit's card: its figures with the upgrades the player owns, and each upgrade that changes one
	* of them with what it changes, from the figure without it to the figure with it. */
//-------------------------------------------------------------------------------------------------
static void putUnitFigures( BuildTooltipCard &card, const ThingTemplate *thing, const Player *player )
{
	UpgradeEffects effects;
	findUpgradeEffects( thing, effects );

	const UpgradeEffect owned = ownedEffect( effects, player, "" );
	const UnitFigures now = figuresOf( thing, owned );
	card.health = REAL_TO_INT( now.health );
	const Int main = now.mainSlot();
	if( main != WEAPONSLOT_COUNT )
	{
		const WeaponFigures &weapon = now.slots[ main ];
		card.damage = REAL_TO_INT( weapon.damage );
		card.range = REAL_TO_INT( weapon.range );
		card.attacksPerSecond = weapon.attacksPerSecond;
		card.damagePerSecond = REAL_TO_INT( weapon.damage * weapon.attacksPerSecond );
	}

	for( UpgradeEffects::const_iterator effect = effects.begin(); effect != effects.end(); ++effect )
	{
		const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( effect->first.c_str() );
		if( upgrade == NULL )
			continue;

		BuildTooltipUpgrade entry;
		entry.name = upgradeButtonName( upgrade );
		entry.owned = player->hasUpgradeComplete( upgrade );
		UpgradeEffect without = ownedEffect( effects, player, effect->first );
		UpgradeEffect with = without;
		addEffect( with, effect->second );
		putChanges( figuresOf( thing, without ), figuresOf( thing, with ), effect->second, entry.changes );
		if( !entry.changes.empty() )
			card.upgrades.push_back( entry );
	}
}

enum
{
	UPGRADE_TARGETS_SHOWN = 6,		///< the most units an upgrade's card lists
};

//-------------------------------------------------------------------------------------------------
/** An upgrade's card: the units of the player's side it changes a figure of, each with the change
	* from the plain unit.  The units are what the side's build buttons make, each name once: walking
	* every template found the campaign's copies of a Ranger or a Crusader as well, one of them with a
	* range of 999996.  Done only when the card is filled in, on hovering and when the bar changes. */
//-------------------------------------------------------------------------------------------------
static void putUpgradeTargets( BuildTooltipCard &card, const UpgradeTemplate *upgrade, const Player *player )
{
	const UpgradeEffect plain = {};
	std::set< WideCharString > named;
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		if( card.upgrades.size() >= UPGRADE_TARGETS_SHOWN )
			break;
		const ThingTemplate *thing = button->getThingTemplate();
		if( thing == NULL || thing->getDefaultOwningSide() != player->getSide() || named.count( thing->getDisplayName().str() ) )
			continue;

		UpgradeEffects effects;
		findUpgradeEffects( thing, effects );
		UpgradeEffects::const_iterator effect = effects.find( upgrade->getUpgradeName().str() );
		if( effect == effects.end() )
			continue;

		BuildTooltipUpgrade entry;
		entry.name = thing->getDisplayName();
		entry.owned = FALSE;
		putChanges( figuresOf( thing, plain ), figuresOf( thing, effect->second ), effect->second, entry.changes );
		if( entry.changes.empty() )
			continue;
		named.insert( thing->getDisplayName().str() );
		card.upgrades.push_back( entry );
	}
}

void ControlBarPopupDescriptionUpdateFunc( WindowLayout *layout, void *param )
{
	if(TheScriptEngine->isGameEnding())
		TheControlBar->hideBuildTooltipLayout();
	
	if(theAnimateWindowManager && !TheControlBar->getShowBuildTooltipLayout() && !theAnimateWindowManager->isReversed())
		theAnimateWindowManager->reverseAnimateWindow();
	else if(!TheControlBar->getShowBuildTooltipLayout() && (!TheGlobalData->m_animateWindows || !useAnimation))
		TheControlBar->deleteBuildTooltipLayout();
		

	if ( useAnimation && theAnimateWindowManager && TheGlobalData->m_animateWindows)
	{
		Bool wasFinished = theAnimateWindowManager->isFinished();
		theAnimateWindowManager->update();
		if (theAnimateWindowManager && theAnimateWindowManager->isFinished() && !wasFinished && theAnimateWindowManager->isReversed())
		{
			delete theAnimateWindowManager;
			theAnimateWindowManager = NULL;
			TheControlBar->deleteBuildTooltipLayout();
		}
	}

}

//
// How far the bar had travelled (minimised, or sliding in) the last time the tooltip was moved with
// it.  It belongs to one tooltip layout: a new layout, made when the resolution changes, stands where
// the loader put it, and carrying the old one's offset over lifted it by the whole minimised drop.
//
static ICoord2D theTooltipLastOffset = { 0, 0 };

void ControlBarPopupDescription_forgetOffset( void )
{
	theTooltipLastOffset.x = theTooltipLastOffset.y = 0;
}

// ---------------------------------------------------------------------------------------
void ControlBar::showBuildTooltipLayout( GameWindow *cmdButton )
{
	if (TheInGameUI->areTooltipsDisabled() 	|| TheScriptEngine->isGameEnding())
	{
		return;
	}

	Bool passedWaitTime = FALSE;
	static Bool isInitialized = FALSE;
	static UnsignedInt beginWaitTime;
	if(prevWindow == cmdButton)	
	{
		m_showBuildToolTipLayout = TRUE;
		if(!isInitialized &&  beginWaitTime + cmdButton->getTooltipDelay() < Clock_Milliseconds())
		{
			//DEBUG_LOG(("%d beginwaittime, %d tooltipdelay, %dtimegettime\n", beginWaitTime, cmdButton->getTooltipDelay(), timeGetTime()));
			passedWaitTime = TRUE;
		}
		
		if(!passedWaitTime)
			return;
	}
	else if( !m_buildToolTipLayout->isHidden() )
	{
		if(useAnimation && TheGlobalData->m_animateWindows && !theAnimateWindowManager->isReversed())
			theAnimateWindowManager->reverseAnimateWindow();
		else if( useAnimation && TheGlobalData->m_animateWindows && theAnimateWindowManager->isReversed())
		{
			return;
		}
		else
		{
//			m_buildToolTipLayout->destroyWindows();
//			m_buildToolTipLayout->deleteInstance();
//			m_buildToolTipLayout = NULL;
			m_buildToolTipLayout->hide(TRUE);
			prevWindow = NULL;
		}	
		return;
	}
	
	
	// will only get here the firsttime through the function through this window
	if(!passedWaitTime)
	{
		prevWindow = cmdButton;
		beginWaitTime = Clock_Milliseconds();
		isInitialized = FALSE;
		return;
	}
	isInitialized = TRUE;

	if(!cmdButton)
		return;
	UnicodeString portraitName, portraitDescription;
	if( describePortraitBarWindow( cmdButton, portraitName, portraitDescription ) )
	{
		// an upgrade cameo or a type tile on the portrait bar: no command button behind it
		if( TheInGameUI->isQuitMenuVisible() )
			return;
		m_showBuildToolTipLayout = TRUE;
		populateBuildTooltipLayout( NULL, cmdButton );
	}
	else if(BitTest(cmdButton->winGetStyle(), GWS_PUSH_BUTTON))
	{
		const CommandButton *commandButton = (const CommandButton *)GadgetButtonGetData(cmdButton);
		
		if(!commandButton)
			return;

		// note that, in this branch, ENABLE_SOLO_PLAY is ***NEVER*** defined...
		// this is so that we have a multiplayer build that cannot possibly be hacked
		// to work as a solo game!
		if (TheGameLogic->isInReplayGame())
			return;

		if (TheInGameUI->isQuitMenuVisible())
			return;

		if (TheDisconnectMenu && TheDisconnectMenu->isScreenVisible())
			return;

		//	if (m_buildToolTipLayout)
		//	{
		//		m_buildToolTipLayout->destroyWindows();
		//		m_buildToolTipLayout->deleteInstance();
		//
		//	}

		m_showBuildToolTipLayout = TRUE;
		//	m_buildToolTipLayout = TheWindowManager->winCreateLayout( "ControlBarPopupDescription.wnd" );
		//	m_buildToolTipLayout->setUpdate(ControlBarPopupDescriptionUpdateFunc);
		
		populateBuildTooltipLayout(commandButton);
	}
	else
	{
		// we're a generic window
		if(!BitTest(cmdButton->winGetStyle(), GWS_USER_WINDOW) && !BitTest(cmdButton->winGetStyle(), GWS_STATIC_TEXT))
			return;
		populateBuildTooltipLayout(NULL, cmdButton);
	}
	m_buildToolTipLayout->hide(FALSE);

	if (useAnimation && TheGlobalData->m_animateWindows)
	{
		theAnimateWindowManager = NEW AnimateWindowManager;	
		theAnimateWindowManager->reset();
		theAnimateWindowManager->registerGameWindow( m_buildToolTipLayout->getFirstWindow(), WIN_ANIMATION_SLIDE_RIGHT_FAST, TRUE, 200 );
	}
	
	
}


void ControlBar::repopulateBuildTooltipLayout( void )
{
	if(!prevWindow || !m_buildToolTipLayout)
		return;
	if(!BitTest(prevWindow->winGetStyle(), GWS_PUSH_BUTTON))
		return;
	const CommandButton *commandButton = (const CommandButton *)GadgetButtonGetData(prevWindow);
	populateBuildTooltipLayout(commandButton);
}

void ControlBar::populateBuildTooltipLayout( const CommandButton *commandButton, GameWindow *tooltipWin)
{
	populateBuildTooltipLayout( commandButton, tooltipWin, ThePlayerList->getLocalPlayer() );
}

void ControlBar::populateBuildTooltipLayout( const CommandButton *commandButton, GameWindow *tooltipWin, Player *player )
{
	if(!m_buildToolTipLayout)
		return;

	UnicodeString name, cost, descrip;
	UnicodeString requires = UnicodeString::TheEmptyString, requiresList;
	Bool firstRequirement = true;
	const ProductionPrerequisite *prereq;
	Bool fireScienceButton = false;
	UnsignedInt costToBuild = 0;
	UnicodeString cardWarning;		// why it cannot be bought right now, empty when it can
	UnicodeString cardRequires;		// "Requires: ..." naming what is still missing
	BuildTooltipCard card = BuildTooltipCard();

	if(commandButton)
	{
		const ThingTemplate *thingTemplate = commandButton->getThingTemplate();
		const UpgradeTemplate *upgradeTemplate = commandButton->getUpgradeTemplate();

		ScienceType	st = SCIENCE_INVALID; 
		if( commandButton->getCommandType() != GUI_COMMAND_PLAYER_UPGRADE &&
				commandButton->getCommandType() != GUI_COMMAND_OBJECT_UPGRADE ) 
		{
			if( commandButton->getScienceVec().size() > 1 ) 						
			{
				for(Int j = 0; j < commandButton->getScienceVec().size(); ++j)
				{
					st = commandButton->getScienceVec()[ j ];
					
					if( commandButton->getCommandType() != GUI_COMMAND_PURCHASE_SCIENCE )
					{
						if( !player->hasScience( st ) && j > 0 )
						{
							//If we're not looking at a command button that purchases a science, then
							//it means we are looking at a command button that can USE the science. This
							//means we want to get the description for the previous science -- the one
							//we can use, not purchase!
							st = commandButton->getScienceVec()[ j - 1 ];
						}

						//Now that we got the science for the button that executes the science, we need
						//to generate a simpler help text!
						fireScienceButton = TRUE;

						break;
					}
					else if( !player->hasScience( st ) )
					{
						//Purchase science case. The first science we run into that we don't have, that's the
						//one we'll want to show!
						break;
					}
				}
			}
			else if(commandButton->getScienceVec().size() == 1 )
			{
				st = commandButton->getScienceVec()[ 0 ];
				if( commandButton->getCommandType() != GUI_COMMAND_PURCHASE_SCIENCE )
				{
					//Now that we got the science for the button that executes the science, we need
					//to generate a simpler help text!
					fireScienceButton = TRUE;
				}
			}
		}

		if( commandButton->getDescriptionLabel().isNotEmpty() )
		{
			descrip = TheGameText->fetch(commandButton->getDescriptionLabel());

			Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
			Object *selectedObject = draw ? draw->getObject() : NULL;
			if( selectedObject )
			{
				//Special case: Append status of overcharge on China power plant.
				if( commandButton->getCommandType() == GUI_COMMAND_TOGGLE_OVERCHARGE )
				{
					{
						OverchargeBehaviorInterface *obi;
						for( BehaviorModule **bmi = selectedObject->getBehaviorModules(); *bmi; ++bmi )
						{
							obi = (*bmi)->getOverchargeBehaviorInterface();
							if( obi )
							{
								descrip.concat( u"\n" );
								if( obi->isOverchargeActive() )
									descrip.concat( TheGameText->fetch( "TOOLTIP:TooltipNukeReactorOverChargeIsOn" ) );
								else
									descrip.concat( TheGameText->fetch( "TOOLTIP:TooltipNukeReactorOverChargeIsOff" ) );
							}
						}  
					}
				} //End overcharge special case
				
				//Special case: When building units & buildings, the CanMakeType determines reasons for not being able to buy stuff.
				else if( thingTemplate )
				{
					CanMakeType makeType = TheBuildAssistant->canMakeUnit( selectedObject, commandButton->getThingTemplate() );
					switch( makeType )
					{
						case CANMAKE_NO_MONEY:
							cardWarning = TheGameText->fetch( "TOOLTIP:TooltipNotEnoughMoneyToBuild" );
							break;
						case CANMAKE_QUEUE_FULL:
							cardWarning = TheGameText->fetch( "TOOLTIP:TooltipCannotPurchaseBecauseQueueFull" );
							break;
						case CANMAKE_PARKING_PLACES_FULL:
							cardWarning = TheGameText->fetch( "TOOLTIP:TooltipCannotBuildUnitBecauseParkingFull" );
							break;
						case CANMAKE_MAXED_OUT_FOR_PLAYER:
              if ( thingTemplate->isKindOf( KINDOF_STRUCTURE ) )
              {
                cardWarning = TheGameText->fetch( "TOOLTIP:TooltipCannotBuildBuildingBecauseMaximumNumber" );
              }
              else
              {
  							cardWarning = TheGameText->fetch( "TOOLTIP:TooltipCannotBuildUnitBecauseMaximumNumber" );
              }
							break;
						//case CANMAKE_NO_PREREQ:
						//	descrip.concat( L"\n\n" );
						//	descrip.concat( TheGameText->fetch( "TOOLTIP:TooltipCannotBuildDueToPrerequisites" ) );
						//	break;
					}
				}

				//Special case: When building upgrades
				else if( upgradeTemplate && !player->hasUpgradeInProduction( upgradeTemplate ) )
				{
					if( commandButton->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE ||
						  commandButton->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE )
					{
						ProductionUpdateInterface *pui = selectedObject->getProductionUpdateInterface();
						if( pui && pui->getProductionCount() >= getQueueCap( pui ) )
						{
							cardWarning = TheGameText->fetch( "TOOLTIP:TooltipCannotPurchaseBecauseQueueFull" );
						}
						else if( !TheUpgradeCenter->canAffordUpgrade( ThePlayerList->getLocalPlayer(), upgradeTemplate, FALSE ) )
						{
							cardWarning = TheGameText->fetch( "TOOLTIP:TooltipNotEnoughMoneyToBuild" );
						}
					}
				}
				
			}

		}

		name = TheGameText->fetch(commandButton->getTextLabel().str());

		if( thingTemplate && commandButton->getCommandType() != GUI_COMMAND_PURCHASE_SCIENCE )
		{
			//We are either looking at building a unit or a structure that may or may not have any 
			//prerequisites.

			//Format the cost only when we have to pay for it.
			costToBuild = thingTemplate->calcCostToBuild( player );
			if( costToBuild > 0 )
			{
				cost.format( TheGameText->fetch("TOOLTIP:Cost"), costToBuild );
			}

			// retail tells you the price and nothing about what you get: the card adds the build time,
			// health and the main weapon's figures, all read off the template, there is no Object yet
			card.hasStats = TRUE;
			card.buildSeconds = ControlBar_secondsFromFrames( (Real)thingTemplate->calcTimeToBuild( player ) );
			putUnitFigures( card, thingTemplate, player );

			// ask each prerequisite to give us a list of the non satisfied prerequisites
			for( Int i=0; i<thingTemplate->getPrereqCount(); i++ )
			{
				prereq = thingTemplate->getNthPrereq(i);
				requiresList = prereq->getRequiresList(player);

				if( requiresList != UnicodeString::TheEmptyString ) 
				{
					// make sure to put in 'returns' to space things correctly
					if (firstRequirement)
						firstRequirement = false;
					else
						requires.concat(u", ");
				}
				requires.concat(requiresList);
			}
			if( !requires.isEmpty() )
				cardRequires.format( TheGameText->fetch( "CONTROLBAR:Requirements" ).str(), requires.str() );
		}
		else if( upgradeTemplate )
		{
			putUpgradeTargets( card, upgradeTemplate, player );

			//We are looking at an upgrade purchase icon. Maybe we already purchased it?

			Bool hasUpgradeAlready = player->hasUpgradeComplete( upgradeTemplate );
			Bool hasConflictingUpgrade = FALSE;
			Bool missingScience = FALSE;
			Bool playerUpgradeButton = commandButton->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE;
			Bool objectUpgradeButton = commandButton->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE;

			if( !hasUpgradeAlready )
			{
				//Check if the first selected object has the specified upgrade. 
				Drawable *draw = TheInGameUI->getFirstSelectedDrawable();
				if( draw )
				{
					Object *object = draw->getObject();
					if( object )
					{
						hasUpgradeAlready = object->hasUpgrade( upgradeTemplate );
						if( objectUpgradeButton )
						{
							hasConflictingUpgrade = !object->affectedByUpgrade( upgradeTemplate );
						}
					}
				}
			}
			if( hasConflictingUpgrade && !hasUpgradeAlready )
			{
				if( commandButton->getConflictingLabel().isNotEmpty() )
				{
					cardWarning = TheGameText->fetch( commandButton->getConflictingLabel() );
				}
				else
				{
					cardWarning = TheGameText->fetch( "TOOLTIP:HasConflictingUpgradeDefault" );
				}
			}
			else if( hasUpgradeAlready && ( playerUpgradeButton || objectUpgradeButton ) )
			{
				//See if we can fetch the "already upgraded" text for this upgrade. If not.... use the default "fill me in".
				//It goes under the description as the warning rather than over it: retail replaced the
				//description, so a bought upgrade no longer said what it does.
				if( commandButton->getPurchasedLabel().isNotEmpty() )
				{
					cardWarning = TheGameText->fetch( commandButton->getPurchasedLabel() );
				}
				else
				{
					cardWarning = TheGameText->fetch( "TOOLTIP:AlreadyUpgradedDefault" );
				}
			}
			else if( !hasUpgradeAlready )
			{

				//Do we have a prerequisite science?
				for( Int i = 0; i < commandButton->getScienceVec().size(); i++ )
				{
					ScienceType st = commandButton->getScienceVec()[ i ];
					if( !player->hasScience( st ) )
					{
						missingScience = TRUE;
						break;
					}
				}

				//Determine the cost of the upgrade.
				costToBuild = upgradeTemplate->calcCostToBuild( player );
				if( costToBuild > 0 )
				{
					cost.format( TheGameText->fetch("TOOLTIP:Cost"), costToBuild );
				}

				if( missingScience )
					cardRequires.format( TheGameText->fetch( "CONTROLBAR:Requirements" ).str(), TheGameText->fetch( "CONTROLBAR:GeneralsPromotion" ).str() );
			}
		}	
		else if( st != SCIENCE_INVALID && !fireScienceButton )
		{
			TheScienceStore->getNameAndDescription(st, name, descrip);
			
			costToBuild = TheScienceStore->getSciencePurchaseCost( st );
			if( costToBuild > 0 )
			{
				cost.format( TheGameText->fetch("TOOLTIP:ScienceCost"), costToBuild );
			}

			// ask each prerequisite to give us a list of the non satisfied prerequisites
			if( thingTemplate )
			{
				for( Int i=0; i<thingTemplate->getPrereqCount(); i++ ) 
				{
					prereq = thingTemplate->getNthPrereq(i);
					requiresList = prereq->getRequiresList(player);

					if( requiresList != UnicodeString::TheEmptyString ) 
					{
						// make sure to put in 'returns' to space things correctly
						if (firstRequirement)
							firstRequirement = false;
						else
							requires.concat(u", ");
					}
					requires.concat(requiresList);
				}
				if( !requires.isEmpty() )
					cardRequires.format( TheGameText->fetch( "CONTROLBAR:Requirements" ).str(), requires.str() );
			}

		}
	}
	else if(tooltipWin)
	{

		if( describePortraitBarWindow( tooltipWin, name, descrip ) )
		{
			// an upgrade cameo or a type tile: the call has written the name and the description
		}
		else if( tooltipWin ==TheWindowManager->winGetWindowFromId(m_buildToolTipLayout->getFirstWindow(), TheNameKeyGenerator->nameToKey("ControlBar.wnd:MoneyDisplay")))
		{
			name = TheGameText->fetch("CONTROLBAR:Money");
			descrip = TheGameText->fetch("CONTROLBAR:MoneyDescription");
		}
		else if(tooltipWin == TheWindowManager->winGetWindowFromId(m_buildToolTipLayout->getFirstWindow(), TheNameKeyGenerator->nameToKey("ControlBar.wnd:PowerWindow")) )
		{
			name = TheGameText->fetch("CONTROLBAR:Power");
			descrip = TheGameText->fetch("CONTROLBAR:PowerDescription");

			Player *playerToDisplay = NULL;
			if(TheControlBar->isObserverControlBarOn())
				playerToDisplay = TheControlBar->getObserverLookAtPlayer();
			else
				playerToDisplay = ThePlayerList->getLocalPlayer();

			if( playerToDisplay && playerToDisplay->getEnergy() )
			{
				Energy *energy = playerToDisplay->getEnergy();
				descrip.format(descrip, energy->getProduction(), energy->getConsumption());
			}
			else
			{
				descrip.format(descrip, 0, 0);
			}
		}
		else if(tooltipWin == TheWindowManager->winGetWindowFromId(m_buildToolTipLayout->getFirstWindow(), TheNameKeyGenerator->nameToKey("ControlBar.wnd:GeneralsExp")) )
		{
			name = TheGameText->fetch("CONTROLBAR:GeneralsExp");
			descrip = TheGameText->fetch("CONTROLBAR:GeneralsExpDescription");
		}
		else
		{
			DEBUG_ASSERTCRASH(FALSE, ("ControlBar::populateBuildTooltipLayout We attempted to call the popup tooltip on a game window that has yet to be hand coded in as this fuction was/is designed for only buttons but has been hacked to work with GameWindows."));
			return;
		}

	}

	// the layout's one text, in the order Reforged's card had it: the description, the unit's figures
	// counted with the upgrades the player owns, every upgrade that moves one of them and by how much
	// (on an upgrade's own button, the units it changes), then why it cannot be bought now and what
	// it still needs
	if( card.hasStats )
	{
		// each figure ends in a short gloss of what it counts, so the card reads without a manual
		UnicodeString figures;
		auto row = [&]( const char *label, const UnicodeString &value, const char *hint )
		{
			figures.concat( u"\n" );
			figures.concat( TheGameText->fetch( label ) );
			figures.concat( u": " );
			figures.concat( value );
			figures.concat( u" " );
			figures.concat( TheGameText->fetch( hint ) );
		};
		auto number = []( Int value )
		{
			UnicodeString text;
			text.format( u"%d", value );
			return text;
		};
		if( card.health > 0 )
			row( "TOOLTIP:StatHealth", number( card.health ), "TOOLTIP:StatHealthHint" );
		if( card.damage > 0 )
		{
			// a bomb that kills its carrier goes off once, so it has no rate
			const Bool once = card.attacksPerSecond <= 0.0f;
			row( "TOOLTIP:StatDamage", number( card.damage ), "TOOLTIP:StatDamageHint" );
			row( "TOOLTIP:StatAttackSpeed", once ? UnicodeString( u"-" ) : UnicodeString( MultiByteToWideCharSingleLine( rateFigure( card.attacksPerSecond ).c_str() ).c_str() ),
					 "TOOLTIP:StatAttackSpeedHint" );
			row( "TOOLTIP:StatDamagePerSecond", once ? UnicodeString( u"-" ) : number( card.damagePerSecond ), "TOOLTIP:StatDamagePerSecondHint" );
			row( "TOOLTIP:StatRange", number( card.range ), "TOOLTIP:StatRangeHint" );
		}
		UnicodeString seconds;
		seconds.format( TheGameText->fetch( "TOOLTIP:StatSeconds" ), card.buildSeconds );
		row( "TOOLTIP:StatTime", seconds, "TOOLTIP:StatTimeHint" );
		if( !descrip.isEmpty() )
			descrip.concat( u"\n" );
		descrip.concat( figures );
	}
	if( !card.upgrades.empty() )
	{
		descrip.concat( u"\n\n" );
		descrip.concat( TheGameText->fetch( card.hasStats ? "TOOLTIP:Upgrades" : "TOOLTIP:UpgradeChanges" ) );
		descrip.concat( u" " );
		descrip.concat( TheGameText->fetch( card.hasStats ? "TOOLTIP:UpgradesHint" : "TOOLTIP:UpgradeChangesHint" ) );
		for( size_t each = 0; each < card.upgrades.size(); ++each )
		{
			const BuildTooltipUpgrade &upgrade = card.upgrades[ each ];
			WideCharString upgradeName = upgrade.name.str();
			const size_t marker = upgradeName.find( u'&' );
			if( marker != WideCharString::npos )
				upgradeName.erase( marker, 1 );
			descrip.concat( u"\n" );
			descrip.concat( upgradeName.c_str() );
			if( upgrade.owned )
			{
				descrip.concat( u" (" );
				descrip.concat( TheGameText->fetch( "TOOLTIP:UpgradeOwned" ) );
				descrip.concat( u")" );
			}
			for( size_t change = 0; change < upgrade.changes.size(); ++change )
			{
				const BuildTooltipChange &line = upgrade.changes[ change ];
				descrip.concat( u"\n   " );
				descrip.concat( TheGameText->fetch( line.label ) );
				descrip.concat( u": " );
				if( !line.from.empty() )
				{
					descrip.concat( MultiByteToWideCharSingleLine( line.from.c_str() ).c_str() );
					descrip.concat( u" \x2192 " );
				}
				descrip.concat( MultiByteToWideCharSingleLine( line.to.c_str() ).c_str() );
			}
		}
	}
	if( !cardWarning.isEmpty() )
	{
		descrip.concat( u"\n\n" );
		descrip.concat( cardWarning );
	}
	if( !cardRequires.isEmpty() )
	{
		if( !descrip.isEmpty() )
			descrip.concat( u"\n" );
		descrip.concat( cardRequires );
	}

	GameWindow *win = TheWindowManager->winGetWindowFromId(m_buildToolTipLayout->getFirstWindow(), TheNameKeyGenerator->nameToKey("ControlBarPopupDescription.wnd:StaticTextName"));
	if(win)
	{
		if( commandButton && commandButton->getName() == "NonCommand_IdleWorker" )
			name.concat( metaKeySuffix( GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER ) );
		else if( commandButton && commandButton->getName() == "NonCommand_IdleInfantry" )
			name.concat( metaKeySuffix( GameMessage::MSG_META_SELECT_NEXT_IDLE_INFANTRY ) );
		else if( commandButton && commandButton->getName() == "NonCommand_IdleVehicle" )
			name.concat( metaKeySuffix( GameMessage::MSG_META_SELECT_NEXT_IDLE_VEHICLE ) );
		else if( commandButton && commandButton->getName() == "NonCommand_IdleAircraft" )
			name.concat( metaKeySuffix( GameMessage::MSG_META_SELECT_NEXT_IDLE_AIRCRAFT ) );
		GadgetStaticTextSetText(win, name);
	}

	win = TheWindowManager->winGetWindowFromId(m_buildToolTipLayout->getFirstWindow(), TheNameKeyGenerator->nameToKey("ControlBarPopupDescription.wnd:StaticTextCost"));
	if(win)
	{
		if( costToBuild > 0 )
		{
			win->winHide( FALSE );
			GadgetStaticTextSetText(win, cost);
		}
		else
		{
			win->winHide( TRUE );
		}
	}

	win = TheWindowManager->winGetWindowFromId(m_buildToolTipLayout->getFirstWindow(), TheNameKeyGenerator->nameToKey("ControlBarPopupDescription.wnd:StaticTextDescription"));
	if(win)
	{

		static NameKeyType winNamekey	= TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:BackgroundMarker" ) );
		ICoord2D &lastOffset = theTooltipLastOffset;

		ICoord2D size, newSize, pos;
		Int diffSize;
		
		DisplayString *tempDString = TheDisplayStringManager->newDisplayString();
		win->winGetSize(&size.x, &size.y);
		tempDString->setFont(win->winGetFont());
		tempDString->setWordWrap(size.x - 10);
		tempDString->setText(descrip);
		tempDString->getSize(&newSize.x, &newSize.y);
		TheDisplayStringManager->freeDisplayString(tempDString);
		tempDString = NULL;
		diffSize = newSize.y - size.y;
 		GameWindow *parent = m_buildToolTipLayout->getFirstWindow();
 		if(!parent)
 			return;
		
 		parent->winGetSize(&size.x, &size.y);
 		if(size.y + diffSize < 102) {
			diffSize = 102 - size.y;
		}

		parent->winSetSize(size.x, size.y + diffSize);
 		parent->winGetPosition(&pos.x, &pos.y);
//		if(size.y + diffSize < 102)
//		{
//			
//			parent->winSetPosition(pos.x, pos.y -  (102 - (newSize.y + size.y + diffSize) ));
//		}
//		else

//		heightChange = controlBarPos.y - m_defaultControlBarPosition.y;

		GameWindow *marker =  TheWindowManager->winGetWindowFromId(NULL,winNamekey);
		static ICoord2D basePos;
		if(!marker)
		{
			return;
		}
		TheControlBar->getBackgroundMarkerPos(&basePos.x, &basePos.y);
		ICoord2D curPos, offset;
		marker->winGetScreenPosition(&curPos.x,&curPos.y);

		offset.x = curPos.x - basePos.x;
		offset.y = curPos.y - basePos.y;

		parent->winSetPosition(pos.x, (pos.y - diffSize) + (offset.y - lastOffset.y));

		lastOffset.x = offset.x;
		lastOffset.y = offset.y;

		win->winGetSize(&size.x, &size.y);
 		win->winSetSize(size.x, size.y + diffSize);

		GadgetStaticTextSetText(win, descrip);		
	}
	m_buildToolTipLayout->hide(FALSE);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ControlBar::hideBuildTooltipLayout()
{
	if(theAnimateWindowManager && theAnimateWindowManager->isReversed())
		return;
	if(useAnimation && theAnimateWindowManager && TheGlobalData->m_animateWindows)
		theAnimateWindowManager->reverseAnimateWindow();
	else
		deleteBuildTooltipLayout();

}

void ControlBar::deleteBuildTooltipLayout( void )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	m_showBuildToolTipLayout = FALSE;
	prevWindow= NULL;
	m_buildToolTipLayout->hide(TRUE);
//	if(!m_buildToolTipLayout)
//		return;
//	
//	m_buildToolTipLayout->destroyWindows();
//	m_buildToolTipLayout->deleteInstance();
//	m_buildToolTipLayout = NULL;
	if(theAnimateWindowManager)
		delete theAnimateWindowManager;
	theAnimateWindowManager = NULL;

}
