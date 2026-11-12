// Game client state shared by the client's translation units:
//   main.cpp      match flow, camera, picking, input -> Commands
//   ClientUi.cpp  RmlUi documents (menu, HUD, command card, pause) and their data model
//   Minimap.cpp   minimap image (CPU raster -> dynamic UI texture)
//   Lobby.cpp     online play: NetClient, lobby / room screens, network matches
#pragma once
#include "UiModel.h"
#include "modules/rts/view/EntityActionControls.h"
#include "modules/rts/view/TopDownControls.h"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/EventListener.h>

#include "engine/audio/Audio.h"
#include "engine/gfx/Gfx.h"
#include "engine/i18n/GameLanguage.h"
#include "engine/net/NetClient.h"
#include "engine/platform/Platform.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/render/PostFx.h"
#include "engine/render/QualityGovernor.h"
#include "engine/render/Renderer3D.h"
#include "engine/services/Store.h"
#include "engine/sim/Simulation.h"
#include "engine/ui/UiLayer.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsLocalize.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/audio/RtsAudio.h"
#include "modules/rts/audio/AudioScriptSound.h"
#include "modules/rts/view/RtsView.h"
#include "ClientFx.h"
#include "modules/rts/view/RtsViewScript.h"
#include "modules/rts/view/RegionSwipe.h"

namespace Rml { class ElementDocument; class Element; }
typedef struct lua_State lua_State;

namespace aiverse::client {

using namespace aiverse::render;
using namespace aiverse::game;

struct MapEntry { std::string file, title; };
struct Notice { std::string text; double until; };

class Minimap {
public:
    void build(gfx::Device& dev, const GameState& gs, const Rules& rules);  // per match: terrain colours, texture
    void update(gfx::Device& dev, const GameState& gs, const Rules& rules, const RtsScene& scene, const std::vector<uint8_t>* vision,
                const float3 (&viewQuad)[4], bool haveQuad, const std::vector<OverlayMinimapMark>* marks = nullptr);
    void release(gfx::Device& dev);
    gfx::TextureHandle texture() const { return tex_; }
    int width() const { return w_; }
    int height() const { return h_; }
    float cellsPerPixel() const { return cellsPerPx_; }
    // planet maps (MinimapPlanet.cpp): a globe around the camera focus, or the system seen from above
    void buildPlanet(gfx::Device& dev, const RtsView& view);
    void updatePlanet(gfx::Device& dev, const RtsView& view, const RtsScene& scene, const Rules& rules, bool fog);
    // u, v in [0, 1] of the image -> new camera focus (globe) or focused body (system); false = outside
    bool planetPointer(RtsView& view, float u, float v) const;

private:
    // planet minimap state
    bool system_ = false;
    std::vector<uint32_t> cellColor_;       // per cell of the shown body
    std::vector<int> pixelCell_;            // per pixel: cell id, -1 = space
    std::vector<float> pixelShade_;
    render::float3 lastDir_, lastFwd_;
    int body_ = -1;
    gfx::TextureHandle tex_;
    int w_ = 0, h_ = 0, gridW_ = 0, gridH_ = 0;
    float cellsPerPx_ = 1;
    std::vector<uint32_t> base_, pixels_;
};

class GameClient {
    friend struct LuaApi;   // ClientScript.cpp: the `game` table for UI scripts
    struct ScriptUiApi;     // ClientScriptUi.cpp: scripthost.event / send for engine/ui/lib/scriptui.lua
    friend struct ScriptUiApi;
public:
    void init(gfx::Device& dev);   // every init step at once
    // Startup in steps, one per frame, so the splash screen (ClientSplash.h) keeps animating.
    static constexpr int kInitSteps = 6;
    void initStep(gfx::Device& dev, int step);
    static float initWeight(int step);   // share of the loading progress bar
    void frame(gfx::Device& dev, double dt);
    void event(const platform::Event& e);
    void shutdown();
    void autostart(size_t i) { startMatch(i); }
    void forceLanguage(std::string tag) { forcedLanguage_ = std::move(tag); }
    void spectate() { spectator_ = true; }
    void showHowto() { howtoOpen_ = true; }
    // --ui-screen name [--ui-params json] [--ui-profile json] [--ui-lua code]: show one game screen (MCP ui_preview)
    void previewScreen(std::string luaCode) { uiPreview_ = true; uiPreviewLua_ = std::move(luaCode); }   // --howto: open "How to play" over the menu

    // called from UI event listeners
    void minimapPointer(float u, float v);   // u, v in [0, 1] of the minimap image

