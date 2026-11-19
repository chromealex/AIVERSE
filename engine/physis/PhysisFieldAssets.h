// PHYSIS Stage 19: typed, deterministic field assets from a generic map JSON.
// No editor, renderer, Box3D, NEREID, TerraWeave or genre dependencies.
#pragma once
#include "PhysisIntegration.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace aiverse::physis {

enum class AuthoredFieldKind : std::uint8_t { Wind, Storm, Gravity };

struct AuthoredField {
    std::string id; // stable map authoring ID; sorted before deterministic spawn
    AuthoredFieldKind kind = AuthoredFieldKind::Wind;
    std::variant<WindZone, StormEvent, GravityField> data{WindZone{}};
};

namespace field_asset_detail {
inline void fail(const std::string& name, const char* message) {
    throw std::invalid_argument("PHYSIS field '" + name + "': " + message);
}

inline Fixed number(const Json& object, const char* key, Fixed fallback,
                    Fixed low, Fixed high, const std::string& id) {
    const Json& item = object[key];
    if (item.isNull()) return fallback;
    if (!item.isNumber()) fail(id, "number required");
    const Fixed result = item.fx();
    if (result < low || result > high) fail(id, "numeric value out of range");
    return result;
}

inline std::uint64_t integer(const Json& object, const char* key,
                             std::uint64_t fallback, std::uint64_t maximum,
                             const std::string& id) {
    const Json& item = object[key];
    if (item.isNull()) return fallback;
    if (!item.isNumber() ||
        item.numberLiteral().find_first_of(".eE") != std::string::npos)
        fail(id, "integer required");
    const auto result = item.integer(-1);
    if (result < 0 || std::uint64_t(result) > maximum) fail(id, "integer out of range");
    return std::uint64_t(result);
}

inline bool flag(const Json& object, const char* key, bool fallback,
                 const std::string& id) {
    const Json& item = object[key];
    if (item.isNull()) return fallback;
    if (item.type() != Json::Type::Bool) fail(id, "boolean required");
    return item.boolean();
}

inline Vec3 vector(const Json& object, const char* key, Vec3 fallback,
                   Fixed min, Fixed max, const std::string& id,
                   bool required = false) {
    const Json& value = object[key];
    if (value.isNull() && !required) return fallback;
    if (!value.isArray() || value.size() != 3) fail(id, "3D vector required");
    Vec3 result;
    Fixed* axes[] = {&result.x,&result.y,&result.z};
    for (size_t i = 0; i < 3; ++i) {
        if (!value[i].isNumber()) fail(id, "3D vector contains a non-number");
        *axes[i] = value[i].fx();
        if (*axes[i] < min || *axes[i] > max) fail(id, "vector component out of range");
    }
    return result;
}

inline std::string id(const Json& object) {
    const auto& value = object["id"];
    if (!value.isString()) fail("", "stable string ID required");
    std::string result = value.str();
    if (result.empty() || result.size() > 64) fail(result, "ID must have 1..64 characters");
    for (unsigned char c : result)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '.')
            fail(result, "ID accepts only ASCII letters, numbers, underscore, dash, dot");
    return result;
}
} // namespace field_asset_detail

