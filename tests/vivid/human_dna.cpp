#include "engine/character/vivid/HumanDNA.h"
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::vivid;
namespace {
void expect(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
template<class F> void reject(F fn) {
    try {fn();}catch(const std::invalid_argument&){return;}
    throw std::runtime_error("invalid DNA was accepted");
}
void verify() {
    for(const auto* preset:{"neutral","warm","cool","stylized"}) {
        auto d=HumanDNA::preset(preset);
        d.validate();
        const auto json=d.toJson();
        expect(json["version"].integer()==1 && json["expressionRest"].size()==52,
               "wrong DNA schema");
        auto restored=HumanDNA::fromJson(Json::parse(json.dump(-1)));
        expect(restored.toJson()==json,"DNA JSON canonical roundtrip changed");
        expect(restored.rig.headBone=="head" && restored.groom.segments==8,
               "DNA rig/groom data lost");
    }
    auto d=HumanDNA::preset("neutral");
    d.anatomy.eyeScale=0.51f;
    d.skin.scatteringMm=2.7f;
    d.groom.guideCount=8192;
    d.groom.segments=32;
    d.expressionRest[0]=0.5f;
    d.validate();
    auto j=d.toJson();
    expect(HumanDNA::fromJson(j).expressionRest[0]==0.5f,
           "rest face channels truncated");
    reject([]{HumanDNA::preset("unknown");});
    reject([]{auto d=HumanDNA{};d.version=2;d.validate();});
    reject([]{auto d=HumanDNA{};d.groom.guideCount=8193;d.validate();});
    reject([]{auto d=HumanDNA{};d.groom.segments=1;d.validate();});
    reject([]{auto d=HumanDNA{};d.skin.scatteringMm=
                 std::numeric_limits<float>::quiet_NaN();d.validate();});
    reject([]{auto d=HumanDNA{};d.eyes.corneaIor=3.0f;d.validate();});
    reject([]{auto d=HumanDNA{};d.rig.jawBone="head";d.validate();});
    reject([]{auto d=HumanDNA{};d.rig.optionalBones={"head"};d.validate();});
    reject([]{HumanDNA::fromJson(Json::parse(R"({"version":1,"unknown":10})"));});
    reject([]{HumanDNA::fromJson(Json::parse(R"({"anatomy":{"jawWidth":"bad"}})"));});
    reject([]{HumanDNA::fromJson(Json::parse(R"({"eyes":{"pupil":0.99}})"));});
    reject([]{HumanDNA::fromJson(Json::parse(R"({"groom":{"guideCount":12.5}})"));});
    reject([]{HumanDNA::fromJson(Json::parse(R"({"expressionRest":[0,0]})"));});
    reject([]{HumanDNA::fromJson(Json::parse(R"({"rig":{"revision":3}})"));});
    expect(HumanDNA::fromJson(Json::parse(R"({})")).id=="vivid_default",
           "default DNA load failed");
}
}
int main(){try{verify();std::cout<<"VIVID Human DNA schema PASS\n";}
catch(const std::exception& e){std::cerr<<"VIVID Human DNA FAIL: "<<e.what()<<"\n";return 1;}return 0;}
