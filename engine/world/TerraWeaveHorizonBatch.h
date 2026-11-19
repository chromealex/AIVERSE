// TerraWeave Horizon offline bake API. Used by native project tooling and
// engine build pipelines; no graphics device, gameplay ECS, or filesystem.
#pragma once
#include "engine/world/TerraWeaveHorizonArchive.h"
#include "engine/render/TerraWeaveHorizonClip.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::terraweave {

// Half-open camera *region keys*, not arbitrary world-space points.
// Region key (x,z) maps to the runtime HorizonScene work item and filename
// "<x>_<z>.twh". Negative coordinates are supported without rounding drift.
struct HorizonBatchBounds {
    int32_t minCellX=0, minCellZ=0, maxCellX=0, maxCellZ=0;
};
struct HorizonBatchOptions {
    HorizonBatchBounds regions;
    size_t maxRegions=4096;
    uint64_t authoredContentHash=0; // required for nonprocedural sources
    bool skipExisting=true;
};
struct HorizonBatchProgress {
    size_t total=0, visited=0, generated=0, skipped=0;
    size_t bytesWritten=0;
    int32_t lastCellX=0, lastCellZ=0;
    bool canceled=false;
};
struct HorizonBatchStorage {
    // Read only when skipExisting is enabled. A stale or malformed blob
    // causes a fresh bake. Callbacks are synchronous on the tool's CPU thread.
    std::function<bool(int32_t,int32_t,std::vector<uint8_t>&)> read;
    // The external build system provides atomic/temporary-file semantics.
    // Throw on write failure; build must not report a partial success.
    std::function<void(int32_t,int32_t,const std::vector<uint8_t>&)> write;
    // false cancels cleanly *before* starting the next region.
    std::function<bool(const HorizonBatchProgress&)> continueBake;
};
inline int64_t horizonRegionCenterCm(int32_t key,int32_t middleTileCm) {
    if(middleTileCm<=0)throw std::invalid_argument("TerraWeave Horizon region spacing");
    return int64_t(key)*middleTileCm+middleTileCm/2;
}

inline HorizonBatchProgress bakeHorizonBatch(
        const TerrainSettings& terrain,
        const BiomeSettings& biomes,
        const HorizonSettings& settings,
        const HorizonBatchOptions& options,
        const HorizonBatchStorage& storage,
        const HorizonSurfaceSources* authored = nullptr) {
    validateHorizonSettings(settings);
    const auto& b=options.regions;
    if(!storage.write)
        throw std::invalid_argument("TerraWeave Horizon missing asset writer");
    if(authored && !options.authoredContentHash)
        throw std::invalid_argument("TerraWeave Horizon authored archive requires content hash");
    if(b.maxCellX<=b.minCellX||b.maxCellZ<=b.minCellZ ||
       options.maxRegions==0||options.maxRegions>100000)
        throw std::invalid_argument("TerraWeave Horizon invalid batch bounds");
    const uint64_t cols=uint64_t(int64_t(b.maxCellX)-b.minCellX);
    const uint64_t rows=uint64_t(int64_t(b.maxCellZ)-b.minCellZ);
    if(cols>options.maxRegions||rows>options.maxRegions||
       cols*rows>options.maxRegions)
        throw std::invalid_argument("TerraWeave Horizon batch exceeds region budget");
    const uint64_t fingerprint=horizonArchiveFingerprint(
        terrain,biomes,settings,options.authoredContentHash);
    HorizonBatchProgress status;
    status.total=static_cast<size_t>(cols*rows);
    // Deterministic z-major then x-major ordering. Geometry generation is
    // independent of CPU scheduling and no global scratch atlas is retained.
    for(int64_t z=b.minCellZ;z<b.maxCellZ;++z) {
        for(int64_t x=b.minCellX;x<b.maxCellX;++x) {
            status.lastCellX=static_cast<int32_t>(x);
            status.lastCellZ=static_cast<int32_t>(z);
            if(storage.continueBake && !storage.continueBake(status)) {
                status.canceled=true;
                return status;
            }
            const int64_t wx=horizonRegionCenterCm(
                static_cast<int32_t>(x),settings.middleTileCm);
            const int64_t wz=horizonRegionCenterCm(
                static_cast<int32_t>(z),settings.middleTileCm);
            bool exists=false;
            if(options.skipExisting && storage.read) {
                try {
                    std::vector<uint8_t> bytes;
                    if(storage.read(static_cast<int32_t>(x),
                                    static_cast<int32_t>(z),bytes)) {
                        const auto prior=decodeHorizonArchive(bytes,fingerprint);
                        exists=prior.cameraXcm==wx && prior.cameraZcm==wz;
                    }
                } catch(const std::exception&) {
                    exists=false; // corrupt/stale archive must be regenerated
                }
            }
            if(exists) {
                ++status.skipped;
            } else {
                auto baked=bakeHorizon(terrain,biomes,settings,wx,wz,authored);
                populateHorizonNearSeamSamples(baked,authored);
                const auto bytes=encodeHorizonArchive(baked,fingerprint);
                storage.write(static_cast<int32_t>(x),
                              static_cast<int32_t>(z),bytes);
                ++status.generated;
                status.bytesWritten+=bytes.size();
            }
            ++status.visited;
        }
    }
    return status;
}

} // namespace aiverse::terraweave
