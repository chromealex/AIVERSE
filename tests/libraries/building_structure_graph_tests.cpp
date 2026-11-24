// Standalone C++20 test. BuildingStructureGraph is independent of engine
// materials, rendering, game modules and editor runtime.
#include <engine/systems/BuildingStructureGraph.h>
#include <engine/systems/BuildingCirculationRuntime.h>
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
        for(const auto& rows:std::vector<std::vector<std::string>>{
            {"100","100","111"},{"111","010","010"},{"101","101","111"},
            {"101","111","101"},{"111","101","111"}}){
            FunctionalBuildingRecipe shape;shape.width=24;shape.depth=24;shape.height=10;
            shape.floors=2;shape.roomsX=shape.roomsZ=3;shape.footprint=rows;
            const auto graph=buildFunctionalStructureGraph(shape);CHECK(graph);
            size_t active=0;
            for(int z=0;z<3;++z)for(int x=0;x<3;++x){
                const bool present=rows[size_t(z)][size_t(x)]=='1';active+=present?1:0;
                const float cx=(x+.5f)*8,cz=(z+.5f)*8;
                CHECK(intersects(graph,cx,.12f,cz,BuildingGraphPart::Floor)==present);
                CHECK(intersects(graph,cx,5.f,cz,BuildingGraphPart::Floor)==present);
                CHECK(intersects(graph,cx,9.9f,cz,BuildingGraphPart::Roof)==present);
            }
            CHECK(graph.rooms.size()==active*2);
            for(const auto& edge:graph.connections){
                bool from=false,to=false;
                for(const auto& room:graph.rooms){from|=room.id==edge.from;to|=room.id==edge.to;}
                CHECK(from&&to);
            }
        }
        FunctionalBuildingRecipe courtyard;courtyard.width=24;courtyard.depth=24;courtyard.height=8;
        courtyard.roomsX=courtyard.roomsZ=3;courtyard.footprint={"111","101","111"};
        BuildingFeature courtDoor;courtDoor.side=BuildingFeatureSide::Back;courtDoor.wallLine=1;
        courtDoor.position=12;courtDoor.width=1.2f;courtDoor.height=2.4f;courtDoor.open=1;
        courtyard.features={courtDoor};
        const auto court=buildFunctionalStructureGraph(courtyard);CHECK(court);
        CHECK(court.sockets.size()==1&&std::abs(court.sockets[0].wallPlane-8)<.001f);
        CHECK(std::abs(court.sockets[0].z-9)<.001f);
        CHECK(!intersects(court,12,1,7.8f,BuildingGraphPart::Wall));
        CHECK(intersects(court,9,1,7.8f,BuildingGraphPart::Wall));
        CHECK(court.exteriorApertures.size()==1&&court.exteriorApertures[0].roomId=="room/0/1/0");
        courtyard.features[0].position=3;CHECK(!buildFunctionalStructureGraph(courtyard));
        FunctionalBuildingRecipe stepped;stepped.width=24;stepped.depth=24;stepped.height=12;
        stepped.roomsX=stepped.roomsZ=3;stepped.floors=3;
        stepped.floorFootprints={{"111","111","111"},{"010","111","010"},{"000","010","000"}};
        BuildingFeature upper;upper.kind=BuildingFeatureKind::Window;upper.floor=2;
        upper.side=BuildingFeatureSide::Front;upper.wallLine=1;upper.position=12;upper.width=1.2f;upper.height=1.f;
        stepped.features={upper};
        const auto stairStep=buildFunctionalStructureGraph(stepped);CHECK(stairStep);
        CHECK(stairStep.rooms.size()==15&&stairStep.sockets.size()==1);
        CHECK(std::abs(stairStep.sockets[0].wallPlane-8)<.001f);
        const float level=.24f+(12.f-.52f)/3.f;
        CHECK(intersects(stairStep,4,level+.12f,4,BuildingGraphPart::Roof));
        CHECK(!intersects(stairStep,4,level+.12f,4,BuildingGraphPart::Floor));
        CHECK(!intersects(stairStep,4,11.9f,4,BuildingGraphPart::Roof));
        CHECK(intersects(stairStep,12,11.9f,12,BuildingGraphPart::Roof));
        stepped.floorFootprints[2]={"100","000","000"};CHECK(!buildFunctionalStructureGraph(stepped));
        FunctionalBuildingRecipe deck;deck.width=24;deck.depth=24;deck.height=10;deck.floors=2;
        deck.roomsX=deck.roomsZ=3;deck.floorFootprints={{"111","111","111"},{"000","010","000"}};
        deck.walkableSetbacks=true;
        BuildingFeature deckDoor;deckDoor.floor=1;deckDoor.side=BuildingFeatureSide::Front;
        deckDoor.wallLine=1;deckDoor.position=12;deckDoor.open=1;
        deck.features={deckDoor};
        const auto deckGraph=buildFunctionalStructureGraph(deck);CHECK(deckGraph);
        CHECK(deckGraph.terraces.size()==8&&deckGraph.terraceAccess.size()==1);
        CHECK(deckGraph.exteriorApertures.empty()&&deckGraph.terraceAccess[0].roomId=="room/1/1/1");
        const float deckY=.24f+(10.f-.52f)/2.f+.24f;
        CHECK(std::abs(deckGraph.sockets[0].openingBottom-deckY)<.001f);
        CHECK(intersects(deckGraph,12,deckY-.12f,4,BuildingGraphPart::Floor));
        CHECK(intersects(deckGraph,12,deckY+.5f,.07f,BuildingGraphPart::Trim));
        CHECK(!intersects(deckGraph,12,deckY+.5f,8.1f,BuildingGraphPart::Wall));
        deck.walkableSetbacks=false;CHECK(!buildFunctionalStructureGraph(deck));
        deck.walkableSetbacks=true;deck.features[0].wallLine=0;CHECK(!buildFunctionalStructureGraph(deck));
        FunctionalBuildingRecipe balcony;balcony.width=24;balcony.depth=24;balcony.height=10;balcony.floors=2;
        balcony.roomsX=balcony.roomsZ=3;balcony.floorFootprints={{"111","101","111"},{"111","101","111"}};
        BuildingFeature balconyDoor;balconyDoor.floor=1;balconyDoor.side=BuildingFeatureSide::Back;
        balconyDoor.wallLine=1;balconyDoor.position=12;balconyDoor.open=1;balconyDoor.balconyDepth=1.8f;
        balcony.features={balconyDoor};
        const float balconyY=.24f+(10.f-.52f)/2.f+.24f;
        const auto cantilever=buildFunctionalStructureGraph(balcony);CHECK(cantilever);
        CHECK(cantilever.balconies.size()==1&&cantilever.terraceAccess.size()==1);
        CHECK(intersects(cantilever,12,balconyY-.1f,9,BuildingGraphPart::Floor));
        CHECK(intersects(cantilever,12,balconyY+.5f,9.73f,BuildingGraphPart::Trim));
        CHECK(!intersects(cantilever,12,balconyY-.1f,12,BuildingGraphPart::Floor));
        balcony.features[0].balconySupport=BuildingBalconySupport::Columns;
        balcony.features[0].balconyDepth=2.5f;
        const auto columns=buildFunctionalStructureGraph(balcony);CHECK(columns);
        CHECK(intersects(columns,11.04f,1.f,10.26f,BuildingGraphPart::Trim));
        balcony.features[0].balconySupport=BuildingBalconySupport::Cantilever;
        CHECK(!buildFunctionalStructureGraph(balcony));
        FunctionalBuildingRecipe disconnected;disconnected.roomsX=disconnected.roomsZ=3;
        disconnected.footprint={"100","010","001"};CHECK(!buildFunctionalStructureGraph(disconnected));
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
        for(const auto& socket:a.sockets){
            CHECK(ids.insert(socket.id).second);
            if(socket.kind==BuildingFeatureKind::Window)
                CHECK(std::abs(socket.y-(0.24f+(14.f-0.52f)/2.f+0.24f+1.05f))<0.002f);
        }
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
        // Wall bands below/above windows and above entrances must remain
        // structural Wall solids, never opaque fill over the clear opening.
        CHECK(intersects(a,7.f,5.f,0.15f,BuildingGraphPart::Wall));
        for(const auto& socket:a.sockets){
            if(socket.kind!=BuildingFeatureKind::Window)continue;
            CHECK(intersects(a,0.18f,socket.y-0.2f,socket.z,BuildingGraphPart::Wall));
            CHECK(!intersects(a,0.18f,socket.y+0.5f,socket.z,BuildingGraphPart::Wall));
            CHECK(intersects(a,0.18f,socket.y+0.5f,socket.z,BuildingGraphPart::WindowPane));
            CHECK(intersects(a,0.18f,socket.y+socket.height+0.2f,socket.z,BuildingGraphPart::Wall));
        }
        CHECK(!intersects(a,13.f,13.86f,10.f,BuildingGraphPart::Roof));
        CHECK(!intersects(a,13.f,13.86f,10.f,BuildingGraphPart::RoofPanel));
        auto bad=r;bad.floors=7;CHECK(!buildFunctionalStructureGraph(bad));
        bad=r;bad.features[1].floor=2;CHECK(!buildFunctionalStructureGraph(bad));
        bad=r;bad.features[0].position=-0.25f;CHECK(!buildFunctionalStructureGraph(bad));
        // Vertical circulation: both shafts remove REAL upper floor slab
        // collision, stairs have bounded physical steps and landings,
        // and graph edges connect only adjacent storeys.
        FunctionalBuildingRecipe vertical=r;
        vertical.features.clear();
        BuildingFeature stairs;
        stairs.kind=BuildingFeatureKind::Stairs;
        stairs.width=2.6f;stairs.depth=6.6f;
        stairs.position=6.f;stairs.positionZ=4.5f;
        BuildingFeature lift;
        lift.kind=BuildingFeatureKind::Elevator;
        lift.width=2.f;lift.depth=2.2f;
        lift.position=18.f;lift.positionZ=4.5f;
        vertical.features={stairs,lift};
        const auto shaft=buildFunctionalStructureGraph(vertical);
        CHECK(shaft);
        CHECK(shaft.rooms.size()==8);
        CHECK(shaft.connections.size()==10); // 8 horizontal + 2 vertical
        CHECK(!intersects(shaft,6.f,7.f,4.5f,BuildingGraphPart::Floor));
        CHECK(!intersects(shaft,18.f,7.f,4.5f,BuildingGraphPart::Floor));
        CHECK(intersects(shaft,12.f,7.f,9.f,BuildingGraphPart::Floor));
        int steps=0,landings=0,platforms=0,verticalEdges=0;
        for(const auto& solid:shaft.solids){
            if(solid.part==BuildingGraphPart::StairTread){
                ++steps;
                CHECK(solid.featureIndex==0);
                CHECK(solid.y1-solid.y0<=0.181f);
                CHECK(solid.z1-solid.z0>=0.24f);
            }
            if(solid.part==BuildingGraphPart::StairLanding)++landings;
            if(solid.part==BuildingGraphPart::ElevatorPlatform)++platforms;
        }
        for(const auto& edge:shaft.connections){
            if(edge.kind==BuildingConnectionKind::Passage)continue;
            ++verticalEdges;
            CHECK(edge.from=="room/0/"+std::string(edge.kind==BuildingConnectionKind::Stair?"0":"1")+"/0");
            CHECK(edge.to=="room/1/"+std::string(edge.kind==BuildingConnectionKind::Stair?"0":"1")+"/0");
            CHECK(edge.height>6.0f);
        }
        CHECK(steps>=36&&landings==1&&platforms==1&&verticalEdges==2);
        CHECK(shaft.sockets.size()==2);
        CHECK(shaft.sockets[0].id=="stairs/0/0");
        CHECK(shaft.sockets[1].id=="lift/1/0");
        auto collision=vertical;collision.features[1].position=6.f;
        CHECK(!buildFunctionalStructureGraph(collision)); // overlapping cores
        collision=vertical;collision.features[0].depth=5.0f;
        CHECK(!buildFunctionalStructureGraph(collision)); // unsafe stair pitch
        collision=vertical;collision.floors=1;
        CHECK(!buildFunctionalStructureGraph(collision)); // no vertical links
        // Three storeys: two vertical links per core and only ONE initial
        // lift platform; no phantom upper-floor lift slabs.
        auto three=vertical;three.height=22.f;three.floors=3;
        auto threeGraph=buildFunctionalStructureGraph(three);
        CHECK(threeGraph);
        CHECK(threeGraph.rooms.size()==12);
        CHECK(threeGraph.connections.size()==16); // 12 horizontal + 4 vertical
        int liftPlatforms=0,guides=0,upperHoles=0;
        for(const auto& part:threeGraph.solids){
            if(part.part==BuildingGraphPart::ElevatorPlatform)++liftPlatforms;
            if(part.part==BuildingGraphPart::ElevatorGuide)++guides;
        }
        CHECK(liftPlatforms==1&&guides==4);
        for(uint32_t floor=1;floor<3;++floor){
            const float level=0.24f+(three.height-0.52f)/3.f*float(floor)+0.12f;
            CHECK(!intersects(threeGraph,6.f,level,4.5f,BuildingGraphPart::Floor));
            CHECK(!intersects(threeGraph,18.f,level,4.5f,BuildingGraphPart::Floor));
            ++upperHoles;
        }
        CHECK(upperHoles==2);
        // Recover an occupied lift mid-trip, preserving integer rounding and
        // door timing. Unconsumed motion cannot be checkpointed.
        BuildingLiftController original,recovered;
        CHECK(original.initialize(threeGraph,1));
        CHECK(recovered.initialize(threeGraph,1));
        CHECK(original.board(0));
        CHECK(original.requestDestination(2));
        original.advance(BuildingLiftController::kDoorTimeMs+17);
        BuildingLiftCheckpoint saved;
        CHECK(!original.captureCheckpoint(saved));
        CHECK(original.takePlatformMotion().has_value());
        CHECK(original.captureCheckpoint(saved));
        CHECK(saved.state.phase==BuildingLiftPhase::Traveling);
        auto invalid=saved;invalid.travelRemainder=1000;
        CHECK(!recovered.restoreCheckpoint(invalid));
        CHECK(recovered.restoreCheckpoint(saved));
        CHECK(!recovered.takePlatformMotion().has_value());
        for(int i=0;i<20;++i){
            original.advance(17);recovered.advance(17);
            auto x=original.takePlatformMotion(),y=recovered.takePlatformMotion();
            CHECK(bool(x)==bool(y));
            if(x)CHECK(x->oldYmm==y->oldYmm&&x->newYmm==y->newYmm&&x->revision==y->revision);
        }
        CHECK(original.snapshot().platformYmm==recovered.snapshot().platformYmm);
        CHECK(recovered.snapshot().passengerCount==1);
        std::puts("building structure graph standalone PASS");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building structure graph test FAIL: %s\n",e.what());return 1;
    }
}
