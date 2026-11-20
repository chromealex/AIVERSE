#include "engine/render/LumaWeaveMetalDirect.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::render;

struct LumaFakeMetal : FakeDevice {
    std::vector<BufferReadFn> callbacks;
    bool asyncSupported=true;
    int bufferBindings[6]={-1,-1,-1,-1,-1,-1};
    uint32_t lastThreads=0;
    size_t uniformBytes=0;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    void setComputeBuffer(int slot,gfx::BufferHandle h) override {
        AIVERSE_TEST_CHECK(inCompute && slot>=0 && slot<6);
        AIVERSE_TEST_CHECK(buffers.contains(h.id));
        bufferBindings[slot]=int(h.id);
    }
    void setComputeUniforms(const void* data,size_t n) override {
        AIVERSE_TEST_CHECK(inCompute && data);
        uniformBytes=n;
    }
    void dispatch(uint32_t x,uint32_t y=1) override {
        lastThreads=x;
        FakeDevice::dispatch(x,y);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t n,BufferReadFn callback) override {
        if(!asyncSupported||!inFrame||inCompute||n!=4||!buffers.contains(h.id))return false;
        callbacks.push_back(std::move(callback));
        return true;
    }
    void finish(bool success) {
        AIVERSE_TEST_CHECK(!callbacks.empty());
        auto callback=std::move(callbacks.front());
        callbacks.erase(callbacks.begin());
        std::vector<uint8_t> bytes;
        if(success){
            bytes.resize(4);
            const uint32_t marker=LumaWeaveMetalDirect::kFenceMarker;
            std::memcpy(bytes.data(),&marker,4);
        }
        callback(std::move(bytes));
    }
};

static LumaGpuInputPacket packet() {
    LumaWeaveRegistry sources;
    std::vector<LumaLight> lights;
    for(uint64_t i=1;i<=18;++i) {
        LumaLight l;
        l.key=i;
        l.radius=12;
        l.position={1,1,1};
        l.intensity=2;
        lights.push_back(l);
    }
    (void)sources.publish(std::move(lights));
    LumaWeaveClusters clusters;
    LumaClusterConfig cfg;
    cfg.origin={-4,-4,-4};
    cfg.nx=cfg.ny=cfg.nz=1;
    cfg.maxFastPerCell=2;
    clusters.rebuild(sources,cfg);
    return packLumaSources(sources,clusters);
}
int main() {
    LumaFakeMetal dev;
    dev.computeEnabled=true;
    LumaWeaveMetalDirect direct;
    AIVERSE_TEST_CHECK(!direct.initialize(dev)); // backend guard
    dev.info_.backend=gfx::Backend::Metal;
    AIVERSE_TEST_CHECK(direct.initialize(dev));
    AIVERSE_TEST_CHECK(direct.ready());
    AIVERSE_TEST_CHECK(!direct.completedOutput().valid());
    AIVERSE_TEST_CHECK(direct.setSources(packet()));
    AIVERSE_TEST_CHECK(direct.sourceRevision()==1);

    auto bvh=GiGpuBvh::fromPackedTriangles({
        {{0,0,2,0},{2,0,2,0},{0,2,2,0},{1,1,1,0}}
    });
    AIVERSE_TEST_CHECK(direct.setGeometry(bvh,7));
    AIVERSE_TEST_CHECK(direct.geometryRevision()==7);

    const std::vector<LumaGpuReceiver> receivers={
        {{0,0,0,1},{0,0,1,0}},
        {{0,0,1,1},{0,0,1,0}}
    };
    AIVERSE_TEST_CHECK(!direct.dispatch(receivers,4,101).encoded);
    dev.beginFrame(800,600);
    auto batch=direct.dispatch(receivers,4,101);
    AIVERSE_TEST_CHECK(batch.encoded && batch.fenceQueued);
    AIVERSE_TEST_CHECK(batch.receivers==2 && batch.samples==4);
    AIVERSE_TEST_CHECK(batch.maximumShadowRays==8);
    AIVERSE_TEST_CHECK(batch.sourceRevision==1 && batch.geometryRevision==7);
    AIVERSE_TEST_CHECK(batch.candidateChecks>0);
    AIVERSE_TEST_CHECK(direct.pending());
    AIVERSE_TEST_CHECK(!direct.completedOutput().valid());
    AIVERSE_TEST_CHECK(dev.uniformBytes==sizeof(LumaWeaveMetalDirect::Params));
    AIVERSE_TEST_CHECK(dev.lastThreads==2 && dev.dispatchCount==1);
    for(int id:dev.bufferBindings)AIVERSE_TEST_CHECK(id>=0);
    AIVERSE_TEST_CHECK(!direct.setSources(packet()));
    AIVERSE_TEST_CHECK(!direct.setGeometry(bvh,8));
    AIVERSE_TEST_CHECK(!direct.shutdown());
    dev.endFrame();

    AIVERSE_TEST_CHECK(!direct.poll().completed);
    dev.finish(true);
    auto result=direct.poll();
    AIVERSE_TEST_CHECK(result.completed && result.success);
    AIVERSE_TEST_CHECK(result.serial==batch.serial);
    AIVERSE_TEST_CHECK(direct.completedOutput().valid());
    AIVERSE_TEST_CHECK(direct.completedReceiverCount()==2);
    AIVERSE_TEST_CHECK(direct.setGeometry(bvh,8));
    AIVERSE_TEST_CHECK(!direct.completedOutput().valid());

    dev.beginFrame(800,600);
    batch=direct.dispatch(receivers,1,202);
    AIVERSE_TEST_CHECK(batch.encoded && batch.maximumShadowRays==2);
    dev.endFrame();
    dev.finish(false);
    result=direct.poll();
    AIVERSE_TEST_CHECK(result.completed && !result.success);
    AIVERSE_TEST_CHECK(!direct.completedOutput().valid());

    // No callbacks: require a genuine caller-provided GPU completion signal.
    dev.asyncSupported=false;
    dev.beginFrame(800,600);
    batch=direct.dispatch(receivers,2,303);
    AIVERSE_TEST_CHECK(batch.encoded && !batch.fenceQueued);
    dev.endFrame();
    AIVERSE_TEST_CHECK(!direct.poll().completed);
    AIVERSE_TEST_CHECK(!direct.confirmExternalCompletion(batch.serial+1,true));
    AIVERSE_TEST_CHECK(direct.confirmExternalCompletion(batch.serial,true));
    result=direct.poll();
    AIVERSE_TEST_CHECK(result.completed && result.success);
    AIVERSE_TEST_CHECK(direct.completedOutput().valid());

    dev.beginFrame(800,600);
    AIVERSE_TEST_CHECK(!direct.dispatch(receivers,0,1).encoded);
    AIVERSE_TEST_CHECK(!direct.dispatch(receivers,9,1).encoded);
    dev.endFrame();
    AIVERSE_TEST_CHECK(direct.shutdown());
    AIVERSE_TEST_CHECK(dev.buffers.empty() && dev.textures.empty());
    const std::string shader=AIVERSE_MSL_LUMAWEAVE_DIRECT;
    AIVERSE_TEST_CHECK(shader.find("traceBvh")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("kernel void cs_main")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("access::write")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("constant LumaParams& p [[buffer(8)]]")!=std::string::npos);
    AIVERSE_TEST_CHECK(shader.find("full?p.limits.x:entry.count")!=std::string::npos);
}
