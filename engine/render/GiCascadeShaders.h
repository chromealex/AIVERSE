// Optional dual-cascade dynamic diffuse GI. Only compiled for the separate
// DYNAMIC_GI_CASCADES permutation, never for ordinary model materials.
// tex13: fine RGBA16F L1; tex14: fine R8 validity; tex15: COMPLETE coarse L1.
// Coarse is bound only after all of its GPU probe tickets were fence-acknowledged.
// This keeps fragments within Metal's low-end 16 sampled-texture slots.
// The CPU reference is GiCascadeBlend.h (smoothstep fade across 1.5 cells).
#pragma once

#define AIVERSE_GLSL_CASCADE_GI R"GI(
#if defined(DYNAMIC_GI_CASCADES) && defined(DYNAMIC_GI_VALIDITY)
layout(std140) uniform U7 {
    vec4 u_dynamicGiFarOrigin;
    vec4 u_dynamicGiFarSize;
    vec4 u_dynamicGiFarRing;
};
uniform highp sampler2D tex15;
vec4 giCascadeLayer(vec3 p,vec3 n,vec4 origin,vec4 size,vec4 ring,
                    sampler2D atlas,bool masked) {
    vec3 logical=(p-origin.xyz)*origin.w;
    vec3 extent=size.xyz;
    vec3 distanceToEdge=min(logical,extent-logical);
    float border=min(min(distanceToEdge.x,distanceToEdge.y),distanceToEdge.z);
    if(border<=0.0)return vec4(0.0);
    float fade=smoothstep(0.0,1.5,border);
    vec3 coord=clamp(logical-vec3(.5),vec3(0.0),extent-1.0);
    ivec3 base=ivec3(floor(coord));
    vec3 f=fract(coord);
    int first=f.x>=f.y?(f.x>=f.z?0:2):(f.y>=f.z?1:2);
    int last=f.x<=f.y?(f.x<=f.z?0:2):(f.y<=f.z?1:2);
    if(first==last)last=(first+2)%3;
    int middle=3-first-last;
    vec4 normal=vec4(1.0,n);
    vec3 incident=vec3(0.0);
    float weightSum=0.0;
    for(int i=0;i<4;++i) {
        ivec3 corner=ivec3(0);
        if(i>=1)corner[first]=1;
        if(i>=2)corner[middle]=1;
        if(i>=3)corner[last]=1;
        float w=i==0?1.0-f[first]:i==1?f[first]-f[middle]:i==2?f[middle]-f[last]:f[last];
        if(w<=0.0)continue;
        ivec3 cell=clamp(base+corner,ivec3(0),ivec3(extent)-1);
        ivec3 physical=(cell+ivec3(ring.xyz))%ivec3(extent);
        int probe=physical.x+int(extent.x)*(physical.y+int(extent.y)*physical.z);
        if(masked && texelFetch(tex14,ivec2(probe%128,probe/128),0).r<0.5)continue;
        int offset=probe*3;
        vec3 rgb=vec3(dot(texelFetch(atlas,ivec2(offset%384,offset/384),0),normal),
                      dot(texelFetch(atlas,ivec2((offset+1)%384,(offset+1)/384),0),normal),
                      dot(texelFetch(atlas,ivec2((offset+2)%384,(offset+2)/384),0),normal));
        incident+=w*max(rgb,vec3(0.0));
        weightSum+=w;
    }
    return vec4(incident*fade,clamp(weightSum*fade,0.0,1.0));
}
vec3 dynamicGiCascadedAt(vec3 p,vec3 n) {
    vec4 fine=giCascadeLayer(p,n,u_dynamicGiOrigin,u_dynamicGiSize,
                              u_dynamicGiRing,tex13,true);
    vec4 coarse=giCascadeLayer(p,n,u_dynamicGiFarOrigin,u_dynamicGiFarSize,
                                u_dynamicGiFarRing,tex15,false);
    return fine.rgb+(1.0-fine.a)*coarse.rgb;
}
#endif
)GI"

