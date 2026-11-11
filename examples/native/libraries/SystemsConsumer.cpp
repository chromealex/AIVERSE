#include <aiverselib/systems/EngineMechanics.h>
#include <aiverselib/systems/Persistence.h>
#include <aiverselib/systems/Configured.h>
#include <aiverselib/systems/Interactions.h>
#include <aiverselib/systems/Curves.h>
#include <array>
// Custom workshop state only: built-in RTS inventories/statuses remain owned by the engine.
// Caller uses a module's NativeState; no persistent library object is stored on the module instance.
bool librarySystemsConsumerExample(aiverse::game::NativeState& state){
    using namespace aiverse::lib::systems;
    ReservationBook book;Quantities capacity{{1,10}};
    if(reserve(book,capacity,1,1,{{1,3}})!=ReserveResult::Ok)return false;
    auto next=state;
    if(!storeReservations(next,"workshop.reservations",book)||!storeSequence(next,"workshop.sequence",{})||!storeLoot(next,"workshop.loot",{}))return false;
    next.values["workshop.capacity"]=capacity.at(1);next.values["workshop.lastLoot"]=-1;state=std::move(next);return true;
}
bool librarySystemsTickExample(aiverse::Simulation& sim,aiverse::game::NativeState& state){
    using namespace aiverse::lib::systems;
    auto book=loadReservations(state,"workshop.reservations");auto sequence=loadSequence(state,"workshop.sequence");auto loot=loadLoot(state,"workshop.loot");
    auto capacity=state.values.find("workshop.capacity");if(!book||!sequence||!loot||capacity==state.values.end())return false;
    Quantities stock{{1,capacity->second}};const std::array<ActionStage,1> plan{{{1,200,220}}};bool success=true;
    if(sequence->phase==SequencePhase::Ready||sequence->phase==SequencePhase::Running)
        success=advanceSequence(plan,*sequence,sim.tick(),[&](auto){return book->tickets.count(1)!=0;},[](auto){return StageResult::Complete;},[&](auto,bool completed){
            success=(completed?commit(*book,stock,1,1):release(*book,1,1))==ReserveResult::Ok;
        })&&success;
    if(!success)return false;
    auto next=state;
    if(sequence->phase==SequencePhase::Complete&&sim.tick()%20==0){
        const std::array<LootRow,2> rows{{{1,9,false},{2,1,true}}};auto reward=rollSimLoot(sim,rows,{3},*loot);if(!reward)return false;next.values["workshop.lastLoot"]=*reward;
    }
    if(!storeReservations(next,"workshop.reservations",*book)||!storeSequence(next,"workshop.sequence",*sequence)||!storeLoot(next,"workshop.loot",*loot))return false;
    next.values["workshop.capacity"]=stock.at(1);state=std::move(next);return true;
}
