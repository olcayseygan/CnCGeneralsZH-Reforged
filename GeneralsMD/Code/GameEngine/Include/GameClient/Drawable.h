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

// FILE: Drawable.h ///////////////////////////////////////////////////////////////////////////////
// Simple base drawable
// Author: Michael S. Booth, March 2001

#pragma once
#ifndef _DRAWABLE_H_
#define _DRAWABLE_H_

#include "Common/AudioEventRTS.h"
#include "Common/DisabledTypes.h"
#include "Common/GameType.h"
#include "Common/LogicThread.h"
#include "Common/ModelState.h"
#include "Common/ModuleFactory.h"
#include "Common/Thing.h"
#include "Common/Geometry.h"
#include "GameClient/Color.h"
#include "WWMath/matrix3d.h"
#include "GameClient/DrawableInfo.h"

#include <functional>

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class PositionalSound;
class ThingTemplate;
class Particle;
class DisplayString;
class FXList;
class DrawModule;
class ClientUpdateModule;
class View;
class Locomotor;
class Anim2D;
class Shadow;
class ModuleInfo;
class Anim2DTemplate;
class Image;
class DynamicAudioEventInfo;
enum BodyDamageType : Int;

// this is a very worthwhile performance win. left conditionally defined for now, just 
// in case, but probably should be made permanent soon. (srj)
#define DIRTY_CONDITION_FLAGS

#define DEFAULT_TINT_COLOR_FADE_RATE (0.6f) // fast fade
// logic frames; the retail 1, 1 and 4 at twice the rate
#define DEF_ATTACK_FRAMES (1 * LOGICFRAMES_PER_RETAIL_FRAME)
#define DEF_SUSTAIN_FRAMES (1 * LOGICFRAMES_PER_RETAIL_FRAME)
#define DEF_DECAY_FRAMES (4 * LOGICFRAMES_PER_RETAIL_FRAME)
#define SUSTAIN_INDEFINITELY (0xfffffffe) // forever and ever, amen.

//
// A structure that has been placed but whose builder has not walked over yet is drawn as a
// translucent silhouette at this opacity - the same value the placement preview uses, so what
// you see under the cursor is exactly what lands on the map until the build actually starts.
//
const Real PLACEMENT_SILHOUETTE_OPACITY = 0.45f;

///< the opacity a drawable is actually drawn at, given what it is doing
extern Real Drawable_effectiveOpacity( Real explicitOpacity, Real stealthOpacity, Bool awaitingBuilder );

///< whether a structure's health bar is worth drawing at all
extern Bool Drawable_structureShowsHealthBar( Bool isBridge, Bool isUnowned, Bool isGarrisonable,
																							Bool isCapturable );

///< whether an object of these kinds is a thing that wears a health bar at all
extern Bool Drawable_kindShowsHealthBar( const KindOfMaskType& kinds );

///< whether a weapon fires slowly enough (over three seconds a shot, clip and reload counted) to wear a reload bar;
///< a mine-clearing weapon (DamageType DISARM) never does
extern Bool Drawable_weaponWearsReloadBar( Int clipSize, UnsignedInt delayFrames, UnsignedInt clipReloadFrames, Bool disarms );

///< how far a weapon's wait has come, 0 to 1, or -1 when there is no wait worth a bar
extern Real Drawable_reloadBarFraction( UnsignedInt now, UnsignedInt started, UnsignedInt ready, UnsignedInt longest );

///< whether the health bar setting lets this object show one right now
extern Bool Drawable_healthBarModeShows( Int healthBarMode, Bool isSelected, Bool isMousedOver,
																				 Bool isDamaged );

///< whether these disabled flags mean the health bar is drawn blue instead of the owner's colour
extern Bool Drawable_disabledShowsBlueHealthBar( DisabledMaskType disabledFlags );

//-----------------------------------------------------------------------------
//@TODO -- The drawable icon system needs to be implemented in a proper manner -- KM
//				 Fact 1: Every drawable in the world shouldn't have to have a pointer 
//				 and frame counter for every possible icon type. It should be a dynamic vector.
//				 Fact 2: It's polling every frame for every object on screen for every possible icon condition...
// KM : I moved this into Drawable.cpp so I don't have to recompile the entire project
//      each time I add a new icon, and made the arrays dynamic...
// CD: No so good, core engine components should not be made dynamic in this way

enum DrawableIconType
{
/** NOTE: This enum MUST appear in the same order as TheDrawableIconNames array to be
	* indexed correctly using that array */
	ICON_INVALID = -1,
	ICON_FIRST = 0,

	ICON_DEFAULT_HEAL = ICON_FIRST,
	ICON_STRUCTURE_HEAL,
	ICON_VEHICLE_HEAL,
#ifdef ALLOW_DEMORALIZE
	ICON_DEMORALIZED,
#else
	ICON_DEMORALIZED_OBSOLETE,
#endif
	ICON_BOMB_TIMED,
	ICON_BOMB_REMOTE,
	ICON_DISABLED,
	ICON_BATTLEPLAN_BOMBARD,
	ICON_BATTLEPLAN_HOLDTHELINE,
	ICON_BATTLEPLAN_SEARCHANDDESTROY,
	ICON_EMOTICON,
	ICON_ENTHUSIASTIC,
	ICON_ENTHUSIASTIC_SUBLIMINAL,
	ICON_CARBOMB,

	MAX_ICONS									///< keep this last
};

//-----------------------------------------------------------------------------
class DrawableIconInfo : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(DrawableIconInfo, "DrawableIconInfo" )		
public:
	Anim2D*								m_icon[MAX_ICONS];
	UnsignedInt						m_keepTillFrame[MAX_ICONS];

	DrawableIconInfo();
	//~DrawableIconInfo();
	
	void clear();
	void killIcon(DrawableIconType t);

};

//-----------------------------------------------------------------------------
struct TWheelInfo 
{
	Real m_frontLeftHeightOffset;			 ///< Height offsets for tires due to suspension sway
	Real m_frontRightHeightOffset;
	Real m_rearLeftHeightOffset;
	Real m_rearRightHeightOffset;
	Real m_wheelAngle;								///< Wheel angle.  0 = straight, >0 left, <0 right.
	Int	 m_framesAirborneCounter;			///< Counter.
	Int	 m_framesAirborne;						///< How many frames it was in the air.
};

