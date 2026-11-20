// Lightweight, borrowed and non-owning GI ray-query interface.
// Works with an immutable StaticShadowGeometry BVH or a chunked geometry snapshot.
// Host must keep the target snapshot alive until after the last GI update using it.
#pragma once
#include "StaticPointShadows.h"

namespace aiverse::render {

struct GiTraceGeometryView {
    using Query = float (*)(const void* user, float3 origin, float3 direction,
                           float limit, float3* normal, float3* albedo);
    const void* source = nullptr;
    Query query = nullptr;

    bool valid() const { return source != nullptr && query != nullptr; }
    explicit operator bool() const { return valid(); }
    bool operator==(const GiTraceGeometryView&) const = default;

    float distance(float3 origin, float3 direction, float limit,
                   float3* normal = nullptr, float3* albedo = nullptr) const {
        if (!valid()) {
            if (normal) *normal = {};
            if (albedo) *albedo = {};
            return limit;
        }
        return query(source, origin, direction, limit, normal, albedo);
    }

    static GiTraceGeometryView fromStatic(const StaticShadowGeometry* geometry) {
        if (!geometry) return {};
        return {geometry, &queryStatic};
    }

private:
    static float queryStatic(const void* source,float3 origin,float3 direction,
                             float limit,float3* normal,float3* albedo) {
        return static_cast<const StaticShadowGeometry*>(source)->distance(
            origin,direction,limit,normal,albedo);
    }
};

} // namespace aiverse::render
