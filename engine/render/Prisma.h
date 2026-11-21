// Post-processing on the RHI: the effect chain of game.json / map "postfx" blocks (PrismaData.h).
// Plugs into Renderer3D through the PostProcess hook:
//
//   r3d.render(pass, &prisma);   // the scene goes into prisma's target when an effect is active
//   particles.render(...);       // still part of the scene
//   r3d.resolvePrisma();         // effects -> `pass`, which stays open for UI
//
// Every effect has GLSL + MSL code (PrismaShaders.h, one body for both) and runs per quality tier
// (PrismaQuality, PrismaData plans). RGBA16F on capable High-tier devices, RGBA8 fallback; no compute.
#pragma once
#include <functional>
#include <algorithm>
#include <cmath>
#include "RenderDeviceLifetime.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "PrismaData.h"
#include "PrismaLensWater.h"
#include "PhysicalSky.h"
#include "LocalFog.h"
#include "PostFxData.h" // legacy aliases used by existing clients and editor
#include "PrismaVolumes.h"
#include "PrismaRenderGraph.h" // GPU-free post-pass graph planner; executor follows later
#include "PrismaBloomGraph.h" // persistent compiled Bloom topology per viewport
#include "PrismaRhiExecutor.h" // opt-in per-view graph GPU context
#include "PrismaGpuRetirement.h"
#include "PrismaAdaptiveResolution.h"
#include "Renderer3D.h"

