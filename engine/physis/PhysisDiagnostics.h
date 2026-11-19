// PHYSIS Stage 14: read-only, renderer-agnostic environmental debug heatmaps.
// CPU diagnostics only. Never advances simulation or creates authoritative ECS.
#pragma once
#include "PhysisSpatialIndex.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::physis {

enum class DiagnosticLayer : std::uint8_t {
    TemperatureCelsius, RelativeHumidity, PressureHpa, Rain, Snow,
    SurfaceWetness, SnowpackMeters, SurfaceIce, WindSpeedMetersPerSecond,
    VerticalWindMetersPerSecond, GravityMagnitude, Fire, Quake,
    LocalFloodMeters
};

struct DiagnosticRequest {
    // PHYSIS cell coordinates, not world metres.
    std::int32_t originX = 0, originZ = 0;
    std::int32_t width = 16, height = 16;
    std::int32_t stride = 1; // one sample per stride cells
    DiagnosticLayer layer = DiagnosticLayer::TemperatureCelsius;
};

struct DiagnosticRaster {
    std::int32_t originX = 0, originZ = 0, stride = 1;
    std::int32_t width = 0, height = 0;
    std::uint64_t tick = 0;
    DiagnosticLayer layer = DiagnosticLayer::TemperatureCelsius;
    std::vector<std::int64_t> rawValues; // Fixed::raw for each sample
    std::vector<std::uint8_t> valid; // 1 inside PHYSIS, 0 outside
    std::int64_t minRaw = 0, maxRaw = 0;
    std::uint32_t sampleCount = 0;
};

// For a visual-layer payload the caller decides the gradient/color palette.
// The scalar 0..65535 is normalized using only integer arithmetic, so overlays
// can work without direct dependency on gfx/Imgui or any game genre.
inline std::uint16_t diagnosticNormalized(const DiagnosticRaster& raster,
                                          std::size_t index) {
    if (index >= raster.rawValues.size() || index >= raster.valid.size() ||
        !raster.valid[index] || raster.maxRaw <= raster.minRaw)
        return 0;
    const std::int64_t delta = raster.rawValues[index] - raster.minRaw;
    const std::uint64_t range = static_cast<std::uint64_t>(
        raster.maxRaw - raster.minRaw);
    if (!range) return 0;
    // The environmental field values are deliberately bounded here to avoid
    // int64 overflow during normalization. This payload is presentation-only.
    const auto numerator = static_cast<std::uint64_t>(delta);
    return static_cast<std::uint16_t>(
        numerator >= range ? 65535u : numerator * 65535u / range);
}

inline Fixed diagnosticValue(const NatureFieldSample& sample,
                             DiagnosticLayer layer) {
    switch (layer) {
        case DiagnosticLayer::TemperatureCelsius:
            return sample.environment.temperature;
        case DiagnosticLayer::RelativeHumidity:
            return sample.environment.humidity;
        case DiagnosticLayer::PressureHpa:
            return sample.pressureHpa;
        case DiagnosticLayer::Rain:
            return sample.environment.rain;
        case DiagnosticLayer::Snow:
            return sample.environment.snow;
        case DiagnosticLayer::SurfaceWetness:
            return sample.environment.wetness;
        case DiagnosticLayer::SnowpackMeters:
            return sample.environment.snowpack;
        case DiagnosticLayer::SurfaceIce:
            return sample.environment.ice;
        case DiagnosticLayer::WindSpeedMetersPerSecond: {
            const Vec3 v = sample.windVelocity;
            const Fixed cap = Fixed::fromInt(200);
            const Vec3 bounded{fxClamp(v.x, -cap, cap),
                               fxClamp(v.y, -cap, cap), fxClamp(v.z, -cap, cap)};
            return fxSqrt(bounded.lengthSq());
        }
        case DiagnosticLayer::VerticalWindMetersPerSecond:
            return sample.windVelocity.y;
        case DiagnosticLayer::GravityMagnitude: {
            const Vec3 a = sample.gravityAcceleration;
            const Fixed cap = Fixed::fromInt(200);
            const Vec3 bounded{fxClamp(a.x, -cap, cap),
                               fxClamp(a.y, -cap, cap), fxClamp(a.z, -cap, cap)};
            return fxSqrt(bounded.lengthSq());
        }
        case DiagnosticLayer::Fire: return sample.hazards.fire;
        case DiagnosticLayer::Quake: return sample.hazards.quake;
        case DiagnosticLayer::LocalFloodMeters:
            return sample.hazards.waterDepth; // not NEREID's water depth
    }
    return {};
}

// Atmospheric/surface layers are direct ECS reads. They do not need to
// evaluate every WindZone, GravityField and StormEvent at each pixel.
inline bool diagnosticCellValue(const World& world, Entity cell,
                                DiagnosticLayer layer, Fixed& result) {
    const auto* air = world.tryGetConst<Atmosphere>(cell);
    const auto* surface = world.tryGetConst<Surface>(cell);
    if (!air || !surface) return false;
    switch (layer) {
        case DiagnosticLayer::TemperatureCelsius: result = air->temperature; break;
        case DiagnosticLayer::RelativeHumidity: result = air->humidity; break;
        case DiagnosticLayer::PressureHpa: result = air->pressure; break;
        case DiagnosticLayer::Rain: result = air->rain; break;
        case DiagnosticLayer::Snow: result = air->snow; break;
        case DiagnosticLayer::SurfaceWetness: result = surface->wetness; break;
        case DiagnosticLayer::SnowpackMeters: result = surface->snowpack; break;
        case DiagnosticLayer::SurfaceIce: result = surface->ice; break;
        case DiagnosticLayer::Fire:
        case DiagnosticLayer::Quake:
        case DiagnosticLayer::LocalFloodMeters: {
            const auto* hazard = world.tryGetConst<HazardCell>(cell);
            if (!hazard) return false;
            result = layer == DiagnosticLayer::Fire ? hazard->fire :
                     layer == DiagnosticLayer::Quake ? hazard->quake :
                     hazard->waterDepth;
            break;
        }
        default: return false; // vector field layers need the 3D sampler
    }
    return true;
}

