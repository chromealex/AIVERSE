// Scene: the map in 3D with a free camera; select/drag objects and apply the current tool.
//   RMB drag: look around, RMB + WASD/QE: fly, MMB drag: pan, wheel: zoom, F: focus selection.
#include "../EditorEntityAdapter.h"
#include "../EditorTransformGizmo.h"
#include "../EditorSelectionAdapter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../Editor.h"
#include "StudioContextMenus.h"
#include "StudioLuaExtensions.h"
#include "../EditorGenreUi.h"
#include "../core/PlanetOps.h"
#include "../EditorUtil.h"
#include "../EditorWorldEdit.h"
#include "../PhysisSceneOverlay.h"
#include "../NatureGizmos.h"
#include "../PhysisFieldEditor.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/render/GpuIcbPolicy.h"
#include "modules/rts/view/RtsRoadView.h"
#include <array>

namespace aiverse::editor {

using namespace render;

class SceneWindow : public EditorWindow {
public:
    const char* id() const override { return "scene"; }
    const char* title() const override { return "Сцена"; }
    const char* group() const override { return "scene"; }
    int order() const override { return 0; }
    const char* dockArea() const override { return "center"; }
    bool zeroPadding() const override { return true; }
    ImGuiWindowFlags flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

    void update(Editor& ed, double) override {
        // Hidden dock tab / closed Scene window: release its framebuffer
        // without waiting for destruction. Retired handles survive until
        // ImGui and the GPU finish reading them.
        if ((!open || vp_.drawnFrame < ImGui::GetFrameCount()-1) &&
            vp_.color.valid())
            vp_.release(ed.device());
    }
    void draw(Editor& ed) override {
        const bool traceScene = std::getenv("AIVERSE_FRAME_TRACE") != nullptr;
        const auto sceneStart = std::chrono::steady_clock::now();
        toolbar(ed); // Same scene tools and selection controls in every genre.
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const float dpi = ImGui::GetIO().DisplayFramebufferScale.x;
        vp_.setGpuDeviceLease(ed.gpuDeviceLease());
        const bool cleanCapture=ed.cleanSceneCaptureRequested();
        const auto captureSize=ed.sceneCaptureSize(int(avail.x*dpi),int(avail.y*dpi));
        vp_.ensure(ed.device(),captureSize.first,captureSize.second,ed.viewportRetirement(),"Scene");

        ImVec2 p = ImGui::GetCursorScreenPos();
        vp_.originX = p.x; vp_.originY = p.y; vp_.pointsW = avail.x; vp_.pointsH = avail.y;
        // image first (like the Game window): the screen overlays renderScene adds (bars, labels) stay on top of it
        vp_.image(ed.device(), avail.x, avail.y);
        const auto afterViewport = std::chrono::steady_clock::now();
        // Diagnostic rendering is scoped to Scene, never leaked into the Game tab.
        auto& renderer = ed.renderer();
        // The map document assigns 0xED-prefixed IDs to authored entities.
        // Trace only the selected one; avoid per-entity allocation in runtime.
        uint64_t lodTarget = 0;
        renderer.setLodDiagnosticsEnabled(lodPopupVisible_ || std::getenv("AIVERSE_LOD_DIAGNOSTICS"));
        if (lodPopupVisible_ &&
            ed.selection.kind == Selection::Entity && ed.selection.index >= 0 &&
            size_t(ed.selection.index) < ed.doc().data.entities.size())
            lodTarget = (uint64_t(0xED) << 56) | (uint64_t(ed.selection.index) + 1);
        renderer.setLodDebugTarget(lodTarget);
        renderer.setStudioDebugMode(studioDebugMode_);
        renderer.setProfiling(sceneProfiling_);
        // Preserve the *actual previous* host configuration rather than
        // assuming it always inherited the global environment. A game/native
        // project may deliberately set nondefault CPU MDI policies before
        // rendering the Studio Scene viewport.
        const auto priorCpuMdiPolicy = renderer.cpuMdiPolicyOverrides();
        auto sceneCpuMdiPolicy = priorCpuMdiPolicy;
        sceneCpuMdiPolicy.crossModel = sceneCrossModelMode_ == 1
            ? Renderer3D::StaticCrossModelBatchMode::Enable
            : sceneCrossModelMode_ == 2
                ? Renderer3D::StaticCrossModelBatchMode::Forbid
                : Renderer3D::StaticCrossModelBatchMode::Environment;
        const auto policy = [](int index) {
            return index == 1 ? Renderer3D::CpuMdiPolicyMode::Enable
                : index == 2 ? Renderer3D::CpuMdiPolicyMode::Forbid
                : Renderer3D::CpuMdiPolicyMode::Environment;
        };
        sceneCpuMdiPolicy.adaptive = policy(sceneCpuMdiAdaptiveMode_);
        sceneCpuMdiPolicy.layoutCache = policy(sceneMdiLayoutCacheMode_);
        sceneCpuMdiPolicy.geometrySort = policy(sceneMdiGeometrySortMode_);
        sceneCpuMdiPolicy.adaptiveBudgetUs = sceneCpuMdiBudgetOverrideUs_;
        // One fixed view namespace, not a GPU resource or Entity ID.
        sceneCpuMdiPolicy.adaptiveScope = kSceneCpuMdiAdaptiveScope_;
        // Scoped policy changes cannot leak to other views, including when
        // rendering exits early. The guard restores the previous host values.
        {
        Renderer3D::ScopedCpuMdiPolicyOverride sceneCpuMdiGuard(
            renderer, sceneCpuMdiPolicy);
        Renderer3D::ScopedModelRenderPolicyOverride sceneModelPolicyGuard(renderer);
        const bool measuredMode = benchmarkActive_ || autoBatchChoice_ >= 0;
        const int activeMode = benchmarkActive_ ? benchmarkMode_ : autoBatchChoice_;
        renderer.setModelInstancingEnabled(measuredMode ? true : sceneInstancing_);
        renderer.setModelMultiDrawEnabled(measuredMode ? activeMode != 0 : sceneIndirect_);
        renderer.setGpuAuthoredDrawEnabled(measuredMode ? activeMode == 2 : true);
        renderer.setModelCullingEnabled(sceneCulling_);
        renderer.setModelLodEnabled(sceneLodEnabled_);
        renderer.setModelLodFadeWidth(sceneLodFadeWidth_);
        // Pipeline layouts depend on instance limit. Apply only when the user changes
        // the control: resetting this after every Scene frame would recompile shaders.
        if (sceneInstanceLimitDirty_) {
            renderer.setInstanceLimit(uint32_t(sceneInstanceLimit_));
            sceneInstanceLimitDirty_ = false;
        }
        const auto benchmarkStart = std::chrono::steady_clock::now();
        if (measureBatchingNextFrame_ && !benchmarkActive_) {
            measureBatchingNextFrame_ = false;
            // Both passes use exactly the same scene camera and frame. A/B
            // disables both per-primitive instancing and indirect submissions;
            // geometry remains uploaded, so no asset reload is required.
            renderer.setModelInstancingEnabled(false);
            renderer.setModelMultiDrawEnabled(false);
            ed.renderScene(vp_, ed.sceneCam, !ed.playing() && !cleanCapture);
            baselineDraws_ = int64_t(renderer.drawCalls());
            baselineModelDraws_ = int64_t(renderer.profile().draws[1]) +
                                  int64_t(renderer.profile().draws[2]);
            renderer.setModelInstancingEnabled(measuredMode ? true : sceneInstancing_);
            renderer.setModelMultiDrawEnabled(measuredMode ? activeMode != 0 : sceneIndirect_);
            renderer.setGpuAuthoredDrawEnabled(measuredMode ? activeMode == 2 : true);
            // Present optimized scene as usual (not the comparison baseline).
            ed.renderScene(vp_, ed.sceneCam, !ed.playing() && !cleanCapture);
            optimizedDraws_ = int64_t(renderer.drawCalls());
            optimizedModelDraws_ = int64_t(renderer.profile().draws[1]) +
                                   int64_t(renderer.profile().draws[2]);
            comparisonValid_ = true;
        } else {
            ed.renderScene(vp_, benchmarkActive_ ? benchmarkCamera_ : ed.sceneCam, !ed.playing() && !cleanCapture);
        }
        if (benchmarkActive_) {
            const double cpuMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - benchmarkStart).count();
            if (benchmarkWarmup_ > 0) --benchmarkWarmup_;
            else {
                // Tag this full Metal command buffer with the exact benchmark
                // mode. Results arrive asynchronously and are matched by tag.
                constexpr uint64_t kGpuTag = 0xA17E000000000000ull;
                const uint64_t tag = kGpuTag |
                    (uint64_t(benchmarkGeneration_) << 40) |
                    (uint64_t(benchmarkMode_) << 38) |
                    (++benchmarkGpuSequence_ & ((uint64_t(1) << 38) - 1));
                auto& timingRouter=ed.device().gpuTimingRouter();
                if(benchmarkTimingDevice_!=&ed.device()) {
                    benchmarkTimingDevice_=&ed.device();
                    benchmarkTimingSubscriber_=0;
                }
                if(!benchmarkTimingSubscriber_)
                    benchmarkTimingSubscriber_=timingRouter.subscribe();
                if(benchmarkTimingSubscriber_)
                    timingRouter.request(ed.device(),
                        benchmarkTimingSubscriber_,tag);
                benchmarkSamples_[benchmarkMode_].push_back(cpuMs);
                benchmarkDrawSum_[benchmarkMode_] += renderer.drawCalls();
                benchmarkIndirectSum_[benchmarkMode_] += renderer.modelStats().multiDrawSubmissions;
                benchmarkCommandSum_[benchmarkMode_] += renderer.modelStats().indirectCommands;
                const auto gpuDraw = renderer.gpuModelDrawStats();
                benchmarkGpuAuthoredSum_[benchmarkMode_] += gpuDraw.submitted +
                    gpuDraw.gpuEncodedCommands;
                benchmarkGpuIcbSum_[benchmarkMode_] += gpuDraw.gpuEncodedSubmissions;
                ++benchmarkPhaseSamples_;
                if (benchmarkPhaseSamples_ == 300) {
                    benchmarkPhaseSamples_ = 0;
                    ++benchmarkPhase_;
                    if (benchmarkPhase_ == benchmarkBlockCount_) {
                        benchmarkActive_ = false;
                        benchmarkDone_ = true;
                        if (autoBatchEnabled_) {
                            std::array<double, 3> averages{};
                            for (int mode = 0; mode < 3; ++mode) {
                                double sum = 0.0;
                                for (double sample : benchmarkSamples_[mode]) sum += sample;
                                averages[mode] = benchmarkSamples_[mode].empty() ?
                                    1.0e30 : sum / double(benchmarkSamples_[mode].size());
                            }
                            autoBatchChoice_ = render::chooseSceneBatchMode(
                                averages, benchmarkThreeModes_ ? 3 : 2);
                        }
                    } else {
                        // Two modes: ABBAAB. Three modes: ABC CBA ABC.
                        // All modes get 3 x 300 samples with per-block warmup.
                        static constexpr int twoModes[] = {0, 1, 1, 0, 0, 1};
                        static constexpr int threeModes[] = {0, 1, 2, 2, 1, 0, 0, 1, 2};
                        benchmarkMode_ = benchmarkThreeModes_ ?
                            threeModes[benchmarkPhase_] : twoModes[benchmarkPhase_];
                        benchmarkWarmup_ = 60;
                    }
                }
            }
        }
        if (benchmarkActive_ || benchmarkDone_) {
            // Metal timings include the full editor MTLCommandBuffer, not just
            // renderScene. Never label them as isolated Scene GPU durations.
            auto& timingRouter=ed.device().gpuTimingRouter();
            if(benchmarkTimingDevice_!=&ed.device()) {
                benchmarkTimingDevice_=&ed.device();
                benchmarkTimingSubscriber_=0;
            }
            timingRouter.poll(ed.device());
            for (const auto& timing :
                 timingRouter.take(benchmarkTimingSubscriber_)) {
                if (!timing.valid || !std::isfinite(timing.milliseconds) ||
                    timing.milliseconds <= 0.0 ||
                    (timing.tag & 0xFFFF000000000000ull) != 0xA17E000000000000ull ||
                    ((timing.tag >> 40) & 0xFFu) != benchmarkGeneration_)
                    continue;
                const uint32_t mode = uint32_t((timing.tag >> 38) & 0x3u);
                if (mode < 3) benchmarkGpuSamples_[mode].push_back(timing.milliseconds);
            }
        }
        // Take a stable copy before the Game window overwrites the shared
        // Renderer3D counters later in the editor frame.
        lastSceneLodStats_ = renderer.modelStats();
        lastSelectedLod_ = renderer.lodDebugSample();
        } // Restore CPU MDI and model-render policies before Game can render.
        const auto afterRenderScene = std::chrono::steady_clock::now();
        // Changes are scoped to Scene; Game playback uses the normal renderer defaults.
        renderer.setStudioDebugMode(0);
        renderer.setProfiling(false);
        // The scoped model policy restores the prior host values instead
        // of forcing Game/native clients back to default-true modes.
        renderer.setLodDebugTarget(0);
        renderer.setLodDiagnosticsEnabled(false);
        vp_.drawnFrame = ImGui::GetFrameCount();
        ed.sceneViewport = &vp_;
        if (traceScene) {
            static unsigned samples = 0;
            static double totals[3] = {};
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            totals[0] += ms(sceneStart, afterViewport);
            totals[1] += ms(afterViewport, afterRenderScene);
            totals[2] += ms(sceneStart, std::chrono::steady_clock::now());
            if (++samples == 120) {
                std::fprintf(stderr, "[scene-frame] avg/120 viewport=%.3f ms renderScene=%.3f ms total-before-input=%.3f ms\n",
                             totals[0] / samples, totals[1] / samples, totals[2] / samples);
                samples = 0;
                for (double& value : totals) value = 0.0;
            }
        }
        // A requested export may temporarily use a different aspect ratio.
        // Resume ordinary hit-testing on the next normal-sized Scene frame.
        if(cleanCapture)return;
        // PHYSIS authored markers capture only explicit marker clicks/drags.
        // Ordinary FPS/RTS/other selection paths remain untouched otherwise.
        // «Живая природа»: in ignite mode a LMB click on the ground starts a
        // fire in the PRIVATE preview World (never the live game).
        bool natureIgnite = false;
        {
            auto& nature = ed.physisNaturePreview();
            ImGuiIO& io = ImGui::GetIO();
            if (nature.settings.enabled && nature.settings.igniteOnClick && !ed.playing() &&
                vp_.hovered && !io.KeyAlt) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    natureIgnite = true;   // only the click; RMB/MMB navigation stays
                    float gx = 0, gz = 0;
                    std::string why;
                    if (ed.groundAt(vp_, ed.sceneCam, io.MousePos.x, io.MousePos.y, gx, gz))
                        ed.status = nature.ignite(gx, gz, &why)
                            ? "PHYSIS: подожжено" : "PHYSIS: " + why;
                }
            }
        }
        const bool physisMarkerInput = natureIgnite || processPhysisFieldGizmo(ed, vp_, ed.sceneCam);
        if(ed.ws().project.isFps()) {
            ImGuiIO& io = ImGui::GetIO();
            const bool navigation = vp_.hovered || (ImGui::IsWindowFocused() && !io.WantTextInput);
            if (vp_.hovered && io.MouseWheel != 0)
                ed.sceneCam.distance = std::clamp(ed.sceneCam.distance * std::pow(0.85f, io.MouseWheel), 1.0f, 2000.0f);
            if (vp_.hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ed.sceneCam.yaw -= io.MouseDelta.x * 0.005f;
                ed.sceneCam.pitch = std::clamp(ed.sceneCam.pitch + io.MouseDelta.y * 0.005f, -1.553343f, 1.553343f);
            }
            if (vp_.hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                const float s = ed.sceneCam.distance * 0.002f;
                const float3 right{std::cos(ed.sceneCam.yaw), 0, -std::sin(ed.sceneCam.yaw)};
                const float3 forward{-std::sin(ed.sceneCam.yaw), 0, -std::cos(ed.sceneCam.yaw)};
                ed.sceneCam.target += right * (-io.MouseDelta.x * s) + forward * (io.MouseDelta.y * s);
            }
            // FPS preview also uses the editor free camera: no RMB hold required for WASD.
            if (navigation && !io.WantTextInput && !io.KeyCtrl && !io.KeySuper) {
                const float speed = ed.sceneCam.distance * 0.9f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
                const float3 right{std::cos(ed.sceneCam.yaw), 0, -std::sin(ed.sceneCam.yaw)};
                const float3 forward{-std::sin(ed.sceneCam.yaw), 0, -std::cos(ed.sceneCam.yaw)};
                if (ImGui::IsKeyDown(ImGuiKey_W)) ed.sceneCam.target += forward * speed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) ed.sceneCam.target += forward * -speed;
                if (ImGui::IsKeyDown(ImGuiKey_D)) ed.sceneCam.target += right * speed;
                if (ImGui::IsKeyDown(ImGuiKey_A)) ed.sceneCam.target += right * -speed;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) ed.sceneCam.target.y -= speed;
                if (ImGui::IsKeyDown(ImGuiKey_E)) ed.sceneCam.target.y += speed;
                if (ImGui::IsKeyPressed(ImGuiKey_F, false)) ed.fitScene();
            }
            // FPS previously rendered the shared selection HUD without ever
            // updating the selection. Pick using the same scene model as RTS.
            // During play the inspector stays read-only.
            if (!physisMarkerInput && vp_.hovered && !io.WantTextInput &&
                !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                const Selection hovered = ed.pickObject(vp_, ed.sceneCam, io.MousePos.x, io.MousePos.y);
                ed.setHover(hovered);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // The same Selection now drives Scene, Hierarchy and Inspector.
                    // Do not run a second genre-specific picker or overwrite a
                    // cleared FPS selection with a stale RTS selection.
                    fpsGizmoAxis_ = -1;
                    if (ed.tool == Tool::Select && ed.selection.kind == Selection::FpsObject) {
                        DocumentEntityRef selectedRef;
                        DocumentTransform position;
                        if (DocumentEntityRef::fromFpsKey(ed.selection.ref, selectedRef) &&
                            readDocumentTransform(ed.doc(), selectedRef, position)) {
                            const float3 pivot{float(position.x), float(position.y),
                                               float(position.z)};
                            const float extent = std::max(0.35f, ed.sceneCam.distance * 0.075f);
                            const Json* source = fpsDocumentEntity(ed.doc().data.fps, selectedRef);
                            const bool oriented = selectedRef.source ==
                                DocumentEntityRef::Source::FpsCollider && source &&
                                (*source)["yaw"].isNumber();
                            if (ed.transformTool == TransformTool::Rotate && oriented) {
                                fpsGizmoAxis_ = pickRotateYawGizmoRing(
                                    ed.sceneCam, vp_.pointsW, vp_.pointsH,
                                    vp_.originX, vp_.originY, io.MousePos.x,
                                    io.MousePos.y, pivot, extent) ? 3 : -1;
                            } else if (ed.transformTool == TransformTool::Scale && oriented) {
                                fpsGizmoAxis_ = pickMoveGizmoAxis(
                                    ed.sceneCam, vp_.pointsW, vp_.pointsH,
                                    vp_.originX, vp_.originY, io.MousePos.x,
                                    io.MousePos.y, pivot, extent);
                            } else if (ed.transformTool == TransformTool::Move) {
                                fpsGizmoAxis_ = pickMoveGizmoAxis(
                                    ed.sceneCam, vp_.pointsW, vp_.pointsH,
                                    vp_.originX, vp_.originY, io.MousePos.x,
                                    io.MousePos.y, pivot, extent);
                            }
                        }
                    }
                    const bool alreadySelected =
                        std::find(ed.selectionSet.begin(), ed.selectionSet.end(),
                                  hovered) != ed.selectionSet.end();
                    // Preserve a group when dragging an already-selected
                    // member. A simple click without movement still selects
                    // just that member on release, matching RTS behavior.
                    fpsDragKeepSelection_ = fpsGizmoAxis_ < 0 &&
                        alreadySelected && ed.selectionSet.size() > 1 &&
                        !io.KeyCtrl && !io.KeySuper && ed.tool == Tool::Select;
                    fpsDragClicked_ = hovered;
                    if (fpsGizmoAxis_ < 0 && !fpsDragKeepSelection_)
                        pickSelect(ed, hovered, io);
                    fpsDragActive_ = ed.tool == Tool::Select &&
                        !io.KeyCtrl && !io.KeySuper &&
                        (fpsGizmoAxis_ >= 0 ||
                         (hovered.kind == Selection::FpsObject &&
                          (ed.selection == hovered || fpsDragKeepSelection_)));
                    fpsDragEdited_ = false;
                    fpsDragSnapshot_ = false;
                    fpsDragLastX_ = io.MousePos.x;
                    fpsDragLastY_ = io.MousePos.y;
                }
            }
            if (!physisMarkerInput && fpsDragActive_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                (io.MousePos.x != fpsDragLastX_ || io.MousePos.y != fpsDragLastY_)) {
                if (ed.transformTool != TransformTool::Move && fpsGizmoAxis_ >= 0) {
                    DocumentEntityRef ref;
                    const bool validRef = documentEntityFromSelection(ed.selection, ref) &&
                        ref.source == DocumentEntityRef::Source::FpsCollider;
                    const Json* source = validRef
                        ? fpsDocumentEntity(ed.doc().data.fps, ref) : nullptr;
                    if (source && (*source)["yaw"].isNumber()) {
                        Json transform;
                        if (readDocumentComponent(ed.doc(), ref,
                                DocumentComponentKind::Transform, transform)) {
                            Json patch = Json::object();
                            if (ed.transformTool == TransformTool::Rotate &&
                                fpsGizmoAxis_ == 3) {
                                const double previous = transform["rotationY"].toDouble();
                                const double next = previous +
                                    double(io.MousePos.x - fpsDragLastX_) * 0.5;
                                patch.set("rotationY", Json::number(next, 3));
                            } else if (ed.transformTool == TransformTool::Scale &&
                                       fpsGizmoAxis_ >= 0 && fpsGizmoAxis_ < 3) {
                                const Json& size = transform["scale"];
                                if (size.isArray() && size.size() == 3) {
                                    Json scale = Json::array();
                                    const double amount =
                                        double(fpsDragLastY_ - io.MousePos.y) *
                                        std::max(0.005, double(ed.sceneCam.distance) * 0.001);
                                    for (int i = 0; i < 3; ++i) {
                                        const double dimension = size[size_t(i)].toDouble();
                                        scale.push(Json::number(i == fpsGizmoAxis_
                                            ? std::max(0.01, dimension + amount)
                                            : dimension, 3));
                                    }
                                    patch.set("scale", scale);
                                }
                            }
                            if (patch.size() > 0) {
                                if (!fpsDragSnapshot_) { ed.doc().beginEdit(); fpsDragSnapshot_ = true; }
                                fpsDragEdited_ = writeDocumentComponent(
                                    ed.doc(), ref, DocumentComponentKind::Transform,
                                    patch) || fpsDragEdited_;
                            }
                        }
                    }
                } else {
                    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
                    bool valid = false;
                    if (fpsGizmoAxis_ == 1 || (fpsGizmoAxis_ < 0 && io.KeyShift)) {
                        dy = (fpsDragLastY_ - io.MousePos.y) *
                             std::max(0.01f, ed.sceneCam.distance * 0.002f);
                        valid = std::isfinite(dy);
                    } else {
                        float3 previous, current;
                        valid = ed.sceneCam.screenToGround(fpsDragLastX_ - vp_.originX,
                                                           fpsDragLastY_ - vp_.originY,
                                                           vp_.pointsW, vp_.pointsH, previous) &&
                                ed.sceneCam.screenToGround(io.MousePos.x - vp_.originX,
                                                           io.MousePos.y - vp_.originY,
                                                           vp_.pointsW, vp_.pointsH, current);
                        if (valid) {
                            dx = current.x - previous.x;
                            dz = current.z - previous.z;
                            if (fpsGizmoAxis_ == 0 || ImGui::IsKeyDown(ImGuiKey_X))
                                dz = 0.0f;
                            else if (fpsGizmoAxis_ == 2 || ImGui::IsKeyDown(ImGuiKey_Z))
                                dx = 0.0f;
                            valid = std::isfinite(dx) && std::isfinite(dz);
                        }
                    }
                    if (valid && (std::abs(dx) > 1e-5f ||
                                  std::abs(dy) > 1e-5f || std::abs(dz) > 1e-5f)) {
                        if (!fpsDragSnapshot_) { ed.doc().beginEdit(); fpsDragSnapshot_ = true; }
                        fpsDragEdited_ = ed.moveSelectedFpsSceneObject(dx, dy, dz) ||
                                         fpsDragEdited_;
                    }
                }
                fpsDragLastX_ = io.MousePos.x;
                fpsDragLastY_ = io.MousePos.y;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (fpsDragKeepSelection_ && !fpsDragEdited_ &&
                    fpsDragClicked_.kind == Selection::FpsObject)
                    ed.select(fpsDragClicked_);
                fpsDragKeepSelection_ = false;
                fpsDragActive_ = false;
                fpsGizmoAxis_ = -1;
            }
        } else {if (!physisMarkerInput &&
            (ImGui::IsWindowFocused() || vp_.hovered || looking_ || panning_)) input(ed);}
        // Optional PHYSIS diagnostics project into the actual camera using
        // read-only ECS data. The shared selection HUD remains above the overlay.
        drawPhysisSceneOverlay(ed, vp_, ed.sceneCam);
        // Authored markers are separate from live simulation: visible even
        // without PHYSIS installed in the current GameModule.
        drawPhysisFieldMarkers(ed, vp_, ed.sceneCam);
        // PHYSIS wind/fire, NEREID flow and TERRA slope/tree-wind directions.
        drawNatureGizmos(ed, vp_, ed.sceneCam);
        // The selection overlay is genre-independent (including FPS).
        hud(ed);
        StudioLuaExtensions::get().drawScene(ed, vp_);
        // Shift+RMB opens custom scene actions. Ordinary RMB still navigates.
        if (StudioContextMenus::get().available(ed) && vp_.hovered &&
            ImGui::GetIO().KeyShift && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##aiverse_scene_context");
        if (ImGui::BeginPopup("##aiverse_scene_context")) {
            StudioContextMenus::get().draw(ed, "scene");
            ImGui::EndPopup();
        }
    }

