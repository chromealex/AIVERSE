#include <cstdlib>
#include "GeometryScene.h"
#include "StrataAutoSelect.h"
#include "Renderer3D.h"
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
#include "StrataModelAdapter.h"
#include "StrataView.h"
#include "StrataGpuScene.h"
#include "StrataGpuCulling.h"
#include "StrataRenderRouting.h"
#include "StrataPortableRender.h"
#include "StrataVulkanIndirectContract.h"
#include "StrataStreamingCoordinator.h"
#endif
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <set>
#include <chrono>
#include <tuple>
#include <utility>

namespace aiverse::render {
namespace {
float3 vector(const Json& j,float3 fallback){return j.isNull()?fallback:float3{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}
// Cross-model merging is safe for untextured materials with identical render properties.
bool sameUntextured(const Material& a,const Material& b){
    auto key=[](const Material& m){return std::make_tuple(m.shading,m.baseColor.x,m.baseColor.y,m.baseColor.z,m.baseColor.w,
        m.metallic,m.roughness,m.normalScale,m.occlusionStrength,m.emissive.x,m.emissive.y,m.emissive.z,m.emissiveStrength,
        m.alpha,m.alphaCutoff,m.doubleSided,m.vertexColors,m.castShadow,m.receiveShadow,m.transparent,m.team,m.teamStrength,
        m.uvOffset.x,m.uvOffset.y,m.uvScale.x,m.uvScale.y,m.uvRotation,m.fixedLight.x,m.fixedLight.y,m.fixedLight.z,m.fixedLight.w,m.thinTransmission,m.shadowDiffusion, m.fixedShade.x,m.fixedShade.y,
        m.sssStrength,m.sssColor.x,m.sssColor.y,m.sssColor.z,m.sssScale,m.sssThickness, m.humanProfile.x, m.humanProfile.y, m.eyeOptics.x, m.eyeOptics.y, m.eyeOptics.z, m.eyeOptics.w);};
    auto plain=[](const Material& m){return !m.shaderGraph&&m.baseTex<0&&m.mrTex<0&&m.normalTex<0&&m.occlusionTex<0&&m.emissiveTex<0&&m.teamTex<0;};
    return plain(a)&&plain(b)&&key(a)==key(b);
}
bool sameBoundMaterial(Material a,Material b,const std::vector<Image>& images){
    const int ai[]={a.baseTex,a.mrTex,a.normalTex,a.occlusionTex,a.emissiveTex,a.teamTex};
    const int bi[]={b.baseTex,b.mrTex,b.normalTex,b.occlusionTex,b.emissiveTex,b.teamTex};
    for(int k=0;k<6;++k){if((ai[k]<0)!=(bi[k]<0))return false;if(ai[k]>=0){const auto x=images[size_t(ai[k])].tex,y=images[size_t(bi[k])].tex;if(!x.valid()||!(x==y))return false;}}
    a.baseTex=a.mrTex=a.normalTex=a.occlusionTex=a.emissiveTex=a.teamTex=-1;
    b.baseTex=b.mrTex=b.normalTex=b.occlusionTex=b.emissiveTex=b.teamTex=-1;
    return sameUntextured(a,b);
}
float scaleBound(const mat4& m){float sum=0;for(int r=0;r<3;++r)for(int c=0;c<3;++c)sum+=m.at(r,c)*m.at(r,c);return std::sqrt(sum);}
}
bool GeometryScene::strataStagingSupported() const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    return true;
#else
    return false;
#endif
}

gfx::BufferHandle GeometryScene::strataGpuMetadataBuffer(const std::string& sourceKey) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuMetadata_) return {};
    const auto it = models_.find(sourceKey);
    return it == models_.end() || !it->second ? gfx::BufferHandle{} :
           strataGpuMetadata_->find(sourceKey, *it->second);
#else
    (void)sourceKey;
    return {};
#endif
}

bool GeometryScene::collectStrataGpuInstances(
    const std::string& sourceKey,
    std::vector<strata::GpuSceneInstance>& out) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataStaging_) return false;
    const auto modelIt = models_.find(sourceKey);
    const auto* asset = strataStaging_->find(sourceKey);
    if (modelIt == models_.end() || !modelIt->second || !asset ||
        asset->primitives.empty()) return false;
    const Model& source = *modelIt->second;
    if (source.bindWorld.size() != source.nodes.size() ||
        source.order.empty()) return false;
    std::vector<strata::GpuSceneInstance> instances;
    uint32_t token = 1;
    for (const Instance& sceneInstance : instances_) {
        if (sceneInstance.model != &source) continue;
        for (const int nodeId : source.order) {
            if (nodeId < 0 || size_t(nodeId) >= source.nodes.size())
                return false;
            const Node& node = source.nodes[size_t(nodeId)];
            if (node.mesh < 0) continue;
            if (size_t(node.mesh) >= source.meshes.size()) return false;
            const mat4 world = sceneInstance.world *
                               source.bindWorld[size_t(nodeId)];
            for (size_t p = 0; p < asset->primitives.size(); ++p) {
                if (asset->primitives[p].mesh != uint32_t(node.mesh)) continue;
                if (instances.size() >= 4096) return false;
                instances.push_back(strata::makeGpuSceneInstance(
                    world, uint32_t(p), token++));
            }
        }
    }
    if (instances.empty()) return false;
    out = std::move(instances);
    return true;
#else
    (void)sourceKey; (void)out;
    return false;
#endif
}

bool GeometryScene::requestStrataGpuReadback(
    gfx::Device& device, const strata::GpuSelectionSubmission& submission,
    std::function<void(strata::GpuSelectionReadback)> callback) {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuCullingEnabled_ || !strataGpuCulling_ ||
        strataGpuDevice_ != &device || !callback) return false;
    return strataGpuCulling_->requestReadback(device, submission,
                                            std::move(callback));
#else
    (void)device; (void)submission; (void)callback;
    return false;
#endif
}

bool GeometryScene::dispatchStrataGpuView(
    gfx::Device& device, const std::string& sourceKey,
    const strata::GpuPassConfig& pass, uint64_t frameSerial,
    strata::GpuSelectionSubmission& out) {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuCullingEnabled_ || !strataStagingEnabled_ ||
        !strataGpuMetadataEnabled_ || !strataGpuMetadata_ ||
        strataGpuDevice_ != &device) return false;
    const auto metadata = strataGpuMetadataBuffer(sourceKey);
    if (!metadata.valid()) return false;
    std::vector<strata::GpuSceneInstance> instances;
    if (!collectStrataGpuInstances(sourceKey, instances)) return false;
    if (!strataGpuCulling_)
        strataGpuCulling_ = std::make_shared<strata::GpuClusterCulling>();
    return strataGpuCulling_->dispatch(device, metadata, instances,
                                      pass, frameSerial, out);
#else
    (void)device; (void)sourceKey; (void)pass; (void)frameSerial; (void)out;
    return false;
#endif
}

bool GeometryScene::strataGpuIndexBinding(const std::string& sourceKey,
                                           StrataIndexBinding& out) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataGpuMetadata_) return false;
    const auto it = models_.find(sourceKey);
    if (it == models_.end() || !it->second) return false;
    strata::GpuMetadataScene::IndexBinding binding;
    if (!strataGpuMetadata_->indexBinding(sourceKey, *it->second, binding))
        return false;
    out = {binding.buffer, binding.firstIndex, binding.indexCount,
           binding.baseVertex};
    return true;
#else
    (void)sourceKey; (void)out;
    return false;
#endif
}

const strata::ModelAsset* GeometryScene::strataAsset(const std::string& sourceKey) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    return strataStaging_ ? strataStaging_->find(sourceKey) : nullptr;
#else
    (void)sourceKey;
    return nullptr;
#endif
}

bool GeometryScene::prepareStrataRenderCandidate(
    const std::string& sourceKey, uint32_t mesh, uint32_t primitive,
    strata::RenderPass pass, const strata::ViewConfig& view,
    strata::RenderCandidate& out, std::string* error) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!strataStagingEnabled_ || !strataGpuMetadataEnabled_ ||
        !strataStaging_ || !strataGpuMetadata_) {
        if (error) *error = "Strata CPU/GPU staging is disabled";
        return false;
    }
    const auto it = models_.find(sourceKey);
    const auto* asset = strataStaging_->find(sourceKey);
    if (it == models_.end() || !it->second || !asset) {
        if (error) *error = "Strata source is not staged";
        return false;
    }
    return strata::prepareRenderCandidate(*it->second, *asset,
        *strataGpuMetadata_, sourceKey, mesh, primitive, pass,
        view, out, error);
#else
    (void)sourceKey; (void)mesh; (void)primitive; (void)pass;
    (void)view; (void)out;
    if (error) *error = "Strata is disabled at build time";
    return false;
#endif
}

bool GeometryScene::planStrataView(const std::string& sourceKey,
                                   uint32_t mesh, uint32_t primitive,
                                   const strata::ViewConfig& config,
                                   strata::ViewPlan& out,
                                   std::string* error) const {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    const auto* model = strataAsset(sourceKey);
    if (!model) {
        if (error) *error = "Strata source is not staged";
        return false;
    }
    for (const auto& p : model->primitives)
        if (p.mesh == mesh && p.primitive == primitive)
            return strata::planView(p.geometry, config, out, error);
    if (error) *error = "Strata primitive is not staged";
    return false;
#else
    (void)sourceKey; (void)mesh; (void)primitive; (void)config; (void)out;
    if (error) *error = "Strata support is disabled at build time";
    return false;
#endif
}

