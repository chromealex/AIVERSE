// Presentation hierarchy. Resolve once from the current frame, never interpolate children independently.
#pragma once
#include "../../../engine/render/Math3D.h"
#include <map>
#include <cstdint>
#include <set>
namespace aiverse::game {
struct EntityParent { uint64_t parent=0; render::float3 offset{}; float yaw=0; };
struct EntityHierarchy {
 std::map<uint64_t,EntityParent> parents;
 bool set(uint64_t child,const EntityParent& link) {
  uint64_t current=link.parent;
  for(size_t depth=0;depth<=parents.size();++depth) {
   if(current==child)return false;
   auto it=parents.find(current);if(it==parents.end()){parents[child]=link;return true;}
   current=it->second.parent;
  }
  return false;
 }
 // Base frames include the parent's rendered tilt/lift; local effects are applied exactly once.
 std::map<uint64_t,render::mat4> resolve(const std::map<uint64_t,render::mat4>& bases,
                                     const std::map<uint64_t,render::mat4>& effects) const {
  std::map<uint64_t,render::mat4> out;std::set<uint64_t> visiting,failed;
  auto visit=[&](auto&& self,uint64_t id,size_t depth)->bool {
   if(out.count(id))return true;
   if(depth>256||failed.count(id)||!bases.count(id)||!visiting.insert(id).second)return false;
   auto parent=parents.find(id);bool ok=true;
   if(parent==parents.end())out[id]=bases.at(id);
   else if(self(self,parent->second.parent,depth+1)) {
    const auto& p=parent->second;
    auto frame=out.at(p.parent)*render::mat4::translate(p.offset)*render::mat4::rotateY(p.yaw);
    auto fx=effects.find(id);if(fx!=effects.end())frame=frame*fx->second;
    out[id]=frame;
   } else {failed.insert(id);ok=false;}
   visiting.erase(id);return ok;
  };
  for(const auto& [id,p]:parents){(void)p;visit(visit,id,0);}
  return out;
 }
};
}
