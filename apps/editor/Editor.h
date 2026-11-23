// Editor for every game built on the engine. Unity-like layout of dockable windows (Scene, Game,
// Hierarchy, Inspector, Tools, Project, Rules, Scenario, Statistics, Agent, Console); windows are
// plugins (EditorWindow.h). All document state lives in the shared Workspace (apps/editor/core),
// which is also what the MCP server drives, so an agent's edits show up here live.
//
// The public part of Editor is the API windows use; keep it small and stable.
#pragma once
#include "SceneRenderable.h"
#include <array>
#include <cmath>
#include <atomic>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>

#include "ClientHost.h"
#include "EditorProfile.h"
#include "EditorTheme.h"
#include "EditorNativeBuild.h"
#include "engine/view/game/GameViewScript.h"
#include "EditorWindow.h"
#include "TerraWeaveForestPreview.h"
#include "PhysisNaturePreview.h"
#include "engine/world/NereidManifestHost.h"
#include "TerraWeaveSparsePreview.h"
#include "core/FileAssoc.h"
#include "core/Net.h"
#include "core/Workspace.h"
#include "core/FieldOps.h"
#include "core/WorldInspect.h"
#include "core/VisualAssetDependencyService.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/render/TextureCache.h"
#include "engine/gfx/Gfx.h"
#include "engine/platform/Platform.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/render/Prisma.h"
#include "engine/render/Renderer3D.h"
#include "ViewportGpuRetirement.h"
#include "engine/render/GpuResourceRetirement.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/render/NativePresentationRetirement.h"
#include "engine/render/RetirementDiagnosticRing.h"
#include "engine/sim/Simulation.h"
#include "engine/game/ComputerPlayer.h"
#include "engine/game/MapGrid.h"
#include "engine/game/GameCore.h"
#include "engine/fx/GameFx.h"
#include "engine/view/game/GameView.h"
#include "engine/view/game/GameModelDemandScheduler.h"

namespace aiverse::spectra {
class GpuPoolWorld;
class GpuCaptureRequestMailbox;
class GpuPoolRecoveryHost;
}

namespace aiverse::editor {

// Read-only 180-frame diagnostic history of the optional Metal preview.
// This is NOT a restorable GPU snapshot or authoritative simulation data.
struct SpectraGpuPreviewFrameRecord {
    uint64_t deviceFrame=0;
    double visualSeconds=0;
    uint32_t estimatedParticles=0;
    uint32_t freshGroups=0, staleGroups=0, pendingGroups=0;
    uint32_t killedSlotsWaiting=0;
    bool stepAccepted=false;
    bool deviceHealthy=false;
};


// What the Inspector shows. Hierarchy/Project/Scene set it.
struct Selection {
    enum Kind { None, Entity, Player, Area, Region, Wave, Trigger, Type, Resource, Tech, Faction, MapProps, GameProps,
                Live,       // an object of the world tree that is not a map entity being edited (ref): created by the game, or play mode
                MapBlock,   // a block of the map document's extra JSON (key = path: "lights[2]", "gridFields")
                Road,       // map road (index)
                ActorObject,  // FPS document collider/spawn, stable source key in ref
                ActorActor,   // FPS runtime actor ECS identity, slot index for editor UI
                ActorRuntimeObject, // FPS projectile, loot or vehicle ECS entity; stable kind/id key in ref
                Body } kind = None;   // planet map body (index)
    int index = -1;          // entity / player (0-based) / wave / trigger / road / body
    std::string key;         // area name, region letter, rule id, map block path
    uint64_t ref = 0;        // Live: worldRef (packed + 1) of an entity of the world tree (Editor::world())
    bool operator==(const Selection& o) const { return kind == o.kind && index == o.index && key == o.key && ref == o.ref; }
};

enum class Tool { Select, Place, Terrain, Level, Height, Field, Region, Area, Start, Road, Erase, Count };
// Universal Scene Transform interaction mode. Unsupported operations are
// disabled per selected entity's real authoring schema.
enum class TransformTool { Move, Rotate, Scale };

// Offscreen 3D view shown inside a window with ImGui::Image.
struct Viewport {
    gfx::TextureHandle color, depth;
    int w = 0, h = 0;                 // pixels
    float pointsW = 0, pointsH = 0;   // logical size in the window
    float originX = 0, originY = 0;   // top-left on screen (points)
    bool hovered = false;
    render::RenderDeviceLifetime::Lease gpuDeviceLease;
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease lease) {gpuDeviceLease=std::move(lease);}
    int drawnFrame = -100;            // ImGui frame the owning window last drew it (hidden tab: stale)
    // Device/retirement owner must outlive this window. Editor clears all
    // windows before draining its retirement queue on GPU-idle teardown.
    gfx::Device* ownerDevice = nullptr;
    ViewportGpuRetirement* retirement = nullptr;
    ~Viewport() { if (ownerDevice) release(*ownerDevice); }
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;
    Viewport() = default;
    void ensure(gfx::Device& dev, int pw, int ph, ViewportGpuRetirement& owner,
                const char* debugName = "Viewport");
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

