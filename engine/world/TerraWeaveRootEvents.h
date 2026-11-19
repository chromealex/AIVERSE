// TerraWeave: persistent plant support events after ground excavation/erosion.
// Header-only pure CPU policy. Host ECS/physics owns the falling rigid body,
// collider/navmesh updates, resources, save/replay and optional VFX/audio.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/world/TerraWeaveVegetationSoil.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct AnchoredPlantId {
    uint32_t speciesId=0;
    uint64_t key=0;
    constexpr auto operator<=>(const AnchoredPlantId&) const = default;
};

struct AnchoredPlant {
    RootAnchor anchor{};
    RootState support{};
    PlantEnvironment environment{};
    bool fallen=false;           // persistent, not recomputed from camera
};

struct PlantSupportChange {
    AnchoredPlantId id{};
    RootState support{};
    bool fallen=false;
};

struct PlantFallEvent {
    AnchoredPlantId id{};
    PlantHabit habit=PlantHabit::Tree;
    int64_t xCm=0,zCm=0;
    int32_t plantedHeightCm=0;
    int32_t fallDirectionXPermille=0;
    int32_t fallDirectionZPermille=0;
    uint32_t supportPermille=0;
    uint32_t exposedCm=0;
    uint64_t sourceSoilRevision=0;
    uint64_t tick=0;
};

struct PlantSupportUpdate {
    std::vector<PlantSupportChange> changed;
    std::vector<PlantFallEvent> fell;
};

// Stable progression even if patches unload/reload and the ECS host saves.
// Max 1e6 *interactable* trees/shrubs; ordinary cosmetic grass is excluded.
// The host may use this directly from a native game module without WorldScene.
class RootSupportRegistry {
public:
    static constexpr size_t kMaxAnchors=1000000;

    size_t size() const {return plants_.size();}
    void exchangeSnapshotState(RootSupportRegistry& other) noexcept {
        plants_.swap(other.plants_);
    }
    std::vector<AnchoredPlant> snapshot() const {
        std::vector<AnchoredPlant> out;
        out.reserve(plants_.size());
        for(const auto& [id,plant]:plants_) {
            (void)id;
            out.push_back(plant);
        }
        return out;
    }
    void setSnapshot(const std::vector<AnchoredPlant>& records) {
        if(records.size()>kMaxAnchors)
            throw std::length_error("TerraWeave rooted plant budget");
        std::map<AnchoredPlantId,AnchoredPlant> validated;
        for(const auto& plant:records) {
            validate(plant);
            const auto id=identity(plant.anchor);
            if(!validated.emplace(id,plant).second)
                throw std::invalid_argument("TerraWeave duplicated rooted plant");
        }
        plants_.swap(validated);
    }
    void upsert(const RootAnchor& anchor,
                PlantEnvironment environment={}) {
        AnchoredPlant p;
        p.anchor=anchor;
        p.environment=environment;
        validate(p);
        const auto id=identity(anchor);
        if(!plants_.contains(id) && plants_.size()>=kMaxAnchors)
            throw std::length_error("TerraWeave rooted plant budget");
        auto it=plants_.find(id);
        if(it!=plants_.end()) {
            // Repeated scene registration must not accidentally revive a
            // fallen tree or reset the once-only fall event.
            p.fallen=it->second.fallen;
            p.support=it->second.support;
        }
        plants_.insert_or_assign(id,std::move(p));
    }
    bool erase(AnchoredPlantId id){return plants_.erase(id)!=0;}
    const AnchoredPlant* find(AnchoredPlantId id) const {
        const auto it=plants_.find(id);
        return it==plants_.end()?nullptr:&it->second;
    }
    // Apply an authoritative server/replay support transition exactly,
    // without recomputing the soil query on the rendering client.
    // Validation finishes before any existing map value is modified.
    void applyAuthoritativeSupportChanges(
            std::span<const PlantSupportChange> changes) {
        if(changes.size()>65536u)
            throw std::length_error("TerraWeave root network update budget");
        std::set<AnchoredPlantId> seen;
        for(const auto& change:changes) {
            if(!seen.insert(change.id).second)
                throw std::invalid_argument("TerraWeave duplicated root network change");
            const auto it=plants_.find(change.id);
            if(it==plants_.end() || !change.id.key ||
               !change.id.speciesId ||
               change.support.plantKey!=change.id.key ||
               change.support.speciesId!=change.id.speciesId ||
               change.support.habit!=it->second.anchor.roots.habit ||
               change.support.exposedCm>it->second.anchor.roots.depthCm ||
               change.support.supportPermille>1000u ||
               change.support.fallDirectionXPermille < -1000 ||
               change.support.fallDirectionXPermille > 1000 ||
               change.support.fallDirectionZPermille < -1000 ||
               change.support.fallDirectionZPermille > 1000)
                throw std::invalid_argument(
                    "TerraWeave invalid/missing rooted plant network state");
        }
        for(const auto& change:changes){
            auto& plant=plants_.at(change.id);
            plant.support=change.support;
            plant.fallen=change.fallen;
        }
    }

