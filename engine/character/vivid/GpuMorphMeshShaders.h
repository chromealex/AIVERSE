// GPU packing bridge from 48-byte VIVID vertex to 60-byte ModelVertex.
// No skinning occurs here: MotionWeave owns the bone pose; Renderer3D uses
// its existing skinned shader path AFTER morph deformation.
#pragma once
namespace aiverse::vivid {

inline constexpr const char* kVividMeshStreamMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Params {uint vertexCount; uint reserved0; uint reserved1; uint reserved2;};
kernel void cs_main(device const uint* original [[buffer(0)]],
                    device const float4* morphed [[buffer(1)]],
                    device uint* result [[buffer(2)]],
                    constant Params& params [[buffer(8)]],
                    uint vertexIndex [[thread_position_in_grid]]) {
    if (vertexIndex >= params.vertexCount) return;
    const uint base = vertexIndex * 15u;
    const uint morph = vertexIndex * 3u;
    for (uint i = 0u; i < 15u; ++i)
        result[base+i] = original[base+i];
    const float4 position = morphed[morph];
    const float4 normal = morphed[morph+1u];
    const float4 tangent = morphed[morph+2u];
    result[base+0u] = as_type<uint>(position.x);
    result[base+1u] = as_type<uint>(position.y);
    result[base+2u] = as_type<uint>(position.z);
    result[base+3u] = as_type<uint>(normal.x);
    result[base+4u] = as_type<uint>(normal.y);
    result[base+5u] = as_type<uint>(normal.z);
    result[base+8u] = as_type<uint>(tangent.x);
    result[base+9u] = as_type<uint>(tangent.y);
    result[base+10u] = as_type<uint>(tangent.z);
}
)MSL";

inline constexpr const char* kVividMeshStreamGLSL = R"GLSL(#version 430
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
layout(std430,binding=0) readonly buffer Original {uint original[];};
layout(std430,binding=1) readonly buffer Morphed {vec4 morphed[];};
layout(std430,binding=2) writeonly buffer Result {uint result[];};
layout(std140,binding=8) uniform Uniforms {
    uint vertexCount;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} params;
void main() {
    uint vertex = gl_GlobalInvocationID.x;
    if (vertex >= params.vertexCount) return;
    uint base = vertex * 15u, morph = vertex * 3u;
    for (uint i = 0u; i < 15u; ++i)
        result[base+i] = original[base+i];
    vec4 position = morphed[morph];
    vec4 normal = morphed[morph+1u];
    vec4 tangent = morphed[morph+2u];
    result[base+0u] = floatBitsToUint(position.x);
    result[base+1u] = floatBitsToUint(position.y);
    result[base+2u] = floatBitsToUint(position.z);
    result[base+3u] = floatBitsToUint(normal.x);
    result[base+4u] = floatBitsToUint(normal.y);
    result[base+5u] = floatBitsToUint(normal.z);
    result[base+8u] = floatBitsToUint(tangent.x);
    result[base+9u] = floatBitsToUint(tangent.y);
    result[base+10u] = floatBitsToUint(tangent.z);
}
)GLSL";

} // namespace aiverse::vivid