private:
    bool fpsDragActive_ = false, fpsDragEdited_ = false;
    bool fpsDragSnapshot_ = false;   // one undo snapshot per drag, even while writes fail
    bool fpsDragKeepSelection_ = false;
    int fpsGizmoAxis_ = -1;
    Selection fpsDragClicked_;
    float fpsDragLastX_ = 0, fpsDragLastY_ = 0;

    void lodControls(Editor& ed) {
        if (ImGui::Button("LOD##scene")) ImGui::OpenPopup("##scene_lod");
        if (!ImGui::BeginPopup("##scene_lod")) {
            lodPopupVisible_ = false;
            return;
        }
        lodPopupVisible_ = true;
        ImGui::TextUnformatted("LOD текущей камеры (модели)");
        ImGui::Separator();
        ImGui::Checkbox("Включить Model LOD", &sceneLodEnabled_);
        ImGui::SliderFloat("Переход LOD", &sceneLodFadeWidth_, 0.0f, 0.49f, "%.2f");
        const auto& stats = lastSceneLodStats_;
        ImGui::Text("В кадре: LOD0 %llu | LOD1 %llu | LOD2 %llu | LOD3+ %llu",
                    (unsigned long long)stats.cameraLodNodes[0],
                    (unsigned long long)stats.cameraLodNodes[1],
                    (unsigned long long)stats.cameraLodNodes[2],
                    (unsigned long long)stats.cameraLodNodes[3]);
        ImGui::Text("С LOD-цепочкой: %llu | без неё: %llu",
                    (unsigned long long)stats.cameraLodChains,
                    (unsigned long long)stats.cameraNoLodChains);
        ImGui::Text("Не подходит для выбора LOD: %llu | ограничено тенями: %llu",
                    (unsigned long long)stats.cameraLodBlocked,
                    (unsigned long long)stats.cameraShadowLimitedLod);
        ImGui::TextDisabled("Счётчики узлов исходных моделей, только main camera.");
        ImGui::TextDisabled("Уровень выбирается по размеру на экране и теневому охвату.");
        ImGui::Separator();
        int selectedType = -1;
        if (ed.selection.kind == Selection::Entity &&
            ed.selection.index >= 0 &&
            size_t(ed.selection.index) < ed.doc().data.entities.size())
            selectedType = ed.rules().findType(
                ed.doc().data.entities[size_t(ed.selection.index)].type);
        else if (ed.selection.kind == Selection::Type) {
            for (size_t i = 0; i < ed.rules().types.size(); ++i)
                if (ed.rules().types[i].id == ed.selection.key) {
                    selectedType = int(i);
                    break;
                }
        }
        if (selectedType >= 0 && size_t(selectedType) < ed.rules().types.size()) {
            ed.retainEditorType(uint16_t(selectedType));
            ImGui::Text("Выбран: %s", ed.rules().types[size_t(selectedType)].id.c_str());
            if (const auto* model = ed.view().modelOf(uint16_t(selectedType))) {
                size_t chains = 0, levels = 0;
                for (const auto& node : model->nodes) {
                    if (node.mesh < 0 || size_t(node.mesh) >= model->meshes.size()) continue;
                    const auto& mesh = model->meshes[size_t(node.mesh)];
                    if (mesh.lods.empty()) continue;
                    ++chains;
                    levels += mesh.lods.size();
                }
                ImGui::Text("LOD-цепочек: %zu | доп. уровней: %zu", chains, levels);
                if (model->autoLod.generatedLevels)
                    ImGui::Text("Авто-LOD: %zu уровней для %zu мешей",
                                model->autoLod.generatedLevels,
                                model->autoLod.generatedMeshes);
                else if (model->autoLod.attempted)
                    ImGui::TextDisabled("Авто-LOD: %s", model->autoLod.reason.c_str());
                if (!chains) ImGui::TextDisabled("В загруженной модели нет LOD.");
                if (ed.selection.kind == Selection::Entity) {
                    const auto& actual = lastSelectedLod_;
                    if (actual.matched) {
                        ImGui::Text("Выбранный объект: LOD0 %llu, LOD1 %llu, LOD2 %llu, LOD3+ %llu",
                            (unsigned long long)actual.levels[0],
                            (unsigned long long)actual.levels[1],
                            (unsigned long long)actual.levels[2],
                            (unsigned long long)actual.levels[3]);
                        ImGui::Text("Видимые узлы: %llu, с цепочкой: %llu, ограничены тенями: %llu",
                            (unsigned long long)actual.visibleNodes,
                            (unsigned long long)actual.withChain,
                            (unsigned long long)actual.shadowLimited);
                    } else ImGui::TextDisabled("Выбранный объект не рисовался в текущей камере.");
                }
                ImGui::TextDisabled("Подробности: окно «Модели» → «LOD модели».");
            }
        } else ImGui::TextDisabled("Выбери автомобиль или здание, чтобы проверить их LOD.");
        ImGui::TextDisabled("Недостающие LOD создаются при загрузке ассета, если модель поддерживается.");
        ImGui::TextDisabled("Настройки влияют на общий Renderer3D (Scene / Game).");
        ImGui::EndPopup();
    }

    void batchingControls(Editor& ed) {
        if (ImGui::Button("Batching##scene")) ImGui::OpenPopup("##scene_batching");
        if (ImGui::BeginPopup("##scene_batching")) {
            ImGui::TextUnformatted("Scene model batching");
            ImGui::Separator();
            const bool mdiBackend = ed.renderer().modelMultiDrawSupported();
            if (!mdiBackend) ImGui::BeginDisabled();
            if (ImGui::Checkbox("Shared Geometry Pool", &sceneGeometryPool_)) {
                render::setGlobalGeometryPoolingEnabled(sceneGeometryPool_);
                ed.reloadRules(); // reupload original models; never toggle existing GPU buffers in place
            }
            if (ImGui::Checkbox("Rigid LOD geometry pooling (experimental)",
                                &sceneRigidLodPooling_)) {
                render::setRigidLodGeometryPoolingEnabled(sceneRigidLodPooling_);
                ed.reloadRules(); // only asset upload can change geometry page membership
            }
            ImGui::TextDisabled("Allows immutable rigid LODs; skin/morph/clip models remain dedicated.");
            if (!mdiBackend) {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Geometry Pool inactive: Multi-Draw Indirect is unavailable on this backend.");
                ImGui::TextDisabled("Metal ICB is disabled until the driver crash is resolved.");
            }
            if (ImGui::Checkbox("GPU Instancing", &sceneInstancing_))
                autoBatchChoice_ = -1;
            if (ImGui::Checkbox("Multi-Draw Indirect", &sceneIndirect_))
                autoBatchChoice_ = -1;
            ImGui::SameLine(); ImGui::TextDisabled(ed.renderer().modelMultiDrawSupported() ? "GPU supported" : "fallback");
            if (ImGui::Combo("Static cross-model CPU MDI##scene",
                    &sceneCrossModelMode_,
                    "Inherit env (default OFF)\0Enable in Scene only\0Disable in Scene only\0"))
                autoBatchChoice_ = -1;
            ImGui::TextDisabled("Experimental: requires shared pooled GPU geometry, supported CPU MDI and compatible static models.");
            static constexpr const char* mdiPolicyModes =
                "Inherit env\0Enable in Scene only\0Disable in Scene only\0";
            ImGui::Combo("CPU MDI adaptive policy##scene",
                &sceneCpuMdiAdaptiveMode_, mdiPolicyModes);
            ImGui::Combo("MDI layout command cache##scene",
                &sceneMdiLayoutCacheMode_, mdiPolicyModes);
            ImGui::Combo("MDI geometry sort##scene",
                &sceneMdiGeometrySortMode_, mdiPolicyModes);
            ImGui::SetNextItemWidth(190.0f);
            if (ImGui::SliderScalar("Adaptive budget (us / command)##scene",
                    ImGuiDataType_Double, &sceneCpuMdiBudgetOverrideUs_,
                    &kSceneCpuMdiBudgetMin_, &kSceneCpuMdiBudgetMax_, "%.0f") &&
                sceneCpuMdiBudgetOverrideUs_ > 0.0 &&
                sceneCpuMdiBudgetOverrideUs_ < 1.0)
                sceneCpuMdiBudgetOverrideUs_ = 1.0;
            ImGui::SameLine();
            if (ImGui::SmallButton("Inherit budget##scene"))
                sceneCpuMdiBudgetOverrideUs_ = 0.0;
            ImGui::TextDisabled("Adaptive needs static cross-model MDI; 0 us means inherit env budget (default 25).");
            ImGui::TextDisabled("All overrides affect Scene only; previous host policy is restored for Game.");
            ImGui::Checkbox("Model culling", &sceneCulling_);
            ImGui::Checkbox("Auto-select fastest mode after benchmark", &autoBatchEnabled_);
            if (!autoBatchEnabled_) autoBatchChoice_ = -1;
            if (autoBatchChoice_ >= 0) {
                const char* names[] = {"Instancing", "CPU MDI", "GPU-authored"};
                ImGui::Text("Selected Scene render mode: %s (lowest measured CPU)",
                            names[autoBatchChoice_]);
                if (ImGui::Button("Restore manual batching"))
                    autoBatchChoice_ = -1;
            }
            ImGui::Checkbox("Profile Scene renderer", &sceneProfiling_);
            ImGui::SameLine(); ImGui::TextDisabled("CPU render stages");
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderInt("Instances / call", &sceneInstanceLimit_, 0, 256,
                sceneInstanceLimit_ == 0 ? "Auto" : "%d")) sceneInstanceLimitDirty_ = true;
            ImGui::Separator();
            const auto geometryPool = render::geometryPoolStats(ed.device());
            ImGui::Text("Geometry: %zu pages / %zu models (%s)", geometryPool.pages, geometryPool.liveModels,
                        !mdiBackend ? "unavailable" : (!sceneGeometryPool_ ? "disabled" : "enabled"));
            ImGui::Text("GPU geometry: %.1f / %.1f MiB",
                        double(geometryPool.occupiedBytes) / (1024.0 * 1024.0),
                        double(geometryPool.allocatedBytes) / (1024.0 * 1024.0));
            if (geometryPool.allocatedBytes) {
                const double fill = 100.0 * double(geometryPool.occupiedBytes) / double(geometryPool.allocatedBytes);
                const double slackMiB = double(geometryPool.allocatedBytes - geometryPool.occupiedBytes) / (1024.0 * 1024.0);
                ImGui::Text("Pool utilization: %.1f%% (%.1f MiB slack)", fill, slackMiB);
                ImGui::TextDisabled("Pool memory excludes textures and dedicated model buffers.");
            }
            // A retired pair is not necessarily safe to recycle: only the
            // GPU's completed queue serial can advance it out of quarantine.
            // These are upload/release statistics, not FPS or GPU timings.
            ImGui::Text("Pooled geometry: %zu retired / %zu fence-ready pairs",
                        geometryPool.retiredPairs, geometryPool.reusablePairs);
            ImGui::Text("Recycled model uploads: %llu",
                        (unsigned long long)geometryPool.recycledUploads);
            ImGui::TextDisabled("Recycling needs AIVERSE_GEOMETRY_POOL_RECYCLE=1 (default OFF).");
            ImGui::TextDisabled("GPU occupied bytes include retained ranges, not only live models.");
            const auto& profile = ed.renderer().profile();
            const auto& modelStats = ed.renderer().modelStats();
            const uint32_t totalSubmissions = ed.renderer().drawCalls();
            const uint32_t modelSubmissions =
                profile.draws[0] + profile.draws[1] + profile.draws[2] +
                profile.draws[3] + profile.draws[4];
            const uint32_t otherSubmissions = totalSubmissions >= modelSubmissions
                ? totalSubmissions - modelSubmissions : 0;
            ImGui::Text("GPU API submissions (tracked): %u", totalSubmissions);
            const uint64_t cameraTriangles = profile.cameraModelTriangles +
                profile.cameraMeshTriangles + profile.cameraOverlayTriangles;
            ImGui::Text("Triangles in camera (tracked): %llu",
                        (unsigned long long)cameraTriangles);
            ImGui::TextDisabled("Models: %llu | Meshes: %llu | Overlays: %llu",
                                (unsigned long long)profile.cameraModelTriangles,
                                (unsigned long long)profile.cameraMeshTriangles,
                                (unsigned long long)profile.cameraOverlayTriangles);
            ImGui::TextDisabled("Current camera only; excludes shadow/reflection and repeated SSR pass.");
            ImGui::TextDisabled("Counts submitted geometry incl. instances, not visible pixels.");
            ImGui::TextDisabled("Terrain, sprites and custom renderers not included.");
            const bool safeMode = [] {
                const char* env = std::getenv("AIVERSE_RENDER_SAFE");
                return env && std::strcmp(env, "1") == 0;
            }();
            const char* off = std::getenv("AIVERSE_MULTI_DRAW_INDIRECT");
            const bool envOff = off && std::strcmp(off, "off") == 0;
            const bool measuredMode = benchmarkActive_ || autoBatchChoice_ >= 0;
            const bool mdiSelected = measuredMode
                ? (benchmarkActive_ ? benchmarkMode_ : autoBatchChoice_) != 0
                : sceneIndirect_;
            const bool mdiAllowed = mdiBackend && mdiSelected && !safeMode && !envOff;
            const char* reason = !mdiBackend ? "Metal ICB unsupported/disabled"
                : !mdiSelected ? "Scene mode disabled"
                : safeMode ? "AIVERSE_RENDER_SAFE=1"
                : envOff ? "AIVERSE_MULTI_DRAW_INDIRECT=off"
                : "available";
            ImGui::Text("CPU Multi-Draw: %s (%s)",
                        mdiAllowed ? "AVAILABLE" : "DISABLED", reason);
            ImGui::Text("Actual CPU MDI: %llu submissions / %llu commands",
                        (unsigned long long)modelStats.multiDrawSubmissions,
                        (unsigned long long)modelStats.indirectCommands);
            if (mdiAllowed && modelStats.multiDrawSubmissions == 0)
                ImGui::TextDisabled("No CPU MDI submissions this frame. Expand diagnostics below.");
            if (ImGui::TreeNode("Why batches split (model passes)")) {
                ImGui::Text("Candidate scans: %llu  Rejected submissions: %llu",
                            (unsigned long long)modelStats.mdiScans,
                            (unsigned long long)modelStats.mdiFailed);
                ImGui::Text("Single/short runs: %llu  Below threshold: %llu",
                            (unsigned long long)modelStats.mdiShort,
                            (unsigned long long)modelStats.mdiBelowThreshold);
                ImGui::Text("Ineligible candidate scans: %llu",
                            (unsigned long long)modelStats.mdiIneligible);
                ImGui::Text("  Geometry clusters: %llu | baked / GI: %llu",
                            (unsigned long long)modelStats.mdiIneligibleGeometry,
                            (unsigned long long)modelStats.mdiIneligibleLighting);
                ImGui::Text("  Material batches: %llu | skin / morph: %llu | alpha / fade: %llu",
                            (unsigned long long)modelStats.mdiIneligibleMaterial,
                            (unsigned long long)modelStats.mdiIneligibleDeform,
                            (unsigned long long)modelStats.mdiIneligibleAlpha);
                ImGui::Text("Run split by geometry buffers: %llu",
                            (unsigned long long)modelStats.mdiGeometryBreaks);
                ImGui::Text("Run split by material state: %llu",
                            (unsigned long long)modelStats.mdiMaterialBreaks);
                ImGui::Text("Run split by flags / GI / pose: %llu",
                            (unsigned long long)modelStats.mdiStateBreaks);
                ImGui::TextDisabled("Counters cover the current scene model passes only.");
                ImGui::TextDisabled("Different GLBs and their material slots may require separate draws.");
                ImGui::TreePop();
            }
            if (!ed.ws().project.isFps() && ImGui::TreeNode("Road surfaces: materials / chunks")) {
                const auto& road = ed.view().roads().batchStats();
                ImGui::Text("Road source chunks: %zu -> %zu uploaded (merge group %d)",
                            road.sourceChunks, road.uploadedChunks, road.mergeGroup);
                ImGui::Text("Road surface primitives: %zu | triangles: %zu",
                            road.primitives, road.triangles);
                ImGui::TextDisabled("Same texture/material != same mesh or one draw.");
                ImGui::TextDisabled("Intersections use kit asphalt; carriageways may use painted textures.");
                ImGui::TextDisabled("Curb/sidewalk material and zebra/wet decal draws are separate.");
                ImGui::TextDisabled("Counts below exclude reflection/shadow replays and custom decals.");
                if (ImGui::BeginChild("##road_materials", ImVec2(0, 160), ImGuiChildFlags_Borders)) {
                    for (size_t i = 0; i < road.materials.size(); ++i) {
                        const auto& mat = road.materials[i];
                        if (!mat.primitives) continue;
                        ImGui::Text("%s [%zu]: %zu prim, %zu chunks, tex %u",
                                    mat.name.empty() ? "material" : mat.name.c_str(), i,
                                    mat.primitives, mat.chunks, mat.textureId);
                    }
                }
                ImGui::EndChild();
                ImGui::TreePop();
            }
            ImGui::Text("Models: %u | Other tracked: %u",
                        modelSubmissions, otherSubmissions);
            ImGui::TextDisabled("Includes repeated geometry in shadow, reflection and colour passes.");
            if (ImGui::TreeNode("Incremental static CPU model cache")) {
                const auto staticCache = ed.renderer().staticModelCacheStats();
                const char* flag = std::getenv("AIVERSE_PERSISTENT_CPU_MODEL_CACHE");
                const bool enabled = flag && std::strcmp(flag, "1") == 0;
                ImGui::Text("Status: %s (AIVERSE_PERSISTENT_CPU_MODEL_CACHE=1)",
                            enabled ? "OPT-IN ACTIVE" : "OFF");
                ImGui::Text("Hits: %llu | Rebuilds: %llu | Declined: %llu",
                            (unsigned long long)staticCache.hits,
                            (unsigned long long)staticCache.rebuilds,
                            (unsigned long long)staticCache.declined);
                ImGui::Text("Live entries: %zu | Evicted: %llu",
                            staticCache.live,
                            (unsigned long long)staticCache.evicted);
                const auto modelEligibility = ed.renderer().staticEligibilityStats();
                ImGui::Text("Model eligibility: %llu reused / %llu classified",
                            (unsigned long long)modelEligibility.hits,
                            (unsigned long long)modelEligibility.misses);
                ImGui::Text("Eligibility entries: %zu | Evicted: %llu",
                            modelEligibility.live,
                            (unsigned long long)modelEligibility.evicted);
                ImGui::TextDisabled("Static model traits follow geometryRevision; draw eligibility stays live.");
                ImGui::TextDisabled("Caches bind transforms, not draw commands or camera-dependent LOD.");
                ImGui::TextDisabled("Asset edits need cache invalidation; benchmark with the same view.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Persistent rigid draw packets")) {
                const auto packets = ed.renderer().staticRenderPacketStats();
                const char* flag = std::getenv("AIVERSE_PERSISTENT_CPU_PACKETS");
                const bool enabled = flag && std::strcmp(flag, "1") == 0;
                ImGui::Text("Status: %s (AIVERSE_PERSISTENT_CPU_PACKETS=1)",
                            enabled ? "OPT-IN ACTIVE" : "OFF");
                ImGui::Text("Replayed: %llu | Rebuilt: %llu | Declined: %llu",
                            (unsigned long long)packets.hits,
                            (unsigned long long)packets.rebuilds,
                            (unsigned long long)packets.declined);
                ImGui::Text("Live packets: %zu | Evicted: %llu | Stored: %.2f MiB",
                            packets.live, (unsigned long long)packets.evicted,
                            double(packets.retainedBytes) / (1024.0 * 1024.0));
                const auto materialMemo = ed.renderer().sharedMaterialStateStats();
                ImGui::Text("Shared material state: %llu reused / %llu refreshed",
                            (unsigned long long)materialMemo.hits,
                            (unsigned long long)materialMemo.misses);
                ImGui::Text("Material owners: %zu | Evicted: %llu | Stored: %.2f MiB",
                            materialMemo.live, (unsigned long long)materialMemo.evicted,
                            double(materialMemo.retainedBytes) / (1024.0 * 1024.0));
                ImGui::TextDisabled("Shared material signature reuse needs owner ID and revision.");
                ImGui::TextDisabled("Requires explicit nonzero ModelDraw::staticPacketRevision.");
                ImGui::TextDisabled("Camera / shadow visibility and depth are still evaluated each frame.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Model pass breakdown")) {
                static constexpr const char* phaseNames[5] = {
                    "Shadow", "Opaque (+ reflection)", "Blended",
                    "Outline", "Transparent depth"
                };
                for (size_t phase = 0; phase < 5; ++phase) {
                    ImGui::Text("%s: %u calls, %u instances, %u triangles",
                        phaseNames[phase], profile.draws[phase],
                        profile.instances[phase], profile.triangles[phase]);
                }
                ImGui::TextDisabled("These are renderer API counts, not unique scene objects.");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Instancing and indirect details")) {
                ImGui::Text("Instanced model calls: %llu",
                    (unsigned long long)modelStats.instancedDraws);
                ImGui::Text("Instances in instanced calls: %llu",
                    (unsigned long long)modelStats.drawnInstances);
                ImGui::Text("MDI submissions: %llu (%llu indexed commands)",
                    (unsigned long long)modelStats.multiDrawSubmissions,
                    (unsigned long long)modelStats.indirectCommands);
                ImGui::Text("MDI exact-layout zero-copy: %llu submissions / %.1f KiB CPU copies avoided",
                    (unsigned long long)modelStats.mdiLayoutZeroCopySubmissions,
                    double(modelStats.mdiLayoutCopyAvoidedBytes) / 1024.0);
                ImGui::Text("MDI RHI command payload: %.1f KiB / %llu discard-prefix updates",
                    double(modelStats.mdiIndirectCommandBytes) / 1024.0,
                    (unsigned long long)modelStats.mdiIndirectPrefixOverwrites);
                ImGui::TextDisabled("CPU payload requested; backend may skip bytes with dirty snapshots.");
                if (ImGui::TreeNode("CPU MDI stage timings")) {
                    if (!sceneProfiling_) {
                        ImGui::TextDisabled("Enable Profile Scene renderer above to collect timings.");
                    } else {
                        static constexpr const char* labels[] = {"Sun shadow", "Main opaque"};
                        for (size_t phase = 0; phase < 2; ++phase) {
                            const auto& cpu = modelStats.mdiCpuStages[phase];
                            if (!cpu.passes) {
                                ImGui::TextDisabled("%s: no timed model pass", labels[phase]);
                                continue;
                            }
                            ImGui::Text("%s: %.3f ms total CPU (%llu passes)",
                                labels[phase], cpu.totalMs,
                                (unsigned long long)cpu.passes);
                            ImGui::Text("  Selection %.3f | material grouping + sort %.3f | candidate scans %.3f ms",
                                cpu.selectionMs, cpu.groupingSortMs, cpu.candidateScanMs);
                            ImGui::Text("  Command packing %.3f | pipeline bind %.3f | buffer upload %.3f | backend call %.3f ms",
                                cpu.commandPackingMs, cpu.pipelineSetupMs,
                                cpu.bufferUploadMs, cpu.backendSubmitMs);
                            ImGui::Text("  %llu candidates / %llu pack attempts / %llu GPU MDI submissions",
                                (unsigned long long)cpu.candidateScans,
                                (unsigned long long)cpu.indirectAttempts,
                                (unsigned long long)cpu.submitted);
                            ImGui::Text("  %llu pipeline warmups / %llu under threshold / %llu backend fallbacks",
                                (unsigned long long)cpu.pipelineWarmups,
                                (unsigned long long)cpu.underThreshold,
                                (unsigned long long)cpu.backendFallbacks);
                            const auto rolling = ed.renderer().cpuMdiRollingSummary(phase);
                            if (rolling.samples) {
                                ImGui::Text("  Rolling CPU mean / P50 / P95 (last %zu/%zu invocations):",
                                    rolling.samples, ed.renderer().kCpuMdiRollingWindow);
                                static constexpr const char* metrics[] = {
                                    "Total model pass", "Selection", "Grouping + sort",
                                    "Candidate scan", "Command packing", "Pipeline setup",
                                    "Buffer upload", "Backend submit"
                                };
                                if (!rolling.hasBaseline) {
                                    ImGui::TextDisabled("  Spike detection collecting baseline: %zu/%zu samples",
                                        rolling.samples, ed.renderer().kCpuMdiSpikeMinSamples);
                                } else if (rolling.dominantStage >= 0) {
                                    const size_t dominant = size_t(rolling.dominantStage);
                                    ImGui::TextColored(ImVec4(1.0f, 0.69f, 0.32f, 1.0f),
                                        "  CPU P95 spike candidate: %s (%.3f ms above mean, %zu flagged stages)",
                                        metrics[dominant], rolling.dominantExcessMs,
                                        rolling.highlightedStages);
                                } else if (rolling.spike[0]) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.69f, 0.32f, 1.0f),
                                        "  Total model-pass CPU P95 varies; no individual MDI stage crossed the threshold.");
                                } else {
                                    ImGui::TextDisabled("  No P95 CPU spike candidates in the sampled stages.");
                                }
                                // Long-lived expensive stages and bursty P95
                                // excursions are independent diagnoses. A row
                                // may satisfy both, even for steady MDI work.
                                if (rolling.hasBaseline) {
                                    if (rolling.dominantSustainedStage >= 0) {
                                        const size_t dominant =
                                            size_t(rolling.dominantSustainedStage);
                                        ImGui::TextColored(ImVec4(0.49f, 0.80f, 0.98f, 1.0f),
                                            "  Sustained CPU cost candidate: %s (P50 %.3f ms, %.0f%% of pass mean; %zu stages)",
                                            metrics[dominant],
                                            rolling.dominantSustainedP50Ms,
                                            100.0 * rolling.dominantSustainedShare,
                                            rolling.sustainedStages);
                                    } else {
                                        ImGui::TextDisabled(
                                            "  No sustained CPU stage cost above the heuristic thresholds.");
                                    }
                                }
                                for (size_t metric = 0; metric < rolling.meanMs.size(); ++metric) {
                                    const bool spiking = rolling.spike[metric];
                                    const bool steady = rolling.sustained[metric];
                                    if (spiking || steady) {
                                        const ImVec4 color = spiking
                                            ? ImVec4(1.0f, 0.69f, 0.32f, 1.0f)
                                            : ImVec4(0.49f, 0.80f, 0.98f, 1.0f);
                                        ImGui::TextColored(color,
                                            "    %s: %.3f / %.3f / %.3f ms [%s]",
                                            metrics[metric], rolling.meanMs[metric],
                                            rolling.p50Ms[metric], rolling.p95Ms[metric],
                                            spiking && steady ? "spike + sustained" :
                                            spiking ? "P95 variability" : "sustained CPU cost");
                                    } else {
                                        ImGui::Text("    %s: %.3f / %.3f / %.3f ms",
                                            metrics[metric], rolling.meanMs[metric],
                                            rolling.p50Ms[metric], rolling.p95Ms[metric]);
                                    }
                                }
                            }
                        }
                        ImGui::TextDisabled("Spike heuristic: >=30 samples; P95 >=0.25 ms, >=mean+0.15 ms, >=1.8x mean.");
                        ImGui::TextDisabled("Sustained-cost heuristic: >=30 samples; stage P50 >=0.35 ms and mean >=15%% of total pass mean.");
                        ImGui::TextDisabled("Warnings are CPU cost candidates, not GPU stalls, confirmed regressions or automatic tuning.");
                        ImGui::TextDisabled("Synchronous CPU time only; overlapping totals are not additive.");
                        ImGui::TextDisabled("P95 is nearest-rank over eligible model-pass samples, not whole frames.");
                        ImGui::TextDisabled("Profile is per beginScene view; reflections and auxiliary passes excluded.");
                    }
                    ImGui::TreePop();
                }
                const auto transfer = ed.renderer().indirectBufferUpdateStats();
                if (transfer.available) {
                    ImGui::Text("Backend snapshot writes: %.1f KiB GPU-visible / %.1f KiB CPU shadow",
                        double(transfer.mappedWriteBytes) / 1024.0,
                        double(transfer.cpuShadowWriteBytes) / 1024.0);
                    ImGui::Text("Backend compared / skipped / preserved: %.1f / %.1f / %.1f KiB",
                        double(transfer.comparedBytes) / 1024.0,
                        double(transfer.skippedWriteBytes) / 1024.0,
                        double(transfer.preservedBytes) / 1024.0);
                    ImGui::Text("Backend snapshots: %llu allocations / %llu reused / %llu updates",
                        (unsigned long long)transfer.allocations,
                        (unsigned long long)transfer.reused,
                        (unsigned long long)transfer.updateCalls);
                    ImGui::Text("Backend retained buffers: %.1f KiB (CPU shadow %.1f KiB)",
                        double(transfer.retainedBufferBytes) / 1024.0,
                        double(transfer.retainedCpuShadowBytes) / 1024.0);
                    ImGui::TextDisabled("Cumulative since command-buffer creation; no GPU readback.");
                } else {
                    ImGui::TextDisabled("Backend indirect snapshot counters unavailable.");
                }
                if (ImGui::TreeNode("Backend transfers by Scene model pass")) {
                    static constexpr const char* labels[] = {"Sun shadow", "Main opaque"};
                    for (size_t phase = 0; phase < 2; ++phase) {
                        const auto& delta = modelStats.mdiBackendPassTransfers[phase];
                        if (!delta.available) {
                            ImGui::TextDisabled("%s: no instrumented MDI activity", labels[phase]);
                            continue;
                        }
                        ImGui::Text("%s (%llu sampled invocations): %llu updates / %.1f KiB requested",
                            labels[phase],
                            (unsigned long long)modelStats.mdiBackendPassSamples[phase],
                            (unsigned long long)delta.updateCalls,
                            double(delta.requestedBytes) / 1024.0);
                        ImGui::Text("  CPU writes: %.1f KiB GPU-visible / %.1f KiB shadow",
                            double(delta.mappedWriteBytes) / 1024.0,
                            double(delta.cpuShadowWriteBytes) / 1024.0);
                        ImGui::Text("  Compared / skipped / preserved: %.1f / %.1f / %.1f KiB",
                            double(delta.comparedBytes) / 1024.0,
                            double(delta.skippedWriteBytes) / 1024.0,
                            double(delta.preservedBytes) / 1024.0);
                        ImGui::Text("  Buffer allocations / safe reuses: %llu / %llu",
                            (unsigned long long)delta.allocations,
                            (unsigned long long)delta.reused);
                    }
                    ImGui::TextDisabled("Deltas for this beginScene/view, not per GPU frame.");
                    ImGui::TextDisabled("Excludes reflection/auxiliary model passes and retained-memory gauges.");
                    ImGui::TreePop();
                }
                const auto layoutCache = ed.renderer().indirectLayoutCacheStats();
                ImGui::Text("MDI layout CPU cache: %zu entries / %.1f KiB retained",
                    layoutCache.entries, double(layoutCache.retainedBytes) / 1024.0);
                ImGui::Text("MDI layout cache: %llu retired / %llu capacity-bypassed attempts",
                    (unsigned long long)layoutCache.retired,
                    (unsigned long long)modelStats.mdiLayoutCacheBypassed);
                ImGui::TextDisabled("Per-pass 96-entry cap, 256 items/layout; stale after 120 scenes.");
                if (sceneCrossModelMode_ == 1 ||
                    (sceneCrossModelMode_ == 0 &&
                     [] {
                         const char* enabled = std::getenv("AIVERSE_STATIC_CROSS_MODEL_BATCHES");
                         return enabled && std::strcmp(enabled, "1") == 0;
                     }())) {
                    ImGui::Text("Static cross-model MDI: %llu submissions / %llu commands / %llu items",
                        (unsigned long long)modelStats.staticCrossModelSubmissions,
                        (unsigned long long)modelStats.staticCrossModelCommands,
                        (unsigned long long)modelStats.staticCrossModelItems);
                    ImGui::Text("Page preflight: %llu ready / %llu groups (%llu rejected)",
                        (unsigned long long)modelStats.staticCrossModelPreflightReady,
                        (unsigned long long)modelStats.staticCrossModelPreflightGroups,
                        (unsigned long long)modelStats.staticCrossModelPreflightRejected);
                    ImGui::Text("Page preflight selected: %llu visible items",
                        (unsigned long long)modelStats.staticCrossModelPreflightItems);
                    ImGui::Text("Pre-ranked pooled state groups: %llu",
                        (unsigned long long)modelStats.staticCrossModelRankedGroups);
                    ImGui::Text("Bucket-sorted static items: %llu in %llu ranked groups",
                        (unsigned long long)modelStats.staticCrossModelBucketedItems,
                        (unsigned long long)modelStats.staticCrossModelBucketedGroups);
                    ImGui::Text("Preflight workspace retained: %.1f KiB",
                        double(modelStats.staticCrossModelPreflightWorkspaceBytes) / 1024.0);
                    ImGui::TextDisabled("Reusable across views; cleared on scene reload, no retained GPU data.");
                    if (ed.renderer().cpuMdiAdaptiveEnabled()) {
                        ImGui::Text("Adaptive CPU MDI: skipped %llu / probe %llu passes, measured %llu attempts",
                            (unsigned long long)modelStats.staticCrossModelAdaptiveSkippedPasses,
                            (unsigned long long)modelStats.staticCrossModelAdaptiveProbePasses,
                            (unsigned long long)modelStats.staticCrossModelAdaptiveMeasuredAttempts);
                        ImGui::Text("Adaptive page groups: %llu skipped / %llu probed",
                            (unsigned long long)modelStats.staticCrossModelAdaptiveSkippedGroups,
                            (unsigned long long)modelStats.staticCrossModelAdaptiveProbeGroups);
                        const auto pages = ed.renderer().cpuMdiPageStats();
                        ImGui::Text("Page feedback: %zu tracked / %llu created / %llu reused",
                            pages.live, (unsigned long long)pages.created,
                            (unsigned long long)pages.reused);
                        ImGui::Text("Page feedback: %llu evicted / %llu untracked",
                            (unsigned long long)pages.evicted,
                            (unsigned long long)pages.untracked);
                        ImGui::Text("Over-budget pages: %zu shadow / %zu opaque",
                            pages.slowShadow, pages.slowOpaque);
                        const auto& cpu = ed.renderer().cpuMdiAdaptiveFeedback();
                        ImGui::Text("All pages shadow (diagnostic): %.2f us/command (%u)",
                            cpu[0].avgUsPerCommand, cpu[0].samples);
                        ImGui::Text("All pages opaque (diagnostic): %.2f us/command (%u)",
                            cpu[1].avgUsPerCommand, cpu[1].samples);
                        ImGui::Text("Adaptive budget: %.1f us/command (CPU submit only)",
                            ed.renderer().cpuMdiAdaptiveBudgetUs());
                        ImGui::TextDisabled("Decisions are per GPU page; aggregate averages never gate others.");
                        ImGui::TextDisabled("Feedback excludes GPU execution; 1 probe per 32 eligible page passes.");
                    }
                    ImGui::TextDisabled("Preflight is necessary, not a guarantee of a GPU MDI submission.");
                    ImGui::TextDisabled("Pooled GPU buffers and exact material grouping; independent view/LOD.");
                    ImGui::TextDisabled("Only accepted CPU MDI draws count, not grouping candidates.");
                }
                if (const char* pal = std::getenv("AIVERSE_MDI_INSTANCE_PALETTE");
                    pal && std::strcmp(pal, "1") == 0) {
                    ImGui::Text("Per-command PBR palette: %llu submissions / %llu commands",
                                (unsigned long long)modelStats.mdiPaletteSubmissions,
                                (unsigned long long)modelStats.mdiPaletteCommands);
                    ImGui::TextDisabled("Opt-in: same shader and textures; per-command base/metal/roughness.");
                }
                ImGui::Text("Material grouping: %llu reused / %llu hashed",
                    (unsigned long long)modelStats.materialGroupsReused,
                    (unsigned long long)modelStats.materialGroupsResolved);
                ImGui::TextDisabled("Across passes in this Scene frame; cache reset per beginScene.");
                ImGui::TextDisabled("One instanced call may draw multiple identical meshes.");
                ImGui::TextDisabled("Indirect command count is not the API submission count.");
                ImGui::TreePop();
            }
            if (!mdiBackend)
                ImGui::TextDisabled("Metal MDI/ICB unavailable: distinct meshes remain separate draws.");
            if (const char* authored = std::getenv("AIVERSE_GPU_AUTHORED_DRAWS");
                authored && std::strcmp(authored, "1") == 0) {
                const auto gpu = ed.renderer().gpuModelDrawStats();
                ImGui::Text("GPU-authored opaque: %u eligible / %u single draws",
                            gpu.eligible, gpu.submitted);
                if (const char* rigid = std::getenv("AIVERSE_GPU_ICB_RIGID");
                    rigid && std::strcmp(rigid, "1") == 0)
                    ImGui::Text("GPU ICB rigid dynamic candidates: %u",
                                gpu.dynamicRigidEligible);
                if (const char* icb = std::getenv("AIVERSE_GPU_ENCODED_ICB");
                    icb && std::strcmp(icb, "1") == 0) {
                    ImGui::Text("GPU-encoded ICB: %u submissions / %u commands",
                                gpu.gpuEncodedSubmissions, gpu.gpuEncodedCommands);
                    ImGui::Text("GPU ICB batches: %u candidates, %u prepared, %u rejected, %u execute failed",
                                gpu.gpuIcbCandidates, gpu.gpuIcbPrepared,
                                gpu.gpuIcbRejected, gpu.gpuIcbExecuteFailed);
                    ImGui::Text("GPU instance buffer: %.2f KiB copied this GPU frame",
                                double(ed.device().gpuIcbInstanceUploadBytes()) / 1024.0);
                    if (const char* lod = std::getenv("AIVERSE_GPU_ICB_LOD");
                        lod && std::strcmp(lod, "1") == 0)
                        ImGui::Text("GPU two-level LOD candidates: %u", gpu.gpuLodEligible);
                    ImGui::Text("GPU ICB preflight: %u potential groups",
                                gpu.preflightGroups);
                    ImGui::Text("Persistent batch plan: %u hits / %u misses (this frame)",
                                gpu.batchCacheHits, gpu.batchCacheMisses);
                    if (const char* adaptive = std::getenv("AIVERSE_GPU_ICB_ADAPTIVE");
                        adaptive && std::strcmp(adaptive, "1") == 0) {
                        if (gpu.adaptiveInstancing)
                            ImGui::TextDisabled("Adaptive GPU ICB: direct instancing selected; GPU work skipped.");
                        else
                            ImGui::TextDisabled("Adaptive GPU ICB grouping enabled.");
                    }
                    if (gpu.gpuIcbRejected || gpu.gpuIcbExecuteFailed)
                        ImGui::TextWrapped("Metal backend error: %s",
                                           ed.device().lastError().c_str());
                }
                ImGui::Text("GPU indirect args: %s",
                            gpu.prepared ? "compute generated" : "not ready / unsupported");
                ImGui::Text("GPU draw record uploads: %u records / %u ranges (%.2f KiB)",
                            gpu.uploadedRecords, gpu.uploadRanges,
                            double(gpu.uploadBytes) / 1024.0);
                if (const char* c = std::getenv("AIVERSE_GPU_COMPACT_DRAW_RECORDS");
                    c && std::strcmp(c, "1") == 0)
                    ImGui::Text("GPU visibility compaction: %s (not rendered yet)",
                                gpu.compacted ? "generated" : "unavailable");
                if (const char* adaptive = std::getenv("AIVERSE_GPU_ICB_ADAPTIVE");
                    adaptive && std::strcmp(adaptive, "1") == 0)
                    ImGui::TextDisabled("Adaptive fallback: conventional instancing/CPU MDI.");
                else
                    ImGui::TextDisabled("Experimental GPU draws may be slower than instancing.");
            }
            if (const char* persistent = std::getenv("AIVERSE_PERSISTENT_GPU_SCENE");
                persistent && std::strcmp(persistent, "1") == 0) {
                const auto stats = ed.renderer().persistentSceneStats();
                ImGui::Text("Persistent GPU scene: %u objects", stats.live);
                ImGui::Text("Scene upload: %u records / %u ranges (%.1f KiB)",
                            stats.records, stats.ranges, double(stats.bytes) / 1024.0);
                if (const char* cull = std::getenv("AIVERSE_GPU_SCENE_CULL");
                    cull && std::strcmp(cull, "1") == 0)
                    ImGui::Text("Metal GPU frustum: %s",
                                stats.gpuFrustumDispatched ? "compute dispatched" : "unavailable");
                ImGui::TextDisabled("Diagnostics only: visibility flags are not used for draws.");
            }
            if (ImGui::Button("Compare A/B same frame##scene_batch") && !benchmarkActive_)
                measureBatchingNextFrame_ = true;
            if (mdiBackend && !ed.playing()) {
                if (!benchmarkActive_) {
                    const bool threeModes = std::getenv("AIVERSE_GPU_AUTHORED_DRAWS") &&
                        std::strcmp(std::getenv("AIVERSE_GPU_AUTHORED_DRAWS"), "1") == 0;
                    if (ImGui::Button(threeModes
                        ? "Benchmark Instancing / CPU MDI / GPU-authored (3 x 300)"
                        : "Benchmark MDI OFF / ON (3 x 300 frames each)")) {
                        benchmarkActive_ = true;
                        benchmarkDone_ = false;
                        benchmarkThreeModes_ = threeModes;
                        benchmarkBlockCount_ = threeModes ? 9 : 6;
                        ++benchmarkGeneration_;
                        benchmarkGpuSequence_ = 0;
                        benchmarkPhase_ = 0;
                        benchmarkMode_ = 0;
                        benchmarkPhaseSamples_ = 0;
                        benchmarkWarmup_ = 60;
                        benchmarkCamera_ = ed.sceneCam;
                        for (int mode = 0; mode < 3; ++mode) {
                            benchmarkSamples_[mode].clear();
                            benchmarkGpuSamples_[mode].clear();
                            benchmarkDrawSum_[mode] = 0;
                            benchmarkIndirectSum_[mode] = 0;
                            benchmarkCommandSum_[mode] = 0;
                            benchmarkGpuAuthoredSum_[mode] = 0;
                            benchmarkGpuIcbSum_[mode] = 0;
                        }
                        measureBatchingNextFrame_ = false;
                    }
                } else {
                    const char* modeName = benchmarkMode_ == 0 ? "Instancing" :
                        benchmarkMode_ == 1 ? "CPU MDI" : "GPU-authored";
                    ImGui::Text("Benchmark: block %d/%d, %s, warmup %d, samples %d / 300",
                                benchmarkPhase_ + 1, benchmarkBlockCount_, modeName,
                                benchmarkWarmup_, benchmarkPhaseSamples_);
                    if (ImGui::Button("Cancel benchmark")) benchmarkActive_ = false;
                }
                if (benchmarkDone_) {
                    auto stats = [](std::vector<double> v) {
                        std::sort(v.begin(), v.end());
                        double sum = 0.0;
                        for (double n : v) sum += n;
                        return std::pair<double,double>{sum / double(v.size()), v[(v.size() * 95 - 1) / 100]};
                    };
                    const auto off = stats(benchmarkSamples_[0]);
                    const auto on = stats(benchmarkSamples_[1]);
                    ImGui::Text("Scene CPU OFF: avg %.3f / P95 %.3f ms, draws %.0f",
                                off.first, off.second, double(benchmarkDrawSum_[0]) / 900.0);
                    ImGui::Text("Scene CPU ON:  avg %.3f / P95 %.3f ms, draws %.0f",
                                on.first, on.second, double(benchmarkDrawSum_[1]) / 900.0);
                    ImGui::Text("MDI CPU delta: %+.1f%%", (on.first / std::max(off.first, 0.0001) - 1.0) * 100.0);
                    ImGui::Text("MDI ON: %.1f indirect submissions, %.1f GPU commands / frame",
                                double(benchmarkIndirectSum_[1]) / 900.0,
                                double(benchmarkCommandSum_[1]) / 900.0);
                    if (benchmarkThreeModes_) {
                        const auto gpu = stats(benchmarkSamples_[2]);
                        ImGui::Text("GPU-authored CPU: avg %.3f / P95 %.3f ms, draws %.0f",
                                    gpu.first, gpu.second, double(benchmarkDrawSum_[2]) / 900.0);
                        ImGui::Text("GPU vs CPU MDI: %+.1f%%; GPU vs Instancing: %+.1f%%",
                                    (gpu.first / std::max(on.first, 0.0001) - 1.0) * 100.0,
                                    (gpu.first / std::max(off.first, 0.0001) - 1.0) * 100.0);
                        ImGui::Text("GPU-authored commands: %.1f/frame, GPU ICB submissions: %.1f/frame",
                                    double(benchmarkGpuAuthoredSum_[2]) / 900.0,
                                    double(benchmarkGpuIcbSum_[2]) / 900.0);
                        ImGui::TextDisabled("3 x 300 per mode; order ABC CBA ABC; P95 pooled.");
                    } else ImGui::TextDisabled("3 x 300 samples/mode, ABBAAB order; P95 pooled.");
                    if (ed.device().info().backend == gfx::Backend::Metal) {
                        const int modes = benchmarkThreeModes_ ? 3 : 2;
                        const char* names[] = {"Instancing", "CPU MDI", "GPU-authored"};
                        for (int mode = 0; mode < modes; ++mode) {
                            if (benchmarkGpuSamples_[mode].empty()) continue;
                            const auto timing = stats(benchmarkGpuSamples_[mode]);
                            ImGui::Text("%s full GPU frame: avg %.3f / P95 %.3f ms (%zu samples)",
                                        names[mode], timing.first, timing.second,
                                        benchmarkGpuSamples_[mode].size());
                        }
                        ImGui::TextDisabled("GPU: tagged full editor command buffer, includes UI/other passes.");
                    }
                    ImGui::TextDisabled("Fixed camera; CPU is Scene render only. GPU results are asynchronous.");
                    ImGui::TextDisabled("Same geometry pool in both modes; excludes asset upload.");
                }
            }
            if (comparisonValid_) {
                ImGui::Text("Total draws: baseline %lld -> batched %lld (%+lld)",
                            (long long)baselineDraws_, (long long)optimizedDraws_,
                            (long long)(optimizedDraws_ - baselineDraws_));
                ImGui::Text("Model pass draws: %lld -> %lld (%+lld)",
                            (long long)baselineModelDraws_, (long long)optimizedModelDraws_,
                            (long long)(optimizedModelDraws_ - baselineModelDraws_));
                ImGui::TextDisabled("Same camera/frame; excludes GPU timing and upload cost.");
            }
            const double gpuMs = ed.device().gpuFrameMs();
            if (gpuMs > 0.0) ImGui::Text("GPU frame: %.3f ms (last completed)", gpuMs);
            else ImGui::TextDisabled("GPU frame timing unavailable / not ready");
            ImGui::Text("Main colour model calls (opaque + blended): %u",
                        profile.draws[1] + profile.draws[2]);
            if (profile.drawMs[1] > 0.0 || profile.drawMs[2] > 0.0)
                ImGui::Text("CPU model draw phase: %.3f ms (profiling enabled)",
                            profile.drawMs[1] + profile.drawMs[2]);
            else ImGui::TextDisabled("CPU model draw timing requires renderer profiling.");
            ImGui::TextDisabled("Draw Calls view: colour per main-view model submission.");
            ImGui::TextDisabled("Shadows/reflections are not separately colour-coded.");
            ImGui::TextDisabled("Terrain, decals and custom passes may use other draw paths.");
            ImGui::TextDisabled("Road merge reduces chunks, not material primitive count.");
            ImGui::TextDisabled("The debug view is not a total GPU draw-call heatmap.");
            if (ImGui::Button("Show draw-call colours")) studioDebugMode_ = 1;
            ImGui::Separator();
            ImGui::TextDisabled("Instance limit applies to the shared renderer (Scene and Game).");
            ImGui::TextDisabled("Static/model material merge is decided at upload.");
            if (ImGui::Button("Reload models from asset policies")) ed.reloadRules();
            ImGui::TextWrapped("Use per-asset batching policy before upload; changing an uploaded model requires reload.");
            ImGui::EndPopup();
        }
    }

    void toolbar(Editor& ed) {
        static const char* names[] = {"Выбор", "Объекты", "Клетки", "Уровни", "Высота", "Поля", "Регионы", "Зоны", "Старты", "Дороги", "Ластик"};
        static const char* icons[] = {ICON_SELECT, ICON_OBJECTS, ICON_CELLS, ICON_LEVELS, ICON_HEIGHT, ICON_FIELDS, ICON_REGIONS, ICON_ZONES,
                                      ICON_STARTS, ICON_ROADS, ICON_ERASER};
        static_assert(sizeof names / sizeof *names == size_t(Tool::Count) && sizeof icons / sizeof *icons == size_t(Tool::Count));
        const float bottom = ui::beginToolbar();
        // On narrow viewports expose additional tools from a popup rather than
        // clipping the toolbar. All tools remain available by shortcuts 1..0.
        const bool compact = ImGui::GetContentRegionAvail().x < 760.0f;
        ui::beginSegment();
        for (int i = 0; i < (compact ? 5 : int(Tool::Count)); ++i) {
            if (i == 1 || i == 10) { ui::endSegment(); ImGui::SameLine(0, 8); ui::beginSegment(); } // select | edit tools | eraser
            else if (i) ImGui::SameLine();
            char tip[64];
            if (i < 10) std::snprintf(tip, sizeof tip, "%s  (%d)", names[i], (i + 1) % 10); else std::snprintf(tip, sizeof tip, "%s", names[i]);
            if (ui::toolButton(icons[i], tip, ed.tool == Tool(i))) { ed.tool = Tool(i); ed.roadStart = -1; }
        }
        ui::endSegment();
        if (ed.tool == Tool::Select) {
            ui::toolbarDivider();
            DocumentEntityRef ref;
            const bool hasDocRef = documentEntityFromSelection(ed.selection, ref);
            const Json* fpsSource = hasDocRef && ref.source ==
                DocumentEntityRef::Source::FpsCollider
                ? fpsDocumentEntity(ed.doc().data.fps, ref) : nullptr;
            const bool fpsOriented = fpsSource && (*fpsSource)["yaw"].isNumber();
            const DocEntity* rtsSource = hasDocRef && ref.source ==
                DocumentEntityRef::Source::MapEntity
                ? documentMapEntity(ed.doc(), ref) : nullptr;
            const bool rtsFacing = rtsSource &&
                rtsSource->extra["facing"].isNumber();
            if ((ed.transformTool == TransformTool::Rotate &&
                 !fpsOriented && !rtsFacing) ||
                (ed.transformTool == TransformTool::Scale && !fpsOriented))
                ed.transformTool = TransformTool::Move;
            for (const auto mode : {TransformTool::Move,
                                    TransformTool::Rotate, TransformTool::Scale}) {
                const char* title = mode == TransformTool::Move ? "Move" :
                                    mode == TransformTool::Rotate ? "Rotate" : "Scale";
                if (mode == TransformTool::Rotate && !fpsOriented && !rtsFacing)
                    continue;
                if (mode == TransformTool::Scale && !fpsOriented) continue;
                ImGui::SameLine(0, 5);
                if (ImGui::SmallButton(title)) ed.transformTool = mode;
            }
        }
        if (compact) {
            ImGui::SameLine(0, 5);
            if (ui::toolButton(ICON_MORE, "Другие инструменты")) ImGui::OpenPopup("##scene_more_tools");
            if (ImGui::BeginPopup("##scene_more_tools")) {
                for (int i = 5; i < int(Tool::Count); ++i)
                    if (ImGui::MenuItem(names[i], nullptr, ed.tool == Tool(i))) { ed.tool = Tool(i); ed.roadStart = -1; }
                ImGui::EndPopup();
            }
        }
        ui::toolbarDivider();
        // Studio visual diagnostics: actual built-in GPU shader views.
        static const char* studioModes[] = {"Shaded", "Draw Calls", "Normals", "UV", "Depth", "Albedo", "Roughness", "Metallic", "Emissive", "World Position"};
        ImGui::SetNextItemWidth(155.0f);
        if (ImGui::BeginCombo("##sceneVisualisation", studioModes[studioDebugMode_])) {
            for (int i = 0; i < 10; ++i) {
                if (ImGui::Selectable(studioModes[i], studioDebugMode_ == i)) studioDebugMode_ = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(i == 1 ? "Цвет на GPU submission основного прохода моделей. Дороги с разными материалами и custom/decal passes могут не объединяться; тени считаются отдельно." :
                                                         i >= 2 ? "Диагностика моделей; terrain и callback renderers могут оставаться без раскраски" : "Обычный материал и освещение");
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        batchingControls(ed);
        ImGui::SameLine();
        lodControls(ed);
        ImGui::SameLine();
        if (ui::toolButton(ICON_EYE, "Отладочные gizmo")) ImGui::OpenPopup("##studio_gizmos");
        if (ImGui::BeginPopup("##studio_gizmos")) {
            ImGui::TextDisabled("Scene overlays");
            ImGui::Checkbox("Границы объектов", &studioDrawBounds_);
            ImGui::Checkbox("Оси мира", &studioDrawAxes_);
            ImGui::Checkbox("Выделение", &studioDrawSelection_);
            ImGui::Separator();
            ImGui::TextDisabled("RTS debugging");
            ImGui::Checkbox("Уровни и рампы", &ed.showLevels);
            ImGui::Checkbox("Регионы", &ed.showRegions);
            ImGui::Checkbox("Триггерные зоны", &ed.showAreas);
            ImGui::Separator();
            ImGui::TextDisabled("Природа: направления");
            auto& nature = natureGizmoSettings();
            ImGui::Checkbox("PHYSIS: ветер", &nature.physisWind);
            ImGui::Checkbox("PHYSIS: распространение огня", &nature.physisFire);
            ImGui::Checkbox("NEREID: течение выделенной воды", &nature.nereidFlow);
            ImGui::Checkbox("NEREID: течение всей воды", &nature.nereidAllWater);
            ImGui::Checkbox("TERRA: уклон выделенного рельефа", &nature.terraSlope);
            ImGui::Checkbox("TERRA: уклон вокруг камеры", &nature.terraSlopeAtTarget);
            ImGui::Checkbox("TERRA: ветер у выделенного дерева", &nature.terraTreeWind);
            ImGui::Checkbox("Зоны размыва (коэффициент)", &nature.erosionZones);
            ImGui::SliderFloat("Радиус, м", &nature.radiusMeters, 8.f, 200.f, "%.0f");
            ImGui::SliderFloat("Шаг, м", &nature.spacingMeters, 2.f, 32.f, "%.0f");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ui::toolbarDivider();
        if (ui::toolButton(ICON_FIT, "Вся карта (F)")) ed.fitScene();
        ImGui::SameLine();
        if (ui::toolButton(ICON_MAGNET, ed.snap ? "Привязка к сетке: вкл" : "Привязка к сетке: выкл", ed.snap)) ed.snap = !ed.snap;
        ImGui::SameLine();
        if (ui::toolButton(ICON_EYE, "Что показывать")) ImGui::OpenPopup("##show");
        if (ImGui::BeginPopup("##show")) {
            ImGui::MenuItem("Уровни и рампы", nullptr, &ed.showLevels);
            ImGui::MenuItem("Регионы", nullptr, &ed.showRegions);
            ImGui::MenuItem("Зоны триггеров", nullptr, &ed.showAreas);
            ImGui::EndPopup();
        }
        const bool brushTool = ed.tool == Tool::Terrain || ed.tool == Tool::Level || ed.tool == Tool::Height || ed.tool == Tool::Field || ed.tool == Tool::Region;
        if (brushTool) {
            ui::toolbarDivider();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(ICON_BRUSH);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110);
            ImGui::SliderInt("##brush", &ed.brush, 1, 16, "кисть %d");
        }
        ImGui::SameLine(0, 12);
        ImGui::AlignTextToFramePadding();
        if (int(ed.tool) < int(Tool::Count)) ImGui::TextDisabled("%s", names[int(ed.tool)]);
        ImGui::SameLine(); ImGui::TextDisabled("·");
        ImGui::SameLine(); ImGui::TextDisabled("Grid 1м");
        ImGui::SameLine(); ImGui::TextDisabled("·");
        ImGui::SameLine(); ImGui::TextDisabled(ed.snap ? "Snap ON" : "Snap OFF");
        ImGui::SameLine(); ImGui::TextDisabled("·");
        ImGui::SameLine(); ImGui::TextDisabled("%zu объектов", ed.doc().data.entities.size());
        ui::endToolbar(bottom);
    }

    // planet maps: wheel zoom (surface -> planet -> system), RMB drag turn + pitch, MMB drag / WASD move over
    // the surface, Tab next body, F back to the start body
    void planetInput(Editor& ed) {
        ImGuiIO& io = ImGui::GetIO();
        game::PlanetCamera& pc = ed.view().planetCamera();
        if (vp_.hovered && io.MouseWheel != 0) pc.zoom(std::pow(0.88f, io.MouseWheel));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && vp_.hovered) looking_ = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && vp_.hovered) panning_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) looking_ = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;
        if (looking_) {
            pc.rotate(-io.MouseDelta.x * 0.006f);
            pc.pitch = std::clamp(pc.pitch + io.MouseDelta.y * 0.005f, 0.15f, 1.55f);
        }
        if (panning_) pc.pan(io.MouseDelta.x, io.MouseDelta.y, vp_.pointsH);
        if (!io.WantTextInput && (ImGui::IsWindowFocused() || vp_.hovered)) {
            const float speed = std::max(pc.distance, 6.0f) * 0.9f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) pc.move(0, speed);
            if (ImGui::IsKeyDown(ImGuiKey_S)) pc.move(0, -speed);
            if (ImGui::IsKeyDown(ImGuiKey_D)) pc.move(speed, 0);
            if (ImGui::IsKeyDown(ImGuiKey_A)) pc.move(-speed, 0);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) pc.rotate(io.DeltaTime * 1.6f);
            if (ImGui::IsKeyDown(ImGuiKey_E)) pc.rotate(-io.DeltaTime * 1.6f);
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
                const auto& W = ed.view().planets().world();
                for (size_t k = 1; k <= W.bodies.size(); ++k) {
                    const int b = int((size_t(pc.body()) + k) % W.bodies.size());
                    if (W.bodies[size_t(b)].surface()) { pc.focus(b); break; }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) ed.fitScene();
        }
    }

    void planetDown(Editor& ed, float sx, float sy, ImGuiIO& io) {
        const int n = ed.pickEntity(vp_, ed.sceneCam, sx, sy);
        const Json at = ed.planetPosAt(vp_, sx, sy);
        auto& doc = ed.doc();
        auto& d = doc.data;
        switch (ed.tool) {
        case Tool::Select:
            ed.selectEntity(n);
            if (n >= 0) { doc.beginEdit(); dragging_ = true; }
            break;
        case Tool::Place: {
            if (n >= 0 && !io.KeyShift) { ed.selectEntity(n); break; }
            if (!at.isObject() || ed.rules().types.empty()) break;
            DocEntity e;
            e.type = ed.rules().types[size_t(ed.placeType)].id;
            e.owner = ed.placeOwner;
            e.at = at; e.x = float(at["x"].toDouble()); e.z = float(at["y"].toDouble());
            ed.selectEntity(doc.addEntity(e));
            ed.status = "Добавлено: " + ed.rules().types[size_t(ed.placeType)].name;
            break;
        }
        case Tool::Start:
            if (!at.isObject() || ed.startPlayer < 1 || ed.startPlayer > int(d.players.size())) break;
            doc.beginEdit();
            d.players[size_t(ed.startPlayer - 1)].hasStart = true;
            d.players[size_t(ed.startPlayer - 1)].startAt = at;
            doc.touched();
            break;
        case Tool::Terrain:
            if (!at.isObject()) break;
            doc.beginEdit();
            painting_ = true;
            planetDrag(ed, sx, sy, io);
            break;
        case Tool::Erase:
            if (n >= 0) { doc.removeEntity(n); ed.select({}); }
            break;
        default: ed.status = "На картах планет: выбор, объекты, клетки, старты, ластик; рельеф — окно «Рельеф»"; break;
        }
    }

    void planetDrag(Editor& ed, float sx, float sy, ImGuiIO& io) {
        auto& doc = ed.doc();
        auto& d = doc.data;
        const Json at = ed.planetPosAt(vp_, sx, sy);
        if (!at.isObject()) return;
        const int sel = ed.selectedEntity();
        if (dragging_ && sel >= 0 && sel < int(d.entities.size())) {
            auto& e = d.entities[size_t(sel)];
            e.at = at; e.x = float(at["x"].toDouble()); e.z = float(at["y"].toDouble());
            doc.touched();
        }
        if (painting_ && ed.tool == Tool::Terrain) {
            int body; render::float3 local;
            if (ed.planetLocal(at, body, local)) planetPaint(doc, at.getStr("body"), normalize(local), float(ed.brush) - 0.5f, io.KeyShift ? '.' : ed.paintCell);
        }
    }

    void input(Editor& ed) {
        ImGuiIO& io = ImGui::GetIO();
        RTSCamera& cam = ed.sceneCam;
        const float mx = io.MousePos.x, my = io.MousePos.y;
        if (ed.planetMap()) { // planet camera; tools: select / move / place / erase objects, start, cells
            planetInput(ed);
            hoverX_ = mx; hoverY_ = my;
            if (ed.playing()) return;
            if (!io.WantTextInput && (ImGui::IsWindowFocused() || vp_.hovered)) {
                if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && !ed.selectedEntities().empty())
                    ed.removeSelectedEntities();
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ed.select({});
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vp_.hovered) planetDown(ed, mx, my, io);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (dragging_ || painting_) && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) planetDrag(ed, mx, my, io);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragging_ = painting_ = false;
            return;
        }
        // camera
        if (vp_.hovered && io.MouseWheel != 0) cam.distance = std::clamp(cam.distance * std::pow(0.88f, io.MouseWheel), cam.minDistance, cam.maxDistance);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && vp_.hovered) looking_ = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && vp_.hovered) panning_ = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) looking_ = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) panning_ = false;
        if (looking_) {
            cam.yaw -= io.MouseDelta.x * 0.006f;
            cam.pitch = std::clamp(cam.pitch + io.MouseDelta.y * 0.005f, -1.553343f, 1.553343f);
        }
        // Free fly: WASD/QE move the camera target while Scene is focused/hovered.
        // Right mouse remains orbit/look, middle mouse pans, Shift increases speed.
        const bool cameraKeys = !io.WantTextInput && !io.KeyCtrl && !io.KeySuper &&
                                (vp_.hovered || (ImGui::IsWindowFocused() && looking_));
        if (cameraKeys) {
            const float speed = cam.distance * 0.9f * io.DeltaTime * (io.KeyShift ? 3.0f : 1.0f);
            const float3 fwd{-std::sin(cam.yaw), 0, -std::cos(cam.yaw)};
            const float3 right{std::cos(cam.yaw), 0, -std::sin(cam.yaw)};
            if (ImGui::IsKeyDown(ImGuiKey_W)) cam.target += fwd * speed;
            if (ImGui::IsKeyDown(ImGuiKey_S)) cam.target += fwd * -speed;
            if (ImGui::IsKeyDown(ImGuiKey_D)) cam.target += right * speed;
            if (ImGui::IsKeyDown(ImGuiKey_A)) cam.target += right * -speed;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) cam.target.y -= speed;
            if (ImGui::IsKeyDown(ImGuiKey_E)) cam.target.y += speed;
        }
        if (panning_) {
            float x0, z0, x1, z1;
            if (ed.groundAt(vp_, cam, mx - io.MouseDelta.x, my - io.MouseDelta.y, x0, z0) && ed.groundAt(vp_, cam, mx, my, x1, z1))
                cam.target += float3{x0 - x1, 0, z0 - z1};
        }
        if (!io.WantTextInput && ImGui::IsWindowFocused() && !looking_) {
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                float fx, fz;
                if (ed.selectionPos(ed.selection, fx, fz)) ed.focusScene(fx, fz, 18);
                else ed.fitScene();
            }
            if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && !ed.selectedEntities().empty())
                ed.removeSelectedEntities();
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) ed.brush = std::max(1, ed.brush - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) ed.brush = std::min(16, ed.brush + 1);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { ed.select({}); ed.roadStart = -1; }
            // 1..9, 0: tools in toolbar order (the eraser is the 11th: Delete with nothing selected is not used for it)
            if (!io.KeyCtrl && !io.KeySuper && !io.KeyAlt)
                for (int k = 0; k < 10 && k < int(Tool::Count); ++k)
                    if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_1 + (k == 9 ? -1 : k)), false)) { ed.tool = Tool(k); ed.roadStart = -1; }
        }
        // the object under the cursor: highlighted here and in the Hierarchy
        if (vp_.hovered && !looking_ && !panning_ && (ed.tool == Tool::Select || ed.playing())) ed.setHover(ed.pickObject(vp_, cam, mx, my));
        if (ed.playing()) {   // play: the Select tool picks live objects for the Inspector (read-only)
            if (ed.tool == Tool::Select && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vp_.hovered) pickSelect(ed, ed.pickObject(vp_, cam, mx, my), io);
            hoverX_ = mx; hoverY_ = my;
            return;
        }
        // tools
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vp_.hovered) down(ed, mx, my, io);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (dragging_ || painting_) && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) drag(ed, mx, my, io);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) up(ed, mx, my);
        hoverX_ = mx; hoverY_ = my;
    }

    // Hit-test the projected move handles, independently of entity mesh picking.
    // Component-authored entities expose XYZ; legacy map units retain X/Z.
    int moveHandle(Editor& ed, float sx, float sy) {
        if (ed.playing() || ed.tool != Tool::Select || ed.planetMap()) return -1;
        float wx, wz;
        if (!ed.selectionPos(ed.selection, wx, wz)) return -1;
        float wy = ed.view().heightAt(ed.editGrid(), wx, wz) + 0.4f;
        if (ed.selection.kind == Selection::Entity && ed.selection.index >= 0 &&
            size_t(ed.selection.index) < ed.doc().data.entities.size() &&
            authoredEntity(ed.doc().data.entities[size_t(ed.selection.index)]))
            wy = float(authoredLocalTransform(ed.doc().data.entities[size_t(ed.selection.index)]).position.y.toDouble()) + 0.4f;
        if (ed.transformTool == TransformTool::Rotate &&
            ed.selection.kind == Selection::Entity &&
            ed.selection.index >= 0 &&
            size_t(ed.selection.index) < ed.doc().data.entities.size() &&
            ed.doc().data.entities[size_t(ed.selection.index)]
                .extra["facing"].isNumber()) {
            return pickRotateYawGizmoRing(ed.sceneCam, vp_.pointsW, vp_.pointsH,
                vp_.originX, vp_.originY, sx, sy,
                {wx, wy, wz}, 1.5f) ? 3 : -1;
        }
        if (ed.transformTool != TransformTool::Move || ed.selectedEntity() < 0 ||
            size_t(ed.selectedEntity()) >= ed.doc().data.entities.size()) return -1;
        return pickMoveGizmoAxis(ed.sceneCam, vp_.pointsW, vp_.pointsH,
                                  vp_.originX, vp_.originY, sx, sy,
                                  {wx, wy, wz}, 1.5f,
                                  authoredEntity(ed.doc().data.entities[size_t(ed.selectedEntity())]) ? 0x7 : 0x5);
    }

    void down(Editor& ed, float sx, float sy, ImGuiIO& io) {
        int n = ed.pickEntity(vp_, ed.sceneCam, sx, sy);
        float x = 0, z = 0;
        bool ground = ed.groundAt(vp_, ed.sceneCam, sx, sy, x, z);
        auto& doc = ed.doc();
        auto& d = doc.data;
        const auto& rules = ed.rules();
        switch (ed.tool) {
        case Tool::Select: {
            const int handle = moveHandle(ed, sx, sy);
            if (handle >= 0 && ed.selectedEntity() >= 0 && (ground || handle == 1)) {
                gizmoAxis_ = handle;
                dragging_ = true;
                dragMoved_ = false;
                gizmoGroundX_ = x;
                gizmoGroundZ_ = z;
                gizmoLastScreenX_ = sx;
                gizmoLastScreenY_ = sy;
                break;
            }
            const Selection picked = ed.pickObject(vp_, ed.sceneCam, sx, sy);
            const bool keepSet = picked.kind == Selection::Entity && ed.isSelected(picked) && !io.KeyCtrl && !io.KeySuper && !io.KeyShift;
            if (!keepSet) pickSelect(ed, picked, io);   // a click on a selected object keeps the set: drag moves all of it
            else ed.selectAdd(picked);
            keepSetClick_ = keepSet && ed.selected().size() > 1;
            clicked_ = picked;
            dragMoved_ = false;
            if (ground && n >= 0 && picked.kind == Selection::Entity && ed.isSelected(picked) && !io.KeyCtrl && !io.KeySuper && !io.KeyShift) {
                dragging_ = true;
                dragOffX_ = d.entities[size_t(n)].x - x;
                dragOffZ_ = d.entities[size_t(n)].z - z;
                if (io.KeyAlt) { // Alt+drag copies
                    doc.beginEdit();
                    DocEntity copy = d.entities[size_t(n)];
                    copy.id.clear();
                    d.entities.push_back(copy);
                    ed.selectEntity(int(d.entities.size() - 1));
                }
            }
            break;
        }
        case Tool::Place: {
            if (n >= 0 && !io.KeyShift) { ed.selectEntity(n); break; }
            if (!ground || rules.types.empty()) break;
            DocEntity e;
            e.type = rules.types[size_t(ed.placeType)].id;
            e.owner = ed.placeOwner;
            e.x = ed.snapCoord(x, ed.placeType); e.z = ed.snapCoord(z, ed.placeType);
            ed.selectEntity(doc.addEntity(e));
            ed.status = "Добавлено: " + rules.types[size_t(ed.placeType)].name;
            break;
        }
        case Tool::Terrain: case Tool::Level: case Tool::Height: case Tool::Field: case Tool::Region:
            if (!ground) break;
            doc.beginEdit();
            painting_ = true;
            drag(ed, sx, sy, io);
            break;
        case Tool::Area:
            if (!ground) break;
            areaDrag_ = true;
            areaX0_ = std::floor(x); areaZ0_ = std::floor(z);
            break;
        case Tool::Start:
            if (!ground || ed.startPlayer < 1 || ed.startPlayer > int(d.players.size())) break;
            doc.beginEdit();
            d.players[size_t(ed.startPlayer - 1)].hasStart = true;
            d.players[size_t(ed.startPlayer - 1)].startX = std::round(x * 2) / 2;
            d.players[size_t(ed.startPlayer - 1)].startZ = std::round(z * 2) / 2;
            break;
        case Tool::Road:
            if (n < 0) { ed.roadStart = -1; break; }
            if (ed.roadStart < 0) { ed.roadStart = n; break; }
            if (n != ed.roadStart) {
                bool added = doc.toggleRoad(ed.roadStart, n);
                ed.status = added ? "Дорога добавлена" : "Дорога удалена";
                ed.roadStart = io.KeyShift ? n : -1;
            }
            break;
        case Tool::Erase:
            if (n >= 0) { doc.removeEntity(n); ed.select({}); }
            else if (int r = ed.pickRoad(vp_, ed.sceneCam, sx, sy); r >= 0) doc.removeRoad(size_t(r));
            break;
        default: break;
        }
    }

    void drag(Editor& ed, float sx, float sy, ImGuiIO& io) {
        float x, z;
        if (!ed.groundAt(vp_, ed.sceneCam, sx, sy, x, z) && gizmoAxis_ != 1) return;
        auto& doc = ed.doc();
        auto& d = doc.data;
        int sel = ed.selectedEntity();
        if (dragging_ && gizmoAxis_ >= 0 && sel >= 0 && sel < int(d.entities.size())) {
            if (gizmoAxis_ == 1 && ed.transformTool == TransformTool::Move) {
                const double dy = double(gizmoLastScreenY_ - sy) *
                    std::max(0.01f, ed.sceneCam.distance * 0.002f);
                if (std::isfinite(dy) && std::abs(dy) > 1e-6) {
                    std::vector<DocumentEntityRef> refs;
                    for (int index : ed.selectedEntities()) {
                        if (index < 0 || size_t(index) >= d.entities.size()) continue;
                        const auto& entity = d.entities[size_t(index)];
                        if (authoredEntity(entity) && !ed.objectLocked(entity.owner, entity.type))
                            refs.push_back(DocumentEntityRef::mapEntity(size_t(index)));
                    }
                    if (!refs.empty()) {
                        if (!dragMoved_) doc.beginEdit();
                        dragMoved_ = translateDocumentEntities(doc, refs, {0.0, dy, 0.0}) || dragMoved_;
                    }
                }
                gizmoLastScreenY_ = sy;
            } else if (gizmoAxis_ == 3 && ed.transformTool == TransformTool::Rotate &&
                d.entities[size_t(sel)].extra["facing"].isNumber()) {
                const double delta = double(sx - gizmoLastScreenX_) * 0.5;
                if (std::isfinite(delta) && std::abs(delta) > 1e-6) {
                    if (!dragMoved_) doc.beginEdit();
                    Json patch = Json::object();
                    patch.set("rotationY", Json::number(
                        d.entities[size_t(sel)].extra["facing"].toDouble() + delta, 3));
                    dragMoved_ = writeDocumentComponent(
                        doc, DocumentEntityRef::mapEntity(size_t(sel)),
                        DocumentComponentKind::Transform, patch) || dragMoved_;
                }
                gizmoLastScreenX_ = sx;
            } else {
            const float dx = gizmoAxis_ == 0 ? x - gizmoGroundX_ : 0.0f;
            const float dz = gizmoAxis_ == 2 ? z - gizmoGroundZ_ : 0.0f;
            if (dx != 0.0f || dz != 0.0f) {
                if (!dragMoved_) doc.beginEdit();
                dragMoved_ = true;
                std::vector<int> idx;
                for (int i : ed.selectedEntities()) if (i >= 0 && size_t(i) < d.entities.size() && !ed.objectLocked(d.entities[size_t(i)].owner, d.entities[size_t(i)].type)) idx.push_back(i);
                std::vector<DocumentEntityRef> refs;
                refs.reserve(idx.size());
                for (int index : idx) refs.push_back(DocumentEntityRef::mapEntity(size_t(index)));
                translateDocumentEntities(doc, refs, {dx, 0.0, dz});
                gizmoGroundX_ = x;
                gizmoGroundZ_ = z;
            }
            }
        } else if (dragging_ && sel >= 0 && sel < int(d.entities.size())) {   // the primary follows the cursor, the rest of the set keeps its offsets
            auto& e = d.entities[size_t(sel)];
            int t = ed.rules().findType(e.type);
            const float dx = ed.snapCoord(x + dragOffX_, t) - e.x, dz = ed.snapCoord(z + dragOffZ_, t) - e.z;
            if (dx != 0 || dz != 0) {
                if (!dragMoved_ && !io.KeyAlt) doc.beginEdit();
                dragMoved_ = true;
                std::vector<int> idx;
                for (int i : ed.selectedEntities()) if (!ed.objectLocked(d.entities[size_t(i)].owner, d.entities[size_t(i)].type) || i == sel) idx.push_back(i);
                std::vector<DocumentEntityRef> refs;
                refs.reserve(idx.size());
                for (int index : idx) refs.push_back(DocumentEntityRef::mapEntity(size_t(index)));
                translateDocumentEntities(doc, refs, {dx, 0.0, dz});
            }
        }
        if (!painting_) return;
        int cx = int(std::floor(x)), cz = int(std::floor(z));
        const bool erase = io.KeyShift;
        const int r = ed.brush - 1;
        switch (ed.tool) {
            case Tool::Terrain: doc.paint(cx, cz, r, erase ? '.' : ed.paintCell); break;
            case Tool::Level: doc.setLevel(cx, cz, r, erase ? 0 : ed.paintLevel); break;
            case Tool::Height: doc.raise(cx, cz, ed.brush, erase ? -2 : 2); break;
            case Tool::Field:
                if (!ed.rules().resources.empty())
                    doc.paintField(cx, cz, r, ed.rules().resources[size_t(std::clamp(ed.fieldRes, 0, int(ed.rules().resources.size()) - 1))].id, erase ? 0 : ed.fieldAmount);
                break;
            case Tool::Region: doc.paintRegion(cx, cz, r, erase ? '.' : ed.regionLetter); break;
            default: break;
        }
    }

    void up(Editor& ed, float sx, float sy) {
        if (areaDrag_) {
            areaDrag_ = false;
            float x, z;
            if (ed.groundAt(vp_, ed.sceneCam, sx, sy, x, z)) {
                float x0 = std::min(areaX0_, std::floor(x)), z0 = std::min(areaZ0_, std::floor(z));
                float x1 = std::max(areaX0_, std::floor(x)) + 1, z1 = std::max(areaZ0_, std::floor(z)) + 1;
                ed.doc().beginEdit();
                Json& areas = ed.doc().data.extra.member("areas");
                std::string name;
                for (int i = 1;; ++i) { name = "area" + std::to_string(i); if (!areas.has(name)) break; }
                Json a = Json::array();
                for (float v : {x0, z0, x1 - x0, z1 - z0}) a.push(Json::number(double(v), 1));
                areas.set(name, a);
                ed.select({Selection::Area, -1, name});
                ed.status = "Зона «" + name + "» — переименуйте в Инспекторе";
            }
        }
        if (dragging_ && keepSetClick_ && !dragMoved_) ed.select(clicked_);   // a plain click inside a multi-selection picks that one
        keepSetClick_ = false;
        dragging_ = painting_ = false;
        gizmoAxis_ = -1;
    }

    // brush circle, placement ghost and help text over the viewport
    void hud(Editor& ed) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Project actual selections from the shared Selection model. A click in
        // Scene, Hierarchy or Inspector therefore receives the same feedback.
        if (!ed.planetMap() && studioDrawSelection_) {
            dl->PushClipRect(ImVec2(vp_.originX, vp_.originY),
                             ImVec2(vp_.originX + vp_.pointsW, vp_.originY + vp_.pointsH), true);
            for (const Selection& sel : ed.selected()) {
                float wx, wz;
                if (!ed.selectionPos(sel, wx, wz)) continue;
                float wy = ed.view().heightAt(ed.editGrid(), wx, wz) + 0.4f;
                if (sel.kind == Selection::Entity && sel.index >= 0 &&
                    size_t(sel.index) < ed.doc().data.entities.size() &&
                    authoredEntity(ed.doc().data.entities[size_t(sel.index)]))
                    wy = float(authoredLocalTransform(ed.doc().data.entities[size_t(sel.index)]).position.y.toDouble()) + 0.4f;
                float2 p;
                if (!ed.sceneCam.worldToScreen({wx, wy, wz}, vp_.pointsW, vp_.pointsH, p)) continue;
                const ImVec2 c(vp_.originX + p.x, vp_.originY + p.y);
                // Screen-space selection pivot and projected XYZ transform axes.
                // This is an editor overlay; a true occlusion-aware mesh silhouette
                // requires a separate selection-mask pass in the scene renderer.
                dl->AddCircle(c, 10.0f, IM_COL32(88, 174, 255, 225), 32, 1.8f);
                dl->AddCircleFilled(c, 3.0f, IM_COL32(225, 238, 255, 255));
                if (sel.kind == Selection::Entity && sel == ed.selection &&
                    sel.index >= 0 &&
                    size_t(sel.index) < ed.doc().data.entities.size()) {
                    const auto& entity = ed.doc().data.entities[size_t(sel.index)];
                    constexpr float extent = 1.5f;
                    if (ed.transformTool == TransformTool::Rotate &&
                        entity.extra["facing"].isNumber()) {
                        constexpr int segments = 64;
                        constexpr float tau = 6.283185307179586f;
                        float2 previous{};
                        for (int i = 0; i <= segments; ++i) {
                            const float angle = tau * float(i) / float(segments);
                            float2 projected;
                            if (ed.sceneCam.worldToScreen(
                                    {wx + std::cos(angle) * extent, wy,
                                     wz + std::sin(angle) * extent},
                                    vp_.pointsW, vp_.pointsH, projected)) {
                                if (i > 0) dl->AddLine(
                                    {vp_.originX + previous.x, vp_.originY + previous.y},
                                    {vp_.originX + projected.x, vp_.originY + projected.y},
                                    IM_COL32(255, 188, 80, 255), 2.5f);
                                previous = projected;
                            }
                        }
                    } else if (ed.transformTool == TransformTool::Move) {
                        const float3 ends[3] = {
                            {wx + extent, wy, wz}, {wx, wy + extent, wz}, {wx, wy, wz + extent}
                        };
                        const ImU32 colors[3] = {
                            IM_COL32(245, 93, 91, 255),
                            IM_COL32(93, 215, 115, 255),
                            IM_COL32(91, 155, 250, 255)
                        };
                        const char* axisLabels[3] = {"X", "Y", "Z"};
                        for (int axis = 0; axis < 3; ++axis) {
                            if (axis == 1 && !authoredEntity(entity)) continue;
                            float2 projected;
                            if (!ed.sceneCam.worldToScreen(
                                    ends[axis], vp_.pointsW, vp_.pointsH,
                                    projected)) continue;
                            const ImVec2 endpoint(vp_.originX + projected.x,
                                                  vp_.originY + projected.y);
                            dl->AddLine(c, endpoint,
                                        IM_COL32(14, 20, 29, 215), 5.0f);
                            dl->AddLine(c, endpoint, colors[axis], 2.5f);
                            dl->AddCircleFilled(endpoint, 4.0f, colors[axis]);
                            dl->AddText({endpoint.x + 6, endpoint.y - 7},
                                        colors[axis], axisLabels[axis]);
                        }
                    }
                }
            }
            dl->PopClipRect();
        }
        if (!ed.planetMap()) studioOverlays(ed, dl);
        float x, z;
        if (ed.planetMap()) { // body / face / cell under the cursor
            int body; float3 local;
            if (vp_.hovered && ed.planetAt(vp_, hoverX_, hoverY_, body, local)) {
                const game::PlanetSurface* s = ed.view().planets().surface(body);
                int f = 0; float a = 0, b = 0;
                if (s) s->faceFromDir(normalize(local), f, a, b);
                char buf[128];
                std::snprintf(buf, sizeof buf, "%s  грань %d  x %.1f  y %.1f  высота %.2f", ed.planetBodyId(body).c_str(), f, a, b, s ? s->heightAt(normalize(local)) : 0.0f);
                ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + vp_.pointsH - 26), buf);
            }
            ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + 10), "Планеты: колесо — масштаб, ПКМ — поворот, СКМ / WASD — движение, Tab — следующее тело", ImVec4(0.86f, 0.9f, 1, 0.9f));
            return;
        }
        if (!ed.playing() && vp_.hovered && ed.groundAt(vp_, ed.sceneCam, hoverX_, hoverY_, x, z)) {
            bool brushTool = ed.tool == Tool::Terrain || ed.tool == Tool::Level || ed.tool == Tool::Height || ed.tool == Tool::Field || ed.tool == Tool::Region;
            const RTSCamera& cam = ed.sceneCam;
            auto toScreen = [&](float wx, float wz, ImVec2& out) {
                float2 s;
                if (!cam.worldToScreen({wx, ed.view().heightAt(ed.editGrid(), wx, wz) + 0.1f, wz}, vp_.pointsW, vp_.pointsH, s)) return false;
                out = ImVec2(vp_.originX + s.x, vp_.originY + s.y);
                return true;
            };
            if (brushTool) {
                float r = float(ed.tool == Tool::Height ? ed.brush : ed.brush - 1) + 0.5f, cx = std::floor(x) + 0.5f, cz = std::floor(z) + 0.5f;
                ImVec2 prev;
                bool havePrev = false;
                for (int i = 0; i <= 40; ++i) {
                    float a = 6.2831853f * float(i) / 40.0f;
                    ImVec2 p;
                    if (toScreen(cx + std::cos(a) * r, cz + std::sin(a) * r, p)) {
                        if (havePrev) dl->AddLine(prev, p, IM_COL32(255, 255, 255, 200), 1.5f);
                        prev = p; havePrev = true;
                    } else havePrev = false;
                }
            }
            if (ed.tool == Tool::Place && !ed.rules().types.empty()) {
                const auto& t = ed.rules().types[size_t(ed.placeType)];
                float cx = ed.snapCoord(x, ed.placeType), cz = ed.snapCoord(z, ed.placeType);
                float hw = t.structure ? t.footprintW * 0.5f : float(t.radius.toDouble()), hh = t.structure ? t.footprintH * 0.5f : hw;
                ImVec2 a, b, c, d;
                if (toScreen(cx - hw, cz - hh, a) && toScreen(cx + hw, cz - hh, b) && toScreen(cx + hw, cz + hh, c) && toScreen(cx - hw, cz + hh, d))
                    dl->AddQuadFilled(a, b, c, d, IM_COL32(255, 255, 255, 60)), dl->AddQuad(a, b, c, d, IM_COL32(255, 255, 255, 200));
            }
            if (areaDrag_) {
                float x0 = std::min(areaX0_, std::floor(x)), z0 = std::min(areaZ0_, std::floor(z));
                float x1 = std::max(areaX0_, std::floor(x)) + 1, z1 = std::max(areaZ0_, std::floor(z)) + 1;
                ImVec2 a, b, c, d;
                if (toScreen(x0, z0, a) && toScreen(x1, z0, b) && toScreen(x1, z1, c) && toScreen(x0, z1, d)) dl->AddQuadFilled(a, b, c, d, IM_COL32(255, 220, 80, 60));
            }
            char buf[96];
            std::snprintf(buf, sizeof buf, "x %.1f  z %.1f  уровень %d", x, z, ed.doc().data.level(int(std::floor(x)), int(std::floor(z))));
            ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + vp_.pointsH - 26), buf);
        }
        if (ed.playing())
            ui::viewportLabel(dl, ImVec2(vp_.originX + 12, vp_.originY + 10), ICON_PLAY "  Идёт тест — правка отключена (F5 — стоп)", ImVec4(1, 0.9f, 0.47f, 1));
    }

    // Studio diagnostics share the Scene's projection and do not change the map or simulation.
    void studioOverlays(Editor& ed, ImDrawList* dl) {
        if (!studioDrawBounds_ && !studioDrawAxes_) return;
        const auto& cam = ed.sceneCam;
        auto project = [&](render::float3 xyz, ImVec2& result) {
            render::float2 xy;
            if (!cam.worldToScreen(xyz, vp_.pointsW, vp_.pointsH, xy)) return false;
            result = ImVec2(vp_.originX + xy.x, vp_.originY + xy.y);
            return true;
        };
        dl->PushClipRect(ImVec2(vp_.originX, vp_.originY),
                         ImVec2(vp_.originX + vp_.pointsW, vp_.originY + vp_.pointsH), true);
        if (studioDrawAxes_) {
            ImVec2 a,b;
            if (project({0,0,0},a)) {
                if (project({4,0,0},b)) dl->AddLine(a,b,IM_COL32(234,96,96,255),2);
                if (project({0,4,0},b)) dl->AddLine(a,b,IM_COL32(105,206,119,255),2);
                if (project({0,0,4},b)) dl->AddLine(a,b,IM_COL32(105,154,239,255),2);
            }
        }
        if (studioDrawBounds_) {
            const auto& entities = ed.doc().data.entities;
            for (size_t i=0; i<entities.size(); ++i) {
                const auto& e = entities[i];
                const int ti = ed.rules().findType(e.type);
                const bool structure = ti>=0 && ed.rules().types[size_t(ti)].structure;
                const float rx = structure ? std::max(.5f, float(ed.rules().types[size_t(ti)].footprintW)*.5f) : .5f;
                const float rz = structure ? std::max(.5f, float(ed.rules().types[size_t(ti)].footprintH)*.5f) : .5f;
                const float ground = ed.view().heightAt(ed.editGrid(), e.x, e.z);
                const float height = structure ? 2.5f : 1.0f;
                ImVec2 corners[8]; bool ok[8];
                for (int j=0;j<8;++j) ok[j]=project({e.x+((j&1)?rx:-rx), ground+((j&2)?height:0.f), e.z+((j&4)?rz:-rz)},corners[j]);
                const ImU32 color=ed.isSelected(Selection{Selection::Entity,int(i),{}}) ? IM_COL32(85,175,255,240) : IM_COL32(149,167,191,165);
                for (int j=0;j<8;++j) for (int bit : {1,2,4}) if (!(j&bit) && ok[j] && ok[j|bit])
                    dl->AddLine(corners[j],corners[j|bit],color,1.25f);
            }
        }
        dl->PopClipRect();
    }

    // Click selection: Ctrl / Cmd toggles, Shift adds, a plain click selects one (nothing: clears).
    static void pickSelect(Editor& ed, const Selection& s, const ImGuiIO& io) {
        if (io.KeyCtrl || io.KeySuper) { if (s.kind != Selection::None) ed.selectToggle(s); }
        else if (io.KeyShift) { if (s.kind != Selection::None) ed.selectAdd(s); }
        else ed.select(s);
    }

    Selection clicked_;
    bool keepSetClick_ = false, dragMoved_ = false;
    Viewport vp_;
    bool looking_ = false, panning_ = false, dragging_ = false, painting_ = false, areaDrag_ = false;
    int studioDebugMode_ = 0;
    int gizmoAxis_ = -1;
    float gizmoGroundX_ = 0.0f, gizmoGroundZ_ = 0.0f;
    float gizmoLastScreenX_ = 0.0f;
    float gizmoLastScreenY_ = 0.0f;
    bool sceneInstancing_ = true, sceneCulling_ = true, sceneIndirect_ = true;
    // 0: follow process environment (OFF by default), 1: opt-in only for
    // Scene viewport, 2: force off for Scene viewport. Never persists to Game.
    int sceneCrossModelMode_ = 0;
    int sceneCpuMdiAdaptiveMode_ = 0;
    int sceneMdiLayoutCacheMode_ = 0;
    int sceneMdiGeometrySortMode_ = 0;
    double sceneCpuMdiBudgetOverrideUs_ = 0.0;
    // Stable per Scene window, not persisted to gameplay or ECS IDs.
    static constexpr uint64_t kSceneCpuMdiAdaptiveScope_ = 0x5343454E45564945ull;
    static constexpr double kSceneCpuMdiBudgetMin_ = 0.0;
    static constexpr double kSceneCpuMdiBudgetMax_ = 1000.0;
    bool sceneLodEnabled_ = true;
    bool lodPopupVisible_ = false;
    float sceneLodFadeWidth_ = 0.15f;
    ModelRenderStats lastSceneLodStats_{};
    Renderer3D::LodDebugSample lastSelectedLod_{};
    bool sceneGeometryPool_ = render::globalGeometryPoolingEnabled();
    bool sceneRigidLodPooling_ = render::rigidLodGeometryPoolingEnabled();
    int sceneInstanceLimit_ = 0;
    bool sceneInstanceLimitDirty_ = true;
    bool sceneProfiling_ = false;
    bool benchmarkActive_ = false, benchmarkDone_ = false, benchmarkThreeModes_ = false;
    bool autoBatchEnabled_ = true;
    int autoBatchChoice_ = -1;
    int benchmarkBlockCount_ = 6;
    int benchmarkPhase_ = 0, benchmarkMode_ = 0, benchmarkPhaseSamples_ = 0, benchmarkWarmup_ = 0;
    RTSCamera benchmarkCamera_{};
    std::vector<double> benchmarkSamples_[3];
    std::vector<double> benchmarkGpuSamples_[3];
    uint8_t benchmarkGeneration_ = 0;
    uint64_t benchmarkGpuSequence_ = 0;
    gfx::GpuTimingRouter::Subscriber benchmarkTimingSubscriber_ = 0;
    gfx::Device* benchmarkTimingDevice_ = nullptr; // reset owner across device recreation
    uint64_t benchmarkDrawSum_[3] = {};
    uint64_t benchmarkIndirectSum_[3] = {};
    uint64_t benchmarkCommandSum_[3] = {};
    uint64_t benchmarkGpuAuthoredSum_[3] = {};
    uint64_t benchmarkGpuIcbSum_[3] = {};
    bool measureBatchingNextFrame_ = false, comparisonValid_ = false;
    int64_t baselineDraws_ = -1, optimizedDraws_ = -1;
    int64_t baselineModelDraws_ = -1, optimizedModelDraws_ = -1;
    bool studioDrawBounds_ = false;
    bool studioDrawAxes_ = false;
    bool studioDrawSelection_ = true;
    float dragOffX_ = 0, dragOffZ_ = 0, areaX0_ = 0, areaZ0_ = 0, hoverX_ = 0, hoverY_ = 0;
};

AIVERSE_EDITOR_WINDOW(SceneWindow);

} // namespace aiverse::editor
