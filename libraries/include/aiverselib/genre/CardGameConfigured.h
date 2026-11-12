#pragma once
// Adapter from the generated optional `cardGame` settings section to CardRules defaults; card JSON `rules` then
// overrides individual variants (readCardLibrary(json, cardRulesFromSettings(settings))).
#include "../gameplay/Configuration.h"
#include "CardGame.h"
namespace aiverse::lib::genre {
inline CardRules cardRulesFromSettings(const gameplay::GameplaySettings& settings,CardRules base=CardRules{}){
    const auto& p=settings.cardGame;
    base.startingHand=int32_t(p.startingHand);base.maxHand=int32_t(p.maxHand);base.startingHealth=int32_t(p.startingHealth);base.turnLimit=int32_t(p.turnLimit);
    return base;
}
}