void GeometryScene::retireResident(gfx::Device& dev) {
    // STRATA index pages and temporal GPU masks have not joined the
    // deferred native retirement queue. Avoid in-flight destruction.
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if ((!strataPortableSources_.empty() || strataGpuMetadata_ ||
         strataGpuCulling_) && !dev.waitForResourceIdle())
        throw std::runtime_error(
            "geometry scene: STRATA GPU resources require idle before reload");
#endif
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    // Renderer3D has consumed the previous draw epoch before scene unload.
    // Release alias borrowers BEFORE their index sidecars / source Models.
    strataPortableDrawModels_.clear();
    strataLastPlans_.clear();
    strataSharedAliases_.clear();
    strataPortableLodHistories_.clear();
    for (auto& [instance, compact] : strataCompactBuffers_)
        if (compact) compact->release(dev);
    strataCompactBuffers_.clear();
    for (auto& [source, staged] : strataPortableSources_)
        if (staged) staged->shutdown(dev);
    strataPortableSources_.clear();
    strataDebugGeometry_.clear();
    // The original RHI buffers are already released through their owner;
    // only now return cross-world reservations to the device ledger.
    for(const auto& [source,token]:strataGlobalIndexLeases_)
        strata::releaseDeviceIndexBytes(dev,token);
    strataGlobalIndexLeases_.clear();
    strata::clearIdleDeviceIndexBudget(dev);
    if (strataGpuCulling_) strataGpuCulling_->shutdown(dev);
    if (strataGpuMetadata_) strataGpuMetadata_->shutdown(dev);
#endif
    strataGpuCulling_.reset();
    strataGpuMetadata_.reset();
    strataGpuDevice_ = nullptr;
    strataPortableDevice_=nullptr;
    strataStreamingFrameSerial_=UINT64_MAX;
    strataStreamingFrameBudgetLeft_=0;
    strataStreamingSourceCursor_=0;
    strataStaging_.reset();
    // Source pointers/StaticModelCache records must be detached before
    // their Models can become eligible for GPU retirement.
    instances_.clear();
    for(auto& g:groups_)retirement_.retireModel(dev,g.proxy);
    groups_.clear();
    for(auto& [key,m]:models_) {
        (void)key;
        retirement_.retireModel(dev,m);
    }
    models_.clear();
    warnings_.clear();
    stats_={};
    proxyVertices_=0;
}
size_t GeometryScene::pendingGpuRetirements() const noexcept {
    size_t count=retirement_.pending();
    for(const auto& stage:failedStages_)
        count+=stage->retirement_.pending();
    return count;
}
bool GeometryScene::collectRetired(gfx::Device& dev) {
    // Called at the render-owner frame boundary, never inside a draw pass.
    bool ok=retirement_.collect(dev);
    for(auto it=failedStages_.begin();it!=failedStages_.end();) {
        auto& stage=**it;
        const bool complete=stage.retirement_.collect(dev);
        if(!complete)ok=false;
        // Never erase an owner if collection reported a failure, even if
        // an unusual backend leaves the pending count at zero.
        if(complete && stage.retirement_.pending()==0)
            it=failedStages_.erase(it);
        else ++it;
    }
    return ok;
}
bool GeometryScene::recoverFailedStagesAfterGpuIdle(gfx::Device& dev) {
    // Explicit recovery point: a failed barrier leaves the current stage
    // and all following stages owned for a subsequent retry. Earlier stages
    // may already have been safely released.
    for(auto it=failedStages_.begin();it!=failedStages_.end();) {
        if(!(*it)->retirement_.completeForShutdown(dev))
            return false;
        it=failedStages_.erase(it);
    }
    return true;
}
void GeometryScene::shutdown(gfx::Device& dev) {
    retireResident(dev);
    if(!recoverFailedStagesAfterGpuIdle(dev))
        throw std::runtime_error("geometry scene: failed-stage GPU shutdown barrier failed");
    // Unlike source reload, final teardown is allowed to block until all
    // recorded GPU submissions and the retirement queue have completed.
    if(!retirement_.completeForShutdown(dev))
        throw std::runtime_error("geometry scene: GPU shutdown barrier failed");
}
// Source vertex budget of a file-loaded scene: room for multi-million
// triangle scans/buildings, which STRATA exists for (loadPrepared caps 16M).
static constexpr size_t kSceneSourceVertexBudget=12000000;
void GeometryScene::load(gfx::Device& dev,const Json& json,const FileReader& read){loadImpl(dev,json,read,{},kSceneSourceVertexBudget);}
void GeometryScene::loadTransactional(gfx::Device& dev,const Json& json,const FileReader& read) {
    // The host may call recoverFailedStagesAfterGpuIdle() explicitly when
    // the device becomes responsive. Do not block here or create more GPU
    // work while the retained failure budget is exhausted.
    if(failedStages_.size()>=kMaxFailedStages) {
        ++rejectedFailedStageLoads_;
        throw std::runtime_error("geometry scene: failed-stage retirement backpressure");
    }
    // Reserve recovery ownership *before* any new GPU allocation. The
    // catch path can then retain the failed staging scene without allocating.
    failedStages_.reserve(failedStages_.size()+1);
    auto staged=std::make_unique<GeometryScene>();
    // The staging owner must build with the live scene's STRATA policy;
    // a default-constructed scene has every STRATA path disabled and a
    // transactional reload would otherwise silently turn STRATA off.
    staged->copyStrataConfigFrom(*this);
    try {
        staged->loadImpl(dev,json,read,{},kSceneSourceVertexBudget);
    } catch (...) {
        // loadImpl enqueues partial GPU uploads in this staging owner.
        // Never drop its retirement queue on a refused device-idle barrier.
        if(!staged->retirement_.completeForShutdown(dev))
            failedStages_.push_back(std::move(staged));
        throw;
    }
    // Staged CPU containers own the newly uploaded resources. Moving them
    // into the live instance is noexcept; enqueue old resident models first.
    // Keep the live retirement queue (which tracks older in-flight frames).
    retireResident(dev);
    models_=std::move(staged->models_);
    instances_=std::move(staged->instances_);
    groups_=std::move(staged->groups_);
    warnings_=std::move(staged->warnings_);
    stats_=staged->stats_;
    hlod_=staged->hlod_;
    geometryEnabled_=staged->geometryEnabled_;
    surfacesEnabled_=staged->surfacesEnabled_;
    cacheEnabled_=staged->cacheEnabled_;
    errorPixels_=staged->errorPixels_;
    maxPixels_=staged->maxPixels_;
    strataTriangleBudget_=staged->strataTriangleBudget_;strataBudgetScale_=1.f;strataBudgetLastTotal_=0;
    proxyVertices_=staged->proxyVertices_;
    // STRATA sidecars are keyed by Model*/Instance* addresses that stay
    // stable across the container moves above (unique_ptr / vector moves).
    // retireResident() already released the old live STRATA resources.
    strataStaging_=std::move(staged->strataStaging_);
    strataGpuMetadata_=std::move(staged->strataGpuMetadata_);
    strataGpuCulling_=std::move(staged->strataGpuCulling_);
    strataPortableSources_=std::move(staged->strataPortableSources_);
    strataGlobalIndexLeases_=std::move(staged->strataGlobalIndexLeases_);
    strataPortableDrawModels_=std::move(staged->strataPortableDrawModels_);
    strataCompactBuffers_=std::move(staged->strataCompactBuffers_);
    strataSharedAliases_=std::move(staged->strataSharedAliases_);
    strataPortableLodHistories_=std::move(staged->strataPortableLodHistories_);
    strataDebugGeometry_=std::move(staged->strataDebugGeometry_);
    strataGpuDevice_=staged->strataGpuDevice_;
    strataPortableDevice_=staged->strataPortableDevice_;
    strataStreamingFrameSerial_=staged->strataStreamingFrameSerial_;
    strataStreamingFrameBudgetLeft_=staged->strataStreamingFrameBudgetLeft_;
    strataStreamingSourceCursor_=staged->strataStreamingSourceCursor_;
    // The staged scene started from the live config; a scene-authored
    // "strata" block may have switched portable rendering / hysteresis.
    strataPortableRenderingEnabled_=staged->strataPortableRenderingEnabled_;
    strataLodHysteresisFraction_=staged->strataLodHysteresisFraction_;
    strataReflectionFinest_=staged->strataReflectionFinest_;
    strataMergeClusters_=staged->strataMergeClusters_;
    strataShadowErrorScale_=staged->strataShadowErrorScale_;
    strataAutoSelect_=staged->strataAutoSelect_;
    strataAutoMinTriangles_=staged->strataAutoMinTriangles_;
    strataSelectDecisions_=std::move(staged->strataSelectDecisions_);
    staged->strataStaging_.reset();
    staged->strataGpuMetadata_.reset();
    staged->strataGpuCulling_.reset();
    staged->strataPortableSources_.clear();
    staged->strataGlobalIndexLeases_.clear();
    staged->strataPortableDrawModels_.clear();
    staged->strataLastPlans_.clear();
    staged->strataCompactBuffers_.clear();
    staged->strataSharedAliases_.clear();
    staged->strataPortableLodHistories_.clear();
    staged->strataDebugGeometry_.clear();
    staged->strataGpuDevice_=nullptr;
    staged->strataPortableDevice_=nullptr;
    staged->stats_={};
    staged->proxyVertices_=0;
}
void GeometryScene::copyStrataConfigFrom(const GeometryScene& o) {
    strataStagingEnabled_=o.strataStagingEnabled_;
    strataGpuMetadataEnabled_=o.strataGpuMetadataEnabled_;
    strataGpuCullingEnabled_=o.strataGpuCullingEnabled_;
    strataRenderPreviewEnabled_=o.strataRenderPreviewEnabled_;
    strataPortableRenderingEnabled_=o.strataPortableRenderingEnabled_;
    strataLodHysteresisFraction_=o.strataLodHysteresisFraction_;
    strataReflectionFinest_=o.strataReflectionFinest_;
    strataMergeClusters_=o.strataMergeClusters_;
    strataShadowErrorScale_=o.strataShadowErrorScale_;
    strataAutoSelect_=o.strataAutoSelect_;
    strataAutoMinTriangles_=o.strataAutoMinTriangles_;
    // Debug view options decide per-source capture during load.
    strataDebugView_=o.strataDebugView_;
    strataDebugRightHalf_=o.strataDebugRightHalf_;
    strataDebugGeometryCapture_=o.strataDebugGeometryCapture_;
    strataGpuLodPolicyEpoch_=o.strataGpuLodPolicyEpoch_;
    strataVulkanGpuIndirectEnabled_=o.strataVulkanGpuIndirectEnabled_;
    strataVulkanGpuSelectionParityVerified_=o.strataVulkanGpuSelectionParityVerified_;
    strataVulkanGpuRasterParityVerified_=o.strataVulkanGpuRasterParityVerified_;
    strataGpuTemporalParityVerified_=o.strataGpuTemporalParityVerified_;
    strataVulkanGpuSunShadowParityVerified_=o.strataVulkanGpuSunShadowParityVerified_;
    strataVulkanGpuSpotShadowParityVerified_=o.strataVulkanGpuSpotShadowParityVerified_;
    strataVulkanGpuReflectionParityVerified_=o.strataVulkanGpuReflectionParityVerified_;
    strataStreamingIndexBudgetBytes_=o.strataStreamingIndexBudgetBytes_;
    strataStreamingSceneBudgetBytes_=o.strataStreamingSceneBudgetBytes_;
    strataGlobalIndexBudgetBytes_=o.strataGlobalIndexBudgetBytes_;
    strataGlobalFrameUploadBudgetBytes_=o.strataGlobalFrameUploadBudgetBytes_;
    strataIncrementalIndexStreamingEnabled_=o.strataIncrementalIndexStreamingEnabled_;
    strataStreamingUploadBudgetBytes_=o.strataStreamingUploadBudgetBytes_;
    strataStreamingFrameUploadBudgetBytes_=o.strataStreamingFrameUploadBudgetBytes_;
    strataDiskPageDirectory_=o.strataDiskPageDirectory_;
    strataDropArchivedCpuIndicesEnabled_=o.strataDropArchivedCpuIndicesEnabled_;
}

