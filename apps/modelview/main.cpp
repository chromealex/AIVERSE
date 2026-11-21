// aiverse_modelview: look at models the way the game renders them (materials, shadows, skinning,
// clips, variants, team colours, quality tiers). For artists checking an export and for CI
// screenshots of the model pipeline.
//
//   aiverse_modelview a.glb b.gltf@Walk c.obj [--clip Walk] [--time 0.4] [--variant winter] [--baked]
//                 [--team 0.9,0.2,0.2] [--quality low|medium|high] [--height 1.5] [--native] [--distance 6]
//                 [--yaw 20] [--pitch 30] [--spacing 2.2]
//                 [--lod-report] [--no-lod] [--lod-fade 0.15] [--shadows off|low|medium|high]
//   file@Clip plays that clip on that instance. Keys: Space next clip, Q quality, V variant,
//   P pause, T team colour; drag to orbit, wheel to zoom. AIVERSE_SCREENSHOT works as in the client.
//
//   aiverse_modelview --game templates/showcase [--types footman,catapult] [--start 4] [--fixed-dt 0.016]
//   gallery: every type of the game through the game's own view (view blocks, overrides,
//   procedural animation), cycling idle -> move -> attack -> death -> respawn.
#include "../../engine/platform/Platform.h"
#include "../../engine/render/Animation.h"
#include "../../engine/render/ImGuiLayer.h"
#include "../../engine/render/ModelMerge.h"
#include "../../engine/render/Renderer3D.h"
#include "../../engine/render/ModelAssetProvider.h"
#include "../../engine/render/FailedModelUpload.h"
#include "../../engine/view/View.h"
#include "../../engine/view/ImportedAttachmentAuthoring.h"
#include "../../engine/view/AttachmentLayout.h"
#include "../../engine/view/SceneAttachmentHost.h"
#include "../../engine/view/AnimatedSceneAttachmentSources.h"
#include "imgui.h"
#include "Gallery.h"
#include "NativeScenePreview.h"
#include "NativeFacialPreview.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <span>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::render;

namespace {

using Imported = ::aiverse::view::ImportedAttachmentAuthoring;
struct AttachmentSpec {
    std::string modelSource;
    std::string socketName;
    bool enabled=true;
    render::mat4 local{};
    render::mat4 childWorld{};
    std::string id{}; // optional scene/prefab-stable authored child identity
    bool animated=false;
    std::string animationGraph{};
};
struct Instance {
    Model* model = nullptr;
    std::string path;
    Animator anim;
    Pose pose;
    modelview::NativeFacialPreview facial;
    std::vector<mat4> world;
    int clip = -1, variant = -1;
    mat4 fix;
    ::aiverse::view::AppearanceAssetRegistry::Snapshot publishedAppearance;
    Imported::Compiled compiledAttachments;
    bool attachmentsReady=false;
    std::string lastAttachmentError;
};

// All parent Animator objects and their borrowed Model* stay with the
// provider that created them until a GPU completion proof permits retirement.
struct NativeAttachmentGeneration {
    World world;
    std::unique_ptr<::aiverse::view::SceneAttachmentHost> scenes;
    ::aiverse::view::AnimatedSceneAttachmentSources animations;
    // Host-owned live overrides, distinct from immutable Graph defaults.
    std::map<std::pair<size_t,std::string>,render::AnimationParameters>
        parameterOverrides;
    std::vector<std::map<std::string,
        ::aiverse::view::AppearanceAssetRegistry::Snapshot>> childGraphs;
    NativeAttachmentGeneration()
        : scenes(std::make_unique<::aiverse::view::SceneAttachmentHost>(world)) {}
    void clear() {
        animations.clear();
        scenes->clear();
        childGraphs.clear();
        parameterOverrides.clear();
    }
    ~NativeAttachmentGeneration() { clear(); }
};

struct RetiredNativeScene {
    std::unique_ptr<ModelAssetProvider> provider;
    std::unique_ptr<::aiverse::view::AppearanceAssetRegistry> appearances;
    std::vector<Instance> instances;
    std::unique_ptr<NativeAttachmentGeneration> generation;
};

struct Viewer {
    std::vector<std::pair<std::string, std::string>> files;   // path, clip
    std::string clipAll, variantName;
    bool lodReport = false, noLod = false;
    float lodFadeWidth=.15f;
    ShadowQuality shadowQuality=ShadowQuality::High;
    bool native = false;
    bool merge = true;   // static batching of parts that share a material (--no-merge: as authored)
    bool providerMode=false; // --provider uses the genre-free owner for static raw files
    float seek = -1, height = 0, spacing = 2.2f, distance = 0, yawDeg = 25, pitchDeg = 32;
    float3 team{0.92f * 0.92f, 0.22f * 0.22f, 0.20f * 0.20f};
    MaterialQuality quality = MaterialQuality::High;
    bool qualitySet = false, paused = false, baked = false;
    std::string gameDir;
    std::vector<std::string> types;
    float start = 0, fixedDt = 0;
    std::unique_ptr<modelview::Gallery> gallery;
    // Opt-in generic SceneAsset and MotionWeave native integration;
    // existing --game, --provider and attachment paths remain unchanged.
    std::string sceneRoot,sceneStart="scenes/motionweave-global";
    std::unique_ptr<render::RenderDeviceLifetime> sceneLifetime;
    std::unique_ptr<modelview::NativeScenePreview> scenePreview;
    bool restoreSceneSnapshotRequested=false;
    // A Studio-owned project/facial-rigs/<name>.json is read-only here.
    // No browser filesystem access or new editor UI is involved.
    std::string facialRigFile;
    modelview::NativeFacialPreview::Scalars facialScalars;
    modelview::NativeFacialPreview::Vectors facialVectors;
    std::filesystem::file_time_type facialStamp{};
    bool facialStampValid=false;
    double facialPollElapsed=0.;
    std::string lastFacialError;

    gfx::Device* dev = nullptr;
    Renderer3D r3d;
    ImGuiLayer gui;
    RTSCamera cam;
    std::map<std::string, std::unique_ptr<Model>> models;
    std::unique_ptr<ModelAssetProvider> nativeModels=
        std::make_unique<ModelAssetProvider>();
    std::unique_ptr<::aiverse::view::AppearanceAssetRegistry> attachmentsRegistry=
        std::make_unique<::aiverse::view::AppearanceAssetRegistry>();
    std::vector<std::unique_ptr<RetiredNativeScene>> retiredNativeScenes;
    static constexpr size_t kRetiredNativeLimit=8;
    bool reimportRequested=false, watchModelFiles=false;
    double watchElapsed=0.;
    std::map<std::string,std::filesystem::file_time_type> sourceStamps;
    std::string nativeReloadError;
    ::aiverse::View attachmentView; // compose borrowed precomputed poses only
    ::aiverse::View animatedChildView; // graphless independently evaluated poses
    std::unique_ptr<NativeAttachmentGeneration> attachmentGeneration;
    std::vector<AttachmentSpec> attachmentSpecs;
    std::string attachmentConfigPath;
    char newAttachmentModel[512]{},newAttachmentSocket[256]{};
    bool attachmentPreview=false,attachmentsDirty=false;
    std::map<std::string, std::string> mergeInfo;   // path -> "parts 47 -> 14" or why not merged
    std::vector<Instance> inst;
    std::vector<std::string> log;
    Mesh ground;
    double time = 0;
    int variant = -1, teamIdx = 0;
    bool dragging = false;
    float lastX = 0, lastY = 0;

    static bool readFile(const std::string& p, std::string& out) {
        std::ifstream f(p, std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }
        return platform::readContentFile(p, out);
    }

