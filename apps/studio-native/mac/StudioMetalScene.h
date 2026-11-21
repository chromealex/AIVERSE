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
#include <chrono>
#include <optional>
#include "apps/studio-native/StudioModelPicking.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
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
#include "apps/studio-native/mac/StudioFacialGpuPreview.h"

namespace aiverse::studio {
class StudioMetalScene final {
public:
    explicit StudioMetalScene(void* metalLayer);
    ~StudioMetalScene();
    StudioMetalScene(const StudioMetalScene&) = delete;
    StudioMetalScene& operator=(const StudioMetalScene&) = delete;
    void frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
               bool fpsProject, const std::string& selectedId,
               const std::string& previewName, const Json& previewFactors,
               uint64_t previewRevision, float levelHeight = 1.5f,
               const std::string& projectPath = {}, const Json& rules = Json::object(),
               const StudioFacialPreviewBinding* facialBinding=nullptr);
    void captureNextFrame(gfx::Device::CaptureFn callback) { device_->captureNextFrame(std::move(callback)); }
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
    struct CameraState { float yaw = 0, pitch = 0, distance = 0; bool ortho = false; };
    CameraState cameraState() const { return {camera_.yaw, camera_.pitch, camera_.distance, camera_.ortho}; }
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
    void setTransformTool(TransformTool tool) { tool_ = tool; drag_ = {}; }
    TransformTool transformTool() const { return tool_; }
    void setLocalSpace(bool local) { localSpace_ = local; }
    bool localSpace() const { return localSpace_; }
    void setSnap(bool enabled, float move, float rotate, float scale);
    void setGizmosVisible(bool visible) { gizmosVisible_ = visible; }
    // -1 none, 0..2 axis, 3 centre (uniform scale), 4..6 Move plane (normal X, Y, Z: YZ, XZ, XY).
    int transformGizmoHit(float x, float y, float width, float height) const;
    bool beginTransformDrag(float x, float y, float width, float height);
    // invertSnap: Ctrl/Cmd held flips the snap setting for this drag step.
    void updateTransformDrag(float x, float y, float width, float height, bool invertSnap);
    struct TransformCommit { std::string id; TransformTool tool = TransformTool::Move; int axis = -1;
        render::float3 move{}; float degrees = 0; float factor = 1; };
    std::optional<TransformCommit> endTransformDrag();
    bool transformDragActive() const { return drag_.axis >= 0; }
    void clearTransformPreview() { drag_ = {}; }
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
    void resetCamera() { lightingRevision_ = std::numeric_limits<uint64_t>::max(); cameraInitialized_ = false; terrainRevision_ = std::numeric_limits<uint64_t>::max(); clearDocumentModels(); }
    // Automation (AIVERSE_STUDIO_WIND_SHOTS): frame a point of interest.
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
    render::RTSCamera camera_;
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
    render::RTSCamera viewCamera() const;
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
    void drawSelectionBox(const render::RTSCamera& view, int width, int height);
    void drawTransformGizmo(const render::RTSCamera& view, int width, int height);
    bool autoIso_ = false; // Iso entered by an axis snap; orbit/look returns to Persp
    void drawOrientationGizmo(const render::RTSCamera& view, int width, int height);
    bool lodDiagnostics_ = false;
    uint64_t diagnosticFrames_ = 0;
    int lastMapWidth_ = 0;
    int lastMapHeight_ = 0;
};
}
