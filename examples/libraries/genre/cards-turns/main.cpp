// Executable cards + turns example (card-games, tactics-grid, CardBot, TacticsBot): a 2-seat card duel with a stack
// and hidden hands, and a hex skirmish between two squads using AP, initiative, cover and overwatch. Both games are
// played bot-vs-bot from per-seat/per-team views only. Each is played twice from the same seed; the program prints
// the winners and final hashes and fails unless both games have a winner and both runs hash identically.
// Usage: aiverse_cards_turns_example <dir with duel.json and skirmish.json> [seed]
#include <aiverselib/genre/CardGameData.h>
#include <aiverselib/genre/CardGamePersistence.h>
#include <aiverselib/genre/TacticsData.h>
#include <aiverselib/genre/TacticsPersistence.h>
#include <aiverselib/bots/CardBot.h>
#include <aiverselib/bots/TacticsBot.h>
#include <cstdio>
#include <exception>
#include <string>
using namespace aiverse;using namespace aiverse::lib;
struct Outcome{int32_t winner=-1;uint32_t steps=0;uint64_t hash=0;std::string detail;};
static void fold(uint64_t& h,uint64_t v){Hasher x;x.u64(h);x.u64(v);h=x.value();}

static Outcome playDuel(const genre::CardLibrary& lib,const std::vector<std::vector<int64_t>>& decks,uint64_t seed){
    Random rng(seed);Outcome out;auto first=int32_t(rng.below(2));
    auto setup=genre::cardGameSetup(lib,decks,first,rng);if(!setup)throw std::runtime_error("duel setup rejected");
    auto state=setup->state;uint64_t h=genre::cardGameHash(state),seq=0;bots::CardBotConfig cfg;
    for(;state.phase!=genre::CardPhase::GameOver&&out.steps<20000;++out.steps){
        auto seat=state.priority;auto view=genre::cardViewFor(lib,state,seat);if(!view)throw std::runtime_error("view failed");
        auto choice=bots::cardBotChoose(lib,*view,cfg);if(!choice)throw std::runtime_error("card bot found no action");
        auto report=genre::stepCardGame(lib,state,{++seq,seat,choice->action},rng);if(!report)throw std::runtime_error("card bot action rejected");
        for(const auto& e:report->events)fold(h,genre::cardGameHash(e));
    }
    fold(h,genre::cardGameHash(state));out.winner=state.winner;out.hash=h;
    out.detail="turn "+std::to_string(state.turn)+", health "+std::to_string(state.seats[0].health)+"/"+std::to_string(state.seats[1].health);
    return out;
}
static Outcome playSkirmish(const genre::TacticsScenarioData& data,uint64_t seed){
    Random rng(seed);Outcome out;const auto& sc=data.scenario;
    auto state=genre::tacticsBegin(sc,data.units);if(!state)throw std::runtime_error("skirmish setup rejected");
    // Objective per team: the opposing deployment zone (map data both sides know), never a live enemy position.
    auto objective=[&](int32_t team){return genre::tacticsIndex(sc.grid,{team==1?sc.grid.width-2:1,sc.grid.height/2});};
    uint64_t h=genre::tacticsHash(*state),seq=0;bots::TacticsBotConfig cfg;
    for(;state->winner==genre::kTacticsNoWinner&&out.steps<20000;++out.steps){
        auto unit=genre::tacticsActiveUnit(*state);auto team=state->units.at(unit).team;
        auto view=genre::tacticsViewFor(sc,*state,team);if(!view)throw std::runtime_error("view failed");
        auto choice=bots::tacticsBotChoose(sc,*view,objective(team),cfg);if(!choice)throw std::runtime_error("tactics bot found no action");
        auto report=genre::stepTactics(sc,*state,{++seq,team,unit,choice->action},rng);
        if(!report)report=genre::stepTactics(sc,*state,{++seq,team,unit,{}},rng); // planned into an unseen unit
        if(!report)throw std::runtime_error("end turn rejected");
        for(const auto& e:report->events)fold(h,genre::tacticsHash(e));
    }
    fold(h,genre::tacticsHash(*state));out.winner=state->winner;out.hash=h;
    int32_t alive=0;for(const auto& [id,u]:state->units){(void)id;if(u.alive&&u.team==state->winner)++alive;}
    out.detail="round "+std::to_string(state->round)+", "+std::to_string(alive)+" survivors";
    return out;
}
int main(int argc,char** argv){
    try{
        if(argc<2){std::fprintf(stderr,"usage: %s <dir> [seed]\n",argv[0]);return 2;}
        std::string dir=argv[1];uint64_t seed=argc>2?std::stoull(argv[2]):7;
        auto duelJson=Json::parseFile(dir+"/duel.json");auto lib=genre::readCardLibrary(duelJson);auto decks=genre::readCardDecks(duelJson,lib);
        auto skirmish=genre::readTacticsScenario(Json::parseFile(dir+"/skirmish.json"));
        auto d1=playDuel(lib,decks,seed),d2=playDuel(lib,decks,seed);
        auto s1=playSkirmish(skirmish,seed),s2=playSkirmish(skirmish,seed);
        std::printf("card duel: winner seat %d after %u commands (%s), hash %016llx\n",d1.winner,d1.steps,d1.detail.c_str(),(unsigned long long)d1.hash);
        std::printf("hex skirmish: winner team %d after %u commands (%s), hash %016llx\n",s1.winner,s1.steps,s1.detail.c_str(),(unsigned long long)s1.hash);
        uint64_t final=d1.hash;fold(final,s1.hash);std::printf("final hash %016llx\n",(unsigned long long)final);
        bool same=d1.hash==d2.hash&&d1.winner==d2.winner&&s1.hash==s2.hash&&s1.winner==s2.winner;
        bool won=d1.winner>=0&&s1.winner>0;
        std::printf("deterministic replay: %s, winners: %s\n",same?"identical":"MISMATCH",won?"yes":"NO");
        return same&&won?0:1;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
