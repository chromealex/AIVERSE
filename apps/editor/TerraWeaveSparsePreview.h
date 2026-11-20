// Scene-view preview of the SAME sparse TerraWeave chunks edited through
// Workspace / MCP / HTML5. One shared CPU Earthworks height source, CPU
// camera LOD plan, GPU models through ordinary Renderer3D. No ECS, physics,
// collision/navmesh ownership. Old Models enter Editor's GPU retirement queue.
#pragma once
#include "apps/editor/core/TerraWeaveSparseOps.h"
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::editor {
class TerraWeaveSparsePreview {
public:
    using Retire=std::function<void(std::unique_ptr<render::Model>&)>;
    size_t residentCount() const{return models_.size();}
    size_t authoredCount()const{return authoring_?authoring_->chunkCount():0;}

    void release(const Retire& retire) {
        if(!retire)throw std::invalid_argument("TerraWeave preview missing GPU retirement owner");
        for(auto& [key,model]:models_){(void)key;retire(model.gpu);}
        models_.clear();
        authoring_.reset();
        earth_.reset();
        cachedRevision_=std::numeric_limits<uint64_t>::max();
    }

    // Editor::shutdown already waits for a real GPU idle barrier. After it,
    // synchronous model cleanup is safe, even if retirement scheduling is
    // unavailable during shutdown. Never call this from an active frame.
    void releaseAfterGpuIdle(gfx::Device& device) {
        for(auto& [key,model]:models_){
            (void)key;
            if(model.gpu)render::releaseModel(device,*model.gpu);
        }
        models_.clear();
        authoring_.reset();earth_.reset();
        cachedRevision_=std::numeric_limits<uint64_t>::max();
    }

    // Called AFTER Renderer3D::beginScene and BEFORE Renderer3D::render.
    // gpuRetire uses Editor::retirePreviewModel; a model still referenced by
    // an earlier GPU frame must NOT be destroyed immediately.
    void draw(gfx::Device& device,render::Renderer3D& renderer,
              const MapDoc& doc,int64_t cameraXcm,int64_t cameraZcm,
              const Retire& gpuRetire) {
        if(!gpuRetire)
            throw std::invalid_argument("TerraWeave missing preview retirement");
        if(doc.data.planet() ||
           !doc.data.extra["terraweaveSparse"].isObject()){
            if(authoring_)release(gpuRetire);
            return;
        }
        if(cachedRevision_!=doc.revision || !authoring_) {
            release(gpuRetire);
            const auto loaded=sparse_terrain_ops::parse(
                doc.data.extra["terraweaveSparse"],Json::object());
            auto ground=std::make_unique<terraweave::EarthworksLayer>(
                loaded.spacingCm,
                [base=loaded.baseHeightCm](int64_t,int64_t){
                    return base;
                },loaded.soil.bedrockCm);
            ground->setSnapshot(loaded.soil);
            auto author=std::make_unique<terraweave::SparseTerrainAuthoring>(
                *ground,loaded.chunkMetres);
            author->restore(loaded.chunks);
            earth_=std::move(ground);
            authoring_=std::move(author);
            cachedRevision_=doc.revision;
        }
        if(!authoring_ || !authoring_->chunkCount())return;
        // Visible-owned chunk radius is bounded before flatness sampling,
        // even when a save contains tens of thousands of remote chunks.
        const auto view=authoring_->view(
            cameraXcm,cameraZcm,{},200000);
        struct Candidate {uint64_t distanceSq;terraweave::SparseTerrainView tile;};
        std::vector<Candidate> candidates;
        candidates.reserve(view.size());
        const int64_t span=int64_t(authoring_->chunkSizeMetres())*100;
        constexpr uint64_t maxDistSq=uint64_t(200000)*200000;
        for(const auto& tile:view) {
            const int64_t dx=int64_t(tile.cell.x)*span+span/2-cameraXcm;
            const int64_t dz=int64_t(tile.cell.z)*span+span/2-cameraZcm;
            const uint64_t d2=uint64_t(dx*dx)+uint64_t(dz*dz);
            if(d2<=maxDistSq)candidates.push_back({d2,tile});
        }
        std::sort(candidates.begin(),candidates.end(),
            [](const Candidate& a,const Candidate& b){
                if(a.distanceSq!=b.distanceSq)
                    return a.distanceSq<b.distanceSq;
                return a.tile.cell<b.tile.cell;
            });
        if(candidates.size()>kMaxVisible)candidates.resize(kMaxVisible);
        std::set<world::Cell> needed;
        for(const auto& c:candidates)needed.insert(c.tile.cell);
        for(auto it=models_.begin();it!=models_.end();) {
            if(!needed.contains(it->first)){
                gpuRetire(it->second.gpu);
                it=models_.erase(it);
            }else ++it;
        }
        size_t uploads=0;
        for(const auto& c:candidates) {
            auto it=models_.find(c.tile.cell);
            const bool matched=it!=models_.end() &&
                it->second.resolution==c.tile.resolution &&
                it->second.neighbors==c.tile.neighborResolution;
            if(matched)continue;
            if(uploads>=kMaxUploadsPerFrame)continue;
            auto visual=terraweave::bakeSparseTerrainVisual(
                *earth_,c.tile,authoring_->chunkSizeMetres());
            auto model=terraweave::makeEarthworksTerrainModel(visual);
            model->path="terraweave/editor-sparse/"+
                std::to_string(c.tile.cell.x)+"/"+
                std::to_string(c.tile.cell.z)+"/"+
                std::to_string(c.tile.resolution);
            render::prepareModelUpload(*model);
            if(!render::uploadModel(device,*model) ||
               !model->vb.valid() || !model->ib.valid()) {
                if(model)gpuRetire(model);
                // Keep the earlier visible tile or retry next frame.
                continue;
            }
            if(it!=models_.end())gpuRetire(it->second.gpu);
            ModelEntry entry;
            entry.resolution=c.tile.resolution;
            entry.neighbors=c.tile.neighborResolution;
            entry.gpu=std::move(model);
            models_.insert_or_assign(c.tile.cell,std::move(entry));
            ++uploads;
        }
        for(const auto& c:candidates) {
            const auto found=models_.find(c.tile.cell);
            if(found==models_.end() || !found->second.gpu)continue;
            render::ModelDraw draw;
            draw.model=found->second.gpu.get();
            draw.world=render::mat4::translate({
                float(int64_t(c.tile.cell.x)*span)*0.01f,
                // Scene's legacy RTS grid may still be drawn under this
                // optional overlay. Lift VISUAL preview by 2 cm to avoid
                // z-fighting; authored ECS soil and collider remain exact.
                0.02f,
                float(int64_t(c.tile.cell.z)*span)*0.01f
            });
            draw.flags=render::kDrawStatic;
            renderer.drawModel(draw);
        }
    }

private:
    static constexpr size_t kMaxVisible=64;
    static constexpr size_t kMaxUploadsPerFrame=4;
    struct ModelEntry {
        uint32_t resolution=0;
        std::array<uint32_t,4> neighbors{};
        std::unique_ptr<render::Model> gpu;
    };
    uint64_t cachedRevision_=std::numeric_limits<uint64_t>::max();
    std::unique_ptr<terraweave::EarthworksLayer> earth_;
    std::unique_ptr<terraweave::SparseTerrainAuthoring> authoring_;
    std::map<world::Cell,ModelEntry> models_;
};
} // namespace aiverse::editor
