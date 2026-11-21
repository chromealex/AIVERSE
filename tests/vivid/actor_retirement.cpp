// VIVID actor GPU retirement contract. Fake RHI verifies token ordering,
// batch completion, failure retention and fallback WITHOUT actual GPU stalls.
// Real Metal device parity is covered by the vivid_metal_rhi fixture.
#include "engine/character/vivid/GpuActorRetirement.h"
#include "engine/character/vivid/GpuMorphAssetCache.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;

namespace {
void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
class FakeMetal final : public gfx::Device {
public:
    gfx::DeviceInfo deviceInfo{};
    std::string error;
    std::uint32_t nextBuffer = 1, nextCompute = 1;
    std::uint64_t currentFrame = 0, nextToken = 1;
    bool inFrame=false, computing=false, tokenSupported=true;
    bool idleAllowed=true, healthy=true;
    int waits=0, issuedTokens=0, tokensClosed=0, tokensAbandoned=0;
    int destroyedBuffers=0, destroyedComputes=0;
    std::map<std::uint32_t,std::vector<unsigned char>> buffers;
    std::map<std::uint64_t,gfx::ResourceRetirementStatus> tokens;

    FakeMetal(){deviceInfo.backend=gfx::Backend::Metal;}
    const gfx::DeviceInfo& info() const override{return deviceInfo;}
    const std::string& lastError() const override{return error;}
    bool gpuHealthy() const override{return healthy;}
    bool supportsCompute() const override{return true;}
    std::uint64_t frameSerial() const override{return currentFrame;}

    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if(!d.size)return {};
        const auto id=nextBuffer++;
        auto& b=buffers[id];b.resize(d.size,0);
        if(d.data)std::memcpy(b.data(),d.data,d.size);
        return {id};
    }
    void updateBuffer(gfx::BufferHandle h,const void* data,
                      std::size_t bytes,std::size_t offset=0) override {
        auto it=buffers.find(h.id);
        require(it!=buffers.end() && data && offset<=it->second.size() &&
                bytes<=it->second.size()-offset,"invalid weight upload");
        std::memcpy(it->second.data()+offset,data,bytes);
    }
    void destroyBuffer(gfx::BufferHandle h) override {
        require(buffers.erase(h.id)==1,"destroyed stale GPU buffer");
        ++destroyedBuffers;
    }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&)override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int)override{}
    void destroyTexture(gfx::TextureHandle)override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&)override{return {};}
    void destroyShader(gfx::ShaderHandle)override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&)override{return {};}
    void destroyPipeline(gfx::PipelineHandle)override{}

    gfx::ComputeHandle createCompute(const gfx::ComputeDesc& desc) override {
        require(desc.msl && desc.glsl, "missing VIVID compute shader pair");
        return {nextCompute++};
    }
    void destroyCompute(gfx::ComputeHandle)override{++destroyedComputes;}
    bool beginCompute(gfx::ComputeHandle h)override{
        if(!inFrame || computing || !h.valid())return false;
        computing=true;return true;
    }
    void setComputeBuffer(int,gfx::BufferHandle)override{}
    void setComputeUniforms(const void*,std::size_t)override{}
    void dispatch(std::uint32_t,std::uint32_t=1)override{
        require(computing,"compute dispatch outside encoder");
    }
    void endCompute()override{require(computing,"missing encoder");computing=false;}

    void beginFrame(int,int)override{require(!inFrame,"nested frame");inFrame=true;++currentFrame;}
    void endFrame()override{require(inFrame && !computing,"incomplete frame");inFrame=false;}
    bool waitForResourceIdle()override {
        ++waits;
        return !inFrame && idleAllowed;
    }
    std::uint64_t beginResourceRetirement()override {
        require(!inFrame,"retirement marker recorded before endFrame");
        if(!tokenSupported)return 0;
        const auto token=nextToken++;
        tokens[token]=gfx::ResourceRetirementStatus::Pending;
        ++issuedTokens;return token;
    }
    bool resourceRetirementComplete(std::uint64_t token)override {
        const auto it=tokens.find(token);
        return it!=tokens.end()&&it->second==gfx::ResourceRetirementStatus::Complete;
    }
    gfx::ResourceRetirementStatus resourceRetirementStatus(std::uint64_t token)override {
        const auto it=tokens.find(token);
        return it==tokens.end()?gfx::ResourceRetirementStatus::Failed:it->second;
    }
    void endResourceRetirement(std::uint64_t token)override {
        require(resourceRetirementComplete(token),"closed an incomplete token");
        tokens.erase(token);++tokensClosed;
    }
    void abandonRetirementTokenAfterIdle(std::uint64_t token)override {
        tokens.erase(token);++tokensAbandoned;
    }

    void beginPass(const gfx::PassDesc&)override{}
    void endPass()override{}
    void setViewport(const gfx::Rect&)override{}
    void setScissor(const gfx::Rect&)override{}
    void setPipeline(gfx::PipelineHandle)override{}
    void setVertexBuffer(gfx::BufferHandle,std::size_t=0)override{}
    void setIndexBuffer(gfx::BufferHandle,std::size_t=0)override{}
    void setTexture(int,gfx::TextureHandle)override{}
    void setUniforms(int,const void*,std::size_t)override{}
    void draw(std::uint32_t,std::uint32_t=0)override{}
    void drawIndexed(std::uint32_t,std::uint32_t=0,std::int32_t=0,std::uint32_t=1)override{}
};

