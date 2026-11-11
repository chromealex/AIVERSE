#include <aiverselib/systems/EngineMechanics.h>
#include <aiverselib/systems/Configured.h>
#include <aiverselib/systems/Persistence.h>
#include <array>
#include <cstdlib>
#include <iostream>
using namespace aiverse;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<": "<<#x<<'\n';std::exit(1);}}while(false)
int main(){
    Simulation sim({},std::make_unique<game::RtsModule>());
    sim.setup(Json::parse(R"({"victory":"noUnits","statuses":{"slow":{"mods":[{"stat":"speed","mul":0.5}]}},"types":{"unit":{"health":{"max":100},"move":{"speed":4}}}})"),Json::parse(R"({"size":[8,8],"players":[{"name":"A","team":1},{"name":"B","team":2}],"terrain":{"rows":["........","........","........","........","........","........","........","........"]},"entities":[{"type":"unit","owner":1,"pos":[2,2]},{"type":"unit","owner":2,"pos":[6,6]}]})"));
    Entity unit;sim.world().each<game::Unit>([&](Entity e,auto&){if(!unit.valid())unit=e;});CHECK(unit.valid());
    auto& module=static_cast<game::RtsModule&>(sim.module());std::array stats{game::Stat::Speed};
    CHECK(effectiveStats(module,sim,unit,stats)->front().value==Fixed::fromInt(4));
    game::EffectCtx context;context.caster=unit;context.owner=1;context.target=unit;
    CHECK(!applyStatus(module,sim,unit,999,Fixed::fromInt(2),context));
    CHECK(applyStatus(module,sim,unit,0,Fixed::fromInt(2),context));
    CHECK(statuses(module,sim,unit)->active.size()==1);CHECK(effectiveStats(module,sim,unit,stats)->front().value==Fixed::fromInt(2));
    auto save=sim.serialize();auto hash=sim.checksum();for(int i=0;i<60;++i)sim.step();CHECK(statuses(module,sim,unit)->active.empty());CHECK(effectiveStats(module,sim,unit,stats)->front().value==Fixed::fromInt(4));
    std::string error;CHECK(sim.deserialize(save,&error));CHECK(sim.checksum()==hash);CHECK(statuses(module,sim,unit)->active.size()==1);
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
