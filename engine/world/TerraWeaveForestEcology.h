// TerraWeave Stage65: budgeted deterministic tree/shrub ecology.
// Uses existing RootAnchor/PlantEnvironment and Arbor forest presentation.
// Does not allocate an ECS entity for cosmetic trees or for grass blades.
// NEREID owns water; the host supplies light/moisture/nutrient snapshots.
// Dead != fallen != harvested. Recruitment creates NEW stable plant IDs.
#pragma once
#include "engine/world/TerraWeaveRootEvents.h"
#include "engine/core/Archive.h"
#include <algorithm>
#include <cstddef>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ForestEcologySpecies {
    uint32_t speciesId=0;
    uint64_t matureTicks=120000;
    uint64_t lifespanTicks=2400000;
    uint64_t seedIntervalTicks=24000;
    uint64_t shadeDeathTicks=240000;
    uint64_t floodDeathTicks=80000; // continuous flooded exposure
    uint64_t droughtDeathTicks=240000;
    uint32_t shadeTolerancePermille=240;
    uint32_t floodTolerancePermille=450; // 0..1000 tolerated immersion
    uint32_t minSoilWaterPermille=150;
    uint32_t crownRadiusFactorPermille=3000; // multiplier of root radius
    uint32_t seedSpreadCm=700;
};

inline void validateForestEcologySpecies(const ForestEcologySpecies& s) {
    if(!s.speciesId || !s.matureTicks || !s.lifespanTicks ||
       s.matureTicks>=s.lifespanTicks ||
       s.lifespanTicks>1000000000000ull ||
       !s.seedIntervalTicks || s.seedIntervalTicks>1000000000000ull ||
       !s.shadeDeathTicks || s.shadeDeathTicks>1000000000000ull ||
       !s.floodDeathTicks || s.floodDeathTicks>1000000000000ull ||
       !s.droughtDeathTicks || s.droughtDeathTicks>1000000000000ull ||
       s.shadeTolerancePermille>1000 ||
       s.floodTolerancePermille>1000 ||
       s.minSoilWaterPermille>1000 ||
       s.crownRadiusFactorPermille<100 || s.crownRadiusFactorPermille>10000 ||
       s.seedSpreadCm<50 || s.seedSpreadCm>20000)
        throw std::invalid_argument("TerraWeave invalid forest ecology species");
}

struct ForestEcologySettings {
    uint32_t intervalTicks=32;      // process bounded slice every N sim ticks
    uint32_t maxPlantsPerStep=256;  // <=1024 expensive shade/light checks
    uint32_t maxTrackedPlants=65536;// only interactable rooted subset
    uint32_t maxRecruitsPerStep=8;   // <=64 authoritative saplings/tick slice
    uint32_t minimumSoilWaterPermille=250;
    uint32_t minimumSoilNutrientsPermille=250;
};

inline void validateForestEcologySettings(const ForestEcologySettings& s) {
    if(!s.intervalTicks || s.intervalTicks>100000 ||
       !s.maxPlantsPerStep || s.maxPlantsPerStep>1024 ||
       !s.maxTrackedPlants || s.maxTrackedPlants>262144 ||
       !s.maxRecruitsPerStep || s.maxRecruitsPerStep>64 ||
       s.minimumSoilWaterPermille>1000 ||
       s.minimumSoilNutrientsPermille>1000)
        throw std::invalid_argument("TerraWeave invalid forest ecology settings");
}

struct ForestEcologyPlantState {
    AnchoredPlantId id{};
    uint64_t lastEvaluatedTick=0;
    uint64_t shadeStressTicks=0;
    uint64_t floodStressTicks=0,droughtStressTicks=0;
    uint64_t lastSeedEpoch=0;
    bool dead=false;
    bool recruited=false; // new procedural-age sapling, not an old scatter tree
};

