// SpectraFX v1 Metal compute particle kernel. Optional, additive billboard subset.
// Paired MSL/GLSL sources have identical 64-byte particle / 40-byte vertex layouts.
// The Vulkan/GL RHI currently does not expose compute. Never invoke on those backends.
#pragma once
namespace aiverse::spectra::shaders {

inline constexpr const char* kMetal = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Particle {
    float4 positionAge;      // xyz position, w age (-1 = unused)
    float4 velocityLife;     // xyz velocity, w lifetime
    float4 tint;             // rgba
    float4 size;             // x billboard size
};
struct Vertex {
    float x, y, z, u, v;
    uint rgba;
    float g0, g1, g2, g3;
};
struct Uniforms {
    float4 gravityDt;       // x gravity Y; y dt; z drag
    float4 rightSize;       // xyz camera right
    float4 up;
    uint4 meta;             // x mode: 0 spawn, 1 advance, 2 expand
                            // y live capacity, z new spawn count
};
uint packColor(float4 c) {
    uint4 bytes=uint4(clamp(c,0.0f,1.0f)*255.0f+0.5f);
    return bytes.x | (bytes.y<<8) | (bytes.z<<16) | (bytes.w<<24);
}
kernel void cs_main(uint2 gid [[thread_position_in_grid]],
                     device Particle* particles [[buffer(0)]],
                     device const Particle* spawns [[buffer(1)]],
                     device const uint* slots [[buffer(2)]],
                     device Vertex* vertices [[buffer(3)]],
                     constant Uniforms& u [[buffer(8)]]) {
    uint id=gid.x;
    if(u.meta.x==0u) {
        if(id>=u.meta.z)return;
        uint slot=slots[id];
        if(slot<u.meta.y) particles[slot]=spawns[id];
        return;
    }
    if(id>=u.meta.y)return;
    Particle p=particles[id];
    if(u.meta.x==1u) {
        if(p.positionAge.w<0.0f)return;
        float dt=u.gravityDt.y;
        p.velocityLife.y+=u.gravityDt.x*dt;
        p.velocityLife.xyz*=max(0.0f,1.0f-u.gravityDt.z*dt);
        p.positionAge.xyz+=p.velocityLife.xyz*dt;
        p.positionAge.w+=dt;
        if(p.positionAge.w>=p.velocityLife.w)p.positionAge.w=-1.0f;
        particles[id]=p;
        return;
    }
    if(u.meta.x!=2u)return;

    uint base=id*4u;
    float3 centre=p.positionAge.xyz;
    bool active=p.positionAge.w>=0.0f && p.velocityLife.w>0.0f;
    float halfSize=active?max(0.0f,p.size.x)*0.5f:0.0f;
    float3 right=u.rightSize.xyz*halfSize;
    float3 up=u.up.xyz*halfSize;
    float3 positions[4]={centre-right-up,centre+right-up,centre+right+up,centre-right+up};
    float2 uv[4]={float2(0,1),float2(1,1),float2(1,0),float2(0,0)};
    float4 color=p.tint;
    color.a=active ? color.a*clamp(1.0f-p.positionAge.w/p.velocityLife.w,0.0f,1.0f) : 0.0f;
    uint rgba=packColor(color);
    for(uint j=0;j<4u;++j) {
        float3 at=active?positions[j]:float3(0.0f);
        vertices[base+j].x=at.x;
        vertices[base+j].y=at.y;
        vertices[base+j].z=at.z;
        vertices[base+j].u=uv[j].x;
        vertices[base+j].v=uv[j].y;
        vertices[base+j].rgba=rgba;
        vertices[base+j].g0=0.0f;
        vertices[base+j].g1=0.0f;
        vertices[base+j].g2=0.0f;
        vertices[base+j].g3=0.0f;
    }
}
)MSL";

// Future GLSL 4.30/Vulkan compute backend. It is deliberately not enabled
// until Vulkan has a working createCompute/buffer barrier contract.
inline constexpr const char* kGLSL = R"GLSL(
#version 430
layout(local_size_x=64) in;
struct Particle { vec4 positionAge,velocityLife,tint,size; };
struct Vertex { float x,y,z,u,v; uint rgba; float g0,g1,g2,g3; };
layout(std430,binding=0) buffer States { Particle particles[]; };
layout(std430,binding=1) readonly buffer Seeds { Particle spawns[]; };
layout(std430,binding=2) readonly buffer Slots { uint slots[]; };
layout(std430,binding=3) buffer Vertices { Vertex vertices[]; };
layout(std140,binding=8) uniform Inputs {
    vec4 gravityDt,rightSize,up;
    uvec4 meta;
} u;
uint packColor(vec4 c) {
    uvec4 b=uvec4(clamp(c,0.0,1.0)*255.0+0.5);
    return b.x|(b.y<<8)|(b.z<<16)|(b.w<<24);
}
void main() {
    uint id=gl_GlobalInvocationID.x;
    if(u.meta.x==0u) {
        if(id<u.meta.z && slots[id]<u.meta.y) particles[slots[id]]=spawns[id];
        return;
    }
    if(id>=u.meta.y)return;
    Particle p=particles[id];
    if(u.meta.x==1u) {
        if(p.positionAge.w<0.0)return;
        float dt=u.gravityDt.y;
        p.velocityLife.y+=u.gravityDt.x*dt;
        p.velocityLife.xyz*=max(0.0,1.0-u.gravityDt.z*dt);
        p.positionAge.xyz+=p.velocityLife.xyz*dt;
        p.positionAge.w+=dt;
        if(p.positionAge.w>=p.velocityLife.w)p.positionAge.w=-1.0;
        particles[id]=p;
        return;
    }
    if(u.meta.x!=2u)return;
    uint base=id*4u;
    bool active=p.positionAge.w>=0.0 && p.velocityLife.w>0.0;
    vec3 centre=p.positionAge.xyz;
    float halfSize=active?max(0.0,p.size.x)*0.5:0.0;
    vec3 right=u.rightSize.xyz*halfSize,up=u.up.xyz*halfSize;
    vec3 positions[4]=vec3[4](centre-right-up,centre+right-up,centre+right+up,centre-right+up);
    vec2 uv[4]=vec2[4](vec2(0,1),vec2(1,1),vec2(1,0),vec2(0,0));
    vec4 color=p.tint;
    color.a=active?color.a*clamp(1.0-p.positionAge.w/p.velocityLife.w,0.0,1.0):0.0;
    uint rgba=packColor(color);
    for(uint j=0u;j<4u;++j) {
        vec3 at=active?positions[j]:vec3(0);
        vertices[base+j].x=at.x;vertices[base+j].y=at.y;vertices[base+j].z=at.z;
        vertices[base+j].u=uv[j].x;vertices[base+j].v=uv[j].y;
        vertices[base+j].rgba=rgba;
        vertices[base+j].g0=0.0;vertices[base+j].g1=0.0;
        vertices[base+j].g2=0.0;vertices[base+j].g3=0.0;
    }
}
)GLSL";
} // namespace aiverse::spectra::shaders
