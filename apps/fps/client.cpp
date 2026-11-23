#include <aiverselib/net/Admission.h>
#include "engine/game/GameCore.h"
#include "engine/net/authority/Session.h"
#include "engine/actor/Project.h"
#include "engine/render/CameraBehavior.h"
#include "RoyaleHud.h"
#include "engine/ui/RaceHud.h"
#include "engine/roads/RoadView.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include "engine/platform/Platform.h"
#include "engine/platform/InputFlow.h"
#include "engine/world/ChunkScene.h"
#include "engine/world/WorldScene.h"
#include "engine/world/TerraWeaveHorizonFrameTimingBridge.h"
#include "engine/platform/ContentReader.h"
#include "engine/render/Renderer3D.h"
#include "engine/fx/SpectraFxSceneHost.h"
#include "libraries/include/aiverselib/motionweave/MotionWeave.h"
#include "libraries/include/aiverselib/animation/AnimationCrowdSystems.h"
#include "libraries/include/aiverselib/animation/AnimationBudgetPolicy.h"
#include "libraries/include/aiverselib/animation/AnimationBackendPolicy.h"
#include "libraries/include/aiverselib/animation/AnimationEventDispatch.h"
#include "libraries/include/aiverselib/animation/AnimationAssetLoader.h"
#include "engine/view/ActorAnimationEvents.h"
#include "libraries/include/aiverselib/animation/AnimationPhaseTransfer.h"
#include "engine/render/GeometryScene.h"
#include "engine/view/View.h"
#include "engine/view/AppearanceAuthoringService.h"
#include "engine/view/AppearanceAssetRegistry.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/i18n/Localization.h"
#include "imgui.h"
#include "engine/audio/Audio.h"
#include "engine/platform/EngineDir.h"
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <chrono>
#include <algorithm>
#include <map>
#include <memory>
#include <cstdlib>
#include <cmath>
#include <cstdio>

using namespace aiverse;
namespace ein = aiverse::lib::gameplay::inputflow;
namespace {
render::float3 visual(Vec3 v){return {float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};}
struct App {
    Json rules,map;actor::Config config;
    std::unique_ptr<net::authority::Server> server;
    std::unique_ptr<net::authority::Client> client;
#ifndef __EMSCRIPTEN__
    net::UdpListener udp;net::WsListener ws;
#endif
    struct RotatedBox{Vec3 center{},half{};Fixed yaw{};};
    std::vector<RotatedBox> rotatedBoxes;   // map colliders with "yaw": drawn as oriented boxes (collision holds their triangles)
    world::SpatialWorld geometry;world::ChunkScene chunks;std::unique_ptr<world::WorldScene> streamedScene;std::string projectRoot;
    terraweave::HorizonFrameTimingBridge horizonFrameTiming; // optional, native stream QoS
    platform::ContentReader contentReader;
    render::GeometryScene staticGeometry;
    roads::RoadView roads;bool hasRoads=false;   // map "roadNetwork": the shared spline road view (engine/roads, docs/roads.md)
    render::Renderer3D renderer;render::ImGuiLayer gui;render::Mesh cube;
    fx::SpectraFxSceneHost fxScene;
    // Shared data-driven entity effects: rule per selector set, and the last
    // shot/hit counters per character life to turn them into cues.
    std::unordered_map<std::string,int> fxRules;
    struct FxCounters {uint32_t life=0,shots=0;int32_t health=0;};
    std::unordered_map<uint8_t,FxCounters> fxCounters;
    int fxRule(std::vector<std::string> selectors) {
        std::string key;for(const auto& s:selectors){key+=s;key+='|';}
        const auto it=fxRules.find(key);
        if(it!=fxRules.end())return it->second;
        const int rule=fxScene.entities().rule(selectors);
        fxRules.emplace(key,rule);
        return rule;
    }
    // Presents characters, vehicles and projectiles of the snapshot to the
    // shared SpectraFxEntityEffects; game code declares nothing here.
    void presentEntityEffects(float dt,const actor::CharacterSnapshot& local,world::Cell origin) {
        auto& fxe=fxScene.entities();
        if(!fxe.enabled())return;
        const auto at=[&](Vec3 p){return visual(world::relativeTo(p,origin,32));};
        const auto heading=[](Fixed yaw){const float a=float(yaw.toDouble())*6.28318530718f;return render::float3{std::sin(a),0,std::cos(a)};};
        fxe.beginFrame(dt);
        const auto& snap=client->snapshot();
        for(const auto& snapshotCharacter:snap.characters) {
            const auto& c=snapshotCharacter.slot==local.slot?local:snapshotCharacter;
            const uint64_t id=(uint64_t(1)<<40)|(uint64_t(c.slot)<<32)|c.life;
            const int rule=fxRule({"*","actor",c.slot==local.slot?"player:local":"player:remote",
                                   "team:"+std::to_string(c.team),"weapon:"+std::to_string(c.weaponIndex)});
            fx::SpectraFxEntityEffects::Pose pose{at(c.pos),heading(c.yaw)};
            fxe.entity(id,rule,pose,c.health>0,std::clamp(float(c.health)/100.0f,0.0f,1.0f));
            auto& counters=fxCounters[c.slot];
            if(counters.life==c.life) {
                if(c.shots>counters.shots) {
                    fx::SpectraFxEntityEffects::Pose muzzle=pose;
                    muzzle.position=muzzle.position+render::float3{0,1.5f,0};
                    fxe.cue(id,"fired",muzzle);
                    fxe.cueAt(fxRule({"weapon:"+std::to_string(c.weaponIndex)}),"fired",muzzle);
                }
                if(c.health<counters.health) fxe.cue(id,"hit",pose);   // it was hit
            }
            counters={c.life,c.shots,c.health};
        }
        for(const auto& v:snap.vehicles)
            fxe.entity((uint64_t(2)<<40)|v.id,fxRule({"*","vehicle"}),{at(v.pos),heading(v.yaw)},
                       v.health>0,std::clamp(float(v.health)/500.0f,0.0f,1.0f));
        for(const auto& p:snap.projectiles) {
            const auto velocity=visual(p.velocity);
            fxe.entity((uint64_t(3)<<40)|p.id,
                       fxRule({"*","projectile","projectile:"+std::to_string(p.kind),
                               "weapon:"+std::to_string(p.weapon)}),
                       {at(p.pos),render::length(velocity)>1e-4f?render::normalize(velocity):render::float3{0,0,1}});
        }
        fxe.endFrame();
        static const bool debug=std::getenv("AIVERSE_FX_DEBUG")!=nullptr;
        static int frames=0;
        if(debug && ++frames%60==0) {
            const auto& es=fxe.stats();
            const auto* playback=fxScene.playback();
            const auto backend=playback?playback->backendSnapshot():fx::SpectraFxHybridPlayback::BackendSnapshot{};
            std::fprintf(stderr,"[spectrafx-entities] entities=%zu attached=%zu spawned=%llu died=%llu "
                         "cues=%llu cpuPlays=%llu gpuPlays=%llu missing=%llu gpu=%zu cpuTwin=%zu\n",
                         es.entities,es.liveAttachments,(unsigned long long)es.spawned,
                         (unsigned long long)es.died,(unsigned long long)es.cues,
                         (unsigned long long)es.cpuPlays,(unsigned long long)es.gpuPlays,
                         (unsigned long long)es.missingEffects,backend.dedicatedGpu,backend.dedicatedCpuTwin);
            if(playback) {
                const auto& ps=playback->stats();
                std::fprintf(stderr,"[spectrafx-playback] played=%llu moved=%llu failed=%llu refused=%llu missing=%llu slots=%llu\n",
                    (unsigned long long)ps.played,(unsigned long long)ps.moved,(unsigned long long)ps.failed,
                    (unsigned long long)ps.refused,(unsigned long long)ps.missing,(unsigned long long)playback->usedGpuSlots());
            }
            const auto& cpuStats=fxScene.particles().stats();
            std::fprintf(stderr,"[spectrafx-cpu] particles=%d culled=%d drawn=%d\n",
                         cpuStats.particles,cpuStats.culled,cpuStats.drawn);
            for(const auto& c:snap.characters) {
                const auto p=at(c.pos);
                std::fprintf(stderr,"[spectrafx-pose] slot=%u local=%d health=%d pos=%.2f,%.2f,%.2f\n",
                             unsigned(c.slot),c.slot==local.slot?1:0,int(c.health),p.x,p.y,p.z);
            }
        }
    }
    world::Cell fxOrigin{};
    bool fxOriginValid=false;
    // One explicit generation for this FPS host/device lifetime. Do not
    // retire it until the character, scene and renderer GPU owners stop.
    std::unique_ptr<render::RenderDeviceLifetime> gpuLifetime;
    // Universal engine View receives generic renderer packets from FPS.
    // Server/net/player semantics remain entirely in this host adapter.
    ::aiverse::View genericSceneView;
    void drawSceneMesh(const render::mat4& world,render::float4 color,
                       uint32_t flags=0) {
        ::aiverse::view::MeshObject packet;
        packet.mesh=&cube;
        packet.world=world;
        packet.color=color;
        packet.flags=flags;
        genericSceneView.submitMesh(renderer,packet);
    }
    // Client-only visual ECS: never add float poses to the authoritative FPS Simulation.
    World characterPresentation;
    std::unique_ptr<render::Model> characterModel;
    std::shared_ptr<const render::AnimationGraph> characterGraph;
    ::aiverse::view::AppearanceAssetRegistry characterAppearanceRegistry;
    ::aiverse::view::AppearanceAssetRegistry::Snapshot characterAppearance;
    std::map<uint8_t,Entity> characterEntities;
    std::map<uint8_t,uint32_t> characterLives;
    std::map<uint8_t,actor::view::ReplicatedAnimationActions> characterActions;
    std::map<std::string,std::shared_ptr<const motionweave::MontageDefinition>> characterMontages;
    float characterScale=1.f,characterHeightOffset=0.f;
    bool characterBakedCrowd=false,characterBakeAvailable=false;
    bool characterAnimationStatsEnabled=false;
    motionweave::AnimationBackendPreference backendPreference=
        motionweave::AnimationBackendPreference::Auto;
    motionweave::AnimationBackendOptions backendOptions{};
    std::unique_ptr<motionweave::MotionWeaveScheduler> characterScheduler;
    motionweave::MotionWeaveScheduleStats lastAnimationStats{};
    std::filesystem::path characterGraphFile;
    std::filesystem::file_time_type characterGraphStamp{};
    bool characterGraphHasStamp=false;
    unsigned animationReloadFrame=0;
    i18n::Localization lang;
    ein::InputFlow actions;
    platform::InputFlowAdapter inputAdapter{actions};
    platform::inputflow_config::UserProfiles userProfiles;
    ein::InputFlow projectActions;
    bool settingsOpen=false;
    std::string rebindContext, settingsError;
    std::uint64_t rebindMapping=0;
    ein::RebindPolicy rebindPolicy=ein::RebindPolicy::Swap;
    actor::Input input;
    bool fireHeld=false,fireQueued=false,jumpQueued=false,aimHeld=false,prone=false;uint32_t buySelection=0;
    audio::AudioEngine audio;double stepSound=0,viewTime=0;uint32_t lastReload=0;
    std::map<uint8_t,std::pair<uint32_t,uint32_t>> heardShots;
    uint32_t shownShots=0,shownHits=0,shownLife=0;
    double flash=0,hitFlash=0;
    double accumulated=0;bool listen=false;
    render::CameraLook look;render::CameraChaseSettings chase;
    net::authority::Snapshot previousSnapshot,received;uint32_t lastSnapshotTick=0;double snapshotAge=0;

