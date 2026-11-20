/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

// Rendering metadata only: real scene depth remains the source of refraction and intersections.
// A broad polygon can contain both an ocean and enclosed pools, so classify its actual wet terrain.
namespace WaterOpticalContext {
constexpr int MaxDimension = 2048;
constexpr size_t MaxCells = 1024u * 1024u;
constexpr size_t MaxPoints = 65536;
constexpr size_t MaxLevels = 8;
// Independent physical terrain-coast distance; never packed into optical RGBA.
constexpr float PhysicalShoreRange = 160.0f;
struct Point { float x, y, z; };
struct Polygon { std::vector<Point> points; };
using Interval=std::pair<double,double>;
// Exact scanline union, including authored edges. Shared by optical connectivity
// and physical coastal domains so a sub-grid polygon gap never becomes a wet link.
inline std::vector<Interval> Intervals(const std::vector<Polygon>&polygons,
                                      double level,double line,bool horizontal) {
    std::vector<Interval> spans;std::vector<double> crossings;
    for(const auto&polygon:polygons) {
        if(polygon.points.size()<3)continue;
        bool same=true;for(const auto&p:polygon.points)same=same&&std::abs(double(p.z)-level)<.01;
        if(!same)continue;
        crossings.clear();
        for(size_t i=0,j=polygon.points.size()-1;i<polygon.points.size();j=i++) {
            const auto&a=polygon.points[i];const auto&b=polygon.points[j];
            const double ax=horizontal?a.x:a.y,ay=horizontal?a.y:a.x;
            const double bx=horizontal?b.x:b.y,by=horizontal?b.y:b.x;
            if((ay>line)!=(by>line))crossings.push_back(ax+(line-ay)*(bx-ax)/(by-ay));
            if(ay==line){spans.push_back({ax,ax});if(by==line)spans.push_back({(std::min)(ax,bx),(std::max)(ax,bx)});}
        }
        std::sort(crossings.begin(),crossings.end());
        for(size_t i=0;i+1<crossings.size();i+=2)spans.push_back({crossings[i],crossings[i+1]});
    }
    std::sort(spans.begin(),spans.end());std::vector<Interval> result;
    for(auto span:spans) {
        if(result.empty()||span.first>result.back().second)result.push_back(span);
        else result.back().second=(std::max)(result.back().second,span.second);
    }
    return result;
}
struct Field {
    std::vector<uint8_t> wet; // exact wet mask, before dry-edge exposure extension
    std::vector<uint8_t> links; // positive-X bit 1, positive-Y bit 2: complete polygon-covered segments
    std::vector<uint8_t> ocean;
    std::vector<uint8_t> shore;
    std::vector<uint8_t> exposure; // wet region connected to the playable map boundary
    std::vector<uint8_t> physicalShore; // 0..PhysicalShoreRange, terrain only; polygon edges are excluded
    std::vector<uint8_t> coast;    // terrain-coast replacement validity, inset from every nonzero ocean texel
};
inline uint32_t Texel(const Field &field,size_t n) {
    return (uint32_t(field.coast[n])<<24)|(uint32_t(field.ocean[n])<<16)
        |(uint32_t(field.shore[n])<<8)|uint32_t(field.exposure[n]);
}

inline float Offshore(float distance) {
    float t = (std::max)(0.0f, (std::min)(1.0f, (distance - 30.0f) / 70.0f));
    return t*t*(3.0f-2.0f*t);
}

inline bool SamePolygons(const std::vector<Polygon>& a, const std::vector<Polygon>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i=0; i<a.size(); ++i) {
        if (a[i].points.size() != b[i].points.size()) return false;
        for (size_t j=0; j<a[i].points.size(); ++j) {
            const Point &p=a[i].points[j], &q=b[i].points[j];
            if (p.x!=q.x || p.y!=q.y || p.z!=q.z) return false;
        }
    }
    return true;
}

