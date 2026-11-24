#pragma once
// Genre-neutral room routing and authoritative integer-time lift state.
// Consumes BuildingStructureGraph room/connection/socket data; does NOT own
// gameplay ECS, PHYSIS, NavMesh GPU state, or platform collider handles.
// Publish output transitions on the existing physics/navigation owner phase.
#include "BuildingStructureGraph.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::systems {

enum class BuildingRouteAction : uint8_t {
    WalkThrough, ClimbStairs, RequestLift
};
struct BuildingRouteStep {
    std::string edgeId,fromRoom,toRoom;
    BuildingRouteAction action=BuildingRouteAction::WalkThrough;
};
struct BuildingRoomRoute {
    std::vector<BuildingRouteStep> steps;
    bool reachable=false;
    std::string error;
};

enum class BuildingLiftPhase : uint8_t {
    Open, Closing, Traveling, Opening, Offline
};
struct BuildingLiftSnapshot {
    uint32_t featureIndex=0;
    BuildingLiftPhase phase=BuildingLiftPhase::Open;
    uint32_t currentFloor=0,targetFloor=0;
    int64_t platformYmm=0;
    uint32_t phaseElapsedMs=0;
    uint64_t revision=1;
    uint32_t passengerCount=0;
    bool doorsOpen()const{return phase==BuildingLiftPhase::Open;}
    bool operator==(const BuildingLiftSnapshot&)const=default;
};
// Authoritative checkpoint, distinct from a network replica snapshot.
// Captured only after the physics owner consumed the pending movement.
struct BuildingLiftCheckpoint {
    BuildingLiftSnapshot state;
    int64_t travelRemainder=0;
    bool operator==(const BuildingLiftCheckpoint&)const=default;
};
struct BuildingLiftEvent {
    enum class Kind : uint8_t { DoorsClosed, Departed, Arrived, DoorsOpened };
    Kind kind=Kind::Arrived;
    uint32_t featureIndex=0,floor=0;
    uint64_t revision=0;
};
struct BuildingLiftPlatformMotion {
    uint32_t featureIndex=0;
    int64_t oldYmm=0,newYmm=0;
    float x0=0,z0=0,x1=0,z1=0,thickness=0.18f;
    uint64_t revision=0;
};
struct BuildingLiftPlatformPose {
    uint32_t featureIndex=0;
    int64_t yMm=0;
    float x0=0,z0=0,x1=0,z1=0;
    float thickness=0.18f;
    uint64_t revision=0;
};

// Immutable initialization from generated static graph. Movement progresses
// in integer millimetres + milliseconds; the game replicates snapshots to
// clients, never relies on per-platform float animation as authoritative.
class BuildingLiftController {
public:
    static constexpr int64_t kSpeedMmPerSecond=1400;
    static constexpr uint32_t kDoorTimeMs=650;
    static constexpr uint32_t kMaxPassengers=8;

