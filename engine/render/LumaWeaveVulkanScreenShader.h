// Vulkan real frame-space LumaWeave direct PBR shader. Receivers originate
// from the ACTUAL Renderer3D depth, material, material depth, normal/roughness
// and surface depth textures. NO CPU-generated fake GBuffer. This shader is
// DIRECT cosine-weighted GGX+diffuse only, not bounced GI or baked ambient.
// Reuses the exact Vulkan direct BVH/point/spot helpers. One low-res sample
// produces material-REFLECTED RGB and actual scene depth in alpha; do not
// multiply albedo a second time in the HDR additive compositor.
#pragma once
#include "LumaWeaveVulkanCommonShader.h"
namespace aiverse::render::luma_vk {
inline constexpr const char* kScreenDirectCompute = R"GLSL(
#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
)GLSL" AIVERSE_GLSL_LUMA_VULKAN_COMMON R"GLSL(
struct ScreenView {
    mat4 inverseViewProjection;
    vec4 eye;
    vec4 resolution;  // low width,height; real HDR width,height
    vec4 options;     // x=max accepted receiver-depth error, spare yzw
};
layout(set=0,binding=1,std430) readonly buffer ScreenData { ScreenView view; };
layout(set=0,binding=5) uniform sampler2D sceneDepthTexture;
layout(set=0,binding=6) uniform sampler2D materialTexture;
layout(set=0,binding=7) uniform sampler2D materialDepthTexture;
layout(set=0,binding=8) uniform sampler2D surfaceNormalTexture;
layout(set=0,binding=9) uniform sampler2D surfaceDepthTexture;

vec3 pbrDirect(Light light,vec3 pos,vec3 normal,vec3 viewDir,
               vec3 base,float metallic,float rough) {
    vec3 cosineRadiance=incident(light,pos,normal);
    if(all(lessThanEqual(cosineRadiance,vec3(0.0))))return vec3(0.0);
    vec3 toLight=light.positionRadius.xyz-pos;
    float d2=dot(toLight,toLight);
    if(d2<=0.0001)return vec3(0.0);
    vec3 L=toLight*inversesqrt(d2);
    float ndl=max(dot(normal,L),0.0),ndv=max(dot(normal,viewDir),0.0);
    if(ndl<=1e-5 || ndv<=1e-5)return vec3(0.0);
    vec3 halfVector=viewDir+L;
    float h2=dot(halfVector,halfVector);
    if(h2<=1e-8)return vec3(0.0);
    vec3 H=halfVector*inversesqrt(h2);
    float ndh=max(dot(normal,H),0.0);
    float vdh=max(dot(viewDir,H),0.0);
    float alpha=max(0.045,rough);
    float a=alpha*alpha,a2=a*a;
    float term=ndh*ndh*(a2-1.0)+1.0;
    float D=a2/(3.141592653589793*term*term);
    float k=(alpha+1.0)*(alpha+1.0)*0.125;
    float G=(ndl/(ndl*(1.0-k)+k))*(ndv/(ndv*(1.0-k)+k));
    vec3 F0=mix(vec3(0.04),base,metallic);
    vec3 F=F0+(1.0-F0)*pow(1.0-vdh,5.0);
    vec3 diffuse=base*(1.0-metallic)*(1.0-F)*(1.0/3.141592653589793);
    vec3 specular=D*G*F/max(4.0*ndl*ndv,1e-5);
    return cosineRadiance*(diffuse+specular);
}

void main() {
    uint id=gl_GlobalInvocationID.x;
    if(id>=p.counts.y || p.control.y==0u)return;
    ivec2 pixel=ivec2(int(id%p.control.y),int(id/p.control.y));
    vec2 uv=(vec2(pixel)+vec2(0.5))/view.resolution.xy;
    float depth=texture(sceneDepthTexture,uv).r;
    if(depth>=0.99999 || isnan(depth) || isinf(depth) || depth<0.0 ||
       p.counts.x==0u) {
        imageStore(directOutput,pixel,vec4(0.0));return;
    }
    // Receiver materials are valid only when BOTH independently captured
    // depth attachments match the actual, current-view scene depth.
    float materialZ=texture(materialDepthTexture,uv).r;
    float surfaceZ=texture(surfaceDepthTexture,uv).r;
    float tolerance=clamp(view.options.x,0.00001,0.02);
    if(materialZ>=0.99999 || surfaceZ>=0.99999 ||
       abs(materialZ-depth)>tolerance || abs(surfaceZ-depth)>tolerance) {
        imageStore(directOutput,pixel,vec4(0.0,0.0,0.0,depth));
        return;
    }
    vec4 baseMetal=texture(materialTexture,uv);
    vec4 surface=texture(surfaceNormalTexture,uv);
    vec3 normal=surface.xyz;
    float n2=dot(normal,normal);
    if(!finite3(normal) || isnan(surface.w) || isinf(surface.w) ||
       n2<0.25) {
        imageStore(directOutput,pixel,vec4(0.0,0.0,0.0,depth));
        return;
    }
    normal*=inversesqrt(n2);
    vec3 base=clamp(baseMetal.xyz,vec3(0.0),vec3(1.0));
    float metallic=clamp(baseMetal.w,0.0,1.0);
    float rough=clamp(surface.w,0.045,1.0);
    if(!finite3(base) || isnan(metallic) || isinf(metallic)) {
        imageStore(directOutput,pixel,vec4(0.0,0.0,0.0,depth));
        return;
    }
    // The engine feeds GL-convention projection with Vulkan's vertex shader
    // z remap (GL [-1,1] -> VK [0,1]). Undo THAT remap before invVP.
    vec4 clip=vec4(uv*2.0-1.0,depth*2.0-1.0,1.0);
    vec4 homogeneous=view.inverseViewProjection*clip;
    if(abs(homogeneous.w)<1e-8) {
        imageStore(directOutput,pixel,vec4(0.0,0.0,0.0,depth));
        return;
    }
    vec3 world=homogeneous.xyz/homogeneous.w;
    vec3 V=view.eye.xyz-world;
    float v2=dot(V,V);
    if(!finite3(world) || v2<1e-8) {
        imageStore(directOutput,pixel,vec4(0.0,0.0,0.0,depth));
        return;
    }
    V*=inversesqrt(v2);
    float sumWeight=0.0;
    // Explicit full registry path: never discard lights because the GPU
    // cluster cache is crowded or the receiver moved out of grid bounds.
    for(uint j=0u;j<p.counts.x;++j) {
        Light light=lights[j];
        sumWeight+=weight(pbrDirect(light,world,normal,V,base,metallic,rough),
                          light.settings.y);
    }
    if(!(sumWeight>0.0) || isnan(sumWeight) || isinf(sumWeight)) {
        imageStore(directOutput,pixel,vec4(0.0,0.0,0.0,depth));return;
    }
    uint rng=(id+1u)*747796405u ^ (p.control.x+1u)*2891336453u;
    if(rng==0u)rng=1u;
    vec3 total=vec3(0.0);
    for(uint sample=0u;sample<p.counts.w;++sample) {
        float threshold=float(random32(rng))*(1.0/4294967296.0)*sumWeight;
        float cumulative=0.0,chosenWeight=0.0;
        vec3 chosenRadiance=vec3(0.0);
        Light chosen=lights[0];
        for(uint j=0u;j<p.counts.x;++j) {
            Light light=lights[j];
            vec3 candidate=pbrDirect(light,world,normal,V,base,metallic,rough);
            float importanceWeight=weight(candidate,light.settings.y);
            if(importanceWeight<=0.0)continue;
            cumulative+=importanceWeight;
            if(threshold<cumulative) {
                chosen=light;chosenRadiance=candidate;
                chosenWeight=importanceWeight;break;
            }
        }
        if(chosenWeight<=0.0)continue;
        bool blocked=false;
        if(chosen.settings.z>0.5 && p.counts.z>0u) {
            vec3 rayOrigin=world+normal*0.02;
            vec3 delta=chosen.positionRadius.xyz-rayOrigin;
            float distance=length(delta);
            if(distance>0.05)blocked=shadowed(
                rayOrigin,delta/distance,distance-0.03);
        }
        if(!blocked)total+=chosenRadiance*(sumWeight/chosenWeight);
    }
    total/=max(float(p.counts.w),1.0);
    imageStore(directOutput,pixel,
               vec4(clamp(total,vec3(0.0),vec3(65504.0)),depth));
}
)GLSL";
} // namespace aiverse::render::luma_vk
