#pragma once
// FPS host simulation state on ECS (owner rule "Everything runs on ECS", AGENTS.md; design
// docs/handoffs/no-genre-split-stage-03-design.md). There is no Actor object model any more:
//
// * Characters: one entity per player slot, created at setup and never destroyed. Components
//   CharacterId / CharacterBody / CharacterHealth / CharacterWeapons / CharacterInventory /
//   CharacterControl / CharacterAction / CharacterMelee (+ lib::systems::GearLoadout when `fps.gear` is set,
//   + BotMemory when `fps.bots` is set). Resource CharacterIndex{bySlot} maps slot 1.. to entities.
// * Dynamic objects: one entity per projectile / loot item / vehicle with the component Projectile / Loot /
//   Vehicle (ActorSystems.h) and the shared vehicles::VehicleDrive on typed vehicles. Deterministic order = ascending `id`.
// * Match-level data: the World resource actor::State (round, winner, nextObject, match, royale, history).
//
// Public API of actor::ActorHost (ActorModule.h) for consumers (Session, client, HUD, editor, bots, tools, tests):
//   size_t            characterCount(const Simulation&) const;
//   Entity            characterEntity(const Simulation&, uint8_t slot) const;          // kNullEntity if invalid
//   CharacterRef      character(Simulation&, uint8_t slot) const;                      // writable view, throws std::out_of_range
//   ConstCharacterRef character(const Simulation&, uint8_t slot) const;                // read-only view, throws std::out_of_range
//   CharacterSnapshot snapshot(const Simulation&, uint8_t slot) const;                 // value copy, throws std::out_of_range
//   std::vector<CharacterSnapshot> snapshots(const Simulation&) const;               // all characters, slot order
//   void forEachCharacter(Simulation&, Fn)  /  forEachCharacter(const Simulation&, Fn) // fn(CharacterRef|ConstCharacterRef), slot order
//   std::vector<Entity> projectileEntities / lootEntities / vehicleEntities(const Simulation&) const; // ascending id
//   std::vector<Projectile> projectiles(const Simulation&) const;  // copies, ascending id
//   std::vector<Loot>       loot(const Simulation&) const;
//   std::vector<Vehicle>    vehicles(const Simulation&) const;
//   State& state(Simulation&) const;  const State& state(const Simulation&) const;   // match-level data
//   const lib::systems::GearLoadout* gear(const Simulation&, uint8_t slot) const;   // nullptr without fps.gear
// Field names of the views / snapshot are the old Actor names: `state.actors[i].health` becomes
// `module.character(sim, i+1).health`. moveActor / stanceHeight / actorBounds accept CharacterRef,
// ConstCharacterRef (read-only ones) and CharacterSnapshot. io(Archive&, CharacterSnapshot&) keeps the old
// Actor wire layout (network snapshots).
#include "engine/core/ECS.h"
#include "engine/core/Archive.h"
#include "engine/world/SpatialWorld.h"
#include "ActorSystems.h"
#include "libraries/include/aiverselib/mechanics/Melee.h"
#include "libraries/include/aiverselib/mechanics/VehicleData.h"
#include "engine/vehicles/Vehicles.h"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace aiverse::actor {
constexpr uint16_t kInputCommand=1;
enum Buttons : uint32_t { Fire=1, Jump=2, Crouch=4, Reload=8, Sprint=16, WeaponMask=224, Aim=256, Prone=512, Melee=1024, Block=2048, LockOn=4096 };
constexpr uint32_t kButtonBits=8191;
constexpr uint32_t selectWeapon(uint32_t index) { return (index+1)<<5; }
// Weapon selection occupies bits 5..7; zero means keep the equipped weapon.
struct Input {
    int32_t forward=0, right=0; // [-1000,1000], relative to yaw
    Fixed yaw{},pitch{};       // turns; pitch positive up
    uint32_t buttons=0,viewTick=0,sequence=0;
    Action action=Action::None;uint32_t argument=0;
    bool operator==(const Input&) const = default;
};
void io(Archive&,Input&);
struct MotorConfig {
    Fixed radius=Fixed::fromRatio(3,10),height=Fixed::fromRatio(18,10),crouchHeight=1_fx;
    Fixed speed=5_fx,sprintSpeed=8_fx,jumpSpeed=6_fx,gravity=20_fx,stepHeight=Fixed::fromRatio(3,10);
};

