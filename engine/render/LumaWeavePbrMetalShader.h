// Opt-in PBR-reflected direct lighting for LumaWeave's High/Ultra Metal path.
// Input material: linear baseColor.rgb, metallic.a.
// Input surface: signed world normal.rgb, perceptual roughness.a.
// Source is the same opaque ModelDraw material path (or host custom receiver).
// Output RGB is ALREADY reflected direct light (diffuse + GGX specular).
// Never multiply this output by material albedo in HDR composition again.
#pragma once
#include "GiGpuBvhMetalShader.h"
#include "LumaWeaveMetalCommon.h"
#include "LumaWeaveMetalScreenCommon.h"

#define AIVERSE_MSL_LUMAWEAVE_SCREEN_PBR \
    "\n" \
    "#include <metal_stdlib>\n" \
    "using namespace metal;\n" \
    AIVERSE_MSL_GI_BVH_RAYQUERY \
    AIVERSE_MSL_LUMAWEAVE_COMMON \
    "\n" \
    AIVERSE_MSL_LUMAWEAVE_SCREEN_COMMON \
    "\n" \
    "static float3 lumaPbrReflected(LumaLight l,float3 p,float3 n,float3 v,\n" \
    "                               float3 base,float metallic,float rough) {\n" \
    "    float3 cosineRadiance=directUnshadowed(l,p,n);\n" \
    "    if(all(cosineRadiance<=float3(0.0f)))return float3(0);\n" \
    "    float3 toLight=l.positionRadius.xyz-p;\n" \
    "    const float dist2=dot(toLight,toLight);\n" \
    "    if(dist2<=.0001f)return float3(0);\n" \
    "    float3 L=toLight*rsqrt(dist2);\n" \
    "    const float ndl=max(dot(n,L),0.0f);\n" \
    "    const float ndv=max(dot(n,v),0.0f);\n" \
    "    if(ndl<=1e-5f || ndv<=1e-5f)return float3(0);\n" \
    "    float3 halfVector=v+L;\n" \
    "    float h2=dot(halfVector,halfVector);\n" \
    "    if(h2<=1e-8f)return float3(0);\n" \
    "    float3 H=halfVector*rsqrt(h2);\n" \
    "    const float ndh=max(dot(n,H),0.0f);\n" \
    "    const float vdh=max(dot(v,H),0.0f);\n" \
    "\n" \
    "    const float alpha=max(.045f,rough);\n" \
    "    const float a=alpha*alpha;\n" \
    "    const float a2=a*a;\n" \
    "    const float d=ndh*ndh*(a2-1.0f)+1.0f;\n" \
    "    const float D=a2/(3.141592653589793f*d*d);\n" \
    "    const float k=(alpha+1.0f)*(alpha+1.0f)*.125f;\n" \
    "    const float G=(ndl/(ndl*(1.0f-k)+k))*\n" \
    "                  (ndv/(ndv*(1.0f-k)+k));\n" \
    "    const float3 F0=mix(float3(.04f),base,metallic);\n" \
    "    const float3 F=F0+(1.0f-F0)*pow(1.0f-vdh,5.0f);\n" \
    "    const float3 diffuse=base*(1.0f-metallic)*(1.0f-F)*\n" \
    "                         (1.0f/3.141592653589793f);\n" \
    "    const float3 specular=(D*G*F)/max(4.0f*ndl*ndv,1e-5f);\n" \
    "    return cosineRadiance*(diffuse+specular);\n" \
    "}\n" \
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
    "    depth2d<float> sceneDepth [[texture(1)]],\n" \
    "    texture2d<float> surfaceMaterial [[texture(2)]],\n" \
    "    texture2d<float> surfaceNormal [[texture(3)]],\n" \
    "    depth2d<float> receiverDepth [[texture(4)]]) {\n" \
    "    if(i>=p.limits.z)return;\n" \
    "    uint2 pixel=uint2(i%p.control.z,i/p.control.z);\n" \
    "    const float2 uv=(float2(pixel)+.5f)/p.resolution.xy;\n" \
    "    const bool flipped=p.eye.w>.5f;\n" \
    "    const float sceneZ=depthAt(sceneDepth,uv,flipped);\n" \
    "    if(sceneZ>=.99999f || sceneZ<=0.0f) {\n" \
    "        output.write(float4(0),pixel);return;\n" \
    "    }\n" \
    "    const float receiverZ=depthAt(receiverDepth,uv,flipped);\n" \
    "    if(receiverZ>=.99999f || abs(receiverZ-sceneZ)>.008f) {\n" \
    "        // Unknown custom surface or occluded ModelDraw: no borrowed PBR\n" \
    "        // material may be projected onto a different visible receiver.\n" \
    "        output.write(float4(0,0,0,sceneZ),pixel);return;\n" \
    "    }\n" \
    "    const float2 readUv=flipped?float2(uv.x,1.0f-uv.y):uv;\n" \
    "    const float4 material=surfaceMaterial.sample(lumaDepthSampler,readUv);\n" \
    "    const float4 surface=surfaceNormal.sample(lumaDepthSampler,readUv);\n" \
    "    const float n2=dot(surface.xyz,surface.xyz);\n" \
    "    if(n2<.25f || !all(isfinite(surface))) {\n" \
    "        output.write(float4(0,0,0,sceneZ),pixel);return;\n" \
    "    }\n" \
    "    const float3 N=surface.xyz*rsqrt(n2);\n" \
    "    const float3 base=clamp(material.rgb,float3(0),float3(1));\n" \
    "    const float metallic=clamp(material.a,0.0f,1.0f);\n" \
    "    const float rough=clamp(surface.w,.045f,1.0f);\n" \
    "    const float3 world=lumaWorld(uv,sceneZ,p);\n" \
    "    float3 V=p.eye.xyz-world;\n" \
    "    const float v2=dot(V,V);\n" \
    "    if(v2<1e-8f) {\n" \
    "        output.write(float4(0,0,0,sceneZ),pixel);return;\n" \
    "    }\n" \
    "    V*=rsqrt(v2);\n" \
    "\n" \
    "    const float3 grid=(world-p.origin.xyz)/p.cellSize.xyz;\n" \
    "    const int3 cell=int3(floor(grid));\n" \
    "    bool full=any(cell<int3(0)) || any(cell>=int3(p.dims.xyz));\n" \
    "    LumaCell entry{0,0,0,0};\n" \
    "    if(!full) {\n" \
    "        uint id=uint(cell.x)+p.dims.x*(uint(cell.y)+p.dims.y*uint(cell.z));\n" \
    "        entry=cells[id];\n" \
    "        full=(entry.flags&1u)!=0u;\n" \
    "    }\n" \
    "    const uint count=full?p.limits.x:entry.count;\n" \
    "    float totalWeight=0.0f;\n" \
    "    for(uint j=0;j<count;++j) {\n" \
    "        uint id=full?j:candidates[entry.first+j];\n" \
    "        if(id>=p.limits.x)continue;\n" \
    "        const LumaLight l=lights[id];\n" \
    "        totalWeight+=importance(lumaPbrReflected(l,world,N,V,base,metallic,rough),\n" \
    "                                l.settings.y);\n" \
    "    }\n" \
    "    if(!(totalWeight>0.0f) || !isfinite(totalWeight)) {\n" \
    "        output.write(float4(0,0,0,sceneZ),pixel);return;\n" \
    "    }\n" \
    "\n" \
    "    uint rng=(i+1u)*747796405u ^ (p.control.y+1u)*2891336453u;\n" \
    "    if(!rng)rng=1u;\n" \
    "    float3 direct=float3(0);\n" \
    "    for(uint sample=0;sample<p.limits.w;++sample) {\n" \
    "        const float threshold=float(nextRandom(rng))*\n" \
    "                              (1.0f/4294967296.0f)*totalWeight;\n" \
    "        float cumulative=0.0f;\n" \
    "        float selectedWeight=0.0f;\n" \
    "        float3 contribution=float3(0);\n" \
    "        LumaLight selected=lights[0];\n" \
    "        for(uint j=0;j<count;++j) {\n" \
    "            const uint id=full?j:candidates[entry.first+j];\n" \
    "            if(id>=p.limits.x)continue;\n" \
    "            const LumaLight light=lights[id];\n" \
    "            const float3 rgb=lumaPbrReflected(light,world,N,V,base,metallic,rough);\n" \
    "            const float weight=importance(rgb,light.settings.y);\n" \
    "            if(weight<=0.0f)continue;\n" \
    "            cumulative+=weight;\n" \
    "            if(threshold<cumulative || j==count-1u) {\n" \
    "                contribution=rgb;\n" \
    "                selectedWeight=weight;\n" \
    "                selected=light;\n" \
    "                break;\n" \
    "            }\n" \
    "        }\n" \
    "        if(selectedWeight<=0.0f)continue;\n" \
    "        bool occluded=false;\n" \
    "        if(selected.settings.z>.5f && p.control.x!=0u) {\n" \
    "            const float3 origin=world+N*.02f;\n" \
    "            const float3 ray=selected.positionRadius.xyz-origin;\n" \
    "            const float distance=length(ray);\n" \
    "            if(distance>.05f) {\n" \
    "                const float limit=distance-.03f;\n" \
    "                occluded=traceBvh(origin,ray/distance,limit,triangles,\n" \
    "                                  nodes,p.control.x).distance<limit;\n" \
    "            }\n" \
    "        }\n" \
    "        if(!occluded)direct+=contribution*(totalWeight/selectedWeight);\n" \
    "    }\n" \
    "    direct/=max(float(p.limits.w),1.0f);\n" \
    "    direct=clamp(direct,float3(0),float3(65504));\n" \
    "    output.write(float4(direct,sceneZ),pixel);\n" \
    "}\n"
