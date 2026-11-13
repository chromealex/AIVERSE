#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::lib::app {
// Existing values are stable for consumers persisting the original four recipes.
enum class Genre {
    ClassicRts, Colony, TowerDefense, LaneBattler, Fps, ActionRpg,
    Survivors, Survival, CityBuilder, TurnBased, Moba, Platformer,
    Racing, Cards, Puzzle, Idle
};
inline constexpr std::array allGenres{
    Genre::ClassicRts, Genre::TowerDefense, Genre::LaneBattler, Genre::Fps,
    Genre::ActionRpg, Genre::Survivors, Genre::Survival, Genre::Colony,
    Genre::CityBuilder, Genre::TurnBased, Genre::Moba, Genre::Platformer,
    Genre::Racing, Genre::Cards, Genre::Puzzle, Genre::Idle
};
struct GenreKit {
    std::string id;
    std::vector<std::string> packages, acceptance;
    // Family is a user-facing classification, not a simulation module name.
    std::string family;
    // A recipe describes required work; it never certifies a playable integration.
    std::vector<std::string> integration;
};
inline GenreKit genreKit(Genre genre) {
    switch (genre) {
        case Genre::ClassicRts: return {"classic-rts", {"input","construction","squads","ui-binding","screens","app-lifecycle"},
            {"gather-build-train","attack-win","save-restore"}, "rts", {"rts-command-host","rts-hud"}};
        case Genre::TowerDefense: return {"tower-defense", {"construction","abilities","objectives","feedback","screens"},
            {"place-tower","reject-blocked","waves-complete"}, "rts", {"wave-routes","tower-upgrades","lives-hud"}};
        case Genre::LaneBattler: return {"lane-battler", {"abilities","squads","objectives","feedback","screens"},
            {"spawn-lane","reach-base","match-finish"}, "rts", {"lane-spawn-rules","base-hud"}};
        case Genre::Fps: return {"fps", {"input","camera","feedback","network-session","app-lifecycle"},
            {"move-jump-collide","shoot-reload","authoritative-hit","save-restore"}, "fps", {"fps-client","authority-transport"}};
        case Genre::ActionRpg: return {"action-rpg", {"input","camera","abilities","inventory","loot-tables","progression-curves","objectives","screens"},
            {"move-cast-kill","loot-equip","level-learn","quest-complete","save-restore"}, "rpg", {"hero-controls","inventory-hud","quest-hud"}};
        case Genre::Survivors: return {"survivors", {"input","camera","abilities","progression-curves","objectives","performance-budgets"},
            {"move-auto-attack","wave-escalation","choose-upgrade","boss-finish","save-restore"}, "survivors", {"direct-controls","upgrade-choice","horde-profile"}};
        case Genre::Survival: return {"survival", {"input","inventory","construction","logistics","objectives","screens"},
            {"gather-craft-build","hunger-consume","shelter-survive","save-restore"}, "survival", {"interaction-controls","crafting-hud","world-persistence"}};
        case Genre::Colony: return {"colony", {"tasks","logistics","inventory","construction","tutorial","screens"},
            {"assign-job","craft-consume","save-restore"}, "colony", {"worker-needs","job-priorities","colony-hud"}};
        case Genre::CityBuilder: return {"city-builder", {"construction","graphs","logistics","tasks","objectives","screens"},
            {"connect-road","supply-homes","tax-service-cycle","disconnect-shortage","save-restore"}, "city-builder", {"road-service-network","population-rules","city-hud"}};
        case Genre::TurnBased: return {"turn-based", {"input","navigation","abilities","objectives","screens"},
            {"spend-actions","reject-wrong-turn","end-turn","battle-finish","save-restore"}, "turn-based", {"turn-command-host","action-budget-hud","turn-ai"}};
        case Genre::Moba: return {"moba", {"input","camera","abilities","inventory","progression-curves","network-session","screens"},
            {"hero-cast","lane-wave","buy-item","respawn","destroy-base","save-restore"}, "moba", {"hero-controls","shop-hud","team-match-rules"}};
        case Genre::Platformer: return {"platformer", {"motion-games","input","camera","feedback","objectives","screens"},
            {"run-jump-land","reject-air-jump","ceiling-wall-collision","checkpoint-respawn","reach-exit","save-restore"}, "platformer", {"platform-motor","side-camera","hazards-checkpoints"}};
        case Genre::Racing: return {"racing", {"motion-games","input","camera","spatial","objectives","feedback","screens"},
            {"accelerate-brake-steer","wall-collision","ordered-checkpoints","reject-shortcut","finish-laps","save-restore"}, "racing", {"vehicle-motor","race-rules","race-hud"}};
        case Genre::Cards: return {"cards", {"board-games","rules","progression","objectives","screens","ui-binding"},
            {"shuffle-draw","pay-play-discard","reject-invalid-target","end-turn-reshuffle","duel-finish","save-restore"}, "cards", {"card-zones","turn-resolution","hand-hud"}};
        case Genre::Puzzle: return {"puzzle", {"board-games","grid","rules","objectives","screens","feedback"},
            {"swap-match","reject-invalid-swap","collapse-refill","moves-limit","goal-complete","save-restore"}, "puzzle", {"board-rules","board-input","board-render"}};
        case Genre::Idle: return {"idle", {"logistics","progression-curves","objectives","screens","app-lifecycle"},
            {"produce-resource","pay-upgrade","reject-unaffordable","reach-milestone","save-restore"}, "idle", {"production-rules","upgrade-hud","persistent-profile"}};
    }
    return {};
}
inline std::optional<Genre> parseGenre(std::string_view id) {
    if (id == "rts") return Genre::ClassicRts;
    if (id == "rpg") return Genre::ActionRpg;
    for (auto genre : allGenres) if (genreKit(genre).id == id) return genre;
    return {};
}
inline bool genreInFamily(std::string_view id, std::string_view family) {
    auto genre = parseGenre(id);
    return genre && genreKit(*genre).family == family;
}
}
