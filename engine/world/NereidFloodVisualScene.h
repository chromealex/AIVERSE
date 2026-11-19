// NEREID Stage 63: dynamic flood/overflow visual layer for WorldScene.
// The authoritative volume is the NEREID WaterFloodResource, not this model.
// One bounded blend/Unlit Model is uploaded through existing gfx::Device and
// drawn through standard Renderer3D, without one draw per flooded cell.
// This is a lightweight temporary water material, not the full NEREID
// refraction/shore-foam shader; ordinary water bodies keep their renderer.
#pragma once
#include <aiverselib/water/NereidFloodRouting.h>
#include <aiverselib/water/NereidGeometry.h>
#include "engine/render/Model.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <compare>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::world {

struct NereidFloodVisualCell {
    int64_t minXcm=0,minZcm=0;
    int32_t gridCm=100;
    int32_t groundCm=0,surfaceCm=0;
    uint32_t velocityCmPerSec=0; // depth tint / future foam; no gameplay
    constexpr auto operator<=>(const NereidFloodVisualCell&) const = default;
};

inline std::vector<NereidFloodVisualCell> collectNereidFloodVisualCells(
        const World& ecs,
        const lib::nereid::WaterFloodResource& flood,
        int64_t minXcm,int64_t minZcm,
        int64_t maxXcm,int64_t maxZcm,
        size_t maxCells=2048u) {
    if(minXcm>=maxXcm || minZcm>=maxZcm ||
       maxCells>4096u || !maxCells)
        throw std::invalid_argument("NEREID flood view area/budget");
    lib::nereid::validateNereidFlood(flood);
    std::vector<NereidFloodVisualCell> out;
    out.reserve(std::min(maxCells,flood.cells.size()));
    for(const auto& cell:flood.cells) {
        if(!cell.volumeCm || !cell.waterBody.valid())continue;
        const int64_t x=flood.originXcm+int64_t(cell.x)*flood.gridCm;
        const int64_t z=flood.originZcm+int64_t(cell.z)*flood.gridCm;
        if(x>=maxXcm || z>=maxZcm ||
           x+flood.gridCm<=minXcm ||
           z+flood.gridCm<=minZcm)
            continue;
        const auto* body=ecs.tryGetConst<lib::nereid::WaterBodyComponent>(
            cell.waterBody);
        if(!body || !lib::nereid::validWaterBody(*body))continue;
        const int64_t surface=int64_t(cell.groundCm)+cell.volumeCm;
        // Original NEREID surface remains authoritative wherever its
        // footprint is already present at the same or higher height.
        // Generate an overlay only for EXTRA flooding or an upstream rise.
        const Vec3 sample{
            Fixed::fromRatio(x+flood.gridCm/2,100),
            Fixed::fromRatio(cell.groundCm,100),
            Fixed::fromRatio(z+flood.gridCm/2,100)};
        const auto* geometry=ecs.tryGetConst<
            lib::nereid::WaterGeometryComponent>(cell.waterBody);
        if(lib::nereid::coversWaterBody(*body,geometry,sample) &&
           surface<=int64_t(body->surfaceY.raw)*100/Fixed::kOne+1)
            continue;
        if(out.size()>=maxCells)break; // deterministic sorted X/Z truncation
        const uint32_t speed=uint32_t(std::min<int64_t>(
            100000,int64_t(std::abs(cell.flowXcmPerSec))+
                   int64_t(std::abs(cell.flowZcmPerSec))));
        out.push_back({x,z,flood.gridCm,cell.groundCm,
                       int32_t(surface),speed});
    }
    std::sort(out.begin(),out.end(),
        [](const auto& a,const auto& b){
            if(a.minXcm!=b.minXcm)return a.minXcm<b.minXcm;
            return a.minZcm<b.minZcm;
        });
    return out;
}

