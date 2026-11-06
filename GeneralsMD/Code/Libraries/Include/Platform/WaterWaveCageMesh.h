/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterProjectedMesh.h"
#include <array>
#include <unordered_map>

namespace WaterWaveCageMesh {
constexpr unsigned WaterlineIterations=16;
// Section 8 of Jeschke et al. (2020): insert rest-waterline vertices before
// evaluating the cage map. Samplers take a WaterProjectedMesh::Point; depth is
// water level minus bilinear terrain height, and eligible selects cage support.
// Authored edges, collars and transition fans are deliberately preserved. If a
// crossed edge touches an ineligible triangle, preserve its whole connected
// crossing chain: partially clipping that chain would introduce a T junction.
// A chain that reaches an authored seam can therefore remain untessellated.
// Failure is atomic. This clips sign-changing triangle edges, not sub-triangle
// islands or multiple terrain crossings with equal-sign edge endpoints.
template<class DepthSampler,class EligibleSampler>
inline bool ClipRestWaterline(WaterProjectedMesh::Mesh&mesh,DepthSampler depthSampler,
                             EligibleSampler eligibleSampler,size_t maxVertices=65535){
    using namespace WaterProjectedMesh;
    if(maxVertices>65535||mesh.vertices.size()>maxVertices||mesh.indices.size()%3)return false;
    const size_t triangleCount=mesh.indices.size()/3;
    std::vector<double> depths(mesh.vertices.size());std::vector<uint8_t> eligibleVertex(mesh.vertices.size());
    for(size_t i=0;i<mesh.vertices.size();++i){
        const auto&v=mesh.vertices[i];
        if(!std::isfinite(v.world.x)||!std::isfinite(v.world.y))return false;
        depths[i]=depthSampler(v.world);if(!std::isfinite(depths[i]))return false;
        eligibleVertex[i]=v.boundary<0&&v.collar<0&&v.transition<0&&eligibleSampler(v.world);
    }
    struct Edge {size_t owner[2]={0,0};unsigned count=0;int vertex=-1;};
    std::unordered_map<uint32_t,Edge> edges;
    constexpr uint32_t None=0xffffffffu;
    std::vector<std::array<uint32_t,3>> crossings(triangleCount,std::array<uint32_t,3>{{None,None,None}});
    std::vector<uint8_t> eligible(triangleCount,1);std::vector<size_t> queue;
    size_t affectedTriangles=0;
    const auto key=[](uint16_t a,uint16_t b){return (uint32_t((std::min)(a,b))<<16)|uint32_t((std::max)(a,b));};
    for(size_t t=0;t<triangleCount;++t){
        bool negative=false;
        for(int corner=0;corner<3;++corner){
            const uint16_t a=mesh.indices[t*3+corner],b=mesh.indices[t*3+(corner+1)%3];
            if(a>=mesh.vertices.size()||b>=mesh.vertices.size()||a==b)return false;
            eligible[t]=eligible[t]&&eligibleVertex[a];
            negative=negative||depths[a]<0;
            if((depths[a]<0&&depths[b]>0)||(depths[b]<0&&depths[a]>0)){
                const auto k=key(a,b);crossings[t][corner]=k;auto&e=edges[k];
                if(e.count>=2)return false;e.owner[e.count++]=t;
            }
        }
        if(eligible[t]&&negative)++affectedTriangles;
        if(!eligible[t])queue.push_back(t);
    }
    for(size_t head=0;head<queue.size();++head){
        for(auto k:crossings[queue[head]])if(k!=None){
            const auto&e=edges.find(k)->second;
            for(unsigned owner=0;owner<e.count;++owner)if(eligible[e.owner[owner]]){
                const size_t t=e.owner[owner];
                if(depths[mesh.indices[t*3]]<0||depths[mesh.indices[t*3+1]]<0||depths[mesh.indices[t*3+2]]<0)
                    --affectedTriangles;
                eligible[t]=0;queue.push_back(t);
            }
        }
    }
    // Validate and propagate crossing chains before accepting an unchanged mesh.
    if(!affectedTriangles)return true;
    // Stage only intersections; original vertices stay untouched until success.
    const size_t originalVertices=mesh.vertices.size();
    std::vector<Vertex> added;std::vector<uint16_t> indices;indices.reserve(mesh.indices.size());bool valid=true;
    const auto world=[&](uint16_t index){
        return index<originalVertices?mesh.vertices[index].world:added[size_t(index)-originalVertices].world;
    };
    const auto intersection=[&](uint16_t a,uint16_t b)->uint16_t{
        if(depths[a]==0)return a;if(depths[b]==0)return b;
        auto found=edges.find(key(a,b));if(found==edges.end()){valid=false;return 0;}
        auto&e=found->second;if(e.vertex>=0)return uint16_t(e.vertex);
        // Always evaluate in canonical endpoint order, independently of winding.
        const uint16_t first=(std::min)(a,b),second=(std::max)(a,b);
        const Point p=mesh.vertices[first].world,q=mesh.vertices[second].world;
        double low=0,high=1,lowDepth=depths[first];
        for(unsigned step=0;step<WaterlineIterations;++step){
            const double mid=(low+high)*.5;const Point point{p.x+(q.x-p.x)*mid,p.y+(q.y-p.y)*mid};
            const double depth=depthSampler(point);if(!std::isfinite(depth)){valid=false;return 0;}
            if(depth==0){low=high=mid;lowDepth=0;break;}
            if((depth>=0)==(lowDepth>=0)){low=mid;lowDepth=depth;}else high=mid;
        }
        const double parameter=lowDepth>=0?low:high; // round toward the wet side
        if(originalVertices+added.size()>=maxVertices){valid=false;return 0;}
        Vertex v;v.world={p.x+(q.x-p.x)*parameter,p.y+(q.y-p.y)*parameter};
        e.vertex=int(originalVertices+added.size());added.push_back(v);return uint16_t(e.vertex);
    };
    const auto triangle=[&](uint16_t a,uint16_t b,uint16_t c){
        if(a==b||b==c||c==a)return;
        const Point pa=world(a),pb=world(b),pc=world(c);
        if(std::abs((pb.x-pa.x)*(pc.y-pa.y)-(pb.y-pa.y)*(pc.x-pa.x))<1e-16)return;
        indices.insert(indices.end(),{a,b,c});
    };
    for(size_t t=0;t<triangleCount;++t){
        const uint16_t a=mesh.indices[t*3],b=mesh.indices[t*3+1],c=mesh.indices[t*3+2];
        if(!eligible[t]||(depths[a]>=0&&depths[b]>=0&&depths[c]>=0)){
            indices.insert(indices.end(),{a,b,c});continue;
        }
        if(depths[a]<0&&depths[b]<0&&depths[c]<0)continue;
        uint16_t polygon[6];unsigned count=0;uint16_t previous=c;
        for(uint16_t current:{a,b,c}){
            const bool before=depths[previous]>=0,after=depths[current]>=0;
            if(before!=after){polygon[count++]=intersection(previous,current);if(!valid)return false;}
            if(after)polygon[count++]=current;
            previous=current;
        }
        for(unsigned i=1;i+1<count;++i)triangle(polygon[0],polygon[i],polygon[i+1]);
    }
    mesh.vertices.insert(mesh.vertices.end(),added.begin(),added.end());mesh.indices.swap(indices);return true;
}
}
