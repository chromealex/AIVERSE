// Metal-only two-stage stochastic direct-light denoiser:
// spatial edge-aware 3x3 + temporal reprojected history with normal/depth
// disocclusion checks and bounded history clamping.
// Input RGB is DIRECT incident light, alpha is native device depth.
// Renderer/material albedo multiplication is separate and NEVER inferred here.
#pragma once
#include "LumaWeaveMetalScreenCommon.h"
#define AIVERSE_MSL_LUMAWEAVE_DENOISE \
    "\n" \
    "#include <metal_stdlib>\n" \
    "using namespace metal;\n" \
    AIVERSE_MSL_LUMAWEAVE_SCREEN_COMMON \
    "\n" \
    "struct LumaDenoiseParams {\n" \
    "    LumaScreenParams current;\n" \
    "    float4x4 prevVP;\n" \
    "    float4 history;   // x=previousValid, y=historyBlend[0..0.95], z=normalDot threshold, w=depth delta threshold\n" \
    "    float4 spatial;   // x=neighbor device depth delta, y=temporal RGB clamp delta, z=far-camera cut, w=reserved\n" \
    "};\n" \
    "kernel void cs_main(\n" \
    "    uint i [[thread_position_in_grid]],\n" \
    "    constant LumaDenoiseParams& p [[buffer(8)]],\n" \
    "    texture2d<float,access::write> filtered [[texture(0)]],\n" \
    "    texture2d<float,access::write> normals [[texture(1)]],\n" \
    "    texture2d<float,access::read> direct [[texture(2)]],\n" \
    "    texture2d<float,access::read> oldFiltered [[texture(3)]],\n" \
    "    texture2d<float,access::read> oldNormals [[texture(4)]],\n" \
    "    depth2d<float> sceneDepth [[texture(5)]]\n" \
    "#if defined(LUMAWEAVE_PBR_NORMALS)\n" \
    "    , texture2d<float,access::read> surfaceNormal [[texture(6)]]\n" \
    "#endif\n" \
    ") {\n" \
    "    if(i>=p.current.limits.z)return;\n" \
    "    uint w=uint(p.current.resolution.x),h=uint(p.current.resolution.y);\n" \
    "    uint2 pixel=uint2(i%w,i/w);\n" \
    "    float4 center=direct.read(pixel);\n" \
    "    float d=center.a;\n" \
    "    if(d>=.99999f || d<=0.0f) {\n" \
    "        filtered.write(float4(0),pixel);\n" \
    "        normals.write(float4(0),pixel);\n" \
    "        return;\n" \
    "    }\n" \
    "    const float2 uv=(float2(pixel)+.5f)/p.current.resolution.xy;\n" \
    "    float3 world=lumaWorld(uv,d,p.current);\n" \
    "    float3 normal=lumaNormal(uv,d,sceneDepth,p.current);\n" \
    "#if defined(LUMAWEAVE_PBR_NORMALS)\n" \
    "    // Respect the exact material world-space normal (including normal maps)\n" \
    "    // when evaluating temporal history for narrow moving specular highlights.\n" \
    "    float2 surfaceUv=p.current.eye.w>.5f?\n" \
    "        float2(uv.x,1.0f-uv.y):uv;\n" \
    "    uint2 surfaceCoord=uint2(clamp(surfaceUv*float2(surfaceNormal.get_width(),\n" \
    "        surfaceNormal.get_height()),float2(0),\n" \
    "        float2(surfaceNormal.get_width()-1,surfaceNormal.get_height()-1)));\n" \
    "    float3 received=surfaceNormal.read(surfaceCoord).xyz;\n" \
    "    if(dot(received,received)>.25f)normal=normalize(received);\n" \
    "#endif\n" \
    "    const float sigma=max(p.spatial.x,1e-6f);\n" \
    "    float3 color=max(center.rgb,float3(0));\n" \
    "    float sum=1.0f;\n" \
    "    // Edge-aware spatial denoise of independent direct-light samples.\n" \
    "    for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx) {\n" \
    "        if(xx==0&&yy==0)continue;\n" \
    "        int2 candidate=int2(pixel)+int2(xx,yy);\n" \
    "        if(any(candidate<int2(0)) || candidate.x>=int(w) ||\n" \
    "           candidate.y>=int(h))continue;\n" \
    "        const float4 tap=direct.read(uint2(candidate));\n" \
    "        if(tap.a<=0.0f || tap.a>=.99999f)continue;\n" \
    "        float depthDelta=abs(d-tap.a);\n" \
    "        if(depthDelta>sigma*4.0f)continue;\n" \
    "        float weight=exp(-depthDelta*depthDelta/(2.0f*sigma*sigma))*\n" \
    "                     (xx==0||yy==0?.8f:.55f);\n" \
    "        color+=max(tap.rgb,float3(0))*weight;\n" \
    "        sum+=weight;\n" \
    "    }\n" \
    "    color/=sum;\n" \
    "    // Reprojection into PREVIOUS displayed frame's low-res texture.\n" \
    "    if(p.history.x>.5f) {\n" \
    "        float4 clip=p.prevVP*float4(world,1.0f);\n" \
    "        if(clip.w>1e-5f) {\n" \
    "            float3 ndc=clip.xyz/clip.w;\n" \
    "            float2 oldUv=ndc.xy*.5f+.5f;\n" \
    "            float2 readUv=p.current.eye.w>.5f ?\n" \
    "                         float2(oldUv.x,1.0f-oldUv.y):oldUv;\n" \
    "            if(all(readUv>=float2(0)) && all(readUv<float2(1)) &&\n" \
    "               ndc.z>=0.0f && ndc.z<=1.0f) {\n" \
    "                uint2 oldPixel=uint2(readUv*p.current.resolution.xy);\n" \
    "                float4 old=oldFiltered.read(oldPixel);\n" \
    "                float4 oldNormal=oldNormals.read(oldPixel);\n" \
    "                float delta=abs(oldNormal.w-ndc.z);\n" \
    "                float nd=dot(normal,oldNormal.xyz);\n" \
    "                if(old.w>0.0f && old.w<.99999f &&\n" \
    "                   delta<=p.history.w && nd>=p.history.z) {\n" \
    "                    float3 clampWidth=float3(p.spatial.y)*(1.0f+color);\n" \
    "                    float3 historic=clamp(old.rgb,max(color-clampWidth,0.0f),\n" \
    "                                          color+clampWidth);\n" \
    "                    color=mix(color,historic,clamp(p.history.y,0.0f,.95f));\n" \
    "                }\n" \
    "            }\n" \
    "        }\n" \
    "    }\n" \
    "    filtered.write(float4(max(color,float3(0)),d),pixel);\n" \
    "    normals.write(float4(normal,d),pixel);\n" \
    "}\n"


// Identical spatial/temporal algorithm but historical surface normals are
// read from the current explicit PBR receiver instead of geometric depth.
#define AIVERSE_MSL_LUMAWEAVE_DENOISE_PBR \
    "\n" \
    "#define LUMAWEAVE_PBR_NORMALS 1\n" \
    AIVERSE_MSL_LUMAWEAVE_DENOISE
