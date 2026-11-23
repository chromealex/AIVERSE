#include "engine/render/TerraWeaveCavernsGeometryScene.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveMesh cave;cave.key={-1,-2,3};
    cave.vertices={{0,0,0,0,1,0},{1,0,0,0,1,0},{0,0,1,0,1,0}};
    cave.indices={0,1,2};
    auto model=makeCaveGeometryModel(cave);
    assert(model->vertices.size()==3);
    assert(model->indices.size()==3);
    auto manifest=caveGeometrySceneManifest(cave.key,1,99,16,100,true);
    assert(manifest["instances"].size()==1);
    const auto key=caveGeometrySourceKey(cave.key,1,99);
    assert(manifest["instances"][size_t(0)].getStr("model")==key);
    assert(manifest["strata"].getBool("enabled"));
    assert(manifest["instances"][size_t(0)]["position"].size()==3);
    assert(manifest["instances"][size_t(0)]["position"][size_t(0)].toDouble()==-16.);
    assert(manifest["instances"][size_t(0)]["position"][size_t(1)].toDouble()==-32.);
    assert(manifest["instances"][size_t(0)]["position"][size_t(2)].toDouble()==48.);
    bool invalid=false;
    try {cave.indices={0,1,4};(void)makeCaveGeometryModel(cave);}
    catch(const std::invalid_argument&){invalid=true;}
    assert(invalid);
    std::cout<<"caverns GeometryScene model/manifest contract OK\n";
}
