#pragma once
// Client-only visual world scene for the existing generic Replica stream.
// No Simulation, collision, ECS movement, authoritative access or new GPU
// device is created here. Network receive is staged by the client host;
// accept/initialize/shutdown run on the serialized graphics owner.
#include "engine/buildings/BuildingReplicaBridge.h"
#include "engine/buildings/BuildingSceneContentIdentity.h"
#include "engine/buildings/BuildingRenderer3DScene.h"
#include <memory>
#include <string>
#include <utility>

namespace aiverse::building {
class BuildingReplicaRenderScene {
public:
    struct Definition {
        uint64_t sceneId=0;
        std::string instanceKey,styleId;
        Vec3 origin{};
        uint32_t yaw=0,graphDomains=0;
        uint16_t stepPermille=100;
    };
    BuildingReplicaRenderScene()=default;
    BuildingReplicaRenderScene(const BuildingReplicaRenderScene&)=delete;
    BuildingReplicaRenderScene& operator=(const BuildingReplicaRenderScene&)=delete;

    bool prepare(const Definition& def,const RuntimeBuildingGenerator& generator,
                 const FunctionalBuildingRecipe& recipe,const BuildingImageByGuid& resolver,
                 std::string* error=nullptr){
        if(prepared_||!def.sceneId||def.instanceKey.empty()||def.yaw>3||
           !def.graphDomains||(def.graphDomains&~kBuildingGraphAll)||
           !def.stepPermille||def.stepPermille>250)
            return fail(error,"Invalid replica building definition");
        const auto* authored=generator.style(def.styleId);
        if(!authored)return fail(error,"Replica building style unavailable");
        auto generated=generator.generateRecipe(def.styleId,recipe,{},def.yaw);
        if(!generated)return fail(error,generated.message);
        std::vector<BuildingAperturePose> poses;
        if(!buildingAuthoredAperturePoses(generated,def.origin,def.yaw,poses,error)||
           poses.size()>64)return false;
        BuildingCirculationHost layout;
        if(!layout.initialize(generated,def.origin,def.yaw,error))return false;
        std::map<uint32_t,uint32_t> liftLayout;
        for(const auto& [id,lift]:layout.lifts())liftLayout.emplace(id,lift.floorCount());
        if(liftLayout.size()>32)return fail(error,"Replica lift budget exceeded");
        const auto hash=buildingSceneContentIdentity(def.sceneId,def.instanceKey,
            def.styleId,def.origin,def.yaw,def.graphDomains,def.stepPermille,
            generated,poses,liftLayout);
        auto style=*authored;style.materials=generated.building.mesh.materials;
        try{
            auto shell=runtimeTexturedBuildingModel(generated.building,resolver,
                "replica-building-"+std::to_string(def.sceneId),error);
            if(!shell||!apertures_.initialize(poses,style,error))return false;
            const int material=style.structure.plate>=0?style.structure.plate:style.shell;
            if(material<0||size_t(material)>=style.materials.size()){
                apertures_.clear();return fail(error,"Invalid replica panel material");
            }
            for(const auto& pose:poses)
                if(!apertures_.bindStyleMaterial(pose.socketId,style.materials[size_t(material)],resolver,error)){
                    apertures_.clear();return false;
                }
            if(!liftLayout.empty()&&!lifts_.initialize(layout,style,resolver,error)){
                apertures_.clear();lifts_.clear();return false;
            }
            const auto size=generated.building.constructionSize;
            if(!liftLayout.empty()&&!lifts_.publish(buildingLiftVisualFrame(layout,
                   buildingLiftVisualDoorLayout(layout),size.width,size.depth,def.origin,def.yaw))){
                apertures_.clear();lifts_.clear();return fail(error,"Invalid initial replica lift layout");
            }
            def_=def;authoredPoses_=std::move(poses);layout_=std::move(layout);
            width_=size.width;depth_=size.depth;hash_=hash;shell_=std::move(shell);
            prepared_=true;return true;
        }catch(const std::exception& e){
            apertures_.clear();lifts_.clear();return fail(error,e.what());
        }
    }

