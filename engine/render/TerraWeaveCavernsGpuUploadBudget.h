#pragma once
// TerraWeave Caverns Stage38: bounded immutable GPU upload admission.
// This limits approximate source bytes + number of whole-shard uploads at
// an owner frame boundary. It does NOT claim a GPU time budget or schedule
// a partially uploaded GeometryScene; loadPrepared remains synchronous.
#include "engine/render/TerraWeaveCavernsBatchGeometry.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveGpuUploadLimits {
    uint64_t maxSourceBytesPerFrame=32ull*1024*1024;
    uint32_t maxShardsPerFrame=1;
    uint32_t maxRetiredScenes=64;
    uint64_t maxLiveSourceBytes=512ull*1024*1024;
    uint32_t maxLiveScenes=256;
};
inline void validateCaveGpuUploadLimits(CaveGpuUploadLimits limits){
    if(!limits.maxSourceBytesPerFrame||
       limits.maxSourceBytesPerFrame>2ull*1024*1024*1024||
       !limits.maxShardsPerFrame||limits.maxShardsPerFrame>64||
       !limits.maxRetiredScenes||limits.maxRetiredScenes>4096||
       !limits.maxLiveSourceBytes||limits.maxLiveSourceBytes>16ull*1024*1024*1024||
       !limits.maxLiveScenes||limits.maxLiveScenes>65536)
        throw std::invalid_argument("Caverns GPU upload budget");
}
inline uint64_t estimateCaveSourceBytes(
    const std::vector<CaveBatchChunk>& chunks) {
    uint64_t bytes=0;
    for(const auto& chunk:chunks){
        const uint64_t v=chunk.mesh.vertices.size();
        const uint64_t i=chunk.mesh.indices.size();
        const uint64_t stride=sizeof(render::ModelVertex);
        if(v>UINT64_MAX/stride || i>UINT64_MAX/sizeof(uint32_t))
            throw std::overflow_error("Caverns GPU source size overflow");
        const uint64_t count=v*stride+i*sizeof(uint32_t);
        if(bytes>UINT64_MAX-count)
            throw std::overflow_error("Caverns GPU source budget overflow");
        bytes+=count;
    }
    return bytes;
}
struct CaveGpuUploadStats {
    uint64_t frame=0,sourceBytes=0;
    uint32_t uploads=0;
    uint64_t refusedBytes=0,refusedPressure=0,refusedLiveBudget=0;
};
class CaveGpuUploadBudget {
public:
    explicit CaveGpuUploadBudget(CaveGpuUploadLimits limits={}):limits_(limits){
        validateCaveGpuUploadLimits(limits_);
    }
    bool admitLive(uint64_t currentBytes,size_t liveScenes,uint64_t candidateBytes){
        if(candidateBytes&&(liveScenes>=limits_.maxLiveScenes||
           currentBytes>limits_.maxLiveSourceBytes||
           candidateBytes>limits_.maxLiveSourceBytes-currentBytes)){
            ++stats_.refusedLiveBudget;return false;
        }
        return true; // empty retirement adds no GPU owner
    }
    // Reserve before GeometryScene::loadPrepared. Failed uploads still
    // consume the current frame's budget and can retry on a later frame.
    bool admit(uint64_t frame,uint64_t estimatedBytes,size_t retiredScenes){
        if(!frame || !estimatedBytes || estimatedBytes>limits_.maxSourceBytesPerFrame){
            ++stats_.refusedBytes;
            return false;
        }
        if(stats_.frame!=frame){
            stats_.frame=frame;stats_.sourceBytes=0;stats_.uploads=0;
        }
        if(retiredScenes>=limits_.maxRetiredScenes){
            ++stats_.refusedPressure;return false;
        }
        if(stats_.uploads>=limits_.maxShardsPerFrame ||
           estimatedBytes>limits_.maxSourceBytesPerFrame-stats_.sourceBytes){
            ++stats_.refusedBytes;return false;
        }
        stats_.sourceBytes+=estimatedBytes;
        ++stats_.uploads;
        return true;
    }
    // No new Model upload for empty scene retirement.
    bool admitEmpty(uint64_t frame,size_t retiredScenes){
        if(!frame)return false;
        if(stats_.frame!=frame){
            stats_.frame=frame;stats_.sourceBytes=0;stats_.uploads=0;
        }
        if(retiredScenes>=limits_.maxRetiredScenes){
            ++stats_.refusedPressure;return false;
        }
        if(stats_.uploads>=limits_.maxShardsPerFrame)return false;
        ++stats_.uploads;
        return true;
    }
    CaveGpuUploadStats stats()const noexcept{return stats_;}
    CaveGpuUploadLimits limits()const noexcept{return limits_;}
private:
    CaveGpuUploadLimits limits_;
    CaveGpuUploadStats stats_{};
};
} // namespace aiverse::terraweave::caverns
