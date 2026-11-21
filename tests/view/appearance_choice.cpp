// Stage 110 deferred: authored FPS/RTS material layers and node variants
// use the same genre-independent CPU policy and transaction builder.
#include "engine/view/AppearanceAssetRegistry.h"
#include "../AlwaysCheck.h"
#include <string>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

int main() {
    using namespace aiverse;
    render::Model model;
    model.path="models/unit.glb";
    model.materials.resize(2);
    model.materials[0].name="Body";
    model.materials[1].name="Armor";
    model.nodes.resize(3);
    model.nodes[0].name="Root";
    model.nodes[0].children={1};
    model.nodes[1].name="Arm";
    model.nodes[1].mesh=0;
    model.nodes[1].children={2};
    model.nodes[2].name="ArmPlate";
    model.nodes[2].mesh=0;

    const Json rules=Json::parse(R"({
        "materials":{
            "*":{"roughness":0.8},
            "Armor":{"roughness":0.3,"metallic":0.5}
        },
        "assign":{"Arm*":"glow","Miss*":{"roughness":0.2}}
    })");
    const Json library=Json::parse(R"({
        "glow":{"emissive":[0.2,0.4,0.6],"emissiveStrength":3}
    })");
    std::vector<std::string> warnings;
    auto warn=[&](const std::string& msg){warnings.push_back(msg);};
    auto choice=view::AppearanceChoiceAuthoring::resolve(model,rules,warn);
    AIVERSE_TEST_CHECK(choice.materialLayers.size()==2);
    AIVERSE_TEST_CHECK(choice.materialLayers[0].size()==1);
    AIVERSE_TEST_CHECK(choice.materialLayers[1].size()==2);
    AIVERSE_TEST_CHECK(choice.extraMaterials.size()==1);
    AIVERSE_TEST_CHECK(choice.extraMaterials[0].name=="glow");
    AIVERSE_TEST_CHECK(choice.nodeMaterial.size()==3);
    AIVERSE_TEST_CHECK(choice.nodeMaterial[0]==-1);
    AIVERSE_TEST_CHECK(choice.nodeMaterial[1]==2 && choice.nodeMaterial[2]==2);
    AIVERSE_TEST_CHECK(choice.variant==-1);
    AIVERSE_TEST_CHECK(warnings.size()==1); // unmatched authored node pattern

    view::AppearanceAssetRegistry assets;
    view::AppearanceAssetRegistry::Request request;
    request.model=&model;
    request.appearance=rules;
    request.materialLibrary=library;
    request.sources={"assets/looks.json"};
    std::string error;
    AIVERSE_TEST_CHECK(assets.publish("unit",request,{},warn,&error));
    const auto first=assets.find("unit");
    AIVERSE_TEST_CHECK(first && first->materials.size()==3);
    AIVERSE_TEST_CHECK(first->materials[0].roughness==0.8f);
    AIVERSE_TEST_CHECK(first->materials[1].roughness==0.3f);
    AIVERSE_TEST_CHECK(first->materials[1].metallic==0.5f);
    AIVERSE_TEST_CHECK(first->materials[2].emissiveStrength==3.f);
    AIVERSE_TEST_CHECK(first->nodeMaterial[1]==2 && first->nodeMaterial[2]==2);
    AIVERSE_TEST_CHECK(first->generation==0);

    view::Object packet;
    AIVERSE_TEST_CHECK(first->packet(render::mat4{},packet));
    AIVERSE_TEST_CHECK(packet.draw.materials==&first->materials);
    AIVERSE_TEST_CHECK(packet.draw.nodeMaterial==&first->nodeMaterial);

    // Unchanged graph inputs are allowed to keep playback generation across
    // a material-only update; Model reimports invalidate source identity.
    request.animationGraph=Json::parse(R"({
        "nodes":[{"id":"bind","kind":"bind"}],
        "output":"bind"
    })");
    request.sourceId=17;
    AIVERSE_TEST_CHECK(assets.publish("unit",request,{},warn,&error));
    const auto animated=assets.find("unit");
    AIVERSE_TEST_CHECK(animated && animated->graph && animated->generation>0);
    const auto originalGeneration=animated->generation;
    AIVERSE_TEST_CHECK(assets.invalidateSources({"assets/looks.json"})==1);
    AIVERSE_TEST_CHECK(assets.publish("unit",request,{},warn,&error));
    AIVERSE_TEST_CHECK(assets.find("unit")->generation==originalGeneration);
    request.modelRevision=1; // same Model address, changed underlying import
    AIVERSE_TEST_CHECK(assets.publish("unit",request,{},warn,&error));
    AIVERSE_TEST_CHECK(assets.find("unit")->generation>originalGeneration);
    const auto gen=assets.find("unit")->generation;
    AIVERSE_TEST_CHECK(assets.invalidateSources({"models/unit.glb"})==1);
    AIVERSE_TEST_CHECK(assets.publish("unit",request,{},warn,&error));
    AIVERSE_TEST_CHECK(assets.find("unit")->generation>gen);
    AIVERSE_TEST_CHECK(assets.find("unit")->model==&model);
}
