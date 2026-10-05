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

	// Edge antialiasing in one pass over the finished world.  The Direct3D 9 device offers
	// multisampling and the swap chain here asks for one sample, so this is what the D3D11 frame
	// has against a jagged edge.
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

// Bloom's threshold and strength in the scene's own units, where one is white.  The game's Bloom
// option sets them every frame through W3DShaderManager, since nothing in this library may read
// GlobalData.  An intensity of zero skips the bright pass and the blur and leaves only the tone
// curve, so the half float scene still reaches eight bits.  Until somebody calls this the chain
// runs at the values it was tuned at, which is what the tests measure.
void DX11Post_Set_Bloom(float threshold, float intensity);

// Apocalypse mode's dusk: the finished world desaturated toward a grey green, a haze that thickens
// toward the top of the screen, which in this camera is the far ground, and the corners darkened.
// One more pass at the end of the chain, so it needs a chain: "-dx11post off" turns it off with the
// rest.  The game sets it every frame from the match it is drawing; off, the chain is the chain it
// always was.  W3DView calls this, nothing here knows what a game mode is.
void DX11Post_Set_Dusk(bool enabled);

// The grade itself as one HLSL function, float3 dusk_grade(float3 colour, float2 at), with at the
// place on the screen from (0,0) at the top left to (1,1) at the bottom right.  It is plain enough
// for ps_2_0 as well as ps_4_0, so the Direct3D 9 screen filter compiles this same text and the two
// devices draw the same dusk.  Its strengths are the DUSK_ constants at the top of the text.
const char * DX11Post_Dusk_Grade_Source();

class DX11PostProcessClass
{
public:
	DX11PostProcessClass();
	~DX11PostProcessClass();

	bool Initialise(DX11DeviceClass * device);
	void Shutdown();

	// An empty chain is the whole feature turned off: Scene_View gives back nothing and the device
	// keeps drawing into the swap chain the way it always has.
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
	};

	bool Create_Shaders();
	bool Create_States();
	bool Create_Target(Target & target, unsigned width, unsigned height, DXGI_FORMAT format);
	bool Create_Targets(unsigned width, unsigned height, bool floating);
	void Release_Target(Target & target);
	void Release_Targets();
	bool Ensure_Buffers();
	bool Finish(const DX11PostEffect * effects, unsigned count);
	ID3D11ShaderResourceView * Run_Bloom(ID3D11RenderTargetView * destination);
	void Draw_Pass(const PassSetup & pass);
	void Describe();

	DX11DeviceClass * Device;
	ID3D11VertexShader * VertexShader;
	ID3D11PixelShader * PixelShaders[DX11_POST_SIMPLE_EFFECTS];
	ID3D11PixelShader * ToneMapShader;
	ID3D11PixelShader * BloomExtractShader;
	ID3D11PixelShader * BloomBlurShader;
	ID3D11PixelShader * BloomCompositeShader;
	ID3D11PixelShader * DuskShader;
	ID3D11SamplerState * Sampler;
	ID3D11BlendState * BlendState;
	ID3D11DepthStencilState * DepthState;
	ID3D11RasterizerState * RasterizerState;
	ID3D11Buffer * ConstantBuffer;

	// The scene lands in the first.  The other two are what a chain of more than one effect uses in
	// turn, and they are always eight bit: the frame is back from half float by the time any of the
	// one pass effects sees it.
	Target SceneTarget;
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
	bool TargetsAreFloat;
	bool ShadersReady;
	bool Resolved;
	char Diagnostic[192];
};

#endif // DX11POST_H
