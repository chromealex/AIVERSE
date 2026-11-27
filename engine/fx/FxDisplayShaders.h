#pragma once
// Display-referred additive geometry: colour remains encoded, depth is borrowed
// from the completed scene. PRISMA owns all full-screen image processing.
namespace aiverse::fx::display_shaders {
inline constexpr const char* kVS=R"(
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_color;
layout(std140) uniform U0 {mat4 viewProj;vec4 cameraFog;vec4 outputSize;};
out vec2 v_uv;
out vec4 v_color;
void main(){
    gl_Position=viewProj*vec4(a_pos,1.0);
    v_uv=a_uv;v_color=a_color;
    v_color.a*=exp(-length(a_pos-cameraFog.xyz)*cameraFog.w);
}
)";
inline constexpr const char* kFS=R"(
precision highp float;
layout(std140) uniform U0 {mat4 viewProj;vec4 cameraFog;vec4 outputSize;};
uniform sampler2D tex0;
uniform sampler2D tex1;
in vec2 v_uv;
in vec4 v_color;
out vec4 o_color;
void main(){
    vec2 uv=gl_FragCoord.xy/outputSize.xy;
    if(gl_FragCoord.z>texture(tex1,uv).r+0.000001)discard;
    o_color=texture(tex0,v_uv)*v_color;
}
)";
inline constexpr const char* kMSL=R"(
#include <metal_stdlib>
using namespace metal;
struct U0 {float4x4 viewProj;float4 cameraFog;float4 outputSize;};
struct VIn {float3 pos [[attribute(0)]];float2 uv [[attribute(1)]];float4 color [[attribute(2)]];};
struct VOut {float4 pos [[position]];float2 uv;float4 color;};
vertex VOut vs_main(VIn in [[stage_in]],constant U0& u [[buffer(1)]]) {
    VOut o;o.pos=u.viewProj*float4(in.pos,1);o.uv=in.uv;o.color=in.color;
    o.color.a*=exp(-length(in.pos-u.cameraFog.xyz)*u.cameraFog.w);return o;
}
fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
    texture2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]],
    depth2d<float> tex1 [[texture(1)]],sampler s1 [[sampler(1)]]) {
    if(in.pos.z>tex1.sample(s1,in.pos.xy/u.outputSize.xy)+0.000001)discard_fragment();
    return tex0.sample(s0,in.uv)*in.color;
}
)";
} // namespace aiverse::fx::display_shaders
