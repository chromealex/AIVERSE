#pragma once
// Stage26 SpatialWorld staged collision lease.
// Stage creates ONLY owned CPU data. Commit first inserts fresh disjoint IDs,
// rolls back inserted IDs on failure, then retires the previous ID range.
// No gameplay queries may interleave commit; owner thread barrier required.
#include "engine/world/TerraWeaveCavernsShadowOwners.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
namespace aiverse::terraweave::caverns {
class CaveSpatialShadow {
public:
    bool prepared()const noexcept{return bool(staged_);}
    uint64_t activeRevision()const noexcept{return activeRevision_;}
    uint64_t stagedRevision()const noexcept{return stagedRevision_;}
    size_t activeCount()const noexcept{return activeCount_;}

    bool prepare(const CaveCollisionPrepared& source,
                 uint32_t firstColliderId,
                 const world::SpatialWorld& spatial){
        if(!source.revision||
           source.triangles.size()>65534||
           (!source.triangles.empty()&&!firstColliderId)||
           source.revision<=activeRevision_||
           (staged_&&source.revision<=stagedRevision_))
            return false;
        const uint64_t begin=firstColliderId;
        const uint64_t end=begin+source.triangles.size();
        if(end>uint64_t(UINT32_MAX)+1)return false;
        if(!source.triangles.empty() && activeCount_ &&
           begin< uint64_t(activeId_)+activeCount_ &&
           uint64_t(activeId_)<end)return false;
        for(uint64_t id=begin;id<end;++id)
            if(spatial.colliders().contains(uint32_t(id)))return false;
        for(const auto& t:source.triangles)
            if(!world::SpatialWorld::validTriangle(t))return false;
        staged_=source.triangles;
        stagedId_=source.triangles.empty()?0:firstColliderId;
        stagedRevision_=source.revision;
        return true;
    }
    // Stage 35: insert a new ID range during an EXCLUSIVE reader pause
    // without retiring any previously active collider. This may fail and
    // is rollbackable while old colliders remain untouched.
    bool installCandidate(world::SpatialWorld& spatial,uint64_t revision) {
        if(!staged_||revision!=stagedRevision_||installed_)return false;
        for(size_t i=0;i<staged_->size();++i)
            if(spatial.colliders().contains(stagedId_+uint32_t(i)))return false;
        size_t inserted=0;
        try {
            for(const auto& tri:*staged_) {
                if(!spatial.addTriangle(stagedId_+uint32_t(inserted),tri))
                    throw std::runtime_error("Caverns staged SpatialWorld insert");
                ++inserted;
            }
        }catch(...) {
            for(size_t i=0;i<inserted;++i)
                spatial.remove(stagedId_+uint32_t(i));
            return false;
        }
        installed_=true;
        return true;
    }
    bool revertCandidate(world::SpatialWorld& spatial,uint64_t revision) {
        if(!installed_||revision!=stagedRevision_||!staged_)return false;
        for(size_t i=0;i<staged_->size();++i)
            spatial.remove(stagedId_+uint32_t(i));
        installed_=false;
        return true;
    }
    // Called after all fallible candidate installations and the host's
    // full no-reader barrier. Only old resource retirement occurs here.
    bool finalizeInstalled(world::SpatialWorld& spatial,uint64_t revision) {
        if(!installed_||revision!=stagedRevision_||!staged_)return false;
        for(size_t i=0;i<activeCount_;++i)
            spatial.remove(activeId_+uint32_t(i));
        activeId_=stagedId_;
        activeCount_=staged_->size();
        activeRevision_=stagedRevision_;
        staged_.reset();stagedId_=0;stagedRevision_=0;
        installed_=false;
        return true;
    }
    // Called only between simulation updates/queries. The fresh range is
    // disjoint from the old range and owned by this lease.
    bool commit(world::SpatialWorld& spatial,uint64_t revision){
        if(!staged_||revision!=stagedRevision_||installed_)return false;
        // Revalidate reservation just before insert; another owner may have
        // occupied the IDs during the CPU staging interval.
        for(size_t i=0;i<staged_->size();++i)
            if(spatial.colliders().contains(stagedId_+uint32_t(i)))return false;
        size_t added=0;
        try {
            for(const auto& tri:*staged_){
                if(!spatial.addTriangle(stagedId_+uint32_t(added),tri))
                    throw std::runtime_error("Caverns collider insertion failed");
                ++added;
            }
        }catch(...){
            for(size_t i=0;i<added;++i)
                spatial.remove(stagedId_+uint32_t(i));
            return false;
        }
        for(size_t i=0;i<activeCount_;++i)
            spatial.remove(activeId_+uint32_t(i));
        activeId_=stagedId_;
        activeCount_=staged_->size();
        activeRevision_=stagedRevision_;
        staged_.reset();stagedId_=0;stagedRevision_=0;
        return true;
    }
    bool abort(uint64_t revision){
        if(!staged_||revision!=stagedRevision_||installed_)return false;
        staged_.reset();stagedId_=0;stagedRevision_=0;return true;
    }
    void unload(world::SpatialWorld& spatial){
        for(size_t i=0;i<activeCount_;++i)
            spatial.remove(activeId_+uint32_t(i));
        activeId_=0;activeCount_=0;activeRevision_=0;
        staged_.reset();stagedRevision_=0;stagedId_=0;
    }
private:
    uint32_t activeId_=0,stagedId_=0;
    size_t activeCount_=0;
    bool installed_=false;
    uint64_t activeRevision_=0,stagedRevision_=0;
    std::optional<std::vector<world::Triangle>> staged_;
};
} // namespace aiverse::terraweave::caverns
