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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

/*
** The Direct3D 11 renderer, driven the way the engine drives a Direct3D 9 device.
**
** The engine sets one thing at a time and then draws: a render state, a texture stage state, a
** texture, a transform, a stream, and eventually a DrawIndexedPrimitive.  Several hundred places
** do it and several thousand calls a frame come out of them, and rewriting those into something
** D3D11 shaped is not a phase, it is a different program.  (This said "236 places and 5600 calls"
** when it was written on 2026-09-09; the first number was a count of every mention of the device
** accessor in the sources, taken before ef8303a9 cut it by 45%, and the second was never a static
** count of anything.  A call-site survey (D1, 2026-09-22) replaced it with a measurement.)
** So this takes the calls as they are and resolves them at the
** moment of the draw, which is the only moment where everything needed to build a D3D11 pipeline is
** known at once.
**
** Resolving means five things.  The render states become the three state objects, the sampler
** states become sampler objects, the texture stage states become a pixel shader that ffshader
** generates, the lighting and transform states become a vertex shader that ffvertex generates, and
** the flexible vertex format becomes an input layout.  All five are cached on the description they
** came from, because the engine sets the same handful of combinations over and over: -ffprobe
** counted 42 distinct stage programs across four maps, and the state objects are fewer still.
**
** What this is not is a Direct3D 9 device.  It answers no COM interface, it is not handed to
** anything that expects one, and there is no D3D9 runtime underneath it: the calls have D3D9's
** names because the engine's calls have D3D9's names, and that is where the resemblance stops.
*/

#ifndef DX11BACKEND_H
#define DX11BACKEND_H

#include "dx11device.h"
#include "dx11state.h"
#include "engineshader.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <d3d11_1.h>
#include <d3d9.h>
#include <map>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

// Two everywhere except the water, which binds four: the river texture, the sparkles, the noise
// and the shroud.  This is what ffshader and ffvertex generate for, and a fifth is a draw this
// refuses rather than draws wrongly.
const unsigned DX11_BACKEND_TEXTURE_STAGES = 4;

// D3DTSS_CONSTANT is the highest texture stage state at 32, so the block is indexed by the state
// itself and has to reach one past it.
const unsigned DX11_BACKEND_STAGE_STATES = 33;

// D3DCOMPILE_SKIP_OPTIMIZATION, which the Modern water's pixel programs compile with.  The
// optimiser spends 34 s on each of them (about 11 thousand instructions, mostly inlined copies of
// the noise and hash helpers) and 4.5 s without it.  The driver optimises the bytecode again
// either way: the frame time stayed inside run-to-run noise and the picture did not change
// (2026-10-23, docs/porting/windows-impact.md).
const unsigned DX11_MODERN_WATER_COMPILE_FLAGS = 1u << 2;

class DX11BackendClass
{
public:
	DX11BackendClass();
	~DX11BackendClass();

	// device is borrowed, not owned: whoever created it outlives this.
	bool Initialise(DX11DeviceClass * device);
	void Shutdown();

	// For whoever binds on the context behind the draws: the next draw binds everything again.
	void Forget_Bindings();

