// AIVERSE SpectraFX — Stage 28 GPU checkpoint format.
// Presentation-only data, NEVER part of an authoritative ECS save/rollback.
// Captures raw GPU particle state and VM sidecar registers without converting
// to the legacy CPU ParticleSystem (which has no state import API).
#pragma once
#include "engine/render/Math3D.h"
#include <array>
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <vector>

namespace aiverse::spectra {

struct GpuCheckpointParticle {
    render::float4 positionAge{}, velocityLife{}, tint{}, size{};
    std::array<float,8> programParams{};
    std::array<float,8> attributes{};
};
static_assert(sizeof(GpuCheckpointParticle)==128);

struct GpuCheckpoint {
    // The physical slot index of each item is preserved; dead slots have
    // positionAge.w < 0 and must be ignored by consumers.
    std::vector<GpuCheckpointParticle> slots;
    // CPU lifespan mirror captured with the GPU submission. It is NOT a GPU
    // position snapshot and may differ from GPU remaining lifetime estimates.
    std::vector<float> remainingLifetime;
    uint64_t frameSerial=0;
    uint32_t shaderBirthSeed=0, shaderBirthOrdinal=0;
    bool shaderBirthSeedEnabled=false;
    // In-memory restore only; diagnostic SPFX does not serialize ownership.
    std::vector<uint32_t> freeSlotOrder;
    std::vector<uint64_t> sourceTokens;
    std::array<float,8> birthParameters{};
    bool hostStateComplete=false;
    bool complete=false; // all required GPU buffers were read successfully
    bool gpuOwnedAllocation=false;
    std::vector<uint32_t> aliveSlotOrder;
};
inline bool validOwnedGpuCheckpoint(const GpuCheckpoint& c){
    const size_t n=c.slots.size();
    if(!c.gpuOwnedAllocation||!n||c.remainingLifetime.size()!=n||c.sourceTokens.size()!=n||
       c.freeSlotOrder.size()>n||c.aliveSlotOrder.size()!=n-c.freeSlotOrder.size())return false;
    std::vector<uint8_t> seen(n);
    for(uint32_t slot:c.freeSlotOrder){
        if(slot>=n||seen[slot]++||c.remainingLifetime[slot]>0.f||c.sourceTokens[slot]||
           !(c.slots[slot].positionAge.w<0.f)||!std::isfinite(c.remainingLifetime[slot]))return false;
    }
    const auto finite4=[](render::float4 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);};
    for(uint32_t slot:c.aliveSlotOrder){
        if(slot>=n||seen[slot]++)return false;
        const auto& p=c.slots[slot];
        if(!finite4(p.positionAge)||!finite4(p.velocityLife)||!finite4(p.tint)||!finite4(p.size)||
           p.positionAge.w<0.f||p.velocityLife.w<=p.positionAge.w||p.size.x<0.f||
           c.remainingLifetime[slot]!=p.velocityLife.w-p.positionAge.w)return false;
        for(float v:p.programParams)if(!std::isfinite(v))return false;
        for(float v:p.attributes)if(!std::isfinite(v))return false;
    }
    for(float v:c.birthParameters)if(!std::isfinite(v))return false;
    return true;
}
} // namespace aiverse::spectra
