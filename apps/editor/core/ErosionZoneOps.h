// Editor/MCP authoring of erosion zones (map "erosionZones", see
// engine/world/TerraWeaveErosionZones.h): physical areas where the ground
// erodes with a coefficient and optional soil class. Undoable map edits;
// the Scene draws them as gizmos (NatureGizmos.h).
#pragma once
#include "apps/editor/core/Workspace.h"
#include "engine/world/TerraWeaveErosionZones.h"
#include <string>

namespace aiverse::editor {

inline void registerErosionZoneOps(Workspace& ws) {
    Workspace* w = &ws;
    ws.registerOp({"erosion_zones",
        "List the map's erosion zones (map key erosionZones): id, shape circle{x,z,radius} | polygon{points [[x,z],...]}, "
        "soil sand|silt|loam|clay|gravel|rock, coefficient 0..10 (x the default bank erosion rate; 0 = protected).",
        Json::parse(R"({"properties":{}})"), [w](const Json&) -> Json {
            Json r = Json::object();
            const Json& zones = w->map.data.extra["erosionZones"];
            r.set("zones", zones.isArray() ? zones : Json::array());
            r.set("count", Json::number(int64_t(zones.isArray() ? zones.size() : 0)));
            return r;
        }});
    ws.registerOp({"erosion_zone_set",
        "Create or replace (by id) an erosion zone: where the ground erodes and how fast. shape circle (x, z, radius m) or "
        "polygon (points [[x,z],...] m, 3..256); soil sand|silt|loam|clay|gravel|rock (optional); coefficient 0..10 "
        "(1 = default rate, 0 = does not erode). Used by the NEREID -> TerraWeave bank erosion (applyErosionZones). Undoable.",
        Json::parse(R"({"properties":{"id":{"type":"string"},"shape":{"type":"string","enum":["circle","polygon"]},
            "x":{"type":"number"},"z":{"type":"number"},"radius":{"type":"number"},
            "points":{"type":"array","items":{"type":"array","items":{"type":"number"}}},
            "soil":{"type":"string","enum":["sand","silt","loam","clay","gravel","rock"]},
            "coefficient":{"type":"number","minimum":0,"maximum":10}},"required":["id"]})"),
        [w](const Json& p) -> Json {
            const std::string id = p.getStr("id");
            if (id.empty()) throw std::invalid_argument("erosion zone id required");
            Json zone = Json::object();
            zone.set("id", Json::string(id));
            const std::string shape = p.getStr("shape", p.has("points") ? "polygon" : "circle");
            zone.set("shape", Json::string(shape));
            if (shape == "circle") {
                zone.set("x", p["x"]); zone.set("z", p["z"]);
                zone.set("radius", p.has("radius") ? p["radius"] : Json::number(10.0, 2));
            } else zone.set("points", p["points"]);
            if (p.has("soil")) zone.set("soil", p["soil"]);
            zone.set("coefficient", p.has("coefficient") ? p["coefficient"] : Json::number(1.0, 2));
            Json zones = w->map.data.extra["erosionZones"].isArray() ? w->map.data.extra["erosionZones"] : Json::array();
            bool replaced = false;
            for (size_t i = 0; i < zones.size(); ++i)
                if (zones[i].getStr("id") == id) { zones.at(i) = zone; replaced = true; }
            if (!replaced) zones.push(zone);
            (void)terraweave::ErosionZoneSet::parse(zones);   // validate before editing
            w->map.beginEdit();
            w->map.data.extra.set("erosionZones", zones);
            w->map.touched();
            w->note({Change::Map, 0, 0, w->map.data.w - 1, w->map.data.h - 1, -1, "Зона размыва " + id});
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("replaced", Json::fromBool(replaced));
            r.set("summary", Json::string("Зона размыва " + id + ": ×" +
                std::to_string(zone["coefficient"].toDouble(1.0)).substr(0, 4)));
            return r;
        }});
    ws.registerOp({"erosion_zone_delete", "Delete an erosion zone by id. Undoable.",
        Json::parse(R"({"properties":{"id":{"type":"string"}},"required":["id"]})"),
        [w](const Json& p) -> Json {
            const std::string id = p.getStr("id");
            Json zones = w->map.data.extra["erosionZones"];
            if (!zones.isArray()) throw std::invalid_argument("no erosion zones");
            Json kept = Json::array();
            for (const auto& z : zones.items()) if (z.getStr("id") != id) kept.push(z);
            if (kept.size() == zones.size()) throw std::invalid_argument("no erosion zone " + id);
            w->map.beginEdit();
            if (kept.size()) w->map.data.extra.set("erosionZones", kept); else w->map.data.extra.erase("erosionZones");
            w->map.touched();
            w->note({Change::Map, 0, 0, w->map.data.w - 1, w->map.data.h - 1, -1, "Удалена зона размыва " + id});
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            return r;
        }});
}

} // namespace aiverse::editor
