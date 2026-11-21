#pragma once
#include "WorldAssets.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/GpuOwnedMemory.h"
#include "engine/render/AsyncResourceRetirementQueue.h"
#include "engine/world/NereidWaterScene.h"
#include "engine/world/NereidFloodVisualScene.h"
#include "engine/world/NereidCascadeScene.h"
#include "engine/world/NereidConfluenceScene.h"
#include "engine/world/NereidUnderwaterView.h"
#include "engine/world/NereidRipples.h"
#include "engine/world/NereidWindPresentation.h"
#include "engine/world/NereidWindField.h"
#include <aiverselib/water/NereidFlowObstacles.h>
#include <cmath>
#include <algorithm>
#include "engine/world/TerraWeave.h"
#include "engine/world/TerraWeaveEarthworks.h"
#include <set>
#include "engine/world/TerraWeaveVegetationSoil.h"
#include "engine/world/TerraWeavePhenology.h"
#include "engine/world/TerraWeaveWindField.h"
#include "engine/world/TerraWeaveWindUploadPolicy.h"
#include "engine/world/TerraWeaveWindShadowCachePolicy.h"
#include "engine/world/TerraWeaveHorizonPresentationQuality.h"
#include "engine/world/TerraWeaveHorizonAdaptiveQuality.h"

