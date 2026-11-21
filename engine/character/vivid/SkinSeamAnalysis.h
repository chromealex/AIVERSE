// Detect shared skin vertices whose primitive palettes interpret the same
// 8-bit vertex joint slots differently. Read-only; no morph/LOD remapping.
#pragma once
#include "engine/render/Model.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace aiverse::vivid {
struct SkinSeamConflict {
    std::uint32_t vertex=0;
    std::size_t firstMesh=0,firstPrimitive=0;
    std::size_t otherMesh=0,otherPrimitive=0;
};
class SkinSeamAnalysis final {
public:
    static std::vector<SkinSeamConflict> detect(
        const render::Model& model,std::size_t maxConflicts=65536) {
        struct Owner {
            std::size_t skin=0,mesh=0,primitive=0;
            std::vector<int> resolved;
        };
        std::map<std::uint32_t,Owner> visited;
        std::vector<SkinSeamConflict> conflicts;
        for(const auto& node:model.nodes) {
            if(node.mesh<0||node.skin<0)continue;
            if(std::size_t(node.mesh)>=model.meshes.size() ||
               std::size_t(node.skin)>=model.skins.size())
                throw std::invalid_argument("VIVID skin seam: invalid node skin/mesh");
            const auto meshId=std::size_t(node.mesh);
            const auto skinId=std::size_t(node.skin);
            const auto& skin=model.skins[skinId];
            const auto& mesh=model.meshes[meshId];
            for(std::size_t p=0;p<mesh.primitives.size();++p) {
                const auto& prim=mesh.primitives[p];
                if(prim.jointPalette.empty())continue;
                const std::size_t first=prim.firstIndex;
                if(first>model.indices.size() ||
                   prim.indexCount>model.indices.size()-first)
                    throw std::invalid_argument("VIVID skin seam: primitive out of bounds");
                std::vector<int> resolved;
                resolved.reserve(prim.jointPalette.size());
                for(auto joint:prim.jointPalette) {
                    if(joint>=skin.joints.size())
                        throw std::invalid_argument("VIVID skin seam: invalid palette joint");
                    resolved.push_back(skin.joints[joint]);
                }
                for(std::size_t i=first;i<first+prim.indexCount;++i) {
                    const auto id=model.indices[i];
                    if(id>=model.vertices.size())
                        throw std::invalid_argument("VIVID skin seam: invalid vertex");
                    auto [it,inserted]=visited.try_emplace(
                        id,Owner{skinId,meshId,p,resolved});
                    if(!inserted && (it->second.skin!=skinId ||
                                     it->second.resolved!=resolved)) {
                        if(conflicts.size()>=maxConflicts)
                            throw std::invalid_argument("VIVID skin seam: conflict budget exceeded");
                        conflicts.push_back({id,it->second.mesh,
                            it->second.primitive,meshId,p});
                    }
                }
            }
        }
        std::sort(conflicts.begin(),conflicts.end(),
                  [](const auto& a,const auto& b){
                      if(a.vertex!=b.vertex)return a.vertex<b.vertex;
                      if(a.otherMesh!=b.otherMesh)return a.otherMesh<b.otherMesh;
                      return a.otherPrimitive<b.otherPrimitive;
                  });
        conflicts.erase(std::unique(conflicts.begin(),conflicts.end(),
                       [](const auto& a,const auto& b){
                           return a.vertex==b.vertex &&
                                  a.otherMesh==b.otherMesh &&
                                  a.otherPrimitive==b.otherPrimitive;
                       }),conflicts.end());
        return conflicts;
    }
};
} // namespace aiverse::vivid
