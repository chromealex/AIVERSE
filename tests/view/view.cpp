#include "engine/view/View.h"
#include "../AlwaysCheck.h"
#include <cmath>
#include <stdexcept>
#include <type_traits>
using namespace aiverse;
using namespace aiverse::render;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    static_assert(std::is_same_v<
        decltype(&View::submitModel),
        void(*)(Renderer3D&,const ModelDraw&)>);
    static_assert(std::is_same_v<
        decltype(&View::submitMesh),
        void(*)(Renderer3D&,const view::MeshObject&)>);
    static_assert(std::is_same_v<Camera3D,RTSCamera>,
                  "legacy camera alias must remain ABI-compatible");
    Model m;
    m.nodes.resize(1);m.order={0};m.nodes[0].name="root";
    auto graph=AnimationGraph::compile(m,Json::parse(R"({
        "parameters":{"intensity":0.0},
        "nodes":[{"id":"base","kind":"bind"}],
        "output":"base"})"));
    View view(0);
    view::Object animated;
    animated.id=101;animated.generation=7;animated.draw.model=&m;
    animated.graph=graph;
    animated.parameters.set("intensity",.5f);
    view::Object unanimated;
    unanimated.id=202;unanimated.draw.model=&m;
    std::vector<view::Object> objects{animated,unanimated};
    auto result=view.update(objects,.016f);
    AIVERSE_TEST_CHECK(result.source.created==1);
    AIVERSE_TEST_CHECK(result.drawable==2);
    AIVERSE_TEST_CHECK(result.animated==1);
    auto id=view.presentationEntity(101);
    AIVERSE_TEST_CHECK(view.presentation().alive(id));
    AIVERSE_TEST_CHECK(view.presentation().get<lib::animation::AnimationPoseComponent>(id).valid);
    AIVERSE_TEST_CHECK(view.presentation().get<lib::animation::AnimationInputsComponent>(id)
        .overrides.values.at("intensity")==.5f);
    // A rejected batch (duplicate animated source ID) must leave the View
    // untouched: no stats reset, no frame stamp / LOD hysteresis advance.
    {
        std::vector<view::Object> invalid{animated,animated};
        bool threw=false;
        try {(void)view.update(invalid,.016f);}
        catch(const std::invalid_argument&) {threw=true;}
        AIVERSE_TEST_CHECK(threw);
        AIVERSE_TEST_CHECK(view.stats().drawable==2);
        AIVERSE_TEST_CHECK(view.stats().animated==1);
        AIVERSE_TEST_CHECK(view.stats().source.created==1);
        AIVERSE_TEST_CHECK(view.presentation().alive(view.presentationEntity(101)));
    }
    animated.generation++;
    objects[0]=animated;
    result=view.update(objects,.016f);
    AIVERSE_TEST_CHECK(result.source.restarted==1);
    objects.erase(objects.begin());
    result=view.update(objects,.016f);
    AIVERSE_TEST_CHECK(result.source.removed==1);
    AIVERSE_TEST_CHECK(!view.presentation().alive(id));
    // A generic View also accepts ordinary mesh render packets without
    // adding RTS/FPS-specific components or behavior.
    view::MeshObject mesh;
    mesh.visible=false;mesh.mesh=nullptr;
    view::LineObject line{{0,0,0},{1,0,0},0xff0000ffu};
    view::TriangleObject tri{{0,0,0},{1,0,0},{0,1,0},0xffffffffu};
    AIVERSE_TEST_CHECK(line.end.x==1.f && tri.c.y==1.f);
    AIVERSE_TEST_CHECK(!mesh.visible && mesh.mesh==nullptr);
    // RTS presentation-owner regression: an imported model has real clips
    // AND a pure graph. MotionWeave must integrate its graph clock once
    // per update; the dormant legacy Animator clock must remain stationary.
    Model animatedModel;
    animatedModel.nodes.resize(1);
    animatedModel.order={0};
    animatedModel.nodes[0].name="root";
    AnimClip walk;
    walk.name="Walk";
    walk.duration=1.f;
    AnimChannel rootTranslation;
    rootTranslation.node=0;
    rootTranslation.times={0.f,1.f};
    rootTranslation.values={0.f,0.f,0.f,2.f,0.f,0.f};
    walk.channels.push_back(rootTranslation);
    animatedModel.clips.push_back(walk);
    auto pureGraph=AnimationGraph::compile(animatedModel,Json::parse(R"({
        "parameters":{"speed":1.0},
        "nodes":[{"id":"walk","kind":"clip","clip":"Walk","parameter":"speed"}],
        "output":"walk"
    })"));
    view::ScenePacketBatch rtsPackets;
    view::Object rtsObject;
    rtsObject.id=303;
    rtsObject.generation=1;
    rtsObject.draw.model=&animatedModel;
    rtsObject.graph=pureGraph;
    rtsObject.parameters.set("speed",1.f);
    rtsPackets.add(rtsObject);
    Animator dormantLegacy;
    dormantLegacy.setModel(&animatedModel);
    dormantLegacy.play(0,0,Animator::Play{});
    dormantLegacy.update(.125f); // a clock from an earlier legacy frame
    const float legacyClock=dormantLegacy.time(0);
    auto firstRts=view.update(rtsPackets,.1f);
    AIVERSE_TEST_CHECK(firstRts.source.created==1);
    AIVERSE_TEST_CHECK(firstRts.animated==1);
    const Entity rtsEntity=view.presentationEntity(303);
    AIVERSE_TEST_CHECK(view.presentation().alive(rtsEntity));
    auto* player=view.presentation().tryGetConst<
        lib::animation::AnimationPlayerComponent>(rtsEntity);
    AIVERSE_TEST_CHECK(player && player->instance);
    AIVERSE_TEST_CHECK(std::abs(player->instance->stateTime()-.1f)<.0001f);
    const auto& firstPose=view.presentation().get<
        lib::animation::AnimationPoseComponent>(rtsEntity);
    AIVERSE_TEST_CHECK(firstPose.valid && firstPose.local.size()==1);
    const float firstX=firstPose.local[0].t.x;
    AIVERSE_TEST_CHECK(std::abs(firstX-.2f)<.005f);
    auto nextRts=view.update(rtsPackets,.1f);
    AIVERSE_TEST_CHECK(nextRts.source.updated==1 && nextRts.source.restarted==0);
    player=view.presentation().tryGetConst<
        lib::animation::AnimationPlayerComponent>(rtsEntity);
    AIVERSE_TEST_CHECK(player && player->instance);
    AIVERSE_TEST_CHECK(std::abs(player->instance->stateTime()-.2f)<.0001f);
    const auto& secondPose=view.presentation().get<
        lib::animation::AnimationPoseComponent>(rtsEntity);
    AIVERSE_TEST_CHECK(secondPose.valid && std::abs(secondPose.local[0].t.x-.4f)<.005f);
    AIVERSE_TEST_CHECK(std::abs(dormantLegacy.time(0)-legacyClock)<.0001f);
    // RTS switches to legacy when custom graph/control/contact features
    // become active. Reinitialize legacy clocks instead of continuing
    // the old clip at its pre-MotionWeave timestamp.
    rtsPackets.clear();
    auto absent=view.update(rtsPackets,.1f);
    AIVERSE_TEST_CHECK(absent.source.removed==1);
    AIVERSE_TEST_CHECK(!view.presentation().alive(rtsEntity));
    dormantLegacy.setModel(nullptr);
    dormantLegacy.setModel(&animatedModel);
    AIVERSE_TEST_CHECK(dormantLegacy.clip(0)<0);
    AnimationGraphInstance fallbackGraph;
    fallbackGraph.reset(pureGraph);
    fallbackGraph.update(.1f);
    Pose legacyPose;
    fallbackGraph.evaluate(legacyPose);
    AIVERSE_TEST_CHECK(std::abs(fallbackGraph.stateTime()-.1f)<.0001f);
    AIVERSE_TEST_CHECK(std::abs(legacyPose[0].t.x-firstX)<.005f);
    // Returning to generic View after a legacy period starts a fresh
    // player, rather than resuming the discarded presentation entity.
    auto resumed=view.update(rtsPackets,.1f);
    AIVERSE_TEST_CHECK(resumed.animated==0);
    rtsPackets.add(rtsObject);
    resumed=view.update(rtsPackets,.1f);
    AIVERSE_TEST_CHECK(resumed.source.created==1);
    const Entity resumedEntity=view.presentationEntity(303);
    const auto* resumedPlayer=view.presentation().tryGetConst<
        lib::animation::AnimationPlayerComponent>(resumedEntity);
    AIVERSE_TEST_CHECK(resumedPlayer && resumedPlayer->instance);
    AIVERSE_TEST_CHECK(std::abs(resumedPlayer->instance->stateTime()-.1f)<.0001f);

    // Scene compositor is now invoked through canonical View rather
    // than the historical RtsView host. Keep submitted order and exactly
    // one execution of each layer, with disabled layers never invoked.
    view::ViewLayerQueue layers;
    std::vector<int> drawOrder;
    layers.append("terrain",view::LayerPhase::Geometry,
                  [&]{drawOrder.push_back(1);});
    layers.append("disabled",view::LayerPhase::Effects,
                  [&]{drawOrder.push_back(-1);},false);
    layers.append("overlay",view::LayerPhase::Overlay,
                  [&]{drawOrder.push_back(2);});
    const auto queued=View::submitLayers(layers);
    AIVERSE_TEST_CHECK(queued.scheduled==3 && queued.executed==2 && queued.disabled==1);
    AIVERSE_TEST_CHECK((drawOrder==std::vector<int>{1,2}));
    AIVERSE_TEST_CHECK(layers.empty());
    AIVERSE_TEST_CHECK(View::submitLayers(layers).scheduled==0);

    view::ViewLayerProviders providers;
    providers.add("surface",[&](auto& q,const auto& frame) {
        AIVERSE_TEST_CHECK(frame.revision==42);
        q.append("path",view::LayerPhase::Geometry,
                 [&]{drawOrder.push_back(3);});
    });
    providers.add("hidden",[&](auto& q,const auto&) {
        q.append("never",view::LayerPhase::Overlay,
                 [&]{drawOrder.push_back(-1);});
    },false);
    const auto provided=View::submitLayers(providers,{1.0,.016f,42});
    AIVERSE_TEST_CHECK(provided.scheduled==1 && provided.executed==1);
    AIVERSE_TEST_CHECK((drawOrder==std::vector<int>{1,2,3}));

    view.clear();
    AIVERSE_TEST_CHECK(view.presentation().countExisting<lib::animation::AnimationPlayerComponent>()==0);
}
