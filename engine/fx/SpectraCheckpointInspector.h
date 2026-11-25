// SpectraFX Stage 41 — read-only, bounded inspection of diagnostic V2 .spfx.
// No graphics Device, GPU readback, World, ECS or native game dependencies.
// This format is SAME-BUILD diagnostics, not a savegame or replay format.
#pragma once

#include "SpectraCheckpointExport.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace aiverse::spectra {

enum class GpuDiagnosticError : uint8_t {
    None, TruncatedHeader, TooLarge, BadMagic, UnsupportedVersion,
    InvalidHeader, InvalidSlotCount, PayloadLengthMismatch,
    TruncatedPayload, TrailingBytes, CrcMismatch
};

inline std::string_view gpuDiagnosticErrorString(GpuDiagnosticError code) {
    switch(code) {
    case GpuDiagnosticError::None:return "ok";
    case GpuDiagnosticError::TruncatedHeader:return "truncated V2 header";
    case GpuDiagnosticError::TooLarge:return "diagnostic exceeds maximum size";
    case GpuDiagnosticError::BadMagic:return "not an SPFX file";
    case GpuDiagnosticError::UnsupportedVersion:return "unsupported SPFX version (V2/V3 only)";
    case GpuDiagnosticError::InvalidHeader:return "invalid SPFX V2 header";
    case GpuDiagnosticError::InvalidSlotCount:return "invalid physical slot count";
    case GpuDiagnosticError::PayloadLengthMismatch:return "declared payload length does not match slot ABI";
    case GpuDiagnosticError::TruncatedPayload:return "truncated checkpoint payload";
    case GpuDiagnosticError::TrailingBytes:return "unexpected bytes after checkpoint payload";
    case GpuDiagnosticError::CrcMismatch:return "GPU checkpoint CRC32 mismatch";
    }
    return "unknown diagnostic error";
}

struct GpuDiagnosticInspection {
    GpuDiagnosticError error=GpuDiagnosticError::None;
    GpuCheckpointExportHeader header{};
    GpuCheckpointRngTrailer rng{};
    bool hasRngMetadata=false;
    uint32_t liveSlots=0, deadSlots=0;
    uint32_t nonFiniteLiveSlots=0;
    uint32_t invalidAgeSlots=0, invalidCpuLifetimeSlots=0;
    uint32_t mismatchedLifeSlots=0;
    uint32_t minLiveSlot=std::numeric_limits<uint32_t>::max();
    uint32_t maxLiveSlot=0;
    float minPosition[3]={},maxPosition[3]={};
    bool containsLivePositions=false;
    bool structurallyValid() const {return error==GpuDiagnosticError::None;}
    bool hasSemanticWarnings() const {
        return nonFiniteLiveSlots || invalidAgeSlots ||
               invalidCpuLifetimeSlots || mismatchedLifeSlots;
    }
};

