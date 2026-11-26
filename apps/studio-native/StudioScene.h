#include <tuple>
#include "engine/bake/CapturedBake.h"
// macOS-only native preview: AIVERSE gfx::Device + Renderer3D over a CAMetalLayer.
// No ImGui, no CPU texture readback, no dependency on UI component hierarchy.
#pragma once
#include "apps/studio-native/StudioMapAreaBounds.h"
#include "engine/fx/SpectraSdfFileIO.h"
#include <chrono>
#include <memory>
#include <optional>
#include <cstdint>
#include <limits>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <filesystem>
#include <deque>
#include <mutex>
#include <chrono>
#include <optional>
#include "apps/studio-native/StudioRegionBorders.h"
#include "apps/studio-native/StudioModelPicking.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/studio-native/StudioPlanetPreview.h"
#include "apps/studio-native/StudioPlanetProcessOverlays.h"
#include "apps/studio-native/StudioPlanetForestOverlays.h"
#include "apps/studio-native/StudioForestCameraFocus.h"
#include "apps/studio-native/StudioPlanetForestTextures.h"
#include "apps/studio-native/StudioPlanetRegionEditing.h"
#include "apps/studio-native/StudioPlanetGravityEditing.h"
#include "apps/studio-native/StudioAuthoredLights.h"
#include "apps/editor/core/MapDoc.h"
#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/NereidManifestHost.h"
#include "engine/render/Prisma.h"
#include "engine/render/RenderLightingRuntime.h"
#include "engine/world/NereidManifestHost.h"
#include "apps/studio-native/StudioWindPreview.h"
#include "engine/render/PostFx.h"
#include "engine/render/AdaptiveShadowResolution.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "apps/studio-native/StudioFacialGpuPreview.h"
#include "apps/studio-native/StudioCavernsPreview.h"
#include "apps/studio-native/StudioPlanetGlobe.h"
#include "engine/render/NativePresentationRetirement.h"
#include "apps/editor/TerraWeaveSparsePreview.h"
#include "apps/studio-native/StudioSpectraPreview.h"
#include "apps/studio-native/StudioSpectraSources.h"

