// PHYSIS tornado nature (2026-11-22): how a tornado interacts with the ground
// under it. Lives on the tornado entity next to TornadoTrack (PhysisTornado.h)
// and is used by advanceTornadoes:
//   * fire  — the vortex draws fire (heat + burning fuel) from PHYSIS cells
//             under its core and ring into an accumulated heat mass; the heat
//             gives the fire-whirl share `fireness` 0..1 (saturating, so a small
//             fire gives a weak effect) and decays without new fire. A fiery
//             vortex no longer blows the fire out in its core, ignites fuel on
//             its path (own RNG), throws embers that ignite where they land
//             (TornadoEmberField) and burns host entities through
//             GameModule::environmentDamage;
//   * water — a land tornado over open water (host GameModule::
//             environmentWaterDepth, or PHYSIS local flood depth) loses vigor
//             and dissipates; a waterspout (born over water, or authored) is
//             weaker, raises spray and dies quickly over land; fire is
//             quenched over water;
//   * soil  — the core scours the ground (GroundScourField, sparse, metres),
//             carries the soil and drops it in a band around the core.
// Opt-in per entity: a tornado without TornadoNature keeps the exact
// 2026-11-22 behaviour. Map tornadoes always get one (PhysisTornadoAssets.h).
// Fixed-point only; persistent and hashed (physis.tornado_nature,
// physis.tornado_embers, physis.ground_scour).
#pragma once
#include "PhysisStorm.h"
#include "engine/core/Archive.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace aiverse::physis {

// Authored kind: Auto = decided by the surface under the vortex when it
// first becomes active (water -> waterspout), Land / Water force it.
enum class TornadoSurfaceKind : std::uint8_t { Auto = 0, Land = 1, Water = 2 };

struct TornadoNature {
    // ---- surface / water
    TornadoSurfaceKind kind = TornadoSurfaceKind::Auto;
    bool classified = false;          // type decided
    bool waterspout = false;          // resolved type
    bool overWater = false;           // last tick
    bool dissipated = false;          // vigor ran out: the vortex is gone
    Fixed vigor = Fixed::fromInt(1);  // 0..1 surface factor on the wind
    Fixed waterThresholdMeters = Fixed::fromRatio(1, 5);
    Fixed waterDecayPerTick{};        // land tornado over water, vigor/tick
    Fixed recoverPerTick{};           // on its own surface, vigor/tick
    Fixed spoutStrength = Fixed::fromRatio(11, 20); // waterspout wind scale
    Fixed spoutLandDecayPerTick{};    // waterspout over land, vigor/tick
    Fixed spray{};                    // 0..1 published (waterspout / over water)
    std::uint32_t surfaceTicks = 0;   // ticks on the current surface
    // ---- fire
    bool fire = true;
    Fixed heat{};                     // accumulated drawn fire, burning m^2
    Fixed fireness{};                 // 0..1 = heat / (heat + halfHeat)
    Fixed absorbed{};                 // fire drawn on the last tick, burning m^2
    Fixed intakePerTick{};            // share of the fire under the vortex drawn
    Fixed heatDecayPerTick{};         // relative heat loss per tick
    Fixed quenchPerTick{};            // extra relative heat loss over water
    Fixed halfHeat = Fixed::fromInt(60);
    Fixed ignitePerTick{};            // chance per fuel cell at fireness 1
    Fixed igniteStrength = Fixed::fromRatio(3, 5);
    Fixed emberPerTick{};             // embers thrown per tick at fireness 1
    Fixed emberAccumulator{};
    Fixed emberIgnite = Fixed::fromRatio(1, 2); // chance a landing ember ignites
    std::uint32_t maxEmbers = 48;
    std::uint32_t emberLifeTicks = 160;
    Fixed heatDamagePerTick{};        // at fireness 1 in the core
    std::uint64_t rng = 0x46495245ull;
    // ---- soil
    Fixed scourPerTick{};             // metres removed per tick at intensity 1
    Fixed maxScourMeters = Fixed::fromRatio(3, 5);
    Fixed depositPerTick{};           // share of carried soil dropped per tick
    Fixed soilCapacity = Fixed::fromInt(40); // m^3
    Fixed soil{};                     // m^3 carried
};

