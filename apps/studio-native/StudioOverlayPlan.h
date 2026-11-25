#pragma once

#include "apps/editor/core/OverlayPreviewGrid.h"
#include "StudioDocumentIds.h"
#include "StudioSession.h"
#include "apps/editor/core/OverlayPreviewTerritory.h"
#include "engine/view/game/GameView.h"
#include <set>

namespace aiverse::studio {

inline bool studioPlanShape(const game::OverlayDef& def) {
    using game::OvShape;
    if (def.style.space == game::OvSpace::Screen)
        return def.shape == OvShape::Text || def.shape == OvShape::Bar || def.shape == OvShape::Circle ||
               def.shape == OvShape::Ring || def.shape == OvShape::Rect;
    return def.style.space == game::OvSpace::Ground && def.shape != OvShape::Icon &&
           def.shape != OvShape::Text && def.shape != OvShape::Bar;
}

inline void registerStudioOverlayPlan(StudioSession& session, std::function<void(const Json&, bool)> notify) {
    auto& ws = session.workspace();
    Json schema;
    for (const auto& op : ws.ops()) if (op.name == "overlay_preview") { schema = op.schema; break; }
    schema.member("properties").set("reveal", Json::parse(R"({"type":"boolean"})"));
    schema.member("properties").set("entry", Json::parse(R"({"type":"object","properties":{"id":{"type":"string"},"visible":{"type":"boolean"}},"required":["id","visible"]})"));
    ws.registerOp({"overlay_preview", "Show project overlays in the HTML Overlays panel. Supply ids, a structure type or map coordinates. off clears the preview. Empty arguments read the current selection.",
        schema, [&session, notify = std::move(notify), state = Json::object()](const Json& patch) mutable {
            const auto& ws = session.workspace();
            if (ws.project.dir.empty()) throw std::runtime_error("Open a project first");
            const auto epoch = session.graphDocumentScope().getStr("epoch");
            if (state.getStr("epoch") != epoch) {
                state = Json::object();
                state.set("epoch", Json::string(epoch));
                state.set("ok", Json::fromBool(true));
                state.set("preview", Json::array());
                for (const auto& type : ws.project.rules.types) if (type.structure) {
                    state.set("placeType", Json::string(type.id));
                    break;
                }
                state.set("version", Json::number(int64_t(0)));
            }
            if (!patch.size()) return state;
            if (patch.has("reveal") && patch["reveal"].type()!=Json::Type::Bool)
                throw std::runtime_error("reveal must be a boolean");
            Json next = state;
            if (patch.has("off") && patch["off"].type()!=Json::Type::Bool) throw std::runtime_error("off must be a boolean");
            if (patch.getBool("off")) {
                next.set("preview", Json::array());
                next.set("placeType", Json::string(""));
                next.erase("x"); next.erase("z");
            } else {
                const auto& rules = ws.project.rules;
                if (patch.has("ids") || patch.has("entry")) {
                    Json requested = patch["ids"];
                    if (patch.has("entry")) {
                        const auto& entry = patch["entry"];
                        if (patch.has("ids") || !entry["id"].isString() || entry["visible"].type()!=Json::Type::Bool)
                            throw std::runtime_error("Pass ids or one entry with id and visible");
                        requested = Json::array();
                        for (const auto& id : state["preview"].items()) if (id.str()!=entry.getStr("id")) requested.push(id);
                        if (entry.getBool("visible")) requested.push(entry["id"]);
                    }
                    if (!requested.isArray()) throw std::runtime_error("ids must be an array");
                    const auto catalog = game::loadOverlaySet(rules, ws.project.rulesJson["overlays"], ws.project.manifest["overlays"]);
                    std::set<std::string> allowed, seen;
                    for (const auto& def : catalog.defs) if (studioPlanShape(def)) allowed.insert(def.id);
                    Json ids = Json::array();
                    for (const auto& id : requested.items()) {
                        if (!id.isString() || !allowed.count(id.str())) throw std::runtime_error("Unavailable preview overlay: " + id.dump());
                        if (seen.insert(id.str()).second) ids.push(id);
                    }
                    next.set("preview", ids);
                }
                if (patch.has("placeType")) {
                    const auto& type = patch["placeType"];
                    const int index = rules.findType(type.str());
                    if (!type.isString() || (!type.str().empty() && (index < 0 || !rules.types[size_t(index)].structure)))
                        throw std::runtime_error("Choose a structure type");
                    next.set("placeType", type);
                }
                for (const char* axis : {"x", "z"}) if (patch.has(axis)) {
                    const auto& value = patch[axis];
                    if (value.isNull()) { next.erase(axis); continue; }
                    const int extent = *axis == 'x' ? ws.map.data.w : ws.map.data.h;
                    if (!value.isNumber() || value.toDouble() < 0 || value.toDouble() >= extent)
                        throw std::runtime_error(std::string(axis) + " must be inside the map");
                    next.set(axis, value);
                }
            }
            next.set("version", Json::number(state.getInt("version") + int64_t(1)));
            state = next;
            notify(state, patch.getBool("reveal", true) && !patch.getBool("off"));
            return state;
        }, true});
    ws.registerOp({"overlay_plan_preview", "Preview ground and screen overlay definitions in project draw order on the map plan.",
        Json::parse(R"({"properties":{"ids":{"type":"array","items":{"type":"string"}},"placeType":{"type":"string"},"x":{"type":"number"},"z":{"type":"number"}}})"),
        [&ws, &session](const Json& args) {
            const auto& map = ws.map.data;
            const auto& project = ws.project;
            const auto& rules = project.rules;
            if (ws.project.dir.empty()) throw std::runtime_error("Open a project first");
            auto set = game::loadOverlaySet(rules, project.rulesJson["overlays"], project.manifest["overlays"]);
            Json result = Json::object(), catalog = Json::array();
            std::set<std::string> ids;
            if (args.has("ids") && !args["ids"].isArray()) throw std::runtime_error("ids must be an array");
            for (const auto& id : args["ids"].items()) ids.insert(id.str());
            game::OverlaySet selected;
            std::vector<std::string> forced;
            for (const auto& def : set.defs) {
                if (!studioPlanShape(def)) continue;
                catalog.push(Json::string(def.id));
                if (ids.erase(def.id)) { selected.defs.push_back(def); forced.push_back(def.id); }
            }
            if (!ids.empty()) throw std::runtime_error("Unavailable preview overlay: " + *ids.begin());
            result.set("catalog", catalog);
            if (selected.defs.empty()) return result;
            if (map.w <= 0 || map.h <= 0 || map.worldSpace || map.planet()) throw std::runtime_error("Open a planar tile map");
            auto coord = [&](const char* key, int extent) {
                if (!args.has(key)) return float(extent) * 0.5f;
                const double v = args[key].toDouble();
                if (!args[key].isNumber() || !std::isfinite(v) || v < 0 || v >= extent)
                    throw std::runtime_error(std::string(key) + " must be inside the map");
                return float(v);
            };
            game::OverlayContext context;
            context.player = 1;
            context.hasCursor = true;
            context.cursor = {coord("x", map.w), 0, coord("z", map.h)};
            context.preview = &forced;
            context.areas = &map.extra["areas"];
            game::MapGrid grid;
            grid.init(map.w, map.h);
            for (size_t i = 0; i < grid.flags.size(); ++i) editor::copyPreviewCell(map, rules, i, grid, i);
            editor::fillPreviewTerritory(map, rules, grid);
            if (!args.getStr("placeType").empty()) {
                const int idx = rules.findType(args.getStr("placeType"));
                if (idx < 0 || !rules.types[size_t(idx)].structure) throw std::runtime_error("Choose a structure type");
                const auto& type = rules.types[size_t(idx)];
                context.placeType = idx;
                context.placeAt = {editor::snapPlacementCenter(context.cursor.x, type.footprintW), 0,
                                   editor::snapPlacementCenter(context.cursor.z, type.footprintH)};
                context.placeOk = grid.areaBuildable(int(std::floor(context.placeAt.x-type.footprintW*.5f+.01f)),
                    int(std::floor(context.placeAt.z-type.footprintH*.5f+.01f)), type.footprintW, type.footprintH);
            }
            game::GameScene scene;
            Json objects = Json::array();
            size_t hovered = map.entities.size();
            float nearest = 1.0f;
            std::set<int> selectedEntities, hiddenEntities;
            for (const auto& id : session.selectedObjectIds()) selectedEntities.insert(studioMapEntityIndex(id, ws.map));
            for (const auto& id : session.hiddenIds()) hiddenEntities.insert(studioMapEntityIndex(id, ws.map));
            for (size_t i = 0; i < map.entities.size(); ++i) {
                if (hiddenEntities.count(int(i))) continue;
                const auto& e = map.entities[i];
                const int idx = rules.findType(e.type);
                if (idx < 0) continue;
                game::SceneObject o;
                o.type = uint16_t(idx); o.owner = e.owner < 0 ? game::kHostileNeutral : uint8_t(std::clamp(e.owner, 0, 16)); o.pos = {e.x, 0, e.z};
                o.selected = selectedEntities.count(int(i)) != 0;
                o.garrison = rules.types[size_t(idx)].garrisonMax > 0 ? std::max(0, int(e.garrison)) : -1;
                const float dx = e.x-context.cursor.x, dz = e.z-context.cursor.z;
                const float distance = dx*dx + dz*dz;
                if (distance < nearest) { nearest = distance; hovered = scene.objects.size(); }
                scene.objects.push_back(o);
                Json point = Json::array();
                point.push(Json::number(double(e.x))); point.push(Json::number(double(e.z)));
                objects.push(point);
            }
            if (hovered < scene.objects.size()) scene.objects[hovered].hovered = true;
            game::GameOverlays overlays;
            overlays.initPrepared(rules, std::move(selected), ws.project.dir);
            overlays.setContext(context);
            Json primitives = Json::array(), screen;
            for (const auto& primitive : overlays.groundPlan(scene, grid, 1.0, &screen)) {
                Json item = Json::array();
                item.push(Json::number(int64_t(primitive.color)));
                auto point = [&](render::float3 p) { item.push(Json::number(double(p.x))); item.push(Json::number(double(p.z))); };
                point(primitive.a); point(primitive.b);
                if (primitive.triangle) point(primitive.c);
                primitives.push(item);
            }
            result.set("width", Json::number(int64_t(map.w)));
            result.set("height", Json::number(int64_t(map.h)));
            // Bound the context image independently of the full-resolution predicate grid.
            const int stride = std::max(1, (std::max(map.w, map.h) + 95) / 96);
            Json terrain = Json::array();
            for (int z = 0; z < map.h; z += stride) {
                std::string row;
                for (int x = 0; x < map.w; x += stride) row += map.cells[size_t(z * map.w + x)];
                terrain.push(Json::string(row));
            }
            result.set("terrain", terrain);
            result.set("screen", std::move(screen));
            result.set("stride", Json::number(int64_t(stride)));
            result.set("objects", objects);
            result.set("primitives", primitives);
            return result;
        }, true});
}

} // namespace aiverse::studio
