#pragma once
#include <aiverselib/strategy/Persistence.h>
namespace strategy_demo {
using namespace aiverse::lib::strategy;
inline CardRules cardRules(){return {{1,{1,1,false,false,{{CardEffectKind::Damage,4,true}}}},{2,{2,1,true,false,{{CardEffectKind::Guard,2,false}}}}};}
inline PushBoard board(){return {5,3,std::vector<uint8_t>(15),{8}};}
inline FactoryRules factoryRules(){return {{{1,{{0,1}},{{1,1}}}},{{1,1,2,1,1}}};}
inline ProductionLaw law(uint32_t rate){return {{{1,1},{0,1}},{0,rate}};}
inline ZoneGraph zones(){return {{{0,true,false},{1,false,false},{0,false,true}},{{0,1,0},{1,0,0},{0,2,1}}};}
inline RhythmChart rhythm(){return {1,25000,80000,{{0,1000000,2000000}}};}
inline RunConfig run(){return {{1,2,1},{3,5,3},{{1,3,1},{2,1,3}},{{10,1},{20,1},{30,1}},2};}
inline PuzzleProgram program(){return {{{PuzzleOp::Read,0},{PuzzleOp::Literal,1,0,2},{PuzzleOp::Multiply,0,1},{PuzzleOp::Write,0},{PuzzleOp::Yield},{PuzzleOp::Jump,0,0,0}},16};}
}