namespace aiverse::terraweave { class TerraWeaveForestScene; class TerraWeaveHorizonScene; }
namespace aiverse::world {
// Opt-in read-only visual copy of an authoritative ECS water obstruction.
// sceneWaterId is the water-manifest stable ID (NOT an ECS Entity.index).
struct NereidSceneFlowObstruction {
    uint64_t sceneWaterId=0;
    lib::nereid::WaterFlowObstructionComponent effect{};
};
// CPU model decode/terrain tessellation in the worker; GPU publication on update's thread.
class WorldScene {
public:
    WorldScene(const Json& manifest,AssetReader reader,std::shared_ptr<render::ModelTextureCache> textures = {});
    ~WorldScene();
    void update(gfx::Device&,std::span<const Vec3> observers,int radius=2);
    void draw(render::Renderer3D&,Cell renderOrigin,int originCellSize);
    void shutdown(gfx::Device&);
    int worldCellSizeMetres()const{return assets_.cellSize;}
    // Owned resident chunk models only. ModelTextureCache is potentially
    // shared with another view, so this never charges its textures again.
    // Distinct shared Model objects are deduplicated across resident cells.
    render::GpuOwnedMemoryStats ownedGpuMemoryStats(
        const gfx::Device& device) const;
    // Call at the explicit owning Device frame boundary, BEFORE new draws.
    // Shared CPU/worker references must drop before a Model can acquire
    // its final GPU retirement marker.
    bool collectRetired(gfx::Device&);
    size_t pendingGpuRetirements() const noexcept {
        return deferredModels_.size()+retiredModels_.pending();
    }
    // Opt-in presentation-only water; call before update. Never mutates ECS.
    void setNereidSurfaces(std::vector<NereidSceneSurface> surfaces);
    size_t nereidVisibleSurfaceCount()const;
    // Optional finite-volume flood overlay, supplied by native NEREID ECS
    // on the graphics/update owner thread after its completed water tick.
    void setNereidFloodCells(std::vector<NereidFloodVisualCell> cells){
        floodVisual_.setCells(std::move(cells));
    }
    size_t nereidFloodCellCount()const{return floodVisual_.cells();}
    NereidUnderwaterView nereidUnderwaterAt(Vec3 camera)const;
    // Native projects may override static waterfall manifest from ECS.
    void setNereidCascades(std::vector<NereidCascadeSource> cascades) {
        cascades_.setCascades(std::move(cascades));
    }
    size_t nereidCascadeCount()const{return cascades_.residentCount();}
    // Publish an immutable confluence snapshot from the same ECS tick
    // as setNereidSurfaces; missing body snapshots suppress contribution.
    void setNereidConfluences(std::vector<NereidConfluenceSource> sources) {
        confluences_.setConfluences(std::move(sources));
    }
    size_t nereidConfluenceCount()const{return confluences_.count();}
    // Host captures Box3D trees/soil debris and maps ECS body -> stable
    // water scene ID at a completed server tick, then publishes here on
    // WorldScene owner thread. Never query mutable physics/ECS in draw().
    void setNereidFlowObstructions(
        std::vector<NereidSceneFlowObstruction> obstructions);
    size_t nereidFlowObstructionCount()const{
        return nereidFlowObstructions_.size();
    }
    Vec3 resolveNereidFlowObstructions(
        uint64_t sceneWaterId,Vec3 sampleAt,Vec3 current)const {
        const auto* source=nereidWater_.surfaceForId(sceneWaterId);
        if(!source)return current;
        for(const auto& obstacle:nereidFlowObstructions_){
            if(obstacle.sceneWaterId!=sceneWaterId ||
               obstacle.effect.topY<=source->body.bottomY ||
               obstacle.effect.bottomY>=source->body.surfaceY)
                continue;
            current=lib::nereid::deflectNereidFlow(
                current,obstacle.effect,sampleAt);
        }
        return current;
    }
    template<class Fn> void forEachNereidCascade(Fn&& fn)const {
        cascades_.forEachSource(std::forward<Fn>(fn));
    }
    // Explicit presentation read barrier: consume one ECS tick of events.
    void ingestNereidInteractions(
        const std::vector<lib::nereid::WaterPresentationEvent>& events,
        uint32_t completedTick) {ripples_.ingest(events,completedTick);}
    template<class Callback> void ingestNereidInteractions(
        const std::vector<lib::nereid::WaterPresentationEvent>& events,
        uint32_t completedTick,Callback&& onSpectraFxOrAudio) {
        ripples_.ingest(events,completedTick,
                        std::forward<Callback>(onSpectraFxOrAudio));
    }
    void advanceNereidRipples(float deltaSeconds) {
        ripples_.advance(deltaSeconds,[this](uint64_t waterId,
                                     double x,double y,double z) {
            // Presentation-only coordinate quantization before an immutable
            // fixed-point current lookup; never enters authoritative ECS.
            auto fixed=[](double value) {
                constexpr double limit=1e12;
                const double safe=std::clamp(value,-limit,limit);
                return Fixed::fromRaw(int64_t(std::llround(safe*Fixed::kOne)));
            };
            const Vec3 position{fixed(x),fixed(y),fixed(z)};
            const Vec3 bodyFlow=nereidWater_.currentForInteractionWater(
                waterId,position);
            if(waterId==UINT64_MAX)return bodyFlow;
            const Vec3 joined=confluences_.flowForWater(
                waterId+1,position,bodyFlow,nereidWater_);
            const Vec3 result=cascades_.flowAtConnection(
                waterId+1,position,joined,nereidWater_);
            return resolveNereidFlowObstructions(
                waterId+1,position,result);
        });
        ripples_.applyVisualWind(deltaSeconds,
            [this](double x,double z) {
                return windField_.sample(x,z);
            });
    }
    void setNereidVisualWind(NereidVisualWind wind) {
        windField_.setBase(wind);
    }
    NereidVisualWind nereidVisualWind()const{return windField_.base();}
    void setNereidWindZones(std::vector<NereidWindZone> zones) {
        windField_.setZones(std::move(zones));
    }
    const NereidVisualWindField& nereidWindField()const{return windField_;}
    const NereidRippleField& nereidRipples()const{return ripples_;}

