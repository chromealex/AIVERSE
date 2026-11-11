// Editor for every game built on the engine. Unity-like layout of dockable windows (Scene, Game,
// Hierarchy, Inspector, Tools, Project, Rules, Scenario, Statistics, Agent, Console); windows are
// plugins (EditorWindow.h). All document state lives in the shared Workspace (apps/editor/core),
// which is also what the MCP server drives, so an agent's edits show up here live.
//
// The public part of Editor is the API windows use; keep it small and stable.
#pragma once
#include <cmath>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ClientHost.h"
#include "EditorProfile.h"
#include "EditorTheme.h"
#include "EditorNativeBuild.h"
#include "modules/rts/view/RtsViewScript.h"
#include "EditorWindow.h"
#include "core/FileAssoc.h"
#include "core/Net.h"
#include "core/Workspace.h"
#include "core/FieldOps.h"
#include "core/WorldInspect.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/render/TextureCache.h"
#include "engine/gfx/Gfx.h"
#include "engine/platform/Platform.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/render/PostFx.h"
#include "engine/render/Renderer3D.h"
#include "engine/sim/Simulation.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsMap.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/fx/RtsFx.h"
#include "modules/rts/view/RtsView.h"

namespace aiverse::editor {

// What the Inspector shows. Hierarchy/Project/Scene set it.
struct Selection {
    enum Kind { None, Entity, Player, Area, Region, Wave, Trigger, Type, Resource, Tech, Faction, MapProps, GameProps,
                Live,       // an object of the world tree that is not a map entity being edited (ref): created by the game, or play mode
                MapBlock,   // a block of the map document's extra JSON (key = path: "lights[2]", "gridFields")
                Road,       // map road (index)
                Body } kind = None;   // planet map body (index)
    int index = -1;          // entity / player (0-based) / wave / trigger / road / body
    std::string key;         // area name, region letter, rule id, map block path
    uint64_t ref = 0;        // Live: worldRef (packed + 1) of an entity of the world tree (Editor::world())
    bool operator==(const Selection& o) const { return kind == o.kind && index == o.index && key == o.key && ref == o.ref; }
};

enum class Tool { Select, Place, Terrain, Level, Height, Field, Region, Area, Start, Road, Erase, Count };

// Offscreen 3D view shown inside a window with ImGui::Image.
struct Viewport {
    gfx::TextureHandle color, depth;
    int w = 0, h = 0;                 // pixels
    float pointsW = 0, pointsH = 0;   // logical size in the window
    float originX = 0, originY = 0;   // top-left on screen (points)
    bool hovered = false;
    int drawnFrame = -100;            // ImGui frame the owning window last drew it (hidden tab: stale)
    void ensure(gfx::Device& dev, int pw, int ph);
    void release(gfx::Device& dev);
    // ImGui::Image with the right orientation for the backend
    void image(gfx::Device& dev, float wPts, float hPts);
};

class Editor {
public:
    struct Options {
        std::string projectDir;   // folder with game.json (empty: show the game picker)
        std::string engineDir;    // engine checkout (templates/, docs/, aiversebuild)
        std::string buildTool;    // path to aiversebuild
        std::string gameExe;      // path to the game client (device runs)
        std::string ownGameExe;   // AiverseGame built in the same tree as this editor (same C++ modules); may be empty
        std::string mcpTool;      // path to aiversemcp (one-click agent setup in the Agent window)
        std::string editorExe;    // this executable (aiversemcp launches it when the editor isn't running)
        std::string openMap;
        int listenPort = -1; // -1 = automatic, 0 = no agent link, positive = exact port
        bool nativeChild = false;    // --native-editor / AIVERSE_NATIVE_EDITOR=1: started by the switch to the project's build (EditorNativeBuild.cpp)
    };

    explicit Editor(Options o) : opt_(std::move(o)) {}
    void init(gfx::Device& dev);
    void frame(gfx::Device& dev, double dt);
    void event(const platform::Event& e);
    void shutdown();

