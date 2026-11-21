#include "HumanCreatorOps.h"
#include "engine/character/vivid/HumanCreatorWorkbench.h"
#include "engine/character/vivid/HumanAppearance.h"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include <stdexcept>

namespace aiverse::editor {

namespace {

std::string projectDir(const Workspace& ws) {
    if (ws.project.dir.empty()) throw std::runtime_error("no game is open: call open_game first");
    return ws.project.dir;
}

vivid::HumanCreatorWorkbench loadHuman(const std::string& dir, const std::string& name) {
    vivid::HumanCreatorWorkbench human;
    std::string error;
    if (!human.loadDocument(vivid::readHumanCreator(dir, name), &error))
        throw std::runtime_error("humans/" + name + ".json: " + error);
    return human;
}

Json describe(const vivid::HumanCreatorWorkbench& human, const std::string& name, bool withCatalog) {
    Json r = Json::object();
    r.set("ok", Json::fromBool(true));
    r.set("name", Json::string(name));
    r.set("file", Json::string("humans/" + name + ".json"));
    r.set("document", human.toDocument());
    if (withCatalog) r.set("catalog", human.catalog());
    return r;
}

// Applies one human_edit request to a COPY; throws on the first invalid edit.
void applyEdits(vivid::HumanCreatorWorkbench& human, const Json& p, int& changes) {
    std::string error;
    auto fail = [&](const std::string& what) { throw std::invalid_argument(what + (error.empty() ? "" : ": " + error)); };
    if (p.has("preset")) {
        if (!human.preset(p.getStr("preset"), &error)) fail("preset");
        ++changes;
    }
    if (p.has("set")) {
        if (!p["set"].isObject()) fail("'set' must be an object of {key: value}");
        for (const auto& [key, value] : p["set"].members()) {
            if (!human.set(key, value, &error)) fail("set " + key);
            ++changes;
        }
    }
    if (p.has("routes")) {   // replaces the whole route list
        const auto& routes = p["routes"];
        if (!routes.isArray() || routes.size() > 256) fail("'routes' must be an array (max 256)");
        while (!human.routes().empty()) human.removeRoute(human.routes().size() - 1);
        for (size_t i = 0; i < routes.size(); ++i) {
            const auto& r = routes[i];
            const double gain = r["gain"].isNumber() ? r["gain"].toDouble() : 1.0;
            if (!human.setRoute(i, r.getStr("channel"), r.getStr("target"), gain, &error)) fail("route " + std::to_string(i));
        }
        ++changes;
    }
    if (p.has("rig")) {
        if (!p["rig"].isArray()) fail("'rig' must be an array of {node, translation:[x,y,z]}");
        for (const auto& e : p["rig"].items()) {
            const auto& t = e["translation"];
            if (!t.isArray() || t.size() != 3 || !t[0].isNumber() || !t[1].isNumber() || !t[2].isNumber())
                fail("rig translation must be [x,y,z]");
            if (!human.setRig(e.getStr("node"), {float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble())}, &error))
                fail("rig " + e.getStr("node"));
            ++changes;
        }
    }
    if (p.has("skin")) {
        if (!p["skin"].isArray()) fail("'skin' must be an array of {vertex, joint, weight} or {vertex, erase:true}");
        for (const auto& e : p["skin"].items()) {
            const auto& v = e["vertex"];
            if (!v.isNumber() || v.toDouble() < 0 || v.toDouble() > 4294967295.0 || v.toDouble() != double(v.integer()))
                fail("skin vertex must be a non-negative integer");
            const auto vertex = std::uint32_t(v.integer());
            if (e.getBool("erase")) {
                if (!human.eraseSkin(vertex)) fail("no skin edit on vertex " + std::to_string(vertex));
            } else {
                const double w = e["weight"].isNumber() ? e["weight"].toDouble() : -1;
                if (!human.setSkin(vertex, e.getStr("joint"), w, &error)) fail("skin vertex " + std::to_string(vertex));
            }
            ++changes;
        }
    }
}

// Studio Character Creator looks (human-looks/<name>.json): the appearance of
// an imported character model (skin, eyes, hair, body), shared with Studio.
std::filesystem::path lookFile(const std::string& dir, const std::string& name) {
    if (name.empty() || name.size() > 64 || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
        throw std::invalid_argument("invalid look name (A-Z a-z 0-9 _ -, up to 64)");
    const auto folder = std::filesystem::path(dir) / "human-looks";
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(folder))) throw std::runtime_error("human-looks cannot be a symlink");
    return folder / (name + ".json");
}
vivid::HumanAppearance readLook(const std::string& dir, const std::string& name) {
    const auto file = lookFile(dir, name);
    if (!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file) > 256 * 1024)
        throw std::runtime_error("human-looks/" + name + ".json not found");
    std::ifstream in(file, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    return vivid::HumanAppearance::fromJson(Json::parse(bytes));
}

} // namespace

