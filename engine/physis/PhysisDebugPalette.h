// PHYSIS debug-view palette: pure CPU data-to-color mapping.
// Non-authoritative; intended for editor heatmaps and external diagnostic tools.
// No renderer, genre, Box3D, NEREID, Terra or GPU dependency.
#pragma once
#include "PhysisDiagnostics.h"
#include <cstdint>

namespace aiverse::physis {

struct DebugColor {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    constexpr bool operator==(const DebugColor&) const = default;
};

enum class DebugPalette : std::uint8_t {
    Thermal = 0,       // dark blue -> cyan -> yellow -> red
    Monochrome = 1,    // near black -> bright white
    Ocean = 2          // dark blue -> light turquoise
};

// Integer-only interpolation for deterministic, platform-independent previews.
inline constexpr std::uint8_t blendDebugChannel(std::uint8_t start,
                                                std::uint8_t end,
                                                std::uint16_t t) {
    return static_cast<std::uint8_t>(
        (std::uint32_t(start) * (65535u - t) + std::uint32_t(end) * t + 32767u) /
        65535u);
}

inline DebugColor debugGradient(DebugColor low, DebugColor high, std::uint16_t t) {
    return {blendDebugChannel(low.r, high.r, t),
            blendDebugChannel(low.g, high.g, t),
            blendDebugChannel(low.b, high.b, t),
            255};
}

inline DebugColor debugColor(std::uint16_t normalized,
                            DebugPalette palette = DebugPalette::Thermal) {
    if (palette == DebugPalette::Monochrome)
        return debugGradient({15, 19, 29, 255}, {238, 244, 252, 255}, normalized);
    if (palette == DebugPalette::Ocean)
        return debugGradient({10, 30, 75, 255}, {103, 236, 211, 255}, normalized);
    // Piecewise ramp gives contrast at the cold/mid/hot regions.
    if (normalized < 21845u)
        return debugGradient({27, 33, 103, 255}, {52, 192, 231, 255},
                             static_cast<std::uint16_t>(std::uint32_t(normalized) * 3u));
    if (normalized < 43690u)
        return debugGradient({52, 192, 231, 255}, {244, 214, 86, 255},
                             static_cast<std::uint16_t>((std::uint32_t(normalized) - 21845u) * 3u));
    return debugGradient({244, 214, 86, 255}, {228, 45, 63, 255},
                         static_cast<std::uint16_t>((std::uint32_t(normalized) - 43690u) * 3u));
}

// Transparent for missing cells; no false zero-valued readings outside PHYSIS.
inline DebugColor diagnosticColor(const DiagnosticRaster& raster, std::size_t index,
                                  DebugPalette palette = DebugPalette::Thermal) {
    if (index >= raster.valid.size() || index >= raster.rawValues.size() ||
        !raster.valid[index]) return {};
    return debugColor(diagnosticNormalized(raster, index), palette);
}

} // namespace aiverse::physis
