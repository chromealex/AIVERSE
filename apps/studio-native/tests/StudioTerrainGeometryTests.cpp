#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/studio-native/StudioSpectraFields.h"
#include "apps/studio-native/StudioPlanetPreview.h"
#include "apps/studio-native/StudioPlanetForestOverlays.h"
#include "apps/studio-native/StudioPlanetLiveTerrain.h"
#include "apps/studio-native/StudioPlanetProcessOverlays.h"
#include "apps/studio-native/StudioPlanetRegionEditing.h"
#include "apps/studio-native/StudioPlanetGravityEditing.h"
#include "tests/TestCheck.h"
#include <cmath>
#include <iostream>
using namespace aiverse;
using namespace aiverse::studio;
using namespace aiverse::render;

int main() {
    {
        // The trunk lies along world +X. The capsule has radius .5 m,
        // total length 6 m; the prototype trunk is 3 m by .25 m radius.
        mat4 body;
        body.at(0,0)=0; body.at(1,0)=-1;
        body.at(0,1)=1; body.at(1,1)=0;
        body=mat4::translate({8,7,6})*body;
        const lib::worldprocess::PlanetArborTrunk trunk{7,300,25};
        const auto transform=studioFallenArborTransform(body,trunk,.5f,2.5f);
        const auto root=transform.transformPoint({0,0,0});
        const auto tip=transform.transformPoint({0,3,0});
        const auto bark=transform.transformPoint({.25f,1.5f,0});
        AIVERSE_TEST_CHECK(root.x==5 && root.y==7 && root.z==6);
        AIVERSE_TEST_CHECK(tip.x==11 && tip.y==7 && tip.z==6);
        AIVERSE_TEST_CHECK(bark.x==8 && bark.y==6.5f && bark.z==6);
    }
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
    auto fields=buildStudioSpectraFields(d,1.5f,true,nullptr);
    float sampledValue=0;
    AIVERSE_TEST_CHECK(fields->sample("terraweave","height",{0.5f,0,0.5f},sampledValue)&&std::abs(sampledValue-0.5f)<0.001f);
    AIVERSE_TEST_CHECK(fields->sample("terraweave","clearance",{0.5f,2,0.5f},sampledValue)&&std::abs(sampledValue-1.5f)<0.001f);
    AIVERSE_TEST_CHECK(!fields->sample("terraweave","height",{-1,0,0},sampledValue));
    AIVERSE_TEST_CHECK(!fields->sample("physis","wind_x",{0,0,0},sampledValue));
    AIVERSE_TEST_CHECK(!buildStudioSpectraFields(d,1.5f,false,nullptr)->sample("terraweave","height",{0,0,0},sampledValue));
    auto collision=buildStudioSpectraCollisions(d,1.5f,true);
    fx::SceneCollisionHit hit;
    fx::SceneCollisionStats collisionStats;
    AIVERSE_TEST_CHECK(collision->sweep({{1,3,1},{1,-1,1},0.25f},hit,collisionStats));
    AIVERSE_TEST_CHECK(std::abs(hit.fraction-0.4375f)<0.001f);
    AIVERSE_TEST_CHECK(!buildStudioSpectraCollisions(d,1.5f,false)->sweep({{1,3,1},{1,-1,1},0},hit,collisionStats));
    d.bodies.emplace_back();
    MeshBuilder planet;
    AIVERSE_TEST_CHECK(buildTerrainPreview(d,planet).quads==0);
    AIVERSE_TEST_CHECK(!buildStudioSpectraFields(d,1.5f,true,nullptr)->sample("terraweave","height",{0,0,0},field));
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
    Json grid=Json::parse(R"({"body":"terra","faceSize":8,"radius":64,"step":2,"side":5})");
    Json coarse=Json::array();for(int i=0;i<6*25;++i)coarse.push(Json::number(int64_t(0)));grid.set("corners",coarse);
    const auto low=buildStudioLivePlanetTerrain(grid,body,*preview);
    AIVERSE_TEST_CHECK(low&&low->indices().size()==6*4*4*6);
    grid.set("patches",Json::parse(R"([{"face":0,"x":1,"y":1,"corners":[0,0,0,0,16,0,0,0,0]}])"));
    const auto refined=buildStudioLivePlanetTerrain(grid,body,*preview);
    AIVERSE_TEST_CHECK(refined&&refined->indices().size()==low->indices().size()+18);
    size_t splitIndices=0;for(int f=0;f<6;++f){const auto face=buildStudioLivePlanetTerrain(grid,body,*preview,f);AIVERSE_TEST_CHECK(face);splitIndices+=face->indices().size();}
    AIVERSE_TEST_CHECK(splitIndices==refined->indices().size());
    const auto face0=studioLiveTerrainFaceSignature(grid,0),face1=studioLiveTerrainFaceSignature(grid,1);
    AIVERSE_TEST_CHECK(face0["patches"].size()==1&&face1["patches"].size()==0);
    bool peak=false;for(const auto& v:refined->vertices())if(dot(v.pos,v.pos)>64.5f*64.5f)peak=true;
    AIVERSE_TEST_CHECK(peak);
    grid.set("patches",Json::parse(R"([{"face":0,"x":1,"y":1,"corners":[1,0,0,0,16,0,0,0,0]}])"));
    AIVERSE_TEST_CHECK(!buildStudioLivePlanetTerrain(grid,body,*preview));
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
    const auto matrix=preview->modelFrames.at(preview->markers.front().id);
    AIVERSE_TEST_CHECK(dot(normalize(matrix.transformDir({0,1,0})),dir)>.999f);
    AIVERSE_TEST_CHECK(std::abs(dot(matrix.transformDir({1,0,0}),matrix.transformDir({0,0,1})))<.001f);
    globe.data.entities[0].extra.set("components",Json::parse(R"({"EvolveRegion":{"enabled":true},"EvolveArea":{"halfWidthCm":300,"halfDepthCm":200,"enabled":true}})"));
    globe.data.extra.set("physis",Json::parse(R"({"enabled":true,"enableGravityFields":true,"gravityFields":[{"id":"local","center":[8192,0,0],"mode":"Attract","radiusMeters":100,"radialMagnitude":10}]})"));
    const auto overlay=buildPlanetProcessOverlays(globe,0,*preview);
    AIVERSE_TEST_CHECK(!overlay.invalidPhysis&&!overlay.truncated&&!overlay.regionChartClipped);
    size_t regionLines=0,gravityLines=0;
    for(const auto& line:overlay.lines){if(line.owner.empty())++gravityLines;else ++regionLines;}
    AIVERSE_TEST_CHECK(regionLines==64&&gravityLines>0);
    PlanetRegionChart chart;chart.owner="entity:@front";chart.body="terra";
    chart.radius=Fixed::fromInt(64);
    game::CubeSphere sphere(8);
    chart.up=sphere.dirFromFace(0,Fixed::fromInt(7),Fixed::fromInt(3));
    game::CubeSphere::faceTangents(0,chart.up,chart.east,chart.south);
    chart.area={100,-200,300,200,true};
    const auto direction=planet_overlay_detail::regionDirection(chart.up,chart.east,chart.south,
        chart.radius,Fixed::fromInt(12),Fixed::fromInt(3));
    AIVERSE_TEST_CHECK(direction);
    const auto coords=planetRegionCoordinates(chart,*direction);
    AIVERSE_TEST_CHECK(coords&&fxAbs(coords->x-Fixed::fromInt(12))<Fixed::fromRatio(1,10));
    AIVERSE_TEST_CHECK(fxAbs(coords->y-Fixed::fromInt(3))<Fixed::fromRatio(1,10));
    auto edited=planetRegionDragArea(chart,PlanetRegionHandle::East,{Fixed::fromInt(8),{}},false,false);
    AIVERSE_TEST_CHECK(edited&&edited->centerXcm-edited->halfWidthCm==-200);
    AIVERSE_TEST_CHECK(edited->centerXcm+edited->halfWidthCm==800);
    edited=planetRegionDragArea(chart,PlanetRegionHandle::East,{Fixed::fromInt(8),{}},true,false);
    AIVERSE_TEST_CHECK(edited&&edited->centerXcm==100&&edited->halfWidthCm==700);
    edited=planetRegionDragArea(chart,PlanetRegionHandle::Center,
        {Fixed::fromRatio(24,10),Fixed::fromRatio(-36,10)},false,true);
    AIVERSE_TEST_CHECK(edited&&edited->centerXcm==200&&edited->centerZcm==-400);
    const auto payload=planetRegionEditPayload(chart,*edited,42);
    AIVERSE_TEST_CHECK(payload.getInt("expectedRevision")==42&&payload["value"].getInt("offsetZcm")==-400);
    auto gravityConfig=editor::physis_authoring::defaultConfig();
    gravityConfig.set("enableGravityFields",Json::fromBool(true));
    gravityConfig.set("gravityFields",Json::parse(R"([{"id":"local","center":[8192,0,0],"mode":"Attract","radiusMeters":100,"radialMagnitude":10},{"id":"global","center":[0,0,0],"mode":"Uniform","radiusMeters":0,"acceleration":[0,-1,0]},{"id":"moon","center":[12288,0,0],"mode":"Repel","radiusMeters":80,"radialMagnitude":5}])"));
    globe.data.extra.set("physis",gravityConfig);
    const auto handles=planetGravityHandles(globe,*preview);
    AIVERSE_TEST_CHECK(handles.size()==1&&handles.front().id=="local");
    const auto& field=handles.front();
    AIVERSE_TEST_CHECK(field.center.x==0&&field.radius==100);
    const auto revision=globe.revision;
    AIVERSE_TEST_CHECK(!planetGravityEditPayload(globe,field,field.center,field.radius,revision));
    const auto edit=planetGravityEditPayload(globe,field,{10,20,-30},125,revision);
    AIVERSE_TEST_CHECK(edit&&edit->getStr("op")=="physis_set");
    AIVERSE_TEST_CHECK((*edit)["params"].getInt("expectedRevision")==int64_t(revision));
    const auto& modified=(*edit)["params"]["config"]["gravityFields"][size_t(0)];
    AIVERSE_TEST_CHECK(modified["center"][size_t(0)].toDouble()==8202);
    AIVERSE_TEST_CHECK(modified["center"][size_t(1)].toDouble()==20);
    AIVERSE_TEST_CHECK(modified["center"][size_t(2)].toDouble()==-30);
    AIVERSE_TEST_CHECK(modified.getInt("radiusMeters")==125);
    AIVERSE_TEST_CHECK((*edit)["params"]["config"]["gravityFields"][size_t(1)]==gravityConfig["gravityFields"][size_t(1)]);
    AIVERSE_TEST_CHECK(!planetGravityEditPayload(globe,field,{2048,0,0},125,revision));
    AIVERSE_TEST_CHECK(!planetGravityEditPayload(globe,field,{0,0,0},125,revision+1));
    AIVERSE_TEST_CHECK(!planetGravityEditPayload(globe,field,{0,0,0},0,revision));
    AIVERSE_TEST_CHECK(globe.data.extra["physis"]==gravityConfig);
    std::cout<<"StudioTerrainGeometry PASS\n";
    editor::MapDoc blank;blank.reset(32,32);render::MeshBuilder noSurface;
    AIVERSE_TEST_CHECK(buildTerrainPreview(blank.data,noSurface).quads==0);
}
