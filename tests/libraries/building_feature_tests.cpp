// Genuine geometry + collision regression for independent functional
// properties. No genre, Studio, viewer or game-host source dependency.
#include <engine/systems/BuildingFeatureGenerator.h>
#include <engine/systems/RuntimeBuildingGenerator.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::lib::systems;

#define CHECK(x) do{if(!(x))throw std::runtime_error( \
    std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
std::string readFile(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    if(!f)throw std::runtime_error("cannot read "+path);
    std::stringstream s;s<<f.rdbuf();return s.str();
}
bool collisionAt(const StyledStructure& s,float x,float y,float z){
    for(const auto& b:s.boxes){
        if(x>=float(b.min.x.toDouble())&&x<float(b.max.x.toDouble())&&
           y>=float(b.min.y.toDouble())&&y<float(b.max.y.toDouble())&&
           z>=float(b.min.z.toDouble())&&z<float(b.max.z.toDouble()))return true;
    }
    return false;
}
bool sameGeometry(const StyledStructure& a,const StyledStructure& b){
    if(a.mesh.vertices.size()!=b.mesh.vertices.size()||a.mesh.triangles()!=b.mesh.triangles())return false;
    for(size_t i=0;i<a.mesh.vertices.size();++i)for(int c=0;c<3;++c)
        if(std::abs(a.mesh.vertices[i].pos[c]-b.mesh.vertices[i].pos[c])>0.000001f)return false;
    return true;
}
}
int main(int argc,char** argv){
    try{
        const std::string root=argc>1?argv[1]:".";
        const auto scifi=readBuildingStyle(Json::parse(readFile(root+"/engine/assets/building-styles/scifi.json")));
        const auto modern=readBuildingStyle(Json::parse(readFile(root+"/engine/assets/building-styles/modern.json")));
        // Visual grammar must vary while geometry bounds and interaction
        // placement remain style-independent.
        FunctionalBuildingRecipe interior;interior.width=24;interior.depth=24;interior.height=8;
        interior.roomsX=interior.roomsZ=2;interior.interiorProfile=BuildingInteriorProfile::Office;
        interior.furnitureDensity=4;interior.interiorLighting=true;interior.interiorDecor=true;
        std::set<std::string> signatures;
        for(const char* id:{"medieval-fantasy","dark-fantasy","steampunk","cyberpunk","scifi-space","alien-civilization","post-apocalypse","modern","ancient-civilizations","asian-historical","solarpunk","underwater"}){
            const auto style=readBuildingStyle(Json::parse(readFile(root+"/engine/assets/building-styles/"+id+".json")));
            buildingStyleDetail::Mesher m(style.materials.size());
            buildingInteriorDetail::Form f{m,0,0,0,1.4f,.8f,.8f};
            buildingInteriorDetail::furniture(f,style,BuildingFurnitureKind::Desk,style.interior,style.shell,style.roof);
            std::string signature;
            for(const auto& v:m.v){for(float p:v.pos)signature+=std::to_string(p)+",";CHECK(v.pos[0]>=-.001f&&v.pos[0]<=1.401f&&v.pos[1]>=-.001f&&v.pos[1]<=.801f&&v.pos[2]>=-.001f&&v.pos[2]<=.801f);}
            buildingInteriorDetail::Form fixture{m,0,0,0,.44f,.16f,.44f};
            buildingInteriorDetail::fixture(fixture,style,style.shell,style.roof);
            for(const auto& v:m.v)for(float p:v.pos)signature+=std::to_string(p)+",";
            CHECK(signatures.insert(signature).second);
            const auto result=generateFunctionalBuilding(style,interior);
            CHECK(result&&result.graph.furniture.size()==16&&result.graph.furnitureSockets.size()==16);
        }
        FunctionalBuildingRecipe composed;composed.width=24;composed.depth=24;composed.height=12;
        composed.roomsX=composed.roomsZ=3;composed.footprint={"111","101","111"};
        composed.roofProfile=FunctionalRoofProfile::Gable;composed.roofRise=2;
        const auto compound=generateFunctionalBuilding(modern,composed);
        CHECK(compound&&compound.graph.roofSections.size()>1);
        CHECK(std::abs(compound.building.mesh.boundsMax[1]-12)<.001f);
        CHECK(!collisionAt(compound.building,12,10.5f,12));
        for(const auto& v:compound.building.mesh.vertices)
            CHECK(!(v.pos[1]>10.001f&&v.pos[0]>8.001f&&v.pos[0]<15.999f&&v.pos[2]>8.001f&&v.pos[2]<15.999f));
        FunctionalBuildingRecipe bevel;bevel.width=24;bevel.depth=24;bevel.height=8;
        bevel.roomsX=bevel.roomsZ=2;bevel.cornerChamfers={3,3,3,3};
        const auto beveled=generateFunctionalBuilding(modern,bevel);
        CHECK(beveled&&beveled.graph.footprintPolygon.size()==8);
        CHECK(!collisionAt(beveled.building,.4f,1,.4f));
        CHECK(collisionAt(beveled.building,1.65f,1,1.65f));
        bool diagonal=false;
        for(const auto& v:beveled.building.mesh.vertices){
            CHECK(v.pos[0]+v.pos[2]>=2.999f);
            CHECK(24.f-v.pos[0]+v.pos[2]>=2.999f);
            CHECK(48.f-v.pos[0]-v.pos[2]>=2.999f);
            CHECK(v.pos[0]+24.f-v.pos[2]>=2.999f);
            diagonal|=std::abs(v.nrm[0])>.5f&&std::abs(v.nrm[2])>.5f;
        }
        CHECK(diagonal);
        const auto recipe=readFunctionalBuildingRecipe(Json::parse(readFile(root+"/engine/assets/building-recipes/multiuse-open.json")));
        CHECK(recipe.features.size()==4);
        CHECK(recipe.features[1].kind==BuildingFeatureKind::SmallGate&&recipe.features[1].count==2);
        CHECK(recipe.features[2].kind==BuildingFeatureKind::PersonnelDoor);
        CHECK(recipe.features[3].kind==BuildingFeatureKind::SlidingRoof);

        auto opened=generateFunctionalBuilding(scifi,recipe);
        CHECK(opened);
        CHECK(opened.anchors.size()==5); // large + 2 small + person + roof
        CHECK(opened.building.gates.size()==4);
        CHECK(!opened.building.boxes.empty());
        CHECK(opened.building.mesh.triangles()>100);
        CHECK(opened.building.mesh.triangles()<10000);
        for(int c=0;c<3;++c){
            const float want[]={24.f,12.f,18.f};
            CHECK(std::abs(opened.building.mesh.boundsMin[c])<0.001f);
            CHECK(std::abs(opened.building.mesh.boundsMax[c]-want[c])<0.001f);
        }
        // Stage 11: exact geometry, collision and sockets come from one graph.
        const auto graph=buildFunctionalStructureGraph(recipe);
        CHECK(graph);
        CHECK(graph.sockets.size()==5);
        CHECK(graph.solids.size()==opened.building.boxes.size());
        CHECK(opened.graph.solids.size()==graph.solids.size());
        CHECK(opened.anchors.size()==graph.sockets.size());
        for(size_t i=0;i<graph.sockets.size();++i){
            CHECK(opened.anchors[i].socketId==graph.sockets[i].id);
            CHECK(opened.anchors[i].featureIndex==graph.sockets[i].featureIndex);
            CHECK(std::abs(float(opened.anchors[i].outside.x.toDouble())-graph.sockets[i].x)<0.002f);
            CHECK(std::abs(float(opened.anchors[i].outside.z.toDouble())-graph.sockets[i].z)<0.002f);
        }
        const auto socket=[&](const BuildingStructureGraph& layout,
                              const std::string& id)->const BuildingGraphSocket* {
            for(const auto& item:layout.sockets)if(item.id==id)return &item;
            return nullptr;
        };
        const auto* large=socket(graph,"entrance/0/0");
        const auto* small0=socket(graph,"entrance/1/0");
        const auto* small1=socket(graph,"entrance/1/1");
        const auto* person=socket(graph,"entrance/2/0");
        const auto* roof=socket(graph,"roof/3");
        CHECK(large&&small0&&small1&&person&&roof);
        CHECK(std::abs(large->x-7.f)<0.001f);
        CHECK(std::abs(small0->x-10.15f)<0.001f);
        CHECK(std::abs(small1->x-13.85f)<0.001f);
        CHECK(std::abs(person->x-18.f)<0.001f);
        CHECK(std::abs(roof->x-13.f)<0.001f);
        CHECK(std::abs(roof->z-10.f)<0.001f);

        // Mixed fixed and automatic positions: auto features fill free wall
        // space without displacing an exact portal or producing overlaps.
        auto mixed=recipe;
        mixed.features[2].position=-1.f;
        const auto mixedGraph=buildFunctionalStructureGraph(mixed);
        CHECK(mixedGraph);
        CHECK(mixedGraph.sockets.size()==5);
        CHECK(socket(mixedGraph,"entrance/0/0"));
        CHECK(socket(mixedGraph,"entrance/2/0"));
        CHECK(std::abs(socket(mixedGraph,"entrance/0/0")->x-7.f)<0.001f);
        CHECK(std::abs(socket(mixedGraph,"entrance/2/0")->x-1.32f)<0.001f);
        auto overlap=recipe;
        overlap.features[2].position=7.f; // same front wall span as large gate
        const auto badGraph=buildFunctionalStructureGraph(overlap);
        CHECK(!badGraph);
        CHECK(badGraph.error.find("overlap")!=std::string::npos);
        auto narrowRoof=recipe;
        narrowRoof.features[3].position=3.1f;
        CHECK(!buildFunctionalStructureGraph(narrowRoof)); // cannot retract off roof
        auto invalidAuto=recipe;
        invalidAuto.features[0].position=-0.5f;
        CHECK(!buildFunctionalStructureGraph(invalidAuto)); // -1 is the only auto sentinel
        auto outOfBoundsLeaf=recipe;
        outOfBoundsLeaf.features[0].position=4.f;
        outOfBoundsLeaf.features[0].open=0.9f;
        CHECK(!buildFunctionalStructureGraph(outOfBoundsLeaf)); // panel cannot slide beyond outer wall
        auto midOpen=recipe;
        midOpen.features[0].open=0.5f;
        auto midGraph=buildFunctionalStructureGraph(midOpen);
        CHECK(midGraph);
        CHECK(midGraph.solids.size()>graph.solids.size()); // two movable leaves at intermediate state
        auto again=generateFunctionalBuilding(scifi,recipe);
        CHECK(again&&sameGeometry(opened.building,again.building));
        CHECK(opened.building.boxes==again.building.boxes);
        // Every front aperture must be a true mesh+collision hole, not merely
        // a visual gate leaf on top of an impenetrable solid hull.
        size_t front=0;
        for(const auto& g:opened.anchors){
            if(g.kind==BuildingFeatureKind::SlidingRoof)continue;
            CHECK(g.open==1.f);
            if(g.side==BuildingFeatureSide::Front){
                ++front;
                const float x=float(g.outside.x.toDouble());
                CHECK(!collisionAt(opened.building,x,1.5f,0.18f));
            }
        }
        CHECK(front==2);
        CHECK(!collisionAt(opened.building,12.f,11.86f,9.f)); // open roof gap

        auto closedRecipe=recipe;
        for(auto& f:closedRecipe.features)f.open=0;
        auto closed=generateFunctionalBuilding(scifi,closedRecipe);
        CHECK(closed);
        CHECK(collisionAt(closed.building,12.f,11.86f,9.f)); // roof panel closes gap
        bool gateClosed=false;
        for(const auto& g:closed.anchors){
            if(g.side==BuildingFeatureSide::Front && g.kind==BuildingFeatureKind::LargeGate){
                gateClosed=collisionAt(closed.building,float(g.outside.x.toDouble()),2.f,0.18f);
            }
        }
        CHECK(gateClosed);

        // The same building properties can use completely different art
        // materials/setting without changing the functional geometry.
        auto modernVariant=generateFunctionalBuilding(modern,recipe);
        CHECK(modernVariant);
        CHECK(!sameGeometry(opened.building,modernVariant.building));
        CHECK(modernVariant.graph.solids.size()==opened.graph.solids.size());
        CHECK(opened.building.boxes==modernVariant.building.boxes);
        CHECK(opened.building.mesh.materials.size()!=0);
        CHECK(modernVariant.building.mesh.materials.size()!=0);
        for(const char* id:{"medieval-fantasy","dark-fantasy","steampunk","cyberpunk","scifi-space","alien-civilization","post-apocalypse","modern","ancient-civilizations","asian-historical","solarpunk","underwater"}){
            const auto themed=readBuildingStyle(Json::parse(readFile(root+"/engine/assets/building-styles/"+id+".json")));
            const auto result=generateFunctionalBuilding(themed,recipe);
            CHECK(result&&!sameGeometry(opened.building,result.building));
            CHECK(result.building.boxes==opened.building.boxes);
            CHECK(result.graph.sockets.size()==opened.graph.sockets.size());
            CHECK(result.graph.connections.size()==opened.graph.connections.size());
            for(const auto& v:result.building.mesh.vertices)for(int axis=0;axis<3;++axis)
                CHECK(v.pos[axis]>=opened.building.mesh.boundsMin[axis]-.001f&&v.pos[axis]<=opened.building.mesh.boundsMax[axis]+.001f);
        }

        // Rotations keep actual apertures and dimensions, never stretch them.
        for(uint32_t turn=1;turn<4;++turn){
            auto rotated=generateFunctionalBuilding(scifi,recipe,{},turn);
            CHECK(rotated);
            CHECK(rotated.building.mesh.triangles()==opened.building.mesh.triangles());
            CHECK(std::abs(rotated.building.mesh.boundsMax[0]-(turn%2?18.f:24.f))<0.002f);
            CHECK(std::abs(rotated.building.mesh.boundsMax[2]-(turn%2?24.f:18.f))<0.002f);
        }

        // Runtime request dispatch does not require a preset "factory" role.
        RuntimeBuildingGenerator runtime;
        std::string error;
        CHECK(runtime.registerStyleJson(readFile(root+"/engine/assets/building-styles/scifi.json"),&error));
        CHECK(runtime.registerStyleJson(readFile(root+"/engine/assets/building-styles/modern.json"),&error));
        RuntimeBuildingRequest request;
        request.style="modern";request.function="";request.seed=recipe.seed;
        request.size={24,12,18};request.composeFeatures=true;
        request.features=recipe.features;
        auto viaRuntime=runtime.generate(request);
        CHECK(viaRuntime);
        // Runtime static geometry excludes moving panels/platforms: the
        // authoritative dynamic owners publish them separately. Compare the
        // exact static projection, not the complete offline export mesh.
        const auto staticModern=generateFunctionalBuilding(modern,recipe,{},0,false,false);
        const auto staticScifi=generateFunctionalBuilding(scifi,recipe,{},0,false,false);
        CHECK(staticModern&&staticScifi);
        CHECK(sameGeometry(viaRuntime.building,staticModern.building));
        CHECK(viaRuntime.building.boxes==staticModern.building.boxes);
        CHECK(staticModern.building.mesh.triangles()<modernVariant.building.mesh.triangles());
        auto viaRecipe=runtime.generateRecipe("scifi",recipe);
        CHECK(viaRecipe&&sameGeometry(viaRecipe.building,staticScifi.building));
        CHECK(viaRecipe.building.boxes==staticScifi.building.boxes);

        // Real roof caps reserve their height above the same room graph.
        // Both visual mesh and physical bounds follow the explicit profile.
        {
            auto flat=recipe;
            flat.features.erase(std::remove_if(flat.features.begin(),flat.features.end(),[](const auto& f){return f.kind==BuildingFeatureKind::SlidingRoof;}),flat.features.end());
            const auto base=generateFunctionalBuilding(scifi,flat);
            CHECK(base);
            for(int i=1;i<=int(FunctionalRoofProfile::Pagoda);++i){
                auto raised=flat;raised.height+=3;raised.roofRise=3;raised.roofProfile=FunctionalRoofProfile(i);
                const auto a=generateFunctionalBuilding(modern,raised);
                CHECK(a);
                CHECK(a.graph.rooms.size()==base.graph.rooms.size());
                CHECK(a.graph.sockets.size()==base.graph.sockets.size());
                CHECK(a.graph.connections.size()==base.graph.connections.size());
                CHECK(a.building.boxes.size()>base.building.boxes.size());
                CHECK(std::abs(a.building.mesh.boundsMax[1]-raised.height)<.003f);
                auto bad=recipe;bad.roofRise=3;bad.height+=3;bad.roofProfile=raised.roofProfile;
                CHECK(!buildFunctionalStructureGraph(bad)); // sliding aperture cannot be sealed by cap
            }
        }
        // Contradictory features are REJECTED (never overlap two doors).
        auto impossible=recipe;
        impossible.width=8.f;
        auto failed=generateFunctionalBuilding(scifi,impossible);
        CHECK(!failed&&!failed.error.empty());
        auto invalid=recipe;
        invalid.features.push_back(invalid.features.back());
        CHECK(!generateFunctionalBuilding(scifi,invalid));
        CHECK(!generateFunctionalBuilding(scifi,recipe,{},4));
        bool parserRejected=false;
        try{
            (void)readFunctionalBuildingRecipe(
                Json::parse(R"({"features":[{"kind":"magic-gate"}]})"));
        }catch(const JsonError&){parserRejected=true;}
        CHECK(parserRejected);
        // Stage 12: two real storeys, rooms, interior passages and windows.
        // Glazing must replace wall material at its opening; glass collision
        // is allowed but no solid Wall graph piece may pass through the pane.
        const auto multi=readFunctionalBuildingRecipe(Json::parse(readFile(
            root+"/engine/assets/building-recipes/two-floor-workshop.json")));
        CHECK(multi.floors==2&&multi.roomsX==2&&multi.roomsZ==2);
        const auto multiGraph=buildFunctionalStructureGraph(multi);
        CHECK(multiGraph);
        CHECK(multiGraph.rooms.size()==8);
        CHECK(multiGraph.connections.size()==8);
        size_t floors=0,glass=0,partition=0,windowSockets=0;
        for(const auto& solid:multiGraph.solids){
            if(solid.part==BuildingGraphPart::Floor)++floors;
            if(solid.part==BuildingGraphPart::WindowPane)++glass;
            if(solid.part==BuildingGraphPart::Partition)++partition;
        }
        for(const auto& socket:multiGraph.sockets)
            if(socket.kind==BuildingFeatureKind::Window)++windowSockets;
        CHECK(floors==2);
        CHECK(glass==4&&windowSockets==4&&partition>0);
        auto upper=generateFunctionalBuilding(scifi,multi);
        CHECK(upper);
        CHECK(upper.building.boxes.size()==upper.graph.solids.size());
        CHECK(upper.building.mesh.triangles()>opened.building.mesh.triangles());
        CHECK(upper.building.gates.size()==2);
        CHECK(upper.graph.rooms.size()==8);
        CHECK(upper.graph.connections.size()==8);
        for(const auto& socket:upper.graph.sockets){
            if(socket.kind!=BuildingFeatureKind::Window)continue;
            const float local=socket.side==BuildingFeatureSide::Left?
                socket.z:socket.x;
            CHECK(local>=0&&local<=24);
            CHECK(socket.y>=0.9f);
        }
        // The exact centre of a room-to-room doorway must be free of
        // physical partition collision at standing pedestrian height.
        for(const auto& edge:upper.graph.connections){
            const float y=edge.y+1.05f;
            CHECK(!collisionAt(upper.building,edge.x,y,edge.z));
            CHECK(edge.width>=1.2f-0.001f);
        }
        // The upper-floor slab is present at its correct elevation, but
        // floors are NOT yet connected by stairs or lifts.
        const float storey=(multi.height-0.52f)/2.f;
        CHECK(collisionAt(upper.building,7.f,0.24f+storey+0.04f,7.f));
        const auto modernUpper=generateFunctionalBuilding(modern,multi);
        CHECK(modernUpper);
        CHECK(!sameGeometry(upper.building,modernUpper.building));
        CHECK(upper.building.boxes==modernUpper.building.boxes);
        auto invalidUpper=multi;
        for(auto& property:invalidUpper.features)
            if(property.kind==BuildingFeatureKind::Window){property.floor=2;break;}
        CHECK(!buildFunctionalStructureGraph(invalidUpper));
        invalidUpper=multi;invalidUpper.floors=7;
        CHECK(!buildFunctionalStructureGraph(invalidUpper));
        // Real cross-floor model from the same genre-neutral graph.
        const auto verticalRecipe=readFunctionalBuildingRecipe(Json::parse(readFile(
            root+"/engine/assets/building-recipes/vertical-circulation.json")));
        auto vertical=generateFunctionalBuilding(scifi,verticalRecipe);
        CHECK(vertical);
        CHECK(vertical.graph.connections.size()==10);
        CHECK(vertical.graph.rooms.size()==8);
        CHECK(vertical.building.mesh.triangles()>0);
        CHECK(vertical.building.boxes.size()==vertical.graph.solids.size());
        CHECK(vertical.anchors.size()==6); // two doors, two windows, two vertical cores
        int stair=0,lift=0;
        for(const auto& socket:vertical.anchors){
            if(socket.kind==BuildingFeatureKind::Stairs)++stair;
            if(socket.kind==BuildingFeatureKind::Elevator)++lift;
        }
        CHECK(stair==1&&lift==1);
        auto modernVertical=generateFunctionalBuilding(modern,verticalRecipe);
        CHECK(modernVertical);
        CHECK(!sameGeometry(vertical.building,modernVertical.building));
        CHECK(vertical.building.boxes==modernVertical.building.boxes);
        CHECK(vertical.graph.rooms.size()==modernVertical.graph.rooms.size());
        std::puts("building functional-feature tests passed");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building functional-feature tests FAILED: %s\n",e.what());
        return 1;
    }
}