struct ForestEcologyResource {
    uint32_t version=1;
    uint64_t initialTick=0;
    uint64_t lastStepTick=0;
    uint64_t cursor=0;        // next rooted plant in stable species/key order
    uint64_t revision=0;
    bool initialized=false;
    std::vector<ForestEcologyPlantState> plants; // sorted by id
};

inline void validateForestEcologyResource(const ForestEcologyResource& state) {
    if(state.version!=1 || state.plants.size()>262144u ||
       (state.initialized && state.lastStepTick<state.initialTick))
        throw std::invalid_argument("TerraWeave invalid forest ecology resource");
    AnchoredPlantId last{};
    bool started=false;
    for(const auto& item:state.plants) {
        if(!item.id.key || !item.id.speciesId ||
           (started && !(last<item.id)) ||
           (state.initialized && item.lastEvaluatedTick>state.lastStepTick))
            throw std::invalid_argument("TerraWeave malformed forest ecology record");
        started=true;last=item.id;
    }
}
inline void io(Archive& a,ForestEcologyPlantState& p) {
    a(p.id.speciesId,p.id.key,p.lastEvaluatedTick,
      p.shadeStressTicks,p.floodStressTicks,p.droughtStressTicks,
      p.lastSeedEpoch,p.dead,p.recruited);
}
inline void io(Archive& a,ForestEcologyResource& state) {
    a(state.version,state.initialTick,state.lastStepTick,
      state.cursor,state.revision,state.initialized);
    uint32_t count=a.count(state.plants.size(),54);
    if(!a.ok() || count>262144u){a.fail();return;}
    if(!a.writing())state.plants.assign(count,{});
    for(auto& p:state.plants)if(a.ok())a(p);
    if(!a.writing() && a.ok()) {
        try{validateForestEcologyResource(state);}
        catch(const std::exception&){a.fail();}
    }
}

