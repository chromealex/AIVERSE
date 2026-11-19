// PHYSIS Stage 18 — opt-in generic native GameModule map-configuration loader.
// Authored by any genre's editor map; never depends on editor, RTS/FPS or Box3D.
#pragma once
#include "PhysisFieldAssets.h"
#include "PhysisRuntimeFields.h"
#include "PhysisFieldInstallMode.h"
#include "PhysisFieldRuntimeState.h"
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
    const auto mode = config.getStr("floodMode", "LocalShallow");
    if (mode == "LocalShallow") options.floodMode = FloodMode::LocalShallow;
    else if (mode == "ExternalAuthority") options.floodMode = FloodMode::ExternalAuthority;
    else throw std::invalid_argument("PHYSIS invalid floodMode");

    // Authored field arrays are parsed in installFromMap, stable-sorted by
    // map IDs and spawned as ordinary PHYSIS ECS components. If a host needs
    // to attach external gameplay state to a field, it owns that association.
    return options;
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
    install(sim, *options);
    spawnFieldAssets(sim.world(), fields);
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
    install(sim, *options);
    sim.registerPersistentResource<RuntimeFieldRegistry>(
        "physis.authored_field_registry", 1);
    reconcileTrackedRuntimeFields(sim.world(), fields);
    sim.world().setResource<AuthoredFieldInstallMode>(AuthoredFieldInstallMode{AuthoredFieldMode::Tracked});
    return true;
}

} // namespace aiverse::physis
