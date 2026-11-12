// Spine shaders (SpineRender), GLSL ES 3.00 bodies and MSL.
//
//   vertex: pos (float2, skeleton space), uv (float2), color (ubyte4n, straight alpha)
//   U0:     mat4 mvp; vec4 params (x page is premultiplied, y additive: alpha out 0,
//           z multiply: rgb + (1 - a) for dst * (src + 1 - srcA), w screen: alpha = max(rgb)); vec4 tint
//   tex0:   atlas page
#pragma once

namespace aiverse::spine::shaders {

inline const char* kSpineVS = R"(
layout(std140) uniform U0 { mat4 u_mvp; vec4 u_params; vec4 u_tint; };
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
out vec2 v_uv;
out vec4 v_col;
void main() { v_uv = a_uv; v_col = a_col * u_tint; gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0); }
)";

inline const char* kSpineFS = R"(
layout(std140) uniform U0 { mat4 u_mvp; vec4 u_params; vec4 u_tint; };
uniform sampler2D tex0;
in vec2 v_uv;
in vec4 v_col;
out vec4 o_color;
void main() {
    vec4 t = texture(tex0, v_uv);
    if (u_params.x < 0.5) t.rgb *= t.a;
    vec4 c = t * vec4(v_col.rgb * v_col.a, v_col.a);
    if (u_params.y > 0.5) c.a = 0.0;
    if (u_params.z > 0.5) c.rgb += vec3(1.0 - c.a);
    if (u_params.w > 0.5) c.a = max(c.r, max(c.g, c.b));
    o_color = c;
}
)";

inline const char* kSpineMSL = R"(#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 mvp; float4 params; float4 tint; };
struct VIn { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o; o.pos = u.mvp * float4(in.pos, 0.0, 1.0); o.uv = in.uv; o.col = in.col * u.tint; return o;
}
fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]],
                        texture2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]]) {
    float4 t = tex0.sample(s0, in.uv);
    if (u.params.x < 0.5) t.rgb *= t.a;
    float4 c = t * float4(in.col.rgb * in.col.a, in.col.a);
    if (u.params.y > 0.5) c.a = 0.0;
    if (u.params.z > 0.5) c.rgb += float3(1.0 - c.a);
    if (u.params.w > 0.5) c.a = max(c.r, max(c.g, c.b));
    return c;
}
)";

} // namespace aiverse::spine::shaders
