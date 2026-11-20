// Shared Metal source structs and direct candidate evaluation for both
// explicit-receiver and future screen-space LumaWeave compute shaders.
// The GI BVH is defined once by GiGpuBvhMetalShader.h, not here.
#pragma once
#define AIVERSE_MSL_LUMAWEAVE_COMMON R"MSL(
struct LumaLight {
    float4 positionRadius, colorIntensity, directionCone, settings;
    uint4 identity; // key lo, key hi, kind, indirect-GI flag
};
struct LumaCell {uint first,count,total,flags;};
struct LumaReceiver {float4 worldPosition, normal;};
struct LumaParams {
    float4 origin, cellSize;
    uint4 dims;    // number of cells x,y,z,total
    uint4 limits;  // sources, fast references, receiver count, samples per receiver
    uint4 control; // BVH node count, RNG frame seed, texture width, reserved
};
static float3 directUnshadowed(LumaLight l,float3 receiver,float3 normal) {
    float3 toLight=l.positionRadius.xyz-receiver;
    float d2=dot(toLight,toLight);
    float radius=l.positionRadius.w;
    if(d2<=.0001f || d2>=radius*radius || l.colorIntensity.w<=0.0f)
        return float3(0.0);
    float d=sqrt(d2);
    float3 L=toLight/d;
    float ndl=max(0.0f,dot(normal,L));
    if(ndl==0.0f)return float3(0.0);
    float angular=1.0f;
    if(l.identity.z==1u) {
        float cone=dot(l.directionCone.xyz,-L);
        float outer=l.directionCone.w;
        if(cone<=outer)return float3(0.0);
        float inner=l.settings.x;
        if(inner>outer)angular=clamp((cone-outer)/(inner-outer),0.0f,1.0f);
    }
    float attenuation=1.0f-d2/(radius*radius);
    attenuation=attenuation*attenuation/(1.0f+d2);
    return l.colorIntensity.xyz*(l.colorIntensity.w*ndl*angular*attenuation);
}
static float importance(float3 rgb,float bias) {
    return max(0.0f,dot(rgb,float3(.2126f,.7152f,.0722f)))*bias;
}
static uint nextRandom(thread uint& seed) {
    // Minimal reproducible shader RNG, not bit-identical to CPU SplitMix64.
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
    return seed;
}
)MSL"