// ---- character components (defaults = a freshly spawned character)
struct CharacterId { uint8_t slot=0,team=0; uint32_t life=1; int32_t kills=0; };
struct CharacterBody {
    Vec3 pos{},velocity{};Fixed yaw{},pitch{};
    bool grounded=false,crouched=false,prone=false,climbing=false,frozen=false;
    uint32_t vehicle=0;   // id of the vehicle the character sits in, 0 = on foot
};
struct CharacterHealth { int32_t health=100,armor=0;bool downed=false;uint32_t respawnAt=0,bleedUntil=0,blindUntil=0,lastDamage=0; };
struct CharacterWeapons {
    uint8_t weaponIndex=0;int32_t ammo=30,reserve=120;int32_t storedAmmo[4]{},storedReserve[4]{};
    uint32_t shots=0,hits=0,switchUntil=0;bool fireHeld=false;uint32_t reloadUntil=0,nextFire=0;
    Fixed recoil{};bool aiming=false;uint8_t owned=15,attachments[4]{};
};
struct CharacterInventory { std::vector<Item> bag;int32_t money=0; };
struct CharacterControl { Input input{};uint32_t lastInputTick=0,lastSequence=0; };
struct CharacterAction { Action activeAction=Action::None;uint32_t actionUntil=0,actionTarget=0;Vec3 actionOrigin{}; };
struct CharacterMelee {
    lib::mechanics::MeleeState melee;uint32_t parryUntil=0,staggerUntil=0;uint8_t lockTarget=0;
    bool blockHeld=false,meleeHasTip=false;Vec3 meleeTip{};
};
// lib::bots::saveFps bytes of a library-controlled bot (only with `fps.bots`).
struct BotMemory { std::string bytes; };
// Typed vehicles (`fps.vehicleTypes`) carry the shared vehicles::VehicleDrive component (engine/vehicles/Vehicles.h)
// next to Vehicle; on race maps the race is the vehicles::RaceState resource (racer i = slot i, the car it drives).
// Resource: character entity per slot (slot 1 -> bySlot[0]); stable for the whole match.
struct CharacterIndex { std::vector<Entity> bySlot; };

// Every character field: (component, name). Order = binding order of the views.
#define AIVERSE_FPS_CHARACTER_FIELDS(X) \
    X(CharacterId,slot) X(CharacterId,team) X(CharacterId,life) X(CharacterId,kills) \
    X(CharacterBody,pos) X(CharacterBody,velocity) X(CharacterBody,yaw) X(CharacterBody,pitch) \
    X(CharacterBody,grounded) X(CharacterBody,crouched) X(CharacterBody,prone) X(CharacterBody,climbing) \
    X(CharacterBody,frozen) X(CharacterBody,vehicle) \
    X(CharacterHealth,health) X(CharacterHealth,armor) X(CharacterHealth,downed) X(CharacterHealth,respawnAt) \
    X(CharacterHealth,bleedUntil) X(CharacterHealth,blindUntil) X(CharacterHealth,lastDamage) \
    X(CharacterWeapons,weaponIndex) X(CharacterWeapons,ammo) X(CharacterWeapons,reserve) \
    X(CharacterWeapons,storedAmmo) X(CharacterWeapons,storedReserve) X(CharacterWeapons,shots) \
    X(CharacterWeapons,hits) X(CharacterWeapons,switchUntil) X(CharacterWeapons,fireHeld) \
    X(CharacterWeapons,reloadUntil) X(CharacterWeapons,nextFire) X(CharacterWeapons,recoil) \
    X(CharacterWeapons,aiming) X(CharacterWeapons,owned) X(CharacterWeapons,attachments) \
    X(CharacterInventory,bag) X(CharacterInventory,money) \
    X(CharacterControl,input) X(CharacterControl,lastInputTick) X(CharacterControl,lastSequence) \
    X(CharacterAction,activeAction) X(CharacterAction,actionUntil) X(CharacterAction,actionTarget) \
    X(CharacterAction,actionOrigin) \
    X(CharacterMelee,melee) X(CharacterMelee,parryUntil) X(CharacterMelee,staggerUntil) \
    X(CharacterMelee,lockTarget) X(CharacterMelee,blockHeld) X(CharacterMelee,meleeHasTip) X(CharacterMelee,meleeTip)