    bool initialize(const BuildingStructureGraph& graph,
                    uint32_t featureIndex,std::string* error=nullptr) {
        auto fail=[&](const std::string& message){
            if(error)*error=message;
            return false;
        };
        std::map<uint32_t,int64_t> floors;
        for(const auto& socket:graph.sockets) {
            if(socket.kind!=BuildingFeatureKind::Elevator||
               socket.featureIndex!=featureIndex)continue;
            const int64_t mm=std::llround(double(socket.y)*1000.);
            if(!floors.emplace(socket.repeatIndex,mm).second)
                return fail("Duplicate lift landing floor");
        }
        if(floors.empty()||floors.begin()->first!=0)
            return fail("Lift has no ground floor socket");
        if(floors.size()>8||floors.rbegin()->first>=8)
            return fail("Too many lift landings");
        std::vector<int64_t> heights;
        heights.reserve(floors.size()+1);
        for(uint32_t i=0;i<=floors.rbegin()->first;++i){
            auto it=floors.find(i);
            if(it==floors.end())return fail("Lift landings are not consecutive");
            if(!heights.empty()&&it->second<=heights.back())
                return fail("Lift floor heights must rise strictly");
            heights.push_back(it->second);
        }
        // Vertical edges connect floor f to floor f+1; the highest floor
        // has no outgoing lift socket. Derive its actual landing from
        // the associated destination room's floor surface.
        const std::string topRoomPrefix="room/"+std::to_string(heights.size())+"/";
        int64_t topY=std::numeric_limits<int64_t>::min();
        for(const auto& room:graph.rooms)
            if(room.id.rfind(topRoomPrefix,0)==0) {
                const auto y=std::llround(double(room.y0)*1000.);
                if(topY==std::numeric_limits<int64_t>::min())topY=y;
                else if(topY!=y)return fail("Top lift floor has inconsistent height");
            }
        if(topY==std::numeric_limits<int64_t>::min()||
           topY<=heights.back())return fail("Lift missing upper landing");
        heights.push_back(topY);
        if(heights.size()<2||heights.size()>8)
            return fail("Lift needs 2 to 8 floors");

        bool platform=false;
        BuildingLiftPlatformPose pose;
        for(const auto& solid:graph.solids){
            if(solid.part!=BuildingGraphPart::ElevatorPlatform||
               solid.featureIndex!=featureIndex)continue;
            if(platform)return fail("More than one lift cabin platform");
            platform=true;
            pose={featureIndex,heights[0],solid.x0,solid.z0,
                  solid.x1,solid.z1,solid.y1-solid.y0,1};
        }
        if(!platform)return fail("Lift platform is missing");
        floorsMm_=std::move(heights);
        pendingMotion_.reset();
        travelRemainder_=0;
        state_={featureIndex,BuildingLiftPhase::Open,0,0,
                floorsMm_[0],0,1};
        pose_=pose;
        if(error)error->clear();
        return true;
    }

