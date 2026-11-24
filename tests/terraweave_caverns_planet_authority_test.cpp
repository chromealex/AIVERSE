#include "engine/world/TerraWeaveCavernsEcsCommand.h"
#include "engine/world/TerraWeaveCavernsPlanetBodyCut.h"
#include "engine/world/TerraWeaveCavernsPlanetDemand.h"
#include "engine/world/TerraWeaveCavernsPlanetMotion.h"
#include "engine/world/TerraWeaveCavernsPlanetNavigation.h"
#include "engine/world/TerraWeaveCavernsGraphSolvers.h"
#include "engine/world/TerraWeaveCavernsNavigationGraph.h"
#include "engine/world/TerraWeaveCavernsPathRouter.h"
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
static void checkNavigationCoverage(const std::shared_ptr<const game::Surface>& surface){
    const CavePlanetAnchor anchor{"planet",0,8_fx,8_fx,800};
    CavePlanetDemandConfig cfg;
    auto keys=planetInteriorKeys(anchor,cfg);
    const auto coverage=CavePlanetNavigationCoverage::certify(anchor,cfg,keys);
    const auto cap=coverage.halfExtentCm();
    check(cap>0&&cap<anchor.halfExtentCm,"coverage smaller than chart");
    check(coverage.contains({-cap+1,-cap+1,-cap+1},{cap-1,cap-1,cap-1}),"interior covered");
    check(!coverage.contains({0,0,0},{cap,0,0}),"outer boundary rejected");
    check(!coverage.contains({1,0,0},{0,0,0}),"inverted bounds rejected");
    auto sim=make(regions());
    auto index=std::make_shared<const CaveSnapshotSpatialIndex>(freezePlanetCaveRegion(sim->world(),2));
    const auto frame=CavePlanetFrame::make(surface,anchor);
    auto frozen=std::make_shared<const World>();
    const auto provider=makeResidentCavePlanetNavigation(frame,index,frozen,7,coverage,
        [](PointCm)->std::optional<std::pair<int32_t,int32_t>>{return {};},{},{},2);
    CaveAgentProfile agent;agent.traversal=CaveTraversal::Flying;
    agent.radiusCm=0;agent.heightCm=1;
    check(provider->residentSegment({0,0,0},{0,0,0},agent),"resident provider interior");
    check(!provider->residentSegment({cap,0,0},{cap,0,0},agent),"provider rejected unmeshed chart");
    const CavePlanetNavigationLease lease{2,3,4,provider};
    check(lease.matches(2,3,4,index->revision(),7),"lease identity");
    check(!lease.matches(9,3,4,index->revision(),7),"wrong region");
    check(!lease.matches(2,4,4,index->revision(),7),"stale restore generation");
    check(!lease.matches(2,3,5,index->revision(),7),"stale native publication");
    check(!lease.matches(2,3,4,index->revision(),8),"stale environment");
    keys.erase(keys.begin());
    bool refused=false;
    try{(void)CavePlanetNavigationCoverage::certify(anchor,cfg,keys);}
    catch(const std::invalid_argument&){refused=true;}
    check(refused,"incomplete lattice accepted");
}
static void checkOrientedNavigation(const std::shared_ptr<const game::Surface>& surface){
    auto snapshot=std::make_shared<CaveVolumeSnapshot>();
    snapshot->settings.tunnelChance=0;snapshot->settings.chamberChance=0;
    snapshot->settings.minYcm=-750;snapshot->settings.maxYcm=0;
    snapshot->stamps.push_back({1,{0,-350,0},300,CaveStampMode::Carve});
    canonicalizeCaveSnapshot(*snapshot);
    const auto index=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);
    const auto frame=CavePlanetFrame::make(surface,{"planet",0,8_fx,8_fx,800});
    const auto provider=[&](Vec3 up,uint64_t scope=0){
        auto world=std::make_shared<World>();
        physis::GravitySettings settings;
        settings.baseline=frame.vectorToBody(up)*Fixed::fromInt(-10);
        world->setResource<physis::GravitySettings>(settings);
        return makeCavePlanetNavigationProvider(frame,index,world,1,{},
            [](PointCm lo,PointCm hi){
                return lo.x>=-799&&lo.y>=-799&&lo.z>=-799&&
                       hi.x<=799&&hi.y<=799&&hi.z<=799;
            },scope);
    };
    CaveAgentProfile agent;
    agent.radiusCm=10;agent.heightCm=100;agent.sampleCm=25;
    agent.groundProbeCm=75;agent.maxStepCm=10;
    const auto floor=provider({0_fx,1_fx,0_fx});
    const auto wall=provider({1_fx,0_fx,0_fx},2);
    const auto ceiling=provider({0_fx,-1_fx,0_fx});
    const auto zero=provider({});
    const PointCm wallStart{-250,-350,0},wallGoal{-250,-325,0};
    check(floor->sample({0,-600,0},agent).traversable,"floor support");
    check(wall->sample(wallStart,agent).traversable,"wall support");
    check(ceiling->sample({0,-100,0},agent).traversable,"ceiling support");
    check(!zero->sample(wallStart,agent).traversable,"zero gravity ground refused");
    check(!floor->sample(wallStart,agent).traversable,"wrong gravity support");
    check(!wall->chartYCompatible(wallStart,agent)&&floor->chartYCompatible({0,-600,0},agent),"native chart-Y guard");
    check(wall->canTraverse(wallStart,wallGoal,agent),"wall tangent edge");
    check(!wall->canTraverse(wallStart,wallGoal,agent,true),"native wall edge refused");
    check(!wall->canTraverse(wallStart,{-225,-350,0},agent),"gravity-axis step cap");
    check(!wall->residentSegment({-700,0,0},{-700,0,0},agent),"oriented footprint outside residency");
    CaveGraphGrid grid;grid.origin={-262,-362,-12};
    grid.width=1;grid.height=2;grid.depth=1;grid.cellCm=25;
    CaveNavigationGridView view(wall,grid,agent);
    std::vector<CaveGraphEdge> edges;view.neighbors(0,edges);
    check(edges.size()==1&&edges.front().to==1,"shared graph uses oriented edge");
    CavePathQuery query;query.start=wallStart;query.goal=wallGoal;
    query.sourceRevision=index->revision();query.agent=agent;
    CavePathResult route;route.sourceRevision=query.sourceRevision;
    route.environmentRevision=1;route.reached=true;route.waypoints={wallStart,wallGoal};
    check(caveRouteSegmentsValid(query,route,*wall),"oriented route accepted");
    check(!caveRouteSegmentsValid(query,route,*wall,true),"2.5D wall route refused");
    route.waypoints={{0,-350,0},wallGoal};
    check(!caveRouteSegmentsValid(query,route,*wall),"unsupported shortcut refused");
    CavePathRouter router;
    CaveGraphSolverSettings cfg;cfg.cellCm=10;cfg.regionMarginCells=2;
    cfg.hpaClusterCells=2;cfg.hybridThreshold=1;
    registerCaveSharedGraphSolvers(router,cfg);
    for(const auto* name:{"astar","hpa","flow","hybrid"}){
        const auto found=router.queryWithProvider(name,query,wall);
        check(found.reached,"shared oriented solver reached wall goal");
    }
    World ecs;
    const auto entity=ecs.create();
    check(nav::assignScopedCaveMotorGoal(ecs,entity,2,wallGoal,agent),"assign region scope");
    const auto routeResult=router.queryWithProvider("astar",query,wall);
    check(nav::assignCaveRoute(ecs,entity,routeResult,agent,25),"assign shared route");
    const nav::CaveWorldPositionSource position=[wallStart](const World&,Entity){return std::optional<PointCm>{wallStart};};
    const auto foreign=provider({1_fx,0_fx,0_fx},9);
    auto& request=ecs.get<nav::CaveSteeringRequest>(entity);
    request.valid=true;request.sourceRevision=wall->revision();request.environmentRevision=wall->environmentRevision();
    check(!nav::caveMotorRequest(ecs,entity,*foreign),"equal epochs wrong region refused");
    (void)nav::prepareScopedCaveSteering(ecs,[foreign](const World&,Entity){return foreign;},position);
    check(ecs.get<nav::CaveRouteFollower>(entity).needsRepath,"wrong region forces repath");
    check(!ecs.get<nav::CaveSteeringRequest>(entity).valid,"wrong region clears request");
    check(nav::assignScopedCaveMotorGoal(ecs,entity,9,wallGoal,agent),"switch scope");
    check(nav::caveRouteScope(ecs,entity)==9&&ecs.get<nav::CaveRouteFollower>(entity).waypoints.empty(),"scope switch discards old route");
}
int main(){
    const auto surface=makeSurface();
    checkBodyCut(surface);
    checkDemand(surface);
    checkPlanetMotion(surface);
    checkNavigationCoverage(surface);
    checkOrientedNavigation(surface);
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
