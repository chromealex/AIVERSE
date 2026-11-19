#include "Renderer3D.h"
#include "Shaders.h"
#include "SelectionOutlineShaders.h"
#include "NereidWaterReflectionPolicy.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>

namespace aiverse::render {

namespace {
constexpr float kPi = 3.14159265358979f;

struct DrawUniforms { float model[16]; float color[4]; float params[4]; float waterRipples[64]; float waterWakeShapes[64]; float waterWind[4]; float waterHydraulics[4]; float waterRapidColor[4]; };
struct FrameUniforms {
    float viewProj[16];
    float lightViewProj[16];
    float invViewProj[16];
    float sunDir[4], sunColor[4], skyColor[4], groundColor[4], fog[4], camPos[4], shadow[4], quality[4];
    float trace0[4], trace1[4], trace2[4], trace3[4];   // Shaders.h: traced terrain shadows + SSR
};
void put3(float* d, float3 v, float w) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = w; }
} // namespace

// ============================== Renderer3D ==============================

ShadowQuality Renderer3D::defaultQualityFor(const gfx::Device& dev, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_SHADOWS")) { // testing override: off|low|medium|high
        std::string v = q;
        return v == "off" ? ShadowQuality::Off : v == "low" ? ShadowQuality::Low : v == "high" ? ShadowQuality::High : ShadowQuality::Medium;
    }
    const std::string& n = dev.info().name;
    if (n.find("llvmpipe") != std::string::npos || n.find("SwiftShader") != std::string::npos) return ShadowQuality::Low;
    if (mobile) {
        // Old/weak mobile GPUs get the cheapest shadows; everything else medium.
        for (const char* weak : {"Mali-4", "Mali-T", "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "PowerVR SGX", "PowerVR Rogue GE"})
            if (n.find(weak) != std::string::npos) return ShadowQuality::Low;
        return ShadowQuality::Medium;
    }
    return ShadowQuality::High;
}

bool Renderer3D::init(gfx::Device& dev) {
    checkGpuDevice(dev);
    // Reinitializing the same instance without shutdown would overwrite live
    // GPU handles and the retirement owners borrowed by LightSystem.
    if(dev_)throw std::logic_error("Renderer3D init: already initialized; call shutdown first");
    if(programRetirement_ && programRetirement_->pendingBatches()!=0)
        throw std::logic_error("Renderer3D cannot reinitialize with pending GPU programs");
    if(targetRetirement_ && targetRetirement_->pendingPairs()!=0)
        throw std::logic_error("Renderer3D cannot reinitialize with pending GPU targets");
    programRetirement_=std::make_unique<GpuProgramRetirement>(
        dev,gpuDeviceLease_);
    targetRetirement_=std::make_unique<OffscreenTargetRetirement>(
        dev,gpuDeviceLease_);
    lights.setTargetRetirement(targetRetirement_.get());
    dev_ = &dev;

    // Allocate as a single transaction. An unsuccessful shader, buffer,
    // texture, LightSystem or trace setup must not leave a half-initialized
    // renderer that a host can mistakenly render or initialize again.
    // Rollback requires a real GPU idle barrier, even for async backends.
    bool initialized = false;
    try {
        initialized = [&]() -> bool {
            gfx::ShaderDesc ls; ls.glslVertex = shaders::kLitVS; ls.glslFragment = shaders::kLitFS; ls.msl = shaders::kLitMSL;
            ls.uniformSlots = 3; ls.textureSlots = 6; ls.label = "lit"; // U2/tex1/tex2: Lights (tex2 water reflection); tex3 height field, tex4/tex5 scene (trace)
            litShader_ = dev.createShader(ls);
            gfx::ShaderDesc ss; ss.glslVertex = shaders::kShadowVS; ss.glslFragment = shaders::kShadowFS; ss.msl = shaders::kShadowMSL;
            ss.uniformSlots = 2; ss.label = "shadow";
            shadowShader_ = dev.createShader(ss);
            gfx::ShaderDesc cs; cs.glslVertex = shaders::kColorVS; cs.glslFragment = shaders::kColorFS; cs.msl = shaders::kColorMSL;
            cs.uniformSlots = 2; cs.label = "color";
            colorShader_ = dev.createShader(cs);
            if (!litShader_.valid() || !shadowShader_.valid() || !colorShader_.valid()) return false;

            gfx::PipelineDesc mp;
            mp.shader = litShader_;
            mp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            mp.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
            mp.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
            mp.attrCount = 3; mp.stride = sizeof(MeshVertex);
            mp.indexType = gfx::IndexType::UInt32;
            litPipe_ = dev.createPipeline(mp);
            mp.blend = gfx::BlendMode::Alpha; mp.depthWrite = false;
            litBlendPipe_ = dev.createPipeline(mp);

            gfx::PipelineDesc sp;
            sp.shader = shadowShader_;
            sp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            sp.attrCount = 1; sp.stride = sizeof(MeshVertex);
            sp.indexType = gfx::IndexType::UInt32;
            sp.depthOnly = true;
            sp.cull = gfx::CullMode::Back;   // models are closed meshes; back faces would self-shadow less but cost more
            sp.depthBiasConstant = 2.0f; sp.depthBiasSlope = 2.5f;
            shadowPipe_ = dev.createPipeline(sp);

            gfx::PipelineDesc lp;
            lp.shader = colorShader_;
            lp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
            lp.attrs[1] = {1, gfx::VertexFormat::UByte4N, 12};
            lp.attrCount = 2; lp.stride = sizeof(ColorVertex);
            lp.indexType = gfx::IndexType::None;
            lp.primitive = gfx::Primitive::Lines;
            lp.cull = gfx::CullMode::None;
            lp.blend = gfx::BlendMode::Alpha;
            lp.depthWrite = false;
            linePipe_ = dev.createPipeline(lp);
            lp.primitive = gfx::Primitive::Triangles;
            triPipe_ = dev.createPipeline(lp);

            gfx::BufferDesc vb; vb.usage = gfx::Usage::Dynamic; vb.size = 64 * 1024;
            lineVB_ = dev.createBuffer(vb);
            triVB_ = dev.createBuffer(vb);
            setShadowQuality(quality_);
            initModels();
            if (!lights.init(dev)) return false;
            if (!initTrace()) return false;
            initSelectionOutline();  // optional editor effect: failure must not break the game renderer
            return litPipe_.valid() && litBlendPipe_.valid() &&
                   shadowPipe_.valid() && linePipe_.valid() && triPipe_.valid() &&
                   lineVB_.valid() && triVB_.valid() && white_.valid() &&
                   black_.valid() && flatNormal_.valid() &&
                   (quality_ == ShadowQuality::Off || shadowMap_.valid());
        }();
    } catch (...) {
        // If idle/cleanup is impossible, shutdown throws and preserves the
        // live owner state instead of freeing potentially in-flight handles.
        shutdown();
        throw;
    }
    if(!initialized)shutdown();
    return initialized;
}

void Renderer3D::retireRenderTargets(gfx::TextureHandle color,
                                     gfx::TextureHandle depth) {
    if(!color.valid() && !depth.valid())return;
    if(targetRetirement_) {
        targetRetirement_->retire(color,depth);
        return;
    }
    // The shared target owner exists throughout normal Renderer3D use.
    // The fallback is for exceptional setup/teardown only. Never destroy
    // GPU-visible resources without a real idle proof, even with no lease.
    if(!dev_) {
        ++unleasedTargetOrphans_;
        return;
    }
    try {
        if(!dev_->waitForResourceIdle()) {
            ++unleasedTargetIdleFailures_;
            ++unleasedTargetOrphans_;
            return;
        }
        if(color.valid())dev_->destroyTexture(color);
        if(depth.valid())dev_->destroyTexture(depth);
    }catch(...) {
        ++unleasedTargetOrphans_;
    }
}

void Renderer3D::setShadowQuality(ShadowQuality q) {
    if(!dev_) {quality_=q;return;}
    checkGpuDevice(*dev_);
    int size=q==ShadowQuality::High?highShadowSize_:
             q==ShadowQuality::Medium?1536:1024;
    if(q==ShadowQuality::Off)size=0;
    if(size==shadowSize_ && shadowMap_.valid()) {
        quality_=q;return;
    }
    gfx::TextureDesc td;
    td.width=td.height=std::max(size,16);
    td.format=gfx::PixelFormat::Depth;
    td.renderTarget=true;
    td.compare=false;
    td.filter=gfx::Filter::Nearest;
    auto replacement=dev_->createTexture(td);
    if(!replacement.valid())return; // preserve previous valid shadow target
    retireRenderTargets(shadowMap_);
    shadowMap_=replacement;
    shadowSize_=size;
    quality_=q;
}

void Renderer3D::shutdown() {
    if (!dev_) return;
    checkGpuDevice(*dev_);
    // Live renderer shaders and buffers also require a completed GPU frame.
    if(!dev_->waitForResourceIdle())
        throw std::runtime_error("Renderer3D shutdown: GPU idle barrier failed");
    // Successful full-device idle is required when shader cache generations
    // are still pending. Failed completion cannot silently discard them.
    if(programRetirement_ && !programRetirement_->drain())
        throw std::runtime_error(
            "Renderer3D shutdown: pending GPU shader/pipeline completion");
    gpuModelCommands_.release(*dev_);
    persistentCull_.release(*dev_);
    if (persistentUpload_) {
        persistentUpload_->release();
        persistentUpload_.reset();
    }
    persistentIds_.clear();
    persistentScene_ = {};
    shutdownModels();
    lights.shutdown();
    lights.setTargetRetirement(nullptr);
    shutdownTrace();
    retireRenderTargets(shadowMap_);
    shadowMap_={};
    if(lineVB_.valid())dev_->destroyBuffer(lineVB_);
    if(triVB_.valid())dev_->destroyBuffer(triVB_);
    lineVB_=triVB_={};
    retireRenderTargets(sceneSelectionMask_);
    sceneSelectionMask_={};
    if (sceneSelectionTriangle_.valid()) dev_->destroyBuffer(sceneSelectionTriangle_);
    sceneSelectionTriangle_={};
    if (sceneSelectionPrimitiveMaskPipe_.valid())
        dev_->destroyPipeline(sceneSelectionPrimitiveMaskPipe_);
    sceneSelectionPrimitiveMaskPipe_={};
    for (auto p : {sceneSelectionEdgePipe8_, sceneSelectionEdgePipeHdr_})
        if (p.valid()) dev_->destroyPipeline(p);
    sceneSelectionEdgePipe8_=sceneSelectionEdgePipeHdr_={};
    if (sceneSelectionEdgeShader_.valid())
        dev_->destroyShader(sceneSelectionEdgeShader_);
    sceneSelectionEdgeShader_={};
    for (auto p : {litPipe_, litBlendPipe_, shadowPipe_, linePipe_, triPipe_})
        if(p.valid())dev_->destroyPipeline(p);
    for (auto s : {litShader_, shadowShader_, colorShader_})
        if(s.valid())dev_->destroyShader(s);
    litPipe_=litBlendPipe_=shadowPipe_=linePipe_=triPipe_={};
    litShader_=shadowShader_=colorShader_={};
    shadowSize_=0;
    if(targetRetirement_ && !targetRetirement_->drain())
        throw std::runtime_error(
            "Renderer3D shutdown: pending GPU render target completion");
    targetRetirement_.reset();
    programRetirement_.reset();
    dev_ = nullptr;
}

void Renderer3D::beginScene(const RTSCamera& cam, int fbW, int fbH) {
    if(dev_)checkGpuDevice(*dev_);
    static const bool enablePersistentScene = [] {
        const char* value = std::getenv("AIVERSE_PERSISTENT_GPU_SCENE");
        return value && std::strcmp(value, "1") == 0;
    }();
    persistentSceneEnabled_ = enablePersistentScene;
    if (persistentSceneEnabled_) ++persistentSceneEpoch_;
    bakedLightmapped_ = bakedCountLm_; bakedProbed_ = bakedCountPr_; bakedCountLm_ = bakedCountPr_ = 0;   // agent-lightbake
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(graphEpoch_ < 0) graphEpoch_ = now;
    graphTime_ = float(now - graphEpoch_);
    sceneW_ = fbW; sceneH_ = fbH; // viewport of the main pass (screen or an offscreen target of this size)
    float aspect = fbH > 0 ? float(fbW) / float(fbH) : 1.0f;
    viewProj_ = cam.proj(aspect, dev_->info().clipDepthZeroToOne) * cam.view();
    invViewProj_ = viewProj_.inverse();
    camPos_ = cam.eye();
    camFwd_ = normalize(cam.target - camPos_);
    cam_ = cam;
    ssrThisFrame_ = false;
    reflectionPlaneValid_ = false;
    reflectionPlaneY_ = 0.f;
    items_.clear(); lines_.clear(); tris_.clear(); custom_.clear(); customOpaque_.clear(); customShadow_.clear();
    modelItems_.clear(); jointData_.clear(); morphData_.clear(); modelStats_ = {};
    lodDebugSample_ = {}; // selected instance is tracked per viewport beginScene
    // ModelItem pointers are valid only within this scene; never reuse their
    // material identity across frames, asset reloads or another viewport.
    materialStateIds_.clear();
    materialHashCandidates_.clear();
    materialRepresentatives_.clear();
    materialItemIds_.clear();
    geometryStats_ = {}; profile_ = {}; geometryMainView_ = true;
    RTSCamera shadowCamera = cam; shadowCamera.jitter = {};
    computeShadowMatrix(shadowCamera, aspect);
    geometryShadowSize_=shadowSize_;
    lights.begin(cam, aspect);
}

void Renderer3D::computeShadowMatrix(const RTSCamera& cam, float aspect) {
    // Fit an orthographic light frustum around the part of the ground the camera sees.
    float3 pts[8];
    int n = 0;
    const float w = 1000.0f, h = w / aspect;
    const float sx[4] = {0, w, 0, w}, sy[4] = {0, 0, h, h};
    for (int i = 0; i < 4; ++i) {
        float3 g;
        if (!cam.screenToGround(sx[i], sy[i], w, h, g) || length(g - cam.target) > cam.distance * 3.0f) {
            float3 o, d;
            cam.screenRay(sx[i], sy[i], w, h, o, d);
            float3 flatD = normalize(float3{d.x, 0, d.z});
            g = cam.target + flatD * (cam.distance * 1.6f);
        }
        pts[n++] = g;
        pts[n++] = g + float3{0, 5.0f, 0}; // tall objects
    }
    if (hasShadowBounds_) {
        n=0;
        for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
            pts[n++]={x?shadowBoundsMax_.x:shadowBoundsMin_.x,y?shadowBoundsMax_.y:shadowBoundsMin_.y,z?shadowBoundsMax_.z:shadowBoundsMin_.z};
    }
    const float3 center=hasShadowBounds_?(shadowBoundsMin_+shadowBoundsMax_)*.5f:cam.target;
    float3 L = normalize(lighting.sunDir);
    float3 up = std::fabs(L.y) > 0.99f ? float3{0, 0, 1} : float3{0, 1, 0};
    mat4 view = mat4::lookAt(center + L * 60.0f, center, up);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int i = 0; i < n; ++i) {
        float3 p = view.transformPoint(pts[i]);
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }
    // texel snapping keeps shadow edges stable while the camera pans
    float size = std::max(maxX - minX, maxY - minY) + 2.0f;
    float texel = size / float(std::max(shadowSize_, 16));
    shadowTexelWorld_ = texel;
    shadowExtent_ = size;
    float cx = std::floor((minX + maxX) * 0.5f / texel) * texel, cy = std::floor((minY + maxY) * 0.5f / texel) * texel;
    mat4 proj = mat4::ortho(cx - size * 0.5f, cx + size * 0.5f, cy - size * 0.5f, cy + size * 0.5f,
                            -maxZ - 40.0f, -minZ + 10.0f, dev_->info().clipDepthZeroToOne);
    lightViewProj_ = proj * view;
}

