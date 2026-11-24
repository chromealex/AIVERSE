// PHYSIS nature presentation host model (2026-11-21).
//
// Turns one committed PHYSIS tick into renderer-neutral presentation data that
// TerraWeave grass, SpectraFX fire/smoke, lights and audio consume:
//   * smooth bilinear fields (fire, charred ground, wetness, snow, 3D wind);
//   * stable "flame points" sub-sampled inside the coarse PHYSIS cells, so a
//     128 m cell does not light up as one square: the front follows the
//     interpolated fire gradient and is pushed by the wind;
//   * tile clusters for audio loops and fire lights.
//
// Ownership: host-level adapter (engine/world), like PhysisSkyBridge. It only
// READS a PHYSIS World (live Simulation or a private preview World) and never
// installs, advances or writes PHYSIS, TerraWeave, NEREID or genre state.
// Floats are presentation-only; nothing here may be fed back into simulation.
// Charred ground is derived from authoritative HazardCell::fuel, so it is
// persistent, rollback-safe and needs no extra state.
#pragma once
#include "engine/physis/PhysisDisasters.h"
#include "engine/physis/PhysisWind.h"
#include "engine/physis/PhysisTornado.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

namespace aiverse::world {

struct NaturePresentationPolicy {
    // Region of interest in metres (usually around the camera). The frame is
    // bounded to maxRegionCells^2 PHYSIS cells regardless of map size.
    float centerX = 0, centerZ = 0;
    float radiusMeters = 160;
    std::int32_t maxRegionCells = 96;
    // Flame points: lattice spacing in metres; 0 = automatic (cell/6, >=1.5 m).
    float flameSpacingMeters = 0;
    float flameThreshold = 0.06f;   // interpolated fire needed for flames
    float smokeThreshold = 0.02f;   // smouldering smoke below the flame level
    std::uint32_t maxFlames = 192;
    std::uint32_t maxLatticeSamples = 24576; // hard CPU budget per build
    std::int32_t clusterCells = 2;  // audio/light cluster tile, in cells
    std::uint32_t maxClusters = 32;
    // Optional ground height for flame Y (scene metres); default: PHYSIS
    // Cell::elevation, bilinear.
    std::function<float(float x, float z)> groundHeight;
};

struct NatureGroundSample {
    float fire = 0;      // 0..1 current flame intensity (interpolated)
    float charred = 0;   // 0..1 burned fuel (1 - HazardCell::fuel)
    float wetness = 0;   // 0..1
    float snow = 0;      // 0..1 normalized snow cover (snowpack, 0.25 m = 1)
    float windX = 0, windZ = 0; // m/s incl. WindZone/WindShelter fields
    bool valid = false;
};

struct NatureFlamePoint {
    std::uint64_t key = 0;       // stable across ticks for the same spot
    float x = 0, y = 0, z = 0;   // scene metres
    float intensity = 0;         // 0..1 flame size (0 = smoke only)
    float smoke = 0;             // 0..1 smoke density
    float wetness = 0;           // 0..1: wet fuel => whiter, thicker smoke
};

struct NatureFireCluster {
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0;   // intensity-weighted centroid
    float intensity = 0;         // 0..1 (peak flame in the cluster)
    float burningArea = 0;       // m^2, approximate
    std::uint32_t flames = 0;
};

// A tornado in or near the region (PhysisTornado.h), for funnel FX / sound /
// water wind. Positions in scene metres; move = last tick's displacement.
struct NatureTornado {
    std::uint64_t key = 0;           // tornado entity (stable while it exists)
    float x = 0, y = 0, z = 0;       // ground centre
    float coreRadius = 1, outerRadius = 5, height = 50;
    float intensity = 0;             // 0..1 life envelope
    float tangentialWind = 0, updraft = 0; // current m/s (already * intensity)
    float moveX = 0, moveZ = 0;      // metres per tick
    bool clockwise = false;
    // TornadoNature (0 / false for tornadoes without one)
    float fireness = 0;              // 0..1 fire whirl share
    float spray = 0;                 // 0..1 water spray at the base
    float soil = 0;                  // m^3 of soil carried
    bool waterspout = false, overWater = false;
};

// A burning ember thrown by a fire whirl (TornadoEmberField).
struct NatureEmberPoint {
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
};

// Ground changed by tornadoes (GroundScourField), aggregated to blocks of
// `size` metres around (x, z). depth > 0 = scoured, < 0 = deposited.
struct NatureScourPoint {
    std::uint64_t key = 0;
    float x = 0, z = 0, size = 1;
    float depth = 0;
};

// One airborne / resting debris piece (TornadoDebrisField).
struct NatureDebrisPoint {
    std::uint64_t key = 0;
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;    // m/s (extrapolation between ticks)
    float size = 0.3f;               // metres, from the piece's area
    bool airborne = true;
};

struct NaturePresentationFrame {
    bool valid = false;
    std::uint64_t tick = 0;
    std::uint64_t seed = 0;
    float cellSize = 1;
    std::int32_t originX = 0, originZ = 0; // grid coords of field[0]
    std::int32_t width = 0, height = 0;
    std::vector<float> fire, charred, wetness, snow, windX, windZ, elevation;
    std::vector<std::uint8_t> present;
    std::vector<NatureFlamePoint> flames;     // sorted by key
    std::vector<NatureFireCluster> clusters;  // sorted by intensity desc, key
    float meanWindX = 0, meanWindZ = 0, maxWindSpeed = 0;
    float burningArea = 0;
    float flameSpacing = 0;
    bool hasDisasters = false;
    std::vector<NatureTornado> tornadoes;     // sorted by key
    std::vector<NatureDebrisPoint> debris;    // field order (stable ids)
    std::vector<NatureEmberPoint> embers;     // field order (stable ids)
    std::vector<NatureScourPoint> scour;      // sorted by key
    std::uint32_t scourRevision = 0;

