#pragma once
// TerraWeave Caverns Stage24: graphics owner-controlled GeometryScene handoff.
// No GPU resources are released merely because a newer cave mesh exists.
// STRATA and Renderer3D are both routed through ONE GeometryScene::draw.
// Physics/NavMesh commit is a mandatory host callback, NOT performed here.
#include "engine/render/TerraWeaveCavernsEditPresentation.h"
#include "engine/render/TerraWeaveCavernsSwapPolicy.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
namespace aiverse::terraweave::caverns {
class CaveGeometrySwapOwner {
public:
    static constexpr size_t kMaxRetainedScenes=8;
    CaveGeometrySwapOwner()=default;
    CaveGeometrySwapOwner(const CaveGeometrySwapOwner&)=delete;
    CaveGeometrySwapOwner& operator=(const CaveGeometrySwapOwner&)=delete;
    bool resident()const noexcept{return active_!=nullptr;}
    bool hasStaged()const noexcept{return staged_!=nullptr || stagedEmpty_;}
    uint64_t activeRevision()const noexcept{return activeRevision_;}
    uint64_t stagedRevision()const noexcept{return stagedRevision_;}
    size_t retiredCount()const noexcept{return retired_.size();}
    size_t pendingGpuOwners()const noexcept{
        return retired_.size()+size_t(bool(staged_));
    }
    // Called only on graphics owner outside any render pass. Stages actual
    // CPU models through GeometryScene's GPU upload/loadPrepared pipeline.
    // Old active scene is untouched. If an upload throws, failed GeometryScene
    // is retained for explicit safe teardown; never destroy a potentially
    // GPU-owning scene when the device's idle barrier is unavailable.
    bool stage(gfx::Device& dev,CaveBatchPrepared prepared,
               uint64_t renderRevision,bool enableStrata,
               size_t vertexBudget=2000000){
        if(!vertexBudget||vertexBudget>16000000||prepared.models.empty()||stagedEmpty_)return false;
        const size_t liveOwners=retired_.size()+size_t(bool(active_))+size_t(bool(staged_));
        if(!caveCanStageScene(activeRevision_,stagedRevision_,renderRevision,
                              liveOwners,kMaxRetainedScenes))return false; // backpressure, never leak/overallocate
        // Reserve before a GPU allocation to make failure retention safe.
        if(retired_.capacity()<kMaxRetainedScenes)
            retired_.reserve(kMaxRetainedScenes);
        auto candidate=std::make_unique<render::GeometryScene>();
        candidate->setStrataStagingEnabled(enableStrata);
        candidate->setStrataPortableRenderingEnabled(enableStrata);
        try {
            candidate->loadPrepared(dev,prepared.manifest,
                                    std::move(prepared.models),vertexBudget);
        } catch(...) {
            // A partially uploaded source may retain GPU retirement tokens.
            // Keep it alive for explicit idle/shutdown in collectAfterGpuIdle.
            retired_.push_back({std::move(candidate),0});
            throw;
        }
        if(staged_)retired_.push_back({std::move(staged_),0});
        staged_=std::move(candidate);
        stagedRevision_=renderRevision;
        return true;
    }
    // Full-Fill or all-solid/no-surface result: stage a DELETE without
    // uploading a dummy GeometryScene. The old active draw remains until
    // the same external owner barrier approves collision and nav retirement.
    bool stageEmpty(uint64_t renderRevision) {
        const size_t liveOwners=retired_.size()+size_t(bool(active_))+
                                size_t(bool(staged_));
        if(!caveCanStageScene(activeRevision_,stagedRevision_,
                              renderRevision,liveOwners,kMaxRetainedScenes)||
           stagedEmpty_)return false;
        if(staged_) {
            if(retired_.capacity()<kMaxRetainedScenes)
                retired_.reserve(kMaxRetainedScenes);
            retired_.push_back(std::move(staged_));
        }
        stagedEmpty_=true;
        stagedRevision_=renderRevision;
        return true;
    }
    // Host callback must guarantee all affected graphics + collision/nav
    // owners have staged compatible revisions and that coordinated visibility
    // swap succeeded at the owner barrier. A false result retains old draw.
    template<class Commit>
    bool activate(Commit&& hostCommit) {
        if(!hasStaged()||!caveCanActivateScene(activeRevision_,stagedRevision_))return false;
        bool committed=false;
        try {committed=hostCommit(stagedRevision_);}
        catch(...) {return false;}
        if(!committed)return false;
        if(active_)retired_.push_back({std::move(active_),0});
        active_=std::move(staged_); // nullptr for a staged empty cave
        activeRevision_=stagedRevision_;
        stagedEmpty_=false;
        stagedRevision_=0;
        return true;
    }
    // Draw only the active GeometryScene. Never additionally submit the same
    // chunk through the old CutTerrainResident or CaveGeometrySceneResident.
    void draw(render::Renderer3D& renderer) {
        if(active_)active_->draw(renderer);
    }
    // Stage38: graphics-owner frame boundary, OUTSIDE render passes.
    // No GPU idle wait: standard non-STRATA scenes are removed after real
    // GPU fence completion, then their model retirement tokens are polled.
    // STRATA scenes remain quarantined for a later explicit full-idle drain.
    // A zero/unsupported fence means keep the old owner alive.
    size_t collectCompletedWithoutIdle(gfx::Device& dev) {
        if(!dev.gpuHealthy())return 0;
        const uint64_t completed=dev.gpuCompletedFence();
        if(!completed)return 0;
        size_t released=0;
        for(auto it=retired_.begin();it!=retired_.end();) {
            if(!it->submissionFence)
                it->submissionFence=dev.gpuSubmissionFence();
            if(!it->submissionFence || completed<it->submissionFence ||
               !it->scene->deferredShutdownEligible()) {
                ++it;continue;
            }
            try {
                if(!it->scene->beginDeferredShutdown(dev)||
                   !it->scene->pollDeferredShutdown(dev)) {
                    ++it;continue;
                }
            }catch(...) {++it;continue;}
            it=retired_.erase(it);
            ++released;
        }
        return released;
    }
    size_t retainedStrataOwners()const noexcept{
        size_t count=0;
        for(const auto& entry:retired_)
            if(!entry.scene->deferredShutdownEligible())++count;
        return count;
    }

