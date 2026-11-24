// PHYSIS Stage 18 — opt-in generic native GameModule map-configuration loader.
// Authored by any genre's editor map; never depends on editor, RTS/FPS or Box3D.
#pragma once
#include "PhysisFieldAssets.h"
#include "PhysisRuntimeFields.h"
#include "PhysisFieldInstallMode.h"
#include "PhysisFieldRuntimeState.h"
#include "PhysisTornadoAssets.h"
#include "engine/data/Json.h"
#include <optional>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <limits>

namespace aiverse::physis {

inline std::optional<InstallOptions> optionsFromMap(const Json& map) {
    const Json& config = map["physis"];
    if (config.isNull()) return std::nullopt;
    if (!config.isObject())
        throw std::invalid_argument("PHYSIS map 'physis' must be an object");
    if (!config["enabled"].isNull() &&
        config["enabled"].type() != Json::Type::Bool)
        throw std::invalid_argument("PHYSIS enabled must be boolean");
    if (!config.getBool("enabled",true)) return std::nullopt;
    const Json& grid = config["grid"];
    const Json& climate = config["climate"];
    if (!grid.isObject() || !climate.isObject())
        throw std::invalid_argument("PHYSIS grid and climate objects required");
    const auto integer = [&](const Json& v, const char* key,
                             int64_t low, int64_t high) -> int64_t {
        const auto& item = v[key];
        if (!item.isNumber() ||
            item.numberLiteral().find_first_of(".eE") != std::string::npos)
            throw std::invalid_argument(std::string("PHYSIS integer required: ") + key);
        const auto number = item.integer(std::numeric_limits<int64_t>::min());
        if (number < low || number > high)
            throw std::out_of_range(std::string("PHYSIS grid bounds: ") + key);
        return number;
    };
    InstallOptions options;
    options.climate.originX = std::int32_t(integer(grid,"originX",-1000000,1000000));
    options.climate.originZ = std::int32_t(integer(grid,"originZ",-1000000,1000000));
    options.climate.width = std::int32_t(integer(grid,"width",1,1024));
    options.climate.height = std::int32_t(integer(grid,"height",1,1024));
    options.climate.cellSizeMeters = std::int32_t(integer(grid,"cellSizeMeters",1,100000));
    options.climate.seed = std::uint64_t(integer(grid,"seed",0,2147483647));
    const auto numeric = [&](const char* key, Fixed lo, Fixed hi) -> Fixed {
        const auto& value = climate[key];
        if (!value.isNumber())
            throw std::invalid_argument(std::string("PHYSIS climate number required: ") + key);
        const Fixed fixed = value.fx();
        if (fixed < lo || fixed > hi)
            throw std::out_of_range(std::string("PHYSIS climate bounds: ") + key);
        return fixed;
    };
    options.climate.baseTemperature = numeric("baseTemperature",
                                             Fixed::fromInt(-80), Fixed::fromInt(80));
    options.climate.baseHumidity = numeric("baseHumidity", Fixed{}, Fixed::fromInt(1));
    const auto readFlag = [&](const char* key) {
        const auto& j = config[key];
        if (!j.isNull() && j.type() != Json::Type::Bool)
            throw std::invalid_argument(std::string("PHYSIS flag must be bool: ") + key);
        return j.boolean(false);
    };
    options.enableDisasters = readFlag("enableDisasters");
    options.enableStorms = readFlag("enableStorms");
    options.enableWindZones = readFlag("enableWindZones");
    options.enableGravityFields = readFlag("enableGravityFields");
    options.enableForces = readFlag("enableForces");
    // A non-empty "tornadoes" array turns on the tornado systems and their
    // persistence (and storms, which they are). Maps without it are unchanged.
    if (const Json& tornadoes = config["tornadoes"]; !tornadoes.isNull()) {
        if (!tornadoes.isArray())
            throw std::invalid_argument("PHYSIS tornadoes must be an array");
        if (tornadoes.size() > 0) options.enableTornadoes = options.enableStorms = true;
    }
    // Optional fire / flood coefficients (DisasterConfig, per tick).
    if (const Json& dc = config["disasterConfig"]; !dc.isNull()) {
        if (!dc.isObject()) throw std::invalid_argument("PHYSIS disasterConfig must be an object");
        const auto rate = [&](const char* key, Fixed& field) {
            const Json& v = dc[key];
            if (v.isNull()) return;
            if (!v.isNumber() || v.fx() < Fixed{} || v.fx() > Fixed::fromInt(1))
                throw std::invalid_argument(std::string("PHYSIS disasterConfig 0..1 number: ") + key);
            field = v.fx();
        };
        rate("fireSpreadRate", options.disasters.fireSpreadRate);
        rate("fireGrowthRate", options.disasters.fireGrowthRate);
        rate("fuelConsumptionRate", options.disasters.fuelConsumptionRate);
        rate("rainSuppressionRate", options.disasters.rainSuppressionRate);
        rate("wetnessSuppressionRate", options.disasters.wetnessSuppressionRate);
    }
    if (!config["scourCellMeters"].isNull())
        options.scourCellMeters = std::int32_t(integer(config, "scourCellMeters", 1, 64));
    if (const Json& st = config["streaming"]; !st.isNull()) {
        if (!st.isObject()) throw std::invalid_argument("PHYSIS streaming must be an object");
        options.streamingChunkCells = std::int32_t(integer(st, "chunkCells", 4, 1024));
    }
    if (const Json& anchors = config["anchors"]; !anchors.isNull()) {
        if (!anchors.isArray() || anchors.size() > 256)
            throw std::invalid_argument("PHYSIS anchors must be an array (<=256)");
        if (options.streamingChunkCells == 0)
            throw std::invalid_argument("PHYSIS anchors require streaming.chunkCells");
        for (const auto& a : anchors.items()) {
            if (!a.isObject() || !a["x"].isNumber() || !a["z"].isNumber() ||
                !a["radiusMeters"].isNumber())
                throw std::invalid_argument("PHYSIS anchor needs x, z, radiusMeters");
            ActivityAnchor anchor;
            anchor.position = {a["x"].fx(), Fixed{}, a["z"].fx()};
            anchor.radiusMeters = a["radiusMeters"].fx();
            if (anchor.radiusMeters <= Fixed{} || anchor.radiusMeters > Fixed::fromInt(1000000) ||
                fxAbs(anchor.position.x) > Fixed::fromInt(100000000) ||
                fxAbs(anchor.position.z) > Fixed::fromInt(100000000))
                throw std::out_of_range("PHYSIS anchor bounds");
            options.anchors.push_back(anchor);
        }
    }
    if (!config["workerThreads"].isNull())
        options.workerThreads = unsigned(integer(config, "workerThreads", 0, 16));
    const auto& floodMode = config["floodMode"];
    if (!floodMode.isNull() && !floodMode.isString())
        throw std::invalid_argument("PHYSIS floodMode must be a string");
    const auto mode = config.getStr("floodMode", "LocalShallow");
    if (mode == "LocalShallow") options.floodMode = FloodMode::LocalShallow;
    else if (mode == "ExternalAuthority") options.floodMode = FloodMode::ExternalAuthority;
    else throw std::invalid_argument("PHYSIS invalid floodMode");

    // Authored field arrays are parsed in installFromMap, stable-sorted by
    // map IDs and spawned as ordinary PHYSIS ECS components. If a host needs
    // to attach external gameplay state to a field, it owns that association.
    return options;
}

// "physis.noFuelOnWater": true — cells whose centre the host reports as open
// water (GameModule::environmentWaterDepth) start without fuel, so lakes and
// rivers do not burn. Applied once at install; off by default.
inline void clearFuelOnWater(Simulation& sim, const Json& physisBlock) {
    const Json& flag = physisBlock["noFuelOnWater"];
    if (flag.isNull()) return;
    if (flag.type() != Json::Type::Bool) throw std::invalid_argument("PHYSIS noFuelOnWater must be boolean");
    if (!flag.boolean()) return;
    const auto* state = sim.world().resourceConst<State>();
    if (!state || !sim.world().resourceConst<DisasterState>()) return;
    const auto cs = std::int64_t(state->settings.cellSizeMeters);
    std::vector<Entity> wet;
    for (const Entity e : state->cells) {
        const auto* c = sim.world().tryGetConst<Cell>(e);
        if (!c) continue;
        const Fixed x = Fixed::fromInt(std::int64_t(c->x) * cs) + Fixed::fromRatio(cs, 2);
        const Fixed z = Fixed::fromInt(std::int64_t(c->z) * cs) + Fixed::fromRatio(cs, 2);
        if (sim.module().environmentWaterDepth(sim, x, z) >= Fixed::fromRatio(1, 5)) wet.push_back(e);
    }
    for (const Entity e : wet)
        if (auto* hz = sim.world().tryGet<HazardCell>(e)) { hz->fuel = Fixed{}; hz->fire = Fixed{}; }
}

// Authored fires: stateless, applied after the tick whose climate tick
// matches (so saves / replays need no extra state). No system when empty.
inline void addIgnitionSystem(Simulation& sim, const std::vector<AuthoredIgnition>& ignitions) {
    if (ignitions.empty()) return;
    sim.addSystem("physis.ignitions", [ignitions](Simulation& s) {
        const auto* st = s.world().resourceConst<State>();
        if (!st) return;
        for (const auto& g : ignitions)
            if (g.tick == st->tick) (void)applyIgnition(s.world(), g);
    });
}

// Explicit opt-in from GameModule::setup, before persistence registration seals.
// Returns false if PHYSIS is absent or disabled. Never advances ECS frames.
inline bool installFromMap(Simulation& sim, const Json& map) {
    const auto options = optionsFromMap(map);
    if (!options) return false;
    // Parse/validate all authored field volumes BEFORE mutating/initializing
    // ECS or sealing persistence. No partial setup from malformed map input.
    if (sim.world().resourceConst<State>() ||
        sim.world().resourceConst<AuthoredFieldInstallMode>() ||
        sim.world().resourceConst<RuntimeFieldRegistry>() ||
        sim.world().resourceConst<FieldRuntimeState>())
        throw std::logic_error("PHYSIS authored fields already installed");
    const auto fields = parseFieldAssets(map["physis"], *options);
    const auto tornadoes = parseTornadoes(map["physis"], sim.tickRate());
    const auto ignitions = parseIgnitions(map["physis"], sim.tickRate());
    if (!ignitions.empty() && !options->enableDisasters)
        throw std::invalid_argument("PHYSIS ignitions require enableDisasters");
    install(sim, *options);
    spawnFieldAssets(sim.world(), fields);
    spawnTornadoes(sim.world(), tornadoes);   // after fields: stable entity order
    clearFuelOnWater(sim, map["physis"]);
    addIgnitionSystem(sim, ignitions);
    sim.world().setResource<AuthoredFieldInstallMode>(AuthoredFieldInstallMode{AuthoredFieldMode::Static});
    return true;
}

// Opt-in alternative to installFromMap for projects requiring stable authored
// field identities across snapshots and native ECS save/replay. This changes
// the persistence manifest intentionally; do not switch existing saves silently.
inline bool installTrackedFromMap(Simulation& sim, const Json& map) {
    const auto options = optionsFromMap(map);
    if (!options) return false;
    if (sim.world().resourceConst<State>() ||
        sim.world().resourceConst<AuthoredFieldInstallMode>() ||
        sim.world().resourceConst<RuntimeFieldRegistry>() ||
        sim.world().resourceConst<FieldRuntimeState>())
        throw std::logic_error("PHYSIS authored fields already installed");
    const auto fields = parseFieldAssets(map["physis"], *options);
    const auto tornadoes = parseTornadoes(map["physis"], sim.tickRate());
    const auto ignitions = parseIgnitions(map["physis"], sim.tickRate());
    if (!ignitions.empty() && !options->enableDisasters)
        throw std::invalid_argument("PHYSIS ignitions require enableDisasters");
    install(sim, *options);
    sim.registerPersistentResource<RuntimeFieldRegistry>(
        "physis.authored_field_registry", 1);
    reconcileTrackedRuntimeFields(sim.world(), fields);
    spawnTornadoes(sim.world(), tornadoes);
    clearFuelOnWater(sim, map["physis"]);
    addIgnitionSystem(sim, ignitions);
    sim.world().setResource<AuthoredFieldInstallMode>(AuthoredFieldInstallMode{AuthoredFieldMode::Tracked});
    return true;
}

} // namespace aiverse::physis