namespace aiverse::editor { class Workspace; class TextureGraphGpuPreview; }
namespace aiverse::studio {
class StudioScene final {
public:
    // The host makes the device for its native surface (Metal: CAMetalLayer; Vulkan: an HWND).
    explicit StudioScene(std::unique_ptr<gfx::Device> device);
    ~StudioScene();
    Json textureGraphCapabilities() const;
    Json textureGraphOperation(editor::Workspace& ws,const Json& params);
    bool serviceTextureGraph(const std::string& project);
    void invalidateTextureGraphProject();
    StudioScene(const StudioScene&) = delete;
    StudioScene& operator=(const StudioScene&) = delete;
    // Render-thread-only: ephemeral generated building preview, never an Asset.
    // On replace/clear the old GPU model retires after a verified device idle.
    bool setBuildingPreview(std::unique_ptr<render::Model> model,
                            std::string* error=nullptr);
    bool hasBuildingPreview() const noexcept {return bool(buildingPreview_)&&buildingPreviewGpuReady_;}
    void frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
               bool fpsProject, const std::string& selectedId,
               const std::string& previewName, const Json& previewFactors,
               uint64_t previewRevision, float levelHeight = 1.5f,
               const std::string& projectPath = {}, const Json& rules = Json::object(),
               const StudioFacialPreviewBinding* facialBinding=nullptr);
    // Called only through StudioHost::onScene, between render-thread frames.
    // A real SpectraFX graph runs on the native Device in the Scene pass.
    // The same native Scene CAMetalLayer can be hosted inside the HTML5
    // SpectraFX Graph pane. Preserve the authored-map camera while the FX
    // workspace uses its own isolated camera and never renders map entities.
    void setSpectraDockMode(bool enabled) {
        if (spectraDockMode_==enabled)return;
        if (enabled) {
            sceneCameraBeforeSpectra_=camera_;
            sceneCameraInitializedBeforeSpectra_=cameraInitialized_;
            camera_.target={0,1.0f,0};
            camera_.distance=6.5f;
            camera_.yaw=0.45f;camera_.pitch=0.23f;camera_.ortho=false;
            cameraInitialized_=true;
        } else {
            camera_=sceneCameraBeforeSpectra_;
            cameraInitialized_=sceneCameraInitializedBeforeSpectra_;
        }
        spectraDockMode_=enabled;
        tween_.active=false;
        lastFrameSignature_=0;
        settleFrames_=30;
    }
    bool spectraDockMode() const {return spectraDockMode_;}
    void setSpectraEngineAssetRoot(std::string root){spectraEngineAssetRoot_=std::move(root);}
    // Explicit native scene-host hook; publish after document activation and
    // before frame preparation. Isolated FX preview has independent data.
    bool setSpectraSceneDataRegistry(std::shared_ptr<const spectra::DataInterfaceRegistry> registry) {
        return spectraSources_.setDataInterfaceRegistry(std::move(registry));
    }
    void resetSpectraDockCamera() {
        if(!spectraDockMode_)return;
        camera_.target={0,1.0f,0};
        camera_.distance=6.5f;
        camera_.yaw=0.45f;camera_.pitch=0.23f;camera_.ortho=false;
        cameraInitialized_=true;
        tween_.active=false;
        lastFrameSignature_=0;settleFrames_=30;
    }
    Json spectraPreviewCommand(const Json& input,const std::string& project,bool frameProvenance=false) {
        spectraPreview_->setTextureBases(project.empty()?std::string{}:project+"/",spectraEngineAssetRoot_);
        const auto action=input.getStr("action","status");
        if(!frameProvenance){
            if(project!=spectraProject_){
                discardReplayBackup();
                if(spectraPreview_->active())spectraPreview_->stop(*device_);
                replayAttemptGeneration_=0;replayOutcome_.clear();replayError_.clear();
            }
            spectraProject_=project;
            finishReplayTransaction();
            if(action=="stop"){
                discardReplayBackup();
                replayAttemptGeneration_=0;replayOutcome_.clear();replayError_.clear();
            }
            if(action=="replayCancel"&&spectraReplayBackup_){
                rollbackReplayTransaction("cancelled","");
                return spectraPreviewCommand(Json::parse(R"({"action":"status"})"),project);
            }
        }
        if(action!="status"&&action!="stop"&&action!="replayCancel"&&(spectraReplayBackup_||spectraPreview_->replayPending())){
            Json out=spectraPreview_->status();
            out.set("error",Json::string("Replay restore pending; cancel or wait"));
            return out;
        }
        if(action=="replayRestore") {
            if(!spectraDockMode_)throw std::runtime_error("Replay restore requires FX stage");
            const auto background=input.getStr("background");
            if((background!="dark"&&background!="neutral"&&background!="light"&&background!="black")||
               input["grid"].type()!=Json::Type::Bool||input["axes"].type()!=Json::Type::Bool)
                throw std::runtime_error("Invalid replay stage");
            const Json& c=input["camera"];
            auto read=[&](const char* key,double limit){
                const Json& v=c[key];
                if(!v.isNumber()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>limit)
                    throw std::runtime_error(std::string("Invalid replay camera field: ")+key);
                return float(v.toDouble());
            };
            if(!c.isObject()||c["ortho"].type()!=Json::Type::Bool)
                throw std::runtime_error("Invalid replay camera");
            const float yaw=read("yaw",1000),pitch=read("pitch",1.56),distance=read("distance",100000);
            const float x=read("targetX",1000000),y=read("targetY",1000000),z=read("targetZ",1000000);
            if(distance<0.01f)throw std::runtime_error("Invalid replay camera distance");
            const double speed=input["speed"].toDouble();
            if(!input["speed"].isNumber()||input["loop"].type()!=Json::Type::Bool||
               (speed!=0.25&&speed!=0.5&&speed!=1.0&&speed!=2.0&&speed!=4.0))
                throw std::runtime_error("Invalid replay playback settings");
            if(!spectraPreview_->canBeginReplayTransaction())
                throw std::runtime_error("Checkpoint or replay operation pending");
            const Json graph=input.has("graph")?input["graph"]:spectraPreview_->authoredGraph();
            if(!graph.isObject()||graph.getInt("version")!=2)
                throw std::runtime_error("Replay requires a loaded Graph v2");
            // Candidate owns its own runtime but borrows the same native Device.
            // No live state is changed by validation or backend failure.
            auto candidate=std::make_unique<StudioSpectraPreview>();
            candidate->setTextureBases(project.empty()?std::string{}:project+"/",spectraEngineAssetRoot_);
            Json load=Json::object();load.set("action",Json::string("load"));
            load.set("graph",graph);
            load.set("gpuBirthRandom",Json::fromBool(input.has("gpuBirthRandom")?
                input.getBool("gpuBirthRandom"):spectraPreview_->authoredGpuBirthRandom()));
            if(input.has("gpuOwnedAllocation")&&input["gpuOwnedAllocation"].type()!=Json::Type::Bool)
                throw std::runtime_error("gpuOwnedAllocation must be boolean");
            load.set("gpuOwnedAllocation",Json::fromBool(input.has("gpuOwnedAllocation")?
                input.getBool("gpuOwnedAllocation"):spectraPreview_->authoredGpuOwnedAllocation()));
            const auto loaded=candidate->command(*device_,load,fx::projectSdfReader(project));
            Json prepare=input;prepare.set("action",Json::string("replayPrepare"));
            const auto prepared=loaded.getBool("ready")&&loaded.getStr("error").empty()?
                candidate->command(*device_,prepare):loaded;
            if(!prepared.getBool("ready")||!prepared.getStr("error").empty()){
                candidate->stop(*device_);
                Json rejected=spectraPreviewCommand(Json::parse(R"({"action":"status"})"),project);
                rejected.set("error",Json::string(prepared.getStr("error")));
                return rejected;
            }
            replayBackupCamera_=camera_;replayBackupCameraInitialized_=cameraInitialized_;
            replayBackupTween_=tween_;
            replayBackupBackground_=fxBackground_;replayBackupGrid_=fxGrid_;replayBackupAxes_=fxAxes_;
            spectraReplayBackup_=std::move(spectraPreview_);
            spectraPreview_=std::move(candidate);
            replayAttemptGeneration_=uint64_t(prepared["generation"].integer());
            replayOutcome_="pending";replayError_.clear();
            fxBackground_=background;fxGrid_=input.getBool("grid");fxAxes_=input.getBool("axes");
            camera_.yaw=yaw;camera_.pitch=pitch;camera_.distance=distance;
            camera_.target={x,y,z};camera_.ortho=c.getBool("ortho");cameraInitialized_=true;
            tween_.active=false;lastFrameSignature_=0;settleFrames_=30;
            return spectraPreviewCommand(Json::parse(R"({"action":"status"})"),project);
        }
        if(action=="stage") {
            const auto background=input.getStr("background");
            if(background!="dark" && background!="neutral" &&
               background!="light" && background!="black")
                throw std::runtime_error("FX stage background must be dark, neutral, light or black");
            if(input["grid"].type()!=Json::Type::Bool ||
               input["axes"].type()!=Json::Type::Bool)
                throw std::runtime_error("FX stage grid and axes must be booleans");
            fxBackground_=background;
            fxGrid_=input.getBool("grid");
            fxAxes_=input.getBool("axes");
        }
        if(action=="cameraPreset") {
            if(!spectraDockMode_)throw std::runtime_error("FX camera preset requires the FX stage");
            const auto preset=input.getStr("preset");
            if(preset!="perspective" && preset!="front" &&
               preset!="side" && preset!="top")
                throw std::runtime_error("FX camera preset must be perspective, front, side or top");
            resetSpectraDockCamera();
            if(preset=="front"){camera_.yaw=0;camera_.pitch=0;camera_.ortho=true;}
            else if(preset=="side"){camera_.yaw=1.5707963f;camera_.pitch=0;camera_.ortho=true;}
            else if(preset=="top"){camera_.yaw=0;camera_.pitch=1.54f;camera_.ortho=true;}
        }
        if(action=="cameraRestore") {
            if(!spectraDockMode_)throw std::runtime_error("FX camera restore requires FX stage");
            const Json& c=input["camera"];
            if(c.type()!=Json::Type::Object)throw std::runtime_error("FX camera must be object");
            auto read=[&](const char* key,double limit)->float {
                const Json& v=c[key];
                if(v.type()!=Json::Type::Number || !std::isfinite(v.toDouble()) || std::abs(v.toDouble())>limit)
                    throw std::runtime_error(std::string("Invalid FX camera field: ")+key);
                return static_cast<float>(v.toDouble());
            };
            const float yaw=read("yaw",1000),pitch=read("pitch",1.56),distance=read("distance",100000);
            const float x=read("targetX",1000000),y=read("targetY",1000000),z=read("targetZ",1000000);
            if(distance<0.01f || c["ortho"].type()!=Json::Type::Bool)
                throw std::runtime_error("Invalid FX camera distance/projection");
            camera_.yaw=yaw;camera_.pitch=pitch;camera_.distance=distance;
            camera_.target={x,y,z};camera_.ortho=c.getBool("ortho");cameraInitialized_=true;
        }
        if(action=="stage"||action=="cameraPreset"||action=="cameraRestore"||action=="status") {
            const Json state=spectraPreview_->command(*device_,Json::parse(R"({"action":"status"})"));
            Json out=state;
            out.set("background",Json::string(fxBackground_));
            out.set("grid",Json::fromBool(fxGrid_));
            out.set("axes",Json::fromBool(fxAxes_));
            Json camera=Json::object();
            camera.set("yaw",Json::number(camera_.yaw));
            camera.set("pitch",Json::number(camera_.pitch));
            camera.set("distance",Json::number(camera_.distance));
            camera.set("targetX",Json::number(camera_.target.x));
            camera.set("targetY",Json::number(camera_.target.y));
            camera.set("targetZ",Json::number(camera_.target.z));
            camera.set("ortho",Json::fromBool(camera_.ortho));
            out.set("camera",std::move(camera));
            out.set("replayAttemptGeneration",Json::number(int64_t(replayAttemptGeneration_)));
            out.set("replayOutcome",Json::string(replayOutcome_));
            out.set("replayError",Json::string(replayError_));
            if(action!="status") {lastFrameSignature_=0;settleFrames_=30;}
            return out;
        }
        Json result=spectraPreview_->command(*device_,input,fx::projectSdfReader(project));
        result.set("background",Json::string(fxBackground_));
        result.set("grid",Json::fromBool(fxGrid_));
        result.set("axes",Json::fromBool(fxAxes_));
        // Preview spawns near the current Scene camera target; never steal
        // the user's editor camera or change map editing navigation.
        lastFrameSignature_=0;settleFrames_=30;
        return result;
    }
    void captureNextFrame(gfx::Device::CaptureFn callback) { lastFrameSignature_=0; device_->captureNextFrame(std::move(callback)); }
    void setCaveGlobe(std::string body,const std::string& project,const std::string& map);
    bool caveGlobeRequested()const noexcept{return !globeBody_.empty();}
    Json pickCaveGlobe(float x,float y,float width,float height,const editor::MapDoc& doc,
                       const std::string& project)const;
    Json cavernsPreview(const Json& input) {
        if(input.has("enabled")||input.has("isolate")||input.has("paging")||input.has("budgets")) {
            if(!device_||!cavernsPreview_.configure(*device_,input))
                throw std::runtime_error("Caverns preview GPU retirement refused");
            lastFrameSignature_=0;planetCutDrawn_=false;
        }
        auto result=cavernsPreview_.stats();
        result.set("globeBody",Json::string(globeBody_));
        result.set("globeReady",Json::fromBool(globeReady_));
        result.set("globeError",Json::string(globeError_));
        const auto shown=planetCutDrawn_&&planetCutPlan_?planetCutPlan_->acceptedEntrances.size():size_t(0);
        result.set("terrainCutEntrances",Json::string(std::to_string(terrainCutStats_.cutEntrances+shown)));
        result.set("terrainRejectedEntrances",Json::string(std::to_string(terrainCutStats_.rejectedEntrances-
            std::min(shown,terrainCutStats_.rejectedEntrances))));
        result.set("terrainTriangles",Json::string(std::to_string(planetCutDrawn_?planetCutTriangles_:terrainCutStats_.triangles)));
        const auto& rimDiagnostic=cavernsPreview_.planetRimDiagnostic();
        result.set("terrainCutDiagnostic",Json::string(!planetCutError_.empty()?planetCutError_:
            !rimDiagnostic.empty()?rimDiagnostic:planetCutDrawn_?"Paired chart surface and collar":terrainCutStats_.cutDiagnostic));
        result.set("terrainCutSupported",Json::fromBool(terrainCutStats_.handled));
        result.set("sparseTerrainResident",Json::string(std::to_string(sparseTerrainPreview_.residentCount())));
        result.set("sparseTerrainAuthored",Json::string(std::to_string(sparseTerrainPreview_.authoredCount())));
        result.set("sparseTerrainDiagnostic",Json::string(sparseTerrainError_.empty()?sparseTerrainPreview_.surfaceGroupDiagnostic():sparseTerrainError_));
        result.set("sparseCertifiedEdges",Json::string(std::to_string(sparseTerrainPreview_.certifiedSurfaceEdges())));
        result.set("sparsePlannedOpenings",Json::string(std::to_string(sparseTerrainPreview_.plannedSurfaceOpenings())));
        return result;
    }
    using FxTaggedCaptureFn=std::function<void(uint64_t,int,int,std::vector<uint8_t>,Json)>;
    void captureNextFrameTagged(FxTaggedCaptureFn callback) {
        lastFrameSignature_=0;
        device_->captureNextFrameTagged([history=fxFrameProvenance_,callback=std::move(callback)](uint64_t serial,int w,int h,std::vector<uint8_t> pixels){
            Json metadata;
            for(const auto& entry:*history)if(entry.first==serial){metadata=entry.second;break;}
            if(callback)callback(serial,w,h,std::move(pixels),std::move(metadata));
        });
    }
    uint64_t fxFrameSerial() const {return device_?device_->frameSerial():0;}
    uint64_t fxCompletedFrameSerial() const {return device_?device_->completedFrameSerial():0;}
    uint64_t fxSubmissionFence() const {return device_?device_->gpuSubmissionFence():0;}
    uint64_t fxCompletedFence() const {return device_?device_->gpuCompletedFence():0;}
    std::string shaderPreviewError() const;
    const std::string& lightingStatus() const { return lightingStatus_; }
    // Preview wind for every rigged model (<model>.wind.json) + debug view.
    void setWindPreview(const StudioWindState& state) { wind_ = state; }
    Json windStats() const;
    bool facialReady() const noexcept{return facialReady_;}
    std::vector<std::string> facialClips() const {return facialGpu_.clipNames();}
    float facialBaseHeight() const {return facialGpu_.baseHeight();}
    Json alignMakeup(const StudioFacialPreviewBinding& b) const {return facialGpu_.alignMakeup(b);}
    const std::string& facialError() const noexcept{return facialError_;}
    // Facial preview sculpt pick (normalized head coordinates); false = miss.
    // Never throws: a rejected pick returns false with the reason in error.
    bool sculptPick(float x,float y,float width,float height,
                    const vivid::HumanFaceShape& selection,vivid::HumanFaceHit& hit,
                    std::string* error=nullptr) const noexcept;
    // Next shown character frame → square PNG around the head (preset cards).
    // crop=false: whole viewport (agent snapshots), at most 1280 px wide.
    void captureFacialThumbnail(std::string path,bool crop=true){thumbnailPath_=std::move(path);thumbnailCrop_=crop;}
    // Assets panel thumbnails: a content model rendered offscreen (one per frame) and written as a PNG.
    // Finished assets (written or failed) are reported once by takeFinishedThumbnails().
    void requestThumbnail(const std::string& projectPath, const std::string& asset, const std::string& file);
    std::vector<std::string> takeFinishedThumbnails();
    std::string pick(float x, float y, float width, float height,
                     const editor::MapDoc& map, bool fpsProject) const;
    void orbit(float dx, float dy);
    void zoom(float wheel);
    void pan(float dx, float dy);
    void look(float dx, float dy);
    void move(float right, float up, float forward);
    void focusSelected(const editor::MapDoc& map, bool fpsProject, const std::string& selectedId);
    void resetView();
    // Unity-style Scene view helpers: animated orbit to an axis view and the
    // Persp/Iso toggle. Orientation/projection are editor camera state only.
    void setOrientation(float yaw, float pitch);
    void setOrthographic(bool ortho);
    void setFieldOfView(float radians) { camera_.fovY = radians; }
    struct CameraState { float yaw = 0, pitch = 0, distance = 0; bool ortho = false; render::float3 target{}; float fov = 0; };
    void requestLightFocus(render::float3 position) { pendingFocusLight_=position; }
    bool hasSceneCamera() const { return cameraInitialized_; }
    CameraState cameraState() const { return {camera_.yaw, camera_.pitch, camera_.distance, camera_.ortho, camera_.target, camera_.fovY}; }
    void setPlanetOrbitCamera(const CameraState& state){
        tween_.active=false;camera_.target=state.target;camera_.distance=state.distance;
        camera_.yaw=state.yaw;camera_.pitch=state.pitch;camera_.ortho=state.ortho;autoIso_=false;cameraInitialized_=true;
    }
    // Corner orientation gizmo rendered by Renderer3D, for the default host where
    // the Metal view covers the WebView (the HTML gizmo is used in composite mode).
    void setOrientationGizmo(bool enabled) { orientationGizmo_ = enabled; }
    // Hit test in the same units as width/height: -1 none, 0..5 arm
    // (axis*2 + negative), 6 centre (projection toggle).
    int orientationGizmoHit(float x, float y, float width, float height) const;
    // Applies a hit: snaps to the axis view (auto Iso) or toggles the projection.
    void activateOrientationGizmo(int hit);
    // Transform tools on the selected object (Unity Q/W/E/R). Coordinates in the
    // caller's view units (points) with the view size; same as pick().
    enum class TransformTool { View = 0, Move = 1, Rotate = 2, Scale = 3 };
    void setTransformTool(TransformTool tool);
    TransformTool transformTool() const { return tool_; }
    void setLocalSpace(bool local) { localSpace_ = local; }
    bool localSpace() const { return localSpace_; }
    void setSnap(bool enabled, float move, float rotate, float scale);
    void syncStaticGeometry(const std::string& project,const std::string& file);
    Json staticGeometryReport(const std::string& project,const std::string& file) const;
    void setPlanetTerrainLook(const std::shared_ptr<const game::TerrainLook>& look){
        if(planetTerrainLook_!=look){planetTerrainLook_=look;planetRevision_=std::numeric_limits<uint64_t>::max();}
    }
    bool baseTerrainView()const {return baseTerrainView_;}
    void setBaseTerrainView(bool base){
        if(baseTerrainView_==base)return;
        baseTerrainView_=base;
        planetRevision_=std::numeric_limits<uint64_t>::max();
        terrainRevision_=std::numeric_limits<uint64_t>::max();
        lightingRevision_=std::numeric_limits<uint64_t>::max();
        lastFrameSignature_=0;
    }
    void setRegionBorders(char letter) {
        if(regionBorderLetter_!=letter){regionBorderLetter_=letter;regionBordersDirty_=true;}
    }
    void setGizmosVisible(bool visible) { gizmosVisible_ = visible; }
    // Hold V while moving: the object's pivot snaps to the nearest other object's pivot on screen.
    void setPivotSnap(bool on) { pivotSnap_ = on; }
    // ⇧F: fit every document object into the view.
    void frameAll(const editor::MapDoc& map, bool fpsProject);
    void tweenCamera(render::float3 target, float distance, float yaw, float pitch) { startTween(target, distance, yaw, pitch); }
    // -1 none, 0..2 axis, 3 centre (uniform scale), 4..6 Move plane (normal X, Y, Z: YZ, XZ, XY).
    int transformGizmoHit(float x, float y, float width, float height) const;
    bool beginTransformDrag(float x, float y, float width, float height);
    // invertSnap: Ctrl/Cmd held flips the snap setting for this drag step.
    void updateTransformDrag(float x, float y, float width, float height, bool invertSnap);
    struct TransformCommit { std::string id; TransformTool tool = TransformTool::Move; int axis = -1;
        render::float3 move{}; float degrees = 0; float factor = 1; Json surfacePlacement; std::optional<uint64_t> expectedRevision; };
    std::optional<TransformCommit> endTransformDrag();
    bool transformDragActive() const { return drag_.axis >= 0 || !planetDragId_.empty(); }
    void clearTransformPreview();
    std::string transformCaption() const; // "Move X +1.25" while dragging, else empty
    // Scene model streaming after a project/map opens: models load within a per-frame
    // budget so the editor stays responsive; the host reports this as load progress.
    struct LoadProgress { int done = 0, total = 0; std::string current; };
    const LoadProgress& modelLoadProgress() const { return loadProgress_; }
    // Editor selection state supplied by the host before each frame: extra selected
    // ids (outlined), hidden ids (not drawn, not picked), locked ids (not picked), hover.
    void setEditorState(std::set<std::string> selection, std::set<std::string> hidden,
                        std::set<std::string> locked, std::string hover) {
        selection_=std::move(selection); hidden_=std::move(hidden); locked_=std::move(locked); hover_=std::move(hover);
    }
    // Marquee rectangle in normalised view coordinates (0..1), drawn over the Scene.
    void setMarquee(bool on, float x0 = 0, float y0 = 0, float x1 = 0, float y1 = 0) { marquee_ = {on, x0, y0, x1, y1}; }
    // Visible, unlocked objects whose pivot projects into the rectangle (view units).
    std::vector<std::string> idsInRect(float x0, float y0, float x1, float y1, float width, float height,
                                       const editor::MapDoc& map, bool fpsProject) const;
    // Ray through (x, y) onto the ground plane y = 0 (asset drops).
    bool caveScreenRay(float x,float y,float width,float height,render::float3& origin,render::float3& direction)const;
    std::optional<Json> playerStartPlacement(float x,float y,float w,float h,const editor::MapDoc& doc) const;
    std::optional<render::float3> flatMapPoint(float x,float y,float w,float h,const editor::MapDoc& doc,bool requireSurface=false) const;
    void showAreaBounds(std::optional<StudioMapAreaBounds> rect){areaBounds_=rect;}
    bool groundPoint(float x, float y, float width, float height, render::float3& out) const;
    std::optional<Json> forestPick(float x,float y,float w,float h,const std::string& project,const std::string& path,uint64_t revision) const;
    void setForestSelection(Json selection){forestSelection_=std::move(selection);}
    bool planetBrushPoint(double x,double y,double w,double h,const editor::MapDoc& doc,render::float3& hit,std::string& body,double& elevation)const;
    std::optional<Json> forestGroundPlacement(float x,float y,float w,float h,const std::string& path,uint64_t revision) const;
    bool beginPlanetRegionDrag(const editor::MapDoc& doc,const std::string& id,float x,float y,float width,float height);
    std::optional<Json> updatePlanetRegionDrag(const editor::MapDoc& doc,float x,float y,float width,float height,bool symmetric,bool snap,bool finish);
    void cancelPlanetRegionDrag(){planetRegionDrag_.reset();}
    bool beginPlanetGravityDrag(const editor::MapDoc& doc,float x,float y,float width,float height);
    std::optional<Json> updatePlanetGravityDrag(const editor::MapDoc& doc,float x,float y,float width,float height,bool snap,bool finish);
    void cancelPlanetGravityDrag(){planetGravityDrag_.reset();}
    // Material slot names of a loaded content model (for the Inspector's Mesh Renderer).
    std::vector<std::string> modelMaterialSlots(const std::string& asset) const;
    // Scene brush ring under the cursor (radius in world units); hidden when visible=false.
    // Texture Resources / Frame Capture panels: live GPU textures (format-based size estimate) and draw calls.
    Json textureResources() const;
    Json prismaFrameReports(const std::string& project, const std::string& map) const;
    Json lensViewPose() const;
    Json configureLensCamera(const Json& params);
    bool lensCameraReady() const {return lensCamera_.has_value();}
    Json attachmentCommand(const std::string& project,const Json& params);
    Json effectFileCommand(const std::string& project,const Json& params);
    void drawEffectFile(int width,int height);
    void drawAttachments(int width,int height);
    void drawAnimationPose(int width,int height,const std::shared_ptr<const render::Model>& source,
                           const render::Pose& pose,float yaw);
    bake::CapturedBake captureLightBake(const editor::MapDoc& doc, const std::string& project, const game::Rules& rules);
    std::optional<bake::CapturedBake> prepareLightBake(const editor::MapDoc& doc,const std::string& project,
        const game::Rules& rules,const Json& rulesJson,float levelHeight);
    void setBakedSceneEnabled(bool enabled);
    Json bakedSceneStatus() const;
    uint32_t drawCalls() const { return renderer_.drawCalls(); }
    // Project extension gizmos (editor/extensions.lua scene_gizmos): world-space lines, circles (screen px), points.
    struct OverlayShape { int kind = 0; render::float3 a{}, b{}; float radius = 8, thickness = 2; uint8_t r = 90, g = 180, bl = 255; std::string text; };   // kind: 1 line, 2 circle, 3 point, 4 label
    void setOverlayShapes(std::vector<OverlayShape> shapes) { overlayShapes_ = std::move(shapes); }
    void setPlanetSceneBody(std::string project,std::string path,std::string body){
        planetBodyProject_=std::move(project);planetBodyPath_=std::move(path);planetBodyOverride_=std::move(body);
        forestSelection_=Json{};forestDragPreview_=Json{};clearTransformPreview();
    }
    std::string planetSceneBodyOverride(const std::string& project,const std::string& path)const{
        return planetBodyProject_==project&&planetBodyPath_==path?planetBodyOverride_:std::string{};
    }
    void setForestDragPreview(Json value){forestDragPreview_=std::move(value);}
    std::string planetSceneBody(const std::string& project,const std::string& path,uint64_t revision)const{
        return planetMode_&&planetPreview_&&planetModelProject_==project&&planetMapPath_==path&&planetRevision_==revision&&
            (planetSceneBodyOverride(project,path).empty()||planetSceneBodyOverride(project,path)==planetPreview_->bodyId)?planetPreview_->bodyId:std::string{};
    }
    std::optional<StudioForestCameraFocus> planetForestCamera(const std::string& project,const std::string& path,uint64_t revision,int width,int height)const{
        const auto body=planetSceneBody(project,path,revision);
        if(body.empty()||!cameraInitialized_)return {};
        StudioForestCameraFocus focus{body,viewCamera(),width,height};
        return focus.valid()?std::optional<StudioForestCameraFocus>(focus):std::nullopt;
    }
    void setPlanetLiveSnapshot(Json snapshot,std::string project,std::string path,uint64_t revision,std::string layer,std::string aetherLayer){
        planetLiveSnapshot_=std::move(snapshot);planetLiveProject_=std::move(project);
        planetLivePath_=std::move(path);planetLiveRevision_=revision;planetLiveLayer_=std::move(layer);planetLiveAetherLayer_=std::move(aetherLayer);
    }
    // World point -> view point (same units as pick / groundPoint); false when behind the camera.
    bool projectPoint(render::float3 world, float width, float height, render::float2& out) const;
    void setBrushCursor(bool visible, render::float3 center, float radius,bool volume=false,bool fill=false) { brushVisible_=visible; brushCenter_=center; brushRadius_=radius; brushVolume_=volume; brushFill_=fill; }
    void resetCamera() { pendingFocusLight_.reset(); if(device_){device_->waitForResourceIdle();spectraSources_.shutdown();} lightingRevision_ = std::numeric_limits<uint64_t>::max(); cameraInitialized_ = false; planetRevision_=std::numeric_limits<uint64_t>::max(); terrainRevision_ = std::numeric_limits<uint64_t>::max(); clearDocumentModels(); }
    // Frames actually drawn / skipped because nothing visible changed (diagnostics).
    uint64_t framesDrawn() const { return framesDrawn_; }
    uint64_t framesSkipped() const { return framesSkipped_; }
    // Default: draw every frame() call and rebuild lighting on every edit (tests,
    // automation, captures). The interactive Studio viewport turns this off: it
    // then skips idle frames and debounces lighting rebuilds during edit bursts.
    void setWeatherPreview(const render::Lighting& lighting, PreviewDayLighting day, float seconds=0) {
        weatherEnvironment_=lighting;
        weatherDay_=day;weatherSeconds_=seconds;
    }
    void setContinuousRedraw(bool on) { continuousRedraw_ = on; }
    // Automation (AIVERSE_STUDIO_WIND_SHOTS): frame a point of interest.
    // Automation (AIVERSE_STUDIO_SKY_SHOTS): the engine's PhysicalSky with a given sun, for the splash art.
    struct SkyShot { float elevationDeg = 8, azimuthDeg = 250, coverage = .5f; render::float3 sunColor{1.f, .85f, .66f}; };
    void setSkyShot(std::optional<SkyShot> shot) { skyShot_ = shot; }
    void setShotCamera(float tx, float ty, float tz, float distance, float yaw, float pitch) {
        camera_.target={tx,ty,tz}; camera_.distance=distance; camera_.yaw=yaw; camera_.pitch=pitch;
        cameraInitialized_=true;
    }
private:
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<render::RenderDeviceLifetime> deviceLifetime_;
    std::unique_ptr<editor::TextureGraphGpuPreview> textureGraph_;
    struct GraphPresentation;
    std::unique_ptr<GraphPresentation> graphPresentation_;
    render::Renderer3D renderer_;
    bake::CapturedBake collectLightBake(const editor::MapDoc& doc,const game::Rules& rules,
        const render::Lighting& environment,float levelHeight,bool terrain);
    struct StaticScenery;
    std::unique_ptr<StaticScenery> staticScenery_;
    struct SceneBake;
    std::unique_ptr<SceneBake> sceneBake_;
    void syncSceneBake(const std::string& project,const std::string& mapPath);

