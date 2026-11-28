#include "Workspace.h"
#include "engine/fx/SpectraFxGpuEmitterLibrary.h"
#include "engine/fx/SpectraFxGpuRoutes.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace aiverse::editor {
namespace {
namespace fs = std::filesystem;

void refuseLink(const fs::path& path) {
    if (fs::is_symlink(fs::symlink_status(path)))
        throw std::runtime_error("GPU asset path contains a symbolic link");
}
fs::path directory(const Workspace& ws) {
    if (ws.project.dir.empty()) throw std::runtime_error("Open a project first");
    const auto fx = fs::path(ws.project.dir) / "fx";
    refuseLink(fx);
    refuseLink(fx / "gpu");
    return fx / "gpu";
}
fs::path file(const Workspace& ws, const std::string& name) {
    if (name != "@routes" && !fx::SpectraFxGpuEmitterLibrary::validName(name))
        throw std::runtime_error("Invalid GPU emitter name");
    const auto path = directory(ws) / (name == "@routes" ? "routes.json" : name + ".gpu.json");
    refuseLink(path);
    return path;
}
Json readSource(const fs::path& path) {
    if (!fs::exists(path)) return Json{};
    if (!fs::is_regular_file(path)) throw std::runtime_error("Expected a GPU asset file");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open GPU asset");
    std::string text{std::istreambuf_iterator<char>(input), {}};
    return Json::string(text);
}
void validate(const std::string& name, const Json& source) {
    if (!source.isString()) throw std::runtime_error("Expected source text");
    const auto doc = Json::parse(source.str());
    std::string error;
    if (name == "@routes") {
        fx::SpectraFxGpuRouteAsset::Routes routes;
        if (!fx::SpectraFxGpuRouteAsset::read(doc, routes, &error)) throw std::runtime_error(error);
    } else {
        fx::SpectraFxHybridEmitter::Config config;
        if (!fx::SpectraFxGpuEmitterAsset::read(doc, config, &error)) throw std::runtime_error(error);
    }
}
struct PendingWrite {
    fs::path directory;
    ~PendingWrite() {
        std::error_code ec;
        fs::remove(directory / "source", ec);
        fs::remove(directory, ec);
    }
};
}

void registerSpectraGpuFileOps(Workspace& ws) {
    ws.registerOp({"fx_gpu_list", "List project GPU emitter names in fx/gpu.", Json::object(), [&ws](const Json&) {
        const auto dir = directory(ws);
        std::vector<std::string> names;
        if (fs::exists(dir)) for (const auto& entry : fs::directory_iterator(dir)) {
            if (!fs::is_regular_file(entry.symlink_status())) continue;
            const auto filename = entry.path().filename().string();
            const std::string suffix = ".gpu.json";
            if (!filename.ends_with(suffix)) continue;
            const auto name = filename.substr(0, filename.size() - suffix.size());
            if (fx::SpectraFxGpuEmitterLibrary::validName(name)) names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        auto result = Json::array();
        for (const auto& name : names) result.push(Json::string(name));
        return result;
    }});
    ws.registerOp({"fx_gpu_read", "Read emitter source or @routes. A missing file has null source.",
        Json::parse(R"({"properties":{"name":{"type":"string"}},"required":["name"]})"), [&ws](const Json& p) {
        auto result = Json::object();
        result.set("source", readSource(file(ws, p.getStr("name"))));
        return result;
    }});
    ws.registerOp({"fx_gpu_validate", "Validate GPU emitter or event route source using the runtime codec.",
        Json::parse(R"({"properties":{"name":{"type":"string"},"text":{"type":"string"}},"required":["name","text"]})"), [&ws](const Json& p) {
        const auto name = p.getStr("name");
        file(ws, name);
        validate(name, p["text"]);
        return Json::fromBool(true);
    }});
    ws.registerOp({"fx_gpu_save", "Validate and save GPU emitter or route source if its disk revision matches.",
        Json::parse(R"({"properties":{"name":{"type":"string"},"text":{"type":"string"},"expectedSource":{"type":["string","null"]}},"required":["name","text","expectedSource"]})"), [&ws](const Json& p) {
        const auto name = p.getStr("name");
        const auto path = file(ws, name);
        validate(name, p["text"]);
        if (!p.has("expectedSource")) throw std::runtime_error("Expected disk revision required");
        fs::create_directories(path.parent_path());
        auto lock = path;
        lock += ".writing";
        if (!fs::create_directory(lock)) throw std::runtime_error("GPU asset is busy");
        PendingWrite pending{lock};
        const auto current = readSource(path);
        const auto& expected = p["expectedSource"];
        if (!(current.isNull() && expected.isNull()) &&
            !(current.isString() && expected.isString() && current.str() == expected.str()))
            throw std::runtime_error("File changed on disk. Reload before saving.");
        const auto temp = lock / "source";
        std::ofstream output(temp, std::ios::binary);
        output << p["text"].str();
        output.close();
        if (!output) throw std::runtime_error("Cannot write GPU asset");
        if (current.isNull()) fs::create_hard_link(temp, path);
        else fs::rename(temp, path);
        return Json::fromBool(true);
    }});
}
}