namespace aiverse::render {

struct PrismaStats {
    int passes = 0, effects = 0, width = 0, height = 0;
    int outputWidth = 0, outputHeight = 0;
    bool temporalUpscaled = false;
    bool reactiveUsed = false;
    bool temporalFallback = false;
    bool adaptiveResolution = false;
    bool adaptiveMemoryPressure = false;
    bool adaptiveHistoryAdmissionRejected = false;
    uint8_t adaptiveViewPriority = 128;
    uint64_t adaptiveKnownGpuBytes = 0;
    uint64_t adaptiveMemoryLimitBytes = 0;
    bool adaptiveScopedGpuTiming = false;
    double adaptiveGpuMs = 0;
    float requestedRenderScale = 1.0f;
    bool temporalResolved = false, temporalJittered = false;
    float jitterPixelX = 0, jitterPixelY = 0;
};

// Per-frame, presentation-only environmental tint from a volume system.
// Prisma never queries ECS or owns the meaning of any water volume.
struct PrismaUnderwater {
    bool submerged=false;
    float tintR=0, tintG=0, tintB=0;
    float absorption=0;
    float depthMetres=0;
    float distortion=0, scattering=0;
    float surfaceBlend=-1; // negative: legacy binary; otherwise 0..1 near boundary
    float surfaceOffset=0; // signed water plane height relative to camera Y
};
struct PrismaUnderwaterState {
    float blend=0;
    float tintR=0, tintG=0, tintB=0;
    float absorption=0, depthMetres=0;
    float distortion=0, scattering=0;
    float crossing=0;
    bool wasSubmerged=false;
    float surfaceBlend=0;
    float surfaceOffset=0;
};

// Pure presentation transition policy, testable without a GPU device.
inline PrismaUnderwaterState advancePrismaUnderwater(
        PrismaUnderwaterState state,PrismaUnderwater target,float seconds) {
    const float dt=std::isfinite(seconds)?std::clamp(seconds,0.f,0.25f):0.f;
    const float alpha=1.f-std::exp(-dt*6.f);
    auto smooth=[alpha](float& v,float goal){v+=alpha*(goal-v);};
    auto finite=[](float v,float lo,float hi) {
        return std::isfinite(v)?std::clamp(v,lo,hi):lo;
    };
    if(target.submerged!=state.wasSubmerged && dt>0.f)
        state.crossing=1.f;
    state.wasSubmerged=target.submerged;
    state.crossing*=std::exp(-dt*8.f);
    const float opacity=target.surfaceBlend>=0.f ?
        finite(target.surfaceBlend,0.f,1.f) : (target.submerged?1.f:0.f);
    smooth(state.blend,opacity);
    state.surfaceBlend=opacity;
    if(target.submerged || opacity>0.f) {
        smooth(state.tintR,finite(target.tintR,0.f,1.f));
        smooth(state.tintG,finite(target.tintG,0.f,1.f));
        smooth(state.tintB,finite(target.tintB,0.f,1.f));
        smooth(state.absorption,finite(target.absorption,0.f,4.f));
        smooth(state.depthMetres,finite(target.depthMetres,0.f,10000.f));
        // Signed distance is required by the depth compositor to clip each
        // scene ray against the water plane instead of tinting air segments.
        state.surfaceOffset=finite(target.surfaceOffset,-10000.f,10000.f);
        smooth(state.distortion,finite(target.distortion,0.f,1.f));
        smooth(state.scattering,finite(target.scattering,0.f,1.f));
    }
    if(state.blend<0.0001f && state.crossing<0.0001f && !target.submerged)
        state={};
    return state;
}

// Fraction of a camera-to-scene segment below the water plane.
// a/b are signed (surfaceY - endpointY). Handles partial crossings in
// either direction, a fully submerged ray, and a fully airborne ray.
inline float prismaUnderwaterRayFraction(float a,float b) {
    if(!std::isfinite(a)||!std::isfinite(b))return 0.f;
    if(a>=0.f&&b>=0.f)return 1.f;
    if(a<=0.f&&b<=0.f)return 0.f;
    return std::clamp(std::max(a,b)/(std::abs(a)+std::abs(b)),0.f,1.f);
}

struct PrismaUnderwaterBudget {
    float distortion=0,scattering=0,crossing=0;
    uint32_t samples=0;
};
// Explicit mobile-friendly quality policy: no extra scene passes or textures.
inline PrismaUnderwaterBudget prismaUnderwaterBudget(PrismaQuality quality) {
    switch(quality) {
        case PrismaQuality::Off:return {};
        case PrismaQuality::Low:return {0.2f,0.25f,0.3f,1};
        case PrismaQuality::Medium:return {0.6f,0.7f,0.7f,2};
        case PrismaQuality::High:return {1.f,1.f,1.f,3};
    }
    return {};
}

class PrismaVolumeStack;

class Prisma : public PostProcess {
public:
    void setGpuDeviceLease(RenderDeviceLifetime::Lease lease) {gpuDeviceLease_=std::move(lease);}
    void setUnderwater(PrismaUnderwater target,float deltaSeconds);
    const PrismaUnderwaterState& underwater() const {return underwater_;}
    void resetUnderwater() {underwater_={};}
    // Legacy diagnostics/collection route to the same Prisma fence owner.
    size_t pendingTargetRetirements() const;
    bool collectRetiredTargets() {collectGpuRetirements(); return pendingTargetRetirements()==0;}
    bool init(gfx::Device& dev);
    void shutdown();
    // For hosts that can guarantee a truly idle GPU: drain quarantined
    // prior viewport-size render targets (never infer idle from CPU frames).
    void releaseRetiredSizesAfterGpuIdle();
    size_t pendingRetiredSizes() const { return retiredSizes_.size(); }
    // Close a presentation view without touching size-shared scene targets.
    // Exposure history is owned per view; borrowed velocity/reactive handles
    // are not destroyed. The current GPU submission fences owned textures.
    void retireView(uint64_t viewId);
    // Explicit host-frame registry of all live camera identities. Inactive
    // view-only state is GPU-fenced via retireView; shared size caches stay
    // until retireUnreferencedSizeCaches. Call AFTER all view submissions
    // for this frame, never infer activity from CPU frameSerial.
    size_t retireUnregisteredViews(const std::vector<uint64_t>& liveViewIds);
    // At a graphics-owner frame boundary, retire size-keyed targets that NO
    // recorded view currently references. Never destroys active/in-flight GPU
    // resources synchronously. The caller keeps any desired view IDs live.
    size_t retireUnreferencedSizeCaches();
    // When Scene/Game is hidden and no new post frame is rendered, the
    // editor still polls verified GPU completions each UI frame.
    void collectGpuRetirements();
    struct RetirementStats {
        size_t activeSizes = 0;
        size_t retiredSizes = 0;
        size_t exposureViews = 0;
        size_t motionViews = 0;
        size_t temporalViews = 0;
        size_t fencedTextures = 0;
        size_t unfencedTextures = 0;
        size_t graphRetiredTextures = 0;
        size_t graphLiveTextures = 0;
        size_t graphUnknownTextures = 0;
        uint64_t graphLiveKnownBytes = 0;
        uint64_t graphPendingKnownBytes = 0;
        uint64_t knownPendingBytes = 0; // lower bound; includes retired graph handles
        uint64_t lastGpuCompletedFence = 0;
    };
    RetirementStats retirementStats() const;
    // CPU-only diagnostics: shared size caches are counted ONCE in totals.
    // A view reports the shared cache it references, not an allocation it owns.
    // A missing RHI metadata entry increments unknownTextures (not 0-byte VRAM).
    struct ViewMemoryStats {
        uint64_t viewId = 0;
        int width = 0, height = 0;
        size_t sharedSizeViews = 0; // >1 means other view IDs reference this same size cache
        size_t sharedTextures = 0, ownedHistoryTextures = 0;
        size_t ownedTemporalTextures = 0;
        size_t unknownTextures = 0;
        uint64_t sharedKnownBytes = 0, ownedHistoryKnownBytes = 0;
        uint64_t ownedTemporalKnownBytes = 0;
        uint64_t graphLiveKnownBytes = 0, graphPendingKnownBytes = 0;
        size_t graphLiveTextures = 0, graphPendingTextures = 0;
    };
    struct MemoryStats {
        std::vector<ViewMemoryStats> views;
        size_t activeSizeCaches = 0, unreferencedSizeCaches = 0;
        size_t liveTextures = 0, unknownTextures = 0;
        uint64_t liveKnownBytes = 0; // active size caches + per-view exposure + resident LUTs
        uint64_t sharedSizeKnownBytes = 0;
        uint64_t unreferencedSizeKnownBytes = 0; // subset of sharedSizeKnownBytes
        uint64_t viewHistoryKnownBytes = 0;
        uint64_t temporalHistoryKnownBytes = 0; // subset of viewHistoryKnownBytes
        uint64_t otherLiveKnownBytes = 0;
        uint64_t graphPendingKnownBytes = 0; // belongs to retirement, NOT live bytes
    };
    MemoryStats memoryStats() const;
    // Optional directly-owned Renderer3D lower-bound estimate supplied by
    // this Prisma instance's host BEFORE renderSizeForOutput. Caller must
    // exclude Prisma-owned textures/targets and not duplicate pending motion
    // with live motion. This does not imply complete hardware VRAM.
    void setExternalRendererGpuMemory(uint64_t liveKnownBytes,
                                      uint64_t pendingKnownBytes) {
        externalRendererLiveKnownBytes_ = liveKnownBytes;
        externalRendererPendingKnownBytes_ = pendingKnownBytes;
    }
    // The global geometry pool has separate ownership and lifetime from
    // Renderer3D's directly-owned GPU buffers. Count its page allocations
    // once per Prisma device, never for each Model referencing a page.
    void setExternalGeometryPoolGpuMemory(uint64_t livePageBytes) {
        externalGeometryPoolKnownBytes_ = livePageBytes;
    }
    // Dedicated model GPU buffers, texture caches and owned external
    // presentation assets. The host submits disjoint owner totals only;
    // pooled geometry, borrowed textures, Renderer3D and Prisma handles
    // are excluded to avoid double counting.
    void setExternalPresentationAssetsGpuMemory(uint64_t liveKnownBytes) {
        externalPresentationAssetKnownBytes_=liveKnownBytes;
    }
    // Per-presentation view admission/priority policy; no ECS/genre coupling.
    // Priority 0 = yield under known memory pressure first, 255 = last.
    // maxTemporalHistoryMiB=0 means no per-view hard admission limit.
    // Limit applies only to the camera-private TWO output RGBA16F histories,
    // NOT the shared scene scratch/cache nor complete device VRAM.
    struct ViewQualityPolicy {
        uint8_t priority = 128;
        uint32_t maxTemporalHistoryMiB = 0;
    };
    void setViewQualityPolicy(uint64_t id, ViewQualityPolicy policy) {
        viewQualityPolicies_[id]=policy;
    }
    void clearViewQualityPolicy(uint64_t id) {
        viewQualityPolicies_.erase(id);
    }

