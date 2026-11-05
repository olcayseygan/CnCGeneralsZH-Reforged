/* Reuse hash nodes across renders, never animated projected-surface values. */
#pragma once

#include "WaterProjectedMesh.h"
#include "WaterSurfaceModel.h"
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace WaterProjectedSampleCache {
template <class Value, class Epoch = uint32_t>
class Cache {
    static_assert(std::is_integral<Epoch>::value && std::is_unsigned<Epoch>::value,
        "The render epoch must be an unsigned integer");
    struct Entry {
        Value value;
        Epoch epoch;
    };
    std::unordered_map<WaterProjectedMesh::WorldKey, Entry, WaterProjectedMesh::WorldHash> entries;
    Epoch epoch = 0;
    size_t live = 0;
    const size_t limit;
public:
    explicit Cache(size_t maximum = WaterSurfaceModel::VertexBudget)
        : limit((std::min)(maximum, size_t(WaterSurfaceModel::VertexBudget))) {}

    void Clear() {
        entries.clear();
        epoch = 0;
        live = 0;
    }
    void BeginRender() {
        // Camera movement may leave old coordinates behind. Amortize their removal;
        // a stable view neither walks stale entries nor allocates/frees hash nodes.
        const size_t allowance = (std::max)((std::min)(size_t(4096), limit),
            live > limit / 2 ? limit : live * 2);
        if (entries.size() > allowance || epoch == (std::numeric_limits<Epoch>::max)())
            Clear();
        ++epoch;
        live = 0;
    }
    size_t Size() const {
        return entries.size();
    }
    size_t LiveSize() const {
        return live;
    }

    const Value *Find(const WaterProjectedMesh::WorldKey &key) const {
        const auto found = entries.find(key);
        return found != entries.end() && found->second.epoch == epoch ? &found->second.value : nullptr;
    }
    void Store(const WaterProjectedMesh::WorldKey &key, const Value &value) {
        if (live >= limit)
            return;
        const auto found = entries.find(key);
        if (found != entries.end()) {
            if (found->second.epoch != epoch) {
                found->second.value = value;
                found->second.epoch = epoch;
                ++live;
            }
        } else {
            // Preserve the old current-render admission order even after a camera
            // move fills resident storage. This pressure-only sweep occurs at most
            // once per render; ordinary BeginRender never scans the table.
            if (entries.size() == limit) {
                for (auto stale = entries.begin(); stale != entries.end();) {
                    if (stale->second.epoch != epoch)
                        stale = entries.erase(stale);
                    else
                        ++stale;
                }
            }
            entries.emplace(key, Entry{value, epoch});
            ++live;
        }
    }
};
}