// Non-owning view of one character's components with the old Actor field names (reference members).
// Valid while the character's component pools are not restructured (characters are never created or
// destroyed during a match, so a view stays valid for a whole tick). Build it with ActorHost::character.
template<bool Const> struct BasicCharacterRef {
    template<class C> using Part=std::conditional_t<Const,const C,C>;
    Entity entity;
#define AIVERSE_FPS_REF_MEMBER(C,f) std::conditional_t<Const,const decltype(C::f)&,decltype(C::f)&> f;
    AIVERSE_FPS_CHARACTER_FIELDS(AIVERSE_FPS_REF_MEMBER)
#undef AIVERSE_FPS_REF_MEMBER
    Part<CharacterId>* CharacterId_;Part<CharacterBody>* CharacterBody_;Part<CharacterHealth>* CharacterHealth_;
    Part<CharacterWeapons>* CharacterWeapons_;Part<CharacterInventory>* CharacterInventory_;
    Part<CharacterControl>* CharacterControl_;Part<CharacterAction>* CharacterAction_;Part<CharacterMelee>* CharacterMelee_;
#define AIVERSE_FPS_REF_INIT(C,f) f(C##_p.f),
    BasicCharacterRef(Entity e,Part<CharacterId>& CharacterId_p,Part<CharacterBody>& CharacterBody_p,Part<CharacterHealth>& CharacterHealth_p,
                      Part<CharacterWeapons>& CharacterWeapons_p,Part<CharacterInventory>& CharacterInventory_p,
                      Part<CharacterControl>& CharacterControl_p,Part<CharacterAction>& CharacterAction_p,Part<CharacterMelee>& CharacterMelee_p)
        : entity(e),AIVERSE_FPS_CHARACTER_FIELDS(AIVERSE_FPS_REF_INIT)
          CharacterId_(&CharacterId_p),CharacterBody_(&CharacterBody_p),CharacterHealth_(&CharacterHealth_p),
          CharacterWeapons_(&CharacterWeapons_p),CharacterInventory_(&CharacterInventory_p),CharacterControl_(&CharacterControl_p),
          CharacterAction_(&CharacterAction_p),CharacterMelee_(&CharacterMelee_p) {}
#undef AIVERSE_FPS_REF_INIT
    BasicCharacterRef(const BasicCharacterRef&)=default;
    BasicCharacterRef& operator=(const BasicCharacterRef&)=delete;   // a view is not rebindable; assign fields
    // A writable view converts to a read-only one.
    operator BasicCharacterRef<true>() const requires(!Const) {
        return {entity,*CharacterId_,*CharacterBody_,*CharacterHealth_,*CharacterWeapons_,*CharacterInventory_,*CharacterControl_,*CharacterAction_,*CharacterMelee_};
    }
};
using CharacterRef=BasicCharacterRef<false>;
using ConstCharacterRef=BasicCharacterRef<true>;
// Builds a view, nullopt-like failure is reported by `ok=false` (missing component or dead entity).
template<bool Const,class W> BasicCharacterRef<Const> makeCharacterRef(W& world,Entity e) {
    auto get=[&](auto tag)->auto&{
        using C=typename decltype(tag)::type;
        if constexpr(Const){auto* p=world.template tryGetConst<C>(e);if(!p)throw std::out_of_range("actor: character component missing");return *p;}
        else {auto* p=world.template tryGet<C>(e);if(!p)throw std::out_of_range("actor: character component missing");return *p;}
    };
    return BasicCharacterRef<Const>(e,get(std::type_identity<CharacterId>{}),get(std::type_identity<CharacterBody>{}),get(std::type_identity<CharacterHealth>{}),
        get(std::type_identity<CharacterWeapons>{}),get(std::type_identity<CharacterInventory>{}),get(std::type_identity<CharacterControl>{}),
        get(std::type_identity<CharacterAction>{}),get(std::type_identity<CharacterMelee>{}));
}

// Plain value record of all character fields (old Actor layout and defaults). Used outside the simulation:
// network snapshots and client prediction, rewind history frames, save files. Not stored in the World.
struct CharacterSnapshot {
    uint8_t slot=0,team=0;
    Vec3 pos{},velocity{};
    Fixed yaw{},pitch{};
    int32_t health=100,ammo=30,reserve=120,kills=0;
    uint32_t life=1;
    uint8_t weaponIndex=0;
    int32_t storedAmmo[4]{},storedReserve[4]{};
    uint32_t shots=0,hits=0,switchUntil=0;
    bool fireHeld=false;
    uint32_t reloadUntil=0,nextFire=0,respawnAt=0,lastInputTick=0,lastSequence=0;
    bool grounded=false,crouched=false;
    Input input{};
    int32_t armor=0,money=0;uint8_t owned=15,attachments[4]{};
    Fixed recoil{};bool aiming=false,prone=false,downed=false,climbing=false,frozen=false;
    uint32_t bleedUntil=0,blindUntil=0,vehicle=0,lastDamage=0;
    Action activeAction=Action::None;uint32_t actionUntil=0,actionTarget=0;Vec3 actionOrigin{};
    std::vector<Item> bag;
    lib::mechanics::MeleeState melee;
    uint32_t parryUntil=0,staggerUntil=0;uint8_t lockTarget=0;
    bool blockHeld=false,meleeHasTip=false;Vec3 meleeTip{};

