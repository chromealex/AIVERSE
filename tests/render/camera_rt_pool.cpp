// CPU-only gfx::Device recording stub: RT allocation, resize, HDR and
// completion-token deferred destruction (no Renderer3D/GPU needed).
#include "engine/render/RenderTargetPool.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aiverse;
static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
struct FakeDevice final : gfx::Device {
    gfx::DeviceInfo description{};
    std::string error;
    uint32_t handle=1;
    uint64_t serial=9;
    bool completed=false,idleAllowed=true;
    bool hdrAvailable=true, depthFail=false;
    std::vector<gfx::TextureHandle> released;
    std::vector<gfx::TextureDesc> requested;
    const gfx::DeviceInfo& info()const override{return description;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t)override{}
    void destroyBuffer(gfx::BufferHandle)override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d)override {
        requested.push_back(d);
        if(depthFail&&d.format==gfx::PixelFormat::Depth)return {};
        return {handle++};
    }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int)override{}
    void destroyTexture(gfx::TextureHandle t)override{released.push_back(t);}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&)override{return {};}
    void destroyShader(gfx::ShaderHandle)override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&)override{return {};}
    void destroyPipeline(gfx::PipelineHandle)override{}
    const std::string& lastError()const override{return error;}
    bool supports(gfx::PixelFormat f)const override {
        return f!=gfx::PixelFormat::RGBA16F||hdrAvailable;
    }
    uint64_t frameSerial() const override{return serial;}
    uint64_t beginResourceRetirement() override{return 1;}
    bool resourceRetirementComplete(uint64_t) override{return completed;}
    void endResourceRetirement(uint64_t) override{}
    bool waitForResourceIdle() override{return idleAllowed;}
    void beginFrame(int,int)override{}
    void endFrame()override{}
    void beginPass(const gfx::PassDesc&)override{}
    void endPass()override{}
    void setViewport(const gfx::Rect&)override{}
    void setScissor(const gfx::Rect&)override{}
    void setPipeline(gfx::PipelineHandle)override{}
    void setVertexBuffer(gfx::BufferHandle,size_t)override{}
    void setIndexBuffer(gfx::BufferHandle,size_t)override{}
    void setTexture(int,gfx::TextureHandle)override{}
    void setUniforms(int,const void*,size_t)override{}
    void draw(uint32_t,uint32_t)override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t)override{}
};
int main() {
    FakeDevice dev;
    render::CameraRenderTargetPool pool;
    check(pool.init(dev),"init");
    auto* first=pool.acquire(123,128,64);
    check(first&&first->color.valid()&&first->depth.valid()&&
          pool.activeTargets()==1,"color and depth allocated");
    const auto oldColor=first->color;
    check(pool.acquire(123,128,64)->color==oldColor,"identical RT reuse");
    pool.markReady(123);
    check(pool.color(123)==oldColor,"ready image is sampleable");
    dev.hdrAvailable=false;
    check(!pool.acquire(123,128,64,true),"unsupported HDR rejected");
    check(pool.color(123)==oldColor,"failed change preserves valid RT");
    dev.hdrAvailable=true;
    auto* changed=pool.acquire(123,256,128,true,false);
    check(changed&&changed->hdr&&!changed->ready&&changed->depth.valid(),
          "resize/format allocates fresh color and internal depth");
    check(!pool.depth(123).valid(),"unexposed depth cannot be sampled");
    check(pool.retiredTargets()==1 && dev.released.empty(),
          "old RT must wait for completed frame");
    pool.collectCompleted();
    check(dev.released.empty(),"not yet completed must not free textures");
    dev.completed=true;
    pool.collectCompleted();
    check(dev.released.size()==2&&pool.retiredTargets()==0,
          "completed token frees retired color+depth");
    dev.depthFail=true;
    auto* failure=pool.acquire(123,300,200);
    check(!failure && pool.find(123)->width==256,
          "failed depth allocation preserves previous RT");
    check(dev.released.size()==3,"failed new color released immediately");
    dev.depthFail=false;
    pool.markReady(123);
    check(pool.color(123).valid(),"new target becomes sampleable after capture");
    pool.release(123);
    check(pool.activeTargets()==0&&pool.retiredTargets()==1,
          "RT retire on release");
    dev.idleAllowed=false;
    bool rejected=false;
    try {pool.shutdownAfterGpuIdle();}catch(const std::runtime_error&){rejected=true;}
    check(rejected && pool.retiredTargets()==1 && dev.released.size()==3,
          "failed GPU idle retains targets for retry");
    dev.idleAllowed=true;
    pool.shutdownAfterGpuIdle();
    check(dev.released.size()==5,"gpu-idle shutdown releases remaining resources");
    // createTexture failure is not retried every frame for the same spec.
    check(pool.init(dev),"re-init after shutdown");
    dev.depthFail=true;
    const size_t before=dev.requested.size();
    check(!pool.acquire(7,64,64),"depth failure");
    const size_t afterFirst=dev.requested.size();
    check(afterFirst==before+2 && pool.failed(7),"failed spec recorded");
    check(!pool.acquire(7,64,64) && dev.requested.size()==afterFirst,
          "same failed spec does not call createTexture again");
    dev.depthFail=false;
    check(!pool.acquire(7,64,64),"still backed off until retry/spec change");
    check(pool.acquire(7,64,32) && !pool.failed(7),"changed spec retries");
    dev.depthFail=true;
    check(!pool.acquire(7,32,32),"fail again");
    dev.depthFail=false;
    pool.retryFailedTargets();
    check(pool.acquire(7,32,32),"explicit retry allocates");
    // Total pixel budget across keys.
    pool.setPixelBudget(64*64);
    check(pool.livePixels()==32*32,"live pixels counted");
    check(!pool.acquire(8,64,64),"over budget rejected");
    check(pool.acquire(7,64,64),"replacing own target counts only the new size");
    check(!pool.acquire(8,1,1),"budget full");
    pool.setPixelBudget(render::CameraRenderTargetPool::kDefaultPixelBudget);
    check(pool.acquire(8,8192,1024),"default budget allows large target");
    // Stale keys are retired (fenced), kept keys stay.
    check(pool.releaseExcept({8})==1 && pool.activeTargets()==1 && pool.find(8),
          "releaseExcept retires stale keys only");
    pool.shutdownAfterGpuIdle();
    std::cout<<"AIVERSE LENS RT pool CPU contract passed\\n";
}
