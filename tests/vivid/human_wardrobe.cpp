#include "engine/character/vivid/HumanWardrobe.h"
#include "tools/modeling/Modeling.h"
#include "engine/render/Animation.h"
#include "engine/character/vivid/HumanPreviewAnimation.h"
#include "tools/branding/BrandImage.h"
#include <iostream>
using namespace aiverse;
using namespace aiverse::render;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 Model idleRig;for(const auto* name:{"root","spine_01","clavicle_l","lowerarm_l","hand_l","foot_l"}){Node n;n.name=name;idleRig.nodes.push_back(n);}idleRig.finalize();
 const auto idle=vivid::humanPreviewClip(idleRig,0,true);bool movingTorso=false,movingHand=false;
 for(const auto& channel:idle.channels){check(idleRig.nodes[size_t(channel.node)].name!="foot_l","idle moved planted foot");
  if(channel.path!=AnimChannel::Rotation)continue;
  float motion=0;for(size_t i=0;i<channel.values.size();++i)motion=std::max(motion,std::abs(channel.values[i]-channel.values[i%4]));
  if(idleRig.nodes[size_t(channel.node)].name=="spine_01")movingTorso=motion>.005f;
  if(idleRig.nodes[size_t(channel.node)].name=="hand_l")movingHand=motion>.005f;
  for(int i=0;i<4;++i)check(std::abs(channel.values[size_t(i)]-channel.values[channel.values.size()-4+size_t(i)])<1e-5f,"body idle loop discontinuity");
 }check(movingTorso&&movingHand,"body idle channels are static");
 check(vivid::humanBoneSemantic("wardrobe/LeftForeArm_12")=="lowerarm_l","common body rig alias missing");

 Model standing;Node pelvis;pelvis.name="pelvis";standing.nodes.push_back(pelvis);
 for(int side=0;side<2;++side){const std::string suffix=side?"r":"l";const int thigh=int(standing.nodes.size());Node a;a.name="thigh_"+suffix;a.parent=0;a.t={side?-.1f:.1f,-.1f,0};standing.nodes.push_back(a);Node b;b.name="calf_"+suffix;b.parent=thigh;b.t={0,-.5f,.05f};standing.nodes.push_back(b);Node c;c.name="foot_"+suffix;c.parent=thigh+1;c.t={0,-.5f,-.05f};standing.nodes.push_back(c);}standing.finalize();standing.boundsMin={-.2f,-1.1f,-.1f};standing.boundsMax={.2f,.8f,.1f};
 Node modularPelvis;modularPelvis.name="pelvis_100";standing.nodes.push_back(modularPelvis);Node distantPelvis;distantPelvis.name="pelvis_101";distantPelvis.t={10,0,0};standing.nodes.push_back(distantPelvis);standing.finalize();standing.boundsMin={-.2f,-1.1f,-.1f};standing.boundsMax={.2f,.8f,.1f};
 const auto planted=vivid::humanPreviewClip(standing,0,false);bool pelvisMoves=false;
 for(int frame:{0,30,60,90,120}){Pose sample;bindPose(standing,sample);for(const auto& c:planted.channels){const size_t k=size_t(frame)*c.comps;if(c.path==AnimChannel::Translation){sample[size_t(c.node)].t={c.values[k],c.values[k+1],c.values[k+2]};if(c.node==0)pelvisMoves|=std::abs(c.values[k])>.001f;}else if(c.path==AnimChannel::Rotation)sample[size_t(c.node)].r={c.values[k],c.values[k+1],c.values[k+2],c.values[k+3]};}std::vector<mat4> world;worldMatrices(standing,sample,world);for(int foot:{3,6})check(length(world[size_t(foot)].transformPoint({})-standing.bindWorld[size_t(foot)].transformPoint({}))<.001f,"weight shift moved planted ankle");}check(pelvisMoves,"weight shift omitted complete leg chains");
 const AnimChannel* bodyShift=nullptr;const AnimChannel* headShift=nullptr;for(const auto& c:planted.channels)if(c.path==AnimChannel::Translation){if(c.node==0)bodyShift=&c;if(c.node==7)headShift=&c;check(c.node!=8,"unrelated modular pelvis borrowed motion");}check(bodyShift&&headShift&&bodyShift->values==headShift->values,"modular head pelvis detached from body shift");
 auto doc=modeling::emptyDocument();doc.set("nodes",Json::parse(R"([{"id":"root"},{"id":"body","geometry":{"kind":"box"},"skin":"skin","bindJoint":"root"},{"id":"shirt","geometry":{"kind":"box"}}])"));doc.set("skins",Json::parse(R"([{"id":"skin","joints":["root"]}])"));
 auto built=modeling::build(doc);built.model->materials[0].name="Skin";auto& mesh=built.model->meshes[0];mesh.morphTargetNames={"jawOpen"};for(auto& p:mesh.primitives){p.morphTargets=1;p.morphDeltas.resize(size_t(p.vertexCount)*6,.01f);}
 // Authored alternatives have no scene owner; assembly must retain/remap them.
 const int faceLod=int(built.model->meshes.size());
 built.model->meshes.push_back(built.model->meshes[0]);
 built.model->meshes.back().name="body_lod1";
 built.model->meshes[0].lods.push_back({faceLod,200,{0}});
 auto one=modeling::build(doc),shared=modeling::build(doc);
 shared.model->meshes[0].primitives.push_back(shared.model->meshes[0].primitives[0]);
 vivid::HumanAppearance proportions;proportions.values["body.height"]=1.2f;
 vivid::bakeHumanBody(*one.model,proportions);vivid::bakeHumanBody(*shared.model,proportions);
 for(size_t i=0;i<one.model->vertices.size();++i)check(length(one.model->vertices[i].pos-shared.model->vertices[i].pos)<1e-5f,"shared vertex was body-baked more than once");
 auto quantized=modeling::build(doc);for(auto& v:quantized.model->vertices)if(v.weights[0]==255)v.weights[0]=254;
 vivid::bakeHumanBody(*quantized.model,proportions);
 for(size_t i=0;i<one.model->vertices.size();++i)check(length(one.model->vertices[i].pos-quantized.model->vertices[i].pos)<1e-5f,"body bake did not normalize packed skin weights like GPU");
 const auto shirtName=built.model->meshes[size_t(built.model->nodes[size_t(built.model->findNode("shirt"))].mesh)].name;
 auto accessory=modeling::build(modeling::robotDocument());accessory.model->clips.clear();
 const int watchLod=int(accessory.model->meshes.size());
 accessory.model->meshes.push_back(accessory.model->meshes[0]);accessory.model->meshes.back().name="watch_lod1";
 accessory.model->meshes[0].lods.push_back({watchLod,180,{}});
 std::map<std::string,std::string> files;files["base.glb"]=modeling::exportGlb(*built.model);files["watch.glb"]=modeling::exportGlb(*accessory.model);
 branding::Image mask;mask.width=2;mask.height=2;mask.rgba={0,0,0,255,255,255,255,255,0,0,0,255,255,255,255,255};files["mask.png"]=branding::encodePng(mask);
 auto reader=[&](const std::string& key,std::string& bytes){auto i=files.find(key);if(i==files.end())return false;bytes=i->second;return true;};
 auto recipe=Json::parse(R"({"version":1,"hiddenMeshes":[],"items":[{"id":"watch","asset":"watch.glb","slot":"wrist_left","enabled":true,"bone":"root","translation":[0,1,0],"scale":[0.1,0.1,0.1],"hideMeshes":[],"masks":[{"material":"Skin","texture":"mask.png"}]}]})");recipe.member("hiddenMeshes").push(Json::string(shirtName));
 vivid::HumanAppearance look;look.materials["Skin"]="skin";look.values["body.height"]=1.2f;
 auto result=vivid::assembleHumanCharacter("base.glb",reader,recipe,look,16);
 check(!result->meshes[0].lods.empty(),"body LOD lost on prune");
 check(result->meshes.at(size_t(result->meshes[0].lods[0].mesh)).name=="body_lod1","LOD index not remapped");
 check(result->meshes.at(size_t(result->meshes[0].lods[0].mesh)).morphTargetNames==std::vector<std::string>{"jawOpen"},"LOD morph names lost");
 check(result->findNode("wardrobe/watch")>=0,"accessory missing");check(result->nodes[size_t(result->findNode("wardrobe/watch"))].parent==result->findNode("root"),"accessory socket lost");
 bool clothingLod=false;for(const auto& m:result->meshes)if(m.name.starts_with("wardrobe/watch/")&&!m.lods.empty()){clothingLod=true;check(result->meshes.at(size_t(m.lods[0].mesh)).name=="wardrobe/watch/watch_lod1","appended clothing LOD index lost");}check(clothingLod,"clothing LOD chain dropped");
 for(const auto& m:result->meshes)check(m.name!=shirtName,"hidden clothing retained");
 const auto& material=result->materials[0];check(!material.shaderGraph&&material.baseTex>=0&&material.alpha==AlphaMode::Mask,"appearance or mask not baked");const auto& image=result->images[size_t(material.baseTex)];check(image.rgba[3]==0&&image.rgba[(image.width-1)*4+3]==255,"mask coverage incorrect");
 result->materials[0].shading=ShadingModel::Eye;
 result->materials[0].eyeOptics={.488f,.504f,.14f,.012f};result->materials[0].humanProfile={1.376f,.7f};
 auto bytes=modeling::exportGlb(*result);auto roundtrip=loadModel("result.glb",[&](const std::string&,std::string& out){out=bytes;return true;});check(roundtrip&&roundtrip->skins.size()==1,"assembled skin lost");check(roundtrip->materials[0].shading==ShadingModel::Eye&&std::abs(roundtrip->materials[0].eyeOptics.w-.012f)<1e-6f&&std::abs(roundtrip->materials[0].humanProfile.x-1.376f)<1e-6f,"layered eye optics lost on assembly export");check(roundtrip->meshes[0].morphTargetNames==std::vector<std::string>{"jawOpen"},"assembled facial names lost");
 files["optics.glb"]=bytes;
 auto reassembled=vivid::assembleHumanCharacter("optics.glb",reader,Json::parse(R"({"version":1,"items":[],"hiddenMeshes":[]})"),vivid::HumanAppearance{},16);
 check(reassembled->materials[0].shading==ShadingModel::Eye&&reassembled->materials[0].eyeOptics.z>.1f,"assembly rejected or flattened persisted eye optics");
 auto invalid=recipe;invalid.member("items").push(recipe["items"][0]);bool rejected=false;try{vivid::validateHumanWardrobe(invalid);}catch(...){rejected=true;}check(rejected,"duplicate ID accepted");
 {auto tinted=recipe;tinted.member("items").at(0).set("tint",Json::parse("[0.2,0.4,1.5]"));vivid::validateHumanWardrobe(tinted);
  tinted.member("items").at(0).set("tint",Json::parse("[0.2,9,1]"));bool badTint=false;try{vivid::validateHumanWardrobe(tinted);}catch(...){badTint=true;}check(badTint,"out-of-range tint accepted");}
 invalid=recipe;invalid.member("items").at(0).set("bone",Json::string("missing"));rejected=false;try{vivid::assembleHumanCharacter("base.glb",reader,invalid,look,16);}catch(...){rejected=true;}check(rejected,"missing socket silently ignored");
 // Independent imported head keeps its skeleton and morphs, even with different names.
 auto headDoc=doc;auto headModel=modeling::build(headDoc);headModel.model->nodes[0].name="headRoot";
 headModel.model->meshes[0].morphTargetNames={"smile"};for(auto& p:headModel.model->meshes[0].primitives){p.morphTargets=1;p.morphDeltas.resize(size_t(p.vertexCount)*6,.01f);}
 files["head.glb"]=modeling::exportGlb(*headModel.model);
 auto headRecipe=Json::parse(R"({"version":1,"hiddenMeshes":[],"items":[{"id":"head","asset":"head.glb","slot":"head","enabled":true,"bone":"root","independentRig":true,"hideMeshes":[],"masks":[]}]})");
 auto attachBody=loadModel("base.glb",reader);attachBody->nodes[size_t(attachBody->findNode("root"))].t={.3f,.4f,.5f};attachBody->nodes[size_t(attachBody->findNode("root"))].s={2,3,4};attachBody->finalize();files["attachment.glb"]=modeling::exportGlb(*attachBody);
 auto combined=vivid::assembleHumanCharacter("attachment.glb",reader,headRecipe,vivid::HumanAppearance{},16);
 check(combined->skins.size()==2,"independent head skin lost");
 const int hn=combined->findNode("wardrobe/head/headRoot");check(hn>=0,"independent head joint missing");
 check(length(combined->bindWorld[size_t(hn)].transformPoint({0,0,0})-headModel.model->bindWorld[0].transformPoint({0,0,0}))<1e-5f,"head bind frame changed on socket attachment");
 const auto before=combined->bindWorld[size_t(hn)].transformPoint({0,0,0});combined->nodes[size_t(combined->findNode("root"))].t.x+=2;combined->finalize();
 check(std::abs(combined->bindWorld[size_t(hn)].transformPoint({0,0,0}).x-before.x-2)<1e-5f,"head did not follow body socket");
 // Any independent SKM can snap to an arbitrary named socket without
 // remapping its joints to the character or losing skin/morph data.
 auto socketRecipe=headRecipe;
 socketRecipe.set("sockets",Json::parse(R"([{"name":"custom_anchor","bone":"root","translation":[0.2,0.5,-0.1],"rotation":[0,0.3,0],"scale":[1,1,1]}])"));
 auto& item=socketRecipe.member("items").at(0);item.set("bone",Json::string(""));item.set("socket",Json::string("custom_anchor"));item.set("attachmentRule",Json::string("snap"));item.set("translation",Json::parse("[0.1,0,0]"));
 auto snapped=vivid::assembleHumanCharacter("attachment.glb",reader,socketRecipe,vivid::HumanAppearance{},16);
 const int anchor=snapped->findNode("socket/custom_anchor"),child=snapped->findNode("wardrobe/head/headRoot"),root=snapped->findNode("root");
 check(anchor>=0&&child>=0&&snapped->skins.size()==2,"socket or independent SKM missing");
 const auto expected=snapped->bindWorld[size_t(anchor)].transformPoint({.1f,0,0});
 check(length(snapped->bindWorld[size_t(child)].transformPoint({})-expected)<1e-5f,"snap ignored socket-local offset");
 // Rotation/nonuniform scale on the parent must affect the child exactly once.
 Pose animated;bindPose(*snapped,animated);animated[size_t(root)].r=quat::axisAngle({0,1,0},.7f);animated[size_t(root)].s={3,2,4};std::vector<mat4> animatedWorld;worldMatrices(*snapped,animated,animatedWorld);
 const auto delta=animatedWorld[size_t(root)]*snapped->bindWorld[size_t(root)].inverse();
 check(length(animatedWorld[size_t(child)].transformPoint({})-delta.transformPoint(expected))<1e-4f,"SKM did not follow rotated/scaled parent");
 const auto snapBytes=modeling::exportGlb(*snapped);auto snapReload=loadModel("socket.glb",[&](const std::string&,std::string& out){out=snapBytes;return true;});
 check(snapReload&&snapReload->findNode("socket/custom_anchor")>=0&&snapReload->skins.size()==2,"GLB lost socket or skin");
 item.set("attachmentRule",Json::string("keep_world"));item.set("translation",Json::parse("[0,0,0]"));
 auto preserved=vivid::assembleHumanCharacter("attachment.glb",reader,socketRecipe,vivid::HumanAppearance{},16);
 check(length(preserved->bindWorld[size_t(preserved->findNode("wardrobe/head/headRoot"))].transformPoint({})-headModel.model->bindWorld[0].transformPoint({}))<1e-4f,"keep-world socket moved bind frame");
 auto badSocket=socketRecipe;badSocket.member("items").at(0).set("socket",Json::string("absent"));rejected=false;try{vivid::assembleHumanCharacter("attachment.glb",reader,badSocket,vivid::HumanAppearance{},16);}catch(...){rejected=true;}check(rejected,"missing named socket accepted");
 badSocket=socketRecipe;badSocket.member("sockets").push(badSocket["sockets"][0]);rejected=false;try{vivid::validateHumanWardrobe(badSocket);}catch(...){rejected=true;}check(rejected,"duplicate sockets accepted");
 // A bake must match the source base alpha and roughness rather than opaque defaults.
 Model p;p.materials.resize(1);p.materials[0].name="cloth";p.materials[0].baseColor={.2f,.3f,.4f,.25f};p.materials[0].alpha=AlphaMode::Blend;vivid::HumanAppearance fabric;fabric.materials["cloth"]="cloth";vivid::bakeHumanAppearance(p,fabric,16);check(p.images[0].rgba[3]==64&&p.materials[0].roughness>.8f&&!p.materials[0].shaderGraph,"fabric alpha/roughness bake");
 std::cout<<"Wardrobe, socket, masks, appearance and facial roundtrip PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
