#pragma once
#include "engine/mechanics/Namespace.h"
// portals-rewind: linked portals and gameplay time rewind.
// Portals: rectangular one-sided openings linked in pairs. crossPortals tests a body's movement segment against every
// portal (ascending id) and, when it passes through the front of an open portal, maps position, velocity and facing
// into the linked portal's frame (exit moving out of the partner's front, speed kept or scaled), with a per-body
// cooldown against immediate re-entry. portalTransform* expose the same mapping for rendering recursive views
// (apply it `depth` times for portal-in-portal views; presentation code decides the depth).
// Rewind: a bounded history of opaque per-tick snapshots (the owner's mechanics::save bytes or a whole-world snapshot)
// with a rewind energy budget: rewinding N ticks spends N energy, recording refills it. rewindTo restores the newest
// snapshot at or before the target tick and drops the newer history, so a replay after rewind is deterministic.
// This is gameplay rewind (Braid/Prince of Persia style), separate from engine/actor lag-compensation history.
// Fixed math; record kinds 203 (PortalBodies) and 204 (RewindHistory).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
// center, normal (front, unit), up (unit, perpendicular to normal), half extents along right and up.
struct Portal {int64_t id=0,link=0;Vec3 center{},normal{0_fx,{},1_fx},up{{},1_fx,{}};Fixed halfWidth=1_fx,halfHeight=Fixed::fromRatio(3,2);bool open=true;Fixed speedScale=1_fx;};
using PortalSet=std::map<int64_t,Portal>;
inline Vec3 portalRight(const Portal& p){return fxCross(p.up,p.normal);}
inline bool valid(const Portal& p){
    if(p.id<=0||p.link<=0||p.link==p.id||!policyBounded(p.center)||p.halfWidth<=Fixed{}||p.halfHeight<=Fixed{}||p.speedScale<=Fixed{}||p.speedScale>10_fx)return false;
    auto unit=[](Vec3 v){auto l=v.lengthSq();return l>Fixed::fromRatio(99,100)&&l<Fixed::fromRatio(101,100);};
    return unit(p.normal)&&unit(p.up)&&fxAbs(p.normal.dot(p.up))<Fixed::fromRatio(1,100);
}
inline bool valid(const PortalSet& s){for(auto& [id,p]:s){if(id!=p.id||!valid(p))return false;auto it=s.find(p.link);if(it==s.end()||it->second.link!=id)return false;}return true;}
// Local coordinates in the portal frame (right, up, normal).
inline Vec3 toPortalLocal(const Portal& p,Vec3 v){return {v.dot(portalRight(p)),v.dot(p.up),v.dot(p.normal)};}
inline Vec3 fromPortalLocal(const Portal& p,Vec3 l){return portalRight(p)*l.x+p.up*l.y+p.normal*l.z;}
// Entering the front of `in` (local z decreasing through 0) exits the front of `out`: mirror right and depth.
inline Vec3 portalTransformDirection(const Portal& in,const Portal& out,Vec3 d){auto l=toPortalLocal(in,d);return fromPortalLocal(out,{-l.x,l.y,-l.z});}
inline Vec3 portalTransformPoint(const Portal& in,const Portal& out,Vec3 p){return out.center+portalTransformDirection(in,out,p-in.center);}

