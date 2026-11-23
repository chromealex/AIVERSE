// MORPH -> actual Renderer3D ModelDraw vertex stream.
// This is an optional native presentation adapter, deliberately outside
// engine/ and libraries/ to preserve gameplay/render dependency direction.
// Reuses VIVID's cross-RHI ModelVertex override path, not a second renderer.
#pragma once
#include "aiverselib/world/WorldProcessesQueries.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelVertexStreamOverride.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::examples::world_processes_native {

struct MorphVertexBinding {
    std::uint32_t vertex=0;  // index into the actual ModelVertex GPU stream
    std::array<Entity,4> node{};
    // 65535 = full influence. Sum of active weights must equal 65535.
    std::array<std::uint16_t,4> weight{};
};

struct MorphModelStreamConfig {
    // One quantized MorphNode displacement unit in metres (e.g., 0.001 = mm).
    float metresPerUnit=0.001f;
    float maxDisplacementMetres=5.f;
    bool recomputeNormals=true;
};

class MorphModelStream {
public:
    static constexpr std::size_t kMaxVertices=1000000;
    static constexpr std::size_t kMaxBindings=1000000;

    bool initialize(gfx::Device& device,const render::Model& model,
                    std::span<const render::ModelVertex> pristine,
                    std::span<const std::uint32_t> indices,
                    std::span<const MorphVertexBinding> influences,
                    MorphModelStreamConfig config={}) {
        if(device_||pristine.empty()||pristine.size()>kMaxVertices||
           indices.empty()||indices.size()%3||influences.empty()||
           influences.size()>kMaxBindings||
           !std::isfinite(config.metresPerUnit)||config.metresPerUnit<=0||
           !std::isfinite(config.maxDisplacementMetres)||
           config.maxDisplacementMetres<=0||config.maxDisplacementMetres>1000||
           !model.vb.valid()||!model.ib.valid()||
           model.batching.allowGeometryPool||
           render::modelVertexOverrideStatus(model,model.vb,
                   static_cast<std::uint32_t>(pristine.size()))!=
               render::ModelVertexOverrideStatus::Ready) {
            error_="MORPH: invalid model, dedicated geometry or influence budget";
            return false;
        }
        for(auto index:indices)if(index>=pristine.size()){
            error_="MORPH: index outside source vertex buffer";return false;
        }
        std::vector<std::uint8_t> used(pristine.size(),0);
        for(const auto& influence:influences){
            if(influence.vertex>=pristine.size()||used[influence.vertex]++){
                error_="MORPH: duplicate or out-of-bounds vertex binding";return false;
            }
            std::uint32_t total=0;
            for(std::size_t j=0;j<4;++j){
                const auto weight=influence.weight[j];
                if(!weight)continue;
                if(!influence.node[j].valid()){
                    error_="MORPH: influence without Entity";return false;
                }
                for(std::size_t k=0;k<j;++k)
                    if(influence.weight[k]&&
                       influence.node[k]==influence.node[j]){
                        error_="MORPH: repeated influence Entity";return false;
                    }
                total+=weight;
            }
            if(total!=65535){
                error_="MORPH: vertex weights must sum to 65535";return false;
            }
        }
        // VIVID uses precisely this dedicated, full 60B vertex stream:
        // snapshotUpdates prevents overwriting in-flight GPU frames.
        std::vector<render::ModelVertex> base(pristine.begin(),pristine.end());
        std::vector<render::ModelVertex> output=base;
        std::vector<std::uint32_t> triangles(indices.begin(),indices.end());
        std::vector<MorphVertexBinding> links(influences.begin(),influences.end());
        gfx::BufferDesc desc{};
        desc.type=gfx::BufferType::Vertex;
        desc.usage=gfx::Usage::Dynamic;
        desc.snapshotUpdates=true;
        desc.size=base.size()*sizeof(render::ModelVertex);
        desc.data=base.data();
        desc.label="MORPH per-instance deformable ModelVertex stream";
        const auto buffer=device.createBuffer(desc);
        if(!buffer.valid()){
            error_="MORPH: dynamic RHI vertex stream allocation failed";
            return false;
        }
        base_=std::move(base);
        output_=std::move(output);
        indices_=std::move(triangles);
        bindings_=std::move(links);
        device_=&device;model_=&model;buffer_=buffer;
        config_=config;modelRevision_=model.geometryRevision;
        lastFrame_=0;lastOffset_=0;published_=false;error_.clear();
        return true;
    }

