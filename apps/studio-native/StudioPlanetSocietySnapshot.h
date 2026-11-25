// Read-only SOCIETY observations, not a second propagation or NPC dispatcher.
#pragma once
#include "aiverselib/world/SocietyNpcTriggers.h"
#include "aiverselib/world/WorldProcessSurfaceFrame.h"
#include "engine/game/Components.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <bit>
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {
inline Json samplePlanetSociety(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,size_t personLimit=256,size_t contactLimit=512,size_t auxiliaryLimit=128){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),people=Json::array(),contacts=Json::array(),messages=Json::array(),triggers=Json::array();
    personLimit=std::min(personLimit,size_t(256));contactLimit=std::min(contactLimit,size_t(512));
    auxiliaryLimit=std::min(auxiliaryLimit,size_t(128));
    const auto pc=ecs.countExisting<society::Person>(),cc=ecs.countExisting<society::Contact>();
    const auto mc=ecs.countExisting<society::Message>(),tc=ecs.countExisting<society::NpcTrigger>();
    const bool capped=pc>society::kMaxPeople||cc>society::kMaxContacts||mc>society::kMaxMessages||tc>society::kMaxTriggers;
    const bool available=!capped&&completedTick!=UINT32_MAX&&surface.sphere()&&surface.tab;
    bool truncated=capped;
    size_t rejected=0,unplaced=0,crossBody=0,omitted=0,stale=0,eligible=0,active=0,fired=0;
    struct Location {int body=-1;Vec3 local{};};
    const auto locate=[&](Entity e){
        Location none;
        if(!ecs.alive(e)||ecs.has<game::Transit>(e))return none;
        if(const auto* u=ecs.tryGetConst<game::Unit>(e);u&&(u->flags&(game::kUnitOffMap|game::kUnitInTransit)))return none;
        const auto* t=ecs.tryGetConst<Transform>(e);if(!t)return none;
        const auto f=wp::surfaceFrame(&surface,t->pos);if(!f)return none;
        return Location{f->body,t->pos-game::Surface::anchor(f->body)};
    };
    const auto vec=[](Vec3 v){Json a=Json::array();for(auto c:{v.x,v.y,v.z})a.push(Json::number(c.toDouble(),4));return a;};
    const auto id=[](Entity e){return Json::string(std::to_string(e.packed()));};
    const auto mask=[](society::Mask m){return Json::string(std::to_string(m));};
    const auto person=[&](Entity e)->const society::Person*{
        if(!ecs.alive(e))return nullptr;
        const auto* p=ecs.tryGetConst<society::Person>(e);
        return p&&society::valid(*p)?p:nullptr;
    };
    const auto putPosition=[&](Json& row,const Location& p){
        row.set("placed",Json::fromBool(p.body>=0));
        if(p.body<0){++unplaced;return;}
        row.set("body",Json::string(surface.bodies[size_t(p.body)].id));row.set("position",vec(p.local));
    };
    if(available){
        const auto ordered=[&]<class C>(){
            std::vector<Entity> ids;ids.reserve(ecs.countExisting<C>());
            ecs.eachExistingConst<C>([&](Entity e,const C&){if(ecs.alive(e))ids.push_back(e);});
            std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});return ids;
        };
        std::map<uint64_t,Location> selected;
        for(auto e:ordered.template operator()<society::Person>()){
            const auto* p=person(e);if(!p){++rejected;continue;}
            if(people.size()>=personLimit){truncated=true;break;}
            const auto pos=locate(e);selected.emplace(e.packed(),pos);
            Json row=Json::object();row.set("id",id(e));putPosition(row,pos);
            row.set("enabled",Json::fromBool(p->enabled));row.set("credibility",Json::number(int64_t(p->credibility)));
            row.set("acceptance",Json::number(int64_t(p->acceptance)));
            const bool current=p->sampledTick==completedTick;
            row.set("knowledgeAvailable",Json::fromBool(current));
            if(current){row.set("knowledge",mask(p->knowledge));row.set("topicCount",Json::number(int64_t(std::popcount(p->knowledge))));}
            else ++stale;
            people.push(std::move(row));
        }
        for(auto e:ordered.template operator()<society::Contact>()){
            const auto& c=*ecs.tryGetConst<society::Contact>(e);
            if(!society::valid(c)||c.source==c.target){++rejected;continue;}
            const auto a=selected.find(c.source.packed()),b=selected.find(c.target.packed());
            if(a==selected.end()||b==selected.end()){++omitted;continue;}
            if(contacts.size()>=contactLimit){truncated=true;break;}
            const auto* src=person(c.source);const auto* dst=person(c.target);
            if(!src||!dst){++rejected;continue;}
            const uint32_t score=uint32_t(c.trust)*src->credibility;
            const bool allowed=c.enabled&&src->enabled&&dst->enabled&&score>=dst->acceptance;
            if(allowed)++eligible;
            Json row=Json::object();row.set("id",id(e));row.set("source",id(c.source));row.set("target",id(c.target));
            row.set("enabled",Json::fromBool(c.enabled));row.set("trust",Json::number(int64_t(c.trust)));
            row.set("score",Json::number(int64_t(score)));row.set("threshold",Json::number(int64_t(dst->acceptance)));
            row.set("eligibleNow",Json::fromBool(allowed));
            if(src->sampledTick==completedTick&&dst->sampledTick==completedTick)
                row.set("sourceOnlyTopics",mask(src->knowledge&~dst->knowledge));
            const auto& x=a->second;const auto& y=b->second;
            const bool placed=x.body>=0&&x.body==y.body;
            if(x.body>=0&&y.body>=0&&x.body!=y.body)++crossBody;
            row.set("placed",Json::fromBool(placed));
            if(placed){row.set("body",Json::string(surface.bodies[size_t(x.body)].id));row.set("a",vec(x.local));row.set("b",vec(y.local));}
            contacts.push(std::move(row));
        }
        const auto putBinding=[&](Json& row,const Location& origin,Entity target){
            if(!person(target))return;
            const auto p=locate(target);
            if(origin.body>=0&&p.body==origin.body)row.set("personPosition",vec(p.local));
            else if(origin.body>=0&&p.body>=0)++crossBody;
        };
        for(auto e:ordered.template operator()<society::Message>()){
            const auto& m=*ecs.tryGetConst<society::Message>(e);
            if(messages.size()>=auxiliaryLimit){truncated=true;break;}
            const auto p=locate(e);const auto* src=person(m.source);
            Json row=Json::object();row.set("id",id(e));putPosition(row,p);
            row.set("source",id(m.source));row.set("topics",mask(m.topics));row.set("enabled",Json::fromBool(m.enabled));
            row.set("sourceValid",Json::fromBool(src&&src->enabled));putBinding(row,p,m.source);
            messages.push(std::move(row));
        }
        for(auto e:ordered.template operator()<society::NpcTrigger>()){
            const auto& t=*ecs.tryGetConst<society::NpcTrigger>(e);
            if(!society::valid(t)){++rejected;continue;}
            if(triggers.size()>=auxiliaryLimit){truncated=true;break;}
            const auto p=locate(e);Json row=Json::object();row.set("id",id(e));putPosition(row,p);
            const char* modes[]={"all","any","none"};
            row.set("person",id(t.person));row.set("topics",mask(t.topics));row.set("actionId",Json::number(int64_t(t.actionId)));
            row.set("mode",Json::string(modes[unsigned(t.mode)]));row.set("enabled",Json::fromBool(t.enabled));row.set("once",Json::fromBool(t.once));
            const bool current=t.sampledTick==completedTick;row.set("outputAvailable",Json::fromBool(current));
            if(current){
                row.set("active",Json::fromBool(t.active));row.set("fired",Json::fromBool(t.fired));
                row.set("transition",Json::number(int64_t(t.transition)));
                if(t.changedTick!=UINT32_MAX)row.set("changedTick",Json::number(int64_t(t.changedTick)));
                if(t.active)++active;if(t.fired)++fired;
            }else ++stale;
            putBinding(row,p,t.person);triggers.push(std::move(row));
        }
    }
    out.set("available",Json::fromBool(available));out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("personCount",Json::number(int64_t(pc)));out.set("contactCount",Json::number(int64_t(cc)));
    out.set("messageCount",Json::number(int64_t(mc)));out.set("triggerCount",Json::number(int64_t(tc)));
    out.set("eligibleContacts",Json::number(int64_t(eligible)));out.set("activeTriggers",Json::number(int64_t(active)));out.set("firedTriggers",Json::number(int64_t(fired)));
    out.set("truncated",Json::fromBool(truncated));out.set("rejected",Json::number(int64_t(rejected)));
    out.set("unplaced",Json::number(int64_t(unplaced)));out.set("crossBody",Json::number(int64_t(crossBody)));
    out.set("omittedContacts",Json::number(int64_t(omitted)));out.set("staleOutputs",Json::number(int64_t(stale)));
    out.set("people",std::move(people));out.set("contacts",std::move(contacts));out.set("messages",std::move(messages));out.set("triggers",std::move(triggers));return out;
}
} // namespace aiverse::studio
