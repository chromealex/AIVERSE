#include "apps/studio-native/StudioContentDirectory.h"
#include "tests/TestCheck.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using aiverse::studio::studioContentFiles;
using aiverse::studio::studioContentRoot;

struct TemporaryProject {
    fs::path path;
    TemporaryProject() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("aiverse-studio-content-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TemporaryProject() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

int main() {
    TemporaryProject project;
    std::ofstream(project.path / "game.json") << "{}";
    std::ofstream(project.path / "rules.json") << "{}";
    AIVERSE_TEST_CHECK(!fs::exists(project.path / "content"));

    const fs::path root = studioContentRoot(project.path);
    AIVERSE_TEST_CHECK(root == project.path / "content");
    AIVERSE_TEST_CHECK(fs::is_directory(root));
    AIVERSE_TEST_CHECK(studioContentRoot(project.path) == root); // idempotent
    AIVERSE_TEST_CHECK(studioContentFiles(project.path).empty());

    fs::create_directories(root / "Models" / "Vehicles");
    std::ofstream(root / "Models" / "Vehicles" / "Ship.glb") << "mesh";
    std::ofstream(root / "stone.png") << "png";
    const auto assets = studioContentFiles(project.path);
    AIVERSE_TEST_CHECK(assets.size() == 2);
    AIVERSE_TEST_CHECK(assets[0].path == "Models/Vehicles/Ship.glb");
    AIVERSE_TEST_CHECK(assets[0].bytes == 4);
    AIVERSE_TEST_CHECK(assets[1].path == "stone.png");
    AIVERSE_TEST_CHECK(studioContentFiles(project.path, 1).size() == 1);
    // game.json and rules.json are NOT visible in the Assets browser.
    for (const auto& file : assets) {
        AIVERSE_TEST_CHECK(file.path != "game.json" && file.path != "rules.json");
        AIVERSE_TEST_CHECK(file.path.rfind("content/", 0) != 0);
    }

    // Symlinks to project files or external paths cannot leak into this browser.
    std::error_code ec;
    fs::create_symlink(project.path / "game.json", root / "private.json", ec);
    if (!ec) {
        for (const auto& file : studioContentFiles(project.path))
            AIVERSE_TEST_CHECK(file.path != "private.json");
        fs::remove(root / "private.json");
    }
    ec.clear();
    fs::create_directory_symlink(project.path, root / "outside", ec);
    if (!ec) {
        for (const auto& file : studioContentFiles(project.path))
            AIVERSE_TEST_CHECK(file.path.rfind("outside/", 0) != 0);
        fs::remove(root / "outside");
    }
    fs::rename(root, project.path / "content-backup");
    ec.clear();
    fs::create_directory_symlink(project.path / "content-backup", root, ec);
    if (!ec) {
        bool refused = false;
        try { (void)studioContentRoot(project.path); }
        catch (const std::runtime_error&) { refused = true; }
        AIVERSE_TEST_CHECK(refused);
        fs::remove(root);
    }
    fs::rename(project.path / "content-backup", root);
    AIVERSE_TEST_CHECK(studioContentFiles(project.path).size() == 2);

    std::ofstream(project.path / "content-conflict") << "file";
    TemporaryProject other;
    std::ofstream(other.path / "content") << "not-a-directory";
    bool rejected = false;
    try { (void)studioContentRoot(other.path); }
    catch (const std::runtime_error&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);

    std::cout << "studio content directory and sandboxed listing PASS\n";
}
