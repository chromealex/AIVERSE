#include "FullBodyIK.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aiverse::render {
namespace {
[[noreturn]] void fail(const std::string& s){throw std::runtime_error("full-body IK: "+s);}
float number(const Json& j,float fallback){if(j.isNull())return fallback;double v=j.toDouble();if(!j.isNumber()||!std::isfinite(v)||std::abs(v)>1000000)fail("invalid number");return float(v);}
float3 vector(const Json& j,float3 fallback){if(j.isNull())return fallback;if(!j.isArray()||j.size()!=3)fail("expected three components");return {number(j[0],0),number(j[1],0),number(j[2],0)};}
quat inverse(quat q){return {-q.x,-q.y,-q.z,q.w};}
float3 logRotation(quat q){q=qnormalize(q);if(q.w<0)q={-q.x,-q.y,-q.z,-q.w};float l=length({q.x,q.y,q.z});return l<1e-7f?float3{}:float3{q.x,q.y,q.z}*(2*std::atan2(l,q.w)/l);}
quat expRotation(float3 v){float l=length(v);return l<1e-7f?quat{}:quat::axisAngle(v*(1/l),l);}
float3 position(const mat4& m){return {m.at(0,3),m.at(1,3),m.at(2,3)};}
float3 control(const AnimationParameters& p,const std::string& name,float3 fallback){auto i=p.vectors.find(name);if(i==p.vectors.end())return fallback;auto v=i->second;return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)?v:fallback;}
float weightOf(const AnimationParameters& p,const FullBodyIK::Effector& e){auto i=p.values.find(e.weightParameter);return i==p.values.end()||!std::isfinite(i->second)?e.weight:std::clamp(i->second,0.f,1.f);}
quat euler(float3 r){return quat::axisAngle({0,0,1},r.z)*quat::axisAngle({0,1,0},r.y)*quat::axisAngle({1,0,0},r.x);}
// Positive-definite 3x3 normal matrix, solved without inversion.
float3 solve3(float a[3][3],float3 b){float l[3][3]{};for(int i=0;i<3;++i)for(int j=0;j<=i;++j){float s=a[i][j];for(int k=0;k<j;++k)s-=l[i][k]*l[j][k];l[i][j]=i==j?std::sqrt(std::max(s,1e-12f)):s/l[j][j];}float rhs[]={b.x,b.y,b.z},y[3]{},x[3]{};for(int i=0;i<3;++i){float s=rhs[i];for(int k=0;k<i;++k)s-=l[i][k]*y[k];y[i]=s/l[i][i];}for(int i=2;i>=0;--i){float s=y[i];for(int k=i+1;k<3;++k)s-=l[k][i]*x[k];x[i]=s/l[i][i];}return {x[0],x[1],x[2]};}
}
std::shared_ptr<const FullBodyIK> FullBodyIK::compile(const Model& m,const Json& j,const AnimationParameters& p){
    auto s=std::make_shared<FullBodyIK>();s->root_=m.findNode(j.getStr("bone"));if(s->root_<0)fail("unknown root bone");
    auto iterations=j.getInt("iterations",24);if(iterations<1||iterations>64)fail("iterations must be 1..64");s->iterations_=int(iterations);
    s->tolerance_=number(j["tolerance"],.001f);s->damping_=number(j["damping"],.05f);s->rootPull_=number(j["rootPull"],0);
    if(s->tolerance_<=0||s->tolerance_>1||s->damping_<.001f||s->damping_>10||s->rootPull_<0||s->rootPull_>1)fail("invalid solver settings");
    auto descendant=[&](int bone,int ancestor){for(size_t k=0;bone>=0&&k<=m.nodes.size();++k){if(bone==ancestor)return true;bone=m.nodes[size_t(bone)].parent;}return false;};
    if(j.has("contacts"))s->contacts_=AnimationContacts::compile(m,j["contacts"],p);
    auto contactBones=s->contacts_?s->contacts_->bones():std::vector<int>{};
    for(int b:contactBones)if(!descendant(b,s->root_))fail("contact bone must descend from root");
    if((!j["effectors"].isArray()&&!(s->contacts_&&(j["effectors"].isNull()||(j["effectors"].isObject()&&j["effectors"].size()==0))))||(j["effectors"].size()==0&&contactBones.empty())||j["effectors"].size()>16)fail("expected 1..16 effectors, or contacts with empty effectors");
    for(const auto& v:j["effectors"].items()){
        Effector e;e.bone=m.findNode(v.getStr("bone"));e.target=v.getStr("target");e.rotation=v.getStr("rotation");e.weightParameter=v.getStr("weightParameter");e.weight=number(v["weight"],1);e.rotationWeight=number(v["rotationWeight"],e.rotation.empty()?0:1);
        if(e.bone<0||!descendant(e.bone,s->root_))fail("effector must descend from root");
        if(!p.vectors.count(e.target)||(!e.rotation.empty()&&!p.vectors.count(e.rotation))||(!e.weightParameter.empty()&&!p.values.count(e.weightParameter)))fail("unknown control parameter");
        if(e.weight<0||e.weight>1||e.rotationWeight<0||e.rotationWeight>1||(e.rotationWeight>0&&e.rotation.empty()))fail("weights must be 0..1");
        if(std::any_of(s->effectors_.begin(),s->effectors_.end(),[&](auto& x){return x.bone==e.bone;}))fail("duplicate effector");s->effectors_.push_back(e);
    }
    // Reverse hierarchy order: distal joints first, common ancestors combine all goals.
    for(auto it=m.order.rbegin();it!=m.order.rend();++it){int bone=*it;std::vector<size_t> affected;
        if(!descendant(bone,s->root_))continue;
        for(size_t i=0;i<s->effectors_.size();++i)if(descendant(s->effectors_[i].bone,bone))affected.push_back(i);
        bool contact=false;for(int b:contactBones)contact|=descendant(b,bone);
        if(affected.empty()&&!contact)continue;
        s->joints_.push_back(Joint{bone});s->affected_.push_back(std::move(affected));
    }
    if(s->joints_.empty()||s->joints_.size()>128)fail("solver supports 1..128 affected joints");
    if(!j["joints"].isNull()&&!j["joints"].isArray())fail("joints must be array");
    std::vector<int> configured;
    for(const auto& v:j["joints"].items()){
        int bone=m.findNode(v.getStr("bone"));auto it=std::find_if(s->joints_.begin(),s->joints_.end(),[&](auto& x){return x.bone==bone;});if(it==s->joints_.end()||std::find(configured.begin(),configured.end(),bone)!=configured.end())fail("unknown, unaffected or duplicate joint");configured.push_back(bone);
        it->stiffness=number(v["stiffness"],0);it->minimum=vector(v["min"],it->minimum);it->maximum=vector(v["max"],it->maximum);it->preferred=vector(v["preferred"],{});it->limited=v.has("min")||v.has("max");
        if(it->stiffness<0||it->stiffness>1||it->minimum.x>it->maximum.x||it->minimum.y>it->maximum.y||it->minimum.z>it->maximum.z)fail("invalid joint constraint");
    }
    return s;
}
FullBodyIK::Result FullBodyIK::solve(const Model& m,const AnimationParameters& parameters,Pose& pose,float weight,AnimationContactState* persistent) const {
    Result result;AnimationContactState temporary;auto& state=persistent?*persistent:temporary;
    if(pose.size()!=m.nodes.size()||!std::isfinite(weight)||weight<=0){state.reset();return result;}weight=std::min(weight,1.f);
    Pose input=pose;std::vector<mat4> world;worldMatrices(m,pose,world);
    if(contacts_)contacts_->plant(m,parameters,pose,world,state);
    std::vector<IKPositionGoal> base;std::vector<IKRotationGoal> baseRotations;
    for(const auto& e:effectors_){float w=weightOf(parameters,e);if(w<=0)continue;bool planted=false;
        if(contacts_)for(size_t i=0;i<contacts_->feet().size();++i)planted|=contacts_->feet()[i].bone==e.bone&&state.feet[i].planted;
        if(!planted)base.push_back({e.bone,{},control(parameters,e.target,position(world[size_t(e.bone)])),w});
        if(!planted&&e.rotationWeight>0)baseRotations.push_back({e.bone,euler(control(parameters,e.rotation,{})),w*e.rotationWeight});
    }
    if(base.empty()&&baseRotations.empty()&&!contacts_){result.converged=true;return result;}
    auto descendant=[&](int bone,int ancestor){for(size_t k=0;bone>=0&&k<=m.nodes.size();++k){if(bone==ancestor)return true;bone=m.nodes[size_t(bone)].parent;}return false;};
    auto constrain=[&](const Joint& j,quat q){if(!j.limited)return q;auto v=logRotation(inverse(m.nodes[size_t(j.bone)].r)*q);v={std::clamp(v.x,j.minimum.x,j.maximum.x),std::clamp(v.y,j.minimum.y,j.maximum.y),std::clamp(v.z,j.minimum.z,j.maximum.z)};return qnormalize(m.nodes[size_t(j.bone)].r*expRotation(v));};
    for(const auto& j:joints_)if(j.stiffness<1)pose[size_t(j.bone)].r=constrain(j,qnormalize(pose[size_t(j.bone)].r*expRotation(j.preferred*(1-j.stiffness))));
    worldMatrices(m,pose,world);
    std::vector<IKPositionGoal> goals;std::vector<IKRotationGoal> rotations;
    auto refresh=[&](){goals=base;rotations=baseRotations;if(contacts_)contacts_->goals(m,parameters,pose,world,state,goals,rotations);};
    auto error=[&](){refresh();float v=0;for(auto& g:goals)v=std::max(v,length(g.target-world[size_t(g.bone)].transformPoint(g.offset))*std::min(g.weight,1.f));for(auto& g:rotations)v=std::max(v,length(logRotation(g.target*inverse(rotationOf(world[size_t(g.bone)]))))*std::min(g.weight,1.f));if(contacts_){float e=0;contacts_->balance(parameters,world,state,&e);v=std::max(v,e);}return v;};
    for(int iteration=0;iteration<iterations_;++iteration){
        result.error=error();if(result.error<=tolerance_)break;result.iterations=iteration+1;
        float3 delta{};float sum=0;for(auto& g:base){delta+=(g.target-world[size_t(g.bone)].transformPoint(g.offset))*g.weight;sum+=g.weight;}delta=delta*(sum>0?rootPull_/sum:0);
        if(contacts_){float3 correction{};float total=0;for(size_t i=base.size();i<goals.size();++i){auto& g=goals[i];correction+=(g.target-world[size_t(g.bone)].transformPoint(g.offset))*g.weight;total+=g.weight;}if(total>0)delta+=correction*(contacts_->rootAdjust/total);delta+=contacts_->balance(parameters,world,state);float l=length(delta);if(l>contacts_->maxCorrection)delta=delta*(contacts_->maxCorrection/l);}
        int parent=m.nodes[size_t(root_)].parent;pose[size_t(root_)].t+=(parent<0?mat4{}:world[size_t(parent)].inverse()).transformDir(delta);worldMatrices(m,pose,world);
        for(const auto& j:joints_){if(j.stiffness>=1)continue;refresh();float a[3][3]{};float3 b{};for(int k=0;k<3;++k)a[k][k]=damping_*damping_;auto origin=position(world[size_t(j.bone)]);
            for(const auto& g:goals){if(!descendant(g.bone,j.bone))continue;auto point=world[size_t(g.bone)].transformPoint(g.offset),r=point-origin,residual=g.target-point;float3 columns[]={cross({1,0,0},r),cross({0,1,0},r),cross({0,0,1},r)};float rhs[3]{};for(int x=0;x<3;++x){rhs[x]=dot(columns[x],residual)*g.weight;for(int y=0;y<3;++y)a[x][y]+=dot(columns[x],columns[y])*g.weight;}b+=float3{rhs[0],rhs[1],rhs[2]};}
            for(const auto& g:rotations)if(descendant(g.bone,j.bone)){b+=logRotation(g.target*inverse(rotationOf(world[size_t(g.bone)])))*g.weight;for(int k=0;k<3;++k)a[k][k]+=g.weight;}
            auto change=solve3(a,b)*(1-j.stiffness);float angle=length(change);if(angle>.35f)change=change*(.35f/angle);
            int jp=m.nodes[size_t(j.bone)].parent;quat parentRotation=jp<0?quat{}:rotationOf(world[size_t(jp)]);
            pose[size_t(j.bone)].r=constrain(j,qnormalize(inverse(parentRotation)*expRotation(change)*parentRotation*pose[size_t(j.bone)].r));worldMatrices(m,pose,world);
        }
    }
    if(weight<1)for(size_t i=0;i<pose.size();++i){pose[i].t=lerp(input[i].t,pose[i].t,weight);pose[i].r=nlerp(input[i].r,pose[i].r,weight);}
    worldMatrices(m,pose,world);result.error=error();result.converged=result.error<=tolerance_;
    if(contacts_)result.contacts=state.report=contacts_->report(m,parameters,pose,world,state);return result;
}
}
