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

// Backend-independent post chain and constant layout. The DX11-prefixed public names are kept
// for source and link compatibility; this header intentionally has no platform includes.
#ifndef POSTPROCESS_H
#define POSTPROCESS_H

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

struct PostProcessConstants
{
	// One over the size of the texture being sampled, and its size.  Per pass rather than per
	// frame, because the bloom passes do not run at the size of the window.
	float TexelSize[4];

	// The step a separable blur takes, already in texture coordinates. Haze aliases
	// this unused slot to its optional water-surface enable, tint gain and shore depth.
	float BlurDirection[4];

	// Bloom: threshold, intensity, tone knee, soft knee. Chain AO: near, far, radius, spread.
	float Tuning[4];

	// How many warps, and the width of the frame over its height.  Then per warp the centre, the
	// radius and the pull, and the ring's radius, width and strength.
	float WarpInfo[4];
	float WarpCentre[DX11_POST_WARP_LIMIT][4];
	float WarpRing[DX11_POST_WARP_LIMIT][4];

	// The grade's amount, contrast, saturation and warm/cool split.
	float Grade[4];

	// The ground haze: its top, thickness, strength and reach, its colour, and the matrix taking
	// the frame's clip space back to the world.  The haze, atmosphere and world AO programs
	// declare the extended fields in b1 with explicit register offsets. Bind this entire block
	// at both b0 and b1; do not compact b1 to just its declared fields.
	float Haze[4];
	float HazeColour[4];
	float WorldFromClip[16];
	// Atmosphere aliases Haze to Fade, HazeColour to Horizon, BlurDirection.rgb to sky tint,
	// and WorldAoProjection.xy to MapSize. Then the way toward the sun with its warmth.
	float HazeSun[4];
	// The shroud light's world rectangle (origin, one over size).
	float HazeShroudRect[4];
	// How the scene's projection scales x and y, for the occlusion in world units to find its reach
	// on the screen.  Only that program declares it.
	float WorldAoProjection[4];
};

// The defaults shared by the native renderers. These are the existing DX11 tuning values.
namespace PostProcess
{
// Bloom runs at a quarter of the window on each axis.  A glow is low frequency by definition, so
// the resolution it is computed at is the one thing about it nobody can see, and a quarter costs a
// sixteenth of the samples.
const unsigned BLOOM_DIVISOR = 4;

// Pairs of blur passes, one horizontal and one vertical, the n-th pair stepping n texels between
// its taps.  Two reach about fifteen pixels of the finished frame; the Glow option's Medium asks
// for three, High and Ultra for four, about twenty five and thirty five.  More than four and a
// fire's halo covers the units fighting in it.
const unsigned BLOOM_BLUR_PASSES = 2;
const unsigned BLOOM_BLUR_PASS_LIMIT = 4;

// How far past the threshold the bright pass takes to reach full strength.  A pixel just over it
// keeps the square of its excess, so a flame flickering across the threshold fades its glow in
// and out instead of switching it.
const float BLOOM_KNEE = 0.5f;

// What counts as bright enough to bleed, what the bleed is worth when it is added back, and where
// the tone curve starts bending.
//
// A threshold of one means brighter than white, which is a thing an eight bit target could not have
// said at all, and it was worth checking that the game ever gets there rather than assuming it.
// Eight inferno cannons firing into a column does not: at a threshold of one the frame comes back
// unchanged even with the intensity at four, because a fire a few pixels across never stacks enough
// additive particles to pass white.  Thirty of them does, and at the same threshold of one the
// fireballs come back with white centres and a glow on the ground around them.  So the float target
// earns its place, and the threshold stays where it is: a lower one blooms pale buildings and the
// sky, which is the look this is trying not to have.
//
// An intensity of 1.5 against a quiet frame with nothing burning costs half a level a channel,
// which is the cost of having this on when there is nothing for it to do.
//
// Those two are the defaults.  The game replaces them every frame from its Glow option, which
// also raises what an additive draw is worth (DX11Post_Additive_Gain): that, and not a lower
// threshold, is what gets fire and lasers past white while the art stays under it.
const float BLOOM_THRESHOLD = 1.0f;
const float BLOOM_INTENSITY = 1.5f;
const float TONE_CURVE_KNEE = 0.8f;

// How far the grade bends the picture at full strength: the power the S raises each side of its
// pivot to, less one, what is left of the colour around its grey, and how far a channel moves cool
// for each step under the pivot (a step over it moves nothing).
const float GRADE_CONTRAST = 0.18f;
const float GRADE_SATURATION = 0.86f;
const float GRADE_SPLIT = 0.05f;

// How dark a fully occluded pixel goes, how far a neighbour may be in front before it counts as a
// different object rather than a corner, and how wide the ring reaches at one unit of depth.  The
// clip planes come from the frame itself.
const float AO_STRENGTH = 0.45f;
const float AO_RADIUS = 14.0f;
const float AO_SPREAD = 26.0f;

}

#endif // POSTPROCESS_H
