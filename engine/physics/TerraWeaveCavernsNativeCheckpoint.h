#pragma once
// Native checkpoint for one airborne/volume actor over static terrain.
// Existing Simulation owns persistent ECS. No native handles, GPU objects,
// pointer identities, or process-local physics ticks are serialized.
#include "engine/physics/TerraWeaveCavernsPlanetMotorDispatch.h"
#include "engine/core/Archive.h"
#include "engine/core/Hash.h"
#include <bit>
#include <cmath>
#include <optional>
#include <string>

namespace aiverse::terraweave::caverns {
struct CaveNativeAirborneCheckpoint {
    Entity actor{};
    uint32_t simulationTick=0;
    uint64_t checksum=0;
    physics::CharacterState controller;
    b3Quat orientation=b3Quat_identity;
    std::string simulation;
};
inline constexpr size_t kCaveNativeCheckpointMaxBytes=64*1024*1024;
inline bool validCaveNativeCheckpoint(const CaveNativeAirborneCheckpoint& c){
    const auto finite=[](float f,float limit){return std::isfinite(f)&&std::abs(f)<=limit;};
    const auto vec=[&](b3Vec3 p,float limit){return finite(p.x,limit)&&finite(p.y,limit)&&finite(p.z,limit);};
    if(c.simulation.empty()||c.simulation.size()>kCaveNativeCheckpointMaxBytes||
       c.controller.grounded||c.controller.ground!=physics::kNoBody||
       !vec(c.controller.position,100000)||!vec(c.controller.velocity,1000)||
       !finite(c.controller.yaw,100000)||!finite(c.controller.groundTurn,100000))return false;
    const auto q=c.orientation;
    return vec(q.v,1.001f)&&finite(q.s,1.001f)&&b3IsValidQuat(q);
}
inline void caveNativeCheckpointFloat(Archive& a,float& value){
    uint32_t bits=a.writing()?std::bit_cast<uint32_t>(value):0;
    a(bits);if(!a.writing())value=std::bit_cast<float>(bits);
}
inline void caveNativeCheckpointVector(Archive& a,b3Vec3& p){
    caveNativeCheckpointFloat(a,p.x);caveNativeCheckpointFloat(a,p.y);caveNativeCheckpointFloat(a,p.z);
}
inline void io(Archive& a,CaveNativeAirborneCheckpoint& c){
    uint32_t magic=0x43565031u,version=1;
    a(magic,version);
    if(magic!=0x43565031u||version!=1){a.fail();return;}
    a(c.actor,c.simulationTick,c.checksum);
    caveNativeCheckpointVector(a,c.controller.position);
    caveNativeCheckpointVector(a,c.controller.velocity);
    caveNativeCheckpointFloat(a,c.controller.yaw);
    caveNativeCheckpointFloat(a,c.controller.groundTurn);
    caveNativeCheckpointVector(a,c.orientation.v);
    caveNativeCheckpointFloat(a,c.orientation.s);
    // Ground body is deliberately not serialized. This checkpoint only
    // supports airborne/swimming actors without moving support handles.
    if(!a.writing()){c.controller.grounded=false;c.controller.ground=physics::kNoBody;}
    uint32_t length=a.writing()?uint32_t(c.simulation.size()):0;
    if(a.writing()&&!validCaveNativeCheckpoint(c)){a.fail();return;}
    a(length);
    if(!a.ok()||!length||length>kCaveNativeCheckpointMaxBytes||
       (!a.writing()&&length>a.remaining())){a.fail();return;}
    if(!a.writing())c.simulation.resize(length);
    a.block(c.simulation.data(),length);
    if(!validCaveNativeCheckpoint(c))a.fail();
}
inline std::string encodeCaveNativeCheckpoint(CaveNativeAirborneCheckpoint checkpoint){
    std::string bytes;Archive a(&bytes);a(checkpoint);
    if(!a.ok())return {};
    Hasher hash;hash.bytes(bytes.data(),bytes.size());auto checksum=hash.value();a(checksum);
    return a.ok()?bytes:std::string{};
}
inline std::optional<CaveNativeAirborneCheckpoint> decodeCaveNativeCheckpoint(const std::string& bytes){
    if(bytes.size()<8||bytes.size()>kCaveNativeCheckpointMaxBytes+256)return {};
    Hasher hash;hash.bytes(bytes.data(),bytes.size()-8);
    CaveNativeAirborneCheckpoint result;Archive a(bytes);a(result);
    uint64_t expected=0;a(expected);
    if(!a.ok()||a.remaining()||expected!=hash.value())return {};
    return result;
}
inline bool caveNativeProxyPositionMatches(const physics::Scene& scene,physics::BodyHandle proxy,
        const physics::CharacterState& controller){
    if(!scene.valid(proxy)||scene.motion(proxy)!=physics::Motion::Kinematic)return false;
    Vec3 actual,desired;
    return physics::physisVec3(scene.position(proxy),actual)&&physics::physisVec3(controller.position,desired)&&
        (actual-desired).length()<=Fixed::fromRatio(1,100);
}
// Call after the single Scene step, under the actual exclusive barrier.
// Caller must have no pending forces/targets or external dynamic actors.
inline std::optional<CaveNativeAirborneCheckpoint> captureCaveNativeCheckpoint(
        Simulation& sim,const physics::Scene& scene,Entity actor,physics::BodyHandle proxy,
        const physics::CharacterState& controller,const CaveSimulationPosition& position){
    if(!sim.world().alive(actor)||!caveNativeProxyPositionMatches(scene,proxy,controller)||
       !cavePlanetDispatchPositionMatches(position,sim.world(),actor,controller))return {};
    CaveNativeAirborneCheckpoint c;c.actor=actor;c.simulationTick=sim.tick();c.checksum=sim.checksum();
    c.controller=controller;c.orientation=scene.rotation(proxy);c.simulation=sim.serialize();
    if(!validCaveNativeCheckpoint(c)||sim.checksum()!=c.checksum||sim.tick()!=c.simulationTick)return {};
    return c;
}
// Explicit restore-only teleport, never a movement/crossing shortcut.
// Kinematic proxy drive velocity is transient: clear it while rebuilding
// coverage. Controller's own velocity is restored for the next motor step.
inline bool restoreCaveNativeAirborneProxy(physics::Scene& scene,physics::BodyHandle proxy,
        const CaveNativeAirborneCheckpoint& checkpoint,physics::CharacterState& controller){
    if(!validCaveNativeCheckpoint(checkpoint)||!scene.valid(proxy)||
       scene.motion(proxy)!=physics::Motion::Kinematic)return false;
    if(!scene.moveCharacterOriented(proxy,checkpoint.controller,checkpoint.orientation,true)||
       !scene.setLinearVelocity(proxy,{0,0,0})||!scene.setAngularVelocity(proxy,{0,0,0}))return false;
    if(!caveNativeProxyPositionMatches(scene,proxy,checkpoint.controller))return false;
    const auto actual=scene.rotation(proxy),wanted=checkpoint.orientation;
    const float alignment=actual.v.x*wanted.v.x+actual.v.y*wanted.v.y+actual.v.z*wanted.v.z+actual.s*wanted.s;
    if(!std::isfinite(alignment)||std::abs(alignment)<.9999f)return false;
    controller=checkpoint.controller;return true;
}
} // namespace aiverse::terraweave::caverns
