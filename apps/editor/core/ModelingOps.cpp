#include "ModelingOps.h"
#include "tools/modeling/Modeling.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace aiverse::editor {
namespace {
namespace fs = std::filesystem;
using modeling::require;
struct Job {
    std::string id;
    int64_t revision = 0;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::future<Json> result;
    Json finished;
};
struct Service {
    modeling::GeometryCache cache;
    std::mutex mutex;
    std::vector<Job> jobs;
    uint64_t serial = 0;
    ~Service() {
        for (auto &j : jobs)
            j.cancel->store(true);
    }
};
fs::path path(const Workspace &ws, const std::string &name) {
    require(!ws.project.dir.empty(), "open a project first");
    require(!name.empty() && name.size() <= 64 &&
                name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                    std::string::npos,
            "name must contain 1..64 letters, digits, _ or -");
    auto root = fs::weakly_canonical(ws.project.dir);
    auto p = fs::weakly_canonical(root / "models" / "source" / (name + ".model.json"));
    auto rel = p.lexically_relative(root);
    require(!rel.empty() && *rel.begin() != "..", "model path escapes project");
    return p;
}
Json read(const fs::path &p) {
    require(fs::is_regular_file(p), "model document not found");
    require(fs::file_size(p) <= 40 * 1024 * 1024, "model history exceeds 40 MiB");
    auto state = Json::parseFile(p.string());
    require(state["document"].isObject() && state["revision"].isNumber(), "invalid authoring state");
    return state;
}
void atomicWrite(const fs::path &p, const std::string &bytes) {
    fs::create_directories(p.parent_path());
    auto tmp = p;
    tmp += ".writing";
    require(!fs::exists(tmp) && !fs::is_symlink(tmp), "temporary output exists; resolve interrupted write first");
    try {
        std::ofstream f(tmp, std::ios::binary);
        f.write(bytes.data(), std::streamsize(bytes.size()));
        f.close();
        require(bool(f), "failed to write asset");
#if defined(_WIN32)
        require(MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "failed to replace asset");
#else
        fs::rename(tmp, p);
#endif
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
}
struct Lock {
    fs::path p;
    explicit Lock(const fs::path &target) : p(target.string() + ".lock") {
        fs::create_directories(target.parent_path());
        require(fs::create_directory(p), "asset is busy; retry after the current operation");
    }
    ~Lock() {
        std::error_code ec;
        fs::remove(p, ec);
    }
};
void revision(const Json &state, const Json &p) {
    require(p["expectedRevision"].isNumber() && p["expectedRevision"].integer(-1) == state.getInt("revision"),
            "revision conflict: get the asset and retry with expectedRevision");
}
Json response(const Json &state) {
    auto r = Json::object();
    r.set("revision", state["revision"]);
    r.set("document", state["document"]);
    r.set("ok", Json::fromBool(true));
    return r;
}
Json commit(const fs::path &p, Json state, const Json &doc) {
    modeling::build(doc);
    state.member("undo").push(state["document"]);
    while (state["undo"].size() > 32)
        state.member("undo").eraseAt(0);
    state.set("redo", Json::array());
    state.set("document", doc);
    state.set("revision", Json::number(state.getInt("revision") + 1));
    while (state.dump(-1).size() > 32 * 1024 * 1024 && state["undo"].size())
        state.member("undo").eraseAt(0);
    atomicWrite(p, state.dump(2));
    return response(state);
}
std::string output(const Workspace &ws, const std::string &name) {
    auto source = path(ws, name);
    auto root = fs::weakly_canonical(ws.project.dir);
    auto dest = fs::weakly_canonical(source.parent_path().parent_path() / (name + ".glb"));
    auto rel = dest.lexically_relative(root);
    require(!rel.empty() && *rel.begin() != "..", "export path escapes project");
    return dest.string();
}
} // namespace
void registerModelingOps(Workspace &ws) {
    auto service = std::make_shared<Service>();
    auto schema = Json::parse(
        R"({"type":"object","properties":{"name":{"type":"string"},"expectedRevision":{"type":"integer"},"document":{"type":"object"},"commands":{"type":"array"},"preset":{"type":"string","enum":["empty","robot"]},"clip":{"type":"string"},"time":{"type":"number"},"fps":{"type":"integer"},"duration":{"type":"number"},"graph":{"type":"object"},"parameters":{"type":"object"},"type":{"type":"string"},"async":{"type":"boolean"},"jobId":{"type":"string"},"yaw":{"type":"number"},"pitch":{"type":"number"},"wireframe":{"type":"boolean"},"skeleton":{"type":"boolean"},"node":{"type":"string"},"skin":{"type":"string"},"prefix":{"type":"string"},"translation":{"type":"array"},"tracks":{"type":"array"},"target":{"type":"string"},"cell":{"type":"number"},"replaceView":{"type":"boolean"},"tab":{"type":"string","enum":["clips","graph"]},"useGraph":{"type":"boolean"},"graphAsset":{"type":"string"},"expectedGraph":{"type":"object"}}})");
    auto add = [&](const char *name, const char *description, Workspace::OpFn fn) {
        ws.registerOp({name, description, schema, std::move(fn)});
    };
    add("model_info",
        "Inspect a type model without requiring a visible Models window; lists nodes, clips, materials and loader "
        "warnings.",
        [&ws](const Json &p) {
            auto type = p.getStr("type");
            auto model = ws.project.rulesJson["types"][type]["view"].getStr("model");
            require(!model.empty(), "type has no model file");
            auto root = fs::weakly_canonical(ws.project.dir);
            std::vector<std::string> warnings;
            auto m = render::loadModel(
                model,
                [&](const std::string &relative, std::string &bytes) {
                    auto file = fs::weakly_canonical(root / relative);
                    auto rel = file.lexically_relative(root);
                    if (rel.empty() || *rel.begin() == ".." || !fs::is_regular_file(file) ||
                        fs::file_size(file) > 128 * 1024 * 1024)
                        return false;
                    std::ifstream f(file, std::ios::binary);
                    bytes.assign(std::istreambuf_iterator<char>(f), {});
                    return bool(f) || f.eof();
                },
                &warnings);
            auto r = Json::object(), nodes = Json::array(), clips = Json::array(), materials = Json::array(),
                 notes = Json::array();
            for (auto &n : m->nodes)
                nodes.push(Json::string(n.name));
            for (auto &c : m->clips) {
                auto j = Json::object();
                j.set("name", Json::string(c.name));
                j.set("duration", Json::number(double(c.duration), 6));
                clips.push(j);
            }
            for (auto &mt : m->materials)
                materials.push(Json::string(mt.name));
            for (auto &w : warnings)
                notes.push(Json::string(w));
            r.set("nodes", nodes);
            r.set("clips", clips);
            r.set("materials", materials);
            r.set("warnings", notes);
            r.set("path", Json::string(model));
            return r;
        });
    add("modeling_catalog", "Authoring operation catalog, format limits and exact algorithm coverage.",
        [](const Json &) { return modeling::catalog(); });
    add("modeling_list", "List project model source assets.", [&ws](const Json &) {
        auto dir = path(ws, "placeholder").parent_path();
        auto a = Json::array();
        std::vector<std::string> names;
        if (fs::exists(dir))
            for (auto &e : fs::directory_iterator(dir)) {
                auto n = e.path().filename().string();
                if (n.ends_with(".model.json"))
                    names.push_back(n.substr(0, n.size() - 11));
            }
        std::sort(names.begin(), names.end());
        for (auto &n : names)
            a.push(Json::string(n));
        auto r = Json::object();
        r.set("models", a);
        return r;
    });
    add("modeling_create",
        "Create a versioned model source; preset empty or robot, or explicit document. Refuses overwrite.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            require(!fs::exists(file), "model already exists");
            require(p.getStr("preset", "empty") == "empty" || p.getStr("preset") == "robot", "unknown preset");
            auto doc = p.has("document")               ? p["document"]
                       : p.getStr("preset") == "robot" ? modeling::robotDocument()
                                                       : modeling::emptyDocument();
            modeling::build(doc);
            auto state = Json::object();
            state.set("revision", Json::number(int64_t(1)));
            state.set("document", doc);
            state.set("undo", Json::array());
            state.set("redo", Json::array());
            atomicWrite(file, state.dump(2));
            return response(state);
        });
    add("modeling_get", "Read model source and current revision.",
        [&ws](const Json &p) { return response(read(path(ws, p.getStr("name")))); });
    auto edit = [&ws](const Json &p) {
        auto file = path(ws, p.getStr("name"));
        Lock lock(file);
        auto state = read(file);
        revision(state, p);
        auto doc = modeling::edit(state["document"], p["commands"]);
        return commit(file, state, doc);
    };
    add("modeling_edit",
        "Atomically apply commands; requires expectedRevision. put/remove nodes/materials/clips/skins, parameters, "
        "graph or replace.",
        edit);
    add("rig_edit",
        "Edit skeleton nodes, skins, vertex influences and graph rig units through the shared authoring transaction.",
        edit);
    add("animation_edit", "Edit clip channels and keys through the shared authoring transaction.", edit);
    for (auto name : {"rig_get", "animation_get", "modeling_graph_get"})
        add(name, "Read the shared authoring document with rig, clips, graph and revision.",
            [&ws](const Json &p) { return response(read(path(ws, p.getStr("name")))); });
    for (auto name : {"modeling_undo", "modeling_redo"})
        add(name, "Undo/redo an asset edit. Requires expectedRevision; revisions always increase.",
            [&ws, redo = std::string(name) == "modeling_redo"](const Json &p) {
                auto file = path(ws, p.getStr("name"));
                Lock lock(file);
                auto state = read(file);
                revision(state, p);
                auto from = redo ? "redo" : "undo", to = redo ? "undo" : "redo";
                require(state[from].size() > 0, "history is empty");
                auto doc = state[from][state[from].size() - 1];
                modeling::build(doc);
                state.member(to).push(state["document"]);
                state.member(from).eraseAt(state[from].size() - 1);
                state.set("document", doc);
                state.set("revision", Json::number(state.getInt("revision") + 1));
                atomicWrite(file, state.dump(2));
                return response(state);
            });
    auto validate = [&ws, service](const Json &p) {
        Json doc = p["document"];
        Json state;
        if (doc.isNull()) {
            state = read(path(ws, p.getStr("name")));
            doc = state["document"];
        }
        if (p.has("graph"))
            doc.set("graph", p["graph"]);
        auto r = modeling::build(doc, nullptr, &service->cache).report;
        if (!state.isNull())
            r.set("revision", state["revision"]);
        return r;
    };
    for (auto name : {"modeling_validate", "rig_validate", "modeling_graph_validate"})
        add(name, "Validate geometry, hierarchy, skin weights, clips and compile graph with actual model. No writes.",
            validate);
    add("modeling_graph_set", "Validate and save graph in model source. Requires expectedRevision.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            auto state = read(file);
            revision(state, p);
            auto doc = state["document"];
            doc.set("graph", p["graph"]);
            return commit(file, state, doc);
        });
    add("animation_sample",
        "Sample clip or graph at time; returns local transforms and world joint positions. No writes.",
        [&ws](const Json &p) {
            auto state = read(path(ws, p.getStr("name")));
            auto built = modeling::build(state["document"]);
            auto r = modeling::sample(*built.model, p);
            r.set("revision", state["revision"]);
            return r;
        });
    add("animation_bake",
        "Bake graph to editable clip keys. Requires expectedRevision, duration, fps; optional graph and parameters.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            auto state = read(file);
            revision(state, p);
            auto clip = modeling::bake(state["document"], p);
            auto commands = Json::array(), c = Json::object();
            c.set("op", Json::string("put"));
            c.set("collection", Json::string("clips"));
            c.set("id", clip["id"]);
            c.set("value", clip);
            commands.push(c);
            return commit(file, state, modeling::edit(state["document"], commands));
        });
    add("modeling_build",
        "Build and report a document. async=true starts a bounded background job; poll modeling_job_status.",
        [&ws, service](const Json &p) {
            auto state = read(path(ws, p.getStr("name")));
            if (!p.getBool("async")) {
                auto r = modeling::build(state["document"], nullptr, &service->cache).report;
                r.set("revision", state["revision"]);
                return r;
            }
            std::lock_guard guard(service->mutex);
            require(service->jobs.size() < 8, "job queue full; collect finished jobs");
            Job j;
            j.id = std::to_string(++service->serial);
            j.revision = state.getInt("revision");
            j.cancel = std::make_shared<std::atomic<bool>>(false);
            j.result =
                std::async(std::launch::async, [doc = state["document"], cancel = j.cancel, cache = &service->cache] {
                    try {
                        return modeling::build(doc, cancel.get(), cache).report;
                    } catch (const std::exception &e) {
                        auto r = Json::object();
                        r.set("error", Json::string(e.what()));
                        return r;
                    }
                });
            auto r = Json::object();
            r.set("jobId", Json::string(j.id));
            r.set("revision", state["revision"]);
            service->jobs.push_back(std::move(j));
            return r;
        });
    for (auto name : {"modeling_job_status", "modeling_job_cancel"})
        add(name, "Poll/collect or cancel a background build. Completed result is consumed once.",
            [service, cancel = std::string(name) == "modeling_job_cancel"](const Json &p) {
                std::lock_guard guard(service->mutex);
                auto it = std::find_if(service->jobs.begin(), service->jobs.end(),
                                       [&](auto &j) { return j.id == p.getStr("jobId"); });
                require(it != service->jobs.end(), "unknown job");
                if (cancel)
                    it->cancel->store(true);
                auto r = Json::object();
                bool ready = it->result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                r.set("running", Json::fromBool(!ready));
                r.set("revision", Json::number(it->revision));
                if (ready) {
                    r.set("result", it->result.get());
                    service->jobs.erase(it);
                }
                return r;
            });
    add("rig_auto_weights",
        "Generate editable nearest-bone segment weights (up to four influences). Requires node, skin, "
        "expectedRevision.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            auto state = read(file);
            revision(state, p);
            return commit(file, state, modeling::autoWeights(state["document"], p));
        });
    add("modeling_duplicate",
        "Duplicate a rigid node subtree with stable prefixed IDs. Requires node, prefix, expectedRevision.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            auto state = read(file);
            revision(state, p);
            return commit(file, state, modeling::duplicateParts(state["document"], p));
        });
    add("animation_generate",
        "Generate editable motion keys. tracks: "
        "node/path/axis/wave(sine,spin,fold,recoil)/amplitude/frequency/phase/offset. Requires expectedRevision.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            auto state = read(file);
            revision(state, p);
            auto clip = modeling::generateClip(state["document"], p);
            auto a = Json::array(), c = Json::object();
            c.set("op", Json::string("put"));
            c.set("collection", Json::string("clips"));
            c.set("id", clip["id"]);
            c.set("value", clip);
            a.push(c);
            return commit(file, state, modeling::edit(state["document"], a));
        });
    add("modeling_lod",
        "Create and export a separate simplified source asset. Requires expectedRevision, target, cell; preserves "
        "node/clip names. Explicit skin weights must be regenerated.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            auto state = read(file);
            revision(state, p);
            auto target = p.getStr("target");
            require(target != p.getStr("name"), "LOD target must differ from source");
            auto dest = path(ws, target);
            Lock targetLock(dest);
            require(!fs::exists(dest), "LOD target already exists");
            auto doc = state["document"];
            for (auto &n : doc.member("nodes").itemsMut())
                if (n.has("geometry") && !n.getBool("construction")) {
                    require(!n.has("weights"), "regenerate explicit weights after simplifying geometry");
                    auto mod = Json::object();
                    mod.set("kind", Json::string("simplify"));
                    mod.set("cell", p["cell"]);
                    auto &g = n.member("geometry");
                    if (!g.has("modifiers"))
                        g.set("modifiers", Json::array());
                    g.member("modifiers").push(mod);
                }
            auto result = modeling::build(doc);
            auto fresh = Json::object();
            fresh.set("revision", Json::number(int64_t(1)));
            fresh.set("document", doc);
            fresh.set("undo", Json::array());
            fresh.set("redo", Json::array());
            auto glb = output(ws, target);
            atomicWrite(glb, modeling::exportGlb(*result.model));
            atomicWrite(dest, fresh.dump(2));
            auto r = response(fresh);
            r.set("path", Json::string(ws.relative(glb)));
            r.set("report", result.report);
            return r;
        });
    auto exportAsset = [&ws](const Json &p) {
        auto file = path(ws, p.getStr("name"));
        Lock lock(file);
        auto state = read(file);
        revision(state, p);
        auto built = modeling::build(state["document"]);
        require(!built.model->meshes.empty(), "cannot export empty model");
        auto dest = output(ws, p.getStr("name"));
        auto bytes = modeling::exportGlb(*built.model);
        atomicWrite(dest, bytes);
        auto r = built.report;
        r.set("path", Json::string(ws.relative(dest)));
        r.set("revision", state["revision"]);
        return r;
    };
    add("modeling_export",
        "Export current model to models/<name>.glb with nodes, PBR, skins and clips; requires expectedRevision.",
        exportAsset);
    add("modeling_graph_publish",
        "Export model and publish a graph snapshot to animations/. Requires expectedRevision; existing graph needs "
        "expectedGraph.",
        [&ws, exportAsset](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            auto state = read(file);
            revision(state, p);
            auto q = Json::object();
            q.set("name", Json::string(p.getStr("graphAsset", p.getStr("name"))));
            auto existing = ws.call("animation_graph_get", q);
            require(existing.has("error") || p.has("expectedGraph"),
                    "graph asset already exists; open it or supply expectedGraph before replacing");
            auto exported = exportAsset(p);
            auto graph = modeling::evaluateDocument(state["document"])["graph"];
            if (!graph.isObject())
                graph = Json::parse(R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})");
            size_t index = 0;
            for (auto &node : graph.member("nodes").itemsMut()) {
                if (!node.has("position")) {
                    auto position = Json::array();
                    position.push(Json::number(double(20 + (index % 2) * 200)));
                    position.push(Json::number(double(20 + (index / 2) * 150)));
                    node.set("position", position);
                }
                ++index;
            }
            graph.set("model", exported["path"]);
            q.set("graph", graph);
            if (p.has("expectedGraph"))
                q.set("expected", p["expectedGraph"]);
            return ws.call("animation_graph_set", q);
        });
    add("modeling_graph_import",
        "Import a saved animations/ graph snapshot into model source. Requires expectedRevision and optional "
        "graphAsset.",
        [&ws](const Json &p) {
            auto file = path(ws, p.getStr("name"));
            Lock lock(file);
            auto state = read(file);
            revision(state, p);
            auto q = Json::object();
            q.set("name", Json::string(p.getStr("graphAsset", p.getStr("name"))));
            auto r = ws.call("animation_graph_get", q);
            if (r.has("error"))
                return r;
            auto doc = state["document"];
            doc.set("graph", r["graph"]);
            return commit(file, state, doc);
        });
    add("modeling_apply",
        "Export and assign model and graph to an existing type. Requires expectedRevision; replaceView:true replaces "
        "incompatible prior view bindings.",
        [&ws, exportAsset](const Json &p) {
            auto type = p.getStr("type");
            require(ws.project.rulesJson["types"].has(type), "unknown type");
            auto state = read(path(ws, p.getStr("name")));
            revision(state, p);
            auto view = ws.project.rulesJson["types"][type]["view"];
            if (p.getBool("replaceView"))
                view = Json::object();
            else
                for (auto key : {"animations", "animationGraph", "additive", "procedural", "attach", "footIK", "lod",
                                 "stages", "muzzle"})
                    require(!view.has(key), std::string("existing view contains model-dependent '") + key +
                                                "'; review it or pass replaceView:true");
            auto r = exportAsset(p);
            if (!view.isObject())
                view = Json::object();
            view.set("model", r["path"]);
            if (state["document"]["graph"].isObject())
                view.set("animationGraph", modeling::evaluateDocument(state["document"])["graph"]);
            auto q = Json::object();
            q.set("path", Json::string("types." + type + ".view"));
            q.set("value", view);
            auto applied = ws.call("rules_set", q);
            if (applied.has("error"))
                return applied;
            return r;
        });
    ws.registerOp({"animation_editor", "Open an authoring asset in the animation editor; tab=clips|graph.", schema,
                   [](const Json &) {
                       auto r = Json::object();
                       r.set("error", Json::string("animation editor requires AiverseEditor"));
                       return r;
                   },
                   true});
    for (auto name : {"modeling_preview", "animation_preview"})
        ws.registerOp({name,
                       "Show authoring model in editor; time/clip, yaw/pitch, wireframe/skeleton. Returns screenshot "
                       "of this asset revision.",
                       schema,
                       [](const Json &) {
                           auto r = Json::object();
                           r.set("error", Json::string("preview requires AiverseEditor"));
                           return r;
                       },
                       true});
}
} // namespace aiverse::editor
