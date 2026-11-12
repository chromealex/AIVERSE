#pragma once
// physical-interactions: shared types for the kinematic interaction policies (carry/throw, push/pull,
// ropes, doors, moving platforms, breakables). These policies are the authoritative gameplay state:
// Fixed math, integer ticks, ascending-ID order. Collision comes from the owner through a Collision
// object with two pure queries (WorldCollision adapts engine/world SpatialWorld):
//   InteractionHit sweep(Vec3 start,Vec3 delta,Vec3 halfExtent,std::span<const uint32_t> ignore) const;
//   std::vector<uint32_t> overlapping(const world::Bounds& box,std::span<const uint32_t> ignore) const; // ascending ids
// A Box3D world (engine/physics) may render debris from DebrisRequest events; it is presentation-only.
#include "engine/core/Fixed.h"
#include "engine/core/FixedMath.h"
#include "engine/world/SpatialWorld.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr Fixed kPhysCoordLimit=Fixed::fromInt(100000),kPhysMaxSpeed=Fixed::fromInt(1000),kPhysMaxMass=Fixed::fromInt(100000),
    kPhysMaxExtent=Fixed::fromInt(1000),kPhysMaxForce=Fixed::fromInt(10000000);
inline constexpr std::size_t kPhysMaxObjects=4096,kPhysMaxCommands=4096,kPhysMaxWaypoints=256,kPhysMaxSegments=64,kPhysMaxStages=16,
    kPhysMaxDebris=16,kPhysMaxRules=64,kPhysMaxKeys=64;
inline constexpr uint32_t kPhysMaxTicks=1000000,kPhysMaxPhaseTicks=3600;
inline constexpr int64_t kPhysMaxHealth=1000000000000ll;

enum class InteractionEventKind:uint8_t {
    None,
    PickedUp,PickupRefused,Dropped,DroppedBlocked,Thrown,Landed,FlightExpired,         // carry / throw
    PushStarted,PushStopped,PushBlocked,PushResisted,PushArrived,                        // push / pull
    RopeAttached,RopeDetached,RopeBroken,                                                // ropes
    DoorOpening,DoorOpened,DoorClosing,DoorClosed,DoorBlocked,DoorReversed,DoorRefused,DoorUnlocked,DoorLocked, // doors
    PlatformDeparted,PlatformArrived,RiderBoarded,RiderLeft,                            // platforms
    Damaged,DamageIgnored,StageReached,Broken                                            // breakables
};
// value: reason code (refusals), impulse/tension/damage in milli-units, stage id, ...; collider: hit/blocking collider.
struct InteractionEvent {InteractionEventKind kind=InteractionEventKind::None;int64_t object=0,actor=0,value=0;uint32_t collider=0;Vec3 position{};
    bool operator==(const InteractionEvent&)const=default;};
// Reason codes in InteractionEvent::value for PickupRefused / PushResisted / DoorRefused / DamageIgnored.
enum InteractionReason:int64_t {
    ReasonTooHeavy=1,ReasonOutOfReach=2,ReasonBusy=3,ReasonNotThrowable=4,ReasonNotPullable=5,ReasonTooWeak=6,
    ReasonLocked=7,ReasonWrongSide=8,ReasonNoKey=9,ReasonDuplicate=10,ReasonAlreadyBroken=11,ReasonBelowThreshold=12,ReasonImmune=13
};
struct InteractionHit {bool hit=false;uint32_t id=0;Fixed fraction=1_fx;Vec3 normal{};};

