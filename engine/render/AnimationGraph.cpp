#include "AnimationGraph.h"
#include "AnimationRig.h"
#include "FullBodyIK.h"
#include "AnimationMotion.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace aiverse::render {
namespace {
[[noreturn]] void bad(const std::string& s) { throw std::runtime_error("animationGraph: " + s); }
float number(const Json& j, float fallback) {
    if (j.isNull()) return fallback;
    if (!j.isNumber()) bad("expected a number");
    const double n = j.toDouble();
    if (!std::isfinite(n) || std::abs(n) > 1000000) bad("number outside finite range +/-1000000");
    return float(n);
}
float3 vector(const Json& j) {
    if (!j.isArray() || j.size() != 3) bad("expected [x,y,z]");
    return {number(j[0],0), number(j[1],0), number(j[2],0)};
}
int ref(const std::map<std::string,int>& ids, const std::string& name, const char* what) {
    auto i = ids.find(name);
    if (i == ids.end()) bad(std::string("unknown ") + what + " '" + name + "'");
    return i->second;
}
float value(const AnimationParameters& p, const std::string& name, float fallback) {
    auto i = p.values.find(name);
    return i == p.values.end() || !std::isfinite(i->second) ? fallback : i->second;
}
void blend(Pose& a, const Pose& b, float w, const std::vector<uint8_t>& mask = {}) {
    for (size_t i=0; i<a.size(); ++i) {
        if (!mask.empty() && !mask[i]) continue;
        auto& x=a[i]; const auto& y=b[i];
        x.t=x.t+(y.t-x.t)*w; x.s=x.s+(y.s-x.s)*w; x.r=nlerp(x.r,y.r,w);
        for(int k=0;k<kMaxMorphTargets;++k) x.w[k]+=(y.w[k]-x.w[k])*w;
    }
}
bool validPose(const Pose& p,size_t size){
    if(p.size()!=size)return false;
    for(const auto& n:p){for(float v:{n.t.x,n.t.y,n.t.z,n.r.x,n.r.y,n.r.z,n.r.w,n.s.x,n.s.y,n.s.z})if(!std::isfinite(v))return false;for(float v:n.w)if(!std::isfinite(v))return false;}
    return true;
}
bool matches(const AnimationGraph::Condition& c, const AnimationParameters& p) {
    float x=value(p,c.parameter,0);
    if(c.op=="gt") return x>c.value;
    if(c.op=="ge") return x>=c.value;
    if(c.op=="lt") return x<c.value;
    if(c.op=="le") return x<=c.value;
    if(c.op=="ne") return x!=c.value;
    return x==c.value;
}
}
bool AnimationParameters::set(const std::string& name,float v) {
    if(name.empty() || name.size()>128 || !std::isfinite(v) || std::abs(v)>1000000 || (!values.count(name)&&values.size()>=128)) return false;
    values[name]=v; return true;
}
bool AnimationParameters::set(const std::string& name,float3 v) {
    if(name.empty() || name.size()>128 || !std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z) ||
       std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)})>1000000 || (!vectors.count(name)&&vectors.size()>=128)) return false;
    vectors[name]=v; return true;
}
struct AnimationGraph::CompileContext {
    const AnimationOperations& operations;
    const AnimationTraits& traits;
    AnimationGraphs graphs;
    std::vector<std::string> stack;
    size_t nodes=0, frames=0;
};
std::shared_ptr<const AnimationGraph> AnimationGraph::compile(const Model& m,const Json& j,const AnimationOperations& operations,const AnimationTraits& traits,const AnimationGraphs& graphs) {
    CompileContext context{operations,traits,graphs,{},0,0};
    if(j.has("graphs")&&!j["graphs"].isObject())bad("graphs must be an object");
    for(const auto& [id,definition]:j["graphs"].members())context.graphs[id]=definition;
    if(context.graphs.size()>64)bad("at most 64 graph definitions");
    return compileImpl(m,j,context,0);
}
std::shared_ptr<const AnimationGraph> AnimationGraph::compileImpl(const Model& m,const Json& j,CompileContext& context,int depth) {
    if(depth>8)bad("nested graph depth exceeds 8");
    const auto& operations=context.operations;
    if(!j.isObject()) bad("expected object");
    auto g=std::make_shared<AnimationGraph>(); g->model_=&m;
    if(j.has("parameters")&&!j["parameters"].isObject()) bad("parameters must be an object");
    for(auto& [key,v]:j["parameters"].members()) {
        bool ok=v.isArray()?g->defaults_.set(key,vector(v)):g->defaults_.set(key,v.type()==Json::Type::Bool?float(v.boolean()):number(v,0));
        if(!ok) bad("invalid parameter '"+key+"'");
    }
    auto scalar=[&](const std::string& name){ if(!name.empty()&&!g->defaults_.values.count(name)) bad("undeclared scalar parameter '"+name+"'"); };
    auto vec=[&](const std::string& name){ if(!g->defaults_.vectors.count(name)) bad("undeclared vector parameter '"+name+"'"); };
    std::map<std::string,int> ids;
    if(!j["nodes"].isArray()||j["nodes"].size()==0||j["nodes"].size()>128) bad("nodes must contain 1..128 entries");
    context.nodes+=j["nodes"].size(); if(context.nodes>512)bad("expanded graph exceeds 512 nodes");
    for(const auto& v:j["nodes"].items()) {
        Node n; n.id=v.getStr("id");
        if(n.id.empty()||ids.count(n.id)) bad("empty or duplicate node id '"+n.id+"'");
        std::string kind=v.getStr("kind");
        if(kind=="bind") n.kind=Kind::Bind;
        else if(kind=="clip") {
            n.kind=Kind::Clip; n.clip=m.findClip(v.getStr("clip"));
            if(n.clip<0) bad("node '"+n.id+"': unknown clip '"+v.getStr("clip")+"'");
            n.speed=number(v["speed"],1); n.loop=v.getBool("loop",true);
            if(n.speed<0) bad("negative clip speed");
        } else if(kind=="blend"||kind=="additive") {
            n.kind=kind=="blend"?Kind::Blend:Kind::Additive;
            n.a=ref(ids,v.getStr("a"),"earlier input node"); n.b=ref(ids,v.getStr("b"),"earlier input node");
        } else if(kind=="twoBoneIK") {
            n.kind=Kind::TwoBoneIK; n.a=ref(ids,v.getStr("input"),"earlier input node");
            n.hip=m.findNode(v.getStr("hip")); n.knee=m.findNode(v.getStr("knee")); n.foot=m.findNode(v.getStr("foot"));
            if(n.hip<0||n.knee<0||n.foot<0||n.hip==n.knee||n.knee==n.foot||n.hip==n.foot ||
               m.nodes[size_t(n.knee)].parent!=n.hip||m.nodes[size_t(n.foot)].parent!=n.knee) bad("node '"+n.id+"': IK requires a direct hip/knee/foot chain");
            n.target=v.getStr("target"); n.pole=v.getStr("pole"); vec(n.target); vec(n.pole);
        } else if(kind=="stack"||kind=="rig") {
            n.kind=kind=="stack"?Kind::Stack:Kind::Rig;
            n.a=ref(ids,v.getStr("input"),"earlier input node");
            if(kind=="rig")n.rig=AnimationRig::compile(m,v["units"],g->defaults_);
        } else if(kind=="motionMatching") {
            n.kind=Kind::MotionMatching;n.motion=MotionDatabase::compile(m,v["database"],g->defaults_);
            context.frames+=n.motion->frames.size();if(context.frames>16384)bad("expanded graph exceeds 16384 motion frames");
        } else if(kind=="subgraph") {
            n.kind=Kind::Subgraph;auto name=v.getStr("graph");auto it=context.graphs.find(name);
            if(it==context.graphs.end())bad("unknown subgraph '"+name+"'");
            if(std::find(context.stack.begin(),context.stack.end(),name)!=context.stack.end())bad("recursive subgraph '"+name+"'");
            context.stack.push_back(name);n.subgraph=compileImpl(m,it->second,context,depth+1);context.stack.pop_back();
            if(v.has("bindings")&&!v["bindings"].isObject())bad("bindings must be an object");
            for(const auto& [child,parent]:v["bindings"].members()){
                if(!parent.isString())bad("binding must name a parent parameter");
                if(n.subgraph->defaults().values.count(child))scalar(parent.str());
                else if(n.subgraph->defaults().vectors.count(child))vec(parent.str());
                else bad("unknown child parameter '"+child+"'");
                if(parent.str().empty())bad("empty parent parameter");n.bindings[child]=parent.str();
            }
        } else if(kind=="native") {
            n.kind=Kind::Native; n.a=ref(ids,v.getStr("input"),"earlier input node");
            auto it=operations.find(v.getStr("operation"));
            if(it==operations.end()||!it->second||!it->second->evaluate||it->second->stateFloats>256) bad("unknown or invalid native operation");
            n.operation=it->second; n.config=v["config"];
            if(n.operation->validate) n.operation->validate(m,n.config);
        } else bad("unknown node kind '"+kind+"'");
        if(v.has("traits")) {
            if(n.kind!=Kind::Stack||!v["traits"].isArray()||v["traits"].size()>16)bad("traits require a stack node with at most 16 entries");
            for(const auto& item:v["traits"].items()) {
                Trait trait;trait.config=item["config"];
                if(item.getStr("kind")=="rig")trait.rig=AnimationRig::compile(m,item["units"],g->defaults_);
                else {
                    auto ti=context.traits.find(item.getStr("trait"));
                    if(ti==context.traits.end()||!ti->second||ti->second->stateFloats>256)bad("unknown/invalid trait");
                    trait.implementation=ti->second;
                    if(ti->second->validate)ti->second->validate(m,trait.config);
                }
                n.traits.push_back(std::move(trait));
            }
        }
        n.weight=number(v["weight"],1); if(n.weight<0||n.weight>1) bad("weight must be 0..1");
        n.parameter=v.getStr("parameter"); scalar(n.parameter);
        if(v.has("mask")) {
            int root=m.findNode(v.getStr("mask")); if(root<0) bad("unknown mask root '"+v.getStr("mask")+"'");
            n.mask=subtreeMask(m,root);
        }
        ids[n.id]=int(g->nodes_.size()); g->nodes_.push_back(std::move(n));
    }
    std::map<std::string,int> states;
    if(!j.has("states")) {
        State s; s.id="default"; s.output=ref(ids,j.getStr("output"),"output node"); g->states_.push_back(s);
    } else {
        if(!j["states"].isArray()||j["states"].size()==0||j["states"].size()>64) bad("states must contain 1..64 entries");
        for(const auto& v:j["states"].items()) {
            State s; s.id=v.getStr("id"); s.output=ref(ids,v.getStr("output"),"output node");
            if(s.id.empty()||states.count(s.id)) bad("empty or duplicate state id");
            states[s.id]=int(g->states_.size()); g->states_.push_back(s);
        }
        g->initial_=ref(states,j.getStr("initial",g->states_[0].id),"initial state");
        for(size_t i=0;i<g->states_.size();++i) {
            const Json& ts=j["states"][i]["transitions"];
            if(!ts.isNull()&&(!ts.isArray()||ts.size()>64)) bad("transitions must be an array of at most 64 entries");
            for(const auto& v:ts.items()) {
                Transition t; t.to=ref(states,v.getStr("to"),"destination state");
                t.fade=number(v["fade"],.2f); t.after=number(v["after"],0);
                if(t.fade<0||t.after<0) bad("negative transition time");
                if((!v["when"].isArray()&&!(v["when"].isObject()&&v["when"].size()==0))||v["when"].size()>32) bad("transition when must be an array of at most 32 conditions");
                for(const auto& c:v["when"].items()) {
                    Condition condition{c.getStr("parameter"),c.getStr("op","eq"),number(c["value"],1)};
                    if(condition.parameter.empty()) bad("empty condition parameter");
                    scalar(condition.parameter);
                    if(condition.op!="eq"&&condition.op!="ne"&&condition.op!="gt"&&condition.op!="ge"&&condition.op!="lt"&&condition.op!="le") bad("unknown comparison '"+condition.op+"'");
                    t.conditions.push_back(condition);
                }
                g->states_[i].transitions.push_back(t);
            }
        }
    }
    // Compile one linear evaluation program per state, sharing each DAG node once.
    for(auto& state:g->states_) {
        std::vector<uint8_t> needed(g->nodes_.size()); needed[size_t(state.output)]=1;
        for(int i=state.output;i>=0;--i) if(needed[size_t(i)]) {
            const auto& n=g->nodes_[size_t(i)];
            if(n.a>=0) needed[size_t(n.a)]=1;
            if(n.b>=0) needed[size_t(n.b)]=1;
        }
        for(size_t i=0;i<needed.size();++i) if(needed[i]) state.program.push_back(int(i));
    }
    return g;
}
void AnimationGraphInstance::reset(std::shared_ptr<const AnimationGraph> g) {
    graph_=std::move(g); parameters_={}; healthy_=true; error_.clear(); state_=0; time_=fadeTime_=fadeDuration_=0;
    source_.clear();last_.clear();bind_.clear();poses_.clear();nativeState_.clear();world_.clear();clocks_.clear();
    children_.clear();motions_.clear();traitState_.clear();rigs_.clear();
    if(!graph_) return;
    parameters_=graph_->defaults(); state_=graph_->initial(); bindPose(*graph_->model(),bind_);
    poses_.resize(graph_->nodes().size(),bind_); nativeState_.resize(poses_.size()); clocks_.resize(poses_.size()); last_=bind_;
    for(size_t i=0;i<poses_.size();++i) if(graph_->nodes()[i].operation)
        nativeState_[i].resize(graph_->nodes()[i].operation->stateFloats);
    resetNodes();
}
void AnimationGraphInstance::resetNodes() {
    rigs_.assign(graph_->nodes().size(),{});
    children_.resize(graph_->nodes().size());motions_.resize(children_.size());traitState_.resize(children_.size());
    for(size_t i=0;i<children_.size();++i){const auto& n=graph_->nodes()[i];
        if(n.rig)rigs_[i]=std::make_shared<AnimationRigState>();
        if(n.subgraph){if(!children_[i])children_[i]=std::make_shared<AnimationGraphInstance>();children_[i]->reset(n.subgraph);}
        if(n.motion){if(!motions_[i])motions_[i]=std::make_shared<MotionPlayer>();motions_[i]->reset(n.motion);}
        traitState_[i].clear();for(const auto& t:n.traits)traitState_[i].push_back({std::vector<float>(t.implementation?t.implementation->stateFloats:0),true,t.rig?std::make_shared<AnimationRigState>():nullptr});
    }
}
void AnimationGraphInstance::bindChild(size_t i){
    auto& child=*children_[i];child.parameters().environment=parameters_.environment;for(const auto& [to,from]:graph_->nodes()[i].bindings){
        auto scalar=parameters_.values.find(from);auto vector=parameters_.vectors.find(from);
        if(child.graph()->defaults().values.count(to)&&scalar!=parameters_.values.end())child.parameters().set(to,scalar->second);
        else if(vector!=parameters_.vectors.end())child.parameters().set(to,vector->second);
    }
}
AnimationGraphInstance* AnimationGraphInstance::child(const std::string& name){if(graph_)for(size_t i=0;i<children_.size();++i)if(graph_->nodes()[i].id==name)return children_[i].get();return nullptr;}
const MotionPlayer* AnimationGraphInstance::motion(const std::string& name)const{if(graph_)for(size_t i=0;i<motions_.size();++i)if(graph_->nodes()[i].id==name)return motions_[i].get();return nullptr;}
bool AnimationGraph::hasContacts() const {
    for(const auto& n:nodes()){if(n.subgraph&&n.subgraph->hasContacts())return true;if(n.rig)for(auto& u:n.rig->units())if(u.fullBody&&u.fullBody->contacts())return true;for(auto& t:n.traits)if(t.rig)for(auto& u:t.rig->units())if(u.fullBody&&u.fullBody->contacts())return true;}return false;
}
bool AnimationGraphInstance::hasContacts() const {return graph_&&graph_->hasContacts();}
AnimationContactReport AnimationGraphInstance::contactReport() const {
    AnimationContactReport result;if(!graph_||last_.empty())return result;std::vector<mat4> world;worldMatrices(*graph_->model(),last_,world);
    auto add=[&](const AnimationContactReport& r){result.supports+=r.supports;result.collisions+=r.collisions;result.penetration=std::max(result.penetration,r.penetration);result.balanceError=std::max(result.balanceError,r.balanceError);result.supportError=std::max(result.supportError,r.supportError);};
    auto rig=[&](const AnimationRig& r,const AnimationRigState* state){if(!state)return;for(size_t i=0;i<r.units().size()&&i<state->contacts.size();++i){auto& u=r.units()[i];if(u.fullBody&&u.fullBody->contacts())add(u.fullBody->contacts()->report(*graph_->model(),parameters_,last_,world,state->contacts[i]));}};
    for(int index:graph_->states()[size_t(state_)].program){size_t i=size_t(index);const auto& n=graph_->nodes()[i];if(n.rig)rig(*n.rig,rigs_[i].get());for(size_t t=0;t<n.traits.size();++t)if(n.traits[t].rig&&traitState_[i][t].enabled)rig(*n.traits[t].rig,traitState_[i][t].rig.get());if(children_[i])add(children_[i]->contactReport());}
    result.supported=result.supports>0;result.stable=result.supported&&result.supportError<.005f&&result.penetration<.005f&&result.balanceError<.005f;return result;
}
void AnimationGraphInstance::traitFailure(size_t i,size_t t){traitState_[i][t].enabled=false;healthy_=false;error_="trait failed at node '"+graph_->nodes()[i].id+"'";}
const std::string& AnimationGraphInstance::state() const {
    static const std::string empty;
    return graph_?graph_->states()[size_t(state_)].id:empty;
}
void AnimationGraphInstance::update(float dt) {
    if(!graph_||!std::isfinite(dt)||dt<0) return;
    dt=std::min(dt,1.f); // bound work and clocks after a suspended host resumes
    time_=std::min(time_+dt,1000000.f); fadeTime_+=dt;
    for(int instruction:graph_->states()[size_t(state_)].program) {
        const size_t i=size_t(instruction);
        const auto& n=graph_->nodes()[i];
        for(size_t t=n.traits.size();t-->0;){auto& st=traitState_[i][t];const auto& op=n.traits[t].implementation;
            if(st.enabled&&op&&op->preUpdate&&!op->preUpdate(n.traits[t].config,parameters_,dt,st.data))traitFailure(i,t);
        }
        if(children_[i]){bindChild(i);children_[i]->update(dt);if(!children_[i]->healthy()){healthy_=false;error_=children_[i]->error();}}
        if(motions_[i])motions_[i]->update(dt,parameters_);
        if(n.operation&&n.operation->update) n.operation->update(n.config,parameters_,dt,nativeState_[i]);
        for(size_t t=0;t<n.traits.size();++t){auto& st=traitState_[i][t];const auto& op=n.traits[t].implementation;
            if(st.enabled&&op&&op->postUpdate&&!op->postUpdate(n.traits[t].config,parameters_,dt,st.data))traitFailure(i,t);
        }
        if(n.kind!=AnimationGraph::Kind::Clip) continue;
        const float duration=graph_->model()->clips[size_t(n.clip)].duration;
        clocks_[i]+=double(dt)*std::clamp(value(parameters_,n.parameter,n.speed),0.f,1000000.f);
        clocks_[i]=duration>0?(n.loop?std::fmod(clocks_[i],duration):std::min(clocks_[i],double(duration))):0;
    }
    for(const auto& t:graph_->states()[size_t(state_)].transitions) {
        if(time_<t.after || !std::all_of(t.conditions.begin(),t.conditions.end(),[&](const auto& c){return matches(c,parameters_);})) continue;
        evaluate(last_); source_=last_; state_=t.to; time_=0; fadeTime_=0; fadeDuration_=t.fade;
        std::fill(clocks_.begin(),clocks_.end(),0);
        for(auto& storage:nativeState_) std::fill(storage.begin(),storage.end(),0);
        resetNodes();
        break;
    }
}
void AnimationGraphInstance::evaluateCurrent(Pose& result) {
    const auto& nodes=graph_->nodes(); const auto& model=*graph_->model();
    const auto& state=graph_->states()[size_t(state_)];
    const int output=state.output;
    for(int instruction:state.program) {
        const size_t i=size_t(instruction);
        const auto& n=nodes[i]; auto& p=poses_[i];
        float w=std::clamp(value(parameters_,n.parameter,n.weight),0.f,1.f);
        if(n.kind==AnimationGraph::Kind::Subgraph){bindChild(i);children_[i]->evaluate(p);if(!children_[i]->healthy()){healthy_=false;error_=children_[i]->error();}}
        else if(n.kind==AnimationGraph::Kind::MotionMatching)motions_[i]->evaluate(p);
        else if(n.kind==AnimationGraph::Kind::Bind) p=bind_;
        else if(n.kind==AnimationGraph::Kind::Clip) {
            p=bind_; const auto& clip=model.clips[size_t(n.clip)];
            double t=clocks_[i];
            sampleClip(clip,float(t),p);
        } else {
            p=poses_[size_t(n.a)];
            if(n.kind==AnimationGraph::Kind::Stack){
                for(size_t t=n.traits.size();t-->0;){auto& st=traitState_[i][t];const auto& op=n.traits[t].implementation;
                    if(st.enabled&&op&&op->preEvaluate){Pose candidate=p;if(!op->preEvaluate(n.traits[t].config,parameters_,st.data,candidate)||!validPose(candidate,p.size()))traitFailure(i,t);else p=std::move(candidate);}
                }
                for(size_t t=0;t<n.traits.size();++t){auto& st=traitState_[i][t];const auto& trait=n.traits[t];
                    if(st.enabled&&trait.rig)trait.rig->execute(model,parameters_,p,world_,st.rig.get());
                    if(st.enabled&&trait.implementation&&trait.implementation->postEvaluate){Pose candidate=p;if(!trait.implementation->postEvaluate(trait.config,parameters_,st.data,candidate)||!validPose(candidate,p.size()))traitFailure(i,t);else p=std::move(candidate);}
                }
            } else if(n.kind==AnimationGraph::Kind::Rig)n.rig->execute(model,parameters_,p,world_,rigs_[i].get());
            else if(n.kind==AnimationGraph::Kind::Blend) blend(p,poses_[size_t(n.b)],w,n.mask);
            else if(n.kind==AnimationGraph::Kind::Additive) {
                const auto& delta=poses_[size_t(n.b)];
                for(size_t k=0;k<p.size();++k) {
                    if(!n.mask.empty()&&!n.mask[k]) continue;
                    const auto& d=delta[k]; const auto& r=bind_[k]; auto& x=p[k];
                    x.t+=(d.t-r.t)*w;
                    const quat inv{-r.r.x,-r.r.y,-r.r.z,r.r.w}; x.r=qnormalize(x.r*nlerp(quat{},inv*d.r,w));
                    x.s+= (d.s-r.s)*w;
                    for(int c=0;c<kMaxMorphTargets;++c) x.w[c]+=(d.w[c]-r.w[c])*w;
                }
            } else if(n.kind==AnimationGraph::Kind::Native) {
                n.operation->evaluate(model,n.config,parameters_,nativeState_[i],p);
            } else {
                auto target=parameters_.vectors.find(n.target), pole=parameters_.vectors.find(n.pole);
                if(target!=parameters_.vectors.end()&&pole!=parameters_.vectors.end()) {
                    auto finite=[](float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
                    if(finite(target->second)&&finite(pole->second)) {
                        worldMatrices(model,p,world_);
                        solveTwoBoneIK(model,p,world_,n.hip,n.knee,n.foot,target->second,pole->second,w);
                    }
                }
            }
        }
    }
    result=poses_[size_t(output)];
}
void AnimationGraphInstance::evaluate(Pose& result) {
    if(!graph_) {result.clear();return;}
    evaluateCurrent(result);
    if(fadeDuration_>0&&fadeTime_<fadeDuration_&&!source_.empty()) {
        float w=std::clamp(fadeTime_/fadeDuration_,0.f,1.f);
        blend(result,source_,1-w);
    }
    last_=result;
}
} // namespace aiverse::render
