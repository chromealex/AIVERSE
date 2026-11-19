// TerraWeave: GPU-owning live terrain patch, compatible with the existing
// render::Model / Renderer3D path. The very same triangles are published to
// SpatialWorld by TerrainCollisionPublisher; there is no second tesselator.
// The caller, never an async worker, owns Device and ECS tick barriers.
#pragma once
#include "engine/world/TerraWeaveTerrainPatch.h"
#include "engine/render/Model.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct TerrainPatchRenderOriginCm {
    int64_t x=0,y=0,z=0;
};

// Optional existing material/texture source must remain GPU-resident
// for the lifetime of the dynamic patch: textures are borrowed, never
// recreated per soil edit. Without a source use an opaque ground material.
inline std::unique_ptr<render::Model> makeEarthworksTerrainModel(
        const TerrainPatch& patch,
        const render::Model* authoredMaterialSource=nullptr,
        float uvScale=0.05f) {
    if(patch.resolution<2 || patch.resolution>129 ||
       patch.cellSizeMetres<16 || patch.cellSizeMetres>4096 ||
       patch.collision.size()!=size_t(patch.resolution-1u)*
                               size_t(patch.resolution-1u)*2u ||
       patch.heightsCm.size()!=size_t(patch.resolution)*patch.resolution ||
       !std::isfinite(uvScale) || uvScale<=0.f || uvScale>16.f)
        throw std::invalid_argument("TerraWeave invalid render patch");
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/dynamic-terrain";
    model->batching.autoLod=false;
    model->batching.allowGeometryPool=false; // mutable and per-tile
    if(authoredMaterialSource) {
        if(authoredMaterialSource->materials.empty())
            throw std::invalid_argument("TerraWeave source has no material");
        model->materials.push_back(authoredMaterialSource->materials.front());
        // Keep texture-slot indices valid; image GPU handles stay borrowed
        // from an externally resident authored model.
        for(const auto& img:authoredMaterialSource->images) {
            if(!img.tex.valid())
                throw std::invalid_argument("TerraWeave source texture not uploaded");
            auto copy=img;
            copy.borrowedTexture=true;
            copy.textureCache.reset(); // no second cache upload/ownership
            copy.rgba.clear();
            copy.sharedRgba.reset();
            copy.compressed.reset();
            model->images.push_back(std::move(copy));
        }
    }else {
        render::Material ground;
        ground.name="TerraWeave Excavated Ground";
        ground.baseColor={0.30f,0.35f,0.22f,1.0f};
        ground.roughness=0.9f;
        ground.doubleSided=true;
        model->materials.push_back(std::move(ground));
    }
    const int64_t cellX=int64_t(patch.cell.x)*patch.cellSizeMetres;
    const int64_t cellZ=int64_t(patch.cell.z)*patch.cellSizeMetres;
    const Vec3 chunkOrigin{Fixed::fromInt(cellX),{},Fixed::fromInt(cellZ)};
    model->vertices.reserve(patch.collision.size()*3u);
    model->indices.reserve(patch.collision.size()*3u);
    const float uvX=float(cellX)*uvScale,uvZ=float(cellZ)*uvScale;
    for(const auto& tri:patch.collision) {
        const auto a=tri.a-chunkOrigin,b=tri.b-chunkOrigin,
                   c=tri.c-chunkOrigin;
        const render::float3 v[3]={
            {float(a.x.toDouble()),float(a.y.toDouble()),float(a.z.toDouble())},
            {float(b.x.toDouble()),float(b.y.toDouble()),float(b.z.toDouble())},
            {float(c.x.toDouble()),float(c.y.toDouble()),float(c.z.toDouble())}
        };
        const auto normal=render::normalize(
            render::cross(v[1]-v[0],v[2]-v[0]));
        for(const auto& p:v) {
            render::ModelVertex vertex;
            vertex.pos=p;
            vertex.nrm=normal;
            vertex.uv={uvX+p.x*uvScale,uvZ+p.z*uvScale};
            model->indices.push_back(
                static_cast<uint32_t>(model->vertices.size()));
            model->vertices.push_back(vertex);
        }
    }
    render::Primitive prim;
    prim.material=0;
    prim.firstIndex=0;
    prim.indexCount=static_cast<uint32_t>(model->indices.size());
    prim.firstVertex=0;
    prim.vertexCount=static_cast<uint32_t>(model->vertices.size());
    render::MeshDef mesh;
    mesh.name="TerraWeave Terrain Patch";
    mesh.primitives.push_back(prim);
    model->meshes.push_back(std::move(mesh));
    render::Node root;
    root.name="TerraWeave Terrain";
    root.mesh=0;
    model->nodes.push_back(std::move(root));
    model->finalize();
    return model;
}

