// STRATA debug visualisation regression: selection capture is transactional,
// overlay emission is bounded and colours/routes are deterministic. Uses a
// recording RHI and a recording sink; no GPU or window is required.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataPortableRender.h"
#include "engine/render/StrataDebugView.h"
#include "engine/render/Geometry.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
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

struct Sink final : DebugSink {
    struct Line { float3 a, b; uint32_t c; };
    struct Tri { float3 a, b, c; uint32_t col; };
    std::vector<Line> lines;
    std::vector<Tri> tris;
    void line(float3 a, float3 b, uint32_t c) override { lines.push_back({a, b, c}); }
    void triangle(float3 a, float3 b, float3 c, uint32_t col) override {
        tris.push_back({a, b, c, col});
    }
};

static void testSelectionCapture() {
    Recorder dev(gfx::Backend::Vulkan, true);
    Model original = makeModel();
    DebugGeometry geometry;
    assert(captureDebugGeometry(original, geometry));
    assert(geometry.positions.size() == 4 && geometry.bytes() == 4 * sizeof(float3));
    const auto asset = makeAsset();
    assert(uploadModelToGeometryPool(dev, original));
    DebugGeometry released;
    assert(original.vertices.empty() || captureDebugGeometry(original, released));
    PortableSource staged;
    std::string error;
    assert(staged.stage(dev, original, asset, &error));
    PortablePassView fine{mat4::identity(), 800, 600, true, .01f};
    PortablePassView coarse{mat4::identity(), 800, 600, true, 10000.f};
    Model alias;
    // Default OFF: nothing recorded.
    assert(staged.prepareModel(mat4::identity(), {fine}, false, alias, &error));
    assert(staged.debugSelection().empty());
    staged.setDebugCaptureEnabled(true);
    assert(staged.prepareModel(mat4::identity(), {fine}, false, alias, &error));
    assert(staged.debugSelection().size() == alias.meshes[0].primitives.size());
    assert(staged.debugSelection().size() == 2);
    std::set<uint32_t> nodes;
    for (const auto& pick : staged.debugSelection()) {
        assert(pick.primitive == 0);
        nodes.insert(pick.node);
    }
    assert(nodes == (std::set<uint32_t>{1, 2}));
    assert(staged.prepareModel(mat4::identity(), {coarse}, false, alias, &error));
    assert(staged.debugSelection().size() == 1 && staged.debugSelection()[0].node == 0);
    // A failed preparation keeps the last published frontier, like the alias.
    PortablePassView bad = fine;
    bad.width = 0;
    assert(!staged.prepareModel(mat4::identity(), {bad}, false, alias, &error));
    assert(staged.debugSelection().size() == 1 && staged.debugSelection()[0].node == 0);
    staged.setDebugCaptureEnabled(false);
    assert(staged.debugSelection().empty());
    staged.shutdown(dev);
}