inline world::Bounds physBox(Vec3 center,Vec3 half){return {center-half,center+half};}
inline bool physBounded(Vec3 p,Fixed m=kPhysCoordLimit){return p.x>= -m&&p.x<=m&&p.y>= -m&&p.y<=m&&p.z>= -m&&p.z<=m;}
inline bool physHalf(Vec3 h){return h.x>Fixed{}&&h.y>Fixed{}&&h.z>Fixed{}&&h.x<=kPhysMaxExtent&&h.y<=kPhysMaxExtent&&h.z<=kPhysMaxExtent;}
inline bool physTickOk(bool initialized,uint32_t lastTick,uint32_t tick){return !initialized||tick>lastTick;}
inline bool physDtOk(Fixed dt){return dt>Fixed{}&&dt<=1_fx;}
// Touching faces do not overlap (resting bodies are not blocking).
inline bool physStrictOverlap(const world::Bounds& a,const world::Bounds& b){
    return a.min.x<b.max.x&&a.max.x>b.min.x&&a.min.y<b.max.y&&a.max.y>b.min.y&&a.min.z<b.max.z&&a.max.z>b.min.z;
}
inline bool physIgnored(std::span<const uint32_t> ignore,uint32_t id){return id==0||std::find(ignore.begin(),ignore.end(),id)!=ignore.end();}
inline Vec3 physClampSpeed(Vec3 v){return {fxClamp(v.x,-kPhysMaxSpeed,kPhysMaxSpeed),fxClamp(v.y,-kPhysMaxSpeed,kPhysMaxSpeed),fxClamp(v.z,-kPhysMaxSpeed,kPhysMaxSpeed)};}
inline Vec3 physClampPosition(Vec3 p){return {fxClamp(p.x,-kPhysCoordLimit,kPhysCoordLimit),fxClamp(p.y,-kPhysCoordLimit,kPhysCoordLimit),fxClamp(p.z,-kPhysCoordLimit,kPhysCoordLimit)};}
// yaw in turns around +Y (yaw 0 faces +Z), pitch in turns (positive looks up).
inline Vec3 physForward(Fixed yaw,Fixed pitch){auto c=fxCosTurns(pitch);return {fxSinTurns(yaw)*c,fxSinTurns(pitch),fxCosTurns(yaw)*c};}
inline int64_t physMilli(Fixed v){return (v*Fixed::fromInt(1000)).toInt();}

// Collision adapter over engine/world SpatialWorld boxes (and triangles for sweeps). Deterministic:
// candidates come from the sorted query, ties on equal fraction go to the smaller collider id.
class WorldCollision {
public:
    explicit WorldCollision(const world::SpatialWorld& world):world_(&world){}
    InteractionHit sweep(Vec3 start,Vec3 delta,Vec3 half,std::span<const uint32_t> ignore)const{
        auto end=start+delta;world::Bounds swept{{fxMin(start.x,end.x)-half.x,fxMin(start.y,end.y)-half.y,fxMin(start.z,end.z)-half.z},
            {fxMax(start.x,end.x)+half.x,fxMax(start.y,end.y)+half.y,fxMax(start.z,end.z)+half.z}};
        InteractionHit best;
        for(auto id:world_->query(swept)){
            if(physIgnored(ignore,id))continue;auto h=world_->castCollider(id,start,delta,half);
            if(h.hit&&(!best.hit||h.fraction<best.fraction))best={true,h.id?h.id:id,h.fraction,h.normal};
        }
        return best;
    }
    std::vector<uint32_t> overlapping(const world::Bounds& box,std::span<const uint32_t> ignore)const{
        std::vector<uint32_t> out;
        for(auto id:world_->query(box))if(!physIgnored(ignore,id)&&physStrictOverlap(world_->colliders().at(id).bounds,box))out.push_back(id);
        return out;
    }
private:
    const world::SpatialWorld* world_;
};

// Swept move with a skin: the query uses half-skin extents over the move extended by `skin`, and stops the
// shrunk box `skin` before the contact (the real box touches it). A body touching a surface can slide along
// it or move away, and pressing into it reports a hit without creeping. Starting inside blocks.
struct PhysMove {Vec3 position{};InteractionHit hit{};};
template<class Collision> PhysMove physSweep(const Collision& collision,Vec3 start,Vec3 delta,Vec3 half,std::span<const uint32_t> ignore,Fixed skin){
    if(delta==Vec3{})return {start,{}};
    const Fixed tiny=Fixed::fromRaw(1);Vec3 inner{fxMax(half.x-skin,tiny),fxMax(half.y-skin,tiny),fxMax(half.z-skin,tiny)};
    auto length=delta.length();if(length.raw==0)return {start,{}};
    auto probe=length+skin;auto hit=collision.sweep(start,delta*(probe/length),inner,ignore);
    if(!hit.hit)return {physClampPosition(start+delta),hit};
    auto travel=fxClamp(hit.fraction*probe-skin,Fixed{},length);
    return {physClampPosition(start+delta*(travel/length)),hit};
}
template<class T> bool physSortedUnique(const std::vector<T>& v){for(std::size_t i=1;i<v.size();++i)if(!(v[i-1]<v[i]))return false;return true;}
}
