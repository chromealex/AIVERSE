// SpectraFX graph authoring operations shared by the editor and MCP.
// All graph compilation is CPU-only and presentation-only (SpectraFX graph v1).
// Legacy fx/*.json and fx_preview continue to work; fx_graph_bake bridges them.
#include "Workspace.h"
#include "FxOps.h"
#include "engine/fx/SpectraFX.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::editor {
namespace {
namespace fs = std::filesystem;

Json strGraph(const std::string& value) { return Json::string(value); }
Json graphStrings(const std::vector<std::string>& values) {
    Json out = Json::array();
    for (const auto& value : values) out.push(strGraph(value));
    return out;
}
void requireGame(const Workspace& ws) {
    if (ws.project.dir.empty()) throw std::runtime_error("no game is open (open_game)");
}
std::string requireGraphName(const Json& p, const char* key = "name") {
    const std::string name = p.getStr(key);
    // Match the existing effect file naming contract; prevent path traversal.
    if (!validFxName(name) || name == "fx")
        throw std::runtime_error(std::string(key) + ": expected 1..48 lowercase letters, digits or underscores (not 'fx')");
    return name;
}
fs::path graphDir(const Workspace& ws) {
    return fs::path(ws.project.dir) / "fx" / "graphs";
}
fs::path graphFile(const Workspace& ws, const std::string& name) {
    return graphDir(ws) / (name + ".vfx.json");
}
fs::path effectFile(const Workspace& ws, const std::string& name) {
    return fs::path(ws.project.dir) / "fx" / (name + ".json");
}
std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.generic_string());
    constexpr std::streamsize maxBytes = 1024 * 1024;
    std::string result;
    result.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.eof() && input.bad()) throw std::runtime_error("read failed: " + path.generic_string());
    if (result.size() > size_t(maxBytes)) throw std::runtime_error("SpectraFX graph file exceeds 1 MiB");
    return result;
}
void writeFile(const fs::path& path, const std::string& text) {
    if (text.size() > 1024 * 1024) throw std::runtime_error("SpectraFX graph file exceeds 1 MiB");
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) throw std::runtime_error("cannot create SpectraFX directory: " + ec.message());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !(out << text) || !out.flush())
        throw std::runtime_error("cannot write " + path.generic_string());
}
Json sourceGraph(const Json& p, const Workspace& ws) {
    if (p.has("graph")) {
        if (!p["graph"].isObject()) throw std::runtime_error("graph must be an object");
        return p["graph"];
    }
    if (p.has("text")) {
        const std::string data = p.getStr("text");
        if (data.size() > 1024 * 1024) throw std::runtime_error("graph text exceeds 1 MiB");
        return Json::parse(data);
    }
    return Json::parse(readFile(graphFile(ws, requireGraphName(p))));
}
Json graphEvaluation(const Json& graph, const Json& overrides, fx::EffectDef* compiled = nullptr) {
    fx::EffectDef def;
    std::vector<std::string> errors;
    const bool ok = spectra::compileGraph(graph, overrides, def, errors);
    Json response = Json::object();
    response.set("ok", Json::fromBool(ok));
    response.set("errors", graphStrings(errors));
    if (ok) {
        response.set("emitters", Json::number(int64_t(def.emitters.size())));
        response.set("looping", Json::fromBool(def.looping()));
        if (!def.looping()) response.set("lifetime", Json::number(double(def.lifetime()), 3));
        std::set<std::string> textures;
        for (const auto& e : def.emitters) {
            if (!e.texture.empty()) textures.insert(e.texture);
            if (e.trail.enabled && !e.trail.texture.empty()) textures.insert(e.trail.texture);
        }
        response.set("textures", graphStrings({textures.begin(), textures.end()}));
        if (compiled) *compiled = std::move(def);
    }
    return response;
}
void noteGraph(Workspace& ws, const std::string& text) {
    // Existing editor live-reload watcher subscribes to Change::Fx.
    ws.note({Change::Fx, 0, 0, -1, -1, -1, text});
}
} // namespace

