// Separate opt-in HDR compositor for the direct-light output from LumaWeave.
// Requires a full-resolution receiver MATERIAL buffer made by the host:
//   RGB = linear baseColor; A = metallic (0..1).
// The compositor uses real scene depth to reject low-res lighting from the
// wrong surface, applies Lambert albedo*(1-metallic)/pi and ADDS only the
// new direct diffuse layer. No specular approximation, no scene-color-as-albedo.
// Never enable if the legacy LightSystem also contributes point/spot direct.
#pragma once

#define AIVERSE_MSL_LUMAWEAVE_HDR_COMPOSITE \
    "\n" \
    "#include <metal_stdlib>\n" \
    "using namespace metal;\n" \
    "struct LumaComposeVertex {float2 pos [[attribute(0)]];};\n" \
    "struct LumaComposeVaryings {float4 position [[position]];float2 uv;};\n" \
    "struct LumaComposeParams {\n" \
    "    float4 options; // x=flip RT texture y, y=depth tolerance, z=exposure scale, w=already BRDF reflected\n" \
    "};\n" \
    "vertex LumaComposeVaryings vs_main(LumaComposeVertex v [[stage_in]]) {\n" \
    "    LumaComposeVaryings out;\n" \
    "    out.position=float4(v.pos,0,1);\n" \
    "    out.uv=v.pos*.5f+.5f;\n" \
    "    return out;\n" \
    "}\n" \
    "constexpr sampler lumaCompositeNearest(coord::normalized,filter::nearest,\n" \
    "                                        address::clamp_to_edge);\n" \
    "fragment float4 fs_main(LumaComposeVaryings in [[stage_in]],\n" \
    "                        constant LumaComposeParams& params [[buffer(1)]],\n" \
    "                        texture2d<float> direct [[texture(0)]],\n" \
    "                        texture2d<float> receiverMaterial [[texture(1)]],\n" \
    "                        depth2d<float> sceneDepth [[texture(2)]],\n" \
    "                        depth2d<float> receiverDepth [[texture(3)]]) {\n" \
    "    float2 projectedUv=in.uv;\n" \
    "    float2 texUv=params.options.x>.5f ?\n" \
    "                 float2(projectedUv.x,1.0f-projectedUv.y):projectedUv;\n" \
    "    float depth=sceneDepth.sample(lumaCompositeNearest,texUv);\n" \
    "    if(depth>=.99999f)return float4(0);\n" \
    "    // The material pass covers opaque Model/ModelDraw geometry only. If a\n" \
    "    // custom terrain/mesh pass occludes it, the main depth differs: do not\n" \
    "    // apply the hidden model's albedo to the front-most unknown material.\n" \
    "    const float receiverZ=receiverDepth.sample(lumaCompositeNearest,texUv);\n" \
    "    if(receiverZ>=.99999f ||\n" \
    "       abs(receiverZ-depth)>max(params.options.y,1e-5f))\n" \
    "        return float4(0);\n" \
    "    float4 material=receiverMaterial.sample(lumaCompositeNearest,texUv);\n" \
    "    float3 diffuse=clamp(material.rgb,0.0f,1.0f)*\n" \
    "                   (1.0f-clamp(material.a,0.0f,1.0f));\n" \
    "    // Material-reflected PBR mode still needs depth/receiver checks, but a\n" \
    "    // black dielectric may have a valid 0.04 Fresnel specular response.\n" \
    "    if(params.options.w<.5f && all(diffuse<=float3(0)))return float4(0);\n" \
    "\n" \
    "    // 2x2 low-resolution taps with depth rejection reduce light leaks at\n" \
    "    // silhouettes. Do not renormalize rejected neighbours to their brightest\n" \
    "    // remaining sample: missing coverage should not invent extra direct energy.\n" \
    "    float2 uv=texUv*float2(direct.get_width(),direct.get_height())-.5f;\n" \
    "    int2 base=int2(floor(uv));\n" \
    "    float2 f=fract(uv);\n" \
    "    float3 accumulated=float3(0);\n" \
    "    float weightSum=0.0f;\n" \
    "    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {\n" \
    "        int2 coord=clamp(base+int2(x,y),int2(0),\n" \
    "                         int2(int(direct.get_width())-1,int(direct.get_height())-1));\n" \
    "        float4 value=direct.read(uint2(coord));\n" \
    "        if(value.a<=0.0f||value.a>=.99999f)continue;\n" \
    "        float delta=abs(depth-value.a);\n" \
    "        if(delta>params.options.y)continue;\n" \
    "        float depthWeight=1.0f-delta/max(params.options.y,1e-6f);\n" \
    "        float bilinear=(x==0?1.0f-f.x:f.x)*(y==0?1.0f-f.y:f.y);\n" \
    "        float w=bilinear*depthWeight;\n" \
    "        accumulated+=max(value.rgb,float3(0))*w;\n" \
    "        weightSum+=w;\n" \
    "    }\n" \
    "    if(weightSum<=0)return float4(0);\n" \
    "    const float3 irradiance=accumulated/weightSum;\n" \
    "    // w=1: LumaWeaveSCREEN_PBR computed reflected diffuse+GGX specular per\n" \
    "    // light BEFORE stochastic sampling. Multiplying base color a second time\n" \
    "    // would lose the Fresnel energy and destroy metallic reflections.\n" \
    "    const float3 reflected=params.options.w>.5f ?\n" \
    "                           irradiance*max(params.options.z,0.0f) :\n" \
    "                           irradiance*diffuse*(1.0f/3.14159265358979323846f)*\n" \
    "                           max(params.options.z,0.0f);\n" \
    "    return float4(max(reflected,float3(0)),0);\n" \
    "}\n"
