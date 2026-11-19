#include "SpectraGpuParticles.h"
#include "SpectraGpuShaders.h"
#include "ParticleShaders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace aiverse::spectra {
namespace {
bool finite(float x) {return std::isfinite(x);}
bool finite(fx::float3 v) {return finite(v.x)&&finite(v.y)&&finite(v.z);}
bool finite(fx::float4 v) {return finite(v.x)&&finite(v.y)&&finite(v.z)&&finite(v.w);}
gfx::BufferHandle buffer(gfx::Device& d,gfx::BufferType type,size_t size,
                         const void* data,const char* label) {
    gfx::BufferDesc desc;
    desc.type=type;desc.usage=gfx::Usage::Immutable;desc.size=size;
    desc.data=data;desc.label=label;
    return d.createBuffer(desc);
}
uint8_t byte(float x) {
    return uint8_t(std::clamp(int(std::lround(std::clamp(x,0.0f,1.0f)*255.f)),0,255));
}
} // namespace

ComputeBackend chooseComputeBackend(gfx::Backend backend,bool computeAvailable,
                                    ComputePreference preference) {
    if(preference==ComputePreference::CpuOnly)return ComputeBackend::Cpu;
    // Metal is the only backend with a verified RHI compute API at the time
    // of this implementation. Enabling Vulkan requires explicit RHI support.
    return backend==gfx::Backend::Metal&&computeAvailable
        ?ComputeBackend::Metal:ComputeBackend::Cpu;
}
bool validComputeEmitter(const ComputeEmitterDesc& d,std::string* why) {
    const auto fail=[&](const char* msg){if(why)*why=msg;return false;};
    if(d.capacity==0||d.capacity>4096)return fail("GPU emitter capacity must be 1..4096");
    if(d.burst==0||d.burst>64)return fail("GPU emitter burst must be 1..64");
    if(d.lifetime<0.01f||d.lifetime>60.f||!finite(d.lifetime))return fail("invalid lifetime");
    if(d.speed<0.f||d.speed>500.f||!finite(d.speed))return fail("invalid speed");
    if(d.size<=0.f||d.size>100.f||!finite(d.size))return fail("invalid size");
    if(!finite(d.gravityY)||std::abs(d.gravityY)>1000.f)return fail("invalid gravity");
    if(!finite(d.drag)||d.drag<0||d.drag>50.f)return fail("invalid drag");
    if(d.burst>d.capacity)return fail("one burst exceeds available slots");
    if(why)why->clear();
    return true;
}

