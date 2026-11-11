#include "NativeBuildOps.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;

namespace aiverse::editor {

namespace {
std::string findOnPath(const std::string& name) {
#if defined(_WIN32)
    const char sep = ';';
    const std::string exe = name + ".exe";
#else
    const char sep = ':';
    const std::string exe = name;
#endif
    std::vector<fs::path> dirs;
    if (const char* p = std::getenv("PATH")) {
        std::string s = p, cur;
        for (char c : s + sep) { if (c == sep) { if (!cur.empty()) dirs.push_back(cur); cur.clear(); } else cur += c; }
    }
    // a GUI app started from Finder has a minimal PATH
    for (const char* d : {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"}) dirs.push_back(d);
    if (const char* h = std::getenv("HOME")) {
        dirs.push_back(fs::path(h) / ".aiverse" / "toolchains" / "py" / "bin");
        dirs.push_back(fs::path(h) / ".rtsengine" / "toolchains" / "py" / "bin"); // existing installations
        dirs.push_back(fs::path(h) / ".local" / "bin");
    }
    std::error_code ec;
    for (auto& d : dirs) if (fs::is_regular_file(d / exe, ec)) return (d / exe).string();
    return {};
}

Json strings(const std::vector<std::string>& v) {
    Json a = Json::array();
    for (auto& s : v) a.push(Json::string(s));
    return a;
}
} // namespace

NativeTools findNativeTools(const Workspace& ws) {
    NativeTools t;
    try {
        Json st = ws.toolchainCheck(nativeHost());
        for (auto& r : st["requirements"].items()) {
            const std::string id = r.getStr("id");
            if (id == "cmake") {
                if (r.getBool("found")) t.cmake = r.getStr("path");
                else { t.problem = "CMake не найден: " + r.getStr("detail", "не установлен"); t.cmakeAutoInstall = r.getBool("autoInstall"); }
            } else if (id == "ninja" && r.getBool("found")) t.ninja = r.getStr("path");
            else if (r.getBool("required") && !r.getBool("found") && id != "cmake" && t.problem.empty())
                t.problem = r.getStr("name", id) + " не найден: " + r.getStr("detail") + (r.has("manual") ? " (" + r.getStr("manual") + ")" : "");
        }
    } catch (const std::exception&) {
        // aiversebuild missing or too old: plain search
        t.cmake = findOnPath("cmake");
        t.ninja = findOnPath("ninja");
        if (t.cmake.empty()) { t.problem = "CMake не найден (PATH, /opt/homebrew/bin, /usr/local/bin)"; t.cmakeAutoInstall = true; }
    }
    if (!t.cmake.empty() && t.problem.rfind("CMake", 0) == 0) t.problem.clear();
    return t;
}

NativeBuildPlan nativePlanFor(const Workspace& ws, const NativeTools& tools) {
    const std::string bd = nativeBuildDir(ws.project.dir, nativeHost());
    std::error_code ec;
    const bool cache = fs::exists(fs::path(bd) / "CMakeCache.txt", ec);
    return planNativeBuild(ws.engineDir, ws.project.dir, nativeHost(), tools.cmake, tools.ninja, cache, nativeBuildJobs());
}

std::string startNativeBuild(Workspace& ws, const NativeBuildPlan& plan) {
    auto fail = [&](const std::string& e) {
        ws.log("native", "Сборка редактора с модулями проекта: " + e, true);
        ws.appendBuildLog("ОШИБКА: " + e);
        return e;
    };
    if (ws.project.dir.empty()) return fail("проект не открыт");
    if (ws.building()) return fail("уже идёт другая сборка (" + ws.runLabel() + ")");
    if (plan.cmake == "cmake" && findOnPath("cmake").empty()) return fail("CMake не найден");
    if (!fs::exists(fs::path(plan.engineDir) / "CMakeLists.txt")) return fail("нет исходников движка в " + plan.engineDir + " (AIVERSE_SOURCE_DIR)");
    if (std::string p = nativeBuildDirProblem(plan.engineDir, plan.buildDir); !p.empty()) return fail(p);
    std::error_code ec;
    fs::create_directories(plan.buildDir, ec);
    if (ec) return fail("не удалось создать " + plan.buildDir + ": " + ec.message());
    // ignored in any repository the project may live in (its own git, not only the engine's)
    if (std::ofstream f(fs::path(plan.buildDir) / ".gitignore"); f) f << "# per-project editor build (AIVERSE): never commit\n*\n";
    if (std::string p = nativeBuildGitProblem(plan.buildDir); !p.empty()) return fail(p);
    const std::string cmd = nativeBuildCommand(plan);
    try { ws.startCommand(cmd, "$ " + cmd, kNativeBuildLabel); }
    catch (const std::exception& e) { return fail(e.what()); }
    ws.log("native", "Сборка редактора с модулями проекта запущена: " + plan.buildDir);
    return {};
}

NativeBuilt findNativeBuilt(const Workspace& ws) {
    NativeBuilt b;
    if (ws.project.dir.empty()) return b;
    std::error_code ec;
    // Ninja / Makefiles layout, then Visual Studio's Release/
    for (bool vs : {false, true}) {
        if (vs && nativeHost() != "windows") break;
        NativeBuildPlan p = planNativeBuild(ws.engineDir, ws.project.dir, nativeHost(), "", vs ? "" : "ninja", true, 0);
        if (b.editor.empty() || fs::exists(p.editorBinary, ec)) { b.editor = p.editorBinary; b.launch = p.launchTarget; b.headless = p.headlessBinary; }
        if (fs::exists(p.editorBinary, ec)) return b;
    }
    b.editor.clear();
    return b;
}

bool nativeBuildReady(const Workspace& ws, const NativeBuilt& b, const std::string& engineExe, std::string* why) {
    if (b.editor.empty()) { if (why) *why = "редактор для проекта ещё не собран"; return false; }
    if (!nativeBuildUpToDate(b.editor, ws.project.dir, engineExe.empty() ? ws.buildTool : engineExe, why)) return false;
    return nativeBuildHasModules(nativeBuildDir(ws.project.dir, nativeHost()), requiredNativeModules(ws.project.rulesJson), why);
}

Json nativeStatus(Workspace& ws, const std::string& engineExe) {
    const auto compiled = compiledNativeIds();
    Json r = Json::object();
    r.set("required", strings(requiredNativeModules(ws.project.rulesJson)));
    r.set("compiled", strings(compiled));
    r.set("missing", strings(missingNativeModules(ws.project.rulesJson, compiled)));
    if (ws.project.dir.empty()) return r;
    const NativeBuildPlan plan = planNativeBuild(ws.engineDir, ws.project.dir, nativeHost(), "", "ninja", true, 0);
    const NativeBuilt b = findNativeBuilt(ws);
    r.set("buildDir", Json::string(plan.buildDir));
    r.set("editor", Json::string(b.editor.empty() ? plan.editorBinary : b.editor));
    r.set("launch", Json::string(b.editor.empty() ? plan.launchTarget : b.launch));
    r.set("headless", Json::string(b.editor.empty() ? plan.headlessBinary : b.headless));
    r.set("built", Json::fromBool(!b.editor.empty()));
    r.set("buildModules", strings(nativeBuildModules(plan.buildDir)));
    std::string why;
    r.set("upToDate", Json::fromBool(!b.editor.empty() && nativeBuildUpToDate(b.editor, ws.project.dir, engineExe.empty() ? ws.buildTool : engineExe)));
    r.set("ready", Json::fromBool(nativeBuildReady(ws, b, engineExe, &why)));
    if (!why.empty()) r.set("why", Json::string(why));
    r.set("building", Json::fromBool(ws.building() && ws.runLabel() == kNativeBuildLabel));
    if (std::string p = nativeBuildDirProblem(ws.engineDir, plan.buildDir); !p.empty()) r.set("problem", Json::string(p));
    return r;
}

void registerNativeBuildOps(Workspace& ws, const std::string& engineExe) {
    Workspace* w = &ws;
    ws.registerOp({"native_status",
        "C++ modules of the open project (rules.json nativeModules) against what this editor binary has compiled in: "
        "required, compiled, missing; the per-project editor build (<project>/Builds/.temp/editor-<host>): buildDir, editor / headless "
        "binary paths, built, buildModules, upToDate (newer than native/ sources and the engine binary), ready (up to date and has every module: the editor switches to it on open without asking), why. See docs/native-modules.md.",
        Json::parse("{}"), [w, engineExe](const Json&) { return nativeStatus(*w, engineExe); }, false});
    ws.registerOp({"native_build",
        "Configure + build the engine for the open project (-DAIVERSE_CONTENT_DIR=<project>, Release, targets aiverse_editor + aiverse_headless) "
        "into <project>/Builds/.temp/editor-<host> (git-ignored; never touches the engine checkout). Output streams into the build log: "
        "poll build_status, then native_status. wait=true blocks until the build ends (aiversemcp without the editor; inside the "
        "editor the window freezes meanwhile). Returns the editor / headless binary paths. Restart into it: GUI only.",
        Json::parse(R"({"properties": {"wait": {"type": "boolean"}}})"), [w, engineExe](const Json& p) {
            if (w->project.dir.empty()) throw std::runtime_error("no game is open");
            NativeTools tools = findNativeTools(*w);
            if (tools.cmake.empty()) {
                w->log("native", "Сборка редактора с модулями проекта: " + tools.problem, true);
                w->appendBuildLog("ОШИБКА: " + tools.problem);
                throw std::runtime_error(tools.problem + (tools.cmakeAutoInstall ? " (toolchain_install {\"id\": \"cmake\"})" : ""));
            }
            const NativeBuildPlan plan = nativePlanFor(*w, tools);
            if (std::string e = startNativeBuild(*w, plan); !e.empty()) throw std::runtime_error(e);
            Json r = Json::object();
            if (p.getBool("wait")) {
                while (w->building()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
                const int rc = w->buildResult();
                if (rc != 0) {
                    w->log("native", "Сборка редактора с модулями проекта не удалась (код " + std::to_string(rc) + "), журнал: " + w->lastLogFile(), true);
                    std::string log = w->buildLog();
                    if (log.size() > 3000) log = "..." + log.substr(log.size() - 3000);
                    throw std::runtime_error("native build failed (exit " + std::to_string(rc) + "):\n" + log);
                }
                w->log("native", "Редактор с модулями проекта собран: " + plan.editorBinary);
            }
            r = nativeStatus(*w, engineExe);
            r.set("ok", Json::fromBool(true));
            r.set("started", Json::fromBool(true));
            r.set("command", Json::string(nativeBuildCommand(plan)));
            r.set("summary", Json::string(p.getBool("wait") ? "Редактор с модулями проекта собран: " + plan.editorBinary
                                                            : "Сборка редактора с модулями проекта запущена (build_status — ход сборки)"));
            return r;
        }, false});
}

} // namespace aiverse::editor
