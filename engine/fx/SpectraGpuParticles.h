// SpectraFX opt-in Metal GPU billboard emitter with CPU fallback.
// Separate renderer-side resource: never store it in authoritative ECS.
// V1 subset: one additive soft-sprite emitter with configurable burst,
// lifetime, gravity, drag and size. CPU fallback reuses fx::ParticleSystem.
// GPU simulation/vertices never round-trip to CPU during normal frames.
#pragma once

#include "ParticleSystem.h"
#include <array>
#include <cstddef>
#include <cstdint>
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
};

bool validComputeEmitter(const ComputeEmitterDesc&, std::string* reason = nullptr);

struct ComputeStats {
    uint32_t alive = 0, dropped = 0;
    uint32_t gpuDispatches = 0, drawCalls = 0;
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
    void clear();                            // inside GPU frame, before render pass
    bool burstAt(fx::float3 position, fx::float3 direction = {0,1,0},
                 fx::float4 tint = {1,1,1,1});
    void setView(const render::RTSCamera&, const render::mat4&, int width, int height);
    bool step(float dt);                    // once per presentation frame
    void draw(const fx::FxFog& fog = {});    // inside render pass
    ComputeBackend backend() const { return backend_; }
    bool gpuActive() const { return initialized_ && backend_ == ComputeBackend::Metal; }
    const std::string& reason() const { return reason_; }
    const ComputeStats& stats() const { return stats_; }
    const fx::ParticleSystem& cpuFallback() const { return cpu_; }
private:
    struct ParticleState {
        render::float4 positionAge, velocityLife, tint, size;
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
    static_assert(sizeof(Vertex)==40);
    static_assert(sizeof(Uniforms)==64);
    static_assert(sizeof(DrawUniforms)==112);

    bool initGpu();
    bool initCpu();
    void releaseGpu();
    void switchToCpu(const std::string& why);
    gfx::Device* device_ = nullptr;
    ComputeEmitterDesc desc_;
    ComputeBackend backend_ = ComputeBackend::Cpu;
    fx::ParticleSystem cpu_;
    int cpuEffect_ = -1;
    gfx::ComputeHandle compute_;
    gfx::ShaderHandle vertexShader_;
    gfx::PipelineHandle pipeline_;
    gfx::BufferHandle states_, seeds_, slots_, vertices_, indices_;
    gfx::TextureHandle texture_;
    std::vector<float> ages_;
    std::vector<uint32_t> freeSlots_, pendingSlots_;
    std::vector<ParticleState> pendingStates_;
    render::mat4 viewProj_;
    fx::float3 eye_{}, right_{1,0,0}, up_{0,1,0};
    bool initialized_ = false, viewReady_ = false, verticesReady_ = false;
    ComputeStats stats_;
    std::string reason_;
};

} // namespace aiverse::spectra
