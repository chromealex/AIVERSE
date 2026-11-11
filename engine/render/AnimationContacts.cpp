#include "AnimationContacts.h"
#include "AnimationGraph.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aiverse::render {
namespace {
[[noreturn]] void fail(const std::string& s){throw std::runtime_error("IK contacts: "+s);}
float num(const Json& j,float d){if(j.isNull())return d;double v=j.toDouble();if(!j.isNumber()||!std::isfinite(v)||std::abs(v)>1000000)fail("invalid number");return float(v);}
float3 vec(const Json& j,float3 d={}){if(j.isNull())return d;if(!j.isArray()||j.size()!=3)fail("expected vector");return {num(j[0],0),num(j[1],0),num(j[2],0)};}
float3 unit(float3 v){if(length(v)<1e-6f)fail("direction is zero");return normalize(v);}
float3 param(const AnimationParameters& p,const std::string& n,float3 d){auto i=p.vectors.find(n);if(i==p.vectors.end())return d;auto v=i->second;return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)?v:d;}
float scalar(const AnimationParameters& p,const std::string& n,float d){auto i=p.values.find(n);return i==p.values.end()?d:i->second;}
bool finite(float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
quat between(float3 a,float3 b){a=normalize(a);b=normalize(b);float d=dot(a,b);if(d<-.9999f)return quat::axisAngle(normalize(cross(a,std::abs(a.y)<.9f?float3{0,1,0}:float3{1,0,0})),3.14159265f);auto v=cross(a,b);return qnormalize({v.x,v.y,v.z,1+d});}
float3 worldPoint(const AnimationParameters& p,float3 v){return p.environment?p.environment->modelToWorld.transformPoint(v):v;}
float3 localPoint(const AnimationParameters& p,float3 v){return p.environment?p.environment->modelToWorld.inverse().transformPoint(v):v;}
float3 localNormal(const AnimationParameters& p,float3 v){return p.environment?normalize(p.environment->modelToWorld.inverse().transformDir(v)):v;}
float scaleOf(const mat4& m){return std::max({length(m.transformDir({1,0,0})),length(m.transformDir({0,1,0})),length(m.transformDir({0,0,1}))});}
struct V2{float x,y;};float cross2(V2 a,V2 b,V2 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
}
bool ikRayBox(float3 start,float3 end,float3 lo,float3 hi,const mat4& transform,IKHit& hit){
    auto inv=transform.inverse();auto a=inv.transformPoint(start),delta=inv.transformPoint(end)-a;float enter=0,leave=1;float3 normal{};
    float origin[]={a.x,a.y,a.z},direction[]={delta.x,delta.y,delta.z},minimum[]={lo.x,lo.y,lo.z},maximum[]={hi.x,hi.y,hi.z};
    for(int k=0;k<3;++k){if(std::abs(direction[k])<1e-8f){if(origin[k]<minimum[k]||origin[k]>maximum[k])return false;continue;}float t0=(minimum[k]-origin[k])/direction[k],t1=(maximum[k]-origin[k])/direction[k],sign=direction[k]>0?-1:1;if(t0>t1)std::swap(t0,t1);if(t0>enter){enter=t0;normal={};if(k==0)normal.x=sign;if(k==1)normal.y=sign;if(k==2)normal.z=sign;}leave=std::min(leave,t1);if(enter>leave)return false;}
    if(enter<0||enter>1||length(normal)<.5f)return false;hit={start+(end-start)*enter,normalize(transform.transformDir(normal)),0,0};return true;
}
bool ikSphereOBB(float3 center,float radius,float3 lo,float3 hi,const mat4& transform,IKHit& hit){
    auto local=transform.inverse().transformPoint(center);float3 nearest{std::clamp(local.x,lo.x,hi.x),std::clamp(local.y,lo.y,hi.y),std::clamp(local.z,lo.z,hi.z)};auto point=transform.transformPoint(nearest),delta=center-point;float distance=length(delta);if(distance>=radius)return false;
    if(distance>1e-7f){hit={point,delta*(1/distance),radius-distance,0};return true;}
    float3 normals[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};float d[]={local.x-lo.x,hi.x-local.x,local.y-lo.y,hi.y-local.y,local.z-lo.z,hi.z-local.z};int best=0;
    for(int i=0;i<6;++i){d[i]*=length(transform.transformDir(normals[i]));if(d[i]<d[best])best=i;}auto n=normalize(transform.transformDir(normals[best]));hit={center+n*d[best],n,radius+d[best],0};return true;
}
bool AnimationContacts::surfaceFrame(const AnimationParameters& p,uint64_t surface,mat4& frame)const{
    constexpr uint64_t external=uint64_t(1)<<63;
    if(surface&external)return p.environment&&p.environment->surfaceTransform&&p.environment->surfaceTransform(surface&~external,frame);
    if(surface==0||surface>shapes_.size())return false;const auto& shape=shapes_[size_t(surface-1)];frame=(p.environment?p.environment->modelToWorld:mat4{})*mat4::translate(param(p,shape.positionParameter,shape.point));return true;
}
bool ikSpherePlane(float3 c,float r,float3 p,float3 n,IKHit& h){float len=length(n);if(len<1e-6f)return false;n=n*(1/len);float d=dot(c-p,n);if(d>=r)return false;h={c-n*d,n,r-d,0};return true;}
bool ikSphereSphere(float3 c,float r,float3 p,float radius,IKHit& h){auto d=c-p;float l=length(d);if(l>=r+radius)return false;auto n=l>1e-7f?d*(1/l):float3{0,1,0};h={p+n*radius,n,r+radius-l,0};return true;}
bool ikSphereBox(float3 c,float r,float3 lo,float3 hi,IKHit& h){float3 p{std::clamp(c.x,lo.x,hi.x),std::clamp(c.y,lo.y,hi.y),std::clamp(c.z,lo.z,hi.z)};auto d=c-p;float l=length(d);if(l>=r)return false;if(l>1e-7f){h={p,d*(1/l),r-l,0};return true;}float distances[]={c.x-lo.x,hi.x-c.x,c.y-lo.y,hi.y-c.y,c.z-lo.z,hi.z-c.z};float3 normals[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};int best=0;for(int i=1;i<6;++i)if(distances[i]<distances[best])best=i;h={c+normals[best]*distances[best],normals[best],r+distances[best],0};return true;}
std::shared_ptr<const AnimationContacts> AnimationContacts::compile(const Model& m,const Json& j,const AnimationParameters& p){
  if (!j.isObject())
    fail("contacts must be object");
  auto c = std::make_shared<AnimationContacts>();
  c->up = unit(vec(j["up"], {0, 1, 0}));
  if (length(c->up) < .5f)
    fail("up is zero");
  c->world_ = j.getBool("world", true);
  c->rootAdjust = num(j["rootAdjust"], .5f);
  c->collisionWeight = num(j["collisionWeight"], 30);
  c->maxCorrection = num(j["maxCorrection"], .05f);
  float slope = num(j["maxSlope"], 45);
  if (c->rootAdjust < 0 || c->rootAdjust > 1 || c->collisionWeight <= 0 || c->collisionWeight > 1000 ||
      c->maxCorrection <= 0 || c->maxCorrection > 2 || slope < 0 || slope >= 90)
    fail("invalid contact settings");
  c->maxSlope_ = std::cos(slope * .01745329252f);
  auto bone = [&](const Json &v, const char *k) {
    int b = m.findNode(v.getStr(k));
    if (b < 0)
      fail(std::string("unknown bone ") + v.getStr(k));
    return b;
  };
  auto array = [&](const char *k, size_t max) {
    if (j.has(k) && ((!j[k].isArray() && !(j[k].isObject() && j[k].size() == 0)) || j[k].size() > max))
      fail(std::string("invalid array ") + k);
  };
  array("feet", 8);
  array("capsules", 16);
  array("obstacles", 32);
  for (const auto &v : j["feet"].items()) {
    Foot f;
    f.bone = bone(v, "bone");
    f.offset = vec(v["offset"]);
    f.up = unit(vec(v["up"], {0, 1, 0}));
    f.plant = v.getStr("plantParameter");
    f.radius = num(v["radius"], .08f);
    f.probeUp = num(v["probeUp"], .3f);
    f.probeDown = num(v["probeDown"], .6f);
    f.release = num(v["releaseDistance"], .5f);
    f.weight = num(v["weight"], 20);
    f.align = num(v["align"], .5f);
    if ((!f.plant.empty() && !p.values.count(f.plant)) || f.radius <= 0 || f.radius > 2 || f.probeUp < 0 ||
        f.probeUp > 5 || f.probeDown <= 0 || f.probeDown > 5 || f.release <= 0 || f.release > 10 || f.weight <= 0 ||
        f.weight > 1000 || f.align < 0 || f.align > 1 || length(f.up) < .5f)
      fail("invalid foot settings");
    for (auto &other : c->feet_)
      if (other.bone == f.bone)
        fail("duplicate foot");
    c->feet_.push_back(f);
  }
    for(const auto& v:j["capsules"].items()){Capsule a;a.bone=bone(v,"bone");a.end=v.has("end")?bone(v,"end"):-1;a.offset=vec(v["offset"]);a.radius=num(v["radius"],.1f);auto samples=v.getInt("samples",4);if(samples<1||samples>8||a.radius<=0||a.radius>2||(a.end>=0&&m.nodes[size_t(a.end)].parent!=a.bone))fail("capsule requires a direct child, radius>0 and 1..8 samples");a.samples=int(samples);c->capsules_.push_back(a);}
    for(const auto& v:j["obstacles"].items()){Shape s;s.kind=v.getStr("kind");s.point=vec(v["point"]);s.positionParameter=v.getStr("positionParameter");s.normal=unit(vec(v["normal"],{0,1,0}));s.extent=vec(v["extent"],{.5f,.5f,.5f});s.radius=num(v["radius"],.5f);if((s.kind!="plane"&&s.kind!="sphere"&&s.kind!="box")||(!s.positionParameter.empty()&&!p.vectors.count(s.positionParameter))||length(s.normal)<.5f||s.extent.x<=0||s.extent.y<=0||s.extent.z<=0||s.radius<=0)fail("invalid obstacle");c->shapes_.push_back(s);}
    auto b=j["balance"];if(!b.isNull()&&!b.isObject())fail("balance must be object");c->balanceStrength_=num(b["strength"],0);c->margin_=num(b["margin"],.02f);if(c->balanceStrength_<0||c->balanceStrength_>1||c->margin_<0||c->margin_>1)fail("invalid balance settings");
    if (b.has("masses") && (!b["masses"].isArray() || b["masses"].size() > 32))
      fail("invalid masses");
    for (const auto &v : b["masses"].items()) {
      Mass mass;
      mass.bone = bone(v, "bone");
      mass.mass = num(v["mass"], 1);
      mass.offset = vec(v["offset"]);
      if (mass.mass <= 0 || mass.mass > 10000)
        fail("mass must be positive");
      c->masses_.push_back(mass);
    }
    if (c->balanceStrength_ > 0 && (c->feet_.empty() || c->masses_.empty()))
      fail("balance requires feet and masses");
    return c;
}
std::vector<int> AnimationContacts::bones()const{std::vector<int> r;for(auto& f:feet_)r.push_back(f.bone);for(auto& c:capsules_)r.push_back(c.bone);for(auto& m:masses_)r.push_back(m.bone);return r;}
bool AnimationContacts::contact(const AnimationParameters& p,float3 center,float radius,IKHit& hit)const{
    bool found=false;hit={};for(size_t i=0;i<shapes_.size();++i){auto& s=shapes_[i];IKHit h;auto pos=param(p,s.positionParameter,s.point);bool ok=s.kind=="plane"?ikSpherePlane(center,radius,pos,s.normal,h):s.kind=="box"?ikSphereBox(center,radius,pos-s.extent,pos+s.extent,h):ikSphereSphere(center,radius,pos,s.radius,h);if(ok&&(!found||h.depth>hit.depth)){hit=h;hit.surface=i+1;found=true;}}
    if(world_&&p.environment&&p.environment->sphereContact){IKHit h;auto& e=*p.environment;float scale=scaleOf(e.modelToWorld);if(scale>1e-6f&&e.sphereContact(e.modelToWorld.transformPoint(center),radius*scale,h)&&finite(h.normal)&&length(h.normal)>.5f&&std::isfinite(h.depth)&&h.depth>0){auto correction=e.modelToWorld.inverse().transformDir(normalize(h.normal)*h.depth);h.depth=length(correction);h.normal=normalize(correction);h.surface|=uint64_t(1)<<63;h.point=e.modelToWorld.inverse().transformPoint(h.point);if(!found||h.depth>hit.depth){hit=h;found=true;}}}return found;
}
bool AnimationContacts::ground(const AnimationParameters& p,float3 start,float3 end,IKHit& hit,bool anySurface)const{
    bool found=false;float best=2;auto delta=end-start;
    for(size_t i=0;i<shapes_.size();++i){auto& s=shapes_[i];auto pos=param(p,s.positionParameter,s.point);float t=2;float3 normal{};
        if(s.kind=="plane"){float d=dot(delta,s.normal);if(std::abs(d)>1e-7f){t=dot(pos-start,s.normal)/d;normal=s.normal;}}
        else if(s.kind=="sphere"){auto r=start-pos;float a=dot(delta,delta),b=dot(r,delta),disc=b*b-a*(dot(r,r)-s.radius*s.radius);if(a>1e-9f&&disc>=0){t=(-b-std::sqrt(disc))/a;normal=normalize(start+delta*t-pos);}}
        else {float enter=0,exit=1;float o[]={start.x,start.y,start.z},d[]={delta.x,delta.y,delta.z};auto lo=pos-s.extent,hi=pos+s.extent;float l[]={lo.x,lo.y,lo.z},h[]={hi.x,hi.y,hi.z};bool ok=true;for(int k=0;k<3;++k){if(std::abs(d[k])<1e-8f){if(o[k]<l[k]||o[k]>h[k])ok=false;continue;}float a=(l[k]-o[k])/d[k],b=(h[k]-o[k])/d[k];float sign=-1;if(a>b){std::swap(a,b);sign=1;}if(a>enter){enter=a;normal={};if(k==0)normal.x=sign;if(k==1)normal.y=sign;if(k==2)normal.z=sign;}exit=std::min(exit,b);if(enter>exit)ok=false;}if(ok)t=enter;}
        if(t>=0&&t<=1&&t<best&&(anySurface||dot(normal,up)>=maxSlope_)){hit={start+delta*t,normal,0,i+1};best=t;found=true;}}
    if(world_&&p.environment&&p.environment->raycast){IKHit h;auto& e=*p.environment;if(e.raycast(worldPoint(p,start),worldPoint(p,end),h)&&finite(h.point)&&finite(h.normal)&&length(h.normal)>.5f){h.surface|=uint64_t(1)<<63;h.point=localPoint(p,h.point);h.normal=localNormal(p,h.normal);float t=dot(h.point-start,delta)/std::max(dot(delta,delta),1e-9f);if(t>=0&&t<=1&&t<best&&(anySurface||dot(h.normal,up)>=maxSlope_)){hit=h;found=true;}}}return found;
}
void AnimationContacts::plant(const Model&,const AnimationParameters& p,const Pose&,const std::vector<mat4>& world,AnimationContactState& state)const{
    state.active=true;state.feet.resize(feet_.size());for(size_t i=0;i<feet_.size();++i){const auto& f=feet_[i];auto& foot=state.feet[i];auto sole=world[size_t(f.bone)].transformPoint(f.offset);float plant=scalar(p,f.plant,1);IKHit hit;
        if(!std::isfinite(plant)||plant<=0||!ground(p,sole+up*f.probeUp,sole-up*f.probeDown,hit)){foot.planted=false;continue;}
        auto anchor=localPoint(p,foot.anchor);if(!foot.planted||foot.surface!=hit.surface||length(anchor-sole)>f.release){foot.anchor=worldPoint(p,hit.point);foot.surface=hit.surface;}else { // Follow support height while keeping tangential contact fixed.
            IKHit anchored;if(ground(p,anchor+up*f.probeUp,anchor-up*f.probeDown,anchored)&&anchored.surface==foot.surface){foot.anchor=worldPoint(p,anchored.point);hit.normal=anchored.normal;}else {foot.planted=false;continue;}}
        foot.normal=p.environment?normalize(p.environment->modelToWorld.transformDir(hit.normal)):hit.normal;foot.planted=true;
    }
}
void AnimationContacts::goals(const Model&,const AnimationParameters& p,const Pose& pose,const std::vector<mat4>& world,const AnimationContactState& state,std::vector<IKPositionGoal>& out,std::vector<IKRotationGoal>& rotations)const{
    for(size_t i=0;i<feet_.size();++i){if(i>=state.feet.size()||!state.feet[i].planted)continue;const auto& f=feet_[i];auto n=localNormal(p,state.feet[i].normal);auto anchor=localPoint(p,state.feet[i].anchor);out.push_back({f.bone,f.offset,anchor,f.weight});if(f.align>0){auto q=rotationOf(world[size_t(f.bone)]);rotations.push_back({f.bone,between(q.rotate(f.up),n)*q,f.weight*f.align});}}
    for(auto& c:capsules_){auto localEnd=c.end>=0?pose[size_t(c.end)].t:c.offset;float segment=length(world[size_t(c.bone)].transformDir(localEnd-c.offset));int count=c.end>=0?c.samples:0;float step=count?segment/float(count):0;float radius=std::sqrt(c.radius*c.radius+step*step*.25f); // Conservative coverage between sample centres.
        for(int i=0;i<=count;++i){auto offset=count?lerp(c.offset,localEnd,float(i)/count):c.offset;auto point=world[size_t(c.bone)].transformPoint(offset);IKHit h;if(contact(p,point,radius,h))out.push_back({c.bone,offset,point+h.normal*h.depth,collisionWeight});}}
}
float3 AnimationContacts::balance(const AnimationParameters& p,const std::vector<mat4>& world,const AnimationContactState& state,float* error)const{
  if (error)
    *error = 0;
  if (balanceStrength_ <= 0)
    return {};
  float mass = 0;
  float3 com{};
  for (auto &m : masses_) {
    com += world[size_t(m.bone)].transformPoint(m.offset) * m.mass;
    mass += m.mass;
  }
  com = com * (1 / std::max(mass, 1e-9f));
  float3 axis = normalize(cross(up, std::abs(up.z) < .9f ? float3{0, 0, 1} : float3{1, 0, 0})), other = cross(up, axis);
  auto project = [&](float3 v) { return V2{dot(v, axis), dot(v, other)}; };
  std::vector<V2> points;
  for (size_t i = 0; i < feet_.size(); ++i)
    if (i < state.feet.size() && state.feet[i].planted) {
      auto a = localPoint(p, state.feet[i].anchor);
      float r = std::max(.001f, feet_[i].radius - margin_);
      for (float x : {-r, r})
        for (float y : {-r, r})
          points.push_back(project(a + axis * x + other * y));
    }
  if (points.empty())
    return {};
  std::sort(points.begin(), points.end(), [](auto a, auto b) { return a.x == b.x ? a.y < b.y : a.x < b.x; });
  std::vector<V2> hull;
  for (auto v : points) {
    while (hull.size() > 1 && cross2(hull[hull.size() - 2], hull.back(), v) <= 0)
      hull.pop_back();
    hull.push_back(v);}size_t lower=hull.size();for(int i=int(points.size())-2;i>=0;--i){auto v=points[size_t(i)];while(hull.size()>lower&&cross2(hull[hull.size()-2],hull.back(),v)<=0)hull.pop_back();hull.push_back(v);}if(hull.size()>1)hull.pop_back();
    auto c=project(com);bool inside=true;float best=1e30f;V2 closest=c;for(size_t i=0;i<hull.size();++i){auto a=hull[i],b=hull[(i+1)%hull.size()];inside&=cross2(a,b,c)>=-1e-6f;float x=b.x-a.x,y=b.y-a.y,t=std::clamp(((c.x-a.x)*x+(c.y-a.y)*y)/std::max(x*x+y*y,1e-12f),0.f,1.f);V2 q{a.x+x*t,a.y+y*t};float d=(q.x-c.x)*(q.x-c.x)+(q.y-c.y)*(q.y-c.y);if(d<best){best=d;closest=q;}}
    if (inside)
      return {};
    auto delta = axis * (closest.x - c.x) + other * (closest.y - c.y);
    if (error)
      *error = length(delta);
    return delta * balanceStrength_;
}
AnimationContactReport AnimationContacts::report(const Model& m,const AnimationParameters& p,const Pose& pose,const std::vector<mat4>& world,const AnimationContactState& state)const{
    AnimationContactReport r;if(!state.active)return r;for(auto& f:state.feet)if(f.planted)++r.supports;r.supported=r.supports>0;balance(p,world,state,&r.balanceError);
    std::vector<IKPositionGoal> goals_;std::vector<IKRotationGoal> rotations;goals(m,p,pose,world,state,goals_,rotations);for(size_t i=0;i<size_t(r.supports);++i){auto& g=goals_[i];r.supportError=std::max(r.supportError,length(g.target-world[size_t(g.bone)].transformPoint(g.offset)));}for(size_t i=size_t(r.supports);i<goals_.size();++i){auto& g=goals_[i];r.penetration=std::max(r.penetration,length(g.target-world[size_t(g.bone)].transformPoint(g.offset)));++r.collisions;}r.stable=r.supported&&r.supportError<.005f&&r.balanceError<.005f&&r.penetration<.005f;return r;
}
}
