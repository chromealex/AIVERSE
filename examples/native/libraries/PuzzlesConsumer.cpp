#include <aiverselib/genre/PuzzleSolvers.h>
#include <aiverselib/bots/PuzzleBot.h>
#include "modules/rts/RtsNative.h"
// puzzle-solvers in a native module: validated level data, the authoritative command path shared by the player
// and the puzzle bot, then per-puzzle records in NativeState (record kinds 112..119) plus their checksums.
// A real game routes Match3Swap / PuzzleSokobanCommand / PuzzleCspAssign from its command handler instead.
bool libraryPuzzlesConsumerExample(aiverse::game::NativeState& state){
    using namespace aiverse::lib::genre;namespace bots=aiverse::lib::bots;
    const auto levels=aiverse::Json::parse(R"({
        "match3":{"rows":["......","......","......","......","......","......"],"colors":4,
            "specials":[{"shape":"line4","special":"stripe"},{"shape":"corner","special":"bomb"}],"goals":{"moves":12,"score":200}},
        "sokoban":{"rows":["######","#@$ .#","######"]},
        "sudoku":{"box":[2,2],"rows":[".2..","3..2","..4.","4..1"]}})");
    const auto match3=readMatch3Level(levels["match3"]);const auto sokoban=readSokobanLevel(levels["sokoban"]);const auto sudoku=readSudoku(levels["sudoku"]);
    aiverse::Random rng(42,1);auto board=match3Begin(match3,rng);if(!board)return false;
    const auto skill=*bots::puzzleBotSkillPreset(2);
    for(int i=0;i<3&&board->status==Match3Status::Playing;++i){auto cmd=bots::puzzleBotMatch3(match3,*board,skill,rng);if(!cmd||!match3Apply(match3,*board,*cmd,rng))return false;}
    auto session=puzzleSokobanBegin(sokoban);
    while(!session.solved){auto cmd=bots::puzzleBotSokoban(sokoban,session,skill,rng);if(!cmd||!puzzleSokobanApply(sokoban,session,*cmd))return false;}
    auto unique=puzzleCspUnique(sudoku,{20000,0});if(!unique.unique)return false;
    auto grid=puzzleCspBegin(sudoku);auto hint=puzzleCspHint(sudoku,grid);if(!hint||!puzzleCspApply(sudoku,grid,*hint))return false;
    const uint64_t checksum=puzzleHash(*board)^(puzzleHash(session)<<1)^(puzzleHash(grid)<<2);
    state.values["genre.puzzles_hash"]=int64_t(checksum&0x7fffffffffffffffull);
    return storeBytes(state,"genre.puzzles.match3",save(*board))&&storeBytes(state,"genre.puzzles.sokoban",save(session))
        &&storeBytes(state,"genre.puzzles.sudoku",save(grid));
}
