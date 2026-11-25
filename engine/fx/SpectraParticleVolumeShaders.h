// Opt-in GPU-only gas drag on existing particle states, before normal integration.
#pragma once
namespace aiverse::spectra::volume_coupling {
inline constexpr const char* metal=R"METAL(
#include <metal_stdlib>
using namespace metal;
struct Particle {float4 positionAge,velocityLife,tint,size;};
struct Cell {float4 velocity,fields;};
struct Params {float4 originCell,control;uint4 meta;};
kernel void cs_main(uint id [[thread_position_in_grid]],device Particle* particles [[buffer(0)]],
 device const Cell* cells [[buffer(1)]],device const uint* solid [[buffer(2)]],constant Params& u [[buffer(8)]]){
 if(id>=u.meta.x)return;
 Particle p=particles[id];
 if(p.positionAge.w<0.0f||p.velocityLife.w<=0.0f)return;
 float3 q=(p.positionAge.xyz-u.originCell.xyz)/u.originCell.w;
 if(!all(isfinite(q))||any(q<0.0f)||any(q>=float(u.meta.y)))return;
 uint3 i=uint3(q);uint n=u.meta.y;uint index=(i.z*n+i.y)*n+i.x;
 if(solid[index]!=0)return;
 float3 v=cells[index].velocity.xyz;
 if(!all(isfinite(v)))return;
 p.velocityLife.xyz=mix(p.velocityLife.xyz,v,u.control.x);
 particles[id]=p;
}
)METAL";
inline constexpr const char* glsl=R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
struct Particle {vec4 positionAge,velocityLife,tint,size;};
struct Cell {vec4 velocity,fields;};
layout(std430,set=0,binding=0) buffer Particles {Particle particles[];};
layout(std430,set=0,binding=1) readonly buffer Cells {Cell cells[];};
layout(std430,set=0,binding=2) readonly buffer Mask {uint solid[];};
layout(std140,set=0,binding=8) uniform Params {vec4 originCell,control;uvec4 meta;} u;
void main(){
 uint id=gl_GlobalInvocationID.x;if(id>=u.meta.x)return;
 Particle p=particles[id];if(p.positionAge.w<0.0||p.velocityLife.w<=0.0)return;
 vec3 q=(p.positionAge.xyz-u.originCell.xyz)/u.originCell.w;
 if(any(isnan(q))||any(isinf(q))||any(lessThan(q,vec3(0)))||any(greaterThanEqual(q,vec3(float(u.meta.y)))))return;
 uvec3 i=uvec3(q);uint n=u.meta.y;uint index=(i.z*n+i.y)*n+i.x;
 if(solid[index]!=0)return;
 vec3 v=cells[index].velocity.xyz;if(any(isnan(v))||any(isinf(v)))return;
 p.velocityLife.xyz=mix(p.velocityLife.xyz,v,u.control.x);particles[id]=p;
}
)GLSL";
}
