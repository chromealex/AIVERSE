// VIVID sparse morph compute shaders. Slot contract is shared by Metal/GLSL.
// Slot 0: GpuMorphVertex base, 1: GpuMorphDelta, 2: uint32 CSR offsets,
// 3: GpuMorphLink, 4: float weights, 5: GpuMorphVertex output,
// 8: GpuMorphUniforms. 64 work items per threadgroup; one writer per vertex.
// The Vulkan shader compiler may require a separate compute integration stage.
#pragma once
namespace aiverse::vivid {

inline constexpr const char* kVividMorphMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Vertex {float4 position; float4 normal; float4 tangent;};
struct Delta {uint vertex; uint pad0; uint pad1; uint pad2; float4 position; float4 normal; float4 tangent;};
struct Link {uint target; uint delta;};
struct Params {uint vertices; uint targets; uint deltas; uint links; uint normalizeDirections; uint pad0; uint pad1; uint pad2;};

kernel void cs_main(device const Vertex* base [[buffer(0)]],
                    device const Delta* deltas [[buffer(1)]],
                    device const uint* offsets [[buffer(2)]],
                    device const Link* links [[buffer(3)]],
                    device const float* weights [[buffer(4)]],
                    device Vertex* output [[buffer(5)]],
                    constant Params& params [[buffer(8)]],
                    uint v [[thread_position_in_grid]]) {
    if (v >= params.vertices) return;
    float3 pos = base[v].position.xyz;
    float3 normal = base[v].normal.xyz;
    float3 tangent = base[v].tangent.xyz;
    uint begin = offsets[v], end = min(offsets[v + 1], params.links);
    for (uint i = begin; i < end; ++i) {
        Link ref = links[i];
        if (ref.target >= params.targets || ref.delta >= params.deltas) continue;
        const float weight = weights[ref.target];
        if (weight == 0.0f) continue;
        const Delta d = deltas[ref.delta];
        pos += d.position.xyz * weight;
        normal += d.normal.xyz * weight;
        tangent += d.tangent.xyz * weight;
    }
    if (params.normalizeDirections != 0) {
        float n2 = dot(normal, normal), t2 = dot(tangent, tangent);
        if (n2 > 1.0e-30f) normal *= rsqrt(n2);
        if (t2 > 1.0e-30f) tangent *= rsqrt(t2);
    }
    output[v].position = float4(pos, 0.0f);
    output[v].normal = float4(normal, 0.0f);
    output[v].tangent = float4(tangent, 0.0f);
}
)MSL";

inline constexpr const char* kVividMorphGLSL = R"GLSL(#version 430
layout(local_size_x=64, local_size_y=1, local_size_z=1) in;
struct Vertex {vec4 position; vec4 normal; vec4 tangent;};
struct Delta {uint vertex; uint pad0; uint pad1; uint pad2; vec4 position; vec4 normal; vec4 tangent;};
struct Link {uint target; uint delta;};
layout(std430,binding=0) readonly buffer BaseVertices {Vertex baseVertices[];};
layout(std430,binding=1) readonly buffer MorphDeltas {Delta morphDeltas[];};
layout(std430,binding=2) readonly buffer VertexOffsets {uint offsets[];};
layout(std430,binding=3) readonly buffer VertexLinks {Link links[];};
layout(std430,binding=4) readonly buffer ShapeWeights {float weights[];};
layout(std430,binding=5) writeonly buffer ResultVertices {Vertex resultVertices[];};
layout(std140,binding=8) uniform U {
    uint vertices;
    uint targets;
    uint deltas;
    uint linksCount;
    uint normalizeDirections;
    uint pad0;
    uint pad1;
    uint pad2;
} params;

void main() {
    uint v = gl_GlobalInvocationID.x;
    if (v >= params.vertices) return;
    vec3 pos = baseVertices[v].position.xyz;
    vec3 n = baseVertices[v].normal.xyz;
    vec3 t = baseVertices[v].tangent.xyz;
    uint begin = offsets[v], end = min(offsets[v + 1], params.linksCount);
    for (uint i = begin; i < end; ++i) {
        Link ref = links[i];
        if (ref.target >= params.targets || ref.delta >= params.deltas) continue;
        float weight = weights[ref.target];
        if (weight == 0.0) continue;
        Delta d = morphDeltas[ref.delta];
        pos += d.position.xyz * weight;
        n += d.normal.xyz * weight;
        t += d.tangent.xyz * weight;
    }
    if (params.normalizeDirections != 0u) {
        float n2 = dot(n,n), t2 = dot(t,t);
        if (n2 > 1.0e-30) n *= inversesqrt(n2);
        if (t2 > 1.0e-30) t *= inversesqrt(t2);
    }
    resultVertices[v].position = vec4(pos,0.0);
    resultVertices[v].normal = vec4(n,0.0);
    resultVertices[v].tangent = vec4(t,0.0);
}
)GLSL";

} // namespace aiverse::vivid