void registerSpectraGraphOps(Workspace& ws) {
    auto add = [&ws](const char* name, const char* description, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, description, Json::parse(schema), std::move(fn)});
    };

    add("fx_graph_list", "List SpectraFX graphs (fx/graphs/*.vfx.json) with compiler diagnostics and emitter counts.",
        R"({"properties":{}})", [&ws](const Json&) {
        requireGame(ws);
        std::vector<std::string> names;
        std::error_code ec;
        for (fs::directory_iterator it(graphDir(ws), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string file = it->path().filename().string();
            const std::string suffix = ".vfx.json";
            if (file.size() <= suffix.size() || file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            const std::string name = file.substr(0, file.size() - suffix.size());
            if (validFxName(name) && name != "fx") names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        Json list = Json::array();
        for (const auto& name : names) {
            Json item = Json::object();
            item.set("name", strGraph(name));
            try {
                Json state = graphEvaluation(Json::parse(readFile(graphFile(ws, name))), Json::object());
                for (const auto& [key, value] : state.members()) item.set(key, value);
            } catch (const std::exception& e) {
                item.set("ok", Json::fromBool(false));
                item.set("errors", graphStrings({e.what()}));
            }
            list.push(std::move(item));
        }
        Json response = Json::object();
        response.set("graphs", std::move(list));
        response.set("summary", strGraph("SpectraFX graphs: " + std::to_string(names.size())));
        return response;
    });

    add("fx_graph_get", "Read an existing SpectraFX graph asset as raw text and parsed JSON.",
        R"({"properties":{"name":{"type":"string"}},"required":["name"]})", [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        const std::string content = readFile(graphFile(ws, name));
        Json response = Json::object();
        response.set("name", strGraph(name));
        response.set("text", strGraph(content));
        try {
            const Json graph = Json::parse(content);
            response.set("graph", graph);
            response.set("validation", graphEvaluation(graph, Json::object()));
        } catch (const std::exception& e) {
            response.set("parseError", strGraph(e.what()));
        }
        return response;
    });

    add("fx_graph_validate", "Compile SpectraFX graph v1 without writing files. Accept a graph object, text or an existing name; optional exposed-parameter overrides.",
        R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"},"text":{"type":"string"},"overrides":{"type":"object"}}})",
        [&ws](const Json& p) {
        requireGame(ws);
        return graphEvaluation(sourceGraph(p, ws), p.has("overrides") ? p["overrides"] : Json::object());
    });

    add("fx_graph_set", "Create or replace fx/graphs/<name>.vfx.json after strict SpectraFX compiler validation; legacy effects are not changed.",
        R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"},"text":{"type":"string"}},"required":["name"]})",
        [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        if (!p.has("graph") && !p.has("text")) throw std::runtime_error("provide graph or text");
        const Json graph = sourceGraph(p, ws);
        Json response = graphEvaluation(graph, Json::object());
        response.set("name", strGraph(name));
        if (!response.getBool("ok")) return response; // never overwrite a valid graph with invalid input
        const std::string content = graph.dump(2) + "\n";
        const bool existed = fs::exists(graphFile(ws, name));
        writeFile(graphFile(ws, name), content);
        noteGraph(ws, (existed ? "SpectraFX graph updated: " : "SpectraFX graph created: ") + name);
        response.set("summary", strGraph((existed ? "Updated " : "Created ") + graphFile(ws, name).generic_string()));
        return response;
    });

    add("fx_graph_delete", "Delete a SpectraFX graph asset by name. Does not affect separately baked fx/*.json effects.",
        R"({"properties":{"name":{"type":"string"}},"required":["name"]})", [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        std::error_code ec;
        if (!fs::remove(graphFile(ws, name), ec)) {
            if (ec) throw std::runtime_error("cannot delete SpectraFX graph: " + ec.message());
            throw std::runtime_error("SpectraFX graph not found: " + name);
        }
        noteGraph(ws, "SpectraFX graph deleted: " + name);
        Json response = Json::object();
        response.set("ok", Json::fromBool(true));
        response.set("summary", strGraph("Deleted graph " + name));
        return response;
    });

    add("fx_graph_bake", "Compile a SpectraFX graph into legacy fx/<effect>.json so fx_preview and existing game/event bindings can use it. Refuses to replace an effect unless overwrite=true.",
        R"({"properties":{"name":{"type":"string"},"effect":{"type":"string"},"overrides":{"type":"object"},"overwrite":{"type":"boolean"}},"required":["name","effect"]})",
        [&ws](const Json& p) {
        requireGame(ws);
        const std::string name = requireGraphName(p);
        const std::string effect = requireGraphName(p, "effect");
        fx::EffectDef compiled;
        Json response = graphEvaluation(sourceGraph(p, ws),
            p.has("overrides") ? p["overrides"] : Json::object(), &compiled);
        if (!response.getBool("ok")) return response;
        const fs::path target = effectFile(ws, effect);
        if (fs::exists(target) && !p.getBool("overwrite"))
            throw std::runtime_error("fx/" + effect + ".json exists; set overwrite=true to replace");
        writeFile(target, fx::effectToJson(compiled).dump(2) + "\n");
        noteGraph(ws, "SpectraFX graph baked: " + name + " -> " + effect);
        response.set("effect", strGraph(effect));
        response.set("summary", strGraph("Baked " + name + " to fx/" + effect + ".json"));
        response.set("next", strGraph("fx_preview {\"effect\":\"" + effect + "\"}"));
        return response;
    });
}

} // namespace aiverse::editor