    // An actual completed GPU-query interval for one view, NOT a whole
    // frame metric. The host owns polling, never block for a query result.
    // Unsupported backends leave this map empty. No FPS/RTS dependency.
    struct ViewGpuTiming {
        uint64_t viewId = 0;
        uint64_t sampleSequence = 0; // increments only on a finished GPU query
        uint64_t gpuFenceAtRead = 0; // freshness, not a replacement for query token
        double lastGpuMs = 0;
    };
    void acceptViewGpuTiming(uint64_t viewId, double gpuMs);
    std::vector<ViewGpuTiming> viewGpuTimings() const;

    // game.json "postfx" then the map's (either may be null); problems go to `issues`.
    void configure(const Json& game, const Json& map, std::vector<std::string>* issues = nullptr);
    // Negative resumes the steady presentation clock; nonnegative freezes fog advection.
    void setLocalFogTime(double seconds) { fogTime_=seconds; }
    void setLocalFog(LocalFogSettings s) { fogSettings_=s; }
    LocalFogScene& localFog() { return fogScene_; }
    const LocalFogScene& localFog() const { return fogScene_; }
    void setPhysicalSky(PhysicalSkySettings s) { skySettings_=sanitizePhysicalSky(s); }
    // Positive displacement moves clouds along positive world X/Z (metres).
    void setPhysicalSkyAdvection(float2 meters) { skyDisplacement_=meters;skyAdvection_=true; }
    void clearPhysicalSkyAdvection() { skyDisplacement_={};skyAdvection_=false; }
    const PhysicalSkySettings& physicalSky() const { return skySettings_; }
    // Explicit time permits paused/reproducible captures; negative resumes the host clock.
    void setPhysicalSkyTime(double seconds) { skyTime_=seconds; }

