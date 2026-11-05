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
 * T1c, defect 17: the water grid steps once per logic frame, however many logic frames a pass runs.
 *
 * WaterGridMotion::updateForLogicFrame is the step moved out of WaterRenderObjClass::update; GameLogic::
 * update calls it (through TerrainLogic's WaterGrid::update) at the top of every logic frame.  This plays
 * Tests/water_grid_scenario.h through it that way, under EA's loop and three shapes of the fork's
 * catch-up, and requires every one to print what the ORIGINAL code printed under EA's loop.
 *
 * The golden is Tests/water_grid_oracle.cpp's first line: the original text, taken from the tree before T1 moved it, by
 * Tools/water_grid_oracle_extract.py and stepped on the client pass as W3DTerrainVisual::update did,
 * one pass per logic frame.  Identical from mingw-w64 under Wine, native arm64 and x86_64 under Rosetta.
 * The armed control is the oracle's other lines: the original under the catch-up schedules below, which
 * all differ from it (and whose grid is still moving at frame 240, where EA's has settled by 183).
 *
 * What it cannot see: that GameLogic::update makes the call, and makes it at the top of the frame - that
 * is the engine's loop, which needs the W3D device to run (read in the source, T1); the
 * client passes' own reads of the grid, which now see it a step earlier in a pass, and draw nothing
 * the simulation reads.
 *
 * Since logic runs at 60 frames a second the step is no longer the original's bits: its gravity is
 * per 60 Hz frame squared, its damping the square root of EA's and its at-rest velocity half of it,
 * so the original's 30 Hz line can no longer be the golden. The catch-up tests hold every schedule
 * to the moved code's own one-pass-per-frame line, computed in the same run, which is the claim T1c
 * makes. EA_LOOP_GOLDEN pins that line against a change to the step, and is that line as a run of
 * this binary prints it ("moved, passes of 1: ..."): regenerate it from a run whenever the step's
 * constants or the frame rate change on purpose.
 */

#include "test_harness.h"

#include "PreRTS.h"
#include "Common/GameCommon.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameLogic/WaterGridMotion.h"
#include "Platform/WaterFoamParticlePolicy.h"
#include <limits>

#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>

#include "water_grid_scenario.h"

namespace {

// The moved code at 60 Hz under one pass per logic frame, as test_water_grid prints it. The 30 Hz
// original's line (water_grid_oracle, "original passes of 1") was "frames 240 all 96559c89 last a7eea444
// moving 183". This is the "moved, passes of 1" line taken from a run of the 60 Hz step.
const char *const EA_LOOP_GOLDEN = "frames 240 all 5c9f1917 last ac3c3fa9 moving 240";

void boot()
{
	static bool booted = false;
	if (booted)
		return;
	booted = true;
	initMemoryManager();
	TheNameKeyGenerator = NEW NameKeyGenerator;
	TheNameKeyGenerator->init();
	TheWritableGlobalData = NEW GlobalData;
	TheWritableGlobalData->m_gravity = ConvertAccelerationInSecsToFrames( -64.0f );	// GameData.ini's
}

// The engine as it is now: nothing on the client pass, the step at the top of each logic frame.
struct MovedEngine
{
	typedef WaterGridMotion::MeshPoint Point;
	std::vector<Point> points;
	Bool moving;
	int callsPerFrame;
	UnsignedInt lastFrame;
	MovedEngine( int calls = 1 ) : points( water_grid::POINTS_X * water_grid::POINTS_Y ), moving( FALSE ), callsPerFrame( calls ), lastFrame( 0 ) {}
	Point *mesh( void ) { return &points[0]; }
	Bool &inMotion( void ) { return moving; }
	void clientPass( UnsignedInt ) {}
	void logicFrameTop( UnsignedInt frame )
	{
		for (int i = 0; i < callsPerFrame; ++i)
			WaterGridMotion::updateForLogicFrame( frame, lastFrame, TRUE, moving, &points[0], water_grid::CELLS_X, water_grid::CELLS_Y );
	}
};

unsigned nextFirstFrame()
{
	static unsigned run = 0;
	return 1 + 1000 * run++;	// the step's last-frame gate is a static: no run starts where one ended
}

// The moved code under EA's loop, one pass per logic frame: what every other schedule has to print.
std::string eaLoopLine( void )
{
	MovedEngine engine;
	return water_grid::run( engine, std::vector<int>( 1, 1 ), nextFirstFrame() );
}

}  // namespace

TEST(water_grid_scenario_uses_the_engines_gravity)
{
	// the scenario's gravity is the oracle's, per 30 Hz frame squared; the engine's is per 60 Hz frame
	// squared, a quarter of it, and exactly so: halving 1/30 only moves the exponent
	boot();
	const Real engine = ConvertAccelerationInSecsToFrames( -64.0f );
	const float scenario = ConvertRetailPerFrameSquared( water_grid::shippedGravity() );
	CHECK( memcmp( &engine, &scenario, sizeof( engine ) ) == 0 );
}

TEST(water_grid_steps_as_eas_loop_did_under_eas_loop)
{
	boot();
	const std::string line = eaLoopLine();
	printf( "  moved, passes of 1: %s\n", line.c_str() );
	CHECK_STR( line.c_str(), EA_LOOP_GOLDEN );
}

TEST(water_grid_k_logic_frames_in_one_pass_equal_k_single_passes)
{
	boot();
	const std::string eaLoop = eaLoopLine();
	const std::vector<std::vector<int> > schedules = water_grid::schedules();
	for (size_t s = 0; s < schedules.size(); ++s)
	{
		MovedEngine engine;
		const std::string line = water_grid::run( engine, schedules[s], nextFirstFrame() );
		printf( "  moved, passes of %s: %s\n", water_grid::describe( schedules[s] ).c_str(), line.c_str() );
		CHECK_STR( line.c_str(), eaLoop.c_str() );
	}
}

TEST(water_grid_a_second_call_for_the_same_frame_does_not_step_again)
{
	/* A frozen or held logic frame calls GameLogic::update again without advancing the frame; the
		 gate, EA's own, keeps it to one step, as one client pass per unchanged frame did. */
	boot();
	const std::string eaLoop = eaLoopLine();
	MovedEngine engine( 3 );
	const std::string line = water_grid::run( engine, std::vector<int>( 1, 2 ), nextFirstFrame() );
	CHECK_STR( line.c_str(), eaLoop.c_str() );
}

TEST(water_grid_unstepped_is_not_the_golden)
{
	// So the reference means something: with no step at all the result is different
	boot();
	const std::string eaLoop = eaLoopLine();
	struct Unstepped : MovedEngine { void logicFrameTop( UnsignedInt ) {} } still;
	const std::string line = water_grid::run( still, std::vector<int>( 1, 1 ), nextFirstFrame() );
	CHECK( line != eaLoop );
}

#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterPerformanceSettings.h"
#include "Platform/WaterProjectedMesh.h"
#include "Platform/WaterBowRefinement.h"
#include "WaterBowShoulderTest.h"
#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterVesselMotion.h"
#include "Platform/WaterFoamHistory.h"
#include "Platform/WaterFoamWindow.h"
#include "Platform/WaterHullContact.h"
#include "Platform/WaterHullPoseCache.h"
#include "Platform/WaterObstacleIndex.h"
#include "Platform/WaterOpticalContext.h"
#include <cmath>

namespace {
WaterOpticalContext::Polygon opticalRectangle(float x0,float y0,float x1,float y1,float level=4) {
	return {{{x0,y0,level},{x1,y0,level},{x1,y1,level},{x0,y1,level}}};
}
}

TEST(water_optical_context_keeps_enclosed_pools_clear_inside_an_ocean_polygon)
{
	using namespace WaterOpticalContext;
	const int width=50,height=40,border=2;
	std::vector<uint8_t> heights(width*height,20);
	for (int y=0;y<height;++y) for (int x=0;x<18;++x) heights[y*width+x]=0;
	for (int y=10;y<31;++y) for (int x=28;x<47;++x) heights[y*width+x]=0;
	std::vector<WaterOpticalContext::Polygon> polygons={opticalRectangle(-100,-100,600,500)};
	Field field;
	CHECK(Build(heights.data(),width,height,border,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[20*width+4],255);
	CHECK_EQ(field.ocean[20*width+37],0); // Broad, wet pool still disconnected from the coast.
	CHECK_EQ(field.ocean[20*width+22],0);
	CHECK_EQ(field.ocean[20*width+15],0); // Within 30 world units of shore.
	CHECK(field.ocean[20*width+12]>0 && field.ocean[20*width+12]<255);
	for (int x=1;x<18;++x) CHECK(field.ocean[20*width+x]<=field.ocean[20*width+x-1]);
}

TEST(water_optical_context_polygon_union_cannot_invent_a_water_connection)
{
	using namespace WaterOpticalContext;
	const int side=60;
	std::vector<uint8_t> heights(side*side,0);
	Field field;
	std::vector<WaterOpticalContext::Polygon> polygons={opticalRectangle(-100,-100,180,700),opticalRectangle(300,100,550,500)};
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[30*side+3],255);
	CHECK_EQ(field.ocean[30*side+42],0); // Low terrain between polygons is not authored water.
	CHECK_EQ(field.ocean[30*side+24],0);
	polygons.push_back(opticalRectangle(100,100,400,500,9));
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[30*side+42],0); // A different level cannot supply the missing connection.
	for (Point& p:polygons.back().points) p.z=4;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[30*side+42],255); // Same-level overlapping polygons join correctly.
}

TEST(water_optical_context_uses_the_playable_boundary_and_has_no_texture_edge_shore)
{
	using namespace WaterOpticalContext;
	const int side=40,border=5;
	std::vector<uint8_t> heights(side*side,0);
	Field field;
	std::vector<WaterOpticalContext::Polygon> polygons={opticalRectangle(-100,-100,500,500)};
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[0],255);
	CHECK_EQ(field.ocean[side*side-1],255);
	polygons={opticalRectangle(-50,-50,340,340)}; // Exact native grid bounds, both orientations.
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	for (uint8_t value:field.ocean) CHECK_EQ(value,255);
	std::reverse(polygons[0].points.begin(),polygons[0].points.end());
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	for (uint8_t value:field.ocean) CHECK_EQ(value,255);
	// Restrict water to the padded border: that alone does not mark it as ocean.
	polygons={opticalRectangle(-100,-100,-10,500)};
	CHECK(Build(heights.data(),side,side,border,10,1,4,polygons,field));
	for (uint8_t value:field.ocean) CHECK_EQ(value,0);
}

TEST(water_optical_context_concave_polygon_preserves_its_dry_notch)
{
	using namespace WaterOpticalContext;
	const int side=60;
	std::vector<uint8_t> heights(side*side,0);
	std::vector<WaterOpticalContext::Polygon> polygons={{{{-10,-10,4},{600,-10,4},{600,600,4},{400,600,4},
		{400,200,4},{200,200,4},{200,600,4},{-10,600,4}}}};
	Field field;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[40*side+30],0);
	CHECK_EQ(field.ocean[10*side+30],255);
	CHECK_EQ(field.ocean[40*side+5],255);
	CHECK_EQ(field.ocean[40*side+55],255);
}

TEST(water_optical_context_refreshes_terrain_level_dimensions_and_polygon_geometry)
{
	using namespace WaterOpticalContext;
	const int side=40;
	std::vector<uint8_t> heights(side*side,20);
	for (int y=10;y<31;++y) for (int x=10;x<31;++x) heights[y*side+x]=0;
	std::vector<WaterOpticalContext::Polygon> polygons={opticalRectangle(-100,-100,500,500)};
	Field field;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[20*side+20],0);
	for (int y=10;y<31;++y) for (int x=0;x<10;++x) heights[y*side+x]=0;
	CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
	CHECK_EQ(field.ocean[20*side+20],255);
	CHECK(Build(heights.data(),side,side,0,10,1,8,polygons,field));
	CHECK_EQ(field.ocean[20*side+20],0);
	const std::vector<WaterOpticalContext::Polygon> before=polygons;
	CHECK(SamePolygons(before,polygons));
	polygons[0].points[0].z=8;
	CHECK(!SamePolygons(before,polygons));
	polygons=before; polygons[0].points[0].x+=1;
	CHECK(!SamePolygons(before,polygons));
	CHECK(Build(heights.data(),20,20,1,10,1,4,before,field));
	CHECK_EQ(field.ocean.size(),400);
	CHECK_EQ(field.shore.size(),400);
}

TEST(water_optical_context_bounds_and_shore_fade)
{
	using namespace WaterOpticalContext;
	CHECK_EQ(Offshore(0),0);
	CHECK_EQ(Offshore(30),0);
	CHECK(std::abs(Offshore(65)-0.5f)<0.00001f);
	CHECK_EQ(Offshore(100),1);
	CHECK_EQ(Offshore(1000),1);
	std::vector<uint8_t> heights(16,0);
	std::vector<WaterOpticalContext::Polygon> polygons={opticalRectangle(-10,-10,100,100)};
	Field field;
	CHECK(Build(heights.data(),4,4,0,10,1,4,polygons,field));
	CHECK(!Build(heights.data(),MaxDimension+1,2,0,10,1,4,polygons,field));
	CHECK(field.ocean.empty() && field.shore.empty());
	CHECK(!Build(heights.data(),2048,2048,0,10,1,4,polygons,field));
	CHECK(!Build(heights.data(),4,4,2,10,1,4,polygons,field));
	CHECK(!Build(heights.data(),4,4,0,0,1,4,polygons,field));
	CHECK(!Build(NULL,4,4,0,10,1,4,polygons,field));
}

TEST(water_surface_analytic_normal_is_derivative_of_displaced_height)
{
	using namespace WaterSurfaceModel;
	for (int t=0;t<4;++t) for (int i=0;i<20;++i) {
		const float x=2.31f*i,y=-1.57f*i,time=0.73f*t,epsilon=0.002f;
		const Sample sample=Evaluate(x,y,time);
		const float dx=(Evaluate(x+epsilon,y,time).height-Evaluate(x-epsilon,y,time).height)/(2*epsilon);
		const float dy=(Evaluate(x,y+epsilon,time).height-Evaluate(x,y-epsilon,time).height)/(2*epsilon);
		CHECK(std::abs(sample.dx-dx)<0.003f);
		CHECK(std::abs(sample.dy-dy)<0.003f);
		CHECK(std::abs(sample.height)<MaxDisplacement);
	}
}

TEST(water_surface_modes_evolve_and_all_freeze_controls_hold_time)
{
	using namespace WaterSurfaceModel;
	// A single location can be at the same height at two different phases. Check the field's
	// motion energy instead, so destructive interference at one point is not a false failure.
	double heightChange=0, slopeChange=0;
	for (int i=0;i<16;++i) {
		const Sample a=Evaluate(12+i*3.7f,34-i*1.3f,0),b=Evaluate(12+i*3.7f,34-i*1.3f,1);
		heightChange+=(a.height-b.height)*(a.height-b.height);
		slopeChange+=(a.dx-b.dx)*(a.dx-b.dx)+(a.dy-b.dy)*(a.dy-b.dy);
	}
	CHECK(heightChange>0.1);
	CHECK(slopeChange>0.1);
	CHECK(AdvanceTime(2,0.1f,true,false,false)==2);
	CHECK(AdvanceTime(2,0.1f,false,true,false)==2);
	CHECK(AdvanceTime(2,0.1f,false,false,true)==2);
	CHECK(AdvanceTime(2,30,false,false,false)==2.1f);
	CHECK(AdvanceTime(2,-1,false,false,false)==2);
	const float resumed=AdvanceTime(2,0.02f,false,false,false);
	CHECK(resumed>2 && resumed<2.1f);
	std::string shader;
	AppendHLSL(shader);
	CHECK(shader.find("float3 waterWave")!=std::string::npos);
	CHECK(shader.find("value.yz")!=std::string::npos);
}

TEST(water_surface_shores_limit_render_displacement_without_changing_water_level)
{
	using namespace WaterSurfaceModel;
	CHECK(ShoreDamping(-10)==0);
	CHECK(ShoreDamping(0)==0);
	CHECK(ShoreDamping(9)==1);
	CHECK(ShoreDamping(100)==1);
	for (int depth=0;depth<90;++depth) CHECK(ShoreDamping(depth*0.1f)<=ShoreDamping((depth+1)*0.1f));
}

TEST(water_surface_shared_edges_weld_in_reverse_and_move_continuously_with_camera)
{
	using namespace WaterSurfaceModel;
	const Point a={-200,17,12},b={2100,533,12};
	const Point cameras[]={{500,200,110},{-100,-300,100},{3500,700,1000}};
	for (const Point &camera:cameras) for (int cells=1;cells<=MeshCells;cells*=2) for (int i=0;i<=cells;++i) {
		const float t=float(i)/cells;
		const Point forward=EdgePoint(a,b,t,camera),reverse=EdgePoint(b,a,1-t,camera);
		CHECK(memcmp(&forward,&reverse,sizeof(Point))==0);
		Point moved=camera;moved.x+=0.01f;
		const Point next=EdgePoint(a,b,t,moved);
		CHECK(std::abs(next.x-forward.x)<0.02f);
		CHECK(std::abs(next.y-forward.y)<0.02f);
	}
}

TEST(water_surface_mesh_stays_in_convex_patch_and_triangle_indices_are_bounded)
{
	using namespace WaterSurfaceModel;
	const Point corners[4]={{0,0,12},{600,10,12},{520,300,12},{-100,270,12}};
	const Point cameras[]={{0,0,1},{300,100,30},{-1000,200,100},{10000,-4000,1200}};
	for (const Point &camera:cameras) {
		std::vector<Point> mesh(MeshVertices);
		for (int j=0;j<=MeshCells;++j) for (int i=0;i<=MeshCells;++i) {
			const float u=float(i)/MeshCells,v=float(j)/MeshCells;
			const Point edges[4]={EdgePoint(corners[0],corners[1],u,camera),EdgePoint(corners[1],corners[2],v,camera),
				EdgePoint(corners[3],corners[2],u,camera),EdgePoint(corners[0],corners[3],v,camera)};
			const Point p=PatchPoint(corners,edges,u,v);
			mesh[j*(MeshCells+1)+i]=p;
			CHECK(std::abs(p.z-12)<0.0001f);
			for (int edge=0;edge<4;++edge) {
				const Point &a=corners[edge],&b=corners[(edge+1)%4];
				CHECK((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x)>-0.05f);
			}
		}
		for (int j=0;j<MeshCells;++j) for (int i=0;i<MeshCells;++i) {
			const int a=j*(MeshCells+1)+i,b=a+1,c=a+MeshCells+1,d=c+1;
			CHECK(a>=0 && b<MeshVertices && c<MeshVertices && d<MeshVertices);
			const Point &p=mesh[a],&q=mesh[b],&r=mesh[d],&s=mesh[c];
			CHECK((q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x)>0);
			CHECK((r.x-p.x)*(s.y-p.y)-(r.y-p.y)*(s.x-p.x)>0);
		}
	}
	for (int patches=1;patches<=2000;++patches) {
		const int cells=CellsForPatches(patches);
		CHECK(cells>=1 && cells<=MeshCells);
		CHECK(patches*(cells+1)*(cells+1)<=VertexBudget);
		int emitted=0;
		for (int row=0;row<cells;row+=IndexBatchRows(cells)) {
			const int rows=std::min(IndexBatchRows(cells),cells-row);
			CHECK(rows*cells*6<=65535);
			emitted+=rows*cells*6;
		}
		CHECK(emitted==cells*cells*6);
	}
}

TEST(water_surface_cannot_be_replaced_by_uniform_texture_advection)
{
	using namespace WaterSurfaceModel;
	// Fit the best possible single scrolling velocity to the temporal derivative. A static
	// translated height map has zero residual; dispersive independently directed modes do not.
	double xx=0,xy=0,yy=0,xt=0,yt=0;
	struct Derivative { double x,y,t; } values[64];
	for (int i=0;i<64;++i) {
		const float x=float(i%8)*7.13f,y=float(i/8)*9.77f,t=1.37f,epsilon=0.002f;
		const Sample sample=Evaluate(x,y,t);
		Derivative &v=values[i];v.x=sample.dx;v.y=sample.dy;
		v.t=(Evaluate(x,y,t+epsilon).height-Evaluate(x,y,t-epsilon).height)/(2*epsilon);
		xx+=v.x*v.x;xy+=v.x*v.y;yy+=v.y*v.y;xt+=v.x*v.t;yt+=v.y*v.t;
	}
	const double determinant=xx*yy-xy*xy;
	CHECK(determinant>0.001);
	const double vx=(xt*yy-yt*xy)/determinant,vy=(yt*xx-xt*xy)/determinant;
	double residual=0,energy=0;
	for (const Derivative &v:values) { const double e=v.t-vx*v.x-vy*v.y;residual+=e*e;energy+=v.t*v.t; }
	CHECK(residual/energy>0.2);
}

namespace {
struct LODTestVertex {
	WaterSurfaceModel::Point position;
	float attenuation,base;
};
LODTestVertex testLODMorph(const LODTestVertex &fine,const LODTestVertex &a,const LODTestVertex &b,float amount)
{
	using namespace WaterSurfaceModel;
	return {{MorphCoordinate(fine.position.x,a.position.x,b.position.x,amount),
		MorphCoordinate(fine.position.y,a.position.y,b.position.y,amount),
		MorphCoordinate(fine.position.z,a.position.z,b.position.z,amount)},
		MorphCoordinate(fine.attenuation,a.attenuation,b.attenuation,amount),MorphCoordinate(fine.base,a.base,b.base,amount)};
}
std::vector<LODTestVertex> testLODGrid(int cells,float amount,bool ocean=false)
{
	using namespace WaterSurfaceModel;
	const Point corners[4]={{0,0,12},{600,10,12},{520,300,12},{-100,270,12}},camera={180,75,200};
	std::vector<LODTestVertex> grid((cells+1)*(cells+1));
	for (int j=0;j<=cells;++j) for (int i=0;i<=cells;++i) {
		const float u=float(i)/cells,v=float(j)/cells;
		const Point edges[4]={EdgePoint(corners[0],corners[1],u,camera),EdgePoint(corners[1],corners[2],v,camera),
			EdgePoint(corners[3],corners[2],u,camera),EdgePoint(corners[0],corners[3],v,camera)};
		Point p=PatchPoint(corners,edges,u,v);
		const Sample blend={ocean?0.5f+0.0005f*p.x:0.0f,ocean?0.0005f:0.0f,0};
		const float attenuation=TerrainDamping(0.05f*(p.y+20),blend.height),base=p.z;
		p.z+=attenuation*EvaluateBlended(p.x,p.y,1.73f,blend).height;
		grid[j*(cells+1)+i]={p,attenuation,base};
	}
	if (amount>0) for (int j=0;j<=cells;++j) for (int i=0;i<=cells;++i) {
		if (((i|j)&1)==0) continue;
		const CoarsePair pair=CoarsePairForVertex(i,j,cells);
		grid[j*(cells+1)+i]=testLODMorph(grid[j*(cells+1)+i],grid[pair.first],grid[pair.second],amount);
	}
	return grid;
}
LODTestVertex testLODSample(const std::vector<LODTestVertex> &grid,int cells,float u,float v)
{
	const int i=std::min(int(u*cells),cells-1),j=std::min(int(v*cells),cells-1),side=cells+1;
	const float x=u*cells-i,y=v*cells-j;
	const LODTestVertex &a=grid[j*side+i],&d=grid[(j+1)*side+i+1];
	const LODTestVertex &other=y>x?grid[(j+1)*side+i]:grid[j*side+i+1];
	const float wa=1-std::max(x,y),wd=std::min(x,y),wo=std::abs(x-y);
	return {{a.position.x*wa+d.position.x*wd+other.position.x*wo,
		a.position.y*wa+d.position.y*wd+other.position.y*wo,
		a.position.z*wa+d.position.z*wd+other.position.z*wo},
		a.attenuation*wa+d.attenuation*wd+other.attenuation*wo,a.base*wa+d.base*wd+other.base*wo};
}
void checkLODVertexNear(const LODTestVertex &a,const LODTestVertex &b,float epsilon)
{
	CHECK(std::abs(a.position.x-b.position.x)<epsilon);
	CHECK(std::abs(a.position.y-b.position.y)<epsilon);
	CHECK(std::abs(a.position.z-b.position.z)<epsilon);
	CHECK(std::abs(a.attenuation-b.attenuation)<epsilon);
	CHECK(std::abs(a.base-b.base)<epsilon);
}
}

TEST(water_surface_lod_retains_freecam_detail_and_reduces_rts_cost_monotonically)
{
	using namespace WaterSurfaceModel;
	CHECK(LODForCameraHeight(128,117).cells==128);
	CHECK(LODForCameraHeight(128,120).morph==0);
	CHECK(LODForCameraHeight(128,180).cells==64);
	CHECK(LODForCameraHeight(128,270).cells==32);
	CHECK(LODForCameraHeight(128,405).cells==16);
	CHECK(LODForCameraHeight(128,607.5f).cells==8);
	CHECK(LODForCameraHeight(128,310).cells==32);
	CHECK(LODForCameraHeight(64,310).cells==16);
	for (int base=1;base<=MeshCells;base*=2) {
		int previous=base;
		float previousDetail=0;
		for (int height=-10;height<=3000;++height) {
			const MeshLOD lod=LODForCameraHeight(base,float(height));
			CHECK(lod.cells<=previous && lod.cells>=std::min(base,MinimumLODCells));
			CHECK(lod.morph>=0 && lod.morph<=1);
			const float detail=std::log2(float(base)/lod.cells)+lod.morph;
			CHECK(detail+0.00001f>=previousDetail);
			previous=lod.cells;previousDetail=detail;
		}
	}
}

TEST(water_surface_lod_coarse_pairs_stay_bounded_and_do_not_read_morphed_vertices)
{
	using namespace WaterSurfaceModel;
	for (int cells=1;cells<=MeshCells;cells*=2) for (int j=0;j<=cells;++j) for (int i=0;i<=cells;++i) {
		const CoarsePair pair=CoarsePairForVertex(i,j,cells);
		CHECK(pair.first>=0 && pair.second<(cells+1)*(cells+1));
		if (cells>1) {
			CHECK(((pair.first%(cells+1))&1)==0 && ((pair.first/(cells+1))&1)==0);
			CHECK(((pair.second%(cells+1))&1)==0 && ((pair.second/(cells+1))&1)==0);
		}
	}
}

TEST(water_surface_lod_completed_morph_equals_coarse_triangles_and_metadata)
{
	using namespace WaterSurfaceModel;
	const std::vector<LODTestVertex> fine=testLODGrid(32,1.0f),coarse=testLODGrid(16,0.0f);
	for (int j=0;j<32;++j) for (int i=0;i<32;++i) {
		// Sample both triangle interiors, not only their shared vertices: this catches a wrong
		// diagonal or an arbitrary nearest-vertex collapse that would still match even/even nodes.
		for (int triangle=0;triangle<2;++triangle) {
			const float u=(i+(triangle?0.23f:0.71f))/32.0f,v=(j+(triangle?0.73f:0.29f))/32.0f;
			checkLODVertexNear(testLODSample(fine,32,u,v),testLODSample(coarse,16,u,v),0.0002f);
		}
	}
	const std::vector<LODTestVertex> original=testLODGrid(32,0.0f);
	for (float amount:{0.1f,0.35f,0.7f}) {
		const std::vector<LODTestVertex> morphed=testLODGrid(32,amount);
		for (size_t i=0;i<morphed.size();++i) {
			CHECK(morphed[i].attenuation>=0 && morphed[i].attenuation<=1);
			CHECK(std::abs(morphed[i].position.z-morphed[i].base)<=MaxDisplacement);
			CHECK(morphed[i].position.x>=std::min(original[i].position.x,fine[i].position.x)-0.0001f);
			CHECK(morphed[i].position.x<=std::max(original[i].position.x,fine[i].position.x)+0.0001f);
		}
	}
}

TEST(water_surface_lod_transition_is_continuous_in_both_camera_directions)
{
	using namespace WaterSurfaceModel;
	for (float threshold:{180.0f,270.0f,405.0f,607.5f}) {
		const MeshLOD below=LODForCameraHeight(128,threshold-0.001f),above=LODForCameraHeight(128,threshold+0.001f);
		const std::vector<LODTestVertex> fine=testLODGrid(below.cells,below.morph),coarse=testLODGrid(above.cells,above.morph);
		for (int j=0;j<12;++j) for (int i=0;i<12;++i) {
			const float u=(i+0.17f)/12,v=(j+0.63f)/12;
			checkLODVertexNear(testLODSample(fine,below.cells,u,v),testLODSample(coarse,above.cells,u,v),0.0003f);
		}
	}
	CHECK(MorphColour(0x10203040,0xff00aa55,0x00ff55aa,0)==0x10203040);
	CHECK(MorphColour(0x10203040,0xff00aa55,0x00ff55aa,1)==0x80808080);
}

TEST(water_surface_lod_shared_edges_remain_bit_identical_throughout_morph)
{
	using namespace WaterSurfaceModel;
	const Point a={-200,17,12},b={2100,533,12},camera={500,200,110};
	for (int cells=8;cells<=MeshCells;cells*=2) {
		std::vector<LODTestVertex> forward(cells+1),reverse(cells+1);
		for (int i=0;i<=cells;++i) {
			Point p=EdgePoint(a,b,float(i)/cells,camera);
			const float attenuation=ShoreDamping((p.y-30)*0.07f);
			p.z+=Evaluate(p.x,p.y,1.73f).height*attenuation;
			forward[i]={p,attenuation,12};
			p=EdgePoint(b,a,float(i)/cells,camera);
			const float reverseAttenuation=ShoreDamping((p.y-30)*0.07f);
			p.z+=Evaluate(p.x,p.y,1.73f).height*reverseAttenuation;
			reverse[i]={p,reverseAttenuation,12};
		}
		for (float amount:{0.0f,0.17f,0.5f,0.93f,1.0f}) for (int i=0;i<=cells;++i) {
			const CoarsePair fp=CoarsePairForVertex(i,0,cells),rp=CoarsePairForVertex(cells-i,0,cells);
			const LODTestVertex f=testLODMorph(forward[i],forward[fp.first],forward[fp.second],amount);
			const LODTestVertex r=testLODMorph(reverse[cells-i],reverse[rp.first],reverse[rp.second],amount);
			CHECK(memcmp(&f,&r,sizeof(f))==0);
		}
	}
}


namespace {
struct IndexedTestObstacle {
	WaterObstacleIndex::Influence influence;
	float z,height,major,minor,cosine,sine;bool box,hasHull;
};
struct IndexedTestDamping { float surface,obstacle;size_t visited; };
IndexedTestDamping indexedTestDamping(const std::vector<IndexedTestObstacle> &objects,
	const WaterObstacleIndex::Grid *index,float x,float y,float z,float depth,float offshore)
{
	IndexedTestDamping result={WaterSurfaceModel::TerrainDamping(depth,offshore),1,0};
	if (result.surface<=0) return result;
	const WaterObstacleIndex::Range range=index && offshore>0?index->Query(x,y):WaterObstacleIndex::Range{};
	const size_t count=range.indexed?range.count:objects.size();
	for (size_t i=0;i<count;++i) {
		const IndexedTestObstacle &o=objects[range.indexed?range.indices[i]:i];++result.visited;
		if (z<o.z-12 || z>o.z+o.height+3) continue;
		const float dx=x-o.influence.x,dy=y-o.influence.y;
		if (offshore>0 && WaterSurfaceModel::BeyondObstacleFade(dx,dy,o.influence.radius)) continue;
		float damping;
		if (o.hasHull && offshore>0) {
			const float legacy=offshore<1?WaterSurfaceModel::ShoreDamping(std::sqrt(dx*dx+dy*dy)-o.influence.radius):1;
			damping=legacy+(1-legacy)*offshore;
		} else if (o.box && offshore>0) {
			damping=WaterSurfaceModel::BoxDamping(dx*o.cosine+dy*o.sine,-dx*o.sine+dy*o.cosine,o.major,o.minor);
			if (offshore<1) { const float legacy=WaterSurfaceModel::ShoreDamping(std::sqrt(dx*dx+dy*dy)-o.influence.radius);damping=legacy+(damping-legacy)*offshore; }
		} else damping=WaterSurfaceModel::ShoreDamping(std::sqrt(dx*dx+dy*dy)-o.influence.radius);
		result.obstacle=(std::min)(result.obstacle,damping);result.surface=(std::min)(result.surface,damping);
		if (result.surface<=0) return result;
	}
	return result;
}
}

TEST(water_obstacle_bins_match_ordered_damping_and_metadata_with_fewer_candidates)
{
	using namespace WaterObstacleIndex;
	std::vector<IndexedTestObstacle> obstacles;std::vector<Influence> influences;
	uint32_t random=0x5317a291u;
	auto unit=[&]() { random=random*1664525u+1013904223u;return float(random>>8)/16777216.0f; };
	for (int i=0;i<839;++i) {
		const float major=2+unit()*70,minor=2+unit()*30,angle=unit()*6.2831853f;
		IndexedTestObstacle o={{unit()*4096-2048,unit()*4096-2048,std::sqrt(major*major+minor*minor)},unit()*30,5+unit()*40,
			major,minor,std::cos(angle),std::sin(angle),i%3!=0,i%11==0};
		obstacles.push_back(o);influences.push_back(o.influence);
	}
	Grid index;CHECK(index.Build(influences,{-2200,-2200,2200,2200}));
	CHECK(index.CellCount()<=Grid::MaxCells && index.ReferenceCount()<=Grid::MaxReferences);
	size_t indexedVisits=0,bruteVisits=0;
	for (int i=0;i<4000;++i) {
		const float x=unit()*4400-2200,y=unit()*4400-2200,z=unit()*50,depth=unit()*14;
		const float offshore=i%5==0?0:(i%5==1?1:unit());
		const auto a=indexedTestDamping(obstacles,nullptr,x,y,z,depth,offshore),b=indexedTestDamping(obstacles,&index,x,y,z,depth,offshore);
		CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
		if (offshore==0) CHECK(a.visited==b.visited);
		else { indexedVisits+=b.visited;bruteVisits+=a.visited; }
		const Range range=index.Query(x,y);
		for (size_t j=1;j<range.count;++j) CHECK(range.indices[j]>range.indices[j-1]);
	}
	CHECK(indexedVisits*20<bruteVisits); // Armed cost check: the implementation cannot return all839.
	// Movement/removal invalidates membership on the next draw; no old candidate list survives.
	for (int i=0;i<50;++i) { obstacles[i].influence.x+=317;obstacles[i].influence.y-=199; }
	obstacles.erase(obstacles.begin()+50,obstacles.begin()+80);influences.clear();
	for (const auto &o:obstacles) influences.push_back(o.influence);
	CHECK(index.Build(influences,{-2200,-2200,2200,2200}));
	for (int i=0;i<2000;++i) {
		const float x=unit()*5000-2500,y=unit()*5000-2500,offshore=unit();
		const auto a=indexedTestDamping(obstacles,nullptr,x,y,7,8,offshore),b=indexedTestDamping(obstacles,&index,x,y,7,8,offshore);
		CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
	}
	index.Clear();CHECK(!index.Query(0,0).indexed);
}

TEST(water_obstacle_bins_include_negative_boundaries_and_fail_to_complete_full_list)
{
	using namespace WaterObstacleIndex;
	const std::vector<IndexedTestObstacle> obstacles={{{-64,-128,54},7,30,40,30,0.6f,0.8f,true,false},
		{{0,0,54},7,30,40,30,1,0,true,true},{{64,128,54},7,30,40,30,1,0,false,false}};
	std::vector<Influence> influences;for (const auto &o:obstacles) influences.push_back(o.influence);
	Grid index;CHECK(index.Build(influences,{-256,-256,256,256}));
	for (float x:{-128.0001f,-128.0f,-127.9999f,-64.0001f,-64.0f,-63.9999f,-0.0001f,0.0f,0.0001f,63.9999f,64.0f,64.0001f,127.9999f,128.0f,128.0001f})
		for (float y:{-192.0f,-128.0f,-64.0f,0.0f,64.0f,128.0f,192.0f}) for (float offshore:{0.0f,0.001f,0.5f,0.999f,1.0f}) {
			const auto a=indexedTestDamping(obstacles,nullptr,x,y,7,7,offshore),b=indexedTestDamping(obstacles,&index,x,y,7,7,offshore);
			CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
		}
	for (Bounds bounds: {Bounds{-1e30f,-1e30f,1e30f,1e30f},Bounds{0,0,100000,100000},Bounds{NAN,0,1,1}}) {
		CHECK(!index.Build(influences,bounds));CHECK(!index.Query(0,0).indexed);
		const auto a=indexedTestDamping(obstacles,nullptr,0,0,7,7,0.5f),b=indexedTestDamping(obstacles,&index,0,0,7,7,0.5f);
		CHECK(a.surface==b.surface && a.obstacle==b.obstacle);
	}
	const std::vector<Influence> huge(20,Influence{0,0,100000});
	CHECK(!index.Build(huge,{-4000,-4000,4000,4000}));CHECK(!index.Query(0,0).indexed);
	CHECK(!index.Build({Influence{INFINITY,0,10}},{-128,-128,128,128}));
	CHECK(index.Build({}, {-128,-128,128,128}));CHECK(index.Query(0,0).indexed && index.Query(0,0).count==0);
	CHECK(!index.Query(INFINITY,0).indexed);
}

namespace {
WaterProjectedMesh::Projection testWaterProjection()
{
	WaterProjectedMesh::Projection p={{0.01,0,0,0,0.01,0,0,0,1},{0},{0,0,1},0.1,10,0.12};
	CHECK(p.Prepare());return p;
}
double projectedMeshArea(const WaterProjectedMesh::Mesh &mesh,const WaterProjectedMesh::Projection &projection)
{
	double area=0;
	for (size_t i=0;i<mesh.indices.size();i+=3) {
		WaterProjectedMesh::Point p[3];
		for (int j=0;j<3;++j) { CHECK(mesh.indices[i+j]<mesh.vertices.size());CHECK(projection.Transform(mesh.vertices[mesh.indices[i+j]].world,p[j])); }
		const double triangle=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);
		CHECK(triangle>0);area+=triangle*0.5;
	}
	return area;
}
}

TEST(water_projected_lattice_covers_visible_plane_once_with_bounded_shared_vertices)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
	const Projection projection=testWaterProjection();Mesh mesh;
	CHECK(Build(polygon,projection,{160,90},mesh));
	CHECK(mesh.vertices.size()<=VertexBound(160,90));
	// Frustum-edge cells retain their centre fan so authored-edge knots can enter without a
	// topology pop. Interior cells still have exactly two triangles; added work is perimeter-only.
	const size_t perimeterCells=2u*160+2u*90-4;
	CHECK(mesh.indices.size()>=160u*90*6 && mesh.indices.size()<=160u*90*6+perimeterCells*6);
	CHECK(mesh.vertices.size()<=161u*91+perimeterCells);
	CHECK(std::abs(projectedMeshArea(mesh,projection)-4*1.12*1.12)<1e-9);
	CHECK(mesh.vertices.size()<mesh.indices.size()/4); // Adjacent cells actually share samples.
	for (const Vertex &vertex:mesh.vertices) {
		CHECK(std::isfinite(vertex.world.x) && std::isfinite(vertex.world.y));
		CHECK(std::abs(vertex.world.x)<=112.000001 && std::abs(vertex.world.y)<=112.000001);
	}
}

TEST(water_projected_integer_cache_preserves_exact_reference_vertices_and_indices)
{
	using namespace WaterProjectedMesh;
	for (int fixture=0;fixture<8;++fixture) for (Resolution resolution:{Resolution{2,2},Resolution{31,17},Resolution{160,90}}) {
		Point corners[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
		if (fixture%3==1) { corners[0]={-95,-170};corners[1]={81,-110};corners[2]={120,92};corners[3]={-64,170}; }
		if (fixture%3==2) { corners[0]={-200,-200};corners[1]={0,-200};corners[2]={0,200};corners[3]={-200,200}; }
		if (fixture&1) std::reverse(corners,corners+4);
		Projection projection=testWaterProjection();
		projection.matrix[2]=fixture*0.013;projection.matrix[5]=fixture*-0.009;
		projection.matrix[6]=fixture*0.000021;projection.matrix[7]=fixture*0.00011;CHECK(projection.Prepare());
		std::vector<BoundaryKnot> knots;
		for (int edge=0;edge<4;++edge) {
			Point a=corners[edge],b=corners[(edge+1)%4];
			if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
			for (int i=0;i<=16;++i) { const double t=double(i)/16;knots.push_back({{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t},edge,i,t}); }
		}
		Mesh reference,fast;
		CHECK(Build(corners,projection,resolution,reference,60000,knots,false));
		CHECK(Build(corners,projection,resolution,fast,60000,knots,true));
		CHECK(reference.cells==fast.cells && reference.indices==fast.indices);
		CHECK(reference.vertices.size()==fast.vertices.size());
		CHECK(reference.transitions.size()==fast.transitions.size() && reference.collars.size()==fast.collars.size());
		if (reference.vertices.size()!=fast.vertices.size()) continue;
		std::unordered_map<WorldKey,float,WorldHash> cachedHeights;
		for (size_t i=0;i<fast.vertices.size();++i) {
			const Vertex &a=reference.vertices[i],&b=fast.vertices[i];
			CHECK(std::memcmp(&a.world,&b.world,sizeof(a.world))==0);
			CHECK(a.boundary==b.boundary && a.knot==b.knot && std::memcmp(&a.edgeParameter,&b.edgeParameter,sizeof(double))==0);
			CHECK(a.transition==b.transition && a.collar==b.collar);
			CHECK(a.lattice==b.lattice);
			if (i%17==0) {
				const float x=float(a.world.x),y=float(a.world.y);
				const WorldKey key=SampleKey(x,y,7);
				const float direct=WaterSurfaceModel::EvaluateOceanHeight(x,y,2.3f);
				const auto old=cachedHeights.emplace(key,direct);
				CHECK(direct==old.first->second); // Bypassing the single-patch value cache changes no wave sample.
			}
		}
	}
}

TEST(water_projected_clipping_preserves_reversed_shared_authored_boundaries)
{
	using namespace WaterProjectedMesh;
	const Point left[4]={{-200,-200},{0,-200},{0,200},{-200,200}};
	const Point right[4]={{200,200},{0,200},{0,-200},{200,-200}};
	Projection projection=testWaterProjection();projection.matrix[7]=0.001;CHECK(projection.Prepare());
	Mesh a,b;CHECK(Build(left,projection,{40,24},a));CHECK(Build(right,projection,{40,24},b));
	std::vector<double> ea,eb;
	for (const Vertex &v:a.vertices) if (v.world.x==0 && v.boundary>=0) ea.push_back(v.world.y);
	for (const Vertex &v:b.vertices) if (v.world.x==0 && v.boundary>=0) eb.push_back(v.world.y);
	std::sort(ea.begin(),ea.end());ea.erase(std::unique(ea.begin(),ea.end()),ea.end());
	std::sort(eb.begin(),eb.end());eb.erase(std::unique(eb.begin(),eb.end()),eb.end());
	CHECK(ea.size()>20 && ea.size()==eb.size());
	for (size_t i=0;i<ea.size() && i<eb.size();++i) CHECK(ea[i]==eb[i]);
	CHECK(std::abs(projectedMeshArea(a,projection)+projectedMeshArea(b,projection)-4*1.12*1.12)<1e-9);
}

TEST(water_projected_plane_guard_preserves_authored_nonplanar_levels)
{
	const float level[4]={7,7,7,7},sloped[4]={7,7,7,7.001f},invalid[4]={INFINITY,INFINITY,INFINITY,INFINITY};
	CHECK(WaterProjectedMesh::HasSingleLevel(level));
	CHECK(!WaterProjectedMesh::HasSingleLevel(sloped));
	CHECK(!WaterProjectedMesh::HasSingleLevel(invalid));
}

TEST(water_projected_near_horizon_is_finite_and_clips_real_authored_coverage)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
	Projection projection=testWaterProjection();projection.matrix[7]=0.002;projection.depth={0,0.002,1};projection.nearDistance=0.2;CHECK(projection.Prepare());
	Mesh mesh;CHECK(Build(polygon,projection,{48,32},mesh));CHECK(!mesh.indices.empty());
	for (const Vertex &v:mesh.vertices) {
		CHECK(std::isfinite(v.world.x) && std::isfinite(v.world.y));
		CHECK(std::abs(v.world.x)<=500.000001 && std::abs(v.world.y)<=500.000001);
		CHECK(Distance(projection.depth,v.world)>=0.2-1e-10);
	}
	CHECK(projectedMeshArea(mesh,projection)>0);
	projection.matrix[3]=projection.matrix[4]=projection.matrix[5]=0;CHECK(!projection.Prepare());
	CHECK(!Build(polygon,testWaterProjection(),{192,128},mesh,20)); // Explicit fallback; no partial mesh accepted.
}

TEST(water_projected_boundary_contains_each_retained_displaced_edge_bend)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-100,-100},{0,-100},{0,100},{-100,100}};
	std::vector<BoundaryKnot> knots;
	for (int i=0;i<=16;++i) knots.push_back({{0,-100+i*12.5},1,i,double(i)/16});
	Mesh mesh;CHECK(Build(polygon,testWaterProjection(),{2,2},mesh,60000,knots));
	CHECK(mesh.vertices.size()<=VertexBound(2,2,knots.size()));
	auto knotHeight=[](int i) { return std::sin((-100+i*12.5)*0.12)+i*0.07; };
	auto retainedHeight=[&](double y) {
		const double u=(y+100)/12.5;const int i=(std::max)(0,(std::min)(15,int(u)));
		return knotHeight(i)+(knotHeight(i+1)-knotHeight(i))*(u-i);
	};
	bool included[17]={};double covered=0,maxSkippedBend=0;
	for (const Vertex &v:mesh.vertices) if (v.knot>=0) {
		CHECK(v.knot<=16);included[v.knot]=true;
		CHECK(v.world.y==knots[v.knot].world.y);
	}
	for (bool present:included) CHECK(present);
	for (size_t t=0;t<mesh.indices.size();t+=3) for (int e=0;e<3;++e) {
		const Vertex &a=mesh.vertices[mesh.indices[t+e]],&b=mesh.vertices[mesh.indices[t+(e+1)%3]];
		if (a.world.x!=0 || b.world.x!=0 || a.world.y==b.world.y) continue;
		const double lo=(std::min)(a.world.y,b.world.y),hi=(std::max)(a.world.y,b.world.y);
		covered+=hi-lo;
		for (const BoundaryKnot &k:knots) CHECK(!(k.world.y>lo+1e-10 && k.world.y<hi-1e-10));
		const double midpoint=(lo+hi)*0.5;
		CHECK(std::abs((retainedHeight(lo)+retainedHeight(hi))*0.5-retainedHeight(midpoint))<1e-12);
	}
	CHECK(std::abs(covered-200)<1e-10);
	// Armed control: joining only the projected cell endpoints would visibly skip wave bends.
	for (int i=1;i<8;++i) maxSkippedBend=(std::max)(maxSkippedBend,
		std::abs(knotHeight(i)-(knotHeight(0)+(knotHeight(8)-knotHeight(0))*i/8)));
	CHECK(maxSkippedBend>0.5);
	const Point concave[4]={{-100,-100},{100,-100},{0,-50},{-100,100}};
	CHECK(!Build(concave,testWaterProjection(),{2,2},mesh));
}

TEST(water_projected_sampling_moves_continuously_and_refreshes_for_resize)
{
	using namespace WaterProjectedMesh;
	Projection a=testWaterProjection(),b=a;b.matrix[2]+=1e-7;CHECK(b.Prepare());
	for (int y=-4;y<=4;++y) for (int x=-4;x<=4;++x) {
		Point p,q;CHECK(a.Transform({x*0.2,y*0.2},p,true));CHECK(b.Transform({x*0.2,y*0.2},q,true));
		CHECK(std::abs((q.x-p.x)+0.00001)<1e-10 && std::abs(q.y-p.y)<1e-10);
		CHECK(std::abs(WaterSurfaceModel::EvaluateOceanHeight(float(p.x),float(p.y),1.7f)-
			WaterSurfaceModel::EvaluateOceanHeight(float(q.x),float(q.y),1.7f))<0.0001f);
	}
	const size_t available=WaterSurfaceModel::VertexBudget-50u*65*65;
	const Resolution carrier=ChooseResolution(1280,720,2,available,4*65),resize=ChooseResolution(960,540,2,available,4*65);
	CHECK(carrier.columns==160 && carrier.rows==90);
	CHECK(resize.columns<carrier.columns && resize.rows<carrier.rows);
	for (int patches:{1,2,10,50}) {
		const Resolution r=ChooseResolution(3840,2160,patches,WaterSurfaceModel::VertexBudget);
		CHECK(size_t(patches)*VertexBound(r.columns,r.rows)<=WaterSurfaceModel::VertexBudget);
		CHECK(VertexBound(r.columns,r.rows)<=60000);
	}
	for (int allPatches:{2,52,500,4000}) {
		const int base=WaterSurfaceModel::CellsForPatches(allPatches),flat=(std::max)(1,allPatches/3);
		const size_t legacy=size_t(base+1)*(base+1),retained=size_t(allPatches-flat)*legacy;
		const size_t available=WaterSurfaceModel::VertexBudget-retained,share=available/flat;
		const Resolution r=ChooseResolution(1280,720,flat,available,4*(base+1));
		const size_t bound=VertexBound(r.columns,r.rows,4*(base+1));
		// Projection can fail (budget, singular camera or concavity); its legacy grid remains
		// inside the same share, so an arbitrary mix of accepted and fallback patches is bounded.
		CHECK(legacy<=share);
		CHECK(retained+size_t(flat)*(bound<=share?bound:legacy)<=WaterSurfaceModel::VertexBudget);
	}
}

namespace {
double projectedTestWave(WaterProjectedMesh::Point p) {
	return 1.2*std::sin(p.x*0.037)+0.7*std::cos(p.y*0.049)+0.4*std::sin((p.x+p.y)*0.021);
}
double projectedTestEdge(const WaterProjectedMesh::Point corners[4],int edge,double parameter) {
	using namespace WaterProjectedMesh;
	Point a=corners[edge],b=corners[(edge+1)%4];
	if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
	// The retained mesh is deliberately much coarser than the nonlinear analytic height.
	const double u=parameter*2;const int index=(std::min)(1,int(u));const double t=u-index;
	const Point lo={a.x+(b.x-a.x)*index/2,a.y+(b.y-a.y)*index/2};
	const Point hi={a.x+(b.x-a.x)*(index+1)/2,a.y+(b.y-a.y)*(index+1)/2};
	return projectedTestWave(lo)+(projectedTestWave(hi)-projectedTestWave(lo))*t;
}
double projectedTestCollar(const WaterProjectedMesh::Point corners[4],WaterProjectedMesh::Point p,const WaterProjectedMesh::BoundaryCollar &collar) {
	const double analytic=projectedTestWave(p);double value=analytic;
	for (int i=0;i<collar.count;++i) value+=collar.weight[i]*(projectedTestEdge(corners,collar.edge[i],collar.parameter[i])-analytic);
	return value;
}
std::vector<double> projectedTestHeights(const WaterProjectedMesh::Mesh &mesh,const WaterProjectedMesh::Point corners[4]) {
	using namespace WaterProjectedMesh;
	std::vector<double> heights;
	for (const Vertex &v:mesh.vertices) {
		double height=v.boundary>=0?projectedTestEdge(corners,v.boundary,v.edgeParameter):
			(v.collar>=0?projectedTestCollar(corners,v.world,mesh.collars[v.collar]):projectedTestWave(v.world));
		if (v.transition>=0) {
			const CentreTransition &transition=mesh.transitions[v.transition];double source[3],sum=0;
			for (int i=0;i<3;++i) {
				CHECK(transition.weight[i]>=-1e-10 && transition.weight[i]<=1+1e-10);sum+=transition.weight[i];
				source[i]=projectedTestCollar(corners,transition.source[i],CollarAt(corners,transition.source[i]));
			}
			CHECK(std::abs(sum-1)<1e-10);height=InterpolateCentre(transition,height,source);
		}
		CHECK(std::abs(height)<=2.300001);heights.push_back(height);
	}
	return heights;
}
double projectedTestRenderedHeight(const WaterProjectedMesh::Mesh &mesh,const std::vector<double> &height,WaterProjectedMesh::Point p) {
	using namespace WaterProjectedMesh;
	for (size_t i=0;i<mesh.indices.size();i+=3) {
		const unsigned ia=mesh.indices[i],ib=mesh.indices[i+1],ic=mesh.indices[i+2];
		const Point a=mesh.vertices[ia].world,b=mesh.vertices[ib].world,c=mesh.vertices[ic].world;
		const double determinant=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
		if (std::abs(determinant)<1e-16) continue;
		const double v=((p.x-a.x)*(c.y-a.y)-(p.y-a.y)*(c.x-a.x))/determinant;
		const double w=((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x))/determinant;
		if (v>=-1e-10 && w>=-1e-10 && v+w<=1+1e-10) return height[ia]*(1-v-w)+height[ib]*v+height[ic]*w;
	}
	CHECK(false);return 0;
}
}

TEST(water_projected_actual_triangles_stay_continuous_at_knot_and_full_cell_transitions)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	std::vector<BoundaryKnot> knots;
	for (int edge=0;edge<4;++edge) {
		Point a=polygon[edge],b=polygon[(edge+1)%4];
		if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
		for (int i=0;i<=2;++i) knots.push_back({{a.x+(b.x-a.x)*i/2,a.y+(b.y-a.y)*i/2},edge,i,i/2.0});
	}
	for (int axis:{0,1}) {
		Projection before=testWaterProjection(),after=before;
		// Y motion moves the only visible retained knot across a lattice line; X motion
		// changes a clipped cell into a full cell. Compare the actual nonlinear triangles.
		before.matrix[axis*3+2]=-1e-7;after.matrix[axis*3+2]=1e-7;
		CHECK(before.Prepare() && after.Prepare());Mesh a,b;
		CHECK(Build(polygon,before,{2,2},a,60000,knots));CHECK(Build(polygon,after,{2,2},b,60000,knots));
		const auto ah=projectedTestHeights(a,polygon),bh=projectedTestHeights(b,polygon);
		for (double y:{-90.0,-50.0,-10.0,10.0,50.0,90.0}) for (double x:{-95.0,-65.0,-35.0,-5.0})
			CHECK(std::abs(projectedTestRenderedHeight(a,ah,{x,y})-projectedTestRenderedHeight(b,bh,{x,y}))<0.00002);
		CHECK(a.vertices.size()<=VertexBound(2,2,knots.size()) && b.vertices.size()<=VertexBound(2,2,knots.size()));
	}
	// Armed controls: both discarded centre height and the old boundary/interior handoff
	// differ materially here. A test comparing only moving XY samples would miss these pops.
	CHECK(std::abs(projectedTestWave({-56,-56})-(projectedTestWave({-112,-112})+projectedTestWave({0,0}))*0.5)>0.1);
	CHECK(std::abs(projectedTestWave({0,50})-projectedTestEdge(polygon,1,0.55))>0.1);
	for (double y:{-501.0,-499.0,0.0,499.0,501.0}) for (double x:{-12.00001,-12.0,-11.99999,-0.00001,0.00001}) {
		const double a=projectedTestCollar(polygon,{x,y},CollarAt(polygon,{x,y}));
		const double b=projectedTestCollar(polygon,{x+1e-7,y},CollarAt(polygon,{x+1e-7,y}));
		CHECK(std::abs(a-b)<0.000001 && std::abs(a)<=2.300001);
	}
}

TEST(water_projected_horizon_reference_rejects_nonconvex_weights_and_preserves_height_bounds)
{
	using namespace WaterProjectedMesh;
	const Point polygon[4]={{-500,-0.9},{500,-0.9},{500,500},{-500,500}};
	// The horizon lies inside one lattice row. More than75% of that row is visible, so it
	// attempts the regular-cell transition, but its upper reference corners are behind the eye.
	Projection projection={{1,0,0,0,1.018,0,0,1,1},{0},{0,1,1},0.1,600,0.12};
	CHECK(projection.Prepare());Mesh mesh;CHECK(Build(polygon,projection,{16,90},mesh));
	CHECK(projectedMeshArea(mesh,projection)>0);
	const double bottom=1.12*(2.0*85/90-1),top=1.12*(2.0*86/90-1),visibleTop=1.018*500/501;
	const double centre=(bottom+visibleTop)*0.5;
	Point lo,hi,mid;CHECK(projection.Transform({0,bottom},lo,true));
	CHECK(projection.Transform({0,top},hi,true));CHECK(projection.Transform({0,centre},mid,true));
	CHECK((top-visibleTop)/(top-bottom)<0.25 && Distance(projection.depth,hi)<0);
	const double opposite=(mid.y-lo.y)/(hi.y-lo.y);
	CHECK(opposite<-0.08); // Armed control: the unguarded production formula is non-convex.
	double unsafe[3]={1-opposite,0,opposite};CHECK(!NormalizeCentreWeights(unsafe));
	CHECK((1-opposite)*2.3+opposite*-2.3>2.6); // It can exceed the2.3-unit source bound.
	int guarded=0;
	for (const Vertex &v:mesh.vertices) if (v.boundary<0 && std::abs(v.world.y-mid.y)<1e-7) {
		++guarded;CHECK(v.transition<0);
	}
	CHECK(guarded>0);
	const auto heights=projectedTestHeights(mesh,polygon);
	for (double height:heights) CHECK(std::isfinite(height) && std::abs(height)<=2.300001);
	CHECK(mesh.vertices.size()<=VertexBound(16,90));
	double roundoff[3]={-1e-12,0.4,0.600000000001};CHECK(NormalizeCentreWeights(roundoff));
	CHECK(roundoff[0]==0 && std::abs(roundoff[0]+roundoff[1]+roundoff[2]-1)<1e-14);
	double nonfinite[3]={0.5,0.5,NAN};CHECK(!NormalizeCentreWeights(nonfinite));
}

static WaterHullModel::Hull foamTestHull(float x,float y,float vx,float vy,float angle=0,float level=7)
{
	WaterHullModel::Hull h;
	h.data[0][0]=x;h.data[0][1]=y;h.data[0][2]=level;h.data[0][3]=4;
	h.data[1][0]=std::cos(angle);h.data[1][1]=std::sin(angle);h.data[1][2]=12;h.data[1][3]=4;
	h.data[2][0]=vx;h.data[2][1]=vy;h.data[3][0]=-12;h.data[3][2]=12;
	const float p[8]={-12,-4,12,-4,12,4,-12,4};
	for(int i=0;i<8;++i) h.data[4+i/4][i%4]=p[i];return h;
}

TEST(water_hull_contact_clips_actual_mesh_and_excludes_unreachable_superstructure)
{
	using namespace WaterHullModel;
	std::vector<Point> points;
	// The greatest projected extent is strictly inside the slab. Endpoint-plane
	// slices alone miss it; a collision rectangle would include unsupported corners.
	CHECK(WaterHullContact::ProjectTriangle({7,0,1},{-2,-3,-1},{-2,3,4},3,points));
	const WaterHullModel::Polygon polygon=BuildPolygon(points);CHECK(polygon.count>=3);
	float maxX=-1e30f,minY=1e30f,maxY=-1e30f;
	for(unsigned i=0;i<polygon.count;++i) {
		maxX=(std::max)(maxX,polygon.points[i].x);minY=(std::min)(minY,polygon.points[i].y);maxY=(std::max)(maxY,polygon.points[i].y);
	}
	CHECK(maxX==7 && minY>-3 && maxY<3);
	CHECK(SignedDistance({6,2},polygon)>0); // Armed against using the triangle AABB.
	const auto before=points;
	CHECK(WaterHullContact::ProjectTriangle({-100,-100,4},{100,-100,5},{0,100,8},3,points));
	CHECK(WaterHullContact::ProjectTriangle({-100,-100,-8},{100,-100,-5},{0,100,-4},3,points));
	CHECK(points.size()==before.size()); // Neither the deck above the slab nor a deep part leaks in.
	CHECK(!WaterHullContact::ProjectTriangle({NAN,0,0},{1,0,0},{0,1,0},3,points));
	CHECK(!WaterHullContact::ProjectTriangle({0,0,0},{1,0,0},{0,1,0},3.1f,points));
	CHECK(points.size()==before.size());
	std::vector<Point> budget(MaxSlicePoints-2);
	CHECK(!WaterHullContact::ProjectTriangle({0,0,1},{1,0,1},{0,1,1},3,budget));
	CHECK(budget.size()==MaxSlicePoints-2); // Failure cannot append a truncated contour.
	std::vector<Point> boundary;
	CHECK(WaterHullContact::ProjectTriangle({0,0,3},{1,0,3},{0,1,3},3,boundary));
	CHECK(BuildPolygon(boundary).count==3);
	CHECK(WaterHullContact::ReachableCrest(NAN)==0);
	CHECK(WaterHullContact::ReachableCrest(4,NAN)==0);
	CHECK(WaterHullContact::ReachableCrest(-1)==0 && WaterHullContact::ReachableCrest(4,0)==0);
	CHECK(std::abs(WaterHullContact::ReachableCrest(.25f)-.10f)<1e-6f);
	CHECK(WaterHullContact::ReachableCrest(4)==3 && WaterHullContact::ReachableCrest(100)==3);
	CHECK(WaterHullContact::ReachableCrest(4,.1f)<=WaterFftGeometry::MaxVerticalDisplacement*.100001f);
}

TEST(water_hull_contact_current_slab_stabilizes_a_tilted_keel_without_changing_mean_slice)
{
	using namespace WaterHullModel;
	const Point3 hull[8]={{-40,-.5f,-.25f},{40,-.5f,-.25f},{40,.5f,-.25f},{-40,.5f,-.25f},
		{-44,-10,3},{44,-10,3},{44,10,3},{-44,10,3}};
	const unsigned indices[12][3]={{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},
		{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
	for(float pitch:{-.05f,0.0f,.05f}) for(float roll:{-.05f,0.0f,.05f}) {
		Point3 rotated[8];float lowX=1e30f,highX=-1e30f,lowY=1e30f,highY=-1e30f;
		const float cy=std::cos(pitch),sy=std::sin(pitch),cp=std::cos(roll),sp=std::sin(roll);
		for(unsigned i=0;i<8;++i) {
			const Point3 p=hull[i];rotated[i]={cy*p.x+sy*sp*p.y+sy*cp*p.z,cp*p.y-sp*p.z,-sy*p.x+cy*sp*p.y+cy*cp*p.z};
			lowX=(std::min)(lowX,rotated[i].x);highX=(std::max)(highX,rotated[i].x);
			lowY=(std::min)(lowY,rotated[i].y);highY=(std::max)(highY,rotated[i].y);
		}
		std::vector<Point> mean,contact,reversed;
		for(const auto &tri:indices) {
			const Point3 a=rotated[tri[0]],b=rotated[tri[1]],c=rotated[tri[2]];
			CHECK(SliceTriangle(a,b,c,mean));
			CHECK(WaterHullContact::ProjectTriangle(a,b,c,3,contact));
			CHECK(WaterHullContact::ProjectTriangle(c,b,a,3,reversed));
		}
		const WaterHullModel::Polygon exact=BuildPolygon(mean),envelope=BuildPolygon(contact),reverse=BuildPolygon(reversed);
		CHECK(exact.count>=3 && envelope.count>=3 && envelope.count<=MaxVertices);
		CHECK(reverse.count==envelope.count);
		for(unsigned i=0;i<envelope.count;++i) {
			const Point p=envelope.points[i];
			CHECK(p.x>=lowX-.0001f && p.x<=highX+.0001f && p.y>=lowY-.0001f && p.y<=highY+.0001f);
			CHECK(std::abs(p.x-reverse.points[i].x)<.0001f && std::abs(p.y-reverse.points[i].y)<.0001f);
		}
		for(unsigned i=0;i<exact.count;++i) CHECK(SignedDistance(exact.points[i],envelope)<=.0002f+envelope.error);
		CHECK(SignedDistance({0,7},envelope)<0 && SignedDistance({0,-7},envelope)<0);
		// Building a new current-transform envelope never mutates the original mean contour.
		const WaterHullModel::Polygon meanAfter=BuildPolygon(mean);CHECK(meanAfter.count==exact.count);
		for(unsigned i=0;i<exact.count;++i) CHECK(exact.points[i].x==meanAfter.points[i].x && exact.points[i].y==meanAfter.points[i].y);
		if(pitch==0 && roll==0) CHECK(SignedDistance({0,7},exact)>4); // Meaningful tiny-keel control.
	}
}

namespace {
WaterHullPoseCache::Mesh waterTestHullPose(float angle=0,float draft=0)
{
	static int model,vertices,triangles;
	WaterHullPoseCache::Mesh mesh;mesh.identity=&model;mesh.vertexArray=&vertices;mesh.triangleArray=&triangles;
	mesh.vertexCount=8;mesh.triangleCount=12;mesh.crossesMean=true;
	const float center[3]={0,-20,24.859f},extent[3]={285,50,25.141f};
	mesh.supportRadius=WaterHullPoseCache::SupportRadius(center,extent);
	mesh.transform[0][0]=mesh.transform[2][2]=std::cos(angle);
	mesh.transform[0][2]=std::sin(angle);mesh.transform[2][0]=-std::sin(angle);
	mesh.transform[1][1]=1;mesh.transform[2][3]=-draft;return mesh;
}
WaterHullModel::Point3 waterTestTransformPoint(const WaterHullPoseCache::Mesh &mesh,WaterHullModel::Point3 p)
{
	WaterHullModel::Point3 q;
	q.x=mesh.transform[0][0]*p.x+mesh.transform[0][1]*p.y+mesh.transform[0][2]*p.z+mesh.transform[0][3];
	q.y=mesh.transform[1][0]*p.x+mesh.transform[1][1]*p.y+mesh.transform[1][2]*p.z+mesh.transform[1][3];
	q.z=mesh.transform[2][0]*p.x+mesh.transform[2][1]*p.y+mesh.transform[2][2]*p.z+mesh.transform[2][3];return q;
}
double waterTestSliceMinimumX(const WaterHullPoseCache::Mesh &mesh,bool contact)
{
	using namespace WaterHullModel;
	const Point3 vertices[8]={{-285,-70,-.282f},{285,-70,-.282f},{285,30,-.282f},{-285,30,-.282f},
		{-285,-70,50},{285,-70,50},{285,30,50},{-285,30,50}};
	const unsigned triangles[12][3]={{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},
		{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
	std::vector<Point> points;
	for(const auto &t:triangles) {
		const Point3 a=waterTestTransformPoint(mesh,vertices[t[0]]),b=waterTestTransformPoint(mesh,vertices[t[1]]),
			c=waterTestTransformPoint(mesh,vertices[t[2]]);
		CHECK(contact?WaterHullContact::ProjectTriangle(a,b,c,3,points):SliceTriangle(a,b,c,points));
	}
	CHECK(points.size()>=3);double minimum=1e30;
	for(Point p:points) minimum=(std::min)(minimum,double(p.x));return minimum;
}
}

TEST(water_hull_pose_cache_refreshes_old_rotation_bucket_at_both_drafts)
{
	using namespace WaterHullPoseCache;
	for(float draft:{0.0f,6.375f}) {
		const float angle0=.0351f,angle1=.0449f;
		const Mesh old=waterTestHullPose(angle0,draft),current=waterTestHullPose(angle1,draft);
		// Armed reproduction of the old signature: these different rotations have identical
		// hundredth-rounded coefficients, despite moving the long hull by several world units.
		for(unsigned i=0;i<3;++i) for(unsigned j=0;j<4;++j)
			CHECK(std::llround(old.transform[i][j]*100.0f)==std::llround(current.transform[i][j]*100.0f));
		CHECK(VertexMovementBound(old,current)>2);
		Cache cache;unsigned sliceCalls=0;double cachedMinimum[2]={};
		auto updateSlice=[&](const Mesh &pose) {
			const std::vector<Mesh> meshes{pose};
			if(!cache.Reusable(meshes,3)) {
				for(unsigned contact=0;contact<2;++contact) cachedMinimum[contact]=waterTestSliceMinimumX(pose,contact!=0);
				++sliceCalls;CHECK(cache.Store(meshes,3));
			}
		};
		updateSlice(old);CHECK(sliceCalls==1);
		const double savedMean=cachedMinimum[0],savedContact=cachedMinimum[1];
		for(unsigned repeat=0;repeat<20;++repeat) updateSlice(old);
		const Mesh noise=waterTestHullPose(angle0+.000001f,draft);
		CHECK(VertexMovementBound(old,noise)<MaxVertexMovement);
		// Actual-height triangle contact cannot reuse even this small rotation: near-tangent
		// slice endpoints amplify it. The default mean-water cache keeps its old tolerance.
		CHECK(cache.Reusable(std::vector<Mesh>{old},3,0));
		CHECK(!cache.Reusable(std::vector<Mesh>{noise},3,0));
		CHECK(!cache.Reusable(std::vector<Mesh>{old},3,-1));
		CHECK(!cache.Reusable(std::vector<Mesh>{old},3,std::numeric_limits<double>::infinity()));
		updateSlice(noise);
		CHECK(sliceCalls==1 && cachedMinimum[0]==savedMean && cachedMinimum[1]==savedContact);
		CHECK(std::abs(waterTestSliceMinimumX(noise,true)-savedContact)>.0001); // a genuinely stale-but-reusable slice
		CHECK(!cache.Reusable(std::vector<Mesh>{current},3));
		for(bool contact:{false,true}) {
			const double h=draft+(contact?3:0),z=-.282;
			// Independent analytic intersection of the tilted lower face with the upper slice
			// plane. The chosen fixture intersects strictly inside its570-unit long side.
			const double expected0=(z-std::cos(double(angle0))*h)/std::sin(double(angle0));
			const double expected1=(z-std::cos(double(angle1))*h)/std::sin(double(angle1));
			const double stale=waterTestSliceMinimumX(old,contact),exact=waterTestSliceMinimumX(current,contact);
			CHECK(std::abs(stale-expected0)<.002 && std::abs(exact-expected1)<.002);
			CHECK(std::abs(stale-exact)>1); // Both actual planes expose a stale contour.
		}
		updateSlice(current);CHECK(sliceCalls==2);
		CHECK(cachedMinimum[0]==waterTestSliceMinimumX(current,false) && cachedMinimum[1]==waterTestSliceMinimumX(current,true));
		updateSlice(current);CHECK(sliceCalls==2 && cache.Reusable(std::vector<Mesh>{current},3));
	}
}

TEST(water_hull_pose_error_bounds_every_transformed_local_aabb_corner)
{
	using namespace WaterHullPoseCache;
	// Nonzero local origin, negative coordinates, scale and shear catch an extent-only R
	// or a rotation-angle shortcut. True movement is evaluated independently at all corners.
	const float center[3]={1000,-700,250},extent[3]={285,50,25};
	const double radius=SupportRadius(center,extent);
	CHECK(radius>std::sqrt(285.0*285+50.0*50+25.0*25)*4);
	for(unsigned fixture=0;fixture<96;++fixture) {
		Mesh a=waterTestHullPose(),b=a;a.supportRadius=b.supportRadius=radius;
		for(unsigned i=0;i<3;++i) for(unsigned j=0;j<4;++j)
			b.transform[i][j]+=float(std::sin(fixture*.71+i*1.31+j*.47)*(j==3?.07:.004));
		const double bound=VertexMovementBound(a,b);CHECK(std::isfinite(bound) && bound>=0);
		for(unsigned corner=0;corner<8;++corner) {
			double squared=0;
			for(unsigned i=0;i<3;++i) {
				double q0=a.transform[i][3],q1=b.transform[i][3];
				for(unsigned j=0;j<3;++j) {
					const double p=double(center[j])+((corner&(1u<<j))?extent[j]:-double(extent[j]));
					q0+=double(a.transform[i][j])*p;q1+=double(b.transform[i][j])*p;
				}
				squared+=(q1-q0)*(q1-q0);
			}
			CHECK(std::sqrt(squared)<=bound+1e-10);
		}
	}
	float badExtent[3]={1,-1,2};CHECK(!std::isfinite(SupportRadius(center,badExtent)));
	badExtent[1]=NAN;CHECK(!std::isfinite(SupportRadius(center,badExtent)));
}

TEST(water_hull_pose_cache_accumulates_translation_and_rotation_against_sliced_pose)
{
	using namespace WaterHullPoseCache;
	Mesh original=waterTestHullPose();std::vector<Mesh> current{original};Cache cache;
	CHECK(!cache.Reusable(current,3));CHECK(cache.Store(current,3));
	for(int repeat=0;repeat<100;++repeat) CHECK(cache.Reusable(current,3));
	current[0].transform[0][3]=.0001f;current[0].transform[1][3]=-.0001f;
	CHECK(cache.Reusable(current,3)); // cancellation noise still reuses the original slice
	unsigned refreshes=0;Mesh previous=original;
	for(unsigned frame=1;frame<=24;++frame) {
		current[0]=original;current[0].transform[0][3]=frame*.002f;
		CHECK(VertexMovementBound(previous,current[0])<MaxVertexMovement); // previous-frame-only control would miss drift
		if(!cache.Reusable(current,3)) { ++refreshes;CHECK(cache.Store(current,3)); }
		previous=current[0];
	}
	CHECK(refreshes>=4);
	CHECK(cache.Store(std::vector<Mesh>{original},3));
	for(unsigned step=1;step<=4;++step) {
		current[0]=waterTestHullPose(step*.000001f);
		CHECK(cache.Reusable(current,3));
	}
	current[0]=waterTestHullPose(.00003f);CHECK(!cache.Reusable(current,3));
	// Store copied the original pose; editing the caller's vector never edits the snapshot.
	cache.Clear();CHECK(cache.Size()==0 && !cache.Reusable(current,3));
}

TEST(water_hull_pose_cache_refreshes_geometry_membership_flags_and_crest)
{
	using namespace WaterHullPoseCache;
	Mesh a=waterTestHullPose(),b=a;static int other;
	std::vector<Mesh> original{a};Cache cache;CHECK(cache.Store(original,3));
	auto rejects=[&](const Mesh &changed) { CHECK(!cache.Reusable(std::vector<Mesh>{changed},3)); };
	b.identity=&other;rejects(b);b=a;b.vertexArray=&other;rejects(b);b=a;b.triangleArray=&other;rejects(b);
	b=a;++b.vertexCount;rejects(b);b=a;++b.triangleCount;rejects(b);
	b=a;b.crossesMean=false;rejects(b);b=a;b.supportRadius+=1;rejects(b);
	CHECK(cache.Reusable(original,2.995));CHECK(!cache.Reusable(original,2.98));
	b=a;b.transform[0][3]=.009f;
	CHECK(cache.Reusable(std::vector<Mesh>{b},3));CHECK(!cache.Reusable(std::vector<Mesh>{b},2.998));
	CHECK(!cache.Reusable(std::vector<Mesh>{},3));CHECK(!cache.Reusable(std::vector<Mesh>{a,a},3));
	b=a;b.identity=&other;CHECK(cache.Store(std::vector<Mesh>{a,b},3));
	CHECK(cache.Reusable(std::vector<Mesh>{a,b},3));CHECK(!cache.Reusable(std::vector<Mesh>{b,a},3));
	// Invalid data cannot even reuse its own identical-looking cached state.
	b=a;b.transform[2][1]=NAN;CHECK(!cache.Store(std::vector<Mesh>{b},3));CHECK(cache.Size()==0);
	CHECK(!cache.Reusable(std::vector<Mesh>{b},3));CHECK(!cache.Store(original,NAN));
	b=a;b.supportRadius=INFINITY;CHECK(!cache.Store(std::vector<Mesh>{b},3));
	CHECK(cache.Store(std::vector<Mesh>(MaxMeshes,a),3));CHECK(cache.Size()==MaxMeshes);
	CHECK(!cache.Reusable(std::vector<Mesh>(MaxMeshes+1,a),3));
	CHECK(!cache.Store(std::vector<Mesh>(MaxMeshes+1,a),3));CHECK(cache.Size()==0);
	CHECK(cache.Store(std::vector<Mesh>{},3));CHECK(cache.Reusable(std::vector<Mesh>{},3)); // stable empty slice
}

TEST(water_foam_line_integral_is_partition_invariant_and_has_no_endpoint_beads)
{
	using namespace WaterFoamHistory;
	const double radius=3.75,speed=30,length=120;
	for(double along:{-3.0,0.0,1.0,15.0,18.75,60.0,118.0,120.0,123.0})
	for(double side:{0.0,1.5,3.74,3.75,4.0}) {
		const auto whole=IntegrateKernel(along,side*side,length,radius);
		double mass=0,moment=0;
		for(unsigned section=0;section<16;++section) {
			const double begin=section*7.5;
			const auto part=IntegrateKernel(along-begin,side*side,7.5,radius);
			mass+=part.mass/16;moment+=part.mass/16*(begin+part.mean*7.5)/length;
		}
		CHECK(std::abs(mass-whole.mass)<1e-13);
		CHECK(std::abs(moment-whole.mass*whole.mean)<1e-13);
		CHECK(whole.mass>=0 && whole.mass<=1 && whole.mean>=0 && whole.mean<=1);
		// Independent scalar Simpson integration of the unclipped radial kernel. This
		// detects a matching algebra mistake in the whole and partitioned analytic path.
		const unsigned samples=8192;double reference=0;
		for(unsigned i=0;i<=samples;++i) {
			const double offset=length*i/samples-along;
			const double value=(std::max)(0.0,1-(offset*offset+side*side)/(radius*radius));
			reference+=value*value*(i==0||i==samples?1:i%2?4:2);
		}
		reference/=3*samples;
		CHECK(std::abs(reference-whole.mass)<2e-8);
	}
	const double expected=(16.0/15)*radius/speed;
	for(double position:{15.0,18.75,30.0,33.75,60.0,63.75,90.0,93.75}) {
		double dose=0;
		for(unsigned section=0;section<16;++section)
			dose+=.25*IntegrateKernel(position-section*7.5,0,7.5,radius).mass;
		CHECK(std::abs(dose-expected)<1e-13); // Joint and mid-section are equally dense.
	}
	// Armed old algorithm: a constant capsule plus each rounded cap doubles the
	// density at a shared endpoint while a half-section has one contribution.
	auto oldDose=[&](double position) {
		double dose=0;
		for(unsigned section=0;section<16;++section) {
			const double begin=section*7.5,nearest=Clamp(position,begin,begin+7.5),distance=position-nearest;
			const double kernel=(std::max)(0.0,1-distance*distance/(radius*radius));
			dose+=.25*kernel*kernel;
		}
		return dose;
	};
	CHECK(oldDose(60)>1.9*oldDose(63.75));
	const auto point=IntegrateKernel(0,0,0,radius);CHECK(point.mass==1 && point.mean==.5);
	CHECK(IntegrateKernel(0,0,1,0).mass==0);
	CHECK(IntegrateKernel(NAN,0,1,radius).mass==0);
	CHECK(IntegrateKernel(0,-1,1,radius).mass==0);
	const auto tiny=IntegrateKernel(5e-8,0,1e-7,radius);
	CHECK(std::abs(tiny.mass-1)<1e-12 && std::abs(tiny.mean-.5)<1e-9);
}

TEST(water_foam_window_covers_rts_frustum_and_has_bounded_resize_fallback)
{
	using WaterFoamWindow::Point;using WaterFoamWindow::Choose;
	const Point camera={125.3,-54.1,430};const double level=4,diagonal=std::sqrt(.5);
	auto raysFor=[&](double aspect,Point rays[5]) {
		rays[0]={0,diagonal,-diagonal};unsigned at=1;
		for(double vertical:{-1.0,1.0}) for(double horizontal:{-1.0,1.0})
			rays[at++]={horizontal*.45*aspect,diagonal*(1+vertical*.45),diagonal*(-1+vertical*.45)};
	};
	Point rays[5];raysFor(16.0/9,rays);const auto map=Choose(camera,rays,level);
	// Freeze the pre-horizon-fix RTS mapping, including the non-power-of-two texel pitch.
	CHECK(map.originX==-957 && map.originY==-38 && map.widthWorld==2112 && map.heightWorld==1216);
	CHECK(map.widthPixels==512 && map.heightPixels==512);
	CHECK(map.widthWorld>=256 && map.widthWorld<=4096 && map.heightWorld>=256 && map.heightWorld<=4096);
	for(unsigned i=0;i<5;++i) {
		const double time=(level-camera.z)/rays[i].z;
		const double x=camera.x+rays[i].x*time,y=camera.y+rays[i].y*time;
		CHECK(x-map.originX>=32 && map.originX+map.widthWorld-x>=32);
		CHECK(y-map.originY>=32 && map.originY+map.heightWorld-y>=32);
	}
	const auto pan=Choose({camera.x+.000001,camera.y+.000001,camera.z},rays,level);
	CHECK(pan.originX==map.originX && pan.originY==map.originY);
	// An equal-aspect viewport resize preserves world mapping; a wider FOV expands it.
	Point resized[5];raysFor(1920.0/1080,resized);const auto same=Choose(camera,resized,level);
	CHECK(same.originX==map.originX && same.originY==map.originY && same.widthWorld==map.widthWorld);
	raysFor(21.0/9,resized);const auto wide=Choose(camera,resized,level);
	CHECK(wide.widthWorld>map.widthWorld && wide.heightWorld==map.heightWorld);
	CHECK(wide.originX==-1260 && wide.originY==-38 && wide.widthWorld==2688 && wide.heightWorld==1216);
	Point horizon[5]={{1,0,-1e-8},{1,-1,0},{1,1,0},{1,-1,-.1},{1,1,-.1}};
	for(const auto fallback:{Choose(camera,horizon,level),Choose({0,0,-3},rays,level)}) {
		CHECK(fallback.widthWorld==4096 && fallback.heightWorld==4096);
		CHECK(std::isfinite(fallback.originX) && std::isfinite(fallback.originY));
		CHECK(fallback.widthPixels==512 && fallback.heightPixels==512);
	}
}

TEST(water_foam_horizon_window_preserves_near_water_across_yaw_pitch_and_resize)
{
	using WaterFoamWindow::Point;using WaterFoamWindow::Choose;
	auto inside=[](const WaterFoamHistory::Mapping &map,Point p,double margin) {
		return p.x-map.originX>=margin && map.originX+map.widthWorld-p.x>=margin &&
			p.y-map.originY>=margin && map.originY+map.heightWorld-p.y>=margin;
	};
	// Armed regression: the former 4096-unit centre-ray focus placed this atlas at
	// x=[2048,6144], excluding water that is plainly visible below a horizontal camera.
	const Point horizontal[5]={{1,0,0},{1,-.5,-.5},{1,.5,-.5},{1,-.5,.5},{1,.5,.5}};
	const auto concrete=Choose({0,0,30},horizontal,0);
	CHECK(concrete.originX==-1024 && concrete.originY==-2048);
	CHECK(inside(concrete,{0,0,0},128));
	CHECK(inside(concrete,{60,-30,0},128) && inside(concrete,{60,30,0},128));
	for(double yaw:{0.0,.37,1.5707963267948966,2.4,3.141592653589793,-2.1})
	for(double pitch:{-.001,0.0,.1,.3,.6})
	for(double aspect:{9.0/16,16.0/9,21.0/9,32.0/9}) {
		const Point camera={-347.25,891.125,34};const double level=4;
		const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
		Point rays[5]={{cy*cp,sy*cp,sp}};unsigned at=1;
		for(double vertical:{-1.0,1.0}) for(double horizontalSide:{-1.0,1.0})
			rays[at++]={cy*cp-horizontalSide*.5*aspect*sy-vertical*.5*cy*sp,
				sy*cp+horizontalSide*.5*aspect*cy-vertical*.5*sy*sp,sp+vertical*.5*cp};
		const auto map=Choose(camera,rays,level);
		CHECK(map.widthWorld==4096 && map.heightWorld==4096 && map.widthPixels==512 && map.heightPixels==512);
		CHECK(inside(map,{camera.x,camera.y,level},128));
		CHECK(std::fmod(map.originX,64)==0 && std::fmod(map.originY,64)==0);
		unsigned nearHits=0;
		for(unsigned i=1;i<5;++i) if(rays[i].z<0) {
			const double time=(level-camera.z)/rays[i].z;
			const Point hit={camera.x+rays[i].x*time,camera.y+rays[i].y*time,level};
			// These lower-ray intersections all fit together with a substantial margin.
			CHECK(std::hypot(hit.x-camera.x,hit.y-camera.y)<1024);
			CHECK(inside(map,hit,128));++nearHits;
		}
		CHECK(nearHits==(pitch<.5?2u:0u));
	}
	// A very distant centre/upper hit must not displace finite nearby lower hits.
	const Point farRays[5]={{1,0,-.000001},{1,-.4,-.2},{1,.4,-.2},{1,-.4,-.00001},{1,.4,-.00001}};
	const auto prioritized=Choose({0,0,30},farRays,0);
	CHECK(inside(prioritized,{150,-60,0},128) && inside(prioritized,{150,60,0},128));
	CHECK(inside(prioritized,{0,0,0},128));
	// Below-water and fully upward views remain finite and bounded without inventing
	// negative-distance plane hits as part of the near-water footprint.
	const auto below=Choose({0,0,-30},horizontal,0);
	CHECK(inside(below,{60,-30,0},128) && inside(below,{60,30,0},128));
	// A paused pan across two snap boundaries reconstructs exactly the same world trail
	// in the overlap. This exercises real history rasterization with the chosen windows.
	WaterFoamHistory::History history;
	for(unsigned i=0;i<=4;++i) {
		history.Advance(i*.25);history.Observe(1,foamTestHull(float(i)*7.5f,0,30,0),i*.25);
	}
	const auto first=Choose({0,0,37},horizontal,7),panned=Choose({128,0,37},horizontal,7);
	CHECK(panned.originX-first.originX==128 && panned.originY==first.originY);
	std::vector<std::uint32_t> a,b;CHECK(history.BuildAtlas(first,7,a) && history.BuildAtlas(panned,7,b));
	bool identical=true;unsigned covered=0;
	for(unsigned y=0;y<512;++y) for(unsigned x=16;x<512;++x) {
		identical=identical && a[y*512+x]==b[y*512+x-16];covered+=a[y*512+x]!=0;
	}
	CHECK(identical && covered>0 && history.Time()==1.0);
}

TEST(water_foam_stern_spread_is_size_bounded_and_emission_remains_time_integrated)
{
	using namespace WaterFoamHistory;
	Sample smallHull,largeHull,tinyHull,hugeHull;
	auto hull=foamTestHull(0,0,30,0);CHECK(MakeSample(hull,0,smallHull));
	hull.data[1][3]=74;CHECK(MakeSample(hull,0,largeHull));
	hull.data[1][3]=1;CHECK(MakeSample(hull,0,tinyHull));
	hull.data[1][3]=4096;CHECK(MakeSample(hull,0,hugeHull));
	CHECK(std::abs(smallHull.width[0]-2.8)<1e-12 && tinyHull.width[0]==2.5);
	CHECK(largeHull.width[0]>29 && largeHull.width[0]<30 && hugeHull.width[0]==30);
	CHECK(smallHull.width[0]/smallHull.halfWidth>largeHull.width[0]/largeHull.halfWidth);
	CHECK(smallHull.width[1]==.65 && largeHull.width[1]==3 && hugeHull.width[1]==3);
	CHECK(std::abs(smallHull.rate[0]-2*(.8+.25*std::sqrt(30.0)))<1e-12);
	CHECK(std::abs(smallHull.rate[1]/smallHull.rate[0]-.22)<1e-12);
	// A short live section must accumulate by elapsed dose time. Repeating the same
	// render cannot stamp another full-opacity blob, even with the stronger response.
	const Mapping map={-12.5,-.5,1,1,1,1};History history;
	history.Observe(1,foamTestHull(0,0,30,0),0);
	std::vector<std::uint32_t> initial,first,duplicate,second;
	CHECK(history.BuildAtlas(map,7,initial) && initial[0]==0);
	history.Advance(.01);history.Observe(1,foamTestHull(.3f,0,30,0),.01);
	CHECK(history.BuildAtlas(map,7,first));
	for(unsigned pass=0;pass<20;++pass)
		CHECK(!history.Observe(1,foamTestHull(.3f,0,30,0),.01));
	CHECK(history.BuildAtlas(map,7,duplicate) && duplicate==first);
	history.Advance(.02);history.Observe(1,foamTestHull(.6f,0,30,0),.02);
	CHECK(history.BuildAtlas(map,7,second) && history.SegmentCount()==0);
	const double coverage1=((first[0]>>16)&255)/255.0,coverage2=((second[0]>>16)&255)/255.0;
	CHECK(coverage1>0 && coverage2<.2);
	const double dose1=-std::log(1-coverage1),dose2=-std::log(1-coverage2);
	CHECK(dose2>1.8*dose1 && dose2<2.2*dose1);
}

TEST(water_foam_history_fixed_time_sampling_is_frame_rate_invariant)
{
	using namespace WaterFoamHistory;
	std::vector<Segment> reference;std::vector<std::uint32_t> referenceAtlas;
	const Mapping mapping={-32,-32,192,64,192,64};
	for(unsigned fps:{20u,30u,60u,120u}) {
		History history;
		for(unsigned frame=0;frame<=fps*4;++frame) {
			const double time=double(frame)/fps;history.Advance(time);
			history.Observe(1,foamTestHull(float(30*time),0,30,0),time);
		}
		CHECK(history.SegmentCount()==16 && history.TrackedCount()==1);
		std::vector<std::uint32_t> atlas;CHECK(history.BuildAtlas(mapping,7,atlas));
		if(reference.empty()) {reference=history.Segments();referenceAtlas=atlas;}
		for(size_t i=0;i<reference.size();++i) {
			const auto &a=reference[i],&b=history.Segments()[i];
			CHECK(a.begin.time==b.begin.time && a.end.time==b.end.time);
			for(int lane=0;lane<3;++lane) {
				CHECK(Distance(a.begin.lanes[lane],b.begin.lanes[lane])<0.00001);
				CHECK(Distance(a.end.lanes[lane],b.end.lanes[lane])<0.00001);
			}
		}
		unsigned total=0;
		for(size_t i=0;i<atlas.size();++i) for(unsigned shift:{0u,8u,16u}) {
			CHECK(std::abs(int((atlas[i]>>shift)&255)-int((referenceAtlas[i]>>shift)&255))<=1);
			if(shift==16) total+=(atlas[i]>>shift)&255;
		}
		CHECK(total>10000); // Armed against an empty atlas satisfying timing invariance.
		for(int pass=0;pass<100;++pass) {
			CHECK(!history.Advance(4));CHECK(!history.Observe(1,foamTestHull(120,0,30,0),4));
		}
		std::vector<std::uint32_t> paused;CHECK(history.BuildAtlas(mapping,7,paused));CHECK(atlas==paused);
	}
}

TEST(water_foam_history_turns_preserve_old_world_paths_and_stops_leave_fading_remnants)
{
	using namespace WaterFoamHistory;
	History history;
	for(unsigned frame=0;frame<=120;++frame) {
		const double t=frame/30.0,angle=t*0.4;
		history.Advance(t);history.Observe(1,foamTestHull(float(70*std::sin(angle)),float(70*(1-std::cos(angle))),
			float(28*std::cos(angle)),float(28*std::sin(angle)),float(angle)),t);
	}
	CHECK(history.SegmentCount()==16);
	const Segment first=history.Segments().front();const Segment last=history.Segments().back();
	CHECK(Distance(first.begin.lanes[0],{-12,0})<1e-7);
	CHECK(last.end.lanes[0].y>55); // The history follows the curve, not one current-heading ray.
	const auto stopped=foamTestHull(float(70*std::sin(1.6)),float(70*(1-std::cos(1.6))),30,0,1.6f);
	for(unsigned frame=121;frame<=150;++frame) {const double t=frame/30.0;history.Advance(t);history.Observe(1,stopped,t);}
	const size_t stoppedCount=history.SegmentCount();CHECK(stoppedCount>=16 && stoppedCount<=17);
	CHECK(Distance(history.Segments().front().begin.lanes[0],first.begin.lanes[0])==0);
	const Mapping mapping={-64,-64,192,192,192,192};std::vector<std::uint32_t> fresh,old;
	CHECK(history.BuildAtlas(mapping,7,fresh));
	for(unsigned frame=151;frame<=300;++frame) {const double t=frame/30.0;history.Advance(t);history.Observe(1,stopped,t);}
	CHECK(history.SegmentCount()==stoppedCount);CHECK(history.BuildAtlas(mapping,7,old));
	unsigned freshDensity=0,oldDensity=0,freshChurn=0,oldChurn=0;
	for(size_t i=0;i<fresh.size();++i) {freshDensity+=(fresh[i]>>16)&255;oldDensity+=(old[i]>>16)&255;
		freshChurn+=fresh[i]&255;oldChurn+=old[i]&255;}
	CHECK(oldDensity>0 && oldDensity<freshDensity && oldChurn<freshChurn);
	history.Advance(30);CHECK(history.SegmentCount()==0);CHECK(history.BuildAtlas(mapping,7,old));
	for(auto pixel:old) CHECK(pixel==0);
}

TEST(water_foam_history_held_logic_transforms_do_not_reduce_emission_at_high_render_fps)
{
	using namespace WaterFoamHistory;
	const Mapping map={-32,-32,256,64,256,64};
	std::vector<std::uint32_t> reference;unsigned referenceDensity=0;
	for(unsigned fps:{30u,60u,120u}) {
		History history;
		for(unsigned frame=0;frame<=fps*6;++frame) {
			const double time=double(frame)/fps;
			const unsigned logicFrame=(frame*30)/fps;
			history.Advance(time);history.Observe(1,foamTestHull(float(logicFrame),0,30,0),time);
		}
		CHECK(history.SegmentCount()==24);
		std::vector<std::uint32_t> atlas;CHECK(history.BuildAtlas(map,7,atlas));unsigned density=0;
		for(auto pixel:atlas) density+=(pixel>>16)&255;
		if(reference.empty()) {reference=atlas;referenceDensity=density;}
		// Different first render phases locate a fixed-time sample at most half a logic
		// step apart. Integrated density stays close, and the trail remains in the same cells.
		CHECK(density>0 && std::abs(double(density)-referenceDensity)<referenceDensity*0.04);
		for(size_t i=0;i<atlas.size();++i)
			CHECK(std::abs(int((atlas[i]>>16)&255)-int((reference[i]>>16)&255))<=12);
		const size_t moving=history.SegmentCount();
		for(unsigned frame=fps*6+1;frame<=fps*8;++frame) {
			const double time=double(frame)/fps;history.Advance(time);
			history.Observe(1,foamTestHull(180,0,30,0),time); // Deliberately stale filtered speed.
		}
		CHECK(history.SegmentCount()==moving);
	}
	History neverMoved;
	for(unsigned frame=0;frame<=120;++frame) {
		const double time=frame/30.0;neverMoved.Advance(time);
		neverMoved.Observe(1,foamTestHull(0,0,30,0),time);
	}
	CHECK(neverMoved.SegmentCount()==0); // A new stationary object with stale speed cannot emit.
}

TEST(water_foam_history_live_tail_closes_fast_stern_gaps_without_duplicate_sections)
{
	using namespace WaterFoamHistory;
	const Mapping map={-32,-16,160,32,320,64};std::vector<std::uint32_t> reference;
	for(unsigned fps:{30u,60u,120u}) {
		History history;
		for(unsigned frame=0;double(frame)/fps<0.37;++frame) {
			const double time=double(frame)/fps;history.Advance(time);
			history.Observe(1,foamTestHull(float(time*250),0,250,0),time);
		}
		history.Advance(.37);history.Observe(1,foamTestHull(92.5f,0,250,0),.37);
		CHECK(history.SegmentCount()==1); // The live .25→.37 tail remains uncommitted.
		std::vector<std::uint32_t> atlas;CHECK(history.BuildAtlas(map,7,atlas));
		const unsigned sternX=unsigned((80.5-map.originX)/map.widthWorld*map.widthPixels);
		CHECK(((atlas[32*320+sternX]>>16)&255)>3); // Armed: committed-only history ends30 units earlier.
		if(reference.empty()) reference=atlas;
		for(size_t i=0;i<atlas.size();++i) for(unsigned shift:{0u,8u,16u})
			CHECK(std::abs(int((atlas[i]>>shift)&255)-int((reference[i]>>shift)&255))<=1);
		CHECK(!history.Advance(.37));CHECK(!history.Observe(1,foamTestHull(92.5f,0,250,0),.37));
		std::vector<std::uint32_t> paused;CHECK(history.BuildAtlas(map,7,paused));CHECK(atlas==paused);
		// Stopping preserves the dose's true end time. Committing that already-visible partial
		// at the next fixed boundary cannot increase coverage or duplicate its contribution.
		history.Advance(.39);history.Observe(1,foamTestHull(92.5f,0,250,0),.39);
		history.Advance(.5);std::vector<std::uint32_t> before,after;
		CHECK(history.BuildAtlas(map,7,before));history.Observe(1,foamTestHull(92.5f,0,250,0),.5);
		CHECK(history.BuildAtlas(map,7,after));CHECK(before==after && history.SegmentCount()==2);
		CHECK(history.Segments().back().end.time==.37);
		for(int step=3;step<=8;++step) {const double time=step*.25;history.Advance(time);
			history.Observe(1,foamTestHull(92.5f,0,250,0),time);}
		CHECK(history.SegmentCount()==2 && history.Segments().back().end.time==.37);
	}
	for(double time:{.01,.06,.12,.24,.25,.26}) {
		History history;history.Observe(1,foamTestHull(0,0,250,0),0);
		CHECK(!history.HasFoam());
		history.Advance(time);history.Observe(1,foamTestHull(float(time*250),0,250,0),time);
		CHECK(history.HasFoam());
		std::vector<std::uint32_t> atlas;CHECK(history.BuildAtlas(map,7,atlas));
		const unsigned x=unsigned((time*250-12-map.originX)/map.widthWorld*map.widthPixels);
		CHECK(((atlas[32*320+x]>>16)&255)>1);
	}
}

TEST(water_foam_history_true_motion_ignores_contact_center_bobbing_after_stop)
{
	using namespace WaterFoamHistory;
	History steady,tilting;const Mapping map={-32,-16,128,32,256,64};
	for(double time:{0.0,.1,.2,.3,.37}) {
		const Point origin={time*30,0};const auto hull=foamTestHull(float(origin.x),0,30,0);
		steady.Advance(time);tilting.Advance(time);
		steady.Observe(1,hull,time,true,false,&origin);tilting.Observe(1,hull,time,true,false,&origin);
	}
	CHECK(steady.SegmentCount()==1 && tilting.SegmentCount()==1);
	const Point stoppedOrigin={11.1,0};
	for(unsigned step=4;step<=30;++step) {
		const double time=step*.1;const float offset=float(std::sin(time*13)*2);
		steady.Advance(time);tilting.Advance(time);
		steady.Observe(1,foamTestHull(11.1f,0,30,0),time,true,false,&stoppedOrigin);
		// The actual contour and its lanes continue moving as the hull tilts, while
		// the velocity filter deliberately retains its previous30-unit/s speed.
		tilting.Observe(1,foamTestHull(11.1f+offset,offset*.4f,30,0),time,true,false,&stoppedOrigin);
		std::vector<std::uint32_t> a,b;CHECK(steady.BuildAtlas(map,7,a) && tilting.BuildAtlas(map,7,b));
		CHECK(a==b && steady.SegmentCount()==tilting.SegmentCount());
	}
	CHECK(tilting.SegmentCount()==2 && tilting.Segments().back().end.time==.37);
	// Without the independent motion coordinate, this residual-speed contour motion
	// is incorrectly classified as translation and emits new stationary foam.
	History oldBehavior;
	for(unsigned step=0;step<=10;++step) {
		const double time=step*.1;oldBehavior.Advance(time);
		oldBehavior.Observe(1,foamTestHull(float(std::sin(time*13)*2),0,30,0),time);
	}
	CHECK(oldBehavior.SegmentCount()>0);
	const Point invalid={NAN,0};tilting.Advance(3.1);
	CHECK(!tilting.Observe(1,foamTestHull(11.1f,0,30,0),3.1,true,false,&invalid));
	CHECK(tilting.TrackedCount()==0 && tilting.SegmentCount()==2);
}

TEST(water_foam_history_removal_and_reentry_keep_the_existing_partial_tail)
{
	using namespace WaterFoamHistory;
	History history;const Mapping map={-32,-16,128,32,128,32};
	history.Observe(1,foamTestHull(0,0,100,0),0);
	history.Advance(.12);history.Observe(1,foamTestHull(12,0,100,0),.12);
	std::vector<std::uint32_t> before,after;CHECK(history.BuildAtlas(map,7,before));
	CHECK(history.SegmentCount()==0);
	history.Observe(1,foamTestHull(12,0,100,0),.12,false);
	CHECK(history.BuildAtlas(map,7,after));CHECK(before==after);
	CHECK(history.SegmentCount()==1 && history.TrackedCount()==0);
	history.Advance(.2);CHECK(!history.Observe(1,foamTestHull(10000,0,100,0),.2));
	CHECK(history.SegmentCount()==1); // A new anchor cannot connect the retained trail to a teleport.
	history.Advance(25);CHECK(history.SegmentCount()==0);
	CHECK(!history.Observe(1,foamTestHull(10001,0,100,0),25));CHECK(history.SegmentCount()==0);
}

TEST(water_foam_history_breaks_spawns_teleports_hidden_gaps_levels_and_reverse_jumps)
{
	using namespace WaterFoamHistory;
	History history;CHECK(!history.Observe(7,foamTestHull(0,0,30,0),0));
	for(int i=1;i<=4;++i) {history.Advance(i*.25);history.Observe(7,foamTestHull(i*7.5f,0,30,0),i*.25);}
	CHECK(history.SegmentCount()==4);const size_t before=history.SegmentCount();
	history.Advance(1.1); // Omitted observation breaks a connection even if the gap is short.
	history.Advance(1.25);CHECK(!history.Observe(7,foamTestHull(37.5f,0,30,0),1.25));
	history.Advance(1.5);CHECK(!history.Observe(7,foamTestHull(10000,0,30,0),1.5));
	history.Advance(1.75);CHECK(!history.Observe(7,foamTestHull(10001,0,30,0),1.75,true,true));
	history.Advance(2);CHECK(!history.Observe(7,foamTestHull(10002,0,30,0,0,9),2));
	history.Advance(2.25);CHECK(!history.Observe(7,foamTestHull(10003,0,30,0,0,9),2.25,false));
	history.Advance(2.5);CHECK(!history.Observe(7,foamTestHull(10004,0,30,0,0,9),2.5));
	CHECK(history.SegmentCount()==before);
	// A reversal moves the chosen stern to the other end of this hull. Do not paint the
	// interpolation across its deck as a long wake, even when the hull center barely moved.
	history.Advance(2.75);CHECK(!history.Observe(7,foamTestHull(10003,0,-30,0,0,9),2.75));
	CHECK(history.SegmentCount()==before);
	history.Advance(4);CHECK(!history.Observe(7,foamTestHull(10000,0,-30,0,0,9),4));
	history.Advance(0.5);CHECK(history.SegmentCount()==0 && history.TrackedCount()==0);
	CHECK(!history.Observe(7,foamTestHull(12,0,30,0),0.5));
	History fast;fast.Observe(1,foamTestHull(0,0,250,0),0);
	fast.Advance(.25);CHECK(fast.Observe(1,foamTestHull(400,0,250,0),.25)); // Accelerated native simulation is allowed.
}

TEST(water_foam_atlas_world_mapping_levels_and_inputs_are_bounded)
{
	using namespace WaterFoamHistory;
	History history;
	for(int i=0;i<=8;++i) {history.Advance(i*.25);history.Observe(1,foamTestHull(i*4.0f,0,16,0),i*.25);}
	const Mapping wide={-64,-64,128,128,128,128},crop={-32,-32,64,64,64,64};
	std::vector<std::uint32_t> a,b;CHECK(history.BuildAtlas(wide,7,a));CHECK(history.BuildAtlas(crop,7,b));
	for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) {
		CHECK(b[y*64+x]==a[(y+32)*128+x+32]);
		const unsigned pixel=b[y*64+x],r=(pixel>>16)&255,g=(pixel>>8)&255,churn=pixel&255;
		CHECK((pixel>>24)==0 && g<=r && churn<=r);
	}
	CHECK(history.BuildAtlas(wide,8,b));for(auto pixel:b) CHECK(pixel==0);
	CHECK(history.BuildAtlas({10000,10000,128,128,128,128},7,b));for(auto pixel:b) CHECK(pixel==0);
	CHECK(!history.BuildAtlas({0,0,1,1,513,512},7,b) && b.empty());
	CHECK(!history.BuildAtlas({0,0,0,1,1,1},7,b) && b.empty());
	CHECK(!history.BuildAtlas({NAN,0,1,1,1,1},7,b) && b.empty());
	auto bad=foamTestHull(0,0,0,0);bad.data[2][0]=NAN;
	history.Advance(2.25);CHECK(!history.Observe(1,bad,2.25));CHECK(history.TrackedCount()==0);
	history.Advance(NAN);CHECK(history.SegmentCount()==0 && history.TrackedCount()==0);
	History stationary;
	for(int i=0;i<=120;++i) {stationary.Advance(i*.25);stationary.Observe(1,foamTestHull(0,0,0,0),i*.25);}
	CHECK(stationary.SegmentCount()==0);
}

TEST(water_foam_history_capacity_is_bounded_under_many_vessels)
{
	using namespace WaterFoamHistory;
	History history;
	for(unsigned id=0;id<MaxEmitters+10;++id) history.Observe(id,foamTestHull(0,float(id*20),30,0),0);
	CHECK(history.TrackedCount()==MaxEmitters);
	history.Clear();
	for(unsigned frame=0;frame<=96;++frame) {
		const double time=frame*.25;history.Advance(time);
		for(unsigned id=0;id<16;++id) history.Observe(id,foamTestHull(float(time*30),float(id*20),30,0),time);
		CHECK(history.SegmentCount()<=MaxSegments && history.TrackedCount()<=MaxEmitters);
	}
	CHECK(history.SegmentCount()==MaxSegments);
	for(const Segment &s:history.Segments()) CHECK(s.end.time>=history.Time()-Lifetime && s.begin.time<s.end.time);
}

// Recorded wake orientation is an axial moment, not current vessel heading or
// wave travel direction. Quantization tolerances below follow the ARGB8 packing.
struct FoamDirectionDecoded {
    unsigned alpha=0;
    double x=0,y=0,width=0;
};
static FoamDirectionDecoded foamDecodeDirection(std::uint32_t p)
{
    FoamDirectionDecoded d;d.alpha=p>>24;
    if(d.alpha) {d.x=(2.0*((p>>16)&255)-d.alpha)/d.alpha;
        d.y=(2.0*((p>>8)&255)-d.alpha)/d.alpha;
        d.width=32.0*(p&255)/d.alpha;}
    return d;
}
static bool foamDirectionAtlas(const WaterFoamHistory::History &history,
    const WaterFoamHistory::Mapping &mapping,float level,
    std::vector<std::uint32_t> &base,std::vector<std::uint32_t> &direction)
{
    std::vector<std::uint32_t> legacy;
    const bool oldOk=history.BuildAtlas(mapping,level,legacy);
    const bool newOk=history.BuildAtlas(mapping,level,base,&direction);
    CHECK(oldOk==newOk && legacy==base); // The extra output cannot alter even one first-tile byte.
    CHECK(direction.size()==base.size());
    if(!newOk || direction.size()!=base.size()) return false;
    for(size_t i=0;i<base.size();++i) {
        const unsigned opacity=(base[i]>>16)&255,a=direction[i]>>24;
        CHECK(a==opacity && (base[i]>>24)==0);
        CHECK(((direction[i]>>16)&255)<=a && ((direction[i]>>8)&255)<=a && (direction[i]&255)<=a);
        if(!a) CHECK(direction[i]==0);
    }
    return true;
}

// Independent direct quadrature of the deposited radial quartic. It does not call
// IntegrateKernel, use its antiderivative/mean, or reproduce its float accumulator.
// The radius-at-nearest-point and endpoint amplitude interpolation are deliberately
// the established history model, rather than an assertion of exact fluid physics.
struct FoamDirectionOracle {double density=0,x=0,y=0,width=0;};
static FoamDirectionOracle foamDirectionOracle(const WaterFoamHistory::History &history,
    WaterFoamHistory::Point p,unsigned steps=1024)
{
    using namespace WaterFoamHistory;
    FoamDirectionOracle out;
    auto clip=[](double x,double a,double b){return (std::max)(a,(std::min)(b,x));};
    for(const Segment &s:history.Segments()) for(unsigned lane=0;lane<3;++lane) {
        const double dx=s.end.lanes[lane].x-s.begin.lanes[lane].x;
        const double dy=s.end.lanes[lane].y-s.begin.lanes[lane].y;
        const double lengthSquared=dx*dx+dy*dy;
        const double nearest=lengthSquared>1e-20?clip(((p.x-s.begin.lanes[lane].x)*dx+
            (p.y-s.begin.lanes[lane].y)*dy)/lengthSquared,0,1):.5;
        double amplitude[2],radius[2];
        for(unsigned end=0;end<2;++end) {
            const Sample &sample=end?s.end:s.begin;
            const double age=clip(history.Time()-sample.time,0,24),w=sample.width[lane];
            const double aged=w+((lane==0?.28:.10)+std::sqrt(w)*(lane==0?.12:.06))*age;
            const double u=clip((age-20)/4,0,1),fade=1-u*u*(3-2*u);
            radius[end]=2.5*aged;
            amplitude[end]=sample.rate[lane]*std::exp(-age/(lane==0?7:3.5))*fade*w*w/(aged*aged);
        }
        const double r=radius[0]+(radius[1]-radius[0])*nearest;
        double density=0,width=0;
        for(unsigned k=0;k<steps;++k) {
            const double t=(k+.5)/steps;
            const double x=p.x-s.begin.lanes[lane].x-t*dx,y=p.y-s.begin.lanes[lane].y-t*dy;
            const double radial=(std::max)(0.0,1-(x*x+y*y)/(r*r));
            const double dose=(s.end.time-s.begin.time)*radial*radial/steps;
            const double a=amplitude[0]*(1-t),b=amplitude[1]*t;
            density+=dose*(a+b);
            width+=dose*(a*s.begin.width[lane]+b*s.end.width[lane]);
        }
        out.density+=density;out.width+=width;
        if(lengthSquared>1e-12) {out.x+=density*(dx*dx-dy*dy)/lengthSquared;
            out.y+=density*2*dx*dy/lengthSquared;}
    }
    if(out.density>0) {out.x/=out.density;out.y/=out.density;out.width/=out.density;}
    return out;
}
static void foamCheckDirectionOracle(std::uint32_t pixel,const FoamDirectionOracle &expected)
{
    const FoamDirectionDecoded d=foamDecodeDirection(pixel);
    CHECK(d.alpha>10 && expected.density>0);
    if(!d.alpha || expected.density<=0) return;
    CHECK_NEAR(double(d.alpha),255*(1-std::exp(-expected.density)),.501);
    // R and A round independently: |decoded moment error| <= (1+.5*(1+|q|))/A.
    CHECK_NEAR(d.x,expected.x,2.0/d.alpha+2e-5);
    CHECK_NEAR(d.y,expected.y,2.0/d.alpha+2e-5);
    CHECK_NEAR(d.width,expected.width,16*(1+expected.width/32)/d.alpha+2e-5);
}

TEST(water_foam_direction_uses_recorded_translation_and_is_axial_under_reversal)
{
    using namespace WaterFoamHistory;
    const Mapping map={-96,-96,192,192,96,96};
    struct Case {float vx,vy,heading;double x,y;};
    const Case cases[]={{30,0,0,1,0},{-30,0,0,1,0},{0,30,0,-1,0},
        {0,-30,0,-1,0},{24,24,0,0,1},{-24,-24,0,0,1},{24,-24,0,0,-1}};
    for(const Case &c:cases) {
        History history;
        for(unsigned i=0;i<=8;++i) {const double t=i*.25;history.Advance(t);
            history.Observe(1,foamTestHull(float(c.vx*t),float(c.vy*t),c.vx,c.vy,c.heading),t);}
        CHECK(history.SegmentCount()==8);
        std::vector<std::uint32_t> base,direction;CHECK(foamDirectionAtlas(history,map,7,base,direction));
        unsigned active=0;
        for(auto p:direction) {const auto d=foamDecodeDirection(p);if(d.alpha<20)continue;++active;
            CHECK_NEAR(d.x,c.x,2.0/d.alpha+1e-6);CHECK_NEAR(d.y,c.y,2.0/d.alpha+1e-6);
            CHECK(d.width>=.65-32.0/d.alpha && d.width<=2.8+32.0/d.alpha);}
        CHECK(active>20);
        // All hulls face +X, so diagonal/broadside cases arm a current-heading substitution.
        if(c.vy!=0) CHECK(std::abs(c.x-1)+std::abs(c.y)>.9);
    }
}

TEST(water_foam_direction_crossings_cancel_or_retain_density_weighted_confidence)
{
    using namespace WaterFoamHistory;
    const Mapping sample={-.5,-.5,1,1,1,1};
    for(unsigned horizontal:{1u,3u}) {
        History history;
        for(unsigned step=0;step<=4;++step) {const double t=step*.25;history.Advance(t);
            for(unsigned id=0;id<horizontal;++id)
                history.Observe(id,foamTestHull(float(30*t-3),0,30,0),t);
            auto vertical=foamTestHull(0,float(30*t-3),0,30);
            vertical.data[1][0]=0;vertical.data[1][1]=1;
            history.Observe(10,vertical,t);
        }
        CHECK(history.SegmentCount()==4*(horizontal+1));
        std::vector<std::uint32_t> base,direction;CHECK(foamDirectionAtlas(history,sample,7,base,direction));
        if(direction.empty())continue;const auto d=foamDecodeDirection(direction[0]);CHECK(d.alpha>100);
        if(!d.alpha)continue;
        const double expected=(double(horizontal)-1)/(horizontal+1);
        CHECK_NEAR(d.x,expected,2.0/d.alpha+1e-6);CHECK_NEAR(d.y,0,2.0/d.alpha+1e-6);
        if(horizontal==1) CHECK(std::hypot(d.x,d.y)<.03); // Isotropic fallback, not an arbitrary diagonal.
        else CHECK(std::hypot(d.x,d.y)>.45 && std::hypot(d.x,d.y)<.55);
        foamCheckDirectionOracle(direction[0],foamDirectionOracle(history,{0,0}));
    }
}

TEST(water_foam_direction_curves_and_changing_width_match_independent_deposition)
{
    using namespace WaterFoamHistory;
    History history;
    for(unsigned i=0;i<=16;++i) {
        const double t=i*.25,a=t*.4;const float width=float(4+2*t);
        auto hull=foamTestHull(float(70*std::sin(a)),float(70*(1-std::cos(a))),
            float(28*std::cos(a)),float(28*std::sin(a)),float(a));
        hull.data[1][3]=width;
        for(unsigned j=1;j<8;j+=2) hull.data[4+j/4][j%4]*=width/4;
        history.Advance(t);history.Observe(1,hull,t);
    }
    CHECK(history.SegmentCount()==16);if(history.SegmentCount()!=16)return;
    unsigned headingControl=0,oldWidthControl=0;
    for(unsigned index:{1u,5u,10u,14u}) for(unsigned lane:{0u,1u}) {
        const Segment &s=history.Segments()[index];
        const Point p={(s.begin.lanes[lane].x+s.end.lanes[lane].x)*.5,
            (s.begin.lanes[lane].y+s.end.lanes[lane].y)*.5};
        const Mapping map={p.x-.5,p.y-.5,1,1,1,1};
        std::vector<std::uint32_t> base,direction;CHECK(foamDirectionAtlas(history,map,7,base,direction));
        if(direction.empty())continue;
        const auto expected=foamDirectionOracle(history,p,2048);
        foamCheckDirectionOracle(direction[0],expected);
        // Coarser independent quadrature convergence makes the .501-byte coverage
        // oracle margin meaningful, rather than hiding arbitrary numerical error.
        const auto coarse=foamDirectionOracle(history,p,1024);
        CHECK_NEAR(coarse.density,expected.density,1e-7);
        CHECK_NEAR(coarse.x,expected.x,1e-7);CHECK_NEAR(coarse.y,expected.y,1e-7);
        CHECK_NEAR(coarse.width,expected.width,1e-7);
        if(std::hypot(expected.x-std::cos(3.2),expected.y-std::sin(3.2))>.5)++headingControl;
        if(std::abs(expected.width-history.Segments().back().end.width[lane])>.4)++oldWidthControl;
    }
    CHECK(headingControl>=2 && oldWidthControl>=2);
    History turned=history;
    const auto last=history.Segments().back().end;
    for(unsigned i=17;i<=20;++i) {const double t=i*.25;history.Advance(t);turned.Advance(t);
        history.Observe(1,foamTestHull(float(last.center.x),float(last.center.y),30,0,1.6f),t);
        turned.Observe(1,foamTestHull(float(last.center.x),float(last.center.y),30,0,2.2f),t);}
    std::vector<std::uint32_t> a,b,da,db;
    const Mapping map={-32,-32,160,160,80,80};
    CHECK(foamDirectionAtlas(history,map,7,a,da) && foamDirectionAtlas(turned,map,7,b,db));
    CHECK(a==b && da==db); // Rotating the stopped hull cannot rotate its retained path.
}

TEST(water_foam_direction_zero_length_lane_has_no_invented_axis)
{
    using namespace WaterFoamHistory;
    History history;const auto hull=foamTestHull(0,0,30,0);
    const Point p0={0,0},p1={1,0};
    history.Observe(1,hull,0,true,false,&p0);history.Advance(.25);
    history.Observe(1,hull,.25,true,false,&p1);
    CHECK(history.SegmentCount()==1);
    // Deliberate synthetic pose: motion origin advances while every contour lane
    // stays fixed. Radial dose remains possible, but there is no recorded tangent.
    std::vector<std::uint32_t> base,direction;
    CHECK(foamDirectionAtlas(history,{-12.5,-.5,1,1,1,1},7,base,direction));
    if(direction.empty())return;const auto d=foamDecodeDirection(direction[0]);CHECK(d.alpha>20);
    if(d.alpha) {CHECK_NEAR(d.x,0,2.0/d.alpha);CHECK_NEAR(d.y,0,2.0/d.alpha);}
    foamCheckDirectionOracle(direction[0],foamDirectionOracle(history,{-12,0}));
}

TEST(water_foam_direction_time_sampling_live_tail_pause_and_commit_are_stable)
{
    using namespace WaterFoamHistory;
    const Mapping map={-32,-16,160,32,160,32};std::vector<std::uint32_t> reference;
    for(unsigned fps:{20u,30u,60u,120u}) {
        History history;
        for(unsigned frame=0;double(frame)/fps<.37;++frame) {const double t=double(frame)/fps;
            history.Advance(t);history.Observe(1,foamTestHull(float(250*t),0,250,0),t);}
        history.Advance(.37);history.Observe(1,foamTestHull(92.5f,0,250,0),.37);
        CHECK(history.SegmentCount()==1);
        std::vector<std::uint32_t> base,direction;
        CHECK(foamDirectionAtlas(history,map,7,base,direction));
        if(direction.size()!=160u*32u)return;
        CHECK((direction[16*160+112]>>24)>3); // Live stern lies near x80.5, beyond committed section.
        if(reference.empty())reference=direction;
        for(size_t i=0;i<direction.size();++i)for(unsigned shift:{0u,8u,16u,24u})
            CHECK(std::abs(int((direction[i]>>shift)&255)-int((reference[i]>>shift)&255))<=1);
        CHECK(!history.Advance(.37));CHECK(!history.Observe(1,foamTestHull(92.5f,0,250,0),.37));
        std::vector<std::uint32_t> b,d;CHECK(foamDirectionAtlas(history,map,7,b,d));CHECK(base==b && direction==d);
        history.Advance(.39);history.Observe(1,foamTestHull(92.5f,0,250,0),.39);
        history.Advance(.5);CHECK(foamDirectionAtlas(history,map,7,base,direction));
        history.Observe(1,foamTestHull(92.5f,0,250,0),.5);
        CHECK(foamDirectionAtlas(history,map,7,b,d));CHECK(base==b && direction==d);
        CHECK(history.SegmentCount()==2 && history.Segments().back().end.time==.37);
    }
}

TEST(water_foam_direction_pan_empty_level_invalid_and_aging_are_bounded)
{
    using namespace WaterFoamHistory;
    History history;
    for(unsigned i=0;i<=8;++i) {history.Advance(i*.25);history.Observe(1,foamTestHull(i*4.f,0,16,0),i*.25);}
    std::vector<std::uint32_t> a,b,da,db;
    CHECK(foamDirectionAtlas(history,{-64,-64,128,128,128,128},7,a,da));
    CHECK(foamDirectionAtlas(history,{-32,-32,64,64,64,64},7,b,db));
    if(a.size()!=128u*128u || da.size()!=a.size() || b.size()!=64u*64u || db.size()!=b.size())return;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        CHECK(b[y*64+x]==a[(y+32)*128+x+32]);CHECK(db[y*64+x]==da[(y+32)*128+x+32]);}
    const Mapping map={-32,-16,128,32,64,16};
    CHECK(foamDirectionAtlas(history,map,8,b,db));for(auto p:db)CHECK(p==0);
    CHECK(foamDirectionAtlas(history,{10000,10000,128,32,64,16},7,b,db));for(auto p:db)CHECK(p==0);
    for(const Mapping invalid:std::initializer_list<Mapping>{{0,0,1,1,513,1},{0,0,0,1,1,1},
        {NAN,0,1,1,1,1},{0,0,1,1,0,1},{0,0,1,1,1,513}}) {
        b.assign(1,0xffffffffu);db=b;CHECK(!history.BuildAtlas(invalid,7,b,&db));CHECK(b.empty()&&db.empty());}
    b.assign(1,0xffffffffu);db=b;CHECK(!history.BuildAtlas(map,NAN,b,&db));CHECK(b.empty()&&db.empty());
    history.Advance(12);CHECK(foamDirectionAtlas(history,map,7,b,db));unsigned active=0;
    for(auto p:db) {const auto d=foamDecodeDirection(p);if(d.alpha<10)continue;++active;
        CHECK_NEAR(d.x,1,2.0/d.alpha);CHECK_NEAR(d.y,0,2.0/d.alpha);}
    CHECK(active>0); // Aging spreads/decays the deposit without changing its recorded axis.
    history.Advance(30);CHECK(foamDirectionAtlas(history,map,7,b,db));for(auto p:db)CHECK(p==0);
    history.Advance(0);CHECK(history.SegmentCount()==0 && history.TrackedCount()==0);
    CHECK(foamDirectionAtlas(history,{0,0,512,512,512,512},7,b,db));
    CHECK(db.size()==512u*512u);for(auto p:db)CHECK(p==0);
}

TEST(water_foam_direction_emitter_eviction_preserves_tail_and_segment_budget)
{
    using namespace WaterFoamHistory;
    History history;history.Observe(1,foamTestHull(0,0,100,0),0);
    history.Advance(.12);history.Observe(1,foamTestHull(12,0,100,0),.12);
    const Mapping map={-24,-12,64,24,64,24};std::vector<std::uint32_t> a,b,da,db;
    CHECK(foamDirectionAtlas(history,map,7,a,da));CHECK(history.SegmentCount()==0);
    for(unsigned id=2;id<MaxEmitters+2;++id)history.Observe(id,foamTestHull(10000,float(id*20),30,0),.12);
    CHECK(history.TrackedCount()==MaxEmitters && history.SegmentCount()==1);
    CHECK(foamDirectionAtlas(history,map,7,b,db));CHECK(a==b && da==db);
    history.Clear();
    for(unsigned frame=0;frame<=96;++frame) {const double t=frame*.25;history.Advance(t);
        for(unsigned id=0;id<16;++id)history.Observe(id,foamTestHull(float(t*30),float(id*20),30,0),t);}
    CHECK(history.SegmentCount()==MaxSegments);
    CHECK(foamDirectionAtlas(history,{620,-4,80,8,16,4},7,b,db));unsigned active=0;
    for(auto p:db) {const auto d=foamDecodeDirection(p);if(d.alpha<10)continue;++active;
        CHECK_NEAR(d.x,1,2.0/d.alpha);CHECK_NEAR(d.y,0,2.0/d.alpha);}
    CHECK(active>0);
}

TEST(water_vessel_motion_tracks_scripted_transforms_and_braking)
{
	using namespace WaterVesselMotion;
	State state;
	CHECK(Update(state,{0,0},7,100,false,{},30).x==0);
	// A scripted vessel can have zero physics velocity while its displayed hull moves.
	// Braking must likewise follow that transform instead of forcing its wake speed to zero.
	for (unsigned step=1;step<=60;++step) {
		const Point velocity=Update(state,{float(step),-0.5f*step},7,100+step,false,{},30);
		const float response=1-std::exp(-float(step)/30/SmoothingSeconds);
		CHECK(std::abs(velocity.x-30*response)<0.00002f);
		CHECK(std::abs(velocity.y+15*response)<0.00002f);
	}
	CHECK(state.velocity.x>29.99f); // Armed against retaining the zero physics-only velocity.
	const Point moving=state.velocity;
	for (unsigned step=1;step<=30;++step) {
		const Point velocity=Update(state,{60,-30},7,160+step,false,{100,100},30);
		const float decay=std::exp(-float(step)/30/SmoothingSeconds);
		CHECK(std::abs(velocity.x-moving.x*decay)<0.00002f);
		CHECK(std::abs(velocity.y-moving.y*decay)<0.00002f);
	}
	CHECK(std::abs(state.velocity.x)<0.04f); // Stale nonzero physics cannot sustain a stopped wake.
}

TEST(water_vessel_motion_is_stable_during_pause_and_repeated_render_frames)
{
	using namespace WaterVesselMotion;
	State state;
	Update(state,{0,0},7,100,false,{30,-15},30);
	const Point original=state.velocity;
	for (int draw=0;draw<100;++draw) {
		const Point sameFrame=Update(state,{0.99f,-0.45f},7,100,false,{0,0},30);
		CHECK(sameFrame.x==original.x && sameFrame.y==original.y);
		CHECK(state.frame==100 && state.position.x==0 && state.position.y==0);
		const Point frozen=Update(state,{9,9},7,101,true,{0,0},30);
		CHECK(frozen.x==original.x && frozen.y==original.y);
		CHECK(state.frame==100 && state.position.x==0 && state.position.y==0);
	}
	const Point resumed=Update(state,{1,-0.5f},7,101,false,{},30);
	CHECK(std::abs(resumed.x-30)<0.00001f && std::abs(resumed.y+15)<0.00001f);
	// Equal elapsed simulation time gives the same filtered velocity when render visibility
	// samples every frame, every other frame, or every third frame; extra render calls do not age it.
	Point results[3];
	for (unsigned cadence=1;cadence<=3;++cadence) {
		State sampled;Update(sampled,{0,0},7,0,false,{},30);
		for (unsigned frame=cadence;frame<=60;frame+=cadence) {
			results[cadence-1]=Update(sampled,{float(frame),float(frame)*0.5f},7,frame,false,{},30);
			Update(sampled,{float(frame)+0.4f,float(frame)*0.5f+0.2f},7,frame,false,{},30);
		}
	}
	for (const Point velocity:results) {
		CHECK(std::abs(velocity.x-results[0].x)<0.00002f);
		CHECK(std::abs(velocity.y-results[0].y)<0.00002f);
	}
}

TEST(water_vessel_motion_preserves_seconds_when_logic_rate_changes)
{
    using namespace WaterVesselMotion;
    for (unsigned fps : {30u,unsigned(LOGICFRAMES_PER_SECOND),120u}) {
        State state;
        Update(state,{0,0},7,0,false,{},float(fps));
        for (unsigned frame=1;frame<=fps*2;++frame) {
            const float time=float(frame)/fps;
            const Point velocity=Update(state,{60*time,-30*time},7,frame,false,{},float(fps));
            const float response=1-std::exp(-time/SmoothingSeconds);
            CHECK_NEAR(velocity.x,60*response,.0001f);
            CHECK_NEAR(velocity.y,-30*response,.0001f);
        }
        const Point moving=state.velocity;
        for (unsigned frame=1;frame<=fps;++frame) {
            const Point velocity=Update(state,{120,-60},7,fps*2+frame,false,{},float(fps));
            const float decay=std::exp(-float(frame)/fps/SmoothingSeconds);
            CHECK_NEAR(velocity.x,moving.x*decay,.0001f);
            CHECK_NEAR(velocity.y,moving.y*decay,.0001f);
        }
    }
}

TEST(water_vessel_motion_rejects_teleports_stale_history_and_object_replacement)
{
	using namespace WaterVesselMotion;
	auto moving=[]() { State state;Update(state,{0,0},7,100,false,{30,10},30);return state; };
	State state=moving();
	Point result=Update(state,{33,0},7,101,false,{90,0},30);
	CHECK(result.x==0 && result.y==0 && state.position.x==33 && state.frame==101);
	result=Update(state,{34,0},7,102,false,{},30);
	CHECK(result.x>0 && result.x<30); // A teleport anchors, then genuine movement can resume.
	state=moving();result=Update(state,{16,0},7,116,false,{90,0},30);
	CHECK(result.x==90 && result.y==0); // Camera reentry reseeds from current physics, never old displacement.
	state=moving();result=Update(state,{16,0},7,116,false,{NAN,0},30);
	CHECK(result.x==0 && result.y==0); // Missing/invalid physics cannot resurrect stale motion.
	state=moving();result=Update(state,{16,0},7,116,false,{300,400},30);
	CHECK(result.x==150 && result.y==200); // The reentry seed obeys the same vector speed bound.
	state=moving();result=Update(state,{1000,0},7,116,false,{90,0},30);
	CHECK(result.x==0 && result.y==0); // A teleport still resets, even after a visibility gap.
	state=moving();result=Update(state,{15,0},7,115,false,{},30);
	CHECK(result.x>0); // The inclusive half-second limit still observes real motion.
	state=moving();result=Update(state,{1,0},8,101,false,{90,0},30);
	CHECK(result.x==0 && result.y==0 && state.level==8);
	state=moving();result=Update(state,{1,0},7,99,false,{90,0},30);
	CHECK(result.x==0 && result.y==0 && state.frame==99);
	state=moving();state=State();result=Update(state,{1000,2000},7,101,false,{},30);
	CHECK(result.x==0 && result.y==0); // Model/ID cache replacement cannot inherit old velocity.
}

TEST(water_vessel_motion_uses_the_actual_first_render_phase)
{
	using namespace WaterVesselMotion;
	const float phases[]={0.125f,0.875f,0.25f,0.75f,0.5f};
	State sampled,integerTime;
	float largestControlError=0;
	for (unsigned frame=100;frame<160;++frame) {
		const float phase=phases[(frame-100)%5];
		// A constant 30,-15 unit/s ship, rendered at a different interpolation phase on
		// each tick. Binary phases keep this independent reference free of roundoff noise.
		const float sampleTime=float(frame-1)+phase;
		const Point position={sampleTime,-sampleTime*0.5f};
		const Point velocity=Update(sampled,position,7,frame,false,{30,-15},30,phase);
		CHECK(std::abs(velocity.x-30)<0.00001f && std::abs(velocity.y+15)<0.00001f);
		CHECK(sampled.phase==phase && sampled.frame==frame);
		const Point control=Update(integerTime,position,7,frame,false,{30,-15},30);
		largestControlError=(std::max)(largestControlError,std::abs(control.x-30));
		// Additional draws and frozen passes do not replace this tick's sampled phase.
		const Point repeated=Update(sampled,{sampleTime+0.1f,0},7,frame,false,{},30,0.99f);
		const Point frozen=Update(sampled,{1000,1000},7,frame+1,true,{},30,0.75f);
		CHECK(repeated.x==velocity.x && frozen.x==velocity.x);
		CHECK(sampled.phase==phase && sampled.position.x==position.x && sampled.frame==frame);
	}
	CHECK(largestControlError>4.0f); // Armed control: integer-only timing invents acceleration.
	// Models snapped to their current tick are sampled at phase1, even when the global
	// renderer alpha differs. Switching between a snapped and blended model stays correct.
	State transitions;
	Update(transitions,{99.25f,0},7,100,false,{30,0},30,0.25f);
	Point velocity=Update(transitions,{101,0},7,101,false,{},30,1);
	CHECK(velocity.x==30 && transitions.phase==1);
	velocity=Update(transitions,{101.5f,0},7,102,false,{},30,0.5f);
	CHECK(velocity.x==30 && transitions.phase==0.5f);
}

TEST(water_vessel_motion_handles_zero_elapsed_phase_and_invalid_phase)
{
	using namespace WaterVesselMotion;
	State state;
	Update(state,{5,3},7,100,false,{30,-15},30,1);
	Point velocity=Update(state,{5,3},7,101,false,{},30,0);
	CHECK(velocity.x==30 && velocity.y==-15 && state.frame==101 && state.phase==0);
	velocity=Update(state,{6.5f,2.25f},7,102,false,{},30,0.5f);
	CHECK(velocity.x==30 && velocity.y==-15);
	Update(state,{8,1.5f},7,103,false,{},30,1);
	velocity=Update(state,{40,10},7,104,false,{100,100},30,0);
	CHECK(velocity.x==0 && velocity.y==0); // Changed transform at identical time cannot be divided by zero.
	for (float invalid:{float(NAN),float(INFINITY),float(-INFINITY)}) {
		State bad;velocity=Update(bad,{0,0},7,0,false,{10,10},30,invalid);
		CHECK(velocity.x==0 && velocity.y==0 && !bad.initialized);
	}
	State clamped;
	Update(clamped,{0,0},7,0,false,{},30,-0.01f);CHECK(clamped.phase==0);
	Update(clamped,{2,0},7,1,false,{},30,1.01f);CHECK(clamped.phase==1);
	// The history threshold uses real displayed sample time too, not an integer tick count.
	State recent;Update(recent,{0,0},7,100,false,{30,0},30,0.875f);
	velocity=Update(recent,{14.25f,0},7,115,false,{90,0},30,0.125f);
	CHECK(velocity.x==30);
	State stale;Update(stale,{0,0},7,100,false,{30,0},30,0.125f);
	velocity=Update(stale,{15.75f,0},7,115,false,{90,0},30,0.875f);
	CHECK(velocity.x==90);
}

TEST(water_vessel_motion_is_finite_vector_bounded_and_rotation_equivariant)
{
	using namespace WaterVesselMotion;
	State state;
	Point velocity=Update(state,{0,0},7,0,false,{300,400},30);
	CHECK(std::abs(velocity.x-150)<0.00001f && std::abs(velocity.y-200)<0.00001f);
	for (unsigned frame=1;frame<=60;++frame) {
		velocity=Update(state,{10.0f*frame,10.0f*frame},7,frame,false,{},30);
		CHECK(std::isfinite(velocity.x) && std::isfinite(velocity.y));
		CHECK(double(velocity.x)*velocity.x+double(velocity.y)*velocity.y<=double(MaxSpeed)*MaxSpeed+0.01);
	}
	for (float invalid:{float(NAN),float(INFINITY),float(-INFINITY)}) {
		State bad;velocity=Update(bad,{0,0},7,0,false,{invalid,1},30);
		CHECK(velocity.x==0 && velocity.y==0);
		velocity=Update(state,{invalid,0},7,61,false,{},30);
		CHECK(velocity.x==0 && velocity.y==0 && !state.initialized);
		velocity=Update(state,{0,0},invalid,61,false,{},30);
		CHECK(velocity.x==0 && velocity.y==0 && !state.initialized);
		velocity=Update(state,{0,0},7,61,false,{},invalid);
		CHECK(velocity.x==0 && velocity.y==0 && !state.initialized);
	}
	State straight,rotated;
	Update(straight,{0,0},7,0,false,{},30);Update(rotated,{100,-200},7,0,false,{},30);
	for (unsigned frame=1;frame<=60;++frame) {
		const Point p={float(frame),0.125f*float(frame*frame)};
		const Point a=Update(straight,p,7,frame,false,{},30);
		const Point b=Update(rotated,{100-p.y,-200+p.x},7,frame,false,{},30);
		CHECK(std::abs(a.x-b.y)<0.00002f && std::abs(a.y+b.x)<0.00002f);
	}
}

TEST(water_fft_metadata_preserves_shared_edges_and_floor_and_hull_bounds)
{
	using namespace WaterProjectedMesh;
	using namespace WaterFftGeometry;
	const Point left[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	const Point right[4]={{0,500},{0,-500},{500,-500},{500,500}};
	const Projection projection=testWaterProjection();SeamFade a,b;
	CHECK(a.Prepare(left,projection,{16,16}) && b.Prepare(right,projection,{16,16}));
	for (int i=-100;i<=100;++i) {
		CHECK(a.Weight({0,double(i)},1)==0 && b.Weight({0,double(i)},1)==0);
		CHECK(a.Weight({-75,double(i)},0)==0);
	}
	CHECK(a.Weight({-75,0},1)==1);
	CHECK(a.Weight({-75,0},0.37f)==0.37f);
	CHECK(InnerWorldCollar>MaxHorizontalDisplacement);
	for (int i=0;i<500;++i) {
		const double distance=i*0.1;
		CHECK(MaxHorizontalDisplacement*a.Weight({-distance,0},1)<=distance+1e-6);
	}
	for (float depth:{-4.0f,0.0f,0.1f,0.15f,0.16f,2.25f,7.0f,30.0f})
		for (float damping:{-1.0f,0.0f,0.17f,0.5f,1.0f,2.0f}) {
			const float safe=SafeDamping(damping,depth);
			CHECK(std::isfinite(safe) && safe>=0 && safe<=1);
			CHECK(safe*MaxVerticalDisplacement<=(std::max)(0.0f,depth-FloorClearance)+1e-6f);
			if (damping<=0 || depth<=FloorClearance) CHECK(safe==0);
		}
	CHECK(SafeDamping(NAN,3)==0 && SafeDamping(1,NAN)==0);
	// The existing nine-unit hull fade also bounds lateral travel: an eight-unit maximum
	// offset cannot carry a sample across the waterline it was attenuated against.
	for (int i=0;i<=900;++i) {
		const float distance=i*0.01f;
		CHECK(MaxHorizontalDisplacement*WaterSurfaceModel::ShoreDamping(distance)<=distance+1e-6f);
	}
}

TEST(water_fft_connected_coasts_keep_phase_support_without_changing_optical_classification)
{
	using namespace WaterOpticalContext;
	const int width=50,height=40,border=2;
	std::vector<uint8_t> heights(width*height,20);
	for (int y=0;y<height;++y) for (int x=0;x<18;++x) heights[y*width+x]=0;
	for (int y=10;y<31;++y) for (int x=28;x<47;++x) heights[y*width+x]=0;
	Field field;
	CHECK(Build(heights.data(),width,height,border,10,1,4,{opticalRectangle(-100,-100,600,500)},field));
	const WaterProjectedMesh::Point polygon[4]={{-100,-100},{600,-100},{600,500},{-100,500}};
	WaterFftGeometry::SeamFade seam;
	CHECK(seam.Prepare(polygon,testWaterProjection(),{160,90}));
	auto sample=[&](const std::vector<uint8_t> &channel,int x,int y) {
		return WaterSurfaceModel::SampleOffshore(channel.data(),width,height,float(x),float(y),10).height;
	};
	// Actual topology metadata distinguishes a nearshore zero-R ocean texel from
	// a zero-R enclosed pool. Colour/extinction R remains unchanged in both cases.
	CHECK(sample(field.ocean,15,20)==0 && sample(field.exposure,15,20)==1);
	CHECK(sample(field.ocean,37,20)==0 && sample(field.exposure,37,20)==0);
	CHECK(sample(field.ocean,4,20)==1 && sample(field.exposure,4,20)==1);
	const WaterProjectedMesh::Point coast={130,180},pool={350,180},open={20,180};
	CHECK(seam.Weight(coast,sample(field.exposure,15,20))==1);
	CHECK(seam.Weight(open,sample(field.exposure,4,20))==1);
	CHECK(seam.Weight(pool,sample(field.exposure,37,20))==0);
	CHECK(seam.Weight(coast,sample(field.ocean,15,20))==0); // Armed pre-fix phase switch.
	// Connection cannot bypass the authored-boundary collar or invalid metadata.
	CHECK(seam.Weight({-100,180},sample(field.exposure,4,20))==0);
	CHECK(seam.Weight(coast,WaterSurfaceModel::SampleOffshore(nullptr,width,height,15,20,10).height)==0);
	std::fill(heights.begin(),heights.end(),0);
	CHECK(Build(heights.data(),width,height,border,10,1,4,{opticalRectangle(100,100,300,300)},field));
	for (uint8_t exposure:field.exposure) CHECK(exposure==0);
	CHECK(seam.Weight(coast,sample(field.exposure,15,20))==0);
}

TEST(water_fft_hull_free_metadata_preserves_original_vertices_and_clearance)
{
	using namespace WaterFftGeometry;
	// Moving only the hull minimum to the two GPU stages must preserve the original
	// vertex displacement, including the authored shallow sea's stricter floor envelope.
	for (float depth:{-2.0f,0.15f,0.3f,2.25f,7.0f,30.0f})
		for (float offshore:{0.0f,0.125f,0.5f,0.875f,1.0f})
			for (float obstacle:{0.0f,0.1f,0.37f,0.8f,1.0f})
				for (int step=-10;step<=120;++step) {
					const float distance=step*0.1f;
					const float hull=1.0f+(WaterSurfaceModel::ShoreDamping(distance)-1.0f)*offshore;
					const float freeDamping=(std::min)(WaterSurfaceModel::TerrainDamping(depth,offshore),obstacle);
					const float original=SafeDamping((std::min)(freeDamping,hull),depth);
					const float separated=(std::min)(SafeDamping(freeDamping,depth),hull);
					CHECK(original==separated && separated>=0 && separated<=1);
					CHECK(separated*MaxVerticalDisplacement<=(std::max)(0.0f,depth-FloorClearance)+1e-6f);
					if (offshore==1 && distance>=0)
						CHECK(MaxHorizontalDisplacement*separated<=distance+1e-6f);
					if (offshore==0 || distance>=9)
						CHECK(separated==SafeDamping(freeDamping,depth));
				}
	// An overlapping second hull remains an ordered minimum, without dividing out a
	// zero hull factor or losing a lower floor/non-vessel obstacle constraint.
	for (int step=0;step<=90;++step) {
		const float a=WaterSurfaceModel::ShoreDamping(step*0.1f);
		const float b=WaterSurfaceModel::ShoreDamping((90-step)*0.1f);
		const float original=SafeDamping((std::min)(0.8f,(std::min)(a,b)),2.25f);
		const float separated=(std::min)(SafeDamping(0.8f,2.25f),(std::min)(a,b));
		CHECK(original==separated);
	}
}

TEST(water_fft_seam_fade_is_continuous_under_camera_motion_resize_and_horizon_crossing)
{
	using namespace WaterProjectedMesh;
	using namespace WaterFftGeometry;
	const Point polygon[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	for (Resolution resolution:{Resolution{16,16},Resolution{160,90},Resolution{192,128}}) {
		Projection p=testWaterProjection(),q=p;q.matrix[2]+=1e-7;CHECK(q.Prepare());SeamFade a,b;
		CHECK(a.Prepare(polygon,p,resolution) && b.Prepare(polygon,q,resolution));
		for (int i=0;i<=1000;++i) {
			const Point point={-i*0.1,1.3};const float w=a.Weight(point,0.8f);
			CHECK(w>=0 && w<=0.8f && std::isfinite(w));
			CHECK(std::abs(w-b.Weight(point,0.8f))<1e-6f);
			CHECK(std::abs(w-a.Weight({point.x-1e-7,point.y},0.8f))<1e-6f);
			if (i>0) CHECK(w+1e-7f>=a.Weight({point.x+0.1,point.y},0.8f));
		}
	}
	// An authored endpoint is exactly on the projective horizon. Moving it from one side
	// to the other must not turn the entire interior field off for a single camera frame.
	const Point horizonPolygon[4]={{-500,-1},{500,-1},{500,500},{-500,500}};
	float previous=-1;
	for (double shift:{-1e-9,0.0,1e-9}) {
		Projection p={{0.01,0,0,0,1.018,0,0,1,1+shift},{0},{0,1,1+shift},0.1,600,0.12};
		CHECK(p.Prepare());SeamFade fade;CHECK(fade.Prepare(horizonPolygon,p,{160,90}));
		const float weight=fade.Weight({0,15},1);CHECK(std::isfinite(weight) && weight>0);
		if (previous>=0) CHECK(std::abs(weight-previous)<1e-6f);previous=weight;
	}
}

TEST(water_fft_actual_boundary_triangles_keep_the_analytic_transition_continuous)
{
	using namespace WaterProjectedMesh;
	using namespace WaterFftGeometry;
	const Point polygon[4]={{-500,-500},{0,-500},{0,500},{-500,500}};
	std::vector<BoundaryKnot> knots;
	for (int edge=0;edge<4;++edge) {
		Point a=polygon[edge],b=polygon[(edge+1)%4];if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
		for (int i=0;i<=2;++i) knots.push_back({{a.x+(b.x-a.x)*i/2,a.y+(b.y-a.y)*i/2},edge,i,i/2.0});
	}
	for (int axis:{0,1}) {
		Projection p=testWaterProjection(),q=p;p.matrix[axis*3+2]=-1e-7;q.matrix[axis*3+2]=1e-7;
		CHECK(p.Prepare() && q.Prepare());Mesh a,b;
		CHECK(Build(polygon,p,{16,16},a,60000,knots) && Build(polygon,q,{16,16},b,60000,knots));
		SeamFade fa,fb;CHECK(fa.Prepare(polygon,p,{16,16}) && fb.Prepare(polygon,q,{16,16}));
		int guardedTotal=0;
		auto displaced=[&](const Mesh &mesh,const SeamFade &fade) {
			auto height=projectedTestHeights(mesh,polygon);int active=0;
			for (size_t i=0;i<mesh.vertices.size();++i) {
				const Vertex &v=mesh.vertices[i];const float weight=fade.Weight(v.world,1);
				if (v.boundary>=0) CHECK(weight==0);
				if (v.transition>=0 && v.world.x>-20 && std::abs(v.world.y)<90) {
					++guardedTotal;CHECK(weight==0);
					for (Point source:mesh.transitions[v.transition].source) CHECK(fade.Weight(source,1)==0);
				}
				const double fft=1.4*std::cos(v.world.x*0.18)+0.6*std::sin(v.world.y*0.11);
				if (weight>0) { ++active; height[i]+=(fft-height[i])*weight; }
				CHECK(std::abs(height[i])<=2.300001);
			}
			CHECK(active>0);return height;
		};
		const auto ah=displaced(a,fa),bh=displaced(b,fb);
		CHECK(guardedTotal>0);
		for (double y:{-80.0,-35.0,-5.0,5.0,35.0,80.0}) for (double x:{-95.0,-75.0,-35.0,-5.0})
			CHECK(std::abs(projectedTestRenderedHeight(a,ah,{x,y})-projectedTestRenderedHeight(b,bh,{x,y}))<0.00003);
	}
}

TEST(water_ocean_height_and_slopes_include_warp_and_packet_derivatives)
{
	using namespace WaterSurfaceModel;
	for (float time:{0.0f,1.73f,31.0f,139.7f}) for (int i=0;i<180;++i) {
		const float x=-190.0f+i*3.713f,y=81.0f-i*2.419f,epsilon=0.015625f;
		const Sample wave=EvaluateOcean(x,y,time);
		const float dx=(EvaluateOcean(x+epsilon,y,time).height-EvaluateOcean(x-epsilon,y,time).height)/(2*epsilon);
		const float dy=(EvaluateOcean(x,y+epsilon,time).height-EvaluateOcean(x,y-epsilon,time).height)/(2*epsilon);
		CHECK(std::abs(wave.dx-dx)<0.002f);
		CHECK(std::abs(wave.dy-dy)<0.002f);
		CHECK(std::isfinite(wave.height) && std::abs(wave.height)<=OceanMaxDisplacement);
	}
	CHECK(MaxDisplacement>=LegacyMaxDisplacement && MaxDisplacement>=OceanMaxDisplacement);
	std::string shader;AppendHLSL(shader);
	CHECK(shader.find("float3 waterOceanWave")!=std::string::npos);
	CHECK(shader.find("envelopeSlope")!=std::string::npos);
}

TEST(water_ocean_zero_classification_preserves_existing_waves_and_damping_exactly)
{
	using namespace WaterSurfaceModel;
	for (int i=0;i<100;++i) {
		const float x=i*19.3f-180,y=i*-7.13f,time=i*0.371f;
		const Sample original=Evaluate(x,y,time),unchanged=EvaluateBlended(x,y,time,{0,0,0});
		CHECK(memcmp(&original,&unchanged,sizeof(Sample))==0);
		const Sample ocean=EvaluateOcean(x,y,time),full=EvaluateBlended(x,y,time,{1,0,0});
		CHECK(memcmp(&ocean,&full,sizeof(Sample))==0);
		const float depth=i*0.137f-1;
		CHECK(TerrainDamping(depth,0)==ShoreDamping(depth));
	}
	const std::uint8_t pool[9]={0,0,0,0,0,0,0,0,0};
	const Sample enclosed=SampleOffshore(pool,3,3,0.3f,1.2f,10);
	CHECK(enclosed.height==0 && enclosed.dx==0 && enclosed.dy==0);
}

TEST(water_ocean_cpu_field_sampling_matches_bilinear_texel_centres_and_padding)
{
	using namespace WaterSurfaceModel;
	const std::uint8_t field[9]={0,64,255,16,96,223,48,128,191};
	for (int y=0;y<3;++y) for (int x=0;x<3;++x)
		CHECK(SampleOffshore(field,3,3,float(x),float(y),10).height==field[y*3+x]/255.0f);
	for (int iy=0;iy<2;++iy) for (int ix=0;ix<2;++ix) for (float fy:{0.17f,0.63f}) for (float fx:{0.23f,0.71f}) {
		const float gx=ix+fx,gy=iy+fy,epsilon=0.001f;
		const Sample sample=SampleOffshore(field,3,3,gx,gy,10);
		const float independent=(field[iy*3+ix]*(1-fx)*(1-fy)+field[iy*3+ix+1]*fx*(1-fy)
			+field[(iy+1)*3+ix]*(1-fx)*fy+field[(iy+1)*3+ix+1]*fx*fy)/255.0f;
		CHECK(std::abs(sample.height-independent)<0.000001f);
		CHECK(std::abs(sample.dx-(SampleOffshore(field,3,3,gx+epsilon,gy,10).height-
			SampleOffshore(field,3,3,gx-epsilon,gy,10).height)/(20*epsilon))<0.00001f);
		CHECK(std::abs(sample.dy-(SampleOffshore(field,3,3,gx,gy+epsilon,10).height-
			SampleOffshore(field,3,3,gx,gy-epsilon,10).height)/(20*epsilon))<0.00001f);
	}
	const Sample left=SampleOffshore(field,3,3,-100,1.5f,10),right=SampleOffshore(field,3,3,100,1.5f,10);
	CHECK(left.dx==0 && right.dx==0);
	CHECK(left.height==SampleOffshore(field,3,3,0,1.5f,10).height);
	CHECK(right.height==SampleOffshore(field,3,3,2,1.5f,10).height);
	CHECK(SampleOffshore(NULL,3,3,1,1,10).height==0);
}

TEST(water_ocean_transition_normals_include_spatial_blend_and_attenuation)
{
	using namespace WaterSurfaceModel;
	auto blend=[](float x,float y) { return Sample{0.45f+x*0.001f+y*0.0005f,0.001f,0.0005f}; };
	auto damping=[&](float x,float y) { return TerrainDamping(6.0f+x*0.01f-y*0.005f,blend(x,y).height); };
	auto height=[&](float x,float y) { return EvaluateBlended(x,y,3.17f,blend(x,y)).height*damping(x,y); };
	for (int i=0;i<80;++i) {
		const float x=-130+i*3.1f,y=53-i*1.7f,epsilon=0.015625f;
		const Sample wave=EvaluateBlended(x,y,3.17f,blend(x,y));
		const float dx=wave.dx*damping(x,y)+wave.height*(damping(x+epsilon,y)-damping(x-epsilon,y))/(2*epsilon);
		const float dy=wave.dy*damping(x,y)+wave.height*(damping(x,y+epsilon)-damping(x,y-epsilon))/(2*epsilon);
		CHECK(std::abs(dx-(height(x+epsilon,y)-height(x-epsilon,y))/(2*epsilon))<0.003f);
		CHECK(std::abs(dy-(height(x,y+epsilon)-height(x,y-epsilon))/(2*epsilon))<0.003f);
	}
}

TEST(water_ocean_displacement_stays_above_authored_floor_and_uses_ship_footprint)
{
	using namespace WaterSurfaceModel;
	for (int d=0;d<=2000;++d) for (int o=0;o<=20;++o) {
		const float depth=d*0.01f,offshore=o/20.0f,attenuation=TerrainDamping(depth,offshore);
		const float bound=LegacyMaxDisplacement+(OceanMaxDisplacement-LegacyMaxDisplacement)*offshore;
		CHECK(attenuation>=0 && attenuation<=1);
		CHECK(attenuation*bound<=depth+0.00001f);
		if (o==20 && depth>=0.15f) CHECK(attenuation*bound<=depth-0.15f+0.00001f);
	}
	CHECK(TerrainDamping(2.25f,1)>TerrainDamping(2.25f,0));
	CHECK(TerrainDamping(OceanMaxDisplacement+0.2f,1)==1);
	CHECK(BoxDamping(0,0,112,74)==0);
	CHECK(BoxDamping(112,74,112,74)==0);
	CHECK(BoxDamping(0,84,112,74)==1); // carrier side: legacy radius134 still flattened here
	CHECK(ShoreDamping(84-134.24f)==0);
	CHECK(BoxDamping(122,0,112,74)==1);
	for (int i=0;i<90;++i) CHECK(BoxDamping(112+i*0.1f,0,112,74)<=BoxDamping(112+(i+1)*0.1f,0,112,74));
}

TEST(water_ocean_obstacle_metadata_preserves_opacity_and_lod_profile)
{
	using namespace WaterSurfaceModel;
	const std::uint32_t colour=0x9e357abdu;
	for (int i=0;i<=1000;++i) {
		const float obstacle=i/1000.0f;
		const std::uint32_t encoded=EncodeObstacleAttenuation(colour,obstacle);
		CHECK((encoded&0xff00ffffu)==(colour&0xff00ffffu));
		CHECK(std::abs(float((encoded>>16)&255)/255.0f-obstacle)<=0.5f/255.0f+0.000001f);
		const std::uint32_t a=EncodeObstacleAttenuation(colour,0.13f),b=EncodeObstacleAttenuation(colour,0.89f);
		for (float morph:{0.0f,0.25f,0.75f,1.0f}) {
			const std::uint32_t mixed=MorphColour(encoded,a,b,morph);
			CHECK((mixed&0xff00ffffu)==(colour&0xff00ffffu));
			CHECK(std::abs(float((mixed>>16)&255)/255.0f-MorphCoordinate(obstacle,0.13f,0.89f,morph))<=1.0f/255.0f+0.000001f);
		}
	}
	CHECK(((EncodeObstacleAttenuation(colour,-1)>>16)&255)==0);
	CHECK(((EncodeObstacleAttenuation(colour,2)>>16)&255)==255);
}

TEST(water_ocean_lod_preserves_budget_and_morphs_continuously_with_shared_topology)
{
	using namespace WaterSurfaceModel;
	for (int height=0;height<2000;++height) {
		const MeshLOD old=LODForCameraHeight(128,float(height)),river=LODForView(128,float(height),0);
		CHECK(old.cells==river.cells && old.morph==river.morph);
		const MeshLOD ocean=LODForView(128,float(height),3000);
		CHECK(ocean.cells>=river.cells && ocean.cells<=128 && ocean.morph>=0 && ocean.morph<=1);
	}
	for (int patches:{1,3,20,33,80,200}) {
		const int base=CellsForPatches(patches);
		for (float height:{117.0f,310.0f,480.0f,900.0f}) {
			const MeshLOD ocean=LODForView(base,height,3000);
			CHECK(patches*(ocean.cells+1)*(ocean.cells+1)<=VertexBudget);
		}
	}
	for (float threshold:{720.0f,1080.0f,1620.0f,2430.0f}) {
		const MeshLOD below=LODForView(128,threshold-0.001f,3000),above=LODForView(128,threshold+0.001f,3000);
		const auto fine=testLODGrid(below.cells,below.morph,true),coarse=testLODGrid(above.cells,above.morph,true);
		for (int j=0;j<12;++j) for (int i=0;i<12;++i) {
			const float u=(i+0.17f)/12,v=(j+0.63f)/12;
			checkLODVertexNear(testLODSample(fine,below.cells,u,v),testLODSample(coarse,above.cells,u,v),0.0003f);
		}
	}
}

TEST(water_ocean_blended_displaced_shared_edges_are_exact_through_all_morphs)
{
	using namespace WaterSurfaceModel;
	std::vector<std::uint8_t> field(256*96);
	for (int y=0;y<96;++y) for (int x=0;x<256;++x) field[y*256+x]=std::uint8_t(x);
	const Point a={-200,17,12},b={2100,533,12},camera={500,200,110};
	auto vertex=[&](Point p) {
		const Sample offshore=SampleOffshore(field.data(),256,96,p.x/10,p.y/10,10);
		const float attenuation=TerrainDamping((p.y-30)*0.07f,offshore.height);
		p.z+=EvaluateBlended(p.x,p.y,1.73f,offshore).height*attenuation;
		return LODTestVertex{p,attenuation,12};
	};
	for (int cells=8;cells<=MeshCells;cells*=2) {
		std::vector<LODTestVertex> forward(cells+1),reverse(cells+1);
		for (int i=0;i<=cells;++i) {
			forward[i]=vertex(EdgePoint(a,b,float(i)/cells,camera));
			reverse[i]=vertex(EdgePoint(b,a,float(i)/cells,camera));
		}
		for (float amount:{0.0f,0.17f,0.5f,0.93f,1.0f}) for (int i=0;i<=cells;++i) {
			const CoarsePair fp=CoarsePairForVertex(i,0,cells),rp=CoarsePairForVertex(cells-i,0,cells);
			const LODTestVertex f=testLODMorph(forward[i],forward[fp.first],forward[fp.second],amount);
			const LODTestVertex r=testLODMorph(reverse[cells-i],reverse[rp.first],reverse[rp.second],amount);
			CHECK(memcmp(&f,&r,sizeof(f))==0);
		}
	}
}

TEST(water_ocean_height_only_evaluation_is_bit_identical_to_the_full_model)
{
	using namespace WaterSurfaceModel;
	for (float time:{-17.3f,0.0f,3.17f,97.5f,1220.3f}) for (int i=0;i<1200;++i) {
		const float x=-4395.0f+i*7.731f,y=3526.0f-i*5.377f;
		const float height=EvaluateOceanHeight(x,y,time),fullHeight=EvaluateOcean(x,y,time).height;
		const float templateHeight=EvaluateOceanInternal<false,false>(x,y,time).height;
		CHECK(memcmp(&height,&fullHeight,sizeof(height))==0);
		CHECK(memcmp(&height,&templateHeight,sizeof(height))==0);
		for (const Sample offshore: {Sample{0,0,0},Sample{0.25f,0.01f,-0.005f},Sample{1,0,0},Sample{1,0.001f,0}})
			CHECK(EvaluateBlendedHeight(x,y,time,offshore)==EvaluateBlended(x,y,time,offshore).height);
	}
}

TEST(water_river_height_only_evaluation_is_bit_identical_to_the_full_model)
{
	using namespace WaterSurfaceModel;
	for (float time:{-17.3f,0.0f,3.17f,97.5f,1220.3f}) for (int i=0;i<1200;++i) {
		const float x=-4395.0f+i*7.731f,y=3526.0f-i*5.377f;
		const float height=EvaluateHeight(x,y,time),fullHeight=Evaluate(x,y,time).height;
		CHECK(memcmp(&height,&fullHeight,sizeof(height))==0);
		for (const Sample offshore: {Sample{-0.2f,0,0},Sample{0,0,0},Sample{0,0.01f,0},
			Sample{0.25f,0.01f,-0.005f},Sample{0.5f,0,0},Sample{1,0,0},Sample{1.2f,0.001f,0}}) {
			const float blended=EvaluateBlendedHeight(x,y,time,offshore);
			const float fullBlended=EvaluateBlended(x,y,time,offshore).height;
			CHECK(memcmp(&blended,&fullBlended,sizeof(blended))==0);
		}
	}
}

TEST(water_ocean_obstacle_broadphase_only_rejects_fully_unattenuated_samples)
{
	using namespace WaterSurfaceModel;
	for (float major:{3.0f,50.0f,112.0f}) for (float minor:{2.0f,30.0f,74.0f}) {
		const float radius=std::sqrt(major*major+minor*minor);
		CHECK(!BeyondObstacleFade(0,0,radius));
		CHECK(!BeyondObstacleFade(radius+8.9f,0,radius));
		CHECK(BeyondObstacleFade(radius+20,0,radius));
		for (int i=0;i<100;++i) for (float margin:{8.0f,9.0f,9.999f,10.001f,50.0f}) {
			const float angle=i*0.37f,orientation=i*0.83f;
			const float dx=std::cos(angle)*(radius+margin),dy=std::sin(angle)*(radius+margin);
			if (!BeyondObstacleFade(dx,dy,radius)) continue;
			CHECK(ShoreDamping(std::sqrt(dx*dx+dy*dy)-radius)==1);
			CHECK(BoxDamping(dx*std::cos(orientation)+dy*std::sin(orientation),
				-dx*std::sin(orientation)+dy*std::cos(orientation),major,minor)==1);
		}
	}
}

TEST(water_ocean_analytic_laplacian_matches_independent_height_curvature)
{
	using namespace WaterSurfaceModel;
	for (float time:{0.0f,3.17f,19.7f}) for (int i=0;i<120;++i) {
		const float x=-61.0f+i*1.073f,y=43.0f-i*0.917f,epsilon=0.125f;
		const OceanSurface surface=EvaluateOceanSurface(x,y,time);
		const Sample wave=EvaluateOcean(x,y,time);
		CHECK(surface.height==wave.height && surface.dx==wave.dx && surface.dy==wave.dy);
		auto central=[&](float step) {
			return (EvaluateOcean(x+step,y,time).height+EvaluateOcean(x-step,y,time).height
				+EvaluateOcean(x,y+step,time).height+EvaluateOcean(x,y-step,time).height
				-4*surface.height)/(step*step);
		};
		// Richardson extrapolation removes the O(h^2) truncation of the shortest ripple modes,
		// without reducing h until float subtraction loses the small curvature signal.
		const float numerical=(4*central(epsilon)-central(2*epsilon))/3;
		CHECK(std::isfinite(surface.laplacian));
		CHECK(std::abs(surface.laplacian-numerical)<0.0025f);
		auto gradientDifference=[&](float step) {
			return (EvaluateOcean(x+step,y,time).dx-EvaluateOcean(x-step,y,time).dx
				+EvaluateOcean(x,y+step,time).dy-EvaluateOcean(x,y-step,time).dy)/(2*step);
		};
		// The gradient stencil also needs its h^2 term removed: at h=.125 the fastest ripples
		// biased three fixtures by .008–.009 although the analytic/double-height error was <2e-5.
		const float derivative=(4*gradientDifference(epsilon*0.5f)-gradientDifference(epsilon))/3;
		CHECK(std::abs(surface.laplacian-derivative)<0.0005f);
	}
	std::string hlsl=OceanShaderSource();
	CHECK(hlsl.find("float4 waterOceanSurface")!=std::string::npos);
	CHECK(hlsl.find("return waterOceanSurface(pxy,seconds).xyz")!=std::string::npos);
}

TEST(water_legacy_foam_replacement_is_exact_attached_current_hull_and_main_pass_only)
{
	using namespace WaterFoamParticlePolicy;
	const char *names[]={"AmphibWave","AmphibWaveRest"};
	const char *textures[]={"EXWave01.tga","EXShockWav.tga"};
	for(unsigned pair=0;pair<2;++pair) {
		// Full truth table prevents a missing readiness, owner, particle-type or mirror gate
		// from removing the fallback or changing an unrelated particle draw path.
		for(unsigned flags=0;flags<16;++flags)
			CHECK(Replace(names[pair],textures[pair],(flags&1)!=0,(flags&2)!=0,
				(flags&4)!=0,(flags&8)!=0)==(flags==7));
		CHECK(KnownSurfaceFoam(names[pair],textures[pair]));
		CHECK(!Replace(names[pair],pair==0?"EXShockWav.tga":"EXWave01.tga",true,true,true,false));
		for(const char *texture:{static_cast<const char*>(nullptr),"","EXWater01.tga","Smoke.tga",
			"Other/EXShockWav.tga","EXShockWav.tga.extra"})
			CHECK(!Replace(names[pair],texture,true,true,true,false));
	}
	CHECK(Replace("amphibwave","exwave01.TGA",true,true,true,false));
	CHECK(Replace("AMPHIBWAVEREST","exshockwav.tga",true,true,true,false));
	CHECK(!Replace("aircarrierwaterripples","exshockwav.TGA",true,true,true,false));
	CHECK(!Replace("BATTLESHIPWATERRIPPLES","EXSHOCKWAV.TGA",true,true,true,false));
	for(const char *name:{"", "Amphib", "AmphibWaveRestExtra", "AmphibWaveSpray", "WaveSpray01",
		"RocketBuggyDust", "MuzzleFlash", "Explosion", "ToxinField", "AirCarrierWaterRipplesExtra",
		"BattleShipWaterRipple", "AirCarrierBowWake", "AirCarrierSideWake", "AirCarrierPropWake",
		"AirCarrierWaterWake", "AirCarrierWaterWakePort", "CustomWake"}) {
		CHECK(!Replace(name,"EXWave01.tga",true,true,true,false));
		CHECK(!Replace(name,"EXShockWav.tga",true,true,true,false));
	}
	CHECK(!Replace(nullptr,"EXWave01.tga",true,true,true,false));
	CHECK(!Replace("AmphibWave",nullptr,true,true,true,false));
	CHECK(!Replace("AmphibWave","EXWave01.tga.extra",true,true,true,false));
	CHECK(!Replace("AmphibWave","Other/EXWave01.tga",true,true,true,false));
}

TEST(water_authored_splash_rings_survive_every_pass_admission)
{
    using namespace WaterFoamParticlePolicy;
    for(const char *name:{"AirCarrierWaterRipples","BattleShipWaterRipples"}) {
        // Authored splash rings remain visible with modern hull foam, in the main and mirror passes.
        CHECK(!KnownSurfaceFoam(name,"EXShockWav.tga"));
        for(unsigned flags=0;flags<8;++flags) {
            const bool ground=(flags&1)!=0,owner=(flags&2)!=0,admitted=(flags&4)!=0;
            CHECK(!Replace(name,"EXShockWav.tga",ground,owner,admitted,false));
            CHECK(!ExcludeSelfReflection(name,"EXShockWav.tga",ground,owner,admitted));
        }
    }
}

TEST(water_surface_foam_does_not_reflect_itself_into_same_modern_plane)
{
	using namespace WaterFoamParticlePolicy;
	for(const auto pair:{std::make_pair("AmphibWave","EXWave01.tga"),std::make_pair("AmphibWaveRest","EXShockWav.tga")}) {
		for(unsigned flags=0;flags<8;++flags)
			CHECK(ExcludeSelfReflection(pair.first,pair.second,(flags&1)!=0,(flags&2)!=0,(flags&4)!=0)==(flags==7));
		// Mirror exclusion is independent of successful main-view foam replacement.
		CHECK(!Replace(pair.first,pair.second,true,true,false,false));
		CHECK(ExcludeSelfReflection(pair.first,pair.second,true,true,true));
		CHECK(!ExcludeSelfReflection(pair.first,"Smoke.tga",true,true,true));
	}
	for(const char *name:{"AmphibWaveSpray","MuzzleFlash","Explosion","Smoke","CustomWake",
		"AirCarrierWaterRipplesExtra","BattleShipWaterRipple","AirCarrierBowWake","AirCarrierSideWake",
		"AirCarrierPropWake","AirCarrierWaterWake","AirCarrierWaterWakePort"}) {
		CHECK(!ExcludeSelfReflection(name,"EXWave01.tga",true,true,true));
		CHECK(!ExcludeSelfReflection(name,"EXShockWav.tga",true,true,true));
	}
	for(const char *name:{"AirCarrierWaterRipples","BattleShipWaterRipples"})
		for(const char *texture:{static_cast<const char*>(nullptr),"","EXWave01.tga","EXWater01.tga",
			"Other/EXShockWav.tga","EXShockWav.tga.extra"})
			CHECK(!ExcludeSelfReflection(name,texture,true,true,true));
	CHECK(!ExcludeSelfReflection(nullptr,"EXWave01.tga",true,true,true));
	CHECK(!ExcludeSelfReflection("AmphibWave",nullptr,true,true,true));
	CHECK(SameMirrorSurface(true,true,4.0f,4.0f));
	CHECK(SameMirrorSurface(true,true,4.04f,4.0f));
	CHECK(!SameMirrorSurface(true,true,4.2f,4.0f));
	CHECK(!SameMirrorSurface(true,true,-4.0f,4.0f));
	CHECK(!SameMirrorSurface(false,true,4.0f,4.0f)); // main, legacy or released target
	CHECK(!SameMirrorSurface(true,false,4.0f,4.0f)); // removed, hidden, dry or missing owner
	CHECK(!SameMirrorSurface(true,true,std::numeric_limits<float>::quiet_NaN(),4.0f));
	CHECK(!SameMirrorSurface(true,true,4.0f,std::numeric_limits<float>::infinity()));
}

namespace {
// Independent parameter-domain oracle: it knows triangles and scanlines, not the
// subdivision stencil, admission policy or edge-morph implementation under test.
using BowTestPoint=WaterProjectedMesh::Point;
using BowTestTriangle=std::array<BowTestPoint,3>;
using BowTestInterval=std::pair<double,double>;
double bowTestArea(const BowTestTriangle &p)
{
	return .5*((p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x));
}
bool bowTestCoincident(const BowTestTriangle &p)
{
	for(unsigned i=0;i<3;++i) if(p[i].x==p[(i+1)%3].x && p[i].y==p[(i+1)%3].y) return true;
	return false;
}
std::vector<BowTestTriangle> bowTestTriangles(const WaterProjectedMesh::Mesh &mesh,
	const WaterProjectedMesh::Projection &projection)
{
	std::vector<BowTestTriangle> result;
	CHECK(mesh.indices.size()%3==0);
	for(size_t i=0;i+2<mesh.indices.size();i+=3) {
		BowTestTriangle triangle;
		bool valid=true;
		for(unsigned j=0;j<3;++j) {
			const size_t index=mesh.indices[i+j];CHECK(index<mesh.vertices.size());
			if(index>=mesh.vertices.size()) { valid=false;continue; }
			const bool finite=projection.Transform(mesh.vertices[index].world,triangle[j]);
			CHECK(finite);valid=valid&&finite;
		}
		if(valid) result.push_back(triangle);
	}
	return result;
}
std::vector<BowTestInterval> bowTestIntervals(const std::vector<BowTestTriangle> &triangles,double y)
{
	std::vector<BowTestInterval> result;
	for(const auto &triangle:triangles) {
		if(std::abs(bowTestArea(triangle))<1e-15) continue;
		double crossing[3];unsigned count=0;
		for(unsigned edge=0;edge<3;++edge) {
			const auto a=triangle[edge],b=triangle[(edge+1)%3];
			// Half-open edges count a vertex only once. Horizontal edges do not cross.
			if((a.y<=y && y<b.y)||(b.y<=y && y<a.y))
				crossing[count++]=a.x+(b.x-a.x)*(y-a.y)/(b.y-a.y);
		}
		if(count==2) {
			const double a=(std::min)(crossing[0],crossing[1]),b=(std::max)(crossing[0],crossing[1]);
			if(b-a>1e-13) result.emplace_back(a,b);
		}
	}
	std::sort(result.begin(),result.end());return result;
}
struct BowTestCoverage {
	double minimumArea=1e30,areaError=0,gap=0,overlap=0,edgeError=0;
	bool finite=true;
	bool Good() const { return finite && minimumArea>=-1e-12 && areaError<1e-9 && gap<1e-9 && overlap<1e-9 && edgeError<1e-9; }
};
BowTestCoverage bowTestCoverage(const std::vector<BowTestTriangle> &actual,
	const std::vector<BowTestTriangle> &reference)
{
	BowTestCoverage audit;double area=0,expected=0,y0=1e30,y1=-1e30;
	for(const auto &triangle:reference) {
		expected+=bowTestArea(triangle);
		for(auto p:triangle) { y0=(std::min)(y0,p.y);y1=(std::max)(y1,p.y); }
	}
	for(const auto &triangle:actual) {
		const double a=bowTestArea(triangle);audit.finite=audit.finite&&std::isfinite(a);
		audit.minimumArea=(std::min)(audit.minimumArea,a);area+=a;
	}
	audit.areaError=std::abs(area-expected);
	// Off-lattice rows independently test the complete interval union. Total area
	// alone would miss equal-sized holes and overlaps elsewhere in the same patch.
	for(double fraction:{.00773,.03191,.07973,.13731,.21379,.31917,.41371,.51793,.61379,.71931,.81317,.91379,.97311,.99731}) {
		const double y=y0+(y1-y0)*fraction;
		const auto wanted=bowTestIntervals(reference,y),got=bowTestIntervals(actual,y);
		if(wanted.empty()) { if(!got.empty()) audit.edgeError=1e30;continue; }
		if(got.empty()) { audit.edgeError=1e30;continue; }
		const double left=wanted.front().first,right=wanted.back().second;
		audit.edgeError=(std::max)(audit.edgeError,std::abs(got.front().first-left));
		double end=left;
		for(auto interval:got) {
			audit.gap=(std::max)(audit.gap,interval.first-end);
			audit.overlap=(std::max)(audit.overlap,end-interval.first);
			end=interval.second;
		}
		audit.edgeError=(std::max)(audit.edgeError,std::abs(end-right));
	}
	return audit;
}
bool bowTestSameVertex(const WaterProjectedMesh::Vertex &a,const WaterProjectedMesh::Vertex &b)
{
	return a.world.x==b.world.x && a.world.y==b.world.y && a.boundary==b.boundary &&
		a.edgeParameter==b.edgeParameter && a.knot==b.knot && a.transition==b.transition && a.collar==b.collar && a.lattice==b.lattice;
}
// The full sample deliberately contains nonlinear position, height, floor damping
// and UV-like metadata. Exact collapsed equality must survive more than XY alone.
std::array<double,8> bowTestSample(BowTestPoint p)
{
	return {{p.x+.013*std::sin(1.73*p.y),p.y+.007*std::cos(2.1*p.x),
		std::sin(1.4*p.x)+.41*std::cos(2.6*p.y),
		.5+.3*std::sin(p.x*.7)*std::cos(p.y*.4),p.x/1.5,p.y/1.5,0,7}};
}
using BowTestSurface=std::vector<std::array<std::array<double,8>,3>>;
BowTestSurface bowTestSurface(const std::vector<BowTestTriangle> &triangles)
{
	BowTestSurface surface;
	for(const auto &triangle:triangles) {
		// Three collinear parameter samples can form a real triangle after the
		// nonlinear vertex shader; only identical complete samples may be removed.
		if(bowTestCoincident(triangle)) continue;
		std::array<std::array<double,8>,3> samples={{bowTestSample(triangle[0]),bowTestSample(triangle[1]),bowTestSample(triangle[2])}};
		std::sort(samples.begin(),samples.end());surface.push_back(samples);
	}
	std::sort(surface.begin(),surface.end());return surface;
}
uint32_t bowTestRandom(uint32_t &state) { state=state*1664525u+1013904223u;return state; }
}

namespace {
WaterProjectedMesh::Projection bowTestProjection()
{
	WaterProjectedMesh::Projection p={{1,0,0,0,1,0,0,0,1},{0},{0,0,1},.1,100,0};
	CHECK(p.Prepare());return p;
}
WaterBowRefinement::Topology bowTestGrid(unsigned columns=1,unsigned rows=1)
{
	using namespace WaterBowRefinement;Topology mesh;
	for(unsigned y=0;y<=rows;++y) for(unsigned x=0;x<=columns;++x) {
		Node node;node.logical={int(x*4),int(y*4)};node.screen={double(x*4),double(y*4)};
		node.world=node.screen;node.source=int(mesh.nodes.size());mesh.nodes.push_back(node);
	}
	for(unsigned y=0;y<rows;++y) for(unsigned x=0;x<columns;++x) {
		const unsigned a=y*(columns+1)+x,b=a+1,c=b+columns+1,d=a+columns+1;
		Triangle first;first.a=a;first.b=b;first.c=c;mesh.triangles.push_back(first);
		Triangle second;second.a=a;second.b=c;second.c=d;mesh.triangles.push_back(second);
	}
	return mesh;
}
std::vector<BowTestTriangle> bowTestTopologyTriangles(const WaterBowRefinement::Topology &mesh,bool reverse=false)
{
	std::vector<BowTestTriangle> triangles;
	for(const auto &t:mesh.triangles) {
		CHECK(t.a<mesh.nodes.size() && t.b<mesh.nodes.size() && t.c<mesh.nodes.size());
		if(t.a>=mesh.nodes.size() || t.b>=mesh.nodes.size() || t.c>=mesh.nodes.size()) continue;
		BowTestTriangle tri={{mesh.nodes[t.a].screen,mesh.nodes[t.b].screen,mesh.nodes[t.c].screen}};
		if(reverse) std::swap(tri[1],tri[2]);triangles.push_back(tri);
	}
	return triangles;
}
void bowTestAliases(const WaterBowRefinement::Topology &mesh)
{
	for(size_t i=0;i<mesh.nodes.size();++i) {
		const auto &node=mesh.nodes[i];
		CHECK(std::isfinite(node.world.x) && std::isfinite(node.world.y));
		if(node.parent<0) continue;
		CHECK(size_t(node.parent)<i);
		if(size_t(node.parent)>=i) continue;
		const auto &parent=mesh.nodes[size_t(node.parent)];
		CHECK(node.world.x==parent.world.x && node.world.y==parent.world.y);
		CHECK(node.screen.x==parent.screen.x && node.screen.y==parent.screen.y);
		// This models the production complete-sample alias, including a nonlinear
		// floor envelope and UVs. A final-logical-location sample would fail here.
		CHECK(bowTestSample(node.world)==bowTestSample(parent.world));
	}
}
bool bowTestSameTopology(const WaterBowRefinement::Topology &a,const WaterBowRefinement::Topology &b)
{
	if(a.nodes.size()!=b.nodes.size() || a.triangles.size()!=b.triangles.size()) return false;
	for(size_t i=0;i<a.nodes.size();++i) {
		const auto &x=a.nodes[i],&y=b.nodes[i];
		if(x.logical.x!=y.logical.x || x.logical.y!=y.logical.y || x.screen.x!=y.screen.x || x.screen.y!=y.screen.y ||
			x.world.x!=y.world.x || x.world.y!=y.world.y || x.source!=y.source || x.parent!=y.parent) return false;
	}
	for(size_t i=0;i<a.triangles.size();++i) {
		const auto &x=a.triangles[i],&y=b.triangles[i];
		if(x.a!=y.a || x.b!=y.b || x.c!=y.c || x.gate!=y.gate) return false;
	}
	return true;
}
void bowTestNoInteriorCracks(const std::vector<BowTestTriangle> &triangles,double extent)
{
	using Edge=std::array<double,4>;
	std::map<Edge,std::pair<unsigned,int>> incidence;
	for(const auto &triangle:triangles) {
		if(bowTestCoincident(triangle)) continue;
		for(unsigned i=0;i<3;++i) {
			auto a=triangle[i],b=triangle[(i+1)%3];
			if(a.x==b.x && a.y==b.y) continue;
			int sign=1;if(a.x>b.x || (a.x==b.x && a.y>b.y)) { std::swap(a,b);sign=-1; }
			auto &entry=incidence[{{a.x,a.y,b.x,b.y}}];++entry.first;entry.second+=sign;
		}
	}
	for(const auto &edge:incidence) {
		const auto &p=edge.first;
		const bool boundary=(p[0]==0 && p[2]==0)||(p[1]==0 && p[3]==0)||
			(p[0]==extent && p[2]==extent)||(p[1]==extent && p[3]==extent);
		// A folded-down interior pair can temporarily lie on the geometric outer
		// edge. Its opposing incidences still cancel; only single edges are borders.
		CHECK(edge.second.first==2 || (boundary && edge.second.first==1));
		if(edge.second.first==2) CHECK(edge.second.second==0);
	}
}
}

TEST(water_bow_refinement_independent_edge_morphs_preserve_union_and_winding)
{
	using namespace WaterBowRefinement;const auto base=bowTestGrid();const auto projection=bowTestProjection();
	const auto reference=bowTestTopologyTriangles(base);uint32_t random=736117335u;
	for(unsigned fixture=0;fixture<128;++fixture) {
		std::map<std::array<int,3>,double> amounts;
		auto weight=[&](unsigned level,Logical at,BowTestPoint) {
			const std::array<int,3> key={{int(level),at.x,at.y}};
			const auto found=amounts.find(key);if(found!=amounts.end()) return found->second;
			const double t=fixture==0?0:fixture==1?1:fixture<32?double(bowTestRandom(random)&1u):double(bowTestRandom(random))/4294967295.0;
			amounts.emplace(key,t);return t;
		};
		Topology fine;CHECK(Subdivide(base,projection,weight,fine,2,false));
		const auto triangles=bowTestTopologyTriangles(fine);
		CHECK(bowTestCoverage(triangles,reference).Good());bowTestAliases(fine);bowTestNoInteriorCracks(triangles,4);
		// Opposite triangle winding changes no logical edge or physical sample.
		Topology reversed=base,other;for(auto &t:reversed.triangles) std::swap(t.b,t.c);
		std::reverse(reversed.triangles.begin(),reversed.triangles.end());
		CHECK(Subdivide(reversed,projection,weight,other,2,false));
		CHECK(bowTestCoverage(bowTestTopologyTriangles(other,true),reference).Good());
		std::map<std::pair<int,int>,std::array<double,4>> locations;
		for(const auto &n:fine.nodes) locations[{n.logical.x,n.logical.y}]={{n.screen.x,n.screen.y,n.world.x,n.world.y}};
		for(const auto &n:other.nodes) {
			const auto found=locations.find({n.logical.x,n.logical.y});CHECK(found!=locations.end());
			if(found!=locations.end()) CHECK((found->second==std::array<double,4>{{n.screen.x,n.screen.y,n.world.x,n.world.y}}));
		}
		if(fixture==0) CHECK(bowTestSurface(triangles)==bowTestSurface(reference));
	}
}

TEST(water_bow_refinement_zero_child_level_exactly_preserves_current_parent_surface)
{
	using namespace WaterBowRefinement;const auto base=bowTestGrid();const auto projection=bowTestProjection();
	for(unsigned fixture=0;fixture<48;++fixture) {
		auto weight=[fixture](unsigned level,Logical at,BowTestPoint) {
			if(level==2) return 0.0;
			uint32_t random=fixture*173u+unsigned(at.x)*119u+unsigned(at.y)*431u+17u;
			return double(bowTestRandom(random))/4294967295.0;
		};
		Topology first,second;CHECK(Subdivide(base,projection,weight,first,1,false));
		CHECK(Subdivide(base,projection,weight,second,2,false));
		CHECK(bowTestSurface(bowTestTopologyTriangles(first))==bowTestSurface(bowTestTopologyTriangles(second)));
		bowTestAliases(second);
	}
}

TEST(water_bow_refinement_perspective_inverse_uses_actual_morphed_coordinates)
{
	using namespace WaterBowRefinement;auto base=bowTestGrid();auto projection=bowTestProjection();
	projection.matrix[0]=.017;projection.matrix[4]=.023;projection.matrix[2]=-.31;
	projection.matrix[5]=.17;projection.matrix[6]=.0011;projection.matrix[7]=-.0008;
	CHECK(projection.Prepare());
	for(auto &node:base.nodes) CHECK(projection.Transform(node.screen,node.world,true));
	const auto reference=bowTestTopologyTriangles(base);
	for(unsigned fixture=0;fixture<32;++fixture) {
		auto weight=[fixture](unsigned level,Logical p,BowTestPoint) {
			return fixture==0?0.0:(.05+.9*double((p.x*11+p.y*7+int(level)*23+int(fixture)*17)%89)/88);
		};
		Topology fine;CHECK(Subdivide(base,projection,weight,fine,2,false));
		CHECK(bowTestCoverage(bowTestTopologyTriangles(fine),reference).Good());bowTestAliases(fine);
		for(const auto &node:fine.nodes) {
			BowTestPoint expected;CHECK(projection.Transform(node.screen,expected,true));
			CHECK_NEAR(node.world.x,expected.x,1e-10);CHECK_NEAR(node.world.y,expected.y,1e-10);
			if(fixture==0 && node.source<0) CHECK(node.parent>=0);
		}
	}
}

TEST(water_bow_refinement_sparse_zero_one_two_neighborhoods_have_no_t_junctions)
{
	using namespace WaterBowRefinement;const auto base=bowTestGrid(3,3);const auto projection=bowTestProjection();
	const auto reference=bowTestTopologyTriangles(base);
	for(unsigned fixture=0;fixture<48;++fixture) {
		auto weight=[fixture](unsigned level,Logical at,BowTestPoint) {
			const unsigned region=(unsigned(at.x/4)+2u*unsigned(at.y/4)+fixture)%3u;
			if(region<level) return 0.0;
			return fixture<3?1.0:.13+.83*double((at.x*17+at.y*29+int(fixture)*13)%97)/96;
		};
		Topology fine;CHECK(Subdivide(base,projection,weight,fine,2,true));
		const auto triangles=bowTestTopologyTriangles(fine);
		CHECK(bowTestCoverage(triangles,reference).Good());bowTestNoInteriorCracks(triangles,12);bowTestAliases(fine);
		for(const auto &node:fine.nodes) {
			CHECK(node.screen.x>=0 && node.screen.x<=12 && node.screen.y>=0 && node.screen.y<=12);
			if(node.source<0 && node.parent<0)
				CHECK(node.screen.x>0 && node.screen.x<12 && node.screen.y>0 && node.screen.y<12);
		}
	}
}

TEST(water_bow_refinement_coverage_oracle_rejects_fold_and_equal_area_hole_overlap)
{
	const auto reference=bowTestTopologyTriangles(bowTestGrid());CHECK(bowTestCoverage(reference,reference).Good());
	auto folded=reference;std::swap(folded[0][1],folded[0][2]);CHECK(!bowTestCoverage(folded,reference).Good());
	// Duplicate one half and omit the other: unchanged total area, yet half the
	// square is uncovered and the other half overlaps. This arms the union oracle.
	std::vector<BowTestTriangle> equalArea={reference[0],reference[0]};
	const auto audit=bowTestCoverage(equalArea,reference);
	CHECK(audit.areaError==0);CHECK(audit.gap>0 || audit.overlap>0 || audit.edgeError>0);CHECK(!audit.Good());
}

TEST(water_bow_refinement_collapsed_parent_edge_cannot_create_a_new_boundary_sample)
{
	using namespace WaterBowRefinement;const auto base=bowTestGrid();const auto projection=bowTestProjection();
	auto full=[](unsigned,Logical,BowTestPoint) { return 1.0; };
	Topology first,fine;CHECK(Subdivide(base,projection,full,first,1,true));CHECK(Subdivide(base,projection,full,fine,2,true));
	auto find=[](const Topology &mesh,int x,int y)->int {
		for(size_t i=0;i<mesh.nodes.size();++i) if(mesh.nodes[i].logical.x==x && mesh.nodes[i].logical.y==y) return int(i);
		return -1;
	};
	const int lo=find(first,2,0),hi=find(first,4,2),child=find(fine,3,1);
	CHECK(lo>=0 && hi>=0 && child>=0);if(lo<0 || hi<0 || child<0) return;
	const auto a=first.nodes[size_t(lo)].screen,b=first.nodes[size_t(hi)].screen;
	CHECK(a.x==0 && a.y==0 && b.x==4 && b.y==0);
	// Armed old max-parent-edge gate: the diagonal split amount 1 admits this
	// logical interior child although its parents lie on the locked outer edge.
	const Point oldMaxTarget={(a.x+b.x)*.5,(a.y+b.y)*.5};CHECK(oldMaxTarget.x==2 && oldMaxTarget.y==0);
	CHECK(std::abs(bowTestSample(oldMaxTarget)[2]-.5*(bowTestSample(a)[2]+bowTestSample(b)[2]))>.1);
	auto boundarySamples=[](const Topology &mesh) {
		unsigned count=0;for(const auto &n:mesh.nodes)
			if(n.source<0 && n.parent<0 && (n.screen.x==0 || n.screen.x==4 || n.screen.y==0 || n.screen.y==4)) ++count;
		return count;
	};
	CHECK(boundarySamples(fine)==0);CHECK(fine.nodes[size_t(child)].parent>=0);
	auto oldMaxControl=fine;oldMaxControl.nodes[size_t(child)].parent=-1;
	oldMaxControl.nodes[size_t(child)].screen=oldMaxTarget;oldMaxControl.nodes[size_t(child)].world=oldMaxTarget;
	CHECK(boundarySamples(oldMaxControl)==1);
}

TEST(water_bow_refinement_invalid_input_is_atomic_and_zero_levels_are_exact)
{
	using namespace WaterBowRefinement;const auto base=bowTestGrid();const auto projection=bowTestProjection();
	const auto sentinel=bowTestGrid(2,2);Topology result=sentinel;
	auto half=[](unsigned,Logical,BowTestPoint) { return .5; };
	CHECK(Subdivide(base,projection,half,result,0,false));CHECK(bowTestSameTopology(result,base));
	result=sentinel;CHECK(!Subdivide(base,projection,half,result,3,false));CHECK(bowTestSameTopology(result,sentinel));
	for(double value:{-.001,1.001,double(NAN),double(INFINITY)}) {
		result=sentinel;
		auto invalid=[value](unsigned,Logical,BowTestPoint) { return value; };
		CHECK(!Subdivide(base,projection,invalid,result,2,false));CHECK(bowTestSameTopology(result,sentinel));
	}
	auto broken=base;broken.triangles[0].a=unsigned(base.nodes.size()+10);result=sentinel;
	CHECK(!Subdivide(broken,projection,half,result,2,false));CHECK(bowTestSameTopology(result,sentinel));
	auto invalidProjection=projection;invalidProjection.inverse[0]=NAN;result=sentinel;
	CHECK(!Subdivide(base,invalidProjection,half,result,2,false));CHECK(bowTestSameTopology(result,sentinel));
}

namespace {
// Independent parameter oracle with no shared production coefficients.
std::array<double,3> pressureTestParameters(double beam,double speed)
{
	const double u=(std::max)(0.0,(std::min)(speed,250.0)-double(.3f));
	return {{(std::max)(9.0,(std::min)(32.0,6+1.2*beam)),
		(std::max)(4.0,(std::min)(60.0,1.5+.95*beam)),
		(std::max)(3.0,(std::min)(8.0,1.8*std::sqrt(beam)))*u*u/(u*u+26*26)}};
}
}

TEST(water_bow_pressure_size_and_speed_parameters_are_independently_measured)
{
    using namespace WaterVesselWaves;
    for(float beam:{0.0f,.1f,2.5f,6.0f,8.37f,22.0f,54.0f,100.0f}) {
        double previous=-1;
        for(float speed:{0.0f,.3f,.3001f,1.0f,5.0f,14.0f,26.3f,40.0f,70.0f,250.0f,500.0f}) {
            const auto expected=pressureTestParameters(beam,speed);const auto p=BowPressure(beam,speed);
            CHECK_NEAR(p.along,expected[0],.00001);CHECK_NEAR(p.across,expected[1],.00001);CHECK_NEAR(p.amplitude,expected[2],.000003);
            CHECK(p.along>=9 && p.along<=32 && p.across>=4 && p.across<=60 && p.amplitude>=0 && p.amplitude<=8);
            CHECK(p.amplitude>=previous);previous=p.amplitude;
            if(speed<=.3f) CHECK(p.amplitude==0);
        }
        CHECK(BowPressure(beam,70).amplitude>BowPressure(beam,14).amplitude*3);
        CHECK(BowPressure(beam,500).amplitude==BowPressure(beam,250).amplitude);
        // Motion starts quadratically, with zero slope at the rest threshold.
        const auto a=BowPressure(beam,.301f),b=BowPressure(beam,.302f);
        CHECK(a.amplitude<.000001 && b.amplitude>3.9f*a.amplitude && b.amplitude<4.1f*a.amplitude);
    }
    const auto slowBow=BowPressure(8.37f,70),fastBow=BowPressure(54,70);
    CHECK(fastBow.along>slowBow.along && fastBow.across>slowBow.across && fastBow.amplitude>slowBow.amplitude);
    CHECK(std::abs(BowPressure(8.37f,14).amplitude-BowPressure(8.37f,70).amplitude)>2);
}

TEST(water_bow_refinement_uses_all_contact_packets_and_velocity_selected_bow)
{
    using namespace WaterBowRefinement;
    for(unsigned id=1;id<=WaterHullModel::MaxHulls;++id) for(double direction:{0.0,1.3,3.141592653589793,4.7}) {
        const float modelAngle=float(id*.31),speed=float(5+id*3);
        const float vx=speed*float(std::cos(direction)),vy=speed*float(std::sin(direction));
        auto mean=foamTestHull(float(id*37),float(-int(id)*11),vx,vy,modelAngle,7),contact=mean;
        // An asymmetric contact contour distinguishes its support from the mean hull.
        const float polygon[8]={-17,-2,23,-3,19,5,-15,4};
        for(unsigned i=0;i<8;++i)contact.data[4+i/4][i%4]=polygon[i];
        contact.data[1][3]=float(id+2);
        Bow bow;CHECK(MakeBow(id,mean,contact,7,bow));
        const double length=std::hypot(double(vx),double(vy)),fx=vx/length,fy=vy/length;
        CHECK(bow.id==id && bow.level==7);
        CHECK_NEAR(bow.forward.x,fx,1e-6);CHECK_NEAR(bow.forward.y,fy,1e-6);
        const double width=(std::max)(3.0,(std::min)(12.0,contact.data[1][3]*double(.45f)));
        CHECK_NEAR(bow.sigmaAlong,width,.000001);CHECK_NEAR(bow.sigmaAcross,width,.000001);
        CHECK_NEAR(bow.contour.amplitude,pressureTestParameters(contact.data[1][3],length)[2],.000003);
        CHECK_NEAR(bow.centre.x,bow.origin.x+.5*width*fx,.000001);
        CHECK_NEAR(bow.centre.y,bow.origin.y+.5*width*fy,.000001);
        CHECK(bow.hullCentre.x==contact.data[0][0] && bow.hullCentre.y==contact.data[0][1]);
        const double ax=contact.data[1][0],ay=contact.data[1][1];
        const double localForwardX=fx*ax+fy*ay,localForwardY=-fx*ay+fy*ax;
        double support=-1e30;
        for(unsigned i=0;i<4;++i) {
            CHECK(bow.contour.points[i].x==polygon[i*2] && bow.contour.points[i].y==polygon[i*2+1]);
            support=(std::max)(support,polygon[i*2]*localForwardX+polygon[i*2+1]*localForwardY);
        }
        const double dx=bow.origin.x-bow.hullCentre.x,dy=bow.origin.y-bow.hullCentre.y;
        CHECK_NEAR((dx*ax+dy*ay)*localForwardX+(-dx*ay+dy*ax)*localForwardY,support,.00001);
        contact.data[0][3]=0;Bow fallback,expected;
        CHECK(MakeBow(id,mean,contact,7,fallback));CHECK(MakeBow(id,mean,mean,7,expected));
        CHECK(fallback.origin.x==expected.origin.x && fallback.origin.y==expected.origin.y);
        CHECK(fallback.centre.x==expected.centre.x && fallback.centre.y==expected.centre.y);
    }
}

TEST(water_bow_registration_refinement_encloses_bounded_horizontal_inverse_image)
{
    using namespace WaterBowRefinement;
    // CommonSource emits MaxHorizontalDisplacement/sqrt(2) per component; test
    // its full axis-aligned square, including diagonal corners, at rotated hulls.
    const double componentRadius=WaterFftGeometry::MaxHorizontalDisplacement/std::sqrt(2.0);
    for(float beam:{.1f,6.0f,9.0f,30.0f,80.0f})for(unsigned orientation=0;orientation<8;++orientation) {
        const double angle=orientation*6.283185307179586/8;
        auto hull=foamTestHull(0,0,float(40*std::cos(angle)),float(40*std::sin(angle)),float(angle));hull.data[1][3]=beam;
        Bow bow;CHECK(MakeBow(1,hull,hull,7,bow));
        Prepared prepared;prepared.bow=bow;prepared.cell={20,20};prepared.desiredDepth=2;prepared.heightGate=1;
        const Logical at={80,80};
        const double amplitude=pressureTestParameters(beam,std::hypot(hull.data[2][0],hull.data[2][1]))[2];
        for(int x=-6;x<=6;++x)for(int y=-6;y<=6;++y)
            for(double shiftX:{-1.0,-.5,0.0,.5,1.0})for(double shiftY:{-1.0,-.5,0.0,.5,1.0}) {
            const double localX=x*bow.sigmaAlong*.6,localY=y*bow.sigmaAcross*.6;
            const Point parameter={bow.origin.x+localX*bow.forward.x-localY*bow.forward.y,
                bow.origin.y+localX*bow.forward.y+localY*bow.forward.x};
            const Point mapped={parameter.x+componentRadius*shiftX,parameter.y+componentRadius*shiftY};
            const double expected=WaterBowShoulderTest::oracle(hull,mapped.x,mapped.y)/amplitude;
            CHECK(Demand(prepared,1,at,parameter)+1e-12>=expected);
            CHECK(Demand(prepared,2,at,parameter)+1e-12>=expected);
            // World expansion must never defeat the hard cell/region cost limits.
            CHECK(Demand(prepared,1,{97,80},parameter)==0);
        }
    }
    CHECK(MaxRegions==3 && RegionVertices==2730 && RegionTriangles==5460);
    CHECK(MaxAddedVertices==8190 && MaxAddedTriangles==16380);
}

TEST(water_bow_refinement_stationary_wrong_level_and_nonfinite_packets_are_rejected)
{
	using namespace WaterBowRefinement;Bow bow;auto hull=foamTestHull(0,0,30,0);
	CHECK(MakeBow(1,hull,hull,7,bow));CHECK(!MakeBow(1,hull,hull,7.11,bow));
	CHECK(!MakeBow(1,hull,hull,NAN,bow));
	for(float speed:{0.0f,.01f,.29f,.3f}) {
		const auto stopped=foamTestHull(0,0,speed,0);CHECK(!MakeBow(1,stopped,stopped,7,bow));
	}
	const auto slow=foamTestHull(0,0,.31f,0);CHECK(MakeBow(1,slow,slow,7,bow));CHECK(bow.contour.amplitude>0 && bow.contour.amplitude<.00001);
	for(unsigned field:{0u,1u,2u,3u}) {
		auto invalid=hull;invalid.data[field][0]=NAN;CHECK(!MakeBow(1,invalid,invalid,7,bow));
	}
	// Match the shader selector: an invalid contact count falls back to the
	// intact mean packet, including a malformed contact's unrelated coordinates.
	Bow valid;CHECK(MakeBow(1,hull,hull,7,valid));
	for(float count:{0.0f,2.0f,13.0f,float(NAN)}) {
		auto invalidContact=hull;invalidContact.data[0][3]=count;invalidContact.data[0][0]=NAN;
		CHECK(MakeBow(1,hull,invalidContact,7,bow));
		CHECK(bow.origin.x==valid.origin.x && bow.origin.y==valid.origin.y && bow.contour.amplitude==valid.contour.amplitude);
	}
	hull.data[0][3]=0;CHECK(!MakeBow(1,hull,hull,7,bow));
}

TEST(water_bow_refinement_projected_width_matches_independent_ellipse_sampling)
{
	using namespace WaterBowRefinement;
	for(unsigned fixture=0;fixture<24;++fixture) {
		const double angle=fixture*.319;
		const auto hull=foamTestHull(float(fixture*3),float(-int(fixture)*2),float(30*std::cos(angle)),float(30*std::sin(angle)),float(angle));
		Bow bow;CHECK(MakeBow(fixture+1,hull,hull,7,bow));View view;
		view.projection=bowTestProjection();view.projection.matrix[0]=.004;view.projection.matrix[1]=fixture*.00003;
		view.projection.matrix[3]=-.001;view.projection.matrix[4]=.007;
		view.projection.matrix[6]=fixture*.00001;view.projection.matrix[7]=-.00012;
		view.projection.margin=.12;CHECK(view.projection.Prepare());view.resolution={100,80};view.width=1280;view.height=720;
		view.verticalClip[0]=.0001;view.verticalClip[1]=.0005;view.verticalClip[2]=.00001;
		Prepared prepared;CHECK(PrepareBow(bow,view,prepared,.75));
		// Search the projected unit covariance ellipse directly with central
		// differences, independent of the production Jacobian/eigenvalue formula.
		double minor=1e30;constexpr double epsilon=.002;
		for(unsigned i=0;i<2048;++i) {
			const double phase=i*6.283185307179586/2048;
			const double dx=bow.sigmaAlong*bow.forward.x*std::cos(phase)-bow.sigmaAcross*bow.forward.y*std::sin(phase);
			const double dy=bow.sigmaAlong*bow.forward.y*std::cos(phase)+bow.sigmaAcross*bow.forward.x*std::sin(phase);
			BowTestPoint lo,hi;
			CHECK(view.projection.Transform({bow.centre.x-epsilon*dx,bow.centre.y-epsilon*dy},lo));
			CHECK(view.projection.Transform({bow.centre.x+epsilon*dx,bow.centre.y+epsilon*dy},hi));
			minor=(std::min)(minor,std::hypot((hi.x-lo.x)*view.width/(4*epsilon),(hi.y-lo.y)*view.height/(4*epsilon)));
		}
		const double step=(std::max)(1.12*view.width/100,1.12*view.height/80);
		// The contour ridge uses exp(-q*q) inside its compact fade. Independently
		// find its half-height radius rather than assuming a Gaussian standard deviation.
		double halfLo=0,halfHi=2;
		for(unsigned i=0;i<48;++i) { const double q=(halfLo+halfHi)*.5;if(std::exp(-q*q)>.5) halfLo=q;else halfHi=q; }
		const double expected=(std::max)(0.0,(std::min)(2.0,std::log2(3*step/((halfLo+halfHi)*minor))));
		CHECK_NEAR(prepared.desiredDepth,expected,.0001);
		CHECK(prepared.heightGate>=0 && prepared.heightGate<=1);
		const auto *m=view.projection.matrix;
		const double clipX=m[0]*bow.centre.x+m[1]*bow.centre.y+m[2];
		const double clipY=m[3]*bow.centre.x+m[4]*bow.centre.y+m[5];
		const double clipW=m[6]*bow.centre.x+m[7]*bow.centre.y+m[8];
		auto raised=[&](double z) {
			return BowTestPoint{(clipX+z*view.verticalClip[0])/(clipW+z*view.verticalClip[2]),
				(clipY+z*view.verticalClip[1])/(clipW+z*view.verticalClip[2])};
		};
		const auto lower=raised(-epsilon),upper=raised(epsilon);
		const double verticalPixels=std::hypot((upper.x-lower.x)*view.width/(4*epsilon),(upper.y-lower.y)*view.height/(4*epsilon));
		const double pressureHeight=WaterBowShoulderTest::oracle(hull,bow.centre.x,bow.centre.y);
		const double bound=WaterFftGeometry::MaxVerticalDisplacement;
		double height=(verticalPixels*bound*std::tanh(pressureHeight/bound)*.75-.25)/.75;
		height=(std::max)(0.0,(std::min)(1.0,height));height=height*height*(3-2*height);
		CHECK_NEAR(prepared.heightGate,height,.000001);
		BowTestPoint anchorScreen;CHECK(view.projection.Transform(bow.origin,anchorScreen));
		CHECK_NEAR(prepared.cell.x,(anchorScreen.x/1.12+1)*50,1e-10);
		CHECK_NEAR(prepared.cell.y,(anchorScreen.y/1.12+1)*40,1e-10);
		Prepared hidden;CHECK(PrepareBow(bow,view,hidden,0));CHECK(hidden.heightGate==0);
		const Logical at={int(std::round(prepared.cell.x*4)),int(std::round(prepared.cell.y*4))};
		for(unsigned level:{1u,2u}) CHECK(Demand(hidden,level,at,bow.centre)==0);
	}
}

TEST(water_bow_refinement_demand_is_bounded_local_continuous_and_pause_repeatable)
{
	using namespace WaterBowRefinement;Bow bow;const auto hull=foamTestHull(0,0,30,0);
	CHECK(MakeBow(1,hull,hull,7,bow));Prepared p;p.bow=bow;p.cell={20,20};p.heightGate=1;
	const double radius=WaterFftGeometry::MaxHorizontalDisplacement;
	const Logical at={80,80};double previous[2]={0,0};
	for(unsigned i=0;i<=400;++i) {
		p.desiredDepth=double(i)/200;
		const double a=Demand(p,1,at,bow.centre),b=Demand(p,2,at,bow.centre);
		CHECK(a>=0 && a<=1 && b>=0 && b<=a);CHECK(a>=previous[0] && b>=previous[1]);
		CHECK(a-previous[0]<.008 && b-previous[1]<.008);previous[0]=a;previous[1]=b;
		CHECK(a==Demand(p,1,at,bow.centre) && b==Demand(p,2,at,bow.centre));
	}
	CHECK(previous[0]==1 && previous[1]==1);
	for(unsigned level:{1u,2u}) {
		CHECK(Demand(p,level,at,bow.centre,0)==0);
		CHECK_NEAR(Demand(p,level,at,bow.centre,.37),.37,1e-12);
		CHECK(Demand(p,level,{at.x+17,at.y},bow.centre)==0);
		CHECK(Demand(p,level,at,{bow.centre.x+3*bow.contour.width+radius+.001,bow.origin.y})==0);
		CHECK(Demand(p,level,at,{bow.origin.x,bow.hullCentre.y+hull.data[1][3]+3.5*bow.contour.width+radius+.001})==0);
		CHECK(Demand(p,level,at,{NAN,0})==0);
	}
	CHECK(Demand(p,0,at,bow.centre)==0 && Demand(p,3,at,bow.centre)==0);
    const BowTestPoint flank={bow.hullCentre.x+10,bow.hullCentre.y+hull.data[1][3]+.5*bow.contour.width};
    CHECK_NEAR(Demand(p,1,at,flank),1,1e-12);
	// The original radial culling contract also limits the contour ridge.
	CHECK(Demand(p,1,at,{bow.hullCentre.x+bow.reach+radius,bow.hullCentre.y})==0);
}

namespace {
WaterBowRefinement::View bowTestView()
{
	WaterBowRefinement::View view;view.projection=testWaterProjection();view.projection.matrix[0]=.005;
	view.projection.matrix[4]=.005;CHECK(view.projection.Prepare());view.resolution={40,40};view.width=1280;view.height=720;
	view.verticalClip[1]=.01;return view;
}
WaterBowRefinement::Active bowTestActive(const WaterBowRefinement::View &view,double x,double y,unsigned id=1)
{
	using namespace WaterBowRefinement;const auto hull=foamTestHull(float(x-13.5),float(y),30,0);
	// Place the compact exterior contour ridge at the requested world-space centre.
	Active active;Bow bow;CHECK(MakeBow(id,hull,hull,7,bow));CHECK(PrepareBow(bow,view,active.prepared));
	active.amount=1;return active;
}
bool bowTestSameMesh(const WaterProjectedMesh::Mesh &a,const WaterProjectedMesh::Mesh &b)
{
	if(a.indices!=b.indices || a.cells!=b.cells || a.vertices.size()!=b.vertices.size() ||
		a.collars.size()!=b.collars.size() || a.transitions.size()!=b.transitions.size()) return false;
	for(size_t i=0;i<a.vertices.size();++i) if(!bowTestSameVertex(a.vertices[i],b.vertices[i])) return false;
	// These arrays are not touched or reconstructed by optional refinement. Byte
	// comparison against the saved copy also catches accidental metadata writes.
	for(size_t i=0;i<a.transitions.size();++i)
		if(std::memcmp(&a.transitions[i],&b.transitions[i],sizeof(a.transitions[i]))) return false;
	for(size_t i=0;i<a.collars.size();++i)
		if(std::memcmp(&a.collars[i],&b.collars[i],sizeof(a.collars[i]))) return false;
	return true;
}
const WaterBowRefinement::Admissions::Slot *bowTestSlot(const WaterBowRefinement::Admissions &admissions,unsigned id)
{
	for(const auto &slot:admissions.Slots()) if(slot.occupied && slot.bow.id==id) return &slot;
	return nullptr;
}
}

TEST(water_bow_refinement_projected_build_keeps_protected_metadata_and_boundary_triangles)
{
	using namespace WaterProjectedMesh;using namespace WaterBowRefinement;
	const Point corners[4]={{-190,-180},{195,-165},{185,190},{-180,180}};const auto view=bowTestView();
	std::vector<BoundaryKnot> knots;
	for(unsigned edge=0;edge<4;++edge) {
		Point a=corners[edge],b=corners[(edge+1)%4];if(a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
		for(unsigned i=0;i<=17;++i) { const double t=double(i)/17;knots.push_back({{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t},int(edge),int(i),t}); }
	}
	Mesh base;CHECK(Build(corners,view.projection,view.resolution,base,60000,knots));
	CHECK(!base.collars.empty() && !base.transitions.empty());
	const auto reference=bowTestTriangles(base,view.projection);
	for(Point centre:{Point{0,0},Point{-157,0},Point{-143,0},Point{145,0}}) {
		const auto active=bowTestActive(view,centre.x,centre.y);Mesh fine=base;Stats stats;
		CHECK(Refine(fine,view,corners,{active},60000,stats));
		CHECK(fine.vertices.size()>=base.vertices.size());CHECK(stats.addedVertices<=RegionVertices && stats.addedTriangles<=RegionTriangles);
		CHECK(fine.vertices.size()<65535);CHECK(bowTestCoverage(bowTestTriangles(fine,view.projection),reference).Good());
		for(size_t i=0;i<base.vertices.size();++i) CHECK(bowTestSameVertex(base.vertices[i],fine.vertices[i]));
		CHECK(base.collars.size()==fine.collars.size() && base.transitions.size()==fine.transitions.size());
		std::set<std::array<uint16_t,3>> triangles;
		for(size_t i=0;i<fine.indices.size();i+=3) triangles.insert({{fine.indices[i],fine.indices[i+1],fine.indices[i+2]}});
		unsigned protectedCount=0;
		for(size_t i=0;i<base.indices.size();i+=3) {
			bool protectedTriangle=false;
			for(unsigned j=0;j<3;++j) {
				const auto &v=base.vertices[base.indices[i+j]];
				protectedTriangle|=v.boundary>=0 || v.collar>=0 || v.transition>=0 || v.knot>=0 || v.lattice<0;
			}
			if(protectedTriangle) { ++protectedCount;CHECK(triangles.count({{base.indices[i],base.indices[i+1],base.indices[i+2]}})==1); }
		}
		CHECK(protectedCount>0);
		if(centre.x==0) CHECK(stats.addedVertices>0 && stats.addedTriangles>0);
		for(size_t i=base.vertices.size();i<fine.vertices.size();++i) {
			const auto &v=fine.vertices[i];CHECK(v.boundary<0 && v.collar<0 && v.transition<0 && v.knot<0 && v.lattice<0);
			const auto collar=CollarAt(corners,v.world);CHECK(collar.count==0);
		}
	}
}

TEST(water_bow_refinement_denied_budget_and_zero_demand_preserve_exact_base_mesh)
{
	using namespace WaterProjectedMesh;using namespace WaterBowRefinement;
	const Point corners[4]={{-300,-300},{300,-300},{300,300},{-300,300}};const auto view=bowTestView();
	Mesh base;CHECK(Build(corners,view.projection,view.resolution,base));const auto active=bowTestActive(view,0,0);
	for(size_t budget:{base.vertices.size(),base.vertices.size()+1}) {
		Mesh denied=base;Stats stats;CHECK(!Refine(denied,view,corners,{active},budget,stats));
		CHECK(stats.rejected && bowTestSameMesh(denied,base));
	}
	for(unsigned exhausted=0;exhausted<2;++exhausted) {
		Mesh denied=base;Stats stats;
		CHECK(!Refine(denied,view,corners,{active},60000,stats,exhausted?MaxAddedVertices:0,exhausted?0:MaxAddedTriangles));
		CHECK(stats.rejected && bowTestSameMesh(denied,base));
	}
	for(unsigned field=0;field<5;++field) {
		auto invalid=active;
		if(field==0) invalid.amount=NAN;if(field==1) invalid.amount=1.1;
		if(field==2) invalid.prepared.desiredDepth=NAN;if(field==3) invalid.prepared.heightGate=-.1;
		if(field==4) invalid.prepared.heightGate=INFINITY;
		Mesh denied=base;Stats stats;CHECK(!Refine(denied,view,corners,{invalid},60000,stats));
		CHECK(stats.rejected && bowTestSameMesh(denied,base));
	}
	{
		Mesh invalid=base;invalid.indices[0]=uint16_t(base.vertices.size()+1);const auto saved=invalid;Stats stats;
		CHECK(!Refine(invalid,view,corners,{active},60000,stats));CHECK(stats.rejected && bowTestSameMesh(invalid,saved));
	}
	for(unsigned reason=0;reason<4;++reason) {
		auto noDemand=active;
		if(reason==0) noDemand.amount=0;if(reason==1) noDemand.prepared.heightGate=0;
		if(reason==2) noDemand.prepared.desiredDepth=0;
		if(reason==3) noDemand.prepared.cell={-1000,-1000};
		Mesh same=base;Stats stats;CHECK(Refine(same,view,corners,{noDemand},60000,stats));
		CHECK(!stats.rejected && stats.addedVertices==0 && bowTestSameMesh(same,base));
	}
	Mesh denied=base;Stats stats;
	CHECK(!Refine(denied,view,corners,std::vector<Active>(MaxRegions+1,active),60000,stats));
	CHECK(stats.rejected && bowTestSameMesh(denied,base));
	std::vector<Active> admitted;
	for(unsigned id=1;id<=MaxRegions;++id) admitted.push_back(bowTestActive(view,double(int(id)-2)*75,0,id));
	Mesh bounded=base;CHECK(Refine(bounded,view,corners,admitted,60000,stats));
	CHECK(stats.addedVertices>0 && stats.addedVertices<=MaxAddedVertices && stats.addedTriangles<=MaxAddedTriangles);
	CHECK(bounded.vertices.size()==base.vertices.size()+stats.addedVertices && bounded.vertices.size()<65535);
	CHECK(bowTestCoverage(bowTestTriangles(bounded,view.projection),bowTestTriangles(base,view.projection)).Good());
}

TEST(water_bow_refinement_admission_reserves_capacity_and_retires_without_replacement_pop)
{
	using namespace WaterBowRefinement;
	const Point corners[4]={{-300,-300},{300,-300},{300,300},{-300,300}};const auto key=MakePatchKey(corners,7);
	const size_t patchBaseBudget=2048;CHECK(patchBaseBudget<Admissions::WorstBaseVertices());
	std::array<Bow,WaterHullModel::MaxHulls> bows;
	for(unsigned i=0;i<bows.size();++i) { const auto hull=foamTestHull(float(i*5),0,30,0);CHECK(MakeBow(i+1,hull,hull,7,bows[i])); }
	Admissions state;state.Begin(0);
	for(unsigned i=0;i<bows.size();++i) CHECK(state.Request(key,bows[i],patchBaseBudget)==(i<MaxRegions));
	state.End();CHECK(bowTestSlot(state,1) && bowTestSlot(state,2) && bowTestSlot(state,3));
	CHECK(bowTestSlot(state,1)->amount==0 && bowTestSlot(state,2)->amount==0 && bowTestSlot(state,3)->amount==0);
	double previous=0,time=0;
	for(unsigned frame=1;frame<=16;++frame) {
		time=frame*.025;state.Begin(time);
		// Demand order and small viewport changes cannot replace retained slots.
		for(unsigned i=MaxRegions;i<bows.size();++i) CHECK(!state.Request(key,bows[i],patchBaseBudget));
		CHECK(state.Request(key,bows[2],patchBaseBudget));CHECK(state.Request(key,bows[1],patchBaseBudget));
		CHECK(state.Request(key,bows[0],patchBaseBudget));
		const double firstRequest=bowTestSlot(state,1)->amount;CHECK(state.Request(key,bows[0],patchBaseBudget));
		CHECK(bowTestSlot(state,1)->amount==firstRequest);state.End();
		const double amount=bowTestSlot(state,1)->amount;CHECK(amount>=previous && amount-previous<=.100000000001);previous=amount;
	}
	CHECK(previous==1);
	for(unsigned repeat=0;repeat<8;++repeat) {
		state.Begin(time);for(unsigned i=0;i<MaxRegions;++i) CHECK(state.Request(key,bows[i],patchBaseBudget));state.End();
		CHECK(bowTestSlot(state,1)->amount==1 && bowTestSlot(state,2)->amount==1);
	}
	previous=1;
	for(unsigned frame=1;frame<=12 && bowTestSlot(state,1);++frame) {
		time+=.025;state.Begin(time);CHECK(state.Request(key,bows[1],patchBaseBudget));CHECK(state.Request(key,bows[2],patchBaseBudget));
		CHECK(!state.Request(key,bows[3],patchBaseBudget));state.End();
		const auto *retiring=bowTestSlot(state,1);
		const double amount=retiring?retiring->amount:0;CHECK(amount<=previous && previous-amount<=.100000000001);previous=amount;
	}
	CHECK(!bowTestSlot(state,1));
	// The next request after the outgoing reserve retires starts exactly collapsed.
	time+=.025;state.Begin(time);CHECK(state.Request(key,bows[1],patchBaseBudget));CHECK(state.Request(key,bows[2],patchBaseBudget));
	CHECK(state.Request(key,bows[3],patchBaseBudget));state.End();
	CHECK(bowTestSlot(state,4) && bowTestSlot(state,4)->amount==0);CHECK(bowTestSlot(state,2)->amount==1);
	CHECK(!(key==MakePatchKey(corners,8)));
	state.Clear();for(const auto &slot:state.Slots()) CHECK(!slot.occupied);
}

TEST(water_bow_refinement_teleport_clock_reset_and_all_eight_hulls_stay_bounded)
{
	using namespace WaterBowRefinement;
	const Point corners[4]={{-300,-300},{300,-300},{300,300},{-300,300}};const auto key=MakePatchKey(corners,7);
	Admissions state;std::array<Bow,WaterHullModel::MaxHulls> bows;
	for(unsigned i=0;i<bows.size();++i) { const auto hull=foamTestHull(float(i*5),0,30,0);CHECK(MakeBow(i+1,hull,hull,7,bows[i])); }
	state.Begin(10);for(unsigned i=0;i<bows.size();++i) CHECK(state.Request(key,bows[i],60000)==(i<MaxRegions));state.End();
	state.Begin(10.25);for(unsigned i=0;i<MaxRegions;++i) CHECK(state.Request(key,bows[i],60000));state.End();
	for(const auto &slot:state.Slots()) CHECK(slot.occupied && slot.amount==1);
	const auto oldCentre=bows[0].centre;bows[0].centre.x+=1000;
	state.Begin(10.275);CHECK(!state.Request(key,bows[0],60000));
	for(unsigned i=1;i<MaxRegions;++i) CHECK(state.Request(key,bows[i],60000));state.End();
	const auto *old=bowTestSlot(state,1);CHECK(old && old->bow.centre.x==oldCentre.x && old->amount>0 && old->amount<1);
	state.Begin(1);for(const auto &slot:state.Slots()) CHECK(!slot.occupied); // new level/water clock
	CHECK(state.Request(key,bows[0],60000));state.End();CHECK(bowTestSlot(state,1)->amount==0);
	state.Begin(NAN);for(const auto &slot:state.Slots()) CHECK(!slot.occupied);
}

TEST(water_bow_refinement_adjacent_reversed_authored_edges_remain_exact)
{
	using namespace WaterProjectedMesh;using namespace WaterBowRefinement;const auto view=bowTestView();
	const Point left[4]={{-180,-180},{0,-180},{0,180},{-180,180}};
	const Point right[4]={{180,180},{0,180},{0,-180},{180,-180}};
	Mesh a,b;CHECK(Build(left,view.projection,view.resolution,a));CHECK(Build(right,view.projection,view.resolution,b));
	const Mesh originalA=a,originalB=b;Stats stats;
	CHECK(Refine(a,view,left,{bowTestActive(view,-90,0)},60000,stats));
	CHECK(Refine(b,view,right,{bowTestActive(view,90,0,2)},60000,stats));
	auto boundary=[](const Mesh &mesh) {
		std::vector<double> values;
		for(const auto &vertex:mesh.vertices) if(vertex.world.x==0 && vertex.boundary>=0) values.push_back(vertex.world.y);
		std::sort(values.begin(),values.end());return values;
	};
	CHECK(boundary(a)==boundary(originalA) && boundary(b)==boundary(originalB));CHECK(boundary(a)==boundary(b));
	CHECK(bowTestCoverage(bowTestTriangles(a,view.projection),bowTestTriangles(originalA,view.projection)).Good());
	CHECK(bowTestCoverage(bowTestTriangles(b,view.projection),bowTestTriangles(originalB,view.projection)).Good());
}

namespace {
double bowTestInterpolatedSurface(const WaterProjectedMesh::Mesh &mesh,BowTestPoint q)
{
	for(size_t i=0;i<mesh.indices.size();i+=3) {
		const auto a=mesh.vertices[mesh.indices[i]].world,b=mesh.vertices[mesh.indices[i+1]].world,c=mesh.vertices[mesh.indices[i+2]].world;
		const double den=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
		if(std::abs(den)<1e-12) continue;
		const double u=((b.y-c.y)*(q.x-c.x)+(c.x-b.x)*(q.y-c.y))/den;
		const double v=((c.y-a.y)*(q.x-c.x)+(a.x-c.x)*(q.y-c.y))/den,w=1-u-v;
		if(u>=-1e-10 && v>=-1e-10 && w>=-1e-10)
			return u*bowTestSample(a)[2]+v*bowTestSample(b)[2]+w*bowTestSample(c)[2];
	}
	CHECK(false);return NAN;
}
}

TEST(water_bow_refinement_window_crossings_are_continuous_and_resize_rebuilds_cleanly)
{
	using namespace WaterProjectedMesh;using namespace WaterBowRefinement;
	const Point corners[4]={{-300,-300},{300,-300},{300,300},{-300,300}};const auto view=bowTestView();
	Mesh base;CHECK(Build(corners,view.projection,view.resolution,base));
	for(double column:{19.0,20.0,21.0}) {
		Mesh side[2];
		for(unsigned sign=0;sign<2;++sign) {
			const double cell=column+(sign?1:-1)*.000001;
			const double screen=1.12*(cell*2/view.resolution.columns-1);BowTestPoint world;
			CHECK(view.projection.Transform({screen,0},world,true));
			// Preserve this tiny double displacement: native Hull packets are floats,
			// so use a prepared descriptor to test the window threshold itself.
			auto active=bowTestActive(view,0,0);active.prepared.cell.x=cell;active.prepared.bow.centre=world;
			side[sign]=base;Stats stats;CHECK(Refine(side[sign],view,corners,{active},60000,stats));
			CHECK(stats.addedVertices>0 && !stats.rejected);
		}
		for(unsigned y=0;y<7;++y) for(unsigned x=0;x<7;++x) {
			const Point q={-29.731+x*9.37,-28.913+y*9.13};
			const double a=bowTestInterpolatedSurface(side[0],q),b=bowTestInterpolatedSurface(side[1],q);
			CHECK(std::abs(a-b)<.001);
		}
	}
	for(Resolution resolution:{Resolution{40,40},Resolution{64,48},Resolution{40,40}}) {
		auto resized=view;resized.resolution=resolution;resized.width=resolution.columns*24;resized.height=resolution.rows*16;
		Mesh rebuilt;CHECK(Build(corners,resized.projection,resolution,rebuilt));const auto original=rebuilt;
		Stats stats;CHECK(Refine(rebuilt,resized,corners,{bowTestActive(resized,0,0)},60000,stats));
		CHECK(stats.addedVertices<=RegionVertices && stats.addedTriangles<=RegionTriangles);
		CHECK(bowTestCoverage(bowTestTriangles(rebuilt,resized.projection),bowTestTriangles(original,resized.projection)).Good());
		Mesh repeat=original;Stats repeated;
		CHECK(Refine(repeat,resized,corners,{bowTestActive(resized,0,0)},60000,repeated));CHECK(bowTestSameMesh(rebuilt,repeat));
	}
}

TEST(water_bow_refinement_small_patch_budget_uses_separate_reserve_without_reducing_base)
{
	using namespace WaterProjectedMesh;using namespace WaterBowRefinement;
	const Point corners[4]={{-300,-300},{300,-300},{300,300},{-300,300}};auto view=bowTestView();
	const size_t patchBaseBudget=1600;view.resolution=ChooseResolution(1280,720,80,262144);
	CHECK(VertexBound(view.resolution.columns,view.resolution.rows)<=262144u/80u);
	Mesh base;CHECK(Build(corners,view.projection,view.resolution,base,patchBaseBudget));
	Admissions state;state.Begin(0);const auto active=bowTestActive(view,0,0);
	CHECK(state.Request(MakePatchKey(corners,7),active.prepared.bow,patchBaseBudget));state.End();
	Mesh fine=base;Stats stats;CHECK(Refine(fine,view,corners,{active},patchBaseBudget+RegionVertices,stats));
	CHECK(stats.addedVertices>0 && stats.addedVertices<=RegionVertices);
	CHECK(fine.cells==base.cells && fine.vertices.size()==base.vertices.size()+stats.addedVertices);
	for(size_t i=0;i<base.vertices.size();++i) CHECK(bowTestSameVertex(base.vertices[i],fine.vertices[i]));
	CHECK(bowTestCoverage(bowTestTriangles(fine,view.projection),bowTestTriangles(base,view.projection)).Good());
	CHECK(MaxAddedVertices==8190 && MaxAddedTriangles==16380);
	// Even an oversized caller budget cannot wrap the 16-bit draw indices.
	Mesh nearLimit=base;nearLimit.vertices.resize(65534,base.vertices[0]);const auto saved=nearLimit;
	CHECK(!Refine(nearLimit,view,corners,{active},100000,stats));CHECK(stats.rejected && bowTestSameMesh(nearLimit,saved));
	Topology sentinel=bowTestGrid(),output=sentinel;const auto many=bowTestGrid(26,26);
	auto full=[](unsigned,Logical,BowTestPoint) { return 1.0; };
	CHECK(!Subdivide(many,bowTestProjection(),full,output,2,false));CHECK(bowTestSameTopology(output,sentinel));
}

TEST(water_bow_refinement_invalid_view_does_not_create_demand)
{
	using namespace WaterBowRefinement;const auto good=bowTestView();const auto active=bowTestActive(good,0,0);
	for(unsigned field=0;field<7;++field) {
		auto view=good;auto bow=active.prepared.bow;Prepared result;result.desiredDepth=.713;
		if(field==0) view.width=NAN;if(field==1) view.height=INFINITY;
		if(field==2) bow.centre.x=NAN;if(field==3) bow.forward.y=INFINITY;
		if(field==4) bow.sigmaAcross=NAN;if(field==5) bow.contour.amplitude=NAN;
		const double amplitude=field==6?double(NAN):1;
		CHECK(!PrepareBow(bow,view,result,amplitude));CHECK(result.desiredDepth==.713);
	}
}

TEST(water_bow_refinement_coincident_patch_owners_share_the_global_cap)
{
	using namespace WaterBowRefinement;const Point corners[4]={{-300,-300},{300,-300},{300,300},{-300,300}};
	Bow bow;const auto hull=foamTestHull(0,0,30,0);CHECK(MakeBow(1,hull,hull,7,bow));
	Admissions state;state.Begin(0);
	CHECK(!state.Request(MakePatchKey(corners,7),bow,0));CHECK(!state.Request(MakePatchKey(corners,7),bow,65536));
	for(unsigned owner=1;owner<=8;++owner) {
		const auto key=MakePatchKey(corners,7,uintptr_t(owner),0);
		CHECK(state.Request(key,bow,1024)==(owner<=MaxRegions));
		CHECK(!(key==MakePatchKey(corners,7,uintptr_t(owner),1)));
		CHECK(!(key==MakePatchKey(corners,7,uintptr_t(owner+1),0)));
	}
	state.End();unsigned occupied=0;for(const auto &slot:state.Slots()) occupied+=slot.occupied?1u:0u;
	CHECK(occupied==MaxRegions && occupied*RegionVertices<=MaxAddedVertices && occupied*RegionTriangles<=MaxAddedTriangles);
}

TEST(water_bow_refinement_unrelated_visible_patches_cannot_spend_bow_admission)
{
	using namespace WaterProjectedMesh;using namespace WaterBowRefinement;
	auto view=bowTestView();view.resolution={80,80};const auto actual=bowTestActive(view,0,0);
	const Point unrelated[3][4]={
		{{-220,-60},{-110,-60},{-110,60},{-220,60}},
		{{110,-60},{220,-60},{220,60},{110,60}},
		{{-60,110},{60,110},{60,220},{-60,220}}};
	const Point ownCentre[3]={{-165,0},{165,0},{0,165}};
	Admissions admissions;admissions.Begin(0);
	for(unsigned i=0;i<3;++i) {
		Mesh mesh;CHECK(Build(unrelated[i],view.projection,view.resolution,mesh));const auto saved=mesh;
		CHECK(!mesh.indices.empty());CHECK(!HasRefinableDemand(mesh,view,unrelated[i],actual.prepared));
		if(HasRefinableDemand(mesh,view,unrelated[i],actual.prepared))
			admissions.Request(MakePatchKey(unrelated[i],7,uintptr_t(i+1)),actual.prepared.bow,60000);
		CHECK(bowTestSameMesh(mesh,saved));
		// Prove each negative is spatial, not an empty, invisible or universally
		// protected fixture: a bow at that polygon's own centre really adds detail.
		const auto local=bowTestActive(view,ownCentre[i].x,ownCentre[i].y,i+2);
		CHECK(HasRefinableDemand(mesh,view,unrelated[i],local.prepared));
		Stats stats;CHECK(Refine(mesh,view,unrelated[i],{local},60000,stats));CHECK(stats.addedVertices>0);
	}
	for(const auto &slot:admissions.Slots()) CHECK(!slot.occupied);
	const Point boatPatch[4]={{-100,-100},{100,-100},{100,100},{-100,100}};
	Mesh mesh;CHECK(Build(boatPatch,view.projection,view.resolution,mesh));const auto saved=mesh;
	CHECK(HasRefinableDemand(mesh,view,boatPatch,actual.prepared));CHECK(bowTestSameMesh(mesh,saved));
	CHECK(admissions.Request(MakePatchKey(boatPatch,7,4),actual.prepared.bow,60000));admissions.End();
	unsigned occupied=0;for(const auto &slot:admissions.Slots()) occupied+=slot.occupied?1u:0u;
	CHECK(occupied==1 && bowTestSlot(admissions,actual.prepared.bow.id));
}

TEST(water_bow_refinement_protected_only_or_nonfresh_geometry_cannot_admit)
{
	using namespace WaterProjectedMesh;using namespace WaterBowRefinement;
	auto view=bowTestView();view.resolution={80,80};
	const Point corners[4]={{-100,-100},{100,-100},{100,100},{-100,100}};
	Mesh mesh;CHECK(Build(corners,view.projection,view.resolution,mesh));const auto saved=mesh;
	const auto actual=bowTestActive(view,0,0),collarOnly=bowTestActive(view,-110,0);
    // The expanded contour support enters only the protected world band.
    const double extent=3*collarOnly.prepared.bow.contour.width+WaterFftGeometry::MaxHorizontalDisplacement;
    CHECK(collarOnly.prepared.bow.centre.x+extent>corners[0].x);
    CHECK(collarOnly.prepared.bow.centre.x+extent-corners[0].x<24);
	CHECK(!HasRefinableDemand(mesh,view,corners,collarOnly.prepared));CHECK(bowTestSameMesh(mesh,saved));
	CHECK(HasRefinableDemand(mesh,view,corners,actual.prepared));
	for(auto &vertex:mesh.vertices) vertex.lattice=-1;const auto nonfresh=mesh;
	CHECK(!HasRefinableDemand(mesh,view,corners,actual.prepared));CHECK(bowTestSameMesh(mesh,nonfresh));
	const Point smallPatch[4]={{-20,-20},{20,-20},{20,20},{-20,20}};
	Mesh protectedMesh;CHECK(Build(smallPatch,view.projection,view.resolution,protectedMesh));CHECK(!protectedMesh.indices.empty());
	CHECK(!HasRefinableDemand(protectedMesh,view,smallPatch,actual.prepared));
}

#include "Platform/WaterShoreFoam.h"
#include <limits>

namespace {
double shoreScalar(double x,double y,double time) {
    double h=0;
    for(unsigned i=0;i<WaterShoreFoam::ModeCount;++i) {
        const auto &m=WaterShoreFoam::Modes[i];
        const double phase=x*m.kx+y*m.ky-time*m.omega+m.phase;
        h+=m.amplitude*(std::sin(phase)+.13*std::sin(2*phase));
    }
    return h;
}
}
TEST(water_shore_wave_matches_shared_visible_modes_and_independent_space_time_derivatives)
{
    using namespace WaterShoreFoam;
    unsigned omittedTimeControls=0,harmonicControls=0;
    for(float x:{-128.f,-9.f,0.f,37.f,1000.f})for(float y:{-31.f,0.f,53.f})for(float t:{0.f,.125f,7.f,120.f}) {
        const Wave w=Evaluate(x,y,t);const double eps=.001;
        const double dx=(shoreScalar(x+eps,y,t)-shoreScalar(x-eps,y,t))/(2*eps);
        const double dy=(shoreScalar(x,y+eps,t)-shoreScalar(x,y-eps,t))/(2*eps);
        const double dt=(shoreScalar(x,y,t+eps)-shoreScalar(x,y,t-eps))/(2*eps);
        CHECK(std::abs(w.height-shoreScalar(x,y,t))<.00008);
        CHECK(std::abs(w.dx-dx)<.00003 && std::abs(w.dy-dy)<.00003 && std::abs(w.dt-dt)<.0001);
        if(std::abs(dt)>.1)++omittedTimeControls;
        double noHarmonic=0;for(unsigned i=0;i<ModeCount;++i) {
            const auto &m=Modes[i];noHarmonic-=m.amplitude*m.omega*std::cos(double(x)*m.kx+double(y)*m.ky-double(t)*m.omega+m.phase);
        }
        if(std::abs(noHarmonic-dt)>.03)++harmonicControls;
        const Wave repeated=Evaluate(x,y,t);CHECK(w.height==repeated.height && w.dx==repeated.dx && w.dy==repeated.dy && w.dt==repeated.dt);
    }
    CHECK(omittedTimeControls>20 && harmonicControls>20);
    for(float invalid:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        const Wave w=Evaluate(invalid,0,0);CHECK(w.height==0 && w.dx==0 && w.dy==0 && w.dt==0);
    }
}
TEST(water_shore_source_has_no_permanent_floor_and_respects_direction_depth_exposure_and_material)
{
    using namespace WaterShoreFoam;
    Context c;c.depth=2;c.shore=1;c.nx=1;c.slope=.1f;c.exposure=1;c.support=1;
    Wave incoming;incoming.height=2;incoming.dx=.3f;incoming.dt=1;
    const float exposed=Rate(c,incoming);CHECK(exposed>.5f && exposed<=MaximumRate);
    Wave trough=incoming;trough.height=-1;CHECK(Rate(c,trough)==0);
    Wave flat=incoming;flat.dx=0;CHECK(Rate(c,flat)==0);
    Wave outgoing=incoming;outgoing.dt=-1;CHECK(Rate(c,outgoing)==0);
    Context dry=c;dry.depth=.15f;CHECK(Rate(dry,incoming)==0);
    Context unsupported=c;unsupported.support=0;CHECK(Rate(unsupported,incoming)==0);
    Context sheltered=c;sheltered.exposure=0;CHECK(std::abs(Rate(sheltered,incoming)-exposed*ShelteredScale)<.000001f);
    Context deep=c;deep.depth=20;CHECK(Rate(deep,incoming)==0);
    Context rock=c;rock.slope=1;rock.shore=5;const float rockRate=Rate(rock,incoming);
    rock.shore=7;CHECK(Rate(rock,incoming)==0 && rockRate>0 && rockRate<exposed);
    CHECK(Blend(.2f,0,1,1,1)==0); // valid coast trough removes the old permanent contact floor
    CHECK(Blend(.2f,exposed,0,1,0)==.2f); // object hit or missing coast keeps the original rim
    CHECK(Blend(.2f,exposed,1,0,1)==0); // scene-depth dry fragment cannot receive fresh source
    CHECK(Blend(.2f,exposed,0,1,0)==Blend(.2f,0,0,1,0));
    float previous=0;
    for(unsigned i=0;i<=1000;++i) {
        Wave w=incoming;w.height=-.5f+3*float(i)/1000;
        const float value=Rate(c,w);CHECK(std::isfinite(value) && value>=0 && value<=MaximumRate);
        CHECK(std::abs(value-previous)<.03f);previous=value;
    }
    // The three physical modes predominantly travel +X. This exposed coast faces -X;
    // the opposite-facing beach should not be used to demand sustained incoming surf.
    c.nx=-1;
    unsigned active=0,inactive=0;
    for(unsigned i=0;i<600;++i) {const float value=Rate(c,Evaluate(0,0,float(i)/30));if(value>.02f)++active;else++inactive;}
    CHECK(active>20 && inactive>100); // the static coastline must actually form and recede
    c.depth=std::numeric_limits<float>::quiet_NaN();CHECK(Rate(c,incoming)==0);
    CHECK(Fresh(std::numeric_limits<float>::infinity())==0);
}
namespace {
double shoreFrontIndependentKernel(double q) {
    const double pi=std::acos(-1.0),tau=2*pi;
    q-=tau*std::floor(q/tau);
    // Find the real harmonic crest from h'(q), independently of stored constants.
    static const double alpha=[](){double lo=0,hi=std::acos(-1.0)*.5;
        for(unsigned n=0;n<48;++n){const double mid=(lo+hi)*.5;if(std::cos(mid)+.26*std::cos(2*mid)>0)lo=mid;else hi=mid;}
        return (lo+hi)*.5;}();
    if(q<=alpha||q>=pi)return 0;
    const double u=(std::cos(alpha)-std::cos(q))/(1+std::cos(alpha));return 4*u*(1-u);
}
double shoreFrontIndependentAverage(double q,double width) {
    if(width==0)return shoreFrontIndependentKernel(q);
    // Composite midpoint integration, deliberately not the production antiderivative.
    const unsigned count=8192;double sum=0;
    for(unsigned i=0;i<count;++i)sum+=shoreFrontIndependentKernel(q+width*((i+.5)/count-.5));
    return sum/count;
}
}
TEST(water_shore_front_phases_belong_to_displaced_nearshore_geometry)
{
    using namespace WaterShoreFoam;
    const WaterProjectedMesh::Point corners[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
    WaterFftGeometry::SeamFade seam;CHECK(seam.Prepare(corners,testWaterProjection(),{16,16}));
    for(float x:{-125.f,0.f,193.f})for(float y:{-40.f,31.f})for(float t:{0.f,.125f,17.f}) {
        const auto actual=WaterSurfaceModel::EvaluateBlended(x,y,t,{0,0,0});
        const auto nearshore=WaterSurfaceModel::Evaluate(x,y,t);const auto broad=Evaluate(x,y,t);
        CHECK(actual.height==nearshore.height&&actual.dx==nearshore.dx&&actual.dy==nearshore.dy);
        CHECK(WaterSurfaceModel::EvaluateBlendedHeight(x,y,t,{0,0,0})==nearshore.height);
        CHECK(seam.Weight({x,y},0)==0); // GPU wrapper retains CPU height at zero FFT weight
        double omitted=0;
        for(unsigned i=ModeCount;i<sizeof(Modes)/sizeof(Modes[0]);++i) {
            const auto&m=Modes[i];const double q=double(x)*m.kx+double(y)*m.ky-double(t)*m.omega+m.phase;
            omitted+=m.amplitude*(std::sin(q)+.13*std::sin(2*q));
        }
        for(float depth:{.8f,2.f,9.f}) {
            const double damping=WaterSurfaceModel::ShoreDamping(depth);
            CHECK(std::abs((broad.height+omitted)*damping-double(actual.height)*damping)<.00008);
        }
    }
    CHECK(std::abs(.52*FrontCrestCosine*FrontCrestCosine+FrontCrestCosine-.26)<1e-7);
    CHECK(std::abs(FrontCrestPhase+FrontPhaseWidth-std::acos(-1.0))<2e-7);
}
TEST(water_shore_front_leading_face_propagates_and_preserves_eligibility)
{
    using namespace WaterShoreFoam;const double tau=2*std::acos(-1.0);
    unsigned moving=0,active=0,inactive=0,hiddenBefore=0;double maximumKernelError=0,maximumShiftError=0;
    for(unsigned i=0;i<ModeCount;++i) {
        // Invert phase in double precision. hypot(float,float) rounds before assignment
        // to double and would move the independent reference off its intended phase.
        const auto&m=Modes[i];const double k=std::hypot(double(m.kx),double(m.ky));
        CHECK(FrontPhaseWidth/k>5 && FrontPhaseWidth/m.omega>1.1);
        for(unsigned step=0;step<256;++step) {
            const double q=(step+.25)*tau/256;
            const float value=FrontKernel(float(std::sin(q)),float(std::cos(q)));
            CHECK(std::isfinite(value)&&value>=0&&value<=1);
            const double kernelError=std::abs(value-shoreFrontIndependentKernel(q));
            maximumKernelError=(std::max)(maximumKernelError,kernelError);
            CHECK(kernelError<5e-7); // bounded float polynomial rounding; double oracle stays independent
            if(value>0)CHECK(std::sin(q)+.13*std::sin(2*q)>0 && std::cos(q)+.26*std::cos(2*q)<0);
            // Follow one actual phase for dt=.1: its world-space displacement is k*omega*dt/|k|^2.
            const double x=(q-m.phase)*m.kx/(k*k),y=(q-m.phase)*m.ky/(k*k),dt=.1;
            const double shifted=(x+m.kx*m.omega*dt/(k*k))*m.kx+(y+m.ky*m.omega*dt/(k*k))*m.ky-dt*m.omega+m.phase;
            const double shiftedError=std::abs(value-shoreFrontIndependentKernel(shifted));
            maximumShiftError=(std::max)(maximumShiftError,shiftedError);CHECK(shiftedError<4e-7);
            if(std::abs(value-shoreFrontIndependentKernel(q-m.omega*dt))>.03)++moving;
        }
    }
    Context c;c.depth=2;c.shore=1;c.nx=-1;c.ny=0;c.slope=.1;c.exposure=c.support=1;
    for(unsigned step=0;step<600;++step) {
        const float t=step/30.f;const auto wave=Evaluate(0,0,t);const Front front=EvaluateFront(0,0,t,c.nx,c.ny);
        const float previous=Rate(c,wave),rate=previous*front.mask;
        const Front pause=EvaluateFront(0,0,t,c.nx,c.ny);CHECK(front.mask==pause.mask&&front.residence==pause.residence);
        CHECK(front.mask>=0&&front.mask<=1&&front.residence>=0&&front.residence<=FrontMaximumResidence+.000001f);
        CHECK(rate>=0&&rate<=previous&&rate<=MaximumRate);
        if(wave.height<=0)CHECK(rate==0);
        CHECK(EvaluateFront(0,0,t,1,0).mask==0); // every broad mode travels away from this reversed coast
        const float fresh=FrontFresh(rate,front.residence);
        if(rate>.02)++active;else ++inactive;
        if(fresh>Fresh(MaximumRate)+.02f)++hiddenBefore;
        CHECK(BlendFrontHistory(.2f,rate,front.residence,.5f,1,0,1)==0);
        CHECK(BlendFrontHistory(.2f,rate,front.residence,.5f,1,1,0)==.2f);
        CHECK(BlendFrontHistory(.2f,rate,front.residence,fresh,1,1,1)==fresh); // no duplicated front/history
        for(float depth:{0.f,.15f,20.f}){Context d=c;d.depth=depth;CHECK(Rate(d,wave)*front.mask==0);}
        Context unsupported=c;unsupported.support=0;CHECK(Rate(unsupported,wave)*front.mask==0);
    }
    CHECK(moving>50&&active>20&&inactive>100&&hiddenBefore>5);
    CHECK(EvaluateFront(NAN,0,0,-1,0).mask==0&&EvaluateFront(0,0,0,NAN,0).mask==0);
    CHECK(FrontFiltered(0,1,NAN)==0&&FrontFresh(INFINITY,1)==0&&FrontFresh(1,NAN)==0);
    std::printf("shore front float kernel/independent phase-shift maxima %.9g / %.9g\n",maximumKernelError,maximumShiftError);
}
TEST(water_shore_front_pixel_integral_and_existing_lattice_do_not_lose_arriving_bands)
{
    using namespace WaterShoreFoam;const double pi=std::acos(-1.0),tau=2*pi;
    double maximum=0;unsigned partial=0;
    for(double q:{-.1,0.0,.5,1.336653506,1.8,2.00009,2.6,pi,4.3,6.2})for(float width:{.000015f,.0001f,.001f,.01f,.2f,1.f,3.f,6.283185307f,9.f,20.f}) {
        const double expected=shoreFrontIndependentAverage(q,width);
        const float actual=FrontFiltered(float(std::sin(q)),float(std::cos(q)),width);
        maximum=(std::max)(maximum,std::abs(actual-expected));CHECK(std::isfinite(actual)&&actual>=0&&actual<=1);
        CHECK(std::abs(actual-expected)<.0015);
        if(shoreFrontIndependentKernel(q)==0&&expected>.01)++partial;
    }
    for(unsigned i=0;i<128;++i) {
        const double q=tau*i/128;
        CHECK(std::abs(FrontFiltered(float(std::sin(q)),float(std::cos(q)),float(tau))-FrontArea/FrontTau)<.000002);
    }
    // Sweep every mode through the real 5-world-unit lattice, its unchanged 2x2
    // quadrature, and .1-second deposition ticks with independent spatial/time offsets.
    // A full face cannot be missed, and phase-integrated mass remains bounded.
    double maxSpatial=0,maxTemporal=0;
    for(unsigned i=0;i<ModeCount;++i) {
        const auto&m=Modes[i];const double k=std::hypot(m.kx,m.ky),wavelength=tau/k,period=tau/m.omega;
        for(unsigned offset=0;offset<24;++offset) {
            const double phase=tau*offset/24;double spatial=0,temporal=0;unsigned hits=0;
            for(double u=-wavelength;u<wavelength;u+=5) {
                double sample=0;
                for(double ox:{-1.25,1.25})for(double oy:{-1.25,1.25}) {
                    const double q=k*(u+2.5)+phase+m.kx*ox+m.ky*oy;
                    sample+=.25*shoreFrontIndependentKernel(q);
                }
                // Exactly two periods with fractional terminal cells integrated separately
                // below; the lattice coverage check itself needs no normalization trick.
                hits+=sample>.01;
            }
            CHECK(hits>0);
            // Keep the actual lattice/tick spacing, and independently integrate its
            // finite window including fractional periods rather than aligning endpoints.
            const unsigned cells=unsigned(std::ceil(4*wavelength/5));const double length=cells*5.0;
            for(unsigned n=0;n<cells;++n)for(double ox:{-1.25,1.25})for(double oy:{-1.25,1.25})
                spatial+=1.25*shoreFrontIndependentKernel(k*((n+.5)*5)+phase+m.kx*ox+m.ky*oy);
            double referenceSpatial=0;
            for(double ox:{-1.25,1.25})for(double oy:{-1.25,1.25})
                referenceSpatial+=.25*length*shoreFrontIndependentAverage(k*length*.5+phase+m.kx*ox+m.ky*oy,k*length);
            const unsigned ticks=unsigned(std::ceil(period/.1));const double duration=ticks*.1;
            for(unsigned n=0;n<ticks;++n)temporal+=.1*shoreFrontIndependentKernel(phase-(n+.5)*.1*m.omega);
            const double referenceTemporal=duration*shoreFrontIndependentAverage(phase-duration*.5*m.omega,duration*m.omega);
            maxSpatial=(std::max)(maxSpatial,std::abs(spatial-referenceSpatial)/referenceSpatial);
            maxTemporal=(std::max)(maxTemporal,std::abs(temporal-referenceTemporal)/referenceTemporal);
        }
    }
    CHECK(partial>5&&maxSpatial<.12&&maxTemporal<.007);
    std::printf("shore leading-front independent footprint max %.8g; lattice phase integral %.6g, time crossing %.6g\n",maximum,maxSpatial,maxTemporal);
}

TEST(water_shore_optical_metadata_preserves_rg_and_cannot_create_polygon_or_deep_ocean_coasts)
{
    using namespace WaterOpticalContext;
    const int side=48;std::vector<uint8_t> heights(side*side,0);
    for(int y=0;y<side;++y)for(int x=0;x<16;++x)heights[y*side+x]=20;
    const std::vector<WaterOpticalContext::Polygon> polygons={opticalRectangle(-10,-10,600,600)};Field field;
    CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
    CHECK(field.exposure.size()==heights.size() && field.coast.size()==heights.size());
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        const size_t n=y*side+x;const float distance=x<16?0:(std::min)(100.f,float(x-15)*10);
        const uint8_t expectedR=x<16?0:uint8_t(Offshore(distance)*255+.5f),expectedG=uint8_t(distance/100*255+.5f);
        CHECK(field.ocean[n]==expectedR && field.shore[n]==expectedG);
        CHECK((Texel(field,n)&0x00ffff00u)==((uint32_t(expectedR)<<16)|(uint32_t(expectedG)<<8)));
        CHECK(field.exposure[n]==(x<15?0:255));
        if(x<15 || distance>=20)CHECK(field.coast[n]==0);
        if(field.coast[n])for(int yy=(std::max)(0,y-1);yy<=(std::min)(side-1,y+1);++yy)
            for(int xx=(std::max)(0,x-1);xx<=(std::min)(side-1,x+1);++xx)CHECK(field.ocean[yy*side+xx]==0);
    }
    CHECK(field.coast[24*side+15]==255 && field.coast[24*side+16]==255);
    // The real waterline lies inside the dry-to-wet interpolation interval. Validity must
    // remain one there even when the old wet-only mask would be only .02 or .2.
    for(float f:{.02f,.1f,.2f,.5f,.9f}) {
        const float validity=(field.coast[24*side+15]*(1-f)+field.coast[24*side+16]*f)/255;
        CHECK(std::abs(validity-1)<.000001f);
        CHECK(std::abs(WaterShoreFoam::Blend(.2f,0,validity,1,1))<.000001f);
        CHECK(WaterShoreFoam::Blend(.2f,0,f,1,1)>.019f); // armed wet-only permanent-border control
        CHECK(WaterShoreFoam::Blend(.2f,0,validity,0,1)==0); // no dry coverage
        CHECK(WaterShoreFoam::Blend(.2f,0,validity,1,0)==.2f); // object contact stays intact
    }
    // An authored polygon edge over uniformly submerged terrain is not a shoreline source.
    std::fill(heights.begin(),heights.end(),0);
    CHECK(Build(heights.data(),side,side,0,10,1,4,{opticalRectangle(100,100,300,300)},field));
    for(auto a:field.coast)CHECK(a==0);
    for(auto r:field.ocean)CHECK(r==0);
    // A shallow but entirely open sea (including carrier/menu floors) has no synthetic border.
    CHECK(Build(heights.data(),side,side,0,10,1,4,polygons,field));
    for(size_t n=0;n<heights.size();++n)CHECK(field.ocean[n]==255 && field.exposure[n]==255 && field.coast[n]==0);
    CHECK(!Build(nullptr,side,side,0,10,1,4,polygons,field));
    CHECK(field.ocean.empty() && field.shore.empty() && field.exposure.empty() && field.coast.empty());
}
TEST(water_shore_replacement_requires_real_original_scene_depth_even_in_troughs)
{
    using namespace WaterShoreFoam;
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    const float beforeClear=std::nextafter(.999999f,0.0f),afterAvailable=std::nextafter(.5f,1.0f);
    unsigned unavailableFallbackControls=0,invalidFallbackControls=0,realTroughs=0;
    for(float available:{0.f,.5f,afterAvailable,1.f,nan,inf,-inf})
        for(float raw:{-inf,-.001f,0.f,.2f,beforeClear,.999999f,1.f,1.1f,inf,nan}) {
            // Independent literal acceptance table: a terrain fallback is never a real hit.
            const bool expected=std::isfinite(available)&&available>.5f&&std::isfinite(raw)
                &&raw>=0.f&&raw<.999999f;
            const bool actual=SceneDepthEligible(available,raw);CHECK(actual==expected);
            const float legacy=.2f,zeroRateReplacement=actual?Blend(legacy,0,1,1,1):legacy;
            const float activeReplacement=actual?Blend(legacy,MaximumRate,1,1,1):legacy;
            if(expected) {
                CHECK(zeroRateReplacement==0 && activeReplacement>0 && activeReplacement<1);++realTroughs;
            } else {
                CHECK(zeroRateReplacement==legacy && activeReplacement==legacy);
                // Armed previous gate: a fallback bottom at terrain height would match it
                // exactly and incorrectly replace the legacy rim, even with zero source.
                CHECK(Blend(legacy,0,1,1,1)==0);
                if(!(std::isfinite(available)&&available>.5f))++unavailableFallbackControls;
                else ++invalidFallbackControls;
            }
        }
    CHECK(realTroughs==6 && unavailableFallbackControls==50 && invalidFallbackControls==14);
}

// ARTIFACT: append to test_water_grid.cpp only after authorized implementation.
#include "Platform/WaterShoreHistory.h"
#include <limits>

namespace {
using ShoreState=WaterShoreHistory::State;
struct ShoreTestSource {
    double rate=1,stop=1e30;unsigned calls=0;bool varying=false;
    void operator()(double time,const WaterShoreHistory::Cell*cells,size_t count,double*out) {
        ++calls;
        for(size_t i=0;i<count;++i)out[i]=time>=stop?0:rate*(varying?(1+.3*std::sin(time*1.7+cells[i].x*.2+cells[i].y*.4)):1);
    }
};
std::vector<WaterShoreHistory::Cell> shoreTestCells() {
    std::vector<WaterShoreHistory::Cell> cells;
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)cells.push_back({x,y,4,uint32_t(cells.size())});
    return cells;
}
bool shoreStatesEqual(const std::vector<ShoreState>&a,const std::vector<ShoreState>&b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(a[i].density!=b[i].density||a[i].ageMoment!=b[i].ageMoment)return false;
    return true;
}
std::vector<ShoreState> shoreRun(unsigned fps,bool previews) {
    WaterShoreHistory::History history;const auto cells=shoreTestCells();
    CHECK(history.Install({},cells,1,0)==WaterShoreHistory::BuildResult::Ready);
    ShoreTestSource source;source.varying=true;source.stop=2.35;
    for(unsigned frame=1;frame<=fps*8;++frame) {
        CHECK(history.Advance(double(frame)/fps,source)!=WaterShoreHistory::AdvanceResult::Invalid);
        if(previews)history.Preview(source);
    }
    return history.Preview(source);
}
}
TEST(water_shore_history_exact_density_age_step_matches_independent_integral_and_partition)
{
    using namespace WaterShoreHistory;
    for(double elapsed:{1e-9,1e-5,.005,.049,.1,1.3,17.0})for(double rate:{0.0,.2,1.2,16.0}) {
        const State initial={.7,2.1};const Decay d=Decay::Make(elapsed,5);const State value=d.Apply(initial,rate);
        // Independent composite midpoint integral over deposition ages, accumulated in long double.
        const unsigned count=16384;long double density=0,age=0;
        for(unsigned i=0;i<count;++i) {
            const long double a=(i+.5L)*elapsed/count,weight=std::exp(-a/5.0L)*elapsed/count;
            density+=rate*weight;age+=rate*a*weight;
        }
        const long double q=std::exp(-static_cast<long double>(elapsed)/5);
        density+=q*initial.density;age+=q*(initial.ageMoment+elapsed*initial.density);
        CHECK(std::abs(value.density-double(density))<2e-7);
        CHECK(std::abs(value.ageMoment-double(age))<3e-6);
        const State split=Decay::Make(elapsed*.63,5).Apply(Decay::Make(elapsed*.37,5).Apply(initial,rate),rate);
        CHECK(std::abs(value.density-split.density)<1e-12 && std::abs(value.ageMoment-split.ageMoment)<1e-11);
        CHECK(value.density>=0 && value.ageMoment>=0);
    }
    const State equilibrium=Decay::Make(1e300,5).Apply({1,1},1.2);
    CHECK(equilibrium.density==6 && equilibrium.ageMoment==30);
}
TEST(water_shore_history_topology_is_deterministic_globally_bounded_and_level_isolated)
{
    using namespace WaterShoreHistory;
    auto cells=shoreTestCells();cells.push_back({0,0,9,99});Topology a,b;
    CHECK(a.Build({},cells)==BuildResult::Ready);std::reverse(cells.begin(),cells.end());CHECK(b.Build({},cells)==BuildResult::Ready);
    CHECK(a.SameGeometry(b) && a.Edges().size()==b.Edges().size());
    for(size_t i=0;i<a.Cells().size();++i)CHECK(a.Cells()[i].sourceIndex==b.Cells()[i].sourceIndex);
    for(size_t i=0;i<a.Edges().size();++i) {
        CHECK(a.Edges()[i].a==b.Edges()[i].a && a.Edges()[i].b==b.Edges()[i].b);
        CHECK(a.Cells()[a.Edges()[i].a].level==a.Cells()[a.Edges()[i].b].level);
    }
    CHECK(a.Find(-1,-2,4)>=0 && a.Find(-1,-2,9)<0 && a.Find(0,0,9)>=0);
    auto duplicate=cells;duplicate.push_back(cells.front());CHECK(a.Build({},duplicate)==BuildResult::Invalid);CHECK(a.SameGeometry(b));
    std::vector<Cell> capacity;for(size_t i=0;i<MaxCells;++i)capacity.push_back({int32_t(i),0,4,uint32_t(i)});
    Topology full;CHECK(full.Build({},capacity)==BuildResult::Ready);CHECK(full.Cells().size()==MaxCells && full.Edges().size()==MaxCells-1);
    capacity.push_back({int32_t(MaxCells),0,4,0});CHECK(full.Build({},capacity)==BuildResult::TooManyCells);CHECK(full.Cells().size()==MaxCells);
    std::vector<Cell> levels;for(unsigned i=0;i<=MaxLevels;++i)levels.push_back({0,0,double(i),i});
    CHECK(a.Build({},levels)==BuildResult::TooManyLevels);CHECK(a.SameGeometry(b));
    Grid invalid;invalid.spacing=NAN;CHECK(a.Build(invalid,cells)==BuildResult::Invalid);CHECK(a.SameGeometry(b));
}
TEST(water_shore_history_diffusion_preserves_mass_age_and_does_not_cross_dry_or_level_boundaries)
{
    using namespace WaterShoreHistory;
    // Two cells each side of an absent dry x=0 column, plus a separate higher water level.
    Topology topology;CHECK(topology.Build({},{{-2,0,4,0},{-1,0,4,1},{1,0,4,2},{2,0,4,3},{-1,0,9,4}})==BuildResult::Ready);
    std::vector<State> state(topology.Cells().size()),decayed,result;std::vector<double> rates(state.size(),0);
    const int seed=topology.Find(-1,0,4);state[size_t(seed)]={1,3};Parameters parameters;
    for(unsigned step=0;step<30;++step) {
        double beforeDensity=0,beforeAge=0;for(auto s:state){beforeDensity+=s.density;beforeAge+=s.ageMoment;}
        CHECK(Step(topology,parameters,state,rates,Tick,decayed,result));
        double afterDensity=0,afterAge=0;for(auto s:result){CHECK(s.density>=0 && s.ageMoment>=0);afterDensity+=s.density;afterAge+=s.ageMoment;}
        const double q=std::exp(-Tick/parameters.lifetime);
        CHECK(std::abs(afterDensity-beforeDensity*q)<1e-13);
        CHECK(std::abs(afterAge-(beforeAge+Tick*beforeDensity)*q)<1e-13);
        for(size_t i=0;i<state.size();++i)if(topology.Cells()[i].x>0||topology.Cells()[i].level!=4)
            CHECK(result[i].density==0 && result[i].ageMoment==0);
        state.swap(result);
    }
    const double nu=parameters.diffusivity*Tick/25;
    const double expectedNeighbour=.5*(1-std::pow(1-2*nu,30))*std::exp(-30*Tick/parameters.lifetime);
    CHECK(std::abs(state[size_t(topology.Find(-2,0,4))].density-expectedNeighbour)<1e-13 && expectedNeighbour>.05);
    CHECK(std::abs(state[size_t(seed)].ageMoment/state[size_t(seed)].density-6)<1e-12);
    auto before=state;rates[0]=NAN;rates[1]=-1;rates[2]=1000;
    CHECK(Step(topology,parameters,state,rates,Tick,decayed,result));
    CHECK(result[2].density<=parameters.maximumRate*Tick && std::isfinite(result[0].density));
    const auto unchanged=result;CHECK(!Step(topology,parameters,state,rates,Tick*2,decayed,result));CHECK(shoreStatesEqual(result,unchanged));
}
TEST(water_shore_history_render_schedule_and_partial_preview_do_not_change_deposition)
{
    using namespace WaterShoreHistory;
    const auto baseline=shoreRun(30,false);
    CHECK(shoreStatesEqual(baseline,shoreRun(30,true)));
    CHECK(shoreStatesEqual(baseline,shoreRun(60,true)));
    CHECK(shoreStatesEqual(baseline,shoreRun(120,true)));
    History history,direct;const auto cells=shoreTestCells();ShoreTestSource source,other;source.varying=other.varying=true;
    CHECK(history.Install({},cells,1,0)==BuildResult::Ready && direct.Install({},cells,1,0)==BuildResult::Ready);
    for(double t:{.01,.037,.08,.099999999}) {
        history.Advance(t,source);const auto committed=history.Committed();const auto a=history.Preview(source);const unsigned calls=source.calls;
        CHECK(shoreStatesEqual(a,history.Preview(source)) && source.calls==calls);
        CHECK(shoreStatesEqual(committed,history.Committed()));
    }
    direct.Advance(.099999999,other);CHECK(shoreStatesEqual(history.Preview(source),direct.Preview(other)));
    const auto justBefore=history.Preview(source);history.Advance(.1,source);const auto atBoundary=history.Preview(source);
    for(size_t i=0;i<justBefore.size();++i) {
        CHECK(std::abs(justBefore[i].density-atBoundary[i].density)<2e-8);
        CHECK(std::abs(justBefore[i].ageMoment-atBoundary[i].ageMoment)<2e-8);
    }
    const unsigned frozenCalls=source.calls;CHECK(history.Advance(.1,source)==AdvanceResult::Unchanged);
    CHECK(shoreStatesEqual(atBoundary,history.Preview(source)) && frozenCalls==source.calls);
}
TEST(water_shore_history_resource_refresh_reset_stale_gap_and_time_rewind_have_explicit_lifetime)
{
    using namespace WaterShoreHistory;
    History history;auto cells=shoreTestCells();ShoreTestSource source;CHECK(history.Install({},cells,7,0)==BuildResult::Ready);
    for(unsigned i=1;i<=7;++i)history.Advance(i*.1,source);
    history.Advance(.75,source);const auto visible=history.Preview(source);
    std::reverse(cells.begin(),cells.end());CHECK(history.Install({},cells,7,.75)==BuildResult::Ready);
    CHECK(shoreStatesEqual(visible,history.Preview(source))); // device refresh/reordered source metadata
    CHECK(history.Advance(3.25,source)==AdvanceResult::GapDecayed);const auto stale=history.Preview(source);
    const Decay onlyDecay=Decay::Make(2.5,5);
    for(size_t i=0;i<stale.size();++i) {
        const State expected=onlyDecay.Apply(visible[i],0);
        CHECK(stale[i].density==expected.density && stale[i].ageMoment==expected.ageMoment);
    }
    CHECK(history.Advance(0,source)==AdvanceResult::Rewound);
    for(auto s:history.Preview(source))CHECK(s.density==0 && s.ageMoment==0);
    history.Advance(.2,source);CHECK(history.Preview(source)[0].density>0);
    CHECK(history.Install({},cells,8,.2)==BuildResult::Ready && !history.ValidFor(7) && history.ValidFor(8));
    for(auto s:history.Preview(source))CHECK(s.density==0 && s.ageMoment==0);
    auto excessive=cells;excessive.resize(MaxCells+1);CHECK(history.Install({},excessive,9,.2)==BuildResult::TooManyCells);
    CHECK(!history.ValidFor(9)); // failed new-map construction cannot expose the old map's alpha
}
TEST(water_shore_history_atlas_sampling_preserves_rgb_world_anchor_and_density_weighted_age)
{
    using namespace WaterShoreHistory;
    Topology topology;CHECK(topology.Build({},{{-1,0,4,0},{0,0,4,1}})==BuildResult::Ready);
    const std::vector<State> states={{1,1},{3,30}};
    const State mixed=Sample(topology,states,-2.5,0,4);
    CHECK(mixed.density==2 && mixed.ageMoment==15.5 && mixed.ageMoment/mixed.density==7.75);
    CHECK(std::abs(mixed.ageMoment/mixed.density-(1.0+10.0)/2)>2); // armed unweighted-age control
    CHECK(Sample(topology,states,-2.5,0,9).density==0 && Sample(topology,states,-100,0,4).density==0);
    auto material=[](const State&s,double,double,double){return -std::expm1(-s.density);};
    Mapping map;map.originX=-5.5;map.originY=-.5;map.widthWorld=6;map.heightWorld=1;map.widthPixels=6;map.heightPixels=1;
    std::vector<uint32_t> pixels(6);for(unsigned i=0;i<6;++i)pixels[i]=0xfa000000u|(i*0x234567u&0x00ffffffu);
    const auto original=pixels;CHECK(RasterizeAlpha(topology,states,map,4,1,material,pixels));
    for(unsigned i=0;i<6;++i)CHECK((pixels[i]&0x00ffffffu)==(original[i]&0x00ffffffu));
    auto repeat=pixels;CHECK(RasterizeAlpha(topology,states,map,4,1,material,repeat));CHECK(repeat==pixels);
    Mapping panned=map;panned.originX-=1;panned.widthWorld=7;panned.widthPixels=7;std::vector<uint32_t> shifted(7,0);
    CHECK(RasterizeAlpha(topology,states,panned,4,1,material,shifted));
    for(unsigned i=0;i<6;++i)CHECK((pixels[i]>>24)==(shifted[i+1]>>24));
    CHECK(RasterizeAlpha(topology,states,map,9,1,material,repeat));
    for(unsigned i=0;i<6;++i)CHECK(repeat[i]==(original[i]&0x00ffffffu));
    CHECK(WithAlpha(0x12abcdefu,NAN)==0x00abcdefu && WithAlpha(0x12abcdefu,2)==0xffabcdefu);
}

#include "Platform/WaterShoreSource.h"
namespace {
struct ShoreFixtureDomain {
    int width=33,height=25;std::vector<uint8_t> heights;
    std::vector<WaterOpticalContext::Polygon> polygons;
    WaterOpticalContext::Field field;
    ShoreFixtureDomain() {
        heights.resize(size_t(width)*height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)heights[size_t(y)*width+x]=uint8_t(x>=24?20:(std::max)(0,x-14));
        WaterOpticalContext::Polygon polygon;polygon.points={{0,0,10},{320,0,10},{320,240,10},{0,240,10}};polygons.push_back(polygon);
        CHECK(WaterOpticalContext::Build(heights.data(),width,height,0,10,1,10,polygons,field));
    }
};
}
TEST(water_shore_integrated_domain_uses_wet_geometry_level_and_polygon_segments) {
    using namespace WaterShoreHistory;using namespace WaterShoreSource;
    ShoreFixtureDomain input;Batch source;
    CHECK(source.Build(input.heights.data(),input.width,input.height,0,10,1,input.polygons,{{10,&input.field}})==BuildResult::Ready);
    CHECK(!source.Cells().empty()&&source.Cells().size()<MaxCells&&source.Grid().spacing==5);
    Topology graph;CHECK(graph.Build(source.Grid(),source.Cells())==BuildResult::Ready);
    for(const auto&c:graph.Cells()) {
        const double x=source.Grid().originX+c.x*5,y=source.Grid().originY+c.y*5;
        CHECK(Height(input.heights.data(),input.width,input.height,x/10,y/10,1)<10);
        CHECK(Covered(Intervals(input.polygons,10,y,true),x,x));
    }
    // Both endpoints are wet, but a subcell gap between authored polygons must have no edge.
    std::vector<WaterOpticalContext::Polygon> gap(2);
    gap[0].points={{0,0,10},{2,0,10},{2,10,10},{0,10,10}};
    gap[1].points={{3,0,10},{10,0,10},{10,10,10},{3,10,10}};
    const auto spans=Intervals(gap,10,5,true);
    CHECK(Covered(spans,0,0)&&Covered(spans,5,5)&&!Covered(spans,0,5));
    Topology guarded;CHECK(guarded.Build({},{{0,0,10,0,2},{1,0,10,1,3}})==BuildResult::Ready);
    CHECK(guarded.Edges().empty()); // armed old endpoint-only topology would create one edge
    Topology connected;CHECK(connected.Build({},{{0,0,10,0},{1,0,10,1}})==BuildResult::Ready);
    CHECK(connected.Edges().size()==1&&!guarded.SameGeometry(connected));
    gap[1].points[0].x=2;gap[1].points[3].x=2;
    CHECK(Covered(Intervals(gap,10,5,true),0,5)); // touching union is continuous
    const auto old=source.Cells();auto invalid=input.polygons;invalid[0].points[0].x=NAN;
    CHECK(source.Build(input.heights.data(),input.width,input.height,0,10,1,invalid,{{10,&input.field}})==BuildResult::Invalid);
    CHECK(source.Cells().size()==old.size()); // atomic failed rebuild
}
TEST(water_shore_cached_source_matches_direct_world_wave_and_gradient) {
    using namespace WaterShoreHistory;using namespace WaterShoreSource;
    ShoreFixtureDomain input;Batch source;
    CHECK(source.Build(input.heights.data(),input.width,input.height,0,10,1,input.polygons,{{10,&input.field}})==BuildResult::Ready);
    std::vector<double> rates(source.Cells().size());double maximum=0;unsigned active=0;
    for(double time:{0.0,.05,.4,1.7,6.3,17.0}) {
        source(time,source.Cells().data(),source.Cells().size(),rates.data());
        for(size_t i=0;i<rates.size();++i) {
            const auto&cell=source.Cells()[i];const auto&context=source.Contexts()[cell.sourceIndex].coast;
            const float x=float(source.Grid().originX+cell.x*5),y=float(source.Grid().originY+cell.y*5);
            double expected=0;
            for(unsigned q=0;q<4;++q) {
                const auto&coast=source.Contexts()[cell.sourceIndex].quadrature[q].coast;
                const float qx=x+(q%2?1.25f:-1.25f),qy=y+(q/2?1.25f:-1.25f);
                expected+=.25*WaterShoreFoam::Rate(coast,WaterShoreFoam::Evaluate(qx,qy,float(time)))
                    *WaterShoreFoam::EvaluateFront(qx,qy,float(time),coast.nx,coast.ny).mask;
            }
            maximum=(std::max)(maximum,std::abs(rates[i]-expected));
            CHECK(std::isfinite(rates[i])&&rates[i]>=0&&rates[i]<=WaterShoreFoam::MaximumRate);
            CHECK(std::abs(rates[i]-expected)<.00015);
            active+=rates[i]>.05;
            const double gx=(Height(input.heights.data(),input.width,input.height,(x+5)/10,y/10,1)
                -Height(input.heights.data(),input.width,input.height,(x-5)/10,y/10,1))/10;
            CHECK(std::abs(context.nx+gx)<1e-7);
        }
    }
    CHECK(active>10);std::printf("shore cached source max direct-rate error %.9g (%u active samples)\n",maximum,active);
}
TEST(water_shore_clock_bounds_and_age_material_are_explicit) {
    using namespace WaterShoreHistory;
    History history;ShoreTestSource source;
    CHECK(history.Install({},{{0,0,4,0}},1,1e28)==BuildResult::Invalid);
    CHECK(history.Install({},{{0,0,4,0}},1,0)==BuildResult::Ready);
    CHECK(history.Advance(1e28,source)==AdvanceResult::Invalid);
    CHECK(history.Time()==0);CHECK(history.Advance(.1,source)==AdvanceResult::Advanced);
    CHECK(WaterShoreSource::Coverage({1,15},1,1,0)==0);
    double youngest=WaterShoreSource::Coverage({1,0},13,7,0),oldest=WaterShoreSource::Coverage({1,6},13,7,0);
    CHECK(youngest>0&&oldest>=0&&oldest<=youngest);
    unsigned fragmented=0;for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        const double fresh=WaterShoreSource::Coverage({1,0},x*3,y*3,0),old=WaterShoreSource::Coverage({1,6},x*3,y*3,0);
        CHECK(old>=0&&old<=fresh);fragmented+=old<fresh*.8;
    }
    CHECK(fragmented>10);
    CHECK(WaterShoreSource::Coverage({1,6},13,7,100)==oldest); // material follows world+age, not frame time
    CHECK(WaterShoreFoam::BlendHistory(.2f,0,.5f,1,1,1)==.5f); // remnant survives source trough
    CHECK(WaterShoreFoam::BlendHistory(.2f,0,.5f,1,0,1)==0); // actual dry fragment
    CHECK(WaterShoreFoam::BlendHistory(.2f,0,.5f,1,1,0)==.2f); // object retains contact fallback
    const float fresh=WaterShoreFoam::Fresh(1.2f);
    CHECK(WaterShoreFoam::BlendHistory(.2f,1.2f,fresh,1,1,1)==fresh); // no fresh/history double-count
}
TEST(water_shore_cached_source_large_world_phase_bound) {
    using namespace WaterShoreSource;using namespace WaterShoreHistory;
    ShoreFixtureDomain input;input.width=1025;input.heights.resize(size_t(input.width)*input.height);
    for(int y=0;y<input.height;++y)for(int x=0;x<input.width;++x)
        input.heights[size_t(y)*input.width+x]=uint8_t(x>=1010?20:(std::max)(0,x-1000));
    input.polygons[0].points[1].x=input.polygons[0].points[2].x=10240;
    CHECK(WaterOpticalContext::Build(input.heights.data(),input.width,input.height,0,10,1,10,input.polygons,input.field));
    Batch source;CHECK(source.Build(input.heights.data(),input.width,input.height,0,10,1,input.polygons,{{10,&input.field}})==BuildResult::Ready);
    std::vector<double> rates(source.Cells().size());double maximum=0;unsigned active=0;
    for(double time:{0.0,.05,7.75,60.0,600.0,3600.0}) {
        source(time,source.Cells().data(),source.Cells().size(),rates.data());
        for(size_t i=0;i<rates.size();++i) {
            const auto&cell=source.Cells()[i];
            const float x=float(source.Grid().originX+cell.x*5),y=float(source.Grid().originY+cell.y*5);
            double expected=0;
            for(unsigned q=0;q<4;++q) {
                const auto&coast=source.Contexts()[cell.sourceIndex].quadrature[q].coast;
                const float qx=x+(q%2?1.25f:-1.25f),qy=y+(q/2?1.25f:-1.25f);
                expected+=.25*WaterShoreFoam::Rate(coast,WaterShoreFoam::Evaluate(qx,qy,float(time)))
                    *WaterShoreFoam::EvaluateFront(qx,qy,float(time),coast.nx,coast.ny).mask;
            }
            maximum=(std::max)(maximum,std::abs(rates[i]-expected));active+=expected>.05;
            CHECK(std::abs(rates[i]-expected)<.003); // float phase re-association at 10,000 world units / one hour
        }
    }
    CHECK(active>0&&maximum>1e-5);std::printf("shore cached source 10,000-unit / one-hour maximum rate error %.9g\n",maximum);
}

TEST(water_shore_source_spatial_resolution_sensitivity) {
    using namespace WaterShoreSource;ShoreFixtureDomain input;
    auto integral=[&](double step) {
        double sum=0;
        for(unsigned frame=0;frame<40;++frame) {
            const float time=float((frame+.5)*.2);
            for(double y=0;y<240;y+=step)for(double x=180;x<240;x+=step) {
                const float terrain=Height(input.heights.data(),input.width,input.height,x/10,y/10,1);
                if(terrain>=10)continue;
                const int ix=int(x/10),iy=int(y/10);const float fx=float(x/10-ix),fy=float(y/10-iy);
                WaterShoreFoam::Context c;c.depth=10-terrain;
                c.nx=-(Height(input.heights.data(),input.width,input.height,(x+5)/10,y/10,1)
                    -Height(input.heights.data(),input.width,input.height,(x-5)/10,y/10,1))*.1f;
                c.ny=-(Height(input.heights.data(),input.width,input.height,x/10,(y+5)/10,1)
                    -Height(input.heights.data(),input.width,input.height,x/10,(y-5)/10,1))*.1f;
                c.slope=std::hypot(c.nx,c.ny);c.support=FieldSample(input.field.coast,input.width,ix,iy,fx,fy);
                c.exposure=FieldSample(input.field.exposure,input.width,ix,iy,fx,fy);
                c.shore=(std::min)(FieldSample(input.field.shore,input.width,ix,iy,fx,fy)*100,c.depth/(std::max)(c.slope,.01f));
                sum+=WaterShoreFoam::Rate(c,WaterShoreFoam::Evaluate(float(x),float(y),time))
                    *WaterShoreFoam::EvaluateFront(float(x),float(y),time,c.nx,c.ny).mask*step*step*.2;
            }
        }
        return sum;
    };
    const double fine=integral(1.25),point5=integral(5),coarse=integral(10);
    Batch source;CHECK(source.Build(input.heights.data(),input.width,input.height,0,10,1,input.polygons,{{10,&input.field}})==WaterShoreHistory::BuildResult::Ready);
    std::vector<double> rates(source.Cells().size());double proposed=0;
    for(unsigned frame=0;frame<40;++frame) {
        source((frame+.5)*.2,source.Cells().data(),source.Cells().size(),rates.data());
        for(double rate:rates)proposed+=rate*25*.2;
    }
    const double proposedError=std::abs(proposed-fine)/fine,pointError=std::abs(point5-fine)/fine,coarseError=std::abs(coarse-fine)/fine;
    CHECK(fine>0&&std::isfinite(proposedError)&&std::isfinite(coarseError));
    // The phase-locked mirrored fixture measured 12.77%/34.10%/80.89% errors.
    // Keep the actual quadrature budget and require both absolute and relative alias
    // discrimination; an old >100% error requirement is not a correctness property.
    CHECK(proposedError<.15 && pointError>.3 && coarseError>.5 && coarseError>4*proposedError);
    std::printf("shore source spatial integral 1.25=%.8g, quadrature5=%.8g (error %.5f), point5 error %.5f, point10 error %.5f\n",fine,proposed,proposedError,pointError,coarseError);
}

#include "Platform/WaterShoreSource.h"
namespace {
using ShorePolygon=WaterOpticalContext::Polygon;
ShorePolygon shoreRect(float left,float bottom,float right,float top,float level=10) {
    ShorePolygon p;p.points={{left,bottom,level},{right,bottom,level},{right,top,level},{left,top,level}};return p;
}
std::vector<WaterShoreHistory::Cell> shoreFourCorners(double level=10) {
    return {{0,0,level,0},{1,0,level,1},{0,1,level,2},{1,1,level,3}};
}
bool shoreIndependentContains(const ShorePolygon&p,double x,double y) {
    bool inside=false;
    for(size_t i=0,j=p.points.size()-1;i<p.points.size();j=i++) {
        const auto&a=p.points[j];const auto&b=p.points[i];
        const double dx=b.x-a.x,dy=b.y-a.y,px=x-a.x,py=y-a.y;
        if(std::abs(dx*py-dy*px)<1e-10&&px*dx+py*dy>=-1e-10&&px*dx+py*dy<=dx*dx+dy*dy+1e-10)return true;
        if((a.y>y)!=(b.y>y))inside=inside^(x<a.x+(y-a.y)*dx/dy);
    }
    return inside;
}
}
TEST(water_shore_atlas_gap_support_does_not_renormalize_or_guess_nearest_membership) {
    using namespace WaterShoreHistory;
    const auto cells=shoreFourCorners();Topology graph;CHECK(graph.Build({},cells)==BuildResult::Ready);
    WaterShoreSupport::Domain support;auto flat=[](double,double){return 0.0f;};
    CHECK(support.Build({},cells,3,3,{shoreRect(0,0,2,5),shoreRect(3,0,5,5)},flat));
    const std::vector<State> left={{1,2},{0,0},{1,2},{0,0}};
    CHECK(std::abs(Sample(graph,left,4,2.5,10).density-.2)<1e-14); // armed old interpolation
    CHECK(Sample(graph,left,4,2.5,10,support).density==0);
    CHECK(Sample(graph,left,2.5,2.5,10,support).density==0);
    const State own=Sample(graph,left,1,2.5,10,support);
    CHECK(std::abs(own.density-.8)<1e-14&&std::abs(own.ageMoment-1.6)<1e-14); // no 0.8 -> 1 normalization
    const std::vector<State> right={{0,0},{1,3},{0,0},{1,3}};
    CHECK(Sample(graph,right,1,2.5,10,support).density==0);
    // The target polygon need not contain a history node. Nearest-node labeling would
    // attribute this isolated tiny pool to the emitting polygon on its left.
    std::vector<Cell> sparse={{0,0,10,0},{0,1,10,1}};Topology sparseGraph;
    CHECK(sparseGraph.Build({},sparse)==BuildResult::Ready);
    CHECK(support.Build({},sparse,3,3,{shoreRect(0,0,1,5),shoreRect(2,2,3,3)},flat));
    std::vector<State> sparseStates={{1,2},{1,2}};
    CHECK(Sample(sparseGraph,sparseStates,2.5,2.5,10).density==.5);
    CHECK(Sample(sparseGraph,sparseStates,2.5,2.5,10,support).density==0);
    CHECK(std::abs(Sample(sparseGraph,sparseStates,.5,2.5,10,support).density-.9)<1e-14);
}
TEST(water_shore_atlas_touching_and_overlapping_unions_keep_original_weights) {
    using namespace WaterShoreHistory;
    const auto cells=shoreFourCorners();Topology graph;CHECK(graph.Build({},cells)==BuildResult::Ready);
    WaterShoreSupport::Domain support;auto flat=[](double,double){return 0.0f;};
    const std::vector<State> states={{1,2},{2,6},{3,12},{4,20}};
    for(const auto&polygons:std::vector<std::vector<ShorePolygon>>{
            {shoreRect(-5,-5,10,10)},
            {shoreRect(0,0,2,5),shoreRect(2,0,5,5)},
            {shoreRect(0,0,3,5),shoreRect(2,0,5,5)}}) {
        CHECK(support.Build({},cells,3,3,polygons,flat));
        for(int iy=0;iy<=20;++iy)for(int ix=0;ix<=20;++ix) {
            const double x=ix*.249,y=iy*.249;
            const State expected=Sample(graph,states,x,y,10),actual=Sample(graph,states,x,y,10,support);
            CHECK(std::abs(expected.density-actual.density)<1e-12&&std::abs(expected.ageMoment-actual.ageMoment)<1e-12);
        }
    }
    CHECK(support.Build({},cells,3,3,{shoreRect(-100,-100,100,100)},flat));
    CHECK(support.VertexCount()==0); // interior certificate has no per-pixel polygon traversal
}
TEST(water_shore_atlas_concave_support_matches_independent_segment_membership) {
    using namespace WaterShoreHistory;
    const auto cells=shoreFourCorners();WaterShoreSupport::Domain support;auto flat=[](double,double){return 0.0f;};
    ShorePolygon u;u.points={{0,0,10},{5,0,10},{5,5,10},{4,5,10},{4,1,10},{1,1,10},{1,5,10},{0,5,10}};
    CHECK(support.Build({},cells,3,3,{u},flat));
    unsigned blocked=0,allowed=0;
    for(unsigned iy=0;iy<29;++iy)for(unsigned ix=0;ix<29;++ix) {
        const double fx=.013+ix*.034,fy=.017+iy*.034;const auto stencil=support.At(0,0,10,fx,fy);
        for(unsigned corner=0;corner<4;++corner) {
            const double ax=(corner&1)*5,ay=(corner>>1)*5;bool expected=true;
            for(unsigned step=0;step<=256&&expected;++step) {
                const double t=double(step)/256;
                expected=shoreIndependentContains(u,ax+(fx*5-ax)*t,ay+(fy*5-ay)*t);
            }
            const bool actual=stencil.Allows(corner&1,corner>>1);
            CHECK(expected==actual);blocked+=!actual;allowed+=actual;
        }
    }
    CHECK(blocked>1000&&allowed>100);
}
TEST(water_shore_atlas_terrain_saddle_blocks_cross_ridge_without_losing_local_foam) {
    using namespace WaterShoreHistory;
    const std::vector<Cell> cells={{0,0,1,0},{1,1,1,1}};Topology graph;CHECK(graph.Build({},cells)==BuildResult::Ready);
    WaterShoreSupport::Domain support;
    // Coordinates received by the height callback are in terrain cells; the support
    // square is half a terrain cell. Its heights are exactly {0,4,4,0}.
    auto saddle=[](double x,double y){x*=2;y*=2;return float(4*(x+y-2*x*y));};
    CHECK(support.Build({},cells,3,3,{shoreRect(0,0,5,5,1)},saddle));
    const std::vector<State> left={{1,2},{0,0}},right={{0,0},{1,3}};
    CHECK(Sample(graph,left,4.5,4.5,1).density>.0099);
    CHECK(Sample(graph,left,4.5,4.5,1,support).density==0);
    CHECK(std::abs(Sample(graph,right,4.5,4.5,1,support).density-.81)<1e-12);
    CHECK(Sample(graph,left,2.5,2.5,1,support).density==0); // dry target
}
TEST(water_shore_supported_raster_preserves_rgb_and_produces_no_gap_alpha) {
    using namespace WaterShoreHistory;
    const auto cells=shoreFourCorners();Topology graph;CHECK(graph.Build({},cells)==BuildResult::Ready);
    WaterShoreSupport::Domain support;auto flat=[](double,double){return 0.0f;};
    CHECK(support.Build({},cells,3,3,{shoreRect(0,0,2,5),shoreRect(3,0,5,5)},flat));
    const std::vector<State> states={{1,2},{0,0},{1,2},{0,0}};
    auto material=[](const State&s,double,double,double){return -std::expm1(-s.density);};
    Mapping map={0,0,5,5,20,20};std::vector<uint32_t> pixels(400,0xfaabcdefu),legacy=pixels;
    CHECK(RasterizeAlpha(graph,states,map,10,1,material,pixels,support));
    CHECK(RasterizeAlpha(graph,states,map,10,1,material,legacy));
    unsigned changed=0,retained=0;
    for(unsigned y=0;y<20;++y)for(unsigned x=0;x<20;++x) {
        const unsigned i=y*20+x;const double worldX=(x+.5)*.25;
        CHECK((pixels[i]&0xffffffu)==0xabcdefu);
        if(worldX>2){CHECK((pixels[i]>>24)==0);changed+=(legacy[i]>>24)>0;}
        else {CHECK(pixels[i]==legacy[i]);retained+=(pixels[i]>>24)>0;}
    }
    CHECK(changed>100&&retained>100);
    auto repeat=pixels;CHECK(RasterizeAlpha(graph,states,map,10,1,material,repeat,support));CHECK(repeat==pixels);
}
TEST(water_shore_support_complexity_failure_is_atomic_and_bounded) {
    using namespace WaterShoreHistory;
    const auto cells=shoreFourCorners();WaterShoreSupport::Domain support;auto flat=[](double,double){return 0.0f;};
    CHECK(support.Build({},cells,3,3,{shoreRect(-1,-1,6,6)},flat));
    const size_t before=support.TileCount();
    // A high-vertex closed polygon wholly within the square needs more local
    // boundary data than the declared cap. Reject the candidate; never truncate it.
    ShorePolygon circle;for(unsigned i=0;i<256;++i) {
        const double theta=i*6.283185307179586/256;
        circle.points.push_back({float(2.5+2*std::cos(theta)),float(2.5+2*std::sin(theta)),10});
    }
    CHECK(!support.Build({},cells,3,3,{circle},flat));
    CHECK(support.TileCount()==before&&support.At(0,0,10,.3,.7).Allows(0,0));
    CHECK(support.Work()<=WaterShoreSupport::MaxWork&&support.VertexCount()<=WaterShoreSupport::MaxVertices);
}

#include <random>
#include <set>
#include <tuple>
#include <cstdio>
namespace ShoreSparseRasterAudit {
using namespace WaterShoreHistory;
static unsigned cases=0;static uint64_t pixels=0;
// Frozen pre-optimization raster loop: independent full-image traversal and original
// Sample hash lookups. Production sparse tiles are never inspected by this oracle.
template<class Coverage,class Support> bool Reference(const Topology&t,const std::vector<State>&s,const Mapping&m,double l,double time,Coverage&c,std::vector<uint32_t>&p,const Support&support){
 if(!std::isfinite(m.originX)||!std::isfinite(m.originY)||!std::isfinite(m.widthWorld)||!std::isfinite(m.heightWorld)||m.widthWorld<=0||m.heightWorld<=0
 ||!std::isfinite(m.originX+m.widthWorld)||!std::isfinite(m.originY+m.heightWorld)||!m.widthPixels||!m.heightPixels||m.widthPixels>512||m.heightPixels>512
 ||!std::isfinite(l)||!std::isfinite(time)||s.size()!=t.Cells().size()||p.size()!=size_t(m.widthPixels)*m.heightPixels)return false;
 for(unsigned y=0;y<m.heightPixels;++y)for(unsigned x=0;x<m.widthPixels;++x){double wx=m.originX+(double(x)+.5)*m.widthWorld/m.widthPixels,wy=m.originY+(double(y)+.5)*m.heightWorld/m.heightPixels;
  State state=Sample(t,s,wx,wy,l,support);double v=state.density>0?c(state,wx,wy,time):0;size_t n=size_t(y)*m.widthPixels+x;p[n]=WithAlpha(p[n],v);}
 return true;
}
struct Coverage {unsigned calls=0;double operator()(const State&s,double x,double y,double time){++calls;
 if(!std::isfinite(s.density)||!std::isfinite(s.ageMoment))return 0;
 return -std::expm1(-s.density)*(.55+.45*std::sin(x*.31+y*.27+time+s.ageMoment/(s.density+.0001)));}};
template<class Support> void Compare(const Topology&t,const std::vector<State>&s,const Mapping&m,double l,const Support&support,double time=1.375){
 std::vector<uint32_t> seed(size_t(m.widthPixels)*m.heightPixels);for(size_t i=0;i<seed.size();++i)seed[i]=0xaa000000u|((uint32_t(i)*2654435761u)&0xffffffu);
 auto a=seed,b=seed;Coverage ca,cb;bool ra=Reference(t,s,m,l,time,ca,a,support),rb=RasterizeAlpha(t,s,m,l,time,cb,b,support);
 CHECK(ra==rb);CHECK(ca.calls==cb.calls);CHECK(a==b);
 for(size_t i=0;i<seed.size();++i)CHECK((b[i]&0xffffffu)==(seed[i]&0xffffffu));++cases;pixels+=seed.size();
}
static void Layout(const Topology&t){const auto&tiles=t.RasterTiles();CHECK(tiles.size()<=4*t.Cells().size());
 for(size_t i=0;i<tiles.size();++i){if(i)CHECK(RasterLess(tiles[i-1],tiles[i]));for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
 CHECK(tiles[i].nodes[dy*2+dx]==t.Find(tiles[i].x+dx,tiles[i].y+dy,tiles[i].level));}}
static void General(){std::mt19937 rng(0x5afe51);for(unsigned run=0;run<180;++run){std::set<std::tuple<double,int,int>> keys;std::vector<Cell> cells;
 for(unsigned i=0;i<320;++i){int x=int(rng()%31)-15,y=int(rng()%31)-15;double level=int(rng()%3)*7-7;if(keys.insert({level,y,x}).second)cells.push_back({x,y,level,0,uint8_t(rng()%4)});}
 Grid grid{double(int(rng()%13)-6)/3,double(int(rng()%11)-5)/7,1+double(rng()%10)};Topology t;CHECK(t.Build(grid,cells)==BuildResult::Ready);Layout(t);
 std::vector<State>s(t.Cells().size());for(auto&v:s){v.density=rng()%4?0:double(rng()%500)/100;v.ageMoment=v.density*double(rng()%200)/10;}
 for(unsigned k=0;k<4;++k){Mapping m{double(int(rng()%251)-125)/3,double(int(rng()%251)-125)/7,.01+double(rng()%401)/3,.02+double(rng()%401)/4,unsigned(1+rng()%65),unsigned(1+rng()%65)};
 Compare(t,s,m,double(int(rng()%4)*7-7),UnrestrictedSupport{});}
 // Exact grid boundaries, repeated texels in the same cell, integer/negative origins.
 Compare(t,s,{grid.originX-grid.spacing*2.5,grid.originY-grid.spacing*2.5,grid.spacing*20,grid.spacing*20,20,20},0,UnrestrictedSupport{});
 }
 for(int edge:{(std::numeric_limits<int32_t>::min)(),(std::numeric_limits<int32_t>::max)()-1}){
 std::vector<Cell>cells{{edge,edge,7,0,3},{edge+1,edge,7,0,3},{edge,edge+1,7,0,3},{edge+1,edge+1,7,0,3}};Topology t;CHECK(t.Build({},cells)==BuildResult::Ready);Layout(t);std::vector<State>s(4,{1,.5});
 Compare(t,s,{double(edge)*5-10,double(edge)*5-10,30,30,31,29},7,UnrestrictedSupport{});}
 Topology t;CHECK(t.Build({1e16,1e16,5},{{0,0,7,0,3},{1,0,7,0,3},{0,1,7,0,3},{1,1,7,0,3}})==BuildResult::Ready);std::vector<State>s(4,{1,.5});
 Compare(t,s,{1e16,1e16,1,1,64,64},7,UnrestrictedSupport{});Compare(t,s,{-1e308,-1e308,1e308,1e308,64,64},7,UnrestrictedSupport{});
 for(auto&v:s)v={0,NAN};Compare(t,s,{1e16,1e16,1,1,16,16},7,UnrestrictedSupport{});
 s[0]={1,2};s[1]={NAN,0};s[2]={-1,0};s[3]={INFINITY,1};Compare(t,s,{1e16,1e16,10,10,16,16},7,UnrestrictedSupport{});
 Compare(t,s,{0,0,0,5,2,2},7,UnrestrictedSupport{});Compare(t,s,{1e308,0,1e308,5,2,2},7,UnrestrictedSupport{});
 Compare(t,s,{0,0,10,10,4,4},NAN,UnrestrictedSupport{});Compare(t,s,{0,0,10,10,4,4},7,UnrestrictedSupport{},NAN);
 Topology empty;CHECK(empty.Build({}, {})==BuildResult::Ready);Compare(empty,{}, {-5,-5,100,100,512,512},7,UnrestrictedSupport{});
}
static void Gaps(){Grid g{-25,-35,5};std::vector<WaterOpticalContext::Polygon> polygons;
 // Fractional 0.4-unit dry polygon gap within a single5-unit history tile, plus a
 // concave boundary and a second level. Support is the real production segment test.
 polygons.push_back({{{-25,-35,7},{1.1f,-35,7},{1.1f,80,7},{-25,80,7}}});
 polygons.push_back({{{1.5f,-35,7},{100,-35,7},{100,80,7},{1.5f,80,7},{1.5f,35,7},{20,35,7},{20,25,7},{1.5f,25,7}}});
 auto high=polygons[0];for(auto&p:high.points)p.z=14;polygons.push_back(high);
 auto terrain=[](double x,double y){return 2+5*std::sin(x*.8)*std::sin(y*.9);};
 std::vector<Cell>cells;for(double l:{7.,14.})for(int y=0;y<24;++y)for(int x=0;x<24;++x)if(terrain(x*.5,y*.5)<l)cells.push_back({x,y,l,0,3});
 Topology t;CHECK(t.Build(g,cells)==BuildResult::Ready);WaterShoreSupport::Domain support;CHECK(support.Build(g,cells,32,32,polygons,terrain));
 std::vector<State>s(t.Cells().size());for(size_t i=0;i<s.size();++i)s[i]={i%5?double(i%17)/13:0,double(i%7)/9};
 for(double level:{7.,14.,3.})for(double phase:{0.,.01,.25,.5,.999999999}){Compare(t,s,{-26+phase,-36+phase,130,130,257,191},level,support);
 Compare(t,s,{-1+phase,-1-phase,5,5,37,41},level,support);}Layout(t);
}
static void Bounds(){
 std::vector<Cell> cells;cells.reserve(MaxCells);for(unsigned i=0;i<MaxCells;++i)cells.push_back({int(i*3),0,7,0,3});
 Topology t;CHECK(t.Build({},cells)==BuildResult::Ready);CHECK(t.RasterTiles().size()==4*MaxCells);Layout(t);
 std::vector<State>s(cells.size(),{.5,.75});Compare(t,s,{-10,-10,1000,20,512,17},7,UnrestrictedSupport{});
 const size_t retained=t.RasterTiles().size();cells.push_back({-1,0,7,0,3});CHECK(t.Build({},cells)==BuildResult::TooManyCells);CHECK(t.RasterTiles().size()==retained);
 std::printf("BOUNDS nodes=%zu tiles=%zu tile_bytes=%zu maximum_reserved_bytes=%zu\n",t.Cells().size(),t.RasterTiles().size(),sizeof(RasterTile),4*MaxCells*sizeof(RasterTile));
 for(double direction:{-INFINITY,INFINITY})for(double boundary:{-5.,0.,5.}){
  double at=std::nextafter(boundary,direction);Compare(t,s,{at-.5,-.5,1,1,1,1},7,UnrestrictedSupport{});}
}
}

TEST(water_shore_sparse_raster_matches_full_scan_reference_across_mappings_and_invalid_states)
{ ShoreSparseRasterAudit::General(); }
TEST(water_shore_sparse_raster_preserves_exact_gap_saddle_and_concave_support)
{ ShoreSparseRasterAudit::Gaps(); }
TEST(water_shore_sparse_raster_bounds_cache_and_owns_every_pixel_once)
{ ShoreSparseRasterAudit::Bounds(); }

// Pure scalar oracle exercises the same bounded positive contribution emitted to both
// stages. The independent central difference includes gradients of F, D and bow pressure.
#include "Platform/WaterBowLift.h"
TEST(water_bow_lift_bounds_zero_and_product_derivative)
{
    using namespace WaterBowLift;const double V=WaterFftGeometry::MaxVerticalDisplacement;
    unsigned restored=0;
    for(double free:{0.,.05,.31,.559713426,1.})for(double ratio:{0.,.2,.8,1.})
        for(double incident:{-100.,-10.,-1.,0.,1.,10.,100.})for(double bow:{0.,.000001,.2,3.,8.,64.}) {
            const double damping=free*ratio,old=damping*V*std::tanh((incident+bow)/V);
            const Field added=Additional({bow,.31,-.22},{free,.003,.002},{damping,-.002,.001});
            const double total=old+added.value;
            CHECK(std::isfinite(total) && std::abs(total)<=free*V+1e-12 && total>=old);
            if(bow==0 || free==damping)CHECK(added.value==0 && added.dx==0 && added.dy==0);
            else {
                ++restored;
                const double epsilon=1e-5;
                auto independent=[&](double x,double y) {
                    return (free+.003*x+.002*y-damping+.002*x-.001*y)*V*
                        std::tanh((bow+.31*x-.22*y)/V);
                };
                // Stay within the branch's positive B domain; separate exact boundary
                // cases above test zero emission without finite-differencing a clamp kink.
                if(bow>.00001) {
                    CHECK(std::abs(added.dx-(independent(epsilon,0)-independent(-epsilon,0))/(2*epsilon))<1e-8);
                    CHECK(std::abs(added.dy-(independent(0,epsilon)-independent(0,-epsilon))/(2*epsilon))<1e-8);
                }
                CHECK(Foam(added.value,0)==0 && Foam(0,1)==0);
                CHECK(Foam(added.value,1)>=0 && Foam(added.value,1)<=.65);
            }
        }
    CHECK(restored>100);
}

// Scalar contract for the diagnostic's counterfactual. Removing only bow keeps
// incident/reflected/stern height and phase; compare complete final heights directly.
TEST(water_bow_retention_same_phase_marginal_is_not_an_isolated_peak)
{
    const double v=WaterFftGeometry::MaxVerticalDisplacement;
    auto limit=[&](double z){return v*std::tanh(z/v);};
    unsigned positive=0,isolatedWrong=0,overEnvelope=0;
    for(double a:{-32.,-10.,-1.,0.,1.,10.,32.})for(double b:{0.,.000001,.2,3.,8.,64.})
        for(double f:{0.,.559713426,1.})for(double ratio:{0.,.5,1.})for(double w:{0.,.4,1.}) {
            const double d=f*ratio,oldAnalytic=-2.3;
            const auto lift=WaterBowLift::Additional({b,0,0},{f,0,0},{d,0,0});
            const double finalWith=(1-w)*oldAnalytic+w*(d*limit(a+b)+lift.value);
            const double finalWithout=(1-w)*oldAnalytic+w*d*limit(a);
            const double expected=w*(d*(limit(a+b)-limit(a))+(f-d)*limit(b));
            CHECK_NEAR(finalWith-finalWithout,expected,1e-12);
            CHECK(expected>=-1e-12 && expected<=w*(f+d)*v+1e-12);
            if(b==0 || w==0 || f==0)CHECK(expected==0);
            if(d==0)CHECK_NEAR(expected,w*f*limit(b),1e-12);
            if(d==f)CHECK(lift.value==0);
            positive+=expected>.01;isolatedWrong+=std::abs(expected-w*f*limit(b))>.01;
            overEnvelope+=expected>w*f*v+.01;
        }
    CHECK(positive>100 && isolatedWrong>100 && overEnvelope>0);
    const double b=8;
    CHECK(limit(-10+b)-limit(-10)>limit(10+b)-limit(10));
    CHECK(limit(-32+64)-limit(-32)>1.9*v);
}

namespace SternAnchorNative {
// Original native campaign carrier trace, object1735. These unmodified contact polygons
// expose the tilted-transom extremal-corner switch, especially frames4336/4337.
struct Pose { unsigned frame;float origin[2],axis[2];double sectionY;WaterHullModel::Point vertices[12]; };
static const Pose Poses[]={
    {4220, {2959.32f,1120.78f}, {-0.99979f,-0.0204964f}, -1.0578909956073836, {{-284.549f,-0.231101f},{-267.471f,-7.19379f},{-7.28663f,-76.2132f},{199.121f,-70.1169f},{270.821f,-65.6736f},{272.496f,-50.5279f},{273.377f,23.8783f},{270.693f,24.3134f},{45.5953f,32.0521f},{-115.97f,25.2146f},{-267.925f,6.48248f},{-275.605f,3.95113f}}},
    {4250, {2999.31f,1121.6f}, {-0.99979f,-0.0204936f}, -0.32441283366568285, {{-226.052f,-16.2233f},{-6.73874f,-76.747f},{160.862f,-73.3101f},{259.01f,-67.9497f},{261.689f,-67.4745f},{265.114f,-44.1614f},{266.778f,20.4989f},{266.337f,24.7154f},{263.832f,25.0799f},{46.1111f,32.0093f},{-114.734f,24.4844f},{-195.63f,14.8339f}}},
    {4280, {3039.3f,1122.42f}, {-0.99979f,-0.0204889f}, -0.10351113058784023, {{-213.799f,12.7919f},{-196.922f,-22.2646f},{-7.64609f,-74.9638f},{183.475f,-70.5559f},{263.074f,-65.9937f},{265.728f,-65.7127f},{267.34f,-42.9061f},{265.513f,21.6078f},{264.768f,26.2135f},{262.238f,26.7739f},{46.5009f,33.2244f},{-115.12f,25.0265f}}},
    {4310, {3079.29f,1123.24f}, {-0.99979f,-0.0204799f}, -1.0705070375351973, {{-284.638f,-0.0455329f},{-268.456f,-6.92232f},{-8.20531f,-74.473f},{224.652f,-67.5397f},{274.875f,-64.5264f},{275.34f,-54.0291f},{271.542f,24.9342f},{268.829f,25.5826f},{46.028f,33.2945f},{-116.389f,26.0933f},{-267.6f,6.71308f},{-275.661f,4.113f}}},
    {4336, {3113.95f,1123.96f}, {-0.999791f,-0.020461f}, -0.4750876418450609, {{-271.514f,-0.275939f},{-247.809f,-8.57374f},{-42.514f,-66.4348f},{-7.76008f,-75.8955f},{256.202f,-65.8565f},{278.09f,-64.5216f},{281.147f,-64.1222f},{281.706f,22.5218f},{279.095f,22.9693f},{45.0761f,31.9652f},{-117.476f,26.114f},{-248.194f,7.7703f}}},
    {4337, {3115.28f,1123.98f}, {-0.999791f,-0.0204598f}, -0.4560529930414532, {{-270.872f,-0.310776f},{-247.179f,-8.64923f},{-43.0014f,-66.3589f},{-7.7475f,-75.9495f},{257.873f,-65.7452f},{281.417f,-64.0851f},{281.679f,-43.3014f},{281.696f,22.5213f},{279.549f,22.8832f},{45.0306f,31.9014f},{-117.493f,26.0859f},{-247.663f,7.77216f}}},
    {4340, {3119.28f,1124.06f}, {-0.999791f,-0.0204556f}, -0.3622370990942123, {{-268.474f,-0.423517f},{-244.801f,-8.91572f},{-45.2046f,-65.9759f},{-7.67355f,-76.1656f},{261.119f,-65.5286f},{278.867f,-64.4342f},{281.712f,-64.0363f},{281.663f,22.5193f},{44.9356f,31.7687f},{-75.0565f,27.7287f},{-117.611f,26.0414f},{-245.581f,7.80207f}}},
    {4369, {3156.56f,1124.83f}, {-0.999791f,-0.020452f}, -0.5364621224824262, {{-252.058f,-1.0349f},{-250.711f,-2.70629f},{-239.325f,-10.5158f},{-54.7156f,-64.3754f},{-7.48132f,-77.0003f},{250.628f,-64.9803f},{165.937f,26.2414f},{44.4449f,31.2162f},{-72.8416f,27.6113f},{-118.362f,25.9218f},{-210.78f,21.1847f},{-239.904f,8.07114f}}},
    {4370, {3157.54f,1124.85f}, {-0.99979f,-0.0204889f}, -0.5084142236865787, {{-252.188f,-1.01236f},{-250.953f,-2.54707f},{-239.406f,-10.4677f},{-54.4356f,-64.4199f},{-7.48825f,-76.9737f},{252.311f,-64.9286f},{168.415f,26.1623f},{44.4587f,31.2324f},{-72.8634f,27.6204f},{-118.345f,25.9305f},{-210.885f,20.9874f},{-239.972f,8.07099f}}},
};
static WaterHullModel::Hull Pack(const Pose &p) {
    WaterHullModel::Hull h;
    float minX=1e30f,maxX=-1e30f,minY=1e30f,maxY=-1e30f;
    for(const auto &v:p.vertices){minX=(std::min)(minX,v.x);maxX=(std::max)(maxX,v.x);minY=(std::min)(minY,v.y);maxY=(std::max)(maxY,v.y);}
    const float cx=(minX+maxX)*.5f,cy=(minY+maxY)*.5f;
    h.data[0][0]=p.origin[0]+p.axis[0]*cx-p.axis[1]*cy;h.data[0][1]=p.origin[1]+p.axis[1]*cx+p.axis[0]*cy;
    h.data[0][2]=7;h.data[0][3]=12;h.data[1][0]=p.axis[0];h.data[1][1]=p.axis[1];
    h.data[1][2]=(maxX-minX)*.5f;h.data[1][3]=(maxY-minY)*.5f;h.data[2][0]=40;h.data[2][1]=.82f;
    h.data[3][0]=-h.data[1][2];h.data[3][2]=h.data[1][2];unsigned left=0,right=0;
    for(unsigned i=0;i<12;++i){const float x=p.vertices[i].x-cx,y=p.vertices[i].y-cy;
        h.data[4+i/2][(i%2)*2]=x;h.data[4+i/2][(i%2)*2+1]=y;
        if(x<-h.data[1][2]+.5f){h.data[3][1]+=y;++left;}if(x>h.data[1][2]-.5f){h.data[3][3]+=y;++right;}}
    if(left)h.data[3][1]/=left;if(right)h.data[3][3]/=right;return h;
}
static WaterFoamHistory::Point OldAnchor(const WaterHullModel::Hull &h) {
    const double norm=std::hypot(double(h.data[1][0]),double(h.data[1][1]));
    const double ax=h.data[1][0]/norm,ay=h.data[1][1]/norm;
    const double speed=std::hypot(double(h.data[2][0]),double(h.data[2][1]));
    const double f=WaterFoamHistory::Smooth(-.25,.25,(h.data[2][0]*ax+h.data[2][1]*ay)/(std::max)(speed,.001));
    const double x=h.data[3][2]+(h.data[3][0]-h.data[3][2])*f,y=h.data[3][3]+(h.data[3][1]-h.data[3][3])*f;
    return {h.data[0][0]+ax*x-ay*y,h.data[0][1]+ay*x+ax*y};
}
}
TEST(water_foam_stern_anchor_native_carrier_extrema_do_not_jump_across_transom)
{
    using namespace WaterFoamHistory;
    double oldLow=1e30,oldHigh=-1e30,newLow=1e30,newHigh=-1e30;
    Point oldBefore={},oldAfter={},newBefore={},newAfter={};
    for(const auto &pose:SternAnchorNative::Poses) {
        auto hull=SternAnchorNative::Pack(pose);Sample sample;CHECK(MakeSample(hull,0,sample));
        const double axisLength=std::hypot(double(hull.data[1][0]),double(hull.data[1][1]));
        const Point axis={hull.data[1][0]/axisLength,hull.data[1][1]/axisLength};
        const Point relative={sample.lanes[0].x-sample.center.x,sample.lanes[0].y-sample.center.y};
        const double cross=-relative.x*axis.y+relative.y*axis.x;
        CHECK_NEAR(cross,pose.sectionY,.00004); // Independent raw-contour double intersection oracle.
        const Point old=SternAnchorNative::OldAnchor(hull);
        oldLow=(std::min)(oldLow,old.y);oldHigh=(std::max)(oldHigh,old.y);
        newLow=(std::min)(newLow,sample.lanes[0].y);newHigh=(std::max)(newHigh,sample.lanes[0].y);
        if(pose.frame==4336){oldBefore=old;newBefore=sample.lanes[0];}
        if(pose.frame==4337){oldAfter=old;newAfter=sample.lanes[0];}
        CHECK_NEAR(sample.width[0],Clamp(sample.halfWidth*.30+std::sqrt(sample.halfWidth)*.8,2.5,30),1e-12);
        const double speed=std::hypot(double(hull.data[2][0]),double(hull.data[2][1]));
        const double response=2*Smooth(.3,12,speed)*(.8+.25*std::sqrt((std::min)(speed,250.)));
        CHECK_NEAR(sample.rate[0],response*(std::min)(1.0,6.0/sample.width[0]),1e-12);
        CHECK_NEAR(sample.rate[1],response*.22,1e-12);CHECK(sample.rate[2]==sample.rate[1]);
        // Longitudinal aft selection is unchanged, including reverse and the existing
        // smooth broadside transition. The correction concerns only transverse placement.
        for(double direction:{-1.0,0.0,1.0}) {
            auto test=hull;test.data[2][0]=float(direction*40*axis.x-40*(1-std::abs(direction))*axis.y);
            test.data[2][1]=float(direction*40*axis.y+40*(1-std::abs(direction))*axis.x);
            Sample selected;CHECK(MakeSample(test,0,selected));
            const Point r={selected.lanes[0].x-selected.center.x,selected.lanes[0].y-selected.center.y};
            const double forward=Smooth(-.25,.25,(test.data[2][0]*axis.x+test.data[2][1]*axis.y)/std::hypot(double(test.data[2][0]),double(test.data[2][1])));
            const double expectedX=test.data[3][2]+(test.data[3][0]-test.data[3][2])*forward;
            CHECK_NEAR(r.x*axis.x+r.y*axis.y,expectedX,.000001);
            CHECK_NEAR(-r.x*axis.y+r.y*axis.x,pose.sectionY,.00004);
            CHECK(Distance(selected.lanes[1],sample.lanes[1])<1e-10 && Distance(selected.lanes[2],sample.lanes[2])<1e-10);
        }
    }
    CHECK(oldHigh-oldLow>90 && newHigh-newLow<6); // Armed original endpoint-placement regression.
    CHECK(Distance(oldBefore,oldAfter)>50 && Distance(newBefore,newAfter)<2);
    std::printf("stern anchor native9poses lateral span %.7g->%.7g; jump4336/4337 %.7g->%.7g\n",
        oldHigh-oldLow,newHigh-newLow,Distance(oldBefore,oldAfter),Distance(newBefore,newAfter));
}

static WaterHullModel::Hull asymmetricSternHull(float x,float y,float vx,float vy,float angle=0)
{
    auto h=foamTestHull(x,y,vx,vy,angle);h.data[1][3]=7;h.data[3][1]=-3;h.data[3][3]=2;
    const float vertices[8]={-12,-7,12,-3,12,7,-12,1};
    for(unsigned i=0;i<8;++i)h.data[4+i/4][i%4]=vertices[i];return h;
}
TEST(water_foam_stern_anchor_turn_stop_pause_and_reverse_keep_history_contract)
{
    using namespace WaterFoamHistory;
    // The asymmetric transom corners have different averages at each end. Their
    // central cross-section is analytically [-5,4], hence the retained path is y=-.5.
    for(float angle:{0.f,.7f,1.6f})for(float direction:{-1.f,1.f}) {
        const double c=std::cos(angle),s=std::sin(angle);
        auto h=asymmetricSternHull(17,-23,float(direction*30*c),float(direction*30*s),angle);Sample sample;
        CHECK(MakeSample(h,0,sample));
        const double norm=std::hypot(double(h.data[1][0]),double(h.data[1][1]));
        const Point axis={h.data[1][0]/norm,h.data[1][1]/norm};
        CHECK_NEAR(sample.lanes[0].x,17-direction*12*axis.x+.5*axis.y,1e-10);
        CHECK_NEAR(sample.lanes[0].y,-23-direction*12*axis.y-.5*axis.x,1e-10);
    }
    History history;
    for(unsigned frame=0;frame<=120;++frame) {
        const double t=frame/30.,angle=t*.4;history.Advance(t);
        history.Observe(1,asymmetricSternHull(float(70*std::sin(angle)),float(70*(1-std::cos(angle))),
            float(28*std::cos(angle)),float(28*std::sin(angle)),float(angle)),t);
    }
    CHECK(history.SegmentCount()==16);const Segment first=history.Segments().front();
    CHECK(Distance(first.begin.lanes[0],{-12,-.5})<1e-10);
    CHECK(history.Segments().back().end.lanes[0].y>55);
    const auto stopped=asymmetricSternHull(float(70*std::sin(1.6)),float(70*(1-std::cos(1.6))),30,0,1.6f);
    const Point stoppedOrigin={stopped.data[0][0],stopped.data[0][1]};
    for(unsigned frame=121;frame<=150;++frame) {
        const double t=frame/30.;auto bob=stopped;bob.data[3][1]+=float(std::sin(t*13)*5);bob.data[3][3]-=float(std::cos(t*11)*5);
        history.Advance(t);history.Observe(1,bob,t,true,false,&stoppedOrigin);
    }
    const size_t count=history.SegmentCount();CHECK(count>=16&&count<=17);
    const Mapping mapping={-64,-64,192,192,192,192};std::vector<std::uint32_t> fresh,paused,old;
    CHECK(history.BuildAtlas(mapping,7,fresh));
    for(unsigned i=0;i<20;++i){CHECK(!history.Advance(5));CHECK(!history.Observe(1,stopped,5,true,false,&stoppedOrigin));}
    CHECK(history.BuildAtlas(mapping,7,paused)&&paused==fresh);
    for(unsigned frame=151;frame<=300;++frame){const double t=frame/30.;history.Advance(t);history.Observe(1,stopped,t,true,false,&stoppedOrigin);}
    CHECK(history.SegmentCount()==count && Distance(history.Segments().front().begin.lanes[0],first.begin.lanes[0])==0);
    CHECK(history.BuildAtlas(mapping,7,old));unsigned freshSum=0,oldSum=0;
    for(size_t i=0;i<fresh.size();++i){freshSum+=(fresh[i]>>16)&255;oldSum+=(old[i]>>16)&255;}
    CHECK(oldSum>0&&oldSum<freshSum);
    history.Advance(30);CHECK(history.SegmentCount()==0);
}

static WaterHullModel::Hull foamSizedHull(float x,float y,float vx,float vy,float beam)
{
    auto h=foamTestHull(x,y,vx,vy);const float length=(std::min)(beam*3,4096.f);
    h.data[1][2]=length;h.data[1][3]=beam;h.data[3][0]=-length;h.data[3][2]=length;
    const float p[8]={-length,-beam,length,-beam,length,beam,-length,beam};
    for(unsigned i=0;i<8;++i)h.data[4+i/4][i%4]=p[i];return h;
}
TEST(water_foam_stern_dose_preserves_small_boats_and_unmodified_side_sources)
{
    using namespace WaterFoamHistory;
    // Native PT half-widths 8.34..10.69 imply initial widths 4.812..5.823. The
    // six-world reference preserves that measured range, including both endpoints.
    for(float beam:{1.f,4.f,8.34f,9.33f,10.69f,11.f,11.2f,54.1f,74.f,4096.f})
    for(float speed:{0.f,.3f,1.f,6.f,12.f,40.f,250.f,500.f})for(float sign:{-1.f,1.f}) {
        Sample s;CHECK(MakeSample(foamSizedHull(0,0,sign*speed,0,beam),0,s));
        const double response=2*Smooth(.3,12,double(speed))*(.8+.25*std::sqrt((std::min)(double(speed),250.)));
        const double frozenWidth=Clamp(double(beam)*.30+std::sqrt(double(beam))*.8,2.5,30);
        CHECK(s.width[0]==frozenWidth);
        CHECK(s.width[1]==Clamp(double(beam)*.055,.65,3) && s.width[2]==s.width[1]);
        CHECK(s.rate[1]==response*.22 && s.rate[2]==s.rate[1]);
        if(frozenWidth<=6) CHECK(s.rate[0]==response); // Exact legacy arithmetic, no approximate tolerance.
        else CHECK_NEAR(s.rate[0]*frozenWidth,response*6,1e-12);
        CHECK(s.rate[0]>=response*.2-1e-14 && s.rate[0]<=response);
    }
}
TEST(water_foam_stern_integrated_dose_scales_with_transom_span_not_disk_area)
{
    using namespace WaterFoamHistory;
    const double pi=3.14159265358979323846;
    for(float beam:{4.f,10.69f,20.f,54.1f,74.f,4096.f})for(float speed:{6.f,12.f,40.f}) {
        Sample s;CHECK(MakeSample(foamSizedHull(0,0,speed,0,beam),0,s));
        const double w=s.width[0],r=2.5*w,response=2*Smooth(.3,12,double(speed))*(.8+.25*std::sqrt(double(speed)));
        // Closed-form radial quartic area is pi*R^2/3. Integrate the actual kernel
        // numerically in polar coordinates, independently of that antiderivative.
        const unsigned radialSteps=1024;double area=0;
        for(unsigned i=0;i<radialSteps;++i) {
            const double rho=r*(i+.5)/radialSteps;
            area+=IntegrateKernel(rho,0,0,r).mass*2*pi*rho*r/radialSteps;
        }
        const double integrated=s.rate[0]*area;
        const double expected=response*(25*pi/12)*w*(std::min)(w,6.0);
        CHECK(std::abs(integrated-expected)<expected*6e-7);
        // A point crossed by a steady path sees 16R/(15*speed) seconds of weighted
        // residence. Both whole-path and partitioned actual kernels must agree;
        // the previous rate grows centre dose with width and is an armed control.
        const double path=4*r,duration=path/speed,position=path*.5;
        const double expectedPeak=response*(8.0/3)*(std::min)(w,6.0)/speed;
        for(unsigned sections:{1u,8u,64u}) {
            double dose=0;
            for(unsigned i=0;i<sections;++i)
                dose+=s.rate[0]*duration/sections*IntegrateKernel(position-path*i/sections,0,path/sections,r).mass;
            CHECK_NEAR(dose,expectedPeak,2e-13);
        }
        if(beam>50) {
            const double oldPeak=response*(16.0/15)*r/speed;
            CHECK(oldPeak>3*expectedPeak);
            CHECK(response*area>3*integrated);
        }
    }
}
TEST(water_foam_large_stern_normalization_preserves_fixed_time_pause_stop_and_age)
{
    using namespace WaterFoamHistory;
    const Mapping map={-240,-100,480,200,256,128};std::vector<std::uint32_t> reference;
    for(unsigned fps:{30u,120u}) {
        History history;
        for(unsigned frame=0;frame<=fps*4;++frame) {
            const double t=double(frame)/fps;history.Advance(t);
            history.Observe(1,foamSizedHull(float(40*t),0,40,0,54.1f),t);
        }
        CHECK(history.SegmentCount()==16);const auto first=history.Segments().front();
        std::vector<std::uint32_t> moving,paused,aged;CHECK(history.BuildAtlas(map,7,moving));
        if(reference.empty())reference=moving;
        unsigned sum=0,peak=0;
        for(size_t i=0;i<moving.size();++i)for(unsigned shift:{0u,8u,16u}) {
            CHECK(std::abs(int((moving[i]>>shift)&255)-int((reference[i]>>shift)&255))<=1);
            if(shift==16){sum+=(moving[i]>>16)&255;peak=(std::max)(peak,(moving[i]>>16)&255u);}
        }
        CHECK(sum>10000 && peak>100 && peak<250); // Nonempty, visibly bounded density, no UNORM saturation.
        const auto stopped=foamSizedHull(160,0,40,0,54.1f); // Deliberately stale speed after position stops.
        for(unsigned pass=0;pass<20;++pass){CHECK(!history.Advance(4));CHECK(!history.Observe(1,stopped,4));}
        CHECK(history.BuildAtlas(map,7,paused)&&paused==moving);
        for(unsigned frame=fps*4+1;frame<=fps*8;++frame) {
            const double t=double(frame)/fps;history.Advance(t);history.Observe(1,stopped,t);
        }
        CHECK(history.SegmentCount()==16 && Distance(history.Segments().front().begin.lanes[0],first.begin.lanes[0])==0);
        CHECK(history.BuildAtlas(map,7,aged));unsigned oldSum=0;for(auto px:aged)oldSum+=(px>>16)&255;
        CHECK(oldSum>0 && oldSum<sum);
        history.Advance(30);CHECK(history.SegmentCount()==0);CHECK(history.BuildAtlas(map,7,aged));
        for(auto px:aged)CHECK(px==0);
    }
}


#include "WaterHullContactFoamTest.h"
TEST(water_bow_shoulder_contour_gradient_motion_bounds_and_inverse_refinement)
{
    namespace S=WaterBowShoulder;namespace T=WaterBowShoulderTest;
    unsigned tested=0;double maximum=0;
    for(double speed:{1.,15.,40.,-40.})for(double angle:{0.,.4,1.3}) {
        auto h=T::hull(speed,angle);auto c=T::contour(h);
        for(int x=-64;x<64;x+=3)for(int y=-23;y<24;y+=3) {
            S::Point local={x+.317,y+.213},p=S::ToWorld(c,local);const auto f=S::Evaluate(c,p);
            CHECK_NEAR(f.value,T::oracle(h,p.x,p.y),.000002);CHECK(f.value>=0&&f.value<=c.amplitude+.000001);
            const double step=.0001;
            CHECK_NEAR(f.dx,(T::oracle(h,p.x+step,p.y)-T::oracle(h,p.x-step,p.y))/(2*step),.00002);
            CHECK_NEAR(f.dy,(T::oracle(h,p.x,p.y+step)-T::oracle(h,p.x,p.y-step))/(2*step),.00002);
            const auto q=S::QueryLocal(c,local);
            for(unsigned k=0;k<8;++k) {
                const double a=k*.7853981633974483;const S::Point parameter={p.x+8*std::cos(a),p.y+8*std::sin(a)};
                const auto lower=S::InverseProfileLowerBounds(c,parameter,8);
                CHECK(lower.x<=std::abs((q.distance-.5*c.width)/c.width)+.000002);
                CHECK(lower.y<=q.retreat/c.length+.000002);
            }
            maximum=(std::max)(maximum,f.value);++tested;
        }
    }
    CHECK(tested>8000&&maximum>3);
    auto h=T::hull();auto c=T::contour(h);const auto centre=S::ToWorld(c,{40+.5*c.width,0});
    CHECK_NEAR(S::Evaluate(c,centre).value,c.amplitude,.000001);
    CHECK(S::Evaluate(c,S::ToWorld(c,{-42,0})).value==0);
    CHECK(S::Evaluate(c,S::ToWorld(c,{40+3.5*c.width,0})).value==0);
    WaterBowRefinement::Bow bow;CHECK(WaterBowRefinement::MakeBow(1,h,h,4,bow));
    CHECK(bow.sigmaAlong==c.width&&bow.sigmaAcross==c.width);
    CHECK(WaterBowRefinement::MaxAddedVertices==8190&&WaterBowRefinement::MaxAddedTriangles==16380);
    auto a=S::Evaluate(c,centre),b=S::Evaluate(c,centre);CHECK(a.value==b.value&&a.dx==b.dx&&a.dy==b.dy);
    h.data[2][0]=0;S::Contour stopped;CHECK(!S::Prepare(h,4,16,4,stopped));
    for(float invalid:{0.f,2.f,3.5f,13.f}){h=T::hull();h.data[0][3]=invalid;CHECK(!S::Prepare(h,4,16,4,stopped));}
    h=T::hull();h.data[4][0]=NAN;CHECK(!S::Prepare(h,4,16,4,stopped));
}

TEST(water_bow_shoulder_actual_native_contours_and_detail_anchor_stability)
{
    namespace S=WaterBowShoulder;namespace T=WaterBowShoulderTest;
    for(unsigned pose=0;pose<3;++pose) {
        const auto h=T::CapturedHull(pose);const auto c=T::contour(h);
        for(double x:{20.31,30.27,38.17,43.13,46.21})for(double y:{-11.13,-7.21,-2.17,1.31,6.19,10.23}) {
            const auto p=S::ToWorld(c,{x,y});const auto f=S::Evaluate(c,p);const double e=.0001;
            CHECK_NEAR(f.value,T::oracle(h,p.x,p.y),.000003);
            CHECK_NEAR(f.dx,(T::oracle(h,p.x+e,p.y)-T::oracle(h,p.x-e,p.y))/(2*e),.00003);
            CHECK_NEAR(f.dy,(T::oracle(h,p.x,p.y+e)-T::oracle(h,p.x,p.y-e))/(2*e),.00003);
        }
    }
    auto h=T::hull(-40);h.data[2][1]=-.0004f;const auto a=T::contour(h);
    h.data[2][1]=.0004f;const auto b=T::contour(h);
    CHECK(std::hypot(a.supportCentre.x-b.supportCentre.x,a.supportCentre.y-b.supportCentre.y)<.002);
    // The physical support value and field do not jump when an extremal edge tie changes.
    const auto p=S::ToWorld(a,{-42,2});CHECK_NEAR(S::Evaluate(a,p).value,S::Evaluate(b,p).value,.0001);
}

TEST(water_bow_shoulder_large_hull_scale_cap_support_and_small_boat_identity)
{
    namespace S=WaterBowShoulder;namespace T=WaterBowShoulderTest;
    // The native carrier half-beam is about54; the PT remains below the old cap.
    // Assert the intended dimensions independently so a stale6-world cap fails.
    for(double beam:{9.,16.,27.,54.,120.})for(double speed:{1.,25.,40.,-40.}) {
        const auto h=T::scaledHull(280,beam,speed);const auto c=T::contour(h);
        CHECK_NEAR(c.width,(std::max)(3.,(std::min)(12.,beam*double(.45f))),.000001);
        CHECK(c.length<=32 && c.width<=12 && c.reach==h.data[2][3]+110);
        const double sign=speed>0?1:-1;
        const auto crest=S::ToWorld(c,{sign*(280+.5*c.width),0});
        CHECK_NEAR(S::Evaluate(c,crest).value,c.amplitude,.000001);
        CHECK_NEAR(c.amplitude,WaterVesselWaves::BowPressure(float(beam),float(std::abs(speed))).amplitude,.000001);
        CHECK(S::Evaluate(c,S::ToWorld(c,{sign*(280+3.5*c.width),0})).value==0);
        // Entire compact support lies within hull radius+42, inside the inherited
        // radius+80 start of attenuation and+110 finite collection support.
        CHECK(3.5*c.width+WaterFftGeometry::MaxHorizontalDisplacement<=50);
        for(double distance:{.31,3.17,6.23,12.37,20.41,34.29,42.13}) {
            const auto p=S::ToWorld(c,{sign*(280+distance),.271});const auto f=S::Evaluate(c,p);
            CHECK_NEAR(f.value,T::oracle(h,p.x,p.y),.000003);
            const double e=.0001;
            CHECK_NEAR(f.dx,(T::oracle(h,p.x+e,p.y)-T::oracle(h,p.x-e,p.y))/(2*e),.00003);
            CHECK_NEAR(f.dy,(T::oracle(h,p.x,p.y+e)-T::oracle(h,p.x,p.y-e))/(2*e),.00003);
            CHECK(f.value>=0 && f.value<=c.amplitude+.000001);
            const auto q=S::QueryLocal(c,S::ToLocal(c,p));
            for(unsigned k=0;k<16;++k) {
                const double a=k*.39269908169872415;
                const auto lower=S::InverseProfileLowerBounds(c,{p.x+8*std::cos(a),p.y+8*std::sin(a)},8);
                CHECK(lower.x<=std::abs((q.distance-.5*c.width)/c.width)+.000002);
                CHECK(lower.y<=q.retreat/c.length+.000002);
            }
        }
    }
    const auto large=T::scaledHull(280,54);const auto c=T::contour(large);
    const auto outside=S::ToWorld(c,{292,0});
    CHECK(S::Evaluate(c,outside).value-T::oracle(large,outside.x,outside.y,6)>3);
    WaterBowRefinement::Bow bow;CHECK(WaterBowRefinement::MakeBow(7,large,large,4,bow));
    CHECK(bow.sigmaAlong==12 && bow.sigmaAcross==12 && bow.contour.length==32);
    CHECK(WaterBowRefinement::MaxRegions==3 && WaterBowRefinement::WindowRadius==6);
    CHECK(WaterBowRefinement::MaxAddedVertices==8190 && WaterBowRefinement::MaxAddedTriangles==16380);
    // Small captured PT bodies, including reverse/rotation in the prior test, must
    // retain the exact old6-cap scalar. No tolerance can hide a changed footprint.
    for(unsigned pose=0;pose<3;++pose) {
        const auto h=T::CapturedHull(pose);const auto contourShape=T::contour(h);CHECK(contourShape.width<6);
        for(double x:{-45.31,20.31,38.17,43.13,46.21})for(double y:{-11.13,-2.17,6.19,10.23}) {
            const auto p=S::ToWorld(contourShape,{x,y});
            CHECK(T::oracle(h,p.x,p.y)==T::oracle(h,p.x,p.y,6));
        }
    }
}


#if defined(__linux__)
#include "W3DDevice/GameClient/W3DWater.h"
#include "Common/LogicThread.h"
#include "GameClient/Water.h"
#include "GameLogic/PolygonTrigger.h"
#include "../GameEngineDevice/Source/PosixDevice/Render/PosixDevice9.h"
#include "../GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.h"
#include "../GameEngineDevice/Source/PosixDevice/Render/SdlResourceMirror.h"
#include "dx8wrapper.h"
#include "ww3d.h"
#include "wwmath.h"
#include "formconv.h"
#include "texture.h"
#include <SDL3/SDL.h>
#include "../Libraries/Source/WWVegas/WW3D2/engineshader.h"

namespace {
struct WaterShaderNameProbe : PosixDevice9 { using PosixDevice9::Engine_Name_Of; };
class LiveWaterStyleProbe : public WaterRenderObjClass
{
public:
    void setMirrorState(bool modern, WaterType type, bool rendering) {
        m_useModernWater=modern; m_waterType=type; m_renderingMirror=rendering;
    }
    void setReflectionMode(WaterScreenReflection::Mode mode) { m_waterReflectionMode=mode; }
    TextureClass *ensureReflectionTarget() { ensureMapReflectionTarget(); return m_pReflectionTexture; }
    void clearReflectionRefresh() { m_reflectionReused=FALSE; }
    bool reflectionNeedsRefresh() const { return m_reflectionReused; }
    bool usesModernWater() const { return m_useModernWater; }
    const std::vector<WaterOpticalContext::Polygon> &opticalPolygonsForDraw() {
        refreshModernWaterOpticalPolygons();
        return m_modernOpticalPolygons;
    }
    void prepareShaders(IDirect3DDevice9 *device) {
        const RenderUInt32 tokens[]={0xffff0101u,0x0000ffffu};
        CHECK(device->CreatePixelShader(tokens,&m_riverWaterPixelShader)==D3D_OK);
        CHECK(device->CreatePixelShader(tokens,&m_trapezoidWaterPixelShader)==D3D_OK);
        registerWaterStyleShaders();
        m_modernWaveTime=37.5f;
        m_level=7;m_drawnMotionEnabled=TRUE;
    }
    void seedPresentation() {
        m_modernHulls.resize(1);m_modernContactHulls.resize(1);m_modernVisualHulls.resize(1);
        m_modernHullObjectIDs.push_back(42);m_modernFoamDrawObjectIDs.push_back(42);
        m_modernFoamDrawFrame=123;m_modernShoreReady=TRUE;
        m_reflectionReused=FALSE;
    }
    void checkChanged(bool modern) {
        CHECK_EQ(bool(m_useModernWater),modern);
        CHECK(m_modernHulls.empty() && m_modernContactHulls.empty() && m_modernVisualHulls.empty());
        CHECK(m_modernHullObjectIDs.empty() && m_modernFoamDrawObjectIDs.empty());
        CHECK(!m_modernShoreReady);
        CHECK(m_reflectionReused); // force the new clear colour even with a stationary camera
        CHECK_EQ(m_modernWaveTime,37.5f);CHECK_EQ(m_level,7);CHECK(m_drawnMotionEnabled);
        const auto river=EngineShader_From_File(WaterShaderNameProbe::Engine_Name_Of(m_riverWaterPixelShader).c_str());
        const auto flat=EngineShader_From_File(WaterShaderNameProbe::Engine_Name_Of(m_trapezoidWaterPixelShader).c_str());
        CHECK(river==(modern?ENGINE_SHADER_WATER_RIVER:ENGINE_SHADER_WATER_CLASSIC_RIVER));
        CHECK(flat==(modern?ENGINE_SHADER_WATER_TRAPEZOID:ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID));
    }
    void checkUnchanged() {
        CHECK(!m_modernHulls.empty() && !m_modernFoamDrawObjectIDs.empty());
        CHECK(m_modernShoreReady);
        CHECK(!m_reflectionReused); // no per-frame history reset or forced mirror refresh
    }
};
}

TEST(water_full_mirror_admits_only_hybrid_modern_map_water)
{
    boot();
    WaterTransparencySetting *saved=(WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer();
    TheWaterTransparency=newInstance(WaterTransparencySetting);
    {
        LiveWaterStyleProbe water;
        const WaterRenderObjClass &query=water;
        CHECK(!query.isModernFullMirror());
        // Every style/type/pass/mode combination uses the real render-policy query.
        // Original reflections and Modern sea water retain the old selective mirror.
        for(bool modern:{false,true})
            for(int type=0;type<WaterRenderObjClass::WATER_TYPE_MAX;++type)
                for(bool rendering:{false,true}) {
                    water.setMirrorState(modern,(WaterRenderObjClass::WaterType)type,rendering);
                    for (WaterScreenReflection::Mode mode : {
                        WaterScreenReflection::Mirror, WaterScreenReflection::ScreenSpace,
                        WaterScreenReflection::Hybrid, WaterScreenReflection::SkyOnly}) {
                        water.setReflectionMode(mode);
                        CHECK_EQ(bool(query.isModernFullMirror()),
                            modern && rendering && type==WaterRenderObjClass::WATER_TYPE_0_TRANSLUCENT
                            && mode==WaterScreenReflection::Hybrid);
                    }
                }
        water.setReflectionMode(WaterScreenReflection::Hybrid);
        water.setMirrorState(true,WaterRenderObjClass::WATER_TYPE_0_TRANSLUCENT,false);
        CHECK(!query.isModernFullMirror()); // the main view never borrows the broader mirror rules
    }
    TheWaterTransparency=saved;
}

TEST(water_hybrid_target_resize_preserves_queued_reflections_and_refreshes_new_targets)
{
    struct RestoreQuality { WaterPerformanceSettings::Configuration saved=WaterPerformanceSettings::Applied();
        ~RestoreQuality() { WaterPerformanceSettings::Applied()=saved; } } restoreQuality;
    boot();
    const bool video=SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (!video) {
        std::printf("SKIP: reflection allocation test needs SDL video: %s\n",SDL_GetError());
        return;
    }
    const char *oldFrames=SDL_GetHint("ZH_OFFSCREEN_FRAMES"), *oldHz=SDL_GetHint("ZH_OFFSCREEN_HZ");
    const bool hadFrames=oldFrames!=NULL, hadHz=oldHz!=NULL;
    const std::string savedFrames=oldFrames?oldFrames:"", savedHz=oldHz?oldHz:"";
    SDL_SetHint("ZH_OFFSCREEN_FRAMES","1");
    SDL_SetHint("ZH_OFFSCREEN_HZ","0");
    WWMath::Init();
    // WW3D::Init normally initializes this before DX8Wrapper. This minimal harness
    // needs the same mapping so render-target caps recognize the display format.
    Init_D3D_To_WW3_Conversion();
    const bool initialized=DX8Wrapper::Init(NULL);
    const bool ready=initialized &&
        WW3D::Set_Render_Device(0,64,64,32,1,false,false,false)==WW3D_ERROR_OK;
    auto *device=ready?static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device()):NULL;
    if (device && device->Get_Gpu()) {
        std::printf("reflection target lifecycle: GPU readback across High/Low and Legacy/Hybrid targets\n");
        WaterTransparencySetting *savedTransparency=
            (WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer();
        TheWaterTransparency=newInstance(WaterTransparencySetting);
        {
            LiveWaterStyleProbe water;
            water.setMirrorState(true,WaterRenderObjClass::WATER_TYPE_0_TRANSLUCENT,false);
            IDirect3DSurface9 *back=NULL;
            CHECK(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)==D3D_OK);
            const unsigned sizes[]={512,1024,512,256,512,1024};
            const WaterScreenReflection::Mode modes[]={WaterScreenReflection::Mirror,WaterScreenReflection::Hybrid,
                WaterScreenReflection::Hybrid,WaterScreenReflection::Mirror,WaterScreenReflection::Mirror,WaterScreenReflection::Hybrid};
            const int details[]={1,1,0,0,1,1};
            const RenderUInt32 colours[]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffff00u,0xff00ffffu,0xffff00ffu};
            const unsigned steps=sizeof(sizes)/sizeof(sizes[0]);
            IDirect3DBaseTexture9 *previous=NULL; // borrowed from the bound sampler
            for (unsigned step=0;back && step<steps;++step) {
                WaterPerformanceSettings::Applied().reflectionDetail=details[step];
                water.setReflectionMode(modes[step]);
                water.clearReflectionRefresh();
                TextureClass *target=water.ensureReflectionTarget();
                CHECK(target && water.reflectionNeedsRefresh());
                if (!target) break;
                D3DSURFACE_DESC desc={};
                CHECK(target->Peek_D3D_Texture()->GetLevelDesc(0,&desc)==D3D_OK);
                CHECK_EQ(desc.Width,sizes[step]);CHECK_EQ(desc.Height,sizes[step]);
                // Replacing WaterRenderObjClass's owner must not release an old texture still
                // held by the sampler; queued GPU work will outlive both CPU owners below.
                if (previous) {
                    IDirect3DBaseTexture9 *bound=NULL;
                    CHECK(device->GetTexture(1,&bound)==D3D_OK && bound==previous);
                    if (bound) bound->Release();
                }
                water.clearReflectionRefresh();
                CHECK(water.ensureReflectionTarget()==target && !water.reflectionNeedsRefresh());
                DX8Wrapper::Set_DX8_Texture(1,target->Peek_D3D_Texture());
                DX8Wrapper::Flush_Deferred_D3D9_State();
                previous=target->Peek_D3D_Texture();
                DX8Wrapper::Set_Render_Target_With_Z(target);
                D3DVIEWPORT9 viewport={0,0,sizes[step],sizes[step],0.0f,1.0f};
                CHECK(device->SetViewport(&viewport)==D3D_OK);
                CHECK(device->Clear(0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,colours[step],1.0f,0)==D3D_OK);
                IDirect3DSurface9 *surface=target->Get_D3D_Surface_Level();
                const RenderRect stripe={int(step*64/steps),0,int((step+1)*64/steps),64};
                CHECK(surface && device->StretchRect(surface,NULL,back,&stripe,D3DTEXF_POINT)==D3D_OK);
                if (surface) surface->Release();
                DX8Wrapper::Set_Render_Target((IDirect3DSurface9 *)NULL);
            }
            // Retire old GPU copies before submitting the clears/blits which still use them.
            // Coloured stripes prove queued references survive every mode/detail replacement.
            DX8Wrapper::Set_DX8_Texture(1,NULL);
            DX8Wrapper::Flush_Deferred_D3D9_State();
            device->Get_Mirrors()->Collect_Dead();
            std::vector<uint8_t> pixels;
            CHECK(device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),64,64,pixels));
            if (pixels.size()==64*64*4) for (unsigned step=0;step<steps;++step) {
                const size_t at=(32*64+(step*2+1)*64/(steps*2))*4;
                CHECK_EQ(unsigned(pixels[at]),colours[step]&255u);
                CHECK_EQ(unsigned(pixels[at+1]),(colours[step]>>8)&255u);
                CHECK_EQ(unsigned(pixels[at+2]),(colours[step]>>16)&255u);
            } else CHECK(false);
            if (back) back->Release();
        }
        TheWaterTransparency=savedTransparency;
    } else std::printf("SKIP: reflection allocation test needs an offscreen GPU renderer: %s\n",SDL_GetError());
    if (initialized) DX8Wrapper::Shutdown();
    WWMath::Shutdown();
    if (hadFrames) SDL_SetHint("ZH_OFFSCREEN_FRAMES",savedFrames.c_str()); else SDL_ResetHint("ZH_OFFSCREEN_FRAMES");
    if (hadHz) SDL_SetHint("ZH_OFFSCREEN_HZ",savedHz.c_str()); else SDL_ResetHint("ZH_OFFSCREEN_HZ");
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(water_polygon_draw_snapshot_survives_live_height_geometry_and_lifetime_changes)
{
    boot();
    const Int savedStyle=TheGlobalData->m_waterStyle;
    TheWritableGlobalData->m_waterStyle=0;
    WaterTransparencySetting *saved=(WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer();
    WaterTransparencySetting *settings=newInstance(WaterTransparencySetting);
    TheWaterTransparency=settings;
    PolygonTrigger *area=newInstance(PolygonTrigger)(4);
    const ICoord3D original[]={{0,0,4},{80,0,4},{80,60,4},{0,60,4}};
    for(const ICoord3D &point:original) area->addPoint(point);
    area->setWaterArea(TRUE);
    PolygonTrigger::addPolygonTrigger(area);
    {
        LiveWaterStyleProbe water;
        // The shared polygon snapshot and its optical consumer need no GPU or running logic.
        water.takeReadsForRender();
        auto checkDraw=[&](bool present,Int offset,Int level) {
            const Int previous=LogicThread::g_windowOpen.exchange(1);
            const auto &polygons=water.opticalPolygonsForDraw();
            CHECK_EQ(polygons.size(),present?1u:0u);
            if(present && polygons.size()==1) {
                CHECK_EQ(polygons[0].points.size(),4u);
                if(polygons[0].points.size()==4) for(unsigned i=0;i<4;++i) {
                    CHECK_EQ(polygons[0].points[i].x,original[i].x+offset);
                    CHECK_EQ(polygons[0].points[i].y,original[i].y+offset);
                    CHECK_EQ(polygons[0].points[i].z,level);
                }
            }
            LogicThread::g_windowOpen.store(previous);
        };
        checkDraw(true,0,4);
        for(Int i=0;i<4;++i) {
            ICoord3D changed={original[i].x+20,original[i].y+20,12};
            area->setPoint(changed,i);
        }
        checkDraw(true,0,4);
        water.takeReadsForRender();
        checkDraw(true,20,12);
        PolygonTrigger::removePolygonTrigger(area);
        area->deleteInstance();
        checkDraw(true,20,12);
        water.takeReadsForRender();
        checkDraw(false,0,0);
    }
    TheWaterTransparency=saved;
    settings->deleteInstance();
    TheWritableGlobalData->m_waterStyle=savedStyle;
}

TEST(water_style_changes_at_render_boundary_without_advancing_paused_time_or_reloading_the_map)
{
    GlobalData *saved=TheWritableGlobalData;
    GlobalData *scratch=NEW GlobalData;TheWritableGlobalData=scratch;
    WaterTransparencySetting *savedTransparency=(WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer();
    TheWaterTransparency=newInstance(WaterTransparencySetting);
    IDirect3D9 *d3d=Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS parameters={};parameters.BackBufferWidth=16;parameters.BackBufferHeight=16;
    parameters.BackBufferFormat=D3DFMT_X8R8G8B8;parameters.Windowed=TRUE;
    IDirect3DDevice9 *device=NULL;
    CHECK(d3d && d3d->CreateDevice(0,D3DDEVTYPE_HAL,NULL,0,&parameters,&device)==D3D_OK);
    if (device) {
        {
            TheWritableGlobalData=NULL;
            LiveWaterStyleProbe water;
            CHECK(!water.usesModernWater());
            TheWritableGlobalData=scratch;scratch->m_waterStyle=1;
            water.prepareShaders(device);water.updateRenderTargetTextures(NULL);
            for (unsigned i=0;i<32;++i) {
                const bool modern=(i&1)!=0;
                water.seedPresentation();scratch->m_waterStyle=modern?1:0;
                // No camera/map/device rendering: this is the real pre-mirror boundary,
                // which must apply an accepted option even while simulation time is paused.
                water.updateRenderTargetTextures(NULL);water.checkChanged(modern);
                water.seedPresentation();water.updateRenderTargetTextures(NULL);water.checkUnchanged();
            }
            TheWritableGlobalData=NULL;water.seedPresentation();
            water.updateRenderTargetTextures(NULL);water.checkChanged(false);
            TheWritableGlobalData=scratch;
            // Classic graphics keeps EA's water even with Modern asked for.
            scratch->m_waterStyle=1;scratch->m_classicGraphics=TRUE;
            water.seedPresentation();water.updateRenderTargetTextures(NULL);water.checkUnchanged();
        }
        device->Release();
    } else {
        ((WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer())->deleteInstance();
        TheWaterTransparency=NULL;
    }
    if (d3d) d3d->Release();
    TheWaterTransparency=savedTransparency;
    delete scratch;TheWritableGlobalData=saved;
}
namespace {
class WaterRenderCacheProbe : public WaterRenderObjClass {
public:
	void seedCaches() {
		using namespace WaterProjectedMesh;
		const Point corners[4]={{-95,-170},{81,-110},{120,92},{-64,170}};
		Mesh mesh;
		CHECK(m_modernRenderCaches.topology.Build(corners,testWaterProjection(),{16,12},mesh));
		m_modernRenderCaches.prepared.Process(mesh,5,false,false,[](Mesh &) {});
		m_modernRenderCaches.staticSamples.Get(1,2,3,[]() {
			return WaterStaticSampleCache::Sample{{.2f,.1f,0},.4f,5,1};
		});
		m_modernHeights.assign(81,30);
		CHECK(m_modernRenderCaches.terrain.Prewarm(m_modernHeights.data(),9,9,1,10));
		m_modernRenderCaches.samples.Store(SampleKey(1,2,3),{});
		CHECK(m_modernRenderCaches.topology.EntryCount()>0 && m_modernRenderCaches.prepared.EntryCount()>0);
		CHECK(m_modernRenderCaches.staticSamples.Size()>0 && m_modernRenderCaches.terrain.EntryCount()>0);
		CHECK(m_modernRenderCaches.samples.Size()>0);
		m_modernShorePreparation.revision=m_modernShoreRevision;
	}
	void checkCleared() {
		CHECK(m_modernRenderCaches.topology.EntryCount()==0 && m_modernRenderCaches.prepared.EntryCount()==0);
		CHECK(m_modernRenderCaches.staticSamples.Size()==0 && m_modernRenderCaches.terrain.EntryCount()==0);
		CHECK(m_modernRenderCaches.samples.Size()==0);
	}
	void changeStyle() { m_useModernWater=TRUE;synchronizeWaterStyle(); }
	void changeWaterPolygons() {
		m_modernOpticalPolygonsValid=TRUE;
		WaterOpticalContext::Polygon polygon;
		polygon.points={{0,0,4},{80,0,4},{80,60,4},{0,60,4}};
		m_modernOpticalPolygons={polygon};
		// The sync snapshot now has no polygons: an authored water area was removed.
		m_waterPolygonsForDraw.clear();
		refreshModernWaterOpticalPolygons();
	}
	void checkPreparationCleared() { CHECK(m_modernShorePreparation.revision==0); }
};
}

TEST(water_render_caches_clear_at_real_style_water_edit_map_and_device_boundaries)
{
	boot();
	const Int savedStyle=TheGlobalData->m_waterStyle;
	TheWritableGlobalData->m_waterStyle=0;
	WaterTransparencySetting *saved=(WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer();
	TheWaterTransparency=newInstance(WaterTransparencySetting);
	{
		WaterRenderCacheProbe water;
		water.seedCaches();water.changeStyle();water.checkCleared();water.checkPreparationCleared();
		water.seedCaches();water.changeWaterPolygons();water.checkCleared();water.checkPreparationCleared();
		water.seedCaches();water.reset();water.checkCleared();water.checkPreparationCleared();
		water.seedCaches();water.ReleaseResources();water.checkCleared();
	}
	TheWaterTransparency=saved;
	TheWritableGlobalData->m_waterStyle=savedStyle;
}

#endif

TEST(water_quality_keeps_scene_depth_independent_of_colour_demand)
{
	using namespace WaterPerformanceSettings;
	const auto saved = Applied();
	Apply(false, 0, Off, 0);
	CHECK(!Applied().sceneReflections && Applied().requestedFftResolution == 128);
	CHECK(Applied().refraction == Off && Applied().reflectionDetail == 0);
	CHECK(!SceneColourRequired(Off, false));
	CHECK(SceneColourRequired(Off, true));
	CHECK(SceneColourRequired(Simple, false) && SceneColourRequired(Full, false));
	Apply(true, 1, Full, 1);
	CHECK(Applied().sceneReflections && Applied().requestedFftResolution == 256);
	Applied() = saved;
}

#include "Platform/WaterProjectedMeshCache.h"

#include "Platform/WaterStaticSampleCache.h"

#include "Platform/WaterPreparedMeshCache.h"
#include "Platform/WaterTerrainCulling.h"
#include "Platform/WaterWaveCageMesh.h"

namespace {
bool projectedCacheSameDouble(double a,double b)
{
	uint64_t aa,bb;std::memcpy(&aa,&a,sizeof(aa));std::memcpy(&bb,&b,sizeof(bb));return aa==bb;
}
bool projectedCacheSameMesh(const WaterProjectedMesh::Mesh &a,const WaterProjectedMesh::Mesh &b)
{
	if (a.cells!=b.cells || a.indices!=b.indices || a.vertices.size()!=b.vertices.size() ||
		a.transitions.size()!=b.transitions.size() || a.collars.size()!=b.collars.size()) return false;
	for (size_t i=0;i<a.vertices.size();++i) {
		const auto &x=a.vertices[i],&y=b.vertices[i];
		if (!projectedCacheSameDouble(x.world.x,y.world.x) || !projectedCacheSameDouble(x.world.y,y.world.y) ||
			!projectedCacheSameDouble(x.edgeParameter,y.edgeParameter) || x.boundary!=y.boundary || x.knot!=y.knot ||
			x.transition!=y.transition || x.collar!=y.collar || x.lattice!=y.lattice) return false;
	}
	for (size_t i=0;i<a.transitions.size();++i) {
		const auto &x=a.transitions[i],&y=b.transitions[i];
		if (!projectedCacheSameDouble(x.analytic,y.analytic)) return false;
		for (int j=0;j<3;++j) if (!projectedCacheSameDouble(x.source[j].x,y.source[j].x) ||
			!projectedCacheSameDouble(x.source[j].y,y.source[j].y) || !projectedCacheSameDouble(x.weight[j],y.weight[j])) return false;
	}
	for (size_t i=0;i<a.collars.size();++i) {
		const auto &x=a.collars[i],&y=b.collars[i];
		if (x.count!=y.count || !projectedCacheSameDouble(x.fade,y.fade)) return false;
		for (int j=0;j<x.count;++j) if (x.edge[j]!=y.edge[j] || !projectedCacheSameDouble(x.parameter[j],y.parameter[j]) ||
			!projectedCacheSameDouble(x.weight[j],y.weight[j])) return false;
	}
	return true;
}
std::vector<WaterProjectedMesh::BoundaryKnot> projectedCacheKnots(const WaterProjectedMesh::Point corners[4])
{
	using namespace WaterProjectedMesh;std::vector<BoundaryKnot> knots;
	for (int edge=0;edge<4;++edge) {
		Point a=corners[edge],b=corners[(edge+1)%4];if (a.x>b.x || (a.x==b.x && a.y>b.y)) std::swap(a,b);
		for (int i=0;i<=8;++i) { const double t=i/8.;knots.push_back({{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t},edge,i,t}); }
	}
	return knots;
}
}

TEST(water_projected_topology_cache_copies_every_base_mesh_field_before_dynamic_edits)
{
	using namespace WaterProjectedMesh;WaterProjectedMeshCache::Cache cache;
	const Point corners[4]={{-95,-170},{81,-110},{120,92},{-64,170}};
	const auto projection=testWaterProjection();const auto knots=projectedCacheKnots(corners);
	Mesh reference,actual;bool reused=true;
	CHECK(Build(corners,projection,{18,12},reference,60000,knots));
	CHECK(!reference.vertices.empty() && !reference.indices.empty() && !reference.transitions.empty() && !reference.collars.empty());
	CHECK(cache.Build(corners,projection,{18,12},actual,60000,knots,&reused));CHECK(!reused);
	CHECK(projectedCacheSameMesh(reference,actual));
	for (int frame=0;frame<3;++frame) {
		// Model downstream refinement, terrain culling and clipping changing every owned vector.
		actual.vertices.front().world.x+=100;actual.vertices.front().boundary=99;
		actual.indices.clear();actual.transitions.front().analytic=-1;actual.collars.front().fade=-1;actual.cells=0;
		actual.vertices.push_back(Vertex{});actual.transitions.push_back(CentreTransition{});actual.collars.push_back(BoundaryCollar{});
		CHECK(cache.Build(corners,projection,{18,12},actual,60000,knots,&reused));CHECK(reused);
		CHECK(projectedCacheSameMesh(reference,actual));
	}
	CHECK(cache.EntryCount()==1 && cache.CachedVertices()==reference.vertices.size());
	cache.Clear();CHECK(cache.EntryCount()==0 && cache.CachedVertices()==0);
	CHECK(cache.Build(corners,projection,{18,12},actual,60000,knots,&reused));CHECK(!reused);
	CHECK(projectedCacheSameMesh(reference,actual));
}

TEST(water_projected_topology_cache_keys_every_build_input_exactly)
{
	using namespace WaterProjectedMesh;WaterProjectedMeshCache::Cache cache(262144,128);
	const Point baseCorners[4]={{-95,-170},{81,-110},{120,92},{-64,170}};
	const Projection baseProjection=testWaterProjection();const auto baseKnots=projectedCacheKnots(baseCorners);
	Mesh baseline;CHECK(cache.Build(baseCorners,baseProjection,{18,12},baseline,60000,baseKnots));
	// Includes both dimensions selected from viewport size, every prepared projection scalar,
	// exact polygon coordinates, the caller's budget, and each ordered boundary-knot member.
	for (int input=0;input<43;++input) {
		Point corners[4];std::copy(baseCorners,baseCorners+4,corners);Projection projection=baseProjection;
		Resolution resolution={18,12};size_t budget=60000;auto knots=baseKnots;
		if (input<8) {
			double &coordinate=input%2?corners[input/2].y:corners[input/2].x;
			coordinate=std::nextafter(coordinate,INFINITY);
		} else if (input<17) {
			double &value=projection.matrix[input-8];value=std::nextafter(value,INFINITY);CHECK(projection.Prepare());
		} else if (input<26) {
			double &value=projection.inverse[input-17];value=std::nextafter(value,INFINITY);
		} else switch (input) {
			case 26: projection.depth.a=.0001;break;
			case 27: projection.depth.b=.0001;break;
			case 28: projection.depth.c=1.01;break;
			case 29: projection.nearDistance=.2;break;
			case 30: projection.farDistance=9;break;
			case 31: projection.margin=.13;break;
			case 32: ++resolution.columns;break;
			case 33: ++resolution.rows;break;
			case 34: --budget;break;
			case 35: knots[0].world.x=std::nextafter(knots[0].world.x,INFINITY);break;
			case 36: knots[0].world.y=std::nextafter(knots[0].world.y,INFINITY);break;
			case 37: ++knots[0].boundary;break;
			case 38: ++knots[0].index;break;
			case 39: knots[0].edgeParameter=.01;break;
			case 40: std::swap(knots[0],knots[1]);break;
			case 41: knots.pop_back();break;
			case 42: projection.matrix[1]=-0.0;break;
		}
		Mesh reference,actual;bool reused=true;
		CHECK(Build(corners,projection,resolution,reference,budget,knots));
		CHECK(cache.Build(corners,projection,resolution,actual,budget,knots,&reused));CHECK(!reused);
		CHECK(projectedCacheSameMesh(reference,actual));
		CHECK(cache.Build(corners,projection,resolution,actual,budget,knots,&reused));CHECK(reused);
		CHECK(projectedCacheSameMesh(reference,actual));
	}
}

TEST(water_projected_topology_cache_preserves_failed_and_empty_build_results)
{
	using namespace WaterProjectedMesh;WaterProjectedMeshCache::Cache cache;
	const Point corners[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
	const auto projection=testWaterProjection();Mesh baseline;CHECK(cache.Build(corners,projection,{8,8},baseline));
	for (int failure=0;failure<3;++failure) {
		Point changed[4];std::copy(corners,corners+4,changed);auto p=projection;size_t budget=60000;
		if (failure==0) budget=1;
		if (failure==1) changed[2]={-500,0}; // Concave authored polygon.
		if (failure==2) { p.inverse[6]=1;p.inverse[8]=0; } // Failure after some appended vertices.
		Mesh reference=baseline,actual=baseline;bool reused=true;
		CHECK(!Build(changed,p,{8,8},reference,budget));
		CHECK(!cache.Build(changed,p,{8,8},actual,budget,{},&reused));CHECK(!reused);
		CHECK(projectedCacheSameMesh(reference,actual));
		CHECK(cache.EntryCount()==1 && cache.CachedVertices()==baseline.vertices.size());
		CHECK(cache.Build(corners,projection,{8,8},actual,60000,{},&reused));CHECK(reused);
		CHECK(projectedCacheSameMesh(baseline,actual));
	}
	Point invisible[4]={{1000,1000},{1100,1000},{1100,1100},{1000,1100}};Mesh empty,actual=baseline;bool reused=true;
	CHECK(Build(invisible,projection,{8,8},empty) && empty.vertices.empty());
	CHECK(cache.Build(invisible,projection,{8,8},actual,60000,{},&reused));CHECK(!reused);
	CHECK(projectedCacheSameMesh(empty,actual));
	CHECK(cache.Build(invisible,projection,{8,8},actual,60000,{},&reused));CHECK(reused);
	CHECK(projectedCacheSameMesh(empty,actual));
}

TEST(water_projected_topology_cache_bounds_multi_patch_storage_and_evicts_deterministically)
{
	using namespace WaterProjectedMesh;const auto projection=testWaterProjection();
	const Point corners[4]={{-500,-500},{500,-500},{500,500},{-500,500}};
	Mesh reference;CHECK(Build(corners,projection,{8,8},reference));const size_t count=reference.vertices.size();
	WaterProjectedMeshCache::Cache cache(count*2,8);Mesh actual;bool reused=true;
	// Distinct authored patches have identical visible meshes, but must retain separate exact keys.
	for (int patch=0;patch<3;++patch) {
		Point changed[4];std::copy(corners,corners+4,changed);changed[0].x-=patch;
		CHECK(cache.Build(changed,projection,{8,8},actual,60000,{},&reused));CHECK(!reused);
		CHECK(projectedCacheSameMesh(reference,actual));
		CHECK(cache.CachedVertices()<=count*2 && cache.EntryCount()<=2);
	}
	Point retained[4];std::copy(corners,corners+4,retained);retained[0].x-=1;
	CHECK(cache.Build(retained,projection,{8,8},actual,60000,{},&reused));CHECK(reused);
	CHECK(cache.Build(corners,projection,{8,8},actual,60000,{},&reused));CHECK(!reused); // FIFO oldest was evicted.
	WaterProjectedMeshCache::Cache tooSmall(count-1,8),disabled(262144,0);
	for (auto *uncached:{&tooSmall,&disabled}) for (int repeat=0;repeat<2;++repeat) {
		CHECK(uncached->Build(corners,projection,{8,8},actual,60000,{},&reused));CHECK(!reused);
		CHECK(projectedCacheSameMesh(reference,actual));CHECK(uncached->EntryCount()==0 && uncached->CachedVertices()==0);
	}
	WaterProjectedMeshCache::Cache emptyCache(0,2);
	for (int patch=0;patch<3;++patch) {
		const double x=1000+patch*100.;const Point invisible[4]={{x,1000},{x+10,1000},{x+10,1100},{x,1100}};
		CHECK(emptyCache.Build(invisible,projection,{8,8},actual));
		CHECK(actual.vertices.empty() && emptyCache.CachedVertices()==0 && emptyCache.EntryCount()<=2);
	}
}

namespace {
WaterStaticSampleCache::Sample staticCacheReference(const uint8_t field[9],float x,float y,float level,float terrainOffset=0)
{
	const auto offshore=WaterSurfaceModel::SampleOffshore(field,3,3,x/10,y/10,10);
	const float depth=level-(x*.2f+y*.3f+terrainOffset);
	return {offshore,offshore.height*.7f,depth,WaterSurfaceModel::TerrainDamping(depth,offshore.height)};
}
bool staticCacheSameSample(const WaterStaticSampleCache::Sample &a,const WaterStaticSampleCache::Sample &b)
{
	const float x[]={a.offshore.height,a.offshore.dx,a.offshore.dy,a.exposure,a.terrainDepth,a.terrainDamping};
	const float y[]={b.offshore.height,b.offshore.dx,b.offshore.dy,b.exposure,b.terrainDepth,b.terrainDamping};
	for (int i=0;i<6;++i) { uint32_t xx,yy;std::memcpy(&xx,&x[i],4);std::memcpy(&yy,&y[i],4);if (xx!=yy) return false; }
	return true;
}
}

TEST(water_static_sample_cache_reuses_exact_fields_and_keys_coordinates_order_and_level)
{
	const uint8_t field[9]={0,40,100,70,150,220,90,190,255};
	for (int change=0;change<4;++change) {
		WaterStaticSampleCache::Cache cache;int computed=0;
		const std::array<float,3> points[2]={{{1.25f,2.5f,7}},{{8,6,9}}};
		auto read=[&](const std::array<float,3> &p,bool expectedReuse) {
			const auto reference=staticCacheReference(field,p[0],p[1],p[2]);bool reused=!expectedReuse;
			auto value=cache.Get(p[0],p[1],p[2],[&]() { ++computed;return reference; },&reused);
			CHECK(reused==expectedReuse && staticCacheSameSample(value,reference));
			value.offshore.height=-100;value.terrainDamping=-100; // Returned copies are independently editable.
		};
		cache.BeginRender();for (const auto &p:points) read(p,false);CHECK(computed==2 && cache.Size()==2);
		for (int frame=0;frame<3;++frame) { cache.BeginRender();for (const auto &p:points) read(p,true); }
		CHECK(computed==2);
		auto changed=points[0];
		if (change<3) changed[change]=std::nextafter(changed[change],INFINITY);
		cache.BeginRender();
		if (change==3) { read(points[1],false);read(points[0],false);CHECK(computed==4); }
		else { read(changed,false);read(points[1],true);CHECK(computed==3); }
	}
}

TEST(water_static_sample_cache_clear_refreshes_changed_terrain_and_optical_context)
{
	uint8_t field[9]={0,40,100,70,150,220,90,190,255};WaterStaticSampleCache::Cache cache;
	int computed=0;float terrainOffset=0;auto compute=[&]() { ++computed;return staticCacheReference(field,8,6,7,terrainOffset); };
	bool reused=true;const auto original=cache.Get(8,6,7,compute,&reused);CHECK(!reused && computed==1);
	cache.BeginRender();CHECK(staticCacheSameSample(cache.Get(8,6,7,compute,&reused),original));CHECK(reused && computed==1);
	field[0]=255;field[1]=0;terrainOffset=3;
	const auto changed=staticCacheReference(field,8,6,7,terrainOffset);CHECK(!staticCacheSameSample(original,changed));
	cache.Clear();CHECK(cache.Size()==0);
	CHECK(staticCacheSameSample(cache.Get(8,6,7,compute,&reused),changed));CHECK(!reused && computed==2);
	cache.BeginRender();CHECK(staticCacheSameSample(cache.Get(8,6,7,compute,&reused),changed));CHECK(reused && computed==2);
}

TEST(water_static_sample_cache_bounds_storage_and_bypasses_nonfinite_coordinates)
{
	WaterStaticSampleCache::Cache cache(2);int computed=0;bool reused=true;
	const WaterStaticSampleCache::Sample reference={{.25f,.01f,-.02f},.4f,3,.8f};
	auto compute=[&]() { ++computed;return reference; };
	for (int render=0;render<2;++render) {
		cache.BeginRender();
		for (int i=0;i<3;++i) {
			CHECK(staticCacheSameSample(cache.Get(float(i),1,7,compute,&reused),reference));
			CHECK(reused==(render==1 && i<2));CHECK(cache.Size()<=2);
		}
	}
	CHECK(computed==4 && cache.Size()==2);
	cache.BeginRender();
	const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
	for (auto p:{std::array<float,3>{{nan,1,7}},std::array<float,3>{{0,inf,7}},std::array<float,3>{{0,1,-inf}}}) {
		CHECK(staticCacheSameSample(cache.Get(p[0],p[1],p[2],compute,&reused),reference));CHECK(!reused && cache.Size()==2);
	}
	CHECK(computed==7);
	// Invalid requests do not consume sequence slots; signed zero uses the common world key.
	CHECK(staticCacheSameSample(cache.Get(-0.0f,1,7,compute,&reused),reference));CHECK(reused && computed==7);
	WaterStaticSampleCache::Cache disabled(0);
	for (int repeat=0;repeat<2;++repeat) {
		disabled.BeginRender();CHECK(staticCacheSameSample(disabled.Get(0,1,7,compute,&reused),reference));CHECK(!reused && disabled.Size()==0);
	}
}

namespace {
WaterProjectedMesh::Mesh preparedCacheInput()
{
	using namespace WaterProjectedMesh;Mesh mesh;mesh.cells=17;
	for (Point p:{Point{2,2},Point{8,2},Point{2,8},Point{22,22},Point{25,22},Point{22,25},Point{90,90}}) {
		Vertex v;v.world=p;mesh.vertices.push_back(v);
	}
	mesh.indices={0,1,2,3,4,5};
	for (int i=0;i<2;++i) {
		CentreTransition transition={};transition.analytic=.2+i*.1;
		for (int j=0;j<3;++j) { transition.source[j]={double(i+j),double(i-j)};transition.weight[j]=(j+1)/6.; }
		mesh.transitions.push_back(transition);
		BoundaryCollar collar={};collar.count=4;collar.fade=.6+i*.1;
		for (int j=0;j<4;++j) { collar.edge[j]=j;collar.parameter[j]=.1*j;collar.weight[j]=.1*(j+1); }
		mesh.collars.push_back(collar);
	}
	return mesh;
}
std::vector<uint8_t> preparedCacheTerrain()
{
	std::vector<uint8_t> heights;
	for (int y=0;y<4;++y) for (uint8_t h:{uint8_t(0),uint8_t(10),uint8_t(100),uint8_t(100)}) heights.push_back(h);
	return heights;
}
void preparedCachePipeline(WaterProjectedMesh::Mesh &mesh,const std::vector<uint8_t> &heights,float level,bool cull,bool cage)
{
	if (cull) CHECK(WaterTerrainCulling::CullDryTriangles(mesh,heights.data(),4,4,0,10,1,level,0,0));
	if (cage) CHECK(WaterWaveCageMesh::ClipRestWaterline(mesh,[&](WaterProjectedMesh::Point p) { return level-p.x; },
		[](WaterProjectedMesh::Point) { return true; }));
	CHECK(WaterTerrainCulling::CompactVertices(mesh));
}
size_t preparedCacheBytes(const WaterProjectedMesh::Mesh &mesh)
{
	return mesh.vertices.size()*sizeof(WaterProjectedMesh::Vertex)+mesh.indices.size()*sizeof(uint16_t)+
		mesh.transitions.size()*sizeof(WaterProjectedMesh::CentreTransition)+mesh.collars.size()*sizeof(WaterProjectedMesh::BoundaryCollar);
}
}

TEST(water_prepared_mesh_cache_matches_actual_cull_clip_compact_and_isolates_output_edits)
{
	const auto input=preparedCacheInput();const auto terrain=preparedCacheTerrain();auto reference=input;
	preparedCachePipeline(reference,terrain,5,true,true);
	CHECK(reference.vertices.size()==4 && reference.indices.size()==6 && !projectedCacheSameMesh(reference,input));
	WaterPreparedMeshCache::Cache cache;int computed=0;bool reused=true;
	auto compute=[&](WaterProjectedMesh::Mesh &mesh) { ++computed;preparedCachePipeline(mesh,terrain,5,true,true); };
	auto actual=input;cache.Process(actual,5,true,true,compute,&reused);
	CHECK(!reused && computed==1 && projectedCacheSameMesh(actual,reference));
	// Each frame starts with the base/refined input; downstream draw preparation owns its result.
	actual.vertices[0].world.x=-100;actual.indices.clear();actual.collars[0].fade=-1;
	actual.transitions[0].analytic=-1;actual.cells=0;
	for (int frame=0;frame<3;++frame) {
		actual=input;cache.Process(actual,5,true,true,compute,&reused);
		CHECK(reused && computed==1 && projectedCacheSameMesh(actual,reference));
	}
	CHECK(cache.CachedBytes()==preparedCacheBytes(input)+preparedCacheBytes(reference));
}

TEST(water_prepared_mesh_cache_keys_every_semantic_field_without_uninitialized_collar_tails)
{
	using namespace WaterProjectedMesh;const Mesh input=preparedCacheInput();
	auto changedMisses=[&](Mesh changed,float level=5,bool cull=false,bool cage=false) {
		WaterPreparedMeshCache::Cache cache;int computed=0;bool reused=true;
		auto compute=[&](Mesh &) { ++computed; };Mesh initial=input;cache.Process(initial,5,false,false,compute);
		Mesh actual=changed;cache.Process(actual,level,cull,cage,compute,&reused);
		CHECK(!reused && computed==2 && projectedCacheSameMesh(actual,changed));
		actual=changed;cache.Process(actual,level,cull,cage,compute,&reused);
		CHECK(reused && computed==2 && projectedCacheSameMesh(actual,changed));
	};
	for (size_t i=0;i<input.vertices.size();++i) for (int field=0;field<8;++field) {
		Mesh changed=input;auto &v=changed.vertices[i];
		switch (field) {
			case 0: v.world.x=std::nextafter(v.world.x,INFINITY);break;
			case 1: v.world.y=std::nextafter(v.world.y,INFINITY);break;
			case 2: v.boundary=0;break;
			case 3: v.edgeParameter=std::nextafter(v.edgeParameter,INFINITY);break;
			case 4: v.knot=0;break;
			case 5: v.transition=0;break;
			case 6: v.collar=0;break;
			case 7: v.lattice=0;break;
		}
		changedMisses(changed);
	}
	for (size_t i=0;i<input.transitions.size();++i) for (int field=0;field<10;++field) {
		Mesh changed=input;auto &t=changed.transitions[i];
		double &value=field==9?t.analytic:field%3==0?t.source[field/3].x:field%3==1?t.source[field/3].y:t.weight[field/3];
		value=std::nextafter(value,INFINITY);changedMisses(changed);
	}
	for (size_t i=0;i<input.collars.size();++i) for (int field=0;field<14;++field) {
		Mesh changed=input;auto &c=changed.collars[i];
		if (field==0) --c.count;
		else if (field==1) c.fade=std::nextafter(c.fade,INFINITY);
		else {
			const int slot=(field-2)/3,member=(field-2)%3;
			if (!member) c.edge[slot]=(c.edge[slot]+1)%4;
			else { double &value=member==1?c.parameter[slot]:c.weight[slot];value=std::nextafter(value,INFINITY); }
		}
		changedMisses(changed);
	}
	for (size_t i=0;i<input.indices.size();++i) { Mesh changed=input;changed.indices[i]=(changed.indices[i]+1)%input.vertices.size();changedMisses(changed); }
	for (int field=0;field<9;++field) {
		Mesh changed=input;
		switch (field) {
			case 0: ++changed.cells;break;
			case 1: changed.vertices.pop_back();break;
			case 2: changed.vertices.push_back(input.vertices.back());break;
			case 3: changed.transitions.pop_back();break;
			case 4: changed.transitions.push_back(input.transitions.back());break;
			case 5: changed.collars.pop_back();break;
			case 6: changed.collars.push_back(input.collars.back());break;
			case 7: changed.indices.resize(3);break;
			case 8: std::swap(changed.indices[0],changed.indices[1]);break;
		}
		changedMisses(changed);
	}
	changedMisses(input,std::nextafter(5.f,INFINITY));changedMisses(input,5,true,false);changedMisses(input,5,false,true);
	WaterPreparedMeshCache::Cache cache;int computed=0;auto compute=[&](Mesh &) { ++computed; };
	Mesh initial=input;initial.collars[0].count=1;cache.Process(initial,0,false,false,compute);
	Mesh same=initial;for (int i=1;i<4;++i) { same.collars[0].edge[i]=100;same.collars[0].parameter[i]=NAN;same.collars[0].weight[i]=NAN; }
	bool reused=false;cache.Process(same,0,false,false,compute,&reused);CHECK(reused && computed==1);
	Mesh signedZero=initial;cache.Process(signedZero,-0.0f,false,false,compute,&reused);CHECK(!reused && computed==2);
}

TEST(water_prepared_mesh_cache_clear_refreshes_terrain_and_bounds_bypasses_preserve_entry)
{
	using namespace WaterProjectedMesh;const auto input=preparedCacheInput();auto terrain=preparedCacheTerrain();
	WaterPreparedMeshCache::Cache cache;int computed=0;bool reused=true;
	auto compute=[&](Mesh &mesh) { ++computed;preparedCachePipeline(mesh,terrain,5,true,false); };
	Mesh original=input;cache.Process(original,5,true,false,compute);CHECK(computed==1 && original.vertices.size()==3);
	std::fill(terrain.begin(),terrain.end(),0);cache.Clear();CHECK(cache.CachedBytes()==0);
	Mesh changed=input;cache.Process(changed,5,true,false,compute,&reused);
	CHECK(!reused && computed==2 && changed.vertices.size()==6 && !projectedCacheSameMesh(original,changed));
	const size_t bound=preparedCacheBytes(input)*2;WaterPreparedMeshCache::Cache bounded(bound);
	int calls=0;auto identity=[&](Mesh &) { ++calls; };Mesh actual=input;bounded.Process(actual,5,false,false,identity);
	CHECK(calls==1 && bounded.CachedBytes()<=bound);const size_t stored=bounded.CachedBytes();
	for (int invalid=0;invalid<6;++invalid) {
		Mesh request=input;float level=5;
		if (invalid==0) request.vertices.resize(1000);
		if (invalid==1) request.indices[0]=65535;
		if (invalid==2) request.collars[0].count=5;
		if (invalid==3) request.vertices[0].world.x=NAN;
		if (invalid==4) level=INFINITY;
		if (invalid==5) request.indices.pop_back();
		const int before=calls;bounded.Process(request,level,false,false,identity,&reused);
		CHECK(!reused && calls==before+1 && bounded.CachedBytes()==stored);
		actual=input;bounded.Process(actual,5,false,false,identity,&reused);CHECK(reused && calls==before+1);
	}
	for (bool invalidOutput:{false,true}) {
		Mesh request=input;++request.cells;const int before=calls;
		bounded.Process(request,5,false,false,[&](Mesh &mesh) { ++calls;if (invalidOutput) mesh.collars[0].count=-1;else mesh.vertices.resize(1000); },&reused);
		CHECK(!reused && calls==before+1 && bounded.CachedBytes()==stored);
		actual=input;bounded.Process(actual,5,false,false,identity,&reused);CHECK(reused && calls==before+1);
	}
	WaterPreparedMeshCache::Cache disabled(0);actual=input;disabled.Process(actual,5,false,false,identity,&reused);
	CHECK(!reused && disabled.CachedBytes()==0 && projectedCacheSameMesh(actual,input));
}

TEST(water_prepared_mesh_cache_keeps_one_entry_per_patch_and_evicts_fifo)
{
	using namespace WaterProjectedMesh;const auto input=preparedCacheInput();
	int computed=0;bool reused=true;auto compute=[&](Mesh &) { ++computed; };
	std::vector<Mesh> patches;
	for (int patch=0;patch<3;++patch) { Mesh changed=input;changed.cells+=patch;patches.push_back(changed); }
	WaterPreparedMeshCache::Cache cache(32u*1024u*1024u,2);
	// Two visible patches alternate every frame; neither may evict the other.
	for (int frame=0;frame<3;++frame) for (int patch=0;patch<2;++patch) {
		Mesh actual=patches[patch];cache.Process(actual,5,false,false,compute,&reused);
		CHECK(reused==(frame>0) && projectedCacheSameMesh(actual,patches[patch]));
	}
	CHECK(computed==2 && cache.EntryCount()==2);
	Mesh third=patches[2];cache.Process(third,5,false,false,compute,&reused);
	CHECK(!reused && computed==3 && cache.EntryCount()==2);
	Mesh oldest=patches[0];cache.Process(oldest,5,false,false,compute,&reused);CHECK(!reused);
	Mesh newest=patches[2];cache.Process(newest,5,false,false,compute,&reused);CHECK(reused);
	WaterPreparedMeshCache::Cache disabled(32u*1024u*1024u,0);
	for (int repeat=0;repeat<2;++repeat) {
		Mesh actual=input;disabled.Process(actual,5,false,false,compute,&reused);
		CHECK(!reused && disabled.EntryCount()==0 && disabled.CachedBytes()==0);
	}
}

#include "WaterPreparedViewTest.h"

#include "WaterShorePreparedTest.h"
TEST(water_shore_prepared_topology_matches_frozen_live_install_phase_and_history)
{
	WaterShorePreparedTest::Check();
}

namespace {
struct ShoreCountingSupport {
	unsigned mask = 15;
	mutable size_t lookups = 0, allows = 0;
	struct Stencil {
		const ShoreCountingSupport *owner;
		double x, y;
		bool Allows(unsigned dx, unsigned dy) const {
			++owner->allows;
			return (owner->mask & (1u << (dy * 2 + dx))) && (dx ? x > .13 : x < .87) && (dy ? y > .17 : y < .83);
		}
	};
	Stencil At(int32_t, int32_t, double, double x, double y) const {
		++lookups;
		return {this, x, y};
	}
};
}
TEST(water_shore_raster_lookup_reuses_only_static_work_and_invalidates_dependencies) {
	using namespace WaterShoreHistory;
	Topology topology;
	auto cells = shoreFourCorners();
	cells.push_back({0, 0, 20, 4});
	CHECK(topology.Build({}, cells) == BuildResult::Ready);
	std::vector<State> states(topology.Cells().size(), {1, 2});
	Mapping mapping = {-2.5, -2.5, 10, 10, 40, 32};
	ShoreCountingSupport support;
	RasterLookup lookup;
	uint64_t revision = 1;
	struct Material {
		std::vector<State> inputs;
		std::vector<double> coordinates;
		double operator()(const State &s, double x, double y, double time) {
			inputs.push_back(s);
			coordinates.insert(coordinates.end(), {x, y, time});
			return -std::expm1(-s.density) * (.6 + .2 * std::sin(x * .3 + y * .7 + time + s.ageMoment / (s.density
				+ .01)));
		}
	};
	auto compare = [&](double level, double time) {
		std::vector<uint32_t> seed(size_t(mapping.widthPixels) * mapping.heightPixels);
		for (size_t i = 0; i < seed.size(); ++i)
			seed[i] = 0xff000000u | ((uint32_t(i) * 2654435761u) & 0xffffffu);
		auto expected = seed, actual = seed;
		Material a, b;
		CHECK(RasterizeAlpha(topology, states, mapping, level, time, a, expected, support));
		const auto before = support.lookups, allowBefore = support.allows, builds = lookup.BuildCount();
		CHECK(RasterizeAlphaCached(topology, states, mapping, level, time, b, actual, support, lookup, revision));
		CHECK(expected == actual);
		CHECK(a.coordinates == b.coordinates);
		CHECK(a.inputs.size() == b.inputs.size());
		for (size_t i = 0; i < a.inputs.size() && i < b.inputs.size(); ++i) {
			CHECK(a.inputs[i].density == b.inputs[i].density);
			CHECK(a.inputs[i].ageMoment == b.inputs[i].ageMoment);
		}
		if (lookup.BuildCount() == builds) {
			CHECK(support.lookups == before);
			CHECK(support.allows == allowBefore);
		}
		CHECK(lookup.SampleCount() <= RasterLookup::MaximumPixels &&lookup.TileCount() <= lookup.SampleCount());
		CHECK(lookup.MemoryBytes() <= sizeof(RasterLookup) + 13 * RasterLookup::MaximumPixels);
	};
	compare(10, 0);
	CHECK(lookup.BuildCount() == 1);
	for (unsigned frame = 1; frame <= 12; ++frame) {
		for (size_t i = 0; i < states.size(); ++i)
			states[i] = {frame % 3 ? double(i + frame) * .13 : 0, double(i + 2 * frame) * .7};
		compare(10, frame * .137);
		CHECK(lookup.BuildCount() == 1);
	}
	mapping.originX += .125;
	compare(10, 2);
	CHECK(lookup.BuildCount() == 2);
	mapping.originY -= .25;
	compare(10, 2);
	CHECK(lookup.BuildCount() == 3);
	mapping.widthWorld += .75;
	compare(10, 2);
	CHECK(lookup.BuildCount() == 4);
	mapping.heightWorld += .5;
	compare(10, 2);
	CHECK(lookup.BuildCount() == 5);
	mapping.widthPixels = 43;
	compare(10, 2);
	CHECK(lookup.BuildCount() == 6);
	mapping.heightPixels = 37;
	compare(10, 2);
	CHECK(lookup.BuildCount() == 7);
	compare(20, 2);
	CHECK(lookup.BuildCount() == 8);
	support.mask = 0;
	++revision;
	compare(20, 2);
	CHECK(lookup.BuildCount() == 9);
	cells.push_back({1, 0, 20, 5});
	CHECK(topology.Build({.5, -.5, 5}, cells) == BuildResult::Ready);
	states.assign(topology.Cells().size(), {1.3, .7});
	support.mask = 15;
	++revision;
	compare(20, 3);
	CHECK(lookup.BuildCount() == 10);
	ShoreCountingSupport other;
	auto material = [](const State &s, double, double, double) {
		return s.density;
	};
	std::vector<uint32_t> pixels(size_t(mapping.widthPixels) * mapping.heightPixels, 0xaabbccddu);
	CHECK(RasterizeAlphaCached(topology, states, mapping, 20, 3, material, pixels, other, lookup, revision));
	CHECK(lookup.BuildCount() == 11); // support object identity is part of the key
	const auto saved = pixels;
	const auto builds = lookup.BuildCount();
	Mapping invalid = mapping;
	invalid.widthPixels = 513;
	CHECK(!RasterizeAlphaCached(topology, states, invalid, 20, 3, material, pixels, other, lookup, revision));
	CHECK(!RasterizeAlphaCached(topology, states, mapping, 20, NAN, material, pixels, other, lookup, revision));
	CHECK(!RasterizeAlphaCached(topology, states, mapping, 20, 3, material, pixels, other, lookup, 0));
	CHECK(pixels == saved && lookup.BuildCount() == builds);
	lookup.Clear();
	CHECK(lookup.BuildCount() == 0 && lookup.SampleCount() == 0 && lookup.TileCount() == 0);
}
TEST(water_shore_raster_prepared_spatial_breakup_preserves_live_coverage_bytes) {
	using namespace WaterShoreHistory;
	const auto cells = shoreFourCorners();
	Topology topology;
	CHECK(topology.Build({}, cells) == BuildResult::Ready);
	WaterShoreSupport::Domain support;
	auto flat = [](double, double) {
		return 0.f;
	};
	CHECK(support.Build({}, cells, 3, 3, {shoreRect(0, 0, 2, 5), shoreRect(3, 0, 5, 5)}, flat));
	RasterLookup lookup;
	uint64_t prepared = 0;
	size_t preparations = 0;
	std::vector<float> breakup;
	for (unsigned camera = 0; camera < 4; ++camera) {
		Mapping mapping = {-1 + camera * .13, -.75 + camera * .21, 7.3, 6.7, 61 + camera, 57 - camera};
		for (unsigned frame = 0; frame < 16; ++frame) {
			std::vector<State> states;
			for (unsigned i = 0; i < 4; ++i)
				states.push_back({frame % 4 ? (.05 + double(frame + i) * .17) : 0, double(frame + 2 * i) * .31});
			const double time = frame * .137 + camera;
			std::vector<uint32_t> seed(size_t(mapping.widthPixels) * mapping.heightPixels, 0xa3abcdefu),
				expected = seed, actual = seed;
			auto coverage = WaterShoreSource::Coverage;
			CHECK(RasterizeAlpha(topology, states, mapping, 10, time, coverage, expected, support));
			CHECK(lookup.Prepare(topology, mapping, 10, 1, support));
			if (prepared != lookup.BuildCount()) {
				breakup.assign(seed.size(), 0);
				lookup.ForEachSample([&](size_t n, double x, double y) {
					const double value = WaterShoreSource::SpatialBreakup(x, y);
					breakup[n] = float(value);
					CHECK(double(breakup[n]) == value);
					++preparations;
				});
				prepared = lookup.BuildCount();
			}
			const auto before = preparations;
			auto material = [&](const State &s, double, double, double, size_t n) {
				return WaterShoreSource::CoverageFromBreakup(s, breakup[n]);
			};
			CHECK(lookup.Rasterize(states, time, material, actual));
			CHECK(actual == expected);
			CHECK(preparations == before && lookup.BuildCount() == camera + 1);
		}
	}
	CHECK(preparations > 1000);
}


#include "WaterWaveCageCopyTest.h"
TEST(water_wave_cage_copy_elision_matches_frozen_noops_and_crossing_chains)
{
	WaterWaveCageCopyTest::NoopsAndChains();
}
TEST(water_wave_cage_copy_elision_matches_frozen_crossings_and_vertex_budgets)
{
	WaterWaveCageCopyTest::CrossingsAndBudgets();
}
TEST(water_wave_cage_copy_elision_preserves_frozen_invalid_input_rollback)
{
	WaterWaveCageCopyTest::InvalidRollback();
}



#include "WaterTerrainProofCacheTest.h"
TEST(water_terrain_proof_cache_matches_frozen_threshold_pond_and_boundary_outputs)
{
	WaterTerrainProofCacheTest::ExtremesAndThresholds();
}
TEST(water_terrain_proof_cache_matches_frozen_moving_projected_cull_clip_compact_pipeline)
{
	WaterTerrainProofCacheTest::MovingProjectedPipeline();
}
TEST(water_terrain_proof_cache_preserves_sample_budgets_and_65536_vertex_indices)
{
	WaterTerrainProofCacheTest::BudgetAndIndexLimits();
}
TEST(water_terrain_compaction_matches_frozen_metadata_order_and_invalid_rollback)
{
	WaterTerrainProofCacheTest::InvalidAndCompaction();
}
TEST(water_terrain_proof_cache_clear_cutoff_reuse_and_capacity_are_exact)
{
	WaterTerrainProofCacheTest::LifetimeAndCapacity();
}
TEST(water_terrain_proof_cache_rectangle_queries_match_encoded_predicate_at_thresholds)
{
	WaterTerrainProofCacheTest::RectangleQueries();
}



#include "WaterShoreSourcePerfTest.h"
#include "WaterProjectedSampleCacheTest.h"

#include "Platform/WaterRenderCaches.h"
TEST(water_render_cache_preferences_preserve_loading_data_and_invalidate_every_setting)
{
	using namespace WaterProjectedMesh;
	WaterRenderCaches::Cache<float> cache;
	WaterRenderCaches::Settings settings;
	const Mesh input=preparedCacheInput();
	const Point corners[4]={{-95,-170},{81,-110},{120,92},{-64,170}};
	const auto projection=testWaterProjection();
	const auto knots=projectedCacheKnots(corners);
	const auto key=SampleKey(1,2,3);
	std::vector<uint8_t> heights(81,30);
	// Loading may fill static caches before the first view is known.
	uint8_t field[9]={0,40,100,70,150,220,90,190,255};
	auto populate=[&]() {
		Mesh mesh;
		CHECK(cache.topology.Build(corners,projection,{32,24},mesh,60000,knots));
		mesh=input;cache.prepared.Process(mesh,5,false,false,[](Mesh &) {});
		cache.staticSamples.BeginRender();
		cache.staticSamples.Get(1,2,3,[&]() { return staticCacheReference(field,1,2,3); });
		CHECK(cache.terrain.Prewarm(heights.data(),9,9,1,10));
		cache.samples.Store(key,7);
	};
	populate();
	cache.BeginRender(settings);
	auto retained=[&]() {
		CHECK(cache.topology.EntryCount()==1 && cache.prepared.EntryCount()==1);
		CHECK(cache.staticSamples.Size()==1 && cache.terrain.EntryCount()==1);
	};
	retained();
	CHECK(!cache.samples.Find(key)); // animated payloads never survive a render
	for (int change=0;change<8;++change) {
		populate();cache.BeginRender(settings);retained();
		auto next=settings;
		switch (change) {
		case 0: ++next.reflectionMode;break;
		case 1: ++next.refraction;break;
		case 2: --next.reflectionDetail;break;
		case 3: next.requestedFftResolution=128;break;
		case 4: next.activeFftResolution=128;break;
		case 5: next.sceneReflections=false;break;
		case 6: next.width=1920;break;
		case 7: next.height=1080;break;
		}
		cache.BeginRender(next);
		CHECK(cache.topology.EntryCount()==0 && cache.prepared.EntryCount()==0);
		CHECK(cache.staticSamples.Size()==0 && cache.terrain.EntryCount()==0 && !cache.samples.Find(key));
		cache.BeginRender(settings);
	}
	populate();cache.Clear();
	CHECK(cache.topology.EntryCount()==0 && cache.prepared.EntryCount()==0);
	CHECK(cache.staticSamples.Size()==0 && cache.terrain.EntryCount()==0 && !cache.samples.Find(key));
}

TEST(water_static_terrain_snapshot_preserves_original_render_map_interpolation)
{
	// Frozen WaterTerrainDepth argument order and WorldHeightMapData bounds.
	const int width=7,height=5;
	std::vector<uint8_t> heights(width*height);
	for (size_t i=0;i<heights.size();++i) heights[i]=uint8_t(i*37);
	auto original=[&](float x,float y,float level,int border,float spacing,float scale) {
		const float mx=clamp(x/spacing+border,0.0f,float(width)-1.001f);
		const float my=clamp(y/spacing+border,0.0f,float(height)-1.001f);
		const int ix=int(mx),iy=int(my);const float fx=mx-ix,fy=my-iy;
		auto getHeight=[&](int column,int row) {
			const int index=row*width+column;
			return index>=0 && index<int(heights.size())?heights[index]:uint8_t(0);
		};
		const float a=getHeight(ix,iy)*(1-fx)+getHeight(ix+1,iy)*fx;
		const float b=getHeight(ix,iy+1)*(1-fx)+getHeight(ix+1,iy+1)*fx;
		return level-(a*(1-fy)+b*fy)*scale;
	};
	for (int border:{0,1}) for (float spacing:{1.f,10.f}) for (float scale:{.625f,1.f})
		for (float level:{-0.f,0.f,7.25f,100.f})
			for (float x:{-100.f,0.f,.123f,3.f,49.99f,60.f,10000.f})
				for (float y:{-100.f,0.f,.456f,2.f,29.99f,40.f,10000.f}) {
					const float expected=original(x,y,level,border,spacing,scale);
					const float actual=WaterStaticSampleCache::TerrainDepth(heights.data(),width,height,border,spacing,scale,x,y,level);
					CHECK(std::memcmp(&actual,&expected,sizeof(actual))==0);
				}
	CHECK(WaterStaticSampleCache::TerrainDepth(nullptr,0,0,0,10,1,0,0,7)==0);
}
