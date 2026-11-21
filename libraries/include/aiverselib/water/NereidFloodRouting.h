// NEREID Stage 63: bounded, deterministic, volume-conserving flood routing.
// One equal-area heightfield cell stores a centimetre water column (a
// volume-equivalent for that cell). This is an OPTIONAL extension to NEREID,
// NOT a separate water-body solver or a replacement for WaterBodyComponent.
// The native host seeds initial columns from actual NEREID water footprints,
// maintains ground heights from authoritative TerraWeave, and owns tick order.
#pragma once
#include <aiverselib/water/NereidFlowObstacles.h>
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

struct WaterFloodCell {
    int32_t x=0,z=0;                  // grid cells, signed
    int32_t groundCm=0;
    uint32_t volumeCm=0;              // 0..100000, equal-area column
    Entity waterBody=kNullEntity;     // originating real NEREID water body
    int32_t flowXcmPerSec=0,flowZcmPerSec=0;
    // Persisted sub-centimetre flux progress through a partially permeable
    // obstruction on this cell's +X / +Z edge, in milli-cm (-999..999).
    // Positive = towards the +X/+Z neighbour, negative = towards this cell.
    // Without it, integer quantization made any permeability<1000 watertight
    // for small heads. Schema "nereid.flood" v2; v1 saves load with 0.
    int16_t carryXMilliCm=0,carryZMilliCm=0;
    constexpr bool operator==(const WaterFloodCell&) const = default;
};
enum class WaterFloodPortKind : uint8_t {
    Inflow=0, Outflow=1
};
struct WaterFloodPort {
    int32_t x=0,z=0;
    WaterFloodPortKind kind=WaterFloodPortKind::Inflow;
    uint32_t maxCmPerTick=0; // 1..5000; equal-area column volume
    Entity waterBody=kNullEntity; // required for inflow, ignored by outflow
    constexpr bool operator==(const WaterFloodPort&) const = default;
};
struct WaterFloodResource {
    uint32_t version=1;
    int32_t gridCm=100;               // equal square cell size
    int64_t originXcm=0,originZcm=0;  // grid origin, includes negative world
    uint64_t revision=0;
    uint64_t wetnessRevision=0;       // new wet/dry cells: auto-bank topology trigger
    uint64_t groundRevision=0;        // TerraWeave soil revision sampled by this grid
    uint32_t lastTick=UINT32_MAX;
    uint64_t externalVolumeCm=0;      // closed, explicitly supplied reservoir
    std::vector<WaterFloodCell> cells; // sorted strictly by x,z
    std::vector<WaterFloodPort> ports; // <=64, stable and explicit
};
struct WaterFloodSettings {
    uint32_t maxFluxCmPerEdge=30;   // <=500 per tick
    uint32_t fluxPermille=1000;     // <=1000, multiplies head gradient
    uint32_t minHeadDifferenceCm=2; // 2..1000, prevents 1cm oscillation
};
struct WaterFloodEdge {
    int32_t fromX=0,fromZ=0,toX=0,toZ=0;
    uint32_t volumeCm=0;
    uint32_t permeabilityPermille=1000;
    bool overtopped=false;         // destination was dry at start of tick
};
struct WaterFloodTick {
    uint64_t totalBeforeCm=0,totalAfterCm=0,movedCm=0;
    uint64_t suppliedCm=0,drainedCm=0;
    uint32_t newlyWetCells=0,newlyDryCells=0;
    std::vector<WaterFloodEdge> edges;  // <=32768 bounded pair evaluations
};

// Resolve permeability for a real NEREID obstruction at the centre of the
// shared edge. The authority samples the same WaterFlowObstructionComponent
// ECS data that locally deflects the ordinary NEREID water current.
using WaterFloodPermeability=std::function<uint32_t(
    Entity waterBody,int64_t edgeXcm,int64_t edgeZcm,
    int32_t waterSurfaceCm)>;

