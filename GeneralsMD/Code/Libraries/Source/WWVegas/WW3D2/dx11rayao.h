/*
** The Ambient Occlusion option at the Ray Traced shadow level: the occlusion's half size term made
** of short hemisphere rays through the ray grid (dx11raygrid.h) instead of the depth taps of
** dx11post's first pass.  The blur, the upsample and the multiply onto the opaque scene stay the
** option's own, so what it darkens and when is what it always was.  The pass is
** DX11BackendClass::Trace_Ray_Occlusion, whose half of the class is in dx11rayao.cpp; this holds
** its programs, textures and queries.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#ifndef DX11RAYAO_H
#define DX11RAYAO_H

#include <d3d11.h>
#include <string>

class DX11BackendClass;
class DX11RayGridClass;

class DX11RayOcclusionClass
{
public:
	DX11RayOcclusionClass();
	~DX11RayOcclusionClass() { Release(); }

	// The rays for every half size pixel of a width by height scene, into a texture of the post
	// chain's first pass's format; NULL when the device refused any of it or ZHR_RT_AO=0 asks for the
	// screen's occlusion at this level.  The grid has to be bound by the caller (DX11RayGridClass::Bind).
	ID3D11ShaderResourceView * Trace(DX11BackendClass & backend, ID3D11Device * device, ID3D11DeviceContext * context,
		ID3D11ShaderResourceView * depth, unsigned width, unsigned height, const float world_from_clip[16],
		ID3D11ShaderResourceView * shroud, const float shroud_rect[4]);

	// Around the whole occlusion, either kind, so the report can set the two against each other.
	void Time(ID3D11Device * device, ID3D11DeviceContext * context, bool begin, bool traced);

	// "; ao ..." for the RTSHADOWS line, and under ZHR_RT_AO_PROBE the rays and what they kept by
	// screen tile.
	std::string Report(ID3D11DeviceContext * context);
	void Release();

private:
	DX11RayOcclusionClass(const DX11RayOcclusionClass &);
	DX11RayOcclusionClass & operator=(const DX11RayOcclusionClass &);

	bool Make(DX11BackendClass & backend, ID3D11Device * device, unsigned width, unsigned height);
	bool Make_Queries(ID3D11Device * device);

	enum { QUERY_FRAMES = 4, TILES = 256, TILE_COUNTS = 5, TILE_WIDTH = 160, TILE_HEIGHT = 144 };
	struct Frame
	{
		ID3D11Query * Disjoint;
		ID3D11Query * Start;
		ID3D11Query * Traced;
		ID3D11Query * End;
		ID3D11Buffer * Readback;	///< the probe's tile counts
		bool Open;
		bool Issued;
		bool WasTraced;
		bool Probed;
		unsigned TilesX;	///< the screen's tiles when it was probed
		unsigned TilesY;
	};
	struct Constants
	{
		float WorldFromClip[16];
		float Screen[4];
		float Half[4];
		float ShroudRect[4];
		float Tuning[4];
	};

	bool Refused;
	bool Wanted;
	bool ProbeWanted;
	bool ProbeTooWide;
	bool QueriesMade;
	bool QueriesRefused;
	unsigned Width;
	unsigned Height;
	ID3D11ComputeShader * Program;
	ID3D11Buffer * ConstantBuffer;
	ID3D11SamplerState * Sampler;
	ID3D11Texture2D * Texture;
	ID3D11UnorderedAccessView * Write;
	ID3D11ShaderResourceView * Read;
	ID3D11Buffer * Hits;
	ID3D11UnorderedAccessView * HitsWrite;
	Frame Frames[QUERY_FRAMES];
	unsigned FrameIndex;
	// The last frame read back, and its tiles.
	unsigned TilesX;
	unsigned TilesY;
	unsigned Counts[TILES * TILE_COUNTS];
	bool CountsRead;
	bool LastTraced;
	float WholeMS;
	float RaysMS;
};

#endif // DX11RAYAO_H
