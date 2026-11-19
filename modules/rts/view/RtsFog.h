// Compatibility header: the visual fog effect is now an independent
// engine-level View feature, not an RTS-specific algorithm.
#pragma once
#include "engine/view/VisibilityField.h"

namespace aiverse::game {
using FogLook=::aiverse::view::VisibilityLook;
using FogOfWar=::aiverse::view::VisibilityField;
inline FogLook parseFogLook(const Json& rulesFog,const Json& gameFog) {
    return ::aiverse::view::parseVisibilityLook(rulesFog,gameFog);
}
} // namespace aiverse::game
