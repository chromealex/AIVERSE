#include "engine/mechanics/WeaponCadence.h"
#include "engine/mechanics/MultiTargetSelection.h"
#include "engine/sim/PresentationEventQueue.h"
#include <aiverselib/systems/EngineMechanics.h>
#include <aiverselib/systems/Configured.h>
#include <aiverselib/systems/Persistence.h>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <iostream>
using namespace aiverse;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<": "<<#x<<'\n';std::exit(1);}}while(false)
int main(){
    {
        Simulation scaled(SimConfig{31,8,0,true},std::make_unique<game::GameCore>());
        scaled.setup(Json::parse(R"({"victory":"noUnits","types":{"caster":{"health":{"max":100},"weapons":[{"damage":20},{"damage":8}],"abilities":[{"id":"cleave","effects":[{"type":"damage","weaponDamage":1.5}]},{"id":"heal","effects":[{"type":"heal","weaponDamage":1.25,"weaponIndex":1}]},{"id":"missing","effects":[{"type":"damage","weaponDamage":1,"weaponIndex":5}]},{"id":"zero","effects":[{"type":"damage","weaponDamage":0}]}]},"target":{"health":{"max":100}}}})"),Json::parse(R"({"size":[8,8],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":["........","........","........","........","........","........","........","........"]},"entities":[{"type":"caster","owner":1,"pos":[2,2]},{"type":"target","owner":2,"pos":[6,6]}]})"));
        auto& host=static_cast<game::GameCore&>(scaled.module());
        Entity caster,target;
        scaled.world().each<game::Unit>([&](Entity e,auto& unit){if(host.rules().types[unit.type].id=="caster")caster=e;else target=e;});
        CHECK(caster.valid()&&target.valid());
        CHECK(host.rules().advancedEffects);
        const auto& abilities=host.rules().types[scaled.world().get<game::Unit>(caster).type].abilities;
        CHECK(abilities.size()==4);
        auto damage=host.currentWeaponDamage(scaled,caster);
        CHECK(damage&&*damage==Fixed::fromInt(20));
        CHECK(!host.currentWeaponDamage(scaled,target));
        CHECK(!host.currentWeaponDamage(scaled,kNullEntity));
        CHECK(!host.currentWeaponDamage(scaled,caster,5));
        auto& buffs=scaled.world().get<game::Buffs>(caster);
        game::upsertTimedBuff(buffs,{game::Stat::Damage,{},Fixed::fromInt(2),100,0});
        damage=host.currentWeaponDamage(scaled,caster);
        CHECK(damage&&*damage==Fixed::fromInt(40));
        auto offhand=host.currentWeaponDamage(scaled,caster,1);
        CHECK(offhand&&*offhand==Fixed::fromInt(16));
        game::EffectCtx ctx;ctx.caster=caster;ctx.owner=1;ctx.target=target;
        ctx.sourceType=scaled.world().get<game::Unit>(caster).type;
        ctx.point=scaled.world().get<Transform>(target).pos;
        ctx.origin=scaled.world().get<Transform>(caster).pos;
        auto hp=[&]{return scaled.world().get<game::Unit>(target).hp;};
        ctx.scale=Fixed::fromRatio(1,2);
        host.applyPayload(scaled,abilities[0].payload,ctx);CHECK(hp()==Fixed::fromInt(70));
        ctx.scale=Fixed::fromInt(1);
        host.applyPayload(scaled,abilities[1].payload,ctx);CHECK(hp()==Fixed::fromInt(90));
        host.applyPayload(scaled,abilities[2].payload,ctx);CHECK(hp()==Fixed::fromInt(90));
        host.applyPayload(scaled,abilities[3].payload,ctx);CHECK(hp()==Fixed::fromInt(90));
        ctx.caster=kNullEntity;
        host.applyPayload(scaled,abilities[0].payload,ctx);CHECK(hp()==Fixed::fromInt(90));
        ctx.caster=caster;
        scaled.world().get<game::Buffs>(caster).list.clear();
        host.applyPayload(scaled,abilities[0].payload,ctx);CHECK(hp()==Fixed::fromInt(60));
        for(const char* invalid:{
            R"({"types":{"unit":{"abilities":[{"effects":[{"type":"damage","weaponDamage":-1}]}]}}})",
            R"({"types":{"unit":{"abilities":[{"effects":[{"type":"damage","weaponDamage":1,"amount":2}]}]}}})",
            R"({"types":{"unit":{"abilities":[{"effects":[{"type":"stun","weaponDamage":1}]}]}}})",
            R"({"types":{"unit":{"abilities":[{"effects":[{"type":"heal","weaponDamage":1,"weaponIndex":0.5}]}]}}})"}){
            bool rejected=false;try{(void)game::loadRules(Json::parse(invalid));}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        }
    }
    {
        using namespace aiverse::lib::mechanics;
        NearestWeaponTargets nearest(2);
        nearest.consider(Entity{4,1},Fixed::fromInt(2));
        nearest.consider(Entity{8,1},Fixed::fromInt(1));
        nearest.consider(Entity{2,1},Fixed::fromInt(2));
        nearest.consider(Entity{8,1},Fixed::fromInt(1));
        nearest.consider(Entity{9,1},Fixed::fromInt(3));
        CHECK(nearest.size()==2);
        CHECK(nearest.begin()[0].entity==(Entity{8,1}));
        CHECK(nearest.begin()[1].entity==(Entity{2,1}));
        NearestWeaponTargets none(0);none.consider(Entity{1,1},Fixed{});CHECK(none.size()==0);
    }
    {
        Simulation multi(SimConfig{31,8,0,true},std::make_unique<game::GameCore>());
        multi.setup(Json::parse(R"({"victory":"noUnits","types":{"shooter":{"health":{"max":100},"weapons":[{"range":6,"damage":10,"cooldown":60,"projectileSpeed":2,"extraTargets":2,"extraTargetTags":["eligible"],"extraIgnoreTags":["immune"],"onHit":{"type":"damage","amount":5}}]},"primary":{"health":{"max":100}},"secondary":{"health":{"max":100},"tags":["eligible"]},"ignored":{"health":{"max":100},"tags":["eligible","immune"]}}})"),Json::parse(R"({"size":[8,8],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":["........","........","........","........","........","........","........","........"]},"entities":[{"type":"shooter","owner":1,"pos":[1,1]},{"type":"primary","owner":2,"pos":[3,1]},{"type":"secondary","owner":2,"pos":[2,3]},{"type":"secondary","owner":2,"pos":[4,3]},{"type":"ignored","owner":2,"pos":[2,1]},{"type":"secondary","owner":1,"pos":[1,3]}]})"));
        auto& host=static_cast<game::GameCore&>(multi.module());
        Entity shooter,primary;std::vector<Entity> eligible,excluded;
        multi.world().each<game::Unit,Owner>([&](Entity e,auto& unit,auto& owner){
            const auto& id=host.rules().types[unit.type].id;
            if(id=="shooter")shooter=e;else if(id=="primary")primary=e;
            else if(id=="secondary"&&owner.player==2)eligible.push_back(e);else excluded.push_back(e);
        });
        CHECK(shooter.valid()&&primary.valid()&&eligible.size()==2&&excluded.size()==2);
        auto& combat=multi.world().get<game::Combat>(shooter);combat.target=primary;combat.autoTarget=false;
        SimSnapshot before;multi.saveState(before);
        auto launch=[&](){
            multi.step();
            int attackEvents=0;for(const auto& event:multi.events())if(event.a==shooter&&event.type==game::kEvAttack)++attackEvents;
            CHECK(attackEvents==1);
            std::vector<uint64_t> targets;
            multi.world().each<game::Projectile>([&](Entity,auto& projectile){if(projectile.source==shooter){CHECK(projectile.onHit>=0);targets.push_back(projectile.target.packed());}});
            std::sort(targets.begin(),targets.end());CHECK(targets.size()==3);
            CHECK(std::adjacent_find(targets.begin(),targets.end())==targets.end());
            CHECK(std::find(targets.begin(),targets.end(),primary.packed())!=targets.end());
            for(auto e:eligible)CHECK(std::find(targets.begin(),targets.end(),e.packed())!=targets.end());
            for(auto e:excluded)CHECK(std::find(targets.begin(),targets.end(),e.packed())==targets.end());
            return multi.checksum();
        };
        const auto hash=launch();multi.loadState(before);CHECK(launch()==hash);
        for(int i=0;i<40;++i)multi.step();
        CHECK(multi.world().get<game::Unit>(primary).hp==Fixed::fromInt(85));
        for(auto e:eligible)CHECK(multi.world().get<game::Unit>(e).hp==Fixed::fromInt(85));
        for(auto e:excluded)CHECK(multi.world().get<game::Unit>(e).hp==Fixed::fromInt(100));
        for(const char* invalid:{R"({"types":{"unit":{"weapons":[{"extraTargets":17}]}}})",R"({"types":{"unit":{"weapons":[{"extraTargets":1.5}]}}})",R"({"types":{"unit":{"weapons":[{"extraTargetTags":["unknown"]}]}}})"}){
            bool rejected=false;try{(void)game::loadRules(Json::parse(invalid));}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        }
    }
    {
        using namespace aiverse::lib::mechanics;
        AttackTimingState phase;
        const Entity target{4,1};
        beginAttackWindup(phase,target,Fixed::fromRatio(1,4));
        CHECK(phase.phase==AttackPhase::Windup);
        CHECK(!advanceAttackPhase(phase,Fixed::fromRatio(1,8)));
        CHECK(cancelAttackWindup(phase)&&phase.phase==AttackPhase::Idle);
        beginAttackWindup(phase,target,Fixed::fromRatio(1,4));
        CHECK(advanceAttackPhase(phase,Fixed::fromRatio(1,4)));
        commitAttackRelease(phase,Fixed::fromRatio(3,8));
        CHECK(phase.phase==AttackPhase::Recovery&&!cancelAttackWindup(phase));
        CHECK(advanceAttackPhase(phase,Fixed::fromRatio(3,8)));
    }
    {
        using namespace aiverse::lib::mechanics;
        Simulation timed(SimConfig{31,8,0,true},std::make_unique<game::GameCore>());
        timed.setup(Json::parse(R"({"victory":"noUnits","types":{"shooter":{"health":{"max":100},"weapons":[{"range":8,"damage":10,"cooldown":0.125,"windup":0.25,"backswing":0.375}]},"target":{"health":{"max":100}}}})"),Json::parse(R"({"size":[8,8],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":["........","........","........","........","........","........","........","........"]},"entities":[{"type":"shooter","owner":1,"pos":[2,2]},{"type":"target","owner":2,"pos":[4,2]}]})"));
        Entity shooter,target;
        timed.world().each<game::Unit>([&](Entity e,auto&){if(timed.world().has<game::Combat>(e))shooter=e;else target=e;});
        CHECK(shooter.valid()&&target.valid());
        auto combat=[&]()->game::Combat&{return timed.world().get<game::Combat>(shooter);};
        auto hp=[&]{return timed.world().get<game::Unit>(target).hp;};
        auto event=[&](uint16_t type){int count=0;for(const auto& e:timed.events())if(e.a==shooter&&e.type==type)++count;return count;};
        combat().target=target;combat().autoTarget=false;
        const auto initialHp=hp();
        timed.step();CHECK(event(game::kEvAttackWindup)==1&&event(game::kEvAttack)==0&&hp()==initialHp);
        CHECK(combat().attackTiming[0].phase==AttackPhase::Windup&&combat().cooldown[0].raw==0);
        auto bytes=timed.serialize();auto hash=timed.checksum();CHECK(!bytes.empty());
        timed.world().get<game::Buffs>(shooter).stunUntil=timed.tick()+1;
        timed.step();CHECK(event(game::kEvAttackCancelled)==1&&event(game::kEvAttack)==0&&hp()==initialHp);
        CHECK(combat().attackTiming[0].phase==AttackPhase::Idle&&combat().cooldown[0].raw==0);
        std::string error;CHECK(timed.deserialize(bytes,&error)&&timed.checksum()==hash);
        CHECK(combat().attackTiming[0].phase==AttackPhase::Windup);
        timed.step();CHECK(event(game::kEvAttack)==0&&hp()==initialHp);
        timed.step();CHECK(event(game::kEvAttack)==1&&hp()<initialHp);
        CHECK(combat().attackTiming[0].phase==AttackPhase::Recovery);
        auto afterHit=hp();timed.step();CHECK(hp()==afterHit&&event(game::kEvAttackWindup)==0);
        timed.step();CHECK(hp()==afterHit&&event(game::kEvAttackWindup)==0);
        timed.step();CHECK(event(game::kEvAttackRecoveryEnd)==1&&hp()==afterHit);
        for(const char* invalid:{R"({"types":{"unit":{"weapons":[{"windup":-1}]}}})",R"({"types":{"unit":{"weapons":[{"backswing":"slow"}]}}})",R"({"types":{"unit":{"weapons":[{"windup":61}]}}})"}){
            bool rejected=false;try{(void)game::loadRules(Json::parse(invalid));}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        }
    }
    {
        using namespace aiverse::lib::mechanics;
        // Integer-ratio test values are exactly representable in 16.16.
        const Fixed elapsed=Fixed::fromRatio(1,8), period=Fixed::fromRatio(3,16);
        Fixed remaining{};uint32_t count=0;
        for(int tick=0;tick<=48;++tick){
            remaining=advanceWeaponCadence(remaining,elapsed,true);
            for(uint32_t shot=0;shot<kMaxCadenceShotsPerTick&&cadenceReady(remaining);++shot){
                remaining=acceptCadenceShot(remaining,period,true);++count;
            }
            remaining=finishWeaponCadence(remaining);
        }
        CHECK(count==33); // t=0 through t=6, inclusive
        remaining=Fixed{};
        const Fixed fast=Fixed::fromRatio(1,32);
        remaining=acceptCadenceShot(remaining,fast,true);
        remaining=advanceWeaponCadence(remaining,elapsed,true);
        count=0;
        while(cadenceReady(remaining)){remaining=acceptCadenceShot(remaining,fast,true);++count;}
        CHECK(count==4);
        remaining=advanceWeaponCadence(Fixed::fromRatio(1,32),elapsed,true);
        CHECK(remaining.raw<0);
        remaining=finishWeaponCadence(remaining);
        CHECK(remaining.raw==0);
        CHECK(advanceWeaponCadence(remaining,elapsed,true).raw==0);
        CHECK(advanceWeaponCadence(fast,elapsed,false).raw==0);
        CHECK(acceptCadenceShot(-fast,period,false)==period);
        auto rules=game::loadRules(Json::parse(R"({"types":{"unit":{"weapons":[{"cooldown":0.125,"preciseCooldown":true}]}}})"));
        CHECK(rules.types.front().weapons.front().preciseCooldown);
        bool rejected=false;
        try{(void)game::loadRules(Json::parse(R"({"types":{"unit":{"weapons":[{"cooldown":0,"preciseCooldown":true}]}}})"));}catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected);
    }
    {
        using namespace game;
        Simulation grouped({},std::make_unique<GameCore>());
        grouped.setup(Json::parse(R"({"victory":"noUnits","statuses":{"slow":{"mods":[{"stat":"speed","mul":0.25,"stackingGroup":"slow"}]},"haste":{"mods":[{"stat":"speed","mul":2,"stackingGroup":"haste"}]}},"types":{"unit":{"health":{"max":100},"move":{"speed":4}}}})"),Json::parse(R"({"size":[8,8],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":["........","........","........","........","........","........","........","........"]},"entities":[{"type":"unit","owner":1,"pos":[2,2]},{"type":"unit","owner":2,"pos":[6,6]}]})"));
        Entity e;grouped.world().each<Unit>([&](Entity candidate,auto&){if(!e.valid())e=candidate;});CHECK(e.valid());
        auto& m=static_cast<GameCore&>(grouped.module());
        const auto& groups=m.rules().modifierGroups;
        CHECK(groups.size()==2);
        uint16_t slow=0,haste=0;
        for(size_t i=0;i<groups.size();++i){if(groups[i]=="slow")slow=uint16_t(i+1);if(groups[i]=="haste")haste=uint16_t(i+1);}
        CHECK(slow&&haste);
        auto& buffs=grouped.world().get<Buffs>(e);
        upsertTimedBuff(buffs,{Stat::Speed,{},Fixed::fromRatio(1,2),100,0,slow});
        upsertTimedBuff(buffs,{Stat::Speed,{},Fixed::fromRatio(3,4),80,0,slow});
        upsertTimedBuff(buffs,{Stat::Speed,{},Fixed::fromRatio(1,2),90,0,slow});
        CHECK(buffs.list.size()==2);CHECK(buffs.list.front().until==100);
        CHECK(m.stat(grouped,e,Stat::Speed)==Fixed::fromInt(2));
        const auto id=m.rules().findType("unit");CHECK(id>=0);
        EffectCtx ctx;ctx.caster=e;ctx.target=e;ctx.owner=1;ctx.sourceType=uint16_t(id);
        int statusId=-1;
        for(size_t i=0;i<m.rules().statuses.size();++i)if(m.rules().statuses[i].id=="slow")statusId=int(i);
        CHECK(statusId>=0);
        CHECK(applyStatus(m,grouped,e,statusId,Fixed::fromInt(1),ctx));
        CHECK(m.stat(grouped,e,Stat::Speed)==Fixed::fromInt(1));
        upsertTimedBuff(buffs,{Stat::Speed,{},Fixed::fromInt(2),200,0,haste});
        CHECK(m.stat(grouped,e,Stat::Speed)==Fixed::fromInt(2));
        auto bytes=grouped.serialize();CHECK(!bytes.empty());auto hash=grouped.checksum();
        SimSnapshot snapshot;grouped.saveState(snapshot);
        for(int i=0;i<40;++i)grouped.step();
        CHECK(m.stat(grouped,e,Stat::Speed)==Fixed::fromInt(4));
        grouped.loadState(snapshot);CHECK(grouped.checksum()==hash);
        CHECK(m.stat(grouped,e,Stat::Speed)==Fixed::fromInt(2));
        std::string error;CHECK(grouped.deserialize(bytes,&error));CHECK(grouped.checksum()==hash);
        CHECK(m.stat(grouped,e,Stat::Speed)==Fixed::fromInt(2));
        for(const char* invalid:{R"({"statuses":{"bad":{"mods":[{"stat":"speed","mul":0.5,"stackingGroup":""}]}}})",R"({"statuses":{"bad":{"mods":[{"stat":"speed","mul":0.5,"stackingGroup":"slow","floor":0.2}]}}})"}){
            bool rejected=false;try{(void)loadRules(Json::parse(invalid));}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        }
    }
    {
        using namespace game;
        WeaponDef weapon;weapon.targets=kLayerGround;weapon.targetTags=1|2;weapon.ignoreTags=4;
        CHECK(weaponTargetMatches(weapon,kLayerGround,1));
        CHECK(weaponTargetMatches(weapon,kLayerGround,2));
        CHECK(!weaponTargetMatches(weapon,kLayerGround,0));
        CHECK(!weaponTargetMatches(weapon,kLayerGround,1|4));
        CHECK(!weaponTargetMatches(weapon,kLayerAir,1));
        CHECK(targetTagsMatch(4,0,0));
        CHECK(!targetTagsMatch(4,0,4));
        auto rules=loadRules(Json::parse(R"({"types":{"shooter":{"weapons":[{"targetTags":["organic"],"ignoreTags":["immune"]}]},"target":{"tags":["organic","immune"]}}})"));
        const auto& wd=rules.types[size_t(rules.findType("shooter"))].weapons.front();
        CHECK(wd.targetTags==rules.tagMask("organic"));
        CHECK(wd.ignoreTags==rules.tagMask("immune"));
        bool rejected=false;
        try{(void)loadRules(Json::parse(R"({"types":{"shooter":{"weapons":[{"ignoreTags":["unknown"]}]}}})"));}catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected);
    }
    {
        PresentationEventQueue<int> local;
        CHECK(local.drain().empty());
        local.push(0,1);local.push(0,2);local.push(1,3);
        CHECK(local.drain()==std::vector<int>({1,2,3}));CHECK(local.drain().empty());
        PresentationEventQueue<int> net(true);
        net.push(0,10);net.push(1,20);net.push(2,30);
        CHECK(net.drain(1)==std::vector<int>({10}));CHECK(net.drain(1).empty());
        net.rewind(1);net.push(1,21);net.push(1,22);net.push(2,31);
        CHECK(net.drain(2)==std::vector<int>({21,22}));
        net.push(1,99);CHECK(net.drain(3)==std::vector<int>({31}));CHECK(net.drain(3).empty());
    }
    Simulation sim({},std::make_unique<game::GameCore>());
    sim.setup(Json::parse(R"({"victory":"noUnits","statuses":{"slow":{"mods":[{"stat":"speed","mul":0.5}]}},"types":{"unit":{"health":{"max":100},"move":{"speed":4}}}})"),Json::parse(R"({"size":[8,8],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":["........","........","........","........","........","........","........","........"]},"entities":[{"type":"unit","owner":1,"pos":[2,2]},{"type":"unit","owner":2,"pos":[6,6]}]})"));
    Entity unit;sim.world().each<game::Unit>([&](Entity e,auto&){if(!unit.valid())unit=e;});CHECK(unit.valid());
    auto& module=static_cast<game::GameCore&>(sim.module());std::array stats{game::Stat::Speed};
    CHECK(effectiveStats(module,sim,unit,stats)->front().value==Fixed::fromInt(4));
    game::EffectCtx context;context.caster=unit;context.owner=1;context.target=unit;
    CHECK(!applyStatus(module,sim,unit,999,Fixed::fromInt(2),context));
    CHECK(applyStatus(module,sim,unit,0,Fixed::fromInt(2),context));
    CHECK(statuses(module,sim,unit)->active.size()==1);CHECK(effectiveStats(module,sim,unit,stats)->front().value==Fixed::fromInt(2));
    auto save=sim.serialize();auto hash=sim.checksum();for(int i=0;i<60;++i)sim.step();CHECK(statuses(module,sim,unit)->active.empty());CHECK(effectiveStats(module,sim,unit,stats)->front().value==Fixed::fromInt(4));
    std::string error;CHECK(sim.deserialize(save,&error));CHECK(sim.checksum()==hash);CHECK(statuses(module,sim,unit)->active.size()==1);
    // Optional bounds saturate the final authoritative stat, including status modifiers.
    {
        Simulation bounded({},std::make_unique<game::GameCore>());
        bounded.setup(Json::parse(R"({"minUnitSpeed":2,"maxUnitSpeed":3,"victory":"noUnits","statuses":{"slow":{"mods":[{"stat":"speed","mul":0.25}]}},"types":{"unit":{"health":{"max":100},"move":{"speed":4}}}})"),Json::parse(R"({"size":[8,8],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":["........","........","........","........","........","........","........","........"]},"entities":[{"type":"unit","owner":1,"pos":[2,2]},{"type":"unit","owner":2,"pos":[6,6]}]})"));
        Entity e;bounded.world().each<game::Unit>([&](Entity candidate,auto&){if(!e.valid())e=candidate;});CHECK(e.valid());
        auto& m=static_cast<game::GameCore&>(bounded.module());
        CHECK(effectiveStats(m,bounded,e,stats)->front().value==Fixed::fromInt(3));
        game::EffectCtx ctx;ctx.caster=e;ctx.owner=1;ctx.target=e;
        CHECK(applyStatus(m,bounded,e,0,Fixed::fromInt(2),ctx));
        CHECK(effectiveStats(m,bounded,e,stats)->front().value==Fixed::fromInt(2));
        auto bytes=bounded.serialize();auto h=bounded.checksum();
        for(int i=0;i<60;++i)bounded.step();
        CHECK(effectiveStats(m,bounded,e,stats)->front().value==Fixed::fromInt(3));
        CHECK(bounded.deserialize(bytes,&error));CHECK(bounded.checksum()==h);
        CHECK(effectiveStats(m,bounded,e,stats)->front().value==Fixed::fromInt(2));
        for(const char* invalid:{R"({"minUnitSpeed":-1})",R"({"maxUnitSpeed":"fast"})",R"({"minUnitSpeed":4,"maxUnitSpeed":2})"}){
            bool rejected=false;try{(void)game::loadRules(Json::parse(invalid));}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        }
    }
    const std::array<LootRow,2> rows{{{1,9,false},{2,1,true}}};LootState a,b;auto snapshot=sim.serialize();std::vector<std::int64_t> first,second;
    for(int i=0;i<30;++i)first.push_back(*rollSimLoot(sim,rows,{3},a));CHECK(sim.deserialize(snapshot,&error));
    for(int i=0;i<30;++i)second.push_back(*rollSimLoot(sim,rows,{3},b));CHECK(first==second&&a.misses==b.misses);
    auto command=interactionCommand(BuiltinInteraction::UseItem,1,unit,unit,{},2);CHECK(command&&command->type==game::kCmdUseItem&&command->param==2);CHECK(!interactionCommand(BuiltinInteraction::Smart,0,unit));
    CHECK(!statuses(module,sim,kNullEntity));CHECK(!effectiveStats(module,sim,kNullEntity,stats));
    auto config=aiverse::lib::gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"loot":{"guaranteeAfter":3},"perception":{"capacity":8,"lifetimeTicks":10}})"));
    CHECK(config.value&&lootConfig(*config.value).guaranteeAfter==3&&memoryConfig(*config.value).capacity==8);
    game::NativeState state;ReservationBook book;CHECK(reserve(book,{{1,10}},1,1,{{1,3}})==ReserveResult::Ok);CHECK(storeReservations(state,"reservations",book));CHECK(loadReservations(state,"reservations")->tickets==book.tickets);
    CHECK(storeSequence(state,"sequence",{1,2,3,true,SequencePhase::Running}));CHECK(loadSequence(state,"sequence")->stage==1);
    PerceptionMemory memory;CHECK(observe(memory,{2,10},{1,2,3,4,5,6}));CHECK(storeMemory(state,"memory",memory));CHECK(loadMemory(state,"memory",2)->known.at(1).x==2);CHECK(!loadMemory(state,"memory",0));
    CHECK(storeLoot(state,"loot",{2}));CHECK(loadLoot(state,"loot")->misses==2);state.arrays["loot"][1]=-1;CHECK(!loadLoot(state,"loot"));
    state.arrays["reservations"].pop_back();CHECK(!loadReservations(state,"reservations"));
    std::cout<<"Engine stats/statuses, commands, sim RNG and restoration: OK\n";
}
