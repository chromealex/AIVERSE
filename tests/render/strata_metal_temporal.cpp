// STRATA Stage17.4: native Metal GPU-only temporal LOD mask acceptance.
// Never stalls GPU to choose production geometry; readbacks are asynchronous
// TEST DIAGNOSTICS ONLY. Must be run on a real Metal device in final QA.
#include "engine/render/StrataGpuCulling.h"
#include "engine/render/StrataGpuScene.h"
#include "engine/render/Geometry.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/platform/Platform.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

namespace {
struct Case {
    GpuPassConfig pass{};
    std::vector<uint8_t> previous,expected;
    GpuSelectionReadback ranges{};
    std::vector<uint8_t> stateBytes;
    bool rangeReady=false,stateReady=false;
};
struct Fixture {
    gfx::Device* device=nullptr;
    Model source;
    ModelAsset staged;
    GpuMetadataScene metadata;
    GpuClusterCulling culling;
    gfx::BufferHandle gpuMetadata{};
    uint32_t pooledStart=0;
    std::array<Case,3> cases{};
    uint32_t frames=0,queued=0;
    bool failed=false;
    std::string error;
    float baseError=0.f;
    const GpuSceneInstance instance=makeGpuSceneInstance(
        mat4::identity(),0,91);

    void fail(std::string reason){
        if(failed)return;
        failed=true;
        error=std::move(reason);
        std::fprintf(stderr,"[strata-metal-temporal] FAIL: %s\n",error.c_str());
    }
    bool initialize(gfx::Device& dev){
        device=&dev;
        if(dev.info().backend!=gfx::Backend::Metal ||
           !dev.supportsCompute()){
            fail("Metal compute device unavailable");
            return false;
        }
        source.vertices.resize(4);
        source.vertices[0].pos={-.5f,-.5f,.4f};
        source.vertices[1].pos={ .5f,-.5f,.4f};
        source.vertices[2].pos={-.5f, .5f,.4f};
        source.vertices[3].pos={ .5f, .5f,.4f};
        source.indices={0,1,2,1,3,2};
        if(!uploadModelToGeometryPool(dev,source)){
            fail("GlobalGeometryPool admission failed");
            return false;
        }
        ModelPrimitiveAsset p{};
        p.mesh=0;p.primitive=0;p.material=0;
        auto& g=p.geometry;
        g.indices={0,1,2,1,3,2,0,1,2,1,3,2};
        g.roots={0};
        g.nodes.resize(3);
        g.bounds.resize(3);
        g.nodes[0].firstChild=1;
        g.nodes[0].childCount=2;
        g.nodes[0].firstIndex=0;g.nodes[0].indexCount=6;
        g.nodes[0].geometricError=.25f;
        g.nodes[1].firstIndex=6;g.nodes[1].indexCount=3;
        g.nodes[2].firstIndex=9;g.nodes[2].indexCount=3;
        for(auto& node:g.nodes){node.resident=true;node.materialGroup=0;}
        for(auto& b:g.bounds)b={{-.5f,-.5f,.2f},{.5f,.5f,.8f}};
        staged.primitives.push_back(std::move(p));
        if(!metadata.stage(dev,"metal-temporal",source,staged)){
            fail("GPU metadata staging failed");
            return false;
        }
        gpuMetadata=metadata.find("metal-temporal",source);
        GpuMetadataScene::IndexBinding index{};
        if(!gpuMetadata.valid() ||
           !metadata.indexBinding("metal-temporal",source,index)){
            fail("GPU pooled index binding unavailable");
            return false;
        }
        pooledStart=index.firstIndex;
        const auto& b=staged.primitives[0].geometry.bounds[0];
        baseError=geometryErrorPixels(mat4::identity(),
            {b.min[0],b.min[1],b.min[2]},
            {b.max[0],b.max[1],b.max[2]},
            staged.primitives[0].geometry.nodes[0].geometricError,
            800,600,dev.info().clipDepthZeroToOne);
        if(!std::isfinite(baseError) || baseError<=0.f){
            fail("invalid reference projected error");
            return false;
        }
        return true;
    }
    void enqueue(gfx::Device& dev,size_t index){
        auto& c=cases[index];
        const float factors[]{.95f,1.08f,1.30f};
        c.pass.viewProjection=mat4::identity();
        c.pass.width=800;c.pass.height=600;
        c.pass.clipDepthZeroToOne=dev.info().clipDepthZeroToOne;
        c.pass.frustumCull=false;
        c.pass.maxOutputClusters=16;
        c.pass.errorThresholdPixels=baseError*factors[index];
        c.pass.hysteresisFraction=.2f;
        c.pass.temporalNodeCount=3;
        c.pass.temporalPassId=1;
        c.pass.temporalRevision=51;
        if(index)c.previous=cases[index-1].expected;

        ViewConfig cpu=cpuReferenceView(c.pass,instance);
        if(index)cpu.previousRefined=&c.previous;
        ViewPlan planned{};
        std::string why;
        if(!planView(staged.primitives[0].geometry,cpu,planned,&why)){
            fail("CPU reference failed: "+why);
            return;
        }
        c.expected=planned.refinedNodes;
        if(c.expected.size()!=3 ||
           planned.ranges.size()!=(index==2?1u:2u)){
            fail("unexpected reference temporal frontier");
            return;
        }
        GpuSelectionSubmission submitted{};
        if(!culling.dispatch(dev,gpuMetadata,{instance},c.pass,
                             dev.frameSerial(),submitted) ||
           !submitted.temporalApplied || !submitted.temporalState.valid()){
            fail("GPU temporal dispatch rejected: "+dev.lastError());
            return;
        }
        if(!culling.requestReadback(dev,submitted,
            [this,index](GpuSelectionReadback read) {
                cases[index].ranges=std::move(read);
                cases[index].rangeReady=true;
            })){
            fail("GPU range readback rejected");
            return;
        }
        if(!dev.readBufferAsync(submitted.temporalState,
            3*sizeof(uint32_t),
            [this,index](std::vector<uint8_t> data) {
                cases[index].stateBytes=std::move(data);
                cases[index].stateReady=true;
            })){
            fail("GPU temporal mask readback rejected");
            return;
        }
        ++queued;
    }
    void frame(gfx::Device& dev){
        if(failed){platform::requestQuit();return;}
        ++frames;
        if(queued<cases.size()){
            enqueue(dev,queued);
            return;
        }
        bool all=true;
        for(const auto& c:cases)
            all &= c.rangeReady && c.stateReady;
        if(!all){
            if(frames>80){
                fail("GPU temporal callbacks timed out");
                platform::requestQuit();
            }
            return;
        }
        for(const auto& c:cases){
            if(!c.ranges.valid() ||
               c.stateBytes.size()!=3*sizeof(uint32_t)){
                fail("GPU temporal output invalid");
                break;
            }
            std::string reason;
            if(!compareGpuSelection(staged,{instance},c.pass,pooledStart,
                c.ranges,&reason,
                c.previous.empty()?nullptr:&c.previous)){
                fail("CPU/GPU temporal range mismatch: "+reason);
                break;
            }
            for(size_t n=0;n<3;++n){
                uint32_t flag=0;
                std::memcpy(&flag,c.stateBytes.data()+n*sizeof(flag),
                            sizeof(flag));
                if(flag!=c.expected[n]){
                    fail("GPU temporal mask differs from CPU refinedNodes");
                    break;
                }
            }
            if(failed)break;
        }
        platform::requestQuit();
    }
    void shutdown(){
        if(!device)return;
        culling.shutdown(*device);
        metadata.shutdown(*device);
        releaseModelGeometryPool(*device,source);
        std::printf("[strata-metal-temporal] submissions=%u status=%s\n",
            queued,failed?"FAILED":"PASSED");
    }
};
} // namespace

int aiverseMain(int,char**){
    if(setenv("AIVERSE_METAL_CPU_MDI","1",1)!=0)return 2;
    Fixture f;
    platform::AppConfig cfg;
    cfg.title="STRATA Metal temporal LOD GPU/CPU parity";
    cfg.width=800;cfg.height=600;cfg.highDpi=false;
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& dev){
        if(!f.initialize(dev))platform::requestQuit();
    };
    callbacks.frame=[&](gfx::Device& dev,double){f.frame(dev);};
    callbacks.shutdown=[&]{f.shutdown();};
    platform::run(cfg,callbacks);
    return f.failed || f.queued!=3 ? 1:0;
}