    explicit Editor(Options o);
    ~Editor(); // out-of-line: owns optional Spectra GPU preview resources
    void init(gfx::Device& dev);
    void frame(gfx::Device& dev, double dt);
    void event(const platform::Event& e);
    void shutdown();

    // ---------------- API for windows ----------------
    Workspace& ws() { return ws_; }
    MapDoc& doc() { return ws_.map; }
    const MapDoc& doc() const { return ws_.map; }
    const game::Rules& rules() const { return ws_.project.rules; }
    gfx::Device& device() { return *dev_; }
    render::RenderDeviceLifetime::Lease gpuDeviceLease() const {
        return gpuLifetime_?gpuLifetime_->lease():render::RenderDeviceLifetime::Lease{};
    }
    const render::RetirementDiagnosticRing* gpuRetirementDiagnostics() const {
        return gpuRetirementRing_.get();
    }
    render::Renderer3D& renderer() { return r3d_; }
    render::GpuResourceRetirement& gpuResourceRetirement() { return gpuResourceRetirement_; }
    const render::GpuResourceRetirement& gpuResourceRetirement() const { return gpuResourceRetirement_; }
    ViewportGpuRetirement& viewportRetirement() { return viewportRetirement_; }
    const ViewportGpuRetirement& viewportRetirement() const { return viewportRetirement_; }
    // Request a GPU-idle drain at NEXT frame's safe pre-render boundary;
    // never synchronize inside an active ImGui/window render pass.
    void requestGpuRetirementFlush() { gpuRetirementFlushRequested_ = true; }
    bool lastGpuRetirementFlushSucceeded() const { return gpuRetirementLastFlushSucceeded_; }
    render::Prisma& prisma() { return prisma_; } // canonical PRISMA editor API
    render::Prisma& postFx() { return prisma(); } // legacy alias; existing tools depend on it
    // Editor-owned native preview Model GPU retirement. Panels may replace
    // Models while the editor is recording offscreen windows in the same
    // frame; never destroy those Models until a later GPU completion token.
    void retirePreviewModel(std::unique_ptr<render::Model>& model) noexcept;
    size_t pendingPreviewRetirements() const noexcept {
        return previewModelRetirement_.pending();
    }
    bool postFxInScene = true;                     // false: only the Game view is post-processed
    bool terraWeaveForestPreviewEnabled() const { return terraWeaveForestPreview_.enabled(); }
    void setTerraWeaveForestPreviewEnabled(bool enabled) {
        if (!enabled && dev_) terraWeaveForestPreview_.release(*dev_);
        terraWeaveForestPreview_.setEnabled(enabled);
    }
    size_t terraWeaveForestPreviewCount() const { return terraWeaveForestPreview_.visibleInstances(); }
    // PHYSIS «Живая природа» Scene preview (PhysisWindow, physis_nature_preview op).
    PhysisNaturePreview& physisNaturePreview() { return physisNaturePreview_; }
    // Authored NEREID water of the open map, rebuilt when the document changes.
    const AuthoredWaterQuery& authoredWater() {
        if (authoredWaterRevision_ != doc().revision || authoredWaterMap_ != &doc()) {
            authoredWater_ = AuthoredWaterQuery::fromDoc(doc());
            authoredWaterRevision_ = doc().revision;
            authoredWaterMap_ = &doc();
        }
        return authoredWater_;
    }
    game::GameView& view() { return view_; }
    bool lazyEditorAssetsEnabled() const { return lazyEditorAssets_; }
    // Pin a type that an editor panel or preview uses (also loads it lazily).
    void retainEditorType(uint16_t type);
    // Reclaim unreferenced type models after a grace period. Invoked by the
    // editor every few seconds or from the Statistics panel.
    void reclaimEditorAssets(bool force = false);
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
    TransformTool transformTool = TransformTool::Move;
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
    game::GameCore* worldModule();
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
    render::Camera sceneCam;
    // Caverns brush hover preview, authored by TerrainWindow on the UI thread.
    // Consumed by drawEditOverlays3D on the same editor frame.
    struct CavernsBrushPreview {
        bool visible=false;
        render::float3 position{};
        float radiusMetres=0.0f;
        bool fill=false;
    } cavernsBrushPreview;
    struct CavernsBuildStats {
        size_t pending=0,prepared=0,inFlight=0;
        size_t gpuActive=0,gpuStaged=0,gpuRetired=0,gpuRemaining=0;
        uint64_t gpuPreviewRevision=0,gpuRetryAfterFrame=0;
        size_t gpuUploadFailures=0,gpuRetainedStrata=0;
        uint64_t gpuFrameBytes=0,gpuRejectedBytes=0,gpuRejectedPressure=0;
        uint64_t authorEpoch=0;
        bool visualCommitConnected=false;
        std::string diagnostic;
    } cavernsBuildStats;
    // Visual-only GPU cave preview. Gameplay colliders/nav remain unchanged.
    bool cavernsVisualPreviewEnabled=false;


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
    game::GameCore* simModule() { return sim_ ? dynamic_cast<game::GameCore*>(&sim_->module()) : nullptr; }
    void startFpsPlay(bool listen=false, const std::string& address="");
    void enqueueFpsSceneGeometry(const render::Camera&, bool editOverlays=true);
    const Json& fpsRuntimeSnapshot(); // FPS character / projectile / loot / vehicle entities; cached by document/rules revision
    int pickFpsSceneObject(const Viewport&, const render::Camera&, float sx, float sy) const;
    void selectFpsSceneSource(bool spawn, size_t sourceIndex);
    bool isFpsSceneSourceSelected(bool spawn, size_t sourceIndex) const;
    bool moveSelectedFpsSceneObject(float dx, float dy, float dz);
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
    render::Camera gameCam;
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
    // Open a project asked for by the OS (double-clicked .aiverse), the hub or the menus: asks to save
    // unsaved map / rules changes first (EditorOpenFile.cpp). Accepts a .aiverse, a folder or game.json.
    void requestOpenProject(const std::string& path);
    // .aiverse file association (FileAssoc.h): status shown in the hub, registration from the Project menu
    const AssocStatus& fileAssoc() const { return assoc_; }
    void registerFileAssoc();
    void reloadRules();                        // after rules change: views, palette
    // Reload presentation assets without resetting project rules, FX, or editor policy.
    // Call only outside a play session; GPU teardown/publication run on the editor thread.
    void reloadVisualAssets();
    // Targeted model-file reload; keeps unaffected View resources and graph
    // clocks resident. Call outside play mode between renderer submissions.
    size_t reloadVisualModelSources(const std::vector<std::string>& paths);
    void showGamePicker() { showProjectHub(0); }
    void showProjectHub(int tab);   // 0: recent projects, 1: new project, 2: make a game (EditorProjects.cpp)

