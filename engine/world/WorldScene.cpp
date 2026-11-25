#include "WorldScene.h"
#include "engine/buildings/BuildingRenderer3DScene.h"
#include "TerraWeaveForestManifest.h"
#include "TerraWeaveHorizonManifest.h"
#include "TerraWeaveWorldHeightSource.h"
#include "engine/render/ModelTextureCache.h"
#include "engine/world/NereidWaterManifest.h"
#include "engine/world/NereidCascadeManifest.h"
#include "engine/world/NereidConfluenceManifest.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace aiverse::world {
namespace {
render::float3 visual(Vec3 v){return {float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};}
size_t modelBytes(const render::Model& m) {
    // CPU + GPU + node/material overhead, including mip chains and upload staging allowance.
    size_t bytes=4096+m.vertices.capacity()*sizeof(render::ModelVertex)*3+m.indices.capacity()*sizeof(uint32_t)*3;
    bytes+=m.nodes.size()*512+m.materials.size()*1024;
    for(const auto& image:m.images){if(image.width<0||image.height<0||image.width>8192||image.height>8192)throw JsonError("world.mesh: image exceeds 8192");bytes+=size_t(image.width)*size_t(image.height)*16+image.rgba.capacity();}
    return bytes;
}
std::unique_ptr<render::Model> terrainModel(const ChunkCollision& collision,const Json& definition,const AssetReader& reader,Vec3 origin) {
    if(!collision.terrain.resolution)return {};
    // The tail contains terrain triangles in the exact collision diagonal/order.
    const size_t count=size_t(collision.terrain.resolution-1)*size_t(collision.terrain.resolution-1)*2;
    auto model=std::make_unique<render::Model>();model->path="world:terrain";
    render::Material material;material.baseColor={.22f,.38f,.16f,1};material.doubleSided=true;
    const auto& authored=definition["material"];
    if(authored.has("color")){auto rgb=visual(worldVector(authored["color"],1_fx));if(rgb.x<0||rgb.y<0||rgb.z<0)throw JsonError("world.terrain.material: negative color");material.baseColor={rgb.x,rgb.y,rgb.z,1};}
    auto texture=[&](const char* key,int& index){if(!authored.has(key))return;auto file=authored.getStr(key);std::string bytes;
        if(!safeAssetPath(file)||!reader(file,bytes))throw JsonError("world.terrain: missing or corrupt texture");
        render::Image image;image.name=file;if(!render::decodeImage(bytes,image))throw JsonError("world.terrain: unsupported texture");
        image.repeat=true;index=int(model->images.size());model->images.push_back(std::move(image));};
    texture("baseTexture",material.baseTex);texture("normalTexture",material.normalTex);
    model->materials.push_back(material);
    const auto uv=definition.getFx("uvScale",Fixed::fromRatio(1,20));
    if(uv<=0_fx||uv>16_fx)throw JsonError("world.terrain.uvScale: expected 0..16");
    const float uvScale=float(uv.toDouble());
    const float uOffset=float(Fixed::fromRaw((origin.x*uv).raw%Fixed::kOne).toDouble());
    const float vOffset=float(Fixed::fromRaw((origin.z*uv).raw%Fixed::kOne).toDouble());
    render::Node node;node.mesh=0;model->nodes.push_back(node);model->meshes.emplace_back();
    for(size_t i=collision.triangles.size()-count;i<collision.triangles.size();++i){const auto& t=collision.triangles[i];
        auto a=visual(t.a),b=visual(t.b),c=visual(t.c);auto normal=render::normalize(render::cross(b-a,c-a));
        for(auto p:{a,b,c}){render::ModelVertex v;v.pos=p;v.nrm=normal;v.uv={p.x*uvScale+uOffset,p.z*uvScale+vOffset};model->indices.push_back(uint32_t(model->vertices.size()));model->vertices.push_back(v);}}
    render::Primitive p;p.indexCount=uint32_t(model->indices.size());p.vertexCount=uint32_t(model->vertices.size());model->meshes[0].primitives.push_back(p);model->finalize();return model;
}
}
// Models shared between chunks ("shareModels": true): a city built from a kit repeats the same files in every chunk,
// so each file is decoded and uploaded once and lives while any resident or pending chunk uses it. Shared models are
// outside the per-chunk reservation: their total is bounded by the number of distinct files of the content.
struct WorldScene::ModelCache { std::mutex mutex;std::map<std::string,std::weak_ptr<render::Model>> models; };
WorldScene::WorldScene(const Json& manifest,AssetReader reader,std::shared_ptr<render::ModelTextureCache> textures):assets_(WorldAssets::parse(manifest)),textures_(std::move(textures)),cache_(std::make_shared<ModelCache>()) {
    shareModels_=manifest.getBool("shareModels");
    overhang_=float(worldInteger(manifest,"meshOverhang",0,0,4096));
    uploadBytes_=size_t(worldInteger(manifest,"maxUploadBytes",16*1024*1024,1024,1024*1024*1024));
    // Static opt-in water snapshots are presentation-only; native ECS hosts
    // can later override them via setNereidSurfaces().
    const auto water=parseNereidWaterManifest(manifest);
    nereidWater_.setMaxUploadsPerFrame(water.maxUploadsPerFrame);
    nereidWater_.setAsyncPreparation(water.asyncPrepare);
    nereidWater_.setMaxWaterStagingBytes(water.maxStagingBytes);
    nereidWater_.setMaxEstimatedUploadBytes(water.maxEstimatedUploadBytes);
    nereidWater_.setVisibilityRange(water.visibilityRange);
    if(manifest.has("nereidWater"))nereidWater_.setSurfaces(water.surfaces);
    const auto cascade=parseNereidCascadeManifest(manifest);
    cascades_.setMaxUploadsPerFrame(cascade.maxUploadsPerFrame);
    if(manifest.has("nereidCascades"))cascades_.setCascades(cascade.sources);
    // Static confluences use explicit scene water IDs, not manufactured ECS
    // Entity handles. Parse all links before accepting a partial scene.
    const auto confluence=parseNereidConfluenceManifest(manifest,water.surfaces);
    if(manifest.has("nereidConfluences"))
        confluences_.setConfluences(confluence.sources);
    // Opt-in only. The existing WorldScene codepath is identical when the
    // manifest has no terraweaveForest block.
    terraForest_=forestSceneFromManifest(manifest,reader);
    terraHorizon_=horizonSceneFromManifest(manifest,terraForest_.get(),reader);
    if(terraHorizon_) {
        // Honor optional manifest presentationQuality in the WorldScene
        // getters as well as the direct HorizonScene runtime policies.
        const auto presentation=terraHorizon_->presentationPolicy();
        vegetationHorizonSpatialWind_=presentation.spatialWindVariation;
        vegetationHorizonExactWindField_=presentation.exactSpatialWindField;
        vegetationHorizonWindUploadPolicy_=presentation.windUpload;
        vegetationHorizonWindShadowCachePolicy_=presentation.windShadowCache;
        vegetationHorizonAdaptiveConfig_=terraHorizon_->adaptiveQualityConfig();
    }
    auto verified=assets_.verifiedReader(reader);
    stream_=std::make_unique<AsyncChunkStream>(assets_.limits,assets_.stagingBytes,assets_.maxJobs,[this,verified](Cell cell){return prepare(cell,verified);});
    for(const auto& [cell,asset]:assets_.chunks){if(asset.bytes>uploadBytes_)throw JsonError("world.chunk: reservation exceeds maxUploadBytes");if(!stream_->add(cell,asset.bytes))throw JsonError("world.chunk: staging budget too small");}
}
WorldScene::~WorldScene()=default;
void WorldScene::setNereidSurfaces(std::vector<NereidSceneSurface> surfaces) {
    nereidWater_.setSurfaces(std::move(surfaces));
}
void WorldScene::setNereidFlowObstructions(
        std::vector<NereidSceneFlowObstruction> obstructions) {
    if(obstructions.size()>lib::nereid::kMaxWaterFlowObstructions)
        throw std::length_error("WorldScene NEREID visual obstacle budget");
    for(const auto& source:obstructions)
        if(!source.sceneWaterId || !source.effect.sourceKey ||
           !source.effect.enabled ||
           !lib::nereid::validWaterFlowObstruction(source.effect))
            throw std::invalid_argument(
                "WorldScene invalid NEREID water obstacle snapshot");
    std::sort(obstructions.begin(),obstructions.end(),
        [](const auto& a,const auto& b){
            return a.sceneWaterId==b.sceneWaterId ?
                a.effect.sourceKey<b.effect.sourceKey :
                a.sceneWaterId<b.sceneWaterId;
        });
    const auto duplicated=std::adjacent_find(
        obstructions.begin(),obstructions.end(),
        [](const auto& a,const auto& b){
            return a.sceneWaterId==b.sceneWaterId &&
                a.effect.sourceKey==b.effect.sourceKey;
        });
    if(duplicated!=obstructions.end())
        throw std::invalid_argument(
            "WorldScene duplicate NEREID scene water obstacle");
    nereidFlowObstructions_.swap(obstructions);
}
size_t WorldScene::nereidVisibleSurfaceCount()const {
    return nereidWater_.visibleSurfaceCount();
}
NereidUnderwaterView WorldScene::nereidUnderwaterAt(Vec3 camera)const {
    return sampleNereidUnderwater(nereidWater_,camera);
}

