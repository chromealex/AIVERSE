// Vulkan Direct Stage18: Release-safe source/receiver lifetime regression.
// This is a FakeDevice contract test, NOT a Vulkan hardware timing/pixel test.
#include "engine/render/LumaWeaveVulkanPostProcess.h"
#include "engine/render/LumaWeaveVulkanDirect.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <vector>

using namespace aiverse::render;

struct MultiFrameDevice final : FakeDevice {
    uint64_t frame=40,completed=0;
    int hdrDraws=0;
    MultiFrameDevice(){
        info_.backend=gfx::Backend::Vulkan;
        computeEnabled=true;
    }
    uint64_t frameSerial()const override{return frame;}
    uint64_t completedFrameSerial()const override{return completed;}
    bool supports(gfx::PixelFormat format)const override{
        return format==gfx::PixelFormat::RGBA16F || FakeDevice::supports(format);
    }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc& d)override{
        AIVERSE_TEST_CHECK(d.textureSlots==4 && d.uniformSlots==1 &&
                           d.glslVertex && d.glslFragment);
        return {next++};
    }
    void setTexture(int slot,gfx::TextureHandle)override{
        AIVERSE_TEST_CHECK(slot>=0 && slot<=3);
    }
    void draw(uint32_t,uint32_t)override{++hdrDraws;}
};

struct LinearHdrPost final : PostProcess {
    gfx::Device& dev;
    gfx::TextureHandle color{},depth{};
    explicit LinearHdrPost(gfx::Device& device):dev(device){
        gfx::TextureDesc d;d.width=d.height=64;d.renderTarget=true;
        d.format=gfx::PixelFormat::RGBA16F;color=dev.createTexture(d);
        d.format=gfx::PixelFormat::Depth;depth=dev.createTexture(d);
    }
    ~LinearHdrPost()override{
        if(color.valid())dev.destroyTexture(color);
        if(depth.valid())dev.destroyTexture(depth);
    }
    bool active()const override{return true;}
    bool linearScene()const override{return true;}
    gfx::PassDesc scenePass(const gfx::PassDesc&,int w,int h)override{
        AIVERSE_TEST_CHECK(w==64 && h==64);
        gfx::PassDesc p;p.colorTarget=color;p.depthTarget=depth;return p;
    }
    void resolve(gfx::Device&,const gfx::PassDesc&,const PostFrame&)override{}
};

static LumaWeaveRegistry oneLight(){
    LumaWeaveRegistry sources;
    LumaLight l;l.key=7;l.radius=4;l.position={0,2,0};
    (void)sources.publish({l});
    return sources;
}
static LumaWeaveVulkanDirect::ScreenView screenView(){
    LumaWeaveVulkanDirect::ScreenView v{};
    v.eye={0,2,-5,0};
    v.resolution={8,8,64,64};
    v.options={.008f,0,0,0};
    return v;
}
static LumaWeaveVulkanDirect::DenoiseRequest historyRequest(){
    LumaWeaveVulkanDirect::DenoiseRequest r{};
    r.viewKey=711;r.viewEpoch=2;r.eye={0,2,-5};
    return r;
}

