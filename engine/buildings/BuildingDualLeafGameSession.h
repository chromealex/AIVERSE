#pragma once
// Genre-neutral world-owned dual leaf scene session. This is the FIXED TICK
// entry point for a game application's existing scheduler; it does not
// create a second ECS, network transport, physics world or render device.
#include "engine/buildings/BuildingDualLeafOwner.h"
#include "engine/buildings/BuildingAperturePresentation.h"
#include "engine/buildings/BuildingApertureFixedTick.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingDualLeafCommandResult {
    Entity actor{};
    uint64_t sequence=0;
    bool accepted=false;
    std::string error;
};
struct BuildingDualLeafTickFrame {
    uint64_t tick=0,revision=0;
    bool graphAcknowledged=false;
    bool presentationAccepted=false;
    bool moving=false;
    bool fullyOpen=false; // only true for a non-moving, fully open socket
    std::vector<BuildingDualLeafCommandResult> commands;
    std::string error;
};

// Host supplies the EXISTING world and owns its lifetime. All mutating
// operations run on the authoritative game thread only. Render code reads
// immutable CPU model pointers and pose snapshots through callback.
class BuildingDualLeafGameSession {
public:
    using Authorize=BuildingApertureFixedTickOwner::Authorize;
    using InteractionRange=BuildingApertureFixedTickOwner::InteractionRange;
    using SubmitLeaf=std::function<bool(
        const render::Model&,const BuildingApertureLeafPose&)>;

    bool attach(uint64_t sceneId,const BuildingDualLeafOwner& physics,
                const BuildingStyle& style,Authorize authorize,
                InteractionRange range,std::string* error=nullptr){
        if(sceneId_||!sceneId||!physics.attached()||physics.faulted()||
           !authorize||!range){
            if(error)*error="Invalid dual leaf scene or missing authoritative guards";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(presentationMutex_);
            if(!presentation_.initialize(physics.poses(),style,error))return false;
        }
        sceneId_=sceneId;
        authorize_=std::move(authorize);
        range_=std::move(range);
        if(error)error->clear();
        return true;
    }

    // Called after the existing authenticated transport has resolved the
    // requester's real ECS Entity. Enqueuing never mutates physics.
    bool enqueue(const World& ecs,const BuildingApertureCommand& command,
                 std::string* error=nullptr){
        if(!sceneId_||command.sceneId!=sceneId_||
           !command.actor.valid()||!ecs.alive(command.actor)||
           command.socketId.empty()||!command.sequence||
           !command.graphDomains||
           (command.graphDomains&~kBuildingGraphAll)){
            if(error)*error="Invalid dual leaf command envelope";
            return false;
        }
        auto id=command.actor.packed();
        if(accepted_.contains(id)&&command.sequence<=accepted_.at(id)){
            if(error)*error="Replayed actor command";
            return false;
        }
        for(const auto& queued:queue_)
            if(queued.actor==command.actor&&
               queued.sequence==command.sequence){
                if(error)*error="Duplicate queued actor sequence";
                return false;
            }
        if(queue_.size()>=1024){
            if(error)*error="Dual leaf command queue full";
            return false;
        }
        queue_.push_back(command);
        if(error)error->clear();
        return true;
    }

