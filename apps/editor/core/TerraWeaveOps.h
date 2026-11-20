// TerraWeave opt-in editor/MCP operations. Kept out of the runtime/simulation.
// Registered alongside existing terrain tools so agents and GUI use one API.
#pragma once
#include "apps/editor/core/TerraWeaveMapAdapter.h"
#include "apps/editor/core/Workspace.h"
#include "apps/editor/core/TerraWeaveEarthworksOps.h"
#include "apps/editor/core/TerraWeaveSparseOps.h"
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
        "Use terraweave_generate to bake the full heightfield; flat RTS map only.",
        Json::parse(schema), [w](const Json& p) -> Json {
            if (w->project.dir.empty()) throw std::runtime_error("open a game first");
            if (w->project.isFps()) throw std::runtime_error(
                "TerraWeave MapDoc heightfield adapter is for RTS grid maps only");
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
        "Generate full deterministic TerraWeave heightfield in the current RTS map. "
        "By default refuses to overwrite existing sculpt/detail. Set overwrite=true "
        "to explicitly replace authored terrain, or preserveEdits=true to move "
        "the existing authored height and detail residuals onto a new generator base "
        "(requires a matching saved TerraWeave recipe fingerprint). "
        "Both choices are mutually exclusive. Creates one undo step.",
        Json::parse(schema), [w](const Json& p) -> Json {
            if (w->project.dir.empty()) throw std::runtime_error("open a game first");
            if (w->project.isFps()) throw std::runtime_error(
                "TerraWeave MapDoc heightfield adapter is for RTS grid maps only");
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
}

} // namespace aiverse::editor
