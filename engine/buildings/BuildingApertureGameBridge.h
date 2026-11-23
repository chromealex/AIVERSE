#pragma once
// AIVERSE procedural building command-to-render boundary. This is a native
// owner-phase adapter, not a new game scheduler, graphics device or network
// transport. Host supplies existing ECS, SpatialWorld and graph router.
#include "engine/buildings/BuildingAperturePresentation.h"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingApertureSceneTick {
    BuildingApertureFixedTickFrame authoritative;
    bool presentationAccepted=false;
    std::string error;
};

// This adapter is owned by one concrete game scene. The same scene must
// retain ECS World, SpatialWorld, graph router, aperture owner and its
// real renderer resources for at least as long as this bridge is active.
// Model pointers are passed only to the existing renderer resource owner.
class BuildingApertureGameBridge {
public:
    using SubmitLeaf=std::function<bool(
        const render::Model&,const BuildingApertureLeafPose&)>;

    bool attach(uint64_t sceneId,
                BuildingApertureFixedTickOwner::Authorize authorize,
                BuildingApertureFixedTickOwner::InteractionRange reachable,
                const BuildingApertureOwner& apertures,
                const BuildingStyle& style,std::string* error=nullptr){
        if(attached_||!apertures.attached()||sceneId==0)
            return fail(error,"Game bridge already active or aperture scene missing");
        std::string issue;
        if(!tick_.attach(sceneId,std::move(authorize),
                         std::move(reachable),&issue))
            return fail(error,issue);
        if(!presentation_.initialize(apertures.poses(),style,&issue)){
            tick_.detach();
            return fail(error,issue);
        }
        sceneId_=sceneId;attached_=true;
        if(error)error->clear();
        return true;
    }

    bool enqueue(const World& ecs,const BuildingApertureCommand& command,
                 std::string* error=nullptr){
        return attached_&&tick_.enqueue(ecs,command,error);
    }

    // Existing authoritative fixed-tick owner calls this after its actor
    // positions/obstruction sensors have been committed, once per tick.
    // Collision + graph publication occur inside the existing tick owner.
    // CPU presentation accepts only a fully acknowledged tick.
    BuildingApertureSceneTick fixedTick(
        World& ecs,BuildingApertureOwner& apertures,
        world::SpatialWorld& spatial,BuildingGraphChangeRouter& router,
        uint64_t serverTick){
        BuildingApertureSceneTick result;
        if(!attached_||!apertures.attached()){
            result.error="Inactive building aperture game bridge";
            return result;
        }
        result.authoritative=tick_.advance(
            ecs,apertures,spatial,router,serverTick);
        if(!result.authoritative.complete){
            result.error=result.authoritative.error;
            return result;
        }
        result.presentationAccepted=presentation_.accept(
            result.authoritative,&result.error);
        return result;
    }

    // Invoked by the existing render owner on its rendering phase.
    // Renderer3D upload/submit/fence lifetime is entirely in submit.
    // Incomplete or failed render submissions do not change physics.
    bool render(uint32_t renderDtMs,const SubmitLeaf& submit,
                std::string* error=nullptr){
        if(!attached_||!submit)
            return fail(error,"Renderer callback unavailable");
        const auto frame=presentation_.interpolate(renderDtMs);
        if(!frame.valid)return fail(error,frame.error);
        for(const auto& leaf:frame.leaves){
            const auto* model=presentation_.model(leaf.socketId);
            if(!model||!submit(*model,leaf))
                return fail(error,"Renderer rejected aperture leaf");
        }
        if(error)error->clear();
        return true;
    }

    void retireActor(Entity actor){tick_.retireActor(actor);}
    const render::Model* model(const std::string& socketId)const{
        return presentation_.model(socketId);
    }
    bool attached()const noexcept{return attached_;}
    uint64_t sceneId()const noexcept{return sceneId_;}
    void detach(){
        // Caller MUST release GPU leases/fences before destroying CPU Models.
        presentation_.clear();tick_.detach();
        attached_=false;sceneId_=0;
    }
private:
    static bool fail(std::string* error,const std::string& why){
        if(error)*error=why;
        return false;
    }
    BuildingApertureFixedTickOwner tick_;
    BuildingAperturePresentation presentation_;
    uint64_t sceneId_=0;
    bool attached_=false;
};

} // namespace aiverse::building
