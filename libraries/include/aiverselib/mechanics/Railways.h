#pragma once
#include "engine/mechanics/Namespace.h"
// railways: trains on a track graph with block signalling (OpenTTD / Factorio style).
// Track segments have a length, a block id and optional station; a block holds at most one train. Trains follow a
// looping route of segments, accelerate and brake with fixed rates, reserve the next block before entering it (path
// signals: a train waits at the signal when the block is reserved), stop at stations on their route for a dwell time
// and report Arrived/Departed so the owner loads and unloads cargo. Deterministic: trains are processed in ascending id; reservations are
// granted to the lowest id first. Fixed math; record kind 213.
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
struct RailSegment {int64_t id=0,block=0,station=0;Fixed length=10_fx,speedLimit=30_fx;};
struct RailNetwork {std::map<int64_t,RailSegment> segments;};
struct TrainDef {int64_t id=0;std::vector<int64_t> route;Fixed maxSpeed=25_fx,accel=1_fx,brake=2_fx;uint32_t dwellTicks=120;};
struct TrainState {std::size_t leg=0;Fixed offset{},speed{};uint32_t dwellUntil=0;int64_t reserved=0;bool stopped=false,waiting=false,served=false;};   // served: station of this leg done
struct RailState {std::map<int64_t,TrainState> trains;std::map<int64_t,int64_t> blockOwner;uint32_t lastTick=0;bool initialized=false;};   // block -> train
enum class RailEventKind:uint8_t {Arrived,Departed,WaitingAtSignal,EnteredBlock};
struct RailEvent {RailEventKind kind{};int64_t train=0,segment=0,station=0;};
struct RailReport {std::vector<RailEvent> events;std::map<int64_t,std::pair<int64_t,Fixed>> positions;};   // train -> (segment, offset)
inline bool valid(const RailNetwork& n){for(auto& [id,s]:n.segments)if(id!=s.id||id<=0||s.block<=0||s.length<=Fixed{}||s.speedLimit<=Fixed{})return false;return true;}
inline bool valid(const RailNetwork& n,const std::map<int64_t,TrainDef>& t){
    if(!valid(n))return false;for(auto& [id,d]:t){if(id!=d.id||id<=0||d.route.empty()||d.maxSpeed<=Fixed{}||d.accel<=Fixed{}||d.brake<=Fixed{})return false;for(auto s:d.route)if(!n.segments.contains(s))return false;}return true;}
// Place every train at the start of its first segment and reserve that block. Fails when two trains share a block.
inline std::optional<RailState> placeTrains(const RailNetwork& n,const std::map<int64_t,TrainDef>& trains){
    if(!valid(n,trains))return std::nullopt;RailState s;
    for(auto& [id,d]:trains){auto b=n.segments.at(d.route.front()).block;if(s.blockOwner.contains(b))return std::nullopt;s.blockOwner[b]=id;s.trains[id]={0,{},{},0,b,false,false,false};}
    return s;
}
inline std::optional<RailReport> stepRail(const RailNetwork& n,const std::map<int64_t,TrainDef>& trains,RailState& state,uint32_t tick,Fixed dt){
    if(!valid(n,trains)||!policyDtOk(dt)||!policyTickOk(state.initialized,state.lastTick,tick))return std::nullopt;
    for(auto& [id,t]:state.trains)if(!trains.contains(id)||t.leg>=trains.at(id).route.size())return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;RailReport r;
    for(auto& [id,t]:s.trains){const auto& d=trains.at(id);
        auto segId=d.route[t.leg];const auto& seg=n.segments.at(segId);
        if(t.stopped){if(tick<t.dwellUntil){r.positions[id]={segId,t.offset};continue;}t.stopped=false;r.events.push_back({RailEventKind::Departed,id,segId,seg.station});}
        auto nextLeg=(t.leg+1)%d.route.size();const auto& next=n.segments.at(d.route[nextLeg]);
        bool needBlock=next.block!=seg.block;bool canEnter=!needBlock||s.blockOwner.find(next.block)==s.blockOwner.end()||s.blockOwner.at(next.block)==id;
        auto remaining=seg.length-t.offset;
        bool stopHere=seg.station!=0&&!t.served;                                     // stop once at the end of a station segment
        auto limit=fxMin(d.maxSpeed,seg.speedLimit);
        bool mustStop=stopHere||!canEnter;
        auto brakingDist=t.speed*t.speed/(d.brake*2);
        if(mustStop&&remaining<=brakingDist+t.speed*dt)t.speed=fxMax(Fixed{},t.speed-d.brake*dt);else t.speed=fxMin(limit,t.speed+d.accel*dt);
        if(!canEnter&&!t.waiting&&remaining<=brakingDist+t.speed*dt){t.waiting=true;r.events.push_back({RailEventKind::WaitingAtSignal,id,segId,0});}
        auto move=t.speed*dt;
        if(move>=remaining){
            if(stopHere){t.offset=seg.length;t.speed={};t.stopped=true;t.served=true;t.dwellUntil=tick+d.dwellTicks;
                r.events.push_back({RailEventKind::Arrived,id,segId,seg.station});r.positions[id]={segId,t.offset};continue;}
            if(!canEnter){t.offset=seg.length;t.speed={};r.positions[id]={segId,t.offset};continue;}
            t.waiting=false;t.offset=move-remaining;
            if(needBlock){s.blockOwner[next.block]=id;s.blockOwner.erase(seg.block);r.events.push_back({RailEventKind::EnteredBlock,id,next.id,0});}
            t.leg=nextLeg;t.served=false;if(t.offset>next.length)t.offset=next.length;
        }else t.offset+=move;
        r.positions[id]={d.route[t.leg],t.offset};
    }
    state=std::move(s);return r;
}
inline void io(Archive& a,TrainState& t){uint64_t leg=t.leg;a(leg,t.offset,t.speed,t.dwellUntil,t.reserved,t.stopped,t.waiting,t.served);t.leg=std::size_t(leg);}
inline void io(Archive& a,RailState& s){a(s.trains,s.blockOwner,s.lastTick,s.initialized);}
inline uint16_t stateKind(const RailState&){return 213;}
inline bool valid(const RailState& s){if(!policyPositiveKeys(s.trains)||!policyPositiveKeys(s.blockOwner))return false;for(auto& [b,t]:s.blockOwner)if(!s.trains.contains(t))return false;return true;}
}
