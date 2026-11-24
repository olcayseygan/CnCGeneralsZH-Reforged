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

/*
** A render target between the scene and the swap chain, and full screen passes over it.
**
** Everything the Direct3D 11 backend does up to here answers one question: does it draw the same
** frame Direct3D 9 draws.  This is the first thing that deliberately draws a different one.  The
** scene goes into a texture instead of into the back buffer, a pixel shader reads that texture and
** writes the back buffer, and what that shader does is the effect.  Nothing above this knows: the
** backend binds Get_Scene_View() where it used to bind the back buffer view and the change ends
** there.
**
** The game asks for bloom, fxaa and sharpen unless -dx11post says otherwise.  An effect that
** improves the picture makes the frame differ from Direct3D 9's on purpose, which is the one thing
** dx11-check.ps1 is built to refuse, so that script passes "-dx11post off" and measures the
** backend without it.
**
** Where it runs matters as much as what it does.  W3DView::draw calls for the chain at the moment
** the world is finished and before the health bars, the unit names and the command bar go over it.
** An edge filter cannot tell a one pixel line from the edge of something bigger, so run over a
** health bar it eats the border, and run over the command bar it doubles the strokes of the font.
** Both of those were measured before the call moved.
**
** The pass sets its own viewport with no half pixel in it.  DX11BackendClass::Set_Viewport carries
** Direct3D 9's pixel centre for the scene's geometry; a full screen triangle wants the texel it is
** over and nothing else, and inheriting that offset resamples the whole frame half a pixel across.
**
** Bloom is why the scene target can be a float one.  Generals blends its explosion particles
** additively, so a stack of them is already brighter than white before it is written down; in an
** eight bit target that arrives clamped and the middle of a fireball is the same white as its edge.
** With a half float target the energy survives, the bright pass can threshold on it, and what
** blooms is the fireball rather than every pale building.  The frame comes back to eight bits
** through a tone curve with a knee: below the knee nothing moves at all, so a frame with nothing
** overbright in it is the frame that went in, and everything above the knee is folded into the gap
** between the knee and white.
*/

#ifndef DX11POST_H
#define DX11POST_H

#include <d3d11.h>
#include <vector>

class DX11DeviceClass;

#include "postprocess.h"

// Bloom's threshold and strength in the scene's own units, where one is white, what an additive
// draw into the scene is multiplied by (DX11BackendClass::Set_Additive_Gain) and how many pairs
// of blur passes spread the glow, each pair a step wider than the one before.  The game's Glow
// option sets them every frame through W3DShaderManager, since nothing in this library may read
// GlobalData.  An intensity of zero skips the bright pass and the blur and leaves only the tone
// curve, so the half float scene still reaches eight bits, and it takes the gain back to one.
// Until somebody calls this the chain runs at the values it was tuned at, which is what the tests
// measure.  Saturation pushes the glow away from its own grey before it is spread, so a fire's halo
// comes out of the tone curve orange rather than cream: one keeps the fire's hue as it was, the
// Glow option asks for 1.5 (ZH Enhanced's ReShade bloom ran 1.53), and it is held under 3.
void DX11Post_Set_Bloom(float threshold, float intensity, float additive_gain, unsigned blur_passes,
	float saturation = 1.0f);

// Clarity, local contrast on the luminance against a blur about fifteen pixels wide at 1080p, laid
// on where the half float scene comes down to eight bits, between the tone curve and the grade.  The
// strength is Clarity.fx's own (ZH Enhanced ran 0.75): a mid grey moves by half of it times how much
// brighter or darker than its surroundings it is.  Zero, the default and what the tests start from,
// is no clarity and leaves the chain drawing what it drew; above zero the bloom's bright pass and
// first two blur pairs run even with the glow off, since the blur rides their targets.  The game
// sets it every frame from the LightingStyle option.  A chain that does not start with bloom keeps
// its scene in eight bits and never takes it.
void DX11Post_Set_Clarity(float strength);

