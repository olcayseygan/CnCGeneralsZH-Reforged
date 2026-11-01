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

// GPU copies of A2's textures and buffers (decision 7, phase A3c).  See SdlResourceMirror.h.

#include "SdlResourceMirror.h"
#include "SdlCreationLog.h"
#include "PosixPixelCodec.h"
#include "PosixResources9.h"
#include "SdlGpuFrame.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SdlResourceMirrors * LiveMirrors = NULL;

// A development aid until A3d's capture: ZH_GPU_DUMP_TEXTURES=<dir> writes level 0 of every texture at
// least 256 wide, decoded to RGB, and its alpha as a second picture, each time it goes up.
static void dump_texture_if_asked(const PosixImage & image, const void * owner, unsigned int upload)
{
	const char * dir = getenv("ZH_GPU_DUMP_TEXTURES");
	if (dir == NULL || image.width() < 256) {
		return;
	}
	std::vector<uint8_t> bgra((size_t)image.width() * image.height() * 4);
	if (!Sdl_Convert_Level(image, false, &bgra[0])) {
		return;
	}
	for (int alpha = 0; alpha < 2; ++alpha) {
		char path[1024];
		snprintf(path, sizeof(path), "%s/tex_%p_%u_%ux%u_fmt%u%s.ppm", dir, owner, upload, image.width(), image.height(),
			(unsigned int)image.format(), alpha ? "_alpha" : "");
		FILE * file = fopen(path, "wb");
		if (file == NULL) {
			return;
		}
		fprintf(file, "P6\n%u %u\n255\n", image.width(), image.height());
		for (size_t i = 0; i < bgra.size(); i += 4) {
			const uint8_t rgb[3] = { alpha ? bgra[i + 3] : bgra[i + 2], alpha ? bgra[i + 3] : bgra[i + 1],
				alpha ? bgra[i + 3] : bgra[i] };
			fwrite(rgb, 1, 3, file);
		}
		fclose(file);
	}
}

bool Sdl_Texture_Format(D3DFORMAT format, unsigned int width, unsigned int height, bool bc_ok,
	unsigned int & sdl_format, bool & native)
{
	const bool whole_blocks = width % 4 == 0 && height % 4 == 0;
	native = true;
	switch (format) {
		case D3DFMT_A8R8G8B8:
			sdl_format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
			return true;
		case D3DFMT_DXT1:
			sdl_format = SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
			break;
		// DXT2 and DXT4 are the premultiplied forms.  The blocks are the same and D3D9 samples them as
		// stored, so they go up as DXT3 and DXT5 do.
		case D3DFMT_DXT2:
		case D3DFMT_DXT3:
			sdl_format = SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
			break;
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
			sdl_format = SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
			break;
		default:
			native = false;
			break;
	}
	if (native && bc_ok && whole_blocks) {
		return true;
	}
	native = false;
	sdl_format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
	return posixCanDecode(format);
}

uint32_t Sdl_Level_Upload_Size(const PosixImage & image, bool native)
{
	return native ? (uint32_t)image.slicePitch() : (uint32_t)(image.width() * image.height() * 4);
}

static uint8_t unit_to_byte(float value)
{
	if (!(value > 0.0f)) return 0;
	if (value >= 1.0f) return 255;
	return (uint8_t)(value * 255.0f + 0.5f);
}

bool Sdl_Convert_Level(const PosixImage & image, bool native, uint8_t * out)
{
	const uint8_t * bytes = image.bytes();
	if (bytes == NULL) {
		return false;
	}
	if (native) {
		memcpy(out, bytes, image.slicePitch());
		return true;
	}
	const unsigned int width = image.width();
	const unsigned int height = image.height();
	if (image.format() == D3DFMT_X8R8G8B8) {
		// The bytes are B, G, R and an unused X, which D3D9 samples as alpha 1.
		for (unsigned int y = 0; y < height; ++y) {
			const uint8_t * row = bytes + (size_t)y * image.rowPitch();
			uint8_t * target = out + (size_t)y * width * 4;
			memcpy(target, row, (size_t)width * 4);
			for (unsigned int x = 0; x < width; ++x) {
				target[x * 4 + 3] = 255;
			}
		}
		return true;
	}
	const PosixFormatLayout & layout = image.layout();
	PosixColor block[16];
	if (layout.blockWidth * layout.blockHeight > 16) {
		return false;
	}
	for (unsigned int block_y = 0; block_y < image.rowCount(); ++block_y) {
		const uint8_t * row = bytes + (size_t)block_y * image.rowPitch();
		for (unsigned int block_x = 0; block_x * layout.blockWidth < width; ++block_x) {
			if (!posixDecodeBlock(image.format(), row + (size_t)block_x * layout.bytesPerBlock, block)) {
				return false;
			}
			for (unsigned int j = 0; j < layout.blockHeight; ++j) {
				const unsigned int y = block_y * layout.blockHeight + j;
				for (unsigned int i = 0; i < layout.blockWidth; ++i) {
					const unsigned int x = block_x * layout.blockWidth + i;
					if (x >= width || y >= height) {
						continue;
					}
					const PosixColor & colour = block[j * layout.blockWidth + i];
					uint8_t * pixel = out + ((size_t)y * width + x) * 4;
					pixel[0] = unit_to_byte(colour.b);
					pixel[1] = unit_to_byte(colour.g);
					pixel[2] = unit_to_byte(colour.r);
					pixel[3] = unit_to_byte(colour.a);
				}
			}
		}
	}
	return true;
}