struct PortalBody {int64_t id=0;Vec3 position{},velocity{},facing{0_fx,{},1_fx};};
struct PortalCooldowns {std::map<int64_t,uint32_t> until;uint32_t lastTick=0;bool initialized=false;};
struct PortalCrossing {int64_t body=0,from=0,to=0;Vec3 position{},velocity{},facing{};};
// bodies: positions after this tick's move; previous: positions before it (same order and ids, ascending).
inline std::optional<std::vector<PortalCrossing>> crossPortals(const PortalSet& portals,PortalCooldowns& cd,uint32_t tick,std::span<const PortalBody> previous,
    std::span<const PortalBody> bodies,uint32_t cooldownTicks=2){
    if(!valid(portals)||!policyTickOk(cd.initialized,cd.lastTick,tick)||previous.size()!=bodies.size()||bodies.size()>kPolicyMaxObjects)return std::nullopt;
    for(std::size_t i=0;i<bodies.size();++i)if(bodies[i].id<=0||bodies[i].id!=previous[i].id||(i&&bodies[i-1].id>=bodies[i].id)||!policyBounded(bodies[i].position)
        ||!policyBounded(previous[i].position)||!policyBounded(bodies[i].velocity,kPolicyMaxSpeed))return std::nullopt;
    auto s=cd;s.initialized=true;s.lastTick=tick;std::vector<PortalCrossing> out;
    for(std::size_t i=0;i<bodies.size();++i){const auto& b=bodies[i];auto it=s.until.find(b.id);if(it!=s.until.end()&&tick<it->second)continue;
        for(auto& [pid,p]:portals){if(!p.open||!portals.at(p.link).open)continue;
            auto a=toPortalLocal(p,previous[i].position-p.center),z=toPortalLocal(p,b.position-p.center);
            if(!(a.z>Fixed{}&&z.z<=Fixed{}))continue;                               // must come from the front
            auto t=a.z/(a.z-z.z);auto hit=a+(z-a)*t;
            if(fxAbs(hit.x)>p.halfWidth||fxAbs(hit.y)>p.halfHeight)continue;
            const auto& q=portals.at(p.link);
            PortalCrossing c{b.id,pid,q.id,portalTransformPoint(p,q,b.position),portalTransformDirection(p,q,b.velocity)*p.speedScale,portalTransformDirection(p,q,b.facing)};
            // keep the body just in front of the exit
            auto lz=toPortalLocal(q,c.position-q.center).z;if(lz<Fixed::fromRatio(1,100))c.position+=q.normal*(Fixed::fromRatio(1,100)-lz);
            out.push_back(c);s.until[b.id]=tick+cooldownTicks;break;}
    }
    for(auto it=s.until.begin();it!=s.until.end();)it=it->second<=tick?s.until.erase(it):std::next(it);
    cd=std::move(s);return out;
}
inline void io(Archive& a,PortalCooldowns& s){a(s.until,s.lastTick,s.initialized);}
inline uint16_t stateKind(const PortalCooldowns&){return 203;}
inline bool valid(const PortalCooldowns& s){return policyPositiveKeys(s.until)&&s.until.size()<=kPolicyMaxObjects;}

// ---- Gameplay rewind.
struct RewindConfig {uint32_t capacityTicks=600,maxEnergy=300,refillPerTick=1;std::size_t maxBytes=64u<<20;};
struct RewindHistory {std::map<uint32_t,std::string> snapshots;uint32_t energy=0,lastTick=0;std::size_t bytes=0;bool initialized=false;};
inline bool valid(const RewindConfig& c){return c.capacityTicks>0&&c.capacityTicks<=1000000&&c.maxEnergy<=1000000&&c.refillPerTick<=c.maxEnergy+1&&c.maxBytes>0;}
// Record the snapshot of `tick` (strictly increasing). Old entries beyond capacity or the byte budget are dropped.
inline bool recordRewind(const RewindConfig& c,RewindHistory& h,uint32_t tick,std::string snapshot){
    if(!valid(c)||!policyTickOk(h.initialized,h.lastTick,tick)||snapshot.empty()||snapshot.size()>c.maxBytes)return false;
    if(!h.initialized)h.energy=c.maxEnergy;h.initialized=true;h.lastTick=tick;h.bytes+=snapshot.size();h.snapshots[tick]=std::move(snapshot);
    h.energy=std::min(c.maxEnergy,h.energy+c.refillPerTick);
    while(!h.snapshots.empty()&&(h.snapshots.begin()->first+c.capacityTicks<=tick||h.bytes>c.maxBytes)){h.bytes-=h.snapshots.begin()->second.size();h.snapshots.erase(h.snapshots.begin());}
    return true;
}
struct RewindResult {uint32_t tick=0;std::string snapshot;uint32_t spent=0;};
// Rewind to targetTick (or the newest snapshot before it). Fails without changes when energy is short or history is empty.
inline std::optional<RewindResult> rewindTo(RewindHistory& h,uint32_t targetTick){
    if(!h.initialized||h.snapshots.empty()||targetTick>h.lastTick)return std::nullopt;
    auto it=h.snapshots.upper_bound(targetTick);if(it==h.snapshots.begin())return std::nullopt;--it;
    uint32_t spent=h.lastTick-it->first;if(spent>h.energy)return std::nullopt;
    RewindResult r{it->first,it->second,spent};h.energy-=spent;
    for(auto j=std::next(it);j!=h.snapshots.end();){h.bytes-=j->second.size();j=h.snapshots.erase(j);}
    h.lastTick=it->first;return r;      // the restored tick stays recorded; recording continues with lastTick+1
}
inline void io(Archive& a,RewindHistory& h){uint64_t b=h.bytes;a(h.snapshots,h.energy,h.lastTick,b,h.initialized);h.bytes=std::size_t(b);}
inline uint16_t stateKind(const RewindHistory&){return 204;}
inline bool valid(const RewindHistory& h){std::size_t n=0;for(auto& [t,s]:h.snapshots){if(s.empty()||t>h.lastTick)return false;n+=s.size();}return n==h.bytes;}
}
