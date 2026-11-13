#pragma once
#include "engine/render/Math3D.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace aiverse::cables {
using render::float2;
using render::float3;
// Presentation-only authoring parameters, in metres. Each consecutive pair is a span.
struct Options {
    std::vector<float3> anchors;
    float sag = 0.6f;             // Mid-span displacement from the straight chord.
    float radius = 0.025f;
    float uvRepeat = 1.0f;        // Metres per longitudinal texture repetition.
    float spacing = 0.15f;
    float3 down{0, -1, 0};
    float3 bundleAxis{0, 0, 1};
    unsigned segments = 32, sides = 8, strands = 1;
    bool caps = true;
};
struct Vertex { float3 position, normal; float2 uv; };
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float length = 0;             // Sum of sampled centreline lengths over all strands.
};
// Stable parabolic sag, not a physical catenary or a rope simulation.
// Throws invalid_argument for malformed/oversized input. No renderer/GPU dependency.
Mesh build(const Options &, const std::atomic<bool> *cancel = nullptr);
} // namespace aiverse::cables
