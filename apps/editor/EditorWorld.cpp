// World tree of the editor (docs/editor-world-tree.md): the rows of every object of the game world for the
// Hierarchy / Inspector, the multi-selection, Scene picking of world objects, and the world_* MCP operations.
//   Edit mode: the open map set up once in a throwaway simulation (WorldPreview), rebuilt after map / rules /
//   script changes (debounced); map entities stay editable, what the game creates at start is read-only.
//   Play mode: the running test (the editor's play-test or the hosted game client), read a few times a second.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include "Editor.h"
#include "core/SimTools.h"

namespace fs = std::filesystem;

namespace rts::editor {

// ---------------------------------------------------------------- selection
void Editor::syncSelection() {
    const int n = int(doc().data.entities.size());   // map entities removed elsewhere (undo, agents) leave the set
    std::erase_if(selectionSet, [n](const Selection& s) { return s.kind == Selection::Entity && (s.index < 0 || s.index >= n); });
    if (selection.kind == Selection::Entity && (selection.index < 0 || selection.index >= n)) selection = selectionSet.empty() ? Selection{} : selectionSet.back();
    if (selection.kind == Selection::None) { selectionSet.clear(); return; }
    if (std::find(selectionSet.begin(), selectionSet.end(), selection) == selectionSet.end()) selectionSet = {selection};
}

const std::vector<Selection>& Editor::selected() { syncSelection(); return selectionSet; }

bool Editor::isSelected(const Selection& s) {
    syncSelection();
    return std::find(selectionSet.begin(), selectionSet.end(), s) != selectionSet.end();
}

void Editor::selectToggle(const Selection& s) {
    syncSelection();
    auto it = std::find(selectionSet.begin(), selectionSet.end(), s);
    if (it != selectionSet.end()) {
        selectionSet.erase(it);
        selection = selectionSet.empty() ? Selection{} : selectionSet.back();
    } else {
        selectionSet.push_back(s);
        selection = s;
    }
}

void Editor::selectAdd(const Selection& s) {
    syncSelection();
    if (std::find(selectionSet.begin(), selectionSet.end(), s) == selectionSet.end()) selectionSet.push_back(s);
    selection = s;
}

void Editor::selectMany(std::vector<Selection> items) {
    items.erase(std::remove_if(items.begin(), items.end(), [](const Selection& s) { return s.kind == Selection::None; }), items.end());
    std::vector<Selection> unique;
    for (auto& s : items) if (std::find(unique.begin(), unique.end(), s) == unique.end()) unique.push_back(s);
    selectionSet = std::move(unique);
    selection = selectionSet.empty() ? Selection{} : selectionSet.front();
}

std::vector<int> Editor::selectedEntities() {
    std::vector<int> out;
    const int n = int(doc().data.entities.size());
    for (auto& s : selected()) if (s.kind == Selection::Entity && s.index >= 0 && s.index < n) out.push_back(s.index);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void Editor::removeSelectedEntities() {
    std::vector<int> idx = selectedEntities();
    if (idx.empty() || playing()) return;
    doc().beginEdit();   // one undo step: removeEntity below would snapshot each time
    auto& d = doc().data;
    for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
        const int i = *it;
        // roads: drop the ones touching i, shift the indices above it (MapDoc::removeEntity semantics)
        std::vector<std::pair<int, int>> roads;
        for (auto [a, b] : d.roads) if (a != i && b != i) roads.push_back({a > i ? a - 1 : a, b > i ? b - 1 : b});
        d.roads = std::move(roads);
        d.entities.erase(d.entities.begin() + i);
    }
    doc().touched();
    status = "Удалено объектов: " + std::to_string(idx.size());
    select({});
}

bool Editor::objectHidden(int owner, const std::string& type) const {
    if (sceneHidden.empty()) return false;
    const std::string o = "o" + std::to_string(owner), t = "t" + type;
    return sceneHidden.count(o) || sceneHidden.count(t) || sceneHidden.count(o + "/" + t);
}

bool Editor::objectLocked(int owner, const std::string& type) const {
    if (sceneLocked.empty()) return false;
    const std::string o = "o" + std::to_string(owner), t = "t" + type;
    return sceneLocked.count(o) || sceneLocked.count(t) || sceneLocked.count(o + "/" + t);
}

// ---------------------------------------------------------------- rows <-> selection
Selection Editor::worldSelection(const WorldRow& r) const {
    if (!worldLive_ && r.doc >= 0) return {Selection::Entity, r.doc, {}};
    Selection s{Selection::Live, -1, {}};
    s.ref = r.ref;
    return s;
}

const WorldRow* Editor::worldRow(const Selection& s) const {
    if (s.kind == Selection::Live) return world_.find(s.ref);
    if (s.kind == Selection::Entity && worldFresh()) return world_.findDoc(s.index);
    return nullptr;
}

bool Editor::selectionPos(const Selection& s, float& x, float& z) const {
    const auto& d = ws_.map.data;
    switch (s.kind) {
    case Selection::Entity:
        if (s.index < 0 || s.index >= int(d.entities.size())) return false;
        x = d.entities[size_t(s.index)].x; z = d.entities[size_t(s.index)].z;
        return true;
    case Selection::Live:
        if (worldLive_) {   // the running world itself: the rows may be old (or not read when nobody shows them)
            Simulation* sim = const_cast<Editor*>(this)->worldSim();
            const Entity e = worldEntity(s.ref);
            if (!sim || !sim->world().alive(e)) return false;
            const auto* tr = sim->world().tryGet<Transform>(e);
            if (!tr) return false;
            x = float(tr->pos.x.toDouble()); z = float(tr->pos.z.toDouble());
            return true;
        }
        if (const WorldRow* r = world_.find(s.ref); r && r->hasPos) { x = r->x; z = r->z; return true; }
        return false;
    case Selection::Player:
        if (s.index < 0 || s.index >= int(d.players.size()) || !d.players[size_t(s.index)].hasStart) return false;
        x = d.players[size_t(s.index)].startX; z = d.players[size_t(s.index)].startZ;
        return true;
    case Selection::Area: {
        const Json& a = d.extra["areas"][s.key];
        if (!a.isArray()) return false;
        x = float(a[0].toDouble() + a[2].toDouble() / 2); z = float(a[1].toDouble() + a[3].toDouble() / 2);
        return true;
    }
    case Selection::Wave: {
        const Json& w = d.extra["waves"][size_t(std::max(0, s.index))];
        if (!w["at"].isArray()) return false;
        x = float(w["at"][0].toDouble()); z = float(w["at"][1].toDouble());
        return true;
    }
    case Selection::Road:
        if (s.index < 0 || s.index >= int(d.roads.size())) return false;
        x = (d.entities[size_t(d.roads[size_t(s.index)].first)].x + d.entities[size_t(d.roads[size_t(s.index)].second)].x) * 0.5f;
        z = (d.entities[size_t(d.roads[size_t(s.index)].first)].z + d.entities[size_t(d.roads[size_t(s.index)].second)].z) * 0.5f;
        return true;
    default: return false;
    }
}

Simulation* Editor::worldSim() {
    if (worldLive_) {
        if (sim_) return sim_.get();
        return clientHost_.active() ? clientHost_.simulation() : nullptr;
    }
    return preview_.sim();
}

game::RtsModule* Editor::worldModule() {
    Simulation* s = worldSim();
    return s ? dynamic_cast<game::RtsModule*>(&s->module()) : nullptr;
}

Selection Editor::pickObject(const Viewport& vp, const render::RTSCamera& cam, float sx, float sy) const {
    if (!playing()) {
        const int n = pickEntity(vp, cam, sx, sy);
        if (n >= 0) return {Selection::Entity, n, {}};
        if (worldLive_ || !showGameObjects || sceneHidden.count("game") || ws_.map.data.planet()) return {};
    }
    float x, z;
    if (view_.planetMode() || !groundAt(vp, cam, sx, sy, x, z)) return {};
    if (worldLive_) {   // play: the running world (positions of this frame; the rows are only read for the Hierarchy)
        Simulation* sim = const_cast<Editor*>(this)->worldSim();
        if (!sim) return {};
        Entity bestE = kNullEntity;
        float bestD = 1e9f;
        World& w = sim->world();
        w.each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform& tr) {
            if ((u.flags & game::kUnitOffMap) || size_t(u.type) >= rules().types.size()) return;
            const auto* o = w.tryGet<Owner>(e);
            const int owner = !o ? 0 : o->player == game::kHostileNeutral ? -1 : int(o->player);
            const std::string& tid = rules().types[u.type].id;
            if (objectHidden(owner, tid) || objectLocked(owner, tid)) return;
            const float rad = const_cast<game::RtsView&>(view_).selectionRadius(u.type) + 0.2f;
            const float dd = std::hypot(x - float(tr.pos.x.toDouble()), z - float(tr.pos.z.toDouble()));
            if (dd < rad && dd < bestD) { bestE = e; bestD = dd; }
        });
        if (!bestE.valid()) return {};
        Selection s{Selection::Live, -1, {}};
        s.ref = worldRef(bestE);
        return s;
    }
    const WorldRow* best = nullptr;
    float bestD = 1e9f;
    for (auto& r : world_.rows) {
        if (!r.hasPos || r.type < 0 || (!worldLive_ && r.doc >= 0) || (r.flags & game::kUnitOffMap)) continue;
        const std::string& tid = rules().types[size_t(r.type)].id;
        if (objectHidden(r.owner, tid) || objectLocked(r.owner, tid)) continue;
        const float rad = const_cast<game::RtsView&>(view_).selectionRadius(uint16_t(r.type)) + 0.2f;
        const float dd = std::hypot(x - r.x, z - r.z);
        if (dd < rad && dd < bestD) { best = &r; bestD = dd; }
    }
    if (!best) return {};
    Selection s{Selection::Live, -1, {}};
    s.ref = best->ref;
    return s;
}

