#pragma once
#include <aiverselib/strategy/Persistence.h>
namespace strategy_demo {
using namespace aiverse::lib::strategy;
inline CardRules cardRules(){return {{1,{1,1,false,false,{{CardEffectKind::Damage,4,true}}}},{2,{2,1,true,false,{{CardEffectKind::Guard,2,false}}}}};}
inline PushBoard board(){return {5,3,std::vector<uint8_t>(15),{8}};}
inline FactoryRules factoryRules(){return {{{1,{{0,1}},{{1,1}}}},{{1,1,2,1,1}}};}
inline ProductionLaw law(uint32_t rate){return {{{1,1},{0,1}},{0,rate}};}
}
