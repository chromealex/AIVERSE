// Immutable GI ray-query snapshot of individually replaceable world chunks.
// StaticShadowGeometry BVHs are built ONLY for changed chunks; publishing a snapshot
// rebuilds the small top-level BVH, sharing untouched chunk allocations.
// Suitable for terrain tiles, fallen-tree proxies, buildings and cave segments.
// This is render-presentation data, not an authoritative ECS state.
#pragma once
#include "GiTraceGeometry.h"
#include "GiUpdateScheduler.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

class GiChunkedGeometrySnapshot {
public:
    struct Chunk {
        uint64_t id = 0;
        GiBounds bounds{};
        std::shared_ptr<const StaticShadowGeometry> geometry;
        uint32_t triangles = 0;
    };
    struct QueryStats { uint32_t nodes = 0, chunks = 0; };

    GiTraceGeometryView traceView() const { return {this, &queryView}; }
    size_t chunkCount() const { return chunks_.size(); }
    size_t triangleCount() const {
        size_t total = 0;
        for (const auto& c : chunks_) total += c.triangles;
        return total;
    }

    // Stable immutable chunk enumeration; use geometry pointer identity for
    // incremental CPU BVH cache reuse across consecutive scene publications.
    // The callback MUST NOT mutate the borrowed source geometry.
    template<class Fn> void forEachChunk(Fn&& visitor) const {
        for (const auto& chunk : chunks_)
            visitor(chunk.id, chunk.bounds, *chunk.geometry);
    }

    // Strong immutable ownership for asynchronous GPU BLAS caches. The
    // callback receives shared_ptr<const ...> without extending its lifetime
    // past the caller unless it explicitly keeps a copy.
    template<class Fn> void forEachChunkShared(Fn&& visitor) const {
        for (const auto& chunk : chunks_)
            visitor(chunk.id, chunk.bounds, chunk.geometry);
    }

    // Offline/scene-change-only extraction for small GPU reference scenes.
    // Stable chunk-ID order; per-chunk geometry retains original surface/material IDs.
    // Production GPU BVH/clipmap path must NOT flatten large worlds per frame.
    template<class Fn> void forEachTriangle(Fn&& visitor) const {
        for (const auto& chunk : chunks_) {
            for (const auto& t : chunk.geometry->triangles) {
                const auto& albedos = chunk.geometry->albedos;
                const float3 color = t.surface < albedos.size()
                    ? albedos[t.surface] : float3{};
                visitor(t, color);
            }
        }
    }

    // Stable chunk ownership is useful when verifying incremental rebuilds.
    const StaticShadowGeometry* findChunk(uint64_t id) const {
        auto it = std::lower_bound(chunks_.begin(), chunks_.end(), id,
            [](const Chunk& c, uint64_t key) { return c.id < key; });
        return it != chunks_.end() && it->id == id ? it->geometry.get() : nullptr;
    }

    // Closest BVH hit, with surface normal and diffuse albedo from the chosen chunk.
    // This matches StaticShadowGeometry::distance, allowing a single GI tracer to
    // evaluate an immutable full-scene snapshot or this incremental chunk snapshot.
    float distance(float3 origin, float3 direction, float limit,
                   float3* normal = nullptr, float3* albedo = nullptr,
                   QueryStats* stats = nullptr) const {
        if (normal) *normal = {};
        if (albedo) *albedo = {};
        if (stats) *stats = {};
        if (nodes_.empty() || limit <= 0) return limit;
        float3 hitNormal{}, hitAlbedo{};
        const float closest = visit(0, origin, direction, limit,
                                    hitNormal, hitAlbedo, stats);
        if (normal) *normal = hitNormal;
        if (albedo) *albedo = hitAlbedo;
        return closest;
    }

private:
    friend class GiChunkedGeometryBuilder;
    static float queryView(const void* user, float3 origin, float3 direction,
                           float limit, float3* normal, float3* albedo) {
        return static_cast<const GiChunkedGeometrySnapshot*>(user)->distance(
            origin,direction,limit,normal,albedo);
    }
    struct Node {
        GiBounds bounds{};
        uint32_t left = 0, right = 0;
        uint32_t first = 0, count = 0; // count > 0 = leaf, first indexes order_
    };

    static GiBounds unite(GiBounds a, GiBounds b) {
        return {min3(a.min,b.min),max3(a.max,b.max)};
    }
    static float component(float3 v, int axis) {
        return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
    }
    static bool rayHitsBox(const GiBounds& bounds, float3 p, float3 dir, float limit) {
        float entry = 0, exit = limit;
        for (int axis = 0; axis < 3; ++axis) {
            const float o = component(p,axis), d = component(dir,axis);
            const float mn = component(bounds.min,axis), mx = component(bounds.max,axis);
            if (std::abs(d) < 1e-9f) {
                if (o < mn || o > mx) return false;
                continue;
            }
            float t0 = (mn-o)/d, t1 = (mx-o)/d;
            if (t0 > t1) std::swap(t0,t1);
            entry = std::max(entry,t0);
            exit = std::min(exit,t1);
            if (entry > exit) return false;
        }
        return true;
    }

