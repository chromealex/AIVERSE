// Flat MapDoc authoring adapter for TerraWeave Earthworks excavation.
// Heightfield-only dig/fill (pits/ditches, NOT caves or undercuts).
// Uses existing MapDoc undo, view revision and authored detail surface.
#pragma once
#include "apps/editor/core/Workspace.h"
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/world/TerraWeaveLayer.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::editor {

inline int16_t terraWeaveHeightCorner(int32_t cm) {
    const int64_t scaled=int64_t(cm)*16;
    const int64_t value=(scaled>=0?scaled+50:scaled-50)/100;
    if(value<std::numeric_limits<int16_t>::min() ||
       value>std::numeric_limits<int16_t>::max())
        throw std::out_of_range("TerraWeave Earthworks legacy R16 height");
    return int16_t(value);
}

inline int32_t terraWeaveCornerCm(int16_t value) {
    const int64_t cm=int64_t(value)*100;
    return int32_t((cm>=0?cm+8:cm-8)/16);
}

inline void registerTerraWeaveEarthworksOps(Workspace& ws) {
    ws.registerOp({"terraweave_excavate",
        "Dig or fill a radius in the current flat RTS heightfield. "
        "Center xCm/zCm, radiusCm and depthCm use world centimetres. "
        "Never creates caves/undercuts; preserves authored detail material "
        "and fine surface by rebasing the detail onto changed corners. Undoable.",
        Json::parse(R"({
          "properties":{
            "xCm":{"type":"integer"},"zCm":{"type":"integer"},
            "radiusCm":{"type":"integer","minimum":1,"maximum":50000},
            "depthCm":{"type":"integer","minimum":1,"maximum":20000},
            "mode":{"type":"string","enum":["dig","fill"]},
            "cellSizeCm":{"type":"integer","minimum":10,"maximum":10000},
            "originXcm":{"type":"integer"},"originZcm":{"type":"integer"}
          },
          "required":["xCm","zCm","radiusCm","depthCm"]
        })"),[w=&ws](const Json& p) -> Json {
            if(w->project.dir.empty())
                throw std::runtime_error("open game first");
            if(w->project.isFps() || w->map.data.planet())
                throw std::invalid_argument(
                    "TerraWeave Earthworks flat MapDoc adapter requires RTS grid");
            const MapData& map=w->map.data;
            const int width=map.w,height=map.h;
            const size_t required=(size_t(width)+1u)*(size_t(height)+1u);
            if(map.corners.size()!=required)
                throw std::invalid_argument(
                    "TerraWeave Earthworks requires a complete heightfield");
            const Json& recipe=map.extra["terraweave"];
            const int64_t step=p.getInt("cellSizeCm",
                recipe.getInt("cellSizeCm",100));
            const int64_t ox=p.getInt("originXcm",
                recipe.getInt("originXcm",0));
            const int64_t oz=p.getInt("originZcm",
                recipe.getInt("originZcm",0));
            if(step<10||step>10000||ox%step||oz%step)
                throw std::invalid_argument(
                    "TerraWeave Earthworks origin must align with cell spacing");
            const int64_t radius=p.getInt("radiusCm",200);
            const int64_t depth=p.getInt("depthCm",100);
            if(radius<1||radius>50000||depth<1||depth>20000)
                throw std::invalid_argument("TerraWeave Earthworks radius/depth");
            const auto mode=p.getStr("mode","dig");
            if(mode!="dig"&&mode!="fill")
                throw std::invalid_argument("TerraWeave Earthworks mode");
            const std::vector<int16_t> oldCorners=map.corners;
            terraweave::EarthworksLayer layer(
                int32_t(step),
                [oldCorners,width,height,step,ox,oz](
                        int64_t x,int64_t z)->int32_t {
                    const int64_t ix=std::clamp<int64_t>(
                        (x-ox)/step,0,width);
                    const int64_t iz=std::clamp<int64_t>(
                        (z-oz)/step,0,height);
                    return terraWeaveCornerCm(oldCorners[
                        size_t(iz)*(size_t(width)+1u)+size_t(ix)]);
                });
            const auto result=layer.sculpt({
                p.getInt("xCm"),p.getInt("zCm"),int32_t(radius),
                int32_t(depth),
                mode=="dig"?terraweave::EarthworkMode::Dig:
                            terraweave::EarthworkMode::Fill});
            auto newCorners=oldCorners;
            uint32_t modified=0;
            for(const auto& edit:result.vertices) {
                const int64_t x=edit.vertex.x*step-ox;
                const int64_t z=edit.vertex.z*step-oz;
                if(x<0||z<0||x>int64_t(width)*step||
                   z>int64_t(height)*step)continue;
                const size_t index=size_t(z/step)*(size_t(width)+1u)+
                                   size_t(x/step);
                const int16_t next=terraWeaveHeightCorner(edit.afterCm);
                modified+=next!=newCorners[index];
                newCorners[index]=next;
            }
            if(!modified) {
                Json empty=Json::object();
                empty.set("ok",Json::fromBool(true));
                empty.set("changedCorners",Json::number(int64_t(0)));
                empty.set("summary",Json::string(
                    "TerraWeave: no corner height change"));
                return empty;
            }
            // Generate full new detail BEFORE changing MapDoc, preserving
            // author-painted splat and fine-detail residuals. All-or-none.
            std::vector<int16_t> newDetail;
            if(!map.detail.empty()) {
                newDetail=terraweave::rebaseLegacyHeightfield(
                    oldCorners,map.detail,width,height,map.detailScale,
                    oldCorners,newCorners).detail;
            }
            w->map.beginEdit();
            w->map.data.corners=std::move(newCorners);
            if(!newDetail.empty())
                w->map.data.detail=std::move(newDetail);
            w->map.touched();
            w->note({Change::Map,0,0,width-1,height-1,-1,
                     mode=="dig"?"TerraWeave excavation":"TerraWeave fill"});
            Json out=Json::object();
            out.set("ok",Json::fromBool(true));
            out.set("changedCorners",Json::number(int64_t(modified)));
            out.set("volumeCm3",Json::number(int64_t(
                std::min<uint64_t>(result.movedVolumeCm3,
                    uint64_t(std::numeric_limits<int64_t>::max())))));
            out.set("revision",Json::number(int64_t(w->map.revision)));
            out.set("summary",Json::string(
                "TerraWeave: "+mode+" modified "+
                std::to_string(modified)+" corners; undo available"));
            return out;
        }});
}

} // namespace aiverse::editor