    // rendering a map view into a Viewport (used by Scene and Game windows)
    void renderScene(Viewport& vp, const render::Camera& cam, bool editOverlays);
    bool cleanSceneCaptureRequested() const;
    std::pair<int,int> sceneCaptureSize(int width,int height) const;
    void renderGame(Viewport& vp, const render::Camera& cam, uint64_t prismaViewId = 2);

    // overlays (Overlays window / overlay_preview): ids drawn regardless of their conditions, and a
    // structure type whose ghost overlays (build grid, placement) are previewed at the cursor
    std::vector<std::string> overlayPreview;
    int overlayPreviewType = -1;

    // particle effects of the play-test (fx/fx.json + fx/*.json of the open game)
    fx::ParticleSystem& gameFx() { return gameFx_; }
    // Queue Metal-only graph preview for the next safe pre-pass phase.
    // Existing CPU preview is the default and never silently GPU-routed.
    void queueSpectraGpuPreview(fx::EffectDef effect,
        std::array<float,8> params,fx::float3 position,bool inGame,bool loop);
    void stopSpectraGpuPreview();
    bool spectraGpuPreviewActive() const;
    bool spectraGpuPreviewPaused() const {return spectraPreviewPaused_;}
    bool spectraGpuPreviewFrozen() const {return spectraPreviewFrozen_;}
    bool spectraGpuPreviewLoop() const {return spectraPreviewLoop_;}
    void setSpectraGpuPreviewPaused(bool value) {spectraPreviewPaused_=value;}
    void setSpectraGpuPreviewFrozen(bool value) {spectraPreviewFrozen_=value;}
    void setSpectraGpuPreviewLoop(bool value) {spectraPreviewLoop_=value;}
    float spectraGpuPreviewSpeed() const {return spectraPreviewSpeed_;}
    void setSpectraGpuPreviewSpeed(float speed);
    bool stepSpectraGpuPreviewOnce() {return queueSpectraGpuPreviewFrames(1);}
    // A bounded forward-only visual timeline. One fixed 1/60 step is
    // consumed per actual scene-render frame; no backward GPU scrubbing.
    bool queueSpectraGpuPreviewFrames(uint32_t frames);
    bool restartSpectraGpuPreview();
    enum class SpectraGpuCaptureState : uint8_t {
        Idle, Queued, InFlight, Ready, Failed
    };
    // Freeze before enqueueing. The request is performed ONLY at next safe
    // pre-pass graphics phase and serviced asynchronously by the RHI.
    bool requestSpectraGpuPreviewCheckpoint();
    SpectraGpuCaptureState spectraGpuCaptureState() const {
        return spectraGpuCaptureState_;
    }
    const std::string& spectraGpuCaptureNote() const {
        return spectraGpuCaptureNote_;
    }
    double spectraGpuPreviewVisualSeconds() const {
        return spectraPreviewVisualSeconds_;
    }
    uint32_t spectraGpuPreviewPendingSteps() const {
        return spectraPreviewStepRequests_;
    }
    const std::string& spectraGpuPreviewLastError() const {
        return spectraPreviewLastError_;
    }
    const std::deque<SpectraGpuPreviewFrameRecord>&
    spectraGpuPreviewHistory() const {return spectraPreviewHistory_;}
    // JSON diagnostic export; never promises a replayable GPU particle state.
    std::string spectraGpuPreviewHistoryJson() const;
    // Non-owning optional telemetry source. Editor's normal FX preview is
    // CPU-only; external Metal preview hosts explicitly attach their pool.
    // The host MUST detach it before destroying the pool.
    void setSpectraGpuTelemetry(const spectra::GpuPoolWorld* pool) {
        spectraGpuTelemetry_=pool;
    }
    const spectra::GpuPoolWorld* spectraGpuTelemetry() const {
        return spectraGpuTelemetry_;
    }
    // Optional native renderer-owner mailbox, NOT a GPU command interface.
    // The external host must detach this before releasing its mailbox/pool.
    // Never use this for the Editor's own Metal preview.
    void setSpectraGpuCaptureMailbox(
        spectra::GpuCaptureRequestMailbox* mailbox) {
        spectraGpuMailbox_=mailbox;
    }
    const spectra::GpuCaptureRequestMailbox*
    spectraGpuCaptureMailbox() const {return spectraGpuMailbox_;}
    bool queueSpectraExternalGpuCapture(size_t groupIndex);
    bool releaseSpectraExternalGpuCapture();
    // Recovery host is also NON-OWNING. UI may enqueue a status-only
    // visual-reset intent; actual GPU recovery runs in native safe pre-pass.
    void setSpectraGpuRecoveryHost(spectra::GpuPoolRecoveryHost* host) {
        spectraGpuRecoveryHost_=host;
    }
    const spectra::GpuPoolRecoveryHost* spectraGpuRecoveryHost() const {
        return spectraGpuRecoveryHost_;
    }
    bool queueSpectraExternalVisualReset();
    // Exports a FULL, CURRENT-revision GPU checkpoint from an attached pool,
    // if available. Writes one <=529 KiB diagnostic file under userDataDir.
    // out receives a path on success or a reason on failure.
    bool saveFreshSpectraGpuCheckpoint(
        std::string& out,size_t groupIndex=static_cast<size_t>(-1)) const;
    // Saves only aggregate diagnostic counters; coordinates/serials require
    // separate explicit opt-ins. NEVER includes raw GPU/VM scalar buffers.
    bool saveSafeSpectraGpuReport(std::string& out,size_t groupIndex,
        bool includeWorldBounds=false,bool includeGpuIdentifiers=false) const;
    // Only a local, explicit user action. This ZIP cannot contain raw SPFX,
    // world bounds, GPU identifiers or VM scalars even if other export
    // checkboxes are enabled. Fixed filenames and total size <=128 KiB.
    bool saveSafeSpectraGpuBundle(std::string& out,size_t groupIndex) const;
    void reloadFx();                           // after fx files changed (Effects window, agents)
    std::vector<std::string> fxWarnings;       // from the last reloadFx()
    int fxRevision = 0;                        // bumped when fx files change (agents): the Effects window re-reads

