#pragma once
// vehicle-controllers: deterministic kinematic vehicle controllers (wheeled arcade-sim, tracked, hover, flight).
// Fixed math, integer ticks, no physics backend authority. The collision owner reports ground/wheel contacts
// (VehicleContacts) and resolves collisions after the step (vehicleResolveCollision); the controller integrates
// the pose itself. Determinism/network model: VehicleState is the authoritative record (save/hash it; clients
// predict by running the same step and roll back by restoring it). A Box3D world may only mirror the result.
// Axes: X/Z ground plane, Y up; yaw in turns around +Y (yaw 0 faces +Z, +X is right, positive yaw turns right).
#include "engine/core/Fixed.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr Fixed kVehicleCoordLimit=Fixed::fromInt(100000),kVehicleMaxSpeed=Fixed::fromInt(1000),kVehicleMaxMass=Fixed::fromInt(1000000),
    kVehicleMaxExtent=Fixed::fromInt(1000),kVehicleTwoPi=Fixed::fromRatio(6283185,1000000),kVehicleRpmPerRadS=Fixed::fromRatio(9549297,1000000);
inline constexpr std::size_t kVehicleMaxWheels=16,kVehicleMaxCurve=64,kVehicleMaxGears=16;

enum class VehicleKind:uint8_t{Wheeled,Tracked,Hover,Flight};
enum class VehicleEventKind:uint8_t{
    None,GearChanged,DriftStarted,DriftEnded,Airborne,Landed,Stalled,StallRecovered,WheelSpin,              // controllers
    ZoneDamaged,ZoneDestroyed,ZoneRepaired,Disabled,Repaired,DamageIgnored,                               // damage zones
    SeatEntered,SeatExited,SeatSwitched,EnterRefused,ExitRefused,SwitchRefused,Ejected,Locked,Unlocked    // seats
};
// subject: zone/seat/wheel id; actor: occupant; value: gear, milli speed/impulse, health or reason code.
struct VehicleEvent{VehicleEventKind kind=VehicleEventKind::None;int64_t subject=0,actor=0,value=0;bool operator==(const VehicleEvent&)const=default;};

// Piecewise-linear curve (x ascending), clamped outside: torque over rpm, grip over slip.
struct VehicleCurvePoint{Fixed x,y;bool operator==(const VehicleCurvePoint&)const=default;};
using VehicleCurve=std::vector<VehicleCurvePoint>;

struct VehicleWheelDef{Vec3 offset{};Fixed radius=Fixed::fromRatio(33,100);bool driven=false,steered=false,braked=true,handbrake=false;};
struct VehicleWheeledConfig{
    Fixed mass=Fixed::fromInt(1200),gravity=Fixed::fromRatio(981,100),cgHeight=Fixed::fromRatio(45,100);
    VehicleCurve torque{{Fixed::fromInt(900),Fixed::fromInt(170)},{Fixed::fromInt(4500),Fixed::fromInt(260)},{Fixed::fromInt(7000),Fixed::fromInt(200)}}; // rpm -> Nm
    Fixed idleRpm=Fixed::fromInt(900),redlineRpm=Fixed::fromInt(7000),shiftUpRpm=Fixed::fromInt(6300),shiftDownRpm=Fixed::fromInt(2600);
    std::vector<Fixed> gears{Fixed::fromRatio(32,10),Fixed::fromRatio(21,10),Fixed::fromRatio(15,10),Fixed::fromRatio(115,100),Fixed::fromRatio(92,100)};
    Fixed reverseGear=Fixed::fromRatio(3,1),finalDrive=Fixed::fromRatio(37,10),efficiency=Fixed::fromRatio(85,100);
    uint32_t shiftTicks=12;bool automatic=true;
    Fixed brakeForce=Fixed::fromInt(14000),handbrakeForce=Fixed::fromInt(6000),handbrakeGrip=Fixed::fromRatio(35,100); // N total, lateral grip multiplier
    Fixed maxSteer=Fixed::fromRatio(1,10),steerRate=Fixed::fromRatio(1,2),steerFalloffSpeed=Fixed::fromInt(40);        // turns, turns/s, m/s
    Fixed friction=Fixed::fromRatio(11,10);                                                                            // peak tyre mu
    VehicleCurve longitudinalGrip{{1_fx,1_fx},{2_fx,Fixed::fromRatio(8,10)}};                                          // demand/limit -> grip
    VehicleCurve lateralGrip{{Fixed{},Fixed{}},{Fixed::fromRatio(2,100),1_fx},{Fixed::fromRatio(8,100),Fixed::fromRatio(85,100)}}; // slip turns -> grip
    Fixed drag=Fixed::fromRatio(4,10),rolling=Fixed::fromInt(12);                                                       // N/(m/s)^2, N/(m/s)
    Fixed driftEnter=Fixed::fromRatio(3,100),driftExit=Fixed::fromRatio(15,1000),driftMinSpeed=Fixed::fromInt(8);       // rear slip turns
    std::vector<VehicleWheelDef> wheels{
        {{-Fixed::fromRatio(8,10),{},Fixed::fromRatio(13,10)},Fixed::fromRatio(33,100),false,true,true,false},
        {{Fixed::fromRatio(8,10),{},Fixed::fromRatio(13,10)},Fixed::fromRatio(33,100),false,true,true,false},
        {{-Fixed::fromRatio(8,10),{},-Fixed::fromRatio(13,10)},Fixed::fromRatio(33,100),true,false,true,true},
        {{Fixed::fromRatio(8,10),{},-Fixed::fromRatio(13,10)},Fixed::fromRatio(33,100),true,false,true,true}};
};
// Differential steering: each track approaches its target speed (throttle +/- steer); pivot turns at zero throttle.
struct VehicleTrackedConfig{
    Fixed mass=Fixed::fromInt(30000),gravity=Fixed::fromRatio(981,100),maxSpeed=Fixed::fromInt(15),reverseSpeed=Fixed::fromInt(6);
    Fixed acceleration=Fixed::fromInt(4),braking=Fixed::fromInt(8),trackWidth=Fixed::fromRatio(28,10),lateralGrip=Fixed::fromInt(8),drag=Fixed::fromRatio(1,10);
    Fixed pivotSpeed=Fixed::fromInt(3);   // track speed difference (each side) at full steer, m/s
};
// Altitude hold over the owner-reported ground height, thrust along the heading, lateral drift damping.
struct VehicleHoverConfig{
    Fixed mass=Fixed::fromInt(800),gravity=Fixed::fromRatio(981,100),height=Fixed::fromRatio(15,10),stiffness=Fixed::fromInt(30),damping=Fixed::fromInt(9);
    Fixed thrust=Fixed::fromInt(9000),reverseThrust=Fixed::fromInt(4000),drag=Fixed::fromRatio(15,10),lateralDamping=Fixed::fromRatio(15,10);
    Fixed turnRate=Fixed::fromRatio(45,100),yawResponse=Fixed::fromInt(3);          // turns/s, turns/s^2
};
// Arcade flight: thrust/drag airspeed, pitch/roll/yaw rates, banked turns, lift below stallSpeed fades to a sink.
struct VehicleFlightConfig{
    Fixed mass=Fixed::fromInt(5000),gravity=Fixed::fromRatio(981,100),thrust=Fixed::fromInt(60000),drag=Fixed::fromRatio(15,10);
    Fixed stallSpeed=Fixed::fromInt(35),recoverSpeed=Fixed::fromInt(42),pitchRate=Fixed::fromRatio(2,10),rollRate=Fixed::fromRatio(5,10),yawRate=Fixed::fromRatio(5,100);
    Fixed maxPitch=Fixed::fromRatio(2,10),maxRoll=Fixed::fromRatio(2,10),bankTurnRate=Fixed::fromRatio(12,100),levelRate=Fixed::fromRatio(15,100);
    Fixed stallAuthority=Fixed::fromRatio(3,10),stallPitchDrop=Fixed::fromRatio(8,100),sinkRecovery=Fixed::fromInt(4),groundFriction=Fixed::fromInt(2);
};
struct VehicleControllerDef{VehicleKind kind=VehicleKind::Wheeled;VehicleWheeledConfig wheeled;VehicleTrackedConfig tracked;VehicleHoverConfig hover;VehicleFlightConfig flight;};

