// Stage 111 deferred: host-resolved appearance snapshots are published
// without replaying texture callbacks or building another animation clock.
#include "engine/view/AppearanceAssetRegistry.h"
#include "../AlwaysCheck.h"
#include <string>
#include <memory>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

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
    registry.clear();
    AIVERSE_TEST_CHECK(!registry.find("character"));
    // Caller-pinned snapshot remains renderable until the host detaches it
    // and then releases model GPU/CPU storage.
    AIVERSE_TEST_CHECK(changed->packet(render::mat4{},packet));
}
