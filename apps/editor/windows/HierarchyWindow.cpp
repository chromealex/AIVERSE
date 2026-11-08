// Hierarchy: every object of the game world (docs/editor-world-tree.md). Edit mode: the map's objects
// (editable) plus what the game creates at start (marked, read-only); play mode: the live world of the test.
// Then the other map nodes: starts, areas, regions, waves, triggers, roads, planet bodies, other map blocks.
//   Grouping: owner → type (default), type, category, component, nesting, origin, + game.json groupings.
//   Search: type / id / owner / region / component / script data, prefixes t: p: c: r: d: #.
//   Click selects (Ctrl / Cmd toggles, Shift range), a click on a group selects its objects, double click
//   focuses the Scene; arrows, Delete, F, F2 (rename), Ctrl+D (duplicate); drag objects onto a player to
//   change the owner; eye / lock per player, type and section; hover highlights the object in the Scene.
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../EditorWorldEdit.h"

namespace rts::editor {

namespace {
struct Grouping { const char* title; std::vector<std::string> by; };
const Grouping kGroupings[] = {
    {"Владелец → тип", {"owner", "type"}},
    {"Тип", {"type"}},
    {"Категория → тип", {"category", "type"}},
    {"Компонент", {"component"}},
    {"Вложенность", {"parent"}},
    {"Происхождение → тип", {"origin", "type"}},
};
constexpr int kBuiltinGroupings = int(sizeof kGroupings / sizeof *kGroupings);

std::string ellipsize(const std::string& s, float maxW) {
    if (maxW <= 0) return {};
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    const float dots = ImGui::CalcTextSize("…").x;
    size_t n = s.size();
    while (n > 0) {
        do --n; while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80);   // UTF-8 boundary
        if (ImGui::CalcTextSize(s.c_str(), s.c_str() + n).x + dots <= maxW) break;
    }
    return s.substr(0, n) + "…";
}
} // namespace

class HierarchyWindow : public EditorWindow {
public:
    const char* id() const override { return "hierarchy"; }
    const char* title() const override { return "Иерархия"; }
    const char* group() const override { return "scene"; }
    int order() const override { return 0; }
    const char* dockArea() const override { return "left"; }

