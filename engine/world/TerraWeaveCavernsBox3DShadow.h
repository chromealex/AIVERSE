#pragma once
// Stage26 Box3D prepare-only CPU lease. Creating a Box3D body immediately
// inserts an active shape; therefore commit must run between simulation steps
// at a host-exclusive barrier. This is NOT cross-system atomic publication.
#include "engine/world/TerraWeaveCavernsShadowOwners.h"
#include <optional>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
class CaveBox3DShadow {
public:
    bool prepared()const noexcept{return bool(staged_);}
    uint64_t stagedRevision()const noexcept{return stagedRevision_;}
    uint64_t activeRevision()const noexcept{return activeRevision_;}
    physics::BodyHandle activeBody()const noexcept{return activeBody_;}
    bool prepare(const CaveCollisionPrepared& source) {
        if(!source.revision||source.revision<=activeRevision_||
           (staged_&&source.revision<=stagedRevision_)||
           (source.body.indices.empty()!=source.body.vertices.empty()))
            return false;
        staged_=source.body;
        stagedRevision_=source.revision;
        return true;
    }
    // Never call during a Box3D tick or concurrent collision query.
    // Creates new body then destroys old one at owner barrier.
    // If creation fails, keeps the old body and staged descriptor.
    // Creating a new body briefly overlaps old one inside this call.
    bool commit(physics::Scene& scene,uint64_t revision){
        if(!staged_||revision!=stagedRevision_)return false;
        // A fully filled/no-surface chunk has NO active body. Retiring
        // the previous body is itself a versioned commit.
        if(staged_->indices.empty()) {
            if(activeBody_!=physics::kNoBody && scene.valid(activeBody_) &&
               !scene.destroy(activeBody_))return false;
            activeBody_=physics::kNoBody;
            activeRevision_=stagedRevision_;
            staged_.reset();stagedRevision_=0;
            return true;
        }
        const auto created=scene.create(*staged_);
        if(created==physics::kNoBody)return false;
        if(activeBody_!=physics::kNoBody && scene.valid(activeBody_)) {
            if(!scene.destroy(activeBody_)) {
                (void)scene.destroy(created);
                return false;
            }
        }
        activeBody_=created;activeRevision_=stagedRevision_;
        staged_.reset();stagedRevision_=0;
        return true;
    }
    bool abort(uint64_t revision){
        if(!staged_||revision!=stagedRevision_)return false;
        staged_.reset();stagedRevision_=0;return true;
    }
    void unload(physics::Scene& scene){
        if(activeBody_!=physics::kNoBody&&scene.valid(activeBody_))
            (void)scene.destroy(activeBody_);
        activeBody_=physics::kNoBody;activeRevision_=0;
        staged_.reset();stagedRevision_=0;
    }
private:
    std::optional<physics::BodyDesc> staged_;
    physics::BodyHandle activeBody_=physics::kNoBody;
    uint64_t stagedRevision_=0,activeRevision_=0;
};
} // namespace aiverse::terraweave::caverns
