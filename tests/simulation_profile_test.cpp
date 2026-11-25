#include "engine/sim/Simulation.h"
#include <cstdio>
#include <memory>

using namespace aiverse;
namespace {
int64_t clockValue=0;
int64_t clockNow(){return ++clockValue*10;}
struct Module final:GameModule {
    bool ready=true;
    std::string name()const override{return "profile-fixture";}
    void setup(Simulation& s,const Json&,const Json&)override{
        s.addSystem("one",[](Simulation&){});s.addSystem("two",[](Simulation&){});
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool readyToStep(Simulation&)override{return ready;}
};
}
#define CHECK(expr) do{if(!(expr)){std::fprintf(stderr,"Profile fixture line %d: %s\n",__LINE__,#expr);return 1;}}while(false)
int main(){
    auto mod=std::make_unique<Module>();auto* owner=mod.get();
    Simulation sim({},std::move(mod));sim.setup(Json::object(),Json::object());
    CHECK(!sim.beginProfileWindow(clockNow,0));
    CHECK(!sim.beginProfileWindow(clockNow,121));
    sim.setProfileClock(clockNow);
    CHECK(!sim.beginProfileWindow(clockNow,2));
    sim.step();CHECK(sim.systemTime(0)==10);
    sim.setProfileClock(nullptr);
    CHECK(sim.beginProfileWindow(clockNow,2));
    CHECK(!sim.beginProfileWindow(clockNow,1));
    owner->ready=false;sim.step();CHECK(sim.profileStepTimes().empty());
    owner->ready=true;
    sim.step();CHECK(sim.profileWindowActive());CHECK(sim.profileStepTimes().size()==1);
    CHECK(sim.systemTime(0)==10);CHECK(sim.systemTime(1)==10);
    CHECK(sim.profileStepTimes()[0]==50);
    sim.step();CHECK(!sim.profileWindowActive());CHECK(sim.profileStepTimes().size()==2);
    CHECK(sim.systemTime(0)==20);sim.step();CHECK(sim.systemTime(0)==20);
    CHECK(sim.beginProfileWindow(clockNow,4));sim.step();sim.endProfileWindow();
    CHECK(sim.profileStepTimes().size()==1);CHECK(!sim.profileWindowActive());sim.step();
    CHECK(sim.profileStepTimes().size()==1);
    CHECK(sim.beginProfileWindow(clockNow,4));sim.step();
    sim.insertSystem(0,"new",[](Simulation&){});
    CHECK(!sim.profileWindowActive());CHECK(sim.profileWindowTarget()==0);
    CHECK(sim.profileStepTimes().empty());
    // A capture only changes tooling state, never the gameplay checksum.
    auto peerMod=std::make_unique<Module>();
    Simulation a({},std::move(peerMod)),b({},std::make_unique<Module>());
    a.setup(Json::object(),Json::object());b.setup(Json::object(),Json::object());
    CHECK(a.beginProfileWindow(clockNow,2));
    a.step();b.step();CHECK(a.checksum()==b.checksum());
    a.step();b.step();CHECK(a.checksum()==b.checksum());
    return 0;
}
