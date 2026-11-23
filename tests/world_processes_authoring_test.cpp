// Authorable map data must be validated before it creates canonical ECS state.
#include "aiverselib/world/WorldProcessesAuthoring.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

using namespace aiverse;
namespace ar=aiverse::lib::worldprocess::authoring;
int main(){
    AIVERSE_TEST_CHECK(ar::supports("EchoZone") &&
                      ar::supports("FractureMember") &&
                      ar::supports("AetherGasCell"));
    AIVERSE_TEST_CHECK(!ar::supports("AetherDuct")); // requires stable link mapping
    auto zone=ar::defaults("EchoZone");
    auto member=ar::defaults("FractureMember");
    auto gas=ar::defaults("AetherGasCell");
    AIVERSE_TEST_CHECK(ar::validate("EchoZone",zone));
    AIVERSE_TEST_CHECK(ar::validate("FractureMember",member));
    AIVERSE_TEST_CHECK(ar::validate("AetherGasCell",gas));
    std::string error;
    AIVERSE_TEST_CHECK(ar::edit("EchoZone","enabled",Json::fromBool(false),zone,&error));
    AIVERSE_TEST_CHECK(ar::edit("FractureMember","anchored",Json::fromBool(true),member,&error));
    AIVERSE_TEST_CHECK(ar::edit("AetherGasCell","smoke",Json::number(int64_t(250)),gas,&error));
    AIVERSE_TEST_CHECK(ar::edit("AetherGasCell","oxygen",Json::number(int64_t(500)),gas,&error));
    AIVERSE_TEST_CHECK(!ar::edit("AetherGasCell","sampledTick",Json::number(int64_t(1)),gas,&error));
    AIVERSE_TEST_CHECK(!ar::edit("FractureMember","detached",Json::fromBool(true),member,&error));
    AIVERSE_TEST_CHECK(!ar::edit("AetherGasCell","smoke",Json::number(int64_t(-1)),gas,&error));
    AIVERSE_TEST_CHECK(!ar::edit("AetherGasCell","smoke",Json::number(int64_t(1000000001)),gas,&error));
    AIVERSE_TEST_CHECK(!ar::edit("AetherGasCell","smoke",Json::numberText("2.5"),gas,&error));
    AIVERSE_TEST_CHECK(!ar::edit("AetherGasCell","smoke",Json::numberText("1e3"),gas,&error));
    AIVERSE_TEST_CHECK(!ar::edit("AetherGasCell","smoke",Json::string("2"),gas,&error));
    AIVERSE_TEST_CHECK(gas["smoke"].integer()==250);
    auto invalid=gas;
    invalid.set("unknown",Json::fromBool(true));
    AIVERSE_TEST_CHECK(!ar::validate("AetherGasCell",invalid,&error));
    invalid=gas;invalid.erase("oxygen");
    AIVERSE_TEST_CHECK(!ar::validate("AetherGasCell",invalid,&error));
    World w;
    const Entity a=w.create(),b=w.create(),c=w.create();
    AIVERSE_TEST_CHECK(ar::apply(w,a,"EchoZone",zone,&error));
    AIVERSE_TEST_CHECK(ar::apply(w,b,"FractureMember",member,&error));
    AIVERSE_TEST_CHECK(ar::apply(w,c,"AetherGasCell",gas,&error));
    AIVERSE_TEST_CHECK(!w.get<echo::AcousticZone>(a).enabled);
    const auto structural=w.get<fracture::Member>(b);
    AIVERSE_TEST_CHECK(structural.anchored&&!structural.broken&&!structural.detached);
    const auto& state=w.get<aether::GasCell>(c);
    AIVERSE_TEST_CHECK(state.smoke==250&&state.toxin==0&&state.oxygen==500&&
                      state.sampledTick==std::numeric_limits<uint32_t>::max());
    AIVERSE_TEST_CHECK(!ar::apply(w,a,"EchoZone",zone,&error)); // no duplicate owner
    AIVERSE_TEST_CHECK(!ar::apply(w,a,"AetherGasCell",invalid,&error));
    AIVERSE_TEST_CHECK(!w.has<aether::GasCell>(a));
    w.destroy(b);
    AIVERSE_TEST_CHECK(!ar::apply(w,b,"FractureMember",member,&error));
    World snapshot=w;
    w.get<aether::GasCell>(c).oxygen=1;
    AIVERSE_TEST_CHECK(snapshot.get<aether::GasCell>(c).oxygen==500);
    std::cout<<"PASS authored world-process component validation, ECS apply and no runtime state injection\n";
}
