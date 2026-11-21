// STRATA Stage 15.3 disk-to-GPU index-page contracts.
// Native CTest execution is deferred until the final combined build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataDiskPages.h"
#include "engine/render/StrataIndexResidency.h"
#include "engine/render/StrataView.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class DiskRecorder final : public gfx::Device {
public:
    gfx::DeviceInfo info_{gfx::Backend::Vulkan,true,false,"STRATA disk recorder"};
    std::string error_;
    uint32_t next_=1, allocations_=0;
    uint64_t transfers_=0, serial_=1, completed_=0;
    bool failUpload_=false;
    std::map<uint32_t,std::vector<uint8_t>> buffers_;
    const gfx::DeviceInfo& info() const override {return info_;}
    const std::string& lastError() const override {return error_;}
    uint64_t frameSerial() const override {return serial_;}
    uint64_t completedFrameSerial() const override {return completed_;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if(!d.size || d.type!=gfx::BufferType::Storage)return {};
        gfx::BufferHandle h{next_++};
        buffers_[h.id].resize(d.size);
        if(d.data)std::memcpy(buffers_[h.id].data(),d.data,d.size);
        ++allocations_;
        return h;
    }
    bool updateSharedBufferChecked(gfx::BufferHandle h,const void* data,
                                   size_t size,size_t offset=0) override {
        if(failUpload_){failUpload_=false;return false;}
        auto it=buffers_.find(h.id);
        if(it==buffers_.end() || !data ||
           offset>it->second.size() || size>it->second.size()-offset)
            return false;
        std::memcpy(it->second.data()+offset,data,size);
        transfers_+=size;
        return true;
    }
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t=0) override {
        assert(false && "only checked GPU upload path is allowed");
    }
    void destroyBuffer(gfx::BufferHandle h) override {buffers_.erase(h.id);}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override {return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override {}
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
    ModelAsset source;
    ModelPrimitiveAsset p;
    p.mesh=0;p.primitive=0;p.material=0;
    auto& g=p.geometry;
    g.indices={0,1,2,1,3,2,0,1,2,1,3,2};
    g.roots={0};
    g.nodes.resize(3);
    g.bounds.resize(3);
    g.nodes[0].firstChild=1;
    g.nodes[0].childCount=2;
    g.nodes[0].firstIndex=0;
    g.nodes[0].indexCount=6;
    g.nodes[0].geometricError=1.f;
    g.nodes[1].firstIndex=6;g.nodes[1].indexCount=3;
    g.nodes[2].firstIndex=9;g.nodes[2].indexCount=3;
    for(auto& n:g.nodes){n.resident=true;n.materialGroup=0;}
    for(auto& b:g.bounds)b={{-1.f,-1.f,-1.f},{1.f,1.f,1.f}};
    source.primitives.push_back(std::move(p));
    return source;
}
template<class Pred>
static bool within(Pred&& predicate) {
    for(int i=0;i<4000;++i){
        if(predicate())return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}
static void runDisk() {
    const auto folder=std::filesystem::temp_directory_path() /
        ("strata-stage153-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const auto key=DiskIndexPages::archiveFilename("../models/../../robot.glb");
    assert(key.find('/')==std::string::npos);
    assert(key.find('\\')==std::string::npos);
    const auto path=folder / key;
    const auto source=makeAsset();
    std::string error;
    assert(DiskIndexPages::writeArchive(path.string(),source,&error));
    assert(error.empty());
    const auto length=std::filesystem::file_size(path);

    {
        DiskIndexPages disk;
        assert(!disk.open(path.string(),source,12,16,&error));
        assert(!error.empty()); // 24 bytes required for BOTH siblings
        assert(disk.open(path.string(),source,48,16,&error));
        std::vector<uint32_t> root;
        assert(disk.readRoot(0,0,root,&error));
        assert(root==std::vector<uint32_t>({0,1,2,1,3,2}));
        assert(disk.stats().completedReads==1);
        disk.retainRequested({{0,1},{0,2}});
        assert(disk.request(0,1));
        assert(disk.request(0,1)); // duplicate must not queue twice
        assert(disk.request(0,2));
        assert(within([&] {
            std::vector<uint32_t> a,b;
            return disk.ready(0,1,a) && disk.ready(0,2,b);
        }));
        std::vector<uint32_t> first;
        assert(disk.ready(0,1,first));
        assert(first==std::vector<uint32_t>({0,1,2}));
        assert(disk.stats().requests==2);
        assert(disk.stats().readyBytes==24);
        disk.consume(0,1);
        disk.consume(0,2);
        assert(disk.stats().readyBytes==0);
        disk.retainRequested({{0,1}});
        assert(disk.request(0,1));
        disk.retainRequested({}); // camera moved; drop old prefetch
        assert(within([&]{return disk.stats().queued==0;}));
        assert(disk.stats().readyBytes==0);
        disk.close();
    }

    // Runtime GPU residency uses ARCHIVE roots and the async page bytes.
    {
        // Runtime copy only: the original import asset remains available
        // for subsequent stale-archive and corruption tests.
        auto runtime=makeAsset();
        auto disk=std::make_shared<DiskIndexPages>();
        assert(disk->open(path.string(),runtime,48,16,&error));
        DiskRecorder device;
        IndexResidency cache;
        assert(cache.initialize(device,runtime,48,&error,true,48,disk));
        const auto freed=runtime.primitives[0].geometry.discardCpuIndexPayload();
        assert(freed==12*sizeof(uint32_t));
        assert(runtime.primitives[0].geometry.indices.empty());
        ViewConfig view;
        view.objectToClip=mat4::identity();
        view.width=800;
        view.height=600;
        view.errorThresholdPixels=.01f;
        ViewPlan reference;
        assert(planView(runtime.primitives[0].geometry,view,reference,&error));
        assert(reference.ranges.size()==2);
        assert(cache.incrementalEnabled());
        assert(cache.stats().diskCompletedReads==1);
        assert(cache.resident(0,0) && !cache.resident(0,1));
        const auto handle=cache.buffer();
        assert(cache.update({{0,1,4.f}},&error));
        assert(!cache.resident(0,1)); // IO is nonblocking
        assert(within([&]{
            assert(cache.update({{0,1,4.f}},&error));
            return cache.resident(0,1) && cache.resident(0,2);
        }));
        assert(cache.buffer()==handle && device.allocations_==1);
        assert(cache.span(0,1).firstIndex==6);
        assert(cache.span(0,2).firstIndex==9);
        assert(cache.stats().diskBytesRead==48);
        assert(cache.stats().diskCompletedReads==3);
        assert(cache.stats().diskReadyBytes==0);
        cache.shutdown(device);
        assert(device.buffers_.empty());
        disk->close();
    }

    // Source fingerprint prevents a valid but stale archive from being used.
    {
        auto changed=makeAsset();
        changed.primitives[0].geometry.indices[11]=17;
        DiskIndexPages stale;
        assert(!stale.open(path.string(),changed,48,16,&error));
        assert(!error.empty());
    }

    // Corruption of one leaf must fail checksum without producing any
    // partially verified disk payload, while the original root stays good.
    {
        std::fstream edit(path,std::ios::binary|std::ios::in|std::ios::out);
        assert(edit);
        edit.seekg(std::streamoff(length-1),std::ios::beg);
        char original=0;
        edit.read(&original,1);
        assert(edit);
        edit.seekp(std::streamoff(length-1),std::ios::beg);
        const char corrupted=char(0xff);
        edit.write(&corrupted,1);
        edit.flush();
        assert(edit);
        edit.close();
        DiskIndexPages disk;
        assert(disk.open(path.string(),source,48,16,&error));
        std::vector<uint32_t> root;
        assert(disk.readRoot(0,0,root,&error));
        disk.retainRequested({{0,2}});
        assert(disk.request(0,2));
        assert(within([&]{return disk.failed(0,2);}));
        std::vector<uint32_t> unverified;
        assert(!disk.ready(0,2,unverified));
        assert(disk.stats().failedReads==1);
        // A failed read is not pinned forever: after a short request
        // backoff the page is retried, and a good read clears the failure.
        assert(!disk.request(0,2)); // backoff
        {
            std::fstream repair(path,std::ios::binary|std::ios::in|std::ios::out);
            assert(repair);
            repair.seekp(std::streamoff(length-1),std::ios::beg);
            repair.write(&original,1);
            repair.flush();
            assert(repair);
        }
        bool retried=false;
        for(int i=0;i<8 && !retried;++i)retried=disk.request(0,2);
        assert(retried);
        std::vector<uint32_t> repaired;
        assert(within([&]{return disk.ready(0,2,repaired);}));
        assert(repaired==std::vector<uint32_t>({1,3,2}));
        assert(!disk.failed(0,2));
        assert(disk.stats().failedReads==1);
        disk.consume(0,2);
        // Persistent corruption becomes permanent after bounded retries.
        {
            std::fstream again(path,std::ios::binary|std::ios::in|std::ios::out);
            assert(again);
            again.seekp(std::streamoff(length-1),std::ios::beg);
            again.write(&corrupted,1);
            again.flush();
            assert(again);
        }
        for(int i=0;i<200;++i){
            (void)disk.request(0,2);
            (void)within([&]{return disk.failed(0,2);});
        }
        assert(disk.failed(0,2));
        assert(!disk.request(0,2));
        assert(disk.stats().failedReads==6);
        disk.close();
    }

    // Broken magic rejects the archive at scene load (Legacy fallback).
    {
        std::fstream edit(path,std::ios::binary|std::ios::in|std::ios::out);
        assert(edit);
        edit.seekp(0);
        const char x='?';
        edit.write(&x,1);
        edit.close();
        DiskIndexPages broken;
        assert(!broken.open(path.string(),source,48,16,&error));
    }
    std::filesystem::remove_all(folder);
}
int main(){runDisk();return 0;}
