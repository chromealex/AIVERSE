#include "engine/render/GiGpuCascadeRuntime.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace aiverse::render;

struct CascadeFake : FakeDevice {
    std::vector<BufferReadFn> completions;
    bool supports(gfx::PixelFormat fmt) const override {
        return fmt==gfx::PixelFormat::RGBA16F || FakeDevice::supports(fmt);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t bytes,BufferReadFn callback) override {
        if (!inFrame || inCompute || bytes!=4 || !buffers.contains(h.id))return false;
        completions.push_back(std::move(callback));
        return true;
    }
    void finishAll() {
        auto callbacks=std::move(completions);
        completions.clear();
        for(auto& callback:callbacks) {
            const uint32_t marker=0x47494631u;
            std::vector<uint8_t> bytes(4);
            std::memcpy(bytes.data(),&marker,4);
            callback(std::move(bytes));
        }
    }
};
static std::shared_ptr<const GiChunkedGeometrySnapshot> scene() {
    GiChunkedGeometryBuilder builder;
    builder.replace(5,{
        {{-10,-10,2},{10,-10,2},{10,10,2},0},
        {{-10,-10,2},{10,10,2},{-10,10,2},0}
    },{{1,1,1}});
    return std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
}

int main() {
    CascadeFake d;
    d.computeEnabled=true;
    d.info_.backend=gfx::Backend::Metal;
    GiGpuCascadeRuntime cascade;
    GiCascadeConfig cfg;
    cfg.nx=4;cfg.ny=2;cfg.nz=2;cfg.count=3;
    cfg.raysPerProbe=16;
    cfg.maxResidentProbes=48;
    AIVERSE_TEST_CHECK(cascade.initialize(d,cfg,{0,0,0}));
    AIVERSE_TEST_CHECK(cascade.ready() && cascade.count()==3);
    AIVERSE_TEST_CHECK(cascade.residentProbes()==48);
    AIVERSE_TEST_CHECK(cascade.level(0).volume().spacing()==1);
    AIVERSE_TEST_CHECK(cascade.level(1).volume().spacing()==2);
    AIVERSE_TEST_CHECK(cascade.level(2).volume().spacing()==4);
    AIVERSE_TEST_CHECK(cascade.setGeometry(scene(),1));
    AIVERSE_TEST_CHECK(cascade.setLights({
        GiMovingLight{1,{0,0,0},{1,0,0},5,10}
    }));
    d.beginFrame(320,240);
    auto step=cascade.updateFrame(10,{0,0,0});
    AIVERSE_TEST_CHECK(step.levels.size()==3);
    AIVERSE_TEST_CHECK(step.submitted==10 && step.allocatedBudget==10);
    AIVERSE_TEST_CHECK(step.levels[0].submitted.probes==8);
    AIVERSE_TEST_CHECK(step.levels[1].submitted.probes==1);
    AIVERSE_TEST_CHECK(step.levels[2].submitted.probes==1);
    AIVERSE_TEST_CHECK(step.validLevels==0 && step.inFlight==10);
    AIVERSE_TEST_CHECK(cascade.busy());
    AIVERSE_TEST_CHECK(!cascade.shutdown());
    d.endFrame();
    d.finishAll();

    for(int pass=0;pass<12;++pass) {
        d.beginFrame(320,240);
        step=cascade.updateFrame(12,{0,0,0});
        AIVERSE_TEST_CHECK(step.submitted<=12);
        d.endFrame();
        d.finishAll();
        if(step.pending==0 && step.inFlight==0 && !cascade.busy())break;
    }
    d.beginFrame(320,240);
    step=cascade.updateFrame(0,{0,0,0});
    AIVERSE_TEST_CHECK(step.pending==0 && step.inFlight==0);
    AIVERSE_TEST_CHECK(step.validLevels==3);
    d.endFrame();

    // A camera displacement shorter than second-level spacing only scrolls
    // the finest cascade; other atlas slabs remain valid.
    d.beginFrame(320,240);
    step=cascade.updateFrame(0,{1.2f,0,0});
    AIVERSE_TEST_CHECK(step.levels[0].scroll.changed);
    AIVERSE_TEST_CHECK(step.levels[0].scroll.recycled==4);
    AIVERSE_TEST_CHECK(!step.levels[1].scroll.changed);
    AIVERSE_TEST_CHECK(!step.levels[2].scroll.changed);
    // Fine level still has 12 valid probes; the R8 validity mask suppresses
    // only its entering slab, so all three levels have safe partial output.
    AIVERSE_TEST_CHECK(step.validLevels==3);
    AIVERSE_TEST_CHECK(cascade.level(0).validity().validCount()==12);
    d.endFrame();
    AIVERSE_TEST_CHECK(cascade.shutdown());
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());

    // Reject over-budget allocations before creating any RHI resources.
    GiCascadeConfig invalid=cfg;
    invalid.maxResidentProbes=47;
    AIVERSE_TEST_CHECK(!cascade.initialize(d,invalid,{0,0,0}));
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());
    invalid=cfg;invalid.count=4;
    AIVERSE_TEST_CHECK(!cascade.initialize(d,invalid,{0,0,0}));
}
