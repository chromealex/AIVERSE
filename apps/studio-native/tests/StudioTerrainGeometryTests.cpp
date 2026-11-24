#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/studio-native/StudioPlanetPreview.h"
#include "tests/TestCheck.h"
#include <cmath>
#include <iostream>
using namespace aiverse;
using namespace aiverse::studio;
using namespace aiverse::render;

int main() {
    editor::MapData d;
    d.w=2; d.h=2;
    d.cells={'.','~','#','R'};
    d.corners={0,0,0,0,16,0,0,0,0}; // centre elevated by one unit
    MeshBuilder mesh;
    const auto stats=buildTerrainPreview(d,mesh);
    AIVERSE_TEST_CHECK(stats.quads==4 && stats.step==1);
    AIVERSE_TEST_CHECK(mesh.vertices().size()==16 && mesh.indices().size()==24);
    AIVERSE_TEST_CHECK(std::abs(terrainPreviewHeight(d,1,1)-1.0f)<0.001f);
    AIVERSE_TEST_CHECK(std::abs(terrainPreviewHeight(d,0.5f,0.5f)-0.25f)<0.001f);
    AIVERSE_TEST_CHECK(terrainPreviewColor('.')!=terrainPreviewColor('~'));
    AIVERSE_TEST_CHECK(terrainPreviewColor('~')!=terrainPreviewColor('#'));
    for (const auto& v:mesh.vertices()) AIVERSE_TEST_CHECK(v.nrm.y>0.1f);
    d.bodies.emplace_back();
    MeshBuilder planet;
    AIVERSE_TEST_CHECK(buildTerrainPreview(d,planet).quads==0);
    d.bodies.clear();
    d.corners.clear();
    d.levels={2,2,2,2};
    d.heights={8,8,8,8};
    AIVERSE_TEST_CHECK(std::abs(terrainPreviewHeight(d,0,0,1.5f)-3.5f)<0.001f);
    editor::MapData huge;
    huge.w=1024; huge.h=768; // bounded downsampling; no large cell vector allocation
    MeshBuilder reduced;
    auto limit=buildTerrainPreview(huge,reduced);
    AIVERSE_TEST_CHECK(limit.step==4 && limit.quads==size_t(256*192));
    editor::MapDoc globe;
    editor::DocBody body;body.id="terra";body.faceSize=8;
    body.meta.set("radius",Json::number(int64_t(64)));
    body.corners.assign(6*9*9,0);body.cells.assign(6*8*8,'.');
    globe.data.bodies.push_back(body);
    editor::DocEntity e;e.id="front";e.type="unit";
    e.at=Json::parse(R"({"body":"terra","face":0,"x":4,"y":4})");
    globe.data.entities.push_back(e);
    const auto preview=StudioPlanetPreview::build(globe,0);
    AIVERSE_TEST_CHECK(preview&&preview->mesh.indices().size()==6*8*8*6);
    AIVERSE_TEST_CHECK(preview->markers.size()==1);
    const auto center=preview->markers.front().center;
    const auto dir=normalize(center);
    AIVERSE_TEST_CHECK(preview->pick(dir*100.f,dir*-1.f,{}, {})==preview->markers.front().id);
    AIVERSE_TEST_CHECK(preview->pick(dir*-100.f,dir,{}, {}).empty());
    AIVERSE_TEST_CHECK(preview->pick(dir*100.f,dir*-1.f,{preview->markers.front().id},{}).empty());
    const auto at=preview->groundPlacement(dir*100.f,dir*-1.f);
    AIVERSE_TEST_CHECK(at&&at->getStr("body")=="terra"&&at->getInt("face")==0);
    AIVERSE_TEST_CHECK(std::abs((*at)["x"].toDouble()-4.)<.01);
    AIVERSE_TEST_CHECK(!preview->groundPlacement(dir*100.f,dir));
    std::cout<<"StudioTerrainGeometry PASS\n";
    editor::MapDoc blank;blank.reset(32,32);render::MeshBuilder noSurface;
    AIVERSE_TEST_CHECK(buildTerrainPreview(blank.data,noSurface).quads==0);
}