// The gain the scene's additive draws take this frame: one whenever the glow is off.
float DX11Post_Additive_Gain();

// The Reforged lighting style's grade, laid on wherever the half float scene comes down to eight
// bits (after the tone curve, in the bloom composite and the plain tone pass): a gentle S that
// sinks the darks and lifts the lights, a little colour taken out, and the lights pushed warm and
// the shade cool around mid grey.  Zero, the default and what the tests measure, is no grade; one
// is the whole of it.  The game sets it every frame from the LightingStyle option.  A chain that
// does not start with bloom keeps its scene in eight bits and is never graded.
void DX11Post_Set_Grade(float amount);

// The Internal HDR Tone Mapping option: the half float scene comes down to eight bits through
// wide_curve instead of tone_curve, wherever a match's frame does (the bloom composite and the plain
// tone pass), so what was past white keeps its colour and its order instead of ending in one white.
// A pixel with no channel past white comes out as it did.  Off, the default and what the tests start
// from, runs the old programs.  The frame on the monitor is still eight bits: this is no HDR output.
// The game sets it every frame from the option.  A chain that does not start with bloom keeps its
// scene in eight bits and has nothing to map.
void DX11Post_Set_Internal_Hdr(bool on);

// The Warm Light option's ground haze (MapLight): a pale layer lying in the low ground,
// laid over the opaque world before the particles out of the frame's depth taken back to world
// height.  Top is the world height the haze thins out to nothing at, thickness how far below that
// it reaches full strength, strength the most of the haze colour a pixel takes (zero, the default,
// is no pass at all) and reach the eye distance at which it stops thickening.  Nothing below water,
// the world height of the water surface the haze lies over, takes any.  Toward_sun points at the
// sun and warmth is how much warmer the haze looks toward it.  Set every frame.
void DX11Post_Set_Haze(const float colour[3], float top, float thickness, float strength, float reach,
	float water, const float toward_sun[3], float warmth);

// The view of the shroud texture the terrain draws its fog with (Direct3D11_Set_Haze_Shroud finds
// it), and the terrain's own world to texture mapping (rect: world origin x, y, then one over the
// world size of the texture x, y).  The haze reads its green, 1 in plain sight, lower in the fog,
// 0 under the shroud, and fades with it, so it lies over exactly the fog the ground shows.  Set
// every frame; null reads everything as in sight.  The view is borrowed, not held.
void DX11Post_Set_Haze_Shroud(ID3D11ShaderResourceView * view, const float rect[4]);

// The Atmosphere option: the far ground fades toward the horizon's colour and whatever the frame left
// undrawn takes a sky.  Laid over the opaque world with the haze, under the particles.  Nothing moves
// but the camera.  Start is the least eye distance the fade begins at, the frame's own focus pushing it
// further at a higher camera; scale how far the fade takes to reach about two thirds of strength; ground
// the world height the camera's focus lies on; the map's width and height how far the playable map runs
// from the world's origin.  Tint multiplies the sky's texture, which is borrowed for the frame.  Strength
// zero, the default, is no pass at all.  Set every frame.
void DX11Post_Set_Atmosphere(const float horizon[3], const float tint[3], float start, float scale,
	float strength, float ground, float map_width, float map_height, ID3D11ShaderResourceView * sky);