// Player-equivalent input (bots produce the same struct). throttle/brake 0..1, steer/pitch/roll/yaw -1..1.
// Wheeled automatic: brake at a standstill engages reverse, where brake drives backward and throttle brakes.
struct VehicleInput{Fixed throttle{},brake{},steer{},pitch{},roll{},yaw{};bool handbrake=false,shiftUp=false,shiftDown=false;};
// Owner-reported contacts for this tick. wheels empty = every wheel follows `grounded`; else one per wheel.
// groundHeight: support height under the vehicle (hover: height below within its sensor range, grounded=false: none).
struct VehicleWheelContact{bool grounded=true;Fixed grip=1_fx;};
struct VehicleContacts{bool grounded=true;Fixed groundHeight{};Vec3 groundNormal{Fixed{},1_fx,Fixed{}};Fixed grip=1_fx;std::vector<VehicleWheelContact> wheels;};
// Multipliers from damage zones (vehiclePerformance) or owner modifiers, each 0..1.
struct VehiclePerformance{Fixed engine=1_fx,grip=1_fx,steering=1_fx,brakes=1_fx,lift=1_fx;bool operator==(const VehiclePerformance&)const=default;};

struct VehicleState{
    Vec3 position{},velocity{};
    Fixed yaw,yawRate,pitch,roll,steer,rpm,airspeed,sink,longAccel,trackLeft,trackRight;
    int32_t gear=1;uint32_t shiftUntil=0,slipMask=0,lastTick=0;
    bool grounded=true,drifting=false,stalled=false,initialized=false;
};
struct VehicleStepResult{std::vector<VehicleEvent> events;Fixed forwardSpeed,slipAngle;uint32_t slipMask=0;Vec3 from{},to{};};

