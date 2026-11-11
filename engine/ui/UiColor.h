// Colour-space helpers of the UI layer (no GPU, no RmlUi: unit-tested in tests/tests.cpp).
//
// "Linear UI blending" (game.json "ui": {"blend": "linear"}) makes translucent UI blend the way a Unity
// project in linear colour space does: vertex colours and images are decoded from sRGB to linear light,
// blended in linear light into an sRGB-encoded layer, and composited over the scene in linear light.
// Alpha is linear in both modes; font atlases are coverage (alpha only), never gamma-decoded.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/data/Json.h"

namespace aiverse::ui {

enum class UiBlend { Srgb, Linear };   // Srgb: today's behaviour (blending on sRGB-encoded values)

// Exact sRGB transfer functions (IEC 61966-2-1), the same curves the shaders and GPU formats use.
inline float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float linearToSrgb(float l) {
    l = l < 0 ? 0 : l > 1 ? 1 : l;
    return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}
inline uint8_t linearToSrgb8(float l) { return static_cast<uint8_t>(std::lround(linearToSrgb(l) * 255.0f)); }

// Straight-alpha sRGB RGBA8 pixels -> colour premultiplied in LINEAR light, stored sRGB-encoded
// (rgb' = enc(dec(rgb) * a)), for an RGBA8_sRGB texture: the GPU decodes before filtering, so filtering
// happens on premultiplied linear values (no fringes) and the shader gets exactly dec(rgb) * a.
// Opaque and fully transparent texels need no maths; the rest go through two 256-entry tables.
inline void premultiplyLinearSrgb8(uint8_t* rgba, size_t pixels) {
    static const auto tables = [] {
        struct T { std::array<float, 256> dec; std::array<float, 255> up; } t{};   // up[k]: smallest linear value of code k+1
        for (int i = 0; i < 256; ++i) t.dec[size_t(i)] = srgbToLinear(float(i) / 255.0f);
        for (int k = 0; k < 255; ++k) t.up[size_t(k)] = srgbToLinear((float(k) + 0.5f) / 255.0f);
        return t;
    }();
    for (size_t i = 0; i < pixels; ++i) {
        uint8_t* p = rgba + i * 4;
        const uint8_t a = p[3];
        if (a == 255) continue;
        if (a == 0) { p[0] = p[1] = p[2] = 0; continue; }
        const float af = float(a) / 255.0f;
        for (int c = 0; c < 3; ++c) {
            const float v = tables.dec[p[c]] * af;
            size_t lo = 0, hi = 255;   // code = number of thresholds <= v (rounding in the encoded space)
            while (lo < hi) { const size_t mid = (lo + hi) / 2; if (tables.up[mid] <= v) lo = mid + 1; else hi = mid; }
            p[c] = static_cast<uint8_t>(lo);
        }
    }
}

// game.json "ui": {"blend": "srgb" | "linear"}. Unknown values keep the default and report it in `error`.
inline UiBlend parseUiBlend(const Json& manifest, std::string* error = nullptr) {
    const Json& ui = manifest["ui"];
    if (!ui.isObject() || !ui.has("blend")) return UiBlend::Srgb;
    const std::string v = ui["blend"].str();
    if (v == "linear") return UiBlend::Linear;
    if (v != "srgb" && error) *error = "game.json ui.blend: unknown value '" + v + "' (expected \"srgb\" or \"linear\")";
    return UiBlend::Srgb;
}

} // namespace aiverse::ui
