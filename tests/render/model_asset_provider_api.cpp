// Deferred native compile fixture: generic model source provider API.
// The real Metal/Vulkan/GL loading and GPU lifetime tests are separate.
#include "engine/render/ModelAssetProvider.h"
#include "../AlwaysCheck.h"
#include <chrono>
#include <thread>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

int main() {
    using namespace aiverse;
    using render::ModelAssetProvider;
    static_assert(std::is_constructible_v<ModelAssetProvider,render::FileReader>);
    static_assert(!std::is_copy_constructible_v<ModelAssetProvider>);
    static_assert(std::is_same_v<
        decltype(std::declval<ModelAssetProvider&>().resources()),
        render::ModelResourceCache&>);
    render::FileReader reader=[](const std::string&,std::string&) {
        return false;
    };
    ModelAssetProvider provider(reader);
    AIVERSE_TEST_CHECK(provider.find("models/missing.glb")==nullptr);
    AIVERSE_TEST_CHECK(provider.resources().generation()==0);
    AIVERSE_TEST_CHECK(provider.resources().pendingRetirements()==0);
    AIVERSE_TEST_CHECK(provider.warnings().empty());
    provider.clearWarnings();
    // An authoring host can inspect/validate prepared CPU data without
    // publishing a Model or touching a gfx::Device.
    provider.setReader([](const std::string&,std::string& bytes) {
        bytes="v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        return true;
    });
    AIVERSE_TEST_CHECK(provider.requestAsync("preview.obj",render::ModelLoadPriority::Interactive));
    ModelAssetProvider::Prepared prepared;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!provider.takePrepared(prepared) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AIVERSE_TEST_CHECK(prepared.model && prepared.source=="preview.obj");
    AIVERSE_TEST_CHECK(!prepared.model->vb.valid() && !prepared.model->ib.valid());
    AIVERSE_TEST_CHECK(provider.find("preview.obj")==nullptr);
    provider.cancelAsync("preview.obj");
    provider.stopAsync();
    AIVERSE_TEST_CHECK(provider.cpuStats().liveThreads==0);
    return 0;
}
