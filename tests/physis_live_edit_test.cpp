#include "apps/editor/PhysisLiveEdit.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
using namespace aiverse;
using namespace aiverse::editor::physis_live;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(condition) do { if(!(condition)){std::printf("PHYSIS LIVE EDIT FAIL %s:%d %s\n",__FILE__,__LINE__,#condition);++failures;} } while(0)
constexpr uint16_t kEdit=51001;
Json initialMap() {
    return Json::parse(R"({"physis":{
       "enabled":true,
       "grid":{"originX":-2,"originZ":-2,"width":4,"height":4,
               "cellSizeMeters":80,"seed":13},
       "climate":{"baseTemperature":12,"baseHumidity":0.4},
       "enableWindZones":true,"enableGravityFields":true,"enableStorms":true,
       "floodMode":"ExternalAuthority",
       "windZones":[{"id":"wind-1","center":[0,0,0],
                     "radiusMeters":100,"velocity":[5,0,0]}],
       "storms":[{"id":"storm-1","kind":"Tornado","center":[0,0,40],
                  "eyeRadiusMeters":10,"outerRadiusMeters":200,
                  "heightMeters":500}],
       "gravityFields":[{"id":"gravity-1","mode":"Uniform",
                         "center":[0,20,0],"radiusMeters":70,
                         "acceleration":[0,-2,0]}]
    }})");
}
Json mapWithWind(double speed) {
    Json m=initialMap();
    Json config=m["physis"];
    Json winds=config["windZones"];
    Json w=winds[0];
    w.set("velocity",Json::parse(
        std::string("[")+std::to_string(speed)+",0,0]"));
    winds.at(0)=w;
    config.set("windZones",winds);
    m.set("physis",config);
    return m;
}
class Host final: public GameModule, public FieldEditHost {
    bool allowed_,applies_;
public:
    Host(bool allowed,bool applies):allowed_(allowed),applies_(applies){}
    std::string name() const override{return "physis-stage24-test-host";}
    void setup(Simulation& sim,const Json&,const Json& map) override {
        if(!installEditableFromMap(sim,map)) throw std::runtime_error("init failed");
    }
    void applyCommand(Simulation& sim,const Command& cmd) override {
        if(applies_ && cmd.type==kEdit) (void)applyFieldEditCommand(sim,cmd,kEdit);
    }
    bool allowLocalFieldEdits(const Simulation&) const override {return allowed_;}
    uint16_t fieldEditCommandType() const override {return kEdit;}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override{return 0;}
    bool serializeState(Simulation&,Archive& a) override {return a.ok();}
};
std::unique_ptr<Simulation> make(bool allowed=true,bool applies=true) {
    auto sim=std::make_unique<Simulation>(
        SimConfig{},std::make_unique<Host>(allowed,applies));
    sim->setup(Json::object(),initialMap());
    return sim;
}
void toTickAfter(Simulation& sim,uint32_t tick) {
    for(int i=0;sim.tick()<=tick && i<256;++i) sim.step();
}
void statusAndRollback() {
    auto sim=make();
    FieldEditSession state;
    state.observe(sim.get());
    CHECK(state.status==EditStatus::Idle);
    SimSnapshot original;
    sim->saveState(original);
    const auto* originalRegistry=sim->world().resourceConst<FieldRuntimeState>();
    const Entity windEntity=originalRegistry->byId.at("wind-1").entity;
    CHECK(state.submit(*sim,mapWithWind(22)));
    CHECK(state.status==EditStatus::Queued);
    state.observe(sim.get());
    CHECK(state.status==EditStatus::Queued);
    toTickAfter(*sim,state.scheduledTick);
    state.observe(sim.get());
    CHECK(state.status==EditStatus::Applied);
    CHECK(sim->world().get<WindZone>(windEntity).velocity.x==Fixed::fromInt(22));
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    // A subsequent trusted edit must not leave stale Applied status.
    FieldEditBatch later{1,parseFieldAssets(mapWithWind(41)["physis"],
        *optionsFromMap(initialMap()))};
    Command overwritten;
    CHECK(encodeFieldEditCommand(later,kEdit,overwritten));
    overwritten.tick=sim->tick();
    CHECK(applyFieldEditCommand(*sim,overwritten,kEdit));
    state.observe(sim.get());
    CHECK(state.status==EditStatus::Conflict);
    sim->loadState(original);
    state.observe(sim.get());
    CHECK(state.status==EditStatus::Conflict); // applied edit was rolled back
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==0);
    state.observe(nullptr);
    CHECK(state.status==EditStatus::Unavailable);
}
void permissionsAndNoHandler() {
    auto forbidden=make(false);
    FieldEditSession noPermission;
    noPermission.observe(forbidden.get());
    CHECK(noPermission.status==EditStatus::Unavailable);
    const auto checksum=forbidden->checksum();
    CHECK(!noPermission.submit(*forbidden,mapWithWind(18)));
    CHECK(forbidden->checksum()==checksum);
    auto missingHandler=make(true,false);
    FieldEditSession pending;
    pending.observe(missingHandler.get());
    CHECK(pending.submit(*missingHandler,mapWithWind(19)));
    toTickAfter(*missingHandler,pending.scheduledTick);
    pending.observe(missingHandler.get());
    CHECK(pending.status==EditStatus::Rejected);
    CHECK(missingHandler->world().resourceConst<FieldRuntimeState>()->revision==0);
    auto net=make();
    net->setCommandSink([](Command&&){return 7u;});
    CHECK(!canSubmit(net.get()));
    FieldEditSession remote;
    remote.observe(net.get());
    CHECK(!remote.submit(*net,mapWithWind(11)));
}
void conflictingEdits() {
    auto sim=make();
    FieldEditSession tracker;
    tracker.observe(sim.get());
    CHECK(tracker.submit(*sim,mapWithWind(16)));
    // A competing system command wins the same expected revision first.
    const auto opts=optionsFromMap(initialMap());
    FieldEditBatch other{0,parseFieldAssets(mapWithWind(27)["physis"],*opts)};
    Command competing;
    CHECK(encodeFieldEditCommand(other,kEdit,competing));
    competing.tick=tracker.scheduledTick;
    competing.seq=0; // sorts before locally scheduled seq=1
    sim->inject(competing);
    toTickAfter(*sim,tracker.scheduledTick);
    tracker.observe(sim.get());
    CHECK(tracker.status==EditStatus::Conflict);
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==1);
    const auto wind=sim->world().resourceConst<FieldRuntimeState>()->byId.at("wind-1").entity;
    CHECK(sim->world().get<WindZone>(wind).velocity.x==Fixed::fromInt(27));
}
void invalidAndOversize() {
    auto sim=make();
    FieldEditSession tracker;
    tracker.observe(sim.get());
    CHECK(!tracker.submit(*sim,mapWithWind(500)));
    CHECK(tracker.status==EditStatus::Rejected);
    CHECK(sim->world().resourceConst<FieldRuntimeState>()->revision==0);
}
struct Principal {bool editor=false;std::uint8_t slot=0;};
void serverIngress() {
    auto server=make();
    const auto opts=optionsFromMap(initialMap());
    FieldEditBatch desired{0,parseFieldAssets(mapWithWind(33)["physis"],*opts)};
    Command untrusted;
    CHECK(encodeFieldEditCommand(desired,kEdit,untrusted));
    untrusted.player=3;
    const auto auth=[](const Principal& user,const Command& cmd){
        return user.editor && user.slot==cmd.player;
    };
    std::uint32_t scheduled=0;
    std::string error;
    const auto baseline=server->checksum();
    CHECK(!admitAuthorizedFieldEdit(*server,untrusted,kEdit,
                                   Principal{false,3},auth,scheduled,&error));
    CHECK(server->checksum()==baseline);
    auto spoof=untrusted;
    spoof.player=0;
    CHECK(!admitAuthorizedFieldEdit(*server,spoof,kEdit,
                                   Principal{true,3},auth,scheduled,&error));
    CHECK(!admitAuthorizedFieldEdit(*server,untrusted,kEdit,
                                   Principal{true,4},auth,scheduled,&error));
    CHECK(admitAuthorizedFieldEdit(*server,untrusted,kEdit,
                                  Principal{true,3},auth,scheduled,&error));
    CHECK(server->checksum()==baseline); // queued, not applied yet
    toTickAfter(*server,scheduled);
    CHECK(server->world().resourceConst<FieldRuntimeState>()->revision==1);
    const auto wind=server->world().resourceConst<FieldRuntimeState>()->byId.at("wind-1").entity;
    CHECK(server->world().get<WindZone>(wind).velocity.x==Fixed::fromInt(33));
    CHECK(!admitAuthorizedFieldEdit(*server,untrusted,kEdit,
                                   Principal{true,3},auth,scheduled,&error)); // stale
}
}
int main(){
    statusAndRollback();
    permissionsAndNoHandler();
    conflictingEdits();
    invalidAndOversize();
    serverIngress();
    std::printf("PHYSIS Live Editing: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