    // Motion is bounded to ONE physical substep per server tick, ensuring
    // NavSource receives the same paired real collider AABBs that drive
    // the visual instances. No fresh command is consumed during motion.
    BuildingDualLeafTickFrame fixedTick(
        World& ecs,BuildingDualLeafOwner& physics,
        world::SpatialWorld& spatial,BuildingGraphChangeRouter& router,
        uint64_t tick,uint16_t stepPermille=100){
        BuildingDualLeafTickFrame frame;
        frame.tick=tick;
        if(!sceneId_||tick<=lastTick_||!physics.attached()||
           physics.faulted()||stepPermille==0||stepPermille>250){
            frame.error="Dual leaf fixed tick invalid or physics faulted";
            return frame;
        }
        lastTick_=tick;
        if(physics.pendingGraphs()){
            const auto ack=physics.retry(router);
            if(!ack.graphsAcknowledged){
                frame.error=ack.error;
                frame.revision=physics.revision();
                return frame;
            }
        }
        if(physics.moving()){
            const auto step=physics.advance(spatial,router,stepPermille);
            if(!step.committed||!step.graphsAcknowledged){
                frame.error=step.error.empty()?
                    "Dual leaf motion blocked or graph owner unavailable":step.error;
                frame.revision=physics.revision();
                return frame;
            }
        }else{
            // At most one motion starts per tick. Recheck ECS lifetime,
            // ownership and interaction range in the simulation phase.
            while(!queue_.empty()){
                const auto cmd=queue_.front();
                queue_.erase(queue_.begin());
                BuildingDualLeafCommandResult result;
                result.actor=cmd.actor;result.sequence=cmd.sequence;
                if(!ecs.alive(cmd.actor)||cmd.sceneId!=sceneId_){
                    result.error="Actor retired or scene changed";
                }else if(accepted_.contains(cmd.actor.packed())&&
                         cmd.sequence<=accepted_.at(cmd.actor.packed())){
                    result.error="Actor command reordered";
                }else{
                    const auto poses=physics.poses();
                    const auto it=std::find_if(poses.begin(),poses.end(),
                        [&](const BuildingAperturePose& pose){
                            return pose.socketId==cmd.socketId;
                        });
                    if(it==poses.end())
                        result.error="Unknown aperture socket";
                    else if(!authorize_(cmd.actor,cmd.socketId,cmd.open)||
                            !range_(cmd.actor,it->bounds))
                        result.error="Actor not authorized or outside interaction range";
                    else{
                        std::string issue;
                        if(physics.begin(cmd.socketId,cmd.open,
                                         cmd.graphDomains,&issue)){
                            accepted_[cmd.actor.packed()]=cmd.sequence;
                            result.accepted=true;
                        }else result.error=issue;
                    }
                }
                frame.commands.push_back(std::move(result));
                if(frame.commands.back().accepted)break;
                if(frame.commands.size()>=64)break;
            }
        }
        frame.revision=physics.revision();
        frame.moving=physics.moving();
        frame.graphAcknowledged=!physics.pendingGraphs();
        if(!frame.graphAcknowledged){
            frame.error="Dual leaf graph publication still pending";
            return frame;
        }
        // This is a committed physics + graph snapshot. Presentation uses
        // exactly the authoritative physical permille, not a client clock.
        {
            std::lock_guard<std::mutex> lock(presentationMutex_);
            frame.presentationAccepted=presentation_.acceptPhysicalStep(
                physics.poses(),tick,frame.revision,&frame.error);
        }
        if(frame.presentationAccepted){
            const auto poses=physics.poses();
            frame.fullyOpen=!frame.moving&&!poses.empty()&&
                std::all_of(poses.begin(),poses.end(),
                    [](const BuildingAperturePose& pose){return pose.open;});
        }
        return frame;
    }

    // Called from the existing render owner, never the ECS simulation
    // thread. The submitted Model is a borrowed CPU pointer: renderer
    // manages actual GPU upload, cache leases and retirement fences.
    bool submit(uint32_t dtMs,const SubmitLeaf& render,
                std::string* error=nullptr){
        if(!sceneId_||!render){
            if(error)*error="Dual leaf renderer owner unavailable";
            return false;
        }
        // CPU visual state may be accepted on a distinct world thread.
        // Keep its immutable Model pointers and mutable pose interpolation
        // stable until the existing Renderer3D has copied this draw batch.
        std::lock_guard<std::mutex> lock(presentationMutex_);
        const auto frame=presentation_.interpolate(dtMs);
        if(!frame.valid){
            if(error)*error=frame.error;
            return false;
        }
        for(const auto& leaf:frame.leaves){
            const auto* model=presentation_.model(leaf.socketId);
            if(!model||!render(*model,leaf)){
                if(error)*error="Renderer rejected committed dual leaf pose";
                return false;
            }
        }
        if(error)error->clear();
        return true;
    }

    // One combined authored material+texture assignment BEFORE GPU upload.
    bool bindMaterial(const std::string& socketId,
                      const StyleMaterial& style,
                      const BuildingImageByGuid& resolve,
                      std::string* error=nullptr){
        std::lock_guard<std::mutex> lock(presentationMutex_);
        return presentation_.bindStyleMaterial(socketId,style,resolve,error);
    }

    void retireActor(Entity actor){
        if(!actor.valid())return;
        accepted_.erase(actor.packed());
        queue_.erase(std::remove_if(queue_.begin(),queue_.end(),
            [&](const auto& cmd){return cmd.actor==actor;}),
            queue_.end());
    }
    void detach(){
        // Caller retires GPU leases FIRST, then detaches physical owner.
        {
            std::lock_guard<std::mutex> lock(presentationMutex_);
            presentation_.clear();
        }
        accepted_.clear();queue_.clear();
        authorize_={};range_={};
        sceneId_=lastTick_=0;
    }
    bool attached()const noexcept{return sceneId_!=0;}
    size_t queued()const noexcept{return queue_.size();}
private:
    BuildingAperturePresentation presentation_;
    mutable std::mutex presentationMutex_;
    Authorize authorize_;
    InteractionRange range_;
    uint64_t sceneId_=0,lastTick_=0;
    std::map<uint64_t,uint64_t> accepted_;
    std::vector<BuildingApertureCommand> queue_;
};

} // namespace aiverse::building
