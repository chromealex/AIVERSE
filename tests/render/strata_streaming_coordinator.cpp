// STRATA Stage 15.4 shared IO and cross-GeometryScene device lease contract.
// Authored now; native execution deliberately deferred to final acceptance.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataStreamingCoordinator.h"
#include "engine/render/StrataDiskPages.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class CoordinatorDevice final : public gfx::Device {
public:
    gfx::DeviceInfo description_{gfx::Backend::Vulkan,true,false,"STRATA shared budget test"};
    std::string error_;
    const gfx::DeviceInfo& info() const override {return description_;}
    const std::string& lastError() const override {return error_;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t=0) override{}
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
    void setVertexBuffer(gfx::BufferHandle,size_t=0) override{}
    void setIndexBuffer(gfx::BufferHandle,size_t=0) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t=0) override{}
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override{}
};
static ModelAsset makeAsset() {
    ModelAsset a;
    ModelPrimitiveAsset entry;
    auto& g=entry.geometry;
    g.indices={0,1,2,1,3,2,0,1,2,1,3,2};
    g.roots={0};
    g.nodes.resize(3);
    g.bounds.resize(3);
    g.nodes[0].firstChild=1;
    g.nodes[0].childCount=2;
    g.nodes[0].firstIndex=0;g.nodes[0].indexCount=6;
    g.nodes[0].geometricError=1;
    g.nodes[1].firstIndex=6;g.nodes[1].indexCount=3;
    g.nodes[2].firstIndex=9;g.nodes[2].indexCount=3;
    for(auto& node:g.nodes){node.resident=true;node.materialGroup=0;}
    for(auto& b:g.bounds)b={{-1,-1,-1},{1,1,1}};
    a.primitives.push_back(std::move(entry));
    return a;
}
template<class Pred>static bool eventually(Pred&& pred) {
    for(int n=0;n<5000;++n){
        if(pred())return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}
static void globalGpuBudget() {
    CoordinatorDevice a,b;
    assert(configureDeviceIndexBudget(a,80));
    assert(configureDeviceIndexBudget(a,80));
    uint64_t world1=0,world2=0,other=0;
    assert(reserveDeviceIndexBytes(a,48,world1));
    assert(reserveDeviceIndexBytes(a,32,world2));
    assert(!reserveDeviceIndexBytes(a,1,other));
    assert(!configureDeviceIndexBudget(a,128)); // conflicting live world
    auto stats=deviceIndexBudgetStats(a);
    assert(stats.reservedBytes==80 && stats.activeLeases==2);
    assert(stats.limitBytes==80);
    // Two worlds share one uploaded-byte allowance for a given RHI frame.
    assert(configureDeviceFrameUploadBudget(a,24));
    assert(deviceFrameUploadAllowance(a,7)==24);
    accountDeviceFrameUpload(a,7,12);
    assert(deviceFrameUploadAllowance(a,7)==12);
    assert(configureDeviceFrameUploadBudget(a,24)); // must NOT reset allowance
    assert(deviceFrameUploadAllowance(a,7)==12);
    assert(!configureDeviceFrameUploadBudget(a,32)); // live worlds disagree
    accountDeviceFrameUpload(a,7,12);
    assert(deviceFrameUploadAllowance(a,7)==0);
    assert(deviceFrameUploadAllowance(a,8)==24);
    assert(deviceFrameUploadAllowance(a,0)==0);
    assert(configureDeviceIndexBudget(b,80));
    assert(reserveDeviceIndexBytes(b,80,other)); // separate devices
    assert(deviceIndexBudgetStats(b).reservedBytes==80);
    releaseDeviceIndexBytes(a,world1);
    assert(deviceIndexBudgetStats(a).reservedBytes==32);
    uint64_t next=0;
    assert(reserveDeviceIndexBytes(a,40,next));
    assert(next!=world1 && next!=world2 && next!=other);
    releaseDeviceIndexBytes(a,world1); // stale double release is harmless
    assert(deviceIndexBudgetStats(a).reservedBytes==72);
    releaseDeviceIndexBytes(a,world2);
    releaseDeviceIndexBytes(a,next);
    assert(deviceIndexBudgetStats(a).activeLeases==0);
    assert(configureDeviceIndexBudget(a,128)); // previous worlds unloaded
    releaseDeviceIndexBytes(b,other);
    assert(deviceIndexBudgetStats(b).reservedBytes==0);
}
static void sharedDiskWorkers() {
    assert(configureSharedDiskIo(2,4096));
    const auto folder=std::filesystem::temp_directory_path() /
        ("strata-world-io-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const auto path=folder / DiskIndexPages::archiveFilename("many/worlds/model");
    const auto asset=makeAsset();
    std::string error;
    assert(DiskIndexPages::writeArchive(path.string(),asset,&error));
    // Multiple independent ECS world/source instances share two actual
    // workers, not one thread per archive. Every model still has its own
    // cancellable request and verified CPU-ready cache.
    std::vector<std::unique_ptr<DiskIndexPages>> sources;
    for(int i=0;i<24;++i){
        auto src=std::make_unique<DiskIndexPages>();
        assert(src->open(path.string(),asset,48,8,&error));
        src->retainRequested({{0,1},{0,2}});
        assert(src->request(0,1));
        assert(src->request(0,2));
        sources.push_back(std::move(src));
    }
    assert(eventually([&]{
        for(const auto& src:sources) {
            std::vector<uint32_t> one,two;
            if(!src->ready(0,1,one)||!src->ready(0,2,two))
                return false;
            if(one.size()!=3||two.size()!=3)return false;
        }
        return true;
    }));
    const auto active=sharedDiskIoStats();
    assert(active.workers==2);
    assert(active.activeJobs<=2);
    assert(active.readyLimitBytes==4096);
    assert(active.readyBytes==24*24);
    assert(!configureSharedDiskIo(4,4096)); // workers already created
    for(auto& src:sources)src->close();
    sources.clear();
    assert(eventually([&]{return sharedDiskIoStats().readyBytes==0;}));
    std::filesystem::remove_all(folder);
}
int main() {
    globalGpuBudget();
    sharedDiskWorkers();
    return 0;
}
