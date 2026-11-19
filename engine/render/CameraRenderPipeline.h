// AIVERSE LENS: execute an ECS CameraRenderPlan into screen and managed RTs.
// The host owns Device::beginFrame/endFrame, the renderer, and scene data;
// this layer owns no gameplay state and never modifies the ECS World.
// A renderer pass must not sample its own output; all camera-produced RT reads
// must be declared in CameraDependenciesComponent for DAG validation.
#pragma once
#include "CameraRenderScheduler.h"
#include "RenderTargetPool.h"
#include "Renderer3D.h"
#include "CameraRig.h"
#include "CameraPostFxPool.h"
#include "CameraCompositor.h"
#include "CameraCaptureViews.h"
#include "CameraCaptureGraph.h"
#include "CameraPanorama.h"
#include "CameraHdrResolve.h"
#include "CameraFrameRecorder.h"
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class CameraExecutionIssue : uint8_t {
    MissingInput, TargetUnavailable, InvalidFramebuffer, SharedPostProcess, DeviceLost, CompositeFailed
};
struct CameraExecutionDiagnostic {
    Entity entity{};
    CameraExecutionIssue reason=CameraExecutionIssue::MissingInput;
    uint64_t targetAssetKey=0;
};
struct CameraExecutionResult {
    CameraRenderPlan plan;
    std::vector<CameraExecutionDiagnostic> diagnostics;
    size_t submitted=0, skipped=0;
};

class CameraRenderPipeline {
public:
    CameraRenderPipeline(gfx::Device& device, Renderer3D& renderer,
                         CameraRenderScheduler& scheduler)
        : device_(device), renderer_(renderer), scheduler_(scheduler) {
        targets_.init(device);
        screenTargets_.init(device);
    }
    CameraRenderTargetPool& targets() { return targets_; }
    const CameraRenderTargetPool& targets() const { return targets_; }
    // Requires a host-confirmed GPU idle point (or final device teardown).
    void shutdownAfterGpuIdle() {
        hdrResolve_.shutdownAfterGpuIdle();panorama_.shutdownAfterGpuIdle();compositor_.shutdownAfterGpuIdle();screenTargets_.shutdownAfterGpuIdle();
        targets_.shutdownAfterGpuIdle(); scheduler_.reset();
    }

    template<class Submit>
    CameraExecutionResult execute(const World& world,uint64_t frame,double seconds,
                                   Submit&& submit,Vec3 renderOrigin={}) {
        return executeWithPost(world,frame,seconds,std::forward<Submit>(submit),
            [](const EcsCameraView&)->PostProcess* {return nullptr;},renderOrigin);
    }

    // A post provider must return a *separate persistent PostProcess* per
    // camera. Shared PostFX instances are rejected within the same frame to
    // prevent TAA/exposure history contamination. The host owns those objects.
    template<class Submit,class PostProvider>
    CameraExecutionResult executeWithPost(const World& world,uint64_t frame,
             double seconds,Submit&& submit,PostProvider&& postFor,
             Vec3 renderOrigin={}) {
        return executeWithPostAndModifier(world,frame,seconds,
            std::forward<Submit>(submit),std::forward<PostProvider>(postFor),
            [](EcsCameraView&,int,int){},renderOrigin);
    }

