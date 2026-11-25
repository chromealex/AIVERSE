// TerraWeave opt-in editor/MCP operations. Kept out of the runtime/simulation.
// Registered alongside existing terrain tools so agents and GUI use one API.
#pragma once
#include "apps/editor/core/TerraWeaveMapAdapter.h"
#include "apps/editor/core/Workspace.h"
#include "apps/editor/core/TerraWeaveEarthworksOps.h"
#include "apps/editor/core/TerraWeaveSparseOps.h"
#include "apps/editor/core/TerraWeaveCavernsOps.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace aiverse::editor {
namespace terraweave_ops_detail {

inline int32_t readCm(const Json& p, const char* key, int32_t defaultValue,
                      int32_t minimum, int32_t maximum) {
    const int64_t v = p.getInt(key, defaultValue);
    if (v < minimum || v > maximum)
        throw std::invalid_argument(std::string("TerraWeave ") + key + " out of range");
    return static_cast<int32_t>(v);
}
inline terraweave::TerrainSettings terrain(const Json& p) {
    terraweave::TerrainSettings t;
    const int64_t seed = p.getInt("seed", 1);
    if (seed < 0) throw std::invalid_argument("TerraWeave seed must be non-negative");
    t.seed = static_cast<uint64_t>(seed);
    t.baseHeightCm = readCm(p, "baseHeightCm", 0, -200000, 200000);
    t.reliefCm = readCm(p, "reliefCm", 6000, 0, 200000);
    t.basePeriodCm = readCm(p, "basePeriodCm", 6400, 100, 1000000);
    t.octaves = static_cast<uint32_t>(readCm(p, "octaves", 5, 1, 12));
    return t;
}
// Map-saved forest recipe (extra.terraweaveForest), read by the editor forest
// overlay and native hosts. Presentation recipe only: no entities, no ECS.
struct ForestRecipe {
    bool enabled=true;
    int32_t spacingCm=1100;
    int32_t densityPermille=780;
    int32_t maxSlopePermille=1250;
    int64_t seedOffset=0;
};
inline ForestRecipe forestRecipe(const Json& p) {
    ForestRecipe f;
    if(!p.isObject())return f;
    f.enabled=p.getBool("enabled",true);
    f.spacingCm=readCm(p,"spacingCm",1100,300,5000);
    f.densityPermille=readCm(p,"densityPermille",780,0,1000);
    f.maxSlopePermille=readCm(p,"maxSlopePermille",1250,0,5000);
    f.seedOffset=p.getInt("seedOffset",0);
    if(f.seedOffset<0)throw std::invalid_argument("TerraWeave forest seedOffset must be non-negative");
    return f;
}
inline Json forestRecipeJson(const ForestRecipe& f) {
    Json j=Json::object();
    j.set("version",Json::number(int64_t(1)));
    j.set("enabled",Json::fromBool(f.enabled));
    j.set("spacingCm",Json::number(int64_t(f.spacingCm)));
    j.set("densityPermille",Json::number(int64_t(f.densityPermille)));
    j.set("maxSlopePermille",Json::number(int64_t(f.maxSlopePermille)));
    j.set("seedOffset",Json::number(f.seedOffset));
    return j;
}
inline terraweave::LegacyHeightfieldOptions origin(const Json& p) {
    terraweave::LegacyHeightfieldOptions out;
    out.worldOriginXcm = p.getInt("originXcm", 0);
    out.worldOriginZcm = p.getInt("originZcm", 0);
    out.cellSizeCm = readCm(p, "cellSizeCm", 100, 1, 10000);
    return out;
}
inline Json sanitizedConfig(const terraweave::TerrainSettings& t,
                            terraweave::LegacyHeightfieldOptions o,
                            int widthCells=0,int heightCells=0,
                            const std::string& baseFingerprint={}) {
    Json j = Json::object();
    j.set("version", Json::number(int64_t(1)));
    j.set("seed", Json::number(static_cast<int64_t>(t.seed)));
    j.set("baseHeightCm", Json::number(int64_t(t.baseHeightCm)));
    j.set("reliefCm", Json::number(int64_t(t.reliefCm)));
    j.set("basePeriodCm", Json::number(int64_t(t.basePeriodCm)));
    j.set("octaves", Json::number(int64_t(t.octaves)));
    j.set("originXcm", Json::number(o.worldOriginXcm));
    j.set("originZcm", Json::number(o.worldOriginZcm));
    j.set("cellSizeCm", Json::number(int64_t(o.cellSizeCm)));
    // Optional verified provenance for safe future non-destructive edits.
    // Old version-1 metadata without these fields is still readable, but
    // cannot be blindly trusted for a rebase (the map may have resized).
    if(widthCells>0 || heightCells>0 || !baseFingerprint.empty()) {
        if(widthCells<=0 || heightCells<=0 || baseFingerprint.size()!=16)
            throw std::invalid_argument("TerraWeave recipe provenance missing");
        j.set("widthCells",Json::number(int64_t(widthCells)));
        j.set("heightCells",Json::number(int64_t(heightCells)));
        j.set("baseFingerprint",Json::string(baseFingerprint));
    }
    return j;
}
// Only fingerprints attached to a complete, dimension-matched recipe are
// authoritative. V1 recipes without provenance require explicit replace.
inline void validateRebaseRecipe(const Json& recipe,int widthCells,int heightCells) {
    if(!recipe.isObject() || recipe.getInt("version")!=1 ||
       recipe.getInt("widthCells",-1)!=widthCells ||
       recipe.getInt("heightCells",-1)!=heightCells ||
       !recipe["baseFingerprint"].isString() ||
       recipe.getStr("baseFingerprint").size()!=16)
        throw std::invalid_argument(
            "TerraWeave rebase needs a matching provenance recipe; use overwrite=true to reset");
    for(const char* key:{"seed","baseHeightCm","reliefCm",
                        "basePeriodCm","octaves","originXcm",
                        "originZcm","cellSizeCm"})
        if(!recipe[key].isNumber())
            throw std::invalid_argument(
                std::string("TerraWeave recipe missing field ")+key);
    // terrain() / origin() perform full range validation in the caller.
}
inline Json preview(const terraweave::TerrainSettings& t,
                    terraweave::LegacyHeightfieldOptions o,
                    int w, int h) {
    constexpr int kPoints = 9; // summary, not a full renderer preview
    Json rows = Json::array();
    int32_t minCm = std::numeric_limits<int32_t>::max();
    int32_t maxCm = std::numeric_limits<int32_t>::min();
    for (int iz = 0; iz < kPoints; ++iz) {
        Json row = Json::array();
        for (int ix = 0; ix < kPoints; ++ix) {
            const int64_t xCm = o.worldOriginXcm +
                int64_t(w) * o.cellSizeCm * ix / (kPoints - 1);
            const int64_t zCm = o.worldOriginZcm +
                int64_t(h) * o.cellSizeCm * iz / (kPoints - 1);
            const int32_t value = terraweave::heightAtCm(t, xCm, zCm);
            minCm = std::min(minCm, value);
            maxCm = std::max(maxCm, value);
            row.push(Json::number(int64_t(value)));
        }
        rows.push(row);
    }
    Json result = Json::object();
    result.set("ok", Json::fromBool(true));
    result.set("samplesCm", rows);
    result.set("minCm", Json::number(int64_t(minCm)));
    result.set("maxCm", Json::number(int64_t(maxCm)));
    result.set("summary", Json::string(
        "TerraWeave: sample 9x9, height " + std::to_string(minCm) +
        ".." + std::to_string(maxCm) + " cm (no changes applied)"));
    return result;
}

} // namespace terraweave_ops_detail

