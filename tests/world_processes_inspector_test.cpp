// Studio/WorldInspect optional ECS read-only projection contract.
#include "aiverselib/world/WorldProcessesInspector.h"
#include "tests/AlwaysCheck.h"
#include <iostream>
using namespace aiverse;
namespace wi=aiverse::lib::worldprocess::inspector;
int main(){
    World w;
    Entity e=w.create(),other=w.create();
    Json components=Json::object(),schemas=Json::object();
    wi::appendWorldProcessComponents(w,e,components,schemas);
    AIVERSE_TEST_CHECK(components.size()==0&&schemas.size()==0);
    w.add<echo::AcousticZone>(e);
    w.add<echo::AcousticPortal>(e,echo::AcousticPortal{e,other,50,true,true});
    w.add<echo::AcousticEmitter>(e,echo::AcousticEmitter{e,100,true});
    w.add<echo::AcousticReceiver>(e,echo::AcousticReceiver{other,25,true});
    w.add<echo::AcousticHearing>(e,echo::AcousticHearing{other,42,7,true,true});
    w.add<fracture::Member>(e,fracture::Member{true,false,false});
    w.add<fracture::Bond>(e,fracture::Bond{e,other,100,20,true});
    w.add<fracture::Collapse>(e,fracture::Collapse{999,7,fracture::Delivery::Pending});
    w.add<aether::GasCell>(e,aether::GasCell{12,4,80});
    w.add<aether::GasDuct>(e,aether::GasDuct{e,other,10,false,true});
    w.add<aether::GasFlux>(e,aether::GasFlux{e,3,0,-2,true});
    w.add<aether::FireCoupling>(e,aether::FireCoupling{5,6,17,true});
    wi::appendWorldProcessComponents(w,e,components,schemas);
    AIVERSE_TEST_CHECK(components.size()==12&&schemas.size()==12);
    AIVERSE_TEST_CHECK(components["EchoHearing"]["intensity"].integer()==42);
    AIVERSE_TEST_CHECK(components["FractureBond"]["damage"].integer()==20);
    AIVERSE_TEST_CHECK(components["AetherGasCell"]["oxygen"].integer()==80);
    AIVERSE_TEST_CHECK(components["AetherFlux"]["oxygen"].integer()==-2);
    AIVERSE_TEST_CHECK(components["AetherFireCoupling"]["processedClimateTick"].str()=="17");
    AIVERSE_TEST_CHECK(components["EchoPortal"]["to"]["packed"].str()==
                       std::to_string(other.packed()));
    AIVERSE_TEST_CHECK(components["EchoPortal"]["to"]["alive"].boolean());
    // Real components only and all authoring fields are explicitly read-only.
    for(const auto& [key,value]:schemas.members()){
        AIVERSE_TEST_CHECK(components.has(key));
        AIVERSE_TEST_CHECK(value["fields"].isObject());
        for(const auto& [field,meta]:value["fields"].members()){
            AIVERSE_TEST_CHECK(components[key].has(field));
            AIVERSE_TEST_CHECK(meta["editable"].isNull()==false);
            AIVERSE_TEST_CHECK(!meta["editable"].boolean(true));
        }
    }
    auto sourceBefore=w.get<aether::GasCell>(e);
    const std::string before=components.dump(-1);
    wi::appendWorldProcessComponents(w,e,components,schemas);
    AIVERSE_TEST_CHECK(components.size()==12&&components.dump(-1)==before);
    AIVERSE_TEST_CHECK(w.get<aether::GasCell>(e).smoke==sourceBefore.smoke);
    // Old refs show their stale state and cannot silently resolve recycled IDs.
    w.destroy(other);
    Entity recycled=w.create();
    AIVERSE_TEST_CHECK(recycled.index==other.index&&recycled.gen!=other.gen);
    components=Json::object();schemas=Json::object();
    wi::appendWorldProcessComponents(w,e,components,schemas);
    AIVERSE_TEST_CHECK(!components["EchoPortal"]["to"]["alive"].boolean());
    // No ghost components on entities without pools or dead entities.
    Json empty=Json::object(),emptySchemas=Json::object();
    wi::appendWorldProcessComponents(w,recycled,empty,emptySchemas);
    AIVERSE_TEST_CHECK(empty.size()==0&&emptySchemas.size()==0);
    w.destroy(e);
    empty=Json::object();emptySchemas=Json::object();
    wi::appendWorldProcessComponents(w,e,empty,emptySchemas);
    AIVERSE_TEST_CHECK(empty.size()==0&&emptySchemas.size()==0);
    std::cout<<"PASS real World Processes ECS Inspector metadata, read-only, exact refs, no ghost components\n";
}
