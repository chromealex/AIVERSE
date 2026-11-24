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
// Explicit diagnostic geometry scale, NOT an authoritative terrain/physics
// placement. The user chooses a square cell/depth; the bottom is ground.
inline Json samplePlanetGranular(const World& ecs,const game::Surface& surface,
        uint32_t completedTick,int32_t cellCm,size_t limit=512){
    namespace wp=lib::worldprocess;
    Json out=Json::object(),rows=Json::array();
    limit=std::min(limit,size_t(512));
    const auto total=ecs.countExisting<granular::Field>();
    const bool available=cellCm>=10&&cellCm<=10000&&completedTick!=UINT32_MAX;
    out.set("available",Json::fromBool(available));
    out.set("cellSizeCm",Json::number(int64_t(cellCm)));
    out.set("sampleTick",Json::number(int64_t(completedTick)));
    out.set("fieldCount",Json::number(int64_t(total)));
    bool truncated=total>granular::kMaxFields;
    size_t rejected=0,tiles=0;
    if(available&&!truncated){
        std::vector<Entity> ids;
        ids.reserve(total);
        ecs.eachExistingConst<granular::Field>([&](Entity e,const granular::Field& f){
            if(ecs.alive(e)&&f.enabled)ids.push_back(e);
        });
        std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
        for(const auto e:ids){
            if(truncated)break;
            const auto* field=ecs.tryGetConst<granular::Field>(e);
            const auto* tr=ecs.tryGetConst<Transform>(e);
            if(!field||!tr||!granular::valid(*field)||field->sampledTick!=completedTick){
                ++rejected;continue;
            }
            const auto frame=wp::surfaceFrame(&surface,tr->pos);
            if(!frame){++rejected;continue;}
            const granular::SurfaceFieldPlacement place{tr->pos,cellCm,0};
            const auto anchor=game::Surface::anchor(frame->body);
            bool invalid=false;
            for(unsigned y=0;y<field->height&&!truncated&&!invalid;y+=16)
                for(unsigned x=0;x<field->width&&!truncated&&!invalid;x+=16){
                    const auto w=std::min<unsigned>(16,field->width-x);
                    const auto h=std::min<unsigned>(16,field->height-y);
                    bool occupied=false;
                    for(unsigned r=0;r<h&&!occupied;++r)
                        for(unsigned c=0;c<w;++c)
                            if(field->mass[size_t(y+r)*field->width+x+c]){occupied=true;break;}
                    if(!occupied)continue;
                    if(tiles>=64||rows.size()>=limit){truncated=true;break;}
                    ++tiles;
                    const auto quads=wp::planetGranularTops(ecs,surface,e,place,cellCm,
                        uint16_t(x),uint16_t(y),uint16_t(w),uint16_t(h),completedTick);
                    if(!quads){++rejected;invalid=true;break;}
                    for(const auto& q:*quads){
                        if(rows.size()>=limit){truncated=true;break;}
                        if(q.body!=frame->body){++rejected;invalid=true;break;}
                        const auto mass=field->mass[size_t(q.row)*field->width+q.column];
                        Json row=Json::object(),vertices=Json::array();
                        row.set("id",Json::string(std::to_string(e.packed())));
                        row.set("body",Json::string(surface.bodies[size_t(q.body)].id));
                        row.set("column",Json::number(int64_t(q.column)));
                        row.set("row",Json::number(int64_t(q.row)));
                        row.set("mass",Json::number(int64_t(mass)));
                        row.set("capacity",Json::number(int64_t(field->capacity)));
                        for(const auto& v:q.vertices){
                            const auto local=v-anchor;
                            Json xyz=Json::array();
                            for(auto c:{local.x,local.y,local.z})xyz.push(Json::number(c.toDouble(),4));
                            vertices.push(std::move(xyz));
                        }
                        row.set("vertices",std::move(vertices));
                        rows.push(std::move(row));
                    }
                }
        }
    }
    out.set("truncated",Json::fromBool(truncated));
    out.set("rejected",Json::number(int64_t(rejected)));
    out.set("tiles",Json::number(int64_t(tiles)));
    out.set("cells",std::move(rows));
    return out;
}
} // namespace aiverse::studio
