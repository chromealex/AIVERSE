#include "engine/ui/RaceHud.h"
#include "engine/platform/WindowSettings.h"
#include "engine/render/PrismaVolumeData.h"
#include "engine/render/PrismaDlss.h"
#include "engine/game/InitialVars.h"
#include "engine/data/JsonCompose.h"
// Generic RTS game client. Everything game-specific comes from the project's data (game.json,
// rules.json, maps): the same executable plays a Warcraft-like, a Red-Alert-like or a
// Mushroom-Wars-like game.
//
// Mouse:  left click / drag box = select (Shift adds, double click = all of that type on screen)
//         right click = smart command (move / attack / gather / return / send garrison), Shift queues
//         A = attack-move, S = stop, H = hold, P = patrol, Esc = cancel / pause
//         Ctrl+1..9 = set control group, 1..9 = recall; WASD/arrows or screen edge = scroll, wheel = zoom
//         Garrison buildings: drag from your building to a target to send troops (percent in the HUD)
// Touch:  tap = select / command, drag = scroll (or send from your garrison building), pinch = zoom
// F8:     RmlUi debugger. The UI itself is data: engine/assets/ui/*.rml (see ClientUi.cpp).
#include <fstream>
#include "engine/core/Hash.h"
#include <cstdlib>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/render/TextureCache.h"
#include "engine/render/GlobalGeometryPool.h"
#include "ClientSplash.h"
#include "NoticeEvents.h"
#include "GameClient.h"
#include "StartPlayers.h"
#include "engine/view/game/EncounterDecals.h"   // boss encounters: ground marks
#include "engine/platform/ContentReader.h"
#include "engine/platform/EnhancedInputConfig.h"
#include "RegionSettings.h"
#include "engine/game/Localize.h"
#include "engine/game/Tiles.h"
#include "engine/render/CameraBehavior.h"
#include "engine/view/game/GameOverlayData.h"
#include "engine/world/TerraWeaveCavernsClientVisualHost.h"
#include "engine/world/TerraWeaveCavernsArchive.h"

using namespace aiverse;
using platform::Event;
using platform::EventType;
using platform::Key;
using i18n::Args;