// The SunShafts option: the sunlight that comes through between objects.  Two things, kept apart all the
// way to the screen so neither cancels the other.  The lift is decided on the surface a pixel shows: a
// surface in the sun's shadow takes none, ever, and a sunlit one takes light by how near the cast
// shadows lie either side of it across the sun's ground bearing, read from the sun's map itself, broken
// into soft streaks along the bearing: a lit gap between two shadows takes the whole band, a sunlit
// border of one shadow a share, ground with no shadow within Radius nothing.  The dimming is the air in
// the sun's shadow seen in front of a sunlit surface (a wall the sun is behind, a stand of trees),
// marched through a low layer of dust.  A shallow ray toward the sun's bearing takes a faint glare.
// Half size, blurred by distance and laid on the scene target before the particles.  Every number is
// the game's (W3DShaderManager's pushSunShafts); strength zero, the default and what the tests start
// from, is no pass at all.  Set every frame.
enum { DX11_POST_SUN_SHAFT_WATER_AREAS = 8 };
struct DX11PostSunShafts
{
	float Colour[3];				// the light the band gives back, the sun's colour (scaled so no channel passes one)
	float Strength;					// the whole effect's weight; zero is no pass
	float Ground;					// the world height the layer's floor and its density are measured from
	float Top;						// how far above Ground the layer reaches
	float Falloff;					// the height over which the density falls to a third
	float Samples;					// steps through the layer, and taps out to Radius on each side of a surface
	float Scale;					// what a world unit of dust at full density weighs (one over a full ray's)
	float Band;						// the light a sunlit gap between two shadows gives back, of the sun's colour
	float Dark;						// the light shaded dust takes off what is seen through it
	float Slack;					// world units nearer the sun a neighbour's caster must stand to count as shade
	float Side;						// the share of the band a sunlit border of one shadow only gives back
	float Radius;					// world units across the bearing a shadow still lights a surface by it
	float StreakWidth;				// world units across the bearing a streak's noise cell spans; nought, a flat half (their mean)
	float StreakLength;				// world units along the bearing it spans
	float DarkCap;					// the most the dimming takes, as a share of what the receivers' shadow takes
	float ShadedMost;				// the most shaded dust a ray counts, in full rays
	float Floor;					// the world height the layer starts at, the map's lowest ground
	float NormalOffset;				// world units off a surface along its normal its own sun is read at
	float TowardSun[2];				// the sun's bearing on the ground, of length one
	float Glare;					// the glare toward that bearing at a shallow ray
	float ShallowTop;				// how steep a ray (its share of straight down) still takes any glare
	float ShallowSpan;				// how much steeper than full glare that is
	// The water areas the layer stops at: a ray that ends under one of these surfaces inside its box
	// (min x, min y, max x, max y) ends on the surface, since water writes no depth.
	float WaterCount;
	float WaterBoxes[DX11_POST_SUN_SHAFT_WATER_AREAS][4];
	float WaterHeights[DX11_POST_SUN_SHAFT_WATER_AREAS];
};
void DX11Post_Set_Sun_Shafts(const DX11PostSunShafts & shafts);

// What the terrain draws past the playable map, for the Atmosphere's fade out there: border, how far the
// heightmap's own border strip runs past the playable edge, and reach, how much further the land drawn
// round the heightmap runs.  The ground out there goes into the horizon's colour over reach from the
// heightmap's edge.  Reach zero, the default, is no land past the map: the ring past the playable edge
// closes to the horizon at once.  Kept until changed.
void DX11Post_Set_Atmosphere_Edge(float border, float reach);

// A blast warping the picture around it: the frame drawn toward a point and then pushed out by a
// ring that travels away from it.  Not an effect in the chain and not a switch: whatever chain is
// set, one more pass goes on its end for every frame that has a warp in it, and a frame with none
// costs a branch.  With no chain at all there is no scene texture to bend and nothing happens.
//
// Everything is in screen terms, because the game has already done the projecting.  The centre is
// in texture coordinates, nought to one across the frame and down it, and may lie off the screen.
// The radii are fractions of the frame's height, so a circle stays round on a wide screen.  Pull
// is how far, as a fraction of Radius, the picture is drawn in at its strongest; RingStrength is how
// far the ring pushes it out at its crest, a fraction of Radius as well.
// The game sets the frame's warps every frame, and a count of zero clears them.  A count past the
// limit keeps the first four.
void DX11Post_Set_Warps(const DX11PostWarp * warps, unsigned count);

