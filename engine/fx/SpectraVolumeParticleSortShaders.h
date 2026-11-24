// GPU-only bitonic sort of compact volume records. Sort keys are separate
// from canonical geometry; failure cannot corrupt simulation or draw arguments.
#pragma once
namespace aiverse::spectra::volume_sort {
inline constexpr const char* metal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Record {float x,y,z,u,v;uint rgba;float g0,g1,g2,g3;};
struct Key {float depth;uint index;};
struct Params {float4x4 viewProj;uint4 meta;uint4 limits;};
bool before(Key a,Key b){return a.depth>b.depth||(a.depth==b.depth&&a.index<b.index);}
kernel void cs_main(uint id [[thread_position_in_grid]],
 device const Record* records [[buffer(0)]],device Key* keys [[buffer(1)]],
 device Record* output [[buffer(2)]],device const uint* args [[buffer(3)]],
 constant Params& u [[buffer(8)]]){
 uint mode=u.meta.x,n=u.meta.y,count=min(u.limits.x,u.limits.y!=0u?args[1]:u.limits.z);
 if(id>=n)return;
 if(mode==0u){
  Key k={-3.402823e38f,id};
  if(id<count){
   Record p=records[id];float4 clip=u.viewProj*float4(p.x,p.y,p.z,1);
   float3 wrow=float3(u.viewProj[0].w,u.viewProj[1].w,u.viewProj[2].w);
   float depth=length(wrow)>1e-6f?clip.w:clip.z;
   if(isfinite(depth))k.depth=depth;
  }
  keys[id]=k;return;
 }
 if(mode==1u){
  uint other=id^u.meta.w;
  if(other<=id||other>=n)return;
  Key a=keys[id],b=keys[other];bool forward=(id&u.meta.z)==0u;
  if((forward&&before(b,a))||(!forward&&before(a,b))){keys[id]=b;keys[other]=a;}
  return;
 }
 if(id<count&&keys[id].index<count)output[id]=records[keys[id].index];
}
)MSL";
inline constexpr const char* glsl=R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
struct Record {float x,y,z,u,v;uint rgba;float g0,g1,g2,g3;};
struct Key {float depth;uint index;};
layout(std430,set=0,binding=0) readonly buffer Records {Record records[];};
layout(std430,set=0,binding=1) buffer Keys {Key keys[];};
layout(std430,set=0,binding=2) buffer OutputRecords {Record outputRecords[];};
layout(std430,set=0,binding=3) readonly buffer Args {uint args[];};
layout(std140,set=0,binding=8) uniform Params {mat4 viewProj;uvec4 meta;uvec4 limits;} u;
bool before(Key a,Key b){return a.depth>b.depth||(a.depth==b.depth&&a.index<b.index);}
void main(){
 uint id=gl_GlobalInvocationID.x,mode=u.meta.x,n=u.meta.y;
 uint count=min(u.limits.x,u.limits.y!=0u?args[1]:u.limits.z);
 if(id>=n)return;
 if(mode==0u){
  Key k=Key(-3.402823e38,id);
  if(id<count){
   Record p=records[id];vec4 clip=u.viewProj*vec4(p.x,p.y,p.z,1);
   vec3 wrow=vec3(u.viewProj[0].w,u.viewProj[1].w,u.viewProj[2].w);
   float depth=length(wrow)>1e-6?clip.w:clip.z;
   if(!isnan(depth)&&!isinf(depth))k.depth=depth;
  }
  keys[id]=k;return;
 }
 if(mode==1u){
  uint other=id^u.meta.w;
  if(other<=id||other>=n)return;
  Key a=keys[id],b=keys[other];bool forward=(id&u.meta.z)==0u;
  if((forward&&before(b,a))||(!forward&&before(a,b))){keys[id]=b;keys[other]=a;}
  return;
 }
 if(id<count&&keys[id].index<count)outputRecords[id]=records[keys[id].index];
}
)GLSL";
}
