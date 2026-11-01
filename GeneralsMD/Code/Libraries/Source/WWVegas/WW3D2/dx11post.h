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

class DX11DeviceClass;

enum DX11PostEffect
{
	// Straight copy, which is what proves the plumbing rather than the shader: with this in the
	// chain the frame that reaches the screen has been through the offscreen texture and the full
	// screen pass and has to come out unchanged.
	DX11_POST_COPY,

	// Edge antialiasing in one pass over the finished world, what the D3D11 frame has against a
	// jagged edge without multisampling.  A multisampled scene leaves it out of the chain: the
	// edges are already resolved, and FXAA over them only blurs the textures.
	DX11_POST_FXAA,

	// Unsharp mask.  The game's textures are 2003 vintage and a 1080p window magnifies them; this
	// is a taste knob and it is not in the default chain.
	DX11_POST_SHARPEN,

	// Ambient occlusion off the frame's own depth: the darkening a corner traps, which is what puts
	// a building on the ground rather than over it.  Reads the depth buffer at t1.
	DX11_POST_AO,

	// Bright things bleed light, and the scene is kept in half floats so that "bright" means
	// brighter than white rather than close to it.  Several passes rather than one, and it takes
	// the frame from half float back to eight bits, so it can only be the first effect in a chain.
	DX11_POST_BLOOM
};

// The effects that are one pass over one texture.  Bloom is not one of them and is handled apart.
const unsigned DX11_POST_SIMPLE_EFFECTS = DX11_POST_BLOOM;

// Room for every effect at once.  A chain longer than the effects that exist repeats one, which is
// allowed and is how sharpening twice is asked for.
const unsigned DX11_POST_CHAIN_LIMIT = 4;

// Reads "fxaa", "bloom,fxaa", "copy", "off" or "none".  Returns false and leaves count at zero for
// a name it does not know and for a chain with bloom anywhere but first, so a misspelt or
// impossible switch turns the chain off loudly rather than silently drawing something else.
// Separated from the class so it can be tested without a device.
bool DX11Post_Parse_Chain(const char * text, DX11PostEffect effects[DX11_POST_CHAIN_LIMIT],
	unsigned & count);

// The name a parsed effect goes back to, for the report line.
const char * DX11Post_Effect_Name(DX11PostEffect effect);

// Bloom's threshold and strength in the scene's own units, where one is white, what an additive
// draw into the scene is multiplied by (DX11BackendClass::Set_Additive_Gain) and how many pairs
// of blur passes spread the glow, each pair a step wider than the one before.  The game's Glow
// option sets them every frame through W3DShaderManager, since nothing in this library may read
// GlobalData.  An intensity of zero skips the bright pass and the blur and leaves only the tone
// curve, so the half float scene still reaches eight bits, and it takes the gain back to one.
// Until somebody calls this the chain runs at the values it was tuned at, which is what the tests
// measure.
void DX11Post_Set_Bloom(float threshold, float intensity, float additive_gain, unsigned blur_passes);

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
struct DX11PostWarp
{
	float CentreX;
	float CentreY;
	float Radius;
	float Pull;
	float RingRadius;
	float RingWidth;
	float RingStrength;
	// The ring is the outline of a dome standing on the ground, not a circle on the glass.  Above
	// the centre it is the dome's own round silhouette, rising as high as it is wide; below it, it
	// is the dome's footprint on the ground, which the camera's pitch flattens.  This is how far it
	// is flattened there: the footprint's depth on screen over its width, one looking straight down.
	float RingSquash;
};

// Four blasts on one screen at once is already more than anyone can read.
const unsigned DX11_POST_WARP_LIMIT = 4;

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
	bool World_Occlude_Scene(ID3D11ShaderResourceView * depth, const float world_from_clip[16],
		float projection_x, float projection_y);
	bool Wants_World_Ao_Programs() const;
	const char * World_Ao_Program_Source(unsigned index) const;
	void Load_World_Ao_Program(unsigned index, const void * bytecode, SIZE_T size);
	void Compile_World_Ao_Programs();
	static const unsigned WORLD_AO_PROGRAMS = 3;

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
	};

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
	char Diagnostic[192];
};

#endif // DX11POST_H
