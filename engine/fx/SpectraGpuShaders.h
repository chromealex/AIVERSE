// SpectraFX v1 Metal compute particle kernel. Optional, additive billboard subset.
// Paired MSL/GLSL sources have identical 64-byte particle / 40-byte vertex layouts.
// The Vulkan/GL RHI currently does not expose compute. Never invoke on those backends.
#pragma once
namespace aiverse::spectra::shaders {

// Dedicated migration kernel: old allocator is read-only, candidate is
// unpublished. A completed occupancy proof and unchanged birth epoch
// guarantee the current alive count cannot exceed candidate capacity.
inline constexpr const char* kCompactAllocationMetal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Particle {float4 positionAge,velocityLife,tint,size;};
struct Pair {float4 a,b;};
kernel void cs_main(uint id [[thread_position_in_grid]],
    device const Particle* oldState [[buffer(0)]],
    device const Pair* oldParams [[buffer(1)]],
    device const Pair* oldAttrs [[buffer(2)]],
    device const uint2* oldOwners [[buffer(3)]],
    device const uint* oldAlive [[buffer(4)]],
    device const uint* oldCounters [[buffer(5)]],
    device Particle* newState [[buffer(6)]],
    device Pair* newParams [[buffer(7)]],
    constant uint4& u [[buffer(8)]],
    device Pair* newAttrs [[buffer(9)]],
    device uint2* newOwners [[buffer(10)]],
    device uint* newAlive [[buffer(11)]],
    device uint* newFree [[buffer(12)]],
    device uint* newCounters [[buffer(13)]]) {
    const uint count=oldCounters[0];
    if(count>u.x||count>u.y||id>=u.y)return;
    if(id<count){
        const uint slot=oldAlive[id];
        if(slot>=u.x)return;
        newState[id]=oldState[slot];
        newParams[id]=oldParams[slot];
        newAttrs[id]=oldAttrs[slot];
        newOwners[id]=oldOwners[slot];
        newAlive[id]=id;
    }else newFree[id-count]=u.y-1u-(id-count);
    if(id==0u){
        newCounters[0]=count;newCounters[1]=u.y-count;
        newCounters[2]=0u;newCounters[3]=0u;
        newCounters[4]=count;newCounters[5]=count;
        newCounters[6]=oldCounters[6];newCounters[7]=oldCounters[7];
    }
}
)MSL";

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
    uint4 meta;             // x: 0 spawn, 1 integrate, 2 host gather, 3 reset
                            // 4 atomic compact, 5 GPU-authored indirect args
                            // 6 scatter-kill by physical slot IDs
                            // y: capacity; z: births; w: host survivor count
    float4x4 cullViewProj;
    float4 cullControl;
    float4 lodEye;
    float4 collisionSphere,collisionResponse;
    float4x4 depthViewProj,depthInverse;
    float4 depthControl,depthEye;
};
struct DrawArgs {
    uint indexCount, instanceCount, firstIndex;
    int baseVertex;
    uint firstInstance;
};
// GPU VM ABI mirrors SpectraGpuProgram.h. Slot/field/op numbers are fixed
// and checked by prepareForMetal before a payload is bound.
struct VmParams { float4 p0; float4 p1; };
// Persisted across GPU Spawn/Update in a buffer separate from Particle ABI.
// Every reused physical slot receives a fresh zero-initialized struct.
struct VmAttributes { float4 a0; float4 a1; };
struct VmInstruction { uint opcode; uint field; float scalar; uint reserved; };
struct VmStatement { uint stage; uint action; uint target; uint firstInstruction;
                     uint instructionCount; uint reserved[3]; };