inline std::unique_ptr<render::Model> makeNereidFloodVisualModel(
        const std::vector<NereidFloodVisualCell>& cells,
        int64_t& originXcm,int64_t& originZcm) {
    if(cells.empty())return {};
    if(cells.size()>4096u)
        throw std::length_error("NEREID flood GPU tile budget");
    auto model=std::make_unique<render::Model>();
    model->path="nereid/flood-overflow";
    model->batching.autoLod=false;
    model->batching.allowGeometryPool=false;
    render::Material water;
    water.name="NEREID Flood Overlay";
    water.shading=render::ShadingModel::Unlit;
    water.baseColor={0.08f,0.24f,0.39f,0.76f};
    water.alpha=render::AlphaMode::Blend;
    water.transparent=true;
    water.doubleSided=true;
    water.castShadow=false;
    water.receiveShadow=false;
    water.vertexColors=false;
    model->materials.push_back(std::move(water));
    const auto first=cells.front();
    originXcm=first.minXcm;
    originZcm=first.minZcm;
    model->vertices.reserve(cells.size()*4u);
    model->indices.reserve(cells.size()*6u);
    for(const auto& cell:cells){
        if(cell.gridCm<10 || cell.gridCm>10000 ||
           cell.surfaceCm<=cell.groundCm ||
           cell.surfaceCm<-10000000 || cell.surfaceCm>10000000 ||
           cell.minXcm<-100000000 || cell.minZcm<-100000000 ||
           cell.minXcm>100000000-cell.gridCm ||
           cell.minZcm>100000000-cell.gridCm)
            throw std::invalid_argument("NEREID invalid flood surface cell");
        const float x=float(cell.minXcm-originXcm)*0.01f;
        const float z=float(cell.minZcm-originZcm)*0.01f;
        const float w=float(cell.gridCm)*0.01f;
        const float y=float(cell.surfaceCm)*0.01f;
        const uint32_t base=uint32_t(model->vertices.size());
        for(const render::float3 pos:{
            render::float3{x,y,z},
            render::float3{x+w,y,z},
            render::float3{x+w,y,z+w},
            render::float3{x,y,z+w}
        }){
            render::ModelVertex v{};
            v.pos=pos;
            v.nrm={0,1,0};
            v.uv={pos.x*0.08f,pos.z*0.08f};
            model->vertices.push_back(v);
        }
        for(const uint32_t index:{0u,2u,1u,0u,3u,2u})
            model->indices.push_back(base+index);
    }
    render::Primitive primitive;
    primitive.material=0;
    primitive.firstIndex=0;
    primitive.indexCount=uint32_t(model->indices.size());
    primitive.firstVertex=0;
    primitive.vertexCount=uint32_t(model->vertices.size());
    render::MeshDef mesh;
    mesh.name="Floodwater Grid";
    mesh.primitives.push_back(primitive);
    model->meshes.push_back(std::move(mesh));
    render::Node node;
    node.name="NEREID Floodwater";
    node.mesh=0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    return model;
}

// Device-owned single resident model. Only upload after a changed ECS flood
// snapshot, bounded to 4096 visible cells. On upload failure the previous
// resident mesh stays live; all data are passed as IMMUTABLE copies.
class NereidFloodVisualScene {
public:
    bool setCells(std::vector<NereidFloodVisualCell> cells) {
        if(cells.size()>4096u)
            throw std::length_error("NEREID flood visual source budget");
        if(cells==pending_)return false;
        pending_=std::move(cells);
        dirty_=true;
        return true;
    }
    size_t cells()const{return pending_.size();}
    bool resident()const{return bool(model_);}
    void update(gfx::Device& device) {
        if(!dirty_)return;
        if(pending_.empty()){
            if(model_)render::releaseModel(device,*model_);
            model_.reset();
            dirty_=false;
            return;
        }
        int64_t x=0,z=0;
        auto next=makeNereidFloodVisualModel(pending_,x,z);
        render::prepareModelUpload(*next);
        if(!render::uploadModel(device,*next) ||
           !next->vb.valid() || !next->ib.valid()) {
            render::releaseModel(device,*next);
            return; // retry later without hiding current water
        }
        if(model_)render::releaseModel(device,*model_);
        model_=std::move(next);
        originXcm_=x;originZcm_=z;
        dirty_=false;
    }
    void draw(render::Renderer3D& renderer,
              int64_t originXcm,int64_t originYcm,
              int64_t originZcm)const {
        if(!model_)return;
        render::ModelDraw draw;
        draw.model=model_.get();
        draw.world=render::mat4::translate({
            float(originXcm_-originXcm)*0.01f,
            float(-originYcm)*0.01f,
            float(originZcm_-originZcm)*0.01f});
        draw.flags=render::kDrawTransparent|
                   render::kDrawNoCastShadow;
        renderer.drawModel(draw);
    }
    void shutdown(gfx::Device& device) {
        if(model_)render::releaseModel(device,*model_);
        model_.reset();
        pending_.clear();
        dirty_=false;
    }
private:
    std::vector<NereidFloodVisualCell> pending_;
    std::unique_ptr<render::Model> model_;
    int64_t originXcm_=0,originZcm_=0;
    bool dirty_=false;
};

} // namespace aiverse::world