class DX11PostProcessClass
{
public:
	DX11PostProcessClass();
	~DX11PostProcessClass();

	bool Initialise(DX11DeviceClass * device);
	void Shutdown();

	// An empty chain is the whole feature turned off: Scene_View gives back nothing and the device
	// keeps drawing into the swap chain the way it always has.  A multisampled device is the
	// exception, since the swap chain cannot hold samples: its scene still goes into the target and
	// reaches the screen through a copy.
	void Set_Chain(const DX11PostEffect effects[DX11_POST_CHAIN_LIMIT], unsigned count);
	unsigned Chain_Length() const { return ChainLength; }

	// True when the scene is being kept in half floats, which is what bloom asks for.
	bool Scene_Is_Float() const;

	// Where the scene is drawn: the offscreen texture while a chain is set and the buffers are
	// built, and NULL otherwise, which the device reads as the swap chain.  So a failure anywhere
	// in here costs the effect and not the picture.
	ID3D11RenderTargetView * Scene_View();

	// Runs the chain into the swap chain's back buffer.  Called when the world has been drawn and
	// before anything two dimensional goes on top of it.  Returns false when this frame is already
	// finished.
	bool Run_Chain();

	// The scene into the swap chain with no effect over it, for a frame nobody ran the chain on: a
	// menu, a loading screen.  Without this a frame that never reached the world would sit in the
	// offscreen texture and the screen would stay black.  A half float scene still goes through the
	// tone curve here, because the swap chain cannot hold what it holds.
	bool Copy_Through();

	// Call at the top of a frame.  Arms the two above again.
	void Begin_Frame();

	// The chain's ambient occlusion multiplied into the scene target as it stands, out of the depth
	// given, when "ao" is in the chain and the scene is going into this chain's target.  Once a
	// frame: the chain leaves its own occlusion pass out after it.  The caller binds its target,
	// depth and viewport again afterwards.  False when it drew nothing.
	bool Occlude_Scene(ID3D11ShaderResourceView * depth);

	// The ground haze (DX11Post_Set_Haze) blended into the scene target as it stands, at the same
	// moment as Occlude_Scene and under the same terms, once a frame.  World_from_clip takes the
	// depth back to the world.  False when it drew nothing; the caller rebinds as for the other.
	bool Haze_Scene(ID3D11ShaderResourceView * depth, const float world_from_clip[16]);

	// The haze's program comes from the backend's cache of compiled programs (dx11shaders.cache), so
	// only the first run that meets it compiles it.  Wants_Haze_Program is true while the haze is on
	// and no program is loaded or refused; Load_Haze_Program takes the bytecode for the source
	// Haze_Program_Source gives, or null when the compiler refused it, which leaves the haze off.
	bool Wants_Haze_Program() const;
	const char * Haze_Program_Source() const;
	void Load_Haze_Program(const void * bytecode, SIZE_T size);
	// The same program compiled here, past any cache, for a caller without the backend (the tests).
	void Compile_Haze_Program();

	// The same again for the Atmosphere option's sky, over the whole frame before anything is drawn
	// on it, and its fade, over the haze.
	enum { ATMOSPHERE_PROGRAMS = 2 };
	bool Sky_Scene(const float world_from_clip[16]);
	bool Atmosphere_Scene(ID3D11ShaderResourceView * depth, const float world_from_clip[16]);
	bool Wants_Atmosphere_Programs() const;
	const char * Atmosphere_Program_Source(unsigned index) const;
	void Load_Atmosphere_Program(unsigned index, const void * bytecode, SIZE_T size);
	void Compile_Atmosphere_Programs();

