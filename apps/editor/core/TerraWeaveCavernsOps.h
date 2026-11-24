// Caverns authoring commands shared by native editor, MCP and HTML5.
// Persists only inputs in MapDoc::extra, ensuring Undo/Redo via MapDoc.
#pragma once
#include "apps/editor/core/Workspace.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsRaycast.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
namespace aiverse::editor {
inline void registerTerraWeaveCavernsOps(Workspace& ws) {
    const Json schema=Json::parse(R"({
      "properties":{
        "mode":{"type":"string","enum":["carve","fill","erase","get"]},
        "xCm":{"type":"integer"},"yCm":{"type":"integer"},"zCm":{"type":"integer"},
        "radiusCm":{"type":"integer","minimum":1,"maximum":1000000},
        "stampId":{"type":"integer","minimum":1},
        "stampIdText":{"type":"string"},
        "expectedRevision":{"type":"string"},
        "withinStroke":{"type":"boolean"}
      }
    })");
    Workspace* w=&ws;
    ws.registerOp({"terraweave_caverns_paint",
        "Author carve/fill spherical cave SDF stamps in signed world centimetres. "
        "Use erase with stampId or get to inspect. MapDoc persistence, undo/redo, "
        "MCP and HTML5 share this operation. Caller owns runtime chunk invalidation.",
        schema,[w](const Json& p)->Json{
            if(w->project.dir.empty())
                throw std::runtime_error("Caverns needs an open project");
            if(w->map.data.planet())
                throw std::invalid_argument("Caverns flat-world volume only");
            const auto mode=p.getStr("mode","carve");
            if(mode!="carve"&&mode!="fill"&&mode!="erase"&&mode!="get")
                throw std::invalid_argument("Caverns paint mode");
            const Json& original=w->map.data.extra["terraweaveCaverns"];
            auto volume=terraweave::caverns::caveSnapshotFromJson(original);
            // Browser transports cannot safely represent every int64 ID.
            const auto decimalId=[](const Json& value)->uint64_t {
                if(!value.isString())throw std::invalid_argument("Caverns decimal identity required");
                const auto s=value.str();
                uint64_t n=0;
                const auto parsed=std::from_chars(s.data(),s.data()+s.size(),n);
                if(parsed.ec!=std::errc{}||parsed.ptr!=s.data()+s.size()||
                   !n||n>uint64_t(INT64_MAX))
                    throw std::invalid_argument("Caverns invalid decimal identity");
                return n;
            };
            if(mode!="get"&&p.has("expectedRevision")&&
               decimalId(p["expectedRevision"])!=volume.sourceRevision)
                throw std::invalid_argument("Caverns document revision changed; refresh");
            bool changed=false;
            uint64_t stampId=0;
            std::optional<terraweave::caverns::CaveSphereStamp> affected;
            if(mode=="erase"){
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
                if(volume.sourceRevision>=uint64_t(INT64_MAX))
                    throw std::overflow_error("Caverns revision overflow");
                ++volume.sourceRevision;
                auto updated=terraweave::caverns::caveSnapshotToJson(volume);
                if(!p.getBool("withinStroke",false))w->map.beginEdit();
                w->map.data.extra.set("terraweaveCaverns",std::move(updated));
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
            if(affected){
                const auto bounds=terraweave::caverns::dirtyBoundsForStamp(*affected);
                Json dirty=Json::object();
                dirty.set("minXcm",Json::number(bounds.minX));
                dirty.set("minYcm",Json::number(bounds.minY));
                dirty.set("minZcm",Json::number(bounds.minZ));
                dirty.set("maxXcm",Json::number(bounds.maxX));
                dirty.set("maxYcm",Json::number(bounds.maxY));
                dirty.set("maxZcm",Json::number(bounds.maxZ));
                result.set("dirtyBounds",std::move(dirty));
            }
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
