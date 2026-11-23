// Read-only gameplay/presentation queries for all seven world-process ECS
// systems. No secondary authority, renderer references or global caches.
#pragma once
#include "WorldProcessesSuite.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace aiverse::lib::worldprocess::queries {
struct MorphSample {
    Entity entity=kNullEntity;
    std::int64_t x=0,y=0,z=0,vx=0,vy=0,vz=0;
    bool anchored=false;
};
inline std::optional<MorphSample> morphPose(const World& w,Entity e){
    const auto* p=w.tryGetConst<morph::Node>(e);
    if(!p||!p->enabled)return std::nullopt;
    return MorphSample{e,p->x,p->y,p->z,p->vx,p->vy,p->vz,p->anchored};
}
// A view/skin adapter may request all current offsets, but must not modify
// the authoritative simulation; the vector owns a stable, sorted snapshot.
inline std::vector<MorphSample> morphPoses(const World& w){
    std::vector<MorphSample> out;
    w.eachExistingConst<morph::Node>([&](Entity e,const morph::Node& n){
        if(w.alive(e)&&n.enabled)
            out.push_back({e,n.x,n.y,n.z,n.vx,n.vy,n.vz,n.anchored});
    });
    std::sort(out.begin(),out.end(),
        [](const auto& a,const auto& b){return a.entity.packed()<b.entity.packed();});
    return out;
}
struct CrowdSample {
    Entity entity=kNullEntity,slot=kNullEntity;
    std::int64_t x=0,z=0;
};
inline std::optional<CrowdSample> crowdPose(const World& w,Entity e){
    const auto* a=w.tryGetConst<crowd::Agent>(e);
    if(!a||!a->enabled)return std::nullopt;
    return CrowdSample{e,a->allocated,a->x,a->z};
}
struct ImprintSample {
    std::int64_t footprint=0,scent=0,residue=0;
};
inline std::optional<ImprintSample> imprintLevels(const World& w,Entity e){
    const auto* c=w.tryGetConst<imprint::Cell>(e);
    if(!c||!c->enabled)return std::nullopt;
    return ImprintSample{c->footprint,c->scent,c->residue};
}
struct CircuitSample {
    bool powered=false,signal=false;
    std::int64_t supply=0,demand=0;
};
inline std::optional<CircuitSample> circuitState(const World& w,Entity e){
    const auto* n=w.tryGetConst<circuit::Node>(e);
    if(!n||!n->enabled)return std::nullopt;
    return CircuitSample{n->powered,n->signal,n->supply,n->demand};
}
struct GranularSample {
    std::int64_t mass=0,capacity=0;
    bool solid=false;
};
inline std::optional<GranularSample> granularCell(const World& w,Entity e,
                                                 std::uint16_t x,std::uint16_t y){
    const auto* f=w.tryGetConst<granular::Field>(e);
    if(!f||!f->enabled||x>=f->width||y>=f->height||
       f->mass.size()!=std::size_t(f->width)*f->height||
       f->solid.size()!=f->mass.size())return std::nullopt;
    const std::size_t index=std::size_t(y)*f->width+x;
    return GranularSample{f->mass[index],f->capacity,f->solid[index]!=0};
}
struct EvolutionSample {
    std::int64_t durability=0,fertility=0,succession=0,pollution=0;
};
inline std::optional<EvolutionSample> evolution(const World& w,Entity e){
    const auto* r=w.tryGetConst<evolve::Region>(e);
    if(!r||!r->enabled)return std::nullopt;
    return EvolutionSample{r->durability,r->fertility,r->succession,r->pollution};
}
struct SocietySample {
    std::uint64_t knowledge=0;
    std::uint16_t credibility=0;
    std::uint32_t acceptance=0;
};
inline std::optional<SocietySample> societyPerson(const World& w,Entity e){
    const auto* p=w.tryGetConst<society::Person>(e);
    if(!p||!p->enabled)return std::nullopt;
    return SocietySample{p->knowledge,p->credibility,p->acceptance};
}
inline bool knowsTopic(const World& w,Entity e,unsigned bit){
    if(bit>=64)return false;
    const auto person=societyPerson(w,e);
    return person&&((person->knowledge>>bit)&1u)!=0;
}
} // namespace aiverse::lib::worldprocess::queries