namespace aiverse::client {

GameClient::GameClient() = default;
GameClient::~GameClient() = default;

bool GameClient::updateCavernsVisual(const Json& authored) {
    if(!dev_||!sim_||!authored.isObject())return false;
    if(lastCavernsVisualJson_==authored)return true;
    auto source=std::make_shared<terraweave::caverns::CaveVolumeSnapshot>(
        terraweave::caverns::caveSnapshotFromJson(authored));
    if(!cavernsVisual_){
        cavernsVisual_=std::make_unique<
            terraweave::caverns::CaveClientVisualHost>();
        cavernsVisual_->open(std::move(source));
    } else cavernsVisual_->replaceSnapshot(std::move(source));
    lastCavernsVisualJson_=authored;
    return true;
}

// ------------------------------------------------------------------ setup

void GameClient::init(gfx::Device& dev) {
    for (int i = 0; i < kInitSteps; ++i) initStep(dev, i);
}

float GameClient::initWeight(int step) {
    static const float w[kInitSteps] = {0.15f, 0.15f, 0.35f, 0.1f, 0.1f, 0.15f};   // view: models, looks, terrain
    return step >= 0 && step < kInitSteps ? w[step] : 0.0f;
}

void GameClient::initStep(gfx::Device& dev, int step) {
    switch (step) {
    case 0: {   // manifest, rules, language
        // Never mint a second generation when hosted by the editor.
        // An external lease must match this exact device and host thread.
        if(gpuDeviceLease_.device()) {
            if(!gpuDeviceLease_.validFor(dev))
                throw std::logic_error("GameClient external GPU device lease expired");
        } else {
            ownedGpuLifetime_=std::make_unique<render::RenderDeviceLifetime>(dev);
            gpuDeviceLease_=ownedGpuLifetime_->lease();
        }
        dev_ = &dev;
        { // settings (language, online name) are saved here; platforms do not create it
            std::error_code ec;
            std::filesystem::create_directories(platform::userDataDir(), ec);
        }
        std::string text;
        try {
            if (!platform::readContentFile("game.json", text)) throw std::runtime_error("game.json not found in content");
            manifest_ = Json::parse(text);
            edgeScroll_ = render::CameraEdgeScroll::fromManifest(manifest_);
            cursors_.load(manifest_["cursors"]);
            gameName_ = manifest_.getStr("name", gameName_);
            rulesJson_ = readComposedJson(manifest_.getStr("rules", "rules.json"), [](const std::string& p, std::string& o) { return platform::readContentFile(p, o); });   // + includes / extends
            langs_ = i18n::readGameLanguages(manifest_);
            applyLanguage(!forcedLanguage_.empty() ? forcedLanguage_
                                                   : i18n::chooseLanguage(langs_, i18n::readSavedLanguage(platform::userDataDir())), false);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        // Project-owned Enhanced Input asset is optional. Bad data retains the legacy default shortcut map.
        std::string actionAsset;
        if (platform::readContentFile("input/actions.json", actionAsset)) {
            try {
                platform::enhanced_config::Asset authored;
                std::string inputError;
                if (!platform::enhanced_config::readAsset(Json::parse(actionAsset), authored, &inputError) ||
                    !platform::enhanced_config::installAsset(inputShortcuts_.system(), authored, &inputError))
                    std::fprintf(stderr, "RTS Input Actions: %s\n", inputError.c_str());
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "RTS Input Actions: %s\n", ex.what());
            }
        }
        if (!platform::hostedView()) platform::setTitle(gameName_);   // hosted: the editor keeps its title
        loadNetSettings();
        break;
    }
    case 1:     // renderer
        view_.setDeferredLoading(std::getenv("AIVERSE_EAGER_LOADING") == nullptr);
        view_.setStreamingTypes(initialAssetRadius_ > 0);
#ifndef __EMSCRIPTEN__
        { // Model URIs may contain ../ for shared textures. Normalize before the checked reader.
            auto reader = platform::makeContentReader(platform::contentRoot(), 512u * 1024u * 1024u);
            view_.setAsyncReader([reader](const std::string& path, std::string& out) {
                return reader(std::filesystem::path(path).lexically_normal().generic_string(), out);
            });
        }
#endif
        overlay_.init(dev, "fonts/ui.ttf", 17.0f);
        r3d_.setGpuDeviceLease(gpuDeviceLease_);
        // Touch devices on GLES: hardware PCF for the sun shadow (Shaders.h AIVERSE_HW_SHADOW), set before any
        // lit pipeline exists. Adreno 610: the manual bilinear filter cost ~6 ms a frame at 540p.
        if (platform::isTouchDevice() && dev.info().backend == gfx::Backend::GLES3) {
            const char* e = std::getenv("AIVERSE_HW_SHADOW");
            if (!(e && *e == '0')) { dev.setGlslPrelude("#define AIVERSE_HW_SHADOW 1\n"); r3d_.setHardwareShadowCompare(true); }
        }
        if (!r3d_.init(dev)) error_ = "renderer: " + dev.lastError();
        view_.setAppearancePipelineValidator(
            [this](const render::Model& model,
                   const std::vector<render::Material>& materials) {
                return r3d_.warmModelPipelinesChecked(model,&materials);
            });
        r3d_.setShadowQuality(Renderer3D::defaultQualityFor(dev, platform::isTouchDevice()));
        r3d_.setMobileShadowFilter(platform::isTouchDevice());
        r3d_.setMaterialQuality(Renderer3D::defaultMaterialQualityFor(dev, platform::isTouchDevice()));
        r3d_.setBakedQuality(Renderer3D::defaultBakedQualityFor(dev, platform::isTouchDevice()));
        r3d_.lights.setQuality(aiverse::render::LightSystem::defaultQualityFor(dev, platform::isTouchDevice()));
        prisma_.setGpuDeviceLease(gpuDeviceLease_);
        prisma_.init(dev);
        // PRISMA "dlss" effect: NVIDIA NGX provider when this build has the DLSS SDK
        // (AIVERSE_DLSS_SDK_DIR) and runs on Vulkan; otherwise the effect reports why.
        if (render::prismaDlssCompiled() && dev.info().backend == gfx::Backend::Vulkan)
            prisma_.setExternalUpscaler(render::makePrismaDlss());
        prismaBase_ = render::Prisma::defaultQualityFor(dev, platform::isTouchDevice());
        prisma_.setQuality(hostPostQuality_.value_or(prismaBase_));
        prisma_.setImageLoader([](const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba) {
            std::string bytes;
            return platform::readContentFile(path, bytes) && render::decodeImage(bytes, w, h, rgba);
        });
        // Optional texture-only cameras. The existing GameClient always owns
        // its main framebuffer and UI, so screen outputs are forbidden here.
        if(manifest_["lens"].getBool("enabled",false)) {
            std::string bytes,lensError;
            if(platform::readContentFile("cameras.json",bytes) &&
               lensCameras_.loadOffscreenAsset(bytes,&lensError) &&
               lensCameras_.init(dev,r3d_)) {
                lensCamerasActive_=true;
            } else {
                std::fprintf(stderr,"[lens] disabled: %s\n",
                    lensError.empty()?"camera asset unavailable":lensError.c_str());
            }
        }
        break;
    case 2:
        view_.setGpuDeviceLease(gpuDeviceLease_);
        view_.init(dev, rules_);
        view_.setSelectionOutline(manifest_.getBool("selectionOutline", true));
        break;     // models, textures, atlases of every type
    case 3:
        fx_.setGpuDeviceLease(gpuDeviceLease_);
        fx_.init(dev, rules_);
        break;
    case 4: initAudio(); break;
    case 5: initUi(); break;
    default: break;
    }
}

void GameClient::initAudio() {
    audio::AudioConfig ac;
    ac.maxVoices = platform::isTouchDevice() ? 24 : 32;
    if (const char* mode = std::getenv("AIVERSE_AUDIO")) { // off | null (audio thread without hardware, CI)
        ac.openDevice = std::string(mode) != "off";
        ac.nullBackend = std::string(mode) == "null";
    }
    if (const char* wav = std::getenv("AIVERSE_AUDIO_CAPTURE")) ac.capturePath = wav;
    audio_.init(ac);
    std::ifstream settings(platform::userDataDir() + "/audio.cfg");
    if (settings) audio_.loadSettings(std::string(std::istreambuf_iterator<char>(settings), {}));
    volume_ = boundVolume_ = int(std::lround(audio_.masterVolume() * 100));
    musicVolume_ = boundMusicVolume_ = int(std::lround(audio_.volume(audio::Bus::Music) * 100));
    sfxVolume_ = boundSfxVolume_ = int(std::lround(audio_.volume(audio::Bus::Sfx) * 100));
    musicOn_ = boundMusicOn_ = audio_.busEnabled(audio::Bus::Music) ? 1 : 0;
    sfxOn_ = boundSfxOn_ = audio_.busEnabled(audio::Bus::Sfx) ? 1 : 0;
    scriptSound_ = &scriptSoundImpl_;
    viewScripts_.setSound(scriptSound_);
    viewScripts_.setUiCall([this](lua_State* L) { return luaUiCall(L); });
    std::string text;
    if (platform::readContentFile(manifest_.getStr("audio", "audio.json"), text)) { // a game without audio.json is silent
        try {
            Json bank = Json::parse(text);
            world::addPhysisNatureSounds(bank);   // synth fallbacks unless audio.json has its own
            world::addPhysisTornadoSounds(bank);
            std::string err;
            if (!audio_.loadBank(bank, platform::readContentFile, &err) || !sound_.init(audio_, rules_, bank, &err)) error_ = err;
            for (auto& w : sound_.warnings()) std::fprintf(stderr, "audio: %s\n", w.c_str());
        } catch (const std::exception& e) {
            error_ = std::string("audio.json: ") + e.what();
        }
    }
    menuMusic();
}

void GameClient::saveAudioSettings() {
    std::ofstream f(platform::userDataDir() + "/audio.cfg");
    if (f) f << audio_.saveSettings();
}

// After the simulation advanced: sounds of what happened, acknowledgements, listener at the camera.
void GameClient::acknowledgeSelectionClick(Entity target) {
    const auto* owner = sim_->world().alive(target) ? sim_->world().tryGet<Owner>(target) : nullptr;
    if (!owner || owner->player != human_ || spectator_) {
        sound_.resetSelectionClicks();
        return;
    }
    if (sound_.onSelectionClick(*sim_, target)) heardSelection_ = selection_;
}

void GameClient::updateAudio(double dt) {
    if (sim_) {
        if (screen_ == Screen::Playing) sound_.update(*sim_, dt);
        if (selection_ != heardSelection_) { // a newly selected own unit answers
            sound_.resetSelectionClicks();
            Entity first = selection_.empty() ? Entity{} : selection_[0];
            auto* own = first.valid() ? sim_->world().tryGet<Owner>(first) : nullptr;
            if (own && own->player == human_ && std::find(heardSelection_.begin(), heardSelection_.end(), first) == heardSelection_.end())
                sound_.onSelect(*sim_, first);
            heardSelection_ = selection_;
        }
        const float3 eye = cam_.eye();
        const float target[3] = {cam_.target.x, cam_.target.y, cam_.target.z}, eyeP[3] = {eye.x, eye.y, eye.z};
        audio_.setListener(GameAudio::cameraListener(target, eyeP));
    }
    if (volume_ != boundVolume_ || musicVolume_ != boundMusicVolume_ || sfxVolume_ != boundSfxVolume_ ||
        musicOn_ != boundMusicOn_ || sfxOn_ != boundSfxOn_) { // changed from the pause menu / settings screen
        audio_.setMasterVolume(float(std::clamp(volume_, 0, 100)) / 100);
        audio_.setVolume(audio::Bus::Music, float(std::clamp(musicVolume_, 0, 100)) / 100);
        audio_.setBusEnabled(audio::Bus::Music, musicOn_ != 0);
        for (audio::Bus b : {audio::Bus::Sfx, audio::Bus::Ui, audio::Bus::Voice, audio::Bus::Ambient}) { // "sfx": every effect bus
            audio_.setVolume(b, float(std::clamp(sfxVolume_, 0, 100)) / 100);
            audio_.setBusEnabled(b, sfxOn_ != 0);
        }
        boundVolume_ = volume_;
        boundMusicVolume_ = musicVolume_;
        boundSfxVolume_ = sfxVolume_;
        boundMusicOn_ = musicOn_;
        boundSfxOn_ = sfxOn_;
        saveAudioSettings();
    }
    audio_.update(dt);
}

void GameClient::presentOrder(const Command& c) {
    if (c.units.empty()) return;
    switch (c.type) {
    case kCmdMove: case kCmdAttack: case kCmdSmart: case kCmdPatrol: case kCmdRally:
    case kCmdSend: case kCmdFollow: case kCmdGuard: case kCmdGather: case kCmdRepair:
        break;
    default: return;
    }
    Vec3 pos = c.point;
    if (c.target.valid())
        if (const auto* transform = sim_->world().tryGet<Transform>(c.target)) pos = transform->pos;
    SimEvent event{kViewEventOrder, c.player, c.units.front(), c.target, c.type, pos};
    event.flags = c.flags;
    pendingOrders_.push_back(event);
    viewScripts_.queueLocalOrder(event);
}

void GameClient::pingMap(float3 point) {
    pendingPings_.push_back({point, human_});
    sound_.play(Cue::Ping);
}

void GameClient::toMenu() {
    playback_.reset();
    pendingPings_.clear();
    pendingOrders_.clear();
    markers_.clear();
    hostPaused_ = hostStep_ = false;
    hostSpeed_=0;
    hostCheckpoint_.clear();
    mapLoading_ = false; loadingTypes_.clear(); loadingSplash_.shutdown();
    sound_.detach();
    natureStop();
    fx_.stopMatch();
    audio_.stopAll(0.2f, combatBuses());
    noticeEvents_.reset();
    city_.reset(); sim_.reset();
    setScreen(Screen::Menu);
    menuMusic();
}


// Display copy of the rules and every data text in the chosen language. Safe at any time: a running
// simulation has its own Rules and never sees translated names.
void GameClient::applyLanguage(const std::string& tag, bool save) {
    auto reader = [](const std::string& path, std::string& out) { return platform::readContentFile(path, out); };
    try {
        i18n::loadGameLocalization(loc_, reader, langs_, tag);
    } catch (const std::exception& e) {
        error_ = e.what();
        loc_.clear();
        loc_.setLanguage(langs_.source);
    }
    if (save) i18n::saveLanguage(platform::userDataDir(), loc_.language());
    langNames_.clear();
    for (auto& l : langs_.available) langNames_.push_back(i18n::languageSelfName(reader, l));
    gameName_ = loc_.trOr("game.name", manifest_.getStr("name", "AIVERSE"));
    if (!platform::hostedView()) platform::setTitle(gameName_);   // hosted: the editor keeps its title
    rules_ = loadRules(rulesJson_);
    localizeRules(rules_, loc_);
    configureLoading();
    maps_.clear();
    for (auto& m : manifest_["maps"].items()) {
        std::string file = m.getStr("file");
        maps_.push_back({file, loc_.trOr("map." + mapStem(file) + ".title", m.getStr("title", file))});
    }
    if (!netMaps_.empty()) describeNetMaps(); // localized titles
    ui_.setKeyboardLanguage(loc_.language());
    luaEmit("language", Json::string(loc_.language()));
}

Json GameClient::startRuntimeTextureMaterial(size_t type,size_t choice,size_t material,
        const Json& graph,const Json& overrides,const render::texture_graph::ResidentMaterialOptions& options){
    if(!dev_||!gpuDeviceLease_.validFor(*dev_)||!inMatch()||runtimeTextureTask_)
        throw std::runtime_error("runtime texture target unavailable or request active");
    const auto current=view_.graphMaterialTarget(type,choice,material);
    if(!current)throw std::runtime_error("runtime texture material not resident");
    auto result=runtimeTextureGraph_.startResidentMaterial(graph,overrides,runtimeTextureScope_,gpuDeviceLease_,options,current->identity);
    runtimeTextureRequest_=result.getStr("request");
    runtimeTextureTask_=view::RuntimeTextureGraphTask{runtimeTextureRequest_,type,choice,current->identity};
    runtimeTextureState_="queued";runtimeTextureReason_.clear();return runtimeTextureMaterialStatus();
}
std::optional<GameView::GraphMaterialTargetInfo> GameClient::runtimeTextureMaterialTarget(size_t type,size_t choice,size_t material)const{
    if(!dev_||!gpuDeviceLease_.validFor(*dev_)||!inMatch())return {};
    return view_.graphMaterialTarget(type,choice,material);
}
render::texture_graph::NativeGraphImageRead GameClient::readRuntimeTextureMaterialImage(
        size_t type,size_t choice,const view::LiveAppearanceMaterialTarget& target,
        std::string_view output)const{
    if(!dev_||!gpuDeviceLease_.validFor(*dev_)||!inMatch()||mapLoading_||runtimeTextureTask_)return {};
    const auto state=runtimeTextureGraph_.status();
    if(state.getBool("busy")||state.getBool("gpuTextureSetReady"))return {};
    return view_.readGraphMaterialImage(type,choice,target,output);
}
render::texture_graph::GraphImageReclaimResult GameClient::collectRuntimeTextureMaterialImages(size_t type,size_t choice,size_t material,
        const view::LiveAppearanceMaterialTarget& target,const render::texture_graph::GraphImageRoots& external){
    if(!dev_||!gpuDeviceLease_.validFor(*dev_)||!inMatch()||runtimeTextureTask_)
        throw std::runtime_error("runtime texture collection requires an idle live presentation host");
    const auto graphState=runtimeTextureGraph_.status();
    if(graphState.getBool("busy")||graphState.getBool("gpuTextureSetReady"))
        throw std::runtime_error("runtime texture collection requires released graph results");
    const auto result=view_.collectGraphMaterialImages(type,choice,material,target,external,
        [this](const render::Model& model,std::vector<std::span<const render::Material>>& roots){
            return appendRuntimeGraphImageRoots(model,roots);
        });
    if(result.detached)runtimeTextureGraph_.invalidateOutputReuseReceipt();
    return result;
}
bool GameClient::appendRuntimeGraphImageRoots(const render::Model& model,
        std::vector<std::span<const render::Material>>& roots)const{
    return r3d_.appendGraphImageRoots(model,roots)&&
        (!insetR3d_||insetR3d_->appendGraphImageRoots(model,roots))&&
        lensModelSnapshot_.appendGraphImageRoots(model,roots);
}
void GameClient::setAutomaticRuntimeGraphImageCollection(GameView::GraphImageRootProvider provider,
        bool recycleSlots,bool detachCompletedReplay){
    if(!provider){view_.setAutomaticGraphImageCollection({});return;}
    view_.setAutomaticGraphImageCollection(
        [this,provider=std::move(provider),detachCompletedReplay](const render::Model& model,
                std::vector<std::span<const render::Material>>& roots){
            if(!provider(model,roots))return false;
            // Caller explicitly certified that no later CPU consumer will
            // replay the prior frame. No GPU work is destroyed here.
            if(detachCompletedReplay){
                // Both owner-thread renderers must be ready before mutating
                // either. A capture on the inset must not discard main replay.
                if(!r3d_.canDetachCompletedGraphImageReplay()||
                   (insetR3d_&&!insetR3d_->canDetachCompletedGraphImageReplay()))return false;
                if(!r3d_.detachCompletedGraphImageReplay())return false;
                if(insetR3d_&&!insetR3d_->detachCompletedGraphImageReplay())return false;
                lensModelSnapshot_.clear();
            }
            return appendRuntimeGraphImageRoots(model,roots);
        },recycleSlots);
}
Json GameClient::runtimeTextureMaterialStatus()const{
    auto r=runtimeTextureGraph_.status();
    r.set("applicationState",Json::string(runtimeTextureState_));
    r.set("applicationReason",Json::string(runtimeTextureReason_));
    r.set("applicationRequest",Json::string(runtimeTextureRequest_));
    const auto sweep=view_.automaticGraphImageCollectionStats();
    r.set("activeModelReaders",Json::string(std::to_string(view_.modelResources().activeModelReaders())));
    r.set("automaticCollectionEnabled",Json::fromBool(view_.automaticGraphImageCollectionEnabled()));
    r.set("automaticRecyclingEnabled",Json::fromBool(view_.automaticGraphImageRecyclingEnabled()));
    r.set("automaticCollectionInspected",Json::string(std::to_string(sweep.inspected)));
    r.set("automaticCollectionDetached",Json::string(std::to_string(sweep.detached)));
    r.set("automaticCollectionDeferred",Json::string(std::to_string(sweep.deferred)));
    return r;
}
bool GameClient::cancelRuntimeTextureMaterial(const std::string& request){
    if(!dev_||!gpuDeviceLease_.validFor(*dev_)||!runtimeTextureTask_||runtimeTextureTask_->request!=request)return false;
    Json p=Json::object();p.set("request",Json::string(request));
    runtimeTextureGraph_.cancel(p,runtimeTextureScope_);
    runtimeTextureTask_.reset();runtimeTextureState_="cancelled";runtimeTextureReason_="cancelled by native caller";return true;
}
void GameClient::tickRuntimeTextureMaterial(gfx::Device& dev){
    if(runtimeTextureRequest_.empty())return; // unused feature has no per-frame owner work
    // No render encoders or Model read scopes are active at this frame hook.
    // Cancel stale scene tasks before issuing more GPU work.
    bool targetCurrent=inMatch();
    if(runtimeTextureTask_&&targetCurrent){
        const auto& task=*runtimeTextureTask_;
        const auto current=view_.graphMaterialTarget(task.type,task.choice,task.target.material);
        targetCurrent=current&&current->identity.publication==task.target.publication&&current->identity.version==task.target.version;
    }
    if(runtimeTextureTask_&&!targetCurrent&&gpuDeviceLease_.validFor(dev)){
        const auto request=runtimeTextureTask_->request;
        const auto s=runtimeTextureGraph_.status();
        if(s.getStr("request")==request&&(s.getStr("state")=="queued"||s.getStr("state")=="pending"||s.getStr("state")=="ready"))
            cancelRuntimeTextureMaterial(request);
    }
    runtimeTextureGraph_.tick(dev,runtimeTextureScope_);
    if(!runtimeTextureTask_)return;
    const auto s=runtimeTextureGraph_.status();
    using Decision=view::RuntimeTextureGraphDecision;
    const auto decision=view::decideRuntimeTextureGraph(*runtimeTextureTask_,s.getStr("request"),s.getStr("state"),
        s.getBool("busy"),s.getBool("gpuTextureSetReady"),targetCurrent);
    if(decision==Decision::Wait){runtimeTextureState_="pending";return;}
    if(decision==Decision::Cancel){cancelRuntimeTextureMaterial(runtimeTextureTask_->request);return;}
    const auto task=*runtimeTextureTask_;
    runtimeTextureTask_.reset();
    if(decision==Decision::Failed){runtimeTextureState_="failed";runtimeTextureReason_=s.getStr("reason");return;}
    std::string why;
    const bool ok=view_.applyCompletedGraphMaterial(task.type,task.choice,task.target,
        runtimeTextureGraph_,runtimeTextureScope_,task.request,&why);
    runtimeTextureState_=ok?"applied":"failed";runtimeTextureReason_=std::move(why);
    // A refused target must not leave a completed resident set holding the owner.
    if(!ok){const auto after=runtimeTextureGraph_.status();
        if(after.getStr("request")==task.request&&after.getStr("state")=="ready"&&!after.getBool("published")){
            Json p=Json::object();p.set("request",Json::string(task.request));runtimeTextureGraph_.cancel(p,runtimeTextureScope_);
        }
    }
}

void GameClient::shutdown() {
    if(view_.modelMutationBlocked())
        throw std::logic_error("GameClient shutdown: live Model read or graph transaction");
    if (campaignCamera_) {
        platform::setPointerLock(false);
        campaignCamera_.reset();
    }
    cursors_.clear();
    // Preserve the MotionWeave device lease and shutdown contract.
    // The additional camera resources must retire before Renderer3D dies.
    // A failed GPU idle must not abort the rest of the shutdown (audio, net,
    // UI, renderer): log it and leak the LENS targets instead of destroying
    // textures the GPU may still use.
    if(lensCamerasActive_) {
        lensCamerasActive_=false;
        try {
            if(!dev_ || !dev_->waitForResourceIdle())
                std::fprintf(stderr,"[lens] shutdown: GPU did not reach idle; LENS render targets leaked\n");
            else
                lensCameras_.shutdownAfterGpuIdle();
        } catch(const std::exception& e) {
            std::fprintf(stderr,"[lens] shutdown failed (%s); LENS render targets leaked\n",e.what());
        }
    }
    loadingSplash_.shutdown();
    net_.reset(); // detaches from the simulation before it goes away
    stopLan();
    fx_.shutdown();
    sound_.detach();
    audio_.shutdown();
    horizonFrameTiming_.detach(*dev_);
    if (cavernsVisual_) {
        // Shutdown of a GPU owner requires genuine device-idle proof.
        // If unavailable, keep that owner inert instead of destructing live
        // Model GPU references on a borrowed Editor-hosted Device.
        if (!dev_ || !cavernsVisual_->closeAfterGpuIdle(*dev_)) {
            std::fprintf(stderr,
                "[caverns] GPU cave shutdown idle proof unavailable; owner quarantined\n");
            (void)cavernsVisual_.release();
        } else cavernsVisual_.reset();
    }
    if (worldScene_) { worldScene_->shutdown(*dev_); worldScene_.reset(); }
    if(dev_&&!runtimeTextureRequest_.empty()){
        if(!dev_->waitForResourceIdle())throw std::runtime_error("runtime texture graph shutdown: GPU idle barrier failed");
        runtimeTextureGraph_.shutdownAfterIdle(*dev_);
        runtimeTextureTask_.reset();runtimeTextureState_="idle";runtimeTextureReason_.clear();runtimeTextureRequest_.clear();
    }
    ui_.setDynamicTexture("portrait",{},portraitSize_,portraitSize_);
    selectionPortrait_.shutdown();
    view_.shutdown(*dev_);
    prisma_.shutdown();
    r3d_.setBakedLighting(nullptr);
    baked_.release();
    r3d_.shutdown();
    spine_.shutdown();
    shutdownUi();
    minimap_.release(*dev_);
    overlay_.shutdown();
    // All owned render resources are gone while gfx::Device still exists.
    // Editor-hosted clients only drop their borrowed lease; the editor owns
    // the epoch and retires it after ALL of its own resource owners shutdown.
    if(ownedGpuLifetime_)ownedGpuLifetime_->retire();
    ownedGpuLifetime_.reset();
    gpuDeviceLease_={};
    dev_=nullptr;
}

world::PhysisNatureRuntime::Targets GameClient::natureTargets() {
    world::PhysisNatureRuntime::Targets t;
#ifndef AIVERSE_CLIENT_NO_FX
    t.particles = &fx_.particles();
#endif
    t.files = [](const std::string& p, std::string& o) { return platform::readContentFile(p, o); };
    t.audio = &audio_;
    t.scene = worldScene_.get();
    t.prisma = &prisma_;
    return t;
}

void GameClient::natureStart(const Json& map) {
    if (nature_.start(map, natureTargets()))
        std::fprintf(stderr, "[physis] nature presentation on\n");
}

void GameClient::natureUpdate(float dt) {
    if (!sim_ || !nature_.configured()) return;
    world::PhysisNatureRuntime::View v;
    v.focusX = cam_.target.x; v.focusY = cam_.target.y; v.focusZ = cam_.target.z;
    v.dt = dt;
    v.secondsPerTick = 1.0 / double(std::max<uint32_t>(1, sim_->tickRate()));
    v.sourceToken = uint64_t(uintptr_t(sim_.get()));
    const auto stats = nature_.update(sim_->world(), v, natureTargets());
    static const bool dbg = std::getenv("AIVERSE_PHYSIS_DEBUG") != nullptr;
    static int frames = 0;
    if (dbg && stats.active && ++frames % 60 == 0)
        std::fprintf(stderr, "[physis] tick %llu flames %zu smoke %zu voices %zu wind %.1f,%.1f tornadoes %zu debris %zu"
                     " fire %zu (%.2f) spray %zu (%.2f) embers %zu scars %zu\n",
                     static_cast<unsigned long long>(stats.tick), stats.flames, stats.smoke,
                     stats.voices, double(stats.meanWindX), double(stats.meanWindZ),
                     stats.tornadoes, stats.debris, stats.fireFunnels, double(stats.maxFireness),
                     stats.sprays, double(stats.maxSpray), stats.embers, stats.scars);
}

void GameClient::natureStop() { nature_.stop(natureTargets()); }

void GameClient::configurePrisma(const Json& map) {
    prisma_.configure(manifest_["postfx"], map["postfx"]);
    std::vector<std::string> prismaWarnings;
    prismaVolumes_ = render::parsePrismaVolumes(manifest_["prismaVolumes"], map["prismaVolumes"], &prismaWarnings);
    for (const auto& issue : prismaWarnings)
        std::fprintf(stderr, "[prisma] %s\n", issue.c_str());
    prisma_.setQuality(hostPostQuality_.value_or(prismaBase_));
}

std::vector<std::string> GameClient::matchFileList(bool replay) const {
    return MatchFiles::list(std::filesystem::path(saveDir())/"matches", replay);
}

bool GameClient::writeMatchFile(const std::string& slot, bool replay) {
    if (!sim_ || netMatch_ || city_ || playback_ || mapLoading_ || mapIndex_ >= maps_.size()) {
        error_ = "A local match is required";
        return false;
    }
    MatchFile file;
    file.map = maps_[mapIndex_].file;
    file.setup = matchSetup_.dump();
    file.contentHash = matchContentHash_;
    file.seed = sim_->config().seed;
    if (replay) {
        auto& timeline = file.replay;
        timeline.module = sim_->module().name();
        timeline.mapPath = file.map;
        timeline.rulesPath = manifest_.getStr("rules","rules.json");
        timeline.seed = file.seed;
        timeline.dataHash = file.contentHash;
        timeline.tickRate = sim_->tickRate();
        timeline.inputDelay = sim_->config().inputDelayTicks;
        timeline.endTick = sim_->tick();
        timeline.finalChecksum = sim_->checksum();
        timeline.commands = sim_->executedCommands();
    } else {
        file.simulation = sim_->serialize();
        for (const auto& bot : bots_) file.bots.emplace_back(bot.player(),bot.saveState());
    }
    if (!MatchFiles::write(std::filesystem::path(saveDir())/"matches",slot,replay,file,error_)) return false;
    platform::syncUserData();
    slotsDirty_ = true;
    error_.clear();
    return true;
}

bool GameClient::openMatchFile(const std::string& slot, bool replay) {
    if (netMatch_ || city_) { error_ = "Leave the network match first"; return false; }
    MatchFile file;
    if (!MatchFiles::read(std::filesystem::path(saveDir())/"matches",slot,replay,file,error_)) return false;
    if (file.nativeReplay) {
        describeNetMaps();
        const auto recorded = std::filesystem::path(file.map).lexically_normal().generic_string();
        const auto found = std::find_if(netMaps_.begin(),netMaps_.end(),[&](const auto& m) {
            const auto suffix = "/" + m.file;
            return m.dataHash == file.contentHash && (recorded == m.file ||
                (recorded.size() >= suffix.size() && recorded.compare(recorded.size()-suffix.size(),suffix.size(),suffix)==0));
        });
        if (found == netMaps_.end()) { error_ = "Replay content does not match an installed map"; return false; }
        file.map = found->file;
    }
    auto map = std::find_if(maps_.begin(),maps_.end(),[&](const auto& m){ return m.file == file.map; });
    if (map == maps_.end()) { error_ = "Saved map is unavailable"; return false; }
    if (replay && (file.replay.module != "game" || file.replay.tickRate == 0 || file.replay.tickRate > 1000 ||
        file.replay.seed != file.seed || file.replay.dataHash != file.contentHash)) {
        error_ = "Incompatible replay setup";
        return false;
    }
    return startMatch(size_t(map-maps_.begin()),Json(),&file,replay);
}

bool GameClient::startMatch(size_t idx, const Json& initialVars, const MatchFile* file, bool playback, std::optional<uint8_t> localSeat) {
    if (idx >= maps_.size() || netMatch_ || (file && city_)) return false;
    // A new map/game is a camera cut. Retire the old view's private
    // exposure + motion history without releasing shared render sizes.
    prisma_.retireView(3ULL);
    r3d_.retireObjectMotionView(3ULL,
        dev_ ? dev_->gpuSubmissionFence() : 0);
    std::string text;
    if (!platform::readContentFile(maps_[idx].file, text)) { error_ = "map not found: " + maps_[idx].file; return false; }
    Json map;
    try {
        map = Json::parse(text);
        Json setup = Json::object();
        if (file) setup = Json::parse(file->setup);
        else {
            setup.set("vars",initialVars); setup.set("players",startPlayers_); setup.set("deck",startDeck_);
            setup.set("human",Json::number(int64_t(human_))); setup.set("spectator",Json::fromBool(spectator_));
            setup.set("difficulty",Json::number(int64_t(difficulty_)));
            Json sets = Json::array();
            for (const auto& id : startRuleSets_) sets.push(Json::string(id));
            setup.set("ruleSets",std::move(sets));
        }
        if (localSeat) {
            const auto& players = map["players"];
            if (!players.isArray() || players.size()==0 || players.size()>255 || *localSeat>players.size() ||
                (*localSeat && players[*localSeat-1].getBool("waves")))
                throw std::runtime_error("The selected local seat is unavailable on this map");
            Json controllers = Json::array();
            for (size_t i=0; i<players.size(); ++i) {
                Json row = Json::object();
                row.set("controller", Json::string(controllerName(i+1==*localSeat ? SeatController::Human : SeatController::Computer)));
                controllers.push(std::move(row));
            }
            setup.set("players",std::move(controllers));
            setup.set("human",Json::number(int64_t(*localSeat ? *localSeat : 1)));
            setup.set("spectator",Json::fromBool(*localSeat==0));
        }
        const int human = int(setup.getInt("human",1));
        if (human < 0 || human > 255) throw std::runtime_error("Invalid saved seat");
        game::mergeInitialVars(map,setup["vars"]);
        const auto playerError = mergeStartPlayers(map,setup["players"],human,setup["deck"]);
        if (!playerError.empty()) throw std::runtime_error(playerError);
        if (setup["ruleSets"].size()) {
            Json sets = map["ruleSets"].isArray() ? map["ruleSets"] : Json::array();
            for (const auto& id : setup["ruleSets"].items())
                if (rulesJson_["ruleSets"][id.str()].isArray()) sets.push(id);
            if (sets.size()) map.set("ruleSets",sets);
        }
        std::string terr; // 2D tile maps: the tileset decides cell kinds / levels / fields
        if (!applyTilesToMap(map, [](const std::string& p, std::string& o) { return platform::readContentFile(p, o); }, &terr)) throw std::runtime_error(terr);
        SimConfig cfg;
        // startMatch owns offline play. Network peers construct their simulation in Lobby.
        cfg.singleAuthorityPhysics = !playback;
        cfg.seed = uint64_t(std::time(nullptr));
        if (const char* sd = std::getenv("AIVERSE_SEED")) cfg.seed = uint64_t(std::strtoull(sd, nullptr, 10));   // testing: reproducible matches
        if (file) cfg.seed = file->seed;
        if (playback) {
            cfg.tickRate = file->replay.tickRate;
            cfg.inputDelayTicks = file->replay.inputDelay;
        }
        auto candidate = std::make_unique<Simulation>(cfg, std::make_unique<GameCore>());
        Json rules = rulesJson_;
        if (!embedScripts(rules, map, [](const std::string& p, std::string& o) { return platform::readContentFile(p, o); }, &terr)) throw std::runtime_error(terr);
        if (!file) cityPrepare(rules, map);
        Hasher content;
        content.str(rules.dump()); content.str(map.dump());
        if (file && !file->nativeReplay && content.value() != file->contentHash) throw std::runtime_error("Match rules or map changed");
        candidate->setup(rules, map);
        std::vector<ComputerPlayer> restoredBots;
        if (file && !playback) {
            if (!candidate->deserialize(file->simulation,&terr)) throw std::runtime_error(terr);
            AIConfig ai;
            const auto difficulty = setup.getInt("difficulty",1);
            ai.thinkTicks = difficulty == 0 ? 30 : difficulty == 1 ? 15 : 8;
            ai.firstAttackArmy = difficulty == 0 ? 12 : difficulty == 1 ? 8 : 6;
            for (const auto& [seat,bytes] : file->bots) {
                if (!candidate->player(seat)) throw std::runtime_error("Unknown saved bot seat");
                restoredBots.emplace_back(seat,cfg.seed,ai);
                if (!restoredBots.back().loadState(bytes) || restoredBots.back().player()!=seat)
                    throw std::runtime_error("Invalid saved bot state");
            }
        }
        if (playback) {
            for (const auto& command : file->replay.commands) candidate->inject(command);
            candidate->setCommandSink([](Command&&) -> uint32_t { return 0; });
        }
        sim_ = std::move(candidate);
        if (!city_) { human_ = uint8_t(human); spectator_ = setup.getBool("spectator"); }
        difficulty_ = int(setup.getInt("difficulty",1));
        playback_ = playback ? std::optional<Replay>(file->replay) : std::nullopt;
        playbackSpeed_ = 1; playbackPaused_ = false;
        bots_ = std::move(restoredBots);
        matchSetup_ = std::move(setup); matchContentHash_ = content.value();
        mapCamera_ = map["camera"];
        startPlayers_ = startDeck_ = Json(); startRuleSets_.clear();
        hostPaused_ = hostStep_ = false;
        if (!file && !city_) for (const auto& seat : sim_->players()) {
            const auto& authored = map["players"][size_t(seat.slot - 1)];
            const auto controller = authored.has("controller") ? seat.controller :
                (seat.slot == human_ && !spectator_ ? SeatController::Human : SeatController::Computer);
            if (seat.controller != controller) sim_->inject(seatControllerCommand(seat.slot, controller, true));
        }
        attachNoticeEvents(false);
        cityAttach();
        horizonFrameTiming_.detach(*dev_);
        if (cavernsVisual_) {
            if(!cavernsVisual_->closeAfterGpuIdle(*dev_))
                throw std::runtime_error(
                    "Caverns previous game visual GPU owner still in flight");
            cavernsVisual_.reset();
        }
        if (worldScene_) { worldScene_->shutdown(*dev_); worldScene_.reset(); }
        // Optional authored cave volume for BOTH standalone GameClient and
        // the Editor Play Game window. This is visual-only; no Box3D/nav ACK.
        const Json& caves=map["terraweaveCaverns"].isObject()
            ? map["terraweaveCaverns"] : map["extra"]["terraweaveCaverns"];
        lastCavernsVisualJson_=Json();
        if(caves.isObject()) {
            lastCavernsVisualJson_=caves;
            auto frozen=std::make_shared<terraweave::caverns::CaveVolumeSnapshot>(
                terraweave::caverns::caveSnapshotFromJson(caves));
            cavernsVisual_=std::make_unique<
                terraweave::caverns::CaveClientVisualHost>();
            cavernsVisual_->open(std::move(frozen));
        }
        // static scenery streamed around the camera instead of being loaded whole. The key is "streamedWorld" here:
        // in these maps "world" is the planet system (FPS maps keep the same block under "world")
        if (const Json& sw = map["streamedWorld"]; sw.getBool("async")) {
            worldScene_ = std::make_unique<world::WorldScene>(sw, platform::makeContentReader(platform::contentRoot()), view_.textureCache());
            worldRadius_ = int(std::clamp<int64_t>(sw.getInt("viewRadius", 2), 0, 4));
        }
        scriptUiReset();
        if (std::getenv("AIVERSE_SCRIPT_LOG"))   // view scripts' log lines too ("[view]" / "[view error]")
            viewScripts_.onLog = [](const ScriptLogEntry& e) {
                std::printf("[view%s] t=%u %s\n", e.error ? " error" : "", unsigned(e.tick), e.text.c_str()); std::fflush(stdout); };
        if (viewScripts_.load(*sim_, rules, map, uint8_t(spectator_ ? 0 : human_))) viewScripts_.attach(*sim_);
        view_.lights().configure(manifest_["lighting"], map["lighting"], map["lights"]); // day/night, map lights
        if (auto pe = game::configurePlayerColors(manifest_["playerColors"]); !pe.empty()) std::fprintf(stderr, "%s\n", pe.c_str());
        configurePrisma(map);
        natureStart(map);
        configureBakedLighting(maps_[idx].file);
        quality_.reset(int(r3d_.shadowQuality()), int(r3d_.lights.quality()));
    } catch (const std::exception& e) {
        error_ = e.what(); return false;
    }
    mapIndex_ = idx;
    if (!file) for (auto& p : sim_->players()) {
        const auto& authored = map["players"][size_t(p.slot - 1)];
        if (authored.has("controller") ? p.controller != SeatController::Computer : (p.slot == human_ && !spectator_)) continue;
        AIConfig c;
        c.thinkTicks = difficulty_ == 0 ? 30 : difficulty_ == 1 ? 15 : 8;
        c.firstAttackArmy = difficulty_ == 0 ? 12 : difficulty_ == 1 ? 8 : 6;
        bots_.emplace_back(p.slot, sim_->config().seed, c);
    }
    selection_.clear();
    for (auto& g : groups_) g.clear();
    view_.setTiles(map["tiles"]);
    {   // generated maps (map / world body "generate"): the view reads the map the simulation was built from
        const Json& vm = mod().generatedMap() ? *mod().generatedMap() : map;
        // planets: bodies / heights / cells from the simulation's Surface (visual keys from the map); flat: unchanged
        if (mod().surface(*sim_)) view_.setSimWorld(mod().state(*sim_).surface, mod().state(*sim_).grid, vm);
        else view_.setWorld(vm);
        view_.setTerrainMaterial(vm["terrainMaterial"], mod().state(*sim_).grid.w, mod().state(*sim_).grid.h);
        view_.setTerrainDetail(vm);
    }
    view_.buildTerrain(*dev_, mod().state(*sim_).grid);
    if (std::getenv("AIVERSE_SCRIPT_LOG") && mod().scripts())   // testing (screenshots, captures): script log lines on stdout
        mod().scripts()->onLog = [](const ScriptLogEntry& e) {
            std::printf("[script%s] t=%u %s\n", e.error ? " error" : "", unsigned(e.tick), e.text.c_str()); std::fflush(stdout); };
    if (const char* ff = std::getenv("AIVERSE_FAST_FORWARD"); !file && ff) { // testing (screenshots): bots play N game seconds first
        const uint32_t ticks = uint32_t(std::max(0.0, std::atof(ff)) * sim_->tickRate());
        std::vector<ComputerPlayer> ffBots = bots_;   // the human slot is played by a bot too while fast-forwarding
        if (!spectator_) ffBots.emplace_back(human_, sim_->config().seed, AIConfig{});
        for (uint32_t i = 0; i < ticks && sim_->result() == 0; ++i) { for (auto& b : ffBots) b.update(*sim_); sim_->step(); }
        bots_.assign(ffBots.begin(), ffBots.begin() + long(bots_.size()));
    }
    sceneFromSim(*sim_, scene_, 0, spectator_ ? 0 : human_);
    fitCamera();
    buildMinimap(map);
    mode_ = Mode::Normal;
    setScreen(Screen::Playing);
    audio_.stopAll(0.1f, combatBuses());
    sound_.attach(*sim_, spectator_ ? 0 : human_, maps_[idx].file);
    fx_.configureEntityEffects(rulesJson_, map);   // data-declared entity effects (SpectraFX)
    fx_.startMatch(*sim_, spectator_ ? 0 : human_, view_, mod().state(*sim_).grid);
    heardSelection_.clear();
    beginMapLoading();
    error_.clear();
    return true;
}

void GameClient::fitCamera() {
    if (planet()) { fitPlanetCamera(); return; }
    auto& g = mod().state(*sim_).grid;
    float3 c{g.w * 0.5f, 0, g.h * 0.5f};
    bool small = std::max(g.w, g.h) <= 48;
    if (!small) for (auto& o : scene_.objects) if (o.owner == human_ && rules_.types[o.type].structure) { c = o.pos; break; }
    cam_.target = {c.x, 0, c.z};
    cam_.pitch = 0.95f; cam_.yaw = 0;
    cam_.maxDistance = std::max(40.0f, float(std::max(g.w, g.h)) * 1.3f);
    const auto overhead = render::overheadCameraFrom(manifest_);
    cam_.designAspect = overhead.designAspect;
    const float viewportAspect = platform::windowHeight() > 0 ? platform::windowWidth() / platform::windowHeight() : 1.6f;
    cam_.distance = small ? std::max(g.w / cam_.projectionAspect(viewportAspect), float(g.h)) * 1.05f + 4 : 24.0f;
    render::applyOverheadCamera(overhead, cam_, float(platform::framebufferHeight())); // game.json "camera" (2D games)
    if (const char* e = std::getenv("AIVERSE_CAM")) { // testing (close-up screenshots): "x,z,distance[,pitchDeg[,yawDeg]]"
        float v[5] = {c.x, c.z, cam_.distance, cam_.pitch * 57.2958f, cam_.yaw * 57.2958f};
        int n = std::sscanf(e, "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]);
        if (n >= 2) { cam_.target = {v[0], 0, v[1]}; cam_.distance = v[2]; cam_.pitch = v[3] / 57.2958f; cam_.yaw = v[4] / 57.2958f; cam_.minDistance = std::min(cam_.minDistance, v[2]); }
    }
    if (hasStartCamera_) {   // host "Play from here"
        hasStartCamera_ = false;
        const float* v = startCamera_;
        cam_.target = {v[0], 0, v[1]}; cam_.distance = std::clamp(v[2], 2.0f, cam_.maxDistance);
        cam_.pitch = std::clamp(v[3], 10.0f, 89.0f) / 57.2958f; cam_.yaw = v[4] / 57.2958f;
        cam_.minDistance = std::min(cam_.minDistance, cam_.distance);
    }
    topDownConfigure();   // top-down heroes: follow camera on the own hero (ClientTopDown.cpp)
}

// ------------------------------------------------------------------ picking / selection

bool GameClient::groundAt(float sx, float sy, float3& out) const {
    if (planet()) { // planet picking -> sim-space point (anchor(body) + body-local, Surface.h)
        int body; float3 local;
        if (!view_.pickPlanet(cam_, sx, sy, platform::windowWidth(), platform::windowHeight(), body, local)) return false;
        out = GameView::bodyToSim(body, local);
        return true;
    }
    return cam_.screenToGround(sx, sy, platform::windowWidth(), platform::windowHeight(), out);
}

Entity GameClient::pick(float sx, float sy) const {
    float3 g;
    if (!groundAt(sx, sy, g)) return kNullEntity;
    Entity best; float bestD = 1e9f;
    for (auto& o : scene_.objects) {
        if (!o.selectable(rules_)) continue;
        const UnitType& t = rules_.types[o.type];
        if (view_.spriteHit(cam_, o, static_cast<GameCore&>(sim_->module()).state(*sim_).grid, sx, sy, platform::windowWidth(), platform::windowHeight())) {
            if (bestD > 0.01f) { best = o.entity; bestD = 0.01f; } // sprites stand above their ground point
            continue;
        }
        if (t.layer & kLayerAir) { // flyers are drawn above the ground: test in screen space
            float2 sp;
            if (objectScreen(o, 2.4f, sp)) {
                float d = std::hypot(sp.x - sx, sp.y - sy);
                if (d < 22 && d * 0.05f < bestD) { best = o.entity; bestD = d * 0.05f; }
            }
            continue;
        }
        float r = view_.selectionRadius(o.type) + (t.structure ? 0.2f : 0.35f);
        float d = std::hypot(g.x - o.pos.x, g.z - o.pos.z);
        if (planet()) { // same body, 3D distance between the picked ground point and the object's (body-local)
            int gb; float3 gl;
            if (o.body < 0 || !GameView::simToBody(g, gb, gl) || gb != o.body) continue;
            d = length(gl - o.pos);
        }
        if (d < r && d < bestD) { best = o.entity; bestD = d; }
    }
    return best;
}

void GameClient::cleanSelection() {
    World& w = sim_->world();
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [&](Entity e) {
        const auto* unit = w.tryGetConst<Unit>(e);
        return !unit || (unit->flags & kUnitInHand) || !rules_.types[unit->type].selectable;
    }), selection_.end());
}