	// The Ambient Occlusion option multiplied into the scene target as it stands: half size, in world
	// units out of the depth given, blurred, and brought back up by distance.  The scene calls it before
	// the water and every translucent draw, so none of those takes a corner's shade.  It takes the
	// place of the chain's "ao" that frame, whether or not the chain has one.  Projection_x and _y
	// are the scene projection's scale on each axis.  False when it drew nothing; the caller rebinds
	// as for Occlude_Scene.  The programs come through the backend's cache as the haze's do: three
	// sources, and the tests compile them here.
	// traced, when given, is the occlusion already worked out at half size by the Ray Traced level's
	// rays (DX11BackendClass::Trace_Ray_Occlusion), the first pass's format: what is kept, then the
	// distance along the eye's axis.  The first pass is skipped and the blur and upsample take it;
	// took_traced says whether it did (a texture not of the half size is ignored).
	bool World_Occlude_Scene(ID3D11ShaderResourceView * depth, const float world_from_clip[16],
		float projection_x, float projection_y, ID3D11ShaderResourceView * traced = NULL, bool * took_traced = NULL);
	// Whether World_Occlude_Scene would draw this frame, so nothing is traced for it in vain.
	bool World_Occlusion_Ready() const;
	// The shroud the first pass fades the occlusion by and its rectangle (Set_Shroud), for a pass
	// that takes its place; NULL when none was handed over this frame.
	ID3D11ShaderResourceView * World_Ao_Shroud(float rect[4]) const;
	bool Wants_World_Ao_Programs() const;
	const char * World_Ao_Program_Source(unsigned index) const;
	void Load_World_Ao_Program(unsigned index, const void * bytecode, SIZE_T size);
	void Compile_World_Ao_Programs();
	static const unsigned WORLD_AO_PROGRAMS = 3;

	// The SunShafts option (DX11Post_Set_Sun_Shafts) added into the scene target as it stands, at the
	// haze's moment and under its terms, once a frame.  Sun_map is the sun's depth map the frame's
	// shadows came from, sun_from_world the matrix it was filled through, bias the receivers' depth bias
	// and shadow_strength what their shadow takes off.  Heights is the terrain's byte heights (the
	// backend's Terrain_Heights_View) with heights_mapping its cells a world unit, border cells and world
	// units a byte; with them a caster is told from rising ground and the lift kept on the ground, without
	// them (NULL) only the surface's tilt keeps it there.  It borrows the occlusion's two half size
	// targets (the lift in R, the dimming in G) and runs its own blur and composite.  Strength zero
	// returns before binding anything.  False when it drew nothing.
	bool Sun_Shafts_Scene(ID3D11ShaderResourceView * depth, const float world_from_clip[16],
		ID3D11ShaderResourceView * sun_map, const float sun_from_world[16], float bias, float shadow_strength,
		float projection_x, float projection_y, ID3D11ShaderResourceView * heights = NULL,
		const float * heights_mapping = NULL);
	// Three programs, the march, the blur and the composite, through the backend's cache as the haze's.
	static const unsigned SUN_SHAFT_PROGRAMS = 3;
	bool Wants_Sun_Shaft_Programs() const;
	const char * Sun_Shaft_Program_Source(unsigned index) const;
	void Load_Sun_Shaft_Program(unsigned index, const void * bytecode, SIZE_T size);
	void Compile_Sun_Shaft_Programs();

