#include "aiverselib/world/EvolveVisualization.h"
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include "engine/physis/Physis.h"
#include "apps/studio-native/StudioGameSurface.h"
#include "apps/studio-native/StudioPlanetWorldProcesses.h"
#include "apps/studio-native/StudioPlanetLiveSnapshot.h"
#include "apps/studio-native/StudioPlanetForestSnapshot.h"
#include "apps/studio-native/StudioPlanetLiveTerrain.h"
#include "apps/studio-native/StudioPlanetLiveBudget.h"
#include "apps/studio-native/StudioPlanetAetherSnapshot.h"
#include "apps/studio-native/StudioPlanetFractureSnapshot.h"
#include "apps/studio-native/StudioPlanetMorphSnapshot.h"
#include "apps/studio-native/StudioPlanetEchoSnapshot.h"
#include "apps/studio-native/StudioPlanetCrowdSnapshot.h"
#include "apps/studio-native/StudioPlanetCircuitSnapshot.h"
#include "apps/studio-native/StudioPlanetSocietySnapshot.h"
#include "engine/sim/Simulation.h"
#include "engine/sim/CommonComponents.h"
#include "engine/game/GameCore.h"
#include "engine/world/WorldPersistenceDiagnostics.h"
#include "engine/world/SimulationLod.h"
#include "apps/studio-native/StudioRuntimeEdit.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace aiverse::studio {
StudioGameSurface::StudioGameSurface(std::unique_ptr<gfx::Device> device):device_(std::move(device)) {
    if (!device_) throw std::runtime_error("Game GPU device unavailable");
    lifetime_=std::make_unique<render::RenderDeviceLifetime>(*device_);
    host_=std::make_unique<client::HostedClient>();
}
StudioGameSurface::~StudioGameSurface() {
    // HostedClient stops and drains by a real GPU idle/fence before device dies.
    actor_.reset();
    host_.reset();
    lifetime_->retire();
}
void StudioGameSurface::start(const StudioGameLaunch& plan,const std::string& assets) {
    error_.clear();launch_=plan;assets_=assets;matchObserved_=false;runtimeDebugEdited_=false;++runtimeEpoch_;worldProcessDiagnostics_.reset();
    if (plan.actorSession) {
        stop();
        actor_=client::HostedActorClient::open(*device_,plan.project,assets,plan.map.value_or(0));
        return;
    }
    actor_.reset();
    host_->start(*device_,plan.project,assets,lifetime_->lease(),plan.map);
    if (plan.camera) { const auto& c=*plan.camera; host_->setStartCamera(c[0],c[1],c[2],c[3],c[4]); }
}
void StudioGameSurface::stop() { actor_.reset();matchObserved_=false;runtimeDebugEdited_=false;++runtimeEpoch_;worldProcessDiagnostics_.reset(); host_->suspendInput();host_->stop();host_->collectGpuRetirements(); }
void StudioGameSurface::setDevice(const StudioGameDevice& profile) {
    (void)profile.profile();suspend();profile_=profile;
}
void StudioGameSurface::control(const std::string& action) {
    if (action=="restart") {
        if (!actor_ && !host_->active()) throw std::runtime_error("Start a game before restarting");
        start(launch_,assets_);return;
    }
    if (actor_) {
        if (action=="pause" || action=="resume") actor_->pause(action=="pause");
        else if (action=="step" && actor_->paused()) actor_->step();
        else throw std::runtime_error("Step requires a paused local actor session");
        return;
    }
    if(action=="checkpointSave"||action=="checkpointRestore"){
        if(!host_->checkpoint(action=="checkpointRestore"))throw std::runtime_error("Checkpoint requires a paused local match and valid saved state");
        if(action=="checkpointRestore"){++runtimeEpoch_;worldProcessDiagnostics_.reset();}
        return;
    }
    if(action=="speed1"||action=="speed2"||action=="speed5"||action=="speed10"||action=="speed25"||action=="speed50"||action=="speed100"){
        if(!host_->setSpeed(std::stoi(action.substr(5))))throw std::runtime_error("Speed requires a local match");
        return;
    }
    const bool ok=action=="pause"?host_->setPaused(true):action=="resume"?host_->setPaused(false):
        action=="step"?host_->stepOnce():false;
    if (!ok) throw std::runtime_error("Control unavailable: use a loaded local match; Step requires editor Pause and a closed game menu");
}
void StudioGameSurface::suspend() {
    if (actor_) {
        platform::Event lost{};lost.type=platform::EventType::FocusLost;
        actor_->event(lost);
    } else {
        host_->suspendInput();host_->collectGpuRetirements();
    }
}
void StudioGameSurface::event(const platform::Event& event) {
    if (actor_) {
        auto local=event;
        local.x/=inputScale_;local.y/=inputScale_;
        actor_->event(local);return;
    }
    if (!host_->active()) return;
    if (event.type==platform::EventType::FocusLost) { suspend();return; }
    if (event.type==platform::EventType::MouseDown || event.type==platform::EventType::FocusGained)
        host_->place(0,0,inputScale_,true);
    host_->event(event);
}
void StudioGameSurface::frame(int w,int h,float pw,float ph,double dt,bool focused) {
    host_->collectGpuRetirements();
    if ((!actor_ && !host_->active()) || w<2 || h<2) return;
    const auto logical=profile_.size(pw,ph);
    inputScale_=pw/std::max(1.0f,logical.first);
    if (actor_) {
        try {actor_->frame(w,h,logical.first,logical.second,std::clamp(dt,0.0,0.1));}
        catch(const std::exception& e) {error_=e.what();stop();}
        return;
    }
    host_->place(0,0,inputScale_,focused);
    device_->beginFrame(w,h);
    try {
        // A loading step may not draw yet; initialize the actual drawable.
        gfx::PassDesc clear;device_->beginPass(clear);device_->endPass();
        host_->frame(std::clamp(dt,0.0,0.1),{}, {},w,h,logical.first,logical.second,profile_.profile().touch,profile_.insets());
        device_->endFrame();
        const bool match=host_->inMatch();
        if(match && !matchObserved_){++runtimeEpoch_;runtimeDebugEdited_=false;}
        if(!match&&matchObserved_)worldProcessDiagnostics_.reset();
        matchObserved_=match;
        if (host_->quitRequested()) stop();
        if (!host_->errorText().empty()) error_=host_->errorText();
    } catch (const std::exception& e) {
        device_->endPass();device_->endFrame();error_=e.what();stop();
    }
}
Simulation* StudioGameSurface::liveSimulation() const {
    if (actor_) return &actor_->simulation();
    return host_->inMatch()?host_->simulation():nullptr;
}
bool StudioGameSurface::paused() const {return actor_?actor_->paused():host_->paused();}
bool StudioGameSurface::canControl() const {return actor_ || host_->canControl();}
bool StudioGameSurface::canStep() const {return actor_?actor_->paused():host_->canStep();}
Json StudioGameSurface::planetForestState(const std::string& focusBody,const std::optional<StudioForestCameraFocus>& camera){
    Simulation* sim=host_&&host_->inMatch()&&host_->canControl()?host_->simulation():nullptr;
    if(sim&&sim->tick()>0)if(const auto* game=dynamic_cast<const game::GameCore*>(&sim->module()))
        if(const auto* surface=game->surface(*sim))
            {
                const auto config=game->native().loadedConfiguration("world_processes");
                const auto weather=studioForestWeatherMappings(sim->world(),*surface,config?*config:Json{},game->matchMap());
                return sampleStudioPlanetForest(sim->world(),*surface,sim->tick()-1,128,weather,focusBody,camera);
            }
    Json out=Json::object();out.set("active",Json::fromBool(false));return out;
}
Json StudioGameSurface::worldProcessDiagnostics(int cap,const std::string& focus){
    // Restrict diagnostics to a loaded local match, no remote world inspection.
    Simulation* sim=host_&&host_->inMatch()&&host_->canControl()?host_->simulation():nullptr;
    if(sim)if(const auto* game=dynamic_cast<const game::GameCore*>(&sim->module()))
        if(const auto* surface=game->surface(*sim))
            return planet_wp::sample(worldProcessDiagnostics_,sim->world(),*surface,
                std::to_string(runtimeEpoch_),sim->tick(),cap,focus);
    return worldProcessDiagnostics_.sample(sim?&sim->world():nullptr,
        std::to_string(runtimeEpoch_),sim?sim->tick():0,cap,focus);
}
bool StudioGameSurface::planetMatch() const {
    auto* sim=host_&&host_->inMatch()?host_->simulation():nullptr;
    if(sim)if(const auto* game=dynamic_cast<const game::GameCore*>(&sim->module()))
        return game->surface(*sim)!=nullptr;
    return false;
}
bool StudioGameSurface::launchedMapCurrent() const {
    return host_&&launch_.map.has_value()&&host_->currentMapIndex()==launch_.map;
}
bool StudioGameSurface::planetSnapshotCurrent(const Json& snapshot) const {
    return host_&&host_->canControl()&&planetMatch()&&launchedMapCurrent()&&
        snapshot.getBool("active")&&snapshot.getStr("topology")=="planet"&&
        snapshot.getStr("epoch")==std::to_string(runtimeEpoch_);
}
Json StudioGameSurface::ecologyState(const StudioWorldProcessLiveOptions& options,const std::string& focusBody,const std::optional<StudioForestCameraFocus>& camera,Json* forestRender){
    Json previousForestRender;
    if(forestRender){previousForestRender=std::move(*forestRender);*forestRender=Json{};}
    Simulation* sim=host_&&host_->inMatch()?host_->simulation():nullptr;
    Json out=Json::object(),regions=Json::array();
    out.set("active",Json::fromBool(sim!=nullptr));
    if(sim)if(const auto* game=dynamic_cast<const game::GameCore*>(&sim->module()))
        if(const auto* surface=game->surface(*sim)){
            // Planar ecology rows cannot be interpreted as spherical positions.
            // No remote world inspection and no sample before the first completed tick.
            out.set("topology",Json::string("planet"));
            out.set("active",Json::fromBool(host_->canControl()));
            out.set("tick",Json::number(int64_t(sim->tick())));
            out.set("epoch",Json::string(std::to_string(runtimeEpoch_)));
            for(const char* key:{"regions","plants","weather","logs","soil"})
                out.set(key,Json::array());
            if(host_->canControl()&&sim->tick()>0){
                const bool ecology=!options.live||options.layer!="off";
                const bool granular=options.granularCellCm>0;
                const bool forestTerrain=options.forest&&!focusBody.empty();
                const size_t layers=size_t(ecology)*2+granular+options.gravity+options.fracture+forestTerrain+
                    options.echo+options.circuit+options.society+options.forest+options.crowd+options.morph+(options.aetherLayer!="off");
                if(layers){
                    PlanetLivePayloadBudget budget(layers);
                    const auto cap=[&](size_t n){return budget.ownerLimit(n);};
                    const auto tick=sim->tick()-1;
                    if(ecology){
                        budget.add(out,"imprint",samplePlanetImprint(sim->world(),*surface,tick,cap(1024)));
                        budget.add(out,"evolve",samplePlanetEvolve(sim->world(),*surface,tick,cap(64)));
                    }
                    if(options.gravity)budget.add(out,"gravity",samplePlanetGravity(sim->world(),*surface,tick));
                    if(options.fracture)budget.add(out,"fracture",samplePlanetFracture(sim->world(),*surface,tick,cap(256),cap(512)));
                    if(options.echo)budget.add(out,"echo",samplePlanetEcho(sim->world(),*surface,tick,cap(256),cap(512),cap(256),cap(256)));
                    if(options.circuit)budget.add(out,"circuit",samplePlanetCircuit(sim->world(),*surface,tick,cap(256),cap(512),cap(128)));
                    if(options.forest){
                        const auto config=game->native().loadedConfiguration("world_processes");
                        const auto weather=studioForestWeatherMappings(sim->world(),*surface,config?*config:Json{},game->matchMap());
                        budget.add(out,"forest",sampleStudioPlanetForest(sim->world(),*surface,tick,cap(kStudioForestDiagnosticLimit),weather,focusBody,camera));
                        // Native Scene only: independent budget, same completed tick and body.
                        if(forestRender&&!focusBody.empty()){
                            const bool sameEpoch=previousForestRender.getStr("epoch")==std::to_string(runtimeEpoch_);
                            *forestRender=sampleStudioPlanetForest(sim->world(),*surface,tick,kStudioForestRenderLimit,weather,focusBody,camera,true,sameEpoch?&previousForestRender:nullptr);
                            forestRender->set("epoch",Json::string(std::to_string(runtimeEpoch_)));
                            // Independent native terrain payload; never forwarded to MCP or browser.
                            if(forestTerrain)forestRender->set("nativeTerrain",sampleStudioPlanetTerrain(*surface,tick,focusBody,384u*1024u,camera,sameEpoch?&previousForestRender["nativeTerrain"]["grid"]:nullptr));
                        }
                        if(forestTerrain)budget.add(out,"forestTerrain",sampleStudioPlanetTerrain(*surface,tick,focusBody,planetLiveShare(kPlanetLivePayloadBytes-kPlanetLiveRootReserve,layers)));
                    }
                    if(options.society)budget.add(out,"society",samplePlanetSociety(sim->world(),*surface,tick,cap(256),cap(512),cap(128)));
                    if(options.crowd){
                        const auto config=game->native().loadedConfiguration("world_processes");
                        budget.add(out,"crowd",samplePlanetCrowd(sim->world(),*surface,tick,config&&config->getBool("crowdGravitySupport"),game,cap(256),cap(512),cap(2048)));
                    }
                    if(options.morph){
                        const auto config=game->native().loadedConfiguration("world_processes");
                        budget.add(out,"morph",samplePlanetMorph(sim->world(),*surface,tick,config&&config->getBool("morphGravity"),cap(256),cap(512)));
                    }
                    if(options.aetherLayer!="off"){
                        auto policy=AetherDiagnosticPolicy::Unknown;
                        if(const auto config=game->native().loadedConfiguration("world_processes"))
                            policy=config->getBool("aetherStratification")?AetherDiagnosticPolicy::Stratified:AetherDiagnosticPolicy::Diffusion;
                        budget.add(out,"aether",samplePlanetAether(sim->world(),*surface,tick,policy,cap(256),cap(512)));
                    }
                    if(granular)budget.add(out,"granular",samplePlanetGranular(sim->world(),*surface,tick,options.granularCellCm,cap(512)));
                    budget.finish(out);
                }
            }
            return out;
        }
    if(sim){
        out.set("tick",Json::number(int64_t(sim->tick())));
        for(const auto& v:evolve::visualRegions(sim->world())){
            Json r=Json::object();
            r.set("id",Json::string(std::to_string(v.bounds.region.packed())));
            r.set("minXcm",Json::number(v.bounds.minXcm));r.set("minZcm",Json::number(v.bounds.minZcm));
            r.set("maxXcm",Json::number(v.bounds.maxXcm));r.set("maxZcm",Json::number(v.bounds.maxZcm));
            r.set("fertility",Json::number(v.fertility));r.set("pollution",Json::number(v.pollution));
            r.set("succession",Json::number(v.succession));r.set("durability",Json::number(v.durability));
            regions.push(std::move(r));
        }
    }
    Json plants=Json::array(),weather=Json::array();
    if(sim){
        const auto& world=sim->world();
        if(const auto* authority=world.resourceConst<terraweave::TerraWeaveAuthorityResource>()){
            const auto* ecology=world.resourceConst<terraweave::ForestEcologyResource>();
            for(const auto& p:authority->rooted){
                if(plants.size()>=4096)break;
                Json row=Json::object();
                row.set("x",Json::number(p.anchor.xCm));row.set("z",Json::number(p.anchor.zCm));
                row.set("fallen",Json::fromBool(p.fallen));
                row.set("ageTicks",Json::string(std::to_string(p.environment.ageTicks)));
                row.set("matureTicks",Json::string(std::to_string(p.environment.matureTicks)));
                row.set("mature",Json::fromBool(p.environment.ageTicks>=p.environment.matureTicks));
                row.set("id",Json::string(std::to_string(p.anchor.speciesId)+":"+std::to_string(p.anchor.plantKey)));
                if(ecology){
                    const terraweave::AnchoredPlantId id{p.anchor.speciesId,p.anchor.plantKey};
                    const auto it=std::lower_bound(ecology->plants.begin(),ecology->plants.end(),id,
                        [](const auto& a,const auto& b){return a.id<b;});
                    if(it!=ecology->plants.end()&&it->id==id){
                        row.set("dead",Json::fromBool(it->dead));
                        row.set("recruited",Json::fromBool(it->recruited));
                        row.set("shadeStress",Json::string(std::to_string(it->shadeStressTicks)));
                        row.set("floodStress",Json::string(std::to_string(it->floodStressTicks)));
                        row.set("droughtStress",Json::string(std::to_string(it->droughtStressTicks)));
                    }
                }
                plants.push(std::move(row));
            }
            out.set("plantCount",Json::number(int64_t(authority->rooted.size())));
        }
        if(const auto* state=world.resourceConst<physis::State>()){
            for(int z=0;z<state->settings.height&&weather.size()<4096;++z)
                for(int x=0;x<state->settings.width&&weather.size()<4096;++x){
                    physis::Sample sample;
                    const int gx=x+state->settings.originX,gz=z+state->settings.originZ;
                    if(!physis::sample(world,gx,gz,sample))continue;
                    const int64_t size=int64_t(state->settings.cellSizeMeters)*100;
                    Json row=Json::object();
                    row.set("x",Json::number(gx*size));row.set("z",Json::number(gz*size));
                    row.set("size",Json::number(size));
                    row.set("rain",Json::number(sample.atmosphere.rain.toDouble(),3));
                    row.set("temperature",Json::number(sample.atmosphere.temperature.toDouble(),2));
                    row.set("wetness",Json::number(sample.surface.wetness.toDouble(),3));
                    weather.push(std::move(row));
                }
        }
    }
    Json logs=Json::array(),soil=Json::array();
    if(sim)if(const auto* d=sim->world().resourceConst<terraweave::ForestDeadwoodResource>()){
        for(const auto& log:d->logs){
            if(logs.size()>=4096)break;
            if(!log.remainingUnits)continue;
            Json row=Json::object();
            row.set("x",Json::number(log.xCm));row.set("z",Json::number(log.zCm));
            row.set("ratio",Json::number(log.initialUnits?double(log.remainingUnits)/double(log.initialUnits):0.,4));
            logs.push(std::move(row));
        }
        for(const auto& cell:d->soil){
            if(soil.size()>=4096)break;
            Json row=Json::object();
            row.set("x",Json::number(cell.x*terraweave::ForestDeadwoodResource::kNutrientCellCm));
            row.set("z",Json::number(cell.z*terraweave::ForestDeadwoodResource::kNutrientCellCm));
            row.set("units",Json::string(std::to_string(cell.availableUnits)));
            soil.push(std::move(row));
        }
        out.set("absorbedUnits",Json::string(std::to_string(d->totalAbsorbedUnits)));
        out.set("respiredUnits",Json::string(std::to_string(d->totalRespiredUnits)));
    }
    out.set("logs",std::move(logs));out.set("soil",std::move(soil));
    out.set("plants",std::move(plants));out.set("weather",std::move(weather));
    out.set("regions",std::move(regions));return out;
}
std::function<Json(uint64_t)> StudioGameSurface::buildingGraphReader(){
    auto* sim=liveSimulation();
    return sim?sim->openDiagnosticSource("building.graph"):std::function<Json(uint64_t)>{};
}
bool StudioGameSurface::canReview(const editor::Workspace& ws) const {
    return !actor_ && host_->canStep()&&studioPlaySourceMatches(launch_,ws)&&host_->currentMapIndex()==launch_.map;
}
Json StudioGameSurface::mapEntityStates() {
    Simulation* sim=liveSimulation();
    auto* mod=sim?dynamic_cast<game::GameCore*>(&sim->module()):nullptr;
    if(!mod)throw std::runtime_error("No game with map entities is running");
    auto& w=sim->world();
    Json out=Json::array();
    const auto& placed=mod->mapPlaced();
    for(size_t i=0;i<placed.size();++i){
        const Entity e=placed[i];
        if(!e.valid())continue;   // static collider: not a live entity
        Json o=Json::object();
        o.set("index",Json::number(int64_t(i)));
        if(!w.alive(e)){o.set("removed",Json::fromBool(true));out.push(std::move(o));continue;}
        if(const auto* u=w.tryGet<game::Unit>(e))o.set("type",Json::string(mod->rules().types[u->type].id));
        if(const auto* t=w.tryGet<Transform>(e)){o.set("x",Json::number(t->pos.x.toDouble(),3));o.set("z",Json::number(t->pos.z.toDouble(),3));}
        if(const auto* rs=w.tryGet<game::ResourceStock>(e);rs&&!rs->infinite){
            o.set("amount",Json::number(rs->amount.toDouble(),2));
            if(const auto* u=w.tryGet<game::Unit>(e))o.set("defaultAmount",Json::number(mod->rules().types[u->type].resourceAmount.toDouble(),2));
        }
        out.push(std::move(o));
    }
    Json r=Json::object();r.set("entities",std::move(out));r.set("tick",Json::number(int64_t(sim->tick())));
    return r;
}
// Explicit operator snapshot, not a per-frame scan or a durable-save claim.
Json StudioGameSurface::regions() const {
    const Simulation* sim=liveSimulation();
    Json out=Json::object();
    const auto* ledger=sim?sim->world().resourceConst<world::RegionEntityLedger>():nullptr;
    if(ledger) {
        out=world::ledgerStatus(*ledger,sim->world());
        if(out.has("error"))throw std::runtime_error(out.getStr("error"));
        out.set("durabilityKnown",Json::fromBool(false));
        out.set("simulationLod",world::simulationLodDiagnosticsJson(sim->world().resourceConst<world::SimulationLodDiagnostics>()));
    }else{
        out.set("reason",Json::string(sim?"Region ownership is not enabled in this match":"No local match is running"));
    }
    out.set("active",Json::fromBool(sim!=nullptr));
    out.set("available",Json::fromBool(ledger!=nullptr));
    out.set("epoch",Json::string(std::to_string(runtimeEpoch_)));
    out.set("tick",Json::number(int64_t(sim?sim->tick():0)));
    return out;
}
Json StudioGameSurface::profile(const std::string& action,uint32_t steps) {
    Json out=Json::object();
    out.set("epoch",Json::string(std::to_string(runtimeEpoch_)));
    auto* sim=canControl()?liveSimulation():nullptr;
    out.set("available",Json::fromBool(sim!=nullptr));
    if(!sim){
        if(action!="status")throw std::runtime_error("Profiling requires a controllable local match");
        out.set("reason",Json::string("Start a controllable local match"));return out;
    }
    if(action=="start"){
        const auto clock=[]()->int64_t{return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();};
        if(!sim->beginProfileWindow(clock,steps))throw std::runtime_error("Profiler is busy or capture size invalid");
    }else if(action=="stop")sim->endProfileWindow();
    out.set("active",Json::fromBool(sim->profileWindowActive()));
    out.set("target",Json::number(int64_t(sim->profileWindowTarget())));
    out.set("tick",Json::number(int64_t(sim->tick())));
    out.set("tickRate",Json::number(int64_t(sim->tickRate())));
    out.set("paused",Json::fromBool(paused()));
    Json history=Json::array(),rows=Json::array();
    double totalMs=0,systemsMs=0;
    for(const auto ns:sim->profileStepTimes()){
        const double ms=double(ns)/1000000.0;history.push(Json::number(ms,6));totalMs+=ms;
    }
    // Return the first 256 systems in schedule order. Sum covers all registered systems.
    const auto count=sim->profileWindowTarget()?sim->systemCount():size_t(0);
    for(size_t i=0;i<count;++i){
        const double ms=double(sim->systemTime(i))/1000000.0;systemsMs+=ms;
        if(i>=256)continue;
        Json row=Json::object();row.set("index",Json::number(int64_t(i)));
        row.set("name",Json::string(sim->systemName(i)));row.set("totalMs",Json::number(ms,6));rows.push(std::move(row));
    }
    out.set("historyMs",std::move(history));out.set("rows",std::move(rows));
    out.set("systemCount",Json::number(int64_t(count)));
    out.set("totalMs",Json::number(totalMs,6));out.set("systemsMs",Json::number(systemsMs,6));
    return out;
}
Json StudioGameSurface::entities(int offset,int limit,const std::string& query) {
    return inspector_.entities(liveSimulation(),std::to_string(runtimeEpoch_),offset,limit,query);
}
Json StudioGameSurface::inspect(const std::string& epoch,const std::string& ref) {
    auto* sim=liveSimulation();
    Json out=inspector_.inspect(sim,std::to_string(runtimeEpoch_),epoch,ref);
    out.set("runtimeDebugEdited",Json::fromBool(runtimeDebugEdited_));
    out.set("runtimeEditAvailable",Json::fromBool(canStep()));
    if(canStep()&&sim){
        const auto entity=editor::worldEntity(std::stoull(out.getStr("ref")));
        out.set("runtimeFields",StudioRuntimeEdit::describe(*sim,entity));
    }
    return out;
}
Json StudioGameSurface::editRuntime(const Json& params){
    // canStep means an editor-paused, controllable local match, not a game menu/network mirror.
    if(!canStep()||!liveSimulation())throw std::runtime_error("Pause the local match in Studio before editing");
    auto* sim=liveSimulation();
    if(!sim||params.getStr("epoch")!=std::to_string(runtimeEpoch_))throw std::runtime_error("Runtime session changed");
    const auto ref=params.getStr("ref");uint64_t packed=0;
    if(ref.empty()||ref.size()>20)throw std::runtime_error("Invalid runtime reference");
    const auto parsed=std::from_chars(ref.data(),ref.data()+ref.size(),packed);
    if(parsed.ec!=std::errc{}||parsed.ptr!=ref.data()+ref.size()||!packed)throw std::runtime_error("Invalid runtime reference");
    const auto tick=params["expectedTick"].toDouble();
    if(!params["expectedTick"].isNumber()||!(tick>=0&&tick<=4294967295.0)||double(uint32_t(tick))!=tick)
        throw std::runtime_error("Invalid expected tick");
    if(!params["field"].isString()||!params["expectedRaw"].isString()||!params["value"].isString())
        throw std::runtime_error("Invalid runtime edit parameters");
    const auto changed=StudioRuntimeEdit::apply(*sim,editor::worldEntity(packed),uint32_t(tick),
        params.getStr("field"),params.getStr("expectedRaw"),params.getStr("value"));
    runtimeDebugEdited_=true;
    Json out=Json::object();out.set("field",changed);out.set("runtimeDebugEdited",Json::fromBool(true));return out;
}
Json StudioGameSurface::state() const {
    Json out=Json::object();out.set("device",profile_.json());
    out.set("active",Json::fromBool(actor_ || host_->active()));
    out.set("epoch",Json::string(std::to_string(runtimeEpoch_)));
    out.set("paused",Json::fromBool(paused()));
    out.set("runtimeDebugEdited",Json::fromBool(runtimeDebugEdited_));
    out.set("canControl",Json::fromBool(canControl()));
    out.set("canStep",Json::fromBool(canStep()));
    out.set("loading",Json::fromBool(!actor_ && host_->loading()));
    out.set("progress",Json::number(actor_?1.0:double(host_->progress())));
    out.set("inMatch",Json::fromBool(liveSimulation()!=nullptr));
    out.set("error",Json::string(error_));
    auto* sim=liveSimulation();
    out.set("tick",Json::number(int64_t(sim?sim->tick():0)));
    return out;
}
}
