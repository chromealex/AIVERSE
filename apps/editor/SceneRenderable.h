// Genre-independent render submissions for editor Scene View.
// The extractor owns the lifetime of referenced geometry until submit().
#pragma once
#include "../../engine/render/Renderer3D.h"
#include "../../engine/view/View.h"
#include <cstdint>
#include <limits>
#include <cmath>
#include <algorithm>
#include <vector>

namespace aiverse::editor {

struct SceneRenderable {
    uint64_t entity = 0;                 // authored source reference; 0 = non-document entity
    int runtimeActor = -1;              // FPS runtime actor slot index (ECS mirrored)
    uint64_t runtimeObject = 0;         // FPS ECS dynamic-object kind/id key
    render::mat4 world{};
    render::float4 tint{1, 1, 1, 1};
    const render::Mesh* mesh = nullptr;   // editor/debug primitives
    const render::Model* model = nullptr; // authored/skinned geometry
    bool selected = false;
    bool hovered = false;
    // Optional world-space selection bounds. Extractors should provide these
    // for picking; render-only effects may leave them disabled.
    render::float3 boundsMin{};
    render::float3 boundsMax{};
    bool pickable = false;
};

// Shared world-space ray/AABB hit test. Returns the closest object index,
// or -1; does not depend on a genre-specific ECS or camera implementation.
inline int pickSceneRenderable(const std::vector<SceneRenderable>& objects,
                               render::float3 origin, render::float3 direction) {
    // Reject degenerate/invalid rays before entering the slab test. NaNs can
    // otherwise silently select an arbitrary object.
    const float len2 = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        !std::isfinite(len2) || len2 < 1e-12f) return -1;
    int nearest = -1;
    float nearestT = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto& o = objects[i];
        if (!o.pickable) continue;
        float nearT = 0.0f, farT = nearestT;
        const float mins[3] = {o.boundsMin.x, o.boundsMin.y, o.boundsMin.z};
        const float maxs[3] = {o.boundsMax.x, o.boundsMax.y, o.boundsMax.z};
        const float starts[3] = {origin.x, origin.y, origin.z};
        const float dirs[3] = {direction.x, direction.y, direction.z};
        bool hit = true;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(mins[axis]) || !std::isfinite(maxs[axis]) || mins[axis] > maxs[axis]) { hit = false; break; }
            if (std::abs(dirs[axis]) < 1e-8f) {
                if (starts[axis] < mins[axis] || starts[axis] > maxs[axis]) { hit = false; break; }
            } else {
                const float inv = 1.0f / dirs[axis];
                const float a = (mins[axis] - starts[axis]) * inv;
                const float b = (maxs[axis] - starts[axis]) * inv;
                nearT = std::max(nearT, std::min(a, b));
                farT = std::min(farT, std::max(a, b));
                if (nearT > farT) { hit = false; break; }
            }
        }
        if (hit && farT >= 0.0f && nearT < nearestT) { nearestT = nearT; nearest = int(i); }
    }
    return nearest;
}


inline void submitSceneRenderables(render::Renderer3D& renderer,
                                   const std::vector<SceneRenderable>& objects) {
    for (const auto& object : objects) {
        if (object.model) {
            render::ModelDraw draw;
            draw.model = object.model;
            draw.world = object.world;
            draw.tint = object.tint;
            if (object.selected) draw.outline = {0.25f, 0.62f, 1.0f, 0.035f};
            else if (object.hovered) draw.outline = {0.75f, 0.8f, 0.85f, 0.025f};
            ::aiverse::View::submitModel(renderer,draw);
        } else if (object.mesh) {
            ::aiverse::view::MeshObject packet;
            packet.mesh=object.mesh;
            packet.world=object.world;
            packet.color=object.tint;
            packet.selectionOutline=object.selected || object.hovered;
            packet.selectionColor=object.selected
                ? render::float4{0.25f, 0.62f, 1.0f, 1.0f}
                : render::float4{0.75f, 0.8f, 0.85f, 1.0f};
            ::aiverse::View::submitMesh(renderer,packet);
            if (!renderer.sceneSelectionOutlineEnabled() && object.pickable &&
                (object.selected || object.hovered)) {
                const auto lo = object.boundsMin, hi = object.boundsMax;
                const render::float3 corners[8] = {
                    {lo.x,lo.y,lo.z}, {hi.x,lo.y,lo.z}, {hi.x,lo.y,hi.z}, {lo.x,lo.y,hi.z},
                    {lo.x,hi.y,lo.z}, {hi.x,hi.y,lo.z}, {hi.x,hi.y,hi.z}, {lo.x,hi.y,hi.z}
                };
                constexpr int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},
                                              {4,5},{5,6},{6,7},{7,4},
                                              {0,4},{1,5},{2,6},{3,7}};
                const uint32_t color = object.selected ? render::rgba(64,158,255) : render::rgba(180,190,205);
                for (const auto& edge:edges) {
                    ::aiverse::View::submitLine(renderer,
                        {corners[edge[0]],corners[edge[1]],color});
                }
            }
        }
    }
}

} // namespace aiverse::editor