void GeometryScene::loadPrepared(gfx::Device& dev,const Json& json,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget){if(!vertexBudget||vertexBudget>16000000)throw std::runtime_error("geometry scene: prepared vertex budget must be 1..16000000");loadImpl(dev,json,{},std::move(models),vertexBudget);}
void GeometryScene::loadImpl(gfx::Device& dev,const Json& json,const FileReader& read,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget){
    validate(json);
    // A live HLOD/source refresh may happen while an older frame still
    // references its old geometry. Do NOT call blocking shutdown here.
    retireResident(dev);
    strataPortableDevice_=&dev;
    models_=std::move(models);geometryEnabled_=surfacesEnabled_=cacheEnabled_=true;
    // Scene-authored STRATA opt-in (default OFF). AIVERSE_STRATA=off keeps
    // the legacy path for A/B. Applied before staging below.
    if(json.has("strata")) {
        const char* env=std::getenv("AIVERSE_STRATA");
        const bool off=env&&(std::string(env)=="off"||std::string(env)=="0");
        strataPortableRenderingEnabled_=!off&&json["strata"].getBool("enabled");
        strataMergeClusters_=json["strata"].getBool("mergeClusters",true);
        // 0 (default): the shadow pass uses the camera's cut. With a shadow view in the selection a shadow map
        // that covers the model at full resolution pinned STRATA to its finest clusters at any camera distance.
        strataShadowErrorScale_=float(json["strata"]["shadowErrorScale"].toDouble(0.0));
        strataAutoSelect_=json["strata"].getStr("select","auto")!="all";
        strataAutoMinTriangles_=uint64_t(std::max(0.0,json["strata"]["minTriangles"].toDouble(0.0)));
        strataCookedDir_=json["strata"].getStr("cooked");
        strataTriangleBudget_=uint64_t(std::max(0.0,json["strata"]["triangleBudget"].toDouble(0.0)));
        if(const char* b=std::getenv("AIVERSE_STRATA_TRI_BUDGET"); b&&*b)strataTriangleBudget_=uint64_t(std::max(0.0,std::atof(b)));
        strataBudgetScale_=1.f;strataBudgetLastTotal_=0;
        if(json["strata"].has("reflections"))
            strataReflectionFinest_=json["strata"].getStr("reflections")!="main";
        if(json["strata"].has("hysteresis"))
            (void)setStrataLodHysteresisFraction(float(json["strata"]["hysteresis"].toDouble()));
    }
    hlod_=json["hlod"].getBool("enabled");errorPixels_=float(json["hlod"]["errorPixels"].toDouble(2));maxPixels_=float(json["hlod"]["maxPixels"].toDouble(256));
    try{
        size_t vertices=0;
        for(const auto& [name,m]:models_){
            if(!m||m->vertices.empty()||m->indices.empty()||!m->skins.empty()||!m->clips.empty())throw std::runtime_error("geometry scene: prepared models must be nonempty rigid CPU meshes");
            if(m->vb.valid()||m->ib.valid())throw std::runtime_error("geometry scene: prepared geometry must not be uploaded");
            for(const auto& mesh:m->meshes)for(const auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph source unsupported");
            vertices+=m->vertices.size();
        }
        if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
        for(auto& i:json["instances"].items()){
            const auto file=i.getStr("model");
            if(!models_.count(file)){
                if(!read)throw std::runtime_error("geometry scene: missing prepared source "+file);
                auto m=loadModel(file,read,&warnings_);
                if(!m->skins.empty()||!m->clips.empty())throw std::runtime_error("geometry scene: animated model '"+file+"' must use ordinary entities");
                for(auto& mesh:m->meshes)for(auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph model must use ordinary entities");
                vertices+=m->vertices.size();if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
                if(m->vertices.empty()||m->indices.empty())throw std::runtime_error("geometry scene: source has no triangles: "+file);
                models_[file]=std::move(m);
            }
            Instance in;in.model=models_.at(file).get();in.policy=parseGeometryPolicy(i["geometry"]);
            auto rot=vector(i["rotation"],{});constexpr float deg=.0174532925199433f;
            in.world=mat4::translate(vector(i["position"],{}))*mat4::rotateY(rot.y*deg)*mat4::rotateX(rot.x*deg)*mat4::rotateZ(rot.z*deg)*mat4::scale(vector(i["scale"],{1,1,1}));
            if(i.has("layers"))in.layers=uint32_t(i["layers"].toDouble());
            if(i.has("strata")&&i["strata"].type()==Json::Type::Bool)in.strataForce=i["strata"].boolean()?1:-1;
            if(i.has("proxy")){
                const auto proxyFile=i["proxy"].getStr("model");
                if(!models_.count(proxyFile)){
                    if(!read)throw std::runtime_error("geometry scene: missing prepared proxy "+proxyFile);
                    auto proxy=loadModel(proxyFile,read,&warnings_);
                    vertices+=proxy->vertices.size();if(vertices>vertexBudget)throw std::runtime_error("geometry scene: exceeds source vertex budget");
                    if(proxy->vertices.empty()||proxy->indices.empty()||!proxy->skins.empty()||!proxy->clips.empty())throw std::runtime_error("geometry scene: proxy must be a nonempty rigid model");
                    for(auto& mesh:proxy->meshes)for(auto& p:mesh.primitives)if(p.morphTargets)throw std::runtime_error("geometry scene: morph proxy unsupported");
                    models_[proxyFile]=std::move(proxy);
                }
                in.surfaceProxy=models_.at(proxyFile).get();in.proxyPixels=float(i["proxy"]["maxPixels"].toDouble(96));
                ++stats_.proxies;
            }
            instances_.push_back(in);
        }
        // strata.select auto: decide per source BEFORE upload preparation may
        // generate LOD meshes, so "authored LODs" means the asset's own chain.
        std::set<const Model*> strataLodSources;
        strataSelectDecisions_.clear();
        if (strataPortableRenderingEnabled_) {
            // Per instance: "strata": true forces STRATA, false forces the LOD
            // path, absent = auto (the model's decision below; with
            // strata.select "all" every model is STRATA). A model is staged
            // when at least one of its instances draws through STRATA.
            std::map<const Model*,size_t> count;
            for (const auto& in : instances_) ++count[in.model];
            StrataAutoSelectConfig cfg;
            if (strataAutoMinTriangles_) cfg.minTriangles=strataAutoMinTriangles_;
            std::map<const Model*,bool> autoStrata;
            for (const auto& [file, m] : models_) {
                if (!m || !count.count(m.get())) continue;
                std::string why="strata.select all";
                const bool a=!strataAutoSelect_ || strataPreferred(strataSourceFacts(*m,count[m.get()]),cfg,&why);
                autoStrata[m.get()]=a;
                size_t on=0,off=0,forcedOn=0,forcedOff=0;
                for (auto& in : instances_) {
                    if (in.model!=m.get()) continue;
                    in.strataRoute=in.strataForce>0 || (in.strataForce==0 && a);
                    in.strataRoute ? ++on : ++off;
                    if (in.strataForce>0) ++forcedOn;
                    if (in.strataForce<0) ++forcedOff;
                }
                const bool staged=on>0;
                if (!staged) strataLodSources.insert(m.get());
                staged ? ++stats_.strataAutoStrataSources : ++stats_.strataAutoLodSources;
                std::string line=file+": "+(staged?"strata":"lod")+" (auto: "+(a?"strata":"lod")+", "+why+")";
                if (forcedOn||forcedOff)
                    line+=" instances strata="+std::to_string(on)+" lod="+std::to_string(off)+
                          " (forced on "+std::to_string(forcedOn)+", forced off "+std::to_string(forcedOff)+")";
                strataSelectDecisions_.push_back(line);
            }
        }
        for(auto& [file,m]:models_)if(hlod_||std::any_of(instances_.begin(),instances_.end(),[&](const Instance& i){return i.model==m.get()&&(i.policy.clusters||i.policy.shadowClusters||i.policy.occluder);}))prepareGeometry(*m);
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
        if (strataPortableRenderingEnabled_) {
            // Crucial ordering: ordinary uploadModel() may generate LOD
            // meshes, change packed vertex attributes and rebuild batches.
            // Complete that preparation first, then stage Strata from the
            // EXACT source vertex/index stream that the RHI will upload.
            // This leaves legacy material/shadow/LOD metadata intact.
            for (auto& [key, model] : models_)
                if (model && !model->uploadPrepared) {
                    // A model that every instance draws through STRATA needs
                    // no generated LOD chain: it would cost load time (mesh
                    // simplification) and GPU index memory for a path that
                    // never draws. Authored LODs are kept.
                    const Model* m=model.get();
                    const bool strataOnly=!strataLodSources.count(m) &&
                        std::none_of(instances_.begin(),instances_.end(),[m](const Instance& in){return in.model==m && !in.strataRoute;}) &&
                        std::any_of(instances_.begin(),instances_.end(),[m](const Instance& in){return in.model==m;});
                    if (strataOnly) model->batching.autoLod=false;
                    prepareModelUpload(*model);
                    using Policy=Model::Batching::GeometryPoolPolicy;
                    if (model->batching.geometryPoolPolicy==Policy::Inherit)
                        model->batching.geometryPoolPolicy=
                            Policy::AllowRigidLods;
                }
        }
        if (strataStagingEnabled_ || strataPortableRenderingEnabled_) {
            // Build once for each *source* model before uploadModel may release
            // CPU vertex streams. Surface/HLOD proxies retain legacy ownership
            // and are never duplicated here. This path does NOT draw Strata.
            const auto stageStart=std::chrono::steady_clock::now();
            strataStaging_ = std::make_shared<strata::SceneStaging>();
            if (strataPortableRenderingEnabled_) {
                strataStaging_->setAllowPreparedRootLods(true);
                strataStaging_->setTolerateSourceSeamDefects(true);
            }
            // Cooked STRATA (StrataCooked.h): load the hierarchy built at
            // cook time instead of building it now. AIVERSE_STRATA_COOK_OUT
            // (a folder) makes this load the cook pass: every source built
            // here is written there, to be shipped as strata.cooked.
            {
                strata::CookIo io;
                if (!strataCookedDir_.empty() && read) {
                    const std::string dir = strataCookedDir_;
                    io.read = [read, dir](const std::string& name, std::string& bytes) {
                        return read(dir + "/" + name, bytes);
                    };
                }
                if (const char* out = std::getenv("AIVERSE_STRATA_COOK_OUT"); out && *out)
                    io.writeDirectory = out;
                strataStaging_->setCookIo(std::move(io));
            }
            std::set<const Model*> usedSources;
            for (const auto& instance : instances_)
                if (!strataLodSources.count(instance.model))
                    usedSources.insert(instance.model);
            for (const auto& [file, model] : models_)
                if (model && usedSources.count(model.get())) {
                    strataStaging_->stage(file, *model);
                    // Debug-only copy BEFORE uploadModel releases vertices.
                    if (strataDebugGeometryCapture_ &&
                        strataStaging_->find(file)) {
                        auto geometry=
                            std::make_shared<strata::DebugGeometry>();
                        if (strata::captureDebugGeometry(*model,*geometry))
                            strataDebugGeometry_[model.get()]=
                                std::move(geometry);
                    }
                }
            stats_.strataCookedSources=strataStaging_->stats().cookedLoadedSources;
            stats_.strataCookWrittenSources=strataStaging_->stats().cookedWrittenSources;
            stats_.strataCookStaleSources=strataStaging_->stats().cookedStaleSources;
            stats_.strataCookedLoadMs=strataStaging_->stats().cookedLoadMs;
            stats_.strataBuildMs=strataStaging_->stats().buildMs;
            stats_.strataStageMs=float(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stageStart).count());
            const auto& st = strataStaging_->stats();
            stats_.strataSources = st.stagedSources;
            stats_.strataPrimitives = st.stagedPrimitives;
            stats_.strataSkippedSources = st.skippedSources;
            stats_.strataSkippedPrimitives = st.skippedPrimitives;
            stats_.strataFailures = st.failedSources;
            stats_.strataNodes = st.clusterNodes;
            stats_.strataMeshlets = st.meshlets;
            stats_.strataIndexBytes = st.indexBytes;
            stats_.strataSeamCertifiedPrimitives=st.seamCertifiedPrimitives;
            stats_.strataSeamRejectedParents=st.seamRejectedParents;
            stats_.strataCrossMaterialCertifiedSources=
                st.crossMaterialCertifiedSources;
            stats_.strataCrossMaterialMatchedSeams=
                st.crossMaterialMatchedSeams;
            for (const auto& warning : strataStaging_->warnings())
                warnings_.push_back(warning);
        }
#endif
        if(hlod_&&!instances_.empty()){std::vector<size_t> ids(instances_.size());std::iota(ids.begin(),ids.end(),0);buildGroup(dev,std::move(ids));}
        for(auto& [k,m]:models_)if(!uploadModel(dev,*m)||!m->vb.valid()||!m->ib.valid())throw std::runtime_error("geometry scene: source GPU upload failed");
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
        if (strataPortableRenderingEnabled_ && strataStaging_) {
            // Budget zero preserves the original Stage 14 eager path. A
            // scene budget (even with no per-source cap) enables streaming.
            // Root reservations are admitted BEFORE any GPU index upload:
            // models that cannot fit their roots stay intact on Legacy.
            std::map<std::string,size_t> streamingBudgets;
            const bool streamingRequested = strataStreamingIndexBudgetBytes_ ||
                                            strataStreamingSceneBudgetBytes_ ||
                                            strataGlobalIndexBudgetBytes_;
            const bool globalBudgetConfigured = !streamingRequested ||
                (strata::configureDeviceIndexBudget(
                    dev,uint64_t(strataGlobalIndexBudgetBytes_)) &&
                 strata::configureDeviceFrameUploadBudget(
                    dev,uint64_t(strataGlobalFrameUploadBudgetBytes_)));
            if(!globalBudgetConfigured)
                warnings_.push_back(
                    "STRATA global device budget conflicts with an active world");
            if (streamingRequested) {
                std::vector<strata::SceneIndexBudgetRequest> candidates;
                constexpr uint64_t kSourceMaximum=64ull*1024ull*1024ull;
                for (const auto& [key,model]:models_) {
                    if (!model) continue;
                    const auto* asset=strataStaging_->find(key);
                    if (!asset || asset->primitives.empty() ||
                        !asset->skipped.empty()) continue;
                    uint64_t roots=0, all=0;
                    for(const auto& entry:asset->primitives){
                        const auto& g=entry.geometry;
                        all+=uint64_t(g.indices.size())*sizeof(uint32_t);
                        for(uint32_t root:g.roots)
                            if(root<g.nodes.size())
                                roots+=uint64_t(g.nodes[root].indexCount)*
                                       sizeof(uint32_t);
                    }
                    uint64_t cap=std::min(all,kSourceMaximum);
                    if(strataStreamingIndexBudgetBytes_)
                        cap=std::min(cap,uint64_t(strataStreamingIndexBudgetBytes_));
                    if(!roots || roots>cap) continue;
                    candidates.push_back({key,roots,cap});
                }
                for(const auto& grant:strata::apportionSceneIndexBudget(
                        std::move(candidates),strataStreamingSceneBudgetBytes_))
                    if(grant.bytes)
                        streamingBudgets[grant.key]=size_t(grant.bytes);
            }
            // CPU-selected raster fallback works without compute or MDI.
            // Only index data is streamed; source VB/material ownership
            // remains with the single original Renderer3D Model.
            for (const auto& [key, model] : models_) {
                if (!model) continue;
                const auto* asset = strataStaging_->find(key);
                if (!asset) { ++stats_.strataPortableDeclined; continue; }
                if(!asset->sourceSeamsCertified ||
                   !std::all_of(asset->primitives.begin(),
                       asset->primitives.end(),
                       [](const strata::ModelPrimitiveAsset& entry){
                           return entry.geometry.seamCertified;
                       })) {
                    ++stats_.strataPortableDeclined;
                    ++stats_.strataSeamUnsafeDeclined;
                    continue; // no unverified parent LOD enters Renderer3D
                }
                if(!globalBudgetConfigured){
                    ++stats_.strataPortableDeclined;
                    ++stats_.strataGlobalBudgetDeclined;
                    continue;
                }
                size_t sourceBudget=0;
                if(streamingRequested){
                    const auto granted=streamingBudgets.find(key);
                    if(granted==streamingBudgets.end()){
                        ++stats_.strataPortableDeclined;
                        ++stats_.strataStreamingBudgetDeclined;
                        continue; // source remains fully valid Legacy
                    }
                    sourceBudget=granted->second;
                }
                // Reserve the maximum arena allocation BEFORE loading a
                // source. All worlds on this device compete for one ledger.
                // If the remaining cap allows only roots, clamp this source
                // rather than dropping it when a safe coarse LOD fits.
                uint64_t leaseToken=0;
                if(streamingRequested){
                    uint64_t roots=0;
                    for(const auto& entry:asset->primitives)
                        for(uint32_t root:entry.geometry.roots)
                            roots+=uint64_t(entry.geometry.nodes[root].indexCount)*4u;
                    const auto current=strata::deviceIndexBudgetStats(dev);
                    if(current.limitBytes){
                        const uint64_t spare=current.reservedBytes<=current.limitBytes
                            ? current.limitBytes-current.reservedBytes:0;
                        if(spare<roots){
                            ++stats_.strataPortableDeclined;
                            ++stats_.strataGlobalBudgetDeclined;
                            continue;
                        }
                        sourceBudget=size_t(std::min<uint64_t>(sourceBudget,spare));
                    }
                    if(!strata::reserveDeviceIndexBytes(dev,sourceBudget,leaseToken)){
                        ++stats_.strataPortableDeclined;
                        ++stats_.strataGlobalBudgetDeclined;
                        continue;
                    }
                    strataGlobalIndexLeases_[model.get()]=leaseToken;
                }
                std::string error;
                std::shared_ptr<strata::DiskIndexPages> diskPages;
                const bool diskApplicable =
                    !strataDiskPageDirectory_.empty() && sourceBudget &&
                    strataIncrementalIndexStreamingEnabled_ &&
                    dev.canUpdateSharedBuffers() &&
                    (dev.info().backend==gfx::Backend::Metal ||
                     dev.info().backend==gfx::Backend::Vulkan);
                if(diskApplicable){
                    diskPages=std::make_shared<strata::DiskIndexPages>();
                    // Source key is only hashed to a safe archive filename;
                    // it is never interpreted as a filesystem subpath.
                    const auto filename=strata::DiskIndexPages::
                        archiveFilename(key);
                    const auto path=(std::filesystem::path(
                        strataDiskPageDirectory_)/filename).string();
                    if(!diskPages->open(path,*asset,sourceBudget,512,&error)){
                        ++stats_.strataPortableDeclined;
                        ++stats_.strataDiskArchiveDeclined;
                        if(leaseToken){
                            strata::releaseDeviceIndexBytes(dev,leaseToken);
                            strataGlobalIndexLeases_.erase(model.get());
                        }
                        warnings_.push_back("STRATA disk archive '"+key+"': "+error);
                        continue; // original Model remains Legacy
                    }
                }
                auto source=std::make_shared<strata::PortableSource>();
                // Scene-authored STRATA draws adjacent clusters as one primitive.
                source->setMergeAdjacentClusters(strataMergeClusters_);
                // One draw per material instead of one per cluster.
                source->setCompactSelection(strataMergeClusters_);
                if (!source->stage(dev, *model, *asset, &error,
                                   sourceBudget,
                                   strataIncrementalIndexStreamingEnabled_,
                                   strataStreamingUploadBudgetBytes_,
                                   std::move(diskPages))) {
                    ++stats_.strataPortableDeclined;
                    if(leaseToken){
                        strata::releaseDeviceIndexBytes(dev,leaseToken);
                        strataGlobalIndexLeases_.erase(model.get());
                    }
                    if (!error.empty())
                        warnings_.push_back("portable Strata source " + key +
                                            ": " + error);
                    continue;
                }
                if(source->streamingEnabled()){
                    ++stats_.strataStreamedSources;
                    if(const auto* status=source->residencyStats()){
                        stats_.strataStreamingIndexBytes+=status->activeBytes;
                        stats_.strataStreamingArenaBytes+=status->arenaBytes;
                        if(status->incremental)++stats_.strataIncrementalSources;
                        if(diskApplicable){
                            ++stats_.strataDiskArchiveSources;
                            stats_.strataDiskBytesRead+=status->diskBytesRead;
                        }
                    }
                }
                source->setDebugCaptureEnabled(strataDebugView_.enabled);
                strataPortableSources_.emplace(model.get(), std::move(source));
                ++stats_.strataPortableSources;
                // Disk manifest was matched against the entire source,
                // and all pinned roots have been checked and GPU-uploaded.
                // Only the portable disk path is allowed to drop its
                // redundant STRATA CPU index arrays. Diagnostic/metadata
                // paths still require raw indices and must retain them.
                const bool canReleaseCpu = diskApplicable &&
                    strataDropArchivedCpuIndicesEnabled_ &&
                    !strataStagingEnabled_ && !strataGpuMetadataEnabled_ &&
                    !strataGpuCullingEnabled_ && !strataRenderPreviewEnabled_;
                if(canReleaseCpu){
                    const size_t freed=strataStaging_->
                        discardArchivedCpuIndices(key);
                    stats_.strataArchivedCpuIndexBytesFreed+=freed;
                    stats_.strataArchivedCpuIndexSources+=freed!=0;
                }else if(diskApplicable && strataDropArchivedCpuIndicesEnabled_){
                    warnings_.push_back("STRATA kept CPU index payload for diagnostic source "+key);
                }
                // Compacted submission keeps its own compressed index copy,
                // so the asset's full 32-bit CPU indices are redundant once
                // the GPU index stream is uploaded (debug views and the GPU
                // metadata/culling/preview paths still read them).
                const auto* portable=strataPortableSources_.at(model.get()).get();
                stats_.strataPortableGpuIndexBytes+=portable->gpuIndexBytes();
                if(!canReleaseCpu && portable->compactSelection() && !portable->streamingEnabled() &&
                   !strataDebugView_.enabled && !strataDebugGeometryCapture_ &&
                   !strataStagingEnabled_ && !strataGpuMetadataEnabled_ &&
                   !strataGpuCullingEnabled_ && !strataRenderPreviewEnabled_ &&
                   !strataVulkanGpuIndirectEnabled_){
                    const size_t freed=strataStaging_->discardArchivedCpuIndices(key);
                    stats_.strataArchivedCpuIndexBytesFreed+=freed;
                    stats_.strataCompressedCpuIndexBytes+=portable->cpuIndexBytes();
                }
            }
        }
        {
            const auto shared=strata::deviceIndexBudgetStats(dev);
            stats_.strataDeviceReservedIndexBytes=shared.reservedBytes;
            stats_.strataDeviceIndexLeases=shared.activeLeases;
            stats_.strataDeviceFrameUploadedBytes=shared.currentFrameUploadedBytes;
            stats_.strataDeviceFrameUploadRemainingBytes=shared.frameUploadRemainingBytes;
            const auto io=strata::sharedDiskIoStats();
            stats_.strataSharedIoWorkers=io.workers;
            stats_.strataSharedIoQueuedJobs=io.queuedJobs;
            stats_.strataSharedIoActiveJobs=io.activeJobs;
            stats_.strataSharedCpuReadyBytes=io.readyBytes;
            stats_.strataSharedCpuReadyBudgetBytes=io.readyLimitBytes;
        }
        if (strataGpuMetadataEnabled_ && strataStaging_) {
            strataGpuDevice_ = &dev;
            // Optional GPU metadata only; source Model remains owner of
            // GlobalGeometryPool VB/IB and legacy Renderer3D draws unchanged.
            // Failure to allocate metadata must never abort the scene.
            strataGpuMetadata_ = std::make_shared<strata::GpuMetadataScene>();
            for (const auto& [key, model] : models_)
                if (model)
                    if (const auto* asset = strataStaging_->find(key)) {
                        const bool seamSafe=std::all_of(
                            asset->primitives.begin(),asset->primitives.end(),
                            [](const strata::ModelPrimitiveAsset& entry){
                                return entry.geometry.seamCertified;
                            });
                        if(seamSafe && asset->sourceSeamsCertified)
                            strataGpuMetadata_->stage(dev,key,*model,*asset);
                        else
                            ++stats_.strataSeamUnsafeDeclined;
                    }
            const auto& gpuStats = strataGpuMetadata_->stats();
            stats_.strataGpuMetadataSources = gpuStats.sources;
            stats_.strataGpuMetadataDeclined = gpuStats.declined;
            stats_.strataGpuMetadataFailures = gpuStats.failed;
            stats_.strataGpuMetadataBytes = gpuStats.bytes;
            stats_.strataGpuIndexedSources = gpuStats.indexedSources;
            stats_.strataGpuIndexBytes = gpuStats.indexBytes;
            stats_.strataGpuIndexDeclined = gpuStats.indexDeclined;
            stats_.strataGpuIndexFailures = gpuStats.indexFailed;
            for (const auto& warning : strataGpuMetadata_->warnings())
                warnings_.push_back(warning);
        }
#endif
        for(auto& in:instances_){
            in.cache.prepare(*in.model,in.world);
            if(in.surfaceProxy)in.proxyCache.prepare(*in.surfaceProxy,in.world);
            in.min={1e30f,1e30f,1e30f};in.max={-1e30f,-1e30f,-1e30f};
            for(int c=0;c<8;++c){auto a=in.model->boundsMin,b=in.model->boundsMax;auto p=in.world.transformPoint({c&1?b.x:a.x,c&2?b.y:a.y,c&4?b.z:a.z});in.min=min3(in.min,p);in.max=max3(in.max,p);}
        }
        for(auto& g:groups_)if(g.proxy)g.cache.prepare(*g.proxy,mat4{});
        stats_.instances=uint32_t(instances_.size());stats_.groups=uint32_t(groups_.size());
    }catch(...){
        // A failed/partially uploaded new source or HLOD proxy is still a
        // GPU lifetime owner; defer it behind a real completion marker.
        retireResident(dev);
        throw;
    }
}
std::unique_ptr<Model> GeometryScene::makeProxy(const std::vector<size_t>& ids,float& error){
    // Explicit baked surfaces own their switch threshold. Do not bypass it with a parent proxy.
    for(auto id:ids)if(instances_[id].surfaceProxy)return {};
    // Transparent/masked/graph materials retain their original independent draws and sorting.
    for(auto id:ids)for(auto& m:instances_[id].model->materials)if(m.alpha!=AlphaMode::Opaque||m.transparent||m.shaderGraph)return {};
    auto out=std::make_unique<Model>();out->path="geometry:hlod";
    out->meshes.emplace_back();Node node;node.mesh=0;out->nodes.push_back(node);
    std::map<std::pair<const Model*,int>,int> materials;
    std::map<const Model*,int> images;
    for(auto id:ids){const auto& in=instances_[id];const Model& m=*in.model;
        if(proxyVertices_+out->vertices.size()+m.vertices.size()>2000000)return {};
        if(!images.count(&m)){images[&m]=int(out->images.size());out->images.insert(out->images.end(),m.images.begin(),m.images.end());}
        for(int n:m.order){const auto& nd=m.nodes[size_t(n)];if(nd.mesh<0)continue;mat4 xf=in.world*m.bindWorld[size_t(n)],normal=xf.normalMatrix();
            size_t pi=0;for(int mi=0;mi<nd.mesh;++mi)pi+=m.meshes[size_t(mi)].primitives.size();
            for(const auto& p:m.meshes[size_t(nd.mesh)].primitives){
                const auto key=std::make_pair(&m,p.material);
                if(!materials.count(key)){auto mat=m.materials[size_t(p.material)];int index=-1;
                    for(auto field:{&mat.baseTex,&mat.mrTex,&mat.normalTex,&mat.occlusionTex,&mat.emissiveTex,&mat.teamTex})if(*field>=0)*field+=images[&m];
                    for(size_t k=0;k<out->materials.size();++k)if(sameBoundMaterial(mat,out->materials[k],out->images)){index=int(k);break;}
                    if(index<0){index=int(out->materials.size());out->materials.push_back(mat);}
                    materials[key]=index;}
                // Build temporary per-material index lists after all transforms below.
                const int material=materials[key];
                std::map<uint32_t,uint32_t> remap;
                auto emit=[&](uint32_t first,uint32_t count){for(uint32_t k=0;k<count;++k){uint32_t source=m.indices[first+k];
                    if(!remap.count(source)){auto v=m.vertices[source];v.pos=xf.transformPoint(v.pos);v.nrm=normalize(normal.transformDir(v.nrm));auto t=normalize(xf.transformDir({v.tangent.x,v.tangent.y,v.tangent.z}));v.tangent={t.x,t.y,t.z,v.tangent.w};remap[source]=uint32_t(out->vertices.size());out->vertices.push_back(v);}
                    out->indices.push_back(remap[source]);
                }};
                const auto first=uint32_t(out->indices.size());
                const auto& clusters=m.geometry->primitives[pi++];
                if(clusters.empty())emit(p.firstIndex,p.indexCount);
                else for(auto& c:clusters){emit(c.coarseFirst,c.coarseCount);error=std::max(error,c.error*scaleBound(xf));}
                // Each segment is tagged; regrouped by material at the end.
                Primitive segment;segment.material=material;segment.firstIndex=first;segment.indexCount=uint32_t(out->indices.size())-first;
                out->meshes[0].primitives.push_back(segment);
            }
        }
    }
    std::vector<uint32_t> indices;std::vector<Primitive> primitives;
    for(size_t material=0;material<out->materials.size();++material){Primitive p;p.material=int(material);p.firstIndex=uint32_t(indices.size());
        for(auto& segment:out->meshes[0].primitives)if(segment.material==int(material))indices.insert(indices.end(),out->indices.begin()+segment.firstIndex,out->indices.begin()+segment.firstIndex+segment.indexCount);
        p.indexCount=uint32_t(indices.size())-p.firstIndex;p.vertexCount=uint32_t(out->vertices.size());if(p.indexCount)primitives.push_back(p);}
    // Existing instancing already combines identical primitives in batches of 32.
    std::map<std::pair<const Model*,const Primitive*>,size_t> batches;size_t sourceIndices=0;
    for(auto id:ids){const auto* model=instances_[id].model;for(int n:model->order){const auto& node=model->nodes[size_t(n)];if(node.mesh<0)continue;
        for(const auto& p:model->meshes[size_t(node.mesh)].primitives){++batches[{model,&p}];sourceIndices+=p.indexCount;}}}
    size_t sourceDraws=0;for(auto [key,count]:batches)sourceDraws+=(count+31)/32;
    if(primitives.size()>=sourceDraws && indices.size()>=sourceIndices)return {};
    out->indices=std::move(indices);out->meshes[0].primitives=std::move(primitives);out->finalize();proxyVertices_+=out->vertices.size();return out;
}
int GeometryScene::buildGroup(gfx::Device& dev,std::vector<size_t> ids){
    const int index=int(groups_.size());groups_.emplace_back();
    float3 mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
    for(auto id:ids){const auto& in=instances_[id];for(int c=0;c<8;++c){auto a=in.model->boundsMin,b=in.model->boundsMax;auto p=in.world.transformPoint({c&1?b.x:a.x,c&2?b.y:a.y,c&4?b.z:a.z});mn=min3(mn,p);mx=max3(mx,p);}}
    groups_[size_t(index)].min=mn;groups_[size_t(index)].max=mx;
    if(ids.size()>8){const auto e=mx-mn;const int axis=e.x>=e.z?0:2;std::stable_sort(ids.begin(),ids.end(),[&](size_t a,size_t b){auto x=instances_[a].world.transformPoint({}),y=instances_[b].world.transformPoint({});return axis==0?x.x<y.x:x.z<y.z;});
        auto middle=ids.begin()+ids.size()/2;const int l=buildGroup(dev,{ids.begin(),middle}),r=buildGroup(dev,{middle,ids.end()});groups_[size_t(index)].left=l;groups_[size_t(index)].right=r;
    }else groups_[size_t(index)].instances=ids;
    auto& group=groups_[size_t(index)];
    group.occlusion=std::all_of(ids.begin(),ids.end(),[&](size_t id){return instances_[id].policy.occlusion;});
    if(ids.size()>1){group.proxy=makeProxy(ids,group.error);if(group.proxy){if(!uploadModel(dev,*group.proxy)||!group.proxy->vb.valid()||!group.proxy->ib.valid())throw std::runtime_error("geometry scene: proxy GPU upload failed");++stats_.proxies;}}
    return index;
}
void GeometryScene::drawGroup(Renderer3D& r,int index){auto& g=groups_[size_t(index)];
    // Retain offscreen source draws so their shadows/reflections remain available.
    const float extent=length(g.max-g.min);
    const bool far=geometryErrorPixels(r.viewProj(),g.min,g.max,extent,r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=maxPixels_;
    if(g.proxy&&far&&geometryErrorPixels(r.viewProj(),g.min,g.max,g.error,r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=errorPixels_){ModelDraw d;d.model=g.proxy.get();d.staticCache=cacheEnabled_?&g.cache:nullptr;d.geometry.occlusion=geometryEnabled_&&g.occlusion;r.drawModel(d);++stats_.submitted;return;}
    if(g.left>=0){drawGroup(r,g.left);drawGroup(r,g.right);return;}
    for(auto id:g.instances)drawInstance(r,instances_[id]);
}
void GeometryScene::updateStrataStreamingBeforeDraw(Renderer3D& r){
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if(!strataPortableRenderingEnabled_ ||
       !(strataStreamingIndexBudgetBytes_ || strataStreamingSceneBudgetBytes_) ||
       strataPortableSources_.empty() ||
       r.sceneWidth()<=0 || r.sceneHeight()<=0)return;
    std::vector<strata::PortablePassView> views;
    views.push_back({r.viewProj(),r.sceneWidth(),r.sceneHeight(),
                     r.clipDepthZeroToOne(),errorPixels_});
    if(r.shadowQuality()!=ShadowQuality::Off && r.shadowMapSize()>0)
        views.push_back({r.lightViewProj(),r.shadowMapSize(),
                         r.shadowMapSize(),r.clipDepthZeroToOne(),errorPixels_});
    const bool forceFinest=r.lights.reflectionsWanted();
    std::map<const Model*,std::vector<strata::IndexDemand>> requests;
    std::set<const Model*> failed;
    // Preflight ALL instances before submitting ANY Renderer3D ModelDraw.
    // One shared source buffer generation covers the complete draw epoch,
    // including independent main, shadow and reflection passes.
    for(const auto& instance:instances_){
        const auto it=strataPortableSources_.find(instance.model);
        if(it==strataPortableSources_.end() || !it->second ||
           !it->second->streamingEnabled() ||
           failed.count(instance.model))continue;
        auto& list=requests[instance.model];
        const strata::PortableLodHistory* history=nullptr;
        if(strataLodHysteresisFraction_>0.f){
            const auto state=strataPortableLodHistories_.find(&instance);
            if(state!=strataPortableLodHistories_.end())
                history=state->second.get();
        }
        // A cold or newly visible instance must still be able to request
        // the unmodified reference frontier before any history is stored.
        // A temporary empty history has exactly cold-start semantics.
        const strata::PortableLodHistory cold{};
        if(!it->second->collectResidencyDemands(
               instance.world,views,forceFinest,list,
               strataLodHysteresisFraction_>0.f
                   ? (history ? history:&cold):nullptr,
               strataLodHysteresisFraction_)){
            failed.insert(instance.model);
            list.clear(); // keep roots; don't promote a partial view plan
        }
    }
    stats_.strataStreamingIndexBytes=0;
    stats_.strataStreamingEvictions=0;
    stats_.strataStreamingArenaBytes=0;
    stats_.strataStreamingUploadedBytes=0;
    stats_.strataStreamingUploadDeferrals=0;
    stats_.strataStreamingFenceDeferrals=0;
    stats_.strataDiskBytesRead=0;
    stats_.strataDiskReadFailures=0;
    stats_.strataDiskPending=0;
    stats_.strataDiskReadyBytes=0;
    stats_.strataStreamingGpuTransferBytes=0;
    // A zero/unavailable serial is not a reliable frame identity: reset
    // per invocation rather than silently accumulate across unknown epochs.
    const uint64_t telemetrySerial=strataPortableDevice_
        ? strataPortableDevice_->frameSerial() : 0;
    if(!telemetrySerial || stats_.strataStreamFrameSerial!=telemetrySerial){
        stats_.strataStreamFrameSerial=telemetrySerial;
        stats_.strataStreamFrameDemandRecords=0;
        stats_.strataStreamFrameGpuTransferBytes=0;
        stats_.strataStreamFrameUploadedBytes=0;
        stats_.strataStreamFramePromotions=0;
        stats_.strataStreamFrameEvictions=0;
        stats_.strataStreamFrameDeferrals=0;
        stats_.strataStreamFrameFailedUploads=0;
    }
    const size_t frameCap=strataStreamingFrameUploadBudgetBytes_;
    if(frameCap && strataPortableDevice_){
        const uint64_t serial=strataPortableDevice_->frameSerial();
        if(!serial || strataStreamingFrameSerial_!=serial){
            strataStreamingFrameSerial_=serial;
            strataStreamingFrameBudgetLeft_=frameCap;
        }
    }
    // Model keys are stable across runs. Rotate the first source once per
    // prepass so one expensive source cannot permanently monopolize the
    // frame-level GPU bandwidth allowance.
    std::vector<const Model*> ordered;
    for(const auto& [key,instance]:models_){
        if(!instance)continue;
        const auto it=strataPortableSources_.find(instance.get());
        if(it!=strataPortableSources_.end() && it->second &&
           it->second->streamingEnabled())
            ordered.push_back(instance.get());
    }
    if(!ordered.empty()){
        const size_t start=size_t(strataStreamingSourceCursor_++%ordered.size());
        for(size_t i=0;i<ordered.size();++i){
            const Model* model=ordered[(start+i)%ordered.size()];
            auto& source=strataPortableSources_.at(model);
            std::string error;
            const auto* before=source->residencyStats();
            const strata::IndexResidencyStats snapshot=
                before ? *before : strata::IndexResidencyStats{};
            stats_.strataStreamFrameDemandRecords+=requests[model].size();
            const bool incremental=before && before->incremental;
            size_t allowance=frameCap && incremental
                ? strataStreamingFrameBudgetLeft_ : SIZE_MAX;
            const uint64_t serial=strataPortableDevice_ ?
                strataPortableDevice_->frameSerial() : 0;
            if(incremental && strataPortableDevice_){
                const uint64_t shared=strata::deviceFrameUploadAllowance(
                    *strataPortableDevice_,serial);
                allowance=std::min<uint64_t>(allowance,shared);
            }
            if(failed.count(model) ||
               !source->updateResidency(requests[model],&error,allowance))
                ++stats_.strataStreamingPrepareFailures;
            if(const auto* status=source->residencyStats()){
                const auto change=strata::indexResidencyFrameDelta(
                    snapshot,*status);
                const uint64_t copied=change.gpuTransferBytes;
                stats_.strataStreamFrameGpuTransferBytes+=copied;
                stats_.strataStreamFrameUploadedBytes+=change.uploadedBytes;
                stats_.strataStreamFramePromotions+=change.promotions;
                stats_.strataStreamFrameEvictions+=change.evictions;
                stats_.strataStreamFrameDeferrals+=change.deferredDemands;
                stats_.strataStreamFrameFailedUploads+=change.failedUploads;
                if(frameCap && incremental){
                    strataStreamingFrameBudgetLeft_=
                        copied>=strataStreamingFrameBudgetLeft_
                            ? 0 : strataStreamingFrameBudgetLeft_-size_t(copied);
                }
                if(incremental && strataPortableDevice_)
                    strata::accountDeviceFrameUpload(
                        *strataPortableDevice_,serial,copied);
                stats_.strataStreamingIndexBytes+=status->activeBytes;
                stats_.strataStreamingEvictions+=status->evictions;
                stats_.strataStreamingArenaBytes+=status->arenaBytes;
                stats_.strataStreamingUploadedBytes+=status->uploadedBytes;
                stats_.strataStreamingGpuTransferBytes+=status->gpuTransferBytes;
                stats_.strataStreamingUploadDeferrals+=status->uploadBudgetDeferrals;
                stats_.strataStreamingFenceDeferrals+=status->fenceReuseDeferrals;
                stats_.strataDiskBytesRead+=status->diskBytesRead;
                stats_.strataDiskReadFailures+=status->diskFailures;
                stats_.strataDiskPending+=status->diskPending;
                stats_.strataDiskReadyBytes+=status->diskReadyBytes;
            }
        }
    }
    stats_.strataStreamPeakGpuTransferBytes=std::max(
        stats_.strataStreamPeakGpuTransferBytes,
        stats_.strataStreamFrameGpuTransferBytes);
    stats_.strataStreamPeakActiveIndexBytes=std::max(
        stats_.strataStreamPeakActiveIndexBytes,
        stats_.strataStreamingIndexBytes);
    if(strataPortableDevice_){
        const auto shared=strata::deviceIndexBudgetStats(*strataPortableDevice_);
        stats_.strataDeviceReservedIndexBytes=shared.reservedBytes;
        stats_.strataDeviceIndexLeases=shared.activeLeases;
        stats_.strataDeviceFrameUploadedBytes=shared.currentFrameUploadedBytes;
        stats_.strataDeviceFrameUploadRemainingBytes=shared.frameUploadRemainingBytes;
    }
    const auto io=strata::sharedDiskIoStats();
    stats_.strataSharedIoWorkers=io.workers;
    stats_.strataSharedIoQueuedJobs=io.queuedJobs;
    stats_.strataSharedIoActiveJobs=io.activeJobs;
    stats_.strataSharedCpuReadyBytes=io.readyBytes;
    stats_.strataSharedCpuReadyBudgetBytes=io.readyLimitBytes;
#else
    (void)r;
#endif
}
void GeometryScene::draw(Renderer3D& r){
    // STRATA triangle budget: steer the pixel-error threshold so the STRATA
    // triangles of a frame stay near the budget. Triangle count falls roughly
    // with the square of the threshold, so step by the square root of the
    // overshoot, damped and clamped (1x..32x the authored threshold).
    if(strataTriangleBudget_ && !strataInsetPass_){
        const uint64_t frameTris=stats_.strataPortableTriangles-strataBudgetLastTotal_;
        strataBudgetLastTotal_=stats_.strataPortableTriangles;
        if(frameTris){
            const double ratio=double(frameTris)/double(strataTriangleBudget_);
            double step=1.0;
            if(ratio>1.03)step=std::min(1.25,std::sqrt(ratio));
            else if(ratio<0.85)step=std::max(0.9,std::sqrt(ratio));
            strataBudgetScale_=float(std::clamp(double(strataBudgetScale_)*step,1.0,32.0));
        }
    }
    stats_.strataErrorScale=strataBudgetScale_;
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    {
        uint64_t bytes=0;
        for(const auto& [instance,buffer]:strataCompactBuffers_) if(buffer) bytes+=buffer->capacityBytes;
        stats_.strataSelectionGpuBytes=bytes;
    }
#endif
    stats_.submitted=0;stats_.surfaceProxiesSubmitted=0;
    strataDebugStats_.reset();
    ++strataDrawFrame_;
    // Shared STRATA aliases only borrow source buffers; drop selections no
    // copy has used for a few seconds (bounded CPU memory, no GPU release).
    if((strataDrawFrame_&63)==0)
        for(auto it=strataSharedAliases_.begin();it!=strataSharedAliases_.end();)
            it=it->second.lastFrame+240<strataDrawFrame_?strataSharedAliases_.erase(it):std::next(it);
    stats_.strataPortableSharedModels=uint32_t(strataSharedAliases_.size());
    updateStrataStreamingBeforeDraw(r);
    if(hlod_&&!groups_.empty())drawGroup(r,0);
    else for(auto& i:instances_)drawInstance(r,i);
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    // Count both accepted and declined GPU temporal submissions. Rendering
    // uses no readback and does not depend on these diagnostic counters.
    if(strataGpuCulling_){
        const auto temporal=strataGpuCulling_->temporalStats();
        stats_.strataGpuTemporalHistories=uint32_t(temporal.histories);
        stats_.strataGpuTemporalBytes=temporal.retainedBytes;
        stats_.strataGpuTemporalSubmissions=temporal.submitted;
        stats_.strataGpuTemporalColdStarts=temporal.coldStarts;
        stats_.strataGpuTemporalRefused=temporal.refused;
    }
#endif
}
bool GeometryScene::tryStrataVulkanGpuDraw(
        Renderer3D& renderer,const Instance& instance,ModelDraw& draw) {
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if(!strataVulkanGpuIndirectEnabled_ ||
       !strataStagingEnabled_ || !strataGpuMetadataEnabled_ ||
       !strataGpuCullingEnabled_ || !strataStaging_ ||
       !strataGpuMetadata_ || !strataGpuDevice_ ||
       strataGpuDevice_->info().backend!=gfx::Backend::Vulkan ||
       !strataGpuDevice_->gpuHealthy() ||
       !strataGpuDevice_->supportsCompute() ||
       !strataGpuDevice_->supportsMultiDrawIndirect() ||
       !strataGpuDevice_->frameSerial() ||
       draw.model!=instance.model ||
       !instance.model || !instance.model->vb.valid() ||
       renderer.sceneWidth()<=0 || renderer.sceneHeight()<=0)
        return false;
    // Acceptance switches must stay OFF until native source-revision-matched
    // GPU selection AND Renderer3D raster A/B parity have both passed.
    strata::RenderRouteGate gate{};
    gate.requested=true;
    gate.cpuGpuSelectionParityVerified=
        strataVulkanGpuSelectionParityVerified_;
    gate.rasterParityVerified=strataVulkanGpuRasterParityVerified_;
    gate.rendererOwnsSubmission=true;
    gate.sourceResident=true;
    gate.sourceAndMaterialSafe=true;
    if(strata::chooseExclusiveRenderRoute(gate)!=strata::RenderRoute::Strata ||
       (strataLodHysteresisFraction_>0.f &&
        !strataGpuTemporalParityVerified_))
        return false; // CPU/portable retains uncertified warm LOD cases
    // One GPU dispatch per primitive AND scene instance. These independent
    // packets share source materials/VB/IB but never share a U0 transform.
    // This is a bounded first production tier: too many concurrent source
    // instances stay on CPU/Legacy without using stale GPU command slots.
    size_t sourceInstances=0;
    uint32_t instanceToken=0;
    for(size_t index=0;index<instances_.size();++index) {
        if(instances_[index].model==instance.model)++sourceInstances;
        if(&instances_[index]==&instance)instanceToken=uint32_t(index+1);
    }
    if(!instanceToken || !sourceInstances || sourceInstances>16)return false;
    // Pass-specific GPU selection cannot be reused for shadows or
    // reflection cameras. Preserve all existing Legacy/portable passes.
    if(renderer.lights.reflectionsWanted() &&
       !strataVulkanGpuReflectionParityVerified_)return false;
    const bool sunShadow=renderer.shadowQuality()!=ShadowQuality::Off &&
                         renderer.shadowMapSize()>0;
    if(sunShadow && !strataVulkanGpuSunShadowParityVerified_)
        return false;

    for(const auto& [key,model]:models_){
        if(model.get()!=instance.model)continue;
        const auto* staged=strataStaging_->find(key);
        if(!staged || !staged->sourceSeamsCertified ||
           staged->primitives.empty() ||
           !std::all_of(staged->primitives.begin(),
                        staged->primitives.end(),
                        [](const strata::ModelPrimitiveAsset& primitive){
                            return primitive.geometry.seamCertified;
                        }) ||
           staged->primitives.size()>16 ||
           instance.model->bindWorld.size()!=1 ||
           instance.model->nodes.size()!=1)break;
        strata::GpuMetadataScene::IndexBinding binding{};
        if(!strataGpuMetadata_->indexBinding(key,*instance.model,binding))
            break;
        const auto metadata=strataGpuMetadata_->find(key,*instance.model);
        if(!metadata.valid())break;
        const mat4 selectedWorld=
            instance.world*instance.model->bindWorld[0];
        uint64_t globalTemporalNodes=0;
        for(const auto& primitive:staged->primitives)
            globalTemporalNodes+=primitive.geometry.nodes.size();
        // A new source revision/policy creates a new camera history
        // identity. Never borrow temporal data from a different model
        // upload or an earlier hysteresis setting.
        uint64_t temporalRevision=0xcbf29ce484222325ull;
        const auto mixRevision=[&](uint64_t value) {
            temporalRevision^=value;
            temporalRevision*=1099511628211ull;
        };
        mixRevision(instance.model->geometryRevision);
        mixRevision(instance.model->materialRevision);
        mixRevision(strataGpuLodPolicyEpoch_);
        if(!temporalRevision)temporalRevision=1;

        struct Planned {
            strata::GpuPassConfig view{};
            strata::VulkanIndirectContract contract{};
            uint32_t ordinal=0;
            GpuIndexedPass pass=GpuIndexedPass::MainOpaque;
        };
        std::vector<Planned> plan;
        plan.reserve(staged->primitives.size()*(sunShadow?2u:1u));
        std::string reason;
        bool accepted=true;
        // Preflight ALL material/primitive identities before the first GPU
        // job so a partially unsafe model never switches just one material.
        for(size_t i=0;i<staged->primitives.size();++i){
            const auto& primitive=staged->primitives[i];
            strata::GpuPassConfig view{};
            view.viewProjection=renderer.viewProj();
            view.width=renderer.sceneWidth();
            view.height=renderer.sceneHeight();
            view.clipDepthZeroToOne=renderer.clipDepthZeroToOne();
            view.frustumCull=true;
            view.errorThresholdPixels=errorPixels_;
            view.maxOutputClusters=uint32_t(std::min<size_t>(2048,
                std::max<size_t>(16,primitive.geometry.nodes.size())));
            if(strataLodHysteresisFraction_>0.f){
                if(globalTemporalNodes>262144) {
                    accepted=false;
                    break; // bounded memory: use CPU/Legacy
                }
                view.hysteresisFraction=strataLodHysteresisFraction_;
                view.temporalNodeCount=uint32_t(globalTemporalNodes);
                view.temporalPassId=1u; // main camera
                view.temporalRevision=temporalRevision;
            }
            Planned entry{};
            entry.ordinal=uint32_t(i);
            if(!strata::prepareVulkanIndirectPrimitiveContract(
                 *strataGpuDevice_,*instance.model,*staged,binding,
                 entry.ordinal,view,false,entry.contract,entry.view,&reason)){
                accepted=false;
                break;
            }
            plan.push_back(entry);
            if(sunShadow){
                Planned shadow{};
                shadow.ordinal=uint32_t(i);
                shadow.pass=GpuIndexedPass::SunShadow;
                auto shadowView=view;
                shadowView.viewProjection=renderer.lightViewProj();
                shadowView.width=renderer.shadowMapSize();
                shadowView.height=renderer.shadowMapSize();
                shadowView.frustumCull=true;
                if(shadowView.hysteresisFraction>0.f)
                    shadowView.temporalPassId=2u; // sun shadow
                if(!strata::prepareVulkanIndirectPrimitiveContract(
                    *strataGpuDevice_,*instance.model,*staged,binding,
                    shadow.ordinal,shadowView,false,shadow.contract,
                    shadow.view,&reason)){
                    accepted=false;
                    break;
                }
                plan.push_back(shadow);
            }
        }
        if(!accepted)break;
        if(!strataGpuCulling_)
            strataGpuCulling_ = std::make_shared<strata::GpuClusterCulling>();

        std::vector<GpuIndexedSelectionPacket> packets;
        std::vector<GpuIndexedSelectionPacket> shadowPackets;
        packets.reserve(staged->primitives.size());
        if(sunShadow)shadowPackets.reserve(staged->primitives.size());
        for(const auto& entry:plan){
            const auto gpuInstance=strata::makeGpuSceneInstance(
                selectedWorld,entry.ordinal,instanceToken,true);
            strata::GpuSelectionSubmission submission{};
            ++stats_.strataVulkanGpuCandidates;
            if(!strataGpuCulling_->dispatch(*strataGpuDevice_,metadata,
                {gpuInstance},entry.view,strataGpuDevice_->frameSerial(),
                submission) ||
               !strata::validateVulkanIndirectSubmission(
                *strataGpuDevice_,*instance.model,
                entry.contract,submission,&reason)){
                accepted=false;
                break;
            }
            GpuIndexedSelectionPacket packet{};
            packet.commands=submission.indirect;
            packet.vertex=entry.contract.vertex;
            packet.index=entry.contract.index;
            packet.selectedWorld=selectedWorld;
            packet.viewProjection=entry.view.viewProjection;
            packet.viewWidth=entry.view.width;
            packet.viewHeight=entry.view.height;
            packet.pass=entry.pass;
            packet.frameSerial=entry.contract.expectedFrameSerial;
            packet.geometryRevision=entry.contract.geometryRevision;
            packet.materialRevision=entry.contract.materialRevision;
            packet.commandCapacity=submission.capacity;
            packet.sourceFirstIndex=
                entry.contract.originalFirstIndex-
                instance.model->gpuFirstIndex;
            packet.sourceIndexCount=entry.contract.originalIndexCount;
            packet.sourceBaseVertex=entry.contract.pooledBaseVertex;
            const auto& stagedPrimitive=staged->primitives[entry.ordinal];
            packet.sourceMesh=stagedPrimitive.mesh;
            packet.sourcePrimitive=stagedPrimitive.primitive;
            packet.expectedMaterial=entry.contract.expectedMaterial;
            packet.metadata=metadata;
            packet.selector=strataGpuCulling_.get();
            packet.instanceToken=instanceToken;
            packet.errorThresholdPixels=entry.view.errorThresholdPixels;
            packet.lodHysteresisFraction=entry.view.hysteresisFraction;
            packet.temporalNodeCount=entry.view.temporalNodeCount;
            packet.temporalRevision=entry.view.temporalRevision;
            packet.allowSpotShadow=
                strataVulkanGpuSpotShadowParityVerified_;
            packet.allowReflection=
                strataVulkanGpuReflectionParityVerified_;
            packet.exclusive=true;
            if(entry.pass==GpuIndexedPass::SunShadow)
                shadowPackets.push_back(packet);
            else packets.push_back(packet);
        }
        if(!accepted ||
           packets.size()!=staged->primitives.size() ||
           (sunShadow &&
            shadowPackets.size()!=staged->primitives.size()))break;
        // No second geometry owner: the same ModelDraw and Renderer3D
        // ModelItems still bind each original material and world transform.
        draw.gpuIndexed={};
        draw.gpuIndexedPrimitives=std::move(packets);
        draw.gpuShadowPrimitives=std::move(shadowPackets);
        draw.flags|=kDrawStatic;
        draw.geometry={};
        draw.staticCache=nullptr;
        draw.persistentId=0;
        stats_.strataVulkanGpuPrepared+=
            draw.gpuIndexedPrimitives.size()+draw.gpuShadowPrimitives.size();
        if(strataGpuCulling_){
            const auto temporal=strataGpuCulling_->temporalStats();
            stats_.strataGpuTemporalHistories=uint32_t(temporal.histories);
            stats_.strataGpuTemporalBytes=temporal.retainedBytes;
            stats_.strataGpuTemporalSubmissions=temporal.submitted;
            stats_.strataGpuTemporalColdStarts=temporal.coldStarts;
            stats_.strataGpuTemporalRefused=temporal.refused;
        }
        return true;
    }
    ++stats_.strataVulkanGpuDeclined;
#else
    (void)renderer;(void)instance;(void)draw;
#endif
    return false;
}

#if defined(AIVERSE_STRATA_STAGING_ENABLED)
// Identity of a portable alias: which index ranges of which meshes it draws.
// Same source + same signature => identical draw, safe to share.
static uint64_t strataAliasSignature(const Model& alias){
    uint64_t h=1469598103934665603ull;
    const auto mix=[&](uint64_t v){h^=v;h*=1099511628211ull;};
    mix(alias.ib.valid()?uint64_t(alias.ib.id):0);mix(alias.gpuFirstIndex);
    for(size_t m=0;m<alias.meshes.size();++m){
        mix(m);mix(alias.meshes[m].primitives.size());
        for(const auto& p:alias.meshes[m].primitives){mix(p.firstIndex);mix(p.indexCount);mix(uint64_t(uint32_t(p.material)));}
    }
    return h;
}
#endif
void GeometryScene::drawInstance(Renderer3D& r,const Instance& i){
    ModelDraw d;d.model=i.model;d.world=i.world;d.geometry=geometryEnabled_?i.policy:GeometryPolicy{};d.staticCache=cacheEnabled_?&i.cache:nullptr;d.layers=i.layers;
    if(i.surfaceProxy&&surfacesEnabled_){
        const auto mn=i.min,mx=i.max;
        if(geometryErrorPixels(r.viewProj(),mn,mx,length(mx-mn),r.sceneWidth(),r.sceneHeight(),r.clipDepthZeroToOne())<=i.proxyPixels){
            ++stats_.surfaceProxiesSubmitted;d.model=i.surfaceProxy;d.staticCache=cacheEnabled_?&i.proxyCache:nullptr;d.geometry.clusters=false;d.geometry.shadowClusters=false;d.geometry.occluder=false;
        }
    }
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    // The Vulkan GPU route does NOT create a second ModelDraw. It only
    // annotates this single source draw for Renderer3D's own main pass.
    // An unsupported view/source retains the normal Legacy/portable path.
    const bool gpuStrataPrepared=i.strataRoute && tryStrataVulkanGpuDraw(r,i,d);
    // No duplicate geometry: preview only; always execute the exact legacy
    // draw below. Source proxies and HLOD substitutes are intentionally
    // excluded from this diagnostic path. Source key is borrowed only for
    // the synchronous preview call; no Renderer3D packet stores it.
    if (strataRenderPreviewEnabled_ && strataStagingEnabled_ &&
        strataGpuMetadataEnabled_ && strataStaging_ &&
        strataGpuMetadata_ && d.model == i.model) {
        for (const auto& [key, model] : models_) {
            if (model.get() != d.model) continue;
            const auto* prepared = strataStaging_->find(key);
            if (prepared && strataGpuMetadata_->find(key, *d.model).valid())
                r.previewStrataModel(d, *prepared, *strataGpuMetadata_, key);
            break;
        }
    }
#endif
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if (!gpuStrataPrepared && strataPortableRenderingEnabled_ && i.strataRoute &&
        d.model == i.model &&
        !strataPortableSources_.empty()) {
        const auto staged=strataPortableSources_.find(d.model);
        if (staged != strataPortableSources_.end() && staged->second &&
            r.sceneWidth() > 0 && r.sceneHeight() > 0) {
            std::vector<strata::PortablePassView> views;
            views.push_back({r.viewProj(), r.sceneWidth(), r.sceneHeight(),
                             r.clipDepthZeroToOne(), errorPixels_*strataBudgetScale_});
            if (strataShadowErrorScale_ > 0 && r.shadowQuality() != ShadowQuality::Off &&
                r.shadowMapSize() > 0)
                views.push_back({r.lightViewProj(), r.shadowMapSize(),
                                 r.shadowMapSize(), r.clipDepthZeroToOne(),
                                 errorPixels_*strataBudgetScale_*strataShadowErrorScale_});
            // A planar reflection camera is chosen after drawModel(), by
            // renderReflection(). To guarantee no missing reflected detail,
            // use the finest resident frontier whenever water reflections
            // might consume this ModelDraw in the current frame.
            const bool forceFinest = r.lights.reflectionsWanted() && strataReflectionFinest_;
            uint64_t planKey=1469598103934665603ull;
            {
                auto mix=[&](const void* p,size_t n){const auto* b=static_cast<const unsigned char*>(p);for(size_t k=0;k<n;++k){planKey^=b[k];planKey*=1099511628211ull;}};
                for(const auto& v:views){mix(v.viewProjection.m,sizeof(v.viewProjection.m));mix(&v.width,sizeof v.width);mix(&v.height,sizeof v.height);mix(&v.errorThresholdPixels,sizeof v.errorThresholdPixels);const uint8_t z=v.clipDepthZeroToOne;mix(&z,1);}
                mix(i.world.m,sizeof(i.world.m));
                const uint8_t f=forceFinest;mix(&f,1);
                mix(&strataLodHysteresisFraction_,sizeof strataLodHysteresisFraction_);
            }
            // (Debug views read the source's last frontier: always re-plan then.)
            if(auto last=strataLastPlans_.find(&i);last!=strataLastPlans_.end() && last->second.key==planKey &&
               !staged->second->partial() && !strataDebugView_.enabled){
                auto alias=strataSharedAliases_.find({i.model,last->second.signature});
                if(alias!=strataSharedAliases_.end() && alias->second.model){
                    if(last->second.ib.valid())alias->second.model->ib=last->second.ib;
                    alias->second.lastFrame=strataDrawFrame_;
                    d.model=alias->second.model.get();
                    d.flags |= kDrawStatic;
                    d.staticCache=nullptr;
                    d.persistentId=0;
                    if(!strataInsetPass_){
                        ++stats_.strataPortableDraws;
                        ++stats_.strataPlanReuses;
                        for(const auto& mesh:d.model->meshes)
                            for(const auto& p:mesh.primitives)
                                stats_.strataPortableTriangles+=p.indexCount/3;
                    }
                    goto strataDrawn;
                }
            }
            {
            auto& slot=strataPortableDrawModels_[&i];
            if (!slot) slot=std::make_unique<Model>();
            std::string error;
            strata::PortableLodHistory* history=nullptr;
            if(strataLodHysteresisFraction_>0.f){
                auto& owned=strataPortableLodHistories_[&i];
                if(!owned)
                    owned=std::make_shared<strata::PortableLodHistory>();
                history=owned.get();
            }
            const uint64_t oldTransitions=
                staged->second->stats().hysteresisTransitions;
            const ModelDraw legacyPart=d; // original source, for unstaged parts
            auto& compactSlot=strataCompactBuffers_[&i];
            if (!compactSlot) compactSlot=std::make_shared<strata::PortableCompactBuffer>();
            const uint64_t uploadsBefore=compactSlot->uploads, reusesBefore=compactSlot->reuses;
            if (staged->second->prepareModel(i.world, views, forceFinest,
                    *slot, &error,history,strataLodHysteresisFraction_,
                    compactSlot.get())) {
                stats_.strataCompactUploads+=compactSlot->uploads-uploadsBefore;
                stats_.strataCompactReuses+=compactSlot->reuses-reusesBefore;
                // Partial source (e.g. blended glass not staged): draw only
                // those materials from the original model, then the alias.
                if (staged->second->partial()) {
                    ModelDraw rest=legacyPart;
                    rest.materialMask=&staged->second->legacyMaterialMask();
                    rest.staticCache=nullptr;
                    rest.persistentId=0;
                    r.drawModel(rest);++stats_.submitted;
                }
                stats_.strataLodHysteresisTransitions+=
                    staged->second->stats().hysteresisTransitions-
                    oldTransitions;
                // Copies that selected the same clusters draw ONE shared
                // alias, so Renderer3D instances them (same Primitive*).
                const uint64_t signature=strataAliasSignature(*slot);
                auto& shared=strataSharedAliases_[{i.model,signature}];
                if(!shared.model)
                    shared.model=std::make_unique<Model>(std::move(*slot));
                else {
                    ++stats_.strataPortableSharedReuses;
                    // Same clusters => identical index content; take THIS
                    // instance's live buffer (the first owner's buffer may
                    // already be retired after its own selection changed).
                    if(slot->ib.valid()) shared.model->ib=slot->ib;
                }
                shared.lastFrame=strataDrawFrame_;
                strataLastPlans_[&i]={planKey,signature,shared.model->ib};
                d.model=shared.model.get();
                d.flags |= kDrawStatic; // eligible rigid Strata, Vulkan MDI may batch it
                d.staticCache=nullptr;
                d.persistentId=0; // no stale original static-render packets
                // The actual draw is STILL authored only by Renderer3D.
                // Do not draw the original source after a successful switch.
                if(!strataInsetPass_)++stats_.strataPortableDraws;
                if(!strataInsetPass_)for(const auto& mesh:d.model->meshes)
                    for(const auto& p:mesh.primitives)
                        stats_.strataPortableTriangles+=p.indexCount/3;
                stats_.strataPortableClusters+=
                    d.model->meshes.empty() ? 0u :
                    uint64_t(std::accumulate(
                        d.model->meshes.begin(),d.model->meshes.end(),size_t(0),
                        [](size_t n,const MeshDef& mesh) {
                            return n+mesh.primitives.size();
                        }));
            }else{
                ++stats_.strataPortableFallbacks;
                // Failure is atomic: output was preserved, and source model
                // always remains the only drawn object in this epoch.
                strataLastPlans_.erase(&i);
            }
            }
            strataDrawn:;
        }
    }
#endif
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    // The portable alias is NOT a new identity; preserve its history.
    // GPU route and an HLOD/surface substitute have no CPU LOD frontier
    // to reuse. Failure to prepare an alias does NOT modify history.
    if(strataLodHysteresisFraction_==0.f || gpuStrataPrepared ||
       (i.surfaceProxy && d.model==i.surfaceProxy))
        strataPortableLodHistories_.erase(&i);
    stats_.strataLodHysteresisHistories=
        uint32_t(strataPortableLodHistories_.size());
#endif
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if(strataDebugView_.enabled && !strataDebugSuppressed_)drawStrataDebug(r,i,d,gpuStrataPrepared);
#endif
    r.drawModel(d);++stats_.submitted;
}

#if defined(AIVERSE_STRATA_STAGING_ENABLED)
namespace {
// Immediate overlay sink: Renderer3D owns the actual line/triangle passes.
class RendererDebugSink final : public strata::DebugSink {
public:
    // rightHalf: keep only primitives entirely in the right half of the
    // screen (side-by-side shots: plain image left, debug view right).
    RendererDebugSink(Renderer3D& r,bool rightHalf) : r_(r),rightHalf_(rightHalf),clip_(r.viewProj()) {}
    void line(float3 a,float3 b,uint32_t c) override { if(keep(a)&&keep(b)) r_.line(a,b,c); }
    void triangle(float3 a,float3 b,float3 c,uint32_t col) override {
        if(keep(a)&&keep(b)&&keep(c)) r_.triangle(a,b,c,col);
    }
private:
    bool keep(float3 p) const {
        if(!rightHalf_)return true;
        const float4 q=clip_*float4{p.x,p.y,p.z,1};
        return q.w>0 && q.x>=0;
    }
    Renderer3D& r_;
    bool rightHalf_=false;
    mat4 clip_;
};
} // namespace
#endif

bool GeometryScene::setStrataDebugView(const strata::DebugViewOptions& o){
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    if(!strata::validDebugViewOptions(o))return false;
    strataDebugView_=o;
    for(auto& [model,source]:strataPortableSources_)
        if(source)source->setDebugCaptureEnabled(o.enabled);
    if(!o.enabled)strataDebugStats_.reset();
    return true;
#else
    (void)o;
    return false; // STRATA not compiled into this runtime
#endif
}

uint64_t GeometryScene::strataDebugGeometryBytes() const {
    uint64_t bytes=0;
    for(const auto& [model,geometry]:strataDebugGeometry_)
        if(geometry)bytes+=geometry->bytes();
    return bytes;
}

void GeometryScene::drawStrataDebug(Renderer3D& r,const Instance& i,
                                    const ModelDraw& d,bool gpuRoute){
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
    const auto& o=strataDebugView_;
    auto& st=strataDebugStats_;
    RendererDebugSink sink(r,strataDebugRightHalf_);
    const auto source=strataPortableSources_.find(i.model);
    const bool staged=source!=strataPortableSources_.end() && source->second;
    const auto slot=strataPortableDrawModels_.find(&i);
    const bool portable=!gpuRoute && staged &&
        slot!=strataPortableDrawModels_.end() && slot->second &&
        d.model!=i.model;
    strata::DebugRoute route=strata::DebugRoute::Legacy;
    if(gpuRoute)route=strata::DebugRoute::StrataVulkanGpu;
    else if(portable)route=strata::DebugRoute::StrataPortable;
    else if(staged)route=strata::DebugRoute::LegacyFallback;
    if(route==strata::DebugRoute::Legacy && !o.includeLegacy)return;
    ++st.instances;
    if(o.instanceBounds && i.min.x<=i.max.x)
        strata::emitDebugBox(sink,mat4::identity(),i.min,i.max,
                             strata::debugRouteColor(route),o,st);
    // The GPU route has no CPU frontier (no readback): bounds only.
    if(!portable || !source->second->debugAsset())return;
    const auto geometry=strataDebugGeometry_.find(i.model);
    strata::emitSelectedClusters(sink,*source->second->debugAsset(),*i.model,
        geometry!=strataDebugGeometry_.end() ? geometry->second.get() : nullptr,
        i.world,source->second->debugSelection(),o,st);
#else
    (void)r;(void)i;(void)d;(void)gpuRoute;
#endif
}

}
