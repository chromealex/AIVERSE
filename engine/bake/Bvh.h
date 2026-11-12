// Triangle BVH for the light baker (agent-lightbake): binned SAH build, closest-hit and any-hit
// queries with optional alpha testing. World-space triangles, built once per bake, read-only after
// (shared by all baking threads).
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "../render/Math3D.h"

namespace aiverse::bake {

using render::float3;

struct BvhTri { float3 v0, e1, e2; uint32_t id; };   // id = caller's triangle index

struct BvhHit { float t = 0, u = 0, v = 0; uint32_t tri = UINT32_MAX; bool backface = false; };

class Bvh {
public:
    // `alphaTest(id, u, v)` returns true when the surface is present there (alpha-tested materials);
    // `masked[id]` says which triangles need it.
    void build(std::vector<BvhTri> tris);
    void setAlphaTest(std::vector<uint8_t> masked, std::function<bool(uint32_t, float, float)> test) {
        masked_ = std::move(masked); alphaTest_ = std::move(test);
    }
    bool intersect(float3 org, float3 dir, float tmax, BvhHit& hit) const;   // closest
    bool occluded(float3 org, float3 dir, float tmax) const;                 // any
    size_t triangles() const { return tris_.size(); }
    size_t nodes() const { return nodes_.size(); }

private:
    struct Node { float bmin[3], bmax[3]; uint32_t first = 0, count = 0; };   // count 0: inner, first = left child (right = left + 1)
    template <bool Any> bool trace(float3 org, float3 dir, float tmax, BvhHit* hit) const;
    std::vector<Node> nodes_;
    std::vector<BvhTri> tris_;
    std::vector<uint8_t> masked_;
    std::function<bool(uint32_t, float, float)> alphaTest_;
};

} // namespace aiverse::bake