void WorldScene::setVegetationRootStates(
        std::vector<terraweave::RootState> states) {
    if(!terraForest_)
        throw std::logic_error("WorldScene root states require enabled terraweaveForest");
    terraForest_->setRootStates(states);
}
size_t WorldScene::vegetationRootPrototypeCount()const {
    return terraForest_?terraForest_->generatedRootPrototypes():0u;
}

// Publish an immutable post-ECS Earthworks snapshot. This only affects
// presentation source data; terrain triangles, collision and navmesh still
// require explicit native TerrainMutationCoordinator publication.
void WorldScene::setVegetationTerrainSnapshot(
        std::shared_ptr<const terraweave::EarthworksLayer> soil,
        std::vector<terraweave::RectCm> edited) {
    if(edited.size()>128u)
        throw std::length_error("TerraWeave terrain snapshot edit budget");
    for(const auto& r:edited)
        if(r.minX>=r.maxX || r.minZ>=r.maxZ ||
           r.minX<-terraweave::kWorldLimitCm ||
           r.minZ<-terraweave::kWorldLimitCm ||
           r.maxX>terraweave::kWorldLimitCm ||
           r.maxZ>terraweave::kWorldLimitCm)
            throw std::invalid_argument("TerraWeave invalid terrain edit bounds");
    if(soil==vegetationTerrain_ && edited.empty())return;
    vegetationTerrain_=std::move(soil);
    vegetationTerrainEdited_=std::move(edited);
    vegetationTerrainDirty_=true;
}

void WorldScene::setReplacedTerrainTiles(
        std::vector<Cell> residentReplacementCells) {
    if(residentReplacementCells.size()>512u)
        throw std::length_error("TerraWeave replacement tile budget");
    std::set<Cell> next;
    for(const auto cell:residentReplacementCells) {
        if(cell.y!=0)
            throw std::invalid_argument("TerraWeave replacement cell must be X/Z column");
        next.insert(cell);
    }
    replacedTerrainTiles_.swap(next);
}

