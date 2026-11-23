#include "GameView.h"
#include "GameSelectionPresentation.h"
#include "engine/view/View.h"
#include "engine/view/GeometryBounds.h"
#include "engine/view/Frustum.h"
#include "engine/view/SceneLayers.h"
#include "engine/view/SceneLayerProviders.h"
#include "../../roads/RoadView.h"
#include "GameShoreView.h"
#include "engine/render/OwnerPrepareCommit.h"
#include "engine/render/CameraModelSnapshot.h"
#include "../../data/JsonCompose.h"
#include "../../game/GameCore.h"
#include "../../game/Arsenal.h"
#include "../../game/TopDown.h"
#include "imgui.h"
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <unistd.h>
#endif
#include "../../platform/Platform.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include <chrono>
#include <cstdlib>
#include <exception>

namespace aiverse::game {
void GameView::clearSharedAnimationProfile() {
    if(genericView_)genericView_->clearAnimationProfile();
}
Json GameView::sharedAnimationProfile(uint64_t entityKey,uint64_t worldEpoch,const std::string& action) {
    Json r=Json::object();r.set("available",Json::fromBool(false));
    if(action=="disable"&&!framePublication_.active()&&genericView_&&genericView_->profileMatches(entityKey,worldEpoch)){
        genericView_->clearAnimationProfile();r.set("reason",Json::string("Profiling disabled"));return r;
    }
    if(framePublication_.active()||!framePublication_.published().ready()||!genericView_||
       !sharedAnimationGraphDiagnostic(entityKey,worldEpoch).getBool("available")){
        r.set("reason",Json::string("Current committed common View instance unavailable"));return r;
    }
    if(action=="enable"){
        if(!genericView_->enableAnimationProfile(entityKey,worldEpoch))return r;
    }else if(action=="reset"){
        if(!genericView_->resetAnimationProfile(entityKey,worldEpoch))return r;
    }else if(action=="disable")return r;
    else if(action!="read")throw std::invalid_argument("unknown live profile action");
    return genericView_->animationProfileReport(entityKey,worldEpoch);
}
Json GameView::sharedAnimationGraphDiagnostic(uint64_t entityKey,uint64_t worldEpoch,graph_telemetry::History* history) const {
    Json unavailable=Json::object();unavailable.set("available",Json::fromBool(false));
    const auto it=anims_.find(entityKey);
    if(!genericView_||it==anims_.end()||!it->second.genericViewPose||
       it->second.genericViewId!=entityKey||it->second.seen!=frame_||
       !it->second.last.entity.valid()||it->second.last.entity.packed()!=entityKey)
        return unavailable;
    auto sample=genericView_->animationTelemetry(entityKey,worldEpoch);
    if(!sample)return unavailable;
    if(history){
        sample->value.set("historyCaptured",Json::fromBool(history->publish(sample->identity,sample->sequence,sample->value)));
        sample->value.set("history",history->read(sample->identity));
    }
    return std::move(sample->value);
}

namespace {
// Resident memory of this process (MB), for the benchmark panel.
double processResidentMB() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return double(info.resident_size) / 1048576.0;
    return 0;
#elif defined(__linux__) || defined(__ANDROID__)
    long pages = 0, resident = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return double(resident) * double(sysconf(_SC_PAGESIZE)) / 1048576.0;
#else
    return 0;
#endif
}
}


using namespace render;

namespace {
float3 toF(const Vec3& v) { return {float(v.x.toDouble()), float(v.y.toDouble()), float(v.z.toDouble())}; }

// Conservative content fingerprints for cross-call private GPU candidates.
// Identity pointers alone cannot detect editor mutation in-place. The hashes
// are computed synchronously from host inputs without allocating storage.
struct SurfaceLeaseHash {
    uint64_t value=1469598103934665603ull;
    template<class T> void scalar(const T& item) noexcept {
        const auto* data=reinterpret_cast<const unsigned char*>(&item);
        for(size_t i=0;i<sizeof(T);++i) {
            value^=uint64_t(data[i]);
            value*=1099511628211ull;
        }
    }
    template<class T> void array(const std::vector<T>& items) noexcept {
        scalar(items.size());
        for(const auto& item:items)scalar(item);
    }
    void pos(float3 v) noexcept {scalar(v.x);scalar(v.y);scalar(v.z);}
};
uint64_t mapLeaseFingerprint(const MapGrid& g) noexcept {
    SurfaceLeaseHash h;
    h.scalar(g.w);h.scalar(g.h);
    h.array(g.flags);h.array(g.level);h.array(g.height);
    h.array(g.corner);h.array(g.dig);h.array(g.digMark);
    h.array(g.territory);h.array(g.wall);h.array(g.room);
    h.array(g.foundation);h.array(g.fieldRes);h.array(g.creep);
    h.scalar(g.fieldAmt.size());
    for(const auto& field:g.fieldAmt)h.scalar(field.raw);
    return h.value;
}
uint64_t sceneLeaseFingerprint(const GameScene& s) noexcept {
    SurfaceLeaseHash h;
    h.scalar(s.viewer);h.scalar(s.laneSignature);
    h.scalar(s.roads.size());
    for(const auto& road:s.roads) {
        h.pos(road.first);h.pos(road.second);
    }
    h.scalar(s.objects.size());
    for(const auto& o:s.objects) {
        h.scalar(o.entity.packed());
        h.scalar(o.type);h.scalar(o.garrison);
        h.pos(o.pos);
    }
    h.scalar(s.lanes!=nullptr);
    if(s.lanes) {
        h.scalar(s.lanes->size());
        for(const auto& lane:*s.lanes) {
            h.scalar(lane.a.packed());h.scalar(lane.b.packed());
            h.scalar(lane.offset);h.scalar(lane.points.size());
            for(const auto& point:lane.points) {
                h.scalar(point.x.raw);
                h.scalar(point.y.raw);
                h.scalar(point.z.raw);
            }
        }
    }
    return h.value;
}
uint64_t cameraLeaseFingerprint(const Renderer3D& r) {
    SurfaceLeaseHash h;
    const auto& matrix=r.viewProj();
    h.scalar(matrix);
    h.pos(r.camera().eye());
    const auto quality=r.materialQuality();
    const auto shadow=r.shadowQuality();
    h.scalar(quality);h.scalar(shadow);
    const bool trace=r.traceActive();
    h.scalar(trace);
    return h.value;
}
uint64_t visibilityLeaseFingerprint(
        const std::vector<uint8_t>* vision) noexcept {
    SurfaceLeaseHash h;
    const bool present=vision!=nullptr;
    h.scalar(present);
    if(vision)h.array(*vision);
    return h.value;
}
} // namespace

// Both topology publication and render-frame preflight must validate the
// same map bytes. Keep this one conservative source signature in one place.
uint64_t GameView::mapSourceFingerprint(const MapGrid& grid) noexcept {
    return mapLeaseFingerprint(grid);
}
uint64_t GameView::sceneSourceFingerprint(const GameScene& scene) noexcept {
    return sceneLeaseFingerprint(scene);
}
uint64_t GameView::cameraSourceFingerprint(
        const render::Renderer3D& renderer) {
    return cameraLeaseFingerprint(renderer);
}
uint64_t GameView::visibilitySourceFingerprint(
        const std::vector<uint8_t>* vision) noexcept {
    return visibilityLeaseFingerprint(vision);
}

// Owner-specific candidates stay private until the primary frame validates
// this one-shot lease and publishes the full OwnerPrepareCommit transaction.
struct GameView::PreparedSurfaceLease final {
    struct Part final:render::OwnerPrepareCommit::Participant {
        std::function<void()> stage,accept,discard;
        Part(std::function<void()> p,std::function<void()> c,
             std::function<void()> r)
            :stage(std::move(p)),accept(std::move(c)),
             discard(std::move(r)){}
        void prepare() override {stage();}
        void commit() noexcept override {accept();}
        void rollback() noexcept override {discard();}
    };
    const GameView* owner=nullptr;
    const gfx::Device* device=nullptr;
    const Renderer3D* renderer=nullptr;
    const GameScene* scene=nullptr;
    const MapGrid* map=nullptr;
    const std::vector<uint8_t>* vision=nullptr;
    double time=0;
    uint64_t terrainGeneration=0,frameStamp=0;
    uint64_t roadRevision=0,shoreRevision=0,groundRevision=0;
    uint64_t fogRevision=0,terrainRevision=0;
    uint64_t gridSignature=0,sceneSignature=0,cameraSignature=0;
    uint64_t visibilitySignature=0;
    bool tiles=false,fogSheetStaged=false,fogChanged=false;
    bool retained=true; // immediate leases cannot pass a later matches() check
    // Topology and ordinary scene-frame leases share the same one-shot
    // owner transaction and source-revision checks; only one can be pending.
    bool topology=false,preserveFogHistory=false;
    std::shared_ptr<TerrainRenderer::PreparedMap> topologyCandidate;
    std::shared_ptr<GameGroundFx::PreparedFrame> groundCandidate;
    std::shared_ptr<roads::RoadView::PreparedRebuild> roadCandidate;
    std::shared_ptr<GameShoreView::PreparedRebuild> shoreCandidate;
    std::unique_ptr<FogOfWar::PreparedSheet> fogSheet;
    std::unique_ptr<FogOfWar::PreparedUpdate> fogUpdate;
    std::shared_ptr<TerrainRenderer::PreparedFrame> terrainCandidate;
    std::vector<std::unique_ptr<Part>> parts;
    // Declared LAST, so its destructor rollback runs before Part and
    // private GPU-candidate destructors, even after a late prepare failure.
    std::unique_ptr<render::OwnerPrepareCommit> transaction;
    bool matches(const GameView& view,const Renderer3D& r,
                 const GameScene& s,double seconds,
                 const std::vector<uint8_t>* field,const MapGrid& g, bool advanced=false) const {
        return retained && owner==&view && device==view.dev_ &&
            renderer==&r && scene==&s && map==&g && vision==field &&
            time==seconds && frameStamp+uint64_t(advanced)==view.frame_ &&
            terrainGeneration==view.terrainGeneration_ &&
            tiles==bool(view.twoD_ && view.twoD_->hasTiles()) &&
            roadRevision==view.roads_->stateRevision() &&
            shoreRevision==view.shores_->stateRevision() &&
            groundRevision==view.groundFx_.stateRevision() &&
            fogRevision==view.fog_.stateRevision() &&
            terrainRevision==view.terrain_.stateRevision() &&
            gridSignature==mapLeaseFingerprint(g) &&
            sceneSignature==sceneLeaseFingerprint(s) &&
            cameraSignature==cameraLeaseFingerprint(r) &&
            visibilitySignature==visibilityLeaseFingerprint(field) &&
            !view.planets_.active();
    }
};

GameView::GameView() : roads_(std::make_unique<roads::RoadView>()), shores_(std::make_unique<GameShoreView>()) {}
GameView::~GameView() { pendingSurfaceLease_.reset(); }
void GameView::discardPendingSurfaceLease() noexcept {
    pendingSurfaceLease_.reset();
}

render::GpuOwnedMemoryStats GameView::ownedPresentationGpuMemoryStats(
        const gfx::Device& device) const {
        auto result=ownedModelGpuMemoryStats(device);
        const auto merge=[&](const render::GpuOwnedMemoryStats& owner) {
            result.textureKnownBytes=
                render::GpuOwnedMemoryCounter::saturatingAdd(
                    result.textureKnownBytes,owner.textureKnownBytes);
            result.bufferKnownBytes=
                render::GpuOwnedMemoryCounter::saturatingAdd(
                    result.bufferKnownBytes,owner.bufferKnownBytes);
            result.textures+=owner.textures;
            result.buffers+=owner.buffers;
            result.unknownTextures+=owner.unknownTextures;
            result.unknownBuffers+=owner.unknownBuffers;
            result.multiVersionBuffers+=owner.multiVersionBuffers;
        };
        // Include resident allocations even while the corresponding
        // terrain/planet presentation is hidden or inactive this frame.
        // GPU memory ownership ends at shutdown, not when drawn=false.
        merge(terrain_.ownedGpuMemoryStats());
        merge(planets_.ownedGpuMemoryStats());
        merge(groundFx_.ownedGpuMemoryStats());
        merge(planetGroundFx_.ownedGpuMemoryStats(device));
        merge(fog_.ownedGpuMemoryStats());
        merge(fieldView_.ownedGpuMemoryStats());
        merge(overlays_.ownedGpuMemoryStats());
        merge(groundCanvas_.ownedGpuMemoryStats());
        merge(paintLayers_.ownedGpuMemoryStats());
        if (twoD_) merge(twoD_->ownedGpuMemoryStats(device));
        if (roads_) merge(roads_->ownedGpuMemoryStats());
        if (shores_) merge(shores_->ownedGpuMemoryStats());
        render::GpuOwnedMemoryCounter direct(device);
        direct.buffer(projectile_.vb);direct.buffer(projectile_.ib);
        merge(direct.stats());
        return result;

}
// Called by client/editor/preview owner at the SAME explicit pre-render
// frame boundary used for ModelResourceCache and ImageRollbackPool. Never
// generate a GPU retirement token from inside GameView::draw(): the editor
// can draw the same View more than once inside one RHI frame.
size_t GameView::pendingModelRetirements() const {
    return resources_.pendingRetirements()+
           roads_->pendingGpuRetirements()+
           shores_->pendingGpuRetirements()+
           (pendingRoadRecovery_ ? pendingRoadRecovery_->pendingGpuRetirements() : 0)+
           (pendingShoreRecovery_ ? pendingShoreRecovery_->pendingGpuRetirements() : 0)+
           geometryScene_.pendingGpuRetirements()+
           overlays_.pendingGpuRetirements()+
           terrain_.pendingMeshGpuRetirements()+
           groundFx_.pendingMeshGpuRetirements()+
           fog_.pendingGpuRetirements();
}
bool GameView::collectRetiredModels() {
    if(!dev_)return false;
    const bool models=resources_.collectRetired(*dev_);
    const bool images=imageRollback_.collect(*dev_);
    // Always poll all three queues, even while a previous one is pending.
    roads_->collectRetired(*dev_);
    shores_->collectRetired(*dev_);
    const bool roads=roads_->pendingGpuRetirements()==0;
    const bool shores=shores_->pendingGpuRetirements()==0;
    // Failed staged owners remain alive until a host-proven idle shutdown.
    // Poll their token queues too so they cannot accumulate GPU retirements.
    if(pendingRoadRecovery_)pendingRoadRecovery_->collectRetired(*dev_);
    if(pendingShoreRecovery_)pendingShoreRecovery_->collectRetired(*dev_);
    const bool geometry=geometryScene_.collectRetired(*dev_);
    const bool overlays=overlays_.collectRetiredGpuResources(*dev_);
    const bool terrain=terrain_.collectRetiredMeshBuffers(*dev_);
    const bool ground=groundFx_.collectRetiredMeshBuffers(*dev_);
    const bool visibility=fog_.collectRetired(*dev_);
    const bool sprites=!twoD_ || twoD_->collectRetiredFrames(*dev_);
    return models && images && roads && shores && geometry && overlays &&
           terrain && ground && visibility && sprites;
}
void GameView::setRoadMap(const Json& map) {
    pendingSurfaceLease_.reset();
    framePublication_.invalidate();
    roads_->setMap(map);
    shores_->setMap(map);
}