namespace vehicleDetail {
inline bool tickOk(bool initialized,uint32_t last,uint32_t tick){return !initialized||tick>last;}
inline bool vehicleBounded(Vec3 p,Fixed m){return p.x>= -m&&p.x<=m&&p.y>= -m&&p.y<=m&&p.z>= -m&&p.z<=m;}
inline bool vehiclePositive(Fixed v,Fixed hi){return v>Fixed{}&&v<=hi;}
inline bool vehicleNonneg(Fixed v,Fixed hi){return v>=Fixed{}&&v<=hi;}
inline bool vehicleUnitRange(Fixed v,bool signedRange){return v<=1_fx&&v>=(signedRange? -1_fx:Fixed{});}
inline Fixed wrap(Fixed t){int64_t r=t.raw%Fixed::kOne;if(r>Fixed::kOne/2)r-=Fixed::kOne;else if(r<= -Fixed::kOne/2)r+=Fixed::kOne;return Fixed::fromRaw(r);}
inline Fixed sign(Fixed v){return v>Fixed{}?1_fx:v<Fixed{}? -1_fx:Fixed{};}
inline Fixed toward(Fixed v,Fixed target,Fixed step){if(v<target)return fxMin(target,v+step);return fxMax(target,v-step);}
inline Vec2 forward(Fixed yaw){return {fxSinTurns(yaw),fxCosTurns(yaw)};}
inline Vec2 rightOf(Vec2 f){return {f.y,-f.x};}
inline Vec2 rotate(Vec2 f,Fixed turns){auto c=fxCosTurns(turns),s=fxSinTurns(turns);return {f.x*c+f.y*s,f.y*c-f.x*s};}
inline bool curveOk(const VehicleCurve& c){
    if(c.empty()||c.size()>kVehicleMaxCurve)return false;
    for(std::size_t i=0;i<c.size();++i){if(fxAbs(c[i].x)>Fixed::fromInt(100000)||fxAbs(c[i].y)>Fixed::fromInt(100000))return false;if(i&&!(c[i-1].x<c[i].x))return false;}return true;
}
inline Fixed curve(const VehicleCurve& c,Fixed x){
    if(x<=c.front().x)return c.front().y;
    for(std::size_t i=1;i<c.size();++i)if(x<=c[i].x){auto t=(x-c[i-1].x)/(c[i].x-c[i-1].x);return c[i-1].y+(c[i].y-c[i-1].y)*t;}
    return c.back().y;
}
inline Fixed capped(Fixed v,Fixed limit){return fxClamp(v,-limit,limit);}
inline void vertical(VehicleState& s,const VehicleContacts& k,Fixed gravity,Fixed dt,VehicleStepResult& out){
    if(k.grounded){
        if(!s.grounded)out.events.push_back({VehicleEventKind::Landed,0,0,(fxAbs(s.velocity.y)*Fixed::fromInt(1000)).toInt()});
        s.position.y=k.groundHeight;s.velocity.y={};s.grounded=true;
    }else{
        if(s.grounded)out.events.push_back({VehicleEventKind::Airborne,0,0,0});
        s.grounded=false;s.velocity.y-=gravity*dt;s.position.y+=s.velocity.y*dt;
    }
}
}