// Index of the cell (x,z) in the strictly sorted cell list, or cells.size().
inline size_t findNereidFloodCell(const WaterFloodResource& r,
                                  int32_t x,int32_t z){
    const auto key=std::pair{x,z};
    const auto it=std::lower_bound(r.cells.begin(),r.cells.end(),key,
        [](const WaterFloodCell& a,const std::pair<int32_t,int32_t>& b){
            return std::pair{a.x,a.z}<b;
        });
    if(it==r.cells.end() || it->x!=x || it->z!=z)return r.cells.size();
    return size_t(it-r.cells.begin());
}

inline void validateNereidFlood(const WaterFloodResource& r){
    if(r.version!=1 || r.gridCm<10 || r.gridCm>10000 ||
       r.originXcm<-100000000 || r.originXcm>100000000 ||
       r.originZcm<-100000000 || r.originZcm>100000000 ||
       r.cells.size()>16384u ||
       r.ports.size()>64u ||
       r.externalVolumeCm>1000000000ull)
        throw std::invalid_argument("NEREID flood configuration/budget");
    std::pair<int32_t,int32_t> last{};
    bool started=false;
    for(const auto& c:r.cells){
        const auto key=std::pair{c.x,c.z};
        if((started && !(last<key)) ||
           c.groundCm<-10000000 || c.groundCm>10000000 ||
           c.volumeCm>100000u ||
           (c.volumeCm && !c.waterBody.valid()) ||
           (!c.volumeCm && c.waterBody.valid()) ||
           c.flowXcmPerSec<-100000 || c.flowXcmPerSec>100000 ||
           c.flowZcmPerSec<-100000 || c.flowZcmPerSec>100000 ||
           c.carryXMilliCm<-999 || c.carryXMilliCm>999 ||
           c.carryZMilliCm<-999 || c.carryZMilliCm>999 ||
           int64_t(c.x)*r.gridCm+r.originXcm<-100000000 ||
           int64_t(c.x+int64_t(1))*r.gridCm+r.originXcm>100000000 ||
           int64_t(c.z)*r.gridCm+r.originZcm<-100000000 ||
           int64_t(c.z+int64_t(1))*r.gridCm+r.originZcm>100000000)
            throw std::invalid_argument("NEREID invalid/unsorted flood cell");
        started=true;last=key;
    }
    std::set<std::tuple<int32_t,int32_t,uint8_t>> uniquePorts;
    for(const auto& p:r.ports){
        if(p.kind!=WaterFloodPortKind::Inflow &&
           p.kind!=WaterFloodPortKind::Outflow)
            throw std::invalid_argument("NEREID unknown flood port kind");
        if(!p.maxCmPerTick || p.maxCmPerTick>5000 ||
           (p.kind==WaterFloodPortKind::Inflow &&
            !p.waterBody.valid()) ||
           !uniquePorts.emplace(p.x,p.z,uint8_t(p.kind)).second)
            throw std::invalid_argument("NEREID invalid/duplicate flood boundary port");
        // Rejected at load/publication instead of throwing on every tick.
        if(findNereidFloodCell(r,p.x,p.z)==r.cells.size())
            throw std::invalid_argument("NEREID flood boundary port outside grid");
    }
}
// Schema v1 (formatVersion 1) has no flux carries; v2+ (and unversioned
// hashing, formatVersion UINT32_MAX) persists them.
inline bool nereidFloodHasCarries(const Archive& a){
    return a.formatVersion>=2u;
}
inline void io(Archive& a,WaterFloodCell& c){
    a(c.x,c.z,c.groundCm,c.volumeCm,c.waterBody,
      c.flowXcmPerSec,c.flowZcmPerSec);
    if(nereidFloodHasCarries(a))a(c.carryXMilliCm,c.carryZMilliCm);
}
inline void io(Archive& a,WaterFloodPort& p){
    a(p.x,p.z,p.kind,p.maxCmPerTick,p.waterBody);
}
inline void io(Archive& a,WaterFloodResource& r){
    a(r.version,r.gridCm,r.originXcm,r.originZcm,
      r.revision,r.wetnessRevision,r.groundRevision,r.lastTick,
      r.externalVolumeCm);
    // Each cell is exactly 32 persisted bytes (6 int32 fields + Entity),
    // plus two int16 flux carries from schema v2.
    uint32_t n=a.count(r.cells.size(),nereidFloodHasCarries(a)?36:32);
    if(!a.ok() || n>16384u){a.fail();return;}
    if(!a.writing())r.cells.assign(n,{});
    for(auto& c:r.cells)if(a.ok())a(c);
    uint32_t portCount=a.count(r.ports.size(),21);
    if(!a.ok() || portCount>64u){a.fail();return;}
    if(!a.writing())r.ports.assign(portCount,{});
    for(auto& p:r.ports)if(a.ok())a(p);
    if(!a.writing() && a.ok()){
        try{validateNereidFlood(r);}
        catch(const std::exception&){a.fail();}
    }
}
inline void registerNereidFloodPersistence(Simulation& sim){
    // v2 adds per-edge flux carries (WaterFloodCell::carry*MilliCm).
    sim.registerPersistentResource<WaterFloodResource>("nereid.flood",2);
    // The v1 chunk is decoded with formatVersion=1, i.e. without carries.
    sim.registerPersistentResourceMigration<WaterFloodResource>(
        "nereid.flood",1,[](Archive& a,WaterFloodResource& r){io(a,r);});
}

