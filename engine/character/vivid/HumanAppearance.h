// VIVID appearance recipe: imported surface adjustments and presentation-only body shaping.
#pragma once
#include "engine/data/Json.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
namespace aiverse::vivid {
struct AppearanceField {const char* key;const char* label;float min,max,initial;};
inline constexpr AppearanceField appearanceFields[]={
 {"skin.roughness","Skin roughness",.15f,1,.62f},
 {"skin.oiliness","Oiliness",0,1,.06f},{"skin.wetness","Wetness",0,1,0},
 {"skin.normal","Normal texture strength",0,2,.65f},{"skin.cavity","Cavity strength",0,1,.8f},
 {"skin.texture","Colour texture strength",0,1,1},{"skin.exposure","Skin exposure (stops)",-2,2,0},
 {"skin.saturation","Skin saturation",0,2,1},{"skin.contrast","Skin contrast",.5f,1.5f,1},
 {"skin.redness","Skin redness",0,1,0},{"skin.tintR","Skin tint red",0,1,1},
 {"skin.tintG","Skin tint green",0,1,1},{"skin.tintB","Skin tint blue",0,1,1},
 {"skin.freckles","Freckle strength",0,1,0},{"skin.freckleDensity","Freckle density",0,1,.35f},
 {"skin.freckleScale","Freckle frequency",40,300,140},{"skin.freckleTone","Freckle darkness",0,1,.5f},
 {"skin.pores","Pore colour detail",0,1,.1f},{"skin.poreScale","Pore frequency",20,300,150},
 {"hair.roughness","Hair roughness",.15f,1,.65f},{"hair.melanin","Melanin",0,1,.72f},
 {"hair.redness","Hair redness",0,1,.12f},{"hair.dyeR","Dye red",0,1,1},
 {"hair.dyeG","Dye green",0,1,1},{"hair.dyeB","Dye blue",0,1,1},
 {"hair.gray","Gray strand fraction",0,1,0},{"hair.grayLightness","Gray lightness",0,1,.65f},
 {"hair.variation","Strand colour variation",0,1,.25f},
 {"hair.rootDarkening","Root darkening",0,1,.15f},{"hair.ombre","Ombre intensity",0,1,0},
 {"hair.ombreStart","Ombre start",0,1,.55f},{"hair.ombreSoftness","Ombre softness",.02f,.5f,.2f},
 {"hair.tipR","Tip red",0,1,.5f},{"hair.tipG","Tip green",0,1,.2f},{"hair.tipB","Tip blue",0,1,.08f},
 {"hair.highlights","Highlight strand fraction",0,1,0},{"hair.highlightStrength","Highlight intensity",0,1,.5f},
 {"hair.coverage","Strand coverage",.25f,1.5f,1},{"hair.cutoff","Dense core threshold",.1f,.9f,.5f},
 {"lashes.coverage","Eyelash coverage",0,1.5f,1},{"lashes.roughness","Eyelash roughness",.15f,1,.75f},
 {"eyes.corneaIor","Cornea refraction",1.f,1.5f,1.376f},
 {"eyes.tearFilm","Cornea wetness",0,1,.7f},{"eyes.depth","Iris optical depth",0,.03f,.012f},
 {"eyes.roughness","Eye surface roughness",.08f,.65f,.6f},{"eyes.irisSize","Iris radius (UV)",.08f,.19f,.14f},
 {"eyes.pupilSize","Pupil radius (UV)",.008f,.075f,.048f},
 {"eyes.irisR","Iris red",0,1,.176f},{"eyes.irisG","Iris green",0,1,.077f},{"eyes.irisB","Iris blue",0,1,.045f},
 {"eyes.irisTexture","Iris texture detail",0,1,.7f},{"eyes.limbus","Limbus darkness",0,1,.35f},
 {"eyes.limbusWidth","Limbus width",.002f,.04f,.015f},{"eyes.scleraLightness","Sclera lightness",.2f,1,.40f},
 {"eyes.redness","Sclera redness",0,1,.08f},{"eyes.leftU","Left iris centre U",0,1,.488389f},
 {"eyes.rightU","Right iris centre U",0,1,.50877f},{"eyes.centerV","Iris centre V",0,1,.504f},
 {"cloth.roughness","Fabric roughness",.3f,1,.88f},{"cloth.normal","Fabric normal strength",0,2,.4f},
 {"cloth.tintR","Fabric tint red",0,1,1},{"cloth.tintG","Fabric tint green",0,1,1},{"cloth.tintB","Fabric tint blue",0,1,1},
 {"cloth.saturation","Fabric saturation",0,2,1},{"cloth.exposure","Fabric exposure (stops)",-2,2,0},
 {"body.height","Height scale",.8f,1.2f,1},{"body.width","Body width",.8f,1.2f,1},
 {"body.depth","Body depth",.8f,1.2f,1},{"body.shoulders","Shoulder width",.8f,1.2f,1},
 {"body.chest","Chest width",.8f,1.2f,1},{"body.waist","Waist width",.75f,1.25f,1},
 {"body.hips","Hip width",.8f,1.2f,1},{"body.head","Head scale",.85f,1.15f,1},
 {"body.legs","Leg length",.85f,1.15f,1},{"body.torso","Torso length",.85f,1.15f,1},
 // Makeup / detail layers: UV-space ellipses on the skin texture (centre U of
 // the face, then per-layer centre V and size), aligned once per model.
 {"makeup.faceU","Face centre U",0,1,.5f},
 {"makeup.lips","Lipstick",0,1,0},{"makeup.lipR","Lipstick red",0,1,.55f},{"makeup.lipG","Lipstick green",0,1,.12f},{"makeup.lipB","Lipstick blue",0,1,.16f},
 {"makeup.lipV","Lips centre V",0,1,.3f},{"makeup.lipW","Lips width (UV)",.005f,.3f,.06f},{"makeup.lipH","Lips height (UV)",.005f,.2f,.025f},
 {"makeup.blush","Blush",0,1,0},{"makeup.blushR","Blush red",0,1,.85f},{"makeup.blushG","Blush green",0,1,.35f},{"makeup.blushB","Blush blue",0,1,.35f},
 {"makeup.blushOffset","Blush distance from centre (UV)",0,.4f,.1f},{"makeup.blushV","Blush centre V",0,1,.45f},{"makeup.blushSize","Blush size (UV)",.005f,.3f,.06f},
 {"makeup.shadow","Eyeshadow",0,1,0},{"makeup.shadowR","Eyeshadow red",0,1,.35f},{"makeup.shadowG","Eyeshadow green",0,1,.25f},{"makeup.shadowB","Eyeshadow blue",0,1,.3f},
 {"makeup.shadowOffset","Eyeshadow distance from centre (UV)",0,.4f,.07f},{"makeup.shadowV","Eyeshadow centre V",0,1,.58f},
 {"makeup.shadowW","Eyeshadow width (UV)",.005f,.2f,.04f},{"makeup.shadowH","Eyeshadow height (UV)",.005f,.2f,.02f},
 {"makeup.stubble","Stubble",0,1,0},{"makeup.stubbleDensity","Stubble density",0,1,.5f},
 {"makeup.stubbleV","Stubble centre V",0,1,.25f},{"makeup.stubbleSize","Stubble size (UV)",.01f,.4f,.12f}
};
struct HumanAppearance {
 std::map<std::string,float> values;
 std::map<std::string,std::string> materials;
 float get(const std::string& key) const {
  if(auto it=values.find(key);it!=values.end())return it->second;
  for(const auto& f:appearanceFields)if(key==f.key)return f.initial;
  throw std::invalid_argument("Unknown appearance control: "+key);
 }
 static HumanAppearance fromJson(const Json& j) {
  if(!j.isObject()||!j["version"].isNumber()||j["version"].toDouble()!=1)throw std::invalid_argument("Appearance version must be 1");
  for(const auto& [k,v]:j.members())if(k!="version"&&k!="values"&&k!="materials")throw std::invalid_argument("Unknown appearance section");
  HumanAppearance a;
  if(!j["values"].isObject()||!j["materials"].isObject()||j["materials"].size()>128)throw std::invalid_argument("Invalid appearance objects");
  for(const auto& [k,v]:j["values"].members()) {
   const auto f=std::find_if(std::begin(appearanceFields),std::end(appearanceFields),[&](const auto& f){return k==f.key;});
   if(f==std::end(appearanceFields)||!v.isNumber()||!std::isfinite(v.toDouble())||v.toDouble()<f->min||v.toDouble()>f->max)
    throw std::invalid_argument("Invalid appearance control: "+k);
   a.values.emplace(k,float(v.toDouble()));
  }
  for(const auto& [k,v]:j["materials"].members()) {
   if(k.empty()||k.size()>256||!v.isString()||(v.str()!="skin"&&v.str()!="hair"&&v.str()!="lashes"&&v.str()!="eyes"&&v.str()!="cloth"))throw std::invalid_argument("Invalid appearance material role");
   a.materials.emplace(k,v.str());
  }
  return a;
 }
 Json toJson() const {
  auto j=Json::object(),v=Json::object(),m=Json::object();j.set("version",Json::number(int64_t(1)));
  for(const auto& f:appearanceFields)v.set(f.key,Json::number(double(get(f.key)),6));
  for(const auto& [key,role]:materials)m.set(key,Json::string(role));
  j.set("values",std::move(v));j.set("materials",std::move(m));return j;
 }
 static Json schema() {
  auto list=Json::array();for(const auto& f:appearanceFields){auto j=Json::object();j.set("key",Json::string(f.key));j.set("label",Json::string(f.label));j.set("min",Json::number(double(f.min),6));j.set("max",Json::number(double(f.max),6));j.set("initial",Json::number(double(f.initial),6));list.push(std::move(j));}return list;
 }
};
// Apply once to a fresh unpublished model. Does not change textures, geometry or gameplay.
void applyHumanAppearance(render::Model&,const HumanAppearance&);
// Post-animation presentation deformation. Upright Y-up full body only; all garment
// skeletons receive the same world-space field. Does not mutate bind data/Animator.
void applyHumanBody(const render::Model&,const HumanAppearance&,std::span<render::mat4>);
} // namespace aiverse::vivid

namespace aiverse::vivid {
// Operates on an unpublished CPU candidate; caller discards it on failure.
void bakeHumanAppearance(render::Model&,const HumanAppearance&,int maxTextureSize=1024);
}