    // helpers for windows
    bool groundAt(const Viewport& vp, const render::Camera& cam, float sx, float sy, float& x, float& z) const;
    int pickEntity(const Viewport& vp, const render::Camera& cam, float sx, float sy) const;
    // Display ground height at x,z (metres): the heightfield / levels surface,
    // or on maps whose terrain is entity tiles (terrainPresentation
    // "entities", e.g. TerraWeave exports) a vertical ray against those tile
    // meshes. Cached per 0.5 m; used by the PHYSIS nature preview and gizmos.
    float surfaceHeightAt(float x, float z) const;
    // Any object under the cursor: map entities (not hidden / locked) and the world tree's objects shown in
    // the Scene (game-created objects in edit mode, live objects in play mode). None when nothing is hit.
    Selection pickObject(const Viewport& vp, const render::Camera& cam, float sx, float sy) const;
    int pickRoad(const Viewport& vp, const render::Camera& cam, float sx, float sy) const;
    float snapCoord(float v, int typeIdx) const;
    void flash(float x0, float z0, float x1, float z1, double seconds = 1.6);
    std::string status;                        // status bar text
    void runBuild(const std::string& target);
    int buildConfig = 1;

    // windows
    EditorWindow* window(const std::string& id);
    // Windows may query dock-tab visibility from update() to cancel pending
    // key captures when another tab becomes active. Uses the same check as
    // hosted-client input routing.
    bool isWindowInFront(EditorWindow* w) const;
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
    // Last rendered viewport, same scope as drawCalls; triangle counts omit custom renderers.
    struct FrameStats { float frameMs = 0, simMs = 0; uint32_t drawCalls = 0; uint64_t cameraTriangles = 0; };
    std::deque<FrameStats> frameHistory;
    EditorProfile prof;                                // world tree phase timings (AIVERSE_EDITOR_PROFILE=1 prints them)
    struct PlaySample { float t; std::vector<int> units, structures; std::vector<std::vector<float>> resources; };
    std::vector<PlaySample> playHistory;

private:
    // Unpublished CPU-only generation. Pointer keeps the bridge's async worker
    // state out of this header and is retired with the Editor lifecycle.
    class CavernsEditHost;
    std::unique_ptr<CavernsEditHost> cavernsEditHost_;
    void drawMenuBar();
    void drawWindowItems(const std::string& group);
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
    void buildEditScene(game::GameScene& out) const;
    void drawEditOverlays3D();
    void applyChanges();
    void updateCavernsAuthoring(); // owner-thread MapDoc / Undo observer
    void pollVisualAssets(double dt); // editor-owned asset watcher, independent of windows
    bool convertVisualModelSource(const std::string& source);
    void pollAgentLink();
    void registerGuiOps();
    void registerPlayOps();   // EditorPlayOps.cpp
    void registerFxPreviewOps();   // EditorFxOps.cpp: fx_preview
    // Particles of the Scene / Game views (EditorFxOps.cpp): after beginScene (dt, previews, event bridge,
    // particle lights into r3d_.lights); render them after Renderer3D::render.
    void tickFx(const render::Camera& cam, int w, int h, const std::vector<game::SceneObject>* objects);
    void tickSpectraGpuPreview(const render::Camera& cam,int w,int h,bool game);
    void drawSpectraGpuPreview();
    void samplePlayStats();
    void updateWorld();       // EditorWorld.cpp: preview / live rows, selection kept across rebuilds
    void syncSelection();
    void registerWorldOps();  // EditorWorld.cpp: world_entities, world_entity, editor_select
    void deliverShots(gfx::Device& dev);
    void setOverlayContext(const Viewport& vp, const render::Camera& cam, bool play);   // input state for GameOverlays

