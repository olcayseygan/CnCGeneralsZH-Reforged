/* Exact reuse of terrain-culled, rest-waterline-clipped projected water meshes, one per patch. */
#pragma once
#include "WaterProjectedMesh.h"
#include <deque>
#include <memory>

namespace WaterPreparedMeshCache {
class Cache {
public:
    class View {
        friend class Cache;
        std::shared_ptr<const WaterProjectedMesh::Mesh> pinned_;
        const WaterProjectedMesh::Mesh *fallback_ = nullptr;
        explicit View(std::shared_ptr<const WaterProjectedMesh::Mesh> mesh) : pinned_(std::move(mesh)) {}
        explicit View(const WaterProjectedMesh::Mesh &mesh) : fallback_(&mesh) {}
    public:
        View() = default;
        const WaterProjectedMesh::Mesh &Get() const {
            return pinned_ ? *pinned_ : *fallback_;
        }
        const WaterProjectedMesh::Mesh &operator*() const {
            return Get();
        }
        const WaterProjectedMesh::Mesh *operator->() const {
            return &Get();
        }
    };
    explicit Cache(size_t maxBytes=32u*1024u*1024u,size_t maxEntries=8):maxBytes_(maxBytes),maxEntries_(maxEntries) {}

    // The owner clears this cache when terrain, optical support or processing rules change.
    // Dynamic bow refinement is part of the complete input mesh, never an implicit key.
    template <class Compute>
    void Process(WaterProjectedMesh::Mesh &mesh, float level, bool cullTerrain, bool waveCage,
        const Compute &compute, bool *reused = nullptr) {
        if (reused) *reused=false;
        size_t inputBytes=0;
        const bool cacheable = maxBytes_ && maxEntries_ && std::isfinite(level) && Measure(mesh, maxBytes_,
            inputBytes);
        if (!cacheable) {
            compute(mesh);
            return;
        }
        for (const Entry &entry : entries_) {
            if (!(Same(level, entry.level) && cullTerrain == entry.cullTerrain &&
                waveCage == entry.waveCage && Same(mesh, entry.input)))
                continue;
            mesh = *entry.output;
            if (reused) *reused=true;
            return;
        }
        const bool validInput = Valid(mesh);
        if (!validInput) {
            compute(mesh);
            return;
        }
        WaterProjectedMesh::Mesh before=mesh;
        compute(mesh);
        size_t outputBytes=0;
        // Bypasses preserve the existing entries. Neither an oversized output nor invalid
        // callback result can displace a valid earlier input/output pair.
        const bool validOutput = Measure(mesh, maxBytes_ - inputBytes, outputBytes) && Valid(mesh);
        if (!validOutput)
            return;
        WaterProjectedMesh::Mesh after=mesh;
        const bool fits = Measure(before, maxBytes_, inputBytes, true) && Measure(after, maxBytes_ - inputBytes,
            outputBytes, true);
        if (!fits)
            return;
        const size_t bytes=inputBytes+outputBytes;
        // FIFO keeps eviction independent of frame timing, as the topology cache does.
        while (!entries_.empty() && (entries_.size()>=maxEntries_ || cachedBytes_>maxBytes_-bytes)) {
            cachedBytes_-=entries_.front().bytes;entries_.pop_front();
        }
        entries_.emplace_back();
        Entry &entry=entries_.back();
        std::swap(entry.input, before);
        entry.output = std::make_shared<const WaterProjectedMesh::Mesh>(std::move(after));
        entry.level = level;
        entry.cullTerrain = cullTerrain;
        entry.waveCage = waveCage;
        entry.bytes = bytes;
        cachedBytes_ += bytes;
    }

