#pragma once
// Sokoban sessions for puzzle-solvers over the grid-puzzles rules (strategy/Puzzles.h: pushMove, PushHistory undo/redo,
// pushDeadCells / pushDeadlocked, solvePushAStar). Adds strictly increasing move numbers, push/undo counters and the
// command struct players and bots share. The board/state legality stays owned by grid-puzzles.
#include "PuzzleCommon.h"
#include "../strategy/Puzzles.h"
namespace aiverse::lib::genre {
struct PuzzleSokobanLevel {strategy::PushBoard board;strategy::PushState start;uint32_t undoCapacity=256;};
enum class PuzzleSokobanAction:uint8_t {Move,Undo,Redo};
struct PuzzleSokobanCommand {uint32_t move=0;PuzzleSokobanAction action=PuzzleSokobanAction::Move;int32_t direction=0;};
struct PuzzleSokobanSession {strategy::PushHistory history;uint32_t moves=0,pushes=0,undos=0,lastMove=0;bool solved=false;};
struct PuzzleSokobanSolution {PuzzleSearch status=PuzzleSearch::Invalid;std::vector<int32_t> directions;uint64_t nodes=0;};
inline bool valid(const PuzzleSokobanLevel& l){return strategy::valid(l.board,l.start)&&l.undoCapacity>=1&&l.undoCapacity<=4096;}
inline bool valid(const PuzzleSokobanSession& s){
    if(s.lastMove<s.moves||s.history.past.size()>4096||s.history.future.size()>4096)return false;
    auto ok=[](const strategy::PushState& p){return p.player>=0&&p.player<4096&&!p.boxes.empty()&&p.boxes.size()<=256&&std::is_sorted(p.boxes.begin(),p.boxes.end())&&std::adjacent_find(p.boxes.begin(),p.boxes.end())==p.boxes.end();};
    if(!ok(s.history.current))return false;
    for(const auto* v:{&s.history.past,&s.history.future})for(const auto& p:*v)if(!ok(p))return false;
    return true;
}
inline bool puzzleSokobanCompatible(const PuzzleSokobanLevel& l,const PuzzleSokobanSession& s){
    if(!valid(l)||!valid(s)||!strategy::valid(l.board,s.history.current)||s.history.past.size()>l.undoCapacity)return false;
    for(const auto* v:{&s.history.past,&s.history.future})for(const auto& p:*v)if(!strategy::valid(l.board,p))return false;
    return true;
}
inline PuzzleSokobanSession puzzleSokobanBegin(const PuzzleSokobanLevel& l){PuzzleSokobanSession s;s.history.current=l.start;s.solved=strategy::pushSolved(l.board,l.start);return s;}
inline bool puzzleSokobanApply(const PuzzleSokobanLevel& l,PuzzleSokobanSession& s,const PuzzleSokobanCommand& c){
    if(c.move<=s.lastMove||!puzzleSokobanCompatible(l,s))return false;
    auto next=s.history;bool ok=false;
    switch(c.action){
    case PuzzleSokobanAction::Move:
        if(s.solved)return false;
        ok=strategy::pushMove(l.board,next,c.direction,l.undoCapacity);
        if(ok&&next.current.boxes!=s.history.current.boxes)++s.pushes;
        break;
    case PuzzleSokobanAction::Undo:ok=strategy::pushUndo(l.board,next);if(ok)++s.undos;break;
    case PuzzleSokobanAction::Redo:ok=strategy::pushRedo(l.board,next);break;
    }
    if(!ok)return false;
    s.history=std::move(next);++s.moves;s.lastMove=c.move;s.solved=strategy::pushSolved(l.board,s.history.current);return true;
}
inline bool puzzleSokobanDeadlocked(const PuzzleSokobanLevel& l,const PuzzleSokobanSession& s){return !s.solved&&strategy::pushDeadlocked(l.board,s.history.current);}
inline PuzzleSokobanSolution puzzleSokobanSolve(const PuzzleSokobanLevel& l,const strategy::PushState& from,PuzzleBudget budget={}){
    PuzzleSokobanSolution out;if(!valid(l)||!puzzleValidBudget(budget))return out;
    auto r=strategy::solvePushAStar(l.board,from,size_t(budget.nodes-budget.used));out.nodes=r.expanded;
    switch(r.status){
    case strategy::SolveStatus::Solved:out.status=PuzzleSearch::Solved;out.directions=std::move(r.moves);break;
    case strategy::SolveStatus::Unsolvable:out.status=PuzzleSearch::Unsolvable;break;
    case strategy::SolveStatus::BudgetExceeded:out.status=PuzzleSearch::Unknown;break;
    case strategy::SolveStatus::Invalid:break;
    }
    return out;
}
// Hint: Undo out of a proven deadlock (when history allows), else the first move of an A* solution.
inline std::optional<PuzzleSokobanCommand> puzzleSokobanHint(const PuzzleSokobanLevel& l,const PuzzleSokobanSession& s,PuzzleBudget budget={}){
    if(s.solved||!puzzleSokobanCompatible(l,s))return std::nullopt;
    if(puzzleSokobanDeadlocked(l,s))return s.history.past.empty()?std::nullopt:std::optional{PuzzleSokobanCommand{s.lastMove+1,PuzzleSokobanAction::Undo,0}};
    auto r=puzzleSokobanSolve(l,s.history.current,budget);
    if(r.status!=PuzzleSearch::Solved||r.directions.empty())return std::nullopt;
    return PuzzleSokobanCommand{s.lastMove+1,PuzzleSokobanAction::Move,r.directions.front()};
}
}
