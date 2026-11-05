// Frozen HEAD 3c1f8285 shore history oracle; only namespace renamed.
/* Render-only, fixed-world coastal foam history. Source physics is supplied by the caller. */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace WaterShoreHistoryFrozenReference {
constexpr size_t MaxCells=65536,MaxLevels=8,MaxEdges=2*MaxCells;
constexpr double Tick=0.1;
constexpr uint64_t MaxCatchupTicks=8,MaxTickIndex=uint64_t(1)<<48;
// Keep absolute double clock resolution well below a fixed tick.
inline bool ValidTime(double time){return std::isfinite(time)&&std::abs(time)<=Tick*double(MaxTickIndex);}
struct Parameters {double lifetime=5,diffusivity=2,maximumRate=16;};
struct Grid {double originX=0,originY=0,spacing=5;};
// The caller only supplies actual wet terrain cells within its bounded coastal domain.
// Domain includes a wet-only spread margin around source cells, not dry validity-mask cells.
struct Cell {
    int32_t x=0,y=0;double level=0;
    uint32_t sourceIndex=0; // opaque index into the caller's immutable, per-revision source context
    uint8_t connections=3; // positive X/Y edges; builder proves each segment stays in the polygon union
};
struct State {double density=0,ageMoment=0;};
struct Edge {uint32_t a=0,b=0;};
inline bool Less(const Cell&a,const Cell&b) {
    if(a.level!=b.level)return a.level<b.level;
    if(a.y!=b.y)return a.y<b.y;
    return a.x<b.x;
}
inline bool SameKey(const Cell&a,const Cell&b){return a.level==b.level&&a.x==b.x&&a.y==b.y;}
inline double Clamp(double x,double low,double high){return (std::max)(low,(std::min)(high,x));}
inline bool ValidGrid(const Grid&g) {
    return std::isfinite(g.originX)&&std::isfinite(g.originY)&&std::isfinite(g.spacing)
        &&g.spacing>=1&&g.spacing<=1024;
}
inline bool ValidParameters(const Parameters&p,const Grid&g) {
    // Four-neighbour diffusion is a convex combination with >=1/2 self weight.
    return std::isfinite(p.lifetime)&&p.lifetime>=.1&&p.lifetime<=3600
        &&std::isfinite(p.diffusivity)&&p.diffusivity>=0&&4*p.diffusivity*Tick/(g.spacing*g.spacing)<=.5
        &&std::isfinite(p.maximumRate)&&p.maximumRate>=0&&p.maximumRate<=1000;
}
struct Decay {
    double q=1,elapsed=0,sourceDensity=0,sourceAge=0;
    static Decay Make(double seconds,double lifetime) {
        Decay d;d.elapsed=seconds;const double x=seconds/lifetime;
        if(x>700){d.q=0;d.sourceDensity=lifetime;d.sourceAge=lifetime*lifetime;return d;}
        d.q=std::exp(-x);const double oneMinus=-std::expm1(-x);
        // Stable integral of age*exp(-age/lifetime); no cancellation at small partial dt.
        const double moment=x<.05?x*x*(.5+x*(-1.0/3+x*(1.0/8+x*(-1.0/30+x*(1.0/144+x*(-1.0/840+x/5760))))))
            :oneMinus-x*d.q;
        d.sourceDensity=lifetime*oneMinus;d.sourceAge=lifetime*lifetime*moment;return d;
    }
    State Apply(const State&s,double rate)const {
        // The early q==0 case also avoids 0*(large elapsed*density) becoming NaN.
        if(q==0)return {rate*sourceDensity,rate*sourceAge};
        return {q*s.density+rate*sourceDensity,q*(s.ageMoment+elapsed*s.density)+rate*sourceAge};
    }
};
enum class BuildResult {Ready,TooManyCells,TooManyLevels,Invalid};
// Cached interpolation ownership. Every raster sample belongs to one half-open grid
// tile; indices retain the original dy/dx summation order and absent nodes stay -1.
struct RasterTile {
    int32_t x=0,y=0;double level=0;
    std::array<int32_t,4> nodes{{-1,-1,-1,-1}};
};
inline bool RasterLess(const RasterTile&a,const RasterTile&b) {
    if(a.level!=b.level)return a.level<b.level;
    if(a.y!=b.y)return a.y<b.y;return a.x<b.x;
}
class Topology {
    Grid grid;std::vector<Cell> cells;std::vector<Edge> edges;
    std::vector<RasterTile> rasterTiles;
    // Fixed open-address lookup: at most 1/2 occupancy, bounded allocations and no frame-time writes.
    std::vector<uint32_t> table;static constexpr uint32_t Empty=~uint32_t(0);
    static uint64_t Mix(uint64_t v){v^=v>>30;v*=0xbf58476d1ce4e5b9ULL;v^=v>>27;v*=0x94d049bb133111ebULL;return v^(v>>31);}
    uint64_t Hash(int32_t x,int32_t y,double level)const {
        // Level is located in a tiny sorted list, avoiding host-endian double byte hashing.
        const auto at=std::lower_bound(levels.begin(),levels.end(),level);
        const uint64_t l=uint64_t(at-levels.begin());
        return Mix(uint64_t(uint32_t(x))|(uint64_t(uint32_t(y))<<32))^Mix(l+0x9e3779b97f4a7c15ULL);
    }
    std::vector<double> levels;
public:
    BuildResult Build(const Grid&inputGrid,const std::vector<Cell>&input) {
        if(input.size()>MaxCells)return BuildResult::TooManyCells;
        if(!ValidGrid(inputGrid))return BuildResult::Invalid;
        Topology next;next.grid=inputGrid;next.cells=input;
        for(Cell&c:next.cells) {
            if(!std::isfinite(c.level)||!std::isfinite(inputGrid.originX+double(c.x)*inputGrid.spacing)
                ||!std::isfinite(inputGrid.originY+double(c.y)*inputGrid.spacing))return BuildResult::Invalid;
            if(c.level==0)c.level=0; // canonical positive zero
        }
        std::sort(next.cells.begin(),next.cells.end(),Less);
        for(size_t i=0;i<next.cells.size();++i) {
            if(i&&SameKey(next.cells[i-1],next.cells[i]))return BuildResult::Invalid;
            if(next.levels.empty()||next.levels.back()!=next.cells[i].level)next.levels.push_back(next.cells[i].level);
        }
        if(next.levels.size()>MaxLevels)return BuildResult::TooManyLevels;
        size_t size=2;while(size<2*next.cells.size())size*=2;
        next.table.assign(size,Empty);next.edges.reserve(2*next.cells.size());
        for(uint32_t i=0;i<next.cells.size();++i) {
            const Cell&c=next.cells[i];size_t slot=next.Hash(c.x,c.y,c.level)&(size-1);
            while(next.table[slot]!=Empty)slot=(slot+1)&(size-1);
            next.table[slot]=i;
        }
        for(uint32_t i=0;i<next.cells.size();++i) {
            const Cell&c=next.cells[i];int neighbour=-1;
            if((c.connections&1)&&c.x<(std::numeric_limits<int32_t>::max)())neighbour=next.Find(c.x+1,c.y,c.level);
            if(neighbour>=0)next.edges.push_back({i,uint32_t(neighbour)});
            if((c.connections&2)&&c.y<(std::numeric_limits<int32_t>::max)())neighbour=next.Find(c.x,c.y+1,c.level);else neighbour=-1;
            if(neighbour>=0)next.edges.push_back({i,uint32_t(neighbour)});
        }
        if(next.edges.size()>MaxEdges)return BuildResult::Invalid;
        // Build at most four contributions per node once per geometry revision, not
        // four hash lookups per atlas texel on every history upload.
        next.rasterTiles.reserve(4*next.cells.size());
        for(uint32_t i=0;i<next.cells.size();++i) {
            const Cell&c=next.cells[i];
            for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx) {
                const int64_t x=int64_t(c.x)-dx,y=int64_t(c.y)-dy;
                // Sample permits floor indices [INT_MIN,INT_MAX), with +1 neighbours.
                if(x<(std::numeric_limits<int32_t>::min)()||y<(std::numeric_limits<int32_t>::min)()
                    ||x>=(std::numeric_limits<int32_t>::max)()||y>=(std::numeric_limits<int32_t>::max)())continue;
                RasterTile tile;tile.x=int32_t(x);tile.y=int32_t(y);tile.level=c.level;tile.nodes[dy*2+dx]=int32_t(i);
                next.rasterTiles.push_back(tile);
            }
        }
        std::sort(next.rasterTiles.begin(),next.rasterTiles.end(),RasterLess);
        size_t kept=0;
        for(size_t read=0;read<next.rasterTiles.size();++read) {
            const RasterTile tile=next.rasterTiles[read];
            if(kept&&next.rasterTiles[kept-1].x==tile.x&&next.rasterTiles[kept-1].y==tile.y&&next.rasterTiles[kept-1].level==tile.level) {
                for(unsigned n=0;n<4;++n)if(tile.nodes[n]>=0)next.rasterTiles[kept-1].nodes[n]=tile.nodes[n];
            } else next.rasterTiles[kept++]=tile;
        }
        next.rasterTiles.resize(kept);
        *this=std::move(next);return BuildResult::Ready;
    }
    int Find(int32_t x,int32_t y,double level)const {
        if(table.empty()||!std::isfinite(level))return -1;
        size_t slot=Hash(x,y,level)&(table.size()-1);
        for(size_t attempts=0;attempts<table.size();++attempts) {
            const uint32_t index=table[slot];if(index==Empty)return -1;
            const Cell&c=cells[index];if(c.x==x&&c.y==y&&c.level==level)return int(index);
            slot=(slot+1)&(table.size()-1);
        }
        return -1;
    }
    bool SameGeometry(const Topology&o)const {
        if(grid.originX!=o.grid.originX||grid.originY!=o.grid.originY||grid.spacing!=o.grid.spacing||cells.size()!=o.cells.size())return false;
        for(size_t i=0;i<cells.size();++i)if(!SameKey(cells[i],o.cells[i])||cells[i].connections!=o.cells[i].connections)return false;
        return true;
    }
    const Grid& Geometry()const{return grid;}
    const std::vector<Cell>& Cells()const{return cells;}
    const std::vector<Edge>& Edges()const{return edges;}
    const std::vector<RasterTile>& RasterTiles()const{return rasterTiles;}
};
// One full operator: exact source/decay/age integration for constant rate over dt, followed
// by conservative reciprocal wet-neighbour diffusion. The latter is first-order in time,
// not claimed to be an exact heat solver. Fixed global ticks make render schedules irrelevant.
inline bool Step(const Topology&topology,const Parameters&parameters,const std::vector<State>&input,
                 const std::vector<double>&rates,double dt,std::vector<State>&decayed,std::vector<State>&output) {
    const size_t count=topology.Cells().size();
    if(input.size()!=count||rates.size()!=count||&input==&decayed||&input==&output||&decayed==&output
        ||!std::isfinite(dt)||dt<0||dt>Tick+1e-12||!ValidParameters(parameters,topology.Geometry()))return false;
    for(const State&s:input)if(!std::isfinite(s.density)||!std::isfinite(s.ageMoment)||s.density<0||s.ageMoment<0)return false;
    const Decay decay=Decay::Make(dt,parameters.lifetime);
    decayed.resize(count);output.resize(count);
    for(size_t i=0;i<count;++i) {
        const double rate=std::isfinite(rates[i])?Clamp(rates[i],0,parameters.maximumRate):0;
        output[i]=decayed[i]=decay.Apply(input[i],rate);
    }
    const double amount=parameters.diffusivity*dt/(topology.Geometry().spacing*topology.Geometry().spacing);
    for(const Edge&e:topology.Edges()) {
        const double density=amount*(decayed[e.b].density-decayed[e.a].density);
        const double age=amount*(decayed[e.b].ageMoment-decayed[e.a].ageMoment);
        output[e.a].density+=density;output[e.b].density-=density;
        output[e.a].ageMoment+=age;output[e.b].ageMoment-=age;
    }
    return true;
}
enum class AdvanceResult {Advanced,Unchanged,Rewound,GapDecayed,Invalid};
class History {
    Topology topology;Parameters parameters;std::vector<State> committed,preview,decayed,next;
    std::vector<double> rates;uint64_t revision=0,completed=0;double epoch=0,observed=0;
    bool installed=false,previewDirty=false;
    void ClearDynamic(double time) {
        std::fill(committed.begin(),committed.end(),State{});preview=committed;
        epoch=observed=time;completed=0;previewDirty=false;
    }
    template<class Source> void Fill(Source&source,double time) {
        std::fill(rates.begin(),rates.end(),0.0);
        // Batch callback permits three shared temporal sin/cos pairs, not per-cell transcendental work.
        source(time,topology.Cells().data(),topology.Cells().size(),rates.data());
    }
public:
    // Exact keys + revision preserve dynamic state through device refresh and reordered source tables.
    // A new map/terrain/polygon revision clears history even when coordinates happen to match.
    BuildResult Install(const Grid&grid,const std::vector<Cell>&cells,uint64_t mapRevision,double time,
                        Parameters settings=Parameters()) {
        if(!ValidTime(time)||!ValidGrid(grid)||!ValidParameters(settings,grid))return BuildResult::Invalid;
        Topology candidate;const BuildResult result=candidate.Build(grid,cells);if(result!=BuildResult::Ready)return result;
        const bool reuse=installed&&revision==mapRevision&&topology.SameGeometry(candidate)
            &&settings.lifetime==parameters.lifetime&&settings.diffusivity==parameters.diffusivity&&settings.maximumRate==parameters.maximumRate;
        topology=std::move(candidate);parameters=settings;revision=mapRevision;installed=true;
        if(!reuse) {
            committed.assign(cells.size(),{});preview.resize(cells.size());decayed.resize(cells.size());next.resize(cells.size());rates.resize(cells.size());ClearDynamic(time);
        }
        return BuildResult::Ready;
    }
    void Clear(){*this=History();}
    bool ValidFor(uint64_t mapRevision)const{return installed&&mapRevision==revision;}
    template<class Source> AdvanceResult Advance(double time,Source&source) {
        if(!installed||!ValidTime(time))return AdvanceResult::Invalid;
        if(time==observed)return AdvanceResult::Unchanged;
        if(time<observed){ClearDynamic(time);return AdvanceResult::Rewound;}
        const double ratio=(time-epoch)/Tick;
        if(!std::isfinite(ratio)||ratio>double(MaxTickIndex))return AdvanceResult::Invalid;
        uint64_t wanted=uint64_t(std::floor(ratio));
        // Compare the actual fixed-clock boundaries, correcting representational division noise.
        while(epoch+double(wanted)*Tick>time)--wanted;
        while(wanted<MaxTickIndex&&epoch+double(wanted+1)*Tick<=time)++wanted;
        if(wanted-completed>MaxCatchupTicks) {
            // Preserve the last observed partial field, even if no atlas requested it yet.
            // Only the genuinely unseen interval suppresses births and diffusion.
            committed=Preview(source);
            const Decay decay=Decay::Make(time-observed,parameters.lifetime);
            for(State&s:committed)s=decay.Apply(s,0);
            preview=committed;epoch=observed=time;completed=0;previewDirty=false;
            return AdvanceResult::GapDecayed; // no invented births during an unseen large gap
        }
        while(completed<wanted) {
            Fill(source,epoch+(double(completed)+.5)*Tick);
            // The mathematical fixed step is Tick; subtracting large absolute timestamps
            // would introduce an age/diffusion error and can exceed Tick by roundoff.
            if(!Step(topology,parameters,committed,rates,Tick,decayed,next))return AdvanceResult::Invalid;
            committed.swap(next);++completed;
        }
        observed=time;previewDirty=true;return AdvanceResult::Advanced;
    }
    template<class Source> const std::vector<State>& Preview(Source&source) {
        if(!previewDirty)return preview;
        const double anchor=epoch+double(completed)*Tick,partial=Clamp(observed-anchor,0,Tick);
        if(partial<=0)preview=committed;
        else {
            Fill(source,(anchor+observed)*.5);
            if(!Step(topology,parameters,committed,rates,partial,decayed,preview))preview=committed;
        }
        previewDirty=false;return preview;
    }
    const std::vector<State>& Committed()const{return committed;}
    const Topology& Domain()const{return topology;}
    double Time()const{return observed;}
    double CommittedTime()const{return epoch+double(completed)*Tick;}
};
// Bilinear interpolation of density and age MOMENTS, never unweighted average age.
struct UnrestrictedSupport {
    struct Stencil {bool Allows(unsigned,unsigned)const{return true;}};
    Stencil At(int32_t,int32_t,double,double,double)const{return {};}
};
template<class Support> inline State Sample(const Topology&topology,const std::vector<State>&states,double x,double y,double level,const Support&support) {
    if(states.size()!=topology.Cells().size()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(level))return {};
    const Grid&g=topology.Geometry();const double px=(x-g.originX)/g.spacing,py=(y-g.originY)/g.spacing;
    if(!std::isfinite(px)||!std::isfinite(py)||px<(std::numeric_limits<int32_t>::min)()||py<(std::numeric_limits<int32_t>::min)()
        ||px>=(std::numeric_limits<int32_t>::max)()||py>=(std::numeric_limits<int32_t>::max)())return {};
    const int32_t ix=int32_t(std::floor(px)),iy=int32_t(std::floor(py));const double fx=px-ix,fy=py-iy;State out;
    const auto stencil=support.At(ix,iy,level,fx,fy);
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
        const int index=topology.Find(ix+dx,iy+dy,level);if(index<0)continue;
        const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
        // Zero rejected contributions without redistributing their weight to another shore.
        if(weight<=0 || !stencil.Allows(unsigned(dx),unsigned(dy)))continue;
        out.density+=states[size_t(index)].density*weight;out.ageMoment+=states[size_t(index)].ageMoment*weight;
    }
    return out;
}
inline State Sample(const Topology&topology,const std::vector<State>&states,double x,double y,double level) {
    return Sample(topology,states,x,y,level,UnrestrictedSupport{});
}
inline uint32_t WithAlpha(uint32_t vesselARGB,double coverage) {
    const uint32_t alpha=std::isfinite(coverage)?uint32_t(Clamp(coverage,0,1)*255+.5):0;
    return (vesselARGB&0x00ffffffu)|(alpha<<24);
}
struct Mapping {double originX=0,originY=0,widthWorld=0,heightWorld=0;unsigned widthPixels=0,heightPixels=0;};
template<class Coverage,class Support> inline bool RasterizeAlpha(const Topology&topology,const std::vector<State>&states,
    const Mapping&mapping,double level,double time,Coverage&coverage,std::vector<uint32_t>&vesselARGB,const Support&support) {
    if(!std::isfinite(mapping.originX)||!std::isfinite(mapping.originY)||!std::isfinite(mapping.widthWorld)
        ||!std::isfinite(mapping.heightWorld)||mapping.widthWorld<=0||mapping.heightWorld<=0
        ||!std::isfinite(mapping.originX+mapping.widthWorld)||!std::isfinite(mapping.originY+mapping.heightWorld)
        ||!mapping.widthPixels||!mapping.heightPixels||mapping.widthPixels>512||mapping.heightPixels>512
        ||!std::isfinite(level)||!std::isfinite(time)||states.size()!=topology.Cells().size()
        ||vesselARGB.size()!=size_t(mapping.widthPixels)*mapping.heightPixels)return false;
    // Clear stale coast alpha everywhere without changing vessel RGB. Empty or offscreen
    // history then costs only this linear write, not per-pixel support and hash searches.
    for(uint32_t&pixel:vesselARGB)pixel&=0x00ffffffu;
    struct Axis {double world=0,fraction=0;int64_t key=0;};
    std::array<Axis,512> horizontal,vertical;
    const Grid&g=topology.Geometry();
    auto axis=[](std::array<Axis,512>&out,unsigned count,double origin,double extent,double gridOrigin,double spacing) {
        for(unsigned i=0;i<count;++i) {
            Axis&a=out[i];
            // Preserve Sample's arithmetic, including overflow/out-of-grid rejection.
            a.world=origin+(double(i)+.5)*extent/count;
            const double p=(a.world-gridOrigin)/spacing;
            if(!std::isfinite(p)||p<(std::numeric_limits<int32_t>::min)()||p>=(std::numeric_limits<int32_t>::max)()) {
                a.key=p<(std::numeric_limits<int32_t>::min)()?(std::numeric_limits<int64_t>::min)():(std::numeric_limits<int64_t>::max)();
            } else {a.key=int32_t(std::floor(p));a.fraction=p-double(a.key);}
        }
    };
    axis(horizontal,mapping.widthPixels,mapping.originX,mapping.widthWorld,g.originX,g.spacing);
    axis(vertical,mapping.heightPixels,mapping.originY,mapping.heightWorld,g.originY,g.spacing);
    auto first=[](const std::array<Axis,512>&a,unsigned count,int64_t key) {
        return unsigned(std::lower_bound(a.begin(),a.begin()+count,key,[](const Axis&v,int64_t k){return v.key<k;})-a.begin());
    };
    auto after=[](const std::array<Axis,512>&a,unsigned count,int64_t key) {
        return unsigned(std::upper_bound(a.begin(),a.begin()+count,key,[](int64_t k,const Axis&v){return k<v.key;})-a.begin());
    };
    for(const RasterTile&tile:topology.RasterTiles()) {
        if(tile.level!=level||tile.x<horizontal[0].key||tile.x>horizontal[mapping.widthPixels-1].key
            ||tile.y<vertical[0].key||tile.y>vertical[mapping.heightPixels-1].key)continue;
        bool active=false;for(int32_t node:tile.nodes)if(node>=0&&states[size_t(node)].density>0){active=true;break;}
        if(!active)continue;
        const unsigned x0=first(horizontal,mapping.widthPixels,tile.x),x1=after(horizontal,mapping.widthPixels,tile.x);
        if(x0==x1)continue;
        const unsigned y0=first(vertical,mapping.heightPixels,tile.y),y1=after(vertical,mapping.heightPixels,tile.y);
        for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x) {
            const double fx=horizontal[x].fraction,fy=vertical[y].fraction;State state;
            const auto stencil=support.At(tile.x,tile.y,level,fx,fy);
            for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx) {
                const int32_t node=tile.nodes[dy*2+dx];if(node<0)continue;
                const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
                if(weight<=0||!stencil.Allows(dx,dy))continue;
                state.density+=states[size_t(node)].density*weight;state.ageMoment+=states[size_t(node)].ageMoment*weight;
            }
            const double value=state.density>0?coverage(state,horizontal[x].world,vertical[y].world,time):0;
            const size_t n=size_t(y)*mapping.widthPixels+x;vesselARGB[n]=WithAlpha(vesselARGB[n],value);
        }
    }
    return true;
}