    void setEnvironment(AnchoredPlantId id,PlantEnvironment environment) {
        (void)plantAppearance(environment); // pre-validate without mutation
        const auto it=plants_.find(id);
        if(it==plants_.end())
            throw std::out_of_range("TerraWeave rooted plant identity");
        it->second.environment=environment;
    }
    void resetFall(AnchoredPlantId id) {
        // Explicit regrowth/replant decision from authoritative gameplay.
        const auto it=plants_.find(id);
        if(it==plants_.end())throw std::out_of_range("TerraWeave rooted plant identity");
        it->second.fallen=false;
        // A replanted stem must be resampled even when the current soil
        // shape is identical to its last collapsed support snapshot.
        it->second.support={};
    }

    // Soil change is a revisioned terrain edit. Only plants whose root disks
    // intersect its dirty bounds need reevaluation. No scan of individual
    // grass blades, GPU copies, physics impulses, or ECS structural writes.
    PlantSupportUpdate applyGroundChange(const SoilHeightSampler& ground,
                                         const EarthworkChange& change,
                                         uint64_t tick) {
        if(!ground)throw std::invalid_argument("TerraWeave missing ground snapshot");
        PlantSupportUpdate update;
        if(change.vertices.empty())return update;
        // Stage decisions before mutating registry state. A failing height
        // sampler or allocation cannot produce a half-updated plant set.
        struct Pending {
            AnchoredPlantId id{};
            RootState support{};
            bool fallen=false;
        };
        std::vector<Pending> pending;
        for(const auto& [id,p]:plants_) {
            const auto& anchor=p.anchor;
            const int64_t radius=anchor.roots.radiusCm;
            if(anchor.xCm+radius<change.dirty.minX ||
               anchor.xCm-radius>=change.dirty.maxX ||
               anchor.zCm+radius<change.dirty.minZ ||
               anchor.zCm-radius>=change.dirty.maxZ)
                continue;
            const auto next=evaluateRootSupport(anchor,ground);
            if(next==p.support)continue;
            // A root visibility change is a lightweight presentation
            // update, independent of whether this tree has fallen.
            bool fallen=p.fallen;
            if(next.uprooted && !fallen) {
                fallen=true;
                int32_t dx=next.fallDirectionXPermille;
                int32_t dz=next.fallDirectionZPermille;
                if(!dx && !dz) {
                    // Symmetric erosion/digging: choose stable azimuth
                    // rather than allowing frame-rate/randomness to choose.
                    static constexpr int32_t directions[8][2]={
                        {1000,0},{707,707},{0,1000},{-707,707},
                        {-1000,0},{-707,-707},{0,-1000},{707,-707}
                    };
                    const uint64_t seed=detail::mix64(
                        anchor.plantKey^(uint64_t(anchor.speciesId)<<32));
                    dx=directions[seed%8][0];
                    dz=directions[seed%8][1];
                }
                update.fell.push_back({
                    id,anchor.roots.habit,anchor.xCm,anchor.zCm,
                    anchor.plantedHeightCm,dx,dz,next.supportPermille,
                    next.exposedCm,change.revision,tick
                });
            }
            pending.push_back({id,next,fallen});
            update.changed.push_back({id,next,fallen});
        }
        for(const auto& item:pending) {
            auto& original=plants_.at(item.id);
            original.support=item.support;
            original.fallen=item.fallen;
        }
        return update;
    }

    // Evaluate initial cliff/overhang exposure once at spawn time, including
    // standing roots on naturally steep terrain. This can report an initial
    // collapse if the generated terrain offers insufficient support.
    PlantSupportUpdate initialize(const SoilHeightSampler& ground,
                                  uint64_t revision,uint64_t tick) {
        EarthworkChange all;
        all.revision=revision;
        all.dirty={-kWorldLimitCm,-kWorldLimitCm,
                   kWorldLimitCm,kWorldLimitCm};
        // The virtual edit forces an all-anchors support scan without
        // introducing an actual terrain mutation or allocating an R16 grid.
        all.vertices.push_back({});
        return applyGroundChange(ground,all,tick);
    }

private:
    static AnchoredPlantId identity(const RootAnchor& p){
        return {p.speciesId,p.plantKey};
    }
    static void validate(const AnchoredPlant& plant){
        const auto& p=plant.anchor;
        if(!p.speciesId || !p.plantKey ||
           p.xCm<-kWorldLimitCm || p.xCm>kWorldLimitCm ||
           p.zCm<-kWorldLimitCm || p.zCm>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave invalid root anchor");
        validateRootProfile(p.roots);
        if(p.roots.habit==PlantHabit::Grass)
            throw std::invalid_argument("TerraWeave grass has no rooted ECS records");
        (void)plantAppearance(plant.environment);
        if(plant.support.plantKey &&
           (plant.support.plantKey!=p.plantKey ||
            plant.support.speciesId!=p.speciesId ||
            plant.support.habit!=p.roots.habit))
            throw std::invalid_argument("TerraWeave wrong support identity");
        if(plant.support.supportPermille>1000 ||
           plant.support.exposedCm>p.roots.depthCm)
            throw std::invalid_argument("TerraWeave invalid persisted root state");
    }
    std::map<AnchoredPlantId,AnchoredPlant> plants_;
};

} // namespace aiverse::terraweave
