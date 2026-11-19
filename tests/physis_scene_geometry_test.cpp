#include "engine/physis/PhysisSceneGeometry.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failed = 0;
#define CHECK(c) do { if(!(c)){std::printf("PHYSIS SCENE FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failed;} } while(0)
void tilesAndGridOrigin() {
    World world;
    Settings s;
    s.width=4; s.height=4; s.originX=-2; s.originZ=-2;
    s.cellSizeMeters=100;
    initialize(world,s);
    DiagnosticRequest request;
    request.originX=-3;request.originZ=-3;
    request.width=6;request.height=6;
    request.layer=DiagnosticLayer::TemperatureCelsius;
    DiagnosticRaster raster;
    CHECK(sampleDiagnosticRaster(world,request,raster));
    auto tiles=makeSceneDiagnosticTiles(world,raster);
    CHECK(tiles.size()==16);
    CHECK(tiles.front().cellX==-2 && tiles.front().cellZ==-2);
    CHECK(tiles.front().corners[0].x==Fixed::fromInt(-200));
    CHECK(tiles.front().corners[0].z==Fixed::fromInt(-200));
    CHECK(tiles.front().corners[1].x==Fixed::fromInt(-100));
    CHECK(tiles.front().corners[2].z==Fixed::fromInt(-100));
    CHECK(tiles.front().corners[0].y==Fixed::fromRatio(1,20));
    CHECK(tiles.front().color.a==255);
    const World before=world;
    CHECK(makeSceneDiagnosticTiles(world,raster,DebugPalette::Ocean).size()==16);
    CHECK(world.resourceConst<State>()->tick==0);
    CHECK(before.resourceConst<State>()->tick==0);
}
void downsampleAndMissingWorld() {
    World world;
    DiagnosticRaster raster;
    CHECK(makeSceneDiagnosticTiles(world,raster).empty());
    Settings s;s.width=4;s.height=4;s.cellSizeMeters=40;
    initialize(world,s);
    DiagnosticRequest req;
    req.width=4;req.height=4;req.stride=2;
    req.layer=DiagnosticLayer::Rain;
    CHECK(sampleDiagnosticRaster(world,req,raster));
    auto tiles=makeSceneDiagnosticTiles(world,raster);
    CHECK(tiles.size()==4);
    CHECK(tiles[0].corners[1].x-tiles[0].corners[0].x==Fixed::fromInt(80));
    DiagnosticRaster invalid=raster;
    invalid.valid.resize(1);
    CHECK(makeSceneDiagnosticTiles(world,invalid).empty());
}
}
int main() {
    tilesAndGridOrigin();
    downsampleAndMissingWorld();
    std::printf("PHYSIS SceneGeometry: %s\n",failed?"FAIL":"PASS");
    return failed?1:0;
}