struct VmPayload {
    uint abiVersion, statementCount, instructionCount, parameterMask;
    VmStatement statements[64];
    VmInstruction instructions[512];
};
uint birthRandomBits(Particle p,uint stream) {
    uint seed=uint(p.size.y)|(uint(p.size.z)<<16u);
    uint x=seed^(uint(p.size.w)*0x9e3779b9u)^(stream*0x85ebca6bu);
    x^=x>>16u;x*=0x7feb352du;
    x^=x>>15u;x*=0x846ca68bu;
    return x^(x>>16u);
}
float birthRandom01(Particle p,uint stream) {
    return float(birthRandomBits(p,stream)>>8u)*(1.0f/16777216.0f);
}
float vmRead(uint field,Particle p,VmParams params,VmAttributes attrs,float dt) {
    switch(field) {
    case 0u:return p.positionAge.w; // age
    case 1u:return clamp(p.positionAge.w/max(p.velocityLife.w,0.01f),0.0f,1.0f);
    case 2u:return dt;
    case 32u:return birthRandom01(p,1u);
    case 4u:return p.positionAge.x;
    case 5u:return p.positionAge.y;
    case 6u:return p.positionAge.z;
    case 7u:return p.velocityLife.x;
    case 8u:return p.velocityLife.y;
    case 9u:return p.velocityLife.z;
    case 10u:return p.size.x;
    case 12u:return p.tint.x;
    case 13u:return p.tint.y;
    case 14u:return p.tint.z;
    case 15u:return p.tint.w;
    default:
        if(field>=16u && field<20u)return attrs.a0[field-16u];
        if(field>=20u && field<24u)return attrs.a1[field-20u];
        if(field>=24u && field<28u)return params.p0[field-24u];
        if(field>=28u && field<32u)return params.p1[field-28u];
        return 0.0f; // must be rejected by the C++ ABI validator
    }
}
void vmWrite(uint field,thread Particle& p,thread VmAttributes& attrs,float value) {
    switch(field) {
    case 4u:p.positionAge.x=value;break;
    case 5u:p.positionAge.y=value;break;
    case 6u:p.positionAge.z=value;break;
    case 7u:p.velocityLife.x=value;break;
    case 8u:p.velocityLife.y=value;break;
    case 9u:p.velocityLife.z=value;break;
    case 10u:p.size.x=max(0.0f,value);break;
    case 12u:p.tint.x=clamp(value,0.0f,1.0f);break;
    case 13u:p.tint.y=clamp(value,0.0f,1.0f);break;
    case 14u:p.tint.z=clamp(value,0.0f,1.0f);break;
    case 15u:p.tint.w=clamp(value,0.0f,1.0f);break;
    default:
        if(field>=16u && field<20u)attrs.a0[field-16u]=value;
        else if(field>=20u && field<24u)attrs.a1[field-20u]=value;
        break;
    }
}
// Returns false for malformed/unsafe expressions; an individual failed
// statement is skipped like the CPU VM rather than poisoning GPU state.
bool vmEvaluate(device const VmPayload& vm,VmStatement s,Particle p,
                VmParams params,VmAttributes attrs,float dt,thread float& result) {
    float stack[48];
    uint top=0u;
    if(s.instructionCount==0u || s.instructionCount>48u ||
       s.firstInstruction+s.instructionCount>vm.instructionCount)return false;
    for(uint i=0u;i<s.instructionCount;++i) {
        VmInstruction ins=vm.instructions[s.firstInstruction+i];
        if(ins.opcode<=1u) {
            if(top>=48u)return false;
            float v=ins.opcode==0u?ins.scalar:vmRead(ins.field,p,params,attrs,dt);
            if(!isfinite(v)||fabs(v)>1.0e7f)return false;
            stack[top++]=v;
            continue;
        }
        bool unary=ins.opcode==11u||ins.opcode==12u||ins.opcode==16u;
        bool ternary=ins.opcode==17u||ins.opcode==18u;
        uint required=unary?1u:ternary?3u:2u;
        if(top<required)return false;
        float t=ternary?stack[--top]:0.0f;
        float b=unary?0.0f:stack[--top];
        float a=stack[--top];
        float v=0.0f;
        switch(ins.opcode) {
        case 2u:v=a+b;break;
        case 3u:v=a-b;break;
        case 4u:v=a*b;break;
        case 6u:v=min(a,b);break;
        case 7u:v=max(a,b);break;
        case 11u:v=fabs(a);break;
        case 12u:v=-a;break;
        case 16u:v=clamp(a,0.0f,1.0f);break;
        case 17u:if(b>t)return false;v=clamp(a,b,t);break;
        case 18u:v=a+(b-a)*clamp(t,0.0f,1.0f);break;
        default:return false;
        }
        if(!isfinite(v)||fabs(v)>1.0e7f)return false;
        stack[top++]=v;
    }
    if(top!=1u)return false;
    result=stack[0];
    return true;
}
void vmRun(device const VmPayload& vm,uint stage,thread Particle& p,
           VmParams params,thread VmAttributes& attrs,float dt,
           device atomic_uint* vmErrors) {
    if(vm.abiVersion!=1u || vm.statementCount>64u ||
       vm.instructionCount>512u) {
        atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);
        return;
    }
    for(uint i=0u;i<vm.statementCount;++i) {
        const VmStatement s=vm.statements[i];
        if(s.stage!=stage)continue;
        float value=0.0f;
        if(!vmEvaluate(vm,s,p,params,attrs,dt,value)) {
            atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);
            continue;
        }
        const float before=vmRead(s.target,p,params,attrs,dt);
        float next=value;
        if(s.action==1u)next=before+value;
        else if(s.action==2u)next=before+value*dt;
        else if(s.action==3u)next=before*value;
        else if(s.action!=0u) {
            atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);
            continue;
        }
        if(isfinite(next) && fabs(next)<=1.0e7f)vmWrite(s.target,p,attrs,next);
        else atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);
    }
}
// Vertex shaderGraph sources are compile-time-validated on the CPU.
// The dynamic GPU kernel only gathers sources; no authored source code here.
// Stateless integer hash of the authored seed, birth ordinal and stream.
float materialSource(uint code,Particle p,VmParams params,VmAttributes attrs) {
    switch(code) {
    case 0u:return p.positionAge.w;
    case 1u:return p.velocityLife.w;
    case 2u:return birthRandom01(p,71u);
    case 3u:return 0.0f;
    default:
        if(code>=4u && code<8u)return attrs.a0[code-4u];
        if(code>=8u && code<12u)return attrs.a1[code-8u];
        if(code>=12u && code<16u)return params.p0[code-12u];
        if(code>=16u && code<20u)return params.p1[code-16u];
        return 0.0f; // stable CPU UID random is intentionally GPU-ineligible
    }
}
bool collideStaticSphere(thread Particle& p,float3 from,constant Uniforms& u,float dt,thread float3& eventNormal) {
    if(u.collisionSphere.w<=0.0f || dt<=0.0f)return false;
    float radius=u.collisionSphere.w+max(0.0f,p.size.x)*0.5f*u.collisionResponse.z;
    float3 delta=p.positionAge.xyz-from,m=from-u.collisionSphere.xyz;
    float len=length(delta),dist=length(m);
    float3 n;
    if(dist<radius) {
        n=dist>0.000001f?m/dist:(len>0.000001f?-delta/len:float3(0,1,0));
    }else{
        if(len<=0.000001f)return false;
        float3 d=delta/len;
        float b=dot(m,d);
        if(b>=0.0f)return false;
        float c=dot(m,m)-radius*radius,disc=b*b-c;
        if(disc<0.0f)return false;
        float travel=c/(-b+sqrt(disc));
        if(travel<0.0f||travel>len)return false;
        float3 at=m+d*travel;
        float norm=length(at);
        if(norm<=0.000001f)return false;
        n=at/norm;
    }
    p.positionAge.xyz=u.collisionSphere.xyz+n*(radius+0.001f);
    float vn=dot(p.velocityLife.xyz,n);
    if(vn<0.0f)p.velocityLife.xyz=(p.velocityLife.xyz-n*vn)*(1.0f-u.collisionResponse.y)-n*(vn*u.collisionResponse.x);
    eventNormal=n;return true;
}
bool sampleSdf(device const uint* graphSources,float3 p,thread float& distance,thread float3& gradient) {
    uint n=graphSources[4];
    float3 origin=float3(as_type<float>(graphSources[5]),as_type<float>(graphSources[6]),as_type<float>(graphSources[7]));
    float spacing=as_type<float>(graphSources[8]);
    float3 q=(p-origin)/spacing;
    if(!all(q>=float3(0))||!all(q<=float3(float(n-1u))))return false;
    uint3 base=min(uint3(q),uint3(n-2u));
    float3 t=q-float3(base);
    uint i=9u+(base.z*n+base.y)*n+base.x;
    float a=as_type<float>(graphSources[i]),b=as_type<float>(graphSources[i+1u]),c=as_type<float>(graphSources[i+n]),d=as_type<float>(graphSources[i+n+1u]);
    i+=n*n;
    float e=as_type<float>(graphSources[i]),f=as_type<float>(graphSources[i+1u]),g=as_type<float>(graphSources[i+n]),h=as_type<float>(graphSources[i+n+1u]);
    distance=mix(mix(mix(a,b,t.x),mix(c,d,t.x),t.y),mix(mix(e,f,t.x),mix(g,h,t.x),t.y),t.z);
    gradient=float3(mix(mix(b-a,d-c,t.y),mix(f-e,h-g,t.y),t.z),
                mix(mix(c-a,d-b,t.x),mix(g-e,h-f,t.x),t.z),
                mix(mix(e-a,f-b,t.x),mix(g-c,h-d,t.x),t.y))/spacing;
    return true;
}
bool collideSdf(thread Particle& p,float3 from,constant Uniforms& u,device const uint* graphSources,float dt,thread float3& eventNormal) {
    if(u.collisionResponse.w<0.5f||dt<=0.0f)return false;
    graphSources+=uint(u.collisionResponse.w)-1u;
    float3 delta=p.positionAge.xyz-from;
    float len=length(delta),spacing=as_type<float>(graphSources[8]);
    uint steps=uint(clamp(ceil(len/(spacing*0.5f)),1.0f,64.0f));
    float radius=max(0.0f,p.size.x)*0.5f*u.collisionResponse.z;
    float distance,previous=0.0f,penetration=0.0f,t=0.0f;
    float3 normal;
    if(!sampleSdf(graphSources,from,distance,normal))return false;
    if(distance<radius)penetration=radius-distance;
    else {
        bool found=false;
        for(uint i=1u;i<=steps;++i) {
            float next=float(i)/float(steps);
            if(!sampleSdf(graphSources,from+delta*next,distance,normal))return false;
            if(distance<=radius) {
                float lo=previous,hi=next;
                for(uint j=0u;j<8u;++j) {
                    float mid=(lo+hi)*0.5f;
                    if(!sampleSdf(graphSources,from+delta*mid,distance,normal))return false;
                    if(distance>radius)lo=mid;else hi=mid;
                }
                t=lo;
                if(!sampleSdf(graphSources,from+delta*hi,distance,normal))return false;
                found=true;break;
            }
            previous=next;
        }
        if(!found)return false;
    }
    float norm=length(normal);
    if(norm<=0.00001f)return false;
    normal/=norm;
    if(penetration==0.0f&&dot(delta,normal)>=0.0f)return false;
    p.positionAge.xyz=from+delta*t+normal*(penetration+0.001f);
    float vn=dot(p.velocityLife.xyz,normal);
    if(vn<0.0f)p.velocityLife.xyz=(p.velocityLife.xyz-normal*vn)*(1.0f-u.collisionResponse.y)-normal*(vn*u.collisionResponse.x);
    eventNormal=normal;return true;
}
bool depthPoint(constant Uniforms& u,device const uint* graphSources,int2 pixel,thread float3& point) {
    float depth=as_type<float>(graphSources[4u+uint(pixel.y)*64u+uint(pixel.x)]);
    if(!(depth>=0.0f&&depth<0.99999f))return false;
    float2 uv=(float2(pixel)+0.5f)/64.0f;
    float y=u.depthControl.z>0.5f?1.0f-2.0f*uv.y:2.0f*uv.y-1.0f;
    float4 p=u.depthInverse*float4(uv.x*2.0f-1.0f,y,u.depthControl.y>0.5f?depth:depth*2.0f-1.0f,1.0f);
    if(abs(p.w)<0.000001f)return false;
    point=p.xyz/p.w;
    return true;
}
bool collideDepth(thread Particle& p,float3 from,constant Uniforms& u,device const uint* graphSources,float dt,thread float3& eventNormal) {
    if(u.depthControl.x<0.5f||dt<=0.0f)return false;
    float4 clip=u.depthViewProj*float4(p.positionAge.xyz,1.0f);
    if(clip.w<=0.000001f)return false;
    float2 ndc=clip.xy/clip.w;
    float2 uv=float2(ndc.x*0.5f+0.5f,u.depthControl.z>0.5f?0.5f-ndc.y*0.5f:0.5f+ndc.y*0.5f);
    if(!all(uv>=float2(0))||!all(uv<float2(1)))return false;
    int2 pixel=int2(uv*64.0f);
    if(pixel.x<1||pixel.y<1||pixel.x>=63||pixel.y>=63)return false;
    float3 a,b,c;
    if(!depthPoint(u,graphSources,pixel,a)||!depthPoint(u,graphSources,pixel+int2(1,0),b)||!depthPoint(u,graphSources,pixel+int2(0,1),c))return false;
    float3 x=b-a,y=c-a;
    if(length(x)>u.depthControl.w||length(y)>u.depthControl.w)return false;
    float3 normal=cross(x,y);
    float n=length(normal);
    if(n<0.000001f)return false;
    normal/=n;
    if(dot(normal,u.depthEye.xyz-a)<0.0f)normal=-normal;
    float radius=max(0.0f,p.size.x)*0.5f*u.collisionResponse.z;
    float start=dot(from-a,normal)-radius,end=dot(p.positionAge.xyz-a,normal)-radius;
    // Only front-to-back crossings; no hidden-side penetration recovery.
    if(start<0.0f||end>=0.0f||start-end<0.000001f)return false;
    float fraction=start/(start-end);
    float3 contact=mix(from,p.positionAge.xyz,fraction);
    if(length(contact-a)>u.depthControl.w+radius)return false;
    p.positionAge.xyz=contact+normal*0.001f;
    float vn=dot(p.velocityLife.xyz,normal);
    if(vn<0.0f)p.velocityLife.xyz=(p.velocityLife.xyz-normal*vn)*(1.0f-u.collisionResponse.y)-normal*(vn*u.collisionResponse.x);
    eventNormal=normal;return true;
}
void reportCollision(device atomic_uint* vmErrors,constant Uniforms& u,Particle p,float3 normal,uint kind,uint slot){
    if(u.depthEye.w<0.5f||kind==0u)return;
    uint at=atomic_fetch_add_explicit(&vmErrors[1],1u,memory_order_relaxed);
    if(at>=64u)return;
    uint base=2u+at*8u;
    float3 pos=p.positionAge.xyz;
    atomic_store_explicit(&vmErrors[base],as_type<uint>(pos.x),memory_order_relaxed);
    atomic_store_explicit(&vmErrors[base+1u],as_type<uint>(pos.y),memory_order_relaxed);
    atomic_store_explicit(&vmErrors[base+2u],as_type<uint>(pos.z),memory_order_relaxed);
    atomic_store_explicit(&vmErrors[base+3u],as_type<uint>(normal.x),memory_order_relaxed);
    atomic_store_explicit(&vmErrors[base+4u],as_type<uint>(normal.y),memory_order_relaxed);
    atomic_store_explicit(&vmErrors[base+5u],as_type<uint>(normal.z),memory_order_relaxed);
    atomic_store_explicit(&vmErrors[base+6u],kind,memory_order_relaxed);
    atomic_store_explicit(&vmErrors[base+7u],slot,memory_order_relaxed);
}
uint packColor(float4 c) {
    uint4 bytes=uint4(clamp(c,0.0f,1.0f)*255.0f+0.5f);
    return bytes.x | (bytes.y<<8) | (bytes.z<<16) | (bytes.w<<24);
}
kernel void cs_main(uint2 gid [[thread_position_in_grid]],
                     device Particle* particles [[buffer(0)]],
                     device const Particle* spawns [[buffer(1)]],
                     device const uint* slots [[buffer(2)]],
                     device Vertex* vertices [[buffer(3)]],
                     device uint* liveSlots [[buffer(4)]],
                     device atomic_uint* aliveCounter [[buffer(5)]],
                     device DrawArgs* gpuDrawArgs [[buffer(6)]],
                     device const VmParams* seedParams [[buffer(7)]],
                     constant Uniforms& u [[buffer(8)]],
                     device VmParams* stateParams [[buffer(9)]],
                     device const VmPayload* vmProgram [[buffer(10)]],
                     device VmAttributes* stateAttributes [[buffer(11)]],
                     device atomic_uint* vmErrors [[buffer(12)]],
                     device const uint* graphSources [[buffer(13)]],
                      device const uint* killSlots [[buffer(14)]]
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
                     ,device uint2* owners [[buffer(15)]]
                     ,device uint* currentAlive [[buffer(16)]]
                     ,device uint* nextAlive [[buffer(17)]]
                     ,device uint* dispatchArgs [[buffer(18)]]
#endif
                     ) {
    uint id=gid.x;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
    // Counters: alive, free, reserved base, admitted, work, next alive, visible, LOD rejected.
    // Kill marks entries in the old list; only update returns their slots.
    // Thus a killed slot cannot be reused while it is still in the input list.
    // Growth writes the unpublished replacement free stack, then publishes
    // its free count in a separate ordered dispatch. Old physical IDs survive.
    if(u.meta.x==14u){
        uint old=u.meta.z;
        if(old<u.meta.y&&id<u.meta.y-old){
            uint freeCount=min(atomic_load_explicit(&aliveCounter[1],memory_order_relaxed),old);
            liveSlots[freeCount+id]=u.meta.y-1u-id;
        }
        return;
    }
    if(u.meta.x==15u){
        if(id==0u&&u.meta.z<u.meta.y){
            uint freeCount=min(atomic_load_explicit(&aliveCounter[1],memory_order_relaxed),u.meta.z);
            atomic_store_explicit(&aliveCounter[1],freeCount+u.meta.y-u.meta.z,memory_order_relaxed);
        }
        return;
    }
    if(u.meta.x==9u){
        uint count=min(atomic_load_explicit(&aliveCounter[0],memory_order_relaxed),u.meta.y);
        if(id<count){
            uint slot=currentAlive[id];
            if(slot<u.meta.y&&particles[slot].positionAge.w>=0.0f&&
               all(owners[slot]==uint2(u.meta.z,u.meta.w)))
                particles[slot].positionAge.w=-2.0f;
        }
        return;
    }
    if(u.meta.x==10u){
        if(id==0u){
            uint oldCount=min(atomic_load_explicit(&aliveCounter[0],memory_order_relaxed),u.meta.y);
            uint freeCount=min(atomic_load_explicit(&aliveCounter[1],memory_order_relaxed),u.meta.y);
            uint admitted=min(min(freeCount,u.meta.z),u.meta.y-oldCount);
            uint base=freeCount-admitted;
            atomic_store_explicit(&aliveCounter[1],base,memory_order_relaxed);
            atomic_store_explicit(&aliveCounter[2],base,memory_order_relaxed);
            atomic_store_explicit(&aliveCounter[3],admitted,memory_order_relaxed);
            atomic_store_explicit(&aliveCounter[4],oldCount+admitted,memory_order_relaxed);
            atomic_store_explicit(&aliveCounter[5],0u,memory_order_relaxed);
            dispatchArgs[0]=max(1u,(oldCount+admitted+63u)/64u);
            dispatchArgs[1]=dispatchArgs[2]=1u;
        }
        return;
    }
    if(u.meta.x==8u){
        uint admitted=atomic_load_explicit(&aliveCounter[3],memory_order_relaxed);
        if(id>=admitted||id>=u.meta.z)return;
        uint base=atomic_load_explicit(&aliveCounter[2],memory_order_relaxed);
        uint oldCount=atomic_load_explicit(&aliveCounter[0],memory_order_relaxed);
        if(base>=u.meta.y||id>=u.meta.y-base||oldCount+id>=u.meta.y)return;
        uint slot=liveSlots[base+id];
        if(slot>=u.meta.y)return;
        Particle p=spawns[id];VmParams params=seedParams[id];VmAttributes attrs{};
        vmRun(vmProgram[0],0u,p,params,attrs,0.0f,vmErrors);
        particles[slot]=p;stateParams[slot]=params;stateAttributes[slot]=attrs;
        owners[slot]=uint2(slots[id*2u],slots[id*2u+1u]);
        currentAlive[oldCount+id]=slot;
        return;
    }
    if(u.meta.x==11u){
        uint count=min(atomic_load_explicit(&aliveCounter[4],memory_order_relaxed),u.meta.y);
        if(id>=count)return;
        uint slot=currentAlive[id];
        if(slot>=u.meta.y){atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);return;}
        Particle p=particles[slot];
        bool dead=p.positionAge.w<0.0f;
        if(!dead&&u.up.w>0.5f){
            float dt=u.gravityDt.y;
            float3 previousPosition=p.positionAge.xyz;
            p.velocityLife.y+=u.gravityDt.x*dt;
            p.velocityLife.xyz*=max(0.0f,1.0f-u.gravityDt.z*dt);
            p.positionAge.xyz+=p.velocityLife.xyz*dt;p.positionAge.w+=dt;
            dead=p.positionAge.w>=p.velocityLife.w;
            if(!dead){
                VmParams params=stateParams[slot];VmAttributes attrs=stateAttributes[slot];
                vmRun(vmProgram[0],1u,p,params,attrs,dt,vmErrors);float3 eventNormal=float3(0);uint eventKind=0u;
            if(collideStaticSphere(p,previousPosition,u,dt,eventNormal))eventKind=1u;
            else if(collideSdf(p,previousPosition,u,graphSources,dt,eventNormal))eventKind=2u;
            else if(collideDepth(p,previousPosition,u,graphSources,dt,eventNormal))eventKind=3u;
            reportCollision(vmErrors,u,p,eventNormal,eventKind,slot);stateAttributes[slot]=attrs;
            }
        }
        if(dead){
            p.positionAge.w=-1.0f;owners[slot]=uint2(0);
            uint at=atomic_fetch_add_explicit(&aliveCounter[1],1u,memory_order_relaxed);
            if(at<u.meta.y)liveSlots[at]=slot;
            else atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);
        }else{
            uint at=atomic_fetch_add_explicit(&aliveCounter[5],1u,memory_order_relaxed);
            if(at<u.meta.y)nextAlive[at]=slot;
            else atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);
        }
        particles[slot]=p;
        return;
    }
    if(u.meta.x==13u){
        if(id==0u){
            uint count=min(atomic_load_explicit(&aliveCounter[5],memory_order_relaxed),u.meta.y);
            atomic_store_explicit(&aliveCounter[0],count,memory_order_relaxed);
            atomic_store_explicit(&aliveCounter[6],u.cullControl.x>0.5f?0u:count,memory_order_relaxed);
            atomic_store_explicit(&aliveCounter[7],0u,memory_order_relaxed);
            dispatchArgs[0]=max(1u,(count+63u)/64u);
            dispatchArgs[1]=dispatchArgs[2]=1u;
        }
        return;
    }
