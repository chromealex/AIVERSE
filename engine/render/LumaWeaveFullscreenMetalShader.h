// LumaWeave low-resolution screen-space direct-light compute.
// Requires a real HDR scene depth texture from a PostProcess::scenePass.
// Reconstructs geometric normals from depth; material normals/alpha require a
// future explicit G-buffer producer. Output RGB = linear DIRECT incident
// radiance, A = raw device depth for later edge-aware upsampling.
// This does not replace or mix into any legacy render target by itself.
#pragma once
#include "GiGpuBvhMetalShader.h"
#include "LumaWeaveMetalCommon.h"
#include "LumaWeaveMetalScreenCommon.h"

#define AIVERSE_MSL_LUMAWEAVE_FULLSCREEN \
    "\n" \
    "#include <metal_stdlib>\n" \
    "using namespace metal;\n" \
    AIVERSE_MSL_GI_BVH_RAYQUERY \
    AIVERSE_MSL_LUMAWEAVE_COMMON \
    "\n" \
    AIVERSE_MSL_LUMAWEAVE_SCREEN_COMMON \
    "\n" \
    "kernel void cs_main(\n" \
    "    uint i [[thread_position_in_grid]],\n" \
    "    device const LumaLight* lights [[buffer(0)]],\n" \
    "    device const LumaCell* cells [[buffer(1)]],\n" \
    "    device const uint* candidates [[buffer(2)]],\n" \
    "    device const Tri* triangles [[buffer(3)]],\n" \
    "    device const Node* nodes [[buffer(4)]],\n" \
    "    constant LumaScreenParams& p [[buffer(8)]],\n" \
    "    texture2d<float,access::write> output [[texture(0)]],\n" \
    "    depth2d<float> depth [[texture(1)]]) {\n" \
    "    if(i>=p.limits.z)return;\n" \
    "    uint2 pixel=uint2(i%p.control.z,i/p.control.z);\n" \
    "    float2 uv=(float2(pixel)+.5f)/p.resolution.xy;\n" \
    "    float d=depthAt(depth,uv,p.eye.w>.5f);\n" \
    "    if(d>=.99999f) {\n" \
    "        output.write(float4(0.0f),pixel);\n" \
    "        return;\n" \
    "    }\n" \
    "    if(p.limits.x==0u) {\n" \
    "        output.write(float4(0,0,0,d),pixel);\n" \
    "        return;\n" \
    "    }\n" \
    "    float3 world=lumaWorld(uv,d,p);\n" \
    "    float3 normal=lumaNormal(uv,d,depth,p);\n" \
    "\n" \
    "    float3 grid=(world-p.origin.xyz)/p.cellSize.xyz;\n" \
    "    int3 cell=int3(floor(grid));\n" \
    "    bool full=any(cell<int3(0))||any(cell>=int3(p.dims.xyz));\n" \
    "    LumaCell entry{0,0,0,0};\n" \
    "    if(!full) {\n" \
    "        uint id=uint(cell.x)+p.dims.x*(uint(cell.y)+p.dims.y*uint(cell.z));\n" \
    "        entry=cells[id];\n" \
    "        full=(entry.flags&1u)!=0u;\n" \
    "    }\n" \
    "    uint count=full?p.limits.x:entry.count;\n" \
    "    float totalWeight=0;\n" \
    "    for(uint j=0;j<count;++j) {\n" \
    "        uint id=full?j:candidates[entry.first+j];\n" \
    "        if(id>=p.limits.x)continue;\n" \
    "        LumaLight l=lights[id];\n" \
    "        totalWeight+=importance(directUnshadowed(l,world,normal),l.settings.y);\n" \
    "    }\n" \
    "    if(!(totalWeight>0.0f)||!isfinite(totalWeight)) {\n" \
    "        output.write(float4(0,0,0,d),pixel);\n" \
    "        return;\n" \
    "    }\n" \
    "    uint rng=(i+1u)*747796405u ^ (p.control.y+1u)*2891336453u;\n" \
    "    if(rng==0u)rng=1u;\n" \
    "    float3 direct=float3(0);\n" \
    "    for(uint sample=0;sample<p.limits.w;++sample) {\n" \
    "        const float target=float(nextRandom(rng))*(1.0f/4294967296.0f)*totalWeight;\n" \
    "        float weight=0, cumulative=0;\n" \
    "        float3 contribution=float3(0);\n" \
    "        LumaLight selected=lights[0];\n" \
    "        for(uint j=0;j<count;++j) {\n" \
    "            uint id=full?j:candidates[entry.first+j];\n" \
    "            if(id>=p.limits.x)continue;\n" \
    "            LumaLight l=lights[id];\n" \
    "            float3 c=directUnshadowed(l,world,normal);\n" \
    "            float w=importance(c,l.settings.y);\n" \
    "            if(w<=0)continue;\n" \
    "            cumulative+=w;\n" \
    "            if(target<cumulative || j==count-1u) {\n" \
    "                weight=w;selected=l;contribution=c;break;\n" \
    "            }\n" \
    "        }\n" \
    "        if(weight<=0)continue;\n" \
    "        bool blocked=false;\n" \
    "        if(selected.settings.z>.5f && p.control.x!=0u) {\n" \
    "            float3 start=world+normal*.02f;\n" \
    "            float3 ray=selected.positionRadius.xyz-start;\n" \
    "            float distance=length(ray);\n" \
    "            if(distance>.05f) {\n" \
    "                const float limit=distance-.03f;\n" \
    "                blocked=traceBvh(start,ray/distance,limit,triangles,nodes,\n" \
    "                                 p.control.x).distance<limit;\n" \
    "            }\n" \
    "        }\n" \
    "        if(!blocked)direct+=contribution*(totalWeight/weight);\n" \
    "    }\n" \
    "    direct/=max(1.0f,float(p.limits.w));\n" \
    "    output.write(float4(min(direct,float3(65504.0f)),d),pixel);\n" \
    "}\n"
