// Portable GI presentation admission. CPU RGBA32F probe atlases are sampled
// textures, not HDR render targets: GLES 3.0 can read them into a normal
// RGBA8/LDR PBR framebuffer even without EXT_color_buffer_float.
// GPU-written RGBA16F GI remains HDR-only until validated for LDR targets.
// Keep the source/BRDF ownership and Shader Graph texture-slot guards outside.
#pragma once

namespace aiverse::render {

constexpr bool giDynamicColorPassAllowed(bool hdrScene,
                                         bool computeWritableProbeAtlas) {
    return hdrScene || !computeWritableProbeAtlas;
}

} // namespace aiverse::render