#endif
    if(u.meta.x==6u) {
        if(id>=u.meta.z)return; // z carries kill count for this dispatch
        uint slot=killSlots[id];
        if(slot<u.meta.y) {
            Particle p=particles[slot];
            p.positionAge.w=-1.0f;
            particles[slot]=p;
        }
        return;
    }
    if(u.meta.x==0u) {
        if(id>=u.meta.z)return;
        uint slot=slots[id];
        if(slot<u.meta.y) {
            Particle p=spawns[id];
            VmParams params=seedParams[id];
            VmAttributes attrs{}; // no leakage from recycled physical slot
            vmRun(vmProgram[0],0u,p,params,attrs,0.0f,vmErrors);
            particles[slot]=p;
            stateParams[slot]=params;
            stateAttributes[slot]=attrs;
        }
        return;
    }
    if(u.meta.x==3u) {
        if(id==0u) atomic_store_explicit(&aliveCounter[0],0u,memory_order_relaxed);
        return;
    }
    if(u.meta.x==5u) {
        if(id==0u) {
            uint alive=min(atomic_load_explicit(&aliveCounter[0],memory_order_relaxed),u.meta.y);
 #ifdef SPECTRA_GPU_OWNED_ALLOCATION
            alive=min(atomic_load_explicit(&aliveCounter[6],memory_order_relaxed),alive);
 #endif
            const bool procedural=u.gravityDt.w>0.5f;
            gpuDrawArgs[0].indexCount=procedural?6u:alive*6u;
            gpuDrawArgs[0].instanceCount=procedural?alive:1u;
            gpuDrawArgs[0].firstIndex=0u;
            gpuDrawArgs[0].baseVertex=0;
            gpuDrawArgs[0].firstInstance=0u;
        }
        return;
    }
    if(u.meta.x==1u) {
        if(id>=u.meta.y)return;
        Particle p=particles[id];
        if(p.positionAge.w<0.0f)return;
        float dt=u.gravityDt.y;
        float3 previousPosition=p.positionAge.xyz;
        p.velocityLife.y+=u.gravityDt.x*dt;
        p.velocityLife.xyz*=max(0.0f,1.0f-u.gravityDt.z*dt);
        p.positionAge.xyz+=p.velocityLife.xyz*dt;
        p.positionAge.w+=dt;
        if(p.positionAge.w>=p.velocityLife.w){
            p.positionAge.w=-1.0f;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
            // Only a live -> dead transition returns a slot. Subsequent
            // integration/kill passes see the dead marker and skip it.
            owners[id]=uint2(0);
            uint at=atomic_fetch_add_explicit(&aliveCounter[1],1u,memory_order_relaxed);
            if(at<u.meta.y)liveSlots[at]=id;
            else atomic_fetch_add_explicit(vmErrors,1u,memory_order_relaxed);
#endif
        }else {
            VmParams params=stateParams[id];
            VmAttributes attrs=stateAttributes[id];
            vmRun(vmProgram[0],1u,p,params,attrs,dt,vmErrors);
            float3 eventNormal=float3(0);uint eventKind=0u;
            if(collideStaticSphere(p,previousPosition,u,dt,eventNormal))eventKind=1u;
            else if(collideSdf(p,previousPosition,u,graphSources,dt,eventNormal))eventKind=2u;
            else if(collideDepth(p,previousPosition,u,graphSources,dt,eventNormal))eventKind=3u;
            reportCollision(vmErrors,u,p,eventNormal,eventKind,id);
            stateAttributes[id]=attrs;
        }
        particles[id]=p;
        return;
    }
    if(u.meta.x!=2u && u.meta.x!=4u && u.meta.x!=12u)return;
    if(u.meta.x==2u && id>=u.meta.w)return;
    if(id>=u.meta.y)return;
    // Mode 2 uses the CPU's live map; mode 4 compacts entirely on GPU.
    uint slot=u.meta.x==2u ? liveSlots[id] : id;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
    if(u.meta.x==12u){
        if(id>=min(atomic_load_explicit(&aliveCounter[0],memory_order_relaxed),u.meta.y))return;
        slot=nextAlive[id];
    }
