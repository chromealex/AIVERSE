// SpectraFX opt-in Metal GPU billboard emitter with CPU fallback.
// Separate renderer-side resource: never store it in authoritative ECS.
// V1 subset: one additive soft-sprite emitter with configurable burst,
// lifetime, gravity, drag and size. CPU fallback reuses fx::ParticleSystem.
// GPU simulation/vertices never round-trip to CPU during normal frames.
#pragma once

#include "ParticleSystem.h"
#include "SpectraGpuCheckpoint.h"
#include "SpectraCheckpointExport.h"
#include "SpectraCheckpointReadbackBudget.h"
#include "SpectraAdaptiveCheckpoint.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::spectra {

enum class ComputePreference : uint8_t {
    Automatic, CpuOnly, PreferGpu, RequireGpu
};
enum class ComputeBackend : uint8_t { Cpu, Metal };

// Pure RHI selection; never assume that every Metal or Vulkan device supports
// a particular shader. createCompute and the other resource allocations are
// checked at runtime; Automatic/PreferGpu fall back to Cpu on any failure.
ComputeBackend chooseComputeBackend(gfx::Backend backend, bool computeAvailable,
                                    ComputePreference preference);

struct ComputeEmitterDesc {
    uint32_t capacity = 1024;          // GPU hard limit: 4096 slots (16-bit quad indices)
    uint32_t burst = 8;                // particles per call to burstAt
    float lifetime = 0.8f;            // seconds
    float speed = 3.0f;
    float size = 0.35f;               // world metres
    float gravityY = -9.81f;
    float drag = 0.0f;
    fx::FxQuality quality = fx::FxQuality::High;
    ComputePreference backend = ComputePreference::Automatic;
    // Experimental Metal-only option; no effect on existing CPU/host-gather
    // paths. Requires RHI supportsGpuAuthoredIndirectDraw().
    bool gpuIndirect = false;
    // Experimental Metal GPU scalar VM (requires strict Stage-22 eligibility).
    // Off by default; unsupported stages/operations execute the full CPU VM.
    bool gpuProgram = false;
    Json particleProgram; // compiled once to fixed ABI, never executed as JSON
    // Optional particle shaderGraph subset sharing the CPU material language.
    // Off by default, validated on the whole EffectDef before Metal routing.
    bool gpuMaterial = false;
    bool gpuBirthRandom = false; // explicit Metal-specific random semantics
    uint32_t gpuBirthSeed = 0;
    Json shaderGraph;
    Json graphInputs; // four vertex graphData source names
    // Opt-in async checkpoint cadence (0 = disabled). One full capture is
    // at most 512 KiB / eight 64 KiB reads at capacity 4096.
    uint32_t checkpointEveryFrames = 0;
    AdaptiveCheckpointPolicy adaptiveCheckpoint{};
    // Optional shared readback budget; all GPU emitters on one Device should
    // share it. If absent, legacy independent checkpoint behavior is retained.
    std::shared_ptr<CheckpointReadbackBudget> checkpointBudget;
};

bool validComputeEmitter(const ComputeEmitterDesc&, std::string* reason = nullptr);

// Read-only checkpoint state. Fresh means the most recent completed capture
// matches the latest submitted GPU frame; it does not guarantee numerical
// CPU/Metal parity or restoration of emitter identity.
struct GpuCheckpointStatus {
    bool gpuActive = false;
    bool pending = false;
    bool complete = false;
    bool fresh = false;
    bool restoreEligible = false;
    bool unsentBirths = false;
    bool stagingRecoveryPending = false; // partial checked GPU inputs; retry next step
    uint64_t currentRevision = 0;
    uint64_t capturedRevision = 0;
    uint64_t frameSerial = 0;
    uint64_t framesBehind = 0; // valid only when complete
    uint32_t physicalSlots = 0;
    uint32_t killsAwaitingGpuFence = 0;
    uint32_t checkpointIntervalFrames = 0;
    uint64_t nextCheckpointRevision = 0;
};