bool ComputeParticles::init(gfx::Device* dev,const ComputeEmitterDesc& config) {
    shutdown();
    desc_=config;
    if(!validComputeEmitter(config,&reason_))return false;
    device_=dev;
    backend_=ComputeBackend::Cpu;
    const bool canGpu=dev&&dev->supportsCompute()&&desc_.quality>=fx::FxQuality::Medium;
    const auto selected=dev
        ?chooseComputeBackend(dev->info().backend,canGpu,desc_.backend)
        :ComputeBackend::Cpu;
    if(desc_.backend==ComputePreference::RequireGpu && selected!=ComputeBackend::Metal) {
        reason_="Metal compute is unavailable; RequireGpu forbids CPU fallback";
        device_=nullptr;return false;
    }
    if(selected==ComputeBackend::Metal) {
        if(initGpu()) {
            backend_=ComputeBackend::Metal;initialized_=true;reason_.clear();
            return true;
        }
        const std::string gpuFailure=reason_;
        releaseGpu();
        if(desc_.backend==ComputePreference::RequireGpu) {
            reason_="Metal GPU setup failed: "+gpuFailure;
            device_=nullptr;return false;
        }
        reason_="Metal GPU unavailable, CPU fallback: "+gpuFailure;
    } else if(desc_.backend!=ComputePreference::CpuOnly) {
        reason_="Metal compute unavailable or low-tier quality; CPU fallback";
    }
    if(!initCpu()) {
        if(reason_.empty())reason_="CPU particle fallback init failed";
        device_=nullptr;return false;
    }
    initialized_=true;return true;
}
bool ComputeParticles::initCpu() {
    cpu_.setQuality(desc_.quality);
    if(!cpu_.init(device_)) {
        reason_+=" CPU init: "+cpu_.lastError();
        return false;
    }
    fx::EffectDef effect;
    fx::EmitterDef emitter;
    emitter.texture="builtin:soft";
    emitter.blend=fx::FxBlend::Additive;
    emitter.burst=int(desc_.burst);
    emitter.life={desc_.lifetime,desc_.lifetime};
    emitter.speed={desc_.speed,desc_.speed};
    emitter.size={desc_.size,desc_.size};
    emitter.gravity={0,desc_.gravityY,0};
    emitter.drag={desc_.drag,desc_.drag};
    emitter.spread=0;
    emitter.color={{0.f,{1,1,1,1}},{1.f,{1,1,1,0}}};
    effect.emitters.push_back(std::move(emitter));
    cpuEffect_=cpu_.define("__spectrafx_compute_cpu_fallback",effect);
    return cpuEffect_>=0;
}
bool ComputeParticles::initGpu() {
    if(!device_) {reason_="no RHI Device";return false;}
    const uint32_t cap=desc_.capacity;
    compute_=device_->createCompute({shaders::kMetal,shaders::kGLSL,"SpectraFX Metal particle simulation"});
    if(!compute_.valid()) {reason_=device_->lastError();return false;}

    std::vector<ParticleState> reset(cap);
    for(auto& s:reset) {s.positionAge={0,0,0,-1};s.velocityLife={0,0,0,1};}
    states_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(ParticleState),
                   reset.data(),"SpectraFX GPU particle states");
    seeds_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(ParticleState),
                  nullptr,"SpectraFX GPU particle spawn queue");
    slots_=buffer(*device_,gfx::BufferType::Storage,cap*sizeof(uint32_t),
                  nullptr,"SpectraFX GPU spawn slots");
    vertices_=buffer(*device_,gfx::BufferType::Storage,
                     cap*4*sizeof(Vertex),nullptr,"SpectraFX GPU quad vertices");

    std::vector<uint16_t> idx(cap*6);
    for(uint32_t n=0;n<cap;++n) {
        const uint16_t i=uint16_t(n*4);
        const size_t at=size_t(n)*6;
        idx[at]=i;idx[at+1]=uint16_t(i+1);idx[at+2]=uint16_t(i+2);
        idx[at+3]=i;idx[at+4]=uint16_t(i+2);idx[at+5]=uint16_t(i+3);
    }
    indices_=buffer(*device_,gfx::BufferType::Index,idx.size()*sizeof(uint16_t),
                    idx.data(),"SpectraFX GPU billboard indices");

    // One soft-sprite texture shared across all GPU particles in this first
    // backend. Texture atlas/mesh/ribbons remain on the existing CPU backend.
    constexpr int n=32;
    std::array<uint8_t,n*n*4> pixels{};
    for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
        const float dx=(float(x)+0.5f-float(n)*0.5f)/(float(n)*0.5f);
        const float dy=(float(y)+0.5f-float(n)*0.5f)/(float(n)*0.5f);
        const float dist=std::sqrt(dx*dx+dy*dy);
        const float alpha=std::clamp((1.f-dist)*2.f,0.f,1.f);
        const size_t at=size_t(y*n+x)*4;
        pixels[at]=pixels[at+1]=pixels[at+2]=255;
        pixels[at+3]=byte(alpha);
    }
    gfx::TextureDesc texture;
    texture.width=n;texture.height=n;texture.data=pixels.data();
    texture.filter=gfx::Filter::Linear;texture.label="SpectraFX GPU soft sprite";
    texture_=device_->createTexture(texture);

    gfx::ShaderDesc shader;
    shader.glslVertex=fx::shaders::kParticleVS;
    shader.glslFragment=fx::shaders::kParticleFS;
    shader.msl=fx::shaders::kParticleMSL;
    shader.textureSlots=1;shader.uniformSlots=1;shader.label="SpectraFX GPU billboard";
    vertexShader_=device_->createShader(shader);
    if(vertexShader_.valid()) {
        gfx::PipelineDesc pd;
        pd.shader=vertexShader_;
        pd.attrs[0]={0,gfx::VertexFormat::Float3,0};
        pd.attrs[1]={1,gfx::VertexFormat::Float2,12};
        pd.attrs[2]={2,gfx::VertexFormat::UByte4N,20};
        pd.attrCount=3;pd.stride=sizeof(Vertex);
        pd.indexType=gfx::IndexType::UInt16;
        pd.cull=gfx::CullMode::None;pd.blend=gfx::BlendMode::Additive;
        pd.depthTest=true;pd.depthWrite=false;
        pd.label="SpectraFX GPU additive";
        pipeline_=device_->createPipeline(pd);
    }
    if(!states_.valid()||!seeds_.valid()||!slots_.valid()||!vertices_.valid()||
       !indices_.valid()||!texture_.valid()||!vertexShader_.valid()||!pipeline_.valid()) {
        reason_="GPU resource/pipeline allocation failed: "+device_->lastError();
        return false;
    }
    ages_.assign(cap,-1.f);
    freeSlots_.clear();freeSlots_.reserve(cap);
    for(uint32_t i=cap;i>0;--i)freeSlots_.push_back(i-1);
    pendingStates_.reserve(cap);
    pendingSlots_.reserve(cap);
    stats_.gpuBytes=size_t(cap)*(sizeof(ParticleState)*2+sizeof(uint32_t)+4*sizeof(Vertex)+6*sizeof(uint16_t))
                    +size_t(n*n*4);
    return true;
}
void ComputeParticles::releaseGpu() {
    if(device_) {
        if(pipeline_.valid())device_->destroyPipeline(pipeline_);
        if(vertexShader_.valid())device_->destroyShader(vertexShader_);
        if(compute_.valid())device_->destroyCompute(compute_);
        if(states_.valid())device_->destroyBuffer(states_);
        if(seeds_.valid())device_->destroyBuffer(seeds_);
        if(slots_.valid())device_->destroyBuffer(slots_);
        if(vertices_.valid())device_->destroyBuffer(vertices_);
        if(indices_.valid())device_->destroyBuffer(indices_);
        if(texture_.valid())device_->destroyTexture(texture_);
    }
    pipeline_={};vertexShader_={};compute_={};states_={};seeds_={};slots_={};
    vertices_={};indices_={};texture_={};
    ages_.clear();freeSlots_.clear();pendingStates_.clear();pendingSlots_.clear();
}
void ComputeParticles::shutdown() {
    cpu_.shutdown();cpuEffect_=-1;releaseGpu();device_=nullptr;
    initialized_=false;viewReady_=false;verticesReady_=false;
    backend_=ComputeBackend::Cpu;stats_={};reason_.clear();
}
void ComputeParticles::clear() {
    if(!initialized_)return;
    if(backend_==ComputeBackend::Cpu){cpu_.clear();stats_.alive=0;return;}
    pendingStates_.clear();pendingSlots_.clear();verticesReady_=false;
    const uint32_t cap=desc_.capacity;
    std::vector<ParticleState> empty(cap);
    for(auto& s:empty){s.positionAge={0,0,0,-1};s.velocityLife={0,0,0,1};}
    device_->updateBuffer(states_,empty.data(),empty.size()*sizeof(ParticleState));
    ages_.assign(cap,-1.f);
    freeSlots_.clear();
    for(uint32_t i=cap;i>0;--i)freeSlots_.push_back(i-1);
    stats_.alive=0;
}
bool ComputeParticles::burstAt(fx::float3 position,fx::float3 direction,fx::float4 tint) {
    if(!initialized_||desc_.quality==fx::FxQuality::Off||!finite(position)||!finite(direction)||!finite(tint))return false;
    const float length=render::length(direction);
    const fx::float3 normal=length>1e-5f?direction*(1.0f/length):fx::float3{0,1,0};
    if(backend_==ComputeBackend::Cpu) {
        const bool success=cpu_.play(cpuEffect_,position,normal,1.f,tint).valid();
        if(!success)stats_.dropped+=desc_.burst;
        return success;
    }
    if(freeSlots_.size()<desc_.burst) {
        stats_.dropped+=desc_.burst;return false;
    }
    for(uint32_t n=0;n<desc_.burst;++n) {
        const uint32_t slot=freeSlots_.back();freeSlots_.pop_back();
        ages_[slot]=desc_.lifetime;
        ParticleState particle{};
        particle.positionAge={position.x,position.y,position.z,0};
        particle.velocityLife={normal.x*desc_.speed,normal.y*desc_.speed,normal.z*desc_.speed,desc_.lifetime};
        particle.tint={tint.x,tint.y,tint.z,tint.w};
        particle.size={desc_.size,0,0,0};
        pendingStates_.push_back(particle);pendingSlots_.push_back(slot);
    }
    stats_.alive=desc_.capacity-uint32_t(freeSlots_.size());
    return true;
}
void ComputeParticles::setView(const render::RTSCamera& camera,const render::mat4& viewProjection,int w,int h) {
    viewProj_=viewProjection;
    eye_=camera.eye();
    const fx::float3 forward=render::normalize(camera.target-eye_);
    fx::float3 side=render::cross(forward,fx::float3{0,1,0});
    if(render::length(side)<1e-5f)side={1,0,0};
    right_=render::normalize(side);
    up_=render::normalize(render::cross(right_,forward));
    viewReady_=true;
    if(backend_==ComputeBackend::Cpu)cpu_.setView(camera,viewProjection,w,h);
}
bool ComputeParticles::step(float dt) {
    if(!initialized_||!finite(dt)||dt<0)return false;
    dt=std::clamp(dt,0.f,0.1f);
    if(backend_==ComputeBackend::Cpu) {
        cpu_.update(dt);
        stats_.alive=uint32_t(cpu_.stats().particles);
        return true;
    }
    if(!device_->gpuHealthy()) {
        switchToCpu("Metal GPU reported unhealthy");
        return backend_==ComputeBackend::Cpu;
    }
    if(!device_->canUpdateSharedBuffers()) {
        reason_="SpectraFX GPU step must be before the render pass";
        return false;
    }
    const uint32_t count=uint32_t(pendingStates_.size());
    if(count>0) {
        device_->updateBuffer(seeds_,pendingStates_.data(),size_t(count)*sizeof(ParticleState));
        device_->updateBuffer(slots_,pendingSlots_.data(),size_t(count)*sizeof(uint32_t));
        stats_.uploadedBytes+=size_t(count)*(sizeof(ParticleState)+sizeof(uint32_t));
    }
    if(!device_->beginCompute(compute_)) {
        reason_="Metal compute encoder unavailable: call step between beginFrame/endFrame, before rendering";
        return false;
    }
    device_->setComputeBuffer(0,states_);
    device_->setComputeBuffer(1,seeds_);
    device_->setComputeBuffer(2,slots_);
    device_->setComputeBuffer(3,vertices_);
    Uniforms u{};
    u.gravityDt={desc_.gravityY,dt,desc_.drag,0};
    u.rightSize={right_.x,right_.y,right_.z,desc_.size};
    u.up={up_.x,up_.y,up_.z,0};
    u.meta={0,desc_.capacity,count,0};
    if(count){device_->setComputeUniforms(&u,sizeof(u));device_->dispatch(count);++stats_.gpuDispatches;}
    u.meta[0]=1;
    device_->setComputeUniforms(&u,sizeof(u));
    device_->dispatch(desc_.capacity);++stats_.gpuDispatches;
    u.meta[0]=2;
    device_->setComputeUniforms(&u,sizeof(u));
    device_->dispatch(desc_.capacity);++stats_.gpuDispatches;
    device_->endCompute();
    pendingStates_.clear();pendingSlots_.clear();
    for(uint32_t i=0;i<desc_.capacity;++i) {
        if(ages_[i]<=0.f)continue;
        ages_[i]-=dt;
        if(ages_[i]<=0.f){ages_[i]=-1.f;freeSlots_.push_back(i);}
    }
    stats_.alive=desc_.capacity-uint32_t(freeSlots_.size());
    verticesReady_=true;reason_.clear();
    return true;
}
void ComputeParticles::draw(const fx::FxFog& fog) {
    stats_.drawCalls=0;
    if(!initialized_||!viewReady_||desc_.quality==fx::FxQuality::Off)return;
    if(backend_==ComputeBackend::Cpu) {
        cpu_.render(fog);stats_.drawCalls=uint32_t(cpu_.stats().drawCalls);return;
    }
    if(!verticesReady_||!pipeline_.valid())return;
    DrawUniforms u{};
    std::memcpy(u.viewProj,viewProj_.m,sizeof(u.viewProj));
    u.fog[0]=fog.color.x;u.fog[1]=fog.color.y;u.fog[2]=fog.color.z;u.fog[3]=fog.density;
    u.camera[0]=eye_.x;u.camera[1]=eye_.y;u.camera[2]=eye_.z;u.camera[3]=1;
    u.params[0]=1;u.params[1]=0;u.params[2]=0;u.params[3]=0;
    device_->setPipeline(pipeline_);
    device_->setVertexBuffer(vertices_);
    device_->setIndexBuffer(indices_);
    device_->setUniforms(0,&u,sizeof(u));
    device_->setTexture(0,texture_);
    device_->drawIndexed(desc_.capacity*6);
    stats_.drawCalls=1;
}
void ComputeParticles::switchToCpu(const std::string& why) {
    releaseGpu();backend_=ComputeBackend::Cpu;
    const bool ok=initCpu();
    reason_=why+(ok?"; CPU fallback active":"; CPU fallback init failed");
    initialized_=ok;stats_.alive=0;stats_.gpuBytes=0;
}

} // namespace aiverse::spectra