    void setSettings(PrismaSettings s);
    const PrismaSettings& settings() const { return settings_; }
    const PrismaSettings& baseSettings() const { return baseSettings_; }

    // Native/ECS users choose per-camera profiles without modifying game.json.
    // Call before Renderer3D::render(); uses original configured settings as base.
    void applyCameraVolumes(const PrismaVolumeStack& stack, float3 eye,
                            uint32_t cameraMask = 0xffffffffu);
    void clearCameraVolumes();
    // Camera cuts/teleports invalidate the temporal exposure history per viewport.
    void resetAutoExposure();
    // Stable presentation-only camera/view identity. 0 is the legacy default.
    // Set before rendering a view, even when two views have equal dimensions.
    void setViewId(uint64_t id) { viewId_ = id; }
    uint64_t viewId() const { return viewId_; }
    // Native cinematic camera controls, indexed ONLY by stable viewId.
    // Overrides take precedence over authored postfx/volume parameters,
    // including an explicit false; clear to restore profile inheritance.
    // These affect presentation state only, never authoritative ECS.
    void setViewExposureFreeze(uint64_t viewId, bool freeze);
    void clearViewExposureFreeze(uint64_t viewId);
    struct ExposureMeterRegion {
        bool enabled = false;
        float centerX = 0.5f, centerY = 0.5f; // normalized bottom-up screen UV
        float radius = 0.25f, feather = 0.10f; // relative to frame height
        float outsideWeight = 0.05f; // strictly >0, prevents empty metering
        // Appended to preserve existing aggregate {enabled,center,radius,
        // feather,outsideWeight} call sites; circle remains the default.
        bool box = false;
        float halfWidth = 0.25f, halfHeight = 0.25f; // normalized viewport axes
    };
    void setViewExposureMeterRegion(uint64_t viewId, ExposureMeterRegion region);
    void clearViewExposureMeterRegion(uint64_t viewId);
    // Fixed manual camera EV replaces GPU Auto Exposure (no 1x1 history).
    // Unlike freezeExposure, it works without having first metered a frame.
    // Explicit enabled=false overrides an inherited manualExposure=true.
    // The opt-in remains per stable view and requires an authored
    // AutoExposure effect and usable linear HDR presentation.
    void setViewManualExposure(uint64_t viewId, bool enabled, float ev);
    void clearViewManualExposure(uint64_t viewId);
    // Per-view physical-lens defocus (LENS). Replaces metric focus distance,
    // sharp interval and max blur of the AUTHORED depthOfField effect; keeps
    // its intensity and silhouette tolerance. enabled=false switches DoF off
    // for this view. Still requires the effect and AIVERSE_PRISMA_DOF=1.
    struct DepthOfFieldOverride {
        bool enabled = true;
        float focusDistance = 12.0f, focusRangeMeters = 4.0f, maxBlur = 8.0f;
    };
    void setViewDepthOfField(uint64_t viewId, DepthOfFieldOverride dof);
    void clearViewDepthOfField(uint64_t viewId);
    bool viewDepthOfField(uint64_t viewId, DepthOfFieldOverride& out) const;
    // Optional subpixel jitter for a COPY of the current render camera.
    // Call after setViewId/applyCameraVolumes and BEFORE Renderer3D::beginScene;
    // retain the original camera for gameplay picking and 2D overlays.
    // Active only with HDR temporalResolve + AIVERSE_PRISMA_JITTER=1.
    // Pass the INTERNAL render resolution, not the final display dimensions.
    RTSCamera jitteredCamera(RTSCamera camera, int width, int height);
    // Host query BEFORE beginScene(): returns internal 3D resolution while
    // leaving the output texture/UI at the caller's full output resolution.
    // The host must call it every rendered frame for offscreen output metadata.
    // The default is unchanged. Upscaling requires temporalResolve, HDR/High,
    // an explicit renderScale < 1 and AIVERSE_PRISMA_UPSCALE=1.
    std::pair<int,int> renderSizeForOutput(int width, int height);
    void resetAutoExposure(uint64_t id);
    // Camera cut/teleport: stop motion blur on the next rendered frame.
    void resetMotionHistory(uint64_t id);
    void resetMotionHistory();
    // Optional per-frame object velocity: RG = current-minus-previous screen UV,
    // B > 0.5 = valid object pixel. Must match this view's render dimensions.
    // The external texture is borrowed and cleared after each resolve.
    void setObjectMotionVectors(uint64_t view, gfx::TextureHandle velocity);
    void setObjectMotionVectors(uint64_t view, MotionTextureInput velocity);
    // Optional current-frame R=0..1 reactive coverage; borrowed until resolve.
    void setMotionReactiveMask(uint64_t view, gfx::TextureHandle mask);
    void setMotionReactiveMask(uint64_t view, MotionTextureInput mask);

