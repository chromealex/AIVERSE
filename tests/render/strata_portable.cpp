// Portable Strata raster contract regression: no GPU compute requirements.
// Recording RHI verifies GLES3 index-only fallback and Vulkan shared pool;
// the native Mac build will compile this target but execution is deferred.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataPortableRender.h"
#include "engine/render/Geometry.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class Recorder final : public gfx::Device {
public:
    explicit Recorder(gfx::Backend backend, bool mdi)
        : info_{backend,true,false,"Portable Strata recorder"}, mdi_(mdi) {}
    gfx::DeviceInfo info_{};
    bool mdi_=false;
    bool healthy_=true, failNextIndex_=false;
    uint64_t testFrameSerial_=0;
    uint32_t next_=1;
    uint32_t draws=0, allocs=0, releases=0;
    std::map<uint32_t,std::vector<uint8_t>> data_;
    std::string error_;
    const gfx::DeviceInfo& info() const override {return info_;}
    uint64_t frameSerial() const override {return testFrameSerial_;}
    const std::string& lastError() const override {return error_;}
    bool supportsMultiDrawIndirect() const override {return mdi_;}
    bool gpuHealthy() const override {return healthy_;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if(failNextIndex_ && d.type==gfx::BufferType::Index){
            failNextIndex_=false;return {};
        }
        if(!d.size || d.size>32u*1024u*1024u)return {};
        gfx::BufferHandle h{next_++};
        data_[h.id].resize(d.size);
        if(d.data)std::memcpy(data_[h.id].data(),d.data,d.size);
        ++allocs;
        return h;
    }
    void updateBuffer(gfx::BufferHandle h,const void* bytes,size_t length,
                      size_t offset=0) override {
        assert(data_.count(h.id)&&offset<=data_[h.id].size()&&
               length<=data_[h.id].size()-offset);
        std::memcpy(data_[h.id].data()+offset,bytes,length);
    }
    bool updateSharedBufferChecked(gfx::BufferHandle h,const void* bytes,
                                   size_t length,size_t offset=0) override {
        if(!data_.count(h.id)||!bytes||
           offset>data_[h.id].size()||
           length>data_[h.id].size()-offset)return false;
        updateBuffer(h,bytes,length,offset);return true;
    }
    void destroyBuffer(gfx::BufferHandle h) override {
        if(data_.erase(h.id))++releases;
    }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
    void destroyPipeline(gfx::PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setIndexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t=0) override {++draws;}
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override{
        ++draws;
    }
};