//-----------------------------------------------------------------------------
class DrawableLocoInfo : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(DrawableLocoInfo, "DrawableLocoInfo" )		
public:
	Real m_pitch;								///< pitch of the entire drawable
	Real m_pitchRate;						///< rate of change of pitch
	Real m_roll;								///< roll of the entire drawable
	Real m_rollRate;						///< rate of change of roll
	Real m_yaw;									///< yaw for entire drawable
	Real m_accelerationPitch;					///< pitch of the drawable due to impact/acceleration
	Real m_accelerationPitchRate;			///< rate of change of pitch
	Real m_accelerationRoll;					///< roll of the entire drawable
	Real m_accelerationRollRate;			///< rate of change of roll
	Real m_overlapZVel;					///< fake Z velocity
	Real m_overlapZ;						///< current height (additional)
	Real m_wobble;							///< for wobbling
  Real m_yawModulator;        ///< for the swimmy soft hover of a helicopter
  Real m_pitchModulator;        ///< for the swimmy soft hover of a helicopter
	Real m_leanPitch[2];				///< a helicopter's pitch target, smoothed through two stages once a retail frame
	Real m_leanRoll[2];					///< the same for roll
	UnsignedInt m_airFrame;			///< the retail frame (logic frame / LOGICFRAMES_PER_RETAIL_FRAME) hover, wings or thrust last stepped on
	Real m_airPitch;						///< hover or wings pitch, roll and yaw from that frame, drawn until the next one
	Real m_airRoll;
	Real m_airYaw;
	Real m_jetBank;							///< a jet's bank into its turn and its nose-up pitch, eased
	Real m_jetPitch;
	Real m_groundLeanStages[2][3];	///< a ground vehicle's pitch and roll lean, smoothed through three stages once a retail frame
	UnsignedInt m_groundFrame;	///< the retail frame (logic frame / LOGICFRAMES_PER_RETAIL_FRAME) those were last stepped on
	Real m_prevForwardSpeed;		///< a ground vehicle's forward speed on that frame
	Real m_prevAngle;						///< and its heading
	Real m_groundLeanPitch;			///< a ground vehicle's lean from starting, braking and turning, on top of terrain and recoil
	Real m_groundLeanRoll;
	Real m_groundPitch;					///< a ground vehicle's chassis pitch, roll and lift from that frame, drawn until the next one
	Real m_groundRoll;
	Real m_groundZ;
	TWheelInfo m_wheelInfo;			///< Wheel offset & angle info for a wheeled type locomotor.

	DrawableLocoInfo();
};

//-----------------------------------------------------------------------------
//* TintEnvelope handles the fading of the tint color up, down stable etc...
//* assumes that 0,0,0, is the color for the AT REST state, used as decay target
//* works like an ADSR envelope, 
//* except that SUSTAIN and RELEASE are randomly (or never) triggered, externally 
class TintEnvelope : public MemoryPoolObject, public Snapshot
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TintEnvelope, "TintEnvelope" )		
public:

	TintEnvelope(void);
	void update(void);  ///< does all the work
	void play(const RGBColor *peak, 
						UnsignedInt atackFrames = DEF_ATTACK_FRAMES, 
						UnsignedInt decayFrames = DEF_DECAY_FRAMES, 
						UnsignedInt sustainAtPeak = DEF_SUSTAIN_FRAMES ); // ask MLorenzen
	void sustain(void) { m_envState = ENVELOPE_STATE_SUSTAIN; }
	void release(void);
	void rest(void)    { m_envState = ENVELOPE_STATE_REST; } // goes away now!
	Bool isEffective() const { return m_affect; }
	const Vector3* getColor() const { return &m_currentColor; }

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

private:

	void setAttackFrames(UnsignedInt frames);
	void setDecayFrames( UnsignedInt frames); 
	void setPeakColor( const RGBColor *peak) {m_peakColor = Vector3( peak->red, peak->green, peak->blue );};
	void setPeakColor( Real r, Real g, Real b ) {m_peakColor.Set( r, g, b );};

	enum EnvelopeStatesEnum
	{
		ENVELOPE_STATE_REST,
		ENVELOPE_STATE_ATTACK,
		ENVELOPE_STATE_DECAY,
		ENVELOPE_STATE_SUSTAIN ///< RELEASE IS THE LOGICAL COMPLIMENT TO SUSTAIN								
	};

	Vector3							m_attackRate;		 	///< step amount to make tint turn on slow or fast 
	Vector3							m_decayRate;			///< step amount to make tint turn off slow or fast
	Vector3							m_peakColor;			///< um, the peak color, what color we are headed toward during attack
	Vector3							m_currentColor;		///< um, the current color, how we are colored, now
	UnsignedInt					m_sustainCounter;
	Byte								m_envState;				///< a randomly switchable SUSTAIN state, release is compliment
	Bool								m_affect;         ///< set TRUE if this has any effect (has a non 0,0,0 color).
};
EMPTY_DTOR(TintEnvelope)

//-----------------------------------------------------------------------------
enum StealthLookType : Int
{
	STEALTHLOOK_NONE,								///< unit is not stealthed at all
	STEALTHLOOK_VISIBLE_FRIENDLY,		///< unit is stealthed-but-visible due to friendly status
	STEALTHLOOK_DISGUISED_ENEMY,		///< we can have units that are disguised (instead of invisible)
	STEALTHLOOK_VISIBLE_DETECTED,		///< unit is stealthed and invisible, but a second material pass
																						///< is added to reveal the invisible unit as with heat vision
	STEALTHLOOK_VISIBLE_FRIENDLY_DETECTED,		///< unit is stealthed-but-visible due to being detected, 
																						///< and rendered in heatvision effect second material pass
	STEALTHLOOK_INVISIBLE						///< unit is stealthed-and-invisible
};

// ------------------------------------------------------------------------------------------------
/** Drawable status bits */
// ------------------------------------------------------------------------------------------------
enum DrawableStatus
{
	DRAWABLE_STATUS_NONE									= 0x00000000,		///< no status
	DRAWABLE_STATUS_DRAWS_IN_MIRROR				=	0x00000001,		///< drawable can reflect
	DRAWABLE_STATUS_SHADOWS								=	0x00000002,		///< use setShadowsEnabled() access method
	DRAWABLE_STATUS_TINT_COLOR_LOCKED			=	0x00000004,		///< drawable tint color is "locked" and won't fade to normal
	DRAWABLE_STATUS_NO_STATE_PARTICLES		= 0x00000008,		///< do *not* auto-create particle systems based on model condition
	DRAWABLE_STATUS_NO_SAVE								= 0x00000010,		///< do *not* save this drawable (UI fluff only). ignored (error, actually) if attached to an object
};

enum TintStatus
{
	TINT_STATUS_DISABLED		= 0x00000001,///< drawable tint color is deathly dark grey
	TINT_STATUS_IRRADIATED	= 0x00000002,///< drawable tint color is sickly green
	TINT_STATUS_POISONED		= 0x00000004,///< drawable tint color is open-sore red
	TINT_STATUS_GAINING_SUBDUAL_DAMAGE		= 0x00000008,///< When gaining subdual damage, we tint SUBDUAL_DAMAGE_COLOR
	TINT_STATUS_FRENZY			= 0x00000010,///< When frenzied, we tint FRENZY_COLOR

};

