/*
** The scene's dynamic point lights (explosions, muzzle flashes, burning wrecks and the Firelight
** option's fires) with shadows, at the Ray Traced shadow level.  Everywhere else they are
** BLAST_LIGHT_SAMPLING in the receiving programs (ffshader.h), unshadowed, so a blast behind a
** building lights the ground in front of it.  Here the opaque scene's receivers stand down for them
** (Upload_Pixel_Constants sends no lights while Ray_Lights_Pending) and, once the opaque world but
** the batched trees is down, one compute pass works the same lights out per pixel from the scene's depth with a shadow
** ray to each through the ray grid (dx11raygrid.h), then multiplies the gain onto the scene and
** clamps it as the programs' saturate did.  The pass is DX11BackendClass::Trace_Ray_Lights, whose
** half of the class is in dx11raylights.cpp; this holds its programs, textures and queries.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#ifndef DX11RAYLIGHTS_H
#define DX11RAYLIGHTS_H

#include <d3d11.h>
#include <string>

#include "ffshader.h"

class DX11BackendClass;

class DX11RayLightClass
{
	friend class DX11BackendClass;

public:
	DX11RayLightClass();
	~DX11RayLightClass() { Release(); }

	// ZHR_RT_LIGHTS=0 leaves the lights to the programs at the Ray Traced level, for a before and
	// after; a device that refused any of the pass does the same for good.
	bool Wanted() const { return EnvironmentWanted && !Refused; }
	// ZHR_RT_LIGHTS_PROBE: the pass counts what it lit and what the rays took away, for a line each
	// logic frame (Probe_Report).
	bool Probing() const { return ProbeWanted && !Refused; }

	// "; lights ..." for the RTSHADOWS line: the pass's GPU time and how many lights it took.
	std::string Report(ID3D11DeviceContext * context);
	// The last frame read back: pixels lit, pixels a ray took light from, rays and rays stopped,
	// whole screen and per 160 by 144 pixel tile.  Empty when nothing was lit.
	std::string Probe_Report(ID3D11DeviceContext * context);
	void Release();

private:
	DX11RayLightClass(const DX11RayLightClass &);
	DX11RayLightClass & operator=(const DX11RayLightClass &);

	bool Make(DX11BackendClass & backend, ID3D11Device * device, unsigned width, unsigned height);
	void Release_Sized();
	void Collect(ID3D11DeviceContext * context);

	enum { QUERY_FRAMES = 4, TILES = 256, TILE_COUNTS = 4, TILE_WIDTH = 160, TILE_HEIGHT = 144, SLOTS = BLAST_LIGHT_SLOTS };
	struct Frame
	{
		ID3D11Query * Disjoint;
		ID3D11Query * Start;
		ID3D11Query * End;
		ID3D11Buffer * Readback;	///< the probe's tile counts
		bool Issued;
		bool Probed;
		unsigned Lights;
		unsigned TilesX;
		unsigned TilesY;
	};
	struct Constants
	{
		float WorldFromClip[16];
		float Screen[4];			///< width, height and their reciprocals
		float Tuning[4];			///< lights, probe on, tiles across, tiles down
		float Position[SLOTS][4];	///< BLAST_LIGHT_FLOATS split as the pixel block splits them
		float Diffuse[SLOTS][4];
		float Ambient[SLOTS][4];
	};

	bool Refused;
	bool EnvironmentWanted;
	bool ProbeWanted;
	unsigned Width;
	unsigned Height;
	ID3D11ComputeShader * Queue;	///< the rays each pixel asks for
	ID3D11ComputeShader * Trace;	///< one ray a thread, over the queue
	ID3D11ComputeShader * Shade;	///< the lights each pixel keeps
	ID3D11PixelShader * Multiply;	///< the gain onto the scene, through LightBlend
	ID3D11PixelShader * Clamp;		///< then white under a MIN blend, where a light reached
	ID3D11BlendState * LightBlend;
	ID3D11BlendState * ClampBlend;
	ID3D11Buffer * ConstantBuffer;
	ID3D11Buffer * Jobs;
	ID3D11UnorderedAccessView * JobsWrite;
	ID3D11ShaderResourceView * JobsRead;
	ID3D11Buffer * Arguments;		///< the job count, then the trace's indirect dispatch
	ID3D11UnorderedAccessView * ArgumentsWrite;
	ID3D11ShaderResourceView * ArgumentsRead;
	ID3D11Texture2D * Blocked;		///< half size: a bit a slot, the lights a quad's rays found blocked
	ID3D11UnorderedAccessView * BlockedWrite;
	ID3D11ShaderResourceView * BlockedRead;
	ID3D11Texture2D * Texture;		///< the light per pixel, alpha one where any light reached
	ID3D11UnorderedAccessView * Write;
	ID3D11ShaderResourceView * Read;
	ID3D11Buffer * Hits;
	ID3D11UnorderedAccessView * HitsWrite;
	Frame Frames[QUERY_FRAMES];
	unsigned FrameIndex;
	// The last frame read back.
	unsigned Counts[TILES * TILE_COUNTS];
	unsigned CountsTilesX;
	unsigned CountsTilesY;
	bool CountsRead;
	unsigned LastLights;
	float LastMS;
	bool Timed;
};

#endif // DX11RAYLIGHTS_H
