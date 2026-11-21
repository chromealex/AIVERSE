#include "engine/view/View.h"
#include "../AlwaysCheck.h"
#include <cstddef>
#include <variant>
#include <vector>
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::view;
    ScenePacketBatch batch;
    batch.reserve(6);
    Object object;object.id=42;object.generation=7;
    MeshObject mesh;mesh.flags=128;
    mesh.selectionOutline=true;
    mesh.selectionColor={0.25f,0.62f,1.0f,1.0f};
    LineObject line{{0,0,0},{0,1,0},0xff};
    TriangleObject triangle{{0,0,0},{1,0,0},{0,1,0},0xffffffff};
    CircleXZObject circle{{3,0,4},.7f,0xff00ff00u,24,.05f};
    QuadXZObject ribbon{{0,0,0},{4,0,5},.5f,0xffaabbccu,.04f};
    batch.add(object);
    batch.add(mesh);
    batch.add(line);
    batch.add(triangle);
    batch.add(circle);
    batch.add(ribbon);
    AIVERSE_TEST_CHECK(batch.size()==6);
    const auto& packets=batch.packets();
    AIVERSE_TEST_CHECK(std::holds_alternative<Object>(packets[0]));
    AIVERSE_TEST_CHECK(std::get<Object>(packets[0]).id==42);
    AIVERSE_TEST_CHECK(std::holds_alternative<MeshObject>(packets[1]));
    AIVERSE_TEST_CHECK(std::get<MeshObject>(packets[1]).flags==128);
    AIVERSE_TEST_CHECK(std::get<MeshObject>(packets[1]).selectionOutline);
    AIVERSE_TEST_CHECK(std::get<MeshObject>(packets[1]).selectionColor.x==0.25f);
    AIVERSE_TEST_CHECK(std::get<MeshObject>(packets[1]).selectionColor.w==1.0f);
    AIVERSE_TEST_CHECK(std::holds_alternative<LineObject>(packets[2]));
    AIVERSE_TEST_CHECK(std::get<LineObject>(packets[2]).end.y==1.f);
    AIVERSE_TEST_CHECK(std::holds_alternative<TriangleObject>(packets[3]));
    AIVERSE_TEST_CHECK(std::get<TriangleObject>(packets[3]).c.y==1.f);
    AIVERSE_TEST_CHECK(std::holds_alternative<CircleXZObject>(packets[4]));
    AIVERSE_TEST_CHECK(std::get<CircleXZObject>(packets[4]).segments==24);
    AIVERSE_TEST_CHECK(std::holds_alternative<QuadXZObject>(packets[5]));
    AIVERSE_TEST_CHECK(std::get<QuadXZObject>(packets[5]).start.x==0.f);
    AIVERSE_TEST_CHECK(std::get<QuadXZObject>(packets[5]).end.z==5.f);
    AIVERSE_TEST_CHECK(std::get<QuadXZObject>(packets[5]).width==.5f);
    AIVERSE_TEST_CHECK(std::get<QuadXZObject>(packets[5]).y==.04f);
    // Updating the same mixed stream must count only Object packets;
    // empty/model-less objects must not allocate animation sources.
    View scene(0);
    const auto first=scene.update(batch,1.f/60.f);
    AIVERSE_TEST_CHECK(first.visible==1);
    AIVERSE_TEST_CHECK(first.drawable==0);
    AIVERSE_TEST_CHECK(first.animated==0);
    AIVERSE_TEST_CHECK(scene.presentationEntity(42)==aiverse::kNullEntity);
    const auto second=scene.update(batch,1.f/60.f);
    AIVERSE_TEST_CHECK(second.visible==first.visible);
    AIVERSE_TEST_CHECK(second.animated==first.animated);
    batch.clear();AIVERSE_TEST_CHECK(batch.size()==0);
    const auto empty=scene.update(batch,1.f/60.f);
    AIVERSE_TEST_CHECK(empty.visible==0 && empty.animated==0);
    // A host with a separate presentation World (e.g. montage/crowd
    // controller) can stage precomputed model packets without supplying
    // their graph to View. The mixed order is not sorted or deduplicated.
    ScenePacketBatch precomputed;
    Object alive;alive.id=5;alive.generation=2;
    MeshObject fallback;fallback.flags=73;
    AIVERSE_TEST_CHECK(!fallback.selectionOutline); // ordinary unselected mesh keeps its old path
    Object dead;dead.id=9;dead.generation=3;
    precomputed.add(alive);
    precomputed.add(fallback);
    precomputed.add(dead);
    AIVERSE_TEST_CHECK(precomputed.size()==3);
    AIVERSE_TEST_CHECK(std::get<Object>(precomputed.packets()[0]).id==5);
    AIVERSE_TEST_CHECK(std::get<MeshObject>(precomputed.packets()[1]).flags==73);
    AIVERSE_TEST_CHECK(std::get<Object>(precomputed.packets()[2]).id==9);
    // Without an attached graph, View does not create a second player.
    const auto staged=scene.update(precomputed,1.f/60.f);
    AIVERSE_TEST_CHECK(staged.animated==0);
    AIVERSE_TEST_CHECK(scene.presentationEntity(5)==aiverse::kNullEntity);
    AIVERSE_TEST_CHECK(scene.presentationEntity(9)==aiverse::kNullEntity);
    // A candidate has its own source bridge, clocks and mutable graph state.
    aiverse::render::Model model;model.nodes.resize(1);model.order={0};
    auto operation=std::make_shared<aiverse::render::AnimationOperation>();
    operation->stateFloats=1;
    operation->update=[](const auto&,const auto&,float dt,std::span<float> state) noexcept {state[0]+=dt;};
    operation->evaluate=[](const auto&,const auto&,const auto&,std::span<const float> state,auto& pose) noexcept {pose[0].t.y=state[0];};
    auto graph=aiverse::render::AnimationGraph::compile(model,aiverse::Json::parse(R"({
        "nodes":[{"id":"base","kind":"bind"},
        {"id":"clock","kind":"native","input":"base","operation":"clock"}],"output":"clock"})"),{{"clock",operation}});
    Object animated;animated.id=77;animated.draw.model=&model;animated.graph=graph;
    std::vector<Object> input{animated};
    scene.update(input,.1f);
    using Player=aiverse::lib::animation::AnimationPlayerComponent;
    auto original=scene.presentation().get<Player>(scene.presentationEntity(77)).instance;
    auto candidate=scene.prepareUpdate(input,.1f);
    auto stagedPlayer=candidate->presentation().get<Player>(candidate->presentationEntity(77)).instance;
    AIVERSE_TEST_CHECK(original!=stagedPlayer);
    AIVERSE_TEST_CHECK(std::abs(original->stateTime()-.1f)<.001f);
    AIVERSE_TEST_CHECK(std::abs(stagedPlayer->stateTime()-.2f)<.001f);
    aiverse::render::Pose originalPose,nextPose;
    original->evaluate(originalPose);stagedPlayer->evaluate(nextPose);
    AIVERSE_TEST_CHECK(std::abs(originalPose[0].t.y-.1f)<.001f);
    AIVERSE_TEST_CHECK(std::abs(nextPose[0].t.y-.2f)<.001f);
    candidate.reset();
    auto retry=scene.prepareUpdate(input,.1f);
    AIVERSE_TEST_CHECK(std::abs(retry->presentation().get<Player>(retry->presentationEntity(77)).instance->stateTime()-.2f)<.001f);
    // The parallel path keeps one scheduler (worker threads) across
    // preparation candidates instead of spawning one per prepared frame.
    {
        aiverse::View parallel(1);
        parallel.setParallelThreshold(1);
        (void)parallel.update(input,.1f);
        auto first=parallel.prepareUpdate(input,.1f);
        AIVERSE_TEST_CHECK(first->sharesAnimationScheduler(parallel));
        auto second=first->prepareUpdate(input,.1f);
        AIVERSE_TEST_CHECK(second->sharesAnimationScheduler(parallel));
        AIVERSE_TEST_CHECK(std::abs(second->presentation().get<Player>(
            second->presentationEntity(77)).instance->stateTime()-.3f)<.001f);
    }
    input[0].id=0;bool rejected=false;
    try {auto invalid=scene.prepareUpdate(input,.1f);}catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected && std::abs(original->stateTime()-.1f)<.001f);
}
