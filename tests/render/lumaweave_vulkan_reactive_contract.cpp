// Stage21 PRISMA reactive-only path through Vulkan LumaWeave PostProcess.
// FakeDevice verifies typed current-frame RGBA8 silhouette copying, fail-
// closed missing-mask behavior, and actual fence-based image retirement.
// NOT a real shader or physical Vulkan test.
#include "engine/render/LumaWeaveVulkanPostProcess.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>

using namespace aiverse;
using namespace aiverse::render;
static_assert(sizeof(LumaWeaveVulkanDirect::DenoiseView)==208);

struct MockDevice final:FakeDevice {
    uint64_t serial=33,completed=0;
    unsigned reactiveCopies=0;
    gfx::TextureHandle captured{};
    MockDevice(){info_.backend=gfx::Backend::Vulkan;computeEnabled=true;}
    uint64_t frameSerial()const override{return serial;}
    uint64_t completedFrameSerial()const override{return completed;}
    bool supports(gfx::PixelFormat f)const override{
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc& d)override{
        AIVERSE_TEST_CHECK(d.glslVertex && d.glslFragment &&
                           d.textureSlots==4 && d.linearTarget);
        return {next++};
    }
    void setTexture(int slot,gfx::TextureHandle)override{
        AIVERSE_TEST_CHECK(slot>=0 && slot<4);
    }
    bool copyColor(gfx::TextureHandle source,gfx::TextureHandle destination)override{
        const auto a=textures.find(source.id),b=textures.find(destination.id);
        if(a==textures.end() || b==textures.end() ||
           source.id==destination.id ||
           a->second.format!=gfx::PixelFormat::RGBA8 ||
           b->second.format!=gfx::PixelFormat::RGBA8 ||
           a->second.w!=b->second.w || a->second.h!=b->second.h)return false;
        b->second.data=a->second.data;
        captured=destination;++reactiveCopies;
        return true;
    }
};
struct LinearHdr final:PostProcess {
    gfx::Device& dev;
    gfx::TextureHandle color{},depth{};
    explicit LinearHdr(gfx::Device& device):dev(device){
        gfx::TextureDesc td;td.width=td.height=64;
        td.format=gfx::PixelFormat::RGBA16F;td.renderTarget=true;
        color=dev.createTexture(td);
        td.format=gfx::PixelFormat::Depth;depth=dev.createTexture(td);
    }
    ~LinearHdr()override{
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
static void publishReceivers(LumaWeaveVulkanPostProcess& p){
    AIVERSE_TEST_CHECK(p.wantsMaterialReceiverPass());
    auto mat=p.materialReceiverPass(64,64);
    AIVERSE_TEST_CHECK(mat.colorTarget.valid() && mat.depthTarget.valid());
    p.materialReceiverPublished(mat.colorTarget,64,64);
    AIVERSE_TEST_CHECK(p.wantsSurfaceReceiverPass());
    auto surf=p.surfaceReceiverPass(64,64);
    AIVERSE_TEST_CHECK(surf.colorTarget.valid() && surf.depthTarget.valid());
    p.surfaceReceiverPublished(surf.colorTarget,64,64);
}
int main(){
    MockDevice dev;
    LumaWeaveVulkanDirect owner;
    AIVERSE_TEST_CHECK(owner.initialize(dev));
    LumaWeaveRegistry registry;
    LumaLight source;source.key=17;source.radius=4;
    source.position={0,2,0};
    (void)registry.publish({source});
    GiGpuBvh empty;
    AIVERSE_TEST_CHECK(owner.setSources(registry));
    AIVERSE_TEST_CHECK(owner.setGeometry(empty,1));
    LightSystem legacy;
    {
        LinearHdr hdr(dev);
        LumaWeaveVulkanPostProcess p(hdr,legacy,owner);
        AIVERSE_TEST_CHECK(p.initialize(dev));
        p.setEnabled(true);p.setHistoryIdentity(900,7);
        p.setObjectMotionVectors({},0,900,7,false); // no moving rigid meshes
        p.setReactiveGeometryPresent(true); // only alpha-cutout water/dissolve
        gfx::TextureDesc td;td.width=td.height=64;
        td.format=gfx::PixelFormat::RGBA8;td.renderTarget=true;
        const auto mask=dev.createTexture(td);
        dev.textures.at(mask.id).data[0]={1,0,0,1};
        PostFrame frame{};frame.width=frame.height=64;
        frame.cameraPos={0,2,-5};
        dev.beginFrame(64,64);
        (void)p.scenePass({},64,64);
        publishReceivers(p);
        PostProcess& mainProducer=p;
        AIVERSE_TEST_CHECK(mainProducer.wantsObjectMotionVectors());
        AIVERSE_TEST_CHECK(mainProducer.objectMotionSceneDepth()==hdr.depth);
        mainProducer.acceptMotionReactiveMask(MotionTextureInput::declared(
            mask,64,64,gfx::PixelFormat::RGBA8));
        AIVERSE_TEST_CHECK(dev.reactiveCopies==1 && dev.captured.valid() &&
                           dev.captured.id!=mask.id);
        AIVERSE_TEST_CHECK(dev.textures.at(dev.captured.id).data[0].x==1);
        p.resolve(dev,{},frame);
        AIVERSE_TEST_CHECK(p.stats().composited);
        AIVERSE_TEST_CHECK(p.stats().reactiveAutoCaptured);
        AIVERSE_TEST_CHECK(p.stats().reactiveSnapshotCopied);
        AIVERSE_TEST_CHECK(p.stats().reactiveMaskBound);
        AIVERSE_TEST_CHECK(!p.stats().motionInputBound);
        dev.endFrame();
        AIVERSE_TEST_CHECK(!p.shutdown()); // GPU may still read current mask
        AIVERSE_TEST_CHECK(!owner.poll());
        dev.completed=33;
        AIVERSE_TEST_CHECK(owner.poll());
        dev.destroyTexture(mask); // producer may now safely release source

        dev.serial=34;p.setEnabled(true);p.setHistoryIdentity(900,7);
        p.setObjectMotionVectors({},0,900,7,false);
        p.setReactiveGeometryPresent(true);
        dev.beginFrame(64,64);
        (void)p.scenePass({},64,64);
        publishReceivers(p);
        mainProducer.acceptMotionReactiveMask(MotionTextureInput::legacy({77}));
        p.resolve(dev,{},frame);
        AIVERSE_TEST_CHECK(p.stats().composited);
        AIVERSE_TEST_CHECK(!p.stats().reactiveAutoCaptured);
        AIVERSE_TEST_CHECK(!p.stats().reactiveMaskBound);
        AIVERSE_TEST_CHECK(dev.reactiveCopies==1);
        dev.endFrame();dev.completed=34;
        AIVERSE_TEST_CHECK(owner.poll());
        p.setEnabled(false);
        AIVERSE_TEST_CHECK(p.shutdown());
    }
    AIVERSE_TEST_CHECK(owner.shutdown());
    AIVERSE_TEST_CHECK(dev.buffers.empty() && dev.textures.empty());
    return 0;
}
