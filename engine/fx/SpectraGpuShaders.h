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
    uint4 meta;             // x: 0 spawn, 1 integrate, 2 host gather, 3 reset
                            // 4 atomic compact, 5 GPU-authored indirect args
                            // 6 scatter-kill by physical slot IDs
                            // y: capacity; z: births; w: host survivor count
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
uint packColor(float4 c) {
    uint4 bytes=uint4(clamp(c,0.0f,1.0f)*255.0f+0.5f);
    return bytes.x | (bytes.y<<8) | (bytes.z<<16) | (bytes.w<<24);
}
kernel void cs_main(uint2 gid [[thread_position_in_grid]],
                     device Particle* particles [[buffer(0)]],
                     device const Particle* spawns [[buffer(1)]],
                     device const uint* slots [[buffer(2)]],
                     device Vertex* vertices [[buffer(3)]],
                     device const uint* liveSlots [[buffer(4)]],
                     device atomic_uint* aliveCounter [[buffer(5)]],
                     device DrawArgs* gpuDrawArgs [[buffer(6)]],
                     device const VmParams* seedParams [[buffer(7)]],
                     constant Uniforms& u [[buffer(8)]],
                     device VmParams* stateParams [[buffer(9)]],
                     device const VmPayload* vmProgram [[buffer(10)]],
                     device VmAttributes* stateAttributes [[buffer(11)]],
                     device atomic_uint* vmErrors [[buffer(12)]],
                     device const uint* graphSources [[buffer(13)]],
                      device const uint* killSlots [[buffer(14)]]) {
    uint id=gid.x;
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
            gpuDrawArgs[0].indexCount=alive*6u;
            gpuDrawArgs[0].instanceCount=1u;
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
        p.velocityLife.y+=u.gravityDt.x*dt;
        p.velocityLife.xyz*=max(0.0f,1.0f-u.gravityDt.z*dt);
        p.positionAge.xyz+=p.velocityLife.xyz*dt;
        p.positionAge.w+=dt;
        if(p.positionAge.w>=p.velocityLife.w)p.positionAge.w=-1.0f;
        else {
            VmParams params=stateParams[id];
            VmAttributes attrs=stateAttributes[id];
            vmRun(vmProgram[0],1u,p,params,attrs,dt,vmErrors);
            stateAttributes[id]=attrs;
        }
        particles[id]=p;
        return;
    }
    if(u.meta.x!=2u && u.meta.x!=4u)return;
    if(u.meta.x==2u && id>=u.meta.w)return;
    if(id>=u.meta.y)return;
    // Mode 2 uses the CPU's live map; mode 4 compacts entirely on GPU.
    uint slot=u.meta.x==2u ? liveSlots[id] : id;
    if(slot>=u.meta.y)return;
    Particle p=particles[slot];
    uint ordinal=id;
    if(u.meta.x==4u) {
        if(p.positionAge.w<0.0f||p.velocityLife.w<=0.0f)return;
        ordinal=atomic_fetch_add_explicit(&aliveCounter[0],1u,memory_order_relaxed);
        if(ordinal>=u.meta.y)return;
    }
    uint base=ordinal*4u;
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
    VmAttributes attributes=stateAttributes[slot];
    VmParams sourceParams=stateParams[slot];
    float4 graphData=float4(
        materialSource(graphSources[0],p,sourceParams,attributes),
        materialSource(graphSources[1],p,sourceParams,attributes),
        materialSource(graphSources[2],p,sourceParams,attributes),
        materialSource(graphSources[3],p,sourceParams,attributes));
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
layout(std430,binding=4) readonly buffer LiveSlots { uint liveSlots[]; };
layout(std430,binding=14) readonly buffer KillSlots { uint killSlots[]; };
layout(std430,binding=5) buffer AliveCounter { uint aliveCounter; };
layout(std430,binding=6) buffer IndirectArgs {
    uint indexCount, instanceCount, firstIndex;
    int baseVertex;
    uint firstInstance;
};
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
    if(u.meta.x==6u) {
        if(id<u.meta.z && killSlots[id]<u.meta.y)
            particles[killSlots[id]].positionAge.w=-1.0;
        return;
    }
    if(u.meta.x==0u) {
        if(id<u.meta.z && slots[id]<u.meta.y) particles[slots[id]]=spawns[id];
        return;
    }
    if(u.meta.x==3u) {if(id==0u)aliveCounter=0u;return;}
    if(u.meta.x==5u) {
        if(id==0u) {
            indexCount=min(aliveCounter,u.meta.y)*6u;
            instanceCount=1u;firstIndex=0u;baseVertex=0;firstInstance=0u;
        }
        return;
    }
    if(u.meta.x==1u) {
        if(id>=u.meta.y)return;
        Particle p=particles[id];
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
    if(u.meta.x!=2u && u.meta.x!=4u)return;
    if(u.meta.x==2u && id>=u.meta.w)return;
    if(id>=u.meta.y)return;
    uint slot=u.meta.x==2u?liveSlots[id]:id;
    if(slot>=u.meta.y)return;
    Particle p=particles[slot];
    uint ordinal=id;
    if(u.meta.x==4u) {
        if(p.positionAge.w<0.0||p.velocityLife.w<=0.0)return;
        ordinal=atomicAdd(aliveCounter,1u);
        if(ordinal>=u.meta.y)return;
    }
    uint base=ordinal*4u;
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