	// The shroud outside Classic (Direct3D11_Set_Shroud).  Soften_Shroud takes the view of the
	// terrain's shroud texture, a texel a cell, and keeps two pictures of its own: the level each cell
	// shows, eased toward the texture's over a few tenths of a second of wall time, and that smoothed
	// up to SHROUD_SOFT_SCALE texels a cell, which it returns and which binds wherever the texture
	// would.  Seconds is the wall time since the last call, written whether the texture changed since.
	// Nothing draws once the levels have settled; the last picture stands.  A null cells view turns it
	// off.  The caller binds its target and viewport again, and forgets its bindings, afterwards.
	// Fog_light is the fogged cell's level (FogAlpha over 255) for Shroud_Memory_Scene, fogged whether
	// any cell is below clear: with none, once the levels settle, the memory pass has nothing to grey.
	ID3D11ShaderResourceView * Soften_Shroud(ID3D11ShaderResourceView * cells, float seconds,
		bool written, float fog_light, bool fogged);
	// The soft picture while Soften_Shroud last succeeded, else null.
	ID3D11ShaderResourceView * Soft_Shroud() const { return ShroudFogLight >= 0.0f ? ShroudSoft.Resource : NULL; }
	// Ground in the fog drained toward grey and dimmed a little, out of the depth given, at the haze's
	// moment and under its terms.  Needs the soft picture this frame.  False when it drew nothing.
	bool Shroud_Memory_Scene(ID3D11ShaderResourceView * depth, const float world_from_clip[16]);
	// Its three programs (the easing, the smoothing, the memory) come through the backend's cache as the
	// haze's do.
	static const unsigned SHROUD_PROGRAMS = 3;
	bool Wants_Shroud_Programs() const;
	const char * Shroud_Program_Source(unsigned index) const;
	void Load_Shroud_Program(unsigned index, const void * bytecode, SIZE_T size);

	// -heathaze, a prototype.  The flames' splats for this frame (Direct3D11_Set_Heat_Splats has the
	// layout); Heat_Haze_Scene then draws each into a half size target of screen shifts, added up,
	// and puts a copy of the scene back into the scene target read through them.  Once a frame,
	// before the chain; the splats are spent either way.  With none set it returns before binding
	// anything, so a run without the switch draws what it always drew.  False when it drew nothing.
	void Set_Heat_Splats(const float * splats, unsigned count);
	bool Heat_Haze_Scene(ID3D11ShaderResourceView * depth);
	// Its three programs (a vertex one, then two pixel ones) come through the backend's cache as the
	// haze's do, loaded the first time the switch is seen on, so no frame with a fire pays for them.
	// The tests compile them here instead.
	static const unsigned HEAT_HAZE_PROGRAMS = 3;
	bool Wants_Heat_Haze_Programs() const;
	const char * Heat_Haze_Program_Source(unsigned index) const;
	const char * Heat_Haze_Program_Profile(unsigned index) const;
	void Load_Heat_Haze_Program(unsigned index, const void * bytecode, SIZE_T size);
	void Compile_Heat_Haze_Programs();

	// Call once at the very top of a frame, before any scene of it (a mirror's included) begins.
	// Arms Occlude_Scene again.
	void Open_Frame();

	// One line for the shutdown report: the chain, the size of the buffers, and the reason there is
	// no chain if there is not one.
	const char * Diagnostic_Line() const { return Diagnostic; }

private:
	DX11PostProcessClass(const DX11PostProcessClass &);
	DX11PostProcessClass & operator=(const DX11PostProcessClass &);

	struct Target
	{
		ID3D11Texture2D * Texture;
		ID3D11RenderTargetView * View;
		ID3D11ShaderResourceView * Resource;
	};

	// Everything one full screen pass needs.  It grew past what a readable argument list holds once
	// bloom arrived: the passes no longer all read one texture, and no longer all run at the size
	// of the window.
	struct PassSetup
	{
		ID3D11PixelShader * Shader;
		ID3D11ShaderResourceView * Source;
		ID3D11ShaderResourceView * Extra;
		ID3D11RenderTargetView * Destination;
		unsigned DestinationWidth;
		unsigned DestinationHeight;
		unsigned SourceWidth;
		unsigned SourceHeight;
		float BlurX;
		float BlurY;
		// This pass reads the frame's depth rather than the frame, and wants the clip planes with it.
		bool Occlusion;
		// The scene came down without a match finishing it (the loading screen, interface and all
		// drawn into the scene target): the tone curve, but never the lighting style's grade.
		bool Ungraded;
		// The pass's colour multiplies what the destination holds instead of replacing it.
		bool Multiply;
		// The pass's colour goes over what the destination holds by its alpha.
		bool Blend;
		// The pass reads the world's fields of the constants at b1 and the shroud at t2, as the haze does.
		bool World;
		// The Atmosphere option's: its own constants in the haze's slots, the sky at t3 and s1.
		bool Atmosphere;
		// The bright pass's saturation (DX11Post_Set_Bloom), zero for every other pass.
		float Saturation;
		// The composite's clarity strength, or in a blur pass one when it carries the clarity's
		// luminance along (DX11Post_Set_Clarity).
		float Clarity;
		// The sun shafts' composite: the pass's colour added to what the destination holds after the
		// destination is dimmed by the pass's alpha (ONE, INV_SRC_ALPHA), the alpha kept.
		bool Shafts;
		// The sun's depth map at t3 and the terrain's heights at t4, for the shafts.
		ID3D11ShaderResourceView * SunMap;
		ID3D11ShaderResourceView * Heights;
	};

