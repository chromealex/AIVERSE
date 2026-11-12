#pragma once
// Puzzle-solving bot (puzzle-solvers): plays every puzzle family through the same command structs a player submits
// (Match3Swap, PuzzleFloodPick, PuzzlePipeRotate, PuzzleTilesSlide, PuzzleBlocksMove, PuzzlePackCommand,
// PuzzleCspAssign, PuzzleSokobanCommand), numbered lastMove + 1. The owner applies them with the ordinary *Apply
// function, which re-validates everything. Difficulty = search budget for hints/solvers + a mistake chance that
// swaps the hinted move for a random legal one; randomness only from the passed-in Random (sim.rng() or an RNG
// saved with the simulation). The bot is stateless: nothing to persist beyond the puzzle state and the RNG.
#include "../genre/PuzzleSolvers.h"
#include "engine/core/Random.h"
namespace aiverse::lib::bots {
struct PuzzleBotSkill {
    uint64_t budget=20000;                                         // node budget per hint/solver call
    int64_t mistakeMilli=0;                                        // 0..1000 chance to play a random legal move
    genre::Match3HintMode match3=genre::Match3HintMode::Best;      // First = shallow play, Best = previewed value
};
inline bool valid(const PuzzleBotSkill& s){return s.budget>=1&&s.budget<=genre::kPuzzleMaxBudget&&s.mistakeMilli>=0&&s.mistakeMilli<=1000&&s.match3<=genre::Match3HintMode::Best;}
// 0 easy, 1 normal, 2 hard.
inline std::optional<PuzzleBotSkill> puzzleBotSkillPreset(int level){
    switch(level){
    case 0:return PuzzleBotSkill{500,250,genre::Match3HintMode::First};
    case 1:return PuzzleBotSkill{5000,80,genre::Match3HintMode::Best};
    case 2:return PuzzleBotSkill{100000,0,genre::Match3HintMode::Best};
    default:return std::nullopt;
    }
}
namespace detail_puzzlebot {
// Consumes RNG only for chances strictly between 0 and 1000, so a perfect bot never touches the stream.
inline bool mistake(const PuzzleBotSkill& s,Random& rng){if(s.mistakeMilli<=0)return false;if(s.mistakeMilli>=1000)return true;return int64_t(rng.below(1000))<s.mistakeMilli;}
template<class T> std::optional<T> any(const std::vector<T>& v,Random& rng){if(v.empty())return std::nullopt;return v[rng.below(uint32_t(v.size()))];}
inline genre::PuzzleBudget budget(const PuzzleBotSkill& s){return genre::PuzzleBudget{s.budget,0};}
}
inline std::optional<genre::Match3Swap> puzzleBotMatch3(const genre::Match3Level& l,const genre::Match3State& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.status!=genre::Match3Status::Playing)return std::nullopt;
    std::optional<std::pair<int32_t,int32_t>> pick;
    if(detail_puzzlebot::mistake(skill,rng))pick=detail_puzzlebot::any(genre::match3Moves(l.rules,s.tiles),rng);
    if(!pick)pick=genre::match3Hint(l,s,skill.match3);
    if(!pick)return std::nullopt;
    return genre::Match3Swap{s.lastMove+1,pick->first,pick->second};
}
inline std::optional<genre::PuzzleFloodPick> puzzleBotFlood(const genre::PuzzleFloodLevel& l,const genre::PuzzleFloodState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.status!=genre::PuzzleFloodStatus::Playing||!genre::puzzleFloodCompatible(l,s))return std::nullopt;
    std::optional<uint8_t> color;
    if(detail_puzzlebot::mistake(skill,rng)){std::vector<uint8_t> all;for(uint8_t c=1;c<=l.colors;++c)if(c!=s.cells[size_t(l.origin)])all.push_back(c);color=detail_puzzlebot::any(all,rng);}
    if(!color)color=genre::puzzleFloodHint(l,s,detail_puzzlebot::budget(skill));
    if(!color)return std::nullopt;
    return genre::PuzzleFloodPick{s.lastMove+1,*color};
}
inline std::optional<genre::PuzzlePipeRotate> puzzleBotPipes(const genre::PuzzlePipeLevel& l,const genre::PuzzlePipeState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!genre::puzzlePipeCompatible(l,s))return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){std::vector<int32_t> cells;for(int32_t c=0;c<int32_t(l.flags.size());++c)if(l.flags[size_t(c)]&genre::kPuzzlePipeRotatable)cells.push_back(c);if(auto c=detail_puzzlebot::any(cells,rng))return genre::PuzzlePipeRotate{s.lastMove+1,*c,1};}
    return genre::puzzlePipeHint(l,s,detail_puzzlebot::budget(skill));
}
inline std::optional<genre::PuzzleTilesSlide> puzzleBotTiles(const genre::PuzzleTilesState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!valid(s))return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){std::vector<int32_t> cells;const auto blank=genre::puzzleTilesBlank(s);for(int32_t d=0;d<4;++d){auto c=genre::puzzleStep(s.width,s.height,blank,d);if(c>=0)cells.push_back(c);}if(auto c=detail_puzzlebot::any(cells,rng))return genre::PuzzleTilesSlide{s.lastMove+1,*c};}
    auto cell=genre::puzzleTilesHint(s,detail_puzzlebot::budget(skill));if(!cell)return std::nullopt;return genre::PuzzleTilesSlide{s.lastMove+1,*cell};
}
inline std::optional<genre::PuzzleBlocksMove> puzzleBotBlocks(const genre::PuzzleBlocksLevel& l,const genre::PuzzleBlocksState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!valid(l)||s.positions.size()!=l.blocks.size())return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){
        std::vector<std::pair<int32_t,int32_t>> moves;for(int32_t b=0;b<int32_t(l.blocks.size());++b)for(int32_t d=0;d<4;++d){auto p=s.positions;if(genre::puzzleBlocksShift(l,p,b,d))moves.push_back({b,d});}
        if(auto m=detail_puzzlebot::any(moves,rng))return genre::PuzzleBlocksMove{s.lastMove+1,m->first,m->second};
    }
    auto r=genre::puzzleBlocksSolve(l,s.positions,detail_puzzlebot::budget(skill));
    if(r.status!=genre::PuzzleSearch::Solved||r.moves.empty())return std::nullopt;
    return genre::PuzzleBlocksMove{s.lastMove+1,r.moves.front().first,r.moves.front().second};
}
inline std::optional<genre::PuzzlePackCommand> puzzleBotPack(const genre::PuzzlePackLevel& l,const genre::PuzzlePackState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!valid(l)||!valid(s))return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){
        // A random piece/orientation that fits at the first uncovered target cell.
        std::vector<genre::PuzzlePackCommand> options;
        for(int32_t p=0;p<int32_t(l.pieces.size());++p){
            if(std::any_of(s.placed.begin(),s.placed.end(),[&](const genre::PuzzlePlacement& x){return x.piece==p;}))continue;
            auto orient=genre::puzzlePieceOrientations(l.pieces[size_t(p)]);
            for(int32_t y=0;y<l.height;++y)for(int32_t x=0;x<l.width;++x)for(int32_t o=0;o<int32_t(orient.size());++o){
                auto copy=s;genre::PuzzlePackCommand c{s.lastMove+1,genre::PuzzlePackAction::Place,{p,o,x,y}};if(genre::puzzlePackApply(l,copy,c))options.push_back(c);
            }
        }
        if(auto c=detail_puzzlebot::any(options,rng))return c;
    }
    return genre::puzzlePackHint(l,s,detail_puzzlebot::budget(skill));
}
inline std::optional<genre::PuzzleCspAssign> puzzleBotCsp(const genre::PuzzleCsp& p,const genre::PuzzleCspState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved)return std::nullopt;
    auto hint=genre::puzzleCspHint(p,s,detail_puzzlebot::budget(skill));if(!hint)return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)&&s.values[size_t(hint->cell)]<0){
        // Right cell, guessed value: the hint later repairs a wrong guess.
        std::vector<int8_t> values;for(int32_t v=0;v<=genre::kPuzzleCspMaxValue;++v)if(p.domains[size_t(hint->cell)]&(1u<<v))values.push_back(int8_t(v));
        if(auto v=detail_puzzlebot::any(values,rng))hint->value=*v;
    }
    return hint;
}
inline std::optional<genre::PuzzleSokobanCommand> puzzleBotSokoban(const genre::PuzzleSokobanLevel& l,const genre::PuzzleSokobanSession& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!genre::puzzleSokobanCompatible(l,s))return std::nullopt;
    if(!genre::puzzleSokobanDeadlocked(l,s)&&detail_puzzlebot::mistake(skill,rng)){
        std::vector<int32_t> dirs;for(int32_t d=0;d<4;++d){auto copy=s.history.current;if(strategy::pushMove(l.board,copy,d))dirs.push_back(d);}
        if(auto d=detail_puzzlebot::any(dirs,rng))return genre::PuzzleSokobanCommand{s.lastMove+1,genre::PuzzleSokobanAction::Move,*d};
    }
    if(auto hint=genre::puzzleSokobanHint(l,s,detail_puzzlebot::budget(skill)))return hint;
    // No solution known from here (budget or proof): back out of the last move and try again.
    if(!s.history.past.empty())return genre::PuzzleSokobanCommand{s.lastMove+1,genre::PuzzleSokobanAction::Undo,0};
    return std::nullopt;
}
}
