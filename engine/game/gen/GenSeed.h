#pragma once
#include "engine/data/Json.h"
#include <charconv>

namespace aiverse::gen {

// Seed literals can exceed the game's fixed-point numeric range.
inline int64_t integerSeed(const Json& value, int64_t fallback) {
    if (!value.isNumber()) return fallback;
    const auto& literal=value.numberLiteral();
    int64_t seed=0;
    const auto parsed=std::from_chars(literal.data(),literal.data()+literal.size(),seed);
    if(parsed.ec==std::errc{} && parsed.ptr==literal.data()+literal.size()) return seed;
    return value.integer(fallback); // Preserve the existing handling of decimal game data.
}

} // namespace aiverse::gen
