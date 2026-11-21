#pragma once
// PRISMA Stage 32: reactive coverage for SpriteBatch::drawWater.
// Replays the same SpriteVertex GPU mesh, base alpha texture and tint alpha.
// Rendered only within Renderer3D's already-open RGBA8 reactive target.
// Never writes/samples a simultaneously attached scene depth texture.
namespace aiverse::render::sprite_reactive_shaders {
inline constexpr const char* kVS = R"(
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_color;
layout(std140) uniform U0 { mat4 viewProj; vec4 params; };
out vec2 v_uv;
out float v_alpha;
void main() {
    gl_Position = viewProj * vec4(a_pos,1.0);
    v_uv = a_uv;
    v_alpha = a_color.a;
}
)";
inline constexpr const char* kFS = R"(
layout(std140) uniform U0 { mat4 viewProj; vec4 params; };
uniform sampler2D tex0; // original water sheet texture
uniform sampler2D tex1; // sampled scene depth
in vec2 v_uv;
in float v_alpha;
out vec4 o_color;
void main() {
    vec2 screen = gl_FragCoord.xy / params.xy;
    if (gl_FragCoord.z > texture(tex1,screen).r + 0.0015) discard;
    float alpha = clamp(texture(tex0,v_uv).a * v_alpha,0.0,1.0);
    if (alpha < 0.002) discard;
    o_color = vec4(alpha,0.0,0.0,1.0);
}
)";
inline constexpr const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 {float4x4 viewProj;float4 params;};
struct VIn {
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};
struct VOut {
    float4 position [[position]];
    float2 uv;
    float alpha;
};
vertex VOut vs_main(VIn in [[stage_in]],constant U0& u [[buffer(1)]]) {
    VOut o;
    o.position = u.viewProj*float4(in.pos,1.0);
    o.uv = in.uv; o.alpha=in.color.a;
    return o;
}
fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
    texture2d<float> tex0 [[texture(0)]],sampler tex0_s [[sampler(0)]],
    depth2d<float> tex1 [[texture(1)]],sampler tex1_s [[sampler(1)]]) {
    const float2 screen = in.position.xy/u.params.xy;
    if (in.position.z > tex1.sample(tex1_s,screen)+0.0015)
        discard_fragment();
    const float alpha = clamp(tex0.sample(tex0_s,in.uv).a*in.alpha,0.0,1.0);
    if (alpha < 0.002) discard_fragment();
    return float4(alpha,0.0,0.0,1.0);
}
)";
} // namespace aiverse::render::sprite_reactive_shaders
