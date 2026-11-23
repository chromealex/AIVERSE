// Isolated CPU execution probes for authoring. Not attached to a live game or GPU.
#pragma once
#include "AnimationOps.h"
#include "engine/render/AnimationGraph.h"
#include "engine/fx/SpectraFX.h"
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace aiverse::editor::graph_probe {
inline double ms(std::chrono::steady_clock::time_point start){
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
inline Json num(double v){if(!std::isfinite(v))return Json();return Json::number(v,6);}
inline Json capabilities(){
    return Json::parse(R"({"ok":true,"liveAttached":false,"maxSteps":60,"stepSeconds":0.0166666666666667,"domains":[{"domain":"motionweave","mode":"isolated-cpu","metrics":["state","stateTime","activeProgram","stateChanges","notifies","healthy","updateMs","evaluateMs"]},{"domain":"spectrafx","mode":"isolated-cpu","metrics":["particles","instances","dropped","culled","emitting","updateMs"]},{"domain":"shader","mode":"unavailable","reason":"no runtime shader node telemetry"},{"domain":"modeling","mode":"unavailable","reason":"use native build report"},{"domain":"strata","mode":"analysis-only","operation":"strata_model_analyze"}]})");
}
inline int steps(const Json& p){
    const auto& v=p["steps"];
    const auto n=p.getInt("steps",30);
    if((!v.isNull()&&(!v.isNumber()||v.toDouble()!=double(n)))||n<1||n>60)throw std::invalid_argument("steps must be integer 1..60");
    return int(n);
}
inline Json base(const std::string& domain,int n){
    Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("domain",Json::string(domain));
    r.set("scope",Json::string("isolated-cpu-preview"));r.set("liveAttached",Json::fromBool(false));
    r.set("gpuMeasured",Json::fromBool(false));r.set("requestedSteps",Json::number(int64_t(n)));return r;
}
inline Json motion(Workspace& ws,const Json& g,const Json& p,int n){
    if(!g.isObject()||g.dump(-1).size()>256*1024)throw std::invalid_argument("animation graph must fit 256 KiB");
    auto start=std::chrono::steady_clock::now();
    auto model=loadAnimationModel(ws,g.getStr("model"));
    if(model->nodes.size()>4096||model->vertices.size()>250000)throw std::length_error("animation probe model exceeds budget");
    auto compiled=render::AnimationGraph::compile(*model,g);
    render::AnimationGraphInstance instance;instance.reset(compiled);
    if(p.has("parameters")&&!p["parameters"].isObject())throw std::invalid_argument("parameters must be object");
    for(const auto& [name,value]:p["parameters"].members()){
        if(!compiled->defaults().values.count(name)||(!value.isNumber()&&value.type()!=Json::Type::Bool))throw std::invalid_argument("probe supports declared scalar parameters only");
        const double v=value.type()==Json::Type::Bool?double(value.boolean()):value.toDouble();
        if(!std::isfinite(v)||std::abs(v)>1000000||!instance.parameters().set(name,float(v)))throw std::invalid_argument("invalid scalar override");
    }
    const double setup=ms(start);
    Json frames=Json::array(),changes=Json::array();
    render::Pose pose;int completed=0;double update=0,evaluate=0;
    for(int i=0;i<n;++i){
        const auto before=instance.state();
        start=std::chrono::steady_clock::now();instance.update(1.f/60.f);const auto u=ms(start);
        start=std::chrono::steady_clock::now();instance.evaluate(pose);const auto e=ms(start);
        update+=u;evaluate+=e;++completed;
        Json f=Json::object();f.set("step",Json::number(int64_t(completed)));f.set("state",Json::string(instance.state()));
        f.set("stateTime",num(instance.stateTime()));f.set("updateMs",num(u));f.set("evaluateMs",num(e));
        Json program=Json::array();
        for(const auto& state:compiled->states())if(state.id==instance.state()){
            for(int idx:state.program)program.push(Json::string(compiled->nodes()[size_t(idx)].id));
            break;
        }
        f.set("activeProgram",program);
        const auto& notifies=instance.notifies();Json events=Json::array();
        for(size_t j=0;j<notifies.size()&&j<32;++j){
            const auto& event=notifies[j];Json item=Json::object();item.set("node",Json::string(event.node));item.set("name",Json::string(event.name));events.push(item);
        }
        f.set("notifyCount",Json::number(int64_t(notifies.size())));f.set("notifies",events);frames.push(f);
        if(before!=instance.state()){
            Json change=Json::object();change.set("step",Json::number(int64_t(completed)));change.set("from",Json::string(before));change.set("to",Json::string(instance.state()));changes.push(change);
        }
        if(!instance.healthy())break;
    }
    auto r=base("motionweave",n);r.set("setupMs",num(setup));r.set("updateMs",num(update));r.set("evaluateMs",num(evaluate));
    r.set("completedSteps",Json::number(int64_t(completed)));r.set("healthy",Json::fromBool(instance.healthy()));
    r.set("errorMessage",Json::string(instance.error()));r.set("frames",frames);r.set("stateChanges",changes);
    r.set("note",Json::string("Active program is the compiled pose dependency set after update, not per-node execution trace. State changes omit same-state restarts; no game/world environment or custom host callback registry."));
    return r;
}
inline Json fx(const Json& g,const Json& p,int n){
    if(!g.isObject()||g.dump(-1).size()>1024*1024)throw std::invalid_argument("FX graph must fit 1 MiB");
    if(p.has("parameters")&&!p["parameters"].isObject())throw std::invalid_argument("parameters must be object");
    auto start=std::chrono::steady_clock::now();
    spectra::Effect effect;std::vector<std::string> errors;
    if(!spectra::compileGraph(g,p["parameters"].isObject()?p["parameters"]:Json::object(),effect,errors)){
        auto r=base("spectrafx",n);r.set("ok",Json::fromBool(false));Json list=Json::array();
        for(const auto& error:errors)list.push(Json::string(error));r.set("errors",list);return r;
    }
    // Native headless backend, isolated from project assets, scene and gameplay.
    spectra::Runtime runtime;runtime.setQuality(aiverse::fx::FxQuality::Low);
    if(!runtime.init(nullptr))throw std::runtime_error("headless FX initialization failed");
    const int id=runtime.define("__graph_probe",effect);
    if(id<0)throw std::runtime_error("headless FX definition rejected");
    const auto handle=runtime.play(id,{0,0,0});
    if(!handle.valid())throw std::runtime_error("headless FX playback rejected");
    const double setup=ms(start);double total=0;Json frames=Json::array();
    for(int i=0;i<n;++i){
        start=std::chrono::steady_clock::now();runtime.update(1.f/60.f);const auto elapsed=ms(start);total+=elapsed;
        const auto& stats=runtime.stats();Json f=Json::object();
        f.set("step",Json::number(int64_t(i+1)));f.set("particles",Json::number(int64_t(stats.particles)));
        f.set("instances",Json::number(int64_t(stats.instances)));f.set("dropped",Json::number(int64_t(stats.dropped)));
        f.set("culled",Json::number(int64_t(stats.culled)));f.set("emitting",Json::fromBool(runtime.playing(handle)));
        f.set("updateMs",num(elapsed));frames.push(f);
    }
    runtime.shutdown();
    auto r=base("spectrafx",n);r.set("setupMs",num(setup));r.set("updateMs",num(total));r.set("completedSteps",Json::number(int64_t(n)));
    r.set("frames",frames);r.set("quality",Json::string("low"));
    r.set("note",Json::string("Actual isolated native particle updates; no GPU render, external effect/file loaders, world collisions, or per-node timing. Low quality native budgets apply."));
    return r;
}
} // namespace aiverse::editor::graph_probe
