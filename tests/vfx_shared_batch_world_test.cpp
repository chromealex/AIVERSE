// SpectraFX: two independent effect instances MUST share a CPU draw call
// when their GPU texture, blend state and shader pipeline are compatible.
// This test records the actual RHI draw submissions (not just effect counts).
#include "engine/fx/SpectraFX.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace sp = aiverse::spectra;

static void check(bool ok, const char* text) {
    if(!ok) throw std::runtime_error(text);
}

class BatchRecordingDevice final : public gfx::Device {
public:
    gfx::DeviceInfo data{gfx::Backend::GL33,false,false,"SpectraFX batch mock"};
    std::string error;
    uint32_t next=1;
    int indexedDraws=0, otherDraws=0;
    uint32_t submittedIndices=0;
    bool inPass=false, invalidDraw=false;
    const gfx::DeviceInfo& info() const override { return data; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override { return {next++}; }
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t=0) override {}
    void destroyBuffer(gfx::BufferHandle) override {}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override { return {next++}; }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override { return {next++}; }
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override { return {next++}; }
    void destroyPipeline(gfx::PipelineHandle) override {}
    const std::string& lastError() const override { return error; }
    void beginFrame(int w,int h) override { fbW_=w;fbH_=h; }
    void endFrame() override {}
    void beginPass(const gfx::PassDesc&) override { inPass=true; }
    void endPass() override { inPass=false; }
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setIndexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t=0) override {
        ++otherDraws;
        if(!inPass)invalidDraw=true;
    }
    void drawIndexed(uint32_t n,uint32_t=0,int32_t=0,uint32_t=1) override {
        ++indexedDraws;
        submittedIndices+=n;
        if(!inPass)invalidDraw=true;
    }
    void reset() { indexedDraws=otherDraws=0;submittedIndices=0;invalidDraw=false; }
};

static fx::EffectDef effect(const std::string& texture, int burst,
                            fx::FxBlend blend=fx::FxBlend::Additive) {
    fx::EffectDef result;
    fx::EmitterDef e;
    e.texture=texture;
    e.blend=blend;
    e.burst=burst;
    e.life={2.f,2.f};
    e.speed={0.f,0.f};
    e.size={0.3f,0.3f};
    e.spread=0;
    e.gravity={0,0,0};
    result.emitters.push_back(e);
    return result;
}

