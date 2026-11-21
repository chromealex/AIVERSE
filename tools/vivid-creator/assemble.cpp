#include "apps/studio-native/StudioHumanCharacterBuild.h"
#include <iostream>
int main(int argc,char** argv){try{
 if(argc==6&&std::string(argv[1])=="--compare-body") {
  using namespace aiverse;using namespace render;
  const auto reader=studio::humanCharacterReader(std::filesystem::path(argv[2])/"content");
  auto source=loadModel(argv[3],reader),baked=loadModel(argv[4],reader);
  const auto look=vivid::HumanAppearance::fromJson(Json::parseFile(argv[5]));
  auto expected=source->bindWorld;vivid::applyHumanBody(*source,look,expected);
  float maxDistance=0,minNormalDot=1;double sum=0;size_t count=0;
  auto vertex=[](const Model& m,const std::vector<mat4>& world,const Node& node,const Primitive& p,uint32_t id){const auto& v=m.vertices.at(id);mat4 transform=world.at(size_t(&node-m.nodes.data()));
   if(node.skin>=0&&!p.jointPalette.empty()){for(auto& x:transform.m)x=0;const auto& skin=m.skins.at(size_t(node.skin));for(int k=0;k<4;++k)if(v.weights[k]){const size_t j=p.jointPalette.at(v.joints[k]);auto matrix=world.at(size_t(skin.joints.at(j)))*skin.inverseBind.at(j);const float total=float(v.weights[0])+v.weights[1]+v.weights[2]+v.weights[3];for(int c=0;c<16;++c)transform.m[c]+=matrix.m[c]*v.weights[k]/total;}}
   return std::pair{transform.transformPoint(v.pos),normalize(transform.transformDir(v.nrm))};};
  for(const auto& node:source->nodes)if(node.mesh>=0){const auto& other=baked->nodes.at(size_t(baked->findNode(node.name)));const auto& a=source->meshes.at(size_t(node.mesh));const auto& b=baked->meshes.at(size_t(other.mesh));if(a.primitives.size()!=b.primitives.size())throw std::runtime_error("Primitive counts differ");
   for(size_t pi=0;pi<a.primitives.size();++pi){const auto& pa=a.primitives[pi];const auto& pb=b.primitives[pi];if(pa.indexCount!=pb.indexCount)throw std::runtime_error("Index counts differ");for(uint32_t i=0;i<pa.indexCount;i+=3){auto va=vertex(*source,expected,node,pa,source->indices.at(pa.firstIndex+i));auto vb=vertex(*baked,baked->bindWorld,other,pb,baked->indices.at(pb.firstIndex+i));const float d=length(va.first-vb.first);maxDistance=std::max(maxDistance,d);sum+=d*d;minNormalDot=std::min(minNormalDot,dot(va.second,vb.second));++count;}}}
  std::cout<<"samples="<<count<<" maxPositionError="<<maxDistance<<" rms="<<std::sqrt(sum/count)<<" minNormalDot="<<minNormalDot<<'\n';return maxDistance<.001f?0:1;
 }

 if(argc!=7&&argc!=8){std::cerr<<"usage: vivid_assemble PROJECT SOURCE_REL WARDROBE_JSON LOOK_JSON NAME RESOLUTION [FACE_JSON]\n";return 2;}
 const auto recipe=aiverse::Json::parseFile(argv[3]);const auto look=aiverse::vivid::HumanAppearance::fromJson(aiverse::Json::parseFile(argv[4]));
 auto editable=aiverse::Json::object();editable.set("source",aiverse::Json::string(argv[2]));editable.set("wardrobe",recipe);editable.set("look",look.toJson());
 if(argc==8)editable.set("face",aiverse::Json::parseFile(argv[7]));
 const auto result=aiverse::studio::buildStudioHumanCharacter(argv[1],argv[2],recipe,look,argv[5],std::stoi(argv[6]),editable);
 std::cout<<result.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
