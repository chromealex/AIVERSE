// Reusable CPU texture authoring templates, not pre-baked assets.
#pragma once
#include "TextureGraph.h"
namespace aiverse::render::texture_graph {
inline Json templates(){
    struct Recipe {const char* name;const char* pattern;double metallic,lo,hi,strength;};
    const Recipe recipes[]={{"metal","metal",1,.2,.65,1},{"stone","noise",0,.55,.95,3},{"wood","wood",0,.4,.8,2},{"brick","brick",0,.6,.95,4},{"earth","noise",0,.7,1,2},{"fabric","checker",0,.65,.95,1}};
    Json out=Json::array();
    for(const auto& recipe:recipes){
        auto g=Json::parse(R"({"version":1,"size":256,"nodes":[{"id":"color","op":"pattern","inputs":[],"properties":{}},{"id":"height","op":"fbm","inputs":[],"properties":{"period":16,"seed":1,"octaves":4}},{"id":"normal","op":"normal","inputs":["height"],"properties":{}},{"id":"rough","op":"remap","inputs":["height"],"properties":{}},{"id":"metal","op":"constant","inputs":[],"properties":{}},{"id":"packed","op":"metallicRoughness","inputs":["rough","metal"]},{"id":"ao","op":"constant","inputs":[],"properties":{"value":1}}],"outputs":{"color":"color","height":"height","normal":"normal","roughness":"rough","metallic":"metal","metallicRoughness":"packed","ao":"ao"}})");
        auto& nodes=g.member("nodes");nodes.at(0).member("properties").set("pattern",Json::string(recipe.pattern));
        nodes.at(2).member("properties").set("strength",Json::number(recipe.strength));
        nodes.at(3).member("properties").set("low",Json::number(recipe.lo));nodes.at(3).member("properties").set("high",Json::number(recipe.hi));
        nodes.at(4).member("properties").set("value",Json::number(recipe.metallic));
        // Brick relief tracks the mortar mask; other patterns use independent microrelief.
        if(std::string(recipe.name)=="brick"){nodes.at(1).set("op",Json::string("patternMask"));nodes.at(1).set("properties",Json::parse(R"({"pattern":"brick","scale":8,"seed":1})"));nodes.at(0).member("properties").set("scale",Json::number(int64_t(8)));}
        compile(g);Json row=Json::object();row.set("name",Json::string(recipe.name));row.set("graph",g);out.push(row);
    }
    return out;
}
} // namespace aiverse::render::texture_graph