    // Only trusted SERVER messages from the existing admitted transport.
    // Verify all sockets/lifts and checkpoint identities before changing
    // any presentation. Epoch rollback snaps instead of animating old state.
    bool accept(std::span<const uint8_t> packet,std::string* error=nullptr){
        if(!prepared_)return fail(error,"Replica CPU assets not prepared");
        auto candidate=view_;
        if(!candidate.accept(packet,def_.sceneId,hash_,authoredPoses_.size(),error))return false;
        const auto& frame=candidate.frame();
        if(frame.hasCirculation!=!layout_.lifts().empty())
            return fail(error,"Replica lift layout mismatch");
        auto poses=authoredPoses_;
        for(size_t i=0;i<poses.size();++i){
            poses[i].physicalPermille=frame.leaves[i].physicalPermille;
            poses[i].moving=frame.leaves[i].moving;poses[i].open=frame.leaves[i].open;
            poses[i].revision=frame.revision;
        }
        BuildingLiftPresentationFrame liftFrame;
        if(frame.hasCirculation){
            BuildingCirculationCheckpoint saved;
            for(const auto& state:frame.circulation.lifts)
                if(!saved.lifts.emplace(state.featureIndex,BuildingLiftCheckpoint{state,0}).second)
                    return fail(error,"Duplicate replicated lift");
            for(const auto& rider:frame.circulation.passengers)
                if(!saved.riders.emplace(rider.actorId,rider.liftFeature).second)
                    return fail(error,"Duplicate replicated rider");
            auto staged=layout_;
            if(!staged.restoreCheckpoint(saved))return fail(error,"Invalid replicated lift checkpoint");
            liftFrame=buildingLiftVisualFrame(staged,buildingLiftVisualDoorLayout(staged),
                                     width_,depth_,def_.origin,def_.yaw);
            if(!liftFrame.valid)return fail(error,"Invalid replicated lift pose");
        }
        const bool reset=!view_.frame().sceneId||view_.frame().epoch!=frame.epoch;
        if(!apertures_.resetPhysicalTimeline(poses,frame.serverTick,frame.revision,reset))
            return fail(error,"Invalid replica aperture geometry");
        if(frame.hasCirculation&&!lifts_.publish(std::move(liftFrame)))return false;
        view_=std::move(candidate);
        if(error)error->clear();return true;
    }
    template<class SceneView>
    bool upload(gfx::Device& device,render::Renderer3D& renderer,
                render::RenderDeviceLifetime::Lease lease,SceneView& view,
                std::string* error=nullptr){
        if(!prepared_||!view_.frame().sceneId||renderer_)
            return fail(error,"Await confirmed replica state before GPU publication");
        renderer_=std::make_shared<BuildingRenderer3DScene>();
        if(!renderer_->initializePresentation(device,renderer,std::move(lease),
            [this](const BuildingRenderer3DScene::ModelVisitor& visit){
                return apertures_.forEachCpuModel(visit);
            },[this](uint32_t ms,const BuildingDualLeafGameSession::SubmitLeaf& submit,std::string* issue){
                const auto frame=apertures_.interpolate(ms);
                if(!frame.valid)return fail(issue,frame.error);
                for(const auto& pose:frame.leaves){
                    const auto* model=apertures_.model(pose.socketId);
                    if(!model||!submit(*model,pose))return false;
                }
                return true;
            },def_.sceneId,error,shell_.get(),def_.origin,
            layout_.lifts().empty()?nullptr:&lifts_))return false;
        view.setBuildingRenderOwner(def_.sceneId,renderer_);
        return true;
    }
    template<class SceneView>
    bool retire(SceneView& view,std::string* error=nullptr){
        if(renderer_){
            view.setBuildingRenderOwner(def_.sceneId,std::shared_ptr<BuildingRenderer3DScene>{});
            if(!renderer_->shutdown(error))return false;
            renderer_.reset();
        }
        return true;
    }
    bool clear(){
        if(renderer_)return false;
        apertures_.clear();lifts_.clear();shell_.reset();layout_.clear();
        authoredPoses_.clear();view_.reset();prepared_=false;hash_=0;return true;
    }
    uint64_t contentHash()const noexcept{return hash_;}
    uint64_t commandEpoch()const noexcept{return view_.frame().epoch;}
    const BuildingReplicaSceneFrame& frame()const noexcept{return view_.frame();}
    // CPU readback for tests/tools; same poses used by Renderer3D.
    BuildingApertureVisualFrame sampleApertures(uint32_t ms){return apertures_.interpolate(ms);}
private:
    static bool fail(std::string* error,const std::string& message){if(error)*error=message;return false;}
    Definition def_;
    uint64_t hash_=0;
    float width_=0,depth_=0;
    bool prepared_=false;
    std::vector<BuildingAperturePose> authoredPoses_;
    BuildingCirculationHost layout_;
    BuildingReplicaView view_;
    BuildingAperturePresentation apertures_;
    BuildingLiftRenderState lifts_;
    std::unique_ptr<render::Model> shell_;
    std::shared_ptr<BuildingRenderer3DScene> renderer_;
};
}
