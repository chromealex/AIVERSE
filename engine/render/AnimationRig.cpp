#include "AnimationRig.h"
#include "FullBodyIK.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aiverse::render {
namespace {
[[noreturn]] void fail(const std::string& s){throw std::runtime_error("animation rig: "+s);}
float num(const Json& j,float fallback){if(j.isNull())return fallback;double n=j.toDouble();if(!j.isNumber()||!std::isfinite(n)||std::abs(n)>1000000)fail("invalid number");return float(n);}
float3 vec(const Json& j,float3 fallback){if(j.isNull())return fallback;if(!j.isArray()||j.size()!=3)fail("expected vector of length 3");return {num(j[0],0),num(j[1],0),num(j[2],0)};}
float3 parameter(const AnimationParameters& p,const std::string& name,float3 fallback){auto i=p.vectors.find(name);if(i==p.vectors.end())return fallback;auto v=i->second;return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)?v:fallback;}
quat inverseQ(quat q){return {-q.x,-q.y,-q.z,q.w};}
quat euler(float3 r){return quat::axisAngle({0,0,1},r.z)*quat::axisAngle({0,1,0},r.y)*quat::axisAngle({1,0,0},r.x);}
quat between(float3 a,float3 b){a=normalize(a);b=normalize(b);float d=dot(a,b);if(d<-.99999f)return quat::axisAngle(normalize(cross(a,std::abs(a.y)<.9f?float3{0,1,0}:float3{1,0,0})),3.14159265f);auto c=cross(a,b);return qnormalize({c.x,c.y,c.z,1+d});}
float3 position(const mat4& m){return {m.at(0,3),m.at(1,3),m.at(2,3)};}
}
std::shared_ptr<const AnimationRig> AnimationRig::compile(const Model& m,const Json& j,const AnimationParameters& p){
    if(!j.isArray()||j.size()>64)fail("units must be an array with at most 64 entries");
    auto result=std::make_shared<AnimationRig>();
    auto bone=[&](const Json& v,const char* key){int b=m.findNode(v.getStr(key));if(b<0)fail(std::string("unknown bone '")+v.getStr(key)+"'");return b;};
    auto binding=[&](const Json& v,const char* key){auto name=v.getStr(key);if(v.has(key)&&(!v[key].isString()||!p.vectors.count(name)))fail(std::string("unknown vector ")+key);return name;};
    for(const auto& v:j.items()){
        Unit u;u.bone=bone(v,"bone");auto kind=v.getStr("op");
        if(kind=="transform")u.kind=Kind::Transform;
        else if(kind=="copy"){u.kind=Kind::Copy;u.source=bone(v,"source");}
        else if(kind=="aim"){u.kind=Kind::Aim;u.target=binding(v,"target");if(u.target.empty())fail("aim needs target");u.axis=vec(v["axis"],u.axis);if(length(u.axis)<1e-6f)fail("aim axis is zero");}
        else if(kind=="twoBoneIK"){u.kind=Kind::TwoBoneIK;u.knee=bone(v,"knee");u.end=bone(v,"end");u.target=binding(v,"target");u.pole=binding(v,"pole");if(u.target.empty()||u.pole.empty()||m.nodes[size_t(u.knee)].parent!=u.bone||m.nodes[size_t(u.end)].parent!=u.knee||u.bone==u.end)fail("invalid IK chain/controls");}
        else if(kind=="fullBodyIK"){u.kind=Kind::FullBodyIK;u.fullBody=FullBodyIK::compile(m,v,p);}
        else if(kind=="limit"){u.kind=Kind::Limit;u.minimum=vec(v["min"],u.minimum);u.maximum=vec(v["max"],u.maximum);if(u.minimum.x>u.maximum.x||u.minimum.y>u.maximum.y||u.minimum.z>u.maximum.z)fail("inverted limits");}
        else fail("unknown operation '"+kind+"'");
        auto space=v.getStr("space","local");if(space!="local"&&space!="model")fail("space must be local or model");u.global=space=="model";
        u.additive=v.getBool("additive");u.weight=num(v["weight"],1);if(u.weight<0||u.weight>1)fail("weight must be 0..1");
        u.weightParameter=v.getStr("weightParameter");if(!u.weightParameter.empty()&&!p.values.count(u.weightParameter))fail("unknown weight parameter");
        u.translation=binding(v,"translationParameter");u.rotation=binding(v,"rotationParameter");u.scale=binding(v,"scaleParameter");
        u.t=vec(v["translation"],{});u.r=vec(v["rotation"],{});u.s=vec(v["scale"],{1,1,1});result->units_.push_back(u);
    }
    return result;
}
void AnimationRig::execute(const Model& m,const AnimationParameters& params,Pose& pose,std::vector<mat4>& world, AnimationRigState* state) const{
    if(pose.size()!=m.nodes.size())return;
    if(state)state->contacts.resize(units_.size());
    for(size_t ui=0;ui<units_.size();++ui){const auto& u=units_[ui];
        float weight=u.weight;auto wi=params.values.find(u.weightParameter);if(wi!=params.values.end()&&std::isfinite(wi->second))weight=std::clamp(wi->second,0.f,1.f);if(weight<=0){if(state)state->contacts[ui].reset();continue;}
        if(u.fullBody){u.fullBody->solve(m,params,pose,weight,state?&state->contacts[ui]:nullptr);continue;}
        worldMatrices(m,pose,world);auto& p=pose[size_t(u.bone)];NodePose desired=p;
        int parent=m.nodes[size_t(u.bone)].parent;mat4 parentWorld=parent<0?mat4{}:world[size_t(parent)];
        if(u.kind==Kind::TwoBoneIK){solveTwoBoneIK(m,pose,world,u.bone,u.knee,u.end,parameter(params,u.target,{}),parameter(params,u.pole,{0,0,1}),weight);continue;}
        if(u.kind==Kind::Aim){float3 dir=parameter(params,u.target,{})-position(world[size_t(u.bone)]);if(length(dir)<1e-6f)continue;quat current=rotationOf(world[size_t(u.bone)]);desired.r=inverseQ(rotationOf(parentWorld))*between(current.rotate(u.axis),dir)*current;}
        else if(u.kind==Kind::Copy){
            desired=pose[size_t(u.source)];
            if(u.global){auto local=parentWorld.inverse()*world[size_t(u.source)];desired.t=position(local);desired.r=rotationOf(local);desired.s={length({local.at(0,0),local.at(1,0),local.at(2,0)}),length({local.at(0,1),local.at(1,1),local.at(2,1)}),length({local.at(0,2),local.at(1,2),local.at(2,2)})};}
        } else if(u.kind==Kind::Limit){desired.t={std::clamp(p.t.x,u.minimum.x,u.maximum.x),std::clamp(p.t.y,u.minimum.y,u.maximum.y),std::clamp(p.t.z,u.minimum.z,u.maximum.z)};}
        else {
            float3 t=parameter(params,u.translation,u.t),r=parameter(params,u.rotation,u.r),s=parameter(params,u.scale,u.s);
            if(u.global){
                mat4 requested=mat4::trs(t,euler(r),s);
                if(u.additive)requested=requested*world[size_t(u.bone)];
                auto local=parentWorld.inverse()*requested;desired.t=position(local);desired.r=rotationOf(local);
                desired.s={length({local.at(0,0),local.at(1,0),local.at(2,0)}),length({local.at(0,1),local.at(1,1),local.at(2,1)}),length({local.at(0,2),local.at(1,2),local.at(2,2)})};
            }else{desired.t=u.additive?p.t+t:t;desired.r=u.additive?p.r*euler(r):euler(r);desired.s=u.additive?mul(p.s,s):s;}
        }
        p.t=lerp(p.t,desired.t,weight);p.r=nlerp(p.r,desired.r,weight);p.s=lerp(p.s,desired.s,weight);
    }
}
}
