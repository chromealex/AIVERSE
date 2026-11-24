// PHYSIS tornadoes from map data: the "physis.tornadoes" array (2026-11-22).
// Shared by the runtime installer (PhysisProjectConfig.h) and the editor/MCP
// validator, so both accept exactly the same documents. Authored per-second
// values are converted to per-tick values with the simulation tick rate.
//
//   "tornadoes": [{
//     "id": "twister",                         // stable ASCII id (spawn order)
//     "center": [20, 0, 12],                   // metres, or [x, z]
//     "coreRadiusMeters": 3,                   // peak-wind ring (StormEvent eye)
//     "outerRadiusMeters": 14, "heightMeters": 60,
//     "tangentialWind": 45, "updraft": 18, "radialInflow": 6,   // m/s
//     "clockwise": false,
//     "startTick": 40, "durationTicks": 1800,  // or startSeconds / durationSeconds
//     "rampSeconds": 5,                        // spin-up and decay (or rampTicks)
//     "path": {"mode": "wander", "speed": 2, "turnDegreesPerSecond": 30,
//              "seed": 3, "heading": 0, "bounds": [4, 4, 60, 44]},
//          // {"mode": "waypoints", "speed": 2.5, "points": [[10,10],[50,30]], "loop": true}
//          // {"mode": "drift", "velocity": [1, 0, 0]}     (m/s)
//     "damagePerSecond": 8, "damageRadiusMeters": 6,
//     "fire": {"suppression": 0.6, "fanning": 0.4,        // shares per second
//              // fire whirl (TornadoNature, on by default; "whirl": false = off):
//              "intake": 0.5, "decay": 0.2, "halfHeat": 60, "quench": 1.5,
//              "ignite": 0.4, "igniteStrength": 0.6, "embersPerSecond": 6,
//              "maxEmbers": 48, "emberLifeSeconds": 8, "emberIgnite": 0.5,
//              "heatDamagePerSecond": 4},
//     "surface": "auto",       // or "land" / "water" or {"kind": "auto",
//              // "waterThresholdMeters": 0.2, "waterDecay": 0.08, "recover": 0.1,
//              // "spoutStrength": 0.55, "spoutLandDecay": 0.6}   (per second)
//     "erosion": {"rate": 0.04, "maxDepthMeters": 0.6, "deposit": 0.25,
//                 "capacityCubicMeters": 40},             // or false
//     "debris": {"max": 48, "perSecond": 8, "massKg": 0.5,
//                "areaSqMeters": 0.05, "lifeSeconds": 20}
//   }]
#pragma once
#include "PhysisTornado.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::physis {

struct AuthoredTornado {
    std::string id;
    StormEvent storm{};
    TornadoTrack track{};
    TornadoNature nature{};          // fire whirl / water / ground scour
};

// "physis.ignitions": fires started by the map at a tick (needs enableDisasters).
//   [{"center": [x, z], "radiusMeters": 6, "strength": 1, "startSeconds": 0}]
struct AuthoredIgnition {
    Vec3 center{};
    Fixed radiusMeters = Fixed::fromInt(2);
    Fixed strength = Fixed::fromInt(1);
    std::uint64_t tick = 1;          // climate tick it is applied on (>= 1)
};

