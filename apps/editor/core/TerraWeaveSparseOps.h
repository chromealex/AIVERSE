// Editor/MCP shared operations for open-world TerraWeave sparse chunk paint.
// Canonical occupancy + Earthworks height overrides round-trip in MapDoc::extra;
// no reliance on legacy RTS-grid dimensions or second height authority.
#pragma once
#include "apps/editor/core/Workspace.h"
#include "engine/world/TerraWeaveSparseTerrainAuthoring.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::editor {

namespace sparse_terrain_ops {
inline int32_t checked(const Json& j,const char* key,int64_t def,
                       int64_t lo,int64_t hi) {
    const int64_t v=j.getInt(key,def);
    if(v<lo || v>hi)
        throw std::invalid_argument(std::string("TerraWeave sparse ")+key);
    return static_cast<int32_t>(v);
}
struct Loaded {
    int32_t chunkMetres=256,spacingCm=100,baseHeightCm=0;
    terraweave::EarthworksSnapshot soil;
    std::vector<world::Cell> chunks;
};
inline Loaded parse(const Json& state,const Json& options) {
    Loaded loaded;
    if(!state.isNull() && !state.isObject())
        throw std::invalid_argument("TerraWeave sparse document malformed");
    if(state.isObject() && state.getInt("version",-1)!=1)
        throw std::invalid_argument("TerraWeave sparse document version");
    const Json& source=state.isObject()?state:options;
    loaded.chunkMetres=checked(source,"chunkMetres",256,16,4096);
    loaded.spacingCm=checked(source,"spacingCm",100,10,10000);
    loaded.baseHeightCm=checked(source,"baseHeightCm",0,-1000000,1000000);
    loaded.soil.spacingCm=loaded.spacingCm;
    loaded.soil.bedrockCm=-1000000;
    if(state.isObject()){
        const int64_t revision=state.getInt("revision",0);
        if(revision<0)
            throw std::invalid_argument("TerraWeave sparse negative revision");
        loaded.soil.revision=uint64_t(revision);
        const Json& chunks=state["chunks"];
        const Json& heights=state["heights"];
        if(!chunks.isArray() || !heights.isArray() ||
           chunks.size()>terraweave::SparseTerrainAuthoring::kMaxChunks ||
           heights.size()>1000000u)
            throw std::invalid_argument("TerraWeave sparse arrays invalid");
        for(const auto& tile:chunks.items()){
            if(!tile.isObject())throw std::invalid_argument("TerraWeave sparse tile");
            const int64_t x=tile.getInt("x",INT64_MAX);
            const int64_t z=tile.getInt("z",INT64_MAX);
            if(x<INT32_MIN || x>INT32_MAX ||
               z<INT32_MIN || z>INT32_MAX)
                throw std::out_of_range("TerraWeave sparse tile extent");
            loaded.chunks.push_back({int32_t(x),0,int32_t(z)});
        }
        for(const auto& height:heights.items()){
            if(!height.isObject())
                throw std::invalid_argument("TerraWeave sparse soil override");
            const int64_t h=height.getInt("h",INT64_MAX);
            if(h<INT32_MIN || h>INT32_MAX)
                throw std::out_of_range("TerraWeave sparse height overflow");
            loaded.soil.heights.push_back({
                {height.getInt("x",INT64_MAX),
                 height.getInt("z",INT64_MAX)},int32_t(h)});
        }
    }
    return loaded;
}
inline Json encode(const terraweave::SparseTerrainAuthoring& painter,
                   const terraweave::EarthworksLayer& earth,
                   const Loaded& metadata) {
    const auto soil=earth.snapshot();
    Json state=Json::object();
    state.set("version",Json::number(int64_t(1)));
    state.set("chunkMetres",Json::number(int64_t(metadata.chunkMetres)));
    state.set("spacingCm",Json::number(int64_t(metadata.spacingCm)));
    state.set("baseHeightCm",Json::number(int64_t(metadata.baseHeightCm)));
    state.set("revision",Json::number(int64_t(soil.revision)));
    Json chunks=Json::array();
    for(const auto& c:painter.chunks()){
        Json item=Json::object();
        item.set("x",Json::number(int64_t(c.x)));
        item.set("z",Json::number(int64_t(c.z)));
        chunks.push(std::move(item));
    }
    state.set("chunks",std::move(chunks));
    Json heights=Json::array();
    for(const auto& [key,value]:soil.heights){
        Json item=Json::object();
        item.set("x",Json::number(key.x));
        item.set("z",Json::number(key.z));
        item.set("h",Json::number(int64_t(value)));
        heights.push(std::move(item));
    }
    state.set("heights",std::move(heights));
    return state;
}
} // namespace sparse_terrain_ops