inline WaterFloodTick stepNereidFlood(
        WaterFloodResource& r,uint32_t tick,uint32_t dtMs,
        WaterFloodSettings settings={},
        WaterFloodPermeability obstruction={}) {
    validateNereidFlood(r);
    if(dtMs==0 || dtMs>1000 ||
       settings.maxFluxCmPerEdge==0 ||
       settings.maxFluxCmPerEdge>500u ||
       settings.fluxPermille>1000u ||
       settings.minHeadDifferenceCm<2 ||
       settings.minHeadDifferenceCm>1000 ||
       tick==UINT32_MAX ||
       (r.lastTick!=UINT32_MAX && tick<=r.lastTick))
        throw std::invalid_argument("NEREID invalid/stale flood tick");

    WaterFloodTick result;
    result.totalBeforeCm=r.externalVolumeCm;
    const size_t n=r.cells.size();
    for(size_t i=0;i<n;++i)result.totalBeforeCm+=r.cells[i].volumeCm;
    // All transfers are proposed from the OLD snapshot, then committed once.
    // Each undirected edge is processed once (+X or +Z only), so opposite
    // proposals cannot double-spend or make source iteration order relevant.
    std::vector<uint32_t> incoming(n,0),outgoing(n,0);
    std::vector<int64_t> netX(n,0),netZ(n,0);
    std::vector<Entity> newBody(n,kNullEntity);
    // Edge carries are rewritten every tick; an edge that does not flow (or
    // flows the other way) restarts from zero, so the carry stays < 1 cm.
    std::vector<int16_t> newCarryX(n,0),newCarryZ(n,0);
    const auto tryEdge=[&](size_t i,int32_t nx,int32_t nz) {
        // Cells are sorted by (x,z): the +Z neighbour is usually next.
        size_t j=n;
        if(i+1<n && r.cells[i+1].x==nx && r.cells[i+1].z==nz)j=i+1;
        else j=findNereidFloodCell(r,nx,nz);
        if(j==n)return;
        const bool alongX=nx!=r.cells[i].x;
        int16_t& carryOut=alongX?newCarryX[i]:newCarryZ[i];
        const int16_t carryIn=alongX?r.cells[i].carryXMilliCm:
                                     r.cells[i].carryZMilliCm;
        const auto& a=r.cells[i],&b=r.cells[j];
        const int64_t ha=int64_t(a.groundCm)+a.volumeCm;
        const int64_t hb=int64_t(b.groundCm)+b.volumeCm;
        if(ha==hb)return;
        const size_t source=ha>hb?i:j,destination=ha>hb?j:i;
        const auto& from=r.cells[source],&to=r.cells[destination];
        if(!from.volumeCm || !from.waterBody.valid() ||
           (to.volumeCm && to.waterBody!=from.waterBody))
            return;
        const int64_t difference=std::abs(ha-hb);
        if(difference<int64_t(settings.minHeadDifferenceCm))return;
        // Moving more than half the head difference in one step would
        // reverse the pressure gradient and produce artificial oscillations.
        const uint64_t gradient=uint64_t(difference/2);
        uint64_t potential=gradient*settings.fluxPermille*dtMs/1000000ull;
        if(potential==0 && settings.fluxPermille)
            potential=1; // quantized minimum only when routing enabled
        const int64_t ox=r.originXcm+int64_t(a.x)*r.gridCm;
        const int64_t oz=r.originZcm+int64_t(a.z)*r.gridCm;
        const int64_t centerX=ox+(nx-a.x==1 ? r.gridCm : r.gridCm/2);
        const int64_t centerZ=oz+(nz-a.z==1 ? r.gridCm : r.gridCm/2);
        const uint32_t permeability=obstruction?
            obstruction(from.waterBody,centerX,centerZ,
                        int32_t(std::min<int64_t>(
                            int64_t(from.groundCm)+from.volumeCm,
                            std::numeric_limits<int32_t>::max()))):
            1000u;
        if(permeability>1000u)
            throw std::invalid_argument("NEREID flood obstruction permeability");
        // Partial permeability scales the quantized flux exactly: the
        // sub-cm remainder is carried per edge (same direction only), so a
        // 25% permeable log passes 1 cm every 4 ticks instead of nothing.
        // permeability 1000 leaves potential unchanged (carry stays 0).
        const int16_t direction=source==i?1:-1;
        const uint64_t prior=(carryIn>0)==(direction>0) && carryIn!=0 ?
            uint64_t(carryIn<0?-int32_t(carryIn):int32_t(carryIn)):0u;
        const uint64_t scaled=potential*permeability+prior;
        potential=scaled/1000u;
        const int16_t remainder=int16_t(int32_t(scaled%1000u)*direction);
        if(!potential){carryOut=remainder;return;}
        const uint32_t sourceRemaining=from.volumeCm-outgoing[source];
        const uint32_t targetSpace=100000u-to.volumeCm-incoming[destination];
        const uint32_t moved=uint32_t(std::min<uint64_t>({
            potential,uint64_t(settings.maxFluxCmPerEdge),
            uint64_t(sourceRemaining),uint64_t(targetSpace),gradient}));
        if(!moved)return;
        // A capped transfer drops its fractional progress (bounded budget).
        if(moved==potential)carryOut=remainder;
        outgoing[source]+=moved;incoming[destination]+=moved;
        if(!to.volumeCm){
            if(!newBody[destination].valid())
                newBody[destination]=from.waterBody;
            else if(newBody[destination]!=from.waterBody){
                // Only the first compatible water source may wet a dry cell
                // in one tick. Real confluences remain NEREID-owned.
                outgoing[source]-=moved;incoming[destination]-=moved;
                carryOut=0;
                return;
            }
        }
        const int32_t dx=to.x-from.x,dz=to.z-from.z;
        netX[source]+=int64_t(dx)*moved;
        netZ[source]+=int64_t(dz)*moved;
        netX[destination]+=int64_t(dx)*moved;
        netZ[destination]+=int64_t(dz)*moved;
        result.movedCm+=moved;
        result.edges.push_back({
            from.x,from.z,to.x,to.z,moved,permeability,!to.volumeCm});
    };
    for(size_t i=0;i<n;++i){
        const auto& c=r.cells[i];
        if(c.x<std::numeric_limits<int32_t>::max())
            tryEdge(i,c.x+1,c.z);
        if(c.z<std::numeric_limits<int32_t>::max())
            tryEdge(i,c.x,c.z+1);
    }
    WaterFloodResource candidate=r;
    for(size_t i=0;i<n;++i){
        auto& c=candidate.cells[i];
        c.volumeCm=c.volumeCm-outgoing[i]+incoming[i];
        if(!r.cells[i].volumeCm && c.volumeCm)
            c.waterBody=newBody[i];
        if(!c.volumeCm)c.waterBody=kNullEntity;
        c.carryXMilliCm=newCarryX[i];
        c.carryZMilliCm=newCarryZ[i];
        // Derived local surface flow is deterministic signed cm/s:
        // velocity = moved column-cm * cell width / (column depth * dt).
        // Without *gridCm the value had units cells/s. The multiply
        // saturates before the +/-1000m/s clamp (NEREID's current clamp).
        const int64_t divisor=std::max<uint64_t>(1,
            uint64_t(c.volumeCm)*uint64_t(dtMs));
        const int64_t scale=int64_t(1000)*r.gridCm;
        const auto velocity=[&](int64_t net){
            const int64_t limit=std::numeric_limits<int64_t>::max()/scale;
            const int64_t numerator=net>limit ?
                std::numeric_limits<int64_t>::max() :
                net< -limit ? -std::numeric_limits<int64_t>::max() :
                net*scale;
            return int32_t(std::clamp<int64_t>(
                numerator/divisor,-100000,100000));
        };
        c.flowXcmPerSec=velocity(netX[i]);
        c.flowZcmPerSec=velocity(netZ[i]);
    }
    // Explicit boundary ports feed/discharge this finite grid from/to the
    // SAME counted external store. They can create a hydraulic head rise
    // before a blocked outlet, but never conjure infinite water from a
    // static NEREID WaterBodyComponent footprint.
    for(const auto& port:r.ports){
        const size_t found=findNereidFloodCell(r,port.x,port.z);
        if(found==n) // unreachable: validateNereidFlood rejects it
            throw std::invalid_argument("NEREID flood boundary port outside grid");
        auto& c=candidate.cells[found];
        uint32_t amount=0;
        if(port.kind==WaterFloodPortKind::Inflow) {
            if(c.volumeCm && c.waterBody!=port.waterBody)continue;
            amount=uint32_t(std::min<uint64_t>({
                uint64_t(port.maxCmPerTick),
                candidate.externalVolumeCm,
                uint64_t(100000u-c.volumeCm)}));
            if(amount){
                if(!c.volumeCm)c.waterBody=port.waterBody;
                c.volumeCm+=amount;
                candidate.externalVolumeCm-=amount;
                result.suppliedCm+=amount;
            }
        }else{
            amount=std::min(port.maxCmPerTick,c.volumeCm);
            if(amount){
                if(candidate.externalVolumeCm>
                   1000000000ull-uint64_t(amount))
                    throw std::overflow_error("NEREID external flood reservoir full");
                c.volumeCm-=amount;
                candidate.externalVolumeCm+=amount;
                if(!c.volumeCm){
                    c.waterBody=kNullEntity;
                }
                result.drainedCm+=amount;
            }
        }
    }
    result.totalAfterCm=candidate.externalVolumeCm;
    for(size_t i=0;i<n;++i){
        const auto& c=candidate.cells[i];
        result.totalAfterCm+=c.volumeCm;
        result.newlyWetCells+=!r.cells[i].volumeCm && c.volumeCm;
        result.newlyDryCells+=r.cells[i].volumeCm && !c.volumeCm;
    }
    if(result.totalAfterCm!=result.totalBeforeCm)
        throw std::logic_error("NEREID flood routing did not conserve water");
    candidate.lastTick=tick;
    if(result.newlyWetCells || result.newlyDryCells){
        if(candidate.wetnessRevision==UINT64_MAX)
            throw std::overflow_error("NEREID flood wetness revision overflow");
        ++candidate.wetnessRevision;
    }
    if(result.movedCm || result.suppliedCm || result.drainedCm){
        if(candidate.revision==UINT64_MAX)
            throw std::overflow_error("NEREID flood revision overflow");
        ++candidate.revision;
    }
    r=std::move(candidate);
    return result;
}

