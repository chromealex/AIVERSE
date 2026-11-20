// Source/FakeDevice regression for native Renderer3D/PostProcess Vulkan HDR
// integration. It verifies *real API/texture flow and GPU serial gating*,
// NOT actual Vulkan pixels or shader execution.
#include "engine/render/LumaWeaveVulkanPostProcess.h"
#include "engine/render/LumaWeaveNativeDriver.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <type_traits>

using namespace aiverse::render;
static_assert(int(LumaDirectRoute::Disabled)==0);
static_assert(int(LumaDirectRoute::MetalGpu)==1);
static_assert(int(LumaDirectRoute::MobileRasterFallback)==2);
static_assert(int(LumaDirectRoute::VulkanGpu)==3);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.enableVulkanDirect),bool>);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.vulkanReceiverCoverageConfirmed),bool>);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.vulkanSubmission),
                             LumaWeaveVulkanDirect::Submission>);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.vulkanHistoryKey),
                             uint64_t>);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.vulkanTemporalHistoryUsed),
                             bool>);

struct ShaderFake final:FakeDevice {
    uint64_t current=21,completed=0;
    int shaders=0,draws=0,passes=0;
    std::array<gfx::TextureHandle,4> sampled{};
    ShaderFake(){info_.backend=gfx::Backend::Vulkan;computeEnabled=true;}
    bool supports(gfx::PixelFormat format) const override {
        return format==gfx::PixelFormat::RGBA16F || FakeDevice::supports(format);
    }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc& d) override {
        AIVERSE_TEST_CHECK(d.textureSlots==4 && d.uniformSlots==1 &&
                           d.linearTarget && d.glslVertex && d.glslFragment);
        ++shaders;return {next++};
    }
    void setTexture(int slot,gfx::TextureHandle texture) override {
        AIVERSE_TEST_CHECK(slot>=0 && slot<4);
        sampled[size_t(slot)]=texture;
    }
    void draw(uint32_t,uint32_t)override{++draws;}
    void beginPass(const gfx::PassDesc&)override{++passes;}
    uint64_t frameSerial()const override{return current;}
    uint64_t completedFrameSerial()const override{return completed;}
};

struct LinearPost final:PostProcess {
    gfx::Device& device;
    gfx::TextureHandle color{},depth{};
    bool resolved=false;
    explicit LinearPost(gfx::Device& d):device(d) {
        gfx::TextureDesc desc;desc.width=64;desc.height=64;
        desc.renderTarget=true;desc.format=gfx::PixelFormat::RGBA16F;
        color=device.createTexture(desc);
        desc.format=gfx::PixelFormat::Depth;
        depth=device.createTexture(desc);
    }
    ~LinearPost()override{
        if(color.valid())device.destroyTexture(color);
        if(depth.valid())device.destroyTexture(depth);
    }
    bool active()const override{return true;}
    bool linearScene()const override{return true;}
    gfx::PassDesc scenePass(const gfx::PassDesc&,int w,int h) override {
        AIVERSE_TEST_CHECK(w==64 && h==64);
        gfx::PassDesc p;p.colorTarget=color;p.depthTarget=depth;return p;
    }
    void resolve(gfx::Device&,const gfx::PassDesc&,const PostFrame&)override{
        resolved=true;
    }
};

int main() {
    ShaderFake device;
    LumaWeaveVulkanDirect gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(device));
    LumaWeaveRegistry registry;
    LumaLight light;light.key=123;
    light.position={0,2,0};light.radius=4;
    (void)registry.publish({light});
    AIVERSE_TEST_CHECK(gpu.setSources(registry));
    GiGpuBvh empty;
    AIVERSE_TEST_CHECK(gpu.setGeometry(empty,1));
    LightSystem legacy;
    {
        LinearPost downstream(device);
        LumaWeaveVulkanPostProcess post(downstream,legacy,gpu);
        AIVERSE_TEST_CHECK(post.initialize(device));
        post.setEnabled(true);
        post.setHistoryIdentity(501,3);
        post.setQuality(LumaWeaveQuality::Balanced);
        device.beginFrame(64,64);
        const auto scene=post.scenePass({},64,64);
        AIVERSE_TEST_CHECK(scene.colorTarget==downstream.color);
        AIVERSE_TEST_CHECK(post.wantsMaterialReceiverPass());
        auto material=post.materialReceiverPass(64,64);
        AIVERSE_TEST_CHECK(material.colorTarget.valid() &&
                           material.depthTarget.valid());
        post.materialReceiverPublished(material.colorTarget,64,64);
        AIVERSE_TEST_CHECK(post.wantsSurfaceReceiverPass());
        auto surface=post.surfaceReceiverPass(64,64);
        AIVERSE_TEST_CHECK(surface.colorTarget.valid() &&
                           surface.depthTarget.valid());
        post.surfaceReceiverPublished(surface.colorTarget,64,64);
        PostFrame frame{};
        frame.width=64;frame.height=64;
        frame.cameraPos={0,2,-5};
        post.resolve(device,{},frame);
        const auto& stats=post.stats();
        AIVERSE_TEST_CHECK(stats.eligible && stats.composited);
        AIVERSE_TEST_CHECK(stats.denoiseEncoded && !stats.temporalHistoryUsed);
        AIVERSE_TEST_CHECK(gpu.denoiseStatus().encoded);
        AIVERSE_TEST_CHECK(!gpu.historyValid()); // fence not completed yet
        AIVERSE_TEST_CHECK(stats.materialAvailable &&
                           stats.surfaceAvailable);
        AIVERSE_TEST_CHECK(stats.submission.encoded &&
                           stats.submission.frameSerial==21);
        AIVERSE_TEST_CHECK(gpu.busy() && !gpu.completedOutput().valid());
        AIVERSE_TEST_CHECK(device.sampled[0].valid() &&
                           device.sampled[1]==downstream.depth &&
                           device.sampled[2]==material.depthTarget &&
                           device.sampled[3]==surface.depthTarget);
        AIVERSE_TEST_CHECK(device.draws==1 && downstream.resolved);
        AIVERSE_TEST_CHECK(!post.shutdown()); // images still GPU-borrowed
        // A temporary raster frame while GPU serial 21 is in flight must
        // not prevent the eventual physical fence from publishing a valid
        // history candidate for the same stable camera/world identity.
        post.setEnabled(false,true);
        device.endFrame();
        AIVERSE_TEST_CHECK(!gpu.poll());
        device.completed=21;
        AIVERSE_TEST_CHECK(gpu.poll() && gpu.historyValid());
        post.setEnabled(false); // actual route disable drops old history
        AIVERSE_TEST_CHECK(!gpu.historyValid());
        AIVERSE_TEST_CHECK(post.shutdown());
        AIVERSE_TEST_CHECK(!post.ready());
    }
    AIVERSE_TEST_CHECK(gpu.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