// ---------------------------------------------------------------- refresh
namespace {
uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
}

void Editor::readLiveWorld(Simulation& live, game::RtsModule& mod) {
    const bool data = worldDataWanted();
    if (data) { auto ps = prof.scope(EditorProfile::Snapshot); live.module().prepareSnapshot(live); }   // observation only (as every checksum does)
    auto rs = prof.scope(EditorProfile::ReadWorld);
    readWorld(live, mod, worldCfg_, liveLinks_, liveSetup_, &doc().data, world_, WorldReadOptions{data});
    worldStale_ = false;
}

void Editor::ensureWorld() {
    worldDataAt_ = worldWantAt_ = time_;
    if (worldLive_) {
        Simulation* live = worldSim();
        auto* mod = worldModule();
        if (live && mod && (worldStale_ || !world_.scriptData || world_.tick != live->tick())) readLiveWorld(*live, *mod);
        return;
    }
    if (!preview_.sim() || (!preview_.busy() && previewKey_ != previewSeenKey_)) previewForce_ = true;
}

void Editor::updateWorld() {
    auto profScope = prof.scope(EditorProfile::WorldUpdate);
    Simulation* live = sim_ ? sim_.get() : clientHost_.active() ? clientHost_.simulation() : nullptr;
    if (live) {
        auto* mod = dynamic_cast<game::RtsModule*>(&live->module());
        if (!mod) return;
        const bool fresh = live != liveSim_ || (worldLive_ && live->tick() < world_.tick);
        if (fresh) {
            // a new match: what exists at its start is "setup", map entities are known for the editor's own play-test
            liveSim_ = live;
            liveLinks_ = live == sim_.get() ? playDocLinks_ : std::vector<uint64_t>();
            liveDocOf_.clear();
            for (size_t i = 0; i < liveLinks_.size(); ++i) if (liveLinks_[i]) liveDocOf_[liveLinks_[i]] = int(i);
            liveSetup_.clear();
            if (live->tick() <= live->tickRate() * 2) {
                const auto st = live->world().entityState();
                for (uint32_t k = 0; k < st.alive.size(); ++k) if (st.alive[k]) liveSetup_.push_back(worldRef(Entity{k, st.gens[k]}));
            }
            // the selection follows: map entities -> their live objects
            std::vector<Selection> next;
            for (auto& s : selected()) {
                if (s.kind == Selection::Entity && s.index >= 0 && size_t(s.index) < liveLinks_.size() && liveLinks_[size_t(s.index)]) {
                    Selection l{Selection::Live, -1, {}};
                    l.ref = liveLinks_[size_t(s.index)];
                    next.push_back(l);
                } else if (s.kind != Selection::Entity && s.kind != Selection::Live) next.push_back(s);
            }
            if (!worldLive_) selectMany(next);
            worldLive_ = true;
            worldReadAt_ = 0;
            world_ = {};
            worldStale_ = true;
        }
        // rows only for a consumer (Hierarchy open, MCP): 5 times a second, at once when one appears or wants script data
        const bool want = worldWanted();
        const bool due = time_ >= worldReadAt_;
        if (want && (due || worldStale_ || (worldDataWanted() && !world_.scriptData))) {
            worldReadAt_ = time_ + 0.2;
            readLiveWorld(*live, *mod);
        } else if (due) {
            worldReadAt_ = time_ + 0.2;
            worldStale_ = true;
        }
        if (due) {
            // live objects that died leave the selection (checked on the world: no read needed)
            std::vector<Selection> keep;
            bool changed = false;
            for (auto& s : selected()) {
                if (s.kind == Selection::Live && !live->world().alive(worldEntity(s.ref))) { changed = true; continue; }
                keep.push_back(s);
            }
            if (changed) {
                const Selection primary = selection;
                selectMany(keep);
                if (std::find(keep.begin(), keep.end(), primary) != keep.end()) selection = primary;
            }
        }
        return;
    }
    if (worldLive_) {   // play ended: live objects of map entities -> the map entities again
        std::vector<Selection> next;
        for (auto& s : selected()) {
            if (s.kind == Selection::Live) {
                if (auto it = liveDocOf_.find(s.ref); it != liveDocOf_.end()) next.push_back({Selection::Entity, it->second, {}});
            } else next.push_back(s);
        }
        worldLive_ = false;
        liveSim_ = nullptr;
        world_ = {};
        previewKey_ = 0;
        previewForce_ = true;
        selectMany(next);
    }
    if (ws_.project.dir.empty()) return;
    // a background build finished: adopt it and read its rows (once per build: the preview does not run)
    if (preview_.poll()) {
        prof.add(EditorProfile::PreviewWorker, preview_.buildMs());
        if (Simulation* ps = preview_.sim()) {
            if (auto* pm = preview_.module()) {
                auto rs = prof.scope(EditorProfile::ReadWorld);
                readWorld(*ps, *pm, worldCfg_, preview_.links(), preview_.setupRefs(), &doc().data, world_);
            }
        } else world_ = {};
        if (preview_.buildMs() > 2500 && worldAuto) {
            worldAuto = false;   // big games: rebuild on request (Hierarchy refresh button)
            ws_.log("world", "Предпросмотр мира долгий (" + std::to_string(int(preview_.buildMs())) + " мс): автообновление выключено");
        }
    }
    const bool want = worldWanted() || previewForce_;
    // project scripts: their newest change time, checked once a second (hot edits from the Scripts window / agents)
    if (want && time_ >= scriptsCheckAt_) {
        scriptsCheckAt_ = time_ + 1.0;
        uint64_t stamp = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(ws_.project.dir + "/scripts", ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".lua")
                stamp = std::max(stamp, uint64_t(fs::last_write_time(it->path(), ec).time_since_epoch().count()));
        scriptsStamp_ = stamp;
    }
    uint64_t key = 14695981039346656037ull;
    key = mix(key, doc().revision + 1);
    key = mix(key, uint64_t(rulesRevision_) + 7);
    key = mix(key, scriptsStamp_);
    key = mix(key, std::hash<std::string>{}(ws_.project.dir + "\n" + doc().path));
    if (key != previewSeenKey_) { previewSeenKey_ = key; previewDue_ = time_ + 0.6; }   // debounce while the user drags / paints
    // never while a stroke / drag / slider is held: the map changes every frame then (rebuild after the release)
    const ImGuiIO& io = ImGui::GetIO();
    const bool holding = io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2];
    if (holding && key != previewKey_) previewDue_ = std::max(previewDue_, time_ + 0.25);
    if (previewForce_) previewDue_ = std::min(previewDue_, time_);
    if (key != previewKey_ && (worldAuto || previewForce_) && want && time_ >= previewDue_) {
        previewForce_ = false;
        previewKey_ = key;
        // this thread only copies the inputs; the map / rules JSON, the scripts and the setup are made on the worker
        auto bs = prof.scope(EditorProfile::PreviewBuild);
        auto data = std::make_shared<MapData>(doc().data);
        preview_.start([data, path = doc().path, dir = ws_.project.dir, rules = ws_.project.rulesJson](Json& simRules, Json& simMap) {
            MapDoc md;
            md.data = std::move(*data);
            md.path = path;
            simMap = mapForSim(md.toJson(), dir);
            Project p;
            p.dir = dir;
            p.rulesJson = rules;
            simRules = p.simRules(simMap);
        }, doc().data, worldCfg_);
    }
    // runtime objects of the preview that vanished on a rebuild leave the selection
    std::vector<Selection> keep;
    bool changed = false;
    for (auto& s : selected()) {
        if (s.kind == Selection::Live && !world_.find(s.ref)) { changed = true; continue; }
        keep.push_back(s);
    }
    if (changed) selectMany(keep);
}

