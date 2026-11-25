// Read-only MORPH diagnostics: quantized values remain authoritative in ECS.
// Only explicit owner frames and units permit world-space geometry.
#pragma once
#include "aiverselib/world/MorphPhysisGravity.h"
#include "engine/game/Components.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace aiverse::studio {
inline Json samplePlanetMorph(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,bool gravityInstalled,size_t nodeLimit=256,size_t springLimit=512){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),nodes=Json::array(),springs=Json::array();
    nodeLimit=std::min(nodeLimit,size_t(256));springLimit=std::min(springLimit,size_t(512));
    const auto nodeCount=ecs.countExisting<morph::Node>();
    const auto springCount=ecs.countExisting<morph::Spring>();
    const auto forceCount=ecs.countExisting<morph::Force>();
    const bool capped=nodeCount>morph::kMaxNodes||springCount>morph::kMaxSprings||
        forceCount>morph::kMaxForces||ecs.countExisting<morph::Impact>()>morph::kMaxNodes||
        ecs.countExisting<morph::GravityBinding>()>morph::kMaxNodes;
    const bool available=!capped&&completedTick!=UINT32_MAX&&surface.sphere()&&surface.tab;
    const bool gravityCapped=gravityInstalled&&ecs.countExisting<physis::GravityField>()>1024;
    bool truncated=capped;
    size_t rejected=0,unprojected=0,omitted=0,frameMismatch=0,crossBody=0;
    struct Row {
        morph::Node value;
        int body=-1;Entity owner=kNullEntity;int32_t units=0;
        Vec3 current{},rest{};
        std::array<morph::I,3> force{};
    };
    std::map<uint64_t,Row> selected;
    const auto integers=[](std::array<morph::I,3> v){
        Json a=Json::array();for(auto c:v)a.push(Json::number(c));return a;
    };
    const auto vec=[](Vec3 v){
        Json a=Json::array();for(auto c:{v.x,v.y,v.z})a.push(Json::number(c.toDouble(),4));return a;
    };
    if(available){
        std::vector<Entity> ids;ids.reserve(nodeCount);
        ecs.eachExistingConst<morph::Node>([&](Entity e,const morph::Node&){if(ecs.alive(e))ids.push_back(e);});
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(auto e:ids){
            const auto* n=ecs.tryGetConst<morph::Node>(e);
            if(!n||!morph::valid(*n)||n->sampledTick!=completedTick){++rejected;continue;}
            if(selected.size()>=nodeLimit){truncated=true;break;}
            Row row{*n};
            const auto* binding=ecs.tryGetConst<morph::GravityBinding>(e);
            if(binding&&morph::valid(*binding)&&ecs.alive(binding->frameOwner)){
                const auto owner=binding->frameOwner;
                const auto* u=ecs.tryGetConst<game::Unit>(owner);
                if(!ecs.has<game::Transit>(owner)&&!(u&&(u->flags&(game::kUnitOffMap|game::kUnitInTransit)))){
                    const auto frame=morph::gravity_detail::ownerFrame(ecs,&surface,owner);
                    if(frame){
                        const auto at=[&](morph::I x,morph::I y,morph::I z){
                            return frame->origin+frame->simulationVector({Fixed::fromRatio(x,binding->unitsPerMetre),
                                Fixed::fromRatio(y,binding->unitsPerMetre),Fixed::fromRatio(z,binding->unitsPerMetre)});
                        };
                        const auto current=at(n->x,n->y,n->z),rest=at(n->restX,n->restY,n->restZ);
                        // Reject excessive/unresolved offsets rather than draw across bodies.
                        if(morph::gravity_detail::boundedPosition(current)&&morph::gravity_detail::boundedPosition(rest)){
                            const auto a=wp::surfaceFrame(&surface,current),b=wp::surfaceFrame(&surface,rest);
                            if(a&&b&&a->body==frame->body&&b->body==frame->body){
                                row.body=frame->body;row.owner=owner;row.units=binding->unitsPerMetre;
                                row.current=current;row.rest=rest;
                            }
                        }
                    }
                }
            }
            if(row.body<0)++unprojected;
            selected.emplace(e.packed(),std::move(row));
        }
        // Raw configured enabled force sum; not the solver's saturated impulse.
        ecs.eachExistingConst<morph::Force>([&](Entity e,const morph::Force& f){
            if(!ecs.alive(e)||!f.enabled)return;
            if(!morph::valid(f)){++rejected;return;}
            const auto it=selected.find(f.node.packed());if(it==selected.end())return;
            it->second.force[0]+=f.x;it->second.force[1]+=f.y;it->second.force[2]+=f.z;
        });
        for(const auto& [id,row]:selected){
            const auto e=Entity::fromPacked(id);const auto& n=row.value;
            Json j=Json::object();
            j.set("id",Json::string(std::to_string(id)));j.set("enabled",Json::fromBool(n.enabled));j.set("anchored",Json::fromBool(n.anchored));
            j.set("current",integers({n.x,n.y,n.z}));j.set("rest",integers({n.restX,n.restY,n.restZ}));
            j.set("displacement",integers({n.x-n.restX,n.y-n.restY,n.z-n.restZ}));
            j.set("velocity",integers({n.vx,n.vy,n.vz}));j.set("configuredForce",integers(row.force));
            j.set("projected",Json::fromBool(row.body>=0));
            if(row.body>=0){
                const auto anchor=game::Surface::anchor(row.body);
                j.set("body",Json::string(surface.bodies[size_t(row.body)].id));
                j.set("owner",Json::string(std::to_string(row.owner.packed())));
                j.set("unitsPerMetre",Json::number(int64_t(row.units)));
                j.set("position",vec(row.current-anchor));j.set("restPosition",vec(row.rest-anchor));
            }
            if(const auto* binding=ecs.tryGetConst<morph::GravityBinding>(e)){
                Json b=Json::object();
                b.set("owner",Json::string(std::to_string(binding->frameOwner.packed())));
                b.set("unitsPerMetre",Json::number(int64_t(binding->unitsPerMetre)));
                b.set("impulseMilli",Json::number(int64_t(binding->impulseMilli)));
                b.set("relative",Json::fromBool(binding->relative));b.set("enabled",Json::fromBool(binding->enabled));
                j.set("binding",std::move(b));
                if(gravityInstalled&&!gravityCapped&&n.enabled&&!n.anchored&&row.body>=0)
                    if(const auto load=morph::physisNodeLoad(ecs,&surface,e,n))j.set("currentGravityLoad",integers(*load));
            }
            if(const auto* impact=ecs.tryGetConst<morph::Impact>(e);impact&&morph::valid(*impact)&&impact->pending){
                Json i=Json::object();i.set("tick",Json::number(int64_t(impact->applyTick)));
                i.set("impulse",integers({impact->x,impact->y,impact->z}));j.set("pendingImpact",std::move(i));
            }
            nodes.push(std::move(j));
        }
        ids.clear();ids.reserve(springCount);
        ecs.eachExistingConst<morph::Spring>([&](Entity e,const morph::Spring&){if(ecs.alive(e))ids.push_back(e);});
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(auto e:ids){
            const auto* s=ecs.tryGetConst<morph::Spring>(e);
            if(!s||!morph::valid(*s)||s->a==s->b){++rejected;continue;}
            const auto a=selected.find(s->a.packed()),b=selected.find(s->b.packed());
            if(a==selected.end()||b==selected.end()){++omitted;continue;}
            if(springs.size()>=springLimit){truncated=true;break;}
            const auto& x=a->second;const auto& y=b->second;
            const auto& n=x.value;const auto& m=y.value;
            const bool projected=x.body>=0&&x.body==y.body&&x.owner==y.owner&&x.units==y.units;
            if(x.body>=0&&y.body>=0&&x.body!=y.body)++crossBody;
            else if(!projected)++frameMismatch;
            Json j=Json::object();
            j.set("id",Json::string(std::to_string(e.packed())));
            j.set("first",Json::string(std::to_string(a->first)));j.set("second",Json::string(std::to_string(b->first)));
            j.set("enabled",Json::fromBool(s->enabled));j.set("stiffness",Json::number(s->stiffness));
            j.set("residual",integers({m.x-n.x-s->restDx,m.y-n.y-s->restDy,m.z-n.z-s->restDz}));
            j.set("projected",Json::fromBool(projected));
            if(projected){
                const auto anchor=game::Surface::anchor(x.body);
                j.set("body",Json::string(surface.bodies[size_t(x.body)].id));
                j.set("a",vec(x.current-anchor));j.set("b",vec(y.current-anchor));
            }
            springs.push(std::move(j));
        }
    }
    out.set("available",Json::fromBool(available));out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("nodeCount",Json::number(int64_t(nodeCount)));out.set("springCount",Json::number(int64_t(springCount)));
    out.set("forceCount",Json::number(int64_t(forceCount)));
    out.set("gravityInstalled",Json::fromBool(gravityInstalled));out.set("gravityCapped",Json::fromBool(gravityCapped));
    out.set("truncated",Json::fromBool(truncated));out.set("rejected",Json::number(int64_t(rejected)));
    out.set("unprojected",Json::number(int64_t(unprojected)));out.set("omittedSprings",Json::number(int64_t(omitted)));
    out.set("frameMismatch",Json::number(int64_t(frameMismatch)));out.set("crossBody",Json::number(int64_t(crossBody)));
    out.set("nodes",std::move(nodes));out.set("springs",std::move(springs));
    return out;
}
} // namespace aiverse::studio
