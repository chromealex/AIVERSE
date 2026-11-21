// TerraWeave opt-in forest scene host for every AIVERSE genre.
// CPU placement streaming is deterministic and bounded. GPU upload and
// draw submission use the existing Renderer3D/gfx::Device render thread.
// Does not create one ECS entity per cosmetic tree or mutate game state.
#pragma once

#include "engine/render/TerraWeaveForestAdapter.h"
#include "engine/world/AsyncChunkStream.h"
#include "engine/world/TerraWeaveExclusions.h"
#include "engine/world/TerraWeaveVegetationSoil.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ForestSceneSettings {
    TerrainSettings terrain;
    BiomeSettings biomes;
    ScatterSettings scatter;
    int32_t chunkSizeCm = 6400;       // signed world-space lattice tiles
    uint32_t radiusChunks = 4;        // residency extent around observer
    uint32_t maxResidentChunks = 64;  // hard CPU cache bound
    bool asynchronous = false;        // opt in; synchronous native API remains source compatible
    uint32_t maxPendingJobs = 3;      // 1..8; bounded queue and reserved bytes
    uint32_t requestsPerUpdate = 2;  // 1..8
    uint32_t publicationsPerUpdate = 2; // 1..8
    FloraView view{0, 0, 3000, 9000, 12000, 24000, 650, 3000};
    // Near-tree geometry (ArborForestAdapter::setTreeGeometry): Merged draws
    // every tree of a species/tier/material in one instanced call.
    ArborTreeGeometry treeGeometry = ArborTreeGeometry::Auto;
    TreeLodSettings treeLod;
    // GPU hierarchical wind masks on near trees (docs/wind-masks.md); with
    // Auto geometry this also selects merged whole-tree draws.
    bool hierarchicalWind = false;
};

struct ForestSceneStats {
    size_t chunks = 0;
    size_t instances = 0;
    size_t species = 0;
    uint64_t revision = 0;
    size_t preparing = 0, discarded = 0; // asynchronous streaming diagnostics
    size_t removed = 0; // persisted hidden tree identities
    size_t scheduledRegrowth = 0; // persistent visual regrowth schedules
    uint64_t presentationTick = 0; // caller-provided deterministic tick
};

// A stable resource/tree identity for gameplay-driven removal and restoration.
// The presentation forest does not own a gameplay entity for each tree.
// Native games may serialize this POD key set alongside their save data.
struct ForestTreeIdentity {
    uint64_t key = 0;
    uint32_t speciesId = 0;
    constexpr bool operator<(const ForestTreeIdentity& other) const {
        return speciesId == other.speciesId ? key < other.key :
                                             speciesId < other.speciesId;
    }
    constexpr bool operator==(const ForestTreeIdentity&) const = default;
};

// Persistent sparse lifecycle presentation state, keyed by procedural tree ID.
// The game owns authoritative harvesting, save/replication and actual growth
// mechanics. The forest host only filters/scales instances on a supplied tick.
struct ForestRegrowthSchedule {
    ForestTreeIdentity tree{};
    uint64_t regrowAtTick = 0;   // hidden before this tick
    uint64_t matureAtTick = 0;   // fully grown on/after this tick
    uint32_t initialScalePermille = 150; // relative to generated tree size
    constexpr bool operator==(const ForestRegrowthSchedule&) const = default;
};

// Optional sparse author/simulation overrides; default per-plant age and
// environmental values come from seeded FoliageInstance placement.
struct ForestPlantCondition {
    ForestTreeIdentity tree{};
    PlantEnvironment environment{};
};

// Header-only native-project API. Call addSpecies before streamAt/uploadModels.
// streamAt(...) is currently synchronous and must run on a host work phase;
// run it on an owning thread, not concurrently with submit/releaseModels.
class TerraWeaveForestScene {
public:
    // Optional host-provided *immutable* terrain snapshots. Decorative only.
    // Keep callbacks alive and stable while streamAt() evaluates patches.
    using HeightSource = std::function<int32_t(int64_t, int64_t)>;
    using PlacementFilter = std::function<bool(const FoliageInstance&)>;

    explicit TerraWeaveForestScene(ForestSceneSettings settings = {})
        : settings_(std::move(settings)) {
        if (settings_.chunkSizeCm < 100 || settings_.chunkSizeCm > 100000 ||
            settings_.radiusChunks > 8 ||
            settings_.maxResidentChunks == 0 ||
            settings_.maxResidentChunks > 289 ||
            settings_.scatter.spacingCm <= 0 ||
            settings_.scatter.densityPermille > 1000 ||
            settings_.maxPendingJobs == 0 || settings_.maxPendingJobs > 8 ||
            settings_.requestsPerUpdate == 0 || settings_.requestsPerUpdate > 8 ||
            settings_.publicationsPerUpdate == 0 ||
            settings_.publicationsPerUpdate > 8)
            throw std::invalid_argument("TerraWeave forest scene settings");
        // Bound scatter candidate work across the WHOLE cache, not merely
        // each chunk. Tiny spacing and a large radius must not allocate
        // hundreds of millions of instances before budget checks.
        const int64_t cellsPerAxis =
            (int64_t(settings_.chunkSizeCm) + settings_.scatter.spacingCm - 1) /
            settings_.scatter.spacingCm + 2;
        const int64_t perChunk = cellsPerAxis * cellsPerAxis;
        if (perChunk > 65536 ||
            perChunk * settings_.maxResidentChunks > 1000000)
            throw std::invalid_argument("TerraWeave forest scatter candidate budget");
        asyncReservation_ = 4096 + static_cast<size_t>(perChunk) * sizeof(FoliageInstance);
        // Validate LOD/far transition settings before first draw.
        (void)buildFloraBatches({}, settings_.view);
        const int32_t windEnd=std::max<int32_t>(1,settings_.view.cullRangeCm);
        adapter_.setWindTransitionRange(
            std::min<int32_t>(settings_.view.mediumRangeCm,windEnd-1),
            windEnd);
        if(settings_.hierarchicalWind)adapter_.enableHierarchicalWind();
        adapter_.setTreeGeometry(settings_.treeGeometry,settings_.treeLod);
    }

