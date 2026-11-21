// SpectraFX genre-independent hybrid playback retirement policy regression.
// A deliberately deterministic fake Metal-capable RHI validates policy,
// NOT GPU shader execution. Real Metal coverage: spectrafx_metal_rhi.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include "engine/fx/SpectraFxHybridPlayback.h"

using namespace aiverse;
using namespace aiverse::fx;

namespace {
int failures=0;
void check(bool ok,const char* label) {
    if(!ok) {
        std::fprintf(stderr,"FAIL [spectrafx-retirement] %s\n",label);
        ++failures;
    }
}

class FakeMetal final : public gfx::Device {
public:
    const gfx::DeviceInfo& info() const override {return info_;}
    const std::string& lastError() const override {return error_;}
    uint64_t frameSerial() const override {return serial_;}
    // GPU completion is modelled separately from the CPU frame counter:
    // retirement must follow completedFrameSerial(), never frameSerial().
    uint64_t completedFrameSerial() const override {return completed_;}
    void completeThrough(uint64_t serial) {completed_=serial;}
    void setCompletionLag(uint64_t frames) {lag_=frames;}
    bool gpuHealthy() const override {return true;}
    bool supportsCompute() const override {return true;}
    bool supports(gfx::PixelFormat format) const override {
        return format==gfx::PixelFormat::RGBA32F ||
               format==gfx::PixelFormat::RGBA8 ||
               format==gfx::PixelFormat::Depth;
    }
    bool canUpdateSharedBuffers() const override {
        return inFrame_ && !inPass_ && !inCompute_;
    }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override {
        return {allocate()};
    }
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override {}
    void destroyBuffer(gfx::BufferHandle h) override {erase(h.id);}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override {
        return {allocate()};
    }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle h) override {erase(h.id);}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override {
        return {allocate()};
    }
    void destroyShader(gfx::ShaderHandle h) override {erase(h.id);}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override {
        return {allocate()};
    }
    void destroyPipeline(gfx::PipelineHandle h) override {erase(h.id);}
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc&) override {
        return {allocate()};
    }
    void destroyCompute(gfx::ComputeHandle h) override {erase(h.id);}
    bool beginCompute(gfx::ComputeHandle h) override {
        if(!h.valid() || !inFrame_ || inPass_ || inCompute_) return false;
        inCompute_=true;return true;
    }
    void setComputeBuffer(int,gfx::BufferHandle) override {}
    void setComputeTexture(int,gfx::TextureHandle) override {}
    void setComputeUniforms(const void*,size_t) override {}
    void dispatch(uint32_t,uint32_t) override {}
    void endCompute() override {inCompute_=false;}
    void beginFrame(int,int) override {
        check(!inFrame_,"frame not nested");
        inFrame_=true;++serial_;
        if(lag_ && serial_>lag_) completed_=std::max(completed_,serial_-lag_);
    }
    void endFrame() override {
        check(inFrame_ && !inPass_ && !inCompute_,"frame ends safely");
        inFrame_=false;
    }
    void beginPass(const gfx::PassDesc&) override {
        check(inFrame_ && !inPass_ && !inCompute_,"render pass begins safely");
        inPass_=true;
    }
    void endPass() override {inPass_=false;}
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,size_t) override {}
    void setIndexBuffer(gfx::BufferHandle,size_t) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t) override {}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override {}
    size_t liveResources() const {return live_.size();}
    size_t peakLiveResources() const {return peakLive_;}
    uint64_t createdResources() const {return created_;}
    uint64_t serial() const {return serial_;}
private:
    uint32_t allocate() {
        const uint32_t id=++nextId_;
        live_.insert(id);
        ++created_;
        peakLive_=std::max(peakLive_,live_.size());
        return id;
    }
    void erase(uint32_t id) {
        if(!id || live_.erase(id)!=1)
            check(false,"RHI resource destroyed exactly once");
    }
    gfx::DeviceInfo info_{gfx::Backend::Metal,true,true,"FakeMetal policy"};
    std::string error_;
    uint64_t serial_=0, completed_=0, lag_=0;
    uint32_t nextId_=0;
    bool inFrame_=false,inPass_=false,inCompute_=false;
    std::unordered_set<uint32_t> live_;
    size_t peakLive_=0;
    uint64_t created_=0;
};

SpectraFxCue cue(uint64_t key,SpectraFxCue::Action action) {
    SpectraFxCue c{};
    c.id=42;c.key=key;c.action=action;c.position={0,0,0};
    c.direction={0,1,0};
    return c;
}
}

