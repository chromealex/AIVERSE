#include "engine/world/TerraWeaveCavernsVolumeCollision.h"
#include "engine/world/TerraWeaveCavernsAuthoredNav.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace aiverse::terraweave::caverns;
static CaveMesh threeSurfaceMesh() {
    CaveMesh m;m.key={-1,-2,0};
    // Three nondegenerate triangles with independent winding: a cave floor,
    // a vertical wall and a ceiling. Local coordinates are in metres.
    m.vertices={
        {0,0,0,0,1,0},{2,0,0,0,1,0},{0,0,2,0,1,0}, // floor
        {0,0,0,1,0,0},{0,2,0,1,0,0},{0,0,2,1,0,0}, // wall
        {0,3,0,0,-1,0},{0,3,2,0,-1,0},{2,3,0,0,-1,0} // ceiling
    };
    m.indices={0,1,2,3,4,5,6,7,8};
    return m;
}
int main(){
    const auto mesh=threeSurfaceMesh();
    const auto payload=prepareCaveVolumeCollision(mesh,4,100,42,17);
    assert(payload.revision==42);
    assert(payload.triangles.size()==3);
    assert(payload.body.indices.size()==9);
    assert(payload.body.vertices.size()==8);
    assert(payload.body.position.x==-4.0f);
    assert(payload.body.position.y==-8.0f);
    assert(payload.body.position.z==0.0f);
    assert(payload.body.userData==17);
    // The first triangle's vertices preserve absolute world coordinates.
    assert(payload.triangles[0].a.x.toDouble()==-4.0);
    assert(payload.triangles[0].a.y.toDouble()==-8.0);
    auto malformed=mesh;malformed.indices[0]=999999;
    bool invalid=false;
    try{(void)prepareCaveVolumeCollision(malformed,4,100,43);}
    catch(const std::invalid_argument&){invalid=true;}
    assert(invalid);
    malformed=mesh;malformed.indices[1]=malformed.indices[0];
    invalid=false;
    try{(void)prepareCaveVolumeCollision(malformed,4,100,43);}
    catch(const std::invalid_argument&){invalid=true;}
    assert(invalid);
    bool budget=false;
    try{(void)prepareCaveVolumeCollision(mesh,4,100,43,0,{2,65535});}
    catch(const std::invalid_argument&){budget=true;}
    assert(budget);

    auto carve=std::make_shared<CaveVolumeSnapshot>();
    carve->settings.chamberChance=0;
    carve->settings.tunnelChance=0;
    carve->settings.minYcm=-1000;
    carve->settings.maxYcm=-100;
    carve->stamps.push_back({1,{50,-500,50},350,CaveStampMode::Carve});
    canonicalizeCaveSnapshot(*carve);
    CaveNavConfig navConfig;
    navConfig.width=1;navConfig.depth=1;
    navConfig.originXcm=0;navConfig.originZcm=0;
    navConfig.columnCm=100;navConfig.minYcm=-950;navConfig.maxYcm=-100;
    navConfig.verticalStepCm=25;navConfig.agentHeightCm=100;
    auto source=makeCaveAuthoredNavSource(carve,navConfig,
        [](int64_t,int64_t){return int64_t(0);});
    std::vector<aiverse::nav::Span> spans;
    source.spans(0,0,spans);
    assert(!spans.empty()); // authored carve created a walkable underground span

    auto fill=std::make_shared<CaveVolumeSnapshot>(*carve);
    fill->sourceRevision=2;
    fill->stamps.push_back({2,{50,-500,50},400,CaveStampMode::Fill});
    canonicalizeCaveSnapshot(*fill);
    auto filled=makeCaveAuthoredNavSource(fill,navConfig,
        [](int64_t,int64_t){return int64_t(0);});
    spans.clear();
    filled.spans(0,0,spans);
    assert(spans.empty()); // authored fill blocks the previously open cave
    std::cout<<"caverns stage27 volume collision and authored nav contract OK\n";
}