// A caller-owned, serializable set of runtime clearings shared by both
// Flora instances and distant Horizon canopy silhouettes. Only the first
// state and the latest replacement matter until the next WorldScene update.
void WorldScene::setVegetationClearings(
        std::vector<terraweave::RectCm> clearings) {
    if(clearings.size()>4096)
        throw std::length_error("TerraWeave forest clearing budget");
    for(const auto& r:clearings) {
        if(r.minX>=r.maxX || r.minZ>=r.maxZ ||
           r.minX<-terraweave::kWorldLimitCm ||
           r.minZ<-terraweave::kWorldLimitCm ||
           r.maxX>terraweave::kWorldLimitCm ||
           r.maxZ>terraweave::kWorldLimitCm)
            throw std::invalid_argument("TerraWeave forest clearing rectangle");
    }
    const auto less=[](const terraweave::RectCm& a,
                       const terraweave::RectCm& b) {
        if(a.minZ!=b.minZ)return a.minZ<b.minZ;
        if(a.minX!=b.minX)return a.minX<b.minX;
        if(a.maxZ!=b.maxZ)return a.maxZ<b.maxZ;
        return a.maxX<b.maxX;
    };
    const auto same=[](const terraweave::RectCm& a,
                       const terraweave::RectCm& b) {
        return a.minX==b.minX && a.minZ==b.minZ &&
               a.maxX==b.maxX && a.maxZ==b.maxZ;
    };
    std::sort(clearings.begin(),clearings.end(),less);
    clearings.erase(std::unique(clearings.begin(),clearings.end(),same),
                    clearings.end());
    if(clearings.size()==vegetationClearings_.size() &&
       std::equal(clearings.begin(),clearings.end(),
                  vegetationClearings_.begin(),same))return;
    if(!vegetationClearingsDirty_)
        previousVegetationClearings_=vegetationClearings_;
    vegetationClearings_=std::move(clearings);
    vegetationClearingsDirty_=true;
}

