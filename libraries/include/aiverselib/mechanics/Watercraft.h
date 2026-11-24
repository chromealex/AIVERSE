#pragma once
#include "engine/mechanics/Namespace.h"
// watercraft: boats, ships and aircraft flight models next to vehicle-controllers (ground vehicles) and NEREID water.
// - Hulls are assembled from blocks (mass, displaced volume, position): assembleHull gives total mass, centre of mass,
//   inertia estimate and buoyancy sample points, so built ships (Stormworks / From the Depths style) and authored boats
//   use the same model. A damaged block can be removed and the hull reassembled.
// - Boat step: each sample point asks the owner for the water surface height and current there (NEREID query); the
//   submerged depth gives buoyancy, water drag acts against the relative velocity, thrust and rudder act at the stern
//   (rudder effect grows with speed), heave/pitch/roll come from the point imbalance with damping. Capsized and sunk
//   states are reported.
// - Aircraft step: lift and drag from angle of attack with a stall curve, control surfaces (elevator, aileron,
//   rudder), thrust, gravity; integrates pitch, roll, yaw and velocity. Arcade aircraft stay in vehicle-controllers.
// Fixed math, fixed dt; record kinds 220 (BoatState) and 221 (AircraftState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
struct HullBlock {int64_t id=0;Vec3 position{};Fixed mass=100_fx,volume=Fixed::fromRatio(1,4);};   // volume in m^3 of displacement
struct Hull {Fixed mass{};Vec3 centerOfMass{};Fixed inertia{};std::vector<Vec3> points;std::vector<Fixed> pointVolume;};
inline std::optional<Hull> assembleHull(std::span<const HullBlock> blocks){
    if(blocks.empty()||blocks.size()>4096)return std::nullopt;Hull h;Vec3 m{};
    for(auto& b:blocks){if(b.mass<=Fixed{}||b.volume<Fixed{}||!policyBounded(b.position,1000_fx))return std::nullopt;h.mass+=b.mass;m+=b.position*b.mass;}
    h.centerOfMass=m/h.mass;
    for(auto& b:blocks){auto d=b.position-h.centerOfMass;h.inertia+=b.mass*(d.x*d.x+d.z*d.z+Fixed::fromRatio(1,4));if(b.volume>Fixed{}){h.points.push_back(d);h.pointVolume.push_back(b.volume);}}
    return h;
}
struct BoatConfig {Fixed waterDensity=1000_fx,gravity=Fixed::fromRatio(98,10),linearDrag=Fixed::fromRatio(1,2),quadDrag=Fixed::fromRatio(1,10),angularDamping=2_fx,
    maxThrust=20000_fx,rudderTurn=Fixed::fromRatio(3,10),blockHeight=Fixed::fromRatio(1,2),capsizeRoll=Fixed::fromRatio(1,4);};
struct BoatState {Vec3 position{},velocity{};Fixed yaw{},pitch{},roll{},yawRate{},pitchRate{},rollRate{};uint32_t lastTick=0;bool capsized=false,sunk=false,initialized=false;};
struct BoatInput {Fixed throttle{},rudder{};};                                           // throttle -1..1, rudder -1..1
struct WaterSample {Fixed surface{};Vec3 current{};};
struct BoatReport {Fixed submergedMilli{};Vec3 buoyancy{};bool capsized=false,sunk=false;};
inline Vec3 rotateYPR(Vec3 v,Fixed yaw,Fixed pitch,Fixed roll){
    auto cr=fxCosTurns(roll),sr=fxSinTurns(roll),cp=fxCosTurns(pitch),sp=fxSinTurns(pitch),cy=fxCosTurns(yaw),sy=fxSinTurns(yaw);
    Vec3 a{v.x*cr-v.y*sr,v.x*sr+v.y*cr,v.z};Vec3 b{a.x,a.y*cp+a.z*sp,a.z*cp-a.y*sp};return {b.x*cy+b.z*sy,b.y,b.z*cy-b.x*sy};}