// A canopy bucket is 32m; each tree/shrub contributes to only the buckets
// intersecting a capped circular crown (radius <=20m). Expensive reads
// touch nearby indices rather than scanning millions of forest instances.
class ForestEcologyCanopyIndex {
public:
    explicit ForestEcologyCanopyIndex(
            const std::vector<AnchoredPlant>& plants,
            const std::map<uint32_t,ForestEcologySpecies>& traits,
            const std::map<AnchoredPlantId,ForestEcologyPlantState>& states) {
        if(plants.size()>262144u)
            throw std::length_error("TerraWeave canopy index population limit");
        entries_.reserve(plants.size());
        for(const auto& p:plants) {
            if(p.fallen)continue;
            const AnchoredPlantId id{p.anchor.speciesId,p.anchor.plantKey};
            const auto trait=traits.find(id.speciesId);
            if(trait==traits.end())continue;
            if(const auto state=states.find(id);
               state!=states.end() && state->second.dead)continue;
            const uint32_t maturity=plantAppearance(p.environment).sizePermille;
            const int64_t radius=std::clamp<int64_t>(
                int64_t(p.anchor.roots.radiusCm)*
                trait->second.crownRadiusFactorPermille*
                maturity/1000000ll,60,2000);
            if(radius<=0)continue;
            const size_t index=entries_.size();
            const int64_t estimatedHeight=std::clamp<int64_t>(
                int64_t(p.anchor.roots.radiusCm)*
                (p.anchor.roots.habit==PlantHabit::Tree?8ll:3ll)*
                maturity/1000ll,
                p.anchor.roots.habit==PlantHabit::Tree?150ll:40ll,
                30000ll);
            const int64_t canopyTop=
                int64_t(p.anchor.plantedHeightCm)+estimatedHeight;
            entries_.push_back({id,p.anchor.xCm,p.anchor.zCm,
                                radius,canopyTop,
                                p.anchor.roots.habit});
            const int64_t x0=detail::floorDiv(p.anchor.xCm-radius,3200);
            const int64_t x1=detail::floorDiv(p.anchor.xCm+radius,3200);
            const int64_t z0=detail::floorDiv(p.anchor.zCm-radius,3200);
            const int64_t z1=detail::floorDiv(p.anchor.zCm+radius,3200);
            for(int64_t z=z0;z<=z1;++z)
                for(int64_t x=x0;x<=x1;++x) {
                    auto& bucket=buckets_[{x,z}];
                    if(bucket.size()>=2048u)
                        throw std::length_error("TerraWeave overcrowded canopy bucket");
                    bucket.push_back(index);
                }
        }
    }
    uint32_t lightAt(const AnchoredPlant& subject,
                     uint32_t baseSunPermille)const{
        if(baseSunPermille>1000)
            throw std::invalid_argument("TerraWeave canopy sun range");
        const auto bin=std::pair{
            detail::floorDiv(subject.anchor.xCm,3200),
            detail::floorDiv(subject.anchor.zCm,3200)};
        const auto it=buckets_.find(bin);
        if(it==buckets_.end())return baseSunPermille;
        uint32_t blocked=0;
        const AnchoredPlantId subjectId{
            subject.anchor.speciesId,subject.anchor.plantKey};
        for(const size_t index:it->second) {
            const Entry& crown=entries_[index];
            if(crown.id==subjectId)continue;
            // Canopies of older/larger neighbours suppress understory light.
            // For an equally tall tree the overlap is weaker than for a
            // tall neighbour above a seedling. No float/trig or frame clocks.
            const int64_t dx=crown.x-subject.anchor.xCm;
            const int64_t dz=crown.z-subject.anchor.zCm;
            const uint64_t r2=uint64_t(crown.radius)*crown.radius;
            const uint64_t dist2=uint64_t(dx*dx+dz*dz);
            if(dist2>=r2)continue;
            const uint32_t closeness=uint32_t(
                (r2-dist2)*1000u/r2);
            const uint32_t canopyStrength=
                crown.habit==PlantHabit::Tree?650u:300u;
            const int64_t subjectTop=
                int64_t(subject.anchor.plantedHeightCm)+
                int64_t(subject.anchor.roots.radiusCm)*
                (subject.anchor.roots.habit==PlantHabit::Tree?6ll:2ll);
            const uint32_t tall=crown.canopyTopCm>=subjectTop?
                1000u:300u;
            const uint32_t attenuation=uint32_t(
                uint64_t(closeness)*canopyStrength*tall/1000000ull);
            blocked=std::min<uint32_t>(900,blocked+attenuation);
        }
        return uint32_t(uint64_t(baseSunPermille)*(1000-blocked)/1000);
    }
    bool occupied(int64_t x,int64_t z,uint32_t spacingCm=120)const {
        const auto bin=std::pair{
            detail::floorDiv(x,3200),detail::floorDiv(z,3200)};
        for(int64_t bz=bin.second-1;bz<=bin.second+1;++bz)
            for(int64_t bx=bin.first-1;bx<=bin.first+1;++bx) {
                const auto it=buckets_.find({bx,bz});
                if(it==buckets_.end())continue;
                for(const size_t idx:it->second){
                    const auto& crown=entries_[idx];
                    const int64_t dx=crown.x-x,dz=crown.z-z;
                    const int64_t minimum=int64_t(spacingCm);
                    if(dx*dx+dz*dz<minimum*minimum)return true;
                }
            }
        return false;
    }
private:
    struct Entry {
        AnchoredPlantId id{};
        int64_t x=0,z=0,radius=0;
        int64_t canopyTopCm=0;
        PlantHabit habit=PlantHabit::Tree;
    };
    std::vector<Entry> entries_;
    std::map<std::pair<int64_t,int64_t>,std::vector<size_t>> buckets_;
};

struct ForestEcologyClimate {
    uint32_t sunPermille=1000;   // PHYSIS / canopy base illumination
    uint32_t floodedPermille=0;  // submerged root zone from NEREID
    uint32_t waterPermille=1000; // NEREID soil water or existing state
    uint32_t nutrientsPermille=1000;
};
using ForestEcologyClimateSampler=
    std::function<ForestEcologyClimate(const AnchoredPlant&)>;