void Renderer3D::drawMesh(const Mesh& m, const mat4& model, float4 color, uint32_t flags, float specular, float emissive) {
    items_.push_back({&m, model, color, flags, specular, emissive});
    // Specialized water/selection draw variants carry extra state that a
    // plain mesh packet cannot replay; skip those until their own packets exist.
    if(geometryPacketTap_ && !(flags&kDrawWaterSurface))
        geometryPacketTap_({CameraGeometryCommand::Kind::Mesh,&m,model,
                            color,flags,specular,emissive});
}
void Renderer3D::drawNereidRippleMesh(const Mesh& mesh,const mat4& model,
        float4 color,float specular,float refraction,
        const std::array<float4,16>& ripples,
        uint32_t firstIndex,uint32_t indexCount,
        const std::array<float4,16>& wakeShapes,float4 wind,
        float4 hydraulics,float4 rapidColor) {
    drawMesh(mesh,model,color,kDrawNoCastShadow|kDrawWaterSurface,
             specular,refraction);
    if(!items_.empty()) {
        items_.back().waterRipples=ripples;
        items_.back().waterWakeShapes=wakeShapes;
        items_.back().waterWind=wind;
        items_.back().waterHydraulics=hydraulics;
        items_.back().waterRapidColor=rapidColor;
        items_.back().waterFirstIndex=firstIndex;
        items_.back().waterIndexCount=indexCount;
    }
}

