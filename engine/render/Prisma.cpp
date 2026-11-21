#include "Prisma.h"
#include "PrismaVolumes.h"
#include "PrismaRhiExecutor.h"
#include "PrismaCompositeTextureLayout.h"
#include <chrono>
#include "graph/ShaderGraph.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include "PrismaShaders.h"

namespace aiverse::render {

namespace {

// Composite feature bits (also the shader cache key).
enum : uint32_t {
    kPfPixelate = 1u << 0, kPfCa = 1u << 1, kPfSharpen = 1u << 2, kPfTilt = 1u << 3, kPfBloom = 1u << 4,
    kPfShafts = 1u << 5, kPfGrade = 1u << 6, kPfLut = 1u << 7, kPfPosterize = 1u << 8, kPfVignette = 1u << 9,
    kPfGrain = 1u << 10, kPfFade = 1u << 11, kPfLetterbox = 1u << 12,
    kPfAutoExposure = 1u << 13,
    kPfUnderwater = 1u << 14, kPfUnderwaterDepth = 1u << 15, kPfLensWater = 1u << 16, kPfWaterline = 1u << 17,
};
const char* kPfNames[] = {"PF_PIXELATE", "PF_CA", "PF_SHARPEN", "PF_TILT", "PF_BLOOM", "PF_SHAFTS", "PF_GRADE",
                          "PF_LUT", "PF_POSTERIZE", "PF_VIGNETTE", "PF_GRAIN", "PF_FADE", "PF_LETTERBOX", "PF_AUTO_EXPOSURE", "PF_UNDERWATER", "PF_UNDERWATER_DEPTH", "PF_LENS_WATER", "PF_WATERLINE"};

// Target roles inside one Sized set.
enum Role { kScene = 0, kAa = 1, kBloomDown = 10, kBloomUp = 20, kTilt0 = 30, kTilt1 = 31, kShaft0 = 40, kShaft1 = 41, kDof = 50, kExposureMip = 80, kExposureHistory = 100, kMotionVelocity = 130, kMotionColor = 131, kPhysicalSky = 200, kLocalFog = 201 };

struct SimpleU { float px[4]; float p0[4]; float sun[4]; };
// Only the HDR luminance prefilter needs the camera-authored region.
// Reduction/adaptation and all other postFX retain their original UBOs.
struct ExposureMeterU {
    float px[4], p0[4], sun[4], region[4], regionBox[4];
};
const char* kExposureMeterFields[] = {"px", "p0", "sun", "region", "regionBox"};
// Keep the legacy SimpleU field prefix untouched. New camera-linear DoF
// uniforms are present only in the separate DepthOfField shader program.
struct DepthOfFieldU {
    float px[4], p0[4], depthInfo[4], focusInfo[4];
};
const char* kDepthOfFieldFields[] = {"px", "p0", "depthInfo", "focusInfo"};
struct MotionBlurU {
    float px[4], p0[4], sun[4], depthInfo[4], tapInfo[4];
};
const char* kMotionBlurFields[] = {"px", "p0", "sun", "depthInfo", "tapInfo"};
struct MotionUniforms {
    float px[4], p0[4], sun[4];
    float inv0[4], inv1[4], inv2[4], inv3[4];
    float prev0[4], prev1[4], prev2[4], prev3[4];
};
// Preserve the existing motion UBO prefix and add only temporal subpixel
// jitter in the canonical fragment shader's last vec4.
struct TemporalUniforms {
    float px[4], p0[4], sun[4];
    float inv0[4], inv1[4], inv2[4], inv3[4];
    float prev0[4], prev1[4], prev2[4], prev3[4];
    float jitter[4];
    float inputPx[4]; // reciprocal native 3D scene resolution
};
const char* kMotionFields[] = {
    "px", "p0", "sun", "inv0", "inv1", "inv2", "inv3",
    "prev0", "prev1", "prev2", "prev3"
};
const char* kTemporalFields[] = {
    "px", "p0", "sun", "inv0", "inv1", "inv2", "inv3",
    "prev0", "prev1", "prev2", "prev3", "jitter", "inputPx"
};
struct CompositeU {
    float px[4], grade0[4], grade1[4], lift[4], gamma[4], gain[4], filt[4], vig[4], bloom[4], shafts[4], tilt[4], fade[4], lut[4], lbox[4], graphFrame[4], underwater0[4], underwater1[4], underwater2[4], inverseViewProj0[4], inverseViewProj1[4], inverseViewProj2[4], inverseViewProj3[4], underwaterCamera[4], lensWater0[4], lensWater1[4], lensWater2[4], waterline0[4], waterline1[4];
};
const char* kSimpleFields[] = {"px", "p0", "sun"};
const char* kCompositeFields[] = {"px", "grade0", "grade1", "lift", "gamma", "gain", "filt", "vig", "bloom", "shafts", "tilt", "fade", "lut", "lbox", "graphFrame", "underwater0", "underwater1", "underwater2", "inverseViewProj0", "inverseViewProj1", "inverseViewProj2", "inverseViewProj3", "underwaterCamera", "lensWater0", "lensWater1", "lensWater2", "waterline0", "waterline1"};

struct Built { std::string vs, fs, msl; };

// Wraps a common-subset body (PrismaShaders.h) into GLSL ES 3.00 and MSL.
Built buildProgram(const char* const* fields, size_t fieldCount, uint32_t texMask, const std::string& defines,
                   const char* helpers, const char* body, uint32_t depthMask = 0) {
    Built b;
    b.vs = "layout(location = 0) in vec2 a_pos;\nout vec2 v_screen;\n"
           "void main() { v_screen = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    std::string block = "layout(std140) uniform U0 {";
    std::string mstruct = "struct U0 {", mlocals;
    for (size_t i = 0; i < fieldCount; ++i) {
        block += std::string(" vec4 ") + fields[i] + ";";
        mstruct += std::string(" float4 ") + fields[i] + ";";
        mlocals += std::string("    const float4 ") + fields[i] + " = u." + fields[i] + ";\n";
    }
    block += " };\n";
    mstruct += " };\n";
    const std::string common = "#define TUV(p) (px.z > 0.5 ? vec2((p).x, 1.0 - (p).y) : (p))\n"
                               "#define TDIR(d) (px.z > 0.5 ? vec2((d).x, -(d).y) : (d))\n";
    std::string gTex, mArgs, gMacros, mMacros;
    // Slot 9 is reserved for Auto Exposure when the postfx graph uses
    // all four authored texture slots (5,6,7,8).
    for (int t = 0; t < 10; ++t) {
        if (!(texMask & (1u << t))) continue;
        const std::string n = std::to_string(t);
        gTex += "uniform sampler2D tex" + n + ";\n";
        gMacros += "#define TEX" + n + "(p) texture(tex" + n + ", p)\n";
        const bool isDepth = (depthMask & (1u << t)) != 0;
        mArgs += std::string(", ") + (isDepth ? "depth2d<float>" : "texture2d<float>") +
                 " tex" + n + " [[texture(" + n + ")]], sampler tex" + n + "_s [[sampler(" + n + ")]]";
        mMacros += "#define TEX" + n + "(p) " + (isDepth ? "vec4(" : "") +
                   "tex" + n + ".sample(tex" + n + "_s, p)" + (isDepth ? ")" : "") + "\n";
    }
    b.fs = defines + block + gTex + "in vec2 v_screen;\nout vec4 o_color;\n" + common + gMacros +
           "#define RETURN(c) { o_color = (c); return; }\n#define PF_FN\n" + (helpers ? helpers : "") +
           "void main() {\n    vec2 s = v_screen;\n" + body + "}\n";
    b.msl = "#include <metal_stdlib>\nusing namespace metal;\n" + defines +
            "#define vec2 float2\n#define vec3 float3\n#define vec4 float4\n" + mstruct +
            "struct VIn { float2 pos [[attribute(0)]]; };\nstruct VOut { float4 pos [[position]]; float2 screen; };\n"
            "vertex VOut vs_main(VIn in [[stage_in]]) { VOut o; o.pos = float4(in.pos, 0.0, 1.0); o.screen = in.pos * 0.5 + 0.5; return o; }\n" +
            common + mMacros + "#define RETURN(c) return (c)\n#define PF_FN static\n" + (helpers ? helpers : "") +
            "fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]]" + mArgs + ") {\n" + mlocals +
            "    vec2 s = in.screen;\n" + body + "}\n";
    return b;
}

void put(float* d, float a, float b, float c, float e) { d[0] = a; d[1] = b; d[2] = c; d[3] = e; }

} // namespace

PrismaQuality Prisma::defaultQualityFor(const gfx::Device& dev, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_POSTFX")) {   // testing override: off|low|medium|high
        PrismaQuality r;
        if (parsePrismaQuality(q, r)) return r;
    }
    const std::string& n = dev.info().name;
    if (n.find("llvmpipe") != std::string::npos || n.find("SwiftShader") != std::string::npos) return PrismaQuality::Low;
    if (mobile) {
        for (const char* weak : {"Mali-4", "Mali-T", "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "PowerVR SGX", "PowerVR Rogue GE"})
            if (n.find(weak) != std::string::npos) return PrismaQuality::Low;
        return PrismaQuality::Medium;
    }
    return PrismaQuality::High;
}

bool Prisma::init(gfx::Device& dev) {
    checkGpuDevice(dev);
    dev_ = &dev;
    hdrFailed_ = false; hdrFailedSizes_.clear(); hdrFailedError_.clear();
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    gfx::BufferDesc bd; bd.data = tri; bd.size = sizeof tri; bd.label = "postfx triangle";
    triangle_ = dev.createBuffer(bd);
    const uint32_t zero = 0xff000000u;
    gfx::TextureDesc td; td.width = td.height = 1; td.data = &zero; td.label = "postfx black";
    black_ = dev.createTexture(td);
    return triangle_.valid();
}

void Prisma::release(gfx::TextureHandle& t) {
    if (t.valid() && dev_) dev_->destroyTexture(t);
    t = {};
}
void Prisma::retireTarget(gfx::TextureHandle& t) {
    if (!t.valid()) return;
    // A newly issued frame token includes the last render use on this
    // graphics queue; no token -> keep until explicit GPU idle teardown.
    const uint64_t fence = dev_ ? dev_->gpuSubmissionFence() : 0;
    if (fence) retiredTargets_.retire(t,fence);
    else unfencedTargets_.push_back(t);
    t = {};
}

void Prisma::destroySized(Sized& s) {
    // Only when the last GPU submission that may reference these targets
    // has completed (or the host has confirmed a fully idle device).
    if (s.graphExecutor) s.graphExecutor->resetAfterGpuIdle();
    s.graphExecutor.reset();
    for (auto& [r, t] : s.targets) release(t.tex);
    for (auto& [k, d] : s.depths) release(d);
    s.targets.clear();
    s.depths.clear();
    s.bloomGraph.clear();
}
void Prisma::collectCompletedSizes(uint64_t completedGpuFence) {
    if (!dev_ || completedGpuFence == 0) return;
    retiredTargets_.collect(completedGpuFence,
        [this](gfx::TextureHandle h) { dev_->destroyTexture(h); });
    for (auto it = retiredSizes_.begin(); it != retiredSizes_.end();) {
        if (it->lastSubmittedFence &&
            it->lastSubmittedFence <= completedGpuFence) {
            destroySized(it->resources);
            it = retiredSizes_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& s : sizes_)
        if (s.graphExecutor)
            s.graphExecutor->collectCompleted(completedGpuFence);
}
void Prisma::retireView(uint64_t id) {
    lensWaterViews_.erase(id);
    // Per-camera temporal state is not shared with another view, even if
    // both viewports have the same size. Do NOT touch Sized: color/depth
    // and graph targets are still shared by dimensions.
    auto exposure = exposureViews_.find(id);
    if (exposure != exposureViews_.end()) {
        for (auto& h : exposure->second.history) retireTarget(h);
        exposureViews_.erase(exposure);
    }
    viewExposureFreeze_.erase(id);
    viewExposureMeterRegions_.erase(id);
    viewManualExposure_.erase(id);
    viewDepthOfField_.erase(id);
    if (auto it = temporalViews_.find(id); it != temporalViews_.end()) {
        for (auto& h : it->second.history) retireTarget(h);
        temporalViews_.erase(it);
    }
    requestedOutputSizes_.erase(id);
    adaptiveViews_.erase(id);
    viewQualityPolicies_.erase(id);
    viewHistoryAdmissionRejected_.erase(id);
    scopedGpuTimings_.erase(id);
    effectReports_.erase(id);
    adaptivePressure_.erase(id);
    motionViews_.erase(id); // only matrix state + borrowed GPU handles
    viewDimensions_.erase(id); // the shared size cache itself stays resident
    // If the current presentation view was closed, keep its stable ID for
    // compatibility with callers; the next render seeds fresh history.
}

size_t Prisma::retireUnregisteredViews(
        const std::vector<uint64_t>& liveViewIds) {
    const std::set<uint64_t> live(liveViewIds.begin(),liveViewIds.end());
    std::set<uint64_t> tracked;
    const auto append=[&](const auto& map) {
        for (const auto& [id, state]:map) { (void)state; tracked.insert(id); }
    };
    append(exposureViews_);
    append(viewExposureFreeze_);
    append(viewExposureMeterRegions_);
    append(viewManualExposure_);
    append(temporalViews_);
    append(adaptiveViews_);
    append(viewQualityPolicies_);
    append(viewHistoryAdmissionRejected_);
    append(scopedGpuTimings_);
    append(effectReports_);
    append(adaptivePressure_);
    append(motionViews_);
    append(viewDimensions_);
    append(requestedOutputSizes_);
    size_t count=0;
    for (uint64_t id:tracked) if (!live.contains(id)) {
        retireView(id);
        ++count;
    }
    return count;
}

size_t Prisma::retireUnreferencedSizeCaches() {
    if (!dev_) return 0;
    size_t retiredCount = 0;
    for (auto it = sizes_.begin(); it != sizes_.end();) {
        bool referenced = false;
        for (const auto& [id, dimensions] : viewDimensions_) {
            (void)id;
            if (dimensions.first == it->w && dimensions.second == it->h) {
                referenced = true;
                break;
            }
        }
        if (referenced) { ++it; continue; }
        RetiredSized retired;
        retired.resources = std::move(*it);
        // The current submission token also fences draws already recorded
        // this frame. GPU completion is checked only by the owner collector.
        retired.lastSubmittedFence = dev_->gpuSubmissionFence();
        retiredSizes_.push_back(std::move(retired));
        it = sizes_.erase(it);
        ++retiredCount;
    }
    return retiredCount;
}

void Prisma::collectGpuRetirements() {
    if(dev_)checkGpuDevice(*dev_);
    if (dev_) collectCompletedSizes(dev_->gpuCompletedFence());
}

Prisma::RetirementStats Prisma::retirementStats() const {
    RetirementStats stats;
    stats.activeSizes = sizes_.size();
    stats.retiredSizes = retiredSizes_.size();
    stats.exposureViews = exposureViews_.size();
    stats.motionViews = motionViews_.size();
    stats.temporalViews = temporalViews_.size();
    stats.fencedTextures = retiredTargets_.pendingCount();
    stats.unfencedTextures = unfencedTargets_.size();
    stats.lastGpuCompletedFence = dev_ ? dev_->gpuCompletedFence() : 0;
    auto bytesOf = [this](gfx::TextureHandle h) -> uint64_t {
        if (!dev_ || !h.valid()) return 0;
        gfx::TextureInfo info{};
        if (!dev_->textureInfo(h,info) || info.width <= 0 || info.height <= 0)
            return 0;
        uint64_t bpp = 4;
        switch (info.format) {
            case gfx::PixelFormat::R8: bpp = 1; break;
            case gfx::PixelFormat::RGBA16F: bpp = 8; break;
            case gfx::PixelFormat::RGBA32F: bpp = 16; break;
            case gfx::PixelFormat::Depth: bpp = 4; break;
            default: break;
        }
        return uint64_t(info.width) * uint64_t(info.height) * bpp;
    };
    for (const auto& p : retiredTargets_.pending())
        stats.knownPendingBytes += bytesOf(p.handle);
    for (auto h : unfencedTargets_) stats.knownPendingBytes += bytesOf(h);
    for (const auto& item : retiredSizes_) {
        for (const auto& [role, target] : item.resources.targets) {
            if (target.tex.valid()) {
                if (item.lastSubmittedFence) ++stats.fencedTextures;
                else ++stats.unfencedTextures;
                stats.knownPendingBytes += bytesOf(target.tex);
            }
        }
        for (const auto& [key, handle] : item.resources.depths) {
            if (handle.valid()) {
                if (item.lastSubmittedFence) ++stats.fencedTextures;
                else ++stats.unfencedTextures;
                stats.knownPendingBytes += bytesOf(handle);
            }
        }
        if (item.resources.graphExecutor) {
            const auto gpu = item.resources.graphExecutor->gpuMemoryStats();
            // A sized context is fully retired; even its formerly live graph
            // resources must count toward pending memory until fence completes.
            stats.graphRetiredTextures += gpu.liveTextures + gpu.pendingTextures;
            stats.graphUnknownTextures += gpu.unknownFormatsOrDimensions;
            stats.graphPendingKnownBytes += gpu.liveKnownBytes + gpu.pendingKnownBytes;
            stats.knownPendingBytes += gpu.liveKnownBytes + gpu.pendingKnownBytes;
            if (item.lastSubmittedFence)
                stats.fencedTextures += gpu.liveTextures + gpu.pendingTextures;
            else
                stats.unfencedTextures += gpu.liveTextures + gpu.pendingTextures;
        }
    }
    for (const auto& s : sizes_) {
        if (!s.graphExecutor) continue;
        const auto gpu = s.graphExecutor->gpuMemoryStats();
        stats.graphLiveTextures += gpu.liveTextures;
        stats.graphRetiredTextures += gpu.pendingTextures;
        stats.graphUnknownTextures += gpu.unknownFormatsOrDimensions;
        stats.graphLiveKnownBytes += gpu.liveKnownBytes;
        stats.graphPendingKnownBytes += gpu.pendingKnownBytes;
        stats.knownPendingBytes += gpu.pendingKnownBytes;
    }
    return stats; // known bytes exclude actual driver allocation overhead
}

Prisma::MemoryStats Prisma::memoryStats() const {
    // A read-only, lower-bound estimate for editor diagnostics. No GPU
    // readback/flush. Cached render targets are owned by SIZE, not view ID.
    MemoryStats out;
    struct SizeSample {
        size_t textures = 0, unknown = 0;
        size_t graphLiveTextures = 0, graphPendingTextures = 0;
        uint64_t knownBytes = 0;
        uint64_t graphLiveBytes = 0, graphPendingBytes = 0;
    };
    std::map<std::pair<int,int>, SizeSample> cacheBySize;
    const auto addTexture = [this](gfx::TextureHandle h, uint64_t& bytes,
                                    size_t& count, size_t& unknown) {
        if (!h.valid()) return;
        ++count;
        gfx::TextureInfo info{};
        if (!dev_ || !dev_->gpuHealthy() ||
            !dev_->textureInfo(h,info) || info.width <= 0 || info.height <= 0) {
            ++unknown;
            return;
        }
        uint64_t bpp = 0;
        switch (info.format) {
            case gfx::PixelFormat::R8: bpp = 1; break;
            case gfx::PixelFormat::RGBA8:
            case gfx::PixelFormat::RGBA8_sRGB:
            case gfx::PixelFormat::Depth: bpp = 4; break;
            case gfx::PixelFormat::RGBA16F: bpp = 8; break;
            case gfx::PixelFormat::RGBA32F: bpp = 16; break;
            default: ++unknown; return; // compressed/mip formats not estimated
        }
        bytes += uint64_t(info.width) * uint64_t(info.height) * bpp;
    };
    for (const auto& size : sizes_) {
        SizeSample sample;
        for (const auto& [role, target] : size.targets)
            addTexture(target.tex, sample.knownBytes, sample.textures, sample.unknown);
        for (const auto& [key, depth] : size.depths)
            addTexture(depth, sample.knownBytes, sample.textures, sample.unknown);
        if (size.graphExecutor) {
            const auto gpu = size.graphExecutor->gpuMemoryStats();
            sample.graphLiveTextures = gpu.liveTextures;
            sample.graphPendingTextures = gpu.pendingTextures;
            sample.graphLiveBytes = gpu.liveKnownBytes;
            sample.graphPendingBytes = gpu.pendingKnownBytes;
            sample.knownBytes += gpu.liveKnownBytes;
            sample.textures += gpu.liveTextures;
            sample.unknown += gpu.unknownLiveTextures;
            out.graphPendingKnownBytes += gpu.pendingKnownBytes;
        }
        cacheBySize[{size.w,size.h}] = sample;
        ++out.activeSizeCaches;
        out.liveTextures += sample.textures;
        out.unknownTextures += sample.unknown;
        out.sharedSizeKnownBytes += sample.knownBytes;
        bool referenced = false;
        for (const auto& [id, dimensions] : viewDimensions_)
            if (dimensions.first == size.w && dimensions.second == size.h) {
                referenced = true;
                break;
            }
        if (!referenced) {
            ++out.unreferencedSizeCaches;
            out.unreferencedSizeKnownBytes += sample.knownBytes;
        }
    }
    for (const auto& [id, dimensions] : viewDimensions_) {
        ViewMemoryStats view;
        view.viewId = id;
        view.width = dimensions.first;
        view.height = dimensions.second;
        for (const auto& [otherId, otherDimensions] : viewDimensions_)
            if (otherDimensions == dimensions) ++view.sharedSizeViews;
        if (const auto cache = cacheBySize.find(dimensions);
            cache != cacheBySize.end()) {
            view.sharedTextures = cache->second.textures;
            view.sharedKnownBytes = cache->second.knownBytes;
            view.unknownTextures = cache->second.unknown;
            view.graphLiveKnownBytes = cache->second.graphLiveBytes;
            view.graphPendingKnownBytes = cache->second.graphPendingBytes;
            view.graphLiveTextures = cache->second.graphLiveTextures;
            view.graphPendingTextures = cache->second.graphPendingTextures;
        }
        if (const auto history = exposureViews_.find(id);
            history != exposureViews_.end()) {
            for (const auto& h : history->second.history) {
                size_t unknown = 0;
                addTexture(h, view.ownedHistoryKnownBytes,
                           view.ownedHistoryTextures, unknown);
                view.unknownTextures += unknown;
                out.unknownTextures += unknown;
            }
            out.liveTextures += view.ownedHistoryTextures;
            out.viewHistoryKnownBytes += view.ownedHistoryKnownBytes;
        }
        if (const auto history = temporalViews_.find(id);
            history != temporalViews_.end()) {
            for (const auto& h : history->second.history) {
                size_t unknown = 0;
                addTexture(h, view.ownedTemporalKnownBytes,
                           view.ownedTemporalTextures, unknown);
                view.unknownTextures += unknown;
                out.unknownTextures += unknown;
            }
            out.liveTextures += view.ownedTemporalTextures;
            out.temporalHistoryKnownBytes += view.ownedTemporalKnownBytes;
            out.viewHistoryKnownBytes += view.ownedTemporalKnownBytes;
        }
        out.views.push_back(view);
    }
    // Exposure history can be allocated before a view's successful scene
    // pass is recorded. Include it in totals exactly once, even if the view
    // has no currently attributable size cache.
    for (const auto& [id, history] : exposureViews_) {
        if (viewDimensions_.count(id)) continue;
        for (const auto& h : history.history)
            addTexture(h, out.viewHistoryKnownBytes,
                       out.liveTextures, out.unknownTextures);
    }
    // Count unattributed camera histories once without repeatedly querying
    // RHI metadata; these may be awaiting the next retireView() call.
    for (const auto& [id, history] : temporalViews_) {
        if (viewDimensions_.count(id)) continue;
        uint64_t bytes=0;
        for (const auto& h : history.history)
            addTexture(h, bytes, out.liveTextures, out.unknownTextures);
        out.temporalHistoryKnownBytes += bytes;
        out.viewHistoryKnownBytes += bytes;
    }
    addTexture(black_, out.otherLiveKnownBytes,
               out.liveTextures, out.unknownTextures);
    for (const auto& [path, lut] : luts_)
        addTexture(lut.tex, out.otherLiveKnownBytes,
                   out.liveTextures, out.unknownTextures);
    for (const auto& [path, tex] : graphTextures_)
        addTexture(tex, out.otherLiveKnownBytes,
                   out.liveTextures, out.unknownTextures);
    out.liveKnownBytes = out.sharedSizeKnownBytes +
                         out.viewHistoryKnownBytes + out.otherLiveKnownBytes;
    return out;
}

void Prisma::releaseRetiredSizesAfterGpuIdle() {
    if (!dev_) return;
    // Explicit host guarantee: GPU queue is idle (all render encoders and
    // submissions referencing the archived resources have completed).
    for (auto& item : retiredSizes_) destroySized(item.resources);
    retiredSizes_.clear();
    retiredTargets_.drainAfterGpuIdle(
        [this](gfx::TextureHandle h) { dev_->destroyTexture(h); });
    for (auto h : unfencedTargets_)
        if (h.valid()) dev_->destroyTexture(h);
    unfencedTargets_.clear();
}
void Prisma::shutdown() {
    if (!dev_) return;
    checkGpuDevice(*dev_);
    if (gpuDeviceLease_.device() && !dev_->waitForResourceIdle())
        throw std::runtime_error("Prisma shutdown: GPU retirement barrier failed");
    // Existing host teardown must synchronize the GPU before shutdown.
    // Unlike per-frame resize, shutdown is a full GPU idle safe point.
    fog_.shutdown(*dev_);fogSettings_={};fogScene_.clear();fogTime_=fogEpoch_=-1;
    sky_.shutdown(*dev_);skySettings_={};skyTime_=skyEpoch_=-1;clearPhysicalSkyAdvection();
    releaseRetiredSizesAfterGpuIdle();
    for (auto& s : sizes_) destroySized(s);
    sizes_.clear();
    for (auto& [id, view] : exposureViews_) {
        for (auto& history : view.history) release(history);
    }
    exposureViews_.clear();
    viewExposureFreeze_.clear();
    viewExposureMeterRegions_.clear();
    viewManualExposure_.clear();
    for (auto& [id, view] : temporalViews_)
        for (auto& h : view.history) release(h);
    temporalViews_.clear();
    adaptiveViews_.clear();
    viewQualityPolicies_.clear();
    viewHistoryAdmissionRejected_.clear();
    scopedGpuTimings_.clear();
    effectReports_.clear();
    adaptivePressure_.clear();
    externalRendererLiveKnownBytes_=externalRendererPendingKnownBytes_=0;
    externalGeometryPoolKnownBytes_=0;
    externalPresentationAssetKnownBytes_=0;
    motionViews_.clear();
    viewDimensions_.clear();
    requestedOutputSizes_.clear();
    for (auto& [p, l] : luts_) release(l.tex);
    luts_.clear();
    for(auto& [path,t]:graphTextures_)release(t);
    graphTextures_.clear();graphPaths_.clear();
    for (int i = 0; i < kProgCount; ++i) {
        if (pipes_[i].valid()) dev_->destroyPipeline(pipes_[i]);
        if (shaders_[i].valid()) dev_->destroyShader(shaders_[i]);
        pipes_[i] = {}; shaders_[i] = {}; failed_[i] = false;
    }
    for (auto& [m, sp] : composites_) { dev_->destroyPipeline(sp.second); dev_->destroyShader(sp.first); }
    composites_.clear();
    compositeFailed_.clear();
    if (triangle_.valid()) dev_->destroyBuffer(triangle_);
    release(black_);
    triangle_ = {};
    underwater_ = {};
    lensWaterViews_.clear();
    dev_ = nullptr;
}

void Prisma::setUnderwater(PrismaUnderwater target,float deltaSeconds) {
    underwater_=advancePrismaUnderwater(underwater_,target,deltaSeconds);
    const auto* effect=settings_.find(PrismaType::LensWater);
    if(!enabled_||!settings_.enabled||!effect||!effect->enabled||quality_<effect->minQuality||effect->get("amount")<=0){lensWaterViews_.erase(viewId_);return;}
    auto found=lensWaterViews_.find(viewId_);
    if(found==lensWaterViews_.end()) {
        if(!target.submerged||lensWaterViews_.size()>=64)return;
        found=lensWaterViews_.emplace(viewId_,PrismaLensWaterState{}).first;
    }
    found->second=advancePrismaLensWater(found->second,target.submerged,deltaSeconds,effect->get("dryTime"));
    if(found->second.wetness<=0)lensWaterViews_.erase(found);
}

size_t Prisma::pendingTargetRetirements() const {
    size_t count=retiredTargets_.pendingCount()+unfencedTargets_.size();
    for(const auto& entry:retiredSizes_)
        count+=entry.resources.targets.size()+entry.resources.depths.size();
    return count;
}

void Prisma::setImageLoader(ImageLoader fn) {
    if(dev_)checkGpuDevice(*dev_);
    // Project reload can happen while the previous frame's LUTs/graph
    // textures remain referenced by GPU commands. Fence-retire them rather
    // than destroying live borrowed inputs as soon as a new loader arrives.
    for (auto& [path,l] : luts_) retireTarget(l.tex);
    for (auto& [path,t] : graphTextures_) retireTarget(t);
    luts_.clear(); graphTextures_.clear();
    loader_ = std::move(fn);
}

void Prisma::configure(const Json& game, const Json& map, std::vector<std::string>* issues) {
    std::vector<std::string> local;
    setSettings(parsePrisma(game, map, issues ? issues : &local));
    fogSettings_={};fogScene_.clear();fogTime_=fogEpoch_=-1;
    skySettings_=parsePhysicalSky(game,map,issues?issues:&local);
    skyEpoch_=-1;skyTime_=-1;clearPhysicalSkyAdvection();
    if (!issues) for (auto& m : local) std::fprintf(stderr, "[postfx] %s\n", m.c_str());
}

void Prisma::setSettings(PrismaSettings s) {
    // Native/ECS callers may bypass parsePrisma. Normalize all authored
    // numeric/color/bool parameters against the GPU-free catalog before
    // they can be observed by any shader-building or render pass.
    sanitizePrismaSettings(s);
    // New map/project configuration invalidates adaptation across scenes.
    resetAutoExposure();
    resetMotionHistory();
    baseSettings_ = s;
    settings_ = std::move(s);
    effectReports_.clear(); // no stale per-view success from a previous profile
    error_.clear();
}

void Prisma::applyCameraVolumes(const PrismaVolumeStack& stack, float3 eye, uint32_t cameraMask) {
    settings_ = stack.resolve(baseSettings_, eye, cameraMask);
}

void Prisma::clearCameraVolumes() { settings_ = baseSettings_; }

void Prisma::setViewExposureFreeze(uint64_t id, bool freeze) {
    viewExposureFreeze_[id] = freeze;
}
void Prisma::clearViewExposureFreeze(uint64_t id) {
    viewExposureFreeze_.erase(id);
}
void Prisma::setViewExposureMeterRegion(uint64_t id, ExposureMeterRegion r) {
    const auto safe = [](float value,float lo,float hi,float fallback) {
        return std::isfinite(value) ? std::clamp(value,lo,hi) : fallback;
    };
    r.centerX = safe(r.centerX,0.0f,1.0f,0.5f);
    r.centerY = safe(r.centerY,0.0f,1.0f,0.5f);
    r.radius = safe(r.radius,0.01f,1.0f,0.25f);
    r.feather = safe(r.feather,0.001f,0.5f,0.10f);
    r.outsideWeight = safe(r.outsideWeight,0.02f,1.0f,0.05f);
    r.halfWidth = safe(r.halfWidth,0.01f,0.5f,0.25f);
    r.halfHeight = safe(r.halfHeight,0.01f,0.5f,0.25f);
    viewExposureMeterRegions_[id] = r;
}
void Prisma::clearViewExposureMeterRegion(uint64_t id) {
    viewExposureMeterRegions_.erase(id);
}
void Prisma::setViewManualExposure(uint64_t id, bool enabled, float ev) {
    // Clamp on ingress so NaNs and extreme scripted camera values never
    // enter the HDR pre-tone-map composite or destabilize the scene.
    viewManualExposure_[id] = {
        enabled, std::isfinite(ev) ? std::clamp(ev,-12.0f,12.0f) : 0.0f
    };
    if (enabled) resetAutoExposure(id); // force fresh meter on later auto resume
}
void Prisma::clearViewManualExposure(uint64_t id) {
    if (viewManualExposure_.erase(id)) resetAutoExposure(id);
}
void Prisma::setViewDepthOfField(uint64_t id, DepthOfFieldOverride d) {
    // Same bounds as the depthOfField catalog parameters.
    const auto safe = [](float v, float lo, float hi, float fallback) {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    d.focusDistance = safe(d.focusDistance, 0.05f, 2000.0f, 12.0f);
    d.focusRangeMeters = safe(d.focusRangeMeters, 0.01f, 500.0f, 4.0f);
    d.maxBlur = safe(d.maxBlur, 0.0f, 16.0f, 8.0f);
    viewDepthOfField_[id] = d;
}
void Prisma::clearViewDepthOfField(uint64_t id) { viewDepthOfField_.erase(id); }
bool Prisma::viewDepthOfField(uint64_t id, DepthOfFieldOverride& out) const {
    auto it = viewDepthOfField_.find(id);
    if (it == viewDepthOfField_.end()) return false;
    out = it->second;
    return true;
}

// Allocate everything that an output-size temporal resolve requires BEFORE
// jittering any world geometry. A failed allocation returns the host to the
// original full-resolution camera and disables subpixel jitter for this view.
// No GPU wait and no destruction of in-flight targets occurs here.
bool Prisma::preflightUpscale(int outputW,int outputH,int renderW,int renderH) {
    if (!dev_ || !dev_->gpuHealthy() ||
        !dev_->supports(gfx::PixelFormat::RGBA16F)) return false;
    if (!program(kTemporalResolve).valid() ||
        !program(kMotionVectors).valid() ||
        !composite(0).valid()) return false;
    auto& history=temporalViews_[viewId_];
    if (history.width!=outputW || history.height!=outputH ||
        history.inputWidth!=renderW || history.inputHeight!=renderH) {
        for (auto& handle:history.history) retireTarget(handle);
        history.width=outputW;history.height=outputH;
        history.inputWidth=renderW;history.inputHeight=renderH;
        history.current=0;history.valid=false;
    }
    for (auto& handle:history.history) {
        if (handle.valid()) continue;
        gfx::TextureDesc desc{};
        desc.width=outputW;desc.height=outputH;
        desc.format=gfx::PixelFormat::RGBA16F;
        desc.renderTarget=true;
        desc.filter=gfx::Filter::Nearest;
        desc.label="prisma temporal camera history";
        handle=dev_->createTexture(desc);
    }
    // Sized is keyed by internal scene resolution; its scratch depth for
    // output is separately keyed by output dimensions within this cache.
    Sized& cache=sized(renderW,renderH);
    const bool previousHdr=hdrFrame_;
    hdrFrame_=true;
    const auto color=target(cache,kScene,renderW,renderH).tex;
    const auto velocity=target(cache,kMotionVelocity,renderW,renderH).tex;
    hdrFrame_=previousHdr;
    const auto depth=depthFor(cache,renderW,renderH);
    const auto internalScratch=depthFor(cache,renderW,renderH,true);
    const auto outputScratch=depthFor(cache,outputW,outputH,true);
    return history.history[0].valid() && history.history[1].valid() &&
           color.valid() && velocity.valid() && depth.valid() &&
           internalScratch.valid() && outputScratch.valid();
}

void Prisma::acceptViewGpuTiming(uint64_t id,double gpuMs) {
    if (!std::isfinite(gpuMs) || gpuMs<=0.0 || gpuMs>250.0) return;
    auto& row=scopedGpuTimings_[id];
    row.viewId=id;
    row.lastGpuMs=gpuMs;
    row.gpuFenceAtRead=dev_?dev_->gpuCompletedFence():0;
    ++row.sampleSequence; // only a real completed scoped GPU query
}

const Prisma::ViewEffectReport* Prisma::effectReport(uint64_t id) const {
    const auto it = effectReports_.find(id);
    return it == effectReports_.end() ? nullptr : &it->second;
}

std::vector<uint64_t> Prisma::effectReportViewIds() const {
    std::vector<uint64_t> ids;
    ids.reserve(effectReports_.size());
    for (const auto& [id, snapshot] : effectReports_) {
        (void)snapshot;
        ids.push_back(id);
    }
    return ids;
}

std::vector<Prisma::ViewGpuTiming> Prisma::viewGpuTimings() const {
    std::vector<ViewGpuTiming> out;
    out.reserve(scopedGpuTimings_.size());
    for (const auto& [id, row] : scopedGpuTimings_) out.push_back(row);
    return out;
}

std::pair<int,int> Prisma::renderSizeForOutput(int width,int height) {
    width=std::max(1,width);
    height=std::max(1,height);
    requestedOutputSizes_[viewId_]={width,height};
    viewHistoryAdmissionRejected_.erase(viewId_);
    auto previous=temporalViews_.find(viewId_);
    if (previous!=temporalViews_.end()) previous->second.preflightRejected=false;
    if (width<64 || height<64 || !dev_ || !dev_->gpuHealthy() ||
        !enabled_ || !settings_.enabled ||
        quality_!=PrismaQuality::High || !hdrRequested()) {
        adaptiveViews_.erase(viewId_);
        adaptivePressure_.erase(viewId_);
        return {width,height};
    }
    const auto* effect=settings_.find(PrismaType::TemporalResolve);
    const char* temporal=std::getenv("AIVERSE_PRISMA_TEMPORAL");
    const char* upscale=std::getenv("AIVERSE_PRISMA_UPSCALE");
    const char* jitter=std::getenv("AIVERSE_PRISMA_JITTER");
    // History quotas must protect normal 1:1 temporal resolve too; Stage 42
    // can accumulate without jitter and without temporal upscaling.
    const bool temporalActive=effect && effect->enabled &&
        int(quality_)>=int(effect->minQuality) &&
        temporal && std::strcmp(temporal,"1")==0 &&
        effect->get("historyWeight")>0;
    if (!temporalActive) {
        adaptiveViews_.erase(viewId_);
        adaptivePressure_.erase(viewId_);
        return {width,height};
    }
    const auto policyIt=viewQualityPolicies_.find(viewId_);
    const ViewQualityPolicy policy=policyIt==viewQualityPolicies_.end()
        ? ViewQualityPolicy{} : policyIt->second;
    const uint64_t historyBytes=uint64_t(width)*uint64_t(height)*16u;
    if (policy.maxTemporalHistoryMiB &&
        historyBytes>uint64_t(policy.maxTemporalHistoryMiB)*1024u*1024u) {
        viewHistoryAdmissionRejected_[viewId_]=true;
        if (auto history=temporalViews_.find(viewId_);
            history!=temporalViews_.end()) {
            history->second.valid=false;
            history->second.preflightRejected=true;
            for (auto& handle:history->second.history) retireTarget(handle);
        }
        adaptiveViews_.erase(viewId_);
        adaptivePressure_.erase(viewId_);
        return {width,height};
    }
    const bool jitterRequested=jitter && std::strcmp(jitter,"1")==0 &&
        effect->get("jitterPixels")>0;
    const bool upscaleRequested=jitterRequested && upscale &&
        std::strcmp(upscale,"1")==0;
    // Scene jitter needs a history/velocity pipeline even when renderScale
    // is native 1:1. Preflight before Renderer3D::beginScene so a failed
    // allocation never produces an unresolvable jittered frame.
    if (!upscaleRequested) {
        adaptiveViews_.erase(viewId_);
        adaptivePressure_.erase(viewId_);
        if (jitterRequested) {
            auto& history=temporalViews_[viewId_];
            if (history.preflightCooldown) {
                --history.preflightCooldown;
                history.preflightRejected=true;
            } else if (!preflightUpscale(width,height,width,height)) {
                history.valid=false;
                history.preflightRejected=true;
                history.preflightCooldown=60;
            }
        }
        return {width,height};
    }
    const float ceiling=PrismaAdaptiveResolution::quantize(
        std::clamp(effect->get("renderScale"),0.5f,1.0f));
    float scale=ceiling;
    const char* adaptiveFlag=std::getenv("AIVERSE_PRISMA_ADAPTIVE");
    const bool adaptiveRequested=adaptiveFlag &&
        std::strcmp(adaptiveFlag,"1")==0 &&
        effect->get("adaptiveResolution")>0.5f;
    if (adaptiveRequested) {
        auto& adaptive=adaptiveViews_[viewId_];
        // Only what Prisma can actually enumerate counts toward this
        // threshold: once-counted resident caches, per-camera histories,
        // and pending fence-retired resources. Not total physical VRAM.
        const auto saturatingAdd=[](uint64_t a,uint64_t b) {
            constexpr uint64_t maximum=std::numeric_limits<uint64_t>::max();
            return a>maximum-b?maximum:a+b;
        };
        const uint64_t liveBytes=saturatingAdd(
            saturatingAdd(
                saturatingAdd(memoryStats().liveKnownBytes,
                              externalRendererLiveKnownBytes_),
                externalGeometryPoolKnownBytes_),
            externalPresentationAssetKnownBytes_);
        const uint64_t pendingBytes=saturatingAdd(
            retirementStats().knownPendingBytes,externalRendererPendingKnownBytes_);
        const uint64_t knownBytes=saturatingAdd(liveBytes,pendingBytes);
        const uint64_t memoryLimit=uint64_t(
            std::clamp(effect->get("maxPrismaMemoryMiB"),32.0f,4096.0f))
            * uint64_t(1024*1024);
        const bool memoryPressure=knownBytes>memoryLimit;
        // Prefer completed, bounded Metal-counter/GL33 viewport GPU
        // queries. Where scoped GPU samples are unavailable or stale, the
        // backend whole-frame timer remains a distinct fallback, NEVER
        // mislabeled as per-view. No CPU/FPS timing is synthesized.
        const auto scoped=scopedGpuTimings_.find(viewId_);
        const uint64_t completedFence=dev_->gpuCompletedFence();
        const bool scopedValid=scoped!=scopedGpuTimings_.end() &&
            scoped->second.sampleSequence>0 &&
            // Stale/closed tab GPU samples must not prevent a later
            // whole-frame fallback; no extrapolation from old queries.
            (completedFence==0 || scoped->second.gpuFenceAtRead==0 ||
             (completedFence>=scoped->second.gpuFenceAtRead &&
              completedFence-scoped->second.gpuFenceAtRead<=8));
        const double gpuMs=scopedValid ? scoped->second.lastGpuMs :
            dev_->gpuFrameMs();
        const uint64_t sampleToken=scopedValid ?
            scoped->second.sampleSequence : completedFence;
        adaptive.observe(gpuMs,sampleToken,
            effect->get("targetGpuMs"),effect->get("minRenderScale"),
            ceiling,memoryPressure,scopedValid,policy.priority);
        adaptivePressure_[viewId_]={
            knownBytes,memoryLimit,memoryPressure,scopedValid,gpuMs};
        scale=adaptive.scale;
    } else {
        adaptiveViews_.erase(viewId_);
        adaptivePressure_.erase(viewId_);
    }
    if (scale>=0.999f) {
        // Adaptive may recover to 1:1 but the camera still jitters.
        // Guarantee output history and motion targets before beginScene.
        auto& history=temporalViews_[viewId_];
        if (history.preflightCooldown) {
            --history.preflightCooldown;
            history.preflightRejected=true;
        } else if (!preflightUpscale(width,height,width,height)) {
            history.valid=false;
            history.preflightRejected=true;
            history.preflightCooldown=60;
        }
        return {width,height};
    }
    const int renderW=std::clamp(int(std::lround(double(width)*scale)),32,width);
    const int renderH=std::clamp(int(std::lround(double(height)*scale)),32,height);
    auto& history=temporalViews_[viewId_];
    // Avoid hammering unsupported VRAM allocation every frame. A successful
    // preflight clears the cooldown immediately.
    if (history.preflightCooldown>0) {
        --history.preflightCooldown;
        history.preflightRejected=true;
        return {width,height};
    }
    if (!preflightUpscale(width,height,renderW,renderH)) {
        history.preflightRejected=true;
        history.valid=false;
        history.preflightCooldown=60;
        if (error_.empty()) error_="PRISMA upscale preflight: missing GPU targets; using full-resolution unjittered fallback";
        return {width,height};
    }
    history.preflightRejected=false;
    history.preflightCooldown=0;
    return {renderW,renderH};
}

// Stage 43: projection-only Halton jitter. Keep the caller's unjittered camera
// for selection, UI and gameplay. History tracks per-view jitter in stable
// unflipped UV coordinates; this helper performs no GPU work.
RTSCamera Prisma::jitteredCamera(RTSCamera camera, int width, int height) {
    if (width <= 0 || height <= 0) return camera;
    const auto* effect = settings_.find(PrismaType::TemporalResolve);
    const char* temporal = std::getenv("AIVERSE_PRISMA_TEMPORAL");
    const char* jitter = std::getenv("AIVERSE_PRISMA_JITTER");
    bool eligible = dev_ && dev_->gpuHealthy() &&
        enabled_ && settings_.enabled &&
        quality_ == PrismaQuality::High && hdrRequested() &&
        temporal && std::strcmp(temporal,"1") == 0 &&
        jitter && std::strcmp(jitter,"1") == 0 &&
        effect && effect->enabled && int(quality_) >= int(effect->minQuality) &&
        effect->get("historyWeight") > 0 && effect->get("jitterPixels") > 0 &&
        camera.jitter.x == 0 && camera.jitter.y == 0 &&
        viewHistoryAdmissionRejected_.count(viewId_)==0 &&
        (temporalViews_.find(viewId_)==temporalViews_.end() ||
         !temporalViews_.at(viewId_).preflightRejected);
    // Shader compilation can fail on a backend. Never jitter the scene
    // if there is no temporal reconstruction shader to undo it.
    if (eligible && !program(kTemporalResolve).valid()) eligible=false;
    if (!eligible) {
        if (auto it=temporalViews_.find(viewId_); it!=temporalViews_.end()) {
            if (it->second.jitterActive) it->second.valid=false;
            it->second.currentJitterUv={};
            it->second.jitterActive=false;
        }
        return camera;
    }
    auto& state = temporalViews_[viewId_];
    const float amplitude = std::clamp(effect->get("jitterPixels"),0.0f,0.5f);
    // Projection discontinuities invalidate both color and object-velocity
    // history, even when camera world position itself has not moved.
    const bool projectionChanged = state.projectionKnown &&
        (state.previousOrtho != camera.ortho ||
         std::abs(state.previousFovY-camera.fovY)>0.002f ||
         std::abs(state.previousNear-camera.zNear)>0.0001f ||
         std::abs(state.previousFar-camera.zFar)>0.01f);
    if (projectionChanged) resetMotionHistory(viewId_);
    if (projectionChanged || !state.jitterActive ||
        std::abs(state.lastJitterPixels-amplitude)>1e-6f) {
        state.valid=false;
        state.jitterSequence=0;
        state.previousJitterUv={};
    }
    state.projectionKnown=true;
    state.previousOrtho=camera.ortho;
    state.previousFovY=camera.fovY;
    state.previousNear=camera.zNear;
    state.previousFar=camera.zFar;
    const auto halton=[](uint32_t index,uint32_t base) {
        float result=0.0f, factor=1.0f;
        while (index>0) { factor/=float(base); result+=factor*float(index%base); index/=base; }
        return result;
    };
    const uint32_t index=uint32_t((state.jitterSequence++ % 8u)+1u);
    const float pixelX=(halton(index,2)-0.5f)*2.0f*amplitude;
    const float pixelY=(halton(index,3)-0.5f)*2.0f*amplitude;
    camera.jitter={2.0f*pixelX/float(width),2.0f*pixelY/float(height)};
    state.currentJitterUv={camera.jitter.x*0.5f,camera.jitter.y*0.5f};
    state.jitterActive=true;
    state.lastJitterPixels=amplitude;
    return camera;
}

bool Prisma::wantsObjectMotionVectors() const {
    if (!enabled_ || !settings_.enabled || !hdrFrame_) return false;
    const auto activeEffect = [this](PrismaType type, const char* flag) {
        const PrismaEffect* effect = settings_.find(type);
        const char* env = std::getenv(flag);
        return effect && effect->enabled &&
            int(quality_) >= int(effect->minQuality) &&
            env && std::strcmp(env, "1") == 0;
    };
    const bool blur = activeEffect(PrismaType::MotionBlur, "AIVERSE_PRISMA_MOTION_BLUR") &&
        settings_.find(PrismaType::MotionBlur)->get("intensity") > 0 &&
        settings_.find(PrismaType::MotionBlur)->get("maxPixels") > 0;
    const bool temporal = activeEffect(PrismaType::TemporalResolve, "AIVERSE_PRISMA_TEMPORAL") &&
        settings_.find(PrismaType::TemporalResolve)->get("historyWeight") > 0;
    return blur || temporal;
}
uint64_t Prisma::objectMotionEpoch(uint64_t id) const {
    const auto it = motionViews_.find(id);
    return it == motionViews_.end() ? 1 : it->second.cutEpoch;
}

bool Prisma::objectMotionContext(uint64_t& id, mat4& previous) const {
    id = viewId_;
    if (!wantsObjectMotionVectors()) return false;
    const auto found = motionViews_.find(viewId_);
    if (found == motionViews_.end() || !found->second.valid) return false;
    previous = found->second.previousViewProj;
    return true;
}
void Prisma::acceptObjectMotionVectors(gfx::TextureHandle texture) {
    setObjectMotionVectors(viewId_, texture);
}
void Prisma::acceptObjectMotionVectors(MotionTextureInput input) {
    setObjectMotionVectors(viewId_, input);
}
void Prisma::setObjectMotionVectors(uint64_t id, gfx::TextureHandle texture) {
    motionViews_[id].objectVelocity = MotionTextureInput::legacy(texture);
}
void Prisma::setObjectMotionVectors(uint64_t id, MotionTextureInput input) {
    motionViews_[id].objectVelocity = input;
}
void Prisma::setMotionReactiveMask(uint64_t id, gfx::TextureHandle texture) {
    motionViews_[id].reactiveMask = MotionTextureInput::legacy(texture);
}
void Prisma::setMotionReactiveMask(uint64_t id, MotionTextureInput input) {
    motionViews_[id].reactiveMask = input;
}
void Prisma::acceptMotionReactiveMask(gfx::TextureHandle texture) {
    setMotionReactiveMask(viewId_, texture);
}
void Prisma::acceptMotionReactiveMask(MotionTextureInput input) {
    setMotionReactiveMask(viewId_, input);
}
void Prisma::resetMotionHistory(uint64_t id) {
    // Epoch is created even if the camera has not rendered before; this
    // prevents a renderer's prior transform history from surviving a cut.
    auto& view = motionViews_[id];
    view.valid = false;
    ++view.cutEpoch;
    view.objectVelocity = {};
    view.reactiveMask = {};
    if (auto it = temporalViews_.find(id); it != temporalViews_.end())
        it->second.valid = false;
    // A host-reported camera cut invalidates exposure as well as velocity:
    // preserve its allocated 1x1 history but do not reuse old luminance.
    resetAutoExposure(id);
}
void Prisma::resetMotionHistory() {
    for (auto& [id, view] : motionViews_) {
        view.valid = false;
        ++view.cutEpoch;
        view.objectVelocity = {};
        view.reactiveMask = {};
    }
    for (auto& [id, view] : temporalViews_) view.valid = false;
    resetAutoExposure(); // all-camera cut/profile transition
}

void Prisma::resetAutoExposure() {
    for (auto& [id, view] : exposureViews_) {
        view.valid = false;
        view.time = 0;
    }
}
void Prisma::resetAutoExposure(uint64_t id) {
    auto it = exposureViews_.find(id);
    if (it != exposureViews_.end()) {
        it->second.valid = false;
        it->second.time = 0;
    }
}

Prisma::RuntimeCapabilities Prisma::runtimeCapabilities() const {
    RuntimeCapabilities out;
    out.deviceHealthy=dev_ && dev_->gpuHealthy() && enabled_ && settings_.enabled;
    out.hdrFormatSupported=dev_ && dev_->supports(gfx::PixelFormat::RGBA16F);
    out.hdrAuthoredRequested=hdrModeWithEnv(settings_.hdr)!=HdrMode::Off;
    out.hdrRequested=hdrRequested();
    out.hdrEffectiveLastFrame=hdrFrame_ && stats_.width>0 && sceneReady_;
    const auto* effect=settings_.find(PrismaType::TemporalResolve);
    const char* temporal=std::getenv("AIVERSE_PRISMA_TEMPORAL");
    const char* jitter=std::getenv("AIVERSE_PRISMA_JITTER");
    const char* upscale=std::getenv("AIVERSE_PRISMA_UPSCALE");
    const char* adaptive=std::getenv("AIVERSE_PRISMA_ADAPTIVE");
    out.temporalRequested=effect && effect->enabled &&
        temporal && std::strcmp(temporal,"1")==0 &&
        effect->get("historyWeight")>0;
    out.temporalPipelinePrepared=pipes_[kTemporalResolve].valid();
    out.velocityPipelinePrepared=pipes_[kMotionVectors].valid();
    out.temporalResolvedLastFrame=stats_.temporalResolved;
    out.jitterRequested=out.temporalRequested && jitter &&
        std::strcmp(jitter,"1")==0 && effect->get("jitterPixels")>0;
    out.jitterAppliedLastFrame=stats_.temporalJittered;
    out.upscaleRequested=out.jitterRequested && upscale &&
        std::strcmp(upscale,"1")==0 &&
        effect->get("renderScale")<0.999f;
    out.upscaleAppliedLastFrame=stats_.temporalUpscaled;
    out.adaptiveRequested=out.upscaleRequested && adaptive &&
        std::strcmp(adaptive,"1")==0 &&
        effect->get("adaptiveResolution")>0.5f;
    if (const auto sample=scopedGpuTimings_.find(viewId_);
        sample!=scopedGpuTimings_.end() && sample->second.sampleSequence) {
        const uint64_t completed=dev_?dev_->gpuCompletedFence():0;
        out.scopedGpuSampleAvailable=completed==0 ||
            sample->second.gpuFenceAtRead==0 ||
            (completed>=sample->second.gpuFenceAtRead &&
             completed-sample->second.gpuFenceAtRead<=8);
    }
    out.reactiveLastFrame=stats_.reactiveUsed;
    if (!dev_) out.fallbackReason="GPU device absent";
    else if (!dev_->gpuHealthy()) out.fallbackReason="GPU device lost";
    else if (!enabled_ || !settings_.enabled) out.fallbackReason="Prisma disabled";
    else if (!out.hdrFormatSupported) out.fallbackReason="RGBA16F unavailable";
    else if (hdrFailed_)
        out.fallbackReason=hdrFailedError_.empty() ?
            std::string("HDR target allocation failed") :
            "HDR target allocation failed: "+hdrFailedError_;
    else if (!out.hdrRequested) out.fallbackReason="HDR disabled or tier insufficient";
    else if (out.temporalRequested && quality_!=PrismaQuality::High)
        out.fallbackReason="Temporal HDR requires High quality";
    else if (out.temporalRequested && !out.temporalPipelinePrepared)
        out.fallbackReason="Temporal Resolve GPU pipeline unavailable";
    else if (out.jitterRequested && !out.velocityPipelinePrepared)
        out.fallbackReason="Motion-vector GPU pipeline unavailable";
    else if (viewHistoryAdmissionRejected_.count(viewId_))
        out.fallbackReason="Temporal history admission limit exceeded";
    else if (auto history=temporalViews_.find(viewId_);
             history!=temporalViews_.end() && history->second.preflightRejected)
        out.fallbackReason="Temporal target preflight failed";
    else if (!error_.empty()) out.fallbackReason=error_;
    return out;
}

bool Prisma::canonicalTemporalRequested() const {
    const auto report=runtimeCapabilities();
    // Ownership is selected by authored intent, not by the success of a
    // lazily compiled shader. Even if the canonical shader is unavailable,
    // starting legacy TAA would create a competing history unexpectedly.
    return report.deviceHealthy && report.hdrRequested &&
        quality_==PrismaQuality::High && report.temporalRequested;
}

bool Prisma::hdrRequested() const {
    if (!dev_ || hdrFailed_ || !enabled_ || !settings_.enabled || quality_ <= PrismaQuality::Low || !dev_->supports(gfx::PixelFormat::RGBA16F)) return false;
    return hdrAtQuality(hdrModeWithEnv(settings_.hdr), quality_, dev_->supports(gfx::PixelFormat::RGBA16F));
}

HdrMode Prisma::hdrModeWithEnv(HdrMode authored) {
    if (const char* e = std::getenv("AIVERSE_HDR")) {
        const std::string v = e;
        if (v == "off" || v == "0") return HdrMode::Off;
        if (v == "on" || v == "1") return HdrMode::On;
        if (v == "auto") return HdrMode::Auto;
    }
    return authored;
}

bool Prisma::active() const {
    return dev_ && triangle_.valid() && enabled_ && settings_.enabled &&
        quality_!=PrismaQuality::Off &&
        (hdrRequested() || settings_.anyActive(quality_) || underwater_.blend>0.001f);
}

Prisma::Sized& Prisma::sized(int w, int h) {
    ++frame_; // ONLY LRU ordering, NEVER a proxy for GPU completion
    if (dev_) collectCompletedSizes(dev_->gpuCompletedFence());
    for (auto& s : sizes_)
        if (s.w == w && s.h == h) {
            s.lastUse = frame_;
            return s;
        }
    if (sizes_.size() >= 3) {
        auto it = std::min_element(sizes_.begin(), sizes_.end(),
            [](const Sized& a, const Sized& b) {
                return a.lastUse < b.lastUse;
            });
        // Save the complete viewport context, including its graph pool.
        // A fence for the CURRENT GPU frame safely covers this older view's
        // last use on the same graphics queue; no token means quarantine.
        RetiredSized retired;
        retired.resources = std::move(*it);
        retired.lastSubmittedFence = dev_ ? dev_->gpuSubmissionFence() : 0;
        retiredSizes_.push_back(std::move(retired));
        sizes_.erase(it);
    }
    sizes_.reserve(3);
    sizes_.emplace_back();
    Sized& s = sizes_.back();
    s.w = w; s.h = h; s.lastUse = frame_;
    return s;
}

Prisma::Target& Prisma::target(Sized& s, int role, int w, int h) {
    w = std::max(w, 1); h = std::max(h, 1);
    Target& t = s.targets[role];
    const auto format = hdrFrame_ ? gfx::PixelFormat::RGBA16F : gfx::PixelFormat::RGBA8;
    if (t.tex.valid() && t.w == w && t.h == h && t.format == format) return t;
    retireTarget(t.tex); // HDR/size switches can overlap previous GPU frames
    gfx::TextureDesc td;
    td.width = w; td.height = h;
    td.renderTarget = true;
    td.filter = role == kMotionVelocity ? gfx::Filter::Nearest : gfx::Filter::Linear;
    td.label = "postfx target";
    td.format = format; t.format = format;
    t.tex = dev_->createTexture(td);
    t.w = w; t.h = h;
    return t;
}

gfx::TextureHandle Prisma::depthFor(Sized& s, int w, int h, bool scratch) {
    const uint64_t key = (uint64_t(uint32_t(w)) << 32) | uint32_t(h) |
                         (scratch ? (uint64_t(1) << 63) : 0);
    auto it = s.depths.find(key);
    if (it != s.depths.end()) return it->second;
    gfx::TextureDesc td;
    td.width = w; td.height = h;
    td.format = gfx::PixelFormat::Depth;
    td.renderTarget = true;
    td.filter = gfx::Filter::Nearest;
    td.label = "postfx depth";
    return s.depths[key] = dev_->createTexture(td);
}

gfx::PipelineHandle Prisma::program(Prog p) {
    if (pipes_[p].valid() || failed_[p]) return pipes_[p];
    struct Def { const char* body; const char* helpers; uint32_t tex; const char* label; };
    static const Def defs[kProgCount] = {
        {prisma::kPrefilter, nullptr, 1, "postfx prefilter"},
        {prisma::kDown, nullptr, 1, "postfx down"},
        {prisma::kUp, nullptr, 3, "postfx up"},
        {prisma::kBlur, nullptr, 1, "postfx blur"},
        {prisma::kShaftsPre, nullptr, 1, "postfx shafts mask"},
        {prisma::kShafts, nullptr, 1, "postfx shafts"},
        {prisma::kFxaa, prisma::kFxaaHelpers, 1, "postfx fxaa"},
        {prisma::kDepthOfField, nullptr, 3, "prisma depth of field"},
        {prisma::kExposureLog, nullptr, 1, "prisma exposure luminance"},
        {prisma::kExposureReduce, nullptr, 1, "prisma exposure reduce"},
        {prisma::kExposureAdapt, nullptr, 3, "prisma exposure adapt"},
        {prisma::kMotionVectors, nullptr, 7, "prisma camera motion vectors"},
        {prisma::kMotionBlur, prisma::kMotionBlurHelpers, 15, "prisma shutter motion blur"},
        {prisma::kTemporalResolve, prisma::kTemporalResolveHelpers, 31, "prisma HDR temporal resolve"},
    };
    const Def& d = defs[p];
    const uint32_t sampledDepth = p == kMotionVectors || p == kDepthOfField
        ? (1u << 1) : (p == kMotionBlur ? (1u << 2) :
            (p == kTemporalResolve ? (1u << 3) : 0u));
    Built b = p == kTemporalResolve
        ? buildProgram(kTemporalFields, 13, d.tex, "", d.helpers, d.body, sampledDepth)
        : p == kMotionVectors
            ? buildProgram(kMotionFields, 11, d.tex, "", d.helpers, d.body, sampledDepth)
        : p == kMotionBlur
            ? buildProgram(kMotionBlurFields, 5, d.tex, "", d.helpers, d.body, sampledDepth)
            : p == kExposureLog
            ? buildProgram(kExposureMeterFields, 5, d.tex, "", d.helpers, d.body, sampledDepth)
            : p == kDepthOfField
            ? buildProgram(kDepthOfFieldFields, 4, d.tex, "",
                prisma::kDepthOfFieldHelpers, d.body, sampledDepth)
            : buildProgram(kSimpleFields, 3, d.tex, "", d.helpers, d.body, sampledDepth);
    gfx::ShaderDesc sd;
    sd.glslVertex = b.vs.c_str(); sd.glslFragment = b.fs.c_str(); sd.msl = b.msl.c_str();
    sd.textureSlots = p == kTemporalResolve ? 5 :
        (p == kMotionBlur ? 4 : (p == kMotionVectors ? 3 : 2));
    sd.uniformSlots = 1; sd.label = d.label;
    shaders_[p] = dev_->createShader(sd);
    if (!shaders_[p].valid()) {
        failed_[p] = true;
        error_ = std::string(d.label) + ": " + dev_->lastError();
        std::fprintf(stderr, "[postfx] %s\n", error_.c_str());
        return {};
    }
    gfx::PipelineDesc pd;
    pd.shader = shaders_[p];
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 8;
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.depthTest = false; pd.depthWrite = false;
    pd.label = d.label;
    pipes_[p] = dev_->createPipeline(pd);
    if (!pipes_[p].valid()) failed_[p] = true;
    return pipes_[p];
}

gfx::PipelineHandle Prisma::composite(uint32_t mask, const Json* graphData,
                                      const std::string* graphKey) {
    const std::string graphText = graphData ?
        (graphKey ? *graphKey : graphData->dump(-1)) : std::string();
    const std::string key = std::to_string(mask) + graphText;
    if (auto it = composites_.find(key); it != composites_.end()) return it->second.second;
    if (compositeFailed_.count(key)) return {};
    std::string defines;
    for (size_t i = 0; i < sizeof(kPfNames)/sizeof(kPfNames[0]); ++i) if (mask & (1u << i)) defines += std::string("#define ") + kPfNames[i] + " 1\n";
    uint32_t tex = 1;
    if (mask & kPfUnderwaterDepth) {
        tex |= 1u << 5;
        defines += "#define PF_DEPTH_SAMPLE(p) TEX5(p).r\n";
    }
    if (mask & kPfBloom) tex |= 2;
    if (mask & kPfTilt) tex |= 4;
    if (mask & kPfShafts) tex |= 8;
    if (mask & kPfLut) tex |= 16;
    std::string body = prisma::kComposite;
    graph::Program graphProgram;
    if (graphData) {
        try {
            graphProgram=graph::compile(*graphData,"postfx");
            graphPaths_[graphText]=graphProgram.textures;
            const std::string marker="    RETURN(vec4(clamp(col, vec3(0.0), vec3(1.0)), 1.0));";
            std::string macros="\n#define SG_SAMPLE(uv) TEX0(TUV(uv))\n";
            for(size_t i=0;i<graphProgram.textures.size();++i) {
                const auto slot=std::to_string(5+i);tex|=1u<<(5+i);
                macros+="#define SG_TEXTURE"+std::to_string(i)+"(uv) TEX"+slot+"(uv)\n";
            }
            body.replace(body.find(marker),marker.size(),macros+"vec2 sg_uv=s,sg_screen=s;vec4 sg_input=vec4(col,1.0);float sg_time=graphFrame.x;\n// SG_CODE\nRETURN("+graphProgram.outputs.at("color")+");\n");
        }catch(const std::exception& ex){error_=ex.what();compositeFailed_[key]=true;return {};}
    }
    const auto layout = PrismaCompositeTextureLayout::plan(
        graphProgram.textures.size(), (mask & kPfAutoExposure) != 0);
    if (!layout.valid) {
        error_ = "postfx composite: Shader Graph exceeds four independent textures";
        compositeFailed_[key] = true;
        return {};
    }
    if (mask & kPfAutoExposure) {
        tex |= (1u << layout.exposureSlot);
        if (layout.exposureSlot == 9)
            defines += "#define PF_AUTO_EXPOSURE_SLOT9 1\n";
    }
    Built b=buildProgram(kCompositeFields,int(sizeof(kCompositeFields)/sizeof(kCompositeFields[0])),tex,defines,prisma::kCompositeHelpers,body.c_str(),
        (mask & kPfUnderwaterDepth) ? (1u << 5) : 0);
    if(graphData) {
        auto at=b.fs.find("// SG_CODE");b.fs.replace(at,10,graphProgram.glsl);
        at=b.msl.find("// SG_CODE");b.msl.replace(at,10,graphProgram.msl);
    }
    gfx::ShaderDesc sd;
    sd.glslVertex = b.vs.c_str(); sd.glslFragment = b.fs.c_str(); sd.msl = b.msl.c_str();
    sd.textureSlots = std::max(layout.textureSlots, (mask & kPfUnderwaterDepth) ? 6 : 0);
    sd.uniformSlots = 1; sd.label = "postfx composite";
    gfx::ShaderHandle sh = dev_->createShader(sd);
    if (!sh.valid()) {
        compositeFailed_[key] = true;
        error_ = "postfx composite: " + dev_->lastError();
        std::fprintf(stderr, "[postfx] %s\n", error_.c_str());
        return {};
    }
    gfx::PipelineDesc pd;
    pd.shader = sh;
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 8;
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.depthTest = false; pd.depthWrite = false;
    pd.label = "postfx composite";
    gfx::PipelineHandle p = dev_->createPipeline(pd);
    if (!p.valid()) { dev_->destroyShader(sh); compositeFailed_[key] = true; return {}; }
    composites_[key] = {sh, p};
    return p;
}

const Prisma::Lut* Prisma::lut(const std::string& path) {
    auto it = luts_.find(path);
    if (it != luts_.end()) return it->second.failed ? nullptr : &it->second;
    Lut& l = luts_[path];
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!loader_ || !loader_(path, w, h, rgba)) {
        l.failed = true;
        error_ = "postfx LUT: cannot read image '" + path + "'";
        std::fprintf(stderr, "[postfx] %s\n", error_.c_str());
        return nullptr;
    }
    if (h < 2 || w != h * h || rgba.size() < size_t(w) * size_t(h) * 4) {
        l.failed = true;
        error_ = "postfx LUT: invalid strip dimensions for '" + path +
                 "' (expected N^2 x N, e.g. 256x16)";
        std::fprintf(stderr, "[postfx] %s (got %dx%d)\n", error_.c_str(), w, h);
        return nullptr;
    }
    gfx::TextureDesc td;
    td.width = w; td.height = h; td.data = rgba.data();
    td.filter = gfx::Filter::Linear;
    td.label = "postfx lut";
    l.tex = dev_->createTexture(td);
    l.n = h;
    if (!l.tex.valid()) {
        l.failed = true;
        error_ = "postfx LUT: GPU upload failed for '" + path + "': " +
                 dev_->lastError();
        return nullptr;
    }
    return &l;
}

gfx::TextureHandle Prisma::graphTexture(const std::string& path) {
    auto it=graphTextures_.find(path);if(it!=graphTextures_.end()){if(it->second.valid())return it->second;error_="shader graph: cannot load texture '"+path+"'";return black_;}
    int w=0,h=0;std::vector<uint8_t> bytes;
    if(!loader_ || !loader_(path,w,h,bytes) || w<=0 || h<=0 || bytes.size()<size_t(w)*size_t(h)*4) {
        error_="shader graph: cannot load texture '"+path+"'";graphTextures_[path]={};return black_;
    }
    gfx::TextureDesc td;td.width=w;td.height=h;td.data=bytes.data();td.filter=gfx::Filter::Linear;
    auto texture=dev_->createTexture(td);
    if (!texture.valid()) {
        // Negative-cache failed GPU uploads just like failed image
        // decodes. Retrying them on every frame would stall the
        // render loop; setImageLoader() clears this cache explicitly.
        graphTextures_[path] = {};
        error_ = "shader graph: GPU texture upload failed for '" + path +
                 "': " + dev_->lastError();
        return black_;
    }
    graphTextures_[path]=texture;return texture;
}

gfx::PassDesc Prisma::scenePass(const gfx::PassDesc& out, int w, int h) {
    if(dev_)checkGpuDevice(*dev_);
    // A failed HDR allocation is not retried every frame for the same
    // size, but a new size gets a fresh attempt (quality changes and
    // init() forget all failures).
    hdrFailed_ = std::find(hdrFailedSizes_.begin(), hdrFailedSizes_.end(),
                           std::pair<int,int>{w, h}) != hdrFailedSizes_.end();
    hdrFrame_ = hdrRequested();
    Sized& s = sized(w, h);
    gfx::PassDesc p = out;
    p.colorTarget = target(s, kScene, w, h).tex;
    p.depthTarget = depthFor(s, w, h);
    sceneDepthForMotion_ = p.depthTarget;
    sceneReady_ = p.colorTarget.valid() && p.depthTarget.valid();
    if (!sceneReady_ && hdrFrame_) {
        hdrFailed_ = true; hdrFrame_ = false;
        if (hdrFailedSizes_.size() >= 16) hdrFailedSizes_.erase(hdrFailedSizes_.begin());
        hdrFailedSizes_.emplace_back(w, h);
        hdrFailedError_ = dev_ ? dev_->lastError() : std::string{};
        std::fprintf(stderr, "[postfx] HDR allocation failed; falling back to RGBA8: %s\n", hdrFailedError_.c_str());
        return scenePass(out, w, h);
    }
    if (!sceneReady_) return out;
    // Track the last successfully allocated post scene pass for this view.
    // Several view IDs may point to the same size-owned render-target cache.
    viewDimensions_[viewId_] = {w, h};
    if (hdrFrame_) for (int i = 0; i < 3; ++i) {
        const float c = std::max(p.clearColor[i], 0.0f);
        p.clearColor[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    p.clearColorEnabled = true;     // the composite covers every pixel; clearing is cheaper than loading on tilers
    p.clearDepthEnabled = true;
    return p;
}

void Prisma::draw(Sized& s, const Target& dst, gfx::PipelineHandle pipe, const void* u, size_t size,
                  std::initializer_list<std::pair<int, gfx::TextureHandle>> tex) {
    if (!dst.tex.valid() || !pipe.valid()) return; // never redirect a failed effect to the screen
    gfx::PassDesc p;
    p.colorTarget = dst.tex;
    p.depthTarget = depthFor(s, dst.w, dst.h, true);
    p.clearColor[0] = p.clearColor[1] = p.clearColor[2] = 0; p.clearColor[3] = 1;
    dev_->beginPass(p);
    dev_->setViewport({0, 0, dst.w, dst.h});
    dev_->setPipeline(pipe);
    dev_->setVertexBuffer(triangle_);
    dev_->setUniforms(0, u, size);
    for (auto& [slot, t] : tex) dev_->setTexture(slot, t);
    dev_->draw(3);
    dev_->endPass();
    ++stats_.passes;
}

void Prisma::resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& f) {
    checkGpuDevice(dev);
    stats_ = {};
    auto& effectReport = effectReports_[viewId_];
    ++effectReport.sequence;
    effectReport.viewId = viewId_;
    effectReport.quality = quality_;
    effectReport.hdr = hdrFrame_;
    effectReport.sceneReady = sceneReady_;
    effectReport.width = f.width;
    effectReport.height = f.height;
    effectReport.shaderGraphTextures = 0;
    effectReport.shaderGraphFallback = false;
    effectReport.autoExposureTextureSlot = -1;
    effectReport.autoExposureHighlightRejection = 0;
    effectReport.autoExposureFrozen = false;
    effectReport.autoExposureRegionMetered = false;
    effectReport.manualExposureEncoded = false;
    effectReport.manualExposureEV = 0.0f;
    effectReport.autoExposureBoxMetered = false;
    effectReport.reactiveInputSupplied = false;
    effectReport.reactiveInputAccepted = false;
    effectReport.objectVelocityInputAccepted = false;
    effectReport.reactiveWithoutObjectVelocity = false;
    effectReport.motionBlurReactiveEdgesEnabled = false;
    effectReport.effects.clear();
    if (!sceneReady_) {
        for (const auto& e : settings_.effects) {
            EffectExecution state{};
            state.type = e.type;
            state.authored = true;
            state.reason = "scene render targets unavailable";
            effectReport.effects.push_back(std::move(state));
        }
        auto p = out; p.clearColorEnabled = p.clearDepthEnabled = false;
        dev.beginPass(p);
        return;
    }
    const int w = std::max(f.width, 1), h = std::max(f.height, 1);
    // The scene/depth/velocity may be lower resolution than the final
    // framebuffer, but this remains the original resolution without opt-in.
    int outW=w, outH=h;
    gfx::TextureInfo outputInfo{};
    if (out.colorTarget.valid() && dev.textureInfo(out.colorTarget,outputInfo) &&
        outputInfo.width>0 && outputInfo.height>0) {
        outW=outputInfo.width; outH=outputInfo.height;
    } else if (const auto it=requestedOutputSizes_.find(viewId_);
               it!=requestedOutputSizes_.end()) {
        outW=std::max(1,it->second.first);
        outH=std::max(1,it->second.second);
    } else if (!out.colorTarget.valid() &&
               dev.framebufferWidth()>0 && dev.framebufferHeight()>0) {
        outW=dev.framebufferWidth();outH=dev.framebufferHeight();
    }
    // Never upscale an unrelated native caller that did not explicitly
    // choose the new render-size contract. An output target with a larger
    // size is still filled correctly by the ordinary composite fallback.
    stats_.width=w;stats_.height=h;
    stats_.outputWidth=outW;stats_.outputHeight=outH;
    Sized& set = sized(w, h);
    Target scene = target(set, kScene, w, h);
    const float flip = dev.info().renderTargetFlipY ? 1.0f : 0.0f;
    const auto active = settings_.activeAt(quality_);
    stats_.effects = int(active.size());
    auto find = [&](PrismaType t) -> const PrismaEffect* {
        for (auto* e : active) if (e->type == t) return e;
        return nullptr;
    };
    const float seed = float(frame_ % 61) * 0.173f;

    // Atmosphere contributes to the HDR scene before exposure, motion and temporal resolve.
    if(skySettings_.enabled&&hdrFrame_) {
        const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(skyEpoch_<0)skyEpoch_=now;
        const Target skyTarget=target(set,kPhysicalSky,w,h);
        if(sky_.draw(dev,f,skySettings_,int(quality_),skyTime_>=0?skyTime_:now-skyEpoch_,
                     scene.tex,depthFor(set,w,h),skyTarget.tex,depthFor(set,w,h,true),triangle_,
                     skyAdvection_?&skyDisplacement_:nullptr)) {
            scene=skyTarget;++stats_.passes;++stats_.effects;
        } else if(!sky_.lastError().empty())error_=sky_.lastError();
    }
    if(fogSettings_.enabled&&hdrFrame_) {
        const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(fogEpoch_<0)fogEpoch_=now;
        const Target fogTarget=target(set,kLocalFog,w,h);
        if(fog_.draw(dev,f,fogSettings_,fogScene_,int(quality_),scene.tex,depthFor(set,w,h),
                     fogTarget.tex,depthFor(set,w,h,true),triangle_,fogTime_>=0?fogTime_:now-fogEpoch_)) {
            scene=fogTarget;++stats_.passes;++stats_.effects;
        } else if(!fog_.lastError().empty())error_=fog_.lastError();
    }

    // Invalidate per-camera HDR exposure BEFORE the current-frame meter.
    // Otherwise a camera jump can present one frame of stale adaptation.
    auto& motionView = motionViews_[viewId_];
    const float3 cameraDelta = f.cameraPos - motionView.previousCameraPos;
    const bool automaticCut = motionView.valid &&
        (!std::isfinite(cameraDelta.x) || !std::isfinite(cameraDelta.y) ||
         !std::isfinite(cameraDelta.z) ||
         dot(cameraDelta,cameraDelta) > 144.0f); // >12 presentation units
    if (automaticCut) {
        motionView.valid = false;
        ++motionView.cutEpoch;
        if (auto it = temporalViews_.find(viewId_); it != temporalViews_.end())
            it->second.valid = false;
        resetAutoExposure(viewId_);
    }

    // ---- Stage 23: GPU-only HDR eye adaptation (linear scene -> log luminance
    // pyramid -> 1x1 exposure ping-pong). The 1x1 multiplier is applied in
    // composite BEFORE tone mapping; never post-process UI/HUD.
    gfx::TextureHandle automaticExposure;
    const PrismaEffect* autoEffect = find(PrismaType::AutoExposure);
    // Manual EV replaces the adapted factor but still belongs to the
    // authored AutoExposure effect. Explicit native false disables even a
    // profile-authored manual mode; the old GPU auto flag is not required
    // when the user explicitly selected fixed HDR exposure.
    const auto manualOverride = viewManualExposure_.find(viewId_);
    const bool manualSelected = hdrFrame_ && autoEffect &&
        (manualOverride != viewManualExposure_.end()
            ? manualOverride->second.enabled
            : autoEffect->get("manualExposure") > 0.5f);
    const float manualRawEV = manualSelected
        ? (manualOverride != viewManualExposure_.end()
            ? manualOverride->second.ev : autoEffect->get("manualEV"))
        : 0.0f;
    // Guard both JSON and native-mutated profiles: NaN/Inf must never
    // propagate into scene-linear color or produce invalid HDR pixels.
    const float manualEV = std::isfinite(manualRawEV)
        ? std::clamp(manualRawEV,-12.0f,12.0f) : 0.0f;
    if (manualSelected) resetAutoExposure(viewId_);
    const char* autoFlag = std::getenv("AIVERSE_PRISMA_AUTO_EXPOSURE");
    const bool autoRequested = hdrFrame_ && autoEffect && !manualSelected &&
        autoFlag && std::strcmp(autoFlag, "1") == 0;
    // Native stable-view overrides take precedence over volume/profile
    // fields, including an explicit false. Two cameras at the SAME world
    // position may therefore use independent cinematic exposure controls.
    const auto freezeOverride = viewExposureFreeze_.find(viewId_);
    const bool freezeRequested = autoRequested &&
        (freezeOverride != viewExposureFreeze_.end()
            ? freezeOverride->second
            : autoEffect->get("freezeExposure") > 0.5f);
    ExposureMeterRegion meterRegion{};
    if (autoEffect) {
        meterRegion.enabled = autoEffect->get("meterRegionEnabled") > 0.5f;
        meterRegion.centerX = autoEffect->get("meterCenterX");
        meterRegion.centerY = autoEffect->get("meterCenterY");
        meterRegion.radius = autoEffect->get("meterRadius");
        meterRegion.feather = autoEffect->get("meterFeather");
        meterRegion.outsideWeight = autoEffect->get("meterOutsideWeight");
        meterRegion.box = autoEffect->get("meterBox") > 0.5f;
        meterRegion.halfWidth = autoEffect->get("meterHalfWidth");
        meterRegion.halfHeight = autoEffect->get("meterHalfHeight");
    }
    if (const auto override = viewExposureMeterRegions_.find(viewId_);
        override != viewExposureMeterRegions_.end())
        meterRegion = override->second;
    const bool regionRequested = autoRequested && meterRegion.enabled;
    bool exposureFrozenThisFrame = false;
    bool exposureRegionMeteredThisFrame = false;
    if (freezeRequested) {
        // Reuse the last successful 1x1 exposure without encoding any
        // luminance pyramid or writing to a history currently sampled.
        // First render/camera cut resets valid and seeds exactly once below.
        if (auto it = exposureViews_.find(viewId_);
            it != exposureViews_.end() && it->second.valid &&
            it->second.history[it->second.current].valid()) {
            automaticExposure = it->second.history[it->second.current];
            it->second.time = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            exposureFrozenThisFrame = true;
        }
    }
    if (!automaticExposure.valid() && autoRequested &&
        program(kExposureLog).valid() &&
        program(kExposureReduce).valid() && program(kExposureAdapt).valid()) {
        gfx::TextureHandle source = scene.tex;
        int pw = w, ph = h;
        bool valid = source.valid();
        for (int level = 0; valid && (pw > 1 || ph > 1) && level < 15; ++level) {
            const int nextW = std::max(1, (pw + 1) / 2);
            const int nextH = std::max(1, (ph + 1) / 2);
            const Target mip = target(set, kExposureMip + level, nextW, nextH);
            if (!mip.tex.valid() || !depthFor(set, nextW, nextH).valid()) {
                valid = false;
                break;
            }
            ExposureMeterU measure{};
            put(measure.px, 1.0f / float(pw), 1.0f / float(ph), flip, 0);
            if (level == 0) {
                put(measure.p0, autoEffect->get("centerWeight"),
                    autoEffect->get("centerRadius"),
                    autoEffect->get("highlightRejection"),
                    meterRegion.outsideWeight);
                put(measure.sun, autoEffect->get("minMeterLogL"),
                    autoEffect->get("maxMeterLogL"),
                    autoEffect->get("highlightStartEV"),
                    autoEffect->get("highlightSoftnessEV"));
                put(measure.region, meterRegion.centerX,
                    meterRegion.centerY,
                    regionRequested ? meterRegion.radius : -1.0f,
                    meterRegion.feather);
                put(measure.regionBox, meterRegion.halfWidth,
                    meterRegion.halfHeight, meterRegion.box ? 1.0f : 0.0f, 0);
            }
            draw(set, mip, pipes_[level == 0 ? kExposureLog : kExposureReduce],
                 &measure, level == 0 ? sizeof measure : sizeof(SimpleU),
                 {{0, source}});
            source = mip.tex;
            pw = nextW; ph = nextH;
        }
        // A 1x1 scene input still needs logarithmic luminance encoding.
        if (valid && pw == w && ph == h) {
            const Target mip = target(set, kExposureMip, 1, 1);
            if (mip.tex.valid() && depthFor(set, 1, 1).valid()) {
                ExposureMeterU measure{};
                put(measure.px, 1, 1, flip, 0);
                put(measure.p0, autoEffect->get("centerWeight"),
                    autoEffect->get("centerRadius"),
                    autoEffect->get("highlightRejection"),
                    meterRegion.outsideWeight);
                put(measure.sun, autoEffect->get("minMeterLogL"),
                    autoEffect->get("maxMeterLogL"),
                    autoEffect->get("highlightStartEV"),
                    autoEffect->get("highlightSoftnessEV"));
                put(measure.region, meterRegion.centerX,
                    meterRegion.centerY,
                    regionRequested ? meterRegion.radius : -1.0f,
                    meterRegion.feather);
                put(measure.regionBox, meterRegion.halfWidth,
                    meterRegion.halfHeight, meterRegion.box ? 1.0f : 0.0f, 0);
                draw(set, mip, pipes_[kExposureLog], &measure, sizeof measure,
                     {{0, scene.tex}});
                source = mip.tex;
            } else valid = false;
        }
        // Avoid allocating or writing exposure history after a failed
        // mip reduction: a partially encoded pyramid must not be sampled.
        valid = valid && pw == 1 && ph == 1;
        if (!valid) {
            resetAutoExposure(viewId_);
        } else {
        auto& exposureView = exposureViews_[viewId_];
        // History belongs to the stable view ID, not to the shared 1x1
        // Sized cache: same-resolution Scene/Game views never ping-pong
        // against each other's exposure or time delta.
        for (auto& handle : exposureView.history) {
            if (handle.valid()) continue;
            gfx::TextureDesc desc;
            desc.width = desc.height = 1;
            desc.format = gfx::PixelFormat::RGBA16F;
            desc.renderTarget = true;
            desc.filter = gfx::Filter::Nearest;
            desc.label = "prisma exposure camera history";
            handle = dev.createTexture(desc);
        }
        const int nextIndex = 1 - exposureView.current;
        const gfx::TextureHandle nextExposure = exposureView.history[nextIndex];
        const gfx::TextureHandle prevExposure = exposureView.history[exposureView.current];
        if (valid && nextExposure.valid() && prevExposure.valid() &&
            depthFor(set, 1, 1).valid()) {
            const double timeNow =
                std::chrono::duration<double>(std::chrono::steady_clock::now()
                    .time_since_epoch()).count();
            const float dt = exposureView.valid && exposureView.time > 0
                ? std::clamp(float(timeNow - exposureView.time), 0.0f, 0.1f)
                : 0.0f;
            SimpleU adapt{};
            put(adapt.px, 1, 1, flip, 0);
            put(adapt.p0, dt, autoEffect->get("speedUp"),
                autoEffect->get("speedDown"), exposureView.valid ? 1.0f : 0.0f);
            put(adapt.sun, autoEffect->get("keyValue"),
                autoEffect->get("minEV"), autoEffect->get("maxEV"),
                autoEffect->get("compensation"));
            const Target exposureOutput{nextExposure, 1, 1, gfx::PixelFormat::RGBA16F};
            draw(set, exposureOutput, pipes_[kExposureAdapt], &adapt, sizeof adapt,
                 {{0, source}, {1, exposureView.valid ? prevExposure : black_}});
            exposureView.current = nextIndex;
            exposureView.valid = true;
            exposureView.time = timeNow;
            automaticExposure = nextExposure;
            exposureRegionMeteredThisFrame = regionRequested;
        } else {
            exposureView.valid = false;
        }
        } // complete metering pyramid only
    } else if (!automaticExposure.valid()) {
        // No authored/usable meter this frame: preserve allocated storage,
        // but invalidate previous luminance for the next cold frame.
        resetAutoExposure(viewId_);
    }

    // ---- Stage 42: one camera/object velocity pass feeds both HDR temporal
    // reconstruction and shutter blur. Temporal history is keyed by viewId,
    // not shared by same-resolution Scene/Game cameras.
    Target src = scene;
    // The same camera cut already invalidated both velocity and exposure
    // before the GPU meter was encoded.
    const bool previousValid = motionView.valid &&
        motionView.width == w && motionView.height == h;
    const auto validInput = [&](const MotionTextureInput& input,
                                gfx::PixelFormat required) {
        if (!input.validFor(w,h,required)) return false;
        gfx::TextureInfo actual{};
        return !dev.textureInfo(input.texture,actual) ||
            (actual.width == w && actual.height == h &&
             actual.format == required);
    };
    const bool validVelocity = validInput(motionView.objectVelocity,
                                         gfx::PixelFormat::RGBA16F);
    const bool validReactive = validInput(motionView.reactiveMask,
                                         gfx::PixelFormat::RGBA8);
    effectReport.reactiveInputSupplied = motionView.reactiveMask.texture.valid();
    effectReport.reactiveInputAccepted = validReactive;
    effectReport.objectVelocityInputAccepted = validVelocity;
    effectReport.reactiveWithoutObjectVelocity = validReactive && !validVelocity;
    stats_.reactiveUsed=validReactive;
    bool motionBlurEncoded = false;
    bool depthOfFieldEncoded = false;
    bool dofMetricRequested = false;
    bool dofMetricEncoded = false;
    bool fxaaEncoded = false;
    const PrismaEffect* motionEffect = find(PrismaType::MotionBlur);
    const PrismaEffect* temporalEffect = find(PrismaType::TemporalResolve);
    const char* motionFlag = std::getenv("AIVERSE_PRISMA_MOTION_BLUR");
    const char* temporalFlag = std::getenv("AIVERSE_PRISMA_TEMPORAL");
    const bool motionRequested = hdrFrame_ && motionEffect && motionFlag &&
        std::strcmp(motionFlag,"1")==0 && motionEffect->get("intensity")>0 &&
        motionEffect->get("maxPixels")>0;
    const bool rejectedHistory = viewHistoryAdmissionRejected_.count(viewId_) != 0 ||
        (temporalViews_.count(viewId_) != 0 &&
         temporalViews_.at(viewId_).preflightRejected);
    const bool temporalRequested = hdrFrame_ && temporalEffect && temporalFlag &&
        std::strcmp(temporalFlag,"1")==0 && !rejectedHistory &&
        temporalEffect->get("historyWeight")>0;
    const gfx::TextureHandle sceneDepth = depthFor(set,w,h);
    const gfx::TextureHandle scratchDepth = depthFor(set,w,h,true);
    Target velocity;
    if (previousValid && (motionRequested || temporalRequested) &&
        scene.tex.valid() && sceneDepth.valid() && scratchDepth.valid() &&
        program(kMotionVectors).valid()) {
        velocity=target(set,kMotionVelocity,w,h);
        if (velocity.tex.valid()) {
            MotionUniforms vectors{};
            put(vectors.px,1.0f/float(w),1.0f/float(h),flip,0);
            put(vectors.p0,dev.info().clipDepthZeroToOne?1.0f:0.0f,
                validVelocity?1.0f:0.0f,0,0);
            const mat4 inverseViewProj=f.viewProj.inverse();
            std::memcpy(vectors.inv0,inverseViewProj.m,sizeof inverseViewProj.m);
            std::memcpy(vectors.prev0,motionView.previousViewProj.m,
                        sizeof motionView.previousViewProj.m);
            gfx::PassDesc pass{};
            pass.colorTarget=velocity.tex;
            pass.depthTarget=scratchDepth; // cannot bind sampled scene depth as attachment
            pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
            pass.clearColor[3]=1;
            dev.beginPass(pass);
            dev.setViewport({0,0,w,h});
            dev.setPipeline(pipes_[kMotionVectors]);
            dev.setVertexBuffer(triangle_);
            dev.setUniforms(0,&vectors,sizeof vectors);
            dev.setTexture(0,scene.tex);
            dev.setTexture(1,sceneDepth);
            dev.setTexture(2,validVelocity?motionView.objectVelocity.texture:black_);
            dev.draw(3);
            dev.endPass();
            ++stats_.passes;
        }
    }

    // Motion blur runs in internal render resolution before the temporal
    // upscale; both effects still share exactly one velocity pass.
    if (motionRequested && previousValid && velocity.tex.valid() &&
        sceneDepth.valid() && scratchDepth.valid() &&
        program(kMotionBlur).valid()) {
        const Target blurred=target(set,kMotionColor,w,h);
        if (blurred.tex.valid()) {
            MotionBlurU blur{};
            put(blur.px,1.0f/float(w),1.0f/float(h),flip,0);
            put(blur.p0,motionEffect->get("shutterScale"),
                motionEffect->get("maxPixels"),
                motionEffect->get("samples"),motionEffect->get("intensity"));
            put(blur.sun,motionEffect->get("depthThreshold"),
                motionEffect->get("reactiveStrength"),
                motionEffect->get("velocityDilation"),
                motionEffect->get("depthToleranceMeters"));
            const bool linearDepth=motionEffect->get("linearDepth")>0.5f &&
                f.depthFar>f.depthNear+0.001f &&
                (f.orthographic || f.depthNear>0.0f);
            put(blur.depthInfo,f.depthNear,f.depthFar,
                f.orthographic?1.0f:0.0f,linearDepth?1.0f:0.0f);
            // No reactive texture? Keep the historical shutter filter.
            // When supplied, optionally suppress neighboring samples
            // belonging to a different transparency coverage layer.
            put(blur.tapInfo,validReactive?
                    motionEffect->get("reactiveTapRejection"):0.0f,0,0,0);
            gfx::PassDesc pass{};
            pass.colorTarget=blurred.tex;
            pass.depthTarget=scratchDepth;
            pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
            pass.clearColor[3]=1;
            dev.beginPass(pass);
            dev.setViewport({0,0,w,h});
            dev.setPipeline(pipes_[kMotionBlur]);
            dev.setVertexBuffer(triangle_);
            dev.setUniforms(0,&blur,sizeof blur);
            dev.setTexture(0,src.tex);
            dev.setTexture(1,velocity.tex);
            dev.setTexture(2,sceneDepth);
            dev.setTexture(3,validReactive?motionView.reactiveMask.texture:black_);
            dev.draw(3);
            dev.endPass();
            ++stats_.passes;
            src=blurred;
            motionBlurEncoded = true;
            effectReport.motionBlurReactiveEdgesEnabled = validReactive &&
                motionEffect->get("reactiveTapRejection") > 0.001f;
        }
    }
    // ---- experimental Depth of Field: opt-in camera-linear focus plane
    // while preserving the existing device-depth result for old profiles.
    // The scene depth texture must NEVER be attached for depth writes while
    // sampled, so a separate scratch depth attachment is used for this pass.
    if (const PrismaEffect* dof = find(PrismaType::DepthOfField)) {
        const char* flag = std::getenv("AIVERSE_PRISMA_DOF");
        const auto lensDof = viewDepthOfField_.find(viewId_);
        const bool lensOverride = lensDof != viewDepthOfField_.end();
        const float dofMaxBlur = lensOverride ? lensDof->second.maxBlur
                                              : dof->get("maxBlur");
        if (flag && std::strcmp(flag, "1") == 0 &&
            (!lensOverride || lensDof->second.enabled) &&
            dof->get("intensity") > 0.0f && dofMaxBlur > 0.0f &&
            program(kDepthOfField).valid()) {
            const Target destination = target(set, kDof, w, h);
            const auto sceneDepth = depthFor(set, w, h);
            const auto scratchDepth = depthFor(set, w, h, true);
            if (sceneDepth.valid() && scratchDepth.valid() &&
                destination.tex.valid() && src.tex.valid()) {
                DepthOfFieldU dofU{};
                put(dofU.px, 1.0f / float(w), 1.0f / float(h), flip, 0);
                put(dofU.p0, dof->get("focusDepth"),
                    dof->get("focusRange"), dofMaxBlur,
                    dof->get("intensity"));
                // RHI depth uses non-reversed window [0,1] on GL/Metal.
                // Orthographic bounds may be signed (e.g. -200..1000);
                // perspective near must be positive to linearize safely.
                const bool projectionValid =
                    std::isfinite(f.depthNear) && std::isfinite(f.depthFar) &&
                    f.depthFar > f.depthNear + 0.0001f &&
                    (f.orthographic || f.depthNear > 0.0f);
                const bool metricRequested =
                    lensOverride || dof->get("metricFocus") > 0.5f;
                const bool useMetricFocus = metricRequested && projectionValid;
                dofMetricRequested = metricRequested;
                put(dofU.depthInfo, f.depthNear, f.depthFar,
                    f.orthographic ? 1.0f : 0.0f, 0.0f);
                put(dofU.focusInfo,
                    lensOverride ? lensDof->second.focusDistance
                                 : dof->get("focusDistance"),
                    lensOverride ? lensDof->second.focusRangeMeters
                                 : dof->get("focusRangeMeters"),
                    dof->get("occlusionToleranceMeters"),
                    useMetricFocus ? 1.0f : 0.0f);
                gfx::PassDesc pass;
                pass.colorTarget = destination.tex;
                pass.depthTarget = scratchDepth;
                pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0;
                pass.clearColor[3] = 1;
                dev.beginPass(pass);
                dev.setViewport({0, 0, w, h});
                dev.setPipeline(pipes_[kDepthOfField]);
                dev.setVertexBuffer(triangle_);
                dev.setUniforms(0, &dofU, sizeof dofU);
                dev.setTexture(0, src.tex);
                dev.setTexture(1, sceneDepth);
                dev.draw(3);
                dev.endPass();
                ++stats_.passes;
                src = destination;
                depthOfFieldEncoded = true;
                dofMetricEncoded = useMetricFocus;
            }
        }
    }

    // ---- FXAA: the composite reads the anti-aliased image instead of the scene
    if (const PrismaEffect* e = find(PrismaType::Fxaa); !hdrFrame_ && e && program(kFxaa).valid()) {
        SimpleU u{};
        put(u.px, 1.0f / float(w), 1.0f / float(h), flip, 0);
        put(u.p0, e->get("subpixel"), 0, 0, 0);
        const Target fxaaInput = src;
        const Target fxaaOutput = target(set, kAa, w, h);
        // Do not substitute a failed/invalid render target as the next
        // composite input. The previous scene remains the stable fallback.
        if (fxaaInput.tex.valid() && fxaaOutput.tex.valid() &&
            depthFor(set, w, h).valid()) {
            // Experimental Stage 14: only the SDR FXAA prepass uses the compiled
            // RenderGraph, with the same pipeline/uniforms/inputs as the fixed path.
            // Off by default; the fallback is kept for unsupported devices/errors.
            bool graphDrawn = false;
            const char* optIn = std::getenv("AIVERSE_PRISMA_GRAPH_FXAA");
            if (optIn && std::strcmp(optIn, "1") == 0 && fxaaOutput.tex.valid()) {
                const gfx::TextureHandle aaDepth = depthFor(set, w, h, true);
                if (aaDepth.valid()) {
                    PrismaRenderGraph graph;
                    const auto input = graph.addResource({
                        "scene-fxaa", PrismaGraphFormat::RGBA8,
                        static_cast<uint32_t>(w), static_cast<uint32_t>(h), true});
                    const auto output = graph.addResource({
                        "fxaa-output", PrismaGraphFormat::RGBA8,
                        static_cast<uint32_t>(w), static_cast<uint32_t>(h), true});
                    PrismaGraphPass fxaa;
                    fxaa.name = "fxaa";
                    fxaa.reads = {input};
                    fxaa.writes = {output};
                    graph.addPass(std::move(fxaa));
                    const auto plan = graph.compile();
                    // No graph-managed targets for this first pass: both handles
                    // already exist in the same per-view Sized allocator.
                    if (!set.graphExecutor) set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                    auto& executor = *set.graphExecutor;
                    std::vector<gfx::TextureHandle> external{fxaaInput.tex, fxaaOutput.tex};
                    const auto run = executor.execute(
                        graph, plan, external,
                        [&](gfx::Device& gpu, PrismaGraphId,
                            const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                            gpu.setViewport({0, 0, w, h});
                            gpu.setPipeline(pipes_[kFxaa]);
                            gpu.setVertexBuffer(triangle_);
                            gpu.setUniforms(0, &u, sizeof u);
                            gpu.setTexture(0, view.resource(input));
                            gpu.draw(3);
                            return true;
                        },
                        [](gfx::Device&, PrismaGraphId,
                           const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                            return false; // FXAA is raster-only.
                        },
                        aaDepth);
                    graphDrawn = run.success;
                    if (graphDrawn) ++stats_.passes;
                    else if (error_.empty()) error_ = run.error;
                }
            }
            if (!graphDrawn) {
                const int before = stats_.passes;
                draw(set, fxaaOutput, pipes_[kFxaa], &u, sizeof u, {{0, fxaaInput.tex}});
                fxaaEncoded = stats_.passes > before;
            } else fxaaEncoded = true;
            if (fxaaEncoded) src = fxaaOutput;
        } // valid SDR FXAA attachments only
    }

    // ---- bloom: prefilter -> dual-filter down chain -> up chain
    gfx::TextureHandle bloomTex;
    float bloomNorm = 1;
    const PrismaEffect* bloom = find(PrismaType::Bloom);
    if (bloom && program(kPrefilter).valid() && program(kDown).valid() && program(kUp).valid()) {
        const BloomPlan plan = bloomPlan(quality_);
        const char* chainFlag = std::getenv("AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN");
        const bool chainEnabled = chainFlag && std::strcmp(chainFlag, "1") == 0;
        const char* poolFlag = std::getenv("AIVERSE_PRISMA_GRAPH_BLOOM_POOL");
        const bool pooledBloom = chainEnabled && poolFlag && std::strcmp(poolFlag, "1") == 0;
        std::vector<Target> down;
        int lw = std::max(1, w / plan.startDiv), lh = std::max(1, h / plan.startDiv);
        for (int i = 0; i < plan.levels && lw >= 2 && lh >= 2; ++i, lw /= 2, lh /= 2) {
            if (pooledBloom) {
                Target descriptor;
                descriptor.w = lw; descriptor.h = lh;
                down.push_back(descriptor);
            } else {
                down.push_back(target(set, kBloomDown + i, lw, lh));
            }
        }
        if (!down.empty()) {
            // Stage 16: opt-in complete Bloom graph. All physical textures
            // remain in the existing per-viewport Sized cache; the graph
            // handles pass dependencies/order, not cross-frame ownership.
            bool bloomChainHandled = false;
            if (chainEnabled && src.tex.valid()) {
                const PrismaGraphFormat format =
                    hdrFrame_ ? PrismaGraphFormat::RGBA16F : PrismaGraphFormat::RGBA8;
                std::vector<PrismaBloomMip> mipSizes;
                mipSizes.reserve(down.size());
                for (const auto& mip : down)
                    mipSizes.push_back({static_cast<uint32_t>(mip.w),
                                        static_cast<uint32_t>(mip.h)});
                // Only recompile when viewport dimensions, HDR mode or the
                // quality-dependent mip layout change. Per-frame uniforms and
                // texture handles remain dynamic.
                auto& cache = set.bloomGraph;
                if (cache.configure(static_cast<uint32_t>(src.w),
                                    static_cast<uint32_t>(src.h), format, mipSizes,
                                    pooledBloom)) {
                std::vector<Target> upTargets(down.size());
                for (size_t i = 0; i + 1 < down.size(); ++i) {
                    if (pooledBloom) {
                        upTargets[i].w = down[i].w;
                        upTargets[i].h = down[i].h;
                    } else {
                        upTargets[i] = target(set, kBloomUp + static_cast<int>(i),
                                              down[i].w, down[i].h);
                    }
                }

                std::vector<gfx::TextureHandle> handles(
                    cache.graph().resources().size());
                handles[cache.sourceId()] = src.tex;
                for (size_t i = 0; i < down.size(); ++i)
                    handles[cache.downIds()[i]] = down[i].tex;
                for (size_t i = 0; i + 1 < down.size(); ++i)
                    handles[cache.upIds()[i]] = upTargets[i].tex;

                struct BloomGraphOp {
                    Prog pipeline;
                    Target destination;
                    SimpleU uniforms;
                    PrismaGraphId input0;
                    PrismaGraphId input1;
                };
                std::vector<BloomGraphOp> ops;
                ops.reserve(cache.operations().size());
                const float scatter = 0.3f + 0.7f * bloom->get("radius");
                for (const auto& binding : cache.operations()) {
                    const size_t i = binding.mipIndex;
                    const bool upsample = binding.type == PrismaBloomPassType::Upsample;
                    Target destination = upsample ? upTargets[i] : down[i];
                    SimpleU bu{};
                    Prog pipeline = kPrefilter;
                    if (binding.type == PrismaBloomPassType::Prefilter) {
                        put(bu.px, float(plan.startDiv) * 0.5f / float(w),
                            float(plan.startDiv) * 0.5f / float(h), flip, 0);
                        put(bu.p0, bloom->get("threshold"), bloom->get("knee"), 0, 0);
                    } else if (binding.type == PrismaBloomPassType::Downsample) {
                        pipeline = kDown;
                        put(bu.px, 1.0f / float(down[i-1].w),
                            1.0f / float(down[i-1].h), flip, 0);
                    } else {
                        pipeline = kUp;
                        const Target low = (i + 1 == down.size() - 1)
                            ? down.back() : upTargets[i+1];
                        put(bu.px, 1.0f / float(low.w),
                            1.0f / float(low.h), flip, 0);
                        put(bu.p0, scatter, 0, 0, 0);
                    }
                    ops.push_back({pipeline, destination, bu,
                                   binding.input0, binding.input1});
                }
                const auto& graphPlan = cache.plan();
                std::vector<gfx::TextureHandle> depths;
                bool ready = graphPlan.valid;
                if (ready) for (const auto& op : ops) {
                    const auto depth = depthFor(set, op.destination.w, op.destination.h, true);
                    depths.push_back(depth);
                    ready = ready && depth.valid() &&
                        (pooledBloom || op.destination.tex.valid());
                }
                for (size_t id = 0; id < handles.size(); ++id)
                    if (cache.graph().resources()[id].external)
                        ready = ready && handles[id].valid();
                if (ready) {
                    if (!set.graphExecutor) set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                    auto& executor = *set.graphExecutor;
                    const auto run = executor.execute(
                        cache.graph(), graphPlan, handles,
                        [&](gfx::Device& gpu, PrismaGraphId id,
                            const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                            const auto& op = ops[id];
                            gpu.setViewport({0, 0, op.destination.w, op.destination.h});
                            gpu.setPipeline(pipes_[op.pipeline]);
                            gpu.setVertexBuffer(triangle_);
                            gpu.setUniforms(0, &op.uniforms, sizeof op.uniforms);
                            gpu.setTexture(0, view.resource(op.input0));
                            if (op.input1 != kInvalidPrismaGraphId)
                                gpu.setTexture(1, view.resource(op.input1));
                            gpu.draw(3);
                            return true;
                        },
                        [](gfx::Device&, PrismaGraphId,
                           const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                            return false;
                        }, {}, true,
                        [&](PrismaGraphId id) { return depths[id]; });
                    if (run.success) {
                        stats_.passes += static_cast<int>(run.passes);
                        bloomTex = pooledBloom ? run.resources[cache.outputId()] :
                            ((down.size() == 1) ? down.back().tex : upTargets[0].tex);
                        bloomNorm = 0.0f;
                        for (size_t k = 0; k < down.size(); ++k)
                            bloomNorm += std::pow(scatter, float(k));
                        bloomChainHandled = true;
                    } else {
                        if (error_.empty()) error_ = run.error;
                        // Never repeat a partially submitted GPU graph.
                        bloomChainHandled = run.passes > 0;
                        stats_.passes += static_cast<int>(run.passes);
                    }
                }
                } // compiled/cached Bloom graph is valid
            }
            if (!bloomChainHandled) {
            if (pooledBloom) for (size_t i = 0; i < down.size(); ++i)
                down[i] = target(set, kBloomDown + static_cast<int>(i),
                                 down[i].w, down[i].h);
            SimpleU u{};
            const float spread = float(plan.startDiv) * 0.5f;
            put(u.px, spread / float(w), spread / float(h), flip, 0);
            put(u.p0, bloom->get("threshold"), bloom->get("knee"), 0, 0);
            // Stage 15: optionally route bloom's *first* prefilter pass
            // through the same graph/RHI contract used by experimental FXAA.
            // The remaining bloom down/up passes keep the existing path.
            bool bloomGraphDrawn = false;
            const char* bloomOptIn = std::getenv("AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER");
            if (bloomOptIn && std::strcmp(bloomOptIn, "1") == 0 &&
                src.tex.valid() && down[0].tex.valid()) {
                const gfx::TextureHandle prefilterDepth =
                    depthFor(set, down[0].w, down[0].h, true);
                if (prefilterDepth.valid()) {
                    PrismaRenderGraph graph;
                    const PrismaGraphFormat pixelFormat =
                        hdrFrame_ ? PrismaGraphFormat::RGBA16F : PrismaGraphFormat::RGBA8;
                    const auto input = graph.addResource({
                        "bloom-source", pixelFormat,
                        static_cast<uint32_t>(src.w), static_cast<uint32_t>(src.h), true});
                    const auto output = graph.addResource({
                        "bloom-prefilter", pixelFormat,
                        static_cast<uint32_t>(down[0].w),
                        static_cast<uint32_t>(down[0].h), true});
                    PrismaGraphPass prefilter;
                    prefilter.name = "bloom-prefilter";
                    prefilter.reads = {input};
                    prefilter.writes = {output};
                    graph.addPass(std::move(prefilter));
                    const auto graphPlan = graph.compile();
                    if (!set.graphExecutor) set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                    auto& executor = *set.graphExecutor;
                    const std::vector<gfx::TextureHandle> external{
                        src.tex, down[0].tex};
                    const auto result = executor.execute(
                        graph, graphPlan, external,
                        [&](gfx::Device& gpu, PrismaGraphId,
                            const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                            gpu.setViewport({0, 0, down[0].w, down[0].h});
                            gpu.setPipeline(pipes_[kPrefilter]);
                            gpu.setVertexBuffer(triangle_);
                            gpu.setUniforms(0, &u, sizeof u);
                            gpu.setTexture(0, view.resource(input));
                            gpu.draw(3);
                            return true;
                        },
                        [](gfx::Device&, PrismaGraphId,
                           const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                            return false;
                        },
                        prefilterDepth);
                    bloomGraphDrawn = result.success;
                    if (bloomGraphDrawn) ++stats_.passes;
                    else if (error_.empty()) error_ = result.error;
                }
            }
            if (!bloomGraphDrawn)
                draw(set, down[0], pipes_[kPrefilter], &u, sizeof u, {{0, src.tex}});
            for (size_t i = 1; i < down.size(); ++i) {
                put(u.px, 1.0f / float(down[i - 1].w), 1.0f / float(down[i - 1].h), flip, 0);
                draw(set, down[i], pipes_[kDown], &u, sizeof u, {{0, down[i - 1].tex}});
            }
            const float scatter = 0.3f + 0.7f * bloom->get("radius");
            Target low = down.back();
            for (int i = int(down.size()) - 2; i >= 0; --i) {
                const Target up = target(set, kBloomUp + i, down[size_t(i)].w, down[size_t(i)].h);
                put(u.px, 1.0f / float(low.w), 1.0f / float(low.h), flip, 0);
                put(u.p0, scatter, 0, 0, 0);
                draw(set, up, pipes_[kUp], &u, sizeof u, {{0, low.tex}, {1, down[size_t(i)].tex}});
                low = up;
            }
            bloomTex = low.tex;
            bloomNorm = 0;
            for (size_t k = 0; k < down.size(); ++k) bloomNorm += std::pow(scatter, float(k));
            } // legacy Bloom chain
        }
    }

    // ---- tilt-shift: downsample + one separable gaussian (H, V)
    gfx::TextureHandle tiltTex;
    const PrismaEffect* tilt = find(PrismaType::TiltShift);
    if (tilt && program(kDown).valid() && program(kBlur).valid()) {
        const BlurPlan plan = tiltBlurPlan(quality_);
        const Target a = target(set, kTilt0, w / plan.div, h / plan.div), b = target(set, kTilt1, w / plan.div, h / plan.div);
        SimpleU u{};
        put(u.px, float(plan.div) * 0.5f / float(w), float(plan.div) * 0.5f / float(h), flip, 0);
        const SimpleU downU = u;
        const float spread = (0.5f + 2.0f * tilt->get("blur")) * (plan.div == 2 ? 2.0f : 1.0f);   // same radius on screen
        put(u.px, 1.0f / float(a.w), 1.0f / float(a.h), flip, 0);
        put(u.p0, spread, 0, 0, 0);
        const SimpleU horizontalU = u;
        put(u.p0, 0, spread, 0, 0);
        const SimpleU verticalU = u;

        bool graphHandled = false;
        const char* graphFlag = std::getenv("AIVERSE_PRISMA_GRAPH_TILT_SHIFT");
        if (graphFlag && std::strcmp(graphFlag, "1") == 0 &&
            src.tex.valid() && a.tex.valid() && b.tex.valid()) {
            const auto depth = depthFor(set, a.w, a.h, true);
            if (depth.valid()) {
                const auto format = hdrFrame_ ? PrismaGraphFormat::RGBA16F :
                                                PrismaGraphFormat::RGBA8;
                PrismaRenderGraph graph;
                const auto source = graph.addResource({
                    "tilt-source", format, static_cast<uint32_t>(src.w),
                    static_cast<uint32_t>(src.h), true});
                const auto down = graph.addResource({
                    "tilt-down", format, static_cast<uint32_t>(a.w),
                    static_cast<uint32_t>(a.h), true});
                const auto horizontal = graph.addResource({
                    "tilt-horizontal", format, static_cast<uint32_t>(b.w),
                    static_cast<uint32_t>(b.h), true});
                const auto output = graph.addResource({
                    "tilt-output", format, static_cast<uint32_t>(a.w),
                    static_cast<uint32_t>(a.h), true});
                auto pass = [&](const char* name, PrismaGraphId input,
                                PrismaGraphId dest) {
                    PrismaGraphPass p; p.name = name;
                    p.reads = {input}; p.writes = {dest};
                    graph.addPass(std::move(p));
                };
                pass("tilt-downsample", source, down);
                pass("tilt-blur-horizontal", down, horizontal);
                pass("tilt-blur-vertical", horizontal, output);
                // The two logical versions of A alias the same existing
                // physical target, but their lifetimes never overlap.
                const std::vector<gfx::TextureHandle> handles{
                    src.tex, a.tex, b.tex, a.tex};
                if (!set.graphExecutor)
                    set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                const auto result = set.graphExecutor->execute(
                    graph, graph.compile(), handles,
                    [&](gfx::Device& gpu, PrismaGraphId id,
                        const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                        const SimpleU& params = id == 0 ? downU :
                            (id == 1 ? horizontalU : verticalU);
                        gpu.setViewport({0, 0, a.w, a.h});
                        gpu.setPipeline(id == 0 ? pipes_[kDown] : pipes_[kBlur]);
                        gpu.setVertexBuffer(triangle_);
                        gpu.setUniforms(0, &params, sizeof params);
                        gpu.setTexture(0, view.resource(id == 0 ? source :
                            (id == 1 ? down : horizontal)));
                        gpu.draw(3);
                        return true;
                    },
                    [](gfx::Device&, PrismaGraphId,
                       const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                        return false;
                    }, depth);
                graphHandled = result.success || result.passes > 0;
                stats_.passes += static_cast<int>(result.passes);
                if (!result.success && error_.empty()) error_ = result.error;
                if (result.success) tiltTex = a.tex;
            }
        }
        if (!graphHandled) {
            draw(set, a, pipes_[kDown], &downU, sizeof downU, {{0, src.tex}});
            draw(set, b, pipes_[kBlur], &horizontalU, sizeof horizontalU, {{0, a.tex}});
            draw(set, a, pipes_[kBlur], &verticalU, sizeof verticalU, {{0, b.tex}});
            tiltTex = a.tex;
        }
    }

    // ---- sun shafts: from the sun on screen, or (top-down RTS cameras rarely see the sun) from a point
    // just past the screen edge in the direction the sunlight comes from
    gfx::TextureHandle shaftTex;
    float shaftColor[3] = {0, 0, 0};
    const PrismaEffect* shafts = find(PrismaType::SunShafts);
    if (shafts && program(kShaftsPre).valid() && program(kShafts).valid()) {
        const float3 sd = normalize(f.lighting.sunDir);
        const float4 c = f.viewProj * float4{sd.x, sd.y, sd.z, 0.0f};   // direction: the sun at infinity
        float nx = 0, ny = 0;
        bool placed = false;
        if (c.w > 1e-4f && std::max(std::fabs(c.x / c.w), std::fabs(c.y / c.w)) < 1.3f) { nx = c.x / c.w; ny = c.y / c.w; placed = true; }
        else {
            const mat4 inv = f.viewProj.inverse();
            const float4 m = inv * float4{0, 0, 0.5f, 1};
            if (std::fabs(m.w) > 1e-6f) {
                const float3 p0{m.x / m.w, m.y / m.w, m.z / m.w};
                const float3 p1 = p0 + sd * std::max(1.0f, length(p0 - f.cameraPos) * 0.25f);
                const float4 a0 = f.viewProj * float4{p0.x, p0.y, p0.z, 1}, a1 = f.viewProj * float4{p1.x, p1.y, p1.z, 1};
                if (a0.w > 1e-4f && a1.w > 1e-4f) {
                    float dx = a1.x / a1.w - a0.x / a0.w, dy = a1.y / a1.w - a0.y / a0.w;
                    const float l = std::sqrt(dx * dx + dy * dy);
                    if (l > 1e-5f) { nx = dx / l * 1.35f; ny = dy / l * 1.35f; placed = true; }
                }
            }
        }
        const float vis = placed ? std::clamp(f.lighting.sunIntensity, 0.0f, 1.0f) * std::clamp(sd.y * 4.0f + 0.2f, 0.0f, 1.0f) : 0.0f;
        {
            if (vis > 0.01f) {
                const ShaftsPlan plan = shaftsPlan(quality_);
                float sx = nx * 0.5f + 0.5f, sy = ny * 0.5f + 0.5f;
                if (flip > 0.5f) sy = 1.0f - sy;
                const Target a = target(set, kShaft0, w / plan.div, h / plan.div), b = target(set, kShaft1, w / plan.div, h / plan.div);
                SimpleU u{};
                const float spread = float(plan.div) * 0.5f;
                put(u.px, spread / float(w), spread / float(h), flip, 0);
                put(u.p0, shafts->get("threshold"), 0, 0, 0);
                put(u.sun, sx, sy, 0, float(w) / float(h));
                const SimpleU prefilterU = u;
                put(u.px, 1.0f / float(a.w), 1.0f / float(a.h), flip, 0);
                put(u.p0, float(plan.samples), 0.3f + 0.6f * shafts->get("length"), 0.97f, 1.6f);
                const SimpleU radialU = u;
                bool shaftGraphHandled = false;
                const char* graphFlag = std::getenv("AIVERSE_PRISMA_GRAPH_SUN_SHAFTS");
                if (graphFlag && std::strcmp(graphFlag, "1") == 0 &&
                    src.tex.valid() && a.tex.valid() && b.tex.valid()) {
                    const auto depth = depthFor(set, a.w, a.h, true);
                    if (depth.valid()) {
                        const auto fmt = hdrFrame_ ? PrismaGraphFormat::RGBA16F :
                                                     PrismaGraphFormat::RGBA8;
                        PrismaRenderGraph graph;
                        const auto source = graph.addResource({
                            "shafts-source", fmt, static_cast<uint32_t>(src.w),
                            static_cast<uint32_t>(src.h), true});
                        const auto mask = graph.addResource({
                            "shafts-mask", fmt, static_cast<uint32_t>(a.w),
                            static_cast<uint32_t>(a.h), true});
                        const auto output = graph.addResource({
                            "shafts-radial", fmt, static_cast<uint32_t>(b.w),
                            static_cast<uint32_t>(b.h), true});
                        PrismaGraphPass pre;
                        pre.name = "shafts-prefilter"; pre.reads = {source};
                        pre.writes = {mask}; graph.addPass(std::move(pre));
                        PrismaGraphPass radial;
                        radial.name = "shafts-radial"; radial.reads = {mask};
                        radial.writes = {output}; graph.addPass(std::move(radial));
                        if (!set.graphExecutor)
                            set.graphExecutor = std::make_unique<PrismaRhiExecutor>(dev);
                        const auto run = set.graphExecutor->execute(
                            graph, graph.compile(),
                            std::vector<gfx::TextureHandle>{src.tex, a.tex, b.tex},
                            [&](gfx::Device& gpu, PrismaGraphId id,
                                const PrismaGraphDispatchView<gfx::TextureHandle>& view) {
                                const SimpleU& params = id == 0 ? prefilterU : radialU;
                                gpu.setViewport({0, 0, a.w, a.h});
                                gpu.setPipeline(id == 0 ? pipes_[kShaftsPre] : pipes_[kShafts]);
                                gpu.setVertexBuffer(triangle_);
                                gpu.setUniforms(0, &params, sizeof params);
                                gpu.setTexture(0, view.resource(id == 0 ? source : mask));
                                gpu.draw(3);
                                return true;
                            },
                            [](gfx::Device&, PrismaGraphId,
                               const PrismaGraphDispatchView<gfx::TextureHandle>&) {
                                return false;
                            }, depth);
                        shaftGraphHandled = run.success || run.passes > 0;
                        stats_.passes += static_cast<int>(run.passes);
                        if (!run.success && error_.empty()) error_ = run.error;
                        if (run.success) shaftTex = b.tex;
                    }
                }
                if (!shaftGraphHandled) {
                    draw(set, a, pipes_[kShaftsPre], &prefilterU,
                         sizeof prefilterU, {{0, src.tex}});
                    draw(set, b, pipes_[kShafts], &radialU,
                         sizeof radialU, {{0, a.tex}});
                    shaftTex = b.tex;
                }
                const float* col = shafts->color("color");
                const float k = shafts->get("intensity") * vis;
                for (int i = 0; i < 3; ++i) shaftColor[i] = col[i] * k;
            }
        }
    }

    // Native-output temporal reconstruction happens after all render-scale
    // HDR scene passes, including optional shutter blur / DoF / bloom inputs.
    // This prevents later passes from accidentally shrinking output history.
    float2 fallbackJitter{};
    if (const auto it=temporalViews_.find(viewId_);
        it!=temporalViews_.end() && it->second.jitterActive)
        fallbackJitter=it->second.currentJitterUv;
    bool temporalResolved=false;
    const gfx::TextureHandle outputDepth = temporalRequested
        ? depthFor(set,outW,outH,true) : gfx::TextureHandle{};
    if (temporalRequested && src.tex.valid() && sceneDepth.valid() &&
        outputDepth.valid() && program(kTemporalResolve).valid()) {
        auto& history=temporalViews_[viewId_];
        if (history.width != outW || history.height != outH ||
            history.inputWidth != w || history.inputHeight != h) {
            for (auto& handle : history.history) retireTarget(handle);
            history.width=outW;history.height=outH;
            history.inputWidth=w;history.inputHeight=h;
            history.valid=false;history.current=0;
        }
        for (auto& handle : history.history) {
            if (handle.valid()) continue;
            gfx::TextureDesc desc{};
            desc.width=outW;desc.height=outH;
            desc.format=gfx::PixelFormat::RGBA16F;
            desc.filter=gfx::Filter::Nearest;
            desc.renderTarget=true;
            desc.label="prisma temporal camera history";
            handle=dev.createTexture(desc);
        }
        const int next=1-history.current;
        const gfx::TextureHandle targetHistory=history.history[next];
        const gfx::TextureHandle previousHistory=history.history[history.current];
        if (targetHistory.valid() && previousHistory.valid()) {
            TemporalUniforms uniforms{};
            // Output-space history taps and input-space current neighborhood
            // are distinct when temporal upscaling is enabled.
            put(uniforms.px,1.0f/float(outW),1.0f/float(outH),flip,0);
            put(uniforms.inputPx,1.0f/float(w),1.0f/float(h),
                temporalEffect->get("reconstructionSharpness"),0);
            const bool useHistory=history.valid && previousValid && velocity.tex.valid();
            put(uniforms.p0,
                dev.info().clipDepthZeroToOne?1.0f:0.0f,
                useHistory?1.0f:0.0f,
                temporalEffect->get("historyWeight"),
                temporalEffect->get("reactiveStrength"));
            put(uniforms.sun,
                temporalEffect->get("depthThreshold"),
                temporalEffect->get("motionScale"),
                validReactive?1.0f:0.0f,
                temporalEffect->get("historyConfidence"));
            put(uniforms.jitter,
                history.currentJitterUv.x, history.currentJitterUv.y,
                history.previousJitterUv.x, history.previousJitterUv.y);
            const mat4 inverseViewProj=f.viewProj.inverse();
            std::memcpy(uniforms.inv0,inverseViewProj.m,sizeof inverseViewProj.m);
            std::memcpy(uniforms.prev0,motionView.previousViewProj.m,
                        sizeof motionView.previousViewProj.m);
            gfx::PassDesc pass{};
            pass.colorTarget=targetHistory;
            pass.depthTarget=outputDepth;
            pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
            pass.clearColor[3]=1;
            dev.beginPass(pass);
            dev.setViewport({0,0,outW,outH});
            dev.setPipeline(pipes_[kTemporalResolve]);
            dev.setVertexBuffer(triangle_);
            dev.setUniforms(0,&uniforms,sizeof uniforms);
            dev.setTexture(0,src.tex);
            dev.setTexture(1,useHistory?previousHistory:black_);
            dev.setTexture(2,velocity.tex.valid()?velocity.tex:black_);
            dev.setTexture(3,sceneDepth);
            dev.setTexture(4,validReactive?motionView.reactiveMask.texture:black_);
            dev.draw(3);
            dev.endPass();
            ++stats_.passes;
            history.current=next;
            history.valid=true;
            history.previousJitterUv=history.currentJitterUv;
            src={targetHistory,outW,outH,gfx::PixelFormat::RGBA16F};
            temporalResolved=true;
            stats_.temporalResolved=true;
            stats_.temporalUpscaled=outW>w || outH>h;
            stats_.temporalJittered=history.jitterActive;
            stats_.jitterPixelX=history.currentJitterUv.x*float(w);
            stats_.jitterPixelY=history.currentJitterUv.y*float(h);
        } else history.valid=false;
    } else {
        // Release camera-owned HDR history if its effect is off or the
        // viewport switched to an unsupported quality/format.
        auto it=temporalViews_.find(viewId_);
        if (it!=temporalViews_.end()) {
            for(auto& handle:it->second.history)retireTarget(handle);
            temporalViews_.erase(it);
        }
    }

    // Seed motion history independently of the effect switches. A cut or
    // resize makes the next temporal pass sharp, never a stale reprojection.
    motionView.previousViewProj=f.viewProj;
    motionView.previousCameraPos=f.cameraPos;
    motionView.width=w;motionView.height=h;motionView.valid=true;
    motionView.objectVelocity={};
    motionView.reactiveMask={};

    // ---- composite into the caller's pass (left open)
    CompositeU u{};
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(graphEpoch_<0)graphEpoch_=now;
    u.graphFrame[0]=float(now-graphEpoch_);
    // If a temporal pass fails AFTER jittered scene encoding, present the
    // current frame at stable pixel coordinates. This cancellation does not
    // require an extra target or GPU flush and keeps the HUD untouched.
    if (!temporalResolved && (fallbackJitter.x!=0 || fallbackJitter.y!=0)) {
        u.graphFrame[2]=fallbackJitter.x;
        u.graphFrame[3]=fallbackJitter.y;
        stats_.temporalFallback=true;
    }
    stats_.adaptiveResolution=adaptiveViews_.count(viewId_)!=0;
    stats_.adaptiveHistoryAdmissionRejected=
        viewHistoryAdmissionRejected_.count(viewId_)!=0;
    if (const auto policy=viewQualityPolicies_.find(viewId_);
        policy!=viewQualityPolicies_.end())
        stats_.adaptiveViewPriority=policy->second.priority;
    if (const auto m=adaptivePressure_.find(viewId_);
        m!=adaptivePressure_.end()) {
        stats_.adaptiveKnownGpuBytes=m->second.knownBytes;
        stats_.adaptiveMemoryLimitBytes=m->second.limitBytes;
        stats_.adaptiveMemoryPressure=m->second.overLimit;
        stats_.adaptiveScopedGpuTiming=m->second.scopedGpu;
        stats_.adaptiveGpuMs=m->second.gpuMs;
    }
    stats_.requestedRenderScale=float(w)/float(std::max(1,outW));
    uint32_t mask = 0;
    put(u.px, 1.0f / float(outW), 1.0f / float(outH), flip, seed);
    put(u.grade1, 1, 1, 1, 1);
    put(u.gamma, 1, 1, 1, 0.5f);
    put(u.gain, 1, 1, 1, 0);
    put(u.grade0, 0, 1, 1, 8);
    put(u.filt, hdrFrame_ ? 1.0f : 0.0f, f.lighting.tonemap ? 1.0f : 0.0f, 1, 1);
    for (auto* e : active) {
        switch (e->type) {
        case PrismaType::Pixelate: mask |= kPfPixelate; u.grade1[3] = std::max(1.0f, e->get("size")); break;
        case PrismaType::ChromaticAberration: mask |= kPfCa; u.vig[3] = e->get("intensity"); break;
        case PrismaType::Sharpen: mask |= kPfSharpen; u.bloom[3] = e->get("amount"); break;
        case PrismaType::TiltShift:
            if (tiltTex.valid()) { mask |= kPfTilt; put(u.tilt, e->get("focus"), e->get("width"), e->get("falloff"), 0); }
            break;
        case PrismaType::Bloom:
            if (bloomTex.valid()) {
                mask |= kPfBloom;
                const float* t = e->color("tint");
                const float k = e->get("intensity") / std::max(bloomNorm, 1.0f);
                for (int i = 0; i < 3; ++i) u.bloom[i] = t[i] * k;
            }
            break;
        case PrismaType::SunShafts:
            if (shaftTex.valid()) { mask |= kPfShafts; for (int i = 0; i < 3; ++i) u.shafts[i] = shaftColor[i]; }
            break;
        case PrismaType::ColorGrade: {
            mask |= kPfGrade;
            const float exposure = std::exp2(e->get("exposure"));
            const float ex = hdrFrame_ ? 1.0f : exposure;
            if (hdrFrame_) u.filt[2] = exposure;
            const float t = e->get("temperature"), ti = e->get("tint");
            const float* fl = e->color("filter");
            const float wb[3] = {1.0f + 0.25f * t + 0.1f * ti, 1.0f - 0.2f * ti, 1.0f - 0.25f * t + 0.1f * ti};
            for (int i = 0; i < 3; ++i) u.grade1[i] = ex * wb[i] * fl[i];
            const float* li = e->color("lift");
            const float* ga = e->color("gamma");
            const float* gn = e->color("gain");
            for (int i = 0; i < 3; ++i) { u.lift[i] = li[i]; u.gamma[i] = 1.0f / std::max(ga[i], 0.05f); u.gain[i] = gn[i]; }
            u.grade0[1] = e->get("contrast");
            u.grade0[2] = e->get("saturation");
            break;
        }
        case PrismaType::Lut:
            if (const Lut* l = lut(e->text)) { mask |= kPfLut; u.lut[0] = float(l->n); u.lut[1] = e->get("flipY"); u.shafts[3] = e->get("intensity"); }
            break;
        case PrismaType::Posterize: mask |= kPfPosterize; u.grade0[3] = std::max(2.0f, e->get("levels")); break;
        case PrismaType::Vignette: {
            mask |= kPfVignette;
            u.lift[3] = e->get("intensity");
            u.gamma[3] = e->get("softness");
            const float* c = e->color("color");
            for (int i = 0; i < 3; ++i) u.vig[i] = c[i];
            break;
        }
        case PrismaType::Grain:
            mask |= kPfGrain;
            u.gain[3] = e->get("intensity");
            u.filt[3] = std::max(1.0f, e->get("size"));
            if (e->get("animated") < 0.5f) u.px[3] = 0;
            break;
        case PrismaType::Fade: { mask |= kPfFade; const float* c = e->color("color"); put(u.fade, c[0], c[1], c[2], e->get("amount")); break; }
        case PrismaType::Letterbox: { mask |= kPfLetterbox; const float* c = e->color("color"); put(u.lbox, c[0], c[1], c[2], e->get("amount")); break; }
        default: break;
        }
    }
    // Fixed cinematic EV is a scene-linear multiplier in the existing
    // final HDR composite, BEFORE ACES/sRGB/grade/LUT/Shader Graph.
    // No exposure history, sampled texture, extra sampler or pipeline
    // variant is needed. ColorGrade's artistic exposure composes with it.
    if (manualSelected) u.filt[2] *= std::exp2(manualEV);
    // Camera at the surface: split frame along the waterline (lensWater
    // "waterline"). Underwater optics then apply at full strength below the
    // line only, instead of tinting the whole screen by the soft blend.
    const auto* lensEffect=find(PrismaType::LensWater);
    const bool waterline=lensEffect && lensEffect->get("waterline")>.5f &&
        underwater_.blend>0.001f && quality_!=PrismaQuality::Off;
    if(underwater_.blend>0.001f && quality_!=PrismaQuality::Off) {
        mask|=kPfUnderwater;
        put(u.underwater0,underwater_.tintR,underwater_.tintG,
            underwater_.tintB,waterline?1.f:underwater_.blend);
        const auto budget=prismaUnderwaterBudget(quality_);
        put(u.underwater1,underwater_.absorption,
            underwater_.depthMetres,
            underwater_.distortion*budget.distortion,
            underwater_.scattering*budget.scattering);
        put(u.underwater2,budget.crossing,budget.scattering,
            float(budget.samples),underwater_.surfaceOffset);
        u.graphFrame[1]=underwater_.crossing*budget.crossing;
        ++stats_.effects;
    }
    if(waterline) {
        mask|=kPfWaterline;
        const float lensScale=float(w)/std::max(1,outW);
        put(u.waterline0,underwater_.surfaceOffset,lensEffect->get("waveHeight"),
            lensEffect->get("waveLength"),lensEffect->get("waveSpeed"));
        put(u.waterline1,lensEffect->get("meniscus")*lensScale,lensEffect->get("lensDistance"),
            lensEffect->get("glint"),dev.info().clipDepthZeroToOne?1.f:0.f);
        const auto inv=f.viewProj.inverse();
        for(int n=0;n<4;++n) {
            float* dst=n==0?u.inverseViewProj0:n==1?u.inverseViewProj1:
                       n==2?u.inverseViewProj2:u.inverseViewProj3;
            for(int j=0;j<4;++j)dst[j]=inv.m[n*4+j];
        }
        put(u.underwaterCamera,f.cameraPos.x,f.cameraPos.y,
            f.cameraPos.z,dev.info().clipDepthZeroToOne?1.f:0.f);
    }
    if(const auto* effect=find(PrismaType::LensWater)) {
        const auto it=lensWaterViews_.find(viewId_);
        // Dry camera: whole lens wet. At the surface (waterline) the emerged
        // part already carries drops; the shader masks the covered part.
        if(it!=lensWaterViews_.end()&&(!it->second.submerged||waterline)&&it->second.wetness>0&&effect->get("amount")>0){
            mask|=kPfLensWater;
            const float lensScale=float(w)/std::max(1,outW);
            put(u.lensWater0,effect->get("amount")*it->second.wetness,effect->get("dropSize")*lensScale,effect->get("density"),effect->get("distortion")*lensScale);
            put(u.lensWater1,effect->get("flowSpeed")*lensScale,it->second.age,effect->get("film"),float(h)*outW/(float(std::max(1,outH))*std::max(1,w)));
            put(u.lensWater2,effect->get("streaks"),effect->get("glint"),effect->get("sheetTime"),effect->get("sheet"));
            ++stats_.effects;
        }
    }
    const auto* graphEffect = find(PrismaType::ShaderGraph);
    // Depth sampling is enabled only after the scene pass has ended.
    // ShaderGraph owns tex5+, so avoid conflicting with its inputs.
    const auto depthTex=depthFor(set,w,h);
    const bool depthSafe=(mask & kPfUnderwater) &&
        quality_>=PrismaQuality::Medium && !graphEffect &&
        depthTex.valid() && depthTex != out.depthTarget;
    if(depthSafe) {
        mask |= kPfUnderwaterDepth;
        const auto inv=f.viewProj.inverse();
        for(int n=0;n<4;++n) {
            float* dst=n==0?u.inverseViewProj0:n==1?u.inverseViewProj1:
                       n==2?u.inverseViewProj2:u.inverseViewProj3;
            for(int j=0;j<4;++j)dst[j]=inv.m[n*4+j];
        }
        put(u.underwaterCamera,f.cameraPos.x,f.cameraPos.y,
            f.cameraPos.z,dev.info().clipDepthZeroToOne?1.f:0.f);
    }
    const Json* graphData = graphEffect ? &graphEffect->shaderGraph : nullptr;
    // Serialize the graph once per frame; every composite() lookup below
    // reuses this key instead of dumping the JSON again.
    const std::string graphKey = graphData ? graphData->dump(-1) : std::string();
    std::vector<gfx::TextureHandle> graphImages;
    bool graphInputsReady = true;
    if (graphEffect) {
        // ShaderGraph textures have their own cache/loader. A missing
        // image must not silently become a black graph input, or force
        // Bloom/Grade/LUT to fall all the way back to composite(0).
        // Compile the graph's variant once to populate its texture list;
        // the normal composite cache prevents a repeated GPU build.
        composite(mask, graphData, &graphKey);
        const auto it = graphPaths_.find(graphKey);
        if (it != graphPaths_.end()) {
            for (const auto& path : it->second) {
                graphImages.push_back(graphTexture(path));
                const auto loaded = graphTextures_.find(path);
                if (loaded == graphTextures_.end() || !loaded->second.valid())
                    graphInputsReady = false;
            }
        }
    }
    // Try the authored graph first, including Auto Exposure. On failure,
    // try the SAME graph without exposure; if both fail (or a graph image
    // is absent), preserve the full fixed-effect mask WITHOUT the graph,
    // still with Auto Exposure when supported. Plain composite(0) is the
    // final emergency fallback, NOT the first graph-failure response.
    const uint32_t fixedMask = mask;
    const Json* selectedGraph = graphInputsReady ? graphData : nullptr;
    const auto authoredLayout = PrismaCompositeTextureLayout::plan(
        selectedGraph ? graphImages.size() : 0, automaticExposure.valid());
    if (automaticExposure.valid() && authoredLayout.valid)
        mask |= kPfAutoExposure;
    gfx::PipelineHandle pipe = composite(mask, selectedGraph, &graphKey);
    if (!pipe.valid() && (mask & kPfUnderwaterDepth)) {
        mask &= ~kPfUnderwaterDepth;
        pipe = composite(mask, selectedGraph, &graphKey);
    }
    if (!pipe.valid() && (mask & kPfAutoExposure)) {
        mask &= ~kPfAutoExposure;
        pipe = composite(mask, selectedGraph, &graphKey);
    }
    if (!pipe.valid() && selectedGraph) {
        selectedGraph = nullptr;
        mask = fixedMask;
        if (automaticExposure.valid()) mask |= kPfAutoExposure;
        pipe = composite(mask, nullptr);
        if (!pipe.valid() && (mask & kPfAutoExposure)) {
            mask &= ~kPfAutoExposure;
            pipe = composite(mask, nullptr);
        }
    }
    if (!pipe.valid()) {
        selectedGraph = nullptr;
        mask = 0;
        pipe = composite(0); // last resort still presents the scene
    }
    const bool graphCompositePrepared = graphEffect &&
        selectedGraph != nullptr && pipe.valid();
    // The final sampler slot must describe the selected variant. If a
    // four-image graph failed, the graphless AE variant reads TEX8, NOT
    // the graph variant's TEX9. This also drives the runtime setTexture.
    const auto compositeLayout = PrismaCompositeTextureLayout::plan(
        graphCompositePrepared ? graphImages.size() : 0,
        (mask & kPfAutoExposure) != 0);
    effectReport.shaderGraphTextures = static_cast<int>(graphImages.size());
    effectReport.shaderGraphFallback = graphEffect && !graphCompositePrepared;
    if (manualSelected && pipe.valid()) {
        effectReport.manualExposureEncoded = true;
        effectReport.manualExposureEV = manualEV;
    }
    if (pipe.valid() && (mask & kPfAutoExposure)) {
        effectReport.autoExposureTextureSlot = compositeLayout.exposureSlot;
        effectReport.autoExposureHighlightRejection =
            autoEffect ? autoEffect->get("highlightRejection") : 0.0f;
        effectReport.autoExposureFrozen = exposureFrozenThisFrame;
        effectReport.autoExposureRegionMetered = exposureRegionMeteredThisFrame;
        effectReport.autoExposureBoxMetered =
            exposureRegionMeteredThisFrame && meterRegion.box;
    }
    // TAA and FXAA are mutually exclusive for the same HDR presentation.
    const PrismaEffect* finalAa = hdrFrame_ && !temporalResolved ? find(PrismaType::Fxaa) : nullptr;
    // Do not schedule the HDR final FXAA output pass if the preceding
    // composite could not be created. It would sample unwritten pixels.
    bool aaAfter = pipe.valid() && finalAa && program(kFxaa).valid();
    gfx::PassDesc compositeOut = out;
    Target aaTarget;
    if (aaAfter) {
        hdrFrame_ = false; aaTarget = target(set, kAa, outW, outH); hdrFrame_ = true;
        const auto aaDepth = depthFor(set, outW, outH, true);
        aaAfter = aaTarget.tex.valid() && aaDepth.valid();
        if (aaAfter) { compositeOut.colorTarget = aaTarget.tex; compositeOut.depthTarget = aaDepth; }
    }
    dev.beginPass(compositeOut);
    dev.setViewport({0, 0, outW, outH});
    if (pipe.valid()) {
        dev.setPipeline(pipe);
        dev.setVertexBuffer(triangle_);
        dev.setUniforms(0, &u, sizeof u);
        dev.setTexture(0, src.tex);
        dev.setTexture(1, bloomTex.valid() ? bloomTex : black_);
        dev.setTexture(2, tiltTex.valid() ? tiltTex : black_);
        dev.setTexture(3, shaftTex.valid() ? shaftTex : black_);
        const Lut* l = (mask & kPfLut) ? lut(find(PrismaType::Lut)->text) : nullptr;
        dev.setTexture(4, l ? l->tex : black_);
        if(mask & kPfUnderwaterDepth)dev.setTexture(5,depthTex);
        if (graphCompositePrepared)
            for (size_t i = 0; i < graphImages.size(); ++i)
                dev.setTexture(int(i) + 5, graphImages[i]);
        if (mask & kPfAutoExposure)
            dev.setTexture(compositeLayout.exposureSlot, automaticExposure);
        dev.draw(3);
    }
    ++stats_.passes;
    if (aaAfter) {
        dev.endPass(); dev.beginPass(out); dev.setViewport({0, 0, outW, outH});
        SimpleU au{}; put(au.px, 1.0f / float(outW), 1.0f / float(outH), flip, 0); put(au.p0, finalAa->get("subpixel"), 0, 0, 0);
        dev.setPipeline(pipes_[kFxaa]); dev.setVertexBuffer(triangle_); dev.setUniforms(0, &au, sizeof au);
        dev.setTexture(0, aaTarget.tex); dev.draw(3); ++stats_.passes;
        fxaaEncoded = true;
    }

    // Final GPU-free status snapshot. This records the ACTUAL last encoded
    // resolve for this stable view, not a hypothetical prepared shader or a
    // result from another viewport. GPU completion/pixel validity is unknown.
    const bool compositeEncoded = pipe.valid();
    const auto optIn = [](const char* key) {
        const char* flag = std::getenv(key);
        return flag && std::strcmp(flag, "1") == 0;
    };
    for (const auto& authored : settings_.effects) {
        EffectExecution state{};
        state.type = authored.type;
        state.authored = true;
        if (!enabled_ || !settings_.enabled || !dev.gpuHealthy()) {
            state.reason = "Prisma or GPU disabled";
        } else if (!authored.enabled) {
            state.reason = "effect disabled in camera profile";
        } else if (int(quality_) < int(authored.minQuality)) {
            state.reason = "below effect minimum quality tier";
        } else {
            state.requested = true;
            const auto compositeBit = [&](uint32_t bit) {
                state.pipelinePrepared = compositeEncoded;
                state.encoded = compositeEncoded && (mask & bit) != 0;
                if (!state.encoded)
                    state.reason = "effect omitted from final composite or fallback variant";
            };
            const auto requireHdrFlag = [&](const char* key) {
                if (!hdrFrame_) {
                    state.requested = false;
                    state.reason = "requires linear HDR output";
                } else if (!optIn(key)) {
                    state.requested = false;
                    state.reason = std::string("opt-in disabled: ") + key;
                }
                return state.requested;
            };
            switch (authored.type) {
            case PrismaType::Fxaa:
                state.pipelinePrepared = pipes_[kFxaa].valid();
                state.encoded = fxaaEncoded;
                if (hdrFrame_ && temporalResolved)
                    state.reason = "suppressed by canonical Temporal Resolve";
                else if (!state.encoded)
                    state.reason = state.pipelinePrepared ?
                        "FXAA render target or composite unavailable" : "FXAA pipeline unavailable";
                break;
            case PrismaType::Bloom:
                state.pipelinePrepared = pipes_[kPrefilter].valid() &&
                    pipes_[kDown].valid() && pipes_[kUp].valid();
                compositeBit(kPfBloom);
                state.pipelinePrepared = pipes_[kPrefilter].valid() &&
                    pipes_[kDown].valid() && pipes_[kUp].valid();
                if (!state.encoded) state.reason = !state.pipelinePrepared ?
                    "Bloom GPU pipelines unavailable" : "Bloom target/chain not composited";
                break;
            case PrismaType::TiltShift:
                state.pipelinePrepared = pipes_[kDown].valid() && pipes_[kBlur].valid();
                compositeBit(kPfTilt);
                state.pipelinePrepared = pipes_[kDown].valid() && pipes_[kBlur].valid();
                if (!state.encoded) state.reason = !state.pipelinePrepared ?
                    "tilt/blur GPU pipelines unavailable" : "tilt target not composited";
                break;
            case PrismaType::SunShafts:
                state.pipelinePrepared = pipes_[kShaftsPre].valid() &&
                    pipes_[kShafts].valid();
                compositeBit(kPfShafts);
                state.pipelinePrepared = pipes_[kShaftsPre].valid() &&
                    pipes_[kShafts].valid();
                if (!state.encoded) state.reason = !shaftTex.valid() ?
                    "sun unavailable or shaft target not produced" : "shaft composite failed";
                break;
            case PrismaType::Lut:
                compositeBit(kPfLut);
                if (!state.encoded) state.reason = "LUT missing/invalid or composite unavailable";
                break;
            case PrismaType::AutoExposure:
                if (manualSelected) {
                    // Manual EV is a pre-tonemap scalar in the final HDR
                    // composite. It does not require any AE pipelines or
                    // 1x1 exposure texture and cannot run alongside AE.
                    state.pipelinePrepared = pipe.valid();
                    state.encoded = effectReport.manualExposureEncoded;
                    if (!state.encoded)
                        state.reason = "manual EV final HDR composite unavailable";
                } else if (requireHdrFlag("AIVERSE_PRISMA_AUTO_EXPOSURE")) {
                    state.pipelinePrepared = pipes_[kExposureLog].valid() &&
                        pipes_[kExposureReduce].valid() && pipes_[kExposureAdapt].valid();
                    state.encoded = compositeEncoded && (mask & kPfAutoExposure) != 0;
                    if (!state.encoded) state.reason = !compositeLayout.valid ?
                        "shader graph exceeds four texture bindings" :
                        (!state.pipelinePrepared ? "exposure pipelines unavailable" :
                         "exposure history/target or composite fallback unavailable");
                }
                break;
            case PrismaType::DepthOfField:
                // The legacy DoF pass is allowed in SDR as well when a
                // project explicitly lowers minQuality. Report the actual
                // runtime gate, not the stricter HDR rule of Temporal AA.
                if (!optIn("AIVERSE_PRISMA_DOF")) {
                    state.requested = false;
                    state.reason = "opt-in disabled: AIVERSE_PRISMA_DOF";
                } else {
                    if (authored.get("intensity") <= 0 ||
                        authored.get("maxBlur") <= 0) {
                        state.requested = false;
                        state.reason = "zero defocus intensity/maxBlur";
                    } else {
                        state.pipelinePrepared = pipes_[kDepthOfField].valid();
                        state.encoded = depthOfFieldEncoded;
                        if (!state.encoded) state.reason = state.pipelinePrepared ?
                            "DoF depth/target unavailable" : "DoF pipeline unavailable";
                        else if (dofMetricRequested && !dofMetricEncoded) {
                            // The authored metric mode ran in legacy device
                            // depth because camera near/far was not usable.
                            state.degraded = true;
                            state.reason = "metric focus rejected: invalid camera near/far; legacy depth used";
                        }
                    }
                }
                break;
            case PrismaType::MotionBlur:
                if (requireHdrFlag("AIVERSE_PRISMA_MOTION_BLUR")) {
                    if (authored.get("intensity") <= 0 ||
                        authored.get("maxPixels") <= 0) {
                        state.requested = false;
                        state.reason = "zero shutter intensity/maxPixels";
                    } else {
                        state.pipelinePrepared = pipes_[kMotionVectors].valid() &&
                            pipes_[kMotionBlur].valid();
                        state.encoded = motionBlurEncoded;
                        if (!state.encoded) state.reason = !previousValid ?
                            "no previous valid camera frame (cut/resize/first frame)" :
                            (!state.pipelinePrepared ? "motion pipelines unavailable" :
                             "velocity/depth/target unavailable");
                    }
                }
                break;
            case PrismaType::TemporalResolve:
                if (requireHdrFlag("AIVERSE_PRISMA_TEMPORAL")) {
                    if (quality_ != PrismaQuality::High) {
                        state.requested = false;
                        state.reason = "requires High tier";
                    } else if (authored.get("historyWeight") <= 0) {
                        state.requested = false;
                        state.reason = "zero temporal history weight";
                    } else {
                        state.pipelinePrepared = pipes_[kTemporalResolve].valid();
                        state.encoded = temporalResolved;
                        if (!state.encoded) state.reason = rejectedHistory ?
                            "camera temporal history denied or preflight rejected" :
                            (!state.pipelinePrepared ? "temporal pipeline unavailable" :
                             "temporal history/scene/depth/target unavailable");
                    }
                }
                break;
            case PrismaType::ShaderGraph:
                state.pipelinePrepared = graphCompositePrepared;
                state.encoded = graphCompositePrepared && compositeEncoded;
                if (!state.encoded)
                    state.reason = !graphInputsReady ?
                        "Shader Graph source texture missing or failed to upload; fixed effects used" :
                        "Shader Graph compile/composite failed; fixed effects fallback attempted";
                break;
            case PrismaType::Pixelate: compositeBit(kPfPixelate); break;
            case PrismaType::ChromaticAberration: compositeBit(kPfCa); break;
            case PrismaType::Sharpen: compositeBit(kPfSharpen); break;
            case PrismaType::ColorGrade: compositeBit(kPfGrade); break;
            case PrismaType::Posterize: compositeBit(kPfPosterize); break;
            case PrismaType::Vignette: compositeBit(kPfVignette); break;
            case PrismaType::Grain: compositeBit(kPfGrain); break;
            case PrismaType::LensWater: compositeBit(kPfLensWater); break;
            case PrismaType::Fade: compositeBit(kPfFade); break;
            case PrismaType::Letterbox: compositeBit(kPfLetterbox); break;
            default:
                state.requested = false;
                state.reason = "effect has no runtime execution mapping";
                break;
            }
        }
        effectReport.effects.push_back(std::move(state));
    }
}

} // namespace aiverse::render
