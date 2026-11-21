// Fixed-point root-motion request + authoritative collision-motor adapter.
// Use OFFLINE quantized samples. Never sample float AnimClip inside Simulation.
#pragma once
#include "engine/core/FixedMath.h"
#include "engine/sim/CommonComponents.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::lib::animation {
struct RootMotionFrame { Vec3 local{}; Fixed yawTurns{}; };
struct RootMotionTrack {
    uint32_t id=0,rate=60;
    bool loop=false;
    std::vector<RootMotionFrame> frames;
};
struct RootMotionTracksResource { std::vector<RootMotionTrack> clips; };
struct RootMotionIntentComponent {
    uint32_t clipId=0,sequence=0; // increment sequence to restart
    bool enabled=false;
};
struct RootMotionStateComponent {
    uint32_t clipId=0,sequence=0,frame=0;
    bool finished=false;
};
struct RootMotionResultComponent {
    Vec3 requested{},actual{};
    Fixed yaw{};
    uint32_t tick=0;
    bool valid=false,blocked=false;
};
inline void io(Archive& a,RootMotionFrame& f) {a(f.local,f.yawTurns);}
inline void io(Archive& a,RootMotionTrack& t) {a(t.id,t.rate,t.loop,t.frames);}
inline void io(Archive& a,RootMotionTracksResource& t) {a(t.clips);}
inline void io(Archive& a,RootMotionIntentComponent& t) {a(t.clipId,t.sequence,t.enabled);}
inline void io(Archive& a,RootMotionStateComponent& t) {a(t.clipId,t.sequence,t.frame,t.finished);}
inline void io(Archive& a,RootMotionResultComponent& t) {a(t.requested,t.actual,t.yaw,t.tick,t.valid,t.blocked);}
inline bool validRootTrack(const RootMotionTrack& t,uint32_t rate) {
    if (!t.id || t.rate!=rate || t.frames.empty() || t.frames.size()>65536) return false;
    for (const auto& f:t.frames)
        if (fxAbs(f.local.x)>1000_fx || fxAbs(f.local.y)>1000_fx ||
            fxAbs(f.local.z)>1000_fx || fxAbs(f.yawTurns)>Fixed::fromRatio(1,2)) return false;
    return true;
}
inline const RootMotionTrack* findRootTrack(const RootMotionTracksResource& lib,uint32_t id) {
    for (const auto& t:lib.clips) if (t.id==id)return &t;
    return nullptr;
}
inline Vec3 rotateRootMotion(Vec3 v,Fixed yaw) {
    const Fixed c=fxCosTurns(yaw),s=fxSinTurns(yaw);
    return {v.x*c+v.z*s,v.y,v.z*c-v.x*s};
}
struct RootMotorResult { Vec3 translation{}; bool accepted=false; };
// Function Motor(Vec3 desired)->RootMotorResult. Must be deterministic and
// collision-aware, must NOT update the Transform itself. The caller owns the
// collision world; rejected movement does not change yaw. Time advances.
template<class Motor>
RootMotionResultComponent consumeRootMotion(const RootMotionTrack& clip,
        const RootMotionIntentComponent& intent,RootMotionStateComponent& state,
        Transform& transform,uint32_t rate,uint32_t tick,Motor&& motor) {
    RootMotionResultComponent output;output.tick=tick;
    if (!intent.enabled || intent.clipId!=clip.id || !validRootTrack(clip,rate))return output;
    if (state.clipId!=intent.clipId || state.sequence!=intent.sequence) {
        state={intent.clipId,intent.sequence,0,false};
    }
    if (state.finished)return output;
    const auto& frame=clip.frames[size_t(state.frame)];
    const Vec3 desired=rotateRootMotion(frame.local,transform.yaw);
    const RootMotorResult result=motor(desired);
    const bool safe=result.accepted &&
        result.translation.lengthSq()<=desired.lengthSq()+Fixed::fromRatio(1,1000);
    output.valid=true;output.requested=desired;output.blocked=!safe;
    if (safe) {
        transform.prevPos=transform.pos;
        transform.pos+=result.translation;
        transform.yaw+=frame.yawTurns;
        output.actual=result.translation;output.yaw=frame.yawTurns;
    }
    ++state.frame;
    if (state.frame>=clip.frames.size()) {
        if (clip.loop)state.frame=0;
        else state.finished=true;
    }
    return output;
}
// Register AFTER input/action validation and BEFORE downstream pose projection.
// Only Entities with all three required components are processed.
// ResultComponent is optional. Pass a deterministic collision callback taking
// (Simulation&,Entity,Vec3 desired) and returning RootMotorResult.
// Persistence: register the components and track resource in GameModule::setup.
template<class Motor>
void addRootMotionMotorSystem(Simulation& sim,std::string name,Motor motor) {
    sim.addSystem(std::move(name),[motor=std::move(motor)](Simulation& s) mutable {
        auto& w=s.world();
        auto* states=w.prepareExistingWrite<RootMotionStateComponent>();
        auto* transforms=w.prepareExistingWrite<Transform>();
        auto* results=w.prepareExistingWrite<RootMotionResultComponent>();
        if(!states || !transforms)return;
        const auto* tracks=static_cast<const World&>(w).resourceConst<RootMotionTracksResource>();
        if(!tracks)return;
        for(size_t i=0;i<states->size();++i) {
            const Entity e=states->entityAt(i);
            if(!w.alive(e))continue;
            const auto* intent=static_cast<const World&>(w).tryGetConst<RootMotionIntentComponent>(e);
            auto* transform=transforms->tryGet(e.index);
            if(!intent || !transform)continue;
            if(auto* result=results?results->tryGet(e.index):nullptr)
                *result=RootMotionResultComponent{};
            const auto* track=findRootTrack(*tracks,intent->clipId);
            if (!track)continue;
            auto outcome=consumeRootMotion(*track,*intent,states->dataAt(i),
                *transform,s.tickRate(),s.tick(),[&](Vec3 v){return motor(s,e,v);});
            if(auto* result=results?results->tryGet(e.index):nullptr)
                *result=outcome;
        }
    });
}
} // namespace aiverse::lib::animation
