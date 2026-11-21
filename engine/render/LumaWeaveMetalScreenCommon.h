// Shared world/normal reconstruction + param contract for LumaWeave fullscreen
// direct resolve and its separate temporal/spatial denoiser.
#pragma once
#define AIVERSE_MSL_LUMAWEAVE_SCREEN_COMMON \
    "\n" \
    "struct LumaScreenParams {\n" \
    "    float4x4 invVP;\n" \
    "    float4 eye;                 // xyz world, w=texture-Y inverted relative to NDC\n" \
    "    float4 resolution;          // output W/H, scene W/H\n" \
    "    float4 origin,cellSize;\n" \
    "    uint4 dims;                 // nx,ny,nz,count\n" \
    "    uint4 limits;               // lightCount,csr refs,receiverCount,sampleCount\n" \
    "    uint4 control;              // BVH nodes, temporal seed, output width, reserved\n" \
    "};\n" \
    "constexpr sampler lumaDepthSampler(coord::normalized,filter::nearest,\n" \
    "                                    address::clamp_to_edge);\n" \
    "static float depthAt(depth2d<float> depth,float2 projectedUv,bool flipY) {\n" \
    "    float2 readUv=flipY?float2(projectedUv.x,1.0f-projectedUv.y):projectedUv;\n" \
    "    return depth.sample(lumaDepthSampler,clamp(readUv,float2(0),float2(1)));\n" \
    "}\n" \
    "static float3 lumaWorld(float2 uv,float depth,constant LumaScreenParams& p) {\n" \
    "    float4 h=p.invVP*float4(uv*2.0f-1.0f,depth,1.0f);\n" \
    "    return h.xyz/max(abs(h.w),1e-8f)*(h.w<0?-1.0f:1.0f);\n" \
    "}\n" \
    "static float3 lumaNormal(float2 uv,float center,depth2d<float> depth,\n" \
    "                         constant LumaScreenParams& p) {\n" \
    "    float2 dx=float2(1.0f/max(p.resolution.z,1.0f),0.0f);\n" \
    "    float2 dy=float2(0.0f,1.0f/max(p.resolution.w,1.0f));\n" \
    "    float3 origin=lumaWorld(uv,center,p);\n" \
    "    float3 tangent=lumaWorld(clamp(uv+dx,0.0f,1.0f),\n" \
    "              depthAt(depth,uv+dx,p.eye.w>0.5f),p)-origin;\n" \
    "    float3 bitangent=lumaWorld(clamp(uv+dy,0.0f,1.0f),\n" \
    "              depthAt(depth,uv+dy,p.eye.w>0.5f),p)-origin;\n" \
    "    float3 n=cross(tangent,bitangent);\n" \
    "    float length2=dot(n,n);\n" \
    "    if(length2<1e-10f)return float3(0,1,0);\n" \
    "    n*=rsqrt(length2);\n" \
    "    if(dot(n,p.eye.xyz-origin)<0.0f)n=-n;\n" \
    "    return n;\n" \
    "}\n"