    // Data-driven semantic mapping: games may replace this context with their own JSON assets.
    // These defaults reproduce the reference FPS controls; the network command layout is unchanged.
    void configureInput() {
        std::string error;
        if(!platform::inputflow_config::installAsset(actions,platform::inputflow_presets::fpsReference(),&error))
            throw std::runtime_error("FPS InputFlow preset: "+error);
    }

    void loadInputConfig() {
        namespace cfg=platform::inputflow_config;
        std::string text,error;
        // A project asset is a complete mapping scheme. Never store authored
        // keys inside the OS user profile.
        if(platform::readContentFile(projectRoot+"/input/actions.json",text)){
            try{
                cfg::Asset asset;
                if(!cfg::readAsset(Json::parse(text),asset,&error) ||
                   !cfg::installAsset(actions,asset,&error))
                    std::fprintf(stderr,"InputFlow asset ignored: %s\n",error.c_str());
            }catch(const std::exception& ex){
                std::fprintf(stderr,"InputFlow asset ignored: %s\n",ex.what());
            }
        }
        projectActions=actions; // reloading player settings must preserve project defaults
        userProfiles.setScope(platform::userDataDir(),projectRoot);
        if(!userProfiles.load(0,actions,&error))
            std::fprintf(stderr,"InputFlow profile ignored: %s\n",error.c_str());
    }
    // One transactional path for both single-player FPS and future couch co-op.
    bool rebindInput(const std::string& context,std::uint64_t id,ein::Control control,
                     ein::RebindPolicy policy) {
        return userProfiles.rebind(0,actions,context,id,control,policy,&settingsError);
    }
    // Presentation-only in-game rebinding. Authored actions.json is never changed by players.
    void drawInputSettings() {
        if(!settingsOpen)return;
        ImGui::SetNextWindowSize({600,570},ImGuiCond_FirstUseEver);
        if(!ImGui::Begin("InputFlow Settings (F10)",&settingsOpen)){
            ImGui::End();return;
        }
        ImGui::TextWrapped("Click Rebind, then press a keyboard key or mouse/gamepad button. "
                           "Overrides are saved per player and project; Escape cancels capture.");
        if(ImGui::Button("Resume (click viewport to capture mouse)")){
            settingsOpen=false;rebindMapping=0;
        }
        ImGui::SameLine();
        if(ImGui::Button("Restore project defaults")){
            if(userProfiles.reset(0,actions,projectActions,&settingsError)){
                settingsError.clear();
                rebindMapping=0;
                platform::syncUserData();
            }
        }
        int policy=int(rebindPolicy);
        if(ImGui::Combo("Binding conflicts",&policy,
                        "Allow duplicate\0Reject duplicate\0Swap keys\0\0"))
            rebindPolicy=ein::RebindPolicy(policy);
        if(!settingsError.empty())ImGui::TextWrapped("Input: %s",settingsError.c_str());
        const auto conflicts=actions.conflicts();
        if(!conflicts.empty() && ImGui::CollapsingHeader("Detected conflicts")) {
            for(const auto& c:conflicts) {
                ImGui::TextWrapped("%s:%s / %s:%s %s",c.higherContext.c_str(),
                    c.higherAction.c_str(),c.lowerContext.c_str(),
                    c.lowerAction.c_str(),c.shadowed?"(shadowed)":"(shared)");
            }
        }
        ImGui::Separator();
        if(ImGui::BeginChild("##bindings")) {
            for(const auto& name:actions.registeredContexts()) {
                const auto* context=actions.context(name);
                if(!context)continue;
                const std::string header=name+(actions.isContextActive(name)?" (active)":" (inactive)");
                if(!ImGui::CollapsingHeader(header.c_str(),ImGuiTreeNodeFlags_DefaultOpen))continue;
                for(const auto& mapping:context->mappings) {
                    ImGui::PushID(name.c_str());
                    ImGui::PushID(int(mapping.id)); // stable mapping id for runtime profile
                    const auto binding=platform::inputflow_config::detail::writeControl(mapping.control).dump(0);
                    ImGui::Text("%s  %s",mapping.action.c_str(),binding.c_str());
                    ImGui::SameLine();
                    if(ImGui::SmallButton("Rebind")){
                        rebindContext=name;rebindMapping=mapping.id;settingsError.clear();
                    }
                    if(rebindMapping==mapping.id&&rebindContext==name) {
                        ImGui::TextDisabled("Waiting for key / mouse / gamepad...");
                    }
                    for(const auto& sibling:context->mappings)
                        if(sibling.id!=mapping.id&&sibling.control==mapping.control)
                            ImGui::TextDisabled("Conflict: another action uses this control.");
                    ImGui::PopID();
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
        if(!settingsOpen)rebindMapping=0;
    }
    bool finishRebind(ein::Control control){
        if(!rebindMapping)return false;
        const bool ok=rebindInput(rebindContext,rebindMapping,control,rebindPolicy);
        rebindMapping=0;rebindContext.clear();
        return ok;
    }
    void init(gfx::Device& device) {
        gpuLifetime=std::make_unique<render::RenderDeviceLifetime>(device);
        configureInput();
        loadInputConfig();
        renderer.setGpuDeviceLease(gpuLifetime->lease());
        // LENS camera blocks (CameraBehavior.h, presets in CameraPresets.h): rules "camera", the map's on top.
        const auto cameraBlock=[&](const char* key){
            Json block=render::resolveCameraPreset(rules["camera"][key]);
            if(!block.isObject())block=Json::object();
            for(const auto& [k,v]:render::resolveCameraPreset(map["camera"][key]).members())block.set(k,v);
            return block;};
        look.settings=render::cameraLookFrom(cameraBlock("firstPerson"));
        chase=render::cameraChaseFrom(cameraBlock("chase"));
        renderer.init(device);renderer.setShadowQuality(render::ShadowQuality::Low);
        if(!fxScene.init(device,
             [this](const std::string& p,std::string& out) {
                 return platform::readContentFile(projectRoot+"/"+p,out);
             },projectRoot+"/")) {
            std::fprintf(stderr,"SpectraFX FPS scene: %s\n",
                         fxScene.lastError().c_str());
        }
        // Data-declared entity effects (rules/map "spectraFx", inline
        // "spectraFx" on rules fps.weapons -> selector "weapon:<index>").
        if(!fxScene.configureEntityEffects(rules,map,
               {{"weapon:",&rules["actor"]["weapons"],"id"}}))
            std::fprintf(stderr,"SpectraFX entity effects: %s\n",
                         fxScene.lastError().c_str());
        if (const auto file=map.getStr("geometryScene"); !file.empty()) {
            auto reader=[&](const std::string& path,std::string& text){return platform::readContentFile(projectRoot+"/"+path,text);};
            std::string text;if(!reader(file,text))throw std::runtime_error("geometryScene: file not found: "+file);
            staticGeometry.load(device,Json::parse(text),reader);
        }
        if(map["roadNetwork"].isObject()) {   // the same spline roads as every other client (rules "roadTypes": cellMeters 1 on a metre map)
            roads.init(device,rules["roadTypes"],[this](const std::string& path,std::string& text){return platform::readContentFile(projectRoot+"/"+path,text);});
            roads.setMap(map);hasRoads=true;
        }
        gui.init(device,"engine/assets/fonts/ui.ttf",17);
        render::MeshBuilder builder;builder.color(render::rgba(255,255,255)).box({0,0.5f,0},{1,1,1});cube=builder.upload(device);
        loadAnimatedCharacter(device);
        lang.load([](const auto& path,auto& out){return platform::readContentFile(path,out);},"en",{"engine/assets/lang"});
        Simulation sim({1,config.tickRate,0},std::make_unique<game::GameCore>());sim.setup(rules,map);
        geometry=actor::host(sim).collision();
        rotatedBoxes.clear();
        for(const auto& b:map["colliders"].items())if(b.has("yaw"))rotatedBoxes.push_back({world::worldVector(b["center"],100000_fx),world::worldVector(b["half"],100000_fx),b["yaw"].fx()});
        if(map["world"].getBool("async"))streamedScene=std::make_unique<world::WorldScene>(map["world"],contentReader);
        else chunks.configure(map["world"]);
        audio::AudioConfig ac;
        if(const char* mode=std::getenv("AIVERSE_AUDIO")){ac.openDevice=std::string(mode)!="off";ac.nullBackend=std::string(mode)=="null";}
        if(const char* wav=std::getenv("AIVERSE_AUDIO_CAPTURE"))ac.capturePath=wav;
        audio.init(ac);std::string bank,error;
        if(platform::readContentFile(projectRoot+"/audio.json",bank)||platform::readContentFile("engine/assets/fps_audio.json",bank))if(!audio.loadBank(Json::parse(bank),[](const auto& file,auto& out){return platform::readContentFile(file,out);},&error))std::fprintf(stderr,"FPS audio: %s\n",error.c_str());
    }

    // Source-owned resource publication; MotionWeave and the presentation
    // ECS remain the sole pose/clock authority for every remote player.
    bool publishCharacterAppearance(const render::Model& model,
                                    const Json& fallbackGraph,
                                    std::string& error) {
        ::aiverse::view::AppearanceAssetRegistry::Request request;
        request.model=&model;
        request.sourceId=1; // registry asset key; NOT an individual actor ID
        request.appearance=rules["actor"]["visual"];
        request.materialLibrary=rules["actor"]["visual"]["materialLibrary"];
        request.animationGraph=characterGraphFile.empty()
            ?fallbackGraph:Json::string(characterGraphFile.string());
        if(!model.path.empty())request.sources.push_back(model.path);
        if(!characterGraphFile.empty()) {
            request.sources.push_back(characterGraphFile.string());
            request.graphReader=[](const std::string& path,std::string& out) {
                std::ifstream stream(path,std::ios::binary);
                if(!stream)return false;
                // Bounded read: a failed oversized edit must keep last-good.
                std::string bytes(256u*1024u+1u,'\0');
                stream.read(bytes.data(),std::streamsize(bytes.size()));
                const auto count=stream.gcount();
                if(stream.bad() || count>256*1024)return false;
                bytes.resize(size_t(count));
                out=std::move(bytes);
                return true;
            };
        }
        return characterAppearanceRegistry.publish(
            "fpsCharacter",request,{},
            [](const std::string& warning) {
                std::fprintf(stderr,"FPS appearance: %s\n",warning.c_str());
            },&error);
    }

    // Opt-in model for FPS remote players. Existing box fallback remains when
    // fps.visual.model is unset or the asset fails to load.
    void loadAnimatedCharacter(gfx::Device& device) {
        if(!gpuLifetime || !gpuLifetime->lease().validFor(device))
            throw std::logic_error("FPS animated character GPU device generation expired");
        const auto& view=rules["actor"]["visual"];
        const std::string file=view.getStr("model");
        if (file.empty()) return;
        // Own partially uploaded assets until the graph and optional bake
        // have compiled successfully; otherwise release their GPU buffers.
        std::unique_ptr<render::Model> pendingModel;
        try {
            std::vector<std::string> warnings;
            auto reader=[](const std::string& path,std::string& text) {
                return platform::readContentFile(path,text);
            };
            pendingModel=render::loadModel(projectRoot+"/"+file,reader,&warnings);
            auto& model=pendingModel;
            if (!model || !render::uploadModel(device,*model))
                throw std::runtime_error("model upload failed");
            Json def=view["animationGraph"];
            if (!def.isObject()) {
                // Generate a useful idle/walk graph from imported clip names.
                // Projects can supply their own full graph with parameters.
                Json nodes=Json::array();
                Json bind=Json::object();
                bind.set("id",Json::string("bind"));
                bind.set("kind",Json::string("bind"));
                nodes.push(bind);
                std::string output="bind";
                const int idle=model->findClip("Idle")>=0?model->findClip("Idle"):model->findClip("Stand");
                const int move=model->findClip("Run")>=0?model->findClip("Run"):model->findClip("Walk");
                auto addClip=[&](const char* id,int index){
                    Json node=Json::object();
                    node.set("id",Json::string(id));
                    node.set("kind",Json::string("clip"));
                    node.set("clip",Json::string(model->clips[size_t(index)].name));
                    nodes.push(node);
                };
                if (idle>=0) {addClip("idle",idle);output="idle";}
                if (move>=0) {
                    addClip("move",move);
                    if (output=="bind")output="move";
                    else {
                        Json blend=Json::object();
                        blend.set("id",Json::string("locomotion"));
                        blend.set("kind",Json::string("blend"));
                        blend.set("a",Json::string("idle"));
                        blend.set("b",Json::string("move"));
                        blend.set("parameter",Json::string("moving"));
                        nodes.push(blend);
                        output="locomotion";
                    }
                }
                def=Json::object();
                def.set("nodes",nodes);
                def.set("output",Json::string(output));
                def.set("parameters",Json::parse(R"({"moving":0,"speed":0})"));
            }
            // Optional project-local hot-reloadable graph asset; file revisions
            // never affect authoritative FPS simulation or networking.
            const auto graphFile=view.getStr("graphFile");
            if(!graphFile.empty()) {
                const auto local=std::filesystem::path(graphFile).lexically_normal();
                if(local.is_absolute() || local.empty() ||
                   *local.begin()==".." ||
                   graphFile.find("..")!=std::string::npos)
                    throw std::invalid_argument("animation graphFile escapes project");
                characterGraphFile=std::filesystem::path(projectRoot)/local;
            }
            // Both the initial file and later edits use the same immutable
            // appearance transaction (materials, node overrides, graph).
            std::string appearanceError;
            if(!publishCharacterAppearance(*model,def,appearanceError))
                throw std::runtime_error("FPS appearance: "+appearanceError);
            characterAppearance=characterAppearanceRegistry.find("fpsCharacter");
            if(!characterAppearance || !characterAppearance->graph)
                throw std::runtime_error("FPS appearance has no animation graph");
            const auto compiled=characterAppearance->graph;
            if(!characterGraphFile.empty()) {
                std::error_code ec;
                characterGraphStamp=std::filesystem::last_write_time(characterGraphFile,ec);
                characterGraphHasStamp=!ec;
            }
            // Authoring uses Workspace animations/montage/*.json. Opt-in
            // cosmetic montages never drive hit, reload or damage authority.
            const auto& montagePaths=view["montages"];
            if(!montagePaths.isNull() && !montagePaths.isObject())
                throw std::invalid_argument("actor.visual.montages must be an object");
            for(const char* action:{"shot","reload","death"}) {
                const std::string file=montagePaths.getStr(action);
                if(file.empty())continue;
                try {
                    const auto relative=std::filesystem::path(file).lexically_normal();
                    if(relative.empty() || relative.is_absolute() ||
                       *relative.begin()==".." || file.find("..")!=std::string::npos)
                        throw std::invalid_argument("montage path escapes project");
                    std::string text;
                    if(!platform::readContentFile(
                        (std::filesystem::path(projectRoot)/relative).string(),text))
                        throw std::runtime_error("montage file unavailable");
                    characterMontages[action]=motionweave::compileMontageAsset(
                        *model,Json::parse(text));
                }catch(const std::exception& e){
                    std::fprintf(stderr,"FPS MotionWeave montage %s: %s\n",action,e.what());
                }
            }
            // Complex imported controllers always use palette/graph evaluation.
            // Simple clip crowds can use the existing instanced GPU baked skinning.
            // MotionWeave chooses CPU Graph for complex rig/IK/controllers and
            // GPU-baked sampling for sufficiently populated simple crowds.
            // "bakedCrowd":true remains a backwards-compatible forced GPU hint.
            const auto preference=view.getStr("animationMode",
                view.getBool("bakedCrowd",false)?"gpu":"auto");
            backendPreference=motionweave::parseAnimationBackend(preference);
            backendOptions.gpuEnterActors=size_t(std::clamp<int64_t>(
                view.getInt("gpuEnterActors",24),1,1000));
            backendOptions.gpuExitActors=size_t(std::clamp<int64_t>(
                view.getInt("gpuExitActors",12),0,999));
            characterAnimationStatsEnabled=view.getBool("animationStats",false);
            const bool custom=view["animationGraph"].isObject() ||
                !characterGraphFile.empty();
            const bool hasSimpleClips=model->findClip("Idle")>=0 ||
                model->findClip("Stand")>=0 || model->findClip("Walk")>=0 ||
                model->findClip("Run")>=0;
            backendOptions.requiresCustomGraph=custom || !characterMontages.empty();
            characterBakeAvailable=
                backendPreference!=motionweave::AnimationBackendPreference::CpuGraph &&
                !backendOptions.requiresCustomGraph && hasSimpleClips && model->skinned() &&
                render::bakeAnimations(device,*model);
            backendOptions.gpuAvailable=characterBakeAvailable;
            characterBakedCrowd=characterBakeAvailable &&
                backendPreference==motionweave::AnimationBackendPreference::GpuBaked;
            const double scale=view["scale"].toDouble(1.0);
            if (std::isfinite(scale) && scale>0.001 && scale<100)
                characterScale=float(scale);
            const double offset=view["heightOffset"].toDouble(0.0);
            if (std::isfinite(offset) && std::abs(offset)<100)
                characterHeightOffset=float(offset);
            characterGraph=compiled;
            characterModel=std::move(model);
            for (const auto& warning:warnings)
                std::fprintf(stderr,"FPS animation import: %s\n",warning.c_str());
        } catch (const std::exception& e) {
            std::fprintf(stderr,"FPS animation disabled (box fallback): %s\n",e.what());
            if(pendingModel)render::releaseModel(device,*pendingModel);
            if(characterModel)render::releaseModel(device,*characterModel);
            characterGraph.reset();
            characterAppearance.reset();
            characterAppearanceRegistry.clear();
            characterModel.reset();
            characterBakeAvailable=false;
            characterBakedCrowd=false;
            characterGraphFile.clear();
            characterMontages.clear();
        }
    }
    bool showRemoteDeathMontage(const actor::CharacterSnapshot& remote) const {
        if(characterBakedCrowd || remote.health>0 || !characterModel)return false;
        const auto death=characterMontages.find("death");
        const auto actor=characterEntities.find(remote.slot);
        if(death==characterMontages.end() || actor==characterEntities.end())return false;
        const auto* slots=characterPresentation.tryGetConst<
            motionweave::AnimationMontageSlotsComponent>(actor->second);
        if(!slots)return false;
        for(const auto& player:slots->players)
            if(player.active() && player.slot()==death->second->slot)return true;
        return false;
    }
    void updateCharacterAnimations(double dt,const actor::CharacterSnapshot& local) {
        if (!characterModel || !characterGraph) return;
        const auto& actors=client->snapshot().characters;
        size_t remoteCount=0;
        for(const auto& remote:actors)
            if(remote.slot!=local.slot && remote.health>0)++remoteCount;
        backendOptions.visibleActors=remoteCount;
        const auto oldBackend=characterBakedCrowd?
            motionweave::AnimationBackend::GpuBaked:
            motionweave::AnimationBackend::CpuGraph;
        const auto nextBackend=motionweave::selectAnimationBackend(
            backendPreference,oldBackend,backendOptions);
        std::map<uint8_t,float> phaseHandoff;
        if((nextBackend==motionweave::AnimationBackend::GpuBaked)!=
            characterBakedCrowd) {
            // Transfer clip phase before recreating presentation components.
            // More complex controller/montage graphs never enter GPU mode.
            for(const auto& remote:actors) {
                const auto found=characterEntities.find(remote.slot);
                if(found==characterEntities.end())continue;
                std::optional<float> phase;
                if(characterBakedCrowd) {
                    const auto* crowd=characterPresentation.tryGetConst<
                        motionweave::AnimationCrowdComponent>(found->second);
                    if(crowd)
                        phase=motionweave::captureBakedClipPhase(
                            crowd->animator,*characterModel);
                } else {
                    const auto* player=characterPresentation.tryGetConst<
                        motionweave::AnimationPlayerComponent>(found->second);
                    if(player && player->instance)
                        phase=motionweave::captureGraphClipPhase(
                            *player->instance,*characterModel,
                            render::length(visual(remote.velocity))>.1f);
                }
                if(phase)phaseHandoff.emplace(remote.slot,*phase);
            }
            characterBakedCrowd=
                nextBackend==motionweave::AnimationBackend::GpuBaked;
            characterPresentation=World{};
            characterEntities.clear();
            characterLives.clear();
            // Keep characterActions: confirmed snapshot event history must
            // not reset merely because a rendering backend changed.
        }
        if(!characterGraphFile.empty() && ++animationReloadFrame%30u==0u) {
            std::error_code ec;
            const auto stamp=std::filesystem::last_write_time(characterGraphFile,ec);
            if(!ec && (!characterGraphHasStamp || stamp!=characterGraphStamp)) {
                characterGraphStamp=stamp;
                characterGraphHasStamp=true;
                characterAppearanceRegistry.invalidateSources(
                    {characterGraphFile.string()});
                std::string error;
                if(publishCharacterAppearance(*characterModel,Json{},error)) {
                    characterAppearance=characterAppearanceRegistry.find("fpsCharacter");
                    const auto compiled=characterAppearance->graph;
                    if(compiled!=characterGraph) {
                        // MotionWeave owns graph rebind semantics for ANY
                        // presentation source, independent of host genre.
                        // This does not create a second player or evaluator.
                        for(const auto& [slot,entity]:characterEntities) {
                            (void)slot;
                            (void)motionweave::rebindAnimationPresentationGraph(
                                characterPresentation,entity,
                                *characterModel,compiled);
                        }
                        characterGraph=compiled;
                    }
                } else
                    std::fprintf(stderr,
                        "FPS appearance hot reload (last good retained): %s\n",
                        error.c_str());
            }
        }
        std::set<uint8_t> presentSlots;
        for (const auto& remote:actors) {
            if (remote.slot==local.slot) continue;
            presentSlots.insert(remote.slot);
            Entity e;
            const auto found=characterEntities.find(remote.slot);
            if (found==characterEntities.end()) {
                e=characterPresentation.create();
                characterEntities.emplace(remote.slot,e);
                if(characterBakedCrowd) {
                    auto& crowd=characterPresentation.add<motionweave::AnimationCrowdComponent>(e);
                    crowd.model=characterModel.get();
                    if(const auto phase=phaseHandoff.find(remote.slot);
                       phase!=phaseHandoff.end())
                        crowd.initialNormalizedPhase=phase->second;
                    crowd.idle=characterModel->findClip("Idle");
                    if(crowd.idle<0)crowd.idle=characterModel->findClip("Stand");
                    crowd.walk=characterModel->findClip("Walk");
                    crowd.run=characterModel->findClip("Run");
                    if(crowd.walk<0)crowd.walk=crowd.run;
                    crowd.death=characterModel->findClip("Death");
                    characterPresentation.add<motionweave::AnimationCrowdSampleComponent>(e);
                } else {
                    auto& asset=characterPresentation.add<motionweave::AnimationAssetComponent>(e);
                    asset.model=characterModel.get();
                    asset.graph=characterGraph;
                    characterPresentation.add<motionweave::AnimationPlayerComponent>(e);
                    characterPresentation.add<motionweave::AnimationPoseComponent>(e);
                    if(const auto phase=phaseHandoff.find(remote.slot);
                       phase!=phaseHandoff.end())
                        characterPresentation.add<motionweave::AnimationPhaseSeedComponent>(
                            e,motionweave::AnimationPhaseSeedComponent{phase->second});
                    characterPresentation.add<motionweave::AnimationInputsComponent>(e);
                    characterPresentation.add<motionweave::AnimationGraphNotifiesComponent>(e);
                    if(!characterMontages.empty())
                        characterPresentation.add<motionweave::AnimationMontageSlotsComponent>(e);
                    characterPresentation.add<motionweave::AnimationBudgetComponent>(e);
                    characterPresentation.add<motionweave::AnimationSignificanceComponent>(e);
                }
            } else e=found->second;
            const auto knownLife=characterLives.find(remote.slot);
            const bool respawn=knownLife==characterLives.end() ||
                knownLife->second!=remote.life;
            characterLives[remote.slot]=remote.life;
            // These are confirmed server snapshot counters. Repeated and
            // out-of-order snapshots cannot restart cosmetic montages.
            auto triggers=characterActions[remote.slot].observe({
                client->snapshot().tick,remote.life,remote.shots,
                remote.reloadUntil,remote.health});
            const auto speed=render::length(visual(remote.velocity));
            if(characterBakedCrowd) {
                auto& crowd=characterPresentation.get<motionweave::AnimationCrowdComponent>(e);
                if(respawn){crowd.animator.setModel(nullptr);crowd.playing=-2;}
                crowd.speed=speed;
                crowd.dead=remote.health<=0;
            } else {
                auto& player=characterPresentation.get<motionweave::AnimationPlayerComponent>(e);
                if(respawn)player.restart=true;
                if(auto* slots=characterPresentation.tryGet<motionweave::AnimationMontageSlotsComponent>(e)) {
                    if(respawn)slots->players.clear();
                    for(const auto& [enabled,action] : std::initializer_list<std::pair<bool,const char*>>{
                        {triggers.death,"death"},{triggers.reload,"reload"},{triggers.fire,"shot"}}) {
                        if(!enabled)continue;
                        const auto asset=characterMontages.find(action);
                        if(asset!=characterMontages.end())
                            motionweave::playAnimationSlot(*slots,asset->second);
                    }
                }
                auto& inputs=characterPresentation.get<motionweave::AnimationInputsComponent>(e).overrides;
                inputs.values.clear();
                inputs.set("speed",speed);
                inputs.set("moving",speed>.1f?1.f:0.f);
                inputs.set("dead",remote.health<=0?1.f:0.f);
                inputs.set("grounded",remote.grounded?1.f:0.f);
                inputs.set("reloading",remote.reloadUntil?1.f:0.f);
                auto& significance=characterPresentation.get<motionweave::AnimationSignificanceComponent>(e);
                const float distance=render::length(visual(remote.pos-local.pos));
                significance.pixels=720.f*1.8f/std::max(1.f,distance);
                significance.importance=remote.team==local.team?1.25f:1.f;
                significance.visible=remote.health>0 || showRemoteDeathMontage(remote);
            }
        }
        // Drop view entities no longer present in the replicated actor set.
        // Reused actor slots then get a fresh visual player and montage clock.
        for(auto it=characterEntities.begin();it!=characterEntities.end();) {
            if(!presentSlots.contains(it->first)) {
                characterPresentation.destroy(it->second);
                characterLives.erase(it->first);
                characterActions.erase(it->first);
                it=characterEntities.erase(it);
            } else ++it;
        }
        if(characterBakedCrowd)
            motionweave::stepAnimationCrowd(characterPresentation,
                float(std::clamp(dt,0.,.25)));
        else {
            motionweave::AnimationBudgetPolicy budget;
            budget.fullRateSlots=32;budget.mediumRateSlots=96;
            motionweave::assignAnimationBudgets(characterPresentation,budget);
            // Persistent workers are created lazily when a sufficiently
            // populated palette/graph scene actually needs CPU animation.
            if(characterEntities.size()>=16 && !characterScheduler)
                characterScheduler=std::make_unique<motionweave::MotionWeaveScheduler>();
            if(characterScheduler)
                lastAnimationStats=motionweave::MotionWeave::update(
                    characterPresentation,float(std::clamp(dt,0.,.25)),
                    *characterScheduler);
            else {
                motionweave::MotionWeave::update(characterPresentation,
                    float(std::clamp(dt,0.,.25)));
                lastAnimationStats={};
            }
            motionweave::dispatchAnimationCues(characterPresentation,
                [&](const motionweave::AnimationCue& cue) {
                    if(cue.name!="step" && cue.name!="footstep" && cue.name!="land")
                        return; // weapon hits/reloads remain server-authoritative
                    for(const auto& [slot,entity]:characterEntities)
                        if(entity==cue.entity) {
                            for(const auto& remote:client->snapshot().characters)
                                if(remote.slot==slot && remote.health>0) {
                                    audio::PlayParams params;params.positional=true;
                                    params.x=float(remote.pos.x.toDouble());
                                    params.y=float(remote.pos.y.toDouble());
                                    params.z=float(remote.pos.z.toDouble());
                                    audio.play("step",params);
                                }
                            break;
                        }
                });
        }
    }
    void event(const platform::Event& e) {
        using E=platform::EventType; using K=platform::Key;
        gui.handleEvent(e);
        if(e.type==E::KeyDown&&e.key==K::F10&&!e.repeat){
            settingsOpen=!settingsOpen;
            rebindMapping=0;
            platform::setPointerLock(false);
            inputAdapter.focusLost();
            fireHeld=fireQueued=jumpQueued=aimHeld=false;input.buttons=0;
            return;
        }
        if(settingsOpen) {
            if(rebindMapping && e.type==E::KeyDown && !e.repeat) {
                if(e.key==K::Escape){rebindMapping=0;rebindContext.clear();}
                else if(e.key!=K::Unknown)
                    finishRebind({ein::Device::Keyboard,int(e.key)});
            }else if(rebindMapping && e.type==E::MouseDown && e.button>=0 && e.button<=4){
                finishRebind({ein::Device::MouseButton,e.button});
            }else if(e.type==E::KeyDown && e.key==K::Escape) {
                settingsOpen=false;rebindMapping=0;rebindContext.clear();
            }
            if(e.type!=E::FocusLost)return;
        }
        if(e.type==E::FocusLost){
            platform::setPointerLock(false);inputAdapter.focusLost();
            input.buttons=0;input.action=actor::Action::None;
            fireHeld=fireQueued=jumpQueued=aimHeld=false;
            return;
        }
        if(e.type==E::KeyDown&&e.key==K::Escape){
            platform::setPointerLock(false);inputAdapter.focusLost();
            input.buttons=0;input.action=actor::Action::None;
            fireHeld=fireQueued=jumpQueued=aimHeld=false;
            return;
        }
        if(e.type==E::MouseDown&&e.button==0&&!platform::pointerLocked())
            platform::setPointerLock(true);
        // Gameplay context is only fed while the viewport owns the pointer lock.
        if(platform::pointerLocked()||e.type==E::KeyUp||e.type==E::MouseUp)
            inputAdapter.onEvent(e);
    }
    void frame(gfx::Device& device,double dt) {
        const auto frameStarted=terraweave::HorizonFrameTimingBridge::Clock::now();
        // Shader variants invalidated by quality/instance changes belong
        // to the host Device and retire only after real GPU completion.
        if(renderer.pendingProgramRetirements()!=0)
            renderer.collectRetiredPrograms();
        if(renderer.pendingTargetRetirements()!=0)
            renderer.collectRetiredTargets();
        if(renderer.hasPendingModelPipelineJobs())
            renderer.pollPendingModelPipelineJobs();
        const auto now=net::nowMs();
#ifndef __EMSCRIPTEN__
        if(listen) {
            auto admit=[&](auto connections){for(auto& conn:connections){uint8_t slot=0;for(size_t i=2;i<=map["players"].size();++i)if(!server->occupied(uint8_t(i))){slot=uint8_t(i);break;}if(slot)server->attach(slot,std::move(conn));else conn->close();}};
            admit(udp.poll(now));admit(ws.poll(now));
        }
#endif
        client->updateStreaming();client->poll(now);if(server)server->poll(now);
        const bool playing=platform::pointerLocked() && !settingsOpen;
        if(settingsOpen) {
            platform::pollGamepads();
            if(rebindMapping){
                for(int pad=0;pad<platform::kMaxGamepads && rebindMapping;++pad){
                    const auto& state=platform::gamepad(pad);
                    if(!state.connected)continue;
                    for(int button=0;button<int(platform::PadButton::Count);++button){
                        if(state.pressed(platform::PadButton(button))){
                            finishRebind({ein::Device::GamepadButton,button,pad});break;
                        }
                    }
                    for(int axis=0;axis<int(platform::PadAxis::Count)&&rebindMapping;++axis)
                        if(std::abs(state.axis(platform::PadAxis(axis)))>0.8f)
                            finishRebind({ein::Device::GamepadAxis,axis,pad});
                }
            }
        }
        if(playing){
            inputAdapter.pollGamepads();
            const auto events=actions.update(float(std::min(dt,0.25)));
            for(const auto& e:events){
                if(e.phase!=ein::Phase::Triggered)continue;
                const auto& name=e.action;
                if(name=="Jump")jumpQueued=true;
                else if(name=="Fire")fireQueued=true;
                else if(name=="Prone")prone=!prone;
                else if(name=="Frag")input.action=actor::Action::Frag;
                else if(name=="Smoke")input.action=actor::Action::Smoke;
                else if(name=="Flash")input.action=actor::Action::Flash;
                else if(name=="Heal")input.action=actor::Action::Heal;
                else if(name=="Interact")input.action=actor::Action::Interact;
                else if(name=="Drop")input.action=actor::Action::Drop;
                else if(name=="Plant")input.action=actor::Action::Plant;
                else if(name=="Vault")input.action=actor::Action::Vault;
                else if(name=="Buy"){
                    input.action=actor::Action::Buy;input.argument=buySelection;
                    input.buttons=(input.buttons&~uint32_t(actor::WeaponMask))|actor::selectWeapon(buySelection);
                }else if(name=="Weapon1"||name=="Weapon2"||name=="Weapon3"||name=="Weapon4"){
                    buySelection=uint32_t(name.back()-'1');
                    input.buttons=(input.buttons&~uint32_t(actor::WeaponMask))|actor::selectWeapon(buySelection);
                }else if(name=="Attach1"||name=="Attach2"||name=="Attach3"){
                    input.action=actor::Action::Attach;input.argument=6+uint32_t(name.back()-'1');
                }
            }
            auto held=[&](const char* name){const auto* state=actions.state(name);return state&&state->active;};
            fireHeld=held("Fire");aimHeld=held("Aim");
            if(const auto* mouse=actions.state("Look");mouse&&mouse->active)
                look.motion(mouse->value.x,mouse->value.y,aimHeld);
            if(const auto* stick=actions.state("LookStick");stick&&stick->active){
                const float pxPerSecond=1400.0f;
                const float scaled=float(std::min(dt,0.25))*pxPerSecond;
                look.motion(stick->value.x*scaled,stick->value.y*scaled,aimHeld);
            }
        }else{
            inputAdapter.focusLost();
            input.buttons=0;input.action=actor::Action::None;
            fireHeld=fireQueued=jumpQueued=aimHeld=false;
        }
        auto held=[&](const char* name){const auto* state=actions.state(name);return state&&state->active;};
        accumulated+=std::min(dt,0.25);int budget=8;
        while(accumulated>=1.0/config.tickRate&&budget--){
            const auto* move=actions.state("Move");
            input.forward=move?int32_t(std::clamp(move->value.y,-1.0f,1.0f)*1000):0;
            input.right=move?int32_t(std::clamp(move->value.x,-1.0f,1.0f)*1000):0;
            input.buttons&=uint32_t(actor::WeaponMask);
            if(jumpQueued)input.buttons|=actor::Jump;
            if(aimHeld)input.buttons|=actor::Aim;
            if(prone)input.buttons|=actor::Prone;
            if(fireHeld||fireQueued)input.buttons|=actor::Fire;
            if(held("Crouch"))input.buttons|=actor::Crouch;
            if(held("Sprint"))input.buttons|=actor::Sprint;
            if(held("Reload"))input.buttons|=actor::Reload;
            if(config.melee.enabled){
                if(held("Melee"))input.buttons|=actor::Melee;
                if(held("Block"))input.buttons|=actor::Block;
                if(held("LockOn"))input.buttons|=actor::LockOn;
            }
            // Testing (screenshots): AIVERSE_FPS_WATCH=1 turns the view to the nearest living remote character.
            static const bool watchRemote=std::getenv("AIVERSE_FPS_WATCH")!=nullptr;
            if(watchRemote) {
                const auto& self=client->predicted();float best=1e9f;
                for(const auto& other:client->snapshot().characters) {
                    if(other.slot==self.slot||other.health<=0)continue;
                    const auto d=visual(other.pos-self.pos);
                    const bool hidden=geometry.cast(self.pos+Vec3{{},1_fx,{}},other.pos-self.pos).hit;
                    const float dist=render::length(d)+(hidden?1000.0f:0.0f);   // prefer characters in sight
                    if(dist>0.5f&&dist<best){best=dist;look.yaw=std::atan2(d.x,d.z)/6.28318530718f;if(look.yaw<0)look.yaw+=1;look.pitch=-0.01f;}
                }
            }
            input.yaw=Fixed::fromRaw(int64_t(look.yaw*65536));input.pitch=Fixed::fromRaw(int64_t(look.pitch*65536));
            if(client->submit(input)){
                fireQueued=jumpQueued=false;
                input.action=actor::Action::None;
                input.buttons&=~uint32_t(actor::Jump|actor::WeaponMask);
            }
            if(server){server->poll(now);if(!server->step()){accumulated=0;break;}client->poll(now);}
            accumulated-=1.0/config.tickRate;
        }
        // Streamed WorldScene GPU ownership is retired only at this
        // real device-frame boundary, before any new scene submissions.
        if(streamedScene && streamedScene->pendingGpuRetirements()!=0)
            (void)streamedScene->collectRetired(device);
        if(staticGeometry.pendingGpuRetirements()!=0)
            (void)staticGeometry.collectRetired(device);
        device.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());
        // GPU query belongs to this FPS frame's command buffer. Unsupported
        // backends transparently use measured CPU callback time only.
        horizonFrameTiming.begin(device,streamedScene.get());
        gui.newFrame(dt);
        auto actor=client->predicted();viewTime+=dt;
        updateCharacterAnimations(dt,actor);
        if(actor.reloadUntil&&!lastReload)audio.play("reload");
        lastReload=actor.reloadUntil;
        stepSound+=dt;
        if(actor.grounded&&actor.health>0&&!actor.prone&&!actor.vehicle&&actor.velocity.xz().lengthSq()>1_fx&&stepSound>0.38){audio.play("step");stepSound=0;}
        for(const auto& remote:client->snapshot().characters){
            auto it=heardShots.find(remote.slot);
            if(it!=heardShots.end()&&it->second.first==remote.life&&remote.shots>it->second.second){
                audio::PlayParams params;params.positional=remote.slot!=actor.slot;params.x=float(remote.pos.x.toDouble());params.y=float(remote.pos.y.toDouble());params.z=float(remote.pos.z.toDouble());
                params.volume=(remote.attachments[remote.weaponIndex]&2)?0.22f:0.7f;
                if(params.positional&&geometry.cast(actor.pos+Vec3{{},1_fx,{}},remote.pos-actor.pos).hit)params.volume*=0.25f;
                audio.play("shot"+std::to_string(remote.weaponIndex),params);
            }
            heardShots[remote.slot]={remote.life,remote.shots};
        }
        flash=std::max(0.0,flash-dt);hitFlash=std::max(0.0,hitFlash-dt);
        // Local predicted shot presentation only; never publish another
        // network or authoritative event. Four FX requests per frame max.
        const uint32_t fxLocalShots=shownLife==actor.life &&
            actor.shots>shownShots?std::min(uint32_t(actor.shots-shownShots),4u):0u;
        if(shownLife==actor.life) {
            if(actor.shots>shownShots)flash=0.09;
            if(actor.hits>shownHits)hitFlash=0.18;
        }
        shownLife=actor.life;shownShots=actor.shots;shownHits=actor.hits;
        const auto& weapon=config.weapons[std::min(size_t(actor.weaponIndex),config.weapons.size()-1)];
        snapshotAge+=dt;

        if(client->snapshot().tick!=lastSnapshotTick){previousSnapshot=received;received=client->snapshot();lastSnapshotTick=received.tick;snapshotAge=0;}
        float interpolation=float(std::min(1.0,snapshotAge*config.snapshotRate));
        world::Cell origin=geometry.cell(actor.pos);
        bool fxOriginReady=true;
        if(fxOriginValid && origin!=fxOrigin) {
            if(!fxScene.resetScene()) fxOriginReady=false;
        }
        if(fxOriginReady) {
            fxOrigin=origin;
            fxOriginValid=true;
        }
        // If a reset could not safely run outside a GPU encoder, do not
        // enqueue or render VFX in mismatched floating-origin coordinates.
        // The next frame retries the reset before accepting new FX.
        Vec3 eye=actor.pos+Vec3{{},actor::stanceHeight(actor,config.motor)-Fixed::fromRatio(1,10),{}};
        auto localEye=visual(world::relativeTo(eye,origin,32));
        auto cameraLook=look;cameraLook.pitch+=float(actor.recoil.toDouble());
        auto cam=cameraLook.camera(localEye);
        // Race: chase camera behind the driven car, interpolated between the last two snapshots.
        const actor::Vehicle* ride=nullptr;const actor::Vehicle* rideBefore=nullptr;
        if(actor.vehicle&&client->snapshot().race.enabled){
            for(const auto& v:received.vehicles)if(v.id==actor.vehicle)ride=&v;
            for(const auto& v:previousSnapshot.vehicles)if(v.id==actor.vehicle)rideBefore=&v;
        }
        if(ride){
            auto at=visual(world::relativeTo(ride->pos,origin,32));float yaw=float(ride->yaw.toDouble());
            if(rideBefore){auto from=visual(world::relativeTo(rideBefore->pos,origin,32));at=from+(at-from)*interpolation;yaw=render::lerpTurns(float(rideBefore->yaw.toDouble()),yaw,interpolation);}
            cam=render::chaseCamera(at,yaw,chase);localEye=cam.eye();
        }
        if(aimHeld)cam.fovY=(weapon.range>=700_fx||(actor.attachments[actor.weaponIndex]&1))?0.28f:0.65f;
        audio::ListenerPose listener;auto globalEye=visual(eye);listener.pos[0]=globalEye.x;listener.pos[1]=globalEye.y;listener.pos[2]=globalEye.z;auto forward=render::normalize(cam.target-cam.eye());listener.forward[0]=forward.x;listener.forward[1]=forward.y;listener.forward[2]=forward.z;audio.setListener(listener);
        if(fxOriginReady && fxScene.enabled()) {
            // FPS-specific visual cue ID 1: local weapon fired; opt-in only.
            for(uint32_t i=0;i<fxLocalShots;++i) {
                fx::SpectraFxCue cue{};
                cue.id=1;
                cue.action=fx::SpectraFxCue::Action::Play;
                cue.position=localEye+forward*0.8f;
                cue.direction=forward;
                fxScene.enqueueCue(std::move(cue),true,fxScene.cueEpoch());
            }
            presentEntityEffects(float(std::clamp(dt,0.0,0.1)),actor,origin);
        }
        if(streamedScene)streamedScene->update(device,{&actor.pos,1});
        // Prepare GPU particle evolution outside render passes.
        if(fxOriginReady) fxScene.prepareGpu(float(std::clamp(dt,0.0,0.1)),cam);
        genericSceneView.beginScene(renderer,cam,
            device.framebufferWidth(),device.framebufferHeight());
        if(fxOriginReady)
            fxScene.prepareCpu(float(std::clamp(dt,0.0,0.1)),cam,renderer.viewProj(),
                               device.framebufferWidth(),device.framebufferHeight());
        if(streamedScene)streamedScene->draw(renderer,origin,32);
        staticGeometry.draw(renderer);
        if(hasRoads){roads.collectRetired(device);roads.setWorldShift(visual(world::relativeTo(Vec3{},origin,32)));roads.draw(renderer,[](float,float){return 0.f;});}
        chunks.update(actor.pos,2,2,[&](const auto& file,auto& data){return platform::readContentFile(projectRoot+"/"+file,data);});
        for(const auto& [cell,boxes]:chunks.chunks())for(const auto& b:boxes) {
            (void)cell;
            auto base=Vec3{(b.min.x+b.max.x)/2,b.min.y,(b.min.z+b.max.z)/2};
            auto transform=render::mat4::translate(visual(world::relativeTo(base,origin,32)))*render::mat4::scale(visual(b.max-b.min));
            drawSceneMesh(transform,{0.4f,0.46f,0.5f,1});
        }
        Vec3 extent{600_fx,600_fx,600_fx};
        for(const auto& r:rotatedBoxes)if((r.center-actor.pos).xz().lengthSq()<extent.x*extent.x) {
            auto base=r.center-Vec3{{},r.half.y,{}};
            auto transform=render::mat4::translate(visual(world::relativeTo(base,origin,32)))*render::mat4::rotateY(float(r.yaw.toDouble())*6.28318530718f)*render::mat4::scale(visual(r.half*2_fx));
            drawSceneMesh(transform,{0.4f,0.46f,0.5f,1});
        }
        for(auto id:geometry.query({actor.pos-extent,actor.pos+extent})) {
            if(chunks.enabled()&&id!=1&&id<actor::kBuildingColliderBase)continue;   // generated buildings are not in the chunk files
            if(id>=actor::kRotatedColliderBase&&!rotatedBoxes.empty())continue;      // triangles of rotated boxes: drawn above
            const auto& b=geometry.colliders().at(id).bounds;
            // Draw relative to the observer before converting coordinates to floating point.
            auto base=Vec3{(b.min.x+b.max.x)/2,b.min.y,(b.min.z+b.max.z)/2};
            auto transform=render::mat4::translate(visual(world::relativeTo(base,origin,32)))*render::mat4::scale(visual(b.max-b.min));
            drawSceneMesh(transform,id==1?render::float4{0.18f,0.23f,0.22f,1}:render::float4{0.4f,0.46f,0.5f,1});
        }
        // Stage remote character draws in one ordered View stream. Their
        // Poses are authored by characterPresentation above; the generic
        // View must never evaluate those graphs a second time.
        // Maintain prepared-packet LOD state without duplicating ECS clocks.
        genericSceneView.beginPreparedFrame();
        ::aiverse::view::ScenePacketBatch remotePackets;
        remotePackets.reserve(client->snapshot().characters.size());
        for(const auto& remote:client->snapshot().characters)
            if(remote.slot!=actor.slot &&
               (remote.health>0 || showRemoteDeathMontage(remote))) {
            auto position=visual(world::relativeTo(remote.pos,origin,32));
            for(const auto& old:previousSnapshot.characters)if(old.slot==remote.slot&&old.life==remote.life&&old.health>0) {
                auto before=visual(world::relativeTo(old.pos,origin,32));position=before+(position-before)*interpolation;break;
            }
            bool animated=false;
            if(characterModel) {
                const auto found=characterEntities.find(remote.slot);
                if(found!=characterEntities.end()) {
                    render::ModelDraw draw;
                    draw.model=characterModel.get();
                    // Snapshot owns material/node overrides for the entire
                    // ordered submit; characterModel owns the GPU resources.
                    if(characterAppearance) {
                        draw.materials=&characterAppearance->materials;
                        draw.nodeMaterial=&characterAppearance->nodeMaterial;
                        draw.variant=characterAppearance->variant;
                        draw.geometry=characterAppearance->geometry;
                    }
                    draw.persistentId=uint64_t(remote.slot);
                    const float yaw=float(remote.yaw.toDouble()*6.283185307179586);
                    draw.world=render::mat4::translate(position+render::float3{0,characterHeightOffset,0})
                        *render::mat4::rotateY(yaw)
                        *render::mat4::scale({characterScale,characterScale,characterScale});
                    draw.team=remote.team==actor.team?render::float3{.1f,.5f,.9f}:render::float3{.9f,.2f,.1f};
                    const auto submitCharacter=[&](const render::ModelDraw& packetDraw) {
                        ::aiverse::view::Object packet;
                        // The generic published Asset is the source of
                        // material, geometry and Model revision identity.
                        // The native host supplies only the current resolved
                        // pose/bake, world transform and appearance overrides.
                        const bool published=characterAppearance &&
                            characterAppearance->model==characterModel.get() &&
                            characterAppearance->packetWithBindings(
                                packetDraw.world,{},nullptr,packet);
                        if(published) {
                            packet.draw.persistentId=packetDraw.persistentId;
                            packet.draw.team=packetDraw.team;
                            packet.draw.pose=packetDraw.pose;
                            packet.draw.nodeWorld=packetDraw.nodeWorld;
                            packet.draw.baked=packetDraw.baked;
                        } else packet.draw=packetDraw; // old unbound fallback
                        packet.id=remote.slot;
                        packet.generation=remote.life;
                        packet.animationModel=characterModel.get();
                        packet.animationModelGeneration=characterAppearance
                            ?characterAppearance->modelPublicationGeneration:0;
                        // Animation was evaluated ONCE in the host's
                        // presentation World. A published Asset may contain
                        // a Graph, but prepared packets must not create a
                        // competing View-owned evaluator.
                        packet.graph.reset();
                        remotePackets.add(std::move(packet));
                    };
                    if(characterBakedCrowd) {
                        const auto* sample=characterPresentation.tryGetConst<
                            motionweave::AnimationCrowdSampleComponent>(found->second);
                        if(sample && sample->valid) {
                            draw.baked=&sample->sample;
                            submitCharacter(draw);
                            animated=true;
                        }
                    } else {
                        const auto* posed=characterPresentation.tryGetConst<
                            motionweave::AnimationPoseComponent>(found->second);
                        if(posed && posed->valid) {
                            draw.nodeWorld=&posed->matrices;
                            draw.pose=&posed->local;
                            submitCharacter(draw);
                            animated=true;
                        }
                    }
                }
            }
            if(!animated) {
                auto transform=render::mat4::translate(position)*render::mat4::scale({0.6f,float(actor::stanceHeight(remote,config.motor).toDouble()),0.6f});
                ::aiverse::view::MeshObject fallback;
                fallback.mesh=&cube;
                fallback.world=transform;
                fallback.color=remote.team==actor.team
                    ?render::float4{0.1f,0.5f,0.9f,1}
                    :render::float4{0.9f,0.2f,0.1f,1};
                remotePackets.add(std::move(fallback));
            }
        }
        // Precomputed model/mesh packets are intentionally NOT passed to
        // View::update: the client has already advanced MotionWeave once
        // and copying them into another presentation World would double
        // animate or reset montage/phase state.
        genericSceneView.submitPackets(renderer,remotePackets);
        // Generic ordered View packet stream: the host creates ordinary
        // geometry draws without exposing gameplay meaning to the renderer.
        ::aiverse::view::ScenePacketBatch worldPackets;
        worldPackets.reserve(
            client->snapshot().loot.size()+client->snapshot().vehicles.size()+
            client->snapshot().projectiles.size()+
            client->snapshot().royale.crates.size()+1);
        auto box=[&](Vec3 pos,render::float3 size,render::float4 color){
            ::aiverse::view::MeshObject item;
            item.mesh=&cube;
            item.world=render::mat4::translate(
                visual(world::relativeTo(pos,origin,32)))*
                render::mat4::scale(size);
            item.color=color;
            worldPackets.add(item);
        };
        for(const auto& loot:client->snapshot().loot)box(loot.pos,{0.35f,0.35f,0.35f},{0.9f,0.75f,0.15f,1});
        for(const auto& v:client->snapshot().vehicles)if(v.health>0){
            // Oriented car body; in a race each racer's car has its own colour (own car: gold).
            render::float4 color{0.2f,0.5f,0.25f,1};
            if(client->snapshot().race.enabled){static const render::float4 palette[8]={{0.85f,0.2f,0.2f,1},{0.2f,0.45f,0.9f,1},{0.25f,0.75f,0.35f,1},{0.9f,0.55f,0.15f,1},{0.65f,0.3f,0.85f,1},{0.2f,0.75f,0.8f,1},{0.85f,0.85f,0.85f,1},{0.35f,0.35f,0.4f,1}};
                color=v.driver==client->snapshot().localSlot?render::float4{0.95f,0.78f,0.2f,1}:palette[(v.driver+7)%8];}
            ::aiverse::view::MeshObject item;item.mesh=&cube;
            item.world=render::mat4::translate(visual(world::relativeTo(v.pos,origin,32)))*render::mat4::rotateY(float(v.yaw.toDouble())*6.28318530718f)*render::mat4::scale({2,1.4f,4});
            item.color=color;worldPackets.add(item);
        }
        for(const auto& projectile:client->snapshot().projectiles){
            if(projectile.kind==4){auto r=float(config.systems.smokeRadius.toDouble());box(projectile.pos-Vec3{{},config.systems.smokeRadius,{}},{r*2,r*2,r*2},{0.65f,0.68f,0.7f,1});}
            else box(projectile.pos,{0.15f,0.15f,0.15f},projectile.kind?render::float4{0.2f,0.3f,0.15f,1}:render::float4{1,0.8f,0.2f,1});
        }
        if(client->snapshot().match.planted)box(client->snapshot().match.bombPos,{0.4f,0.3f,0.3f},{1,0.2f,0.1f,1});
        genericSceneView.submitPackets(renderer,worldPackets);
        worldPackets.clear();
        // Preserve the original intermediate zone wall ordering. Its host
        // primitive adapter can migrate later without moving this pass.
        actor::view::drawZoneWall(renderer,cube,client->snapshot().royale,actor.pos,origin,visual);
        for(const auto& c:client->snapshot().royale.crates)box(c,{1.2f,1.2f,1.2f},{0.95f,0.6f,0.1f,1});
        genericSceneView.submitPackets(renderer,worldPackets);
        gfx::PassDesc pass;pass.clearColor[0]=0.2f;pass.clearColor[1]=0.28f;pass.clearColor[2]=0.36f;renderer.render(pass);
        if(fxOriginReady) fxScene.render(renderer.lighting,renderer.viewProj(),cam);
        ImGui::SetNextWindowPos({16,16});ImGui::SetNextWindowBgAlpha(0.65f);
        ImGui::Begin("##fps",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs);
        auto text=lang.tr("actor.status",i18n::Args().num("health",actor.health).num("ammo",actor.ammo).num("reserve",actor.reserve).num("tick",client->snapshot().tick));
        if(!client->snapshot().race.enabled)ImGui::TextUnformatted(text.c_str());   // a race shows its own HUD (engine/ui/RaceHud.h), no weapons
        if((streamedScene&&!streamedScene->status().complete())||(server&&server->module().streamingStatus()&&!server->module().streamingStatus()->complete()))ImGui::TextUnformatted(lang.tr("actor.worldLoading").c_str());
        if(!client->snapshot().race.enabled){
        ImGui::TextUnformatted(lang.tr(weapon.nameKey).c_str());
        if(actor.reloadUntil)ImGui::TextUnformatted(lang.tr("actor.reloading").c_str());
        else if(actor.ammo==0)ImGui::TextUnformatted(lang.tr("actor.empty").c_str());
        for(size_t i=0;i<config.weapons.size();++i) {
            auto label=lang.tr("actor.weaponSlot",i18n::Args().num("slot",int(i+1)).str("weapon",lang.tr(config.weapons[i].nameKey)));
            ImGui::TextColored(i==actor.weaponIndex?ImVec4(1,0.8f,0.3f,1):ImVec4(0.6f,0.65f,0.7f,1),"%s",label.c_str());
        }
        }   // !client->snapshot().race.enabled
        ImGui::PushTextWrapPos(580);ImGui::TextUnformatted(lang.tr(!client->ready()?"actor.connecting":client->snapshot().race.enabled?"race.controls":"actor.controls").c_str());ImGui::PopTextWrapPos();
        if(!client->connected())ImGui::TextUnformatted(lang.tr("actor.disconnected").c_str());
        if(client->snapshot().winner)ImGui::TextUnformatted(lang.tr("actor.finished").c_str());
        if(characterModel && characterAnimationStatsEnabled) {
            ImGui::Text("MotionWeave: %s (%zu visible)",
                characterBakedCrowd?"GPU baked":"CPU graph",
                backendOptions.visibleActors);
            if(!characterBakedCrowd) {
                ImGui::Text("Workers %zu | Safe %zu | Callback %zu | Jobs %zu",
                    lastAnimationStats.workerThreads,lastAnimationStats.parallelEntities,
                    lastAnimationStats.serialEntities,lastAnimationStats.batches);
                ImGui::Text("Pose %.3fms | Callback %.3fms | Est. work %zu",
                    lastAnimationStats.workerPhaseMs,lastAnimationStats.serialPhaseMs,
                    lastAnimationStats.estimatedSafeCost);
            }
        }
        if(config.systems.enabled&&!client->snapshot().race.enabled){
            auto stats=lang.tr("actor.systems.status",i18n::Args().num("armor",actor.armor).num("money",actor.money));ImGui::TextUnformatted(stats.c_str());
            if(actor.downed)ImGui::TextUnformatted(lang.tr("actor.downed").c_str());
            if(actor.health<=0)ImGui::TextUnformatted(lang.tr("actor.dead").c_str());
            if(actor.activeAction!=actor::Action::None){auto remaining=std::max(0,int(actor.actionUntil)-int(client->snapshot().tick));ImGui::TextUnformatted(lang.tr("actor.action",i18n::Args().num("seconds",(remaining+int(config.tickRate)-1)/int(config.tickRate))).c_str());}
            ImGui::PushTextWrapPos(580);ImGui::TextUnformatted(lang.tr("actor.systems.controls").c_str());ImGui::PopTextWrapPos();
            if(held("ShowInventory"))for(auto item:actor.bag)ImGui::TextUnformatted(lang.tr("actor.item",i18n::Args().str("name",lang.tr("actor.item."+std::to_string(item.type))).num("count",item.count)).c_str());
            if(config.mode=="bomb")ImGui::TextUnformatted(lang.tr("actor.round",i18n::Args().num("round",client->snapshot().round).num("a",client->snapshot().match.scoreA).num("b",client->snapshot().match.scoreB).num("bomb",client->snapshot().match.planted?std::max(0,int(client->snapshot().match.bombExplodes)-int(client->snapshot().tick))/int(config.tickRate):0)).c_str());
        }
        ImGui::End();
        drawInputSettings();
        auto* draw=ImGui::GetForegroundDrawList();float x=platform::windowWidth()/2,y=platform::windowHeight()/2;
        if(!ride){   // no crosshair and no weapon while racing at the wheel
        draw->AddLine({x-5,y},{x+5,y},IM_COL32_WHITE,1.5f);draw->AddLine({x,y-5},{x,y+5},IM_COL32_WHITE,1.5f);
        if(hitFlash>0) {
            for(float sign:{-1.f,1.f}){draw->AddLine({x+sign*8,y+8},{x+sign*14,y+14},IM_COL32(255,100,60,255),2);draw->AddLine({x+sign*8,y-8},{x+sign*14,y-14},IM_COL32(255,100,60,255),2);}
        }
        // Procedural placeholder viewmodel: distinct silhouettes, no external art dependency.
        float w=float(platform::windowWidth()),h=float(platform::windowHeight());
        float gx=w*(aimHeld?0.5f:0.65f),gy=h*(aimHeld?0.65f:0.79f)+float(flash)*110;
        if(actor.velocity.xz().lengthSq()>1_fx)gy+=std::sin(float(viewTime)*10)*3;
        if(actor.reloadUntil)gy+=45+std::sin(float(viewTime)*8)*20;
        float barrel=weapon.pellets>1?35.f:(weapon.range>=700_fx?13.f:22.f);
        draw->AddQuadFilled({gx-38,h},{gx+70,h},{gx+35,gy+45},{gx-25,gy+45},IM_COL32(44,51,59,255));
        draw->AddRectFilled({gx-barrel,gy},{gx+barrel,gy+105},IM_COL32(80,91,103,255),4);
        draw->AddRectFilled({gx-5,gy-5},{gx+5,gy+10},IM_COL32(160,175,185,255),2);
        if(flash>0){draw->AddTriangleFilled({gx-35,gy-10},{gx+30,gy-5},{gx,gy-70},IM_COL32(255,210,70,230));draw->AddLine({gx,gy-30},{x,y},IM_COL32(255,225,140,150),1.5f);}
        if(aimHeld&&(weapon.range>=700_fx||(actor.attachments[actor.weaponIndex]&1))){
            float radius=h*0.38f;draw->AddRectFilled({0,0},{x-radius,h},IM_COL32_BLACK);draw->AddRectFilled({x+radius,0},{w,h},IM_COL32_BLACK);draw->AddRectFilled({x-radius,0},{x+radius,y-radius},IM_COL32_BLACK);draw->AddRectFilled({x-radius,y+radius},{x+radius,h},IM_COL32_BLACK);draw->AddCircle({x,y},radius,IM_COL32_BLACK,64,12);draw->AddLine({x-radius,y},{x+radius,y},IM_COL32_BLACK);draw->AddLine({x,y-radius},{x,y+radius},IM_COL32_BLACK);
        }
        for(const auto& cloud:client->snapshot().projectiles)if(cloud.kind==4&&(actor.pos-cloud.pos).lengthSq()<config.systems.smokeRadius*config.systems.smokeRadius)draw->AddRectFilled({0,0},{w,h},IM_COL32(165,170,175,250));
        if(actor.blindUntil>client->snapshot().tick){auto remain=actor.blindUntil-client->snapshot().tick;auto alpha=int(std::min(1.0,double(remain)/std::max(1u,config.systems.flashTicks))*255);draw->AddRectFilled({0,0},{w,h},IM_COL32(255,255,255,alpha));}
        }   // !ride
        actor::view::drawRoyaleHud(lang,client->snapshot(),actor,config);
        ui::drawRaceHud(lang,client->snapshot().race,client->snapshot().tick,config.tickRate);
        gui.render();device.endPass();device.endFrame();
        horizonFrameTiming.finish(streamedScene.get(),frameStarted);
    }
    void shutdown(gfx::Device& device) {
        if(!gpuLifetime || !gpuLifetime->lease().validFor(device))
            throw std::logic_error("FPS shutdown GPU device generation expired");
        horizonFrameTiming.detach(device);
        fxScene.shutdown();
        characterScheduler.reset();
        characterEntities.clear();
        characterLives.clear();
        characterActions.clear();
        characterMontages.clear();
        characterGraph.reset();
        characterAppearance.reset();
        characterAppearanceRegistry.clear();
        if(characterModel) {
            render::releaseModel(device,*characterModel);
            characterModel.reset();
        }
        if(streamedScene)streamedScene->shutdown(device);
        server.reset();client.reset();
        staticGeometry.shutdown(device);
        if(hasRoads)roads.shutdown(device);
        platform::setPointerLock(false);
        audio.shutdown();gui.shutdown();renderer.shutdown();
        device.destroyBuffer(cube.vb);
        device.destroyBuffer(cube.ib);
        // gfx::Device is still alive: all known FPS device allocations have
        // been submitted for release before the old generation is retired.
        gpuLifetime->retire();
        gpuLifetime.reset();
    }
};
}
// Actor projects (game.json "module": "actor") in the one game client (apps/client/main.cpp dispatches here).
int actorClientMain(int argc,char** argv) {
    try {
        App app;auto engineRoot=platform::resolveEngineDir(AIVERSE_SOURCE_DIR,argv[0]);
        std::string sessionTokenFile;std::string project=engineRoot+"/templates/fps_training",connect;
        for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--project"&&i+1<argc)project=argv[++i];else if(arg=="--connect"&&i+1<argc)connect=argv[++i];else if(arg=="--session-token-file"&&i+1<argc)sessionTokenFile=argv[++i];else if(arg=="--listen")app.listen=true;else throw std::runtime_error("usage: AiverseGame --project DIR [--connect udp://host:27100 | --listen] [--session-token-file FILE]");}
        project=std::filesystem::absolute(project).string();
        std::filesystem::current_path(engineRoot);
        if(!sessionTokenFile.empty()&&(app.listen||connect.empty()))throw std::runtime_error("session token requires --connect");
        if(app.listen&&!connect.empty())throw std::runtime_error("choose --listen or --connect");
        auto data=actor::readProject([&](const auto& file,auto& text){return platform::readContentFile(project+"/"+file,text);});
        app.projectRoot=project;app.contentReader=platform::makeContentReader(project);app.rules=data.rules;app.map=data.map;app.config=actor::readConfig(app.rules);
        if(connect.empty()) {
            app.server=std::make_unique<net::authority::Server>(app.rules,app.map,1,app.contentReader);
            auto pair=net::makeLoopbackPair();app.server->attach(1,std::move(pair.first));
            app.client=std::make_unique<net::authority::Client>(app.rules,app.map,std::move(pair.second),app.contentReader);
#ifndef __EMSCRIPTEN__
            if(app.listen&&(!app.udp.open(27100)||!app.ws.open(27101)))throw std::runtime_error("cannot open listen server");
#endif
        } else {auto conn=net::connectTo(connect);if(!conn)throw std::runtime_error("invalid server address");if(!sessionTokenFile.empty()){std::ifstream tokenFile(sessionTokenFile);std::string token;std::getline(tokenFile,token);if(!tokenFile||!aiverse::lib::session::detail::tokenValid(token))throw std::runtime_error("invalid session token file");conn=std::make_unique<aiverse::lib::session::ClientAdmission>(std::move(conn),std::move(token));}app.client=std::make_unique<net::authority::Client>(app.rules,app.map,std::move(conn),app.contentReader);}
        platform::AppConfig cfg;cfg.title="AIVERSE FPS";cfg.width=1280;cfg.height=720;
        gfx::Device* device=nullptr;platform::AppCallbacks cb;
        cb.init=[&](auto& d){device=&d;app.init(d);};cb.frame=[&](auto& d,double dt){app.frame(d,dt);};cb.event=[&](const auto& e){app.event(e);};cb.shutdown=[&]{app.shutdown(*device);};
        return platform::run(cfg,cb);
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