inline void registerTerraWeaveSparseOps(Workspace& ws){
    const Json schema=Json::parse(R"({
      "properties":{
        "xCm":{"type":"integer"},"zCm":{"type":"integer"},
        "radiusCm":{"type":"integer","minimum":1,"maximum":50000},
        "depthCm":{"type":"integer","minimum":1,"maximum":20000},
        "mode":{"type":"string","enum":["create","raise","lower"]},
        "chunkMetres":{"type":"integer","minimum":16,"maximum":4096},
        "spacingCm":{"type":"integer","minimum":10,"maximum":10000},
        "baseHeightCm":{"type":"integer"},
        "withinStroke":{"type":"boolean"}
      },
      "required":["xCm","zCm"]
    })");
    Workspace* w=&ws;
    ws.registerOp({"terraweave_sparse_paint",
        "Paint or create TerraWeave terrain at any signed world-centimetre coordinate. "
        "Missing chunks touched by the brush appear automatically; neighbors use "
        "the same global Earthworks heights. Flat chunk renders as one quad at "
        "every camera distance. State saved in map.terraweaveSparse; undoable.",
        schema,[w](const Json& p)->Json{
            if(w->project.dir.empty())
                throw std::runtime_error("TerraWeave sparse paint needs an open project");
            if(w->map.data.planet())
                throw std::invalid_argument("Use planet sculpt for spherical terrain");
            if(!p["xCm"].isNumber() || !p["zCm"].isNumber())
                throw std::invalid_argument("TerraWeave sparse requires world centimetres");
            const int64_t x=p.getInt("xCm"),z=p.getInt("zCm");
            const Json& original=w->map.data.extra["terraweaveSparse"];
            auto loaded=sparse_terrain_ops::parse(original,p);
            terraweave::EarthworksLayer earth(
                loaded.spacingCm,
                [base=loaded.baseHeightCm](int64_t,int64_t){return base;},
                loaded.soil.bedrockCm);
            earth.setSnapshot(loaded.soil);
            terraweave::SparseTerrainAuthoring painter(
                earth,loaded.chunkMetres);
            painter.restore(loaded.chunks);
            const auto previousChunks=painter.chunkCount();
            const std::string mode=p.getStr("mode","create");
            if(mode=="create"){
                painter.ensure(x,z);
            }else if(mode=="raise" || mode=="lower"){
                terraweave::EarthworkBrush brush;
                brush.centerXcm=x;brush.centerZcm=z;
                brush.radiusCm=sparse_terrain_ops::checked(
                    p,"radiusCm",300,1,50000);
                brush.depthCm=sparse_terrain_ops::checked(
                    p,"depthCm",50,1,20000);
                brush.mode=mode=="raise"?
                    terraweave::EarthworkMode::Fill:
                    terraweave::EarthworkMode::Dig;
                painter.paint(brush);
            }else throw std::invalid_argument(
                "TerraWeave sparse mode must be create/raise/lower");
            const auto updated=sparse_terrain_ops::encode(painter,earth,loaded);
            if(updated!=original){
                // The GUI calls beginEdit once per mouse stroke; remote ops
                // create one undo snapshot per invocation by default.
                if(!p.getBool("withinStroke",false))
                    w->map.beginEdit();
                w->map.data.extra.set("terraweaveSparse",updated);
                w->map.touched();
                w->note({Change::Map,0,0,-1,-1,-1,
                         "TerraWeave: разреженный чанк/рельеф обновлён"});
            }
            Json result=Json::object();
            result.set("ok",Json::fromBool(true));
            result.set("chunkCount",Json::number(int64_t(painter.chunkCount())));
            result.set("createdChunks",Json::number(
                int64_t(painter.chunkCount()-previousChunks)));
            result.set("earthRevision",Json::number(int64_t(earth.revision())));
            result.set("summary",Json::string(
                "TerraWeave sparse: chunks "+
                std::to_string(painter.chunkCount())+
                ", soil revision "+std::to_string(earth.revision())));
            return result;
        }});
}
} // namespace aiverse::editor
