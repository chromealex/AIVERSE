#include "engine/render/GiMainCpuBridge.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <memory>
#include <vector>
using namespace aiverse::render;
struct FaultDevice final : FakeDevice {
    bool failUploads=false;
    void updateTexture(gfx::TextureHandle t,int x,int y,int w,int h,
                       const void* pixels,int pitch) override {
        if(failUploads)throw std::runtime_error("injected GI texture upload failure");
        FakeDevice::updateTexture(t,x,y,w,h,pixels,pitch);
    }
};
int main() {
    FaultDevice device;
    GiMainCpuBridge bridge;
    GiMainCpuConfig cfg;
    cfg.origin={-1,-1,-1}; cfg.spacing=1;
    cfg.nx=3;cfg.ny=3;cfg.nz=2;
    cfg.samples=16;cfg.traceDistance=6;
    cfg.cpuBudgetMs=1.0;cfg.maxProbesPerFrame=2;
    AIVERSE_TEST_CHECK(bridge.initialize(device,cfg));
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    GiChunkedGeometryBuilder builder;
    std::vector<StaticShadowGeometry::Triangle> wall={
        {{-4,-4,1},{4,-4,1},{4,4,1},0},
        {{-4,-4,1},{4,4,1},{-4,4,1},0}};
    builder.replace(1,wall,{{1,1,1}});
    const auto scene=std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
    AIVERSE_TEST_CHECK(bridge.publishGeometry(scene,1));
    AIVERSE_TEST_CHECK(bridge.borrowedAtlas().valid());
    AIVERSE_TEST_CHECK(bridge.publishLights({GiMovingLight{5,{0,0,0},{1,0,0},4,20}}));
    const auto frame=bridge.updateFrame({0,0,0});
    AIVERSE_TEST_CHECK(frame.canPresent);
    AIVERSE_TEST_CHECK(frame.traced<=2 && frame.published<=frame.traced);
    AIVERSE_TEST_CHECK(frame.uploaded==frame.published);
    // Source changes create new dirty probes. A failed GPU upload after CPU
    // tracing MUST close the presentation gate until a full atlas flush.
    AIVERSE_TEST_CHECK(bridge.publishLights({
        GiMovingLight{5,{0,0,0},{1,0,0},4,30}}));
    device.failUploads=true;
    bool uploadFailed=false;
    try { (void)bridge.updateFrame({0,0,0}); }
    catch(const std::runtime_error&) {uploadFailed=true;}
    AIVERSE_TEST_CHECK(uploadFailed);
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    AIVERSE_TEST_CHECK(!bridge.updateFrame({0,0,0}).canPresent);
    // An unchanged snapshot has no dirty rows; it cannot repair a partial atlas.
    AIVERSE_TEST_CHECK(bridge.publishLights({
        GiMovingLight{5,{0,0,0},{1,0,0},4,30}}));
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    AIVERSE_TEST_CHECK(bridge.publishEnvironment({}));
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    device.failUploads=false;
    AIVERSE_TEST_CHECK(bridge.flushAll());
    AIVERSE_TEST_CHECK(bridge.borrowedAtlas().valid());
    // A failed newer geometry publication may not expose stale GI.
    AIVERSE_TEST_CHECK(!bridge.publishGeometry(nullptr,2));
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    AIVERSE_TEST_CHECK(!bridge.updateFrame({0,0,0}).canPresent);
    AIVERSE_TEST_CHECK(!bridge.flushAll());
    AIVERSE_TEST_CHECK(bridge.publishLights({}));
    AIVERSE_TEST_CHECK(bridge.publishEnvironment({}));
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    AIVERSE_TEST_CHECK(!bridge.updateFrame({0,0,0}).canPresent);
    AIVERSE_TEST_CHECK(bridge.publishGeometry(scene,2));
    AIVERSE_TEST_CHECK(bridge.borrowedAtlas().valid());
    AIVERSE_TEST_CHECK(bridge.publishLights({}));
    AIVERSE_TEST_CHECK(bridge.borrowedAtlas().valid());
    // Accepted geometry with an upload failure can recover via a full upload;
    // unlike rejected geometry, its CPU snapshot is current and owned.
    device.failUploads=true;
    uploadFailed=false;
    try { (void)bridge.publishGeometry(scene,3); }
    catch(const std::runtime_error&) {uploadFailed=true;}
    AIVERSE_TEST_CHECK(uploadFailed);
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    device.failUploads=false;
    AIVERSE_TEST_CHECK(bridge.flushAll());
    AIVERSE_TEST_CHECK(bridge.borrowedAtlas().valid());
    // No actual draws in this FakeDevice: explicit host idle is legal.
    AIVERSE_TEST_CHECK(bridge.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(!bridge.borrowedAtlas().valid());
    AIVERSE_TEST_CHECK(bridge.pendingRetiredTextures()==0);
}