namespace tornado_asset_detail {
[[noreturn]] inline void fail(const std::string& id, const std::string& what) {
    throw std::invalid_argument("PHYSIS tornado '" + id + "': " + what);
}
inline Fixed number(const Json& o, const char* key, Fixed fallback, Fixed lo, Fixed hi,
                    const std::string& id) {
    const Json& v = o[key];
    if (v.isNull()) return fallback;
    if (!v.isNumber()) fail(id, std::string(key) + " must be a number");
    const Fixed f = v.fx();
    if (f < lo || f > hi) fail(id, std::string(key) + " out of range");
    return f;
}
inline std::uint64_t integer(const Json& o, const char* key, std::uint64_t fallback,
                             std::uint64_t maximum, const std::string& id) {
    const Json& v = o[key];
    if (v.isNull()) return fallback;
    if (!v.isNumber() || v.numberLiteral().find_first_of(".eE-") != std::string::npos)
        fail(id, std::string(key) + " must be a non-negative integer");
    const auto n = v.integer(-1);
    if (n < 0 || std::uint64_t(n) > maximum) fail(id, std::string(key) + " out of range");
    return std::uint64_t(n);
}
inline bool flag(const Json& o, const char* key, bool fallback, const std::string& id) {
    const Json& v = o[key];
    if (v.isNull()) return fallback;
    if (v.type() != Json::Type::Bool) fail(id, std::string(key) + " must be boolean");
    return v.boolean();
}
// [x, z] or [x, y, z]
inline Vec3 point(const Json& v, const std::string& id, const char* what) {
    if (!v.isArray() || (v.size() != 2 && v.size() != 3))
        fail(id, std::string(what) + " must be [x, z] or [x, y, z]");
    for (std::size_t i = 0; i < v.size(); ++i)
        if (!v[i].isNumber()) fail(id, std::string(what) + " contains a non-number");
    const Fixed range = Fixed::fromInt(1000000);
    Vec3 p = v.size() == 2 ? Vec3{v[0].fx(), Fixed{}, v[1].fx()}
                           : Vec3{v[0].fx(), v[1].fx(), v[2].fx()};
    if (fxAbs(p.x) > range || fxAbs(p.y) > range || fxAbs(p.z) > range)
        fail(id, std::string(what) + " outside +-1000000 m");
    return p;
}
inline std::uint64_t ticks(const Json& o, const char* tickKey, const char* secondKey,
                           std::uint64_t fallback, std::uint32_t rate, const std::string& id) {
    const bool hasTicks = !o[tickKey].isNull(), hasSeconds = !o[secondKey].isNull();
    if (hasTicks && hasSeconds) fail(id, std::string(tickKey) + " and " + secondKey + " are exclusive");
    if (hasTicks) return integer(o, tickKey, fallback, 2147483647u, id);
    if (!hasSeconds) return fallback;
    const Fixed s = number(o, secondKey, Fixed{}, Fixed{}, Fixed::fromInt(10000000), id);
    return std::uint64_t((s * std::int64_t(rate) + Fixed::fromRatio(1, 2)).floorInt());
}
inline Fixed perTick(Fixed perSecond, std::uint32_t rate) {
    return perSecond / std::int64_t(std::max<std::uint32_t>(1, rate));
}
inline std::string id(const Json& o) {
    const Json& v = o["id"];
    if (!v.isString()) fail("", "stable string id required");
    std::string s = v.str();
    if (s.empty() || s.size() > 64) fail(s, "id must have 1..64 characters");
    for (unsigned char c : s)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '.')
            fail(s, "id accepts only ASCII letters, digits, '_', '-', '.'");
    return s;
}
} // namespace tornado_asset_detail