    // No active-GPU scene disposal without actual device idle proof.
    // Explicitly call only when renderer submissions for the old owner are
    // finished; failure must RETAIN all retired scenes for a later retry.
    bool collectAfterGpuIdle(gfx::Device& dev) {
        if(retired_.empty())return true;
        if(!dev.waitForResourceIdle())return false;
        for(auto it=retired_.begin();it!=retired_.end();) {
            try {
                it->scene->shutdown(dev);
                it=retired_.erase(it);
            }catch(...) {
                return false;
            }
        }
        return true;
    }
    // Cancels staging but does not release GPU resources. Retire via above.
    bool cancelStage() {
        if(stagedEmpty_){stagedEmpty_=false;stagedRevision_=0;return true;}
        if(!staged_)return false;
        // Moving the already-counted staged owner to retirement does not
        // increase the total number of live GPU owners.
        retired_.push_back(std::move(staged_));
        stagedRevision_=0;
        return true;
    }
    // Final teardown; caller must have stopped Scene draws and prove GPU idle.
    // On failure do NOT destroy this owner or its Scene pointers.
    bool shutdownAfterGpuIdle(gfx::Device& dev) {
        if(!dev.waitForResourceIdle())return false;
        // Explicitly retain any owner whose shutdown refuses the barrier.
        // No scene objects are destroyed until their shutdown returns.
        if(staged_) {
            try {staged_->shutdown(dev);}catch(...) {return false;}
            staged_.reset();stagedRevision_=0;
        }
        stagedEmpty_=false;
        if(active_) {
            try {active_->shutdown(dev);}catch(...) {return false;}
            active_.reset();activeRevision_=0;
        }
        return collectAfterGpuIdle(dev);
    }

private:
    std::unique_ptr<render::GeometryScene> active_;
    std::unique_ptr<render::GeometryScene> staged_;
    uint64_t activeRevision_=0,stagedRevision_=0;
    bool stagedEmpty_=false;
    struct RetiredScene {
        std::unique_ptr<render::GeometryScene> scene;
        uint64_t submissionFence=0; // captured at frame owner collection
    };
    std::vector<RetiredScene> retired_;
};
} // namespace aiverse::terraweave::caverns