    // ---------------- API for windows ----------------
    Workspace& ws() { return ws_; }
    MapDoc& doc() { return ws_.map; }
    const game::Rules& rules() const { return ws_.project.rules; }
    gfx::Device& device() { return *dev_; }
    render::Renderer3D& renderer() { return r3d_; }
    render::PostFx& postFx() { return postFx_; }   // post effects of Scene / Game views (PostFxWindow configures it)
    bool postFxInScene = true;                     // false: only the Game view is post-processed
    game::RtsView& view() { return view_; }
    std::shared_ptr<render::PaintLayers> paintLayers;
    std::shared_ptr<game::EntityFxStore> entityFx;   // view.entity_fx of the play-test view scripts
    game::ViewTargeting scriptTargeting;
    const game::MapGrid& editGrid() const { return grid_; }
    const Options& options() const { return opt_; }
    int mcpPort() const { return server_.port(); }
    bool enableAgentLink() { return server_.listening() || server_.listenAvailable(); }   // after --listen 0: open the channel now
    double time() const { return time_; }

    // Selection: `selection` is the primary item (Inspector header, F focus); selectionSet holds every selected
    // item (multi-selection from the Hierarchy / Scene). Code that assigns `selection` directly gets a set of one.
    Selection selection;
    std::vector<Selection> selectionSet;
    void select(Selection s) { selection = std::move(s); selectionSet.clear(); if (selection.kind != Selection::None) selectionSet.push_back(selection); }
    int selectedEntity() const { return selection.kind == Selection::Entity ? selection.index : -1; }
    void selectEntity(int i) { if (i < 0) select({}); else select({Selection::Entity, i, {}}); }
    void selectToggle(const Selection& s);             // Ctrl / Cmd click
    void selectAdd(const Selection& s);                // keeps the others, makes s primary
    void selectMany(std::vector<Selection> items);     // first = primary
    bool isSelected(const Selection& s);
    const std::vector<Selection>& selected();          // the set, in sync with `selection`
    std::vector<int> selectedEntities();               // map document entities of the set (sorted)
    void removeSelectedEntities();                     // one undo step
    // Hover shared by the Scene and the Hierarchy: windows call setHover() while drawing; `hover` is last frame's.
    Selection hover;
    void setHover(const Selection& s) { hoverNext_ = s; }

    // ---- world tree (EditorWorld.cpp, docs/editor-world-tree.md): every object of the game world. Edit mode: the
    // map's setup run once in a throwaway simulation (map entities + what the game creates at start); play mode:
    // the running test (the editor's play-test or the hosted client).
    const WorldModel& world() const { return world_; }
    bool worldLive() const { return worldLive_; }
    Simulation* worldSim();                            // the simulation the rows were read from (null: none)
    game::RtsModule* worldModule();
    const WorldPreview& worldPreview() const { return preview_; }
    const WorldConfig& worldConfig() const { return worldCfg_; }
    void refreshWorld() { previewKey_ = 0; previewForce_ = true; }
    bool worldPending() const { return !worldLive_ && (previewKey_ != previewSeenKey_ || preview_.busy()); }   // preview older than the map
    bool worldBuilding() const { return !worldLive_ && preview_.busy(); }     // a background preview build runs
    // Consumers of the rows say so every frame they use them (Hierarchy, the Scene's game objects, Details in
    // edit mode); without one the play world is not read and the preview is not built. scriptData: the rows
    // need game.data (label / data grouping / search), which costs a Lua flush + decoding every blob.
    void wantWorld(bool scriptData = false) const { worldWantAt_ = time_; if (scriptData) worldDataAt_ = time_; }
    // One selected object as it is now: its row, and (data) inspectEntity + the current Lua data. Play mode reads
    // the running simulation directly (not the 5 Hz rows). False: gone / not in the world tree.
    bool worldDetails(const Selection& s, WorldRow& row, Json* data);
    bool worldAuto = true;                             // rebuild the preview on map / rules / script changes
    bool showGameObjects = true;                       // Scene (edit mode): draw what the game creates at start
    Selection worldSelection(const WorldRow& r) const; // Entity(doc) while editing a map object, Live(ref) otherwise
    const WorldRow* worldRow(const Selection& s) const;
    bool worldFresh() const;                           // edit mode: the preview matches the map as it is now
    bool selectionPos(const Selection& s, float& x, float& z) const;   // where to focus
    // Scene visibility / lock (Hierarchy eye / lock): keys "o<owner>", "t<type id>", "o<owner>/t<type id>", "game"
    std::set<std::string> sceneHidden, sceneLocked;
    bool objectHidden(int owner, const std::string& type) const;
    bool objectLocked(int owner, const std::string& type) const;

