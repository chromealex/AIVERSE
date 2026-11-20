// Vulkan-only HDR additive direct compositor. The screen compute output
// is ALREADY PBR GGX+diffuse material reflected; do not multiply base color
// again. Double depth receiver checks protect unknown custom/transparent
// surfaces from borrowing an occluded model's material. Scene HDR target must
// be used with an independent depth attachment, never sample an attached depth.
// No baked, sky or bounced GI term is added here.
#pragma once
namespace aiverse::render::luma_vk {
inline constexpr const char* kHdrCompositeVS = R"GLSL(
layout(location=0) in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv=a_pos*0.5+0.5;
    gl_Position=vec4(a_pos,0.0,1.0);
}
)GLSL";
inline constexpr const char* kHdrCompositeFS = R"GLSL(
in vec2 v_uv;
layout(std140) uniform U0 {vec4 u_options;}; // depth tolerance, intensity, reserve...
uniform sampler2D tex0; // low-resolution direct PBR RGB + real depth A
uniform sampler2D tex1; // HDR scene depth
uniform sampler2D tex2; // opaque material receiver depth
uniform sampler2D tex3; // opaque surface/normal receiver depth
layout(location=0) out vec4 o_color;
void main() {
    float scene=texture(tex1,v_uv).r;
    float material=texture(tex2,v_uv).r;
    float surface=texture(tex3,v_uv).r;
    float tolerance=clamp(u_options.x,0.00001,0.02);
    if(scene>=0.99999 || material>=0.99999 || surface>=0.99999 ||
       abs(scene-material)>tolerance || abs(scene-surface)>tolerance) {
        o_color=vec4(0.0);return;
    }
    ivec2 size=textureSize(tex0,0);
    vec2 scaled=v_uv*vec2(size)-vec2(0.5);
    ivec2 base=ivec2(floor(scaled));
    vec2 frac=fract(scaled);
    vec3 light=vec3(0.0);
    float weightSum=0.0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec2 coord=clamp(base+ivec2(x,y),ivec2(0),size-ivec2(1));
        vec4 value=texelFetch(tex0,coord,0);
        if(value.a<=0.0 || value.a>=0.99999)continue;
        float distance=abs(scene-value.a);
        if(distance>tolerance)continue;
        float edgeWeight=1.0-distance/max(tolerance,0.00001);
        float bilinear=(x==0?1.0-frac.x:frac.x)*
                       (y==0?1.0-frac.y:frac.y);
        float w=bilinear*edgeWeight;
        light+=max(value.rgb,vec3(0.0))*w;
        weightSum+=w;
    }
    if(weightSum<=0.0){o_color=vec4(0.0);return;}
    // BRDF reflected already in compute: add exactly one direct term.
    o_color=vec4(light/weightSum*max(u_options.y,0.0),0.0);
}
)GLSL";
} // namespace aiverse::render::luma_vk