void Renderer3D::drawSelectionMesh(const Mesh& mesh, const mat4& model,
                                   float4 tint, float4 selection) {
    drawMesh(mesh, model, tint);
    if (!items_.empty()) items_.back().selection = selection;
}


void Renderer3D::line(float3 a, float3 b, uint32_t c) {
    lines_.push_back({a,c});lines_.push_back({b,c});
    if(geometryPacketTap_) {
        CameraGeometryCommand packet;
        packet.kind=CameraGeometryCommand::Kind::Line;
        packet.a=a;packet.b=b;packet.rgba=c;
        geometryPacketTap_(packet);
    }
}
void Renderer3D::triangle(float3 a, float3 b, float3 c, uint32_t col) {
    tris_.push_back({a,col});tris_.push_back({b,col});tris_.push_back({c,col});
    if(geometryPacketTap_) {
        CameraGeometryCommand packet;
        packet.kind=CameraGeometryCommand::Kind::Triangle;
        packet.a=a;packet.b=b;packet.c=c;packet.rgba=col;
        geometryPacketTap_(packet);
    }
}

void Renderer3D::quadXZ(float3 a, float3 b, float w, uint32_t c, float y) {
    float3 d = b - a; d.y = 0;
    float len = length(d);
    if (len < 1e-5f) return;
    float3 side = float3{-d.z, 0, d.x} * (w * 0.5f / len);
    a.y = b.y = y;
    triangle(a - side, b - side, b + side, c);
    triangle(a - side, b + side, a + side, c);
}

