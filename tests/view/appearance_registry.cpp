// Stage 109 deferred: transactional material/graph publication must not
// break the last visible appearance or reset a stable graph clock.
#include "engine/view/AppearanceAssetRegistry.h"
#include "../AlwaysCheck.h"
#include <string>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

int main() {
    using namespace aiverse;
    using view::AppearanceAssetRegistry;

    render::Model model;
    model.materials.resize(1);
    model.materials[0].name="body";
    AppearanceAssetRegistry registry;
    AppearanceAssetRegistry::Request request;
    request.model=&model;
    request.sourceId=123;
    request.sources={"assets/materials.json","models/character.glb"};
    request.materialLibrary=Json::parse(R"({
        "base":{"roughness":0.8,"metallic":0.2},
        "paint":{"base":"base","roughness":0.4}
    })");
    request.materialOverrides={Json::string("paint")};
    request.parameters.set("speed",0.75f);

    std::string error;
    AIVERSE_TEST_CHECK(registry.publish("character",request,{}, {},&error));
    auto first=registry.find("character");
    AIVERSE_TEST_CHECK(first && first->version==1 && first->generation==0);
    AIVERSE_TEST_CHECK(first->materials[0].roughness==0.4f);
    AIVERSE_TEST_CHECK(first->materials[0].metallic==0.2f);
    AIVERSE_TEST_CHECK(!registry.dirty("character"));
    view::Object packet;
    AIVERSE_TEST_CHECK(first->packet(render::mat4{},packet));
    AIVERSE_TEST_CHECK(packet.id==123 && packet.generation==0);
    AIVERSE_TEST_CHECK(packet.draw.materials==&first->materials);
    AIVERSE_TEST_CHECK(packet.parameters.values.at("speed")==0.75f);

    AIVERSE_TEST_CHECK(registry.invalidateSources({"assets/materials.json"})==1);
    AIVERSE_TEST_CHECK(registry.dirty("character"));
    AIVERSE_TEST_CHECK(registry.find("character")==first); // old live snapshot survives
    request.materialLibrary=Json::parse(R"({
        "base":{"roughness":0.8,"metallic":0.2},
        "paint":{"base":"base","roughness":0.3}
    })");
    AIVERSE_TEST_CHECK(registry.publish("character",request,{}, {},&error));
    auto second=registry.find("character");
    AIVERSE_TEST_CHECK(second && second!=first && second->version>first->version);
    AIVERSE_TEST_CHECK(!registry.dirty("character"));
    AIVERSE_TEST_CHECK(second->materials[0].roughness==0.3f);
    AIVERSE_TEST_CHECK(first->materials[0].roughness==0.4f); // pinned snapshot immutable

    // A failed shader program cannot silently erase an already working one.
    request.materialOverrides={Json::parse(R"({
        "shaderGraph":{"nodes":[],"domain":"invalid"}
    })")};
    AIVERSE_TEST_CHECK(!registry.publish("character",request,{}, {},&error));
    AIVERSE_TEST_CHECK(!error.empty());
    AIVERSE_TEST_CHECK(registry.find("character")==second);

    // A valid external graph becomes an immutable single-writer View source.
    request.materialOverrides={Json::string("paint")};
    request.animationGraph=Json::string("graphs/character.json");
    request.graphReader=[](const std::string& file,std::string& content){
        if(file!="graphs/character.json")return false;
        content=R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})";
        return true;
    };
    AIVERSE_TEST_CHECK(registry.publish("character",request,{}, {},&error));
    auto animated=registry.find("character");
    AIVERSE_TEST_CHECK(animated && animated->graph && animated->generation!=0);
    AIVERSE_TEST_CHECK(animated->packet(render::mat4{},packet));
    AIVERSE_TEST_CHECK(packet.graph==animated->graph);
    AIVERSE_TEST_CHECK(packet.id==123 && packet.generation==animated->generation);
    AIVERSE_TEST_CHECK(packet.parameters.values.at("speed")==0.75f);

    // Material-only rebuild keeps the graph pointer and animation generation.
    request.materialLibrary=Json::parse(R"({
        "paint":{"roughness":0.23}
    })");
    AIVERSE_TEST_CHECK(registry.invalidateSources({"assets/materials.json"})==1);
    AIVERSE_TEST_CHECK(registry.publish("character",request,{}, {},&error));
    auto materialOnly=registry.find("character");
    AIVERSE_TEST_CHECK(materialOnly && materialOnly->graph==animated->graph);
    AIVERSE_TEST_CHECK(materialOnly->generation==animated->generation);
    AIVERSE_TEST_CHECK(materialOnly->version>animated->version);

    // The same graph path is recompiled after its source is invalidated.
    AIVERSE_TEST_CHECK(registry.invalidateSources({"graphs/character.json"})==1);
    AIVERSE_TEST_CHECK(registry.dirty("character"));
    AIVERSE_TEST_CHECK(registry.publish("character",request,{}, {},&error));
    auto newGraph=registry.find("character");
    AIVERSE_TEST_CHECK(newGraph->graph!=materialOnly->graph);
    AIVERSE_TEST_CHECK(newGraph->generation>materialOnly->generation);

    // A broken external graph must not displace the previous animation.
    request.graphReader=[](const std::string&,std::string& out){
        out=R"({"nodes":[{"id":"broken","kind":"unknown"}],"output":"broken"})";
        return true;
    };
    registry.invalidateSources({"graphs/character.json"});
    AIVERSE_TEST_CHECK(!registry.publish("character",request,{}, {},&error));
    AIVERSE_TEST_CHECK(registry.find("character")==newGraph);
    AIVERSE_TEST_CHECK(registry.dirty("character"));

    registry.erase("character");
    AIVERSE_TEST_CHECK(!registry.find("character"));
    AIVERSE_TEST_CHECK(!registry.dirty("character"));
    AIVERSE_TEST_CHECK(newGraph->packet(render::mat4{},packet)); // caller still pins snapshot
}