    bool update(const World& world) {
        if(!ready()||!device_->gpuHealthy()||
           !device_->canUpdateSharedBuffers()||
           model_->geometryRevision!=modelRevision_||
           render::modelVertexOverrideStatus(*model_,buffer_,
                static_cast<std::uint32_t>(base_.size()))!=
                render::ModelVertexOverrideStatus::Ready){
            error_="MORPH: missing GPU owner, stale model or incompatible stream";
            return false;
        }
        const std::uint64_t frame=device_->frameSerial();
        if(frame&&frame==lastFrame_){
            error_="MORPH: duplicate update in one GPU frame";
            return false;
        }
        auto next=base_;
        float largest=0;
        for(const auto& bind:bindings_){
            render::float3 delta{};
            for(std::size_t i=0;i<4;++i){
                if(!bind.weight[i])continue;
                const auto* n=world.tryGetConst<morph::Node>(bind.node[i]);
                if(!n||!n->enabled||!morph::valid(*n)){
                    error_="MORPH: missing, stale or invalid ECS influence";
                    return false;
                }
                const float factor=(float(bind.weight[i])/65535.f)*
                                   config_.metresPerUnit;
                delta.x+=float(n->x-n->restX)*factor;
                delta.y+=float(n->y-n->restY)*factor;
                delta.z+=float(n->z-n->restZ)*factor;
            }
            const float displacement=render::length(delta);
            if(!std::isfinite(displacement)||
               displacement>config_.maxDisplacementMetres){
                error_="MORPH: vertex displacement exceeds renderer budget";
                return false;
            }
            largest=std::max(largest,displacement);
            next[bind.vertex].pos+=delta;
        }
        if(config_.recomputeNormals){
            std::vector<render::float3> sums(next.size());
            for(std::size_t i=0;i<indices_.size();i+=3){
                const auto a=indices_[i],b=indices_[i+1],c=indices_[i+2];
                const auto normal=render::cross(
                    next[b].pos-next[a].pos,next[c].pos-next[a].pos);
                sums[a]+=normal;sums[b]+=normal;sums[c]+=normal;
            }
            for(std::size_t i=0;i<next.size();++i){
                const auto magnitude=render::length(sums[i]);
                if(!std::isfinite(magnitude)){
                    error_="MORPH: malformed deformed triangle normal";
                    return false;
                }
                if(magnitude>1e-7f)next[i].nrm=sums[i]*(1.f/magnitude);
                // Gram-Schmidt keeps the source texture tangent consistent
                // with the modified normal without rewriting the UV layout.
                auto& tangent=next[i].tangent;
                render::float3 t{tangent.x,tangent.y,tangent.z};
                const auto normal=next[i].nrm;
                t=t-normal*render::dot(normal,t);
                if(render::length(t)>1e-7f){
                    t=render::normalize(t);
                    tangent.x=t.x;tangent.y=t.y;tangent.z=t.z;
                }
            }
        }
        device_->updateBuffer(buffer_,next.data(),
                              next.size()*sizeof(render::ModelVertex));
        output_.swap(next);
        published_=true;lastFrame_=frame;lastOffset_=largest;error_.clear();
        return true;
    }

    // Attach immediately after update() and before drawModel/render. The
    // renderer preserves the normal PBR, skin, morph, shadow and outline
    // passes; only this instance's vertex stream is replaced.
    bool attach(render::ModelDraw& draw)const {
        if(!ready()||!published_||draw.model!=model_||
           draw.vertexOverride.valid()||draw.vertexOverrideCount||
           (device_->frameSerial()&&device_->frameSerial()!=lastFrame_))return false;
        draw.vertexOverride=buffer_;
        draw.vertexOverrideCount=static_cast<std::uint32_t>(output_.size());
        draw.maxVertexOffsetMetres=std::max(draw.maxVertexOffsetMetres,lastOffset_);
        return true;
    }
    bool ready()const noexcept{return device_&&model_&&buffer_.valid();}
    float maximumOffsetMetres()const noexcept{return lastOffset_;}
    const std::string& lastError()const noexcept{return error_;}
    // The owner MUST call at a completed GPU lifetime boundary and after
    // any ModelDraw packets referencing the old buffer are retired.
    bool shutdownAfterIdle(){
        if(!device_)return true;
        if(!device_->waitForResourceIdle())return false;
        device_->destroyBuffer(buffer_);
        device_=nullptr;model_=nullptr;buffer_={};
        base_.clear();output_.clear();indices_.clear();bindings_.clear();
        published_=false;lastFrame_=0;lastOffset_=0;
        return true;
    }
private:
    gfx::Device* device_=nullptr;
    const render::Model* model_=nullptr;
    gfx::BufferHandle buffer_{};
    MorphModelStreamConfig config_{};
    std::uint64_t modelRevision_=0,lastFrame_=0;
    float lastOffset_=0;
    bool published_=false;
    std::vector<render::ModelVertex> base_,output_;
    std::vector<std::uint32_t> indices_;
    std::vector<MorphVertexBinding> bindings_;
    std::string error_;
};
} // namespace aiverse::examples::world_processes_native
