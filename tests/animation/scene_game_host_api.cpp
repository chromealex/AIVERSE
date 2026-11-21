// Stage 176: compile-time native game-host integration fixture.
// No GPU device required. Full CMake/CTest and actual scene load exercise
// are deferred to the coordinated Mac acceptance campaign.
#include "engine/view/MotionWeaveSceneAssetBuilder.h"
#include "engine/view/MotionWeaveScenePrefabBindings.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace aiverse;

namespace {
// Deliberately not called; forces the C++ compiler/linker to instantiate
// the REAL SceneLoadCoordinator::commitReady() adapter, not just check
// method names through requires expressions.
bool instantiateCommitPath(view::MotionWeaveSceneGameHost& host,
                           scene::SceneTicket ticket) {
    auto build=[](const scene::SceneAsset&,
                  view::MotionWeaveSceneGameHost::Owner&,
                  std::vector<view::MotionWeaveSceneGameHost::Object>&,
                  std::string&) -> bool {
        return false; // no GPU allocations in a compile fixture
    };
    view::MotionWeaveSceneGameHost::Statistics stats;
    std::string error;
    return host.commitReady(ticket,build,stats,&error);
}
// Also instantiate the shared-residency path against canonical native
// coordinator types; no GPU owner is created by this deferred fixture.
bool instantiateSharedCommitPath(view::MotionWeaveSceneGameHost& host,
                                 scene::SceneTicket ticket) {
    auto build=[](const scene::SceneAsset&,
                  const render::ModelAssetProvider*,
                  view::MotionWeaveSceneGameHost::Owner&,
                  std::vector<view::MotionWeaveSceneGameHost::Object>&,
                  std::string&) -> bool {
        return false;
    };
    view::MotionWeaveSceneGameHost::Statistics stats;
    std::string error;
    return host.commitReadyShared(
        ticket,"common/characters",build,stats,&error);
}
// Force prefab bootstrap's actual native dedicated/shared commit templates
// to instantiate against canonical SceneLoadCoordinator and GPU owner types.
bool instantiatePrefabBootstrap(view::MotionWeaveScenePrefabBindings& bindings,
                                view::MotionWeaveSceneGameHost& host,
                                gfx::Device& device,scene::SceneTicket ticket,
                                render::RenderDeviceLifetime::Lease lease) {
    view::MotionWeaveSceneGameHost::Statistics stats;
    std::string error;
    return bindings.commitReadyShared(
        host,ticket,device,std::move(lease),render::FileReader{},stats,&error);
}
} // namespace

int main() {
    using Host=view::MotionWeaveSceneGameHost;
    static_assert(!std::is_copy_constructible_v<Host>);
    static_assert(!std::is_copy_constructible_v<
        view::MotionWeaveSceneAssetBuilder>);
    static_assert(std::is_constructible_v<
        view::MotionWeaveSceneAssetBuilder,gfx::Device&,
        render::RenderDeviceLifetime::Lease,
        render::FileReader,view::MotionWeaveSceneAssetBuilder::Resolve>);
    static_assert(std::is_constructible_v<
        Host,Simulation&,scene::SceneLoadCoordinator&,gfx::Device&,
        render::RenderDeviceLifetime::Lease>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().request(std::string{})),
        scene::SceneTicket>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().restoreResidencyAfterRollback()),
        std::vector<scene::SceneTicket>>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().collectRetired()),bool>);
    static_assert(std::is_same_v<
        decltype(std::declval<Host&>().completeForShutdown()),bool>);
    using SharedLease=render::SceneModelProviderResidency::Lease;
    static_assert(!std::is_copy_constructible_v<SharedLease>);
    static_assert(!std::is_copy_assignable_v<SharedLease>);
    static_assert(std::is_move_constructible_v<SharedLease>);
    static_assert(std::is_move_assignable_v<SharedLease>);
    static_assert(std::is_same_v<
        decltype(std::declval<const Host&>().sharedProviderReferences(
            std::declval<const std::string&>())),std::size_t>);
    (void)&instantiateCommitPath;
    (void)&instantiateSharedCommitPath;
    (void)&instantiatePrefabBootstrap;

    // Real canonical SceneState types from main, with persistent and
    // preparing records. Only Active scenes can retain visuals on Replace.
    scene::SceneState states;
    states.scenes.emplace(12,scene::SceneRecord{
        "scenes/active",scene::ScenePhase::Active,true});
    states.scenes.emplace(13,scene::SceneRecord{
        "scenes/loading",scene::ScenePhase::Loading,false});
    states.scenes.emplace(14,scene::SceneRecord{
        "scenes/other",scene::ScenePhase::Active,false});
    const auto ids=
        view::MotionWeaveSceneCoordinatorAdapter::
            canonicalActiveSceneIds(&states);
    if(ids!=std::vector<uint64_t>{12,14}) {
        std::fputs("MotionWeave SceneState ACTIVE filter mismatch\n",stderr);
        return 1;
    }
    // Catalogue semantics are independent of native Device creation.
    // Unknown prefab and duplicate registration must never quietly turn
    // scene objects into invisible visual entities.
    view::MotionWeaveScenePrefabBindings bindings;
    std::string bindingError;
    if(!bindings.registerNonvisual("logic/trigger",&bindingError) ||
       bindingError.size() ||
       bindings.registerNonvisual("logic/trigger",&bindingError) ||
       bindingError.empty()) {
        std::fputs("MotionWeave prefab duplicate contract mismatch\\n",stderr);
        return 1;
    }
    view::MotionWeaveScenePrefabBindings::Resolve fresh=
        [](const scene::SceneObjectAsset&,render::ModelAssetProvider&,
           gfx::Device&,std::optional<Host::Object>&,
           std::string&)->bool { return false; };
    view::MotionWeaveScenePrefabBindings::ResolveResident resident=
        [](const scene::SceneObjectAsset&,
           const render::ModelAssetProvider&,
           std::optional<Host::Object>&,
           std::string&)->bool { return false; };
    if(!bindings.registerVisual("characters/human",fresh,resident,&bindingError) ||
       !bindings.seal("characters/base-pack@v1",&bindingError) ||
       !bindings.sealed() ||
       bindings.residencyKey()!="characters/base-pack@v1" ||
       bindings.prefabCount()!=2 ||
       bindings.registerNonvisual("logic/late",&bindingError) ||
       bindingError.empty()) {
        std::fputs("MotionWeave prefab seal contract mismatch\\n",stderr);
        return 1;
    }
    std::puts("MotionWeave typed scene host contract OK");
}
