#pragma once
// Existing ECS/world-owner fixed-phase integration for interactive building
// apertures. No transport, graphics device, or second graph/physics owner.
// The host calls advance() only on its authoritative fixed tick.
#include "engine/buildings/BuildingApertureOwner.h"
#include "engine/core/ECS.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingApertureCommand {
    uint64_t sceneId=0;
    Entity actor{};
    uint64_t sequence=0;
    std::string socketId;
    bool open=false;
    uint32_t graphDomains=0;
};
struct BuildingApertureCommandOutcome {
    Entity actor{};
    uint64_t sequence=0;
    bool accepted=false;
    bool physicsCommitted=false;
    bool graphAccepted=false;
    std::string reason;
};
struct BuildingApertureFixedTickFrame {
    uint64_t tick=0;
    uint64_t revision=0;
    bool complete=false;
    std::vector<BuildingApertureCommandOutcome> commands;
    // Committed CPU states for the game render/network snapshot owner.
    // Must not be sent to clients as authoritative commands.
    std::vector<BuildingAperturePose> committedPoses;
    std::string error;
};

class BuildingApertureFixedTickOwner {
public:
    // Existing connection owner supplies the authenticated actor identity;
    // this interface does not parse client IDs from untrusted packets.
    using Authorize=std::function<bool(Entity,const std::string&,bool)>;
    using InteractionRange=std::function<bool(Entity,const world::Bounds&)>;
    bool attach(uint64_t sceneId,Authorize authorize,InteractionRange reachable,
                std::string* error=nullptr){
        if(sceneId==0||!authorize||!reachable||sceneId_!=0){
            if(error)*error="Aperture tick requires a unique scene and both authorization guards";
            return false;
        }
        sceneId_=sceneId;
        authorize_=std::move(authorize);
        reachable_=std::move(reachable);
        if(error)error->clear();
        return true;
    }
    // Queue only; never mutate collision in a network handler or on the
    // renderer thread. ECS World owns actor generation and its lifetime.
    bool enqueue(const World& ecs,const BuildingApertureCommand& command,
                 std::string* error=nullptr){
        if(sceneId_==0||command.sceneId!=sceneId_||
           !command.actor.valid()||!ecs.alive(command.actor)||
           !command.sequence||command.socketId.empty()||
           command.graphDomains==0||
           (command.graphDomains&~kBuildingGraphAll)!=0){
            if(error)*error="Invalid authenticated aperture command envelope";
            return false;
        }
        const uint64_t id=command.actor.packed();
        const auto it=lastAccepted_.find(id);
        if(it!=lastAccepted_.end()&&command.sequence<=it->second){
            if(error)*error="Replayed aperture command sequence";
            return false;
        }
        for(const auto& queued:queue_)
            if(queued.actor==command.actor&&
               queued.sequence==command.sequence){
                if(error)*error="Duplicate queued aperture command";
                return false;
            }
        if(queue_.size()>=kMaximumCommands){
            if(error)*error="Aperture command queue budget exceeded";
            return false;
        }
        queue_.push_back(command);
        if(error)error->clear();
        return true;
    }

    BuildingApertureFixedTickFrame advance(
        World& ecs,BuildingApertureOwner& apertures,
        world::SpatialWorld& spatial,BuildingGraphChangeRouter& router,
        uint64_t tick){
        BuildingApertureFixedTickFrame frame;
        frame.tick=tick;
        if(sceneId_==0||tick<=lastTick_||!apertures.attached()){
            frame.error="Aperture fixed tick is stale or scene detached";
            return frame;
        }
        lastTick_=tick;
        // A failed graph consumer never rolls back committed physics.
        // Retry that same revision before consuming new commands.
        if(apertures.hasPendingGraphs()){
            const auto retry=apertures.retryGraphPublication(router);
            if(!retry.graphsAcknowledged){
                frame.error=retry.error.empty()?
                    "Graph publication remains pending":retry.error;
                frame.revision=apertures.revision();
                return frame;
            }
        }
        // Preserves authoritative arrival order and places a strict
        // budget on per-tick collision/graph mutations.
        size_t handled=0;
        while(!queue_.empty()&&handled<kMaximumPerTick){
            auto cmd=std::move(queue_.front());
            queue_.erase(queue_.begin());
            ++handled;
            BuildingApertureCommandOutcome outcome;
            outcome.actor=cmd.actor;
            outcome.sequence=cmd.sequence;
            auto reject=[&](const char* reason){
                outcome.reason=reason;
                frame.commands.push_back(std::move(outcome));
            };
            if(cmd.sceneId!=sceneId_||!ecs.alive(cmd.actor)){
                reject("Actor retired or scene identity changed");
                continue;
            }
            // Check sequence AGAIN in the world-owner phase. A later
            // packet may have been queued before its earlier predecessor.
            const uint64_t id=cmd.actor.packed();
            if(lastAccepted_.contains(id)&&
               cmd.sequence<=lastAccepted_.at(id)){
                reject("Out-of-order authenticated actor command");
                continue;
            }
            const auto poses=apertures.poses();
            const auto it=std::find_if(poses.begin(),poses.end(),
                [&](const BuildingAperturePose& pose){
                    return pose.socketId==cmd.socketId;
                });
            if(it==poses.end()){
                reject("Unknown aperture socket");
                continue;
            }
            if(!authorize_(cmd.actor,cmd.socketId,cmd.open)||
               !reachable_(cmd.actor,it->bounds)){
                reject("Actor not authorized or not at the physical interaction zone");
                continue;
            }
            const auto result=apertures.setOpen(
                cmd.socketId,cmd.open,spatial,router,cmd.graphDomains);
            outcome.physicsCommitted=result.collisionCommitted;
            outcome.graphAccepted=result.graphsAcknowledged;
            // Once physics is committed it must never be replayed by a
            // duplicate command, even if a graph callback was unavailable.
            if(result.collisionCommitted){
                lastAccepted_[id]=cmd.sequence;
                outcome.accepted=true;
            }
            outcome.reason=result.error;
            frame.commands.push_back(std::move(outcome));
            if(result.collisionCommitted&&!result.graphsAcknowledged){
                frame.error=result.error.empty()?
                    "Committed collision; graph owners pending":result.error;
                break;
            }
        }
        frame.revision=apertures.revision();
        if(!apertures.hasPendingGraphs()){
            frame.committedPoses=apertures.poses();
            frame.complete=true;
        }else if(frame.error.empty())
            frame.error="Graph consumers have not acknowledged latest physical state";
        return frame;
    }

    // Game ECS logout/despawn owner calls before generation reuse. This is
    // not a network-transport revocation: that remains externally owned.
    void retireActor(Entity actor){
        if(!actor.valid())return;
        lastAccepted_.erase(actor.packed());
        queue_.erase(std::remove_if(queue_.begin(),queue_.end(),
            [&](const auto& command){return command.actor==actor;}),
            queue_.end());
    }
    void detach(){
        queue_.clear();lastAccepted_.clear();
        authorize_={};reachable_={};sceneId_=0;lastTick_=0;
    }
    static constexpr size_t kMaximumCommands=1024;
    static constexpr size_t kMaximumPerTick=64;
    size_t queued()const noexcept{return queue_.size();}
private:
    uint64_t sceneId_=0,lastTick_=0;
    Authorize authorize_;
    InteractionRange reachable_;
    std::map<uint64_t,uint64_t> lastAccepted_;
    std::vector<BuildingApertureCommand> queue_;
};

} // namespace aiverse::building
