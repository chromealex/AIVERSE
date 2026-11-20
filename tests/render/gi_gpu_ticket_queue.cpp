#include "engine/render/GiGpuTicketQueue.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <cstring>
#include <vector>

using namespace aiverse::render;

struct FenceDevice : FakeDevice {
    bool asyncEnabled=true;
    uint64_t submittedVkFrame=1, completedVkFrame=0;
    bool vulkanHealthy=true;
    uint64_t frameSerial() const override {
        return info_.backend==gfx::Backend::Vulkan && inFrame ? submittedVkFrame : 0;
    }
    uint64_t completedFrameSerial() const override { return completedVkFrame; }
    bool gpuHealthy() const override { return vulkanHealthy; }
    std::vector<BufferReadFn> readers;
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    bool readBufferAsync(gfx::BufferHandle h,size_t bytes,BufferReadFn fn) override {
        if (!asyncEnabled || !inFrame || inCompute || bytes!=sizeof(uint32_t) ||
            !buffers.contains(h.id))return false;
        readers.push_back(std::move(fn));
        return true;
    }
    void finishRead(bool success) {
        AIVERSE_TEST_CHECK(!readers.empty());
        auto fn=std::move(readers.front());
        readers.erase(readers.begin());
        std::vector<uint8_t> bytes;
        if (success) {
            constexpr uint32_t marker=0x47494631u;
            bytes.resize(sizeof(marker));
            std::memcpy(bytes.data(),&marker,sizeof(marker));
        }
        fn(std::move(bytes));
    }
};
static GiChunkedGeometrySnapshot makeScene() {
    GiChunkedGeometryBuilder builder;
    builder.replace(1,{
        {{-4,-4,2},{4,-4,2},{4,4,2},0},
        {{-4,-4,2},{4,4,2},{-4,4,2},0}
    },{{1,0,0}});
    return builder.publish();
}