    const BuildingLiftSnapshot& snapshot()const noexcept{return state_;}
    const BuildingLiftPlatformPose& platformPose()const noexcept{return pose_;}
    uint32_t floorCount()const{return uint32_t(floorsMm_.size());}
    // Consumed by the existing ECS/physics owner, after one or more ticks.
    // A continuous series of movements coalesces to old -> newest pose.
    std::optional<BuildingLiftPlatformMotion> takePlatformMotion(){
        auto movement=std::move(pendingMotion_);
        pendingMotion_.reset();
        return movement;
    }
    bool captureCheckpoint(BuildingLiftCheckpoint& out)const{
        if(floorsMm_.empty()||pendingMotion_)return false;
        out={state_,travelRemainder_};return true;
    }
    // Restore into an initialized controller before registering its collider.
    // No synthetic motion or actor delta: the ECS checkpoint already contains
    // the actors at the saved physical height.
    bool restoreCheckpoint(const BuildingLiftCheckpoint& saved){
        const auto& s=saved.state;
        if(floorsMm_.empty()||pendingMotion_||s.featureIndex!=state_.featureIndex||
           !s.revision||s.revision==UINT64_MAX||
           s.currentFloor>=floorsMm_.size()||s.targetFloor>=floorsMm_.size()||
           s.phase>BuildingLiftPhase::Offline||s.passengerCount>kMaxPassengers||
           s.platformYmm<floorsMm_.front()||s.platformYmm>floorsMm_.back()||
           saved.travelRemainder<0||saved.travelRemainder>=1000)return false;
        const bool timed=s.phase==BuildingLiftPhase::Closing||s.phase==BuildingLiftPhase::Opening;
        if(timed?s.phaseElapsedMs>=kDoorTimeMs:s.phaseElapsedMs!=0)return false;
        if((s.phase==BuildingLiftPhase::Open||timed)&&
           s.platformYmm!=floorsMm_[s.currentFloor])return false;
        if((s.phase==BuildingLiftPhase::Open||s.phase==BuildingLiftPhase::Opening)&&
           s.currentFloor!=s.targetFloor)return false;
        state_=s;pose_.yMm=s.platformYmm;pose_.revision=s.revision;
        travelRemainder_=saved.travelRemainder;
        return true;
    }
    // Client-side replication accepts only a validated newer snapshot.
    // Authoritative simulation must never use remote snapshots.
    bool canApplyReplicatedSnapshot(const BuildingLiftSnapshot& snapshot)const{
        if(floorsMm_.empty()||snapshot.revision<=state_.revision||
           snapshot.featureIndex!=state_.featureIndex||
           snapshot.currentFloor>=floorsMm_.size()||
           snapshot.targetFloor>=floorsMm_.size()||
           snapshot.phase>BuildingLiftPhase::Offline||
           snapshot.phaseElapsedMs>kDoorTimeMs||
           snapshot.passengerCount>kMaxPassengers||
           snapshot.platformYmm<floorsMm_.front()||
           snapshot.platformYmm>floorsMm_.back())return false;
        // At an open/closing/opening station the cabin must already be
        // at the actual floor height. Never accept forged landing poses.
        if((snapshot.phase==BuildingLiftPhase::Open||
            snapshot.phase==BuildingLiftPhase::Closing||
            snapshot.phase==BuildingLiftPhase::Opening) &&
           snapshot.platformYmm!=floorsMm_[snapshot.currentFloor])return false;
        return true;
    }
    bool applyReplicatedSnapshot(const BuildingLiftSnapshot& snapshot){
        if(!canApplyReplicatedSnapshot(snapshot))return false;
        const int64_t old=state_.platformYmm;
        state_=snapshot;
        pose_.yMm=snapshot.platformYmm;
        pose_.revision=snapshot.revision;
        queueMotion(old,state_.platformYmm);
        return true;
    }
    bool online()const noexcept{
        return !floorsMm_.empty()&&state_.phase!=BuildingLiftPhase::Offline;
    }
    bool readyAt(uint32_t floor)const noexcept{
        return online()&&state_.phase==BuildingLiftPhase::Open&&
               state_.currentFloor==floor;
    }
    // Station call moves an EMPTY cabin to the pickup floor. Once boarded,
    // the rider calls a destination using requestDestination().
    bool board(uint32_t floor,uint32_t passengerCount=1){
        if(!readyAt(floor)||passengerCount==0||
           passengerCount>kMaxPassengers-state_.passengerCount)return false;
        state_.passengerCount+=passengerCount;
        touch();return true;
    }
    bool disembark(uint32_t floor,uint32_t passengerCount=1){
        if(!readyAt(floor)||passengerCount==0||
           passengerCount>state_.passengerCount)return false;
        state_.passengerCount-=passengerCount;
        touch();return true;
    }
    bool requestDestination(uint32_t destination){
        if(state_.passengerCount==0)return false;
        return beginTrip(destination);
    }
    // Authoritative door safety sensor: if a landing or cabin doorway
    // becomes obstructed while Closing, return to Open. The physics owner
    // must provide the actual occupant/proximity check; no geometry
    // generator can infer a live actor from the static StructureGraph.
    bool interruptClosing(){
        if(!online()||state_.phase!=BuildingLiftPhase::Closing)return false;
        state_.phase=BuildingLiftPhase::Open;
        state_.targetFloor=state_.currentFloor;
        state_.phaseElapsedMs=0;
        travelRemainder_=0;
        touch();return true;
    }
    bool call(uint32_t floor){
        // A building actor may summon the empty lift to a pickup floor,
        // but cannot redirect a cabin currently occupied by other actors.
        if(state_.passengerCount>0)return false;
        return beginTrip(floor);
    }
    void setOffline(bool value){
        if(floorsMm_.empty())return;
        if(value)state_.phase=BuildingLiftPhase::Offline;
        else {
            // A resumed lift cannot expose its doors while between floors.
            auto it=std::min_element(floorsMm_.begin(),floorsMm_.end(),
                [&](auto a,auto b){return std::llabs(a-state_.platformYmm)<
                                         std::llabs(b-state_.platformYmm);});
            state_.targetFloor=uint32_t(it-floorsMm_.begin());
            state_.phase=BuildingLiftPhase::Traveling;
        }
        state_.phaseElapsedMs=0;touch();
    }

