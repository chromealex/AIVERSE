// Shared GLSL/MSL colour contract. Scene shaders keep radiance in RGBA16F;
// the compatibility variant preserves the original gamma-2/LDR output exactly.
#pragma once
#include <string>

namespace aiverse::gfx {
inline std::string colorShaderPrefix(bool metal, bool hdr) {
    std::string s = hdr ? "#define AIVERSE_HDR 1\n" : "#define AIVERSE_HDR 0\n";
    s += metal ? "#include <metal_stdlib>\nusing namespace metal;\n#define AIVERSE_V3 float3\n#define AIVERSE_V4 float4\n#define AIVERSE_FN static\n"
               : "#define AIVERSE_V3 vec3\n#define AIVERSE_V4 vec4\n#define AIVERSE_FN\n";
    s += R"(
AIVERSE_FN AIVERSE_V3 rtsSrgbDecode(AIVERSE_V3 c) {
    c = max(c, AIVERSE_V3(0.0));
    AIVERSE_V3 lo = c / 12.92;
    AIVERSE_V3 hi = pow((c + 0.055) / 1.055, AIVERSE_V3(2.4));
    return AIVERSE_V3(c.r <= 0.04045 ? lo.r : hi.r, c.g <= 0.04045 ? lo.g : hi.g, c.b <= 0.04045 ? lo.b : hi.b);
}
AIVERSE_FN AIVERSE_V3 rtsSrgbEncode(AIVERSE_V3 c) {
    c = max(c, AIVERSE_V3(0.0));
    AIVERSE_V3 lo = c * 12.92;
    AIVERSE_V3 hi = 1.055 * pow(c, AIVERSE_V3(1.0 / 2.4)) - 0.055;
    return AIVERSE_V3(c.r <= 0.0031308 ? lo.r : hi.r, c.g <= 0.0031308 ? lo.g : hi.g, c.b <= 0.0031308 ? lo.b : hi.b);
}
AIVERSE_FN AIVERSE_V3 rtsDecodeScene(AIVERSE_V3 c) {
#if AIVERSE_HDR
    return rtsSrgbDecode(c);
#else
    return c * c;
#endif
}
AIVERSE_FN AIVERSE_V3 rtsEncodeScene(AIVERSE_V3 c) {
#if AIVERSE_HDR
    return max(c, AIVERSE_V3(0.0));
#else
    return sqrt(c);
#endif
}
AIVERSE_FN AIVERSE_V3 rtsToneScene(AIVERSE_V3 c, bool tonemap) {
#if AIVERSE_HDR
    return max(c, AIVERSE_V3(0.0));
#else
    return tonemap ? clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0) : clamp(c, 0.0, 1.0);
#endif
}
// Display-authored sprites/overlays: decode RGB, keep coverage linear.
AIVERSE_FN AIVERSE_V4 rtsDisplay(AIVERSE_V4 c) {
#if AIVERSE_HDR
    return AIVERSE_V4(rtsSrgbDecode(c.rgb), c.a);
#else
    return c;
#endif
}
)";
    return s;
}
} // namespace aiverse::gfx