SdlResourceMirrors::SdlResourceMirrors(SdlGpuFrame * frame) :
	Frame(frame),
	WhiteTexture(NULL),
	BcSupported(-1),
	StaleFlushes(0),
	TexturesUploaded(0),
	BuffersUploaded(0)
{
	LiveMirrors = this;
	posixResourceDestroyed = &SdlResourceMirrors::Destroyed;
}

SdlResourceMirrors::~SdlResourceMirrors()
{
	posixResourceDestroyed = NULL;
	LiveMirrors = NULL;
	std::lock_guard<std::mutex> guard(Lock);
	for (std::unordered_map<const void *, Copy>::iterator it = Copies.begin(); it != Copies.end(); ++it) {
		Dead.push_back(it->second);
	}
	Copies.clear();
	for (size_t i = 0; i < Dead.size(); ++i) {
		Frame->Release_After_Batch(Dead[i].Texture, Dead[i].Buffer);
	}
	Dead.clear();
	Frame->Release_After_Batch(WhiteTexture, NULL);
}

/// PERF1's hitch hunt: how long taking Lock waited, when over 5 ms - another thread held it.
static void Log_Lock_Wait(double started, const char *what)
{
	if (!Sdl_Creation_Log_Asked()) return;
	const double waited = Sdl_Now_Ms() - started;
	if (waited > 5.0) {
		Sdl_Creation_Log("lockwait", started, waited, what);
	}
}

void SdlResourceMirrors::Destroyed(const void * resource)
{
	SdlResourceMirrors * mirrors = LiveMirrors;
	if (mirrors == NULL) {
		return;
	}
	std::lock_guard<std::mutex> guard(mirrors->Lock);
	std::unordered_map<const void *, Copy>::iterator it = mirrors->Copies.find(resource);
	if (it != mirrors->Copies.end()) {
		mirrors->Dead.push_back(it->second);
		mirrors->Copies.erase(it);
	}
}

void SdlResourceMirrors::Collect_Dead()
{
	std::lock_guard<std::mutex> guard(Lock);
	for (size_t i = 0; i < Dead.size(); ++i) {
		Frame->Release_After_Batch(Dead[i].Texture, Dead[i].Buffer);
	}
	Dead.clear();
}

bool SdlResourceMirrors::Has_Copy(const void * owner) const
{
	std::lock_guard<std::mutex> guard(Lock);
	return Copies.find(owner) != Copies.end();
}

size_t SdlResourceMirrors::Live_Copies() const
{
	std::lock_guard<std::mutex> guard(Lock);
	return Copies.size();
}

bool SdlResourceMirrors::Prepare_Update(Copy & copy)
{
	if (copy.UsedBatch == Frame->Batch()) {
		if (!Frame->Flush()) {
			return false;
		}
		++StaleFlushes;
	}
	return true;
}