int main() {
    FenceDevice d;
    d.info_.backend=gfx::Backend::Metal;
    d.computeEnabled=true;
    GiGpuSmallScene gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(d,4));
    GiGpuTicketQueue queue;
    AIVERSE_TEST_CHECK(queue.initialize(d));
    auto geometry=makeScene();
    AIVERSE_TEST_CHECK(queue.setGeometry(gpu,geometry,1));
    AIVERSE_TEST_CHECK(queue.setLights(gpu,{GiMovingLight{1,{0,0,0},{1,0,0},5,10}}));

    GiUpdateScheduler s;
    s.configure({0,0,0},1,4,1,1);
    d.beginFrame(200,150);
    const auto first=queue.submit(gpu,s,2,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(first.submitted && first.fenceQueued && first.probes==2);
    AIVERSE_TEST_CHECK(queue.pending()==1);
    AIVERSE_TEST_CHECK(s.inFlight()==2 && s.pending()==4);
    AIVERSE_TEST_CHECK(!queue.safeToSample(s));
    AIVERSE_TEST_CHECK(!queue.setGeometry(gpu,geometry,2));
    d.endFrame();
    AIVERSE_TEST_CHECK(queue.poll(s).completed==0);

    // Invalidation while GPU work is in flight must reject old results.
    AIVERSE_TEST_CHECK(s.invalidate({{.5f,.5f,.5f},{.5f,.5f,.5f}},
                                     GiDirtyReason::Lighting)==1);
    d.finishRead(true);
    auto stats=queue.poll(s);
    AIVERSE_TEST_CHECK(stats.completed==1);
    AIVERSE_TEST_CHECK(stats.accepted==1 && stats.stale==1);
    AIVERSE_TEST_CHECK(s.inFlight()==0 && s.pending()==3);

    // Error callback aborts but never acknowledges the scheduler tickets.
    d.beginFrame(200,150);
    const auto failed=queue.submit(gpu,s,4,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(failed.submitted);
    d.endFrame();
    d.finishRead(false);
    stats=queue.poll(s);
    AIVERSE_TEST_CHECK(stats.aborted==3 && stats.pending==0);
    AIVERSE_TEST_CHECK(s.pending()==3);

    // Successful callback acknowledges only the eventual valid source revision.
    d.beginFrame(200,150);
    const auto retried=queue.submit(gpu,s,4,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(retried.submitted && retried.probes==3);
    d.endFrame();
    d.finishRead(true);
    stats=queue.poll(s);
    AIVERSE_TEST_CHECK(stats.accepted==3 && stats.stale==0);
    AIVERSE_TEST_CHECK(queue.safeToSample(s));
    AIVERSE_TEST_CHECK(queue.pending()==0);
    AIVERSE_TEST_CHECK(queue.setGeometry(gpu,geometry,2));

    // Unsupported RHI readback cannot silently fake GPU completion.
    d.asyncEnabled=false;
    s.invalidateAll(GiDirtyReason::Material);
    d.beginFrame(200,150);
    const auto manual=queue.submit(gpu,s,4,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(manual.submitted && !manual.fenceQueued);
    d.endFrame();
    AIVERSE_TEST_CHECK(queue.poll(s).completed==0);
    AIVERSE_TEST_CHECK(!queue.shutdown());
    AIVERSE_TEST_CHECK(!queue.confirmExternalCompletion(manual.serial+1,true));
    AIVERSE_TEST_CHECK(queue.confirmExternalCompletion(manual.serial,true));
    stats=queue.poll(s);
    AIVERSE_TEST_CHECK(stats.accepted==4 && queue.safeToSample(s));
    AIVERSE_TEST_CHECK(queue.shutdown());
    gpu.shutdown();
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());

    // Vulkan must NOT acknowledge a command merely because endFrame submitted
    // it. Only the owning VkDevice's actually completed frame serial counts.
    d.info_.backend=gfx::Backend::Vulkan;
    d.submittedVkFrame=10;
    d.completedVkFrame=0;
    d.vulkanHealthy=true;
    AIVERSE_TEST_CHECK(gpu.initialize(d,4));
    AIVERSE_TEST_CHECK(queue.initialize(d));
    AIVERSE_TEST_CHECK(queue.setGeometry(gpu,geometry,3));
    AIVERSE_TEST_CHECK(queue.setLights(gpu,{GiMovingLight{5,{0,0,0},{0,1,0},5,10}}));
    GiUpdateScheduler vs;
    vs.configure({0,0,0},1,4,1,1);
    d.beginFrame(200,150);
    const auto vkBatch=queue.submit(gpu,vs,4,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(vkBatch.submitted && vkBatch.fenceQueued && vkBatch.probes==4);
    AIVERSE_TEST_CHECK(d.readers.empty());  // Vulkan never relies on Metal readback.
    d.endFrame();
    AIVERSE_TEST_CHECK(queue.poll(vs).completed==0);
    AIVERSE_TEST_CHECK(vs.inFlight()==4 && !queue.safeToSample(vs));
    AIVERSE_TEST_CHECK(!queue.confirmExternalCompletion(vkBatch.serial,true));
    d.completedVkFrame=9;
    AIVERSE_TEST_CHECK(queue.poll(vs).completed==0);
    d.completedVkFrame=10; // Simulates the backend seeing the VkFence signalled.
    auto vkStats=queue.poll(vs);
    AIVERSE_TEST_CHECK(vkStats.accepted==4 && queue.safeToSample(vs));

    // A late old geometry/light result must not accept a newer ticket revision.
    vs.invalidateAll(GiDirtyReason::Environment);
    d.submittedVkFrame=11;
    d.beginFrame(200,150);
    const auto oldVk=queue.submit(gpu,vs,4,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(oldVk.submitted);
    d.endFrame();
    vs.invalidate({{.5f,.5f,.5f},{.5f,.5f,.5f}},GiDirtyReason::Lighting);
    d.completedVkFrame=11;
    vkStats=queue.poll(vs);
    AIVERSE_TEST_CHECK(vkStats.accepted==3 && vkStats.stale==1);
    AIVERSE_TEST_CHECK(vs.pending()==1);

    // Device-lost after submission aborts rather than publishing stale GI.
    d.submittedVkFrame=12;
    d.beginFrame(200,150);
    const auto lost=queue.submit(gpu,vs,1,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(lost.submitted);
    d.endFrame();
    d.vulkanHealthy=false;
    d.completedVkFrame=11; // lost submission NEVER produced a VkFence signal
    AIVERSE_TEST_CHECK(d.completedFrameSerial()<d.submittedVkFrame);
    vkStats=queue.poll(vs);
    AIVERSE_TEST_CHECK(vkStats.aborted==1 && vkStats.pending==0);
    AIVERSE_TEST_CHECK(vs.pending()==1 && vs.inFlight()==0);
    AIVERSE_TEST_CHECK(!queue.safeToSample(vs));
    d.beginFrame(200,150);
    const auto unhealthy=queue.submit(gpu,vs,1,{0,0,0},16,6);
    AIVERSE_TEST_CHECK(!unhealthy.submitted && queue.pending()==0);
    AIVERSE_TEST_CHECK(vs.pending()==1); // retrace only after device recovery
    d.endFrame();
    AIVERSE_TEST_CHECK(queue.shutdown());
    gpu.shutdown();
    AIVERSE_TEST_CHECK(d.buffers.empty() && d.textures.empty());
}
