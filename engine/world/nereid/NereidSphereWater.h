// AIVERSE NEREID -- water bodies on spheres (planet seas): deterministic ECS sampling and buoyancy.
// A sphere body is a sea shell around a centre: points closer to the centre than the surface radius (and
// farther than the floor) are in the water. "Up" is the radial direction; the current is a rotation about
// an axis (metres per second at the surface). Fixed-point only; no renderer, genre or planet-module types.
// Plane bodies stay in NereidWater.h; a project may use both (separate contact components).
// Genre-independent: any native host (shooter, strategy, sim, editor tool) creates the bodies itself or with
// waterSphereFromJson / spawnWaterSpheres, then registers the systems (or NativeWaterPipelineConfig::enableSphereWater).
#pragma once
#include "engine/world/nereid/NereidBuoyancySystems.h"
#include "engine/core/FixedMath.h"
#include "engine/data/Json.h"
#include <stdexcept>
#include <vector>
#include <string>
#include <utility>

namespace aiverse::lib::nereid {

struct WaterSphereComponent {
    WaterKind kind = WaterKind::Ocean;
    Vec3 center{};                 // sphere centre, simulation space
    Fixed surfaceRadius = 1_fx;    // sea surface
    Fixed floorRadius{};           // deepest water (0: down to the centre)
    Vec3 axis{Fixed{}, 1_fx, Fixed{}};   // current rotation axis (need not be unit)
    Fixed currentSpeed{};          // m/s at the surface, eastward about `axis`; 0 = still water
    int32_t priority = 0;          // larger overrides smaller where shells overlap
    bool enabled = true;
};

// Optional gameplay swell on the same body: the surface radius moves by amplitude * sin(t / period +
// cycles * dot(up, direction)) turns. Deterministic Fixed; independent of the renderer's wave look.
struct WaterSphereWavesComponent {
    Fixed amplitude{};             // metres, 0 = flat sea
    Fixed period = 8_fx;           // seconds per swell
    Fixed cycles = 6_fx;           // swells around the sphere along `direction`
    Vec3 direction{1_fx, Fixed{}, Fixed{}};
};

// The sphere counterpart of WaterSample: depth / altitude are radial.
struct SphereWaterSample {
    Entity body = kNullEntity;
    WaterKind kind = WaterKind::Ocean;
    Vec3 up{};                     // unit radial direction at the probe
    Fixed surfaceRadius{}, floorRadius{};
    Fixed depth{};                 // below the surface, 0 above it
    Vec3 flow{};                   // tangential current
    Vec3 surfacePoint{};           // the probe projected radially onto the (wavy) surface
    int32_t priority = 0;
    bool found = false, submerged = false;
};
struct WaterSphereContactComponent {
    SphereWaterSample sample{};
    uint32_t sampledTick = 0;
};

inline bool validWaterSphere(const WaterSphereComponent& s) {
    return s.enabled && s.surfaceRadius > Fixed{} && s.floorRadius >= Fixed{} && s.floorRadius < s.surfaceRadius;
}

inline Vec3 sphereCross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

inline bool validWaterSphereWaves(const WaterSphereWavesComponent& w) {
    return w.amplitude >= Fixed{} && w.period > Fixed{} && w.cycles >= Fixed{} && w.direction.lengthSq().raw > 0;
}

// Swell offset of the surface at radial direction `up` and time `seconds` (0 without waves).
inline Fixed sphereWaveOffset(const WaterSphereWavesComponent* w, Vec3 up, Fixed seconds) {
    if (!w || w->amplitude.raw == 0 || !validWaterSphereWaves(*w)) return Fixed{};
    const Fixed along = up.dot(w->direction) / w->direction.length();
    return w->amplitude * fxSinTurns(seconds / w->period + w->cycles * along);
}

// Pure sampling of one sphere body (nothing found when the probe sits on the centre).
inline SphereWaterSample sampleWaterSphere(Entity entity, const WaterSphereComponent& s, Vec3 position,
                                           const WaterSphereWavesComponent* waves = nullptr, Fixed seconds = {}) {
    SphereWaterSample out;
    if (!validWaterSphere(s)) return out;
    const Vec3 r = position - s.center;
    const Fixed dist = r.length();
    if (dist.raw <= 0) return out;
    out.body = entity;
    out.kind = s.kind;
    out.up = r / dist;
    out.surfaceRadius = s.surfaceRadius + sphereWaveOffset(waves, out.up, seconds);
    if (out.surfaceRadius <= s.floorRadius) out.surfaceRadius = s.floorRadius + Fixed::fromRaw(1);
    out.floorRadius = s.floorRadius;
    out.depth = fxClamp(out.surfaceRadius - dist, Fixed{}, out.surfaceRadius - s.floorRadius);
    out.surfacePoint = s.center + out.up * out.surfaceRadius;
    out.priority = s.priority;
    out.found = true;
    out.submerged = dist < out.surfaceRadius && dist >= s.floorRadius;
    if (s.currentSpeed.raw != 0) {   // east = axis x up, scaled so the surface moves at currentSpeed
        const Vec3 east = sphereCross(s.axis, out.up);
        const Fixed len = east.length();
        if (len.raw > 0) out.flow = east * s.currentSpeed / len;
    }
    return out;
}

// Highest priority wins, then the larger surface, then the smallest Entity id. Only bodies whose shell
// (up to the surface) contains the probe or whose surface it is above are considered: the nearest sea.
inline SphereWaterSample sampleSphereWater(const World& world, Vec3 position, Fixed seconds = {}) {
    SphereWaterSample best;
    world.eachExistingConst<WaterSphereComponent>([&](Entity entity, const WaterSphereComponent& s) {
        if (!world.alive(entity)) return;
        const SphereWaterSample c = sampleWaterSphere(entity, s, position, world.tryGetConst<WaterSphereWavesComponent>(entity), seconds);
        if (!c.found) return;
        const bool better = !best.found || c.priority > best.priority ||
            (c.priority == best.priority && (c.submerged && !best.submerged)) ||
            (c.priority == best.priority && c.submerged == best.submerged &&
             (c.surfaceRadius > best.surfaceRadius ||
              (c.surfaceRadius == best.surfaceRadius && entity.packed() < best.body.packed())));
        if (better) best = c;
    });
    return best;
}

// Buoyancy along the radial up (the plane policy of computeWaterForces with "up" taken from the sample).
inline WaterForceComponent computeSphereWaterForces(const SphereWaterSample& sample, const WaterBuoyancyComponent& params,
                                                    Vec3 velocity) {
    WaterForceComponent out;
    if (!sample.found || !sample.submerged || !validBuoyancy(params)) return out;
    out.immersedFraction = fxClamp(sample.depth / params.immersionHeight, Fixed{}, 1_fx);
    const Vec3 relative = velocity - sample.flow;
    out.acceleration = sample.up * (params.buoyancyAcceleration * out.immersedFraction) +
                       relative * (-params.linearDrag * out.immersedFraction);
    return out;
}

// Read: WaterSphere + WaterProbe. Write: WaterSphereContact only. Place after the probe system.
inline void addSphereWaterSamplingSystem(Simulation& sim, std::string name) {
    sim.addSystem(std::move(name), [](Simulation& current) {
        World& world = current.world();
        if (!world.countExisting<WaterSphereContactComponent>()) return;
        auto* contacts = &world.pool<WaterSphereContactComponent>();   // detach the COW pool before writing
        const World& read = world;
        for (size_t i = 0; i < contacts->size(); ++i) {
            const Entity e = contacts->entityAt(i);
            if (!world.alive(e)) continue;
            const auto* probe = read.tryGetConst<WaterProbeComponent>(e);
            auto& contact = contacts->dataAt(i);
            const Fixed seconds = current.dt() * Fixed::fromInt(int64_t(current.tick()));
            contact.sample = probe ? sampleSphereWater(read, probe->position, seconds) : SphereWaterSample{};
            contact.sampledTick = current.tick();
        }
    });
}

// Entities with a sphere contact get their WaterForceComponent from it (radial buoyancy, drag to the current).
inline void addSphereWaterBuoyancySystem(Simulation& sim, std::string name) {
    sim.addSystem(std::move(name), [](Simulation& current) {
        World& world = current.world();
        if (!world.countExisting<WaterSphereContactComponent>()) return;
        auto* forces = world.prepareExistingWrite<WaterForceComponent>();
        if (!forces) return;
        const World& read = world;
        for (size_t i = 0; i < forces->size(); ++i) {
            const Entity e = forces->entityAt(i);
            if (!world.alive(e)) continue;
            const auto* contact = read.tryGetConst<WaterSphereContactComponent>(e);
            if (!contact) continue;   // plane contacts keep the plane buoyancy system
            const auto* params = read.tryGetConst<WaterBuoyancyComponent>(e);
            const auto* velocity = read.tryGetConst<WaterVelocityInputComponent>(e);
            auto& force = forces->dataAt(i);
            force = params && velocity && contact->sampledTick == current.tick()
                ? computeSphereWaterForces(contact->sample, *params, velocity->velocity) : WaterForceComponent{};
            force.sampledTick = current.tick();
        }
    });
}

inline void io(Archive& a, WaterSphereComponent& s) {
    a(s.kind, s.center, s.surfaceRadius, s.floorRadius, s.axis, s.currentSpeed, s.priority, s.enabled);
}
inline void io(Archive& a, WaterSphereWavesComponent& w) { a(w.amplitude, w.period, w.cycles, w.direction); }
inline void io(Archive& a, SphereWaterSample& s) {
    a(s.body, s.kind, s.up, s.surfaceRadius, s.floorRadius, s.depth, s.flow, s.surfacePoint, s.priority, s.found, s.submerged);
}
inline void io(Archive& a, WaterSphereContactComponent& c) { a(c.sample, c.sampledTick); }

inline void registerNereidSphereWaterPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterSphereComponent>("nereid.water_sphere", 1);
    sim.registerPersistentComponent<WaterSphereContactComponent>("nereid.water_sphere_contact", 2);
    sim.registerPersistentComponent<WaterSphereWavesComponent>("nereid.water_sphere_waves", 1);
}