SDL_GPUTexture * SdlResourceMirrors::Texture(IDirect3DBaseTexture9 * base, std::string & refusal)
{
	if (base->GetType() != D3DRTYPE_TEXTURE) {
		refusal = "a cube or volume texture (A3c draws 2D textures)";
		return NULL;
	}
	PosixTexture9 * texture = static_cast<PosixTexture9 *>(base);
	const unsigned int levels = texture->levelCount();
	const double started = Sdl_Creation_Log_Asked() ? Sdl_Now_Ms() : 0.0;
	bool created = false;
	std::lock_guard<std::mutex> guard(Lock);
	Log_Lock_Wait(started, "texture");
	std::unordered_map<const void *, Copy>::iterator found = Copies.find(texture);
	if (found == Copies.end()) {
		if (BcSupported < 0) {
			SDL_GPUDevice * device = Frame->Device();
			BcSupported = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
					SDL_GPU_TEXTUREUSAGE_SAMPLER)
				&& SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
					SDL_GPU_TEXTUREUSAGE_SAMPLER)
				&& SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM, SDL_GPU_TEXTURETYPE_2D,
					SDL_GPU_TEXTUREUSAGE_SAMPLER) ? 1 : 0;
		}
		const PosixImage & base_level = texture->level(0);
		unsigned int format = 0;
		bool native = false;
		if (levels == 0 || !Sdl_Texture_Format(base_level.format(), base_level.width(), base_level.height(),
			BcSupported == 1, format, native)) {
			refusal = "a texture format with no way up";
			return NULL;
		}
		// A render-target texture (A3d) is drawn into on the GPU, which owns its pixels: it is the frame's
		// target format whatever D3D9 format it was made in, and what the CPU image holds goes up only when
		// something writes that image.
		D3DSURFACE_DESC desc;
		const bool render_target = texture->GetLevelDesc(0, &desc) == D3D_OK && (desc.Usage & D3DUSAGE_RENDERTARGET) != 0;
		if (render_target) {
			format = SdlGpuFrame::Target_Format();
			native = base_level.format() == D3DFMT_A8R8G8B8;
		}
		SDL_GPUTextureCreateInfo info;
		SDL_zero(info);
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = (SDL_GPUTextureFormat)format;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (render_target ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
		info.width = base_level.width();
		info.height = base_level.height();
		info.layer_count_or_depth = 1;
		info.num_levels = levels;
		info.sample_count = SDL_GPU_SAMPLECOUNT_1;
		Copy copy;
		copy.Texture = SDL_CreateGPUTexture(Frame->Device(), &info);
		copy.Buffer = NULL;
		copy.Size = 0;
		copy.Native = native;
		copy.UsedBatch = 0;
		if (copy.Texture == NULL) {
			refusal = std::string("the GPU refused the texture: ") + SDL_GetError();
			return NULL;
		}
		if (render_target) {
			// Nothing to upload: a new render target's pixels are undefined in D3D9 too.
			for (unsigned int level = 0; level < levels; ++level) {
				copy.Versions.push_back(texture->level(level).version());
			}
		}
		found = Copies.insert(std::make_pair((const void *)texture, copy)).first;
		created = true;
	}
	Copy & copy = found->second;
	bool stale = copy.Versions.size() != levels;
	for (unsigned int level = 0; !stale && level < levels; ++level) {
		stale = copy.Versions[level] != texture->level(level).version();
	}
	if (stale) {
		if (!Prepare_Update(copy)) {
			refusal = std::string("the GPU refused to flush before a texture update: ") + SDL_GetError();
			return NULL;
		}
		copy.Versions.resize(levels);
		dump_texture_if_asked(texture->level(0), texture, TexturesUploaded);
		for (unsigned int level = 0; level < levels; ++level) {
			const PosixImage & image = texture->level(level);
			uint32_t offset = 0;
			uint8_t * space = Frame->Upload_Space(Sdl_Level_Upload_Size(image, copy.Native), offset);
			Sdl_Convert_Level(image, copy.Native, space);
			Frame->Queue_Texture_Upload(copy.Texture, level, image.width(), image.height(), offset);
			copy.Versions[level] = image.version();
		}
		++TexturesUploaded;
		if (Sdl_Creation_Log_Asked()) {
			char detail[96];
			snprintf(detail, sizeof(detail), "%ux%u, %u levels, format %u, %s", texture->level(0).width(),
				texture->level(0).height(), levels, (unsigned)texture->level(0).format(), copy.Native ? "native" : "expanded");
			Sdl_Creation_Log(created ? "texture" : "retexture", started, Sdl_Now_Ms() - started, detail);
		}
	}
	copy.UsedBatch = Frame->Batch();
	if (Sdl_Creation_Log_Asked() && Sdl_Now_Ms() - started > 5.0) {
		char detail[96];
		snprintf(detail, sizeof(detail), "%ux%u, %s, %s", texture->level(0).width(), texture->level(0).height(),
			created ? "created" : "found", stale ? "uploaded" : "current");
		Sdl_Creation_Log("slowtex", started, Sdl_Now_Ms() - started, detail);
	}
	return copy.Texture;
}