    // Hosting (the editor's Game window, apps/editor/ClientHost.cpp): frames render into these textures
    // instead of the screen; the host also sets platform::setHostedView around every call.
    void setHostTarget(gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h) { hostColor_ = color; hostDepth_ = depth; hostW_ = w; hostH_ = h; }
    Simulation* simulation() { return sim_.get(); }
    bool inMatch() const { return sim_ != nullptr && (screen_ == Screen::Playing || screen_ == Screen::Paused || screen_ == Screen::Over); }
    const std::string& errorText() const { return error_; }

private:
    enum class Screen { Menu, Lobby, Playing, Paused, Over };
    enum class Mode { Normal, AttackMove, Patrol, Place, Rally, Ability, Claim, Skill, Dig, Undig, Room, Road, Unroad, Drop, ScriptCmd };

    void startMatch(size_t idx, const Json& initialVars = Json());
    void fitCamera();
    // planet maps (ClientPlanet.cpp): orbit camera input, picking through RtsView; flat maps never get here
    bool planet() const { return view_.planetMode(); }
    void planetCameraKeys(double dt);
    void fitPlanetCamera();
    bool planetCameraEvent(const platform::Event& e, bool uiWants);
    bool objectScreen(const SceneObject& o, float lift, float2& sp) const;   // flat maps: worldToScreen({x, lift, z})
    static Vec3 simPoint(float3 g);   // planets: picked sim-space point -> command point (1/256 units)
    RtsModule& mod() { return *static_cast<RtsModule*>(&sim_->module()); }
    bool groundAt(float sx, float sy, float3& out) const;
    Entity pick(float sx, float sy) const;
    void selectBox(float x0, float y0, float x1, float y1, bool add);
    void selectSameType(Entity e);
    void command(uint16_t type, float sx, float sy, bool queue);
    void issue(Command c) {
        c.player = human_;
        if (!c.units.empty()) sound_.onCommand(*sim_, c.units[0], c.type);
        sim_->submit(c);
    }
    void processEvents();
    void notice(const std::string& t) { notices_.push_back({t, time_ + 2.5}); if (notices_.size() > 4) notices_.erase(notices_.begin()); }
    void cleanSelection();
    bool selectionHasGarrison() const;
    Vec3 placeCentre(float sx, float sy, bool& ok);
    void setScreen(Screen s);
    void toMenu();

    // audio (main.cpp)
    void initAudio();
    void updateAudio(double dt);
    void menuMusic() { if (!audio_.scripted() && !audio_.playMusic("menu")) audio_.stopMusic(); }
    // Voices stopped when a match starts or ends: all, or only the sfx bus when the game's scripts own
    // music and UI sounds (audio.json "scripted": a click that leaves the match keeps ringing).
    uint32_t combatBuses() const { return audio_.scripted() ? 1u << unsigned(audio::Bus::Sfx) : ~0u; }
    void saveAudioSettings();

    // ClientUi.cpp
    void initUi();
    void shutdownUi();
    void updateUi();
    void buildTopBar(UiState& s);
    void buildCommandCard(UiState& s);
    // ClientScriptCmd.cpp: rules "commands" (script commands) on the card, hotkeys, targeting, view.send
    const std::vector<game::ScriptCommandDef>* scriptCommands();
    bool scriptCommandFits(const game::ScriptCommandDef& d);
    void buildScriptCommands(UiState& s);
    void startScriptCommand(int index);
    void issueScriptCommand(int index, Entity target, Vec3 point, bool havePoint);
    bool scriptCommandClick(const platform::Event& e);
    bool mapClickTap(float x, float y);   // rules "mapClick": send the tap command at a screen point (ClientScriptCmd.cpp)
    bool scriptCommandKey(platform::Key k);
    std::string scriptCommandHint();
    void issueViewCommands();
    // ClientScriptUi.cpp: view scripts' ui.* -> data model "script" and ui/hud documents (scriptui.lua)
    void initScriptUi();
    void scriptUiReset();
    void scriptUiFrame();
    int scriptCmd_ = -1;
    void buildOverlays(UiState& s);
    void loadDocuments();
    // ClientScript.cpp: Lua for UI documents and the game's ui/*.lua
    void initLua();
    void runGameScript();
    void shutdownLua();
    void luaEmit(const char* name, const Json& data);   // game.on(name, fn) handlers
    void luaTick(double dt);
    int luaUiCall(lua_State* view);   // view.ui_call: a UI Lua global called from a view script (values copied)
    const char* screenName() const;
    std::string saveDir() const;                         // storage.*: per-game folder in the user data dir
    // ClientActions.cpp: player abilities, rect tools, hand, possession, "How to play"
    void buildTools(UiState& s);
    void buildToolCard(UiState& s);
    void buildHowto(UiState& s);
    std::string toolHint() const;
    bool rectMode() const;
    Entity possessedUnit() const;
    bool cellAt(float sx, float sy, int& x, int& y) const;
    bool toolMouseDown(const platform::Event& e);
    bool toolMouseUp(const platform::Event& e);
    void steerPossessed();
    // ClientTopDown.cpp: top-down heroes (rules / game.json "topDown"): sticks, buttons, keys / mouse, follow camera
    bool topDownActive() const;
    bool topDownTouchUi() const;
    void topDownConfigure();
    bool topDownEvent(const platform::Event& e, bool uiWants);   // true: consumed
    void topDownFrame(double dt);    // input -> kCmdTopDown / kCmdTopDownAbility
    void topDownCamera(double dt);   // after the scene is built (interpolated hero position)
    void topDownDraw();              // screen canvas: sticks and buttons
    game::TopDownControls topDown_;
    bool tdMouseHeld_ = false, tdFireHeld_ = false;
    void drawToolPreview();
    // localization (presentation only: rules_ is the display copy, the sim loads its own rules)
    void applyLanguage(const std::string& tag, bool save);
    std::string tr(std::string_view key, const i18n::Args& a = {}) const { return loc_.tr(key, a); }

