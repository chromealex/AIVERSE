// ECHO live observations only. Never rerun propagation or infer a physical sound path.
#pragma once
#include "aiverselib/world/EchoEcs.h"
#include "aiverselib/world/WorldProcessSurfaceFrame.h"
#include "engine/game/Components.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {
inline Json samplePlanetEcho(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,size_t zoneLimit=256,size_t portalLimit=512,size_t receiverLimit=256){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),zones=Json::array(),portals=Json::array(),emitters=Json::array(),receivers=Json::array();
    zoneLimit=std::min(zoneLimit,size_t(256));portalLimit=std::min(portalLimit,size_t(512));
    receiverLimit=std::min(receiverLimit,size_t(256));
    const auto zoneCount=ecs.countExisting<echo::AcousticZone>(),portalCount=ecs.countExisting<echo::AcousticPortal>();
    const auto emitterCount=ecs.countExisting<echo::AcousticEmitter>(),receiverCount=ecs.countExisting<echo::AcousticReceiver>();
    const bool capped=zoneCount>echo::kMaxZones||portalCount>echo::kMaxPortals||emitterCount>echo::kMaxEmitters||
        receiverCount>echo::kMaxZones||ecs.countExisting<echo::AcousticHearing>()>echo::kMaxZones;
    const bool available=!capped&&completedTick!=UINT32_MAX&&surface.sphere()&&surface.tab;
    bool truncated=capped;
    size_t rejected=0,unplaced=0,omitted=0,crossBody=0,stale=0,audible=0;
    struct Location {int body=-1;Vec3 local{};};
    const auto locate=[&](Entity e){
        Location result;
        if(!ecs.alive(e)||ecs.has<game::Transit>(e))return result;
        if(const auto* u=ecs.tryGetConst<game::Unit>(e);u&&(u->flags&(game::kUnitOffMap|game::kUnitInTransit)))return result;
        const auto* t=ecs.tryGetConst<Transform>(e);if(!t)return result;
        const auto frame=wp::surfaceFrame(&surface,t->pos);if(!frame)return result;
        return Location{frame->body,t->pos-game::Surface::anchor(frame->body)};
    };
    const auto vec=[](Vec3 v){Json a=Json::array();for(auto c:{v.x,v.y,v.z})a.push(Json::number(c.toDouble(),4));return a;};
    const auto putPosition=[&](Json& row,const Location& p){
        row.set("placed",Json::fromBool(p.body>=0));
        if(p.body<0){++unplaced;return;}
        row.set("body",Json::string(surface.bodies[size_t(p.body)].id));row.set("position",vec(p.local));
    };
    const auto id=[](Entity e){return Json::string(std::to_string(e.packed()));};
    const auto activeZone=[&](Entity e){
        const auto* z=ecs.tryGetConst<echo::AcousticZone>(e);return ecs.alive(e)&&z&&z->enabled;
    };
    if(available){
        const auto ordered=[&]<class C>(){
            std::vector<Entity> ids;ids.reserve(ecs.countExisting<C>());
            ecs.eachExistingConst<C>([&](Entity e,const C&){if(ecs.alive(e))ids.push_back(e);});
            std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
            return ids;
        };
        std::map<uint64_t,Location> selected;
        for(auto e:ordered.template operator()<echo::AcousticZone>()){
            if(zones.size()>=zoneLimit){truncated=true;break;}
            const auto p=locate(e);selected.emplace(e.packed(),p);
            Json row=Json::object();row.set("id",id(e));row.set("enabled",Json::fromBool(activeZone(e)));
            putPosition(row,p);zones.push(std::move(row));
        }
        for(auto e:ordered.template operator()<echo::AcousticPortal>()){
            const auto& p=*ecs.tryGetConst<echo::AcousticPortal>(e);
            if(p.loss<0||p.loss>echo::kMaxIntensity){++rejected;continue;}
            const auto a=selected.find(p.from.packed()),b=selected.find(p.to.packed());
            if(a==selected.end()||b==selected.end()){++omitted;continue;}
            if(portals.size()>=portalLimit){truncated=true;break;}
            Json row=Json::object();row.set("id",id(e));row.set("from",id(p.from));row.set("to",id(p.to));
            row.set("enabled",Json::fromBool(p.enabled));row.set("bidirectional",Json::fromBool(p.bidirectional));
            row.set("loss",Json::number(p.loss));row.set("active",Json::fromBool(p.enabled&&activeZone(p.from)&&activeZone(p.to)));
            const auto& x=a->second;const auto& y=b->second;
            const bool placed=x.body>=0&&x.body==y.body;
            if(x.body>=0&&y.body>=0&&x.body!=y.body)++crossBody;
            row.set("placed",Json::fromBool(placed));
            if(placed){row.set("body",Json::string(surface.bodies[size_t(x.body)].id));row.set("a",vec(x.local));row.set("b",vec(y.local));}
            portals.push(std::move(row));
        }
        for(auto e:ordered.template operator()<echo::AcousticEmitter>()){
            const auto& p=*ecs.tryGetConst<echo::AcousticEmitter>(e);
            if(p.intensity<0||p.intensity>echo::kMaxIntensity){++rejected;continue;}
            Json row=Json::object();row.set("id",id(e));row.set("zone",id(p.zone));row.set("intensity",Json::number(p.intensity));
            row.set("enabled",Json::fromBool(p.enabled));row.set("active",Json::fromBool(p.enabled&&p.intensity>0&&activeZone(p.zone)));
            putPosition(row,locate(e));emitters.push(std::move(row));
        }
        for(auto e:ordered.template operator()<echo::AcousticReceiver>()){
            const auto& p=*ecs.tryGetConst<echo::AcousticReceiver>(e);
            if(p.threshold<0||p.threshold>echo::kMaxIntensity){++rejected;continue;}
            if(receivers.size()>=receiverLimit){truncated=true;break;}
            Json row=Json::object();row.set("id",id(e));row.set("zone",id(p.zone));row.set("threshold",Json::number(p.threshold));
            row.set("enabled",Json::fromBool(p.enabled));row.set("zoneActive",Json::fromBool(activeZone(p.zone)));
            const auto position=locate(e);putPosition(row,position);
            const auto* h=ecs.tryGetConst<echo::AcousticHearing>(e);
            const bool current=h&&h->sampledTick==completedTick&&h->intensity>=-1&&h->intensity<=echo::kMaxIntensity;
            row.set("hearingAvailable",Json::fromBool(current));
            if(!current)++stale;
            else {
                row.set("valid",Json::fromBool(h->valid));row.set("audible",Json::fromBool(h->audible));row.set("intensity",Json::number(h->intensity));
                if(h->valid&&h->audible){
                    ++audible;row.set("source",id(h->source));
                    // The reported winner is not an acoustic route. Only draw a
                    // relationship when the generation-safe source still exists.
                    if(ecs.alive(h->source)&&ecs.has<echo::AcousticEmitter>(h->source)){
                        const auto src=locate(h->source);
                        if(position.body>=0&&position.body==src.body)row.set("sourcePosition",vec(src.local));
                        else if(position.body>=0&&src.body>=0)++crossBody;
                    }
                }
            }
            receivers.push(std::move(row));
        }
    }
    out.set("available",Json::fromBool(available));out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("zoneCount",Json::number(int64_t(zoneCount)));out.set("portalCount",Json::number(int64_t(portalCount)));
    out.set("emitterCount",Json::number(int64_t(emitterCount)));out.set("receiverCount",Json::number(int64_t(receiverCount)));
    out.set("truncated",Json::fromBool(truncated));out.set("rejected",Json::number(int64_t(rejected)));
    out.set("unplaced",Json::number(int64_t(unplaced)));out.set("omittedPortals",Json::number(int64_t(omitted)));
    out.set("crossBody",Json::number(int64_t(crossBody)));out.set("staleHearing",Json::number(int64_t(stale)));
    out.set("audible",Json::number(int64_t(audible)));
    out.set("zones",std::move(zones));out.set("portals",std::move(portals));out.set("emitters",std::move(emitters));out.set("receivers",std::move(receivers));
    return out;
}
} // namespace aiverse::studio