float4 GameView::playerColor(int p) { return overlayPlayerColor(p); }   // palette shared with the overlays

bool GameView::init(gfx::Device& dev, const Rules& rules, FileReader reader) {
    pendingSurfaceLease_.reset();
    framePublication_.invalidate();
    // Terrain, fog, roads, shores and overlays also own RHI handles when a
    // sprite view is absent. Never rebind ANY live view owner to a foreign
    // device; sprite recovery performs its own stricter owner check below.
    if (dev_ && dev_ != &dev)
        throw std::logic_error("GameView reload: presentation resources belong to another GPU device");
    if(gpuDeviceLease_.device() && !gpuDeviceLease_.validFor(dev))
        throw std::logic_error("RTS view stale GPU device generation");
    if (pendingSpriteRecovery_) {
        if (!pendingSpriteRecovery_->shutdownAfterGpuIdle(dev))
            throw std::runtime_error("GameView reload: staged sprite GPU recovery pending");
        pendingSpriteRecovery_.reset();
    }
    // Previous failed road/shore staging is still a GPU ownership lease.
    // Cleanup may fail again: keep each pointer intact until retry succeeds.
    if(pendingRoadRecovery_) {
        pendingRoadRecovery_->shutdown(dev);
        pendingRoadRecovery_.reset();
    }
    if(pendingShoreRecovery_) {
        pendingShoreRecovery_->shutdown(dev);
        pendingShoreRecovery_.reset();
    }
    // Stage the replacement sprites before touching the live view. A failed
    // sprite pipeline/texture allocation must not delete its predecessor.
    // The idle barrier is established for *all* existing RHI owners (not
    // only the sprite view): fog, road/shore kits, terrain and overlays may
    // remain resident even when sprite initialization had returned false.
    if ((dev_ || twoD_) && !dev.waitForResourceIdle())
        throw std::runtime_error("GameView presentation reload: GPU idle barrier failed");
    const FileReader incomingRead = reader ? reader :
        FileReader([](const std::string& p,std::string& out) {
            return platform::readContentFile(p,out);
        });
    // Parse once for both the staged sprites and the remaining presentation
    // owners. Re-reading mutable files halfway through reload can produce a
    // sprite set configured differently from fog, overlays and roads.
    Json incomingGame, incomingRules;
    {
        std::string text, rulesPath="rules.json";
        try {
            if(incomingRead("game.json",text)) {
                incomingGame=Json::parse(text);
                rulesPath=incomingGame.getStr("rules","rules.json");
            }
        } catch (...) {}
        try {
            incomingRules=readComposedJson(rulesPath,
                [&](const std::string& p,std::string& out) {
                    return incomingRead(p,out);
                });
        } catch (...) {}
    }
    const SpriteDefaults incomingSpriteDefaults=
        parseSpriteDefaults(incomingRules["sprites"],incomingGame["sprites"]);
    // Prepare the fog look and overlay definitions before any replacement
    // GPU owner is published. An invalid CPU config can now fail while the
    // previous live view is still intact.
    const FogLook incomingFogLook=
        parseFogLook(incomingRules["fogOfWar"],incomingGame["fogOfWar"]);
    OverlaySet incomingOverlaySet=
        loadOverlaySet(rules,incomingRules["overlays"],incomingGame["overlays"]);
    // Parse the optional geometry descriptor before retiring the old sprite
    // owner; GPU geometry resources are still loaded/published later.
    Json incomingGeometry;
    bool incomingGeometryPresent=false;
    std::string incomingGeometryWarning;
    try {
        const std::string file=incomingGame.getStr("geometryScene");
        if (!file.empty()) {
            std::string bytes;
            if (!incomingRead(file,bytes))
                throw std::runtime_error("geometryScene: file not found: "+file);
            incomingGeometry=Json::parse(bytes);
            incomingGeometryPresent=true;
        }
    } catch(const std::exception& e) {
        incomingGeometryWarning=e.what();
    }
    // Parse non-GPU shared materials before publishing the staged sprite
    // view. Keep warnings until warnings_ is reset by the normal reload.
    Json incomingMaterials=Json::object();
    std::vector<std::string> incomingMaterialWarnings;
    {
        std::string text;
        if(incomingRead("materials.json",text)) {
            try { incomingMaterials=Json::parse(text)["materials"]; }
            catch(const std::exception& e) {
                incomingMaterialWarnings.push_back(std::string("materials.json: ")+e.what());
            }
            if(!incomingMaterials.isObject())incomingMaterials=Json::object();
        }
    }
    auto nextSprites=std::make_unique<Rts2DView>();
    bool spritesReady=false;
    try {
        spritesReady=nextSprites->init(
            dev,rules,contentBase_,incomingSpriteDefaults,deferredLoading_);
    } catch (...) {
        // A failed idle barrier may leave shader/texture/buffer handles
        // owned by this staging instance. Do not discard its recovery owner.
        if(nextSprites->ownsGpuResources())
            pendingSpriteRecovery_=std::move(nextSprites);
        throw;
    }
    if(!spritesReady) {
        if(nextSprites->ownsGpuResources())
            pendingSpriteRecovery_=std::move(nextSprites);
        return false;
    }
    // A successful staging pass is not yet the complete GameView transaction:
    // later terrain/model/overlay initialization can still fail.
    if(twoD_) {
        twoD_->shutdown(dev); // validated idle above; no intervening draw pass
        twoD_.reset();
    }
    twoD_=std::move(nextSprites);
    if(gpuDeviceLease_.device())
        resources_.bindResourceDevice(dev,gpuDeviceLease_);
    else resources_.bindResourceDevice(dev);
#ifndef __EMSCRIPTEN__
    resources_.preparation().reset();
#endif
    rules_ = &rules;
    dev_ = &dev;
    if (!fog_.ready()) fog_.init(dev);
    fieldView_.shutdown();
    fieldView_.init(dev);
    fieldDefsSeen_ = nullptr;
    read_ = incomingRead;
    {   // Reuse the exact parsed snapshot used for sprite staging.
        const Json& rj=incomingRules;
        fog_.setLook(incomingFogLook);
        spriteDefaults_ = incomingSpriteDefaults;   // same snapshot as staged sprites
        // game.json "lod": {"scale": s, "shadowScale": t} — projected-size
        // multipliers for model LOD selection (<1 = coarser levels sooner).
        {
            const Json& lod=incomingGame["lod"];
            auto num=[&](const char* k){
                const Json& v=lod[k];
                const double d=v.isNumber()?v.toDouble(1.0):1.0;
                return float(std::clamp(std::isfinite(d)?d:1.0,0.05,4.0));
            };
            modelLodScale_=lod.isObject()?num("scale"):1.f;
            shadowLodScale_=lod.isObject()?num("shadowScale"):1.f;
            const Json& rc=lod["reflectionCullPixels"];
            reflectionCullPixels_=lod.isObject()&&rc.isNumber()?float(std::clamp(rc.toDouble(0.0),0.0,4096.0)):0.f;
            // Optional LOD cross-fade width (0..0.49); absent keeps the host's.
            const Json& fd=lod["fade"];
            lodFadeWidth_=lod.isObject()&&fd.isNumber()?float(std::clamp(fd.toDouble(0.15),0.0,0.49)):-1.f;
        }
        fogMeshDirty_ = true;
        // overlays: built-ins + rules "overlays" + game.json "overlays" (by id)
        overlays_.initPrepared(rules,std::move(incomingOverlaySet),contentBase_);
        overlays_.initGpu(dev);
        // Prepare BOTH GPU kits before replacing the live map views. The
        // old road and shore instances remain drawable if either kit fails.
        auto nextRoad=std::make_unique<roads::RoadView>();
        auto nextShore=std::make_unique<GameShoreView>();
        nextRoad->inheritRuntimeStateForReload(*roads_);
        nextShore->inheritRuntimeStateForReload(*shores_);
        auto rollbackNewKits=[&]() {
            if(nextRoad && nextRoad->ownsGpuResources()) {
                try {nextRoad->shutdown(dev);}
                catch(...) {pendingRoadRecovery_=std::move(nextRoad);}
            }
            if(nextShore && nextShore->ownsGpuResources()) {
                try {nextShore->shutdown(dev);}
                catch(...) {pendingShoreRecovery_=std::move(nextShore);}
            }
        };
        try {
            nextRoad->init(dev,rj["roadTypes"],read_);
            nextShore->init(dev,rj["shoreTypes"],read_);
            // Complete device idle was established at the start of reload
            // when presentation resources were already resident.
            roads_->shutdown(dev);
            shores_->shutdown(dev);
        } catch(...) {
            // Failed staged initialization never discards its GPU owner when
            // cleanup cannot prove idle; retain it for the next host call.
            rollbackNewKits();
            throw;
        }
        roads_.swap(nextRoad);
        shores_.swap(nextShore);   // water shores (docs/water.md)
    }
    if (!resources_.models().count("shape:generic")) buildShapes(dev);
    looks_.clear();
    warnings_.clear();
    // A live rules/project reload must not stall the whole GPU or destroy
    // static/HLOD Models still referenced by an earlier rendered frame.
    // Final teardown continues to call the blocking shutdown barrier.
    if(!incomingGeometryWarning.empty())warnings_.push_back(incomingGeometryWarning);
    if(incomingGeometryPresent) {
        // AIVERSE_STRATA_DEBUG: STRATA debug view for acceptance screenshots.
        // "bounds" (cluster boxes), "wire" (cluster triangle edges), "fill"
        // (flat cluster colours); add ",lod" to colour by hierarchy depth.
        if (const char* e = std::getenv("AIVERSE_STRATA_DEBUG"); e && *e && *e != '0') {
            const std::string mode = e;
            render::strata::DebugViewOptions o;
            o.enabled = true;
            o.clusterBounds = mode.find("bounds") != std::string::npos;
            o.clusterWireframe = mode.find("wire") != std::string::npos;
            o.clusterFill = mode.find("fill") != std::string::npos;
            o.instanceBounds = false;
            o.includeLegacy = false;
            o.fillAlpha = 1.0f;
            o.maxTriangles = 2u * 1024u * 1024u; // whole frontier of all instances, not the first sphere only
            o.maxLines = 4u * 1024u * 1024u;
            if (mode.find("lod") != std::string::npos) o.color = render::strata::DebugColorMode::LodDepth;
            geometryScene_.setStrataDebugGeometryCaptureEnabled(o.clusterWireframe || o.clusterFill);
            geometryScene_.setStrataDebugView(o);
            // Presentation: picture-in-picture inset in the right corner by
            // default; "half" = debug on the right half of the screen; "full" =
            // debug over the whole frame.
            const bool full = mode.find("full") != std::string::npos, half = mode.find("half") != std::string::npos;
            geometryScene_.setStrataDebugRightHalf(half);
            strataDebugInset_ = !full && !half;
            geometryScene_.setStrataDebugSuppressed(strataDebugInset_);
        }
        try { geometryScene_.loadTransactional(dev,incomingGeometry,read_); }
        catch(const std::exception& e) {
            // A rejected replacement never empties the current geometry scene.
            warnings_.push_back(e.what());
        }
    } else if(incomingGeometryWarning.empty()) {
        // No descriptor was configured: explicitly remove old geometry.
        geometryScene_.clear(dev);
    }

    // Publish the same materials snapshot parsed before sprite staging.
    materialLib_=std::move(incomingMaterials);
    warnings_.insert(warnings_.end(),
                     incomingMaterialWarnings.begin(),incomingMaterialWarnings.end());
    for (auto& w : overlays_.set().warnings) warnings_.push_back(w);
    if(genericView_)genericView_->clear();
    anims_.clear(); streamingChoices_.clear(); reloadPlayback_.clear(); animationCache_.clear();
    tracers_.clear();
    looks_.resize(rules.types.size());
    actorRevealTypes_.resize(rules.types.size());
    for (size_t i = 0; i < rules.types.size(); ++i) {
        const auto& id = rules.types[i].id;
        actorRevealTypes_[i] = uint8_t(id.starts_with("city_interior_") || id == "shop_roof");
    }
    loadedTypes_.assign(rules.types.size(), false);
    requestedModels_.assign(rules.types.size(), {}); loadedModels_.assign(rules.types.size(), {});
    drawTypeStamps_.assign(rules.types.size(), 0);
    drawTypes_.clear();
    drawTypeEpoch_ = 0;
    if(deferredLoading_)for(size_t t=0;t<rules.types.size();++t)
        requestedModels_[t]=rules.types[t].view["streaming"].getStr("coarseModel");
    if (!deferredLoading_) for (size_t i = 0; i < rules.types.size(); ++i) loadType(uint16_t(i));
    // The replacement 2D view was initialized before mutating this view.
    lights_.init(rules);
    groundFx_.init(dev, rules, read_, contentBase_);
    terrain_.init(dev, rules, read_, contentBase_);
    planets_.init(dev, terrain_.look(), read_, contentBase_);
    return true;
}

void GameView::setGenericRetargetScratchBudgetBytes(size_t bytes) {
    genericRetargetScratchBudgetBytes_=bytes;
    if(genericView_)genericView_->setRetargetScratchBudgetBytes(bytes);
}
size_t GameView::genericRetargetScratchCapacityBytes() const noexcept {
    return genericView_?genericView_->retargetScratchCapacityBytes():0;
}

// Map water cells converted to NEREID (terrain.water.renderer "nereid",
// GameWaterNereid.h). The parsed scene is replaced only when the converted
// manifest changes; uploads happen here, before Renderer3D encodes the frame.
void GameView::drawNereidLegacyWater(Renderer3D& r,bool terrainReady) {
    auto manifest=terrainReady&&terrain_.look().render&&terrain_.mapSurfaceEnabled()
        ?terrain_.nereidWaterManifest():nullptr;
    const std::string signature=manifest?manifest->dump():std::string();
    if(!dev_ || (signature.empty()&&nereidLegacySignature_.empty()))return;
    try {
        if(signature!=nereidLegacySignature_) {
            auto parsed=manifest?world::parseNereidWaterManifest(*manifest):world::NereidManifest{};
            nereidLegacy_.setMaxUploadsPerFrame(parsed.maxUploadsPerFrame);
            nereidLegacy_.setMaxEstimatedUploadBytes(parsed.maxEstimatedUploadBytes);
            nereidLegacy_.setSurfaces(std::move(parsed.surfaces));
            nereidLegacySignature_=signature;
        }
        if(signature.empty())return;
        const auto eye=r.camera().eye();
        const auto fixed=[](float v){return Fixed::fromRaw(int64_t(std::llround(double(std::clamp(v,-1e6f,1e6f))*Fixed::kOne)));};
        const Vec3 observer{fixed(eye.x),fixed(eye.y),fixed(eye.z)};
        nereidLegacy_.update(*dev_,&observer);
        nereidLegacy_.draw(r,{},1);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"[water] NEREID map water: %s\n",e.what());
    }
}

