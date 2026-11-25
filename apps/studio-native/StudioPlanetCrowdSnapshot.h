// Local Play observation of CROWD reservations and the actual movement owner.
// No route generation, movement command, reservation planning or ECS writes.
#pragma once
#include "aiverselib/world/CrowdGameCoreSurface.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {
inline Json samplePlanetCrowd(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,bool gravityEnabled,const game::GameCore* host=nullptr,
        size_t agentLimit=256,size_t slotLimit=512,size_t routeBudget=2048){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),agents=Json::array(),slots=Json::array();
    agentLimit=std::min(agentLimit,size_t(256));slotLimit=std::min(slotLimit,size_t(512));
    routeBudget=std::min(routeBudget,size_t(2048));
    const auto agentCount=ecs.countExisting<crowd::Agent>();
    const auto slotCount=ecs.countExisting<crowd::Slot>();
    const auto preferenceCount=ecs.countExisting<crowd::Preference>();
    const bool capped=agentCount>crowd::kMaxAgents||slotCount>crowd::kMaxSlots||preferenceCount>crowd::kMaxPreferences;
    const bool available=!capped&&completedTick!=UINT32_MAX&&surface.sphere()&&surface.tab;
    const bool gravityCapped=gravityEnabled&&ecs.countExisting<physis::GravityField>()>1024;
    bool truncated=capped;
    size_t rejected=0,unplaced=0,crossBody=0,invalidReservation=0,routePoints=0,stale=0;
    struct Location {int body=-1;Vec3 position{};};
    const auto locate=[&](Entity e){
        Location none;
        if(!ecs.alive(e)||ecs.has<game::Transit>(e))return none;
        if(const auto* u=ecs.tryGetConst<game::Unit>(e);u&&(u->flags&(game::kUnitOffMap|game::kUnitInTransit)))return none;
        const auto* tr=ecs.tryGetConst<Transform>(e);if(!tr)return none;
        const auto frame=wp::surfaceFrame(&surface,tr->pos);
        return frame?Location{frame->body,tr->pos}:none;
    };
    const auto vec=[](Vec3 p){Json a=Json::array();for(auto v:{p.x,p.y,p.z})a.push(Json::number(v.toDouble(),4));return a;};
    const auto id=[](Entity e){return Json::string(std::to_string(e.packed()));};
    const auto putPosition=[&](Json& row,const Location& p){
        row.set("placed",Json::fromBool(p.body>=0));
        if(p.body<0){++unplaced;return;}
        row.set("body",Json::string(surface.bodies[size_t(p.body)].id));
        row.set("position",vec(p.position-game::Surface::anchor(p.body)));
        if(gravityEnabled&&!gravityCapped)if(const auto g=wp::samplePlanetGravitySupport(ecs,surface,p.position)){
            const char* status="airborne";
            switch(g->status){
                case wp::GravitySupportStatus::Supported:status="supported";break;
                case wp::GravitySupportStatus::WeakGravity:status="weak";break;
                case wp::GravitySupportStatus::Outward:status="outward";break;
                case wp::GravitySupportStatus::TooSteep:status="steep";break;
                case wp::GravitySupportStatus::Airborne:break;
            }
            Json support=Json::object();
            support.set("status",Json::string(status));support.set("acceleration",vec(g->acceleration));
            support.set("normal",vec(g->normal));support.set("clearance",Json::number(g->signedClearance.toDouble(),4));
            support.set("inward",Json::number(g->inwardAcceleration.toDouble(),4));
            row.set("support",std::move(support));
        }
    };
    if(available){
        const auto ordered=[&]<class C>(){
            std::vector<Entity> ids;ids.reserve(ecs.countExisting<C>());
            ecs.eachExistingConst<C>([&](Entity e,const C&){if(ecs.alive(e))ids.push_back(e);});
            std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
            return ids;
        };
        // Occupancy counts use all valid current agents, not just visible rows.
        std::map<uint64_t,size_t> occupied;
        const auto allAgents=ordered.template operator()<crowd::Agent>();
        for(auto e:allAgents){
            const auto& a=*ecs.tryGetConst<crowd::Agent>(e);
            if(!crowd::valid(a)||a.sampledTick!=completedTick||!a.enabled||!a.allocated.valid())continue;
            const auto* slot=ecs.tryGetConst<crowd::Slot>(a.allocated);
            const auto src=locate(e),dst=locate(a.allocated);
            if(slot&&slot->enabled&&crowd::valid(*slot)&&src.body>=0&&src.body==dst.body)++occupied[a.allocated.packed()];
        }
        // Preferences are authored candidates, not successful allocations.
        std::map<uint64_t,std::vector<Entity>> preferences;
        for(auto e:ordered.template operator()<crowd::Preference>()){
            const auto& p=*ecs.tryGetConst<crowd::Preference>(e);
            if(!p.enabled||!ecs.alive(p.agent)||!ecs.has<crowd::Agent>(p.agent))continue;
            auto& list=preferences[p.agent.packed()];
            for(auto option:p.options){
                if(!ecs.alive(option)||!ecs.has<crowd::Slot>(option))continue;
                if(std::find(list.begin(),list.end(),option)!=list.end())continue;
                if(list.size()<16)list.push_back(option);else truncated=true;
            }
        }
        for(auto e:allAgents){
            const auto& a=*ecs.tryGetConst<crowd::Agent>(e);
            if(!crowd::valid(a)){++rejected;continue;}
            if(a.sampledTick!=completedTick){++stale;continue;}
            if(agents.size()>=agentLimit){truncated=true;break;}
            Json row=Json::object();row.set("id",id(e));row.set("enabled",Json::fromBool(a.enabled));
            const auto p=locate(e);putPosition(row,p);
            if(host)row.set("eligible",Json::fromBool(wp::surfaceCrowdEligible(*host,ecs,e)));
            if(a.allocated.valid()){
                row.set("allocated",id(a.allocated));
                const auto* s=ecs.tryGetConst<crowd::Slot>(a.allocated);
                const auto dst=locate(a.allocated);
                const bool valid=a.enabled&&s&&s->enabled&&crowd::valid(*s)&&p.body>=0&&dst.body==p.body;
                row.set("reservationValid",Json::fromBool(valid));
                if(valid)row.set("reservationPosition",vec(dst.position-game::Surface::anchor(p.body)));
                else{
                    ++invalidReservation;
                    if(p.body>=0&&dst.body>=0&&dst.body!=p.body)++crossBody;
                }
            }
            Json options=Json::array();
            if(const auto it=preferences.find(e.packed());it!=preferences.end())for(auto option:it->second)options.push(id(option));
            row.set("preferences",std::move(options));
            if(const auto* q=ecs.tryGetConst<game::Orders>(e))row.set("orderCount",Json::number(int64_t(q->queue.size())));
            if(const auto* m=ecs.tryGetConst<game::Mobile>(e)){
                row.set("moving",Json::fromBool(m->moving));row.set("flow",Json::fromBool(m->flow));
                row.set("velocity",vec(m->vel));row.set("pathCursor",Json::number(int64_t(m->pathPos)));
                row.set("pathCount",Json::number(int64_t(m->path.size())));
                if(p.body>=0&&m->moving){
                    const auto anchor=game::Surface::anchor(p.body);
                    const auto local=[&](Vec3 pos)->Json{
                        const auto f=wp::surfaceFrame(&surface,pos);
                        if(!f||f->body!=p.body){++crossBody;return Json{};}
                        return vec(pos-anchor);
                    };
                    const auto goal=local(m->goal);
                    if(!goal.isNull())row.set("goal",goal);
                    Json route=Json::array();
                    const size_t start=std::min(size_t(m->pathPos),m->path.size());
                    const size_t take=std::min({m->path.size()-start,size_t(64),routeBudget-routePoints});
                    for(size_t i=0;i<take;++i)route.push(local(m->path[start+i]));
                    routePoints+=take;
                    const bool clipped=take<m->path.size()-start;
                    if(clipped)truncated=true;
                    row.set("routeTruncated",Json::fromBool(clipped));row.set("route",std::move(route));
                }
            }
            agents.push(std::move(row));
        }
        for(auto e:ordered.template operator()<crowd::Slot>()){
            const auto& s=*ecs.tryGetConst<crowd::Slot>(e);
            if(!crowd::valid(s)){++rejected;continue;}
            if(slots.size()>=slotLimit){truncated=true;break;}
            Json row=Json::object();row.set("id",id(e));row.set("enabled",Json::fromBool(s.enabled));
            const auto it=occupied.find(e.packed());
            row.set("occupancy",Json::number(int64_t(it==occupied.end()?0:it->second)));
            putPosition(row,locate(e));slots.push(std::move(row));
        }
    }
    out.set("available",Json::fromBool(available));out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("agentCount",Json::number(int64_t(agentCount)));out.set("slotCount",Json::number(int64_t(slotCount)));
    out.set("preferenceCount",Json::number(int64_t(preferenceCount)));
    out.set("gravityEnabled",Json::fromBool(gravityEnabled));out.set("gravityCapped",Json::fromBool(gravityCapped));
    out.set("truncated",Json::fromBool(truncated));out.set("rejected",Json::number(int64_t(rejected)));
    out.set("stale",Json::number(int64_t(stale)));out.set("unplaced",Json::number(int64_t(unplaced)));
    out.set("crossBody",Json::number(int64_t(crossBody)));out.set("invalidReservation",Json::number(int64_t(invalidReservation)));
    out.set("routePoints",Json::number(int64_t(routePoints)));
    out.set("agents",std::move(agents));out.set("slots",std::move(slots));return out;
}
} // namespace aiverse::studio