// Reasonable defaults for a tick rate (all authored values are per second).
inline TornadoNature defaultTornadoNature(std::uint32_t ticksPerSecond = 20) {
    const std::int64_t r = std::max<std::int64_t>(1, ticksPerSecond);
    TornadoNature n;
    n.waterDecayPerTick = Fixed::fromRatio(2, 25) / r;      // ~12 s over water
    n.recoverPerTick = Fixed::fromRatio(1, 10) / r;
    n.spoutLandDecayPerTick = Fixed::fromRatio(3, 5) / r;   // ~2 s over land
    n.intakePerTick = Fixed::fromRatio(1, 2) / r;
    n.heatDecayPerTick = Fixed::fromRatio(1, 5) / r;
    n.quenchPerTick = Fixed::fromRatio(3, 2) / r;
    n.ignitePerTick = Fixed::fromRatio(2, 5) / r;
    n.emberPerTick = Fixed::fromInt(6) / r;
    n.emberLifeTicks = std::uint32_t(8 * r);
    n.heatDamagePerTick = Fixed::fromInt(4) / r;
    n.scourPerTick = Fixed::fromRatio(1, 25) / r;           // 4 cm/s
    n.depositPerTick = Fixed::fromRatio(1, 4) / r;
    return n;
}

inline void io(Archive& a, TornadoNature& v) {
    a(v.kind, v.classified, v.waterspout, v.overWater, v.dissipated, v.vigor,
      v.waterThresholdMeters, v.waterDecayPerTick, v.recoverPerTick,
      v.spoutStrength, v.spoutLandDecayPerTick, v.spray, v.surfaceTicks,
      v.fire, v.heat, v.fireness, v.absorbed, v.intakePerTick,
      v.heatDecayPerTick, v.quenchPerTick, v.halfHeat, v.ignitePerTick,
      v.igniteStrength, v.emberPerTick, v.emberAccumulator, v.emberIgnite,
      v.maxEmbers, v.emberLifeTicks, v.heatDamagePerTick, v.rng,
      v.scourPerTick, v.maxScourMeters, v.depositPerTick, v.soilCapacity, v.soil);
}

// Burning embers thrown by fire whirls (same point-mass model as debris).
struct TornadoEmber {
    std::uint32_t id = 0;
    Entity owner{};
    Vec3 position{}, velocity{};
    std::uint32_t ageTicks = 0, lifeTicks = 160;
};
struct TornadoEmberField {
    std::vector<TornadoEmber> embers;   // creation order
    std::uint32_t nextId = 1;
    std::uint32_t ignitions = 0;        // cells set on fire by landed embers
};
inline void io(Archive& a, TornadoEmber& v) {
    a(v.id, v.owner, v.position, v.velocity, v.ageTicks, v.lifeTicks);
}
inline void io(Archive& a, TornadoEmberField& v) { a(v.embers, v.nextId, v.ignitions); }

// Ground changed by tornadoes: sparse cells of `cellMeters` (x/z floor
// division), sorted by (z, x). delta < 0 = scoured, > 0 = deposited, metres.
// Hosts that own terrain (TerraWeave Earthworks, a heightfield) may apply it;
// presentation draws it as a scar on the ground.
struct GroundScourCell {
    std::int32_t x = 0, z = 0;
    Fixed delta{};
};
struct GroundScourField {
    std::int32_t cellMeters = 1;
    std::vector<GroundScourCell> cells;
    std::uint32_t revision = 0;         // bumps on every change
    static constexpr std::size_t kMaxCells = 65536;
};
inline void io(Archive& a, GroundScourCell& v) { a(v.x, v.z, v.delta); }
inline void io(Archive& a, GroundScourField& v) { a(v.cellMeters, v.cells, v.revision); }

