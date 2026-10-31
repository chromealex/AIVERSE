#pragma once
#include "engine/data/Json.h"
#include <stdexcept>

namespace rts::game {
// Immutable match setup data: copied into the map before setup/replay recording.
inline void validateInitialVars(const Json& values) {
    if (values.isNull()) return;
    if (!values.isObject() || values.size() > 128)
        throw std::runtime_error("initialVars: expected an object with at most 128 entries");
    for (const auto& [key, value] : values.members()) {
        if (key.empty() || key.size() > 128)
            throw std::runtime_error("initialVars: variable name must contain 1..128 characters");
        for (const char c : key)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
                throw std::runtime_error("initialVars: invalid variable name '" + key + "'");
        if (!value.isNumber() || value.fx().raw % Fixed::fromInt(1).raw != 0 ||
            value.fx() < Fixed::fromInt(-1000000) || value.fx() > Fixed::fromInt(1000000))
            throw std::runtime_error("initialVars: '" + key + "' must be an integer in -1000000..1000000");
    }
}
inline void mergeInitialVars(Json& map, const Json& overrides) {
    validateInitialVars(overrides);
    Json values = map["initialVars"].isNull() ? Json::object() : map["initialVars"];
    validateInitialVars(values);
    for (const auto& [key, value] : overrides.members()) values.set(key, value);
    validateInitialVars(values);
    if (values.size()) map.set("initialVars", std::move(values));
}
} // namespace rts::game