static void testEmission() {
    Model source = makeModel();
    DebugGeometry geometry;
    assert(captureDebugGeometry(source, geometry));
    const auto asset = makeAsset();
    const std::vector<SelectedCluster> leaves{{0, 1}, {0, 2}};
    DebugViewOptions o;
    assert(validDebugViewOptions(o));
    o.enabled = true;
    // Disabled options emit nothing.
    {
        Sink sink; DebugDrawStats st;
        DebugViewOptions off = o; off.enabled = false;
        emitSelectedClusters(sink, asset, source, &geometry, mat4::identity(), leaves, off, st);
        assert(sink.lines.empty() && st.clusters == 0);
    }
    // Bounds only: 12 edges per cluster, in world space.
    {
        Sink sink; DebugDrawStats st;
        const mat4 world = mat4::translate({10, 0, 0});
        emitSelectedClusters(sink, asset, source, nullptr, world, leaves, o, st);
        assert(st.clusters == 2 && sink.lines.size() == 24 && st.lines == 24);
        assert(sink.tris.empty() && st.wireframeUnavailable == 0);
        for (const auto& l : sink.lines) assert(l.a.x >= 9.4f && l.a.x <= 10.6f);
        // Neighbouring clusters get different colours in Cluster mode.
        assert(sink.lines[0].c != sink.lines[12].c);
    }
    // Wireframe + fill with captured positions: one triangle per leaf.
    {
        Sink sink; DebugDrawStats st;
        DebugViewOptions w = o;
        w.clusterBounds = false; w.clusterWireframe = true; w.clusterFill = true;
        emitSelectedClusters(sink, asset, source, &geometry, mat4::identity(), leaves, w, st);
        assert(sink.tris.size() == 2 && sink.lines.size() == 6);
        assert(((sink.tris[0].col >> 24) & 0xff) == uint32_t(std::lround(.35f * 255.f)));
        assert(((sink.lines[0].c >> 24) & 0xff) == 255);
        // Without positions the clusters are counted, never faked.
        Sink none; DebugDrawStats missing;
        emitSelectedClusters(none, asset, source, nullptr, mat4::identity(), leaves, w, missing);
        assert(none.lines.empty() && none.tris.empty() && missing.wireframeUnavailable == 2);
        // Archived (discarded) CPU indices behave the same way.
        auto archived = makeAsset();
        archived.primitives[0].geometry.discardCpuIndexPayload();
        Sink a; DebugDrawStats ast;
        emitSelectedClusters(a, archived, source, &geometry, mat4::identity(), leaves, w, ast);
        assert(a.tris.empty() && ast.wireframeUnavailable == 2);
    }
    // Caps truncate deterministically.
    {
        Sink sink; DebugDrawStats st;
        DebugViewOptions capped = o; capped.maxLines = 5;
        emitSelectedClusters(sink, asset, source, nullptr, mat4::identity(), leaves, capped, st);
        assert(sink.lines.size() == 5 && st.lines == 5 && st.truncated == 19);
    }
    // Out-of-range picks are ignored.
    {
        Sink sink; DebugDrawStats st;
        emitSelectedClusters(sink, asset, source, &geometry, mat4::identity(),
                             {{7, 0}, {0, 99}}, o, st);
        assert(sink.lines.empty() && st.clusters == 0);
    }
}

static void testColours() {
    // LOD depth: root is red, deepest is blue.
    const uint32_t root = debugClusterColor(DebugColorMode::LodDepth, 0, 0, 0, 3, 0, 0);
    const uint32_t leaf = debugClusterColor(DebugColorMode::LodDepth, 0, 9, 3, 3, 0, 0);
    assert((root & 0xff) > ((root >> 16) & 0xff));
    assert(((leaf >> 16) & 0xff) > (leaf & 0xff));
    // Error: larger error is "hotter" (more red) than a small error.
    const uint32_t hot = debugClusterColor(DebugColorMode::Error, 0, 0, 0, 0, 1.f, 1.f);
    const uint32_t cold = debugClusterColor(DebugColorMode::Error, 0, 0, 0, 0, .0001f, 1.f);
    assert((hot & 0xff) > ((hot >> 16) & 0xff));
    assert(((cold >> 16) & 0xff) > (cold & 0xff));
    // Deterministic and alpha-preserving.
    assert(debugClusterColor(DebugColorMode::Cluster, 1, 2, 0, 0, 0, 0) ==
           debugClusterColor(DebugColorMode::Cluster, 1, 2, 5, 9, 3, 4));
    assert(debugClusterColor(DebugColorMode::Material, 0, 1, 0, 0, 0, 0) ==
           debugClusterColor(DebugColorMode::Material, 0, 2, 0, 0, 0, 0));
    std::set<uint32_t> routes{debugRouteColor(DebugRoute::StrataPortable),
                              debugRouteColor(DebugRoute::StrataVulkanGpu),
                              debugRouteColor(DebugRoute::LegacyFallback),
                              debugRouteColor(DebugRoute::Legacy)};
    assert(routes.size() == 4);
    DebugViewOptions bad;
    bad.fillAlpha = 2.f;
    assert(!validDebugViewOptions(bad));
    bad.fillAlpha = NAN;
    assert(!validDebugViewOptions(bad));
}

int main() {
    testColours();
    testEmission();
    testSelectionCapture();
    return 0;
}
