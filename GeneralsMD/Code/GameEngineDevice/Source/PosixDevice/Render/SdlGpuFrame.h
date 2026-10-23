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
** The device's frame on SDL3 GPU (decision 7, phase A3a): the SDL_GPUDevice, C2's window claimed for it,
** the offscreen back buffer and depth-stencil the engine draws into, and Present, which takes the back
** buffer to the window through D3D9's gamma ramp.  Only a device with a window has one: -headless makes
** no SDL_GPUDevice at all (phase A3's design).
**
** Nothing is issued as the engine asks for it.  Clears and draws are recorded into a batch, with a copy
** of every byte a draw reads that can still change (the staging stream) and the uploads of the GPU
** copies it needs; Flush runs the batch as one copy pass and then render passes that replay the records
** in order, a clear being the load operation of the pass after it (A3c).  Only a clear of the whole target is taken here; a clear of part of it is a clear draw.
**
** Main thread only, as every SDL GPU call is.
*/

#pragma once

#ifndef SDLGPUFRAME_H
#define SDLGPUFRAME_H

#include "Platform/RenderTypes.h"

#include <stdint.h>
#include <string>
#include <vector>

struct SDL_GPUBuffer;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUShader;
struct SDL_GPUTexture;
struct SDL_Window;

/// Where a pass draws: a colour texture of Target_Format() and a depth-stencil of Depth_Format(), the
/// same size.  The back buffer is one; a render-target surface's GPU texture is another (A3d).  Either
/// attachment may be absent: the sun target is depth only, the cloud caches colour only.
struct SdlTarget
{
	SDL_GPUTexture * Colour;
	SDL_GPUTexture * Depth;
	uint32_t Width;
	uint32_t Height;
	// Zero retains the scene format; independent depth-only targets name their format.
	uint32_t DepthFormat = 0;
};

/// One draw as the flush replays it, everything by value.  The device's resolve fills it.
struct SdlRecordedDraw
{
	enum { MAXIMUM_SAMPLERS = 16, MAXIMUM_UNIFORM_SLOTS = 4, WATER_PIXEL_BLOCKS = 3 };
	SDL_GPUGraphicsPipeline * Pipeline;
	SDL_GPUBuffer * VertexBuffer;		///< a GPU copy, or null for the batch's staging stream
	uint32_t VertexOffset;				///< bytes into it
	SDL_GPUBuffer * IndexBuffer;		///< as VertexBuffer, when IndexSize is not 0
	uint32_t IndexOffset;
	uint32_t IndexSize;					///< 0 (not indexed), 2 or 4
	uint32_t Count;						///< vertices, or indices
	uint32_t First;						///< the first vertex, or the first index
	int32_t BaseVertex;
	uint32_t VertexConstants;			///< offsets into the batch's constant bytes (Constants)
	uint32_t VertexConstantsSize;
	uint32_t VertexWaterConstants;		///< water-only b1; aliases this draw's immutable pixel constants
	uint32_t VertexWaterConstantsSize;
	uint32_t PixelConstants;
	uint32_t PixelConstantsSize;
	uint32_t PixelWaterConstants[WATER_PIXEL_BLOCKS]; ///< immutable modern-water-only PS b1..b3
	uint32_t PixelWaterConstantsSize[WATER_PIXEL_BLOCKS]; ///< zero for every other draw
	uint32_t SamplerCount;
	SDL_GPUTexture * Textures[MAXIMUM_SAMPLERS];
	SDL_GPUSampler * Samplers[MAXIMUM_SAMPLERS];
	uint32_t VertexSamplerCount;
	SDL_GPUTexture * VertexTextures[MAXIMUM_SAMPLERS];
	SDL_GPUSampler * VertexSamplers[MAXIMUM_SAMPLERS];
	float Viewport[6];					///< x, y, width, height, min depth, max depth: as the GPU takes it
	int32_t Scissor[4];					///< x, y, width, height
	uint32_t StencilReference;
	uint32_t BlendFactor;				///< D3DCOLOR
};

class SdlGpuFrame
{
public:
	/// A byte the batch's arenas grow by without clearing it: whatever is staged is written in full at
	/// once, so std::vector<uint8_t>'s value-initialising resize cleared every byte only for it to be
	/// overwritten (PERF1 measured the memset at about 1% of the main thread).  Alignment gaps between
	/// allocations keep what they held; they are uploaded and never read.
	struct ArenaByte
	{
		uint8_t Value;
		ArenaByte() {}
	};

	/// The device, with the window claimed when there is one.  A null window makes an offscreen frame,
	/// which is what the tests use: Present then needs a target of the caller's (Present_To).  Null, with
	/// the reason, when SDL has no GPU device to give.
	static SdlGpuFrame * Create(RenderWindow window, unsigned int width, unsigned int height, std::string & error);
	~SdlGpuFrame();

	/// A failed flush or allocation retains the old attachments and dimensions.
	/// Recording/submission failures still have Flush's batch-consumption semantics.
	enum ResizeFailure { RESIZE_NONE, RESIZE_INVALID_SIZE, RESIZE_FLUSH_FAILED, RESIZE_ALLOCATION_FAILED };
	bool Resize(unsigned int width, unsigned int height, ResizeFailure *failure = NULL);

	/// Where the clears and draws recorded from now on go (A3d); the back buffer until told otherwise.
	void Set_Target(const SdlTarget & target);
	SdlTarget Back_Buffer_Target() const;
	unsigned int Target_Depth_Format(const SdlTarget &target) const;
	/// Sampleable sun depth, retired only after previously recorded readers are submitted.
	bool Sun_Shadow_Target(unsigned int size, SdlTarget &target, std::string &error);
	bool Fill_Sun_Smoke(const float *sun, const float *casters, unsigned count);
	SDL_GPUTexture *Sun_Smoke_Texture() const;
	SDL_GPUSampler *Sun_Smoke_Sampler() const;
	bool Read_Sun_Smoke(std::vector<float> &moments);
	unsigned Sun_Smoke_Count() const { return SmokeCount; }
	/// Diagnostic scalar readback; no scene-depth snapshot or attachment mutation.
	bool Read_Depth(SDL_GPUTexture *texture, unsigned width, unsigned height, std::vector<float> &depth);
	/// A depth-stencil of this size for a render target the back buffer's does not fit, made once and
	/// kept.  D3D9 lets a smaller target borrow the bigger depth surface; SDL3 GPU wants one size per pass.
	SDL_GPUTexture * Depth_For(unsigned int width, unsigned int height);
	/// A copy between two GPU textures of Target_Format(), rectangles as x, y, width, height, recorded in
	/// order between the passes (StretchRect).
	void Record_Blit(SDL_GPUTexture * source, const int32_t source_rect[4], SDL_GPUTexture * destination,
		const int32_t destination_rect[4], bool linear);

	/// D3DCLEAR_TARGET, _ZBUFFER and _STENCIL over the whole current target, recorded for the next pass.  A
	/// later clear of the same thing replaces an earlier one, as the second would overwrite the first.
	void Clear_Back_Buffer(bool colour, bool depth, bool stencil, uint32_t argb, float z, uint32_t stencil_value);
	/// The same over a rectangle of it, in pixels: recorded in order as a clear draw, a triangle over the
	/// target scissored to the rectangle, that writes the value whatever the depth and stencil hold.
	void Clear_Rect(int x, int y, int width, int height, bool colour, bool depth, bool stencil, uint32_t argb, float z,
		uint32_t stencil_value);

	/// Runs what is recorded into the back buffer.  False when the GPU refused the work.
	bool Flush();

	// ---- The batch (A3c).  Offsets are into this batch only: a flush starts the next one empty.

	/// Room for `size` bytes in the staging stream, which the flush uploads into one GPU buffer that the
	/// records bind as vertices and as indices; 16-byte aligned.  The pointer is good until the next call.
	uint8_t * Stage(uint32_t size, uint32_t & offset);
	/// Where the next Stage() would put its bytes: the stream's end, aligned as Stage aligns it.
	uint32_t Stage_Offset_Next() const;
	/// Room for bytes going to a GPU copy, and the uploads that take them there in the copy pass.  A
	/// buffer or level-0 upload cycles its target, so draws already submitted keep what they read.
	uint8_t * Upload_Space(uint32_t size, uint32_t & offset);
	void Queue_Buffer_Upload(SDL_GPUBuffer * buffer, uint32_t upload_offset, uint32_t size);
	void Queue_Texture_Upload(SDL_GPUTexture * texture, unsigned int level, unsigned int width, unsigned int height,
		uint32_t upload_offset);
	/// A constant block's bytes, for stage 0 (vertex) or 1 (pixel); the same bytes as that stage's block
	/// before at the same slot share its offset, so the flush pushes only a change.
	uint32_t Constants(unsigned int stage, const void * bytes, uint32_t size, unsigned int slot=0);
	void Record_Draw(const SdlRecordedDraw & draw);
	/// A GPU object nothing will record again, released once the batch that may use it has gone.
	void Release_After_Batch(SDL_GPUTexture * texture, SDL_GPUBuffer * buffer);
	/// Counts flushes: what a GPU copy compares to know whether this batch has used it.
	uint64_t Batch() const { return BatchNumber; }
	/// Whether the batch has grown enough that the next draw should flush first: its staged and uploaded bytes,
	/// or its draws (BatchDrawLimit).  Counts the flushes the draw limit asks for.
	bool Batch_Is_Full();
	/// The scene depth-stencil format; colour-only passes omit that attachment.
	unsigned int Depth_Format() const { return DepthFormat; }
	/// Sampling is optional on older devices. The selected depth format supports this complete usage.
	unsigned int Depth_Usage() const;
	/// Device capability selected at frame creation, including the R32F resolve target.
	bool Supports_Scene_Depth() const { return DepthSampleable; }

	/// Flush, then the back buffer to the window through the gamma ramp (null: none, a straight blit).
	bool Present(const uint16_t (*ramp)[256]);

	/// PERF1's timing aid (PosixDevice9's ZH_GPU_TIMING): with Serialize_Submits, every submit waits for
	/// its fence, so the waits add up to the GPU's time for the work (CPU and GPU no longer overlap, so a
	/// serialized run is a measurement, not a frame rate).  Take_Timing hands over, and zeroes, what was
	/// spent since the last call: in mid-frame flushes, the fence waits, and how many flushes there were.
	void Serialize_Submits(bool serialize) { SerializeSubmits = serialize; }
	/// Two kinds of Present since the last call that the display did not pace, so their times are not the
	/// panel's: not_visible, made while SDL called the window hidden, occluded or minimised (macOS hands such a
	/// window drawables and does not wait for vsync: a -hiddenwindow run, a locked session); not_shown, whose
	/// swapchain gave no drawable at all, so nothing reached the display.
	void Take_Timing(double & flush_ms, double & fence_ms, unsigned int & flushes, double & acquire_ms,
		double & offscreen_ms, unsigned int & not_visible, unsigned int & not_shown);
	/// Every windowed Present so far made to a window that was not visible, and with no drawable.
	unsigned int Presents_Not_Visible() const { return NotVisibleTotal; }
	unsigned int Presents_Not_Shown() const { return NotShownTotal; }
	/// Mid-frame flushes (Flush) since the frame was made, the most that were ever submitted and not yet done
	/// at once, and how many times a flush waited because the ring was full (Flush_Limit).
	unsigned int Flushes_Total() const { return FlushesTotal; }
	unsigned int Flushes_In_Flight_Most() const { return FlushInFlightMost; }
	unsigned int Flush_Waits() const { return FlushWaits; }
	unsigned int Flush_Limit() const { return FlushLimit; }
	/// The flushes the draw limit asked for, and the limit.
	unsigned int Draw_Limit_Flushes() const { return DrawLimitFlushes; }
	unsigned int Batch_Draw_Limit() const { return BatchDrawLimit; }

	/// -offscreen, the game with no window: Present draws the gamma pass into a display texture of the back
	/// buffer's size, as it would into a swapchain's, and keeps at most two frames on the GPU, which a
	/// swapchain otherwise does (SDL frees what a submit left only as the GPU finishes it; a loop that
	/// never waits runs ahead without bound).  With hz, the frames are paced at hz a second, each on the
	/// next tick as vsync would put it.  Both waits add up in offscreen_ms, not acquire_ms.  The tests'
	/// offscreen frames never ask for this: their Present stays a flush and a copy to the front.
	void Set_Offscreen_Presents(unsigned int hz);
	bool Offscreen_Presents() const { return OffscreenPresents; }

	/// Present into a texture of the caller's, of Target_Format(), instead of the window: the test's
	/// window.  The back buffer's size must be the target's.
	bool Present_To(SDL_GPUTexture * target, unsigned int width, unsigned int height, const uint16_t (*ramp)[256]);

	/// The texture's pixels, B8G8R8A8 rows top first, width * 4 bytes each.  Flushes first.
	bool Read_Back(SDL_GPUTexture * texture, unsigned int width, unsigned int height, std::vector<uint8_t> & bgra);

	/// Writes B8G8R8A8 pixels into the back buffer (the gamma test's input picture).
	bool Upload_Back_Buffer(const std::vector<uint8_t> & bgra);

	SDL_GPUDevice * Device() const { return GpuDevice; }
	SDL_GPUTexture * Back_Buffer() const { return BackBuffer; }
	/// What the last Present showed: the back buffer is copied here at each Present, so the front buffer
	/// (GetFrontBufferData) is the presented picture whenever it is asked for (A3d).
	SDL_GPUTexture * Front_Copy() const { return FrontCopy; }
	/// Opaque scene before the first water draw, cached per actual colour/depth target and frame.
	SDL_GPUTexture * Water_Background(const SdlTarget &source);
	/// Device depth (0..1), resolved into an independent R32_FLOAT texture between render passes, in the
	/// same per-target, per-frame capture as Water_Background; water and hazard clouds share it.
	/// Null if sampled depth is unsupported. Borrowed for immediate draw recording, never released by
	/// the caller, and never aliases the depth attachment being drawn into.
	SDL_GPUTexture * Scene_Depth(const SdlTarget &source);
	/// Read-only lookup; never captures after water/overlays have already changed the scene.
	SDL_GPUTexture * Ready_Scene_Depth(const SdlTarget &source, uint64_t &serial) const;
	/// Lazy, GPU-only Phillips spectrum/IFFT. Previous draws finish recording before maps change;
	/// repeated paused times reuse the same field. Failure leaves the analytic water fallback usable.
	bool Prepare_Ocean(float time);
	SDL_GPUTexture * Ocean_Map(unsigned int index) const;
	SDL_GPUSampler * Ocean_Sampler() const;
	uint64_t Ocean_Updates() const { return OceanUpdates; }

	unsigned int Width() const { return BackWidth; }
	unsigned int Height() const { return BackHeight; }
	/// The back buffer's format, and so what a Present_To target has to be.
	static unsigned int Target_Format();

private:
	/// Submits, and with SerializeSubmits waits for the fence, adding the wait to FenceMs.
	bool Submit(struct SDL_GPUCommandBuffer * commands);
	bool SerializeSubmits;
	double FlushMs;
	double FenceMs;
	unsigned int Flushes;
	double AcquireMs;		///< waiting in SDL_WaitAndAcquireGPUSwapchainTexture: the display's pacing, not work
	unsigned int NotVisible;		///< Presents to a hidden, occluded or minimised window since the last Take_Timing
	unsigned int NotVisibleTotal;	///< and since the frame was made
	unsigned int NotShown;			///< Presents with no drawable since the last Take_Timing
	unsigned int NotShownTotal;		///< and since the frame was made
	unsigned int ShownWidth;		///< the swapchain's size at the last Present that had one, which stderr gives
	unsigned int ShownHeight;		///< whenever it changes: the back buffer is scaled to it
	/// The mid-frame flushes still in flight, oldest first.  Each holds its command buffer, and on Direct3D 12
	/// the two descriptor heaps SDL gives every command buffer, until the GPU is done with it.  Unbounded, a
	/// slow GPU can let them pile up until the driver makes no more heaps; FlushLimit bounds them.
	std::vector<struct SDL_GPUFence *> FlushFences;
	unsigned int FlushLimit;			///< the most allowed in flight (ZH_GPU_FLUSH_LIMIT; 0: no limit)
	unsigned int FlushesTotal;
	unsigned int FlushInFlightMost;
	unsigned int FlushWaits;
	/// The most draws one batch records before the next draw flushes it.  A Direct3D 9 driver submits a full
	/// command buffer on its own, presented or not; a batch that only ended at Present grew without bound when
	/// the game drew and did not present (a campaign quickstart's movie: minutes, in one command buffer).
	/// ZH_GPU_BATCH_DRAWS=<n> (0: no limit).
	unsigned int BatchDrawLimit;
	unsigned int DrawLimitFlushes;
	double OffscreenMs;		///< -offscreen's waits: frames in flight, and the pacer
	bool OffscreenPresents;
	unsigned int OffscreenHz;
	SDL_GPUTexture * DisplayTexture;	///< -offscreen's stand-in for the swapchain's texture
	struct SDL_GPUFence * InFlight[2];
	unsigned int InFlightNext;
	uint64_t NextTickNs;
	bool Submit_Offscreen(struct SDL_GPUCommandBuffer * commands);
	bool Submit_Flush(struct SDL_GPUCommandBuffer * commands);

	SdlGpuFrame();
	bool Create_Targets(unsigned int width, unsigned int height);
	void Release_Targets();
	bool Record_Batch(struct SDL_GPUCommandBuffer * commands);
	bool Record_Passes(struct SDL_GPUCommandBuffer * commands, SDL_GPUBuffer * stream);
	bool Begin_Pass(struct SDL_GPUCommandBuffer * commands, const SdlTarget & target, bool clear_colour, bool clear_depth,
		bool clear_stencil, uint32_t argb, float z, uint32_t stencil_value, struct SDL_GPURenderPass ** pass);
	SDL_GPUGraphicsPipeline * Clear_Pipeline(unsigned int which);
	void End_Batch();
	bool Present_Into(struct SDL_GPUCommandBuffer * commands, SDL_GPUTexture * target, unsigned int width,
		unsigned int height, unsigned int format, const uint16_t (*ramp)[256]);
	SDL_GPUGraphicsPipeline * Gamma_Pipeline(unsigned int format);
	bool Upload_Ramp(struct SDL_GPUCommandBuffer * commands, const uint16_t (*ramp)[256]);

	SDL_GPUDevice * GpuDevice;
	SDL_Window * Window;				///< C2's, claimed; null for an offscreen frame
	SDL_GPUTexture * BackBuffer;
	SDL_GPUTexture * DepthStencil;
	SDL_GPUTexture * FrontCopy;			///< the last presented picture
	SDL_GPUTexture *SunShadowTexture = NULL;
	unsigned SunShadowSize = 0;
	struct SmokeResources;
	SmokeResources *Smoke = NULL;
	bool SmokeFailed = false;
	unsigned SmokeCount = 0;
	bool Create_Sun_Smoke();
	void Release_Sun_Smoke();
	bool Fail_Sun_Smoke(const char *reason);
	void Release_Sun_Shadow();
	struct WaterSnapshot
	{
		SdlTarget Source;
		SDL_GPUTexture *Colour, *Depth;
		bool Ready;
		uint64_t UsedFrame;
		uint64_t CaptureSerial;
	};
	std::vector<WaterSnapshot> WaterSnapshots; // at most eight; replaced resources retire with their batch
	uint64_t WaterFrame = 0;
	uint64_t WaterCaptureSerial = 0;
	bool DepthSampleable = false;
	SDL_GPUShader *SceneDepthVertex = NULL, *SceneDepthPixel = NULL;
	SDL_GPUGraphicsPipeline *SceneDepthPipeline = NULL;
	SDL_GPUSampler *SceneDepthSampler = NULL;
	struct OceanResources;
	OceanResources *Ocean = NULL;
	bool OceanFailed = false, OceanReady = false;
	float OceanTime = 0;
	uint64_t OceanUpdates = 0;
	bool Create_Ocean();
	void Release_Ocean();
	bool Fail_Ocean(const char *reason);

	WaterSnapshot *Water_Snapshot(const SdlTarget &source);
	void Invalidate_Water_Snapshots(SDL_GPUTexture *colour, SDL_GPUTexture *depth);
	void Refresh_Water_Snapshots();
	// The one depth-to-R32F resolve: the scene snapshots (water, hazard clouds) and the sun's
	// diagnostic Read_Depth share it.
	bool Create_Scene_Depth_Pipeline();
	bool Resolve_Scene_Depth(struct SDL_GPUCommandBuffer *commands, SDL_GPUTexture *source,
		SDL_GPUTexture *destination, unsigned int width, unsigned int height);
	bool Copy_To_Front(struct SDL_GPUCommandBuffer * commands);
	unsigned int DepthFormat;			///< an SDL_GPUTextureFormat: D24S8 where there is one, else D32S8
	unsigned int BackWidth;
	unsigned int BackHeight;

	// The batch.  A command is a clear or a draw, in the order the engine asked for them.
	struct Command
	{
		bool IsDraw;
		uint32_t Draw;					///< into Draws
		bool IsRect;					///< a clear draw over Rect, not a load operation
		int32_t Rect[4];				///< x, y, width, height
		uint32_t Target;				///< into Targets: where a clear or draw goes
		bool IsBlit;					///< a copy between textures, Rect to BlitRect
		bool IsDepthSnapshot;		///< sampled depth to R32_FLOAT, with neither scene attachment bound
		SDL_GPUTexture * BlitSource;
		SDL_GPUTexture * BlitDestination;
		int32_t BlitRect[4];
		bool BlitLinear;
		bool Colour, Depth, Stencil;	///< a clear's
		uint32_t Argb;
		float Z;
		uint32_t StencilValue;
	};
	struct Upload
	{
		SDL_GPUBuffer * Buffer;			///< or
		SDL_GPUTexture * Texture;
		unsigned int Level, Width, Height;
		uint32_t Offset, Size;
	};
	std::vector<Command> Commands;
	std::vector<SdlTarget> Targets;		///< this batch's, in first use order
	SdlTarget CurrentTarget;
	bool TargetSet;						///< CurrentTarget was given; the back buffer otherwise
	struct ScratchDepth { SDL_GPUTexture * Texture; uint32_t Width, Height; };
	std::vector<ScratchDepth> ScratchDepths;
	uint32_t Target_Index();
	std::vector<SdlRecordedDraw> Draws;
	std::vector<ArenaByte> StreamBytes;
	std::vector<ArenaByte> UploadBytes;
	std::vector<ArenaByte> ConstantBytes;
	uint32_t LastConstants[2][SdlRecordedDraw::MAXIMUM_UNIFORM_SLOTS], LastConstantsSize[2][SdlRecordedDraw::MAXIMUM_UNIFORM_SLOTS];
	std::vector<Upload> Uploads;
	std::vector<SDL_GPUTexture *> DeadTextures;
	std::vector<SDL_GPUBuffer *> DeadBuffers;
	uint64_t BatchNumber;
	SDL_GPUShader * ClearVertex;			///< the clear draw's programs, and its pipelines by what it clears
	SDL_GPUShader * ClearPixel;
	SDL_GPUGraphicsPipeline * ClearPipelines[8];
	SDL_GPUBuffer * StreamBuffer;		///< the staging stream on the GPU, grown as needed
	uint32_t StreamBufferSize;
	struct SDL_GPUTransferBuffer * Transfer;
	uint32_t TransferSize;

	// The gamma pass, made the first time a ramp is not the identity.
	SDL_GPUShader * GammaVertex;
	SDL_GPUShader * GammaPixel;
	SDL_GPUGraphicsPipeline * GammaPipeline;
	unsigned int GammaPipelineFormat;
	SDL_GPUTexture * RampTexture;		///< 256 x 1, the three ramps in R, G and B
	SDL_GPUSampler * PointSampler;
	std::vector<uint16_t> UploadedRamp;	///< what RampTexture holds, to upload only a change
};

/// Whether the three ramps are D3D9's identity, value i as i * 257, which Present takes as a blit.
bool Sdl_Gamma_Is_Identity(const uint16_t (*ramp)[256]);

#endif // SDLGPUFRAME_H