struct TerrainPatchDirtyRegion {
    world::Cell cell{};
    RectCm bounds{};
    uint64_t sourceRevision=0;
    // Every consumer (collision, navmesh, Horizon, vegetation) sees this
    // SAME revision and X/Z rectangle after a successfully published tile.
};

// An opt-in replacement for one authored terrain tile. The host must hide
// the original static terrain draw for this tile while this patch is active;
// it must also avoid registering a second set of original collision triangles.
// Actor colliders are never implicitly removed/reassigned by this adapter.
class TerrainPatchResident {
public:
    bool resident() const {return bool(model_);}
    uint64_t revision() const {return revision_;}
    world::Cell cell() const {return cell_;}
    const std::vector<TerrainPatchDirtyRegion>& pendingDirty() const {
        return dirty_;
    }
    std::vector<TerrainPatchDirtyRegion> takeDirty() {
        std::vector<TerrainPatchDirtyRegion> result;
        result.swap(dirty_);
        return result;
    }

    // Prepare GPU first, then commit collision. If any collision tile or RHI
    // step fails, the old model and old SpatialWorld remain untouched.
    // A successful call swaps both representations on this Device thread.
    bool publish(gfx::Device& device,world::SpatialWorld& spatial,
                 TerrainCollisionPublisher& collisions,
                 const TerrainPatch& patch,uint32_t firstColliderId,
                 const render::Model* materialSource=nullptr,
                 float uvScale=0.05f) {
        if(model_ && !(patch.cell==cell_))
            throw std::invalid_argument("TerraWeave resident owns a different tile");
        auto candidate=makeEarthworksTerrainModel(
            patch,materialSource,uvScale);
        render::prepareModelUpload(*candidate);
        if(!render::uploadModel(device,*candidate) ||
           !candidate->vb.valid() || !candidate->ib.valid()) {
            render::releaseModel(device,*candidate);
            return false;
        }
        // Preallocate the navmesh/vegetation dirty notification BEFORE
        // committing collision. Memory allocation failure must not create
        // geometry/collision revision skew.
        std::vector<TerrainPatchDirtyRegion> nextDirty;
        try {
            nextDirty=dirty_;
            if(nextDirty.size()>=64u)nextDirty.erase(nextDirty.begin());
            const int64_t x=int64_t(patch.cell.x)*
                            patch.cellSizeMetres*100;
            const int64_t z=int64_t(patch.cell.z)*
                            patch.cellSizeMetres*100;
            nextDirty.push_back({patch.cell,{
                x,z,x+int64_t(patch.cellSizeMetres)*100,
                z+int64_t(patch.cellSizeMetres)*100},patch.sourceRevision});
            collisions.publish(spatial,patch,firstColliderId);
        }catch(...){
            render::releaseModel(device,*candidate);
            throw;
        }
        dirty_.swap(nextDirty);
        if(model_)render::releaseModel(device,*model_);
        model_=std::move(candidate);
        cell_=patch.cell;
        cellSizeMetres_=patch.cellSizeMetres;
        revision_=patch.sourceRevision;
        return true;
    }

    void submit(render::Renderer3D& renderer,
                TerrainPatchRenderOriginCm origin={}) const {
        if(!model_)return;
        const int64_t x=int64_t(cell_.x)*cellSizeMetres_*100;
        const int64_t z=int64_t(cell_.z)*cellSizeMetres_*100;
        render::ModelDraw draw;
        draw.model=model_.get();
        draw.world=render::mat4::translate({
            float(x-origin.x)*0.01f,
            float(-origin.y)*0.01f,
            float(z-origin.z)*0.01f
        });
        draw.flags=render::kDrawStatic;
        renderer.drawModel(draw);
    }

    // Must run BEFORE the shared graphics Device is destroyed.
    // The host calls this at chunk unload, map change or game shutdown.
    void shutdown(gfx::Device& device,world::SpatialWorld& spatial,
                  TerrainCollisionPublisher& collisions) {
        if(!model_)return;
        collisions.unload(spatial,cell_);
        render::releaseModel(device,*model_);
        model_.reset();
        revision_=0;
        cellSizeMetres_=0;
        dirty_.clear();
    }
private:
    std::unique_ptr<render::Model> model_;
    world::Cell cell_{};
    int32_t cellSizeMetres_=0;
    uint64_t revision_=0;
    std::vector<TerrainPatchDirtyRegion> dirty_;
};

} // namespace aiverse::terraweave