#else
    if(u.meta.x==12u)return;
#endif
    if(slot>=u.meta.y)return;
    Particle p=particles[slot];
    uint ordinal=id;
    if(u.meta.x==4u) {
        if(p.positionAge.w<0.0f||p.velocityLife.w<=0.0f)return;
        ordinal=atomic_fetch_add_explicit(&aliveCounter[0],1u,memory_order_relaxed);
        if(ordinal>=u.meta.y)return;
    }
    float3 centre=p.positionAge.xyz;
    bool active=p.positionAge.w>=0.0f && p.velocityLife.w>0.0f;
    float halfSize=active?max(0.0f,p.size.x)*0.5f:0.0f;
    float3 right=u.rightSize.xyz*halfSize;
    float3 up=u.up.xyz*halfSize;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
    if(u.meta.x==12u&&u.cullControl.x>0.5f){
        if(!active)return;
        // Homogeneous plane support of the actual billboard quad. A quad
        // is outside only if ALL corners are outside the SAME clip plane.
        float4 c=u.cullViewProj*float4(centre,1.0f);
        float4 r=u.cullViewProj*float4(right,0.0f);
        float4 v=u.cullViewProj*float4(up,0.0f);
        if(all(isfinite(c))&&all(isfinite(r))&&all(isfinite(v))){
            // Use the wider [-w,w] depth interval to remain conservative
            // across projection conventions; rasterization handles near z.
            float3 hi=c.www-c.xyz+abs(r.www-r.xyz)+abs(v.www-v.xyz);
            float3 lo=c.www+c.xyz+abs(r.www+r.xyz)+abs(v.www+v.xyz);
            float eps=1e-5f*(1.0f+max(max(abs(c.x),abs(c.y)),max(abs(c.z),abs(c.w)))+
                       max(max(abs(r.x),abs(r.y)),max(abs(r.z),abs(r.w)))+
                       max(max(abs(v.x),abs(v.y)),max(abs(v.z),abs(v.w))));
            if(isfinite(eps)&&all(isfinite(hi))&&all(isfinite(lo))&&
               (any(hi < -eps)||any(lo < -eps)))return;
            // Medium quality: only distant AND small billboards are thinned.
            // Bound the full projected extent in pixels; do not thin quads
            // crossing the eye plane or with an ambiguous projection.
            if(u.cullControl.w>u.lodEye.w && c.w>0.0f){
                float minW=c.w-abs(r.w)-abs(v.w);
                if(minW>max(1e-6f,c.w*1e-5f)){
                    float2 ndc=c.xy/c.w;
                    float2 extent=(abs(r.xy-ndc*r.w)+abs(v.xy-ndc*v.w))
                                  *u.cullControl.yz/minW;
                    float pixels=max(extent.x,extent.y);
                    float dist=length(centre-u.lodEye.xyz);
                    if(isfinite(pixels)&&isfinite(dist)){
                        float farFactor=smoothstep(u.lodEye.w,u.cullControl.w,dist);
                        float smallFactor=1.0f-smoothstep(2.0f,8.0f,pixels);
                        float keep=1.0f-0.75f*farFactor*smallFactor;
                        // Stable physical slot, not frame or compact ordinal:
                        // frozen camera/particles produce identical membership.
                        uint hash=slot^0x9e3779b9u;
                        hash^=hash>>16u;hash*=0x7feb352du;
                        hash^=hash>>15u;hash*=0x846ca68bu;hash^=hash>>16u;
                        float sample=float(hash>>8u)*(1.0f/16777216.0f);
                        if(sample>=keep){
                            atomic_fetch_add_explicit(&aliveCounter[7],1u,memory_order_relaxed);
                            return;
                        }
                    }
                }
            }
        }
        ordinal=atomic_fetch_add_explicit(&aliveCounter[6],1u,memory_order_relaxed);
        if(ordinal>=u.meta.y)return;
    }
#endif
    uint base=ordinal*4u;
    float3 positions[4]={centre-right-up,centre+right-up,centre+right+up,centre-right+up};
    float2 uv[4]={float2(0,1),float2(1,1),float2(1,0),float2(0,0)};
    float4 color=p.tint;
    color.a=active ? color.a*clamp(1.0f-p.positionAge.w/p.velocityLife.w,0.0f,1.0f) : 0.0f;
    uint rgba=packColor(color);
    // Compact path: same 40-byte ABI, one record per surviving particle.
    // u = half size, v = unused. Expansion happens in the vertex shader.
    if(u.gravityDt.w>0.5f){
        Vertex record{};
        record.x=active?centre.x:0.0f;record.y=active?centre.y:0.0f;record.z=active?centre.z:0.0f;
        record.u=halfSize;record.v=active?clamp(p.positionAge.w/max(p.velocityLife.w,0.01f),0.0f,1.0f):0.0f;record.rgba=rgba;
        vertices[ordinal]=record;
        return;
    }
    VmAttributes attributes=stateAttributes[slot];
    VmParams sourceParams=stateParams[slot];
    const uint sourceBase=u.collisionResponse.w>=1.0f?uint(u.collisionResponse.w)-1u:0u;
    float4 graphData=float4(
        materialSource(graphSources[sourceBase],p,sourceParams,attributes),
        materialSource(graphSources[sourceBase+1u],p,sourceParams,attributes),
        materialSource(graphSources[sourceBase+2u],p,sourceParams,attributes),
        materialSource(graphSources[sourceBase+3u],p,sourceParams,attributes));
    for(uint j=0;j<4u;++j) {
        float3 at=active?positions[j]:float3(0.0f);
        vertices[base+j].x=at.x;
        vertices[base+j].y=at.y;
        vertices[base+j].z=at.z;
        vertices[base+j].u=uv[j].x;
        vertices[base+j].v=uv[j].y;
        vertices[base+j].rgba=rgba;
        vertices[base+j].g0=graphData.x;
        vertices[base+j].g1=graphData.y;
        vertices[base+j].g2=graphData.z;
        vertices[base+j].g3=graphData.w;
    }
}
)MSL";

// Metal indexed-instanced billboard expansion. Buffer 0 is the compact
// compute-written record stream, not a vertex descriptor attribute stream.
inline constexpr const char* kProceduralBillboardMetal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Record { float x,y,z,u,v; uint rgba; float g0,g1,g2,g3; };
struct U0 { float4x4 viewProj; float4 fog,camPos,params,right,up; };
struct VOut { float4 pos [[position]]; float2 uv; half4 col; };
vertex VOut vs_main(uint vertexIndex [[vertex_id]],uint instance [[instance_id]],
                    device const Record* records [[buffer(0)]],
                    constant U0& u [[buffer(1)]]) {
    const Record p=records[instance];
    const float2 corners[4]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,1)};
    const float2 uv[4]={float2(0,1),float2(1,1),float2(1,0),float2(0,0)};
    const uint j=vertexIndex&3u;
    const float3 at=float3(p.x,p.y,p.z)+p.u*(u.right.xyz*corners[j].x+u.up.xyz*corners[j].y);
    float4 c=float4(float(p.rgba&255u),float((p.rgba>>8)&255u),
                    float((p.rgba>>16)&255u),float((p.rgba>>24)&255u))/255.0f;
    VOut o;o.pos=u.viewProj*float4(at,1.0f);o.uv=uv[j];
    const float f=1.0f-exp(-length(at-u.camPos.xyz)*u.fog.w);
    c.rgb=mix(c.rgb,mix(u.fog.rgb*mix(1.0f,c.a,u.params.y),float3(1.0f),u.params.z),f*(1.0f-u.params.x));
    c.a*=1.0f-f*u.params.x;o.col=half4(c);return o;
}
fragment half4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
                      texture2d<half> tex0 [[texture(0)]],sampler s0 [[sampler(0)]]) {
    float4 c=float4(tex0.sample(s0,in.uv)*in.col);
#if AIVERSE_HDR
    if(u.params.y>0.5f)c.rgb=rtsSrgbDecode(c.rgb/max(c.a,0.0001f))*c.a;
    else c=rtsDisplay(c);
#endif
    return half4(c);
}
)MSL";

