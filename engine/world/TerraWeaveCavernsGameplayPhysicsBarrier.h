#pragma once
// TerraWeave Caverns Stage35: restricted game-world physics publication.
// Invoke inside CaveGameplayEditHost::commitAtOwnerBarrier while all
// simulation, SpatialWorld, NavMesh and render readers are EXCLUSIVE/PAUSED.
// New body creation mutates live Box3D. Candidate insertion is rollbackable;
// final old-body destruction is NOT rollbackable if the native API fails.
#include "engine/world/TerraWeaveCavernsGameplayEditHost.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace aiverse::terraweave::caverns {
enum class CavePhysicsCommitPhase:uint8_t {
    Idle,Preflight,Installing,Retiring,Complete,RecoveryRequired
};
struct CavePhysicsCommitStatus {
    CavePhysicsCommitPhase phase=CavePhysicsCommitPhase::Idle;
    uint64_t revision=0;
    size_t installedPairs=0;
    bool recoveryRequired=false;
};
class CaveGameplayPhysicsBarrier {
    struct Pair {
        CaveSpatialShadow* spatial=nullptr;
        CaveBox3DShadow* box=nullptr;
    };
public:
    using ExclusiveProof=std::function<bool()>;
    const CavePhysicsCommitStatus& status()const noexcept{return status_;}
    // The exclusive predicate must reflect actual host-side exclusion, not
    // merely an animation frame counter. Exclusive lock must outlive this
    // function and the subsequent GPU shard activation.
    bool apply(const CaveGameplayCommitView& view,
               physics::Scene& boxScene,world::SpatialWorld& spatial,
               const ExclusiveProof& exclusive) {
        if(status_.recoveryRequired||!exclusive||!exclusive()||
           !view.snapshot.sourceRevision||view.meshes.empty())return false;
        const uint64_t revision=view.snapshot.sourceRevision;
        status_={CavePhysicsCommitPhase::Preflight,revision,0,false};
        if(!view.navigation.staged(revision))return false;
        std::vector<Pair> owners;
        owners.reserve(view.meshes.size());
        for(const auto& item:view.meshes) {
            if(item.candidate.sourceRevision!=revision||
               item.dirty.sourceRevision!=revision)return false;
            const auto it=view.physics.find(item.dirty.key);
            if(it==view.physics.end()||!it->second||
               !it->second->prepared(revision))return false;
            owners.push_back({&it->second->spatialOwner(),
                              &it->second->boxOwner()});
        }
        status_.phase=CavePhysicsCommitPhase::Installing;
        std::vector<Pair> installed;
        installed.reserve(owners.size());
        for(const auto owner:owners) {
            if(!owner.spatial->installCandidate(spatial,revision)) {
                rollback(installed,spatial,boxScene,revision);
                return false;
            }
            installed.push_back(owner);
            if(!owner.box->installCandidate(boxScene,revision)) {
                rollback(installed,spatial,boxScene,revision);
                return false;
            }
            ++status_.installedPairs;
        }
        // Irreversible section. Real host must be able to enter explicit
        // recovery when Scene::destroy fails. A false return here MUST NOT
        // be followed by more sim steps with inconsistent scene epochs.
        status_.phase=CavePhysicsCommitPhase::Retiring;
        for(const auto owner:installed) {
            if(!owner.box->finalizeInstalled(boxScene,revision)) {
                status_.phase=CavePhysicsCommitPhase::RecoveryRequired;
                status_.recoveryRequired=true;
                return false;
            }
        }
        for(const auto owner:installed) {
            if(!owner.spatial->finalizeInstalled(spatial,revision)) {
                status_.phase=CavePhysicsCommitPhase::RecoveryRequired;
                status_.recoveryRequired=true;
                return false;
            }
        }
        if(!view.navigation.commit(revision)) {
            status_.phase=CavePhysicsCommitPhase::RecoveryRequired;
            status_.recoveryRequired=true;
            return false;
        }
        status_.phase=CavePhysicsCommitPhase::Complete;
        return true;
    }
private:
    void rollback(const std::vector<Pair>& installed,
                  world::SpatialWorld& spatial,physics::Scene& boxScene,
                  uint64_t revision) {
        bool okay=true;
        for(auto it=installed.rbegin();it!=installed.rend();++it) {
            // Box first: if a new body was created, release it while reader
            // barrier remains closed. Existing active bodies remain intact.
            if(it->box->candidateInstalled() &&
               !it->box->revertCandidate(boxScene,revision))okay=false;
            if(it->spatial->candidateInstalled() &&
               !it->spatial->revertCandidate(spatial,revision))okay=false;
        }
        if(!okay) {
            status_.phase=CavePhysicsCommitPhase::RecoveryRequired;
            status_.recoveryRequired=true;
        } else status_.phase=CavePhysicsCommitPhase::Preflight;
    }
    CavePhysicsCommitStatus status_{};
};
} // namespace aiverse::terraweave::caverns
