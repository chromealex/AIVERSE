#include "PackageTool.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace aiverse::editor {
namespace {
std::string quote(const std::string& value) {
    if (value.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("newline in command argument");
#if defined(_WIN32)
    if (value.find_first_of("\"%!") != std::string::npos) throw std::runtime_error("unsupported shell characters in argument");
    return "\"" + value + "\"";
#else
    std::string out = "'";
    for (char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
#endif
}
} // namespace

Json runPackageTool(const std::string& engineDir, const std::vector<std::string>& args) {
    const auto script = std::filesystem::path(engineDir) / "tools/pkg/aiverse_pkg.py";
    if (!std::filesystem::exists(script)) throw std::runtime_error("tools/pkg/aiverse_pkg.py not found in the engine directory");
    const char* python = std::getenv("AIVERSE_PYTHON");
#if defined(_WIN32)
    if (!python) python = "python";
#else
    if (!python) python = "python3";
#endif
    std::string cmd = quote(python) + " " + quote(script.string()) + " --json";
    for (const auto& a : args) cmd += " " + quote(a);
#if defined(_WIN32)
    cmd = "\"" + cmd + " 2>NUL\"";
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    cmd += " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) throw std::runtime_error("cannot launch Python (set AIVERSE_PYTHON)");
    std::string output;
    char buffer[4096];
    while (size_t n = std::fread(buffer, 1, sizeof buffer, pipe)) output.append(buffer, n);
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    Json r;
    try { r = Json::parse(output); }
    catch (const std::exception&) { throw std::runtime_error("aiverse_pkg.py: " + output.substr(0, 2000)); }
    if (r.has("error")) throw std::runtime_error(r["error"].str("aiverse_pkg.py failed"));
    return r["result"];
}

} // namespace aiverse::editor
