// Executable puzzle-solvers example: a match-3 level with goals played to completion by the puzzle bot, a Sokoban
// level solved by the A* solver and replayed through the same move commands a player sends, and a sudoku checked for
// a unique solution and filled in by the bot. Every state goes through save/load; the run prints a final hash and
// repeats the whole scenario to prove the result is identical. Usage: aiverse_puzzles_example [levels.json]
#if defined(__GNUC__)&&!defined(__clang__)
#pragma GCC diagnostic ignored "-Wmisleading-indentation" // compact one-line checks below
#endif
#include <aiverselib/bots/PuzzleBot.h>
#include <cstdio>
#include <string>
using namespace aiverse;
using namespace aiverse::lib;
namespace {
struct Outcome {bool complete=false;uint64_t hash=0;std::string log;};
template<class State> bool persisted(const State& s){auto bytes=genre::save(s);State copy;return !bytes.empty()&&genre::load(bytes,copy)&&genre::save(copy)==bytes;}
void line(std::string& log,const char* format,auto... args){char buffer[256];std::snprintf(buffer,sizeof buffer,format,args...);log+=buffer;log+='\n';}
Outcome run(const Json& levels){
    Outcome out;Hasher hash;bool ok=true;
    // Match-3: seeded board, goals from data, a normal-strength bot (Best hints, 8% random swaps) plays every move.
    {
        const auto level=genre::readMatch3Level(levels["match3"]);Random rng(20261112,1);
        auto state=genre::match3Begin(level,rng);ok=ok&&state.has_value();
        auto skill=*bots::puzzleBotSkillPreset(1);uint32_t specials=0,cascades=0;
        for(int step=0;state&&step<1000&&state->status==genre::Match3Status::Playing;++step){
            auto cmd=bots::puzzleBotMatch3(level,*state,skill,rng);if(!cmd){ok=false;break;}
            auto report=genre::match3Apply(level,*state,*cmd,rng);if(!report){ok=false;break;}
            specials+=uint32_t(report->created.size());cascades+=report->chains>1;
        }
        const bool won=state&&state->status==genre::Match3Status::Won;ok=ok&&won&&persisted(*state);
        if(state){
            line(out.log,"match3: %s after %u moves, score %lld, collected c1=%u c3=%u, specials %u, cascades %u, shuffles %u, max chain %u",
                won?"won":"not won",state->moves,(long long)state->score,state->collected[1],state->collected[3],specials,cascades,state->shuffles,state->maxChain);
            hash.u64(genre::puzzleHash(*state));
        }
    }
    // Sokoban: solve once with A* (node budget), then replay the directions as player commands.
    {
        const auto level=genre::readSokobanLevel(levels["sokoban"]);auto session=genre::puzzleSokobanBegin(level);
        auto solution=genre::puzzleSokobanSolve(level,session.history.current,{200000,0});
        const bool solvedBySearch=solution.status==genre::PuzzleSearch::Solved;
        for(auto d:solution.directions)ok=ok&&genre::puzzleSokobanApply(level,session,{session.lastMove+1,genre::PuzzleSokobanAction::Move,d});
        // Undo and redo the last move through the same command path, as a player would.
        ok=ok&&genre::puzzleSokobanApply(level,session,{session.lastMove+1,genre::PuzzleSokobanAction::Undo,0})&&!session.solved;
        ok=ok&&genre::puzzleSokobanApply(level,session,{session.lastMove+1,genre::PuzzleSokobanAction::Redo,0});
        ok=ok&&solvedBySearch&&session.solved&&persisted(session);
        line(out.log,"sokoban: %s, %zu moves (%u pushes), %llu nodes expanded",session.solved?"solved":"not solved",solution.directions.size(),session.pushes,(unsigned long long)solution.nodes);
        hash.u64(genre::puzzleHash(session));
    }
    // Sudoku: uniqueness proof, then the hard bot fills it one assignment at a time.
    {
        const auto csp=genre::readSudoku(levels["sudoku"]);auto unique=genre::puzzleCspUnique(csp,{200000,0});
        auto state=genre::puzzleCspBegin(csp);Random rng(7,3);auto skill=*bots::puzzleBotSkillPreset(2);uint32_t givens=0;
        for(int32_t c=0;c<int32_t(csp.domains.size());++c)givens+=genre::puzzleCspGiven(csp,c);
        for(int step=0;step<200&&!state.solved;++step){auto cmd=bots::puzzleBotCsp(csp,state,skill,rng);if(!cmd||!genre::puzzleCspApply(csp,state,*cmd)){ok=false;break;}}
        ok=ok&&unique.unique&&state.solved&&state.values==unique.values&&persisted(state);
        line(out.log,"sudoku: %s solution, %u givens, solved in %u moves, %llu search nodes",unique.unique?"unique":"non-unique",givens,state.moves,(unsigned long long)unique.nodes);
        hash.u64(genre::puzzleHash(state));
    }
    out.complete=ok;out.hash=hash.value();return out;
}
}
int main(int argc,char** argv){
    const std::string path=argc>1?argv[1]:"examples/libraries/genre/puzzles/levels.json";
    try{
        const auto levels=Json::parseFile(path);
        const auto first=run(levels),second=run(levels);
        std::fputs(first.log.c_str(),stdout);
        std::printf("final hash: %016llx (second run %016llx)\n",(unsigned long long)first.hash,(unsigned long long)second.hash);
        if(!first.complete||!second.complete||first.hash!=second.hash||first.log!=second.log){std::puts("puzzles example FAILED");return 1;}
        std::puts("puzzles example completed");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