    void draw(Editor& ed) override {
        // the rows are read for us (play: 5 Hz) only while we are shown; script data only when the label, a
        // data: grouping or the search can use it
        bool data = !ed.worldConfig().labelKey.empty() || !filter_.empty();
        for (auto& k : groupingKeys(ed)) data = data || k.rfind("data:", 0) == 0;
        ed.wantWorld(data);
        toolbar(ed);
        statusLine(ed);
        rebuild(ed);
        ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None);
        focused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        reveal(ed);
        const float rowH = ImGui::GetFontSize() + 6;
        if (scrollTo_ >= 0) {
            const float y = float(scrollTo_) * rowH, h = ImGui::GetWindowHeight();
            if (y < ImGui::GetScrollY() || y + rowH > ImGui::GetScrollY() + h) ImGui::SetScrollY(std::max(0.0f, y - h * 0.4f));
            scrollTo_ = -1;
        }
        ImGuiListClipper clip;
        clip.Begin(int(rows_.size()), rowH);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) drawRow(ed, i, rowH);
        clip.End();
        if (ed.doc().data.entities.empty() && ed.world().rows.empty() && ed.doc().data.players.empty())
            ui::emptyStateHere(ICON_HIERARCHY, "Карта пуста", "Инструмент «Объекты» ставит юниты и здания.");
        if (ImGui::BeginPopupContextWindow("##ctx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) { addMenu(ed); ImGui::EndPopup(); }
        if (focused_) keys(ed);
        ImGui::EndChild();
    }

private:
    // ---------------------------------------------------------------- model
    struct Entry {
        // world row: a row of Editor::world() (valid until its next read, which bumps the serial and so rebuilds
        // us before any use), or for map entities in edit mode a synthesized one (+ the preview's data when fresh)
        const WorldRow* src = nullptr;
        WorldRow own;
        Selection sel;
        std::string typeId;
        bool editable = false;      // a map document entity in edit mode
        const WorldRow& row() const { return src ? *src : own; }
    };
    struct Row {
        int depth = 0;
        std::string key;            // expansion key ("" = not expandable)
        std::string label, tip;
        const char* icon = "";
        ImVec4 iconColor{0, 0, 0, 0};
        bool dot = false;           // player colour dot instead of an icon
        size_t count = 0;
        bool showCount = false;
        Selection sel;              // what a click selects (groups: none -> their members)
        std::vector<int> members;   // groups / items: entries below (click on a group = select them)
        int entry = -1;             // item row: the entry
        std::string vis;            // eye / lock key (Editor::sceneHidden / sceneLocked) or "#areas" / "#regions"
        int dropOwner = -1000;      // drop target: map owner
        bool dim = false;           // game-created (read-only) marker
        float fx = 0, fz = 0;
        bool hasPos = false;
    };

    std::vector<std::string> groupingKeys(Editor& ed) const {
        if (grouping_ < kBuiltinGroupings) return kGroupings[grouping_].by;
        const auto& g = ed.worldConfig().groupings;
        const int k = grouping_ - kBuiltinGroupings;
        return k < int(g.size()) ? g[size_t(k)].by : std::vector<std::string>{"owner", "type"};
    }

    void buildEntries(Editor& ed) {
        entries_.clear();
        const auto& d = ed.doc().data;
        const auto& R = ed.rules();
        const WorldModel& W = ed.world();
        entries_.reserve(W.rows.size() + (ed.worldLive() ? 0 : d.entities.size()));
        if (!ed.worldLive()) {
            const bool fresh = ed.worldFresh();
            for (size_t i = 0; i < d.entities.size(); ++i) {
                const DocEntity& de = d.entities[i];
                Entry& en = entries_.emplace_back();
                WorldRow& row = en.own;
                if (const WorldRow* pr = fresh ? W.findDoc(int(i)) : nullptr) row = *pr;
                row.doc = int(i);
                row.origin = WorldOrigin::Map;
                row.owner = de.owner;
                row.type = R.findType(de.type);
                row.hasPos = true;
                row.x = de.x; row.z = de.z;
                if (row.type >= 0 && !fresh) {
                    const auto& t = R.types[size_t(row.type)];
                    row.kind = t.hero ? WorldKind::Hero : t.resourceNode >= 0 ? WorldKind::Resource : t.structure ? WorldKind::Structure : WorldKind::Unit;
                    row.category = worldKindTitle(row.kind);
                }
                en.sel = {Selection::Entity, int(i), {}};
                en.typeId = de.type;
                en.editable = true;
            }
        }
        for (auto& r : W.rows) {
            if (!ed.worldLive() && r.doc >= 0) continue;   // shown above from the document
            Entry& en = entries_.emplace_back();
            en.src = &r;
            en.sel = ed.worldSelection(r);
            if (r.type >= 0 && size_t(r.type) < R.types.size()) en.typeId = R.types[size_t(r.type)].id;
        }
    }

    std::string entryName(Editor& ed, const Entry& en) const {
        const auto& R = ed.rules();
        const auto& d = ed.doc().data;
        if (en.editable && en.row().type < 0) return (size_t(en.row().doc) < d.entities.size() ? d.entities[size_t(en.row().doc)].type : en.typeId) + " (?)";
        return worldRowName(ed.world(), R, &d, en.row());
    }

    // value + sort key of a grouping level
    std::pair<std::string, std::string> groupOf(Editor& ed, const Entry& en, const std::string& key) const {
        std::string v = worldGroupValue(ed.world(), ed.rules(), en.row(), key);
        if (key == "owner") {
            const auto& pl = ed.doc().data.players;   // names as the map document has them (also without a preview)
            if (en.row().owner > 0 && en.row().owner <= int(pl.size()) && !ed.worldLive()) v = pl[size_t(en.row().owner - 1)].name;   // players first, then neutral, then hostile
            const int o = en.row().owner;
            char buf[16];
            std::snprintf(buf, sizeof buf, "%04d", o > 0 ? o : o == 0 ? 9000 : 9001);
            return {v, buf};
        }
        return {v, v};
    }

    struct Node {
        std::string label, sortKey, key;
        std::string vis;
        int owner = -1000;   // owner group
        int type = -1;       // type group
        std::map<std::string, Node> children;   // by sort key
        std::vector<int> items;
        size_t count = 0;
        std::vector<int> all;
    };

    void addTo(Node& n, int e) { n.all.push_back(e); ++n.count; }

    void rebuild(Editor& ed) {
        const auto& d = ed.doc().data;
        const uint64_t key = (ed.doc().revision * 1000003ull) ^ (ed.world().serial * 7919ull) ^ (uint64_t(ed.worldLive()) << 60) ^
                             (uint64_t(ed.worldFresh()) << 61) ^ (uint64_t(grouping_) << 52) ^ (expandRev_ * 104729ull) ^ std::hash<std::string>{}(filter_);
        if (key == cacheKey_ && !rows_.empty()) return;
        cacheKey_ = key;
        auto prof = ed.prof.scope(EditorProfile::Hierarchy);
        buildEntries(ed);
        gameRows_ = 0;
        for (auto& r : ed.world().rows) gameRows_ += r.doc < 0;
        rows_.clear();
        const bool filtering = !filter_.empty();
        const auto& R = ed.rules();
        auto open = [&](const std::string& k) { return filtering || expanded_.count(k) > 0; };
        // root
        Row root;
        root.key = "map";
        root.label = (ed.worldLive() ? "Мир: " : "Карта: ") + d.name;
        root.icon = ICON_REGIONS;
        root.sel = {Selection::MapProps, -1, {}};
        root.count = entries_.size();
        root.showCount = true;
        if (!expanded_.count("!map")) expanded_.insert("map");   // open by default
        const bool rootOpen = open("map");
        rows_.push_back(root);
        if (!rootOpen) return;

        std::vector<int> matched;
        for (size_t i = 0; i < entries_.size(); ++i)
            if (!filtering || worldRowMatches(ed.world(), R, &d, entries_[i].row(), filter_)) matched.push_back(int(i));
        const std::vector<std::string> keys = groupingKeys(ed);
        if (keys.size() == 1 && keys[0] == "parent") buildNesting(ed, matched, open);
        else {
            Node top;
            top.key = "g" + std::to_string(grouping_);
            if (keys[0] == "owner" && !filtering) {   // every player shows, even without objects
                for (size_t p = 0; p < d.players.size(); ++p) {
                    Entry fake;
                    fake.own.owner = int(p + 1);
                    auto [v, sk] = groupOf(ed, fake, "owner");
                    Node& n = top.children[sk];
                    n.label = v; n.sortKey = sk; n.owner = int(p + 1);
                }
            }
            // values of a level depend on a few row fields: memoized per (level, fields), so thousands of objects
            // cost a map lookup each instead of building strings
            using GV = std::pair<std::string, std::string>;
            std::vector<std::map<uint64_t, GV>> memo(keys.size());
            std::vector<GV> scratch(keys.size()), comps;
            for (auto& c : worldComponents()) comps.push_back({c.title, c.title});
            const GV noComps[2] = {{"Объекты карты (предпросмотр не готов)", "~"}, {"Без компонентов", "~"}};
            auto memoKey = [](const std::string& k, const WorldRow& r, bool& ok) -> uint64_t {
                ok = true;
                if (k == "owner") return uint64_t(int64_t(r.owner) + 1000);
                if (k == "type" || k == "tag" || k == "category") return uint64_t(int64_t(r.type) + 1) << 8 | uint64_t(r.kind);
                if (k == "kind") return uint64_t(r.kind);
                if (k == "origin") return uint64_t(r.origin);
                if (k == "region") return uint64_t(int64_t(r.region) + 1);
                ok = false;
                return 0;
            };
            std::vector<std::vector<const GV*>> levels(keys.size());   // per level: values (component: several)
            for (int e : matched) {
                const Entry& en = entries_[size_t(e)];
                const WorldRow& row = en.row();
                for (size_t l = 0; l < keys.size(); ++l) {
                    auto& lv = levels[l];
                    lv.clear();
                    const std::string& k = keys[l];
                    if (k == "component") {
                        row.eachComp([&](int c) { lv.push_back(&comps[size_t(c)]); });
                        if (lv.empty()) lv.push_back(&noComps[en.editable ? 0 : 1]);
                        continue;
                    }
                    bool ok;
                    const uint64_t mk = memoKey(k, row, ok);
                    if (!ok) { scratch[l] = groupOf(ed, en, k); lv.push_back(&scratch[l]); continue; }
                    auto it = memo[l].find(mk);
                    if (it == memo[l].end()) it = memo[l].emplace(mk, groupOf(ed, en, k)).first;
                    lv.push_back(&it->second);
                }
                // walk every combination (only the component level has several values)
                std::vector<size_t> idx(levels.size(), 0);
                while (true) {
                    Node* n = &top;
                    for (size_t l = 0; l < levels.size(); ++l) {
                        const GV& g = *levels[l][idx[l]];
                        auto ci = n->children.find(g.second);
                        if (ci == n->children.end()) {
                            ci = n->children.emplace(g.second, Node{}).first;
                            Node& c = ci->second;
                            c.label = g.first; c.sortKey = g.second;
                            if (keys[l] == "owner") c.owner = row.owner;
                            if (keys[l] == "type") c.type = row.type;
                        } else if (ci->second.label.empty()) {   // a player row made above without objects
                            ci->second.label = g.first; ci->second.sortKey = g.second;
                        }
                        addTo(ci->second, e);
                        n = &ci->second;
                    }
                    n->items.push_back(e);
                    int l = int(levels.size()) - 1;
                    while (l >= 0 && ++idx[size_t(l)] >= levels[size_t(l)].size()) { idx[size_t(l)] = 0; --l; }
                    if (l < 0) break;
                }
            }
            emitNodes(ed, top, 1, "", open);
        }
        mapSections(ed, open);
        if (anchor_ >= int(rows_.size()) || (anchor_ >= 0 && rowOf(ed.selection) >= 0)) anchor_ = rowOf(ed.selection);
    }

    template <class Open>
    void emitNodes(Editor& ed, Node& parent, int depth, const std::string& path, Open&& open) {
        const auto& R = ed.rules();
        const bool flat = parent.children.empty();
        if (flat) {
            for (int e : parent.items) itemRow(ed, e, depth);
            return;
        }
        for (auto& [sk, n] : parent.children) {
            Row r;
            r.depth = depth;
            r.key = path + "/" + sk;
            r.label = n.label;
            r.count = n.count;
            r.showCount = true;
            r.members = n.all;
            const std::string ownerKey = parent.owner != -1000 ? "o" + std::to_string(parent.owner) + "/" : "";
            if (n.owner != -1000) {
                r.dot = n.owner > 0;
                r.icon = n.owner == 0 ? ICON_TREES : n.owner < 0 ? ICON_SWORDS : "";
                if (n.owner > 0) {
                    const render::float4 c = game::RtsView::playerColor(n.owner);
                    r.iconColor = ImVec4(c.x, c.y, c.z, 1);
                    const auto& pl = ed.doc().data.players;
                    if (n.owner <= int(pl.size())) {
                        if (!ed.worldLive()) r.sel = {Selection::Player, n.owner - 1, {}};
                        if (pl[size_t(n.owner - 1)].waves) r.label += "  (волны)";
                    }
                }
                r.vis = "o" + std::to_string(n.owner);
                r.dropOwner = n.owner;
            } else if (n.type >= 0 && size_t(n.type) < R.types.size()) {
                r.icon = R.types[size_t(n.type)].structure ? ICON_CASTLE : ICON_OBJECTS;
                r.vis = ownerKey + "t" + R.types[size_t(n.type)].id;
                r.tip = R.types[size_t(n.type)].id;
            } else r.icon = ICON_LAYERS;
            // children of n inherit its owner for "o<owner>/t<type>" keys
            n.owner = n.owner != -1000 ? n.owner : parent.owner;
            const bool o = open(r.key);
            rows_.push_back(std::move(r));
            if (o) emitNodes(ed, n, depth + 1, rows_.back().key, open);
        }
        for (int e : parent.items) itemRow(ed, e, depth);
    }

    void itemRow(Editor&, int e, int depth, const std::string& key = {}) {
        const Entry& en = entries_[size_t(e)];
        const WorldRow& row = en.row();
        Row& r = rows_.emplace_back();
        r.depth = depth;
        r.key = key;
        r.entry = e;
        r.sel = en.sel;
        r.members = {e};
        r.dim = !en.editable;
        r.icon = row.kind == WorldKind::Structure ? ICON_CASTLE : row.kind == WorldKind::Projectile ? ICON_ZAP
               : row.kind == WorldKind::Effect ? ICON_FLAME : row.kind == WorldKind::Resource ? ICON_FIELDS : ICON_DOT;
        if (row.owner > 0) { const render::float4 c = game::RtsView::playerColor(row.owner); r.iconColor = ImVec4(c.x, c.y, c.z, 1); }
        r.hasPos = row.hasPos;
        r.fx = row.x; r.fz = row.z;
        // label / tooltip: when the row is first drawn (lists of thousands are clipped)
    }

    void itemText(Editor& ed, Row& r) {
        const Entry& en = entries_[size_t(r.entry)];
        const WorldRow& row = en.row();
        const std::string name = entryName(ed, en);
        char pos[48] = "";
        if (row.hasPos) std::snprintf(pos, sizeof pos, "(%.0f, %.0f)", row.x, row.z);
        r.tip = name + "  " + pos + "\n" + (en.editable ? std::string("Объект карты") : std::string(worldOriginTitle(row.origin)) + " — только чтение") +
                (row.parent ? std::string(", ") + row.parentRel : "") + "\n#" + std::to_string(row.e.index);
        r.label = name + "  " + pos;
    }

    template <class Open>
    void buildNesting(Editor& ed, const std::vector<int>& matched, Open&& open) {
        std::map<uint64_t, std::vector<int>> kids;
        std::set<uint64_t> present;
        for (int e : matched) present.insert(entries_[size_t(e)].row().ref);
        std::vector<int> top;
        for (int e : matched) {
            const auto& r = entries_[size_t(e)].row();
            if (r.parent && present.count(r.parent)) kids[r.parent].push_back(e);
            else top.push_back(e);
        }
        std::function<void(int, int)> emit = [&](int e, int depth) {
            const uint64_t ref = entries_[size_t(e)].row().ref;
            auto it = kids.find(ref);
            const std::string k = it != kids.end() && ref ? "p/" + std::to_string(ref) : "";
            itemRow(ed, e, depth, k);
            if (it != kids.end() && ref) {
                rows_.back().count = it->second.size();
                rows_.back().showCount = true;
                if (open(k)) for (int c : it->second) emit(c, depth + 1);
            }
        };
        for (int e : top) emit(e, 1);
    }

    template <class Open>
    void mapSections(Editor& ed, Open&& open) {
        const auto& d = ed.doc().data;
        if (ed.worldLive()) return;   // the map document nodes are edited in edit mode
        auto section = [&](const char* icon, const std::string& label, const std::string& key, size_t count, const std::string& vis = {}) {
            if (!count) return false;
            Row r;
            r.depth = 1; r.key = key; r.label = label; r.icon = icon; r.count = count; r.showCount = true; r.vis = vis;
            rows_.push_back(r);
            return open(key);
        };
        auto leaf = [&](const char* icon, const std::string& label, Selection s, bool hasPos = false, float x = 0, float z = 0) {
            if (!filter_.empty() && !ui::contains(label, filter_)) return;
            Row r;
            r.depth = 2; r.label = label; r.icon = icon; r.sel = std::move(s); r.hasPos = hasPos; r.fx = x; r.fz = z;
            rows_.push_back(std::move(r));
        };
        size_t starts = 0;
        for (auto& p : d.players) starts += p.hasStart;
        if (section(ICON_STARTS, "Старты игроков", "#starts", starts))
            for (size_t p = 0; p < d.players.size(); ++p)
                if (d.players[p].hasStart) leaf(ICON_MAP_PIN, d.players[p].name, {Selection::Player, int(p), {}}, true, d.players[p].startX, d.players[p].startZ);
        const Json& areas = d.extra["areas"];
        if (section(ICON_ZONES, "Зоны", "#areas", areas.size(), "#areas"))
            for (auto& [name, a] : areas.members())
                leaf(ICON_ZONES, name, {Selection::Area, -1, name}, true, float(a[0].toDouble() + a[2].toDouble() / 2), float(a[1].toDouble() + a[3].toDouble() / 2));
        std::vector<char> letters;
        for (char c : d.regions) if (c != '.' && std::find(letters.begin(), letters.end(), c) == letters.end()) letters.push_back(c);
        std::sort(letters.begin(), letters.end());
        if (section(ICON_REGIONS, "Регионы", "#regions", letters.size(), "#regions"))
            for (char c : letters) leaf(ICON_REGIONS, std::string(1, c) + (d.regionNames.count(c) ? " — " + d.regionNames.at(c) : ""), {Selection::Region, -1, std::string(1, c)});
        const Json& waves = d.extra["waves"];
        if (section(ICON_WAVES, "Волны", "#waves", waves.size()))
            for (size_t i = 0; i < waves.size(); ++i) {
                const Json& w = waves[i];
                leaf(ICON_WAVES, "Волна " + std::to_string(i + 1) + "  ·  " + std::to_string(w["time"].integer()) + " с", {Selection::Wave, int(i), {}},
                     w["at"].isArray(), float(w["at"][0].toDouble()), float(w["at"][1].toDouble()));
            }
        const Json& trig = d.extra["triggers"];
        if (section(ICON_ZAP, "Триггеры карты", "#triggers", trig.size()))
            for (size_t i = 0; i < trig.size(); ++i) leaf(ICON_ZAP, trig[i].getStr("id", "trigger" + std::to_string(i)), {Selection::Trigger, int(i), {}});
        if (section(ICON_ROADS, "Дороги", "#roads", d.roads.size()))
            for (size_t i = 0; i < d.roads.size(); ++i) {
                auto nm = [&](int k) { const auto& e = d.entities[size_t(k)]; return e.id.empty() ? e.type + " #" + std::to_string(k) : e.id; };
                leaf(ICON_ROADS, nm(d.roads[i].first) + " — " + nm(d.roads[i].second), {Selection::Road, int(i), {}});
            }
        if (section(ICON_GLOBE, "Тела (планеты)", "#bodies", d.bodies.size()))
            for (size_t i = 0; i < d.bodies.size(); ++i)
                leaf(ICON_GLOBE, d.bodies[i].id + (d.bodies[i].surface() ? "  · грань " + std::to_string(d.bodies[i].faceSize) : std::string("  · без поверхности")),
                     {Selection::Body, int(i), {}});
        // every other block of the map (lights, gridFields, fx, script data, ...) - generic, edited as JSON
        static const std::set<std::string> known = {"areas", "waves", "triggers", "victory"};
        size_t blocks = 0;
        for (auto& [k, v] : d.extra.members()) blocks += !known.count(k) && !k.empty() && k[0] != '$';
        if (section(ICON_BRACES, "Другие блоки карты", "#blocks", blocks))
            for (auto& [k, v] : d.extra.members()) {
                if (known.count(k) || k.empty() || k[0] == '$') continue;
                if (v.isArray() && v.size() > 0 && v[0].isObject() && v.size() <= 200) {
                    Row r;
                    r.depth = 2; r.key = "#block/" + k; r.label = k; r.icon = ICON_BRACES; r.count = v.size(); r.showCount = true;
                    r.sel = {Selection::MapBlock, -1, k};
                    rows_.push_back(r);
                    if (open(r.key))
                        for (size_t i = 0; i < v.size(); ++i) {
                            const std::string nm = v[i].getStr("id", v[i].getStr("name", k + "[" + std::to_string(i) + "]"));
                            Row l;
                            l.depth = 3; l.label = nm; l.icon = ICON_DOT; l.sel = {Selection::MapBlock, -1, k + "[" + std::to_string(i) + "]"};
                            const Json& at = v[i]["at"].isArray() ? v[i]["at"] : v[i]["pos"];
                            if (at.isArray()) { l.hasPos = true; l.fx = float(at[0].toDouble()); l.fz = float(at[at.size() > 2 ? 2 : 1].toDouble()); }
                            rows_.push_back(l);
                        }
                } else leaf(ICON_BRACES, k, {Selection::MapBlock, -1, k});
            }
    }

    // ---------------------------------------------------------------- drawing
    void toolbar(Editor& ed) {
        const float bw = ImGui::GetFrameHeight();
        const float clear = filter_.empty() ? 0 : bw + ImGui::GetStyle().ItemInnerSpacing.x;
        if (ui::searchField("##filter", filter_, "Тип, id, игрок, t: p: c: r: d: #", std::max(40.0f, ImGui::GetContentRegionAvail().x - (bw + 4) * 3 - clear))) cacheKey_ = 0;
        if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
            ImGui::TextUnformatted("Поиск: слова через пробел (все должны совпасть).\n"
                                   "t:тип  p:игрок  c:компонент  r:регион  d:данные скрипта  #номер или id карты");
            ImGui::EndTooltip();
        }
        ImGui::SameLine(0, 4);
        const auto& custom = ed.worldConfig().groupings;
        if (ui::toolButton(ICON_LIST_TREE, "Группировка")) ImGui::OpenPopup("##grouping");
        if (ImGui::BeginPopup("##grouping")) {
            ImGui::TextDisabled("Группировать");
            for (int g = 0; g < kBuiltinGroupings + int(custom.size()); ++g) {
                const char* t = g < kBuiltinGroupings ? kGroupings[g].title : custom[size_t(g - kBuiltinGroupings)].title.c_str();
                if (ImGui::MenuItem(t, nullptr, grouping_ == g)) { grouping_ = g; cacheKey_ = 0; }
            }
            ImGui::Separator();
            ImGui::MenuItem("Объекты игры в «Сцене»", nullptr, &ed.showGameObjects);
            ImGui::MenuItem("Обновлять предпросмотр сам", nullptr, &ed.worldAuto);
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 4);
        if (ui::toolButton(ICON_REFRESH, ed.worldLive() ? "Мир игры читается из идущего теста" : "Пересобрать предпросмотр мира (установка карты, скрипты, модули)",
                           false, !ed.worldLive()))
            ed.refreshWorld();
        ImGui::SameLine(0, 4);
        if (ui::toolButton(ICON_PLUS, "Добавить волну, триггер, зону")) ImGui::OpenPopup("##add");
        if (ImGui::BeginPopup("##add")) { addMenu(ed); ImGui::EndPopup(); }
    }

    void statusLine(Editor& ed) {
        const auto& P = ed.worldPreview();
        if (ed.worldLive()) {
            const Simulation* s = const_cast<Editor&>(ed).worldSim();
            ui::toneText(ui::Tone::Accent, "%s Живой мир · тик %u · объектов %zu", ICON_ACTIVITY, s ? s->tick() : 0u, ed.world().rows.size());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Читается 5 раз в секунду, пока открыта «Иерархия».\n%s", ed.prof.line().c_str());
        } else if (!P.error().empty()) {
            ui::toneText(ui::Tone::Warn, "%s Предпросмотр: %s", ICON_WARNING, P.error().c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", P.error().c_str());
        } else if (ed.worldPending()) {
            ui::toneText(ui::Tone::Dim, "%s %s", ICON_CLOCK, ed.worldAuto ? "Обновляю объекты игры…" : "Объекты игры устарели — обновите (↻)");
        } else if (P.sim()) {
            ui::toneText(ui::Tone::Dim, "%s На карте %zu · создаёт игра %zu", ICON_CHECK, ed.doc().data.entities.size(), gameRows_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Игра установлена в отдельной копии (%.0f мс, в фоне): карта, скрипты, модули. Объекты, созданные игрой, отмечены и только для чтения.\n%s",
                                  P.buildMs(), ed.prof.line().c_str());
        }
    }

    bool rowSelected(Editor& ed, const Row& r) {
        if (r.sel.kind != Selection::None) return ed.isSelected(r.sel);
        if (r.members.empty() || ed.selected().size() < r.members.size()) return false;
        for (int e : r.members) if (!ed.isSelected(entries_[size_t(e)].sel)) return false;
        return true;
    }

    void drawRow(Editor& ed, int i, float rowH) {
        Row& r = rows_[size_t(i)];
        if (r.entry >= 0 && r.label.empty()) itemText(ed, r);
        ImGui::PushID(i);
        const float indent = ImGui::GetFontSize() * 0.9f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const bool sel = rowSelected(ed, r);
        const bool hoverSync = r.sel.kind != Selection::None && ed.hover == r.sel;   // the object under the cursor in the Scene
        if (renaming_ && renameRow_ == r.key + "|" + r.label) { renameField(ed, r, p, indent, rowH); ImGui::PopID(); return; }
        ImGui::Selectable("##row", sel || hoverSync, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            if (r.sel.kind == Selection::Entity || r.sel.kind == Selection::Live) ed.setHover(r.sel);
            if (!r.tip.empty() || truncated(r, w, indent)) ImGui::SetItemTooltip("%s", r.tip.empty() ? r.label.c_str() : (r.label + "\n" + r.tip).c_str());
        }
        // drag objects onto a player
        if (r.entry >= 0 && entries_[size_t(r.entry)].editable && ImGui::BeginDragDropSource()) {
            dragged_ = ed.isSelected(r.sel) ? ed.selectedEntities() : std::vector<int>{r.sel.index};
            ImGui::SetDragDropPayload("RTS_MAP_ENTITIES", "x", 1);
            ImGui::Text("%s  %zu → игроку", ICON_MOVE, dragged_.size());
            ImGui::EndDragDropSource();
        }
        if (r.dropOwner != -1000 && !ed.worldLive() && ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload("RTS_MAP_ENTITIES")) {
                worldedit::setOwner(ed.doc(), dragged_, r.dropOwner);
                ed.status = "Владелец: " + r.label + " (" + std::to_string(dragged_.size()) + ")";
            }
            ImGui::EndDragDropTarget();
        }
        // click: arrow toggles, row selects
        const float arrowX = p.x + indent * float(r.depth);
        const bool expandable = !r.key.empty();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            const float mx = ImGui::GetIO().MousePos.x;
            if (expandable && mx < arrowX + indent) toggle(r.key);
            else click(ed, i);
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (r.hasPos) ed.focusScene(r.fx, r.fz, 18);
            else if (expandable) toggle(r.key);
            else if (!r.members.empty()) focusMembers(ed, r.members);
        }
        if (ImGui::BeginPopupContextItem("##rowctx")) { rowMenu(ed, r); ImGui::EndPopup(); }
        // content: arrow, icon / dot, label (ellipsis), eye / lock, count
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (focused_ && i == anchor_ && !sel)   // keyboard cursor on a row that selects nothing (a group)
            dl->AddRect(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImGuiCol_NavCursor), 3);
        const float cy = p.y + rowH * 0.5f, th = ImGui::GetTextLineHeight();
        const ImU32 textCol = ImGui::GetColorU32(r.dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
        if (expandable) dl->AddText(ImVec2(arrowX, cy - th * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), open(r.key) ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT);
        float x = arrowX + indent;
        if (r.dot) {
            dl->AddCircleFilled(ImVec2(x + th * 0.3f, cy), th * 0.28f, ImGui::GetColorU32(r.iconColor));
            x += th * 0.9f;
        } else if (r.icon && *r.icon) {
            dl->AddText(ImVec2(x, cy - th * 0.5f), r.iconColor.w > 0 ? ImGui::GetColorU32(r.iconColor) : ImGui::GetColorU32(ImGuiCol_TextDisabled), r.icon);
            x += ImGui::CalcTextSize(r.icon).x + 6;
        }
        float right = p.x + w - 4;
        if (r.showCount) {
            char c[32];
            std::snprintf(c, sizeof c, r.entry >= 0 || r.depth == 0 || r.key[0] == '#' || r.dropOwner != -1000 ? "%zu" : "×%zu", r.count);
            const float cw = ImGui::CalcTextSize(c).x;
            dl->AddText(ImVec2(right - cw, cy - th * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), c);
            right -= cw + 8;
        }
        if (!r.vis.empty()) right = visButtons(ed, r, right, cy, hovered);
        if (r.dim) {   // created by the game: read-only marker
            dl->AddText(ImVec2(right - ImGui::CalcTextSize(ICON_SPARKLES).x, cy - th * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_SPARKLES);
            right -= ImGui::CalcTextSize(ICON_SPARKLES).x + 6;
        }
        const std::string shown = ellipsize(r.label, right - x);
        dl->AddText(ImVec2(x, cy - th * 0.5f), textCol, shown.c_str());
        ImGui::PopID();
    }

    bool truncated(const Row& r, float w, float indent) const { return ImGui::CalcTextSize(r.label.c_str()).x > w - indent * float(r.depth + 2) - 60; }

    // eye / lock at the right of a group row: shown on hover or while active
    float visButtons(Editor& ed, const Row& r, float right, float cy, bool hovered) {
        bool hidden, locked = false;
        const bool section = r.vis[0] == '#';
        if (section) hidden = r.vis == "#areas" ? !ed.showAreas : !ed.showRegions;
        else { hidden = ed.sceneHidden.count(r.vis) > 0; locked = ed.sceneLocked.count(r.vis) > 0; }
        const float bw = ImGui::GetTextLineHeight() + 4;
        auto button = [&](const char* id, const char* icon, bool on, const char* tip) {
            right -= bw;
            if (!hovered && !on) return false;
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(right, cy - bw * 0.5f));
            const bool pressed = ImGui::InvisibleButton(id, ImVec2(bw, bw));
            const bool h = ImGui::IsItemHovered();
            if (h) ImGui::SetTooltip("%s", tip);
            ImGui::GetWindowDrawList()->AddText(ImVec2(right + 2, cy - ImGui::GetTextLineHeight() * 0.5f),
                                                ImGui::GetColorU32(on ? ui::accentColor() : h ? ImGui::GetStyle().Colors[ImGuiCol_Text] : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]), icon);
            ImGui::SetCursorScreenPos(save);
            return pressed;
        };
        if (!section && button("##lock", locked ? ICON_LOCK : ICON_UNLOCK, locked, locked ? "Разблокировать в «Сцене»" : "Заблокировать в «Сцене» (нельзя выбрать и сдвинуть)")) {
            if (locked) ed.sceneLocked.erase(r.vis); else ed.sceneLocked.insert(r.vis);
        }
        if (button("##eye", hidden ? ICON_EYE_OFF : ICON_EYE, hidden, hidden ? "Показать в «Сцене»" : "Скрыть в «Сцене»")) {
            if (section) { bool& f = r.vis == "#areas" ? ed.showAreas : ed.showRegions; f = !f; }
            else if (hidden) ed.sceneHidden.erase(r.vis); else ed.sceneHidden.insert(r.vis);
        }
        return right - 4;
    }

    void renameField(Editor& ed, const Row& r, ImVec2 p, float indent, float rowH) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + indent * float(r.depth + 1), p.y));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (renameFocus_) { ImGui::SetKeyboardFocusHere(); renameFocus_ = false; }
        const bool enter = ui::inputString("##rename", renameText_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (enter || ImGui::IsItemDeactivatedAfterEdit()) applyRename(ed);
        // the field takes focus a frame later: close it once it was active and lost focus (or never got it)
        if (ImGui::IsItemActive()) renameActive_ = true;
        else if (renameActive_ || ++renameWait_ > 3) renaming_ = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) renaming_ = false;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
        ImGui::Dummy(ImVec2(0, 0));
    }

    // ---------------------------------------------------------------- selection
    void click(Editor& ed, int i) {
        const Row& r = rows_[size_t(i)];
        ImGuiIO& io = ImGui::GetIO();
        const bool toggleKey = io.KeyCtrl || io.KeySuper, rangeKey = io.KeyShift;
        std::vector<Selection> sels;
        if (r.sel.kind != Selection::None && !(rangeKey && anchor_ >= 0)) sels.push_back(r.sel);
        else for (int e : r.members) sels.push_back(entries_[size_t(e)].sel);
        if (rangeKey && anchor_ >= 0 && anchor_ < int(rows_.size())) {   // Shift: every object row between the anchor and here
            sels.clear();
            for (int k = std::min(anchor_, i); k <= std::max(anchor_, i); ++k)
                if (rows_[size_t(k)].entry >= 0 || rows_[size_t(k)].sel.kind != Selection::None) sels.push_back(rows_[size_t(k)].sel);
            if (toggleKey) for (auto& s : sels) ed.selectAdd(s);
            else ed.selectMany(sels);
            mine_ = ed.selection;
            return;
        }
        if (toggleKey) {
            if (sels.size() == 1) ed.selectToggle(sels[0]);
            else for (auto& s : sels) ed.selectAdd(s);
        } else if (sels.size() == 1 && r.sel.kind != Selection::None) ed.select(sels[0]);
        else ed.selectMany(sels);
        anchor_ = i;
        mine_ = ed.selection;
        renaming_ = false;
    }

    // a selection made elsewhere (Scene, Inspector, agents): open its groups and scroll to it
    void reveal(Editor& ed) {
        if (ed.selection == mine_ || ed.selection.kind == Selection::None) { mine_ = ed.selection; return; }
        mine_ = ed.selection;
        int entry = -1;
        for (size_t e = 0; e < entries_.size(); ++e) if (entries_[e].sel == ed.selection) { entry = int(e); break; }
        if (entry >= 0) {
            // open the groups along its path in the current grouping
            const auto keys = groupingKeys(ed);
            if (!(keys.size() == 1 && keys[0] == "parent")) {
                std::string path;
                for (auto& k : keys) {
                    std::string sk = k == "component" ? (entries_[size_t(entry)].row().firstComp() < 0 ? "~" : worldComponents()[size_t(entries_[size_t(entry)].row().firstComp())].title)
                                                      : groupOf(ed, entries_[size_t(entry)], k).second;
                    path += "/" + sk;
                    expanded_.insert(path);
                }
            } else {
                for (uint64_t p = entries_[size_t(entry)].row().parent; p;) {
                    expanded_.insert("p/" + std::to_string(p));
                    const WorldRow* pr = ed.world().find(p);
                    p = pr ? pr->parent : 0;
                }
            }
        } else {
            static const std::map<int, const char*> sec = {{Selection::Area, "#areas"}, {Selection::Region, "#regions"}, {Selection::Wave, "#waves"},
                                                           {Selection::Trigger, "#triggers"}, {Selection::Road, "#roads"}, {Selection::Body, "#bodies"}};
            if (auto it = sec.find(ed.selection.kind); it != sec.end()) expanded_.insert(it->second);
        }
        expanded_.insert("map");
        ++expandRev_;
        rebuild(ed);
        for (size_t k = 0; k < rows_.size(); ++k) if (rows_[k].sel == ed.selection) { scrollTo_ = int(k); anchor_ = int(k); break; }
    }

    bool open(const std::string& key) const { return !filter_.empty() || expanded_.count(key) > 0; }
    void toggle(const std::string& key) {
        if (expanded_.count(key)) { expanded_.erase(key); if (key == "map") expanded_.insert("!map"); }
        else { expanded_.insert(key); if (key == "map") expanded_.erase("!map"); }
        ++expandRev_;
    }

    int rowOf(const Selection& s) const {
        for (size_t k = 0; k < rows_.size(); ++k) if (rows_[k].sel == s) return int(k);
        return -1;
    }

    void focusMembers(Editor& ed, const std::vector<int>& members) {
        float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
        for (int e : members) {
            const auto& r = entries_[size_t(e)].row();
            if (!r.hasPos) continue;
            x0 = std::min(x0, r.x); z0 = std::min(z0, r.z); x1 = std::max(x1, r.x); z1 = std::max(z1, r.z);
        }
        if (x1 < x0) return;
        ed.focusScene((x0 + x1) * 0.5f, (z0 + z1) * 0.5f, std::clamp(std::max(x1 - x0, z1 - z0) * 1.2f + 14, 14.0f, 200.0f));
        ed.flash(x0 - 0.5f, z0 - 0.5f, x1 + 0.5f, z1 + 0.5f);
    }

    // ---------------------------------------------------------------- keyboard
    void keys(Editor& ed) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || renaming_ || rows_.empty()) return;
        const bool mod = io.KeyCtrl || io.KeySuper;
        int cur = anchor_ >= 0 && anchor_ < int(rows_.size()) ? anchor_ : rowOf(ed.selection);
        if (cur < 0) cur = 0;
        auto moveTo = [&](int k) {
            k = std::clamp(k, 0, int(rows_.size()) - 1);
            const Row& r = rows_[size_t(k)];
            if (r.sel.kind != Selection::None) ed.select(r.sel);
            else if (r.entry >= 0) ed.select(entries_[size_t(r.entry)].sel);
            mine_ = ed.selection;
            anchor_ = k;
            scrollTo_ = k;
        };
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) moveTo(cur + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) moveTo(cur - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !rows_[size_t(cur)].key.empty() && !open(rows_[size_t(cur)].key)) toggle(rows_[size_t(cur)].key);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            if (!rows_[size_t(cur)].key.empty() && open(rows_[size_t(cur)].key)) toggle(rows_[size_t(cur)].key);
            else for (int k = cur - 1; k >= 0; --k) if (rows_[size_t(k)].depth < rows_[size_t(cur)].depth) { moveTo(k); break; }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !mod) {
            float x, z;
            if (ed.selectionPos(ed.selection, x, z)) ed.focusScene(x, z, 18);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelection(ed);
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) startRename(ed, cur);
        if (mod && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelection(ed);
        if (mod && ImGui::IsKeyPressed(ImGuiKey_A, false)) {   // everything listed
            std::vector<Selection> all;
            for (auto& e : entries_) all.push_back(e.sel);
            ed.selectMany(all);
            mine_ = ed.selection;
        }
    }

    // ---------------------------------------------------------------- edits
    void deleteSelection(Editor& ed) {
        if (ed.worldLive()) return;
        Selection s = ed.selection;
        if (!ed.selectedEntities().empty()) { ed.removeSelectedEntities(); return; }
        auto& d = ed.doc().data;
        switch (s.kind) {
        case Selection::Wave: case Selection::Trigger: {
            Json* a = d.extra.find(s.kind == Selection::Wave ? "waves" : "triggers");
            if (a && s.index >= 0 && size_t(s.index) < a->size()) { ed.doc().beginEdit(); a = d.extra.find(s.kind == Selection::Wave ? "waves" : "triggers"); a->eraseAt(size_t(s.index)); }
            ed.select({});
            break;
        }
        case Selection::Area:
            if (Json* a = d.extra.find("areas"); a && a->has(s.key)) { ed.doc().beginEdit(); d.extra.find("areas")->erase(s.key); }
            ed.select({});
            break;
        case Selection::Road:
            if (s.index >= 0 && s.index < int(d.roads.size())) ed.doc().removeRoad(size_t(s.index));
            ed.select({});
            break;
        default: break;
        }
    }

    void duplicateSelection(Editor& ed) {
        if (ed.worldLive()) return;
        auto idx = ed.selectedEntities();
        auto& d = ed.doc().data;
        if (!idx.empty()) {
            auto copies = worldedit::duplicate(ed.doc(), idx);
            std::vector<Selection> v;
            for (int c : copies) v.push_back({Selection::Entity, c, {}});
            ed.selectMany(v);
            ed.status = "Дублировано: " + std::to_string(copies.size());
            return;
        }
        const Selection s = ed.selection;
        if (s.kind == Selection::Wave || s.kind == Selection::Trigger) {
            const char* key = s.kind == Selection::Wave ? "waves" : "triggers";
            const Json& a = d.extra[key];
            if (s.index < 0 || size_t(s.index) >= a.size()) return;
            Json copy = a[size_t(s.index)];
            if (s.kind == Selection::Trigger) copy.set("id", Json::string(uniqueTriggerId(d, copy.getStr("id", "trigger"))));
            ed.doc().beginEdit();
            d.extra.find(key)->push(copy);
            ed.select({s.kind, int(d.extra[key].size() - 1), {}});
        } else if (s.kind == Selection::Area) {
            const Json& areas = d.extra["areas"];
            if (!areas.has(s.key)) return;
            std::string name;
            for (int i = 2;; ++i) { name = s.key + "_" + std::to_string(i); if (!areas.has(name)) break; }
            Json a = areas[s.key];
            ed.doc().beginEdit();
            d.extra.find("areas")->set(name, a);
            ed.select({Selection::Area, -1, name});
        }
    }

    static std::string uniqueTriggerId(const MapData& d, const std::string& base) {
        for (int i = 2;; ++i) {
            std::string id = base + "_" + std::to_string(i);
            bool used = false;
            for (auto& t : d.extra["triggers"].items()) used |= t.getStr("id") == id;
            if (!used) return id;
        }
    }

    void startRename(Editor& ed, int row) {
        if (ed.worldLive() || row < 0 || row >= int(rows_.size())) return;
        const Row& r = rows_[size_t(row)];
        const auto& d = ed.doc().data;
        if (r.sel.kind == Selection::Entity && r.sel.index >= 0 && r.sel.index < int(d.entities.size())) renameText_ = d.entities[size_t(r.sel.index)].id;
        else if (r.sel.kind == Selection::Area) renameText_ = r.sel.key;
        else if (r.sel.kind == Selection::Trigger) renameText_ = d.extra["triggers"][size_t(r.sel.index)].getStr("id");
        else if (r.sel.kind == Selection::Player && r.sel.index >= 0 && r.sel.index < int(d.players.size())) renameText_ = d.players[size_t(r.sel.index)].name;
        else return;
        renaming_ = true;
        renameFocus_ = true;
        renameActive_ = false;
        renameWait_ = 0;
        renameSel_ = r.sel;
        renameRow_ = r.key + "|" + r.label;
        scrollTo_ = row;
    }

    void applyRename(Editor& ed) {
        renaming_ = false;
        auto& doc = ed.doc();
        auto& d = doc.data;
        const Selection s = renameSel_;
        if (s.kind == Selection::Entity && s.index >= 0 && s.index < int(d.entities.size())) {
            if (renameText_ == d.entities[size_t(s.index)].id) return;
            if (!renameText_.empty() && doc.findEntity(renameText_) >= 0) { ed.status = "id «" + renameText_ + "» уже занят"; return; }
            doc.beginEdit();
            d.entities[size_t(s.index)].id = renameText_;
        } else if (s.kind == Selection::Area) {
            Json* areas = d.extra.find("areas");
            if (!areas || renameText_.empty() || renameText_ == s.key || areas->has(renameText_)) return;
            doc.beginEdit();
            for (auto& [k, v] : d.extra.find("areas")->membersMut()) if (k == s.key) k = renameText_;
            ed.select({Selection::Area, -1, renameText_});
        } else if (s.kind == Selection::Trigger) {
            if (renameText_.empty()) return;
            doc.beginEdit();
            d.extra.find("triggers")->at(size_t(s.index)).set("id", Json::string(renameText_));
        } else if (s.kind == Selection::Player && s.index >= 0 && s.index < int(d.players.size()) && !renameText_.empty()) {
            doc.beginEdit();
            d.players[size_t(s.index)].name = renameText_;
        }
        cacheKey_ = 0;
    }

    std::vector<int> docMembers(const Row& r) const {
        std::vector<int> v;
        for (int e : r.members) if (entries_[size_t(e)].editable) v.push_back(entries_[size_t(e)].sel.index);
        std::sort(v.begin(), v.end());
        return v;
    }

    void ownerMenu(Editor& ed, const std::vector<int>& idx) {
        if (!ImGui::BeginMenu(ICON_USER "  Сменить владельца", !idx.empty() && !ed.worldLive())) return;
        const auto& d = ed.doc().data;
        if (ImGui::MenuItem("Нейтральные")) worldedit::setOwner(ed.doc(), idx, 0);
        if (ImGui::MenuItem("Враждебные монстры")) worldedit::setOwner(ed.doc(), idx, -1);
        for (size_t p = 0; p < d.players.size(); ++p) if (ImGui::MenuItem((d.players[p].name + "##o" + std::to_string(p)).c_str())) worldedit::setOwner(ed.doc(), idx, int(p + 1));
        ImGui::EndMenu();
    }

    void typeMenu(Editor& ed, const std::vector<int>& idx) {
        if (!ImGui::BeginMenu(ICON_OBJECTS "  Заменить тип", !idx.empty() && !ed.worldLive())) return;
        for (auto& t : ed.rules().types) if (ImGui::MenuItem((t.name + "##" + t.id).c_str())) worldedit::setType(ed.doc(), idx, t.id);
        ImGui::EndMenu();
    }

    void selectRows(Editor& ed, const std::vector<int>& members) {
        std::vector<Selection> v;
        for (int e : members) v.push_back(entries_[size_t(e)].sel);
        ed.selectMany(v);
        mine_ = ed.selection;
    }

    void rowMenu(Editor& ed, const Row& r) {
        auto& d = ed.doc().data;
        const bool edit = !ed.worldLive();
        if (r.entry >= 0) {   // one object (it joins the selection when it is not part of it)
            if (!ed.isSelected(r.sel)) { ed.select(r.sel); mine_ = ed.selection; }
            const Entry& en = entries_[size_t(r.entry)];
            const auto idx = ed.selectedEntities();
            if (r.hasPos && ui::menuItem(ICON_CROSSHAIR, "Показать в «Сцене»", "F")) ed.focusScene(r.fx, r.fz, 18);
            if (!en.typeId.empty()) {
                if (ui::menuItem(ICON_RULES, "Правила этого типа")) ed.select({Selection::Type, -1, en.typeId});
                if (ui::menuItem(ICON_SELECT, "Выделить все этого типа")) {
                    std::vector<int> m;
                    for (size_t e = 0; e < entries_.size(); ++e) if (entries_[e].typeId == en.typeId) m.push_back(int(e));
                    selectRows(ed, m);
                }
            }
            if (en.editable && edit) {
                ImGui::Separator();
                ownerMenu(ed, idx);
                typeMenu(ed, idx);
                if (ui::menuItem(ICON_COPY, "Дублировать", "Ctrl+D")) duplicateSelection(ed);
                if (ui::menuItem(ICON_PENCIL, "Задать id", "F2")) startRename(ed, rowOf(r.sel));
                if (ui::menuItem(ICON_TRASH, idx.size() > 1 ? "Удалить выбранные" : "Удалить", "Del")) ed.removeSelectedEntities();
            } else {
                ImGui::Separator();
                ImGui::TextDisabled("%s  %s — только чтение", ICON_SPARKLES, worldOriginTitle(en.row().origin));
                if (ui::menuItem(ICON_COPY, "Копировать ссылку (ref)")) ImGui::SetClipboardText(std::to_string(en.row().ref).c_str());
            }
            return;
        }
        if (r.sel.kind == Selection::Player || r.dropOwner != -1000 || (!r.members.empty() && r.sel.kind == Selection::None) || (r.depth == 0)) {
            if (r.depth == 0) { addMenu(ed); ImGui::Separator(); }
            if (!r.members.empty() && ui::menuItem(ICON_SELECT, "Выделить все объекты")) selectRows(ed, r.members);
            if (!r.members.empty() && ui::menuItem(ICON_CROSSHAIR, "Показать в «Сцене»")) focusMembers(ed, r.members);
            if (r.sel.kind == Selection::Player) {
                if (ui::menuItem(ICON_USER, "Свойства игрока")) ed.select(r.sel);
                if (edit && ui::menuItem(ICON_PENCIL, "Переименовать", "F2")) startRename(ed, rowOf(r.sel));
            }
            // type groups: rules of the type
            for (int e : r.members) {
                if (r.tip.empty() || entries_[size_t(e)].typeId != r.tip) break;
                if (ui::menuItem(ICON_RULES, "Правила этого типа")) ed.select({Selection::Type, -1, r.tip});
                break;
            }
            const auto idx = docMembers(r);
            if (edit && !idx.empty()) {
                ImGui::Separator();
                ownerMenu(ed, idx);
                typeMenu(ed, idx);
                if (ui::menuItem(ICON_COPY, "Дублировать все")) {
                    auto copies = worldedit::duplicate(ed.doc(), idx);
                    std::vector<Selection> v;
                    for (int c : copies) v.push_back({Selection::Entity, c, {}});
                    ed.selectMany(v);
                }
                char del[64];
                std::snprintf(del, sizeof del, "Удалить все (%zu)", idx.size());
                if (ui::menuItem(ICON_TRASH, del)) {
                    std::vector<Selection> v;
                    for (int i : idx) v.push_back({Selection::Entity, i, {}});
                    ed.selectMany(v);
                    ed.removeSelectedEntities();
                }
            }
            if (!r.vis.empty() && r.vis[0] != '#') {
                ImGui::Separator();
                const bool hidden = ed.sceneHidden.count(r.vis), locked = ed.sceneLocked.count(r.vis);
                if (ui::menuItem(hidden ? ICON_EYE : ICON_EYE_OFF, hidden ? "Показать в «Сцене»" : "Скрыть в «Сцене»")) { if (hidden) ed.sceneHidden.erase(r.vis); else ed.sceneHidden.insert(r.vis); }
                if (ui::menuItem(locked ? ICON_UNLOCK : ICON_LOCK, locked ? "Разблокировать" : "Заблокировать")) { if (locked) ed.sceneLocked.erase(r.vis); else ed.sceneLocked.insert(r.vis); }
            }
            return;
        }
        // map nodes: starts, areas, waves, triggers, roads, blocks
        if (r.sel.kind != Selection::None) {
            ed.select(r.sel);
            mine_ = ed.selection;
            if (r.hasPos && ui::menuItem(ICON_CROSSHAIR, "Показать в «Сцене»", "F")) ed.focusScene(r.fx, r.fz, 18);
            if (!edit) return;
            const bool dup = r.sel.kind == Selection::Wave || r.sel.kind == Selection::Trigger || r.sel.kind == Selection::Area;
            const bool ren = r.sel.kind == Selection::Trigger || r.sel.kind == Selection::Area;
            if (ren && ui::menuItem(ICON_PENCIL, "Переименовать", "F2")) startRename(ed, rowOf(r.sel));
            if (dup && ui::menuItem(ICON_COPY, "Дублировать", "Ctrl+D")) duplicateSelection(ed);
            if ((dup || r.sel.kind == Selection::Road) && ui::menuItem(ICON_TRASH, "Удалить", "Del")) deleteSelection(ed);
            if (r.sel.kind == Selection::Region && ui::menuItem(ICON_BRUSH, "Рисовать этот регион")) { ed.tool = Tool::Region; ed.regionLetter = r.sel.key[0]; }
            (void)d;
            return;
        }
        addMenu(ed);
    }

    void addMenu(Editor& ed) {
        const auto& rules = ed.rules();
        const bool edit = !ed.worldLive();
        if (ui::menuItem(ICON_WAVES, "Добавить волну", nullptr, false, edit)) {
            ed.doc().beginEdit();
            Json w = Json::parse(R"({"time": 60, "player": 2, "at": [10, 10], "spawn": [], "every": 0})");
            if (!rules.types.empty()) { Json s = Json::object(); s.set("type", Json::string(rules.types[0].id)); s.set("count", Json::number(int64_t(5))); w.find("spawn")->push(s); }
            if (!ed.doc().data.extra["waves"].isArray()) ed.doc().data.extra.set("waves", Json::array());
            ed.doc().data.extra.find("waves")->push(w);
            ed.select({Selection::Wave, int(ed.doc().data.extra["waves"].size() - 1), {}});
        }
        if (ui::menuItem(ICON_ZAP, "Добавить триггер", nullptr, false, edit)) {
            ed.doc().beginEdit();
            Json t = Json::parse(R"({"id": "new_trigger", "event": {"type": "time", "seconds": 30}, "actions": [{"type": "message", "text": "Прошло 30 секунд"}]})");
            if (!ed.doc().data.extra["triggers"].isArray()) ed.doc().data.extra.set("triggers", Json::array());
            ed.doc().data.extra.find("triggers")->push(t);
            ed.select({Selection::Trigger, int(ed.doc().data.extra["triggers"].size() - 1), {}});
        }
        if (ui::menuItem(ICON_ZONES, "Нарисовать зону", nullptr, false, edit)) ed.tool = Tool::Area;
        if (ui::menuItem(ICON_OBJECTS, "Поставить объекты", nullptr, false, edit)) ed.tool = Tool::Place;
    }

    std::string filter_;
    int grouping_ = 0;
    std::vector<Entry> entries_;
    std::vector<Row> rows_;
    std::set<std::string> expanded_;
    uint64_t cacheKey_ = 0, expandRev_ = 0;
    size_t gameRows_ = 0;       // objects the game created (edit mode status line)
    Selection mine_;            // last selection this window made or revealed
    int anchor_ = -1, scrollTo_ = -1;
    bool focused_ = false;
    std::vector<int> dragged_;
    bool renaming_ = false, renameFocus_ = false, renameActive_ = false;
    int renameWait_ = 0;
    std::string renameText_, renameRow_;
    Selection renameSel_;
};

RTS_EDITOR_WINDOW(HierarchyWindow);

} // namespace rts::editor