    // tools (Tools window edits these; Scene window applies them)
    Tool tool = Tool::Select;
    int placeType = 0, placeOwner = 0;
    char paintCell = '~';
    int paintLevel = 1, fieldRes = 0, fieldAmount = 9, startPlayer = 1, brush = 1;
    char regionLetter = 'A';
    bool snap = true, showLevels = true, showRegions = true, showAreas = true, followAgent = true;
    int roadStart = -1;

    // camera of the Scene window (agents fly it with view_focus)
    render::RTSCamera sceneCam;
    void focusScene(float x, float z, float distance = -1);
    // planet maps (world block): the Scene camera is the view's orbit camera; picking returns a body point
    bool planetMap() const { return view_.planetMode(); }
    bool planetAt(const Viewport& vp, float sx, float sy, int& body, render::float3& local) const;
    std::string planetBodyId(int body) const;
    // Planet maps: screen point -> map position {"body", "face", "x", "y"} (cells); null when nothing is hit.
    Json planetPosAt(const Viewport& vp, float sx, float sy) const;
    // Body-local point of a map position (entity "at", player "start"); false when invalid.
    bool planetLocal(const Json& at, int& body, render::float3& local) const;
    void fitScene();

    // play-test (Game window)
    bool playing() const { return sim_ != nullptr; }
    Simulation* sim() { return sim_.get(); }
    game::RtsModule* simModule() { return sim_ ? dynamic_cast<game::RtsModule*>(&sim_->module()) : nullptr; }
    void startFpsPlay(bool listen=false, const std::string& address="");
    void renderFpsScene(Viewport&,const render::RTSCamera&);
    void startPlay(int humanSlot = 0);
    void stopPlay();
    // Play as in the game client, inside the Game window (EditorClientPlay.cpp, ClientHost.h): the game's
    // menus / meta screens, view scripts, script UI and input. Scripted games (ui/main.lua) prefer it for F5.
    void startClientPlay();
    void stopClientPlay();
    bool clientPlaying() const { return clientHost_.active(); }
    bool prefersClientPlay() const;
    ClientHost& clientHost() { return clientHost_; }
    int humanSlot() const { return human_; }
    float playSpeed = 1.0f;
    bool playPaused = false;
    render::RTSCamera gameCam;
    std::vector<aiverse::Entity> gameSelection;
    std::string gameDevice = "none";           // simulator preset (Devices.h)
    bool gameLandscape = true;
    bool gameSafeArea = false;          // Game window: show the device safe area on top of the picture
    Viewport* gameViewport = nullptr;          // set by the Game window (screenshots crop to it)
    Viewport* sceneViewport = nullptr;
    // the Scene tab drew its viewport this frame or the last one (false while another tab covers it):
    // gizmos and brushes of other windows must not use a stale sceneViewport
    bool sceneVisible() const { return sceneViewport && sceneViewport->drawnFrame >= ImGui::GetFrameCount() - 1; }
    void issueCommand(Command c);             // from the human player in the Game window

    // documents
    void newMap(int w, int h);
    void openMap(const std::string& file);
    bool saveMap(const std::string& file);
    void openGame(const std::string& idOrDir);
    // Open a project asked for by the OS (double-clicked .rtsproj), the hub or the menus: asks to save
    // unsaved map / rules changes first (EditorOpenFile.cpp). Accepts a .rtsproj, a folder or game.json.
    void requestOpenProject(const std::string& path);
    // .rtsproj file association (FileAssoc.h): status shown in the hub, registration from the Project menu
    const AssocStatus& fileAssoc() const { return assoc_; }
    void registerFileAssoc();
    void reloadRules();                        // after rules change: views, palette
    void showGamePicker() { showProjectHub(0); }
    void showProjectHub(int tab);   // 0: recent projects, 1: new project, 2: make a game (EditorProjects.cpp)

    // rendering a map view into a Viewport (used by Scene and Game windows)
    void renderScene(Viewport& vp, const render::RTSCamera& cam, bool editOverlays);
    void renderGame(Viewport& vp, const render::RTSCamera& cam);

    // overlays (Overlays window / overlay_preview): ids drawn regardless of their conditions, and a
    // structure type whose ghost overlays (build grid, placement) are previewed at the cursor
    std::vector<std::string> overlayPreview;
    int overlayPreviewType = -1;

    // particle effects of the play-test (fx/fx.json + fx/*.json of the open game)
    fx::ParticleSystem& gameFx() { return gameFx_; }
    void reloadFx();                           // after fx files changed (Effects window, agents)
    std::vector<std::string> fxWarnings;       // from the last reloadFx()
    int fxRevision = 0;                        // bumped when fx files change (agents): the Effects window re-reads

