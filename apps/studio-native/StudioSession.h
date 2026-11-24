// UI-independent editor API for native HTML5 hosts; intentionally no ImGui.
#pragma once
#include <string>
#include <future>
#include <optional>
#include <map>
#include <set>
#include <vector>
#include <filesystem>
#include <functional>
#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioFacialPreviewBinding.h"
#include "engine/character/vivid/HumanCreatorWorkbench.h"
#include "apps/editor/core/Workspace.h"
#include "apps/studio-native/StudioWindPreview.h"
#include "apps/editor/core/Net.h"
#include "apps/editor/core/WizardAgent.h"
#include "apps/studio-native/StudioLuaPanel.h"
namespace aiverse::studio {
class StudioSession {
public:
    explicit StudioSession(const std::string& engineRoot, const std::string& project = {});
    Json dispatch(const Json& request);   // labels undo steps for the History panel, then dispatchImpl
    const editor::Workspace& workspace() const { return ws_; }
    int selectedEntityIndex() const;
    const std::string& selectedObjectId() const { return selected_; }
    // Multi-selection (primary = selectedObjectId(), always last) and editor-only
    // Hierarchy flags: hidden entities are not drawn or picked, locked ones not picked.
    std::vector<std::string> selectedObjectIds() const;
    const std::set<std::string>& hiddenIds() const { return hidden_; }
    const std::set<std::string>& lockedIds() const { return locked_; }
    const std::string& previewMaterialName() const { return previewMaterialName_; }
    const Json& previewMaterialFactors() const { return previewMaterialFactors_; }
    uint64_t previewMaterialRevision() const { return previewMaterialRevision_; }
    // «Ветер» panel: preview wind + rig debug view (session only, not saved).
    const StudioWindState& windPreview() const { return wind_; }
    void setWindSceneStats(Json stats) { windSceneStats_ = std::move(stats); }
    Json saveMapAs(const std::string& path); // trusted native Save panel only
    const StudioFacialPreviewBinding& facialPreviewBinding() const noexcept {
        return facialPreview_;
    }
    // Viewport sculpt brush (Human Creator "Sculpt" tab). While armed, a
    // viewport click on the character is a stroke, not a selection.
    // regionPick: viewport clicks only report the face point (region choice
    // for the face blend) instead of sculpting.
    struct SculptBrush { bool armed=false; float radius=.12f, strength=.012f; bool pull=false; bool symmetry=true; bool regionPick=false; };
    const SculptBrush& sculptBrush() const noexcept {return sculpt_;}
    void setFacialPreviewClips(const std::vector<std::string>& names,float baseHeight){facialPreview_.setClips(names);facialPreview_.setBaseHeight(baseHeight);}
    void setFacialPreviewGpuStatus(bool rendered,const std::string& reason) {
        facialPreview_.setGpuStatus(rendered,reason);
    }
    Json openProject(const std::string& path); // only call with path returned by a trusted native picker
    // aiversebuild used by the Build panel (resolved by the native host next to Studio).
    void setBuildTool(const std::string& path) { ws_.buildTool = path; }
    // Agent panel: aiversemcp built next to Studio (the in-editor agent reaches this session through it).
    void setMcpTool(const std::string& path) { mcpTool_ = path; }
    // Agent link (docs/mcp-agent-link.md): serves MCP tool calls from aiversemcp; call every frame.
    // True when an agent edit changed the document (the host then refreshes the Scene and panels).
    bool pollAgentLink();
    // view_screenshot from the agent: the host captures the Scene's next frame and answers here
    // (png "" + error when there is nothing to capture).
    // Scene brush (Terrain / Tiles / Fields / Roads / Water panels → Scene): while armed, a Scene drag
    // edits the map. Dab kinds (terrain, paint, tiles, field, objects) apply live; path kinds (road, river)
    // collect points and apply on release. Either way the stroke is one History step.
    struct SceneBrush {
        bool armed=false; std::string kind="terrain";
        std::string op="raise"; double radius=4, strength=1;   // terrain sculpt; paint uses radius/strength + layer
        int layer=0; std::string tile, field, fieldMode="add"; double value=1;
        std::string type; int owner=0, count=1; double spread=1;
        std::string roadType; double width=6;
        std::string caveMode="carve"; double caveY=-5; bool caveSurface=true;
    };
    const SceneBrush& sceneBrush() const noexcept { return brush_; }
    // Spline editing (Roads / Water panels): "road" or "river" shows the network's handles in the Scene;
    // a handle dragged and released moves that road node / river point (one History step).
    const std::string& splineEdit() const noexcept { return splineEdit_; }
    Json splineHandles();   // {handles:[{id,x,z}], segments:[[x0,z0,x1,z1],...]}
    bool splineMove(const std::string& id, double x, double z, std::string* error);
    bool caveBrushPoint(double ox,double oy,double oz,double dx,double dy,double dz,double& x,double& y,double& z,std::string* error);
    bool strokeBegin(double x, double z, std::string* error,double y=0);
    bool strokeDab(double x, double z, std::string* error,double y=0);
    bool strokeEnd(std::string* error = nullptr);
    // Stats panel: the host reports the Scene's frame time (ms, smoothed) and its frame count.
    void setSceneFrameStats(double ms, uint64_t frames) { sceneFrameMs_=ms; sceneFrames_=frames; }
    // Scene overlay shapes from editor/extensions.lua scene_gizmos(ctx) (empty without a project or script).
    Json extensionGizmos() { return ws_.project.dir.empty() ? Json::array() : lua_.gizmos(ws_.project.dir,luaContext()); }
    bool agentScreenshotPending() const { return !agentShots_.empty(); }
    void finishAgentScreenshot(const std::string& pngBase64, int width, int height, const std::string& error);
    // Host-provided material slot names of a loaded content model (the native
    // Scene owns model loading). Without it, slots come from authored overrides.
    using MaterialSlotProvider=std::function<std::vector<std::string>(const std::string& asset)>;
    void setMaterialSlotProvider(MaterialSlotProvider provider) { materialSlots_=std::move(provider); }
    Json previewExternalAssets(const std::vector<std::filesystem::path>& files,
                               const std::string& destination); // native picker ONLY
private:
    Json snapshot() const;
    Json buildRequest(const std::string& method, const Json& params);
    Json dispatchImpl(const Json& request);
    // History panel: a label per MapDoc undo / redo snapshot (MapDoc keeps only snapshots).
    std::vector<std::string> undoLabels_, redoLabels_;
    static std::string historyLabel(const std::string& method, const Json& params);
    void syncHistoryLabels();
    Json historyJson() const;
    Json propertiesFor(const std::string& entityId) const;
    Json componentCatalog(const std::string& entityId) const;
    editor::DocEntity promotedAuthored(int index) const;
    Json inspectorFor(const std::string& entityId) const; // entity header + components
    Json setMeshRenderer(int index, const std::string& key, const Json& value);
    MaterialSlotProvider materialSlots_;
    int indexFor(const std::string& id) const;
    editor::Workspace ws_;
    std::string selected_;
    std::vector<std::string> selection_;          // extra selected ids (primary kept in selected_)
    std::set<std::string> hidden_, locked_;
    void selectOnly(const std::string& id) { selected_=id; selection_.clear(); }
    void clearSelection() { selected_.clear(); selection_.clear(); }
    bool documentHas(const std::string& id) const;
    void dropOrdinalFlags(); // positional ids shift after structural edits
    StudioFacialPreviewBinding facialPreview_;
    vivid::HumanCreatorWorkbench humanCreator_;
    SculptBrush sculpt_;
    std::map<std::string,Json> wardrobeInfoCache_; // humanWardrobeInfo by path+mtime
    std::future<Json> humanBuild_;
    // MetaHuman adapter runs off the UI thread (large exports take seconds).
    std::future<Json> metaHumanJob_;std::string metaHumanKey_;Json metaHumanResult_;
    Json humanBuildResult_=Json::object();
    std::string humanBuildProject_;
    std::string previewMaterialName_;
    Json previewMaterialFactors_;
    uint64_t previewMaterialRevision_ = 0;
    void refreshMaterialPreview();
    std::optional<StudioTransferPlan> pendingAssetTransfer_;
    StudioWindState wind_;
    Json windSceneStats_;
    // Agent panel (studio.agent*): link server, the Claude Code run, its transcript and change list.
    editor::net::LineServer agentServer_{editor::net::LineServer::Token::Publish};
    editor::AgentRun agentRun_;
    std::string mcpTool_, agentSession_;
    std::vector<std::string> agentLines_;
    Json agentChanges_=Json::array();
    int64_t agentRunStart_=-1;
    struct AgentShot { int client; Json id; };
    std::vector<AgentShot> agentShots_;
    SceneBrush brush_;
    std::string splineEdit_;
    StudioLuaPanel lua_;   // Extensions panel (editor/extensions.lua)
    StudioLuaContext luaContext() const;
    Json sceneBrushJson() const;
    Json strokePoints_=Json::array(); int strokeDabs_=0;
    double sceneFrameMs_=0; uint64_t sceneFrames_=0;
    size_t strokeDepth_=0; bool stroking_=false; double strokeX_=0, strokeY_=0, strokeHeight_=0;   // History position when the current conversation started (Undo all)
    Json agentRequest(const std::string& method, const Json& params);
};
Json hubTemplatesJson(const std::string& engineDir, const Json& params);
}