static Model makeModel(){
    Model m;
    m.path="portable-rigid";
    m.batching.autoLod=false;
    m.vertices.resize(4);
    m.vertices[0].pos={-.5f,-.5f,.4f};
    m.vertices[1].pos={ .5f,-.5f,.4f};
    m.vertices[2].pos={-.5f, .5f,.4f};
    m.vertices[3].pos={ .5f, .5f,.4f};
    m.indices={0,1,2,1,3,2};
    m.meshes.resize(1);
    Primitive p;
    p.firstVertex=0;p.vertexCount=4;
    p.firstIndex=0;p.indexCount=6;p.material=0;
    m.meshes[0].primitives.push_back(p);
    m.materials.resize(1);
    Node n;n.mesh=0;
    m.nodes.push_back(n);
    m.finalize();
    return m;
}
static ModelAsset makeAsset(){
    ModelAsset a;
    ModelPrimitiveAsset p;
    p.mesh=0;p.primitive=0;p.material=0;
    auto& g=p.geometry;
    g.indices={0,1,2,1,3,2,0,1,2,1,3,2};
    g.roots={0};
    g.nodes.resize(3);
    g.bounds.resize(3);
    g.nodes[0].firstChild=1;
    g.nodes[0].childCount=2;
    g.nodes[0].firstIndex=0;g.nodes[0].indexCount=6;
    g.nodes[0].geometricError=.25f;
    g.nodes[1].firstIndex=6;g.nodes[1].indexCount=3;
    g.nodes[2].firstIndex=9;g.nodes[2].indexCount=3;
    for(uint32_t i=0;i<3;++i){
        g.nodes[i].resident=true;
        g.nodes[i].materialGroup=0;
        g.bounds[i]={{-.5f,-.5f,.2f},{.5f,.5f,.8f}};
    }
    a.primitives.push_back(std::move(p));
    return a;
}
static void testBackend(gfx::Backend backend,bool mdi){
    Recorder dev(backend,mdi);
    Model original=makeModel();
    const auto asset=makeAsset();
    if(mdi)assert(uploadModelToGeometryPool(dev,original));
    else {
        gfx::BufferDesc v{};v.type=gfx::BufferType::Vertex;
        v.size=original.vertices.size()*sizeof(ModelVertex);
        v.data=original.vertices.data();
        original.vb=dev.createBuffer(v);
        gfx::BufferDesc i{};i.type=gfx::BufferType::Index;
        i.size=original.indices.size()*sizeof(uint32_t);
        i.data=original.indices.data();
        original.ib=dev.createBuffer(i);
        assert(original.vb.valid()&&original.ib.valid());
    }
    const auto vb=original.vb;
    const auto ib=original.ib;
    const uint32_t initialAllocations=dev.allocs;
    std::string error;
    // Reject malformed import data BEFORE allocating a GPU sidecar:
    // original leaf coverage and parent bounds must remain conservative.
    auto incomplete=makeAsset();
    incomplete.primitives[0].geometry.nodes[1].indexCount=6;
    PortableSource rejected;
    assert(!rejected.stage(dev,original,incomplete,&error));
    assert(dev.allocs==initialAllocations);
    auto badBounds=makeAsset();
    badBounds.primitives[0].geometry.bounds[0].min[0]=0.f;
    assert(!rejected.stage(dev,original,badBounds,&error));
    assert(dev.allocs==initialAllocations);
    PortableSource staged;
    assert(staged.stage(dev,original,asset,&error));
    assert(error.empty());
    assert(staged.validFor(original));
    assert(staged.stats().pooledIndices == mdi);
    assert(staged.stats().dedicatedIndices == !mdi);
    assert(dev.allocs==initialAllocations+(!mdi?1u:0u));
    assert(staged.indexBuffer().valid());
    if(mdi)assert(staged.indexBuffer()==ib);
    else assert(staged.indexBuffer()!=ib);
    PortablePassView fine{mat4::identity(),800,600,true,.01f};
    PortablePassView coarse{mat4::identity(),800,600,true,10000.f};
    Model renderModel;
    assert(staged.prepareModel(mat4::identity(),{coarse,fine},false,
                               renderModel,&error));
    assert(renderModel.vb==vb);
    assert(renderModel.ib==staged.indexBuffer());
    assert(renderModel.gpuFirstIndex==staged.firstIndex());
    assert(renderModel.meshes.size()==1);
    assert(renderModel.meshes[0].primitives.size()==2);
    assert(renderModel.materials.size()==original.materials.size());
    assert(staged.prepareModel(mat4::identity(),{coarse},false,
                               renderModel,&error));
    assert(renderModel.meshes[0].primitives.size()==1);
    assert(renderModel.meshes[0].primitives[0].indexCount==6);
    assert(staged.prepareModel(mat4::identity(),{},true,
                               renderModel,&error));
    assert(renderModel.meshes[0].primitives.size()==2);
    // A source Model can be reuploaded without an explicit revision bump.
    // Dedicated and pooled sidecars must refuse old VB/IB identities and
    // old page offsets, leaving the current output alias unchanged.
    const auto savedVB=original.vb;
    original.vb=gfx::BufferHandle{savedVB.id+1000};
    assert(!staged.validFor(original));
    assert(!staged.prepareModel(mat4::identity(),{fine},false,
                                renderModel,&error));
    assert(renderModel.meshes[0].primitives.size()==2);
    original.vb=savedVB;
    const uint32_t savedFirstIndex=original.gpuFirstIndex;
    original.gpuFirstIndex=savedFirstIndex+1;
    assert(!staged.validFor(original));
    original.gpuFirstIndex=savedFirstIndex;
    assert(staged.validFor(original));
    // Ownership is device-local: a foreign device's matching numeric handles
    // must not release this staged source. On device loss, fail closed and
    // leave the caller's last prepared alias unchanged for Legacy fallback.
    Recorder foreign(backend,mdi);
    staged.shutdown(foreign);
    assert(staged.validFor(original));
    assert(foreign.releases==0);
    dev.healthy_=false;
    assert(!staged.validFor(original));
    assert(!staged.prepareModel(mat4::identity(),{fine},false,
                                renderModel,&error));
    assert(renderModel.meshes[0].primitives.size()==2);
    dev.healthy_=true;
    assert(staged.validFor(original));
    // A material swap that disables the opaque source is a strict fallback:
    // rejected prepare does not clobber the last valid alias.
    original.materials[0].alpha=AlphaMode::Blend;
    assert(!staged.prepareModel(mat4::identity(),{fine},false,
                                renderModel,&error));
    assert(!error.empty());
    assert(renderModel.meshes[0].primitives.size()==2);
    original.materials[0].alpha=AlphaMode::Opaque;
    original.markMaterialChanged();
    assert(!staged.validFor(original));
    assert(!staged.prepareModel(mat4::identity(),{fine},false,
                                renderModel,&error));
    // Renderer consumes the alias and never submits original alongside it.
    dev.drawIndexed(renderModel.meshes[0].primitives[0].indexCount);
    assert(dev.draws==1);
    renderModel=Model{}; // borrow released before source/page shutdown
    staged.shutdown(dev);
    if(mdi)releaseModelGeometryPool(dev,original);
    else {dev.destroyBuffer(vb);dev.destroyBuffer(ib);}
    assert(dev.data_.empty());
}
// GeometryScene may already have legacy cluster/occluder metadata before
// staging Strata. It is NOT a second geometry source in the prepared alias:
// legacy stays intact for fallback, while Strata selects its own indices.
static void testLegacyClusteredSource(gfx::Backend backend) {
    Recorder dev(backend,false);
    Model source=makeModel();
    prepareGeometry(source);
    assert(source.geometry);
    const ModelAsset asset=makeAsset();
    gfx::BufferDesc vb{};
    vb.type=gfx::BufferType::Vertex;
    vb.size=source.vertices.size()*sizeof(ModelVertex);
    vb.data=source.vertices.data();
    source.vb=dev.createBuffer(vb);
    gfx::BufferDesc ib{};
    ib.type=gfx::BufferType::Index;
    ib.size=source.indices.size()*sizeof(uint32_t);
    ib.data=source.indices.data();
    source.ib=dev.createBuffer(ib);
    PortableSource staged;
    std::string error;
    assert(staged.stage(dev,source,asset,&error));
    assert(staged.stats().dedicatedIndices);
    Model alias;
    PortablePassView fine{mat4::identity(),640,480,true,.01f};
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(!alias.geometry);
    assert(source.geometry);
    assert(alias.vb==source.vb);
    assert(alias.meshes[0].primitives.size()==2);
    alias=Model{};
    staged.shutdown(dev);
    dev.destroyBuffer(source.vb);
    dev.destroyBuffer(source.ib);
    assert(dev.data_.empty());
}