    // helpers for windows
    bool groundAt(const Viewport& vp, const render::RTSCamera& cam, float sx, float sy, float& x, float& z) const;
    int pickEntity(const Viewport& vp, const render::RTSCamera& cam, float sx, float sy) const;
    // Any object under the cursor: map entities (not hidden / locked) and the world tree's objects shown in
    // the Scene (game-created objects in edit mode, live objects in play mode). None when nothing is hit.
    Selection pickObject(const Viewport& vp, const render::RTSCamera& cam, float sx, float sy) const;
    int pickRoad(const Viewport& vp, const render::RTSCamera& cam, float sx, float sy) const;
    float snapCoord(float v, int typeIdx) const;
    void flash(float x0, float z0, float x1, float z1, double seconds = 1.6);
    std::string status;                        // status bar text
    void runBuild(const std::string& target);
    int buildConfig = 1;

    // windows
    EditorWindow* window(const std::string& id);
    const std::vector<std::unique_ptr<EditorWindow>>& windows() const { return windows_; }
    void resetLayout() { resetLayout_ = true; }
    void launchOnDevice(const std::string& device, bool landscape);
    // The project in a separate game client window (AiverseGame with AIVERSE_CONTENT = project), as players get it:
    // the game's own menus, scripts and input (EditorClientPlay.cpp).
    void launchClient();
    void focusWindow(const std::string& id);   // open + bring to front (by stable id)
    // Windows of a top-menu group (EditorWindow::group(); "other": unknown groups), default ones first.
    std::vector<EditorWindow*> windowsOf(const std::string& group) const;
    // Workspace: the default windows + all windows of the group, layout rebuilt, the group's windows in front.
    void openWorkspace(const std::string& group);
    EditorTheme& theme() { return theme_; }

    // statistics
    struct FrameStats { float frameMs = 0, simMs = 0; uint32_t drawCalls = 0; };
    std::deque<FrameStats> frameHistory;
    EditorProfile prof;                                // world tree phase timings (AIVERSE_EDITOR_PROFILE=1 prints them)
    struct PlaySample { float t; std::vector<int> units, structures; std::vector<std::vector<float>> resources; };
    std::vector<PlaySample> playHistory;

private:
    void drawMenuBar();
    void drawWindowItems(const std::string& group);
    bool isWindowInFront(EditorWindow* w) const;
    void drawTopBarControls();   // breadcrumb, play controls, MCP pill (inside the main menu bar)
    void drawPlayControls();
    void drawDockspace();
    void drawGamePicker();
    void drawGameWizard();       // hub page "Сделать игру" (EditorGameWizard.cpp)
    void pollGameWizard();       // every frame: the wizard's agent progress (EditorGameWizard.cpp)
    void drawStatusBar();
    float statusBarHeight() const { return ImGui::GetFontSize() + 12; }
    void drawActivityToasts();
    void handleShortcuts();
    void syncGrid();
    void buildEditScene(game::RtsScene& out) const;
    void drawEditOverlays3D();
    void applyChanges();
    void pollAgentLink();
    void registerGuiOps();
    void registerPlayOps();   // EditorPlayOps.cpp
    void registerFxPreviewOps();   // EditorFxOps.cpp: fx_preview
    // Particles of the Scene / Game views (EditorFxOps.cpp): after beginScene (dt, previews, event bridge,
    // particle lights into r3d_.lights); render them after Renderer3D::render.
    void tickFx(const render::RTSCamera& cam, int w, int h, const std::vector<game::SceneObject>* objects);
    void samplePlayStats();
    void updateWorld();       // EditorWorld.cpp: preview / live rows, selection kept across rebuilds
    void syncSelection();
    void registerWorldOps();  // EditorWorld.cpp: world_entities, world_entity, editor_select
    void deliverShots(gfx::Device& dev);
    void setOverlayContext(const Viewport& vp, const render::RTSCamera& cam, bool play);   // input state for RtsOverlays

    Options opt_;
    Workspace ws_;
    gfx::Device* dev_ = nullptr;
    render::ImGuiLayer ui_;
    ClientHost clientHost_;   // after ui_: shut down first (its ImGui context, RmlUi)
    render::Renderer3D r3d_;
    render::Mesh fpsPreviewMesh_;
    render::PostFx postFx_;
    game::RtsView view_;
    bool showPicker_ = false;
    bool resetLayout_ = false;
    bool layoutChecked_ = false;
    int lastPlaySlot_ = 0;       // what the top bar Play button starts (0 = bots)
    EditorTheme theme_;
    int focusFirst_ = 0;
    std::vector<std::string> pendingFocus_;   // openWorkspace: tabs to bring to front once docked
    int pendingFocusFrames_ = 0;
    std::vector<std::unique_ptr<EditorWindow>> windows_;

