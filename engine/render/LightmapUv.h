// Lightmap UVs (second UV set) of models (agent-lightbake).
//
// Sources, in order: the model's own second UV set (glTF TEXCOORD_1, FBX second UV set; unskinned
// primitives only, expected in [0, 1] like Unity's UV2) or a generated unwrap stored next to the model
// as "<model>.lmuv" (written by the light baker with xatlas, engine/bake/LightmapUnwrap.h). The sidecar
// replaces the vertices / indices of the primitives it covers (chart seams split vertices), so the
// runtime and the baker see the same geometry. Storage: ModelVertex::joints as two unorm16 (Model.h).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Model.h"

namespace aiverse::render {

inline void setLightmapUv(ModelVertex& v, float2 uv) {
    auto q = [](float x) { return uint32_t(x <= 0 ? 0 : x >= 1 ? 65535 : x * 65535.0f + 0.5f); };
    const uint32_t u = q(uv.x), w = q(uv.y);
    v.joints[0] = uint8_t(u & 255); v.joints[1] = uint8_t(u >> 8);
    v.joints[2] = uint8_t(w & 255); v.joints[3] = uint8_t(w >> 8);
}
inline float2 lightmapUv(const ModelVertex& v) {
    return {float(v.joints[0] | (v.joints[1] << 8)) / 65535.0f, float(v.joints[2] | (v.joints[3] << 8)) / 65535.0f};
}

// Vertex range of a primitive: firstVertex / vertexCount, or the range its indices span when a loader left
// them unset (procedural models).
void primitiveVertexRange(const Model& m, const Primitive& p, uint32_t& first, uint32_t& count);
// True when every primitive of the mesh carries lightmap UVs.
bool meshHasLightmapUv(const Model& m, int mesh);

// Sidecar format (little endian): "LMUV" u32 version(1) u64 sourceHash, u32 meshCount, then per mesh:
// u32 mesh index, u32 primitive count, per primitive: u32 vertexCount, u32 indexCount,
// vertexCount x { u32 source vertex (relative to the primitive), u16 u, u16 v }, indexCount x u32 (relative).
struct LightmapUvMesh {
    int mesh = -1;
    struct Prim { std::vector<uint32_t> xref; std::vector<uint16_t> uv; std::vector<uint32_t> indices; };
    std::vector<Prim> prims;
};
// Hash of the geometry a sidecar was generated for (positions, normals, indices of every primitive).
uint64_t lightmapSourceHash(const Model& m);
std::string encodeLightmapUvs(uint64_t sourceHash, const std::vector<LightmapUvMesh>& meshes);
// Applies a sidecar: false (and nothing changes) when it is broken or was made for other geometry.
bool applyLightmapUvs(Model& m, const std::string& bytes, std::string* error = nullptr);
// Rewrites the given meshes of `m` (CPU vertices required) with the unwrap; morph targets are dropped there.
void applyLightmapUvMeshes(Model& m, const std::vector<LightmapUvMesh>& meshes);

} // namespace aiverse::render