inline bool valid(const VehicleWheeledConfig& c){
    using namespace vehicleDetail;
    if(!vehiclePositive(c.mass,kVehicleMaxMass)||!vehicleNonneg(c.gravity,Fixed::fromInt(100))||!vehicleNonneg(c.cgHeight,Fixed::fromInt(10))||!curveOk(c.torque)||!curveOk(c.longitudinalGrip)||!curveOk(c.lateralGrip))return false;
    if(!(c.idleRpm>Fixed{}&&c.idleRpm<c.redlineRpm&&c.redlineRpm<=Fixed::fromInt(30000)&&c.shiftDownRpm>=c.idleRpm&&c.shiftDownRpm<c.shiftUpRpm&&c.shiftUpRpm<=c.redlineRpm))return false;
    if(c.gears.empty()||c.gears.size()>kVehicleMaxGears||!vehiclePositive(c.reverseGear,Fixed::fromInt(50))||!vehiclePositive(c.finalDrive,Fixed::fromInt(50))||!vehiclePositive(c.efficiency,1_fx)||c.shiftTicks>600)return false;
    for(std::size_t i=0;i<c.gears.size();++i)if(!vehiclePositive(c.gears[i],Fixed::fromInt(50))||(i&&!(c.gears[i]<c.gears[i-1])))return false;
    if(!vehicleNonneg(c.brakeForce,Fixed::fromInt(10000000))||!vehicleNonneg(c.handbrakeForce,Fixed::fromInt(10000000))||!vehicleNonneg(c.handbrakeGrip,1_fx)||!vehicleNonneg(c.maxSteer,Fixed::fromRatio(1,4))||!vehicleNonneg(c.steerRate,Fixed::fromInt(10))||!vehicleNonneg(c.steerFalloffSpeed,kVehicleMaxSpeed))return false;
    if(!vehiclePositive(c.friction,Fixed::fromInt(10))||!vehicleNonneg(c.drag,Fixed::fromInt(1000))||!vehicleNonneg(c.rolling,Fixed::fromInt(100000))||!vehicleNonneg(c.driftExit,c.driftEnter)||c.driftEnter>Fixed::fromRatio(1,4)||!vehicleNonneg(c.driftMinSpeed,kVehicleMaxSpeed))return false;
    if(c.wheels.empty()||c.wheels.size()>kVehicleMaxWheels)return false;
    bool driven=false;for(const auto& w:c.wheels){if(!vehicleBounded(w.offset,Fixed::fromInt(50))||!vehiclePositive(w.radius,Fixed::fromInt(5)))return false;driven=driven||w.driven;}
    return driven;
}
inline bool valid(const VehicleTrackedConfig& c){
    using namespace vehicleDetail;
    return vehiclePositive(c.mass,kVehicleMaxMass)&&vehicleNonneg(c.gravity,Fixed::fromInt(100))&&vehiclePositive(c.maxSpeed,kVehicleMaxSpeed)&&vehicleNonneg(c.reverseSpeed,kVehicleMaxSpeed)&&vehiclePositive(c.acceleration,Fixed::fromInt(1000))
        &&vehiclePositive(c.braking,Fixed::fromInt(1000))&&vehiclePositive(c.trackWidth,Fixed::fromInt(50))&&vehicleNonneg(c.lateralGrip,Fixed::fromInt(1000))&&vehicleNonneg(c.drag,Fixed::fromInt(100))&&vehicleNonneg(c.pivotSpeed,kVehicleMaxSpeed);
}
inline bool valid(const VehicleHoverConfig& c){
    using namespace vehicleDetail;
    return vehiclePositive(c.mass,kVehicleMaxMass)&&vehicleNonneg(c.gravity,Fixed::fromInt(100))&&vehiclePositive(c.height,Fixed::fromInt(100))&&vehicleNonneg(c.stiffness,Fixed::fromInt(1000))&&vehicleNonneg(c.damping,Fixed::fromInt(1000))
        &&vehicleNonneg(c.thrust,Fixed::fromInt(100000000))&&vehicleNonneg(c.reverseThrust,Fixed::fromInt(100000000))&&vehicleNonneg(c.drag,Fixed::fromInt(10000))&&vehicleNonneg(c.lateralDamping,Fixed::fromInt(1000))
        &&vehicleNonneg(c.turnRate,Fixed::fromInt(10))&&vehiclePositive(c.yawResponse,Fixed::fromInt(100));
}
inline bool valid(const VehicleFlightConfig& c){
    using namespace vehicleDetail;
    return vehiclePositive(c.mass,kVehicleMaxMass)&&vehicleNonneg(c.gravity,Fixed::fromInt(100))&&vehicleNonneg(c.thrust,Fixed::fromInt(100000000))&&vehicleNonneg(c.drag,Fixed::fromInt(10000))
        &&vehiclePositive(c.stallSpeed,kVehicleMaxSpeed)&&c.recoverSpeed>=c.stallSpeed&&c.recoverSpeed<=kVehicleMaxSpeed&&vehicleNonneg(c.pitchRate,Fixed::fromInt(10))&&vehicleNonneg(c.rollRate,Fixed::fromInt(10))
        &&vehicleNonneg(c.yawRate,Fixed::fromInt(10))&&vehiclePositive(c.maxPitch,Fixed::fromRatio(24,100))&&vehiclePositive(c.maxRoll,Fixed::fromRatio(24,100))&&vehicleNonneg(c.bankTurnRate,Fixed::fromInt(10))
        &&vehicleNonneg(c.levelRate,Fixed::fromInt(10))&&vehicleNonneg(c.stallAuthority,1_fx)&&vehicleNonneg(c.stallPitchDrop,Fixed::fromInt(10))&&vehicleNonneg(c.sinkRecovery,Fixed::fromInt(1000))&&vehicleNonneg(c.groundFriction,Fixed::fromInt(1000));
}
inline bool valid(const VehicleControllerDef& d){
    switch(d.kind){
    case VehicleKind::Wheeled:return valid(d.wheeled);
    case VehicleKind::Tracked:return valid(d.tracked);
    case VehicleKind::Hover:return valid(d.hover);
    case VehicleKind::Flight:return valid(d.flight);
    }
    return false;
}
inline bool valid(const VehicleInput& in){
    using vehicleDetail::vehicleUnitRange;
    return vehicleUnitRange(in.throttle,false)&&vehicleUnitRange(in.brake,false)&&vehicleUnitRange(in.steer,true)&&vehicleUnitRange(in.pitch,true)&&vehicleUnitRange(in.roll,true)&&vehicleUnitRange(in.yaw,true);
}
inline bool valid(const VehiclePerformance& p){
    using vehicleDetail::vehicleNonneg;return vehicleNonneg(p.engine,1_fx)&&vehicleNonneg(p.grip,1_fx)&&vehicleNonneg(p.steering,1_fx)&&vehicleNonneg(p.brakes,1_fx)&&vehicleNonneg(p.lift,1_fx);
}
inline bool valid(const VehicleContacts& k){
    if(!vehicleDetail::vehicleBounded({Fixed{},k.groundHeight,Fixed{}},kVehicleCoordLimit)||!vehicleDetail::vehicleBounded(k.groundNormal,1_fx)||k.groundNormal.y<=Fixed{}||!vehicleDetail::vehicleNonneg(k.grip,Fixed::fromInt(4))||k.wheels.size()>kVehicleMaxWheels)return false;
    for(const auto& w:k.wheels)if(!vehicleDetail::vehicleNonneg(w.grip,Fixed::fromInt(4)))return false;return true;
}
inline bool valid(const VehicleState& s){
    using namespace vehicleDetail;
    auto turnOk=[](Fixed t){return t>= -1_fx&&t<=1_fx;};
    return vehicleBounded(s.position,kVehicleCoordLimit)&&vehicleBounded(s.velocity,kVehicleMaxSpeed)&&turnOk(s.yaw)&&turnOk(s.pitch)&&turnOk(s.roll)&&turnOk(s.steer)&&fxAbs(s.yawRate)<=Fixed::fromInt(100)
        &&s.rpm>=Fixed{}&&s.rpm<=Fixed::fromInt(30000)&&vehicleNonneg(s.airspeed,kVehicleMaxSpeed)&&fxAbs(s.sink)<=kVehicleMaxSpeed&&fxAbs(s.longAccel)<=Fixed::fromInt(100000)
        &&fxAbs(s.trackLeft)<=kVehicleMaxSpeed&&fxAbs(s.trackRight)<=kVehicleMaxSpeed&&s.gear>= -1&&s.gear<=int32_t(kVehicleMaxGears)&&(!s.initialized||s.shiftUntil<=s.lastTick+601);
}