// Parses physisBlock["tornadoes"] (absent = empty). Throws std::invalid_argument
// on malformed data, before anything is installed. Sorted by id.
inline std::vector<AuthoredTornado> parseTornadoes(const Json& physisBlock,
                                                   std::uint32_t ticksPerSecond = 20) {
    using namespace tornado_asset_detail;
    std::vector<AuthoredTornado> out;
    if (!physisBlock.isObject()) return out;
    const Json& list = physisBlock["tornadoes"];
    if (list.isNull()) return out;
    if (!list.isArray()) throw std::invalid_argument("PHYSIS tornadoes must be an array");
    if (list.size() > 64) throw std::invalid_argument("PHYSIS tornadoes: at most 64");
    const std::uint32_t rate = std::max<std::uint32_t>(1, ticksPerSecond);
    const Fixed zero{}, one = Fixed::fromInt(1);
    for (const Json& o : list.items()) {
        if (!o.isObject()) throw std::invalid_argument("PHYSIS tornado must be an object");
        AuthoredTornado a;
        a.id = id(o);
        StormEvent& s = a.storm;
        TornadoTrack& t = a.track;
        s.kind = StormKind::Tornado;
        s.center = point(o["center"], a.id, "center");
        s.eyeRadiusMeters = number(o, "coreRadiusMeters", Fixed::fromInt(4), Fixed::fromRatio(1, 4),
                                   Fixed::fromInt(2000), a.id);
        s.outerRadiusMeters = number(o, "outerRadiusMeters", s.eyeRadiusMeters * 5,
                                     Fixed::fromRatio(1, 2), Fixed::fromInt(20000), a.id);
        if (s.outerRadiusMeters <= s.eyeRadiusMeters)
            fail(a.id, "outerRadiusMeters must exceed coreRadiusMeters");
        s.heightMeters = number(o, "heightMeters", Fixed::fromInt(80), one, Fixed::fromInt(20000), a.id);
        s.tangentialWind = number(o, "tangentialWind", Fixed::fromInt(45), zero, Fixed::fromInt(150), a.id);
        s.updraft = number(o, "updraft", Fixed::fromInt(15), zero, Fixed::fromInt(100), a.id);
        s.radialInflow = number(o, "radialInflow", Fixed::fromInt(5), zero, Fixed::fromInt(100), a.id);
        s.precipitationBoost = number(o, "precipitationBoost", Fixed::fromRatio(1, 4), zero, one, a.id);
        s.pressureDrop = number(o, "pressureDrop", Fixed::fromInt(20), zero, Fixed::fromInt(150), a.id);
        s.clockwise = flag(o, "clockwise", false, a.id);
        s.enabled = flag(o, "enabled", true, a.id);
        s.startTick = ticks(o, "startTick", "startSeconds", 0, rate, a.id);
        s.durationTicks = std::uint32_t(std::min<std::uint64_t>(
            ticks(o, "durationTicks", "durationSeconds", 0, rate, a.id), 2147483647u));
        t.rampTicks = std::uint32_t(std::min<std::uint64_t>(
            ticks(o, "rampTicks", "rampSeconds", rate * 3u, rate, a.id), 2147483647u));

        const Json& path = o["path"];
        if (!path.isNull()) {
            if (!path.isObject()) fail(a.id, "path must be an object");
            const std::string mode = path.getStr("mode", "drift");
            const Fixed speed = number(path, "speed", Fixed::fromInt(2), zero, Fixed::fromInt(100), a.id);
            if (mode == "wander") {
                t.mode = TornadoPathMode::Wander;
                t.speedPerTick = perTick(speed, rate);
                const Fixed turn = number(path, "turnDegreesPerSecond", Fixed::fromInt(30), zero,
                                          Fixed::fromInt(720), a.id);
                t.turnPerTick = perTick(turn / 360, rate);
                t.heading = number(path, "heading", zero, zero, Fixed::fromInt(360), a.id) / 360;
                t.rng = mix64(integer(path, "seed", 1, 2147483647u, a.id) ^ 0x544f524e41444full);
                if (const Json& b = path["bounds"]; !b.isNull()) {
                    if (!b.isArray() || b.size() != 4) fail(a.id, "bounds must be [minX, minZ, maxX, maxZ]");
                    for (std::size_t i = 0; i < 4; ++i)
                        if (!b[i].isNumber()) fail(a.id, "bounds contains a non-number");
                    t.boundsMin = {b[0].fx(), zero, b[1].fx()};
                    t.boundsMax = {b[2].fx(), zero, b[3].fx()};
                    if (!(t.boundsMax.x > t.boundsMin.x && t.boundsMax.z > t.boundsMin.z))
                        fail(a.id, "bounds must have max > min");
                }
            } else if (mode == "waypoints") {
                t.mode = TornadoPathMode::Waypoints;
                t.speedPerTick = perTick(speed, rate);
                t.loop = flag(path, "loop", false, a.id);
                const Json& pts = path["points"];
                if (!pts.isArray() || pts.size() == 0 || pts.size() > 256)
                    fail(a.id, "waypoints need 1..256 points");
                for (const Json& p : pts.items()) t.waypoints.push_back(point(p, a.id, "waypoint"));
            } else if (mode == "drift") {
                t.mode = TornadoPathMode::Drift;
                if (const Json& v = path["velocity"]; !v.isNull()) {
                    const Vec3 vel = point(v, a.id, "velocity");
                    const Fixed cap = Fixed::fromInt(100);
                    if (fxAbs(vel.x) > cap || fxAbs(vel.y) > cap || fxAbs(vel.z) > cap)
                        fail(a.id, "velocity components must be within +-100 m/s");
                    s.driftPerTick = {perTick(vel.x, rate), perTick(vel.y, rate), perTick(vel.z, rate)};
                }
            } else {
                fail(a.id, "path.mode must be drift, wander or waypoints");
            }
        }
        t.damagePerTick = perTick(number(o, "damagePerSecond", zero, zero, Fixed::fromInt(100000), a.id), rate);
        t.damageRadiusMeters = number(o, "damageRadiusMeters", zero, zero, Fixed::fromInt(20000), a.id);
        if (const Json& f = o["fire"]; !f.isNull()) {
            if (!f.isObject()) fail(a.id, "fire must be an object");
            t.fireSuppression = fxMin(one, perTick(number(f, "suppression", zero, zero, Fixed::fromInt(20), a.id), rate));
            t.fireFanning = fxMin(one, perTick(number(f, "fanning", zero, zero, Fixed::fromInt(20), a.id), rate));
        }
        if (const Json& d = o["debris"]; !d.isNull()) {
            if (d.type() == Json::Type::Bool) {
                if (d.boolean()) { t.maxDebris = 32; t.debrisPerTick = perTick(Fixed::fromInt(6), rate); }
            } else {
                if (!d.isObject()) fail(a.id, "debris must be an object or boolean");
                t.maxDebris = std::uint32_t(integer(d, "max", 32, 512, a.id));
                t.debrisPerTick = perTick(number(d, "perSecond", Fixed::fromInt(6), zero, Fixed::fromInt(1000), a.id), rate);
                t.debrisMassKg = number(d, "massKg", t.debrisMassKg, Fixed::fromRatio(1, 100), Fixed::fromInt(1000), a.id);
                t.debrisAreaSqMeters = number(d, "areaSqMeters", t.debrisAreaSqMeters, Fixed::fromRatio(1, 1000),
                                              Fixed::fromInt(100), a.id);
                t.debrisLifeTicks = std::uint32_t(std::clamp<std::uint64_t>(
                    ticks(d, "lifeTicks", "lifeSeconds", std::uint64_t(rate) * 20u, rate, a.id), 1u, 1000000u));
            }
        }
        // Fire whirl, water and soil (TornadoNature).
        TornadoNature& n = a.nature;
        n = defaultTornadoNature(rate);
        n.rng = mix64(std::uint64_t(out.size()) ^ 0x46495245ull);
        for (unsigned char c : a.id) n.rng = mix64(n.rng ^ c);
        if (const Json& f = o["fire"]; f.isObject()) {
            n.fire = flag(f, "whirl", true, a.id);
            const auto rateOf = [&](const char* key, Fixed& field) {
                if (!f[key].isNull()) field = perTick(number(f, key, zero, zero, Fixed::fromInt(100), a.id), rate);
            };
            rateOf("intake", n.intakePerTick);
            rateOf("decay", n.heatDecayPerTick);
            rateOf("quench", n.quenchPerTick);
            rateOf("ignite", n.ignitePerTick);
            rateOf("embersPerSecond", n.emberPerTick);
            n.intakePerTick = fxMin(one, n.intakePerTick);
            n.halfHeat = number(f, "halfHeat", n.halfHeat, Fixed::fromRatio(1, 10), Fixed::fromInt(1000000), a.id);
            n.igniteStrength = number(f, "igniteStrength", n.igniteStrength, zero, one, a.id);
            n.emberIgnite = number(f, "emberIgnite", n.emberIgnite, zero, one, a.id);
            n.maxEmbers = std::uint32_t(integer(f, "maxEmbers", n.maxEmbers, 512, a.id));
            n.emberLifeTicks = std::uint32_t(std::clamp<std::uint64_t>(
                ticks(f, "emberLifeTicks", "emberLifeSeconds", n.emberLifeTicks, rate, a.id), 1u, 100000u));
            n.heatDamagePerTick = perTick(number(f, "heatDamagePerSecond", n.heatDamagePerTick * std::int64_t(rate),
                                                 zero, Fixed::fromInt(100000), a.id), rate);
        }
        if (const Json& sf = o["surface"]; !sf.isNull()) {
            const auto kindOf = [&](const std::string& k) {
                if (k == "auto") return TornadoSurfaceKind::Auto;
                if (k == "land") return TornadoSurfaceKind::Land;
                if (k == "water") return TornadoSurfaceKind::Water;
                fail(a.id, "surface kind must be auto, land or water");
                return TornadoSurfaceKind::Auto;
            };
            if (sf.isString()) n.kind = kindOf(sf.str());
            else if (sf.isObject()) {
                if (!sf["kind"].isNull() && !sf["kind"].isString()) fail(a.id, "surface.kind must be a string");
                n.kind = kindOf(sf.getStr("kind", "auto"));
                n.waterThresholdMeters = number(sf, "waterThresholdMeters", n.waterThresholdMeters,
                                                Fixed::fromRatio(1, 100), Fixed::fromInt(100), a.id);
                const auto rateOf = [&](const char* key, Fixed& field) {
                    if (!sf[key].isNull()) field = perTick(number(sf, key, zero, zero, Fixed::fromInt(100), a.id), rate);
                };
                rateOf("waterDecay", n.waterDecayPerTick);
                rateOf("recover", n.recoverPerTick);
                rateOf("spoutLandDecay", n.spoutLandDecayPerTick);
                n.spoutStrength = number(sf, "spoutStrength", n.spoutStrength, Fixed::fromRatio(1, 100), one, a.id);
            } else fail(a.id, "surface must be a string or an object");
        }
        if (const Json& er = o["erosion"]; !er.isNull()) {
            if (er.type() == Json::Type::Bool) {
                if (!er.boolean()) { n.scourPerTick = zero; n.depositPerTick = zero; }
            } else if (er.isObject()) {
                n.scourPerTick = perTick(number(er, "rate", n.scourPerTick * std::int64_t(rate), zero,
                                                Fixed::fromInt(10), a.id), rate);
                n.maxScourMeters = number(er, "maxDepthMeters", n.maxScourMeters, Fixed::fromRatio(1, 100),
                                          Fixed::fromInt(50), a.id);
                n.depositPerTick = fxMin(one, perTick(number(er, "deposit", n.depositPerTick * std::int64_t(rate),
                                                             zero, Fixed::fromInt(100), a.id), rate));
                n.soilCapacity = number(er, "capacityCubicMeters", n.soilCapacity, zero,
                                        Fixed::fromInt(1000000), a.id);
            } else fail(a.id, "erosion must be an object or boolean");
        }
        prepareTornado(s, t);
        out.push_back(std::move(a));
    }
    std::sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.id < y.id; });
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i - 1].id == out[i].id)
            throw std::invalid_argument("PHYSIS duplicate tornado id: " + out[i].id);
    return out;
}