static void testStreamedPortable(gfx::Backend backend) {
    Recorder dev(backend,false);
    Model source=makeModel();
    const auto asset=makeAsset();
    gfx::BufferDesc desc{};
    desc.type=gfx::BufferType::Vertex;
    desc.size=source.vertices.size()*sizeof(ModelVertex);
    desc.data=source.vertices.data();
    source.vb=dev.createBuffer(desc);
    desc.type=gfx::BufferType::Index;
    desc.size=source.indices.size()*sizeof(uint32_t);
    desc.data=source.indices.data();
    source.ib=dev.createBuffer(desc);
    std::string error;
    PortablePassView fine{mat4::identity(),800,600,true,.01f};
    PortableSource staged;
    // Root requires six uint32 indices = 24 bytes. Insufficient budgets
    // refuse staged Strata, retaining source Legacy geometry unchanged.
    assert(!staged.stage(dev,source,asset,&error,23));
    assert(staged.stage(dev,source,asset,&error,48));
    assert(staged.streamingEnabled());
    assert(staged.stats().residentIndexBytes==24);
    const auto rootBuffer=staged.indexBuffer();
    Model alias;
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(alias.meshes[0].primitives.size()==1);
    assert(alias.meshes[0].primitives[0].indexCount==6);
    std::vector<IndexDemand> demands;
    assert(staged.collectResidencyDemands(mat4::identity(),{fine},false,demands));
    assert(demands.size()==2);
    // Failure must NOT publish half-loaded children, or destroy root VB/IB.
    dev.failNextIndex_=true;
    assert(!staged.updateResidency(demands,&error));
    assert(staged.indexBuffer()==rootBuffer);
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(alias.meshes[0].primitives.size()==1);
    assert(staged.updateResidency(demands,&error));
    assert(staged.indexBuffer()!=rootBuffer);
    assert(staged.stats().residentIndexBytes==48);
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(alias.meshes[0].primitives.size()==2);
    assert(alias.ib==staged.indexBuffer());
    assert(alias.vb==source.vb);
    assert(staged.updateResidency({},&error));
    assert(staged.stats().residentIndexBytes==24);
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(alias.meshes[0].primitives.size()==1);
    assert(dev.data_.size()==3); // source VB+IB and exactly one active sidecar
    alias=Model{}; // renderer must finish before source/sidecar destruction
    staged.shutdown(dev);
    dev.destroyBuffer(source.vb);
    dev.destroyBuffer(source.ib);
    assert(dev.data_.empty());
}

