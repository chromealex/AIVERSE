#include "engine/world/TerraWeaveCavernsEcsCommand.h"
#include "engine/world/TerraWeaveCavernsPlanetBodyCut.h"
#include "engine/world/TerraWeaveCavernsPlanetDemand.h"
#include "engine/world/TerraWeaveCavernsPlanetMotion.h"
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::terraweave::caverns;

static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::shared_ptr<const game::Surface> makeSurface(){
    auto surface=std::make_shared<game::Surface>();
    game::BodyDef body;body.id="planet";body.radius=Fixed::fromInt(100);body.faceSize=16;
    surface->bodies.push_back(body);surface->build();return surface;
}
static Json region(uint64_t id,int face){
    CaveVolumeSnapshot volume;volume.settings.cellCm=1000;
    volume.settings.tunnelRadiusCm=100;volume.settings.chamberRadiusCm=250;
    volume.settings.minYcm=-600;volume.settings.maxYcm=-100;
    CavePlanetAnchor anchor{"planet",face,Fixed::fromInt(8),Fixed::fromInt(8),800};
    Json out=Json::object();out.set("version",Json::number(int64_t(1)));
    out.set("id",Json::string(std::to_string(id)));
    out.set("anchor",cavePlanetAnchorToJson(anchor));
    out.set("volume",caveSnapshotToJson(volume));return out;
}
static Json regions(bool overlap=false){
    Json out=Json::object(),list=Json::array();
    list.push(region(9,overlap?0:1));list.push(region(2,0));
    out.set("version",Json::number(int64_t(2)));
    out.set("activeRegionId",Json::string("9"));out.set("nextRegionId",Json::string("10"));
    out.set("regions",std::move(list));return out;
}
struct NativeHost final:GameModule {
    std::string name()const override{return "planet-cave-authority-test";}
    void setup(Simulation& sim,const Json&,const Json& map)override{
        registerPlanetCaveMap(sim,map[kPlanetCaveMapKey],makeSurface());
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
static std::unique_ptr<Simulation> make(Json doc){
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<NativeHost>());
    Json map=Json::object();map.set(kPlanetCaveMapKey,std::move(doc));
    sim->setup(Json::object(),map);return sim;
}
static void checkBodyCut(const std::shared_ptr<const game::Surface>& surface){
    const auto set=decodePlanetCaveMap(regions(),surface);
    CavePlanetBodyCutPlan body;
    body.bodyId="planet";body.surfaceRevision=7;body.surfaceEpoch=9;
    for(const auto& entry:set.regions){
        auto plan=std::make_shared<CavePlanetCutPlan>();
        plan->anchor=entry.region.anchor;plan->surfaceRevision=7;
        plan->surfaceEpoch=9;plan->authorityRevision=entry.authority.volume.sourceRevision;
        const auto frame=CavePlanetFrame::make(surface,plan->anchor);
        const auto one=Fixed::fromInt(1),minus=Fixed::fromInt(-1);
        const world::Triangle local{{minus,{},minus},{minus,{},one},{one,{},minus}};
        const world::Triangle native{frame.toBody(local.a),frame.toBody(local.b),frame.toBody(local.c)};
        CavePlanetCutSlot slot;slot.key={plan->anchor.face,0,0,1,0};
        slot.originalBody.push_back(native);slot.bodyTriangles.push_back(native);
        slot.chartTriangles.push_back(local);
        plan->slots.emplace(slot.key,slot);
        body.regions.emplace(entry.id,std::move(plan));
    }
    check(collectPlanetBodyCutSlots(body).size()==2,"aggregate leaf count");
    check(certifyPlanetBodyCut(body,surface).size()==2,"aggregate certificate");
    auto broken=body;
    auto conflict=std::make_shared<CavePlanetCutPlan>(*body.regions.at(9));
    conflict->slots.clear();
    const auto first=body.regions.at(2)->slots.begin()->second;
    conflict->slots.emplace(first.key,first);broken.regions[9]=conflict;
    bool refused=false;
    try{(void)collectPlanetBodyCutSlots(broken);}catch(const std::invalid_argument&){refused=true;}
    check(refused,"duplicate leaf accepted");
    broken=body;conflict=std::make_shared<CavePlanetCutPlan>(*body.regions.at(9));
    conflict->surfaceEpoch=8;broken.regions[9]=conflict;refused=false;
    try{(void)collectPlanetBodyCutSlots(broken);}catch(const std::invalid_argument&){refused=true;}
    check(refused,"mixed surface epoch accepted");
    broken=body;conflict=std::make_shared<CavePlanetCutPlan>(*body.regions.at(9));
    conflict->slots.begin()->second.bodyTriangles.clear();
    conflict->slots.begin()->second.chartTriangles.clear();
    broken.regions[9]=conflict;refused=false;
    try{(void)certifyPlanetBodyCut(broken,surface);}catch(const std::invalid_argument&){refused=true;}
    check(refused,"missing exterior accepted");
}
static void checkDemand(const std::shared_ptr<const game::Surface>& surface){
    const auto set=decodePlanetCaveMap(regions(),surface);
    CavePlanetDemandConfig cfg;
    const auto first=CavePlanetFrame::make(surface,set.regions.front().region.anchor);
    const auto second=CavePlanetFrame::make(surface,set.regions.back().region.anchor);
    std::vector<CavePlanetObserver> observers{{0,first.toSimulation({})},{0,second.toSimulation({})}};
    auto demand=selectPlanetCaveDemand(set,surface,observers,{},{},cfg);
    check(demand.bodies.at("planet")==std::vector<uint64_t>({2,9}),"two same-body demands");
    check(demand.volumeSlots==16,"global slot count");
    cfg.maxRegions=1;
    demand=selectPlanetCaveDemand(set,surface,observers,{}, {9},cfg);
    check(demand.bodies.at("planet")==std::vector<uint64_t>({9}),"pin priority");
    check(demand.refused==std::vector<uint64_t>({2})&&!demand.pinnedOverflow,"budget refusal");
    demand=selectPlanetCaveDemand(set,surface,observers,{}, {2,9},cfg);
    check(demand.pinnedOverflow,"pinned overflow must gate host");
    cfg.maxRegions=8;
    const auto local=first.originBody()+first.east()*Fixed::fromInt(20);
    observers={{0,game::Surface::anchor(0)+local}};
    demand=selectPlanetCaveDemand(set,surface,observers,{},{},cfg);
    check(demand.bodies.empty(),"enter hysteresis");
    demand=selectPlanetCaveDemand(set,surface,observers,{2},{},cfg);
    check(demand.bodies.at("planet")==std::vector<uint64_t>({2}),"leave hysteresis");
    observers.clear();
    check(selectPlanetCaveDemand(set,surface,observers,{2},{},cfg).bodies.empty(),"retirement demand");
}
static void checkPlanetMotion(const std::shared_ptr<const game::Surface>& surface){
    World world;
    physis::GravitySettings settings;settings.baseline={};
    world.setResource<physis::GravitySettings>(settings);
    physis::GravityField field;field.mode=physis::GravityMode::Attract;
    field.center=game::Surface::anchor(0);field.radiusMeters=200_fx;field.radialMagnitude=20_fx;
    const auto entity=world.create();world.add<physis::GravityField>(entity,field);
    for(int face=0;face<6;++face){
        const CavePlanetAnchor anchor{"planet",face,8_fx,8_fx,800};
        const auto frame=CavePlanetFrame::make(surface,anchor);
        const auto basis=cavePlanetMotionBasis(frame,world,frame.toSimulation({}));
        check(basis.groundedCompatible&&basis.up.dot(frame.up())>Fixed::fromRatio(99,100),"radial motion up");
        check(basis.magnitude>9_fx&&basis.magnitude<11_fx,"radial magnitude");
        lib::mechanics::MotionState state;
        lib::mechanics::MotionConfig cfg;
        const auto falling=stepCavePlanetMotion(basis,cfg,state,{},1,Fixed::fromRatio(1,20));
        check(falling.has_value()&&state.velocity.dot(basis.up)<Fixed{},"radial acceleration");
        world.get<physis::GravityField>(entity).mode=physis::GravityMode::Repel;
        const auto inverted=cavePlanetMotionBasis(frame,world,frame.toSimulation({}));
        check(inverted.groundedCompatible&&inverted.up.dot(frame.up())<Fixed::fromRatio(-99,100),"inverted support");
        world.get<physis::GravityField>(entity).mode=physis::GravityMode::Attract;
    }
    const auto frame=CavePlanetFrame::make(surface,{"planet",0,8_fx,8_fx,800});
    world.get<physis::GravityField>(entity).enabled=false;
    const auto zero=cavePlanetMotionBasis(frame,world,frame.toSimulation({}));
    lib::mechanics::MotionState state;
    check(!zero.groundedCompatible&&!stepCavePlanetMotion(zero,{},state,{},1,Fixed::fromRatio(1,20)),"zero gravity refuses grounded motor");
    check(!state.initialized,"refused step mutated state");
}
int main(){
    const auto surface=makeSurface();
    checkBodyCut(surface);
    checkDemand(surface);
    checkPlanetMotion(surface);
    const auto old=decodePlanetCaveMap(region(99,0),surface);
    check(old.regions.size()==1&&old.regions.front().id==1,"v1 migration");
    bool refused=false;
    try{(void)decodePlanetCaveMap(regions(true),surface);}
    catch(const std::invalid_argument&){refused=true;}
    check(refused,"overlapping v2 accepted");
    auto doc=regions();auto sim=make(doc);auto peer=make(doc);
    auto selected=doc;selected.set("activeRegionId",Json::string("2"));
    auto differentSelection=make(selected);
    check(sim->checksum()==differentSelection->checksum(),"editor selection leaked into gameplay hash");
    const auto* initial=sim->world().resourceConst<CavePlanetAuthoritySet>();
    check(initial&&initial->regions.size()==2&&initial->regions[0].id==2,"canonical order");
    SimSnapshot before;sim->saveState(before);
    const auto initialHash=sim->checksum();
    CaveStampCommand edit;edit.expectedRevision=1;
    edit.edits.push_back({CaveStampEditKind::Add,{0,{0,-350,0},100,CaveStampMode::Carve}});
    const auto change=applyAuthorizedPlanetCaveStampCommand(sim->world(),2,edit,sim->tick());
    check(change.changed&&change.newRevision==2,"region edit revision");
    check(freezePlanetCaveRegion(sim->world(),2)->stamps.size()==1,"edited region missing stamp");
    check(freezePlanetCaveRegion(sim->world(),9)->stamps.empty(),"other region changed");
    const auto hash=sim->checksum();check(hash!=initialHash,"hash ignored edit");
    refused=false;
    try{(void)applyAuthorizedPlanetCaveStampCommand(sim->world(),2,edit,sim->tick());}
    catch(const std::invalid_argument&){refused=true;}
    check(refused&&sim->checksum()==hash,"stale edit not atomic");
    const auto save=sim->serialize();check(!save.empty(),"empty save");
    std::string error;
    check(peer->deserialize(save,&error),"save restore");
    check(peer->checksum()==hash,"restored hash mismatch");
    check(freezePlanetCaveRegion(peer->world(),2)->stamps.size()==1,"restored stamp missing");
    sim->loadState(before);check(sim->checksum()==initialHash,"rollback hash mismatch");
    check(freezePlanetCaveRegion(sim->world(),2)->stamps.empty(),"rollback stamp retained");
    std::cout<<"PASS planetary cave region set, edit isolation, save and rollback\n";
}