inline bool Build(const uint8_t* heights, int width, int height, int border,
                  float spacing, float heightScale, float level,
                  const std::vector<Polygon>& polygons, Field& result, bool retainTopology=false) {
    result.ocean.clear(); result.shore.clear(); result.exposure.clear(); result.coast.clear(); result.physicalShore.clear(); result.wet.clear(); result.links.clear();
    if (!heights || width<2 || height<2 || width>MaxDimension || height>MaxDimension ||
        size_t(width)*height>MaxCells || border<0 || border>(width-2)/2 || border>(height-2)/2 ||
        !std::isfinite(spacing) || spacing<=0 || !std::isfinite(heightScale) || heightScale<=0 ||
        !std::isfinite(level) || polygons.size()>MaxPoints/3) return false;
    size_t points=0;
    for (const Polygon& polygon : polygons) {
        if (polygon.points.size()>MaxPoints-points) return false;
        points+=polygon.points.size();
        for (const Point& p : polygon.points)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    }
    const size_t count=size_t(width)*height;
    enum : uint8_t {PositiveX=1,PositiveY=2};
    std::vector<uint8_t> wet(count,0),links(count,0),wetColumn(size_t(width),0);
    auto span = [&](int y,double from,double to) {
        const int left=int((std::max)(0.0,(std::min)(double(width),std::ceil(from/spacing+border))));
        const int right=int((std::max)(-1.0,(std::min)(double(width-1),std::floor(to/spacing+border))));
        for (int x=left; x<=right; ++x) {
            const size_t n=size_t(y)*width+x;
            if (heights[n]*heightScale<level) { wet[n]=1;wetColumn[size_t(x)]=1; }
            if (x<right) links[n]|=PositiveX;
        }
    };
    // Scanline intervals form the union, including concave polygons and overlaps. Only flat,
    // non-river polygons at this level enter the caller's list; different levels never connect.
    for (int y=0;y<height;++y)
        for (const auto &interval:Intervals(polygons,level,(y-border)*spacing,true))
            span(y,interval.first,interval.second);
    // Record complete covered segments once, with bounded scanline storage. The flood
    // never scans polygons per cell, and overlapping spans can jointly cover one link.
    for (int x=0;x<width;++x) if(wetColumn[size_t(x)])
        for (const auto &interval:Intervals(polygons,level,(x-border)*spacing,false)) {
            const int first=int((std::max)(0.0,(std::min)(double(height),std::ceil(interval.first/spacing+border))));
            const int last=int((std::max)(-1.0,(std::min)(double(height-1),std::floor(interval.second/spacing+border))));
            for(int y=first;y<last;++y)links[size_t(y)*width+x]|=PositiveY;
        }
    result.ocean.assign(count,0);
    result.shore.assign(count,0);
    std::vector<int> queue; queue.reserve(count);
    auto seed = [&](int x,int y) {
        const int n=y*width+x;
        if (wet[n] && !result.ocean[n]) { result.ocean[n]=1; queue.push_back(n); }
    };
    // Playable boundary, not texture padding or polygon bounds, defines connection to open water.
    for (int x=border; x<width-border; ++x) { seed(x,border); seed(x,height-border-1); }
    for (int y=border; y<height-border; ++y) { seed(border,y); seed(width-border-1,y); }
    for (size_t i=0; i<queue.size(); ++i) {
        const int n=queue[i], x=n%width,y=n/width;
        if (x>0 && (links[n-1]&PositiveX)) seed(x-1,y);
        if (x+1<width && (links[n]&PositiveX)) seed(x+1,y);
        if (y>0 && (links[n-width]&PositiveY)) seed(x,y-1);
        if (y+1<height && (links[n]&PositiveY)) seed(x,y+1);
    }
    if(retainTopology){result.wet=wet;result.links=links;}
    result.exposure=result.ocean;
    for(uint8_t &value:result.exposure)value=value?255:0;
    result.coast.assign(count,0);
    result.physicalShore.assign(count,255);
    // Two-pass chamfer distance is bounded O(cells), with diagonal distances avoiding a square
    // shoreline fade. Grid borders are not synthetic shores; an entirely wet sea has full extent.
    const float limit=100.0f, diagonal=spacing*1.41421356237f;
    std::vector<float> distance(count,limit),terrainDistance(count,PhysicalShoreRange);
    for (size_t n=0; n<count; ++n) {
        if (!wet[n]) distance[n]=0;
        // Authored polygon boundaries over submerged terrain are not physical coasts.
        if (heights[n]*heightScale>=level) terrainDistance[n]=0;
    }
    auto relax = [&](int n,int neighbour,float step) {
        distance[n]=(std::min)(distance[n],distance[neighbour]+step);
        terrainDistance[n]=(std::min)(terrainDistance[n],terrainDistance[neighbour]+step);
    };
    for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
        const int n=y*width+x;
        if (x>0) relax(n,n-1,spacing);
        if (y>0) {
            relax(n,n-width,spacing);
            if (x>0) relax(n,n-width-1,diagonal);
            if (x+1<width) relax(n,n-width+1,diagonal);
        }
    }
    for (int y=height-1; y>=0; --y) for (int x=width-1; x>=0; --x) {
        const int n=y*width+x;
        if (x+1<width) relax(n,n+1,spacing);
        if (y+1<height) {
            relax(n,n+width,spacing);
            if (x>0) relax(n,n+width-1,diagonal);
            if (x+1<width) relax(n,n+width+1,diagonal);
        }
        result.physicalShore[n]=uint8_t(terrainDistance[n]/PhysicalShoreRange*255.0f+0.5f);
        result.shore[n]=uint8_t(distance[n]/limit*255.0f+0.5f);
        result.ocean[n]=result.ocean[n] ? uint8_t(Offshore(distance[n])*255.0f+0.5f) : 0;
    }
    // Analytic shore-foam validity stays inset from every nonzero-R texel. The shader
    // also fades this fallback by the remaining analytic wave contribution when FFT
    // phases extend into exposed shallows, and repeats the zero-R resource guard.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const size_t n=size_t(y)*width+x;
        if(terrainDistance[n]>=20.0f)continue;
        bool adjacentWet=wet[n]!=0,exposed=result.exposure[n]!=0,enclosedWet=false;
        for(int yy=(std::max)(0,y-1);yy<=(std::min)(height-1,y+1);++yy)
            for(int xx=(std::max)(0,x-1);xx<=(std::min)(width-1,x+1);++xx) {
                const size_t neighbour=size_t(yy)*width+xx;
                adjacentWet=adjacentWet || wet[neighbour]!=0;
                exposed=exposed || (wet[neighbour] && result.exposure[neighbour]!=0);
                enclosedWet=enclosedWet || (wet[neighbour] && result.exposure[neighbour]==0);
            }
        if(!adjacentWet)continue;
        bool analytic=true;
        for(int yy=(std::max)(0,y-1);yy<=(std::min)(height-1,y+1);++yy)
            for(int xx=(std::max)(0,x-1);xx<=(std::min)(width-1,x+1);++xx)
                analytic=analytic && result.ocean[size_t(yy)*width+xx]==0;
        if(!analytic)continue;
        // Replacement validity must include the actual crossing between a dry terrain node
        // and the first wet one. Wet-only A would retain most of the permanent rim there.
        // Emission is independently gated by positive terrain depth and actual scene wetness.
        // Dry exposure supports the ordinary wet-edge transition, but must not
        // interpolate an ocean wave into an enclosed pool across a narrow ridge.
        if(!wet[n])result.exposure[n]=exposed&&!enclosedWet?255:0;
        const float t=(std::max)(0.0f,(std::min)(1.0f,(terrainDistance[n]-10.0f)/10.0f));
        result.coast[n]=uint8_t((1.0f-t*t*(3.0f-2.0f*t))*255.0f+0.5f);
    }
    return true;
}
}
