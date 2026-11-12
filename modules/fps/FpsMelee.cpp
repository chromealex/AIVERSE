#include "FpsModule.h"
#include "engine/core/FixedMath.h"
#include <algorithm>

namespace aiverse::fps {
namespace {
// Inside the defender's guard arc: the attacker is within guardTurns of where the defender faces.
bool guarded(const Actor& target,const Actor& attacker,Fixed guardTurns) {
    auto toAttacker=(attacker.pos-target.pos).xz();
    if(toAttacker.lengthSq()==Fixed{})return true;
    return direction(target.yaw,{}).xz().dot(toAttacker)>=toAttacker.length()*fxCosTurns(guardTurns);
}
}

// SimEvents: 3 = swing started (value = attack id), 4 = melee contact
// (value = target slot | MeleeContact << 8, pos = contact point).
void Module::tickMelee(Simulation& sim,Actor& actor,const Input& input) {
    auto& actors=state(sim).actors;const auto now=sim.tick();const auto& m=config_.melee;
    const bool alive=actor.health>0&&!actor.downed;
    const bool block=alive&&!actor.vehicle&&(input.buttons&Block)!=0;
    // The parry window opens on the press edge, not while holding the guard.
    if(block&&!actor.blockHeld)actor.parryUntil=now+m.parryTicks;
    actor.blockHeld=block;
    if(alive&&(input.buttons&LockOn)!=0) {
        actor.lockTarget=0;Fixed best=m.lockRange*m.lockRange;
        for(const auto& candidate:actors)if(candidate.health>0&&!candidate.downed&&candidate.team!=actor.team) {
            auto distance=(candidate.pos-actor.pos).lengthSq();
            if(distance<best){best=distance;actor.lockTarget=candidate.slot;}
        }
    }
    if(actor.lockTarget) {
        const auto& target=actors[actor.lockTarget-1];
        if(!alive||target.health<=0||target.downed||target.team==actor.team||(target.pos-actor.pos).lengthSq()>m.lockRange*m.lockRange*4)actor.lockTarget=0;
        else {auto delta=target.pos-actor.pos;actor.yaw=fxAtan2Turns(delta.x,delta.z);if(actor.yaw<Fixed{})actor.yaw+=1_fx;}
    }
    const bool free=alive&&!actor.vehicle&&!block&&actor.activeAction==Action::None&&now>=actor.staggerUntil;
    int64_t requested=free&&(input.buttons&Melee)?1:0;
    lib::genre::stepMelee(meleeAttacks_,actor.melee,now,requested,[&](int64_t){return free;});
    auto phase=lib::genre::attackPhase(meleeAttacks_,actor.melee,now);
    if(actor.melee.attack&&actor.melee.started==now)sim.emit({3,actor.slot,{},{},actor.melee.attack,actor.pos});
    if(phase!=lib::genre::AttackPhase::Active||!alive){actor.meleeHasTip=false;return;}
    // Sweep the blade tip from its previous position so fast turns cannot skip a target.
    auto shoulder=actor.pos+Vec3{{},stanceHeight(actor,config_.motor)*Fixed::fromRatio(3,4),{}};
    auto tip=shoulder+direction(actor.yaw,{})*m.range;
    auto start=actor.meleeHasTip?actor.meleeTip:shoulder;auto delta=tip-start;actor.meleeTip=tip;actor.meleeHasTip=true;
    for(auto& target:actors) {
        if(target.slot==actor.slot||target.health<=0||target.downed||(target.team==actor.team&&!config_.friendlyFire))continue;
        auto bounds=actorBounds(target,config_.motor);
        bounds.min=bounds.min-Vec3{m.radius,m.radius,m.radius};bounds.max=bounds.max+Vec3{m.radius,m.radius,m.radius};
        Fixed fraction;Vec3 normal;
        bool inside=start.x>=bounds.min.x&&start.x<=bounds.max.x&&start.y>=bounds.min.y&&start.y<=bounds.max.y&&start.z>=bounds.min.z&&start.z<=bounds.max.z;
        if(inside)fraction=Fixed{};else if(!world::intersect(start,delta,bounds,fraction,normal))continue;
        if(!lib::genre::recordMeleeHit(meleeAttacks_,actor.melee,now,target.slot))continue;
        auto contact=lib::genre::resolveMeleeContact(false,now<target.parryUntil,target.blockHeld,guarded(target,actor,m.guardTurns));
        if(contact==lib::genre::MeleeContact::Parried)actor.staggerUntil=now+m.staggerTicks;
        else if(contact==lib::genre::MeleeContact::Hit){damage(sim,actor.slot,target,m.damage);target.staggerUntil=now+m.staggerTicks;}
        sim.emit({4,actor.slot,{},{},int64_t(target.slot)|(int64_t(contact)<<8),start+delta*fraction});
    }
}
}
