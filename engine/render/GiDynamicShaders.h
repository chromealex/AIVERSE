// Optional dynamic GI L1 probe overlay. Only indirect radiance; no direct or sky term.
// The 384-texel RGBA32F/RGBA16F rows match GiDynamicProbeTexture (128 probes per row, 3 texels/probe).
// Four-probe tetrahedral interpolation, edge fade, zero outside the volume.
// Shader variants opt in with DYNAMIC_GI; this header adds no default shader cost.
#pragma once

#define AIVERSE_GLSL_DYNAMIC_GI R"GI(
#if defined(DYNAMIC_GI)
layout(std140) uniform U6 { vec4 u_dynamicGiOrigin; vec4 u_dynamicGiSize; vec4 u_dynamicGiRing; };
uniform highp sampler2D tex13;
#if defined(DYNAMIC_GI_VALIDITY)
uniform highp sampler2D tex14;
#endif
vec4 dynamicGiTexel(int index) {
    return texelFetch(tex13, ivec2(index % 384, index / 384), 0);
}
vec3 dynamicGiAt(vec3 p, vec3 n) {
    vec3 coord=(p-u_dynamicGiOrigin.xyz)*u_dynamicGiOrigin.w-vec3(0.5);
    vec3 size=u_dynamicGiSize.xyz;
    if(any(lessThan(coord,vec3(-0.5))) || any(greaterThan(coord,size-vec3(0.5)))) return vec3(0.0);
    float edge=min(min(coord.x+0.5,size.x-0.5-coord.x),
                   min(min(coord.y+0.5,size.y-0.5-coord.y),min(coord.z+0.5,size.z-0.5-coord.z)));
    float fade=clamp(edge*2.0,0.0,1.0);
    vec3 fcoord=clamp(coord,vec3(0.0),size-1.0);
    ivec3 base=ivec3(floor(fcoord));
    vec3 f=fract(fcoord);
    int first=f.x>=f.y?(f.x>=f.z?0:2):(f.y>=f.z?1:2);
    int last=f.x<=f.y?(f.x<=f.z?0:2):(f.y<=f.z?1:2);
    if(first==last)last=(first+2)%3;
    int middle=3-first-last;
    vec4 normal=vec4(1.0,n);
    vec3 result=vec3(0.0);
    for(int i=0;i<4;++i){
        ivec3 corner=ivec3(0);
        if(i>=1)corner[first]=1;
        if(i>=2)corner[middle]=1;
        if(i>=3)corner[last]=1;
        float w=i==0?1.0-f[first]:i==1?f[first]-f[middle]:i==2?f[middle]-f[last]:f[last];
        if(w<=0.0)continue;
        ivec3 c=clamp(base+corner,ivec3(0),ivec3(size)-1);
        // Convert logical grid coordinates into stable toroidal atlas slots.
        ivec3 physical=(c+ivec3(u_dynamicGiRing.xyz))%ivec3(size);
        int probe=physical.x+int(size.x)*(physical.y+int(size.y)*physical.z), offset=probe*3;
#if defined(DYNAMIC_GI_VALIDITY)
        if(texelFetch(tex14,ivec2(probe%128,probe/128),0).r<0.5)continue;
#endif
        result+=w*max(vec3(dot(dynamicGiTexel(offset),normal),
                           dot(dynamicGiTexel(offset+1),normal),
                           dot(dynamicGiTexel(offset+2),normal)),vec3(0.0));
    }
    return result*fade;
}
#endif
)GI"

#define AIVERSE_MSL_DYNAMIC_GI R"GI(
#if defined(DYNAMIC_GI)
struct GiDynamicU { float4 origin; float4 size; float4 ring; };
static float4 dynamicGiTexel(texture2d<float> tex13,int index) {
    return tex13.read(uint2(uint(index%384),uint(index/384)));
}
static float3 dynamicGiAt(texture2d<float> tex13,constant GiDynamicU& u,
#if defined(DYNAMIC_GI_VALIDITY)
                           texture2d<float> tex14,
#endif
                           float3 p,float3 n) {
    float3 coord=(p-u.origin.xyz)*u.origin.w-float3(0.5);
    float3 size=u.size.xyz;
    if(any(coord<float3(-0.5)) || any(coord>size-float3(0.5)))return float3(0.0);
    float edge=min(min(coord.x+0.5,size.x-0.5-coord.x),
                   min(min(coord.y+0.5,size.y-0.5-coord.y),min(coord.z+0.5,size.z-0.5-coord.z)));
    float fade=clamp(edge*2.0,0.0,1.0);
    float3 fcoord=clamp(coord,float3(0.0),size-1.0);
    int3 base=int3(floor(fcoord));
    float3 f=fract(fcoord);
    int first=f.x>=f.y?(f.x>=f.z?0:2):(f.y>=f.z?1:2);
    int last=f.x<=f.y?(f.x<=f.z?0:2):(f.y<=f.z?1:2);
    if(first==last)last=(first+2)%3;
    int middle=3-first-last;
    float4 normal=float4(1.0,n);
    float3 result=float3(0.0);
    for(int i=0;i<4;++i){
        int3 corner=int3(0);
        if(i>=1)corner[first]=1;
        if(i>=2)corner[middle]=1;
        if(i>=3)corner[last]=1;
        float w=i==0?1.0-f[first]:i==1?f[first]-f[middle]:i==2?f[middle]-f[last]:f[last];
        if(w<=0.0)continue;
        int3 c=clamp(base+corner,int3(0),int3(size)-1);
        int3 physical=(c+int3(u.ring.xyz))%int3(size);
        int probe=physical.x+int(size.x)*(physical.y+int(size.y)*physical.z),offset=probe*3;
#if defined(DYNAMIC_GI_VALIDITY)
        if(tex14.read(uint2(uint(probe%128),uint(probe/128))).r<0.5f)continue;
#endif
        result+=w*max(float3(dot(dynamicGiTexel(tex13,offset),normal),
                             dot(dynamicGiTexel(tex13,offset+1),normal),
                             dot(dynamicGiTexel(tex13,offset+2),normal)),float3(0.0));
    }
    return result*fade;
}
#endif
)GI"
