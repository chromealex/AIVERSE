// Vulkan explicit receiver LumaWeave direct shader, composition is opt-in.
#pragma once
#include "LumaWeaveVulkanCommonShader.h"
namespace aiverse::render::luma_vk {
inline constexpr const char* kReceiverDirectCompute = R"GLSL(
#version 450
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
)GLSL" AIVERSE_GLSL_LUMA_VULKAN_COMMON R"GLSL(
layout(set=0,binding=1,std430) readonly buffer ReceiverData { Receiver receivers[]; };

void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=p.counts.y)return;
    ivec2 texel=ivec2(int(i%p.control.y),int(i/p.control.y));
    Receiver r=receivers[i];
    vec3 pos=r.worldPosition.xyz,n=r.normal.xyz;
    float n2=dot(n,n);
    if(r.worldPosition.w<0.5 || !finite3(pos) || !finite3(n) ||
       !(n2>1e-8) || p.counts.x==0u) {
        imageStore(directOutput,texel,vec4(0.0));return;
    }
    n*=inversesqrt(n2);
    float totalWeight=0.0;
    // Unlike the Metal clustered variant, this bounded reference uses ALL
    // sources to preserve correctness even for out-of-grid receivers.
    for(uint j=0u;j<p.counts.x;++j) {
        Light l=lights[j];
        totalWeight+=weight(incident(l,pos,n),l.settings.y);
    }
    if(!(totalWeight>0.0) || isinf(totalWeight) || isnan(totalWeight)) {
        imageStore(directOutput,texel,vec4(0.0));return;
    }
    uint seed=(i+1u)*747796405u ^ (p.control.x+1u)*2891336453u;
    if(seed==0u)seed=1u;
    vec3 direct=vec3(0.0);
    for(uint sample=0u;sample<p.counts.w;++sample) {
        float threshold=float(random32(seed))*(1.0/4294967296.0)*totalWeight;
        float sum=0.0,selectedWeight=0.0;
        vec3 selectedRadiance=vec3(0.0);
        Light selected=lights[0];
        for(uint j=0u;j<p.counts.x;++j) {
            Light l=lights[j];
            vec3 c=incident(l,pos,n);
            float w=weight(c,l.settings.y);
            if(w<=0.0)continue;
            sum+=w;
            if(threshold<sum) {
                selected=l;selectedRadiance=c;selectedWeight=w;break;
            }
        }
        if(selectedWeight<=0.0)continue;
        bool blocked=false;
        if(selected.settings.z>0.5 && p.counts.z>0u) {
            vec3 origin=pos+n*0.02;
            vec3 ray=selected.positionRadius.xyz-origin;
            float dist=length(ray);
            if(dist>0.05) {
                float limit=dist-0.03;
                blocked=shadowed(origin,ray/dist,limit);
            }
        }
        if(!blocked)direct+=selectedRadiance*(totalWeight/selectedWeight);
    }
    direct/=max(1.0,float(p.counts.w));
    imageStore(directOutput,texel,vec4(clamp(direct,vec3(0.0),vec3(65504.0)),1.0));
}
)GLSL";
} // namespace aiverse::render::luma_vk
