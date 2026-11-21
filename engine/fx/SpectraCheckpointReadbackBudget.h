// SpectraFX Stage 32 — bounded Metal checkpoint readback reservations.
// Presentation-only. One budget is shared by all GPU emitters using the same
// graphics Device. The host owns the budget policy; no authoritative ECS state.
// All methods must be called on the owning graphics Device thread.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace aiverse::spectra {

struct CheckpointReadbackLimits {
    size_t maxBytesPerFrame = 512u * 1024u;
    size_t maxBytesInFlight = 1536u * 1024u;
    uint32_t maxChunksPerFrame = 16;
    // Metal currently admits 32 total pending range reads. Keep 8 slots
    // available for other diagnostics and reject rather than overbooking.
    uint32_t maxChunksInFlight = 24;
};

struct CheckpointReadbackStats {
    uint64_t frameSerial = 0;
    size_t frameBytesReserved = 0, bytesInFlight = 0;
    size_t bytesReservedTotal = 0, bytesReturnedTotal = 0;
    uint32_t frameChunksReserved = 0, chunksInFlight = 0;
    uint32_t requestsInFlight = 0;
    uint64_t accepted = 0, denied = 0;
};

// A reservation is held until ALL accepted RHI callbacks have been delivered
// or their callback closures have been destroyed. This counts bandwidth
// reservations, not GPU completion fences or measured PCIe/UMA traffic.
class CheckpointReadbackBudget final {
public:
    explicit CheckpointReadbackBudget(CheckpointReadbackLimits limits = {})
        : limits_(limits) {
        limits_.maxChunksInFlight =
            std::min<uint32_t>(limits_.maxChunksInFlight,24u);
    }

    bool tryReserve(uint64_t frameSerial,size_t bytes,uint32_t chunks) {
        if(frameSerial<stats_.frameSerial) { ++stats_.denied; return false; }
        if(frameSerial!=stats_.frameSerial) {
            stats_.frameSerial=frameSerial;
            stats_.frameBytesReserved=0;
            stats_.frameChunksReserved=0;
        }
        if(bytes==0 || chunks==0 ||
           bytes>limits_.maxBytesPerFrame ||
           bytes>limits_.maxBytesInFlight ||
           chunks>limits_.maxChunksPerFrame ||
           chunks>limits_.maxChunksInFlight ||
           stats_.frameBytesReserved>limits_.maxBytesPerFrame-bytes ||
           stats_.bytesInFlight>limits_.maxBytesInFlight-bytes ||
           stats_.frameChunksReserved>limits_.maxChunksPerFrame-chunks ||
           stats_.chunksInFlight>limits_.maxChunksInFlight-chunks) {
            ++stats_.denied;
            return false;
        }
        stats_.frameBytesReserved+=bytes;
        stats_.frameChunksReserved+=chunks;
        stats_.bytesInFlight+=bytes;
        stats_.chunksInFlight+=chunks;
        stats_.bytesReservedTotal+=bytes;
        ++stats_.requestsInFlight;
        ++stats_.accepted;
        return true;
    }

    void settle(size_t reservedBytes,uint32_t reservedChunks,
                size_t returnedBytes) {
        stats_.bytesInFlight-=std::min(stats_.bytesInFlight,reservedBytes);
        stats_.chunksInFlight-=std::min(stats_.chunksInFlight,reservedChunks);
        if(stats_.requestsInFlight>0)--stats_.requestsInFlight;
        stats_.bytesReturnedTotal+=std::min(returnedBytes,reservedBytes);
    }

    const CheckpointReadbackLimits& limits() const {return limits_;}
    const CheckpointReadbackStats& stats() const {return stats_;}

private:
    CheckpointReadbackLimits limits_{};
    CheckpointReadbackStats stats_{};
};

} // namespace aiverse::spectra
