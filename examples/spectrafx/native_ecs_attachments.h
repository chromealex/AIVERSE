// SpectraFX Stage 36: native C++ example, independent of game genre.
// Copy/include this header from a native project's *presentation* target.
// The host still owns Device::beginFrame/endFrame and the scene render pass.
// No GPU handle or renderer state is stored in the authoritative ECS World.
#pragma once

#include <aiverselib/genre/SpectraFX.h>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::examples::spectrafx {

// Example-authoritative pose: only fixed-point game state is in ECS.
// Real native projects may resolve poses from animation/interpolation instead.
struct DemoPose {
    Vec3 position{};
    Vec3 forward{0_fx,1_fx,0_fx};
};

inline Entity spawnDemoSource(World& world,const std::string& effect,
                              Vec3 position) {
    Entity e=world.create();
    world.add<DemoPose>(e).position=position;
    auto& component=world.add<lib::genre::SpectraSourceComponent>(e);
    component.effect=effect;
    component.scale=1_fx; // strict pooled GPU subset requires unit scale
    component.enabled=true;
    component.killOnDetach=true;
    return e;
}

// The pool must outlive GpuEcsAttachmentPresenter; member declaration order
// ensures the bridge is destroyed first. Device must outlive this object.
class NativeAttachmentExample final {
public:
    NativeAttachmentExample()
        : bridge_(pool_), recovery_(pool_,bridge_,&poseHistory_),
          capture_(pool_) {}
    NativeAttachmentExample(const NativeAttachmentExample&)=delete;
    NativeAttachmentExample& operator=(const NativeAttachmentExample&)=delete;

    // Requires a Device belonging to the host's graphics lifetime.
    // Unsupported graphs keep the original EffectDef in shared CPU world.
    bool init(gfx::Device& device,const fx::EffectDef& authoredEffect) {
        spectra::PoolWorldBudgets budgets{};
        budgets.maxGpuGroups=8;
        budgets.maxGroupCapacity=1024;
        if(!pool_.init(&device,budgets))return false;
        spectra::GraphRuntimeOptions gpu{};
        gpu.preference=spectra::ComputePreference::PreferGpu;
        gpu.gpuProgram=true;
        gpu.gpuMaterial=true;
        if(!pool_.defineEffect("demo_flame",authoredEffect,gpu))return false;

        spectra::PoolContinuousEmission rate{};
        rate.burstsPerSecond=12.f;
        rate.metresPerBurst=0.5f;
        rate.maxBurstsPerFrame=4;
        return bridge_.bindEffect("demo_flame",rate);
    }

    // After Device.beginFrame and BEFORE host starts the scene pass.
    bool prepare(const World& committedWorld,uint64_t committedTick,
                 const render::RTSCamera& camera,const render::mat4& viewProj,
                 int width,int height,float dt,float alpha=1.f) {
        if(frameOpen_)return false;
        ++ownerFrameOrdinal_;
        // Optional bounded reset retry service, only in the native owner's
        // already-open graphics frame and before active GPU encoders.
        if(recovery_.inspect().next==
           spectra::PoolRecoveryAction::RetryResetInGraphicsFrame) {
            capture_.cancel();
            captureMailbox_.clearFromHost();
            activeMailboxSequence_=0;
            (void)recovery_.serviceQueuedResetAtSafeFrame(ownerFrameOrdinal_);
        }
        // Metal delivers previous-frame readback callbacks at Device.beginFrame.
        // Poll BEFORE mutating the next GPU revision. While a capture remains
        // queued/in-flight/ready we intentionally keep VFX presentation frozen.
        // Editor UI only posts to this mailbox. The renderer host resolves
        // requests here on its single gfx Device owner thread, before step.
        if(captureMailbox_.consumeRelease()) {
            if(activeMailboxSequence_!=0)capture_.cancel();
            activeMailboxSequence_=0;
        }
        uint64_t mailboxSequence=0;
        size_t requestedGroup=0;
        if(captureMailbox_.claimRequested(mailboxSequence,requestedGroup)) {
            const auto current=capture_.status().phase;
            if(current!=spectra::GpuCapturePhase::Idle &&
               current!=spectra::GpuCapturePhase::Failed) {
                captureMailbox_.publishFailure(
                    mailboxSequence,"Native GPU capture host already has a request");
            } else if(!capture_.request(requestedGroup)) {
                captureMailbox_.publishFailure(
                    mailboxSequence,capture_.status().reason);
            } else activeMailboxSequence_=mailboxSequence;
        }
        capture_.pollBeforeStep();
        if(activeMailboxSequence_) {
            captureMailbox_.publishFromCapture(
                activeMailboxSequence_,capture_.status());
            if(capture_.status().phase==spectra::GpuCapturePhase::Failed)
                activeMailboxSequence_=0;
        }
        // Surface pending-reset / lost-Device actions to the renderer host
        // BEFORE asking the ECS bridge to stage new particle sources.
        const auto recovery=recovery_.poll();
        if(recovery.next!=spectra::PoolRecoveryAction::None)return false;
        // Capture only immutable committed fixed-point poses, once per tick.
        // A new Entity generation is automatically snapped rather than
        // interpolated against the previous occupant of the same slot.
        const auto resolve=[](const World& world,Entity entity,
                              fx::float3& pos,fx::float3& dir) {
            const DemoPose* pose=world.tryGetConst<DemoPose>(entity);
            if(!pose)return false;
            pos={float(pose->position.x.toDouble()),
                 float(pose->position.y.toDouble()),
                 float(pose->position.z.toDouble())};
            dir={float(pose->forward.x.toDouble()),
                 float(pose->forward.y.toDouble()),
                 float(pose->forward.z.toDouble())};
            return true;
        };
        const auto phase=capture_.status().phase;
        const bool captureFrozen=phase==spectra::GpuCapturePhase::Queued ||
            phase==spectra::GpuCapturePhase::InFlight ||
            phase==spectra::GpuCapturePhase::Ready;
        if(!captureFrozen &&
           !poseHistory_.captureCommitted(committedTick,committedWorld,resolve))
            return false;
        if(!pool_.beginPresentationFrame(camera,viewProj,width,height))
            return false;
        frameOpen_=true;
        // A queued diagnostic must not permit ECS transform changes,
        // source retire/kill or new emissions to invalidate its revision.
        // On resume sync all changes from the latest committed World.
        if((!captureFrozen &&
             !bridge_.syncCommitted(
                 committedTick,committedWorld,poseHistory_.resolver(alpha))) ||
           !pool_.step(captureFrozen?0.f:dt)) {
            pool_.endPresentationFrame();
            frameOpen_=false;
            return false;
        }
        if(phase==spectra::GpuCapturePhase::Queued)
            (void)capture_.submitAfterStepBeforePass();
        return true;
    }