void GameView::shutdown(gfx::Device& dev) {
    // Private unpublished GPU candidates must be released BEFORE the
    // full-device idle boundary or owner kit shutdown.
    pendingSurfaceLease_.reset();
#ifndef __EMSCRIPTEN__
    resources_.preparation().reset();
#endif
    // GeometryScene's standalone meshes and the road/shore/portrait/paint
    // owners may still be GPU-visible. The Model cache's own shutdown idle
    // barrier used to run AFTER GeometryScene had freed its GPU handles.
    // Establish a single full-device completion proof BEFORE tearing down
    // any presentation resource. A failed barrier leaves all owners intact.
    if(!dev.waitForResourceIdle())
        throw std::runtime_error("GameView shutdown: GPU idle barrier failed");
    framePublication_.invalidate();
    nereidLegacy_.shutdown(dev);
    nereidLegacySignature_.clear();
    if(pendingSpriteRecovery_) {
        // The barrier above proves safe retirement of the failed staged view.
        pendingSpriteRecovery_->shutdown(dev);
        pendingSpriteRecovery_.reset();
    }
    if(pendingRoadRecovery_) {
        pendingRoadRecovery_->shutdown(dev);
        pendingRoadRecovery_.reset();
    }
    if(pendingShoreRecovery_) {
        pendingShoreRecovery_->shutdown(dev);
        pendingShoreRecovery_.reset();
    }
    geometryScene_.shutdown(dev);
    // IMPORTANT: detach borrowed Model, graph, skin pose and material
    // references before the cache can release their CPU/GPU storage.
    // The lease stays live until the host successfully completes teardown.
    if(genericView_) { genericView_->clear(); genericView_.reset(); }
    anims_.clear(); streamingChoices_.clear(); reloadPlayback_.clear(); ghostAnim_=EntityAnim{};
    animationCache_.clear();
    looks_.clear(); actorRevealTypes_.clear(); loadedTypes_.clear(); tracers_.clear();
    // Failed appearance publications may leave uncommitted image GPU
    // handles queued behind their own completion tokens. Only a proven
    // full-device idle may drain unresolved/failed markers at shutdown.
    // Do not destroy the Device or Model cache while retirement is pending.
    if(!imageRollback_.drain(dev))
        throw std::runtime_error("RTS staged image rollback: GPU idle barrier failed");
    // All model GPU buffers and textures are now released by the same
    // per-host resource owner, after any asynchronous preparation joined.
    resources_.shutdown(dev);
    drawTypeStamps_.clear(); drawTypes_.clear(); drawTypeEpoch_ = 0;
    for (Mesh* m : {&projectile_}) { dev.destroyBuffer(m->vb); dev.destroyBuffer(m->ib); *m = Mesh{}; }
    terrain_.shutdown(dev);
    fog_.shutdown();
    fieldView_.shutdown();
    hasTerrain_ = false;
    if (twoD_) twoD_->shutdown(dev);
    twoD_.reset();
    groundFx_.shutdown(dev);
    roads_->shutdown(dev);
    shores_->shutdown(dev);
    paintLayers_.shutdown();
    groundCanvas_.shutdown();
    planetGroundFx_.shutdown(dev);
    planetFlora_.shutdown(dev);
    planets_.shutdown(dev);
    overlays_.shutdown(dev);
}

bool GameView::spriteHit(const Camera& cam, const SceneObject& o, const MapGrid& grid, float sx, float sy, float w, float h) const {
    return twoD_ && !o.isProjectile && twoD_->hasSprite(o.type) && twoD_->hit(cam, o, heightAt(grid, o.pos.x, o.pos.z), sx, sy, w, h);
}

float GameView::selectionRadius(uint16_t type) const {
    // Source compatibility only; clients/editor should call the RTS
    // presentation adapter without constructing a legacy renderer.
    return view_adapter::selectionRadius(*rules_,type);
}

// heightAt / buildTerrain: GameTerrain.cpp


// "groundShadow": the image's quad in the drawn model's frame (its rotation without scale / centring), at the object.
void GameView::addGroundShadow(const TypeLook& L, const EntityAnim& st, float3 pos, float ground, const EntityFxValues* fx) {
    const auto& S = L.shadow;
    // view.entity_fx: the image scales with the model (shadow_scale) and fades with it (opacity * shadow)
    float kx = 1, ky = 1, kz = 1, fade = 1;
    if (fx) {
        kx = fx->v[EntityFxValues::kShadowScale]; ky = fx->v[EntityFxValues::kShadowScale + 1]; kz = fx->v[EntityFxValues::kShadowScale + 2];
        fade = fx->opacity() * fx->v[EntityFxValues::kShadow];
        if (fade <= 0 || kx <= 0 || kz <= 0) return;
    }
    uint32_t rgba = S.rgba;
    if (fade < 1) rgba = (rgba & 0xffffff00u) | uint32_t(std::lround(float(rgba & 0xffu) * fade));
    float3 ax = st.drawn.transformDir({1, 0, 0}), az = st.drawn.transformDir({0, 0, 1});
    ax.y = 0; az.y = 0;
    const float lx = length(ax), lz = length(az);
    if (lx < 1e-6f || lz < 1e-6f) return;
    ax = ax * (1.0f / lx); az = az * (1.0f / lz);
    const float3 c = float3{pos.x, ground + S.lift * ky, pos.z} + ax * (S.ox * kx) + az * (S.oz * kz);
    const float3 u = ax * (S.w * 0.5f * kx), v = az * (S.d * 0.5f * kz);   // image top (v = 0) towards -z of the model
    ViewGroundBatch* b = nullptr;
    for (auto& x : shadowDraws_) if (x.image == S.image) { b = &x; break; }
    if (!b) { shadowDraws_.push_back({}); b = &shadowDraws_.back(); b->image = S.image; b->layer = -1; }
    auto vert = [&](float su, float sv) {
        const float3 p = c + u * su + v * sv;
        const float tu = S.flipU ? (1 - su) * 0.5f : (su + 1) * 0.5f, tv = S.flipV ? (1 - sv) * 0.5f : (sv + 1) * 0.5f;
        b->verts.push_back({p.x, p.y, p.z, tu, tv, rgba});
    };
    vert(-1, -1); vert(1, -1); vert(1, 1);
    vert(-1, -1); vert(1, 1); vert(-1, 1);
}

void GameView::drawGround(Renderer3D& r, const std::vector<ViewGroundBatch>& batches) {
    if (!dev_) return;
    groundMerged_.clear();
    for (auto& b : shadowDraws_) if (!b.verts.empty()) groundMerged_.push_back(b);
    if (groundMerged_.empty()) { groundCanvas_.draw(r, *dev_, batches, contentBase_); return; }
    groundMerged_.insert(groundMerged_.end(), batches.begin(), batches.end());
    groundCanvas_.draw(r, *dev_, groundMerged_, contentBase_);
}

// LENS secondary camera: primary GameView::draw has prepared the terrain,
// GPU fog texture, visual clocks and all animation packets already. Do not
// call fog_.update, terrain_.draw or any scene event/FX producers again.
void GameView::drawPreparedSprites(Renderer3D& renderer,uint64_t expectedEpoch) {
    const auto published=framePublication_.published();
    if(published.ready() && (!expectedEpoch || published.id==expectedEpoch) && twoD_)
        twoD_->submitPrepared(renderer);
}

void GameView::drawPreparedTerrainFog(Renderer3D& r,const MapGrid& grid,
                                     const std::vector<uint8_t>* vision,
                                     double seconds,uint64_t expectedEpoch) {
    if(!dev_ || !framePublication_.readyFor(grid.w,grid.h,seconds))return;
    if(expectedEpoch && framePublication_.published().id!=expectedEpoch)return;
    const bool tiles=twoD_ && twoD_->hasTiles();
    if(hasTerrain_ && !tiles && terrain_.built())
        terrain_.drawPrepared(r,grid,vision);
    // Terrain water is in the shared simple mesh packet snapshot. In 2D
    // tile mode we intentionally omit the legacy water draw here as well.
    if(vision && !vision->empty() &&
       !(hasTerrain_ && !tiles && terrain_.shaderFog())) {
        const bool low=r.shadowQuality()==ShadowQuality::Off ||
                       r.shadowQuality()==ShadowQuality::Low;
        fog_.draw(r,seconds,low);
    }
}

// An unpublished map and the scene-visible GroundFX geometry use the same
// one-shot lease as the existing surface prewarm, but start with a FULL
// topology candidate instead of preparing the already-published map.
// Only the commit/rollback callbacks survive this call. They capture the
// stable lease owner, never MapGrid/scene/camera references or stack lambdas.
bool GameView::prepareTerrainAtomicFrame(
        gfx::Device& dev,const MapGrid& grid,Renderer3D& renderer,
        const GameScene& scene,double time,const std::vector<uint8_t>* vision) {
    if(dev_!=&dev || planets_.active() || !terrain_.canPrepareMap() ||
       !fog_.ready() || grid.w<=0 || grid.h<=0 ||
       (twoD_ && twoD_->hasTiles()) ||
       (scene.entityFx && !scene.entityFx->hierarchy.parents.empty()))
        return false;
    if(framePublication_.active())
        throw std::logic_error(
            "GameView: topology prewarm during active publication");
    discardPendingSurfaceLease();
    framePublication_.invalidate(); // no stale secondary capture after prewarm
    ++terrainGeneration_; // editor grid changes may retain their address
    auto lease=std::make_unique<PreparedSurfaceLease>();
    auto* next=lease.get();
    next->topology=true;
    next->owner=this;next->device=&dev;
    next->renderer=&renderer;next->scene=&scene;
    next->map=&grid;next->vision=vision;next->time=time;
    next->frameStamp=frame_;
    next->terrainGeneration=terrainGeneration_;
    next->roadRevision=roads_->stateRevision();
    next->shoreRevision=shores_->stateRevision();
    next->groundRevision=groundFx_.stateRevision();
    next->fogRevision=fog_.stateRevision();
    next->terrainRevision=terrain_.stateRevision();
    ++surfacePrewarmStats_.sourceSnapshots;
    next->gridSignature=mapLeaseFingerprint(grid);
    next->sceneSignature=sceneLeaseFingerprint(scene);
    next->cameraSignature=cameraLeaseFingerprint(renderer);
    next->visibilitySignature=visibilityLeaseFingerprint(vision);
    next->tiles=false;
    next->preserveFogHistory=
        terrain_.mapMatches(grid.w,grid.h) &&
        fog_.sheetMatches(grid.w,grid.h);
    const bool flatDig=groundFx_.drawsDig() &&
        grid.dig.size()==grid.flags.size();
    // Full topology prewarm always needs a fresh GroundFX grid. The
    // published GroundFX dimensions may already match an incoming map
    // even when the Terrain owner is still on the previous map.
    const std::vector<uint8_t> unexplored(
        size_t(grid.w)*size_t(grid.h),uint8_t(0));
    const std::function<float(float,float)> nextHeight=
        [this,next,&grid](float x,float z) {
            return terrain_.preparedSurfaceAt(
                next->topologyCandidate,grid,x,z);
        };
    next->transaction=std::make_unique<render::OwnerPrepareCommit>();
    const auto add=[&](std::function<void()> prepare,
                       std::function<void()> commit,
                       std::function<void()> rollback) {
        next->parts.push_back(std::make_unique<PreparedSurfaceLease::Part>(
            std::move(prepare),std::move(commit),std::move(rollback)));
        next->transaction->add(*next->parts.back());
    };
    add(
        [this,next,&dev,&grid,flatDig] {
            next->topologyCandidate=terrain_.prepareMap(dev,grid,flatDig);
            if(!next->topologyCandidate)
                throw std::runtime_error(
                    "GameView: empty topology prewarm candidate");
        },
        [this,next] {
            (void)terrain_.commitMap(std::move(next->topologyCandidate));
        },
        [next] {next->topologyCandidate.reset();});
    add(
        [this,next,&dev,&nextHeight] {
            next->roadCandidate=roads_->prepareRebuild(
                dev,nextHeight,true);
        },
        [this,next] {
            roads_->commitRebuild(std::move(next->roadCandidate));
        },
        [next] {next->roadCandidate.reset();});
    add(
        [this,next,&dev,&nextHeight,&grid] {
            next->shoreCandidate=shores_->prepareRebuild(
                dev,nextHeight,terrain_.look().waterOffset,
                grid.w,grid.h,true);
        },
        [this,next] {
            shores_->commitRebuild(std::move(next->shoreCandidate));
        },
        [next] {next->shoreCandidate.reset();});
    add(
        [this,next,&renderer,&scene,time,vision,&grid,&nextHeight] {
            if(stagedHeightGrid_ || stagedHeightFn_)
                throw std::logic_error(
                    "GameView: nested unpublished height staging");
            stagedHeightGrid_=&grid;
            stagedHeightFn_=&nextHeight;
            try {
                next->groundCandidate=groundFx_.prepareMapSceneFrame(
                    renderer,scene,time,vision,grid,*this,true);
            }catch(...) {
                stagedHeightGrid_=nullptr;
                stagedHeightFn_=nullptr;
                throw;
            }
            stagedHeightGrid_=nullptr;
            stagedHeightFn_=nullptr;
            if(!next->groundCandidate)
                throw std::runtime_error(
                    "GameView: empty map GroundFX prewarm candidate");
        },
        [this,next] {
            groundFx_.commitFrame(std::move(next->groundCandidate));
        },
        [next] {next->groundCandidate.reset();});
    add(
        [this,next,&dev,&grid,&nextHeight,&unexplored,vision,time] {
            next->fogSheet=fog_.prepareSheet(
                dev,grid.w,grid.h,nextHeight);
            if(!next->fogSheet)
                throw std::runtime_error(
                    "GameView: topology Fog sheet prewarm failed");
            if((vision && !vision->empty()) || !next->preserveFogHistory) {
                const auto& field=(vision && !vision->empty())?*vision:unexplored;
                next->fogUpdate=fog_.prepareUpdate(
                    field,grid.w,grid.h,time,next->fogSheet.get());
                if(!next->fogUpdate)
                    throw std::runtime_error(
                        "GameView: topology Fog R8 prewarm failed");
            }
        },
        [this,next] {
            if(!next->fogUpdate) {
                (void)fog_.commitPreparedSheet(
                    std::move(next->fogSheet));
            }else{
                next->fogChanged=fog_.commitPreparedFrame(
                    std::move(next->fogSheet),
                    std::move(next->fogUpdate));
            }
        },
        [next] {
            next->fogUpdate.reset();
            next->fogSheet.reset();
        });
    add(
        [this,next,&renderer,&grid,vision] {
            const std::vector<uint8_t>* processed=nullptr;
            if(next->fogUpdate && next->fogUpdate->changed())
                processed=&next->fogUpdate->field();
            else if(next->preserveFogHistory)
                processed=&fog_.field();
            terrain_.prepareMapVisible(next->topologyCandidate,
                renderer,grid,vision,processed);
        },
        [] {}, // owned and published by the Terrain map participant
        [] {});
    // A callback can mutate the map, camera, scene or fog bytes during GPU
    // preparation. Verify AFTER the late Fog stage and BEFORE publication.
    next->transaction->prepareAll();
    if(!next->matches(*this,renderer,scene,time,vision,grid)) {
        next->transaction->rollback();
        throw std::runtime_error(
            "GameView: map/scene changed during topology prewarm");
    }
    for(auto& part:next->parts)part->stage={};
    pendingSurfaceLease_=std::move(lease);
    ++surfacePrewarmStats_.staged;
    return true;
}

