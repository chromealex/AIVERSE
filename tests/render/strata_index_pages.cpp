// Stage 15.2 incremental indexed GPU page regression.
// Intentionally NOT executed until the owner's final native build/test pass.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataIndexResidency.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class PageRecorder final : public gfx::Device {
public:
    explicit PageRecorder(gfx::Backend backend)
        : info_{backend,true,false,"Strata indexed page recorder"} {}
    gfx::DeviceInfo info_{};
    std::string error_;
    bool healthy_=true, canUpload_=true, failOneUpload_=false;
    uint64_t failAtCall_=0;
    uint32_t nextId_=1, allocations_=0, frees_=0;
    uint64_t serial_=1, completed_=0, bytesWritten_=0, uploadCalls_=0;
    std::map<uint32_t,std::vector<uint8_t>> data_;
    std::map<uint32_t,gfx::BufferType> types_;
    const gfx::DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }
    bool gpuHealthy() const override { return healthy_; }
    bool canUpdateSharedBuffers() const override { return canUpload_; }
    uint64_t frameSerial() const override { return serial_; }
    uint64_t completedFrameSerial() const override { return completed_; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if(!d.size || d.size>128u*1024u*1024u)return {};
        gfx::BufferHandle h{nextId_++};
        data_[h.id].assign(d.size,0);
        types_[h.id]=d.type;
        if(d.data) std::memcpy(data_[h.id].data(),d.data,d.size);
        ++allocations_;
        return h;
    }
    bool updateSharedBufferChecked(gfx::BufferHandle h,const void* bytes,
                                   size_t size,size_t offset=0) override {
        if(!canUpload_ || failOneUpload_ ||
           (failAtCall_ && uploadCalls_+1==failAtCall_)){
            failOneUpload_=false;
            failAtCall_=0;
            return false;
        }
        if(!bytes || !size || !types_.count(h.id) ||
           types_[h.id]!=gfx::BufferType::Storage ||
           offset>data_[h.id].size() ||
           size>data_[h.id].size()-offset)return false;
        std::memcpy(data_[h.id].data()+offset,bytes,size);
        bytesWritten_+=size;
        ++uploadCalls_;
        return true;
    }
    void updateBuffer(gfx::BufferHandle, const void*,size_t,size_t=0) override {
        assert(false && "Strata indexed pages need checked writes");
    }
    void destroyBuffer(gfx::BufferHandle h) override {
        if(data_.erase(h.id))++frees_;
        types_.erase(h.id);
    }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
    void destroyPipeline(gfx::PipelineHandle) override {}
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

static ModelAsset asset() {
    ModelAsset a;
    for(uint32_t p=0;p<2;++p) {
        ModelPrimitiveAsset entry{};
        entry.mesh=p;
        entry.material=p;
        auto& g=entry.geometry;
        g.roots={0};
        g.nodes.resize(3);
        g.bounds.resize(3);
        g.indices={0,1,2,1,3,2,0,1,2,1,3,2};
        g.nodes[0].firstChild=1;
        g.nodes[0].childCount=2;
        g.nodes[0].firstIndex=0;
        g.nodes[0].indexCount=6;
        g.nodes[0].geometricError=1;
        g.nodes[1].firstIndex=6;
        g.nodes[1].indexCount=3;
        g.nodes[2].firstIndex=9;
        g.nodes[2].indexCount=3;
        for(auto& n:g.nodes){n.resident=true;n.materialGroup=p;}
        for(auto& b:g.bounds)b={{-1,-1,-1},{1,1,1}};
        a.primitives.push_back(std::move(entry));
    }
    return a;
}

static void pageLifecycle(gfx::Backend backend) {
    PageRecorder dev(backend);
    auto data=asset();
    std::string error;
    IndexResidency cache;
    // 48 B roots + one 24 B sibling group; fixed capacity 72 B.
    assert(cache.initialize(dev,data,72,&error,true,24));
    assert(cache.incrementalEnabled());
    assert(cache.stats().arenaBytes==72);
    assert(cache.stats().activeBytes==48);
    assert(cache.stats().uploadedBytes==48);
    assert(dev.allocations_==1 && dev.data_.size()==1);
    const auto handle=cache.buffer();
    const auto rootA=cache.span(0,0);
    const auto rootB=cache.span(1,0);
    assert(rootA.firstIndex==0 && rootB.firstIndex==6);
    const auto writesAtRoots=dev.bytesWritten_;

    // GPU update entirely rejected => no visible residency mutation.
    dev.failOneUpload_=true;
    assert(!cache.update({{0,1,10.f}},&error));
    assert(cache.buffer()==handle);
    assert(!cache.resident(0,1));
    assert(cache.stats().failedUploads==1);
    assert(cache.span(0,0).firstIndex==rootA.firstIndex);
    assert(cache.span(1,0).firstIndex==rootB.firstIndex);

    dev.serial_=2;
    assert(cache.update({{0,1,10.f}},&error));
    assert(cache.buffer()==handle);
    assert(cache.stats().arenaBytes==72);
    assert(cache.stats().activeBytes==72);
    assert(cache.stats().generations==1);
    assert(cache.resident(0,1) && cache.resident(0,2));
    assert(cache.span(0,1).firstIndex==12);
    assert(cache.span(0,2).firstIndex==15);
    assert(dev.bytesWritten_==writesAtRoots+24);
    const uint64_t beforeNoop=dev.bytesWritten_;
    assert(cache.update({{0,1,10.f}},&error));
    assert(dev.bytesWritten_==beforeNoop);

    // Frame serial zero is not a usable retirement fence. Keep already
    // visible leaves rather than generating an unrecyclable GPU hole.
    dev.serial_=0;
    assert(cache.update({},&error));
    assert(cache.resident(0,1) && cache.resident(0,2));

    // Evict old children; same current frame cannot reuse their offsets.
    // The other source still falls back to its resident parent.
    dev.serial_=3;
    assert(cache.update({{1,1,20.f}},&error));
    assert(!cache.resident(0,1) && cache.resident(0,0));
    assert(!cache.resident(1,1) && cache.resident(1,0));
    assert(cache.stats().fenceReuseDeferrals>=1);
    assert(dev.bytesWritten_==beforeNoop);

    // Until the actual completed serial reaches retirement, no unsafe
    // overwrite may occur, even on a different frame.
    dev.serial_=4;
    dev.completed_=2;
    assert(cache.update({{1,1,20.f}},&error));
    assert(!cache.resident(1,1));
    assert(dev.bytesWritten_==beforeNoop);

    // Completed fence permits hole reuse, with one persistent RHI handle.
    dev.completed_=3;
    assert(cache.update({{1,1,20.f}},&error));
    assert(cache.resident(1,1) && cache.resident(1,2));
    assert(cache.span(1,1).firstIndex==12);
    assert(cache.span(1,2).firstIndex==15);
    assert(cache.buffer()==handle);
    assert(cache.span(0,0).firstIndex==rootA.firstIndex);
    assert(dev.allocations_==1 && dev.data_.size()==1);
    assert(cache.stats().activeBytes==72);

    // Upload-safety can be temporarily unavailable during an encoder.
    dev.canUpload_=false;
    assert(!cache.update({},&error));
    assert(cache.resident(1,1));
    dev.canUpload_=true;
    cache.shutdown(dev);
    assert(dev.data_.empty() && dev.frees_==1);
}