// Vulkan GLSL mirror of the bounded Metal ABI and dispatch modes.
// Explicit storage/uniform layout; never sent to legacy GL backends.
inline constexpr const char* kGLSL = R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
struct Particle {
    vec4 positionAge;      // xyz position, w age (-1 = unused)
    vec4 velocityLife;     // xyz velocity, w lifetime
    vec4 tint;             // rgba
    vec4 size;             // x billboard size
};
struct Vertex {
    float x, y, z, u, v;
    uint rgba;
    float g0, g1, g2, g3;
};
struct DrawArgs {
    uint indexCount, instanceCount, firstIndex;
    int baseVertex;
    uint firstInstance;
};
// GPU VM ABI mirrors SpectraGpuProgram.h. Slot/field/op numbers are fixed
// and checked by prepareForMetal before a payload is bound.
struct VmParams { vec4 p0; vec4 p1; };
// Persisted across GPU Spawn/Update in a buffer separate from Particle ABI.
// Every reused physical slot receives a fresh zero-initialized struct.
struct VmAttributes { vec4 a0; vec4 a1; };
struct VmInstruction { uint opcode; uint field; float scalar; uint reserved; };
struct VmStatement { uint stage; uint action; uint target; uint firstInstruction;
                     uint instructionCount; uint reserved[3]; };
struct VmPayload {
    uint abiVersion, statementCount, instructionCount, parameterMask;
    VmStatement statements[64];
    VmInstruction instructions[512];
};

