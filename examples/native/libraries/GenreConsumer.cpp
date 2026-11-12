#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/CombatConfigured.h>
#include <aiverselib/genre/CombatData.h>
#include "modules/rts/RtsNative.h"
#include <array>
#include <span>
#include <string>
// Custom native state initialization; the project's command/physics owners consume the policies.
// Combat actions: validated settings + hitbox data, one authoritative tick of hit collection and contact
// resolution, then per-actor state in NativeState. Damage itself stays with the project's combat owner.
static bool libraryCombatConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;using aiverse::Fixed;
    auto dodge=dodgeConfig(settings);auto guard=guardConfig(settings);auto poise=poiseConfig(settings);auto lock=lockOnConfig(settings);
    if(!valid(dodge)||!valid(guard)||!valid(poise)||!valid(lock))return false;
    AttackSet attacks{{1,attackConfig(settings,1)}};
    auto boxes=readHitboxes(aiverse::Json::parse(R"({"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":)"+std::to_string(attacks[1].windup)+
        R"(,"to":)"+std::to_string(attacks[1].windup+attacks[1].active)+R"(,"shape":"sphere","a":[0,1000,1000],"radiusMilli":500}]}]})"),attacks);
    CombatShape torso;torso.kind=ShapeKind::Capsule;torso.b={{},Fixed::fromInt(2),{}};torso.radius=Fixed::fromRatio(2,5);
    CombatBody attacker{1,1,{},{{1,torso}}},defender{2,2,{{{},{},Fixed::fromRatio(6,5)},Fixed::fromRatio(1,2)},{{1,torso}}};
    MeleeState swing;CombatActorState target;auto always=[](auto){return true;};
    for(uint32_t tick=0;tick<=attacks[1].windup;++tick){
        if(!stepMelee(attacks,swing,tick,tick==0?1:0,always)||!stepDodge(dodge,target.dodge,tick,false,[]{return true;})||!stepGuard(guard,target.guard,tick,false)||!stepPoise(poise,target.poise,tick))return false;
        const std::array<LockCandidate,1> seen{{{attacker.id,attacker.pose.position,true}}};
        if(!stepLockOn(lock,target.lock,tick,defender.pose,seen,{tick==0,0}))return false;
        auto hits=collectMeleeHits(attacks,boxes,swing,tick,attacker,std::span<const CombatBody>(&defender,1));if(!hits)return false;
        for(const auto& hit:*hits){
            (void)hit;auto contact=resolveCombatContact(dodge,target.dodge,guard,target.guard,defender.pose,attacker.pose.position,tick);
            if(!applyPoiseHit(poise,target.poise,tick,poise.maximum/4,contact))return false;
        }
    }
    if(swing.hitTargets!=std::vector<int64_t>{defender.id}||target.lock.target!=attacker.id)return false;
    state.values["genre.combat_hash"]=int64_t(combatHash(target)&0x7fffffffffffffffull);
    return storeBytes(state,"genre.combat",save(target))&&storeBytes(state,"genre.combat_swing",save(swing));
}
bool libraryGenreConsumerExample(aiverse::game::NativeState& state,const aiverse::Json& settings,const aiverse::Json& story){
    using namespace aiverse::lib::genre;
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(settings);if(!parsed.value)return false;
    if(!valid(motionConfig(*parsed.value))||!valid(matchConfig(*parsed.value)))return false;
    auto graph=readStory(story);auto initial=beginStory(graph,graph.begin()->first,{{1,1}});if(!initial)return false;
    if(!storeBytes(state,"genre.story",save(*initial))||!storeBytes(state,"genre.motion",save(MotionState{})))return false;
    return libraryCombatConsumerExample(state,*parsed.value);
}