// End-to-end CPU LOD model alias + stable Metal/Vulkan Storage/INDEX
// page arena. The recording RHI checks the consumed index offsets; a
// real hardware main/shadow/reflection parity run remains deferred.
static void testIncrementalPortable(gfx::Backend backend) {
    Recorder dev(backend,false);
    Model source=makeModel();
    const auto asset=makeAsset();
    gfx::BufferDesc desc{};
    desc.type=gfx::BufferType::Vertex;
    desc.size=source.vertices.size()*sizeof(ModelVertex);
    desc.data=source.vertices.data();
    source.vb=dev.createBuffer(desc);
    desc.type=gfx::BufferType::Index;
    desc.size=source.indices.size()*sizeof(uint32_t);
    desc.data=source.indices.data();
    source.ib=dev.createBuffer(desc);
    PortableSource staged;
    std::string error;
    assert(staged.stage(dev,source,asset,&error,48,true,24));
    assert(staged.streamingEnabled());
    assert(staged.residencyStats() && staged.residencyStats()->incremental);
    const auto indexBuffer=staged.indexBuffer();
    assert(staged.residencyStats()->arenaBytes==48);
    assert(staged.residencyStats()->activeBytes==24);
    PortablePassView fine{mat4::identity(),800,600,true,.01f};
    Model alias;
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(alias.ib==indexBuffer);
    assert(alias.meshes[0].primitives.size()==1);
    std::vector<IndexDemand> demands;
    assert(staged.collectResidencyDemands(mat4::identity(),{fine},false,demands));
    assert(staged.updateResidency(demands,&error));
    assert(staged.indexBuffer()==indexBuffer);
    assert(staged.residencyStats()->activeBytes==48);
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(alias.ib==indexBuffer && alias.vb==source.vb);
    assert(alias.meshes[0].primitives.size()==2);
    assert(alias.meshes[0].primitives[0].firstIndex==6);
    assert(alias.meshes[0].primitives[1].firstIndex==9);
    // Unknown frame serial: keep GPU-visible children resident.
    assert(staged.updateResidency({},&error));
    assert(staged.residencyStats()->activeBytes==48);
    assert(staged.residencyStats()->fenceReuseDeferrals>0);
    // Valid serial: retire children without reusing in-flight slots.
    dev.testFrameSerial_=1;
    assert(staged.updateResidency({},&error));
    assert(staged.residencyStats()->activeBytes==24);
    assert(staged.indexBuffer()==indexBuffer);
    assert(staged.prepareModel(mat4::identity(),{fine},false,alias,&error));
    assert(alias.meshes[0].primitives.size()==1);
    assert(alias.meshes[0].primitives[0].firstIndex==0);
    assert(dev.allocs==3); // source VB, source IB, one stable index arena
    alias=Model{};
    staged.shutdown(dev);
    dev.destroyBuffer(source.vb);
    dev.destroyBuffer(source.ib);
    assert(dev.data_.empty());
}

