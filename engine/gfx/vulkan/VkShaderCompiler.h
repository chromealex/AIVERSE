// GLSL of the RHI (GLSL ES 3.00 bodies, uniform blocks "U<slot>", samplers "tex<slot>") -> SPIR-V for Vulkan.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::gfx::vk {

// Descriptor set 0: uniform block U<slot> at binding <slot>, sampler tex<slot> at kTextureBinding0 + slot.
constexpr int kUniformSlots = 8;
constexpr int kTextureBinding0 = 8;
constexpr int kMaxTextureSlots = 32;

// The body as Vulkan GLSL 4.50: explicit bindings, gl_VertexIndex / gl_InstanceIndex, and in the vertex
// stage a wrapper of main() that maps clip z from GL [-1, 1] to Vulkan [0, 1] (depth values equal GL's).
std::string translateGlsl(const std::string& body, bool vertex, bool linearTarget, uint32_t* shadowSlots = nullptr);  // shadowSlots |= sampler2DShadow slots

struct SpirvProgram {
    std::vector<uint32_t> vertex, fragment;
    uint32_t shadowSlots = 0;   // texture slots declared as sampler2DShadow (unset ones get a depth dummy)
    std::string error;
};
// Compiles and links both stages (varyings get matching locations by name). Thread-safe.
bool compileProgram(const std::string& vertexBody, const std::string& fragmentBody, bool linearTarget, SpirvProgram& out);
// Explicit Vulkan compute shader: caller writes GLSL 4.50 body with
// layout(set=0,binding=0..3,rgba8/rgba16f) uniform image2D imageN,
// sampled tex at bindings 4..11, storage buffers 12..15 and UBO at 16.
// Uses local_size_x = 64 unless the source provides its own layout.
bool compileComputeProgram(const std::string& glslBody, std::vector<uint32_t>& spv,
                           std::string& error);

// Separate GLSL450 compute compiler. The source is a complete shader with
// #version and explicit layout(set=0,binding=N) storage buffers. No legacy
// vertex/fragment translation is applied (critical for std430 byte parity).
// Compilation is CPU-only, shared by Vulkan RHI and shader validation tests.
bool compileComputeProgram(const std::string& source,
                           std::vector<uint32_t>& spirv,
                           std::string* error = nullptr);

} // namespace aiverse::gfx::vk