void WorldScene::setVegetationPhenology(terraweave::CanopyPhenology state) {
    terraweave::validateCanopyPhenology(state);
    if(state==vegetationPhenology_)return;
    vegetationPhenology_=state;
    // Near foliage changes materials in place, without respawning stable
    // foliage keys or invalidating asynchronous scatter workers.
    if(terraForest_)terraForest_->setPhenology(state);
    // Horizon atlas colour/coverage requires a worker rebake, on the next
    // owner-thread WorldScene update. No mutable worker callback captures.
    vegetationPhenologyDirty_=bool(terraHorizon_);
}
void WorldScene::setVegetationWind(terraweave::ForestWindState wind) {
    terraweave::validateForestWind(wind);
    if(wind==vegetationWind_)return;
    vegetationWind_=wind;
    // Pure presentation: no near re-scatter, no Horizon atlas rebuild.
    if(terraForest_)terraForest_->setWind(wind);
    if(terraHorizon_)terraHorizon_->setWind(wind);
}
void WorldScene::setVegetationWindField(
        std::shared_ptr<const terraweave::ForestWindField> field) {
    if(field==vegetationWindField_)return;
    if(field)field->validate(); // fail without changing the live snapshot
    if(terraForest_)terraForest_->setWindField(field);
    if(terraHorizon_)terraHorizon_->setWindField(field);
    vegetationWindField_=std::move(field);
}
void WorldScene::setVegetationPresentationTick(uint64_t tick) {
    vegetationPresentationTick_=tick;
    if(terraForest_)terraForest_->setPresentationTick(tick);
    if(terraHorizon_)terraHorizon_->setPresentationTick(tick);
}
void WorldScene::setVegetationHorizonSpatialWind(bool enabled) {
    vegetationHorizonSpatialWind_=enabled;
    if(terraHorizon_)terraHorizon_->setSpatialWindVariation(enabled);
    vegetationHorizonAdaptiveConfig_.enabled=false;
}
void WorldScene::setVegetationHorizonExactWindField(bool enabled) {
    vegetationHorizonExactWindField_=enabled;
    if(terraHorizon_)terraHorizon_->setExactSpatialWindField(enabled);
    vegetationHorizonAdaptiveConfig_.enabled=false;
}
void WorldScene::setVegetationHorizonWindUploadPolicy(
        terraweave::HorizonWindUploadPolicy policy) {
    terraweave::validateHorizonWindUploadPolicy(policy);
    if(terraHorizon_)terraHorizon_->setWindUploadPolicy(policy);
    vegetationHorizonWindUploadPolicy_=policy;
    vegetationHorizonAdaptiveConfig_.enabled=false;
}
void WorldScene::setVegetationHorizonWindShadowCachePolicy(
        terraweave::HorizonWindShadowCachePolicy policy) {
    terraweave::validateHorizonWindShadowCachePolicy(policy);
    if(terraHorizon_)terraHorizon_->setWindShadowCachePolicy(policy);
    vegetationHorizonWindShadowCachePolicy_=policy;
    vegetationHorizonAdaptiveConfig_.enabled=false;
}
void WorldScene::setVegetationHorizonPresentationPolicy(
        terraweave::HorizonPresentationPolicy policy) {
    // Validate the entire bundle before modifying *any* live policy;
    // all four existing setters are safe and owner-thread-only.
    terraweave::validateHorizonPresentationPolicy(policy);
    if(terraHorizon_)terraHorizon_->setPresentationPolicy(policy);
    vegetationHorizonWindUploadPolicy_=policy.windUpload;
    vegetationHorizonWindShadowCachePolicy_=policy.windShadowCache;
    vegetationHorizonSpatialWind_=policy.spatialWindVariation;
    vegetationHorizonExactWindField_=policy.exactSpatialWindField;
    vegetationHorizonAdaptiveConfig_.enabled=false;
}
void WorldScene::setVegetationHorizonPresentationQualityPreset(
        terraweave::HorizonPresentationQualityPreset preset) {
    setVegetationHorizonPresentationPolicy(
        terraweave::horizonPresentationPolicyForQuality(preset));
}
void WorldScene::setVegetationHorizonAdaptiveQualityConfig(
        terraweave::HorizonAdaptiveQualityConfig config) {
    terraweave::validateHorizonAdaptiveQualityConfig(config);
    if(terraHorizon_)terraHorizon_->setAdaptiveQualityConfig(config);
    vegetationHorizonAdaptiveConfig_=config;
    if(terraHorizon_ && config.enabled) {
        // Enabling selects its declared initial Stage39 profile now.
        const auto current=terraHorizon_->presentationPolicy();
        vegetationHorizonWindUploadPolicy_=current.windUpload;
        vegetationHorizonWindShadowCachePolicy_=current.windShadowCache;
        vegetationHorizonSpatialWind_=current.spatialWindVariation;
        vegetationHorizonExactWindField_=current.exactSpatialWindField;
    }
}
terraweave::HorizonAdaptiveQualityState
WorldScene::vegetationHorizonAdaptiveQualityState() const {
    return terraHorizon_?terraHorizon_->adaptiveQualityState():
           terraweave::initialHorizonAdaptiveQualityState(
               vegetationHorizonAdaptiveConfig_);
}
bool WorldScene::submitVegetationHorizonFrameTiming(
        terraweave::HorizonFrameTimingSample sample) {
    if(!terraHorizon_)return false;
    const auto changed=terraHorizon_->submitAdaptiveFrameTiming(sample);
    if(!changed)return false;
    // Keep the WorldScene public getters coherent with internal automatic
    // Stage39 switches; never copy stale manifest/manual settings.
    const auto current=terraHorizon_->presentationPolicy();
    vegetationHorizonWindUploadPolicy_=current.windUpload;
    vegetationHorizonWindShadowCachePolicy_=current.windShadowCache;
    vegetationHorizonSpatialWind_=current.spatialWindVariation;
    vegetationHorizonExactWindField_=current.exactSpatialWindField;
    return true;
}
std::shared_ptr<WorldScene::Prepared> WorldScene::prepare(Cell cell,const AssetReader& reader) {
    const auto& asset=assets_.chunks.at(cell);std::string text;
    if(!reader(asset.file,text))throw JsonError("world.scene: read/hash failure: "+asset.file);
    auto json=Json::parse(text);auto collision=readChunkCollision(json,assets_.cellSize);
    auto out=std::make_shared<Prepared>();size_t bytes=4096;
    if(terraForest_ || terraHorizon_){
        out->terrainResolution=uint32_t(collision.terrain.resolution);
        out->terrainHeightsCm.reserve(collision.terrain.heights.size());
        for(const Fixed& height:collision.terrain.heights){
            out->terrainHeightsCm.push_back(
                static_cast<int32_t>(std::llround(height.toDouble()*100.0)));
        }
        out->placementBoxes=collision.boxes;
        // Authored source snapshots are resident CPU data only in explicit
        // forest-enabled worlds; charge them to the chunk reservation.
        bytes+=out->terrainHeightsCm.size()*sizeof(int32_t)+
               out->placementBoxes.size()*sizeof(Bounds);
        if(bytes>asset.bytes)throw JsonError(
            "world.terraweaveForest: chunk reservation too small for terrain snapshot");
    }
    auto append=[&](std::shared_ptr<render::Model> model,const std::string& sharedKey=std::string(),bool charge=true,bool fresh=true){
        // a model reused from the cache was checked when it was decoded (and its CPU arrays are gone after the upload)
        if(!fresh){bytes+=4096;if(bytes>asset.bytes)throw JsonError("world.scene: reservedBytes too small");out->models.push_back(std::move(model));out->sharedKeys.push_back(sharedKey);return out->models.size()-1;}
        render::prepareModelUpload(*model);
        if(model->vertices.size()>1000000||model->indices.size()>3000000||!model->skins.empty()||!model->clips.empty())throw JsonError("world.mesh: expected bounded rigid model");
        for(const auto& mesh:model->meshes)for(const auto& p:mesh.primitives)if(p.morphTargets)throw JsonError("world.mesh: morphs require an entity");
        if(model->vertices.empty()||model->indices.empty())throw JsonError("world.mesh: empty model");
        bytes+=charge?modelBytes(*model):size_t(4096);if(bytes>asset.bytes)throw JsonError("world.scene: reservedBytes too small");
        out->models.push_back(std::move(model));out->sharedKeys.push_back(sharedKey);return out->models.size()-1;
    };
    if(auto terrain=terrainModel(collision,json["terrain"],reader,assets_.origin(cell))){auto index=append(std::move(terrain));out->instances.push_back({index,{}});}
    if(json.has("meshes")) {
        if(!json["meshes"].isArray()||json["meshes"].size()>256)throw JsonError("world.meshes: expected at most 256 instances");
        std::map<std::string,size_t> indices;
        for(const auto& instance:json["meshes"].items()){
            auto file=instance.getStr("file");if(!safeAssetPath(file))throw JsonError("world.mesh: invalid path");
            if(!indices.contains(file)&&shareModels_){
                std::shared_ptr<render::Model> shared;
                {std::lock_guard lock(cache_->mutex);if(auto it=cache_->models.find(file);it!=cache_->models.end())shared=it->second.lock();}
                if(shared)indices[file]=append(std::move(shared),file,false,false);
            }
            if(!indices.contains(file)){
                bool missing=false;
                auto checked=[&](const std::string& path,std::string& bytes){
                    const bool optional=(path==file+".lmuv" || path==file+".clips.json") && !assets_.hashes.contains(path);
                    if(optional)return false;
                    bool ok=reader(path,bytes);missing|=!ok;return ok;
                };
                auto model=render::loadModel(file,checked,nullptr,textures_);
                if(missing)throw JsonError("world.mesh: missing or corrupt dependency: "+file);
                render::prepareModelUpload(*model); // finish CPU mutation before publishing to other chunk workers
                std::shared_ptr<render::Model> owned=std::move(model);
                if(shareModels_){std::lock_guard lock(cache_->mutex);cache_->models[file]=owned;}
                indices[file]=append(std::move(owned),shareModels_?file:std::string(),!shareModels_);
            }
            auto position=instance.has("position")?visual(worldVector(instance["position"],Fixed::fromInt(assets_.cellSize))):render::float3{};
            auto scale=instance.has("scale")?visual(worldVector(instance["scale"],100_fx)):render::float3{1,1,1};
            if(scale.x<=0||scale.y<=0||scale.z<=0)throw JsonError("world.mesh: scale must be positive");
            auto rotation=instance.has("rotation")?visual(worldVector(instance["rotation"],360_fx)):render::float3{};
            constexpr float deg=.0174532925199433f;
            auto transform=render::mat4::translate(position)*render::mat4::rotateY(rotation.y*deg)*render::mat4::rotateX(rotation.x*deg)*render::mat4::rotateZ(rotation.z*deg)*render::mat4::scale(scale);
            const auto& model=*out->models[indices.at(file)];
            for(int corner=0;corner<8;++corner){auto a=model.boundsMin,b=model.boundsMax;
                auto p=transform.transformPoint({corner&1?b.x:a.x,corner&2?b.y:a.y,corner&4?b.z:a.z});
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||p.x<-overhang_||p.z<-overhang_||p.x>assets_.cellSize+overhang_||p.z>assets_.cellSize+overhang_||std::abs(p.y)>10000)
                    throw JsonError("world.mesh: transformed bounds must fit owning X/Z cell");
            }
            out->instances.push_back({indices.at(file),transform});
        }
    }
    return out;
}
// A cell eviction stops all new draws of its Models but may race with
// already submitted GPU work AND async CPU workers borrowing shared Models.
// Retain ONE manager-owned shared_ptr per unique Model until every other
// resident/worker reference has gone. Direct releaseModel here was unsafe.
void WorldScene::releaseChunk(gfx::Device& device,Prepared& chunk) {
    (void)device; // the GPU is touched only by collectRetired / shutdown
    for(auto& model:chunk.models) {
        if(!model)continue;
        deferredModels_.try_emplace(model.get(),model);
        model.reset();
    }
}

