#pragma once
// TerraWeave Caverns Stage40: headless authoritative SDF projection.
// AIVERSE's net::Server is a lockstep input relay, NOT a simulation host.
// The actual authoritative/replay simulation owner may use this class to
// read the same immutable cave density without GPU, Box3D, or editor state.
// The owning ECS world remains the source of truth for authoring commands.
#include "engine/world/TerraWeaveCavernsSdfSpatialIndex.h"
#include "engine/world/TerraWeaveCavernsRaycast.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::terraweave::caverns {
class CaveHeadlessWorldSource {
public:
    bool load(std::shared_ptr<const CaveVolumeSnapshot> source) {
        if(!source)return false;
        auto immutable=std::make_shared<CaveVolumeSnapshot>(*source);
        canonicalizeCaveSnapshot(*immutable);
        auto indexed=std::make_shared<const CaveSnapshotSpatialIndex>(
            std::move(immutable));
        if(candidate_)return false;
        active_=std::move(indexed);
        runtimeEpoch_=active_->revision();
        return true;
    }
    // Proposed source is not visible to deterministic simulation readers.
    // The owning command/tick system explicitly commits the proposed source
    // at the same logical tick as its authoritative ECS edit state.
    bool prepare(std::shared_ptr<const CaveVolumeSnapshot> source,
                 CaveDirtyBounds bounds) {
        if(!active_||candidate_||!source)return false;
        if(bounds.maxX<=bounds.minX||bounds.maxY<=bounds.minY||
           bounds.maxZ<=bounds.minZ)
            throw std::invalid_argument("Caverns headless dirty bounds");
        auto draft=std::make_shared<CaveVolumeSnapshot>(*source);
        canonicalizeCaveSnapshot(*draft);
        if(runtimeEpoch_==UINT64_MAX)
            throw std::overflow_error("Caverns headless revision");
        draft->sourceRevision=std::max(draft->sourceRevision,runtimeEpoch_+1);
        candidate_=std::make_shared<const CaveSnapshotSpatialIndex>(
            std::move(draft));
        dirty_=bounds;
        return true;
    }
    bool commit(uint64_t exactRuntimeRevision) {
        if(!candidate_||candidate_->revision()!=exactRuntimeRevision)
            return false;
        active_=std::move(candidate_);
        runtimeEpoch_=exactRuntimeRevision;
        dirty_={};
        return true;
    }
    bool abort(uint64_t exactRuntimeRevision) {
        if(!candidate_||candidate_->revision()!=exactRuntimeRevision)
            return false;
        candidate_.reset();dirty_={};
        return true;
    }
    int64_t densityCm(PointCm p)const{
        if(!active_)throw std::logic_error("Caverns headless world not loaded");
        validatePoint(p);
        return active_->field(p);
    }
    CaveMesh mesh(ChunkKey3 key,uint32_t resolution,
                  int32_t spacingCm)const{
        if(!active_)throw std::logic_error("Caverns headless world not loaded");
        return generateCaveIndexedSnapshotMesh(*active_,key,
                                                resolution,spacingCm);
    }
    uint64_t revision()const noexcept{
        return active_?active_->revision():0;
    }
    uint64_t stagedRevision()const noexcept{
        return candidate_?candidate_->revision():0;
    }
    std::shared_ptr<const CaveSnapshotSpatialIndex>
    committedIndex()const noexcept{return active_;}
    std::shared_ptr<const CaveVolumeSnapshot>
    committedSnapshot()const noexcept{
        return active_?active_->frozenSnapshot():nullptr;
    }
    CaveDirtyBounds pendingBounds()const noexcept{return dirty_;}
private:
    std::shared_ptr<const CaveSnapshotSpatialIndex> active_,candidate_;
    uint64_t runtimeEpoch_=0;
    CaveDirtyBounds dirty_{};
};
} // namespace aiverse::terraweave::caverns
