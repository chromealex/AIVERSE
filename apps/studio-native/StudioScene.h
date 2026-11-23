// macOS-only native preview: AIVERSE gfx::Device + Renderer3D over a CAMetalLayer.
// No ImGui, no CPU texture readback, no dependency on UI component hierarchy.
#pragma once
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
#include "apps/studio-native/StudioModelPicking.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
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
#include "apps/studio-native/StudioSpectraPreview.h"

namespace aiverse::studio {
class StudioScene final {
public:
    // The host makes the device for its native surface (Metal: CAMetalLayer; Vulkan: an HWND).
    explicit StudioScene(std::unique_ptr<gfx::Device> device);
    ~StudioScene();
    StudioScene(const StudioScene&) = delete;
    StudioScene& operator=(const StudioScene&) = delete;
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
    Json spectraPreviewCommand(const Json& input,const std::string& project) {
        if(project!=spectraProject_ && spectraPreview_.active())
            spectraPreview_.stop(*device_);
        spectraProject_=project;
        const Json result=spectraPreview_.command(*device_,input);
        // Preview spawns near the current Scene camera target; never steal
        // the user's editor camera or change map editing navigation.
        lastFrameSignature_=0;settleFrames_=30;
        return result;
    }
    void captureNextFrame(gfx::Device::CaptureFn callback) { lastFrameSignature_=0; device_->captureNextFrame(std::move(callback)); }
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
    struct CameraState { float yaw = 0, pitch = 0, distance = 0; bool ortho = false; render::float3 target{}; };
    CameraState cameraState() const { return {camera_.yaw, camera_.pitch, camera_.distance, camera_.ortho, camera_.target}; }
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
        render::float3 move{}; float degrees = 0; float factor = 1; };
    std::optional<TransformCommit> endTransformDrag();
    bool transformDragActive() const { return drag_.axis >= 0; }
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
    bool groundPoint(float x, float y, float width, float height, render::float3& out) const;
    // Material slot names of a loaded content model (for the Inspector's Mesh Renderer).
    std::vector<std::string> modelMaterialSlots(const std::string& asset) const;
    // Scene brush ring under the cursor (radius in world units); hidden when visible=false.
    // Texture Resources / Frame Capture panels: live GPU textures (format-based size estimate) and draw calls.
    Json textureResources() const;
    uint32_t drawCalls() const { return renderer_.drawCalls(); }
    // Project extension gizmos (editor/extensions.lua scene_gizmos): world-space lines, circles (screen px), points.
    struct OverlayShape { int kind = 0; render::float3 a{}, b{}; float radius = 8, thickness = 2; uint8_t r = 90, g = 180, bl = 255; std::string text; };   // kind: 1 line, 2 circle, 3 point, 4 label
    void setOverlayShapes(std::vector<OverlayShape> shapes) { overlayShapes_ = std::move(shapes); }
    // World point -> view point (same units as pick / groundPoint); false when behind the camera.
    bool projectPoint(render::float3 world, float width, float height, render::float2& out) const;
    void setBrushCursor(bool visible, render::float3 center, float radius) { brushVisible_=visible; brushCenter_=center; brushRadius_=radius; }
    void resetCamera() { lightingRevision_ = std::numeric_limits<uint64_t>::max(); cameraInitialized_ = false; terrainRevision_ = std::numeric_limits<uint64_t>::max(); clearDocumentModels(); }
    // Frames actually drawn / skipped because nothing visible changed (diagnostics).
    uint64_t framesDrawn() const { return framesDrawn_; }
    uint64_t framesSkipped() const { return framesSkipped_; }
    // Default: draw every frame() call and rebuild lighting on every edit (tests,
    // automation, captures). The interactive Studio viewport turns this off: it
    // then skips idle frames and debounces lighting rebuilds during edit bursts.
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
    render::Renderer3D renderer_;
    world::NereidManifestHost nereid_;
    double underwaterFrameTime_=0;
    render::Prisma post_;
    render::RenderLightingRuntime lightingRuntime_;
    std::unique_ptr<render::Model> lightingModel_;
    uint64_t lightingRevision_ = std::numeric_limits<uint64_t>::max();
    std::string lightingStatus_ = "Preview geometry · GI: off · Direct: standard";
    render::Mesh cube_;
    render::Mesh studioCyclorama_;
    render::Mesh terrain_;
    std::unique_ptr<render::Model> materialSphere_;
    // Built-in Mesh Renderer primitives (StaticModel.asset "" / builtin:cube / builtin:sphere).
    std::unique_ptr<render::Model> builtinCube_, builtinSphere_;
    std::map<std::string,std::vector<render::Material>> primitiveMaterials_; // per frame, per entity
    render::Model* builtinPrimitive(bool sphere);
    StudioFacialGpuPreview facialGpu_;
    StudioSpectraPreview spectraPreview_{};
    // Not a second gfx device: the same Scene renderer is placed in the
    // SpectraFX panel and switches between authored map and isolated FX.
    bool spectraDockMode_=false;
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
    render::Camera camera_;
    render::AdaptiveShadowResolution shadowResolution_;
    bool cameraInitialized_ = false;
    // Short eased transition for focus / axis snaps; any direct input cancels it.
    struct CameraTween {
        bool active = false;
        render::float3 fromTarget{}, toTarget{};
        float fromDistance = 0, toDistance = 0, fromYaw = 0, toYaw = 0, fromPitch = 0, toPitch = 0;
        std::chrono::steady_clock::time_point start{};
    } tween_;
    void startTween(render::float3 target, float distance, float yaw, float pitch);
    void stepTween();
    // Camera actually submitted/picked: clip planes follow distance, ortho keeps
    // its framing from `distance` while the eye stays inside the depth range.
    render::Camera viewCamera() const;
    bool orientationGizmo_ = false;
    TransformTool tool_ = TransformTool::Move;
    bool localSpace_ = false, gizmosVisible_ = true, snapEnabled_ = false;
    float snapMove_ = 1.0f, snapRotate_ = 15.0f, snapScale_ = 0.1f;
    struct TransformDrag {
        int axis = -1; TransformTool tool = TransformTool::Move; std::string id;
        render::float3 pivot{}, dir{}; render::float3 axes[3]{};
        float startT = 0, startX = 0, lengthPx = 1, worldLength = 1, value = 0;
        float startAngle = 0, lastAngle = 0, accumulated = 0; bool facing = false; render::float2 screenPivot{};
        render::float3 planeNormal{}, startHit{}, moveOffset{}; // Move: world offset (axis or plane)
        render::float3 startVec{}; bool snapping = false;     // Rotate: ring direction under the press, snap active
    } drag_;
    bool selectedIsLight_ = false, selectedAxesValid_ = false;
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
    PreviewDayLighting day_{};
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
