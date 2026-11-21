// Stage 107 deferred CPU regression. A minimal gfx Device proves scoped
// resource registrations reject the wrong device and calling thread.
// No actual GPU work is submitted; real backend verification remains open.
#include "engine/render/RenderResourceOwnerGroup.h"
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

using namespace aiverse;
class DeviceStub final: public gfx::Device {
public:
    gfx::DeviceInfo details{};
    std::string error;
    const gfx::DeviceInfo& info() const override{return details;}
    const std::string& lastError() const override{return error;}
    bool waitForResourceIdle() override{return true;} // CPU stub: no GPU work
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle) override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
    void destroyPipeline(gfx::PipelineHandle) override{}
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
int main() {
    static_assert(!std::is_copy_constructible_v<
        render::RenderResourceOwnerGroup::ScopedRegistration>);
    static_assert(std::is_move_constructible_v<
        render::RenderResourceOwnerGroup::ScopedRegistration>);
    DeviceStub first,second;
    render::ModelResourceCache scene,portrait;
    render::RenderResourceOwnerGroup group;
    {
        auto sceneLease=group.registerScoped(first,scene);
        auto portraitLease=group.registerScoped(first,portrait);
        AIVERSE_TEST_CHECK(group.size()==2);
        AIVERSE_TEST_CHECK(scene.boundToResourceDevice(first));
        AIVERSE_TEST_CHECK(group.snapshot(first).uniqueOwners==2);
        bool rejected=false;
        try {group.registerScoped(second,scene);}catch(const std::logic_error&) {
            rejected=true;
        }
        AIVERSE_TEST_CHECK(rejected);
        rejected=false;
        try {scene.bindResourceDevice(second);}catch(const std::logic_error&) {
            rejected=true;
        }
        AIVERSE_TEST_CHECK(rejected);
        std::thread other([&] {
            bool failed=false;
            try {(void)group.snapshotOwners();}
            catch(const std::logic_error&) {failed=true;}
            AIVERSE_TEST_CHECK(failed);
        });
        other.join();
    }
    AIVERSE_TEST_CHECK(group.size()==0);
    AIVERSE_TEST_CHECK(group.snapshotOwners().uniqueOwners==0);
    auto lease=group.registerScoped(first,scene);
    AIVERSE_TEST_CHECK(lease.valid());
    lease.reset();
    AIVERSE_TEST_CHECK(!lease.valid());
    AIVERSE_TEST_CHECK(group.size()==0);
    auto active=group.registerScoped(first,scene);
    scene.shutdown(first); // proven empty stub, clears cache/device binding
    bool stale=false;
    try {(void)group.snapshot(first);}
    catch(const std::logic_error&) {stale=true;}
    AIVERSE_TEST_CHECK(stale); // live scope must not survive host device rebind
    active.reset();
    AIVERSE_TEST_CHECK(group.size()==0);

    render::RenderResourceOwnerGroup::ScopedRegistration surviving;
    {
        render::RenderResourceOwnerGroup temporary;
        surviving=temporary.registerScoped(first,scene);
        AIVERSE_TEST_CHECK(surviving.valid());
    }
    surviving.reset(); // safe after group destruction
}
