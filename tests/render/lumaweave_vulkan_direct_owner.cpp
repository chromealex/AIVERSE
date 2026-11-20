// Device-free production-owner contract. FakeDevice confirms transactional
// publication, work caps and frame fence gating; NOT actual Vulkan pixels.
#include "engine/render/LumaWeaveVulkanDirect.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <vector>

using namespace aiverse::render;

struct VulkanComputeFake final : FakeDevice {
    uint64_t current=9,completed=0;
    VulkanComputeFake() {
        info_.backend=gfx::Backend::Vulkan;
        computeEnabled=true;
    }
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    uint64_t frameSerial() const override {return current;}
    uint64_t completedFrameSerial() const override {return completed;}
};

int main() {
    VulkanComputeFake device;
    LumaWeaveVulkanDirect direct;
    AIVERSE_TEST_CHECK(direct.initialize(device));
    AIVERSE_TEST_CHECK(direct.ready() && !direct.busy());
    AIVERSE_TEST_CHECK(!direct.completedOutput().valid());

    LumaWeaveRegistry sources;
    LumaLight light;
    light.key=42;light.position={0,2,0};light.radius=4;
    (void)sources.publish({light});
    AIVERSE_TEST_CHECK(direct.setSources(sources));
    AIVERSE_TEST_CHECK(direct.sourceCount()==1);
    // Two independent registries can have identical revisions but different
    // emitters. Never silently reuse old GPU source energy.
    LumaWeaveRegistry foreign;
    LumaLight changed=light;
    changed.color={0,0,1};
    (void)foreign.publish({changed});
    AIVERSE_TEST_CHECK(foreign.revision()==sources.revision());
    AIVERSE_TEST_CHECK(!direct.setSources(foreign));
    AIVERSE_TEST_CHECK(direct.sourceCount()==1);
    GiGpuBvh empty;
    AIVERSE_TEST_CHECK(direct.setGeometry(empty,1));
    AIVERSE_TEST_CHECK(direct.geometryRevision()==1);
    AIVERSE_TEST_CHECK(!direct.setGeometry(empty,0)); // monotonic generation
    std::array<LumaGpuReceiver,2> receivers{{
        {{0,0,0,1},{0,1,0,0}},
        {{2,0,0,1},{0,1,0,0}}
    }};
    device.beginFrame(64,64);
    const auto submitted=direct.dispatch(receivers,2,1234);
    AIVERSE_TEST_CHECK(submitted.encoded && submitted.frameSerial==9);
    AIVERSE_TEST_CHECK(submitted.sourceRevision==sources.revision());
    AIVERSE_TEST_CHECK(submitted.geometryRevision==1);
    AIVERSE_TEST_CHECK(submitted.receivers==2 && submitted.samples==2);
    AIVERSE_TEST_CHECK(submitted.candidateChecks==6);
    AIVERSE_TEST_CHECK(submitted.maximumShadowRays==4);
    AIVERSE_TEST_CHECK(device.dispatchCount==1);
    AIVERSE_TEST_CHECK(direct.busy() && direct.pendingFrameSerial()==9);
    AIVERSE_TEST_CHECK(direct.encodedOutput(9).valid());
    AIVERSE_TEST_CHECK(!direct.encodedOutput(8).valid());
    AIVERSE_TEST_CHECK(!direct.completedOutput().valid());
    AIVERSE_TEST_CHECK(!direct.shutdown());
    AIVERSE_TEST_CHECK(!direct.dispatch(receivers,2,1).encoded);
    AIVERSE_TEST_CHECK(!direct.setSources(sources));
    device.endFrame();
    AIVERSE_TEST_CHECK(!direct.poll()); // endFrame is NOT a completed fence
    device.completed=8;
    AIVERSE_TEST_CHECK(!direct.poll());
    device.completed=9; // mock delivered a REAL completed-frame serial
    AIVERSE_TEST_CHECK(direct.poll());
    AIVERSE_TEST_CHECK(!direct.busy());
    AIVERSE_TEST_CHECK(direct.completedOutput().valid());
    AIVERSE_TEST_CHECK(direct.completedReceiverCount()==2);

    // A malformed BVH must not mutate accepted GPU geometry or allocate
    // new storage. Caller may recover with a freshly valid scene revision.
    GiGpuBvh corrupt;
    corrupt.nodes.push_back({});
    const uint32_t before=device.next;
    AIVERSE_TEST_CHECK(!direct.setGeometry(corrupt,1)); // duplicate rev, different content
    AIVERSE_TEST_CHECK(!direct.setGeometry(corrupt,2));
    AIVERSE_TEST_CHECK(device.next==before);
    AIVERSE_TEST_CHECK(direct.geometryRevision()==1);
    const GiGpuBvhTri triangle{
        {-2,.5f,-2,0},{2,.5f,-2,0},{0,.5f,2,0},{1,1,1,0}};
    GiGpuBvh good=GiGpuBvh::fromPackedTriangles({triangle});
    AIVERSE_TEST_CHECK(direct.setGeometry(good,2));
    AIVERSE_TEST_CHECK(direct.geometryRevision()==2);

    // No truncation: if 4096 lights * receivers * samples exceeds
    // the GPU worst-case candidate budget, reject the entire dispatch.
    std::vector<LumaLight> crowded;
    crowded.reserve(LumaWeaveRegistry::kMaxSources);
    for(uint64_t key=1;key<=LumaWeaveRegistry::kMaxSources;++key) {
        LumaLight entry=light;entry.key=key;crowded.push_back(entry);
    }
    (void)sources.publish(std::move(crowded));
    AIVERSE_TEST_CHECK(direct.setSources(sources));
    AIVERSE_TEST_CHECK(direct.sourceCount()==LumaWeaveRegistry::kMaxSources);
    std::vector<LumaGpuReceiver> many(256,receivers[0]);
    device.beginFrame(64,64);device.current=10;
    const auto skipped=direct.dispatch(many,8,9);
    AIVERSE_TEST_CHECK(!skipped.encoded && device.dispatchCount==1);
    device.endFrame();

    // Input validation must not encode invalid receivers.
    std::array<LumaGpuReceiver,1> invalid{};
    invalid[0].worldPosition={0,0,0,1};
    invalid[0].normal={0,0,0,0};
    device.beginFrame(64,64);device.current=11;
    AIVERSE_TEST_CHECK(!direct.dispatch(invalid,1,1).encoded);
    device.endFrame();

    // Stage15 real-GBuffer compute ABI: all five existing per-scene input
    // textures are required; GPU source/geometry/normal data are not faked.
    AIVERSE_TEST_CHECK(direct.screenAvailable());
    gfx::TextureDesc sampled;
    sampled.width=64;sampled.height=64;sampled.format=gfx::PixelFormat::RGBA16F;
    sampled.filter=gfx::Filter::Nearest;
    std::array<gfx::TextureHandle,5> inputs{};
    for(auto& handle:inputs)handle=device.createTexture(sampled);
    LumaWeaveVulkanDirect::ScreenInputs surfaces{
        inputs[0],inputs[1],inputs[2],inputs[3],inputs[4]};
    LumaWeaveVulkanDirect::ScreenView view{};
    view.eye={0,2,-5,0};
    view.resolution={8,8,64,64};
    view.options={.008f,0,0,0};
    AIVERSE_TEST_CHECK(!direct.dispatchScreen({},view,8,8,1,12).encoded);
    view.resolution.x=64;view.resolution.y=64;
    // 4096 sources * 9 samples * 4096 pixels is over the 24M cap.
    AIVERSE_TEST_CHECK(!direct.dispatchScreen(surfaces,view,64,64,8,12).encoded);
    view.resolution.x=8;view.resolution.y=8;
    device.current=12;
    device.beginFrame(64,64);
    const auto screen=direct.dispatchScreen(surfaces,view,8,8,1,12);
    AIVERSE_TEST_CHECK(screen.encoded && screen.frameSerial==12);
    AIVERSE_TEST_CHECK(screen.candidateChecks==uint64_t(64)*2*4096);
    AIVERSE_TEST_CHECK(direct.screenWidth()==8 && direct.screenHeight()==8);
    AIVERSE_TEST_CHECK(direct.encodedOutput(12).valid());
    AIVERSE_TEST_CHECK(!direct.completedOutput().valid());
    device.endFrame();
    AIVERSE_TEST_CHECK(!direct.poll());
    device.completed=12;
    AIVERSE_TEST_CHECK(direct.poll());
    AIVERSE_TEST_CHECK(direct.completedOutput().valid());
    AIVERSE_TEST_CHECK(direct.completedReceiverCount()==64);

    // Stage17: the first denoised frame has only spatial samples, despite
    // a matching view key: no previous Vulkan fence has published history.
    AIVERSE_TEST_CHECK(direct.denoiseAvailable());
    LumaWeaveVulkanDirect::DenoiseRequest history{};
    history.viewKey=77;history.viewEpoch=3;
    history.currentVP=mat4{};history.eye={0,2,-5};
    device.current=13;
    device.beginFrame(64,64);
    auto filtered=direct.dispatchScreen(surfaces,view,8,8,1,3,&history);
    AIVERSE_TEST_CHECK(filtered.encoded && filtered.frameSerial==13);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().encoded);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().normalCaptured);
    AIVERSE_TEST_CHECK(!direct.denoiseStatus().historyUsed);
    AIVERSE_TEST_CHECK(!direct.denoiseStatus().motionBound);
    AIVERSE_TEST_CHECK(!direct.historyValid());
    AIVERSE_TEST_CHECK(!direct.shutdown()); // three output images are in flight
    device.endFrame();
    AIVERSE_TEST_CHECK(!direct.poll());
    device.completed=13;
    AIVERSE_TEST_CHECK(direct.poll() && direct.historyValid());

    // Identical world+view and unchanged source/BVH after a REAL fence:
    // admit a reprojected history sample; no new light map is invented.
    device.current=14;
    history.motionVectors=inputs[0]; // FakeDevice's current borrowed RGBA16F
    history.motionFrameSerial=14;
    history.motionViewKey=77;history.motionViewEpoch=3;
    history.requireValidMotion=true;
    device.beginFrame(64,64);
    filtered=direct.dispatchScreen(surfaces,view,8,8,1,4,&history);
    AIVERSE_TEST_CHECK(filtered.encoded);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().encoded);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().historyUsed);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().motionBound);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().normalCaptured);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().reason==
                       LumaVulkanHistoryReason::Accepted);
    device.endFrame();
    device.completed=14;
    AIVERSE_TEST_CHECK(direct.poll() && direct.historyValid());

    // A camera teleport / cut cannot inherit a bright previous viewport.
    history.eye.x+=5.0f;
    history.motionFrameSerial=14; // stale source frame is rejected
    device.current=15;
    device.beginFrame(64,64);
    filtered=direct.dispatchScreen(surfaces,view,8,8,1,5,&history);
    AIVERSE_TEST_CHECK(filtered.encoded &&
                       direct.denoiseStatus().encoded);
    AIVERSE_TEST_CHECK(!direct.denoiseStatus().historyUsed);
    AIVERSE_TEST_CHECK(!direct.denoiseStatus().motionBound);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().normalCaptured);
    AIVERSE_TEST_CHECK(direct.denoiseStatus().reason==
                       LumaVulkanHistoryReason::CameraCut);
    device.endFrame();
    device.completed=15;
    AIVERSE_TEST_CHECK(direct.poll() && direct.historyValid());

    // Old history must be invalidated immediately when the light registry
    // and immutable BVH generation change, independently of camera motion.
    auto edited=sources.lights();
    edited[0].color={0,0,1};
    (void)sources.publish(std::move(edited));
    AIVERSE_TEST_CHECK(direct.setSources(sources));
    AIVERSE_TEST_CHECK(!direct.historyValid());
    AIVERSE_TEST_CHECK(direct.setGeometry(good,3));
    AIVERSE_TEST_CHECK(!direct.historyValid());

    // A changed render resolution starts spatial-only, with new history
    // textures; no recycled 8x8 image is sampled as a 16x16 image.
    history.eye.x=0;
    view.resolution.x=16;view.resolution.y=16;
    device.current=16;
    device.beginFrame(64,64);
    filtered=direct.dispatchScreen(surfaces,view,16,16,1,6,&history);
    AIVERSE_TEST_CHECK(filtered.encoded &&
                       direct.denoiseStatus().encoded);
    AIVERSE_TEST_CHECK(!direct.denoiseStatus().historyUsed);
    AIVERSE_TEST_CHECK(!direct.shutdown());
    device.endFrame();
    device.completed=16;
    AIVERSE_TEST_CHECK(direct.poll() && direct.historyValid());

    // A history reset DURING an in-flight compute must not be undone by
    // a later successful callback for that old command buffer.
    device.current=17;
    device.beginFrame(64,64);
    const auto pending=direct.dispatchScreen(surfaces,view,16,16,1,7,&history);
    AIVERSE_TEST_CHECK(pending.encoded &&
                       direct.denoiseStatus().historyUsed);
    direct.invalidateHistory();
    device.endFrame();
    device.completed=17;
    AIVERSE_TEST_CHECK(direct.poll());
    AIVERSE_TEST_CHECK(!direct.historyValid());
    for(auto t:inputs)device.destroyTexture(t);

    AIVERSE_TEST_CHECK(direct.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
    AIVERSE_TEST_CHECK(!direct.ready());
    VulkanComputeFake unavailable;
    unavailable.computeEnabled=false;
    AIVERSE_TEST_CHECK(!direct.initialize(unavailable));
}
