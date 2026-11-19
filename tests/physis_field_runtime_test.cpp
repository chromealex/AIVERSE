#include "engine/physis/PhysisFieldRuntime.h"
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(e) do { if(!(e)){std::printf("PHYSIS RUNTIME FAIL %s:%d: %s\n",__FILE__,__LINE__,#e);++failures;} } while(0)
constexpr uint16_t kFieldEditCmd=53123;

Json setupMap() {
    return Json::parse(R"({
      "physis":{
        "enabled":true,
        "grid":{"originX":0,"originZ":0,"width":2,"height":2,
                "cellSizeMeters":128,"seed":9},
        "climate":{"baseTemperature":17,"baseHumidity":0.5},
        "enableWindZones":true,"enableGravityFields":true,
        "enableStorms":true,"floodMode":"ExternalAuthority",
        "windZones":[
          {"id":"wind-a","center":[0,0,0],
           "radiusMeters":90,"velocity":[5,0,1]},
          {"id":"wind-b","center":[100,0,50],
           "radiusMeters":80,"velocity":[0,0,3]}
        ],
        "storms":[
          {"id":"storm-c","kind":"Tornado","center":[20,0,10],
           "eyeRadiusMeters":15,"outerRadiusMeters":180,
           "heightMeters":1000,"driftPerTick":[1,0,0]}
        ],
        "gravityFields":[
          {"id":"gravity-d","mode":"Attract","center":[0,80,0],
           "radiusMeters":200,"radialMagnitude":12}
        ]
      }
    })");
}
class EditableHost final:public GameModule {
public:
    std::string name() const override {return "physis-authoritative-test";}
    void setup(Simulation& sim,const Json&,const Json& map) override {
        if(!installEditableFromMap(sim,map))
            throw std::runtime_error("PHYSIS editable setup failed");
    }
    void applyCommand(Simulation& sim,const Command& command) override {
        if(command.type==kFieldEditCmd) {
            // Production host must authenticate system commands at network edge.
            (void)applyFieldEditCommand(sim,command,kFieldEditCmd);
        }
    }
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override{return 0;}
    bool serializeState(Simulation&,Archive& a) override{return a.ok();}
};
std::unique_ptr<Simulation> make() {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<EditableHost>());
    sim->setup(Json::object(),setupMap());
    return sim;
}
std::vector<AuthoredField> initial() {
    const auto map=setupMap();
    return parseFieldAssets(map["physis"],*optionsFromMap(map));
}
const FieldRuntimeState& registry(const Simulation& s) {
    return *s.world().resourceConst<FieldRuntimeState>();
}
Entity bound(const Simulation& s,const char* name) {
    const auto it=registry(s).byId.find(name);
    return it==registry(s).byId.end() ? kNullEntity:it->second.entity;
}
void formatAndValidation() {
    FieldEditBatch req{0,initial()};
    Command cmd;std::string error;
    CHECK(encodeFieldEditCommand(req,kFieldEditCmd,cmd,&error));
    CHECK(!cmd.args.empty() && cmd.args[0]>0 && cmd.type==kFieldEditCmd);
    FieldEditBatch decoded;
    CHECK(decodeFieldEditCommand(cmd,decoded,&error));
    CHECK(decoded.desired.size()==req.desired.size());
    CHECK(decoded.expectedRevision==0);
    // Validate the ACTUAL Simulation network/replay wire format (4096-word cap).
    ByteWriter writer;
    writeCommand(writer,cmd);
    ByteReader reader(writer.buf.data(),writer.buf.size());
    const Command fromWire=readCommand(reader);
    CHECK(reader.ok() && reader.atEnd());
    FieldEditBatch afterWire;
    CHECK(decodeFieldEditCommand(fromWire,afterWire,&error));
    CHECK(afterWire.desired.size()==req.desired.size());
    if(decoded.desired.size()==req.desired.size())
        for(size_t i=0;i<req.desired.size();++i)
            CHECK(authoredFingerprint(decoded.desired[i])==
                  authoredFingerprint(req.desired[i]));
    const auto initialState=decoded;
    cmd.args[0]=-1;
    CHECK(!decodeFieldEditCommand(cmd,decoded,&error));
    CHECK(decoded.desired.size()==initialState.desired.size());
    CHECK(encodeFieldEditCommand(req,kFieldEditCmd,cmd,&error));
    cmd.args.pop_back();
    CHECK(!decodeFieldEditCommand(cmd,decoded,&error));
    auto invalid=req;
    invalid.desired.front().id="bad id";
    CHECK(!encodeFieldEditCommand(invalid,kFieldEditCmd,cmd,&error));
    invalid=req;
    invalid.desired.front().id=invalid.desired[1].id;
    CHECK(!encodeFieldEditCommand(invalid,kFieldEditCmd,cmd,&error));
    invalid=req;
    invalid.desired.front().kind=AuthoredFieldKind::Storm;
    CHECK(!encodeFieldEditCommand(invalid,kFieldEditCmd,cmd,&error));
    // A valid but oversized field map must be rejected rather than encoded
    // beyond the Command.args cap and then silently truncated by readCommand.
    FieldEditBatch huge;
    for(int i=0;i<1024;++i) {
        AuthoredField f;
        f.id="large-"+std::to_string(i);
        f.kind=AuthoredFieldKind::Wind;
        WindZone value;
        value.velocity={Fixed::fromInt(3),Fixed{},Fixed{}};
        f.data=value;
        huge.desired.push_back(std::move(f));
    }
    std::sort(huge.desired.begin(),huge.desired.end(),
        [](const AuthoredField& a,const AuthoredField& b){return a.id<b.id;});
    CHECK(!encodeFieldEditCommand(huge,kFieldEditCmd,cmd,&error));
}
void replayIdentityAndRollback() {
    auto a=make(), b=make();
    CHECK(registry(*a).revision==0);
    CHECK(registry(*a).byId.size()==4);
    CHECK(a->checksum()==b->checksum());
    const auto windA=bound(*a,"wind-a");
    const auto windB=bound(*a,"wind-b");
    const auto storm=bound(*a,"storm-c");
    CHECK(a->world().alive(windA) && a->world().alive(storm));
    SimSnapshot before;
    a->saveState(before);
    FieldEditBatch edit{0,initial()};
    edit.desired.erase(std::remove_if(edit.desired.begin(),edit.desired.end(),
        [](const AuthoredField& x){return x.id=="wind-b";}),edit.desired.end());
    for(auto& source:edit.desired)
        if(source.id=="wind-a") {
            auto wind=std::get<WindZone>(source.data);
            wind.velocity.x=Fixed::fromInt(19);
            source.data=wind;
        }
    AuthoredField extra;
    extra.id="wind-new";extra.kind=AuthoredFieldKind::Wind;
    WindZone newZone;
    newZone.center={Fixed::fromInt(10),Fixed{},Fixed::fromInt(10)};
    newZone.velocity={Fixed::fromInt(2),Fixed{},Fixed{}};
    extra.data=newZone;
    edit.desired.push_back(extra);
    std::sort(edit.desired.begin(),edit.desired.end(),
              [](const auto& x,const auto& y){return x.id<y.id;});
    Command cmd;
    CHECK(encodeFieldEditCommand(edit,kFieldEditCmd,cmd));
    cmd.tick=0;cmd.seq=1;
    a->inject(cmd);b->inject(cmd);
    a->step();b->step();
    CHECK(a->checksum()==b->checksum());
    CHECK(registry(*a).revision==1);
    CHECK(bound(*a,"wind-a")==windA);
    CHECK(!a->world().alive(windB));
    CHECK(bound(*a,"wind-b")==kNullEntity);
    CHECK(a->world().get<WindZone>(windA).velocity.x==Fixed::fromInt(19));
    CHECK(bound(*a,"storm-c")==storm);
    CHECK(a->world().alive(bound(*a,"wind-new")));
    const uint64_t after=a->checksum();
    a->loadState(before);
    CHECK(registry(*a).revision==0);
    CHECK(bound(*a,"wind-a")==windA);
    CHECK(bound(*a,"wind-b")==windB);
    CHECK(a->world().alive(windB));
    a->inject(cmd);
    a->step();
    CHECK(a->checksum()==after);
    auto stale=cmd;
    stale.tick=a->tick();
    a->inject(stale);a->step();
    CHECK(registry(*a).revision==1);
    CHECK(a->checksum()==b->checksum() || a->tick()!=b->tick());
}
void multiEditRollbackReplay() {
    auto sim=make();
    SimSnapshot checkpoint;
    sim->saveState(checkpoint);
    const auto originalChecksum=sim->checksum();
    const Entity originalWind=bound(*sim,"wind-a");
    const Entity originalStorm=bound(*sim,"storm-c");

    FieldEditBatch first{0,initial()};
    for(auto& asset:first.desired) {
        if(asset.id=="wind-a") {
            auto zone=std::get<WindZone>(asset.data);
            zone.velocity.x=Fixed::fromInt(23);
            asset.data=zone;
        }
    }
    Command a;
    CHECK(encodeFieldEditCommand(first,kFieldEditCmd,a));
    a.tick=0;a.seq=1;
    sim->inject(a);
    sim->step();
    CHECK(registry(*sim).revision==1);
    CHECK(bound(*sim,"wind-a")==originalWind);

    FieldEditBatch second{1,first.desired};
    second.desired.erase(std::remove_if(second.desired.begin(),
        second.desired.end(),[](const AuthoredField& field) {
            return field.id=="storm-c";
        }),second.desired.end());
    Command b;
    CHECK(encodeFieldEditCommand(second,kFieldEditCmd,b));
    b.tick=1;b.seq=2;
    sim->inject(b);
    sim->step();
    CHECK(registry(*sim).revision==2);
    CHECK(bound(*sim,"storm-c")==kNullEntity);
    CHECK(!sim->world().alive(originalStorm));
    const auto committedChecksum=sim->checksum();
    const auto committedTick=sim->tick();

    sim->loadState(checkpoint);
    CHECK(sim->checksum()==originalChecksum);
    CHECK(registry(*sim).revision==0);
    CHECK(bound(*sim,"wind-a")==originalWind);
    CHECK(bound(*sim,"storm-c")==originalStorm);
    CHECK(sim->world().alive(originalStorm));

    sim->inject(a);
    sim->step();
    sim->inject(b);
    sim->step();
    CHECK(sim->tick()==committedTick);
    CHECK(registry(*sim).revision==2);
    CHECK(sim->checksum()==committedChecksum);

    // A replay of an already consumed revision cannot mutate the World.
    const auto beforeStale=sim->checksum();
    std::string error;
    a.tick=sim->tick();
    CHECK(!applyFieldEditCommand(*sim,a,kFieldEditCmd,&error));
    CHECK(sim->checksum()==beforeStale);
}
void kindChangeAndSave() {
    auto one=make();
    FieldEditBatch req{0,initial()};
    const Entity oldWind=bound(*one,"wind-a");
    for(auto& source:req.desired)if(source.id=="wind-a") {
        source.kind=AuthoredFieldKind::Gravity;
        GravityField g;
        g.mode=GravityMode::Uniform;
        g.acceleration={Fixed{},Fixed::fromInt(-4),Fixed{}};
        g.radiusMeters=Fixed::fromInt(30);
        source.data=g;
    }
    Command change;CHECK(encodeFieldEditCommand(req,kFieldEditCmd,change));
    change.tick=0;change.seq=1;
    one->inject(change);one->step();
    CHECK(registry(*one).revision==1);
    const Entity updated=bound(*one,"wind-a");
    CHECK(updated!=oldWind);
    CHECK(!one->world().alive(oldWind));
    CHECK(one->world().has<GravityField>(updated));
    CHECK(!one->world().has<WindZone>(updated));
    auto saved=one->serialize();
    auto restored=make();
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    CHECK(restored->checksum()==one->checksum());
    CHECK(bound(*restored,"wind-a")==updated);
    CHECK(registry(*restored).revision==1);
    one->step();restored->step();
    CHECK(one->checksum()==restored->checksum());
}

void submitViaSimulationCommandQueue() {
    auto sim=make();
    Json map=setupMap();
    Json config=map["physis"];
    Json winds=config["windZones"];
    Json wind=winds[0];
    wind.set("velocity",Json::parse("[31,0,0]"));
    winds.at(0)=wind;
    config.set("windZones",winds);
    map.set("physis",config);
    std::uint32_t scheduled=0;
    std::string error;
    CHECK(submitEditableFieldsFromMap(*sim,map,kFieldEditCmd,scheduled,&error));
    CHECK(scheduled>=sim->tick());
    const auto old=bound(*sim,"wind-a");
    while(sim->tick()<=scheduled) sim->step();
    CHECK(registry(*sim).revision==1);
    CHECK(bound(*sim,"wind-a")==old);
    CHECK(sim->world().get<WindZone>(old).velocity.x==Fixed::fromInt(31));
    const auto revision=registry(*sim).revision;
    Json bad=map;
    config=bad["physis"];
    winds=config["windZones"];
    wind=winds[0];
    wind.set("velocity",Json::parse("[301,0,0]"));
    winds.at(0)=wind;
    config.set("windZones",winds);
    bad.set("physis",config);
    CHECK(!submitEditableFieldsFromMap(*sim,bad,kFieldEditCmd,scheduled,&error));
    CHECK(registry(*sim).revision==revision);
    // Field edits are intentionally scoped: climate/grid rebuilds need a
    // separate authoritative migration and must not partially hot-reload.
    bad=setupMap();
    config=bad["physis"];
    Json grid=config["grid"];
    grid.set("seed",Json::number(int64_t(10)));
    config.set("grid",grid);
    bad.set("physis",config);
    CHECK(!submitEditableFieldsFromMap(*sim,bad,kFieldEditCmd,scheduled,&error));
    CHECK(registry(*sim).revision==revision);
}
class AlternateInstallHost final : public GameModule {
public:
    explicit AlternateInstallHost(bool tracked):tracked_(tracked){}
    std::string name() const override { return "physis-install-mode-test"; }
    void setup(Simulation& sim,const Json&,const Json& map) override {
        if(tracked_ ? !installTrackedFromMap(sim,map) : !installFromMap(sim,map))
            throw std::runtime_error("PHYSIS alternate install failed");
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&,Archive& a) override { return a.ok(); }
private:
    bool tracked_;
};
void rejectAlternateInstallers() {
    const auto map=setupMap();
    for(const bool tracked : {false,true}) {
        auto sim=std::make_unique<Simulation>(
            SimConfig{},std::make_unique<AlternateInstallHost>(tracked));
        sim->setup(Json::object(),map);
        const auto checksum=sim->checksum();
        const auto* marker=sim->world().resourceConst<AuthoredFieldInstallMode>();
        CHECK(marker!=nullptr);
        if(marker) CHECK(marker->mode==(tracked ?
            AuthoredFieldMode::Tracked : AuthoredFieldMode::Static));
        if(tracked)
            CHECK(sim->world().resourceConst<RuntimeFieldRegistry>()!=nullptr);
        bool rejected=false;
        try { (void)installEditableFromMap(*sim,map); }
        catch(const std::logic_error&) { rejected=true; }
        CHECK(rejected);
        CHECK(sim->checksum()==checksum);
        rejected=false;
        try { (void)installTrackedFromMap(*sim,map); }
        catch(const std::logic_error&) { rejected=true; }
        CHECK(rejected);
        CHECK(sim->checksum()==checksum);
        rejected=false;
        try { (void)installFromMap(*sim,map); }
        catch(const std::logic_error&) { rejected=true; }
        CHECK(rejected);
        CHECK(sim->checksum()==checksum);
    }
}
void staticSaveRestoreGuards() {
    const auto map=setupMap();
    const auto makeStatic=[&]() {
        auto sim=std::make_unique<Simulation>(
            SimConfig{},std::make_unique<AlternateInstallHost>(false));
        sim->setup(Json::object(),map);
        return sim;
    };
    auto source=makeStatic();
    CHECK(source->world().resourceConst<State>()!=nullptr);
    CHECK(source->world().resourceConst<FieldRuntimeState>()==nullptr);
    CHECK(source->world().resourceConst<RuntimeFieldRegistry>()==nullptr);
    const auto saved=source->serialize();
    auto restored=makeStatic();
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    CHECK(restored->checksum()==source->checksum());
    const auto baseline=restored->checksum();
    // No persistent field registry exists in Static mode. The restored
    // climate State alone must block every competing installer.
    bool rejected=false;
    try { (void)installFromMap(*restored,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(restored->checksum()==baseline);
    rejected=false;
    try { (void)installTrackedFromMap(*restored,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(restored->checksum()==baseline);
    rejected=false;
    try { (void)installEditableFromMap(*restored,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(restored->checksum()==baseline);
    source->step();
    restored->step();
    CHECK(restored->checksum()==source->checksum());
}
void trackedSaveRestoreGuards() {
    const auto map=setupMap();
    auto makeTracked=[]() {
        auto sim=std::make_unique<Simulation>(
            SimConfig{},std::make_unique<AlternateInstallHost>(true));
        sim->setup(Json::object(),setupMap());
        return sim;
    };
    auto source=makeTracked();
    const auto* before=source->world().resourceConst<RuntimeFieldRegistry>();
    CHECK(before!=nullptr);
    if(!before)return;
    CHECK(before->fields.size()==4);
    const auto saved=source->serialize();
    auto restored=makeTracked();
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    const auto* after=restored->world().resourceConst<RuntimeFieldRegistry>();
    CHECK(after!=nullptr);
    if(!after)return;
    CHECK(after->fields.size()==before->fields.size());
    for(std::size_t i=0;i<after->fields.size() && i<before->fields.size();++i) {
        CHECK(after->fields[i].id==before->fields[i].id);
        CHECK(after->fields[i].kind==before->fields[i].kind);
        CHECK(after->fields[i].entity==before->fields[i].entity);
    }
    CHECK(restored->checksum()==source->checksum());
    const auto baseline=restored->checksum();
    // The persisted tracked registry must reject competing installers.
    // A marker-free restore needs a separate World resource removal fixture
    // once the actual World resource API has been verified.
    bool rejected=false;
    try { (void)installEditableFromMap(*restored,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(restored->checksum()==baseline);
    rejected=false;
    try { (void)installTrackedFromMap(*restored,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(restored->checksum()==baseline);
    source->step();
    restored->step();
    CHECK(source->checksum()==restored->checksum());
}
void rejectCompetingInstallers() {
    auto sim=make();
    const auto baseline=sim->checksum();
    const auto count=registry(*sim).byId.size();
    const auto map=setupMap();
    bool rejected=false;
    try { (void)installEditableFromMap(*sim,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(sim->checksum()==baseline);
    CHECK(registry(*sim).byId.size()==count);
    rejected=false;
    try { (void)installTrackedFromMap(*sim,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(sim->checksum()==baseline);
    rejected=false;
    try { (void)installFromMap(*sim,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(sim->checksum()==baseline);
    // A serialized canonical registry survives restoration, while the
    // ephemeral setup marker may not. The persistent registry alone must
    // block competing tracked/static setup before any mutation.
    const auto saved=sim->serialize();
    auto restored=make();
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    CHECK(restored->world().resourceConst<FieldRuntimeState>()!=nullptr);
    const auto restoredChecksum=restored->checksum();
    rejected=false;
    try { (void)installTrackedFromMap(*restored,map); }
    catch(const std::logic_error&) { rejected=true; }
    CHECK(rejected);
    CHECK(restored->checksum()==restoredChecksum);
}
void authorityAndStale() {
    auto sim=make();
    Command cmd;
    FieldEditBatch edit{0,initial()};
    CHECK(encodeFieldEditCommand(edit,kFieldEditCmd,cmd));
    cmd.tick=sim->tick();
    const auto baseline=sim->checksum();
    cmd.player=1;
    std::string error;
    CHECK(!applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error));
    CHECK(sim->checksum()==baseline);
    cmd.player=0;cmd.tick=sim->tick()+1;
    CHECK(!applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error));
    CHECK(sim->checksum()==baseline);
    cmd.tick=sim->tick();
    CHECK(applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error)); // noop consumes revision
    CHECK(registry(*sim).revision==1);
    CHECK(!applyFieldEditCommand(*sim,cmd,kFieldEditCmd,&error));
    CHECK(registry(*sim).revision==1);
}
} // namespace
int main() {
    formatAndValidation();
    replayIdentityAndRollback();
    multiEditRollbackReplay();
    kindChangeAndSave();
    submitViaSimulationCommandQueue();
    authorityAndStale();
    rejectCompetingInstallers();
    rejectAlternateInstallers();
    trackedSaveRestoreGuards();
    staticSaveRestoreGuards();
    std::printf("PHYSIS field runtime: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
