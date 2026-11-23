// Standalone C++20 test. BuildingStructureGraph is independent of engine
// materials, rendering, game modules and editor runtime.
#include <aiverselib/systems/BuildingStructureGraph.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_set>
using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x))throw std::runtime_error( \
    std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)

static bool intersects(const BuildingStructureGraph& g,float x,float y,float z,
                       BuildingGraphPart part){
    for(const auto& s:g.solids)
        if(s.part==part&&x>s.x0&&x<s.x1&&y>s.y0&&y<s.y1&&z>s.z0&&z<s.z1)return true;
    return false;
}
int main(){
    try{
        FunctionalBuildingRecipe r;
        r.width=24;r.height=14;r.depth=18;r.floors=2;r.roomsX=2;r.roomsZ=2;
        BuildingFeature gate;gate.kind=BuildingFeatureKind::LargeGate;gate.width=4.5f;
        gate.height=3.7f;gate.position=7;gate.open=1;
        BuildingFeature window;window.kind=BuildingFeatureKind::Window;
        window.side=BuildingFeatureSide::Left;window.count=2;
        window.floor=1;window.sill=1.05f;window.width=1.6f;window.height=1.2f;
        window.position=9;window.spacing=1.2f;
        BuildingFeature roof;roof.kind=BuildingFeatureKind::SlidingRoof;
        roof.width=5;roof.depth=5;roof.position=13;roof.positionZ=10;roof.open=1;
        r.features={gate,window,roof};

        auto a=buildFunctionalStructureGraph(r),b=buildFunctionalStructureGraph(r);
        CHECK(a&&b);
        CHECK(a.solids.size()==b.solids.size());
        CHECK(a.rooms.size()==8&&a.connections.size()==8);
        CHECK(a.sockets.size()==4);
        std::unordered_set<std::string> ids;
        for(const auto& socket:a.sockets)CHECK(ids.insert(socket.id).second);
        for(const auto& room:a.rooms){
            CHECK(room.x1>room.x0&&room.y1>room.y0&&room.z1>room.z0);
            CHECK(ids.insert(room.id).second);
        }
        for(const auto& edge:a.connections){
            CHECK(edge.width>=1.19f&&edge.height>=2.f);
            CHECK(ids.insert(edge.id).second);
            CHECK(!intersects(a,edge.x,edge.y+1.0f,edge.z,BuildingGraphPart::Partition));
        }
        size_t panes=0,slabs=0;
        for(const auto& s:a.solids){
            if(s.part==BuildingGraphPart::WindowPane){
                ++panes;
                CHECK(s.y0>7.f); // upper floor
                CHECK((s.x1-s.x0)<0.36f); // thin pane along X on the left wall
            }
            if(s.part==BuildingGraphPart::Floor)++slabs;
        }
        CHECK(panes==2&&slabs==2);
        CHECK(!intersects(a,7.f,2.f,0.15f,BuildingGraphPart::Wall));
        CHECK(!intersects(a,13.f,13.86f,10.f,BuildingGraphPart::Roof));
        CHECK(!intersects(a,13.f,13.86f,10.f,BuildingGraphPart::RoofPanel));
        auto bad=r;bad.floors=7;CHECK(!buildFunctionalStructureGraph(bad));
        bad=r;bad.features[1].floor=2;CHECK(!buildFunctionalStructureGraph(bad));
        bad=r;bad.features[0].position=-0.25f;CHECK(!buildFunctionalStructureGraph(bad));
        std::puts("building structure graph standalone PASS");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building structure graph test FAIL: %s\n",e.what());return 1;
    }
}
