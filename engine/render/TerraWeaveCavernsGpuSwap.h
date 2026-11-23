#pragma once
// TerraWeave Caverns Stage24: graphics owner-controlled GeometryScene handoff.
// No GPU resources are released merely because a newer cave mesh exists.
// STRATA and Renderer3D are both routed through ONE GeometryScene::draw.
// Physics/NavMesh commit is a mandatory host callback, NOT performed here.
#include "engine/render/TerraWeaveCavernsEditPresentation.h"
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
    bool hasStaged()const noexcept{return staged_!=nullptr;}
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
        if(!renderRevision||renderRevision<=activeRevision_||
           (staged_ && renderRevision<=stagedRevision_)||
           !vertexBudget||vertexBudget>16000000||
           prepared.models.empty())return false;
        if(retired_.size()>=kMaxRetainedScenes ||
           (staged_ && retired_.size()+1>=kMaxRetainedScenes))
            return false; // backpressure, never leak/overallocate
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
            retired_.push_back(std::move(candidate));
            throw;
        }
        if(staged_)retired_.push_back(std::move(staged_));
        staged_=std::move(candidate);
        stagedRevision_=renderRevision;
        return true;
    }
    // Host callback must guarantee all affected graphics + collision/nav
    // owners have staged compatible revisions and that coordinated visibility
    // swap succeeded at the owner barrier. A false result retains old draw.
    template<class Commit>
    bool activate(Commit&& hostCommit) {
        if(!staged_||stagedRevision_<=activeRevision_)return false;
        bool committed=false;
        try {committed=hostCommit(stagedRevision_);}
        catch(...) {return false;}
        if(!committed)return false;
        if(active_)retired_.push_back(std::move(active_));
        active_=std::move(staged_);
        activeRevision_=stagedRevision_;
        stagedRevision_=0;
        return true;
    }
    // Draw only the active GeometryScene. Never additionally submit the same
    // chunk through the old CutTerrainResident or CaveGeometrySceneResident.
    void draw(render::Renderer3D& renderer) {
        if(active_)active_->draw(renderer);
    }
    // No active-GPU scene disposal without actual device idle proof.
    // Explicitly call only when renderer submissions for the old owner are
    // finished; failure must RETAIN all retired scenes for a later retry.
    bool collectAfterGpuIdle(gfx::Device& dev) {
        if(retired_.empty())return true;
        if(!dev.waitForResourceIdle())return false;
        for(auto it=retired_.begin();it!=retired_.end();) {
            try {
                (*it)->shutdown(dev);
                it=retired_.erase(it);
            }catch(...) {
                return false;
            }
        }
        return true;
    }
    // Cancels staging but does not release GPU resources. Retire via above.
    bool cancelStage() {
        if(!staged_)return false;
        if(retired_.size()>=kMaxRetainedScenes)return false;
        retired_.push_back(std::move(staged_));
        stagedRevision_=0;
        return true;
    }
    // Final teardown; caller must have stopped Scene draws and prove GPU idle.
    // On failure do NOT destroy this owner or its Scene pointers.
    bool shutdownAfterGpuIdle(gfx::Device& dev) {
        if(!dev.waitForResourceIdle())return false;
        if(!cancelStage()&&staged_)return false;
        if(active_) {
            if(retired_.size()>=kMaxRetainedScenes)return false;
            retired_.push_back(std::move(active_));
        }
        activeRevision_=0;
        return collectAfterGpuIdle(dev);
    }
private:
    std::unique_ptr<render::GeometryScene> active_;
    std::unique_ptr<render::GeometryScene> staged_;
    uint64_t activeRevision_=0,stagedRevision_=0;
    std::vector<std::unique_ptr<render::GeometryScene>> retired_;
};
} // namespace aiverse::terraweave::caverns
