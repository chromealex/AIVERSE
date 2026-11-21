#include "HumanFaceShape.h"
#include <set>
#include <map>
#include <algorithm>
#include <cmath>
#include <limits>
namespace aiverse::vivid {
using namespace render;
namespace {
constexpr AppearanceField fields[]={
 {"width","Ширина лица",-.3f,.3f,0},{"height","Высота лица",-.25f,.25f,0},
 {"jawWidth","Ширина челюсти",-.3f,.3f,0},{"chinLength","Длина подбородка",-.2f,.2f,0},
 {"chinForward","Выступ подбородка",-.2f,.2f,0},{"cheekWidth","Ширина скул",-.25f,.25f,0},
 {"cheekForward","Выступ скул",-.15f,.15f,0},{"noseWidth","Ширина носа",-.3f,.3f,0},
 {"noseLength","Длина носа",-.2f,.2f,0},{"noseForward","Выступ носа",-.15f,.15f,0},
 {"mouthWidth","Ширина рта",-.25f,.25f,0},{"mouthHeight","Положение рта",-.12f,.12f,0},
 {"lipVolume","Объём губ",-.1f,.1f,0},{"eyeSpacing","Расстояние между глазами",-.15f,.15f,0},
 {"eyeHeight","Положение глаз",-.12f,.12f,0},{"browHeight","Высота бровей",-.15f,.15f,0},
 {"forehead","Форма лба",-.15f,.15f,0},{"earWidth","Положение ушей",-.2f,.2f,0}};
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
float number(const Json& j,float lo,float hi){require(j.isNumber()&&std::isfinite(j.toDouble())&&j.toDouble()>=lo&&j.toDouble()<=hi,"Invalid facial number");return float(j.toDouble());}
float3 vector(const Json& j){require(j.isArray()&&j.size()==3,"Facial landmark requires XYZ");return {number(j[0],-1,2),number(j[1],-1,2),number(j[2],-1,2)};}
Json json(float3 v){auto a=Json::array();for(float f:{v.x,v.y,v.z})a.push(Json::number(double(f),8));return a;}
struct Point {uint32_t vertex;mat4 bind;};
struct Surface {std::vector<Point> points;float3 lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};};
Surface surface(const Model& m,const std::vector<std::string>& names,bool includeLods=false){Surface out;std::set<std::string> found;std::set<uint32_t> seen;
 for(size_t ni=0;ni<m.nodes.size();++ni){const auto& n=m.nodes[ni];if(n.mesh<0)continue;const auto& mesh=m.meshes.at(size_t(n.mesh));if(!names.empty()&&std::find(names.begin(),names.end(),mesh.name)==names.end())continue;found.insert(mesh.name);
  std::vector<int> levels{n.mesh};if(includeLods)for(const auto& lod:mesh.lods)levels.push_back(lod.mesh);
  for(int level:levels){for(const auto& p:m.meshes.at(size_t(level)).primitives)for(uint32_t ii=0;ii<p.indexCount;++ii){const auto id=m.indices.at(p.firstIndex+ii);if(!seen.insert(id).second)continue;const auto& v=m.vertices.at(id);auto bind=m.bindWorld.at(ni);
   if(n.skin>=0&&!p.jointPalette.empty()){for(auto& x:bind.m)x=0;const auto& skin=m.skins.at(size_t(n.skin));const float total=float(v.weights[0])+v.weights[1]+v.weights[2]+v.weights[3];require(total>0,"Unweighted face vertex");for(int k=0;k<4;++k)if(v.weights[k]){const auto j=p.jointPalette.at(v.joints[k]);auto b=m.bindWorld.at(size_t(skin.joints.at(j)))*skin.inverseBind.at(j);for(int c=0;c<16;++c)bind.m[c]+=b.m[c]*v.weights[k]/total;}}
   const auto world=bind.transformPoint(v.pos);out.lo=min3(out.lo,world);out.hi=max3(out.hi,world);out.points.push_back({id,bind});
  }}
 }
 for(const auto& name:names)require(found.contains(name),"Selected facial mesh missing");require(!out.points.empty(),"Empty facial surface");return out;
}
float3 normalized(float3 p,const Surface& s){return {(p.x-s.lo.x)/(s.hi.x-s.lo.x),(p.y-s.lo.y)/(s.hi.y-s.lo.y),(p.z-s.lo.z)/(s.hi.z-s.lo.z)};}
}
HumanFaceShape HumanFaceShape::fromJson(const Json& j){require(j.isObject()&&j["version"].isNumber()&&j["version"].toDouble()==1,"Face shape version must be 1");for(const auto& [k,v]:j.members())require(k=="version"||k=="meshes"||k=="values"||k=="landmarks","Unknown face shape field");HumanFaceShape out;
 require(j["meshes"].isArray()&&j["meshes"].size()>0&&j["meshes"].size()<=16,"Select 1..16 facial meshes");std::set<std::string> unique;
 for(const auto& n:j["meshes"].items()){require(n.isString()&&!n.str().empty()&&n.str().size()<=256&&unique.insert(n.str()).second,"Invalid face mesh name");out.meshes.push_back(n.str());}
 require(j["values"].isObject(),"Facial controls must be an object");for(const auto& [key,v]:j["values"].members()){auto f=std::find_if(std::begin(fields),std::end(fields),[&](auto f){return key==f.key;});require(f!=std::end(fields),"Unknown facial control");out.values[key]=number(v,f->min,f->max);}
 require(j["landmarks"].isArray()&&j["landmarks"].size()<=1024,"Too many facial landmarks");for(const auto& l:j["landmarks"].items()){require(l.isObject(),"Invalid facial landmark");for(const auto& [k,v]:l.members())require(k=="from"||k=="to"||k=="radius"||k=="weight","Unknown landmark field");out.landmarks.push_back({vector(l["from"]),vector(l["to"]),number(l["radius"],.01f,1),number(l["weight"],0,1)});}return out;
}
Json HumanFaceShape::toJson() const{auto j=Json::object(),m=Json::array(),v=Json::object(),l=Json::array();j.set("version",Json::number(int64_t(1)));for(auto& n:meshes)m.push(Json::string(n));for(auto& [k,f]:values)v.set(k,Json::number(double(f),8));for(auto& p:landmarks){auto a=Json::object();a.set("from",json(p.from));a.set("to",json(p.to));a.set("radius",Json::number(double(p.radius),8));a.set("weight",Json::number(double(p.weight),8));l.push(a);}j.set("meshes",m);j.set("values",v);j.set("landmarks",l);return j;}
Json HumanFaceShape::schema(){auto a=Json::array();for(auto f:fields){auto j=Json::object();j.set("key",Json::string(f.key));j.set("label",Json::string(f.label));j.set("min",Json::number(double(f.min)));j.set("max",Json::number(double(f.max)));a.push(j);}return a;}
void applyHumanFaceShape(Model& model,const HumanFaceShape& recipe){(void)HumanFaceShape::fromJson(recipe.toJson());bool active=false;for(const auto& [k,v]:recipe.values)active|=v!=0;for(const auto& l:recipe.landmarks)active|=l.weight>0&&length(l.to-l.from)>0;if(!active)return;const auto s=surface(model,recipe.meshes,true);const auto extent=s.hi-s.lo;require(extent.x>1e-5f&&extent.y>1e-5f&&extent.z>1e-5f,"Facial mesh has degenerate bounds");
 auto value=[&](const char* key){auto i=recipe.values.find(key);return i==recipe.values.end()?0.f:i->second;};
 auto field=[&](float3 p){const auto q=normalized(p,s);auto r=q;const float x=q.x-.5f,y=q.y;auto bell=[](float v,float c,float w){const float t=(v-c)/w;return std::exp(-t*t);};
  const float front=std::clamp((q.z-.25f)*2,0.f,1.f),face=[&]{const float t=std::clamp((y-.15f)/.30f,0.f,1.f);return t*t*(3-2*t);}(); // smooth neck falloff: no kink above the collar seam
  const float jaw=bell(y,.30f,.15f),cheek=bell(y,.55f,.13f),nose=bell(x,0,.12f)*bell(y,.53f,.15f)*front,mouth=bell(x,0,.20f)*bell(y,.37f,.07f)*front,eye=bell(std::abs(x),.19f,.10f)*bell(y,.65f,.10f)*front;
  r.x+=face*x*(value("width")+value("jawWidth")*jaw+value("cheekWidth")*cheek+value("noseWidth")*nose+value("mouthWidth")*mouth+value("earWidth")*bell(std::abs(x),.44f,.10f));
  r.y+=face*((y-.5f)*value("height")-value("chinLength")*bell(y,.23f,.09f)+value("noseLength")*nose+value("mouthHeight")*mouth+value("eyeHeight")*eye+value("browHeight")*bell(y,.74f,.07f)*front);
  r.x+=face*(x<0?-1.f:1.f)*value("eyeSpacing")*eye;
  r.z+=face*(value("chinForward")*bell(y,.23f,.10f)*front+value("cheekForward")*cheek*front+value("noseForward")*nose+value("lipVolume")*mouth+value("forehead")*bell(y,.85f,.13f)*front);
  float3 displacement{};float weight=0;for(const auto& l:recipe.landmarks){const float d=length(q-l.from)/l.radius;if(d<1){const float w=(1-d)*(1-d)*(1+2*d)*l.weight;displacement+=(l.to-l.from)*w;weight+=w;}}
  r+=displacement*(1/std::max(1.f,weight));return float3{s.lo.x+r.x*extent.x,s.lo.y+r.y*extent.y,s.lo.z+r.z*extent.z};
 };
 const auto source=model.vertices;std::vector<mat4> transforms(model.vertices.size());std::vector<bool> changed(model.vertices.size());
 for(const auto& point:s.points){const auto& v=source[point.vertex];auto& out=model.vertices[point.vertex];const auto p=point.bind.transformPoint(v.pos),q=field(p);const float epsilon=extent.y*.0001f;mat4 local;
  for(int c=0;c<3;++c){const auto axis=point.bind.transformDir({c==0?1.f:0,c==1?1.f:0,c==2?1.f:0});const auto delta=(field(p+axis*epsilon)-q)*(1/epsilon);local.at(0,c)=delta.x;local.at(1,c)=delta.y;local.at(2,c)=delta.z;}
  const auto inv=point.bind.inverse();auto jacobian=inv*local;out.pos=inv.transformPoint(q);out.nrm=normalize(jacobian.normalMatrix().transformDir(v.nrm));const auto tangent=normalize(jacobian.transformDir({v.tangent.x,v.tangent.y,v.tangent.z}));out.tangent={tangent.x,tangent.y,tangent.z,v.tangent.w};transforms[point.vertex]=jacobian;changed[point.vertex]=true;
 }
 for(auto& mesh:model.meshes)for(auto& p:mesh.primitives)for(int target=0;target<p.morphTargets;++target)for(uint32_t i=0;i<p.vertexCount;++i)if(changed.at(p.firstVertex+i)){const size_t offset=(size_t(target)*p.vertexCount+i)*6;require(offset+6<=p.morphDeltas.size(),"Facial CPU morph deltas missing");auto& d=p.morphDeltas;const auto& t=transforms[p.firstVertex+i];const auto a=t.transformDir({d[offset],d[offset+1],d[offset+2]}),b=t.normalMatrix().transformDir({d[offset+3],d[offset+4],d[offset+5]});d[offset]=a.x;d[offset+1]=a.y;d[offset+2]=a.z;d[offset+3]=b.x;d[offset+4]=b.y;d[offset+5]=b.z;}
 model.markGeometryChanged();model.finalize();
}
HumanFaceShape faceShapeFromScan(const Model& head,const Model& scan,const HumanFaceShape& selection,float maximumDistance,float strength){require(std::isfinite(maximumDistance)&&maximumDistance>0&&maximumDistance<=1&&std::isfinite(strength)&&strength>=0&&strength<=1,"Invalid scan fit limits");(void)HumanFaceShape::fromJson(selection.toJson());const auto h=surface(head,selection.meshes),s=surface(scan,{});const auto extent=h.hi-h.lo;require(extent.x>1e-5f&&extent.y>1e-5f&&extent.z>1e-5f,"Degenerate head bounds");require(s.points.size()<=1000000,"Scan vertex budget exceeded");HumanFaceShape out=selection;out.landmarks.clear();const size_t step=std::max(size_t(1),(h.points.size()+127)/128);
 // Bounded coarse landmark fitting. Sources must already share alignment/units.
 for(size_t i=0;i<h.points.size();i+=step){const auto& point=h.points[i];const auto p=point.bind.transformPoint(head.vertices.at(point.vertex).pos);float best=maximumDistance*maximumDistance;float3 target=p;bool found=false;for(const auto& candidate:s.points){const auto q=candidate.bind.transformPoint(scan.vertices.at(candidate.vertex).pos);const auto d=q-p;const float distance=dot(d,d);if(distance<best){best=distance;target=q;found=true;}}if(found)out.landmarks.push_back({normalized(p,h),normalized(target,h),.18f,strength});}
 require(out.landmarks.size()>=4,"Scan has insufficient nearby correspondences; align it first");return out;
}