struct Fixture {
    render::Model model;
    SparseMorphLibrary library;
    std::vector<render::ModelVertex> original;
    FaceShapeBinding mapping;

    Fixture() {
        original.resize(2);
        original[0].pos={0,0,0};original[1].pos={1,0,0};
        for(auto& vertex:original) {
            vertex.nrm={0,1,0};
            vertex.tangent={1,0,0,1};
        }
        model.vertices=original;
        model.vb={900};model.ib={901};
        model.meshes.resize(1);
        model.meshes[0].primitives.resize(1);
        auto& p=model.meshes[0].primitives[0];
        p.firstVertex=0;p.vertexCount=2;p.firstIndex=0;p.indexCount=3;

        MorphDelta delta{};
        delta.vertex=0;delta.position={0.4f,0,0};
        std::vector<MorphTargetInput> inputs;
        inputs.push_back({"jaw",{delta}});
        library=SparseMorphLibrary::compile(2,std::move(inputs));
        const std::vector<ShapeRoute> routes{{Channel::JawOpen,"jaw",1.0f}};
        mapping=FaceShapeBinding::compile(library,routes);
    }
};

std::unique_ptr<GpuActorStream> createActor(FakeMetal& dev, Fixture& fixture) {
    auto actor=std::make_unique<GpuActorStream>();
    require(actor->initialize(dev,fixture.model,fixture.library,fixture.original),
            "actor GPU initialization failed");
    return actor;
}

void completePendingBatch() {
    FakeMetal dev;
    Fixture fixture;
    GpuActorRetirementQueue retirement(dev);
    auto a=createActor(dev,fixture),b=createActor(dev,fixture);
    require(dev.buffers.size()==16,"two actors did not own independent buffers");
    FaceFrame beforeFrame{};
    require(!a->dispatch(beforeFrame,fixture.mapping),
            "actor dispatch before the first frame must fail");
    dev.beginFrame(16,16);
    FaceFrame face{};face.values[static_cast<size_t>(Channel::JawOpen)]=0.5f;
    require(a->dispatch(face,fixture.mapping),"actor A dispatch");
    require(b->dispatch(face,fixture.mapping),"actor B dispatch");
    dev.endFrame();

    require(retirement.retire(a) && retirement.retire(b),"enqueue actors");
    require(!a && !b && retirement.pending()==2,"caller retained retired actors");
    require(!retirement.collect(),"new pending token immediately completed");
    require(dev.issuedTokens==1 && dev.waits==0 &&
            retirement.stats().pendingBatches==1,
            "actors were not grouped under a single nonblocking GPU token");
    require(!retirement.collect() && dev.buffers.size()==16,
            "pending GPU marker prematurely destroyed buffers");

    dev.tokens[1]=gfx::ResourceRetirementStatus::Complete;
    require(retirement.collect(),"completed token not released");
    require(retirement.pending()==0 && dev.buffers.empty() &&
            dev.destroyedBuffers==16 && dev.destroyedComputes==4 &&
            dev.tokensClosed==1 && dev.waits==0,
            "GPU completion release failed or used a blocking idle");
    const auto stats=retirement.stats();
    require(stats.completedResources==2 && stats.completedBatches==1,
            "multi-actor retirement stats wrong");
}

