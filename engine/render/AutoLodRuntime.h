// Automatic import-time LOD fallback for rigid meshes without authored LODs.
// Does not alter node transforms, source vertices, skinning or original LOD0.
// Called before uploadModel on the CPU. Never called during render frames.
#pragma once
#include <cstddef>
namespace aiverse::render {
struct Model;
size_t generateMissingModelLods(Model& model);
}