// All byte lengths and bounds are verified BEFORE accessing slot buffers.
// Output is only committed on success (or a specific parse error), never
// partially populated by reading an unchecked/misaligned byte span.
inline GpuDiagnosticInspection inspectGpuCheckpointDiagnostic(
    const uint8_t* data,size_t size) {
    GpuDiagnosticInspection out;
    if(!data || size<sizeof(GpuCheckpointExportHeader)) {
        out.error=GpuDiagnosticError::TruncatedHeader;return out;
    }
    if(size>kGpuCheckpointExportMaxBytes) {
        out.error=GpuDiagnosticError::TooLarge;return out;
    }
    std::memcpy(&out.header,data,sizeof(out.header));
    if(out.header.magic!=0x58465053u) {
        out.error=GpuDiagnosticError::BadMagic;return out;
    }
    if(out.header.version!=2&&out.header.version!=3) {
        out.error=GpuDiagnosticError::UnsupportedVersion;return out;
    }
    if(out.header.headerBytes!=sizeof(GpuCheckpointExportHeader) ||
       out.header.reserved!=0) {
        out.error=GpuDiagnosticError::InvalidHeader;return out;
    }
    const size_t slots=out.header.physicalSlots;
    if(slots==0 || slots>kGpuCheckpointExportMaxSlots) {
        out.error=GpuDiagnosticError::InvalidSlotCount;return out;
    }
    const size_t bytesPerSlot=sizeof(GpuCheckpointParticle)+sizeof(float);
    const size_t expected=slots*bytesPerSlot+(out.header.version==3?sizeof(GpuCheckpointRngTrailer):0);
    if(out.header.payloadBytes!=expected) {
        out.error=GpuDiagnosticError::PayloadLengthMismatch;return out;
    }
    const size_t total=sizeof(GpuCheckpointExportHeader)+expected;
    if(size<total) {
        out.error=GpuDiagnosticError::TruncatedPayload;return out;
    }
    if(size>total) {
        out.error=GpuDiagnosticError::TrailingBytes;return out;
    }
    if(gpuCheckpointPayloadCrc32(
        data+sizeof(GpuCheckpointExportHeader),expected)!=
        out.header.payloadCrc32) {
        out.error=GpuDiagnosticError::CrcMismatch;return out;
    }
    if(out.header.version==3){
        std::memcpy(&out.rng,data+size-sizeof(out.rng),sizeof(out.rng));
        if(out.rng.enabled>1){out.error=GpuDiagnosticError::InvalidHeader;return out;}
        out.hasRngMetadata=true;
    }
    const size_t particleStart=sizeof(GpuCheckpointExportHeader);
    const size_t lifetimeStart=particleStart+
                               slots*sizeof(GpuCheckpointParticle);
    auto finite3=[](float x,float y,float z) {
        return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);
    };
    for(uint32_t i=0;i<uint32_t(slots);++i) {
        GpuCheckpointParticle particle{};
        float cpuRemaining=0.f;
        std::memcpy(&particle,
                    data+particleStart+size_t(i)*sizeof(particle),
                    sizeof(particle));
        std::memcpy(&cpuRemaining,
                    data+lifetimeStart+size_t(i)*sizeof(float),
                    sizeof(float));
        // NaN age is neither >=0 nor <0; diagnose it instead of silently
        // counting a corrupt live slot as a healthy dead particle.
        if(!std::isfinite(particle.positionAge.w))
            ++out.invalidAgeSlots;
        const bool live=particle.positionAge.w>=0.f &&
                        cpuRemaining>0.f;
        if(!live) {
            ++out.deadSlots;
            if(!std::isfinite(cpuRemaining))
                ++out.invalidCpuLifetimeSlots;
            continue;
        }
        ++out.liveSlots;
        if(!std::isfinite(cpuRemaining))
            ++out.invalidCpuLifetimeSlots;
        const bool positionValid=finite3(particle.positionAge.x,
            particle.positionAge.y,particle.positionAge.z);
        bool finiteData=positionValid &&
            std::isfinite(particle.positionAge.w) &&
            finite3(particle.velocityLife.x,particle.velocityLife.y,
                    particle.velocityLife.z) &&
            std::isfinite(particle.velocityLife.w) &&
            finite3(particle.tint.x,particle.tint.y,particle.tint.z) &&
            std::isfinite(particle.tint.w) &&
            finite3(particle.size.x,particle.size.y,particle.size.z) &&
            std::isfinite(particle.size.w);
        for(float param:particle.programParams)
            finiteData=finiteData&&std::isfinite(param);
        for(float attr:particle.attributes)
            finiteData=finiteData&&std::isfinite(attr);
        if(!finiteData)++out.nonFiniteLiveSlots;
        if(!std::isfinite(particle.positionAge.w) ||
           !std::isfinite(particle.velocityLife.w) ||
           particle.velocityLife.w<=0.f ||
           particle.positionAge.w>particle.velocityLife.w)
            ++out.invalidAgeSlots;
        if(particle.positionAge.w>=particle.velocityLife.w ||
           cpuRemaining>particle.velocityLife.w+0.01f)
            ++out.mismatchedLifeSlots;
        out.minLiveSlot=std::min(out.minLiveSlot,i);
        out.maxLiveSlot=std::max(out.maxLiveSlot,i);
        if(positionValid) {
            const float pos[3]={particle.positionAge.x,
                                particle.positionAge.y,
                                particle.positionAge.z};
            for(int axis=0;axis<3;++axis) {
                if(!out.containsLivePositions)
                    out.minPosition[axis]=out.maxPosition[axis]=pos[axis];
                else {
                    out.minPosition[axis]=std::min(out.minPosition[axis],pos[axis]);
                    out.maxPosition[axis]=std::max(out.maxPosition[axis],pos[axis]);
                }
            }
            out.containsLivePositions=true;
        }
    }
    return out;
}

inline GpuDiagnosticInspection inspectGpuCheckpointDiagnostic(
    const std::vector<uint8_t>& data) {
    return inspectGpuCheckpointDiagnostic(data.data(),data.size());
}

} // namespace aiverse::spectra