// water(point) -> WaterSample: surface height and current at a world point (NEREID query in the host).
template<class Water> std::optional<BoatReport> stepBoat(const BoatConfig& c,const Hull& hull,BoatState& state,const BoatInput& in,uint32_t tick,Fixed dt,Water&& water){
    if(!policyDtOk(dt)||!policyTickOk(state.initialized,state.lastTick,tick)||hull.points.empty()||hull.mass<=Fixed{}||fxAbs(in.throttle)>1_fx||fxAbs(in.rudder)>1_fx)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;BoatReport r;Vec3 force{0_fx,-c.gravity*hull.mass,0_fx};Fixed tPitch{},tRoll{};Fixed sub{},total{};Vec3 meanCurrent{};
    for(std::size_t i=0;i<hull.points.size();++i){auto wp=s.position+rotateYPR(hull.points[i],s.yaw,s.pitch,s.roll);WaterSample w=water(wp);
        auto depth=fxClamp(w.surface-(wp.y-c.blockHeight/2),Fixed{},c.blockHeight);auto frac=depth/c.blockHeight;auto vol=hull.pointVolume[i]*frac;
        auto lift=vol*c.waterDensity*c.gravity;force.y+=lift;tPitch+=-lift*hull.points[i].z;tRoll+=lift*hull.points[i].x;
        sub+=vol;total+=hull.pointVolume[i];meanCurrent+=w.current*frac;}
    auto wet=total>Fixed{}?sub/total:Fixed{};r.submergedMilli=wet*1000;r.buoyancy={{},force.y+c.gravity*hull.mass,{}};
    if(wet>Fixed{}){auto rel=s.velocity-meanCurrent/Fixed::fromInt(int64_t(hull.points.size()));auto sp=rel.length();
        force+=rel*(-(c.linearDrag+c.quadDrag*sp)*hull.mass*wet);
        Vec3 fwd{fxSinTurns(s.yaw),{},fxCosTurns(s.yaw)};force+=fwd*(in.throttle*c.maxThrust*wet);
        auto fwdSpeed=s.velocity.dot(fwd);s.yawRate+=in.rudder*c.rudderTurn*fwdSpeed*dt*wet/Fixed::fromInt(4);}
    s.velocity+=force/hull.mass*dt;s.position+=s.velocity*dt;
    auto inertia=fxMax(hull.inertia,1_fx);
    s.pitchRate+=(tPitch/inertia)*dt/Fixed::fromInt(50);s.rollRate+=(tRoll/inertia)*dt/Fixed::fromInt(50);   // turns per second
    for(Fixed* rate:{&s.pitchRate,&s.rollRate,&s.yawRate})*rate=*rate*fxMax(Fixed{},1_fx-c.angularDamping*dt);
    s.pitch=fxClamp(s.pitch+s.pitchRate*dt,-Fixed::fromRatio(1,4),Fixed::fromRatio(1,4));s.roll=fxClamp(s.roll+s.rollRate*dt,-Fixed::fromRatio(1,2),Fixed::fromRatio(1,2));
    s.yaw=s.yaw+s.yawRate*dt;if(s.yaw>1_fx)s.yaw-=1_fx;if(s.yaw<Fixed{})s.yaw+=1_fx;
    s.capsized=fxAbs(s.roll)>=c.capsizeRoll;s.sunk=wet>=1_fx&&force.y<Fixed{};r.capsized=s.capsized;r.sunk=s.sunk;
    if(!policyBounded(s.position)||!policyBounded(s.velocity,kPolicyMaxSpeed))return std::nullopt;
    state=s;return r;
}
inline void io(Archive& a,BoatState& s){a(s.position,s.velocity,s.yaw,s.pitch,s.roll,s.yawRate,s.pitchRate,s.rollRate,s.lastTick,s.capsized,s.sunk,s.initialized);}
inline uint16_t stateKind(const BoatState&){return 220;}
inline bool valid(const BoatState& s){return policyBounded(s.position)&&policyBounded(s.velocity,kPolicyMaxSpeed);}

// ---- Aircraft.
struct AircraftConfig {Fixed mass=1200_fx,wingArea=16_fx,airDensity=Fixed::fromRatio(6,5),liftSlope=Fixed::fromRatio(11,2),stallAlpha=Fixed::fromRatio(1,24),   // CL per turn^-1 scaled below
    zeroLiftDrag=Fixed::fromRatio(3,100),inducedDrag=Fixed::fromRatio(5,100),maxThrust=6000_fx,pitchRate=Fixed::fromRatio(1,4),rollRate=Fixed::fromRatio(1,2),yawRate=Fixed::fromRatio(1,8),
    gravity=Fixed::fromRatio(98,10),groundY{};};
