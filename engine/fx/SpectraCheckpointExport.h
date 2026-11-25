// SpectraFX Stage 39 — bounded, *diagnostic-only* in-memory GPU checkpoint
// binary. This is NOT an ECS/savegame format or portable GPU serializer.
// Only current-revision, complete, kill-fence-safe captures may be exported.
#pragma once

#include "SpectraGpuCheckpoint.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace aiverse::spectra {

// Native build ABI, little-endian on current supported Metal platforms.
// V2 adds payload CRC32. This is diagnostic-only, not portable savegame IO.
struct GpuCheckpointExportHeader {
    uint32_t magic=0x58465053u; // little-endian "SPFX"
    uint32_t version=2;
    uint32_t headerBytes=48; // exact version-2 header, asserted below
    uint32_t physicalSlots=0;
    uint64_t capturedRevision=0;
    uint64_t gpuFrameSerial=0;
    uint64_t payloadBytes=0;
    uint32_t payloadCrc32=0;
    uint32_t reserved=0; // must be zero in V2
};
static_assert(sizeof(GpuCheckpointExportHeader)==48);
static_assert(std::is_trivially_copyable_v<GpuCheckpointExportHeader>);
static_assert(std::is_trivially_copyable_v<GpuCheckpointParticle>);

struct GpuCheckpointRngTrailer {
    uint32_t seed=0,ordinal=0,enabled=0;
};
static_assert(sizeof(GpuCheckpointRngTrailer)==12);
inline constexpr uint32_t kGpuCheckpointExportMaxSlots=4096;
// Stage 32 raw GPU capture: <=512 KiB; extra CPU lifespan mirror <=16 KiB
// plus 48-byte V2 diagnostic header.
inline constexpr size_t kGpuCheckpointExportMaxBytes=
    sizeof(GpuCheckpointExportHeader)+
    size_t(kGpuCheckpointExportMaxSlots)*
    (sizeof(GpuCheckpointParticle)+sizeof(float))+sizeof(GpuCheckpointRngTrailer);

// Table-free CRC32 (IEEE 802.3 polynomial) over the exact payload bytes.
// Implemented without platform-specific dependencies so a headless log
// reader can reject truncation/corruption before inspecting slot data.
inline uint32_t gpuCheckpointPayloadCrc32(const uint8_t* data,size_t size) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<size;++i) {
        crc^=uint32_t(data[i]);
        for(int bit=0;bit<8;++bit)
            crc=(crc>>1)^(crc&1u?0xedb88320u:0u);
    }
    return ~crc;
}

inline bool validateGpuCheckpointDiagnostic(
    const std::vector<uint8_t>& data,
    GpuCheckpointExportHeader* decodedHeader=nullptr) {
    if(data.size()<sizeof(GpuCheckpointExportHeader) ||
       data.size()>kGpuCheckpointExportMaxBytes)return false;
    GpuCheckpointExportHeader header{};
    std::memcpy(&header,data.data(),sizeof(header));
    if(header.magic!=0x58465053u || (header.version!=2 && header.version!=3) ||
       header.headerBytes!=sizeof(header) || header.reserved!=0 ||
       header.physicalSlots==0 ||
       header.physicalSlots>kGpuCheckpointExportMaxSlots)return false;
    const size_t expectedPayload=size_t(header.physicalSlots)*
        (sizeof(GpuCheckpointParticle)+sizeof(float))+(header.version==3?sizeof(GpuCheckpointRngTrailer):0);
    if(header.payloadBytes!=expectedPayload ||
       data.size()!=sizeof(header)+expectedPayload)return false;
    const uint32_t crc=gpuCheckpointPayloadCrc32(
        data.data()+sizeof(header),expectedPayload);
    if(crc!=header.payloadCrc32)return false;
    if(header.version==3){
        GpuCheckpointRngTrailer rng{};
        std::memcpy(&rng,data.data()+data.size()-sizeof(rng),sizeof(rng));
        if(rng.enabled>1)return false;
    }
    if(decodedHeader)*decodedHeader=header;
    return true;
}

// Must be called only AFTER a caller has checked the current revision,
// GPU health, no unsent births, no pending selective kills/fences, and an
// eligible completed checkpoint. Never read GPU memory in this helper.
inline bool encodeGpuCheckpointDiagnostic(const GpuCheckpoint& capture,
    uint64_t capturedRevision, std::vector<uint8_t>& output,
    size_t maxBytes=kGpuCheckpointExportMaxBytes) {
    const size_t slots=capture.slots.size();
    if(!capture.complete || slots==0 ||
       slots>kGpuCheckpointExportMaxSlots ||
       slots!=capture.remainingLifetime.size())return false;
    const size_t slotBytes=slots*sizeof(GpuCheckpointParticle);
    const size_t lifetimeBytes=slots*sizeof(float);
    const size_t rngBytes=capture.shaderBirthSeedEnabled?sizeof(GpuCheckpointRngTrailer):0;
    const size_t total=sizeof(GpuCheckpointExportHeader)+
                       slotBytes+lifetimeBytes+rngBytes;
    if(total>kGpuCheckpointExportMaxBytes || total>maxBytes ||
       slots>std::numeric_limits<uint32_t>::max())return false;
    GpuCheckpointExportHeader header;
    header.physicalSlots=uint32_t(slots);
    header.capturedRevision=capturedRevision;
    header.gpuFrameSerial=capture.frameSerial;
    header.version=rngBytes?3:2;
    header.payloadBytes=slotBytes+lifetimeBytes+rngBytes;
    std::vector<uint8_t> encoded(total);
    std::memcpy(encoded.data()+sizeof(header),
                capture.slots.data(),slotBytes);
    std::memcpy(encoded.data()+sizeof(header)+slotBytes,
                capture.remainingLifetime.data(),lifetimeBytes);
    if(rngBytes){
        const GpuCheckpointRngTrailer rng{capture.shaderBirthSeed,capture.shaderBirthOrdinal,1};
        std::memcpy(encoded.data()+sizeof(header)+slotBytes+lifetimeBytes,&rng,sizeof(rng));
    }
    header.payloadCrc32=gpuCheckpointPayloadCrc32(
        encoded.data()+sizeof(header),header.payloadBytes);
    std::memcpy(encoded.data(),&header,sizeof(header));
    output.swap(encoded); // output unchanged on any validation failure
    return true;
}

// Decode diagnostic metadata only; never mutate a running emitter.
inline bool decodeGpuCheckpointRng(const std::vector<uint8_t>& data,GpuCheckpointRngTrailer& out) {
    GpuCheckpointExportHeader header{};
    if(!validateGpuCheckpointDiagnostic(data,&header))return false;
    GpuCheckpointRngTrailer next{};
    if(header.version==3)std::memcpy(&next,data.data()+data.size()-sizeof(next),sizeof(next));
    out=next;return true;
}

} // namespace aiverse::spectra
