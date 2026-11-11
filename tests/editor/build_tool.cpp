#include "apps/editor/core/BuildTool.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    namespace fs = std::filesystem;
    const fs::path root = argv[1];
    const fs::path engine = root / "engine";
    const fs::path editor = root / "custom/Release/AiverseEditor.app/Contents/MacOS";
#if defined(_WIN32)
    const char* name = "aiversebuild.exe";
#else
    const char* name = "aiversebuild";
#endif
    auto put = [](const fs::path& file) { fs::create_directories(file.parent_path()); std::ofstream(file).put('\n'); };
    auto check = [&](const fs::path& expected) {
        auto actual = aiverse::editor::findBuildTool(editor.string(), engine.string());
        if (actual != fs::canonical(expected).string()) throw std::runtime_error("Wrong tool: " + actual);
    };
    fs::remove_all(root);
    fs::create_directories(editor);
    if (aiverse::editor::findBuildTool(editor.string(), engine.string()) != name) return 1;
    const auto fallback = engine / "build/macos/Release" / name;
    put(fallback); check(fallback);
    // A directory with the tool's name is not a usable executable.
    fs::create_directories(editor / name); check(fallback);
    fs::remove(editor / name);
    const auto sibling = root / "custom/Release" / name;
    put(sibling); check(sibling);
    const auto bundled = editor / name;
    put(bundled); check(bundled);
    const auto explicitTool = root / "tools" / name;
    put(explicitTool);
    if (aiverse::editor::findBuildTool((root / "tools").string(), engine.string()) != fs::canonical(explicitTool).string()) return 1;
    std::cout << "Build tool discovery passed\n";
}