struct AircraftState {Vec3 position{},velocity{0_fx,{},30_fx};Fixed yaw{},pitch{},roll{};uint32_t lastTick=0;bool stalled=false,onGround=false,initialized=false;};
struct AircraftInput {Fixed throttle{},elevator{},aileron{},rudder{};};                  // -1..1 (throttle 0..1)
struct AircraftReport {Fixed alpha{},lift{},drag{},airspeed{};bool stalled=false,crashed=false;};
inline std::optional<AircraftReport> stepAircraft(const AircraftConfig& c,AircraftState& state,const AircraftInput& in,uint32_t tick,Fixed dt,Vec3 wind={}){
    if(!policyDtOk(dt)||!policyTickOk(state.initialized,state.lastTick,tick)||c.mass<=Fixed{}||in.throttle<Fixed{}||in.throttle>1_fx||fxAbs(in.elevator)>1_fx||fxAbs(in.aileron)>1_fx||fxAbs(in.rudder)>1_fx)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;AircraftReport r;
    s.pitch=fxClamp(s.pitch+in.elevator*c.pitchRate*dt,-Fixed::fromRatio(1,4)+Fixed::fromRatio(1,100),Fixed::fromRatio(1,4)-Fixed::fromRatio(1,100));
    s.roll=s.roll+in.aileron*c.rollRate*dt;if(s.roll>Fixed::fromRatio(1,2))s.roll-=1_fx;if(s.roll< -Fixed::fromRatio(1,2))s.roll+=1_fx;
    s.yaw=s.yaw+(in.rudder*c.yawRate+fxSinTurns(s.roll)*Fixed::fromRatio(1,10))*dt;                         // banked turns
    Vec3 fwd=rotateYPR({{},{},1_fx},s.yaw,s.pitch,{}),up=rotateYPR({{},1_fx,{}},s.yaw,s.pitch,s.roll);
    auto air=s.velocity-wind;auto speed=air.length();r.airspeed=speed;
    Vec3 dir=speed>Fixed{}?air/speed:fwd;
    auto alpha=fxAsinTurns(fxClamp(-dir.dot(up),-1_fx,1_fx));r.alpha=alpha;                  // angle between flight path and wing chord (turns)
    Fixed cl=alpha*c.liftSlope*Fixed::fromInt(6);                                              // ~2*pi per radian in turns
    if(fxAbs(alpha)>c.stallAlpha){cl=cl*Fixed::fromRatio(2,5);s.stalled=true;}else s.stalled=false;r.stalled=s.stalled;
    auto q=c.airDensity*speed*speed/2*c.wingArea;auto lift=q*cl;auto drag=q*(c.zeroLiftDrag+c.inducedDrag*cl*cl);r.lift=lift;r.drag=drag;
    Vec3 liftDir=fxNormalize(up-dir*up.dot(dir));
    Vec3 force=liftDir*lift-dir*drag+fwd*(in.throttle*c.maxThrust)+Vec3{{},-c.gravity*c.mass,{}};
    s.velocity+=force/c.mass*dt;s.position+=s.velocity*dt;
    if(s.position.y<=c.groundY){s.position.y=c.groundY;r.crashed=s.velocity.y< -5_fx||fxAbs(s.roll)>Fixed::fromRatio(1,12);if(s.velocity.y<Fixed{})s.velocity.y={};s.onGround=true;}else s.onGround=false;
    if(!policyBounded(s.position)||!policyBounded(s.velocity,kPolicyMaxSpeed))return std::nullopt;
    state=s;return r;
}
inline void io(Archive& a,AircraftState& s){a(s.position,s.velocity,s.yaw,s.pitch,s.roll,s.lastTick,s.stalled,s.onGround,s.initialized);}
inline uint16_t stateKind(const AircraftState&){return 221;}
inline bool valid(const AircraftState& s){return policyBounded(s.position)&&policyBounded(s.velocity,kPolicyMaxSpeed);}
}
