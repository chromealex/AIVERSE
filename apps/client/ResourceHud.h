#pragma once

#include "engine/game/GameData.h"
#include "engine/game/GameState.h"
#include "engine/i18n/Localization.h"

namespace aiverse::client {

inline std::string resourceHudLabel(const game::ResourceDef& resource,
                                    const game::PlayerEconomy& economy, size_t slot) {
    const bool capacity = resource.kind == game::ResourceKind::Capacity;
    const auto value = (capacity ? economy.capUsed[slot] : economy.stock[slot]).toInt();
    if (resource.hudLabelFormat.empty()) {
        std::string label = resource.name + ": " + std::to_string(value);
        if (capacity) label += "/" + std::to_string(economy.capProvided[slot].toInt());
        return label;
    }
    const auto limit = (capacity ? economy.capProvided[slot] : economy.cap[slot]).toInt();
    return i18n::format(resource.hudLabelFormat, i18n::Args().str("name", resource.name)
        .num("value", value).num("cap", limit));
}

} // namespace aiverse::client
