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

// FILE: W3DParticleSys.h /////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DParticleSys_H_
#define __W3DParticleSys_H_

#include "GameClient/ParticleSys.h"
#include "GameClient/ParticleClosedForm.h"
#include "W3DDevice/GameClient/GpuParticles.h"
#include "WW3D2/pointgr.h"
#include "WW3D2/streak.h"
#include "WW3D2/rinfo.h"
#include "WWLib/bittype.h"
#include "Platform/ModernWaterBuild.h"

#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

class TextureClass;

namespace FireLights { struct Light; }
/// The Firelight option's lights the last doParticles picked: none with the option off.
const FireLights::Light *getFirelights( Int *count );

//=============================================================================
/** W3D implementation of the game display which is responsible for creating
  * all interaction with the screen and updating the display 
	*/
class W3DParticleSystemManager : public ParticleSystemManager
{

public:
	W3DParticleSystemManager();
	~W3DParticleSystemManager();

	virtual void reset();
	/// Forget only cached contamination footprints; all particle systems remain alive.
	/// With fadeIn=false, fields collected on the next render appear immediately,
	/// including while logic is paused. Later fields retain their normal fade-in.
	void invalidateGroundFields(bool fadeIn = true);
	static void releaseCloudResources(); // Before display/device teardown or reset.
	virtual void doParticles(RenderInfoClass &rinfo);
	virtual void queueParticleRender();
	///< returns the number of particles shown on screen per frame
	virtual Int getOnScreenParticleCount() { return m_onScreenParticleCount; }
	/// at the render's sync point: the systems' stealth fades and the fields' fades, which read the logic
	void takeReadsForRender( void );

	/// The GPU particle path (GpuParticles.h, GPU-PARTICLES.md): on a Direct3D 11 frame with the pool
	/// on and Classic off, an eligible system's spawns become records in the device's ring.
	virtual Bool acceptsGpuParticles( void ) const;
	virtual Bool spawnGpuParticle( ParticleSystem *system, const ParticleInfo *info );
	virtual void releaseGpuSystem( ParticleSystem *system );
	/// The textures as the base does, then the GPU path's ring, programs, slices and ground, so the
	/// match does not make them on its first frames.
	virtual void preloadAssets( TimeOfDay timeOfDay );
	/// Before the Direct3D 11 device goes: the ring, the table and the texture array.
	static void releaseGpuResources( void );

private:
	enum { MAX_POINTS_PER_GROUP = 512 };

	PointGroupClass *m_pointGroup;							///< the point group that contains all of the particles
	StreakLineClass *m_streakLine;							///< the streak class that contains all of the streaks
	ShareBufferClass<Vector3> *m_posBuffer;			///< array of particle positions
	ShareBufferClass<Vector4> *m_RGBABuffer;		///< array of particle color and alpha
	ShareBufferClass<float> *m_sizeBuffer;			///< array of particle sizes
	ShareBufferClass<uint8> *m_angleBuffer;			///< array of particle orientations
	ShareBufferClass<uint8> *m_frameBuffer;			///< array of particle flipbook frames
	Bool m_readyToRender;											///< if true, it is OK to render

public:
	/// One sorted billboard system's share of a frame's fill: reserved on the render thread, written by
	/// one job, inserted into the sorting pool on the render thread again, in system list order.
	struct BillboardFill
	{
		ParticleSystem *												system;
		PointGroupClass::SortingBillboardRange	range;
		TextureClass *													texture;
		ShaderClass															shader;
		Int																			capacity;					///< quads reserved: the system's particles, at most MAX_POINTS_PER_GROUP
		UnsignedInt															fieldIncrement;		///< 1 if every drawn particle counts against the field budget
		Real																		fade;							///< ParticleSystem::getStealthFade, above zero
		Int																			drawn;						///< written by the job
		UnsignedInt															pastLimit;				///< written by the job: on-screen particles past capacity
		Bool																		glow;							///< written by the job: the fire's glow is in the normals, not the colour
		Bool																		fire;							///< the template's FireLook: each particle's age and seed go in v2
	};

	/// One particle the sun's smoke map can take, as the last particle update left it.
	struct SunMapCaster
	{
		Real							x, y, z, radius, opticalDepth;
		ParticleSystem *	system;
	};

	/// Every particle of a billboarded alpha system that is drawn at all, with the optical depth its
	/// alpha and its system's stealth fade give it, in system list order.  Walked once a particle
	/// update; fillSmokeMap culls it to its own sun every pass.
	const std::vector<SunMapCaster> &getSunMapCasters( void );

private:
	/// A system with particles that the local player sees at all, and how much of it.
	struct SystemView
	{
		ParticleSystem *	system;
		Real							fade;				///< ParticleSystem::getStealthFade, above zero
#if ZH_MODERN_WATER
		ObjectID owner;					///< vessel foam's attached object at the sync point
#endif
	};

	void refreshFrameView( void );

	std::vector<BillboardFill> m_billboardFills;	///< kept across frames so the fill does not allocate