layout(std430,set=0,binding=0) buffer States {Particle particles[];};
layout(std430,set=0,binding=1) readonly buffer Seeds {Particle spawns[];};
layout(std430,set=0,binding=2) readonly buffer Slots {uint slots[];};
layout(std430,set=0,binding=3) buffer Vertices {Vertex vertices[];};
layout(std430,set=0,binding=4) buffer LiveSlots {uint liveSlots[];};
layout(std430,set=0,binding=5) buffer Counters {uint aliveCounter[];};
layout(std430,set=0,binding=6) buffer DrawBuffer {DrawArgs gpuDrawArgs[];};
layout(std430,set=0,binding=7) readonly buffer SeedParams {VmParams seedParams[];};
layout(std140,set=0,binding=8) uniform Inputs {vec4 gravityDt,rightSize,up;uvec4 meta;mat4 cullViewProj;vec4 cullControl,lodEye,collisionSphere,collisionResponse;mat4 depthViewProj,depthInverse;vec4 depthControl,depthEye;} u;
layout(std430,set=0,binding=9) buffer StateParams {VmParams stateParams[];};
layout(std430,set=0,binding=10) readonly buffer Program {VmPayload vmProgram;};
layout(std430,set=0,binding=11) buffer Attributes {VmAttributes stateAttributes[];};
layout(std430,set=0,binding=12) buffer Errors {uint vmErrors[];};
layout(std430,set=0,binding=13) readonly buffer Sources {uint graphSources[];};
layout(std430,set=0,binding=14) readonly buffer KillSlots {uint killSlots[];};
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
layout(std430,set=0,binding=15) buffer Owners {uvec2 owners[];};
layout(std430,set=0,binding=16) buffer CurrentAlive {uint currentAlive[];};
layout(std430,set=0,binding=17) buffer NextAlive {uint nextAlive[];};
layout(std430,set=0,binding=18) buffer DispatchArgs {uint dispatchArgs[];};
#endif
bool finiteValue(float x){return !isnan(x)&&!isinf(x);}
bvec3 finiteValue(vec3 x){return bvec3(finiteValue(x.x),finiteValue(x.y),finiteValue(x.z));}
bvec4 finiteValue(vec4 x){return bvec4(finiteValue(x.x),finiteValue(x.y),finiteValue(x.z),finiteValue(x.w));}
uint birthRandomBits(Particle p,uint stream) {
    uint seed=uint(p.size.y)|(uint(p.size.z)<<16u);
    uint x=seed^(uint(p.size.w)*0x9e3779b9u)^(stream*0x85ebca6bu);
    x^=x>>16u;x*=0x7feb352du;
    x^=x>>15u;x*=0x846ca68bu;
    return x^(x>>16u);
}
float birthRandom01(Particle p,uint stream) {
    return float(birthRandomBits(p,stream)>>8u)*(1.0/16777216.0);
}
float vmRead(uint field,Particle p,VmParams params,VmAttributes attrs,float dt) {
    switch(field) {
    case 0u:return p.positionAge.w; // age
    case 1u:return clamp(p.positionAge.w/max(p.velocityLife.w,0.01),0.0,1.0);
    case 2u:return dt;
    case 32u:return birthRandom01(p,1u);
    case 4u:return p.positionAge.x;
    case 5u:return p.positionAge.y;
    case 6u:return p.positionAge.z;
    case 7u:return p.velocityLife.x;
    case 8u:return p.velocityLife.y;
    case 9u:return p.velocityLife.z;
    case 10u:return p.size.x;
    case 12u:return p.tint.x;
    case 13u:return p.tint.y;
    case 14u:return p.tint.z;
    case 15u:return p.tint.w;
    default:
        if(field>=16u && field<20u)return attrs.a0[field-16u];
        if(field>=20u && field<24u)return attrs.a1[field-20u];
        if(field>=24u && field<28u)return params.p0[field-24u];
        if(field>=28u && field<32u)return params.p1[field-28u];
        return 0.0; // must be rejected by the C++ ABI validator
    }
}
void vmWrite(uint field,inout Particle p,inout VmAttributes attrs,float value) {
    switch(field) {
    case 4u:p.positionAge.x=value;break;
    case 5u:p.positionAge.y=value;break;
    case 6u:p.positionAge.z=value;break;
    case 7u:p.velocityLife.x=value;break;
    case 8u:p.velocityLife.y=value;break;
    case 9u:p.velocityLife.z=value;break;
    case 10u:p.size.x=max(0.0,value);break;
    case 12u:p.tint.x=clamp(value,0.0,1.0);break;
    case 13u:p.tint.y=clamp(value,0.0,1.0);break;
    case 14u:p.tint.z=clamp(value,0.0,1.0);break;
    case 15u:p.tint.w=clamp(value,0.0,1.0);break;
    default:
        if(field>=16u && field<20u)attrs.a0[field-16u]=value;
        else if(field>=20u && field<24u)attrs.a1[field-20u]=value;
        break;
    }
}
// Returns false for malformed/unsafe expressions; an individual failed
// statement is skipped like the CPU VM rather than poisoning GPU state.
bool vmEvaluate(VmStatement s,Particle p,
                VmParams params,VmAttributes attrs,float dt,out float result) {
    float stack[48];
    uint top=0u;
    if(s.instructionCount==0u || s.instructionCount>48u ||
       s.firstInstruction+s.instructionCount>vmProgram.instructionCount)return false;
    for(uint i=0u;i<s.instructionCount;++i) {
        VmInstruction ins=vmProgram.instructions[s.firstInstruction+i];
        if(ins.opcode<=1u) {
            if(top>=48u)return false;
            float v=ins.opcode==0u?ins.scalar:vmRead(ins.field,p,params,attrs,dt);
            if(!finiteValue(v)||abs(v)>1.0e7)return false;
            stack[top++]=v;
            continue;
        }
        bool unary=ins.opcode==11u||ins.opcode==12u||ins.opcode==16u;
        bool ternary=ins.opcode==17u||ins.opcode==18u;
        uint required=unary?1u:ternary?3u:2u;
        if(top<required)return false;
        float t=ternary?stack[--top]:0.0;
        float b=unary?0.0:stack[--top];
        float a=stack[--top];
        float v=0.0;
        switch(ins.opcode) {
        case 2u:v=a+b;break;
        case 3u:v=a-b;break;
        case 4u:v=a*b;break;
        case 6u:v=min(a,b);break;
        case 7u:v=max(a,b);break;
        case 11u:v=abs(a);break;
        case 12u:v=-a;break;
        case 16u:v=clamp(a,0.0,1.0);break;
        case 17u:if(b>t)return false;v=clamp(a,b,t);break;
        case 18u:v=a+(b-a)*clamp(t,0.0,1.0);break;
        default:return false;
        }
        if(!finiteValue(v)||abs(v)>1.0e7)return false;
        stack[top++]=v;
    }
    if(top!=1u)return false;
    result=stack[0];
    return true;
}
void vmRun(uint stage,inout Particle p,
           VmParams params,inout VmAttributes attrs,float dt) {
    if(vmProgram.abiVersion!=1u || vmProgram.statementCount>64u ||
       vmProgram.instructionCount>512u) {
        atomicAdd(vmErrors[0],1u);
        return;
    }
    for(uint i=0u;i<vmProgram.statementCount;++i) {
        VmStatement s=vmProgram.statements[i];
        if(s.stage!=stage)continue;
        float value=0.0;
        if(!vmEvaluate(s,p,params,attrs,dt,value)) {
            atomicAdd(vmErrors[0],1u);
            continue;
        }
        float before=vmRead(s.target,p,params,attrs,dt);
        float next=value;
        if(s.action==1u)next=before+value;
        else if(s.action==2u)next=before+value*dt;
        else if(s.action==3u)next=before*value;
        else if(s.action!=0u) {
            atomicAdd(vmErrors[0],1u);
            continue;
        }
        if(finiteValue(next) && abs(next)<=1.0e7)vmWrite(s.target,p,attrs,next);
        else atomicAdd(vmErrors[0],1u);
    }
}
// Vertex shaderGraph sources are compile-time-validated on the CPU.
// The dynamic GPU kernel only gathers sources; no authored source code here.
// Stateless integer hash of the authored seed, birth ordinal and stream.
float materialSource(uint code,Particle p,VmParams params,VmAttributes attrs) {
    switch(code) {
    case 0u:return p.positionAge.w;
    case 1u:return p.velocityLife.w;
    case 2u:return birthRandom01(p,71u);
    case 3u:return 0.0;
    default:
        if(code>=4u && code<8u)return attrs.a0[code-4u];
        if(code>=8u && code<12u)return attrs.a1[code-8u];
        if(code>=12u && code<16u)return params.p0[code-12u];
        if(code>=16u && code<20u)return params.p1[code-16u];
        return 0.0; // stable CPU UID random is intentionally GPU-ineligible
    }
}
bool collideStaticSphere(inout Particle p,vec3 from,float dt,out vec3 eventNormal) {
    if(u.collisionSphere.w<=0.0 || dt<=0.0)return false;
    float radius=u.collisionSphere.w+max(0.0,p.size.x)*0.5*u.collisionResponse.z;
    vec3 delta=p.positionAge.xyz-from,m=from-u.collisionSphere.xyz;
    float len=length(delta),dist=length(m);
    vec3 n;
    if(dist<radius) {
        n=dist>0.000001?m/dist:(len>0.000001?-delta/len:vec3(0,1,0));
    }else{
        if(len<=0.000001)return false;
        vec3 d=delta/len;
        float b=dot(m,d);
        if(b>=0.0)return false;
        float c=dot(m,m)-radius*radius,disc=b*b-c;
        if(disc<0.0)return false;
        float travel=c/(-b+sqrt(disc));
        if(travel<0.0||travel>len)return false;
        vec3 at=m+d*travel;
        float norm=length(at);
        if(norm<=0.000001)return false;
        n=at/norm;
    }
    p.positionAge.xyz=u.collisionSphere.xyz+n*(radius+0.001);
    float vn=dot(p.velocityLife.xyz,n);
    if(vn<0.0)p.velocityLife.xyz=(p.velocityLife.xyz-n*vn)*(1.0-u.collisionResponse.y)-n*(vn*u.collisionResponse.x);
    eventNormal=n;return true;
}
bool sampleSdf(vec3 p,out float distance,out vec3 gradient) {
    uint sourceBase=uint(u.collisionResponse.w)-1u;
    uint n=graphSources[sourceBase+4u];
    vec3 origin=vec3(uintBitsToFloat(graphSources[sourceBase+5u]),uintBitsToFloat(graphSources[sourceBase+6u]),uintBitsToFloat(graphSources[sourceBase+7u]));
    float spacing=uintBitsToFloat(graphSources[sourceBase+8u]);
    vec3 q=(p-origin)/spacing;
    if(!all(greaterThanEqual(q,vec3(0)))||!all(lessThanEqual(q,vec3(float(n-1u)))))return false;
    uvec3 base=min(uvec3(q),uvec3(n-2u));
    vec3 t=q-vec3(base);
    uint i=sourceBase+9u+(base.z*n+base.y)*n+base.x;
    float a=uintBitsToFloat(graphSources[i]),b=uintBitsToFloat(graphSources[i+1u]),c=uintBitsToFloat(graphSources[i+n]),d=uintBitsToFloat(graphSources[i+n+1u]);
    i+=n*n;
    float e=uintBitsToFloat(graphSources[i]),f=uintBitsToFloat(graphSources[i+1u]),g=uintBitsToFloat(graphSources[i+n]),h=uintBitsToFloat(graphSources[i+n+1u]);
    distance=mix(mix(mix(a,b,t.x),mix(c,d,t.x),t.y),mix(mix(e,f,t.x),mix(g,h,t.x),t.y),t.z);
    gradient=vec3(mix(mix(b-a,d-c,t.y),mix(f-e,h-g,t.y),t.z),
                mix(mix(c-a,d-b,t.x),mix(g-e,h-f,t.x),t.z),
                mix(mix(e-a,f-b,t.x),mix(g-c,h-d,t.x),t.y))/spacing;
    return true;
}
bool collideSdf(inout Particle p,vec3 from,float dt,out vec3 eventNormal) {
    if(u.collisionResponse.w<0.5||dt<=0.0)return false;
    vec3 delta=p.positionAge.xyz-from;
    float len=length(delta),spacing=uintBitsToFloat(graphSources[uint(u.collisionResponse.w)-1u+8u]);
    uint steps=uint(clamp(ceil(len/(spacing*0.5)),1.0,64.0));
    float radius=max(0.0,p.size.x)*0.5*u.collisionResponse.z;
    float distance,previous=0.0,penetration=0.0,t=0.0;
    vec3 normal;
    if(!sampleSdf(from,distance,normal))return false;
    if(distance<radius)penetration=radius-distance;
    else {
        bool found=false;
        for(uint i=1u;i<=steps;++i) {
            float next=float(i)/float(steps);
            if(!sampleSdf(from+delta*next,distance,normal))return false;
            if(distance<=radius) {
                float lo=previous,hi=next;
                for(uint j=0u;j<8u;++j) {
                    float mid=(lo+hi)*0.5;
                    if(!sampleSdf(from+delta*mid,distance,normal))return false;
                    if(distance>radius)lo=mid;else hi=mid;
                }
                t=lo;
                if(!sampleSdf(from+delta*hi,distance,normal))return false;
                found=true;break;
            }
            previous=next;
        }
        if(!found)return false;
    }
    float norm=length(normal);
    if(norm<=0.00001)return false;
    normal/=norm;
    if(penetration==0.0&&dot(delta,normal)>=0.0)return false;
    p.positionAge.xyz=from+delta*t+normal*(penetration+0.001);
    float vn=dot(p.velocityLife.xyz,normal);
    if(vn<0.0)p.velocityLife.xyz=(p.velocityLife.xyz-normal*vn)*(1.0-u.collisionResponse.y)-normal*(vn*u.collisionResponse.x);
    eventNormal=normal;return true;
}
bool depthPoint(ivec2 pixel,out vec3 point) {
    float depth=uintBitsToFloat(graphSources[4u+uint(pixel.y)*64u+uint(pixel.x)]);
    if(!(depth>=0.0&&depth<0.99999))return false;
    vec2 uv=(vec2(pixel)+0.5)/64.0;
    float y=u.depthControl.z>0.5?1.0-2.0*uv.y:2.0*uv.y-1.0;
    vec4 p=u.depthInverse*vec4(uv.x*2.0-1.0,y,u.depthControl.y>0.5?depth:depth*2.0-1.0,1.0);
    if(abs(p.w)<0.000001)return false;
    point=p.xyz/p.w;
    return true;
}
bool collideDepth(inout Particle p,vec3 from,float dt,out vec3 eventNormal) {
    if(u.depthControl.x<0.5||dt<=0.0)return false;
    vec4 clip=u.depthViewProj*vec4(p.positionAge.xyz,1.0);
    if(clip.w<=0.000001)return false;
    vec2 ndc=clip.xy/clip.w;
    vec2 uv=vec2(ndc.x*0.5+0.5,u.depthControl.z>0.5?0.5-ndc.y*0.5:0.5+ndc.y*0.5);
    if(!all(greaterThanEqual(uv,vec2(0)))||!all(lessThan(uv,vec2(1))))return false;
    ivec2 pixel=ivec2(uv*64.0);
    if(pixel.x<1||pixel.y<1||pixel.x>=63||pixel.y>=63)return false;
    vec3 a,b,c;
    if(!depthPoint(pixel,a)||!depthPoint(pixel+ivec2(1,0),b)||!depthPoint(pixel+ivec2(0,1),c))return false;
    vec3 x=b-a,y=c-a;
    if(length(x)>u.depthControl.w||length(y)>u.depthControl.w)return false;
    vec3 normal=cross(x,y);
    float n=length(normal);
    if(n<0.000001)return false;
    normal/=n;
    if(dot(normal,u.depthEye.xyz-a)<0.0)normal=-normal;
    float radius=max(0.0,p.size.x)*0.5*u.collisionResponse.z;
    float start=dot(from-a,normal)-radius,end=dot(p.positionAge.xyz-a,normal)-radius;
    // Only front-to-back crossings; no hidden-side penetration recovery.
    if(start<0.0||end>=0.0||start-end<0.000001)return false;
    float fraction=start/(start-end);
    vec3 contact=mix(from,p.positionAge.xyz,fraction);
    if(length(contact-a)>u.depthControl.w+radius)return false;
    p.positionAge.xyz=contact+normal*0.001;
    float vn=dot(p.velocityLife.xyz,normal);
    if(vn<0.0)p.velocityLife.xyz=(p.velocityLife.xyz-normal*vn)*(1.0-u.collisionResponse.y)-normal*(vn*u.collisionResponse.x);
    eventNormal=normal;return true;
}
void reportCollision(Particle p,vec3 normal,uint kind,uint slot){
    if(u.depthEye.w<0.5||kind==0u)return;
    uint at=atomicAdd(vmErrors[1],1u);
    if(at>=64u)return;
    uint base=2u+at*8u;
    vec3 pos=p.positionAge.xyz;
    vmErrors[base]=floatBitsToUint(pos.x);vmErrors[base+1u]=floatBitsToUint(pos.y);vmErrors[base+2u]=floatBitsToUint(pos.z);
    vmErrors[base+3u]=floatBitsToUint(normal.x);vmErrors[base+4u]=floatBitsToUint(normal.y);vmErrors[base+5u]=floatBitsToUint(normal.z);
    vmErrors[base+6u]=kind;vmErrors[base+7u]=slot;
}
uint packColor(vec4 c) {
    uvec4 bytes=uvec4(clamp(c,0.0,1.0)*255.0+0.5);
    return bytes.x | (bytes.y<<8) | (bytes.z<<16) | (bytes.w<<24);
}

