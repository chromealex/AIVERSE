#include "PrismaScreenLighting.h"
#include "Shaders.h"
#include "PrismaScreenLightingShaders.h"
#include <algorithm>
#include <cmath>

namespace aiverse::render {
namespace {
struct Uniforms { mat4 invVP, prevVP; float4 eye, prevEye, px, params, jitter; mat4 lightVP; float4 sun, sunColor, volume, ambient; };
float halton(int index, int base) {
    float value=0, fraction=1;
    while(index>0){fraction/=float(base);value+=fraction*float(index%base);index/=base;}
    return value;
}
struct Source { std::string glslVertex,glslFragment,msl; };
Source shader(const char* body,bool sunShadow) {
    const std::string fields="mat4 invVP; mat4 prevVP; vec4 eye; vec4 prevEye; vec4 px; vec4 params; vec4 jitter; mat4 lightVP; vec4 sun; vec4 sunColor; vec4 volume; vec4 ambient;";
    const std::string common=prismalighting::common;
    Source s;
    s.glslVertex="layout(location=0) in vec2 a_pos; out vec2 v_screen; void main(){v_screen=a_pos*0.5+0.5;gl_Position=vec4(a_pos,0.0,1.0);}";
    s.glslFragment="layout(std140) uniform U0 {"+fields+"};\nin vec2 v_screen;out vec4 o_color;\nuniform sampler2D tex0;uniform highp sampler2D tex1;uniform sampler2D tex2;\n"
        "#define COLOR(p) texture(tex0,p)\n#define DEPTH(p) texture(tex1,p).r\n#define HISTORY(p) texture(tex2,p)\n#define RETURN(c) {o_color=(c);return;}\n"+
        (sunShadow?"uniform highp sampler2D tex3;\n" AIVERSE_GLSL_SUN_FILTER("tex3") "#define SHADOW(uv,d) sunCompare(uv,d)\n":"")+common+"void main(){vec2 s=v_screen;\n"+body+"}\n";
    s.msl="#include <metal_stdlib>\nusing namespace metal;\n" AIVERSE_MSL_SUN_FILTER "#define mat4 float4x4\n#define vec2 float2\n#define vec3 float3\n#define vec4 float4\nstruct U0 {"+fields+"};\n"
        "struct VIn{float2 pos [[attribute(0)]];}; struct VOut{float4 pos [[position]];float2 screen;};\n"
        "vertex VOut vs_main(VIn in [[stage_in]]){VOut o;o.pos=float4(in.pos,0.0,1.0);o.screen=in.pos*0.5+0.5;return o;}\n"
        "#define COLOR(p) tex0.sample(s0,p)\n#define DEPTH(p) tex1.sample(s1,p)\n#define HISTORY(p) tex2.sample(s2,p)\n#define RETURN(c) return (c)\n#define SHADOW(uv,d) sunCompare(tex3,uv,d)\n"+common+
        "fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]],depth2d<float> tex1 [[texture(1)]],sampler s1 [[sampler(1)]],texture2d<float> tex2 [[texture(2)]],sampler s2 [[sampler(2)]]"+(sunShadow?",depth2d<float> tex3 [[texture(3)]],sampler s3 [[sampler(3)]]":"")+"){\n"
        "const mat4 invVP=u.invVP,prevVP=u.prevVP,lightVP=u.lightVP;const vec4 eye=u.eye,prevEye=u.prevEye,px=u.px,params=u.params,jitter=u.jitter,sun=u.sun,sunColor=u.sunColor,volume=u.volume,ambient=u.ambient;vec2 s=in.screen;\n"+body+"}\n";
    return s;
}
}
bool PrismaScreenLighting::init(gfx::Device& d, Prisma& post) {
    dev_=&d;post_=&post;
    const float tri[]={-1,-1,3,-1,-1,3};gfx::BufferDesc vb;vb.data=tri;vb.size=sizeof tri;vb.label="screen lighting";
    triangle_=d.createBuffer(vb);
    const char* bodies[]={prismalighting::bounce,prismalighting::temporal,prismalighting::copy,prismalighting::scatter,prismalighting::copyLinear};
    for(int i=0;i<5;++i) {
        auto source=shader(bodies[i],i==3);gfx::ShaderDesc desc;desc.uniformSlots=1;desc.textureSlots=i==3?4:3;desc.label="screen lighting";
        desc.glslVertex=source.glslVertex.c_str();desc.glslFragment=source.glslFragment.c_str();desc.msl=source.msl.c_str();
        shaders_[i]=d.createShader(desc);
        if(!shaders_[i].valid()){error_=d.lastError();return false;}
        gfx::PipelineDesc p;p.shader=shaders_[i];p.attrs[0]={0,gfx::VertexFormat::Float2,0};p.attrCount=1;p.stride=8;
        p.indexType=gfx::IndexType::None;p.cull=gfx::CullMode::None;p.depthTest=p.depthWrite=false;p.label="screen lighting";
        pipes_[i]=d.createPipeline(p);
        if(!pipes_[i].valid()){error_=d.lastError();return false;}
    }
    return triangle_.valid();
}
void PrismaScreenLighting::queueTarget(gfx::TextureHandle& target) {
    if (!target.valid()) return;
    // Retire using the backend's actual submission token, never a CPU
    // frame number. If the backend has no token, quarantine until idle.
    const uint64_t fence = dev_ ? dev_->gpuSubmissionFence() : 0;
    if (fence) retiredTargets_.retire(target, fence);
    else unfencedTargets_.push_back(target);
    target = {};
}
void PrismaScreenLighting::releaseTargets() {
    for (auto* t : {&bounce_,&scatter_,&history_[0],&history_[1],&depth_})
        queueTarget(*t);
    // Only clear this adapter's resources/state. The underlying Prisma
    // scene/history belongs to Prisma and is never retired here.
    w_=h_=0;current_=0;reset();
}
size_t PrismaScreenLighting::collectGpuRetirements() {
    if (!dev_) return 0;
    const uint64_t completed = dev_->gpuCompletedFence();
    if (!completed) return 0;
    return retiredTargets_.collect(completed,
        [this](gfx::TextureHandle t) { dev_->destroyTexture(t); });
}
size_t PrismaScreenLighting::drainRetiredAfterGpuIdle() {
    if (!dev_) return 0;
    // The host MUST have synchronized GPU idle before using this method.
    size_t count = retiredTargets_.drainAfterGpuIdle(
        [this](gfx::TextureHandle t) { dev_->destroyTexture(t); });
    for (const auto t : unfencedTargets_)
        if (t.valid()) { dev_->destroyTexture(t); ++count; }
    unfencedTargets_.clear();
    return count;
}
PrismaScreenLighting::RetirementStats PrismaScreenLighting::retirementStats() const {
    RetirementStats stats;
    stats.fencedTextures=retiredTargets_.pendingCount();
    stats.unfencedTextures=unfencedTargets_.size();
    stats.completedFence=dev_?dev_->gpuCompletedFence():0;
    return stats;
}
void PrismaScreenLighting::shutdown() {
    if(!dev_)return;
    // Caller-owned GPU idle point required for shaders/triangle as well.
    releaseTargets();
    drainRetiredAfterGpuIdle();
    for(auto p:pipes_)if(p.valid())dev_->destroyPipeline(p);
    for(auto s:shaders_)if(s.valid())dev_->destroyShader(s);
    if(triangle_.valid())dev_->destroyBuffer(triangle_);
    dev_=nullptr;post_=nullptr;
}
RTSCamera PrismaScreenLighting::jittered(RTSCamera c,int w,int h) {
    // Never apply two independent temporal jitters: canonical Prisma
    // Temporal Resolve exclusively owns reconstruction when enabled.
    if (post_ && post_->canonicalTemporalRequested()) {
        reset();
        jitter_={};previousJitter_={}; // no legacy reprojection offsets in lighting-only passes
        return post_->jitteredCamera(c,w,h);
    }
    if(enabled_&&post_&&post_->hdrRequested()&&post_->quality()==PostFxQuality::High&&w>0&&h>0) {
        int index=(sequence_++%8)+1;c.jitter={2*(halton(index,2)-0.5f)/float(w),2*(halton(index,3)-0.5f)/float(h)};
    }
    jitter_={c.jitter.x*.5f,c.jitter.y*.5f};
    return c;
}
gfx::PassDesc PrismaScreenLighting::scenePass(const gfx::PassDesc& out,int w,int h) {
    // Opportunistically collect backend-confirmed completions even when
    // lighting is disabled; this does not wait for the graphics queue.
    collectGpuRetirements();
    scene_=post_->scenePass(out,w,h);
    volumeRan_=false;
    // Canonical Prisma owns the only temporal history and camera jitter.
    // Lighting-only passes can still run before its HDR/temporal resolve.
    const bool highHdr=enabled_&&post_->linearScene()&&post_->quality()==PostFxQuality::High;
    const bool canonical=highHdr&&post_->canonicalTemporalRequested();
    const bool wasLegacy=legacyTemporal_;
    legacyTemporal_=highHdr&&!canonical;
    if(wasLegacy!=legacyTemporal_)reset();
    const bool needBounce=highHdr&&diffuseBounce_;
    const bool needScatter=highHdr&&volume_.enabled&&volume_.density>0;
    running_=legacyTemporal_||needBounce||needScatter;
    if(!running_||w<=0||h<=0){
        running_=false;legacyTemporal_=false;
        // Runtime disable is a lifecycle transition: retire cached
        // auxiliary allocations, but never immediately destroy in-flight
        // render targets. A later re-enable allocates only what it needs.
        if(w_||h_||bounce_.valid()||scatter_.valid()||
           history_[0].valid()||history_[1].valid()||depth_.valid())
            releaseTargets();
        reset();return scene_;
    }
    if(w_!=w||h_!=h){releaseTargets();w_=w;h_=h;}
    // Lazy allocation: the canonical path never creates this wrapper's
    // duplicate TAA histories or legacy depth attachment. Existing handles
    // remain cached when switching modes to avoid destroying in-flight GPU
    // resources merely because a setting changed.
    gfx::TextureDesc t;t.width=w;t.height=h;t.renderTarget=true;
    t.filter=gfx::Filter::Linear;t.format=gfx::PixelFormat::RGBA16F;t.label="screen lighting HDR";
    if(needBounce&&!bounce_.valid())bounce_=dev_->createTexture(t);
    if(needScatter&&!scatter_.valid())scatter_=dev_->createTexture(t);
    if(legacyTemporal_){
        if(!history_[0].valid())history_[0]=dev_->createTexture(t);
        if(!history_[1].valid())history_[1]=dev_->createTexture(t);
        t.format=gfx::PixelFormat::Depth;t.filter=gfx::Filter::Nearest;
        if(!depth_.valid())depth_=dev_->createTexture(t);
    }
    if((needBounce&&!bounce_.valid())||(needScatter&&!scatter_.valid())||
        (legacyTemporal_&&(!history_[0].valid()||!history_[1].valid()||!depth_.valid()))){
        error_="screen lighting allocation failed";
        running_=false;legacyTemporal_=false;reset();
    }else error_.clear();
    return scene_;
}
void PrismaScreenLighting::resolve(gfx::Device& d,const gfx::PassDesc& out,const PostFrame& f) {
    if(running_) {
        if(legacyTemporal_&&length(f.cameraPos-previousEye_)>2.0f)valid_=false;
        Uniforms u{f.viewProj.inverse(),previous_,{f.cameraPos.x,f.cameraPos.y,f.cameraPos.z,0},
            {previousEye_.x,previousEye_.y,previousEye_.z,0},
            {1.0f/float(w_),1.0f/float(h_),d.info().renderTargetFlipY?1.0f:0.0f,d.info().clipDepthZeroToOne?1.0f:2.0f},
            {legacyTemporal_&&valid_?1.0f:0.0f,0,0,0},
            {legacyTemporal_?jitter_.x:0.0f,legacyTemporal_?jitter_.y:0.0f,
             legacyTemporal_?previousJitter_.x:0.0f,legacyTemporal_?previousJitter_.y:0.0f},
            f.lightViewProj,{},{},{},{}};
        const auto& L=f.lighting;const float3 toSun=normalize(L.sunDir);
        u.sun={toSun.x,toSun.y,toSun.z,std::clamp(volume_.anisotropy,-.95f,.95f)};
        u.sunColor={L.sunColor.x*L.sunIntensity*volume_.tint.x,L.sunColor.y*L.sunIntensity*volume_.tint.y,L.sunColor.z*L.sunIntensity*volume_.tint.z,std::max(volume_.density,0.0f)};
        // Without THIS wrapper's history keep volume sampling deterministic.
        // Canonical temporal preflight can reject jitter on a given frame;
        // animated noise must not require its history to hide flicker.
        const float noisePhase=legacyTemporal_?float(frame_++%64)*.618034f:0.0f;
        u.volume={std::max(volume_.maxDistance,.1f),float(volume_.steps),noisePhase,volume_.ambientStart};
        u.ambient={std::max(volume_.ambient.x,0.0f),std::max(volume_.ambient.y,0.0f),std::max(volume_.ambient.z,0.0f),std::max(volume_.ambientDensity,0.0f)};
        volumeRan_=volume_.enabled&&volume_.density>0&&f.sunShadow.valid()&&L.sunIntensity>0;
        auto draw=[&](int program,gfx::TextureHandle target,gfx::TextureHandle source){
            gfx::PassDesc p;p.colorTarget=target;
            // The scene depth is only sampled, never modified. The old
            // resolver retains its historical private depth attachment.
            if(legacyTemporal_)p.depthTarget=depth_;
            p.clearColorEnabled=false;p.clearDepthEnabled=false;
            d.beginPass(p);d.setViewport({0,0,w_,h_});d.setPipeline(pipes_[program]);d.setUniforms(0,&u,sizeof u);
            d.setTexture(0,source);d.setTexture(1,scene_.depthTarget);
            // Bind a valid non-target sampler in lighting-only mode even
            // though only the legacy temporal shader reads HISTORY().
            d.setTexture(2,legacyTemporal_?history_[1-current_]:source);
            if(program==3)d.setTexture(3,f.sunShadow);
            d.setVertexBuffer(triangle_);d.draw(3);d.endPass();
        };
        gfx::TextureHandle source=scene_.colorTarget;
        bool contributed=false;
        if(diffuseBounce_){draw(0,bounce_,source);source=bounce_;contributed=true;}
        if(volumeRan_){draw(3,scatter_,source);source=scatter_;contributed=true;}
        if(legacyTemporal_){
            draw(1,history_[current_],source);
            // The temporal history stores receiver distance in alpha:
            // legacy copy deliberately writes opaque alpha to scene.
            draw(2,scene_.colorTarget,history_[current_]);
            previous_=f.viewProj;previousEye_=f.cameraPos;previousJitter_=jitter_;
            valid_=true;current_=1-current_;
        }else if(contributed){
            // Linear HDR lighting -> scene -> canonical Prisma resolve.
            // Unlike legacy TAA, this copy preserves scene alpha.
            draw(4,scene_.colorTarget,source);
        }
    }
    post_->resolve(d,out,f);
}
}