static void testStreamedTemporalDemands() {
    Recorder dev(gfx::Backend::Vulkan,false);
    Model model=makeModel();
    const auto asset=makeAsset();
    gfx::BufferDesc desc{};
    desc.type=gfx::BufferType::Vertex;
    desc.size=model.vertices.size()*sizeof(ModelVertex);
    desc.data=model.vertices.data();
    model.vb=dev.createBuffer(desc);
    desc.type=gfx::BufferType::Index;
    desc.size=model.indices.size()*sizeof(uint32_t);
    desc.data=model.indices.data();
    model.ib=dev.createBuffer(desc);
    PortableSource source;
    std::string error;
    assert(source.stage(dev,model,asset,&error,48));
    assert(source.streamingEnabled());
    const auto& b=asset.primitives[0].geometry.bounds[0];
    const float projected=geometryErrorPixels(mat4::identity(),
        {b.min[0],b.min[1],b.min[2]},
        {b.max[0],b.max[1],b.max[2]},
        asset.primitives[0].geometry.nodes[0].geometricError,
        800,600,true);
    assert(std::isfinite(projected) && projected>0);
    PortablePassView pass{mat4::identity(),800,600,true,projected*.95f};
    PortableLodHistory history;
    std::vector<IndexDemand> demands;
    assert(source.collectResidencyDemands(mat4::identity(),{pass},false,
                                          demands,&history,.2f));
    assert(demands.size()==2);
    assert(source.updateResidency(demands,&error));
    Model alias;
    assert(source.prepareModel(mat4::identity(),{pass},false,alias,
                               &error,&history,.2f));
    assert(alias.meshes[0].primitives.size()==2);
    pass.errorThresholdPixels=projected*1.08f;
    std::vector<IndexDemand> retained,unstable;
    assert(source.collectResidencyDemands(mat4::identity(),{pass},false,
                                          retained,&history,.2f));
    assert(retained.size()==2);
    assert(retained[0].node!=0 && retained[1].node!=0);
    // Unmodified CPU selection would have requested only the parent,
    // potentially evicting the children this frame still uses.
    assert(source.collectResidencyDemands(mat4::identity(),{pass},false,
                                          unstable));
    assert(unstable.size()==1 && unstable[0].node==0);
    assert(source.updateResidency(retained,&error));
    assert(source.prepareModel(mat4::identity(),{pass},false,alias,
                               &error,&history,.2f));
    assert(alias.meshes[0].primitives.size()==2);
    alias=Model{};
    source.shutdown(dev);
    dev.destroyBuffer(model.vb);
    dev.destroyBuffer(model.ib);
    assert(dev.data_.empty());
}

static void testPortableTemporalHysteresis(gfx::Backend backend) {
    Recorder dev(backend,false);
    Model original=makeModel();
    const ModelAsset asset=makeAsset();
    gfx::BufferDesc desc{};
    desc.type=gfx::BufferType::Vertex;
    desc.size=original.vertices.size()*sizeof(ModelVertex);
    desc.data=original.vertices.data();
    original.vb=dev.createBuffer(desc);
    desc.type=gfx::BufferType::Index;
    desc.size=original.indices.size()*sizeof(uint32_t);
    desc.data=original.indices.data();
    original.ib=dev.createBuffer(desc);
    std::string error;
    PortableSource staged;
    assert(staged.stage(dev,original,asset,&error));
    PortableLodHistory cameraA,cameraB;
    PortablePassView pass{mat4::identity(),800,600,true,1.f};
    const auto& bounds=asset.primitives[0].geometry.bounds[0];
    const float errorPixels=geometryErrorPixels(
        pass.viewProjection,
        {bounds.min[0],bounds.min[1],bounds.min[2]},
        {bounds.max[0],bounds.max[1],bounds.max[2]},
        asset.primitives[0].geometry.nodes[0].geometricError,
        pass.width,pass.height,pass.clipDepthZeroToOne);
    assert(std::isfinite(errorPixels) && errorPixels>0.f);
    Model alias;
    auto drawAt=[&](PortableLodHistory& state,float threshold) {
        pass.errorThresholdPixels=errorPixels*threshold;
        assert(staged.prepareModel(mat4::identity(),{pass},false,
            alias,&error,&state,.2f));
        return alias.meshes[0].primitives.size();
    };
    // Camera A remembers fine frontier when the noisy error crosses
    // the ordinary threshold but NOT the wider coarsening threshold.
    assert(drawAt(cameraA,.95f)==2);
    assert(cameraA.states.size()==1);
    assert(drawAt(cameraA,1.08f)==2);
    assert(drawAt(cameraA,.99f)==2);
    assert(drawAt(cameraA,1.30f)==1); // true coarsening threshold
    assert(drawAt(cameraA,.94f)==1); // true refinement threshold
    assert(drawAt(cameraA,.70f)==2);
    assert(staged.stats().hysteresisTransitions>=2);

    // Independent scene instance with same source and pass: cold-start
    // selects coarse despite camera A remaining fine.
    assert(drawAt(cameraB,1.08f)==1);
    assert(drawAt(cameraA,1.08f)==2);
    const auto snapshot=cameraA.states;
    const auto previousAlias=alias.meshes[0].primitives.size();

    // Mid-epoch RHI/source loss is transactional: last alias and history
    // are unchanged, so no spurious next-frame LOD oscillation.
    dev.healthy_=false;
    assert(!staged.prepareModel(mat4::identity(),{pass},false,
        alias,&error,&cameraA,.2f));
    assert(alias.meshes[0].primitives.size()==previousAlias);
    assert(cameraA.states==snapshot);
    dev.healthy_=true;

    // A different pass count (main+sun) creates independent camera slots;
    // force-finest reflection resets all retained view histories.
    assert(staged.prepareModel(mat4::identity(),{pass,pass},false,
        alias,&error,&cameraA,.2f));
    assert(cameraA.passCount==2 && cameraA.states.size()==2);
    assert(staged.prepareModel(mat4::identity(),{},true,alias,
        &error,&cameraA,.2f));
    assert(cameraA.states.empty() && cameraA.source==nullptr);
    assert(drawAt(cameraA,1.08f)==1); // reset -> true cold start

    // Old direct API remains zero-hysteresis even with a history supplied;
    // disabling the feature invalidates temporal state.
    pass.errorThresholdPixels=errorPixels*.99f;
    assert(staged.prepareModel(mat4::identity(),{pass},false,
        alias,&error,&cameraA,0.f));
    assert(alias.meshes[0].primitives.size()==2);
    assert(cameraA.states.empty());
    alias=Model{};
    staged.shutdown(dev);
    dev.destroyBuffer(original.vb);
    dev.destroyBuffer(original.ib);
    assert(dev.data_.empty());
}

