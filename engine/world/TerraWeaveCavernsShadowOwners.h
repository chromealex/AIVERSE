#pragma once
// Stage 26: prepare-only collision and navigation resources.
// This file deliberately does NOT mutate live Box3D / SpatialWorld / nav.
// Actual Box3D Scene::create activates bodies immediately, so it is NOT safe
// as a prepare phase of a supposedly atomic multi-owner transaction.
#include "engine/world/TerraWeaveCavernsBox3D.h"
#include "engine/world/TerraWeaveCavernsMultiNav.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::terraweave::caverns {

struct CaveCollisionPrepared {
    uint64_t revision=0;
    world::Cell cell{};
    std::vector<world::Triangle> triangles;
    physics::BodyDesc body;
};
// Validate and copy the same triangles into future SpatialWorld and Box3D
// sources. No bodies or colliders are inserted here.
inline CaveCollisionPrepared prepareCaveCollision(
    const CutNativePayload& payload,int32_t tileMetres,uint32_t userData=0){
    if(payload.revision==0||payload.collision.empty())
        throw std::invalid_argument("Caverns collision source revision");
    CaveCollisionPrepared result;
    result.revision=payload.revision;
    result.cell=payload.cell;
    result.triangles=payload.collision;
    for(const auto& triangle:result.triangles)
        if(!world::SpatialWorld::validTriangle(triangle))
            throw std::invalid_argument("Caverns malformed collision triangle");
    result.body=makeCavernsBox3DBody(payload,tileMetres,userData);
    return result;
}

// Build a fresh native nav::Mesh without touching the currently visible mesh.
// A client may retain the old mesh until the corresponding graphics and
// collision revision becomes visible. This handles nav::Source generically;
// authored volume-aware cave source must be supplied by its owner.
class CaveNavShadow {
public:
    bool prepared()const noexcept{return bool(staged_);}
    uint64_t revision()const noexcept{return stagedRevision_;}
    bool prepare(const nav::Source& source,const nav::Config& cfg,
                 uint64_t revision,std::string* error=nullptr){
        if(!revision || (active_&&revision<=activeRevision_) ||
           (staged_&&revision<=stagedRevision_))
            return false;
        auto candidate=std::make_unique<nav::Mesh>();
        if(!candidate->build(source,cfg,error))return false;
        staged_=std::move(candidate);
        stagedRevision_=revision;
        return true;
    }
    // Caller must do this at a synchronized owner barrier with no live
    // nav readers or physics queries. This swap cannot roll back other
    // already-mutated systems; the host owns that guarantee.
    bool commit(uint64_t revision){
        if(!staged_||stagedRevision_!=revision)return false;
        active_.swap(staged_);
        staged_.reset();
        activeRevision_=revision;stagedRevision_=0;
        return true;
    }
    bool abort(uint64_t revision){
        if(!staged_||revision!=stagedRevision_)return false;
        staged_.reset();stagedRevision_=0;return true;
    }
    const nav::Mesh* active()const noexcept{return active_.get();}
    uint64_t activeRevision()const noexcept{return activeRevision_;}
private:
    std::unique_ptr<nav::Mesh> active_,staged_;
    uint64_t activeRevision_=0,stagedRevision_=0;
};

} // namespace aiverse::terraweave::caverns