//-----------------------------------------------------------------------------
//Keep this enum in sync with the TerrainDecalTextureName array in drawable.cpp
//
// Note: these values are saved in save files, so you MUST NOT REMOVE OR CHANGE
// existing values!
//
enum TerrainDecalType : Int
{
#ifdef ALLOW_DEMORALIZE
	TERRAIN_DECAL_DEMORALIZED = 0,
#else
	TERRAIN_DECAL_DEMORALIZED_OBSOLETE = 0,
#endif
	TERRAIN_DECAL_HORDE,
	TERRAIN_DECAL_HORDE_WITH_NATIONALISM_UPGRADE,
	TERRAIN_DECAL_HORDE_VEHICLE,
	TERRAIN_DECAL_HORDE_WITH_NATIONALISM_UPGRADE_VEHICLE,
	TERRAIN_DECAL_CRATE,
    TERRAIN_DECAL_HORDE_WITH_FANATICISM_UPGRADE,
	TERRAIN_DECAL_CHEMSUIT,
	TERRAIN_DECAL_NONE,
	TERRAIN_DECAL_SHADOW_TEXTURE,	//use the shadow texture as the terrain decal.

	TERRAIN_DECAL_MAX	///< keep this last
};

//-----------------------------------------------------------------------------

const Int DRAWABLE_FRAMES_PER_FLASH = LOGICFRAMES_PER_SECOND / 2;

//-----------------------------------------------------------------------------
/**
 * A Drawable is a graphical entity which is generally associated
 * with a GameLogic object.  Drawables are managed by TheGameClient.
 */
class Drawable : public Thing,
								 public Snapshot
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(Drawable, "Drawable" )		

