// Actual completed-tick AETHER masses and explicitly advisory duct potentials.
// This is a presentation query, not a transport pass or historical flux log.
#pragma once
#include "aiverselib/world/AetherPhysisStratification.h"
#include "engine/game/Components.h"
#include "engine/data/Json.h"
#include <map>
#include <algorithm>
#include <string>
#include <vector>

namespace aiverse::studio {
enum class AetherDiagnosticPolicy { Unknown, Diffusion, Stratified };
inline Json samplePlanetAether(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,AetherDiagnosticPolicy policy,size_t cellLimit=256,size_t ductLimit=512){
    namespace wp=lib::worldprocess;
    namespace sd=aether::stratification_detail;
    Json out=Json::object(),cells=Json::array(),ducts=Json::array();
    cellLimit=std::min(cellLimit,size_t(256));ductLimit=std::min(ductLimit,size_t(512));
    const auto cellCount=ecs.countExisting<aether::GasCell>();
    const auto ductCount=ecs.countExisting<aether::GasDuct>();
    const auto fluxCount=ecs.countExisting<aether::GasFlux>();
    const bool capped=cellCount>aether::kMaxCells||ductCount>aether::kMaxDucts||fluxCount>aether::kMaxFluxes;
    bool truncated=capped;
    const bool available=!capped&&completedTick!=UINT32_MAX&&surface.sphere()&&surface.tab;
    const bool gravityCapped=policy==AetherDiagnosticPolicy::Stratified&&ecs.countExisting<physis::GravityField>()>1024;
    const bool potential=policy!=AetherDiagnosticPolicy::Unknown&&!gravityCapped;
    size_t rejected=0,crossBody=0,omitted=0;
    struct Row { int body;Vec3 position;std::array<aether::Mass,3> mass;std::optional<sd::Sample> strat;std::array<aether::Mass,3> source{}; };
    std::map<uint64_t,Row> selected;
    const auto vec=[](Vec3 v){
        Json a=Json::array();for(auto c:{v.x,v.y,v.z})a.push(Json::number(c.toDouble(),4));return a;
    };
    const auto numbers=[](const std::array<aether::Mass,3>& v){
        Json a=Json::array();for(auto c:v)a.push(Json::number(c));return a;
    };
    if(available){
        std::vector<Entity> ids;
        ids.reserve(cellCount);
        ecs.eachExistingConst<aether::GasCell>([&](Entity e,const aether::GasCell& c){
            if(ecs.alive(e)&&c.enabled)ids.push_back(e);
        });
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(auto e:ids){
            const auto* c=ecs.tryGetConst<aether::GasCell>(e);
            const auto* t=ecs.tryGetConst<Transform>(e);
            const auto* u=ecs.tryGetConst<game::Unit>(e);
            if(!c||!t||!aether::valid(*c)||c->sampledTick!=completedTick||
               ecs.has<game::Transit>(e)||(u&&(u->flags&(game::kUnitOffMap|game::kUnitInTransit)))){
                ++rejected;continue;
            }
            const auto frame=wp::surfaceFrame(&surface,t->pos);
            if(!frame){++rejected;continue;}
            if(selected.size()>=cellLimit){truncated=true;break;}
            Row row{frame->body,t->pos,{c->smoke,c->toxin,c->oxygen},{}};
            if(potential&&policy==AetherDiagnosticPolicy::Stratified)row.strat=sd::sample(ecs,&surface,e);
            selected.emplace(e.packed(),std::move(row));
        }
        // Configured signed source/sink rates are not a claim of realized flow.
        ecs.eachExistingConst<aether::GasFlux>([&](Entity e,const aether::GasFlux& f){
            if(!ecs.alive(e)||!f.enabled)return;
            if(!aether::valid(f)){++rejected;return;}
            const auto it=selected.find(f.cell.packed());
            if(it==selected.end())return;
            it->second.source[0]+=f.smoke;it->second.source[1]+=f.toxin;it->second.source[2]+=f.oxygen;
        });
        for(const auto& [id,c]:selected){
            Json row=Json::object();
            row.set("id",Json::string(std::to_string(id)));
            row.set("body",Json::string(surface.bodies[size_t(c.body)].id));
            row.set("position",vec(c.position-game::Surface::anchor(c.body)));
            row.set("mass",numbers(c.mass));row.set("sourceRate",numbers(c.source));
            cells.push(std::move(row));
        }
        ids.clear();ids.reserve(ductCount);
        ecs.eachExistingConst<aether::GasDuct>([&](Entity e,const aether::GasDuct& d){
            if(ecs.alive(e)&&d.enabled)ids.push_back(e);
        });
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(auto e:ids){
            const auto* d=ecs.tryGetConst<aether::GasDuct>(e);
            if(!d||!aether::valid(*d)||d->from==d->to){++rejected;continue;}
            const auto from=selected.find(d->from.packed()),to=selected.find(d->to.packed());
            if(from==selected.end()||to==selected.end()){++omitted;continue;}
            const auto& a=from->second;const auto& b=to->second;
            if(a.body!=b.body){++crossBody;continue;}
            if(ducts.size()>=ductLimit){truncated=true;break;}
            const bool known=potential&&(policy!=AetherDiagnosticPolicy::Stratified||(a.strat&&b.strat));
            std::array<aether::Mass,3> head{},bias{},candidate{};
            if(known)for(size_t i=0;i<3;++i){
                const auto raw=a.mass[i]-b.mass[i];
                head[i]=policy==AetherDiagnosticPolicy::Stratified?
                    sd::effectiveHead(i,*a.strat,*b.strat,a.mass[i],b.mass[i],from->first<to->first,{}):raw;
                bias[i]=head[i]-raw;
                // Isolated duct potential only, before competing donors,
                // other ducts, capacity validation and sources in the next tick.
                if(head[i]>1)candidate[i]=std::min({head[i]/2,d->maximumPerTick,a.mass[i]});
                else if(head[i]<-1&&!d->oneWay)candidate[i]=-std::min({-head[i]/2,d->maximumPerTick,b.mass[i]});
            }
            const auto anchor=game::Surface::anchor(a.body);
            Json row=Json::object();
            row.set("id",Json::string(std::to_string(e.packed())));
            row.set("body",Json::string(surface.bodies[size_t(a.body)].id));
            row.set("from",Json::string(std::to_string(from->first)));
            row.set("to",Json::string(std::to_string(to->first)));
            row.set("a",vec(a.position-anchor));row.set("b",vec(b.position-anchor));
            row.set("oneWay",Json::fromBool(d->oneWay));
            row.set("cap",Json::number(d->maximumPerTick));
            row.set("potentialAvailable",Json::fromBool(known));
            if(known){row.set("head",numbers(head));row.set("bias",numbers(bias));row.set("potential",numbers(candidate));}
            ducts.push(std::move(row));
        }
    }
    out.set("available",Json::fromBool(available));
    out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("policy",Json::string(policy==AetherDiagnosticPolicy::Stratified?"stratified":policy==AetherDiagnosticPolicy::Diffusion?"diffusion":"unknown"));
    out.set("gravityCapped",Json::fromBool(gravityCapped));
    out.set("cellCount",Json::number(int64_t(cellCount)));out.set("ductCount",Json::number(int64_t(ductCount)));
    out.set("fluxCount",Json::number(int64_t(fluxCount)));
    out.set("truncated",Json::fromBool(truncated));out.set("rejected",Json::number(int64_t(rejected)));
    out.set("crossBody",Json::number(int64_t(crossBody)));out.set("omittedDucts",Json::number(int64_t(omitted)));
    out.set("cells",std::move(cells));out.set("ducts",std::move(ducts));
    return out;
}
} // namespace aiverse::studio