    // Called INSIDE the open depth-tested scene pass, after scene geometry.
    void drawInsidePass(const fx::FxFog& fog={}) {
        if(frameOpen_)pool_.draw(fog);
    }

    // Called AFTER the host ends its scene pass but BEFORE Device.endFrame().
    void finish() {
        if(!frameOpen_)return;
        pool_.endPresentationFrame();
        frameOpen_=false;
    }

    // On rollback: finish any previous presentation frame. The host must
    // then begin a NEW Device graphics frame and call this outside encoders,
    // BEFORE beginPresentationFrame / scene rendering. GPU checked reset
    // requires an active RHI command buffer. On false, do not replay visuals.
    // Host separately owns restoring the authoritative World snapshot.
    // Schedule a VFX-only reset without attempting GPU work here.
    // The host should call afterRollback() once Device.beginFrame opens a
    // safe Metal command buffer and before opening its scene encoder.
    bool queueVisualReset() {
        if(frameOpen_)return false;
        capture_.cancel();
        captureMailbox_.clearFromHost();
        activeMailboxSequence_=0;
        return recovery_.requestPresentationReset("explicit visual rollback");
    }
    bool afterRollback() {
        if(frameOpen_)return false;
        capture_.cancel(); // invalidate any old-device in-flight callback
        captureMailbox_.clearFromHost();
        activeMailboxSequence_=0;
        if(!recovery_.hasQueuedReset() &&
           !recovery_.requestPresentationReset("committed ECS tick rollback"))
            return false;
        return recovery_.retryPresentationReset();
    }

    // Explicit graphics-device recovery. Both old and new Device objects
    // must still be alive here: the old pool owns old-device resources until
    // shutdown(), and the restored pool initializes on recoveredDevice.
    // Only authored effects/policies are restored; all physical GPU particles
    // and renderer source IDs are lost. ECS rebinds on the next prepare().
    bool rebuildOnRecoveredDevice(gfx::Device& recoveredDevice) {
        if(frameOpen_)return false;
        capture_.cancel(); // no snapshot crosses Device epochs
        captureMailbox_.clearFromHost();
        activeMailboxSequence_=0;
        return recovery_.replaceDevice(recoveredDevice);
    }

