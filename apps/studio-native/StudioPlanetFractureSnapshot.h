// Read-only structural diagnostics. Never run fracture::advance or deliver events.
#pragma once
#include "aiverselib/world/FractureEcs.h"
#include "aiverselib/world/WorldProcessSurfaceFrame.h"
#include "engine/physis/PhysisFractureOutbox.h"
#include "engine/game/Components.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {
inline Json samplePlanetFracture(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,size_t memberLimit=256,size_t bondLimit=512){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),members=Json::array(),bonds=Json::array();
    memberLimit=std::min(memberLimit,size_t(256));bondLimit=std::min(bondLimit,size_t(512));
    const auto memberCount=ecs.countExisting<fracture::Member>();
    const auto bondCount=ecs.countExisting<fracture::Bond>();
    const bool capped=memberCount>fracture::kMaxMembers||bondCount>fracture::kMaxBonds;
    const bool available=!capped&&completedTick!=UINT32_MAX&&surface.sphere()&&surface.tab;
    bool truncated=capped;
    size_t rejected=0,crossBody=0,omitted=0,pending=0,detached=0,broken=0,anchored=0;
    struct Location {int body;Vec3 position;bool inactive;};
    std::map<uint64_t,Location> selected;
    const auto vec=[](Vec3 v){
        Json a=Json::array();for(auto c:{v.x,v.y,v.z})a.push(Json::number(c.toDouble(),4));return a;
    };
    const auto delivery=[](unsigned d){return d==0?"empty":d==1?"pending":d==2?"acknowledged":"invalid";};
    if(available){
        std::vector<Entity> ids;
        ids.reserve(memberCount);
        ecs.eachExistingConst<fracture::Member>([&](Entity e,const fracture::Member& m){
            if(!ecs.alive(e))return;
            ids.push_back(e);
            if(m.detached)++detached;
            if(m.broken)++broken;
            if(m.anchored&&!m.broken&&!m.detached)++anchored;
            const auto* box=ecs.tryGetConst<fracture::Collapse>(e);
            if(m.detached&&box&&box->delivery==fracture::Delivery::Pending&&box->eventKey)++pending;
        });
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(auto e:ids){
            const auto* m=ecs.tryGetConst<fracture::Member>(e);
            const auto* t=ecs.tryGetConst<Transform>(e);
            const auto* u=ecs.tryGetConst<game::Unit>(e);
            if(!m||!t||ecs.has<game::Transit>(e)||(u&&(u->flags&(game::kUnitOffMap|game::kUnitInTransit)))){
                ++rejected;continue;
            }
            const auto frame=wp::surfaceFrame(&surface,t->pos);
            if(!frame){++rejected;continue;}
            if(selected.size()>=memberLimit){truncated=true;break;}
            const auto anchor=game::Surface::anchor(frame->body);
            selected.emplace(e.packed(),Location{frame->body,t->pos,m->broken||m->detached});
            Json row=Json::object();
            row.set("id",Json::string(std::to_string(e.packed())));
            row.set("body",Json::string(surface.bodies[size_t(frame->body)].id));
            row.set("position",vec(t->pos-anchor));
            row.set("anchored",Json::fromBool(m->anchored));
            row.set("broken",Json::fromBool(m->broken));
            row.set("detached",Json::fromBool(m->detached));
            if(const auto* box=ecs.tryGetConst<fracture::Collapse>(e)){
                Json event=Json::object();
                event.set("delivery",Json::string(delivery(unsigned(box->delivery))));
                event.set("key",Json::string(std::to_string(box->eventKey)));
                event.set("tick",Json::number(int64_t(box->detachedTick)));
                row.set("collapse",std::move(event));
            }
            // PHYSIS uses its own climate clock; do not equate it with simulation tick.
            if(const auto* stress=ecs.tryGetConst<physis::NatureStressOutputComponent>(e);stress&&stress->valid){
                Json s=Json::object();
                s.set("climateTick",Json::string(std::to_string(stress->sampledClimateTick)));
                s.set("windLoadN",Json::number(stress->windLoadN.toDouble(),4));
                s.set("bendingMomentNm",Json::number(stress->bendingMomentNm.toDouble(),4));
                s.set("normalizedLoad",Json::number(stress->normalizedLoad.toDouble(),4));
                s.set("accumulatedDamage",Json::number(stress->accumulatedDamage.toDouble(),4));
                s.set("fractured",Json::fromBool(stress->fractured));
                row.set("stress",std::move(s));
            }
            if(const auto* nature=ecs.tryGetConst<physis::NatureFractureOutboxComponent>(e)){
                Json event=Json::object();
                event.set("delivery",Json::string(delivery(unsigned(nature->delivery))));
                event.set("key",Json::string(std::to_string(nature->eventKey)));
                event.set("climateTick",Json::string(std::to_string(nature->climateTick)));
                row.set("natureFracture",std::move(event));
            }
            members.push(std::move(row));
        }
        ids.clear();ids.reserve(bondCount);
        ecs.eachExistingConst<fracture::Bond>([&](Entity e,const fracture::Bond&){if(ecs.alive(e))ids.push_back(e);});
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(auto e:ids){
            const auto* b=ecs.tryGetConst<fracture::Bond>(e);
            if(!b||b->first==b->second||b->strength<=0||b->strength>fracture::kMaxBondStrength||
               b->damage<0||b->damage>b->strength){++rejected;continue;}
            const auto a=selected.find(b->first.packed()),z=selected.find(b->second.packed());
            if(a==selected.end()||z==selected.end()){++omitted;continue;}
            if(a->second.body!=z->second.body){++crossBody;continue;}
            if(bonds.size()>=bondLimit){truncated=true;break;}
            const auto anchor=game::Surface::anchor(a->second.body);
            Json row=Json::object();
            row.set("id",Json::string(std::to_string(e.packed())));
            row.set("body",Json::string(surface.bodies[size_t(a->second.body)].id));
            row.set("first",Json::string(std::to_string(a->first)));
            row.set("second",Json::string(std::to_string(z->first)));
            row.set("a",vec(a->second.position-anchor));row.set("b",vec(z->second.position-anchor));
            row.set("strength",Json::number(b->strength));row.set("damage",Json::number(b->damage));
            row.set("enabled",Json::fromBool(b->enabled));
            row.set("activeSupportEdge",Json::fromBool(fracture::validBond(*b)&&!a->second.inactive&&!z->second.inactive));
            bonds.push(std::move(row));
        }
    }
    out.set("available",Json::fromBool(available));out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("memberCount",Json::number(int64_t(memberCount)));out.set("bondCount",Json::number(int64_t(bondCount)));
    out.set("anchored",Json::number(int64_t(anchored)));out.set("broken",Json::number(int64_t(broken)));
    out.set("detached",Json::number(int64_t(detached)));out.set("pending",Json::number(int64_t(pending)));
    out.set("truncated",Json::fromBool(truncated));out.set("rejected",Json::number(int64_t(rejected)));
    out.set("crossBody",Json::number(int64_t(crossBody)));out.set("omittedBonds",Json::number(int64_t(omitted)));
    out.set("members",std::move(members));out.set("bonds",std::move(bonds));
    return out;
}
} // namespace aiverse::studio