public:

	Drawable( const ThingTemplate *thing, DrawableStatus statusBits = DRAWABLE_STATUS_NONE );

	void onDestroy( void );																							///< run from GameClient::destroyDrawable
  void onLevelStart();                                                ///< run from GameLogic::startNewGame

	Drawable *getNextDrawable( void ) const { return m_nextDrawable; }	///< return the next drawable in the global list

	/** R1, smooth motion: the logic moved this drawable in one step it did not travel (a teleport, a
		container's exit, a parachute rider placed): show the new place at once rather than blend to it. */
	void markMotionDiscontinuity( void ) { m_motionDiscontinuity = TRUE; }
	Bool isMotionDiscontinuous( void ) const { return m_motionDiscontinuity; }
	void clearMotionDiscontinuity( void ) { m_motionDiscontinuity = FALSE; }
	/** R1: the client's record of this drawable's position on its last two logic ticks, taken at the
		start of a render pass, and the blend of them the picture shows (the camera's lock follows it).
		FALSE, with the logic position, when there is nothing to blend. */
	void smoothMotionCapturePosition( UnsignedInt clientFrame );
	Bool getSmoothMotionPosition( Real alpha, Coord3D *pos ) const;
	Drawable *getPrevDrawable( void ) const { return m_prevDrawable; }  ///< return the prev drawable in the global list
	DrawableID getID( void ) const;																			///< return this drawable's unique ID

	void friend_bindToObject( Object *obj ); ///< bind this drawable to an object ID. for use ONLY by GameLogic!
	void friend_forgetObject( void ) { m_object = NULL; }	///< GameClient::destroyDrawable, for a deletion that waits for the flush
	void setIndicatorColor(Color color);
	void refreshIndicatorColor( void );			///< re-apply the last colour asked for, under whatever scheme is on now
	
	void setTintStatus( TintStatus statusBits ) { BitSet( m_tintStatus, statusBits ); };
	void clearTintStatus( TintStatus statusBits ) { BitClear( m_tintStatus, statusBits ); };
	Bool testTintStatus( TintStatus statusBits ) const { return BitTest( LogicThread::isInsideWindow() ? m_lookForDraw.tintStatus : m_tintStatus, statusBits ); };
	TintEnvelope *getColorTintEnvelope( void ) { return m_colorTintEnvelope; }
	void setColorTintEnvelope( TintEnvelope &source ) { if (m_colorTintEnvelope) *m_colorTintEnvelope = source; }

  
  void imitateStealthLook( Drawable& otherDraw );

	void setTerrainDecal(TerrainDecalType type);	///<decal that is to appear under the drawable
	void setTerrainDecalSize(Real x, Real y);
	void setTerrainDecalFadeTarget(Real target, Real rate = ConvertRetailPerFrameRate(0.1f));	///< rate is opacity per logic frame

	inline Object *getObject( void ) { LOGIC_WINDOW_READ( WINDOW_DRAWABLE_OBJECT ); return m_object; }								///< return object ID bound to this drawble
	inline const Object *getObject( void ) const { LOGIC_WINDOW_READ( WINDOW_DRAWABLE_OBJECT ); return m_object; }		///< return object ID bound to this drawble

	inline DrawableInfo *getDrawableInfo(void) {return &m_drawableInfo;}

	void setDrawableHidden( Bool hidden );																		///< hide or unhide drawable
	//
	// note that this is not necessarily the 'get' reflection of setDrawableHidden, since drawables
	// can spontaneously hide via stealth. (srj)
	//
	inline Bool isDrawableEffectivelyHidden() const { if( LogicThread::isInsideWindow() ) return m_lookForDraw.hidden; return m_hidden || m_hiddenByStealth; }

	void setSelectable( Bool selectable );												///< Changes the drawables selectability	
	Bool isSelectable( void ) const;
	Bool isMassSelectable( void ) const;


	void setStealthLook(StealthLookType look);
	StealthLookType getStealthLook() const { return LogicThread::isInsideWindow() ? m_lookForDraw.stealthLook : m_stealthLook; }
	StealthLookType getStealthDrawnLook() const { return LogicThread::isInsideWindow() ? m_lookForDraw.stealthDrawnLook : m_stealthDrawnLook; }
	Real getStealthFade() const { return LogicThread::isInsideWindow() ? m_lookForDraw.stealthFade : m_stealthFade; }
	// 1 is own paint with no bend, the stealth floor (about a half) is glass.  Cloak-in fade 0
	// is solid.  The mix goes to that floor so the first frames still have travel while the
	// pulse has not left 1.0; once faded, the 2003 breath owns it.  Decloak: fade 1 is the
	// pulse, fade 0 is solid.  The shader keeps coverage: this is the mix, not alpha.
	Real getStealthSolid() const
	{
		if( LogicThread::isInsideWindow() )
			return m_lookForDraw.stealthSolid;
		const Real breath = getEffectiveOpacity();
		const Real fade = getStealthFade();
		if( getStealthLook() == STEALTHLOOK_NONE )
			return breath + ( 1.0f - breath ) * ( 1.0f - fade );
		const Real dest = m_stealthOpacity + ( breath - m_stealthOpacity ) * fade;
		return 1.0f - fade * ( 1.0f - dest );
	}

	void updateDrawableClipStatus( UnsignedInt shotsRemaining, UnsignedInt maxShots, WeaponSlotType slot ); ///< This will do the show/hide work if ProjectileBoneFeedbackEnabled is set.
	void updateDrawableSupplyStatus( Int maxSupply, Int currentSupply ); ///< This will do visual feedback on Supplies carried
	
	void notifyDrawableDependencyCleared();///< If any of your draw modules were waiting for something, it's ready now.

	// Override.
	void setPosition( const Coord3D *pos );
	void reactToGeometryChange();

	const GeometryInfo& getDrawableGeometryInfo() const;

	void reactToBodyDamageStateChange(BodyDamageType newState);
	
	const Real getScale (void) const ;

	// access to modules
	//---------------------------------------------------------------------------
	// since most things don't have CU modules, we allow this to be null!
	ClientUpdateModule** getClientUpdateModules() { return (ClientUpdateModule**)getModuleList(MODULETYPE_CLIENT_UPDATE); }
	ClientUpdateModule const** getClientUpdateModules() const { return (ClientUpdateModule const**)getModuleList(MODULETYPE_CLIENT_UPDATE); }
	ClientUpdateModule* findClientUpdateModule( NameKeyType key );

	DrawModule** getDrawModulesNonDirty();
	DrawModule** getDrawModules();					///< flushes a model condition change, or inside a logic tick queues it for after the tick
	DrawModule const** getDrawModules() const;
	DrawModule** getDrawModulesFlushed();		///< applies what the tick deferred now, for a caller that needs the render object as it stands
	static void flushPendingClientWork();		///< after each tick, on the main thread: the transforms and model conditions the tick deferred
	/** LOGIC-THREAD.md phase 2, M4: inside a tick, past the map load, a new drawable is a shell (ID,
		Thing, condition bits and its modules, everything logic reads) whose render objects are built
		after the tick, and a destroyed one is deleted after the tick.  Phase 3: both join and leave the
		client's list and lookup after the tick too (GameClient::linkDrawablesOfTick). */
	static Bool tickDefersLifetime();
	void flushPendingTransform( Bool onDemand );	///< tells the draw modules of a transform the tick deferred; GameClient::destroyDrawable
	void doPendingClientWork( Bool onDemand );	///< everything the tick deferred on this drawable; flushDestroyedDrawables for one the tick also destroyed
	Bool isRenderPending( void ) const { return m_renderPending; }	///< made in a tick and its render objects not built yet
	/** Where a doClientWork goes among the model condition states the tick has not applied yet. */
	enum ClientWorkOrder
	{
		WORK_AFTER_FLUSH,				///< on the state the drawable holds at the call, as getDrawModules did
		WORK_AFTER_APPLIED,			///< on the state last flushed, as getDrawModulesNonDirty did before M1
		WORK_BEFORE_STATES			///< on the render object as it stands, in front of every state still pending
	};
	/** Work on the draw modules, render objects or audio that a call from logic causes.  Inside a
		tick it waits for flushPendingClientWork and runs there in order with the model condition
		states; anywhere else it runs now.  movesBones marks work a bone or barrel read needs done
		first (animation timing, a model rebuild), which an on-demand flush inside the tick runs; the
		rest an on-demand flush leaves for after the tick. */
	void doClientWork( const std::function<void()>& work, ClientWorkOrder order, Bool movesBones = FALSE );
	void startCollapse();		///< the Collapse option, on the way to rubble

	//---------------------------------------------------------------------------
	void setDrawableStatus( DrawableStatus bit )  { BitSet( m_status, bit ); }
	void clearDrawableStatus( DrawableStatus bit ) { BitClear( m_status, bit ); }
	inline Bool testDrawableStatus( DrawableStatus bit ) const { LOGIC_WINDOW_READ( WINDOW_DRAWABLE_FIELD ); return (m_status & bit) != 0; }

	void setShroudClearFrame( UnsignedInt frame )  { m_shroudClearFrame = frame; }
	UnsignedInt getShroudClearFrame( void ) { return m_shroudClearFrame; }

	/** LOGIC-THREAD.md phase 3: what the draw asks of the shroud, taken by GameClient::update's drawable
		* walk, at the sync point, for the player whose fog is drawn: the object's status, or for a drawable
		* with no object the status of the object its DrawableInfo names (a prison's inmates).  The draw
		* calls no getShroudedStatus, which fills the partition's cache and can take a ghost snapshot. */
	void setShroudForDraw( ObjectShroudStatus status, Bool objectDead ) { m_shroudForDraw = status; m_objectDeadForDraw = objectDead; }
	ObjectShroudStatus getShroudForDraw( void ) const { return m_shroudForDraw; }
	Bool isObjectDeadForDraw( void ) const { return m_objectDeadForDraw; }

	/** The rest of what the render asks of the object, taken by the same walk: the scene's look and
		* occlusion, the translucent silhouette of a building waiting for its builder, a projected shadow
		* lifted onto a bridge.  The render reads these and calls no getObject. */
	struct ObjectForDraw
	{
		Bool					hasObject;
		Bool					awaitingBuilder;		///< Object_isAwaitingBuilder, for getEffectiveOpacity
		Bool					empDisabled;				///< DISABLED_EMP
		Bool					outage;							///< a structure underpowered, script underpowered or hacked
		Int						playerIndex;				///< the controlling player's
		Color					playerColor;				///< clientPlayerColor of the controlling player
		UnsignedInt		safeOcclusionFrame;
		PathfindLayerEnum	layer;
		Real					layerHeight;				///< getLayerHeight at the drawable, off LAYER_GROUND only
	};
	void setObjectForDraw( const ObjectForDraw &forDraw ) { m_objectForDraw = forDraw; }
	const ObjectForDraw &getObjectForDraw( void ) const { return m_objectForDraw; }

	void setShadowsEnabled(Bool enable);
	Bool getShadowsEnabled() const { return BitTest(m_status, DRAWABLE_STATUS_SHADOWS); }

	void releaseShadows(void);	///< frees all shadow resources used by this module - used by Options screen.
	void allocateShadows(void); ///< create shadow resources if not already present. Used by Options screen.

	void setFullyObscuredByShroud(Bool fullyObscured);
	inline Bool getFullyObscuredByShroud(void) { return LogicThread::isInsideWindow() ? m_lookForDraw.fullyObscured : m_drawableFullyObscuredByShroud; }

  // Put on ice until later... M Lorenzen
  //	inline UnsignedByte getFullyObscuredByShroudWithCheatSpy(void) {return (UnsignedByte)m_drawableFullyObscuredByShroud | 128;}//8 looks like a zero in most fonts

	Bool getDrawsInMirror() const { if( LogicThread::isInsideWindow() ) return m_lookForDraw.drawsInMirror; return BitTest(m_status, DRAWABLE_STATUS_DRAWS_IN_MIRROR) || isKindOf(KINDOF_CAN_CAST_REFLECTIONS); }
	Bool isNoSaveForDraw() const {
		if( LogicThread::isInsideWindow() ) return m_lookForDraw.noSave;
		return testDrawableStatus( DRAWABLE_STATUS_NO_SAVE );
	}

	/** LOGIC-THREAD.md phase 3, step 3b.  How the drawable looks, as the render reads it: hidden, stealth,
		* opacity, tint, the mirror and dynamic light flags.  The tick writes these fields beside the render
		* (a unit stealthing, a container hiding its passengers, a flash on a hit) and Drawable::draw eases
		* the fades at the sync point, so takeLookForDraw copies them there, after the views' update and
		* before the overlap window, and every getter above answers the copy from inside the window. */
	struct LookForDraw
	{
		Bool						hidden;
		Bool						fullyObscured;
		Bool						drawsInMirror;
		Bool						noSave;
		Bool						receivesDynamicLights;
		Bool						hasTintColor;
		Bool						hasSelectionColor;
		StealthLookType	stealthLook;
		StealthLookType	stealthDrawnLook;
		Real						stealthFade;
		Real						stealthSolid;
		Real						secondMaterialPassOpacity;
		Real						effectiveOpacity;
		UnsignedInt			tintStatus;
		Vector3					tintColor;
		Vector3					selectionColor;
		Coord3D					position;
	};
	void takeLookForDraw( void );
	/** Where it stood at the sync point, for the draw's own walks of the drawables: the Thing's cache moves with the tick. */
	const Coord3D *getPositionForDraw( void ) const { return &m_lookForDraw.position; }

	void colorFlash( const RGBColor *color, UnsignedInt decayFrames = DEF_DECAY_FRAMES, UnsignedInt attackFrames = 0, UnsignedInt sustainAtPeak = FALSE );  ///< flash a drawable in the color specified for a short time
	void colorTint( const RGBColor *color );	 ///< tint this drawable the color specified
	void setTintEnvelope( const RGBColor *color, Real attack, Real decay );	 ///< how to transition color
	void flashAsSelected( const RGBColor *color = NULL ); ///< drawable takes care of the details if you spec no color
	
	/// Return true if drawable has been marked as "selected"
	Bool isSelected( void ) const {	LOGIC_CENSUS( CENSUS_CLIENT_READ ); return m_selected; }	///< logic asks LogicThread::isSelectedForTick
	void onSelected();														///< Work unrelated to selection that must happen at time of selection
	void onUnselected();													///< Work unrelated to selection that must happen at time of unselection

	//---------------------------------------------------------------------------

	// an "instance" matrix defines the local transform of the Drawable, and is concatenated with the global transform
	void setInstanceMatrix( const Matrix3D *instance );									///< set the Drawable's instance transform
	const Matrix3D *getInstanceMatrix( void ) const { return &m_instance; }		///< get drawable instance transform
	inline Bool isInstanceIdentity() const { return m_instanceIsIdentity; }

	inline Real getInstanceScale( void ) const { return m_instanceScale; }		///< get scale that will be applied to instance matrix
	void setInstanceScale(Real value) { m_instanceScale = value;}	///< set scale that will be applied to instance matrix before rendering.

	const Matrix3D *getTransformMatrix( void ) const;	///< return the world transform

	void draw( View *view );													///< render the drawable to the given view
	void updateDrawable();														///< update the drawable

	void drawIconUI( void );													///< draw "icon"(s) needed on drawable (health bars, veterency, etc)

	void startAmbientSound( Bool onlyIfPermanent = false );
	void stopAmbientSound( void );
	void enableAmbientSound( Bool enable );
	void setTimeOfDay( TimeOfDay tod );
  Bool getAmbientSoundEnabledFromScript( void ) const { return m_ambientSoundEnabledFromScript; }

	void prependToList(Drawable **pListHead);
	void removeFromList(Drawable **pListHead);
	void setID( DrawableID id );											///< set this drawable's unique ID

	inline const ModelConditionFlags& getModelConditionFlags( void ) const { LOGIC_WINDOW_READ( WINDOW_DRAWABLE_FIELD ); return m_conditionState; }

	//
	// NOTE: avoid repeated calls to the set and clear for the condition state as they 
	// reconstruct and load models which is expensive ... wrap up all our bit flags to
	// set and clear into one function call
	//
	void clearModelConditionState( ModelConditionFlagType a ) { clearAndSetModelConditionState(a, MODELCONDITION_INVALID); }
	void setModelConditionState( ModelConditionFlagType a ) { clearAndSetModelConditionState(MODELCONDITION_INVALID, a); }
	void clearAndSetModelConditionState( ModelConditionFlagType clr, ModelConditionFlagType set );

	void clearModelConditionFlags( const ModelConditionFlags& clr ) { ModelConditionFlags empty; clearAndSetModelConditionFlags(clr, empty); }
	void setModelConditionFlags( const ModelConditionFlags& set ) { ModelConditionFlags empty; clearAndSetModelConditionFlags(empty, set); }
	void clearAndSetModelConditionFlags( const ModelConditionFlags& clr, const ModelConditionFlags& set );
	void replaceModelConditionFlags( const ModelConditionFlags &flags, Bool forceReplace = FALSE );

	/** A shot's recoil, muzzle flash and FX at the barrel's bone; with no draw module that has one, the FX
		* at the victim (fxAtVictim, a contact weapon) or at the drawable.  Inside a tick it is the drawable's
		* deferred work, in its place between the condition states (LOGIC-THREAD.md phase 3). */
	void handleWeaponFireFX(
							WeaponSlotType wslot,
							Int specificBarrelToUse,
							const FXList* fxl,
							Real weaponSpeed,
							Real recoilAmount,
							Real recoilAngle,
							const Coord3D* victimPos,
							Real damageRadius,
							Bool fxAtVictim
							);

	Int getBarrelCount(WeaponSlotType wslot) const;

	// when our Object changes teams, it calls us to let us know, so
	// we can update our model, etc., if necessary. NOTE, we don't guarantee
	// that the new team is different from the old team, nor do we guarantee
	// that the team is nonnull.
	void changedTeam();

	const TWheelInfo *getWheelInfo(void) const { return m_locoInfo ? &m_locoInfo->m_wheelInfo : NULL; }
	
	const DrawableLocoInfo *getLocoInfo() const { return m_locoInfo; }

	// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
	Bool clientOnly_getFirstRenderObjInfo(Coord3D* pos, Real* boundingSphereRadius, Matrix3D* transform);

	/**
		Find the bone(s) with the given name and return their positions and/or transforms in the given arrays.
		We look for a bone named "boneNamePrefixQQ", where QQ is 01, 02, 03, etc, starting at the
		value of "startIndex". Want to look for just a specific boneName with no numeric suffix?
		just pass zero (0) for startIndex. (no, we never look for "boneNamePrefix00".) 
		We copy up to 'maxBones' into the array(s), and return the total count found. 

		NOTE: this returns the positions and transform for the "ideal" model... that is,
		at its default rotation and scale, located at (0,0,0). You'll have to concatenate
		an Object's position and transform onto these in order to move 'em into "world space"!

		NOTE: this isn't very fast. Please call it sparingly and cache the result.
	*/
	Int getPristineBonePositions(const char* boneNamePrefix, Int startIndex, Coord3D* positions, Matrix3D* transforms, Int maxBones) const;
	Int getCurrentClientBonePositions(const char* boneNamePrefix, Int startIndex, Coord3D* positions, Matrix3D* transforms, Int maxBones) const;
	
	// this is a special-purpose call for W3DModelDraw. (srj)
	Bool getCurrentWorldspaceClientBonePositions(const char* boneName, Matrix3D& transform) const;

	Bool getProjectileLaunchOffset(WeaponSlotType wslot, Int specificBarrelToUse, Matrix3D* launchPos, WhichTurretType tur, Coord3D* turretRotPos, Coord3D* turretPitchPos = NULL) const;

	/**
		This call says, "I want the current animation (if any) to take n frames to complete a single cycle". 
		If it's a looping anim, each loop will take n frames. someday, we may want to add the option to insert
		"pad" frames at the start and/or end, but for now, we always just "stretch" the animation to fit.
		Note that you must call this AFTER setting the condition codes.
	*/
	void setAnimationLoopDuration(UnsignedInt numFrames);
	/**
		similar to the above, but assumes that the current state is a "ONCE",
		and is smart about transition states... if there is a transition state 
		"inbetween", it is included in the completion time.
	*/
	void setAnimationCompletionTime(UnsignedInt numFrames);
	
	//Kris: Manually set a drawable's current animation to specific frame.
	virtual void setAnimationFrame( int frame );

	void updateSubObjects();
	void showSubObject( const AsciiString& name, Bool show );

