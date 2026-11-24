// MotionWeave Stage 180 — native semantic facial control + existing IK.
// All checks execute in Release (NDEBUG) via AlwaysCheck.
#include "engine/animation/FacialControlRig.h"
#include "apps/modelview/NativeFacialPreview.h"
#include "engine/animation/EcsAnimationSystems.h"
#include "engine/animation/MotionWeaveScheduler.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <limits>
#include <memory>
#include <string>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&) {return false;}
}

namespace {
bool near(float a,float b,float tolerance=0.003f) {
    return std::abs(a-b)<tolerance;
}
Model modelWithFace(int count=3) {
    Model m;
    m.nodes.resize(2);
    m.order={0,1};
    m.nodes[0].name="Face";
    m.nodes[0].mesh=0;
    m.nodes[0].children={1};
    m.nodes[1].name="Eye.L";
    m.nodes[1].parent=0;
    m.nodes[1].t={0,1,0};
    m.meshes.resize(1);
    m.meshes[0].primitives.resize(1);
    m.meshes[0].primitives[0].morphTargets=count;
    m.meshes[0].morphTargetNames.clear();
    for(int i=0;i<count;++i)
        m.meshes[0].morphTargetNames.push_back("face-"+std::to_string(i));
    return m;
}
AnimationParameters controls() {
    AnimationParameters p;
    p.values.emplace("jawOpen",0.8f);
    p.values.emplace("blinkLeft",0.25f);
    p.values.emplace("gazeWeight",1.f);
    p.vectors.emplace("lookAt",float3{2,1,3});
    return p;
}
Json authored() {
    return Json::parse(R"json({
        "version":1,
        "morphs":[
            {"parameter":"jawOpen","node":"Face","target":"face-0",
             "halfLife":0.2,"blend":"override"},
            {"parameter":"blinkLeft","node":"Face","target":"face-1",
             "blend":"additive"}
        ],
        "rig":[{"bone":"Eye.L","op":"aim","target":"lookAt",
                "axis":[0,0,1],"weightParameter":"gazeWeight"}]
    })json");
}
void testCompiledRig() {
    auto model=modelWithFace();
    auto params=controls();
    const auto rig=FacialControlRig::compile(model,authored(),params);
    AIVERSE_TEST_CHECK(rig.bindings().size()==2 && rig.hasIk());
    Pose pose;bindPose(model,pose);
    FacialControlState state;
    std::string error;
    AIVERSE_TEST_CHECK(rig.apply(model,params,pose,state,0.016f,1.f,&error));
    AIVERSE_TEST_CHECK(near(pose[0].w[0],.8f));
    AIVERSE_TEST_CHECK(near(pose[0].w[1],.25f));
    AIVERSE_TEST_CHECK(state.initialized && state.smoothed.size()==2);
    AIVERSE_TEST_CHECK(pose[1].r.w<.999f); // eye Aim driven by existing rig
    params.values.at("jawOpen")=0.f;
    bindPose(model,pose);
    AIVERSE_TEST_CHECK(rig.apply(model,params,pose,state,.2f,1.f,&error));
    AIVERSE_TEST_CHECK(near(pose[0].w[0],.4f)); // one half-life
    auto savedPose=pose;
    auto savedState=state.smoothed;
    params.values.at("jawOpen")=std::numeric_limits<float>::quiet_NaN();
    AIVERSE_TEST_CHECK(!rig.apply(model,params,pose,state,.2f,1.f,&error));
    AIVERSE_TEST_CHECK(!error.empty());
    AIVERSE_TEST_CHECK(near(pose[0].w[0],savedPose[0].w[0]));
    AIVERSE_TEST_CHECK(state.smoothed==savedState);
    params.values.at("jawOpen")=1.f;
    params.vectors.erase("lookAt");
    AIVERSE_TEST_CHECK(!rig.apply(model,params,pose,state,.1f,1.f,&error));
    AIVERSE_TEST_CHECK(near(pose[0].w[0],savedPose[0].w[0]));
    params.vectors.emplace("lookAt",float3{2,1,3});
    AIVERSE_TEST_CHECK(!rig.apply(model,params,pose,state,-1.f,1.f,&error));
    AIVERSE_TEST_CHECK(near(pose[0].w[0],savedPose[0].w[0]));
    // Re-import into a previously used pointer invalidates the compiled
    // semantic channel map even when the node graph has the same shape.
    model.meshes[0].morphTargetNames[0]="renamed";
    AIVERSE_TEST_CHECK(!rig.matches(model));
    AIVERSE_TEST_CHECK(!rig.apply(model,params,pose,state,.1f,1.f,&error));
    model.meshes[0].morphTargetNames[0]="face-0";
    model.nodes[1].t.y=2.f; // same pointer, changed bind hierarchy
    AIVERSE_TEST_CHECK(!rig.matches(model));
    model.nodes[1].t.y=1.f;
    model.meshes[0].primitives[0].morphTargets=2; // same names, changed GPU cap
    AIVERSE_TEST_CHECK(!rig.matches(model));
}
void testBadDefinitions() {
    auto m=modelWithFace();
    const auto params=controls();
    const auto reject=[&](const std::string& text) {
        bool threw=false;
        try {(void)FacialControlRig::compile(m,Json::parse(text),params);}
        catch(const std::exception&){threw=true;}
        AIVERSE_TEST_CHECK(threw);
    };
    reject(R"({"version":2,"morphs":[]})");
    reject(R"({"version":1,"morphs":[{"parameter":"missing","node":"Face","target":"face-0"}]})");
    reject(R"({"version":1,"morphs":[{"parameter":"jawOpen","node":"Unseen","target":"face-0"}]})");
    reject(R"({"version":1,"morphs":[{"parameter":"jawOpen","node":"Face","target":"absent"}]})");
    reject(R"({"version":1,"morphs":[
       {"parameter":"jawOpen","node":"Face","target":"face-0"},
       {"parameter":"blinkLeft","node":"Face","target":"face-0"}]})");
    reject(R"({"version":1,"morphs":[{"parameter":"jawOpen","node":"Face",
       "target":"face-0","halfLife":-1}]})");
    reject(R"({"version":1,"rig":[{"bone":"Unseen","op":"aim","target":"lookAt"}]})");
    reject(R"({"version":1,"morphs":[]})");
    // A named mapping cannot silently guess channels for an unnamed rig.
    m.meshes[0].morphTargetNames.clear();
    bool threw=false;
    try {(void)FacialControlRig::compile(m,authored(),params);}
    catch(const std::exception&){threw=true;}
    AIVERSE_TEST_CHECK(threw);
}
void testManyChannels() {
    auto m=modelWithFace(52);
    AnimationParameters params;
    std::string content=R"({"version":1,"morphs":[)";
    for(int i=0;i<52;++i) {
        const std::string key="control"+std::to_string(i);
        params.values.emplace(key,(i%2)?0.f:1.f);
        if(i)content+=",";
        content+=R"({"parameter":")"+key+
                 R"(","node":"Face","target":"face-)"+
                 std::to_string(i)+R"("})";
    }
    content+="]}";
    auto rig=FacialControlRig::compile(m,Json::parse(content),params);
    AIVERSE_TEST_CHECK(rig.bindings().size()==52);
    Pose pose;bindPose(m,pose);
    FacialControlState state;
    AIVERSE_TEST_CHECK(rig.apply(m,params,pose,state,0.f));
    for(int i=0;i<52;++i)
        AIVERSE_TEST_CHECK(near(pose[0].w[i],(i%2)?0.f:1.f));
}

