#pragma once
#include "EvolveEcs.h"
#include <array>
#include <string_view>
namespace aiverse::evolve {
struct RegionPreset {
    std::string_view id;
    I durability,fertility,succession,pollution;
};
inline constexpr std::array<RegionPreset,6> regionPresets{{
    {"healthy",1000000000,900000000,800000000,0},
    {"industrial",700000000,200000000,200000000,800000000},
    {"recovering",800000000,350000000,100000000,500000000},
    {"desert",850000000,100000000,100000000,0},
    {"young_forest",900000000,700000000,200000000,0},
    {"old_growth",1000000000,950000000,950000000,0}
}};
inline bool applyRegionPreset(World& world,Entity entity,std::string_view id){
    if(!world.alive(entity)||!world.has<Region>(entity))return false;
    for(const auto& preset:regionPresets){
        if(preset.id!=id)continue;
        auto next=world.get<Region>(entity);
        next.durability=preset.durability;next.fertility=preset.fertility;
        next.succession=preset.succession;next.pollution=preset.pollution;
        if(!valid(next))return false;
        world.get<Region>(entity)=next;
        return true;
    }
    return false;
}
}
