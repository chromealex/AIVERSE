// Stage20: PRISMA Renderer3D's PostProcess object-motion producer calls
// exactly these typed hooks AFTER the scene and BEFORE resolve. The test
// simulates that contract using a real signed RGBA16F FakeDevice copy.
// This is NOT an actual GPU skinning, shader or Vulkan pixel PASS.
#include "engine/render/LumaWeaveVulkanPostProcess.h"
#include "engine/render/LumaWeaveNativeDriver.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <utility>

using namespace aiverse;
using namespace aiverse::render;
struct Device final : FakeDevice {
    uint64_t serial=70,completed=0;
    unsigned copies=0;
    gfx::TextureHandle lastCopy{};
    Device(){info_.backend=gfx::Backend::Vulkan;computeEnabled=true;}
    bool supports(gfx::PixelFormat f)const override{
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    uint64_t frameSerial()const override{return serial;}
    uint64_t completedFrameSerial()const override{return completed;}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc& d)override{
        AIVERSE_TEST_CHECK(d.glslVertex && d.glslFragment &&
                           d.textureSlots==4 && d.linearTarget);
        return {next++};
    }
    bool copyColor(gfx::TextureHandle from,gfx::TextureHandle to)override{
        const auto src=textures.find(from.id);
        const auto dst=textures.find(to.id);
        if(src==textures.end() || dst==textures.end() || from.id==to.id ||
           src->second.format!=gfx::PixelFormat::RGBA16F ||
           dst->second.format!=gfx::PixelFormat::RGBA16F ||
           src->second.w!=dst->second.w ||
           src->second.h!=dst->second.h)return false;
        dst->second.data=src->second.data;
        lastCopy=to;++copies;
        return true;
    }
};

