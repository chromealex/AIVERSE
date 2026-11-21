// LENS project authoring shared by Studio and the standalone MCP server.
#include "LensOps.h"
#include "Workspace.h"
#include "engine/core/Hash.h"
#include "engine/sim/CameraSceneAsset.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace aiverse::editor {
namespace lens {
namespace {
namespace fs = std::filesystem;
constexpr size_t maxBytes = 1024 * 1024;
Json emptyScene() { return Json::parse(R"({"version":1,"cameras":[]})"); }
fs::path assetPath(const std::string& projectDir) {
    if (projectDir.empty()) throw std::runtime_error("open_game first");
    const fs::path path = fs::path(projectDir) / "cameras.json";
    if (fs::is_symlink(fs::symlink_status(path)))
        throw std::runtime_error("LENS refuses a symlink cameras.json");
    return path;
}
std::string revisionOf(const std::string& bytes) {
    Hasher h;
    h.bytes(bytes.data(), bytes.size());
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(h.value()));
    return text;
}
// Bytes of the current file; nullopt when it does not exist.
std::optional<std::string> readBytes(const fs::path& path) {
    if (!fs::exists(fs::symlink_status(path))) return std::nullopt;
    if (!fs::is_regular_file(path) || fs::file_size(path) > maxBytes)
        throw std::runtime_error("cameras.json must be a regular file of at most 1 MiB");
    std::ifstream input(path, std::ios::binary);
    std::string text(maxBytes + 1, '\0');
    input.read(text.data(), std::streamsize(text.size()));
    if (input.bad()) throw std::runtime_error("cannot read cameras.json");
    text.resize(size_t(input.gcount()));
    if (text.size() > maxBytes) throw std::runtime_error("cameras.json exceeds 1 MiB");
    return text;
}
std::string currentRevision(const fs::path& path) {
    const auto bytes = readBytes(path);
    return bytes ? revisionOf(*bytes) : std::string(kMissingRevision);
}
} // namespace

SceneFile readSceneFile(const std::string& projectDir) {
    const auto path = assetPath(projectDir);
    SceneFile file;
    const auto bytes = readBytes(path);
    if (!bytes) { file.scene = emptyScene(); file.revision = kMissingRevision; return file; }
    file.scene = Json::parse(*bytes);
    file.revision = revisionOf(*bytes);
    file.exists = true;
    return file;
}

Json validateScene(const Json& scene) {
    if (scene.dump(-1).size() > maxBytes) throw std::runtime_error("camera scene exceeds 1 MiB");
    World world;
    SceneHierarchy hierarchy;
    std::string error;
    Json response = Json::object();
    const bool valid = loadCameraScene(world, hierarchy, scene, nullptr, &error);
    response.set("ok", Json::fromBool(valid));
    if (!valid) response.set("error", Json::string(error));
    else {
        response.set("normalizedScene", cameraSceneToJson(world));
        response.set("count", Json::number(int64_t(scene["cameras"].size())));
        bool clientCompatible = true;
        for (const auto& camera : response["normalizedScene"]["cameras"].items())
            clientCompatible &= camera["output"].getStr("kind") == "texture";
        response.set("clientCompatible", Json::fromBool(clientCompatible));
    }
    return response;
}

bool sameScene(const Json& a, const Json& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Json::Type::Number:
            // Editor-side comparison only (never simulation state).
            return a.numberLiteral() == b.numberLiteral() || a.toDouble() == b.toDouble();
        case Json::Type::Array: {
            if (a.items().size() != b.items().size()) return false;
            for (size_t i = 0; i < a.items().size(); ++i)
                if (!sameScene(a.items()[i], b.items()[i])) return false;
            return true;
        }
        case Json::Type::Object: {
            using Member = const std::pair<std::string, Json>*;
            auto sorted = [](const Json& j) {
                std::vector<Member> members;
                for (const auto& m : j.members()) members.push_back(&m);
                std::stable_sort(members.begin(), members.end(),
                                 [](Member x, Member y) { return x->first < y->first; });
                return members;
            };
            const auto x = sorted(a), y = sorted(b);
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (x[i]->first != y[i]->first || !sameScene(x[i]->second, y[i]->second)) return false;
            return true;
        }
        default: return a == b;
    }
}

SaveResult saveScene(const std::string& projectDir, const Json& scene,
                     const std::string& expectedRevision) {
    SaveResult result;
    result.validation = validateScene(scene);
    if (!result.validation.getBool("ok")) {
        result.error = result.validation.getStr("error", "invalid camera scene");
        return result;
    }
    const std::string text = scene.dump(2) + "\n";
    if (text.size() > maxBytes) { result.error = "camera scene exceeds 1 MiB"; return result; }
    const auto destination = assetPath(projectDir);
    if (!expectedRevision.empty() && currentRevision(destination) != expectedRevision) {
        result.conflict = true;
        result.error = "cameras.json changed since it was read (revision mismatch)";
        return result;
    }
    // An exclusively created sibling directory avoids shared temporary files and
    // leaves the previous asset untouched on validation/write/rename failures.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = destination.parent_path() / (".lens-save-" + std::to_string(stamp));
    if (!fs::create_directory(temporary)) { result.error = "cannot stage camera scene"; return result; }
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{temporary};
    const auto staged = temporary / "cameras.json";
    {
        std::ofstream out(staged, std::ios::binary);
        out << text;
        out.close();
        if (!out) { result.error = "cannot write staged camera scene"; return result; }
    }
    std::error_code ec;
    fs::rename(staged, destination, ec); // Never delete last-good data as a fallback.
    if (ec) { result.error = "cannot replace cameras.json: " + ec.message(); return result; }
    result.ok = true;
    result.revision = revisionOf(text);
    return result;
}
} // namespace lens