    NatureGroundSample sample(float x, float z) const {
        NatureGroundSample out;
        if (!valid || width <= 0 || height <= 0) return out;
        // Bilinear on cell centres; clamp to region border.
        const float u = x / cellSize - 0.5f - float(originX);
        const float v = z / cellSize - 0.5f - float(originZ);
        if (!std::isfinite(u) || !std::isfinite(v)) return out;
        if (u < -1.f || v < -1.f || u > float(width) || v > float(height)) return out;
        const float cu = std::clamp(u, 0.f, float(width - 1));
        const float cv = std::clamp(v, 0.f, float(height - 1));
        const int x0 = int(std::floor(cu)), z0 = int(std::floor(cv));
        const int x1 = std::min(x0 + 1, width - 1), z1 = std::min(z0 + 1, height - 1);
        const float fx = cu - float(x0), fz = cv - float(z0);
        const float w[4] = {(1 - fx) * (1 - fz), fx * (1 - fz), (1 - fx) * fz, fx * fz};
        const int idx[4] = {z0 * width + x0, z0 * width + x1, z1 * width + x0, z1 * width + x1};
        float total = 0;
        for (int k = 0; k < 4; ++k) {
            if (!present[std::size_t(idx[k])]) continue;
            const auto i = std::size_t(idx[k]);
            total += w[k];
            out.fire += fire[i] * w[k];
            out.charred += charred[i] * w[k];
            out.wetness += wetness[i] * w[k];
            out.snow += snow[i] * w[k];
            out.windX += windX[i] * w[k];
            out.windZ += windZ[i] * w[k];
        }
        if (total <= 1e-6f) return out;
        const float inv = 1.f / total;
        out.fire *= inv; out.charred *= inv; out.wetness *= inv;
        out.snow *= inv; out.windX *= inv; out.windZ *= inv;
        out.valid = true;
        return out;
    }

