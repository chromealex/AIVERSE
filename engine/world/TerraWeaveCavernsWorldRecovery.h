#pragma once
// TerraWeave Caverns Stage40: explicit recovery workflow for a world that has
// entered Stage35 RecoveryRequired. Preserve last committed SDF, construct a
// NEW native cave runtime, and promote it only after all native owners have
// genuinely rebuilt their corresponding physics/nav/GPU revisions.
// Network relay servers do not own simulations or GPU devices. This is for
// the actual authority/scene owner (client, hosted game, or game server).
#include "engine/world/TerraWeaveCavernsRuntimeFrame.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::terraweave::caverns {
enum class CaveRecoveryPhase : uint8_t {
    Active, Quarantined, Rebuilding, ReadyToPromote, Failed
};
struct CaveRecoveryStatus {
    CaveRecoveryPhase phase=CaveRecoveryPhase::Active;
    uint64_t lastGoodRevision=0;
    uint64_t replacementRevision=0;
    bool nativeOwnersConfirmed=false;
};
class CaveWorldRecoverySession {
public:
    using NativeWorldRebuild=std::function<bool(
        CaveGameplayRuntimeFrame& replacement,
        const CaveVolumeSnapshot& lastCommittedSource)>;
    using NativeWorldSwap=std::function<bool(
        const CaveVolumeSnapshot& lastCommittedSource)>;
    explicit CaveWorldRecoverySession(
        std::unique_ptr<CaveGameplayRuntimeFrame> current)
        :current_(std::move(current)) {
        if(!current_)throw std::invalid_argument("Caverns missing recovery owner");
    }
    CaveGameplayRuntimeFrame& current() {
        if(status_.phase!=CaveRecoveryPhase::Active)
            throw std::logic_error("Caverns world quarantined for recovery");
        return *current_;
    }
    CaveRecoveryStatus status()const noexcept{return status_;}
    // Call ONLY after CaveGameplayRuntimeFrame::publishWithExclusiveWorld
    // returned true for a real all-owner commit, including nav and graphics.
    // Store the frozen authoritative SDF; never store GPU mesh output.
    bool rememberCommittedSource() {
        if(status_.phase!=CaveRecoveryPhase::Active)return false;
        const auto source=current_->host().sourceSnapshot();
        const auto epoch=current_->stats().committedRevision;
        if(!source||!epoch||source->sourceRevision!=epoch)return false;
        lastGood_=source;
        status_.lastGoodRevision=epoch;
        return true;
    }
    // STOP simulation, render submission and all queries before beginning.
    // A failed native commit may have destroyed some Box3D handles already:
    // reusing the partial old Scene instead of rebuilding is unsafe.
    bool beginRecovery(CaveDirtyBounds fullLoadedWorld) {
        if(!current_->physicsStatus().recoveryRequired || !lastGood_ ||
           status_.phase!=CaveRecoveryPhase::Active)return false;
        status_.phase=CaveRecoveryPhase::Quarantined;
        const auto& b=fullLoadedWorld;
        if(b.maxX<=b.minX||b.maxY<=b.minY||b.maxZ<=b.minZ){
            status_.phase=CaveRecoveryPhase::Failed;return false;
        }
        fullLoadedBounds_=fullLoadedWorld;
        replacement_=std::make_unique<CaveGameplayRuntimeFrame>();
        try { replacement_->openWorld(fullLoadedWorld,lastGood_); }
        catch(...) {
            replacement_.reset();
            status_.phase=CaveRecoveryPhase::Failed;
            return false;
        }
        status_.phase=CaveRecoveryPhase::Rebuilding;
        return true;
    }
    // The authoritative owner rebuilds a COMPLETE fresh native physics
    // Scene and SpatialWorld, plus NavMesh and GeometryScene resources, from
    // the same last-committed SDF. This must NOT reuse mixed old handles.
    // Keep all readers stopped during this phase.
    bool rebuildNative(const NativeWorldRebuild& actualRebuild) {
        if(status_.phase!=CaveRecoveryPhase::Rebuilding||
           !replacement_||!lastGood_||!actualRebuild)return false;
        bool ok=false;
        try {ok=actualRebuild(*replacement_,*lastGood_);}
        catch(...) {ok=false;}
        if(!ok)return false;
        // The new runtime must have performed a true all-owner commit. A
        // visual-only GPU preview never increments this committedRevision.
        const auto committed=replacement_->stats().committedRevision;
        if(!committed ||
           committed!=replacement_->host().sourceSnapshot()->sourceRevision)
            return false;
        status_.replacementRevision=committed;
        status_.nativeOwnersConfirmed=true;
        status_.phase=CaveRecoveryPhase::ReadyToPromote;
        return true;
    }
    // Perform the actual complete native world pointer/reader epoch swap
    // under host-owned exclusivity. Old GPU retirement must have real GPU
    // idle proof before dropping the old cave runtime owner.
    bool promote(gfx::Device& device,
                 const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
                 const NativeWorldSwap& swapNativeWorld) {
        if(status_.phase!=CaveRecoveryPhase::ReadyToPromote||
           !replacement_||!lastGood_||
           !status_.nativeOwnersConfirmed||
           !exclusive||!exclusive()||!swapNativeWorld)return false;
        // Retire the old presentation ONLY when GPU idle is proved. The
        // old physics/native world still needs separate full-world teardown.
        if(!current_->closeWorldAfterGpuIdle(device))return false;
        if(!exclusive())return false;
        bool swapped=false;
        try {swapped=swapNativeWorld(*lastGood_);}
        catch(...) {status_.phase=CaveRecoveryPhase::Failed;return false;}
        if(!swapped) {
            status_.phase=CaveRecoveryPhase::Failed;
            return false;
        }
        current_=std::move(replacement_);
        lastGood_=current_->host().sourceSnapshot();
        status_.lastGoodRevision=status_.replacementRevision;
        status_.replacementRevision=0;
        status_.nativeOwnersConfirmed=false;
        status_.phase=CaveRecoveryPhase::Active;
        return true;
    }
    const CaveVolumeSnapshot* lastGoodSource()const noexcept{
        return lastGood_.get();
    }
private:
    std::unique_ptr<CaveGameplayRuntimeFrame> current_,replacement_;
    std::shared_ptr<const CaveVolumeSnapshot> lastGood_;
    CaveDirtyBounds fullLoadedBounds_{};
    CaveRecoveryStatus status_{};
};
} // namespace aiverse::terraweave::caverns
