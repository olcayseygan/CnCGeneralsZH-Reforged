// Frozen before the spatial-rate/coverage cache; keep the original formulas/order.
/* Fixed-world, bounded coastal source contexts. No camera, vessel or gameplay state. */
#pragma once
#include "Platform/WaterShoreFoam.h"
#include "Platform/WaterShoreHistory.h"
#include "Platform/WaterShoreSupport.h"
#include "Platform/WaterOpticalContext.h"
#include <array>

namespace WaterShoreSourceReference {
struct Level {double level=0;const WaterOpticalContext::Field *field=nullptr;};
struct SampleContext {
    WaterShoreFoam::Context coast;
    WaterShoreFoam::FrontWeights frontWeights;
    std::array<float,WaterShoreFoam::ModeCount> sine{},cosine{};
};
struct Context : SampleContext {std::array<SampleContext,4> quadrature;};
using Interval=WaterOpticalContext::Interval;
// Exact scanline union, including authored edges. Every accepted diffusion segment
// lies entirely inside a union interval, rather than testing only its endpoints.
using WaterOpticalContext::Intervals;
inline bool Covered(const std::vector<Interval>&spans,double a,double b) {
    const auto at=std::upper_bound(spans.begin(),spans.end(),a,
        [](double x,const Interval&i){return x<i.first;});
    return at!=spans.begin()&&(at-1)->second>=b;
}
inline float Height(const uint8_t*heights,int width,int height,double x,double y,double scale) {
    x=WaterShoreHistory::Clamp(x,0,width-1.000001);y=WaterShoreHistory::Clamp(y,0,height-1.000001);
    const int ix=int(x),iy=int(y);const double fx=x-ix,fy=y-iy;const size_t n=size_t(iy)*width+ix;
    return float(((heights[n]*(1-fx)+heights[n+1]*fx)*(1-fy)
        +(heights[n+width]*(1-fx)+heights[n+width+1]*fx)*fy)*scale);
}
inline float FieldSample(const std::vector<uint8_t>&field,int width,int x,int y,float fx,float fy) {
    const size_t n=size_t(y)*width+x;
    return ((field[n]*(1-fx)+field[n+1]*fx)*(1-fy)
        +(field[n+width]*(1-fx)+field[n+width+1]*fx)*fy)/255.0f;
}
class Batch {
    WaterShoreHistory::Grid grid;
    std::vector<WaterShoreHistory::Cell> cells;
    std::vector<Context> contexts;
    WaterShoreSupport::Domain support;
public:
    WaterShoreHistory::BuildResult Build(const uint8_t*heights,int width,int height,int border,
            double spacing,double heightScale,const std::vector<WaterOpticalContext::Polygon>&polygons,
            const std::vector<Level>&levels) {
        using namespace WaterShoreHistory;
        if(!heights||width<2||height<2||width>WaterOpticalContext::MaxDimension||height>WaterOpticalContext::MaxDimension
            ||size_t(width)*height>WaterOpticalContext::MaxCells||border<0||border>(width-2)/2||border>(height-2)/2
            ||!std::isfinite(spacing)||spacing<2||spacing>2048||!std::isfinite(heightScale)||heightScale<=0)
            return BuildResult::Invalid;
        if(levels.size()>MaxLevels)return BuildResult::TooManyLevels;
        size_t points=0;
        for(const auto&polygon:polygons) {
            if(polygon.points.size()>WaterOpticalContext::MaxPoints-points)return BuildResult::Invalid;
            points+=polygon.points.size();
            for(const auto&p:polygon.points)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return BuildResult::Invalid;
        }
        Batch next;next.grid={-border*spacing,-border*spacing,spacing*.5};
        std::vector<double> seen;
        for(const Level&entry:levels) {
            if(!entry.field||!std::isfinite(entry.level))return BuildResult::Invalid;
            if(std::find(seen.begin(),seen.end(),entry.level)!=seen.end())return BuildResult::Invalid;
            seen.push_back(entry.level);
            const auto&f=*entry.field;const size_t count=size_t(width)*height;
            if(f.ocean.size()!=count||f.shore.size()!=count||f.exposure.size()!=count||f.coast.size()!=count)return BuildResult::Invalid;
            const size_t first=next.cells.size();
            auto sample=[&](double px,double py) {
                SampleContext context;
                if(px<0||py<0||px>=width-1||py>=height-1)return context;
                const int ix=int(px),iy=int(py);const float fx=float(px-ix),fy=float(py-iy);
                const float terrain=Height(heights,width,height,px,py,heightScale);
                if(!(terrain<entry.level)||FieldSample(f.ocean,width,ix,iy,fx,fy)>0)return context;
                const double offset=5.0/spacing;
                const float gx=(Height(heights,width,height,px+offset,py,heightScale)
                    -Height(heights,width,height,px-offset,py,heightScale))*.1f;
                const float gy=(Height(heights,width,height,px,py+offset,heightScale)
                    -Height(heights,width,height,px,py-offset,heightScale))*.1f;
                context.coast.depth=float(entry.level)-terrain;
                context.coast.nx=-gx;context.coast.ny=-gy;context.coast.slope=std::hypot(gx,gy);
                context.coast.shore=(std::min)(FieldSample(f.shore,width,ix,iy,fx,fy)*100,
                    context.coast.depth/(std::max)(context.coast.slope,.01f));
                context.coast.support=FieldSample(f.coast,width,ix,iy,fx,fy);
                context.coast.exposure=FieldSample(f.exposure,width,ix,iy,fx,fy);
                context.frontWeights=WaterShoreFoam::IncomingWeights(context.coast.nx,context.coast.ny);
                const double worldX=next.grid.originX+px*spacing,worldY=next.grid.originY+py*spacing;
                for(unsigned mode=0;mode<WaterShoreFoam::ModeCount;++mode) {
                    const auto&m=WaterShoreFoam::Modes[mode];
                    const float phase=float(worldX)*m.kx+float(worldY)*m.ky+m.phase;
                    context.sine[mode]=std::sin(phase);context.cosine[mode]=std::cos(phase);
                }
                return context;
            };
            for(int y=0;y<2*(height-1);++y) {
                const double worldY=next.grid.originY+y*next.grid.spacing;
                const auto row=Intervals(polygons,entry.level,worldY,true);
                if(row.empty())continue;
                const std::array<std::vector<Interval>,2> quadratureRows={
                    Intervals(polygons,entry.level,worldY-next.grid.spacing*.25,true),
                    Intervals(polygons,entry.level,worldY+next.grid.spacing*.25,true)};
                for(int x=0;x<2*(width-1);++x) {
                    const int ix=x/2,iy=y/2;const float fx=(x%2)*.5f,fy=(y%2)*.5f;
                    // Include a one-terrain-cell margin around support for wet-only spreading.
                    bool nearCoast=false;
                    for(int yy=(std::max)(0,iy-1);yy<=(std::min)(height-1,iy+2)&&!nearCoast;++yy)
                        for(int xx=(std::max)(0,ix-1);xx<=(std::min)(width-1,ix+2);++xx)
                            nearCoast=nearCoast||f.coast[size_t(yy)*width+xx]!=0;
                    if(!nearCoast||FieldSample(f.ocean,width,ix,iy,fx,fy)>0)continue;
                    const double worldX=next.grid.originX+x*next.grid.spacing;
                    if(!Covered(row,worldX,worldX))continue;
                    const float terrain=Height(heights,width,height,x*.5,y*.5,heightScale);
                    if(!(terrain<entry.level))continue;
                    if(next.cells.size()>=MaxCells)return BuildResult::TooManyCells;
                    Context context;static_cast<SampleContext&>(context)=sample(x*.5,y*.5);
                    // Fixed2x2 area quadrature captures narrow wet source bands and the
                    // nonlinear crest/incoming gates; dry/outside subnodes contribute zero.
                    // It does not expand the wet graph or connect across polygon gaps.
                    for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx) {
                        const double px=x*.5+(sx?.125:-.125),py=y*.5+(sy?.125:-.125);
                        const double qx=next.grid.originX+px*spacing;
                        if(Covered(quadratureRows[sy],qx,qx))context.quadrature[sy*2+sx]=sample(px,py);
                    }
                    const uint8_t connections=Covered(row,worldX,worldX+next.grid.spacing)?1:0;
                    next.cells.push_back({x,y,entry.level,uint32_t(next.contexts.size()),connections});
                    next.contexts.push_back(context);
                }
            }
            // Cache one vertical scanline at a time, using bounded cell indices rather than
            // scanning polygon edges once for every history node.
            std::vector<size_t> columnOrder;
            for(size_t i=first;i<next.cells.size();++i)columnOrder.push_back(i);
            std::sort(columnOrder.begin(),columnOrder.end(),[&](size_t a,size_t b){return next.cells[a].x<next.cells[b].x;});
            int previous=-1;std::vector<Interval> column;
            for(size_t i:columnOrder) {
                auto&cell=next.cells[i];
                if(cell.x!=previous){previous=cell.x;column=Intervals(polygons,entry.level,next.grid.originX+cell.x*next.grid.spacing,false);}
                const double worldY=next.grid.originY+cell.y*next.grid.spacing;
                if(Covered(column,worldY,worldY+next.grid.spacing))cell.connections|=2;
            }
        }
        auto terrain=[&](double x,double y){return Height(heights,width,height,x,y,heightScale);};
        if(!next.support.Build(next.grid,next.cells,width,height,polygons,terrain))return BuildResult::Invalid;
        *this=std::move(next);return BuildResult::Ready;
    }
    void operator()(double time,const WaterShoreHistory::Cell*nodes,size_t count,double*rates) const {
        std::array<float,WaterShoreFoam::ModeCount> sine{},cosine{};
        for(unsigned mode=0;mode<WaterShoreFoam::ModeCount;++mode) {
            const float phase=-float(time)*WaterShoreFoam::Modes[mode].omega;
            sine[mode]=std::sin(phase);cosine[mode]=std::cos(phase);
        }
        for(size_t i=0;i<count;++i) {
            rates[i]=0;if(nodes[i].sourceIndex>=contexts.size())continue;
            const Context&node=contexts[nodes[i].sourceIndex];
            for(const SampleContext&context:node.quadrature) {
                if(context.coast.support<=0)continue;
                WaterShoreFoam::Wave wave;float front=0;
                for(unsigned mode=0;mode<WaterShoreFoam::ModeCount;++mode) {
                    const auto&m=WaterShoreFoam::Modes[mode];
                    const float s=context.sine[mode]*cosine[mode]+context.cosine[mode]*sine[mode];
                    const float c=context.cosine[mode]*cosine[mode]-context.sine[mode]*sine[mode];
                    const float derivative=m.amplitude*(c+.26f*(c*c-s*s));
                    wave.height+=m.amplitude*(s+.26f*s*c);wave.dx+=derivative*m.kx;wave.dy+=derivative*m.ky;wave.dt-=derivative*m.omega;
                    front+=context.frontWeights.value[mode]*WaterShoreFoam::FrontKernel(s,c);
                }
                // A phase-locked birth mask only removes old-source support. No new
                // per-cell trigonometry, graph cells, upload work or time state is added.
                rates[i]+=.25*WaterShoreFoam::Rate(context.coast,wave)*WaterShoreFoam::Clamp(front,0,1);
            }
        }
    }
    const WaterShoreHistory::Grid&Grid()const{return grid;}
    const std::vector<WaterShoreHistory::Cell>&Cells()const{return cells;}
    const std::vector<Context>&Contexts()const{return contexts;}
    const WaterShoreSupport::Domain&Support()const{return support;}
    void Clear(){*this=Batch();}
};
// Density and its age moment become a porous, world-anchored surface coverage.
// The material has no dependence on vessel heading, render count or atlas mapping.
inline double Noise(double x,double y) {
    auto hash=[](int64_t x,int64_t y){uint64_t h=uint64_t(x)*0x9e3779b97f4a7c15ULL^uint64_t(y)*0xbf58476d1ce4e5b9ULL;
        h=(h^(h>>30))*0xbf58476d1ce4e5b9ULL;h=(h^(h>>27))*0x94d049bb133111ebULL;return double((h^(h>>31))&0xffffffu)/16777215.0;};
    const int64_t ix=int64_t(std::floor(x)),iy=int64_t(std::floor(y));
    double fx=x-ix,fy=y-iy;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    return (hash(ix,iy)*(1-fx)+hash(ix+1,iy)*fx)*(1-fy)+(hash(ix,iy+1)*(1-fx)+hash(ix+1,iy+1)*fx)*fy;
}
inline double Coverage(const WaterShoreHistory::State&s,double x,double y,double) {
    if(!(s.density>0)||!std::isfinite(s.density)||!std::isfinite(s.ageMoment))return 0;
    const double age=s.ageMoment/s.density;
    const double texture=Noise(x/7,y/7)*.7+Noise(x/2.3,y/2.3)*.3;
    const double breakup=WaterShoreFoam::Smooth(.25f,.7f,float(texture));
    const double aged=WaterShoreFoam::Smooth(.4f,3.0f,float(age));
    const double terminal=1-WaterShoreFoam::Smooth(8,14,float(age));
    return -std::expm1(-s.density)*(1-aged+aged*breakup)*terminal;
}
}
