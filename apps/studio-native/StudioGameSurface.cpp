#include "apps/studio-native/StudioGameSurface.h"
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
    host_.reset();
    lifetime_->retire();
}
void StudioGameSurface::start(const StudioGameLaunch& plan,const std::string& assets) {
    error_.clear();launch_=plan;assets_=assets;matchObserved_=false;runtimeDebugEdited_=false;++runtimeEpoch_;
    host_->start(*device_,plan.project,assets,lifetime_->lease(),plan.map);
    if (plan.camera) { const auto& c=*plan.camera; host_->setStartCamera(c[0],c[1],c[2],c[3],c[4]); }
}
void StudioGameSurface::stop() { matchObserved_=false;runtimeDebugEdited_=false;++runtimeEpoch_; host_->suspendInput();host_->stop();host_->collectGpuRetirements(); }
void StudioGameSurface::setDevice(const StudioGameDevice& profile) {
    (void)profile.profile();host_->suspendInput();profile_=profile;
}
void StudioGameSurface::control(const std::string& action) {
    if (action=="restart") {
        if (!host_->active()) throw std::runtime_error("Start a game before restarting");
        start(launch_,assets_);return;
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
        if(match && !matchObserved_){++runtimeEpoch_;runtimeDebugEdited_=false;}
        matchObserved_=match;
        if (host_->quitRequested()) stop();
        if (!host_->errorText().empty()) error_=host_->errorText();
    } catch (const std::exception& e) {
        device_->endPass();device_->endFrame();error_=e.what();stop();
    }
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
// Explicit operator snapshot, not a per-frame scan or a durable-save claim.
Json StudioGameSurface::regions() const {
    const Simulation* sim=host_->inMatch()?host_->simulation():nullptr;
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
    auto* sim=host_->inMatch()&&host_->canControl()?host_->simulation():nullptr;
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
    out.set("paused",Json::fromBool(host_->paused()));
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
    return inspector_.entities(host_->inMatch()?host_->simulation():nullptr,std::to_string(runtimeEpoch_),offset,limit,query);
}
Json StudioGameSurface::inspect(const std::string& epoch,const std::string& ref) {
    auto* sim=host_->inMatch()?host_->simulation():nullptr;
    Json out=inspector_.inspect(sim,std::to_string(runtimeEpoch_),epoch,ref);
    out.set("runtimeDebugEdited",Json::fromBool(runtimeDebugEdited_));
    out.set("runtimeEditAvailable",Json::fromBool(host_->canStep()));
    if(host_->canStep()&&sim){
        const auto entity=editor::worldEntity(std::stoull(out.getStr("ref")));
        out.set("runtimeFields",StudioRuntimeEdit::describe(*sim,entity));
    }
    return out;
}
Json StudioGameSurface::editRuntime(const Json& params){
    // canStep means an editor-paused, controllable local match, not a game menu/network mirror.
    if(!host_->canStep()||!host_->inMatch())throw std::runtime_error("Pause the local match in Studio before editing");
    auto* sim=host_->simulation();
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
    out.set("active",Json::fromBool(host_->active()));
    out.set("paused",Json::fromBool(host_->paused()));
    out.set("runtimeDebugEdited",Json::fromBool(runtimeDebugEdited_));
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
