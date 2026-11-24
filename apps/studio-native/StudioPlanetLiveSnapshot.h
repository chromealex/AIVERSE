// Local Play diagnostics only. Read the existing ECS at a completed tick;
// no independent simulation, geometry owner, or persistent state.
#pragma once
#include "aiverselib/world/PlanetProcessGeometry.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <string>
#include <vector>

namespace aiverse::studio {
inline Json samplePlanetImprint(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,size_t limit=1024){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),rows=Json::array();
    limit=std::min(limit,size_t(1024));
    const auto total=ecs.countExisting<imprint::Cell>();
    out.set("cellCount",Json::number(int64_t(total)));
    out.set("sampleTick",Json::number(int64_t(completedTick)));
    bool truncated=total>imprint::kMaxCells;
    size_t rejected=0;
    if(!truncated&&completedTick!=UINT32_MAX){
        std::vector<Entity> ids;
        ids.reserve(total);
        ecs.eachExistingConst<imprint::Cell>([&](Entity e,const imprint::Cell& c){
            if(ecs.alive(e)&&c.enabled&&(c.footprint||c.scent||c.residue))ids.push_back(e);
        });
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(const auto e:ids){
            if(rows.size()>=limit){truncated=true;break;}
            const auto q=wp::planetImprintQuad(ecs,surface,e,completedTick);
            if(!q){++rejected;continue;}
            const auto& g=q->geometry;
            if(g.body<0||size_t(g.body)>=surface.bodies.size()){++rejected;continue;}
            const auto anchor=game::Surface::anchor(g.body);
            Json row=Json::object(),vertices=Json::array();
            row.set("id",Json::string(std::to_string(e.packed())));
            row.set("body",Json::string(surface.bodies[size_t(g.body)].id));
            row.set("footprint",Json::number(q->footprint));
            row.set("scent",Json::number(q->scent));
            row.set("residue",Json::number(q->residue));
            for(const auto& v:g.vertices){
                const auto local=v-anchor;
                Json xyz=Json::array();
                for(auto c:{local.x,local.y,local.z})xyz.push(Json::number(c.toDouble(),4));
                vertices.push(std::move(xyz));
            }
            row.set("vertices",std::move(vertices));
            rows.push(std::move(row));
        }
    }
    out.set("truncated",Json::fromBool(truncated));
    out.set("rejected",Json::number(int64_t(rejected)));
    out.set("cells",std::move(rows));
    return out;
}
} // namespace aiverse::studio