static void testStreamingFrameTelemetryDelta() {
    IndexResidencyStats before{};
    before.gpuTransferBytes=120;
    before.uploadedBytes=80;
    before.uploads=3;
    before.evictions=1;
    before.deferredDemands=4;
    before.failedUploads=2;
    auto after=before;
    after.gpuTransferBytes=152; // failed transaction may still write bytes
    after.uploadedBytes=80; // no published pages
    after.uploads=3;
    after.evictions=1;
    after.deferredDemands=6;
    after.failedUploads=3;
    auto delta=indexResidencyFrameDelta(before,after);
    assert(delta.gpuTransferBytes==32 && delta.uploadedBytes==0);
    assert(delta.promotions==0 && delta.evictions==0);
    assert(delta.deferredDemands==2 && delta.failedUploads==1);
    const auto unchanged=indexResidencyFrameDelta(after,after);
    assert(unchanged.gpuTransferBytes==0 && unchanged.failedUploads==0);
    IndexResidencyStats reset{};
    delta=indexResidencyFrameDelta(after,reset);
    assert(delta.gpuTransferBytes==0 && delta.evictions==0);
    reset.gpuTransferBytes=300;
    reset.uploadedBytes=100;
    reset.uploads=5;
    reset.evictions=2;
    delta=indexResidencyFrameDelta(after,reset);
    assert(delta.gpuTransferBytes==148 && delta.uploadedBytes==20);
    assert(delta.promotions==2 && delta.evictions==1);
}

int main(){
    testStreamingFrameTelemetryDelta();
    testStreamedTemporalDemands();
    testPortableTemporalHysteresis(gfx::Backend::Metal);
    testPortableTemporalHysteresis(gfx::Backend::Vulkan);
    testPortableTemporalHysteresis(gfx::Backend::GLES3);
    testIncrementalPortable(gfx::Backend::Metal);
    testIncrementalPortable(gfx::Backend::Vulkan);
    testStreamedPortable(gfx::Backend::GLES3);
    testStreamedPortable(gfx::Backend::Vulkan);
    testLegacyClusteredSource(gfx::Backend::GLES3);
    testLegacyClusteredSource(gfx::Backend::Vulkan);
    testBackend(gfx::Backend::GLES3,false);
    testBackend(gfx::Backend::Vulkan,true);
    testBackend(gfx::Backend::Metal,true);
    testBackend(gfx::Backend::GL33,false);
    return 0;
}