// Called before the primary-frame camera epoch begins and BEFORE any model
// or animation reads the published height. On a stale prewarm, stage the
// current source synchronously once; NEVER publish the old GPU candidates.
std::optional<bool> GameView::publishPendingTerrainFrame(
        Renderer3D& renderer,const GameScene& scene,double time,
        const std::vector<uint8_t>* vision,const MapGrid& grid) {
    if(!pendingSurfaceLease_ || !pendingSurfaceLease_->topology)return std::nullopt;
    auto lease=std::move(pendingSurfaceLease_);
    if(!lease->matches(*this,renderer,scene,time,vision,grid)) {
        lease.reset(); // reverse-order rollback before new allocations
        ++surfacePrewarmStats_.discarded;
        if(!dev_)
            throw std::logic_error("GameView: missing GPU device for topology");
        buildTerrainAtomicScene(
            *dev_,grid,renderer,scene,time,vision);
        return std::nullopt;
    }
    if(!lease->transaction->publish())
        throw std::logic_error("GameView: topology prewarm not prepared");
    ++surfacePrewarmStats_.consumed;
    // Finish the old synchronous buildTerrainAtomicScene publication effects
    // ONLY after the five-owner transaction has actually committed.
    paintLayers_.shutdown();
    fieldView_.invalidate();
    hasTerrain_=true;
    fogMeshDirty_=false;
    if(twoD_)twoD_->buildTiles(*dev_,grid,
        rules_?float(rules_->levelHeight.toDouble()):1.5f);
    return lease->fogChanged;
}

// Stage every surface owner into a private, one-shot transaction. All input
// references are used synchronously here; stored participants retain ONLY
// their candidate/commit/rollback closures after preparation.
std::unique_ptr<GameView::PreparedSurfaceLease> GameView::stageSurfaceFrame(
        Renderer3D& r,const GameScene& s,double time,
        const std::vector<uint8_t>* vision,const MapGrid& grid,SurfaceLeaseUse use) {
    if(!dev_ || (hasTerrain_ && !terrain_.mapMatches(grid.w,grid.h)))
        throw std::logic_error("GameView: build Terrain before surface preflight");
    auto lease=std::make_unique<PreparedSurfaceLease>();
    auto* next=lease.get();
    next->owner=this;
    next->device=dev_;
    next->renderer=&r;
    next->scene=&s;
    next->map=&grid;
    next->vision=vision;
    next->time=time;
    next->terrainGeneration=terrainGeneration_;
    next->frameStamp=frame_;
    next->roadRevision=roads_->stateRevision();
    next->shoreRevision=shores_->stateRevision();
    next->groundRevision=groundFx_.stateRevision();
    next->fogRevision=fog_.stateRevision();
    next->terrainRevision=terrain_.stateRevision();
    // Only a lease escaping this call needs cross-call content identity.
    // Immediate prepare/publish keeps the SAME owner transaction and all
    // preparations; no fingerprint from this path is ever used by matches().
    next->retained=use==SurfaceLeaseUse::Retained;
    if(next->retained) {
        ++surfacePrewarmStats_.sourceSnapshots;
        next->gridSignature=mapLeaseFingerprint(grid);
        next->sceneSignature=sceneLeaseFingerprint(s);
        next->cameraSignature=cameraLeaseFingerprint(r);
        next->visibilitySignature=visibilityLeaseFingerprint(vision);
    }
    next->tiles=bool(twoD_ && twoD_->hasTiles());
    next->transaction=std::make_unique<render::OwnerPrepareCommit>();
    const std::function<float(float,float)> groundHeight=
        [this,&grid](float x,float z){return heightAt(grid,x,z);};
    const auto add=[&](std::function<void()> prepare,
                       std::function<void()> commit,
                       std::function<void()> rollback) {
        next->parts.push_back(std::make_unique<PreparedSurfaceLease::Part>(
            std::move(prepare),std::move(commit),std::move(rollback)));
        next->transaction->add(*next->parts.back());
    };
    add(
        [this,&r,&s,time,vision,&grid,next] {
            if(!next->tiles)
                next->groundCandidate=groundFx_.prepareFrame(
                    r,s,time,vision,grid,*this);
        },
        [this,next] {groundFx_.commitFrame(std::move(next->groundCandidate));},
        [next] {next->groundCandidate.reset();});
    add(
        [this,&groundHeight,next] {
            next->roadCandidate=roads_->prepareRebuild(*dev_,groundHeight);
        },
        [this,next] {roads_->commitRebuild(std::move(next->roadCandidate));},
        [next] {next->roadCandidate.reset();});
    add(
        [this,&groundHeight,&grid,next] {
            next->shoreCandidate=shores_->prepareRebuild(
                *dev_,groundHeight,terrain_.look().waterOffset,grid.w,grid.h);
        },
        [this,next] {shores_->commitRebuild(std::move(next->shoreCandidate));},
        [next] {next->shoreCandidate.reset();});
    add(
        [this,&groundHeight,vision,&grid,time,next] {
            if(!vision || vision->empty() || !fog_.ready())return;
            if(fogMeshDirty_ || fog_.needsSheetRebuild(grid.w,grid.h)) {
                next->fogSheet=fog_.prepareSheet(
                    *dev_,grid.w,grid.h,groundHeight);
                if(!next->fogSheet)
                    throw std::runtime_error("GameView: fog sheet staging failed");
                next->fogSheetStaged=true;
            }
            next->fogUpdate=fog_.prepareUpdate(
                *vision,grid.w,grid.h,time,next->fogSheet.get());
            if(!next->fogUpdate)
                throw std::runtime_error("GameView: fog field staging failed");
        },
        [this,next] {
            if(next->fogSheet || next->fogUpdate)
                next->fogChanged=fog_.commitPreparedFrame(
                    std::move(next->fogSheet),std::move(next->fogUpdate));
            if(next->fogSheetStaged)fogMeshDirty_=false;
        },
        [next] {
            next->fogUpdate.reset();
            next->fogSheet.reset();
        });
    add(
        [this,&r,&grid,vision,next] {
            if(!hasTerrain_ || next->tiles)return;
            const std::vector<uint8_t>* processed=nullptr;
            if(terrain_.shaderFog()) {
                if(next->fogUpdate && next->fogUpdate->changed() &&
                   next->fogUpdate->field().size()==
                       size_t(grid.w)*size_t(grid.h))
                    processed=&next->fogUpdate->field();
                else if(fog_.field().size()==size_t(grid.w)*size_t(grid.h))
                    processed=&fog_.field();
            }
            next->terrainCandidate=terrain_.prepareFrameVisible(
                r,grid,vision,processed);
        },
        [this,next] {terrain_.commitFrame(std::move(next->terrainCandidate));},
        [next] {next->terrainCandidate.reset();});
    // prepareAll automatically rolls attempted participants back when any
    // later upload fails. The transaction lives across prewarm and draw.
    next->transaction->prepareAll();
    // Nothing can call prepare() again after State::Prepared; clear captured
    // host references now, so no expired MapGrid/scene lambda survives.
    for(auto& part:next->parts)part->stage={};
    return lease;
}

// Publish an existing one-shot prewarm lease only when every dependency still
// matches exactly. Otherwise release unpublished GPU candidates and run the
// same complete preparation on the current primary-frame inputs.
bool GameView::publishSurfaceFrame(Renderer3D& r,const GameScene& s,
        double time,const std::vector<uint8_t>* vision,
        const MapGrid& grid) {
    auto lease=std::move(pendingSurfaceLease_);
    if(lease && !lease->matches(*this,r,s,time,vision,grid)) {
        lease.reset(); // RAII abort releases unpublished GPU resources
        ++surfacePrewarmStats_.discarded;
    }
    if(lease)++surfacePrewarmStats_.consumed;
    else lease=stageSurfaceFrame(r,s,time,vision,grid,SurfaceLeaseUse::Immediate);
    if(!lease->transaction->publish())
        throw std::logic_error("GameView: surface owner cohort publish failed");
    return lease->fogChanged;
}

bool GameView::prepareMapFrame(Renderer3D& renderer,const GameScene& scene,
        double time,const std::vector<uint8_t>* vision,
        const MapGrid& grid) {
    // An unpublished map already carries the visible GroundFX scene.
    // Reuse that exact lease rather than staging an outdated Terrain owner.
    if(pendingSurfaceLease_ && pendingSurfaceLease_->topology)
        return pendingSurfaceLease_->matches(
            *this,renderer,scene,time,vision,grid);
    if(!dev_ || planets_.active() ||
       !terrain_.mapMatches(grid.w,grid.h))return false;
    if(framePublication_.active())
        throw std::logic_error("GameView: prewarm during active publication");
    // The hierarchy transform graph needs the primary animation frame.
    if(scene.entityFx && !scene.entityFx->hierarchy.parents.empty())
        return false;
    // A pending primary surface will later change GPU owner generations.
    // Never replay an older secondary camera epoch after prewarm begins.
    framePublication_.invalidate();
    if(pendingSurfaceLease_) {
        pendingSurfaceLease_.reset();
        ++surfacePrewarmStats_.discarded;
    }
    pendingSurfaceLease_=stageSurfaceFrame(
        renderer,scene,time,vision,grid);
    ++surfacePrewarmStats_.staged;
    return true;
}

// The legacy host owns semantic clocks/events; core View owns only generic
// graph playback. Keep both old owners until the complete frame succeeds.
class GameView::PreparedAnimationLease final {
public:
    PreparedAnimationLease(GameView& view,Renderer3D& renderer)
        :v(view),r(renderer),generic(view.genericView_?view.genericView_->cloneForPreparation():nullptr),
         reload(view.reloadPlayback_),choices(view.streamingChoices_),tracers(view.tracers_),
         events(view.events_),effects(view.effects_),bars(view.bars_),
         shadows(view.shadowDraws_),ground(view.groundMerged_),lights(view.lights_),
         obstacles(view.animationObstacles_),surfaces(view.animationSurfaceFrames_),
         rendererHeight(renderer.heightField()),
         rendererLights(renderer.lights.capturePrepared()),lighting(renderer.lighting),
         lastTime(view.lastTime_),lastTick(view.lastTick_),frame(view.frame_),
         viewProj(view.viewProj_),camPos(view.camPos_),fx(view.fx_) {
        animations.reserve(view.anims_.size());
        for(const auto& [id,state]:view.anims_)
            animations.emplace(id,state.cloneForPreparation());
        // Every fallible copy above finishes before any published owner moves.
        overlays=v.overlays_.beginFramePreparation();
        if(v.twoD_)sprites=v.twoD_->beginFramePreparation();
        swapOwners();
    }
    ~PreparedAnimationLease() noexcept {
        if(committed)return;
        // Queued ModelDraws borrow candidate poses. Clear them BEFORE swap
        // destroys the failed candidate, even if the caller catches the error.
        r.discardSceneSubmission();
        if(v.cameraPreparedCapture_)v.cameraPreparedCapture_->clear();
        swapOwners();
        r.lights.swapPrepared(rendererLights);r.lighting=lighting;r.setHeightField(rendererHeight);
        v.lastTime_=lastTime;v.lastTick_=lastTick;v.frame_=frame;
        v.viewProj_=viewProj;v.camPos_=camPos;v.fx_=fx;
    }
    void commit() noexcept {
        v.overlays_.commitFramePreparation(overlays);
        if(v.twoD_)v.twoD_->commitFramePreparation(sprites);
        committed=true;
    }
private:
    void swapOwners() noexcept {
        v.anims_.swap(animations);v.genericView_.swap(generic);
        v.reloadPlayback_.swap(reload);v.streamingChoices_.swap(choices);
        v.tracers_.swap(tracers);v.events_.swap(events);v.effects_.swap(effects);
        v.bars_.swap(bars);v.shadowDraws_.swap(shadows);v.groundMerged_.swap(ground);
        v.animationObstacles_.swap(obstacles);v.animationSurfaceFrames_.swap(surfaces);
        using std::swap;swap(v.lights_,lights);
    }
    GameView& v;Renderer3D& r;bool committed=false;
    decltype(GameView::anims_) animations;
    decltype(GameView::genericView_) generic;
    decltype(GameView::reloadPlayback_) reload;
    decltype(GameView::streamingChoices_) choices;
    decltype(GameView::tracers_) tracers;
    decltype(GameView::events_) events;
    decltype(GameView::effects_) effects;
    decltype(GameView::bars_) bars;
    decltype(GameView::shadowDraws_) shadows,ground;
    GameLights lights;
    decltype(GameView::animationObstacles_) obstacles;
    decltype(GameView::animationSurfaceFrames_) surfaces;
    render::HeightField rendererHeight;
    std::shared_ptr<GameOverlays::PreparedFrame> overlays;
    std::shared_ptr<Rts2DView::PreparedFrame> sprites;
    render::LightSystem::PreparedFrame rendererLights;
    render::Lighting lighting;
    double lastTime;uint32_t lastTick;uint64_t frame;
    render::mat4 viewProj;render::float3 camPos;EntityFxStore* fx;
};

