#include "engine/render/GiMainGpuBridge.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
using namespace aiverse::render;
struct FenceDevice:FakeDevice {
    uint64_t submitted=1,completed=0;
    bool healthy=true;
    int failWrite=0;
    bool updateSharedBufferChecked(gfx::BufferHandle h,const void* p,size_t n,size_t offset=0) override {
        if(failWrite && --failWrite==0)return false;
        updateBuffer(h,p,n,offset);return true;
    }
    FenceDevice(){info_.backend=gfx::Backend::Metal;computeEnabled=true;}
    bool supports(gfx::PixelFormat) const override{return true;}
    uint64_t gpuSubmissionFence() const override{return inFrame?submitted:0;}
    uint64_t gpuCompletedFence() const override{return completed;}
    bool gpuHealthy() const override{return healthy;}
    bool computeDispatchSucceeded() const override{return dispatchCount>0;}
};
int main() {
    FenceDevice d;GiMainGpuBridge bridge;
    GiMainCpuConfig cfg;cfg.nx=cfg.ny=cfg.nz=1;cfg.samples=16;
    AIVERSE_TEST_CHECK(bridge.initialize(d,cfg));
    GiChunkedGeometryBuilder builder;
    auto scene=std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
    d.beginFrame(16,16);
    AIVERSE_TEST_CHECK(bridge.publishGeometry(scene,1));
    AIVERSE_TEST_CHECK(bridge.publishLights({}));
    AIVERSE_TEST_CHECK(bridge.updateFrame({}));
    AIVERSE_TEST_CHECK(bridge.busy() && !bridge.canPresent());
    d.endFrame();d.submitted=2;d.beginFrame(16,16);
    bridge.poll();AIVERSE_TEST_CHECK(!bridge.canPresent());
    d.completed=1;bridge.poll();AIVERSE_TEST_CHECK(bridge.canPresent());
    AIVERSE_TEST_CHECK(bridge.borrowForFrame().valid());
    // Compute completion is insufficient: the graphics reader at fence 2 is live.
    AIVERSE_TEST_CHECK(!bridge.publishLights({}));
    AIVERSE_TEST_CHECK(!bridge.canPresent());
    d.completed=2;d.submitted=3;
    AIVERSE_TEST_CHECK(bridge.publishLights({}));
    AIVERSE_TEST_CHECK(bridge.updateFrame({}));
    d.completed=3;bridge.poll();AIVERSE_TEST_CHECK(bridge.canPresent());
    GiMovingLight invalid;invalid.radius=-1;
    AIVERSE_TEST_CHECK(!bridge.publishLights({invalid}));
    AIVERSE_TEST_CHECK(bridge.publishEnvironment({}));
    AIVERSE_TEST_CHECK(!bridge.updateFrame({}) && !bridge.canPresent());
    AIVERSE_TEST_CHECK(bridge.publishLights({}));
    AIVERSE_TEST_CHECK(bridge.updateFrame({}));bridge.poll();
    AIVERSE_TEST_CHECK(!bridge.publishGeometry(nullptr,2));
    AIVERSE_TEST_CHECK(bridge.publishLights({}));
    AIVERSE_TEST_CHECK(!bridge.canPresent());
    AIVERSE_TEST_CHECK(bridge.publishGeometry(scene,2));
    AIVERSE_TEST_CHECK(bridge.updateFrame({}));
    d.healthy=false;bridge.poll();AIVERSE_TEST_CHECK(!bridge.canPresent());
    // Fake device has no actual queue; restore health only to drain mock resources.
    d.healthy=true;d.completed=3;d.endFrame();
    AIVERSE_TEST_CHECK(bridge.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());
    GiGpuSmallScene transport;
    d.beginFrame(16,16);
    AIVERSE_TEST_CHECK(transport.initialize(d,1));
    GiChunkedGeometryBuilder geometry;
    geometry.replace(1,{{{0,0,0},{1,0,0},{0,1,0},0}},{{1,1,1}});
    const auto original=geometry.publish();
    AIVERSE_TEST_CHECK(transport.setGeometry(original,1));
    geometry.replace(1,{{{0,0,1},{1,0,1},{0,1,1},0}},{{1,1,1}});
    d.failWrite=2; // triangle write succeeds, node write fails
    AIVERSE_TEST_CHECK(!transport.setGeometry(geometry.publish(),2));
    // Reverting source after a partial write MUST upload the entire old packet.
    AIVERSE_TEST_CHECK(transport.setGeometry(original,2));
    AIVERSE_TEST_CHECK(transport.lastGeometryUpload().full);
    AIVERSE_TEST_CHECK(transport.lastGeometryUpload().bytes==sizeof(GiGpuBvhTri)+sizeof(GiGpuBvhNode));
    const unsigned before=d.dispatchCount;
    d.failWrite=1;
    AIVERSE_TEST_CHECK(!transport.dispatch({{0,1,1,{0,0,0}}},16,8).submitted);
    AIVERSE_TEST_CHECK(d.dispatchCount==before);
    d.endFrame();AIVERSE_TEST_CHECK(transport.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());
}
