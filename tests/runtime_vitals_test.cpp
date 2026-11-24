#include "apps/studio-native/StudioRuntimeEdit.h"
#include "engine/data/JsonCompose.h"
#include "tests/AlwaysCheck.h"
#include <memory>
#include <stdexcept>
#include <string>

using namespace aiverse;
namespace g=aiverse::game;
using aiverse::studio::StudioRuntimeEdit;
int main(){
    const std::string base=std::string(AIVERSE_TEMPLATES_DIR)+"/mushrooms/";
    Simulation sim({},std::make_unique<g::GameCore>());
    sim.setup(readComposedJson(base+"rules.json",fileReader()),Json::parseFile(base+"maps/duel.json"));
    Entity e=kNullEntity;
    sim.world().eachExistingConst<g::Unit>([&](Entity candidate,const g::Unit& u){
        if(!e.valid()&&u.hp.raw>Fixed::kOne&&sim.world().has<Transform>(candidate))e=candidate;
    });
    AIVERSE_TEST_CHECK(e.valid());
    const auto fields=StudioRuntimeEdit::describe(sim,e);
    AIVERSE_TEST_CHECK(fields.size()>0);
    const auto expected=std::to_string(sim.world().tryGetConst<g::Unit>(e)->hp.raw);
    auto reject=[&](uint32_t tick,const std::string& field,const std::string& raw,const std::string& val){
        const auto before=sim.world().tryGetConst<g::Unit>(e)->hp.raw;
        bool refused=false;
        try{StudioRuntimeEdit::apply(sim,e,tick,field,raw,val);}catch(const std::runtime_error&){refused=true;}
        AIVERSE_TEST_CHECK(refused);
        AIVERSE_TEST_CHECK(sim.world().tryGetConst<g::Unit>(e)->hp.raw==before);
    };
    reject(sim.tick()+1,"hp",expected,"1");
    reject(sim.tick(),"owner",expected,"1");
    reject(sim.tick(),"hp","0","1");
    reject(sim.tick(),"hp",expected,"0");
    reject(sim.tick(),"hp",expected,"9999999999999999999");
    reject(sim.tick(),"hp",expected,"1e3");
    const auto changed=StudioRuntimeEdit::apply(sim,e,sim.tick(),"hp",expected,"1");
    AIVERSE_TEST_CHECK(changed.getStr("raw")==std::to_string(Fixed::kOne));
    AIVERSE_TEST_CHECK(sim.world().tryGetConst<g::Unit>(e)->hp==Fixed::fromInt(1));
    reject(sim.tick(),"hp",expected,"1");
    sim.world().add<world::RegionDormant>(e);
    reject(sim.tick(),"hp",changed.getStr("raw"),"1");
    return 0;
}
