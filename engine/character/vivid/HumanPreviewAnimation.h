#pragma once
// Generate presentation clips consumed by the existing Animator/MotionWeave.
#include "engine/render/Animation.h"
#include "engine/data/Json.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>
#include <cctype>
#include <stdexcept>
namespace aiverse::vivid {
inline constexpr std::array<const char*,4> humanPreviewPoses={"relaxed","a_pose","t_pose","portrait"};
// Test expressions for checking how a sculpted/blended face deforms. Each is
// a set of ARKit-named morph weights layered over the clip pose; "talk"
// cycles visemes. Models without those morph targets are left unchanged.
inline constexpr std::array<const char*,8> humanPreviewExpressions={"idle","smile","laugh","surprise","anger","sad","disgust","talk"};
inline bool humanPreviewExpressionKnown(const std::string& id){for(const char* e:humanPreviewExpressions)if(id==e)return true;return false;}
struct HumanPreviewAnimation {
 std::string pose="relaxed";bool playing=true,faceIdle=true;float speed=1;bool automaticLod=true;std::string expression="idle";std::string clip; // non-empty: play this model clip instead of the preview pose
 int poseIndex()const{for(size_t i=0;i<humanPreviewPoses.size();++i)if(pose==humanPreviewPoses[i])return int(i);throw std::invalid_argument("Unknown character pose");}
 Json toJson()const{auto j=Json::object();j.set("automaticLod",Json::fromBool(automaticLod));j.set("pose",Json::string(pose));j.set("playing",Json::fromBool(playing));j.set("faceIdle",Json::fromBool(faceIdle));j.set("speed",Json::number(double(speed)));j.set("expression",Json::string(expression));j.set("clip",Json::string(clip));return j;}
 static HumanPreviewAnimation fromJson(const Json& j){if(!j.isObject())throw std::invalid_argument("Invalid preview animation");for(const auto& [k,v]:j.members())if(k!="pose"&&k!="playing"&&k!="faceIdle"&&k!="speed"&&k!="automaticLod"&&k!="expression"&&k!="clip")throw std::invalid_argument("Unknown animation field");HumanPreviewAnimation s;if(j.has("automaticLod")){if(j["automaticLod"].type()!=Json::Type::Bool)throw std::invalid_argument("Invalid LOD toggle");s.automaticLod=j.getBool("automaticLod");}s.pose=j.getStr("pose");(void)s.poseIndex();if(j["playing"].type()!=Json::Type::Bool||j["faceIdle"].type()!=Json::Type::Bool)throw std::invalid_argument("Invalid animation toggle");s.playing=j.getBool("playing");s.faceIdle=j.getBool("faceIdle");if(!j["speed"].isNumber()||!std::isfinite(j["speed"].toDouble())||j["speed"].toDouble()<.25||j["speed"].toDouble()>2)throw std::invalid_argument("Invalid animation speed");s.speed=float(j["speed"].toDouble());if(j.has("expression")){s.expression=j.getStr("expression");if(!humanPreviewExpressionKnown(s.expression))throw std::invalid_argument("Unknown preview expression");}if(j.has("clip")){s.clip=j.getStr("clip");if(s.clip.size()>128)throw std::invalid_argument("Clip name too long");}return s;}
};
inline std::string humanBoneSemantic(std::string name){const auto slash=name.rfind('/');if(slash!=std::string::npos)name=name.substr(slash+1);if(name=="spine_01"||name=="spine_02"||name=="spine_03"||name=="neck_01")return name;const auto pos=name.rfind('_');if(pos!=std::string::npos&&pos+1<name.size()&&std::all_of(name.begin()+pos+1,name.end(),[](unsigned char c){return std::isdigit(c);}))name.resize(pos);// Common MakeHuman/Mixamo spellings use the same presentation semantics.
 const std::array<std::pair<const char*,const char*>,12> aliases={{{"Spine","spine_01"},{"Spine1","spine_02"},{"Spine2","spine_03"},{"Neck","neck_01"},{"LeftArm","upperarm_l"},{"RightArm","upperarm_r"},{"LeftForeArm","lowerarm_l"},{"RightForeArm","lowerarm_r"},{"LeftShoulder","clavicle_l"},{"RightShoulder","clavicle_r"},{"LeftHand","hand_l"},{"RightHand","hand_r"}}};
 for(const auto& [source,target]:aliases)if(name==source)return target;
 return name;}
// Smooth closure is faster than reopening; every event has zero endpoint velocity.
inline float humanIdleBlink(float time){
 if(!std::isfinite(time))return 0;
 const float t=std::fmod(std::max(0.f,time),6.f);
 float result=0;
 for(float peak:{1.7f,4.4f}){
  const float d=t-peak;
  const float x=std::clamp(1.f-std::abs(d)/(d<0?.10f:.20f),0.f,1.f);
  result=std::max(result,x*x*(3-2*x));
 }
 return result;
}
// Preserve the face/breath phase when switching pose or toggling facial idle.
// Reuse the host Animator, including its quaternion crossfade and paused behavior.
inline void playHumanPreview(render::Animator& animator,int clip,bool playing){
 const bool previous=animator.clip(0)>=0;
 const float phase=previous?animator.time(0):0;
 render::Animator::Play play;play.restart=true;play.fade=previous&&playing?.65f:0.f;
 play.startTime=phase;
 animator.play(0,clip,play);
}
inline render::AnimClip humanPreviewClip(const render::Model& model,int poseIndex,bool faceIdle){
 using namespace render;if(poseIndex<0||poseIndex>=4)throw std::invalid_argument("Invalid pose index");
 AnimClip clip;clip.name="__vivid_preview_"+std::to_string(poseIndex)+(faceIdle?"_face":"_body");clip.duration=6;
 std::vector<std::string> names;for(const auto& n:model.nodes)names.push_back(humanBoneSemantic(n.name));
 // A modular head may retain shoulder joints but omit the lower arms. Resolve
 // directions from complete chains first, then share only between coincident
 // same-semantic bind frames. Never guess from a differently placed character.
 std::vector<float3> armDirections(model.nodes.size());std::vector<bool> complete(model.nodes.size());
 for(size_t i=0;i<names.size();++i)if(names[i]=="upperarm_l"||names[i]=="upperarm_r"){
  const auto target=names[i]=="upperarm_l"?"lowerarm_l":"lowerarm_r";
  for(size_t k=0;k<names.size();++k)if(names[k]==target){bool descendant=false;for(int parent=model.nodes[k].parent;parent>=0;parent=model.nodes[size_t(parent)].parent)if(parent==int(i)){descendant=true;break;}
   if(descendant){const auto d=model.bindWorld[k].transformPoint({})-model.bindWorld[i].transformPoint({});if(length(d)>1e-6f){armDirections[i]=normalize(d);complete[i]=true;}break;}
  }
 }
 const auto originalDirections=armDirections;
 for(size_t i=0;i<names.size();++i)if(!complete[i]&&(names[i]=="upperarm_l"||names[i]=="upperarm_r")){
  bool found=false,ambiguous=false;float3 direction;
  for(size_t j=0;j<names.size();++j)if(complete[j]&&names[i]==names[j]){
   bool same=true;for(int k=0;k<16;++k)if(std::abs(model.bindWorld[i].m[k]-model.bindWorld[j].m[k])>1e-3f*(1+std::abs(model.bindWorld[j].m[k])))same=false;
   if(!same)continue;
   if(found&&dot(direction,originalDirections[j])<.999f)ambiguous=true;
   direction=originalDirections[j];found=true;
  }
  if(found&&!ambiguous)armDirections[i]=direction;
 }
 for(size_t i=0;i<model.nodes.size();++i){const auto& node=model.nodes[i];const auto& name=names[i];
  bool arm=name=="upperarm_l"||name=="upperarm_r",neck=name=="neck_01",spine=name=="spine_01"||name=="spine_02"||name=="spine_03",shoulder=name=="clavicle_l"||name=="clavicle_r",elbow=name=="lowerarm_l"||name=="lowerarm_r",hand=name=="hand_l"||name=="hand_r";quat armDelta;
  if(arm){const auto from=armDirections[i];if(length(from)<.5f)arm=false;
   else{float sign=from.x<0?-1.f:1.f;auto to=normalize(poseIndex==2?float3{sign,0,0}:poseIndex==1?float3{sign*.7f,-.7f,0}:float3{sign*.18f,-1,0});auto axis=cross(from,to);if(length(axis)>1e-5f)armDelta=quat::axisAngle(axis,std::acos(std::clamp(dot(from,to),-1.f,1.f)));}
  }

  if(arm||neck||spine||shoulder||elbow||hand){AnimChannel c;c.node=int(i);c.path=AnimChannel::Rotation;c.comps=4;const auto parent=node.parent<0?quat{}:rotationOf(model.bindWorld[size_t(node.parent)]);const quat inv{-parent.x,-parent.y,-parent.z,parent.w};
   for(int frame=0;frame<=120;++frame){const float t=frame*.05f,wave=std::sin(t*6.28318530718f/6);const bool resting=poseIndex==0||poseIndex==3;
    // Upper-body breathing and small delayed arm settling. Root/legs stay in
    // bind so feet do not slide; this does not pretend to solve weight-shift IK.
    const float breath=std::sin(t*6.28318530718f/3);
    quat world;
    if(arm)world=quat::axisAngle({0,0,1},resting?.025f*wave:0)*quat::axisAngle({1,0,0},resting?.035f*std::sin(t*6.28318530718f/6+.4f):0)*armDelta;
    else if(neck)world=quat::axisAngle({0,1,0},(poseIndex==3?.14f:0)+.025f*wave)*quat::axisAngle({1,0,0},.012f*breath);
    else if(shoulder)world=quat::axisAngle({0,0,1},resting?.012f*breath:0);
    else if(elbow)world=quat::axisAngle({0,1,0},resting?.025f*std::sin(t*6.28318530718f/6+.8f):0);
    else if(hand)world=quat::axisAngle({1,0,0},resting?.018f*std::sin(t*6.28318530718f/6+1.2f):0);
    else world=quat::axisAngle({1,0,0},(resting?.012f:.004f)*breath)*quat::axisAngle({0,0,1},resting?.008f*wave:0)*quat::axisAngle({0,1,0},resting?.009f*std::sin(t*6.28318530718f/6+.3f):0);
    const auto q=qnormalize(inv*world*parent*node.r);c.times.push_back(t);for(float v:{q.x,q.y,q.z,q.w})c.values.push_back(v);}clip.channels.push_back(std::move(c));}
  if(node.mesh>=0){const auto& mesh=model.meshes.at(size_t(node.mesh));const auto count=std::min(mesh.morphTargetNames.size(),size_t(kMaxMorphTargets));if(!count)continue;AnimChannel c;c.node=int(i);c.path=AnimChannel::Weights;c.comps=uint8_t(count);
   for(int frame=0;frame<=120;++frame){float t=frame*.05f;c.times.push_back(t);for(size_t k=0;k<count;++k){const auto& key=mesh.morphTargetNames[k];float v=node.weights[k];
    if(faceIdle){if(key=="eyeBlinkLeft"||key=="eyeBlinkRight")v=humanIdleBlink(t);else if(key=="mouthSmileLeft"||key=="mouthSmileRight")v=.035f+.025f*(1-std::cos(t*6.28318530718f/6));else if(key=="browInnerUp")v=.015f*(1-std::cos(t*6.28318530718f/6));
     else if(key=="eyeLookInLeft"||key=="eyeLookOutRight")v=.12f*std::pow(std::max(0.f,std::sin(t*6.28318530718f/6)),2.f);
     else if(key=="eyeLookOutLeft"||key=="eyeLookInRight")v=.12f*std::pow(std::max(0.f,-std::sin(t*6.28318530718f/6)),2.f);
     else if(key=="eyeLookUpLeft"||key=="eyeLookUpRight")v=.035f*(1-std::cos(t*6.28318530718f/3));}c.values.push_back(v);}}
   clip.channels.push_back(std::move(c));}
 }
 // Bake a small lateral weight shift with the shared two-bone solver.
 // Only complete direct hip/knee/ankle chains opt in. Feet retain bind targets.
 if(poseIndex==0||poseIndex==3){
  const float height=std::max(.1f,model.boundsMax.y-model.boundsMin.y);
  for(size_t hip=0;hip<names.size();++hip)if(names[hip]=="pelvis"){
   struct Leg{int thigh,knee,foot;};std::vector<Leg> legs;
   for(const auto* side:{"l","r"}){int a=-1,b=-1,c=-1;for(size_t k=0;k<names.size();++k)if(names[k]==std::string("thigh_")+side&&model.nodes[k].parent==int(hip))a=int(k);
    if(a>=0)for(size_t k=0;k<names.size();++k)if(names[k]==std::string("calf_")+side&&model.nodes[k].parent==a)b=int(k);
    if(b>=0)for(size_t k=0;k<names.size();++k)if(names[k]==std::string("foot_")+side&&model.nodes[k].parent==b)c=int(k);
    if(c>=0)legs.push_back({a,b,c});
   }
   if(legs.size()!=2){
    // Modular head skins may share the pelvis bind frame but omit both legs.
    // Follow the same presentation root shift; only the complete rig solves IK.
    bool donor=false;
    for(size_t other=0;other<names.size();++other)if(other!=hip&&names[other]=="pelvis"){
     bool same=true;for(int k=0;k<16;++k)if(std::abs(model.bindWorld[hip].m[k]-model.bindWorld[other].m[k])>1e-3f*(1+std::abs(model.bindWorld[other].m[k])))same=false;
     if(!same)continue;int complete=0;
     for(const auto* side:{"l","r"})for(size_t a=0;a<names.size();++a)if(names[a]==std::string("thigh_")+side&&model.nodes[a].parent==int(other))for(size_t b=0;b<names.size();++b)if(names[b]==std::string("calf_")+side&&model.nodes[b].parent==int(a))for(size_t c=0;c<names.size();++c)if(names[c]==std::string("foot_")+side&&model.nodes[c].parent==int(b))++complete;
     donor|=complete==2;
    }
    if(!donor)continue;legs.clear();
   }
   AnimChannel move;move.node=int(hip);move.path=AnimChannel::Translation;
   std::vector<AnimChannel> rotations;
   for(const auto& leg:legs)for(int node:{leg.thigh,leg.knee,leg.foot}){AnimChannel c;c.node=node;c.path=AnimChannel::Rotation;c.comps=4;rotations.push_back(c);}
   for(int frame=0;frame<=120;++frame){float t=frame*.05f,phase=t*6.28318530718f/6;Pose sample;bindPose(model,sample);std::vector<mat4> world;
    const int parent=model.nodes[hip].parent;const auto parentWorld=parent<0?mat4{}:model.bindWorld[size_t(parent)];
    const auto shift=parentWorld.inverse().transformDir({height*.006f*std::sin(phase),-height*.002f*(1-std::cos(phase*2)),0});sample[hip].t+=shift;
    worldMatrices(model,sample,world);
    for(const auto& leg:legs){const auto target=model.bindWorld[size_t(leg.foot)].transformPoint({});const auto pole=model.bindWorld[size_t(leg.knee)].transformPoint({})+float3{0,0,height};
     solveTwoBoneIK(model,sample,world,leg.thigh,leg.knee,leg.foot,target,pole);
     const auto parentRotation=rotationOf(world[size_t(leg.knee)]);const quat inverse{-parentRotation.x,-parentRotation.y,-parentRotation.z,parentRotation.w};sample[size_t(leg.foot)].r=qnormalize(inverse*rotationOf(model.bindWorld[size_t(leg.foot)]));worldMatrices(model,sample,world);
    }
    move.times.push_back(t);for(float v:{sample[hip].t.x,sample[hip].t.y,sample[hip].t.z})move.values.push_back(v);
    for(auto& c:rotations){c.times.push_back(t);const auto q=sample[size_t(c.node)].r;for(float v:{q.x,q.y,q.z,q.w})c.values.push_back(v);}
   }
   clip.channels.push_back(std::move(move));for(auto& c:rotations)clip.channels.push_back(std::move(c));
  }
 }
 return clip;
}
inline int appendHumanPreviewClips(render::Model& model){const int base=int(model.clips.size());for(int i=0;i<4;++i)for(bool face:{false,true})model.clips.push_back(humanPreviewClip(model,i,face));return base;}

// Layer a test expression over the evaluated pose (weights only, max-blend).
inline int applyHumanPreviewExpression(const render::Model& model,render::Pose& pose,const std::string& expression,float time){
 if(expression=="idle"||expression.empty())return 0;
 struct W{const char* key;float v;};
 std::vector<W> w;
 if(expression=="smile")w={{"mouthSmileLeft",.75f},{"mouthSmileRight",.75f},{"cheekSquintLeft",.4f},{"cheekSquintRight",.4f},{"eyeSquintLeft",.2f},{"eyeSquintRight",.2f}};
 else if(expression=="laugh")w={{"mouthSmileLeft",1},{"mouthSmileRight",1},{"jawOpen",.45f},{"cheekSquintLeft",.7f},{"cheekSquintRight",.7f},{"eyeSquintLeft",.55f},{"eyeSquintRight",.55f},{"mouthUpperUpLeft",.3f},{"mouthUpperUpRight",.3f}};
 else if(expression=="surprise")w={{"browInnerUp",.9f},{"browOuterUpLeft",.8f},{"browOuterUpRight",.8f},{"eyeWideLeft",.8f},{"eyeWideRight",.8f},{"jawOpen",.35f},{"mouthFunnel",.25f}};
 else if(expression=="anger")w={{"browDownLeft",.9f},{"browDownRight",.9f},{"eyeSquintLeft",.45f},{"eyeSquintRight",.45f},{"noseSneerLeft",.5f},{"noseSneerRight",.5f},{"mouthPressLeft",.5f},{"mouthPressRight",.5f},{"jawForward",.2f}};
 else if(expression=="sad")w={{"browInnerUp",.75f},{"mouthFrownLeft",.7f},{"mouthFrownRight",.7f},{"mouthLowerDownLeft",.2f},{"mouthLowerDownRight",.2f},{"eyeLookDownLeft",.3f},{"eyeLookDownRight",.3f},{"mouthShrugLower",.3f}};
 else if(expression=="disgust")w={{"noseSneerLeft",.85f},{"noseSneerRight",.85f},{"mouthUpperUpLeft",.6f},{"mouthUpperUpRight",.6f},{"browDownLeft",.5f},{"browDownRight",.5f},{"cheekSquintLeft",.4f},{"cheekSquintRight",.4f}};
 else if(expression=="talk"){
  // Viseme cycle ~4 syllables per second.
  const float p=time*4.f;const int i=int(std::floor(p))%5;const float a=std::sin((p-std::floor(p))*3.14159265f);
  const W cycle[5][2]={{{"jawOpen",.45f},{"mouthFunnel",.1f}},{{"mouthPucker",.6f},{"jawOpen",.15f}},{{"mouthStretchLeft",.4f},{"mouthStretchRight",.4f}},{{"jawOpen",.3f},{"mouthRollLower",.2f}},{{"mouthClose",.3f},{"mouthPressLeft",.3f}}};
  w={{cycle[i][0].key,cycle[i][0].v*a},{cycle[i][1].key,cycle[i][1].v*a},{"browInnerUp",.15f}};
 } else return 0;
 int touched=0;
 for(size_t n=0;n<model.nodes.size()&&n<pose.size();++n){const auto& node=model.nodes[n];if(node.mesh<0)continue;
  const auto& mesh=model.meshes.at(size_t(node.mesh));const size_t count=std::min(mesh.morphTargetNames.size(),size_t(render::kMaxMorphTargets));
  for(size_t k=0;k<count;++k)for(const auto& x:w)if(mesh.morphTargetNames[k]==x.key){pose[n].w[k]=std::max(pose[n].w[k],x.v);++touched;}}
 return touched;
}
}
