#pragma once
#include "engine/game/Components.h"
#include "engine/sim/PresentationEventQueue.h"
#include "engine/sim/Simulation.h"
#include <memory>
#include <unordered_set>

namespace aiverse::client {
inline std::shared_ptr<PresentationEventQueue<SimEvent>> collectNoticeEvents(Simulation& sim, bool confirmedOnly,
    std::unordered_set<uint16_t> extraEvents = {}) {
    using namespace game;
    auto events = std::make_shared<PresentationEventQueue<SimEvent>>(confirmedOnly);
    std::weak_ptr<PresentationEventQueue<SimEvent>> weak = events;
    sim.addSystem("client.notices.collect", [weak, extraEvents = std::move(extraEvents)](Simulation& s) {
        if (auto queue = weak.lock()) {
            for (const auto& ev : s.events()) {
                switch (ev.type) {
                case kEvChat: case kEvMessage: case kEvNotEnough: case kEvBuildDone: case kEvReadyToPlace:
                case kEvResearched: case kEvLevelUp: case kEvLowPower:
                case kEvUpgraded: case kEvRegionClaimed: case kEvPlayerDefeated:
                    queue->push(s.tick(), ev); break;
                default: if (extraEvents.count(ev.type)) queue->push(s.tick(), ev); break;
                }
            }
        }
    });
    if (confirmedOnly) sim.addStateRestoredHook([weak](Simulation& s) {
        if (auto queue = weak.lock()) queue->rewind(s.tick());
    });
    return events;
}
} // namespace aiverse::client