    // Per-species material hook (e.g. BotanicalMaterialTable textures);
    // call before addSpecies.
    void setPrototypeDecorator(ArborForestAdapter::PrototypeDecorator decorator) {
        if (residentStarted_ || gpuUploaded_)
            throw std::logic_error("TerraWeave species registry already in use");
        adapter_.setPrototypeDecorator(std::move(decorator));
    }

    void addSpecies(const TreeProfile& profile,
                    const FoliageSpecies& placement,
                    uint64_t prototypeSeed,ArborMeshSettings mesh = {8,true}) {
        if (residentStarted_ || gpuUploaded_)
            throw std::logic_error("TerraWeave species registry already in use");
        if (placement.id != profile.speciesId || placement.id == 0)
            throw std::invalid_argument("TerraWeave forest species ids");
        // Validates profile before insertion and prepares shared geometry/
        // impostor atlas once per species, rather than once per tree.
        adapter_.addPrototype(profile, prototypeSeed,mesh);
        adapter_.setRootProfile(profile.speciesId,
            defaultRootProfile(placement.habit));
        species_.push_back(placement);
    }

    // Scars from buildings, roads and authored clearings can be supplied as
    // world-space half-open centimetre rectangles. Changing them invalidates
    // the CPU cache without changing the deterministic generator settings.
    void setExclusions(std::vector<RectCm> rectangles) {
        for (const RectCm& r : rectangles) {
            if (r.maxX < r.minX || r.maxZ < r.minZ ||
                r.minX < -kWorldLimitCm || r.minZ < -kWorldLimitCm ||
                r.maxX > kWorldLimitCm || r.maxZ > kWorldLimitCm)
                throw std::invalid_argument("TerraWeave forest exclusion");
        }
        exclusions_ = std::move(rectangles);
        invalidate();
    }

    // Adapt procedural scatter to a real authored/physics terrain. Changing
    // a provider invalidates cached chunks; provider functions are evaluated
    // by streamAt(), not on GPU or from the authoritative ECS simulation.
    void setHeightSource(HeightSource source) {
        heightSource_ = std::move(source);
        invalidate();
    }
    void setPlacementFilter(PlacementFilter filter) {
        placementFilter_ = std::move(filter);
        invalidate();
    }
    // Geometry prepared by any project/terrain provider. Immutable while
    // workers are running; a new shared snapshot invalidates old results.
    void setPlacementMask(std::shared_ptr<const FoliageExclusionMask> mask) {
        placementMask_ = std::move(mask);
        invalidate();
    }

    // Publish one coherent immutable authored-world snapshot (terrain height
    // and footprint mask) with ONE revision change. This is cheaper and safer
    // than invalidating asynchronous jobs twice for the same chunk update.
    // Both providers must retain their captured data until they are released
    // by the last pending worker job.
    void setLandscapeSources(HeightSource height,
                             std::shared_ptr<const FoliageExclusionMask> mask) {
        heightSource_ = std::move(height);
        placementMask_ = std::move(mask);
        invalidate();
    }

    // Invalidate resident foliage if generation settings / landscape are
    // explicitly changed. Game-specific persistent harvest/removal sets
    // should be applied by the caller before presentation submission.
    void invalidate() {
        patches_.clear();
        ++revision_;
        if (revision_ == 0) revision_ = 1;
        // Live async jobs capture immutable configuration snapshots. Removing
        // a catalog key cancels its publication while its staging reservation
        // stays held until the worker completes and update() drains it.
        if (async_) {
            for (const world::Cell& cell : asyncCatalog_)
                async_->remove(cell, [](world::Cell) {});
            asyncCatalog_.clear();
            std::atomic_store_explicit(&asyncSnapshot_, captureSnapshot(), std::memory_order_release);
        }
    }

