// Stage 98: editor-owned watcher and missing-file regression.
// Fixture intentionally deferred; run through CTest after feature phase.
#include "apps/editor/core/VisualAssetDependencyService.h"
#include "../AlwaysCheck.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::editor;
using namespace aiverse::game::view_adapter;
namespace fs=std::filesystem;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
struct TestType {Json view;};

int main() {
    const auto root=fs::temp_directory_path()/"aiverse_visual_dependency_service_fixture";
    std::error_code ec;
    fs::remove_all(root,ec);
    fs::create_directories(root/"models");
    fs::create_directories(root/"animation");
    {
        std::ofstream(root/"models/hero.glb")<<"initial";
        std::ofstream(root/"animation/hero.json")<<"{}";
    }

    VisualAssetDependencyService service;
    RtsModelResourceCache cache;
    std::vector<TestType> types{{
        Json::parse(R"({"model":"models/hero.glb",
                       "animationGraph":"animation/hero.json"})")}};
    const auto library=Json::object();
    int conversions=0, logs=0;
    auto convert=[&](const std::string&)->bool{++conversions;return false;};
    auto log=[&](const std::string&){++logs;};

    AIVERSE_TEST_CHECK(!service.due(root.string(),0.5));
    AIVERSE_TEST_CHECK(service.due(root.string(),0.5));
    service.scan(root.string(),types,library,cache,convert,log);
    AIVERSE_TEST_CHECK(!service.pending());
    AIVERSE_TEST_CHECK(conversions==0); // GLB already native
    // External graph changes are independent of model GPU cache residency.
    const auto graph=root/"animation/hero.json";
    auto initialGraphStamp=fs::last_write_time(graph);
    fs::last_write_time(graph,initialGraphStamp+std::chrono::seconds(4));
    service.scan(root.string(),types,library,cache,convert,log);
    AIVERSE_TEST_CHECK(service.pending());
    auto graphImpact=service.impact();
    AIVERSE_TEST_CHECK(graphImpact.graph && !graphImpact.model);
    AIVERSE_TEST_CHECK(graphImpact.types.count(0)==1);
    service.acknowledge();

    // Deleted and recovered files are each observable changes. The service
    // retains pending work during play until the host acknowledges it.
    const auto model=root/"models/hero.glb";
    fs::remove(model);
    service.scan(root.string(),types,library,cache,convert,log);
    AIVERSE_TEST_CHECK(service.pending());
    AIVERSE_TEST_CHECK(service.impact().model);
    service.scan(root.string(),types,library,cache,convert,log);
    AIVERSE_TEST_CHECK(service.pending());
    service.acknowledge();
    std::ofstream(model)<<"recovered";
    service.scan(root.string(),types,library,cache,convert,log);
    AIVERSE_TEST_CHECK(service.pending());
    AIVERSE_TEST_CHECK(service.changedSources().count("models/hero.glb")==1);

    // Switching projects clears old pending paths and timestamps.
    AIVERSE_TEST_CHECK(!service.due((root/"other").string(),0.01));
    AIVERSE_TEST_CHECK(!service.pending());
    fs::remove_all(root,ec);
    return 0;
}
