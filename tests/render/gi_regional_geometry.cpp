#include "engine/render/GiDynamicRuntime.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

static StaticShadowGeometry floorGeometry() {
    StaticShadowGeometry geometry;
    // Broad opaque plane faces the probe volume; both sides receive bounce.
    geometry.triangles = {
        {{-20,-10,1},{90,-10,1},{90,10,1},0},
        {{-20,-10,1},{90,10,1},{-20,10,1},0}
    };
    geometry.albedos = {{1,1,1}};
    geometry.build();
    return geometry;
}

int main() {
    FakeDevice dev;
    GiDynamicRuntime runtime;
    AIVERSE_TEST_CHECK(runtime.initialize(dev, {0,0,0}, 1, 64, 1, 1, 32, 2));
    auto oldGeometry = floorGeometry();
    auto newGeometry = floorGeometry();

    runtime.setGeometry(&oldGeometry, 1);
    runtime.setLights({
        GiMovingLight{10, {6,.5f,0}, {1,0,0}, 6, 40},
        GiMovingLight{20, {48,.5f,0}, {0,0,1}, 6, 40}
    });
    while (runtime.reference().scheduler().pending()) {
        const auto step = runtime.update(16, {6,.5f,.5f});
        AIVERSE_TEST_CHECK(step.trace.published>0);
    }
    const auto tex = runtime.atlas().texture();
    const auto before = dev.textures.at(tex.id).data;
    AIVERSE_TEST_CHECK(before.size()==384); // 64 probes * 3 texels, padded to row width 384
    constexpr size_t nearProbe = 5, farProbe = 48;
    AIVERSE_TEST_CHECK(runtime.reference().coefficient(uint32_t(farProbe)).evaluate({0,0,1}).z>0);
    AIVERSE_TEST_CHECK(runtime.reference().scheduler().pending()==0);

    const GiBounds edit{{5,-1,0},{6,2,1}};
    const size_t affected = runtime.setGeometryRegion(&newGeometry, 2, edit);
    AIVERSE_TEST_CHECK(affected>0 && affected<64);
    AIVERSE_TEST_CHECK(runtime.reference().scheduler().pending()==affected);
    // Dirty transport is zeroed immediately on CPU and uploaded without tracing.
    AIVERSE_TEST_CHECK(runtime.reference().coefficient(uint32_t(nearProbe)).evaluate({0,0,1}).x==0);
    const auto clear = runtime.update(0,{6,.5f,.5f});
    AIVERSE_TEST_CHECK(clear.trace.primaryRays==0);
    AIVERSE_TEST_CHECK(clear.upload.probes==affected);
    AIVERSE_TEST_CHECK(clear.upload.bytes==affected*3*sizeof(float4));
    const auto& after = dev.textures.at(tex.id).data;
    for(size_t k=0;k<3;++k) {
        const auto zero=after[nearProbe*3+k];
        AIVERSE_TEST_CHECK(zero.x==0 && zero.y==0 && zero.z==0 && zero.w==0);
        const auto left=before[farProbe*3+k],right=after[farProbe*3+k];
        AIVERSE_TEST_CHECK(left.x==right.x && left.y==right.y && left.z==right.z && left.w==right.w);
    }

    // Only the region is retraced. No full atlas transfer on a local edit.
    size_t updated=0;
    while(runtime.reference().scheduler().pending()) {
        const auto step=runtime.update(4,{6,.5f,.5f});
        AIVERSE_TEST_CHECK(step.trace.claimed<=4);
        updated+=step.trace.published;
    }
    AIVERSE_TEST_CHECK(updated==affected);

    // Subsequent overlapping edits before the next update are coalesced by upload().
    const size_t first=runtime.setGeometryRegion(&oldGeometry,3,edit);
    const size_t second=runtime.setGeometryRegion(&newGeometry,4,edit);
    AIVERSE_TEST_CHECK(first==second);
    const auto merged=runtime.update(0,{6,.5f,.5f});
    AIVERSE_TEST_CHECK(merged.upload.probes==affected);
    AIVERSE_TEST_CHECK(merged.trace.primaryRays==0);

    // Invalid ranges cannot replace the previously valid geometry revision.
    bool failed=false;
    try { (void)runtime.setGeometryRegion(&oldGeometry,5,{{5,0,0},{4,1,1}}); }
    catch(const std::invalid_argument&) { failed=true; }
    AIVERSE_TEST_CHECK(failed);

    // Missing geometry needs full setGeometry() rather than a partial update.
    runtime.setGeometry(nullptr,6);
    failed=false;
    try { (void)runtime.setGeometryRegion(&oldGeometry,7,edit); }
    catch(const std::logic_error&) { failed=true; }
    AIVERSE_TEST_CHECK(failed);
    runtime.shutdown();
    AIVERSE_TEST_CHECK(dev.textures.empty());
}
