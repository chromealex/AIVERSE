#pragma once
// Index-only LOD chain for an assembled character (skinned body, clothing,
// hair cards): vertices, skin weights and joint palettes are shared with LOD0,
// so skinning is exact at every level. Meshes with morph targets (the face)
// and meshes that already have LODs are left untouched.
#include "engine/render/Model.h"
#include <cstddef>
namespace aiverse::vivid {
// Returns the number of LOD meshes added (0 when meshoptimizer is unavailable).
size_t generateHumanCharacterLods(render::Model& model);
}
