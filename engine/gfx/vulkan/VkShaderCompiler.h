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

// Vulkan 1.1 compute shader: complete #version 450 source with explicit set/bindings.
// Unlike graphics shaders, this accepts native storage-buffer/image declarations.
// Returns a real SPIR-V module; error contains a parse/link message on failure.
bool compileComputeProgram(const std::string& glsl, std::vector<uint32_t>& out,
                           std::string& error);

} // namespace aiverse::gfx::vk