bool GameClient::selectionHasGarrison() const {
    for (Entity e : selection_) if (sim_->world().has<Garrison>(e)) return true;
    return false;
}

bool GameClient::selectionBuilds() const {   // a builder is selected (repair / reclaim modes)
    for (Entity e : selection_)
        if (auto* u = sim_->world().tryGet<Unit>(e); u && !rules_.types[u->type].builds.empty()) return true;
    return false;
}

void GameClient::selectBox(float x0, float y0, float x1, float y1, bool add) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    std::vector<Entity> units, structs;
    for (auto& o : scene_.objects) {
        if (o.owner != human_ || !o.selectable(rules_)) continue;
        float2 sp;
        float y = (rules_.types[o.type].layer & kLayerAir) ? 2.4f : 0.3f;
        if (!objectScreen(o, y, sp)) continue;
        if (sp.x < x0 || sp.x > x1 || sp.y < y0 || sp.y > y1) continue;
        (rules_.types[o.type].structure ? structs : units).push_back(o.entity);
    }
    if (!add) selection_.clear();
    for (Entity e : units.empty() ? structs : units)
        if (std::find(selection_.begin(), selection_.end(), e) == selection_.end()) selection_.push_back(e);
}

void GameClient::selectSameType(Entity e) {
    auto* u = sim_->world().tryGet<Unit>(e);
    if (!u) return;
    selection_.clear();
    for (auto& o : scene_.objects) {
        if (o.owner != human_ || o.type != u->type || !o.selectable(rules_)) continue;
        float2 sp;
        if (objectScreen(o, o.pos.y, sp) && sp.x >= 0 && sp.y >= 0 &&
            sp.x <= platform::windowWidth() && sp.y <= platform::windowHeight())
            selection_.push_back(o.entity);
    }
}

