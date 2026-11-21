// SpectraFX Stage 23: experimental Metal interpreter for Stage 22 GPU VM ABI.
// NOT wired into the production GraphRuntime/ComputeParticles pipeline.
// The host must upload a fully validated gpu_program::Payload and immutable
// per-dispatch params; errors are counted on GPU. No gameplay ECS access.
#pragma once
namespace aiverse::spectra::gpu_program {
inline constexpr const char* kMetalVmKernel = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VMInstruction { uint opcode, field; float scalar; uint reserved; };
struct VMStatement {
    uint stage, action, target, firstInstruction, instructionCount;
    uint reserved0, reserved1, reserved2;
};
struct VMPayload {
    uint abiVersion, statementCount, instructionCount, parameterMask;
    VMStatement statements[64];
    VMInstruction instructions[512];
};
struct VMParticle {
    float4 positionAge;
    float4 velocityLife;
    float4 tint;
    float4 size;
};
struct VMParams { float values[8]; };
struct VMParticleParams {
    float values[8];
    uint sourceSlot, generation, active, reserved;
};
struct VMDispatch {
    uint particleCount;
    uint stage; // 0 Spawn or 1 Update
    float dt;
    uint reserved;
};
bool vmFinite(float v) { return isfinite(v) && abs(v) <= 10000000.0f; }
float vmRead(uint field, VMParticle p, VMParams params,
             float dt) {
    switch(field) {
        case 0: return p.positionAge.w;
        case 1: return p.velocityLife.w>0.0f
            ? p.positionAge.w/p.velocityLife.w : 0.0f;
        case 2: return dt;
        case 4: return p.positionAge.x;
        case 5: return p.positionAge.y;
        case 6: return p.positionAge.z;
        case 7: return p.velocityLife.x;
        case 8: return p.velocityLife.y;
        case 9: return p.velocityLife.z;
        case 10: return p.size.x;
        case 12: return p.tint.x;
        case 13: return p.tint.y;
        case 14: return p.tint.z;
        case 15: return p.tint.w;
        default: return (field>=24u && field<=31u)
            ? params.values[field-24u] : 0.0f;
    }
}
void vmWrite(uint field, float v, thread VMParticle& p) {
    switch(field) {
        case 4: p.positionAge.x=v; break;
        case 5: p.positionAge.y=v; break;
        case 6: p.positionAge.z=v; break;
        case 7: p.velocityLife.x=v; break;
        case 8: p.velocityLife.y=v; break;
        case 9: p.velocityLife.z=v; break;
        case 10: p.size.x=max(0.0f,v); break;
        case 12: p.tint.x=clamp(v,0.0f,1.0f); break;
        case 13: p.tint.y=clamp(v,0.0f,1.0f); break;
        case 14: p.tint.z=clamp(v,0.0f,1.0f); break;
        case 15: p.tint.w=clamp(v,0.0f,1.0f); break;
    }
}
bool vmEvaluate(const device VMPayload& code, VMStatement st,
                VMParticle p, VMParams params,
                float dt, thread float& result) {
    float stack[128];
    uint used=0u;
    if(st.instructionCount==0u || st.instructionCount>128u ||
       st.firstInstruction>code.instructionCount ||
       st.instructionCount>code.instructionCount-st.firstInstruction)
        return false;
    for(uint i=0u;i<st.instructionCount;++i) {
        VMInstruction op=code.instructions[st.firstInstruction+i];
        if(op.opcode==0u || op.opcode==1u) {
            if(used>=128u)return false;
            float v=op.opcode==0u ? op.scalar : vmRead(op.field,p,params,dt);
            if(!vmFinite(v))return false;
            stack[used++]=v;
            continue;
        }
        bool unary=op.opcode==11u || op.opcode==12u || op.opcode==16u;
        bool ternary=op.opcode==17u || op.opcode==18u;
        uint needed=unary?1u:(ternary?3u:2u);
        if(used<needed)return false;
        float t=0.0f,b=0.0f,a=0.0f;
        if(ternary)t=stack[--used];
        if(!unary)b=stack[--used];
        a=stack[--used];
        float value=0.0f;
        switch(op.opcode) {
            case 2: value=a+b; break;
            case 3: value=a-b; break;
            case 4: value=a*b; break;
            case 6: value=min(a,b); break;
            case 7: value=max(a,b); break;
            case 11: value=abs(a); break;
            case 12: value=-a; break;
            case 16: value=clamp(a,0.0f,1.0f); break;
            case 17:
                if(b>t)return false;
                value=clamp(a,b,t);break;
            case 18: value=a+(b-a)*clamp(t,0.0f,1.0f);break;
            default:return false;
        }
        if(!vmFinite(value))return false;
        stack[used++]=value;
    }
    if(used!=1u || !vmFinite(stack[0]))return false;
    result=stack[0];return true;
}
kernel void cs_main(uint tid [[thread_position_in_grid]],
                    device VMParticle* particles [[buffer(0)]],
                    const device VMPayload& code [[buffer(1)]],
                    constant VMParams& params [[buffer(2)]],
                    device atomic_uint* errorCount [[buffer(3)]],
                    device const VMParticleParams* snapshots [[buffer(4)]],
                    constant VMDispatch& dispatch [[buffer(8)]]) {
    if(tid>=dispatch.particleCount)return;
    if(code.abiVersion!=1u || code.statementCount>64u ||
       code.instructionCount>512u || dispatch.stage>1u ||
       !vmFinite(dispatch.dt) || dispatch.dt<0.0f) {
        atomic_fetch_add_explicit(errorCount,1u,memory_order_relaxed);
        return;
    }
    VMParticle p=particles[tid];
    if(p.positionAge.w<0.0f)return;
    VMParams effective=params;
    if(dispatch.reserved==1u) {
        VMParticleParams snapshot=snapshots[tid];
        if(snapshot.active==0u || snapshot.generation==0u)return;
        // Spawn is exactly once per newly allocated physical particle.
        if(dispatch.stage==0u && snapshot.reserved!=1u)return;
        for(uint k=0u;k<8u;++k)effective.values[k]=snapshot.values[k];
    } else if(dispatch.reserved!=0u) {
        atomic_fetch_add_explicit(errorCount,1u,memory_order_relaxed);
        return;
    }
    // Transactional per-particle semantics: commit only after every statement
    // has evaluated; malformed payloads never expose partial state updates.
    for(uint i=0u;i<code.statementCount;++i) {
        VMStatement st=code.statements[i];
        if(st.stage!=dispatch.stage)continue;
        float value=0.0f;
        if(st.target<4u || st.target==11u || st.target>15u ||
           st.action>3u ||
           !vmEvaluate(code,st,p,effective,dispatch.dt,value)) {
            atomic_fetch_add_explicit(errorCount,1u,memory_order_relaxed);
            return;
        }
        float prior=vmRead(st.target,p,effective,dispatch.dt);
        if(st.action==1u)value=prior+value;
        else if(st.action==2u)value=prior+value*dispatch.dt;
        else if(st.action==3u)value=prior*value;
        if(!vmFinite(value)) {
            atomic_fetch_add_explicit(errorCount,1u,memory_order_relaxed);
            return;
        }
        vmWrite(st.target,value,p);
    }
    particles[tid]=p;
}
)MSL";
} // namespace aiverse::spectra::gpu_program
