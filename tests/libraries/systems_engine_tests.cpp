#include "engine/sim/PresentationEventQueue.h"
#include <aiverselib/systems/EngineMechanics.h>
#include <aiverselib/systems/Configured.h>
#include <aiverselib/systems/Persistence.h>
#include <array>
#include <cstdlib>
#include <iostream>
using namespace aiverse;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<": "<<#x<<'\n';std::exit(1);}}while(false)
int main(){
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