    // One owner tick; max 1000ms per call prevents accidental time-jumps.
    // Events emitted at the exact state changes. No direct physics mutation.
    bool advance(uint32_t elapsedMs,std::vector<BuildingLiftEvent>* events=nullptr){
        if(!online()||elapsedMs==0||elapsedMs>1000)return false;
        bool changed=false;
        auto emit=[&](BuildingLiftEvent::Kind k){
            touch();changed=true;
            if(events)events->push_back({k,state_.featureIndex,
                state_.currentFloor,state_.revision});
        };
        uint32_t remaining=elapsedMs;
        while(remaining>0) {
            if(state_.phase==BuildingLiftPhase::Open)break;
            if(state_.phase==BuildingLiftPhase::Closing||
               state_.phase==BuildingLiftPhase::Opening){
                const uint32_t left=kDoorTimeMs-state_.phaseElapsedMs;
                const uint32_t take=std::min(left,remaining);
                remaining-=take;
                state_.phaseElapsedMs+=take;
                if(state_.phaseElapsedMs<kDoorTimeMs)break;
                state_.phaseElapsedMs=0;
                if(state_.phase==BuildingLiftPhase::Closing){
                    state_.phase=BuildingLiftPhase::Traveling;
                    emit(BuildingLiftEvent::Kind::DoorsClosed);
                    emit(BuildingLiftEvent::Kind::Departed);
                }else{
                    state_.phase=BuildingLiftPhase::Open;
                    emit(BuildingLiftEvent::Kind::DoorsOpened);
                }
                continue;
            }
            if(state_.phase==BuildingLiftPhase::Traveling){
                const int64_t target=floorsMm_[state_.targetFloor];
                const int64_t delta=target-state_.platformYmm;
                if(delta==0){
                    state_.currentFloor=state_.targetFloor;
                    state_.phase=BuildingLiftPhase::Opening;
                    emit(BuildingLiftEvent::Kind::Arrived);
                    continue;
                }
                // Retain mm rounding residual across variable frame sizes.
                const int64_t advanceNumerator=travelRemainder_+
                    int64_t(remaining)*kSpeedMmPerSecond;
                const int64_t step=std::max<int64_t>(1,advanceNumerator/1000);
                travelRemainder_=advanceNumerator%1000;
                const int64_t move=std::min<int64_t>(std::llabs(delta),step);
                const int64_t previous=state_.platformYmm;
                state_.platformYmm+=delta<0?-move:move;
                pose_.yMm=state_.platformYmm;
                touch();changed=true;
                queueMotion(previous,state_.platformYmm);
                remaining=0;
                if(state_.platformYmm==target){
                    state_.currentFloor=state_.targetFloor;
                    state_.phase=BuildingLiftPhase::Opening;
                    state_.phaseElapsedMs=0;
                    travelRemainder_=0;
                    emit(BuildingLiftEvent::Kind::Arrived);
                }
            }
        }
        return changed;
    }
private:
    bool beginTrip(uint32_t floor){
        if(!online()||floor>=floorsMm_.size()||
           (state_.phase!=BuildingLiftPhase::Open&&
            state_.phase!=BuildingLiftPhase::Opening))return false;
        if(readyAt(floor))return true;
        state_.targetFloor=floor;
        state_.phase=BuildingLiftPhase::Closing;
        state_.phaseElapsedMs=0;
        touch();
        return true;
    }
    void queueMotion(int64_t before,int64_t after){
        if(before==after)return;
        if(pendingMotion_){
            pendingMotion_->newYmm=after;
            pendingMotion_->revision=state_.revision;
            return;
        }
        pendingMotion_=BuildingLiftPlatformMotion{
            state_.featureIndex,before,after,pose_.x0,pose_.z0,
            pose_.x1,pose_.z1,pose_.thickness,state_.revision};
    }
    void touch(){
        ++state_.revision;
        pose_.revision=state_.revision;
    }
    std::vector<int64_t> floorsMm_;
    BuildingLiftSnapshot state_{};
    BuildingLiftPlatformPose pose_{};
    int64_t travelRemainder_=0;
    std::optional<BuildingLiftPlatformMotion> pendingMotion_;
};

struct BuildingRoutePolicy {
    bool allowStairs=true,allowElevators=true;
    // The graph only describes authored connections. A live navmesh may still
    // veto a corridor on collision/actor clearance grounds.
    bool requireAvailableLift=true;
};