    float elevationAt(float x, float z) const {
        if (!valid || width <= 0) return 0;
        const float u = std::clamp(x / cellSize - 0.5f - float(originX), 0.f, float(width - 1));
        const float v = std::clamp(z / cellSize - 0.5f - float(originZ), 0.f, float(height - 1));
        const int x0 = int(u), z0 = int(v);
        const int x1 = std::min(x0 + 1, width - 1), z1 = std::min(z0 + 1, height - 1);
        const float fx = u - float(x0), fz = v - float(z0);
        const auto e = [&](int cx, int cz) { return elevation[std::size_t(cz * width + cx)]; };
        return (e(x0, z0) * (1 - fx) + e(x1, z0) * fx) * (1 - fz) +
               (e(x0, z1) * (1 - fx) + e(x1, z1) * fx) * fz;
    }

    // Quantized appearance signature of a rectangle (metres). Grass/terrain
    // adapters rebuild a patch only when its burn state visibly changed.
    std::uint64_t signature(float minX, float minZ, float maxX, float maxZ) const {
        std::uint64_t h = 0x5048595349535347ull;
        const float xs[3] = {minX, (minX + maxX) * .5f, maxX};
        const float zs[3] = {minZ, (minZ + maxZ) * .5f, maxZ};
        for (float sx : xs)
            for (float sz : zs) {
                const auto s = sample(sx, sz);
                const std::uint64_t q = std::uint64_t(std::lround(s.charred * 15.f)) |
                    (std::uint64_t(std::lround(s.fire * 7.f)) << 4) |
                    (std::uint64_t(std::lround(s.snow * 7.f)) << 8);
                h = physis::mix64(h ^ q);
            }
        return h;
    }
};

namespace nature_detail {
inline float toF(Fixed v) { return float(v.toDouble()); }
inline float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; }
inline std::uint64_t key2(std::uint64_t salt, std::int64_t a, std::int64_t b) {
    std::uint64_t k = physis::mix64(salt ^ (std::uint64_t(a) * 0x9e3779b97f4a7c15ull) ^
                                    (std::uint64_t(b) * 0xc2b2ae3d27d4eb4full));
    return k ? k : 1;
}
inline std::int64_t floorDivF(float v, float d) { return std::int64_t(std::floor(v / d)); }
} // namespace nature_detail