    game::MapGrid grid_;
    uint64_t gridRevision_ = ~0ull;
    uint64_t worldHash_ = 0;   // map world block as last given to the view (planets)
    double gridRebuildAt_ = 0;
    bool playTerrainBuilt_ = false;
    game::RtsScene scene_;
    FieldPreview fieldPreview_;   // grid field initial values for the Scene view (FieldOps.h)
    double time_ = 0;
    bool quit_ = false;
    // EditorOpenFile.cpp
    void initFileAssoc();
    void drawOpenRequest();
    std::string pendingOpen_;
    bool openAsked_ = false;
    AssocStatus assoc_;
    int drawUnsavedChanges(const std::string& question);   // body of an unsaved-changes modal: 0 / 1 go / 2 cancel
    // EditorNativeBuild.cpp: the project's C++ modules missing from this binary -> build an editor with them
    bool projectBuild() const;
    bool checkNativeModules(bool fromPlay);   // true: modules are missing (banner shown)
    void refreshNativeBuild(bool withTools);
    void startNativeEditorBuild();
    void restartIntoNativeBuild();
    void drawNativeBuild();
    NativeBuildUi native_;

    struct Flash { float x0, z0, x1, z1; double until, length; };
    std::vector<Flash> flashes_;
    bool sceneFlying_ = false;
    render::float3 sceneGoal_{};

    fx::ParticleSystem gameFx_;
    game::RtsFx gameFxBridge_;
    render::TextureCache fxTextures_;          // sprite textures of effects (same loader as sprites)
    double fxTime_ = -1;
    struct FxPreview { std::string name; fx::float3 pos{}; float scale = 1; fx::float4 tint{1, 1, 1, 1}; bool loop = false; fx::FxHandle h; double next = 0;
                       bool moving = false; float moveT = 0; };   // moving: circles 2 m around pos (trails, perMeter)
    FxPreview fxPreview_;                      // fx_preview: the effect an agent is looking at
    bool fxFrozen_ = false;                    // fx_preview {freeze}: particles hold still for screenshots
    std::unique_ptr<Simulation> sim_;
    std::vector<game::RtsAI> bots_;
    int human_ = 0;
    std::map<std::string, Entity> playIds_;   // map entity "id" -> live entity of the play-test (play_command names)
    float lastSimMs_ = 0;

    // world tree (EditorWorld.cpp)
    WorldModel world_;
    WorldConfig worldCfg_;
    WorldPreview preview_;
    bool worldLive_ = false;
    uint64_t previewKey_ = 0, previewSeenKey_ = 0;
    double previewDue_ = 0, worldReadAt_ = 0;
    bool previewForce_ = false;
    mutable double worldWantAt_ = -1e9, worldDataAt_ = -1e9;   // last frame a consumer used the rows / their script data
    bool worldStale_ = true;                   // play: rows not read since the last 0.2 s mark (nobody looked)
    std::map<uint64_t, int> liveDocOf_;        // live ref -> map entity (liveLinks_ inverted)
    bool worldWanted() const { return time_ - worldWantAt_ < 0.5; }
    bool worldDataWanted() const { return time_ - worldDataAt_ < 0.5; }
    void readLiveWorld(Simulation& live, game::RtsModule& mod);
    void ensureWorld();                        // MCP: rows (with script data) current now
    int rulesRevision_ = 0;
    const Simulation* liveSim_ = nullptr;      // live world last read (setup refs belong to it)
    std::vector<uint64_t> liveLinks_, liveSetup_;
    std::vector<uint64_t> playDocLinks_;       // editor play-test: map entity -> live entity (set at startPlay)
    Selection hoverNext_;
    double scriptsCheckAt_ = 0;
    uint64_t scriptsStamp_ = 0;

    net::LineServer server_;
    struct PendingShot { int client; int64_t id; std::string device; };
    std::vector<PendingShot> shots_;
    bool captureArmed_ = false;
    bool overlayKeys_[int(platform::Key::Count)] = {};
};

} // namespace aiverse::editor
