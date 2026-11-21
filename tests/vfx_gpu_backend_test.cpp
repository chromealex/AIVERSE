// SpectraFX CPU / GPU backend selection and CPU fallback headless regression.
// GPU kernels remain gated by Metal/RHI and require a real-device test separately.
#include "engine/fx/SpectraFX.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <string>
#include <vector>
using namespace aiverse;

static void check(bool ok,const char* why) {
    if(!ok) throw std::runtime_error(why);
}
// RHI recording mock exercises command order without impersonating a real GPU.
// It does NOT compile or execute Metal shaders; a Metal device test remains open.
class RecordingMetalDevice final:public gfx::Device {
public:
    gfx::DeviceInfo description{gfx::Backend::Metal,false,false,"Recording Metal"};
    std::string error;
    bool availableCompute=true,healthy=true,frame=false,pass=false,computing=false,badOrder=false;
    bool indirectAvailable=false,indirectAccepted=true;
    bool rejectStorageUpload=false;
    uint32_t next=1,lastMode=99,lastDrawCount=0,liveSlotHandle=0;
    uint32_t indirectArgsHandle=0,indirectCalls=0;
    size_t uploadBytes=0;
    std::vector<uint32_t> lastLiveSlots;
    std::vector<uint32_t> stages;

    const gfx::DeviceInfo& info() const override{return description;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& desc)override{
        const uint32_t id=next++;
        if(desc.label && std::strcmp(desc.label,"SpectraFX GPU live-slot gather map")==0)
            liveSlotHandle=id;
        if(desc.label && std::strcmp(desc.label,"SpectraFX GPU authored draw args")==0)
            indirectArgsHandle=id;
        return {id};
    }
    void updateBuffer(gfx::BufferHandle handle,const void* data,size_t count,size_t=0)override{
        if(pass||computing)badOrder=true;
        if(handle.id==liveSlotHandle && count>0) {
            if(count%sizeof(uint32_t)!=0 || !data) {badOrder=true;return;}
            lastLiveSlots.resize(count/sizeof(uint32_t));
            std::memcpy(lastLiveSlots.data(),data,count);
        }
        uploadBytes+=count;
    }
    bool updateStorageBufferChecked(gfx::BufferHandle h,const void* data,size_t size,size_t offset=0) override {
        if(rejectStorageUpload || !healthy || !frame || pass || computing ||
           !h.valid() || !data || !size)return false;
        updateBuffer(h,data,size,offset);
        return true;
    }
    void destroyBuffer(gfx::BufferHandle)override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&)override{return {next++};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int)override{}
    void destroyTexture(gfx::TextureHandle)override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&)override{return {next++};}
    void destroyShader(gfx::ShaderHandle)override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&)override{return {next++};}
    void destroyPipeline(gfx::PipelineHandle)override{}
    const std::string& lastError() const override{return error;}
    bool supportsCompute()const override{return availableCompute;}
    bool supportsGpuAuthoredIndirectDraw() const override {return indirectAvailable;}
    bool drawIndexedGpuIndirect(gfx::BufferHandle handle,size_t offset)override{
        if(!pass||handle.id!=indirectArgsHandle||offset!=0)badOrder=true;
        ++indirectCalls;
        return indirectAccepted;
    }
    bool gpuHealthy() const override {return healthy;}
    bool canUpdateSharedBuffers() const override {return !pass && !computing;}
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc&)override{return {next++};}
    void destroyCompute(gfx::ComputeHandle)override{}
    bool beginCompute(gfx::ComputeHandle)override{
        if(!frame||pass||computing){badOrder=true;return false;}
        computing=true;return true;
    }
    void setComputeBuffer(int,gfx::BufferHandle)override{
        if(!computing)badOrder=true;
    }
    void setComputeUniforms(const void* data,size_t size)override{
        if(!computing||size<64){badOrder=true;return;}
        std::memcpy(&lastMode,static_cast<const uint8_t*>(data)+48,sizeof(uint32_t));
    }
    void dispatch(uint32_t n,uint32_t=1)override{
        if(!computing||!n)badOrder=true;
        stages.push_back(lastMode);
    }
    void endCompute()override{if(!computing)badOrder=true;computing=false;}
    void beginFrame(int width,int height)override{frame=true;fbW_=width;fbH_=height;}
    void endFrame()override{if(pass||computing)badOrder=true;frame=false;}
    void beginPass(const gfx::PassDesc&)override{
        if(!frame||computing||pass)badOrder=true;
        pass=true;
    }
    void endPass()override{if(!pass)badOrder=true;pass=false;}
    void setViewport(const gfx::Rect&)override{}
    void setScissor(const gfx::Rect&)override{}
    void setPipeline(gfx::PipelineHandle)override{if(!pass)badOrder=true;}
    void setVertexBuffer(gfx::BufferHandle,size_t=0)override{if(!pass)badOrder=true;}
    void setIndexBuffer(gfx::BufferHandle,size_t=0)override{if(!pass)badOrder=true;}
    void setTexture(int,gfx::TextureHandle)override{if(!pass)badOrder=true;}
    void setUniforms(int,const void*,size_t)override{if(!pass)badOrder=true;}
    void draw(uint32_t,uint32_t=0)override{if(!pass)badOrder=true;}
    void drawIndexed(uint32_t count,uint32_t=0,int32_t=0,uint32_t=1)override{
        if(!pass)badOrder=true;
        lastDrawCount=count;
    }
};

