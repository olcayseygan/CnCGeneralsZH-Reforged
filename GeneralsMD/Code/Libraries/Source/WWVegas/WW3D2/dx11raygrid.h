/*
** Rays in any direction for the Ray Traced shadow level: a uniform 3D grid over the triangles the
** sun's caster pass streams out (DX11BackendClass::Begin_Ray_Capture), built on the GPU every frame
** that captured them, and one HLSL text that walks it.  The sun's own trace needs none of this, since
** a ray to the sun visits one column of the backend's 2D grid; a ray to a point light or across a
** hemisphere crosses columns, and a column holds everything stacked along the sun's ray.
**
** The grid lives in the sun's clip space, where the triangles already are.  That space is an affine
** map of the world (an orthographic view), so a world ray taken into it keeps its parameter: a hit
** at t is t world units along the world ray, and the grid never has to know where the world is.  Its
** box is the triangles' own bounds inside the sun's box, found by a reduction, and its cells are cut
** near cubic in world units under a fixed budget, so the whole-map box and a close zoom both fill it.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#ifndef DX11RAYGRID_H
#define DX11RAYGRID_H

#include <d3d11.h>
#include <string>

class DX11BackendClass;

class DX11RayGridClass
{
public:
	DX11RayGridClass();
	~DX11RayGridClass() { Release(); }

	// The HLSL a pass includes to trace through the grid: ray_occluded(origin, direction, reach)
	// with a world origin, a unit world direction and a reach in world units, true at the first
	// caster or ground in the way (any hit, no order), a cut caster only where its texel passes its
	// alpha test.  ray_hits_casters and ray_hits_ground are its two halves.  It reads t16 to t23
	// and b1, which Bind sets on the compute stage.
	static std::string Trace_Program();

	// Bins the pass's triangles: the backend's stream-out buffer read as float4 corners, three a
	// triangle, the fourth float the pass's stamp.  Made on first use; a refusal leaves it off for
	// good and says so in Report.
	void Build(DX11BackendClass & backend, ID3D11Device * device, ID3D11DeviceContext * context,
		ID3D11ShaderResourceView * corners, unsigned stamp, unsigned triangle_slots,
		const float sun_from_world[16]);
	// The cut casters' half: a float4 a corner beside the corners (texture coordinates, slice,
	// alpha reference) and the slices of their alpha, read only for a triangle whose stamp is
	// negated.  Borrowed like the corners; NULL for none.
	void Set_Cut(ID3D11ShaderResourceView * coordinates, ID3D11ShaderResourceView * alpha)
	{ CutCoordinates = coordinates; CutAlpha = alpha; }
	// The box in the sun's clip space the grid keeps to, low then high corner, for the next Build;
	// the sun's whole box (Keep_Whole) until told otherwise.  A triangle wholly outside it is left
	// out and the grid's bounds are clamped to it, so casters far from anything the rays start on
	// (trees streamed the length of the sun's column) cost no cells.
	void Set_Keep(const float low[3], const float high[3]);
	void Keep_Whole();
	// True from Build until the next caster pass starts streaming over the corners it lists
	// (Invalidate, from Begin_Ray_Capture), so a frame that captured and did not build traces nothing.
	bool Built() const { return BuiltThisFrame; }
	void Invalidate() { BuiltThisFrame = false; }

	// What a later pass binds before it dispatches a program made with Trace_Program, and after.
	// ground is the backend's terrain numbers (cells per world unit, border cells, world units per
	// height byte, highest ground); heights NULL leaves the ground out of every ray.
	void Bind(ID3D11DeviceContext * context, ID3D11ShaderResourceView * heights, const float ground[4]);
	void Unbind(ID3D11DeviceContext * context);

	// ZHR_RT_GRID: every pixel's ray toward the sun through this grid against the backend's 2D grid
	// (old_grid: corners, starts, counts, items, block starts, as its trace binds them), then two
	// random hemisphere rays a pixel, 40 and 150 world units, counted for the report.
	bool Probing() const { return ProbeWanted && BuiltThisFrame; }
	void Probe(ID3D11DeviceContext * context, ID3D11ShaderResourceView * depth, const float world_from_clip[16],
		unsigned width, unsigned height, const float sun[4], float normal_offset, float depth_bias,
		unsigned old_reference_slots, ID3D11ShaderResourceView * heights, const float ground[4],
		ID3D11ShaderResourceView * const old_grid[5]);

	// "; grid ..." for the RTSHADOWS line: the grid's shape and references, its GPU time, and the
	// probe's counts once one has come back.
	std::string Report(ID3D11DeviceContext * context);
	void Release();

private:
	DX11RayGridClass(const DX11RayGridClass &);
	DX11RayGridClass & operator=(const DX11RayGridClass &);

	bool Make(DX11BackendClass & backend, ID3D11Device * device);
	void Upload(ID3D11DeviceContext * context);

	enum { BUFFERS = 7, PROGRAMS = 8, QUERY_FRAMES = 4, PROBE_COUNTS = 13, BUILD_STEPS = 4, BOUND_READS = 8 };
	enum { BOUNDS, SHAPE, COUNTS, STARTS, BLOCKS, ITEMS, HITS };
	struct Frame
	{
		ID3D11Query * Disjoint;
		ID3D11Query * Start;
		ID3D11Query * Steps[BUILD_STEPS - 1];	///< after the bounds and shape, the count, the scan
		ID3D11Query * Binned;
		ID3D11Query * ProbeStart;
		ID3D11Query * SunProbed;
		ID3D11Query * HemisphereProbed;
		ID3D11Buffer * Readback;	///< the shape, the references and the probe's counts
		bool Open;
		bool Issued;
		bool Probed;
	};
	struct Constants
	{
		float SunFromWorld[16];
		float WorldFromClip[16];
		float Screen[4];
		float Sun[4];
		float Ground[4];
		float Tuning[4];
		float Build[4];
		float Probe[4];
		float KeepLow[4];
		float KeepHigh[4];
	};

	bool Refused;
	bool ProbeWanted;
	bool BuiltThisFrame;
	unsigned FrameIndex;
	unsigned Seed;
	Constants Block;
	ID3D11ShaderResourceView * Corners;		///< borrowed from the backend, valid while its rays are made
	ID3D11ShaderResourceView * CutCoordinates;
	ID3D11ShaderResourceView * CutAlpha;
	ID3D11Buffer * Buffers[BUFFERS];
	ID3D11UnorderedAccessView * Writes[BUFFERS];
	ID3D11ShaderResourceView * Reads[BUFFERS];
	ID3D11ComputeShader * Programs[PROGRAMS];
	ID3D11Buffer * ConstantBuffer;
	ID3D11Buffer * PassBuffers[2];			///< b2: counting, then filling
	ID3D11Buffer * Arguments;				///< the per-triangle dispatches' groups, from this pass's triangle count
	ID3D11UnorderedAccessView * ArgumentsWrite;
	Frame Frames[QUERY_FRAMES];
	// The last frame read back.
	float Shape[12];
	unsigned References;
	unsigned Counts[PROBE_COUNTS];
	bool CountsRead;
	float BuildMS;
	float StepMS[BUILD_STEPS];
	unsigned Triangles;
	float SunProbeMS;
	float HemisphereProbeMS;
};

#endif // DX11RAYGRID_H