template<class Coverage> inline bool RasterizeAlpha(const Topology&topology,const std::vector<State>&states,
    const Mapping&mapping,double level,double time,Coverage&coverage,std::vector<uint32_t>&vesselARGB) {
    return RasterizeAlpha(topology,states,mapping,level,time,coverage,vesselARGB,UnrestrictedSupport{});
}

// One bounded camera mapping. The dependency revision must change whenever the
// topology or support geometry changes; neither density/age nor time is cached.
// Support::At/Allows must be pure within that immutable revision.
class RasterLookup {
    struct Axis {double world=0,fraction=0;int64_t key=0;};
    struct TileRange {uint32_t tile=0;uint16_t x0=0,x1=0,y0=0,y1=0;};
    std::array<Axis,512> horizontal{},vertical{};
    std::vector<TileRange> ranges;
    std::vector<uint8_t> masks;
    const Topology *owner=nullptr;const void *supportOwner=nullptr;
    Mapping mapping;double level=0;uint64_t revision=0,builds=0;
    size_t sampleCount=0;
    static bool Same(double a,double b){return a==b&&(a!=0||std::signbit(a)==std::signbit(b));}
    static bool Valid(const Mapping&m,double l) {
        return std::isfinite(m.originX)&&std::isfinite(m.originY)&&std::isfinite(m.widthWorld)
            &&std::isfinite(m.heightWorld)&&m.widthWorld>0&&m.heightWorld>0
            &&std::isfinite(m.originX+m.widthWorld)&&std::isfinite(m.originY+m.heightWorld)
            &&m.widthPixels&&m.heightPixels&&m.widthPixels<=512&&m.heightPixels<=512&&std::isfinite(l);
    }
public:
    static constexpr size_t MaximumPixels=512*512;
    void Clear(){*this=RasterLookup();}
    uint64_t BuildCount()const{return builds;}
    size_t SampleCount()const{return sampleCount;}
    size_t TileCount()const{return ranges.size();}
    size_t MemoryBytes()const{return sizeof(*this)+ranges.capacity()*sizeof(TileRange)+masks.capacity();}
    bool Matches(const Topology&t,const Mapping&m,double l,uint64_t dependency,const void *support)const {
        return owner==&t&&supportOwner==support&&revision==dependency&&revision!=0&&Same(level,l)
            &&Same(mapping.originX,m.originX)&&Same(mapping.originY,m.originY)
            &&Same(mapping.widthWorld,m.widthWorld)&&Same(mapping.heightWorld,m.heightWorld)
            &&mapping.widthPixels==m.widthPixels&&mapping.heightPixels==m.heightPixels;
    }
    template<class Support> bool Prepare(const Topology&t,const Mapping&m,double l,uint64_t dependency,const Support&support) {
        if(!dependency||!Valid(m,l))return false;
        if(Matches(t,m,l,dependency,&support))return true;
        RasterLookup next;next.owner=&t;next.supportOwner=&support;next.mapping=m;next.level=l;next.revision=dependency;
        next.builds=builds+1;if(!next.builds)next.builds=1;
        const Grid&g=t.Geometry();
        auto axis=[](std::array<Axis,512>&out,unsigned count,double origin,double extent,double gridOrigin,double spacing) {
            for(unsigned i=0;i<count;++i) {
                Axis&a=out[i];a.world=origin+(double(i)+.5)*extent/count;
                const double p=(a.world-gridOrigin)/spacing;
                if(!std::isfinite(p)||p<(std::numeric_limits<int32_t>::min)()||p>=(std::numeric_limits<int32_t>::max)())
                    a.key=p<(std::numeric_limits<int32_t>::min)()?(std::numeric_limits<int64_t>::min)():(std::numeric_limits<int64_t>::max)();
                else {a.key=int32_t(std::floor(p));a.fraction=p-double(a.key);}
            }
        };
        axis(next.horizontal,m.widthPixels,m.originX,m.widthWorld,g.originX,g.spacing);
        axis(next.vertical,m.heightPixels,m.originY,m.heightWorld,g.originY,g.spacing);
        auto first=[](const std::array<Axis,512>&a,unsigned count,int64_t key) {
            return unsigned(std::lower_bound(a.begin(),a.begin()+count,key,[](const Axis&v,int64_t k){return v.key<k;})-a.begin());
        };
        auto after=[](const std::array<Axis,512>&a,unsigned count,int64_t key) {
            return unsigned(std::upper_bound(a.begin(),a.begin()+count,key,[](int64_t k,const Axis&v){return k<v.key;})-a.begin());
        };
        next.masks.assign(size_t(m.widthPixels)*m.heightPixels,0);
        next.ranges.reserve((std::min)(t.RasterTiles().size(),next.masks.size()));
        for(size_t i=0;i<t.RasterTiles().size();++i) {
            const RasterTile&tile=t.RasterTiles()[i];
            if(tile.level!=l||tile.x<next.horizontal[0].key||tile.x>next.horizontal[m.widthPixels-1].key
                ||tile.y<next.vertical[0].key||tile.y>next.vertical[m.heightPixels-1].key)continue;
            const unsigned x0=first(next.horizontal,m.widthPixels,tile.x),x1=after(next.horizontal,m.widthPixels,tile.x);
            if(x0==x1)continue;
            const unsigned y0=first(next.vertical,m.heightPixels,tile.y),y1=after(next.vertical,m.heightPixels,tile.y);
            if(y0==y1)continue;
            next.ranges.push_back({uint32_t(i),uint16_t(x0),uint16_t(x1),uint16_t(y0),uint16_t(y1)});
            for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x) {
                const double fx=next.horizontal[x].fraction,fy=next.vertical[y].fraction;
                const auto stencil=support.At(tile.x,tile.y,l,fx,fy);uint8_t mask=0;
                for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx) {
                    const unsigned corner=dy*2+dx;if(tile.nodes[corner]<0)continue;
                    const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
                    if(weight>0&&stencil.Allows(dx,dy))mask|=uint8_t(1u<<corner);
                }
                next.masks[size_t(y)*m.widthPixels+x]=mask;++next.sampleCount;
            }
        }
        *this=std::move(next);return true;
    }
    // Prepare camera-fixed caller data, e.g. shoreline spatial breakup, once
    // per BuildCount. Pixel ownership is unique and follows the original order.
    template<class Function> void ForEachSample(Function function)const {
        for(const TileRange&r:ranges)for(unsigned y=r.y0;y<r.y1;++y)for(unsigned x=r.x0;x<r.x1;++x)
            function(size_t(y)*mapping.widthPixels+x,horizontal[x].world,vertical[y].world);
    }
    template<class Coverage> bool Rasterize(const std::vector<State>&states,double time,Coverage&coverage,std::vector<uint32_t>&pixels)const {
        if(!owner||!std::isfinite(time)||states.size()!=owner->Cells().size()||pixels.size()!=masks.size())return false;
        for(uint32_t&pixel:pixels)pixel&=0x00ffffffu;
        for(const TileRange&r:ranges) {
            const RasterTile&tile=owner->RasterTiles()[r.tile];
            bool active=false;for(int32_t node:tile.nodes)if(node>=0&&states[size_t(node)].density>0){active=true;break;}
            if(!active)continue;
            for(unsigned y=r.y0;y<r.y1;++y)for(unsigned x=r.x0;x<r.x1;++x) {
                const double fx=horizontal[x].fraction,fy=vertical[y].fraction;State state;
                const size_t n=size_t(y)*mapping.widthPixels+x;const uint8_t mask=masks[n];
                for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx) {
                    const unsigned corner=dy*2+dx;if(!(mask&(1u<<corner)))continue;
                    const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
                    state.density+=states[size_t(tile.nodes[corner])].density*weight;
                    state.ageMoment+=states[size_t(tile.nodes[corner])].ageMoment*weight;
                }
                const double value=state.density>0?coverage(state,horizontal[x].world,vertical[y].world,time,n):0;
                pixels[n]=WithAlpha(pixels[n],value);
            }
        }
        return true;
    }
};
template<class Coverage,class Support> inline bool RasterizeAlphaCached(const Topology&topology,const std::vector<State>&states,
    const Mapping&mapping,double level,double time,Coverage&coverage,std::vector<uint32_t>&pixels,const Support&support,
    RasterLookup&lookup,uint64_t dependencyRevision) {
    if(!std::isfinite(time)||states.size()!=topology.Cells().size()||pixels.size()!=size_t(mapping.widthPixels)*mapping.heightPixels
        ||!lookup.Prepare(topology,mapping,level,dependencyRevision,support))return false;
    auto material=[&](const State&s,double x,double y,double t,size_t){return coverage(s,x,y,t);};
    return lookup.Rasterize(states,time,material,pixels);
}

}
