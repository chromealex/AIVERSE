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

#define AIVERSE_MSL_LUMAWEAVE_SCREEN_PBR R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" AIVERSE_MSL_GI_BVH_RAYQUERY AIVERSE_MSL_LUMAWEAVE_COMMON R"MSL(
)MSL" AIVERSE_MSL_LUMAWEAVE_SCREEN_COMMON R"MSL(
static float3 lumaPbrReflected(LumaLight l,float3 p,float3 n,float3 v,
                               float3 base,float metallic,float rough) {
    float3 cosineRadiance=directUnshadowed(l,p,n);
    if(all(cosineRadiance<=float3(0.0f)))return float3(0);
    float3 toLight=l.positionRadius.xyz-p;
    const float dist2=dot(toLight,toLight);
    if(dist2<=.0001f)return float3(0);
    float3 L=toLight*rsqrt(dist2);
    const float ndl=max(dot(n,L),0.0f);
    const float ndv=max(dot(n,v),0.0f);
    if(ndl<=1e-5f || ndv<=1e-5f)return float3(0);
    float3 halfVector=v+L;
    float h2=dot(halfVector,halfVector);
    if(h2<=1e-8f)return float3(0);
    float3 H=halfVector*rsqrt(h2);
    const float ndh=max(dot(n,H),0.0f);
    const float vdh=max(dot(v,H),0.0f);

    const float alpha=max(.045f,rough);
    const float a=alpha*alpha;
    const float a2=a*a;
    const float d=ndh*ndh*(a2-1.0f)+1.0f;
    const float D=a2/(3.141592653589793f*d*d);
    const float k=(alpha+1.0f)*(alpha+1.0f)*.125f;
    const float G=(ndl/(ndl*(1.0f-k)+k))*
                  (ndv/(ndv*(1.0f-k)+k));
    const float3 F0=mix(float3(.04f),base,metallic);
    const float3 F=F0+(1.0f-F0)*pow(1.0f-vdh,5.0f);
    const float3 diffuse=base*(1.0f-metallic)*(1.0f-F)*
                         (1.0f/3.141592653589793f);
    const float3 specular=(D*G*F)/max(4.0f*ndl*ndv,1e-5f);
    return cosineRadiance*(diffuse+specular);
}

kernel void cs_main(
    uint i [[thread_position_in_grid]],
    device const LumaLight* lights [[buffer(0)]],
    device const LumaCell* cells [[buffer(1)]],
    device const uint* candidates [[buffer(2)]],
    device const Tri* triangles [[buffer(3)]],
    device const Node* nodes [[buffer(4)]],
    constant LumaScreenParams& p [[buffer(8)]],
    texture2d<float,access::write> output [[texture(0)]],
    depth2d<float> sceneDepth [[texture(1)]],
    texture2d<float> surfaceMaterial [[texture(2)]],
    texture2d<float> surfaceNormal [[texture(3)]],
    depth2d<float> receiverDepth [[texture(4)]]) {
    if(i>=p.limits.z)return;
    uint2 pixel=uint2(i%p.control.z,i/p.control.z);
    const float2 uv=(float2(pixel)+.5f)/p.resolution.xy;
    const bool flipped=p.eye.w>.5f;
    const float sceneZ=depthAt(sceneDepth,uv,flipped);
    if(sceneZ>=.99999f || sceneZ<=0.0f) {
        output.write(float4(0),pixel);return;
    }
    const float receiverZ=depthAt(receiverDepth,uv,flipped);
    if(receiverZ>=.99999f || abs(receiverZ-sceneZ)>.008f) {
        // Unknown custom surface or occluded ModelDraw: no borrowed PBR
        // material may be projected onto a different visible receiver.
        output.write(float4(0,0,0,sceneZ),pixel);return;
    }
    const float2 readUv=flipped?float2(uv.x,1.0f-uv.y):uv;
    const float4 material=surfaceMaterial.sample(lumaDepthSampler,readUv);
    const float4 surface=surfaceNormal.sample(lumaDepthSampler,readUv);
    const float n2=dot(surface.xyz,surface.xyz);
    if(n2<.25f || !all(isfinite(surface))) {
        output.write(float4(0,0,0,sceneZ),pixel);return;
    }
    const float3 N=surface.xyz*rsqrt(n2);
    const float3 base=clamp(material.rgb,float3(0),float3(1));
    const float metallic=clamp(material.a,0.0f,1.0f);
    const float rough=clamp(surface.w,.045f,1.0f);
    const float3 world=lumaWorld(uv,sceneZ,p);
    float3 V=p.eye.xyz-world;
    const float v2=dot(V,V);
    if(v2<1e-8f) {
        output.write(float4(0,0,0,sceneZ),pixel);return;
    }
    V*=rsqrt(v2);

    const float3 grid=(world-p.origin.xyz)/p.cellSize.xyz;
    const int3 cell=int3(floor(grid));
    bool full=any(cell<int3(0)) || any(cell>=int3(p.dims.xyz));
    LumaCell entry{0,0,0,0};
    if(!full) {
        uint id=uint(cell.x)+p.dims.x*(uint(cell.y)+p.dims.y*uint(cell.z));
        entry=cells[id];
        full=(entry.flags&1u)!=0u;
    }
    const uint count=full?p.limits.x:entry.count;
    float totalWeight=0.0f;
    for(uint j=0;j<count;++j) {
        uint id=full?j:candidates[entry.first+j];
        if(id>=p.limits.x)continue;
        const LumaLight l=lights[id];
        totalWeight+=importance(lumaPbrReflected(l,world,N,V,base,metallic,rough),
                                l.settings.y);
    }
    if(!(totalWeight>0.0f) || !isfinite(totalWeight)) {
        output.write(float4(0,0,0,sceneZ),pixel);return;
    }

    uint rng=(i+1u)*747796405u ^ (p.control.y+1u)*2891336453u;
    if(!rng)rng=1u;
    float3 direct=float3(0);
    for(uint sample=0;sample<p.limits.w;++sample) {
        const float threshold=float(nextRandom(rng))*
                              (1.0f/4294967296.0f)*totalWeight;
        float cumulative=0.0f;
        float selectedWeight=0.0f;
        float3 contribution=float3(0);
        LumaLight selected=lights[0];
        for(uint j=0;j<count;++j) {
            const uint id=full?j:candidates[entry.first+j];
            if(id>=p.limits.x)continue;
            const LumaLight light=lights[id];
            const float3 rgb=lumaPbrReflected(light,world,N,V,base,metallic,rough);
            const float weight=importance(rgb,light.settings.y);
            if(weight<=0.0f)continue;
            cumulative+=weight;
            if(threshold<cumulative || j==count-1u) {
                contribution=rgb;
                selectedWeight=weight;
                selected=light;
                break;
            }
        }
        if(selectedWeight<=0.0f)continue;
        bool occluded=false;
        if(selected.settings.z>.5f && p.control.x!=0u) {
            const float3 origin=world+N*.02f;
            const float3 ray=selected.positionRadius.xyz-origin;
            const float distance=length(ray);
            if(distance>.05f) {
                const float limit=distance-.03f;
                occluded=traceBvh(origin,ray/distance,limit,triangles,
                                  nodes,p.control.x).distance<limit;
            }
        }
        if(!occluded)direct+=contribution*(totalWeight/selectedWeight);
    }
    direct/=max(float(p.limits.w),1.0f);
    direct=clamp(direct,float3(0),float3(65504));
    output.write(float4(direct,sceneZ),pixel);
}
)MSL"