SDL_GPUTexture * SdlResourceMirrors::Surface(IDirect3DSurface9 * surface, bool depth, std::string & refusal)
{
	D3DSURFACE_DESC desc;
	if (surface->GetDesc(&desc) != D3D_OK) {
		refusal = "a surface with no description";
		return NULL;
	}
	std::lock_guard<std::mutex> guard(Lock);
	std::unordered_map<const void *, Copy>::iterator found = Copies.find(surface);
	if (found != Copies.end()) {
		return found->second.Texture;
	}
	// A standalone render target or depth surface: the GPU's alone, never uploaded.  A CPU write into
	// one reaches the GPU through the resource code's download-then-write rule and a StretchRect or a draw.
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)(depth ? Frame->Depth_Format() : SdlGpuFrame::Target_Format());
	info.usage = depth ? Frame->Depth_Usage()
		: (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
	info.width = desc.Width;
	info.height = desc.Height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	Copy copy;
	copy.Texture = SDL_CreateGPUTexture(Frame->Device(), &info);
	copy.Buffer = NULL;
	copy.Size = 0;
	copy.Native = true;
	copy.UsedBatch = 0;
	if (copy.Texture == NULL) {
		refusal = std::string("the GPU refused the surface: ") + SDL_GetError();
		return NULL;
	}
	Copies.insert(std::make_pair((const void *)surface, copy));
	return copy.Texture;
}

SDL_GPUBuffer * SdlResourceMirrors::Buffer(const void * owner, const PosixBufferStorage & storage, std::string & refusal)
{
	const uint32_t size = (storage.length() + 3u) & ~3u;
	if (size == 0) {
		refusal = "an empty buffer";
		return NULL;
	}
	const double started = Sdl_Creation_Log_Asked() ? Sdl_Now_Ms() : 0.0;
	std::lock_guard<std::mutex> guard(Lock);
	Log_Lock_Wait(started, "buffer");
	std::unordered_map<const void *, Copy>::iterator found = Copies.find(owner);
	bool created = false;
	if (found == Copies.end()) {
		created = true;
		SDL_GPUBufferCreateInfo info;
		SDL_zero(info);
		info.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
		info.size = size;
		Copy copy;
		copy.Texture = NULL;
		copy.Buffer = SDL_CreateGPUBuffer(Frame->Device(), &info);
		copy.Size = size;
		copy.Native = true;
		copy.UsedBatch = 0;
		if (copy.Buffer == NULL) {
			refusal = std::string("the GPU refused the buffer: ") + SDL_GetError();
			return NULL;
		}
		found = Copies.insert(std::make_pair(owner, copy)).first;
	}
	Copy & copy = found->second;
	if (copy.Versions.size() != 1 || copy.Versions[0] != storage.version()) {
		if (!Prepare_Update(copy)) {
			refusal = std::string("the GPU refused to flush before a buffer update: ") + SDL_GetError();
			return NULL;
		}
		uint32_t offset = 0;
		uint8_t * space = Frame->Upload_Space(size, offset);
		memcpy(space, storage.bytes(), storage.length());
		memset(space + storage.length(), 0, size - storage.length());
		Frame->Queue_Buffer_Upload(copy.Buffer, offset, size);
		copy.Versions.assign(1, storage.version());
		++BuffersUploaded;
		if (Sdl_Creation_Log_Asked()) {
			char detail[64];
			snprintf(detail, sizeof(detail), "%u bytes", size);
			Sdl_Creation_Log(created ? "buffer" : "rebuffer", started, Sdl_Now_Ms() - started, detail);
		}
	}
	copy.UsedBatch = Frame->Batch();
	return copy.Buffer;
}

SDL_GPUTexture * SdlResourceMirrors::White()
{
	if (WhiteTexture == NULL) {
		SDL_GPUTextureCreateInfo info;
		SDL_zero(info);
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = 1;
		info.height = 1;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		info.sample_count = SDL_GPU_SAMPLECOUNT_1;
		WhiteTexture = SDL_CreateGPUTexture(Frame->Device(), &info);
		if (WhiteTexture != NULL) {
			uint32_t offset = 0;
			memset(Frame->Upload_Space(4, offset), 0xFF, 4);
			Frame->Queue_Texture_Upload(WhiteTexture, 0, 1, 1, offset);
		}
	}
	return WhiteTexture;
}
