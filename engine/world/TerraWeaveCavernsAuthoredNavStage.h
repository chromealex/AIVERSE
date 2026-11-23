#pragma once
// Stage29: revision-aligned authored NavMesh staging, outside active gameplay.
// Build from the SAME canonical, immutable CaveVolumeSnapshot as the visual
// and collision payload. No Box3D or GeometryScene mutation in this class.
#include "engine/world/TerraWeaveCavernsShadowOwners.h"
#include "engine/world/TerraWeaveCavernsAuthoredNav.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
namespace aiverse::terraweave::caverns {
class CaveAuthoredNavStage {
public:
    // The caller owns the terrain surface height; it must reflect the same
    // committed TerraWeave/entrance version as the authored cave snapshot.
    bool prepare(std::shared_ptr<const CaveVolumeSnapshot> snapshot,
                 const CaveNavConfig& cfg,CaveNavSource::Height terrainHeight,
                 std::string* error=nullptr) {
        if(!snapshot || !snapshot->sourceRevision || !terrainHeight)
            throw std::invalid_argument("Caverns nav owner snapshot");
        if(shadow_.prepared())return false;
        auto source=makeCaveAuthoredNavSource(snapshot,cfg,
                                              std::move(terrainHeight));
        const auto revision=snapshot->sourceRevision;
        if(!shadow_.prepare(source,caveNavMeshConfig(cfg),revision,error))
            return false;
        stagedSnapshot_=std::move(snapshot);
        return true;
    }
    bool staged(uint64_t revision)const noexcept {
        return stagedSnapshot_ &&
               stagedSnapshot_->sourceRevision==revision &&
               shadow_.revision()==revision;
    }
    // The owner must pause all concurrent navigation readers and publish
    // only alongside matching render/SpatialWorld/Box3D revisions.
    bool commit(uint64_t revision) {
        if(!staged(revision) || !shadow_.commit(revision))return false;
        activeSnapshot_=std::move(stagedSnapshot_);
        return true;
    }
    bool abort(uint64_t revision) {
        if(!staged(revision)||!shadow_.abort(revision))return false;
        stagedSnapshot_.reset();
        return true;
    }
    const nav::Mesh* active()const noexcept {return shadow_.active();}
    uint64_t activeRevision()const noexcept {return shadow_.activeRevision();}
    const CaveVolumeSnapshot* activeSnapshot()const noexcept {
        return activeSnapshot_.get();
    }
private:
    CaveNavShadow shadow_;
    std::shared_ptr<const CaveVolumeSnapshot> stagedSnapshot_,activeSnapshot_;
};
} // namespace aiverse::terraweave::caverns