void Renderer3D::circleXZ(float3 c, float r, uint32_t col, int seg, float y) {
    for (int i = 0; i < seg; ++i) {
        float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
        line({c.x + std::cos(a0) * r, y, c.z + std::sin(a0) * r}, {c.x + std::cos(a1) * r, y, c.z + std::sin(a1) * r}, col);
    }
}

void Renderer3D::uploadFrameUniforms(bool forShadow) {
    FrameUniforms f{};
    std::memcpy(f.viewProj, viewProj_.m, sizeof f.viewProj);
    std::memcpy(f.lightViewProj, lightViewProj_.m, sizeof f.lightViewProj);
    std::memcpy(f.invViewProj, invViewProj_.m, sizeof f.invViewProj);
    put3(f.sunDir, normalize(lighting.sunDir), lighting.sunIntensity);
    // trace tier: terrain shadows march the height field; SSR when the scene targets are bound this frame
    if (traceActive() && !forShadow) {
        const HeightField& h = heightField_;
        f.trace0[0] = h.originX; f.trace0[1] = h.originZ;
        f.trace0[2] = 1.0f / std::max(h.extentX, 1e-3f); f.trace0[3] = 1.0f / std::max(h.extentZ, 1e-3f);
        f.trace1[0] = h.heightMin; f.trace1[1] = h.heightRange;
        f.trace1[2] = std::max(lighting.terrainShadowDistance, 2.0f);
        f.trace1[3] = std::max(lighting.terrainShadowSoftness, 0.005f);
    }
    put3(f.trace2, camFwd_, ssrThisFrame_ && !forShadow ? (modelReflections_ ? 2.0f : 1.0f) : 0.0f);
    f.trace3[0] = 48.0f;    // SSR march distance (world)
    f.trace3[1] = 0.6f;     // thickness: a ray passing further behind a surface is a miss (thin objects)
    f.trace3[2] = 16.0f;    // steps (+4 refinement)
    f.trace3[3] = 0.08f;    // terrain shadow ray bias above the receiver (avoids self-shadowing on slopes)
    put3(f.sunColor, lighting.sunColor, 1);
    put3(f.skyColor, lighting.skyColor, lighting.tonemap ? 1.0f : 0.0f);   // w: tonemap switch of the lit shaders
    put3(f.groundColor, lighting.groundColor, lighting.srgbModels ? 2.0f : 1.0f);   // w: model output encoding (2 = sRGB)
    put3(f.fog, lighting.fogColor, lighting.fogDensity);
    put3(f.camPos, camPos_, graphTime_);
    const bool zeroToOne = dev_->info().clipDepthZeroToOne;
    f.shadow[0] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
    f.shadow[1] = zeroToOne ? 1.0f : 0.5f;
    f.shadow[2] = zeroToOne ? 0.0f : 0.5f;
    f.shadow[3] = 1.0f / float(std::max(shadowSize_, 16));
    // More PCF samples preserve fine shadow silhouettes in wide penumbrae.
    // Low stays at four taps; medium/high retain the nine-sample blocker search.
    f.quality[0] = quality_ == ShadowQuality::High ? 24.0f : quality_ == ShadowQuality::Medium ? 12.0f : 4.0f;
    {
        // Maximum search/filter radius; PCSS can narrow contacts to half a texel.
        float radiusWorld = std::max(lighting.shadowSoftness, shadowTexelWorld_ * 1.5f);
        // Low (4 fixed taps): a wide radius would show 4 offset copies, keep it around two texels
        if (quality_ == ShadowQuality::Low) radiusWorld = std::min(radiusWorld, shadowTexelWorld_ * 2.0f);
        f.quality[2] = radiusWorld / std::max(shadowExtent_, 1.0f);
        // Keep receiver displacement tied to resolution, not penumbra width: thin folds must not detach.
        f.quality[3] = shadowTexelWorld_ * 1.5f;
    }
    // Fractional tap count encodes temporal PCSS sampling without changing U1 layout.
    // This flag is active only for the HDR ScreenLighting TAA path; static PCF stays stable elsewhere.
    if (!forShadow && quality_ >= ShadowQuality::Medium && post_ && post_->temporalShadowResolve())
        f.quality[0] += 0.5f;
    f.quality[1] = quality_ == ShadowQuality::Off || forShadow ? 0.0f : lighting.shadowContactHardening ? 2.0f : 1.0f;
    dev_->setUniforms(1, &f, sizeof f);
}

