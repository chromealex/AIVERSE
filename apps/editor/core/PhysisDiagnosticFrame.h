#pragma once
#include "engine/data/Json.h"

namespace aiverse {
class World;
namespace editor {
Json readPhysisGameFrame(const World* world, const Json& params);
}
}