    // Modify a transient view after scheduling and target size resolution,
    // before Renderer3D receives it. Suitable for ECS rigs and jitter.
    template<class Submit,class PostProvider,class Modifier>
    CameraExecutionResult executeWithPostAndModifier(const World& world,uint64_t frame,
             double seconds,Submit&& submit,PostProvider&& postFor,
             Modifier&& modify,Vec3 renderOrigin={}) {
        CameraExecutionResult result;
        targets_.collectCompleted();
        result.plan=scheduler_.buildPlan(world,frame,seconds,renderOrigin);
        // A full-screen view plus explicit viewports/overlays uses isolated
        // per-Entity screen RTs and a single final screen composite pass.
        bool compositing=false;
        std::vector<EcsCameraView> screenViews;
        visitEcsCameras(world,[&](const EcsCameraView& v){
            if(v.output.kind==CameraOutputKind::Screen) {
                screenViews.push_back(v);
                compositing=compositing || v.viewport.enabled || v.stack.overlay;
            }
        },renderOrigin);
        compositing=compositing && !screenViews.empty();
        std::unordered_set<PostProcess*> usedPost;
        std::unordered_set<uint64_t> failedTargets;
        for(const auto& planned: result.plan.jobs) {
            EcsCameraView view=planned; // never mutate the ECS snapshot/plan
            bool inputsReady=true;
            uint64_t missing=0;
            const auto& inputs=view.dependencies;
            for(size_t j=0;j<inputs.count;++j) {
                const uint64_t key=inputs.reads[j];
                if(failedTargets.count(key) || !targets_.color(key).valid()) {
                    inputsReady=false; missing=key; break;
                }
            }
            if (!inputsReady) {
                ++result.skipped;
                result.diagnostics.push_back({view.entity,CameraExecutionIssue::MissingInput,missing});
                if(view.output.kind==CameraOutputKind::Texture)
                    failedTargets.insert(view.output.targetAssetKey);
                continue;
            }
            int width=device_.framebufferWidth(),height=device_.framebufferHeight();
            gfx::PassDesc pass;
            CameraRenderTargetPool::Target const* target=nullptr;
            if(view.output.kind==CameraOutputKind::Texture) {
                if(view.output.width) width=int(view.output.width);
                if(view.output.height) height=int(view.output.height);
                target=targets_.acquire(view.output.targetAssetKey,width,height,
                                         view.output.hdr,view.output.depth);
                if (!target) {
                    ++result.skipped;
                    failedTargets.insert(view.output.targetAssetKey);
                    result.diagnostics.push_back({view.entity,CameraExecutionIssue::TargetUnavailable,
                                                   view.output.targetAssetKey});
                    continue;
                }
                pass.colorTarget=target->color;
                pass.depthTarget=target->depth;
            }
            const bool compositedScreen=compositing && view.output.kind==CameraOutputKind::Screen;
            if(compositedScreen) {
                const auto rect=cameraViewportRect(view.viewport,device_.framebufferWidth(),device_.framebufferHeight());
                if(rect.w<=0 || rect.h<=0) {
                    ++result.skipped;
                    result.diagnostics.push_back({view.entity,CameraExecutionIssue::InvalidFramebuffer});
                    continue;
                }
                width=view.output.width?int(view.output.width):rect.w;
                height=view.output.height?int(view.output.height):rect.h;
                const auto* screenTarget=screenTargets_.acquire(view.entity.packed(),width,height,false,true);
                if(!screenTarget) {
                    ++result.skipped;
                    result.diagnostics.push_back({view.entity,CameraExecutionIssue::TargetUnavailable});
                    continue;
                }
                pass.colorTarget=screenTarget->color;
                pass.depthTarget=screenTarget->depth;
            }
            if(width<=0 || height<=0) {
                ++result.skipped;
                result.diagnostics.push_back({view.entity,CameraExecutionIssue::InvalidFramebuffer,
                                               view.output.targetAssetKey});
                if(target)failedTargets.insert(view.output.targetAssetKey);
                continue;
            }
            modify(view,width,height);
            PostProcess* post=postFor(view);
            if(post && post->active() && !usedPost.insert(post).second) {
                // Explicit opt-out is safer than silently sharing temporal
                // accumulation between distinct ECS camera entities.
                post=nullptr;
                result.diagnostics.push_back({view.entity,CameraExecutionIssue::SharedPostProcess});
            }
            // Isolate per-camera adaptive geometry feedback even when several
            // captures share one Renderer3D and the same geometry pages.
            auto policy=renderer_.cpuMdiPolicyOverrides();
            policy.adaptiveScope=view.entity.packed();
            Renderer3D::ScopedCpuMdiPolicyOverride scope(renderer_,policy);
            renderer_.beginScene(view.camera,width,height);
            submit(view,renderer_); // renderer must be initialized by the host
            renderer_.render(pass,post);
            renderer_.resolvePostFx(); // no-op without an active post chain
            device_.endPass();
            if (!device_.gpuHealthy()) {
                ++result.skipped;
                result.diagnostics.push_back({view.entity,CameraExecutionIssue::DeviceLost,
                                               view.output.targetAssetKey});
                if(target) {
                    targets_.markInvalid(view.output.targetAssetKey);
                    failedTargets.insert(view.output.targetAssetKey);
                }
                if(compositedScreen)screenTargets_.markInvalid(view.entity.packed());
                continue; // no acknowledgement on reported GPU failure
            }
            if(target)targets_.markReady(view.output.targetAssetKey);
            if(compositedScreen)screenTargets_.markReady(view.entity.packed());
            scheduler_.acknowledge(view.entity,view.output,frame,seconds,true);
            ++result.submitted;
        }
        if(compositing) {
            std::vector<CameraCompositeLayer> layers;
            const int fbW=device_.framebufferWidth(),fbH=device_.framebufferHeight();
            for(const auto& view:screenViews) {
                auto color=screenTargets_.color(view.entity.packed());
                auto rect=cameraViewportRect(view.viewport,fbW,fbH);
                if(!color.valid() || rect.w<=0 || rect.h<=0)continue;
                layers.push_back({color,rect,int(view.stack.order),
                    float(view.stack.opacity.toDouble()),view.entity.packed()});
            }
            if(!compositor_.init(device_) || !compositor_.composite(layers,fbW,fbH))
                result.diagnostics.push_back({{},CameraExecutionIssue::CompositeFailed});
        }
        return result;
    }
    // Complete presentation workflow: ECS rig pose/lens/shake, independently
    // owned per-camera temporal histories, capture scheduling and final RT.
    // The World is read-only. GPU post pool must outlive all captures and be
    // shutdown only at a host-confirmed GPU idle point.
    template<class Submit>
    CameraExecutionResult executeManaged(const World& world,uint64_t frame,
             double seconds,float dt,Submit&& submit,
             CameraRigEvaluator& rigs,CameraPostFxPool& post,
             Vec3 renderOrigin={}) {
        post.bindWorld(world);
        return executeWithPostAndModifier(world,frame,seconds,
            std::forward<Submit>(submit),
            [&](const EcsCameraView& camera)->PostProcess* {
                return post.get(camera.entity);
            },
            [&](EcsCameraView& camera,int width,int height) {
                rigs.apply(world,camera,seconds,dt,renderOrigin);
                camera.camera=post.prepare(camera,width,height);
            },renderOrigin);
    }
    // Explicit non-ECS captures for cubemap faces, stereo eyes, portals,
    // mirrors and one-shot scene previews. The host supplies distinct keys
    // and declares any sampling dependencies; no screen clear is involved.
    // Cubemap source keys must already contain six successfully rendered
    // faces. Result is a standard sampleable equirectangular 2D RenderTexture.
    bool renderPanorama(const std::array<uint64_t,6>& faces,uint64_t outputKey,
                        int width,int height,bool hdr=false) {
        if(!outputKey||width<=0||height<=0)return false;
        std::array<gfx::TextureHandle,6> source{};
        for(size_t i=0;i<source.size();++i) {
            if(faces[i]==outputKey)return false;
            source[i]=targets_.color(faces[i]);
            if(!source[i].valid())return false;
        }
        auto* output=targets_.acquire(outputKey,width,height,hdr,false);
        if(!output || !panorama_.init(device_))return false;
        if(!panorama_.render(source,output->color,width,height)) {
            targets_.markInvalid(outputKey);return false;
        }
        targets_.markReady(outputKey);
        return true;
    }

