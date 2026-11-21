#include "HumanWardrobe.h"
#include <set>
namespace aiverse::vivid {
using namespace render;
namespace {
float3 direction(const mat4& m,float3 v){auto r=m*float4{v.x,v.y,v.z,0};return {r.x,r.y,r.z};}
void need(bool ok,const std::string& message){if(!ok)throw std::invalid_argument(message);}
void key(const Json& value){need(value.isString()&&!value.str().empty()&&value.str().size()<=512,"Invalid wardrobe asset/name");}
void names(const Json& list){need(list.isArray()&&list.size()<=256,"Invalid hidden mesh list");for(const auto& v:list.items())key(v);}
float3 vec(const Json& j,float3 fallback){if(j.isNull())return fallback;need(j.isArray()&&j.size()==3,"Expected XYZ vector");float v[3];for(int k=0;k<3;++k){need(j[size_t(k)].isNumber()&&std::isfinite(j[size_t(k)].toDouble())&&std::abs(j[size_t(k)].toDouble())<=100,"Invalid transform");v[k]=float(j[size_t(k)].toDouble());}return {v[0],v[1],v[2]};}
void known(const Json& j,std::initializer_list<const char*> fields){need(j.isObject(),"Expected wardrobe object");for(const auto& [k,v]:j.members()){bool ok=false;for(auto f:fields)ok|=k==f;need(ok,"Unknown wardrobe field: "+k);}}
void hide(Model& model,const Json& list){for(const auto& name:list.items()) {bool found=false;for(const auto& mesh:model.meshes)found|=mesh.name==name.str();for(auto& node:model.nodes)if(node.mesh>=0&&model.meshes[size_t(node.mesh)].name==name.str()){node.mesh=-1;node.skin=-1;found=true;}need(found,"Hidden mesh not found: "+name.str());}}
void prune(Model& m){
 std::vector<int> remap(m.meshes.size(),-1);std::vector<MeshDef> meshes;
 auto retain=[&](auto&& self,int source)->int {
  need(source>=0&&size_t(source)<m.meshes.size(),"Invalid wardrobe LOD reference");
  if(remap[size_t(source)]>=0)return remap[size_t(source)];
  const int target=int(meshes.size());remap[size_t(source)]=target;
  meshes.push_back(m.meshes[size_t(source)]);
  meshes[size_t(target)].materialBatches.clear();meshes[size_t(target)].shadowBatch={};
  const auto levels=m.meshes[size_t(source)].lods;
  for(size_t i=0;i<levels.size();++i){const int mapped=self(self,levels[i].mesh);meshes[size_t(target)].lods[i].mesh=mapped;}
  return target;
 };
 for(auto& n:m.nodes)if(n.mesh>=0)n.mesh=retain(retain,n.mesh);
 m.meshes=std::move(meshes);
}
void compactMaterials(Model& m) {
 std::vector<int> mm(m.materials.size(),-1),im(m.images.size(),-1);std::vector<Material> materials;std::vector<Image> images;
 for(auto& mesh:m.meshes)for(auto& p:mesh.primitives){auto& target=mm.at(size_t(p.material));if(target<0){auto material=m.materials.at(size_t(p.material));need(!material.shaderGraph&&(material.shading==ShadingModel::Pbr||(material.shading==ShadingModel::Eye&&material.eyeOptics.z>0)),"Final character contains an unbaked shader");for(auto* t:{&material.baseTex,&material.mrTex,&material.normalTex,&material.occlusionTex,&material.emissiveTex,&material.teamTex})if(*t>=0){auto& mapped=im.at(size_t(*t));if(mapped<0){mapped=int(images.size());images.push_back(m.images[size_t(*t)]);}*t=mapped;}target=int(materials.size());materials.push_back(std::move(material));}p.material=target;p.variantMaterial.clear();}
 m.materials=std::move(materials);m.images=std::move(images);
}
void append(Model& dst,Model src,const Json& item) {
 const auto id=item.getStr("id"),socket=item.getStr("socket");
 const auto bone=socket.empty()?item.getStr("bone"):"socket/"+socket;
 const auto rule=item.getStr("attachmentRule");
 const bool independent=item.getBool("independentRig");
 need(independent||src.clips.empty(),"Clothing must use character animations; remove its independent clips");
 const auto t=vec(item["translation"],{}),r=vec(item["rotation"],{}),s=vec(item["scale"],{1,1,1});
 const bool skinned=!src.skins.empty();
 need(independent||!skinned || (bone.empty()&&t.x==0&&t.y==0&&t.z==0&&r.x==0&&r.y==0&&r.z==0&&s.x==1&&s.y==1&&s.z==1),"Skinned clothing requires matching bind pose, without socket offsets");
 int parent=-1;for(size_t i=0;i<dst.nodes.size();++i)if(!bone.empty()&&dst.nodes[i].name==bone){need(parent<0,"Ambiguous attachment bone: "+bone);parent=int(i);}
 need(bone.empty()||parent>=0,"Accessory bone missing: "+bone);
 const int target=parent;
 // New keep-world mode applies authored offsets in socket-local coordinates.
 // Legacy independentRig records retain their existing offset order.
 if(independent&&parent>=0&&rule=="keep_world"){
  Node offset;offset.name="wardrobe/"+id+"/socket-offset";offset.parent=parent;offset.t=t;offset.r=quat::axisAngle({0,0,1},r.z)*quat::axisAngle({0,1,0},r.y)*quat::axisAngle({1,0,0},r.x);offset.s=s;parent=int(dst.nodes.size());dst.nodes.push_back(offset);
 }
 if(independent&&parent>=0&&rule!="snap"){
  // Retain the imported world bind frame while following the body's socket.
  // Inverse TRS is represented exactly as three nodes, avoiding shear loss.
  std::vector<Node> chain;for(int n=target;n>=0;n=dst.nodes.at(size_t(n)).parent)chain.push_back(dst.nodes.at(size_t(n)));
  int serial=0;for(const auto& n:chain){need(std::abs(n.s.x)>1e-6f&&std::abs(n.s.y)>1e-6f&&std::abs(n.s.z)>1e-6f,"Singular head attachment transform");
   Node scale;scale.name="wardrobe/"+id+"/bind-scale-"+std::to_string(serial);scale.parent=parent;scale.s={1/n.s.x,1/n.s.y,1/n.s.z};parent=int(dst.nodes.size());dst.nodes.push_back(scale);
   Node rotation;rotation.name="wardrobe/"+id+"/bind-rotation-"+std::to_string(serial);rotation.parent=parent;rotation.r={-n.r.x,-n.r.y,-n.r.z,n.r.w};parent=int(dst.nodes.size());dst.nodes.push_back(rotation);
   Node translation;translation.name="wardrobe/"+id+"/bind-translation-"+std::to_string(serial++);translation.parent=parent;translation.t=-n.t;parent=int(dst.nodes.size());dst.nodes.push_back(translation);
  }
 }

 const int no=int(dst.nodes.size()),mo=int(dst.meshes.size()),ma=int(dst.materials.size()),io=int(dst.images.size()),so=int(dst.skins.size());
 const uint32_t vo=uint32_t(dst.vertices.size()),ix=uint32_t(dst.indices.size());
 for(auto& skin:src.skins)for(size_t j=0;j<skin.joints.size();++j){const int old=skin.joints[j];if(independent){skin.joints[j]=old+no;continue;}const auto& name=src.nodes.at(size_t(old)).name;int target=-1;for(size_t k=0;k<dst.nodes.size();++k)if(dst.nodes[k].name==name){need(target<0,"Ambiguous clothing bone: "+name);target=int(k);}need(target>=0,"Clothing bone not in character: "+name);
  for(int k=0;k<16;++k)need(std::abs(src.bindWorld.at(size_t(old)).m[k]-dst.bindWorld.at(size_t(target)).m[k])<.002f,"Clothing bind pose differs: "+name);
  skin.joints[j]=target;
 }
 Node wrapper;wrapper.name="wardrobe/"+id;wrapper.parent=parent;wrapper.t=t;wrapper.r=quat::axisAngle({0,0,1},r.z)*quat::axisAngle({0,1,0},r.y)*quat::axisAngle({1,0,0},r.x);wrapper.s=s;
 if(independent&&target>=0&&rule=="keep_world"){wrapper.t={};wrapper.r={};wrapper.s={1,1,1};}
 const int wrapperIndex=no+int(src.nodes.size());
 for(auto& node:src.nodes){node.name="wardrobe/"+id+"/"+node.name;node.parent=node.parent<0?wrapperIndex:node.parent+no;for(auto& c:node.children)c+=no;if(node.mesh>=0)node.mesh+=mo;if(node.skin>=0)node.skin+=so;dst.nodes.push_back(std::move(node));}
 for(int n=no;n<wrapperIndex;++n)if(dst.nodes[size_t(n)].parent==wrapperIndex)wrapper.children.push_back(n);
 if(parent>=0)dst.nodes[size_t(parent)].children.push_back(wrapperIndex);dst.nodes.push_back(std::move(wrapper));
 for(auto& mesh:src.meshes){mesh.name="wardrobe/"+id+"/"+mesh.name;for(auto& lod:mesh.lods)lod.mesh+=mo;mesh.materialBatches.clear();mesh.shadowBatch={};for(auto& p:mesh.primitives){p.firstVertex+=vo;p.firstIndex+=ix;p.material+=ma;p.variantMaterial.clear();}dst.meshes.push_back(std::move(mesh));}
 for(auto& m:src.materials){m.name="wardrobe/"+id+"/"+m.name;for(auto* tex:{&m.baseTex,&m.mrTex,&m.normalTex,&m.occlusionTex,&m.emissiveTex,&m.teamTex})if(*tex>=0)*tex+=io;need(!m.shaderGraph,"Clothing shader must be baked PBR");dst.materials.push_back(std::move(m));}
 if(independent)for(auto& clip:src.clips){clip.name="wardrobe/"+id+"/"+clip.name;for(auto& c:clip.channels)c.node+=no;dst.clips.push_back(std::move(clip));}
 dst.vertices.insert(dst.vertices.end(),src.vertices.begin(),src.vertices.end());for(auto index:src.indices)dst.indices.push_back(index+vo);
 for(auto& im:src.images)dst.images.push_back(std::move(im));for(auto& skin:src.skins)dst.skins.push_back(std::move(skin));dst.finalize();
}
void mask(Model& model,const Json& entry,const FileReader& reader,int size) {
 const int index=model.findMaterial(entry.getStr("material"));need(index>=0,"Body mask material missing: "+entry.getStr("material"));
 auto& m=model.materials[size_t(index)];Image maskImage;need(readModelImage(entry.getStr("texture"),reader,maskImage),"Cannot decode body visibility mask");
 need(maskImage.width>0&&maskImage.height>0&&maskImage.rgba.size()==size_t(maskImage.width)*maskImage.height*4,"Body mask pixels unavailable");
 const Image* source=m.baseTex<0?nullptr:&model.images.at(size_t(m.baseTex));
 Image out;out.name=m.name+".visibility";out.width=source?source->width:size;out.height=source?source->height:size;out.repeat=source?source->repeat:true;
 if(source){need(source->rgba.size()==size_t(out.width)*out.height*4,"Body colour pixels unavailable");out.rgba=source->rgba;}else out.rgba.resize(size_t(out.width)*out.height*4,255);
 for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x){const size_t from=(size_t(y*maskImage.height/out.height)*maskImage.width+x*maskImage.width/out.width)*4;const size_t to=(size_t(y)*out.width+x)*4+3;out.rgba[to]=uint8_t((unsigned(out.rgba[to])*maskImage.rgba[from]+127)/255);}
 m.baseTex=int(model.images.size());model.images.push_back(std::move(out));if(m.alpha==AlphaMode::Opaque){m.alpha=AlphaMode::Mask;m.alphaCutoff=.5f;}
}
// Bake the bind-pose deformation into vertices and morph vectors; move joint
// pivots and translation keys by the corresponding bind offset. Runtime retains
// the original single skeleton/Animator, with no live body modifier required.
void body(Model& m,const HumanAppearance& look) {
 bool active=false;for(const auto& f:appearanceFields)if(std::string_view(f.key).starts_with("body.")&&look.get(f.key)!=1)active=true;if(!active)return;
 const auto sourceVertices=m.vertices;
 const auto old=m.bindWorld;auto warped=old;applyHumanBody(m,look,warped);
 auto next=old;std::vector<bool> joints(m.nodes.size());for(const auto& skin:m.skins)for(int n:skin.joints)joints.at(size_t(n))=true;
 std::vector<float3> offsets(m.nodes.size());
 for(int index:m.order){auto& n=m.nodes[size_t(index)];const auto p=n.parent<0?mat4{}:next[size_t(n.parent)];if(joints[size_t(index)]){const auto newT=p.inverse().transformPoint({warped[size_t(index)].m[12],warped[size_t(index)].m[13],warped[size_t(index)].m[14]});offsets[size_t(index)]=newT-n.t;n.t=newT;}next[size_t(index)]=p*mat4::trs(n.t,n.r,n.s);}
 std::set<int> owners;
 for(size_t ni=0;ni<m.nodes.size();++ni){const auto& node=m.nodes[ni];if(node.mesh<0)continue;need(owners.insert(node.mesh).second,"Body bake requires unique mesh owners");
  auto local=next[ni].inverse();
  if(node.skin>=0){const auto& skin=m.skins.at(size_t(node.skin));need(!skin.joints.empty(),"Empty character skin");const auto rest=old.at(size_t(skin.joints[0]))*skin.inverseBind.at(0);for(size_t j=1;j<skin.joints.size();++j){const auto check=old.at(size_t(skin.joints[j]))*skin.inverseBind.at(j);for(int k=0;k<16;++k)need(std::abs(check.m[k]-rest.m[k])<.003f,"Nonuniform skin bind space cannot bake body");}local=rest.inverse();}
  std::vector<int> levels{node.mesh};
  for(const auto& lod:m.meshes[size_t(node.mesh)].lods)levels.push_back(lod.mesh);
  for(int meshIndex:levels){
  auto& mesh=m.meshes.at(size_t(meshIndex));
  for(auto& p:mesh.primitives){std::vector<bool> referenced(p.vertexCount);for(uint32_t ii=0;ii<p.indexCount;++ii){const auto vertex=m.indices.at(p.firstIndex+ii);need(vertex>=p.firstVertex&&vertex-p.firstVertex<p.vertexCount,"Primitive vertex range invalid");referenced[vertex-p.firstVertex]=true;}
  for(uint32_t vi=0;vi<p.vertexCount;++vi){if(!referenced[vi])continue;auto& v=m.vertices.at(p.firstVertex+vi);v=sourceVertices.at(p.firstVertex+vi);mat4 deform;
   // Quantized UNORM weights can sum to 254 or 256. Match the GPU
   // normalization exactly; dividing by 255 dents adjacent triangles.
   const bool skinned=node.skin>=0&&!p.jointPalette.empty();
   if(skinned){mat4 rest;for(float& x:rest.m)x=0;for(float& x:deform.m)x=0;const auto& skin=m.skins.at(size_t(node.skin));const float total=float(v.weights[0])+v.weights[1]+v.weights[2]+v.weights[3];need(total>0,"Unweighted skinned vertex");for(int k=0;k<4;++k)if(v.weights[k]){const size_t j=p.jointPalette.at(v.joints[k]);const auto matrix=warped.at(size_t(skin.joints.at(j)))*skin.inverseBind.at(j);const auto original=old.at(size_t(skin.joints.at(j)))*skin.inverseBind.at(j);for(int c=0;c<16;++c){deform.m[c]+=matrix.m[c]*(v.weights[k]/total);rest.m[c]+=original.m[c]*(v.weights[k]/total);}}deform=rest.inverse()*deform;}
   else deform=local*warped[ni];
   v.pos=deform.transformPoint(v.pos);const auto normal=skinned?deform:deform.normalMatrix();v.nrm=normalize(direction(normal,v.nrm));const auto tangent=normalize(direction(deform,{v.tangent.x,v.tangent.y,v.tangent.z}));v.tangent={tangent.x,tangent.y,tangent.z,v.tangent.w};
   for(int target=0;target<p.morphTargets;++target){const size_t off=(size_t(target)*p.vertexCount+vi)*6;need(off+6<=p.morphDeltas.size(),"Missing CPU morphs");auto& d=p.morphDeltas;const auto dp=direction(deform,{d[off],d[off+1],d[off+2]}),dn=direction(normal,{d[off+3],d[off+4],d[off+5]});d[off]=dp.x;d[off+1]=dp.y;d[off+2]=dp.z;d[off+3]=dn.x;d[off+4]=dn.y;d[off+5]=dn.z;}
  }}
  }
 }
 for(auto& skin:m.skins)for(size_t j=0;j<skin.joints.size();++j){const size_t n=size_t(skin.joints[j]);skin.inverseBind[j]=next[n].inverse()*old[n]*skin.inverseBind[j];}
 for(auto& clip:m.clips)for(auto& c:clip.channels)if(c.path==AnimChannel::Translation){const auto d=offsets.at(size_t(c.node));const size_t stride=c.interp==AnimChannel::CubicSpline?9:3,start=c.interp==AnimChannel::CubicSpline?3:0;for(size_t k=start;k+2<c.values.size();k+=stride){c.values[k]+=d.x;c.values[k+1]+=d.y;c.values[k+2]+=d.z;}}
 m.finalize();
}
}
void bakeHumanBody(Model& model,const HumanAppearance& look){(void)HumanAppearance::fromJson(look.toJson());body(model,look);}
void validateHumanWardrobe(const Json& r) {
 known(r,{"version","hiddenMeshes","items","sockets"});
 if(r.has("sockets")){
  need(r["sockets"].isArray()&&r["sockets"].size()<=128,"Too many sockets");std::set<std::string> socketNames;
  for(const auto& socket:r["sockets"].items()){
   known(socket,{"name","bone","translation","rotation","scale"});key(socket["name"]);key(socket["bone"]);need(socketNames.insert(socket.getStr("name")).second,"Duplicate socket name");
   vec(socket["translation"],{});vec(socket["rotation"],{});const auto scale=vec(socket["scale"],{1,1,1});need(scale.x>.001f&&scale.y>.001f&&scale.z>.001f,"Socket scale must be positive");
  }
 }need(r["version"].isNumber()&&r["version"].toDouble()==1,"Wardrobe version must be 1");names(r["hiddenMeshes"]);need(r["items"].isArray()&&r["items"].size()<=32,"Wardrobe allows up to 32 items");std::set<std::string> ids,slots;
 for(const auto& item:r["items"].items()){
  known(item,{"id","asset","slot","enabled","bone","translation","rotation","scale","hideMeshes","masks","finish","independentRig","attachmentRule","socket","tint"});
  need(!item.has("tint")||(item["tint"].isArray()&&item["tint"].size()==3&&[&]{for(const auto& c:item["tint"].items())if(!c.isNumber()||!(c.toDouble()>=0&&c.toDouble()<=4))return false;return true;}()),"Clothing tint must be [r,g,b] in 0..4");
  if(item.has("attachmentRule")){need(item.getStr("attachmentRule")=="keep_world"||item.getStr("attachmentRule")=="snap","Invalid attachment rule");need(item.getBool("independentRig"),"Attachment rules require a separate skeleton");}
  if(item.has("socket")){need(item["socket"].isString(),"Invalid socket name");need(item.getStr("socket").empty()||item.getStr("bone").empty(),"Choose bone or socket, not both");}
need(!item.has("independentRig")||item["independentRig"].type()==Json::Type::Bool,"Invalid independent rig flag");need(!item.has("finish")||item.getStr("finish")=="cloth"||item.getStr("finish")=="source","Unknown clothing finish");key(item["id"]);key(item["asset"]);key(item["slot"]);need(ids.insert(item.getStr("id")).second,"Duplicate clothing ID");need((item["enabled"]==Json::fromBool(true)||item["enabled"]==Json::fromBool(false)),"Clothing enabled must be boolean");
  if(item.getBool("enabled"))need(slots.insert(item.getStr("slot")).second,"Choose one clothing item per slot");
  need(item["bone"].isString()&&item.getStr("bone").size()<=256,"Invalid socket bone");vec(item["translation"],{});vec(item["rotation"],{});const auto s=vec(item["scale"],{1,1,1});need(s.x>.001f&&s.y>.001f&&s.z>.001f,"Accessory scale must be positive");names(item["hideMeshes"]);
  need(item["masks"].isArray()&&item["masks"].size()<=16,"Too many body masks");for(const auto& mask:item["masks"].items()){known(mask,{"material","texture"});key(mask["material"]);key(mask["texture"]);}
 }
}
Json inspectHumanWardrobe(const Model& model){auto result=Json::object(),meshes=Json::array(),bones=Json::array(),materials=Json::array();std::set<std::string> names;for(const auto& n:model.nodes){if(n.mesh>=0)names.insert(model.meshes.at(size_t(n.mesh)).name);bones.push(Json::string(n.name));}for(const auto& n:names)meshes.push(Json::string(n));for(const auto& m:model.materials)materials.push(Json::string(m.name));result.set("meshes",meshes);result.set("bones",bones);result.set("materials",materials);return result;}
std::unique_ptr<Model> assembleHumanCharacter(const std::string& base,const FileReader& read,const Json& wardrobe,const HumanAppearance& look,int size,const HumanFaceShape* face){
 validateHumanWardrobe(wardrobe);auto model=loadModel(base,read);need(bool(model),"Cannot import base character");model->finalize();
 // Named sockets become ordinary transform nodes, so standard animation/world
 // traversal and GLB export preserve them without a second attachment clock.
 if(wardrobe.has("sockets"))for(const auto& socket:wardrobe["sockets"].items()){
  const auto name="socket/"+socket.getStr("name"),bone=socket.getStr("bone");need(model->findNode(name)<0,"Socket name collides with model node");int parent=-1;
  for(size_t i=0;i<model->nodes.size();++i)if(model->nodes[i].name==bone){need(parent<0,"Ambiguous socket parent");parent=int(i);}
  need(parent>=0,"Socket parent missing: "+bone);Node n;n.name=name;n.parent=parent;n.t=vec(socket["translation"],{});const auto r=vec(socket["rotation"],{});n.r=quat::axisAngle({0,0,1},r.z)*quat::axisAngle({0,1,0},r.y)*quat::axisAngle({1,0,0},r.x);n.s=vec(socket["scale"],{1,1,1});model->nodes.push_back(n);
 }
 model->finalize();if(face)applyHumanFaceShape(*model,*face);const auto bodyMin=model->boundsMin,bodyMax=model->boundsMax;
 bakeHumanAppearance(*model,look,size);hide(*model,wardrobe["hiddenMeshes"]);
 for(const auto& item:wardrobe["items"].items())if(item.getBool("enabled")){
  hide(*model,item["hideMeshes"]);for(const auto& entry:item["masks"].items())mask(*model,entry,read,size);
  auto part=loadModel(item.getStr("asset"),read);need(bool(part),"Cannot import clothing: "+item.getStr("id"));part->finalize();if(item.getStr("finish")=="cloth"){auto fabric=look;fabric.materials.clear();for(const auto& material:part->materials)fabric.materials[material.name]="cloth";bakeHumanAppearance(*part,fabric,size);}
  if(item.has("tint")){const auto& t=item["tint"];for(auto& material:part->materials){material.baseColor.x*=float(t[0].toDouble());material.baseColor.y*=float(t[1].toDouble());material.baseColor.z*=float(t[2].toDouble());}}
  append(*model,std::move(*part),item);
 }
 prune(*model);need(!model->meshes.empty(),"Character has no visible meshes");model->boundsMin=bodyMin;model->boundsMax=bodyMax;body(*model,look);compactMaterials(*model);model->markGeometryChanged();model->markMaterialChanged();model->finalize();return model;
}
}
