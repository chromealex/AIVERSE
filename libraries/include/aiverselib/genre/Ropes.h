#pragma once
// physical-interactions: ropes/chains as particle chains solved with XPBD distance constraints and a fixed
// iteration count (InteractionConfig::ropeIterations), Gauss-Seidel from end A to end B. Segments only resist
// stretching (slack ropes fold freely). Tension = max segment constraint force (lambda/dt^2) in newtons;
// above breakForce the rope breaks. Ends: Free particle, Point (fixed world point), Body (follows an owner
// body pose each tick; the owner applies the reported pull), Load (a hanging mass simulated by the rope).
// No rope-vs-world collision and no rope self-collision.
#include "InteractionConfig.h"
namespace aiverse::lib::genre {
enum class RopeAnchor:uint8_t {Free,Point,Body,Load};
struct RopeEnd {RopeAnchor anchor=RopeAnchor::Free;Vec3 point{};int64_t body=0;Fixed mass{};}; // point: Point anchor; mass: Load
struct RopeDef {int64_t id=0;Fixed length=4_fx;uint32_t segments=8;Fixed particleMass=Fixed::fromRatio(1,5),compliance{},breakForce{};RopeEnd a,b;}; // breakForce 0 = unbreakable
using RopeDefs=std::map<int64_t,RopeDef>;
inline bool valid(const RopeEnd& e){
    if(uint8_t(e.anchor)>uint8_t(RopeAnchor::Load)||!physBounded(e.point))return false;
    if(e.anchor==RopeAnchor::Body&&e.body<=0)return false;if(e.anchor==RopeAnchor::Load&&(e.mass<=Fixed{}||e.mass>kPhysMaxMass))return false;return true;
}
inline bool valid(const RopeDef& d){
    return d.id>0&&d.length>Fixed{}&&d.length<=kPhysMaxExtent&&d.segments>=1&&d.segments<=kPhysMaxSegments&&d.particleMass>=Fixed::fromRatio(1,1000)&&d.particleMass<=kPhysMaxMass
        &&d.compliance>=Fixed{}&&d.compliance<=1_fx&&d.breakForce>=Fixed{}&&d.breakForce<=kPhysMaxForce&&valid(d.a)&&valid(d.b);
}
inline bool valid(const RopeDefs& defs){if(defs.size()>kPhysMaxObjects)return false;for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;return true;}

struct RopeInstance {std::vector<Vec3> points,velocities;RopeEnd a,b;Fixed tension{};bool broken=false;};
struct RopeState {std::map<int64_t,RopeInstance> ropes;uint32_t lastTick=0;bool initialized=false;};
enum class RopeAction:uint8_t {Attach,Detach};
// end: 0 = A, 1 = B. Attach replaces the end anchor (Point/Body snap the end particle to the anchor, Load keeps it).
struct RopeCommand {int64_t rope=0;uint8_t end=0;RopeAction action=RopeAction::Attach;RopeEnd anchor{};};
struct RopeBodyPose {int64_t body=0;Vec3 position{};};
struct RopeInput {std::vector<RopeBodyPose> bodies;std::vector<RopeCommand> commands;};
// pull: force on the attached body at that end (tension along the first segment toward the rope).
struct RopeStatus {int64_t rope=0;Fixed tension{};Vec3 endA{},endB{},pullA{},pullB{};bool broken=false;};
struct RopeReport {std::vector<RopeStatus> ropes;std::vector<InteractionEvent> events;};

inline bool ropePinned(const RopeEnd& e){return e.anchor==RopeAnchor::Point||e.anchor==RopeAnchor::Body;}
// Points interpolated from a to b (the solver straightens or slackens them from the first tick).
inline std::optional<RopeInstance> makeRope(const RopeDef& d,Vec3 a,Vec3 b){
    if(!valid(d)||!physBounded(a)||!physBounded(b))return std::nullopt;RopeInstance r;r.a=d.a;r.b=d.b;
    if(d.a.anchor==RopeAnchor::Point)a=d.a.point;if(d.b.anchor==RopeAnchor::Point)b=d.b.point;
    for(uint32_t i=0;i<=d.segments;++i)r.points.push_back(a+(b-a)*Fixed::fromRatio(i,d.segments));r.velocities.assign(r.points.size(),Vec3{});return r;
}
inline bool valid(const RopeInstance& r,const RopeDef& d){
    if(r.points.size()!=d.segments+1||r.velocities.size()!=r.points.size()||!valid(r.a)||!valid(r.b)||r.tension<Fixed{})return false;
    for(std::size_t i=0;i<r.points.size();++i)if(!physBounded(r.points[i])||!physBounded(r.velocities[i],kPhysMaxSpeed))return false;return true;
}
inline bool valid(const RopeState& s,const RopeDefs& defs){
    if(s.ropes.size()>kPhysMaxObjects)return false;for(const auto& [id,r]:s.ropes){auto d=defs.find(id);if(d==defs.end()||!valid(r,d->second))return false;}return true;
}
inline Fixed ropeInverseMass(const RopeDef& d,const RopeEnd& e){
    switch(e.anchor){case RopeAnchor::Point:case RopeAnchor::Body:return {};case RopeAnchor::Load:return 1_fx/e.mass;default:return 1_fx/d.particleMass;}
}

// One tick: commands (ascending rope, end; input order kept per end), then each intact rope in ascending id.
// dt must be at least 1/1000 s. A Body end whose pose is missing keeps its last position. Broken ropes stop simulating (owner removes them).
inline std::optional<RopeReport> stepRopes(const InteractionConfig& c,const RopeDefs& defs,RopeState& state,uint32_t tick,Fixed dt,const RopeInput& input){
    using K=InteractionEventKind;
    if(!valid(c)||!physDtOk(dt)||dt<Fixed::fromRatio(1,1000)||!physTickOk(state.initialized,state.lastTick,tick)||!valid(defs)||!valid(state,defs))return std::nullopt;
    if(input.bodies.size()>kPhysMaxObjects||input.commands.size()>kPhysMaxCommands)return std::nullopt;
    std::map<int64_t,Vec3> bodies;for(const auto& p:input.bodies)if(p.body<=0||!physBounded(p.position)||!bodies.emplace(p.body,p.position).second)return std::nullopt;
    auto commands=input.commands;for(const auto& x:commands)if(!state.ropes.contains(x.rope)||x.end>1||uint8_t(x.action)>1||(x.action==RopeAction::Attach&&(!valid(x.anchor)||x.anchor.anchor==RopeAnchor::Free)))return std::nullopt;
    std::stable_sort(commands.begin(),commands.end(),[](const auto& a,const auto& b){return a.rope!=b.rope?a.rope<b.rope:a.end<b.end;});
    auto s=state;s.initialized=true;s.lastTick=tick;RopeReport report;auto& ev=report.events;
    for(const auto& x:commands){
        auto& r=s.ropes.at(x.rope);if(r.broken)continue;auto& end=x.end?r.b:r.a;auto& p=x.end?r.points.back():r.points.front();
        if(x.action==RopeAction::Detach){if(end.anchor!=RopeAnchor::Free){end=RopeEnd{};ev.push_back({K::RopeDetached,x.rope,x.end,0,0,p});}continue;}
        end=x.anchor;if(end.anchor==RopeAnchor::Point)p=end.point;if(end.anchor==RopeAnchor::Body){auto it=bodies.find(end.body);if(it!=bodies.end())p=it->second;}
        (x.end?r.velocities.back():r.velocities.front())={};ev.push_back({K::RopeAttached,x.rope,end.body,int64_t(end.anchor),0,p});
    }
    const Fixed invDt=1_fx/dt,invDt2=invDt*invDt,damping=1_fx-Fixed::fromRatio(c.ropeDampingMilli,1000);
    for(auto& [id,r]:s.ropes){
        const auto& d=defs.at(id);if(r.broken){report.ropes.push_back({id,{},r.points.front(),r.points.back(),{},{},true});continue;}
        const std::size_t n=r.points.size();std::vector<Fixed> w(n,1_fx/d.particleMass);w.front()=ropeInverseMass(d,r.a);w.back()=ropeInverseMass(d,r.b);
        auto pin=[&](const RopeEnd& e,Vec3& p){if(e.anchor==RopeAnchor::Point)p=e.point;else if(e.anchor==RopeAnchor::Body){auto it=bodies.find(e.body);if(it!=bodies.end())p=it->second;}};
        auto old=r.points;
        for(std::size_t i=0;i<n;++i){
            if(w[i].raw==0){r.velocities[i]={};continue;}
            r.velocities[i]=physClampSpeed((r.velocities[i]-Vec3{Fixed{},c.gravity*dt,Fixed{}})*damping);r.points[i]=physClampPosition(r.points[i]+r.velocities[i]*dt);
        }
        pin(r.a,r.points.front());pin(r.b,r.points.back());
        const Fixed rest=d.length/int64_t(d.segments),alpha=d.compliance*invDt2;std::vector<Fixed> lambda(n-1);
        for(uint32_t k=0;k<c.ropeIterations;++k)for(std::size_t i=0;i+1<n;++i){
            auto delta=r.points[i+1]-r.points[i];auto len=delta.length();auto err=len-rest;if(err<=Fixed{}||len.raw==0)continue;
            auto sum=w[i]+w[i+1]+alpha;if(sum.raw==0)continue;
            auto dl=(Fixed{}-err-alpha*lambda[i])/sum;lambda[i]+=dl;auto dir=delta/len;
            r.points[i]=r.points[i]-dir*(w[i]*dl);r.points[i+1]=r.points[i+1]+dir*(w[i+1]*dl);
        }
        Fixed tension{};for(auto l:lambda)tension=fxMax(tension,(Fixed{}-l)*invDt2);
        for(std::size_t i=0;i<n;++i)if(w[i].raw)r.velocities[i]=physClampSpeed((r.points[i]-old[i])*invDt);
        r.tension=tension;
        auto pull=[&](std::size_t at,std::size_t to){auto dlt=r.points[to]-r.points[at];return dlt==Vec3{}?Vec3{}:fxNormalize(dlt)*(Fixed{}-lambda[std::min(at,to)])*invDt2;};
        RopeStatus status{id,tension,r.points.front(),r.points.back(),pull(0,1),pull(n-1,n-2),false};
        if(d.breakForce.raw&&tension>d.breakForce){
            r.broken=true;status.broken=true;ev.push_back({K::RopeBroken,id,0,physMilli(tension),0,r.points[n/2]});
        }
        report.ropes.push_back(status);
    }
    state=std::move(s);return report;
}
}