    world::NereidManifestHost nereid_;
    double underwaterFrameTime_=0;
    std::string prismaReportProject_, prismaReportMap_;
    render::Prisma post_;
    struct GraphParticlePreview;
    std::unique_ptr<GraphParticlePreview> graphParticles_;
    std::unique_ptr<render::Prisma> graphPreviewPost_;
    render::RenderLightingRuntime lightingRuntime_;
    std::unique_ptr<render::Model> lightingModel_;
    uint64_t lightingRevision_ = std::numeric_limits<uint64_t>::max();
    std::string lightingStatus_ = "Preview geometry · GI: off · Direct: standard";
    render::Mesh cube_;
    render::Mesh studioCyclorama_;
    render::Mesh terrain_;
    render::Mesh planetTerrain_;
    std::array<render::Mesh,6> planetLiveTerrainFaces_{};
    std::array<Json,6> planetLiveTerrainFaceSignatures_{};
    Json planetLiveTerrainGrid_;
    // CPU triangles from the same successful upload used by the Scene.
    std::optional<render::MeshBuilder> planetLiveTerrainCpu_;
    bool planetLiveTerrainVisible_=false;
    float planetAuthoredOccluderRadius_=0.f,planetLiveOccluderRadius_=0.f;
    const render::MeshBuilder* visiblePlanetTerrain()const{
        return planetLiveTerrainVisible_&&planetLiveTerrainCpu_?&*planetLiveTerrainCpu_:nullptr;
    }
    void clearLivePlanetTerrain();
    bool syncLivePlanetTerrain(const editor::MapDoc& doc,const std::string& projectPath);
    std::optional<StudioPlanetPreview> planetPreview_;
    PlanetProcessOverlays planetProcessOverlays_;
    PlanetForestOverlays planetForestOverlays_;
    Json planetForestOverlayRecipe_;
    Json planetForestModelCatalog_;
    std::string planetForestModelProject_;
    std::map<uint32_t,std::unique_ptr<render::Model>> planetForestModels_;
    std::set<uint32_t> planetForestRejectedModels_;
    std::map<uint32_t,bool> planetForestEvergreen_;
    std::map<std::tuple<uint32_t,int,int>,std::vector<render::Material>> planetForestSeasonMaterials_;
    using ForestRootModelKey=std::tuple<uint32_t,bool,uint32_t>;
    std::map<ForestRootModelKey,std::unique_ptr<render::Model>> planetForestRootModels_;
    std::set<ForestRootModelKey> planetForestRejectedRoots_;
    std::map<uint32_t,std::unique_ptr<render::Model>> planetForestLogModels_;
    std::set<uint32_t> planetForestRejectedLogs_;
    StudioForestTextureStamp planetForestTextureStamp_;
    std::chrono::steady_clock::time_point planetForestTextureCheck_{};
    size_t planetForestModelBytes_=0;
    bool planetForestModelsPending_=false;
    struct ForestFarChunk { render::Mesh mesh; Json rows; float scale=-1.f; size_t bytes=0; };
    std::map<int64_t,ForestFarChunk> planetForestFarChunks_;
    std::string planetForestFarBody_,planetForestFarProject_,planetForestFarPath_,planetForestFarEpoch_;
    uint64_t planetForestFarRevision_=0;
    using ForestVisualId=std::pair<int64_t,std::string>;
    std::map<ForestVisualId,float> planetForestDetailWeights_;
    std::chrono::steady_clock::time_point planetForestBlendTime_{};
    std::string planetForestBlendIdentity_;
    render::Mesh planetForestBlendProxy_;
    void clearPlanetForestFarMesh();
    void drawPlanetForestFarChunks(const Json& rows,float flora,int width,int height);
    void clearPlanetForestModels();
    void syncPlanetForestModels(const editor::MapDoc& doc,const std::string& projectPath);
    bool planetForestLiveCurrent(const editor::MapDoc& doc,const std::string& projectPath)const;
    void drawLivePlanetForestMeshes(const editor::MapDoc& doc,const std::string& projectPath,int width,int height);
    Json forestSelection_,forestDragPreview_;
    std::string planetBodyProject_,planetBodyPath_,planetBodyOverride_;
    Json planetLiveSnapshot_;
    std::string planetLiveProject_,planetLivePath_,planetLiveLayer_,planetLiveAetherLayer_;
    uint64_t planetLiveRevision_=0;
    struct PlanetRegionDrag { PlanetRegionChart chart; PlanetRegionHandle handle=PlanetRegionHandle::Center;
        uint64_t revision=0; std::optional<evolve::Area> candidate; };
    std::optional<PlanetRegionDrag> planetRegionDrag_;
    struct PlanetGravityDrag {
        PlanetGravityHandle original,candidate;
        render::float3 normal{},startHit{};
        bool resize=false,valid=false;
        uint64_t revision=0;
    };
    std::optional<PlanetGravityDrag> planetGravityDrag_;
    std::vector<PlanetGravityHandle> planetGravityHandles_;
    void drawPlanetGravityHandles(const render::Camera& view,int width,int height);
    void drawPlanetRegionHandles(const render::Camera& view,int width,int height,const editor::MapDoc& doc);
    std::string planetMapPath_;
    uint64_t planetRevision_=std::numeric_limits<uint64_t>::max();
    bool planetMode_=false;
    std::string planetDragId_;
    uint64_t planetTransformRevision_=0;
    Json planetDragOriginal_,planetDragPlacement_;
    void framePlanet(int width,int height,const editor::MapDoc& doc,const std::string& selectedId,
                     const std::string& projectPath,const Json& rules);
    std::string planetModelProject_;
    const Json* planetModelRules_=nullptr;

