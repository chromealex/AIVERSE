// Editor-only projection of PHYSIS diagnostic quads into the real Scene view.
// The live Simulation is READ-ONLY; this is not gameplay or Box3D physics.
#pragma once
#include "Editor.h"
#include "engine/physis/PhysisSceneGeometry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::editor {

struct PhysisSceneOverlayState {
    bool enabled = false;
    int radiusCells = 9;                 // bounded 6..24, around the camera target
    int stride = 1;                      // bounded 1..4
    float alpha = 0.35f;                 // 0.1..0.8
    physis::DiagnosticLayer layer = physis::DiagnosticLayer::TemperatureCelsius;
    physis::DebugPalette palette = physis::DebugPalette::Thermal;
    bool drawVolumes = true;
    bool dirty = true;
    const aiverse::World* lastWorld = nullptr; // identity comparison only, never dereferenced
    std::uint64_t lastTick = ~std::uint64_t(0);
    std::int32_t lastX = 0, lastZ = 0;
    int lastRadius = 0, lastStride = 0;
    physis::DiagnosticRaster raster;
    std::vector<physis::SceneDiagnosticTile> tiles;
};

inline PhysisSceneOverlayState& physisSceneOverlayState() {
    static PhysisSceneOverlayState state;
    return state;
}

inline void drawPhysisSceneOverlay(Editor& editor, const Viewport& vp,
                                   const render::RTSCamera& camera) {
    auto& overlay = physisSceneOverlayState();
    if (!overlay.enabled || vp.pointsW <= 0 || vp.pointsH <= 0) return;
    const auto* sim = editor.sim();
    if (!sim) return;
    const aiverse::World& world = sim->world();
    const auto* state = world.resourceConst<physis::State>();
    if (!state || state->settings.cellSizeMeters <= 0) return;

    const std::int64_t metres = state->settings.cellSizeMeters;
    // User's editor camera is float presentation data. PHYSIS grid selection
    // converts its centre explicitly; no float is fed into authoritative ECS.
    const auto axisCell = [&](float coord) -> std::int32_t {
        const double value = std::floor(double(coord) / double(metres));
        return std::int32_t(std::clamp(value, -1000000.0, 1000000.0));
    };
    const std::int32_t centerX = axisCell(camera.target.x);
    const std::int32_t centerZ = axisCell(camera.target.z);
    overlay.radiusCells = std::clamp(overlay.radiusCells, 6, 24);
    overlay.stride = std::clamp(overlay.stride, 1, 4);
    overlay.alpha = std::clamp(overlay.alpha, 0.1f, 0.8f);
    if (overlay.dirty || overlay.lastWorld != &world ||
        overlay.lastTick != state->tick ||
        overlay.lastX != centerX || overlay.lastZ != centerZ ||
        overlay.lastRadius != overlay.radiusCells ||
        overlay.lastStride != overlay.stride) {
        physis::DiagnosticRequest request;
        request.originX = centerX - overlay.radiusCells;
        request.originZ = centerZ - overlay.radiusCells;
        request.width = overlay.radiusCells * 2;
        request.height = overlay.radiusCells * 2;
        request.stride = overlay.stride;
        request.layer = overlay.layer;
        bool sampled = false;
        if (physis::diagnosticNeeds3DField(overlay.layer)) {
            physis::FieldSpatialIndex index(world);
            sampled = physis::sampleDiagnosticRasterIndexed(index, request, overlay.raster);
        } else {
            sampled = physis::sampleDiagnosticRaster(world, request, overlay.raster);
        }
        overlay.tiles = sampled ?
            physis::makeSceneDiagnosticTiles(world, overlay.raster, overlay.palette) :
            std::vector<physis::SceneDiagnosticTile>{};
        overlay.lastWorld = &world;
        overlay.lastTick = state->tick;
        overlay.lastX = centerX;
        overlay.lastZ = centerZ;
        overlay.lastRadius = overlay.radiusCells;
        overlay.lastStride = overlay.stride;
        overlay.dirty = false;
    }

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
        world.eachExistingConst<physis::WindZone>([&](Entity, const physis::WindZone& w) {
            ring(w.center, w.radiusMeters, IM_COL32(79, 202, 243, 220));
        });
        world.eachExistingConst<physis::StormEvent>([&](Entity, const physis::StormEvent& s) {
            if (physis::stormActive(s, state->tick))
                ring(s.center, s.outerRadiusMeters, IM_COL32(255, 187, 73, 220));
        });
        world.eachExistingConst<physis::GravityField>([&](Entity, const physis::GravityField& g) {
            if (g.enabled) ring(g.center, g.radiusMeters, IM_COL32(173, 124, 251, 220));
        });
    }
    draw->PopClipRect();
}

} // namespace aiverse::editor
