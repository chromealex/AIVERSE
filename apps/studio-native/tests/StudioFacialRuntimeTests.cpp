// Stage 185: CPU-only 52-channel semantic/IK runtime contract for Studio.
// This is a real MotionWeave FacialControlRig/FacialPreviewSession test,
// not a replacement for native Metal/Vulkan/GLES rendering acceptance.
#include "aiverselib/animation/FacialPreviewSession.h"
#include "engine/render/Animation.h"
#include "engine/render/Model.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include <string>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;

static Model makeFace() {
    Model model;
    model.nodes.resize(2);
    model.order={0,1};
    model.nodes[0].name="Face";
    model.nodes[0].mesh=0;
    model.nodes[0].children={1};
    model.nodes[1].name="Eye.L";
    model.nodes[1].parent=0;
    model.nodes[1].t={0,1,0};
    model.meshes.resize(1);
    model.meshes[0].primitives.resize(1);
    model.meshes[0].primitives[0].morphTargets=52;
    for(int i=0;i<52;++i)
        model.meshes[0].morphTargetNames.push_back("expression"+std::to_string(i));
    return model;
}
static Json authored() {
    Json doc=Json::object(),morphs=Json::array(),rig=Json::array();
    doc.set("version",Json::number(int64_t(1)));
    for(int i=0;i<52;++i) {
        Json item=Json::object();
        item.set("parameter",Json::string("expression"+std::to_string(i)));
        item.set("node",Json::string("Face"));
        item.set("target",Json::string("expression"+std::to_string(i)));
        morphs.push(std::move(item));
    }
    Json eye=Json::object(),axis=Json::array();
    axis.push(Json::number(int64_t(0)));
    axis.push(Json::number(int64_t(0)));
    axis.push(Json::number(int64_t(1)));
    eye.set("bone",Json::string("Eye.L"));
    eye.set("op",Json::string("aim"));
    eye.set("target",Json::string("lookAt"));
    eye.set("weightParameter",Json::string("gazeWeight"));
    eye.set("axis",std::move(axis));
    rig.push(std::move(eye));
    doc.set("morphs",std::move(morphs));
    doc.set("rig",std::move(rig));
    return doc;
}

static std::string gltf52(int channelCount) {
    // Same authored POSITION delta accessor reused per target; intentionally
    // simple but it exercises the REAL glTF 52/65-channel import contracts.
    std::string doc=R"({"asset":{"version":"2.0"},
      "buffers":[{"uri":"face.bin","byteLength":72}],
      "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                     {"buffer":0,"byteOffset":36,"byteLength":36}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                   {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"}],
      "meshes":[{"name":"Face","primitives":[{"attributes":{"POSITION":0},
      "targets":[)";
    for(int i=0;i<channelCount;++i) {
        if(i)doc+=",";
        doc+=R"({"POSITION":1})";
    }
    doc+=R"(]}],"extras":{"targetNames":[)";
    for(int i=0;i<channelCount;++i) {
        if(i)doc+=",";
        doc+="\"expression"+std::to_string(i)+"\"";
    }
    doc+=R"(]}}],
      "nodes":[{"name":"Face","mesh":0}],
      "scenes":[{"nodes":[0]}],"scene":0})";
    return doc;
}
static void testGltfImport52() {
    std::string bytes(72,'\0');
    const float positions[9]={0,0,0,1,0,0,0,1,0};
    std::memcpy(bytes.data(),positions,sizeof positions);
    const FileReader read=[bytes](const std::string& key,std::string& out) {
        if(key!="face.bin")return false;
        out=bytes;
        return true;
    };
    std::vector<std::string> warnings;
    auto model=loadGltf("face.gltf",gltf52(52),read,&warnings);
    AIVERSE_TEST_CHECK(bool(model));
    AIVERSE_TEST_CHECK(model->nodes.size()==1);
    AIVERSE_TEST_CHECK(model->meshes[0].morphTargetNames.size()==52);
    AIVERSE_TEST_CHECK(model->meshes[0].morphTargetNames[51]=="expression51");
    AIVERSE_TEST_CHECK(model->meshes[0].primitives[0].morphTargets==52);
    AIVERSE_TEST_CHECK(model->meshes[0].primitives[0].morphDeltas.size()
                       ==size_t(52*3*6));
    const Json document=Json::parse(R"({"version":1,"morphs":[
      {"parameter":"expression51","node":"Face","target":"expression51"}
    ],"rig":[]})");
    FacialPreviewSession session;
    FacialPreviewSession::Scalars scalars{{"expression51",0.75f}};
    std::string error;
    AIVERSE_TEST_CHECK(session.configure(*model,document,scalars,{},error));
    Animator animator;animator.setModel(model.get());
    Pose pose;animator.evaluate(pose);
    AIVERSE_TEST_CHECK(session.apply(*model,pose,.016f,error));
    AIVERSE_TEST_CHECK(std::abs(pose[0].w[51]-.75f)<.001f);

    // 65 is rejected at import, never silently truncated to 64.
    bool rejected=false;
    try {(void)loadGltf("face.gltf",gltf52(65),read,&warnings);}
    catch(const std::exception&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}