	// The occlusion's two half size targets, made the first time either user asks.
	bool Ensure_World_Ao_Targets(unsigned & half_width, unsigned & half_height);

	bool Draw_Atmosphere(unsigned index, ID3D11ShaderResourceView * depth, const float world_from_clip[16]);
	bool Create_Shaders();
	bool Create_States();
	// A multisampled target is drawn into and resolved, never read, so it has no shader view.
	bool Create_Target(Target & target, unsigned width, unsigned height, DXGI_FORMAT format,
		unsigned samples = 1);
	bool Create_Targets(unsigned width, unsigned height, bool floating, unsigned samples);
	void Release_Target(Target & target);
	void Release_Targets();
	unsigned Scene_Samples() const;
	bool Ensure_Buffers();
	// The scene as a texture the passes read: the scene target itself, or with multisampling its
	// samples resolved into SceneResolve, which is done again on every call.
	ID3D11ShaderResourceView * Scene_Source();
	bool Finish(const DX11PostEffect * effects, unsigned count);
	ID3D11ShaderResourceView * Run_Bloom(ID3D11RenderTargetView * destination,
		ID3D11ShaderResourceView * scene);
	void Draw_Pass(const PassSetup & pass);
	void Describe();

	DX11DeviceClass * Device;
	ID3D11VertexShader * VertexShader;
	ID3D11PixelShader * PixelShaders[DX11_POST_SIMPLE_EFFECTS];
	ID3D11PixelShader * ToneMapShader;
	ID3D11PixelShader * ToneMapWideShader;			// the tone pass under Internal HDR Tone Mapping
	ID3D11PixelShader * SharpenMultisampledShader;	// the sharpen pass over a multisampled scene
	ID3D11PixelShader * BloomExtractShader;
	ID3D11PixelShader * BloomBlurShader;
	ID3D11PixelShader * BloomCompositeShader;
	ID3D11PixelShader * BloomCompositeWideShader;	// the composite under Internal HDR Tone Mapping
	ID3D11PixelShader * WarpShader;
	ID3D11PixelShader * SceneOcclusionShader;
	ID3D11PixelShader * HazeShader;
	ID3D11PixelShader * AtmosphereShaders[ATMOSPHERE_PROGRAMS];
	// The sky's texture repeats, where every other read in the chain clamps.
	ID3D11SamplerState * WrapSampler;
	ID3D11SamplerState * Sampler;
	ID3D11BlendState * BlendState;
	ID3D11BlendState * MultiplyBlendState;
	ID3D11BlendState * AlphaBlendState;
	// The sun shafts' composite: the pass's colour over the destination dimmed by the pass's alpha.
	ID3D11BlendState * ShaftBlendState;
	ID3D11DepthStencilState * DepthState;
	ID3D11RasterizerState * RasterizerState;
	ID3D11Buffer * ConstantBuffer;