inline void registerTerraWeaveOps(Workspace& ws) {
    registerTerraWeaveEarthworksOps(ws);
    registerTerraWeaveSparseOps(ws);
    registerTerraWeaveCavernsOps(ws);
    const char* schema = R"({
      "properties": {
        "seed":{"type":"integer","minimum":0},
        "baseHeightCm":{"type":"integer"},
        "reliefCm":{"type":"integer","minimum":0},
        "basePeriodCm":{"type":"integer","minimum":100},
        "octaves":{"type":"integer","minimum":1,"maximum":12},
        "originXcm":{"type":"integer"},
        "originZcm":{"type":"integer"},
        "cellSizeCm":{"type":"integer","minimum":1},
        "overwrite":{"type":"boolean"},
        "preserveEdits":{"type":"boolean"}
      }
    })";
    Workspace* w = &ws;
    ws.registerOp({"terraweave_preview",
        "Preview 9x9 deterministic TerraWeave heights in cm, no map mutations. "
        "Use terraweave_generate to bake the full heightfield; flat grid map only.",
        Json::parse(schema), [w](const Json& p) -> Json {
            if (w->project.dir.empty()) throw std::runtime_error("open a game first");
            if (w->map.data.worldSpace) throw std::runtime_error(
                "TerraWeave MapDoc heightfield adapter is for flat grid maps only (world-space maps: G2)");
            if (w->map.data.planet()) throw std::runtime_error(
                "TerraWeave flat map adapter does not support planet maps");
            const auto settings = terraweave_ops_detail::terrain(p);
            const auto origin = terraweave_ops_detail::origin(p);
            // Validate full extent, not just samples, before preview.
            if (int64_t(w->map.data.w) * origin.cellSizeCm > terraweave::kWorldLimitCm ||
                int64_t(w->map.data.h) * origin.cellSizeCm > terraweave::kWorldLimitCm ||
                origin.worldOriginXcm < -terraweave::kWorldLimitCm ||
                origin.worldOriginZcm < -terraweave::kWorldLimitCm ||
                origin.worldOriginXcm > terraweave::kWorldLimitCm -
                    int64_t(w->map.data.w) * origin.cellSizeCm ||
                origin.worldOriginZcm > terraweave::kWorldLimitCm -
                    int64_t(w->map.data.h) * origin.cellSizeCm)
                throw std::out_of_range("TerraWeave preview world extent");
            return terraweave_ops_detail::preview(settings, origin,
                                                   w->map.data.w, w->map.data.h);
        }});
    ws.registerOp({"terraweave_generate",
        "Generate full deterministic TerraWeave heightfield in the current grid map. "
        "By default refuses to overwrite existing sculpt/detail. Set overwrite=true "
        "to explicitly replace authored terrain, or preserveEdits=true to move "
        "the existing authored height and detail residuals onto a new generator base "
        "(requires a matching saved TerraWeave recipe fingerprint). "
        "Both choices are mutually exclusive. Creates one undo step.",
        Json::parse(schema), [w](const Json& p) -> Json {
            if (w->project.dir.empty()) throw std::runtime_error("open a game first");
            if (w->map.data.worldSpace) throw std::runtime_error(
                "TerraWeave MapDoc heightfield adapter is for flat grid maps only (world-space maps: G2)");
            auto settings = terraweave_ops_detail::terrain(p);
            auto origin = terraweave_ops_detail::origin(p);
            const bool overwrite=p.getBool("overwrite",false);
            const bool preserve=p.getBool("preserveEdits",false);
            if(overwrite && preserve)
                throw std::invalid_argument(
                    "TerraWeave overwrite and preserveEdits are mutually exclusive");
            const int mapWidth=w->map.data.w, mapHeight=w->map.data.h;
            Json result=Json::object();
            std::string fingerprint;
            if(preserve) {
                const Json& previous=w->map.data.extra["terraweave"];
                terraweave_ops_detail::validateRebaseRecipe(
                    previous,mapWidth,mapHeight);
                const auto oldTerrain=terraweave_ops_detail::terrain(previous);
                const auto oldOrigin=terraweave_ops_detail::origin(previous);
                const auto report=rebaseTerraWeaveTerrain(
                    w->map,oldTerrain,oldOrigin,
                    previous.getStr("baseFingerprint"),settings,origin);
                fingerprint=report.newBaseFingerprint;
                result.set("preservedAuthoredCorners",
                    Json::number(int64_t(report.authoredCorners)));
                result.set("shiftedCorners",
                    Json::number(int64_t(report.shiftedCorners)));
                result.set("shiftedDetail",
                    Json::number(int64_t(report.shiftedDetail)));
                result.set("preservedDetail",Json::fromBool(
                    !w->map.data.detail.empty()));
            } else {
                bakeTerraWeaveTerrain(w->map,settings,origin,
                    overwrite ? TerraWeaveExistingTerrain::ReplaceExplicitly :
                                TerraWeaveExistingTerrain::RefuseOverwrite);
                // No authored deltas exist after a fresh/explicit overwrite.
                fingerprint=terraweave::legacyBaseFingerprint(
                    w->map.data.corners,mapWidth,mapHeight);
            }
            w->map.data.extra.set("terraweave",
                terraweave_ops_detail::sanitizedConfig(
                    settings,origin,mapWidth,mapHeight,fingerprint));
            w->note({Change::Map,0,0,mapWidth-1,mapHeight-1,-1,
                     preserve?"TerraWeave preserve authored edits":
                              "TerraWeave generated heightfield"});
            result.set("ok",Json::fromBool(true));
            result.set("corners",Json::number(
                int64_t(w->map.data.corners.size())));
            result.set("revision",Json::number(
                static_cast<int64_t>(w->map.revision)));
            result.set("summary",Json::string(
                "TerraWeave: " + std::string(preserve?"rebased":"generated") +
                " " + std::to_string(mapWidth) + "x" +
                std::to_string(mapHeight) +
                " terrain; undo available"));
            return result;
        }});
    ws.registerOp({"terraweave_forest_get",
        "Read the map's TerraWeave forest recipe (extra.terraweaveForest, defaults when absent) "
        "and the saved terrain recipe (extra.terraweave). Read-only.",
        Json::parse(R"({"properties":{}})"), [w](const Json&) -> Json {
            if (w->project.dir.empty()) throw std::runtime_error("open a game first");
            Json r=terraweave_ops_detail::forestRecipeJson(
                terraweave_ops_detail::forestRecipe(w->map.data.extra["terraweaveForest"]));
            r.set("saved",Json::fromBool(w->map.data.extra["terraweaveForest"].isObject()));
            if(w->map.data.extra["terraweave"].isObject())r.set("terrain",w->map.data.extra["terraweave"]);
            r.set("ok",Json::fromBool(true));
            return r;
        }});
    ws.registerOp({"terraweave_forest_set",
        "Save the TerraWeave forest recipe into the map (extra.terraweaveForest): enabled, spacingCm 300..5000, "
        "densityPermille 0..1000, maxSlopePermille 0..5000, seedOffset >= 0. Drives the deterministic "
        "TerraWeave Arbor forest overlay (no entity per tree). One undo step.",
        Json::parse(R"({"properties":{"enabled":{"type":"boolean"},"spacingCm":{"type":"integer","minimum":300,"maximum":5000},
          "densityPermille":{"type":"integer","minimum":0,"maximum":1000},"maxSlopePermille":{"type":"integer","minimum":0,"maximum":5000},
          "seedOffset":{"type":"integer","minimum":0}}})"),
        [w](const Json& p) -> Json {
            if (w->project.dir.empty()) throw std::runtime_error("open a game first");
            if (w->map.data.worldSpace || w->map.data.planet()) throw std::runtime_error(
                "TerraWeave forest recipe is for flat heightfield maps");
            // Merge over the current recipe: unspecified fields keep their value.
            Json merged=terraweave_ops_detail::forestRecipeJson(
                terraweave_ops_detail::forestRecipe(w->map.data.extra["terraweaveForest"]));
            for(const char* key:{"enabled","spacingCm","densityPermille","maxSlopePermille","seedOffset"})
                if(p.has(key))merged.set(key,p[key]);
            const auto recipe=terraweave_ops_detail::forestRecipe(merged); // validates
            w->map.beginEdit();
            w->map.data.extra.set("terraweaveForest",terraweave_ops_detail::forestRecipeJson(recipe));
            w->note({Change::Map,0,0,-1,-1,-1,"TerraWeave forest recipe"});
            Json r=terraweave_ops_detail::forestRecipeJson(recipe);
            r.set("ok",Json::fromBool(true));
            r.set("summary",Json::string("TerraWeave forest: "+std::string(recipe.enabled?"on":"off")+
                ", spacing "+std::to_string(recipe.spacingCm)+" cm, density "+std::to_string(recipe.densityPermille)+" permille"));
            return r;
        }});
}

} // namespace aiverse::editor
