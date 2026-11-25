// Planetary trace producer; canonical IMPRINT Cell/Tracker configuration and
// GameCore Surface remain the owners. No parallel terrain or movement store.
#pragma once
#include "ImprintTrails.h"
#include "WorldProcessSurfaceFrame.h"
#include "engine/sim/CommonComponents.h"
#include "engine/game/Components.h"
#include <map>
#include <optional>

namespace aiverse::imprint {
// Only the new position history is persistent. Legacy Tracker X/Z history
// is unused by this producer; never install both producers on one world.
struct SurfaceTrailHistory {
    Entity source=kNullEntity;
    Vec3 last{};
    int32_t body=-1;
    uint32_t processedTick=UINT32_MAX;
    bool initialized=false;
};
inline void io(Archive& a,SurfaceTrailHistory& h){
    a(h.source,h.last.x,h.last.y,h.last.z,h.body,h.processedTick,h.initialized);
    if(!a.writing()&&(h.body< -1||h.body>=game::Surface::kMaxBodies||
        (h.initialized&&(!h.source.valid()||h.body<0))))a.fail();
}
// Derived, sparse index over existing enabled trace entities. Canonical
// Surface cell includes body and face: no XZ projection, seam wrapping
// assumptions or aliasing identical coordinates on different planets.
class SurfaceTraceIndex {
    std::map<int,Entity> cells_;
public:
    bool build(const World& world,const game::Surface& surface){
        if(world.countExisting<Cell>()>kMaxCells)return false;
        std::map<int,Entity> next;
        bool ok=true;
        world.eachExistingConst<Cell>([&](Entity e,const Cell& c){
            if(!world.alive(e)||!c.enabled)return;
            if(!valid(c)){ok=false;return;}
            const auto* tr=world.tryGetConst<Transform>(e);
            if(!tr)return;
            const auto f=lib::worldprocess::surfaceFrame(&surface,tr->pos);
            if(!f)return;
            auto [it,inserted]=next.try_emplace(f->cell,e);
            if(!inserted&&e.packed()<it->second.packed())it->second=e;
        });
        if(!ok)return false;
        cells_=std::move(next);return true;
    }
    Entity find(int cell)const{
        const auto it=cells_.find(cell);
        return it==cells_.end()?kNullEntity:it->second;
    }
};
// A tracker follows actual GameCore Transform, after movement and before
// IMPRINT diffusion. Missing position/transit/uncovered cells reset the
// trail: never stamp a phantom path across space or unrepresented ground.
// Existing authored cell link is preferred only when physically colocated;
// otherwise resolve the current cell by sparse canonical index.
inline bool stampSurfaceMotion(World& world,const game::Surface& surface,uint32_t tick){
    namespace wp=lib::worldprocess;
    const World& read=world;
    if(tick==UINT32_MAX||read.countExisting<Tracker>()>kMaxTrackers||
       read.countExisting<SurfaceTrailHistory>()>kMaxTrackers)return false;
    std::vector<std::pair<Entity,Tracker>> tracks;
    read.eachExistingConst<Tracker>([&](Entity e,const Tracker& t){
        if(read.alive(e))tracks.push_back({e,t});
    });
    if(tracks.empty())return true;
    std::sort(tracks.begin(),tracks.end(),[](const auto& a,const auto& b){
        return a.first.packed()<b.first.packed();
    });
    SurfaceTraceIndex index;
    if(!index.build(read,surface))return false;
    std::vector<std::pair<Entity,SurfaceTrailHistory>> histories;
    std::map<uint64_t,Cell> deposits;
    for(const auto& [e,t]:tracks){
        if(!valid(t))return false;
        SurfaceTrailHistory h;
        if(const auto* old=read.tryGetConst<SurfaceTrailHistory>(e))h=*old;
        if(h.processedTick==tick)continue;
        if(h.processedTick!=UINT32_MAX&&h.processedTick>tick)return false;
        const auto oldSource=h.source;
        h.source=t.source;h.processedTick=tick;
        const auto* tr=read.tryGetConst<Transform>(t.source);
        const auto* unit=read.tryGetConst<game::Unit>(t.source);
        const auto frame=t.enabled&&read.alive(t.source)&&tr&&
            !read.has<game::Transit>(t.source)&&
            !(unit&&(unit->flags&(game::kUnitOffMap|game::kUnitInTransit)))
            ?wp::surfaceFrame(&surface,tr->pos):std::nullopt;
        Entity target=kNullEntity;
        if(frame){
            target=index.find(frame->cell);
            if(read.alive(t.cell)){
                const auto* c=read.tryGetConst<Cell>(t.cell);
                const auto* ct=read.tryGetConst<Transform>(t.cell);
                if(c&&c->enabled&&valid(*c)&&ct){
                    const auto cf=wp::surfaceFrame(&surface,ct->pos);
                    if(cf&&cf->cell==frame->cell)target=t.cell;
                }
            }
        }
        if(!frame||!target.valid()){
            h.initialized=false;h.body=-1;h.last={};
        }else{
            const auto previous=h.initialized&&oldSource==t.source&&h.body==frame->body
                ?wp::surfaceFrame(&surface,h.last):std::nullopt;
            const auto distance=previous?wp::surfaceGroundDistance(&surface,*previous,*frame):std::nullopt;
            if(!distance){
                h.initialized=true;h.body=frame->body;h.last=frame->origin;
            }else if(*distance>=Fixed::fromRatio(t.minimumDistance,100)){
                const auto* c=read.tryGetConst<Cell>(target);
                if(!c||!c->enabled||!valid(*c)||!read.alive(target))return false;
                auto& out=deposits.try_emplace(target.packed(),*c).first->second;
                out.footprint=std::min(kMaxLevel,out.footprint+t.footprint);
                out.scent=std::min(kMaxLevel,out.scent+t.scent);
                out.residue=std::min(kMaxLevel,out.residue+t.residue);
                h.last=frame->origin;
            }
        }
        histories.push_back({e,h});
    }
    if(histories.empty())return true;
    // Structural additions use a staged World; ordinary ticks preflight
    // both existing write pools and publish only trivial value assignments.
    // Avoid copying the entire world on every movement sample.
    const bool structural=std::any_of(histories.begin(),histories.end(),
        [&](const auto& item){return !read.has<SurfaceTrailHistory>(item.first);});
    if(structural){
        World staged=world;
        for(const auto& [e,h]:histories){
            if(staged.has<SurfaceTrailHistory>(e))staged.get<SurfaceTrailHistory>(e)=h;
            else staged.add<SurfaceTrailHistory>(e,h);
        }
        for(const auto& [key,c]:deposits)staged.get<Cell>(Entity::fromPacked(key))=c;
        world=std::move(staged);
    }else{
        if(!world.prepareExistingWrite<SurfaceTrailHistory>())return false;
        if(!deposits.empty()&&!world.prepareExistingWrite<Cell>())return false;
        for(const auto& [e,h]:histories)world.get<SurfaceTrailHistory>(e)=h;
        for(const auto& [key,c]:deposits)world.get<Cell>(Entity::fromPacked(key))=c;
    }
    return true;
}
} // namespace aiverse::imprint