// On a dry riverbank outside the authored footprint, a positive flood
// column creates a WaterSample that still carries the original NEREID
// body identity/kind and its own dynamically accumulated surface height.
// When the sampled authored water surface is already higher, it remains
// authoritative; flood overlays cannot subtract/hide original water.
inline WaterSample applyNereidFloodSample(
        const World& world,const WaterFloodResource* flood,
        Vec3 point,WaterSample base) {
    if(!flood || flood->cells.empty())return base;
    // validateNereidFlood is required at load/structural publication, NOT
    // for every water probe: the resource is immutable during the ECS read.
    if(point.x.raw>std::numeric_limits<int64_t>::max()/100 ||
       point.x.raw<std::numeric_limits<int64_t>::min()/100 ||
       point.z.raw>std::numeric_limits<int64_t>::max()/100 ||
       point.z.raw<std::numeric_limits<int64_t>::min()/100)
        return base;
    const int64_t px=point.x.raw*100/Fixed::kOne;
    const int64_t pz=point.z.raw*100/Fixed::kOne;
    const int64_t gx=px-flood->originXcm,gz=pz-flood->originZcm;
    const auto floorDivide=[](int64_t n,int64_t d){
        int64_t q=n/d;
        if(n%d<0)--q;
        return q;
    };
    const int64_t ix=floorDivide(gx,flood->gridCm);
    const int64_t iz=floorDivide(gz,flood->gridCm);
    if(ix<INT32_MIN || ix>INT32_MAX || iz<INT32_MIN || iz>INT32_MAX)
        return base;
    const auto it=std::lower_bound(flood->cells.begin(),
        flood->cells.end(),std::pair{int32_t(ix),int32_t(iz)},
        [](const WaterFloodCell& a,
           const std::pair<int32_t,int32_t>& b){
            return std::pair{a.x,a.z}<b;
        });
    if(it==flood->cells.end() || it->x!=ix || it->z!=iz ||
       !it->volumeCm || !it->waterBody.valid() ||
       !world.alive(it->waterBody))
        return base;
    const auto* body=world.tryGetConst<WaterBodyComponent>(
        it->waterBody);
    if(!body || !validWaterBody(*body))return base;
    const int64_t surface=int64_t(it->groundCm)+it->volumeCm;
    const Fixed waterTop=Fixed::fromRatio(surface,100);
    if(base.found && base.surfaceY>=waterTop)return base;
    base.body=it->waterBody;
    base.kind=body->kind;
    base.surfaceY=waterTop;
    base.bottomY=Fixed::fromRatio(it->groundCm,100);
    base.priority=body->priority;
    base.flow={Fixed::fromRatio(it->flowXcmPerSec,100),
               Fixed{},
               Fixed::fromRatio(it->flowZcmPerSec,100)};
    base.found=true;
    base.submerged=point.y<waterTop && point.y>=base.bottomY;
    base.depth=fxClamp(waterTop-point.y,Fixed{},
                       waterTop-base.bottomY);
    return base;
}

