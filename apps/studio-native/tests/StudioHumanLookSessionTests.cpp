#include "apps/studio-native/StudioSession.h"
#include "tests/AlwaysCheck.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "tools/modeling/Modeling.h"
#include <thread>
using namespace aiverse;
namespace fs=std::filesystem;
int main() {
 const auto root=fs::temp_directory_path()/("studio-human-look-"+std::to_string(getpid()));
 const auto project=root/"templates"/"character";fs::create_directories(project/"content"/"Ada");
 try {
  {std::ofstream f(project/"game.json");f<<R"({"name":"Look test","module":"rts","rules":"rules.json"})";}
  {std::ofstream f(project/"rules.json");f<<R"({"resources":{},"types":{"person":{"name":"Person","health":{"max":1}}}})";}
  {std::ofstream f(project/"content/Ada/model.gltf");f<<"{}";}
  vivid::HumanAppearance look;look.materials["Face"]="skin";look.values["skin.roughness"]=.8f;
  {std::ofstream f(project/"content/Ada/appearance.json");f<<look.toJson().dump();}
  studio::StudioSession session(root.string(),project.string());
  auto call=[&](const char* method,Json params=Json::object()) {
   auto r=Json::object();r.set("id",Json::number(int64_t(1)));r.set("method",Json::string(method));r.set("params",std::move(params));return session.dispatch(r);
  };
  const auto info=call("studio.humanAppearanceInfo");
  AIVERSE_TEST_CHECK(!info.has("error")&&info["result"]["schema"].size()==102);
  studio::StudioAssetCatalog catalog(project);catalog.rebuild();const auto asset=catalog.byPath("Ada/model.gltf");
  AIVERSE_TEST_CHECK(asset.has_value());
  auto params=Json::object();params.set("guid",Json::string(asset->guid));
  const auto imported=call("studio.humanAppearanceFromAsset",params);
  AIVERSE_TEST_CHECK(imported["result"]["look"]==look.toJson());
  params.set("revision",info["result"]["preview"]["revision"]);params.set("look",look.toJson());
  const auto applied=call("studio.humanAppearanceApply",params);
  AIVERSE_TEST_CHECK(!applied.has("error")&&applied["result"]["preview"]["bound"].boolean());
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceApply",params).has("error"));
  auto save=Json::object();save.set("name",Json::string("matte"));save.set("look",look.toJson());
  AIVERSE_TEST_CHECK(!call("studio.humanAppearanceSave",save).has("error"));
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceLoad",save)["result"]["look"]==look.toJson());
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceList")["result"]["looks"].size()==1);
  save.set("name",Json::string("../escape"));AIVERSE_TEST_CHECK(call("studio.humanAppearanceSave",save).has("error"));
  fs::remove(project/"content/Ada/appearance.json");
  fs::create_symlink(project/"human-looks/matte.json",project/"content/Ada/appearance.json");
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceFromAsset",params).has("error"));
  // Real build through native dispatch: async completion, self-contained GLB,
  // no overwrite, no partial output and unknown source GUID rejection.
  auto model=modeling::build(modeling::robotDocument());model.model->clips.clear();
  const auto glb=modeling::exportGlb(*model.model);
  {std::ofstream f(project/"content/person.glb",std::ios::binary);f.write(glb.data(),std::streamsize(glb.size()));}
  catalog.rebuild();auto person=catalog.byPath("person.glb");AIVERSE_TEST_CHECK(person.has_value());
  auto build=Json::object();build.set("guid",Json::string(person->guid));build.set("look",vivid::HumanAppearance{}.toJson());build.set("name",Json::string("assembled"));build.set("textureSize",Json::number(int64_t(256)));build.set("wardrobe",Json::parse(R"({"version":1,"hiddenMeshes":[],"items":[]})"));
  auto preset=Json::parse(R"({"version":1,"hiddenMeshes":[],"items":[{"id":"watch","asset":"person.glb","slot":"wrist_left","enabled":true,"bone":"","hideMeshes":[],"masks":[]}]})");
  {std::ofstream f(project/"content/wardrobe.json");f<<preset.dump();}
  const auto presetResult=call("studio.humanWardrobeFromAsset",build);
  AIVERSE_TEST_CHECK(!presetResult.has("error")&&presetResult["result"]["items"][0].getStr("asset")==person->guid);
  AIVERSE_TEST_CHECK(presetResult["result"]["items"][0]["scale"].size()==3&&presetResult["result"]["items"][0].getStr("finish")=="source");
  AIVERSE_TEST_CHECK(call("studio.humanWardrobeInfo",build)["result"]["meshes"].size()>0);
  AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build)["result"].getBool("running"));
  auto poll=[&](){Json status;for(int n=0;n<500;++n){status=call("studio.humanCharacterBuildStatus")["result"];if(!status.getBool("running"))return status;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return status;};
  auto result=poll();AIVERSE_TEST_CHECK(!result.getBool("running")&&!result.has("error")&&result.has("guid"));
  AIVERSE_TEST_CHECK(fs::file_size(project/"content/Characters/assembled/model.glb")>100);
  AIVERSE_TEST_CHECK(fs::is_regular_file(project/"content/Characters/assembled/source.json"));
  AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build)["result"].getBool("running"));
  AIVERSE_TEST_CHECK(poll().has("error"));
  AIVERSE_TEST_CHECK(!fs::exists(project/"content/Characters/.assembled.pending"));
  const auto initialAnimation=call("studio.humanPreviewAnimation")["result"];AIVERSE_TEST_CHECK(initialAnimation.getBool("playing")&&initialAnimation.getBool("faceIdle")&&initialAnimation.getStr("pose")=="relaxed");
  auto animParams=Json::object();auto anim=initialAnimation;anim.set("pose",Json::string("t_pose"));animParams.set("settings",anim);AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation",animParams)["result"].getStr("pose")=="t_pose");
  anim.set("automaticLod",Json::fromBool(false));animParams.set("settings",anim);AIVERSE_TEST_CHECK(!call("studio.humanPreviewAnimation",animParams)["result"].getBool("automaticLod"));
  anim.set("automaticLod",Json::number(3.));animParams.set("settings",anim);AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation",animParams).has("error"));anim.set("automaticLod",Json::fromBool(false));
  anim.set("speed",Json::number(100.));animParams.set("settings",anim);AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation",animParams).has("error"));AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation")["result"].getStr("pose")=="t_pose");
  anim.set("speed",Json::number(1.));anim.set("expression",Json::string("smile"));animParams.set("settings",anim);AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation",animParams)["result"].getStr("expression")=="smile");
  anim.set("expression",Json::string("wink"));animParams.set("settings",anim);AIVERSE_TEST_CHECK(call("studio.humanPreviewAnimation",animParams).has("error"));anim.set("expression",Json::string("smile"));
  AIVERSE_TEST_CHECK(call("studio.humanAppearanceThumbnails")["result"].isObject());
  {auto save=Json::object();save.set("name",Json::string("pk"));save.set("look",vivid::HumanAppearance{}.toJson());AIVERSE_TEST_CHECK(!call("studio.humanAppearanceSave",save).has("error"));
   auto ex=Json::object();ex.set("look",Json::string("pk"));AIVERSE_TEST_CHECK(!call("studio.humanPackageExport",ex).has("error"));
   AIVERSE_TEST_CHECK(call("studio.humanPackageList")["result"]["packages"].size()==1);
   auto im=Json::object();im.set("name",Json::string("pk"));const auto got=call("studio.humanPackageImport",im);
   AIVERSE_TEST_CHECK(!got.has("error")&&got["result"].getStr("look")=="pk_2");}
  {auto bad=Json::object();bad.set("type",Json::string("9 bad"));bad.set("path",Json::string("Ada/model.gltf"));AIVERSE_TEST_CHECK(call("studio.humanPlaceInGame",bad).has("error"));
   bad.set("type",Json::string("hero"));bad.set("path",Json::string("../escape.glb"));AIVERSE_TEST_CHECK(call("studio.humanPlaceInGame",bad).has("error"));}
  {render::Model fixture;fixture.nodes.resize(6);fixture.nodes[4].name="upperarm_l_100";fixture.nodes[4].parent=0;fixture.nodes[5].name="upperarm_l_200";fixture.nodes[5].parent=0;fixture.nodes[5].t={10,0,0};fixture.meshes.resize(1);fixture.nodes[0].name="root";fixture.nodes[1].name="upperarm_l_23";fixture.nodes[1].parent=0;fixture.nodes[2].name="lowerarm_l_24";fixture.nodes[2].parent=1;fixture.nodes[2].t={1,-1,0};fixture.nodes[3].name="face";fixture.nodes[3].parent=0;fixture.nodes[3].mesh=0;fixture.meshes[0].morphTargetNames={"eyeBlinkLeft","eyeBlinkRight","mouthSmileLeft","eyeLookInLeft","eyeLookOutRight","eyeLookOutLeft"};fixture.finalize();
   auto clip=vivid::humanPreviewClip(fixture,2,true);render::Pose pose;render::bindPose(fixture,pose);render::sampleClip(clip,1.7f,pose);AIVERSE_TEST_CHECK(pose[3].w[0]>.99f&&pose[3].w[1]>.99f);AIVERSE_TEST_CHECK(std::abs(pose[4].r.z-pose[1].r.z)<1e-5f&&std::abs(pose[4].r.w-pose[1].r.w)<1e-5f);AIVERSE_TEST_CHECK(std::abs(pose[5].r.z)<1e-5f);std::vector<render::mat4> world;render::worldMatrices(fixture,pose,world);const auto hand=world[2].transformPoint({});AIVERSE_TEST_CHECK(hand.x>1.4f&&std::abs(hand.y)<.001f);
   render::bindPose(fixture,pose);render::sampleClip(clip,0,pose);AIVERSE_TEST_CHECK(pose[3].w[0]==0);render::Pose end;render::bindPose(fixture,end);render::sampleClip(clip,6,end);AIVERSE_TEST_CHECK(std::abs(end[3].w[2]-pose[3].w[2])<1e-5f);
   auto off=vivid::humanPreviewClip(fixture,0,false);render::bindPose(fixture,pose);render::sampleClip(off,1.7f,pose);AIVERSE_TEST_CHECK(pose[3].w[0]==0);render::worldMatrices(fixture,pose,world);const auto relaxed=world[2].transformPoint({});AIVERSE_TEST_CHECK(relaxed.y< -1.3f&&relaxed.x<.3f);AIVERSE_TEST_CHECK(fixture.clips.empty()&&fixture.nodes[2].t.x==1);
   render::bindPose(fixture,pose);render::sampleClip(clip,1.5f,pose);
   AIVERSE_TEST_CHECK(pose[3].w[3]>.11f&&pose[3].w[3]==pose[3].w[4]&&pose[3].w[5]==0);
   AIVERSE_TEST_CHECK(vivid::humanIdleBlink(1.65f)<vivid::humanIdleBlink(1.75f));
   fixture.clips={vivid::humanPreviewClip(fixture,0,true),clip};
   render::Animator animator;animator.setModel(&fixture);vivid::playHumanPreview(animator,0,true);animator.update(1.7f);
   render::Pose before;animator.evaluate(before);vivid::playHumanPreview(animator,1,true);animator.evaluate(pose);
   AIVERSE_TEST_CHECK(std::abs(animator.time(0)-1.7f)<1e-5f&&std::abs(pose[1].r.z-before[1].r.z)<1e-5f&&pose[3].w[0]>.99f);
   animator.update(.325f);animator.evaluate(pose);render::worldMatrices(fixture,pose,world);
   const auto halfway=world[2].transformPoint({});AIVERSE_TEST_CHECK(halfway.y<-.2f&&halfway.y> -1.3f);
   animator.update(.4f);animator.evaluate(pose);render::worldMatrices(fixture,pose,world);AIVERSE_TEST_CHECK(std::abs(world[2].transformPoint({}).y)<.001f);
   vivid::playHumanPreview(animator,0,false);animator.evaluate(pose);render::worldMatrices(fixture,pose,world);AIVERSE_TEST_CHECK(world[2].transformPoint({}).y< -1.3f);

  }
  const auto lightInfo=call("studio.humanStudioLighting")["result"];AIVERSE_TEST_CHECK(lightInfo["presets"].size()==8);
  auto lightParams=Json::object();auto lights=Json::parse(R"({"preset":"gel","rotation":90,"gain":0.75,"backdrop":true})");lightParams.set("settings",lights);
  AIVERSE_TEST_CHECK(call("studio.humanStudioLighting",lightParams)["result"]["settings"].getStr("preset")=="gel");
  lights.set("preset",Json::string("unknown"));lightParams.set("settings",lights);AIVERSE_TEST_CHECK(call("studio.humanStudioLighting",lightParams).has("error"));
  AIVERSE_TEST_CHECK(call("studio.humanStudioLighting")["result"]["settings"].getStr("preset")=="gel");
  const auto backdrop=vivid::humanStudioCyclorama();AIVERSE_TEST_CHECK(backdrop.vertices().size()==134&&backdrop.indices().size()==396);
  for(size_t i=0;i<backdrop.indices().size();i+=3){const auto& a=backdrop.vertices()[backdrop.indices()[i]];const auto& b=backdrop.vertices()[backdrop.indices()[i+1]];const auto& c=backdrop.vertices()[backdrop.indices()[i+2]];AIVERSE_TEST_CHECK(render::dot(render::cross(b.pos-a.pos,c.pos-a.pos),a.nrm)>0);}
  auto face=Json::parse(R"({"version":1,"meshes":[],"values":{"width":0.1},"landmarks":[]})");face.member("meshes").push(Json::string(model.model->meshes[0].name));
  build.set("face",face);build.set("name",Json::string("face_baked"));
  AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build)["result"].getBool("running"));AIVERSE_TEST_CHECK(!poll().has("error"));
  auto scan=Json::object();scan.set("guid",Json::string(person->guid));scan.set("scanGuid",Json::string(person->guid));scan.set("face",face);scan.set("distance",Json::number(.1));scan.set("strength",Json::number(.5));
  AIVERSE_TEST_CHECK(call("studio.humanFaceFitScan",scan)["result"].getBool("running"));auto fitted=poll();AIVERSE_TEST_CHECK(!fitted.has("error")&&fitted["face"]["landmarks"].size()>=4);
  auto parts=Json::object();parts.set("guid",Json::string(person->guid));parts.set("headGuid",Json::string(person->guid));parts.set("bone",Json::string(""));parts.set("hiddenMeshes",Json::array());parts.set("name",Json::string("parts_imported"));
  AIVERSE_TEST_CHECK(call("studio.humanHeadBodyImport",parts)["result"].getBool("running"));AIVERSE_TEST_CHECK(!poll().has("error"));
  build.set("guid",Json::string("../../outside"));AIVERSE_TEST_CHECK(call("studio.humanCharacterBuild",build).has("error"));
  // Viewport sculpt brush: needs face meshes; a stroke becomes one face landmark.
  {auto arm=Json::object();arm.set("enabled",Json::fromBool(true));
   auto stroke=Json::object();stroke.set("point",Json::parse("[0.5,0.6,0.9]"));stroke.set("normal",Json::parse("[0,0,1]"));
   auto badBrush=Json::object();badBrush.set("radius",Json::number(5.0));AIVERSE_TEST_CHECK(call("studio.humanSculpt",badBrush).has("error"));
   auto faced=Json::object();faced.set("guid",Json::string(person->guid));faced.set("look",Json::parse(R"({"version":1,"values":{},"materials":{}})"));
   auto shape=Json::parse(R"({"version":1,"meshes":[],"values":{},"landmarks":[]})");shape.member("meshes").push(Json::string(model.model->meshes[0].name));faced.set("face",shape);
   faced.set("revision",call("studio.humanAppearanceInfo")["result"]["preview"]["revision"]);
   AIVERSE_TEST_CHECK(!call("studio.humanAppearanceApply",faced).has("error"));
   const auto armed=call("studio.humanSculpt",arm);AIVERSE_TEST_CHECK(!armed.has("error")&&armed["result"]["armed"].boolean());
   const auto stroked=call("studio.humanSculptStroke",stroke);
   AIVERSE_TEST_CHECK(!stroked.has("error")&&stroked["result"].getInt("strokes")==1);
   AIVERSE_TEST_CHECK(call("studio.humanAppearanceInfo")["result"]["face"]["landmarks"].size()==1);
   AIVERSE_TEST_CHECK(call("studio.humanSculptUndo")["result"].getInt("strokes")==0);
   AIVERSE_TEST_CHECK(call("studio.humanSculptUndo").has("error"));
   stroke.set("normal",Json::parse("[0,0]"));AIVERSE_TEST_CHECK(call("studio.humanSculptStroke",stroke).has("error"));
   arm.set("enabled",Json::fromBool(false));AIVERSE_TEST_CHECK(!call("studio.humanSculpt",arm)["result"]["armed"].boolean());}
  // Digital Human Creator window methods (studio.humanCreator*).
  const auto creator=call("studio.humanCreatorInfo");
  AIVERSE_TEST_CHECK(!creator.has("error")&&creator["result"]["schema"].size()==10&&creator["result"]["catalog"]["nodes"].size()>0);
  auto set=Json::object();set.set("key",Json::string("skin.melanin"));set.set("value",Json::number(.66));
  AIVERSE_TEST_CHECK(!call("studio.humanCreatorSet",set).has("error"));
  set.set("value",Json::number(9.0));AIVERSE_TEST_CHECK(call("studio.humanCreatorSet",set).has("error"));
  auto preview=Json::object();preview.set("enabled",Json::fromBool(true));
  const auto bound=call("studio.humanCreatorPreview",preview);
  AIVERSE_TEST_CHECK(!bound.has("error")&&bound["result"]["preview"]["creator"].boolean()&&bound["result"]["preview"]["bound"].boolean());
  const auto before=bound["result"]["preview"]["revision"].integer();
  set.set("value",Json::number(.5));
  AIVERSE_TEST_CHECK(call("studio.humanCreatorSet",set)["result"]["preview"]["revision"].integer()>before);
  auto undo=Json::object();undo.set("target",Json::string("dna"));AIVERSE_TEST_CHECK(!call("studio.humanCreatorUndo",undo).has("error"));
  auto named=Json::object();named.set("name",Json::string("ada_creator"));
  AIVERSE_TEST_CHECK(!call("studio.humanCreatorSave",named).has("error"));
  AIVERSE_TEST_CHECK(call("studio.humanCreatorList")["result"]["humans"].size()==1);
  AIVERSE_TEST_CHECK(!call("studio.humanCreatorNew").has("error"));
  AIVERSE_TEST_CHECK(!call("studio.humanCreatorLoad",named).has("error"));
  named.set("name",Json::string("../escape"));AIVERSE_TEST_CHECK(call("studio.humanCreatorSave",named).has("error"));
  preview.set("enabled",Json::fromBool(false));AIVERSE_TEST_CHECK(!call("studio.humanCreatorPreview",preview)["result"]["preview"]["bound"].boolean());
  fs::remove_all(root);
 } catch(...) {fs::remove_all(root);throw;}
}