void sharedImmutableGpuAsset() {
    FakeMetal device;
    Fixture fixture;
    GpuMorphAssetCache cache(device);
    auto asset=cache.acquire(fixture.library,fixture.original);
    auto cacheHit=cache.acquire(fixture.library,fixture.original);
    require(asset && asset->ready() && cacheHit.get()==asset.get() &&
            cache.cachedKeys()==1,
            "GPU asset content cache failed to reuse immutable resources");
    auto first=std::make_unique<GpuActorStream>();
    auto second=std::make_unique<GpuActorStream>();
    require(first->initializeShared(device,fixture.model,asset),
            "first shared-asset actor initialization failed");
    require(second->initializeShared(device,fixture.model,asset),
            "second shared-asset actor initialization failed");
    // 5 immutable GPU buffers (base, deltas, CSR offsets/links, original
    // ModelVertex) plus 3 mutable buffers per actor, instead of 8*2.
    require(device.buffers.size()==11 &&
            device.nextCompute==3 && first->sharedAsset()==asset.get() &&
            second->sharedAsset()==asset.get() &&
            first->vertexBuffer()!=second->vertexBuffer(),
            "actors duplicated shared GPU resource data or reused outputs");

    FaceFrame face{};
    face.values[static_cast<size_t>(Channel::JawOpen)]=0.7f;
    device.beginFrame(16,16);
    require(first->dispatch(face,fixture.mapping) &&
            second->dispatch(face,fixture.mapping),
            "shared compute pipeline multi-actor dispatch failed");
    render::ModelDraw drawA,drawB;
    drawA.model=drawB.model=&fixture.model;
    require(first->attach(drawA) && second->attach(drawB) &&
            drawA.vertexOverride!=drawB.vertexOverride,
            "shared immutable asset leaked per-instance write state");
    device.endFrame();

    GpuActorRetirementQueue queue(device);
    require(queue.retire(first) && queue.retire(second),"shared-asset retire rejected");
    require(!queue.collect(),"GPU marker completed at creation");
    require(device.buffers.size()==11,"shared buffers retired while GPU work pending");
    device.tokens[1]=gfx::ResourceRetirementStatus::Complete;
    require(queue.collect(),"shared-asset GPU marker failed to retire actors");
    require(device.buffers.size()==5 &&
            device.destroyedComputes==0 && asset->ready(),
            "shared immutable asset destroyed while still externally owned");
    asset.reset();
    require(device.buffers.size()==5 &&
            device.destroyedComputes==0 && cacheHit->ready(),
            "GPU asset destroyed while another cache caller holds it");
    cacheHit.reset();
    require(device.buffers.empty() && device.destroyedComputes==2 &&
            cache.pruneExpired()==1 && cache.cachedKeys()==0,
            "last shared asset release failed to expire weak cache entry");

    auto altered=fixture.original;
    altered[0].pos.x+=0.125f;
    auto changed=cache.acquire(fixture.library,altered);
    require(changed && changed->ready() && cache.cachedKeys()==1,
            "changed model revision did not create an independent GPU asset");
    changed.reset();
    require(cache.pruneExpired()==1 && device.buffers.empty(),
            "new GPU asset revision leaked after release");
}

