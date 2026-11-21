// AIVERSE SpectraFX — Stage 28 GPU checkpoint format.
// Presentation-only data, NEVER part of an authoritative ECS save/rollback.
// Captures raw GPU particle state and VM sidecar registers without converting
// to the legacy CPU ParticleSystem (which has no state import API).
#pragma once
#include "engine/render/Math3D.h"
#include <array>
#include <cstdint>
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
    bool complete=false; // all three GPU buffers were read successfully
};
} // namespace aiverse::spectra
