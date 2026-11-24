#pragma once
// Stage25: glue from CPU-prepared cave edits to real GeometryScene staging.
// SpatialWorld/Box3D/NavMesh prepare operations must be supplied by the host.
// No auto-confirmation on GPU load alone. No draw twice.
#include "engine/render/TerraWeaveCavernsGpuSwap.h"
#include "engine/world/TerraWeaveCavernsOwnerBarrier.h"
#include <stdexcept>
#include <utility>
#include <vector>
namespace aiverse::terraweave::caverns {
class CaveEditedSceneTransaction {
public:
    // Host owner must call only outside a graphics pass, between simulation
    // steps and with a stable immutable candidate set.
    bool prepareVisual(gfx::Device& device,
                       const std::vector<CavePreparedEdit>& candidates,
                       uint64_t generatorVersion,uint64_t batchRenderRevision,
                       bool strataEnabled=false,
                       CaveBatchSpec options={}) {
        if(!candidates.size()||!batchRenderRevision||state_.state()==CaveOwnerBarrierState::Preparing||
           state_.state()==CaveOwnerBarrierState::Ready)return false;
        const uint64_t src=candidates.front().candidate.sourceRevision;
        for(const auto& c:candidates)
            if(c.candidate.sourceRevision!=src || !src ||
               c.candidate.mesh.key!=c.dirty.key)return false;
        auto prepared=prepareCaveEditPresentation(candidates,generatorVersion,options);
        // Stage FIRST, preserving old active scene; then open owner ticket.
        // Do not expose geometry to Renderer3D until all owners are ready.
        // A completely filled/no-surface batch retires the old visual scene
        // ONLY after the same Box3D/SpatialWorld/NavMesh barrier succeeds.
        const bool staged=prepared.models.empty()
            ? gpu_.stageEmpty(batchRenderRevision)
            : gpu_.stage(device,std::move(prepared),
                         batchRenderRevision,strataEnabled,options.maxVertices);
        if(!staged)return false;
        ticket_=state_.begin(src,batchRenderRevision,candidates.size());
        (void)state_.prepared(ticket_,CaveOwnerPart::Geometry);
        return true;
    }
    // Must be called only after an external host stage successfully prepared
    // its actual corresponding revision with no gameplay-visible mutation.
    bool prepared(CaveOwnerBatchTicket t,CaveOwnerPart owner) {
        if(owner==CaveOwnerPart::Geometry)return false;
        return state_.prepared(t,owner);
    }
    // Require external all-owner atomic visibility proof. GPU owner changes
    // draw route ONLY if that proof callback returns true. A callback that
    // partially commits physics is a HOST BUG, not something we can roll back.
    template<class Commit>
    bool commit(CaveOwnerBatchTicket t,Commit&& externalAtomicCommit) {
        if(!state_.ready(t))return false;
        // Callback must synchronize physics, collision, nav and display
        // visibility at the owner barrier; never invoke from open render pass.
        bool success=gpu_.activate([&](uint64_t revision){
            return revision==t.renderRevision &&
                   externalAtomicCommit(t);
        });
        (void)state_.confirm(t,success);
        return success;
    }
    bool cancel() {
        const bool pending=state_.state()==CaveOwnerBarrierState::Ready||
                           state_.state()==CaveOwnerBarrierState::Preparing;
        if(!pending)return false;
        (void)gpu_.cancelStage();
        return state_.abort(ticket_);
    }
    void draw(render::Renderer3D& renderer){gpu_.draw(renderer);}
    CaveOwnerBatchTicket ticket()const noexcept{return ticket_;}
    CaveOwnerBarrierState state()const noexcept{return state_.state();}
    bool collectAfterGpuIdle(gfx::Device& device){return gpu_.collectAfterGpuIdle(device);}
    bool shutdownAfterGpuIdle(gfx::Device& device){return gpu_.shutdownAfterGpuIdle(device);}
    uint64_t activeRenderRevision()const noexcept{return gpu_.activeRevision();}
private:
    CaveGeometrySwapOwner gpu_;
    CaveOwnerBarrier state_;
    CaveOwnerBatchTicket ticket_{};
};
} // namespace aiverse::terraweave::caverns
