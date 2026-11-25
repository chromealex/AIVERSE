#pragma once
// Multi-chart gameplay authority. CPU/ECS only; no editor, GPU or physics
// dependencies. All density edits use the existing detached stamp kernel.
#include "engine/world/TerraWeaveCavernsPlanetAuthority.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include <charconv>
#include <map>

namespace aiverse::terraweave::caverns {
inline constexpr const char* kPlanetCaveMapKey="terraweavePlanetCaverns";
inline constexpr size_t kMaxPlanetCaveRegions=32;
struct CavePlanetAuthorityEntry {
    uint64_t id=0;
    CavePlanetRegionResource region;
    CaveAuthorityResource authority;
};
struct CavePlanetAuthoritySet {
    uint32_t version=1;
    std::vector<CavePlanetAuthorityEntry> regions; // ascending stable ID
};
inline uint64_t parsePlanetCaveRegionId(const Json& value){
    if(!value.isString())throw std::invalid_argument("Caverns region ID must be decimal text");
    const auto s=value.str();uint64_t n=0;
    const auto p=std::from_chars(s.data(),s.data()+s.size(),n);
    if(p.ec!=std::errc{}||p.ptr!=s.data()+s.size()||!n||
       n>uint64_t(INT64_MAX)||std::to_string(n)!=s)
        throw std::invalid_argument("Caverns invalid region ID");
    return n;
}
inline CaveAuthorityConfig planetCaveConfig(uint64_t id,uint64_t generatorVersion){
    if(!id)throw std::invalid_argument("Caverns region ID");
    CaveAuthorityConfig cfg;cfg.recipeId="planet-cave/"+std::to_string(id);
    cfg.generatorVersion=generatorVersion;return cfg;
}
// Conservative circumscribed-box sphere test in deterministic body-local
// fixed coordinates. A small tolerance covers chart frame rounding.
inline bool cavePlanetFramesMayOverlap(const CavePlanetFrame& a,const CavePlanetFrame& b){
    if(a.anchor().bodyId!=b.anchor().bodyId)return false;
    const auto distance=a.originBody()-b.originBody();
    const auto radius=Fixed::fromRatio(int64_t(a.anchor().halfExtentCm)+b.anchor().halfExtentCm,100)
        *Fixed::fromRatio(1733,1000)+Fixed::fromRatio(5,100);
    return distance.lengthSq()<=radius*radius;
}
inline void validatePlanetCaveSet(const CavePlanetAuthoritySet& set){
    if(set.version!=1||set.regions.size()>kMaxPlanetCaveRegions)
        throw std::invalid_argument("Caverns planetary authority set");
    uint64_t previous=0;size_t stamps=0,entrances=0;
    for(const auto& item:set.regions){
        if(item.id<=previous||item.id>uint64_t(INT64_MAX)||item.region.version!=1)
            throw std::invalid_argument("Caverns noncanonical region identity");
        previous=item.id;
        requireCaveRecipe(item.authority,planetCaveConfig(item.id,item.authority.volume.generatorVersion));
        validateCavePlanetBounds(item.region.anchor,item.authority.volume);
        stamps+=item.authority.volume.stamps.size();entrances+=item.authority.volume.entrances.size();
        if(stamps>131072||entrances>32768)
            throw std::length_error("Caverns total planetary edit budget");
    }
}
inline void validatePlanetCaveFrames(const CavePlanetAuthoritySet& set,
        const std::shared_ptr<const game::Surface>& surface){
    validatePlanetCaveSet(set);
    if(!surface||!surface->tab)throw std::invalid_argument("Caverns actual gameplay Surface required");
    std::vector<CavePlanetFrame> frames;frames.reserve(set.regions.size());
    for(const auto& item:set.regions){
        auto frame=CavePlanetFrame::make(surface,item.region.anchor);
        for(const auto& old:frames)if(cavePlanetFramesMayOverlap(old,frame))
            throw std::invalid_argument("Caverns overlapping planetary charts");
        frames.push_back(std::move(frame));
    }
}
inline CavePlanetAuthoritySet decodePlanetCaveMap(const Json& doc,
        const std::shared_ptr<const game::Surface>& surface){
    CavePlanetAuthoritySet out;
    if(doc.isNull())return out;
    if(!doc.isObject())throw std::invalid_argument("Caverns planet map format");
    const auto decode=[&](uint64_t id,const Json& j){
        if(j.getInt("version",0)!=1||!j["anchor"].isObject()||!j["volume"].isObject())
            throw std::invalid_argument("Caverns planetary region record");
        CavePlanetAuthorityEntry item;item.id=id;
        item.region.anchor=cavePlanetAnchorFromJson(j["anchor"]);
        item.authority.volume=caveSnapshotFromJson(j["volume"]);
        item.authority.recipeId=planetCaveConfig(id,item.authority.volume.generatorVersion).recipeId;
        if(!item.authority.volume.stamps.empty()){
            const auto last=item.authority.volume.stamps.back().id;
            if(last==UINT64_MAX)throw std::overflow_error("Caverns stamp allocator");
            item.authority.nextStampId=last+1;
        }
        out.regions.push_back(std::move(item));
    };
    const auto version=doc.getInt("version",0);
    if(version==1)decode(1,doc);
    else if(version==2){
        if(!doc["regions"].isArray()||doc["regions"].size()>kMaxPlanetCaveRegions||
           !doc["activeRegionId"].isString())
            throw std::invalid_argument("Caverns planetary region list");
        const auto next=parsePlanetCaveRegionId(doc["nextRegionId"]);
        const auto active=doc.getStr("activeRegionId").empty()?0:parsePlanetCaveRegionId(doc["activeRegionId"]);
        bool found=false;
        for(const auto& j:doc["regions"].items()){
            const auto id=parsePlanetCaveRegionId(j["id"]);
            if(id>=next)throw std::invalid_argument("Caverns future region ID");
            found|=id==active;decode(id,j);
        }
        if(out.regions.empty()?active!=0:!found)
            throw std::invalid_argument("Caverns selected region missing");
        std::sort(out.regions.begin(),out.regions.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    }else throw std::invalid_argument("Caverns planetary map version");
    // Selection and next editor ID are not gameplay state.
    validatePlanetCaveFrames(out,surface);return out;
}
inline void io(Archive& a,CavePlanetAuthoritySet& set){
    if(a.writing())try{validatePlanetCaveSet(set);}catch(const std::exception&){a.fail();return;}
    a(set.version);
    if(!a.ok()||set.version!=1){a.fail();return;}
    authority_detail::records(a,set.regions,uint32_t(kMaxPlanetCaveRegions),8,
        [](Archive& ar,CavePlanetAuthorityEntry& item){ar(item.id,item.region,item.authority);});
    if(a.ok()&&!a.writing())try{validatePlanetCaveSet(set);}catch(const std::exception&){a.fail();}
}
inline bool samePlanetCaveRecipe(const CavePlanetAuthorityEntry& a,const CavePlanetAuthorityEntry& b){
    const auto& x=a.authority.volume.settings;const auto& y=b.authority.volume.settings;
    return a.id==b.id&&sameCavePlanetAnchor(a.region.anchor,b.region.anchor)&&
        a.authority.recipeId==b.authority.recipeId&&
        a.authority.volume.generatorVersion==b.authority.volume.generatorVersion&&
        x.seed==y.seed&&x.cellCm==y.cellCm&&x.tunnelRadiusCm==y.tunnelRadiusCm&&
        x.chamberRadiusCm==y.chamberRadiusCm&&x.tunnelChance==y.tunnelChance&&
        x.chamberChance==y.chamberChance&&x.minYcm==y.minYcm&&x.maxYcm==y.maxYcm&&
        a.authority.volume.entrances==b.authority.volume.entrances;
}
inline void registerPlanetCaveMap(Simulation& sim,const Json& doc,
        std::shared_ptr<const game::Surface> surface){
    if(doc.isNull())return;
    auto set=decodePlanetCaveMap(doc,surface);
    if(set.regions.empty())return;
    if(sim.world().resourceConst<CaveAuthorityResource>()||
       sim.world().resourceConst<CavePlanetRegionResource>()||
       sim.world().resourceConst<CavePlanetAuthoritySet>())
        throw std::logic_error("Caverns competing gameplay authority");
    // The immutable recipe validator need not retain initial stamp arrays.
    auto recipe=set;
    for(auto& item:recipe.regions)item.authority.volume.stamps.clear();
    sim.registerPersistentResource<CavePlanetAuthoritySet>("terraweave.caverns.planet_regions",1);
    sim.addPersistentLoadValidator("terraweave.caverns.planet_regions.recipe",
        [surface,recipe=std::move(recipe)](const World& staged,std::string& error){
            try{
                const auto* loaded=staged.resourceConst<CavePlanetAuthoritySet>();
                if(!loaded||loaded->regions.size()!=recipe.regions.size())
                    throw std::invalid_argument("Caverns restored region set mismatch");
                validatePlanetCaveFrames(*loaded,surface);
                for(size_t i=0;i<recipe.regions.size();++i)
                    if(!samePlanetCaveRecipe(loaded->regions[i],recipe.regions[i]))
                        throw std::invalid_argument("Caverns restored immutable region recipe differs");
                return true;
            }catch(const std::exception& e){error=e.what();return false;}
        });
    sim.world().setResource<CavePlanetAuthoritySet>(std::move(set));
}
inline const CavePlanetAuthorityEntry* findPlanetCaveRegion(const World& world,uint64_t id){
    const auto* set=world.resourceConst<CavePlanetAuthoritySet>();if(!set)return nullptr;
    const auto it=std::lower_bound(set->regions.begin(),set->regions.end(),id,
        [](const auto& item,uint64_t value){return item.id<value;});
    return it!=set->regions.end()&&it->id==id?&*it:nullptr;
}
inline std::shared_ptr<const CaveVolumeSnapshot> freezePlanetCaveRegion(const World& world,uint64_t id){
    const auto* item=findPlanetCaveRegion(world,id);
    if(!item)throw std::out_of_range("Caverns region not loaded");
    return std::make_shared<const CaveVolumeSnapshot>(item->authority.volume);
}
inline CaveAuthorityChange applyAuthorizedPlanetCaveStampCommand(World& world,uint64_t id,
        const CaveStampCommand& command,uint32_t tick){
    const auto* old=world.resourceConst<CavePlanetAuthoritySet>();
    const auto* item=findPlanetCaveRegion(world,id);
    if(!old||!item)throw std::out_of_range("Caverns region not loaded");
    CaveAuthorityResource candidate;
    const auto change=planAuthorizedCaveStampCommand(item->authority,candidate,
        planetCaveConfig(id,item->authority.volume.generatorVersion),command,tick,&item->region.anchor);
    if(!change.changed)return change;
    auto next=*old;
    next.regions[size_t(item-old->regions.data())].authority=std::move(candidate);
    validatePlanetCaveSet(next);
    world.setResource<CavePlanetAuthoritySet>(std::move(next));return change;
}
} // namespace aiverse::terraweave::caverns
