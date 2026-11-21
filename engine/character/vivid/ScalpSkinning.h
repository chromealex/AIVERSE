// VIVID CPU reference for deforming authored closed scalp proxies.
// Presentation-only: uses MotionWeave-owned node matrices, no ECS scheduler.
#pragma once
#include "engine/render/Model.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::vivid {
class ScalpSkinning {
public:
    struct Weight {
        std::uint32_t joint=0;
        render::mat4 inverseBind{};
        float weight=0;
    };
    struct Vertex {
        render::float3 bind{};
        std::array<Weight,4> weights{};
        std::uint8_t count=0;
    };

    // sourceIds maps compact proxy vertices to Model::vertices indices.
    // The selected mesh must have exactly one owning skinned node.
    static ScalpSkinning compile(const render::Model& model,
                                 std::size_t meshId,std::size_t nodeId,
                                 std::span<const std::uint32_t> sourceIds) {
        if(meshId>=model.meshes.size() || nodeId>=model.nodes.size() ||
           model.nodes[nodeId].mesh!=int(meshId) ||
           model.nodes[nodeId].skin<0 ||
           std::size_t(model.nodes[nodeId].skin)>=model.skins.size() ||
           sourceIds.empty() || sourceIds.size()>16384)
            throw std::invalid_argument("VIVID scalp skin: invalid mesh/node or vertex count");
        const auto& skin=model.skins[std::size_t(model.nodes[nodeId].skin)];
        if(skin.joints.size()!=skin.inverseBind.size())
            throw std::invalid_argument("VIVID scalp skin: invalid bind matrices");
        ScalpSkinning out;
        out.vertices_.resize(sourceIds.size());
        for(std::size_t i=0;i<sourceIds.size();++i) {
            if(sourceIds[i]>=model.vertices.size())
                throw std::invalid_argument("VIVID scalp skin: vertex outside source stream");
            out.vertices_[i].bind=model.vertices[sourceIds[i]].pos;
        }
        std::vector<bool> assigned(sourceIds.size(),false);
        std::vector<std::int32_t> compact(model.vertices.size(),-1);
        for(std::size_t i=0;i<sourceIds.size();++i) {
            if(compact[sourceIds[i]]>=0)
                throw std::invalid_argument("VIVID scalp skin: repeated compact vertex");
            compact[sourceIds[i]]=std::int32_t(i);
        }
        const auto& mesh=model.meshes[meshId];
        // A source vertex belongs to exactly one skin palette. Visit each
        // referenced triangle index once; never scan the whole index buffer
        // for every vertex of a production-sized scalp proxy.
        for(const auto& primitive:mesh.primitives) {
            if(primitive.jointPalette.empty())
                throw std::invalid_argument("VIVID scalp skin: rigid primitive in skinned proxy");
            const std::size_t first=primitive.firstIndex;
            if(first>model.indices.size() ||
               primitive.indexCount>model.indices.size()-first)
                throw std::invalid_argument("VIVID scalp skin: indices outside model");
            for(std::size_t k=first;k<first+primitive.indexCount;++k) {
                const auto source=model.indices[k];
                if(source>=compact.size())
                    throw std::invalid_argument("VIVID scalp skin: index outside vertices");
                const auto local=compact[source];
                if(local<0)continue;
                // The same vertex may repeat within its own primitive.
                // Cross-primitive sharing with a different palette must
                // be rejected instead of silently mis-skinning the vertex.
                auto& vertex=out.vertices_[std::size_t(local)];
                if(assigned[std::size_t(local)]) {
                    // A shared vertex is only safe if every nonzero
                    // influence still resolves to the same joint nodes.
                    const auto& src=model.vertices[source];
                    std::size_t influence=0;
                    for(int j=0;j<4;++j) {
                        if(!src.weights[j])continue;
                        const auto slot=std::size_t(src.joints[j]);
                        if(slot>=primitive.jointPalette.size())
                            throw std::invalid_argument("VIVID scalp skin: inconsistent palette slot");
                        const auto jointId=std::size_t(primitive.jointPalette[slot]);
                        if(jointId>=skin.joints.size() ||
                           influence>=vertex.count ||
                           skin.joints[jointId]!=int(vertex.weights[influence].joint))
                            throw std::invalid_argument("VIVID scalp skin: conflicting shared vertex palettes");
                        ++influence;
                    }
                    continue;
                }
                const auto& src=model.vertices[source];
                float total=0;
                for(int j=0;j<4;++j) {
                    const auto raw=src.weights[j];
                    if(!raw)continue;
                    const auto slot=std::size_t(src.joints[j]);
                    if(slot>=primitive.jointPalette.size())
                        throw std::invalid_argument("VIVID scalp skin: invalid palette slot");
                    const auto jointId=std::size_t(primitive.jointPalette[slot]);
                    if(jointId>=skin.joints.size() || skin.joints[jointId]<0 ||
                       std::size_t(skin.joints[jointId])>=model.nodes.size())
                        throw std::invalid_argument("VIVID scalp skin: missing joint node");
                    auto& weight=vertex.weights[vertex.count++];
                    weight.joint=std::uint32_t(skin.joints[jointId]);
                    weight.inverseBind=skin.inverseBind[jointId];
                    weight.weight=float(raw);
                    total+=float(raw);
                }
                if(total<=0 || !std::isfinite(total))
                    throw std::invalid_argument("VIVID scalp skin: missing vertex weights");
                for(std::size_t j=0;j<vertex.count;++j)
                    vertex.weights[j].weight/=total;
                assigned[std::size_t(local)]=true;
            }
        }
        for(bool assignedVertex:assigned)
            if(!assignedVertex)
                throw std::invalid_argument("VIVID scalp skin: unassigned source vertex");
        return out;
    }