	// The setters, named for the D3D9 calls the engine makes.  None of them touches the device;
	// they write into the shadow state that Draw resolves.
	void Set_Render_State(D3DRENDERSTATETYPE state, DWORD value);
	void Set_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, DWORD value);
	void Set_Sampler_State(unsigned sampler, D3DSAMPLERSTATETYPE state, DWORD value);
	void Set_Texture(unsigned stage, ID3D11ShaderResourceView * texture);

	// The vehicle headlights for the frame, eight floats a light: world position, reach, world
	// direction, cosine of the cone's edge.  Past HEADLIGHT_SLOTS the rest are dropped; the caller
	// sorts them nearest first.  Zero lights leaves every program's pixels as they were.  The gain
	// is what a pixel gains per channel per unit of light (HEADLIGHT_SAMPLING).
	void Set_Headlights(const float * lights, unsigned count, const float gain[3]);

	// The scene's dynamic point lights for the frame, BLAST_LIGHT_FLOATS a light: world position and
	// far reach, diffuse colour and near reach, ambient colour and one unused.  The colours are
	// already divided by the map's terrain light (BLAST_LIGHT_SAMPLING).  Past BLAST_LIGHT_SLOTS the
	// rest are dropped; the caller sorts them nearest first.
	void Set_Blast_Lights(const float * lights, unsigned count);

	// Whether the shadow-receiving programs, which carry the per-pixel lights, are drawing this
	// frame.  While they are, the engine leaves the dynamic lights out of its own vertex lighting.
	bool Lights_Per_Pixel() const { return ShadowReceiving && ShadowMapTexture != NULL; }

	// How many blast lights the pixel block the last draw went up with carries.  A pass multiplying
	// what is under it carries none (Upload_Pixel_Constants).
	unsigned Pixel_Block_Blast_Lights() const
	{
		return static_cast<unsigned>(HeldPixelConstants.BlastLightParameters[0]);
	}

	// The draws that follow are sorted particle billboards, which fade where they meet the scene's
	// depth (SOFT_PARTICLE_SAMPLING).  Only the sorting pool sets it, around the billboards' runs.
	void Set_Soft_Particles(bool soft);

	// Copy the scene's depth for the soft particles if anything wrote it since the last copy.  The
	// sorting pool calls it at the top of each flush, before any particle run resolves its program.
	void Take_Scene_Depth();
	// The copy Take_Scene_Depth keeps, for a pass drawn behind the backend's back that fades where
	// it meets the world (the GPU particles).  Null before the first copy and under Classic.
	ID3D11ShaderResourceView * Scene_Depth_Copy() const { return SceneDepthCopyView; }
	// How many depth copies Take_Scene_Depth made since the last call.  Taking it resets it.
	unsigned Take_Frame_Depth_Copies();
	// Off under the Classic graphics setting, whose picture is -d3d9's: no draw fades, no copy.
	void Allow_Soft_Particles(bool allowed)
	{
		if (SoftParticlesAllowed != allowed) {
			SoftParticlesAllowed = allowed;
			PipelineChanged = true;
		}
	}
	bool Soft_Particles_Allowed() const { return SoftParticlesAllowed; }

	// The engine bound a texture at this stage that has no D3D11 copy - a render target it drew
	// into, most often.  Sampling white there paints a full screen quad over the frame, so a draw
	// that reads one is refused instead.
	void Set_Texture_Missing(unsigned stage, bool missing);
	void Set_Vertex_Format(DWORD fvf);

	// One of the engine's own D3D9 shaders is bound.  Most of the shipped .vso and .pso files have
	// no D3D11 counterpart, so a draw made while one is bound is refused and counted apart; the
	// ones engineshader transcribes are named instead and the draw takes that program.  The file
	// name comes with it because a refusal count says how much is missing and only the name says
	// which shader has to be written next.
	void Set_Pixel_Program(EngineShaderProgram program, bool bound, const char * file_name);
	void Set_Vertex_Program(EngineShaderProgram program, bool bound, const char * file_name);

	// The engine's own float4 shader constant bank, as it sets it.  The transcribed programs index
	// it by the same register number the SetVertexShaderConstantF call used.
	void Set_Vertex_Program_Constant(unsigned first_register, const float * values, unsigned count);

	// The Modern water's float4 registers as W3DWater sets them (Platform/WaterConstants.h), up to
	// DX11_WATER_REGISTERS.  Only a Modern water draw reads them, through dx11water.cpp's blocks.
	void Set_Water_Constants(unsigned first_register, const float * values, unsigned count);
	// What the Modern water drew with, for the shutdown report: its draws, the GPU ocean's updates,
	// or why the analytic surface stood in for it.
	std::string Water_Report() const;
	void Set_Stream_Source(ID3D11Buffer * buffer, unsigned stride, unsigned offset);
	void Set_Indices(ID3D11Buffer * buffer, DXGI_FORMAT format);

	// The three transforms the generated vertex shader reads.  D3D9 keeps world, view and
	// projection apart and multiplies them itself; the shader wants the products, so they are
	// multiplied here when one of them changes rather than once per draw.
	void Set_Transform(D3DTRANSFORMSTATETYPE state, const float matrix[16]);

	// Everything the fixed-function vertex pipeline needs that is not a render state: the material
	// colours and the lights.  A light set here is on; Disable_Light takes it off again.
	void Set_Material(const float ambient[4], const float diffuse[4], const float specular[4],
		const float emissive[4], float power);
	void Set_Light(unsigned index, DWORD type, const float position[4], const float direction[4],
		const float diffuse[4], const float specular[4], const float attenuation[4],
		const float spot[4], const float ambient[4]);
	void Disable_Light(unsigned index);

	// Write every program this builds to a file in this directory, named by the state it was built
	// from.  A generated program that draws the wrong thing is unreadable from the outside: the
	// description is a key, and the key is not the code.
	void Set_Dump_Directory(const char * directory);

	// Keep every compiled program in this file across runs: read now, written while the game runs
	// (Save_Shader_Cache_If_Due) and at Shutdown when a program was compiled that the file did not
	// hold.  A program compiled on the frame that first needs it costs that frame 100 to 130ms of
	// FXC, the stutter a new explosion brought; Set_Compile_Off_Frame moves most of them to a worker.
	void Set_Shader_Cache_Path(const char * path);
	// The programs that ship with the game, compiled by Microsoft's d3dcompiler_47 when the file was
	// recorded (Tools/record-dx11-shaders.ps1): read after the user's cache, so for the same source a
	// shipped program wins.  A first start then compiles only what the recording never met.  Under
	// Wine, whose d3dcompiler is vkd3d-shader, a single compile took minutes (measured on a Steam Deck, X2).
	void Load_Shipped_Programs(const char * path);
	unsigned Shipped_Program_Count() const { return ShippedPrograms; }
	// Writes the cache if a program was compiled since the last write and the last write is at least
	// SHADER_CACHE_SAVE_INTERVAL_MS old: called once a frame, so a run that dies before Shutdown, or
	// is killed, keeps what it compiled.
	void Save_Shader_Cache_If_Due();
	unsigned Compiled_Program_Count() const { return static_cast<unsigned>(CompiledPrograms.size()); }
	// A pixel program from outside the backend (the post chain's haze) through the same cache, so
	// only the first run that meets it compiles it.  False when the compiler refused it.
	bool Compile_Pixel_Program(const std::string & source, const char * name,
		std::vector<unsigned char> & bytecode);

	// Binds the swap chain's back buffer and depth buffer and sets the viewport over the whole of
	// it.  D3D11 keeps no default target: without this every draw is complete, legal, and lands
	// nowhere.  Called at the start of each scene rather than once, because a resize replaces both
	// views and nothing tells the backend when that happens.
	void Begin_Scene();

	void Set_Viewport(unsigned x, unsigned y, unsigned width, unsigned height);
	void Clear(bool colour, bool depth, const float colour_value[4]);

	// Draw into a texture instead of into the back buffer.  A null target goes back to the back
	// buffer.  D3D11 wants the depth buffer to match the target's size, so one is made for each
	// size a target comes in and kept; the engine uses two or three of them in a match.
	void Set_Render_Target(ID3D11RenderTargetView * target);

	// The sun's own depth buffer, which the caster pass draws into and everything that receives a
	// shadow samples.  Begin binds it as the only output, takes the viewport with it and clears it;
	// End puts the back buffer and the viewport back.  The texture is made once, at the first size
	// asked for.  SHADOW-MAP-PLAN.md phase 1.
	bool Begin_Shadow_Map(unsigned size);
	void End_Shadow_Map();

	// The filter's own numbers, set once a frame by the pass that fills the map.  The matrix that
	// takes a pixel into the sun's clip space is built here rather than handed in: the sun's view
	// and projection are the ones this held while the map was bound, and the frame's are the ones
	// it holds when the draw arrives, so both halves are already in one convention.
	void Set_Shadow_Parameters(float bias, float strength, float widest_radius_in_texels,
		float narrowest_radius_in_texels, float texels_per_unit_of_gap, float units_per_unit_of_depth,
		float sky_fill);
	void Clear_Shadow_Parameters();
	bool Shadow_Map_Bound() const { return ShadowMapBound; }
	ID3D11ShaderResourceView * Shadow_Map() const { return ShadowMapTexture; }

	// The smoke's own map over the same sun, filled once a frame after End_Shadow_Map from the sun
	// it held.  A caster is five floats: its centre in world units, its radius and the optical
	// depth through its middle.  Strength is how dark the thickest smoke leaves what is behind it;
	// zero casters or zero strength is a frame with no smoke in the light, and so is a frame that
	// never calls this.  True means the map holds every caster handed over that stands in the sun's
	// box; false that it holds none of them (no blendable, filterable four channel 32-bit float
	// target, or a buffer the device refused), which the caller reads as the smoke not being in the
	// sun's light and keeps its older shade.  ffshader.h, VOLUMETRIC_SAMPLING, says what the map holds.
	bool Fill_Smoke_Map(const float * casters, unsigned count, float strength);
	ID3D11ShaderResourceView * Smoke_Map() const { return SmokeMapFilled ? SmokeMapTexture : NULL; }

	// The opaque casters of the sun's depth pass, drawn depth only and instanced: one run is one
	// piece of one mesh model (a buffer pair, an index range, a cull and a depth test), drawn once
	// for each of its instances.  Upload_Shadow_Casters takes the pass's instances, each a world as
	// the three rows of a Matrix3D (NULL for an identity world) and the first bone of a skinned one,
	// and the pass's bones, three rows of a Matrix3D each, and writes them straight into the mapped
	// buffers; it is false, with nothing written, when the device refused a program or a buffer, and
	// the caller draws them the old way.  Each instance's world is the matrix the texture category's
	// walk sets (the Matrix3D transposed into Direct3D 9's layout) and its position goes through the
	// product Upload_Vertex_Constants builds, in the same order, so the map holds the depth the
	// generated programs wrote.  An alpha tested run samples the texture and
	// sampler on stage 0 as they stand, so Draw_Shadow_Casters is handed the runs of one texture at
	// a time, and is false, with nothing drawn, when alpha tested runs find no texture there.  A
	// skinned run's vertices are the shadow skin source.  Only while the map is bound.  Every run
	// counts in Draws_Made.
	enum ShadowCasterKind
	{
		SHADOW_CASTER_RIGID,
		SHADOW_CASTER_ALPHA_TESTED,
		SHADOW_CASTER_SKINNED
	};
	struct ShadowCasterRun
	{
		ShadowCasterKind Kind;
		ID3D11Buffer * Vertices;
		unsigned Stride;
		unsigned TextureOffset;		///< bytes into a vertex to stage 0's coordinates, alpha tested only
		ID3D11Buffer * Indices;
		unsigned StartIndex;
		unsigned IndexCount;
		unsigned BaseVertex;
		bool Strip;
		DWORD CullMode;			///< D3DCULL
		DWORD DepthFunction;	///< D3DCMPFUNC
		unsigned FirstInstance;
		unsigned InstanceCount;
	};
	struct ShadowSkinVertex
	{
		float Position[3];		///< the bind pose, in its bone's space
		unsigned Bone;
	};
	bool Upload_Shadow_Casters(const float * const * worlds, const unsigned * first_bones,
		unsigned instance_count, const float * const * bones, unsigned bone_count);
	bool Draw_Shadow_Casters(const ShadowCasterRun * runs, unsigned run_count);

	// The bind poses of the skins the depth pass skins on the GPU, appended a model at a time and
	// kept across passes.  A model whose vertices do not fit starts a new, larger buffer, and so does
	// a release of the device; either moves the generation on, and a model uploaded under an older
	// generation uploads again.  False when the device refused the buffer.
	bool Upload_Shadow_Skin(const ShadowSkinVertex * vertices, unsigned count, unsigned & first_vertex);
	unsigned Shadow_Skin_Generation() const { return ShadowSkinGeneration; }

	// The scene camera's view, which a camera space draw's pixels go back to the world through: the
	// sorted particles are written in camera space and drawn with an identity world and view
	// (PointGroupClass::Insert_Sorted_Billboards), so the view the backend holds for them says
	// nothing about where they are.  Set by the shadow pass every frame it fills the map, after the
	// frame's camera is back; until the first one, such a draw uses the view it was drawn with.
	void Set_Scene_View(const float view[16]);

	// The draws that follow carry the glow of the fires near them in their normals and want it added
	// after the shade (ffvertex.h and ffshader.h, SmokeGlow).  Only the sorting pool sets it, around
	// the smoke billboards' runs.  An unlit draw with a normal takes it; any other ignores it.
	void Set_Smoke_Glow(bool glow);

	// The draws that follow are billboards of a fire or explosion system, carrying each particle's
	// age and seed in the second coordinate set (ffshader.h, FIRE_LOOK_SAMPLING).  Only the sorting
	// pool sets it, around those systems' runs.  A draw it does not suit takes none (Fire_Look).
	void Set_Fire_Look(bool fire);

	// The draws that follow are an Emissive particle system's billboards, a fireball's alpha blended
	// body: they give light, so they take neither the sun's shadow nor the smoke's shade.  Only the
	// sorting pool sets it, around those systems' runs.
	void Set_Emissive(bool emissive);

	// The draws that follow are a house colour mesh under the Team Glow option: its hue is added again
	// after the shade, past white, for the bloom (ffshader.h, TeamGlow).  The mesh renderer sets it
	// around such a mesh's draws.  A draw it does not suit takes none (Team_Glow).
	void Set_Team_Glow(bool glow);

	// The Hemispheric Ambient option: lit draws take the scene ambient from the sky above and the ground
	// below (ffvertex.h, HemiAmbient).  Off under Classic, whose picture is -d3d9's.
	void Set_Hemi_Ambient(bool hemi);

	// The look the draws that follow are dressed in (a DRAW_EFFECT_* value, ffshader.h) and its
	// four numbers: wall clock seconds, strength, a per-object seed and the object's opacity.  The
	// mesh renderer sets it around one object's draws and puts DRAW_EFFECT_NONE back after them.
	// A draw it does not suit takes none (Draw_Effect).
	void Set_Draw_Effect(unsigned effect, const float parameters[4]);

	// The look Set_Draw_Effect last asked for and its numbers (zeros with none), for the sorting
	// pool: a sorted mesh is drawn after its object's draws have put the look back to none.
	unsigned Requested_Draw_Effect(float parameters[4]) const;

	// Whether a look asked for now would be drawn at all, for the scene to ask before it leaves an
	// object's heat vision pass or disabled tint out for it: only into the scene's own target, not
	// a screen filter's texture, a mirror or the sun's map, and the cloak only where the device
	// makes the copy of the scene it bends.  A single draw can still refuse one (Draw_Effect).
	bool Takes_Draw_Effect(unsigned effect);

	// Whether the draws are going into the scene's own target.
	bool Draws_Into_Scene() const;

	// Where the scene is going now: its own target, or a screen filter's texture drawn over the
	// scene's depth; NULL for a mirror or the sun's map.
	ID3D11RenderTargetView * Scene_Target() const;
	// The depth that goes with Scene_Target: the scene's own, multisampled with the scene, or the
	// single-sample one a screen filter's texture is drawn over.
	ID3D11DepthStencilView * Scene_Depth_Target() const;

	// With a multisampled scene, its depth resolved into the device's single-sample depth buffer,
	// which is what Get_Depth_Texture reads and what every pass reading the scene's depth samples:
	// a full screen pass taking sample 0 of each pixel, since a depth buffer cannot be resolved the
	// way a colour target is.  Only while the draws go into the scene, and only when a clear or a
	// draw wrote its depth since the last one; anything that reads the depth calls it first.
	void Resolve_Scene_Depth();
	// Makes that pass now; false when the device refuses it, and a multisampled scene without it
	// would leave every reader of the depth an unwritten buffer (Direct3D11_Create drops to one sample).
	bool Make_Depth_Resolve();

	// The scene's target, its depth and the draws' viewport bound again after a pass that bound
	// its own on the context mid-scene (the post chain's occlusion).
	void Resume_Scene();

	// The scene camera's clip space back into the world, out of Set_Scene_View's view and the
	// projection held with it, for a pass over the world that reads its depth (the prototype map
	// haze).  False when Set_Scene_View has not run since Open_Frame, when that projection is not a
	// perspective one or the matrix has no inverse.
	bool Scene_World_From_Clip(float world_from_clip[16]) const;
	// How that projection scales x and y: a world unit at eye distance d is x / d and y / d of half the frame.
	void Scene_Projection_Scale(float & x, float & y) const { x = SceneProjection[0]; y = SceneProjection[5]; }
	// The top of a frame, before any scene of it: Scene_World_From_Clip waits for this frame's view.
	void Open_Frame();

	// What an additive draw (ONE, ONE) into the scene is worth, for the Glow option: the blend
	// takes the draw's colour times this through the blend factor, so fire, muzzle flashes and
	// laser cores pass white in the half float scene and the bloom's bright pass finds them, where
	// LDR art on its own never gets there.  One is off.  Only the scene target takes it: the
	// runtime sets it while the scene goes into the post chain's float texture and puts it back to
	// one when the frame goes to the screen, and a draw into a render target of its own (the
	// water's reflection), a screen space draw and any other blend keep their colour.
	void Set_Additive_Gain(float gain);
	float Additive_Gain() const { return AdditiveGain; }

	// Whether the draws that follow opted into that gain (ShaderClass::GLOW_ENABLE, sent by
	// ShaderClass::Apply).  Additive water, waypoint lines, decals and heat vision are ONE, ONE as
	// well and stay as they were painted.
	void Set_Glow_Draw(bool glow) { GlowDraw = glow; }

	// The texture the engine draws the scene into for its screen filters (W3DShaderManager's render
	// texture).  Here the scene stays in the scene target, where the gain and the bloom work: a
	// redirect into this texture's 8-bit twin would clip every fire at white.  A draw that samples
	// the view samples a copy of the scene instead (Readable_Texture).  NULL when there is none.
	void Set_Scene_Stand_In(ID3D11ShaderResourceView * view) { SceneStandIn = view; }

	// What is in the map, read back through a staging copy: how much of it was drawn into and how
	// near the nearest thing is.  A caster pass that drew nothing leaves a map that is all one
	// value, and no draw count tells that apart from a pass that drew the world.
	std::string Shadow_Map_Report();

	// The two draws.  Both resolve the shadow state into a pipeline first, and both return false
	// when some part of that state has no D3D11 answer, which leaves the draw undone rather than
	// drawn wrongly.
	bool Draw_Indexed_Triangles(unsigned index_count, unsigned start_index, unsigned base_vertex);
	bool Draw_Triangles(unsigned vertex_count, unsigned start_vertex);

	// The same indexed draw over a triangle strip, which is how the water lays out both of its
	// grids.  A strip drawn as a list reads three indices where the strip meant one triangle and
	// paints a third of the surface in torn triangles, so the topology cannot be assumed.
	bool Draw_Indexed_Strip(unsigned index_count, unsigned start_index, unsigned base_vertex);

	// A triangle strip handed over as vertices rather than as a buffer, which is how the engine
	// draws every screen-space quad it has.
	bool Draw_User_Strip(const void * vertices, unsigned primitive_count, unsigned stride);

	// How many pipelines were built and how many draws were refused, which is the same pair
	// -ffshader reports and the same thing it is for: a backend that silently refuses half the
	// draws looks like a renderer with a lot missing and no error anywhere.
	void Statistics(unsigned & pipelines_built, unsigned long long & draws_made,
		unsigned long long & draws_refused) const;

	// The pipelines built since the last call and the milliseconds they took, generation, compile
	// and device objects together.  A pipeline is built the first time its state reaches a draw, so
	// this is the stutter a new effect costs on the frame it first appears.
	void Take_Frame_Build_Cost(double & milliseconds, unsigned & pipelines);

	// The constant blocks written since the last call, and how many of those writes discarded their
	// buffer, which is a rename in the driver.  With offsets a rename is a ring wrapping; without
	// them every write is one.
	void Take_Frame_Constant_Writes(unsigned & writes, unsigned & renames);
	// Which way the constant blocks go up, and why, for the log line Initialise owes: the rings, or
	// one entry a kind because the device has no offset binding or refused a ring that size.
	const char * Constant_Path() const { return ConstantPath; }
	bool Constant_Offsets() const { return OffsetContext != NULL; }

	// Why the refusals happened, in the order Draw checks them: no buffer bound, no texture stage
	// enabled, a vertex format with no input layout, and a program the generator or the compiler
	// would not produce.  "Half the draws are refused" is not a finding; which half is.
	void Refusals(unsigned long long & no_buffer, unsigned long long & no_stage,
		unsigned long long & no_layout, unsigned long long & no_program,
		unsigned long long & no_object, unsigned long long & foreign_shader,
		unsigned long long & no_texture) const;

	// The distinct states that were refused, as the keys they were cached under, and whatever the
	// shader compiler said about the first one it rejected.  A count says how much is missing; the
	// key says what, and it is the only thing that does.
	unsigned Refused_Description_Count() const;
	const char * Refused_Description(unsigned index) const;

	// What each pipeline that did draw actually painted with: how many draws took it, and the size
	// of the texture bound at stage zero the first time one did.  A count of draws made says the
	// picture was drawn; it does not say what was drawn into it, and a ground that comes out white
	// looks exactly like a ground that came out right from every other number here.
	unsigned Pipeline_Report_Count() const;
	const char * Pipeline_Report(unsigned index);

	// The shipped shaders the refused draws had bound, and how many draws each cost.  This is the
	// list of what engineshader still has to transcribe, in the order worth doing it.
	unsigned Foreign_Report_Count() const;
	const char * Foreign_Report(unsigned index);

	// How many times the draws were sent into a texture instead of the back buffer, and how many
	// landed there.  A scene that comes back black usually means a target was set and not unset.
	void Target_Statistics(unsigned long long & bound, unsigned long long & restored,
		unsigned long long & draws) const
		{ bound = TargetsBound; restored = TargetsRestored; draws = DrawsIntoTargets; }
	const char * Diagnostic_Line() const { return Diagnostic.c_str(); }
	unsigned Target_Trace_Count() const { return (unsigned)TargetTrace.size(); }
	const char * Target_Trace(unsigned index) const
		{ return (index < TargetTrace.size()) ? TargetTrace[index].c_str() : ""; }
	const char * First_Compiler_Error() const { return CompilerError.c_str(); }

	// A program for a pass of someone else's (the hazard clouds), compiled once and kept in the
	// shader cache with the generated ones, so a second run compiles nothing.  The flags are
	// D3DCompile's first flag word; zero is its default optimisation.
	bool Compile_Program(const std::string & source, const char * name, const char * profile,
		std::vector<unsigned char> & bytecode, unsigned flags = 0);

	// A generated fixed-function program missing from both caches is compiled on a worker thread
	// rather than on the frame that first meets it, which cost that frame 100 to 130ms of FXC: the
	// draws that want it are refused until it lands, about a tenth of a second, and the frame goes
	// on without them.  Only a scene draw through a perspective camera takes this road.  These still
	// compile on the spot: a draw into a texture or into the sun's map (drawn once, or a shadow
	// lost), a pre-transformed one or one under an identity or orthographic projection (the
	// interface, Render2D's quads, the filters), a multiplicative pass (the terrain's shroud pass:
	// left out, it would show shrouded ground) and an engine shader (terrain, water, trees).  Roads
	// and bridges are fixed-function and may wait.  Off unless asked for, so a test that draws and
	// reads back sees its draw.
	void Set_Compile_Off_Frame(bool enabled) { CompileOffFrame = enabled; }
	// Programs the worker compiled, the longest one took from queue to landing, and the draws
	// refused while their program was being compiled.
	void Compile_Statistics(unsigned & compiled_off_frame, double & longest_milliseconds,
		unsigned long long & draws_waited) const
		{ compiled_off_frame = ProgramsCompiledOffFrame; longest_milliseconds = LongestOffFrameMilliseconds;
		  draws_waited = DrawsWaitedForProgram; }