// Must run before install(sim, options) so malformed authored data cannot
// partially install resources or seal a mismatched persistence registry.
inline std::vector<AuthoredField> parseFieldAssets(
    const Json& physisBlock, const InstallOptions& options) {
    using namespace field_asset_detail;
    if (!physisBlock.isObject()) throw std::invalid_argument("PHYSIS block must be object");
    std::vector<AuthoredField> output;
    const auto bounded = [&](const char* key, bool enabled, AuthoredFieldKind kind) {
        const Json& array = physisBlock[key];
        if (array.isNull()) return;
        if (!array.isArray()) throw std::invalid_argument(std::string("PHYSIS ") + key + " must be array");
        if (array.size() && !enabled)
            throw std::invalid_argument(std::string("PHYSIS ") + key + " requires enable flag");
        if (array.size() > 2048 || output.size() + array.size() > 2048)
            throw std::invalid_argument("PHYSIS field count exceeds 2048");
        for (const Json& object : array.items()) {
            if (!object.isObject()) throw std::invalid_argument("PHYSIS field must be object");
            AuthoredField asset;
            asset.id = id(object);
            asset.kind = kind;
            const Fixed one = Fixed::fromInt(1);
            const Fixed zero{};
            const Fixed range = Fixed::fromInt(1000000);
            const Fixed radiusCap = Fixed::fromInt(100000);
            const Fixed speedCap = Fixed::fromInt(200);
            const auto at = vector(object, "center", {}, -range, range, asset.id, true);
            if (kind == AuthoredFieldKind::Wind) {
                WindZone wind;
                wind.center = at;
                wind.radiusMeters = number(object,"radiusMeters",wind.radiusMeters,one,radiusCap,asset.id);
                wind.velocity = vector(object,"velocity",{},-speedCap,speedCap,asset.id,true);
                wind.gustAmplitude = number(object,"gustAmplitude",wind.gustAmplitude,
                                             zero,Fixed::fromInt(100),asset.id);
                wind.seed = integer(object,"seed",wind.seed,2147483647u,asset.id);
                wind.startTick = integer(object,"startTick",wind.startTick,2147483647u,asset.id);
                wind.durationTicks = std::uint32_t(integer(object,"durationTicks",
                                              wind.durationTicks,2147483647u,asset.id));
                wind.gustPeriodTicks = std::uint32_t(integer(object,"gustPeriodTicks",
                                               wind.gustPeriodTicks,2147483647u,asset.id));
                asset.data = wind;
            } else if (kind == AuthoredFieldKind::Storm) {
                StormEvent storm;
                storm.center = at;
                const Json& type = object["kind"];
                if (!type.isString()) fail(asset.id,"kind required (Hurricane or Tornado)");
                const std::string name = type.str();
                if (name == "Tornado") storm.kind = StormKind::Tornado;
                else if (name == "Hurricane") storm.kind = StormKind::Hurricane;
                else fail(asset.id,"invalid storm kind");
                storm.eyeRadiusMeters = number(object,"eyeRadiusMeters",storm.eyeRadiusMeters,
                                               one,radiusCap,asset.id);
                storm.outerRadiusMeters = number(object,"outerRadiusMeters",storm.outerRadiusMeters,
                                                 one,radiusCap,asset.id);
                if (storm.outerRadiusMeters <= storm.eyeRadiusMeters)
                    fail(asset.id, "outer radius must exceed eye radius");
                storm.heightMeters = number(object,"heightMeters",storm.heightMeters,
                                            one,radiusCap,asset.id);
                storm.driftPerTick = vector(object,"driftPerTick",{},-Fixed::fromInt(1000),
                                           Fixed::fromInt(1000),asset.id);
                storm.tangentialWind = number(object,"tangentialWind",storm.tangentialWind,
                                               zero,speedCap,asset.id);
                storm.radialInflow = number(object,"radialInflow",storm.radialInflow,
                                            zero,Fixed::fromInt(100),asset.id);
                storm.updraft = number(object,"updraft",storm.updraft,zero,
                                       Fixed::fromInt(150),asset.id);
                storm.precipitationBoost = number(object,"precipitationBoost",
                                                   storm.precipitationBoost,zero,one,asset.id);
                storm.pressureDrop = number(object,"pressureDrop",storm.pressureDrop,
                                            zero,Fixed::fromInt(150),asset.id);
                storm.startTick = integer(object,"startTick",storm.startTick,2147483647u,asset.id);
                storm.durationTicks = std::uint32_t(integer(object,"durationTicks",
                                               storm.durationTicks,2147483647u,asset.id));
                storm.clockwise = flag(object,"clockwise",storm.clockwise,asset.id);
                storm.enabled = flag(object,"enabled",storm.enabled,asset.id);
                asset.data = storm;
            } else {
                GravityField gravity;
                gravity.center = at;
                const Json& mode = object["mode"];
                if (!mode.isString()) fail(asset.id,"mode required (Uniform, Attract, Repel)");
                const auto name = mode.str();
                if (name == "Uniform") gravity.mode = GravityMode::Uniform;
                else if (name == "Attract") gravity.mode = GravityMode::Attract;
                else if (name == "Repel") gravity.mode = GravityMode::Repel;
                else fail(asset.id,"invalid gravity mode");
                const Fixed lowRadius = gravity.mode == GravityMode::Uniform ? zero : one;
                gravity.radiusMeters = number(object,"radiusMeters",gravity.radiusMeters,
                                              lowRadius,radiusCap,asset.id);
                gravity.acceleration = vector(object,"acceleration",{},-speedCap,speedCap,asset.id);
                gravity.radialMagnitude = number(object,"radialMagnitude",
                                                 gravity.radialMagnitude,zero,speedCap,asset.id);
                gravity.priority = std::int32_t(integer(object,"priority",0,1000000u,asset.id));
                gravity.overrideBaseline = flag(object,"overrideBaseline",gravity.overrideBaseline,asset.id);
                gravity.enabled = flag(object,"enabled",gravity.enabled,asset.id);
                asset.data = gravity;
            }
            output.push_back(std::move(asset));
        }
    };
    bounded("windZones",options.enableWindZones,AuthoredFieldKind::Wind);
    bounded("storms",options.enableStorms,AuthoredFieldKind::Storm);
    bounded("gravityFields",options.enableGravityFields,AuthoredFieldKind::Gravity);
    std::sort(output.begin(), output.end(),
              [](const AuthoredField& a, const AuthoredField& b){return a.id < b.id;});
    for (std::size_t i=1;i<output.size();++i)
        if (output[i-1].id == output[i].id)
            throw std::invalid_argument("PHYSIS duplicate field id: " + output[i].id);
    return output;
}

// Called once at native module setup. Entity allocation order is based on
// stable authored IDs, independent of JSON array/key order.
inline void spawnFieldAssets(World& world, const std::vector<AuthoredField>& fields) {
    for (const auto& asset : fields) {
        const Entity entity = world.create();
        switch (asset.kind) {
            case AuthoredFieldKind::Wind:
                world.add<WindZone>(entity, std::get<WindZone>(asset.data)); break;
            case AuthoredFieldKind::Storm:
                world.add<StormEvent>(entity, std::get<StormEvent>(asset.data)); break;
            case AuthoredFieldKind::Gravity:
                world.add<GravityField>(entity, std::get<GravityField>(asset.data)); break;
        }
    }
}

} // namespace aiverse::physis