void Renderer3D::bindLitFrame() {
    uploadFrameUniforms(false);
    dev_->setTexture(0, shadowMap_);
    lights.bind();
}

void Renderer3D::bindTraceTextures() {
    bindTrace(3);
    bindSceneTextures(4, 5);
}

void Renderer3D::resolvePostFx() {
    if(dev_)checkGpuDevice(*dev_);
    if (!post_) return;
    PostProcess* post = post_;
    post_ = nullptr;
    dev_->endPass();
    PostFrame f{viewProj_, camPos_, lighting, sceneW_ > 0 ? sceneW_ : dev_->framebufferWidth(), sceneH_ > 0 ? sceneH_ : dev_->framebufferHeight(),
                quality_ != ShadowQuality::Off ? shadowMap_ : gfx::TextureHandle{}, lightViewProj_};
    post->resolve(*dev_, postOut_, f);
    hdrFrame_ = false;
}

// Distinct, stable high-contrast colour for each draw call. Instanced draws
// intentionally consume one ID, not one ID per instance.
float4 Renderer3D::drawCallDebugColor(uint32_t drawId) {
    const float h = std::fmod(float(drawId) * 0.61803398875f, 1.0f) * 6.0f;
    const float x = 0.76f * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = .76f; g = x; }
    else if (h < 2) { r = x; g = .76f; }
    else if (h < 3) { g = .76f; b = x; }
    else if (h < 4) { g = x; b = .76f; }
    else if (h < 5) { r = x; b = .76f; }
    else { r = .76f; b = x; }
    return {r + .17f, g + .17f, b + .17f, 1.0f};
}