private:
	DX11BackendClass(const DX11BackendClass &);
	DX11BackendClass & operator=(const DX11BackendClass &);

	struct Pipeline
	{
		ID3D11VertexShader * VertexShader;
		ID3D11PixelShader * PixelShader;
		ID3D11InputLayout * Layout;
	};

	// The whole constant block the generated vertex shader reads, always uploaded in full.  A shader
	// generated for fewer lights declares a prefix of this, and a constant buffer larger than what a
	// shader declares is legal, so one buffer serves every program.
	struct VertexConstantBlock
	{
		float WorldViewProjection[16];
		float WorldView[16];
		float NormalTransform[16];
		float TextureMatrix[MAXIMUM_VERTEX_STAGES][16];
		float MaterialAmbient[4];
		float MaterialDiffuse[4];
		float MaterialSpecular[4];
		float MaterialEmissive[4];
		float MaterialPower[4];
		float GlobalAmbient[4];
		float FogParameters[4];
		// One over the viewport's width and height, for the pre-transformed draws.  It goes before
		// the lights because the generated block declares only as many lights as the state has.
		float ViewportInverse[4];
		float LightFields[MAXIMUM_VERTEX_LIGHTS][VERTEX_REGISTERS_PER_LIGHT][4];
	};

	// A program that receives no shadow declares the first three fields and nothing else, which a
	// larger buffer serves.  The four after AlphaReference, and Sky and SkyUp, belonged to the normal
	// maps taken out on 2026-10-06; nothing reads them and nothing writes them, but the generated
	// programs still declare them, and every field behind them is found by its offset.  They stay
	// until the generators and the cached programs move together.
	struct PixelConstantBlock
	{
		float TextureFactor[4];
		float FogColour[4];
		float AlphaReference[4];
		float NormalLightDirection[NORMAL_MAPPED_LIGHTS][4];
		float NormalLightDiffuse[NORMAL_MAPPED_LIGHTS][4];
		float NormalMapParameters[4];
		float TerrainSunDirection[4];
		// Clip space to the sun's clip space, and the filter's own numbers: texel size, depth bias,
		// how dark a fully blocked pixel goes, and the radius in texels.
		float ShadowFromClip[16];
		float ShadowParameters[4];
		float ShadowViewport[4];
		// The narrowest the filter goes, how many texels it opens per world unit of gap between a
		// caster and what it falls on, how many world units a unit of depth is, and how much of a
		// wide shadow the sky fills back in.
		float ShadowSoftness[4];
		// What a metal surface mirrors: the map's own light as a sky colour with the share metal
		// returns in its alpha, and which way up the world is in camera space with the horizon's
		// share of the zenith colour in its own.
		float Sky[4];
		float SkyUp[4];
		// The smoke in the sun's light (VOLUMETRIC_SAMPLING): how dark the thickest smoke leaves
		// what is behind it, zero on a frame without; the same for a particle shading itself; one
		// for a draw in camera space; and the power on the self-shade.
		float VolumeParameters[4];
		// The headlights (HEADLIGHT_SAMPLING): the pixel's way back to the world, how many slots
		// are lit and the gain per channel, then each slot's position and reach and its direction
		// and cone edge.
		float WorldFromClip[16];
		float HeadlightParameters[4];
		float HeadlightPosition[HEADLIGHT_SLOTS][4];
		float HeadlightDirection[HEADLIGHT_SLOTS][4];
		// The dynamic point lights (BLAST_LIGHT_SAMPLING): how many are lit, then each slot's
		// position and far reach, diffuse and near reach, and ambient.
		float BlastLightParameters[4];
		float BlastLightPosition[BLAST_LIGHT_SLOTS][4];
		float BlastLightDiffuse[BLAST_LIGHT_SLOTS][4];
		float BlastLightAmbient[BLAST_LIGHT_SLOTS][4];
		// A soft particle's way from a depth back to a distance, and one over the distance its fade
		// takes (SOFT_PARTICLE_SAMPLING).  Only a program that fades or wears a look declares it.
		float SoftParticleDepth[4];
		// A per-object look's numbers (DRAW_EFFECT_SAMPLING).  Last, so only such a program
		// declares it.
		float DrawEffectParameters[4];
	};
	// A draw that is painting the world and can take a shadow from the sun's map.
	bool Shadow_Receiving() const;

	bool Resolve(Pipeline & pipeline);
	bool Build_Vertex_Description(VertexPipelineDescription & description) const;
	bool Build_Combiner_Description(CombinerDescription & description) const;
	void Upload_Constants();
	void Upload_Vertex_Constants();
	void Upload_World_Constants();
	void Upload_Pixel_Constants();

	// Both indexed draws land here; only the topology differs.
	bool Draw_Indexed(unsigned index_count, unsigned start_index, unsigned base_vertex,
		D3D11_PRIMITIVE_TOPOLOGY topology);

	// A dynamic constant buffer the blocks of one kind are written into one after another, each at
	// the next free entry with NO_OVERWRITE, and bound at that entry's offset.  Only a write that
	// finds the ring full discards it and starts again at zero, so a frame of thousands of writes
	// renames a few times instead of once a write.  Each kind has its own ring because a discard
	// throws away everything in the buffer: in a shared one the block a skipped write left behind
	// would be gone.  Without offset binding the ring is one entry long and every write discards,
	// which is how every write went before.
	struct ConstantRing
	{
		ID3D11Buffer * Buffer;
		unsigned EntryBytes;	///< the block rounded up to the 256 bytes an offset moves in
		unsigned Bytes;			///< the whole buffer, a whole number of entries
		unsigned Next;			///< where the next write goes; Bytes makes it discard
		unsigned Current;		///< where the last write went, which is what the draws read
		unsigned Discards;		///< every discard and every new buffer: an entry written since is still there
	};
	bool Create_Constant_Ring(ConstantRing & ring, unsigned block_bytes);
	bool Write_Constants(ConstantRing & ring, const void * block, unsigned bytes);
	void Release_Constant_Ring(ConstantRing & ring);

	// Which bank the vertex half is reading: the engine's own registers while a transcribed
	// program is bound, the generated block otherwise.
	const ConstantRing & Vertex_Constants() const;
	void Bind_State_Objects();
	void Release_Cached();

	ID3D11BlendState * Blend_State(bool glow, bool coverage);
	ID3D11DepthStencilState * Depth_Stencil_State();
	ID3D11RasterizerState * Rasterizer_State();
	ID3D11SamplerState * Sampler_State(unsigned sampler);

	DX11DeviceClass * Device;

	DX11StateBlockClass RenderStates;
	DX11SamplerBlockClass Samplers[DX11_BACKEND_TEXTURE_STAGES];
	DWORD StageStates[DX11_BACKEND_TEXTURE_STAGES][DX11_BACKEND_STAGE_STATES];
	ID3D11ShaderResourceView * Textures[DX11_BACKEND_TEXTURE_STAGES];

	DWORD VertexFormat;
	ID3D11Buffer * StreamBuffer;
	unsigned StreamStride;
	unsigned StreamOffset;
	ID3D11Buffer * IndexBuffer;
	DXGI_FORMAT IndexFormat;

	float World[16];
	float View[16];
	float Projection[16];	// The per-stage texture transforms.  Two of them, because ffvertex writes two and the game sets
	// no more than two stages in any state the probe counted.  These are what turn a camera space
	// position into the cloud shadow's coordinates, and with an identity in their place the whole
	// terrain samples one texel of it.
	float TextureTransforms[DX11_BACKEND_TEXTURE_STAGES][16];

	// What the viewport is, so a pre-transformed vertex can be put back into clip space.
	unsigned ViewportWidth;
	unsigned ViewportHeight;
	unsigned ViewportX;
	unsigned ViewportY;

	// The Modern water (dx11water.cpp): its registers, the stages past the fourth, the copies of the
	// scene it refracts through, the GPU ocean and the displaced vertex programs.  Prepare runs
	// before a water draw binds anything, because the ocean's passes take the context; Bind after
	// the draw's own bindings, Unbind after the draw.
	struct WaterResources;
	WaterResources * Water;
	static WaterResources * Create_Water();
	static void Destroy_Water(WaterResources * water);
	void Set_Water_Texture(unsigned stage, ID3D11ShaderResourceView * texture);
	bool Prepare_Water();
	void Bind_Water();
	void Unbind_Water();
	void Water_Scene_Changed(ID3D11Resource * target);	// null: a new frame, every copy is old
	void Release_Water();

	// The sun's depth buffer and the two views of it: one to draw into, one to sample.  The
	// viewport the frame was using is kept while it is bound, because the map is square and the
	// screen is not.
	ID3D11Texture2D * ShadowMapSurface;
	ID3D11DepthStencilView * ShadowMapDepth;
	ID3D11ShaderResourceView * ShadowMapTexture;
	unsigned ShadowMapSize;
	bool ShadowMapBound;
	unsigned ShadowMapSavedWidth;
	unsigned ShadowMapSavedHeight;
	// What the frame was drawing into when the pass took the device.  Almost never the back buffer:
	// the scene goes into a texture and the post chain puts it on the screen, so restoring the back
	// buffer here left the rest of the frame painting somewhere nobody shows.
	ID3D11RenderTargetView * ShadowMapSavedTarget;
	ID3D11SamplerState * ShadowMapSampler;
	float SunViewProjection[16];		///< what the sun was looking through while the map was filled
	float ShadowBias;
	float ShadowStrength;
	float ShadowRadius;
	float ShadowNarrowestRadius;
	float ShadowTexelsPerGap;
	float ShadowUnitsPerDepth;
	float ShadowSkyFill;
	bool ShadowReceiving;
	// The frame's clip space to the sun's, worked out from a scene view and projection pair and
	// kept until one of them changes, because an inverse a draw does not need is an inverse nobody
	// should pay for.
	float ShadowFromClip[16];
	float WorldFromClip[16];
	float ShadowFromClipView[16];
	float ShadowFromClipProjection[16];
	bool ShadowFromClipValid;

	// Set_Scene_View's matrix, and whether it has been set; and the projection held when it was,
	// which is the scene camera's own (Set_Scene_View follows the camera's Apply).
	float SceneView[16];
	float SceneProjection[16];
	bool SceneViewKnown;
	// Set_Scene_View ran since the last Open_Frame: the haze reprojects with this frame's camera or
	// not at all (the shadow pass returns before handing the view over on a frame it skips).
	bool SceneViewThisFrame;
	bool Camera_Space_Draw() const;
	bool SmokeGlow;
	bool Smoke_Glow() const;
	// Set_Fire_Look, and which way the current draw wears it (a FIRE_LOOK_* value).
	bool FireLook;
	unsigned Fire_Look() const;
	// Set_Emissive: the current draws take no shadow and no smoke shade (Shadow_Receiving).
	bool Emissive;
	// Set_Team_Glow, and whether the current draw takes it.
	bool TeamGlow;
	bool Team_Glow() const;
	// Set_Hemi_Ambient.
	bool HemiAmbient;
	// Set_Draw_Effect's look and numbers, and the look the current draw takes after the checks.
	unsigned DrawEffect;
	float DrawEffectNumbers[4];
	unsigned Draw_Effect() const;
	// The scene as it stood when the first cloaked draw of the frame needed it, at t8: one copy a
	// frame, taken at Set_Draw_Effect.  The scene asks for the cloak only in its translucent pass,
	// after the opaque world, so the copy holds what is behind them.  Refused for good when the device will not make it.
	ID3D11Texture2D * SceneColourCopy;
	ID3D11ShaderResourceView * SceneColourCopyView;
	bool SceneColourStale;
	bool SceneColourRefused;
	bool Make_Scene_Colour();
	bool Take_Scene_Colour();
	void Release_Scene_Colour();
	float AdditiveGain;
	bool GlowDraw;
	bool Additive_Glow() const;
	// An alpha tested draw without blending into the multisampled scene covers the samples its
	// alpha says, which smooths a leaf's or a fence's cut edge the way the samples smooth a
	// triangle's.  Never into a single-sample target, where it would only dither, never for a test
	// that keeps the low alphas, and never under Classic, whose fences are -d3d9's hard ones.
	bool Alpha_To_Coverage() const;
	ID3D11ShaderResourceView * SceneStandIn;
	// A stage samples the target the draw is going into: the heat haze, which bends a picture that
	// already took its shadows, and would take them a second time.
	bool Samples_Current_Target() const;

	// The smoke's map (Fill_Smoke_Map), the program that splats the casters into it and what it
	// draws with.  Made on the first fill and kept; refused for good if the device cannot.
	ID3D11Texture2D * SmokeMapSurface;
	ID3D11RenderTargetView * SmokeMapTarget;
	ID3D11ShaderResourceView * SmokeMapTexture;
	ID3D11SamplerState * SmokeMapSampler;
	ID3D11VertexShader * SmokeSplatVertexShader;
	ID3D11PixelShader * SmokeSplatPixelShader;
	ID3D11InputLayout * SmokeSplatLayout;
	ID3D11BlendState * SmokeSplatBlend;
	ID3D11RasterizerState * SmokeSplatRasterizer;
	ID3D11Buffer * SmokeSplatInstances;
	unsigned SmokeSplatCapacity;
	std::vector<float> SmokeSplats;
	bool SmokeMapRefused;
	bool SmokeMapFilled;
	float SmokeStrength;
	// Draw_Shadow_Casters' programs and layouts a kind of run, the alpha test's pixel program, the
	// instance buffer and the bones' buffer, both of which grow by doubling and are written once a
	// pass, and the skin source.  Made on the first pass.
	bool Make_Shadow_Caster_Program();
	void Release_Shadow_Casters();
	ID3D11VertexShader * ShadowCasterVertexShaders[3];
	ID3D11InputLayout * ShadowCasterLayouts[3];
	ID3D11PixelShader * ShadowCasterAlphaTest;
	ID3D11Buffer * ShadowCasterInstances;
	unsigned ShadowCasterCapacity;
	ID3D11Buffer * ShadowCasterBones;
	ID3D11ShaderResourceView * ShadowCasterBonesView;
	unsigned ShadowCasterBonesCapacity;
	bool ShadowCasterRefused;
	ID3D11Buffer * ShadowSkinSource;
	unsigned ShadowSkinCapacity;
	unsigned ShadowSkinUsed;
	unsigned ShadowSkinGeneration;
	// Set_Headlights' slots, eight floats each, how many are lit and the gain per channel.
	float Headlights[HEADLIGHT_SLOTS][8];
	unsigned HeadlightCount;
	float HeadlightGain[3];
	// Set_Blast_Lights' slots and how many are lit.
	float BlastLights[BLAST_LIGHT_SLOTS][BLAST_LIGHT_FLOATS];
	unsigned BlastLightCount;
	// Set_Soft_Particles, and which fade the current draw takes (a SOFT_PARTICLE_* value).
	bool SoftParticles;
	bool SoftParticlesAllowed;
	unsigned Soft_Particle() const;
	// The scene's depth as a texture, copied when a soft particle first needs it after anything
	// wrote depth: a depth buffer cannot be sampled while it is the one being tested against.
	ID3D11Texture2D * SceneDepthCopy;
	ID3D11ShaderResourceView * SceneDepthCopyView;
	bool SceneDepthStale;
	bool SceneDepthRefused;		///< the device would not make the copy; no draw fades again
	unsigned FrameDepthCopies;
	void Release_Scene_Depth();
	// Resolve_Scene_Depth's pass (Make_Depth_Resolve); refused for good if the device cannot.
	// Stale while a clear or a draw into the scene wrote the multisampled depth since the last one.
	ID3D11VertexShader * DepthResolveVertexShader;
	ID3D11PixelShader * DepthResolvePixelShader;
	ID3D11DepthStencilState * DepthResolveState;
	ID3D11RasterizerState * DepthResolveRasterizer;
	bool DepthResolveStale;
	bool DepthResolveRefused;
	void Release_Depth_Resolve();
	bool Make_Smoke_Map();
	void Release_Smoke_Map();

	float MaterialAmbient[4];
	float MaterialDiffuse[4];
	float MaterialSpecular[4];
	float MaterialEmissive[4];
	float MaterialPower;

	struct Light
	{
		bool Enabled;
		DWORD Type;
		float Position[4];
		float Direction[4];
		float Diffuse[4];
		float Specular[4];
		float Attenuation[4];
		float Spot[4];
		float Ambient[4];
	};
	Light Lights[MAXIMUM_VERTEX_LIGHTS];

	ConstantRing VertexConstantRing;
	ConstantRing PixelConstantRing;

	// The engine's own constant bank and the ring it is uploaded through.  It replaces the
	// generated vertex block at slot zero while a transcribed program is bound, because that
	// program reads registers and not the fixed-function fields.
	ConstantRing EngineConstantRing;
	float EngineConstants[ENGINE_SHADER_CONSTANTS][4];

	// The context's 11.1 face, which binds a constant buffer at an offset.  Null when the runtime or
	// the driver cannot bind at an offset or map a constant buffer without discarding it; the rings
	// are then one entry long and bound whole.
	ID3D11DeviceContext1 * OffsetContext;
	unsigned FrameConstantWrites;
	unsigned FrameConstantRenames;
	const char * ConstantPath;

	// What each constant buffer already holds.
	//
	// Upload_Constants runs on every draw, and a write used to be a Map with
	// D3D11_MAP_WRITE_DISCARD, the driver being told to hand back a fresh region: three renames a
	// draw, thousands of draws a frame, and NtGdiDdDDICreateAllocation sitting in a steady-state
	// profile where nothing should be allocating at all.  The rings above took the renames out of
	// the writes that remain.  Most draws in a row change none of this - the transforms, the
	// material and the lights are set once for a whole batch - so the block is built into a local,
	// compared against the copy here, and written through only when it differs.  A draw that
	// writes nothing binds the entry the last write went to.
	//
	// The setters do flag the blocks now (ConstantsChanged below), but only to skip building them.
	// The byte compare still decides the write: a flag says something was set, often to the value
	// it already had in another order, and only the bytes say whether the buffer is different.
	VertexConstantBlock HeldVertexConstants;
	PixelConstantBlock HeldPixelConstants;
	float HeldEngineConstants[ENGINE_SHADER_CONSTANTS][4];

	// The last few pixel blocks written, with the entry each went to and the ring's discard count
	// then.  The sorted flush goes back and forth between a few of them, a billboard run's soft
	// particle depth and a streak's none, and one still in the buffer is bound where it lies instead
	// of written again (Rebind_Recent_Pixel_Constants).
	enum { RECENT_PIXEL_BLOCKS = 4 };
	PixelConstantBlock RecentPixelConstants[RECENT_PIXEL_BLOCKS];
	unsigned RecentPixelOffset[RECENT_PIXEL_BLOCKS];
	unsigned RecentPixelDiscards[RECENT_PIXEL_BLOCKS];
	unsigned NextRecentPixel;
	bool Rebind_Recent_Pixel_Constants(const PixelConstantBlock & block);

	// A freshly created dynamic buffer holds nothing in particular, and a Map can fail, so a copy
	// above only stands for what is in its buffer once a write has actually gone through.  One flag
	// each rather than one between them: the engine bank is skipped on every draw that takes no
	// transcribed program, and a shared flag would claim it was written when it was not.
	bool VertexConstantsHeld;
	bool PixelConstantsHeld;
	bool EngineConstantsHeld;

	// Whether anything the draws read has changed since the last draw used it, so a draw that
	// follows another with nothing set in between skips building what it would build identically:
	// the two constant blocks with their three matrix products, both pipeline descriptions, and a
	// sampler description a stage.  The terrain is the case this is for, hundreds of tiles in a row
	// under one transform and one state.  Every setter that writes something a block, a description
	// or a sampler reads sets the matching flag, and only when the value actually differs.  A new
	// input to any of them needs its setter to raise the flag here, or the draw after it reads the
	// previous batch's lighting.
	bool ConstantsChanged;
	// An input of one block only: the world and texture transforms and the material reach only the
	// vertex block, the terrain sun, the scene view and the smoke only the pixel block.  A mesh
	// changes its world transform every draw, and rebuilding the pixel block for it was a rebuild
	// whose bytes always matched.  ConstantsChanged stands for both.
	bool VertexConstantsChanged;
	bool PixelConstantsChanged;
	// The blend switch or either factor, which the pixel block reads only through Pixel_Blend_Key:
	// the block is built again only when the key moved from the one it was last built with.  The
	// sorted flush flips the blend at nearly every run, and each flip rebuilt and compared the
	// whole block to find the same bytes.
	bool PixelBlendChanged;
	unsigned HeldPixelBlendKey;
	unsigned Pixel_Blend_Key() const;
	// A blend that multiplies what is under it: it takes no blast light.
	bool Multiplies_Below() const;
	// The world transform alone, which is every mesh draw: with nothing else flagged the held block
	// is the rest of the next one, and only its three world products are made again
	// (Upload_World_Constants).
	bool WorldChanged;
	bool PipelineChanged;
	bool SamplerChanged[DX11_BACKEND_TEXTURE_STAGES];
	// A stage's texture changed, or the last draw sampled the current target and so needs a fresh
	// copy of it: either way the stages have to be looked at again before the next draw binds.
	bool TexturesChanged;
	// A render state, or the shadow caster pass that answers for some of them, changed since the
	// three state objects were last looked up.
	bool StateObjectsChanged;
	// Whether Memos[LastMemo] is what the last Resolve handed back.  A refusal clears it, so a draw
	// that was refused is refused again through the whole path and counted the same way.
	bool LastResolveHeld;
	void Note_Shadow_State_Changed() { ConstantsChanged = true; PipelineChanged = true; StateObjectsChanged = true; }
	EngineShaderProgram VertexProgram;
	EngineShaderProgram PixelProgram;

	// Where the screen quads are copied to, grown to fit and never shrunk.
	ID3D11Buffer * UserBuffer;
	unsigned UserBufferBytes;
	bool Reserve_User_Buffer(unsigned byte_count);

	std::map<std::string, Pipeline> Pipelines;
	// The state combinations that could not be built.  A refusal costs two HLSL generations and two
	// D3DCompile calls, and the states that produce one are produced again on the next frame by the
	// same object: a scene with a hundred and thirty of them a frame spent a second a frame
	// compiling shaders it had already failed to compile.
	std::map<std::string, unsigned> RefusedPipelines;

	// What a pipeline drew with, kept beside the pipeline itself rather than in it, because a draw
	// holds a copy of the pipeline and would be updating the copy.
	struct PipelineUse
	{
		unsigned long long Draws;
		unsigned TextureWidth;
		unsigned TextureHeight;
	};
	std::map<std::string, PipelineUse> PipelineUses;
	std::string ReportLine;
	PipelineUse * Record_Use(const std::string & key);

	// The pipelines Resolve worked out most recently, and the state each was worked out from.
	//
	// Resolve runs once a draw and its key costs about twenty snprintf calls, a handful of string
	// appends and two lookups in a map keyed by that string.  Sampling a burning column at 1280x720
	// put a eighth of the whole frame inside the C runtime's integer formatter.  The renderer
	// batches by texture and material, so consecutive draws ask for the same pipeline far more
	// often than not, and a draw that matches skips the key entirely.  One remembered pipeline was
	// enough for that; a sorted particle frame goes back and forth between a few pipelines draw after
	// draw, and with a hundred thousand particles the formatter was back at seven percent of the
	// frame.  So a handful are kept, the one used last checked first and the oldest written over.
	//
	// The descriptions are compared with memcmp, which is safe because Build_Vertex_Description and
	// Build_Combiner_Description both memset before they fill: no padding byte is ever undefined.
	// Use points into PipelineUses, whose nodes are stable across insertion; the whole memo is
	// dropped in Release_Cached, where the pipelines it names are released.
	struct ResolveMemo
	{
		bool Valid;
		VertexPipelineDescription Vertex;
		CombinerDescription Combiner;
		DWORD Format;
		EngineShaderProgram VertexProgram;
		EngineShaderProgram PixelProgram;
		Pipeline Resolved;
		PipelineUse * Use;
	};
	enum { RESOLVE_MEMO_ENTRIES = 8 };
	ResolveMemo Memos[RESOLVE_MEMO_ENTRIES];
	unsigned LastMemo;		///< the entry the last hit or write used; checked first
	unsigned NextMemo;		///< the entry the next miss writes over
	void Remember_Resolution(PipelineUse * use, const Pipeline & resolved,
		const VertexPipelineDescription & vertex, const CombinerDescription & combiner);

	// Every resolution a memo miss has already worked out, keyed by the state's own bytes.  A frame
	// asks for more pipelines in turn than the memo holds, and each miss built the string key again:
	// about twenty snprintf calls to find a pipeline it had found many times.  The string key is
	// built once per state here and kept, for a refusal's count.  The key is memset before it is
	// filled, so hashing and comparing its bytes is safe for the memo's reason; the map is dropped
	// in Release_Cached with the pipelines it names.
	struct ResolveKey
	{
		DWORD Format;
		EngineShaderProgram VertexProgram;
		EngineShaderProgram PixelProgram;
		VertexPipelineDescription Vertex;
		CombinerDescription Combiner;
	};
	struct ResolveKey_Hash
	{
		size_t operator()(const ResolveKey & key) const;
	};
	struct ResolveKey_Equal
	{
		bool operator()(const ResolveKey & left, const ResolveKey & right) const
		{
			return memcmp(&left, &right, sizeof(ResolveKey)) == 0;
		}
	};
	struct Resolution
	{
		bool Refused;
		unsigned Reason;		///< a RefusalReason, as RefusedPipelines holds it
		Pipeline Resolved;
		PipelineUse * Use;
		std::string Key;
	};
	typedef std::unordered_map<ResolveKey, Resolution, ResolveKey_Hash, ResolveKey_Equal>
		ResolutionMap;
	ResolutionMap Resolutions;

	// Where the draws are landing.  Null means the device's own back buffer and depth buffer.
	// The first few target changes, in order, with the draw count at each one.  Which target is
	// bound when the scene is drawn is the whole question and no total answers it.
	static const unsigned TARGET_TRACE_LIMIT = 24;
	std::vector<std::string> TargetTrace;
	void Trace_Target(const char * what, unsigned width, unsigned height);
	bool TracedUserStrip;
	DWORD MaskWhileTargeted;
	std::string Diagnostic;
	std::string Texture_Average(unsigned stage);

	unsigned long long TargetsBound;
	unsigned long long TargetsRestored;
	unsigned long long DrawsIntoTargets;
	ID3D11RenderTargetView * CurrentTarget;
	ID3D11DepthStencilView * CurrentDepth;
	std::map<unsigned long long, ID3D11DepthStencilView *> TargetDepths;
	ID3D11DepthStencilView * Depth_For(unsigned width, unsigned height);

	// A draw that samples the target it is drawing into reads a copy of it taken just before the
	// draw.  Direct3D 9 handed such a draw whatever had been drawn so far; Direct3D 11 unbinds a
	// resource that is bound both ways and the sample reads zero.  Heat haze samples the scene while
	// the scene is still going into that texture, and read as zero it put a black blob around every
	// microwave tank.
	ID3D11Resource * CurrentTargetResource;
	ID3D11Texture2D * TargetCopy;
	ID3D11ShaderResourceView * TargetCopyView;
	ID3D11ShaderResourceView * Readable_Texture(unsigned stage, ID3D11ShaderResourceView * texture);
	// Per stage, the last view asked about and whether it views the current target.  Mutable because
	// Shadow_Receiving asks the same question while the pipeline is described.
	bool Views_Current_Target(unsigned stage, ID3D11ShaderResourceView * texture) const;
	mutable ID3D11ShaderResourceView * TargetCheckedViews[DX11_BACKEND_TEXTURE_STAGES];
	mutable bool TargetCheckedIsTarget[DX11_BACKEND_TEXTURE_STAGES];

	// Keyed by the description itself and ordered by its bytes, which is safe for the ResolveMemo's
	// reason: every Build_*_Description memsets first.  They were keyed by the bytes copied into a
	// std::string, longer than the string's own small buffer, so every lookup allocated and freed.
	struct Description_Bytes_Less
	{
		template <class Description>
		bool operator()(const Description & left, const Description & right) const
		{
			return memcmp(&left, &right, sizeof(Description)) < 0;
		}
	};
	typedef std::map<D3D11_BLEND_DESC, ID3D11BlendState *, Description_Bytes_Less> BlendStateMap;
	typedef std::map<D3D11_DEPTH_STENCIL_DESC, ID3D11DepthStencilState *, Description_Bytes_Less>
		DepthStencilStateMap;
	typedef std::map<D3D11_RASTERIZER_DESC, ID3D11RasterizerState *, Description_Bytes_Less>
		RasterizerStateMap;
	typedef std::map<D3D11_SAMPLER_DESC, ID3D11SamplerState *, Description_Bytes_Less> SamplerStateMap;
	BlendStateMap BlendStates;
	DepthStencilStateMap DepthStencilStates;
	RasterizerStateMap RasterizerStates;
	SamplerStateMap SamplerStates;

	// The last description each kind of state object was looked up by, and the object it got.
	//
	// Bind_State_Objects makes seven lookups a draw, and a particle-heavy frame is several hundred
	// draws whose states change far less often than that.  A null object means no memo;
	// Release_Cached nulls them all.  The blend, depth stencil and rasterizer descriptions are built
	// from the render states alone, so while StateObjectsChanged is down the three memos are what a
	// build would find and the draw skips building and comparing them.
	D3D11_BLEND_DESC LastBlendDescription;
	ID3D11BlendState * LastBlendState;
	bool LastBlendGlow;
	bool LastBlendCoverage;
	D3D11_DEPTH_STENCIL_DESC LastDepthStencilDescription;
	ID3D11DepthStencilState * LastDepthStencilState;
	D3D11_RASTERIZER_DESC LastRasterizerDescription;
	ID3D11RasterizerState * LastRasterizerState;
	D3D11_SAMPLER_DESC LastSamplerDescriptions[DX11_BACKEND_TEXTURE_STAGES];
	ID3D11SamplerState * LastSamplerStates[DX11_BACKEND_TEXTURE_STAGES];
	void Forget_Last_State_Objects();

	// What the last draw left bound on the context, so the next one only makes the calls that change
	// something.  Every draw used to make fifteen binding calls, and in the fireball scene the runtime
	// behind them was the largest single thing on the main thread.  Only the draws below bind through
	// this; whatever else binds on the context calls Forget_Bindings, and so does anything that
	// changes the render target, since the runtime unbinds any view of a texture that becomes one.
	struct ContextBindings
	{
		bool Known;
		ID3D11BlendState * Blend;
		ID3D11DepthStencilState * DepthStencil;
		UINT StencilReference;
		ID3D11RasterizerState * Rasterizer;
		ID3D11SamplerState * Samplers[DX11_BACKEND_TEXTURE_STAGES];
		ID3D11ShaderResourceView * Textures[DX11_BACKEND_TEXTURE_STAGES];
		// The sun's map and the smoke's at t5 and t6, and their samplers as last passed: the
		// second sampler is null when only the first was set.
		ID3D11ShaderResourceView * Maps[2];
		ID3D11SamplerState * MapSamplers[2];
		// The scene's depth copy at t7, for the soft particles.
		ID3D11ShaderResourceView * SceneDepth;
		// The scene's colour copy at t8, for the cloak.
		ID3D11ShaderResourceView * SceneColour;
		ID3D11InputLayout * Layout;
		ID3D11Buffer * VertexBuffer;
		UINT VertexStride;
		UINT VertexOffset;
		ID3D11Buffer * IndexBuffer;
		DXGI_FORMAT IndexFormat;
		D3D11_PRIMITIVE_TOPOLOGY Topology;
		ID3D11VertexShader * VertexShader;
		ID3D11Buffer * VertexConstants;
		UINT VertexConstantsOffset;
		ID3D11PixelShader * PixelShader;
		ID3D11Buffer * PixelConstants;
		UINT PixelConstantsOffset;
	};
	ContextBindings Bound;
	void Bind_Pipeline(const Pipeline & pipeline, ID3D11Buffer * vertices, UINT stride,
		UINT offset, ID3D11Buffer * indices, DXGI_FORMAT index_format,
		D3D11_PRIMITIVE_TOPOLOGY topology);

	unsigned PipelinesBuilt;
	unsigned long long DrawsMade;
	unsigned long long DrawsRefused;
	double FrameBuildMilliseconds;
	unsigned FrameBuildCount;
	enum RefusalReason { REFUSED_NO_STAGE, REFUSED_NO_LAYOUT, REFUSED_NO_PROGRAM,
		REFUSED_NO_OBJECT };

	void Refuse(const std::string & key, RefusalReason reason);
	void Record_Compiler_Error(const char * which, ID3DBlob * errors);
	void Note_Refusal(const char * reason);
	bool Any_Missing_Texture() const;
	void Dump_Program(const std::string & key, const std::string & vertex_hlsl,
		const std::string & pixel_hlsl);
	void Save_Shader_Cache() const;

	std::string CompilerError;
	std::string DumpDirectory;

	// Bytecode by the hash of the profile and the source it was compiled from.  Two pipelines that
	// generate the same vertex program share one compile, and so do two runs through the file.
	std::map<unsigned long long, std::vector<unsigned char> > CompiledPrograms;

	// The worker that compiles off the frame (Set_Compile_Off_Frame), made with its first job and
	// joined in Shutdown.  It runs D3DCompile and nothing else: the device and the context stay on
	// this thread, so a device reset leaves it nothing to touch.
	struct CompileWorker;
	CompileWorker * Worker;
	bool CompileOffFrame;
	// Pipelines waiting on the worker, by key, with the hashes of their two programs.
	std::map<std::string, std::pair<unsigned long long, unsigned long long> > PendingPipelines;
	// Programs the worker could not compile: the pipeline then compiles on the spot, which records
	// the compiler's error and refuses it the usual way.
	std::map<unsigned long long, bool> FailedOffFrame;
	unsigned ProgramsCompiledOffFrame;
	double LongestOffFrameMilliseconds;
	unsigned long long DrawsWaitedForProgram;
	// Whether the draw being resolved may wait for the worker (Set_Compile_Off_Frame has which may not).
	bool Compiles_Off_Frame() const;
	bool Program_Settled(unsigned long long hash) const
		{ return CompiledPrograms.count(hash) != 0 || FailedOffFrame.count(hash) != 0; }
	bool Queue_Off_Frame(unsigned long long hash, const std::string & source, const char * name,
		const char * profile, unsigned flags);
	void Take_Off_Frame_Programs();
	void Stop_Compile_Worker();

	std::string ShaderCachePath;
	bool ShaderCacheChanged;
	unsigned ShippedPrograms;
	unsigned long LastShaderCacheSave;

	unsigned long long RefusedNoBuffer;
	unsigned long long RefusedNoStage;
	unsigned long long RefusedNoLayout;
	unsigned long long RefusedNoProgram;
	unsigned long long RefusedNoObject;
	unsigned long long RefusedForeignShader;
	unsigned long long RefusedNoTexture;

	bool MissingTexture[DX11_BACKEND_TEXTURE_STAGES];
	bool ForeignPixelShader;
	bool ForeignVertexShader;
	std::string ForeignPixelName;
	std::string ForeignVertexName;

	// Which shipped shader the refusals are on, counted by file name.  Nothing else says it: the
	// refusal happens before there is a pipeline key, so the state that would describe the draw is
	// never built.
	std::map<std::string, unsigned long long> ForeignRefusals;
	void Note_Foreign_Refusal();
};

#endif // DX11BACKEND_H
