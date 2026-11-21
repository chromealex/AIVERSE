// Contract for authoring light data in a genre-specific document via fps_set.
// Only validation here; actual mutation/serialization happens on the engine thread.
#pragma once
#include "StudioLightingValidation.h"
#include <cstdint>
#include <string_view>

namespace aiverse::studio {
inline bool validFpsLightingChange(std::string_view method, std::string_view key,
                                   double numeric, bool isNumeric, bool isText,
                                   std::string_view text, bool isBool) {
    if (method == "studio.lightingSet")
        return isNumeric && validLightingScalar(key,numeric);
    if (method == "studio.lightUpdate")
        return (isNumeric && validLightScalar(key,numeric)) ||
               (isText && validLightText(key,text)) ||
               (isBool && validLightBoolean(key));
    if (method == "studio.lightAdd" || method == "studio.lightDelete") return true;
    return false;
}
} // namespace aiverse::studio