// Run once at the owning Device frame boundary, before this frame's new
// submissions. No texture/model GPU resources are destroyed before a
// real completion token or verified full-idle fallback.
bool WorldScene::collectRetired(gfx::Device& device) {
    try {
    for(auto it=deferredModels_.begin();it!=deferredModels_.end();) {
        bool finalCpuOwner=false;
        {
            // A worker can lock the weak shared-model cache concurrently.
            // Serialize final-owner check with its weak.lock(), and erase
            // only the weak entry for THIS Model (a newer import may have
            // already replaced the same source file's weak cache entry).
            std::lock_guard lock(cache_->mutex);
            if(it->second.use_count()==1) {
                for(auto weak=cache_->models.begin();weak!=cache_->models.end();) {
                    auto locked=weak->second.lock();
                    if(!locked || locked.get()==it->first)
                        weak=cache_->models.erase(weak);
                    else ++weak;
                }
                // A late worker cannot acquire this candidate after erase.
                finalCpuOwner=true;
            }
        }
        if(finalCpuOwner) {
            retiredModels_.enqueueFrom(it->second);
            it=deferredModels_.erase(it);
        }else ++it;
    }
    if(retiredModels_.empty())return deferredModels_.empty();
    return retiredModels_.collect(
        [&]{return device.beginResourceRetirement();},
        [&](uint64_t token){return device.resourceRetirementComplete(token);},
        [&](uint64_t token){
            if(device.resourceRetirementStatus(token)==
               gfx::ResourceRetirementStatus::Complete)
                device.endResourceRetirement(token);
            else
                device.abandonRetirementTokenAfterIdle(token);
        },
        [&]{
            // Unsupported async-token backends should not stall for every
            // streaming-cell eviction. A bounded-count pressure point or
            // explicit shutdown authorizes one full-device idle fallback.
            return retiredModels_.pending()>=128 &&
                   device.waitForResourceIdle();
        },
        [&](std::shared_ptr<render::Model>& model) {
            if(model)render::releaseModel(device,*model);
            model.reset();
        },
        [&](uint64_t token) {
            return device.resourceRetirementStatus(token)==
                   gfx::ResourceRetirementStatus::Failed;
        });
    }catch(...) {
        // Failed queue allocation or Device callback cannot authorize GPU
        // destruction. Keep the remaining Models for a later owner frame.
        return false;
    }
}
void WorldScene::update(gfx::Device& device,std::span<const Vec3> observers,int radius) {
    bool landscapeChanged=false;
    std::vector<terraweave::RectCm> editedLandscape;
    const auto markLandscapeCell=[&](Cell cell) {
        const int64_t span=int64_t(assets_.cellSize)*100;
        const int64_t minX=int64_t(cell.x)*span;
        const int64_t minZ=int64_t(cell.z)*span;
        const int64_t maxX=minX+span,maxZ=minZ+span;
        const int64_t limit=terraweave::kWorldLimitCm;
        if(maxX<=-limit||maxZ<=-limit||minX>=limit||minZ>=limit)return;
        editedLandscape.push_back({
            std::max(minX,-limit),std::max(minZ,-limit),
            std::min(maxX,limit),std::min(maxZ,limit)});
    };
    std::vector<StreamObserver> interests;for(auto p:observers)interests.push_back({assets_.cell(p),radius});
    stream_->update(interests,{},4,2,[&](Cell cell,const AsyncChunkStream::Payload& payload){
        auto prepared=std::static_pointer_cast<Prepared>(payload);
        try {
            for(auto& model:prepared->models){
                if(model->vb.valid()&&model->ib.valid())continue;   // shared with a resident chunk: already on the GPU
                if(!render::uploadModel(device,*model)||!model->vb.valid()||!model->ib.valid())throw JsonError("world.mesh: GPU buffer upload failed");
                for(auto& image:model->images)if(!image.tex.valid())throw JsonError("world.mesh: GPU texture upload failed");
            }
            resident_[cell]=prepared;
            if((terraForest_ || terraHorizon_) &&
               (prepared->terrainResolution || !prepared->placementBoxes.empty())) {
                landscapeChanged=true;
                markLandscapeCell(cell);
            }
            return true;
        }catch(...){releaseChunk(device,*prepared);throw;}
    },[&](Cell cell){
        auto it=resident_.find(cell);
        if(it==resident_.end())return;
        if((terraForest_ || terraHorizon_) &&
           (it->second->terrainResolution || !it->second->placementBoxes.empty())) {
            landscapeChanged=true;
            markLandscapeCell(cell);
        }
        releaseChunk(device,*it->second);
        resident_.erase(it);
    },uploadBytes_);
    if((terraForest_ || terraHorizon_) &&
       (landscapeChanged || vegetationClearingsDirty_ ||
        vegetationPhenologyDirty_ || vegetationTerrainDirty_)){
        // Cleared patches may extend far beyond resident collision tiles.
        // Include BOTH old and new rectangles so an old canopy silhouette
        // is invalidated when a clearing is restored or moved.
        if(vegetationClearingsDirty_) {
            editedLandscape.insert(editedLandscape.end(),
                previousVegetationClearings_.begin(),
                previousVegetationClearings_.end());
            editedLandscape.insert(editedLandscape.end(),
                vegetationClearings_.begin(),vegetationClearings_.end());
        }
        if(vegetationTerrainDirty_)
            editedLandscape.insert(editedLandscape.end(),
                vegetationTerrainEdited_.begin(),vegetationTerrainEdited_.end());
        // Publish a single immutable snapshot for both terrain heights and
        // obstacle footprints. Async foliage jobs keep old snapshots alive
        // until canceled; revision checks discard their stale results.
        // A phenology-only change (season / growth) keeps the resident tiles,
        // clearings and soil: reuse the last height/obstacle snapshot instead
        // of resampling every resident tile.
        const bool phenologyOnly=vegetationPhenologyDirty_ && !landscapeChanged &&
            !vegetationClearingsDirty_ && !vegetationTerrainDirty_ &&
            lastVegetationHeights_;
        auto mutableHeights=phenologyOnly ? nullptr :
            std::make_shared<terraweave::AuthoredWorldHeightSnapshot>(assets_.cellSize);
        auto mutableMask=phenologyOnly ? nullptr :
            std::make_shared<terraweave::FoliageExclusionMask>();
        size_t boxes=0;
        if(!phenologyOnly) {
        // The same immutable exclusion mask is captured by near forest
        // workers and Horizon's CPU-only canopy baker. No live callbacks
        // inspect mutable gameplay state from a worker thread.
        for(const auto& r:vegetationClearings_){
            mutableMask->addBuilding(r);
            ++boxes;
        }
        for(const auto& [cell,prepared]:resident_){
            if(prepared->terrainResolution){
                terraweave::AuthoredHeightTileCm tile;
                tile.resolution=prepared->terrainResolution;
                tile.heightsCm=prepared->terrainHeightsCm;
                if(vegetationTerrain_) {
                    // Resample the published immutable Earthworks snapshot at
                    // the exact Fixed-grid X/Z positions used by collision.
                    // Never query mutable gameplay soil from a render worker.
                    const int64_t baseX=int64_t(cell.x)*assets_.cellSize*100;
                    const int64_t baseZ=int64_t(cell.z)*assets_.cellSize*100;
                    const int64_t den=int64_t(tile.resolution)-1;
                    const auto roundedCm=[](Fixed v)->int64_t{
                        const int64_t n=v.raw*100;
                        return n>=0 ? (n+Fixed::kOne/2)/Fixed::kOne :
                                      (n-Fixed::kOne/2)/Fixed::kOne;
                    };
                    for(uint32_t z=0;z<tile.resolution;++z)
                        for(uint32_t x=0;x<tile.resolution;++x){
                            const auto sx=baseX+roundedCm(Fixed::fromRatio(
                                int64_t(x)*assets_.cellSize,den));
                            const auto sz=baseZ+roundedCm(Fixed::fromRatio(
                                int64_t(z)*assets_.cellSize,den));
                            const int64_t limit=terraweave::kWorldLimitCm-
                                vegetationTerrain_->spacingCm();
                            if(sx<=-limit || sz<=-limit ||
                               sx>=limit || sz>=limit)
                                throw std::out_of_range(
                                    "TerraWeave soil snapshot outside authored tile extent");
                            tile.heightsCm[size_t(z)*tile.resolution+x]=
                                vegetationTerrain_->sampleHeightCm(sx,sz);
                        }
                }
                mutableHeights->setTile(cell,std::move(tile));
            }
            const int64_t baseX=int64_t(cell.x)*assets_.cellSize*100;
            const int64_t baseZ=int64_t(cell.z)*assets_.cellSize*100;
            for(const Bounds& box:prepared->placementBoxes){
                const int64_t x0=baseX+static_cast<int64_t>(
                    std::floor(box.min.x.toDouble()*100.0));
                const int64_t x1=baseX+static_cast<int64_t>(
                    std::ceil(box.max.x.toDouble()*100.0));
                const int64_t z0=baseZ+static_cast<int64_t>(
                    std::floor(box.min.z.toDouble()*100.0));
                const int64_t z1=baseZ+static_cast<int64_t>(
                    std::ceil(box.max.z.toDouble()*100.0));
                if(x1<=-terraweave::kWorldLimitCm ||
                   x0>=terraweave::kWorldLimitCm ||
                   z1<=-terraweave::kWorldLimitCm ||
                   z0>=terraweave::kWorldLimitCm)continue;
                const terraweave::RectCm footprint{
                    std::max(x0,-terraweave::kWorldLimitCm),
                    std::max(z0,-terraweave::kWorldLimitCm),
                    std::min(x1,terraweave::kWorldLimitCm),
                    std::min(z1,terraweave::kWorldLimitCm)};
                if(footprint.maxX>footprint.minX &&
                   footprint.maxZ>footprint.minZ){
                    mutableMask->addBuilding(footprint);
                    ++boxes;
                }
            }
        }
        }
        std::shared_ptr<const terraweave::AuthoredWorldHeightSnapshot> heights =
            std::move(mutableHeights);
        std::shared_ptr<const terraweave::FoliageExclusionMask> mask;
        if(boxes)mask=std::move(mutableMask);
        if(phenologyOnly) {heights=lastVegetationHeights_;mask=lastVegetationMask_;}
        lastVegetationHeights_=heights;lastVegetationMask_=mask;
        if(terraForest_ &&
           (landscapeChanged || vegetationClearingsDirty_ ||
            vegetationTerrainDirty_)){
            const terraweave::TerrainSettings fallback=
                terraForest_->settings().terrain;
            const auto soil=vegetationTerrain_;
            terraForest_->setLandscapeSources(
                [heights,fallback,soil](int64_t x,int64_t z)->int32_t{
                    if(soil) {
                        const int64_t edge=terraweave::kWorldLimitCm-
                            soil->spacingCm();
                        if(x>-edge && z>-edge && x<edge && z<edge)
                            return soil->sampleHeightCm(x,z);
                    }
                    if(const auto sample=heights->sample(x,z))return *sample;
                    return terraweave::heightAtCm(fallback,x,z);
                },
                mask);
        }
        if(terraHorizon_){
            // An immutable source snapshot is shared by each in-flight
            // Horizon worker. The far-ground and canopy samples now use
            // the same resident authored tiles/obstacle footprint snapshot
            // as near foliage. Unloaded regions fall back to the Horizon
            // generator's own procedural terrain seed.
            const auto fallback=terraHorizon_->terrainSettings();
            auto surface=std::make_shared<terraweave::HorizonSurfaceSources>();
            surface->phenology=vegetationPhenology_;
            const auto soil=vegetationTerrain_;
            surface->heightAtCm=[heights,fallback,soil](int64_t x,int64_t z){
                if(soil){
                    const int64_t edge=terraweave::kWorldLimitCm-
                        soil->spacingCm();
                    if(x>-edge && z>-edge && x<edge && z<edge)
                        return soil->sampleHeightCm(x,z);
                }
                if(const auto sample=heights->sample(x,z))return *sample;
                return terraweave::heightAtCm(fallback,x,z);
            };
            // An authored near terrain quad is removed from the Horizon
            // only when every intersecting near terrain tile is resident.
            // Missing tiles intentionally keep their far ground visible.
            surface->ownsNearTerrainRect=[heights](
                    int64_t x0,int64_t z0,int64_t x1,int64_t z1){
                return heights->coversRect(x0,z0,x1,z1);
            };
            surface->collectNearTerrainRects=[heights](
                    terraweave::RectCm area,
                    std::vector<terraweave::RectCm>& rectangles) {
                return heights->coveredRectsIn(area,rectangles,1024);
            };
            surface->nearGroundHeightAtCm=[heights](
                    int64_t xCm,int64_t zCm)->std::optional<int32_t>{
                return heights->sample(xCm,zCm);
            };
            surface->nearGroundHeightAtQ16Cm=[heights](
                    int64_t xQ16cm,int64_t zQ16cm)->std::optional<int32_t>{
                return heights->sampleQ16(xQ16cm,zQ16cm);
            };
            surface->nearGroundContinuousSlopeAtQ16Cm=[heights](
                    int64_t xQ16cm,int64_t zQ16cm)
                    ->std::optional<std::pair<int32_t,int32_t>>{
                return heights->continuousSlopeQ16(xQ16cm,zQ16cm);
            };
            if(mask)surface->allowsCanopyAt=[mask](int64_t x,int64_t z){
                return !mask->excludes(x,z);
            };
            terraHorizon_->setSurfaceSources(
                std::move(surface),
                std::span<const terraweave::RectCm>(
                    editedLandscape.data(),editedLandscape.size()));
        }
        vegetationClearingsDirty_=false;
        vegetationPhenologyDirty_=false;
        vegetationTerrainDirty_=false;
        vegetationTerrainEdited_.clear();
        previousVegetationClearings_.clear();
    }
    if(terraForest_){
        if(!observers.empty()) {
            const Vec3 observer=observers.front();
            const double xCm=observer.x.toDouble()*100.0;
            const double zCm=observer.z.toDouble()*100.0;
            if(!std::isfinite(xCm)||!std::isfinite(zCm)||
               std::fabs(xCm)>double(terraweave::kWorldLimitCm)||
               std::fabs(zCm)>double(terraweave::kWorldLimitCm))
                throw JsonError("world.terraweaveForest: observer outside supported extent");
            terraForest_->streamAt({
                static_cast<int64_t>(std::llround(xCm)),
                static_cast<int64_t>(std::llround(zCm))
            });
        }
        if(!terraForestReady_ || terraForest_->pendingRootUploads()) {
            // New exposed root tiers are prepared lazily after a soil
            // change. GPU buffer lifetime stays on the owning render thread.
            if(!terraForest_->uploadModels(device))
                throw JsonError("world.terraweaveForest: model GPU upload failed");
            terraForestReady_=true;
        }
    }
    if(terraHorizon_&&!observers.empty()){
        const Vec3 observer=observers.front();
        const double xCm=observer.x.toDouble()*100.0;
        const double zCm=observer.z.toDouble()*100.0;
        if(!std::isfinite(xCm)||!std::isfinite(zCm)||
           std::fabs(xCm)>double(terraweave::kWorldLimitCm)||
           std::fabs(zCm)>double(terraweave::kWorldLimitCm))
            throw JsonError("world.terraweaveHorizon: observer outside supported extent");
        // CPU region baking runs through AsyncChunkStream; update performs
        // only bounded requests and GPU publication on the render thread.
        terraHorizon_->update(device,{
            static_cast<int64_t>(std::llround(xCm)),
            static_cast<int64_t>(std::llround(zCm))});
    }
    std::function<Vec3(uint64_t,Vec3,Vec3)> resolveFlow;
    if(confluences_.count()||cascades_.count()||!nereidFlowObstructions_.empty())
        resolveFlow=[this](uint64_t stableWaterId,Vec3 position,Vec3 ordinary) {
            const Vec3 joined=confluences_.flowForWater(stableWaterId,position,ordinary,nereidWater_);
            const Vec3 current=cascades_.flowAtConnection(stableWaterId,position,joined,nereidWater_);
            return resolveNereidFlowObstructions(stableWaterId,position,current);
        };
    nereidWater_.update(device,observers.empty()?nullptr:&observers.front(),resolveFlow);
    floodVisual_.update(device);
    cascades_.update(device,&nereidWater_);
    if(textures_)textures_->trim(device);
}
render::GpuOwnedMemoryStats WorldScene::ownedGpuMemoryStats(
        const gfx::Device& device) const {
    render::GpuOwnedMemoryCounter counter(device);
    // Models can be shared by several active chunks. Count the actual
    // owned RHI handles once across all resident cells; borrowed cached
    // images and global pooled pages remain with their external owners.
    for (const auto& [cell, chunk] : resident_) {
        (void)cell;
        if (!chunk) continue;
        for (const auto& model : chunk->models)
            if (model) render::appendModelOwnedGpuMemory(*model,counter);
    }
    return counter.stats();
}

