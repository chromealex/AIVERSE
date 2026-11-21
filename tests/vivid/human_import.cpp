#include "engine/character/vivid/HumanImport.h"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;
namespace {
void expect(bool valid,const char* message) {
    if(!valid)throw std::runtime_error(message);
}
template<class F> void reject(F fn) {
    try {fn();}catch(const std::invalid_argument&){return;}
    throw std::runtime_error("invalid VIVID human import accepted");
}
void contract() {
    const auto manifest=HumanImportManifest::fromJson(Json::parse(R"({
      "version":1,"model":"characters/head.obj","dna":"characters/skin.json",
      "morphs":"characters/face.json","scalpColliderMesh":"closed_head_proxy",
      "routes":[{"channel":"jawOpen","target":"jawOpen","gain":0.8}],
      "materials":{"skinMat":"skin"}
    })"));
    auto baked=manifest;baked.authoredHair=true;
    expect(HumanImportManifest::fromJson(baked.toJson()).authoredHair,"baked groom flag lost");
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":1,"model":"head.obj","authoredHair":1})"));});
    expect(manifest.scalpColliderMesh=="closed_head_proxy",
           "import manifest lost explicit scalp proxy name");
    reject([] {
        HumanImportManifest m;
        m.modelPath="head.obj";
        m.scalpColliderMesh="head\nproxy";
        m.validate();
    });

    {
        auto indexed=manifest;
        indexed.facialLods.push_back({"head_lod1","head","vivid-lod-1-a1.mapping.json"});
        indexed.facialLods.push_back({"head_lod2","head","vivid-lod-2-b2.mapping.json"});
        const auto restored=HumanImportManifest::fromJson(indexed.toJson());
        expect(restored.facialLods.size()==2 &&
               restored.facialLods[1].sourceMeshName=="head" &&
               restored.facialLods[0].mappingPath=="vivid-lod-1-a1.mapping.json",
               "indexed facial LOD chain did not roundtrip");
        reject([&] {
            auto bad=indexed;
            bad.facialLods[1].meshName="head_lod1";
            bad.validate();
        });
        reject([&] {
            auto bad=indexed;
            bad.facialLods[1].mappingPath="../unsafe.mapping.json";
            bad.validate();
        });
        reject([&] {
            auto bad=indexed;
            bad.facialLods.resize(9);
            bad.validate();
        });
    }
    expect(manifest.toJson()==HumanImportManifest::fromJson(manifest.toJson()).toJson(),
           "human import manifest canonical JSON lost fields");
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":1,"model":"../bad.obj"})"));});
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":1,"model":"/absolute.obj"})"));});
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":2,"model":"head.obj"})"));});
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":1,"model":"head.obj","unknown":0})"));});
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":1,"model":"head.obj","scalpColliderMesh":42})"));});
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":1,"model":"head.obj","routes":[{"channel":"nonsense","target":"jawOpen"}]})"));});
    reject([]{HumanImportManifest::fromJson(Json::parse(R"({"version":1,"model":"head.obj","materials":{"a":"unknown"}})"));});

    const auto morphs=Json::parse(R"({
       "version":1,"vertices":3,"targets":[
         {"name":"jawOpen","deltas":[{"vertex":0,"position":[0.1,-0.2,0]}]},
         {"name":"brow","deltas":[{"vertex":2,"normal":[0,0.1,0]}]}
       ]
    })");
    const auto csr=parseHumanMorphs(morphs,3);
    expect(csr.targets()==2 && csr.vertices()==3 && csr.deltas()==2,
           "human VIVID sparse morph source did not compile");
    reject([&]{parseHumanMorphs(morphs,4);});
    reject([]{parseHumanMorphs(Json::parse(R"({"version":1,"vertices":2,"targets":[{"name":"x","deltas":[{"vertex":2,"position":[0,0,1]}]}]})"),2);});
    reject([]{parseHumanMorphs(Json::parse(R"({"version":1,"vertices":2,"targets":[{"name":"x","deltas":[{"vertex":0,"position":[999,0,0]}]}]})"),2);});

    const std::map<std::string,std::string> files{
      {"characters/head.obj",
       "o head\nv -0.3 0 0\nv 0.3 0 0\nv 0 0.6 0\n"
       "vn 0 0 1\nf 1//1 2//1 3//1\n"},
      {"characters/skin.json",HumanDNA{}.toJson().dump(-1)},
      {"characters/face.json",morphs.dump(-1)}
    };
    const render::FileReader reader=[&](const std::string& file,std::string& data) {
        const auto it=files.find(file);
        if(it==files.end())return false;
        data=it->second;return true;
    };
    HumanImportManifest source=manifest;
    source.materials.clear();
    source.routes.clear();
    source.routes.push_back({Channel::JawOpen,"jawOpen",1.0f});
    auto imported=importHumanAsset(source,reader);
    expect(imported.model && imported.model->vertices.size()>=3 &&
           imported.model->findNode("head")>=0 && imported.rig.valid() &&
           imported.facialShapes.vertices()==imported.model->vertices.size() &&
           imported.facialBinding.routes()==1,
           "OBJ head + external VIVID sparse shapes + DNA rig import failed");
    const auto& p=imported.model->meshes[0].primitives[0];
    expect(p.vertexCount>=3 && p.firstVertex<imported.model->vertices.size(),
           "OBJ VIVID primitive was not given stable model-relative vertex metadata");
    HumanImportManifest morphLess=source;
    morphLess.morphPath.clear();morphLess.routes.clear();
    auto rigOnly=importHumanAsset(morphLess,reader);
    expect(rigOnly.facialShapes.targets()==0 && rigOnly.rig.valid(),
           "morph-free human OBJ import failed");
}
}
int main() {
    try {contract();std::cout<<"VIVID open human importer PASS\n";return 0;}
    catch(const std::exception& e) {
        std::cerr<<"VIVID human import FAIL: "<<e.what()<<"\n";return 1;
    }
}