struct HDRPost final : PostProcess {
    gfx::Device& dev;
    gfx::TextureHandle color{},depth{};
    explicit HDRPost(gfx::Device& d):dev(d) {
        gfx::TextureDesc td;
        td.width=td.height=64;td.renderTarget=true;
        td.format=gfx::PixelFormat::RGBA16F;color=dev.createTexture(td);
        td.format=gfx::PixelFormat::Depth;depth=dev.createTexture(td);
    }
    ~HDRPost()override{
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

static void publishReceivers(LumaWeaveVulkanPostProcess& post) {
    AIVERSE_TEST_CHECK(post.wantsMaterialReceiverPass());
    const auto material=post.materialReceiverPass(64,64);
    AIVERSE_TEST_CHECK(material.colorTarget.valid() &&
                       material.depthTarget.valid());
    post.materialReceiverPublished(material.colorTarget,64,64);
    AIVERSE_TEST_CHECK(post.wantsSurfaceReceiverPass());
    const auto normal=post.surfaceReceiverPass(64,64);
    AIVERSE_TEST_CHECK(normal.colorTarget.valid() &&
                       normal.depthTarget.valid());
    post.surfaceReceiverPublished(normal.colorTarget,64,64);
}

int main() {
    Device dev;
    LumaWeaveVulkanDirect owner;
    AIVERSE_TEST_CHECK(owner.initialize(dev));
    LumaWeaveRegistry registry;
    LumaLight light;light.key=1;light.radius=4;light.position={0,2,0};
    (void)registry.publish({light});
    AIVERSE_TEST_CHECK(owner.setSources(registry));
    GiGpuBvh bvh;
    AIVERSE_TEST_CHECK(owner.setGeometry(bvh,1));
    LightSystem legacy;
    {
        HDRPost downstream(dev);
        LumaWeaveVulkanPostProcess post(downstream,legacy,owner);
        AIVERSE_TEST_CHECK(post.initialize(dev));
        post.setEnabled(true);
        post.setHistoryIdentity(700,3);
        post.setObjectMotionVectors({},0,700,3,true);

        gfx::TextureDesc td;
        td.width=td.height=64;
        td.format=gfx::PixelFormat::RGBA16F;
        td.renderTarget=true;
        const auto velocity=dev.createTexture(td);
        const auto secondVelocity=dev.createTexture(td);
        dev.textures.at(velocity.id).data[0]={-.25f,.1f,1.f,0};
        dev.textures.at(secondVelocity.id).data[0]={.5f,-.2f,1.f,0};

        gfx::TextureHandle firstSnapshot{};
        PostFrame frame{};
        frame.width=frame.height=64;
        frame.cameraPos={0,2,-5};

        // First view: producer has no previous camera. It seeds pose
        // history but can still provide a correctly metadata-tagged image.
        dev.beginFrame(64,64);
        (void)post.scenePass({},64,64);
        publishReceivers(post);
        PostProcess& producerContract=post;
        AIVERSE_TEST_CHECK(producerContract.wantsObjectMotionVectors());
        AIVERSE_TEST_CHECK(producerContract.objectMotionSceneDepth()==
                           downstream.depth);
        uint64_t viewId=0;mat4 oldCamera;
        AIVERSE_TEST_CHECK(!producerContract.objectMotionContext(
            viewId,oldCamera));
        AIVERSE_TEST_CHECK(viewId==700);
        AIVERSE_TEST_CHECK(producerContract.objectMotionEpoch(700)==3);
        producerContract.acceptObjectMotionVectors(
            MotionTextureInput::declared(velocity,64,64,
                                         gfx::PixelFormat::RGBA16F));
        AIVERSE_TEST_CHECK(dev.copies==1);
        firstSnapshot=dev.lastCopy;
        AIVERSE_TEST_CHECK(firstSnapshot.valid() &&
                           firstSnapshot.id!=velocity.id);
        const auto saved=dev.textures.at(firstSnapshot.id).data[0];
        AIVERSE_TEST_CHECK(saved.x==-.25f && saved.y==.1f && saved.z==1);
        post.resolve(dev,{},frame);
        AIVERSE_TEST_CHECK(post.stats().composited);
        AIVERSE_TEST_CHECK(post.stats().motionInputBound);
        AIVERSE_TEST_CHECK(post.stats().motionAutoCaptured);
        AIVERSE_TEST_CHECK(post.stats().motionSnapshotCopied);
        dev.endFrame();
        AIVERSE_TEST_CHECK(!post.shutdown()); // snapshot is GPU-borrowed
        dev.completed=70;
        AIVERSE_TEST_CHECK(owner.poll() && owner.historyValid());

        // Second view: PRISMA producer gets exact previous view matrix
        // and its new velocity snapshot cannot clobber frame 70.
        dev.serial=71;post.setObjectMotionVectors({},0,700,3,true);
        dev.beginFrame(64,64);
        (void)post.scenePass({},64,64);
        publishReceivers(post);
        AIVERSE_TEST_CHECK(producerContract.wantsObjectMotionVectors());
        viewId=0;
        AIVERSE_TEST_CHECK(producerContract.objectMotionContext(
            viewId,oldCamera));
        AIVERSE_TEST_CHECK(viewId==700);
        producerContract.acceptObjectMotionVectors(
            MotionTextureInput::declared(secondVelocity,64,64,
                                         gfx::PixelFormat::RGBA16F));
        AIVERSE_TEST_CHECK(dev.copies==2 &&
                           firstSnapshot.id!=dev.lastCopy.id);
        AIVERSE_TEST_CHECK(
            dev.textures.at(dev.lastCopy.id).data[0].x==.5f);
        post.resolve(dev,{},frame);
        AIVERSE_TEST_CHECK(post.stats().composited);
        AIVERSE_TEST_CHECK(post.stats().temporalHistoryUsed);
        dev.endFrame();
        dev.completed=71;
        AIVERSE_TEST_CHECK(owner.poll());

        // Camera cut: the prior main-renderer pose matrix is rejected
        // even when the view ID and source texture pointer are unchanged.
        dev.serial=72;
        post.setHistoryIdentity(700,4);
        post.setObjectMotionVectors({},0,700,4,true);
        dev.beginFrame(64,64);
        (void)post.scenePass({},64,64);
        publishReceivers(post);
        viewId=0;
        AIVERSE_TEST_CHECK(!producerContract.objectMotionContext(
            viewId,oldCamera));
        // A metadata-less legacy handle is NOT an admissible source.
        producerContract.acceptObjectMotionVectors(velocity);
        AIVERSE_TEST_CHECK(dev.copies==2);
        post.resolve(dev,{},frame);
        AIVERSE_TEST_CHECK(post.stats().composited &&
                           !post.stats().motionSnapshotCopied);
        AIVERSE_TEST_CHECK(!post.stats().motionInputBound);
        dev.endFrame();
        dev.completed=72;
        AIVERSE_TEST_CHECK(owner.poll());
        post.setEnabled(false); // real route disable clears camera history
        AIVERSE_TEST_CHECK(post.shutdown());
        dev.destroyTexture(velocity);
        dev.destroyTexture(secondVelocity);
    }
    AIVERSE_TEST_CHECK(owner.shutdown());
    AIVERSE_TEST_CHECK(dev.buffers.empty() && dev.textures.empty());
    return 0;
}