using ForestEcologySoilSampler=SoilHeightSampler;

struct ForestEcologyDeath {
    AnchoredPlantId id{};
    uint64_t tick=0;
};
struct ForestEcologyBirth {
    AnchoredPlantId id{},parent{};
    int64_t xCm=0,zCm=0;
    uint64_t tick=0;
};
struct ForestEcologyResult {
    size_t processed=0;
    std::vector<AnchoredPlantId> changed;
    std::vector<ForestEcologyDeath> died;
    std::vector<ForestEcologyBirth> born;
    bool populationChanged=false;
    uint64_t revision=0;
};

// Plants must be a stable, rooted, interactable subset. O(N) index creation
// occurs once PER ECOLOGY PERIOD (not each rendered frame); the costly shade,
// health, seed and death evaluation is capped at maxPlantsPerStep.
// The entire operation stages new resource/roots before a single commit.
// World-level harvest/Box3D body removal is NOT performed here.
inline ForestEcologyResult advanceForestEcology(
        ForestEcologyResource& forest,
        std::vector<AnchoredPlant>& rooted,
        uint64_t completedTick,
        const std::vector<ForestEcologySpecies>& species,
        const ForestEcologySoilSampler& soilHeight,
        ForestEcologyClimateSampler climate={},
        ForestEcologySettings cfg={}) {
    validateForestEcologySettings(cfg);
    validateForestEcologyResource(forest);
    if(!soilHeight || rooted.size()>cfg.maxTrackedPlants ||
       forest.plants.size()>cfg.maxTrackedPlants ||
       (forest.initialized && completedTick<forest.lastStepTick))
        throw std::invalid_argument("TerraWeave forest ecology budget/tick");
    std::map<uint32_t,ForestEcologySpecies> traits;
    for(const auto& entry:species) {
        validateForestEcologySpecies(entry);
        if(!traits.emplace(entry.speciesId,entry).second)
            throw std::invalid_argument("TerraWeave duplicate ecology species");
    }
    if(traits.size()>64)
        throw std::length_error("TerraWeave ecology species budget");

    ForestEcologyResource next=forest;
    if(!next.initialized){
        next.initialized=true;
        next.initialTick=next.lastStepTick=completedTick;
        forest=std::move(next);
        return {};
    }
    if(completedTick-next.lastStepTick<cfg.intervalTicks)return {};
    next.lastStepTick=completedTick;

    std::vector<AnchoredPlant> plants=rooted;
    std::sort(plants.begin(),plants.end(),
        [](const auto& a,const auto& b){
            return AnchoredPlantId{a.anchor.speciesId,a.anchor.plantKey}<
                   AnchoredPlantId{b.anchor.speciesId,b.anchor.plantKey};
        });
    for(size_t i=0;i<plants.size();++i){
        const auto& p=plants[i];
        if(!p.anchor.speciesId || !p.anchor.plantKey ||
           p.anchor.roots.habit==PlantHabit::Grass ||
           (i && p.anchor.speciesId==plants[i-1].anchor.speciesId &&
            p.anchor.plantKey==plants[i-1].anchor.plantKey))
            throw std::invalid_argument("TerraWeave invalid rooted ecology population");
        (void)plantAppearance(p.environment);
    }
    std::map<AnchoredPlantId,ForestEcologyPlantState> histories;
    for(const auto& record:next.plants)
        histories.emplace(record.id,record);
    const ForestEcologyCanopyIndex canopy(plants,traits,histories);
    ForestEcologyResult result;
    if(plants.empty()){
        next.cursor=0;next.plants.clear();
        forest=std::move(next);return result;
    }
    std::set<AnchoredPlantId> occupiedIds;
    for(const auto& p:plants)
        occupiedIds.insert({p.anchor.speciesId,p.anchor.plantKey});
    const size_t start=size_t(next.cursor%plants.size());
    const size_t count=std::min<size_t>(cfg.maxPlantsPerStep,plants.size());
    result.processed=count;
    std::vector<AnchoredPlant> seedlings;
    seedlings.reserve(cfg.maxRecruitsPerStep);
    const uint64_t ageLimit=1000000000000ull;
    for(size_t offset=0;offset<count;++offset){
        AnchoredPlant& plant=plants[(start+offset)%plants.size()];
        const AnchoredPlantId id{
            plant.anchor.speciesId,plant.anchor.plantKey};
        const auto tr=traits.find(id.speciesId);
        if(tr==traits.end() || plant.fallen)continue;
        ForestEcologyPlantState& history=histories[id];
        if(!history.id.key)history={id,next.initialTick,0,0,0,0,false,false};
        if(history.dead)continue;
        const uint64_t dt=completedTick-history.lastEvaluatedTick;
        history.lastEvaluatedTick=completedTick;
        const ForestEcologySpecies& policy=tr->second;
        ForestEcologyClimate environment=climate?
            climate(plant):ForestEcologyClimate{
                1000u,0u,plant.environment.waterPermille,
                plant.environment.nutrientsPermille};
        if(environment.sunPermille>1000 ||
           environment.floodedPermille>1000 ||
           environment.waterPermille>1000 ||
           environment.nutrientsPermille>1000)
            throw std::invalid_argument("TerraWeave forest climate range");
        const uint32_t availableLight=canopy.lightAt(
            plant,environment.sunPermille);
        const PlantEnvironment before=plant.environment;
        auto& env=plant.environment;
        if(env.matureTicks==0){
            // Legacy authoring defined matureTicks==0 as already mature.
            // Opting such a tree into ecology must not unexpectedly shrink
            // it to a tiny seedling on its first processed tick. Explicit
            // seedling records always carry a nonzero matureTicks value.
            env.ageTicks=std::max(env.ageTicks,policy.matureTicks);
            env.matureTicks=policy.matureTicks;
        }
        if(env.lifespanTicks==0)env.lifespanTicks=policy.lifespanTicks;
        env.ageTicks=std::min<uint64_t>(
            ageLimit,env.ageTicks+std::min<uint64_t>(
                dt,ageLimit-env.ageTicks));
        env.lightPermille=availableLight;
        env.waterPermille=environment.waterPermille;
        env.nutrientsPermille=environment.nutrientsPermille;
        (void)plantAppearance(env);
        if(availableLight<policy.shadeTolerancePermille){
            history.shadeStressTicks=std::min<uint64_t>(
                ageLimit,history.shadeStressTicks+
                std::min<uint64_t>(dt,ageLimit-history.shadeStressTicks));
        }else{
            history.shadeStressTicks=history.shadeStressTicks>dt?
                history.shadeStressTicks-dt:0;
        }
        const auto stressStep=[dt,ageLimit](uint64_t previous,
                                             bool stressed){
            if(stressed)
                return std::min<uint64_t>(ageLimit,previous+
                    std::min<uint64_t>(dt,ageLimit-previous));
            return previous>dt?previous-dt:0ull;
        };
        history.floodStressTicks=stressStep(history.floodStressTicks,
            environment.floodedPermille>policy.floodTolerancePermille);
        history.droughtStressTicks=stressStep(history.droughtStressTicks,
            environment.waterPermille<policy.minSoilWaterPermille);
        if(env.ageTicks>=policy.lifespanTicks ||
           history.shadeStressTicks>=policy.shadeDeathTicks ||
           history.floodStressTicks>=policy.floodDeathTicks ||
           history.droughtStressTicks>=policy.droughtDeathTicks) {
            history.dead=true;
            result.died.push_back({id,completedTick});
        }else if(env.ageTicks>=policy.matureTicks &&
                 availableLight>=policy.shadeTolerancePermille &&
                 env.waterPermille>=cfg.minimumSoilWaterPermille &&
                 env.nutrientsPermille>=cfg.minimumSoilNutrientsPermille &&
                 seedlings.size()<cfg.maxRecruitsPerStep &&
                 plants.size()+seedlings.size()<cfg.maxTrackedPlants) {
            const uint64_t epoch=completedTick/policy.seedIntervalTicks;
            if(epoch && epoch>history.lastSeedEpoch){
                history.lastSeedEpoch=epoch; // one deterministic attempt/epoch
                const uint64_t hash=detail::mix64(
                    id.key ^ (uint64_t(id.speciesId)<<32) ^
                    (epoch*0x9e3779b97f4a7c15ull));
                const int64_t spread=policy.seedSpreadCm;
                const int64_t dx=int64_t(hash%uint64_t(2*spread+1))-spread;
                const int64_t dz=int64_t((hash>>31)%uint64_t(2*spread+1))-spread;
                const int64_t px=plant.anchor.xCm+dx;
                const int64_t pz=plant.anchor.zCm+dz;
                constexpr int64_t margin=20000;
                if(px>-kWorldLimitCm+margin &&
                   px<kWorldLimitCm-margin &&
                   pz>-kWorldLimitCm+margin &&
                   pz<kWorldLimitCm-margin &&
                   !canopy.occupied(px,pz) &&
                   !std::any_of(seedlings.begin(),seedlings.end(),
                       [&](const AnchoredPlant& s){
                           const int64_t x=s.anchor.xCm-px;
                           const int64_t z=s.anchor.zCm-pz;
                           return x*x+z*z<120ll*120ll;
                       })){
                    const auto height=soilHeight(px,pz);
                    const uint64_t newKey=detail::mix64(
                        hash ^ 0x736565646c696e67ull);
                    const AnchoredPlantId child{id.speciesId,
                                               newKey?newKey:1ull};
                    if(height && !occupiedIds.contains(child)){
                        RootAnchor anchor=plant.anchor;
                        anchor.plantKey=child.key;
                        anchor.xCm=px;anchor.zCm=pz;
                        anchor.plantedHeightCm=*height;
                        AnchoredPlant newborn;
                        newborn.anchor=anchor;
                        newborn.environment={0,policy.matureTicks,
                            policy.lifespanTicks,availableLight,
                            env.waterPermille,env.nutrientsPermille};
                        seedlings.push_back(newborn);
                        occupiedIds.insert(child);
                        result.born.push_back({child,id,px,pz,completedTick});
                    }
                }
            }
        }
        if(before!=plant.environment || history.dead)
            result.changed.push_back(id);
    }
    next.cursor=(start+count)%plants.size();
    result.populationChanged=!seedlings.empty();
    for(auto& child:seedlings)
        plants.push_back(std::move(child));
    if(!seedlings.empty())
        std::sort(plants.begin(),plants.end(),
            [](const auto& a,const auto& b){
                return AnchoredPlantId{a.anchor.speciesId,a.anchor.plantKey}<
                       AnchoredPlantId{b.anchor.speciesId,b.anchor.plantKey};
            });
    next.plants.clear();
    next.plants.reserve(histories.size()+seedlings.size());
    for(const auto& [id,record]:histories) {
        if(occupiedIds.contains(id))next.plants.push_back(record);
    }
    for(const auto& child:seedlings)
        next.plants.push_back({{child.anchor.speciesId,
                               child.anchor.plantKey},completedTick,0,0,0,0,false,true});
    std::sort(next.plants.begin(),next.plants.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    if(next.revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave forest ecology revision overflow");
    ++next.revision;
    result.revision=next.revision;
    validateForestEcologyResource(next);
    forest=std::move(next);
    rooted=std::move(plants);
    return result;
}

} // namespace aiverse::terraweave
