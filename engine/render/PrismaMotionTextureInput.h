#pragma once
// Stage 32: caller-declared metadata for borrowed screen-space RHI inputs.
// No GPU texture is owned or destroyed by this value. The declared dimensions
// and format can be verified against a view without CPU/GPU readback.
// A declaration is NOT proof of the underlying GPU allocation's actual size;
// providers must declare the truth. Backends can add RHI introspection later.
#include "../gfx/Gfx.h"

namespace aiverse::render {
struct MotionTextureInput {
    gfx::TextureHandle texture{};
    int width = 0, height = 0;
    gfx::PixelFormat format = gfx::PixelFormat::RGBA8;
    bool hasMetadata = false;

    static MotionTextureInput legacy(gfx::TextureHandle handle) {
        MotionTextureInput i;
        i.texture = handle;
        return i;
    }
    static MotionTextureInput declared(gfx::TextureHandle handle,
                                       int w, int h, gfx::PixelFormat f) {
        MotionTextureInput i;
        i.texture = handle;
        i.width = w;
        i.height = h;
        i.format = f;
        i.hasMetadata = true;
        return i;
    }
    bool validFor(int w, int h, gfx::PixelFormat requiredFormat) const {
        return texture.valid() &&
            (!hasMetadata ||
             (width == w && height == h && format == requiredFormat));
    }
};
} // namespace aiverse::render