    // Native host receives an event when reset progress, failure/recovery
    // phase or Metal health changes. No automatic platform Device creation.
    void onRecoveryEvent(spectra::GpuPoolRecoveryHost::OnChange handler) {
        recovery_.onChange(std::move(handler));
    }
    spectra::PoolRecoveryEvent recoveryStatus() {
        return recovery_.poll();
    }
    // One asynchronous request. Call between presentation frames; the
    // normal prepare() frame loop handles GPU submission and completion.
    bool queueGpuCheckpoint(size_t groupIndex=0) {
        if(frameOpen_)return false;
        return capture_.request(groupIndex);
    }
    // Multi-group selection, still a single outstanding GPU readback.
    bool queueNextGpuCheckpoint(
        spectra::GpuCaptureTargetPolicy policy=
            spectra::GpuCaptureTargetPolicy::RoundRobin) {
        if(frameOpen_)return false;
        return capture_.requestNext(policy);
    }
    spectra::GpuCaptureStatus gpuCaptureStatus() const {
        return capture_.status();
    }
    bool takeGpuCheckpoint(std::vector<uint8_t>& bytes) {
        const bool taken=capture_.takeReady(bytes);
        if(taken && activeMailboxSequence_) {
            captureMailbox_.clearFromHost();
            activeMailboxSequence_=0;
        }
        return taken; // explicit sensitive RAW .spfx bytes
    }
    bool takeSafeGpuDiagnosticReport(
        std::string& json,spectra::GpuDiagnosticReportPrivacy privacy={}) {
        std::vector<uint8_t> bytes;
        if(!capture_.takeReady(bytes))return false;
        const auto inspected=spectra::inspectGpuCheckpointDiagnostic(bytes);
        if(!inspected.structurallyValid())return false;
        json=spectra::gpuDiagnosticReportJson(inspected,privacy);
        if(activeMailboxSequence_) {
            captureMailbox_.clearFromHost();
            activeMailboxSequence_=0;
        }
        return true; // aggregate JSON; no raw VM or per-particle coordinates
    }
    void cancelGpuCheckpoint() {
        capture_.cancel();
        captureMailbox_.clearFromHost();
        activeMailboxSequence_=0;
    }
    // Non-owning pointer may be attached to the editor UI. Native host MUST
    // detach it before shutdown. UI may only queue/cancel, not issue GPU calls.
    spectra::GpuCaptureRequestMailbox& captureMailbox() {
        return captureMailbox_;
    }
    // Native/editor host integration exposes only non-owning references.
    // Attach while NativeAttachmentExample lives; detach before shutdown.
    const spectra::GpuPoolWorld& poolTelemetry() const {return pool_;}
    spectra::GpuPoolRecoveryHost& recoveryHost() {return recovery_;}
    // Legacy immediate export remains read-only and cannot request readback.
    bool exportFreshGpuCheckpoint(size_t groupIndex,
                                  std::vector<uint8_t>& bytes) const {
        return pool_.exportFreshCheckpoint(groupIndex,bytes);
    }
    bool setLoadShedding(spectra::PoolLoadSheddingPolicy policy) {
        return pool_.setLoadShedding(policy);
    }

    // Explicitly release resources while Device is still alive.
    void shutdown() {
        if(frameOpen_)finish();
        capture_.cancel(); // RHI callbacks retain only weak capture state
        captureMailbox_.clearFromHost();
        activeMailboxSequence_=0;
        pool_.shutdown();
        bridge_.discardTrackingAfterPoolRecreation();
        poseHistory_.resetForRollback();
    }

    const spectra::PoolWorldStats& stats() const {return pool_.stats();}
    const std::string& reason() const {
        return bridge_.reason().empty()?pool_.reason():bridge_.reason();
    }
private:
    spectra::GpuPoolWorld pool_;
    spectra::GpuEcsAttachmentPresenter bridge_;
    spectra::GpuEcsPoseInterpolator poseHistory_{4096};
    spectra::GpuPoolRecoveryHost recovery_;
    spectra::GpuPoolCaptureHost capture_;
    spectra::GpuCaptureRequestMailbox captureMailbox_;
    uint64_t activeMailboxSequence_=0;
    uint64_t ownerFrameOrdinal_=0; // presentation frames, never gameplay ticks
    bool frameOpen_=false;
};

// Example call sequence (pseudocode around host-owned RHI):
//   NativeAttachmentExample fx;
//   fx.init(device,compiledEffect);
//   World world; auto flame=spawnDemoSource(world,"demo_flame",{});
//   Device.beginFrame();
//   fx.prepare(world,lastCommittedTick,camera,viewProj,w,h,dt,alpha);
//   Device.beginPass(scenePass); ... draw geometry ...
//   fx.drawInsidePass(); Device.endPass();
//   fx.finish(); Device.endFrame();
//   // Later, to demonstrate generation safety:
//   world.destroy(flame);
//   auto replacement=spawnDemoSource(world,"demo_flame",newPosition);
//   // After a gameplay rollback, at a safe graphics-frame boundary:
//   Device.beginFrame(); // requires a real active graphics command buffer
//   if(fx.afterRollback()) { world.copyFrom(savedAuthoritativeSnapshot); }
//   Device.endFrame();
//   // Never restart presentation replay unless visual reset succeeded.
//   // After an unrecoverable Metal Device loss:
//   // gfx::Device newDevice = hostRecreateGraphicsDevice(); // pseudocode
//   // bool recovered=fx.rebuildOnRecoveredDevice(newDevice);
//   // Host must keep the OLD Device alive through pool shutdown above.
//   // Re-resolve committed ECS sources via prepare() only after recovery.

} // namespace aiverse::examples::spectrafx