// Shared JSON form for any host: true | {"depth", "current", "axis": [x,y,z], "priority",
// "waves": {"amplitude", "period", "cycles", "direction": [x,y,z]}}. `surfaceRadius` and `center` come from the
// host (a planet's radius + sea level, a pond sphere, a gas giant...). Throws readable errors.
struct WaterSphereSpec {
    WaterSphereComponent body{};
    WaterSphereWavesComponent waves{};
    bool hasWaves = false;
};
inline WaterSphereSpec waterSphereFromJson(const Json& j, Vec3 center, Fixed surfaceRadius, const std::string& where) {
    WaterSphereSpec out;
    WaterSphereComponent& w = out.body;
    auto vec = [&](const Json& a, Vec3 def) { return a.isArray() && a.size() == 3 ? Vec3{a[0].fx(), a[1].fx(), a[2].fx()} : def; };
    w.center = center;
    w.surfaceRadius = surfaceRadius;
    if (surfaceRadius <= Fixed{}) throw std::runtime_error(where + ": the sea surface radius must be > 0");
    const Fixed depth = j.isObject() ? j.getFx("depth", surfaceRadius * Fixed::fromRatio(1, 4)) : surfaceRadius * Fixed::fromRatio(1, 4);
    if (depth.raw <= 0 || depth > surfaceRadius) throw std::runtime_error(where + ": depth must be 0 < depth <= surface radius");
    w.floorRadius = surfaceRadius - depth;
    if (j.isObject()) {
        const std::string kind = j.getStr("kind", "ocean");
        w.kind = kind == "lake" ? WaterKind::Lake : WaterKind::Ocean;
        w.currentSpeed = j.getFx("current");
        w.axis = vec(j["axis"], w.axis);
        w.priority = int32_t(j.getInt("priority", 0));
        const Json& wj = j["waves"];
        if (wj.isObject()) {
            out.hasWaves = true;
            out.waves.amplitude = wj.getFx("amplitude", 1_fx);
            out.waves.period = wj.getFx("period", 8_fx);
            out.waves.cycles = wj.getFx("cycles", 6_fx);
            out.waves.direction = vec(wj["direction"], out.waves.direction);
            if (!validWaterSphereWaves(out.waves) || out.waves.amplitude >= depth)
                throw std::runtime_error(where + ".waves: amplitude 0..depth, period > 0, cycles >= 0, direction non-zero");
        }
    }
    if (w.axis.lengthSq().raw <= 0) throw std::runtime_error(where + ": axis must be non-zero");
    return out;
}

// Deterministic creation order (call at setup or a structural barrier).
inline std::vector<Entity> spawnWaterSpheres(World& world, const std::vector<WaterSphereSpec>& specs) {
    std::vector<Entity> out;
    out.reserve(specs.size());
    for (const auto& s : specs) {
        const Entity e = world.create();
        world.add<WaterSphereComponent>(e, s.body);
        if (s.hasWaves) world.add<WaterSphereWavesComponent>(e, s.waves);
        out.push_back(e);
    }
    return out;
}

} // namespace aiverse::lib::nereid