// Room-level graph route (not a NavMesh movement path): preserves edge kind
// and correct action so clients cannot teleport up a staircase/elevator.
// Elevator requests are permitted only if its registered controller is online.
inline BuildingRoomRoute planBuildingRoomRoute(
    const BuildingStructureGraph& graph,
    const std::string& start,const std::string& goal,
    const BuildingRoutePolicy& policy={},
    const std::map<uint32_t,BuildingLiftController>* lifts=nullptr){
    BuildingRoomRoute result;
    if(start.empty()||goal.empty()||graph.rooms.empty()||!graph)
        {result.error="Invalid room route request";return result;}
    std::map<std::string,size_t> rooms;
    for(size_t i=0;i<graph.rooms.size();++i)
        if(!rooms.emplace(graph.rooms[i].id,i).second){
            result.error="Duplicate room ID";return result;
        }
    auto a=rooms.find(start),b=rooms.find(goal);
    if(a==rooms.end()||b==rooms.end()){
        result.error="Unknown room ID";return result;
    }
    if(start==goal){result.reachable=true;return result;}
    std::vector<std::vector<std::pair<size_t,size_t>>> adj(rooms.size());
    for(size_t i=0;i<graph.connections.size();++i){
        const auto& edge=graph.connections[i];
        if(edge.kind==BuildingConnectionKind::Stair&&!policy.allowStairs)continue;
        if(edge.kind==BuildingConnectionKind::Elevator){
            if(!policy.allowElevators)continue;
            if(policy.requireAvailableLift){
                // Stable vertical edge naming convention from Stage 13.
                if(edge.id.rfind("vertical/",0)!=0)continue;
                const auto slash=edge.id.find('/',9);
                if(slash==std::string::npos)continue;
                uint32_t feature=0;
                const auto token=edge.id.substr(9,slash-9);
                if(token.empty()||token.size()>9)continue;
                bool digits=true;
                for(char c:token)if(c<'0'||c>'9'){digits=false;break;}
                if(!digits)continue;
                for(char c:token)feature=feature*10u+uint32_t(c-'0');
                const auto it=lifts?lifts->find(feature):std::map<uint32_t,BuildingLiftController>::const_iterator{};
                if(!lifts||it==lifts->end()||!it->second.online())continue;
            }
        }
        auto from=rooms.find(edge.from),to=rooms.find(edge.to);
        if(from==rooms.end()||to==rooms.end())continue;
        adj[from->second].push_back({to->second,i});
        adj[to->second].push_back({from->second,i});
    }
    for(auto& row:adj)std::sort(row.begin(),row.end(),
        [&](auto x,auto y){
            if(x.first!=y.first)return x.first<y.first;
            return graph.connections[x.second].id<graph.connections[y.second].id;
        });
    std::vector<int64_t> parent(rooms.size(),-1),via(rooms.size(),-1);
    std::queue<size_t> q;
    q.push(a->second);
    parent[a->second]=int64_t(a->second);
    while(!q.empty()&&parent[b->second]<0){
        const size_t cur=q.front();q.pop();
        for(const auto& [next,idx]:adj[cur])if(parent[next]<0){
            parent[next]=int64_t(cur);
            via[next]=int64_t(idx);
            q.push(next);
        }
    }
    if(parent[b->second]<0){
        result.error="No available room connection";
        return result;
    }
    for(size_t cur=b->second;cur!=a->second;cur=size_t(parent[cur])){
        const auto& edge=graph.connections[size_t(via[cur])];
        const size_t previous=size_t(parent[cur]);
        const auto action=edge.kind==BuildingConnectionKind::Stair?
            BuildingRouteAction::ClimbStairs:
            edge.kind==BuildingConnectionKind::Elevator?
                BuildingRouteAction::RequestLift:
                BuildingRouteAction::WalkThrough;
        result.steps.push_back({edge.id,graph.rooms[previous].id,
            graph.rooms[cur].id,action});
    }
    std::reverse(result.steps.begin(),result.steps.end());
    result.reachable=true;
    return result;
}
} // namespace aiverse::lib::systems