    render::Mesh planetCutTerrain_;
    render::Mesh globeMesh_;
    std::optional<StudioPlanetGlobe> globe_;
    std::string globeBody_,globeProject_,globeMap_,globeError_;
    const editor::MapDoc* globeDoc_=nullptr; // identity only
    uint64_t globeRevision_=0;
    bool globeReady_=false;
    void renderCaveGlobe(int width,int height,const editor::MapDoc& doc);
    std::shared_ptr<const terraweave::caverns::CaveSurfaceGroupPlan> planetCutPlan_;
    const editor::MapDoc* planetTerrainDoc_=nullptr; // identity only
    std::string planetTerrainProject_,planetCutError_;
    size_t planetCutTriangles_=0;
    bool planetCutDrawn_=false;
    render::MeshBuilder terrainCpu_;
    TerrainPreviewStats terrainCutStats_;
    render::NativePresentationRetirement terrainRetirement_;
    editor::TerraWeaveSparsePreview sparseTerrainPreview_;
    render::NativePresentationRetirement sparseTerrainRetirement_;
    std::string sparseTerrainProject_;
    std::string planetChartIdentity_;
    std::string sparseTerrainError_;
    std::shared_ptr<const game::TerrainLook> planetTerrainLook_;
    bool baseTerrainView_=false;
    bool terrainWasShown_=false,terrainCavernsAuthored_=false;
    std::unique_ptr<class StudioEffectPlayback> effectFile_;
    std::unique_ptr<class StudioAttachmentPreview> attachmentPreview_;
    std::shared_ptr<const render::Model> animationSource_;
    std::unique_ptr<render::Model> animationModel_;
    std::unique_ptr<render::Model> materialSphere_;
    std::unique_ptr<render::Model> buildingPreview_;
    bool buildingPreviewGpuReady_=false;
    // Built-in Mesh Renderer primitives (StaticModel.asset "" / builtin:cube / builtin:sphere).
    std::unique_ptr<render::Model> builtinCube_, builtinSphere_;
    std::map<std::string,std::vector<render::Material>> primitiveMaterials_; // per frame, per entity
    render::Model* builtinPrimitive(bool sphere);
    StudioFacialGpuPreview facialGpu_;
    StudioCavernsPreview cavernsPreview_;
    StudioSpectraSources spectraSources_;
    std::unique_ptr<StudioSpectraPreview> spectraPreview_=std::make_unique<StudioSpectraPreview>();
    std::string spectraEngineAssetRoot_;
    std::unique_ptr<StudioSpectraPreview> spectraReplayBackup_;
    uint64_t replayAttemptGeneration_=0;
    std::string replayOutcome_,replayError_;
    render::Camera replayBackupCamera_{};
    bool replayBackupCameraInitialized_=false,replayBackupGrid_=true,replayBackupAxes_=true;
    std::string replayBackupBackground_;
    void discardReplayBackup();
    void rollbackReplayTransaction(const std::string& outcome,const std::string& reason);
    void finishReplayTransaction();
    std::shared_ptr<std::deque<std::pair<uint64_t,Json>>> fxFrameProvenance_=std::make_shared<std::deque<std::pair<uint64_t,Json>>>();
    // Not a second gfx device: the same Scene renderer is placed in the
    // SpectraFX panel and switches between authored map and isolated FX.
    bool spectraDockMode_=false;
    std::string fxBackground_="dark";
    bool fxGrid_=true,fxAxes_=true;
    bool sceneCameraInitializedBeforeSpectra_=false;
    render::Camera sceneCameraBeforeSpectra_{};
    void frameSpectraDock(int framebufferWidth,int framebufferHeight);