void Renderer3D::render(const gfx::PassDesc& screenPass, PostProcess* post) {
    if(dev_)checkGpuDevice(*dev_);
    drawCalls_ = 0;
    // Synchronize persistent scene staging before any rendering passes.
    // Model shader bindings and GPU culling remain on the legacy path.
    syncPersistentModels();
    // GPU command generation must complete encoding before render passes.
    // Metal guarantees command order between compute and render encoders.
    prepareGpuModelDraws();
    prepareOcclusion();
    // ---- shadow pass
    if (quality_ != ShadowQuality::Off && shadowMap_.valid()) {
        gfx::PassDesc sp;
        sp.depthTarget = shadowMap_;
        sp.clearColorEnabled = false;
        sp.clearDepthEnabled = true;
        dev_->beginPass(sp);
        dev_->setViewport({0, 0, shadowSize_, shadowSize_});
        dev_->setPipeline(shadowPipe_);
        uploadFrameUniforms(true);
        for (auto& it : items_) {
            if (it.flags & (kDrawNoCastShadow | kDrawUnlit) || it.color.w < 0.99f) continue;
            DrawUniforms u{};
            std::memcpy(u.model, it.model.m, sizeof u.model);
            dev_->setUniforms(0, &u, sizeof u);
            dev_->setVertexBuffer(it.mesh->vb);
            dev_->setIndexBuffer(it.mesh->ib);
            dev_->drawIndexed(it.mesh->indexCount);
            ++drawCalls_;
        }
        renderModels(0);
        for (auto& fn : customShadow_) fn(*dev_);
        dev_->endPass();
    }

    renderSpotShadow();   // High: the main spot light's shadow map (Renderer3DReflect.cpp)
    lights.renderMap();


    // ---- main pass (left open for the caller's UI; with a post hook: the hook's scene target)
    post_ = post && post->active() ? post : nullptr;
    postOut_ = screenPass;
    const int sw = sceneW_ > 0 ? sceneW_ : dev_->framebufferWidth(), sh = sceneH_ > 0 ? sceneH_ : dev_->framebufferHeight();
    const gfx::PassDesc mainPass = post_ ? post_->scenePass(screenPass, sw, sh) : screenPass;
    hdrFrame_ = post_ && post_->linearScene();
    lights.setLinearScene(hdrFrame_);
    renderReflection();
    // Exclude shadow, probe and reflection passes from the visible-colour ID range.
    debugMainDrawId_ = 0;
    // trace tier + water: the opaque scene goes into colour + depth textures first (SSR reads them),
    // then it is blitted (with depth) into the main pass and the rest is drawn on top
    bool capture = modelReflections_ && hdrFrame_ && traceActive();
    if (traceActive()) {
        for (const auto& it : items_)
            if (it.color.w < 0.99f && it.specular >= 0.85f && !(it.flags & kDrawUnlit)) { capture = true; break; }
        if (capture) capture = beginSceneCapture(sw, sh);
    }
    if (!capture) dev_->beginPass(mainPass);
    dev_->setViewport({0, 0, sw, sh});
    for (auto& fn : customOpaque_) fn(*dev_);
    dev_->setPipeline(litPipe_);
    uploadFrameUniforms(false);
    dev_->setTexture(0, shadowMap_);
    lights.bind(2, 1, 2);
    bindTraceTextures();
    auto drawItem = [&](const DrawItem& it) {
        DrawUniforms u{};
        std::memcpy(u.model, it.model.m, sizeof u.model);
        u.color[0] = it.color.x; u.color[1] = it.color.y; u.color[2] = it.color.z; u.color[3] = it.color.w;
        // Shader -1: reflective NEREID water. -2: NEREID water at a
        // different elevation than the single planar reflection texture.
        // Keep waves and SSR on secondary surfaces, but skip the wrong plane.
        const bool markedWater=(it.flags & kDrawWaterSurface)!=0;
        const bool otherPlane=markedWater && reflectionPlaneValid_ &&
            !sameWaterReflectionPlane(
                it.model.transformPoint((it.mesh->boundsMin+
                                         it.mesh->boundsMax)*0.5f).y,
                reflectionPlaneY_);
        u.params[0] = markedWater ?
                      ((it.flags & kDrawNereidCascade) ? -3.f :
                       (otherPlane ? -2.f : -1.f)) :
                      ((it.flags & kDrawUnlit) ? 1.f : 0.f);
        u.params[1] = it.specular;
        u.params[2] = (it.flags & kDrawNoReceiveShadow) ? 0.f : 1.f;
        u.params[3] = it.emissive;
        std::memcpy(u.waterRipples,it.waterRipples.data(),sizeof u.waterRipples);
        std::memcpy(u.waterWakeShapes,it.waterWakeShapes.data(),sizeof u.waterWakeShapes);
        std::memcpy(u.waterWind,&it.waterWind,sizeof u.waterWind);
        std::memcpy(u.waterHydraulics,&it.waterHydraulics,
                    sizeof u.waterHydraulics);
        std::memcpy(u.waterRapidColor,&it.waterRapidColor,
                    sizeof u.waterRapidColor);
        if (studioDebugMode_ == 2 || studioDebugMode_ == 4 || studioDebugMode_ == 9)
            u.params[0] = 10.0f + float(studioDebugMode_);
        if (drawCallDebug_) {
            const float4 c = drawCallDebugColor(++debugMainDrawId_);
            u.color[0] = c.x; u.color[1] = c.y; u.color[2] = c.z; u.color[3] = 1.0f;
            u.params[0] = 2.0f; // solid diagnostic output in GLSL and Metal
        }
        dev_->setUniforms(0, &u, sizeof u);
        dev_->setVertexBuffer(it.mesh->vb);
        dev_->setIndexBuffer(it.mesh->ib);
        const uint32_t drawCount=it.waterIndexCount?it.waterIndexCount:it.mesh->indexCount;
        dev_->drawIndexed(drawCount,it.waterFirstIndex);
        ++drawCalls_;
        // drawItem is only used by the current camera's colour pass.
        profile_.cameraMeshTriangles += uint64_t(it.waterIndexCount?it.waterIndexCount:it.mesh->indexCount) / 3;
    };
    for (auto& it : items_) if (it.color.w >= 0.99f) drawItem(it);
    // The per-phase profile also includes reflection/SSR replay. Snapshot
    // only the *first* main-camera opaque pass so a triangle is not counted
    // twice when the same visible surface is drawn for another target.
    const uint32_t opaqueStart = profile_.triangles[1];
    renderModels(1);
    profile_.cameraModelTriangles += uint32_t(profile_.triangles[1] - opaqueStart);
    if (!sceneSelectionOutline_) renderModels(3); // legacy game outline; editor uses a depth-tested mask
    if (capture) {
        endSceneCapture(mainPass, sw, sh); dev_->setViewport({0, 0, sw, sh});
        // The blit binds scene color at slot 0; model shaders expect the sun depth map there.
        dev_->setTexture(0, shadowMap_);
        // Read the first opaque pass while shading into a different target (no feedback).
        if (modelReflections_ && hdrFrame_) { uploadFrameUniforms(false); renderModels(1); }
    }
    // translucent meshes (water, ghosts) before overlays so that overlays (fog of war, markers) stay on top
    bool any = false;
    for (auto& it : items_) {
        if (it.color.w >= 0.99f) continue;
        if (!any) { dev_->setPipeline(litBlendPipe_); uploadFrameUniforms(false); dev_->setTexture(0, shadowMap_); lights.bind(2, 1, 2); bindTraceTextures(); any = true; }
        drawItem(it);
    }
    const uint32_t blendedStart = profile_.triangles[2];
    renderModels(2);
    profile_.cameraModelTriangles += uint32_t(profile_.triangles[2] - blendedStart);
    renderModels(4);   // transparent objects (kDrawTransparent) write their depth now: later ground layers stay under them
    lights.drawHalos(viewProj_);   // additive glows, depth-tested against the scene
    // custom renderers (sprites) after translucent meshes: they don't write depth, so water drawn
    // later would tint them; overlays (fog of war, markers) still come on top
    for (auto& fn : custom_) fn(*dev_);
    if (!tris_.empty()) {
        dev_->updateBuffer(triVB_, tris_.data(), tris_.size() * sizeof(ColorVertex));
        dev_->setPipeline(triPipe_);
        uploadFrameUniforms(false);
        dev_->setVertexBuffer(triVB_);
        dev_->draw(uint32_t(tris_.size()));
        ++drawCalls_;
        profile_.cameraOverlayTriangles += uint64_t(tris_.size()) / 3;
    }
    if (!lines_.empty()) {
        dev_->updateBuffer(lineVB_, lines_.data(), lines_.size() * sizeof(ColorVertex));
        dev_->setPipeline(linePipe_);
        uploadFrameUniforms(false);
        dev_->setVertexBuffer(lineVB_);
        dev_->draw(uint32_t(lines_.size()));
        ++drawCalls_;
    }
    if (sceneSelectionOutline_) renderSelectionOutline(mainPass, sw, sh);
}