    // Runtime forest clearing rectangles, signed world centimetres,
    // half-open. Shared across near Flora and far Horizon. Set on the
    // WorldScene update thread, not while worker/render callbacks execute.
    void setVegetationClearings(std::vector<terraweave::RectCm> clearings);
    // Native-authority terrain handoff. The host publishes an immutable
    // EarthworksLayer snapshot whose base callback also captures immutable data.
    // Only forest/Horizon read this; native game owns collision/RHI changes.
    // nullptr restores authored/procedural height sampling.
    void setVegetationTerrainSnapshot(
        std::shared_ptr<const terraweave::EarthworksLayer> soil,
        std::vector<terraweave::RectCm> edited = {});
    // Suppress ONLY the old static terrain draw for tiles where a complete,
    // already-GPU-resident replacement is submitted by the native host.
    // This does not unload/change terrain collision or other mesh instances.
    void setReplacedTerrainTiles(std::vector<Cell> residentReplacementCells);
    size_t replacedTerrainTileCount() const {return replacedTerrainTiles_.size();}
    uint64_t vegetationTerrainRevision() const {
        return vegetationTerrain_ ? vegetationTerrain_->revision() : 0;
    }
    // Explicit authoritative soil->presentation handoff for tree/shrub root
    // exposure. WorldScene never decides gameplay falling or edits ECS.
    void setVegetationRootStates(std::vector<terraweave::RootState> states);
    size_t vegetationRootPrototypeCount()const;
    const std::vector<terraweave::RectCm>& vegetationClearings() const {
        return vegetationClearings_;
    }
    // Host-managed seasonal/PHYSIS bridge. Leaves cosmetic near-tree
    // scatter intact, and rebakes only the distant Horizon atlas.
    void setVegetationPhenology(terraweave::CanopyPhenology state);
    const terraweave::CanopyPhenology& vegetationPhenology() const {
        return vegetationPhenology_;
    }
    // PHYSIS-neutral wind bridge: pass snapshot wind velocities in cm/s.
    // Tick is the host's deterministic simulation/presentation frame count.
    void setVegetationWind(terraweave::ForestWindState wind);
    terraweave::ForestWindState vegetationWind() const {return vegetationWind_;}
    // Immutable bounded PHYSIS/game spatial wind grid. Null removes it.
    // The same snapshot is forwarded to near Flora and Horizon HLOD.
    void setVegetationWindField(
        std::shared_ptr<const terraweave::ForestWindField> field);
    std::shared_ptr<const terraweave::ForestWindField> vegetationWindField() const {
        return vegetationWindField_;
    }
    void setVegetationPresentationTick(uint64_t tick);
    uint64_t vegetationPresentationTick() const {return vegetationPresentationTick_;}
    // Quality/performance switch for independent 2.4km Horizon cluster
    // wind variation. Disabling keeps a single shared macro wind shader.
    void setVegetationHorizonSpatialWind(bool enabled);
    bool vegetationHorizonSpatialWind() const {
        return vegetationHorizonSpatialWind_;
    }
    // Exact per-cluster PHYSIS field is optional; false keeps the previous
    // static, procedural Horizon variation and avoids dynamic VB patches.
    void setVegetationHorizonExactWindField(bool enabled);
    bool vegetationHorizonExactWindField() const {
        return vegetationHorizonExactWindField_;
    }
    // Runtime tuneable, genre-independent Horizon wind RHI bandwidth.
    void setVegetationHorizonWindUploadPolicy(
        terraweave::HorizonWindUploadPolicy policy);
    terraweave::HorizonWindUploadPolicy vegetationHorizonWindUploadPolicy() const {
        return vegetationHorizonWindUploadPolicy_;
    }
    // Maximum CPU wind shadow capacity retained by inactive Horizon GPU
    // LRU entries. An over-budget entry is fully evicted and later reloaded.
    void setVegetationHorizonWindShadowCachePolicy(
        terraweave::HorizonWindShadowCachePolicy policy);
    terraweave::HorizonWindShadowCachePolicy
    vegetationHorizonWindShadowCachePolicy() const {
        return vegetationHorizonWindShadowCachePolicy_;
    }
    // Compose existing presentation switches and Stage36-38 runtime policies
    // without altering any Horizon geometry/bake quality or game simulation.
    void setVegetationHorizonPresentationPolicy(
        terraweave::HorizonPresentationPolicy policy);
    terraweave::HorizonPresentationPolicy
    vegetationHorizonPresentationPolicy() const {
        terraweave::HorizonPresentationPolicy policy;
        policy.exactSpatialWindField=vegetationHorizonExactWindField_;
        policy.spatialWindVariation=vegetationHorizonSpatialWind_;
        policy.windUpload=vegetationHorizonWindUploadPolicy_;
        policy.windShadowCache=vegetationHorizonWindShadowCachePolicy_;
        return policy;
    }
    void setVegetationHorizonPresentationQualityPreset(
        terraweave::HorizonPresentationQualityPreset preset);
    // Stage40 host-fed controller: frames are monotonically indexed,
    // timings are actual measured CPU/GPU duration in microseconds.
    // Defaults OFF. A manual Stage39 setter opts out of adaptation.
    void setVegetationHorizonAdaptiveQualityConfig(
        terraweave::HorizonAdaptiveQualityConfig config);
    terraweave::HorizonAdaptiveQualityConfig
    vegetationHorizonAdaptiveQualityConfig() const {
        return vegetationHorizonAdaptiveConfig_;
    }
    terraweave::HorizonAdaptiveQualityState
    vegetationHorizonAdaptiveQualityState() const;
    bool submitVegetationHorizonFrameTiming(
        terraweave::HorizonFrameTimingSample sample);
    const AsyncChunkStream::Status& status()const{return stream_->status();}
    // Optional genre-independent presentation forest. Native hosts may feed
    // a deterministic tick and schedule sparse harvest/regrowth without
    // touching renderer buffers or creating one entity per cosmetic tree.
    // Null when terraweaveForest is absent from the world manifest.
    terraweave::TerraWeaveForestScene* forestScene() noexcept {
        return terraForest_.get();
    }
    const terraweave::TerraWeaveForestScene* forestScene() const noexcept {
        return terraForest_.get();
    }
private:
    struct Instance { size_t model=0;render::mat4 transform; };
    struct Prepared final:AsyncChunkStream::Resource {
        std::vector<std::shared_ptr<render::Model>> models;
        std::vector<std::string> sharedKeys;                  // cache key per model, empty: owned by this chunk
        std::vector<Instance> instances;
        // Optional render-only authored landscape snapshot, populated only
        // when this WorldScene opts into TerraWeaveForest. Fixed metre data
        // is converted to signed cm before workers publish it.
        uint32_t terrainResolution = 0;
        std::vector<int32_t> terrainHeightsCm;
        std::vector<Bounds> placementBoxes; // local-space collision footprints
    };
    struct ModelCache;
    WorldAssets assets_;
    std::shared_ptr<render::ModelTextureCache> textures_;
    std::shared_ptr<ModelCache> cache_;                       // "shareModels": one decoded / uploaded model per file
    bool shareModels_=false;
    // Null unless the manifest explicitly opts in to procedural forests.
    std::unique_ptr<terraweave::TerraWeaveForestScene> terraForest_;
    std::unique_ptr<terraweave::TerraWeaveHorizonScene> terraHorizon_;
    bool terraForestReady_=false;
    std::vector<terraweave::RectCm> vegetationClearings_;
    std::vector<terraweave::RectCm> previousVegetationClearings_;
    bool vegetationClearingsDirty_=false;
    // Published only on owner update thread; captured const snapshot for workers.
    std::shared_ptr<const terraweave::EarthworksLayer> vegetationTerrain_;
    std::vector<terraweave::RectCm> vegetationTerrainEdited_;
    bool vegetationTerrainDirty_=false;
    std::set<Cell> replacedTerrainTiles_;
    terraweave::CanopyPhenology vegetationPhenology_{};
    bool vegetationPhenologyDirty_=false;
    terraweave::ForestWindState vegetationWind_{};
    std::shared_ptr<const terraweave::ForestWindField> vegetationWindField_;
    uint64_t vegetationPresentationTick_=0;
    bool vegetationHorizonSpatialWind_=true;
    bool vegetationHorizonExactWindField_=true;
    terraweave::HorizonWindUploadPolicy vegetationHorizonWindUploadPolicy_{};
    terraweave::HorizonWindShadowCachePolicy vegetationHorizonWindShadowCachePolicy_{};
    terraweave::HorizonAdaptiveQualityConfig vegetationHorizonAdaptiveConfig_{};
    float overhang_=0;                                        // "meshOverhang": units a mesh may reach past its chunk
    void releaseChunk(gfx::Device&,Prepared&);
    // Candidate registry retains one shared_ptr per GPU Model, including
    // after its last render chunk disappears. Weak worker cache entries are
    // invalidated atomically only when the final candidate is enqueued.
    std::map<render::Model*,std::shared_ptr<render::Model>> deferredModels_;
    render::AsyncResourceRetirementQueue<std::shared_ptr<render::Model>> retiredModels_;
    size_t uploadBytes_=16*1024*1024;
    std::map<Cell,std::shared_ptr<Prepared>> resident_;
    std::unique_ptr<AsyncChunkStream> stream_;
    NereidWaterScene nereidWater_;
    NereidFloodVisualScene floodVisual_;
    std::vector<NereidSceneFlowObstruction> nereidFlowObstructions_;
    NereidCascadeScene cascades_;
    NereidConfluenceScene confluences_;
    NereidRippleField ripples_;
    NereidVisualWindField windField_{};
    std::shared_ptr<Prepared> prepare(Cell,const AssetReader&);
};
}