void GameView::draw(Renderer3D& r, const GameScene& input, double time, const std::vector<uint8_t>* vision, const MapGrid& grid) {
    r.setModelLodScale(modelLodScale_);
    r.setShadowLodScale(shadowLodScale_);
    r.setReflectionCullPixels(reflectionCullPixels_);
    if(lodFadeWidth_>=0.f) r.setModelLodFadeWidth(lodFadeWidth_);
    // A matching topology prewarm MUST publish before even the first
    // hierarchy/animation height query or FramePublicationEpoch::begin().
    std::unique_ptr<PreparedSurfaceLease> surfaceLease;
    std::optional<bool> topologyFrame;
    if(cameraPreparedCapture_ && pendingSurfaceLease_ && pendingSurfaceLease_->topology) {
        if(!pendingSurfaceLease_->matches(*this,r,input,time,vision,grid)) {
            pendingSurfaceLease_.reset();++surfacePrewarmStats_.discarded;
            if(!dev_ || !prepareTerrainAtomicFrame(*dev_,grid,r,input,time,vision))
                throw std::logic_error("LENS: topology source cannot be prepared atomically");
        }
        surfaceLease=std::move(pendingSurfaceLease_);
    } else if(!cameraPreparedCapture_) {
        topologyFrame=publishPendingTerrainFrame(r,input,time,vision,grid);
    }
    // The temporary ground source is visible only during synchronous private
    // frame preparation; published queries recover their old source on abort.
    const std::function<float(float,float)> candidateHeight=[&](float x,float z) {
        return terrain_.preparedSurfaceAt(surfaceLease->topologyCandidate,grid,x,z);
    };
    const std::function<float(float,float,float)> candidateRoad=[&](float x,float z,float h) {
        return roads_->candidateSurfaceAt(surfaceLease->roadCandidate,x,z,h);
    };
    struct RestoreSources {
        GameView& view;
        ~RestoreSources(){view.stagedHeightFn_=nullptr;view.stagedHeightGrid_=nullptr;
            view.stagedRoadFn_=nullptr;}
    } restoreSources{*this};
    if(surfaceLease) {
        stagedHeightGrid_=&grid;stagedHeightFn_=&candidateHeight;
        stagedRoadFn_=&candidateRoad;
    }
    // Resource retirement is driven by the explicit owner frame hook,
    // never by this draw function (which can be called several times).
    const bool traceView = std::getenv("AIVERSE_FRAME_TRACE") != nullptr;
    const auto viewStart = std::chrono::steady_clock::now();
    using Epoch=render::FramePublicationEpoch;
    constexpr uint32_t kSceneMask=Epoch::Geometry|Epoch::Terrain|
        Epoch::Ground|Epoch::Roads|Epoch::Shores|Epoch::Fog|
        Epoch::Sprites|Epoch::Animation|Epoch::Lighting|Epoch::Overlays;
    auto publication=framePublication_.begin(
        planets_.active()?uint32_t(Epoch::Geometry):kSceneMask,
        frame_+1,grid.w,grid.h,time);
    // Script spawns, reinforcements and restored entities need not exist at
    // map load time. Build the active type set once for both lazy residency
    // and contact-animation detection, without allocating/clearing an array
    // proportional to the entire rules catalog every frame.
    if (drawTypeStamps_.size() != loadedTypes_.size()) {
        drawTypeStamps_.assign(loadedTypes_.size(), 0);
        drawTypeEpoch_ = 0;
    }
    if (++drawTypeEpoch_ == 0) {
        std::fill(drawTypeStamps_.begin(), drawTypeStamps_.end(), 0);
        drawTypeEpoch_ = 1;
    }
    drawTypes_.clear();
    for (const auto& o : input.objects) {
        if (o.isProjectile || o.type >= drawTypeStamps_.size() ||
            drawTypeStamps_[o.type] == drawTypeEpoch_) continue;
        drawTypeStamps_[o.type] = drawTypeEpoch_;
        drawTypes_.push_back(o.type);
        if (!streamingTypes_) loadType(o.type);
    }
    // All synchronous Model loads above must finish BEFORE this scope.
    // The cache owns unique_ptr<Model>; the reader guard blocks
    // replace/invalidate/trim/shutdown until the View's graph evaluation
    // and synchronous Renderer3D model submissions have completed.
    // GPU work remains owned by the separate fenced retirement queue.
    [[maybe_unused]] const auto modelReadScope=resources_.acquireFrameReadScope();
    // Full animation/event rollback is used by LENS capture frames. Ordinary
    // single-camera hosts keep their established allocation/performance path.
    std::unique_ptr<PreparedAnimationLease> animationLease;
    if(cameraPreparedCapture_)
        animationLease=std::make_unique<PreparedAnimationLease>(*this,r);
    GameScene resolved;
    const bool hierarchy=input.entityFx && !input.entityFx->hierarchy.parents.empty();
    if(hierarchy) {
        resolved=input;
        std::map<uint64_t,mat4> bases,effects;
        for(const auto& o:input.objects) {
            if(!o.entity.valid() || o.type>=looks_.size() || o.body!=-1)continue;
            const auto& look=looks_[o.type];EntityFxValues fx;input.entityFx->lookup(o.entity.packed(),fx);
            constexpr float deg=.017453292f;
            auto effect=mat4::translate({0,(look.air?look.flyHeight:0)+fx.v[EntityFxValues::kLift],0})
                *mat4::rotateZ(-fx.v[EntityFxValues::kPitch]*deg)*mat4::rotateX(-fx.v[EntityFxValues::kRoll]*deg)
                *mat4::scale({fx.scale(0),fx.scale(1),fx.scale(2)});
            effects[o.entity.packed()]=effect;
            auto external=input.entityFrames.find(o.entity.packed());
            bases[o.entity.packed()]=external!=input.entityFrames.end()?external->second:
                mat4::translate({o.pos.x,heightAt(grid,o.pos.x,o.pos.z),o.pos.z})
                *mat4::rotateY(std::atan2(-o.facing.z,o.facing.x))*effect;
        }
        auto frames=input.entityFx->hierarchy.resolve(bases,effects);
        for(auto& o:resolved.objects)if(input.entityFx->hierarchy.parents.count(o.entity.packed())) {
            auto it=frames.find(o.entity.packed());
            if(it==frames.end()){o.visible=false;continue;}
            o.parented=true;o.parentFrame=it->second;o.pos=it->second.transformPoint({0,0,0});
            o.facing=it->second.transformPoint({1,0,0})-o.pos;
        }
    }
    const GameScene& s=hierarchy?resolved:input;
    fx_ = s.entityFx.get();   // view.entity_fx
    if (planets_.active()) {
        // Planet geometry is a host-owned provider of the same generic
        // View layer pipeline, not a special mode inside engine View.
        ::aiverse::view::ViewLayerProviders providers;
        providers.add("planet scene",[&](auto& q,const auto&){
            q.append("planet scene",::aiverse::view::LayerPhase::Geometry,
                [&]{
                    drawPlanets(r,s,time,vision,grid);
                    publication.mark(Epoch::Geometry);
                });
        });
        // Universal View owns the synchronous presentation barrier.
        ::aiverse::View::submitLayers(providers,{time,0.f,frame_});
        if(!publication.finish())
            throw std::logic_error("GameView: incomplete planet visual frame publication");
        if(animationLease)animationLease->commit();
        return;
    }
    animationObstacles_.clear();
    bool contacts = fx_ && !fx_->animations.empty();
    if (!contacts) {
        // A graph on an unused library type cannot produce contacts in this
        // scene. Avoid scanning hundreds of unrelated types and variants
        // every frame (especially in the eager-loading editor).
        for (uint16_t type : drawTypes_) {
            if (type >= looks_.size()) continue;
            const auto& look = looks_[type];
            if (std::any_of(look.models.begin(), look.models.end(),
                            [](const ModelChoice& ch) {
                                return ch.graph && ch.graph->hasContacts();
                            })) { contacts = true; break; }
        }
    }
    // AnimationEnvironment handles a null frame-map (id 1 = world ground).
    // Avoid allocating an empty shared map on every ordinary static frame.
    animationSurfaceFrames_ = contacts
        ? std::make_shared<std::map<uint64_t,mat4>>() : nullptr;
    for(size_t oi=0;contacts&&oi<s.objects.size();++oi){const auto& o=s.objects[oi];if(!o.visible||o.isProjectile||o.body!=-1||o.type>=looks_.size())continue;
        const auto& look=looks_[o.type];if(look.models.empty())continue;uint64_t key=o.entity.valid()?o.entity.packed():(uint64_t(1)<<63)|oi;auto state=anims_.find(key);
        size_t choice=state!=anims_.end()&&size_t(state->second.choice)<look.models.size()?size_t(state->second.choice):0;const ModelChoice* ch=&look.models[choice];
        const Stage* stage=nullptr;for(const auto& sg:look.stages){if(o.build<1&&look.construction==Construction::Stages&&sg.build>=0&&sg.build<=o.build&&(!stage||sg.build>stage->build))stage=&sg;if(o.build>=1&&sg.hp>=0&&o.hp<=sg.hp&&(!stage||sg.hp<stage->hp))stage=&sg;}if(stage)ch=&stage->choice;if(!ch->model)continue;
        EntityFxValues values;bool hasFx=fxFor(o,values);float lift=hasFx?values.v[EntityFxValues::kLift]:0;const float groundY=heightAt(grid,o.pos.x,o.pos.z);
        if(o.posed&&o.poseHasY)lift+=state!=anims_.end()&&state->second.poseLiftSet?state->second.poseLift:o.poseY-groundY;
        float3 position{o.pos.x,groundY+(look.air?look.flyHeight:0)+lift,o.pos.z};
        float yaw=state!=anims_.end()?state->second.yaw:std::atan2(-o.facing.z,o.facing.x);float k=look.scale,grow=look.construction==Construction::Grow?std::clamp(o.build,.15f,1.f):1;
        mat4 base;if(look.structure&&look.shape&&!stage)base=mat4::translate(position)*mat4::scale({look.sx*k,k*std::max(look.sx,look.sz)*.75f*grow,look.sz*k});else base=mat4::translate(position)*mat4::rotateY(yaw)*mat4::scale({k,k*(look.structure?grow:1),k});
        if(hasFx){auto tilt=mat4::rotateZ(-values.v[EntityFxValues::kPitch]*.017453292f)*mat4::rotateX(-values.v[EntityFxValues::kRoll]*.017453292f);base=mat4::translate(position)*mat4::rotateY(yaw)*tilt*mat4::rotateY(-yaw)*mat4::translate(-position)*base*mat4::scale({values.scale(0),values.scale(1),values.scale(2)});}
        if(o.posed&&!o.parented)base=authoredPoseFrame(o,yaw,position)*base;
        if(o.parented)base=o.parentFrame*mat4::scale({k,k*(look.structure?grow:1),k});
        if(state!=anims_.end())base=base*state->second.objXf;base=base*ch->fix;const auto& model=*ch->model;
        const auto& matrices=!stage&&state!=anims_.end()&&state->second.world.size()==model.nodes.size()?state->second.world:model.bindWorld;
        for(size_t node=0;node<model.nodes.size()&&node<matrices.size();++node){int mesh=model.nodes[node].mesh;if(mesh<0||size_t(mesh)>=model.meshes.size())continue;auto transform=base*matrices[node];const auto& primitives=model.meshes[size_t(mesh)].primitives;
            for(size_t part=0;part<primitives.size()&&part<256;++part){auto a=primitives[part].boundsMin,b=primitives[part].boundsMax;
                const auto bounds=::aiverse::view::transformAabb(a,b,transform);
                if(!bounds.valid)continue; // skip malformed visual bounds
                const auto lo=bounds.min,hi=bounds.max;
                uint64_t id=key^((node+1)*0x9e3779b97f4a7c15ull)^((part+1)*0xbf58476d1ce4e5b9ull);id^=id>>30;id*=0xbf58476d1ce4e5b9ull;id^=id>>27;id=(id&0x3fffffffffffffffull)+2;
                animationObstacles_.push_back({&o,o.entity,lo,hi,a,b,id,transform});(*animationSurfaceFrames_)[id]=transform;
            }
        }
    }
    // Primary and optional scene-aware warmup reuse the same atomic
    // surface-owner transaction; only draw publishes a complete camera epoch.
    bool fogFieldChanged=false;
    if(cameraPreparedCapture_) {
        if(!surfaceLease) {
            surfaceLease=std::move(pendingSurfaceLease_);
            if(surfaceLease && !surfaceLease->matches(*this,r,s,time,vision,grid)) {
                surfaceLease.reset();++surfacePrewarmStats_.discarded;
            }
            if(!surfaceLease)surfaceLease=stageSurfaceFrame(r,s,time,vision,grid);
        }
        stagedRoadFn_=&candidateRoad;
        fogFieldChanged=surfaceLease->fogUpdate && surfaceLease->fogUpdate->changed();
    }else fogFieldChanged=topologyFrame?*topologyFrame:
        publishSurfaceFrame(r,s,time,vision,grid);
    const bool tiles=twoD_ && twoD_->hasTiles();
    const auto afterSetup = std::chrono::steady_clock::now();
    // Legacy game-specific inputs are translated to engine-owned generic
    // presentation passes. Every callback executes immediately in its
    // original order on the frame thread; View owns no Rules/Map/Fog types.
    ::aiverse::view::ViewLayerQueue layers;
    using LayerPhase=::aiverse::view::LayerPhase;
    layers.append("scene geometry",LayerPhase::Geometry,[&] {
        geometryScene_.draw(r);
        publication.mark(Epoch::Geometry);
    });
    if(!geometryOnly_) {
    layers.append("2D scene begin",LayerPhase::Background,[&] {
        if(twoD_)twoD_->beginFrame(r.camera(),time,r,&grid,
            rules_?float(rules_->levelHeight.toDouble()):1.5f);
    });
    bars_.assign(s.objects.size(), {});
    layers.append("surface",LayerPhase::Geometry,[&] {
        if(surfaceLease && surfaceLease->topology)
            terrain_.drawMapCandidate(surfaceLease->topologyCandidate,r,grid,vision);
        else if(hasTerrain_ && !tiles) {
            if(surfaceLease)terrain_.drawCandidate(surfaceLease->terrainCandidate,r,grid,vision);
            else terrain_.draw(r,grid,vision,groundFx_.drawsDig());
        }
        if(hasTerrain_ && tiles)terrain_.drawWater(r);
        drawNereidLegacyWater(r,hasTerrain_);
        publication.mark(Epoch::Terrain);
    });
    layers.append("surface effects",LayerPhase::Effects,[&] {
        if(!tiles) {
            if(surfaceLease)groundFx_.drawCandidate(surfaceLease->groundCandidate,r,s,time,vision,grid,*this);
            else groundFx_.drawPrepared(r,s,time,vision,grid,*this);
        }
        publication.mark(Epoch::Ground);
    });
    // External host-only layer providers. The engine View doesn't know
    // what roads, shorelines or the game map mean.
    ::aiverse::view::ViewLayerProviders surfaceProviders;
    surfaceProviders.add("path surface",[&](auto& queue,const auto&){
        queue.append("paths",LayerPhase::Geometry,[&]{
            if(surfaceLease)roads_->drawCandidate(surfaceLease->roadCandidate,r);
            else if(roads_->active() || roads_->needsRebuild())
                roads_->draw(r,[this,&grid](float x,float z){
                    return heightAt(grid,x,z);
                });
            publication.mark(Epoch::Roads);
        });
    });
    surfaceProviders.add("shore surface",[&](auto& queue,const auto&){
        queue.append("shorelines",LayerPhase::Geometry,[&]{
            if(surfaceLease)shores_->drawCandidate(surfaceLease->shoreCandidate,r);
            else if(shores_->active())
                shores_->draw(r,[this,&grid](float x,float z){
                    return heightAt(grid,x,z);
                },terrain_.look().waterOffset,grid.w,grid.h);
            publication.mark(Epoch::Shores);
        });
    });
    surfaceProviders.collect(layers,{time,0.f,frame_});
    layers.append("surface paint",LayerPhase::Effects,[&] {
        paintLayers_.draw(r,*dev_,s.paintLayers,grid,*this,vision,time,s.viewer);
    });
    layers.append("surface links",LayerPhase::Overlay,[&] {
        if(tiles || !groundFx_.drawsLinks())
            for(const auto& [a,b]:s.roads)
                ::aiverse::View::submitQuadXZ(r,{
                    a,b,.5f,rgba(196,164,112,235),.04f});
    });
    } else {
        // Geometry-only: the surface owners publish an empty frame.
        publication.mark(Epoch::Terrain);
        publication.mark(Epoch::Ground);
        publication.mark(Epoch::Roads);
        publication.mark(Epoch::Shores);
    } // !geometryOnly_
    // Synchronous flush is the GPU pass boundary: callback captures cannot
    // escape this GameView::draw frame; there is no parallel render submission.
    ::aiverse::View::submitLayers(layers);

    const auto afterGround = std::chrono::steady_clock::now();
    const float dt = lastTime_ < 0 ? 0.0f : float(std::clamp(time - lastTime_, 0.0, 0.1));
    lastTime_ = time;
    ++frame_;
    viewProj_ = r.viewProj();
    camPos_ = r.cameraPos();
    events_.clear();
    effects_.clear();
    handleEvents(s);
    // Legacy display adapter -> generic engine View, for simple pure pose
    // graphs ONLY. All genre-specific intents, contact queries, IK,
    // procedural effects, attachments and rendering styling stay here.
    // The core View receives only identity, model, graph and named values.
    for(auto& [key,st]:anims_) { (void)key;st.genericViewPose=false; }
    ::aiverse::view::ScenePacketBatch genericObjects;
    if(genericViewEnabled_) {
        genericObjects.reserve(s.objects.size());
        for(size_t oi=0;oi<s.objects.size();++oi) {
            const auto& o=s.objects[oi];
            // A fallback render-array index is not a stable logical
            // identity: a different object could reuse it after reload.
            // Keep those objects on the existing legacy animation path.
            if(!o.entity.valid())continue;
            if(!o.visible || o.isProjectile || o.parented || o.body!=-1 ||
               o.type>=looks_.size() || !typeLoaded(o.type))continue;
            if(twoD_ && twoD_->hasSprite(o.type))continue;
            const auto& look=looks_[o.type];
            if(!look.stages.empty() || !look.lods.empty() ||
               look.models.empty())continue;
            const uint64_t key=o.entity.valid()?o.entity.packed():
                (uint64_t(1)<<63)|uint64_t(oi);
            auto& st=stateFor(key,o);
            const auto& ch=look.models[size_t(st.choice)];
            if(!ch.graph || !ch.model || !ch.needsPose ||
               ch.modelGeneration==0 ||
               ch.baked || !ch.procs.empty() || !ch.legs.empty() ||
               !ch.additive.empty() || !ch.attach.empty() ||
               !ch.fx.empty() || !ch.weaponPoses.empty() ||
               ch.graph->hasContacts() || ch.graph->states().size()!=1 ||
               !ch.graph->states()[0].transitions.empty())
                continue;
            // Unknown/custom parameters and callback-bound features are
            // deliberately left on the legacy path until parity is proven.
            bool supported=true;
            for(const auto& [name,value]:ch.graph->defaults().values) {
                (void)value;
                if(name!="speed" && name!="moving" && name!="dead")
                    supported=false;
            }
            for(const auto& node:ch.graph->nodes()) {
                using Kind=render::AnimationGraph::Kind;
                if(node.kind!=Kind::Bind && node.kind!=Kind::Clip &&
                   node.kind!=Kind::Blend && node.kind!=Kind::Additive)
                    supported=false;
                if(!node.traits.empty())supported=false;
            }
            if(!supported)continue;
            if(fx_ && !fx_->animations.empty() &&
               fx_->animations.count(o.entity.packed()))continue;
            ::aiverse::view::Object packet;
            packet.id=key;packet.generation=(uint64_t(o.type)<<32)|
                uint32_t(st.choice);
            packet.draw.model=ch.model;
            // Preserve the authored asset-publication epoch across graph
            // edits; a new Model upload receives a fresh epoch in RtsLook.
            packet.animationModelGeneration=ch.modelGeneration;
            packet.graph=ch.graph;
            packet.parameters=ch.graph->defaults();
            packet.parameters.set("speed",st.speed);
            packet.parameters.set("moving",o.moving?1.f:0.f);
            packet.parameters.set("dead",st.dead?1.f:0.f);
            packet.significance.visible=true;
            packet.significance.pixels=64.f;
            genericObjects.add(std::move(packet));
            st.genericViewId=key;
            st.genericViewPose=true;
        }
    }
    // Remove presentation entities for no-longer-eligible objects every
    // frame. No model draws here; GameViewDraw still applies materials,
    // terrain height, outlines, shadow, effects and other scene features.
    if(genericObjects.size()!=0 && !genericView_) {
        genericView_=std::make_unique<::aiverse::view::View>(
            ::aiverse::lib::animation::MotionWeaveWorkerPool::recommendedWorkers());
        genericView_->setRetargetScratchBudgetBytes(
            genericRetargetScratchBudgetBytes_);
    }
    if(genericView_)genericView_->update(genericObjects,dt);
    for (auto& b : shadowDraws_) b.verts.clear();
    // objects the spline roads stand in for (roadNetwork.replaces: tags or type id prefixes) are not drawn
    std::vector<uint8_t> replacedByRoads;
    if (roads_->active() && !roads_->replaces().empty() && rules_) {
        replacedByRoads.assign(rules_->types.size(), 0);
        uint64_t mask = 0;
        for (const auto& name : roads_->replaces()) mask |= rules_->tagMask(name);
        for (size_t t = 0; t < rules_->types.size(); ++t) {
            bool hit = (rules_->types[t].tags & mask) != 0;
            for (const auto& name : roads_->replaces()) if (!hit && rules_->types[t].id.rfind(name, 0) == 0) hit = true;
            replacedByRoads[t] = hit;
        }
    }
    placementCacheHits_ = placementCacheMisses_ = 0;
    groundCacheHits_ = groundCacheMisses_ = 0;
    staticAnimateFastHits_ = 0;
    // Camera culling before ground sampling, animation and submission, like Unity's
    // "Cull Completely" / Unreal's "Only Tick Pose When Rendered". The test sphere
    // covers the object's last submitted bounds and its ground shadow away from the
    // sun. Off: secondary camera captures (they reuse these packets), planets,
    // AIVERSE_RTS_VIEW_CULL=0.
    static const bool viewCullSetting = [] {
        const char* v = std::getenv("AIVERSE_RTS_VIEW_CULL");
        return !(v && std::strcmp(v, "0") == 0);
    }();
    const bool viewCull = viewCullSetting && !cameraPreparedCapture_ && !planets_.active();
    const auto cullFrustum = ::aiverse::view::SideFrustum::fromViewProjection(viewProj_);
    float3 sunAway{0, 0, 0};
    {
        float3 sun = r.lighting.sunDir;
        const float len = std::sqrt(dot(sun, sun));
        if (len > 1e-6f) sun = sun * (1.0f / len);
        // Shadow offset per metre of height (long shadows capped at ~6.7x).
        const float up = std::max(sun.y, 0.15f);
        sunAway = {-sun.x / up, 0, -sun.z / up};
    }
    size_t culledObjects = 0;
    double objectStateMs = 0.0, objectAnimateMs = 0.0, objectDrawMs = 0.0, objectShadowMs = 0.0;
    size_t tracedObjects = 0;
    for (size_t i = 0; i < s.objects.size(); ++i) {
        const auto objectStart = traceView ? std::chrono::steady_clock::now() : viewStart;
        const SceneObject& o = s.objects[i];
        if (!o.isProjectile && o.type < replacedByRoads.size() && replacedByRoads[o.type]) continue;
        if (o.isProjectile) {
            if (o.visible && o.entity.valid()) drawProjectile(r, o.entity.packed(), o, dt);
            else if(o.visible) {
                ::aiverse::view::MeshObject packet;
                packet.mesh=&projectile_;
                packet.world=mat4::translate(o.pos);
                packet.color={1,0.95f,0.8f,1};
                packet.flags=kDrawUnlit;
                ::aiverse::View::submitMesh(r,packet);
            }
            continue;
        }
        if (twoD_ && twoD_->hasSprite(o.type)) { // 2D games: sprites instead of models
            float g = heightAt(grid, o.pos.x, o.pos.z);
            EntityFxValues sfx;
            twoD_->addObject(o, g, fxFor(o, sfx) ? &sfx : nullptr);
            bars_[i] = twoD_->barPoint(o, g);
            continue;   // selection rings: overlays
        }
        if (o.type >= looks_.size() || !typeLoaded(o.type)) continue;
        // editor placements have no entity: key them by position in the list
        const uint64_t key = o.entity.valid() ? o.entity.packed() : (1ull << 63) | uint64_t(i);
        EntityAnim& st = stateFor(key, o);
        st.last = o;
        if (traceView) objectStateMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - objectStart).count();
        if (!o.visible) {
            if (o.blip) {   // radar contact: a filled dot with a ring where the unit is, no model (the viewer doesn't know its type)
                const float gy = groundAt(grid, o.pos.x, o.pos.z) + 0.06f;
                constexpr int kSeg = 12;
                const float rd = 0.32f;
                for (int k = 0; k < kSeg; ++k) {
                    const float a0 = 6.2831853f * float(k) / kSeg, a1 = 6.2831853f * float(k + 1) / kSeg;
                    ::aiverse::View::submitTriangle(r,{
                        {o.pos.x,gy,o.pos.z},
                        {o.pos.x+rd*std::cos(a0),gy,o.pos.z+rd*std::sin(a0)},
                        {o.pos.x+rd*std::cos(a1),gy,o.pos.z+rd*std::sin(a1)},
                        rgba(255,60,40,220)});
                }
                ::aiverse::View::submitCircle(r,{
                    {o.pos.x,gy,o.pos.z},0.6f,rgba(255,70,50,150),16,gy});
            }
            continue;
        }
        const TypeLook& L = looks_[o.type];
        const bool roadSurface = !rules_->types[o.type].structure && !L.air;
        static const bool cacheGround = [] {
            // On by default; AIVERSE_RTS_GROUND_CACHE=0 samples the ground every frame.
            const char* setting = std::getenv("AIVERSE_RTS_GROUND_CACHE");
            return !(setting && std::strcmp(setting, "0") == 0);
        }();
        const bool groundEligible = cacheGround && !o.moving && !o.parented &&
            (L.structure || rules_->types[o.type].speed <= Fixed{});
        auto& cachedGround = st.ground;
        const uint64_t roadsGeneration = roads_ ? roads_->surfaceGeneration() : 0;
        const bool reuseGround = groundEligible && cachedGround.valid &&
            cachedGround.grid == &grid && cachedGround.terrainGeneration == terrainGeneration_ &&
            cachedGround.roadGeneration == roadsGeneration &&
            cachedGround.type == o.type && cachedGround.roadSurface == roadSurface &&
            cachedGround.pos.x == o.pos.x && cachedGround.pos.z == o.pos.z;
        const float ground = reuseGround ? cachedGround.height :
            (roadSurface ? groundAt(grid, o.pos.x, o.pos.z) : heightAt(grid, o.pos.x, o.pos.z));
        if (groundEligible) {
            if (reuseGround) ++groundCacheHits_;
            else {
                ++groundCacheMisses_;
                cachedGround.valid = true;
                cachedGround.grid = &grid;
                cachedGround.terrainGeneration = terrainGeneration_;
                cachedGround.roadGeneration = roadsGeneration;
                cachedGround.type = o.type;
                cachedGround.roadSurface = roadSurface;
                cachedGround.pos = o.pos;
                cachedGround.height = ground;
            }
        } else cachedGround.valid = false;
        float animateDt = dt;
        if (viewCull && st.cullValid && !o.selected && !o.hovered) {
            // Bounds follow the object's current ground point; the radius gets a
            // margin for rotation and animation since the last submission.
            const float baseY = ground;
            const float3 centre = float3{o.pos.x, baseY, o.pos.z} + st.cullOffset;
            const float radius = st.cullRadius * 1.25f + 1.0f;
            const float height = std::max(0.0f, centre.y + radius - baseY);
            const float3 shadow{sunAway.x * height, 0, sunAway.z * height};
            const float3 mid = centre + shadow * 0.5f;
            const float reach = radius + 0.5f * std::sqrt(dot(shadow, shadow));
            if (!cullFrustum.visibleSphere(mid, reach)) {
                // Not drawn: animation clocks (events, decay) still advance, 4x a second.
                st.culledDt += dt;
                if (st.culledDt >= 0.25f) { animate(st, o, st.culledDt, &grid); st.culledDt = 0; }
                ++culledObjects;
                continue;
            }
        }
        if (st.culledDt > 0) { animateDt += st.culledDt; st.culledDt = 0; }
        const auto beforeAnimate = traceView ? std::chrono::steady_clock::now() : viewStart;
        animate(st, o, animateDt, &grid);
        const auto beforeDraw = traceView ? std::chrono::steady_clock::now() : viewStart;
        drawObject(r, st, o, ground, 1.0f);
        // Reuse the draw end timestamp as the shadow start timestamp.
        // Tracing must not issue another clock read for every scene object.
        const auto afterDraw = traceView ? std::chrono::steady_clock::now() : viewStart;
        if (traceView) {
            objectAnimateMs += std::chrono::duration<double, std::milli>(beforeDraw - beforeAnimate).count();
            objectDrawMs += std::chrono::duration<double, std::milli>(afterDraw - beforeDraw).count();
            ++tracedObjects;
        }
        // The model FX was handled by drawObject(). Query FX again only
        // when an additional ground or flyer shadow actually needs it.
        const bool groundShadow = !L.shadow.image.empty();
        if (groundShadow || L.flyer) {
            EntityFxValues fx;
            const bool hasFx = fxFor(o, fx);
            if (groundShadow) {
                addGroundShadow(L, st, o.pos, ground, hasFx ? &fx : nullptr);
            } else { // flyer blob, unless the type supplies an authored shadow
                const float k = hasFx ? fx.v[EntityFxValues::kShadowScale] : 1.0f;
                const float a = hasFx ? std::clamp(fx.opacity() * fx.v[EntityFxValues::kShadow], 0.0f, 1.0f) : 1.0f;
                if(a>0 && k>0)
                    ::aiverse::View::submitCircle(r,{
                        {o.pos.x,ground,o.pos.z},0.35f*k,
                        rgba(0,0,0,uint8_t(std::lround(90*a))),16,
                        ground+0.03f});
            }
            // No clock call for the common case with no additional shadow.
            if (traceView) objectShadowMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - afterDraw).count();
        }
    }
    const auto afterObjects = std::chrono::steady_clock::now();
    drawTracers(r, dt, &grid);
    // corpses (entity already removed) play their death and fade; stale states are dropped
    for (auto it = anims_.begin(); it != anims_.end();) {
        EntityAnim& st = it->second;
        if (st.seen == frame_ && !st.dead) { ++it; continue; }
        if ((!st.dead || !st.initialized) && !fxCorpse(it->first, st)) {
            it = st.seen + 2 < frame_ ? anims_.erase(it) : std::next(it);
            continue;
        }
        float adt = dt;
        st.deathAge = corpseAge(it->first, st.deathAge, dt, adt);
        if (st.type >= looks_.size() || st.deathAge > looks_[st.type].corpse) { it = anims_.erase(it); continue; }
        const TypeLook& L = looks_[st.type];
        if (st.seen != frame_ && st.last.visible) {
            SceneObject o = st.last;
            o.selected = o.hovered = false;
            animate(st, o, adt, &grid);
            const float g = heightAt(grid, o.pos.x, o.pos.z), fade = L.dissolveDeath ? 1.0f : L.corpseAlpha(st.deathAge);
            drawObject(r, st, o, g, fade);
            if (!L.shadow.image.empty()) {   // a corpse keeps its footprint image while it lies there (faded like the model)
                EntityFxValues fx;
                if (!fxFor(o, fx)) fx = {};
                fx.v[EntityFxValues::kShadow] *= fade;
                addGroundShadow(L, st, o.pos, g, &fx);
            }
        }
        ++it;
    }
    if (twoD_) {
        twoD_->endFrame(grid);
        twoD_->submit(r, grid, rules_ ? float(rules_->levelHeight.toDouble()) : 1.5f);
    }
    publication.mark(Epoch::Sprites|Epoch::Animation);

    const auto afterAnimations = std::chrono::steady_clock::now();
    // Game-owned providers for visibility, field markers and scene
    // decoration. The engine View only schedules opaque ordered passes.
    ::aiverse::view::ViewLayerProviders viewProviders;
    viewProviders.add("field visuals",[&](auto& q,const auto&){
        q.append("world fields",LayerPhase::Effects,[&]{
            if (!s.fields.empty() && s.fieldDefs) {
                if (fieldDefsSeen_ != s.fieldDefs ||
                    fieldDefsCount_ != s.fieldDefs->size()) {
                    fieldView_.setFields(*s.fieldDefs);
                    fieldDefsSeen_=s.fieldDefs;
                    fieldDefsCount_=s.fieldDefs->size();
                }
                const bool lowFields =
                    r.materialQuality()==MaterialQuality::Low ||
                    r.shadowQuality()==ShadowQuality::Off;
                fieldView_.draw(r,s.fields,vision,grid,*this,time,
                                lowFields,s.viewer);
            }
        });
    });
    viewProviders.add("visibility surface",[&](auto& q,const auto&){
        q.append("visibility surface",LayerPhase::Effects,[&]{
            if(vision && !vision->empty()) {
                const bool low=r.shadowQuality()==ShadowQuality::Off ||
                    r.shadowQuality()==ShadowQuality::Low;
                if(hasTerrain_ && !tiles && terrain_.shaderFog()) {
                    if(!surfaceLease)terrain_.setFog(&fog_.field(),fog_.look(),fogFieldChanged);
                }else if(surfaceLease)
                    fog_.drawCandidate(surfaceLease->fogSheet.get(),surfaceLease->fogUpdate.get(),r,time,low);
                else fog_.draw(r,time,low);
            }
            publication.mark(Epoch::Fog);
        });
    });
    viewProviders.add("scene illumination",[&](auto& q,const auto&){
        q.append("scene illumination",LayerPhase::Effects,[&]{
            lights_.draw(r,s,time,vision,grid,*this);
            publication.mark(Epoch::Lighting);
        });
    });
    viewProviders.add("world indicators",[&](auto& q,const auto&){
        q.append("world indicators",LayerPhase::Overlay,[&]{
            if(s.gameplayOverlays)overlays_.drawWorld(r,s,time,vision,grid,*this);
            publication.mark(Epoch::Overlays);
        });
    });
    ::aiverse::view::ViewLayerQueue latePasses;
    viewProviders.collect(latePasses,{time,dt,frame_});
    latePasses.flush();
    if(surfaceLease) {
        if(!surfaceLease->matches(*this,r,s,time,vision,grid,true) || !publication.ready())
            throw std::logic_error("LENS: source changed during private frame preparation");
        if(!surfaceLease->transaction->publish())
            throw std::logic_error("LENS: surface cohort was not prepared");
        ++surfacePrewarmStats_.consumed;
        if(surfaceLease->topology) {
            hasTerrain_=true;fogMeshDirty_=false;fieldView_.invalidate();
        }
        if(hasTerrain_ && !tiles && terrain_.shaderFog())
            terrain_.setFog(&fog_.field(),fog_.look(),surfaceLease->fogChanged);
    }
    if(!publication.finish())
        throw std::logic_error("GameView: incomplete visual frame publication");
    if(animationLease)animationLease->commit();
    // AIVERSE_STRATA_LOG=1: one "[strata]" line per 300 frames for STRATA on/off
    // acceptance runs (GLES/Vulkan on devices, desktop): static geometry scene
    // routing plus this frame's renderer counters. Diagnostic only.
    if (geometryScene_.instanceCount()>0) {
        static const bool strataLog=[]{const char* e=std::getenv("AIVERSE_STRATA_LOG");return e&&*e&&*e!='0';}();
        static unsigned strataFrames=0;static uint64_t lastDraws=0,lastClusters=0,lastFallbacks=0,lastTris=0;
        static double tris=0,calls=0,shadowTris=0;
        {   // per-frame numbers for the corner panel (drawStrataHud)
            static auto prev=std::chrono::steady_clock::now();
            const auto now=std::chrono::steady_clock::now();
            const double ms=std::chrono::duration<double,std::milli>(now-prev).count();prev=now;
            const auto& g=geometryScene_.stats();
            strataHud_.tris=double(g.strataPortableTriangles-strataHudLastTris_);
            strataHudLastTris_=g.strataPortableTriangles;
            strataHud_.legacyTris=double(r.profile().cameraModelTriangles);
            strataHud_.draws=double(r.drawCalls());
            strataHud_.ms=strataHud_.ms>0?strataHud_.ms*0.9+ms*0.1:ms;
        }
        if(strataLog){
            r.setProfiling(true); // triangle counters below need the renderer profile (from the next frame on)
            tris+=renderedTris_;calls+=renderedDraws_;
            shadowTris+=renderedShadowTris_;
            if(++strataFrames==300){
                // wall-clock average over the 300 frames (includes vsync waits)
                static auto lastWall=std::chrono::steady_clock::now();
                const auto nowWall=std::chrono::steady_clock::now();
                const double frameMs=std::chrono::duration<double,std::milli>(nowWall-lastWall).count()/300.0;
                lastWall=nowWall;
                const auto& g=geometryScene_.stats();
                char line[768];
                std::snprintf(line,sizeof line,
                    "[strata] enabled=%d instances=%zu staged=%u portableSources=%u autoStrata=%u autoLod=%u failures=%u "
                    "draws/frame=%.2f clusters/frame=%.1f fallbacks/frame=%.2f cameraModelTris/frame=%.0f shadowModelTris/frame=%.0f drawCalls/frame=%.1f "
                    "cooked=%u cookStale=%u cookWritten=%u stageMs=%.0f cookReadMs=%.0f buildMs=%.0f strataTris/frame=%.0f frameMs=%.2f fps=%.1f errorScale=%.2f rssMB=%.0f strataCpuMB=%.0f selectionGpuMB=%.1f",
                    int(geometryScene_.strataPortableRenderingEnabled()),geometryScene_.instanceCount(),
                    unsigned(g.strataSources),unsigned(g.strataPortableSources),unsigned(g.strataAutoStrataSources),
                    unsigned(g.strataAutoLodSources),unsigned(g.strataFailures),
                    double(g.strataPortableDraws-lastDraws)/300.0,double(g.strataPortableClusters-lastClusters)/300.0,
                    double(g.strataPortableFallbacks-lastFallbacks)/300.0,tris/300.0,shadowTris/300.0,calls/300.0,
                    unsigned(g.strataCookedSources),unsigned(g.strataCookStaleSources),unsigned(g.strataCookWrittenSources),
                    double(g.strataStageMs),double(g.strataCookedLoadMs),double(g.strataBuildMs),
                    double(g.strataPortableTriangles-lastTris)/300.0,frameMs,frameMs>0?1000.0/frameMs:0.0,double(g.strataErrorScale),processResidentMB(),
                    double(g.strataIndexBytes-std::min(g.strataIndexBytes,g.strataArchivedCpuIndexBytesFreed)+g.strataCompressedCpuIndexBytes+g.strataNodes*52ull+g.strataMeshlets*20ull)/1048576.0,double(g.strataSelectionGpuBytes)/1048576.0);
#if defined(__ANDROID__)
                __android_log_print(ANDROID_LOG_INFO,"AIVERSE","%s",line);
#else
                std::fprintf(stderr,"%s\n",line);
#endif
                lastDraws=g.strataPortableDraws;lastClusters=g.strataPortableClusters;lastTris=g.strataPortableTriangles;lastFallbacks=g.strataPortableFallbacks;
                strataFrames=0;tris=calls=shadowTris=0;
            }
        }
    }
    if (traceView) {
        static unsigned samples = 0;
        static double totals[5] = {};
        static double objectTotals[4] = {};
        static size_t objectCount = 0;
        static uint64_t placementHits = 0, placementMisses = 0;
        static uint64_t groundHits = 0, groundMisses = 0;
        static uint64_t staticFastHits = 0;
        static uint64_t culledTotal = 0;
        static uint64_t uniqueTypes = 0, contactFrames = 0;
        uniqueTypes += drawTypes_.size();
        contactFrames += contacts ? 1 : 0;
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b-a).count(); };
        totals[0] += ms(viewStart, afterSetup);
        totals[1] += ms(afterSetup, afterGround);
        totals[2] += ms(afterGround, afterObjects);
        totals[3] += ms(afterObjects, afterAnimations);
        totals[4] += ms(afterAnimations, std::chrono::steady_clock::now());
        objectTotals[0] += objectStateMs;
        objectTotals[1] += objectAnimateMs;
        objectTotals[2] += objectDrawMs;
        objectTotals[3] += objectShadowMs;
        objectCount += tracedObjects;
        placementHits += placementCacheHits_;
        placementMisses += placementCacheMisses_;
        groundHits += groundCacheHits_;
        groundMisses += groundCacheMisses_;
        staticFastHits += staticAnimateFastHits_;
        culledTotal += culledObjects;
        if (++samples == 120) {
            std::fprintf(stderr, "[rts-view-cull] avg/120 culled=%.1f objects/frame (AIVERSE_RTS_VIEW_CULL=0 disables)\n",
                         double(culledTotal)/samples);
            std::fprintf(stderr,
                "[view-stages] avg/120 setup=%.3f ground=%.3f objects=%.3f anim-cleanup=%.3f overlays=%.3f ms\n",
                totals[0]/samples, totals[1]/samples, totals[2]/samples,
                totals[3]/samples, totals[4]/samples);
            std::fprintf(stderr, "[view-objects] avg/120 state=%.3f animate=%.3f drawObject=%.3f shadow=%.3f ms objects/frame=%.1f\n",
                         objectTotals[0]/samples, objectTotals[1]/samples, objectTotals[2]/samples,
                         objectTotals[3]/samples, double(objectCount)/samples);
            if (placementHits + placementMisses)
                std::fprintf(stderr, "[rts-placement] avg/120 hits=%.1f misses=%.1f hit-rate=%.1f%%\n",
                    double(placementHits)/samples, double(placementMisses)/samples,
                    100.0 * double(placementHits)/double(placementHits+placementMisses));
            if (groundHits + groundMisses)
                std::fprintf(stderr,
                    "[rts-ground-cache] avg/120 hits=%.1f misses=%.1f hit-rate=%.1f%%\n",
                    double(groundHits)/samples, double(groundMisses)/samples,
                    100.0 * double(groundHits)/double(groundHits+groundMisses));
            if (staticFastHits)
                std::fprintf(stderr,
                    "[rts-static-animate] avg/120 fast-path=%.1f objects/frame\n",
                    double(staticFastHits) / samples);
            std::fprintf(stderr,
                "[rts-visible-types] avg/120 unique=%.1f catalog=%zu contact-frame=%.1f%%\n",
                double(uniqueTypes) / samples, looks_.size(),
                100.0 * double(contactFrames) / samples);
            uniqueTypes = contactFrames = 0;
            placementHits = placementMisses = groundHits = groundMisses = staticFastHits = culledTotal = 0;
            samples = 0;
            for (double& v : totals) v = 0.0;
            for (double& v : objectTotals) v = 0.0;
            objectCount = 0;
        }
    }
}

