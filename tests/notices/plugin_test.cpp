#include "tools/aiversebuild/Plugins.h"
#include "tools/aiversebuild/Notices.h"
#include <cassert>
#include <iostream>
namespace fs = std::filesystem;
using namespace aiverse::build;
static void write(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p) << s; }
static std::string read(const fs::path& p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main(int argc, char** argv) {
    assert(argc == 3);
    const fs::path engine = argv[1], project = argv[2], out = project / "Builds";
    write(project / "fonts/OFL.txt", "font license\n");
    write(project / "native/vendor/LICENSE", "native library license\n");
    write(project / "models/LICENSE_asset.txt", "model license\n");
    write(out / "old/LICENSE", "must not leak\n");
    collectProjectNotices(project, out, out / "project-notices.txt");
    const auto assets = read(out / "project-notices.txt");
    assert(assets.find("model license") != std::string::npos);
    assert(assets.find("font license") != std::string::npos);
    assert(assets.find("native library license") != std::string::npos);
    assert(assets.find("must not leak") == std::string::npos);
    write(project / "plugins/test/plugin.json", R"({"android":{"dependencies":["test:sdk:1"]},"web":{"scripts":["https://example.invalid/sdk.js"]}})");
    auto set = loadPlugins(engine, project, R"({"plugins":{"test":{}}})", "android", "");
    bool failed = false;
    try { writePluginNotices(set, out); } catch (const std::runtime_error&) { failed = true; }
    assert(failed);
    write(project / "licenses/sdk.txt", "SDK and transitive dependency licenses\n");
    set = loadPlugins(engine, project, R"({"plugins":{"test":{"notices":["licenses/sdk.txt"]}}})", "android", "");
    writePluginsCMake(set, out);
    assert(read(out / "plugin-notices.txt").find("SDK and transitive") != std::string::npos);
    assert(read(out / "plugins.cmake").find("AIVERSE_PLUGIN_NOTICES") != std::string::npos);
    set = loadPlugins(engine, project, R"({"plugins":{"test":{"notices":["missing.txt"]}}})", "android", "");
    failed = false;
    try { writePluginNotices(set, out); } catch (const std::runtime_error&) { failed = true; }
    assert(failed);
    set = loadPlugins(engine, project, "{}", "android", "");
    writePluginNotices(set, out);
    assert(read(out / "plugin-notices.txt").find("SDK and transitive") == std::string::npos);
    // Platform-specific configuration, override and unsupported platform isolation.
    set = loadPlugins(engine, project, R"({"plugins":{"test":{"notices":{"android":["licenses/sdk.txt"],"web":["missing.txt"]}}}})", "android", "test");
    writePluginNotices(set, out);
    set = loadPlugins(engine, project, R"({"plugins":{"test":{}}})", "linux", "test");
    writePluginNotices(set, out);
    std::cout << "plugin and project notices OK\n";
}
