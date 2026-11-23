#include "AnimationGraph.h"
#include "AnimationRig.h"
#include "FullBodyIK.h"
#include "AnimationMotion.h"
#include "AnimationController.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace aiverse::render {
std::unique_ptr<AnimationGraphInstance> AnimationGraphInstance::cloneForPreparation() const {
    // The private memberwise copy includes every scalar, clock, pose, native
    // state span, notify queue and diagnostic. Detach all mutable shared owners
    // before exposing the candidate; allocation failure leaves this untouched.
    auto next=std::unique_ptr<AnimationGraphInstance>(new AnimationGraphInstance(*this));
    // Explicit debug instrumentation must never migrate into prepared View candidates.
    next->executionProfile_.reset();
    for(auto& traits:next->traitState_)
        for(auto& trait:traits)
            if(trait.rig)trait.rig=std::make_shared<AnimationRigState>(*trait.rig);
    for(auto& child:next->children_)
        if(child)child=child->cloneForPreparation();
    for(auto& motion:next->motions_)
        if(motion)motion=std::make_shared<MotionPlayer>(*motion);
    for(auto& controller:next->controllers_)
        if(controller)controller=std::make_shared<AnimationControllerPlayer>(*controller);
    for(auto& rig:next->rigs_)
        if(rig)rig=std::make_shared<AnimationRigState>(*rig);
    return next;
}

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
void blend(Pose& a, const Pose& b, float w, const std::vector<uint8_t>& mask = {}, bool morph = true) {
    for (size_t i=0; i<a.size(); ++i) {
        if (!mask.empty() && !mask[i]) continue;
        auto& x=a[i]; const auto& y=b[i];
        x.t=x.t+(y.t-x.t)*w; x.s=x.s+(y.s-x.s)*w; x.r=nlerp(x.r,y.r,w);
        if(morph) for(int k=0;k<kMaxMorphTargets;++k) x.w[k]+=(y.w[k]-x.w[k])*w;
    }
}
// Copy a pose; in a morph-free graph every internal pose already holds the
// bind morph weights, so only translation/rotation/scale need copying.
void assignPose(Pose& to, const Pose& from, bool morph) {
    if (morph || to.size()!=from.size()) { to=from; return; }
    for (size_t i=0; i<to.size(); ++i) { to[i].t=from[i].t; to[i].r=from[i].r; to[i].s=from[i].s; }
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
            if(v.has("notifies")) {
                if(!v["notifies"].isArray() || v["notifies"].size()>256)
                    bad("clip notifies must be an array of up to 256 keys");
                for(const auto& key:v["notifies"].items()) {
                    const float time=number(key["time"],-1.f);
                    const std::string name=key.getStr("name");
                    const float duration=m.clips[size_t(n.clip)].duration;
                    if(!key.isObject() || name.empty() || name.size()>128 ||
                       time<0 || time>duration || (n.loop && time>=duration))
                        bad("invalid clip notify time/name");
                    n.notifies.push_back({time,name});
                }
            }
        } else if(kind=="blend"||kind=="additive") {
            n.kind=kind=="blend"?Kind::Blend:Kind::Additive;
            n.a=ref(ids,v.getStr("a"),"earlier input node"); n.b=ref(ids,v.getStr("b"),"earlier input node");
            n.sync=v.getBool("sync",false);
            if(n.sync) {
                if(n.kind!=Kind::Blend||n.a==n.b) bad("node '"+n.id+"': sync requires a blend of two different clips");
                for(int input:{n.a,n.b}) {
                    const auto& clip=g->nodes_[size_t(input)];
                    if(clip.kind!=Kind::Clip||!clip.loop||!(m.clips[size_t(clip.clip)].duration>0.f))
                        bad("node '"+n.id+"': sync inputs must be looping clips with a positive duration");
                    for(const auto& other:g->nodes_)
                        if(other.sync&&(other.a==input||other.b==input))
                            bad("node '"+n.id+"': clip '"+clip.id+"' already belongs to another sync blend");
                }
            }
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
        } else if(kind=="controller") {
            n.kind=Kind::Controller;n.controller=AnimationController::compile(m,v["controller"],g->defaults_);
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
                if (parent.str().empty())
                  bad("empty parent parameter");
                n.bindings[child] = parent.str();
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
    g->morphFree_=true;
    for(const auto& clip:m.clips) for(const auto& ch:clip.channels) if(ch.path==AnimChannel::Weights) g->morphFree_=false;
    for(const auto& n:g->nodes_) {
        if(n.operation || n.controller || (n.subgraph && !n.subgraph->morphFree())) g->morphFree_=false;
        for(const auto& t:n.traits) if(t.implementation) g->morphFree_=false;
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
    executionProfile_.reset();
    graph_=std::move(g); parameters_={}; healthy_=true; error_.clear(); state_=0; time_=fadeTime_=fadeDuration_=0;
    source_.clear();last_.clear();bind_.clear();poses_.clear();nativeState_.clear();world_.clear();clocks_.clear();unwrappedClocks_.clear();clipStarted_.clear();notifies_.clear();
    children_.clear();motions_.clear();controllers_.clear();traitState_.clear();rigs_.clear();
    if(!graph_) return;
    parameters_=graph_->defaults(); state_=graph_->initial(); bindPose(*graph_->model(),bind_);
    poses_.resize(graph_->nodes().size(),bind_); nativeState_.resize(poses_.size()); clocks_.resize(poses_.size()); unwrappedClocks_.resize(poses_.size()); clipStarted_.assign(poses_.size(),1); last_=bind_;
    syncRate_.assign(poses_.size(),-1.); syncLeader_.assign(poses_.size(),-1);
    for(size_t i=0;i<poses_.size();++i) if(graph_->nodes()[i].operation)
        nativeState_[i].resize(graph_->nodes()[i].operation->stateFloats);
    resetNodes();
}
void AnimationGraphInstance::resetNodes() {
    rigs_.assign(graph_->nodes().size(),{});
    children_.resize(graph_->nodes().size());motions_.resize(children_.size());controllers_.resize(children_.size());traitState_.resize(children_.size());
    for(size_t i=0;i<children_.size();++i){const auto& n=graph_->nodes()[i];
        if(n.rig)rigs_[i]=std::make_shared<AnimationRigState>();
        if(n.subgraph){if(!children_[i])children_[i]=std::make_shared<AnimationGraphInstance>();children_[i]->reset(n.subgraph);}
        if(n.motion){if(!motions_[i])motions_[i]=std::make_shared<MotionPlayer>();motions_[i]->reset(n.motion);}
        if(n.controller){if(!controllers_[i])controllers_[i]=std::make_shared<AnimationControllerPlayer>();controllers_[i]->reset(n.controller);}
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
const AnimationControllerPlayer* AnimationGraphInstance::controller(const std::string& name)const{if(graph_)for(size_t i=0;i<controllers_.size();++i)if(graph_->nodes()[i].id==name)return controllers_[i].get();return nullptr;}
const MotionPlayer* AnimationGraphInstance::motion(const std::string& name)const{if(graph_)for(size_t i=0;i<motions_.size();++i)if(graph_->nodes()[i].id==name)return motions_[i].get();return nullptr;}
bool AnimationGraph::hasContacts() const {
    for(const auto& n:nodes()){if(n.subgraph&&n.subgraph->hasContacts())return true;if(n.rig)for(auto& u:n.rig->units())if(u.locomotion||(u.fullBody&&u.fullBody->contacts()))return true;for(auto& t:n.traits)if(t.rig)for(auto& u:t.rig->units())if(u.locomotion||(u.fullBody&&u.fullBody->contacts()))return true;}return false;
}
bool AnimationGraphInstance::hasContacts() const {return graph_&&graph_->hasContacts();}
AnimationContactReport AnimationGraphInstance::contactReport() const {
    AnimationContactReport result;if(!graph_||last_.empty())return result;std::vector<mat4> world;worldMatrices(*graph_->model(),last_,world);
    auto add=[&](const AnimationContactReport& r){result.supports+=r.supports;result.collisions+=r.collisions;result.penetration=std::max(result.penetration,r.penetration);result.balanceError=std::max(result.balanceError,r.balanceError);result.supportError=std::max(result.supportError,r.supportError);};
    auto rig=[&](const AnimationRig& r,const AnimationRigState* state){if(!state)return;for(size_t i=0;i<r.units().size()&&i<state->contacts.size();++i){auto& u=r.units()[i];if(u.fullBody&&u.fullBody->contacts())add(u.fullBody->contacts()->report(*graph_->model(),parameters_,last_,world,state->contacts[i]));}};
    for(int index:graph_->states()[size_t(state_)].program){size_t i=size_t(index);const auto& n=graph_->nodes()[i];if(n.rig)rig(*n.rig,rigs_[i].get());for(size_t t=0;t<n.traits.size();++t)if(n.traits[t].rig&&traitState_[i][t].enabled)rig(*n.traits[t].rig,traitState_[i][t].rig.get());if(children_[i])add(children_[i]->contactReport());}
    result.supported=result.supports>0;result.stable=result.supported&&result.supportError<.005f&&result.penetration<.005f&&result.balanceError<.005f;return result;
}
SurfaceLocomotionReport AnimationGraphInstance::locomotionReport() const {
    if(!graph_)return {};for(int index:graph_->states()[size_t(state_)].program){size_t i=size_t(index);if(rigs_[i])for(auto& s:rigs_[i]->locomotion)if(s.report.active)return s.report;for(auto& t:traitState_[i])if(t.enabled&&t.rig)for(auto& s:t.rig->locomotion)if(s.report.active)return s.report;if(children_[i]){auto r=children_[i]->locomotionReport();if(r.active)return r;}}return {};
}
void AnimationGraphInstance::traitFailure(size_t i,size_t t){traitState_[i][t].enabled=false;healthy_=false;error_="trait failed at node '"+graph_->nodes()[i].id+"'";}
const std::string& AnimationGraphInstance::state() const {
    static const std::string empty;
    return graph_?graph_->states()[size_t(state_)].id:empty;
}
// Stage 40: preserve presentation time only when the compiled graph has no
// mutable CPU simulation state other than its state timer and clip clocks.
// This snapshot contains no GPU handles, Model pointers, poses or callbacks.
std::optional<AnimationGraphInstance::PreviewClockSnapshot>
AnimationGraphInstance::snapshotPreviewClocks() const {
    if (!graph_ || !healthy_ || !std::isfinite(time_) || time_ < 0 ||
        (fadeDuration_ > 0 && fadeTime_ < fadeDuration_))
        return std::nullopt;
    const Model* model = graph_->model();
    if (!model || clocks_.size() != graph_->nodes().size()) return std::nullopt;
    PreviewClockSnapshot snapshot;
    snapshot.state = state();
    snapshot.stateTime = time_;
    for (const auto& st : graph_->states()) snapshot.stateIds.push_back(st.id);
    for (const auto& bone : model->nodes)
        snapshot.skeleton.emplace_back(bone.name, bone.parent);
    for (size_t i = 0; i < graph_->nodes().size(); ++i) {
        const auto& n = graph_->nodes()[i];
        // No hidden state: native operations, traits, rigs, controllers,
        // subgraphs and motion matching have additional mutable state.
        switch (n.kind) {
        case AnimationGraph::Kind::Bind:
        case AnimationGraph::Kind::Clip:
        case AnimationGraph::Kind::Blend:
        case AnimationGraph::Kind::Additive:
        case AnimationGraph::Kind::TwoBoneIK: break;
        default: return std::nullopt;
        }
        if (!n.traits.empty() || n.operation || n.subgraph || n.rig ||
            n.motion || n.controller) return std::nullopt;
        snapshot.nodeKinds.emplace_back(n.id, n.kind);
        if (n.kind != AnimationGraph::Kind::Clip) continue;
        if (n.clip < 0 || size_t(n.clip) >= model->clips.size()) return std::nullopt;
        const auto& clip = model->clips[size_t(n.clip)];
        const double clock = clocks_[i], duration = double(clip.duration);
        if (!std::isfinite(clock) || !std::isfinite(duration) || duration < 0 ||
            clock < 0 || clock > duration + 1e-4) return std::nullopt;
        snapshot.clips.push_back({n.id, clip.name, duration, clock, n.loop});
    }
    return snapshot;
}
bool AnimationGraphInstance::restorePreviewClocks(const PreviewClockSnapshot& snapshot) {
    // Validate ALL state and model layout before mutating the live instance.
    // The caller must also verify the full authored graph program key.
    const auto current = snapshotPreviewClocks();
    if (!current || current->skeleton != snapshot.skeleton ||
        current->stateIds != snapshot.stateIds ||
        current->nodeKinds != snapshot.nodeKinds ||
        current->clips.size() != snapshot.clips.size() ||
        !std::isfinite(snapshot.stateTime) || snapshot.stateTime < 0 ||
        snapshot.stateTime > 1000000.f) return false;
    int nextState = -1;
    for (size_t i = 0; i < current->stateIds.size(); ++i)
        if (current->stateIds[i] == snapshot.state) {
            nextState = int(i); break;
        }
    if (nextState < 0) return false;
    for (size_t i = 0; i < current->clips.size(); ++i) {
        const auto& a = current->clips[i];
        const auto& b = snapshot.clips[i];
        if (a.nodeId != b.nodeId || a.clipName != b.clipName ||
            a.loop != b.loop || std::abs(a.duration - b.duration) > 1e-4 ||
            !std::isfinite(b.clock) || b.clock < 0 ||
            b.clock > a.duration + 1e-4) return false;
    }
    state_ = nextState;
    time_ = snapshot.stateTime;
    fadeTime_ = fadeDuration_ = 0;
    source_.clear();
    size_t clipIndex = 0;
    for (size_t i = 0; i < graph_->nodes().size(); ++i)
        if (graph_->nodes()[i].kind == AnimationGraph::Kind::Clip)
            clocks_[i] = snapshot.clips[clipIndex++].clock;
    return true;
}
AnimationGraphInstance::PlaybackSnapshot
AnimationGraphInstance::snapshotPlayback() const {
    PlaybackSnapshot out;
    if(!graph_ || !healthy_)return out;
    out.state=state();
    out.stateTime=time_;
    out.clips.reserve(graph_->nodes().size());
    for(size_t i=0;i<graph_->nodes().size();++i) {
        const auto& node=graph_->nodes()[i];
        if(node.kind!=AnimationGraph::Kind::Clip || node.clip<0 ||
           size_t(node.clip)>=graph_->model()->clips.size())continue;
        const auto& clip=graph_->model()->clips[size_t(node.clip)];
        if(!(clip.duration>0.f) || !std::isfinite(clip.duration) ||
           !std::isfinite(clocks_[i]))continue;
        out.clips.push_back({node.id,clip.name,
            std::clamp(float(clocks_[i]/double(clip.duration)),0.f,1.f),
            node.loop});
    }
    return out;
}

bool AnimationGraphInstance::restorePlayback(const PlaybackSnapshot& snapshot) {
    if(!graph_ || snapshot.state.empty() ||
       !std::isfinite(snapshot.stateTime) || snapshot.stateTime<0.f)return false;
    size_t targetState=graph_->states().size();
    for(size_t i=0;i<graph_->states().size();++i)
        if(graph_->states()[i].id==snapshot.state) {
            targetState=i;
            break;
        }
    if(targetState>=graph_->states().size())return false;
    std::vector<std::pair<size_t,double>> matched;
    matched.reserve(snapshot.clips.size());
    for(size_t i=0;i<graph_->nodes().size();++i) {
        const auto& node=graph_->nodes()[i];
        if(node.kind!=AnimationGraph::Kind::Clip || node.clip<0 ||
           size_t(node.clip)>=graph_->model()->clips.size())continue;
        const auto& clip=graph_->model()->clips[size_t(node.clip)];
        if(!(clip.duration>0.f) || !std::isfinite(clip.duration))continue;
        const auto prev=std::find_if(snapshot.clips.begin(),snapshot.clips.end(),
            [&](const PlaybackSnapshot::Clip& candidate) {
                return candidate.node==node.id && candidate.clip==clip.name &&
                       candidate.loop==node.loop &&
                       std::isfinite(candidate.phase) && candidate.phase>=0.f &&
                       candidate.phase<=1.f;
            });
        if(prev==snapshot.clips.end())continue;
        const double seconds=std::clamp(double(prev->phase),0.,
                              node.loop?0.999999:1.)*double(clip.duration);
        matched.emplace_back(i,seconds);
    }
    // A graph with entirely renamed/removed clips must restart, not
    // accidentally restore only its state clock.
    if(!snapshot.clips.empty() && matched.empty())return false;
    // The previous source's poses, native state, events and contact caches
    // remain discarded. Only the current named state and clip phases survive.
    state_=int(targetState);
    time_=std::clamp(snapshot.stateTime,0.f,1000000.f);
    fadeTime_=fadeDuration_=0.f;
    notifies_.clear();
    for(const auto& [i,seconds]:matched) {
        clocks_[i]=seconds;
        unwrappedClocks_[i]=seconds;
        clipStarted_[i]=0; // do not synthesize time-zero notify on reload
    }
    return true;
}
bool AnimationGraphInstance::rebindPlayback(
        std::shared_ptr<const AnimationGraph> nextGraph) {
    if(graph_.get()==nextGraph.get())return bool(graph_);
    // A pure playback snapshot contains only names, phases and a scalar
    // state clock. Never retain a pointer into an unloaded source Model.
    const bool previousValid=bool(graph_) && healthy_;
    const auto old=previousValid?snapshotPlayback():PlaybackSnapshot{};
    reset(std::move(nextGraph));
    return previousValid && restorePlayback(old);
}
std::optional<float> AnimationGraphInstance::clipTime(const std::string& node) const {
    if(!graph_)return std::nullopt;
    for(size_t i=0;i<graph_->nodes().size();++i)
        if(graph_->nodes()[i].id==node &&
           graph_->nodes()[i].kind==AnimationGraph::Kind::Clip)
            return float(clocks_[i]);
    return std::nullopt;
}
bool AnimationGraphInstance::seekClipNode(const std::string& node,float seconds) {
    if(!graph_ || !std::isfinite(seconds) || seconds<0.f)return false;
    for(size_t i=0;i<graph_->nodes().size();++i) {
        const auto& n=graph_->nodes()[i];
        if(n.id!=node || n.kind!=AnimationGraph::Kind::Clip)continue;
        const float duration=graph_->model()->clips[size_t(n.clip)].duration;
        if(duration<=0.f)return false;
        const double time=n.loop?std::fmod(double(seconds),double(duration)):
            std::min(double(seconds),double(duration));
        unwrappedClocks_[i]=time;
        clocks_[i]=time;
        clipStarted_[i]=0; // never generate phantom time-zero notifies
        return true;
    }
    return false;
}
void AnimationGraphInstance::enableExecutionProfile(bool enabled) {
    if(!enabled){executionProfile_.reset();return;}
    if(!graph_)throw std::logic_error("execution profile requires a compiled graph");
    executionProfile_.emplace(graph_->nodes().size());
}
Json AnimationGraphInstance::executionProfileReport() const {
    Json r=Json::object();r.set("available",Json::fromBool(bool(executionProfile_)));
    if(!executionProfile_||!graph_)return r;
    r.set("scope",Json::string("root-node-inclusive-cpu-scopes"));
    const char* phases[]={"sync","update","evaluate"};
    Json rows=Json::array(),events=Json::array();
    for(size_t i=0;i<executionProfile_->nodes().size();++i){
        Json row=Json::object();row.set("node",Json::string(graph_->nodes()[i].id));
        for(size_t p=0;p<3;++p){
            const auto& c=executionProfile_->nodes()[i][p];Json value=Json::object();
            value.set("calls",Json::string(std::to_string(c.calls)));
            value.set("totalUs",Json::number(c.totalUs,3));value.set("maxUs",Json::number(c.maxUs,3));
            row.set(phases[p],value);
        }
        rows.push(row);
    }
    for(const auto& e:executionProfile_->events()){
        Json row=Json::object();row.set("node",Json::string(graph_->nodes()[e.node].id));
        row.set("phase",Json::string(phases[size_t(e.phase)]));
        row.set("startUs",Json::number(e.startUs,3));row.set("durationUs",Json::number(e.durationUs,3));
        events.push(row);
    }
    r.set("nodes",rows);r.set("events",events);
    r.set("droppedEvents",Json::string(std::to_string(executionProfile_->droppedEvents())));
    r.set("note",Json::string("Inclusive CPU wall-clock scopes. Subgraphs/rigs are included in parent node time; sync, transition and final pose work outside scopes is not fully attributed. Trace stores only the latest explicitly begun interval; instrumentation overhead is included."));
    return r;
}
void AnimationGraphInstance::update(float dt) {
    notifies_.clear();
    if(!graph_||!std::isfinite(dt)||dt<0) return;
    dt=std::min(dt,1.f); // bound work and clocks after a suspended host resumes
    time_=std::min(time_+dt,1000000.f); fadeTime_+=dt;
    // Sync groups: a synced blend drives both clips at the weight-blended
    // cycle rate; the later clip follows the earlier clip's normalized phase.
    std::fill(syncRate_.begin(),syncRate_.end(),-1.);
    std::fill(syncLeader_.begin(),syncLeader_.end(),-1);
    for(int instruction:graph_->states()[size_t(state_)].program) {
        const auto& n=graph_->nodes()[size_t(instruction)];
        if(!n.sync) continue;
        graph_profile::Scope timing(executionProfile_?&*executionProfile_:nullptr,size_t(instruction),graph_profile::Phase::Sync);
        const auto& a=graph_->nodes()[size_t(n.a)];const auto& b=graph_->nodes()[size_t(n.b)];
        const double da=graph_->model()->clips[size_t(a.clip)].duration,db=graph_->model()->clips[size_t(b.clip)].duration;
        const double ra=std::clamp(value(parameters_,a.parameter,a.speed),0.f,1000000.f)/da;
        const double rb=std::clamp(value(parameters_,b.parameter,b.speed),0.f,1000000.f)/db;
        const double w=std::clamp(value(parameters_,n.parameter,n.weight),0.f,1.f);
        const double cycles=ra>0&&rb>0?1./((1.-w)/ra+w/rb):(1.-w)*ra+w*rb; // blended cycle duration
        syncRate_[size_t(n.a)]=cycles*da; syncRate_[size_t(n.b)]=cycles*db;
        syncLeader_[size_t(std::max(n.a,n.b))]=std::min(n.a,n.b);
    }
    for(int instruction:graph_->states()[size_t(state_)].program) {
        const size_t i=size_t(instruction);
        const auto& n=graph_->nodes()[i];
        graph_profile::Scope timing(executionProfile_?&*executionProfile_:nullptr,i,graph_profile::Phase::Update);
        if(rigs_[i])rigs_[i]->pendingTime=std::min(.1f,rigs_[i]->pendingTime+dt);
        for(auto& t:traitState_[i])if(t.enabled&&t.rig)t.rig->pendingTime=std::min(.1f,t.rig->pendingTime+dt);
        for(size_t t=n.traits.size();t-->0;){auto& st=traitState_[i][t];const auto& op=n.traits[t].implementation;
            if(st.enabled&&op&&op->preUpdate&&!op->preUpdate(n.traits[t].config,parameters_,dt,st.data))traitFailure(i,t);
        }
        if(children_[i]){
            bindChild(i);children_[i]->update(dt);
            for(const auto& notify:children_[i]->notifies())
                notifies_.push_back({n.id+"/"+notify.node,notify.name,
                                     notify.framePhase,notify.cycle});
            if(!children_[i]->healthy()){healthy_=false;error_=children_[i]->error();}
        }
        if(motions_[i])motions_[i]->update(dt,parameters_);
        if(controllers_[i])controllers_[i]->update(dt,parameters_);
        if(n.operation&&n.operation->update) n.operation->update(n.config,parameters_,dt,nativeState_[i]);
        for(size_t t=0;t<n.traits.size();++t){auto& st=traitState_[i][t];const auto& op=n.traits[t].implementation;
            if(st.enabled&&op&&op->postUpdate&&!op->postUpdate(n.traits[t].config,parameters_,dt,st.data))traitFailure(i,t);
        }
        if(n.kind!=AnimationGraph::Kind::Clip) continue;
        const float duration=graph_->model()->clips[size_t(n.clip)].duration;
        const double prev=unwrappedClocks_[i];
        double advance=syncRate_[i]>=0.?double(dt)*syncRate_[i]:
            double(dt)*std::clamp(value(parameters_,n.parameter,n.speed),0.f,1000000.f);
        if(const int leader=syncLeader_[i];leader>=0) {
            const double leaderDuration=graph_->model()->clips[size_t(graph_->nodes()[size_t(leader)].clip)].duration;
            advance=unwrappedClocks_[size_t(leader)]/leaderDuration*double(duration)-prev;
        }
        const double next=prev+advance;
        if(duration>0.f && !n.notifies.empty()) {
            constexpr size_t kNotifyLimit=1024;
            for(const auto& key:n.notifies) {
                if(notifies_.size()>=kNotifyLimit)break;
                const double before=prev-(clipStarted_[i]?1.e-9:0.);
                const double until=n.loop?next:std::min(next,double(duration));
                const auto emit=[&](double at,int64_t cycle) {
                    if(notifies_.size()>=kNotifyLimit)return;
                    const float phase=advance>0?float(std::clamp(
                        (at-prev)/advance,0.,1.)):0.f;
                    notifies_.push_back({n.id,key.name,phase,cycle});
                };
                if(n.loop) {
                    const int64_t first=std::max<int64_t>(0,
                        int64_t(std::floor((before-double(key.time))/duration))+1);
                    const int64_t last=int64_t(std::floor(
                        (until-double(key.time))/duration));
                    for(int64_t cycle=first;cycle<=last &&
                        notifies_.size()<kNotifyLimit;++cycle) {
                        const double at=double(key.time)+double(cycle)*duration;
                        if(at>before && at<=until)emit(at,cycle);
                    }
                } else if(key.time>before && key.time<=until) {
                    emit(key.time,0);
                }
            }
            if(notifies_.size()>=kNotifyLimit) {
                healthy_=false;error_="animation notify budget exceeded";
            }
        }
        clipStarted_[i]=0;
        unwrappedClocks_[i]=n.loop?next:std::min(next,double(duration));
        clocks_[i]=duration>0? (n.loop?
            std::fmod(unwrappedClocks_[i],double(duration)):
            unwrappedClocks_[i]):0;
    }
    std::stable_sort(notifies_.begin(),notifies_.end(),
        [](const AnimationGraphNotify& a,const AnimationGraphNotify& b) {
            return a.framePhase<b.framePhase;
        });
    for(const auto& t:graph_->states()[size_t(state_)].transitions) {
        if(time_<t.after || !std::all_of(t.conditions.begin(),t.conditions.end(),[&](const auto& c){return matches(c,parameters_);})) continue;
        evaluate(last_); source_=last_; state_=t.to; time_=0; fadeTime_=0; fadeDuration_=t.fade;
        std::fill(clocks_.begin(),clocks_.end(),0);
        std::fill(unwrappedClocks_.begin(),unwrappedClocks_.end(),0);
        std::fill(clipStarted_.begin(),clipStarted_.end(),1);
        for(auto& storage:nativeState_) std::fill(storage.begin(),storage.end(),0);
        resetNodes();
        break;
    }
}
void AnimationGraphInstance::evaluateCurrent(Pose& result) {
    const auto& nodes=graph_->nodes(); const auto& model=*graph_->model();
    const auto& state=graph_->states()[size_t(state_)];
    const int output=state.output;
    const bool morph=!graph_->morphFree();
    for(int instruction:state.program) {
        const size_t i=size_t(instruction);
        const auto& n=nodes[i]; auto& p=poses_[i];
        graph_profile::Scope timing(executionProfile_?&*executionProfile_:nullptr,i,graph_profile::Phase::Evaluate);
        float w=std::clamp(value(parameters_,n.parameter,n.weight),0.f,1.f);
        if(n.kind==AnimationGraph::Kind::Subgraph){bindChild(i);children_[i]->evaluate(p);if(!children_[i]->healthy()){healthy_=false;error_=children_[i]->error();}}
        else if(n.kind==AnimationGraph::Kind::MotionMatching)motions_[i]->evaluate(p);
        else if(n.kind==AnimationGraph::Kind::Controller)controllers_[i]->evaluate(p,parameters_);
        else if(n.kind==AnimationGraph::Kind::Bind) assignPose(p,bind_,morph);
        else if(n.kind==AnimationGraph::Kind::Clip) {
            assignPose(p,bind_,morph); const auto& clip=model.clips[size_t(n.clip)];
            double t=clocks_[i];
            sampleClip(clip,float(t),p);
        } else {
            assignPose(p,poses_[size_t(n.a)],morph);
            if(n.kind==AnimationGraph::Kind::Stack){
                for(size_t t=n.traits.size();t-->0;){auto& st=traitState_[i][t];const auto& op=n.traits[t].implementation;
                    if(st.enabled&&op&&op->preEvaluate){Pose candidate=p;if(!op->preEvaluate(n.traits[t].config,parameters_,st.data,candidate)||!validPose(candidate,p.size()))traitFailure(i,t);else p=std::move(candidate);}
                }
                for(size_t t=0;t<n.traits.size();++t){auto& st=traitState_[i][t];const auto& trait=n.traits[t];
                    if(st.enabled&&trait.rig)trait.rig->execute(model,parameters_,p,world_,st.rig.get());
                    if(st.enabled&&trait.implementation&&trait.implementation->postEvaluate){Pose candidate=p;if(!trait.implementation->postEvaluate(trait.config,parameters_,st.data,candidate)||!validPose(candidate,p.size()))traitFailure(i,t);else p=std::move(candidate);}
                }
            } else if(n.kind==AnimationGraph::Kind::Rig)n.rig->execute(model,parameters_,p,world_,rigs_[i].get());
            else if(n.kind==AnimationGraph::Kind::Blend) blend(p,poses_[size_t(n.b)],w,n.mask,morph);
            else if(n.kind==AnimationGraph::Kind::Additive) {
                const auto& delta=poses_[size_t(n.b)];
                for(size_t k=0;k<p.size();++k) {
                    if(!n.mask.empty()&&!n.mask[k]) continue;
                    const auto& d=delta[k]; const auto& r=bind_[k]; auto& x=p[k];
                    x.t+=(d.t-r.t)*w;
                    const quat inv{-r.r.x,-r.r.y,-r.r.z,r.r.w}; x.r=qnormalize(x.r*nlerp(quat{},inv*d.r,w));
                    x.s+= (d.s-r.s)*w;
                    if(morph) for(int c=0;c<kMaxMorphTargets;++c) x.w[c]+=(d.w[c]-r.w[c])*w;
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
        blend(result,source_,1-w,{},!graph_->morphFree());
    }
    assignPose(last_,result,!graph_->morphFree());
}
} // namespace aiverse::render