bool Editor::worldDetails(const Selection& s, WorldRow& row, Json* data) {
    if (worldLive_) {
        if (s.kind != Selection::Live) return false;
        Simulation* sim = worldSim();
        game::RtsModule* mod = worldModule();
        const Entity e = worldEntity(s.ref);
        if (!sim || !mod || !readWorldRow(*sim, *mod, worldCfg_, e, row, WorldReadOptions{false})) return false;
        if (auto it = liveDocOf_.find(row.ref); it != liveDocOf_.end()) { row.doc = it->second; row.origin = WorldOrigin::Map; }
        else { row.doc = -1; row.origin = std::binary_search(liveSetup_.begin(), liveSetup_.end(), row.ref) ? WorldOrigin::Setup : WorldOrigin::Runtime; }
        // Lua data as the scripts hold it now, without flushing the whole VM into the world
        std::string blob;
        const bool hasData = mod->scripts() && mod->scripts()->peekData(*sim, e, blob);
        if (hasData) setWorldRowData(worldCfg_, blob, row);
        if (data) {
            *data = inspectEntity(*sim, *mod, e, ws_.project.rulesJson);
            if (hasData && (*data)["components"].isObject()) data->member("components").set("ScriptData", decodeScriptBlob(blob));
        }
        return true;
    }
    const WorldRow* r = worldRow(s);
    if (!r) return false;
    row = *r;
    if (data) {
        Simulation* sim = worldSim();
        game::RtsModule* mod = worldModule();
        if (sim && mod) *data = inspectEntity(*sim, *mod, r->e, ws_.project.rulesJson);
    }
    return true;
}

