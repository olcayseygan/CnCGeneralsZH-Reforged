/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

/*
** GPU copies of A2's textures and buffers (decision 7, phase A3c).  A copy is made the first time a draw needs it, and brought up to date
** whenever a draw finds the A2 object's version() moved.
**
** What a draw sees is what D3D9 would have shown it.  A copy's new bytes are taken when the draw that
** needs them is recorded, and the batch's copy pass uploads them before any of the batch's draws.  So a
** copy that a draw of this batch has already used, and that changes again, flushes the batch first:
** otherwise the earlier draw would see the later bytes.  Those flushes are counted, since they should be
** rare: the engine writes its dynamic buffers, which draws copy into the staging stream instead, and
** loads its textures once.
**
** Texture formats: DXT1, DXT2/3 and DXT4/5 go up as BC1, BC2 and BC3 where the GPU samples them, and
** A8R8G8B8 as B8G8R8A8, byte for byte.  Everything else is expanded to B8G8R8A8 with A2's codec, since
** SDL3 GPU has no swizzle: X8R8G8B8 must read alpha 1, the luminance formats replicate it.
**
** A2 objects die on whatever thread drops them (posixResourceDestroyed): their copies leave the table
** under a mutex at once, and are handed to the frame to release after the batch, on the main thread.
*/

#pragma once

#ifndef SDLRESOURCEMIRROR_H
#define SDLRESOURCEMIRROR_H

#include "Platform/RenderTypes.h"
#include "Platform/D3D9Posix.h"

#include <mutex>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

class PosixBufferStorage;
class PosixImage;
class SdlGpuFrame;
struct SDL_GPUBuffer;
struct SDL_GPUTexture;

/// How a level of `format` goes up: the SDL_GPUTextureFormat it is sampled as, and whether its bytes
/// go up as they are (else they are expanded to B8G8R8A8).  `bc_ok` says whether the GPU samples BC
/// formats; a block-compressed base level whose sides are not multiples of four is expanded too.
/// False for a format A2's codec cannot read.
bool Sdl_Texture_Format(D3DFORMAT format, unsigned int width, unsigned int height, bool bc_ok,
	unsigned int & sdl_format, bool & native);

/// The bytes one level uploads, native or expanded.
uint32_t Sdl_Level_Upload_Size(const PosixImage & image, bool native);

/// Writes one level's upload bytes: a copy when native, else B8G8R8A8 rows, top first.
bool Sdl_Convert_Level(const PosixImage & image, bool native, uint8_t * out);

class SdlResourceMirrors
{
public:
	/// Sets posixResourceDestroyed; only one table lives at a time.
	explicit SdlResourceMirrors(SdlGpuFrame * frame);
	~SdlResourceMirrors();

	/// The copy of a 2D texture, current for a draw being recorded now.  Null, with the reason, for a
	/// texture that is not 2D (A3c draws 2D textures only) or a format there is no way up for.
	SDL_GPUTexture * Texture(IDirect3DBaseTexture9 * texture, std::string & refusal);
	/// Whether the object already has a GPU copy: a render target nothing has drawn into has none, and its
	/// CPU image is then the current one.
	bool Has_Copy(const void * owner) const;
	/// A standalone render-target (colour) or depth surface's GPU texture, made on first use (A3d).  Its
	/// pixels are the GPU's: nothing is uploaded from its image.
	SDL_GPUTexture * Surface(IDirect3DSurface9 * surface, bool depth, std::string & refusal);
	/// The copy of a vertex or index buffer's bytes (`owner` is the buffer object: its address is the key).
	SDL_GPUBuffer * Buffer(const void * owner, const PosixBufferStorage & storage, std::string & refusal);
	/// 1x1 opaque white: what a slot with no texture bound samples.
	SDL_GPUTexture * White();

	/// Hands the copies of objects that have died to the frame, to go after the batch.  Main thread.
	void Collect_Dead();

	unsigned int Stale_Flushes() const { return StaleFlushes; }
	unsigned int Textures_Uploaded() const { return TexturesUploaded; }
	unsigned int Buffers_Uploaded() const { return BuffersUploaded; }
	size_t Live_Copies() const;

private:
	struct Copy
	{
		SDL_GPUTexture * Texture;
		SDL_GPUBuffer * Buffer;
		uint32_t Size;						///< a buffer's
		bool Native;						///< a texture's levels go up as they are
		std::vector<uint32_t> Versions;		///< what the copy holds, one per level (one for a buffer)
		uint64_t UsedBatch;					///< the last batch a draw recorded it in
	};

	static void Destroyed(const void * resource);
	/// Flushes a batch that already uses this copy. Failure leaves its pending upload untouched.
	bool Prepare_Update(Copy & copy);

	SdlGpuFrame * Frame;
	mutable std::mutex Lock;							///< Copies and Dead: the destroyed hook runs on any thread
	std::unordered_map<const void *, Copy> Copies;
	std::vector<Copy> Dead;
	SDL_GPUTexture * WhiteTexture;
	int BcSupported;									///< -1 not asked yet
	unsigned int StaleFlushes;
	unsigned int TexturesUploaded;
	unsigned int BuffersUploaded;
};

#endif // SDLRESOURCEMIRROR_H