    std::string spectraProject_;
    std::string facialFramedGuid_; // character the camera was last framed on
    std::string thumbnailPath_;   // pending preset thumbnail capture
    bool thumbnailCrop_=true;
    uint64_t facialCameraSerial_=0; // last applied character-editor camera view
    bool facialMode_=false,facialReady_=false;
    std::string facialError_;
    uint64_t materialRevision_ = std::numeric_limits<uint64_t>::max();
    std::string graphPreviewError_;
    bool materialMode_ = false;
    uint64_t terrainRevision_ = std::numeric_limits<uint64_t>::max();
    float terrainLevelHeight_ = 1.5f;
    // Each model is owned by the native Scene; upload keeps CPU vertices for
    // triangle picking. The draw list and hit list share the exact instances.
    std::map<std::string,std::unique_ptr<render::Model>> documentModels_;
    std::set<std::string> rejectedDocumentModels_;
    std::map<std::string,std::filesystem::file_time_type> documentModelTimes_;
    std::vector<StudioModelPickInstance> modelInstances_;
    bool pivotSnap_ = false;
    struct ThumbJob { std::string project, asset, file; };
    std::deque<ThumbJob> thumbJobs_;
    struct ThumbShared { std::mutex mutex; std::vector<std::string> done; };
    std::shared_ptr<ThumbShared> thumbShared_ = std::make_shared<ThumbShared>();
    gfx::TextureHandle thumbColor_, thumbDepth_;
    std::map<std::string,std::unique_ptr<render::Model>> thumbModels_; // models not placed in the map
    void renderThumbnail();
    std::unique_ptr<render::Model> loadContentModel(const std::string& projectPath, const std::string& asset);
    std::vector<std::pair<std::string,render::float3>> snapPivots_; // proxy centres of the last frame
    // RTS built-in procedural shapes ("view": {"shape": …}) for types without a model file,
    // built once and shared; team-coloured parts take the owner colour.
    std::map<std::string,std::unique_ptr<render::Model>> shapeModels_;
    bool shapesBuilt_ = false;
    std::map<std::string,render::float3> instanceTeams_;
    const render::Model* rtsShapeModel(const Json& type);
    // Per-entity StaticModel.materials overrides resolved against materials.json.
    struct InstanceMaterials { std::string signature; std::vector<render::Material> materials; };
    std::map<std::string,InstanceMaterials> instanceMaterials_;
    Json materialLibrary_ = Json::object();
    std::filesystem::file_time_type materialLibraryTime_{};
    std::string materialLibraryPath_;
    void releasePreviewModel(render::Model& model);
    void clearDocumentModels();
    void syncDocumentModels(const editor::MapDoc& doc, const std::string& projectPath,
                            const Json& rules, const std::vector<StudioDocumentProxy>& proxies);
    StudioWindState wind_;
    size_t riggedInstances_ = 0, riggedParts_ = 0;
    std::map<const render::Model*, std::optional<render::wind::ModelWindRigView>> windRigs_;
    std::chrono::steady_clock::time_point windClock_ = std::chrono::steady_clock::now();
    // Facial/material preview FXAA (LDR); the map view keeps PRISMA HDR post_.
    render::PostFx antialiasing_;
    bool spectraDockHdr_=false;
    render::Camera camera_;
    std::optional<render::Camera> lensCamera_;
    std::optional<render::Camera> pendingSceneLens_;
    std::optional<render::float3> pendingFocusLight_;
    void applyPendingSceneLens();
    render::AdaptiveShadowResolution shadowResolution_;
    bool cameraInitialized_ = false;
    // Short eased transition for focus / axis snaps; any direct input cancels it.
    struct CameraTween {
        bool active = false;
        render::float3 fromTarget{}, toTarget{};
        float fromDistance = 0, toDistance = 0, fromYaw = 0, toYaw = 0, fromPitch = 0, toPitch = 0;
        std::chrono::steady_clock::time_point start{};
    } tween_;
    CameraTween replayBackupTween_{};
    void startTween(render::float3 target, float distance, float yaw, float pitch);
    void stepTween();
    // Camera actually submitted/picked: clip planes follow distance, ortho keeps
    // its framing from `distance` while the eye stays inside the depth range.
    render::Camera viewCamera() const;
    bool orientationGizmo_ = false;
    TransformTool tool_ = TransformTool::Move;
    StudioRegionBorders regionBorders_;
    char regionBorderLetter_=0;
    bool regionBordersDirty_=true;
    bool localSpace_ = false, gizmosVisible_ = true, snapEnabled_ = false;
    float snapMove_ = 1.0f, snapRotate_ = 15.0f, snapScale_ = 0.1f;
    struct TransformDrag {
        int axis = -1; TransformTool tool = TransformTool::Move; std::string id;
        render::float3 pivot{}, dir{}; render::float3 axes[3]{};
        std::optional<uint64_t> expectedRevision;
        float startT = 0, startX = 0, lengthPx = 1, worldLength = 1, value = 0;
        float startAngle = 0, lastAngle = 0, accumulated = 0; bool facing = false; render::float2 screenPivot{};
        render::float3 planeNormal{}, startHit{}, moveOffset{}; // Move: world offset (axis or plane)
        render::float3 startVec{}; bool snapping = false;     // Rotate: ring direction under the press, snap active
    } drag_;
    bool selectedIsLight_ = false, selectedAxesValid_ = false, selectedOrientedCollider_ = false;
    std::set<std::string> selection_, hidden_, locked_;
    LoadProgress loadProgress_;
    bool modelsPending_ = false;
    std::string hover_;
    struct Marquee { bool on = false; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; } marquee_;
    render::float3 selectedAxes_[3]{{1,0,0},{0,1,0},{0,0,1}};
    void handleAxes(render::float3 out[3]) const;
    render::mat4 dragPreview() const;
    std::string selectedId_;
    bool selectedPivotValid_ = false;
    render::float3 selectedPivot_{};
    bool selectedBoxValid_ = false;
    render::float3 selectedBox_[8]{};          // world corners of the selected object's box
    void captureSelectedBox(const render::mat4& world, render::float3 lo, render::float3 hi);
    void drawSelectionBox(const render::Camera& view, int width, int height);
    std::vector<OverlayShape> overlayShapes_;
    std::optional<SkyShot> skyShot_;
    bool brushVolume_=false,brushFill_=false;
    std::optional<StudioMapAreaBounds> areaBounds_;
    bool brushVisible_ = false; render::float3 brushCenter_{}; float brushRadius_ = 0;
    void drawBrushCursor(const render::Camera& view, int width, int height);
    void drawTransformGizmo(const render::Camera& view, int width, int height);
    bool autoIso_ = false; // Iso entered by an axis snap; orbit/look returns to Persp
    void drawOrientationGizmo(const render::Camera& view, int width, int height);
    bool lodDiagnostics_ = false;
    uint64_t diagnosticFrames_ = 0;
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;

