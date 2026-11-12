#pragma once
#include "Locomotion.h"
#include "Melee.h"
#include "Weapons.h"
#include "Match.h"
#include "Narrative.h"
#include "engine/core/Archive.h"
#include "../runtime/NativeBytes.h"
namespace aiverse::lib::genre {
inline void io(Archive& a,MotionState& s){a(s.velocity,s.lastTick,s.lastGround,s.jumpTick,s.jumps,s.initialized,s.hasGround,s.buffered);}
inline void io(Archive& a,MeleeState& s){a(s.attack,s.queued,s.started,s.queuedAt,s.lastTick,s.initialized,s.hitTargets);}
inline void io(Archive& a,RecoilState& s){a(s.yaw,s.pitch);}
inline void io(Archive& a,ProjectileState& s){a(s.position,s.velocity,s.age,s.bounces,s.alive);}
inline void io(Archive& a,TeamScore& s){a(s.score,s.wins,s.lastEvent);}
inline void io(Archive& a,MatchState& s){a(s.teams,s.phase,s.phaseStart,s.lastTick,s.round,s.winner,s.initialized);}
inline void io(Archive& a,CaptureState& s){a(s.owner,s.candidate,s.progress,s.lastTick,s.initialized);}
inline void io(Archive& a,StoryState& s){a(s.node,s.facts,s.visits,s.lastCommand,s.complete);}
inline bool bounded(Vec3 p,Fixed maximum){return p.x>= -maximum&&p.x<=maximum&&p.y>= -maximum&&p.y<=maximum&&p.z>= -maximum&&p.z<=maximum;}
inline bool valid(const MotionState& s){return bounded(s.velocity,1000_fx)&&s.jumps<=16&&(!s.hasGround||s.lastGround<=s.lastTick)&&(!s.buffered||s.jumpTick<=s.lastTick);}
inline bool valid(const MeleeState& s){return s.attack>=0&&s.queued>=0&&s.started<=s.lastTick&&s.queuedAt<=s.lastTick&&s.hitTargets.size()<=4096&&std::is_sorted(s.hitTargets.begin(),s.hitTargets.end())&&std::adjacent_find(s.hitTargets.begin(),s.hitTargets.end())==s.hitTargets.end()&&(s.hitTargets.empty()||s.hitTargets.front()>0);}
inline bool valid(const RecoilState& s){return s.yaw>=Fixed::fromRatio(-1,4)&&s.yaw<=Fixed::fromRatio(1,4)&&s.pitch>=Fixed::fromRatio(-1,4)&&s.pitch<=Fixed::fromRatio(1,4);}
inline bool valid(const ProjectileState& s){return bounded(s.position,100000_fx)&&bounded(s.velocity,1000_fx)&&s.age<=100000&&s.bounces<=32;}
inline bool valid(const MatchState& s){
    if(s.teams.size()<2||s.teams.size()>200||uint8_t(s.phase)>uint8_t(MatchPhase::Finished)||s.phaseStart>s.lastTick||!s.round||s.round>1000||(s.winner>0&&!s.teams.contains(s.winner))||s.winner< -1)return false;
    for(auto [team,score]:s.teams)if(team<=0||score.score<0||score.wins>1000)return false;
    return true;
}
inline bool valid(const CaptureState& s){return s.owner>=0&&s.candidate>=0&&s.progress<=10000000;}
inline bool valid(const StoryState& s){
    if(s.node<0||s.facts.size()>10000||s.visits.size()>10000||(s.complete&&s.node!=0))return false;
    for(auto [key,value]:s.facts){(void)value;if(key<=0)return false;}for(auto [key,value]:s.visits)if(key<=0||value<1)return false;return true;
}
inline uint16_t stateKind(const MotionState&){return 1;}
inline uint16_t stateKind(const MeleeState&){return 2;}
inline uint16_t stateKind(const RecoilState&){return 3;}
inline uint16_t stateKind(const ProjectileState&){return 4;}
inline uint16_t stateKind(const MatchState&){return 5;}
inline uint16_t stateKind(const CaptureState&){return 6;}
inline uint16_t stateKind(const StoryState&){return 7;}
template<class State> std::string save(State s){
    if(!valid(s))return {};
    std::string bytes;
    Archive a(&bytes);
    uint32_t version=1;
    auto kind=stateKind(s);
    a(version,kind,s);
    return bytes;
}
template<class State> bool load(const std::string& bytes,State& state){
    if(bytes.size()>1024*1024)return false;
    Archive a(bytes);
    uint32_t version=0;
    uint16_t kind=0;
    State copy;
    a(version,kind);
    if(version!=1||kind!=stateKind(copy))return false;
    a(copy);
    if(!a.ok()||a.remaining()||version!=1||!valid(copy)||save(copy)!=bytes)return false;
    state=std::move(copy);
    return true;
}
using runtime::storeBytes;using runtime::loadBytes;
}
