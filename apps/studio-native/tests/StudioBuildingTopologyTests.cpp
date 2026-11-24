#include "apps/studio-native/StudioBuildingTopology.h"
#include "tests/TestCheck.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::lib::systems;
int main(){
    FunctionalBuildingRecipe recipe;
    recipe.width=16;recipe.height=8;recipe.depth=12;
    recipe.floors=2;recipe.roomsX=2;recipe.roomsZ=1;
    auto graph=buildFunctionalStructureGraph(recipe);
    AIVERSE_TEST_CHECK(bool(graph));
    const auto separated=studio::studioBuildingTopology(graph);
    AIVERSE_TEST_CHECK(separated["rooms"].size()==4 && separated["connections"].size()==2);
    AIVERSE_TEST_CHECK(separated.getInt("components")==2 && separated.getInt("withoutElevatorsComponents")==2);
    BuildingFeature lift;
    lift.kind=BuildingFeatureKind::Elevator;lift.width=2;lift.depth=2;
    recipe.features.push_back(lift);
    graph=buildFunctionalStructureGraph(recipe);
    AIVERSE_TEST_CHECK(bool(graph));
    const auto connected=studio::studioBuildingTopology(graph);
    AIVERSE_TEST_CHECK(connected.getInt("components")==1 && connected.getInt("withoutElevatorsComponents")==2);
    bool foundLift=false;
    for(const auto& edge:connected["connections"].items()){
        if(edge.getStr("kind")!="elevator")continue;
        foundLift=true;
        AIVERSE_TEST_CHECK(edge.getStr("socketId")=="lift/0/0");
        AIVERSE_TEST_CHECK(edge.getStr("from")=="room/0/0/0" && edge.getStr("to")=="room/1/0/0");
    }
    AIVERSE_TEST_CHECK(foundLift);
    graph.connections.front().to="missing";
    bool refused=false;
    try{(void)studio::studioBuildingTopology(graph);}catch(const std::runtime_error&){refused=true;}
    AIVERSE_TEST_CHECK(refused);
    std::cout<<"Studio building topology PASS\n";
}
