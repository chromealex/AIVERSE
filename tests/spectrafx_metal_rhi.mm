// SpectraFX Stage 98: exercise the real gfx::Device Metal RHI with two
// independent offscreen cameras, GPU-written particles, and 3-slot retirement.
// No FPS/RTS/Editor code. This fixture links production GfxMetal.mm.
// Tests should SKIP (77), never claim PASS, when no usable Metal device exists.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "engine/gfx/Gfx.h"
#include "engine/fx/SpectraFxView.h"
#include "engine/fx/SpectraFxGpuEvolution.h"
#include "engine/fx/SpectraFxGpuBillboardRenderer.h"
#include "engine/fx/SpectraFxGpuAlphaSort.h"
#include "engine/fx/SpectraFxGpuParticleSlots.h"
#include "engine/fx/SpectraFxGpuImpactFeedback.h"

// The production backend's optional shader-archive loader only needs this
// platform file-reader symbol. In a standalone headless test no game bundle
// or resource pack exists, so compile runtime Metal sources from MSL.
namespace aiverse::platform {
bool readContentFile(const std::string&, std::string&) { return false; }
}

namespace {
constexpr int kWidth=128;
constexpr int kHeight=128;
int failures=0;
void check(bool ok,const char* what) {
    if (!ok) {
        std::fprintf(stderr,"FAIL [spectrafx-metal-rhi] %s\n",what);
        ++failures;
    }
}
uint8_t red(const std::vector<uint8_t>& rgba,int x,int y) {
    if (rgba.size()!=size_t(kWidth)*kHeight*4u) return 0;
    return rgba[(size_t(y)*kWidth+size_t(x))*4u];
}
aiverse::gfx::TextureHandle texture(aiverse::gfx::Device& device,
                                    aiverse::gfx::PixelFormat format) {
    aiverse::gfx::TextureDesc desc{};
    desc.width=kWidth;desc.height=kHeight;desc.format=format;
    desc.renderTarget=true;
    desc.label="SpectraFX RHI offscreen test";
    return device.createTexture(desc);
}
aiverse::fx::SpectraFxGpuSeed seed(float x) {
    aiverse::fx::SpectraFxGpuSeed s;
    s.position={x,0,0.5f};s.velocity={0,0,0};
    s.age=0;s.lifetime=10;
    return s;
}
struct Retiring {
    std::unique_ptr<aiverse::fx::SpectraFxGpuEvolution> particles;
    uint64_t lastDrawSerial=0;
};
} // namespace

