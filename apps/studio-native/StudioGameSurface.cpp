#include "aiverselib/world/EvolveVisualization.h"
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include "engine/physis/Physis.h"
#include "apps/studio-native/StudioGameSurface.h"
#include "engine/sim/Simulation.h"
#include "engine/sim/CommonComponents.h"
#include "engine/game/GameCore.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::studio {
StudioGameSurface::StudioGameSurface(std::unique_ptr<gfx::Device> device):device_(std::move(device)) {
    if (!device_) throw std::runtime_error("Game GPU device unavailable");
    lifetime_=std::make_unique<render::RenderDeviceLifetime>(*device_);
    host_=std::make_unique<client::HostedClient>();
}
StudioGameSurface::~StudioGameSurface() {
    // HostedClient stops and drains by a real GPU idle/fence before device dies.
    host_.reset();
    lifetime_->retire();
}
void StudioGameSurface::start(const StudioGameLaunch& plan,const std::string& assets) {
    error_.clear();launch_=plan;assets_=assets;matchObserved_=false;++runtimeEpoch_;
    host_->start(*device_,plan.project,assets,lifetime_->lease(),plan.map);
    if (plan.camera) { const auto& c=*plan.camera; host_->setStartCamera(c[0],c[1],c[2],c[3],c[4]); }
}
void StudioGameSurface::stop() { matchObserved_=false;++runtimeEpoch_; host_->suspendInput();host_->stop();host_->collectGpuRetirements(); }
void StudioGameSurface::setDevice(const StudioGameDevice& profile) {
    (void)profile.profile();host_->suspendInput();profile_=profile;
}
void StudioGameSurface::control(const std::string& action) {
    if (action=="restart") {
        if (!host_->active()) throw std::runtime_error("Start a game before restarting");
        start(launch_,assets_);return;
    }
    if(action=="speed1"||action=="speed2"||action=="speed5"||action=="speed10"||action=="speed25"||action=="speed50"||action=="speed100"){
        if(!host_->setSpeed(std::stoi(action.substr(5))))throw std::runtime_error("Speed requires a local match");
        return;
    }
    const bool ok=action=="pause"?host_->setPaused(true):action=="resume"?host_->setPaused(false):
        action=="step"?host_->stepOnce():false;
    if (!ok) throw std::runtime_error("Control unavailable: use a loaded local match; Step requires editor Pause and a closed game menu");
}
void StudioGameSurface::suspend() { host_->suspendInput();host_->collectGpuRetirements(); }
void StudioGameSurface::event(const platform::Event& event) {
    if (!host_->active()) return;
    if (event.type==platform::EventType::FocusLost) { suspend();return; }
    if (event.type==platform::EventType::MouseDown || event.type==platform::EventType::FocusGained)
        host_->place(0,0,inputScale_,true);
    host_->event(event);
}
void StudioGameSurface::frame(int w,int h,float pw,float ph,double dt,bool focused) {
    host_->collectGpuRetirements();
    if (!host_->active() || w<2 || h<2) return;
    const auto logical=profile_.size(pw,ph);
    inputScale_=pw/std::max(1.0f,logical.first);
    host_->place(0,0,inputScale_,focused);
    device_->beginFrame(w,h);
    try {
        // A loading step may not draw yet; initialize the actual drawable.
        gfx::PassDesc clear;device_->beginPass(clear);device_->endPass();
        host_->frame(std::clamp(dt,0.0,0.1),{}, {},w,h,logical.first,logical.second,profile_.profile().touch,profile_.insets());
        device_->endFrame();
        const bool match=host_->inMatch();
        if(match && !matchObserved_)++runtimeEpoch_;
        matchObserved_=match;
        if (host_->quitRequested()) stop();
        if (!host_->errorText().empty()) error_=host_->errorText();
    } catch (const std::exception& e) {
        device_->endPass();device_->endFrame();error_=e.what();stop();
    }
}
Json StudioGameSurface::ecologyState(){
    Simulation* sim=host_&&host_->inMatch()?host_->simulation():nullptr;
    Json out=Json::object(),regions=Json::array();
    out.set("active",Json::fromBool(sim!=nullptr));
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
Json StudioGameSurface::mapEntityStates() {
    Simulation* sim=host_ && host_->inMatch()?host_->simulation():nullptr;
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
Json StudioGameSurface::entities(int offset,int limit,const std::string& query) {
    return inspector_.entities(host_->inMatch()?host_->simulation():nullptr,std::to_string(runtimeEpoch_),offset,limit,query);
}
Json StudioGameSurface::inspect(const std::string& epoch,const std::string& ref) {
    return inspector_.inspect(host_->inMatch()?host_->simulation():nullptr,std::to_string(runtimeEpoch_),epoch,ref);
}
Json StudioGameSurface::state() const {
    Json out=Json::object();out.set("device",profile_.json());
    out.set("active",Json::fromBool(host_->active()));
    out.set("paused",Json::fromBool(host_->paused()));
    out.set("canControl",Json::fromBool(host_->canControl()));
    out.set("canStep",Json::fromBool(host_->canStep()));
    out.set("loading",Json::fromBool(host_->loading()));
    out.set("progress",Json::number(double(host_->progress())));
    out.set("inMatch",Json::fromBool(host_->inMatch()));
    out.set("error",Json::string(error_));
    auto* sim=host_->simulation();
    out.set("tick",Json::number(int64_t(sim?sim->tick():0)));
    return out;
}
}