    void setQuality(PrismaQuality q) {
        if (quality_ != q) {
            quality_ = q; resetAutoExposure(); resetMotionHistory();
            hdrFailed_ = false; hdrFailedSizes_.clear(); // fresh HDR attempt per tier
        }
    }
    PrismaQuality quality() const { return quality_; }
    static PrismaQuality defaultQualityFor(const gfx::Device& dev, bool mobile);   // AIVERSE_POSTFX overrides
    void setEnabled(bool on) {
        if (enabled_ != on) { enabled_ = on; resetAutoExposure(); resetMotionHistory(); }
    }          // editor: preview on / off
    bool enabled() const { return enabled_; }

    // Reads an image of the game (LUTs): path -> RGBA8 top row first. Client: content files; editor: project dir.
    using ImageLoader = std::function<bool(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba)>;
    void setImageLoader(ImageLoader fn);

    // PostProcess
    bool active() const override;
    bool linearScene() const override { return hdrFrame_; }
    bool hdrRequested() const;
    // Pure read-only requested/capability/last-frame diagnostic. No GPU
    // programs, textures, or temporal histories are allocated by this query.
    struct RuntimeCapabilities {
        bool deviceHealthy=false, hdrFormatSupported=false;
        bool hdrAuthoredRequested=false, hdrRequested=false;
        bool hdrEffectiveLastFrame=false;
        bool temporalRequested=false, temporalPipelinePrepared=false;
        bool temporalResolvedLastFrame=false;
        bool jitterRequested=false, jitterAppliedLastFrame=false;
        bool upscaleRequested=false, upscaleAppliedLastFrame=false;
        bool adaptiveRequested=false, scopedGpuSampleAvailable=false;
        bool velocityPipelinePrepared=false, reactiveLastFrame=false;
        std::string fallbackReason;
    };
    RuntimeCapabilities runtimeCapabilities() const;
    // GPU-free snapshot captured AFTER resolve() for one stable camera.
    // encoded means a draw was issued, NOT that the GPU finished or the
    // resulting pixels were verified. Queries never compile shaders.
    struct EffectExecution {
        PrismaType type = PrismaType::ColorGrade;
        bool authored = false;       // present in the resolved camera profile
        bool requested = false;      // enabled, tier and opt-in/parameter gates
        bool pipelinePrepared = false; // cached RHI handle, not device validation
        bool encoded = false;        // an applicable pass actually issued draw(s)
        bool degraded = false;       // encoded but had to use a lower-quality fallback
        std::string reason;          // skip/degraded explanation; empty for full-success
    };
    struct ViewEffectReport {
        uint64_t viewId = 0;
        uint64_t sequence = 0;      // resolve count per view since last settings reset
        int width = 0, height = 0;
        PrismaQuality quality = PrismaQuality::Off;
        bool hdr = false, sceneReady = false;
        int shaderGraphTextures = 0;     // authored graph texture bindings on last resolve
        bool shaderGraphFallback = false; // graph omitted, fixed postFX preserved if possible
        int autoExposureTextureSlot = -1; // -1 when final composite omitted exposure
        float autoExposureHighlightRejection = 0; // last encoded composite; zero if absent
        bool autoExposureFrozen = false; // valid 1x1 camera history reused, no meter passes
        bool autoExposureRegionMetered = false; // current scene metered with authored circle
        bool manualExposureEncoded = false; // fixed EV applied by final HDR composite
        float manualExposureEV = 0; // last applied EV, meaningful iff manualExposureEncoded
        bool autoExposureBoxMetered = false; // last encoded region meter used a box, not circle
        // Borrowed reactive coverage is camera-local and current-frame only.
        // "accepted" means declared metadata/RHI checks passed, NOT pixel proof.
        bool reactiveInputSupplied = false;
        bool reactiveInputAccepted = false;
        bool objectVelocityInputAccepted = false; // independent borrowed RGBA16F
        bool reactiveWithoutObjectVelocity = false; // current-frame mask only
        bool motionBlurReactiveEdgesEnabled = false; // actually encoded with tap rejection
        std::vector<EffectExecution> effects;
    };
    const ViewEffectReport* effectReport(uint64_t viewId) const;
    // Last reported view IDs only; owning camera IDs are not inferred from size.
    std::vector<uint64_t> effectReportViewIds() const;
    // Canonical history has sole ownership. The optional older
    // PrismaScreenLighting wrapper MUST skip its separate TAA when true.
    bool canonicalTemporalRequested() const;
    gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) override;
    void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) override;
    bool wantsObjectMotionVectors() const override;
    gfx::TextureHandle objectMotionSceneDepth() const override { return sceneDepthForMotion_; }
    uint64_t objectMotionEpoch(uint64_t viewId) const override;
    bool objectMotionContext(uint64_t& viewId, mat4& previousViewProj) const override;
    void acceptObjectMotionVectors(gfx::TextureHandle velocity) override;
    void acceptObjectMotionVectors(MotionTextureInput velocity) override;
    void acceptMotionReactiveMask(gfx::TextureHandle reactive) override;
    void acceptMotionReactiveMask(MotionTextureInput reactive) override;

    const PrismaStats& stats() const { return stats_; }
    const std::string& lastError() const { return error_; }

