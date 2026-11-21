// TerraWeave Stage68: cost-neutral deadwood colour tiers over shared Arbor
// model instances. No new shader, per-tree mesh, atlas or GPU upload.
#pragma once
#include "engine/world/TerraWeaveForestDeadwood.h"
#include <algorithm>
#include <cstdint>

namespace aiverse::terraweave {

enum class DeadwoodVisualTier : uint8_t {
    Fresh=0, Weathered=1, Rotten=2, Crumbling=3, Gone=4
};

struct DeadwoodVisualState {
    DeadwoodVisualTier tier=DeadwoodVisualTier::Fresh;
    uint32_t remainingPermille=1000;
    uint32_t tintRPermille=1000,tintGPermille=1000,tintBPermille=1000;
};

inline DeadwoodVisualState deadwoodVisualState(
        const ForestDeadwoodResource& resource,AnchoredPlantId id) {
    const auto it=std::lower_bound(
        resource.logs.begin(),resource.logs.end(),id,
        [](const ForestDeadwoodRecord& record,AnchoredPlantId key){
            return record.id<key;
        });
    if(it==resource.logs.end() || it->id!=id)
        return {}; // legacy worlds with no opted-in deadwood
    DeadwoodVisualState out;
    out.remainingPermille=it->initialUnits?
        uint32_t(std::min<uint64_t>(1000,
            it->remainingUnits*1000/it->initialUnits)):0;
    if(!out.remainingPermille){
        out.tier=DeadwoodVisualTier::Gone;
        out.tintRPermille=out.tintGPermille=out.tintBPermille=0;
    }else if(out.remainingPermille>=750){
        out.tier=DeadwoodVisualTier::Fresh;
    }else if(out.remainingPermille>=400){
        out.tier=DeadwoodVisualTier::Weathered;
        out.tintRPermille=900;out.tintGPermille=820;
        out.tintBPermille=650;
    }else if(out.remainingPermille>=150){
        out.tier=DeadwoodVisualTier::Rotten;
        out.tintRPermille=690;out.tintGPermille=730;
        out.tintBPermille=570;
    }else{
        out.tier=DeadwoodVisualTier::Crumbling;
        out.tintRPermille=480;out.tintGPermille=490;
        out.tintBPermille=380;
    }
    return out;
}

} // namespace aiverse::terraweave
