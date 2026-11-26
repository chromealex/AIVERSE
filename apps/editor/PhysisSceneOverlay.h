// Editor-only projection of PHYSIS diagnostic quads into the real Scene view.
// The live Simulation is READ-ONLY; this is not gameplay or Box3D physics.
#pragma once
#include "Editor.h"
#include "apps/editor/core/PhysisOverlayTiles.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::editor {

inline PhysisSceneOverlayState& physisSceneOverlayState() {
    static PhysisSceneOverlayState state;
    return state;
}

inline void drawPhysisSceneOverlay(Editor& editor, const Viewport& vp,
                                   const render::Camera& camera) {
    auto& overlay = physisSceneOverlayState();
    if (!overlay.enabled || vp.pointsW <= 0 || vp.pointsH <= 0) return;
    // Live game PHYSIS first; otherwise the editor's «Живая природа» private
    // preview World, so the heatmap no longer needs a running play-test.
    const aiverse::World* source = nullptr;
    if (auto* sim = editor.sim(); sim && sim->world().resourceConst<physis::State>())
        source = &sim->world();
    if (!source) source = editor.physisNaturePreview().previewWorld();
    if (!source) return;
    const aiverse::World& world = *source;
    const auto* state = world.resourceConst<physis::State>();
    if (!state || state->settings.cellSizeMeters <= 0) return;
    updatePhysisOverlayTiles(overlay, world, camera.target.x, camera.target.z);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 clipA(vp.originX, vp.originY);
    const ImVec2 clipB(vp.originX + vp.pointsW, vp.originY + vp.pointsH);
    draw->PushClipRect(clipA, clipB, true);
    const auto project = [&](aiverse::Vec3 p, ImVec2& output) -> bool {
        const double x = p.x.toDouble(), y = p.y.toDouble(), z = p.z.toDouble();
        // Classic float rendering is not origin-rebased for enormous worlds yet.
        if (std::abs(x) > 1000000 || std::abs(y) > 1000000 ||
            std::abs(z) > 1000000) return false;
        render::float2 screen{};
        if (!camera.worldToScreen({float(x), float(y), float(z)},
                                   vp.pointsW, vp.pointsH, screen))
            return false;
        if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) return false;
        output = {vp.originX + screen.x, vp.originY + screen.y};
        return true;
    };
    for (const auto& tile : overlay.tiles) {
        ImVec2 points[4];
        bool valid = true;
        for (int i = 0; i < 4; ++i)
            if (!project(tile.corners[i], points[i])) { valid = false; break; }
        if (!valid) continue;
        draw->AddQuadFilled(points[0], points[1], points[2], points[3],
            IM_COL32(tile.color.r, tile.color.g, tile.color.b,
                     std::uint8_t(overlay.alpha * tile.color.a)));
    }

    if (overlay.drawVolumes) {
        // Only a handful of projected rings near the camera; hard cap prevents
        // accidentally drawing thousands of debug circles per editor frame.
        int drawn = 0;
        const auto ring = [&](aiverse::Vec3 center, aiverse::Fixed radius,
                              ImU32 color) {
            if (++drawn > 32 || radius <= aiverse::Fixed{}) return;
            ImVec2 points[48];
            for (int i = 0; i < 48; ++i) {
                const double a = double(i) * 6.283185307179586 / 48.0;
                const aiverse::Vec3 p{
                    center.x + aiverse::Fixed::fromRaw(std::int64_t(std::cos(a) * radius.raw)),
                    center.y,
                    center.z + aiverse::Fixed::fromRaw(std::int64_t(std::sin(a) * radius.raw))
                };
                if (!project(p, points[i])) return;
            }
            draw->AddPolyline(points, 48, color, ImDrawFlags_Closed, 1.5f);
        };
        eachPhysisOverlayVolume(world,state->tick,[&](Vec3 center,Fixed radius,physis::DebugColor c){
            ring(center,radius,IM_COL32(c.r,c.g,c.b,c.a));
        });
    }
    draw->PopClipRect();
}

} // namespace aiverse::editor
