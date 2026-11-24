#pragma once
// Stage45 — ActorHost cave staged edit coordinator.
// Stage shadow SpatialWorld and solver caches while the active actor
// controller continues using the old scene. Publish exclusively at a
// between-tick barrier, after external full native-owner confirmation.
// No new physics World or second actor controller is ever installed.
#include "engine/world/TerraWeaveCavernsActorCollisionCatalog.h"
#include "engine/world/TerraWeaveCavernsNavigationRegionCache.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
namespace aiverse::terraweave::caverns {
class CaveActorCollisionCommit {
public:
    using Exclusive=std::function<bool()>;
    // Called only by actual native game owners. When Box3D/GPU/NavMesh
    // have owners, this callback must publish their matching revision.
    // A false return is fail-closed; if caller mutated an external owner
    // before returning false it MUST stop the world and recover.
    using ExternalOwners=std::function<bool(uint64_t)>;
    struct Status {
        uint64_t active=0,prepared=0;
        bool staged=false,recoveryRequired=false;
        size_t affectedChunks=0;
    };
    bool addInitial(world::SpatialWorld& spatial,
                    const CaveActorCollisionUpdate& edit,
                    uint64_t revision){
        if(staged_||recovery_)return false;
        return catalog_.addInitial(spatial,edit,revision);
    }
    bool prepare(const world::SpatialWorld& active,
                 std::shared_ptr<const CaveNavigationDataProvider> oldSource,
                 std::shared_ptr<const CaveNavigationDataProvider> nextSource,
                 std::shared_ptr<const CaveNavigationRegionCache> oldRegion,
                 const CaveNavigationChange& change,
                 const std::vector<CaveActorCollisionUpdate>& edits){
        if(staged_||recovery_||!oldSource||!nextSource||
           nextSource->revision()<=oldSource->revision()||
           change.fromRevision!=oldSource->revision()||
           change.toRevision!=nextSource->revision()||
           catalog_.revision()!=oldSource->revision())
            return false;
        // Build everything fallible BEFORE any external owner commits.
        auto candidate=catalog_.prepare(active,edits,
            oldSource->revision(),nextSource->revision());
        std::shared_ptr<CaveNavigationRegionCache> updated;
        if(oldRegion){
            updated=std::make_shared<CaveNavigationRegionCache>(*oldRegion);
            updated->refresh(change,nextSource);
        }
        staged_=std::make_unique<Pending>(Pending{
            std::move(candidate),std::move(nextSource),
            std::move(updated),change,edits.size()});
        return true;
    }
    bool publish(world::SpatialWorld& active,
                 std::shared_ptr<const CaveNavigationDataProvider>& ownerSource,
                 std::shared_ptr<CaveNavigationRegionCache>& ownerRegion,
                 const Exclusive& exclusive,
                 const ExternalOwners& externalOwners) {
        if(!staged_||recovery_||!exclusive||!exclusive()||
           !ownerSource||
           ownerSource->revision()!=staged_->change.fromRevision)
            return false;
        // These external owners are optional ONLY if the game has no such
        // resources. Stage35 multi-owner publish callback needs to hold the
        // SAME exclusive barrier through this method. It may be irrevocable;
        // never silently retry if it throws or reports a half-commit.
        if(externalOwners){
            bool ok=false;
            try{ok=externalOwners(staged_->candidate.revision);}
            catch(...){recovery_=true;return false;}
            if(!ok)return false;
        }
        if(!exclusive()){
            recovery_=true;return false;
        }
        // Map & SpatialWorld swaps allocate nothing. All candidate geometry,
        // navigation cache refresh, and new SDF source already exist.
        if(!catalog_.publish(active,staged_->candidate,exclusive)){
            recovery_=true;return false;
        }
        ownerSource.swap(staged_->source);
        if(staged_->region)ownerRegion.swap(staged_->region);
        staged_.reset();
        return true;
    }
    void abort()noexcept{staged_.reset();}
    void requireRecovery()noexcept{recovery_=true;}
    bool recoveryRequired()const noexcept{return recovery_;}
    Status status()const noexcept {
        return {catalog_.revision(),
                staged_?staged_->candidate.revision:0,
                bool(staged_),recovery_,
                staged_?staged_->affected:0};
    }
    const CaveActorCollisionCatalog& catalog()const noexcept{
        return catalog_;
    }
private:
    struct Pending {
        CaveActorCollisionCatalog::Candidate candidate;
        std::shared_ptr<const CaveNavigationDataProvider> source;
        std::shared_ptr<CaveNavigationRegionCache> region;
        CaveNavigationChange change;
        size_t affected=0;
    };
    CaveActorCollisionCatalog catalog_;
    std::unique_ptr<Pending> staged_;
    bool recovery_=false;
};
} // namespace aiverse::terraweave::caverns
