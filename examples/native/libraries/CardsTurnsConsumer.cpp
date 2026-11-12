// Cards + turn strategy consumer: validated settings, card JSON with rule variants and a hex tactics scenario, a few
// bot-driven authoritative commands on each, versioned records in NativeState. A real native module passes sim.rng()
// (shuffles, shot rolls), routes player commands through the same stepCardGame/stepTactics calls as the bots, sends
// each seat/team only cardViewFor/cardEventsFor or tacticsViewFor/tacticsEventsFor, and never the full state.
#include <aiverselib/genre/CardGameConfigured.h>
#include <aiverselib/genre/CardGameData.h>
#include <aiverselib/genre/CardGamePersistence.h>
#include <aiverselib/genre/TacticsConfigured.h>
#include <aiverselib/genre/TacticsData.h>
#include <aiverselib/genre/TacticsPersistence.h>
#include <aiverselib/bots/CardBot.h>
#include <aiverselib/bots/TacticsBot.h>
#include "modules/rts/RtsNative.h"
bool libraryCardsTurnsConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;namespace bots=aiverse::lib::bots;
    aiverse::Random rng(0x51ed270bu);
    auto cards=aiverse::Json::parse(R"({"rules":{"mulligan":"none","deck":[6,30]},
        "cards":[{"id":1,"name":"card.bolt","kind":"instant","cost":1,"target":"character","side":"enemy","effects":[{"op":"damage","amount":2}]},
                 {"id":2,"name":"card.squire","kind":"unit","cost":1,"power":1,"health":2,"guard":true}],
        "decks":[[1,1,1,2,2,2],[2,2,2,1,1,1]]})");
    auto lib=readCardLibrary(cards,cardRulesFromSettings(settings));auto decks=readCardDecks(cards,lib);
    auto setup=cardGameSetup(lib,decks,0,rng);if(!setup)return false;auto game=setup->state;
    for(uint64_t seq=1;seq<=6&&game.phase!=CardPhase::GameOver;++seq){
        auto seat=game.priority;auto view=cardViewFor(lib,game,seat);if(!view)return false;
        auto choice=bots::cardBotChoose(lib,*view,bots::cardBotConfig(settings));if(!choice||!stepCardGame(lib,game,{seq,seat,choice->action},rng))return false;
    }
    auto skirmish=readTacticsScenario(aiverse::Json::parse(R"({"grid":{"shape":"hex","rows":["..h..","..H..","....."]},
        "unitTypes":[{"id":1,"name":"unit.rifleman","hp":6,"mobility":2,"range":[3,5],"sight":6}],
        "units":[{"id":1,"type":1,"team":1,"at":[0,1]},{"id":2,"type":1,"team":2,"at":[4,1]}]})"),tacticsRulesFromSettings(settings));
    auto tactics=tacticsBegin(skirmish.scenario,skirmish.units);if(!tactics)return false;
    auto unit=tacticsActiveUnit(*tactics);auto team=tactics->units.at(unit).team;auto view=tacticsViewFor(skirmish.scenario,*tactics,team);
    auto plan=view?bots::tacticsBotChoose(skirmish.scenario,*view,tacticsIndex(skirmish.scenario.grid,{2,2}),bots::tacticsBotConfig(settings)):std::nullopt;
    if(!plan||!stepTactics(skirmish.scenario,*tactics,{1,team,unit,plan->action},rng))return false;
    state.values["genre.cards.library"]=int64_t(cardLibraryHash(lib)&0x7fffffffffffffffull);
    state.values["genre.tactics.scenario"]=int64_t(tacticsScenarioHash(skirmish.scenario)&0x7fffffffffffffffull);
    state.values["genre.cards_turns_hash"]=int64_t((cardGameHash(game)^tacticsHash(*tactics))&0x7fffffffffffffffull);
    return storeBytes(state,"genre.cards.state",save(game))&&storeBytes(state,"genre.tactics.state",save(*tactics));
}
