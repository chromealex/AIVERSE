#include "SurfaceLocomotion.h"
#include "AnimationGraph.h"
#include "FullBodyIK.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aiverse::render {
namespace {
[[noreturn]] void fail(const std::string& s){throw std::runtime_error("surface locomotion: "+s);}
float number(const Json& v,float d){if(v.isNull())return d;float f=float(v.toDouble());if(!v.isNumber()||!std::isfinite(f)||std::abs(f)>10000)fail("invalid number");return f;}
float3 vector(const Json& v,float3 d={}){if(v.isNull())return d;if(!v.isArray()||v.size()!=3)fail("expected vector");return {number(v[0],0),number(v[1],0),number(v[2],0)};}
float scalar(const AnimationParameters& p,const std::string& key,float fallback){auto it=p.values.find(key);return it==p.values.end()||!std::isfinite(it->second)?fallback:it->second;}
float3 vector(const AnimationParameters& p,const std::string& key){auto it=p.vectors.find(key);if(it==p.vectors.end())return {};auto v=it->second;return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)?v:float3{};}
quat between(float3 a,float3 b){a=normalize(a);b=normalize(b);float d=std::clamp(dot(a,b),-1.f,1.f);if(d<-.9999f)return quat::axisAngle(normalize(cross(a,std::abs(a.x)<.9f?float3{1,0,0}:float3{0,0,1})),3.14159265f);auto c=cross(a,b);return qnormalize({c.x,c.y,c.z,1+d});}
float3 position(const mat4& m){return m.transformPoint({});}
}
std::shared_ptr<const SurfaceLocomotion> SurfaceLocomotion::compile(const Model& m,const Json& j,const AnimationParameters& p){
    auto c=std::make_shared<SurfaceLocomotion>();c->followSurface_=j.getBool("followSurface",true);c->root_=m.findNode(j.getStr("bone"));if(c->root_<0)fail("unknown body bone");
    auto binding=[&](const char* name,bool vec){auto s=j.getStr(name);if(!s.empty()&&(vec?!p.vectors.count(s):!p.values.count(s)))fail(std::string("unknown parameter ")+name);return s;};
    c->velocity_=binding("velocity",true);if(c->velocity_.empty())fail("velocity parameter is required");c->adhesion_=binding("adhesion",false);c->reset_=binding("resetParameter",false);
    auto positive=[&](const char* name,float d,float max){float n=number(j[name],d);if(n<=0||n>max)fail(std::string("invalid ")+name);return n;};
    c->clearance_=positive("clearance",.5f,10);c->probe_=positive("probe",.8f,20);c->stepDistance_=positive("stepDistance",.3f,10);c->stepDuration_=positive("stepDuration",.25f,5);c->stepHeight_=positive("stepHeight",.15f,5);c->turnSpeed_=positive("turnSpeed",4,20);c->maxSpeed_=positive("maxSpeed",2,20);c->bodyRadius_=positive("bodyRadius",.2f,10);
    if(c->bodyRadius_>c->clearance_)fail("bodyRadius exceeds clearance");
    if(!j["legs"].isArray()||j["legs"].size()<3||j["legs"].size()>16)fail("expected 3..16 legs");
    auto minimum=j.getInt("minSupport",3),maximum=j.getInt("maxSwing",1);if(minimum<1||minimum>=int64_t(j["legs"].size())||maximum<1||maximum>int64_t(j["legs"].size())-minimum)fail("invalid support/swing budget");c->minSupport_=int(minimum);c->maxSwing_=int(maximum);
    Pose bind;bindPose(m,bind);std::vector<mat4> world;worldMatrices(m,bind,world);auto rootInverse=world[size_t(c->root_)].inverse();
    auto descendant=[&](int b){for(size_t i=0;b>=0&&i<m.nodes.size();++i){if(b==c->root_)return true;b=m.nodes[size_t(b)].parent;}return false;};
    for(const auto& v:j["legs"].items()){Leg l;l.hip=m.findNode(v.getStr("hip"));l.knee=m.findNode(v.getStr("knee"));l.foot=m.findNode(v.getStr("foot"));if(l.hip<0||l.knee<0||l.foot<0||l.hip==c->root_||!descendant(l.hip)||m.nodes[size_t(l.knee)].parent!=l.hip||m.nodes[size_t(l.foot)].parent!=l.knee)fail("leg needs a two-bone chain below body");
        for(auto& prev:c->legs_)for(int a:{prev.hip,prev.knee,prev.foot})for(int b:{l.hip,l.knee,l.foot})if(a==b)fail("legs must have independent chains");
        l.home=vector(v["home"],rootInverse.transformPoint(position(world[size_t(l.foot)])));l.pole=vector(v["pole"],rootInverse.transformPoint(position(world[size_t(l.knee)]))+float3{0,.5f,0});
        l.reach=length(bind[size_t(l.knee)].t)+length(bind[size_t(l.foot)].t);if(l.reach<.001f)fail("zero length leg");c->legs_.push_back(l);
    }
    Json surfaces=Json::object();surfaces.set("world",Json::fromBool(j.getBool("world",true)));if(j.has("obstacles"))surfaces.set("obstacles",j["obstacles"]);c->surfaces_=AnimationContacts::compile(m,surfaces,p);
    if(j.has("capsules")&&j["capsules"].size()>0){Json solver=Json::object();solver.set("bone",j["bone"]);solver.set("iterations",j.has("collisionIterations")?j["collisionIterations"]:Json::number(int64_t(24)));auto contacts=surfaces;contacts.set("capsules",j["capsules"]);contacts.set("rootAdjust",Json::number(0.0));solver.set("contacts",contacts);Json effectors=Json::array();auto parameters=p;
        for(size_t i=0;i<c->legs_.size();++i){std::string key="$surface/"+std::to_string(i);parameters.set(key,float3{});Json e=Json::object();e.set("bone",Json::string(m.nodes[size_t(c->legs_[i].foot)].name));e.set("target",Json::string(key));effectors.push(e);}solver.set("effectors",effectors);Json joints=Json::array(),locked=Json::object();locked.set("bone",j["bone"]);locked.set("stiffness",Json::number(1.0));joints.push(locked);solver.set("joints",joints);c->collisionSolver_=FullBodyIK::compile(m,solver,parameters);
    }return c;
}
void SurfaceLocomotion::solve(const Model& m,const AnimationParameters& p,Pose& pose,float dt,SurfaceLocomotionState& s,float weight)const{
    if(pose.size()!=m.nodes.size()||!std::isfinite(dt)||dt<0||!std::isfinite(weight)||weight<=0){s.reset();return;}
    weight=std::min(weight,1.f);dt=std::min(dt,.1f);Pose input=pose;std::vector<mat4> world;worldMatrices(m,pose,world);
    mat4 host=p.environment?p.environment->modelToWorld:mat4{},toModel=host.inverse();float scale=length(host.transformDir({1,0,0}));if(scale<1e-5f){s.reset();return;}
    float reset=scalar(p,reset_,0);if(s.initialized&&reset!=s.resetToken)s.reset();
    const bool initialize=!s.initialized;
    if(initialize){auto root=host*world[size_t(root_)];s.position=position(root);s.rotation=rotationOf(root);s.normal=s.rotation.rotate({0,1,0});s.legs.resize(legs_.size());s.host=host;s.resetToken=reset;s.initialized=true;}
    auto ray=[&](float3 a,float3 b,IKHit& h){if(!surfaces_->ground(p,toModel.transformPoint(a),toModel.transformPoint(b),h,true))return false;h.point=host.transformPoint(h.point);h.normal=normalize(host.transformDir(h.normal));return true;};
    auto latch=[&](SurfaceLocomotionState::Leg& leg){mat4 frame;leg.framed=surfaces_->surfaceFrame(p,leg.surface,frame);if(leg.framed){auto inv=frame.inverse();leg.localAnchor=inv.transformPoint(leg.anchor);leg.localNormal=normalize(inv.transformDir(leg.normal));}};
    auto rootMatrix=[&](){return mat4::trs(s.position,s.rotation,{scale,scale,scale});};
    auto placeBody=[&](){auto local=toModel*rootMatrix();int parent=m.nodes[size_t(root_)].parent;if(parent>=0)local=world[size_t(parent)].inverse()*local;pose[size_t(root_)].t=position(local);pose[size_t(root_)].r=rotationOf(local);worldMatrices(m,pose,world);};
    if(initialize||dt>0){
        // Move the body with its host while retaining world-space stance anchors.
        auto change=host*s.host.inverse();s.position=change.transformPoint(s.position);s.rotation=qnormalize(rotationOf(change)*s.rotation);s.normal=normalize(change.transformDir(s.normal));s.host=host;
        if(s.supportFramed&&followSurface_){mat4 frame;if(surfaces_->surfaceFrame(p,s.support,frame)){auto motion=frame*s.supportFrame.inverse();if(!(s.support&(uint64_t(1)<<63)))motion=motion*change.inverse();s.position=motion.transformPoint(s.position);s.rotation=qnormalize(rotationOf(motion)*s.rotation);s.normal=normalize(motion.transformDir(s.normal));s.supportFrame=frame;}else s.supportFramed=false;}
        for(auto& leg:s.legs){mat4 frame;if(leg.planted&&!leg.swing&&leg.framed){if(surfaces_->surfaceFrame(p,leg.surface,frame)){leg.anchor=frame.transformPoint(leg.localAnchor);leg.normal=normalize(frame.transformDir(leg.localNormal));}else leg.planted=leg.framed=false;}if(leg.swing&&leg.targetFramed){if(surfaces_->surfaceFrame(p,leg.surface,frame)){leg.target=frame.transformPoint(leg.localTarget);leg.targetNormal=normalize(frame.transformDir(leg.localTargetNormal));}else leg.swing=leg.planted=leg.targetFramed=false;}}
        bool adhesive=scalar(p,adhesion_,1)>0;float3 localVelocity=vector(p,velocity_);localVelocity.y=0;float speed=length(localVelocity);if(speed>maxSpeed_)localVelocity=localVelocity*(maxSpeed_/speed);
        auto up=s.rotation.rotate({0,1,0});auto velocity=s.rotation.rotate(localVelocity)*scale;
        IKHit support,front;bool hasSupport=adhesive&&ray(s.position+s.normal*probe_*scale*.25f,s.position-s.normal*(clearance_+probe_)*scale,support);
        bool hasFront=adhesive&&length(velocity)>1e-5f&&ray(s.position,s.position+normalize(velocity)*(clearance_+stepDistance_)*scale,front)&&dot(front.normal,normalize(velocity))<-.1f;
        if(hasFront&&(!hasSupport||dot(front.normal,s.normal)<.7f))support=front,hasSupport=true;
        s.supportFramed=hasSupport&&surfaces_->surfaceFrame(p,support.surface,s.supportFrame);if(hasSupport)s.support=support.surface;
        if(hasSupport){s.normal=support.normal;float angle=std::acos(std::clamp(dot(up,s.normal),-1.f,1.f));auto rotation=between(up,s.normal);float amount=angle>1e-5f?std::min(1.f,turnSpeed_*dt/angle):1;s.rotation=qnormalize(nlerp(quat{},rotation,initialize?1.f:amount)*s.rotation);}
        int planted=0,swinging=0;
        for(size_t i=0;i<legs_.size();++i){auto& leg=s.legs[i];
            if(!adhesive){leg.planted=leg.swing=false;continue;}
            if(leg.swing){leg.elapsed+=dt;float t=std::min(1.f,leg.elapsed/stepDuration_);auto lift=normalize(leg.normal+leg.targetNormal);leg.anchor=lerp(leg.start,leg.target,t)+lift*(std::sin(t*3.14159265f)*stepHeight_*scale);if(t>=1){IKHit hit;if(ray(leg.target+leg.targetNormal*probe_*scale*.25f,leg.target-leg.targetNormal*probe_*scale*.25f,hit)&&hit.surface==leg.surface){leg.anchor=hit.point;leg.normal=hit.normal;leg.planted=true;latch(leg);}else leg.planted=false;leg.swing=false;}}
            if(leg.planted&&!leg.swing){IKHit hit;if(!ray(leg.anchor+leg.normal*.02f*scale,leg.anchor-leg.normal*.04f*scale,hit)||hit.surface!=leg.surface||length(hit.point-leg.anchor)>.05f*scale)leg.planted=false;}
            if(initialize&&!leg.planted&&!leg.swing){auto home=rootMatrix().transformPoint(legs_[i].home);IKHit hit;if(ray(home+s.normal*probe_*scale,home-s.normal*probe_*scale,hit)){leg.anchor=hit.point;leg.normal=hit.normal;leg.surface=hit.surface;leg.planted=true;latch(leg);}}
            planted+=leg.planted&&!leg.swing;swinging+=leg.swing;
        }
        if(hasSupport&&planted>=minSupport_){
            auto desired=s.position+s.rotation.rotate(localVelocity)*(dt*scale);float distance=dot(desired-support.point,s.normal);auto correction=s.normal*((clearance_*scale-distance)*std::min(1.f,dt*12.f));desired+=correction;
            auto old=s.position;s.position=desired;placeBody();bool reachable=true;
            for(size_t i=0;i<legs_.size();++i)if(s.legs[i].planted&&!s.legs[i].swing&&length(s.legs[i].anchor-host.transformPoint(position(world[size_t(legs_[i].hip)])))>legs_[i].reach*scale*.98f)reachable=false;
            if(!reachable)s.position=old+correction;
            IKHit hit;if(surfaces_->contact(p,toModel.transformPoint(s.position),bodyRadius_,hit))s.position+=host.transformDir(hit.normal*hit.depth);
        }
        placeBody();
        // Largest stance error first, stable round-robin tie breaking, bounded simultaneous swings.
        if(adhesive&&hasSupport&&dt>0&&swinging<maxSwing_&&planted>=minSupport_){int best=-1;float urgency=stepDistance_*scale;IKHit bestHit;
            for(size_t offset=0;offset<legs_.size();++offset){size_t i=(s.next+offset)%legs_.size();auto& leg=s.legs[i];if(leg.swing||(leg.planted&&planted<=minSupport_))continue;auto home=rootMatrix().transformPoint(legs_[i].home)+s.rotation.rotate(localVelocity)*(stepDuration_*.5f*scale);IKHit hit;
                if(!ray(home+s.normal*probe_*scale,home-s.normal*probe_*scale,hit))continue;IKHit blocked;auto original=hit.point;
                // At corners the nominal home can be inside the adjacent solid. Slide
                // the candidate along its support plane, never through that solid.
                for(int attempt=0;attempt<4;++attempt){if(!surfaces_->contact(p,toModel.transformPoint(hit.point+hit.normal*.01f*scale),.005f,blocked))break;auto push=host.transformDir(blocked.normal*(blocked.depth+.002f));hit.point+=push-hit.normal*dot(push,hit.normal);}
                if(length(hit.point-original)>probe_*scale||surfaces_->contact(p,toModel.transformPoint(hit.point+hit.normal*.01f*scale),.005f,blocked))continue;
                IKHit checked;if(!ray(hit.point+hit.normal*.03f*scale,hit.point-hit.normal*.03f*scale,checked)||checked.surface!=hit.surface)continue;hit=checked;auto hip=host.transformPoint(position(world[size_t(legs_[i].hip)]));if(length(hit.point-hip)>legs_[i].reach*scale*.98f)continue;
                float error=leg.planted?length(hit.point-leg.anchor):stepDistance_*scale+length(hit.point-host.transformPoint(position(world[size_t(legs_[i].foot)])));if(leg.planted&&length(localVelocity)>1e-5f&&error>.02f*scale&&length(leg.anchor-hip)>legs_[i].reach*scale*.9f)error+=stepDistance_*scale;if(error>urgency){urgency=error;best=int(i);bestHit=hit;}}
            if(best>=0){auto& leg=s.legs[size_t(best)];leg.swing=true;leg.start=leg.planted?leg.anchor:host.transformPoint(position(world[size_t(legs_[size_t(best)].foot)]));leg.target=bestHit.point;leg.targetNormal=bestHit.normal;leg.surface=bestHit.surface;leg.elapsed=0;leg.framed=false;mat4 frame;leg.targetFramed=surfaces_->surfaceFrame(p,leg.surface,frame);if(leg.targetFramed){leg.localTarget=frame.inverse().transformPoint(leg.target);leg.localTargetNormal=normalize(frame.inverse().transformDir(leg.targetNormal));}s.next=(size_t(best)+1)%legs_.size();}
        }
        s.report.attached=adhesive&&hasSupport&&planted>=minSupport_;
    }
    placeBody();s.report.active=true;s.report.supports=s.report.stepping=0;s.report.footError=0;s.report.position=s.position;s.report.normal=s.normal;
    for(size_t i=0;i<legs_.size();++i){auto& leg=s.legs[i];if(!leg.planted&&!leg.swing)continue;auto& def=legs_[i];auto target=toModel.transformPoint(leg.anchor),pole=toModel.transformPoint(rootMatrix().transformPoint(def.pole));solveTwoBoneIK(m,pose,world,def.hip,def.knee,def.foot,target,pole);worldMatrices(m,pose,world);s.report.supports+=!leg.swing;s.report.stepping+=leg.swing;}
    s.report.penetration=0;
    if(collisionSolver_){auto parameters=p;for(size_t i=0;i<legs_.size();++i){auto target=(s.legs[i].planted||s.legs[i].swing)?toModel.transformPoint(s.legs[i].anchor):position(world[size_t(legs_[i].foot)]);parameters.set("$surface/"+std::to_string(i),target);}auto result=collisionSolver_->solve(m,parameters,pose);s.report.penetration=result.contacts.penetration;}
    worldMatrices(m,pose,world);
    for(size_t i=0;i<legs_.size();++i)if(s.legs[i].planted||s.legs[i].swing){int foot=legs_[i].foot;auto current=rotationOf(world[size_t(foot)]),parent=rotationOf(world[size_t(legs_[i].knee)]);quat inverseParent{-parent.x,-parent.y,-parent.z,parent.w};auto normal=normalize(toModel.transformDir(s.legs[i].swing?s.legs[i].targetNormal:s.legs[i].normal));pose[size_t(foot)].r=qnormalize(inverseParent*between(current.rotate({0,1,0}),normal)*current);}
    if(weight<1)for(size_t i=0;i<pose.size();++i){pose[i].t=lerp(input[i].t,pose[i].t,weight);pose[i].r=nlerp(input[i].r,pose[i].r,weight);}worldMatrices(m,pose,world);
    if(collisionSolver_){AnimationContactState collisionState;collisionState.active=true;s.report.penetration=collisionSolver_->contacts()->report(m,p,pose,world,collisionState).penetration;}
    for(size_t i=0;i<legs_.size();++i)if(s.legs[i].planted||s.legs[i].swing)s.report.footError=std::max(s.report.footError,length(host.transformPoint(position(world[size_t(legs_[i].foot)]))-s.legs[i].anchor)/scale);
    if(s.report.supports<minSupport_||s.report.footError>.05f||s.report.penetration>.01f)s.report.attached=false;
}
}