    void init(gfx::Device& d) {
        dev = &d;
        loadAttachmentConfig();
        // This attachment preview uses ONE real ModelAssetProvider for both
        // source rig and imported children. The original comparison/gallery
        // paths remain unchanged unless the user opts into attachments.
        if(!attachmentSpecs.empty() || !attachmentConfigPath.empty() ||
           watchModelFiles) {
            if(!gameDir.empty())
                throw std::invalid_argument(
                    "attachment preview takes model files, not --game gallery");
            attachmentPreview=true;
            providerMode=true;
            merge=false;
            baked=false;
        }
        r3d.init(d);
        r3d.setProfiling(lodReport);
        r3d.setModelLodEnabled(!noLod);
        r3d.setShadowQuality(shadowQuality);
        r3d.setModelLodFadeWidth(lodFadeWidth);
        r3d.setMaterialQuality(qualitySet ? quality : Renderer3D::defaultMaterialQualityFor(d, platform::isTouchDevice()));
        gui.init(d, "engine/fonts/ui.ttf", 15.0f);
        MeshBuilder gb;
        gb.color(rgba(120, 132, 110)).box({0, -0.05f, 0}, {60, 0.1f, 60});
        ground = gb.upload(d);
        time = start;
        if(!facialRigFile.empty()) {
            if(!sceneRoot.empty() || !gameDir.empty() ||
               attachmentPreview || baked)
                throw std::invalid_argument(
                    "--facial-rig requires normal ModelView models, not scenes, gallery, attachments or baked clips");
            // Keep imported node/mesh morph semantic names untouched.
            merge=false;
        }
        if (!sceneRoot.empty()) {
            if(!gameDir.empty() || !files.empty() || attachmentPreview)
                throw std::invalid_argument(
                    "--scene-preview cannot be combined with model/gallery/attachments");
            sceneLifetime=std::make_unique<render::RenderDeviceLifetime>(d);
            scenePreview=std::make_unique<modelview::NativeScenePreview>();
            std::string error;
            if(!scenePreview->init(
                    d,sceneLifetime->lease(),sceneRoot,sceneStart,error)) {
                log.push_back("native scene preview: "+error);
                throw std::runtime_error(log.back());
            }
            cam.target={1.5f,0.5f,0.f};
            cam.distance=distance>0?distance:8.f;
            cam.yaw=yawDeg*3.14159265f/180.f;
            cam.pitch=pitchDeg*3.14159265f/180.f;
            cam.minDistance=1.f;
            return;
        }
        if (!gameDir.empty()) {
            gallery = std::make_unique<modelview::Gallery>();
            std::string err;
            if (!gallery->init(d, gameDir, types, err)) { log.push_back(err); gallery.reset(); }
            else for (auto& w : gallery->warnings()) log.push_back(w);
            if (gallery) {
                cam.target = gallery->center();
                cam.distance = distance > 0 ? distance : gallery->extent() * 0.75f;
                cam.yaw = yawDeg * 3.14159265f / 180.0f;
                cam.pitch = pitchDeg * 3.14159265f / 180.0f;
                cam.minDistance = 1;
                for (auto& l : log) std::printf("%s\n", l.c_str());
                return;
            }
        }
        // The standalone native provider deliberately keeps source geometry
        // untouched. Existing merged/animated comparisons retain their
        // historic loader and all of its authoring/animation semantics.
        if(providerMode)nativeModels->setReader(readFile);
        float x = -spacing * float(files.size() - 1) * 0.5f, maxH = 1;
        for (auto& [path, clip] : files) {
            Model* resolved=nullptr;
            if(providerMode && !merge && !baked &&
               ((clip.empty() && clipAll.empty()) || attachmentPreview)) {
                resolved=nativeModels->load(d,path);
                if(resolved) {
                    for(auto& mt:resolved->materials) {
                        std::string n=mt.name;
                        for(auto& ch:n)
                            ch=char(std::tolower(static_cast<unsigned char>(ch)));
                        if(n.find("team")!=std::string::npos &&
                           mt.team==TeamMode::None)mt.team=TeamMode::Tint;
                    }
                    log.push_back(path+": generic native source provider");
                }
            }
            auto it = models.find(path);
            if(!resolved && it == models.end()) {
                // Create map storage before any GPU upload; a later
                // allocation failure must never destroy GPU-live Models.
                it = models.try_emplace(path).first;
                std::unique_ptr<Model> m;
                bool uploadStarted=false;
                try {
                    std::vector<std::string> warn;
                    m = loadModel(path, readFile, &warn);
                    if(!m)throw std::runtime_error("model import returned no data");
                    for (auto& w : warn) log.push_back(w);
                    if (merge) {
                        const auto mr = mergeStaticPrimitives(*m);
                        mergeInfo[path] = mr.merged ? "parts " + std::to_string(mr.primitivesBefore) + " -> " + std::to_string(mr.primitivesAfter) +
                                                          (mr.shadowBatched ? ", shadow 1 draw for " + std::to_string(mr.shadowBatched) : "")
                                                    : "parts not merged: " + mr.reason;
                        log.push_back(path + ": " + mergeInfo[path]);
                    }
                    uploadStarted=true;
                    if(!uploadModel(d, *m))
                        throw std::runtime_error("GPU Model upload returned false");
                // as the game view does by default: materials named "*team*" take the team colour
                for (auto& mt : m->materials) {
                    std::string n = mt.name;
                    for (auto& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (n.find("team") != std::string::npos && mt.team == TeamMode::None) mt.team = TeamMode::Tint;
                }
                    char buf[256];
                    std::snprintf(buf, sizeof buf, "%s: %zu nodes, %zu meshes, %zu materials, %zu textures, %zu skins, %zu clips",
                                  path.c_str(), m->nodes.size(), m->meshes.size(), m->materials.size(), m->images.size(), m->skins.size(), m->clips.size());
                    log.push_back(buf);
                    for (auto& c : m->clips) log.push_back("  clip '" + c.name + "' " + std::to_string(c.duration) + " s");
                    for (auto& im : m->images) if (im.gpuCompressed) log.push_back("  texture '" + im.name + "' stays GPU-compressed");
                    if (m->baked.tex.valid() || bakeAnimations(d, *m)) log.push_back("  clips baked: " + std::to_string(m->baked.rows) + " rows at " + std::to_string(int(m->baked.fps)) + " fps");
                    for (auto& v : m->variants) log.push_back("  variant '" + v + "'");
                } catch (const std::exception& e) {
                    if(uploadStarted) {
                        const auto status=abortFailedModelUpload(d,m);
                        if(status==FailedModelUploadDisposition::OrphanedWithoutGpuProof)
                            log.push_back(path+": GPU completion unavailable; partial Model quarantined");
                    } else m.reset();
                    log.push_back(path+": "+e.what());
                }
                it->second = std::move(m);
            }
            Instance in;
            in.model = resolved ? resolved : it->second.get();
            in.path = path;
            if (in.model) {
                Model& m = *in.model;
                float3 mn = m.boundsMin, mx = m.boundsMax;
                float s = height > 0 && mx.y > mn.y ? height / (mx.y - mn.y) : 1.0f;
                // formats with their own units (voxels, Warcraft, Generals, Collada in cm) would put the
                // camera inside the model: frame anything far from game size at 2 units (--native keeps it)
                const float big = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
                if (height <= 0 && !native && big > 1e-6f && (big > 6.0f || big < 0.2f)) {
                    s = 2.0f / big;
                    log.push_back(path + ": shown at " + std::to_string(s) + "x (native size " + std::to_string(big) + " units; --native to keep it)");
                }
                in.fix = mat4::translate({x, 0, 0}) * mat4::scale({s, s, s}) * mat4::translate({-(mn.x + mx.x) * 0.5f, -mn.y, -(mn.z + mx.z) * 0.5f});
                maxH = std::max(maxH, (mx.y - mn.y) * s);
                in.anim.setModel(&m);
                std::string want = clip.empty() ? clipAll : clip;
                in.clip = want.empty() ? (m.clips.empty() ? -1 : 0) : m.findClip(want);
                if (!want.empty() && in.clip < 0) log.push_back(path + ": no clip '" + want + "'");
                play(in);
                if (!variantName.empty()) in.variant = m.findVariant(variantName);
            }
            inst.push_back(std::move(in));
            x += spacing;
        }
        if(!facialRigFile.empty()) {
            std::string error;
            if(!reloadFacialRig(error))
                throw std::runtime_error("facial rig preview unavailable: "+error);
            std::error_code ec;
            facialStamp=std::filesystem::last_write_time(facialRigFile,ec);
            facialStampValid=!ec;
        }
        if(attachmentPreview)rebuildAttachments();
        for (auto& l : log) std::printf("%s\n", l.c_str());
        cam.target = {0, maxH * 0.45f, 0};
        cam.distance = distance > 0 ? distance : std::max(4.0f, spacing * float(files.size()) * 1.1f + maxH);
        cam.yaw = yawDeg * 3.14159265f / 180.0f;
        cam.pitch = pitchDeg * 3.14159265f / 180.0f;
        cam.minDistance = 1;
    }

    // ModelView is an actual file-backed authoring host. It never stores
    // permanent read leases in its compiled bundles: only the provider owns
    // Models and the draw-scoped Prepared object holds temporary leases.
    void loadAttachmentConfig() {
        if(attachmentConfigPath.empty())return;
        std::string contents;
        if(!readFile(attachmentConfigPath,contents))return; // new file
        try {
            ::aiverse::view::AttachmentLayout document;
            ::aiverse::view::AttachmentLayoutCodec::read(
                Json::parse(contents),document);
            std::vector<AttachmentSpec> fromFile;
            fromFile.reserve(document.attachments.size()+attachmentSpecs.size());
            for(const auto& entry:document.attachments)
                fromFile.push_back({
                    entry.model,entry.socket,entry.enabled,
                    entry.local,entry.childWorld,entry.id,
                    entry.animated,entry.animationGraph});
            fromFile.insert(fromFile.end(),attachmentSpecs.begin(),attachmentSpecs.end());
            attachmentSpecs=std::move(fromFile);
        }catch(const std::exception& e) {
            log.push_back(std::string("attachment config rejected: ")+e.what());
        }
    }

    void saveAttachmentConfig() {
        try {
            if(attachmentConfigPath.empty())
                attachmentConfigPath=files.empty()?"attachments.json":
                    files.front().first+".attachments.json";
            ::aiverse::view::AttachmentLayout document;
            document.parent=files.empty()?std::string{}:files.front().first;
            for(const auto& entry:attachmentSpecs)
                document.attachments.push_back({
                    entry.modelSource,entry.socketName,entry.enabled,
                    entry.local,entry.childWorld,entry.id,
                    entry.animated,entry.animationGraph});
            // Validate duplicate authored IDs and transform matrices BEFORE
            // opening/truncating a previously valid on-disk layout file.
            const auto root=::aiverse::view::AttachmentLayoutCodec::write(document);
            std::ofstream file(attachmentConfigPath,std::ios::binary|std::ios::trunc);
            if(!file || !(file<<root.dump(2)))
                log.push_back("failed to save attachment config: "+attachmentConfigPath);
            else log.push_back("saved attachment config: "+attachmentConfigPath);
        }catch(const std::exception& e) {
            log.push_back(std::string("attachment save rejected: ")+e.what());
        }
    }

    // Compile an entire attachment configuration in any provider generation.
    // Both normal layout edits and on-disk Model hot-reimport call this same
    // genre-neutral API. No live instance is mutated before all entries pass.
    bool buildAttachmentLayout(
            ModelAssetProvider& provider,
            ::aiverse::view::AppearanceAssetRegistry& registry,
            std::vector<Instance>& actors,
            std::unique_ptr<NativeAttachmentGeneration>& outputGeneration,
            std::string& error) {
        std::vector<Imported::Compiled> staged;
        std::vector<::aiverse::view::AppearanceAssetRegistry::Snapshot> parents;
        staged.reserve(actors.size());
        parents.reserve(actors.size());
        std::map<std::string,::aiverse::view::AppearanceAssetRegistry::Snapshot> children;
        auto generation=std::make_unique<NativeAttachmentGeneration>();
        generation->childGraphs.resize(actors.size());
        try {
            // Authoring identity/affine validation happens BEFORE any GPU
            // import, including disabled entries and duplicate stable IDs.
            ::aiverse::view::AttachmentLayout check;
            check.parent=files.empty()?std::string{}:files.front().first;
            for(const auto& child:attachmentSpecs)
                check.attachments.push_back({
                    child.modelSource,child.socketName,child.enabled,
                    child.local,child.childWorld,child.id,
                    child.animated,child.animationGraph});
            (void)::aiverse::view::AttachmentLayoutCodec::write(check);
            // Animated child sources are intentionally excluded from static
            // parent sockets. Their graph clocks belong to this generation.
            // Import every real child Model before taking any source read
            // lease: the cache cannot evict/replace under active readers.
            for(const auto& entry:attachmentSpecs) {
                if(!entry.enabled)continue;
                if(entry.modelSource.empty() || entry.socketName.empty())
                    throw std::invalid_argument(
                        "attachment needs a Model file and named socket");
                if(entry.animated || children.count(entry.modelSource))continue;
                ::aiverse::view::AppearanceAssetRegistry::Snapshot published;
                ::aiverse::view::AppearanceAssetRegistry::Request request;
                if(!Imported::importChild(
                        provider,*dev,registry,
                        "modelview/child/"+entry.modelSource,
                        entry.modelSource,std::move(request),published,{},&error))
                    throw std::runtime_error(error);
                children.emplace(entry.modelSource,std::move(published));
            }
            // Import every animated child Model before ANY parent cache read
            // lease is held. An import may evict/reallocate cache entries.
            std::map<std::string,Model*> animatedModels;
            for(const auto& child:attachmentSpecs) {
                if(!child.enabled || !child.animated ||
                   animatedModels.count(child.modelSource))continue;
                auto* imported=provider.load(*dev,child.modelSource);
                if(!imported)
                    throw std::runtime_error("animated child Model import failed: "+
                                             child.modelSource);
                animatedModels.emplace(child.modelSource,imported);
            }
            for(size_t i=0;i<actors.size();++i) {
                auto& actor=actors[i];
                if(!actor.model)
                    throw std::invalid_argument("parent Model is unavailable");
                auto lease=provider.resources().acquireModelRead(actor.model);
                if(!lease)
                    throw std::invalid_argument(
                        "parent Model must belong to the same provider");
                ::aiverse::view::AppearanceAssetRegistry::Request req;
                req.model=actor.model;
                req.sources.push_back(actor.path);
                const auto key="modelview/source/"+std::to_string(i);
                if(!registry.publish(key,req,{}, {},&error))
                    throw std::runtime_error(error);
                auto parent=registry.find(key);
                std::vector<Imported::ChildRequest> requested;
                requested.reserve(attachmentSpecs.size());
                for(const auto& entry:attachmentSpecs) {
                    if(!entry.enabled || entry.animated)continue;
                    requested.push_back(Imported::childFromLayout(
                        ::aiverse::view::AttachmentLayoutItem{
                            entry.modelSource,entry.socketName,entry.enabled,
                            entry.local,entry.childWorld,entry.id,
                            entry.animated,entry.animationGraph},
                        children.at(entry.modelSource)));
                }
                Imported::Compiled compiled;
                if(!Imported::compile(parent,provider.resources(),
                                      requested,compiled,&error))
                    throw std::runtime_error(error);
                for(size_t j=0;j<attachmentSpecs.size();++j) {
                    const auto& child=attachmentSpecs[j];
                    if(!child.enabled || !child.animated)continue;
                    Model* model=animatedModels.at(child.modelSource);
                    ::aiverse::view::AppearanceAssetRegistry::Request childRequest;
                    childRequest.model=model;
                    childRequest.sources.push_back(child.modelSource);
                    childRequest.animationGraph=Json::string(child.animationGraph);
                    childRequest.graphReader=readFile;
                    childRequest.sourceId=uint64_t(j)+1;
                    const auto graphKey="modelview/graph/"+std::to_string(i)+"/"+child.id;
                    if(!registry.publish(graphKey,childRequest,{}, {},&error))
                        throw std::runtime_error("animated child Graph: "+error);
                    auto graph=registry.find(graphKey);
                    if(!graph || !graph->graph || graph->graph->model()!=model)
                        throw std::runtime_error("animated child Graph/model mismatch");
                    generation->childGraphs[i].emplace(child.id,std::move(graph));
                }
                ::aiverse::view::AttachmentLayout document;
                document.parent=actor.path;
                for(const auto& child:attachmentSpecs)
                    document.attachments.push_back({
                        child.modelSource,child.socketName,child.enabled,
                        child.local,child.childWorld,child.id,
                        child.animated,child.animationGraph});
                const auto resolver=[&](const std::string& source)
                    ->::aiverse::view::AppearanceAssetRegistry::Snapshot {
                    const auto found=children.find(source);
                    return found==children.end()
                        ?::aiverse::view::AppearanceAssetRegistry::Snapshot{}
                        :found->second;
                };
                const Entity parentEntity=generation->world.create();
                if(!generation->scenes->activate(
                        {1,"actor-"+std::to_string(i)},parentEntity,
                        ::aiverse::view::AttachmentLayoutCodec::write(document),
                        parent,provider.resources(),resolver,&error))
                    throw std::runtime_error("animated scene activate: "+error);
                staged.push_back(std::move(compiled));
                parents.push_back(std::move(parent));
            }
            for(size_t i=0;i<actors.size();++i) {
                actors[i].compiledAttachments=std::move(staged[i]);
                actors[i].publishedAppearance=std::move(parents[i]);
                actors[i].attachmentsReady=true;
                actors[i].lastAttachmentError.clear();
            }
            outputGeneration=std::move(generation);
            return true;
        }catch(const std::exception& e) {
            error=e.what();
            return false;
        }
    }

    bool rebuildAttachments() {
        if(!dev || !attachmentPreview || gallery)return false;
        // Reimport/rebuild is one generation transaction; this also
        // retains all old graph players and Model owners until retirement.
        if(reimportModelFiles()) {
            log.push_back("attachment layout compiled for "+
                          std::to_string(inst.size())+" source Models");
            return true;
        }
        log.push_back("attachment update rejected; last good retained: "+nativeReloadError);
        return false;
    }

    // A provider generation cannot be freed until its LAST queued draw
    // is GPU-complete. Both old Animators and every published appearance
    // snapshot stay with the old provider while completion is uncertain.
    void deferNativeScene(std::unique_ptr<RetiredNativeScene> scene) noexcept {
        if(!scene)return;
        try {retiredNativeScenes.push_back(std::move(scene));}
        catch(...) {
            // No proof of completed GPU work: leaking this provider is safer
            // than its destructor releasing potentially GPU-live buffers.
            (void)scene.release();
        }
    }

    // The owner thread calls this only at a frame boundary, never while a
    // Renderer3D draw uses Prepared ModelReadLease or an Animator Model*.
    // The cap is advisory; a failed GPU idle proof keeps old resources.
    bool collectRetiredNativeScenes(bool force=false) noexcept {
        if(!dev || retiredNativeScenes.empty())return true;
        if(!force && retiredNativeScenes.size()<kRetiredNativeLimit)
            return false;
        try {
            if(!dev->waitForResourceIdle()) {
                nativeReloadError="old Model providers retained: GPU idle unavailable";
                return false;
            }
            for(auto& retired:retiredNativeScenes) {
                if(!retired)continue;
                try {
                    retired->generation.reset();
                    retired->instances.clear();
                    retired->appearances.reset();
                    if(retired->provider)
                        retired->provider->shutdown(*dev);
                    retired.reset();
                }catch(...) {
                    // A provider with failed shutdown still owns GPU models:
                    // never permit its destructor to destroy those objects.
                    (void)retired.release();
                }
            }
            retiredNativeScenes.clear();
            return true;
        }catch(...) {
            nativeReloadError="GPU retirement failed; old providers kept";
            return false;
        }
    }

    // Replace the ENTIRE ModelView source generation, never a Model* alone:
    // the parent Animator, its named clip phase, the child source snapshots
    // and compiled sockets are all rebound against new CPU Model objects.
    bool reimportModelFiles() {
        if(!dev || !attachmentPreview || gallery || !nativeModels)
            return false;
        auto candidate=std::make_unique<RetiredNativeScene>();
        candidate->provider=std::make_unique<ModelAssetProvider>(readFile);
        candidate->appearances=
            std::make_unique<::aiverse::view::AppearanceAssetRegistry>();
        std::string error;
        try {
            if(nativeModels->resources().activeModelReaders()!=0)
                throw std::logic_error(
                    "source reload requested while old Model readers are active");
            candidate->instances.reserve(inst.size());
            for(const auto& old:inst) {
                if(!old.model)
                    throw std::runtime_error(
                        "cannot reload an instance without a valid old Model");
                Model* replacement=candidate->provider->load(*dev,old.path);
                if(!replacement)
                    throw std::runtime_error(
                        "on-disk Model reload failed: "+old.path);
                Instance next;
                next.model=replacement;
                next.path=old.path;
                next.fix=old.fix; // keep user-authored world placement
                next.variant=variantName.empty()
                    ?old.variant:replacement->findVariant(variantName);
                const int oldClip=old.clip;
                if(oldClip>=0 && size_t(oldClip)<old.model->clips.size()) {
                    const auto& previous=old.model->clips[size_t(oldClip)];
                    next.clip=replacement->findClip(previous.name);
                } else next.clip=-1;
                next.anim.setModel(replacement);
                if(next.clip>=0) {
                    Animator::Play playing;
                    playing.fade=0.f;
                    playing.restart=true;
                    next.anim.play(0,next.clip,playing);
                    const auto oldClipIndex=old.clip;
                    if(oldClipIndex>=0 &&
                       size_t(oldClipIndex)<old.model->clips.size()) {
                        const float oldDuration=
                            old.model->clips[size_t(oldClipIndex)].duration;
                        const float newDuration=
                            replacement->clips[size_t(next.clip)].duration;
                        if(std::isfinite(oldDuration) && oldDuration>0.f &&
                           std::isfinite(newDuration) && newDuration>0.f) {
                            const float phase=std::clamp(
                                old.anim.time(0)/oldDuration,0.f,0.999999f);
                            (void)next.anim.seek(0,phase*newDuration);
                        }
                    }
                }
                next.anim.evaluate(next.pose);
                worldMatrices(*replacement,next.pose,next.world);
                candidate->instances.push_back(std::move(next));
            }
            if(!buildAttachmentLayout(*candidate->provider,
                                      *candidate->appearances,
                                      candidate->instances,
                                      candidate->generation,error))
                throw std::runtime_error("compiled attachment reload: "+error);
            // Old CPU Graph players remain alive during the transfer.
            // Only portable named-state/clip clocks cross the generation.
            // Stable actor/child identity and complete node hierarchy must
            // match; incompatible imports start from graph defaults.
            size_t transferred=0,declined=0;
            if(attachmentGeneration && candidate->generation) {
                const size_t count=std::min(
                    attachmentGeneration->childGraphs.size(),
                    candidate->generation->childGraphs.size());
                for(size_t i=0;i<count;++i) {
                    const auto& incoming=candidate->generation->childGraphs[i];
                    for(const auto& [childId,snapshot]:incoming) {
                        const auto& previous=attachmentGeneration->childGraphs[i];
                        if(previous.find(childId)==previous.end() ||
                           !snapshot || !snapshot->model)continue;
                        const ::aiverse::view::AnimatedSceneAttachmentSources::Key
                            key{1,"actor-"+std::to_string(i),childId};
                        const auto playback=
                            attachmentGeneration->animations.capturePlayback(key);
                        if(!playback) {++declined;continue;}
                        if(candidate->generation->animations.queuePlayback(
                                key,*playback,*snapshot->model))
                            ++transferred;
                        else ++declined;
                    }
                }
            }
            // Preserve ONLY explicitly host-authored, name+type compatible
            // inputs. Never copy old transient AnimationGraphParameters or
            // implicit graph defaults over a changed authored asset.
            if(attachmentGeneration && candidate->generation) {
                for(const auto& [id,overrides]:
                    attachmentGeneration->parameterOverrides) {
                    const auto actor=id.first;
                    if(actor>=candidate->generation->childGraphs.size())continue;
                    const auto& nextChildren=candidate->generation->childGraphs[actor];
                    const auto child=nextChildren.find(id.second);
                    if(child==nextChildren.end() || !child->second ||
                       !child->second->graph)continue;
                    const auto& defaults=child->second->graph->defaults();
                    render::AnimationParameters accepted;
                    for(const auto& [name,value]:overrides.values)
                        if(defaults.values.count(name))
                            accepted.values.emplace(name,value);
                    for(const auto& [name,value]:overrides.vectors)
                        if(defaults.vectors.count(name))
                            accepted.vectors.emplace(name,value);
                    if(!accepted.values.empty() || !accepted.vectors.empty())
                        candidate->generation->parameterOverrides.emplace(
                            id,std::move(accepted));
                }
            }
            // Allocate the old-session owner BEFORE the atomic move/commit.
            auto retired=std::make_unique<RetiredNativeScene>();
            retired->provider=std::move(nativeModels);
            retired->appearances=std::move(attachmentsRegistry);
            retired->instances=std::move(inst);
            retired->generation=std::move(attachmentGeneration);
            nativeModels=std::move(candidate->provider);
            attachmentsRegistry=std::move(candidate->appearances);
            inst=std::move(candidate->instances);
            attachmentGeneration=std::move(candidate->generation);
            deferNativeScene(std::move(retired));
            nativeReloadError.clear();
            log.push_back("reimported "+
                          std::to_string(inst.size())+
                          " ModelView sources and socket attachments from disk; "+
                          std::to_string(transferred)+" child graph clocks queued, "+
                          std::to_string(declined)+" declined");
            return true;
        }catch(const std::exception& e) {
            nativeReloadError=e.what();
            log.push_back("on-disk reimport rejected; last-good retained: "+
                          nativeReloadError);
            // Partially GPU-uploaded scratch Models remain owned until the
            // same fenced retirement boundary as any old visible source.
            deferNativeScene(std::move(candidate));
            return false;
        }
    }

    // Poll on-disk file revisions only when explicitly opted in. An edit
    // schedules ONE complete staged import at the next owner frame boundary,
    // never from a file-watcher worker or Renderer3D submission callback.
    void pollModelSourceChanges(double seconds) {
        if(!attachmentPreview || !watchModelFiles || gallery)return;
        watchElapsed+=std::max(0.,seconds);
        if(watchElapsed<1.)return;
        watchElapsed=0.;
        std::set<std::string> observed;
        for(const auto& actor:inst)observed.insert(actor.path);
        for(const auto& child:attachmentSpecs) {
            if(!child.enabled)continue;
            observed.insert(child.modelSource);
            // Independent MotionWeave children are authored through an
            // external AnimationGraph asset. Monitor its file separately:
            // a Graph edit must invalidate the staged native generation
            // even if the child Model bytes have not changed.
            if(child.animated && !child.animationGraph.empty())
                observed.insert(child.animationGraph);
        }
        bool changed=false;
        for(const auto& path:observed) {
            std::error_code ec;
            const auto stamp=std::filesystem::last_write_time(path,ec);
            if(ec)continue; // assets from non-filesystem providers are not watched
            const auto [it,inserted]=sourceStamps.try_emplace(path,stamp);
            if(!inserted && it->second!=stamp) {
                it->second=stamp;
                changed=true;
            }
        }
        for(auto it=sourceStamps.begin();it!=sourceStamps.end();)
            if(!observed.count(it->first))it=sourceStamps.erase(it);
            else ++it;
        if(changed)reimportRequested=true;
    }


    // Studio's existing native document writer owns the file; this is a
    // bounded read-only consumer of exactly the same v1 JSON. Compile every
    // visible instance before committing any one of them.
    bool reloadFacialRig(std::string& error) {
        error.clear();
        if(facialRigFile.empty())return true;
        try {
            std::string bytes;
            if(!readFile(facialRigFile,bytes) ||
               bytes.size()>128u*1024u)
                throw std::runtime_error("facial rig JSON missing or exceeds 128 KiB");
            const Json document=Json::parse(bytes);
            std::vector<modelview::NativeFacialPreview> candidates;
            candidates.reserve(inst.size());
            std::size_t count=0;
            for(const auto& actor:inst) {
                modelview::NativeFacialPreview next;
                if(actor.model) {
                    std::string reason;
                    if(!next.configure(*actor.model,document,
                                       facialScalars,facialVectors,reason))
                        throw std::runtime_error(
                            actor.path+": "+reason);
                    ++count;
                }
                candidates.push_back(std::move(next));
            }
            if(count==0)throw std::runtime_error(
                "facial rig preview has no imported Model");
            for(std::size_t i=0;i<inst.size();++i)
                inst[i].facial=std::move(candidates[i]);
            lastFacialError.clear();
            return true;
        }catch(const std::exception& ex) {
            error=ex.what(); // last-good program and state are unchanged
            return false;
        }
    }
    // Poll only at the device owner frame boundary. A failed Studio save
    // never destroys the valid last-good facial rig currently rendered.
    void pollFacialRig(double seconds) {
        if(facialRigFile.empty() || inst.empty())return;
        facialPollElapsed+=std::max(0.,seconds);
        if(facialPollElapsed<0.5)return;
        facialPollElapsed=0.;
        std::error_code ec;
        const auto stamp=std::filesystem::last_write_time(facialRigFile,ec);
        if(ec) {
            const std::string message="facial rig file is not accessible";
            if(lastFacialError!=message)log.push_back(message);
            lastFacialError=message;
            return;
        }
        if(facialStampValid && stamp==facialStamp)return;
        facialStampValid=true;
        facialStamp=stamp;
        std::string error;
        if(!reloadFacialRig(error) && error!=lastFacialError) {
            lastFacialError=error;
            log.push_back("facial rig hot reload rejected: "+error);
        }
    }

    void play(Instance& in) {
        if (!in.model || in.clip < 0) return;
        Animator::Play p;
        p.fade = 0;
        p.restart = true;
        in.anim.play(0, in.clip, p);
        if (seek >= 0) in.anim.update(seek);
    }

    void frame(gfx::Device& d, double dt) {
        if (fixedDt > 0) dt = fixedDt;
        if (!paused && seek < 0) time += dt;
        // Explicit Device-owner frame boundary. Gallery may draw a portrait
        // and scene within ONE RHI frame; neither draw is a safe retirement
        // hook. Poll model and paint token queues before any GPU pass.
        if(gallery)gallery->collectRetiredGpuResources();
        if(scenePreview) {
            std::string error;
            if(restoreSceneSnapshotRequested) {
                restoreSceneSnapshotRequested=false;
                scenePreview->restoreSnapshot(error);
            }
            if(error.empty())scenePreview->pollAndCommit(error);
            if(!error.empty() && (log.empty() || log.back()!=error))
                log.push_back(error);
        }
        if(nativeModels->resources().pendingRetirements()!=0 ||
           nativeModels->pendingRolledBackImages()!=0)
            nativeModels->collectRetired(d);
        pollModelSourceChanges(dt);
        pollFacialRig(dt);
        // All staged on-disk imports and old-provider cleanup happen on
        // this owner boundary, before d.beginFrame or any model draw.
        if(reimportRequested) {
            reimportRequested=false;
            (void)reimportModelFiles();
        }
        (void)collectRetiredNativeScenes();
        // All UI attachment edits are applied at the next OWNER frame
        // boundary, before any Renderer3D GPU submission and after prior
        // draw-scoped ModelReadLeases have been released.
        if(attachmentsDirty) {
            (void)rebuildAttachments();
            attachmentsDirty=false;
        }
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        gui.newFrame(dt);
        gfx::TextureHandle portrait = gallery ? gallery->portrait(time) : gfx::TextureHandle{}; // before the scene pass
        r3d.beginScene(cam, d.framebufferWidth(), d.framebufferHeight());
        // All graphics-visible borrowed models must stay pinned until
        // Renderer3D::render, not just until View::submitOne returns.
        attachmentView.beginPreparedFrame();
        animatedChildView.beginPreparedFrame();
        std::vector<Imported::Prepared> attachmentPackets;
        attachmentPackets.reserve(inst.size());
        std::vector<::aiverse::view::AnimatedSceneAttachmentSources::Prepared>
            childPackets;
        // Parent Animators write their own poses before child graph updates.
        for(auto& actor:inst) {
            if(!actor.model)continue;
            if(!paused && seek<0)actor.anim.update(float(dt));
            actor.anim.evaluate(actor.pose); // ONE native Animator
            if(actor.facial.ready()) {
                std::string error;
                const float facialDt=(paused || seek>=0)
                    ?0.f:float(std::max(0.,dt));
                if(!actor.facial.apply(*actor.model,actor.pose,
                                       facialDt,error) &&
                   error!=lastFacialError) {
                    lastFacialError=error;
                    log.push_back(actor.path+": facial preview: "+error);
                }
            }
            worldMatrices(*actor.model,actor.pose,actor.world);
        }
        bool childReady=false;
        if(attachmentGeneration) {
            std::vector<::aiverse::view::AnimatedSceneAttachmentSources::Input> inputs;
            for(size_t i=0;i<attachmentGeneration->childGraphs.size();++i)
                for(const auto& [id,published]:attachmentGeneration->childGraphs[i]) {
                    auto parameters=published->parameters;
                    const auto it=attachmentGeneration->parameterOverrides.find({i,id});
                    if(it!=attachmentGeneration->parameterOverrides.end()) {
                        for(const auto& [name,value]:it->second.values)
                            parameters.values[name]=value;
                        for(const auto& [name,value]:it->second.vectors)
                            parameters.vectors[name]=value;
                    }
                    inputs.push_back({
                        {1,"actor-"+std::to_string(i),id},
                        published,std::move(parameters)});
                }
            childPackets.reserve(inputs.size());
            ::aiverse::view::AnimatedSceneAttachmentSources::Statistics stats;
            std::string error;
            childReady=attachmentGeneration->animations.sync(
                *attachmentGeneration->scenes,nativeModels->resources(),
                inputs,(paused || seek>=0)?0.f:float(std::max(0.,dt)),
                stats,&error);
            if(!childReady && error!=nativeReloadError) {
                nativeReloadError=error;
                log.push_back("child Graph sync: "+error);
            }
        }
        if (gallery) { gallery->update(time); gallery->draw(r3d, time); }
        else r3d.drawMesh(ground, mat4{}, {1, 1, 1, 1}, kDrawNoCastShadow, 0.05f);
        for (auto& in : inst) {
            if (!in.model) continue;
            // Pre-evaluated before the independent child graph sync.
            ModelDraw md;
            md.model = in.model;
            md.world = in.fix;
            md.nodeWorld = &in.world;
            md.pose = &in.pose;
            md.team = team;
            md.variant = variant >= 0 ? variant : in.variant;
            BakedSample bs;
            if (baked && in.model->baked.tex.valid()) { // skin from the baked clip texture, as crowds do
                const auto b = in.anim.baseSample();
                bs = {b.clipA, b.clipB, b.timeA, b.timeB, b.w, b.loopA, b.loopB};
                md.baked = &bs;
            }
            // Existing host Animator is the only writer: generic View
            // consumes its precomputed pose and composes imported children.
            // Retain owner-thread ModelReadLeases through Renderer3D::render.
            bool presented=false;
            if(attachmentPreview && in.attachmentsReady) {
                Imported::Prepared prepared;
                std::string error;
                if(Imported::prepare(
                        in.compiledAttachments,nativeModels->resources(),
                        in.fix,nullptr,prepared,
                        Imported::AnimationOwner::HostPrecomputed,&error)) {
                    auto& draw=prepared.object.draw;
                    draw.pose=md.pose;
                    draw.nodeWorld=md.nodeWorld;
                    draw.baked=md.baked;
                    draw.variant=md.variant;
                    draw.team=md.team;
                    // Stable per-viewer-instance visual draw identity; the
                    // parent asset's registry key identifies a RESOURCE,
                    // not every visible instance using that resource.
                    prepared.object.id=uint64_t(&in-inst.data())+1;
                    attachmentView.submitOne(r3d,prepared.object);
                    if(childReady && attachmentGeneration) {
                        const size_t actorIndex=size_t(&in-inst.data());
                        for(const auto& [childId,published]:
                            attachmentGeneration->childGraphs[actorIndex]) {
                            (void)published;
                            ::aiverse::view::AnimatedSceneAttachmentSources::Prepared child;
                            std::string childError;
                            if(attachmentGeneration->animations.prepare(
                                    *attachmentGeneration->scenes,
                                    nativeModels->resources(),
                                    {1,"actor-"+std::to_string(actorIndex),childId},
                                    prepared.object.draw,
                                    std::span<const mat4>(in.world.data(),in.world.size()),
                                    child,&childError)) {
                                animatedChildView.submitOne(r3d,child.object);
                                childPackets.push_back(std::move(child));
                            } else if(childError!=nativeReloadError) {
                                nativeReloadError=childError;
                                log.push_back("child Graph draw: "+childError);
                            }
                        }
                    }
                    attachmentPackets.push_back(std::move(prepared));
                    presented=true;
                    in.lastAttachmentError.clear();
                } else if(error!=in.lastAttachmentError) {
                    in.lastAttachmentError=error;
                    log.push_back(in.path+": attachment packet: "+error);
                }
            }
            if(!presented)
                ::aiverse::View::submitModel(r3d,md);
        }
        if(scenePreview) {
            std::string error;
            scenePreview->submit(r3d,(paused || seek>=0)?0.f:float(dt),error);
            if(!error.empty() && (log.empty() || log.back()!=error))
                log.push_back(error);
        }
        gfx::PassDesc pass;
        pass.clearColor[0] = 0.55f; pass.clearColor[1] = 0.66f; pass.clearColor[2] = 0.78f;
        r3d.render(pass);
        if(scenePreview)scenePreview->finishRender();
        // All submitted GPU input has now been synchronously encoded;
        // ModelResourceCache retains ownership for actual GPU completion.
        childPackets.clear();
        attachmentPackets.clear();
        ImGui::SetNextWindowPos(ImVec2(8, 8));
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("info", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        static const char* q[] = {"low", "medium", "high"};
        ImGui::Text("materials: %s   draw calls: %u", q[int(r3d.materialQuality())], r3d.drawCalls());
        for (auto& in : inst)
            if (in.model) {
                ImGui::Text("%s  %s", in.path.c_str(), in.clip >= 0 ? in.model->clips[size_t(in.clip)].name.c_str() : "(bind pose)");
                auto mi = mergeInfo.find(in.path);
                if (mi != mergeInfo.end()) ImGui::Text("  %s", mi->second.c_str());
            }
        if(!facialRigFile.empty()) {
            ImGui::Text("Facial Control Rig: %s",facialRigFile.c_str());
            ImGui::TextUnformatted("Saved Studio JSON | Native Animator + FacialControlRig");
            if(!lastFacialError.empty())
                ImGui::TextWrapped("Facial: %s",lastFacialError.c_str());
        }
        if(scenePreview) {
            ImGui::Separator();
            ImGui::Text("MotionWeave native scenes: %zu; ready draws: %zu",
                        scenePreview->sceneCount(),scenePreview->drawnParents());
            ImGui::Text("authoritative Entities: %zu; GPU pack leases: %zu",
                        scenePreview->authoritativeEntities(),
                        scenePreview->sharedProviderRefs());
            if(ImGui::Button("Add scene")) {
                std::string error;
                if(!scenePreview->request(
                        "scenes/motionweave-area",scene::LoadMode::Additive,
                        false,error))log.push_back(error);
            }
            ImGui::SameLine();
            if(ImGui::Button("Replace scene")) {
                std::string error;
                if(!scenePreview->request(
                        "scenes/motionweave-area",scene::LoadMode::Replace,
                        false,error))log.push_back(error);
            }
            if(ImGui::Button("Snapshot"))scenePreview->saveSnapshot();
            ImGui::SameLine();
            if(ImGui::Button("Restore") && scenePreview->hasSnapshot())
                restoreSceneSnapshotRequested=true; // next safe owner barrier
            if(!scenePreview->lastError().empty())
                ImGui::TextWrapped("Scene: %s",scenePreview->lastError().c_str());
        }
        if (portrait.valid()) {
            const bool flip = d.info().renderTargetFlipY;
            ImGui::Image(ImTextureRef(ImTextureID(portrait.id)), ImVec2(128, 128), ImVec2(0, flip ? 0.f : 1.f), ImVec2(1, flip ? 1.f : 0.f));
        }
        if (gallery) for (auto& ev : gallery->recentEvents()) ImGui::TextColored(ImVec4(1, 0.5f, 0.85f, 1), "event: %s", ev.c_str());
        if (!r3d.modelError().empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", r3d.modelError().c_str());
        ImGui::End();
        if(attachmentPreview) {
            ImGui::Begin("Attachments");
            ImGui::TextUnformatted("Real imported child Models on named sockets");
            ImGui::TextUnformatted("Changes apply next frame; models remain provider-owned");
            for(size_t i=0;i<attachmentSpecs.size();++i) {
                ImGui::PushID(int(i));
                auto& spec=attachmentSpecs[i];
                if(ImGui::Checkbox("##enabled",&spec.enabled))
                    attachmentsDirty=true;
                ImGui::SameLine();
                ImGui::Text("%s @ %s",spec.modelSource.c_str(),spec.socketName.c_str());
                char authoredId[129]{};
                std::snprintf(authoredId,sizeof(authoredId),"%s",spec.id.c_str());
                if(ImGui::InputText("Prefab child ID",authoredId,
                                    sizeof(authoredId))) {
                    spec.id=authoredId;
                    attachmentsDirty=true;
                }
                if(ImGui::Checkbox("Independent animation",&spec.animated))
                    attachmentsDirty=true;
                char graphPath[512]{};
                std::snprintf(graphPath,sizeof(graphPath),"%s",
                              spec.animationGraph.c_str());
                if(spec.animated &&
                   ImGui::InputText("AnimationGraph file",graphPath,
                                    sizeof(graphPath))) {
                    spec.animationGraph=graphPath;
                    attachmentsDirty=true;
                }
                float offset[]={spec.local.m[12],spec.local.m[13],spec.local.m[14]};
                if(ImGui::DragFloat3("Socket offset",offset,0.01f)) {
                    spec.local.m[12]=offset[0];
                    spec.local.m[13]=offset[1];
                    spec.local.m[14]=offset[2];
                    attachmentsDirty=true;
                }
                ImGui::SameLine();
                if(ImGui::SmallButton("Detach")) {
                    attachmentSpecs.erase(attachmentSpecs.begin()+i);
                    attachmentsDirty=true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            // Live, host-owned graph overrides; edit parameters without
            // republishing immutable Models/Graphs or mutating worker state.
            if(attachmentGeneration) {
                for(size_t actor=0;actor<attachmentGeneration->childGraphs.size();++actor) {
                    for(const auto& [childId,published]:
                        attachmentGeneration->childGraphs[actor]) {
                        if(!published || !published->graph)continue;
                        ImGui::PushID(int(actor));
                        ImGui::PushID(childId.c_str());
                        if(ImGui::TreeNode("Animated child parameters")) {
                            ImGui::Text("Actor %zu / %s",actor,childId.c_str());
                            const auto& defaults=published->graph->defaults();
                            auto& override=attachmentGeneration->parameterOverrides[
                                {actor,childId}];
                            for(const auto& [name,value]:defaults.values) {
                                float edited=value;
                                const auto found=override.values.find(name);
                                if(found!=override.values.end())edited=found->second;
                                if(ImGui::DragFloat(name.c_str(),&edited,0.01f))
                                    override.values[name]=edited;
                            }
                            for(const auto& [name,value]:defaults.vectors) {
                                float edited[3]{value.x,value.y,value.z};
                                const auto found=override.vectors.find(name);
                                if(found!=override.vectors.end()) {
                                    edited[0]=found->second.x;
                                    edited[1]=found->second.y;
                                    edited[2]=found->second.z;
                                }
                                if(ImGui::DragFloat3(name.c_str(),edited,0.01f))
                                    override.vectors[name]={edited[0],edited[1],edited[2]};
                            }
                            if(ImGui::SmallButton("Reset graph overrides"))
                                override={};
                            const auto status=attachmentGeneration->animations.restoreResult(
                                {1,"actor-"+std::to_string(actor),childId});
                            ImGui::Text("Playback restore: %llu accepted / %llu rejected",
                                static_cast<unsigned long long>(status.restored),
                                static_cast<unsigned long long>(status.rejected));
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                        ImGui::PopID();
                    }
                }
            }
            ImGui::InputText("Child model file",newAttachmentModel,
                             sizeof(newAttachmentModel));
            ImGui::InputText("Socket name",newAttachmentSocket,
                             sizeof(newAttachmentSocket));
            if(ImGui::Button("Attach") && newAttachmentModel[0] &&
               newAttachmentSocket[0]) {
                attachmentSpecs.push_back(
                    {newAttachmentModel,newAttachmentSocket,true});
                newAttachmentModel[0]=newAttachmentSocket[0]='\0';
                attachmentsDirty=true;
            }
            ImGui::SameLine();
            if(ImGui::Button("Rebuild"))attachmentsDirty=true;
            ImGui::SameLine();
            if(ImGui::Button("Reload model files"))reimportRequested=true;
            ImGui::Checkbox("Watch source files",&watchModelFiles);
            if(ImGui::Button("Save"))saveAttachmentConfig();
            ImGui::Text("Retired provider generations: %zu",
                        retiredNativeScenes.size());
            if(!attachmentConfigPath.empty())
                ImGui::TextWrapped("Config: %s",attachmentConfigPath.c_str());
            if(!nativeReloadError.empty())
                ImGui::TextWrapped("Reload: %s",nativeReloadError.c_str());
            if(!log.empty())
                ImGui::TextWrapped("%s",log.back().c_str());
            ImGui::End();
        }
        gui.render();
        d.endPass();
        d.endFrame();
    }

    void event(const platform::Event& e) {
        if (gui.handleEvent(e)) return;
        using platform::EventType;
        if (e.type == EventType::MouseDown) { dragging = true; lastX = e.x; lastY = e.y; }
        else if (e.type == EventType::MouseUp) dragging = false;
        else if (e.type == EventType::MouseMove && dragging) {
            cam.yaw -= (e.x - lastX) * 0.01f;
            cam.pitch = std::clamp(cam.pitch + (e.y - lastY) * 0.01f, 0.05f, 1.5f);
            lastX = e.x; lastY = e.y;
        } else if (e.type == EventType::MouseWheel) cam.distance = std::clamp(cam.distance * (e.wheelY > 0 ? 0.9f : 1.1f), 1.0f, 200.0f);
        else if (e.type == EventType::KeyDown) {
            using platform::Key;
            if (e.key == Key::Space) for (auto& in : inst) if (in.model && !in.model->clips.empty()) { in.clip = (in.clip + 1) % int(in.model->clips.size()); play(in); }
            if (e.key == Key::Q) r3d.setMaterialQuality(MaterialQuality((int(r3d.materialQuality()) + 1) % 3));
            if (e.key == Key::V) variant = variant + 1 >= 4 ? -1 : variant + 1;
            if (e.key == Key::P) paused = !paused;
            if (e.key == Key::T) {
                static const float3 teams[] = {{0.85f, 0.05f, 0.04f}, {0.04f, 0.2f, 0.9f}, {0.06f, 0.64f, 0.09f}, {0.94f, 0.64f, 0.02f}};
                teamIdx = (teamIdx + 1) % 4; team = teams[teamIdx];
            }
        }
    }

    void shutdown() {
        if(!dev)return;
        // Both Prisma fence completion and MotionWeave model/resource idle
        // must be proven before native models or graphics owners are destroyed.
        if (!dev->gpuHealthy() || !dev->waitForSubmittedGpuWork() ||
            !dev->waitForResourceIdle()) {
            std::fprintf(stderr,
                "aiverse_modelview: GPU shutdown barrier failed; GPU owners quarantined\n");
            // Viewer is static. A mere early return would allow its
            // unique_ptr members to destroy GPU-live Models on process
            // teardown. Explicitly orphan owners when idle cannot be proven.
            for(auto& [key,model]:models) {
                (void)key;
                (void)model.release();
            }
            // Preserve graph Worlds and registries with quarantined CPU
            // Models rather than release borrowed graph/pose owners first.
            (void)attachmentGeneration.release();
            (void)attachmentsRegistry.release();
            (void)nativeModels.release();
            (void)scenePreview.release();
            (void)sceneLifetime.release();
            for(auto& scene:retiredNativeScenes)(void)scene.release();
            retiredNativeScenes.clear();
            return;
        }
        for (auto& [p, m] : models) if (m) releaseModel(*dev, *m);
        // Old Animators and published snapshots must be destroyed BEFORE
        // their owning ModelProvider releases Model CPU/GPU storage.
        attachmentGeneration.reset();
        inst.clear();
        attachmentsRegistry.reset();
        if(scenePreview) {
            std::string error;
            if(!scenePreview->shutdown(error)) {
                std::fprintf(stderr,"aiverse_modelview: %s\n",error.c_str());
                (void)scenePreview.release();
                (void)sceneLifetime.release();
            }else {
                scenePreview.reset();
                sceneLifetime.reset();
            }
        }
        if(nativeModels) {
            try {
                nativeModels->shutdown(*dev);
                nativeModels.reset();
            }catch(...) {
                // Preserve partial uploads rather than invoking an unsafe
                // destructor when provider shutdown reports GPU failure.
                (void)nativeModels.release();
                std::fprintf(stderr,
                    "aiverse_modelview: provider shutdown failed; GPU owner retained\n");
            }
        }
        (void)collectRetiredNativeScenes(true);
        // If even full-idle shutdown could not complete, retain old GPU
        // owners rather than letting unique_ptr destruct GPU-visible Models.
        for(auto& retired:retiredNativeScenes)(void)retired.release();
        retiredNativeScenes.clear();
        if (gallery) gallery->shutdown(*dev);
        dev->destroyBuffer(ground.vb); dev->destroyBuffer(ground.ib);
        gui.shutdown();
        if(lodReport){const auto& p=r3d.profile();std::printf("LOD_REPORT nodes=%u transitions=%u mainTriangles=%u shadowTriangles=%u\n",p.lodNodes,p.lodTransitionNodes,p.triangles[1],p.triangles[0]);}
        r3d.shutdown();
    }
};

Viewer* g_viewer = nullptr;

float3 parse3(const std::string& s) {
    float3 v;
    std::sscanf(s.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
    return {v.x * v.x, v.y * v.y, v.z * v.z}; // given like player colours (~sRGB)
}

} // namespace

int aiverseMain(int argc, char** argv) {
    static Viewer v;
    g_viewer = &v;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--clip") v.clipAll = next();
        else if (a == "--game") v.gameDir = next();
        else if (a == "--scene-preview") v.sceneRoot = next();
        else if (a == "--scene-start") v.sceneStart = next();
        else if (a == "--facial-rig") {
            v.facialRigFile=next();
            if(v.facialRigFile.empty()) {
                std::fprintf(stderr,"--facial-rig needs project/facial-rigs/name.json\n");
                return 2;
            }
        }
        else if (a == "--face") {
            const std::string assignment=next();
            const auto pos=assignment.find('=');
            if(pos==std::string::npos || pos==0 ||
               pos+1>=assignment.size()) {
                std::fprintf(stderr,"--face needs parameter=weight (0..1)\n");
                return 2;
            }
            try {
                const std::string valueText=assignment.substr(pos+1);
                size_t read=0;
                const float value=std::stof(valueText,&read);
                if(read!=valueText.size() || !std::isfinite(value) ||
                   value<0.f || value>1.f)
                    throw std::invalid_argument("invalid facial weight");
                v.facialScalars[assignment.substr(0,pos)]=value;
            }catch(const std::exception&) {
                std::fprintf(stderr,"invalid --face weight (0..1)\n");
                return 2;
            }
        }
        else if (a == "--face-vector") {
            const std::string assignment=next();
            const auto pos=assignment.find('=');
            render::float3 value{};
            char extra=0;
            if(pos==std::string::npos || pos==0 ||
               std::sscanf(assignment.c_str()+pos+1,"%f,%f,%f%c",
                    &value.x,&value.y,&value.z,&extra)!=3 ||
               !std::isfinite(value.x) || !std::isfinite(value.y) ||
               !std::isfinite(value.z)) {
                std::fprintf(stderr,"--face-vector needs parameter=x,y,z\n");
                return 2;
            }
            v.facialVectors[assignment.substr(0,pos)]=value;
        }
        else if (a == "--types") {
            std::stringstream ss(next());
            for (std::string t; std::getline(ss, t, ',');) v.types.push_back(t);
        }
        else if (a == "--start") v.start = std::stof(next());
        else if (a == "--lod-report") v.lodReport = true;
        else if (a == "--no-lod") v.noLod = true;
        else if (a == "--lod-fade") v.lodFadeWidth = std::stof(next());
        else if (a == "--shadows") {
            auto q=next();v.shadowQuality=q=="off"?ShadowQuality::Off:q=="low"?ShadowQuality::Low:q=="medium"?ShadowQuality::Medium:ShadowQuality::High;
        }
        else if (a == "--baked") v.baked = true;
        else if (a == "--fixed-dt") v.fixedDt = std::stof(next());
        else if (a == "--time") v.seek = std::stof(next());
        else if (a == "--variant") v.variantName = next();
        else if (a == "--team") v.team = parse3(next());
        else if (a == "--height") v.height = std::stof(next());
        else if (a == "--spacing") v.spacing = std::stof(next());
        else if (a == "--distance") v.distance = std::stof(next());
        else if (a == "--native") v.native = true;
        else if (a == "--no-merge") v.merge = false;
        else if (a == "--provider") v.providerMode = true;
        else if (a == "--watch-model-files")v.watchModelFiles=true;
        else if (a == "--attachments-file") {
            v.attachmentConfigPath=next();
            if(v.attachmentConfigPath.empty()) {
                std::fprintf(stderr,"--attachments-file requires a JSON path\n");
                return 2;
            }
        }
        else if (a == "--attach") {
            auto authored=next();
            const auto at=authored.rfind('@');
            if(at==std::string::npos || at==0 || at+1>=authored.size()) {
                std::fprintf(stderr,
                    "--attach expects imported-child.glb@NamedSocket\n");
                return 2;
            }
            v.attachmentSpecs.push_back({
                authored.substr(0,at),authored.substr(at+1),true});
        }
        else if (a == "--yaw") v.yawDeg = std::stof(next());
        else if (a == "--pitch") v.pitchDeg = std::stof(next());
        else if (a == "--quality") {
            std::string q = next();
            v.quality = q == "low" ? MaterialQuality::Low : q == "medium" ? MaterialQuality::Medium : MaterialQuality::High;
            v.qualitySet = true;
        } else {
            size_t at = a.find('@');
            v.files.push_back({a.substr(0, at), at == std::string::npos ? std::string() : a.substr(at + 1)});
        }
    }
    if(v.facialRigFile.empty() &&
       (!v.facialScalars.empty() || !v.facialVectors.empty())) {
        std::fprintf(stderr,"--face/--face-vector require --facial-rig\n");
        return 2;
    }
    if(!v.facialRigFile.empty() && v.baked) {
        std::fprintf(stderr,"facial preview requires CPU pose, not --baked\n");
        return 2;
    }
    // A serialized generic scene/prefab attachment layout can be opened
    // directly without separately spelling its parent Model on the CLI.
    // init() will read the complete document once after the Device exists.
    if(v.files.empty() && v.gameDir.empty() &&
       !v.attachmentConfigPath.empty()) {
        std::string bytes;
        if(!Viewer::readFile(v.attachmentConfigPath,bytes)) {
            std::fprintf(stderr,
                "attachment layout file unavailable: %s\n",
                v.attachmentConfigPath.c_str());
            return 2;
        }
        try {
            ::aiverse::view::AttachmentLayout doc;
            ::aiverse::view::AttachmentLayoutCodec::read(
                Json::parse(bytes),doc);
            if(!doc.parent.empty())
                v.files.push_back({doc.parent,{}});
        }catch(const std::exception& ex) {
            std::fprintf(stderr,"invalid attachment layout: %s\n",ex.what());
            return 2;
        }
    }
    if (v.files.empty() && v.gameDir.empty() && v.sceneRoot.empty()) {
        std::printf("usage: aiverse_modelview model.glb [model2.gltf@Clip ...] [--clip C] [--time T] [--variant V] [--quality low|medium|high] [--no-merge] [--provider]\n"
                    "       aiverse_modelview parent.glb@Walk --attach child.glb@Hand.R [--attachments-file file.json]\n"
                    "       aiverse_modelview --game templates/<id> [--types a,b] [--start T] [--fixed-dt S]\n"
                    "       aiverse_modelview --scene-preview templates/showcase [--scene-start scenes/motionweave-global]\n"
                    "       aiverse_modelview face.glb --facial-rig project/facial-rigs/face.json [--face jawOpen=0.8] [--face-vector lookAt=1,2,3]\n");
        return 1;
    }
    platform::AppConfig cfg;
    cfg.title = "AIVERSE model viewer";
    cfg.width = 1280; cfg.height = 720;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { g_viewer->init(d); };
    cb.frame = [](gfx::Device& d, double dt) { g_viewer->frame(d, dt); };
    cb.event = [](const platform::Event& e) { g_viewer->event(e); };
    cb.shutdown = [] { g_viewer->shutdown(); };
    return platform::run(cfg, cb);
}