void GameClient::command(uint16_t type, float sx, float sy, bool queue) {
    cleanSelection();
    if (selection_.empty()) return;
    Command c;
    c.type = type;
    c.flags = queue ? kCmdQueue : 0;
    c.units = selection_;
    float3 g;
    const bool hitGround = groundAt(sx, sy, g);
    if (hitGround) c.point = {Fixed::fromInt(int64_t(std::lround(g.x * 256))) / 256, Fixed{}, Fixed::fromInt(int64_t(std::lround(g.z * 256))) / 256};
    if (planet() && hitGround) c.point = simPoint(g);   // planets: all three coordinates
    Entity t = pick(sx, sy);
    if (type != kCmdMove && type != kCmdPatrol) c.target = t;
    if (type == kCmdSmart && selectionHasGarrison()) {
        if (!t.valid() || !sim_->world().has<Garrison>(t)) return;
        c.type = kCmdSend; c.param = sendPercent_;
        std::vector<Entity> src;
        for (Entity e : selection_) if (e != t && sim_->world().has<Garrison>(e)) src.push_back(e);
        c.units = src;
    }
    if (!hitGround && !c.target.valid()) return;
    issue(c);
    if (hitGround) markers_.push_back({g, time_ + 0.6});
}

Vec3 GameClient::placeCentre(float sx, float sy, bool& ok) {
    ok = false;
    float3 g;
    if (placeType_ < 0 || !groundAt(sx, sy, g)) return {};
    if (planet()) { ok = true; return simPoint(g); }   // the sim snaps footprints on faces (snapToFootprint)
    const UnitType& t = rules_.types[size_t(placeType_)];
    float cx = std::floor(g.x) + (t.footprintW % 2 ? 0.5f : 0.0f), cz = std::floor(g.z) + (t.footprintH % 2 ? 0.5f : 0.0f);
    ok = true;
    return {Fixed::fromInt(int64_t(cx * 2)) / 2, Fixed{}, Fixed::fromInt(int64_t(cz * 2)) / 2};
}

// ------------------------------------------------------------------ input