int main() {
    @autoreleasepool {
        const id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        if (!metal) {
            std::puts("SKIP [spectrafx-metal-rhi] no Metal device");
            return 77;
        }
        CAMetalLayer* layer=[CAMetalLayer layer];
        layer.device=metal;
        layer.frame=CGRectMake(0,0,kWidth,kHeight);
        auto device=aiverse::gfx::createMetalDevice((__bridge void*)layer);
        if (!device || !device->supportsCompute()) {
            std::puts("SKIP [spectrafx-metal-rhi] RHI compute unavailable");
            return 77;
        }
        std::printf("[spectrafx-metal-rhi] device=%s\n",
                    device->info().name.c_str());
        auto colorA=texture(*device,aiverse::gfx::PixelFormat::RGBA8);
        auto colorB=texture(*device,aiverse::gfx::PixelFormat::RGBA8);
        auto depth=texture(*device,aiverse::gfx::PixelFormat::Depth);
        check(colorA.valid() && colorB.valid() && depth.valid(),
              "two render-to-texture color targets and depth");
        if (failures) return 1;

        // Inspector graph-v2 compatibility: real checked storage upload and
        // bounded ranged readback must preserve offsets and owner-thread delivery.
        const uint32_t storageWords[4]={11,22,33,44};
        auto storage=device->createBuffer({aiverse::gfx::BufferType::Storage,
            aiverse::gfx::Usage::Dynamic,sizeof(storageWords),nullptr,"graph-v2 RHI test"});
        check(storage.valid(),"graph-v2 storage allocation");
        check(!device->updateStorageBufferChecked(storage,storageWords,sizeof(storageWords)),
              "checked upload rejects missing frame");
        auto ranged=std::make_shared<std::vector<uint8_t>>();
        auto deliveredRange=std::make_shared<bool>(false);
        device->beginFrame(kWidth,kHeight);
        check(device->updateStorageBufferChecked(storage,storageWords,sizeof(storageWords)),
              "checked storage upload accepted in frame");
        check(!device->readBufferRangeAsync(storage,sizeof(storageWords),4,[](auto){}),
              "ranged read rejects overflow");
        check(device->readBufferRangeAsync(storage,4,8,[ranged,deliveredRange](auto bytes) {
            *ranged=std::move(bytes);*deliveredRange=true;
        }),"ranged storage read accepted");
        check(!*deliveredRange,"readback callback is asynchronous");
        device->endFrame();
        check(device->waitIdle(),"storage readback idle barrier");
        device->beginFrame(kWidth,kHeight);
        device->endFrame();
        check(*deliveredRange && ranged->size()==8 &&
              std::memcmp(ranged->data(),storageWords+1,8)==0,
              "ranged storage read matches uploaded offset bytes");
        check(device->waitIdle(),"storage teardown idle barrier");
        device->destroyBuffer(storage);

        const auto sharedCompute=device->createCompute(
            {aiverse::fx::kSpectraFxComputeMetal,nullptr,
             "SpectraFX RHI shared GPU evolution"});
        check(sharedCompute.valid(),"production Metal compute pipeline");
        aiverse::fx::SpectraFxGpuBillboardRenderer renderer;
        check(renderer.init(*device),"production gfx::Device billboard pipelines");
        if (failures) {
            std::fprintf(stderr,"[spectrafx-metal-rhi] RHI init: %s / %s\n",
                device->lastError().c_str(),renderer.lastError().c_str());
            return 1;
        }

        check(device->supportsStorageTexture(
                  aiverse::gfx::PixelFormat::RGBA32F,
                  aiverse::gfx::TextureDimension::Texture2D),
              "SpectraFX RGBA32F 2D writable storage capability");
        aiverse::fx::SpectraFxGpuEvolution particles;
        check(particles.init(*device,{seed(-0.45f)},false,sharedCompute),
              "production GPU particle buffers and RGBA32F storage");
        if (failures) return 1;
        aiverse::fx::SpectraFxGpuEnvironment env{};
        env.acceleration={0,0,0};env.damping=1;

        aiverse::fx::SpectraFxView cameraA;
        cameraA.eye={0,0,2};cameraA.forward={0,0,-1};
        cameraA.projectionYScale=1;cameraA.width=kWidth;cameraA.height=kHeight;
        aiverse::fx::SpectraFxView cameraB=cameraA;
        cameraB.viewProj=aiverse::render::mat4::translate({0.9f,0,0});
        cameraB.right={0,1,0};cameraB.up={-1,0,0}; // 90-degree camera roll
        check(cameraA.valid() && cameraB.valid(),"two arbitrary camera packets");

        const aiverse::fx::SpectraFxGpuBillboardRenderer::Appearance appearance{
            {0.9f,0.2f,0.05f},0.2f,false};
        auto pass=[&](aiverse::gfx::TextureHandle target) {
            aiverse::gfx::PassDesc d{};
            d.colorTarget=target;d.depthTarget=depth;
            d.clearColor[0]=d.clearColor[1]=d.clearColor[2]=0;
            d.clearColor[3]=0;
            return d;
        };
        device->beginFrame(kWidth,kHeight);
        check(particles.step(0.01f,env),"RHI compute command encodes before render pass");
        device->beginPass(pass(colorA));
        device->setViewport({0,0,kWidth,kHeight});
        check(renderer.draw(particles,cameraA.viewProj,cameraA.right,
                            cameraA.up,appearance),"camera A RHI instanced draw");
        device->endPass();
        device->beginPass(pass(colorB));
        device->setViewport({0,0,kWidth,kHeight});
        check(renderer.draw(particles,cameraB.viewProj,cameraB.right,
                            cameraB.up,appearance),"camera B rotated RHI draw");
        device->endPass();
        // Debug readback is deliberately forbidden while an RHI frame exists.
        std::vector<uint8_t> frameA,frameB;
        check(!device->readColorRGBA8(colorA,frameA),
              "diagnostic readback rejects in-flight recording");
        device->endFrame();
        check(device->readColorRGBA8(colorA,frameA) &&
              device->readColorRGBA8(colorB,frameB),
              "RHI offscreen texture GPU->CPU diagnostic readback");
        check(red(frameA,35,64)>100 && red(frameA,93,64)<10,
              "first camera has visible billboard on left only");
        check(red(frameB,93,64)>100 && red(frameB,35,64)<10,
              "second camera shifts billboard to right only");
        check(red(frameA,0,0)==0 && red(frameB,0,0)==0,
              "both render targets preserve clear outside billboard");
        std::printf("[spectrafx-metal-rhi] A left/right=%u/%u B left/right=%u/%u\n",
            unsigned(red(frameA,35,64)),unsigned(red(frameA,93,64)),
            unsigned(red(frameB,35,64)),unsigned(red(frameB,93,64)));
        check(device->gpuHealthy(),"GPU healthy after two independent RHI passes");
        // Finished raw readback also fences the first GPU storage use.
        particles.shutdown();

        // Create/churn GPU particle allocations across three Metal command
        // buffers in flight. A resource is destroyed only at the next safe
        // beginFrame after >=3 newer serials; never immediately after draw.
        std::deque<Retiring> retiring;
        auto retire=[&]() {
            const uint64_t serial=device->frameSerial();
            for (auto it=retiring.begin();it!=retiring.end();) {
                if (serial>=it->lastDrawSerial &&
                    serial-it->lastDrawSerial>=3) {
                    it->particles->shutdown();
                    it=retiring.erase(it);
                } else ++it;
            }
        };
        // Default smoke stays quick. Mac Studio stress may opt in to
        // 512+ frames and 256+ particle slots while retaining the exact
        // same real compute/draw/resource-retirement checks. No perf
        // threshold depends on the host's clock or thermal conditions.
        auto envInt=[](const char* key,int fallback,int lo,int hi) {
            const char* raw=std::getenv(key);
            if(!raw || !*raw) return fallback;
            char* end=nullptr;
            const long value=std::strtol(raw,&end,10);
            return end!=raw && *end=='\0' && value>=lo && value<=hi
                ?int(value):fallback;
        };
        const int kChurnFrames=envInt(
            "AIVERSE_SPECTRAFX_RHI_STRESS_FRAMES",24,24,2048);
        const int kChurnCapacity=envInt(
            "AIVERSE_SPECTRAFX_RHI_STRESS_CAPACITY",1,1,4096);
        uint64_t maxOutstandingBytes=0;
        // These measurements are a diagnostic whole-frame GPU timeline,
        // NOT a SpectraFX-exclusive shader dispatch benchmark. Backend
        // timing data is asynchronous; poll every frame so the Metal RHI's
        // 64 pending-sample cap cannot be reached by a 2048-frame test.
        constexpr uint64_t kGpuTimingTagBase=0x5350580000000000ull;
        std::vector<double> gpuFrameSamplesMs;
        uint32_t timedFrames=0, invalidGpuSamples=0;
        auto collectGpuTimes=[&]() {
            for(const auto& t:device->takeGpuTimings()) {
                if(t.tag<kGpuTimingTagBase ||
                   t.tag>=kGpuTimingTagBase+uint64_t(kChurnFrames))
                    continue;
                if(t.valid && std::isfinite(t.milliseconds) &&
                   t.milliseconds>=0.0)
                    gpuFrameSamplesMs.push_back(t.milliseconds);
                else ++invalidGpuSamples;
            }
        };
        // Metal currentAllocatedSize is device-wide resource allocation,
        // not resident VRAM and not uniquely attributable to SpectraFX.
        const auto metalAllocation=[&]() -> uint64_t {
            if (@available(macOS 10.15, *))
                return uint64_t(metal.currentAllocatedSize);
            return 0;
        };
        const uint64_t baselineMetalAllocation=metalAllocation();
        uint64_t peakMetalAllocation=baselineMetalAllocation;
        for (int i=0;i<kChurnFrames;++i) {
            device->beginFrame(kWidth,kHeight);
            collectGpuTimes();
            if(device->timeGpuFrame(kGpuTimingTagBase+uint64_t(i)))
                ++timedFrames;
            retire();
            auto next=std::make_unique<aiverse::fx::SpectraFxGpuEvolution>();
            std::vector<aiverse::fx::SpectraFxGpuSeed> seeds;
            seeds.reserve(size_t(kChurnCapacity));
            for(int j=0;j<kChurnCapacity;++j) {
                auto instance=seed((i%2==0)?-0.45f:0.45f);
                instance.position.x+=float(j%32)*0.002f;
                seeds.push_back(instance);
            }
            check(next->init(*device,seeds,false,sharedCompute),
                  "churn allocation of production GPU resources");
            if (!next->ready()) {device->endFrame();break;}
            check(next->step(0.016f,env),"churn GPU evolution dispatch");
            device->beginPass(pass(colorA));
            device->setViewport({0,0,kWidth,kHeight});
            check(renderer.draw(*next,cameraA.viewProj,cameraA.right,
                                cameraA.up,appearance),"churn GPU RHI draw");
            device->endPass();
            retiring.push_back({std::move(next),device->frameSerial()});
            uint64_t outstanding=0;
            for(const auto& held:retiring)
                outstanding+=held.particles->nominalGpuBytes();
            maxOutstandingBytes=std::max(maxOutstandingBytes,outstanding);
            peakMetalAllocation=std::max(peakMetalAllocation,metalAllocation());
            device->endFrame();
            if (failures) break;
        }
        for (int i=0;i<4;++i) {
            device->beginFrame(kWidth,kHeight);
            collectGpuTimes();
            retire();
            device->endFrame();
        }
        check(retiring.empty(),"all retired resources released only after safe 3-frame grace");
        check(device->readColorRGBA8(colorA,frameA),
              "finished churn frame readable through RHI");
        collectGpuTimes(); // readback above fences all earlier GPU work
        std::sort(gpuFrameSamplesMs.begin(),gpuFrameSamplesMs.end());
        auto percentile=[&](double fraction) {
            if(gpuFrameSamplesMs.empty()) return 0.0;
            const size_t n=gpuFrameSamplesMs.size();
            const size_t idx=std::min(n-1,
                size_t(std::ceil(fraction*double(n))-1.0));
            return gpuFrameSamplesMs[idx];
        };
        const uint64_t afterMetalAllocation=metalAllocation();
        std::printf("[spectrafx-metal-rhi] gpu_frame_timing "
                    "requested=%d scheduled=%u valid=%zu invalid=%u "
                    "p50_ms=%.4f p95_ms=%.4f "
                    "metal_alloc_baseline_kib=%llu peak_kib=%llu after_kib=%llu\n",
                    kChurnFrames,timedFrames,gpuFrameSamplesMs.size(),
                    invalidGpuSamples,percentile(0.50),percentile(0.95),
                    static_cast<unsigned long long>(
                        baselineMetalAllocation/1024u),
                    static_cast<unsigned long long>(
                        peakMetalAllocation/1024u),
                    static_cast<unsigned long long>(
                        afterMetalAllocation/1024u));
        check(device->gpuHealthy(),"GPU healthy after 24 emitter churn frames");
        std::printf("[spectrafx-metal-rhi] churn_frames=%d capacity=%d "
                    "peak_nominal_kib=%llu retire_remaining=%zu "
                    "gpu_healthy=%d\n",
                    kChurnFrames,kChurnCapacity,
                    static_cast<unsigned long long>(
                        maxOutstandingBytes/1024u),
                    retiring.size(),device->gpuHealthy()?1:0);

        // Stage 108: real GPU alpha-sort parity for two opposite camera
        // directions in ONE Metal frame. This is a test-only diagnostic
        // shader: it visualizes the first GPU-sorted source index in RGBA8,
        // without CPU mapping or reading particle positions.
        aiverse::fx::SpectraFxGpuAlphaSort viewSort;
        aiverse::fx::SpectraFxGpuEvolution alphaParticles;
        auto first=seed(0),second=seed(0);
        first.position={0,0,0.5f};second.position={0,0,-0.5f};
        check(alphaParticles.init(*device,{first,second},false,sharedCompute),
              "two GPU-owned alpha particles initialized");
        check(viewSort.init(*device,2),
              "Metal GPU alpha sorter ready for two slots");
        static constexpr const char* kIndexVisual=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct VIn { float2 p [[attribute(0)]]; };
struct VOut { float4 pos [[position]]; };
vertex VOut vs_main(VIn v [[stage_in]]) {
    VOut o; o.pos=float4(v.p,0.0,1.0);return o;
}
fragment half4 fs_main(VOut i [[stage_in]],
                       texture2d<float> tex0 [[texture(0)]]) {
    const float index=tex0.read(uint2(0,0)).x;
    return half4(half(index>0.5f?0.90f:0.20f),
                 half(0.0f),half(0.0f),half(1.0f));
}
)MSL";
        const auto indexShader=device->createShader({
            nullptr,nullptr,kIndexVisual,1,0,
            "SpectraFX alpha-sort index map diagnostic"});
        const float fullscreenTriangle[]={-1,-1,3,-1,-1,3};
        const auto fullscreenVB=device->createBuffer({
            aiverse::gfx::BufferType::Vertex,
            aiverse::gfx::Usage::Immutable,sizeof(fullscreenTriangle),
            fullscreenTriangle,"SpectraFX sorted-index probe quad"});
        aiverse::gfx::PipelineDesc indexDesc{};
        indexDesc.shader=indexShader;
        indexDesc.attrs[0]={0,aiverse::gfx::VertexFormat::Float2,0};
        indexDesc.attrCount=1;
        indexDesc.stride=sizeof(float)*2;
        indexDesc.depthTest=false;
        indexDesc.depthWrite=false;
        indexDesc.cull=aiverse::gfx::CullMode::None;
        indexDesc.indexType=aiverse::gfx::IndexType::None;
        indexDesc.label="SpectraFX sorted index visual probe";
        const auto indexPipeline=device->createPipeline(indexDesc);
        check(indexShader.valid() && fullscreenVB.valid() &&
              indexPipeline.valid(),"GPU sorted-index diagnostic RHI pipeline");
        if(alphaParticles.ready() && viewSort.ready() &&
           indexPipeline.valid() && fullscreenVB.valid()) {
            device->beginFrame(kWidth,kHeight);
            check(alphaParticles.step(0.0f,env),
                  "GPU position texture initialized before sorting");
            const auto probeSort=[&](aiverse::gfx::TextureHandle target,
                                     aiverse::render::float3 forward) {
                check(viewSort.prepare(alphaParticles,{0,0,0},forward),
                      "per-camera GPU bitonic sort encoding");
                check(viewSort.preparedFor(alphaParticles),
                      "current frame sorted index texture available");
                device->beginPass(pass(target));
                device->setViewport({0,0,kWidth,kHeight});
                device->setPipeline(indexPipeline);
                device->setVertexBuffer(fullscreenVB);
                device->setTexture(0,viewSort.sortedIndexTexture());
                device->draw(3);
                device->endPass();
            };
            probeSort(colorA,{0,0,1});
            probeSort(colorB,{0,0,-1});
            device->endFrame();
            std::vector<uint8_t> sortedA,sortedB;
            check(device->readColorRGBA8(colorA,sortedA) &&
                  device->readColorRGBA8(colorB,sortedB),
                  "both camera index maps recorded by real Metal");
            const int a=int(red(sortedA,kWidth/2,kHeight/2));
            const int b=int(red(sortedB,kWidth/2,kHeight/2));
            check(a>=35 && a<=85 && b>=195 && b<=255,
                  "opposite camera depth directions reverse GPU sort ordering");
            std::printf("[spectrafx-metal-rhi] alpha_views_first="
                        "%d/%d expected~51/230\n",a,b);
        }
        viewSort.release();
        alphaParticles.shutdown();
        if(indexPipeline.valid())device->destroyPipeline(indexPipeline);
        if(fullscreenVB.valid())device->destroyBuffer(fullscreenVB);
        if(indexShader.valid())device->destroyShader(indexShader);

        // Stage 100: exercise the real asynchronous Metal impact callback,
        // including an invalidated scene epoch and recycled particle handles.
        // Callback data stays presentation-only; neither path writes ECS.
        aiverse::fx::SpectraFxGpuParticleSlots impactSlots;
        check(impactSlots.init(*device,1,true,sharedCompute),
              "GPU impact slots allocated with real RHI");
        aiverse::fx::SpectraFxGpuImpactFeedback feedback;
        aiverse::fx::SpectraFxGpuEnvironment collision{};
        collision.acceleration={0,0,0};
        collision.damping=1;
        collision.collideGround=true;
        collision.reportImpacts=true;
        collision.groundHeight=0;
        collision.restitution=0;
        collision.tangentRetention=1;
        aiverse::fx::SpectraFxGpuSeed falling;
        falling.position={0,0.05f,0.5f};
        falling.velocity={0,-4,0};
        falling.age=0;falling.lifetime=5;
        if(impactSlots.evolution().ready()) {
            device->beginFrame(kWidth,kHeight);
            const auto oldHandle=impactSlots.emit(falling);
            check(oldHandle.valid(),"first generation-safe GPU impact slot");
            check(impactSlots.step(0.05f,collision),
                  "first native GPU ground-collision compute");
            check(feedback.request(*device,impactSlots),
                  "first asynchronous RHI impact readback queued");
            // Simulate rollback/floating-origin reset BEFORE GPU completion.
            feedback.reset();
            device->endFrame();

            // Three frame slots must be cycled before asserting callback
            // delivery/discard. The fourth frame supplies safe slack.
            for(int frame=0;frame<4;++frame) {
                device->beginFrame(kWidth,kHeight);
                device->endFrame();
            }
            int delivered=0;
            feedback.drain(impactSlots,
                           [&](const aiverse::fx::SpectraFxGpuImpact&) {
                               ++delivered;
                           });
            check(feedback.pending()==0 && delivered==0,
                  "old scene epoch cannot publish delayed GPU impacts");

            device->beginFrame(kWidth,kHeight);
            check(impactSlots.kill(oldHandle),"retire old impact particle");
            const auto newHandle=impactSlots.emit(falling);
            check(newHandle.valid() &&
                  newHandle.generation!=oldHandle.generation,
                  "recycled GPU slot receives a new generation");
            check(impactSlots.step(0.05f,collision),
                  "second real GPU impact collision");
            check(feedback.request(*device,impactSlots),
                  "new scene can request impact after reset");
            device->endFrame();

            for(int frame=0;frame<4;++frame) {
                device->beginFrame(kWidth,kHeight);
                device->endFrame();
            }
            const size_t queuedBeforeReuse=feedback.pending();
            check(queuedBeforeReuse==1,
                  "GPU callback queued a genuine collision event");

            device->beginFrame(kWidth,kHeight);
            check(impactSlots.kill(newHandle),
                  "recycle particle after GPU callback arrival");
            const auto recycled=impactSlots.emit(falling);
            check(recycled.valid() &&
                  recycled.index==newHandle.index &&
                  recycled.generation!=newHandle.generation,
                  "new entity generation cannot alias queued GPU feedback");
            const auto discardedBefore=feedback.stats().discarded;
            feedback.drain(impactSlots,
                           [&](const aiverse::fx::SpectraFxGpuImpact&) {
                               ++delivered;
                           });
            check(delivered==0 && feedback.pending()==0 &&
                  feedback.stats().discarded>discardedBefore,
                  "stale GPU impact rejected on particle generation mismatch");
            device->endFrame();
            // Finish the final RHI slot writes before destroying storage.
            for(int frame=0;frame<4;++frame) {
                device->beginFrame(kWidth,kHeight);
                device->endFrame();
            }
            check(device->gpuHealthy(),"GPU remains healthy after callback churn");
            std::printf("[spectrafx-metal-rhi] impact_queued=%zu "
                        "impact_discarded=%llu delivered=%d\n",
                        queuedBeforeReuse,
                        static_cast<unsigned long long>(feedback.stats().discarded),
                        delivered);
        }
        impactSlots.shutdown();

        // Teardown order: the standalone RHI device outlives all dependent
        // particle textures, shaders, render targets and shared compute state.
        renderer.release();
        device->destroyCompute(sharedCompute);
        device->destroyTexture(colorA);
        device->destroyTexture(colorB);
        device->destroyTexture(depth);
        if(failures) {
            std::fprintf(stderr,
                "SpectraFX real Metal RHI acceptance: %d failures\n",failures);
            return 1;
        }
        std::puts("PASS [spectrafx-metal-rhi] production RHI offscreen cameras, "
                  "pixel parity, compute->draw and 3-frame resource retirement");
        return 0;
    }
}