HumanFacePickSurface humanFacePickSurface(const Model& m,const HumanFaceShape& selection){
 require(!selection.meshes.empty(),"Select the face meshes first");
 const auto s=surface(m,selection.meshes);const auto extent=s.hi-s.lo;require(extent.x>1e-5f&&extent.y>1e-5f&&extent.z>1e-5f,"Facial mesh has degenerate bounds");
 std::map<uint32_t,float3> world;for(const auto& p:s.points)world.emplace(p.vertex,p.bind.transformPoint(m.vertices.at(p.vertex).pos));
 HumanFacePickSurface out;out.lo=s.lo;out.hi=s.hi;
 for(size_t ni=0;ni<m.nodes.size();++ni){const auto& n=m.nodes[ni];if(n.mesh<0)continue;const auto& mesh=m.meshes.at(size_t(n.mesh));
  if(std::find(selection.meshes.begin(),selection.meshes.end(),mesh.name)==selection.meshes.end())continue;
  for(const auto& p:mesh.primitives)for(uint32_t i=0;i+2<p.indexCount;i+=3){
   const auto a=world.find(m.indices.at(p.firstIndex+i)),b=world.find(m.indices.at(p.firstIndex+i+1)),c=world.find(m.indices.at(p.firstIndex+i+2));
   if(a==world.end()||b==world.end()||c==world.end())continue;
   out.triangles.push_back(a->second);out.triangles.push_back(b->second);out.triangles.push_back(c->second);
  }}
 require(!out.triangles.empty(),"Selected face meshes have no triangles");return out;
}
bool humanFaceUvAt(const Model& m,const HumanFaceShape& selection,const std::vector<std::string>& skinMaterials,float x,float y,float& u,float& v){
 require(!selection.meshes.empty(),"Select the face meshes first");
 const auto s=surface(m,selection.meshes);const auto extent=s.hi-s.lo;require(extent.x>1e-5f&&extent.y>1e-5f&&extent.z>1e-5f,"Facial mesh has degenerate bounds");
 std::set<uint32_t> skin;
 for(const auto& n:m.nodes){if(n.mesh<0)continue;const auto& mesh=m.meshes.at(size_t(n.mesh));
  if(std::find(selection.meshes.begin(),selection.meshes.end(),mesh.name)==selection.meshes.end())continue;
  for(const auto& p:mesh.primitives){
   if(!skinMaterials.empty()){if(p.material<0||size_t(p.material)>=m.materials.size())continue;
    if(std::find(skinMaterials.begin(),skinMaterials.end(),m.materials[size_t(p.material)].name)==skinMaterials.end())continue;}
   for(uint32_t i=0;i<p.vertexCount;++i)skin.insert(p.firstVertex+i);}}
 float best=1e30f;bool found=false;
 for(const auto& point:s.points){
  if(!skin.contains(point.vertex))continue;
  const auto q=normalized(point.bind.transformPoint(m.vertices.at(point.vertex).pos),s);
  const float dx=q.x-x,dy=q.y-y,back=std::max(0.f,.75f-q.z);
  const float score=dx*dx+dy*dy+back*back*.5f;
  if(score<best){best=score;found=true;u=m.vertices[point.vertex].uv.x;v=m.vertices[point.vertex].uv.y;}
 }
 return found;
}
bool raycastHumanFace(const HumanFacePickSurface& s,float3 origin,float3 direction,HumanFaceHit& hit){
 const float dl=length(direction);require(std::isfinite(dl)&&dl>1e-12f&&std::isfinite(origin.x)&&std::isfinite(origin.y)&&std::isfinite(origin.z),"Invalid sculpt ray");
 direction=direction*(1.f/dl);const auto extent=s.hi-s.lo;
 bool found=false;float best=1e30f;float3 bestPoint{},bestNormal{};
 for(size_t i=0;i+2<s.triangles.size();i+=3){
  const float3 a=s.triangles[i],e1=s.triangles[i+1]-a,e2=s.triangles[i+2]-a,q=cross(direction,e2);const float det=dot(e1,q);
  if(std::fabs(det)<1e-12f)continue;const float inv=1.f/det;const float3 t=origin-a;const float u=dot(t,q)*inv;if(u<0||u>1)continue;
  const float3 r=cross(t,e1);const float v=dot(direction,r)*inv;if(v<0||u+v>1)continue;const float d=dot(e2,r)*inv;
  if(d>0&&d<best){best=d;found=true;bestPoint=origin+direction*d;float3 nrm=cross(e1,e2);if(dot(nrm,direction)>0)nrm=nrm*-1.f;bestNormal=nrm;}
 }
 if(!found)return false;
 // Normals transform with the inverse-transpose of the per-axis normalization.
 float3 nn{bestNormal.x*extent.x,bestNormal.y*extent.y,bestNormal.z*extent.z};const float nl=length(nn);
 hit.point={(bestPoint.x-s.lo.x)/extent.x,(bestPoint.y-s.lo.y)/extent.y,(bestPoint.z-s.lo.z)/extent.z};
 hit.normal=nl>1e-12f?nn*(1.f/nl):float3{0,0,1};hit.distance=best;return true;
}
bool raycastHumanFace(const Model& m,const HumanFaceShape& selection,float3 origin,float3 direction,HumanFaceHit& hit){
 return raycastHumanFace(humanFacePickSurface(m,selection),origin,direction,hit);
}
HumanFaceShape::Landmark humanSculptLandmark(const HumanFaceHit& hit,float radius,float strength){
 require(std::isfinite(radius)&&radius>=.01f&&radius<=1&&std::isfinite(strength)&&std::fabs(strength)<=.25f,"Sculpt brush radius 0.01..1, strength -0.25..0.25");
 const auto clamp=[](float3 p){return float3{std::clamp(p.x,-1.f,2.f),std::clamp(p.y,-1.f,2.f),std::clamp(p.z,-1.f,2.f)};};
 return {clamp(hit.point),clamp(hit.point+hit.normal*strength),radius,1.f};
}
}
