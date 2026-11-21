#pragma once
// Presentation-only authoring light rigs. No scene/gameplay state or asset mutation.
#include "engine/render/Renderer3D.h"
#include "engine/render/AdaptiveShadowResolution.h"
#include "engine/data/Json.h"
#include <array>
#include <string>
#include <stdexcept>
namespace aiverse::vivid {
struct HumanStudioPreset {
 const char* id;const char* label;
 render::float3 keyDirection,keyColor;float key;
 render::float3 ambient,background;
 std::array<render::PointLight,3> lights;
};
inline const std::array<HumanStudioPreset,8>& humanStudioPresets(){
 using render::PointLight;
 static const std::array<HumanStudioPreset,8> presets={{
  {"neutral","01 · Нейтральная студия",{-.45f,.7f,1},{1,.96f,.9f},.85f,{.20f,.23f,.28f},{.22f,.24f,.28f},{{PointLight{{.8f,.8f,.8f},2.8f,{.8f,.9f,1},.4f},PointLight{{-.65f,1.15f,-.6f},2.5f,{1,1,1},.65f},PointLight{{0,1.6f,.1f},2.5f,{1,.95f,.9f},.3f}}}},
  {"portrait","02 · Мягкий портрет",{-.35f,.45f,1},{1,.88f,.78f},.7f,{.24f,.22f,.23f},{.29f,.24f,.23f},{{PointLight{{.65f,.85f,1},3,{.82f,.88f,1},.45f},PointLight{{-.6f,1.15f,-.6f},2.5f,{1,.8f,.66f},.5f},PointLight{{0,.3f,1},2.5f,{1,.92f,.85f},.2f}}}},
  {"warm","03 · Тёплый вечер",{-.8f,.4f,.7f},{1,.62f,.32f},1.05f,{.16f,.13f,.16f},{.22f,.12f,.075f},{{PointLight{{.8f,.8f,.8f},3,{.48f,.65f,1},.3f},PointLight{{-.6f,1,-.7f},2.5f,{1,.46f,.18f},.8f},PointLight{{0,1.5f,0},2.5f,{1,.78f,.5f},.2f}}}},
  {"cool","04 · Холодный дневной",{.7f,.8f,1},{.7f,.85f,1},.95f,{.16f,.22f,.32f},{.13f,.20f,.30f},{{PointLight{{-.8f,.8f,.8f},3,{.65f,.8f,1},.4f},PointLight{{.6f,1.15f,-.6f},2.5f,{.65f,.9f,1},.7f},PointLight{{0,1.6f,0},2.5f,{1,1,1},.25f}}}},
  {"dramatic","05 · Контрастный портрет",{-.9f,.4f,.45f},{1,.85f,.68f},1.1f,{.055f,.065f,.085f},{.035f,.04f,.055f},{{PointLight{{.85f,.8f,.8f},2.5f,{.55f,.7f,1},.08f},PointLight{{.7f,1.2f,-.6f},2.5f,{.7f,.82f,1},.75f},PointLight{{0,1.6f,0},2.5f,{1,.8f,.6f},.15f}}}},
  {"rim","06 · Контровой свет",{.1f,.45f,1},{.8f,.88f,1},.35f,{.07f,.09f,.12f},{.055f,.07f,.09f},{{PointLight{{-.65f,1.05f,-.55f},2.5f,{.55f,.75f,1},1.1f},PointLight{{.65f,1.05f,-.55f},2.5f,{1,.68f,.4f},1.1f},PointLight{{0,.85f,1},2.5f,{1,.95f,.9f},.25f}}}},
  {"gel","07 · Цветные фильтры",{-.7f,.6f,.7f},{.45f,.8f,1},.8f,{.10f,.055f,.14f},{.12f,.045f,.18f},{{PointLight{{.7f,.85f,.5f},2.5f,{1,.2f,.5f},.75f},PointLight{{-.6f,1.15f,-.6f},2.5f,{.2f,.8f,1},.9f},PointLight{{.6f,1.3f,-.5f},2.5f,{.8f,.3f,1},.7f}}}},
  {"catalog","08 · Светлый каталог",{-.2f,.65f,1},{1,.98f,.95f},.8f,{.34f,.35f,.38f},{.72f,.73f,.75f},{{PointLight{{.8f,.9f,.9f},3,{1,1,1},.5f},PointLight{{-.8f,.9f,.9f},3,{1,1,1},.35f},PointLight{{0,1.5f,-.6f},3,{1,1,1},.6f}}}}
 }};return presets;
}
struct HumanStudioLighting {
 std::string preset="neutral";float rotation=0,gain=1;bool backdrop=true;
 const HumanStudioPreset& rig() const {for(const auto& p:humanStudioPresets())if(preset==p.id)return p;throw std::invalid_argument("Unknown studio light preset");}
 Json toJson() const {auto j=Json::object();j.set("preset",Json::string(preset));j.set("rotation",Json::number(double(rotation)));j.set("gain",Json::number(double(gain)));j.set("backdrop",Json::fromBool(backdrop));return j;}
 static HumanStudioLighting fromJson(const Json& j){
  if(!j.isObject())throw std::invalid_argument("Studio lighting must be an object");
  for(const auto& [k,v]:j.members())if(k!="preset"&&k!="rotation"&&k!="gain"&&k!="backdrop")throw std::invalid_argument("Unknown studio lighting field");
  HumanStudioLighting out;out.preset=j.getStr("preset");(void)out.rig();
  auto number=[&](const char* k,float lo,float hi){const auto& v=j[k];if(!v.isNumber()||!std::isfinite(v.toDouble())||v.toDouble()<lo||v.toDouble()>hi)throw std::invalid_argument("Studio lighting value out of range");return float(v.toDouble());};
  out.rotation=number("rotation",-180,180);out.gain=number("gain",.25f,2);
  if(j["backdrop"].type()!=Json::Type::Bool)throw std::invalid_argument("Invalid backdrop toggle");out.backdrop=j.getBool("backdrop");return out;
 }
 Json describe()const {auto j=Json::object(),items=Json::array();for(const auto& p:humanStudioPresets()){auto item=Json::object();item.set("id",Json::string(p.id));item.set("label",Json::string(p.label));item.set("sources",Json::number(int64_t(4)));items.push(item);}j.set("settings",toJson());j.set("presets",items);return j;}
};
inline void applyHumanStudioLighting(render::Renderer3D& renderer,const HumanStudioLighting& settings,render::float3 foot,float height){
 // Character-stage shadow coverage: independent of camera zoom, includes
 // outstretched arms and nearby floor. Do not spend facial texels on the room.
 const float sh=std::max(.01f,height);
 renderer.setShadowBounds(foot+render::float3{-.75f,-.05f,-.75f}*sh,
                          foot+render::float3{.75f,1.15f,.75f}*sh);
 const auto& p=settings.rig();const float a=settings.rotation*.01745329252f;
 auto rotate=[&](render::float3 v){return render::float3{std::cos(a)*v.x+std::sin(a)*v.z,v.y,-std::sin(a)*v.x+std::cos(a)*v.z};};
 renderer.lighting=render::Lighting{};auto& l=renderer.lighting;l.fogDensity=0;l.sunDir=rotate(p.keyDirection);l.sunColor=p.keyColor;l.sunIntensity=p.key*settings.gain;l.skyColor=p.ambient*settings.gain;l.groundColor=p.ambient*(.55f*settings.gain);// Portrait rigs use a broad fixed soft key. The general PCSS path uses a
 // small fixed angular emitter and collapsed nose shadows to almost hard edges.
 l.shadowContactHardening=false;l.shadowSoftness=.012f*height;
 renderer.lights.setQuality(render::LightQuality::High);
 for(size_t i=0;i<p.lights.size();++i){auto light=p.lights[i];light.pos=foot+rotate(light.pos)*height;light.radius*=height;light.intensity*=settings.gain;light.key=uint32_t(0x564900+i);renderer.lights.add(light);}
}
// Call once per presented view after lighting setup. Coverage includes the
// renderer fitting margin and the diagonal of our fixed character-stage box.
inline void updateHumanStudioShadows(render::Renderer3D& renderer,
                                    render::AdaptiveShadowResolution& policy,
                                    float height,int viewportHeight) {
 if(renderer.shadowQuality()!=render::ShadowQuality::High)return;
 const auto& camera=renderer.camera();
 const float coverage=2.44f*std::max(.01f,height)+2.f;
 const float demand=render::shadowTexelDemand(coverage,float(viewportHeight),camera.distance,camera.fovY);
 if(policy.update(demand)) (void)renderer.setShadowMapSize(policy.resolution);
}
// Unit-height cyclorama: smooth quarter circle from horizontal floor to wall.
inline render::MeshBuilder humanStudioCyclorama(render::float3 foot={},float height=1){
 using namespace render;MeshBuilder b;constexpr int segments=64;uint32_t previous=0;bool first=true;
 auto row=[&](float y,float z,float3 normal){const auto left=b.addVertex(foot+float3{-5,y,z}*height,normal,0xffffffffu);b.addVertex(foot+float3{5,y,z}*height,normal,0xffffffffu);if(!first){b.addTriangle(previous,previous+1,left+1);b.addTriangle(previous,left+1,left);}previous=left;first=false;};
 row(0,4,{0,1,0});row(0,-1,{0,1,0});
 for(int i=1;i<=segments;++i){const float a=float(i)/segments*1.57079632679f;row(1-std::cos(a),-1-std::sin(a),{0,std::cos(a),std::sin(a)});}
 row(4,-2,{0,0,1});return b;
}
}
