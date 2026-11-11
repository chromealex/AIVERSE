#pragma once
namespace aiverse::cloth {
inline constexpr const char* kClothComputeMetal=R"GPU(#include <metal_stdlib>
using namespace metal;

struct Edge {uint a;uint b;float rest;float compliance;};
struct Input {float4 forceH;float4 pinFloor;float4 solver;uint4 meta;float4 sleep;uint4 epoch;};
struct SleepState {float quiet;uint candidate;uint sleeping;uint epochLo;uint epochHi;uint pad0;uint pad1;uint pad2;};
struct Capsule {float4 a;float4 b;};
struct Uniforms {uint mode;uint n;uint edgeTotal;uint jobCount;uint edgeStart;uint edgeCount;uint sub;uint iter;uint objects;uint columns;uint pad0;uint pad1;float4 physical;};

kernel void cs_main(uint2 id [[thread_position_in_grid]],uint lane [[thread_index_in_simdgroup]],
 device float4* positions [[buffer(0)]],device float4* previous [[buffer(1)]],device float4* velocity [[buffer(2)]],
 device const Edge* edges [[buffer(3)]],device float* lambdas [[buffer(4)]],device const uint* jobs [[buffer(5)]],
 device const uint* mapping [[buffer(6)]],device const Input* inputs [[buffer(7)]],constant Uniforms& u [[buffer(8)]],
 device const Capsule* capsules [[buffer(9)]],device atomic_uint* moving [[buffer(10)]],device SleepState* states [[buffer(11)]],texture2d<float,access::write> output [[texture(0)]]){

    uint particle=id.x;
    // These passes run one lane per object, with an RHI barrier between modes.
    if(u.mode==5u){
        if(particle!=0u||id.y>=u.objects)return;
        uint object=id.y;Input input=inputs[object];
        if(states[object].epochLo!=input.epoch.x||states[object].epochHi!=input.epoch.y){
            states[object].quiet=0.0;states[object].candidate=0u;states[object].sleeping=0u;
            states[object].epochLo=input.epoch.x;states[object].epochHi=input.epoch.y;
        }
        atomic_store_explicit(&moving[object],0u,memory_order_relaxed);atomic_store_explicit(&moving[u.objects+object],0u,memory_order_relaxed);return;
    }
    if(u.mode==6u){
        if(particle!=0u||id.y>=u.objects)return;
        uint object=id.y,source=mapping[object];Input input=inputs[object];
        if(input.sleep.w<=0.0||states[object].sleeping!=0u)return;
        bool quiet=input.sleep.x>=0.0&&atomic_load_explicit(&moving[source],memory_order_relaxed)==0u;
        states[object].quiet=quiet?states[object].quiet+input.sleep.w:0.0;
        states[object].candidate=quiet&&states[object].quiet>=input.sleep.z?1u:0u;
        if(states[object].candidate==0u)atomic_fetch_or_explicit(&moving[u.objects+source],1u,memory_order_relaxed);return;
    }
    if(u.mode==7u){
        if(particle!=0u||id.y>=u.objects)return;
        uint object=id.y;if(inputs[object].sleep.w<=0.0)return;
        states[object].sleeping=atomic_load_explicit(&moving[u.objects+mapping[object]],memory_order_relaxed)==0u?1u:0u;
        return;
    }

    if(u.mode==4u){
        if(id.y>=u.objects||particle>=u.n*u.n)return;
        uint object=id.y,source=mapping[object],dst=object*u.n*u.n+particle,src=source*u.n*u.n+particle;
        if(source!=object){if(inputs[source].forceH.w>0.0&&states[source].sleeping==0u){positions[dst]=positions[src];velocity[dst]=velocity[src];}return;}
        uint2 xy=uint2((object%u.columns)*u.n+particle%u.n,(object/u.columns)*u.n+particle/u.n);
        output.write(positions[src],xy);return;
    }
    if(id.y>=u.jobCount)return;
    uint object=jobs[id.y];Input input=inputs[object];
    if(states[object].sleeping!=0u)return;
    if(u.sub>=input.meta.x)return;
    uint base=object*u.n*u.n;
    float h=input.forceH.w;
    if(u.mode==1u){
        if(particle>=u.edgeCount||u.iter>=input.meta.y)return;
        uint ei=u.edgeStart+particle;Edge e=edges[ei];uint a=base+e.a,b=base+e.b;
        float wa=positions[a].w,wb=positions[b].w;if(wa+wb==0.0)return;
        uint li=object*u.edgeTotal+ei;float lam=u.iter==0u?0.0:lambdas[li];
        float3 d=positions[a].xyz-positions[b].xyz;float l2=dot(d,d);if(l2<1e-16){lambdas[li]=lam;return;}
        float len=sqrt(l2),alpha=e.compliance*input.solver.x;
        float dl=(-(len-e.rest)-alpha*lam)/(wa+wb+alpha);lambdas[li]=lam+dl;
        float3 correction=d*(dl/len);
        positions[a]=float4(positions[a].xyz+correction*wa,wa);positions[b]=float4(positions[b].xyz-correction*wb,wb);return;
    }
    if(u.mode==3u){
        bool moved=false;
        if(particle<u.n*u.n){uint index=base+particle;float4 p=positions[index];
            float3 v=p.w>0.0?(p.xyz-previous[index].xyz)*input.solver.z:float3(0);velocity[index]=float4(v,0);
            float speed2=dot(v,v);moved=!isfinite(speed2)||speed2>input.sleep.x||speed2*input.sleep.w*input.sleep.w>input.sleep.y;
        }
        if(input.sleep.x>=0.0){bool anyMoved=simd_any(moved);if(lane==0u&&anyMoved)atomic_fetch_or_explicit(&moving[object],1u,memory_order_relaxed);}
        return;
    }
    if(particle>=u.n*u.n)return;
    uint index=base+particle;float4 p=positions[index];
    if(u.mode==0u){
        previous[index]=p;
        if(p.w==0.0){p.xyz=float3(u.physical.x*float(particle%u.n)/float(u.n-1u),-u.physical.y*float(particle/u.n)/float(u.n-1u),0)+input.pinFloor.xyz;}
        else {velocity[index]=float4(velocity[index].xyz*input.solver.y+input.forceH.xyz*h,0);p.xyz+=velocity[index].xyz*h;}
        positions[index]=p;return;
    }
    if(u.mode==2u){
        if(p.w==0.0||u.iter>=input.meta.y)return;
        p.y=max(p.y,input.pinFloor.w+input.solver.w);
        for(uint ci=0u;ci<input.meta.w;++ci){Capsule c=capsules[input.meta.z+ci];float radius=c.a.w+input.solver.w;
            if(any(p.xyz<min(c.a.xyz,c.b.xyz)-radius)||any(p.xyz>max(c.a.xyz,c.b.xyz)+radius))continue;
            float3 ab=c.b.xyz-c.a.xyz;float l2=dot(ab,ab);float t=clamp(dot(p.xyz-c.a.xyz,ab)*(l2>1e-12?1.0/l2:0.0),0.0,1.0);
            float3 center=c.a.xyz+ab*t,delta=p.xyz-center;float d2=dot(delta,delta);
            if(d2<radius*radius)p.xyz=center+(d2>1e-16?delta*(radius/sqrt(d2)):float3(0,0,radius));
        }
        positions[index]=p;return;
    }
}
)GPU";
inline constexpr const char* kClothComputeGLSL=R"GPU(#version 430
layout(local_size_x=64,local_size_y=1) in;