// Initial state at a pose (gear 1, idle rpm, grounded).
inline VehicleState makeVehicleState(const VehicleControllerDef& d,Vec3 position,Fixed yaw){
    VehicleState s;s.position=position;s.yaw=vehicleDetail::wrap(yaw);s.rpm=d.kind==VehicleKind::Wheeled?d.wheeled.idleRpm:Fixed{};s.grounded=true;return s;
}
// Teleport (respawn) keeping gear and initialization: zero motion, idle engine.
inline void respawnVehicle(const VehicleControllerDef& d,VehicleState& s,Vec3 position,Fixed yaw){
    auto keep=s;s=makeVehicleState(d,position,yaw);s.lastTick=keep.lastTick;s.initialized=keep.initialized;s.gear=keep.gear<1?1:keep.gear;if(d.kind==VehicleKind::Wheeled)s.gear=1;
}
// Collision response after the owner's sweep: place at `position`, remove the velocity into `normal` (unit, pointing
// out of the obstacle) with restitution 0..1. Returns the normal impulse per unit mass (m/s) for damage rules.
inline Fixed vehicleResolveCollision(VehicleState& s,Vec3 position,Vec3 normal,Fixed restitution=Fixed{}){
    s.position=position;auto into=s.velocity.dot(normal);if(into>=Fixed{})return {};
    auto change=-into*(1_fx+fxClamp(restitution,Fixed{},1_fx));s.velocity+=normal*change;return change;
}
inline Fixed vehicleForwardSpeed(const VehicleState& s){auto f=vehicleDetail::forward(s.yaw);return s.velocity.x*f.x+s.velocity.z*f.y;}

