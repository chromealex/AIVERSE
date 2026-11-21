// LumaWeave immutable GPU-facing source packet and cluster CSR contract.
// No gfx::Device ownership: the renderer uploads this snapshot only after the
// source registry/cluster revision matches, outside render/compute encoders.
// Direct-light energy is kept separate from the GI bounce atlas.
#pragma once
#include "LumaWeaveClusters.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct LumaGpuLight {
    float4 positionRadius{}; // xyz world, w = finite radius
    float4 colorIntensity{}; // linear rgb, w = intensity; NOT pre-exposed or baked
    float4 directionCone{};  // xyz, w = spot cosOuter, -2 means omni point
    float4 settings{};       // x = cosInner, y = importanceBias, z = shadowEnabled, w reserved
    std::array<uint32_t,4> identity{}; // key low/high, source kind, contributesToGi flag
};
static_assert(sizeof(LumaGpuLight)==80, "LumaWeave GPU light buffer ABI changed");

struct LumaGpuReceiver {
    float4 worldPosition{}; // xyz, w = isValid (0 or 1)
    float4 normal{};        // xyz geometric normal; w = reserved
};
static_assert(sizeof(LumaGpuReceiver)==32, "LumaWeave GPU receiver buffer ABI changed");

// Uniform layout for a future Metal direct-light resolve. Do not add std140
// array padding by assuming vec3/float3 is tightly packed.
struct LumaGpuGridUniforms {
    float4 origin{};     // xyz world grid corner
    float4 cellSize{};   // xyz cell dimensions
    std::array<uint32_t,4> dims{}; // nx, ny, nz, total cluster count
    std::array<uint32_t,4> limits{}; // source count, reference count, receivers, samples
};
static_assert(sizeof(LumaGpuGridUniforms)==64);

struct LumaGpuInputPacket {
    uint64_t sourceRevision=0;
    uint64_t clusterGeneration=0; // view/camera origin may change with sources unchanged
    std::vector<LumaGpuLight> lights;             // sorted by stable LumaLight::key
    std::vector<LumaGpuClusterEntry> cells;        // 16-byte entries
    std::vector<uint32_t> candidateIndices;       // sorted registry indices
    LumaGpuGridUniforms uniforms{};
    size_t totalBytes() const {
        return lights.size()*sizeof(LumaGpuLight)+
               cells.size()*sizeof(LumaGpuClusterEntry)+
               candidateIndices.size()*sizeof(uint32_t)+sizeof(uniforms);
    }
};

inline LumaGpuInputPacket packLumaSources(const LumaWeaveRegistry& registry,
                                         const LumaWeaveClusters& clusters) {
    const auto csr=clusters.exportCsr(registry); // validates snapshot revision
    LumaGpuInputPacket packet;
    packet.sourceRevision=registry.revision();
    packet.clusterGeneration=csr.buildGeneration;
    packet.cells=csr.cells;
    packet.candidateIndices=csr.indices;
    packet.lights.reserve(registry.lights().size());
    for(const auto& light:registry.lights()) {
        if(!lumaValidLight(light))
            throw std::invalid_argument("invalid published LumaWeave light");
        packet.lights.push_back({
            {light.position.x,light.position.y,light.position.z,light.radius},
            {light.color.x,light.color.y,light.color.z,light.intensity},
            {light.direction.x,light.direction.y,light.direction.z,
             light.kind==LumaLightKind::Spot ? light.cosOuter : -2.0f},
            {light.cosInner,light.importanceBias,light.castsShadows?1.0f:0.0f,0.0f},
            {uint32_t(light.key),uint32_t(light.key>>32),
             uint32_t(light.kind),light.contributesToGi?1u:0u}
        });
    }
    const auto& c=csr.config;
    packet.uniforms.origin={c.origin.x,c.origin.y,c.origin.z,0};
    packet.uniforms.cellSize={c.cellSize.x,c.cellSize.y,c.cellSize.z,0};
    packet.uniforms.dims={uint32_t(c.nx),uint32_t(c.ny),uint32_t(c.nz),
                          uint32_t(packet.cells.size())};
    packet.uniforms.limits={uint32_t(packet.lights.size()),
                            uint32_t(packet.candidateIndices.size()),0,0};
    for(const auto& cell:packet.cells) {
        if(size_t(cell.first)+cell.count>packet.candidateIndices.size())
            throw std::logic_error("invalid LumaWeave packed cluster range");
    }
    for(uint32_t index:packet.candidateIndices)
        if(index>=packet.lights.size())
            throw std::logic_error("LumaWeave cluster references missing source");
    return packet;
}

} // namespace aiverse::render
