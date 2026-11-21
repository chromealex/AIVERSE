// Reusable VIVID editor-only skinning plan. Immutable model topology and
// palette resolution are validated once at candidate publication/first click.
// MotionWeave node matrices remain the single pose authority. This is a CPU
// reference, not a second animation scheduler or authoritative ECS state.
#pragma once
#include "SkinnedWeightPick.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::vivid {

class SculptSkinningPlan final {
    struct Influence {
        std::uint32_t skinJoint=0;
        float weight=0;
    };
    struct Vertex {
        std::array<Influence,4> influences{};
        std::uint8_t count=0;
        bool included=false;
    };
    std::vector<Vertex> vertices_;
    std::vector<render::mat4> transforms_; // reused per joint / click
    std::size_t mesh_=std::numeric_limits<std::size_t>::max();
    std::size_t owner_=std::numeric_limits<std::size_t>::max();
    std::size_t skin_=std::numeric_limits<std::size_t>::max();
    bool ready_=false;
public:
    void clear() noexcept {
        vertices_.clear();
        transforms_.clear();
        mesh_=owner_=skin_=std::numeric_limits<std::size_t>::max();
        ready_=false;
    }
    bool ready() const noexcept {return ready_;}
    std::size_t mesh() const noexcept {return mesh_;}
    std::size_t vertexCount() const noexcept {return vertices_.size();}
    std::size_t jointCount() const noexcept {return transforms_.size();}

    // Must rebuild on a new published model or any in-place topology, skin
    // palette, inverse-bind or weights change. Failures never replace plan.
    bool build(const render::Model& model,std::size_t meshId) {
        using namespace render;
        if(meshId>=model.meshes.size() || model.vertices.empty() ||
           model.vertices.size()>1000000u || model.nodes.empty())return false;
        std::size_t owner=model.nodes.size();
        for(std::size_t i=0;i<model.nodes.size();++i) {
            if(model.nodes[i].mesh!=int(meshId))continue;
            if(owner!=model.nodes.size())return false;
            owner=i;
        }
        if(owner==model.nodes.size())return false;
        const auto skinId=model.nodes[owner].skin;
        const Skin* skin=nullptr;
        if(skinId>=0) {
            if(std::size_t(skinId)>=model.skins.size())return false;
            skin=&model.skins[std::size_t(skinId)];
            if(skin->joints.size()!=skin->inverseBind.size() ||
               skin->joints.size()>65536)return false;
            for(std::size_t i=0;i<skin->joints.size();++i) {
                const int joint=skin->joints[i];
                if(joint<0||std::size_t(joint)>=model.nodes.size())return false;
                for(float v:skin->inverseBind[i].m)
                    if(!std::isfinite(v))return false;
            }
        }
        std::vector<Vertex> proposed(model.vertices.size());
        std::vector<std::size_t> primitiveForVertex(
            model.vertices.size(),std::numeric_limits<std::size_t>::max());
        const auto& primitives=model.meshes[meshId].primitives;
        for(std::size_t p=0;p<primitives.size();++p) {
            const auto& prim=primitives[p];
            const std::size_t begin=prim.firstIndex;
            if(begin>model.indices.size() ||
               prim.indexCount>model.indices.size()-begin ||
               prim.indexCount%3u ||
               (skin!=nullptr)!=!prim.jointPalette.empty())return false;
            for(std::size_t i=begin;i<begin+prim.indexCount;++i) {
                const auto id=model.indices[i];
                if(id>=proposed.size())return false;
                if(primitiveForVertex[id]!=std::numeric_limits<std::size_t>::max()) {
                    // Preserve the existing strict shared-palette policy.
                    if(primitives[primitiveForVertex[id]].jointPalette!=
                       prim.jointPalette)return false;
                }else {
                    primitiveForVertex[id]=p;
                    proposed[id].included=true;
                }
            }
        }
        for(std::size_t id=0;id<proposed.size();++id) {
            auto& dst=proposed[id];
            if(!dst.included)continue;
            const auto& src=model.vertices[id];
            if(!skin)continue;
            const auto& palette=primitives[primitiveForVertex[id]].jointPalette;
            float sum=0.f;
            for(std::size_t i=0;i<4;++i) {
                const auto raw=src.weights[i];
                if(!raw)continue;
                const auto local=std::size_t(src.joints[i]);
                if(local>=palette.size())return false;
                const auto skinJoint=std::size_t(palette[local]);
                if(skinJoint>=skin->joints.size())return false;
                dst.influences[dst.count++]={
                    std::uint32_t(skinJoint),float(raw)/255.f};
                sum+=float(raw)/255.f;
            }
            if(!(sum>0.f)||!std::isfinite(sum))return false;
            for(std::size_t i=0;i<dst.count;++i)
                dst.influences[i].weight/=sum;
        }
        if(std::none_of(proposed.begin(),proposed.end(),
                        [](const Vertex& v){return v.included;}))return false;
        std::vector<mat4> transforms(skin?skin->joints.size():0);
        vertices_.swap(proposed);
        transforms_.swap(transforms);
        mesh_=meshId;
        owner_=owner;
        skin_=skin?std::size_t(skinId):std::numeric_limits<std::size_t>::max();
        ready_=true;
        return true;
    }