void GameView::drawGeometryInset(Renderer3D& r) {
    geometryScene_.setStrataDebugSuppressed(false);
    geometryScene_.setStrataInsetPass(true);
    geometryScene_.draw(r);
    geometryScene_.setStrataInsetPass(false);
    geometryScene_.setStrataDebugSuppressed(true);
}


void GameView::drawStrataHud(float w, float h) {
    static const char* debugEnv=std::getenv("AIVERSE_STRATA_DEBUG");
    const auto& g=geometryScene_.stats();
    const bool on=geometryScene_.strataPortableRenderingEnabled()&&g.strataPortableSources>0;
    std::string mode="off";
    if(debugEnv&&*debugEnv&&*debugEnv!='0'){
        const std::string d=debugEnv;mode.clear();
        if(d.find("fill")!=std::string::npos)mode+="clusters (fill) ";
        if(d.find("wire")!=std::string::npos)mode+="clusters (wire) ";
        if(d.find("bounds")!=std::string::npos)mode+="cluster bounds ";
        mode+=d.find("lod")!=std::string::npos?"| colour = LOD level":"| colour = cluster";
    }
    char text[768];
    std::snprintf(text,sizeof text,
        "STRATA: %s%s\n"
        "debug view: %s\n"
        "instances: %zu   sources: %u\n"
        "triangles drawn: %.2f M   (STRATA %.2f M)\n"
        "error threshold: x%.2f%s\n"
        "draw calls: %.0f\n"
        "memory: process %.0f MB | STRATA CPU %.0f MB, GPU indices %.0f MB + selection %.1f MB\n"
        "frame: %.1f ms (%.0f fps)",
        on?"ON":"OFF (LOD path)",
        on?(g.strataCookedSources?"  [cooked on disk]":"  [built at load]"):"",
        mode.c_str(),geometryScene_.instanceCount(),unsigned(g.strataPortableSources),
        renderedTris_/1e6,strataHud_.tris/1e6,double(g.strataErrorScale),g.strataErrorScale>1.001f?"  (triangle budget)":"",renderedDraws_,processResidentMB(),double(g.strataIndexBytes-std::min(g.strataIndexBytes,g.strataArchivedCpuIndexBytesFreed)+g.strataCompressedCpuIndexBytes+g.strataNodes*52ull+g.strataMeshlets*20ull)/1048576.0,
        double(g.strataPortableGpuIndexBytes)/1048576.0,double(g.strataSelectionGpuBytes)/1048576.0,strataHud_.ms,strataHud_.ms>0?1000.0/strataHud_.ms:0.0);
    ImDrawList* dl=ImGui::GetForegroundDrawList();
    if(strataDebugInset_ && insetTexture_){
        const float s2=std::clamp(h/52.f,11.f,ImGui::GetFontSize()*1.5f);
        const float iw=std::floor(w*.40f), ih=std::floor(iw*h/std::max(1.f,w)), m=s2*.6f;
        const ImVec2 p0{w-iw-m,m}, p1{w-m,m+ih};
        dl->AddRectFilled({p0.x-3,p0.y-3},{p1.x+3,p1.y+3+s2*1.4f},IM_COL32(0,0,0,200),4.f);
        dl->AddImage(ImTextureRef(ImTextureID(insetTexture_)),p0,p1,ImVec2(0,insetFlipY_?0.f:1.f),ImVec2(1,insetFlipY_?1.f:0.f));
        dl->AddRect({p0.x-1,p0.y-1},{p1.x+1,p1.y+1},IM_COL32(255,255,255,220),0,0,2.f);
        const char* label=geometryScene_.strataDebugView().color==render::strata::DebugColorMode::LodDepth
            ? "STRATA clusters - colour: LOD level (red coarse, blue fine)" : "STRATA clusters - colour: cluster";
        dl->AddText(ImGui::GetFont(),s2,{p0.x+s2*.3f,p1.y+s2*.2f},IM_COL32(255,255,255,235),label);
    }
    if(geometryScene_.strataDebugView().enabled && geometryScene_.strataDebugRightHalf()){
        const float x=std::floor(w*.5f);
        dl->AddLine({x,0},{x,h},IM_COL32(255,255,255,200),2.f);
        const float s2=std::clamp(h/52.f,11.f,ImGui::GetFontSize()*1.5f);
        dl->AddText(ImGui::GetFont(),s2,{x+s2*.6f,h-s2*1.6f},IM_COL32(255,255,255,230),"STRATA debug: clusters");
        dl->AddText(ImGui::GetFont(),s2,{s2*.6f,h-s2*1.6f},IM_COL32(255,255,255,230),"final image");
    }
    const float size=std::clamp(h/52.f,11.f,ImGui::GetFontSize()*1.5f);
    ImFont* font=ImGui::GetFont();
    const ImVec2 extent=font->CalcTextSizeA(size,1e9f,0,text);
    const ImVec2 at{size*0.6f,size*0.6f};
    dl->AddRectFilled({at.x-size*.4f,at.y-size*.3f},{at.x+extent.x+size*.4f,at.y+extent.y+size*.3f},IM_COL32(0,0,0,170),size*.25f);
    dl->AddText(font,size,at,IM_COL32(255,255,255,255),text);
}