struct Edge {uint a;uint b;float rest;float compliance;};
struct Input {vec4 forceH;vec4 pinFloor;vec4 solver;uvec4 meta;vec4 sleep;uvec4 epoch;};
struct SleepState {float quiet;uint candidate;uint sleeping;uint epochLo;uint epochHi;uint pad0;uint pad1;uint pad2;};
struct Capsule {vec4 a;vec4 b;};
struct Uniforms {uint mode;uint n;uint edgeTotal;uint jobCount;uint edgeStart;uint edgeCount;uint sub;uint iter;uint objects;uint columns;uint pad0;uint pad1;vec4 physical;};

layout(std430,binding=0) buffer P {vec4 positions[];};
layout(std430,binding=1) buffer Old {vec4 previous[];};
layout(std430,binding=2) buffer V {vec4 velocity[];};
layout(std430,binding=3) readonly buffer E {Edge edges[];};
layout(std430,binding=4) buffer L {float lambdas[];};
layout(std430,binding=5) readonly buffer J {uint jobs[];};
layout(std430,binding=6) readonly buffer M {uint mapping[];};
layout(std430,binding=7) readonly buffer I {Input objectInputs[];};
layout(std140,binding=8) uniform U {Uniforms u;};
layout(std430,binding=9) readonly buffer C {Capsule capsules[];};
layout(std430,binding=10) buffer Motion {uint moving[];};
layout(std430,binding=11) buffer Sleep {SleepState states[];};
shared uint motionGroup[64];
layout(rgba32f,binding=0) uniform writeonly image2D outputImage;
void main(){uvec2 id=gl_GlobalInvocationID.xy;

    uint particle=id.x;
    // These passes run one lane per object, with an RHI barrier between modes.
    if(u.mode==5u){
        if(particle!=0u||id.y>=u.objects)return;
        uint object=id.y;Input objectInput=objectInputs[object];
        if(states[object].epochLo!=objectInput.epoch.x||states[object].epochHi!=objectInput.epoch.y){
            states[object].quiet=0.0;states[object].candidate=0u;states[object].sleeping=0u;
            states[object].epochLo=objectInput.epoch.x;states[object].epochHi=objectInput.epoch.y;
        }
        moving[object]=0u;moving[u.objects+object]=0u;return;
    }
    if(u.mode==6u){
        if(particle!=0u||id.y>=u.objects)return;
        uint object=id.y,source=mapping[object];Input objectInput=objectInputs[object];
        if(objectInput.sleep.w<=0.0||states[object].sleeping!=0u)return;
        bool quiet=objectInput.sleep.x>=0.0&&moving[source]==0u;
        states[object].quiet=quiet?states[object].quiet+objectInput.sleep.w:0.0;
        states[object].candidate=quiet&&states[object].quiet>=objectInput.sleep.z?1u:0u;
        if(states[object].candidate==0u)atomicOr(moving[u.objects+source],1u);return;
    }
    if(u.mode==7u){
        if(particle!=0u||id.y>=u.objects)return;
        uint object=id.y;if(objectInputs[object].sleep.w<=0.0)return;
        states[object].sleeping=moving[u.objects+mapping[object]]==0u?1u:0u;
        return;
    }

    if(u.mode==4u){
        if(id.y>=u.objects||particle>=u.n*u.n)return;
        uint object=id.y,source=mapping[object],dst=object*u.n*u.n+particle,src=source*u.n*u.n+particle;
        if(source!=object){if(objectInputs[source].forceH.w>0.0&&states[source].sleeping==0u){positions[dst]=positions[src];velocity[dst]=velocity[src];}return;}
        uvec2 xy=uvec2((object%u.columns)*u.n+particle%u.n,(object/u.columns)*u.n+particle/u.n);
        imageStore(outputImage,ivec2(xy),positions[src]);return;
    }
    if(id.y>=u.jobCount)return;
    uint object=jobs[id.y];Input objectInput=objectInputs[object];
    if(states[object].sleeping!=0u)return;
    if(u.sub>=objectInput.meta.x)return;
    uint base=object*u.n*u.n;
    float h=objectInput.forceH.w;
    if(u.mode==1u){
        if(particle>=u.edgeCount||u.iter>=objectInput.meta.y)return;
        uint ei=u.edgeStart+particle;Edge e=edges[ei];uint a=base+e.a,b=base+e.b;
        float wa=positions[a].w,wb=positions[b].w;if(wa+wb==0.0)return;
        uint li=object*u.edgeTotal+ei;float lam=u.iter==0u?0.0:lambdas[li];
        vec3 d=positions[a].xyz-positions[b].xyz;float l2=dot(d,d);if(l2<1e-16){lambdas[li]=lam;return;}
        float len=sqrt(l2),alpha=e.compliance*objectInput.solver.x;
        float dl=(-(len-e.rest)-alpha*lam)/(wa+wb+alpha);lambdas[li]=lam+dl;
        vec3 correction=d*(dl/len);
        positions[a]=vec4(positions[a].xyz+correction*wa,wa);positions[b]=vec4(positions[b].xyz-correction*wb,wb);return;
    }
    if(u.mode==3u){
        bool moved=false;
        if(particle<u.n*u.n){uint index=base+particle;vec4 p=positions[index];
            vec3 v=p.w>0.0?(p.xyz-previous[index].xyz)*objectInput.solver.z:vec3(0);velocity[index]=vec4(v,0);
            float speed2=dot(v,v);moved=isnan(speed2)||isinf(speed2)||speed2>objectInput.sleep.x||speed2*objectInput.sleep.w*objectInput.sleep.w>objectInput.sleep.y;
        }
        if(objectInput.sleep.x>=0.0){
            motionGroup[gl_LocalInvocationIndex]=moved?1u:0u;barrier();
            if(gl_LocalInvocationIndex==0u){uint anyMoved=0u;for(uint i=0u;i<64u;++i)anyMoved=anyMoved|motionGroup[i];if(anyMoved!=0u)atomicOr(moving[object],1u);}
        }
        return;
    }
    if(particle>=u.n*u.n)return;
    uint index=base+particle;vec4 p=positions[index];
    if(u.mode==0u){
        previous[index]=p;
        if(p.w==0.0){p.xyz=vec3(u.physical.x*float(particle%u.n)/float(u.n-1u),-u.physical.y*float(particle/u.n)/float(u.n-1u),0)+objectInput.pinFloor.xyz;}
        else {velocity[index]=vec4(velocity[index].xyz*objectInput.solver.y+objectInput.forceH.xyz*h,0);p.xyz+=velocity[index].xyz*h;}
        positions[index]=p;return;
    }
    if(u.mode==2u){
        if(p.w==0.0||u.iter>=objectInput.meta.y)return;
        p.y=max(p.y,objectInput.pinFloor.w+objectInput.solver.w);
        for(uint ci=0u;ci<objectInput.meta.w;++ci){Capsule c=capsules[objectInput.meta.z+ci];float radius=c.a.w+objectInput.solver.w;
            if(any(lessThan(p.xyz,min(c.a.xyz,c.b.xyz)-radius))||any(greaterThan(p.xyz,max(c.a.xyz,c.b.xyz)+radius)))continue;
            vec3 ab=c.b.xyz-c.a.xyz;float l2=dot(ab,ab);float t=clamp(dot(p.xyz-c.a.xyz,ab)*(l2>1e-12?1.0/l2:0.0),0.0,1.0);
            vec3 center=c.a.xyz+ab*t,delta=p.xyz-center;float d2=dot(delta,delta);
            if(d2<radius*radius)p.xyz=center+(d2>1e-16?delta*(radius/sqrt(d2)):vec3(0,0,radius));
        }
        positions[index]=p;return;
    }
}
)GPU";
}
