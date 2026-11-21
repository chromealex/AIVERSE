// VIVID native skeleton bind translation authoring.
// Stages immutable input into scratch and applies only a validated rebind.
// No gameplay ECS, alternative animation scheduler or GPU calls.
#pragma once
#include "RigAuthoring.h"
#include "engine/render/Model.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace aiverse::vivid {
class BindPoseRebind {
    static bool finite(const render::mat4& m) noexcept {
        for(float value:m.m)if(!std::isfinite(value))return false;
        return true;
    }
    static bool nonsingular(const render::mat4& m) noexcept {
        using namespace render;
        const float3 a{m.at(0,0),m.at(1,0),m.at(2,0)};
        const float3 b{m.at(0,1),m.at(1,1),m.at(2,1)};
        const float3 c{m.at(0,2),m.at(1,2),m.at(2,2)};
        return finite(m)&&std::fabs(dot(a,cross(b,c)))>1e-8f;
    }
public:
    struct Result {
        std::size_t editedNodes=0;
        std::size_t reboundSkins=0;
        std::size_t influencedVertices=0;
    };
    // The new bind frame uses the authored local bone translations.
    // Existing mesh positions are deliberately retained: recomputing
    // inverseBind ensures the authored rest pose remains un-deformed,
    // while future MotionWeave animation uses the new skeleton bind frame.
    // This is *skeleton skin rebinding*, not bone-weight painting or
    // facial geometry sculpting. Morph target vertex IDs remain unchanged.
    static Result apply(render::Model& model,const RigAuthoring& edits) {
        using namespace render;
        if(model.nodes.empty() || model.nodes.size()>65536 ||
           model.skins.size()>4096 || model.vb.valid() ||
           model.ib.valid() || model.geometryPoolPage)
            throw std::invalid_argument(
                "VIVID bind rebind: requires pre-upload owned CPU model");
        std::vector<float3> translated;
        translated.reserve(model.nodes.size());
        for(const auto& node:model.nodes)translated.push_back(node.t);
        Result stats;
        for(const auto& [name,offset]:edits.edits()) {
            const int index=model.findNode(name);
            if(index<0)
                throw std::invalid_argument("VIVID bind rebind: missing authored bone");
            auto& v=translated[std::size_t(index)];
            v=v+offset;
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
                throw std::invalid_argument("VIVID bind rebind: invalid local transform");
            ++stats.editedNodes;
        }
        // Build hierarchy from parent links, independent of potentially
        // stale model.order. Fail closed for cycles / invalid parents.
        std::vector<mat4> world(model.nodes.size());
        std::vector<mat4> originalWorld(model.nodes.size());
        std::vector<std::uint8_t> state(model.nodes.size());
        const auto solve=[&](auto&& self,std::size_t i)->void {
            if(state[i]==2)return;
            if(state[i]==1)
                throw std::invalid_argument("VIVID bind rebind: cyclic hierarchy");
            state[i]=1;
            const auto& node=model.nodes[i];
            const auto local=mat4::trs(translated[i],node.r,node.s);
            const auto oldLocal=mat4::trs(node.t,node.r,node.s);
            if(!nonsingular(local) || !nonsingular(oldLocal))
                throw std::invalid_argument("VIVID bind rebind: singular local matrix");
            if(node.parent>=0) {
                if(std::size_t(node.parent)>=model.nodes.size())
                    throw std::invalid_argument("VIVID bind rebind: unknown parent");
                self(self,std::size_t(node.parent));
                world[i]=world[std::size_t(node.parent)]*local;
                originalWorld[i]=originalWorld[std::size_t(node.parent)]*oldLocal;
            }else {
                world[i]=local;
                originalWorld[i]=oldLocal;
            }
            if(!nonsingular(world[i]) || !nonsingular(originalWorld[i]))
                throw std::invalid_argument("VIVID bind rebind: singular world matrix");
            state[i]=2;
        };
        for(std::size_t i=0;i<model.nodes.size();++i)solve(solve,i);
        if(model.skins.empty())
            throw std::invalid_argument("VIVID bind rebind: no skinned skeleton to rebind");
        std::vector<std::vector<mat4>> newInverse;
        newInverse.reserve(model.skins.size());
        for(const auto& skin:model.skins) {
            if(skin.joints.size()!=skin.inverseBind.size())
                throw std::invalid_argument("VIVID bind rebind: invalid skin joint/matrix counts");
            std::vector<mat4> matrices;
            matrices.reserve(skin.joints.size());
            for(std::size_t j=0;j<skin.joints.size();++j) {
                const auto id=skin.joints[j];
                if(id<0||std::size_t(id)>=world.size() ||
                   !nonsingular(skin.inverseBind[j]))
                    throw std::invalid_argument("VIVID bind rebind: invalid skin joint");
                // Preserve the original mesh binding frame, including
                // glTF skin roots and non-identity mesh bind transforms:
                // newJoint * newIB = oldJoint * oldIB.
                matrices.push_back(
                    world[std::size_t(id)].inverse() *
                    originalWorld[std::size_t(id)] *
                    skin.inverseBind[j]);
                if(!finite(matrices.back()))
                    throw std::invalid_argument("VIVID bind rebind: invalid inverse bind");
            }
            newInverse.push_back(std::move(matrices));
            ++stats.reboundSkins;
        }
        // Validate every influenced vertex and palette (including shared
        // vertices across primitive ranges) before any model mutation.
        std::vector<bool> influenced(model.vertices.size(),false);
        for(const auto& node:model.nodes) {
            if(node.skin<0)continue;
            if(std::size_t(node.skin)>=model.skins.size() ||
               node.mesh<0||std::size_t(node.mesh)>=model.meshes.size())
                throw std::invalid_argument("VIVID bind rebind: invalid skinned node");
            const auto& skin=model.skins[std::size_t(node.skin)];
            for(const auto& primitive:model.meshes[std::size_t(node.mesh)].primitives) {
                if(primitive.jointPalette.empty())
                    throw std::invalid_argument("VIVID bind rebind: missing joint palette");
                const auto first=std::size_t(primitive.firstIndex);
                if(first>model.indices.size() ||
                   std::size_t(primitive.indexCount)>model.indices.size()-first)
                    throw std::invalid_argument("VIVID bind rebind: index range invalid");
                for(std::size_t i=first;i<first+primitive.indexCount;++i) {
                    const auto id=model.indices[i];
                    if(id>=model.vertices.size())
                        throw std::invalid_argument("VIVID bind rebind: vertex ID invalid");
                    const auto& v=model.vertices[id];
                    unsigned total=0;
                    for(unsigned k=0;k<4;++k) {
                        if(v.weights[k]==0)continue;
                        if(v.joints[k]>=primitive.jointPalette.size() ||
                           primitive.jointPalette[v.joints[k]]>=skin.joints.size())
                            throw std::invalid_argument("VIVID bind rebind: palette weight out of range");
                        total+=v.weights[k];
                    }
                    if(!total)throw std::invalid_argument("VIVID bind rebind: no vertex influences");
                    if(!influenced[id]){influenced[id]=true;++stats.influencedVertices;}
                }
            }
        }
        // All calculations succeeded; the only publications below are
        // non-allocating assignments/moves, leaving input transactional.
        for(std::size_t i=0;i<model.nodes.size();++i)
            model.nodes[i].t=translated[i];
        model.bindWorld=std::move(world);
        for(std::size_t i=0;i<model.skins.size();++i)
            model.skins[i].inverseBind=std::move(newInverse[i]);
        return stats;
    }
};
} // namespace aiverse::vivid
