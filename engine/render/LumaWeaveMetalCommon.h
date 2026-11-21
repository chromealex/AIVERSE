// Shared Metal source structs and direct candidate evaluation for both
// explicit-receiver and future screen-space LumaWeave compute shaders.
// The GI BVH is defined once by GiGpuBvhMetalShader.h, not here.
#pragma once
#define AIVERSE_MSL_LUMAWEAVE_COMMON \
    "\n" \
    "struct LumaLight {\n" \
    "    float4 positionRadius, colorIntensity, directionCone, settings;\n" \
    "    uint4 identity; // key lo, key hi, kind, indirect-GI flag\n" \
    "};\n" \
    "struct LumaCell {uint first,count,total,flags;};\n" \
    "struct LumaReceiver {float4 worldPosition, normal;};\n" \
    "struct LumaParams {\n" \
    "    float4 origin, cellSize;\n" \
    "    uint4 dims;    // number of cells x,y,z,total\n" \
    "    uint4 limits;  // sources, fast references, receiver count, samples per receiver\n" \
    "    uint4 control; // BVH node count, RNG frame seed, texture width, reserved\n" \
    "};\n" \
    "static float3 directUnshadowed(LumaLight l,float3 receiver,float3 normal) {\n" \
    "    float3 toLight=l.positionRadius.xyz-receiver;\n" \
    "    float d2=dot(toLight,toLight);\n" \
    "    float radius=l.positionRadius.w;\n" \
    "    if(d2<=.0001f || d2>=radius*radius || l.colorIntensity.w<=0.0f)\n" \
    "        return float3(0.0);\n" \
    "    float d=sqrt(d2);\n" \
    "    float3 L=toLight/d;\n" \
    "    float ndl=max(0.0f,dot(normal,L));\n" \
    "    if(ndl==0.0f)return float3(0.0);\n" \
    "    float angular=1.0f;\n" \
    "    if(l.identity.z==1u) {\n" \
    "        float cone=dot(l.directionCone.xyz,-L);\n" \
    "        float outer=l.directionCone.w;\n" \
    "        if(cone<=outer)return float3(0.0);\n" \
    "        float inner=l.settings.x;\n" \
    "        if(inner>outer)angular=clamp((cone-outer)/(inner-outer),0.0f,1.0f);\n" \
    "    }\n" \
    "    float attenuation=1.0f-d2/(radius*radius);\n" \
    "    attenuation=attenuation*attenuation/(1.0f+d2);\n" \
    "    return l.colorIntensity.xyz*(l.colorIntensity.w*ndl*angular*attenuation);\n" \
    "}\n" \
    "static float importance(float3 rgb,float bias) {\n" \
    "    return max(0.0f,dot(rgb,float3(.2126f,.7152f,.0722f)))*bias;\n" \
    "}\n" \
    "static uint nextRandom(thread uint& seed) {\n" \
    "    // Minimal reproducible shader RNG, not bit-identical to CPU SplitMix64.\n" \
    "    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;\n" \
    "    return seed;\n" \
    "}\n"
