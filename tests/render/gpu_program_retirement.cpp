// Stage 115 deferred: material quality/instance-limit invalidation must
// never destroy GPU programs while their submission token is Pending/Failed.
#include "engine/render/GpuProgramRetirement.h"
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;

struct Device final : gfx::Device {
    gfx::DeviceInfo desc{};
    std::string error;
    uint32_t next=1;
    uint64_t token=11;
    bool complete=false,failed=false,idle=false;
    std::vector<char> destroyed;
    const gfx::DeviceInfo& info() const override{return desc;}
    const std::string& lastError() const override{return error;}
    bool waitForResourceIdle() override{return idle;}
    uint64_t beginResourceRetirement() override{return token;}
    bool resourceRetirementComplete(uint64_t) override{return complete;}
    gfx::ResourceRetirementStatus resourceRetirementStatus(uint64_t) override {
        return failed ? gfx::ResourceRetirementStatus::Failed:
            complete ? gfx::ResourceRetirementStatus::Complete:
                       gfx::ResourceRetirementStatus::Pending;
    }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle) override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {next++};}
    void destroyShader(gfx::ShaderHandle h) override {
        AIVERSE_TEST_CHECK(h.valid());
        destroyed.push_back('S');
    }
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {next++};}
    void destroyPipeline(gfx::PipelineHandle h) override {
        AIVERSE_TEST_CHECK(h.valid());
        destroyed.push_back('P');
    }
    void beginFrame(int,int) override{}
    void endFrame() override{}
    void beginPass(const gfx::PassDesc&) override{}
    void endPass() override{}
    void setViewport(const gfx::Rect&) override{}
    void setScissor(const gfx::Rect&) override{}
    void setPipeline(gfx::PipelineHandle) override{}
    void setVertexBuffer(gfx::BufferHandle,size_t) override{}
    void setIndexBuffer(gfx::BufferHandle,size_t) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t) override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override{}
};

static render::GpuProgramBatch makeBatch(Device& device) {
    render::GpuProgramBatch batch;
    batch.shaders.push_back(device.createShader({}));
    batch.pipelines.push_back(device.createPipeline({}));
    return batch;
}
int main() {
    Device device;
    render::RenderDeviceLifetime epoch(device);
    render::GpuProgramRetirement owner(device,epoch.lease());

    owner.retire(makeBatch(device));
    AIVERSE_TEST_CHECK(owner.pendingBatches()==1);
    AIVERSE_TEST_CHECK(device.destroyed.empty());
    AIVERSE_TEST_CHECK(!owner.collect()); // submits its own marker
    AIVERSE_TEST_CHECK(!owner.collect()); // still Pending
    AIVERSE_TEST_CHECK(device.destroyed.empty());
    device.failed=true;
    AIVERSE_TEST_CHECK(!owner.collect()); // Failed marker != completed work
    AIVERSE_TEST_CHECK(!owner.drain()); // waitForResourceIdle cannot prove completion
    AIVERSE_TEST_CHECK(device.destroyed.empty());
    device.idle=true;
    AIVERSE_TEST_CHECK(owner.drain());
    AIVERSE_TEST_CHECK(owner.pendingBatches()==0);
    AIVERSE_TEST_CHECK(device.destroyed==std::vector<char>({'P','S'}));

    // A backend without a retirement token must explicitly wait for idle.
    device.failed=false;
    device.idle=false;
    device.token=0;
    owner.retire(makeBatch(device));
    AIVERSE_TEST_CHECK(!owner.collect());
    AIVERSE_TEST_CHECK(device.destroyed.size()==2);
    device.idle=true;
    AIVERSE_TEST_CHECK(owner.collect());
    AIVERSE_TEST_CHECK(device.destroyed==std::vector<char>({'P','S','P','S'}));
    AIVERSE_TEST_CHECK(owner.enqueueFailures()==0 && owner.orphanBatches()==0);

    epoch.retire();
    bool rejected=false;
    try {
        render::GpuProgramRetirement stale(device,epoch.lease());
    }catch(const std::logic_error&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}