void GameView::drawOverlays(const Camera& cam, const GameScene& s, float w, float h, float ox, float oy, void* drawList) {
    // bars, garrison counts, labels, damage numbers: GameOverlays (built-in overlays reproduce the old look)
    if(s.gameplayOverlays)overlays_.drawScreen(cam, s, w, h, ox, oy, drawList, *this);
}

bool GameView::overlayTop(const Camera& cam, const SceneObject& o, size_t oi, float w, float h, float2& sp) const {
    if (!rules_ || o.type >= rules_->types.size() || o.type >= looks_.size()) return false;
    const UnitType& t = rules_->types[o.type];
    if (planets_.active()) // anchors computed by drawPlanets (view world; hidden behind the planet / far away)
        return oi < bars_.size() && oi < barOk_.size() && barOk_[oi] && cam.worldToScreen(bars_[oi], w, h, sp);
    if (twoD_ && twoD_->hasSprite(o.type) && oi < bars_.size()) {
        if (!cam.worldToScreen(bars_[oi], w, h, sp)) return false;
        sp.y -= 4;
        return true;
    }
    float top = t.structure ? 1.6f * looks_[o.type].scale * std::max(1.0f, std::max(t.footprintW, t.footprintH) * 0.6f) : 1.2f * looks_[o.type].scale;
    if (looks_[o.type].air) top += 2.4f;
    return cam.worldToScreen({o.pos.x, top, o.pos.z}, w, h, sp);
}

