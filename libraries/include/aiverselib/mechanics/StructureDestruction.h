#pragma once
#include "engine/mechanics/Namespace.h"
// structure-destruction: destructible structures made of box pieces (walls, floors, pillars, bridges, voxel-free
// buildings from BuildingGen). Area damage with falloff, per-piece health, fracture of a large piece into a grid of
// smaller pieces on its first heavy hit, structural support from anchored pieces through face contacts (unsupported
// pieces collapse), and authoritative debris: each removed piece becomes a debris body integrated with gravity to a
// ground height and put to rest, so every client sees the same debris (replicate DebrisBody records or the state).
// Each step reports the removed collision boxes, the added fracture boxes and one dirty region (union of changed
// boxes) for the owner to update collision, navmesh and visibility. Fixed math; ids ascending; record kind 202.
#include "PolicyCommon.h"
#include "engine/world/SpatialWorld.h"
namespace aiverse::lib::mechanics {
struct StructurePiece {int64_t id=0;world::Bounds box{};int64_t health=100,armor=0;bool anchored=false;
    int32_t fractureX=1,fractureY=1,fractureZ=1;int64_t fractureThreshold=0;};   // fracture grid used once when one hit >= threshold (0 = never)
struct StructureConfig {Fixed gravity=Fixed::fromRatio(98,10),groundY{},contactTolerance=Fixed::fromRatio(1,20),debrisRestSpeed=Fixed::fromRatio(1,2),
    bounce=Fixed::fromRatio(1,5);uint32_t debrisLifetime=1800;std::size_t maxPieces=8192,maxDebris=2048;};
struct DebrisBody {int64_t id=0,piece=0;world::Bounds box{};Vec3 velocity{};uint32_t spawnTick=0;bool resting=false;};
struct StructureState {std::map<int64_t,StructurePiece> pieces;std::map<int64_t,DebrisBody> debris;int64_t nextId=1,nextDebris=1;uint32_t lastTick=0;bool initialized=false;};
struct StructureDamage {int64_t id=0;Vec3 center{};Fixed radius=1_fx;int64_t amount=0;Vec3 impulse{};};   // falloff: full at centre, 0 at radius
enum class StructureEventKind:uint8_t {Damaged,Fractured,Destroyed,Collapsed,DebrisSpawned,DebrisRested,DebrisExpired};
struct StructureEvent {StructureEventKind kind{};int64_t piece=0,value=0;};
struct StructureReport {std::vector<world::Bounds> removed,added;std::optional<world::Bounds> dirty;std::vector<StructureEvent> events;std::vector<DebrisBody> spawned;};

inline bool validBox(const world::Bounds& b){return policyBounded(b.min)&&policyBounded(b.max)&&b.min.x<b.max.x&&b.min.y<b.max.y&&b.min.z<b.max.z;}
inline bool valid(const StructurePiece& p){return p.id>0&&validBox(p.box)&&p.health>0&&p.health<=1000000000&&p.armor>=0&&p.fractureX>=1&&p.fractureY>=1&&p.fractureZ>=1
    &&p.fractureX*p.fractureY*p.fractureZ<=64&&p.fractureThreshold>=0;}
inline bool valid(const StructureConfig& c){return c.gravity>=Fixed{}&&c.contactTolerance>=Fixed{}&&c.debrisRestSpeed>Fixed{}&&c.bounce>=Fixed{}&&c.bounce<1_fx&&c.maxPieces>0&&c.maxDebris>0;}
inline bool valid(const StructureState& s){
    for(auto& [id,p]:s.pieces)if(id!=p.id||!valid(p)||id>=s.nextId)return false;
    for(auto& [id,d]:s.debris)if(id!=d.id||!validBox(d.box)||!policyBounded(d.velocity,kPolicyMaxSpeed)||id>=s.nextDebris)return false;
    return s.nextId>0&&s.nextDebris>0;
}
inline bool addPiece(StructureState& s,StructurePiece p){if(p.id==0)p.id=s.nextId;if(!valid(p)||s.pieces.contains(p.id))return false;s.nextId=std::max(s.nextId,p.id+1);s.pieces.emplace(p.id,p);return true;}
inline bool touching(const world::Bounds& a,const world::Bounds& b,Fixed tol){
    bool ox=a.min.x<=b.max.x+tol&&b.min.x<=a.max.x+tol,oy=a.min.y<=b.max.y+tol&&b.min.y<=a.max.y+tol,oz=a.min.z<=b.max.z+tol&&b.min.z<=a.max.z+tol;
    if(!(ox&&oy&&oz))return false;
    auto overlap=[](Fixed a0,Fixed a1,Fixed b0,Fixed b1){return fxMin(a1,b1)-fxMax(a0,b0);};
    int open=(overlap(a.min.x,a.max.x,b.min.x,b.max.x)>tol)+(overlap(a.min.y,a.max.y,b.min.y,b.max.y)>tol)+(overlap(a.min.z,a.max.z,b.min.z,b.max.z)>tol);
    return open>=2;                                                               // share a face area, not only an edge
}
inline Fixed boxDistance(const world::Bounds& b,Vec3 p){Vec3 q{fxClamp(p.x,b.min.x,b.max.x),fxClamp(p.y,b.min.y,b.max.y),fxClamp(p.z,b.min.z,b.max.z)};return (q-p).length();}
inline void grow(std::optional<world::Bounds>& d,const world::Bounds& b){if(!d){d=b;return;}
    d->min={fxMin(d->min.x,b.min.x),fxMin(d->min.y,b.min.y),fxMin(d->min.z,b.min.z)};d->max={fxMax(d->max.x,b.max.x),fxMax(d->max.y,b.max.y),fxMax(d->max.z,b.max.z)};}
// Pieces connected (through face contacts) to an anchored piece.
inline std::set<int64_t> supportedPieces(const StructureState& s,Fixed tol){
    std::set<int64_t> seen;std::vector<int64_t> stack;for(auto& [id,p]:s.pieces)if(p.anchored){seen.insert(id);stack.push_back(id);}
    while(!stack.empty()){auto id=stack.back();stack.pop_back();const auto& a=s.pieces.at(id).box;
        for(auto& [oid,o]:s.pieces)if(!seen.contains(oid)&&touching(a,o.box,tol)){seen.insert(oid);stack.push_back(oid);}}
    return seen;
}

inline std::optional<StructureReport> stepStructure(const StructureConfig& c,StructureState& state,uint32_t tick,Fixed dt,std::span<const StructureDamage> damage){
    if(!valid(c)||!policyDtOk(dt)||!policyTickOk(state.initialized,state.lastTick,tick)||!valid(state)||damage.size()>kPolicyMaxCommands)return std::nullopt;
    std::vector<StructureDamage> hits(damage.begin(),damage.end());std::sort(hits.begin(),hits.end(),[](auto& a,auto& b){return a.id<b.id;});
    for(std::size_t i=0;i<hits.size();++i)if(hits[i].id<=0||(i&&hits[i].id==hits[i-1].id)||hits[i].amount<0||hits[i].radius<=Fixed{}||!policyBounded(hits[i].center)||!policyBounded(hits[i].impulse,kPolicyMaxSpeed))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;StructureReport r;
    std::map<int64_t,Vec3> impulseOf;
    auto removePiece=[&](int64_t id,StructureEventKind why,Vec3 impulse){auto p=s.pieces.at(id);s.pieces.erase(id);r.removed.push_back(p.box);grow(r.dirty,p.box);
        r.events.push_back({why,id,0});
        if(s.debris.size()<c.maxDebris){DebrisBody d{s.nextDebris++,id,p.box,impulse,tick,false};s.debris.emplace(d.id,d);r.spawned.push_back(d);r.events.push_back({StructureEventKind::DebrisSpawned,id,d.id});}};
    for(const auto& h:hits){
        std::vector<int64_t> ids;for(auto& [id,p]:s.pieces)ids.push_back(id);
        for(auto id:ids){auto it=s.pieces.find(id);if(it==s.pieces.end())continue;auto& p=it->second;auto dist=boxDistance(p.box,h.center);if(dist>=h.radius)continue;
            int64_t applied=((h.amount*((h.radius-dist)/h.radius).raw)>>Fixed::kFracBits)-p.armor;if(applied<=0)continue;
            if(p.fractureThreshold>0&&applied>=p.fractureThreshold&&p.fractureX*p.fractureY*p.fractureZ>1&&s.pieces.size()+p.fractureX*p.fractureY*p.fractureZ<=c.maxPieces){
                auto old=p;s.pieces.erase(id);r.removed.push_back(old.box);grow(r.dirty,old.box);r.events.push_back({StructureEventKind::Fractured,id,old.fractureX*old.fractureY*old.fractureZ});
                auto size=old.box.max-old.box.min;int64_t share=std::max<int64_t>(1,old.health);
                for(int32_t z=0;z<old.fractureZ;++z)for(int32_t y=0;y<old.fractureY;++y)for(int32_t x=0;x<old.fractureX;++x){
                    StructurePiece q;q.id=s.nextId++;q.armor=old.armor;q.health=share;q.anchored=old.anchored&&y==0;
                    q.box.min=old.box.min+Vec3{size.x*Fixed::fromRatio(x,old.fractureX),size.y*Fixed::fromRatio(y,old.fractureY),size.z*Fixed::fromRatio(z,old.fractureZ)};
                    q.box.max=old.box.min+Vec3{size.x*Fixed::fromRatio(x+1,old.fractureX),size.y*Fixed::fromRatio(y+1,old.fractureY),size.z*Fixed::fromRatio(z+1,old.fractureZ)};
                    // the fragment closest to the hit takes the remaining damage
                    auto qd=boxDistance(q.box,h.center);if(qd<h.radius){int64_t qa=((h.amount*((h.radius-qd)/h.radius).raw)>>Fixed::kFracBits)-q.armor;if(qa>0)q.health-=qa;}
                    if(q.health<=0){r.events.push_back({StructureEventKind::Destroyed,q.id,0});grow(r.dirty,q.box);
                        if(s.debris.size()<c.maxDebris){DebrisBody d{s.nextDebris++,q.id,q.box,h.impulse,tick,false};s.debris.emplace(d.id,d);r.spawned.push_back(d);r.events.push_back({StructureEventKind::DebrisSpawned,q.id,d.id});}
                        continue;}
                    s.pieces.emplace(q.id,q);r.added.push_back(q.box);}
                continue;
            }
            p.health-=applied;r.events.push_back({StructureEventKind::Damaged,id,applied});
            if(p.health<=0)removePiece(id,StructureEventKind::Destroyed,h.impulse);
        }
    }
    if(!r.removed.empty()){auto keep=supportedPieces(s,c.contactTolerance);std::vector<int64_t> fall;for(auto& [id,p]:s.pieces)if(!keep.contains(id))fall.push_back(id);
        for(auto id:fall)removePiece(id,StructureEventKind::Collapsed,{});}
    // Debris: gravity, ground contact with bounce, rest and lifetime.
    std::vector<int64_t> expired;
    for(auto& [id,d]:s.debris){
        if(tick-d.spawnTick>=c.debrisLifetime){expired.push_back(id);continue;}if(d.resting)continue;
        d.velocity.y-=c.gravity*dt;auto delta=d.velocity*dt;d.box.min+=delta;d.box.max+=delta;
        if(d.box.min.y<c.groundY){auto lift=c.groundY-d.box.min.y;d.box.min.y+=lift;d.box.max.y+=lift;d.velocity.y=-d.velocity.y*c.bounce;d.velocity.x=d.velocity.x*Fixed::fromRatio(4,5);d.velocity.z=d.velocity.z*Fixed::fromRatio(4,5);
            if(d.velocity.length()<c.debrisRestSpeed){d.velocity={};d.resting=true;r.events.push_back({StructureEventKind::DebrisRested,d.piece,id});}}
        if(!policyBounded(d.box.min)||!policyBounded(d.box.max))expired.push_back(id);
    }
    for(auto id:expired){r.events.push_back({StructureEventKind::DebrisExpired,s.debris.at(id).piece,id});s.debris.erase(id);}
    state=std::move(s);return r;
}
inline void io(Archive& a,StructurePiece& p){a(p.id,p.box.min,p.box.max,p.health,p.armor,p.anchored,p.fractureX,p.fractureY,p.fractureZ,p.fractureThreshold);}
inline void io(Archive& a,DebrisBody& d){a(d.id,d.piece,d.box.min,d.box.max,d.velocity,d.spawnTick,d.resting);}
inline void io(Archive& a,StructureState& s){a(s.pieces,s.debris,s.nextId,s.nextDebris,s.lastTick,s.initialized);}
inline uint16_t stateKind(const StructureState&){return 202;}
}
