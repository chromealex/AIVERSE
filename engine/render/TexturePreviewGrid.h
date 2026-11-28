#pragma once
#include "engine/bake/Half.h"
#include "engine/gfx/Gfx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace aiverse::render {

struct TexturePreviewGrid {
    int width = 0, height = 0, columns = 0, rows = 0;
    std::vector<std::array<uint8_t, 3>> colors;
    std::string error;
};

constexpr size_t texturePreviewPixelBytes(gfx::PixelFormat format) {
    return gfx::uncompressedTexturePixelBytes(format);
}

inline TexturePreviewGrid texturePreviewGrid(const gfx::TextureReadbackResult& image, bool preview2D = false, bool fullResolution = false) {
    TexturePreviewGrid grid;
    if (!image.success()) {
        grid.error = "GPU readback failed or resource was released.";
        return grid;
    }
    const size_t stride = texturePreviewPixelBytes(image.format);
    if (!stride) {
        grid.error = "Readback returned an unsupported preview format.";
        return grid;
    }
    const uint64_t pixels = uint64_t(image.width) * uint64_t(image.height);
    if (image.pixels.size() % stride != 0 || image.pixels.size() / stride != pixels) {
        grid.error = "Readback pixel count does not match the texture dimensions.";
        return grid;
    }
    grid.width = image.width;
    grid.height = image.height;
    grid.columns = std::min(32, image.width);
    grid.rows = std::min(32, image.height);
    if (fullResolution) {
        grid.columns = image.width;
        grid.rows = image.height;
    } else if (preview2D) {
        const double scale = std::min(1.0, 160.0 / std::max(image.width, image.height));
        grid.columns = std::max(1, int(std::round(image.width * scale)));
        grid.rows = std::max(1, int(std::round(image.height * scale)));
    }
    using Format = gfx::PixelFormat;
    const auto format = image.format;
    const bool half = format == Format::R16F || format == Format::RG16F || format == Format::RGBA16F;
    const bool single = format == Format::R32F || format == Format::RG32F || format == Format::RGBA32F;
    const size_t channelBytes = half ? 2 : single ? 4 : 1;
    const size_t channels = stride / channelBytes;
    grid.colors.reserve(grid.columns * grid.rows);
    for (int y = 0; y < grid.rows; ++y) {
        const size_t sy = uint64_t(y) * grid.height / grid.rows;
        for (int x = 0; x < grid.columns; ++x) {
            const size_t sx = uint64_t(x) * grid.width / grid.columns;
            const auto* sample = image.pixels.data() + (sy * grid.width + sx) * stride;
            std::array<uint8_t, 3> rgb{};
            for (size_t c = 0; c < std::min(channels, rgb.size()); ++c) {
                if (channelBytes == 1) { rgb[c] = sample[c]; continue; }
                float linear;
                if (single) {
                    std::memcpy(&linear, sample + c * sizeof(float), sizeof(float));
                } else {
                    const auto bits = uint16_t(sample[c * 2]) | (uint16_t(sample[c * 2 + 1]) << 8);
                    linear = bake::halfToFloat(bits);
                }
                const float value = std::isfinite(linear) ? std::max(0.f, linear) : 0.f;
                const float mapped = value / (1.f + value);
                const float display = mapped <= .0031308f ? mapped * 12.92f : 1.055f * std::pow(mapped, 1.f / 2.4f) - .055f;
                rgb[c] = uint8_t(std::clamp(int(display * 255.f + .5f), 0, 255));
            }
            if (channels == 1) rgb.fill(rgb[0]);
            grid.colors.push_back(rgb);
        }
    }
    return grid;
}
} // namespace aiverse::render