    // Prepare visible world chunks using globally keyed scatter.
    // Any spatial partition generates the same placements and stable IDs.
    // Select nearest candidates deterministically when the budget is full.
    void streamAt(ForestCameraCm camera) {
        if (camera.x < -kWorldLimitCm || camera.x > kWorldLimitCm ||
            camera.z < -kWorldLimitCm || camera.z > kWorldLimitCm)
            throw std::out_of_range("TerraWeave forest observer extent");
        if (settings_.asynchronous) {
            streamAsyncAt(camera);
            return;
        }
        residentStarted_ = true;
        observer_ = camera;
        const int64_t side = settings_.chunkSizeCm;
        const int64_t cx = detail::floorDiv(camera.x, side);
        const int64_t cz = detail::floorDiv(camera.z, side);
        using Key = std::pair<int64_t, int64_t>;
        std::vector<Key> candidates;
        const int64_t radius = settings_.radiusChunks;
        for (int64_t dz = -radius; dz <= radius; ++dz) {
            for (int64_t dx = -radius; dx <= radius; ++dx) {
                const int64_t x = (cx + dx) * side;
                const int64_t z = (cz + dz) * side;
                // Each scatter candidate samples +/-100cm for slope.
                if (x < -kWorldLimitCm + 100 ||
                    z < -kWorldLimitCm + 100 ||
                    x + side > kWorldLimitCm - 100 ||
                    z + side > kWorldLimitCm - 100)
                    continue;
                candidates.push_back({cx + dx, cz + dz});
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [cx, cz](Key a, Key b) {
                      const int64_t ax = a.first - cx, az = a.second - cz;
                      const int64_t bx = b.first - cx, bz = b.second - cz;
                      const int64_t da = ax * ax + az * az;
                      const int64_t db = bx * bx + bz * bz;
                      return da == db ? a < b : da < db;
                  });
        if (candidates.size() > settings_.maxResidentChunks)
            candidates.resize(settings_.maxResidentChunks);
        const std::set<Key> desired(candidates.begin(), candidates.end());
        for (auto it = patches_.begin(); it != patches_.end();) {
            if (!desired.contains(it->first)) it = patches_.erase(it);
            else ++it;
        }
        for (const Key& key : candidates) {
            if (patches_.contains(key)) continue;
            const int64_t minX = key.first * side;
            const int64_t minZ = key.second * side;
            auto instances = scatter(
                settings_.terrain, settings_.biomes, settings_.scatter,
                {minX, minZ, minX + side, minZ + side},
                species_, exclusions_);
            if (heightSource_ || placementFilter_ || placementMask_) {
                size_t write = 0;
                for (auto instance : instances) {
                    if (heightSource_)
                        instance.heightCm = heightSource_(instance.xCm, instance.zCm);
                    if (int64_t(instance.heightCm) < -kWorldLimitCm ||
                        int64_t(instance.heightCm) > kWorldLimitCm)
                        throw std::out_of_range("TerraWeave forest sampled height extent");
                    if (placementMask_ &&
                        placementMask_->excludes(instance.xCm, instance.zCm)) continue;
                    if (placementFilter_ && !placementFilter_(instance)) continue;
                    instances[write++] = instance;
                }
                instances.resize(write);
            }
            patches_.emplace(key, std::move(instances));
        }
    }

    // Native host supplies deterministic gameplay/replay time; no platform
    // clocks, floating elapsed time or RHI calls are used for regrowth.
    // Advancing this tick changes visibleBatches() without rescattering any
    // resident chunk or canceling in-flight worker jobs.
    void setPresentationTick(uint64_t tick) {
        presentationTick_=tick;
        adapter_.setPresentationTick(tick);
    }
    uint64_t presentationTick() const { return presentationTick_; }
    void setWind(ForestWindState value) {adapter_.setWind(value);}
    const ForestWindState& wind() const {return adapter_.wind();}
    void setWindField(std::shared_ptr<const ForestWindField> field) {
        adapter_.setWindField(std::move(field));
    }
    std::shared_ptr<const ForestWindField> windField() const {
        return adapter_.windField();
    }
    // Native hosts may tune the CPU/pose memory budget by quality tier.
    // Zero disables articulated branches, preserving root-only sway.
    void setBranchPoseBudgetBytes(size_t bytes) {
        adapter_.setBranchPoseBudgetBytes(bytes);
    }
    size_t branchPoseBudgetBytes() const {
        return adapter_.branchPoseBudgetBytes();
    }
    bool pendingRootUploads() const {return adapter_.pendingRootUploads();}
    size_t generatedRootPrototypes()const {
        return adapter_.generatedRootPrototypes();
    }
    // Global, presentation-only canopy appearance. PHYSIS or a game calendar
    // may translate its weather/season state into this genre-free POD.
    // Material overrides are updated once; resident scatter and async
    // generation revisions are untouched.
    void setPhenology(CanopyPhenology state) {
        adapter_.setPhenology(state);
    }
    const CanopyPhenology& phenology() const {return adapter_.phenology();}


    // A sparse, persistent regrowth timeline. When scheduled, the tree is
    // hidden until regrowAtTick, then smoothly scales from initialScalePermille
    // to its original generator scale by matureAtTick. The interval is
    // bounded so integer arithmetic is deterministic and overflow-free.
    void scheduleRegrowth(ForestRegrowthSchedule schedule) {
        validateRegrowth(schedule);
        if(regrowth_.size() >= 1000000 && !regrowth_.contains(schedule.tree))
            throw std::length_error("TerraWeave regrowth state too large");
        regrowth_[schedule.tree]=schedule;
        removed_.erase(schedule.tree);
    }
    bool cancelRegrowth(ForestTreeIdentity id) {
        return regrowth_.erase(id)!=0;
    }
    void setRegrowthSchedules(const std::vector<ForestRegrowthSchedule>& schedules) {
        if(schedules.size()>1000000)
            throw std::length_error("TerraWeave regrowth state too large");
        std::map<ForestTreeIdentity,ForestRegrowthSchedule> replacement;
        for(const auto& schedule:schedules) {
            validateRegrowth(schedule);
            if(!replacement.emplace(schedule.tree,schedule).second)
                throw std::invalid_argument("TerraWeave duplicate regrowth key");
        }
        // Load replacement atomically after all validation succeeds.
        for(const auto& [id,record]:replacement) {
            (void)record;
            removed_.erase(id);
        }
        regrowth_=std::move(replacement);
    }
    std::vector<ForestRegrowthSchedule> regrowthSchedules() const {
        std::vector<ForestRegrowthSchedule> out;
        out.reserve(regrowth_.size());
        for(const auto& [id,schedule]:regrowth_) {
            (void)id;
            out.push_back(schedule);
        }
        return out;
    }

    // Age/light/moisture/nutrients may change without rescattering foliage
    // or allocating ECS components for cosmetic plants. Stable identities are
    // caller-owned for save, replay and replication. Grass uses GrassPatch.
    void setPlantEnvironment(ForestTreeIdentity id,PlantEnvironment environment) {
        if(!id.speciesId)throw std::invalid_argument("TerraWeave plant id");
        (void)plantAppearance(environment); // validate before insertion
        if(conditions_.size()>=1000000u && !conditions_.contains(id))
            throw std::length_error("TerraWeave condition budget");
        conditions_[id]=environment;
    }
    void clearPlantEnvironment(ForestTreeIdentity id) {conditions_.erase(id);}
    void setPlantConditions(const std::vector<ForestPlantCondition>& conditions) {
        if(conditions.size()>1000000u)
            throw std::length_error("TerraWeave condition budget");
        std::map<ForestTreeIdentity,PlantEnvironment> next;
        for(const auto& condition:conditions) {
            if(!condition.tree.speciesId)
                throw std::invalid_argument("TerraWeave plant id");
            (void)plantAppearance(condition.environment);
            if(!next.emplace(condition.tree,condition.environment).second)
                throw std::invalid_argument("TerraWeave duplicate plant condition");
        }
        conditions_.swap(next);
    }
    std::vector<ForestPlantCondition> plantConditions() const {
        std::vector<ForestPlantCondition> out;
        out.reserve(conditions_.size());
        for(const auto& [id,environment]:conditions_)
            out.push_back({id,environment});
        return out;
    }

    // Root support is calculated by the game/terrain owner *only when* a
    // soil region changes. Each tree/shrub gets one small RootState, not
    // repeated sampling on every frame. Gameplay consumes fall events.
    void setRootStates(const std::vector<RootState>& states) {
        if(states.size()>1000000u)
            throw std::length_error("TerraWeave root-state budget");
        std::map<ForestTreeIdentity,RootState> next;
        for(const auto& state:states) {
            if(!state.speciesId || state.habit==PlantHabit::Grass ||
               state.supportPermille>1000u)
                throw std::invalid_argument("TerraWeave invalid root state");
            if(!next.emplace(ForestTreeIdentity{state.plantKey,state.speciesId},
                             state).second)
                throw std::invalid_argument("TerraWeave duplicate root state");
        }
        adapter_.setRootStates(states);
        rootStates_.swap(next);
    }
    std::optional<RootState> rootState(ForestTreeIdentity id)const {
        const auto it=rootStates_.find(id);
        return it==rootStates_.end()?std::nullopt:
            std::optional<RootState>(it->second);
    }
    std::vector<RootState> rootStates()const {
        std::vector<RootState> out;
        out.reserve(rootStates_.size());
        for(const auto& [id,state]:rootStates_) {
            (void)id;
            out.push_back(state);
        }
        return out;
    }

    // Dedicated fallen-tree visibility channel, independent of harvested
    // removals/regrowth. A rollback from fallen->standing cannot accidentally
    // resurrect a tree that gameplay harvested for another reason.
    void setFallenTrees(const std::vector<ForestTreeIdentity>& fallen) {
        if(fallen.size()>1000000u)
            throw std::length_error("TerraWeave fallen tree visibility budget");
        std::set<ForestTreeIdentity> next;
        for(const auto& id:fallen){
            if(!id.key || !id.speciesId || !next.insert(id).second)
                throw std::invalid_argument("TerraWeave duplicate/invalid fallen tree");
        }
        fallen_.swap(next);
    }
    std::vector<ForestTreeIdentity> fallenTrees()const{
        return {fallen_.begin(),fallen_.end()};
    }
    // Natural ecological death is independent of harvesting and uprooting.
    // A dead tree cannot reappear merely because another tree regrows.
    void setDeadTrees(const std::vector<ForestTreeIdentity>& dead) {
        if(dead.size()>1000000u)
            throw std::length_error("TerraWeave dead forest tree budget");
        std::set<ForestTreeIdentity> next;
        for(const auto& id:dead){
            if(!id.key || !id.speciesId || !next.insert(id).second)
                throw std::invalid_argument("TerraWeave duplicate/invalid dead tree");
        }
        dead_.swap(next);
    }
    std::vector<ForestTreeIdentity> deadTrees()const{
        return {dead_.begin(),dead_.end()};
    }

    // Spawned saplings reuse the existing per-species Arbor mesh/impostor
    // and the ordinary Flora LOD batches. A separate sparse, CPU-authored
    // list survives chunk streaming but no per-tree GPU geometry is made.
    void setRecruitedPlants(const std::vector<FoliageInstance>& plants) {
        if(plants.size()>65536u)
            throw std::length_error("TerraWeave recruited sapling budget");
        std::map<ForestTreeIdentity,FoliageInstance> next;
        for(const auto& plant:plants){
            if(!plant.key || !plant.speciesId ||
               plant.habit==PlantHabit::Grass ||
               plant.scalePermille==0 || plant.scalePermille>10000 ||
               plant.xCm<-kWorldLimitCm || plant.xCm>kWorldLimitCm ||
               plant.zCm<-kWorldLimitCm || plant.zCm>kWorldLimitCm ||
               int64_t(plant.heightCm)<-kWorldLimitCm ||
               int64_t(plant.heightCm)>kWorldLimitCm ||
               plant.yawMilliDegrees>=360000 ||
               !std::any_of(species_.begin(),species_.end(),
                   [&](const FoliageSpecies& sp){
                       return sp.id==plant.speciesId &&
                              sp.habit==plant.habit;
                   }))
                throw std::invalid_argument("TerraWeave invalid/unregistered sapling");
            const ForestTreeIdentity id{plant.key,plant.speciesId};
            if(!next.emplace(id,plant).second)
                throw std::invalid_argument("TerraWeave duplicated recruited sapling");
        }
        // One spatial bucket per EXISTING forest chunk. Runtime rendering
        // can now visit at most the nearby stream radius (<=17x17 buckets)
        // instead of scanning all 65k persistent recruits every frame.
        std::map<std::pair<int64_t,int64_t>,
                 std::vector<ForestTreeIdentity>> byChunk;
        for(const auto& [id,plant]:next) {
            const auto key=std::pair{
                detail::floorDiv(plant.xCm,settings_.chunkSizeCm),
                detail::floorDiv(plant.zCm,settings_.chunkSizeCm)};
            byChunk[key].push_back(id);
        }
        recruited_.swap(next);
        recruitedByChunk_.swap(byChunk);
    }
    size_t recruitedPlantCount() const {return recruited_.size();}

    // Gameplay systems can call this on the owning update thread when a
    // tree is harvested/destroyed; the stable key survives chunk unloading.
    // This is a presentation filter, NOT a substitute for authoritative ECS
    // inventory, collision, save versioning or network replication.
    bool removeTree(ForestTreeIdentity id) {
        if (!id.speciesId) throw std::invalid_argument("TerraWeave tree species");
        if (removed_.size() >= 1000000 && !removed_.contains(id))
            throw std::length_error("TerraWeave removed-tree state too large");
        regrowth_.erase(id); // a new harvest cancels previously scheduled growth
        return removed_.insert(id).second;
    }
    bool restoreTree(ForestTreeIdentity id) {
        const bool hadSchedule=regrowth_.erase(id)!=0;
        const bool wasRemoved=removed_.erase(id)!=0;
        return wasRemoved || hadSchedule;
    }

    void setRemovedTrees(const std::vector<ForestTreeIdentity>& ids) {
        if (ids.size() > 1000000)
            throw std::length_error("TerraWeave removed-tree state too large");
        std::set<ForestTreeIdentity> replacement;
        for (const auto& id : ids) {
            if (!id.speciesId)
                throw std::invalid_argument("TerraWeave tree species");
            replacement.insert(id);
        }
        removed_ = std::move(replacement);
        // Explicitly hidden trees take precedence over any loaded regrowth.
        for(const auto& id:removed_)regrowth_.erase(id);
    }
    std::vector<ForestTreeIdentity> removedTrees() const {
        return {removed_.begin(), removed_.end()};
    }

    std::vector<FloraBatch> visibleBatches() const {
        if (!residentStarted_) return {};
        size_t count = 0;
        for (const auto& [key, patch] : patches_) {
            (void)key;
            count += patch.size();
        }
        std::vector<FoliageInstance> placements;
        placements.reserve(count+std::min<size_t>(recruited_.size(),1024));
        for (const auto& [key, patch] : patches_) {
            (void)key;
            for (const FoliageInstance& instance : patch) {
                if(auto shown=presentationInstance(instance))
                    placements.push_back(*shown);
            }
        }
        // Recruits are sparse, persistent and rendered only by the
        // existing Arbor prototypes. The normal Flora distance/LOD cull
        // rejects those outside the visual forest radius.
        const int64_t cx=detail::floorDiv(
            observer_.x,settings_.chunkSizeCm);
        const int64_t cz=detail::floorDiv(
            observer_.z,settings_.chunkSizeCm);
        const int64_t radius=settings_.radiusChunks;
        for(int64_t dz=-radius;dz<=radius;++dz)
            for(int64_t dx=-radius;dx<=radius;++dx){
                const auto found=recruitedByChunk_.find({
                    cx+dx,cz+dz});
                if(found==recruitedByChunk_.end())continue;
                for(const auto& id:found->second){
                    const auto& plant=recruited_.at(id);
                    if(auto shown=presentationInstance(plant))
                        placements.push_back(*shown);
                }
            }
        FloraView view = settings_.view;
        view.cameraXcm = observer_.x;
        view.cameraZcm = observer_.z;
        return buildFloraBatches(placements, view);
    }


    // Native gameplay selection without allocating per-tree ECS entities.
    // Searches only resident CPU scatter patches, regardless of camera LOD.
    // Hidden/removed trees are excluded; growing trees return visual scale.
    // Equal-distance ties resolve by stable species ID and tree key.
    std::optional<FoliageInstance> nearestTreeAt(
            int64_t xCm,int64_t zCm,int32_t radiusCm=500) const {
        validateQueryCenter(xCm,zCm);
        if(radiusCm<=0 || radiusCm>10000000)
            throw std::invalid_argument("TerraWeave tree query radius");
        const int64_t radius=radiusCm;
        const int64_t radiusSquared=radius*radius;
        std::optional<FoliageInstance> best;
        int64_t bestDist=radiusSquared;
        for(const auto& [cell,patch]:patches_){
            if(!patchOverlaps(cell,{xCm-radius,zCm-radius,
                                    xCm+radius+1,zCm+radius+1}))continue;
            for(const FoliageInstance& instance:patch) {
                const int64_t dx=instance.xCm-xCm,dz=instance.zCm-zCm;
                if(dx<-radius || dx>radius ||
                   dz<-radius || dz>radius)continue;
                const int64_t dist=dx*dx+dz*dz;
                if(dist>bestDist)continue;
                const auto shown=presentationInstance(instance);
                if(!shown)continue;
                if(!best || dist<bestDist ||
                   (dist==bestDist &&
                    ForestTreeIdentity{shown->key,shown->speciesId} <
                    ForestTreeIdentity{best->key,best->speciesId})){
                    best=*shown;
                    bestDist=dist;
                }
            }
        }
        // These are explicit ECS-grown saplings, not present in procedural
        // scatter patches. Selection/harvest queries must see them too.
        for(const auto& [id,instance]:recruited_){
            (void)id;
            const int64_t dx=instance.xCm-xCm,dz=instance.zCm-zCm;
            if(dx<-radius || dx>radius || dz<-radius || dz>radius)
                continue;
            const int64_t dist=dx*dx+dz*dz;
            if(dist>bestDist)continue;
            const auto shown=presentationInstance(instance);
            if(!shown)continue;
            if(!best || dist<bestDist ||
               (dist==bestDist &&
                ForestTreeIdentity{shown->key,shown->speciesId} <
                ForestTreeIdentity{best->key,best->speciesId})){
                best=*shown;
                bestDist=dist;
            }
        }
        return best;
    }

    // Bounded rectangle query for construction/harvest gameplay. Results are
    // sorted by stable species/key; an oversized match set throws instead of
    // silently dropping arbitrary trees or producing unbounded allocations.
    std::vector<FoliageInstance> treesInRect(
            RectCm region,size_t maxResults=1024) const {
        if(region.minX>=region.maxX || region.minZ>=region.maxZ ||
           region.minX<-kWorldLimitCm || region.minZ<-kWorldLimitCm ||
           region.maxX>kWorldLimitCm || region.maxZ>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave tree query rectangle");
        if(maxResults==0 || maxResults>100000)
            throw std::invalid_argument("TerraWeave tree query result budget");
        std::vector<FoliageInstance> results;
        for(const auto& [cell,patch]:patches_){
            if(!patchOverlaps(cell,region))continue;
            for(const FoliageInstance& instance:patch){
                if(!region.contains(instance.xCm,instance.zCm))continue;
                const auto shown=presentationInstance(instance);
                if(!shown)continue;
                if(results.size()==maxResults)
                    throw std::length_error("TerraWeave tree query result overflow");
                results.push_back(*shown);
            }
        }
        for(const auto& [id,instance]:recruited_){
            (void)id;
            if(!region.contains(instance.xCm,instance.zCm))continue;
            const auto shown=presentationInstance(instance);
            if(!shown)continue;
            if(results.size()==maxResults)
                throw std::length_error("TerraWeave recruited tree query overflow");
            results.push_back(*shown);
        }
        std::sort(results.begin(),results.end(),
            [](const FoliageInstance& a,const FoliageInstance& b){
                if(a.speciesId!=b.speciesId)return a.speciesId<b.speciesId;
                if(a.key!=b.key)return a.key<b.key;
                if(a.xCm!=b.xCm)return a.xCm<b.xCm;
                return a.zCm<b.zCm;
            });
        return results;
    }

    // Call on GPU-owning render thread AFTER all species are registered.
    bool uploadModels(gfx::Device& device) {
        if (!adapter_.uploadModels(device)) {
            // Upload can fail midway through prototype iteration; free
            // successfully uploaded buffers/textures before retry or abort.
            adapter_.releaseModels(device);
            gpuUploaded_ = false;
            return false;
        }
        gpuUploaded_ = true;
        return true;
    }

    // Submit after renderer.beginScene, before renderer.render.
    // No own shaders, passes, buffers or engine-global ECS scheduler.
    void submit(render::Renderer3D& renderer,
                ForestRenderOriginCm origin = {}) const {
        if (!gpuUploaded_) throw std::logic_error(
            "TerraWeave forest models must be uploaded before drawing");
        const auto batches = visibleBatches();
        adapter_.submit(renderer, batches, origin);
    }

    void releaseModels(gfx::Device& device) {
        adapter_.releaseModels(device);
        gpuUploaded_ = false;
    }

    ForestSceneStats stats() const {
        size_t n = 0;
        for (const auto& [key, patch] : patches_) {
            (void)key;
            n += patch.size();
        }
        const auto state = async_ ? async_->status() : world::AsyncChunkStream::Status{};
        return {patches_.size(), n, species_.size(), revision_,
                state.preparing, state.discarded, removed_.size(),
                regrowth_.size(), presentationTick_};
    }

    const ForestSceneSettings& settings() const { return settings_; }
    // Must be called on the owning update thread, before destroying captured
    // terrain snapshots or callback contexts. Also called by the destructor.
    void shutdownAsync() {
        if (async_) {
            async_->shutdown([this](world::Cell cell) {
                patches_.erase({cell.x, cell.z});
            });
            async_.reset();
            asyncCatalog_.clear();
        }
        std::atomic_store_explicit(&asyncSnapshot_, std::shared_ptr<const GenerationSnapshot>{}, std::memory_order_release);
    }
    ~TerraWeaveForestScene() { shutdownAsync(); }
    const ArborForestAdapter& adapter() const { return adapter_; }

private:
    static void validateQueryCenter(int64_t x,int64_t z){
        if(x<-kWorldLimitCm || x>kWorldLimitCm ||
           z<-kWorldLimitCm || z>kWorldLimitCm)
            throw std::out_of_range("TerraWeave tree query world extent");
    }
    bool patchOverlaps(std::pair<int64_t,int64_t> cell,RectCm region) const {
        const int64_t side=settings_.chunkSizeCm;
        const int64_t minX=cell.first*side,minZ=cell.second*side;
        return minX<region.maxX && minX+side>region.minX &&
               minZ<region.maxZ && minZ+side>region.minZ;
    }
    std::optional<FoliageInstance> presentationInstance(
            const FoliageInstance& instance) const {
        const ForestTreeIdentity id{instance.key,instance.speciesId};
        if(removed_.contains(id) || fallen_.contains(id) ||
           dead_.contains(id))return std::nullopt;
        FoliageInstance scaled=instance;
        if(const auto root=rootStates_.find(id);root!=rootStates_.end())
            scaled.heightCm=root->second.plantedHeightCm;
        PlantEnvironment environment{
            instance.ageTicks,instance.matureTicks,instance.lifespanTicks,
            instance.lightPermille,instance.waterPermille,
            instance.nutrientsPermille};
        if(const auto condition=conditions_.find(id);condition!=conditions_.end())
            environment=condition->second;
        const PlantAppearance appearance=plantAppearance(environment);
        scaled.ageTicks=environment.ageTicks;
        scaled.lightPermille=environment.lightPermille;
        scaled.waterPermille=environment.waterPermille;
        scaled.nutrientsPermille=environment.nutrientsPermille;
        scaled.foliagePermille=appearance.foliagePermille;
        uint32_t factor=appearance.sizePermille;
        const auto it=regrowth_.find(id);
        if(it!=regrowth_.end()) {
            const auto& growth=it->second;
            if(presentationTick_<growth.regrowAtTick)return std::nullopt;
            uint32_t regrowFactor=1000;
            if(presentationTick_<growth.matureAtTick &&
               growth.matureAtTick>growth.regrowAtTick){
                const uint64_t duration=growth.matureAtTick-growth.regrowAtTick;
                const uint64_t elapsed=presentationTick_-growth.regrowAtTick;
                regrowFactor=growth.initialScalePermille+static_cast<uint32_t>(
                    elapsed*(1000u-growth.initialScalePermille)/duration);
            }
            factor=uint32_t(uint64_t(factor)*regrowFactor/1000u);
        }
        scaled.scalePermille=std::max<uint32_t>(1u,
            uint32_t(uint64_t(instance.scalePermille)*factor/1000u));
        return scaled;
    }
    static void validateRegrowth(const ForestRegrowthSchedule& value) {
        constexpr uint64_t kMaxGrowthTicks=1000000000000ULL;
        if(!value.tree.speciesId ||
           !value.initialScalePermille ||
           value.initialScalePermille>1000 ||
           value.matureAtTick<value.regrowAtTick ||
           value.matureAtTick-value.regrowAtTick>kMaxGrowthTicks)
            throw std::invalid_argument("TerraWeave regrowth schedule");
    }
    struct GenerationSnapshot {
        TerrainSettings terrain;
        BiomeSettings biomes;
        ScatterSettings scatter;
        std::vector<FoliageSpecies> species;
        std::vector<RectCm> exclusions;
        HeightSource heightSource;
        PlacementFilter placementFilter;
        std::shared_ptr<const FoliageExclusionMask> placementMask;
        int32_t side = 0;
        uint64_t revision = 0;
    };
    struct PreparedPatch final : world::AsyncChunkStream::Resource {
        uint64_t revision = 0;
        std::vector<FoliageInstance> instances;
    };
    std::shared_ptr<const GenerationSnapshot> captureSnapshot() const {
        auto snap = std::make_shared<GenerationSnapshot>();
        snap->terrain = settings_.terrain;
        snap->biomes = settings_.biomes;
        snap->scatter = settings_.scatter;
        snap->species = species_;
        snap->exclusions = exclusions_;
        snap->heightSource = heightSource_;
        snap->placementFilter = placementFilter_;
        snap->placementMask = placementMask_;
        snap->side = settings_.chunkSizeCm;
        snap->revision = revision_;
        return snap;
    }
    static std::vector<FoliageInstance> preparePatch(
            const GenerationSnapshot& s, int64_t cx, int64_t cz) {
        const int64_t minX = cx * s.side, minZ = cz * s.side;
        auto instances = scatter(s.terrain, s.biomes, s.scatter,
            {minX, minZ, minX + s.side, minZ + s.side},
            s.species, s.exclusions);
        if (s.heightSource || s.placementFilter || s.placementMask) {
            size_t write = 0;
            for (auto instance : instances) {
                if (s.heightSource)
                    instance.heightCm = s.heightSource(instance.xCm, instance.zCm);
                if (int64_t(instance.heightCm) < -kWorldLimitCm ||
                    int64_t(instance.heightCm) > kWorldLimitCm)
                    throw std::out_of_range("TerraWeave forest sampled height extent");
                if (s.placementMask &&
                    s.placementMask->excludes(instance.xCm, instance.zCm)) continue;
                if (s.placementFilter && !s.placementFilter(instance)) continue;
                instances[write++] = instance;
            }
            instances.resize(write);
        }
        return instances;
    }
    void streamAsyncAt(ForestCameraCm camera) {
        // All catalog mutation/publication remains on the caller's update
        // thread. Only pure preparePatch(snapshot) runs on the worker thread.
        residentStarted_ = true;
        observer_ = camera;
        if (!async_) {
            std::atomic_store_explicit(&asyncSnapshot_, captureSnapshot(), std::memory_order_release);
            world::StreamLimits limits;
            limits.residentChunks = settings_.maxResidentChunks;
            limits.residentBytes = asyncReservation_ * settings_.maxResidentChunks;
            limits.retryUpdates = 2;
            async_ = std::make_unique<world::AsyncChunkStream>(
                limits, asyncReservation_ * (settings_.maxPendingJobs + 1u),
                settings_.maxPendingJobs,
                [this](world::Cell cell) -> world::AsyncChunkStream::Payload {
                    const auto snap = std::atomic_load_explicit(&asyncSnapshot_, std::memory_order_acquire);
                    if (!snap) return {};
                    auto result = std::make_shared<PreparedPatch>();
                    result->revision = snap->revision;
                    result->instances = preparePatch(*snap, cell.x, cell.z);
                    return result;
                });
        }
        const int64_t side = settings_.chunkSizeCm;
        const int64_t cx = detail::floorDiv(camera.x, side);
        const int64_t cz = detail::floorDiv(camera.z, side);
        using Key = std::pair<int64_t, int64_t>;
        std::vector<Key> candidates;
        for (int64_t dz = -int64_t(settings_.radiusChunks);
                dz <= int64_t(settings_.radiusChunks); ++dz)
            for (int64_t dx = -int64_t(settings_.radiusChunks);
                    dx <= int64_t(settings_.radiusChunks); ++dx) {
                const int64_t minX = (cx + dx) * side;
                const int64_t minZ = (cz + dz) * side;
                if (minX < -kWorldLimitCm + 100 ||
                    minZ < -kWorldLimitCm + 100 ||
                    minX + side > kWorldLimitCm - 100 ||
                    minZ + side > kWorldLimitCm - 100) continue;
                candidates.push_back({cx + dx, cz + dz});
            }
        std::sort(candidates.begin(), candidates.end(), [cx,cz](Key a,Key b) {
            const int64_t ax=a.first-cx, az=a.second-cz;
            const int64_t bx=b.first-cx, bz=b.second-cz;
            const int64_t da=ax*ax+az*az, db=bx*bx+bz*bz;
            return da==db?a<b:da<db;
        });
        if (candidates.size() > settings_.maxResidentChunks)
            candidates.resize(settings_.maxResidentChunks);
        std::set<world::Cell> wanted;
        std::vector<world::Cell> pins;
        pins.reserve(candidates.size());
        for (const Key& key : candidates) {
            // Forest key range is limited to +/-1e9 cm, so uint32 Cell
            // coordinates cannot overflow for any supported chunk size.
            world::Cell cell{static_cast<int32_t>(key.first), 0,
                             static_cast<int32_t>(key.second)};
            wanted.insert(cell);
            pins.push_back(cell);
        }
        for (auto it = asyncCatalog_.begin(); it != asyncCatalog_.end();) {
            if (wanted.contains(*it)) { ++it; continue; }
            const world::Cell cell=*it;
            async_->remove(cell, [this](world::Cell old) {
                patches_.erase({old.x, old.z});
            });
            patches_.erase({cell.x, cell.z});
            it=asyncCatalog_.erase(it);
        }
        for (const world::Cell& cell : pins) {
            if (asyncCatalog_.contains(cell)) continue;
            if (!async_->add(cell, asyncReservation_))
                throw std::runtime_error("TerraWeave async chunk reservation failed");
            asyncCatalog_.insert(cell);
        }
        async_->update({}, std::span<const world::Cell>(pins.data(),pins.size()),
            settings_.requestsPerUpdate, settings_.publicationsPerUpdate,
            [this](world::Cell cell,const world::AsyncChunkStream::Payload& payload) {
                const auto& patch = static_cast<const PreparedPatch&>(*payload);
                if (patch.revision != revision_ || !asyncCatalog_.contains(cell))
                    return false;
                patches_[{cell.x,cell.z}] = patch.instances;
                return true;
            },
            [this](world::Cell cell) {
                patches_.erase({cell.x,cell.z});
            });
    }

    ForestSceneSettings settings_;
    ArborForestAdapter adapter_;
    std::vector<FoliageSpecies> species_;
    std::set<ForestTreeIdentity> removed_;
    std::set<ForestTreeIdentity> fallen_;
    std::set<ForestTreeIdentity> dead_;
    std::map<ForestTreeIdentity,FoliageInstance> recruited_;
    std::map<std::pair<int64_t,int64_t>,
             std::vector<ForestTreeIdentity>> recruitedByChunk_;
    std::map<ForestTreeIdentity,ForestRegrowthSchedule> regrowth_;
    std::map<ForestTreeIdentity,PlantEnvironment> conditions_;
    std::map<ForestTreeIdentity,RootState> rootStates_;
    uint64_t presentationTick_ = 0;
    std::vector<RectCm> exclusions_;
    HeightSource heightSource_;
    PlacementFilter placementFilter_;
    std::shared_ptr<const FoliageExclusionMask> placementMask_;
    std::map<std::pair<int64_t, int64_t>, std::vector<FoliageInstance>> patches_;
    ForestCameraCm observer_;
    uint64_t revision_ = 1;
    bool residentStarted_ = false;
    bool gpuUploaded_ = false;
    size_t asyncReservation_ = 0;
    // C++20 atomic<shared_ptr> specialization is unavailable in some
    // Apple libc++ SDK configurations. Atomic shared_ptr free functions
    // provide the same acquire/release snapshot publication semantics.
    std::shared_ptr<const GenerationSnapshot> asyncSnapshot_{};
    std::unique_ptr<world::AsyncChunkStream> async_;
    std::set<world::Cell> asyncCatalog_;
};

} // namespace aiverse::terraweave