	/// Nothing moves a particle between two particle updates and nothing but the logic changes a
	/// stealth fade, and both run once a logic frame, so the walk over every system is taken once a
	/// logic frame and every pass drawn until the next one reads this.  The counts and the next system
	/// id catch anything that adds or takes a system or a particle away in between; a reset clears it.
	/// That only holds while every add and remove of a system or a particle moves m_particleCount,
	/// m_particleSystemCount or m_uniqueSystemID, and while the stealth look, the opacity and the
	/// attachment that decide a fade change in the logic alone, or in doDrawModule before a pass's first
	/// refresh.  Anything else that changes one of them mid-frame is not seen until the next logic frame.
	std::vector<SystemView>		m_frameView;
	std::vector<ParticleSystem *>	m_frameFires;			///< the additive and Emissive FireLook systems among them, for the Firelight option
	std::vector<Real>					m_frameFireFades;		///< and their fades
	std::vector<ParticleSystem *>	m_fieldSystems;		///< the contamination fields' systems, empty ones too
	std::vector<SunMapCaster>	m_sunMapCasters;
	Bool											m_frameViewValid;
	Bool											m_sunMapCastersValid;
	UnsignedInt								m_frameViewLogicFrame;
	UnsignedInt								m_frameViewFrame;
	UnsignedInt								m_frameViewParticles;
	UnsignedInt								m_frameViewSystems;
	ParticleSystemID					m_frameViewNextID;

	/// One system entry slot of the GPU path.  A released slot is free again from reuseFrame, when
	/// the last record that names it has died.
	struct GpuSlot
	{
		ParticleSystem *	system;				///< NULL while free
		UnsignedInt				slice;				///< its texture's slice in the device's array
		UnsignedInt				reuseFrame;
	};
	/// An additive or Emissive GPU system the fire light is taken from, seen at the sync point.
	struct GpuFireSystem
	{
		ParticleSystem *	system;
		Real							fade;
		UnsignedInt				slot;
	};
	enum { GPU_NO_SLOT = 0xFFFFFFFF };
	/// Death frames are counted in a ring of this many frames, so the live count needs no walk.
	enum { GPU_DEATH_FRAMES = 0x10000 };

	UnsignedInt gpuSlotFor( ParticleSystem *system );
	UnsignedInt gpuTextureSlice( const AsciiString &name );
	void markGpuSlotDirty( UnsignedInt slot );
	void drainGpuDeaths( UnsignedInt now );
	Bool prepareGpu( void );								///< the device's half made, or found; false if it refused
	void sendGpuGround( void );
	void drawGpuSlices( void );
	void refreshGpuSystems( void );					///< entries and fades, with the frame view
	void drawGpuParticles( RenderInfoClass &rinfo );	///< upload, age, and both passes
	void resetGpuParticles( void );

	Bool																m_gpuSupported;			///< GpuParticles::Supported, asked on the main thread only
	UnsignedInt													m_gpuCapacity;			///< ring records, fixed by the first spawn; 0 before it
	Bool																m_gpuRefused;				///< the device refused the ring: CPU from here on
	UnsignedInt64												m_gpuWritten;				///< records ever written; the next goes at m_gpuWritten % capacity
	UnsignedInt64												m_gpuTail;					///< the oldest record that may be alive
	UnsignedInt64												m_gpuPendingFirst;	///< the record m_gpuPending[0] is
	std::vector<UnsignedInt>						m_gpuDeath;					///< per ring record, the first frame it is surely dead
	std::vector<UnsignedInt>						m_gpuDeathCounts;		///< records alive that die on each frame, by frame % GPU_DEATH_FRAMES
	UnsignedInt													m_gpuLive;					///< records alive
	UnsignedInt													m_gpuDrainedFrame;	///< m_gpuDeathCounts is taken off m_gpuLive up to this frame
	std::vector<ParticleGpuSpawn>				m_gpuPending;				///< written since the last upload
	std::vector<GpuSlot>								m_gpuSlots;
	std::vector<ParticleGpuSystemEntry>	m_gpuEntries;				///< as the device has them, one a slot
	std::vector<Real>										m_gpuFades;
	Bool																m_gpuFadesDirty;		///< m_gpuFades differs from the device's copy
	std::deque<UnsignedInt>							m_gpuFreeSlots;			///< in release order, so the front is the first to come free
	UnsignedInt													m_gpuDirtyFirst, m_gpuDirtyEnd;	///< entries the device lacks
	std::unordered_map<ParticleSystem *, UnsignedInt>	m_gpuSlotOf;	///< GPU_NO_SLOT for a system refused for good
	ParticleSystem *										m_gpuLastSystem;		///< one spawn after another is mostly the same system
	UnsignedInt													m_gpuLastSlot;
	std::vector<TextureClass *>					m_gpuTextures;			///< by slice, a reference held on each
	std::map<TextureClass *, UnsignedInt>	m_gpuTextureSlices;
	std::deque<UnsignedInt>							m_gpuSliceQueue;		///< slices whose picture the device has not drawn yet
	UnsignedInt													m_gpuSliceCursor;		///< the next slice checked for a view that changed
	Bool																m_gpuTexturesFullTold;
	std::vector<GpuFireSystem>					m_gpuFireSystems;
	UnsignedInt													m_gpuSpawned;				///< since the last GPUPARTICLES line
	UnsignedInt													m_gpuEvicted;
	UnsignedInt													m_gpuLogFrame;
};

#endif  // end __W3DParticleSys_H_