void manyActorStress() {
    constexpr int count=128;
    FakeMetal device;
    Fixture fixture;
    GpuMorphAssetCache cache(device);
    auto shared=cache.acquire(fixture.library,fixture.original);
    require(shared && shared->ready(),"128-actor GPU asset missing");
    std::vector<std::unique_ptr<GpuActorStream>> actors;
    actors.reserve(count);
    for(int i=0;i<count;++i) {
        auto actor=std::make_unique<GpuActorStream>();
        require(actor->initializeShared(device,fixture.model,shared),
                "128-actor shared asset initialization failed");
        actors.push_back(std::move(actor));
    }
    require(device.buffers.size()==5+3*count && device.nextCompute==3,
            "128 actors duplicated immutable geometry or compute programs");
    for(int frame=0;frame<4;++frame) {
        device.beginFrame(32,32);
        for(int i=0;i<count;++i) {
            FaceFrame face{};
            face.values[static_cast<std::size_t>(Channel::JawOpen)] =
                float((i+frame)%100)/100.0f;
            require(actors[std::size_t(i)]->dispatch(face,fixture.mapping),
                    "128-actor compute dispatch failed");
            render::ModelDraw draw;
            draw.model=&fixture.model;
            require(actors[std::size_t(i)]->attach(draw) &&
                    draw.vertexOverride.valid(),
                    "128-actor stream binding failed");
        }
        device.endFrame();
    }
    GpuActorRetirementQueue queue(device);
    for(auto& actor:actors)require(queue.retire(actor),"128-actor retirement rejected");
    require(!queue.collect() && queue.pending()==count &&
            device.issuedTokens==1 && device.waits==0,
            "128-actor GPU marker batching failed");
    device.tokens[1]=gfx::ResourceRetirementStatus::Complete;
    require(queue.collect() && queue.pending()==0 &&
            device.buffers.size()==5 && device.waits==0 &&
            queue.stats().completedResources==count,
            "128-actor resources did not retire in one completed token");
    shared.reset();
    require(cache.pruneExpired()==1 && device.buffers.empty() &&
            device.destroyedComputes==2,
            "128-actor cache or immutable GPU programs leaked");
}

void failedTokenAndRecovery() {
    FakeMetal dev;Fixture fixture;
    GpuActorRetirementQueue retirement(dev);
    auto actor=createActor(dev,fixture);
    require(retirement.retire(actor),"failed-token actor not retired");
    require(!retirement.collect(),"marker unexpectedly complete");
    dev.tokens[1]=gfx::ResourceRetirementStatus::Failed;
    require(!retirement.collect(),"failed marker freed GPU memory");
    require(retirement.stats().failedFenceBatches==1 &&
            retirement.stats().unresolvedFailedFences==1 &&
            !dev.buffers.empty(),"failed-fence GPU buffers lost");
    dev.idleAllowed=false;
    require(!retirement.drain() && retirement.pending()==1 &&
            !dev.buffers.empty(),"failed idle barrier freed GPU buffers");
    dev.idleAllowed=true;
    require(retirement.drain() && retirement.pending()==0 &&
            dev.buffers.empty() && dev.tokensAbandoned==1,
            "successful fallback did not retire failed-token batch");
}

void unsupportedMarkersAndWrongDevice() {
    FakeMetal dev,other;
    Fixture fixture;
    GpuActorRetirementQueue retirement(dev);
    auto foreign=createActor(other,fixture);
    require(!retirement.retire(foreign) && bool(foreign),
            "actor from another gfx device was accepted");
    require(foreign->shutdownAfterIdle(),"foreign GPU resource cleanup failed");

    dev.tokenSupported=false;
    auto actor=createActor(dev,fixture);
    require(retirement.retire(actor),"fallback actor not queued");
    dev.idleAllowed=false;
    require(!retirement.collect() && retirement.pending()==1 &&
            !dev.buffers.empty(),"unsupported-token idle failure freed buffers");
    dev.idleAllowed=true;
    require(retirement.collect() && retirement.pending()==0 &&
            dev.buffers.empty() && dev.waits==2,
            "unsupported GPU markers did not use explicit idle recovery");
}
} // namespace

int main() {
    try {
        completePendingBatch();
        sharedImmutableGpuAsset();
        manyActorStress();
        failedTokenAndRecovery();
        unsupportedMarkersAndWrongDevice();
        std::cout << "VIVID async GPU actor retirement PASS "
                     "(batch token, pending, failed, idle recovery, device ownership)\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "VIVID async GPU actor retirement FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
