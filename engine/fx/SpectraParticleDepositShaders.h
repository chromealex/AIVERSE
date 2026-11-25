// Bounded GPU scatter count and gather: no floating atomics or particle readback.
// Reuses one uint per gas cell, allocated lazily under the Device ledger.
#pragma once
namespace aiverse::spectra::volume::deposition {
inline constexpr const char* metal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Particle {float4 positionAge,velocityLife,tint,size;};
struct Cell {float4 velocity,fields;};
struct Params {float4 originCell,rates;uint4 meta;};
kernel void cs_main(uint id [[thread_position_in_grid]],
 device const Particle* particles [[buffer(0)]],device const Cell* input [[buffer(1)]],
 device Cell* output [[buffer(2)]],device const uint* solid [[buffer(3)]],
 device atomic_uint* counts [[buffer(4)]],constant Params& u [[buffer(8)]]){
 uint n=u.meta.y, total=n*n*n;
 if(u.meta.x==0){if(id<total)atomic_store_explicit(&counts[id],0u,memory_order_relaxed);return;}
 if(u.meta.x==1){
  if(id>=u.meta.z)return;
  Particle p=particles[id];
  if(p.positionAge.w<0.0f||p.velocityLife.w<=0.0f||p.positionAge.w>=p.velocityLife.w)return;
  float3 q=(p.positionAge.xyz-u.originCell.xyz)/u.originCell.w;
  if(!all(isfinite(q))||any(q<0.0f)||any(q>=float(n)))return;
  uint3 i=uint3(q);uint index=(i.z*n+i.y)*n+i.x;
  if(solid[index]==0)atomic_fetch_add_explicit(&counts[index],1u,memory_order_relaxed);
  return;
 }
 if(id>=total)return;
 Cell c=input[id];
 if(solid[id]==0)c.fields.xyz=min(float3(16.0f),c.fields.xyz+u.rates.xyz*float(atomic_load_explicit(&counts[id],memory_order_relaxed)));
 output[id]=c;
}
)MSL";
inline constexpr const char* glsl=R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
struct Particle {vec4 positionAge,velocityLife,tint,size;};
struct Cell {vec4 velocity,fields;};
layout(std430,set=0,binding=0) readonly buffer Particles {Particle particles[];};
layout(std430,set=0,binding=1) readonly buffer InputCells {Cell inputCells[];};
layout(std430,set=0,binding=2) buffer OutputCells {Cell outputCells[];};
layout(std430,set=0,binding=3) readonly buffer Mask {uint solid[];};
layout(std430,set=0,binding=4) buffer Counts {uint counts[];};
layout(std140,set=0,binding=8) uniform Params {vec4 originCell,rates;uvec4 meta;} u;
void main(){
 uint id=gl_GlobalInvocationID.x,n=u.meta.y,total=n*n*n;
 if(u.meta.x==0u){if(id<total)counts[id]=0u;return;}
 if(u.meta.x==1u){
  if(id>=u.meta.z)return;
  Particle p=particles[id];
  if(p.positionAge.w<0.0||p.velocityLife.w<=0.0||p.positionAge.w>=p.velocityLife.w)return;
  vec3 q=(p.positionAge.xyz-u.originCell.xyz)/u.originCell.w;
  if(any(isnan(q))||any(isinf(q))||any(lessThan(q,vec3(0)))||any(greaterThanEqual(q,vec3(float(n)))))return;
  uvec3 i=uvec3(q);uint index=(i.z*n+i.y)*n+i.x;
  if(solid[index]==0u)atomicAdd(counts[index],1u);
  return;
 }
 if(id>=total)return;
 Cell c=inputCells[id];
 if(solid[id]==0u)c.fields.xyz=min(vec3(16),c.fields.xyz+u.rates.xyz*float(counts[id]));
 outputCells[id]=c;
}
)GLSL";
}
