#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/LocomotionModes.h>
#include <aiverselib/genre/CombatConfigured.h>
#include <aiverselib/genre/CombatData.h>
#include <aiverselib/genre/WeaponData.h>
#include <aiverselib/genre/WeaponPersistence.h>
#include <aiverselib/genre/SensesConfigured.h>
#include <aiverselib/genre/SensesData.h>
#include <aiverselib/genre/SensesPersistence.h>
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
// Stealth senses: validated settings + disguise/hiding definitions, one authoritative perception tick for a
// guard pair and an intruder (owner LOS/occlusion callbacks), then the versioned state in NativeState.
// Bots read SensesReport::perceived (PerceivedTarget) and searchGoal; movement stays with their controllers.
static bool librarySensesConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;using aiverse::Fixed;using aiverse::Vec3;
    if(!sensesSettingsErrors(settings).empty())return false;
    const auto config=sensesConfig(settings);
    auto defs=readSenseDefinitions(aiverse::Json::parse(R"({"disguises":[{"id":1,"factions":[1],"zones":[2],"blownBy":[1]}],
        "hidingSpots":[{"id":1,"position":[0,0,30000]}]})"));
    SensesInput in;
    SenseAgent guard;guard.id=1;guard.team=1;SenseAgent partner=guard;partner.id=2;partner.position={Fixed::fromInt(4),{},{}};
    in.observers={guard,partner};
    SenseTarget intruder;intruder.id=10;intruder.team=2;intruder.position={{},Fixed::fromRatio(17,10),Fixed::fromInt(6)};intruder.zone=2;intruder.equipDisguise=1;
    intruder.lightMilli=600;intruder.stanceMilli=500;intruder.leaveTrail=true;in.targets={intruder};
    NoiseEvent step;step.id=1;step.source=intruder.id;step.team=intruder.team;step.position=intruder.position;step.radius=Fixed::fromInt(8);step.loudnessMilli=400;in.noises={step};
    in.candidates={{1,{{},{},Fixed::fromInt(30)},1}};
    SensesState senses;
    auto los=[](const SenseAgent&,const SenseTarget&){return true;};          // owner: world raycast eye -> target
    auto occlusion=[](const SenseAgent&,const NoiseEvent&){return int64_t(1000);}; // owner: wall/door attenuation
    auto report=stepSenses(config,defs,senses,0,in,los,occlusion);
    if(!report||report->perceived.empty()||report->perceived.front().target!=intruder.id||!senses.disguises.contains(intruder.id))return false;
    state.values["genre.senses.config"]=int64_t(sensesConfigHash(config)&0x7FFFFFFFFFFFFFFFull);
    state.values["genre.senses_hash"]=int64_t(sensesHash(senses)&0x7FFFFFFFFFFFFFFFull);
    return storeBytes(state,"genre.senses",save(senses));
}
namespace {
// Weapon policies from the same validated settings plus project definitions (materials, frame,
// attachments). The host's fire/reload commands later call stepFeed/stepCharge once per tick in the
// authoritative handler; here only validated initial records are stored, one NativeState key each.
bool weaponConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse;using namespace aiverse::lib::genre;
    if(!weaponSettingsErrors(settings).empty())return false;
    auto defs=readWeaponDefinitions(Json::parse(R"({"materials":[{"id":1,"resistanceMilli":200,"damageFalloffMilli":100},{"id":2,"impenetrable":true}],
        "frame":{"slots":[1],"stats":[{"id":1,"baseMilli":25000,"minimumMilli":0,"maximumMilli":100000}]},
        "attachments":[{"id":10,"slot":1,"modifiers":[{"stat":1,"op":"mul","valueMilli":1200}]}]})"));
    LoadoutState loadout;auto stats=equipAttachment(defs.frame,defs.attachments,loadout,10)?resolveStats(defs.frame,defs.attachments,loadout):std::nullopt;
    auto feedRules=feedConfig(settings);auto charge=chargeConfig(settings);
    auto shot=resolvePenetration(defs.materials,penetrationConfig(settings),{{1,1,{},1_fx,{},{}},{2,2,1_fx,{},{},{}}},stats?stats->at(1):Fixed{},800_fx);
    auto blast=resolveExplosion(explosionConfig(settings),{},{{1,{2_fx,{},{}},{}}},[](const ExplosionTarget&){return 1_fx;});
    auto aim=solveIntercept(aimConfig(settings),{},{20_fx,{},{}},{},50_fx,10_fx);
    if(!stats||!valid(feedRules)||!valid(charge)||!shot||!blast||!aim||!aim->found)return false;
    return storeBytes(state,"genre.weapon.feed",save(makeFeed(feedRules)))&&storeBytes(state,"genre.weapon.charge",save(ChargeState{}))&&storeBytes(state,"genre.weapon.loadout",save(loadout));
}
}
bool libraryGenreConsumerExample(aiverse::game::NativeState& state,const aiverse::Json& settings,const aiverse::Json& story){
    using namespace aiverse::lib::genre;
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(settings);if(!parsed.value)return false;
    if(!valid(motionConfig(*parsed.value))||!valid(matchConfig(*parsed.value)))return false;
    auto graph=readStory(story);auto initial=beginStory(graph,graph.begin()->first,{{1,1}});if(!initial)return false;
    // Mode machine (slopes, ledges, ladders, swimming, climbing, mantle): one grounded spawn tick.
    // The physics owner fills LocomotionInput::env every tick; bind the config hash in the save envelope.
    const auto modes=locomotionModeConfig(*parsed.value);if(!valid(modes))return false;
    LocomotionState body;LocomotionInput spawn;spawn.env.grounded=true;
    if(!stepLocomotion(modes,body,spawn,0,aiverse::Fixed::fromRatio(1,60)))return false;
    state.values["genre.locomotion.config"]=int64_t(locomotionConfigHash(modes)&0x7FFFFFFFFFFFFFFFull);
    if(!storeBytes(state,"genre.story",save(*initial))||!storeBytes(state,"genre.motion",save(MotionState{}))||!storeBytes(state,"genre.locomotion",save(body)))return false;
    if(!libraryCombatConsumerExample(state,*parsed.value))return false;
    if(!librarySensesConsumerExample(state,*parsed.value))return false;
    return weaponConsumerExample(state,*parsed.value);
}
