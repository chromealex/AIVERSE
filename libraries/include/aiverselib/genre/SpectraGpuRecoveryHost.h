// SpectraFX Stage 39 — host-owned recovery workflow for a native game.
// This lives in the native presentation integration layer, not gameplay ECS.
// The host owns BOTH graphics Devices and must keep the OLD Device alive
// through pool.shutdown() while it recreates graphics.
#pragma once

#include "SpectraGpuAttachmentEcsAdapter.h"
#include "SpectraEcsRenderPose.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::spectra {

enum class PoolRecoveryPhase : uint8_t {
    Uninitialized, Ready, ResetRequested, ResetRetryRequired,
    DeviceRecreationRequired, BlockedUnknown
};
enum class PoolRecoveryAction : uint8_t {
    None, InitPool, RetryResetInGraphicsFrame, RecreateGraphicsDevice,
    InspectFailure
};
enum class PoolRecoveryServiceResult : uint8_t {
    NoWork, Deferred, ResetSucceeded, ResetFailed, NeedsNewDevice
};
struct PoolRecoveryEvent {
    PoolRecoveryPhase phase=PoolRecoveryPhase::Uninitialized;
    PoolRecoveryAction next=PoolRecoveryAction::InitPool;
    uint32_t resetGroupsAccepted=0, resetGroupsTotal=0;
    uint64_t resetAttempts=0;
    uint64_t transitionSequence=0; // stable until a REAL observable transition
    uint64_t hostResetRequests=0, deviceReplacementAttempts=0;
    bool explicitResetPending=false;
    bool recoveryCatalogRetained=false;
    uint32_t consecutiveResetFailures=0;
    uint64_t retryNotBeforeOwnerFrame=0;
    bool deviceHealthKnown=false, deviceHealthy=false;
    bool presentationBlocked=false;
    std::string reason;
};

class GpuPoolRecoveryHost final {
public:
    using OnChange=std::function<void(const PoolRecoveryEvent&)>;

    GpuPoolRecoveryHost(GpuPoolWorld& pool,
                        GpuEcsAttachmentPresenter& presenter,
                        GpuEcsPoseInterpolator* poses=nullptr)
        : pool_(pool),presenter_(presenter),poses_(poses) {}

    // Called by native frame host at a presentation safe point. Emits a
    // callback ONLY when the phase/progress/health changed; never starts
    // asynchronous work, destroys a Device, or performs GPU commands.
    PoolRecoveryEvent poll() {
        PoolRecoveryEvent result=inspect();
        if(!observed_ || changed(last_,result)) {
            result.transitionSequence=++transitionSequence_;
            last_=result;
            observed_=true;
            if(onChange_)onChange_(result);
        } else result.transitionSequence=last_.transitionSequence;
        return result;
    }
    void onChange(OnChange callback) {onChange_=std::move(callback);}
    // Caller can explicitly request a presentation reset BETWEEN frames.
    // This does not touch GPU or ECS; it tells the host to schedule
    // retryPresentationReset after Device.beginFrame and before render pass.
    bool requestPresentationReset(const std::string& purpose="visual rollback") {
        if(!pool_.ready() || recoveryRecipe_ ||
           pool_.resetReport().deviceLost ||
           (pool_.presentationBlocked() &&
            !pool_.resetReport().retryRequired)) {
            reason_="SpectraFX cannot queue reset on missing or lost Device";
            poll();
            return false;
        }
        explicitResetPending_=true;
        consecutiveResetFailures_=0;
        retryNotBeforeOwnerFrame_=0;
        resetPurpose_=purpose.substr(0,128);
        ++resetRequestCount_;
        reason_.clear();
        poll();
        return true;
    }
    bool hasQueuedReset() const {return explicitResetPending_;}
    // Optional bounded retry pump for the native presentation owner. This
    // must be called AFTER gfx::Device::beginFrame, BEFORE GPU encoders.
    // ownerFrameOrdinal counts local host frames, not wall time, GPU serials
    // or ECS ticks. It never spins/sleeps/starts asynchronous work.
    PoolRecoveryServiceResult serviceQueuedResetAtSafeFrame(
        uint64_t ownerFrameOrdinal) {
        const PoolRecoveryEvent before=inspect();
        if(before.next==PoolRecoveryAction::RecreateGraphicsDevice)
            return PoolRecoveryServiceResult::NeedsNewDevice;
        if(before.next!=PoolRecoveryAction::RetryResetInGraphicsFrame)
            return PoolRecoveryServiceResult::NoWork;
        if(ownerFrameOrdinal<retryNotBeforeOwnerFrame_)
            return PoolRecoveryServiceResult::Deferred;
        servicingReset_=true;
        const bool accepted=retryPresentationReset();
        servicingReset_=false;
        if(accepted) {
            consecutiveResetFailures_=0;
            retryNotBeforeOwnerFrame_=0;
            poll();
            return PoolRecoveryServiceResult::ResetSucceeded;
        }
        if(inspect().next==PoolRecoveryAction::RecreateGraphicsDevice)
            return PoolRecoveryServiceResult::NeedsNewDevice;
        // Exponential retry spacing avoids issuing failing Metal reset
        // blits every frame. Max 32 host frames between safe-point attempts.
        consecutiveResetFailures_=std::min<uint32_t>(
            consecutiveResetFailures_+1,16u);
        const uint32_t shift=std::min(consecutiveResetFailures_,5u);
        const uint64_t delay=uint64_t(1)<<shift;
        retryNotBeforeOwnerFrame_=
            ownerFrameOrdinal>std::numeric_limits<uint64_t>::max()-delay?
            std::numeric_limits<uint64_t>::max():
            ownerFrameOrdinal+delay;
        poll();
        return PoolRecoveryServiceResult::ResetFailed;
    }



