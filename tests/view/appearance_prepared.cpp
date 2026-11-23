// Stage 111 deferred: host-resolved appearance snapshots are published
// without replaying texture callbacks or building another animation clock.
#include "engine/view/AppearanceAssetRegistry.h"
#include "../AlwaysCheck.h"
#include "../cloth/FakeDevice.h"
#include <string>
#include <memory>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

struct MaterialFixtureDevice : FakeDevice {
    int allocationsBeforeFailure=-1;
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d) override {
        if(allocationsBeforeFailure==0)return {};
        if(allocationsBeforeFailure>0)--allocationsBeforeFailure;
        return FakeDevice::createTexture(d);
    }
    bool waitForResourceIdle() override{return true;}
};
int main() {
    using namespace aiverse;
    render::Model model;
    model.path="models/unit.glb";
    model.materials.resize(1);
    model.materials[0].name="Body";
    auto graph=render::AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));

    view::AppearanceAssetRegistry registry;
    view::AppearanceAssetRegistry::Asset staged;
    staged.model=&model;
    staged.materials=model.materials;
    staged.materials[0].roughness=0.3f;
    staged.graph=graph;
    staged.sourceId=42;
    staged.graphDefinition="bind graph";
    staged.sources={"materials.json",model.path};

    std::string error;
    AIVERSE_TEST_CHECK(registry.publishPrepared("character",staged,&error));
    auto first=registry.find("character");
    AIVERSE_TEST_CHECK(first && first->version==1 && first->generation>0);
    AIVERSE_TEST_CHECK(first->materials[0].roughness==0.3f);
    AIVERSE_TEST_CHECK(first->graph==graph);
    view::Object packet;
    AIVERSE_TEST_CHECK(first->packet(render::mat4{},packet));
    AIVERSE_TEST_CHECK(packet.graph==graph && packet.draw.materials==&first->materials);

    // A material-only publication pins old materials while retaining
    // the exact compiled graph pointer/generation of the active player.
    staged.materials[0].roughness=0.6f;
    AIVERSE_TEST_CHECK(registry.invalidateSources({"materials.json"})==1);
    AIVERSE_TEST_CHECK(registry.publishPrepared("character",staged,&error));
    auto next=registry.find("character");
    AIVERSE_TEST_CHECK(next && next!=first && next->version>first->version);
    AIVERSE_TEST_CHECK(next->generation==first->generation && next->graph==first->graph);
    AIVERSE_TEST_CHECK(first->materials[0].roughness==0.3f);
    AIVERSE_TEST_CHECK(next->materials[0].roughness==0.6f);

    // Reject an invalid prepared node-map without modifying a working
    // appearance, dirty status or animation generation.
    AIVERSE_TEST_CHECK(registry.invalidateSources({model.path})==1);
    const auto good=registry.find("character");
    staged.nodeMaterial={500};
    AIVERSE_TEST_CHECK(!registry.publishPrepared("character",staged,&error));
    AIVERSE_TEST_CHECK(!error.empty());
    AIVERSE_TEST_CHECK(registry.dirty("character"));
    AIVERSE_TEST_CHECK(registry.find("character")==good);
    staged.nodeMaterial.clear();

    auto other=render::AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind2","kind":"bind"}],"output":"bind2"})"));
    staged.graph=other;
    AIVERSE_TEST_CHECK(registry.publishPrepared("character",staged,&error));
    auto changed=registry.find("character");
    AIVERSE_TEST_CHECK(changed->graph==other && changed->generation>good->generation);
    AIVERSE_TEST_CHECK(good->graph==graph);
    // Runtime publication appends only validated procedural images, pins
    // the previous snapshot, and preserves animation/model generations.
    MaterialFixtureDevice device;
    render::RenderDeviceLifetime epoch(device);
    render::ModelImageRollbackPool rollback;
    render::texture_graph::Program program;program.size=8;
    program.outputs={{"ao",0},{"color",1}};
    std::vector<render::Image> images(2);
    for(auto& image:images){image.width=image.height=8;image.rgba.assign(8*8*4,255);}
    const auto revision=model.materialRevision;
    auto publish=[&](uint64_t version,std::string_view generation){
        return registry.publishTextureGraphMaterial("character",version,
            changed->modelPublicationGeneration,0,model,device,epoch.lease(),
            rollback,program,images,generation,&error);
    };
    AIVERSE_TEST_CHECK(!publish(good->version,"stale"));
    AIVERSE_TEST_CHECK(model.images.empty()&&device.textures.empty());
    device.allocationsBeforeFailure=1;
    AIVERSE_TEST_CHECK(!publish(changed->version,"partial"));
    AIVERSE_TEST_CHECK(registry.find("character")==changed&&model.images.empty());
    AIVERSE_TEST_CHECK(rollback.pendingImages()==1&&device.textures.size()==1);
    AIVERSE_TEST_CHECK(rollback.drain(device)&&device.textures.empty());
    device.allocationsBeforeFailure=-1;
    AIVERSE_TEST_CHECK(publish(changed->version,"valid"));
    auto bound=registry.find("character");
    AIVERSE_TEST_CHECK(bound->version>changed->version&&bound->graph==changed->graph);
    AIVERSE_TEST_CHECK(bound->generation==changed->generation&&bound->modelPublicationGeneration==changed->modelPublicationGeneration);
    AIVERSE_TEST_CHECK(model.materialRevision!=revision&&model.images.size()==2);
    AIVERSE_TEST_CHECK(bound->materials[0].occlusionTex==0&&bound->materials[0].baseTex==1);
    AIVERSE_TEST_CHECK(changed->materials[0].baseTex==-1);
    AIVERSE_TEST_CHECK(!publish(bound->version,"valid"));
    AIVERSE_TEST_CHECK(registry.find("character")==bound&&model.images.size()==2);
    for(size_t i=2;i<view::AppearanceAssetRegistry::maxProceduralImages;i+=2){
        AIVERSE_TEST_CHECK(publish(registry.find("character")->version,"gen_"+std::to_string(i)));
    }
    AIVERSE_TEST_CHECK(!publish(registry.find("character")->version,"over_budget"));
    AIVERSE_TEST_CHECK(model.images.size()==view::AppearanceAssetRegistry::maxProceduralImages);
    registry.clear();
    AIVERSE_TEST_CHECK(!registry.find("character"));
    // Caller-pinned snapshot remains renderable until the host detaches it
    // and then releases model GPU/CPU storage.
    AIVERSE_TEST_CHECK(changed->packet(render::mat4{},packet));
    packet={};bound.reset();
    render::releaseModel(device,model);
    AIVERSE_TEST_CHECK(device.textures.empty());
}
