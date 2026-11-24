// Caverns authoring commands shared by native editor, MCP and HTML5.
// Persists only inputs in MapDoc::extra, ensuring Undo/Redo via MapDoc.
#pragma once
#include "apps/editor/core/Workspace.h"
#include "apps/editor/core/TerraWeavePlanetCavernsDoc.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsRaycast.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
namespace aiverse::editor {
inline void registerTerraWeaveCavernsOps(Workspace& ws) {
    const Json schema=Json::parse(R"({
      "properties":{
        "mode":{"type":"string","enum":["carve","fill","erase","get","settings","entrance_add","entrance_update","entrance_erase","planet_region","planet_entrance"]},
        "anchor":{"type":"object"},"depthCm":{"type":"integer"},"xCm":{"type":"integer"},"yCm":{"type":"integer"},"zCm":{"type":"integer"},
        "radiusCm":{"type":"integer","minimum":1,"maximum":1000000},
        "stampId":{"type":"integer","minimum":1},
        "stampIdText":{"type":"string"},
        "expectedRevision":{"type":"string"},
        "withinStroke":{"type":"boolean"},
        "settings":{"type":"object","properties":{"seedText":{"type":"string"},"cellCm":{"type":"integer"},"tunnelRadiusCm":{"type":"integer"},"chamberRadiusCm":{"type":"integer"},"tunnelChance":{"type":"integer"},"chamberChance":{"type":"integer"},"minYcm":{"type":"integer"},"maxYcm":{"type":"integer"}}},
        "entranceIdText":{"type":"string"},
        "surfaceYcm":{"type":"integer"},
        "caveYcm":{"type":"integer"}
      }
    })");
    Workspace* w=&ws;
    struct PickCache {std::string document;std::shared_ptr<const terraweave::caverns::CaveSnapshotSpatialIndex> index;};
    auto cache=std::make_shared<PickCache>();
    ws.registerOp({"terraweave_caverns_pick","Read-only SDF surface raycast in cm and normalized Q20 direction.",
        Json::parse(R"({"properties":{"xCm":{"type":"integer"},"yCm":{"type":"integer"},"zCm":{"type":"integer"},"dxQ20":{"type":"integer"},"dyQ20":{"type":"integer"},"dzQ20":{"type":"integer"}}})"),
        [w,cache](const Json& p)->Json {
            using namespace terraweave::caverns;
            if(w->project.dir.empty())throw std::invalid_argument("Caverns project required");
            const bool planet=w->map.data.planet();
            const auto region=planet?std::optional{planet_caves::read(w->map.data)}:std::nullopt;
            const auto authored=planet?caveSnapshotToJson(region->volume):w->map.data.extra["terraweaveCaverns"];
            const auto text=authored.dump(-1);
            if(!cache->index||cache->document!=text){
                auto snapshot=std::make_shared<const CaveVolumeSnapshot>(caveSnapshotFromJson(authored));
                cache->index=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);cache->document=text;
            }
            const auto field=[&](const char* key,int64_t limit){
                if(!p[key].isNumber())throw std::invalid_argument("Caverns ray coordinate missing");
                const auto n=p.getInt(key);if(n< -limit||n>limit)throw std::out_of_range("Caverns ray coordinate");return n;
            };
            CaveRayCm ray{{field("xCm",kLimitCm),field("yCm",kLimitCm),field("zCm",kLimitCm)},
                int32_t(field("dxQ20",1<<20)),int32_t(field("dyQ20",1<<20)),int32_t(field("dzQ20",1<<20))};
            int64_t distance=50000,step=100;
            if(region){
                // Studio camera can be outside the local chart. Clip the
                // ray to its closed cube before querying the existing SDF.
                const double cap=region->anchor.halfExtentCm-1.;
                double lo=0,hi=50000;
                const double origins[]={double(ray.origin.x),double(ray.origin.y),double(ray.origin.z)};
                const double dirs[]={ray.dxQ20/double(1<<20),ray.dyQ20/double(1<<20),ray.dzQ20/double(1<<20)};
                for(int i=0;i<3;++i){
                    if(std::abs(dirs[i])<1e-9){if(std::abs(origins[i])>cap)hi=-1;continue;}
                    double a=(-cap-origins[i])/dirs[i],b=(cap-origins[i])/dirs[i];
                    if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);
                }
                const auto miss=[](){Json result=Json::object();result.set("hit",Json::fromBool(false));return result;};
                if(hi-lo<2)return miss();
                const auto t=std::ceil(lo);
                ray.origin={int64_t(std::llround(origins[0]+dirs[0]*t)),int64_t(std::llround(origins[1]+dirs[1]*t)),int64_t(std::llround(origins[2]+dirs[2]*t))};
                distance=int64_t(std::floor(hi-t));step=std::clamp<int64_t>(region->anchor.halfExtentCm/64,1,25);
                if(distance<step)return miss();
            }
            const auto hit=raycastCaveSurface(*cache->index,ray,distance,step);
            Json result=Json::object();result.set("hit",Json::fromBool(bool(hit)));
            if(hit){result.set("xCm",Json::number(hit->point.x));result.set("yCm",Json::number(hit->point.y));result.set("zCm",Json::number(hit->point.z));}
            return result;
        }});
    ws.registerOp({"terraweave_caverns_paint",
        "Author carve/fill spherical cave SDF stamps in signed world centimetres. "
        "Use erase with stampId or get to inspect; settings changes the generator recipe, entrance_add/update/erase author shafts. MapDoc persistence, undo/redo, "
        "MCP and HTML5 share this operation. Caller owns runtime chunk invalidation.",
        schema,[w](const Json& p)->Json{
            if(w->project.dir.empty())
                throw std::runtime_error("Caverns needs an open project");
            using namespace terraweave::caverns;
            const bool planet=w->map.data.planet();
            const auto mode=p.getStr("mode","carve");
            if(mode!="carve"&&mode!="fill"&&mode!="erase"&&mode!="get"&&mode!="settings"&&
               mode!="entrance_add"&&mode!="entrance_update"&&mode!="entrance_erase"&&mode!="planet_region"&&mode!="planet_entrance")
                throw std::invalid_argument("Caverns paint mode");
            if(!planet&&(mode=="planet_region"||mode=="planet_entrance"))
                throw std::invalid_argument("Caverns planetary map required");
            const bool hasRegion=planet&&!w->map.data.extra[planet_caves::key].isNull();
            std::optional<planet_caves::Region> region;
            if(hasRegion)region=planet_caves::read(w->map.data);
            if(planet&&!region&&mode!="get"&&mode!="planet_region")
                throw std::invalid_argument("Create a planetary Caverns region first");
            const Json original=planet?(region?caveSnapshotToJson(region->volume):Json{}):w->map.data.extra["terraweaveCaverns"];
            auto volume=caveSnapshotFromJson(original);
            // Browser transports cannot safely represent every int64 ID.
            const auto decimalId=[](const Json& value,bool allowZero=false)->uint64_t {
                if(!value.isString())throw std::invalid_argument("Caverns decimal identity required");
                const auto s=value.str();
                uint64_t n=0;
                const auto parsed=std::from_chars(s.data(),s.data()+s.size(),n);
                if(parsed.ec!=std::errc{}||parsed.ptr!=s.data()+s.size()||
                   (!allowZero&&!n)||n>uint64_t(INT64_MAX))
                    throw std::invalid_argument("Caverns invalid decimal identity");
                return n;
            };
            if(mode!="get"&&p.has("expectedRevision")&&
               decimalId(p["expectedRevision"])!=volume.sourceRevision)
                throw std::invalid_argument("Caverns document revision changed; refresh");
            bool changed=false;
            uint64_t stampId=0;
            std::optional<terraweave::caverns::CaveSphereStamp> affected;
            using namespace terraweave::caverns;
            const auto bounded=[](const Json& input,const char* key,int64_t lo,int64_t hi){
                const auto& v=input[key];
                const double d=v.toDouble();
                if(!v.isNumber()||!std::isfinite(d)||std::trunc(d)!=d||d<double(lo)||d>double(hi))
                    throw std::invalid_argument(std::string("Caverns invalid integer: ")+key);
                return int64_t(d);
            };
            bool fullRebuild=false;
            uint64_t entranceId=0;
            std::optional<CaveDirtyBounds> entranceBounds;
            const auto includeEntrance=[&](const Entrance& e){
                const int64_t r=int64_t(e.radiusCm)+100;
                CaveDirtyBounds b{e.xCm-r,e.caveYcm-r,e.zCm-r,e.xCm+r+1,e.surfaceYcm+r+1,e.zCm+r+1};
                if(!entranceBounds)entranceBounds=b;
                else {auto& a=*entranceBounds;a.minX=std::min(a.minX,b.minX);a.minY=std::min(a.minY,b.minY);a.minZ=std::min(a.minZ,b.minZ);
                    a.maxX=std::max(a.maxX,b.maxX);a.maxY=std::max(a.maxY,b.maxY);a.maxZ=std::max(a.maxZ,b.maxZ);}
            };
            if(mode=="planet_region"){
                if(hasRegion)throw std::invalid_argument("Planetary chart immutable; create a new region in a new document");
                region=planet_caves::create(w->map.data,cavePlanetAnchorFromJson(p["anchor"]));
                volume=region->volume;changed=true;fullRebuild=true;
            }else if(mode=="planet_entrance"){
                if(volume.entrances.size()>=4096)throw std::length_error("Caverns entrance limit");
                const auto frame=planet_caves::frameForDocument(w->map.data,region->anchor);
                const auto patch=planet_caves::captureLocalSurface(w->map.data,region->anchor,w->map.revision);
                entranceId=volume.sourceRevision;
                for(const auto& e:volume.entrances)entranceId=std::max(entranceId,e.id);
                if(entranceId>=uint64_t(INT64_MAX))throw std::overflow_error("Caverns entrance ID");
                ++entranceId;
                const auto e=makeCavePlanetEntrance(frame,patch,entranceId,bounded(p,"xCm",-kLimitCm,kLimitCm),bounded(p,"zCm",-kLimitCm,kLimitCm),int32_t(bounded(p,"depthCm",1,1000000)),int32_t(bounded(p,"radiusCm",1,1000000)));
                volume.entrances.push_back(e);includeEntrance(e);changed=true;
            }else if(mode=="settings"){
                const auto& cfg=p["settings"];
                if(!cfg.isObject())throw std::invalid_argument("Caverns settings object required");
                auto next=volume.settings;
                next.seed=decimalId(cfg["seedText"],true);
                next.cellCm=int32_t(bounded(cfg,"cellCm",100,10000));
                next.tunnelRadiusCm=int32_t(bounded(cfg,"tunnelRadiusCm",1,10000));
                next.chamberRadiusCm=int32_t(bounded(cfg,"chamberRadiusCm",1,10000));
                next.tunnelChance=uint32_t(bounded(cfg,"tunnelChance",0,1000));
                next.chamberChance=uint32_t(bounded(cfg,"chamberChance",0,1000));
                next.minYcm=bounded(cfg,"minYcm",-kLimitCm,kLimitCm);
                next.maxYcm=bounded(cfg,"maxYcm",-kLimitCm,kLimitCm);
                validate(next);
                const auto& old=volume.settings;
                changed=next.seed!=old.seed||next.cellCm!=old.cellCm||next.tunnelRadiusCm!=old.tunnelRadiusCm||
                    next.chamberRadiusCm!=old.chamberRadiusCm||next.tunnelChance!=old.tunnelChance||
                    next.chamberChance!=old.chamberChance||next.minYcm!=old.minYcm||next.maxYcm!=old.maxYcm||original.isNull();
                volume.settings=next;fullRebuild=changed;
            }else if(mode=="entrance_add"||mode=="entrance_update"||mode=="entrance_erase"){
                auto& list=volume.entrances;
                auto it=list.end();
                if(mode!="entrance_add"){
                    entranceId=decimalId(p["entranceIdText"]);
                    it=std::find_if(list.begin(),list.end(),[&](const auto& e){return e.id==entranceId;});
                    if(it==list.end())throw std::out_of_range("Caverns entrance not found");
                    includeEntrance(*it);
                }
                if(mode=="entrance_erase"){list.erase(it);changed=true;}
                else {
                    Entrance e{};
                    e.xCm=bounded(p,"xCm",-kLimitCm,kLimitCm);
                    e.zCm=bounded(p,"zCm",-kLimitCm,kLimitCm);
                    e.surfaceYcm=bounded(p,"surfaceYcm",-kLimitCm,kLimitCm);
                    e.caveYcm=bounded(p,"caveYcm",-kLimitCm,kLimitCm);
                    e.radiusCm=int32_t(bounded(p,"radiusCm",1,1000000));
                    if(e.surfaceYcm<=e.caveYcm)throw std::invalid_argument("Caverns entrance needs positive depth");
                    if(mode=="entrance_add"){
                        if(list.size()>=4096)throw std::length_error("Caverns entrance limit");
                        entranceId=volume.sourceRevision;
                        for(const auto& old:list)entranceId=std::max(entranceId,old.id);
                        if(entranceId>=uint64_t(INT64_MAX))throw std::overflow_error("Caverns entrance ID");
                        ++entranceId;e.id=entranceId;list.push_back(e);changed=true;
                    }else {e.id=entranceId;changed=!(e==*it);*it=e;}
                    includeEntrance(e);
                }
            }else if(mode=="erase"){
                const auto legacy=p.getInt("stampId",0);
                const uint64_t requested=p.has("stampIdText")?decimalId(p["stampIdText"]):
                    (legacy>0?uint64_t(legacy):0);
                if(!requested)throw std::invalid_argument("Caverns erase stamp ID");
                auto it=std::find_if(volume.stamps.begin(),volume.stamps.end(),
                    [requested](const auto& s){return s.id==uint64_t(requested);});
                if(it==volume.stamps.end())
                    throw std::out_of_range("Caverns stamp not found");
                stampId=it->id;
                affected=*it;
                volume.stamps.erase(it);
                changed=true;
            }else if(mode!="get"){
                if(!p["xCm"].isNumber()||!p["yCm"].isNumber()||
                   !p["zCm"].isNumber())
                    throw std::invalid_argument("Caverns brush requires xyz world cm");
                const auto radius=p.getInt("radiusCm",400);
                if(radius<1||radius>1000000)
                    throw std::invalid_argument("Caverns brush radius");
                const auto xyz=terraweave::caverns::PointCm{
                    p.getInt("xCm"),p.getInt("yCm"),p.getInt("zCm")};
                terraweave::caverns::validatePoint(xyz);
                if(volume.stamps.size()>=16384)
                    throw std::length_error("Caverns stamp limit");
                stampId=volume.sourceRevision;
                for(const auto& stamp:volume.stamps)
                    stampId=std::max(stampId,stamp.id);
                if(stampId>=uint64_t(INT64_MAX))
                    throw std::overflow_error("Caverns stamp ID overflow");
                ++stampId;
                volume.stamps.push_back({stampId,xyz,int32_t(radius),
                    mode=="carve"?terraweave::caverns::CaveStampMode::Carve:
                                  terraweave::caverns::CaveStampMode::Fill});
                affected=volume.stamps.back();
                changed=true;
            }
            if(changed){
                if((fullRebuild||entranceId)&&p.getBool("withinStroke",false))throw std::invalid_argument("Caverns recipe/entrance edit requires its own undo step");
                if(volume.sourceRevision>=uint64_t(INT64_MAX))
                    throw std::overflow_error("Caverns revision overflow");
                ++volume.sourceRevision;
                Json updated;
                if(planet){region->volume=volume;updated=planet_caves::encode(w->map.data,*region);}
                else updated=caveSnapshotToJson(volume);
                if(!p.getBool("withinStroke",false))w->map.beginEdit();
                w->map.data.extra.set(planet?planet_caves::key:"terraweaveCaverns",std::move(updated));
                w->map.touched();
                w->note({Change::Map,0,0,-1,-1,-1,
                         "TerraWeave Caverns authored SDF changed"});
            }
            Json result=Json::object();
            result.set("ok",Json::fromBool(true));
            result.set("revision",Json::number(int64_t(volume.sourceRevision)));
            result.set("revisionText",Json::string(std::to_string(volume.sourceRevision)));
            result.set("stampCount",Json::number(int64_t(volume.stamps.size())));
            if(stampId){result.set("stampId",Json::number(int64_t(stampId)));
                result.set("stampIdText",Json::string(std::to_string(stampId)));}
            result.set("changed",Json::fromBool(changed));
            result.set("fullRebuild",Json::fromBool(fullRebuild));
            if(entranceId)result.set("entranceIdText",Json::string(std::to_string(entranceId)));
            if(changed&&(affected||entranceBounds)){
                const auto bounds=affected?dirtyBoundsForStamp(*affected):*entranceBounds;
                Json dirty=Json::object();
                dirty.set("minXcm",Json::number(bounds.minX));
                dirty.set("minYcm",Json::number(bounds.minY));
                dirty.set("minZcm",Json::number(bounds.minZ));
                dirty.set("maxXcm",Json::number(bounds.maxX));
                dirty.set("maxYcm",Json::number(bounds.maxY));
                dirty.set("maxZcm",Json::number(bounds.maxZ));
                result.set("dirtyBounds",std::move(dirty));
            }
            result.set("planetary",Json::fromBool(planet));
            if(planet){result.set("planetBodies",planet_caves::describeBodies(w->map.data));
                if(region)result.set("planetAnchor",cavePlanetAnchorToJson(region->anchor));}
            if(mode=="get"){
                result.set("snapshot",terraweave::caverns::caveSnapshotToJson(volume));
                Json ids=Json::array(),entranceIds=Json::array();
                for(const auto& s:volume.stamps)ids.push(Json::string(std::to_string(s.id)));
                for(const auto& e:volume.entrances)entranceIds.push(Json::string(std::to_string(e.id)));
                result.set("stampIds",std::move(ids));
                result.set("entranceIds",std::move(entranceIds));
                result.set("seedText",Json::string(std::to_string(volume.settings.seed)));
                result.set("generatorVersionText",Json::string(std::to_string(volume.generatorVersion)));
            }
            result.set("summary",Json::string(
                "Caverns: "+std::to_string(volume.stamps.size())+
                " SDF stamps, revision "+std::to_string(volume.sourceRevision)));
            return result;
        }});
}
} // namespace aiverse::editor
