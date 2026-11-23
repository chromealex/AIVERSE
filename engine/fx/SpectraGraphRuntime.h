// AIVERSE SpectraFX — capability-based graph execution.
// Conservative GPU eligibility gates the experimental Metal billboard subset.
// Unsupported graphs preserve the full fx::EffectDef on the established CPU
// ParticleSystem. No FPS/RTS dependencies or authoritative gameplay writes.
#pragma once

#include "VfxGraph.h"
#include "SpectraGpuParticles.h"
#include "SpectraBatchWorld.h"
#include "SpectraInstanceParameters.h"
#include "SpectraDataInterfaces.h"
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::spectra {

struct GraphRuntimeOptions {
    fx::FxQuality quality = fx::FxQuality::High;
    ComputePreference preference = ComputePreference::Automatic;
    uint32_t capacity = 1024; // GPU-only slots; not a CPU EffectDef particle budget
    bool gpuIndirect = false; // opt-in GPU-authored compaction/draw, Metal only
    // Opt-in Stage-22 bounded Metal VM. Unsupported programs use exact CPU effects.
    bool gpuProgram = false;
    // Strict opt-in: only pure, bounded particle-domain shaderGraph nodes.
    // Arbitrary shader/material graphs remain on the original CPU backend.
    bool gpuMaterial = false;
    // Nonblocking checkpoint requests every N submitted GPU frames (0=off).
    uint32_t checkpointEveryFrames = 0;
    AdaptiveCheckpointPolicy adaptiveCheckpoint{};
    // Share one limiter across GraphRuntime instances using the same Device.
    // Optional, for compatibility with single-emitter users.
    std::shared_ptr<CheckpointReadbackBudget> checkpointBudget;
};

struct GraphGpuEligibility {
    bool eligible = false;
    ComputeEmitterDesc gpu{};
    std::string reason; // why a CPU fallback is mandatory
};

// Stateless, strict capability check for the *compiled* graph. Validated
// EffectDef is the authority, never the graph node names/genre or a heuristic
// like "contains SpriteOutput". No RHI resources are touched.
GraphGpuEligibility inspectGraphGpuEligibility(
    const fx::EffectDef& effect, const GraphRuntimeOptions& options = {});

// One graph definition per instance. Can be used by ANY native project or
// presentation adapter. Instances do not own the graphics Device.
// initGraph compiles v1 graph IR and delegates to initEffect.
// On Auto/PreferGpu: compatible graphs attempt Metal; unsupported or failed
// compute pipelines get the exact original EffectDef on CPU.
// RequireGpu fails loudly if the graph/device cannot use the GPU backend.
// Call step before render pass; draw within the pass. No ECS gameplay state.
class GraphRuntime final {
public:
    GraphRuntime() = default;
    ~GraphRuntime() { shutdown(); }
    GraphRuntime(const GraphRuntime&) = delete;
    GraphRuntime& operator=(const GraphRuntime&) = delete;

    bool initGraph(gfx::Device* device, const Json& graph,
                   const Json& overrides = Json::object(),
                   const GraphRuntimeOptions& options = {});
    bool initEffect(gfx::Device* device, const fx::EffectDef& compiled,
                    const GraphRuntimeOptions& options = {});

    // An opt-in shared CPU world. Each GraphRuntime owns only its handles;
    // the host owns sharedWorld and calls sharedWorld.update()/draw() ONCE
    // per frame for all graph instances. This overload never creates a
    // separate ParticleSystem or an extra draw call. RequireGpu is rejected.
    // The BatchWorld MUST outlive every connected GraphRuntime.
    bool initEffect(BatchWorld& sharedWorld, const fx::EffectDef& compiled,
                    const GraphRuntimeOptions& options = {});
    bool initGraph(BatchWorld& sharedWorld, const Json& graph,
                   const Json& overrides = Json::object(),
                   const GraphRuntimeOptions& options = {});
    void shutdown();

    // Read-only VM variables param0..param7, scoped to THIS GraphRuntime.
    // Updating a source never recompiles its graph; affectExisting also
    // updates all live particles this source still owns.
    // Restricted Metal GPU billboard path does not execute the VM.
    bool setProgramParameter(uint32_t slot,float value,bool affectExisting=false);
    // v2 graph instanceParameters: strict name, slot, bounds and defaults.
    bool setNamedProgramParameter(const std::string& name,float value,
                                  bool affectExisting=false);
    const InstanceParameterTable& instanceParameters() const {return namedParams_;}
    const DataInterfaceBindings& dataInterfaceBindings() const {return dataBindings_;}
    // Non-owning, strictly presentation-thread registry. Call after initGraph,
    // before burstAt(). Null restores optional fallbacks and makes required
    // bindings reject a burst. The host owns the registry and its snapshots.
    void setDataInterfaceRegistry(const DataInterfaceRegistry* registry) {
        dataRegistry_=registry;
    }
    const DataInterfaceSampleStats& dataInterfaceStats() const {return dataStats_;}
    bool burstAt(fx::float3 worldPosition,
                 fx::float3 direction = {0,1,0},
                 fx::float4 tint = {1,1,1,1});
    void setView(const fx::SpectraFxView& view);
    template<class Camera>
    void setView(const Camera& camera,const render::mat4& projection,int width,int height) {
        return setView(fx::SpectraFxView::fromCamera(camera,projection,width,height));
    }
    bool step(float dt); // between beginFrame/endFrame, before scene render pass
    void draw(const fx::FxFog& fog = {}); // during still-open render pass
    void clear();

    ComputeBackend backend() const { return backend_; }
    bool gpuActive() const { return initialized_ && backend_ == ComputeBackend::Metal && gpu_.gpuActive(); }
    const std::string& reason() const { return reason_; }
    const std::vector<std::string>& errors() const { return errors_; }
    const fx::ParticleSystem& cpuFallback() const {
        return sharedWorld_ ? sharedWorld_->particles()
             : useComputeWrapper_ ? gpu_.cpuFallback() : cpu_;
    }
    bool usesSharedBatchWorld() const { return sharedWorld_ != nullptr; }
    const ComputeStats& gpuStats() const { return gpu_.stats(); }
    // Diagnostic readback, only between completed Metal frames. Returns false
    // when GPU is unavailable; never treats missing results as zero errors.
    bool readGpuVmErrors(uint32_t& count) { return gpu_.readGpuVmErrors(count); }
    // Accepted async diagnostic requests callback on a later device frame.
    // Callbacks are cancelled if the GPU runtime shuts down beforehand.
    bool requestGpuVmErrors(std::function<void(bool,uint32_t)> callback) {
        return useComputeWrapper_ &&
            gpu_.requestGpuVmErrors(std::move(callback));
    }
    // Blocking checkpoint capture between COMPLETED frames; this is
    // diagnostic/export data, not yet a CPU ParticleSystem import.
    bool captureGpuCheckpoint(GpuCheckpoint& out) {
        return useComputeWrapper_ && gpu_.captureGpuCheckpoint(out);
    }
    GpuCheckpointStatus checkpointStatus() const {
        return useComputeWrapper_ ? gpu_.checkpointStatus() : GpuCheckpointStatus{};
    }
    bool requestGpuCheckpoint(std::function<void(bool)> callback = {}) {
        return useComputeWrapper_ &&
            gpu_.requestGpuCheckpoint(std::move(callback));
    }
private:
    bool initCpu(gfx::Device*, const fx::EffectDef&, fx::FxQuality);
    bool applyDataInterfaces(fx::float3 position);
    bool initialized_ = false;
    bool useComputeWrapper_ = false;
    BatchWorld* sharedWorld_ = nullptr;
    // Bounded history: on overflow an older handle is killed before reuse.
    // Use one GraphRuntime per logical effect source, not per burst.
    std::deque<fx::FxHandle> sharedHandles_;
    std::deque<fx::FxHandle> cpuHandles_;
    std::array<float,8> paramValues_{};
    std::array<bool,8> paramSet_{};
    InstanceParameterTable namedParams_;
    DataInterfaceBindings dataBindings_;
    const DataInterfaceRegistry* dataRegistry_=nullptr;
    DataInterfaceSampleStats dataStats_{};
    ComputeBackend backend_ = ComputeBackend::Cpu;
    fx::ParticleSystem cpu_;
    ComputeParticles gpu_;
    int cpuEffect_ = -1;
    std::string reason_;
    std::vector<std::string> errors_;
};

} // namespace aiverse::spectra