    // Document-derived scene cache. Unreal/Godot keep a persistent render-side
    // scene (scene proxies / RenderingServer instances) and update it on change;
    // the Studio likewise rebuilds proxies, lights, water and model instances only
    // when the document, rules or project change, and re-checks model files on
    // disk (hot reload) at most once a second, not on every 60 Hz frame.
    struct SceneCacheKey {
        const editor::MapDoc* doc = nullptr; uint64_t revision = std::numeric_limits<uint64_t>::max();
        bool fps = false; float levelHeight = 0; std::string project; const Json* rules = nullptr;
        bool operator==(const SceneCacheKey& o) const {
            return doc==o.doc && revision==o.revision && fps==o.fps && levelHeight==o.levelHeight &&
                   project==o.project && rules==o.rules;
        }
    };
    // Per document proxy: everything the frame loop needs that depends only on the
    // document, rules and loaded models (selection, hover, hide and drag stay per frame).
    struct ProxyDraw {
        size_t proxy = 0;            // index into proxies_
        int sourceIndex = -1;        // map entity index or -1
        bool water = false;          // NereidWater component: drawn by NEREID
        bool hasModel = false;       // drawn through modelInstances_
        render::mat4 world{}, frame{};
        render::Model* primitive = nullptr; // Mesh Renderer builtin cube/sphere (PBR path)
        std::vector<render::Material> materials;
    };
    SceneCacheKey sceneKey_;
    bool sceneCacheValid_ = false;
    std::chrono::steady_clock::time_point nextFileCheck_{};
    std::vector<StudioDocumentProxy> proxies_;
    std::vector<ProxyDraw> proxyDraws_;
    std::vector<render::PointLight> activeLights_;
    Json lightingSettings_ = Json::object();
    bool useLighting_ = false;
    PreviewDayLighting day_{},weatherDay_{};
    float weatherSeconds_=0;
    std::optional<render::Lighting> weatherEnvironment_;
    bool showTerrain_ = true;
    void rebuildProxyDraws(const editor::MapDoc& doc);
    // Lighting/GI rebuilds are expensive (GPU idle wait, GI snapshot): a burst of
    // edits (Inspector slider scrubbing) rebuilds at most every ~0.3 s and once
    // more after the edits settle, like a debounced "Build Lighting".
    uint64_t lastSeenRevision_ = std::numeric_limits<uint64_t>::max();
    std::chrono::steady_clock::time_point lastDocChange_{}, lastLightingBuild_{};
    // Idle frame skipping: the viewport redraws only when something visible changed,
    // plus a few settle frames (temporal AA, GI) and a 1 s heartbeat.
    uint64_t lastFrameSignature_ = 0;
    int settleFrames_ = 0;
    std::chrono::steady_clock::time_point lastDrawn_{};
    uint64_t framesDrawn_ = 0, framesSkipped_ = 0;
    bool continuousRedraw_ = true;
    uint64_t frameSignature(int width, int height, const editor::MapDoc& doc, bool fpsProject,
                            const std::string& selectedId) const;
};
}