void GameClient::event(const Event& incoming) {
    // UI/platform routing sees the physical device; only world-level gameplay
    // receives the logical button assigned to semantic Select/Command Actions.
    Event mapped=incoming;
    const Event& e=mapped;
    if (mapLoading_) { if (e.type == EventType::KeyDown && e.key == Key::Escape) toMenu(); return; }
    if (!padInjecting_ && (e.type == EventType::MouseMove || e.type == EventType::MouseDown || e.type == EventType::TouchBegin))
        gamepad_.mouseUsed(e.x, e.y);   // a real pointer hides the pad cursor (ClientGamepad.cpp)
    const bool encounterPointer = campaignCamera_ && screen_ == Screen::Playing && !howtoOpen_ &&
        (e.type == EventType::MouseMove || e.type == EventType::MouseDown || e.type == EventType::MouseUp ||
         e.type == EventType::MouseWheel) && platform::pointerLocked();
    if (encounterPointer && e.type == EventType::MouseWheel) return;
    bool uiWants = !encounterPointer && ui_.handleEvent(e);
    if (e.type == EventType::FocusLost) {
        std::fill_n(keys_,int(platform::Key::Count),false);
        mouseX_ = mouseY_ = -1;
        edgePointer_.reset();
    }
    if(screen_!=Screen::Playing || e.type==EventType::FocusLost){entityActions_.cancel();entityPointer_=false;mapTapPending_=false;}
    if (e.type == EventType::KeyDown && e.key == Key::F8 && !e.repeat) ui_.toggleDebugger();
    // a press that started on the UI keeps the pointer until release (dragging out of a panel
    // must not start a selection box); a press that started in the world ignores the UI
    if (e.type == EventType::MouseDown || e.type == EventType::TouchBegin) uiOwnsPointer_ = uiWants;
    if (e.type == EventType::MouseMove || e.type == EventType::TouchMove) uiWants = uiWants && !boxing_ && !sendDrag_ && !panning_;
    if (e.type == EventType::MouseUp || e.type == EventType::TouchEnd || e.type == EventType::TouchCancel) uiWants = uiOwnsPointer_;
    inputShortcuts_.setTopDownEnabled(screen_==Screen::Playing && topDownActive());
    inputShortcuts_.setPlanetEnabled(screen_==Screen::Playing && planet());
    inputShortcuts_.setPossessionEnabled(screen_==Screen::Playing && possessedUnit().valid());
    const auto inputActions=inputShortcuts_.onEvent(e,screen_==Screen::Playing && !uiWants && !howtoOpen_ && sim_);
    if(!uiWants && screen_==Screen::Playing && !topDownActive() &&
       !regionSwipe_.enabled && !planet() && !possessedUnit().valid() &&
       (e.type==EventType::MouseDown||e.type==EventType::MouseUp)) {
        mapped.button=inputShortcuts_.logicalMouseButton(incoming.button);
        if(mapped.button<0)return; // unassigned physical button cannot select/order
    }
    if (!uiWants) overlay_.handleEvent(e);
    mapDragEvent(e, uiWants);   // rules "mapClick": drag from a map-drag HUD element onto the map (ClientScriptCmd.cpp)
    if (e.type == EventType::KeyDown || e.type == EventType::KeyUp) {
        const int code=int(e.key);
        if (code>=0 && code<int(platform::Key::Count)) {
            const bool allowed=screen_==Screen::Playing && !uiWants &&
                               !ui_.textInputFocused() && !howtoOpen_;
            // Legacy camera/top-down fallbacks are presentation-only: a key
            // captured by a text field or modal must never stay latched here.
            keys_[code]=e.type==EventType::KeyDown && allowed;
        }
    }
    if (campaignCamera_ && screen_ == Screen::Playing && e.type == EventType::MouseMove && !howtoOpen_) {
        if (platform::pointerLocked()) encounterLook_.motion(e.deltaX, e.deltaY);
        else if (!uiWants && mouseX_ >= 0) encounterLook_.motion(e.x - mouseX_, e.y - mouseY_);
        mouseX_ = e.x;
        mouseY_ = e.y;
        return;
    }
    if (e.type == EventType::MouseMove || e.type == EventType::MouseDown) { mouseX_ = e.x; mouseY_ = e.y; }
    // A view script with ui.on("back", fn) owns Esc / Android Back (which arrives as Escape) during a match: the engine's
    // pause / resume / tool cancel are skipped; a true return cancels the pointer interaction in progress (docs/scripting.md)
    if (e.type == EventType::KeyDown && e.key == Key::Escape && !e.repeat && sim_ && !howtoOpen_ &&
        (screen_ == Screen::Playing || screen_ == Screen::Paused) && viewScripts_.hasUiHandler("back")) {
        Json args = Json::object();
        args.set("screen", Json::string(screen_ == Screen::Paused ? "paused" : "playing"));
        args.set("platform", Json::string(platform::name()));
        if (viewScripts_.uiEvent(*sim_, "back", args)) {
            mapDrag_ = false; skillDragging_ = false; skillTouch_ = -1; rectDrag_ = false; uiOwnsPointer_ = false;
            if (mode_ != Mode::Normal) mode_ = Mode::Normal;
        }
        return;
    }
    if (e.type == EventType::KeyDown && !e.repeat && e.key != Key::Unknown && sim_ &&
        screen_ == Screen::Playing && !uiWants && !ui_.textInputFocused() && !howtoOpen_ &&
        viewScripts_.hasUiHandler("keydown")) {
        Json args = Json::object();
        args.set("key", Json::string(ClientGamepad::keyName(e.key)));
        args.set("shift", Json::fromBool(e.mods & platform::kModShift));
        args.set("ctrl", Json::fromBool(e.mods & platform::kModCtrl));
        args.set("alt", Json::fromBool(e.mods & platform::kModAlt));
        args.set("super", Json::fromBool(e.mods & platform::kModSuper));
        if (viewScripts_.uiEvent(*sim_, "keydown", args)) {
            keys_[int(e.key)] = false;
            inputShortcuts_.suppressKey(e.key);
            return;
        }
    }
    // Script HUD drags own their pointer from button press through release on the world.
    if(skillDragging_) {
        const bool begin=e.type==EventType::MouseDown||e.type==EventType::TouchBegin;
        if(begin){
            if(e.type==EventType::TouchBegin&&skillTouch_!=-1&&skillTouch_!=e.touchId){skillDragging_=false;mode_=Mode::Normal;return;}
            skillStartX_=e.x;skillStartY_=e.y;skillTouch_=e.type==EventType::TouchBegin?e.touchId:-1;
        }
        if(e.type==EventType::FocusLost||e.type==EventType::TouchCancel||
           (e.type==EventType::KeyDown&&e.key==Key::Escape)||
           (e.type==EventType::MouseDown&&e.button==1)) {
            skillDragging_=false;skillTouch_=-1;mode_=Mode::Normal;return;
        }
        if(begin||e.type==EventType::MouseMove||e.type==EventType::TouchMove||e.type==EventType::MouseUp||e.type==EventType::TouchEnd){
            if((e.type==EventType::TouchMove||e.type==EventType::TouchEnd)&&e.touchId!=skillTouch_)return;
            mouseX_=e.x;mouseY_=e.y;
            auto move=e;move.type=EventType::MouseMove;
            ui_.handleEvent(move);skillOverWorld_=!ui_.pointerOverUi();
            if(e.type==EventType::MouseUp||e.type==EventType::TouchEnd){
                if(screen_==Screen::Playing&&skillOverWorld_&&std::hypot(e.x-skillStartX_,e.y-skillStartY_)>=6){
                    auto click=e;click.type=EventType::MouseDown;click.button=0;toolMouseDown(click);
                }
                skillDragging_=false;skillTouch_=-1;mode_=Mode::Normal;uiOwnsPointer_=false;
            }
            return;
        }
    }
    // mobile apps go quiet in the background; desktop keeps playing when the window loses focus
    if (e.type == EventType::FocusLost && platform::isTouchDevice()) audio_.setPaused(true);
    if (e.type == EventType::FocusGained) audio_.setPaused(false);
    // hosted battles on phones: away in the background (nobody waits for our input), back with a reconnect if long
    if (battle_ && net_ && e.type == EventType::FocusLost && platform::isTouchDevice()) net_->suspend(net::nowMs());
    if (battle_ && net_ && e.type == EventType::FocusGained && net_->suspended()) net_->resume(net::nowMs());
    if (e.type == EventType::Resized) luaEmit("resize", Json());   // UI scripts: safe area, layout
    if (howtoOpen_ && e.type == EventType::KeyDown && e.key == Key::Escape) { howtoOpen_ = false; return; }
    // the game's own screens (ui/lib/meta.lua): Esc / Android back goes through ui.back()
    if (screen_ == Screen::Menu && !engineMenu_ && e.type == EventType::KeyDown && e.key == Key::Escape && !e.repeat) { luaEmit("back", Json()); return; }
    if (screen_ == Screen::Paused && e.type == EventType::KeyDown && e.key == Key::Escape && !uiWants) { setScreen(Screen::Playing); return; }
    if (screen_ != Screen::Playing || !sim_) return;
    if (!planet() && !uiWants && e.type == EventType::MouseDown && e.button == 0 && (e.mods & platform::kModAlt)) {
        float3 point;
        if (groundAt(e.x, e.y, point)) pingMap(point);
        return;
    }
    if (planet() && planetCameraEvent(e, uiWants, inputActions)) return;   // orbit camera (ClientPlanet.cpp)
    if (regionEvent(e, uiWants)) return;
    if (topDownEvent(e, uiWants, inputActions)) return;   // top-down heroes: sticks, buttons, WASD + mouse aim (ClientTopDown.cpp)
    World& w = sim_->world();
    switch (e.type) {
    case EventType::MouseDown:
        if (uiWants) return;
        downX_ = e.x; downY_ = e.y;
        if (toolMouseDown(e)) return;   // skills, rect tools, hand, possession (ClientActions.cpp)
        if (e.button == 0) {
            if (mode_ == Mode::Place) {
                bool ok;
                Vec3 c = placeCentre(e.x, e.y, ok);
                if (!ok) sound_.play(Cue::Error);
                if (ok && placeProducer_.valid()) {
                    Command cmd; cmd.type = kCmdPlace; cmd.units = {placeProducer_}; cmd.point = c;
                    issue(cmd);
                    placeProducer_ = kNullEntity;
                    mode_ = Mode::Normal;
                    return;
                }
                if (ok) {
                    Command cmd; cmd.type = kCmdBuild; cmd.param = placeType_; cmd.flags = (e.mods & platform::kModShift) ? kCmdQueue : 0;
                    for (Entity s : selection_)
                        if (auto* u = w.tryGet<Unit>(s); u && std::find(rules_.types[u->type].builds.begin(), rules_.types[u->type].builds.end(), uint16_t(placeType_)) != rules_.types[u->type].builds.end()) { cmd.units = {s}; break; }
                    cmd.point = c;
                    if (!cmd.units.empty()) issue(cmd);
                }
                if (!(e.mods & platform::kModShift)) mode_ = Mode::Normal;
                return;
            }
            if (scriptCommandClick(e)) return;   // ClientScriptCmd.cpp
            if (mode_ == Mode::Ability || mode_ == Mode::Claim) {
                float3 g;
                if (groundAt(e.x, e.y, g)) {
                    Command c;
                    c.type = mode_ == Mode::Claim ? kCmdClaim : kCmdAbility;
                    c.point = {Fixed::fromRatio(int64_t(g.x * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(g.z * 256), 256)};
                    if (planet()) c.point = simPoint(g);
                    if (mode_ == Mode::Ability) {
                        c.param = abilityIndex_;
                        c.target = pick(e.x, e.y);
                        for (Entity s2 : selection_) {
                            auto& st = rules_.types[w.get<Unit>(s2).type];
                            if (size_t(abilityIndex_) < st.abilities.size()) c.units.push_back(s2);
                        }
                    }
                    issue(c);
                }
                mode_ = Mode::Normal;
                return;
            }
            if (mode_ == Mode::AttackMove || mode_ == Mode::Patrol || mode_ == Mode::Rally) {
                command(mode_ == Mode::AttackMove ? kCmdAttack : mode_ == Mode::Patrol ? kCmdPatrol : kCmdRally, e.x, e.y, e.mods & platform::kModShift);
                mode_ = Mode::Normal;
                return;
            }
            if (mode_ == Mode::Repair || mode_ == Mode::Reclaim) {   // field operations: click the unit / wreck
                command(mode_ == Mode::Repair ? kCmdRepair : kCmdReclaim, e.x, e.y, e.mods & platform::kModShift);
                mode_ = Mode::Normal;
                return;
            }
            Entity t = pick(e.x, e.y);
            if (t.valid() && w.has<Garrison>(t) && w.get<Owner>(t).player == human_) {
                sendDrag_ = true; // Mushroom-Wars gesture: drag from own building to a target
                if (std::find(selection_.begin(), selection_.end(), t) == selection_.end()) {
                    if (!(e.mods & platform::kModShift)) selection_.clear();
                    selection_.push_back(t);
                }
                acknowledgeSelectionClick(t);
                return;
            }
            if (t.valid() && selectionHasGarrison() && w.has<Garrison>(t)) { command(kCmdSmart, e.x, e.y, false); return; }
            boxing_ = true;
        } else if (e.button == 1) {
            if (mode_ != Mode::Normal) { mode_ = Mode::Normal; rectDrag_ = false; return; }
            command(kCmdSmart, e.x, e.y, e.mods & platform::kModShift);
        } else {
            panning_ = true;
        }
        break;
    case EventType::MouseMove:
        hover_ = uiWants ? kNullEntity : pick(e.x, e.y);
        if (mapTapPending_ && std::hypot(e.x - mapTapX_, e.y - mapTapY_) > 10) {   // the press became a drag: pan from here
            mapTapPending_ = false; panning_ = true; downX_ = e.x; downY_ = e.y;
        }
        if (panning_) {
            float3 a, b;
            if (groundAt(downX_, downY_, a) && groundAt(e.x, e.y, b)) cam_.target += a - b;
            downX_ = e.x; downY_ = e.y;
        }
        if (sendDrag_) {
            Entity t = pick(e.x, e.y);
            if (t.valid() && w.has<Garrison>(t) && w.get<Owner>(t).player == human_ &&
                std::find(selection_.begin(), selection_.end(), t) == selection_.end()) selection_.push_back(t); // swipe-select
        }
        break;
    case EventType::MouseUp:
        if (mapTapPending_) {
            mapTapPending_ = false;
            if (e.button == 0) { mapClickTap(e.x, e.y); panning_ = false; break; }
        }
        if (toolMouseUp(e)) { panning_ = false; break; }
        if (e.button == 0) {
            if (sendDrag_) {
                Entity t = pick(e.x, e.y);
                bool moved = std::hypot(e.x - downX_, e.y - downY_) > 10;
                if (moved && t.valid() && w.has<Garrison>(t)) command(kCmdSmart, e.x, e.y, false);
                sendDrag_ = false;
            } else if (boxing_) {
                boxing_ = false;
                bool add = e.mods & platform::kModShift;
                if (std::hypot(e.x - downX_, e.y - downY_) > 6) selectBox(downX_, downY_, e.x, e.y, add);
                else {
                    Entity t = pick(e.x, e.y);
                    if (t.valid() && time_ - lastClickTime_ < 0.3 && w.get<Owner>(t).player == human_) selectSameType(t);
                    else if (t.valid()) {
                        if (!add) selection_.clear();
                        if (std::find(selection_.begin(), selection_.end(), t) == selection_.end()) selection_.push_back(t);
                    } else if (!add) selection_.clear();
                    acknowledgeSelectionClick(t);
                    lastClickTime_ = time_;
                }
            }
        }
        panning_ = false;
        break;
    case EventType::MouseWheel: {
        if (uiWants) return;
        // Mouse wheel drives semantic CameraZoom (potentially rebound/scaled per project).
        const auto* zoom=inputShortcuts_.system().state("CameraZoom");
        if(zoom && zoom->value.x!=0.0f)
            cam_.distance=std::clamp(cam_.distance*std::pow(0.88f,zoom->value.x),
                                     cam_.minDistance,cam_.maxDistance);
        break;
    }
    case EventType::TouchBegin:
        if (uiWants && touches_.empty()) return;
        touches_[e.touchId] = {e.x, e.y};
        if (touches_.size() == 1) {
            downX_ = mouseX_ = e.x; downY_ = mouseY_ = e.y; touchMoved_ = false;
            if (rectMode()) { rectDrag_ = true; rectX_ = e.x; rectY_ = e.y; sendDrag_ = false; break; }
            Entity t = pick(e.x, e.y);
            sendDrag_ = t.valid() && w.has<Garrison>(t) && w.get<Owner>(t).player == human_;
            if (sendDrag_) {
                if (std::find(selection_.begin(), selection_.end(), t) == selection_.end()) selection_ = {t};
                acknowledgeSelectionClick(t);
            }
        } else if (touches_.size() == 2) {
            sendDrag_ = false;
            auto it = touches_.begin(); auto a = it->second; auto b = (++it)->second;
            pinchDist_ = std::hypot(a.first - b.first, a.second - b.second);
            downX_ = (a.first + b.first) * 0.5f; downY_ = (a.second + b.second) * 0.5f;
        }
        break;
    case EventType::TouchMove: {
        if (!touches_.count(e.touchId)) return;
        touches_[e.touchId] = {e.x, e.y};
        if (touches_.size() == 1) {
            if (std::hypot(e.x - downX_, e.y - downY_) > 12) touchMoved_ = true;
            mouseX_ = e.x; mouseY_ = e.y;
            if (rectDrag_) break;   // painting a rect, not panning
            if (sendDrag_) {
                Entity t = pick(e.x, e.y);
                if (t.valid() && w.has<Garrison>(t) && w.get<Owner>(t).player == human_ &&
                    std::find(selection_.begin(), selection_.end(), t) == selection_.end()) selection_.push_back(t);
            } else if (touchMoved_) {
                float3 a,b;
                const auto* drag=inputShortcuts_.system().state("CameraTouchDrag");
                // Use normalized semantic gesture deltas, with legacy fallback
                // for projects that define their own complete input asset.
                const float dx=drag?drag->value.x:e.x-downX_;
                const float dy=drag?drag->value.y:e.y-downY_;
                if(groundAt(e.x-dx,e.y-dy,a)&&groundAt(e.x,e.y,b))
                    cam_.target+=a-b;
                downX_=e.x;downY_=e.y;
            }
        } else if (touches_.size() == 2) {
            auto it = touches_.begin(); auto a = it->second; auto b = (++it)->second;
            const float rawDistance=std::hypot(a.first-b.first,a.second-b.second);
            const auto* pinch=inputShortcuts_.system().state("CameraPinch");
            const float appliedDelta=pinch?pinch->value.x:rawDistance-pinchDist_;
            // Keep the previous reference distance, not the authored scaling,
            // so CameraPinch modifiers can tune sensitivity without jumps.
            if(pinchDist_>1 && rawDistance>1)
                cam_.distance=std::clamp(cam_.distance*pinchDist_/
                        std::max(1.0f,pinchDist_+appliedDelta),
                        cam_.minDistance,cam_.maxDistance);
            pinchDist_=rawDistance;
            const float cx=(a.first+b.first)*0.5f,cy=(a.second+b.second)*0.5f;
            const auto* pan=inputShortcuts_.system().state("CameraTouchPan");
            const float px=pan?pan->value.x:cx-downX_;
            const float py=pan?pan->value.y:cy-downY_;
            float3 p0,p1;
            if(groundAt(cx-px,cy-py,p0)&&groundAt(cx,cy,p1))
                cam_.target+=p0-p1;
            downX_=cx;downY_=cy;
            touchMoved_=true;
        }
        break;
    }
    case EventType::TouchEnd: case EventType::TouchCancel: {
        if (!touches_.count(e.touchId)) return;
        bool single = touches_.size() == 1 && e.type == EventType::TouchEnd;
        touches_.erase(e.touchId);
        // a pinch ends with one finger left: it pans from where it is (not from the pinch midpoint)
        if (touches_.size() == 1) { downX_ = touches_.begin()->second.first; downY_ = touches_.begin()->second.second; }
        if (rectDrag_) {
            Event up = e; up.type = EventType::MouseUp; up.button = 0;
            if (single) toolMouseUp(up); else rectDrag_ = false;
            break;
        }
        if (single) {
            if (sendDrag_) {
                if (touchMoved_) command(kCmdSmart, e.x, e.y, false);
            } else if (!touchMoved_ && !uiWants) {
                Entity t = pick(e.x, e.y);
                if (mapClickTap(e.x, e.y)) {   // rules "mapClick": a tap is the game's tap command, as a mouse click
                } else if (mode_ == Mode::Place || mode_ == Mode::Ability || mode_ == Mode::Claim || mode_ == Mode::Skill || mode_ == Mode::Drop || mode_ == Mode::ScriptCmd) {
                    Event fake = e; fake.type = EventType::MouseDown; fake.button = 0; fake.mods = 0;
                    event(fake);
                } else if (t.valid() && w.get<Owner>(t).player == human_ && !selectionHasGarrison()) {
                    if (time_ - lastClickTime_ < 0.3) selectSameType(t); else selection_ = {t};
                    acknowledgeSelectionClick(t);
                    lastClickTime_ = time_;
                } else if (!selection_.empty()) {
                    command(mode_ == Mode::AttackMove ? kCmdAttack : kCmdSmart, e.x, e.y, false);
                    mode_ = Mode::Normal;
                } else if (t.valid() && w.get<Owner>(t).player == human_) {
                    selection_ = {t};
                    acknowledgeSelectionClick(t);
                }
            }
        }
        sendDrag_ = false;
        break;
    }
    case EventType::KeyDown: {
        if (uiWants) return;
        const bool ctrl = e.mods & (platform::kModCtrl | platform::kModSuper);
        if (e.key == Key::Escape && possessedUnit().valid() && mode_ == Mode::Normal) { Command c; c.type = kCmdPossess; issue(c); break; }
        if (e.key == Key::Escape && mode_ == Mode::Normal && !cardPage_.empty()) {
            const auto slash = cardPage_.rfind('/');
            cardPage_ = slash == std::string::npos ? "" : cardPage_.substr(0,slash);
            break;
        }
        if (e.key == Key::Escape) { if (mode_ != Mode::Normal) { mode_ = Mode::Normal; rectDrag_ = false; } else setScreen(Screen::Paused); }
        if (possessedUnit().valid()) break;   // WASD steers the possessed creature
        if(!ctrl && scriptCommandKey(e.key)) {
            inputShortcuts_.suppressKey(e.key);
            break; // authored script hotkeys still take precedence
        }
        if (!ctrl && selection_ == cardKeySelection_) {
            const auto it = cardKeys_.find(e.key);
            if (it != cardKeys_.end() && size_t(it->second) < actions_.size()) {
                inputShortcuts_.suppressKey(e.key);
                if (!e.repeat && actions_[size_t(it->second)]) actions_[size_t(it->second)]();
                break;
            }
        }
        if (e.key >= Key::Num1 && e.key <= Key::Num9) {
            int gi = int(e.key) - int(Key::Num0);
            if (ctrl) groups_[size_t(gi)] = selection_;
            else if (selectionHasGarrison() && gi <= 4) sendPercent_ = gi * 25; // garrison games: 1-4 = 25..100%
            else if (!groups_[size_t(gi)].empty()) {
                selection_ = groups_[size_t(gi)];
                cleanSelection();
            }
        }
        applyRtsActions(inputActions,!ctrl);
        break;
    }
    default: break;
    }
}

// Semantic RTS Actions can fire on a physical event OR after a timed
// Hold/Pulse interval. Both paths must send the same validated Commands.
void GameClient::applyRtsActions(const std::vector<std::string>& actions,bool allowOrders) {
    if(actions.empty() || !sim_ || screen_!=Screen::Playing ||
       topDownActive() || possessedUnit().valid())return;
    const auto fired=[&](const char* name){
        return std::find(actions.begin(),actions.end(),name)!=actions.end();
    };
    if(allowOrders && !selection_.empty()) {
        if(fired("AttackMove"))mode_=Mode::AttackMove;
        if(fired("Patrol"))mode_=Mode::Patrol;
        if(mod().fieldOps() && selectionBuilds()) {
            if(fired("Repair"))mode_=Mode::Repair;
            if(fired("Reclaim"))mode_=Mode::Reclaim;
        }
        if(fired("Stop")){Command c;c.type=kCmdStop;c.units=selection_;issue(c);}
        if(fired("Hold")){Command c;c.type=kCmdHold;c.units=selection_;issue(c);}
    }
    if(fired("FocusCamera"))fitCamera();
}

void GameClient::attachNoticeEvents(bool confirmedOnly) {
    notices_.clear();
    activeNoticeIndices_.clear();
    messageLogOpen_ = false;
    cardPage_.clear();
    cameraShake_.clear();
    shakeBindings_.clear();
    std::unordered_set<uint16_t> events;
    for (const auto& binding : manifest_["camera"]["shakes"].items()) {
        const int kind = game::script::simEventType(binding.getStr("event"));
        if (kind < 0) continue;
        events.insert(uint16_t(kind));
        shakeBindings_[uint16_t(kind)].push_back(binding);
    }
    noticeEvents_ = collectNoticeEvents(*sim_, confirmedOnly, std::move(events));
}

void GameClient::notice(const std::string& text, double duration, std::string style) {
    if (style.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        style.clear();
    notices_.push_back({text, std::move(style), time_ + (std::isfinite(duration) ? std::max(0.0, duration) : 2.5)});
    if (notices_.back().until > time_) activeNoticeIndices_.push_back(notices_.size() - 1);
}

void GameClient::noticeMissingResource(int64_t resource) {
    if (resource >= 0 && size_t(resource) < rules_.resources.size())
        notice(tr("notice.not_enough", Args().str("resource", rules_.resources[size_t(resource)].name)));
}

void GameClient::processEvents() {
    if (!noticeEvents_ || !sim_) return;
    // Input can be confirmed before the corresponding tick is simulated.
    const uint32_t confirmed = netMatch_ && net_ && net_->session()
        ? std::min(sim_->tick(), net_->session()->confirmedNext()) : 0;
    for (const auto& ev : noticeEvents_->drain(confirmed)) {
        const auto shakes = shakeBindings_.find(ev.type);
        if (shakes != shakeBindings_.end() && (spectator_ || ev.player == human_ || mod().isVisible(*sim_, human_, ev.pos))) {
            const Unit* source = sim_->world().alive(ev.a) ? sim_->world().tryGet<Unit>(ev.a) : nullptr;
            const int64_t type = ev.type == kEvDied ? ev.value : source ? source->type : -1;
            for (const auto& binding : shakes->second) {
                if (binding.has("type") && (type < 0 || size_t(type) >= rules_.types.size() ||
                    rules_.types[size_t(type)].id != binding.getStr("type"))) continue;
                if (binding.has("ability") && (ev.type != kEvAbility || !source || source->type >= rules_.types.size() ||
                    ev.value < 0 || size_t(ev.value) >= rules_.types[source->type].abilities.size() ||
                    rules_.types[source->type].abilities[size_t(ev.value)].id != binding.getStr("ability"))) continue;
                cameraShake_.start(binding, float(ev.pos.x.toDouble()), float(ev.pos.z.toDouble()));
            }
        }
        if (ev.type == kEvChat && ev.message) {
            const auto* sender = sim_->player(ev.player);
            if (sender) notice(sender->name + ": " + ev.message->text, 2.5, "chat");
            continue;
        }
        if (ev.type == kEvMessage) {
            if (ev.player != 0 && (ev.player != human_ || spectator_)) continue;
            if (ev.message) notice(ev.message->text, ev.message->duration, ev.message->style);
            else {
                const auto& texts = mod().state(*sim_).trig.messages;
                if (ev.value >= 0 && size_t(ev.value) < texts.size()) notice(texts[size_t(ev.value)]);
            }
            continue;
        }
        if (ev.player != human_ || spectator_) continue;
        if (ev.type == kEvNotEnough) {
            if (ev.value == 255) notice(tr("notice.need_supply"));
            else if (ev.value == 254) notice(tr("notice.no_room"));
            else noticeMissingResource(ev.value);
        } else if (ev.type == kEvBuildDone) notice(tr("notice.built", Args().str("name", rules_.types[size_t(ev.value)].name)));
        else if (ev.type == kEvReadyToPlace) notice(tr("notice.ready_to_place", Args().str("name", rules_.types[size_t(ev.value)].name)));
        else if (ev.type == kEvResearched) notice(tr("notice.researched", Args().str("name", rules_.techs[size_t(ev.value)].name)));
        else if (ev.type == kEvLevelUp) notice(tr("notice.hero_level", Args().num("level", ev.value)));
        else if (ev.type == kEvLowPower) notice(tr("notice.low_power"));
        else if (ev.type == kEvUpgraded) notice(tr("notice.upgraded", Args().str("name", rules_.types[size_t(ev.value)].name)));
        else if (ev.type == kEvRegionClaimed) notice(tr("notice.region_claimed"));
        else if (ev.type == kEvPlayerDefeated) notice(tr("notice.defeated"));
    }
}

// View scripts: presentation requests -> notices, particle effects, sounds (never into the sim).
void GameClient::runViewScripts(float dt) {
    if (!viewScripts_.active()) return;
    game::ViewTargeting target;
    if(screen_==Screen::Playing&&mode_==Mode::Skill&&abilityIndex_>=0&&size_t(abilityIndex_)<rules_.playerAbilities.size()){
        target.active=true;target.ability=rules_.playerAbilities[size_t(abilityIndex_)].id;
        float3 p{};target.overWorld=groundAt(mouseX_,mouseY_,p)&&(!skillDragging_||skillOverWorld_);
        target.x=p.x;target.z=p.z;
    }
    // a drag from a map-drag HUD element (mapClick games' drag-to-cast): ability "map-drag", the pointer's ground point,
    // overWorld while it is over the map (a release there taps it)
    // (the view's map-drag offset moves the ground point, e.g. above a finger; overWorld stays the pointer's own)
    if(screen_==Screen::Playing&&mapDrag_&&!target.active){
        target.active=true;target.ability="map-drag";target.touch=mapDragTouch_>=0;target.sx=mapDragX_;target.sy=mapDragY_;
        float dx=0,dy=0;viewScripts_.mapDragOffset(dx,dy);
        float3 p{};target.overWorld=groundAt(mapDragX_,mapDragY_,p)&&!mapDragOverUi_;
        if(dx!=0||dy!=0){p={};if(!groundAt(mapDragX_+dx,mapDragY_+dy,p))target.overWorld=false;}
        target.x=p.x;target.z=p.z;
    }
    if(screen_==Screen::Playing&&entityPointer_)if(const auto* source=entityActions_.source()){
        target.active=true;target.ability=source->abilityId;target.caster=source->entity.packed();
        float3 p{};target.overWorld=groundAt(mouseX_,mouseY_,p)&&!ui_.pointerOverUi();target.x=p.x;target.z=p.z;
    }
    std::vector<Entity> visibleSelection;
    for (Entity e : selection_) if (visibleSelectionUnit(e)) visibleSelection.push_back(e);
    viewScripts_.setSelection(visibleSelection);
    viewScripts_.setTargeting(target);
    {   // view.camera(): zoom in the units of game.json "camera" (ortho: visible view-plane height)
        game::ViewCamera vc;
        vc.x = cam_.target.x; vc.z = cam_.target.z;
        vc.zoom = cam_.ortho ? 2.0f * cam_.orthoHalfHeight() : cam_.distance;
        vc.pitch = cam_.pitch * 57.29578f; vc.yaw = cam_.yaw * 57.29578f;
        vc.width = int(platform::framebufferWidth()); vc.height = int(platform::framebufferHeight());
        vc.ortho = cam_.ortho;
        vc.view = cam_; vc.screenW = platform::windowWidth(); vc.screenH = platform::windowHeight();   // view.to_screen
        viewScripts_.setCamera(vc);
    }
    viewScripts_.setScreen(screen_ == Screen::Paused ? "paused" : screen_ == Screen::Over ? "over" : "playing");   // view.screen()
    viewScripts_.update(*sim_, dt);
    if (static const char* pf = std::getenv("AIVERSE_VIEW_PROFILE"); pf && *pf) {   // testing: view script cost per frame
        static FILE* out = std::fopen(pf, "w");
        static size_t named = 0;
        if (out) {
            const game::ViewScriptProfile p = viewScripts_.profile();
            for (; named < p.handlers.size(); ++named) std::fprintf(out, "h %zu %s\n", named, p.handlers[named].name.c_str());
            std::fprintf(out, "f %u %lld", unsigned(sim_->tick()), (long long)p.frameLast);
            for (auto& h : p.handlers) std::fprintf(out, " %lld", h.disabled ? -1LL : (long long)h.last);
            std::fputc('\n', out); std::fflush(out);
        }
    }
    canvas_ = viewScripts_.takeDraws();   // view.draw: drawn after the overlays, under the UI
    groundCanvas_ = viewScripts_.takeGroundDraws();   // view.draw_ground: in the 3D scene
    if (game::ViewCameraRequest cr; viewScripts_.takeCameraRequest(cr)) {   // view.set_camera: presentation only
        if (std::isfinite(cr.x)) cam_.target.x = cr.x;
        if (std::isfinite(cr.z)) cam_.target.z = cr.z;
        if (std::isfinite(cr.pitch)) cam_.pitch = cr.pitch / 57.29578f;
        if (std::isfinite(cr.yaw)) cam_.yaw = cr.yaw / 57.29578f;
        if (std::isfinite(cr.zoom)) {
            const float d = cam_.ortho ? cr.zoom * 0.5f / std::tan(cam_.fovY * 0.5f) : cr.zoom;
            cam_.minDistance = std::min(cam_.minDistance, d); cam_.maxDistance = std::max(cam_.maxDistance, d);
            cam_.distance = d;
        }
    }
    issueViewCommands();
    scriptUiFrame();
    for (auto& r : viewScripts_.takeRequests()) {
        if (r.kind == game::ViewRequest::Notice) notice(r.text, r.duration, r.style);
        else if (r.kind == game::ViewRequest::Effect) fx_.playEffect(r.text, r.x, r.y, r.z);
        else if (r.kind == game::ViewRequest::ShakeCamera)
            cameraShake_.start(r.shake, float(r.shake["x"].toDouble(cam_.target.x)), float(r.shake["z"].toDouble(cam_.target.z)));
        else if (r.kind != game::ViewRequest::Sound) fx_.scriptEffect(r);
        else {
            audio::PlayParams pp;
            pp.positional = r.positioned; pp.x = r.x; pp.z = r.z;
            audio_.play(r.text, pp);
        }
    }
}

bool GameClient::hostCanControl() const {
    return !mapLoading_ && sim_ && !netMatch_ && !city_ &&
        (screen_ == Screen::Playing || screen_ == Screen::Paused);
}
bool GameClient::hostPaused() const {
    return hostCanControl() && (hostPaused_ || screen_ == Screen::Paused || topDownPaused());
}
bool GameClient::hostCanStep() const {
    return hostCanControl() && hostPaused_ && screen_ == Screen::Playing && !topDownPaused();
}
bool GameClient::hostCheckpoint(bool restore) {
    if(!hostCanStep())return false;
    if(!restore){
        auto bytes=sim_->serialize();
        if(bytes.empty()||bytes.size()>64u*1024u*1024u)return false;
        hostCheckpoint_=std::move(bytes);return true;
    }
    if(hostCheckpoint_.empty())return false;
    std::string error;
    if(!sim_->deserialize(hostCheckpoint_,&error))return false;
    hostStep_=false;
    return true;
}
double GameClient::simulationSpeed() const {
    static const double defaultSimSpeed = [] { const char* v = std::getenv("AIVERSE_SIM_SPEED"); return v ? std::clamp(std::atof(v), 0.1, 16.0) : 1.0; }();
    return hostCanControl()&&hostSpeed_>0?hostSpeed_:defaultSimSpeed;
}
bool GameClient::setHostSpeed(double speed) {
    if(!hostCanControl()||!(speed>=0.25&&speed<=100))return false;
    hostSpeed_=speed;return true;
}
bool GameClient::setHostPaused(bool paused) {
    if (!hostCanControl()) return false;
    hostPaused_ = paused; hostStep_ = false;
    if (!paused && screen_ == Screen::Paused) setScreen(Screen::Playing);
    return true;
}
bool GameClient::requestHostStep() {
    if (!hostCanStep()) return false;
    hostStep_ = true;
    return true;
}

namespace {
// AIVERSE_CLIENT_PROFILE=1: average time of the client frame stages, printed every 120 frames
// ("[client-stages] ..."): sim, scene, ui (update), view (GameView::draw), fx, render (scene + PRISMA),
// hud (overlays, RmlUi), other. Used for the Android benchmarks (tools/perf/android-bench.sh).
struct ClientStageProfile {
    static constexpr int kStages = 8;
    const char* names[kStages] = {"sim", "scene", "ui", "view", "fx", "render", "hud", "other"};
    double total[kStages] = {};
    int frames = 0;
    bool on = false, checked = false;   // read on the first frame: Android sets the env from adb after load
    std::chrono::steady_clock::time_point last;
    void start() {
        if (!checked) { checked = true; const char* e = std::getenv("AIVERSE_CLIENT_PROFILE"); on = e && *e == '1'; }
        if (on) last = std::chrono::steady_clock::now();
    }
    void lap(int stage) {
        if (!on) return;
        const auto now = std::chrono::steady_clock::now();
        total[stage] += std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
    }
    void endFrame() {
        if (!on) return;
        lap(kStages - 1);
        if (++frames < 120) return;
        char line[512]; int n = std::snprintf(line, sizeof line, "[client-stages] avg/%d", frames);
        double sum = 0;
        for (int i = 0; i < kStages; ++i) { n += std::snprintf(line + n, sizeof line - size_t(n), " %s=%.2f", names[i], total[i] / frames); sum += total[i]; total[i] = 0; }
        std::snprintf(line + n, sizeof line - size_t(n), " total=%.2f ms", sum / frames);
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, "AIVERSE", "%s", line);
#else
        std::fprintf(stderr, "%s\n", line);
#endif
        frames = 0;
    }
} g_stages;
} // namespace

std::string GameClient::cursorState() {
    if (ui_.pointerOverUi()) {
        const auto name = ui_.pointerCursor();
        return name.empty() ? "normal" : name;
    }
    if (!sim_ || screen_ != Screen::Playing) return "normal";
    if (cursorEdge_.x != 0 || cursorEdge_.y != 0) {
        std::string state = "edge";
        if (cursorEdge_.y != 0) state += cursorEdge_.y > 0 ? "North" : "South";
        if (cursorEdge_.x != 0) state += cursorEdge_.x > 0 ? "East" : "West";
        return state;
    }
    auto& w = sim_->world();
    const Entity hovered = pick(mouseX_, mouseY_);
    const Unit* target = w.alive(hovered) ? w.tryGet<Unit>(hovered) : nullptr;
    if (mode_ == Mode::Place && placeType_ >= 0) {
        bool onMap = false;
        const Vec3 centre = placeCentre(mouseX_, mouseY_, onMap);
        return onMap && mod().canPlace(*sim_, human_, uint16_t(placeType_), centre) ? "place" : "invalid";
    }
    if (mode_ == Mode::AttackMove || mode_ == Mode::Ability || mode_ == Mode::Skill || entityPointer_) {
        float3 ground;
        if (!groundAt(mouseX_, mouseY_, ground)) return "invalid";
        TargetKind kind = TargetKind::Point;
        if (mode_ == Mode::Skill && abilityIndex_ >= 0 && size_t(abilityIndex_) < rules_.playerAbilities.size())
            kind = mod().playerAbilityFor(*sim_, human_, abilityIndex_).target;
        if (mode_ == Mode::Ability) {
            for (Entity selected : selection_) {
                auto* unit = w.alive(selected) ? w.tryGet<Unit>(selected) : nullptr;
                if (!unit || abilityIndex_ < 0 || unit->type >= rules_.types.size()) continue;
                const auto& abilities = rules_.types[unit->type].abilities;
                if (size_t(abilityIndex_) < abilities.size()) { kind = mod().abilityFor(*sim_, selected, abilityIndex_).target; break; }
            }
        }
        if ((kind == TargetKind::Unit || kind == TargetKind::Structure) && !target) return "invalid";
        if (kind == TargetKind::Structure && (target->type >= rules_.types.size() || !rules_.types[target->type].structure)) return "invalid";
        return mode_ == Mode::AttackMove ? "attack" : "ability";
    }
    if (target) if (const auto* owner = w.tryGet<Owner>(hovered)) {
        if (mod().hostile(*sim_, human_, owner->player)) return "enemy";
        if (sim_->allied(human_, owner->player)) return "ally";
    }
    return "normal";
}

void GameClient::frame(gfx::Device& dev, double dt) {
    if (campaignCamera_ && (screen_ != Screen::Playing || !possessedUnit().valid() || !rules_.possession.firstPerson)) {
        cam_ = *campaignCamera_;
        campaignCamera_.reset();
        platform::setPointerLock(false);
    }
    cursorEdge_ = {};
    g_stages.start();
    lensCameras_.collectRetiredTargets();
    const bool hostFrozen = hostCanControl() && hostPaused_;
    const bool singleStep = hostFrozen && hostCanStep() && hostStep_;
    hostStep_ = false;
    if (hostFrozen) dt = 0;
    // Even on menus/loading screens there may be previous per-view motion
    // textures awaiting the GPU completion fence from an old match.
    prisma_.collectGpuRetirements();
    r3d_.collectCompletedObjectMotion(dev.gpuCompletedFence());
    // WorldScene unloads can retain a Model after its last resident cell
    // leaves; only the real Device owner frame hook can enqueue a final GPU
    // retirement token, never a streaming worker or View draw callback.
    if(worldScene_ && worldScene_->pendingGpuRetirements()!=0)
        (void)worldScene_->collectRetired(dev);
    // Previous-frame model uploads can remain GPU-visible after their
    // TypeLooks detach. Drain only before this frame's render submissions;
    // if a backend refuses the idle barrier, retain them for the next frame.
    if(view_.pendingModelRetirements()!=0 ||
       view_.pendingRolledBackImages()!=0)
        view_.collectRetiredModels();
    if(view_.pendingPaintGpuRetirements()!=0)
        view_.collectRetiredPaintGpuResources();
    if(r3d_.pendingProgramRetirements()!=0)
        r3d_.collectRetiredPrograms();
    if(r3d_.pendingTargetRetirements()!=0)
        r3d_.collectRetiredTargets();
    if(r3d_.hasPendingModelPipelineJobs())
        r3d_.pollPendingModelPipelineJobs();
    if(inMatch()&&view_.automaticGraphImageCollectionEnabled()&&!runtimeTextureTask_){
        const auto graphState=runtimeTextureGraph_.status();
        if(!graphState.getBool("busy")&&!graphState.getBool("gpuTextureSetReady")&&view_.collectAutomaticGraphImages())
            runtimeTextureGraph_.invalidateOutputReuseReceipt();
    }
    tickRuntimeTextureMaterial(dev);
    const auto frameStarted = std::chrono::steady_clock::now();
    if (mapLoading_) {
        horizonFrameTiming_.discard();
        // This frame does not render camera 3: retire stale motion/exposure
        // history from the previous scene using the GPU submission token.
        prisma_.retireUnregisteredViews({});
        r3d_.retireUnregisteredObjectMotionViews({});
        prisma_.retireUnreferencedSizeCaches();
        frameMapLoading(dev, dt);
        return;
    }
    if (!worldScene_ || !sim_ || planet()) horizonFrameTiming_.discard();
    if (mapJustLoaded_) { dt = 0; mapJustLoaded_ = false; }
    if (sim_ && screen_ == Screen::Playing) updateBackgroundLoading();
    const double backgroundMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStarted).count();
    double worldMs = 0;
    time_ += dt;
    updateGamepad(float(dt));   // pad input as mouse / key events + camera pan (ClientGamepad.cpp)
    inputShortcuts_.setTopDownEnabled(screen_==Screen::Playing && topDownActive());
    inputShortcuts_.setPlanetEnabled(screen_==Screen::Playing && planet());
    inputShortcuts_.setPossessionEnabled(screen_==Screen::Playing && possessedUnit().valid());
    const bool keyboardAllowed=sim_ && screen_==Screen::Playing &&
                                !howtoOpen_ && !ui_.textInputFocused();
    if(!keyboardAllowed) {
        inputShortcuts_.reset();
        // No key-up is guaranteed when a modal or text field takes focus.
        // Keep the legacy fallback mirror in sync with InputFlow cancellation.
        std::fill_n(keys_,int(platform::Key::Count),false);
    } else {
        const auto timed=inputShortcuts_.advance(float(dt));
        if(topDownActive())topDownTimedActions(timed);
        else if(!regionSwipe_.enabled && !planet())applyRtsActions(timed);
    }
    float alpha = 0;
    bool netAdvanced = false;
    updateNet(dt, netAdvanced);   // online: the network session drives the sim, even behind the pause menu
    if (netAdvanced && sim_) {
        alpha = float(sim_->alpha());
        processEvents();
        if (sim_->result() != 0 && screen_ != Screen::Over) setScreen(Screen::Over);
    }
    if (sim_ && screen_ == Screen::Playing) {
        // keyboard / edge scrolling
        float speed = cam_.distance * 0.9f * float(dt);
        float fx = std::sin(cam_.yaw), fz = std::cos(cam_.yaw);
        float3 fwd{-fx, 0, -fz}, right{fz, 0, -fx};
        const auto* arrow=inputShortcuts_.system().state("CameraPan");
        const auto* idle=inputShortcuts_.system().state("IdleCameraPan");
        const float arrowX=arrow?arrow->value.x:0.0f,arrowY=arrow?arrow->value.y:0.0f;
        const float idleX=idle?idle->value.x:0.0f,idleY=idle?idle->value.y:0.0f;
        float W = platform::windowWidth(), H = platform::windowHeight();
        // edge scrolling only while the cursor is inside the window (macOS reports moves outside it too)
        const bool panAllowed = !ui_.textInputFocused() && !possessedUnit().valid() && !topDownActive();
        const bool edges = panAllowed && !regionSwipe_.enabled && !platform::isTouchDevice() &&
                           std::string(platform::name()) != "web";
        const auto edge = edgePointer_.sample(edgeScroll_, mouseX_, mouseY_, W, H, float(dt), edges);
        cursorEdge_ = edge;
        if (panAllowed) {
            const bool noSel = selection_.empty();
            auto combined = [](float controls, float pointer) {
                const float limit = std::max(1.f, std::abs(pointer));
                return std::clamp(controls + pointer, -limit, limit);
            };
            const float panY = combined(arrowY + (noSel ? idleY : 0.f) - padPanY_, edge.y);
            const float panX = combined(arrowX + (noSel ? idleX : 0.f) + padPanX_, edge.x);
            cam_.target += fwd * (speed * panY);
            cam_.target += right * (speed * panX);
        }
        auto& g = mod().state(*sim_).grid;
        if (planet()) planetCameraKeys(dt);
        else {
        cam_.target.x = std::clamp(cam_.target.x, 0.0f, float(g.w));
        cam_.target.z = std::clamp(cam_.target.z, 0.0f, float(g.h));
        }
        if (aiverse::Vec3 sc{}; !planet() && mod().screenView(*sim_, sc, screenSize_)) {
            // map "screen" / map scroller: the camera follows the centre and frames the screen
            const float smooth = mod().screen().enabled ? float(mod().screen().smooth.toDouble()) : 10.0f;
            const float k = screenFramed_ ? std::min(1.0f, float(dt) * smooth) : 1.0f;   // the first frame jumps
            cam_.target.x += (float(sc.x.toDouble()) - cam_.target.x) * k;
            cam_.target.z += (float(sc.z.toDouble()) - cam_.target.z) * k;
            const float sw = float(screenSize_.x.toDouble()), sh = float(screenSize_.y.toDouble());
            const float t = std::tan(cam_.fovY * 0.5f), aspect = cam_.projectionAspect(H > 0 ? W / H : 1.0f);
            const float s = std::max(0.2f, std::sin(cam_.pitch));
            const float d = std::max(sh * s / (2 * t), sw / (2 * t * std::max(aspect, 0.1f)));   // whole screen visible
            cam_.minDistance = std::min(cam_.minDistance, d); cam_.maxDistance = std::max(cam_.maxDistance, d);
            cam_.distance += (d - cam_.distance) * k;
            screenFramed_ = true;
        }
        if (quality_.update(dt)) { // too slow for these tiers: step down (keeps the pause menu in sync)
            r3d_.setShadowQuality(ShadowQuality(quality_.shadows()));
            r3d_.lights.setQuality(aiverse::render::LightQuality(quality_.lights()));
            shadowQuality_ = boundShadowQuality_ = quality_.shadows();
            // post effects follow: one tier down per governor step, but keep the cheap look (low)
            if (!hostPostQuality_ && prismaBase_ != render::PrismaQuality::Off)
                prisma_.setQuality(render::PrismaQuality(std::max(1, int(prismaBase_) - quality_.stepsDown())));
        }

        if (!playback_ && (!hostFrozen || singleStep)) {
            steerPossessed();
            topDownFrame(singleStep ? 1.0 / sim_->tickRate() : dt);
        }
        if (city_) alpha = cityFrame(dt);   // the city runs on a server: nothing is stepped here
        else if (playback_) {
            if (!playbackPaused_ && sim_->tick() < playback_->endTick) {
                const auto remaining = playback_->endTick - sim_->tick();
                sim_->advance(dt * playbackSpeed_,int(std::min<uint32_t>(remaining,64)));
                alpha = float(sim_->alpha());
                processEvents();
            }
            if (!playbackPaused_ && sim_->tick() == playback_->endTick) {
                playbackPaused_ = true;
                if (sim_->checksum() != playback_->finalChecksum) error_ = "Replay checksum mismatch";
                setScreen(Screen::Over);
            }
        }
        else if (!netMatch_ && !topDownPaused() && (!hostFrozen || singleStep)) {   // single player: the escape preview pauses the match
            for (auto& b : bots_) b.update(*sim_);
            const double simSpeed = simulationSpeed();
            if (singleStep) sim_->step();
            else if (simSpeed == 1.0) sim_->advance(dt);
            else sim_->advance(dt * simSpeed, int(8 * simSpeed + .5));   // testing: match time faster than real time
            alpha = float(sim_->alpha());
            processEvents();
            if (sim_->result() != 0) {
                setScreen(Screen::Over);
                auto* me = sim_->player(human_);
                if (!spectator_) sound_.onMatchEnd(me && me->team == sim_->result());
            }
        }
    }
    g_stages.lap(0);   // sim (and everything before it)
    if (sim_) {
        sceneFromSim(*sim_, scene_, alpha, spectator_ ? 0 : human_);
        scene_.paintLayers = viewScripts_.paintLayers();
        scene_.entityFx = viewScripts_.entityFx();
        if (const auto* unit = sim_->world().tryGetConst<Unit>(hover_); unit && !rules_.types[unit->type].selectable)
            hover_ = ui_.pointerOverUi() ? kNullEntity : pick(mouseX_, mouseY_);
        cleanSelection();
        for (auto& o : scene_.objects) {
            o.selected = std::find(selection_.begin(), selection_.end(), o.entity) != selection_.end();
            o.hovered = o.entity == hover_;
        }
        scene_.moveMarkers.clear();
        for (auto& [p, until] : markers_) if (until > time_) scene_.moveMarkers.push_back(p);
        markers_.erase(std::remove_if(markers_.begin(), markers_.end(), [&](auto& m) { return m.second < time_; }), markers_.end());
        if (screen_ == Screen::Playing) { topDownPresent(dt, alpha); topDownCamera(dt); }
    }

    g_stages.lap(1);   // scene
    if (sim_ && ensureLocalAssets()) {
        // Asset-loading output has no active 3D presentation camera.
        prisma_.retireUnregisteredViews({});
        r3d_.retireUnregisteredObjectMotionViews({});
        prisma_.retireUnreferencedSizeCaches();
        frameMapLoading(dev, dt);
        return;
    }

    luaTick(dt);   // game.on("update", fn) in UI scripts
    updateAudio(dt);
    overlay_.newFrame(dt);
    drawGamepadCursor();
    regionSwipe_.configure(manifest_);
    if (sim_ && regionSwipe_.enabled) {
        if(sim_->tick()<regionTick_){regionSwipe_.ratio=loadRegionRatio();regionSwipe_.cancel();regionSwipe_.arrows.clear();}
        regionSwipe_.refresh(*sim_,mod(),human_);
        regionSwipe_.fit(cam_,float(platform::windowWidth()),float(platform::windowHeight()),sim_->tick()<regionTick_);
        regionTick_=sim_->tick();
    }
    updateUi();
    renderSelectionPortrait();
    spine_.render(float(dt));   // Spine views into their textures (outside the main pass)
    g_stages.lap(2);   // ui update
    gfx::PassDesc pass;
    pass.colorTarget = hostColor_;
    pass.depthTarget = hostDepth_;
    pass.clearColor[0] = 0.55f; pass.clearColor[1] = 0.66f; pass.clearColor[2] = 0.78f;
    if (static const bool flat = [] { const char* e = std::getenv("AIVERSE_GEOMETRY_ONLY"); return e && *e && *e != '0'; }(); flat) {
        pass.clearColor[0] = 0.18f; pass.clearColor[1] = 0.19f; pass.clearColor[2] = 0.21f;   // geometry-only benchmark background
    }
    render::LightSystem::PreparedFrame lensLightingSnapshot;
    if (sim_) {
        if (planet()) { // orbit camera -> view-world Camera; space is black
            cam_ = view_.updatePlanetCamera(scene_.seconds, float(dt), fbH());
            pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0.0f;
        }
        // Apply view.set_camera before streaming, culling and drawing so the
        // world and view.to_screen use the same camera in this frame.
        runViewScripts(screen_ == Screen::Playing ? float(dt) : 0.0f);
        if (worldScene_ && !planet()) {   // load / unload chunks for where the camera looks (GPU uploads: before the scene pass)
            horizonFrameTiming_.begin(dev,worldScene_.get());
            const Vec3 focus{Fixed::fromInt(int64_t(cam_.target.x)), Fixed{}, Fixed::fromInt(int64_t(cam_.target.z))};
            const auto worldStarted=std::chrono::steady_clock::now();
            worldScene_->update(dev, {&focus, 1}, worldRadius_);
            worldMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-worldStarted).count();
        }
        prisma_.setViewId(3ULL);
        prisma_.applyCameraVolumes(prismaVolumes_, cam_.eye());
        // The running game has priority over auxiliary editor previews.
        // Up to 256 MiB for two camera-owned output HDR histories.
        prisma_.setViewQualityPolicy(3ULL,{224,256});
        const auto viewAssets=view_.ownedPresentationGpuMemoryStats(dev);
        const auto fxAssets=fx_.ownedGpuMemoryStats();
        const auto streamed=worldScene_
            ? worldScene_->ownedGpuMemoryStats(dev) : render::GpuOwnedMemoryStats{};
        prisma_.setExternalPresentationAssetsGpuMemory(
            render::GpuOwnedMemoryCounter::saturatingAdd(
                render::GpuOwnedMemoryCounter::saturatingAdd(
                    viewAssets.knownBytes(),fxAssets.knownBytes()),
                streamed.knownBytes()));
        const auto rendererMemory=r3d_.residentMemoryStats();
        prisma_.setExternalRendererGpuMemory(
            rendererMemory.liveTextureKnownBytes+
            rendererMemory.liveBufferKnownBytes,
            rendererMemory.pendingMotionKnownBytes);
        prisma_.setExternalGeometryPoolGpuMemory(
            render::geometryPoolStats(dev).allocatedBytes);
        // Presentation-only Halton jitter: gameplay aiming, viewport picking,
        // UI and input continue to use the original unjittered cam_.
        const auto [renderW,renderH]=prisma_.renderSizeForOutput(fbW(),fbH());
        // AIVERSE_CAM_FLY="x,z,dist,pitch,yaw>x,z,dist,pitch,yaw@frames":
        // benchmark fly-through. The camera moves linearly from the first
        // pose to the second over the given number of rendered frames (by
        // frame count, so every device sees the same path), then holds.
        if (static const char* fly = std::getenv("AIVERSE_CAM_FLY"); fly && *fly) {
            static float a[5], b[5]; static int frames = 0, frame = 0;
            static const bool ok = std::sscanf(fly, "%f,%f,%f,%f,%f>%f,%f,%f,%f,%f@%d",
                &a[0], &a[1], &a[2], &a[3], &a[4], &b[0], &b[1], &b[2], &b[3], &b[4], &frames) == 11 && frames > 0;
            if (ok && sim_ && !mapLoading_) {
                const float t = std::min(1.0f, float(frame++) / float(frames));
                float v[5];
                for (int k = 0; k < 5; ++k) v[k] = a[k] + (b[k] - a[k]) * t;
                cam_.target = {v[0], 0, v[1]};
                cam_.minDistance = std::min(cam_.minDistance, v[2]);
                cam_.distance = v[2];
                cam_.pitch = v[3] / 57.2958f;
                cam_.yaw = v[4] / 57.2958f;
            }
        }
        const auto shakenCam = cameraShake_.sample(cam_, screen_ == Screen::Playing || screen_ == Screen::Over ? dt : 0.);
        const render::Camera renderCam = prisma_.jitteredCamera(shakenCam,renderW,renderH);
        // SpectraFX GPU simulation is encoded before the scene pass.
        fx_.setPlanetView(planet() ? &view_ : nullptr);
        const auto graphViewProj=renderCam.proj(float(renderW)/float(std::max(1,int(renderH))),
                                              dev.info().clipDepthZeroToOne)*renderCam.view();
        fx_.prepareGraphGpuFrame(*sim_,scene_,renderCam,graphViewProj,int(renderW),int(renderH),
            screen_ == Screen::Playing || screen_ == Screen::Over ? float(dt) : 0.f);
        fx_.prepareGpuFrame(screen_ == Screen::Playing || screen_ == Screen::Over
                                ? float(std::clamp(dt,0.0,0.1)) : 0.0f,shakenCam);
        if (view_.strataDebugInset() && !mapLoading_) {
            if (!insetR3d_ || insetW_ != int(renderW) || insetH_ != int(renderH)) {
                if (insetColor_.valid()) dev.destroyTexture(insetColor_);
                if (insetDepth_.valid()) dev.destroyTexture(insetDepth_);
                insetColor_ = insetDepth_ = {};
                if (!insetR3d_) {
                    insetR3d_ = std::make_unique<render::Renderer3D>();
                    if (insetR3d_->init(dev)) insetR3d_->setShadowQuality(render::ShadowQuality::Off);
                    else insetR3d_.reset();
                }
                gfx::TextureDesc td;
                td.width = int(renderW); td.height = int(renderH);
                td.renderTarget = true;
                td.label = "strata debug inset";
                insetColor_ = dev.createTexture(td);
                td.format = gfx::PixelFormat::Depth;
                td.filter = gfx::Filter::Nearest;
                insetDepth_ = dev.createTexture(td);
                insetW_ = int(renderW); insetH_ = int(renderH);
            }
            if (insetR3d_ && insetColor_.valid() && insetDepth_.valid()) {
                insetR3d_->lighting = r3d_.lighting;
                insetR3d_->lighting.fogDensity = 0;
                insetR3d_->beginScene(renderCam, int(renderW), int(renderH));
                view_.drawGeometryInset(*insetR3d_);
                gfx::PassDesc ip;
                ip.colorTarget = insetColor_;
                ip.depthTarget = insetDepth_;
                ip.clearColor[0] = 0.18f; ip.clearColor[1] = 0.19f; ip.clearColor[2] = 0.21f; ip.clearColor[3] = 1.f;
                insetR3d_->render(ip);
                dev.endPass();
                view_.setStrataInsetTexture(insetColor_.id, dev.info().renderTargetFlipY);
            }
        }
        // Game and Editor Play use the SAME GameClient visual cave owner.
        // GPU preparation occurs outside the renderer pass and never runs
        // from a deterministic Simulation tick or worker thread.
        if(cavernsVisual_ && !planet()) {
            const auto eyecm=terraweave::caverns::PointCm{
                int64_t(std::llround(double(cam_.target.x)*100.)),
                cavernsVisual_->suggestedFocusYcm(),
                int64_t(std::llround(double(cam_.target.z)*100.))};
            try {cavernsVisual_->beforeRender(dev,eyecm);}
            catch(const std::exception& e) {
                std::fprintf(stderr,"[caverns] visual streaming: %s\n",e.what());
            }
        }
        r3d_.beginScene(renderCam,renderW,renderH);
        if (worldScene_ && !planet()) worldScene_->draw(r3d_, world::Cell{0, 0, 0}, 1);
        if (cavernsVisual_ && !planet()) cavernsVisual_->draw(r3d_);
        auto& gs = mod().state(*sim_);
        {   // input state for the overlays (placement footprint / build grids, held keys, modes, cursor)
            static const char* kModes[] = {"normal", "attack", "patrol", "place", "rally", "ability", "claim"};
            OverlayContext oc;
            oc.sim = sim_.get();
            oc.pings = &pendingPings_;
            oc.orders = &pendingOrders_;
            oc.player = spectator_ ? 0 : human_;
            float3 g;
            if (groundAt(mouseX_, mouseY_, g)) { oc.hasCursor = true; oc.cursor = g; }
            if (mode_ == Mode::Place && placeType_ >= 0) {
                bool ok;
                Vec3 c = placeCentre(mouseX_, mouseY_, ok);
                if (ok) {
                    oc.placeType = placeType_;
                    oc.placeAt = {float(c.x.toDouble()), 0, float(c.z.toDouble())};
                    oc.placeOk = mod().canPlace(*sim_, human_, uint16_t(placeType_), c);
                }
            }
            oc.keys = keys_; oc.keyCount = int(platform::Key::Count);
            oc.mode = kModes[int(mode_)];
            oc.paused = screen_ == Screen::Paused;
            oc.over = screen_ == Screen::Over;
            view_.setOverlayContext(oc);
        }
        render::CameraModelSnapshot::CaptureGuard lensCaptureGuard;
        // Automatically clear the borrowed GameView capture pointer if
        // any rendering/animation callback throws before the normal end.
        struct PreparedCaptureReset {
            game::GameView* view=nullptr;
            ~PreparedCaptureReset() {
                if(view)view->setCameraPreparedCapture(nullptr);
            }
        } preparedCaptureReset{lensCamerasActive_?&view_:nullptr};
        if(lensCamerasActive_) {
            lensCaptureGuard.begin(lensModelSnapshot_,r3d_);
            view_.setCameraPreparedCapture(&lensModelSnapshot_);
        }
        g_stages.lap(7);
        // AIVERSE_GEOMETRY_ONLY=1: clean geometry benchmark (STRATA/LOD): only
        // the static geometry scene on a flat grey background; no terrain,
        // water, sky gradient, fog, HUD; sun shadows only with
        // AIVERSE_GEOMETRY_SHADOWS=1.
        static const bool geometryOnly = [] { const char* e = std::getenv("AIVERSE_GEOMETRY_ONLY"); return e && *e && *e != '0'; }();
        static const bool geometryShadows = [] { const char* e = std::getenv("AIVERSE_GEOMETRY_SHADOWS"); return e && *e && *e != '0'; }();
        if (geometryOnly) {
            view_.setGeometryOnly(true);
            quality_.lock(); // fixed tiers: the adaptive governor would change what is measured
            r3d_.setProfiling(true); // triangle counters for the corner panel / [strata] log
            static bool shadowsSet = false;
            if (!shadowsSet && !geometryShadows) { r3d_.setShadowQuality(render::ShadowQuality::Off); shadowsSet = true; }
        }
        view_.draw(r3d_, scene_, time_, spectator_ ? nullptr : &gs.vision[human_], gs.grid);
        pendingPings_.clear();
        pendingOrders_.clear();
        g_stages.lap(3);   // view
        for (auto& fe : view_.fieldView().takeEffects()) fx_.playEffect(fe.name, fe.pos.x, fe.pos.y + 0.2f, fe.pos.z);   // grid field particles
        natureUpdate(screen_ == Screen::Playing || screen_ == Screen::Over ? float(dt) : 0.0f);
        fx_.update(*sim_, scene_, shakenCam, r3d_.viewProj(),renderW,renderH,
                   screen_ == Screen::Playing || screen_ == Screen::Over ? float(dt) : 0.0f);
        fx_.drawModels(r3d_);
        fx_.drawChainBeams(r3d_, shakenCam);
        fx_.addLights(r3d_.lights);   // particle lights ("light" module)
        if (auto* enc = mod().encounters(); enc && enc->active()) {   // telegraph decals + status rings (EncounterDecals.h)
            auto ground = groundCanvas_;
            game::appendEncounterDecals(*sim_, mod(), view_, spectator_ ? 0 : human_, ground);
            view_.drawGround(r3d_, ground);
        } else view_.drawGround(r3d_, groundCanvas_);
        if (mode_ == Mode::Place && placeType_ >= 0) { // ghost of the building to place (its footprint is the "placement" overlay)
            bool ok;
            Vec3 c = placeCentre(mouseX_, mouseY_, ok);
            if (ok) {
                bool can = mod().canPlace(*sim_, human_, uint16_t(placeType_), c);
                view_.drawGhost(r3d_, uint16_t(placeType_), {float(c.x.toDouble()), 0, float(c.z.toDouble())}, can, mod().state(*sim_).grid, time_);
            }
        }
        drawToolPreview();
        lensCaptureGuard.end();
        if(preparedCaptureReset.view) {
            preparedCaptureReset.view->setCameraPreparedCapture(nullptr);
            preparedCaptureReset.view=nullptr;
        }
        const float fogBase = r3d_.lighting.fogDensity;   // a narrow top-down lens: fog as a default lens framing it sees
        if (screen_ == Screen::Playing || screen_ == Screen::Paused || screen_ == Screen::Over)
            if (topDownActive()) r3d_.lighting.fogDensity = fogBase * topDown_.fogScale();
        if(lensCamerasActive_) lensLightingSnapshot=r3d_.lights.capturePrepared();
        g_stages.lap(4);   // fx, scripts, ground, ghosts
        if (geometryOnly) {
            r3d_.lighting.skyColor = r3d_.lighting.fogColor = {0.18f, 0.19f, 0.21f};
            r3d_.lighting.fogDensity = 0;
        }
        fx_.contributeVolumes(prisma_);
        r3d_.render(pass, &prisma_);
        view_.noteRenderedFrame(r3d_.profile().cameraModelTriangles, r3d_.profile().triangles[0], r3d_.drawCalls());
        fx_.render(r3d_.lighting);
        // GPU billboards share this scene depth/pass; no separate pass and no
        // duplicate CPU draw of GPU-owned hybrid emitter instances.
        fx_.renderGpu(r3d_.viewProj(),shakenCam);
        // PRISMA: current-frame particle alpha/depth coverage for MotionBlur.
        // Callback runs only when the HDR motion/reactive pass is active;
        // it reuses particle geometry already submitted by fx_.render().
        if (fx_.hasReactiveGeometry())
            r3d_.addMotionReactiveDraw([this](gfx::Device& gpu,
                    gfx::TextureHandle sceneDepth, int width, int height) {
                fx_.renderReactive(gpu,sceneDepth,width,height);
            });
        r3d_.lighting.fogDensity = fogBase;
        r3d_.resolvePrisma();   // post effects (if any) under the HUD
        g_stages.lap(5);   // render
        prisma_.clearCameraVolumes();
        if (!geometryOnly) {
            view_.drawOverlays(shakenCam, scene_, platform::windowWidth(), platform::windowHeight());
            view_.drawCanvas(canvas_);
        }
        if (static const bool hud = geometryOnly || std::getenv("AIVERSE_STRATA_DEBUG"); hud)
            view_.drawStrataHud(platform::windowWidth(), platform::windowHeight());
        topDownDraw();
        // races (rules "race" + map "race", docs/races.md): the same HUD as every other client
        if (auto* drv = mod().driving(); drv && drv->racing(*sim_))
            ui::drawRaceHud(loc_, drv->raceView(*sim_, human_), sim_->tick(), sim_->tickRate());
    } else {
        pass.clearColor[0] = 0.10f; pass.clearColor[1] = 0.12f; pass.clearColor[2] = 0.14f;
        dev.beginPass(pass);
    }
    overlay_.render();
    ui_.setTarget(hostColor_, hostDepth_);   // linear UI blending reopens this pass
    static const bool hideUi = [] { const char* e = std::getenv("AIVERSE_GEOMETRY_ONLY"); return e && *e && *e != '0'; }();
    if (!hideUi || !sim_) ui_.render();
    if (cursors_.enabled() && !platform::isTouchDevice())
        cursors_.show(platform::pointerLocked() ? std::string{} : cursorState(), time_);
    dev.endPass();
    g_stages.lap(6);   // hud
    g_stages.endFrame();
    // A separate offscreen path after the already presented scene/UI pass.
    // Never replay GameView::draw: its animation, event and appearance updates
    // are stateful, even with MotionWeave's universal View adapter.
    if(lensCamerasActive_ && sim_ && !mapLoading_ && dev.gpuHealthy() &&
       view_.visualFrameReadyFor(mod().state(*sim_).grid,time_)) {
        // Use the actual multi-owner committed epoch, never an unrelated
        // client counter. Failed primary updates cannot be replayed by RT.
        const auto published=view_.publishedVisualFrame();
        const render::CameraVisualFrameId visualId{
            uint64_t(sim_->tick()),published.id,time_};
        const auto result=lensCameras_.renderPrepared(
            lensModelSnapshot_,visualId,float(dt),
            [&](const render::CameraModelSnapshot& snapshot,
                const render::EcsCameraView& camera,render::Renderer3D& renderer){
                if(worldScene_ && !planet())
                    worldScene_->draw(renderer,world::Cell{0,0,0},1);
                if(cavernsVisual_ && !planet())cavernsVisual_->draw(renderer);
                // Separate per-camera terrain culling and view projection,
                // but reuse the primary frame's already uploaded GPU state.
                if(!planet()) {
                    auto& gs=mod().state(*sim_);
                    view_.drawPreparedTerrainFog(renderer,gs.grid,
                        spectator_?nullptr:&gs.vision[human_],time_,published.id);
                    view_.drawPreparedSprites(renderer,published.id);
                }
                // Renderer3D::beginScene reset the per-camera light list.
                // Reuse primary-frame particle lights without collecting FX again.
                renderer.lights.replayPrepared(lensLightingSnapshot);
                // Re-submit final pose/material/attachment packets directly.
                // GameView::draw and MotionWeave::update are NOT repeated.
                snapshot.submit(renderer,camera.entity.packed());
            },{},[&](const render::EcsCameraView& camera,
                     render::Renderer3D& renderer,int width,int height) {
                // The already updated particle population is reprojected
                // into each camera's still-open draw pass. No FX tick/event.
                fx_.renderPrepared(camera.camera,renderer.viewProj(),
                                   width,height,renderer.lighting);
            });
        if(std::getenv("AIVERSE_LENS_LOG") && !result.diagnostics.empty())
            std::fprintf(stderr,"[lens] submitted=%zu skipped=%zu diagnostics=%zu\n",
                result.submitted,result.skipped,result.diagnostics.size());
    }
    if (!sim_) {
        // Menu-only output: no active camera view; avoid keeping the
        // previous match's private histories and size caches indefinitely.
        prisma_.retireUnregisteredViews({});
        r3d_.retireUnregisteredObjectMotionViews({});
        prisma_.retireUnreferencedSizeCaches();
    }
    // CPU includes full client callback work (not clamped simulation dt);
    // GPU completion is matched later through the tagged gfx query.
    horizonFrameTiming_.finish(worldScene_.get(),frameStarted);
    if (std::getenv("AIVERSE_STREAM_TRACE") && sim_ && !mapLoading_) {
        static auto previous = frameStarted;
        const double interval = std::chrono::duration<double,std::milli>(frameStarted-previous).count(); previous=frameStarted;
        const double cpu = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStarted).count();
        size_t missing=0;
        for(const auto& o:scene_.objects) if(!o.isProjectile && !view_.typeLoaded(o.type) && assetDistance(o)<assetRadius()-12.0f) ++missing;
        std::fprintf(stderr,"[stream-frame] tick=%u interval=%.3f cpu=%.3f x=%.3f z=%.3f pending=%zu missing=%zu stalls=%zu background=%.3f world=%.3f\n",unsigned(sim_->tick()),interval,cpu,cam_.target.x,cam_.target.z,backgroundTypes_.size(),missing,loadingStalls_,backgroundMs,worldMs);
    }
    if (!startupReported_ && !mapLoading_) {
        startupReported_ = true;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startupStarted_).count();
        std::printf("[loading] first %s frame in %.1f ms\n", sim_ ? "map" : "menu", ms);
        if (std::getenv("AIVERSE_BENCH_STARTUP")) platform::requestQuit();
    }
}

} // namespace aiverse::client