bool Renderer3D::initSelectionOutline() {
    if (!dev_) return false;
    gfx::ShaderDesc shader;
    shader.glslVertex = selection_outline::kVertex;
    shader.glslFragment = selection_outline::kFragment;
    shader.msl = selection_outline::kMetal;
    shader.textureSlots = 1;
    shader.uniformSlots = 1;
    shader.label = "scene selection edge";
    sceneSelectionEdgeShader_ = dev_->createShader(shader);
    if (!sceneSelectionEdgeShader_.valid()) return false;
    gfx::PipelineDesc pd;
    pd.shader = sceneSelectionEdgeShader_;
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 2 * sizeof(float);
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.blend = gfx::BlendMode::Alpha;
    pd.depthTest = false;
    pd.depthWrite = false;
    pd.colorFormat = gfx::PixelFormat::RGBA8;
    pd.label = "scene selection edge RGBA8";
    sceneSelectionEdgePipe8_ = dev_->createPipeline(pd);
    pd.colorFormat = gfx::PixelFormat::RGBA16F;
    pd.label = "scene selection edge HDR";
    sceneSelectionEdgePipeHdr_ = dev_->createPipeline(pd);
    // Mask primitives with their REAL geometry, but never modify the scene
    // depth. The same depth buffer is reused to hide occluded silhouettes.
    gfx::PipelineDesc primitiveMask;
    primitiveMask.shader = litShader_;
    primitiveMask.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
    primitiveMask.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
    primitiveMask.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
    primitiveMask.attrCount = 3;
    primitiveMask.stride = sizeof(MeshVertex);
    primitiveMask.indexType = gfx::IndexType::UInt32;
    primitiveMask.depthTest = true;
    primitiveMask.depthWrite = false;
    primitiveMask.colorFormat = gfx::PixelFormat::RGBA8;
    primitiveMask.label = "selection primitive mask depth read-only";
    sceneSelectionPrimitiveMaskPipe_ = dev_->createPipeline(primitiveMask);
    const float triangle[6] = {-1, -1, 3, -1, -1, 3};
    gfx::BufferDesc bd;
    bd.data = triangle;
    bd.size = sizeof triangle;
    bd.label = "scene selection edge fullscreen triangle";
    sceneSelectionTriangle_ = dev_->createBuffer(bd);
    return sceneSelectionEdgePipe8_.valid() && sceneSelectionTriangle_.valid();
}

