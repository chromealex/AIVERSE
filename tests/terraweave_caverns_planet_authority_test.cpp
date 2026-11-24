#include "engine/world/TerraWeaveCavernsEcsCommand.h"
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
int main(){
    const auto surface=makeSurface();
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