    Options opt_;
    Workspace ws_;
    gfx::Device* dev_ = nullptr;
    std::unique_ptr<render::RenderDeviceLifetime> gpuLifetime_;
    std::shared_ptr<render::RetirementDiagnosticRing> gpuRetirementRing_;
    gfx::TextureHandle studioLogo_{}; // editor brand mark
    render::ImGuiLayer ui_;
    ClientHost clientHost_;   // after ui_: shut down first (its ImGui context, RmlUi)
    render::Renderer3D r3d_;
    TerraWeaveForestPreview terraWeaveForestPreview_;
    PhysisNaturePreview physisNaturePreview_;
    world::NereidManifestHost nereidPreview_;
    struct VisualPickModel {
        const render::Model* model=nullptr; // identity only until cache read lease acquired
        std::string entityId;
        size_t documentIndex=0;
        render::mat4 world;
        std::vector<render::mat4> nodes;
        std::vector<render::Material> materials;
        std::vector<int> nodeMaterials;
    };
    std::vector<VisualPickModel> visualPickModels_;
    size_t visualPickModelCount_=0;
    uint64_t visualPickCacheGeneration_=0;
    AuthoredWaterQuery authoredWater_;
    uint64_t authoredWaterRevision_ = ~uint64_t(0);
    const MapDoc* authoredWaterMap_ = nullptr;
    mutable std::vector<float> surfaceGrid_;
    mutable int surfaceGridW_=0, surfaceGridH_=0;
    mutable uint64_t surfaceHeightCacheKey_=0;
    TerraWeaveSparsePreview terraWeaveSparsePreview_;