namespace {
using namespace lens;
Json checkedCurrent(const Workspace& ws, const Json& p, std::string& revision) {
    const SceneFile current = readSceneFile(ws.project.dir);
    revision = current.revision;
    if (p["expectedRevision"].isString()) {
        if (p.getStr("expectedRevision") != current.revision)
            throw std::runtime_error("camera scene changed (expectedRevision is stale); call lens_scene_get and reapply your edit");
        return current.scene;
    }
    if (!p["expectedScene"].isObject() || !sameScene(current.scene, p["expectedScene"]))
        throw std::runtime_error("camera scene changed or expectedRevision/expectedScene missing; call lens_scene_get and reapply your edit");
    return current.scene;
}
Json save(Workspace& ws, const Json& scene, const std::string& expectedRevision) {
    SaveResult saved = saveScene(ws.project.dir, scene, expectedRevision);
    if (saved.conflict)
        throw std::runtime_error("camera scene changed during the edit; call lens_scene_get and reapply your edit");
    if (!saved.validation.getBool("ok")) return saved.validation;
    if (!saved.ok) throw std::runtime_error(saved.error);
    ws.note({Change::Project, 0, 0, -1, -1, -1, "LENS: cameras.json saved; Reload camera window"});
    Json response = saved.validation;
    response.set("scene", scene);
    response.set("revision", Json::string(saved.revision));
    response.set("reloadRequired", Json::fromBool(true));
    return response;
}
int64_t positiveId(const Json& value) {
    const auto id = value.integer();
    if (!value.isNumber() || id <= 0 || value.toDouble() != double(id))
        throw std::runtime_error("camera id must be a positive integer");
    return id;
}
} // namespace

void registerLensOps(Workspace& ws) {
    auto add = [&ws](const char* name, const char* description, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, description, Json::parse(schema), std::move(fn)});
    };
    // Writes take expectedRevision (preferred: the opaque revision from lens_scene_get) or
    // expectedScene (compared by value: member order and number spelling are ignored).
    static constexpr const char* guard =
        R"("expectedRevision":{"type":"string"},"expectedScene":{"type":"object"})";
    const std::string sceneSetSchema = std::string(R"({"type":"object","properties":{"scene":{"type":"object"},)") + guard + R"(},"required":["scene"]})";
    const std::string cameraSetSchema = std::string(R"({"type":"object","properties":{"camera":{"type":"object"},)") + guard + R"(},"required":["camera"]})";
    const std::string deleteSchema = std::string(R"({"type":"object","properties":{"id":{"type":"integer","minimum":1},)") + guard + R"(},"required":["id"]})";
    add("lens_scene_get", "Read project cameras.json (missing = empty v1 scene), runtime validation and normalized defaults. Does not render. Returns an opaque revision: pass it as expectedRevision when editing (expectedScene with the returned scene is still accepted).",
        R"({"type":"object","properties":{}})", [&ws](const Json&) {
            const SceneFile file = readSceneFile(ws.project.dir);
            Json result = validateScene(file.scene);
            result.set("scene", file.scene);
            result.set("revision", Json::string(file.revision));
            result.set("exists", Json::fromBool(file.exists));
            result.set("clientEnabled", Json::fromBool(ws.project.manifest["lens"].getBool("enabled")));
            return result;
        });
    add("lens_scene_validate", "Validate a v1 camera scene with the native loader, without changing files. normalizedScene exposes defaults/clamping; clientCompatible means texture-only outputs, not GPU acceptance.",
        R"({"type":"object","properties":{"scene":{"type":"object"}},"required":["scene"]})",
        [](const Json& p) { return validateScene(p["scene"]); });
    add("lens_scene_set", "Replace cameras.json after native validation. Requires expectedRevision (or expectedScene) from lens_scene_get; a stale one fails without writing. Saves immediately; no editor undo. Reload the LENS window/restart the client to consume it. Does not enable game.json lens.enabled.",
        sceneSetSchema.c_str(),
        [&ws](const Json& p) {
            std::string revision;
            checkedCurrent(ws, p, revision);
            return save(ws, p["scene"], revision);
        });
    add("lens_camera_set", "Create or replace one complete camera record by positive id, preserving other records. Requires expectedRevision (or expectedScene) from lens_scene_get. For partial edits merge locally first. Saves immediately; Reload LENS afterward.",
        cameraSetSchema.c_str(),
        [&ws](const Json& p) {
            std::string revision;
            Json scene = checkedCurrent(ws, p, revision);
            if (!validateScene(scene).getBool("ok")) throw std::runtime_error("repair invalid scene with lens_scene_set first");
            const auto id = positiveId(p["camera"]["id"]);
            bool found = false;
            for (auto& item : scene.member("cameras").itemsMut())
                if (item.getInt("id") == id) { item = p["camera"]; found = true; break; }
            if (!found) scene.member("cameras").push(p["camera"]);
            return save(ws, scene, revision);
        });
    add("lens_camera_delete", "Remove one camera record by positive id. Requires expectedRevision (or expectedScene) from lens_scene_get; unknown ids fail without writes. Saves immediately; no undo or live GPU resource deletion.",
        deleteSchema.c_str(),
        [&ws](const Json& p) {
            std::string revision;
            Json scene = checkedCurrent(ws, p, revision);
            if (!validateScene(scene).getBool("ok")) throw std::runtime_error("repair invalid scene with lens_scene_set first");
            const auto id = positiveId(p["id"]);
            for (size_t i = 0; i < scene["cameras"].size(); ++i)
                if (scene["cameras"][i].getInt("id") == id) {
                    scene.member("cameras").eraseAt(i);
                    return save(ws, scene, revision);
                }
            throw std::runtime_error("camera id not found");
        });
}
} // namespace aiverse::editor
