// Compatibility alias for the generic per-host model resource owner.
// Native projects should include <engine/render/ModelResourceCache.h>.
#pragma once
#include "engine/render/ModelResourceCache.h"
namespace aiverse::game::view_adapter {
using RtsModelResourceCache=::aiverse::render::ModelResourceCache;
}