struct ComputeStats {
    uint32_t alive = 0, dropped = 0;
    uint32_t gpuDispatches = 0, drawCalls = 0;
    uint32_t submittedSlots = 0; // dense GPU quads published from live-slot map
    uint32_t gpuFallbacks = 0, lostOnFallback = 0;
    uint32_t recoveredOnFallback=0;
    uint32_t checkpointRequests=0, checkpointCompleted=0, checkpointFailed=0;
    uint32_t checkpointBudgetDenied=0;
    uint32_t checkpointAdaptiveBackoffs=0, checkpointAdaptiveRecoveries=0;
    uint32_t selectivelyKilled=0;
    uint32_t killedSlotsReclaimed=0;
    uint32_t gpuDeviceUnhealthy=0;
    uint32_t gpuKillUploadFailures=0;
    uint32_t gpuStagingUploadFailures=0;
    uint32_t gpuComputeBeginFailures=0;
    uint32_t gpuPassOrderRejections=0;
    uint32_t gpuResetFailures=0;
    uint32_t gpuKillScatterDispatches=0;
    size_t gpuKillIndexBytes=0;
    uint32_t gpuKillFencesPending=0; // host estimate, not GPU observed
    size_t checkpointBytesReturned=0; // valid callback payload bytes, not DMA profiling
    uint32_t gpuCompactDispatches = 0, gpuIndirectDraws = 0;
    uint32_t gpuVmErrorsLastRead = 0;
    bool gpuVmErrorsKnown = false; // unknown until a successful diagnostic read
    // In indirect mode submittedSlots/alive are CPU lifespan estimates;
    // exact GPU alive count stays on GPU and is used as the draw argument.
    size_t gpuBytes = 0, uploadedBytes = 0;
};

// Caller owns the Device and must release this class before destroying it.
// For Metal, step() must run between beginFrame/endFrame and OUTSIDE passes;
// draw() runs inside the scene's still-open depth-tested pass.
// Legacy ParticleSystem and existing RTS/FPS VFX are untouched.
class ComputeParticles final {
public:
    ComputeParticles() = default;
    ~ComputeParticles() { shutdown(); }
    ComputeParticles(const ComputeParticles&) = delete;
    ComputeParticles& operator=(const ComputeParticles&) = delete;

    bool init(gfx::Device* device, const ComputeEmitterDesc& description = {});
    void shutdown();
    void clear(); // compatibility API; inspect reason() on GPU failure
    // Checked whole-emitter reset. Metal must have an OPEN graphics frame,
    // outside encoders. Never alters CPU slot bookkeeping on upload failure.
    bool tryClear();
    // Reproducible GPU particle-slot allocation, not cross-backend RNG parity.
    bool resetGpuAllocationSeed(uint32_t seed);
    bool resetGpuShaderSeed(uint32_t seed);
    bool burstAt(fx::float3 position, fx::float3 direction = {0,1,0},
                 fx::float4 tint = {1,1,1,1},uint64_t sourceToken=0);
    // Renderer-only source token. 0 means unowned one-shot particles.
    // Cancel pending births and queue an ordered GPU kill for submitted slots.
    // Slots cannot be reused until the next successful GPU step submission.
    bool killSourceParticles(uint64_t sourceToken);
    void setView(const fx::SpectraFxView& view);
    template<class Camera>
    void setView(const Camera& camera,const render::mat4& projection,int width,int height) {
        return setView(fx::SpectraFxView::fromCamera(camera,projection,width,height));
    }
    bool step(float dt);                    // once per presentation frame
    void draw(const fx::FxFog& fog = {});    // inside render pass
    ComputeBackend backend() const { return backend_; }
    bool ready() const { return initialized_; }
    bool gpuActive() const { return initialized_ && backend_ == ComputeBackend::Metal; }
    bool gpuIndirectActive() const { return gpuActive() && gpuIndirectActive_; }
    bool gpuProgramActive() const {
        return gpuActive() && desc_.gpuProgram && !desc_.particleProgram.isNull();
    }
    bool setProgramParameter(uint32_t slot,float value,bool affectExisting=false);
    // Between completed frames only. Returns false when unavailable; failure
    // must never be interpreted as zero VM errors.
    bool readGpuVmErrors(uint32_t& count);
    // Asynchronous 4-byte diagnostics. Accepted requests invoke callback
    // after a later frame; shutdown invalidates callbacks safely.
    bool requestGpuVmErrors(std::function<void(bool,uint32_t)> callback);
    // Blocking diagnostics only, between COMPLETED frames (Metal's readBuffer
    // rejects open command buffers). This is not CPU ParticleSystem import.
    bool captureGpuCheckpoint(GpuCheckpoint& out);
    // Enqueues bounded async chunks, returns false if it cannot start.
    // Callback reports whether all three buffers formed a complete snapshot.
    bool requestGpuCheckpoint(std::function<void(bool)> callback = {});
    const std::string& reason() const { return reason_; }
    const ComputeStats& stats() const { return stats_; }
    // No GPU readback; status is a snapshot of local bookkeeping only.
    GpuCheckpointStatus checkpointStatus() const;
    // Copies only the latest fully completed checkpoint at the CURRENT GPU
    // revision. No GPU access, no blocking readback, and <=528 KiB + header.
    // Never call this an authoritative ECS save or a replayable history.
    bool exportFreshCheckpoint(std::vector<uint8_t>& bytes,
        size_t maxBytes=kGpuCheckpointExportMaxBytes,
        std::string* reason=nullptr) const;
    const fx::ParticleSystem& cpuFallback() const { return cpu_; }
private:
    struct ParticleState {
        render::float4 positionAge, velocityLife, tint, size;
    };
    struct ProgramParams {
        render::float4 p0{}, p1{};
    };
    // Extra GPU VM storage, not part of the 64-byte ParticleState ABI.
    struct ProgramAttributes {
        render::float4 a0{}, a1{};
    };
    struct Vertex {
        float x,y,z,u,v;
        uint32_t rgba;
        float graph[4];
    };
    struct Uniforms {
        render::float4 gravityDt, rightSize, up;
        std::array<uint32_t,4> meta;
    };
    struct DrawUniforms {
        float viewProj[16];
        float fog[4], camera[4], params[4];
    };
    static_assert(sizeof(ParticleState)==64);
    static_assert(sizeof(ProgramParams)==32);
    static_assert(sizeof(ProgramAttributes)==32);
    static_assert(sizeof(Vertex)==40);
    static_assert(sizeof(Uniforms)==64);
    static_assert(sizeof(DrawUniforms)==112);