    // Retry at a graphics safe point AFTER Device::beginFrame (Metal has an
    // active ordered command buffer), BEFORE any scene encoder/pass.
    // A false return leaves all sources paused/blocked for another retry.
    bool retryPresentationReset() {
        const PoolRecoveryEvent state=inspect();
        if(state.phase==PoolRecoveryPhase::DeviceRecreationRequired ||
           state.phase==PoolRecoveryPhase::Uninitialized) {
            reason_="SpectraFX reset needs an initialized recoverable GPU pool";
            poll();
            return false;
        }
        if(!presenter_.resetForRollback()) {
            reason_="SpectraFX presentation reset: "+presenter_.reason();
            if(!servicingReset_)poll();
            return false;
        }
        if(poses_)poses_->resetForRollback();
        explicitResetPending_=false;
        resetPurpose_.clear();
        reason_.clear();
        if(!servicingReset_)poll();
        return true;
    }

    // Host has already constructed a healthy NEW gfx::Device.
    // Call outside render/compute encoders and with the OLD Device alive.
    // Physical GPU particles and all render-source IDs are intentionally
    // lost. Effects are re-declared from original CPU-side definitions.
    // A failed import shuts down its partial new pool and does not claim
    // that any GPU data was restored.
    bool replaceDevice(gfx::Device& healthyReplacement) {
        ++deviceReplacementAttempts_;
        if(pool_.boundToDevice(&healthyReplacement)) {
            reason_="SpectraFX recovery requires a DIFFERENT graphics Device, "
                    "not the same previous graphics object";
            poll();
            return false;
        }
        if(!healthyReplacement.gpuHealthy()) {
            reason_="SpectraFX replacement Device is not healthy";
            poll();
            return false;
        }
        if(pool_.ready()) {
            // Snapshot compiled authoring data BEFORE releasing the old pool.
            // No old GPU handles, particle slots or ECS source generations
            // become part of the recovery recipe.
            recoveryRecipe_=pool_.exportRecoveryManifest();
            pool_.shutdown(); // old gfx Device must still be alive here
            presenter_.discardTrackingAfterPoolRecreation();
            if(poses_)poses_->resetForRollback();
        }
        if(!recoveryRecipe_) {
            reason_="SpectraFX Device replacement has no saved authored effect catalog";
            poll();
            return false;
        }
        // If the previous import failed, keep the CPU recipe and allow
        // another host-owned Device replacement WITHOUT the old Device.
        const bool rebuilt=pool_.importRecoveryManifest(
            &healthyReplacement,*recoveryRecipe_);
        if(!rebuilt) {
            reason_="SpectraFX Device replacement failed; CPU effect catalog "
                    "retained for retry: "+pool_.reason();
            poll();
            return false;
        }
        recoveryRecipe_.reset();
        explicitResetPending_=false;
        consecutiveResetFailures_=0;
        retryNotBeforeOwnerFrame_=0;
        resetPurpose_.clear();
        reason_.clear();
        poll();
        return true;
    }

