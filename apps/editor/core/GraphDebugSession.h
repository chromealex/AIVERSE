// Owner-thread isolated debugger; never attaches to or mutates gameplay.
#pragma once
#include "GraphRuntimeProbe.h"
#include "engine/render/graph/GraphTelemetry.h"
#include <memory>
#include <set>
#include <limits>

namespace aiverse::editor::graph_debug {
class Session {
    std::unique_ptr<render::Model> model_;
    std::shared_ptr<const render::AnimationGraph> compiled_;
    render::AnimationGraphInstance motion_;
    std::unique_ptr<spectra::Runtime> fx_;
    spectra::Handle handle_;
    graph_telemetry::History history_;
    graph_telemetry::Identity identity_;
    std::string project_, stopReason_="not-started";
    std::set<std::string> states_, nodes_;
    uint64_t serial_=0, step_=0;
    bool active_=false;
public:
    void stop(){
        active_=false;history_.clear();fx_.reset();motion_.reset(nullptr);compiled_.reset();model_.reset();
        states_.clear();nodes_.clear();step_=0;stopReason_="stopped";project_.clear();
    }
    void checkProject(const Workspace& ws){if(active_&&project_!=ws.project.dir)stop();}
    Json status() const{
        Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("scope",Json::string("isolated-cpu-debugger"));
        r.set("active",Json::fromBool(active_));r.set("liveAttached",Json::fromBool(false));r.set("gpuMeasured",Json::fromBool(false));
        r.set("reason",Json::string(stopReason_));
        if(active_){
            r.set("session",Json::string(identity_.entity));r.set("domain",Json::string(identity_.domain));
            r.set("program",Json::string(identity_.program));r.set("step",Json::number(int64_t(step_)));
            r.set("frames",history_.read(identity_));
        }
        return r;
    }
    void requireSession(const Json& p) const{
        if(!active_||p.getStr("session")!=identity_.entity)throw std::runtime_error("debug session expired; start a new isolated session");
    }
    Json start(Workspace& ws,const Json& p){
        if(active_)throw std::runtime_error("stop existing debug session first");
        const auto domain=p.getStr("domain");const auto& g=p["graph"];
        if(ws.project.dir.empty())throw std::invalid_argument("open a project first");
        if(!g.isObject()||g.dump(-1).size()>(domain=="motionweave"?256*1024:1024*1024))throw std::invalid_argument("invalid graph or size budget");
        if(serial_==std::numeric_limits<uint64_t>::max())throw std::overflow_error("session identity exhausted");
        try{
            if(domain=="motionweave"){
                model_=loadAnimationModel(ws,g.getStr("model"));
                if(model_->nodes.size()>4096||model_->vertices.size()>250000)throw std::length_error("animation model exceeds probe budget");
                compiled_=render::AnimationGraph::compile(*model_,g);motion_.reset(compiled_);
            }else if(domain=="spectrafx"){
                spectra::Effect effect;std::vector<std::string> errors;
                if(!spectra::compileGraph(g,Json::object(),effect,errors))throw std::runtime_error(errors.empty()?"FX compile failed":errors.front());
                fx_=std::make_unique<spectra::Runtime>();fx_->setQuality(aiverse::fx::FxQuality::Low);
                if(!fx_->init(nullptr))throw std::runtime_error("headless FX init failed");
                const int id=fx_->define("__graph_debug",effect);
                if(id<0)throw std::runtime_error("FX definition rejected");
                handle_=fx_->play(id,{0,0,0});
                if(!handle_.valid())throw std::runtime_error("FX playback rejected");
            }else throw std::invalid_argument("unsupported debug domain");
            ++serial_;const auto token=std::to_string(serial_);
            identity_={"isolated",token,"immutable-session:"+token,domain};
            project_=ws.project.dir;active_=true;step_=0;stopReason_="ready";
            return status();
        }catch(...){stop();throw;}
    }
    Json breakpoints(const Json& p){
        requireSession(p);
        if(identity_.domain!="motionweave")throw std::invalid_argument("state/program breakpoints require MotionWeave");
        auto read=[&](const char* key,bool state){
            std::set<std::string> out;const auto& list=p[key];
            if(!list.isArray()||list.size()>32)throw std::invalid_argument("breakpoint list must contain at most 32 IDs");
            for(const auto& v:list.items()){
                const auto id=v.str();bool found=false;
                if(state){for(const auto& s:compiled_->states())if(s.id==id)found=true;}
                else{for(const auto& n:compiled_->nodes())if(n.id==id)found=true;}
                if(id.empty()||!found)throw std::invalid_argument("unknown breakpoint ID");
                out.insert(id);
            }return out;
        };
        auto nextStates=read("states",true),nextNodes=read("nodes",false);
        states_=std::move(nextStates);nodes_=std::move(nextNodes);return status();
    }
    Json advance(const Json& p,bool single){
        requireSession(p);
        if(stopReason_=="unhealthy"||stopReason_=="limit"||stopReason_=="history-limit")return status();
        const int count=single?1:graph_probe::steps(p);
        stopReason_="paused";
        for(int i=0;i<count;++i){
            if(step_>=600){stopReason_="limit";break;}
            Json f=Json::object();const auto start=std::chrono::steady_clock::now();
            bool hit=false;
            if(identity_.domain=="motionweave"){
                motion_.update(1.f/60.f);const double update=graph_probe::ms(start);
                render::Pose pose;const auto eval=std::chrono::steady_clock::now();motion_.evaluate(pose);
                f.set("updateMs",graph_probe::num(update));f.set("evaluateMs",graph_probe::num(graph_probe::ms(eval)));
                f.set("state",Json::string(motion_.state()));f.set("stateTime",graph_probe::num(motion_.stateTime()));
                f.set("healthy",Json::fromBool(motion_.healthy()));f.set("errorMessage",Json::string(motion_.error()));
                hit=states_.count(motion_.state())>0;Json program=Json::array();
                for(const auto& s:compiled_->states())if(s.id==motion_.state()){
                    for(int idx:s.program){const auto& id=compiled_->nodes()[size_t(idx)].id;program.push(Json::string(id));hit|=nodes_.count(id)>0;}break;
                }
                f.set("activeProgram",program);f.set("notifyCount",Json::number(int64_t(motion_.notifies().size())));
            }else{
                fx_->update(1.f/60.f);const auto& s=fx_->stats();
                f.set("updateMs",graph_probe::num(graph_probe::ms(start)));f.set("particles",Json::number(int64_t(s.particles)));
                f.set("instances",Json::number(int64_t(s.instances)));f.set("dropped",Json::number(int64_t(s.dropped)));
                f.set("emitting",Json::fromBool(fx_->playing(handle_)));
            }
            ++step_;f.set("breakpointMatched",Json::fromBool(hit));
            if(!history_.publish(identity_,step_,f)){stopReason_="history-limit";break;}
            if(identity_.domain=="motionweave"&&!motion_.healthy()){stopReason_="unhealthy";break;}
            if(hit){stopReason_="breakpoint";break;}
        }
        return status();
    }
};
} // namespace aiverse::editor::graph_debug
