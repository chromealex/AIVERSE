#include "NativeBuild.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "modules/rts/RtsNative.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

namespace {
std::string quote(const std::string& s) {
#if defined(_WIN32)
    return "\"" + s + "\"";
#else
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
#endif
}

// Quoting for the target host of a plan (tests build plans of other hosts).
std::string quoteFor(const std::string& host, const std::string& s) {
    if (host == "windows") return "\"" + s + "\"";
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

fs::path normal(const std::string& p) {
    std::error_code ec;
    fs::path a = fs::weakly_canonical(fs::absolute(p, ec), ec);
    return (ec ? fs::absolute(p) : a).lexically_normal();
}

// rel = path below base, or empty when `p` is not inside `base`
bool inside(const fs::path& p, const fs::path& base, fs::path* rel) {
    auto b = base.begin(), e = base.end();
    auto q = p.begin();
    for (; b != e; ++b, ++q) {
        if (b->empty()) continue;   // trailing separator
        if (q == p.end() || *q != *b) return false;
    }
    fs::path r;
    for (; q != p.end(); ++q) r /= *q;
    if (rel) *rel = r;
    return true;
}

fs::file_time_type mtime(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type::min() : t;
}
} // namespace

std::string nativeHost() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::vector<std::string> requiredNativeModules(const Json& rules) {
    std::vector<std::string> ids;
    const Json& list = rules["nativeModules"];
    if (!list.isArray()) return ids;
    for (auto& m : list.items()) {
        std::string id = m.isString() ? m.str() : m.getStr("id");
        if (!id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> compiledNativeIds() {
    std::vector<std::string> ids;
    for (auto& d : game::compiledNativeModules()) ids.push_back(d.id);
    return ids;
}

std::vector<std::string> missingNativeModules(const Json& rules, const std::vector<std::string>& compiled) {
    std::vector<std::string> out;
    for (auto& id : requiredNativeModules(rules))
        if (std::find(compiled.begin(), compiled.end(), id) == compiled.end()) out.push_back(id);
    return out;
}

std::string projectBuildsTempDir(const std::string& projectDir) {
    return (fs::path(projectDir) / "Builds" / ".temp").lexically_normal().string();
}

std::string nativeBuildDir(const std::string& projectDir, const std::string& host) {
    return (fs::path(projectBuildsTempDir(projectDir)) / ("editor-" + host)).lexically_normal().string();
}

NativeBuildPlan planNativeBuild(const std::string& engineDir, const std::string& projectDir, const std::string& host,
                                const std::string& cmake, const std::string& ninja, bool cacheExists, int jobs) {
    NativeBuildPlan p;
    p.host = host;
    p.engineDir = engineDir;
    p.projectDir = projectDir;
    p.buildDir = nativeBuildDir(projectDir, host);
    p.cmake = cmake.empty() ? "cmake" : cmake;
    p.configureArgs = {"-S", engineDir, "-B", p.buildDir};
    // a configured dir keeps its generator (cmake refuses another -G)
    const bool useNinja = !ninja.empty() && !cacheExists;
    if (useNinja) { p.configureArgs.insert(p.configureArgs.end(), {"-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja}); }
    p.configureArgs.insert(p.configureArgs.end(), {
        "-DCMAKE_BUILD_TYPE=Release",
        "-DAIVERSE_CONTENT_DIR=" + projectDir,
        "-DAIVERSE_NATIVE_DIR=",            // a stale override must not pick another project's modules
        "-DAIVERSE_BUILD_TESTS=OFF", "-DAIVERSE_BUILD_SERVER=OFF"});
    // macOS picks the .aiverse handler by bundle id: the project's AiverseEditor.app must not share the regular one's
    if (host == "macos") p.configureArgs.push_back("-DAIVERSE_EDITOR_BUNDLE_ID=com.aiverse.editor.project");
    p.buildArgs = {"--build", p.buildDir, "--config", "Release", "--target", "aiverse_editor", "aiverse_headless"};
    if (jobs > 0) p.buildArgs.insert(p.buildArgs.end(), {"--parallel", std::to_string(jobs)});
    const fs::path bd(p.buildDir);
    // multi-config generators (Visual Studio) put binaries in Release/; Ninja / Makefiles in the dir itself
    const bool multiConfig = host == "windows" && ninja.empty();
    const fs::path bin = multiConfig ? bd / "Release" : bd;
    if (host == "macos") {
        p.launchTarget = (bin / "AiverseEditor.app").string();
        p.editorBinary = (bin / "AiverseEditor.app" / "Contents" / "MacOS" / "AiverseEditor").string();
        p.headlessBinary = (bin / "aiverse_headless").string();
    } else if (host == "windows") {
        p.editorBinary = p.launchTarget = (bin / "aiverse_editor.exe").string();
        p.headlessBinary = (bin / "aiverse_headless.exe").string();
    } else {
        p.editorBinary = p.launchTarget = (bin / "aiverse_editor").string();
        p.headlessBinary = (bin / "aiverse_headless").string();
    }
    return p;
}

std::string nativeBuildCommand(const NativeBuildPlan& p) {
    auto line = [&](const std::vector<std::string>& args) {
        std::string s = quoteFor(p.host, p.cmake);
        for (auto& a : args) s += " " + quoteFor(p.host, a);
        return s;
    };
    return line(p.configureArgs) + " 2>&1 && " + line(p.buildArgs);   // the runner merges stderr of the last one
}

int nativeBuildJobs() {
    if (const char* e = std::getenv("AIVERSE_NATIVE_BUILD_JOBS"); e && std::atoi(e) > 0) return std::atoi(e);
    return std::max(1, int(std::thread::hardware_concurrency() / 2));
}

std::string nativeBuildDirProblem(const std::string& engineDir, const std::string& buildDir) {
    const fs::path eng = normal(engineDir), bd = normal(buildDir);
    fs::path rel;
    if (!inside(bd, eng, &rel)) return {};
    if (rel.empty()) return "папка сборки совпадает с папкой движка: " + bd.string();
    auto first = rel.begin();
    const std::string top = first->string();
    if (top == "build" || top.rfind("build-", 0) == 0 || top == "dist")
        return "папка сборки внутри сборок движка (" + top + "/): " + bd.string();
    // inside the engine checkout only below a Builds/ folder (.gitignore: "Builds/", any depth)
    for (const auto& part : rel) if (part == "Builds") return {};
    return "папка сборки внутри репозитория движка и не в Builds/ (git её не игнорирует): " + bd.string();
}

std::string nativeBuildGitProblem(const std::string& buildDir) {
#if defined(_WIN32)
    const std::string devnull = " >NUL 2>&1";
#else
    const std::string devnull = " >/dev/null 2>&1";
#endif
    // the dir may not exist yet: ask about a file inside it
    const fs::path probe = normal(buildDir) / "CMakeCache.txt";
    fs::path dir = probe.parent_path();
    std::error_code ec;
    while (!dir.empty() && !fs::exists(dir, ec) && dir != dir.parent_path()) dir = dir.parent_path();
    const std::string inRepo = "git -C " + quote(dir.string()) + " rev-parse --is-inside-work-tree" + devnull;
    if (std::system(inRepo.c_str()) != 0) return {};   // no git or not in a work tree
    const std::string chk = "git -C " + quote(dir.string()) + " check-ignore -q " + quote(probe.string()) + devnull;
    if (std::system(chk.c_str()) == 0) return {};
    return "git не игнорирует папку сборки " + normal(buildDir).string() + " (добавьте Builds/ в .gitignore)";
}

bool nativeBuildUpToDate(const std::string& binary, const std::string& projectDir, const std::string& engineExe, std::string* why) {
    auto say = [&](std::string s) { if (why) *why = std::move(s); return false; };
    std::error_code ec;
    if (!fs::is_regular_file(binary, ec)) return say("редактор для проекта ещё не собран");
    const auto built = mtime(binary);
    if (!engineExe.empty() && fs::exists(engineExe, ec) && mtime(engineExe) > built) return say("движок новее собранного редактора");
    const fs::path native = fs::path(projectDir) / "native";
    if (fs::is_directory(native, ec))
        for (auto it = fs::recursive_directory_iterator(native, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec) && mtime(it->path()) > built)
                return say("исходники модулей изменены: " + fs::relative(it->path(), native, ec).generic_string());
    if (why) why->clear();
    return true;
}

std::vector<std::string> nativeBuildModules(const std::string& buildDir) {
    std::vector<std::string> ids;
    std::ifstream f(fs::path(buildDir) / "gen" / "RtsNativeRegistry.cpp");
    std::string line;
    const std::string key = "result.push_back({\"";
    while (std::getline(f, line)) {
        size_t a = line.find(key);
        if (a == std::string::npos) continue;
        a += key.size();
        size_t b = line.find('"', a);
        if (b != std::string::npos) ids.push_back(line.substr(a, b - a));
    }
    return ids;
}

bool nativeBuildHasModules(const std::string& buildDir, const std::vector<std::string>& required, std::string* why) {
    const auto have = nativeBuildModules(buildDir);
    std::string lack;
    for (auto& id : required)
        if (std::find(have.begin(), have.end(), id) == have.end()) lack += (lack.empty() ? "" : ", ") + id;
    if (why) *why = lack.empty() ? std::string() : "в собранном редакторе нет модулей: " + lack;
    return lack.empty();
}

NativeOpenAction nativeOpenAction(bool modulesMissing, bool buildReady, bool nativeChild) {
    if (!modulesMissing) return NativeOpenAction::None;
    if (buildReady && !nativeChild) return NativeOpenAction::Switch;
    return NativeOpenAction::Offer;
}

std::string nativeRestartCommand(const std::string& host, const std::string& launchTarget, const std::string& projectArg, const std::string& toolsDir) {
    auto q = [&](const std::string& s) { return quoteFor(host, s); };
    std::string args = q(projectArg) + " --native-editor";
    if (!toolsDir.empty()) args += " --tools-from " + q(toolsDir);
    if (host == "macos") return "open -n " + q(launchTarget) + " --args " + args;
    if (host == "windows") return "start \"\" " + q(launchTarget) + " " + args;
    // double fork: the new editor is not a child of this one (survives its exit, no zombie)
    return "( AIVERSE_NATIVE_EDITOR=1 " + q(launchTarget) + " " + args + " >/dev/null 2>&1 & )";
}

bool isProjectBuildExe(const std::string& exe) {
    const fs::path p = fs::path(exe).lexically_normal();
    fs::path prev;
    for (const auto& part : p) {
        if ((prev == "Builds" || prev == ".temp") && part.string().rfind("editor-", 0) == 0) return true;
        prev = part;
    }
    return false;
}

} // namespace aiverse::editor