    PoolRecoveryEvent inspect() const {
        PoolRecoveryEvent out;
        out.hostResetRequests=resetRequestCount_;
        out.deviceReplacementAttempts=deviceReplacementAttempts_;
        out.explicitResetPending=explicitResetPending_;
        out.recoveryCatalogRetained=bool(recoveryRecipe_);
        out.consecutiveResetFailures=consecutiveResetFailures_;
        out.retryNotBeforeOwnerFrame=retryNotBeforeOwnerFrame_;
        if(!pool_.ready()) {
            out.phase=recoveryRecipe_?
                PoolRecoveryPhase::DeviceRecreationRequired:
                PoolRecoveryPhase::Uninitialized;
            out.next=recoveryRecipe_?
                PoolRecoveryAction::RecreateGraphicsDevice:
                PoolRecoveryAction::InitPool;
            out.presentationBlocked=bool(recoveryRecipe_);
            out.reason=reason_.empty()?pool_.reason():reason_;
            return out;
        }
        const auto& reset=pool_.resetReport();
        const auto stats=pool_.checkpointStats();
        out.resetGroupsAccepted=reset.groupsAccepted;
        out.resetGroupsTotal=reset.groupCount;
        out.resetAttempts=reset.attempts;
        out.deviceHealthKnown=stats.gpuDeviceHealthKnown;
        out.deviceHealthy=stats.gpuDeviceHealthy;
        out.presentationBlocked=pool_.presentationBlocked();
        if(reset.deviceLost ||
           (stats.gpuDeviceHealthKnown && !stats.gpuDeviceHealthy)) {
            out.phase=PoolRecoveryPhase::DeviceRecreationRequired;
            out.next=PoolRecoveryAction::RecreateGraphicsDevice;
        } else if(reset.retryRequired) {
            out.phase=PoolRecoveryPhase::ResetRetryRequired;
            out.next=PoolRecoveryAction::RetryResetInGraphicsFrame;
        } else if(explicitResetPending_) {
            out.phase=PoolRecoveryPhase::ResetRequested;
            out.next=PoolRecoveryAction::RetryResetInGraphicsFrame;
            out.presentationBlocked=true; // native frame host must not emit
        } else if(out.presentationBlocked) {
            out.phase=PoolRecoveryPhase::BlockedUnknown;
            out.next=PoolRecoveryAction::InspectFailure;
        } else {
            out.phase=PoolRecoveryPhase::Ready;
            out.next=PoolRecoveryAction::None;
        }
        out.reason=reason_.empty()?
            (explicitResetPending_?resetPurpose_:pool_.reason()):reason_;
        return out;
    }

    const std::string& reason() const {return reason_;}

private:
    static bool changed(const PoolRecoveryEvent& a,
                        const PoolRecoveryEvent& b) {
        return a.phase!=b.phase || a.next!=b.next ||
            a.resetGroupsAccepted!=b.resetGroupsAccepted ||
            a.resetGroupsTotal!=b.resetGroupsTotal ||
            a.resetAttempts!=b.resetAttempts ||
            a.hostResetRequests!=b.hostResetRequests ||
            a.deviceReplacementAttempts!=b.deviceReplacementAttempts ||
            a.explicitResetPending!=b.explicitResetPending ||
            a.recoveryCatalogRetained!=b.recoveryCatalogRetained ||
            a.consecutiveResetFailures!=b.consecutiveResetFailures ||
            a.retryNotBeforeOwnerFrame!=b.retryNotBeforeOwnerFrame ||
            a.deviceHealthKnown!=b.deviceHealthKnown ||
            a.deviceHealthy!=b.deviceHealthy ||
            a.presentationBlocked!=b.presentationBlocked ||
            a.reason!=b.reason;
    }
    GpuPoolWorld& pool_;
    GpuEcsAttachmentPresenter& presenter_;
    GpuEcsPoseInterpolator* poses_=nullptr;
    OnChange onChange_;
    PoolRecoveryEvent last_{};
    std::string reason_;
    std::optional<PoolRecoveryManifest> recoveryRecipe_;
    bool observed_=false;
    bool explicitResetPending_=false;
    bool servicingReset_=false;
    uint64_t transitionSequence_=0,resetRequestCount_=0;
    uint64_t deviceReplacementAttempts_=0;
    uint32_t consecutiveResetFailures_=0;
    uint64_t retryNotBeforeOwnerFrame_=0;
    std::string resetPurpose_;
};

} // namespace aiverse::spectra