void WorldScene::draw(render::Renderer3D& renderer,Cell origin,int size) {
    std::function<Vec3(uint64_t,Vec3,Vec3)> resolveFlow;
    if(confluences_.count()||cascades_.count()||!nereidFlowObstructions_.empty())
        resolveFlow=[this](uint64_t stableWaterId,Vec3 position,Vec3 ordinary) {
            const Vec3 joined=confluences_.flowForWater(stableWaterId,position,ordinary,nereidWater_);
            const Vec3 current=cascades_.flowAtConnection(stableWaterId,position,joined,nereidWater_);
            return resolveNereidFlowObstructions(stableWaterId,position,current);
        };
    nereidWater_.draw(renderer,origin,size,&ripples_,&windField_,resolveFlow);
    cascades_.draw(renderer,origin,size);
    const int64_t floodOriginCm=int64_t(size)*100;
    floodVisual_.draw(renderer,
        int64_t(origin.x)*floodOriginCm,
        int64_t(origin.y)*floodOriginCm,
        int64_t(origin.z)*floodOriginCm);
    // Shader-normal ripples replace the old overlay rings for surfaces
    // using NEREID. The overlay remains available via ripples().draw().
    for(const auto& [cell,chunk]:resident_){
        const auto offset=render::mat4::translate(
            visual(relativeTo(assets_.origin(cell),origin,size)));
        const bool dynamicGround=replacedTerrainTiles_.contains(cell) &&
            chunk->terrainResolution!=0;
        for(const auto& instance:chunk->instances){
            // prepare() pushes the static terrain model first whenever the
            // chunk has terrain. Hide only that draw after the native host
            // published a complete dynamic terrain replacement.
            // Other authored models and water remain untouched.
            if(dynamicGround && instance.model==0)continue;
            render::ModelDraw draw;
            draw.model=chunk->models[instance.model].get();
            draw.world=offset*instance.transform;
            renderer.drawModel(draw);
        }
    }
    if(terraForest_&&terraForestReady_) {
        const int64_t factor=int64_t(size)*100;
        terraForest_->submit(renderer,{
            int64_t(origin.x)*factor, 0, int64_t(origin.z)*factor
        });
    }
    if(terraHorizon_&&terraHorizon_->ready()){
        const int64_t factor=int64_t(size)*100;
        terraHorizon_->submit(renderer,{
            int64_t(origin.x)*factor,0,int64_t(origin.z)*factor
        });
    }
    // Optional common ECS building aperture presentation. The world
    // renderer receives only a borrowed owner and never updates physics.
    buildingDrawError_.clear();
    std::set<const building::BuildingRenderer3DScene*> drawnBuildings;
    auto drawBuilding=[&](const auto& weak){
        if(auto owner=weak.lock()){
            if(!drawnBuildings.insert(owner.get()).second)return;
            std::string issue;
            if(!owner->draw(renderer,origin,size,buildingRenderElapsedMs_,&issue)
               &&buildingDrawError_.empty())
                buildingDrawError_=issue.empty()?"Building Renderer3D draw rejected":issue;
        }
    };
    drawBuilding(buildingRenderOwner_);
    for(auto it=buildingRenderOwners_.begin();it!=buildingRenderOwners_.end();){
        if(it->second.expired())it=buildingRenderOwners_.erase(it);
        else{drawBuilding(it->second);++it;}
    }

}
void WorldScene::shutdown(gfx::Device& device) {
    // Renderer is not owned here. The game/world owner must first call
    // BuildingRenderer3DScene::shutdown outside an active graphics pass,
    // then detach its authoritative session. Weak references are dropped.
    buildingRenderOwner_.reset();
    buildingRenderOwners_.clear();
    buildingDrawError_.clear();
    // Join streamed CPU preparation before releasing Model ownership.
    stream_->shutdown([&](Cell cell){
        auto it=resident_.find(cell);
        if(it!=resident_.end()){
            releaseChunk(device,*it->second);
            resident_.erase(it);
        }
    });
    if(terraForest_)terraForest_->shutdownAsync();
    // Block until all previously submitted GPU uses of streamed, native
    // and independently owned environment resources are complete.
    if(!device.waitForResourceIdle())
        throw std::runtime_error("WorldScene GPU shutdown barrier failed");
    if(!retiredModels_.drain(
            []{return true;},
            [&](uint64_t token){device.abandonRetirementTokenAfterIdle(token);},
            [&](std::shared_ptr<render::Model>& model){
                if(model)render::releaseModel(device,*model);
                model.reset();
            }))
        throw std::runtime_error("WorldScene GPU retirement drain failed");
    for(auto& [key,model]:deferredModels_) {
        (void)key;
        if(model)render::releaseModel(device,*model);
    }
    deferredModels_.clear();
    {std::lock_guard lock(cache_->mutex);cache_->models.clear();}
    nereidFlowObstructions_.clear();
    lastVegetationHeights_.reset();lastVegetationMask_.reset();
    floodVisual_.shutdown(device);
    nereidWater_.shutdown(device);
    cascades_.shutdown(device);
    ripples_.clear();
    if(terraHorizon_)terraHorizon_->shutdown(device);
    if(terraForest_&&terraForestReady_){
        terraForest_->releaseModels(device);
        terraForestReady_=false;
    }
}
}