namespace scour_detail {
inline bool less(const GroundScourCell& c, std::int32_t x, std::int32_t z) {
    return c.z != z ? c.z < z : c.x < x;
}
inline std::int64_t floorDivFixed(Fixed v, std::int32_t cell) {
    const std::int64_t i = v.floorInt();
    std::int64_t q = i / cell;
    if ((i % cell) != 0 && i < 0) --q;
    return q;
}
} // namespace scour_detail

inline const GroundScourCell* findScour(const GroundScourField& f, std::int32_t x, std::int32_t z) {
    const auto it = std::lower_bound(f.cells.begin(), f.cells.end(), 0,
        [&](const GroundScourCell& c, int) { return scour_detail::less(c, x, z); });
    return it != f.cells.end() && it->x == x && it->z == z ? &*it : nullptr;
}

// Ground change at a point in metres (0 where no tornado touched the ground).
inline Fixed groundScourAt(const World& world, Fixed x, Fixed z) {
    const auto* f = world.resourceConst<GroundScourField>();
    if (!f || f->cellMeters <= 0) return Fixed{};
    const auto gx = scour_detail::floorDivFixed(x, f->cellMeters);
    const auto gz = scour_detail::floorDivFixed(z, f->cellMeters);
    const auto* c = findScour(*f, std::int32_t(gx), std::int32_t(gz));
    return c ? c->delta : Fixed{};
}

// Adds `change` to cells (any order, duplicates summed), clamping each cell to
// [-limit, +limit]. Returns the volume actually applied (sum of applied deltas
// times cell area). New cells beyond kMaxCells are dropped.
struct ScourChange { std::int32_t x = 0, z = 0; Fixed change{}; };
inline Fixed applyScour(GroundScourField& f, std::vector<ScourChange> changes, Fixed limit) {
    if (changes.empty()) return Fixed{};
    std::sort(changes.begin(), changes.end(), [](const ScourChange& a, const ScourChange& b) {
        return a.z != b.z ? a.z < b.z : a.x < b.x;
    });
    const Fixed area = Fixed::fromInt(std::int64_t(f.cellMeters) * f.cellMeters);
    Fixed applied{};
    std::vector<GroundScourCell> fresh;
    for (std::size_t i = 0; i < changes.size();) {
        ScourChange c = changes[i++];
        while (i < changes.size() && changes[i].x == c.x && changes[i].z == c.z) c.change += changes[i++].change;
        if (c.change == Fixed{}) continue;
        auto it = std::lower_bound(f.cells.begin(), f.cells.end(), 0,
            [&](const GroundScourCell& g, int) { return scour_detail::less(g, c.x, c.z); });
        Fixed before{};
        GroundScourCell* cell = nullptr;
        if (it != f.cells.end() && it->x == c.x && it->z == c.z) { cell = &*it; before = it->delta; }
        const Fixed after = fxClamp(before + c.change, -limit, limit);
        if (after == before) continue;
        applied += (after - before) * area;
        if (cell) cell->delta = after;
        else if (f.cells.size() + fresh.size() < GroundScourField::kMaxCells)
            fresh.push_back({c.x, c.z, after});
        else applied -= (after - before) * area;
    }
    if (!fresh.empty()) {
        const auto mid = f.cells.size();
        f.cells.insert(f.cells.end(), fresh.begin(), fresh.end());   // fresh is sorted
        std::inplace_merge(f.cells.begin(), f.cells.begin() + std::ptrdiff_t(mid), f.cells.end(),
            [](const GroundScourCell& a, const GroundScourCell& b) {
                return a.z != b.z ? a.z < b.z : a.x < b.x;
            });
    }
    ++f.revision;
    return applied;
}

} // namespace aiverse::physis