void registerHumanCreatorOps(Workspace& ws) {
    Workspace* w = &ws;
    ws.registerOp({"human_schema",
        "VIVID digital-human authoring schema (the Studio 'Human Creator' window): sections skin, eyes, groom (hair), anatomy "
        "(face and body proportions), expression (52 ARKit-order rest channels), routes (channel -> morph target, gain 0..4), "
        "rig (bone names), rigCorrections and skinWeights. Each field has key (DNA path for human_edit 'set'), kind, min/max. "
        "Also presets, channel names and the reference human's rig nodes / morph targets / vertex count.",
        Json::parse("{}"), [](const Json&) {
            vivid::HumanCreatorWorkbench human;
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("schema", vivid::HumanCreatorWorkbench::schema());
            r.set("presets", Json::parse(R"(["neutral","warm","cool","stylized"])"));
            r.set("catalog", human.catalog());
            return r;
        }});
    ws.registerOp({"human_list", "Saved digital humans of the open game (humans/<name>.json).", Json::parse("{}"),
        [w](const Json&) {
            Json r = vivid::listHumanCreators(projectDir(*w));
            r.set("ok", Json::fromBool(true));
            return r;
        }});
    ws.registerOp({"human_get", "One saved digital human: its full document (dna, rigCorrections, skinWeights, routes) and the "
        "rig node / morph target names of the generated reference human.",
        Json::parse(R"({"properties": {"name": {"type": "string"}}, "required": ["name"]})"),
        [w](const Json& p) {
            const auto name = p.getStr("name");
            return describe(loadHuman(projectDir(*w), name), name, true);
        }});
    ws.registerOp({"human_create", "Create humans/<name>.json from a preset (neutral | warm | cool | stylized; default neutral). "
        "Fails if it exists unless overwrite:true. Edit it with human_edit; open it in Studio > Human Creator > Load to see it.",
        Json::parse(R"({"properties": {"name": {"type": "string", "description": "A-Z a-z 0-9 _ -, up to 64"},
            "preset": {"type": "string", "enum": ["neutral", "warm", "cool", "stylized"]}, "overwrite": {"type": "boolean"}},
            "required": ["name"]})"),
        [w](const Json& p) {
            const auto dir = projectDir(*w);
            const auto name = p.getStr("name");
            if (!vivid::validHumanCreatorName(name)) throw std::invalid_argument("invalid human name (A-Z a-z 0-9 _ -, up to 64)");
            if (!p.getBool("overwrite") && std::filesystem::exists(vivid::humanCreatorFolder(dir) / (name + ".json")))
                throw std::runtime_error("humans/" + name + ".json exists (pass overwrite:true)");
            vivid::HumanCreatorWorkbench human;
            std::string error;
            if (p.has("preset") && !human.preset(p.getStr("preset"), &error)) throw std::invalid_argument(error);
            vivid::saveHumanCreator(dir, name, human.toDocument());
            Json r = describe(human, name, false);
            r.set("summary", Json::string("Created human " + name));
            return r;
        }});
    ws.registerOp({"human_edit",
        "Edit a saved digital human in one all-or-nothing transaction (nothing is written if any edit is invalid), applied in "
        "this order: preset (resets the DNA), set {key: value} with keys from human_schema (e.g. 'skin.melanin': 0.6, "
        "'eyes.irisColor': [0.2,0.4,0.3], 'groom.lengthMeters': 0.3, 'anatomy.jawWidth': 1.1, 'expressionRest.24': 0.4, "
        "'rig.headBone': 'head'), routes (REPLACES the list: [{channel, target, gain}]), rig ([{node, translation:[x,y,z]}], "
        "|x|,|y|,|z| <= 1, [0,0,0] removes), skin ([{vertex, joint, weight}] or [{vertex, erase:true}]).",
        Json::parse(R"J({"properties": {"name": {"type": "string"},
            "preset": {"type": "string", "enum": ["neutral", "warm", "cool", "stylized"]},
            "set": {"type": "object", "description": "DNA path -> value (number, [r,g,b] colour, string bone, string[] optionalBones)"},
            "routes": {"type": "array", "items": {"type": "object", "properties": {"channel": {"type": "string"}, "target": {"type": "string"}, "gain": {"type": "number"}}}},
            "rig": {"type": "array", "items": {"type": "object", "properties": {"node": {"type": "string"}, "translation": {"type": "array", "items": {"type": "number"}}}}},
            "skin": {"type": "array", "items": {"type": "object", "properties": {"vertex": {"type": "integer"}, "joint": {"type": "string"}, "weight": {"type": "number"}, "erase": {"type": "boolean"}}}}},
            "required": ["name"]})J"),
        [w](const Json& p) {
            const auto dir = projectDir(*w);
            const auto name = p.getStr("name");
            auto human = loadHuman(dir, name);
            int changes = 0;
            applyEdits(human, p, changes);
            if (changes > 0) vivid::saveHumanCreator(dir, name, human.toDocument());
            Json r = describe(human, name, false);
            r.set("changes", Json::number(int64_t(changes)));
            r.set("summary", Json::string("Edited human " + name + " (" + std::to_string(changes) + " changes)"));
            return r;
        }});
    ws.registerOp({"human_look_schema",
        "Character Creator look fields for an imported character model (Studio > Character Creator: Skin, Eyes, Hair, Body): "
        "key, label, min, max, initial. A look is {version:1, values:{key:number}, materials:{sourceMaterial: role}}.",
        Json::parse("{}"), [](const Json&) {
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("schema", vivid::HumanAppearance::schema());
            return r;
        }});
    ws.registerOp({"human_look_list", "Saved Character Creator looks of the open game (human-looks/<name>.json); Studio shows "
        "them as preset cards.", Json::parse("{}"),
        [w](const Json&) {
            const auto folder = std::filesystem::path(projectDir(*w)) / "human-looks";
            Json names = Json::array();
            std::vector<std::string> sorted;
            if (std::filesystem::is_directory(folder))
                for (const auto& e : std::filesystem::directory_iterator(folder))
                    if (e.is_regular_file() && e.path().extension() == ".json") sorted.push_back(e.path().stem().string());
            std::sort(sorted.begin(), sorted.end());
            for (const auto& n : sorted) names.push(Json::string(n));
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("looks", names);
            return r;
        }});
    ws.registerOp({"human_look_get", "One saved Character Creator look with every value (defaults filled in).",
        Json::parse(R"({"properties": {"name": {"type": "string"}}, "required": ["name"]})"),
        [w](const Json& p) {
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("name", Json::string(p.getStr("name")));
            r.set("look", readLook(projectDir(*w), p.getStr("name")).toJson());
            return r;
        }});
    ws.registerOp({"human_look_save",
        "Write a Character Creator look (agent draft from a text description). 'values' {key: number} from human_look_schema "
        "are merged onto 'base' (another saved look) or the defaults; 'materials' maps source material -> role. Fails if the "
        "look exists unless overwrite:true. The artist then applies it in Studio > Character Creator > Presets.",
        Json::parse(R"({"properties": {"name": {"type": "string"}, "base": {"type": "string"},
            "values": {"type": "object"}, "materials": {"type": "object"}, "overwrite": {"type": "boolean"}}, "required": ["name"]})"),
        [w](const Json& p) {
            const auto dir = projectDir(*w);
            const auto name = p.getStr("name");
            const auto file = lookFile(dir, name);
            if (!p.getBool("overwrite") && std::filesystem::exists(file))
                throw std::runtime_error("human-looks/" + name + ".json exists (pass overwrite:true)");
            Json look = p.has("base") ? readLook(dir, p.getStr("base")).toJson() : vivid::HumanAppearance{}.toJson();
            if (p.has("values")) {
                if (!p["values"].isObject()) throw std::invalid_argument("'values' must be {key: number}");
                for (const auto& [k, v] : p["values"].members()) look.member("values").set(k, v);
            }
            if (p.has("materials")) {
                if (!p["materials"].isObject()) throw std::invalid_argument("'materials' must be {material: role}");
                for (const auto& [k, v] : p["materials"].members()) look.member("materials").set(k, v);
            }
            const auto checked = vivid::HumanAppearance::fromJson(look); // validates keys and limits
            std::filesystem::create_directories(file.parent_path());
            {
                std::ofstream out(file.string() + ".pending", std::ios::binary | std::ios::trunc);
                out << checked.toJson().dump(2) << "\n";
                if (!out) throw std::runtime_error("cannot write human-looks/" + name + ".json");
            }
            std::filesystem::rename(file.string() + ".pending", file);
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("name", Json::string(name));
            r.set("look", checked.toJson());
            r.set("summary", Json::string("Saved look " + name));
            return r;
        }});
    ws.registerOp({"studio_character_snapshot",
        "PNG of the character shown in the open Studio's Character Creator viewport (Studio must run with this game and "
        "the character shown). view: face | torso | full (default face). Returns the PNG path; open it to SEE your edit "
        "before reporting. Waits up to 15 s.",
        Json::parse(R"({"properties": {"view": {"type": "string", "enum": ["face", "torso", "full"]}}})"),
        [w](const Json& p) {
            namespace fs = std::filesystem;
            const fs::path root = projectDir(*w);
            const std::string view = p.has("view") ? p.getStr("view") : "face";
            if (view != "face" && view != "torso" && view != "full") throw std::invalid_argument("view: face | torso | full");
            static int serial = 0;
            const std::string id = "s" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(++serial);
            const fs::path requests = root / ".aiverse" / "agent-requests", out = root / ".aiverse" / "agent-snapshots";
            fs::create_directories(requests); fs::create_directories(out);
            Json req = Json::object(); req.set("id", Json::string(id)); req.set("view", Json::string(view));
            { std::ofstream f(requests / "character-snapshot.json.pending"); f << req.dump(); }
            fs::rename(requests / "character-snapshot.json.pending", requests / "character-snapshot.json");
            const auto png = out / (id + ".png"), err = out / (id + ".error");
            for (int i = 0; i < 75; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::error_code ec;
                if (fs::exists(err, ec)) {
                    std::ifstream in(err); std::string msg((std::istreambuf_iterator<char>(in)), {});
                    throw std::runtime_error("Studio: " + msg);
                }
                if (fs::is_regular_file(png, ec) && fs::file_size(png, ec) > 0) {
                    Json r = Json::object();
                    r.set("ok", Json::fromBool(true)); r.set("path", Json::string(png.string())); r.set("view", Json::string(view));
                    return r;
                }
            }
            std::error_code ec; fs::remove(requests / "character-snapshot.json", ec);
            throw std::runtime_error("Studio did not answer: open this game in AIVERSE Studio and show the character in Character Creator");
        }});
    ws.registerOp({"human_validate", "Check a digital-human document (as written by hand) without saving: version 1, dna limits, "
        "rig corrections, skin weights, routes. Returns ok or the first error.",
        Json::parse(R"({"properties": {"document": {"type": "object"}}, "required": ["document"]})"),
        [](const Json& p) {
            vivid::HumanCreatorWorkbench human;
            std::string error;
            Json r = Json::object();
            const bool ok = human.loadDocument(p["document"], &error);
            r.set("ok", Json::fromBool(ok));
            if (!ok) r.set("error", Json::string(error));
            return r;
        }});
}

} // namespace aiverse::editor