    std::size_t vertices() const noexcept {return vertices_.size();}

    // Output is in the local rigid-head groom coordinate system.
    // On invalid matrices or output sizes no output element is written.
    bool evaluate(std::span<const render::mat4> nodeWorld,
                  const render::mat4& headRigidWorld,
                  std::span<render::float3> output,
                  std::span<render::float3> scratch) const {
        if(output.size()!=vertices_.size() ||
           scratch.size()!=vertices_.size() ||
           nodeWorld.empty())return false;
        for(float value:headRigidWorld.m)if(!std::isfinite(value))return false;
        for(const auto& vertex:vertices_)
            for(std::size_t j=0;j<vertex.count;++j) {
                const auto& w=vertex.weights[j];
                if(w.joint>=nodeWorld.size())return false;
                for(float value:nodeWorld[w.joint].m)
                    if(!std::isfinite(value))return false;
                for(float value:w.inverseBind.m)
                    if(!std::isfinite(value))return false;
            }
        // Reject singular head frame (mat4::inverse otherwise silently
        // substitutes identity).
        const auto& m=headRigidWorld;
        const render::float3 a{m.at(0,0),m.at(1,0),m.at(2,0)};
        const render::float3 b{m.at(0,1),m.at(1,1),m.at(2,1)};
        const render::float3 c{m.at(0,2),m.at(1,2),m.at(2,2)};
        if(std::fabs(render::dot(a,render::cross(b,c)))<1e-7f)return false;
        const auto inverseHead=headRigidWorld.inverse();
        std::size_t index=0;
        for(const auto& vertex:vertices_) {
            render::float3 position{};
            for(std::size_t j=0;j<vertex.count;++j) {
                const auto& w=vertex.weights[j];
                const auto skinned=(nodeWorld[w.joint]*w.inverseBind)
                                   .transformPoint(vertex.bind);
                position+=skinned*w.weight;
            }
            const auto local=inverseHead.transformPoint(position);
            if(!std::isfinite(local.x)||!std::isfinite(local.y)||
               !std::isfinite(local.z))return false;
            scratch[index++]=local;
        }
        for(std::size_t i=0;i<scratch.size();++i)output[i]=scratch[i];
        return true;
    }
    // Convenience form for standalone tools and tests. Runtime callers
    // should supply a persistent scratch buffer to avoid frame allocations.
    bool evaluate(std::span<const render::mat4> nodeWorld,
                  const render::mat4& headRigidWorld,
                  std::span<render::float3> output) const {
        std::vector<render::float3> scratch(vertices_.size());
        return evaluate(nodeWorld,headRigidWorld,output,scratch);
    }
private:
    std::vector<Vertex> vertices_;
};
} // namespace aiverse::vivid
