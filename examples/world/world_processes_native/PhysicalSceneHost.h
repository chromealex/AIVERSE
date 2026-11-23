// Native host facade for real MORPH + GRANULAR rendering/physics.
// Deliberately opt-in and above engine/: neither gameplay ECS core nor
// unrelated RTS/FPS games receive a Box3D/Renderer3D dependency.
#pragma once
#include "MorphModelStream.h"
#include "MorphBox3DContactBridge.h"
#include "GranularWorldIntegration.h"
#include "GranularTerraWeaveTransfer.h"
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace aiverse::examples::world_processes_native {

class PhysicalSceneHost {
public:
    using InstanceId=std::uint64_t;

    // Call on the rendering owner thread after the source Model has a
    // dedicated GPU upload; one stream per *render instance*, never global
    // glTF vertex state. The host owns source Model and pristine CPU data.
    bool addMorphVisual(InstanceId id,gfx::Device& device,
                        const render::Model& model,
                        std::span<const render::ModelVertex> pristine,
                        std::span<const std::uint32_t> indices,
                        std::span<const MorphVertexBinding> binding,
                        MorphModelStreamConfig config={}) {
        if(!id||morphVisuals_.contains(id)||morphVisuals_.size()>=1024)return false;
        auto stream=std::make_unique<MorphModelStream>();
        if(!stream->initialize(device,model,pristine,indices,binding,config))
            return false;
        try{morphVisuals_.emplace(id,std::move(stream));}
        catch(...){
            if(stream)stream->shutdownAfterIdle();
            throw;
        }
        return true;
    }
    bool updateMorphVisuals(const World& ecs) {
        for(auto& [id,stream]:morphVisuals_){
            (void)id;
            if(!stream->update(ecs))return false;
        }
        return true;
    }
    bool attachMorphVisual(InstanceId id,render::ModelDraw& draw) const {
        const auto it=morphVisuals_.find(id);
        return it!=morphVisuals_.end()&&it->second->attach(draw);
    }

    // Read from Scene immediately after physics::Scene::step(), but do
    // NOT modify ECS yet. The authoritative host must record these
    // integer inputs to its existing net/replay channel.
    bool collectMorphPhysicsInputs(
            const physics::Scene& scene,const World& ecs,
            const std::map<physics::BodyHandle,Entity>& bodies,
            std::uint32_t nextTick,
            std::vector<MorphContactInput>& out,
            MorphContactConfig config={}) const {
        return collectMorphContactInputs(scene,ecs,bodies,nextTick,out,config);
    }

    // Commit recorded inputs on the fixed ECS owner barrier BEFORE the
    // scheduled MORPH system tick. Never re-sample Box3D on clients.
    bool applyRecordedMorphInputs(World& ecs,
                  std::span<const MorphContactInput> input,
                  std::uint32_t nextTick) {
        return enqueueMorphContactInputs(ecs,input,nextTick);
    }

    // The sole authoritative terrain/loose-granule transaction. Must be
    // invoked at the host's completed fixed-step World barrier.
    bool transferTerrainMass(World& ecs,
            const terraweave::TerraWeaveEcsAuthorityConfig& terra,
            const GranularTerraTransfer& request,std::uint32_t completedTick,
            GranularTerraTransferResult& out) {
        return transferGranularAndTerrain(ecs,terra,request,completedTick,out);
    }

    // Frame prepared from a completed ECS tick and the SAME TerraWeave soil
    // revision. Move/share immutable patches to physics and render owners.
    GranularPreparedFrame prepareGranular(
            const World& ecs,const terraweave::EarthworksLayer& soil,
            std::span<const GranularResidentTile> residency,
            const std::map<std::uint64_t,GranularSurfaceSettings>& placements,
            std::uint32_t completedTick,GranularFrameBudget budget={}) const {
        return prepareGranularFrame(ecs,soil,residency,
                                   placements,completedTick,budget);
    }

    bool publishPhysics(physics::Scene& scene,
                        const GranularPreparedFrame& frame,
                        std::uint32_t userData=0) {
        return publishGranularPhysicsFrame(scene,physics_,frame,userData);
    }
    bool publishRender(gfx::Device& device,
                       const GranularPreparedFrame& frame) {
        return publishGranularRenderFrame(device,render_,frame);
    }

    // Called by the usual Renderer3D frame owner; no new render pass or
    // backend-specific shader is introduced.
    void drawGranular(render::Renderer3D& renderer,
                      render::float4 tint={1,1,1,1},
                      std::uint32_t flags=0) const {
        render_.draw(renderer,tint,flags);
    }
    // Called when a *historical* COW snapshot is restored. The old
    // visual/Box3D projections must not be mistaken for canonical state.
    bool resetAfterRollback(physics::Scene& scene,gfx::Device& device) {
        if(!render_.shutdownAfterIdle(device))return false;
        physics_.shutdown(scene);
        // The MORPH models remain valid if the source asset revision did
        // not change; the next update() reconstructs every vertex from
        // the restored ECS Node positions and unchanged pristine sources.
        return true;
    }

    // Explicitly unload all host-owned GPU and Box3D resources at
    // completed owner-thread lifetime fences.
    bool shutdownAfterIdle(physics::Scene& scene,gfx::Device& device) {
        for(auto& [id,stream]:morphVisuals_){
            (void)id;
            if(!stream->shutdownAfterIdle())return false;
        }
        morphVisuals_.clear();
        return resetAfterRollback(scene,device);
    }
    std::size_t morphVisualCount()const{return morphVisuals_.size();}
    std::size_t granularPhysicsTiles()const{return physics_.count();}
    std::size_t granularRenderTiles()const{return render_.count();}
private:
    std::map<InstanceId,std::unique_ptr<MorphModelStream>> morphVisuals_;
    GranularBox3DPublisher physics_;
    GranularRenderPublisher render_;
};

} // namespace aiverse::examples::world_processes_native