    // Scratch and output must not alias. Every result is staged and
    // validated before swapping, so a bad pose never changes last-good
    // world coordinates. The plan must be rebuilt when model data changes.
    bool evaluate(const render::Model& model,
                  std::span<const render::mat4> nodeWorld,
                  std::span<const render::float3> bindPositions,
                  SkinnedWeightSurface& output,
                  SkinnedWeightSurface& staging) {
        using namespace render;
        if(!ready_ || &output==&staging ||
           mesh_>=model.meshes.size() ||
           vertices_.size()!=model.vertices.size() ||
           nodeWorld.size()!=model.nodes.size() ||
           (!bindPositions.empty() && bindPositions.size()!=vertices_.size()))
            return false;
        for(const auto& matrix:nodeWorld)
            for(float value:matrix.m)if(!std::isfinite(value))return false;
        const Skin* skin=nullptr;
        if(skin_!=std::numeric_limits<std::size_t>::max()) {
            if(skin_>=model.skins.size())return false;
            skin=&model.skins[skin_];
            if(skin->joints.size()!=transforms_.size() ||
               skin->inverseBind.size()!=transforms_.size())return false;
            for(std::size_t i=0;i<transforms_.size();++i) {
                const auto joint=skin->joints[i];
                if(joint<0||std::size_t(joint)>=nodeWorld.size())return false;
                transforms_[i]=nodeWorld[std::size_t(joint)]*
                               skin->inverseBind[i];
                for(float v:transforms_[i].m)if(!std::isfinite(v))return false;
            }
        }
        staging.ready=false;
        staging.mesh=mesh_;
        staging.world.resize(vertices_.size());
        staging.included.resize(vertices_.size());
        for(std::size_t i=0;i<vertices_.size();++i) {
            const auto& vertex=vertices_[i];
            staging.included[i]=vertex.included?1:0;
            if(!vertex.included)continue;
            const auto bind=bindPositions.empty()
                ?model.vertices[i].pos:bindPositions[i];
            if(!std::isfinite(bind.x)||!std::isfinite(bind.y)||
               !std::isfinite(bind.z))return false;
            float3 position{};
            if(!skin) {
                position=nodeWorld[owner_].transformPoint(bind);
            }else {
                for(std::size_t j=0;j<vertex.count;++j) {
                    const auto& influence=vertex.influences[j];
                    if(influence.skinJoint>=transforms_.size())return false;
                    position+=transforms_[influence.skinJoint]
                        .transformPoint(bind)*influence.weight;
                }
            }
            if(!std::isfinite(position.x)||!std::isfinite(position.y)||
               !std::isfinite(position.z))return false;
            staging.world[i]=position;
        }
        staging.ready=true;
        std::swap(output,staging);
        return true;
    }
};

} // namespace aiverse::vivid