    render::NativePresentationRetirement previewModelRetirement_;
    render::Mesh fpsPreviewMesh_;
    std::vector<SceneRenderable> fpsSceneObjects_;
    Json fpsRuntimeCache_;
    uint64_t fpsRuntimeRevision_ = UINT64_MAX;
    int fpsRuntimeRulesRevision_ = -1; // same signed revision domain as rulesRevision_
    std::string fpsRuntimeProjectDir_;

    render::Prisma prisma_;
    render::PrismaVolumeStack prismaVolumes_; // editor camera profiles, presentation-only
    std::string prismaVolumesSignature_;
    void refreshPrismaVolumes();
    game::GameView view_;
    bool lazyEditorAssets_ = true;
    game::view_adapter::GameModelDemandScheduler modelDemand_;
    VisualAssetDependencyService visualAssets_;
    bool showPicker_ = false;
    bool resetLayout_ = false;
    bool layoutChecked_ = false;
    int lastPlaySlot_ = 0;       // what the top bar Play button starts (0 = bots)
    EditorTheme theme_;
    int focusFirst_ = 0;
    std::vector<std::string> pendingFocus_;   // openWorkspace: tabs to bring to front once docked
    int pendingFocusFrames_ = 0;
    render::GpuResourceRetirement gpuResourceRetirement_; // outlives ShaderGraph/FX windows
    ViewportGpuRetirement viewportRetirement_; // outlives all window Viewports
    bool gpuRetirementFlushRequested_ = false;
    bool gpuRetirementLastFlushSucceeded_ = false;
    std::vector<std::unique_ptr<EditorWindow>> windows_;
    // Only the GUI owner can decide whether Scene/Game postprocess views
    // actually rendered in the current frame, including dock-hidden tabs.
    bool prismaSceneRendered_ = false, prismaGameRendered_ = false;
    bool prismaScenePreviouslyLive_ = false, prismaGamePreviouslyLive_ = false;

    game::MapGrid grid_;
    uint64_t gridRevision_ = ~0ull;
    uint64_t worldHash_ = 0;   // map world block as last given to the view (planets)
    double gridRebuildAt_ = 0;
    bool playTerrainBuilt_ = false;
    game::GameScene scene_;
    // Opt-in edit-scene snapshot: immutable document-derived objects only.
    // Selection/hover and live world preview remain updated every frame.
    struct EditSceneCacheEntry {
        size_t documentIndex = 0;
        game::SceneObject object;
    };
    mutable std::vector<EditSceneCacheEntry> editSceneCache_;
    mutable uint64_t editSceneCacheMapRevision_ = ~0ull;
    mutable uint64_t editSceneCacheRulesRevision_ = ~0ull;
    mutable const void* editSceneCacheEntities_ = nullptr;
    mutable size_t editSceneCacheEntityCount_ = 0;
    mutable std::set<std::string> editSceneCacheHidden_;
    mutable bool editSceneCacheValid_ = false;
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
    uint64_t roadHash_ = 0;   // spline roads: network + ground the view's meshes were built from
    bool sceneFlying_ = false;
    render::float3 sceneGoal_{};