// Enabled obstructions bucketed once per flood tick by water body (sorted by
// generation-safe entity id), instead of iterating every ECS obstruction for
// each of up to 32768 edge probes. permeability() is the minimum over the
// matching obstructions, so it is independent of iteration order and equal
// to the former per-call ECS scan.
class NereidFloodObstructionIndex {
public:
    explicit NereidFloodObstructionIndex(const World& read){
        entries_.reserve(read.countExisting<WaterFlowObstructionComponent>());
        read.eachExistingConst<WaterFlowObstructionComponent>(
            [&](Entity id,const WaterFlowObstructionComponent& obs){
                if(!read.alive(id) || !obs.enabled)return;
                entries_.push_back({obs.waterBody.packed(),obs.center,
                    obs.radius,obs.bottomY,obs.topY,
                    obs.permeabilityPermille});
            });
        std::stable_sort(entries_.begin(),entries_.end(),
            [](const Entry& a,const Entry& b){return a.body<b.body;});
    }
    uint32_t permeability(Entity body,int64_t x,int64_t z,
                          int32_t surface)const{
        uint32_t strength=1000u;
        const uint64_t key=body.packed();
        auto it=std::lower_bound(entries_.begin(),entries_.end(),key,
            [](const Entry& e,uint64_t k){return e.body<k;});
        if(it==entries_.end() || it->body!=key)return strength;
        const Fixed fx=Fixed::fromRatio(x,100);
        const Fixed fz=Fixed::fromRatio(z,100);
        const Fixed top=Fixed::fromRatio(surface,100);
        for(;it!=entries_.end() && it->body==key;++it){
            if(it->topY<=top-Fixed::fromInt(1) ||
               it->bottomY>=top ||
               fxAbs(fx-it->center.x)>=it->radius ||
               fxAbs(fz-it->center.y)>=it->radius)continue;
            strength=std::min(strength,it->permeabilityPermille);
        }
        return strength;
    }
    bool empty()const{return entries_.empty();}
private:
    struct Entry {
        uint64_t body=0;
        Vec2 center{};
        Fixed radius{},bottomY{},topY{};
        uint32_t permeabilityPermille=1000;
    };
    std::vector<Entry> entries_;
};