    uint32_t build(uint32_t first, uint32_t count) {
        const uint32_t index = uint32_t(nodes_.size());
        nodes_.emplace_back();
        GiBounds bounds = chunks_[order_[first]].bounds;
        for (uint32_t i=1;i<count;++i)
            bounds = unite(bounds,chunks_[order_[first+i]].bounds);
        nodes_[index].bounds = bounds;
        if (count <= 4) {
            nodes_[index].first = first;
            nodes_[index].count = count;
            return index;
        }
        const float3 extent = bounds.max - bounds.min;
        int axis = extent.x > extent.y ? 0 : 1;
        if (extent.z > component(extent,axis)) axis = 2;
        auto start = order_.begin() + first, middle = start + count/2;
        std::nth_element(start,middle,start+count,
            [&](uint32_t ia,uint32_t ib) {
                const auto& a=chunks_[ia], &b=chunks_[ib];
                const float ca=component(a.bounds.min,axis)+component(a.bounds.max,axis);
                const float cb=component(b.bounds.min,axis)+component(b.bounds.max,axis);
                return ca == cb ? a.id < b.id : ca < cb;
            });
        const uint32_t left = build(first,count/2);
        const uint32_t right = build(first+count/2,count-count/2);
        nodes_[index].left = left;
        nodes_[index].right = right;
        return index;
    }

    float visit(uint32_t index,float3 p,float3 dir,float limit,
                float3& normal,float3& albedo,QueryStats* stats) const {
        const auto& node=nodes_[index];
        if (stats) ++stats->nodes;
        if (!rayHitsBox(node.bounds,p,dir,limit)) return limit;
        if (node.count) {
            for (uint32_t i=0;i<node.count;++i) {
                const auto& chunk=chunks_[order_[node.first+i]];
                if (!rayHitsBox(chunk.bounds,p,dir,limit)) continue;
                if (stats) ++stats->chunks;
                float3 hitNormal{},hitAlbedo{};
                const float hit=chunk.geometry->distance(p,dir,limit,&hitNormal,&hitAlbedo);
                if (hit < limit) {
                    limit = hit;
                    normal = hitNormal;
                    albedo = hitAlbedo;
                }
            }
            return limit;
        }
        limit=visit(node.left,p,dir,limit,normal,albedo,stats);
        return visit(node.right,p,dir,limit,normal,albedo,stats);
    }

    void rebuild() {
        order_.resize(chunks_.size());
        std::iota(order_.begin(),order_.end(),uint32_t(0));
        nodes_.clear();
        if (!order_.empty()) build(0,uint32_t(order_.size()));
    }

    std::vector<Chunk> chunks_; // sorted by stable id
    std::vector<uint32_t> order_;
    std::vector<Node> nodes_;
};

class GiChunkedGeometryBuilder {
public:
    // Replacing exactly one chunk rebuilds only that chunk's triangle BVH.
    // Empty triangle input removes the chunk. All source coordinates are world-space.
    void replace(uint64_t id, std::vector<StaticShadowGeometry::Triangle> triangles,
                 std::vector<float3> albedos) {
        if (triangles.empty()) { chunks_.erase(id); return; }
        if (triangles.size() > 1000000u)
            throw std::invalid_argument("GI geometry chunk triangle budget exceeded");
        for(const auto& v:albedos) {
            if (!finite(v) || v.x<0 || v.y<0 || v.z<0 ||
                v.x>10000 || v.y>10000 || v.z>10000)
                throw std::invalid_argument("invalid GI geometry chunk albedo");
        }
        float3 lo{1e30f,1e30f,1e30f}, hi{-1e30f,-1e30f,-1e30f};
        for (const auto& t:triangles) {
            if (!finite(t.a) || !finite(t.b) || !finite(t.c) ||
                (!albedos.empty() && t.surface >= albedos.size()))
                throw std::invalid_argument("invalid GI triangle");
            lo=min3(lo,min3(t.a,min3(t.b,t.c)));
            hi=max3(hi,max3(t.a,max3(t.b,t.c)));
        }
        // Build before publication. Failures leave the previous chunk untouched.
        auto geometry=std::make_shared<StaticShadowGeometry>();
        geometry->triangles=std::move(triangles);
        geometry->albedos=std::move(albedos);
        geometry->build();
        const uint32_t triangleCount=uint32_t(geometry->triangles.size());
        chunks_[id]={id,{lo,hi},std::move(geometry),triangleCount};
    }

    bool remove(uint64_t id) { return chunks_.erase(id)!=0; }
    size_t chunkCount() const { return chunks_.size(); }

    GiChunkedGeometrySnapshot publish() const {
        GiChunkedGeometrySnapshot snapshot;
        snapshot.chunks_.reserve(chunks_.size());
        for (const auto& [id, chunk]:chunks_) snapshot.chunks_.push_back(chunk);
        snapshot.rebuild();
        return snapshot;
    }

private:
    static bool finite(float3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    std::map<uint64_t,GiChunkedGeometrySnapshot::Chunk> chunks_;
};

} // namespace aiverse::render