static void partiallyFailedUpload() {
    PageRecorder dev(gfx::Backend::Vulkan);
    auto data=asset();
    IndexResidency cache;
    std::string error;
    assert(cache.initialize(dev,data,96,&error,true,0));
    const auto handle=cache.buffer();
    const auto before=cache.stats().activeBytes;
    // Fail the SECOND child write: first successful checked upload is to
    // a never-published slot. The two-child frontier must remain absent.
    dev.failAtCall_=dev.uploadCalls_+2;
    assert(!cache.update({{0,1,10.f}},&error));
    assert(cache.buffer()==handle);
    assert(cache.stats().activeBytes==before);
    assert(!cache.resident(0,1) && !cache.resident(0,2));
    assert(cache.resident(0,0) && cache.resident(1,0));
    // Next retry can safely write the same unused slots and then publish
    // both siblings atomically; no new GPU buffer allocation is required.
    assert(cache.update({{0,1,10.f}},&error));
    assert(cache.resident(0,1) && cache.resident(0,2));
    assert(cache.stats().activeBytes==before+24);
    assert(dev.allocations_==1);
    cache.shutdown(dev);
    assert(dev.data_.empty());
}

static void bandwidthLimits() {
    PageRecorder dev(gfx::Backend::Vulkan);
    auto data=asset();
    std::string error;
    IndexResidency cache;
    assert(cache.initialize(dev,data,96,&error,true,16));
    assert(cache.incrementalEnabled());
    const auto initial=dev.bytesWritten_;
    // Each group needs 24 B. Strict cap 16 B: cannot publish half a
    // sibling group, so no progress; root stays valid.
    assert(cache.update({{0,1,2.f}},&error));
    assert(!cache.resident(0,1));
    assert(cache.stats().uploadBudgetDeferrals>0);
    assert(dev.bytesWritten_==initial);
    cache.shutdown(dev);

    IndexResidency allowed;
    assert(allowed.initialize(dev,data,96,&error,true,24));
    assert(allowed.update({{0,1,2.f},{1,1,1.f}},&error));
    assert(allowed.resident(0,1) && allowed.resident(0,2));
    assert(!allowed.resident(1,1));
    assert(allowed.stats().activeBytes==72);
    assert(allowed.stats().uploadBudgetDeferrals>0);
    allowed.shutdown(dev);
}

static void aggregateAllowance() {
    PageRecorder dev(gfx::Backend::Vulkan);
    auto data=asset();
    IndexResidency cache;
    std::string error;
    assert(cache.initialize(dev,data,96,&error,true,0));
    const auto atRoots=cache.stats().gpuTransferBytes;
    // Scene governor can deny an entire 24-byte sibling group while
    // preserving its source root; 0 means no new GPU uploads.
    assert(cache.update({{0,1,4.f}},&error,0));
    assert(!cache.resident(0,1));
    assert(cache.stats().gpuTransferBytes==atRoots);
    assert(cache.update({{0,1,4.f}},&error,16));
    assert(!cache.resident(0,1));
    assert(cache.update({{0,1,4.f}},&error,24));
    assert(cache.resident(0,1) && cache.resident(0,2));
    assert(cache.stats().gpuTransferBytes==atRoots+24);
    cache.shutdown(dev);
}

static void portableFallback() {
    for(auto backend:{gfx::Backend::GLES3,gfx::Backend::GL33}) {
        PageRecorder dev(backend);
        auto data=asset();
        std::string error;
        IndexResidency cache;
        assert(cache.initialize(dev,data,72,&error,true,24));
        assert(!cache.incrementalEnabled());
        const auto first=cache.buffer();
        assert(dev.types_.at(first.id)==gfx::BufferType::Index);
        assert(cache.update({{0,1,4.f}},&error));
        assert(cache.buffer()!=first); // ES3 immutable-generation fallback
        cache.shutdown(dev);
        assert(dev.data_.empty());
    }
}
int main() {
    pageLifecycle(gfx::Backend::Metal);
    pageLifecycle(gfx::Backend::Vulkan);
    partiallyFailedUpload();
    bandwidthLimits();
    aggregateAllowance();
    portableFallback();
    return 0;
}
