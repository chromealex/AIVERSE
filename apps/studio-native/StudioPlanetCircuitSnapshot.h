// Read-only CIRCUIT topology and committed outputs. No power allocation,
// logic evaluation, switch writes, device actuation or inferred wire current.
#pragma once
#include "aiverselib/world/CircuitDevices.h"
#include "aiverselib/world/WorldProcessSurfaceFrame.h"
#include "engine/game/Components.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {
inline Json samplePlanetCircuit(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,size_t nodeLimit=256,size_t wireLimit=512,size_t auxiliaryLimit=128){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),nodes=Json::array(),wires=Json::array(),gates=Json::array(),switches=Json::array(),devices=Json::array();
    nodeLimit=std::min(nodeLimit,size_t(256));wireLimit=std::min(wireLimit,size_t(512));
    auxiliaryLimit=std::min(auxiliaryLimit,size_t(128));
    const auto nc=ecs.countExisting<circuit::Node>(),wc=ecs.countExisting<circuit::Wire>();
    const auto gc=ecs.countExisting<circuit::Gate>(),sc=ecs.countExisting<circuit::Switch>(),dc=ecs.countExisting<circuit::Device>();
    const bool capped=nc>circuit::kMaxNodes||wc>circuit::kMaxWires||gc>circuit::kMaxGates||
        sc>circuit::kMaxSwitches||dc>circuit::kMaxDevices;
    const bool available=!capped&&completedTick!=UINT32_MAX&&surface.sphere()&&surface.tab;
    bool truncated=capped;
    size_t rejected=0,unplaced=0,crossBody=0,omitted=0,stale=0,powered=0,high=0,activeDevices=0;
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
    const auto putPosition=[&](Json& row,const Location& p){
        row.set("placed",Json::fromBool(p.body>=0));
        if(p.body<0){++unplaced;return;}
        row.set("body",Json::string(surface.bodies[size_t(p.body)].id));row.set("position",vec(p.local));
    };
    const auto node=[&](Entity e)->const circuit::Node*{
        if(!ecs.alive(e))return nullptr;
        const auto* n=ecs.tryGetConst<circuit::Node>(e);
        return n&&circuit::valid(*n)?n:nullptr;
    };
    const auto active=[&](Entity e){const auto* n=node(e);return n&&n->enabled;};
    const auto current=[&](const circuit::Node* n){return n&&n->sampledTick==completedTick;};
    if(available){
        const auto ordered=[&]<class C>(){
            std::vector<Entity> ids;ids.reserve(ecs.countExisting<C>());
            ecs.eachExistingConst<C>([&](Entity e,const C&){if(ecs.alive(e))ids.push_back(e);});
            std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});return ids;
        };
        std::map<uint64_t,Location> selected;
        for(auto e:ordered.template operator()<circuit::Node>()){
            const auto* n=node(e);if(!n){++rejected;continue;}
            if(nodes.size()>=nodeLimit){truncated=true;break;}
            const auto p=locate(e);selected.emplace(e.packed(),p);
            Json row=Json::object();row.set("id",id(e));putPosition(row,p);
            row.set("enabled",Json::fromBool(n->enabled));row.set("supply",Json::number(n->supply));row.set("demand",Json::number(n->demand));
            row.set("drive",Json::fromBool(n->drive));row.set("outputAvailable",Json::fromBool(current(n)));
            if(current(n)){
                row.set("powered",Json::fromBool(n->powered));row.set("signal",Json::fromBool(n->signal));
                if(n->powered)++powered;
                if(n->signal)++high;
            }else ++stale;
            nodes.push(std::move(row));
        }
        for(auto e:ordered.template operator()<circuit::Wire>()){
            const auto& w=*ecs.tryGetConst<circuit::Wire>(e);
            if(w.from==w.to){++rejected;continue;}
            const auto a=selected.find(w.from.packed()),b=selected.find(w.to.packed());
            if(a==selected.end()||b==selected.end()){++omitted;continue;}
            if(wires.size()>=wireLimit){truncated=true;break;}
            const auto& x=a->second;const auto& y=b->second;
            Json row=Json::object();row.set("id",id(e));row.set("from",id(w.from));row.set("to",id(w.to));
            row.set("enabled",Json::fromBool(w.enabled));row.set("bidirectional",Json::fromBool(w.bidirectional));
            row.set("active",Json::fromBool(w.enabled&&active(w.from)&&active(w.to)));
            const auto* na=node(w.from);const auto* nb=node(w.to);
            const bool known=current(na)&&current(nb);
            row.set("outputsAvailable",Json::fromBool(known));
            if(known)row.set("bothPowered",Json::fromBool(na->powered&&nb->powered));
            const bool placed=x.body>=0&&x.body==y.body;
            if(x.body>=0&&y.body>=0&&x.body!=y.body)++crossBody;
            row.set("placed",Json::fromBool(placed));
            if(placed){row.set("body",Json::string(surface.bodies[size_t(x.body)].id));row.set("a",vec(x.local));row.set("b",vec(y.local));}
            wires.push(std::move(row));
        }
        const auto putBinding=[&](Json& row,const Location& origin,Entity target,const char* key){
            const auto p=locate(target);
            if(origin.body>=0&&p.body==origin.body)row.set(key,vec(p.local));
            else if(origin.body>=0&&p.body>=0)++crossBody;
        };
        for(auto e:ordered.template operator()<circuit::Gate>()){
            const auto& g=*ecs.tryGetConst<circuit::Gate>(e);
            if(unsigned(g.operation)>3){++rejected;continue;}
            if(gates.size()>=auxiliaryLimit){truncated=true;break;}
            const auto p=locate(e);
            Json row=Json::object();row.set("id",id(e));putPosition(row,p);
            const char* ops[]={"AND","OR","XOR","NOT"};
            row.set("operation",Json::string(ops[unsigned(g.operation)]));row.set("enabled",Json::fromBool(g.enabled));
            row.set("inputA",id(g.inputA));if(g.operation!=circuit::Op::Not)row.set("inputB",id(g.inputB));
            row.set("output",id(g.output));
            row.set("inputsValid",Json::fromBool(active(g.inputA)&&(g.operation==circuit::Op::Not||active(g.inputB))));
            const auto* n=node(g.output);
            row.set("outputAvailable",Json::fromBool(current(n)));
            if(current(n))row.set("outputSignal",Json::fromBool(n->signal));
            if(active(g.inputA))putBinding(row,p,g.inputA,"inputAPosition");
            if(g.operation!=circuit::Op::Not&&active(g.inputB))putBinding(row,p,g.inputB,"inputBPosition");
            if(n)putBinding(row,p,g.output,"outputPosition");
            gates.push(std::move(row));
        }
        for(auto e:ordered.template operator()<circuit::Switch>()){
            const auto& sw=*ecs.tryGetConst<circuit::Switch>(e);
            if(switches.size()>=auxiliaryLimit){truncated=true;break;}
            const auto p=locate(e);
            Json row=Json::object();row.set("id",id(e));putPosition(row,p);
            row.set("node",id(sw.node));row.set("closed",Json::fromBool(sw.closed));row.set("enabled",Json::fromBool(sw.enabled));
            row.set("nodeValid",Json::fromBool(node(sw.node)!=nullptr));
            if(node(sw.node))putBinding(row,p,sw.node,"nodePosition");
            switches.push(std::move(row));
        }
        for(auto e:ordered.template operator()<circuit::Device>()){
            const auto& d=*ecs.tryGetConst<circuit::Device>(e);
            if(!circuit::valid(d)){++rejected;continue;}
            if(devices.size()>=auxiliaryLimit){truncated=true;break;}
            const auto p=locate(e);
            Json row=Json::object();row.set("id",id(e));putPosition(row,p);
            const char* kinds[]={"generic","light","door","motor","pump","alarm"};
            row.set("kind",Json::string(kinds[unsigned(d.kind)]));row.set("node",id(d.node));
            row.set("enabled",Json::fromBool(d.enabled));row.set("requireSignal",Json::fromBool(d.requireSignal));
            const bool known=d.sampledTick==completedTick;
            row.set("outputAvailable",Json::fromBool(known));
            if(known){
                row.set("valid",Json::fromBool(d.valid));row.set("active",Json::fromBool(d.active));
                row.set("transition",Json::number(int64_t(d.transition)));
                if(d.changedTick!=UINT32_MAX)row.set("changedTick",Json::number(int64_t(d.changedTick)));
                if(d.active)++activeDevices;
            }else ++stale;
            if(node(d.node))putBinding(row,p,d.node,"nodePosition");
            devices.push(std::move(row));
        }
    }
    out.set("available",Json::fromBool(available));out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("nodeCount",Json::number(int64_t(nc)));out.set("wireCount",Json::number(int64_t(wc)));
    out.set("gateCount",Json::number(int64_t(gc)));out.set("switchCount",Json::number(int64_t(sc)));out.set("deviceCount",Json::number(int64_t(dc)));
    out.set("powered",Json::number(int64_t(powered)));out.set("high",Json::number(int64_t(high)));out.set("activeDevices",Json::number(int64_t(activeDevices)));
    out.set("truncated",Json::fromBool(truncated));out.set("rejected",Json::number(int64_t(rejected)));
    out.set("unplaced",Json::number(int64_t(unplaced)));out.set("crossBody",Json::number(int64_t(crossBody)));
    out.set("omittedWires",Json::number(int64_t(omitted)));out.set("staleOutputs",Json::number(int64_t(stale)));
    out.set("nodes",std::move(nodes));out.set("wires",std::move(wires));out.set("gates",std::move(gates));out.set("switches",std::move(switches));out.set("devices",std::move(devices));return out;
}
} // namespace aiverse::studio
