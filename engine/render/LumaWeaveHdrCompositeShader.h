// Separate opt-in HDR compositor for the direct-light output from LumaWeave.
// Requires a full-resolution receiver MATERIAL buffer made by the host:
//   RGB = linear baseColor; A = metallic (0..1).
// The compositor uses real scene depth to reject low-res lighting from the
// wrong surface, applies Lambert albedo*(1-metallic)/pi and ADDS only the
// new direct diffuse layer. No specular approximation, no scene-color-as-albedo.
// Never enable if the legacy LightSystem also contributes point/spot direct.
#pragma once

#define AIVERSE_MSL_LUMAWEAVE_HDR_COMPOSITE R"MSL(
#include <metal_stdlib>
using namespace metal;
struct LumaComposeVertex {float2 pos [[attribute(0)]];};
struct LumaComposeVaryings {float4 position [[position]];float2 uv;};
struct LumaComposeParams {
    float4 options; // x=flip RT texture y, y=depth tolerance, z=exposure scale, w=already BRDF reflected
};
vertex LumaComposeVaryings vs_main(LumaComposeVertex v [[stage_in]]) {
    LumaComposeVaryings out;
    out.position=float4(v.pos,0,1);
    out.uv=v.pos*.5f+.5f;
    return out;
}
constexpr sampler lumaCompositeNearest(coord::normalized,filter::nearest,
                                        address::clamp_to_edge);
fragment float4 fs_main(LumaComposeVaryings in [[stage_in]],
                        constant LumaComposeParams& params [[buffer(1)]],
                        texture2d<float> direct [[texture(0)]],
                        texture2d<float> receiverMaterial [[texture(1)]],
                        depth2d<float> sceneDepth [[texture(2)]],
                        depth2d<float> receiverDepth [[texture(3)]]) {
    float2 projectedUv=in.uv;
    float2 texUv=params.options.x>.5f ?
                 float2(projectedUv.x,1.0f-projectedUv.y):projectedUv;
    float depth=sceneDepth.sample(lumaCompositeNearest,texUv);
    if(depth>=.99999f)return float4(0);
    // The material pass covers opaque Model/ModelDraw geometry only. If a
    // custom terrain/mesh pass occludes it, the main depth differs: do not
    // apply the hidden model's albedo to the front-most unknown material.
    const float receiverZ=receiverDepth.sample(lumaCompositeNearest,texUv);
    if(receiverZ>=.99999f ||
       abs(receiverZ-depth)>max(params.options.y,1e-5f))
        return float4(0);
    float4 material=receiverMaterial.sample(lumaCompositeNearest,texUv);
    float3 diffuse=clamp(material.rgb,0.0f,1.0f)*
                   (1.0f-clamp(material.a,0.0f,1.0f));
    // Material-reflected PBR mode still needs depth/receiver checks, but a
    // black dielectric may have a valid 0.04 Fresnel specular response.
    if(params.options.w<.5f && all(diffuse<=float3(0)))return float4(0);

    // 2x2 low-resolution taps with depth rejection reduce light leaks at
    // silhouettes. Do not renormalize rejected neighbours to their brightest
    // remaining sample: missing coverage should not invent extra direct energy.
    float2 uv=texUv*float2(direct.get_width(),direct.get_height())-.5f;
    int2 base=int2(floor(uv));
    float2 f=fract(uv);
    float3 accumulated=float3(0);
    float weightSum=0.0f;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        int2 coord=clamp(base+int2(x,y),int2(0),
                         int2(int(direct.get_width())-1,int(direct.get_height())-1));
        float4 value=direct.read(uint2(coord));
        if(value.a<=0.0f||value.a>=.99999f)continue;
        float delta=abs(depth-value.a);
        if(delta>params.options.y)continue;
        float depthWeight=1.0f-delta/max(params.options.y,1e-6f);
        float bilinear=(x==0?1.0f-f.x:f.x)*(y==0?1.0f-f.y:f.y);
        float w=bilinear*depthWeight;
        accumulated+=max(value.rgb,float3(0))*w;
        weightSum+=w;
    }
    if(weightSum<=0)return float4(0);
    const float3 irradiance=accumulated/weightSum;
    // w=1: LumaWeaveSCREEN_PBR computed reflected diffuse+GGX specular per
    // light BEFORE stochastic sampling. Multiplying base color a second time
    // would lose the Fresnel energy and destroy metallic reflections.
    const float3 reflected=params.options.w>.5f ?
                           irradiance*max(params.options.z,0.0f) :
                           irradiance*diffuse*(1.0f/3.14159265358979323846f)*
                           max(params.options.z,0.0f);
    return float4(max(reflected,float3(0)),0);
}
)MSL"
