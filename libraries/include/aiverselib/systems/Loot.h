#pragma once
#include "../Progression.h"
#include <set>
#include <vector>
namespace aiverse::lib::systems {
struct LootRow {std::int64_t id=0,weight=0;bool rare=false;};
struct LootConfig {std::int64_t guaranteeAfter=0;};
struct LootState {std::int64_t misses=0;};
// guaranteeAfter=N forces a rare item on attempt N after N-1 misses. 0 disables pity.
// Excluded items have zero probability. An impossible guarantee fails without drawing or changing state.
template<class Draw>
std::optional<std::int64_t> rollLoot(std::span<const LootRow> rows,LootConfig config,LootState& state,const std::set<std::int64_t>& excluded,Draw draw){
    if(config.guaranteeAfter<0||state.misses<0)return {};
    const bool force=config.guaranteeAfter>0&&state.misses>=config.guaranteeAfter-1;
    std::set<std::int64_t> ids;std::vector<aiverse::lib::Weighted> weights;
    for(const auto& row:rows){
        if(row.id<=0||row.weight<0||!ids.insert(row.id).second)return {};
        if(!excluded.count(row.id)&&(!force||row.rare))weights.push_back({row.id,row.weight});
    }
    // Preflight state arithmetic before advancing caller RNG.
    if(!force&&state.misses==std::numeric_limits<std::int64_t>::max())return {};
    auto selected=aiverse::lib::weighted(std::span<const aiverse::lib::Weighted>(weights),draw);if(!selected)return {};
    for(const auto& row:rows)if(row.id==*selected){state.misses=row.rare?0:state.misses+1;break;}
    return selected;
}
}
