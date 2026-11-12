// Automatic lightmap UVs (agent-lightbake): xatlas (MIT, third_party/xatlas) charts + packs every
// unskinned mesh of a model that has no lightmap UVs; all primitives of a mesh share one [0, 1] space.
// The result is applied to the model and returned as a ".lmuv" sidecar (render/LightmapUv.h), which
// loadModel applies from then on, so runtime and baker agree on the geometry.
#pragma once
#include <string>
#include <vector>
#include "../render/LightmapUv.h"

namespace aiverse::bake {

// `texelsPerUnit`: expected lightmap density (charts get `padding` texels at that density).
// Returns the number of meshes unwrapped (0 = nothing to do); `sidecar` is empty then.
int unwrapLightmapUvs(render::Model& m, float texelsPerUnit, int padding, std::string& sidecar, std::string& error);

} // namespace aiverse::bake