void Renderer3D::renderSelectionOutline(const gfx::PassDesc& mainPass,
                                        int width, int height) {
    if (!dev_ || !mainPass.colorTarget.valid() || !mainPass.depthTarget.valid() ||
        !sceneSelectionTriangle_.valid() || width <= 0 || height <= 0)
        return;
    const gfx::PipelineHandle edgePipeline = hdrFrame_
        ? sceneSelectionEdgePipeHdr_ : sceneSelectionEdgePipe8_;
    if (!edgePipeline.valid()) return;
    bool any = false;
    for (const auto& item : modelItems_) if (item.outline.w > 0 && item.inView) {
        any = true;
        break;
    }
    if (!any) for (const auto& item : items_) if (item.selection.w > 0) {
        any = true;
        break;
    }
    if (!any) return;

    if(!sceneSelectionMask_.valid() ||
       sceneSelectionWidth_!=width || sceneSelectionHeight_!=height) {
        gfx::TextureDesc td;
        td.width=width;td.height=height;
        td.format=gfx::PixelFormat::RGBA8;
        td.renderTarget=true;
        td.filter=gfx::Filter::Nearest;
        td.label="Scene selected-object depth-tested mask";
        const auto replacement=dev_->createTexture(td);
        if(!replacement.valid())return; // keep old mask for its in-flight work
        retireRenderTargets(sceneSelectionMask_);
        sceneSelectionMask_=replacement;
        sceneSelectionWidth_=width;
        sceneSelectionHeight_=height;
    }
    if (!sceneSelectionMask_.valid()) return;

    // Renderer3D::render normally leaves the main pass open. Temporarily
    // suspend it, draw visible selected geometry using the same scene depth,
    // then resume the colour pass WITHOUT clearing its contents or depth.
    dev_->endPass();
    gfx::PassDesc mask;
    mask.colorTarget = sceneSelectionMask_;
    mask.depthTarget = mainPass.depthTarget;
    mask.clearColor[0] = mask.clearColor[1] = mask.clearColor[2] =
        mask.clearColor[3] = 0.0f;
    mask.clearDepthEnabled = false;
    dev_->beginPass(mask);
    dev_->setViewport({0, 0, width, height});

    // Existing model skin/morph/LOD pipelines draw a second pass with zero
    // normal displacement. Cull backfaces and do not write the scene depth.
    selectionMaskPass_ = true;
    uploadFrameUniforms(false);
    renderModels(3);
    selectionMaskPass_ = false;

    // Primitive previews use their original geometry, not bounding-box lines.
    bool bound = false;
    for (const auto& item : items_) {
        if (item.selection.w <= 0 || !item.mesh || !item.mesh->vb.valid())
            continue;
        if (!bound) {
            if (!sceneSelectionPrimitiveMaskPipe_.valid()) break;
            dev_->setPipeline(sceneSelectionPrimitiveMaskPipe_);
            uploadFrameUniforms(false);
            bound = true;
        }
        DrawUniforms u{};
        std::memcpy(u.model, item.model.m, sizeof u.model);
        u.color[0] = item.selection.x;
        u.color[1] = item.selection.y;
        u.color[2] = item.selection.z;
        u.color[3] = 1.0f;
        u.params[0] = 2.0f;  // existing solid unlit/diagnostic branch
        dev_->setUniforms(0, &u, sizeof u);
        dev_->setVertexBuffer(item.mesh->vb);
        dev_->setIndexBuffer(item.mesh->ib);
        dev_->drawIndexed(item.mesh->indexCount);
        ++drawCalls_;
    }
    dev_->endPass();

    gfx::PassDesc resume = mainPass;
    resume.clearColorEnabled = false;
    resume.clearDepthEnabled = false;
    dev_->beginPass(resume);
    dev_->setViewport({0, 0, width, height});

    dev_->setPipeline(edgePipeline);
    dev_->setVertexBuffer(sceneSelectionTriangle_);
    const float params[4] = {
        1.0f / float(width), 1.0f / float(height),
        dev_->info().renderTargetFlipY ? 1.0f : 0.0f, 0.0f
    };
    dev_->setUniforms(0, params, sizeof params);
    dev_->setTexture(0, sceneSelectionMask_);
    dev_->draw(3);
    ++drawCalls_;
}

} // namespace aiverse::render
