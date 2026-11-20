// BVH accelerated Metal-only first-bounce GI compute prototype.
// 1 GPU thread processes one dirty probe, traversing bounded GPU BVH nodes and
// writing directional RGB L1 into a 384-wide RGBA16F storage texture.
// No CPU readback and no dynamic GI enabled by default.
// This deliberately starts with a bounded flat triangle list; large scenes
// need an uploaded GPU BVH/SDF and shader-level visibility classification.
#pragma once
#include "GiGpuBvhMetalShader.h"
namespace aiverse::render::gi_gpu {
inline constexpr const char* kMetalSmallScene = R"MSL(
#include <metal_stdlib>
using namespace metal;
)MSL" AIVERSE_MSL_GI_BVH_RAYQUERY R"MSL(
struct Lamp {
    float4 posRadius;
    float4 colorIntensity;
};
struct Job {
    uint index;
    float x, y, z;
};
struct Params {
    uint triangles;
    uint lights;
    uint jobs;
    uint samples;
    float traceDistance;
    uint nodes;
    float pad1, pad2;
};
kernel void cs_main(uint thread [[thread_position_in_grid]],
                    device const Tri* tris [[buffer(0)]],
                    device const Lamp* lamps [[buffer(1)]],
                    device const Job* jobs [[buffer(2)]],
                    device const Node* nodes [[buffer(3)]],
                    constant Params& p [[buffer(8)]],
                    texture2d<float, access::write> output [[texture(0)]]) {
    if(thread>=p.jobs) return;
    Job job=jobs[thread];
    float3 position=float3(job.x,job.y,job.z);
    float3 coefficient=float3(0.0);
    float3 cx=float3(0.0),cy=float3(0.0),cz=float3(0.0);
    // Same deterministic golden-angle sphere directions as GiDynamicProbeReference.
    for(uint i=0;i<p.samples;++i) {
        float y=1.0f-2.0f*(float(i)+.5f)/float(p.samples);
        float phi=float(i)*2.39996323f;
        float r=sqrt(max(0.0f,1.0f-y*y));
        float3 ray=float3(r*cos(phi),y,r*sin(phi));
        Hit first=traceBvh(position,ray,p.traceDistance,tris,nodes,p.nodes);
        if(first.distance>=p.traceDistance || first.distance<=.05f ||
           dot(first.normal,first.normal)<.5f) continue;
        float3 n=first.normal;
        if(dot(n,-ray)<0.0f) n=-n;
        float3 surface=position+ray*first.distance+n*.03f;
        float3 outgoing=float3(0.0);
        for(uint l=0;l<p.lights;++l) {
            Lamp light=lamps[l];
            if(light.colorIntensity.w<=0.0f)continue;
            float3 toLight=light.posRadius.xyz-surface;
            float d2=dot(toLight,toLight);
            float radius=light.posRadius.w;
            if(d2<.0025f || d2>=radius*radius)continue;
            float d=sqrt(d2);
            float3 L=toLight/d;
            float ndl=max(0.0f,dot(n,L));
            if(ndl==0.0f)continue;
            if(traceBvh(surface,L,d,tris,nodes,p.nodes).distance<d-.03f)continue;
            float falloff=1.0f-d2/(radius*radius);
            float energy=light.colorIntensity.w*ndl*falloff*falloff/(1.0f+d2);
            outgoing+=light.colorIntensity.xyz*first.albedo*energy;
        }
        float3 v=outgoing/float(p.samples);
        coefficient+=v;
        cx+=2.0f*v*ray.x;
        cy+=2.0f*v*ray.y;
        cz+=2.0f*v*ray.z;
    }
    // Atlas layout matches GiDynamicShaders.h / GiDynamicProbeTexture.h.
    uint offset=job.index*3u;
    uint2 a=uint2(offset%384u,offset/384u);
    uint2 b=uint2((offset+1u)%384u,(offset+1u)/384u);
    uint2 c=uint2((offset+2u)%384u,(offset+2u)/384u);
    output.write(float4(coefficient.x,cx.x,cy.x,cz.x),a);
    output.write(float4(coefficient.y,cx.y,cy.y,cz.y),b);
    output.write(float4(coefficient.z,cx.z,cy.z,cz.z),c);
}
)MSL";
} // namespace aiverse::render::gi_gpu
