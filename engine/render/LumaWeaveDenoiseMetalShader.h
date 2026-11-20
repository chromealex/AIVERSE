// Metal-only two-stage stochastic direct-light denoiser:
// spatial edge-aware 3x3 + temporal reprojected history with normal/depth
// disocclusion checks and bounded history clamping.
// Input RGB is DIRECT incident light, alpha is native device depth.
// Renderer/material albedo multiplication is separate and NEVER inferred here.
#pragma once
#include "LumaWeaveMetalScreenCommon.h"
#define AIVERSE_MSL_LUMAWEAVE_DENOISE R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" AIVERSE_MSL_LUMAWEAVE_SCREEN_COMMON R"MSL(
struct LumaDenoiseParams {
    LumaScreenParams current;
    float4x4 prevVP;
    float4 history;   // x=previousValid, y=historyBlend[0..0.95], z=normalDot threshold, w=depth delta threshold
    float4 spatial;   // x=neighbor device depth delta, y=temporal RGB clamp delta, z=far-camera cut, w=reserved
};
kernel void cs_main(
    uint i [[thread_position_in_grid]],
    constant LumaDenoiseParams& p [[buffer(8)]],
    texture2d<float,access::write> filtered [[texture(0)]],
    texture2d<float,access::write> normals [[texture(1)]],
    texture2d<float,access::read> direct [[texture(2)]],
    texture2d<float,access::read> oldFiltered [[texture(3)]],
    texture2d<float,access::read> oldNormals [[texture(4)]],
    depth2d<float> sceneDepth [[texture(5)]]
#if defined(LUMAWEAVE_PBR_NORMALS)
    , texture2d<float,access::read> surfaceNormal [[texture(6)]]
#endif
) {
    if(i>=p.current.limits.z)return;
    uint w=uint(p.current.resolution.x),h=uint(p.current.resolution.y);
    uint2 pixel=uint2(i%w,i/w);
    float4 center=direct.read(pixel);
    float d=center.a;
    if(d>=.99999f || d<=0.0f) {
        filtered.write(float4(0),pixel);
        normals.write(float4(0),pixel);
        return;
    }
    const float2 uv=(float2(pixel)+.5f)/p.current.resolution.xy;
    float3 world=lumaWorld(uv,d,p.current);
    float3 normal=lumaNormal(uv,d,sceneDepth,p.current);
#if defined(LUMAWEAVE_PBR_NORMALS)
    // Respect the exact material world-space normal (including normal maps)
    // when evaluating temporal history for narrow moving specular highlights.
    float2 surfaceUv=p.current.eye.w>.5f?
        float2(uv.x,1.0f-uv.y):uv;
    uint2 surfaceCoord=uint2(clamp(surfaceUv*float2(surfaceNormal.get_width(),
        surfaceNormal.get_height()),float2(0),
        float2(surfaceNormal.get_width()-1,surfaceNormal.get_height()-1)));
    float3 received=surfaceNormal.read(surfaceCoord).xyz;
    if(dot(received,received)>.25f)normal=normalize(received);
#endif
    const float sigma=max(p.spatial.x,1e-6f);
    float3 color=max(center.rgb,float3(0));
    float sum=1.0f;
    // Edge-aware spatial denoise of independent direct-light samples.
    for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx) {
        if(xx==0&&yy==0)continue;
        int2 candidate=int2(pixel)+int2(xx,yy);
        if(any(candidate<int2(0)) || candidate.x>=int(w) ||
           candidate.y>=int(h))continue;
        const float4 tap=direct.read(uint2(candidate));
        if(tap.a<=0.0f || tap.a>=.99999f)continue;
        float depthDelta=abs(d-tap.a);
        if(depthDelta>sigma*4.0f)continue;
        float weight=exp(-depthDelta*depthDelta/(2.0f*sigma*sigma))*
                     (xx==0||yy==0?.8f:.55f);
        color+=max(tap.rgb,float3(0))*weight;
        sum+=weight;
    }
    color/=sum;
    // Reprojection into PREVIOUS displayed frame's low-res texture.
    if(p.history.x>.5f) {
        float4 clip=p.prevVP*float4(world,1.0f);
        if(clip.w>1e-5f) {
            float3 ndc=clip.xyz/clip.w;
            float2 oldUv=ndc.xy*.5f+.5f;
            float2 readUv=p.current.eye.w>.5f ?
                         float2(oldUv.x,1.0f-oldUv.y):oldUv;
            if(all(readUv>=float2(0)) && all(readUv<float2(1)) &&
               ndc.z>=0.0f && ndc.z<=1.0f) {
                uint2 oldPixel=uint2(readUv*p.current.resolution.xy);
                float4 old=oldFiltered.read(oldPixel);
                float4 oldNormal=oldNormals.read(oldPixel);
                float delta=abs(oldNormal.w-ndc.z);
                float nd=dot(normal,oldNormal.xyz);
                if(old.w>0.0f && old.w<.99999f &&
                   delta<=p.history.w && nd>=p.history.z) {
                    float3 clampWidth=float3(p.spatial.y)*(1.0f+color);
                    float3 historic=clamp(old.rgb,max(color-clampWidth,0.0f),
                                          color+clampWidth);
                    color=mix(color,historic,clamp(p.history.y,0.0f,.95f));
                }
            }
        }
    }
    filtered.write(float4(max(color,float3(0)),d),pixel);
    normals.write(float4(normal,d),pixel);
}
)MSL"


// Identical spatial/temporal algorithm but historical surface normals are
// read from the current explicit PBR receiver instead of geometric depth.
#define AIVERSE_MSL_LUMAWEAVE_DENOISE_PBR \
    R"MSL(
#define LUMAWEAVE_PBR_NORMALS 1
)MSL" AIVERSE_MSL_LUMAWEAVE_DENOISE