void main(){
    uint id=gl_GlobalInvocationID.x;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
    // Counters: alive, free, reserved base, admitted, work, next alive, visible, LOD rejected.
    // Kill marks entries in the old list; only update returns their slots.
    // Thus a killed slot cannot be reused while it is still in the input list.
    // Growth writes the unpublished replacement free stack, then publishes
    // its free count in a separate ordered dispatch. Old physical IDs survive.
    if(u.meta.x==14u){
        uint old=u.meta.z;
        if(old<u.meta.y&&id<u.meta.y-old){
            uint freeCount=min(atomicAdd(aliveCounter[1],0u),old);
            liveSlots[freeCount+id]=u.meta.y-1u-id;
        }
        return;
    }
    if(u.meta.x==15u){
        if(id==0u&&u.meta.z<u.meta.y){
            uint freeCount=min(atomicAdd(aliveCounter[1],0u),u.meta.z);
            atomicExchange(aliveCounter[1],freeCount+u.meta.y-u.meta.z);
        }
        return;
    }
    if(u.meta.x==9u){
        uint count=min(atomicAdd(aliveCounter[0],0u),u.meta.y);
        if(id<count){
            uint slot=currentAlive[id];
            if(slot<u.meta.y&&particles[slot].positionAge.w>=0.0&&
               (owners[slot]==uvec2(u.meta.z,u.meta.w)))
                particles[slot].positionAge.w=-2.0;
        }
        return;
    }
    if(u.meta.x==10u){
        if(id==0u){
            uint oldCount=min(atomicAdd(aliveCounter[0],0u),u.meta.y);
            uint freeCount=min(atomicAdd(aliveCounter[1],0u),u.meta.y);
            uint admitted=min(min(freeCount,u.meta.z),u.meta.y-oldCount);
            uint base=freeCount-admitted;
            atomicExchange(aliveCounter[1],base);
            atomicExchange(aliveCounter[2],base);
            atomicExchange(aliveCounter[3],admitted);
            atomicExchange(aliveCounter[4],oldCount+admitted);
            atomicExchange(aliveCounter[5],0u);
            dispatchArgs[0]=max(1u,(oldCount+admitted+63u)/64u);
            dispatchArgs[1]=dispatchArgs[2]=1u;
        }
        return;
    }
    if(u.meta.x==8u){
        uint admitted=atomicAdd(aliveCounter[3],0u);
        if(id>=admitted||id>=u.meta.z)return;
        uint base=atomicAdd(aliveCounter[2],0u);
        uint oldCount=atomicAdd(aliveCounter[0],0u);
        if(base>=u.meta.y||id>=u.meta.y-base||oldCount+id>=u.meta.y)return;
        uint slot=liveSlots[base+id];
        if(slot>=u.meta.y)return;
        Particle p=spawns[id];VmParams params=seedParams[id];VmAttributes attrs=VmAttributes(vec4(0.0),vec4(0.0));
        vmRun(0u,p,params,attrs,0.0);
        particles[slot]=p;stateParams[slot]=params;stateAttributes[slot]=attrs;
        owners[slot]=uvec2(slots[id*2u],slots[id*2u+1u]);
        currentAlive[oldCount+id]=slot;
        return;
    }
    if(u.meta.x==11u){
        uint count=min(atomicAdd(aliveCounter[4],0u),u.meta.y);
        if(id>=count)return;
        uint slot=currentAlive[id];
        if(slot>=u.meta.y){atomicAdd(vmErrors[0],1u);return;}
        Particle p=particles[slot];
        bool dead=p.positionAge.w<0.0;
        if(!dead&&u.up.w>0.5){
            float dt=u.gravityDt.y;
            vec3 previousPosition=p.positionAge.xyz;
            p.velocityLife.y+=u.gravityDt.x*dt;
            p.velocityLife.xyz*=max(0.0,1.0-u.gravityDt.z*dt);
            p.positionAge.xyz+=p.velocityLife.xyz*dt;p.positionAge.w+=dt;
            dead=p.positionAge.w>=p.velocityLife.w;
            if(!dead){
                VmParams params=stateParams[slot];VmAttributes attrs=stateAttributes[slot];
                vmRun(1u,p,params,attrs,dt);vec3 eventNormal=vec3(0);uint eventKind=0u;
            if(collideStaticSphere(p,previousPosition,dt,eventNormal))eventKind=1u;
            else if(collideSdf(p,previousPosition,dt,eventNormal))eventKind=2u;
            else if(collideDepth(p,previousPosition,dt,eventNormal))eventKind=3u;
            reportCollision(p,eventNormal,eventKind,slot);stateAttributes[slot]=attrs;
            }
        }
        if(dead){
            p.positionAge.w=-1.0;owners[slot]=uvec2(0);
            uint at=atomicAdd(aliveCounter[1],1u);
            if(at<u.meta.y)liveSlots[at]=slot;
            else atomicAdd(vmErrors[0],1u);
        }else{
            uint at=atomicAdd(aliveCounter[5],1u);
            if(at<u.meta.y)nextAlive[at]=slot;
            else atomicAdd(vmErrors[0],1u);
        }
        particles[slot]=p;
        return;
    }
    if(u.meta.x==13u){
        if(id==0u){
            uint count=min(atomicAdd(aliveCounter[5],0u),u.meta.y);
            atomicExchange(aliveCounter[0],count);
            atomicExchange(aliveCounter[6],u.cullControl.x>0.5?0u:count);
            atomicExchange(aliveCounter[7],0u);
            dispatchArgs[0]=max(1u,(count+63u)/64u);
            dispatchArgs[1]=dispatchArgs[2]=1u;
        }
        return;
    }
#endif
    if(u.meta.x==6u) {
        if(id>=u.meta.z)return; // z carries kill count for this dispatch
        uint slot=killSlots[id];
        if(slot<u.meta.y) {
            Particle p=particles[slot];
            p.positionAge.w=-1.0;
            particles[slot]=p;
        }
        return;
    }
    if(u.meta.x==0u) {
        if(id>=u.meta.z)return;
        uint slot=slots[id];
        if(slot<u.meta.y) {
            Particle p=spawns[id];
            VmParams params=seedParams[id];
            VmAttributes attrs=VmAttributes(vec4(0.0),vec4(0.0)); // no leakage from recycled physical slot
            vmRun(0u,p,params,attrs,0.0);
            particles[slot]=p;
            stateParams[slot]=params;
            stateAttributes[slot]=attrs;
        }
        return;
    }
    if(u.meta.x==3u) {
        if(id==0u) atomicExchange(aliveCounter[0],0u);
        return;
    }
    if(u.meta.x==5u) {
        if(id==0u) {
            uint alive=min(atomicAdd(aliveCounter[0],0u),u.meta.y);
 #ifdef SPECTRA_GPU_OWNED_ALLOCATION
            alive=min(atomicAdd(aliveCounter[6],0u),alive);
 #endif
            bool procedural=u.gravityDt.w>0.5;
            gpuDrawArgs[0].indexCount=procedural?6u:alive*6u;
            gpuDrawArgs[0].instanceCount=procedural?alive:1u;
            gpuDrawArgs[0].firstIndex=0u;
            gpuDrawArgs[0].baseVertex=0;
            gpuDrawArgs[0].firstInstance=0u;
        }
        return;
    }
    if(u.meta.x==1u) {
        if(id>=u.meta.y)return;
        Particle p=particles[id];
        if(p.positionAge.w<0.0)return;
        float dt=u.gravityDt.y;
        vec3 previousPosition=p.positionAge.xyz;
        p.velocityLife.y+=u.gravityDt.x*dt;
        p.velocityLife.xyz*=max(0.0,1.0-u.gravityDt.z*dt);
        p.positionAge.xyz+=p.velocityLife.xyz*dt;
        p.positionAge.w+=dt;
        if(p.positionAge.w>=p.velocityLife.w){
            p.positionAge.w=-1.0;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
            // Only a live -> dead transition returns a slot. Subsequent
            // integration/kill passes see the dead marker and skip it.
            owners[id]=uvec2(0);
            uint at=atomicAdd(aliveCounter[1],1u);
            if(at<u.meta.y)liveSlots[at]=id;
            else atomicAdd(vmErrors[0],1u);
#endif
        }else {
            VmParams params=stateParams[id];
            VmAttributes attrs=stateAttributes[id];
            vmRun(1u,p,params,attrs,dt);
            vec3 eventNormal=vec3(0);uint eventKind=0u;
            if(collideStaticSphere(p,previousPosition,dt,eventNormal))eventKind=1u;
            else if(collideSdf(p,previousPosition,dt,eventNormal))eventKind=2u;
            else if(collideDepth(p,previousPosition,dt,eventNormal))eventKind=3u;
            reportCollision(p,eventNormal,eventKind,id);
            stateAttributes[id]=attrs;
        }
        particles[id]=p;
        return;
    }
    if(u.meta.x!=2u && u.meta.x!=4u && u.meta.x!=12u)return;
    if(u.meta.x==2u && id>=u.meta.w)return;
    if(id>=u.meta.y)return;
    // Mode 2 uses the CPU's live map; mode 4 compacts entirely on GPU.
    uint slot=u.meta.x==2u ? liveSlots[id] : id;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
    if(u.meta.x==12u){
        if(id>=min(atomicAdd(aliveCounter[0],0u),u.meta.y))return;
        slot=nextAlive[id];
    }
#else
    if(u.meta.x==12u)return;
#endif
    if(slot>=u.meta.y)return;
    Particle p=particles[slot];
    uint ordinal=id;
    if(u.meta.x==4u) {
        if(p.positionAge.w<0.0||p.velocityLife.w<=0.0)return;
        ordinal=atomicAdd(aliveCounter[0],1u);
        if(ordinal>=u.meta.y)return;
    }
    vec3 centre=p.positionAge.xyz;
    bool particleActive=p.positionAge.w>=0.0 && p.velocityLife.w>0.0;
    float halfSize=particleActive?max(0.0,p.size.x)*0.5:0.0;
    vec3 right=u.rightSize.xyz*halfSize;
    vec3 up=u.up.xyz*halfSize;
#ifdef SPECTRA_GPU_OWNED_ALLOCATION
    if(u.meta.x==12u&&u.cullControl.x>0.5){
        if(!particleActive)return;
        // Homogeneous plane support of the actual billboard quad. A quad
        // is outside only if ALL corners are outside the SAME clip plane.
        vec4 c=u.cullViewProj*vec4(centre,1.0);
        vec4 r=u.cullViewProj*vec4(right,0.0);
        vec4 v=u.cullViewProj*vec4(up,0.0);
        if(all(finiteValue(c))&&all(finiteValue(r))&&all(finiteValue(v))){
            // Use the wider [-w,w] depth interval to remain conservative
            // across projection conventions; rasterization handles near z.
            vec3 hi=c.www-c.xyz+abs(r.www-r.xyz)+abs(v.www-v.xyz);
            vec3 lo=c.www+c.xyz+abs(r.www+r.xyz)+abs(v.www+v.xyz);
            float eps=1e-5*(1.0+max(max(abs(c.x),abs(c.y)),max(abs(c.z),abs(c.w)))+
                       max(max(abs(r.x),abs(r.y)),max(abs(r.z),abs(r.w)))+
                       max(max(abs(v.x),abs(v.y)),max(abs(v.z),abs(v.w))));
            if(finiteValue(eps)&&all(finiteValue(hi))&&all(finiteValue(lo))&&
               (any(lessThan(hi,vec3(-eps)))||any(lessThan(lo,vec3(-eps)))))return;
            // Medium quality: only distant AND small billboards are thinned.
            // Bound the full projected extent in pixels; do not thin quads
            // crossing the eye plane or with an ambiguous projection.
            if(u.cullControl.w>u.lodEye.w && c.w>0.0){
                float minW=c.w-abs(r.w)-abs(v.w);
                if(minW>max(1e-6,c.w*1e-5)){
                    vec2 ndc=c.xy/c.w;
                    vec2 extent=(abs(r.xy-ndc*r.w)+abs(v.xy-ndc*v.w))
                                  *u.cullControl.yz/minW;
                    float pixels=max(extent.x,extent.y);
                    float dist=length(centre-u.lodEye.xyz);
                    if(finiteValue(pixels)&&finiteValue(dist)){
                        float farFactor=smoothstep(u.lodEye.w,u.cullControl.w,dist);
                        float smallFactor=1.0-smoothstep(2.0,8.0,pixels);
                        float keep=1.0-0.75*farFactor*smallFactor;
                        // Stable physical slot, not frame or compact ordinal:
                        // frozen camera/particles produce identical membership.
                        uint hash=slot^0x9e3779b9u;
                        hash^=hash>>16u;hash*=0x7feb352du;
                        hash^=hash>>15u;hash*=0x846ca68bu;hash^=hash>>16u;
                        float selectionSample=float(hash>>8u)*(1.0/16777216.0);
                        if(selectionSample>=keep){
                            atomicAdd(aliveCounter[7],1u);
                            return;
                        }
                    }
                }
            }
        }
        ordinal=atomicAdd(aliveCounter[6],1u);
        if(ordinal>=u.meta.y)return;
    }
#endif
    uint base=ordinal*4u;
    vec3 positions[4]=vec3[4](centre-right-up,centre+right-up,centre+right+up,centre-right+up);
    vec2 uv[4]=vec2[4](vec2(0,1),vec2(1,1),vec2(1,0),vec2(0,0));
    vec4 color=p.tint;
    color.a=particleActive ? color.a*clamp(1.0-p.positionAge.w/p.velocityLife.w,0.0,1.0) : 0.0;
    uint rgba=packColor(color);
    // Compact path: same 40-byte ABI, one record per surviving particle.
    // u = half size, v = unused. Expansion happens in the vertex shader.
    if(u.gravityDt.w>0.5){
        Vertex record=Vertex(0.0,0.0,0.0,0.0,0.0,0u,0.0,0.0,0.0,0.0);
        record.x=particleActive?centre.x:0.0;record.y=particleActive?centre.y:0.0;record.z=particleActive?centre.z:0.0;
        record.u=halfSize;record.v=particleActive?clamp(p.positionAge.w/max(p.velocityLife.w,0.01),0.0,1.0):0.0;record.rgba=rgba;
        vertices[ordinal]=record;
        return;
    }
    VmAttributes attributes=stateAttributes[slot];
    VmParams sourceParams=stateParams[slot];
    uint sourceBase=u.collisionResponse.w>=1.0?uint(u.collisionResponse.w)-1u:0u;
    vec4 graphData=vec4(
        materialSource(graphSources[sourceBase],p,sourceParams,attributes),
        materialSource(graphSources[sourceBase+1u],p,sourceParams,attributes),
        materialSource(graphSources[sourceBase+2u],p,sourceParams,attributes),
        materialSource(graphSources[sourceBase+3u],p,sourceParams,attributes));
    for(uint j=0;j<4u;++j) {
        vec3 at=particleActive?positions[j]:vec3(0.0);
        vertices[base+j].x=at.x;
        vertices[base+j].y=at.y;
        vertices[base+j].z=at.z;
        vertices[base+j].u=uv[j].x;
        vertices[base+j].v=uv[j].y;
        vertices[base+j].rgba=rgba;
        vertices[base+j].g0=graphData.x;
        vertices[base+j].g1=graphData.y;
        vertices[base+j].g2=graphData.z;
        vertices[base+j].g3=graphData.w;
    }
}
)GLSL";
inline constexpr const char* kCompactAllocationGLSL = R"GLSL(#version 450
layout(local_size_x=64) in;
struct Particle {vec4 positionAge,velocityLife,tint,size;};
struct Pair {vec4 a,b;};
layout(std430,set=0,binding=0) readonly buffer OldState {Particle oldState[];};
layout(std430,set=0,binding=1) readonly buffer OldParams {Pair oldParams[];};
layout(std430,set=0,binding=2) readonly buffer OldAttrs {Pair oldAttrs[];};
layout(std430,set=0,binding=3) readonly buffer OldOwners {uvec2 oldOwners[];};
layout(std430,set=0,binding=4) readonly buffer OldAlive {uint oldAlive[];};
layout(std430,set=0,binding=5) readonly buffer OldCounters {uint oldCounters[];};
layout(std430,set=0,binding=6) buffer NewState {Particle newState[];};
layout(std430,set=0,binding=7) buffer NewParams {Pair newParams[];};
layout(std140,set=0,binding=8) uniform Inputs {uvec4 u;};
layout(std430,set=0,binding=9) buffer NewAttrs {Pair newAttrs[];};
layout(std430,set=0,binding=10) buffer NewOwners {uvec2 newOwners[];};
layout(std430,set=0,binding=11) buffer NewAlive {uint newAlive[];};
layout(std430,set=0,binding=12) buffer NewFree {uint newFree[];};
layout(std430,set=0,binding=13) buffer NewCounters {uint newCounters[];};
void main(){
 uint id=gl_GlobalInvocationID.x,count=oldCounters[0];
 if(count>u.x||count>u.y||id>=u.y)return;
 if(id<count){
   uint slot=oldAlive[id];if(slot>=u.x)return;
   newState[id]=oldState[slot];newParams[id]=oldParams[slot];newAttrs[id]=oldAttrs[slot];newOwners[id]=oldOwners[slot];newAlive[id]=id;
 }else newFree[id-count]=u.y-1u-(id-count);
 if(id==0u){newCounters[0]=count;newCounters[1]=u.y-count;newCounters[2]=0u;newCounters[3]=0u;newCounters[4]=count;newCounters[5]=count;newCounters[6]=oldCounters[6];newCounters[7]=oldCounters[7];}
}
)GLSL";
inline constexpr const char* kProceduralBillboardGLSLVertex = R"GLSL(layout(std140) uniform U0 {mat4 u_viewProj;vec4 u_fog;vec4 u_camPos;vec4 u_params;vec4 u_right;vec4 u_up;};
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_size;
layout(location=2) in vec4 a_col;
out mediump vec2 v_uv;
out lowp vec4 v_col;
void main(){
 const vec2 corners[4]=vec2[4](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,1));
 const vec2 uv[4]=vec2[4](vec2(0,1),vec2(1,1),vec2(1,0),vec2(0,0));
 int j=gl_VertexID&3;
 vec3 at=a_pos+a_size.x*(u_right.xyz*corners[j].x+u_up.xyz*corners[j].y);
 gl_Position=u_viewProj*vec4(at,1.0);v_uv=uv[j];
 float f=1.0-exp(-length(at-u_camPos.xyz)*u_fog.w);
 vec4 c=a_col;c.rgb=mix(c.rgb,mix(u_fog.rgb*mix(1.0,c.a,u_params.y),vec3(1.0),u_params.z),f*(1.0-u_params.x));
 c.a*=1.0-f*u_params.x;v_col=c;
}
)GLSL";
inline constexpr const char* kProceduralBillboardGLSLFragment = R"GLSL(precision mediump float;
uniform lowp sampler2D tex0;
in mediump vec2 v_uv;
in lowp vec4 v_col;
out lowp vec4 o_color;
layout(std140) uniform U0 {mat4 u_viewProj;vec4 u_fog;vec4 u_camPos;vec4 u_params;vec4 u_right;vec4 u_up;};
void main(){
 vec4 c=texture(tex0,v_uv)*v_col;
#if AIVERSE_HDR
 if(u_params.y>0.5)c.rgb=rtsSrgbDecode(c.rgb/max(c.a,0.0001))*c.a;
 else c=rtsDisplay(c);
#endif
 o_color=c;
}
)GLSL";
} // namespace aiverse::spectra::shaders