private:
    struct Target { gfx::TextureHandle tex; int w = 0, h = 0; gfx::PixelFormat format = gfx::PixelFormat::RGBA8; };
    struct Sized {                         // targets for one output size (editor: Scene + Game views)
        int w = 0, h = 0;
        uint64_t lastUse = 0;
        std::map<int, Target> targets;     // by role
        std::map<uint64_t, gfx::TextureHandle> depths;   // Metal: every pass has a depth attachment, shared per size
        PrismaBloomGraphCache bloomGraph; // invalidates on size, mip count or HDR format changes
        std::unique_ptr<PrismaRhiExecutor> graphExecutor; // size-shared graph cache, NOT view-owned
    };
    struct RetiredSized {
        Sized resources;
        uint64_t lastSubmittedFence = 0; // 0 = no backend proof, await GPU idle
    };
    struct ExposureView {
        gfx::TextureHandle history[2];
        int current = 0;
        bool valid = false;
        double time = 0;
    };
    struct TemporalView {
        gfx::TextureHandle history[2]; // RGBA16F: HDR RGB + previous hardware depth in A
        int width = 0, height = 0;
        int inputWidth = 0, inputHeight = 0;
        int current = 0; // most recently written history index
        bool valid = false;
        uint64_t jitterSequence = 0;
        float2 currentJitterUv{}, previousJitterUv{}; // NDC jitter / 2, unflipped UV
        bool jitterActive = false;
        float lastJitterPixels = 0;
        bool preflightRejected = false;
        unsigned preflightCooldown = 0;
        bool projectionKnown = false;
        bool previousOrtho = false;
        float previousFovY = 0, previousNear = 0, previousFar = 0;
    };
    struct MotionView {
        mat4 previousViewProj;
        float3 previousCameraPos{};
        bool valid = false;
        uint64_t cutEpoch = 1;
        int width = 0, height = 0;
        MotionTextureInput objectVelocity; // borrowed for this frame only
        MotionTextureInput reactiveMask; // borrowed for this frame only
    };
    struct Lut { gfx::TextureHandle tex; int n = 0; bool failed = false; };
    enum Prog { kPrefilter, kDown, kUp, kBlur, kShaftsPre, kShafts, kFxaa, kDepthOfField, kExposureLog, kExposureReduce, kExposureAdapt, kMotionVectors, kMotionBlur, kTemporalResolve, kProgCount };

    Target& target(Sized& s, int role, int w, int h);
    gfx::TextureHandle depthFor(Sized& s, int w, int h, bool scratch = false);
    Sized& sized(int w, int h);
    void destroySized(Sized& s);
    bool preflightUpscale(int outputW, int outputH, int renderW, int renderH);
    void collectCompletedSizes(uint64_t completedGpuFence);
    gfx::PipelineHandle program(Prog p);
    // graphKey: optional precomputed graph->dump(-1) (one dump per frame).
    gfx::PipelineHandle composite(uint32_t mask, const Json* graph = nullptr,
                                  const std::string* graphKey = nullptr);
    void draw(Sized& s, const Target& dst, gfx::PipelineHandle pipe, const void* u, size_t size,
              std::initializer_list<std::pair<int, gfx::TextureHandle>> tex);
    const Lut* lut(const std::string& path);
    void release(gfx::TextureHandle& t);
    void retireTarget(gfx::TextureHandle& t); // holds until real GPU fence

    void checkGpuDevice(const gfx::Device& dev) const {
        if(gpuDeviceLease_.device() && !gpuDeviceLease_.validFor(dev))
            throw std::logic_error("Prisma GPU device generation expired");
    }
    RenderDeviceLifetime::Lease gpuDeviceLease_;
    PrismaUnderwaterState underwater_{};
    std::map<uint64_t,PrismaLensWaterState> lensWaterViews_;
    gfx::Device* dev_ = nullptr;
    PrismaSettings settings_;
    double fogTime_=-1,fogEpoch_=-1;
    LocalFog fog_;
    LocalFogSettings fogSettings_;
    LocalFogScene fogScene_;
    PhysicalSky sky_;
    PhysicalSkySettings skySettings_;
    double skyTime_=-1,skyEpoch_=-1;
    float2 skyDisplacement_{};
    bool skyAdvection_=false;

    PrismaSettings baseSettings_;
    PrismaQuality quality_ = PrismaQuality::Medium;
    bool enabled_ = true;
    bool hdrFrame_ = false, sceneReady_ = false;
    // HDR target allocation failure: stay on the low-cost path for the
    // failed target sizes (bounded; several views may alternate sizes).
    // hdrFailed_ reflects the size of the current scenePass; a new size,
    // quality change or init() gets a fresh attempt.
    bool hdrFailed_ = false;
    std::vector<std::pair<int,int>> hdrFailedSizes_;
    std::string hdrFailedError_;
    // Authored HDR mode with the AIVERSE_HDR override (off/0, on/1, auto).
    static HdrMode hdrModeWithEnv(HdrMode authored);
    gfx::BufferHandle triangle_;
    gfx::TextureHandle black_;
    gfx::TextureHandle sceneDepthForMotion_;
    gfx::ShaderHandle shaders_[kProgCount];
    gfx::PipelineHandle pipes_[kProgCount];
    bool failed_[kProgCount] = {};
    std::map<std::string, std::pair<gfx::ShaderHandle, gfx::PipelineHandle>> composites_;
    std::map<std::string, bool> compositeFailed_;
    std::vector<Sized> sizes_;
    std::vector<RetiredSized> retiredSizes_; // retains GPU resources until verified safe
    PrismaGpuRetirementQueue<gfx::TextureHandle> retiredTargets_;
    std::vector<gfx::TextureHandle> unfencedTargets_;
    // Temporal state is keyed by view identity, NOT by render target size.
    std::map<uint64_t, ExposureView> exposureViews_;
    std::map<uint64_t, bool> viewExposureFreeze_;
    std::map<uint64_t, ExposureMeterRegion> viewExposureMeterRegions_;
    struct ManualExposureOverride { bool enabled = false; float ev = 0; };
    std::map<uint64_t, ManualExposureOverride> viewManualExposure_;
    std::map<uint64_t, DepthOfFieldOverride> viewDepthOfField_;
    std::map<uint64_t, MotionView> motionViews_;
    std::map<uint64_t, TemporalView> temporalViews_;
    std::map<uint64_t, PrismaAdaptiveResolution> adaptiveViews_;
    std::map<uint64_t, ViewGpuTiming> scopedGpuTimings_;
    std::map<uint64_t, ViewEffectReport> effectReports_;
    struct AdaptivePressure {
        uint64_t knownBytes = 0, limitBytes = 0;
        bool overLimit = false, scopedGpu = false;
        double gpuMs = 0;
    };
    std::map<uint64_t, AdaptivePressure> adaptivePressure_;
    std::map<uint64_t, ViewQualityPolicy> viewQualityPolicies_;
    std::map<uint64_t, bool> viewHistoryAdmissionRejected_;
    uint64_t externalRendererLiveKnownBytes_ = 0;
    uint64_t externalRendererPendingKnownBytes_ = 0;
    uint64_t externalGeometryPoolKnownBytes_ = 0;
    uint64_t externalPresentationAssetKnownBytes_ = 0;
    // Last successful postprocess scene pass per stable view ID. A size cache
    // can be shared by several views; retireView() removes a view reference.
    std::map<uint64_t, std::pair<int, int>> viewDimensions_;
    // Host-declared full output size, needed for offscreen targets whose RHI
    // metadata may be unavailable. Updated by renderSizeForOutput each frame.
    std::map<uint64_t, std::pair<int,int>> requestedOutputSizes_;
    uint64_t viewId_ = 0;
    std::map<std::string, Lut> luts_;
    ImageLoader loader_;
    std::map<std::string,gfx::TextureHandle> graphTextures_;
    std::map<std::string,std::vector<std::string>> graphPaths_;
    gfx::TextureHandle graphTexture(const std::string& path);
    double graphEpoch_ = -1;
    uint64_t frame_ = 0;
    PrismaStats stats_;
    std::string error_;
};


} // namespace aiverse::render