inline bool diagnosticNeeds3DField(DiagnosticLayer layer) {
    return layer == DiagnosticLayer::WindSpeedMetersPerSecond ||
           layer == DiagnosticLayer::VerticalWindMetersPerSecond ||
           layer == DiagnosticLayer::GravityMagnitude;
}

// Bounded, pure, row-major read. Optional layer has no outside-world fallback:
// a valid result means the cell belongs to the climate grid.
template<class FieldSampler>
inline bool sampleDiagnosticRasterWithFieldSampler(const World& world,
                                   const DiagnosticRequest& request,
                                   DiagnosticRaster& output,
                                   FieldSampler&& sampleField) {
    const auto* state = world.resourceConst<State>();
    if (!state) return false;
    if (request.width <= 0 || request.height <= 0 || request.stride <= 0 ||
        request.stride > 1024)
        return false;
    const auto w = (std::int64_t(request.width) + request.stride - 1) /
                   request.stride;
    const auto h = (std::int64_t(request.height) + request.stride - 1) /
                   request.stride;
    if (w <= 0 || h <= 0 || w * h > 65536 ||
        std::int64_t(request.originX) + request.width - 1 >
            std::numeric_limits<std::int32_t>::max() ||
        std::int64_t(request.originZ) + request.height - 1 >
            std::numeric_limits<std::int32_t>::max())
        return false;

    DiagnosticRaster next;
    next.originX = request.originX;
    next.originZ = request.originZ;
    next.width = static_cast<std::int32_t>(w);
    next.height = static_cast<std::int32_t>(h);
    next.stride = request.stride;
    next.layer = request.layer;
    next.tick = state->tick;
    next.rawValues.resize(static_cast<std::size_t>(w * h));
    next.valid.resize(static_cast<std::size_t>(w * h));
    for (std::int64_t row = 0; row < h; ++row) {
        for (std::int64_t col = 0; col < w; ++col) {
            const std::int32_t cellX = static_cast<std::int32_t>(
                std::int64_t(request.originX) + col * request.stride);
            const std::int32_t cellZ = static_cast<std::int32_t>(
                std::int64_t(request.originZ) + row * request.stride);
            const Entity entity = cellAt(world, cellX, cellZ);
            if (entity == kNullEntity) continue;
            const auto* coord = world.tryGetConst<Cell>(entity);
            if (!coord) continue;
            Fixed value{};
            if (diagnosticNeeds3DField(request.layer)) {
                const auto meters = std::int64_t(state->settings.cellSizeMeters);
                const auto xMeters = std::int64_t(cellX) * meters + meters / 2;
                const auto zMeters = std::int64_t(cellZ) * meters + meters / 2;
                const auto maxMetres = std::numeric_limits<std::int64_t>::max() /
                                       Fixed::kOne / 2;
                if (xMeters < -maxMetres || xMeters > maxMetres ||
                    zMeters < -maxMetres || zMeters > maxMetres)
                    continue;
                NatureFieldSample sampleValue;
                const Vec3 query{Fixed::fromInt(xMeters),
                                 coord->elevation, Fixed::fromInt(zMeters)};
                if (!sampleField(query, sampleValue) ||
                    !sampleValue.hasAtmosphere)
                    continue;
                value = diagnosticValue(sampleValue, request.layer);
            } else {
                if (!diagnosticCellValue(world, entity, request.layer, value))
                    continue;
            }
            const std::size_t index = static_cast<std::size_t>(row * w + col);
            const auto raw = value.raw;
            next.rawValues[index] = raw;
            next.valid[index] = 1;
            if (!next.sampleCount) {
                next.minRaw = next.maxRaw = raw;
            } else {
                next.minRaw = std::min(next.minRaw, raw);
                next.maxRaw = std::max(next.maxRaw, raw);
            }
            ++next.sampleCount;
        }
    }
    output = std::move(next);
    return true;
}

// Backwards-compatible reference sampler, preserving all existing call sites.
inline bool sampleDiagnosticRaster(const World& world,
                                   const DiagnosticRequest& request,
                                   DiagnosticRaster& output) {
    return sampleDiagnosticRasterWithFieldSampler(world, request, output,
        [&](Vec3 position, NatureFieldSample& sample) {
            return sampleNatureField(world, position, sample);
        });
}

// Optional indexed sampler: direct weather/hazard cells still stay O(cells);
// wind/gravity layers query the transient snapshot-backed spatial broadphase.
// Index must be rebuilt when an updated world/tick is to be observed.
inline bool sampleDiagnosticRasterIndexed(const FieldSpatialIndex& index,
                                          const DiagnosticRequest& request,
                                          DiagnosticRaster& output) {
    return sampleDiagnosticRasterWithFieldSampler(index.world(), request, output,
        [&](Vec3 position, NatureFieldSample& sample) {
            return index.sample(position, sample);
        });
}

} // namespace aiverse::physis