int main() {
    using spectra::ComputePreference;
    using spectra::ComputeBackend;
    using spectra::chooseComputeBackend;
    check(chooseComputeBackend(gfx::Backend::Metal,true,ComputePreference::Automatic)==ComputeBackend::Metal,
          "available Metal compute was not selected");
    check(chooseComputeBackend(gfx::Backend::Metal,false,ComputePreference::PreferGpu)==ComputeBackend::Cpu,
          "Metal without compute did not fall back");
    check(chooseComputeBackend(gfx::Backend::GLES3,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "GLES3 illegally selected Metal backend");
    check(chooseComputeBackend(gfx::Backend::Vulkan,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "Vulkan GPU path is not implemented yet");
    check(chooseComputeBackend(gfx::Backend::GL33,true,ComputePreference::Automatic)==ComputeBackend::Cpu,
          "GL33 GPU path is not implemented yet");
    check(chooseComputeBackend(gfx::Backend::Metal,true,ComputePreference::CpuOnly)==ComputeBackend::Cpu,
          "CpuOnly policy ignored");

    spectra::ComputeEmitterDesc config;
    config.capacity=80;
    config.burst=5;
    config.lifetime=0.75f;
    config.speed=3;
    config.gravityY=-9.81f;
    config.backend=ComputePreference::Automatic;

    std::string reason;
    check(spectra::validComputeEmitter(config,&reason),"default config invalid");
    auto invalid=config;
    invalid.capacity=4097;
    check(!spectra::validComputeEmitter(invalid,&reason),"unsafe >4096 vertex/index capacity accepted");
    invalid=config; invalid.burst=0;
    check(!spectra::validComputeEmitter(invalid,&reason),"zero burst accepted");
    invalid=config; invalid.lifetime=-3;
    check(!spectra::validComputeEmitter(invalid,&reason),"negative lifetime accepted");
    invalid=config; invalid.speed=999999;
    check(!spectra::validComputeEmitter(invalid,&reason),"out-of-range speed accepted");

    spectra::ComputeParticles emitter;
    check(emitter.init(nullptr,config),"CPU fallback headless init failed");
    check(emitter.backend()==ComputeBackend::Cpu,"null GPU device should use CPU backend");
    check(emitter.burstAt({0,2,0},{1,0,0}),"CPU fallback failed to spawn particles");
    check(emitter.step(0.08f),"CPU fallback step failed");
    std::vector<fx::FxParticleInfo> particles;
    emitter.cpuFallback().snapshot(particles);
    check(particles.size()==5,"fallback lost expected burst size");
    check(particles.front().pos.x>0.001f,"fallback did not move particle");
    emitter.clear();
    emitter.cpuFallback().snapshot(particles);
    check(particles.empty(),"clear did not remove CPU fallback particles");
    check(emitter.burstAt({0,0,0}),"fallback spawn after clear failed");
    emitter.shutdown();
    check(!emitter.burstAt({0,0,0}),"spawn accepted after shutdown");

    // GPU host-path smoke via RHI recording, no real shader execution.
    RecordingMetalDevice mock;
    config.backend=ComputePreference::Automatic;
    config.capacity=32;
    config.burst=3;
    check(emitter.init(&mock,config),"recording Metal device GPU init failed");
    check(emitter.gpuActive(),"supported Metal mock did not select GPU");
    render::RTSCamera cam{{0,1,0},10.f,0.4f,0.6f};
    mock.beginFrame(800,600);
    emitter.setView(cam,cam.proj(800.f/600.f,false)*cam.view(),800,600);
    check(emitter.burstAt({0,2,0},{0,1,0}),"GPU queue rejected burst");
    mock.rejectStorageUpload=true;
    check(!emitter.step(1.f/60.f),"failed storage upload was ignored");
    check(mock.stages.empty(),"compute dispatched with rejected staging");
    mock.rejectStorageUpload=false;
    check(emitter.step(1.f/60.f),"GPU queue compute step failed");
    check(mock.stages==std::vector<uint32_t>({0,1,2}),
          "GPU dispatch order must be spawn, integrate, expand");
    mock.beginPass(gfx::PassDesc{});
    emitter.draw();
    check(mock.lastDrawCount==config.burst*6,
          "GPU draw should submit only occupied slots, not full capacity");
    check(emitter.stats().submittedSlots==config.burst,
          "GPU active draw prefix incorrectly includes unused tail");
    emitter.clear(); // forbidden while GPU render encoder is active
    check(emitter.stats().alive==config.burst,
          "clear inside an active pass must preserve CPU/GPU slot bookkeeping");
    check(!emitter.reason().empty(),
          "GPU clear must report invalid encoder timing");
    mock.endPass();
    emitter.clear();
    check(emitter.stats().alive==0 && emitter.stats().submittedSlots==0,
          "clear between encoders must drop the active GPU draw range");
    const auto dispatches=emitter.stats().gpuDispatches;
    check(emitter.step(1.f/60.f), "GPU empty-pool step failed");
    check(emitter.stats().gpuDispatches==dispatches,
          "empty GPU pool must not dispatch unnecessary compute work");
    mock.beginPass(gfx::PassDesc{});
    mock.lastDrawCount=0;
    emitter.draw();
    check(mock.lastDrawCount==0,"empty GPU pool still submitted a draw");
    mock.endPass();
    mock.endFrame();
    check(!mock.badOrder,"GPU API called during invalid frame/encoder state");
    emitter.shutdown();

    // A device-loss transition is explicit: RequireGpu must NEVER silently
    // switch to CPU, even after successful Metal initialization.
    config.backend=ComputePreference::RequireGpu;
    mock.healthy=true;
    check(emitter.init(&mock,config),"RequireGpu should initialize on simulated healthy Metal");
    mock.beginFrame(800,600);
    mock.healthy=false;
    check(!emitter.step(1.f/60.f),"RequireGpu must fail on GPU device loss");
    check(!emitter.ready() && !emitter.gpuActive(),
          "RequireGpu must not report an active GPU after device loss");
    check(emitter.backend()==ComputeBackend::Metal,
          "RequireGpu silently switched to CPU on GPU loss");
    check(emitter.reason().find("RequireGpu")!=std::string::npos,
          "RequireGpu device-loss diagnostic missing");
    mock.endFrame();
    emitter.shutdown();

    // Auto can migrate to a CPU emitter, but must report the discontinuity.
    config.backend=ComputePreference::Automatic;
    mock.healthy=true;
    check(emitter.init(&mock,config),"Auto Metal init failed");
    check(emitter.burstAt({0,0,0}),"GPU birth before fallback failed");
    mock.beginFrame(800,600);
    mock.healthy=false;
    check(!emitter.step(1.f/60.f),
          "GPU device loss cannot be reported as seamless successful simulation");
    check(emitter.backend()==ComputeBackend::Cpu && emitter.ready(),
          "Auto did not transition to usable CPU fallback");
    check(emitter.stats().gpuFallbacks==1 && emitter.stats().lostOnFallback==config.burst,
          "Auto fallback must report discarded GPU-resident particles");
    mock.endFrame();
    emitter.shutdown();
    mock.healthy=true;

    // Live-slot gather: physical state slots can contain gaps, but the
    // GPU's vertex output must be dense and indexed by alive ordinal.
    RecordingMetalDevice sparseDevice;
    spectra::ComputeParticles sparse;
    config.backend=ComputePreference::Automatic;
    config.capacity=16;config.burst=3;config.lifetime=0.75f;
    check(sparse.init(&sparseDevice,config),"sparse GPU recording init");
    sparse.setView(cam,cam.proj(800.f/600.f,false)*cam.view(),800,600);
    const auto sparseFrame=[&](bool spawn) {
        sparseDevice.lastDrawCount=0;
        sparseDevice.beginFrame(800,600);
        if(spawn)check(sparse.burstAt({0,2,0}),"sparse GPU birth failed");
        check(sparse.step(0.1f),"sparse GPU step failed");
        sparseDevice.beginPass(gfx::PassDesc{});
        sparse.draw();
        sparseDevice.endPass();
        sparseDevice.endFrame();
    };
    sparseFrame(true);sparseFrame(true);
    for(int i=0;i<6;++i)sparseFrame(false);
    check(sparse.stats().alive==3 && sparse.stats().submittedSlots==3,
          "sparse physical pool should produce exactly three dense GPU quads");
    check(sparseDevice.lastDrawCount==18,
          "dense GPU gather must draw only live particles, not physical holes");
    check(sparseDevice.lastLiveSlots==std::vector<uint32_t>({3,4,5}),
          "GPU gather map must preserve the three surviving physical state slots");
    sparseFrame(false);
    check(sparse.stats().alive==0 && sparse.stats().submittedSlots==0,
          "all expired GPU particles must release dense draw count");
    check(sparseDevice.lastDrawCount==0,
          "expired sparse GPU pool still submitted geometry");
    check(!sparseDevice.badOrder,"sparse GPU mock encountered encoder/order violation");
    sparse.shutdown();

    // Stage 14: GPU-resident atomic compaction and GPU-authored indirect
    // commands. This mock validates host sequencing only, not real MSL.
    RecordingMetalDevice indirectDevice;
    indirectDevice.indirectAvailable=true;
    spectra::ComputeParticles indirectParticles;
    config.backend=ComputePreference::Automatic;
    config.gpuIndirect=true;
    config.capacity=24;config.burst=3;config.lifetime=0.75f;
    check(indirectParticles.init(&indirectDevice,config),"indirect Metal mock init");
    check(indirectParticles.gpuIndirectActive(),
          "explicit indirect capability and config were not selected");
    indirectParticles.setView(cam,cam.proj(800.f/600.f,false)*cam.view(),800,600);
    indirectDevice.beginFrame(800,600);
    check(indirectParticles.burstAt({0,2,0}),"indirect GPU spawn");
    check(indirectParticles.step(1.f/60.f),"indirect GPU step");
    check(indirectDevice.stages==std::vector<uint32_t>({3,0,1,4,5}),
          "GPU indirect passes must reset,spawn,integrate,compact,write args");
    check(indirectDevice.lastLiveSlots.empty(),
          "GPU-resident compact mode must not upload CPU survivor list");
    check(indirectParticles.stats().gpuCompactDispatches==1,
          "GPU compact dispatch count missing");
    indirectDevice.beginPass(gfx::PassDesc{});
    indirectParticles.draw();
    check(indirectDevice.indirectCalls==1 &&
          indirectDevice.lastDrawCount==0 &&
          indirectParticles.stats().gpuIndirectDraws==1,
          "GPU indirect should be attempted without a CPU draw");
    indirectDevice.endPass();
    indirectDevice.endFrame();
    check(!indirectDevice.badOrder,"indirect RHI calls used invalid encoder order");

    // An actual RHI indirect-submit failure must NOT draw stale vertices
    // using only the CPU's approximate alive count.
    indirectDevice.indirectAccepted=false;
    indirectDevice.lastDrawCount=0;
    indirectDevice.beginFrame(800,600);
    check(indirectParticles.step(1.f/60.f),"indirect update before forced draw failure");
    indirectDevice.beginPass(gfx::PassDesc{});
    indirectParticles.draw();
    check(indirectDevice.indirectCalls==2 && indirectDevice.lastDrawCount==0,
          "failed GPU indirect command must not submit a direct draw");
    check(indirectParticles.stats().drawCalls==0 &&
          !indirectParticles.reason().empty(),
          "indirect draw failure must be reported");
    indirectDevice.endPass();indirectDevice.endFrame();
    indirectParticles.shutdown();

    // A backend without the explicit indirect capability preserves the
    // CPU-published dense gather even when the author requests GPU indirect.
    RecordingMetalDevice noIndirect;
    spectra::ComputeParticles fallbackGather;
    check(fallbackGather.init(&noIndirect,config),"GPU fallback gather init");
    check(fallbackGather.gpuActive() && !fallbackGather.gpuIndirectActive(),
          "unsupported indirect should fall back to regular GPU gather");
    fallbackGather.setView(cam,cam.proj(800.f/600.f,false)*cam.view(),800,600);
    noIndirect.beginFrame(800,600);
    check(fallbackGather.burstAt({0,2,0}),"fallback gather burst");
    check(fallbackGather.step(1.f/60.f),"fallback gather step");
    check(noIndirect.stages==std::vector<uint32_t>({0,1,2}) &&
          noIndirect.lastLiveSlots.size()==config.burst,
          "fallback must retain CPU-published live-slot map");
    noIndirect.beginPass(gfx::PassDesc{});
    fallbackGather.draw();
    check(noIndirect.lastDrawCount==config.burst*6 &&
          noIndirect.indirectCalls==0,
          "fallback must use CPU-counted direct index draw");
    noIndirect.endPass();noIndirect.endFrame();
    check(!noIndirect.badOrder,"fallback gather RHI command order");
    fallbackGather.shutdown();

    // Explicit hard GPU requirement never silently reports a CPU implementation.
    config.backend=ComputePreference::RequireGpu;
    check(!emitter.init(nullptr,config),"RequireGpu should fail without a Device");
    check(!emitter.reason().empty(),"RequireGpu failure reason missing");
    std::cout<<"PASS SpectraFX compute policy, config validation, CPU fallback and lifecycle\n";
}
