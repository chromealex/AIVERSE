// Shared GLSL/MSL colour contract. Scene shaders keep radiance in RGBA16F;
// the compatibility variant preserves the original gamma-2/LDR output exactly.
#pragma once
#include <string>

namespace rts::gfx {
inline std::string colorShaderPrefix(bool metal, bool hdr) {
    std::string s = hdr ? "#define RTS_HDR 1\n" : "#define RTS_HDR 0\n";
    s += metal ? "#include <metal_stdlib>\nusing namespace metal;\n#define RTS_V3 float3\n#define RTS_V4 float4\n#define RTS_FN static\n"
               : "#define RTS_V3 vec3\n#define RTS_V4 vec4\n#define RTS_FN\n";
    s += R"(
RTS_FN RTS_V3 rtsSrgbDecode(RTS_V3 c) {
    c = max(c, RTS_V3(0.0));
    RTS_V3 lo = c / 12.92;
    RTS_V3 hi = pow((c + 0.055) / 1.055, RTS_V3(2.4));
    return RTS_V3(c.r <= 0.04045 ? lo.r : hi.r, c.g <= 0.04045 ? lo.g : hi.g, c.b <= 0.04045 ? lo.b : hi.b);
}
RTS_FN RTS_V3 rtsSrgbEncode(RTS_V3 c) {
    c = max(c, RTS_V3(0.0));
    RTS_V3 lo = c * 12.92;
    RTS_V3 hi = 1.055 * pow(c, RTS_V3(1.0 / 2.4)) - 0.055;
    return RTS_V3(c.r <= 0.0031308 ? lo.r : hi.r, c.g <= 0.0031308 ? lo.g : hi.g, c.b <= 0.0031308 ? lo.b : hi.b);
}
RTS_FN RTS_V3 rtsDecodeScene(RTS_V3 c) {
#if RTS_HDR
    return rtsSrgbDecode(c);
#else
    return c * c;
#endif
}
RTS_FN RTS_V3 rtsEncodeScene(RTS_V3 c) {
#if RTS_HDR
    return max(c, RTS_V3(0.0));
#else
    return sqrt(c);
#endif
}
RTS_FN RTS_V3 rtsToneScene(RTS_V3 c, bool tonemap) {
#if RTS_HDR
    return max(c, RTS_V3(0.0));
#else
    return tonemap ? clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0) : clamp(c, 0.0, 1.0);
#endif
}
// Display-authored sprites/overlays: decode RGB, keep coverage linear.
RTS_FN RTS_V4 rtsDisplay(RTS_V4 c) {
#if RTS_HDR
    return RTS_V4(rtsSrgbDecode(c.rgb), c.a);
#else
    return c;
#endif
}
)";
    return s;
}
} // namespace rts::gfx