    const spectra::GpuPoolWorld* spectraGpuTelemetry_=nullptr;
    spectra::GpuCaptureRequestMailbox* spectraGpuMailbox_=nullptr; // non-owning
    spectra::GpuPoolRecoveryHost* spectraGpuRecoveryHost_=nullptr; // non-owning
    std::unique_ptr<spectra::GpuPoolWorld> spectraPreviewGpu_;
    struct RetiredSpectraPreview {
        uint64_t fence = 0;
        std::unique_ptr<spectra::GpuPoolWorld> pool;
    };
    std::vector<RetiredSpectraPreview> retiredSpectraPreviews_;
    void retireSpectraPreview(std::unique_ptr<spectra::GpuPoolWorld>& pool);
    void collectRetiredSpectraPreviews();
    struct SpectraPreviewRequest {
        fx::EffectDef effect;
        std::array<float,8> params{};
        fx::float3 position{};
        bool pending=false, inGame=false, loop=false;
    } spectraPreviewRequest_;
    bool spectraPreviewStop_=false, spectraPreviewFrameOpen_=false;
    bool spectraPreviewInGame_=false;
    uint64_t spectraPreviewSimulatedFrame_=~uint64_t{0};
    bool spectraPreviewLoop_=false;
    bool spectraPreviewPaused_=false,spectraPreviewFrozen_=false;
    float spectraPreviewSpeed_=1.f; // visual preview only, not ECS
    uint32_t spectraPreviewStepRequests_=0; // bounded to 300 requests
    bool spectraPreviewRestart_=false;
    double spectraPreviewVisualSeconds_=0;
    std::string spectraPreviewLastError_;
    SpectraGpuCaptureState spectraGpuCaptureState_=SpectraGpuCaptureState::Idle;
    std::string spectraGpuCaptureNote_;
    std::deque<SpectraGpuPreviewFrameRecord> spectraPreviewHistory_;
    double spectraPreviewNextBurst_=0;
    std::array<float,8> spectraPreviewParams_{};
    fx::float3 spectraPreviewPosition_{};
    fx::ParticleSystem gameFx_;
    game::GameFx gameFxBridge_;
    render::TextureCache fxTextures_;          // sprite textures of effects (same loader as sprites)
    double fxTime_ = -1;
    struct FxPreview { std::string name; fx::float3 pos{}; float scale = 1; fx::float4 tint{1, 1, 1, 1}; bool loop = false; fx::FxHandle h; double next = 0;
                       bool moving = false; float moveT = 0;
                       std::array<float,8> programParams{}; }; // preserved across preview loop restarts   // moving: circles 2 m around pos (trails, perMeter)
    FxPreview fxPreview_;                      // fx_preview: the effect an agent is looking at
    bool fxFrozen_ = false;                    // fx_preview {freeze}: particles hold still for screenshots
    std::unique_ptr<Simulation> sim_;
    std::vector<game::ComputerPlayer> bots_;
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
    void readLiveWorld(Simulation& live, game::GameCore& mod);
    void ensureWorld();                        // MCP: rows (with script data) current now
    int rulesRevision_ = 0;
    const Simulation* liveSim_ = nullptr;      // live world last read (setup refs belong to it)
    std::vector<uint64_t> liveLinks_, liveSetup_;
    std::vector<uint64_t> playDocLinks_;       // editor play-test: map entity -> live entity (set at startPlay)
    Selection hoverNext_;
    double scriptsCheckAt_ = 0;
    uint64_t scriptsStamp_ = 0;

    net::LineServer server_{net::LineServer::Token::Publish};   // agent link: session token file (docs/mcp-agent-link.md)
    struct PendingShot { int client; int64_t id; std::string device; Json metadata; };
    std::vector<PendingShot> shots_;
    bool captureArmed_ = false;
    // The graphics backend can call captureNextFrame asynchronously after
    // Editor shuts down. A shared liveness token prevents a stale callback
    // from dereferencing this or touching the closed MCP connection.
    std::shared_ptr<std::atomic<bool>> captureLifetime_ =
        std::make_shared<std::atomic<bool>>(true);
    bool overlayKeys_[int(platform::Key::Count)] = {};
};

} // namespace aiverse::editor