    bool initGpu();
    bool initCpu();
    void releaseGpu();
    void switchToCpu(const std::string& why);
    gfx::Device* device_ = nullptr;
    ComputeEmitterDesc desc_;
    // Separate shader birth stream; zero disables authored seeded randomness.
    uint32_t shaderBirthSeed_=0, shaderBirthOrdinal_=0;
    bool shaderBirthSeedEnabled_=false;
    ComputeBackend backend_ = ComputeBackend::Cpu;
    fx::ParticleSystem cpu_;
    int cpuEffect_ = -1;
    gfx::ComputeHandle compute_;
    gfx::ShaderHandle vertexShader_;
    gfx::PipelineHandle pipeline_;
    gfx::BufferHandle states_, seeds_, slots_, vertices_, indices_, liveSlots_;
    gfx::BufferHandle killSlots_; // 4 bytes per dead physical slot, scatter mode 6
    gfx::BufferHandle aliveCounter_, gpuDrawArgs_;
    gfx::BufferHandle seedProgramParams_, stateProgramParams_, gpuVmPayload_;
    gfx::BufferHandle graphSources_; // 4 uint32 lane selectors, compute slot 13
    gfx::BufferHandle vmErrors_;
    gfx::BufferHandle stateAttributes_; // compute slot 11, 8 scalar VM registers/particle
    gfx::TextureHandle texture_;
    std::vector<float> ages_;
    uint32_t drawCount_ = 0; // dense vertices emitted this frame (<= alive)
    std::vector<uint32_t> freeSlots_, pendingSlots_, aliveSlots_;
    std::vector<uint32_t> pendingKillSlots_;
    struct KilledSlotAwaitingFrame {
        uint32_t slot=0;
        uint64_t submittedFrame=0;
    };
    std::vector<KilledSlotAwaitingFrame> killedSlotsAwaitingFrame_;
    std::vector<uint64_t> slotSourceTokens_;
    std::vector<ParticleState> pendingStates_;
    std::vector<ProgramParams> pendingProgramParams_, programStateCache_;
    std::array<float,8> programParams_{};
    std::vector<fx::FxHandle> cpuHandles_;
    size_t cpuHandlePruneAt_ = 64; // amortized pruning of finished handles
    bool programParamsDirty_ = false;
    render::mat4 viewProj_;
    fx::float3 eye_{}, right_{1,0,0}, up_{0,1,0};
    bool initialized_ = false, viewReady_ = false, verticesReady_ = false;
    bool gpuIndirectActive_ = false;
    bool gpuResetDirty_ = false; // failed partial GPU clear; block new births/steps
    bool gpuStagingDirty_ = false; // checked input upload failed; next step retries all input buffers
    ComputeStats stats_;
    std::string reason_;
    std::shared_ptr<uint64_t> readbackLifetime_ = std::make_shared<uint64_t>(0);
    bool asyncErrorPending_ = false;
    uint64_t vmFrameRevision_ = 0;
    uint64_t checkpointRevision_ = 0;
    GpuCheckpoint lastCheckpoint_;
    bool asyncCheckpointPending_ = false;
    AdaptiveCheckpointCadence checkpointCadence_{};
};

} // namespace aiverse::spectra
