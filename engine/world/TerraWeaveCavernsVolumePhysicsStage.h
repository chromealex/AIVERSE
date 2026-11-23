#pragma once
// Stage 28: stage an authored CaveMesh into independent collision owners.
// Staging modifies NO live Box3D/SpatialWorld; the game host controls commit.
// Both owners use the exact same unsimplified cave surface, including empty
// chunks whose previous collider/body must be retired.
#include "engine/world/TerraWeaveCavernsVolumeCollision.h"
#include "engine/world/TerraWeaveCavernsSpatialShadow.h"
#include "engine/world/TerraWeaveCavernsBox3DShadow.h"
#include "engine/world/TerraWeaveCavernsAuthoredNav.h"
#include <cstdint>
#include <stdexcept>

namespace aiverse::terraweave::caverns {
class CaveVolumePhysicsStage {
public:
    // All operations must be on the physics owner thread between steps.
    // The collider ID allocator must reserve a fresh disjoint range globally
    // BEFORE prepare; firstId=0 is allowed only for empty triangles.
    bool prepare(const CaveMesh& surface,uint32_t resolution,
                 int32_t spacingCm,uint64_t sourceRevision,
                 uint32_t firstId,const world::SpatialWorld& spatial,
                 uint32_t userData=0) {
        if(spatial_.prepared()||box_.prepared())return false;
        auto generated=prepareCaveVolumeCollision(surface,resolution,
            spacingCm,sourceRevision,userData);
        if(!spatial_.prepare(generated,firstId,spatial))return false;
        try {
            if(!box_.prepare(generated)){
                (void)spatial_.abort(sourceRevision);
                return false;
            }
        }catch(...){
            (void)spatial_.abort(sourceRevision);
            throw;
        }
        pendingRevision_=sourceRevision;
        return true;
    }
    bool prepared(uint64_t revision)const noexcept{
        return revision!=0&&revision==pendingRevision_&&
               spatial_.stagedRevision()==revision&&
               box_.stagedRevision()==revision;
    }
    // Aborts ONLY unpublished preparation. Does not roll back a previously
    // committed Box3D body or SpatialWorld collider range.
    bool abort(uint64_t revision){
        if(!prepared(revision))return false;
        const bool a=spatial_.abort(revision);
        const bool b=box_.abort(revision);
        pendingRevision_=0;
        return a&&b;
    }
    // Host uses these in an exclusive barrier, with an explicit rollback
    // policy. Calling both sequentially is NOT cross-system atomic!
    CaveSpatialShadow& spatialOwner()noexcept{return spatial_;}
    CaveBox3DShadow& boxOwner()noexcept{return box_;}
    const CaveSpatialShadow& spatialOwner()const noexcept{return spatial_;}
    const CaveBox3DShadow& boxOwner()const noexcept{return box_;}
private:
    CaveSpatialShadow spatial_;
    CaveBox3DShadow box_;
    uint64_t pendingRevision_=0;
};
} // namespace aiverse::terraweave::caverns