bool Editor::worldFresh() const {
    return !worldLive_ && preview_.sim() && !preview_.busy() && previewKey_ != 0 && previewKey_ == previewSeenKey_ && !previewForce_;
}

// ---------------------------------------------------------------- MCP
void Editor::registerWorldOps() {
    auto add = [this](const char* name, const char* desc, const char* schema, Workspace::OpFn fn) {
        ws_.registerOp({name, desc, Json::parse(schema), std::move(fn), true});
    };
    auto rowJson = [this](const WorldRow& r) {
        Json j = Json::object();
        j.set("ref", Json::number(int64_t(r.ref)));
        j.set("index", Json::number(int64_t(r.e.index)));
        if (r.doc >= 0) j.set("doc", Json::number(int64_t(r.doc)));
        j.set("name", Json::string(worldRowName(world_, rules(), &doc().data, r)));
        if (r.type >= 0) j.set("type", Json::string(rules().types[size_t(r.type)].id));
        j.set("owner", Json::number(int64_t(r.owner)));
        j.set("kind", Json::string(worldKindId(r.kind)));
        j.set("origin", Json::string(r.origin == WorldOrigin::Map ? "map" : r.origin == WorldOrigin::Setup ? "setup" : "runtime"));
        if (r.hasPos) { Json p = Json::array(); p.push(Json::number(double(r.x), 2)); p.push(Json::number(double(r.z), 2)); j.set("pos", p); }
        if (r.maxHp > 0) { j.set("hp", Json::number(double(r.hp), 1)); j.set("maxHp", Json::number(double(r.maxHp), 1)); }
        if (r.parent) { j.set("parent", Json::number(int64_t(r.parent))); j.set("parentRelation", Json::string(r.parentRel)); }
        if (!r.label.empty()) j.set("label", Json::string(r.label));
        Json c = Json::array();
        r.eachComp([&](int k) { c.push(Json::string(worldComponents()[size_t(k)].id)); });
        j.set("components", c);
        return j;
    };
    add("world_entities",
        "Every object of the game world as the Hierarchy shows it: edit mode = the open map set up with the game's "
        "scripts / C++ modules (map entities have 'doc' = index in the map; origin 'setup' = created by the game at start), "
        "play mode = the running test. 'group': keys (owner, type, kind, category, origin, region, parent, tag, component, "
        "data:<key>) -> nested counts; 'filter': Hierarchy search (t: type, p: owner, c: component, r: region, d: data, #id).",
        R"J({"properties": {"group": {"type": "array", "items": {"type": "string"}}, "filter": {"type": "string"},
            "limit": {"type": "integer", "description": "entities listed (default 200, 0 = only groups)"}}})J",
        [this, rowJson](const Json& p) {
            ensureWorld();
            if (!worldLive_ && !preview_.sim()) {
                previewForce_ = true;
                throw std::runtime_error(preview_.error().empty() ? "world preview not ready yet, call again" : "world preview failed: " + preview_.error());
            }
            const std::string filter = p.getStr("filter");
            const int64_t limit = std::clamp<int64_t>(p["limit"].integer(200), 0, 5000);
            std::vector<std::string> keys;
            for (auto& k : p["group"].items()) keys.push_back(k.str());
            Json groups = Json::object(), list = Json::array();
            int64_t total = 0;
            for (auto& r : world_.rows) {
                if (!filter.empty() && !worldRowMatches(world_, rules(), &doc().data, r, filter)) continue;
                ++total;
                if (int64_t(list.size()) < limit) list.push(rowJson(r));
                Json* g = &groups;
                for (size_t k = 0; k < keys.size(); ++k) {
                    const std::string v = worldGroupValue(world_, rules(), r, keys[k]);
                    if (k + 1 == keys.size()) g->set(v, Json::number((*g)[v].integer() + 1));
                    else g = &g->member(v);
                }
            }
            Json r = Json::object();
            r.set("mode", Json::string(worldLive_ ? "play" : "edit"));
            r.set("tick", Json::number(int64_t(world_.tick)));
            r.set("total", Json::number(total));
            if (!keys.empty()) r.set("groups", groups);
            r.set("entities", list);
            if (!worldLive_ && !preview_.error().empty()) r.set("previewError", Json::string(preview_.error()));
            return r;
        });
    add("world_entity",
        "All data of one world object (what the Inspector's details show): components with their fields, effective vs base "
        "stats, the rules block of its type, Lua data (game.data). By 'ref' (from world_entities) or 'doc' (map entity index).",
        R"J({"properties": {"ref": {"type": "integer"}, "doc": {"type": "integer"}}})J",
        [this, rowJson](const Json& p) {
            ensureWorld();
            const WorldRow* r = p.has("ref") ? world_.find(uint64_t(p["ref"].integer())) : p.has("doc") ? world_.findDoc(int(p["doc"].integer())) : nullptr;
            Simulation* s = worldSim();
            game::RtsModule* m = worldModule();
            if (!r || !s || !m) throw std::runtime_error("no such object in the world tree (world_entities lists them)");
            Json j = inspectEntity(*s, *m, r->e, ws_.project.rulesJson);
            j.set("row", rowJson(*r));
            if (r->doc >= 0 && size_t(r->doc) < doc().data.entities.size() && !worldLive_) j.set("map", doc().toJson()["entities"][size_t(r->doc)]);
            return j;
        });
    add("editor_select",
        "Select objects in the editor (Hierarchy, Inspector, Scene): 'docs' = map entity indices, 'refs' = world objects "
        "(world_entities ref), 'focus': fly the Scene camera to the first one. Empty = clear the selection.",
        R"J({"properties": {"docs": {"type": "array", "items": {"type": "integer"}}, "refs": {"type": "array", "items": {"type": "integer"}},
            "focus": {"type": "boolean"}}})J",
        [this](const Json& p) {
            ensureWorld();
            std::vector<Selection> v;
            for (auto& d : p["docs"].items()) {
                const int i = int(d.integer(-1));
                if (i < 0 || i >= int(doc().data.entities.size())) throw std::runtime_error("docs: no map entity " + std::to_string(i));
                if (worldLive_) {
                    if (const WorldRow* r = world_.findDoc(i)) v.push_back(worldSelection(*r));
                } else v.push_back({Selection::Entity, i, {}});
            }
            for (auto& rf : p["refs"].items()) {
                const WorldRow* r = world_.find(uint64_t(rf.integer()));
                if (!r) throw std::runtime_error("refs: no world object " + std::to_string(rf.integer()));
                v.push_back(worldSelection(*r));
            }
            selectMany(v);
            float x, z;
            if (p.getBool("focus") && !v.empty() && selectionPos(v.front(), x, z)) focusScene(x, z, 18);
            Json r = Json::object();
            r.set("selected", Json::number(int64_t(selectionSet.size())));
            return r;
        });
}

} // namespace rts::editor
