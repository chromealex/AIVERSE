#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for puzzle-solvers state.
// Record kinds 112..119 are reserved for this package (all used): Match3State 112, PuzzleFloodState 113,
// PuzzlePipeState 114, PuzzleTilesState 115, PuzzleBlocksState 116, PuzzlePackState 117, PuzzleCspState 118,
// PuzzleSokobanSession 119. The grid-puzzles PushHistory keeps its own strategy/Persistence.h record (kind 5).
// Store each state under its own NativeState key, feed puzzleHash(state) into the simulation checksum, and after
// loading re-check it against the level with the matching *Compatible / valid(level) helper.
#include "PuzzleData.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,Match3Tile& t){a(t.color,t.special);}
inline void io(Archive& a,Match3State& s){a(s.tiles,s.collected,s.score,s.moves,s.lastMove,s.shuffles,s.maxChain,s.status);}
inline void io(Archive& a,PuzzleFloodState& s){a(s.cells,s.moves,s.lastMove,s.status);}
inline void io(Archive& a,PuzzlePipeState& s){a(s.rotation,s.moves,s.lastMove,s.solved);}
inline void io(Archive& a,PuzzleTilesState& s){a(s.width,s.height,s.tiles,s.moves,s.lastMove,s.solved);}
inline void io(Archive& a,PuzzleBlocksState& s){a(s.positions,s.moves,s.lastMove,s.solved);}
inline void io(Archive& a,PuzzlePlacement& p){a(p.piece,p.orientation,p.x,p.y);}
inline void io(Archive& a,PuzzlePackState& s){a(s.placed,s.moves,s.lastMove,s.solved);}
inline void io(Archive& a,PuzzleCspState& s){a(s.values,s.moves,s.lastMove,s.solved);}
namespace detail_puzzlesave {
inline void push(Archive& a,strategy::PushState& s){a(s.player,s.boxes);}
inline void pushes(Archive& a,std::vector<strategy::PushState>& v){
    uint32_t n=uint32_t(v.size());a(n);if(!a.writing()){if(n>4096){a.fail();return;}v.assign(n,{});}
    for(auto& s:v){if(!a.ok())return;push(a,s);}
}
}
inline void io(Archive& a,PuzzleSokobanSession& s){
    detail_puzzlesave::push(a,s.history.current);detail_puzzlesave::pushes(a,s.history.past);detail_puzzlesave::pushes(a,s.history.future);
    a(s.moves,s.pushes,s.undos,s.lastMove,s.solved);
}
inline uint16_t stateKind(const Match3State&){return 112;}
inline uint16_t stateKind(const PuzzleFloodState&){return 113;}
inline uint16_t stateKind(const PuzzlePipeState&){return 114;}
inline uint16_t stateKind(const PuzzleTilesState&){return 115;}
inline uint16_t stateKind(const PuzzleBlocksState&){return 116;}
inline uint16_t stateKind(const PuzzlePackState&){return 117;}
inline uint16_t stateKind(const PuzzleCspState&){return 118;}
inline uint16_t stateKind(const PuzzleSokobanSession&){return 119;}
// Canonical Archive bytes folded into a checksum (the same encoding as the save record body).
template<class T> uint64_t puzzleHash(const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);Hasher h;h.str(bytes);return h.value();}
}