    // Lobby.cpp
    void netAction(const std::string& what, int arg);
    void updateNet(double dt, bool& advanced);
    void startNetMatch();
    void leaveNetMatch();
    void buildLobby(UiState& s);
    std::string mapTitle(const std::string& file, const std::string& serverTitle) const;
    void describeNetMaps();
    void loadNetSettings();
    void saveNetSettings() const;
    // disabled buttons get an empty callback: clicks on them do nothing
    int addAction(std::function<void()> fn, bool enabled = true) { nextActions_.push_back(enabled ? std::move(fn) : nullptr); return int(nextActions_.size()) - 1; }

    gfx::Device* dev_ = nullptr;
    gfx::TextureHandle hostColor_, hostDepth_;   // setHostTarget: invalid = the screen
    int hostW_ = 0, hostH_ = 0;
    int fbW() const { return hostColor_.valid() ? hostW_ : dev_->framebufferWidth(); }
    int fbH() const { return hostColor_.valid() ? hostH_ : dev_->framebufferHeight(); }
    ImGuiLayer overlay_;      // world-space overlays of RtsView (health bars) still draw through ImGui
    ui::UiLayer ui_;
    Rml::DataModelHandle model_;
    Rml::ElementDocument* menuDoc_ = nullptr;
    Rml::ElementDocument* hudDoc_ = nullptr;
    Rml::ElementDocument* lobbyDoc_ = nullptr;
    Rml::ElementDocument* howtoDoc_ = nullptr;
    lua_State* lua_ = nullptr;     // UI scripts (ClientScript.cpp); closed after the UI
    std::shared_ptr<bool> luaAlive_;             // async callbacks (store, ads) check it before calling Lua
    std::map<std::string, store::Purchase> purchases_;   // by token, until the script finishes them
    Json lastResult_;                            // last match: game.lastResult(), "matchEnd" event
    bool engineMenu_ = true;                     // false: the game's screens replace menu.rml
    bool engineMenuShown_ = true;
    bool uiPreview_ = false;                     // --ui-screen: MCP preview of one screen
    std::string uiPreviewLua_;
    std::unique_ptr<Rml::EventListener> minimapListener_;
    Rml::Element* minimapEl_ = nullptr;
    UiState ui_state_, bound_;
    std::vector<std::function<void()>> actions_, nextActions_;
    int shadowQuality_ = 0, boundShadowQuality_ = 0;
    render::QualityGovernor quality_;   // adaptive shadow/light tiers by frame time (lighting agent)
    int volume_ = 100, boundVolume_ = 100, musicVolume_ = 100, boundMusicVolume_ = 100;  // percent, pause menu
    // sound effects (every bus but music) in percent and the music / effects on-off switches (1 / 0)
    int sfxVolume_ = 100, boundSfxVolume_ = 100, musicOn_ = 1, boundMusicOn_ = 1, sfxOn_ = 1, boundSfxOn_ = 1;
    int matQuality_ = 2, boundMatQuality_ = 2; // pause menu: material tier (terrain splat / normal maps, models)
    int fxQuality_ = 0, boundFxQuality_ = 0;   // pause menu: particles off/low/medium/high
    bool uiOwnsPointer_ = false;
    bool skillDragging_ = false, skillOverWorld_ = false;
    float skillStartX_=0, skillStartY_=0;
    int64_t skillTouch_=-1;
    Minimap minimap_;