    bool operator==(const CharacterSnapshot&) const = default;
};
// Same byte layout as the former io(Archive&, Actor&) (melee fields are not part of it, see ioMelee).
void io(Archive&,CharacterSnapshot&);
void ioMelee(Archive&,CharacterSnapshot&);

namespace detail {
// One function: MSVC treats the array overload and `T = U[N]` as equally good.
template<class T> void copyField(T& to,const T& from){
    if constexpr(std::is_array_v<T>) std::copy_n(from,std::extent_v<T>,to);
    else to=from;
}
}
template<bool Const> CharacterSnapshot snapshot(const BasicCharacterRef<Const>& r) {
    CharacterSnapshot s;
#define AIVERSE_FPS_SNAP(C,f) detail::copyField(s.f,r.f);
    AIVERSE_FPS_CHARACTER_FIELDS(AIVERSE_FPS_SNAP)
#undef AIVERSE_FPS_SNAP
    return s;
}
// Writes every field of `s` into the character's components (respawn / round reset / save load).
inline void assign(const CharacterRef& r,const CharacterSnapshot& s) {
#define AIVERSE_FPS_ASSIGN(C,f) detail::copyField(r.f,s.f);
    AIVERSE_FPS_CHARACTER_FIELDS(AIVERSE_FPS_ASSIGN)
#undef AIVERSE_FPS_ASSIGN
}
// Adds all character components of `s` to entity `e`.
inline void addCharacterComponents(World& world,Entity e,const CharacterSnapshot& s) {
    world.add<CharacterId>(e);world.add<CharacterBody>(e);world.add<CharacterHealth>(e);world.add<CharacterWeapons>(e);
    world.add<CharacterInventory>(e);world.add<CharacterControl>(e);world.add<CharacterAction>(e);world.add<CharacterMelee>(e);
    assign(makeCharacterRef<false>(world,e),s);
}

// Pose used by ray / sweep hit tests (live characters and rewind frames alike).
struct HitPose {
    uint8_t slot=0,team=0;uint32_t life=0;int32_t health=0;Vec3 pos{};bool crouched=false,prone=false,downed=false;
    template<class C> static HitPose of(const C& a){return {a.slot,a.team,a.life,a.health,a.pos,a.crouched,a.prone,a.downed};}
};

// ---- motor helpers over any type with the body fields (CharacterRef, CharacterSnapshot, HitPose)
inline Fixed stanceHeight(bool prone,bool downed,bool crouched,const MotorConfig& c){return (prone||downed)?Fixed::fromRatio(6,10):crouched?c.crouchHeight:c.height;}
template<class C> Fixed stanceHeight(const C& a,const MotorConfig& c){return stanceHeight(bool(a.prone),bool(a.downed),bool(a.crouched),c);}
inline world::Bounds bodyBounds(Vec3 pos,Fixed height,const MotorConfig& c){return {pos-Vec3{c.radius,{},c.radius},pos+Vec3{c.radius,height,c.radius}};}
template<class C> world::Bounds actorBounds(const C& a,const MotorConfig& c){return bodyBounds(a.pos,stanceHeight(a,c),c);}
// Fields read and written by the character motor.
struct MotorBody {
    Vec3 pos{},velocity{};Fixed yaw{},pitch{};int32_t health=100;uint32_t vehicle=0;
    bool grounded=false,crouched=false,prone=false,climbing=false,frozen=false,downed=false;
};
void moveBody(MotorBody&,const Input&,const MotorConfig&,const world::SpatialWorld&,Fixed dt,Fixed limit);
template<class C> void moveActor(C&& a,const Input& input,const MotorConfig& config,const world::SpatialWorld& world,Fixed dt,Fixed limit) {
    MotorBody b;b.pos=a.pos;b.velocity=a.velocity;b.yaw=a.yaw;b.pitch=a.pitch;b.health=a.health;b.vehicle=a.vehicle;
    b.grounded=a.grounded;b.crouched=a.crouched;b.prone=a.prone;b.climbing=a.climbing;b.frozen=a.frozen;b.downed=a.downed;
    moveBody(b,input,config,world,dt,limit);
    a.pos=b.pos;a.velocity=b.velocity;a.yaw=b.yaw;a.pitch=b.pitch;a.grounded=b.grounded;a.crouched=b.crouched;a.prone=b.prone;
}
}
