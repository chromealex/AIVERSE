#pragma once
// CPU-only bridge from functional buildings to the existing deterministic
// structure-destruction library. Coordinates are building-local. The world
// owner must transform and atomically publish the final collider delta before
// committing the candidate. No GPU, ECS, physics or navigation side effects.
#include "engine/systems/BuildingStructureGraph.h"
#include "engine/core/Archive.h"
#include "engine/core/Hash.h"
#include <aiverselib/mechanics/StructureDestruction.h>
#include <optional>
#include <set>
#include <span>

namespace aiverse::building {
using lib::systems::BuildingGraphPart;
using lib::systems::BuildingGraphSolid;
using lib::systems::BuildingStructureGraph;
enum class BuildingSupportMode : uint8_t { DamageOnly, LoadBearing };
namespace buildingDestructionDetail {
namespace mechanics=lib::mechanics;
inline constexpr size_t maxPieces=4096,maxDebris=1024;
inline mechanics::StructureConfig config(BuildingSupportMode mode){
    mechanics::StructureConfig c;
    c.maxPieces=maxPieces;c.maxDebris=maxDebris;
    c.collapseUnsupported=mode==BuildingSupportMode::LoadBearing;
    return c;
}
inline Fixed mm(float n){return Fixed::fromRatio(int64_t(std::llround(double(n)*1000.)),1000);}
inline world::Bounds bounds(const BuildingGraphSolid& s){
    return {{mm(s.x0),mm(s.y0),mm(s.z0)},{mm(s.x1),mm(s.y1),mm(s.z1)}};
}
inline bool same(const world::Bounds& a,const world::Bounds& b){return a.min==b.min&&a.max==b.max;}
inline bool supportedPart(BuildingGraphPart p){
    return p==BuildingGraphPart::Floor||p==BuildingGraphPart::Wall||p==BuildingGraphPart::Trim||
        p==BuildingGraphPart::Roof||p==BuildingGraphPart::WindowPane||p==BuildingGraphPart::Partition;
}
inline bool loadBearing(BuildingGraphPart p){
    return p==BuildingGraphPart::Floor||p==BuildingGraphPart::Wall||p==BuildingGraphPart::Roof;
}
inline int split(Fixed span){return span>=Fixed::fromRatio(3,1)?3:span>=Fixed::fromRatio(12,10)?2:1;}
inline mechanics::StructurePiece piece(const BuildingGraphSolid& s,int64_t id){
    mechanics::StructurePiece p;p.id=id;p.box=bounds(s);
    const auto size=p.box.max-p.box.min;
    p.anchored=s.part==BuildingGraphPart::Floor&&p.box.min.y<=mm(.001f);
    switch(s.part){
        case BuildingGraphPart::WindowPane:p.health=18;p.armor=0;break;
        case BuildingGraphPart::Wall:p.health=120;p.armor=8;p.fractureThreshold=65;break;
        case BuildingGraphPart::Floor:p.health=180;p.armor=12;p.fractureThreshold=90;break;
        case BuildingGraphPart::Roof:p.health=140;p.armor=8;p.fractureThreshold=70;break;
        case BuildingGraphPart::Partition:p.health=75;p.armor=3;p.fractureThreshold=40;break;
        default:p.health=60;p.armor=3;break;
    }
    if(p.fractureThreshold){
        p.fractureX=split(size.x);p.fractureY=split(size.y);p.fractureZ=split(size.z);
    }
    return p;
}
} // namespace buildingDestructionDetail

struct BuildingDestructionSource {
    int64_t sourceId=0; // initial graph solid index + 1
    BuildingGraphPart part=BuildingGraphPart::Wall;
    uint32_t featureIndex=UINT32_MAX;
};
inline void io(Archive& a,BuildingDestructionSource& s){a(s.sourceId,s.part,s.featureIndex);}
struct BuildingDestructionState {
    uint64_t fingerprint=0,revision=0;
    uint32_t initialCount=0;
    BuildingSupportMode supportMode=BuildingSupportMode::LoadBearing;
    lib::mechanics::StructureState structure;
    // A source survives in this map while its live fragment or debris exists.
    std::map<int64_t,BuildingDestructionSource> sources;
};
inline bool validBuildingDestruction(const BuildingDestructionState& s){
    using namespace buildingDestructionDetail;
    if(!s.fingerprint||!s.initialCount||s.initialCount>maxPieces||uint8_t(s.supportMode)>uint8_t(BuildingSupportMode::LoadBearing)||
       s.structure.pieces.size()>maxPieces||s.structure.debris.size()>maxDebris||
       s.sources.size()>maxPieces+maxDebris||!lib::mechanics::valid(s.structure))return false;
    std::set<int64_t> used;
    for(const auto& [id,p]:s.structure.pieces){(void)p;used.insert(id);}
    for(const auto& [id,d]:s.structure.debris){(void)id;if(d.piece<=0||d.piece>=s.structure.nextId)return false;used.insert(d.piece);}
    if(used.size()!=s.sources.size())return false;
    for(const auto& [id,source]:s.sources)if(!used.contains(id)||source.sourceId<=0||
        source.sourceId>s.initialCount||!supportedPart(source.part))return false;
    return true;
}
// Fixed-point connectivity only, not structural engineering. The
// tighter bearing contact tolerance is not widened by decorative gaps.
inline std::set<int64_t> buildingSupportedPieces(const lib::mechanics::StructureState& state,
    const std::map<int64_t,BuildingDestructionSource>& sources,Fixed tolerance=Fixed::fromRatio(1,20)){
    using namespace buildingDestructionDetail;
    auto transmits=[&](const lib::mechanics::StructurePiece& p){
        const auto it=sources.find(p.id);return it!=sources.end()&&loadBearing(it->second.part);
    };
    return lib::mechanics::supportedPieces(state,tolerance,transmits,
        [&](const auto& a,const auto& b,Fixed tol){
            if(!transmits(a))return false;
            if(transmits(b))return lib::mechanics::touching(a.box,b.box,tol);
            // Authored window glass has a .06m inset, so attach terminal
            // finishes to nearby load-bearing faces with a .12m tolerance.
            const auto it=sources.find(b.id);
            if(it==sources.end())return false;
            return lib::mechanics::touching(a.box,b.box,Fixed::fromRatio(12,100));
        });
}
inline bool extendBuildingDestructionLineage(std::map<int64_t,BuildingDestructionSource>& sources,
    const lib::mechanics::StructureReport& report){
    for(const auto& [child,parent]:report.fractureChildren){
        const auto it=sources.find(parent);
        if(it==sources.end()||!sources.emplace(child,it->second).second)return false;
    }
    return true;
}
inline void io(Archive& a,BuildingDestructionState& s){
    a(s.fingerprint,s.revision,s.initialCount,s.supportMode,s.structure,s.sources);
    if(!a.ok()||!validBuildingDestruction(s))a.fail();
}
inline uint64_t buildingDestructionStateHash(const BuildingDestructionState& state){
    auto copy=state;Hasher h;Archive archive(h);archive(copy);return archive.ok()?h.value():0;
}
inline std::optional<BuildingDestructionState> beginBuildingDestruction(
    const BuildingStructureGraph& graph,std::string* error=nullptr,
    BuildingSupportMode mode=BuildingSupportMode::LoadBearing){
    using namespace buildingDestructionDetail;
    auto fail=[&](const char* why)->std::optional<BuildingDestructionState>{if(error)*error=why;return std::nullopt;};
    if(!graph||graph.solids.empty()||graph.solids.size()>maxPieces)return fail("Invalid building destruction piece budget");
    // Dynamic panels, lifts, furnished rooms and special polygon/roof
    // geometry have separate owners. Fail closed rather than silently
    // discard their collision or change the shape in this adapter.
    if(!graph.visualPrisms.empty()||!graph.roofSections.empty()||!graph.furniture.empty()||
       !graph.interiorLights.empty()||!graph.interiorDecor.empty()||!graph.terraces.empty()||!graph.balconies.empty())
        return fail("Building destruction currently requires a static orthogonal shell");
    for(const auto& socket:graph.sockets)
        if(socket.kind!=lib::systems::BuildingFeatureKind::Window)
            return fail("Live aperture or vertical-core ownership is not integrated with destruction");
    BuildingDestructionState state;state.initialCount=uint32_t(graph.solids.size());state.supportMode=mode;
    Hasher hash;hash.str("aiverse.building.destruction.v2");hash.u32(state.initialCount);hash.u32(uint32_t(mode));
    for(size_t i=0;i<graph.solids.size();++i){
        const auto& solid=graph.solids[i];
        if(!supportedPart(solid.part)||solid.roofTop[0]>=0.f||solid.collisionOnly||
           solid.furnitureIndex!=UINT32_MAX)return fail("Unsupported building solid for destruction");
        const int64_t id=int64_t(i+1);
        const auto p=piece(solid,id);
        if(!lib::mechanics::addPiece(state.structure,p))return fail("Invalid quantized destruction piece");
        state.sources.emplace(id,BuildingDestructionSource{id,solid.part,solid.featureIndex});
        hash.i64(id);hash.u32(uint32_t(solid.part));hash.u32(solid.featureIndex);
        hash.v3(p.box.min);hash.v3(p.box.max);hash.i64(p.health);hash.i64(p.armor);hash.u32(p.anchored?1u:0u);
        hash.u32(uint32_t(p.fractureX));hash.u32(uint32_t(p.fractureY));hash.u32(uint32_t(p.fractureZ));hash.i64(p.fractureThreshold);
    }
    state.fingerprint=hash.value();
    if(!validBuildingDestruction(state))return fail("Invalid initial destruction state");
    if(mode==BuildingSupportMode::LoadBearing&&buildingSupportedPieces(state.structure,state.sources).size()!=state.structure.pieces.size())
        return fail("Initial building contains unsupported pieces; repair the shell or use damage-only mode");
    if(error)error->clear();return state;
}

struct BuildingDestructionPieceView {
    int64_t id=0;world::Bounds box{};BuildingDestructionSource source;
};
struct BuildingDestructionDebrisView {
    lib::mechanics::DebrisBody body;BuildingDestructionSource source;
};
struct BuildingDestructionFrame {
    uint64_t baseHash=0;
    BuildingDestructionState next;
    // Net collider delta: fragments that spawned and died within this
    // tick never enter the static collider set.
    std::vector<BuildingDestructionPieceView> removed,added;
    std::vector<BuildingDestructionDebrisView> debris;
    std::optional<world::Bounds> dirty;
    std::vector<lib::mechanics::StructureEvent> events;
};
inline std::optional<BuildingDestructionFrame> stageBuildingDestruction(
    const BuildingDestructionState& current,uint32_t tick,Fixed dt,
    std::span<const lib::mechanics::StructureDamage> hits){
    using namespace buildingDestructionDetail;
    if(!validBuildingDestruction(current)||current.revision==UINT64_MAX)return std::nullopt;
    // Bound multiplication in the shared falloff policy and prevent absurd
    // commands from producing oversized dirty regions.
    for(const auto& hit:hits)if(hit.amount<0||hit.amount>1000000||hit.radius>Fixed::fromRatio(320,1))return std::nullopt;
    BuildingDestructionFrame frame;frame.baseHash=buildingDestructionStateHash(current);frame.next=current;
    if(!frame.baseHash)return std::nullopt;
    bool lineageOk=true,lineageApplied=false;
    auto report=lib::mechanics::stepStructureWithSupport(config(current.supportMode),frame.next.structure,tick,dt,hits,
        [&](const lib::mechanics::StructureState& candidate,Fixed tol,const lib::mechanics::StructureReport& events){
            lineageApplied=true;lineageOk=extendBuildingDestructionLineage(frame.next.sources,events);
            return lineageOk?buildingSupportedPieces(candidate,frame.next.sources,tol):std::set<int64_t>{};
        });
    if(!report||!lineageOk)return std::nullopt;
    if(!lineageApplied&&!extendBuildingDestructionLineage(frame.next.sources,*report))return std::nullopt;
    if(current.supportMode==BuildingSupportMode::LoadBearing&&
       buildingSupportedPieces(frame.next.structure,frame.next.sources).size()!=frame.next.structure.pieces.size())return std::nullopt;
    for(const auto& [id,p]:current.structure.pieces){
        const auto it=frame.next.structure.pieces.find(id);
        if(it==frame.next.structure.pieces.end()||!same(p.box,it->second.box)){
            frame.removed.push_back({id,p.box,current.sources.at(id)});lib::mechanics::grow(frame.dirty,p.box);
        }
    }
    std::set<int64_t> used;
    for(const auto& [id,p]:frame.next.structure.pieces){
        const auto source=frame.next.sources.find(id);if(source==frame.next.sources.end())return std::nullopt;
        used.insert(id);const auto old=current.structure.pieces.find(id);
        if(old==current.structure.pieces.end()||!same(p.box,old->second.box)){
            frame.added.push_back({id,p.box,source->second});lib::mechanics::grow(frame.dirty,p.box);
        }
    }
    for(const auto& [id,d]:frame.next.structure.debris){
        (void)id;const auto source=frame.next.sources.find(d.piece);if(source==frame.next.sources.end())return std::nullopt;
        used.insert(d.piece);frame.debris.push_back({d,source->second});
    }
    for(auto it=frame.next.sources.begin();it!=frame.next.sources.end();){
        if(!used.contains(it->first))it=frame.next.sources.erase(it);else ++it;
    }
    frame.next.revision=current.revision+1;
    frame.events=std::move(report->events);
    if(!validBuildingDestruction(frame.next))return std::nullopt;
    return frame;
}
// Call ONLY after the authoritative owner has accepted the staged physical
// and navigation changes. The old state stays intact on rejection.
inline bool commitBuildingDestruction(BuildingDestructionState& current,const BuildingDestructionFrame& frame){
    if(current.revision==UINT64_MAX||!validBuildingDestruction(current)||!validBuildingDestruction(frame.next)||
       frame.next.fingerprint!=current.fingerprint||frame.next.initialCount!=current.initialCount||frame.next.supportMode!=current.supportMode||
       frame.next.revision!=current.revision+1||frame.baseHash!=buildingDestructionStateHash(current))return false;
    current=frame.next;return true;
}
} // namespace aiverse::building
