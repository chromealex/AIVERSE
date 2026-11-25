#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/studio-native/StudioPlanetGlobe.h"
#include "apps/studio-native/StudioSpectraFields.h"
#include "apps/studio-native/StudioPlanetPreview.h"
#include "apps/studio-native/StudioPlanetForestOverlays.h"
#include "apps/studio-native/StudioPlanetLiveTerrain.h"
#include "apps/studio-native/StudioPlanetProcessOverlays.h"
#include "apps/studio-native/StudioPlanetRegionEditing.h"
#include "apps/studio-native/StudioPlanetGravityEditing.h"
#include "apps/studio-native/StudioRegionBorders.h"
#include "tests/TestCheck.h"
#include <cmath>
#include <iostream>
using namespace aiverse;
using namespace aiverse::studio;
using namespace aiverse::render;

#include "apps/studio-native/StudioMapAreaBounds.h"

int main() {
    {
        const StudioMapAreaBounds expected{2,3,8,10},single{4,5,1,1};
        AIVERSE_TEST_CHECK(studioAreaBetween({9,3},{2,12})==expected);
        AIVERSE_TEST_CHECK(studioAreaBetween({2,12},{9,3})==expected);
        AIVERSE_TEST_CHECK(studioAreaBetween({4,5},{4,5})==single);
    }
    {
        editor::MapDoc doc;doc.reset(16,16);
        auto& map=doc.data;
        map.regions[map.idx(0,0)]='A';map.regions[map.idx(1,0)]='A';map.regions[map.idx(2,0)]='B';
        StudioRegionBorders borders;
        auto build=[&]{borders.build(map,{0,0,map.w,map.h},1.5f,false,'B');};
        build();
        AIVERSE_TEST_CHECK(borders.edges.size()==7);
        const auto shared=std::count_if(borders.edges.begin(),borders.edges.end(),[](const auto& e){
            return e.a.x==2 && e.b.x==2 && e.a.z==0 && e.b.z==1 && e.letter=='B';
        });
        AIVERSE_TEST_CHECK(shared==1);
        doc.beginEdit();doc.paintRegionRect(0,0,2,0,'.');build();
        AIVERSE_TEST_CHECK(borders.edges.empty());
        doc.undo();build();AIVERSE_TEST_CHECK(borders.edges.size()==7);
        map.corners.assign(size_t(map.w+1)*(map.h+1),0);
        map.corners[1]=16;
        borders.build(map,{0,0,map.w,map.h},1.5f,true,'.');
        AIVERSE_TEST_CHECK(std::any_of(borders.edges.begin(),borders.edges.end(),[](const auto& e){
            return e.a.x==1 && e.a.z==0 && std::abs(e.a.y-1.05f)<.001f;
        }));
        doc.reset(260,260);
        for(int z=0;z<map.h;++z)for(int x=0;x<map.w;++x)
            map.regions[map.idx(x,z)]=(x+z)%2?'A':'B';
        build();
        AIVERSE_TEST_CHECK(borders.stride==1 && !borders.edges.empty());
        // A third letter breaks the straight runs of selected-color edges.
        for(int z=0;z<map.h;++z)for(int x=0;x<map.w;++x)
            map.regions[map.idx(x,z)]="ABC"[(x+z)%3];
        build();AIVERSE_TEST_CHECK(borders.stride>1 && borders.edges.size()<=32768);
        borders.build(map,{32,32,40,40},1.5f,false,'B');
        AIVERSE_TEST_CHECK(borders.stride==1 && !borders.edges.empty());
    }
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
    AIVERSE_TEST_CHECK(!buildStudioSpectraFields(d,1.5f,true,nullptr)->sample("terraweave","height",{0,0,0},sampledValue));
    d.bodies.clear();
    // Capture the actual coarse body source, not a reconstructed chart mesh.
    {
        namespace pc=editor::planet_caves;
        editor::MapData map;
        auto& body=map.bodies.emplace_back();
        body.id="test-planet";body.faceSize=64;
        body.meta.set("kind",Json::string("planet"));
        body.meta.set("radius",Json::number(int64_t(100)));
        const terraweave::caverns::CavePlanetAnchor anchor{
            body.id,0,Fixed::fromInt(32),Fixed::fromInt(32),1000};
        auto region=pc::create(map,anchor);
        map.extra.set(pc::key,pc::encode(map,region));
        auto regions=pc::readRegions(map);
        AIVERSE_TEST_CHECK(regions.active==1&&regions.next==2&&regions.regions.size()==1);
        auto second=anchor;second.a=Fixed::fromInt(4);second.b=Fixed::fromInt(4);second.halfExtentCm=100;
        AIVERSE_TEST_CHECK(pc::addRegion(map,regions,pc::create(map,second))==2);
        AIVERSE_TEST_CHECK(regions.active==2&&regions.regions.size()==2);
        bool overlap=false;
        try{(void)pc::addRegion(map,regions,pc::create(map,anchor));}
        catch(const std::invalid_argument&){overlap=true;}
        AIVERSE_TEST_CHECK(overlap&&regions.next==3);
        map.extra.set(pc::key,pc::encodeRegions(map,regions));
        const auto restoredRegions=pc::readRegions(map);
        AIVERSE_TEST_CHECK(restoredRegions.active==2&&restoredRegions.next==3);
        AIVERSE_TEST_CHECK(pc::read(map).anchor.a==second.a);
        regions.active=1;
        map.extra.set(pc::key,pc::encodeRegions(map,regions));
        AIVERSE_TEST_CHECK(pc::read(map).anchor.a==anchor.a);
        const auto globe=StudioPlanetGlobe::capture(map,anchor.bodyId);
        AIVERSE_TEST_CHECK(globe.divisions==64&&globe.triangles.size()==12u*64u*64u);
        AIVERSE_TEST_CHECK(globe.markers.size()==2);
        const auto dir=globe.cube.dirFromFace(anchor.face,anchor.a,anchor.b);
        const render::float3 unit=render::normalize(render::float3{
            float(dir.x.toDouble()),float(dir.y.toDouble()),float(dir.z.toDouble())});
        const auto hit=globe.pick(unit*30.f,unit*(-1.f));
        AIVERSE_TEST_CHECK(hit.has_value()&&hit->regionId=="1");
        AIVERSE_TEST_CHECK(hit->face==anchor.face);
        AIVERSE_TEST_CHECK(fxAbs(hit->a-anchor.a)<Fixed::fromRatio(1,8));
        AIVERSE_TEST_CHECK(fxAbs(hit->b-anchor.b)<Fixed::fromRatio(1,8));
        AIVERSE_TEST_CHECK(!globe.pick(unit*30.f,unit));
        AIVERSE_TEST_CHECK(!globe.pick(unit*30.f,unit*2.f));
        const auto frame=pc::frameForDocument(map,anchor);
        const auto patch=pc::captureLocalSurface(map,anchor,7);
        AIVERSE_TEST_CHECK(!patch.cells.empty());
        AIVERSE_TEST_CHECK(patch.bodyTriangles.size()==patch.chartTriangles.size());
        AIVERSE_TEST_CHECK(patch.cells.size()*2==patch.bodyTriangles.size());
        const auto plan=pc::planLocalSurfaceCut(frame,patch,region.volume,9);
        AIVERSE_TEST_CHECK(plan.surfaceRevision==7 && plan.surfaceEpoch==9 && plan.rims.empty());
        size_t originalCount=0;
        for(const auto& [key,slot]:plan.slots){
            (void)key;originalCount+=slot.originalBody.size();
        }
        AIVERSE_TEST_CHECK(originalCount==patch.bodyTriangles.size());
        bool emptyRefused=false;
        try{(void)refinePlanetChartRim(plan,patch,11,8,25);}
        catch(const std::invalid_argument&){emptyRefused=true;}
        AIVERSE_TEST_CHECK(emptyRefused);
        auto bad=patch;
        bad.chartTriangles.front().a.x=bad.chartTriangles.front().a.x+Fixed::fromInt(1);
        bool refused=false;
        try{(void)pc::planLocalSurfaceCut(frame,bad,region.volume,10);}
        catch(const std::invalid_argument&){refused=true;}
        AIVERSE_TEST_CHECK(refused);
    }
    // Exercise grid refinement with a known exact square ring. This isolates
    // the shared chart adapter from the real planet cutter's input policy.
    {
        namespace cave=terraweave::caverns;
        const auto p=[](int x,int z){return Vec3{Fixed::fromRatio(x,2),{},Fixed::fromRatio(z,2)};};
        const std::array<Vec3,4> outer{p(-8,-8),p(-8,8),p(8,8),p(8,-8)};
        const std::array<Vec3,4> inner{p(-3,-3),p(-3,3),p(3,3),p(3,-3)};
        cave::CavePlanetSurfacePatch baseline;
        baseline.surfaceRevision=7;
        baseline.chartTriangles={{outer[0],outer[1],outer[2]},{outer[0],outer[2],outer[3]}};
        cave::CavePlanetCutPlan cut;
        cut.anchor=baseline.anchor;cut.surfaceRevision=7;cut.surfaceEpoch=9;cut.authorityRevision=10;
        cut.accepted={1};cut.rims.push_back({1,{}});
        auto& slot=cut.slots[{0,0,0,1,0}];
        for(size_t i=0;i<4;++i){
            const auto j=(i+1)%4;
            slot.chartTriangles.push_back({outer[i],outer[j],inner[j]});
            slot.chartTriangles.push_back({outer[i],inner[j],inner[i]});
            cut.rims.front().ring.push_back(cave::surfaceVertexKey(inner[i]));
        }
        const auto refined=refinePlanetChartRim(cut,baseline,11,8,25);
        AIVERSE_TEST_CHECK(refined.authorityRevision==11 && refined.surfaceEpoch==9);
        AIVERSE_TEST_CHECK(refined.rims.front().ring.size()==8);
        MeshBuilder cutMesh;
        AIVERSE_TEST_CHECK(buildPlanetChartCutPreview(refined,cutMesh)==12);
        AIVERSE_TEST_CHECK(cutMesh.indices().size()==36);
        bool badGrid=false;
        try{(void)refinePlanetChartRim(cut,baseline,11,8,26);}
        catch(const std::invalid_argument&){badGrid=true;}
        AIVERSE_TEST_CHECK(badGrid);
        auto corrupt=cut;corrupt.slots.begin()->second.chartTriangles.pop_back();
        bool badBoundary=false;
        try{(void)refinePlanetChartRim(corrupt,baseline,11,8,25);}
        catch(const std::invalid_argument&){badBoundary=true;}
        AIVERSE_TEST_CHECK(badBoundary);
    }
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