int main() {
    BatchRecordingDevice gfx;
    sp::BatchWorld world;
    check(world.init(&gfx,fx::FxQuality::High),"cannot initialize shared FX world");
    const auto a=effect("builtin:soft",5);
    const auto b=effect("builtin:spark",7); // different UV region of SAME RHI atlas
    const auto alpha=effect("builtin:soft",3,fx::FxBlend::Alpha);

    const int idA=world.defineEffect(a);
    const int idB=world.defineEffect(b);
    check(idA>=0 && idB>=0 && idA!=idB,"different definitions merged by asset identity");
    check(world.defineEffect(a)==idA,"equivalent shared effect was not deduplicated");

    sp::GraphRuntime one,two,alphaInstance;
    check(one.initEffect(world,a),"first GraphRuntime shared registration");
    check(two.initEffect(world,b),"second GraphRuntime shared registration");
    check(one.usesSharedBatchWorld()&&two.usesSharedBatchWorld(),
          "GraphRuntime created its own particle renderer");
    check(one.backend()==sp::ComputeBackend::Cpu,"shared world must not silently enable GPU");
    sp::GraphRuntimeOptions mandatoryGpu;
    mandatoryGpu.preference=sp::ComputePreference::RequireGpu;
    sp::GraphRuntime rejected;
    check(!rejected.initEffect(world,a,mandatoryGpu),"shared batching silently downgraded RequireGpu");
    check(!rejected.reason().empty(),"missing RequireGpu rejection reason");

    render::RTSCamera camera{{0,0,0},8.f,0.5f,0.5f};
    gfx.beginFrame(1280,720);
    world.setView(camera,camera.proj(1280.f/720.f,false)*camera.view(),1280,720);
    check(one.burstAt({0,0,0}),"first effect instance failed to emit");
    check(two.burstAt({0.2f,0,0}),"second effect instance failed to emit");
    world.update(0.05f); // EXACTLY ONCE for the entire shared particle world
    std::vector<fx::FxParticleInfo> snapshot;
    world.particles().snapshot(snapshot);
    check(snapshot.size()==12,"expected 5+7 particles across two different effects");
    const float firstAge=snapshot[0].age;
    // Independent runtimes must NOT tick or draw the whole shared world.
    check(one.step(0.5f)&&two.step(0.5f),"shared source no-op step failed");
    world.particles().snapshot(snapshot);
    check(std::abs(snapshot[0].age-firstAge)<0.00001f,
          "per-instance step accidentally advanced the shared world");

    one.draw();
    two.draw();
    check(gfx.indexedDraws==0,"GraphRuntime submitted duplicate draws for shared world");
    gfx.beginPass(gfx::PassDesc{});
    world.draw();
    gfx.endPass();
    check(!gfx.invalidDraw,"invalid RHI pass order");
    check(gfx.indexedDraws==1 && gfx.otherDraws==0,
          "same RHI texture/blend across different effect instances must batch into ONE draw");
    check(gfx.submittedIndices==72,"shared batch lost sprite quad indices");
    check(world.stats().batches==1 && world.stats().drawCalls==1,
          "particle stats do not reflect cross-effect draw batching");

    // Same texture but different alpha blend is NOT safely compatible.
    check(alphaInstance.initEffect(world,alpha),"alpha effect shared registration");
    check(alphaInstance.burstAt({-0.2f,0,0}),"alpha instance failed to emit");
    world.update(0.05f);
    gfx.reset();
    gfx.beginPass(gfx::PassDesc{});
    world.draw();
    gfx.endPass();
    check(gfx.indexedDraws==2 && world.stats().drawCalls==2,
          "different blend state must create a separate draw call");

    // Removing one graph's handles never wipes unrelated shared particles.
    one.clear();
    world.particles().snapshot(snapshot);
    check(snapshot.size()==10,"source-scoped clear removed another graph's particles");
    two.clear();
    world.particles().snapshot(snapshot);
    check(snapshot.size()==3,"source-scoped clear must preserve alpha source");
    alphaInstance.clear();
    world.particles().snapshot(snapshot);
    check(snapshot.empty(),"all source-scoped kills should empty shared world");

    // The separate graph frontend also registers into the same batch world.
    const Json graph=Json::parse(R"JSON({
        "version":1,"nodes":[
            {"id":"spawn","type":"Spawn","properties":{"burst":4}},
            {"id":"init","type":"Initialize","properties":{"life":1,"speed":0,"size":0.2}},
            {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:soft"}}
        ],
        "links":[{"from":"spawn","to":"init"},{"from":"init","to":"out"}],
        "outputs":["out"]
    })JSON");
    const int graphId=world.defineGraph(graph);
    check(graphId>=0 && world.defineGraph(graph)==graphId,
          "graph definitions were not cached across independent sources");
    Json malformed=graph;
    malformed.set("version",Json::number(int64_t(3)));
    check(world.defineGraph(malformed)<0 && !world.errors().empty(),
          "invalid graph must not be registered as a shared effect");

    sp::GraphRuntime fromGraph;
    check(fromGraph.initGraph(world,graph),"versioned graph could not join shared FX world");
    check(fromGraph.burstAt({0,0,0}),"shared graph emission failed");
    world.update(0.05f);
    world.particles().snapshot(snapshot);
    check(snapshot.size()==4,"shared compiled graph lost particles");
    fromGraph.shutdown();
    world.particles().snapshot(snapshot);
    check(snapshot.empty(),"graph shutdown left orphaned shared particles");
    world.shutdown();

    std::cout<<"PASS SpectraFX: cross-instance same-atlas batching, blend isolation, "
                "shared tick/draw and source-scoped cleanup\n";
}