// Build one frame from a committed PHYSIS World. Returns an invalid frame if
// PHYSIS is not installed. Cost is bounded by the policy, not by map size.
inline NaturePresentationFrame buildNaturePresentation(
        const World& world, const NaturePresentationPolicy& policy = {}) {
    using namespace nature_detail;
    NaturePresentationFrame f;
    const auto* state = world.resourceConst<physis::State>();
    if (!state || state->settings.cellSizeMeters <= 0) return f;
    if (!std::isfinite(policy.centerX) || !std::isfinite(policy.centerZ) ||
        !std::isfinite(policy.radiusMeters) || policy.radiusMeters <= 0 ||
        policy.maxRegionCells < 2 || policy.maxRegionCells > 512)
        throw std::invalid_argument("PHYSIS presentation region");
    const auto& s = state->settings;
    const float cs = float(s.cellSizeMeters);
    f.cellSize = cs;
    f.tick = state->tick;
    f.seed = s.seed;
    f.hasDisasters = world.resourceConst<physis::DisasterState>() != nullptr;

    // Region in grid coordinates, +1 cell border for bilinear sampling,
    // clamped to the PHYSIS grid and to maxRegionCells.
    const std::int64_t want = std::min<std::int64_t>(policy.maxRegionCells,
        std::int64_t(std::ceil(policy.radiusMeters * 2 / cs)) + 3);
    std::int64_t gx0 = floorDivF(policy.centerX, cs) - want / 2;
    std::int64_t gz0 = floorDivF(policy.centerZ, cs) - want / 2;
    gx0 = std::clamp<std::int64_t>(gx0, s.originX, std::int64_t(s.originX) + s.width - 1);
    gz0 = std::clamp<std::int64_t>(gz0, s.originZ, std::int64_t(s.originZ) + s.height - 1);
    const std::int64_t gx1 = std::min<std::int64_t>(gx0 + want, std::int64_t(s.originX) + s.width);
    const std::int64_t gz1 = std::min<std::int64_t>(gz0 + want, std::int64_t(s.originZ) + s.height);
    f.originX = std::int32_t(gx0);
    f.originZ = std::int32_t(gz0);
    f.width = std::int32_t(gx1 - gx0);
    f.height = std::int32_t(gz1 - gz0);
    if (f.width <= 0 || f.height <= 0) return f;
    const std::size_t n = std::size_t(f.width) * std::size_t(f.height);
    for (auto* v : {&f.fire, &f.charred, &f.wetness, &f.snow, &f.windX, &f.windZ, &f.elevation})
        v->assign(n, 0.f);
    f.present.assign(n, 0);

    double sumWx = 0, sumWz = 0;
    std::size_t windCount = 0;
    for (std::int32_t lz = 0; lz < f.height; ++lz)
        for (std::int32_t lx = 0; lx < f.width; ++lx) {
            const std::int32_t gx = f.originX + lx, gz = f.originZ + lz;
            const Entity e = physis::cellAt(world, gx, gz);
            if (e == kNullEntity) continue;
            const auto i = std::size_t(lz) * std::size_t(f.width) + std::size_t(lx);
            const auto* cell = world.tryGetConst<physis::Cell>(e);
            const auto* surface = world.tryGetConst<physis::Surface>(e);
            if (!cell) continue;
            f.present[i] = 1;
            f.elevation[i] = toF(cell->elevation);
            if (surface) {
                f.wetness[i] = clamp01(toF(surface->wetness));
                f.snow[i] = clamp01(toF(surface->snowpack) * 4.f);
            }
            if (const auto* hz = world.tryGetConst<physis::HazardCell>(e)) {
                f.fire[i] = clamp01(toF(hz->fire));
                f.charred[i] = clamp01(1.f - toF(hz->fuel));
            }
            // windWithStormsAt = cell atmosphere wind + WindZones - WindShelters
            // + storm / tornado vortices, at cell centre, 2 m above the PHYSIS
            // elevation (grass height).
            const Vec3 p{Fixed::fromInt(std::int64_t(gx) * s.cellSizeMeters) +
                             Fixed::fromRatio(s.cellSizeMeters, 2),
                         cell->elevation + Fixed::fromInt(2),
                         Fixed::fromInt(std::int64_t(gz) * s.cellSizeMeters) +
                             Fixed::fromRatio(s.cellSizeMeters, 2)};
            const Vec3 w = physis::windWithStormsAt(world, p);
            f.windX[i] = toF(w.x);
            f.windZ[i] = toF(w.z);
            sumWx += f.windX[i]; sumWz += f.windZ[i]; ++windCount;
            f.maxWindSpeed = std::max(f.maxWindSpeed,
                std::sqrt(f.windX[i] * f.windX[i] + f.windZ[i] * f.windZ[i]));
        }
    if (windCount) {
        f.meanWindX = float(sumWx / double(windCount));
        f.meanWindZ = float(sumWz / double(windCount));
    }
    f.valid = true;

    // Tornadoes touching the region (their outer ring may reach in from outside).
    {
        const float minX = float(f.originX) * cs, minZ = float(f.originZ) * cs;
        const float maxX = minX + float(f.width) * cs, maxZ = minZ + float(f.height) * cs;
        world.eachExistingConst<physis::TornadoTrack>([&](Entity e, const physis::TornadoTrack& t) {
            const auto* st = world.tryGetConst<physis::StormEvent>(e);
            if (!st || !physis::isTornado(*st) || t.intensity <= Fixed{}) return;
            NatureTornado n;
            n.key = e.packed();
            n.x = toF(st->center.x); n.y = toF(st->center.y); n.z = toF(st->center.z);
            n.coreRadius = toF(st->eyeRadiusMeters);
            n.outerRadius = toF(st->outerRadiusMeters);
            n.height = toF(st->heightMeters);
            n.intensity = clamp01(toF(t.intensity));
            n.tangentialWind = toF(st->tangentialWind);
            n.updraft = toF(st->updraft);
            n.moveX = toF(t.lastMove.x); n.moveZ = toF(t.lastMove.z);
            n.clockwise = st->clockwise;
            if (const auto* nat = world.tryGetConst<physis::TornadoNature>(e)) {
                n.fireness = clamp01(toF(nat->fireness));
                n.spray = clamp01(toF(nat->spray));
                n.soil = std::max(0.f, toF(nat->soil));
                n.waterspout = nat->waterspout;
                n.overWater = nat->overWater;
            }
            const float reach = n.outerRadius + policy.radiusMeters;
            if (n.x < minX - reach || n.x > maxX + reach || n.z < minZ - reach || n.z > maxZ + reach) return;
            if (!std::isfinite(n.x) || !std::isfinite(n.z)) return;
            if (policy.groundHeight) {
                const float g = policy.groundHeight(n.x, n.z);
                if (std::isfinite(g)) n.y = g;
            }
            f.tornadoes.push_back(n);
        });
        std::sort(f.tornadoes.begin(), f.tornadoes.end(),
                  [](const auto& a, const auto& b) { return a.key < b.key; });
        if (const auto* field = world.resourceConst<physis::TornadoDebrisField>())
            for (const auto& p : field->pieces) {
                NatureDebrisPoint d;
                d.key = p.id;
                d.x = toF(p.position.x); d.y = toF(p.position.y); d.z = toF(p.position.z);
                if (d.x < minX || d.x > maxX || d.z < minZ || d.z > maxZ) continue;
                d.vx = toF(p.velocity.x); d.vy = toF(p.velocity.y); d.vz = toF(p.velocity.z);
                d.size = std::clamp(std::sqrt(std::max(0.f, toF(p.areaSqMeters))) * 1.4f, 0.08f, 2.f);
                d.airborne = p.airborne;
                f.debris.push_back(d);
                if (f.debris.size() >= 256) break;
            }
        if (const auto* field = world.resourceConst<physis::TornadoEmberField>())
            for (const auto& p : field->embers) {
                NatureEmberPoint d;
                d.key = p.id;
                d.x = toF(p.position.x); d.y = toF(p.position.y); d.z = toF(p.position.z);
                if (d.x < minX || d.x > maxX || d.z < minZ || d.z > maxZ) continue;
                d.vx = toF(p.velocity.x); d.vy = toF(p.velocity.y); d.vz = toF(p.velocity.z);
                f.embers.push_back(d);
                if (f.embers.size() >= 256) break;
            }
        // Scour blocks (>= 2 m): mean change of the touched scour cells.
        if (const auto* field = world.resourceConst<physis::GroundScourField>();
            field && field->cellMeters > 0 && !field->cells.empty()) {
            f.scourRevision = field->revision;
            const std::int64_t cm = field->cellMeters;
            const std::int64_t per = std::max<std::int64_t>(1, (2 + cm - 1) / cm);   // cells per block
            const float block = float(cm * per);
            std::map<std::pair<std::int64_t, std::int64_t>, std::pair<float, int>> acc;
            for (const auto& c : field->cells) {
                const float cx = (float(c.x) + 0.5f) * float(cm), cz = (float(c.z) + 0.5f) * float(cm);
                if (cx < minX || cx > maxX || cz < minZ || cz > maxZ) continue;
                const auto bx = nature_detail::floorDivF(float(c.x), float(per));
                const auto bz = nature_detail::floorDivF(float(c.z), float(per));
                auto& slot = acc[{bz, bx}];
                slot.first += -toF(c.delta);
                ++slot.second;
                if (acc.size() > 4096) break;
            }
            for (const auto& [k, v] : acc) {
                NatureScourPoint p;
                p.key = key2(0x53434f5552ull, k.second, k.first);
                p.size = block;
                p.x = (float(k.second) + 0.5f) * block;
                p.z = (float(k.first) + 0.5f) * block;
                p.depth = v.first / float(per * per);
                if (std::abs(p.depth) < 0.01f) continue;
                f.scour.push_back(p);
            }
            std::sort(f.scour.begin(), f.scour.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
        }
    }
    if (!f.hasDisasters) return f;

    // Flame lattice. Only cells where fire (or a burning neighbour) can make
    // the interpolated value cross the smoke threshold are scanned.
    float spacing = policy.flameSpacingMeters > 0 ? policy.flameSpacingMeters
                                                  : std::max(1.5f, cs / 6.f);
    std::vector<std::uint8_t> hot(n, 0);
    std::size_t hotCells = 0;
    for (std::int32_t lz = 0; lz < f.height; ++lz)
        for (std::int32_t lx = 0; lx < f.width; ++lx) {
            float m = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = lx + dx, z = lz + dz;
                    if (x < 0 || z < 0 || x >= f.width || z >= f.height) continue;
                    m = std::max(m, f.fire[std::size_t(z * f.width + x)]);
                }
            if (m > policy.smokeThreshold) { hot[std::size_t(lz * f.width + lx)] = 1; ++hotCells; }
        }
    if (!hotCells) return f;
    // Level-of-detail lattice: spacing = base * 1.25^level. Keys include the
    // level, so identities stay stable while the burning area keeps one
    // level; a larger fire coarsens the lattice instead of churning a
    // truncated "top N" selection every tick.
    const float baseSpacing = spacing;
    const std::uint32_t budget = std::max<std::uint32_t>(64, policy.maxLatticeSamples);
    int level = 0;
    while (double(hotCells) * double(cs / spacing) * double(cs / spacing) > double(budget) &&
           level < 40) { ++level; spacing = baseSpacing * std::pow(1.25f, float(level)); }
    std::vector<NatureFlamePoint> candidates;
    const auto scan = [&](float step, int lod) {
        candidates.clear();
        for (std::int32_t lz = 0; lz < f.height; ++lz)
            for (std::int32_t lx = 0; lx < f.width; ++lx) {
                if (!hot[std::size_t(lz * f.width + lx)]) continue;
                const float minX = float(f.originX + lx) * cs, minZ = float(f.originZ + lz) * cs;
                const std::int64_t ix0 = std::int64_t(std::ceil(minX / step));
                const std::int64_t iz0 = std::int64_t(std::ceil(minZ / step));
                const std::int64_t ix1 = std::int64_t(std::ceil((minX + cs) / step));
                const std::int64_t iz1 = std::int64_t(std::ceil((minZ + cs) / step));
                for (std::int64_t iz = iz0; iz < iz1; ++iz)
                    for (std::int64_t ix = ix0; ix < ix1; ++ix) {
                        const std::uint64_t key =
                            key2(f.seed ^ 0x464c414d45ull ^ (std::uint64_t(lod) << 56), ix, iz);
                        // Stable jitter (+-40% of spacing) breaks the grid look.
                        const float jx = (float(key & 1023u) / 1023.f - .5f) * .8f;
                        const float jz = (float((key >> 10) & 1023u) / 1023.f - .5f) * .8f;
                        const float x = (float(ix) + jx) * step;
                        const float z = (float(iz) + jz) * step;
                        const auto g = f.sample(x, z);
                        if (!g.valid || g.fire <= policy.smokeThreshold) continue;
                        NatureFlamePoint p;
                        p.key = key;
                        p.x = x; p.z = z;
                        p.wetness = g.wetness;
                        // Flames live on the front: fire that still has fuel.
                        const float fuel = 1.f - g.charred;
                        const float flame = g.fire > policy.flameThreshold
                            ? (g.fire - policy.flameThreshold) /
                                  std::max(1e-3f, 1.f - policy.flameThreshold)
                            : 0.f;
                        p.intensity = clamp01(flame * (0.35f + 0.65f * std::sqrt(clamp01(fuel))));
                        p.smoke = clamp01(0.25f + 0.75f * g.fire) *
                                  (0.6f + 0.4f * g.wetness) *
                                  (p.intensity > 0 ? 1.f : 0.55f);
                        candidates.push_back(p);
                    }
            }
    };
    scan(spacing, level);
    while (candidates.size() > policy.maxFlames && level < 40) {
        const float ratio = std::sqrt(float(candidates.size()) / float(std::max<std::uint32_t>(1, policy.maxFlames)));
        const int steps = std::max(1, int(std::ceil(std::log(ratio) / std::log(1.25f))));
        level += steps;
        spacing = baseSpacing * std::pow(1.25f, float(level));
        scan(spacing, level);
    }
    f.flameSpacing = spacing;
    const float cellArea = spacing * spacing;
    // Keep the strongest within budget, deterministic tie-break by key.
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.intensity != b.intensity) return a.intensity > b.intensity;
        if (a.smoke != b.smoke) return a.smoke > b.smoke;
        return a.key < b.key;
    });
    if (candidates.size() > policy.maxFlames) candidates.resize(policy.maxFlames);
    for (auto& p : candidates) {
        p.y = policy.groundHeight ? policy.groundHeight(p.x, p.z) : f.elevationAt(p.x, p.z);
        if (!std::isfinite(p.y)) p.y = 0;
        if (p.intensity > 0) f.burningArea += cellArea;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.key < b.key; });
    f.flames = std::move(candidates);

    // Clusters for audio loops and lights.
    const float tile = cs * float(std::max(1, policy.clusterCells));
    struct Acc { std::int64_t tx, tz; double w = 0, x = 0, y = 0, z = 0; float peak = 0; std::uint32_t count = 0; };
    std::vector<Acc> acc;
    for (const auto& p : f.flames) {
        if (p.intensity <= 0) continue;
        const std::int64_t tx = floorDivF(p.x, tile), tz = floorDivF(p.z, tile);
        auto it = std::find_if(acc.begin(), acc.end(),
                               [&](const Acc& a) { return a.tx == tx && a.tz == tz; });
        if (it == acc.end()) { acc.push_back({tx, tz}); it = acc.end() - 1; }
        const double w = p.intensity;
        it->w += w; it->x += p.x * w; it->y += p.y * w; it->z += p.z * w;
        it->peak = std::max(it->peak, p.intensity);
        ++it->count;
    }
    for (const auto& a : acc) {
        if (a.w <= 0) continue;
        NatureFireCluster c;
        c.key = key2(f.seed ^ 0x434c5553ull, a.tx, a.tz);
        c.x = float(a.x / a.w); c.y = float(a.y / a.w); c.z = float(a.z / a.w);
        c.intensity = a.peak;
        c.flames = a.count;
        c.burningArea = float(a.count) * cellArea;
        f.clusters.push_back(c);
    }
    std::sort(f.clusters.begin(), f.clusters.end(), [](const auto& a, const auto& b) {
        if (a.intensity != b.intensity) return a.intensity > b.intensity;
        return a.key < b.key;
    });
    if (f.clusters.size() > policy.maxClusters) f.clusters.resize(policy.maxClusters);
    return f;
}

// Presentation-only blend of two consecutive frames' samples (render frames
// between fixed PHYSIS ticks). t in [0,1].
inline NatureGroundSample blendNatureSample(const NatureGroundSample& a,
                                            const NatureGroundSample& b, float t) {
    if (!a.valid) return b;
    if (!b.valid) return a;
    t = std::clamp(t, 0.f, 1.f);
    const auto l = [t](float x, float y) { return x + (y - x) * t; };
    NatureGroundSample r;
    r.valid = true;
    r.fire = l(a.fire, b.fire); r.charred = l(a.charred, b.charred);
    r.wetness = l(a.wetness, b.wetness); r.snow = l(a.snow, b.snow);
    r.windX = l(a.windX, b.windX); r.windZ = l(a.windZ, b.windZ);
    return r;
}

} // namespace aiverse::world