inline void addNereidFloodRoutingSystem(
        Simulation& sim,std::string name,
        uint32_t dtMs=16,WaterFloodSettings config={}){
    sim.addSystem(std::move(name),
        [dtMs,config](Simulation& current){
            World& world=current.world();
            const auto* stored=world.resourceConst<WaterFloodResource>();
            if(!stored || stored->cells.empty())return;
            WaterFloodResource next=*stored;
            // An inflow whose NEREID water body died or was disabled is
            // skipped for this tick (deterministically, in port order)
            // instead of throwing every tick; the configured ports persist.
            std::vector<WaterFloodPort> configured;
            const auto liveInflow=[&](const WaterFloodPort& port){
                if(port.kind!=WaterFloodPortKind::Inflow)return true;
                const auto* body=world.tryGetConst<WaterBodyComponent>(
                    port.waterBody);
                return world.alive(port.waterBody) && body &&
                       validWaterBody(*body);
            };
            if(!std::all_of(stored->ports.begin(),stored->ports.end(),
                            liveInflow)){
                configured=next.ports;
                next.ports.erase(std::remove_if(next.ports.begin(),
                    next.ports.end(),[&](const WaterFloodPort& port){
                        return !liveInflow(port);
                    }),next.ports.end());
            }
            // Obstruction lookup is host-independent, based only on the
            // immutable ECS record for the same water tick.
            const NereidFloodObstructionIndex obstructions(world);
            WaterFloodPermeability resistance;
            if(!obstructions.empty())
                resistance=[&obstructions](Entity body,int64_t x,int64_t z,
                                           int32_t surface){
                    return obstructions.permeability(body,x,z,surface);
                };
            (void)stepNereidFlood(next,current.tick(),dtMs,
                                  config,resistance);
            if(!configured.empty())next.ports=std::move(configured);
            world.setResource<WaterFloodResource>(std::move(next));
        });
}

} // namespace aiverse::lib::nereid