namespace vehicleDetail {
inline void wheeled(const VehicleWheeledConfig& c,VehicleState& s,uint32_t tick,Fixed dt,const VehicleInput& in,const VehicleContacts& k,const VehiclePerformance& p,VehicleStepResult& out){
    auto fwd=forward(s.yaw);auto right=rightOf(fwd);Vec2 v{s.velocity.x,s.velocity.z};
    auto vLong=v.dot(fwd);auto speedAbs=fxAbs(vLong);
    auto steerLimit=c.maxSteer*p.steering;if(c.steerFalloffSpeed>Fixed{})steerLimit=steerLimit*c.steerFalloffSpeed/(c.steerFalloffSpeed+speedAbs);
    s.steer=toward(s.steer,in.steer*steerLimit,c.steerRate*dt);
    // Gearbox: automatic engages reverse from a standstill with the brake; manual shifts on edges.
    const int32_t top=int32_t(c.gears.size());const auto stop=Fixed::fromRatio(1,2);
    auto shiftTo=[&](int32_t g){if(g==s.gear)return;s.gear=g;s.shiftUntil=tick+c.shiftTicks;out.events.push_back({VehicleEventKind::GearChanged,0,0,g});};
    if(c.automatic){
        if(s.gear>=1&&in.brake>Fixed{}&&in.throttle==Fixed{}&&vLong<stop)shiftTo(-1);
        else if(s.gear<=0&&in.throttle>Fixed{}&&vLong> -stop)shiftTo(1);
        else if(s.gear==0&&in.brake>Fixed{}&&vLong<stop)shiftTo(-1);
    }else if(in.shiftUp&&s.gear<top)shiftTo(s.gear+1);
    else if(in.shiftDown&&s.gear> -1)shiftTo(s.gear-1);
    bool reverseSwap=c.automatic&&s.gear==-1;
    auto drive=reverseSwap?in.brake:in.throttle,brake=reverseSwap?in.throttle:in.brake;
    bool shifting=tick<s.shiftUntil;
    auto ratio=s.gear>0?c.gears[std::size_t(s.gear-1)]:s.gear<0? -c.reverseGear:Fixed{};
    // Engine speed follows the driven wheels; neutral/clutch revs freely with the throttle.
    Fixed drivenSpeed{},radius{};int drivenCount=0;
    for(const auto& w:c.wheels)if(w.driven){radius+=w.radius;++drivenCount;}
    radius=radius/int64_t(drivenCount);drivenSpeed=vLong;
    auto coupled=fxAbs(drivenSpeed)/radius*kVehicleRpmPerRadS*fxAbs(ratio)*c.finalDrive;
    s.rpm=(s.gear==0||shifting)?c.idleRpm+(c.redlineRpm-c.idleRpm)*drive/2:fxClamp(fxMax(c.idleRpm,coupled),c.idleRpm,c.redlineRpm);
    auto torque=coupled>=c.redlineRpm?Fixed{}:curve(c.torque,s.rpm)*drive*p.engine;
    auto driveForce=(shifting||s.gear==0)?Fixed{}:torque*ratio*c.finalDrive/radius*c.efficiency;
    if(c.automatic&&s.gear>=1&&!shifting){if(coupled>=c.shiftUpRpm&&s.gear<top)shiftTo(s.gear+1);else if(coupled<=c.shiftDownRpm&&s.gear>1)shiftTo(s.gear-1);}
    // Tyres: static load + longitudinal weight transfer, demand vs. friction limit, lateral slip curve, friction circle.
    const int64_t n=int64_t(c.wheels.size());auto wheelMass=c.mass/n;Fixed wheelbase{};Fixed inertiaSum{};
    int braked=0,hand=0,drivenGrounded=0;Fixed front{},rear{};
    for(std::size_t i=0;i<c.wheels.size();++i){
        const auto& w=c.wheels[i];inertiaSum+=w.offset.x*w.offset.x+w.offset.z*w.offset.z;front=fxMax(front,w.offset.z);rear=fxMin(rear,w.offset.z);
        bool g=k.wheels.empty()?k.grounded:k.wheels[i].grounded;if(!g)continue;braked+=w.braked;hand+=w.handbrake;drivenGrounded+=w.driven;
    }
    wheelbase=front-rear;
    auto transfer=wheelbase>Fixed{}?c.mass*s.longAccel*c.cgHeight/wheelbase/fxMax(1_fx,Fixed::fromInt(n)/2):Fixed{};
    Vec2 force{};Fixed torqueYaw{},rearSlip{};uint32_t slip=0;int grounded=0;
    auto omega=s.yawRate*kVehicleTwoPi;
    for(std::size_t i=0;i<c.wheels.size();++i){
        const auto& w=c.wheels[i];bool g=k.wheels.empty()?k.grounded:k.wheels[i].grounded;if(!g)continue;++grounded;
        auto grip=k.grip*(k.wheels.empty()?1_fx:k.wheels[i].grip)*p.grip;
        auto wf=w.steered?rotate(fwd,s.steer):fwd;auto wr=rightOf(wf);
        auto vp=v+right*(omega*w.offset.z)-fwd*(omega*w.offset.x);
        auto wl=vp.dot(wf),wt=vp.dot(wr);
        auto load=fxMax(Fixed{},c.mass*c.gravity/n+(w.offset.z>Fixed{}? -transfer:w.offset.z<Fixed{}?transfer:Fixed{}));
        auto limit=c.friction*load*grip;
        Fixed fl=w.driven&&drivenGrounded?driveForce/int64_t(drivenGrounded):Fixed{},latScale=1_fx;
        auto stopCap=wheelMass*fxAbs(wl)/dt;
        if(w.braked&&braked&&brake>Fixed{})fl-=sign(wl)*fxMin(c.brakeForce*brake*p.brakes/int64_t(braked),stopCap);
        if(w.handbrake&&hand&&in.handbrake){fl=-sign(wl)*fxMin(c.handbrakeForce/int64_t(hand),stopCap);latScale=c.handbrakeGrip;}
        bool slipping=false;
        if(limit>Fixed{}&&fxAbs(fl)>limit){fl=sign(fl)*limit*curve(c.longitudinalGrip,fxAbs(fl)/limit);slipping=true;}
        if(limit==Fixed{})fl={};
        auto alpha=fxAtan2Turns(fxAbs(wt),fxMax(fxAbs(wl),Fixed::fromRatio(1,2)));
        auto ft=capped(-sign(wt)*limit*curve(c.lateralGrip,alpha)*latScale,wheelMass*fxAbs(wt)/dt/2);
        auto mag=Vec2{fl,ft}.length();
        if(mag>limit&&mag>Fixed{}){auto scale=limit/mag;fl=fl*scale;ft=ft*scale;slipping=true;}
        if(!w.steered)rearSlip=fxMax(rearSlip,alpha);
        if(slipping)slip|=1u<<i;
        auto f=wf*fl+wr*ft;force+=f;
        torqueYaw+=w.offset.z*f.dot(right)-w.offset.x*f.dot(fwd);
    }
    auto speed=v.length();
    force=force-v*(speed*c.drag)-(grounded?v*c.rolling:Vec2{});
    if(grounded)force+=Vec2{k.groundNormal.x,k.groundNormal.z}*(c.mass*c.gravity*k.groundNormal.y);
    // Kingston: finite coast-down on grounded wheels, without selecting reverse.
    // Mass-scaled 0.8 m/s^2 engine/rolling resistance; preserve airborne motion.
    if(grounded && drive==Fixed{} && speed>Fixed{})
        force=force-v/speed*fxMin(c.mass*Fixed::fromRatio(4,5),c.mass*speed/dt);
    auto accel=force/c.mass;v+=accel*dt;s.longAccel=accel.dot(fwd);
    if(grounded && drive==Fixed{} && k.groundNormal.x==Fixed{} && k.groundNormal.z==Fixed{} && v.dot(Vec2{s.velocity.x,s.velocity.z})<=Fixed{})v={};
    auto inertia=c.mass*fxMax(inertiaSum/n,Fixed::fromRatio(1,10));
    s.yawRate+=torqueYaw/inertia/kVehicleTwoPi*dt;
    // Parked: no creep or spin from rounding.
    if(grounded&&drive==Fixed{}&&v.length()<Fixed::fromRatio(5,100)&&accel.length()<Fixed::fromRatio(5,10)&&fxAbs(s.yawRate)<Fixed::fromRatio(1,100)){v={};s.yawRate={};}
    s.yaw=wrap(s.yaw+s.yawRate*dt);
    s.velocity.x=v.x;s.velocity.z=v.y;s.position.x+=v.x*dt;s.position.z+=v.y*dt;
    if(slip&~s.slipMask)out.events.push_back({VehicleEventKind::WheelSpin,0,0,int64_t(slip)});
    s.slipMask=slip;out.slipMask=slip;out.slipAngle=rearSlip;
    auto now=v.length();
    if(!s.drifting&&grounded&&now>=c.driftMinSpeed&&rearSlip>=c.driftEnter){s.drifting=true;out.events.push_back({VehicleEventKind::DriftStarted,0,0,(rearSlip*Fixed::fromInt(1000)).toInt()});}
    else if(s.drifting&&(rearSlip<c.driftExit||now<c.driftMinSpeed/2||!grounded)){s.drifting=false;out.events.push_back({VehicleEventKind::DriftEnded,0,0,0});}
}
inline void tracked(const VehicleTrackedConfig& c,VehicleState& s,Fixed dt,const VehicleInput& in,const VehicleContacts& k,const VehiclePerformance& p){
    auto fwd=forward(s.yaw);auto right=rightOf(fwd);Vec2 v{s.velocity.x,s.velocity.z};
    auto throttle=in.throttle-in.brake;auto steer=in.steer*p.steering*c.pivotSpeed;
    auto base=throttle>=Fixed{}?throttle*c.maxSpeed*p.engine:throttle*c.reverseSpeed*p.engine;
    auto limit=[&](Fixed v){return fxClamp(v,-c.reverseSpeed*p.engine,c.maxSpeed*p.engine);};
    auto left=limit(base+steer),rightTrack=limit(base-steer);
    auto grip=k.grounded?k.grip*p.grip:Fixed{};
    auto stepTrack=[&](Fixed& track,Fixed target){
        bool braking=(target==Fixed{})||(sign(target)!=sign(track)&&track!=Fixed{});
        track=toward(track,target,(braking?c.braking*p.brakes:c.acceleration)*grip*dt);
    };
    stepTrack(s.trackLeft,left);stepTrack(s.trackRight,rightTrack);
    auto vLong=(s.trackLeft+s.trackRight)/2;
    auto lat=v.dot(right);lat=lat*fxMax(Fixed{},1_fx-c.lateralGrip*grip*dt);
    s.yawRate=k.grounded?(s.trackLeft-s.trackRight)/c.trackWidth/kVehicleTwoPi:s.yawRate;
    vLong=vLong-vLong*fxMin(1_fx,c.drag*dt);
    if(k.grounded)v=fwd*vLong+right*lat+Vec2{k.groundNormal.x,k.groundNormal.z}*(c.gravity*k.groundNormal.y*dt);
    s.yaw=wrap(s.yaw+s.yawRate*dt);s.velocity.x=v.x;s.velocity.z=v.y;s.position.x+=v.x*dt;s.position.z+=v.y*dt;
}
inline void hover(const VehicleHoverConfig& c,VehicleState& s,Fixed dt,const VehicleInput& in,const VehicleContacts& k,const VehiclePerformance& p,VehicleStepResult& out){
    auto fwd=forward(s.yaw);auto right=rightOf(fwd);Vec2 v{s.velocity.x,s.velocity.z};
    s.yawRate=toward(s.yawRate,in.steer*c.turnRate*p.steering,c.yawResponse*dt);
    auto thrust=(in.throttle*c.thrust-in.brake*c.reverseThrust)*p.engine;
    auto longV=v.dot(fwd),lat=v.dot(right);
    longV+=(thrust-longV*fxAbs(longV)*c.drag)/c.mass*dt;
    lat=lat*fxMax(Fixed{},1_fx-c.lateralDamping*dt);
    v=fwd*longV+right*lat;
    bool support=k.grounded;
    Fixed ay=support?(k.groundHeight+c.height-s.position.y)*c.stiffness*p.lift-s.velocity.y*c.damping:-c.gravity;
    s.velocity.y+=ay*dt;s.position.y+=s.velocity.y*dt;
    if(support&&s.position.y<k.groundHeight){if(s.velocity.y<Fixed{})out.events.push_back({VehicleEventKind::Landed,0,0,(fxAbs(s.velocity.y)*Fixed::fromInt(1000)).toInt()});s.position.y=k.groundHeight;s.velocity.y=fxMax(s.velocity.y,Fixed{});}
    if(support!=s.grounded)out.events.push_back({support?VehicleEventKind::Landed:VehicleEventKind::Airborne,0,0,0});
    s.grounded=support;
    s.yaw=wrap(s.yaw+s.yawRate*dt);s.velocity.x=v.x;s.velocity.z=v.y;s.position.x+=v.x*dt;s.position.z+=v.y*dt;
}
inline void flight(const VehicleFlightConfig& c,VehicleState& s,Fixed dt,const VehicleInput& in,const VehicleContacts& k,const VehiclePerformance& p,VehicleStepResult& out){
    auto authority=s.stalled?c.stallAuthority:1_fx;
    s.pitch=fxClamp(s.pitch+(in.pitch*c.pitchRate*authority-(s.stalled?c.stallPitchDrop:Fixed{}))*dt,-c.maxPitch,c.maxPitch);
    if(in.roll!=Fixed{})s.roll=fxClamp(s.roll+in.roll*c.rollRate*authority*dt,-c.maxRoll,c.maxRoll);else s.roll=toward(s.roll,Fixed{},c.levelRate*dt);
    auto lift=c.stallSpeed>Fixed{}?fxMin(1_fx,s.airspeed*s.airspeed/(c.stallSpeed*c.stallSpeed))*p.lift:Fixed{};
    s.yawRate=(in.yaw*c.yawRate+s.roll/c.maxRoll*c.bankTurnRate*lift)*p.steering;
    auto accel=(in.throttle*c.thrust*p.engine-s.airspeed*s.airspeed*c.drag)/c.mass-c.gravity*fxSinTurns(s.pitch);
    if(s.grounded)accel-=c.groundFriction*in.brake;
    s.airspeed=fxClamp(s.airspeed+accel*dt,Fixed{},kVehicleMaxSpeed);
    if(lift<1_fx&&!s.grounded)s.sink+=c.gravity*(1_fx-lift)*dt;else s.sink=fxMax(Fixed{},s.sink-c.sinkRecovery*dt);
    s.sink=fxMin(s.sink,kVehicleMaxSpeed/2);
    if(!s.grounded&&!s.stalled&&s.airspeed<c.stallSpeed){s.stalled=true;out.events.push_back({VehicleEventKind::Stalled,0,0,(s.airspeed*Fixed::fromInt(1000)).toInt()});}
    else if(s.stalled&&(s.airspeed>=c.recoverSpeed||s.grounded)){s.stalled=false;out.events.push_back({VehicleEventKind::StallRecovered,0,0,0});}
    s.yaw=wrap(s.yaw+s.yawRate*dt);
    auto cp=fxCosTurns(s.pitch);auto f=forward(s.yaw);
    s.velocity={f.x*cp*s.airspeed,fxSinTurns(s.pitch)*s.airspeed-s.sink,f.y*cp*s.airspeed};
    auto next=s.position+s.velocity*dt;
    if(k.grounded&&next.y<=k.groundHeight){
        if(!s.grounded)out.events.push_back({VehicleEventKind::Landed,0,0,(fxAbs(s.velocity.y)*Fixed::fromInt(1000)).toInt()});
        next.y=k.groundHeight;s.velocity.y={};s.sink={};s.pitch=fxMax(s.pitch,Fixed{});s.roll={};s.grounded=true;
    }else if(s.grounded&&next.y>k.groundHeight){s.grounded=false;out.events.push_back({VehicleEventKind::Airborne,0,0,0});}
    else if(!k.grounded&&s.grounded){s.grounded=false;out.events.push_back({VehicleEventKind::Airborne,0,0,0});}
    s.position=next;
}
}