inline void spawnTornadoes(World& world, const std::vector<AuthoredTornado>& list) {
    for (const auto& a : list) (void)spawnTornado(world, a.storm, a.track, a.nature);
}

// Parses physisBlock["ignitions"] (absent = empty), sorted by tick then input order.
inline std::vector<AuthoredIgnition> parseIgnitions(const Json& physisBlock,
                                                    std::uint32_t ticksPerSecond = 20) {
    using namespace tornado_asset_detail;
    std::vector<AuthoredIgnition> out;
    if (!physisBlock.isObject()) return out;
    const Json& list = physisBlock["ignitions"];
    if (list.isNull()) return out;
    if (!list.isArray() || list.size() > 256)
        throw std::invalid_argument("PHYSIS ignitions must be an array (<=256)");
    const std::uint32_t rate = std::max<std::uint32_t>(1, ticksPerSecond);
    for (const Json& o : list.items()) {
        if (!o.isObject()) throw std::invalid_argument("PHYSIS ignition must be an object");
        AuthoredIgnition g;
        const std::string id = "ignition";
        g.center = point(o["center"], id, "center");
        g.radiusMeters = number(o, "radiusMeters", g.radiusMeters, Fixed{}, Fixed::fromInt(100000), id);
        g.strength = number(o, "strength", g.strength, Fixed::fromRatio(1, 100), Fixed::fromInt(1), id);
        g.tick = std::max<std::uint64_t>(1, ticks(o, "startTick", "startSeconds", 1, rate, id));
        out.push_back(g);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    return out;
}

// Sets every PHYSIS cell whose centre is within the radius (and at least the
// cell under the centre) on fire. Returns the number of cells ignited.
inline std::uint32_t applyIgnition(World& world, const AuthoredIgnition& g) {
    const auto* state = world.resourceConst<State>();
    if (!state || !world.resourceConst<DisasterState>()) return 0;
    const auto cs = std::int64_t(state->settings.cellSizeMeters);
    std::uint32_t n = 0;
    const Entity centre = cellAtPosition(world, g.center);
    const std::int64_t x0 = floorDiv((g.center.x - g.radiusMeters).floorInt(), cs);
    const std::int64_t x1 = floorDiv((g.center.x + g.radiusMeters).floorInt(), cs);
    const std::int64_t z0 = floorDiv((g.center.z - g.radiusMeters).floorInt(), cs);
    const std::int64_t z1 = floorDiv((g.center.z + g.radiusMeters).floorInt(), cs);
    if (x1 - x0 > 1024 || z1 - z0 > 1024) return 0;
    for (std::int64_t gz = z0; gz <= z1; ++gz)
        for (std::int64_t gx = x0; gx <= x1; ++gx) {
            const Entity e = cellAt(world, std::int32_t(gx), std::int32_t(gz));
            if (e == kNullEntity) continue;
            const Vec3 mid{Fixed::fromInt(gx * cs) + Fixed::fromRatio(cs, 2), Fixed{},
                           Fixed::fromInt(gz * cs) + Fixed::fromRatio(cs, 2)};
            if (e != centre && tornado_detail::horizontalDistance(mid, g.center) > g.radiusMeters) continue;
            if (ignite(world, std::int32_t(gx), std::int32_t(gz), g.strength)) ++n;
        }
    return n;
}

} // namespace aiverse::physis
