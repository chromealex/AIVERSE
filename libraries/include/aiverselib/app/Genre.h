#pragma once
#include <string>
#include <vector>
namespace aiverse::lib::app {
enum class Genre { ClassicRts, Colony, TowerDefense, LaneBattler };
struct GenreKit { std::string id; std::vector<std::string> packages,acceptance; };
inline GenreKit genreKit(Genre genre) {
    switch(genre){
        case Genre::ClassicRts:return {"classic-rts",{"input","construction","squads","ui-binding","screens","app-lifecycle"},{"gather-build-train","attack-win","save-restore"}};
        case Genre::Colony:return {"colony",{"tasks","logistics","inventory","construction","tutorial","screens"},{"assign-job","craft-consume","save-restore"}};
        case Genre::TowerDefense:return {"tower-defense",{"construction","abilities","objectives","feedback","screens"},{"place-tower","reject-blocked","waves-complete"}};
        case Genre::LaneBattler:return {"lane-battler",{"abilities","squads","objectives","feedback","screens"},{"spawn-lane","reach-base","match-finish"}};
    }
    return {};
}
}