static void testAuthoredSemanticLod() {
    // Two real glTF meshes: source uses 52 ordered names, visual LOD only
    // carries expression51. The renderer must not read source index 0.
    std::string bytes(72,'\0');
    const float positions[9]={0,0,0,1,0,0,0,1,0};
    std::memcpy(bytes.data(),positions,sizeof positions);
    const FileReader read=[bytes](const std::string& path,std::string& out) {
        if(path!="face.bin")return false;
        out=bytes;
        return true;
    };
    Json doc=Json::parse(gltf52(52));
    Json coarse=Json::parse(R"({"name":"FaceLOD","primitives":[
        {"attributes":{"POSITION":0},"targets":[{"POSITION":1}]}
      ],"extras":{"targetNames":["expression51"]}})");
    doc.member("meshes").push(std::move(coarse));
    Json level=Json::object(),levels=Json::array();
    level.set("mesh",Json::number(int64_t(1)));
    level.set("maxPixels",Json::number(int64_t(96)));
    levels.push(std::move(level));
    doc.member("meshes").at(0).member("extras").set("aiverseLods",std::move(levels));
    auto model=loadGltf("face.gltf",doc.dump(-1),read,nullptr);
    AIVERSE_TEST_CHECK(model && model->meshes.size()==2);
    AIVERSE_TEST_CHECK(model->meshes[0].lods.size()==1);
    AIVERSE_TEST_CHECK(model->meshes[0].lods[0].morphSourceForTarget.size()==1);
    AIVERSE_TEST_CHECK(model->meshes[0].lods[0].morphSourceForTarget[0]==51);
    AIVERSE_TEST_CHECK(model->meshes[1].morphTargetNames.size()==1);
    // An expression present only on the coarse mesh is a hard import error.
    doc.member("meshes").at(1).member("extras").member("targetNames")
       .at(0)=Json::string("inventedChannel");
    bool rejected=false;
    try {(void)loadGltf("face.gltf",doc.dump(-1),read,nullptr);}
    catch(const std::exception&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}

int main() {
    testGltfImport52();
    testAuthoredSemanticLod();
    static_assert(kMaxMorphTargets==64,
                  "Studio native Renderer3D must share 64-channel MotionWeave ABI");
    auto model=makeFace();
    auto document=authored();
    FacialPreviewSession session;
    FacialPreviewSession::Scalars scalars;
    FacialPreviewSession::Vectors vectors{{"lookAt",{2.f,1.f,3.f}}};
    for(int i=0;i<52;++i)
        scalars.emplace("expression"+std::to_string(i),i%2?0.f:1.f);
    scalars.emplace("gazeWeight",1.f);
    std::string error;
    AIVERSE_TEST_CHECK(session.configure(model,document,scalars,vectors,error));
    AIVERSE_TEST_CHECK(session.ready());
    Animator animator;animator.setModel(&model); // exactly one parent pose owner
    Pose pose;animator.evaluate(pose);
    AIVERSE_TEST_CHECK(session.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(pose.size()==2);
    for(int i=0;i<52;++i)
        AIVERSE_TEST_CHECK(std::abs(pose[0].w[i]-(i%2?0.f:1.f))<.001f);
    AIVERSE_TEST_CHECK(pose[1].r.w<.999f); // existing eye Aim IK
    AIVERSE_TEST_CHECK(!session.scalar("absent",.5f));
    AIVERSE_TEST_CHECK(!session.scalar("expression0",2.f));
    AIVERSE_TEST_CHECK(session.scalar("expression0",.8f));
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(session.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(pose[0].w[0]>.79f && pose[0].w[0]<.99f);

    // Invalid edits must preserve last valid compiled program and state.
    Json invalid=authored();
    invalid.member("morphs").at(0).set(
        "target",Json::string("nonexistent"));
    AIVERSE_TEST_CHECK(!session.configure(model,invalid,scalars,vectors,error));
    AIVERSE_TEST_CHECK(session.ready());
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(session.apply(model,pose,.016f,error));

    // In-place importer generation mutation invalidates compiled semantic rig.
    model.meshes[0].morphTargetNames[0]="renamed";
    animator.evaluate(pose);
    AIVERSE_TEST_CHECK(!session.apply(model,pose,.016f,error));
    AIVERSE_TEST_CHECK(!error.empty());
}