#ifdef ALLOW_ANIM_INQUIRIES
// srj sez: not sure if this is a good idea, for net sync reasons...
	Real getAnimationScrubScalar( void ) const; // lorenzen // returns 0 to 1... where are we between start and finish?
#endif

	UnsignedInt getExpirationDate() const { return m_expirationDate; }
	void setExpirationDate(UnsignedInt frame) { m_expirationDate = frame; }

	//
	// *ONLY* the InGameUI should do the actual drawable selection and de-selection
	//
	void friend_setSelected( void );							///< mark drawable as "selected"
	void friend_clearSelected( void );						///< clear drawable's "selected" 
	
	Vector3 * getAmbientLight( void );					///< get color value to add to ambient light when drawing
	void setAmbientLight( Vector3 *ambient );		///< set color value to add to ambient light when drawing

	const Vector3 * getTintColor( void ) const;					///< get FX color value to add to ALL LIGHTS when drawing
	const Vector3 * getSelectionColor( void ) const;					///< get FX color value to add to ALL LIGHTS when drawing

	inline TerrainDecalType getTerrainDecalType( void ) const { return m_terrainDecalType; }

	inline void setDrawableOpacity( Real value ) { m_explicitOpacity = value; }	///< set alpha/opacity value used to override defaults when drawing.
	
	// note that this is not the 'get' inverse of setDrawableOpacity, since stealthing can also affect the effective opacity!
	Real getEffectiveOpacity() const;		///< get alpha/opacity value used to override defaults when drawing.
	void setEffectiveOpacity( Real pulseFactor, Real explicitOpacity = -1.0f );
	
	// this is for the add'l pass fx which operates completely independently of the stealth opacity effects. Draw() does the fading every frame.
	inline Real getSecondMaterialPassOpacity() const { return LogicThread::isInsideWindow() ? m_lookForDraw.secondMaterialPassOpacity : m_secondMaterialPassOpacity; }		///< get alpha/opacity value used to render add'l  rendering pass.
	void setSecondMaterialPassOpacity( Real op ) { m_secondMaterialPassOpacity = op; }; ///< set alpha/opacity value used to render add'l  rendering pass.
	
	// both of these assume that you are starting at one extreme 100% or 0% opacity and are trying to go to the other!! -- amit
	void fadeOut( UnsignedInt frames );		///< fade object out...how gradually this is done is determined by frames
	void fadeIn( UnsignedInt frames );		///< fade object in...how gradually this is done is determined by frames

	void preloadAssets( TimeOfDay timeOfDay );	///< preload the assets
	
	Bool isVisible();											///< for limiting tree sway, etc to visible objects

	Bool getShouldAnimate( Bool considerPower ) const;

	// flash drawable methods ---------------------------------------------------------
  Int getFlashCount( void ) { return m_flashCount; }
	void setFlashCount( Int count ) { m_flashCount = count; }
	void setFlashColor( Color color ) { m_flashColor = color; }
  void saturateRGB(RGBColor& color, Real factor);// not strictly for flash color, but it is the only practical use for this
	//---------------------------------------------------------------------------------

	// caption text methods -----------------------------------------------------------
	void setCaptionText( const UnicodeString& captionText );
	void clearCaptionText( void );
	UnicodeString getCaptionText( void );
	//---------------------------------------------------------------------------------

	DrawableIconInfo* getIconInfo();															///< lazily allocates, if necessary
	void killIcon(DrawableIconType t) { if (m_iconInfo) m_iconInfo->killIcon(t); }
	Bool hasIconInfo() const { return m_iconInfo != NULL; }

  
  Bool getReceivesDynamicLights( void ) { return LogicThread::isInsideWindow() ? m_lookForDraw.receivesDynamicLights : m_receivesDynamicLights; };
  void setReceivesDynamicLights( Bool set ) { m_receivesDynamicLights = set; };
  
  //---------------------------------------------------------------------------------
  // Stuff for overriding ambient sound
  const AudioEventInfo * getBaseSoundAmbientInfo() const; //< Possible starting point if only some parameters are customized
  void enableAmbientSoundFromScript( Bool enable );
  const AudioEventRTS * getAmbientSound() const { return m_ambientSound == NULL ? NULL : &m_ambientSound->m_event; }
  void setCustomSoundAmbientOff(); //< Kill the ambient sound
  void setCustomSoundAmbientInfo( DynamicAudioEventInfo * customAmbientInfo ); //< Set ambient sound.
  void clearCustomSoundAmbient( ) { clearCustomSoundAmbient( true ); } //< Return to using defaults
  Bool getAmbientSoundEnabled( void ) const { return m_ambientSoundEnabled; }
  void mangleCustomAudioName( DynamicAudioEventInfo * audioToMangle ) const;


  Real friend_getStealthOpacity( void ) { return m_stealthOpacity; }
  Real friend_getExplicitOpacity( void ) { return m_explicitOpacity; }
  Real friend_getEffectiveStealthOpacity( void ) { return m_effectiveStealthOpacity; }
  
protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );
	void xferDrawableModules( Xfer *xfer );

	void	startAmbientSound( BodyDamageType dt, TimeOfDay tod, Bool onlyIfPermanent = false );

	Drawable *asDrawableMeth() { return this; }
	const Drawable *asDrawableMeth() const { return this; }

	inline Module** getModuleList(ModuleType i)
	{
		Module** m = m_modules[i - FIRST_DRAWABLE_MODULE_TYPE];
		return m;
	}

	inline Module* const* getModuleList(ModuleType i) const
	{
		Module** m = m_modules[i - FIRST_DRAWABLE_MODULE_TYPE];
		return m;
	}

	void applyPhysicsXform(Matrix3D* mtx);

	struct PhysicsXformInfo
	{
		Real m_totalPitch;					///< Current total pitch for this frame.
		Real m_totalRoll;						///< Current total roll for this frame.
		Real m_totalYaw;						///< Current total yaw for this frame
		Real m_totalZ;

		PhysicsXformInfo() : m_totalPitch(0), m_totalRoll(0), m_totalYaw(0), m_totalZ(0) { }
	};

	Bool calcPhysicsXform(PhysicsXformInfo& info);
	void calcPhysicsXformThrust(const Locomotor *locomotor, PhysicsXformInfo& info);
	void calcPhysicsXformHoverOrWings(const Locomotor *locomotor, PhysicsXformInfo& info);
	void calcPhysicsXformGround(const Locomotor *locomotor, PhysicsXformInfo& info);
	void calcPhysicsXformTreads(const Locomotor *locomotor, PhysicsXformInfo& info);
	void calcPhysicsXformWheels(const Locomotor *locomotor, PhysicsXformInfo& info);
	void calcPhysicsXformMotorcycle( const Locomotor *locomotor, PhysicsXformInfo& info );

	const AudioEventRTS& getAmbientSoundByDamage(BodyDamageType dt);

  void clearCustomSoundAmbient( bool restartSound ); //< Return to using defaults