    Renderer3D r3d_;
    render::PostFx postFx_;               // post-processing: game.json / map "postfx" (engine/render/PostFx.h)
    render::PostFxQuality postFxBase_ = render::PostFxQuality::Medium;   // tier before the governor stepped down
    void configurePostFx(const Json& map);
    RtsView view_;
    ClientFx fx_;                         // particle effects (ClientFx.cpp)
    game::ViewScripts viewScripts_;       // rules "viewScripts" (docs/scripting.md): notices / effects / sounds
    std::vector<game::ViewGroundBatch> groundCanvas_;   // view.draw_ground triangles of the last view-script update
    std::vector<game::ViewDrawBatch> canvas_;   // view.draw triangles of the last view-script update
    void runViewScripts(float dt);
    audio::AudioEngine audio_;
    RtsAudio sound_;
    game::AudioScriptSound scriptSoundImpl_{audio_};   // the Lua `sound` table of view scripts and the UI Lua
    game::ScriptSound* scriptSound_ = nullptr;          // set once the audio engine is up
    std::vector<Entity> heardSelection_;  // selection when the last select sound played
    RTSCamera cam_;
    Json manifest_, rulesJson_;
    std::vector<MapEntry> maps_;
    std::string gameName_ = "AIVERSE";
    Rules rules_;
    i18n::Localization loc_;
    i18n::GameLanguages langs_;
    std::vector<std::string> langNames_;
    std::string forcedLanguage_;

    // online play
    std::vector<net::MapInfo> netMaps_;
    std::string netAddress_ = "127.0.0.1", netName_, netError_, chatInput_, roomName_;
    std::vector<std::string> chat_;
    uint64_t netKey_ = 0;
    bool netMatch_ = false, prevSpectator_ = false;

    Screen screen_ = Screen::Menu;
    std::unique_ptr<Simulation> sim_;
    std::unique_ptr<net::NetClient> net_;     // after sim_: destroyed first, so it never outlives the sim it drives
    std::vector<RtsAI> bots_;
    RtsScene scene_;
    uint8_t human_ = 1;
    bool spectator_ = false;
    int difficulty_ = 1;
    size_t mapIndex_ = 0;
    double time_ = 0;
    std::string error_;
    std::vector<Notice> notices_;

    // input
    std::vector<Entity> selection_;
    std::array<std::vector<Entity>, 10> groups_;
    Mode mode_ = Mode::Normal;
    Entity placeProducer_;   // sidebar placement: structure comes from this producer
    int abilityIndex_ = -1;
    bool pairFirst_ = false;      // pointPair player ability: the first point is chosen
    float pairX_ = 0, pairZ_ = 0;
    int placeType_ = -1;
    int roomIndex_ = -1;          // Mode::Room: rules.rooms index, -1 = erase
    bool rectDrag_ = false;       // rect tools: dragging from (rectX_, rectY_)
    float rectX_ = 0, rectY_ = 0;
    bool fireHeld_ = false, walkHeld_ = false;   // possession: mouse buttons held
    float steerX_ = 0, steerZ_ = 0;
    bool steerFire_ = false;
    Entity steerTarget_;
    uint32_t lastSteerTick_ = 0;
    bool howtoOpen_ = false;
    int sendPercent_ = 50;
    RegionSwipe regionSwipe_;
    game::EntityActionControls entityActions_;
    bool entityPointer_=false;
    int64_t entityTouch_=-1;
    float entityStartX_=0,entityStartY_=0;
    bool regionEvent(const platform::Event&, bool);
    uint32_t regionTick_ = UINT32_MAX;
    bool boxing_ = false, panning_ = false, sendDrag_ = false;
    float downX_ = 0, downY_ = 0, mouseX_ = -1, mouseY_ = -1;
    double lastClickTime_ = 0;
    Entity hover_;
    bool keys_[int(platform::Key::Count)] = {};
    std::map<int64_t, std::pair<float, float>> touches_;
    float pinchDist_ = 0;
    bool touchMoved_ = false;
    bool mapTapPending_ = false;   // "mapClick" games: a left press is a tap until it moves (then it pans)
    float mapTapX_ = 0, mapTapY_ = 0;
    bool mapDrag_ = false, mapDragOverUi_ = true;   // a press on a HUD element with attribute map-drag (ability discs)
    int64_t mapDragTouch_ = -1;
    float mapDragX_ = 0, mapDragY_ = 0;   // the dragging pointer (view.targeting() "map-drag" while it moves)
    void mapDragEvent(const platform::Event& e, bool uiWants);   // ClientScriptCmd.cpp
    std::vector<std::pair<float3, double>> markers_;
};

} // namespace aiverse::client