static void checkOwnerRing() {
    MultiFrameDevice device;
    LumaWeaveVulkanDirect owner;
    AIVERSE_TEST_CHECK(owner.initialize(device));
    auto sources=oneLight();
    GiGpuBvh bvh;
    AIVERSE_TEST_CHECK(owner.setSources(sources));
    AIVERSE_TEST_CHECK(owner.setGeometry(bvh,1));

    gfx::TextureDesc d;d.width=d.height=64;
    d.format=gfx::PixelFormat::RGBA16F;d.filter=gfx::Filter::Nearest;
    std::array<gfx::TextureHandle,5> textures{};
    for(auto& t:textures)t=device.createTexture(d);
    LumaWeaveVulkanDirect::ScreenInputs input{
        textures[0],textures[1],textures[2],textures[3],textures[4]};
    const auto view=screenView(),history=historyRequest();
    std::array<gfx::TextureHandle,3> out{};
    for(uint64_t i=0;i<3;++i) {
        device.frame=40+i;device.beginFrame(64,64);
        auto submitted=owner.dispatchScreen(input,view,8,8,1,
                                            uint32_t(i),&history);
        AIVERSE_TEST_CHECK(submitted.encoded &&
                           submitted.frameSerial==device.frame);
        AIVERSE_TEST_CHECK(owner.denoiseStatus().encoded);
        AIVERSE_TEST_CHECK(owner.denoiseStatus().normalCaptured);
        AIVERSE_TEST_CHECK(!owner.denoiseStatus().historyUsed);
        out[size_t(i)]=owner.encodedOutput(device.frame);
        AIVERSE_TEST_CHECK(out[size_t(i)].valid());
        device.endFrame();
    }
    AIVERSE_TEST_CHECK(out[0]!=out[1] && out[0]!=out[2] &&
                       out[1]!=out[2]);
    AIVERSE_TEST_CHECK(owner.screenFramesInFlight()==3);
    AIVERSE_TEST_CHECK(owner.busy() && !owner.canSubmitScreen());
    AIVERSE_TEST_CHECK(!owner.shutdown());
    AIVERSE_TEST_CHECK(owner.pendingFrameSerial()==40);
    device.frame=43;device.beginFrame(64,64);
    AIVERSE_TEST_CHECK(!owner.dispatchScreen(input,view,8,8,1,3,&history).encoded);
    AIVERSE_TEST_CHECK(owner.setSources(sources)); // unchanged immutable packet
    AIVERSE_TEST_CHECK(owner.setGeometry(bvh,1));  // unchanged immutable BVH
    auto modified=sources.lights();
    modified[0].intensity+=1;
    (void)sources.publish(std::move(modified));
    AIVERSE_TEST_CHECK(!owner.setSources(sources)); // no GPU writes while busy
    AIVERSE_TEST_CHECK(!owner.setGeometry(bvh,2));  // no GPU writes while busy
    device.endFrame();

    device.completed=40;
    AIVERSE_TEST_CHECK(owner.poll());
    AIVERSE_TEST_CHECK(owner.historyValid());
    AIVERSE_TEST_CHECK(owner.screenFramesInFlight()==2);
    AIVERSE_TEST_CHECK(owner.canSubmitScreen()); // slot 3 free, slot 0 is history
    device.frame=44;device.beginFrame(64,64);
    const auto fourth=owner.dispatchScreen(input,view,8,8,1,4,&history);
    AIVERSE_TEST_CHECK(fourth.encoded && owner.denoiseStatus().historyUsed);
    // The previous frame's NORMAL/DEPTH history travels with the same
    // fenced RGB history slot, not an overwritten current-view normal.
    AIVERSE_TEST_CHECK(owner.denoiseStatus().normalCaptured);
    AIVERSE_TEST_CHECK(owner.screenFramesInFlight()==3);
    device.endFrame();

    device.completed=41;
    AIVERSE_TEST_CHECK(owner.poll());
    // The oldest history slot (frame 40) is STILL read by frame 44.
    // It must not be selected as a writable slot after frame 41 retires.
    AIVERSE_TEST_CHECK(!owner.canSubmitScreen());
    device.completed=42;
    AIVERSE_TEST_CHECK(owner.poll());
    AIVERSE_TEST_CHECK(owner.canSubmitScreen()); // frame 41 slot now safe
    device.frame=45;device.beginFrame(64,64);
    const auto fifth=owner.dispatchScreen(input,view,8,8,1,5,&history);
    AIVERSE_TEST_CHECK(fifth.encoded);
    device.endFrame();

    // A route/world history reset while multiple GPU commands remain
    // outstanding must survive every later successful completion.
    owner.invalidateHistory();
    device.completed=45;
    AIVERSE_TEST_CHECK(owner.poll());
    AIVERSE_TEST_CHECK(!owner.historyValid());
    AIVERSE_TEST_CHECK(!owner.busy());
    for(auto t:textures)device.destroyTexture(t);
    AIVERSE_TEST_CHECK(owner.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}

static void checkReceiverRing() {
    MultiFrameDevice device;device.frame=100;
    LumaWeaveVulkanDirect owner;
    AIVERSE_TEST_CHECK(owner.initialize(device));
    const auto sources=oneLight();
    GiGpuBvh bvh;
    AIVERSE_TEST_CHECK(owner.setSources(sources));
    AIVERSE_TEST_CHECK(owner.setGeometry(bvh,1));
    LightSystem legacy;
    {
        LinearHdrPost downstream(device);
        LumaWeaveVulkanPostProcess post(downstream,legacy,owner);
        AIVERSE_TEST_CHECK(post.initialize(device));
        post.setEnabled(true);
        post.setHistoryIdentity(1234,6);
        std::array<gfx::TextureHandle,3> materials{};
        for(uint64_t j=0;j<3;++j) {
            device.frame=100+j;device.beginFrame(64,64);
            const auto scene=post.scenePass({},64,64);
            AIVERSE_TEST_CHECK(scene.colorTarget==downstream.color);
            AIVERSE_TEST_CHECK(post.wantsMaterialReceiverPass());
            const auto mat=post.materialReceiverPass(64,64);
            AIVERSE_TEST_CHECK(mat.colorTarget.valid() &&
                               mat.depthTarget.valid());
            materials[size_t(j)]=mat.colorTarget;
            post.materialReceiverPublished(mat.colorTarget,64,64);
            const auto surface=post.surfaceReceiverPass(64,64);
            AIVERSE_TEST_CHECK(surface.colorTarget.valid() &&
                               surface.depthTarget.valid());
            post.surfaceReceiverPublished(surface.colorTarget,64,64);
            PostFrame frame{};
            frame.width=frame.height=64;frame.cameraPos={0,2,-5};
            post.resolve(device,{},frame);
            AIVERSE_TEST_CHECK(post.stats().composited);
            device.endFrame();
        }
        AIVERSE_TEST_CHECK(materials[0]!=materials[1] &&
                           materials[0]!=materials[2] &&
                           materials[1]!=materials[2]);
        AIVERSE_TEST_CHECK(owner.screenFramesInFlight()==3);
        AIVERSE_TEST_CHECK(!post.canAcquireFrame());
        AIVERSE_TEST_CHECK(!post.shutdown());

        device.completed=100;
        AIVERSE_TEST_CHECK(owner.poll());
        device.frame=103;device.beginFrame(64,64);
        AIVERSE_TEST_CHECK(post.canAcquireFrame());
        (void)post.scenePass({},64,64);
        const auto material=post.materialReceiverPass(64,64);
        AIVERSE_TEST_CHECK(material.colorTarget==materials[0]);
        post.materialReceiverPublished(material.colorTarget,64,64);
        const auto surface=post.surfaceReceiverPass(64,64);
        post.surfaceReceiverPublished(surface.colorTarget,64,64);
        PostFrame frame{};frame.width=frame.height=64;
        frame.cameraPos={0,2,-5};
        post.resolve(device,{},frame);
        AIVERSE_TEST_CHECK(post.stats().composited);
        device.endFrame();
        AIVERSE_TEST_CHECK(!post.shutdown());
        device.completed=103;
        AIVERSE_TEST_CHECK(owner.poll());
        AIVERSE_TEST_CHECK(post.shutdown());
    }
    AIVERSE_TEST_CHECK(owner.shutdown());
    AIVERSE_TEST_CHECK(device.buffers.empty() && device.textures.empty());
}
int main(){checkOwnerRing();checkReceiverRing();}