#ifdef _DEBUG
	void validatePos() const;
#endif

	virtual void reactToTransformChange(const Matrix3D* oldMtx, const Coord3D* oldPos, Real oldAngle);
	void updateHiddenStatus();

private:

	// note, these are lazily allocated!
	TintEnvelope*		m_selectionFlashEnvelope;	///< used for selection flash, works WITH m_colorTintEnvelope
	TintEnvelope*		m_colorTintEnvelope;			///< house color flashing, etc... works WITH m_selectionFlashEnvelope
																//   this used to use m_ambientLight... but this replaces it
																//   int most places. It works harder to change the drawable's
																//   color, by tinting all four scene lights, not just ambient
																//   zero = no effect
																//   1    = full effect

	TerrainDecalType m_terrainDecalType; ///<current decal in use by m_terrainDecal

	Real m_explicitOpacity;			///< opacity level. 1.0f == Solid/Opaque.
	Real m_stealthOpacity;			///< <<minimum>> opacity due to stealth. pulse is between opaque and this
	Real m_effectiveStealthOpacity;			///< opacity actually used to render with, after the pulse and stuff.

	Real m_decalOpacityFadeTarget;
	Real m_decalOpacityFadeRate;
	Real m_decalOpacity;

	Object *m_object;						///< object (if any) that this drawable represents
		
	DrawableID m_id;						///< this drawable's unique ID
	Drawable *m_nextDrawable; 
	Coord3D m_smoothPrevPos;		///< R1: the position on the logic tick before m_smoothCurPos
	Coord3D m_smoothCurPos;			///< R1: the position on the last logic tick the client saw
	UnsignedInt m_smoothFrame;		///< R1: the client frame m_smoothCurPos was taken on
	Bool m_smoothHavePrev;			///< R1: m_smoothPrevPos is the tick just before, and continuous with it
	Bool m_motionDiscontinuity;		///< R1: markMotionDiscontinuity since the last capture
	Drawable *m_prevDrawable;		///< list links

  DynamicAudioEventInfo *m_customSoundAmbientInfo; ///< If not NULL, info about the ambient sound to attach to this object

	UnsignedInt m_status;				///< status bits (see DrawableStatus enum)
	UnsignedInt m_tintStatus;				///< tint color status bits (see TintStatus enum)
	UnsignedInt m_prevTintStatus;///< for edge testing with m_tintStatus
	
	enum FadingMode
	{
		FADING_NONE,
		FADING_IN,
		FADING_OUT
	};
	FadingMode		m_fadeMode;
	UnsignedInt		m_timeElapsedFade;			///< for how many frames have i been fading
	UnsignedInt		m_timeToFade;						///< how slowly am I fading

	UnsignedInt		m_shroudClearFrame;						///< Last frame the local player saw this drawable "OBJECTSHROUD_CLEAR"
	ObjectShroudStatus	m_shroudForDraw;				///< see setShroudForDraw
	Bool					m_objectDeadForDraw;				///< see setShroudForDraw
	ObjectForDraw	m_objectForDraw;						///< see setObjectForDraw
	LookForDraw		m_lookForDraw;							///< see takeLookForDraw

	DrawableLocoInfo*	m_locoInfo;	// lazily allocated

	DynamicAudioEventRTS*	m_ambientSound;		///< sound module for ambient sound (lazily allocated)

	Module** m_modules[NUM_DRAWABLE_MODULE_TYPES];

	StealthLookType m_stealthLook;
	StealthLookType m_stealthDrawnLook;	///< look the shader is fading, while m_stealthLook is the target
	Real m_stealthFade;									///< 0 invisible / 1 fully the drawn look

	Int m_flashCount;           ///< number of times to flash the drawable
	Color m_flashColor;					///< color to flash the drawable

	// the last colour setIndicatorColor was asked for, before the player's scheme was applied to it.
	// A model bakes its tint into its render object, so when the scheme moves the only way back to
	// the right colour is the one that was asked for - a garrisoned building is wearing the
	// occupier's, not its owner's, and re-deriving it from the object would lose that.
	Color m_indicatorColorAsked;

	Matrix3D m_instance;				///< The instance matrix that holds the initial/default position & orientation
	Real m_instanceScale;				///< the uniform scale factor applied to the instance matrix before it is sent to W3D. 

	DrawableInfo				m_drawableInfo;		///< structure pointed to by W3D render objects so they know which drawable they belong to.

	ModelConditionFlags	m_conditionState;				///< The Drawables current behavior state
	Real								m_lastConstructDisplayed;						///< last construct percent used to make the string
	DisplayString*			m_constructDisplayString;  ///< string to display construction % complete
	DisplayString*			m_supplyCashDisplayString; ///< string to display the cash left in a supply pile
	Int									m_lastSupplyCashDisplayed; ///< so the string is only rebuilt when the pile shrinks
	DisplayString*			m_chargeTimeDisplayString; ///< seconds left on a public timer charging here
	DisplayString*			m_captionDisplayString;		///< string to display caption
	DisplayString*			m_groupNumber;						///< string to display the group number of this drawable

	UnsignedInt					m_expirationDate;		///< if nonzero, Drawable should destroy itself at this frame
	DrawableIconInfo*		m_iconInfo;					///< lazily allocated!

	Real m_secondMaterialPassOpacity;			///< drawable gets rendered again in hardware with an extra material layer
	// --------- BYTE-SIZED THINGS GO HERE
	Byte m_selected;						///< drawable is selected or not
	Bool m_hidden;							///< drawable is "hidden" or not (overrides stealth effects)
	Bool m_hiddenByStealth;			///< drawable is hidden due to stealth
	Bool m_instanceIsIdentity;	///< If true, instance matrix can be skipped
	Bool m_drawableFullyObscuredByShroud;	///<drawable is hidden by shroud/fog
  Bool m_ambientSoundEnabled;
  Bool m_ambientSoundEnabledFromScript;

  Bool m_receivesDynamicLights;