	// The scene lands in the first.  The other two are what a chain of more than one effect uses in
	// turn, and they are always eight bit: the frame is back from half float by the time any of the
	// one pass effects sees it.
	Target SceneTarget;
	// With multisampling, the scene target's samples resolved for the passes to read; empty without.
	Target SceneResolve;
	// The scene as Occlude_Scene found it, in its format, made at that pass's first use.
	Target SceneCopy;
	// Half size, occlusion and distance: the two the blur goes between, made at that pass's first use.
	Target WorldAoTargets[2];
	Target ChainTargets[2];
	Target BloomTargets[2];

	DX11PostEffect Chain[DX11_POST_CHAIN_LIMIT];
	// What the frame's projection is using, so the occlusion pass can turn a depth back into a
	// distance.  Set by whoever knows the camera; the defaults are the game's own.
	float NearPlane;
	float FarPlane;
	unsigned ChainLength;
	unsigned Width;
	unsigned Height;
	unsigned BloomWidth;
	unsigned BloomHeight;
	unsigned TargetSamples;
	bool TargetsAreFloat;
	bool ShadersReady;
	bool Resolved;
	// Occlude_Scene ran this frame, so Finish leaves the occlusion pass out.
	bool Occluded;
	// Haze_Scene ran this frame, and the matrix it handed the pass.
	bool Hazed;
	float HazeWorldFromClip[16];
	// The haze program, compiled on first use, failed to compile; not tried again.
	bool HazeRefused;
	// The terrain's shroud texture under the haze, borrowed from its D3D9 texture each pass.
	ID3D11ShaderResourceView * HazeShroudView;
	// An Atmosphere program failed to compile: not tried again.
	bool AtmosphereRefused;
	// The Ambient Occlusion option's programs, the projection scale it was handed, and a refusal: not
	// tried again.
	ID3D11PixelShader * WorldAoShaders[WORLD_AO_PROGRAMS];
	float WorldAoProjection[4];
	bool WorldAoRefused;
	// The SunShafts option's march and composite, a refusal (not tried again), and whether it ran this frame.
	ID3D11PixelShader * SunShaftShaders[SUN_SHAFT_PROGRAMS];
	bool SunShaftRefused;
	bool Shafted;
	// Soften_Shroud's: its programs, the eased levels a texel a cell (two, read one and write the
	// other), the smoothed picture, the cell grid they were made for, how many texels a cell the
	// picture has, the seconds left before the levels stop moving, and the fog's level, below nought
	// while there is no picture.  Released with the device, not with the frame's targets, so a resize
	// keeps them.
	void Release_Shroud();
	ID3D11PixelShader * ShroudShaders[SHROUD_PROGRAMS];
	bool ShroudRefused;
	Target ShroudHeld[2];
	Target ShroudSoft;
	unsigned ShroudHeldIndex;
	unsigned ShroudCellsWidth;
	unsigned ShroudCellsHeight;
	unsigned ShroudScale;
	float ShroudSettle;
	float ShroudFogLight;
	// The picture holds fog: a cell below clear, or levels still easing out of one.
	bool ShroudFogged;
	// Shroud_Memory_Scene ran this frame.
	bool ShroudRemembered;
	// -heathaze: the splats waiting for the next Heat_Haze_Scene, the half size shift target (made
	// with the program, released with the other targets), the splat buffer the vertex program reads,
	// its programs and its additive blend.  Refused once means never tried again.
	bool Create_Heat_Haze();
	void Release_Heat_Haze();
	std::vector<float> HeatSplats;
	Target HeatTarget;
	ID3D11Buffer * HeatSplatBuffer;
	ID3D11ShaderResourceView * HeatSplatView;
	ID3D11VertexShader * HeatSplatVertexShader;
	ID3D11PixelShader * HeatSplatShader;
	ID3D11PixelShader * HeatHazeShader;
	ID3D11BlendState * HeatBlendState;
	bool HeatHazeRefused;
	char Diagnostic[192];
};

#endif // DX11POST_H