    // Pin a read-only cached result through this draw, including cache Clear or
    // eviction during shader setup. Cacheable misses consume mesh's buffers;
    // bypass views borrow the computed caller mesh, which must outlive the view.
    // CachedBytes covers cache-owned entries; release each view after its draw.
    // Never cast away a view's const and pass its mesh back as mutable input.
    template <class Compute>
    View ProcessView(WaterProjectedMesh::Mesh &mesh, float level, bool cullTerrain, bool waveCage,
        const Compute &compute, bool *reused = nullptr) {
        if (reused)
            *reused = false;
        size_t inputBytes = 0;
        const bool cacheable = maxBytes_ && maxEntries_ && std::isfinite(level) && Measure(mesh, maxBytes_,
            inputBytes);
        if (!cacheable) {
            compute(mesh);
            return View(mesh);
        }
        for (const Entry &entry : entries_) {
            if (!(Same(level, entry.level) && cullTerrain == entry.cullTerrain && waveCage == entry.waveCage
                && Same(mesh, entry.input)))
                continue;
            if (reused)
                *reused = true;
            return View(entry.output);
        }
        const bool validInput = Valid(mesh);
        if (!validInput) {
            compute(mesh);
            return View(mesh);
        }
        WaterProjectedMesh::Mesh before = mesh;
        compute(mesh);
        size_t outputBytes = 0;
        const bool validOutput = Measure(mesh, maxBytes_ - inputBytes, outputBytes) && Valid(mesh);
        if (!validOutput)
            return View(mesh);
        const bool inputFits = Measure(before, maxBytes_, inputBytes, true);
        bool outputFits = inputFits && Measure(mesh, maxBytes_ - inputBytes, outputBytes, true);
        if (!inputFits)
            return View(mesh);
        if (!outputFits) {
            // Match Process's compact-copy admission when spare caller capacity
            // alone exceeds the budget; never retain unaccounted moved buffers.
            WaterProjectedMesh::Mesh compact = mesh;
            outputFits = Measure(compact, maxBytes_ - inputBytes, outputBytes, true);
            if (!outputFits)
                return View(mesh);
            std::swap(mesh, compact);
        }
        const size_t bytes = inputBytes + outputBytes;
        while (!entries_.empty() && (entries_.size() >= maxEntries_ || cachedBytes_ > maxBytes_ - bytes)) {
            cachedBytes_ -= entries_.front().bytes;
            entries_.pop_front();
        }
        entries_.emplace_back();
        Entry &entry = entries_.back();
        std::swap(entry.input, before);
        entry.output = std::make_shared<const WaterProjectedMesh::Mesh>(std::move(mesh));
        entry.level=level;entry.cullTerrain=cullTerrain;entry.waveCage=waveCage;entry.bytes=bytes;
        cachedBytes_+=bytes;
        return View(entry.output);
    }
    void Clear() { entries_.clear();cachedBytes_=0; }
    size_t CachedBytes() const { return cachedBytes_; }
    size_t EntryCount() const { return entries_.size(); }

private:
    static bool Same(double a,double b) {
        uint64_t aa,bb;std::memcpy(&aa,&a,sizeof(aa));std::memcpy(&bb,&b,sizeof(bb));return aa==bb;
    }
    static bool Same(float a,float b) {
        uint32_t aa,bb;std::memcpy(&aa,&a,sizeof(aa));std::memcpy(&bb,&b,sizeof(bb));return aa==bb;
    }
    static bool Same(WaterProjectedMesh::Point a,WaterProjectedMesh::Point b) { return Same(a.x,b.x) && Same(a.y,b.y); }
    static bool Same(const WaterProjectedMesh::Mesh &a,const WaterProjectedMesh::Mesh &b) {
        if (a.cells!=b.cells || a.indices!=b.indices || a.vertices.size()!=b.vertices.size() ||
            a.collars.size()!=b.collars.size() || a.transitions.size()!=b.transitions.size()) return false;
        for (size_t i=0;i<a.vertices.size();++i) {
            const auto &x=a.vertices[i],&y=b.vertices[i];
            if (!Same(x.world,y.world) || x.boundary!=y.boundary || !Same(x.edgeParameter,y.edgeParameter) ||
                x.knot!=y.knot || x.transition!=y.transition || x.collar!=y.collar || x.lattice!=y.lattice) return false;
        }
        for (size_t i=0;i<a.transitions.size();++i) {
            const auto &x=a.transitions[i],&y=b.transitions[i];
            if (!Same(x.analytic,y.analytic)) return false;
            for (int j=0;j<3;++j) if (!Same(x.source[j],y.source[j]) || !Same(x.weight[j],y.weight[j])) return false;
        }
        for (size_t i=0;i<a.collars.size();++i) {
            const auto &x=a.collars[i],&y=b.collars[i];
            if (x.count!=y.count || !Same(x.fade,y.fade)) return false;
            // Build leaves unused slots uninitialized. Only active slots have meaning.
            for (int j=0;j<x.count;++j) if (x.edge[j]!=y.edge[j] || !Same(x.parameter[j],y.parameter[j]) || !Same(x.weight[j],y.weight[j])) return false;
        }
        return true;
    }
    static bool Measure(const WaterProjectedMesh::Mesh &mesh,size_t limit,size_t &bytes,bool allocated=false) {
        bytes=0;
        const size_t counts[]={allocated?mesh.vertices.capacity():mesh.vertices.size(),allocated?mesh.indices.capacity():mesh.indices.size(),
            allocated?mesh.transitions.capacity():mesh.transitions.size(),allocated?mesh.collars.capacity():mesh.collars.size()};
        const size_t widths[]={sizeof(WaterProjectedMesh::Vertex),sizeof(uint16_t),sizeof(WaterProjectedMesh::CentreTransition),sizeof(WaterProjectedMesh::BoundaryCollar)};
        for (int i=0;i<4;++i) {
            if (counts[i]>(limit-bytes)/widths[i]) return false;
            bytes+=counts[i]*widths[i];
        }
        return mesh.vertices.size()<=65536 && mesh.indices.size()%3==0;
    }
    static bool Valid(const WaterProjectedMesh::Mesh &mesh) {
        for (const auto &v:mesh.vertices) if (!std::isfinite(v.world.x) || !std::isfinite(v.world.y) || !std::isfinite(v.edgeParameter) ||
            v.boundary < -1 || v.boundary>3 || v.transition < -1 || v.collar < -1 ||
            (v.transition>=0 && size_t(v.transition)>=mesh.transitions.size()) || (v.collar>=0 && size_t(v.collar)>=mesh.collars.size())) return false;
        for (uint16_t i:mesh.indices) if (i>=mesh.vertices.size()) return false;
        for (const auto &t:mesh.transitions) {
            if (!std::isfinite(t.analytic)) return false;
            for (int i=0;i<3;++i) if (!std::isfinite(t.source[i].x) || !std::isfinite(t.source[i].y) || !std::isfinite(t.weight[i])) return false;
        }
        for (const auto &c:mesh.collars) {
            if (c.count<0 || c.count>4 || !std::isfinite(c.fade)) return false;
            for (int i=0;i<c.count;++i) if (!std::isfinite(c.parameter[i]) || !std::isfinite(c.weight[i])) return false;
        }
        return true;
    }
    struct Entry {
        WaterProjectedMesh::Mesh input;
        std::shared_ptr<const WaterProjectedMesh::Mesh> output;
        float level=0;
        bool cullTerrain=false,waveCage=false;
        size_t bytes=0;
    };
    size_t maxBytes_,maxEntries_,cachedBytes_=0;
    std::deque<Entry> entries_;
};
}
