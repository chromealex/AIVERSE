#pragma once
// PRISMA postprocess composite texture layout. Pure CPU policy: no RHI types.
//
// Slots 0..4 are owned by the fixed composite, 5..8 are available to the
// bounded postfx Shader Graph (at most four independent textures). HDR
// AutoExposure prefers slot 8 for backwards-compatible shader variants,
// but moves to slot 9 when a fourth graph texture occupies slot 8.
// The same plan MUST drive GLSL/MSL declarations, ShaderDesc.textureSlots
// and runtime Device::setTexture bindings.
#include <cstddef>

namespace aiverse::render {

struct PrismaCompositeTextureLayout {
    static constexpr int kGraphFirst = 5;
    static constexpr std::size_t kMaxGraphTextures = 4;
    static constexpr int kBaseExposure = 8;

    bool valid = false;
    int exposureSlot = kBaseExposure;
    int textureSlots = kGraphFirst;

    static constexpr PrismaCompositeTextureLayout plan(
            std::size_t graphTextures, bool automaticExposure) {
        if (graphTextures > kMaxGraphTextures) return {};
        const int graphEnd = kGraphFirst + static_cast<int>(graphTextures);
        const int exposureSlot = graphEnd > kBaseExposure
            ? graphEnd : kBaseExposure;
        const int bound = automaticExposure && exposureSlot + 1 > graphEnd
            ? exposureSlot + 1 : graphEnd;
        return {true, exposureSlot, bound};
    }
};

static_assert(PrismaCompositeTextureLayout::plan(0, true).exposureSlot == 8);
static_assert(PrismaCompositeTextureLayout::plan(3, true).textureSlots == 9);
static_assert(PrismaCompositeTextureLayout::plan(4, true).exposureSlot == 9);
static_assert(PrismaCompositeTextureLayout::plan(4, true).textureSlots == 10);
static_assert(!PrismaCompositeTextureLayout::plan(5, true).valid);

} // namespace aiverse::render