#ifdef DIRTY_CONDITION_FLAGS
	mutable Bool m_isModelDirty;				///< if true, must call replaceModelConditionState() before drawing or accessing drawmodule info
#endif
	mutable Bool m_clientWorkQueued;		///< this drawable's ID is in the list flushPendingClientWork walks
	Bool m_transformPending;						///< the tick moved it; the draw modules have not been told
	Bool m_renderPending;								///< made in a tick: the draw modules have not built their render objects
	Int m_clipFeedbackSlots;						///< the draw modules' getProjectileClipFeedbackSlots, or'd at creation
	struct PendingStep
	{
		ModelConditionFlags condition;		///< a state an in-tick flush would have applied, when work is empty
		std::function<void()> work;				///< else a doClientWork the tick deferred
		Bool movesBones;										///< an on-demand flush inside the tick runs this work too
	};
	mutable std::vector<PendingStep> m_pendingSteps;	///< what the tick deferred, in the order it happened

	void queueClientWork() const;
	void buildRenderObjects();
	void recordConditionStep() const;
	void deferModelConditionFlush() const;
	void flushModelCondition( Bool onDemand ) const;
	void markModelDirty();

	//*******************************************
	//Perhaps we can move this out of Drawable???
public:
	static void killStaticImages();
#ifdef DIRTY_CONDITION_FLAGS
	// only for StDrawableDirtyStuffLocker!
	static void friend_lockDirtyStuffForIteration();
	static void friend_unlockDirtyStuffForIteration();
#endif

	//For now, you can only have one emoticon at a time. Changing it will clear the previous one.
	void clearEmoticon();
	void setEmoticon( const AsciiString &name, Int duration );
	void drawUIText( void );				///< draw the group number of this unit // public so gameclient can call
private:
	// "icon" drawing methods **************
	void drawConstructPercent( const IRegion2D *healthBarRegion );  ///< display % construction complete
	void drawSupplyCash( const IRegion2D *healthBarRegion );				///< display the cash left in a supply pile
	void drawCaption( const IRegion2D *healthBarRegion );						///< draw caption
	void drawAmmo( const IRegion2D *healthBarRegion );							///< draw icons
	void drawContained( const IRegion2D *healthBarRegion );					///< draw icons
	void drawVeterancy( const IRegion2D *healthBarRegion );					///< draw veterency information

	void drawEmoticon( const IRegion2D* healthBarRegion );
	void drawHealthBar( const IRegion2D* healthBarRegion );					///< draw heath bar
	void drawCaptureProgress( void );																///< the clock under the health bar of the building this one is taking
	void drawAltitudeLine( void );																	///< the line from an aircraft in the air down to the ground under it
	void drawHealing( const IRegion2D* healthBarRegion );						///< draw icons
	void drawEnthusiastic( const IRegion2D* healthBarRegin );				///< draw icons
#ifdef ALLOW_DEMORALIZE
	void drawDemoralized( const IRegion2D* healthBarRegion );				///< draw icons
#endif
	void drawBombed( const IRegion2D* healthBarRegion );						///< draw icons
	void drawDisabled( const IRegion2D* healthBarRegion );					///< draw icons
	void drawBattlePlans( const IRegion2D* healthBarRegion );				///< Icons rendering for active battle plan statii

	Bool drawsAnyUIText( void );

	/// the modules the icon pass looks for on the object; its modules are made with it and never change
	enum IconModule
	{
		ICON_MODULE_DOCK					= 1 << 0,
		ICON_MODULE_STICKY_BOMB		= 1 << 1,
		ICON_MODULE_PRODUCTION		= 1 << 2,
		ICON_MODULE_SPECIAL_POWER	= 1 << 3,
		ICON_MODULES_KNOWN				= 1 << 7
	};
	UnsignedByte m_iconModules = 0;		///< IconModule bits, found the first time the icon pass asks
	Bool hasIconModule( IconModule module );
public:
	Bool hasProductionModule( void ) { return hasIconModule( ICON_MODULE_PRODUCTION ); }	///< the object's; it has one
private:

	static Bool							s_staticImagesInited;
	static const Image*			s_veterancyImage[LEVEL_COUNT];
	static const Image*			s_fullAmmo;
	static const Image*			s_emptyAmmo;
	static const Image*			s_fullContainer;
	static const Image*			s_emptyContainer;
	static Anim2DTemplate**	s_animationTemplates;

#ifdef DIRTY_CONDITION_FLAGS
	static Int							s_modelLockCount;
#endif


	static void initStaticImages();
	//*******************************************

};


#ifdef DIRTY_CONDITION_FLAGS
class StDrawableDirtyStuffLocker
{
public:
	StDrawableDirtyStuffLocker()
	{
		Drawable::friend_lockDirtyStuffForIteration();
	}
	~StDrawableDirtyStuffLocker()
	{
		Drawable::friend_unlockDirtyStuffForIteration();
	}
};
#endif

#endif // _DRAWABLE_H_
