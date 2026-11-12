// boss-encounters in a native module: an inline encounter (one cone attack, one burn), one boss decision, the chosen
// telegraph begun and stepped to its strike, the burn applied, and every state stored in NativeState. A real
// authoritative handler passes sim.rng(), applies the hits through the game's damage owner and draws the views.
#include <aiverselib/genre/EncounterData.h>
#include <aiverselib/genre/EncounterPersistence.h>
#include "modules/rts/RtsNative.h"
#include <array>
bool libraryEncounterConsumerExample(aiverse::game::NativeState& state){
    using namespace aiverse::lib::genre;using aiverse::Fixed;using aiverse::operator""_fx;
    auto source=aiverse::Json::parse(R"({"statuses":[{"id":1,"kind":"burn","duration":20,"interval":10,"damagePerTick":5}],
        "attacks":[{"id":1,"shape":"cone","radiusMilli":5000,"halfAngleMilli":167,"windup":4,"active":1,"recovery":4,"damage":30,"statuses":[1]}],
        "bosses":[{"id":1,"phases":[{"id":1,"pattern":[{"attack":1,"maxRangeMilli":3000}]}]}]})");
    EncounterData data;
    try{data=readEncounterData(source);}catch(const aiverse::JsonError&){return false;}
    aiverse::Random rng(0x85ebca6bu);BossState boss;boss.boss=1;BossInput in;in.hp=900;in.maxHp=1000;in.targetDistance=2_fx;
    auto step=stepBoss(data.bosses,data.attacks,boss,0,in,rng);if(!step||step->attack!=1)return false;
    CasterPose caster{{},{},true};aiverse::Vec2 hero{{},2_fx};
    auto attack=beginTelegraph(data.attacks,step->attack,1,0,1,2,caster,hero,2);if(!attack)return false;
    const std::array<TelegraphTarget,1> targets{{{2,1,hero,Fixed::fromRatio(1,2),true}}};
    StatusActorState heroStatus;int64_t damage=0;
    for(uint32_t t=0;t<=4;++t){
        auto r=stepTelegraph(data.attacks,*attack,t,caster,{},targets);if(!r)return false;
        for(const auto& h:r->hits){damage+=bossDamage(*step,h.damage);for(auto s:h.statuses)applyStatus(data.statuses,heroStatus,t,s,1);}
    }
    if(damage!=30||heroStatus.active.size()!=1)return false;
    state.values["genre.encounter.hash"]=int64_t((encounterHash(boss)^encounterHash(*attack)^encounterHash(heroStatus))&0x7fffffffffffffffull);
    state.values["genre.encounter.data"]=int64_t(encounterDataHash(source)&0x7fffffffffffffffull);
    return storeBytes(state,"genre.encounter.boss",save(boss))&&storeBytes(state,"genre.encounter.telegraph",save(*attack))&&storeBytes(state,"genre.encounter.status",save(heroStatus));
}