// One authoritative step per vehicle per strictly increasing tick. Invalid def/state/input/contacts, dt outside
// (0,1] or a repeated/backwards tick return nullopt and leave the state untouched.
inline std::optional<VehicleStepResult> stepVehicle(const VehicleControllerDef& d,VehicleState& s,uint32_t tick,Fixed dt,const VehicleInput& in,
    const VehicleContacts& k,const VehiclePerformance& p={}){
    if(!valid(d)||!valid(s)||!valid(in)||!valid(k)||!valid(p)||dt<=Fixed{}||dt>1_fx||!vehicleDetail::tickOk(s.initialized,s.lastTick,tick))return {};
    if(d.kind==VehicleKind::Wheeled&&!k.wheels.empty()&&k.wheels.size()!=d.wheeled.wheels.size())return {};
    if(d.kind==VehicleKind::Wheeled&&s.gear>int32_t(d.wheeled.gears.size()))return {};
    auto next=s;VehicleStepResult out;out.from=s.position;
    switch(d.kind){
    case VehicleKind::Wheeled:vehicleDetail::vertical(next,k,d.wheeled.gravity,dt,out);vehicleDetail::wheeled(d.wheeled,next,tick,dt,in,k,p,out);break;
    case VehicleKind::Tracked:vehicleDetail::vertical(next,k,d.tracked.gravity,dt,out);vehicleDetail::tracked(d.tracked,next,dt,in,k,p);break;
    case VehicleKind::Hover:vehicleDetail::hover(d.hover,next,dt,in,k,p,out);break;
    case VehicleKind::Flight:vehicleDetail::flight(d.flight,next,dt,in,k,p,out);break;
    }
    next.position={fxClamp(next.position.x,-kVehicleCoordLimit,kVehicleCoordLimit),fxClamp(next.position.y,-kVehicleCoordLimit,kVehicleCoordLimit),fxClamp(next.position.z,-kVehicleCoordLimit,kVehicleCoordLimit)};
    next.velocity={fxClamp(next.velocity.x,-kVehicleMaxSpeed,kVehicleMaxSpeed),fxClamp(next.velocity.y,-kVehicleMaxSpeed,kVehicleMaxSpeed),fxClamp(next.velocity.z,-kVehicleMaxSpeed,kVehicleMaxSpeed)};
    next.yawRate=fxClamp(next.yawRate,-Fixed::fromInt(100),Fixed::fromInt(100));next.longAccel=fxClamp(next.longAccel,-Fixed::fromInt(100000),Fixed::fromInt(100000));
    next.lastTick=tick;next.initialized=true;
    if(next.shiftUntil>tick+601)next.shiftUntil=tick+601;
    s=next;out.to=s.position;out.forwardSpeed=vehicleForwardSpeed(s);return out;
}
}