void testStudioFacialNativePreview() {
    auto model=modelWithFace();
    modelview::NativeFacialPreview preview;
    modelview::NativeFacialPreview::Scalars scalars{
        {"jawOpen",0.65f},{"blinkLeft",0.4f},{"gazeWeight",1.f}};
    modelview::NativeFacialPreview::Vectors vectors{
        {"lookAt",{2.f,1.f,3.f}}};
    std::string error;
    AIVERSE_TEST_CHECK(preview.configure(model,authored(),
                                         scalars,vectors,error));
    AIVERSE_TEST_CHECK(preview.ready());
    render::Animator animator;
    animator.setModel(&model);
    render::Pose pose;
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(preview.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(near(pose[0].w[0],.65f));
    AIVERSE_TEST_CHECK(near(pose[0].w[1],.4f));
    AIVERSE_TEST_CHECK(pose[1].r.w<.999f);
    AIVERSE_TEST_CHECK(!preview.scalar("unknown",.5f));
    AIVERSE_TEST_CHECK(!preview.scalar("jawOpen",2.f));
    AIVERSE_TEST_CHECK(preview.scalar("jawOpen",.9f));
    // A failed Studio edit must not replace the current validated rig.
    const Json bad=Json::parse(R"({"version":1,"morphs":[
        {"parameter":"jawOpen","node":"Face","target":"missing"}]})");
    AIVERSE_TEST_CHECK(!preview.configure(model,bad,scalars,vectors,error));
    AIVERSE_TEST_CHECK(preview.ready());
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(preview.apply(model,pose,.25f,error));
    AIVERSE_TEST_CHECK(pose[0].w[0]>.65f);
    // True model reimport invalidates the borrowed compiled rig.
    model.meshes[0].morphTargetNames[0]="renamed";
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(!preview.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(!error.empty());
}

