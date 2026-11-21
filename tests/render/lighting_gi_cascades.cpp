// GiMainGpuBridge cascades: nested lattices in one atlas, finest first,
// residency over the outermost cascade.
#include "engine/render/GiMainGpuBridge.h"
#include "engine/render/GiDynamicShaders.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <memory>
#include <string>
using namespace aiverse::render;
struct FenceDevice:FakeDevice {
    uint64_t submitted=1,completed=0;
    FenceDevice(){info_.backend=gfx::Backend::Metal;computeEnabled=true;}
    bool supports(gfx::PixelFormat) const override{return true;}
    uint64_t gpuSubmissionFence() const override{return inFrame?submitted:0;}
    uint64_t gpuCompletedFence() const override{return completed;}
    bool gpuHealthy() const override{return true;}
    bool computeDispatchSucceeded() const override{return dispatchCount>0;}
};
static bool near(float a,float b){return std::abs(a-b)<1e-4f;}
int main() {
    GiMainCpuConfig cfg;cfg.origin={-2,-2,-2};cfg.spacing=1;cfg.nx=cfg.ny=cfg.nz=4;
    cfg.samples=16;cfg.traceDistance=4;cfg.cascades=3;
    // Cascade k: same center (0,0,0), spacing 2^k, rays 4 * 2^k.
    const auto c2=GiMainGpuBridge::cascadeConfig(cfg,2);
    AIVERSE_TEST_CHECK(near(c2.spacing,4) && near(c2.traceDistance,16));
    AIVERSE_TEST_CHECK(near(c2.origin.x,-8) && near(c2.origin.y,-8) && near(c2.origin.z,-8));
    // The CPU bridge keeps one cascade.
    {FenceDevice d;GiMainCpuBridge cpu;AIVERSE_TEST_CHECK(!cpu.initialize(d,cfg));}
    GiMainCpuConfig bad=cfg;bad.cascades=4;
    {FenceDevice d;GiMainGpuBridge b;AIVERSE_TEST_CHECK(!b.initialize(d,bad));}

    // Residency covers the outermost cascade: a chunk at x=90 is beyond
    // cascade 0's reach (x <= 2 + 2*4 + 64 + 1) but within cascade 2's (x <= 8 + 32 + 64 + 4).
    GiChunkedGeometryBuilder builder;
    builder.replace(1,{{{-1,-1,1},{1,-1,1},{1,1,1},0}},{{1,1,1}});
    builder.replace(2,{{{90,0,0},{91,0,0},{90,1,0},0}},{{1,1,1}});
    auto scene=std::make_shared<const GiChunkedGeometrySnapshot>(builder.publish());
    GiMainCpuConfig single=cfg;single.cascades=1;
    AIVERSE_TEST_CHECK(GiMainGpuBridge::planResidency(*scene,single).excludedRemoteChunks==1);
    AIVERSE_TEST_CHECK(GiMainGpuBridge::planResidency(*scene,cfg).excludedRemoteChunks==0);

    FenceDevice d;GiMainGpuBridge bridge;
    AIVERSE_TEST_CHECK(bridge.initialize(d,cfg));
    AIVERSE_TEST_CHECK(bridge.cascadeCount()==3);
    AIVERSE_TEST_CHECK(bridge.cascadeProbeOffset(0)==0 && bridge.cascadeProbeOffset(1)==64 &&
                       bridge.cascadeProbeOffset(2)==128);
    AIVERSE_TEST_CHECK(near(bridge.cascade(1).spacing,2));
    d.beginFrame(16,16);
    AIVERSE_TEST_CHECK(bridge.publishGeometry(scene,1));
    AIVERSE_TEST_CHECK(bridge.publishLights({}));
    AIVERSE_TEST_CHECK(bridge.pendingProbes()==192);
    // Finest cascade first: 64 probes at 32 per frame, then the others.
    AIVERSE_TEST_CHECK(bridge.updateFrame({0,0,0}));
    d.completed=1;bridge.poll();
    AIVERSE_TEST_CHECK(bridge.pendingProbes()==160);
    int frames=0;
    while(bridge.pendingProbes() && frames<32) {
        ++d.submitted;
        AIVERSE_TEST_CHECK(bridge.updateFrame({0,0,0}));
        d.completed=d.submitted;bridge.poll();++frames;
    }
    AIVERSE_TEST_CHECK(!bridge.pendingProbes() && frames==5);
    AIVERSE_TEST_CHECK(bridge.canPresent());
    // A region edit near the center dirties probes in every cascade.
    AIVERSE_TEST_CHECK(bridge.publishGeometryRegion(scene,2,GiBounds{{-1,-1,1},{1,1,1}}));
    AIVERSE_TEST_CHECK(bridge.pendingProbes()>64);
    d.endFrame();
    AIVERSE_TEST_CHECK(bridge.shutdownAfterGpuIdle());
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());

    // Shader ABI: one atlas, U6 grows to nine float4, call sites unchanged.
    const std::string glsl=AIVERSE_GLSL_DYNAMIC_GI, msl=AIVERSE_MSL_DYNAMIC_GI;
    AIVERSE_TEST_CHECK(glsl.find("vec4 u_dynamicGiCascades[6]")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("vec3 dynamicGiAt(vec3 p, vec3 n)")!=std::string::npos);
    AIVERSE_TEST_CHECK(msl.find("float4 cascades[6]")!=std::string::npos);
    AIVERSE_TEST_CHECK(glsl.find("tex15")==std::string::npos && msl.find("tex15")==std::string::npos);
    return 0;
}
