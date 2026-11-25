#pragma once
#include "WorldAssets.h"
#include <cstdint>
#include <atomic>

namespace aiverse::world {

// Optional explicit namespace for additive scene collision. Each slot reserves
// 65,536 possible collider IDs, with slot N based at 0x80000000 + N*65536.
// The host's SceneCollisionNamespaceCatalog allocates non-overlapping ranges.
// A zero slotCount selects the original standalone/legacy behavior.
struct CollisionIdRange {
    uint32_t firstSlot=0;
    uint32_t slotCount=0;
    constexpr bool valid() const {
        return slotCount>0 && firstSlot<32768 &&
               uint64_t(firstSlot)+slotCount<=32768;
    }
};
class CollisionStream {
public:
    // Existing three-argument constructor remains source/behavior compatible.
    // Explicit ranges are assigned before streaming and never depend on I/O
    // completion order. Caller must hold the namespace lease for this lifetime.
    CollisionStream(const Json& manifest,AssetReader reader,SpatialWorld& target,
                    CollisionIdRange idRange={});
    ~CollisionStream();
    // Must be called before advancing a simulation tick. False means wait, never simulate holes.
    bool prepareRegions(std::span<const Bounds> regions,size_t requests=4,size_t publications=4);
    // Pure preflight for simultaneously pinned gameplay collision regions.
    // Returns false immediately when their reservations cannot fit at once;
    // waiting/retrying async I/O cannot resolve an impossible budget.
    bool canFitRegions(std::span<const Bounds> regions,
                       std::string* error=nullptr)const;

    bool covers(Bounds region)const;
    // Derived collision geometry epoch: increments only when chunk collision is
    // published or retired. Never put this host-local value in a game checksum.
    uint64_t revision() const { return revision_; }
    const AsyncChunkStream::Status& status()const{return stream_->status();}
    const WorldAssets& assets()const{return assets_;}
    bool contentIntegrityFailed()const noexcept{return contentIntegrityFailed_.load();}
private:
    struct Prepared final:AsyncChunkStream::Resource { ChunkCollision collision; };
    WorldAssets assets_;
    SpatialWorld& target_;
    std::map<Cell,std::vector<uint32_t>> ids_;
    std::map<Cell,TerrainTile> terrain_;
    uint64_t revision_=0;
    std::atomic<bool> contentIntegrityFailed_{false};
    std::unique_ptr<AsyncChunkStream> stream_;
    bool publish(Cell,const AsyncChunkStream::Payload&);
    void retire(Cell);
};
}
