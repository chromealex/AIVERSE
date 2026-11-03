#include "PortOps.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace rts::editor {
namespace {
std::string quote(const std::string& value) {
    if (value.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("newline in command argument");
#if defined(_WIN32)
    // cmd.exe expands percent and delayed variables even inside quoted arguments.
    if (value.find_first_of("\"%!\r\n") != std::string::npos) throw std::runtime_error("unsupported shell characters in path");
    return "\"" + value + "\"";
#else
    std::string out = "'";
    for (char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
#endif
}
}

Json runProjectPort(const std::string& engineDir, const std::string& operation, const Json& p) {
    const std::string path = p.getStr("path");
    if (path.empty()) throw std::runtime_error("path is required");
    if (operation != "scan" && operation != "import" && operation != "report" && operation != "item" && operation != "resolve")
        throw std::runtime_error("unknown project import operation");
    const auto script = std::filesystem::path(engineDir) / "tools/importers/port/rts_port.py";
    if (!std::filesystem::exists(script)) throw std::runtime_error("rts_port.py not found in engine directory");
    const char* python = std::getenv("RTS_PYTHON");
#if defined(_WIN32)
    if (!python) python = "python";
#else
    if (!python) python = "python3";
#endif
    std::string cmd = quote(python) + " " + quote(script.string()) + " " + operation + " " + quote(path);
    for (const char* key : {"out", "id", "how", "reason", "mapping", "mode"})
        if (p.has(key)) cmd += std::string(" --") + key + " " + quote(p.getStr(key));
    if (p.has("scale")) cmd += " --scale " + quote(p["scale"].dump());
    cmd += " 2>&1";
#if defined(_WIN32)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) throw std::runtime_error("cannot launch Python importer");
    std::string output;
    char buffer[4096];
    while (size_t n = std::fread(buffer, 1, sizeof buffer, pipe)) output.append(buffer, n);
#if defined(_WIN32)
    int result = _pclose(pipe);
#else
    int result = pclose(pipe);
#endif
    Json report;
    try { report = Json::parse(output); }
    catch (const std::exception&) { throw std::runtime_error("invalid importer response: " + output.substr(0, 3000)); }
    if (result && !report.has("ok")) throw std::runtime_error("importer failed: " + output.substr(0, 3000));
    return report;
}

void registerPortOps(Workspace& ws) {
    const char* schema = R"({"properties":{"options":{"type":"object","properties":{"out":{"type":"string"},"scale":{"type":"number","exclusiveMinimum":0},"mode":{"enum":["auto","2d","3d"]},"mapping":{"type":"string"}}},"path":{"type":"string"},"out":{"type":"string"},"scale":{"type":"number","exclusiveMinimum":0},"mapping":{"type":"string"},"mode":{"enum":["auto","2d","3d"]},"id":{"type":"string"},"how":{"enum":["rules","lua","native","dropped"]},"reason":{"type":"string"}}})";
    const std::pair<const char*, const char*> ops[] = {
        {"scan", "Inspect a Unity/Godot/Unreal project without writing. path is required; returns engine/version/assets/scenes and errors."},
        {"import", "Import project path into out, optional scale (metres to cells), mapping (synonym JSON). Returns port report. Preserves edited files. Does not open the result automatically."},
        {"report", "Read port-report.json in project path. Resolve missing mechanics using rules_schema first, then Lua, then native modules. Verify with scenario_run/bot matches and source screenshots."},
        {"item", "Read report item id plus serialized components, script source, fields and uses from port.json in project path."},
        {"resolve", "Mark report item id resolved with how (rules/lua/native/dropped) and reason. Changes report only; verify implementation before resolving."}
    };
    for (const auto& [op, description] : ops) {
        const std::string operation = op;
        ws.registerOp({"port_" + operation, description, Json::parse(schema), [&ws, operation](const Json& params) {
            Json p = params;
            for (const char* key : {"out", "scale", "mode", "mapping"})
                if (!p.has(key) && params["options"].has(key)) p.set(key, params["options"][key]);
            if (!p.has("path") && !ws.project.dir.empty()) p.set("path", Json::string(ws.project.dir));
            return runProjectPort(ws.engineDir, operation, p);
        }, false});
    }
}
}
