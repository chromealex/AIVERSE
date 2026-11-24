#include "apps/studio-native/StudioTerrainGeometry.h"
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
    std::cout<<"StudioTerrainGeometry PASS\n";
    editor::MapDoc blank;blank.reset(32,32);render::MeshBuilder noSurface;
    AIVERSE_TEST_CHECK(buildTerrainPreview(blank.data,noSurface).quads==0);
}