int main() {
    FakeMetal device;
    {
        ParticleSystem cpu; // no CPU init: only GPU policy is tested
        const std::string asset=R"JSON({
            "version":1,"cpuEffect":"","preference":"gpu",
            "particles":8,"allowCpuFallback":false,
            "appearance":{"additive":true,"radius":0.2},
            "spawn":{"burst":1,"rate":0,"life":2,"speed":0}
        })JSON";
        SpectraFxGpuEmitterLibrary assets([&](const std::string& path,
                                              std::string& text) {
            if(path!="fx/gpu/retire.gpu.json")return false;
            text=asset;return true;
        });
        SpectraFxHybridPlayback playback(device,cpu,assets);
        SpectraFxHybridPlayback::Route route{};
        route.asset="retire";
        playback.route(42,route);

        device.beginFrame(128,128);
        check(playback.dispatch(cue(11,SpectraFxCue::Action::Play)),
              "first keyed GPU emitter created");
        check(playback.attached()==1 &&
              playback.backendSnapshot().dedicatedGpu==1 &&
              playback.usedGpuSlots()==8,"one GPU source reserves eight slots");
        playback.updateGpu(0.016f,{0,0,2},{0,0,-1});
        device.beginPass({});
        playback.renderGpu(render::mat4::identity(),{1,0,0},{0,1,0});
        check(!playback.resetPresentation(),
              "reset inside open render pass rejected without mutation");
        check(playback.attached()==1 && playback.retiringGpuSlots()==0,
              "unsafe reset never partially destroys playback state");
        device.endPass();
        check(playback.dispatch(cue(11,SpectraFxCue::Action::Stop)),
              "Stop retires active GPU emitter");
        check(playback.attached()==0 && playback.retiringGpuSlots()==8 &&
              playback.retiringGpuResourceCount()==1,
              "Stop removes presentation source but retains GPU allocation");
        check(playback.stats().dedicatedGpuRetired==1,
              "dedicated GPU retirement recorded");
        device.endFrame();

        // CPU frame serial advances while the GPU has not completed the
        // retiring frame: resources must stay alive however many frames pass.
        const uint64_t retiredAt=device.serial();
        for(int i=1;i<=6;++i) {
            device.beginFrame(128,128);
            if(i==4) device.completeThrough(retiredAt-1);
            playback.updateGpu(0.016f,{0,0,2},{0,0,-1});
            check(playback.retiringGpuSlots()==8,
                  "retirement never released by frameSerial() alone");
            device.endFrame();
        }
        device.beginFrame(128,128);
        device.completeThrough(retiredAt);
        playback.updateGpu(0.016f,{0,0,2},{0,0,-1});
        check(playback.retiringGpuSlots()==0,
              "retirement released once completedFrameSerial() reaches it");
        device.endFrame();
        check(playback.stats().dedicatedGpuReleased==1,
              "retired GPU resources released exactly once");
        device.setCompletionLag(2); // steady GPU latency for the stress run

        // Repeated keyed replacement and live-scene reset: old slot
        // allocations count against capacity until their fence expires.
        // 1024 cycles x 2 GPU-allocated keyed emitters = 2048 source
        // retirements, including repeated scene resets and slot reuse.
        // Benchmark telemetry is informational only; native host speed
        // is never used as a deterministic pass/fail threshold.
        constexpr int kStressFrames=1024;
        uint64_t peakReservedSlots=0;
        const auto stressStart=std::chrono::steady_clock::now();
        for(int frame=0;frame<kStressFrames;++frame) {
            device.beginFrame(128,128);
            check(playback.dispatch(cue(11,SpectraFxCue::Action::Play)),
                  "GPU emitter can be recreated after retirement");
            check(playback.dispatch(cue(11,SpectraFxCue::Action::Play)),
                  "same-key replacement creates a fresh presentation source");
            check(playback.attached()==1,
                  "key replacement never duplicates authoritative ownership");
            check(playback.resetPresentation(),"live scene reset safe outside pass");
            check(playback.attached()==0 && playback.oneShotCount()==0,
                  "reset invalidates all active presentation entities");
            check(playback.retiringGpuSlots()>=16,
                  "both replaced and reset emitters retain their GPU slots");
            peakReservedSlots=std::max(peakReservedSlots,
                                      playback.retiringGpuSlots());
            check(playback.usedGpuSlots()<=
                  SpectraFxHybridPlayback::kMaxPlaybackGpuSlots,
                  "retirement accounts for GPU capacity on every churn frame");
            playback.updateGpu(0.016f,{0,0,2},{0,0,-1});
            device.endFrame();
        }
        for(int i=0;i<4;++i) {
            device.beginFrame(128,128);
            playback.updateGpu(0.016f,{0,0,2},{0,0,-1});
            device.endFrame();
        }
        const auto stressEnd=std::chrono::steady_clock::now();
        const double stressMs=std::chrono::duration<double,std::milli>(
            stressEnd-stressStart).count();
        check(playback.retiringGpuSlots()==0 &&
              playback.retiringGpuResourceCount()==0,
              "all queued GPU emissions safely retired after frame drain");
        check(playback.stats().dedicatedGpuRetired>=
                  uint64_t(2*kStressFrames),
              "at least 2048 GPU emitters retired during stress");
        check(peakReservedSlots<=512 && device.peakLiveResources()<=4096,
              "bounded resource count under repeated scene resets");
        std::printf("[spectrafx-retirement] frames=%d retired=%llu "
                    "peak_slots=%llu peak_rhi_handles=%zu "
                    "created_rhi_handles=%llu wall_ms=%.2f\n",
                    kStressFrames,
                    static_cast<unsigned long long>(
                        playback.stats().dedicatedGpuRetired),
                    static_cast<unsigned long long>(peakReservedSlots),
                    device.peakLiveResources(),
                    static_cast<unsigned long long>(
                        device.createdResources()),stressMs);
        check(playback.stats().dedicatedGpuRetired==
              playback.stats().dedicatedGpuReleased,
              "no lost or double-released GPU emitters");
        check(playback.usedGpuSlots()==0,"retirement budget fully recovered");
        playback.reset(); // valid here: no in-flight GPU submissions in mock
    }
    check(device.liveResources()==0,
          "playback destructors release all shared GPU resources exactly once");
    std::printf("[spectrafx-retirement] serial=%llu live=%zu errors=%d\n",
        static_cast<unsigned long long>(device.serial()),
        device.liveResources(),failures);
    return failures?1:0;
}