void sceneFromSim(Simulation& sim, GameScene& out, float alpha, uint8_t viewer) {
    World& w = sim.world();
    auto* m = dynamic_cast<GameCore*>(&sim.module());
    if (!m) return;
    const Rules& R = m->rules();
    GameState& gs = m->state(sim);
    out.objects.clear();
    out.entityFrames.clear();
    out.roads.clear();
    out.events = sim.events();
    out.tick = sim.tick();
    if (++out.facingFrame % 256 == 0)   // forget the facing of units gone from the scene
        for (auto it = out.facingTracks.begin(); it != out.facingTracks.end();)
            it = out.facingFrame - it->second.seen > 256 ? out.facingTracks.erase(it) : std::next(it);
    out.viewer = viewer;
    out.lanes = R.lanes.enabled ? &gs.lanes : nullptr;
    out.laneSignature = gs.laneSignature;
    out.seconds = double(sim.tick()) / double(std::max(sim.tickRate(), 1u));
    out.fields.clear();
    out.fieldDefs = nullptr;
    if (GridFields* gf = m->gridFields(); gf && gf->active())
        if (const GridFieldStore* fs = sim.world().resource<GridFieldStore>()) {
            out.fieldDefs = &gf->defs();
            for (size_t i = 0; i < fs->fields.size(); ++i) {
                const auto& f = fs->fields[i];
                out.fields.push_back({int(i), f.w, f.h, gf->defs()[i].resolution, f.layers, f.v.data(), f.version});
            }
        }
    if (const Surface* surf = gs.surface.get()) { sceneFromSphere(sim, *m, *surf, out, alpha, viewer); return; }   // GamePlanetView.cpp
    const Driving* drivingCars = m->driving() && m->driving()->active() ? m->driving() : nullptr;
    // The view configuration is shared by every entity of a type. Avoid
    // repeating JSON object lookups for thousands of identical scenery types.
    std::vector<uint8_t> territoryTeamTypes(R.types.size(), 0);
    for (size_t type = 0; type < R.types.size(); ++type)
        territoryTeamTypes[type] = uint8_t(R.types[type].view.getBool("teamFromTerritory", false));
    auto* arsenal = m->arsenal();
    auto* topDown = m->topDown();
    // radar / sonar contacts of the viewer outside its sight: drawn as blips (Sensors.h)
    std::vector<Entity> blips;
    if (Sensors* sn = m->sensors(); sn && viewer != 0) { blips = sn->blips(sim, viewer); std::sort(blips.begin(), blips.end(), [](Entity a, Entity b) { return a.index < b.index; }); }
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner& o, Transform& t) {
        if (u.flags & kUnitInCargo) return;   // inside a transport (agent-mech)
        SceneObject so;
        so.entity = e; so.type = u.type; so.owner = o.player;
        if (arsenal) if (auto* hand = arsenal->hand(sim, o.player))
            if (hand->hero == e && hand->current >= 0 && size_t(hand->current) < arsenal->rules().weapons.size()) {
                so.heldWeapon = arsenal->rules().weapons[hand->current].weapon;
                if (topDown) if (auto* pilots = topDown->store(sim))
                    if (o.player < pilots->pilots.size())
                        so.weaponAim = pilots->pilots[o.player].input.aim || pilots->pilots[o.player].input.fire;
            }
        float3 a = toF(t.prevPos), b = toF(t.pos);
        so.pos = lerp(a, b, alpha);
        so.facing = toF(u.facing);
        {   // interpolate the facing between the last two ticks (t.prevPos -> t.pos has the same alpha)
            auto [it, fresh] = out.facingTracks.try_emplace(e.packed());
            GameScene::FacingTrack& ft = it->second;
            if (fresh || (t.prevPos == t.pos && ft.tick != sim.tick())) { ft.prev = ft.cur = so.facing; }   // new, or teleported / standing
            else if (ft.tick != sim.tick()) { ft.prev = ft.cur; ft.cur = so.facing; }
            ft.tick = sim.tick(); ft.seen = out.facingFrame;
            // Stationary structures and unchanged facings already have the
            // correct direction. Avoid interpolation, sqrt and division for
            // the common static RTS path, without changing turning behaviour.
            if (ft.prev.x != ft.cur.x || ft.prev.y != ft.cur.y || ft.prev.z != ft.cur.z) {
                const float3 f{ft.prev.x + (ft.cur.x - ft.prev.x) * alpha, ft.prev.y + (ft.cur.y - ft.prev.y) * alpha, ft.prev.z + (ft.cur.z - ft.prev.z) * alpha};
                const float fl = std::sqrt(f.x * f.x + f.z * f.z);
                if (fl > 1e-3f && ft.prev.x * ft.cur.x + ft.prev.z * ft.cur.z > 0) so.facing = {f.x / fl, f.y, f.z / fl};   // a flip over 90 degrees snaps
            }
        }
        const UnitType& ut = R.types[u.type];
        if (territoryTeamTypes[u.type]) {
            const int x = int(t.pos.x.floorInt()), z = int(t.pos.z.floorInt());
            if (gs.grid.inside(x, z)) {
                const size_t cell = size_t(gs.grid.index(x, z));
                if (cell < gs.grid.territory.size()) so.teamOwner = gs.grid.territory[cell];
            }
        }
        so.hp = ut.maxHp.raw > 0 ? float((u.hp / ut.maxHp).toDouble()) : 1.0f;
        if (!gs.routes.walks.empty()) so.fade = float(m->routeAlpha(sim, e).toDouble());   // route end fade
        if (auto* c = w.tryGet<Construction>(e)) so.build = float(c->progress.toDouble());
        so.unpowered = (u.flags & kUnitDisabled) || (o.player < gs.players.size() && gs.players[o.player].productionMult < Fixed::fromInt(1));
        if (auto* ap = w.tryGet<AuthoredPose>(e)) {
            so.posed = true; so.poseHasY = ap->hasY;
            so.poseY = float(ap->y.toDouble());
            so.poseRotX = float(ap->rotX.toDouble()); so.poseRotZ = float(ap->rotZ.toDouble());
            so.poseScale = toF(ap->scale);
        }
        if (auto* g = w.tryGet<Garrison>(e)) so.garrison = g->count;
        if (auto* tp = w.tryGet<Transport>(e); tp && !tp->cargo.empty()) so.garrison = int(tp->cargo.size());   // passengers
        if (auto* c = w.tryGet<Cargo>(e)) { so.carry = float(c->amount.toDouble()); so.carryRes = c->res; }
        if (auto* od = w.tryGet<Orders>(e); od && !od->queue.empty()) so.order = uint8_t(od->queue.front().type) + 1;
        if (auto* mb = w.tryGet<Mobile>(e)) so.moving = mb->moving;
        if (drivingCars) if (const vehicles::VehicleDrive* car = drivingCars->car(sim, e)) so.steer = float(car->controller.steer.toDouble()) * 6.2831853f;
        if (auto* pr = w.tryGet<Production>(e)) so.producing = !pr->queue.empty();
        if (auto* cb = w.tryGet<Combat>(e); cb && cb->target.valid() && w.alive(cb->target)) {
            if (auto* tt = w.tryGet<Transform>(cb->target)) { so.hasTarget = true; so.target = lerp(toF(tt->prevPos), toF(tt->pos), alpha); }
        }
        if (so.moving) so.action = kActMove;
        else if (so.hasTarget) so.action = kActAttack;
        else if (so.order) {
            OrderType ot = OrderType(so.order - 1);
            if (ot == OrderType::Gather || ot == OrderType::Build || ot == OrderType::Repair) so.action = kActWork;
        }
        if (viewer != 0 && o.player != viewer && !sim.allied(o.player, viewer) && !m->canSee(sim, e, viewer)) so.visible = false;   // cloaked, not detected
        else if (viewer != 0 && o.player != viewer && !sim.allied(o.player, viewer)) {
            // enemies only when visible; neutral structures (resources) once explored
            int x = int(t.pos.x.floorInt()), y = int(t.pos.z.floorInt());
            uint8_t v = gs.grid.inside(x, y) ? gs.vision[viewer][size_t(gs.grid.index(x, y))] : 0;
            so.visible = ut.structure && o.player == 0 ? v >= 1 : v == 2;
        }
        if (so.visible && ut.trap >= 0 && viewer != 0 && o.player != viewer && !sim.allied(o.player, viewer) && w.has<TrapState>(e) &&
            (m->statusFlags(sim, e) & kStInvisible) && !(m->statusFlags(sim, e) & kStRevealed)) so.visible = false;   // hidden trap
        if (!so.visible && !blips.empty()) {
            auto it = std::lower_bound(blips.begin(), blips.end(), e, [](Entity a, Entity b) { return a.index < b.index; });
            so.blip = it != blips.end() && *it == e;
        }
        out.objects.push_back(std::move(so));
    });
    // bodies of native modules (kHookScene): drawn as unit types
    if (m->native().has(kHookScene)) {
        std::vector<NativeModule::Body> bodies;
        m->native().sceneBodies(*m, sim, bodies);
        for (auto& b : bodies) {
            if (b.type >= R.types.size()) continue;
            SceneObject so;
            so.type = b.type; so.owner = 0;
            so.pos = toF(b.pos);
            const float3 f = toF(b.facing);
            const float fl = std::sqrt(f.x * f.x + f.z * f.z);
            if (fl > 1e-4f) so.facing = {f.x / fl, 0, f.z / fl};
            so.moving = b.moving && fl > 1e-4f;
            if (viewer != 0) {
                int x = int(b.pos.x.floorInt()), y = int(b.pos.z.floorInt());
                so.visible = gs.grid.inside(x, y) && gs.vision[viewer][size_t(gs.grid.index(x, y))] == 2;
            }
            out.objects.push_back(std::move(so));
        }
    }
    w.each<Projectile, Transform>([&](Entity e, Projectile& pr, Transform& t) {
        SceneObject so;
        so.entity = e; so.isProjectile = true; so.source = pr.source;
        if (auto* su = w.tryGet<Unit>(pr.source)) { so.shooterType = su->type; if (pr.sourceType == su->type) so.shotWeapon = pr.weapon; } // picks the projectile's light and size
        so.pos = lerp(toF(t.prevPos), toF(t.pos), alpha);
        if (viewer != 0) {
            int x = int(t.pos.x.floorInt()), y = int(t.pos.z.floorInt());
            so.visible = gs.grid.inside(x, y) && gs.vision[viewer][size_t(gs.grid.index(x, y))] == 2;
        }
        out.objects.push_back(std::move(so));
    });
    for (size_t i = 0; i < gs.roads.nodes.size(); ++i)
        for (uint16_t j : gs.roads.adj[i]) {
            if (j <= i) continue;
            auto* ta = w.tryGet<Transform>(gs.roads.nodes[i]);
            auto* tb = w.tryGet<Transform>(gs.roads.nodes[j]);
            if (ta && tb) out.roads.push_back({toF(ta->pos), toF(tb->pos)});
        }
}

} // namespace aiverse::game
