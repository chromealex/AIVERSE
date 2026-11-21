#pragma once
#include "StudioHumanLookDocument.h"
#include "StudioContentDirectory.h"
#include <algorithm>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include "engine/character/vivid/HumanWardrobe.h"
#include "engine/character/vivid/HumanLod.h"
#include "tools/modeling/Modeling.h"
#include <sys/stat.h>
namespace aiverse::studio {
inline render::FileReader humanCharacterReader(const std::filesystem::path& content) {
 return [root=std::filesystem::weakly_canonical(content)](const std::string& key,std::string& out){
  namespace fs=std::filesystem;out.clear();const fs::path relative(key);if(relative.empty()||relative.is_absolute())return false;
  fs::path path=root;for(const auto& part:relative){if(part==".."||part=="."||part.empty())return false;path/=part;std::error_code ec;auto s=fs::symlink_status(path,ec);if(ec||fs::is_symlink(s))return false;}
  std::error_code ec;if(!fs::is_regular_file(path,ec)||ec||fs::file_size(path,ec)>128u*1024u*1024u||ec)return false;
  std::ifstream in(path,std::ios::binary);if(!in)return false;out.assign(std::istreambuf_iterator<char>(in),{});return bool(in)||in.eof();
 };
}
// Game budget of an assembled character: triangles per LOD level (a mesh
// without that level keeps drawing its finest available one).
inline Json studioHumanLodTriangles(const render::Model& m) {
 auto tris=[&](int mesh){size_t t=0;if(mesh<0||size_t(mesh)>=m.meshes.size())return t;for(const auto& p:m.meshes[size_t(mesh)].primitives)t+=p.indexCount/3;return t;};
 std::vector<int> base;for(const auto& n:m.nodes)if(n.mesh>=0)base.push_back(n.mesh);
 size_t levels=1;for(int b:base)levels=std::max(levels,m.meshes.at(size_t(b)).lods.size()+1);levels=std::min<size_t>(levels,6);
 auto out=Json::array();
 for(size_t k=0;k<levels;++k){size_t t=0;for(int b:base){const auto& lods=m.meshes.at(size_t(b)).lods;int mesh=b;for(size_t i=0;i<k&&i<lods.size();++i)if(lods[i].mesh>=0)mesh=lods[i].mesh;t+=tris(mesh);}out.push(Json::number(int64_t(t)));}
 return out;
}
inline Json buildStudioHumanCharacter(const std::string& project,const std::string& source,
 const Json& wardrobe,const vivid::HumanAppearance& look,const std::string& name,int size,const Json& editable) {
 namespace fs=std::filesystem;
 if(!validStudioHumanLookName(name))throw std::invalid_argument("Character name: use 1..64 letters, digits, _ or -");
 if(size!=256&&size!=512&&size!=1024&&size!=2048&&size!=4096)throw std::invalid_argument("Unsupported texture size");
 const auto content=studioContentRoot(project);if(fs::is_symlink(fs::symlink_status(content)))throw std::runtime_error("Content directory cannot be a symlink");
 const auto reader=humanCharacterReader(content);
 std::optional<vivid::HumanFaceShape> face;if(editable.has("face")&&!editable["face"].isNull())face=vivid::HumanFaceShape::fromJson(editable["face"]);
 auto model=vivid::assembleHumanCharacter(source,reader,wardrobe,look,size,face?&*face:nullptr);
 (void)vivid::generateHumanCharacterLods(*model); // index-only LODs for skinned parts lacking a chain
 const auto bytes=modeling::exportGlb(*model);
 if(bytes.size()>128u*1024u*1024u)throw std::runtime_error("Character GLB exceeds 128 MiB import budget; reduce texture resolution or source maps");
 auto check=render::loadModel("character.glb",[&](const std::string& key,std::string& out){if(key!="character.glb")return false;out=bytes;return true;});
 if(!check||check->meshes.size()!=model->meshes.size()||check->skins.size()!=model->skins.size())throw std::runtime_error("Assembled GLB roundtrip failed");
 for(size_t i=0;i<check->meshes.size();++i)if(check->meshes[i].morphTargetNames!=model->meshes[i].morphTargetNames)throw std::runtime_error("Assembled facial semantics changed");
 const int root=::open(content.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(root<0)throw std::runtime_error("Cannot open content directory");
 if(::mkdirat(root,"Characters",0700)!=0&&errno!=EEXIST){::close(root);throw std::runtime_error("Cannot create Characters directory");}
 const int folder=::openat(root,"Characters",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);::close(root);if(folder<0)throw std::runtime_error("Unsafe Characters directory");
 const std::string pending="."+name+".pending";
 struct stat stat{};if(::fstatat(folder,name.c_str(),&stat,AT_SYMLINK_NOFOLLOW)==0){::close(folder);throw std::runtime_error("Character already exists; choose a new name");}
 if(::mkdirat(folder,pending.c_str(),0700)!=0){::close(folder);throw std::runtime_error("Character build staging already exists");}
 const int dir=::openat(folder,pending.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
 if(dir<0){::unlinkat(folder,pending.c_str(),AT_REMOVEDIR);::close(folder);throw std::runtime_error("Cannot open staging directory");}
 auto write=[&](const char* file,const std::string& data){int fd=::openat(dir,file,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)throw std::runtime_error("Cannot create character output");size_t offset=0;while(offset<data.size()){auto n=::write(fd,data.data()+offset,data.size()-offset);if(n<=0){::close(fd);throw std::runtime_error("Character write failed");}offset+=size_t(n);}const int synced=::fsync(fd);const int closed=::close(fd);if(synced||closed)throw std::runtime_error("Character file flush failed");};
 try {
  write("model.glb",bytes);write("source.json",editable.dump(2));write("appearance.json",vivid::HumanAppearance{}.toJson().dump(2));
  if(::fsync(dir)!=0)throw std::runtime_error("Character staging flush failed");
#ifdef __APPLE__
  if(::renameatx_np(folder,pending.c_str(),folder,name.c_str(),RENAME_EXCL)!=0)throw std::runtime_error("Cannot publish character without replacing an existing asset");
#else
  if(::fstatat(folder,name.c_str(),&stat,AT_SYMLINK_NOFOLLOW)==0||::renameat(folder,pending.c_str(),folder,name.c_str())!=0)throw std::runtime_error("Cannot publish character");
#endif
  (void)::fsync(folder);::close(dir);::close(folder);
 }catch(...){for(auto file:{"model.glb","source.json","appearance.json"})::unlinkat(dir,file,0);::close(dir);::unlinkat(folder,pending.c_str(),AT_REMOVEDIR);::close(folder);throw;}
 auto result=Json::object();result.set("running",Json::fromBool(false));result.set("path",Json::string("Characters/"+name+"/model.glb"));result.set("vertices",Json::number(int64_t(check->vertices.size())));result.set("meshes",Json::number(int64_t(check->meshes.size())));result.set("materials",Json::number(int64_t(check->materials.size())));result.set("bytes",Json::number(int64_t(bytes.size())));result.set("lodTriangles",studioHumanLodTriangles(*check));result.set("textureSize",Json::number(int64_t(size)));return result;
}
}