#define AIVERSE_MSL_CASCADE_GI R"GI(
#if defined(DYNAMIC_GI_CASCADES) && defined(DYNAMIC_GI_VALIDITY)
static float4 giCascadeLayer(texture2d<float> atlas,texture2d<float> mask,
                             bool masked,constant GiDynamicU& u,
                             float3 p,float3 n) {
    float3 logical=(p-u.origin.xyz)*u.origin.w;
    float3 extent=u.size.xyz;
    float3 distanceToEdge=min(logical,extent-logical);
    float border=min(min(distanceToEdge.x,distanceToEdge.y),distanceToEdge.z);
    if(border<=0.0f)return float4(0.0f);
    float t=clamp(border/1.5f,0.0f,1.0f);
    float fade=t*t*(3.0f-2.0f*t);
    float3 coord=clamp(logical-float3(.5),float3(0.0),extent-1.0f);
    int3 base=int3(floor(coord));
    float3 f=fract(coord);
    int first=f.x>=f.y?(f.x>=f.z?0:2):(f.y>=f.z?1:2);
    int last=f.x<=f.y?(f.x<=f.z?0:2):(f.y<=f.z?1:2);
    if(first==last)last=(first+2)%3;
    int middle=3-first-last;
    float4 normal=float4(1.0f,n);
    float3 incident=float3(0.0f);
    float weightSum=0.0f;
    for(int i=0;i<4;++i) {
        int3 corner=int3(0);
        if(i>=1)corner[first]=1;
        if(i>=2)corner[middle]=1;
        if(i>=3)corner[last]=1;
        float w=i==0?1.0f-f[first]:i==1?f[first]-f[middle]:i==2?f[middle]-f[last]:f[last];
        if(w<=0.0f)continue;
        int3 cell=clamp(base+corner,int3(0),int3(extent)-1);
        int3 physical=(cell+int3(u.ring.xyz))%int3(extent);
        int probe=physical.x+int(extent.x)*(physical.y+int(extent.y)*physical.z);
        if(masked && mask.read(uint2(uint(probe%128),uint(probe/128))).r<0.5f)continue;
        int offset=probe*3;
        float3 rgb=float3(dot(atlas.read(uint2(uint(offset%384),uint(offset/384))),normal),
                          dot(atlas.read(uint2(uint((offset+1)%384),uint((offset+1)/384))),normal),
                          dot(atlas.read(uint2(uint((offset+2)%384),uint((offset+2)/384))),normal));
        incident+=w*max(rgb,float3(0.0));
        weightSum+=w;
    }
    return float4(incident*fade,clamp(weightSum*fade,0.0f,1.0f));
}
static float3 dynamicGiCascadedAt(texture2d<float> nearAtlas,
                                  texture2d<float> nearMask,
                                  texture2d<float> farAtlas,
                                  constant GiDynamicU& nearGi,
                                  constant GiDynamicU& farGi,
                                  float3 p,float3 n) {
    float4 fine=giCascadeLayer(nearAtlas,nearMask,true,nearGi,p,n);
    float4 coarse=giCascadeLayer(farAtlas,nearMask,false,farGi,p,n);
    return fine.rgb+(1.0f-fine.w)*coarse.rgb;
}
#if defined(DYNAMIC_GI_TRIPLE)
// Metal-only permutation: tex16 has a texture resource, no extra sampler.
// Middle and distant atlases MUST be fully fence-confirmed by the caller;
// the fine R8 mask is checked per physical probe as before.
static float3 dynamicGiTripleAt(texture2d<float> fineAtlas,
                                texture2d<float> fineMask,
                                texture2d<float> mediumAtlas,
                                texture2d<float> distantAtlas,
                                constant GiDynamicU& fineGi,
                                constant GiDynamicU& mediumGi,
                                constant GiDynamicU& distantGi,
                                float3 p,float3 n) {
    float4 fine=giCascadeLayer(fineAtlas,fineMask,true,fineGi,p,n);
    float4 middle=giCascadeLayer(mediumAtlas,fineMask,false,mediumGi,p,n);
    float4 distant=giCascadeLayer(distantAtlas,fineMask,false,distantGi,p,n);
    float remaining=1.0f-fine.w;
    return fine.rgb+remaining*middle.rgb+
           remaining*(1.0f-middle.w)*distant.rgb;
}
#endif
#endif
)GI"
