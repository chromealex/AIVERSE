#pragma once
#include "engine/physis/PhysisSceneGeometry.h"
#include <algorithm>
#include <cmath>

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

inline void updatePhysisOverlayTiles(PhysisSceneOverlayState& overlay, const World& world,
                                     float targetX, float targetZ) {
    const auto* state = world.resourceConst<physis::State>();
    if (!state || state->settings.cellSizeMeters <= 0) return;

    const std::int64_t metres = state->settings.cellSizeMeters;
    // User's editor camera is float presentation data. PHYSIS grid selection
    // converts its centre explicitly; no float is fed into authoritative ECS.
    const auto axisCell = [&](float coord) -> std::int32_t {
        const double value = std::floor(double(coord) / double(metres));
        return std::int32_t(std::clamp(value, -1000000.0, 1000000.0));
    };
    const std::int32_t centerX = axisCell(targetX);
    const std::int32_t centerZ = axisCell(targetZ);
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

}

template<class Ring>
inline void eachPhysisOverlayVolume(const World& world, std::uint64_t tick, Ring ring) {
    world.eachExistingConst<physis::WindZone>([&](Entity, const physis::WindZone& w) {
        ring(w.center, w.radiusMeters, physis::DebugColor{79, 202, 243, 220});
    });
    world.eachExistingConst<physis::StormEvent>([&](Entity, const physis::StormEvent& s) {
        if (physis::stormActive(s, tick))
            ring(s.center, s.outerRadiusMeters, physis::DebugColor{255, 187, 73, 220});
    });
    world.eachExistingConst<physis::GravityField>([&](Entity, const physis::GravityField& g) {
        if (g.enabled) ring(g.center, g.radiusMeters, physis::DebugColor{173, 124, 251, 220});
    });
}

} // namespace aiverse::editor
