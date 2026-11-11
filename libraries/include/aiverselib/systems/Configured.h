#pragma once
#include "../gameplay/Configuration.h"
#include "Loot.h"
#include "Perception.h"
#include "Decisions.h"
namespace aiverse::lib::systems {
inline LootConfig lootConfig(const gameplay::GameplaySettings& s){return {s.loot.guaranteeAfter};}
inline MemoryConfig memoryConfig(const gameplay::GameplaySettings& s){return {static_cast<std::size_t>(s.perception.capacity),static_cast<std::uint64_t>(s.perception.lifetimeTicks)};}
inline DecisionReport explainConfigured(std::span<const DecisionFact> facts,const gameplay::GameplaySettings& s){return explain(facts,static_cast<std::size_t>(s.decisionReport.maxFacts));}
}