    // Resolve an already rendered HDR RGBA16F target into a separate SDR RGBA8
    // target, ready for captureTexture() or CameraFrameRecorder::tickTexture().
    // Caller must invoke during a frame, after HDR rendering and outside passes.
    bool resolveHdrForExport(uint64_t hdrKey,uint64_t sdrKey,
                             CameraHdrResolveSettings settings={}) {
        if(!hdrKey||!sdrKey||hdrKey==sdrKey||
           !validCameraHdrResolveSettings(settings))return false;
        const auto* source=targets_.find(hdrKey);
        if(!source||!source->ready||!source->hdr||!source->color.valid())return false;
        const int width=source->width,height=source->height;
        auto* output=targets_.acquire(sdrKey,width,height,false,false);
        if(!output)return false;
        if(!hdrResolve_.init(device_)||
           !hdrResolve_.render(source->color,output->color,width,height,settings)) {
            targets_.markInvalid(sdrKey);return false;
        }
        targets_.markReady(sdrKey);
        return true;
    }
    // Combined scheduling convenience: returns false when resolve/capture
    // could not be scheduled. The host keeps the device frame alive until
    // submissions finish and the recorder callback eventually delivers PNG.
    bool recordHdrFrameForPng(CameraFrameRecorder& recorder,uint64_t hdrKey,
                           uint64_t sdrKey,double seconds,
                           CameraHdrResolveSettings settings={}) {
        if(!recorder.recording()||recorder.pending())return false;
        return resolveHdrForExport(hdrKey,sdrKey,settings) &&
               recorder.tickTexture(device_,targets_.color(sdrKey),seconds);
    }

    template<class Submit>
    CameraCaptureGraphPlan renderCaptureGraph(
            const std::vector<CameraCaptureNode>& nodes,
            Submit&& submit,unsigned maxDepth=4) {
        auto plan=makeCameraCaptureGraphPlan(nodes,maxDepth);
        renderCaptures(plan.ordered,std::forward<Submit>(submit));
        return plan;
    }

    template<class Submit>
    size_t renderCaptures(const std::vector<CameraCaptureView>& captures,
                          Submit&& submit) {
        size_t encoded=0;
        for(const auto& request:captures) {
            if(!request.targetAssetKey || request.width<=0 || request.height<=0)
                continue;
            const auto* target=targets_.acquire(request.targetAssetKey,
                request.width,request.height,request.hdr,true);
            if(!target)continue;
            gfx::PassDesc pass;
            pass.colorTarget=target->color;
            pass.depthTarget=target->depth;
            renderer_.beginScene(request.camera,request.width,request.height);
            submit(request,renderer_);
            renderer_.render(pass);
            device_.endPass();
            if(!device_.gpuHealthy()) {
                targets_.markInvalid(request.targetAssetKey);
                continue;
            }
            targets_.markReady(request.targetAssetKey);
            ++encoded;
        }
        return encoded;
    }
private:
    gfx::Device& device_;
    Renderer3D& renderer_;
    CameraRenderScheduler& scheduler_;
    CameraRenderTargetPool targets_;
    CameraRenderTargetPool screenTargets_;
    CameraCompositor compositor_;
    CameraPanorama panorama_;
    CameraHdrResolve hdrResolve_;
};

} // namespace aiverse::render
