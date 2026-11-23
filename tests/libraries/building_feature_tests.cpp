// Genuine geometry + collision regression for independent functional
// properties. No genre, Studio, viewer or game-host source dependency.
#include <aiverselib/systems/BuildingFeatureGenerator.h>
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
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
        CHECK(graph.sockets[0].id=="entrance/0/0");
        CHECK(std::abs(graph.sockets[0].x-7.f)<0.001f);  // large gate pinned to front X=7
        CHECK(std::abs(graph.sockets[1].x-10.15f)<0.001f); // two back gates, 1m gap
        CHECK(std::abs(graph.sockets[2].x-13.85f)<0.001f);
        CHECK(std::abs(graph.sockets[3].x-18.f)<0.001f); // personnel door pinned at X=18
        CHECK(graph.sockets[4].id=="roof/3");
        CHECK(std::abs(graph.sockets[4].x-13.f)<0.001f);
        CHECK(std::abs(graph.sockets[4].z-10.f)<0.001f);

        // Mixed fixed and automatic positions: auto features fill free wall
        // space without displacing an exact portal or producing overlaps.
        auto mixed=recipe;
        mixed.features[2].position=-1.f;
        const auto mixedGraph=buildFunctionalStructureGraph(mixed);
        CHECK(mixedGraph);
        CHECK(mixedGraph.sockets.size()==5);
        CHECK(std::abs(mixedGraph.sockets[0].x-7.f)<0.001f);
        CHECK(std::abs(mixedGraph.sockets[3].x-1.32f)<0.001f);
        auto overlap=recipe;
        overlap.features[2].position=7.f; // same front wall span as large gate
        const auto badGraph=buildFunctionalStructureGraph(overlap);
        CHECK(!badGraph);
        CHECK(badGraph.error.find("overlap")!=std::string::npos);
        auto narrowRoof=recipe;
        narrowRoof.features[3].position=3.1f;
        CHECK(!buildFunctionalStructureGraph(narrowRoof)); // cannot retract off roof
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
        CHECK(sameGeometry(opened.building,modernVariant.building));
        CHECK(opened.building.boxes==modernVariant.building.boxes);
        CHECK(opened.building.mesh.materials.size()!=0);
        CHECK(modernVariant.building.mesh.materials.size()!=0);

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
        CHECK(sameGeometry(viaRuntime.building,modernVariant.building));
        auto viaRecipe=runtime.generateRecipe("scifi",recipe);
        CHECK(viaRecipe&&sameGeometry(viaRecipe.building,opened.building));

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
        std::puts("building functional-feature tests passed");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building functional-feature tests FAILED: %s\n",e.what());
        return 1;
    }
}
