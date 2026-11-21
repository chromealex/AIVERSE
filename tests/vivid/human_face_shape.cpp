#include "engine/character/vivid/HumanFaceShape.h"
#include "tools/modeling/Modeling.h"
#include <iostream>
using namespace aiverse;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
 auto doc=modeling::emptyDocument();doc.set("nodes",Json::parse(R"([{"id":"head","geometry":{"kind":"box"}}])"));
 auto base=modeling::build(doc),edited=modeling::build(doc);vivid::HumanFaceShape shape;shape.meshes={base.model->meshes[0].name};
 check(vivid::HumanFaceShape::fromJson(shape.toJson()).toJson()==shape.toJson(),"face recipe roundtrip");
 vivid::applyHumanFaceShape(*edited.model,shape);check(edited.model->vertices[0].pos.x==base.model->vertices[0].pos.x,"neutral shape changed source");
 shape.values["width"]=.2f;vivid::applyHumanFaceShape(*edited.model,shape);check(edited.model->vertices.size()==base.model->vertices.size()&&edited.model->indices==base.model->indices,"face edit changed topology");check(edited.model->boundsMax.x>base.model->boundsMax.x,"width control has no effect");
 auto photo=modeling::build(doc);shape.values.clear();shape.landmarks={{{1,1,1},{.8f,1,1},.5f,1}};vivid::applyHumanFaceShape(*photo.model,shape);bool moved=false;for(size_t i=0;i<photo.model->vertices.size();++i){auto a=base.model->vertices[i].pos,b=photo.model->vertices[i].pos;check(std::abs(a.z-b.z)<1e-5f,"front-photo landmark invented depth");moved|=std::abs(a.x-b.x)>1e-3f;}check(moved,"photo landmark has no effect");
 shape.landmarks.clear();auto fit=vivid::faceShapeFromScan(*base.model,*edited.model,shape,.4f,1);check(fit.landmarks.size()>=4,"scan correspondences missing");check(vivid::HumanFaceShape::fromJson(fit.toJson()).landmarks.size()==fit.landmarks.size(),"scan output invalid");
 auto bad=shape.toJson();bad.member("values").set("width",Json::number(100.));bool rejected=false;try{vivid::HumanFaceShape::fromJson(bad);}catch(...){rejected=true;}check(rejected,"out of range face edit accepted");
 {// viewport sculpt: ray hit in normalized head space + push landmark
  vivid::HumanFaceShape sel;sel.meshes={base.model->meshes[0].name};const auto lo=base.model->boundsMin,hi=base.model->boundsMax;
  const render::float3 c{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};vivid::HumanFaceHit hit;
  check(vivid::raycastHumanFace(*base.model,sel,{c.x,c.y,hi.z+5},{0,0,-1},hit),"sculpt ray missed the front face");
  check(std::abs(hit.point.z-1)<1e-3f&&std::abs(hit.point.x-.5f)<1e-3f&&hit.normal.z>.99f,"sculpt hit/normal not normalized to the face box");
  check(!vivid::raycastHumanFace(*base.model,sel,{hi.x+5,hi.y+5,hi.z+5},{0,0,-1},hit),"sculpt ray outside the head hit something");
  check(vivid::raycastHumanFace(*base.model,sel,{c.x,c.y,hi.z+5},{0,0,-1},hit),"sculpt ray");
  auto pushed=modeling::build(doc);sel.landmarks={vivid::humanSculptLandmark(hit,1.f,.1f)};vivid::applyHumanFaceShape(*pushed.model,sel);
  float out=0;for(size_t i=0;i<base.model->vertices.size();++i)out=std::max(out,pushed.model->vertices[i].pos.z-base.model->vertices[i].pos.z);check(out>1e-4f,"push stroke did not move the surface out");
  bool bad=false;try{(void)vivid::humanSculptLandmark(hit,2.f,.1f);}catch(...){bad=true;}check(bad,"oversized brush accepted");}
 std::cout<<"face shape, topology, scan fit and photo depth checks PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
