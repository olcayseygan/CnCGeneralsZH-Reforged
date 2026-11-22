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

// GpuParticles.h /////////////////////////////////////////////////////////////////////////////////
// The device half of the GPU particle path (docs/plans/GPU-PARTICLES.md in the wrapper): a ring of
// spawn records written once at birth, a table of system entries, one texture array for every
// particle texture, and the passes that age the ring in closed form and draw it.  The bookkeeping
// (which slot a system has, where the ring's head and tail are, what died) is the particle
// manager's, in W3DParticleSys.cpp; this only holds what the device holds.
//
// DX11GpuParticles.cpp supplies Direct3D 11; SdlGpuParticlesAdapter.cpp supplies native SDL GPU.
// Builds without a device provider use GpuParticlesUnsupported.cpp and retain CPU particles.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef GPU_PARTICLES_H
#define GPU_PARTICLES_H

struct IDirect3DBaseTexture9;
struct ParticleGpuSpawn;
struct ParticleGpuSystemEntry;

namespace GpuParticles
{

/// System entry slots, the low 16 bits of a spawn record's m_systemSlotSeed.
enum { SYSTEM_SLOTS = 16384 };

/// Particle texture slices: one 128 square RGBA8 layer with mips each.  Past this a system with a
/// new texture stays on the CPU (W3DParticleSys.cpp says so once in the log).
enum { TEXTURE_SLICES = 256 };

/// The frame's camera in the device's layout, row vectors: clip = world * view * projection.
struct Camera
{
	float view[ 16 ];
	float projection[ 16 ];
};

/// Direct3D 11 presents on a feature level 11 device.  False under -d3d9, the SDL3 builds and
/// -headless; the caller adds the Classic setting and the pool size.
bool Supported();

/** Make, or find still there, the ring of capacity records, the system table, the texture array and
	* the programs.  False when the device refused any of them; nothing is kept then.  fresh comes back
	* true when what the device held before is gone (first call, a new device, a new capacity, a
	* release): the ring is empty and every entry and slice has to be written again. */
bool Prepare( unsigned capacity, bool *fresh );

/// count records into the ring from ring index first; first + count never passes the capacity.
void Write_Spawns( unsigned first, const ParticleGpuSpawn *spawns, unsigned count );

/// count system entries from slot first.
void Write_Systems( unsigned first, const ParticleGpuSystemEntry *entries, unsigned count );

/// The stealth fade of slots 0 .. count - 1, zero for a system the local player does not see.
void Write_Fades( const float *fades, unsigned count );

/** The terrain's height at each of width by height vertices, row by row, for the systems that rest
	* on the ground (an entry's flag 2).  mapping takes world x and y to the texture's coordinates:
	* u = x * mapping[0] + mapping[2], v = y * mapping[1] + mapping[3]. */
void Set_Ground( const float *heights, unsigned width, unsigned height, const float mapping[ 4 ] );

/** The picture of texture slice slice: drawn into the array again, at the next Evaluate, when
	* texture's Direct3D 11 view is not the one drawn last time (a texture finishing its background
	* load swaps it), and left as it is otherwise.  A null texture leaves the slice clear; every slice
	* starts clear. */
void Set_Slice_Texture( unsigned slice, IDirect3DBaseTexture9 *texture );

/** The multiply Evaluate puts over the colour of the systems whose entry carries flag 4 (smoke and
	* dust, ParticleSystemManager::setSmokeTint); white leaves them as they are.  A change ages the
	* ring again. */
void Set_Smoke_Tint( const float tint[ 3 ] );

/** Age the count records from ring index first (wrapping) to the frame now plus smooth, a fraction
	* of an update, in closed form, once a frame before either pass draws.  Nothing is aged again when
	* neither the range, the time nor any record changed since the last call. */
void Evaluate( unsigned first, unsigned count, unsigned now, float smooth );

/** Draw what Evaluate aged with this ParticleSystemInfo::ParticleShaderType, ALPHA newest first and
	* ADDITIVE in any order, depth read and not written.  Into the scene, soft where the backend allows
	* it; into a mirror's own target and depth with no fade; nothing into the sun's map. */
void Draw( unsigned shaderType, const Camera &camera );

/// Before the device goes or is reset: everything above is made again by the next Prepare.
void ReleaseDevice();

} // namespace GpuParticles

#endif // GPU_PARTICLES_H
