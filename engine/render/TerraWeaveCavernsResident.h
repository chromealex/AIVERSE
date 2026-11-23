#pragma once
// Opt-in Caverns native terrain resident. Shared clipped geometry is converted
// once into render and collision payloads; no second terrain rasterizer.
#include "engine/world/TerraWeaveCavernsSurfaceCut.h"
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include <cmath>
#include <memory>
namespace aiverse::terraweave::caverns {
struct CutNativePayload {
    world::Cell cell{};
    uint64_t revision=0;
    std::vector<world::Triangle> collision;
    std::unique_ptr<render::Model> model;
    std::vector<SurfaceRectCm> dirty;
};
inline CutNativePayload buildCutNativePayload(const CutSurface& src,
        int32_t cellSizeMetres,const render::Model* materialSource=nullptr,
        float uvScale=0.05f) {
    if(cellSizeMetres<16||cellSizeMetres>4096||
       !std::isfinite(uvScale)||uvScale<=0||uvScale>16||
       src.triangles.empty()||src.triangles.size()>65534)
        throw std::invalid_argument("Caverns native payload settings");
    const int64_t ox=int64_t(src.cell.x)*cellSizeMetres*100;
    const int64_t oz=int64_t(src.cell.z)*cellSizeMetres*100;
    CutNativePayload out;out.cell=src.cell;out.revision=src.sourceRevision;out.dirty=src.dirtyRegions;
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/caverns/cut-terrain";
    model->batching.autoLod=false;
    model->batching.allowGeometryPool=false;
    if(materialSource) {
        if(materialSource->materials.empty())throw std::invalid_argument("Caverns source material missing");
        model->materials.push_back(materialSource->materials.front());
        for(const auto& image:materialSource->images) {
            if(!image.tex.valid())throw std::invalid_argument("Caverns source texture not uploaded");
            auto copy=image;copy.borrowedTexture=true;copy.textureCache.reset();
            copy.rgba.clear();copy.sharedRgba.reset();copy.compressed.reset();
            model->images.push_back(std::move(copy));
        }
    }else{
        render::Material m;m.name="TerraWeave Caverns Surface";
        m.baseColor={0.30f,0.35f,0.22f,1.f};m.roughness=.9f;m.doubleSided=true;
        model->materials.push_back(std::move(m));
    }
    model->vertices.reserve(src.triangles.size()*3);
    model->indices.reserve(src.triangles.size()*3);
    out.collision.reserve(src.triangles.size());
    auto fixedPoint=[](CutVertexCm p)->Vec3 {
        auto toFixed=[](double cm)->Fixed {
            if(!std::isfinite(cm)||std::abs(cm)>100000000.0)
                throw std::out_of_range("Caverns collision vertex extent");
            return Fixed::fromRatio(int64_t(std::llround(cm)),100);
        };
        return {toFixed(p.x),toFixed(p.y),toFixed(p.z)};
    };
    for(const auto& t:src.triangles){
        const world::Triangle collider{fixedPoint(t.a),fixedPoint(t.b),fixedPoint(t.c)};
        if(!world::SpatialWorld::validTriangle(collider))
            throw std::invalid_argument("Caverns invalid clipped triangle");
        out.collision.push_back(collider);
        const auto p=[&](Vec3 v)->render::float3{
            return {float(v.x.toDouble()-double(ox)/100.),
                    float(v.y.toDouble()),
                    float(v.z.toDouble()-double(oz)/100.)};
        };
        const render::float3 a=p(collider.a),b=p(collider.b),c=p(collider.c);
        const auto normal=render::normalize(render::cross(b-a,c-a));
        for(const auto& v:{a,b,c}) {
            render::ModelVertex mv;mv.pos=v;mv.nrm=normal;
            mv.uv={float(double(ox)/100.+v.x)*uvScale,
                   float(double(oz)/100.+v.z)*uvScale};
            model->indices.push_back(uint32_t(model->vertices.size()));
            model->vertices.push_back(mv);
        }
    }
    render::Primitive primitive;primitive.material=0;primitive.firstIndex=0;
    primitive.indexCount=uint32_t(model->indices.size());primitive.firstVertex=0;
    primitive.vertexCount=uint32_t(model->vertices.size());
    render::MeshDef mesh;mesh.name="Caverns Surface Cut";mesh.primitives.push_back(primitive);
    model->meshes.push_back(std::move(mesh));
    render::Node node;node.name="Caverns Cut Terrain";node.mesh=0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    out.model=std::move(model);
    return out;
}
// A published tile owns both its dedicated model and active collider-ID range.
// New publication MUST use a fresh nonoverlapping collider range: stage new
// colliders before removing old ones. Host reserves IDs across all tiles.
class CutTerrainResident {
public:
    bool resident()const{return bool(model_);}
    uint64_t revision()const{return revision_;}
    world::Cell cell()const{return cell_;}
    const std::vector<SurfaceRectCm>& pendingDirty()const{return dirty_;}
    std::vector<SurfaceRectCm> takeDirty(){
        std::vector<SurfaceRectCm> a; a.swap(dirty_);return a;
    }
    // Call only at graphics/collision owner barrier. No concurrent readers.
    bool publish(gfx::Device& device,world::SpatialWorld& spatial,
                 CutNativePayload&& payload,uint32_t firstColliderId,
                 int32_t cellSizeMetres) {
        if(resident() && cell_!=payload.cell)
            throw std::invalid_argument("Caverns resident tile mismatch");
        if(!payload.model||payload.collision.empty()||!firstColliderId||
           payload.collision.size()>65534||cellSizeMetres<16||
           cellSizeMetres>4096||uint64_t(firstColliderId)+payload.collision.size()>uint64_t(UINT32_MAX)+1)
            throw std::invalid_argument("Caverns resident payload");
        const uint64_t begin=firstColliderId,end=begin+payload.collision.size();
        if(count_ && begin<uint64_t(firstId_)+count_ && uint64_t(firstId_)<end)
            throw std::invalid_argument("Caverns staged collider IDs overlap active range");
        for(uint64_t i=begin;i<end;++i)
            if(spatial.colliders().contains(uint32_t(i)))
                throw std::invalid_argument("Caverns collider ID occupied");
        for(const auto& tri:payload.collision)
            if(!world::SpatialWorld::validTriangle(tri))
                throw std::invalid_argument("Caverns invalid collider geometry");
        std::vector<SurfaceRectCm> nextDirty=dirty_;
        nextDirty.insert(nextDirty.end(),payload.dirty.begin(),payload.dirty.end());
        if(model_&&!device.waitForResourceIdle())return false;
        render::prepareModelUpload(*payload.model);
        if(!render::uploadModel(device,*payload.model)||
           !payload.model->vb.valid()||!payload.model->ib.valid()) {
            render::releaseModel(device,*payload.model);return false;
        }
        uint32_t added=0;
        try {
            for(const auto& tri:payload.collision) {
                if(!spatial.addTriangle(firstColliderId+added,tri))
                    throw std::logic_error("Caverns prevalidated collider failed to insert");
                ++added;
            }
        }catch(...) {
            for(uint32_t i=0;i<added;++i)spatial.remove(firstColliderId+i);
            render::releaseModel(device,*payload.model);
            throw;
        }
        for(uint32_t i=0;i<count_;++i)spatial.remove(firstId_+i);
        if(model_)render::releaseModel(device,*model_);
        model_=std::move(payload.model);
        cell_=payload.cell;revision_=payload.revision;firstId_=firstColliderId;
        count_=added;cellSizeMetres_=cellSizeMetres;
        dirty_.swap(nextDirty);
        return true;
    }
    void submit(render::Renderer3D& renderer,
                terraweave::TerrainPatchRenderOriginCm origin={})const{
        if(!model_)return;
        const int64_t x=int64_t(cell_.x)*cellSizeMetres_*100;
        const int64_t z=int64_t(cell_.z)*cellSizeMetres_*100;
        render::ModelDraw draw;draw.model=model_.get();
        draw.world=render::mat4::translate({
            float(x-origin.x)*.01f,float(-origin.y)*.01f,float(z-origin.z)*.01f});
        draw.flags=render::kDrawStatic;renderer.drawModel(draw);
    }
    void shutdown(gfx::Device& device,world::SpatialWorld& spatial){
        for(uint32_t i=0;i<count_;++i)spatial.remove(firstId_+i);
        if(model_)render::releaseModel(device,*model_);
        model_.reset();firstId_=count_=0;revision_=0;dirty_.clear();
    }
private:
    std::unique_ptr<render::Model> model_;
    world::Cell cell_{};
    uint64_t revision_=0;
    uint32_t firstId_=0,count_=0;
    int32_t cellSizeMetres_=0;
    std::vector<SurfaceRectCm> dirty_;
};
} // namespace aiverse::terraweave::caverns