void testEcsComposition() {
    auto model=modelWithFace();
    auto faceParams=controls();
    auto compiled=std::make_shared<const FacialControlRig>(
        FacialControlRig::compile(model,authored(),faceParams));
    auto graph=AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));
    World presentation;
    Entity e=presentation.create();
    auto& asset=presentation.add<AnimationAssetComponent>(e);
    asset.model=&model;asset.graph=std::move(graph);
    presentation.add<AnimationPlayerComponent>(e);
    presentation.add<AnimationPoseComponent>(e);
    auto& facial=presentation.add<AnimationFacialControlComponent>(e);
    facial.rig=std::move(compiled);
    facial.controls=faceParams;
    presentation.add<AnimationDiagnosticsComponent>(e);
    stepAnimationPresentation(presentation,.016f);
    const auto& pose=presentation.get<AnimationPoseComponent>(e);
    AIVERSE_TEST_CHECK(pose.valid && near(pose.local[0].w[0],.8f));
    AIVERSE_TEST_CHECK(near(pose.local[0].w[1],.25f));
    AIVERSE_TEST_CHECK(presentation.get<AnimationDiagnosticsComponent>(e).healthy);
    AIVERSE_TEST_CHECK(presentation.get<AnimationDiagnosticsComponent>(e).evaluations==1);
    presentation.get<AnimationFacialControlComponent>(e).controls.values["jawOpen"]=
        std::numeric_limits<float>::infinity();
    stepAnimationPresentation(presentation,.016f);
    const auto& status=presentation.get<AnimationDiagnosticsComponent>(e);
    AIVERSE_TEST_CHECK(!status.healthy && !status.error.empty());
    AIVERSE_TEST_CHECK(pose.valid); // safe Graph fallback; face pose unchanged
    auto& current=presentation.get<AnimationFacialControlComponent>(e);
    current.controls=faceParams;
    MotionWeaveScheduler scheduler(1);
    const auto serial=scheduler.update(presentation,.016f);
    AIVERSE_TEST_CHECK(serial.serialEntities==1 && serial.parallelEntities==0);
    // Without native IK, the SAME actor/Graph becomes worker-eligible.
    auto onlyMorph=authored();
    onlyMorph.erase("rig");
    auto& morphOnly=presentation.get<AnimationFacialControlComponent>(e);
    morphOnly.rig=std::make_shared<const FacialControlRig>(
        FacialControlRig::compile(model,onlyMorph,faceParams));
    morphOnly.state.reset();
    const auto parallel=scheduler.update(presentation,.016f);
    AIVERSE_TEST_CHECK(parallel.serialEntities==0 && parallel.parallelEntities==1);
}
} // namespace
int main() {
    testCompiledRig();
    testBadDefinitions();
    testManyChannels();
    testStudioFacialNativePreview();
    testEcsComposition();
}
