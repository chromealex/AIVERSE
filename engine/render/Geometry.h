#pragma once
#include "Model.h"
#include "../data/Json.h"
#include <array>

namespace aiverse::render {
// Presentation-only policies. No dependency on simulation or hardware compute features.
struct GeometryPolicy {
    bool clusters = false, occlusion = false, occluder = false, shadowClusters = false;
    float errorPixels = 1, shadowErrorPixels = 1;
};
GeometryPolicy parseGeometryPolicy(const Json& value);
struct GeometryCluster {
    uint32_t firstIndex = 0, indexCount = 0, coarseFirst = 0, coarseCount = 0;
    float3 min, max;
    float error = 0;
};
struct GeometryData {
    std::vector<std::vector<GeometryCluster>> primitives;
    // Exact source triangles, never bounding boxes or simplified proxies used as occluders.
    std::vector<std::vector<std::array<float3, 3>>> occluders;
};
// Must run before upload. Existing vertex data and primitive ranges remain unchanged.
void prepareGeometry(Model& model, uint32_t trianglesPerCluster = 128);
bool geometryBoxVisible(const mat4& clip, float3 min, float3 max);
float geometryErrorPixels(const mat4& clip, float3 min, float3 max, float error, int width, int height, bool zeroToOne = true);

// Conservative full-cell coverage. Rebuilt from the current view every frame; no GPU readback,
// temporal history, or false occlusion caused by holes in a mesh or a near-plane crossing.
class GeometryOcclusion {
public:
    static constexpr int kWidth = 64, kHeight = 36;
    void clear(bool zeroToOne);
    void triangle(const mat4& clip, const std::array<float3, 3>& vertices, bool doubleSided = true);
    bool hidden(const mat4& clip, float3 min, float3 max) const;
private:
    std::array<float, kWidth * kHeight> depth_{};
    bool zeroToOne_ = false;
};
}
