// Scene-view preview of the SAME sparse TerraWeave chunks edited through
// Workspace / MCP / HTML5. One shared CPU Earthworks height source, CPU
// camera LOD plan, GPU models through ordinary Renderer3D. No ECS, physics,
// collision/navmesh ownership. Old Models enter Editor's GPU retirement queue.
#pragma once
#include "apps/editor/core/TerraWeaveSparseOps.h"
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsSurfaceGroup.h"
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
    const std::string& surfaceGroupDiagnostic()const noexcept{return groupDiagnostic_;}
    size_t certifiedSurfaceEdges()const noexcept{return group_?group_->certifiedEdges:0;}
    size_t plannedSurfaceOpenings()const noexcept{return group_?group_->acceptedEntrances.size():0;}
    std::shared_ptr<const terraweave::caverns::CaveSurfaceGroupPlan> surfaceGroup()const noexcept{return group_;}
    bool surfaceGroupUploaded()const noexcept{return group_&&models_.size()==groupCells_.size();}
    // Refuse the entire cut preview on unsupported volume geometry. Keep
    // the selected tile set so the following prepare builds solid terrain.
    void rejectSurfaceGroup(const Retire& retire,std::string diagnostic){
        for(auto& [key,entry]:models_){(void)key;if(entry.gpu)retire(entry.gpu);}
        models_.clear();group_.reset();groupDiagnostic_=std::move(diagnostic);
    }

    void release(const Retire& retire) {
        if(!retire)throw std::invalid_argument("TerraWeave preview missing GPU retirement owner");
        for(auto& [key,model]:models_){(void)key;retire(model.gpu);}
        models_.clear();
        authoring_.reset();
        earth_.reset();
        cachedRevision_=std::numeric_limits<uint64_t>::max();
        caveSnapshot_.reset();cachedPath_.clear();group_.reset();groupCells_.clear();groupDiagnostic_.clear();
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
        caveSnapshot_.reset();cachedPath_.clear();group_.reset();groupCells_.clear();groupDiagnostic_.clear();
    }

    // Called AFTER Renderer3D::beginScene and BEFORE Renderer3D::render.
    // gpuRetire uses Editor::retirePreviewModel; a model still referenced by
    // an earlier GPU frame must NOT be destroyed immediately.
    void draw(gfx::Device& device,render::Renderer3D& renderer,
              const MapDoc& doc,int64_t cameraXcm,int64_t cameraZcm,
              const Retire& gpuRetire,bool allowCuts=false) {
        prepare(device,doc,cameraXcm,cameraZcm,gpuRetire,allowCuts);
        submit(renderer,true);
    }
    void prepare(gfx::Device& device,const MapDoc& doc,int64_t cameraXcm,int64_t cameraZcm,
              const Retire& gpuRetire,bool allowCuts=false){
        if(!gpuRetire)
            throw std::invalid_argument("TerraWeave missing preview retirement");
        if(doc.data.planet() ||
           !doc.data.extra["terraweaveSparse"].isObject()){
            if(authoring_)release(gpuRetire);
            return;
        }
        if(cachedRevision_!=doc.revision || cachedPath_!=doc.path || !authoring_ || allowCuts_!=allowCuts) {
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
            if(allowCuts&&!doc.data.extra["terraweaveCaverns"].isNull())
                caveSnapshot_=std::make_shared<const terraweave::caverns::CaveVolumeSnapshot>(
                    terraweave::caverns::caveSnapshotFromJson(doc.data.extra["terraweaveCaverns"]));
            allowCuts_=allowCuts;cachedPath_=doc.path;
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
        const bool grouped=caveSnapshot_&&!caveSnapshot_->entrances.empty()&&!needed.empty();
        if(grouped&&groupCells_!=needed){
            // A fresh visible set needs a whole-footprint plan. Do not
            // submit any partially uploaded cut group to the draw list.
            for(auto& [key,entry]:models_){(void)key;if(entry.gpu)gpuRetire(entry.gpu);}
            models_.clear();group_.reset();groupDiagnostic_.clear();
            try{
                const std::vector<world::Cell> pins(needed.begin(),needed.end());
                group_=std::make_shared<terraweave::caverns::CaveSurfaceGroupPlan>(
                    terraweave::caverns::planCaveSurfaceGroup(*earth_,*caveSnapshot_,pins,
                        authoring_->chunkSizeMetres(),33,1));
            }catch(const std::exception& e){groupDiagnostic_=e.what();}
            groupCells_=needed;
        }
        for(auto it=models_.begin();it!=models_.end();) {
            if(!needed.contains(it->first)){
                gpuRetire(it->second.gpu);
                it=models_.erase(it);
            }else ++it;
        }
        size_t uploads=0;
        for(const auto& c:candidates) {
            auto it=models_.find(c.tile.cell);
            const bool clipped=group_&&group_->patches.at(c.tile.cell).clippedSurface;
            const uint32_t effective=caveSnapshot_?((c.tile.flat&&!clipped)?2u:33u):c.tile.resolution;
            const auto neighbors=caveSnapshot_?std::array<uint32_t,4>{}:c.tile.neighborResolution;
            const bool matched=it!=models_.end() &&
                it->second.resolution==effective &&
                it->second.neighbors==neighbors;
            if(matched)continue;
            if(uploads>=kMaxUploadsPerFrame)continue;
            // On rejected group preflight use solid ground everywhere,
            // not independent per-tile cuts. Flat uncut tiles stay quads.
            auto visual=clipped?group_->patches.at(c.tile.cell):caveSnapshot_
                ? terraweave::bakeEarthworksTerrainPatch(*earth_,c.tile.cell,authoring_->chunkSizeMetres(),effective)
                : terraweave::bakeSparseTerrainVisual(*earth_,c.tile,authoring_->chunkSizeMetres());
            auto model=terraweave::makeEarthworksTerrainModel(visual);
            if(!model){
                if(it!=models_.end()&&it->second.gpu)gpuRetire(it->second.gpu);
                ModelEntry entry;entry.resolution=effective;entry.neighbors=neighbors;
                models_.insert_or_assign(c.tile.cell,std::move(entry));++uploads;continue;
            }
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
            entry.resolution=effective;
            entry.neighbors=neighbors;
            entry.gpu=std::move(model);
            models_.insert_or_assign(c.tile.cell,std::move(entry));
            ++uploads;
        }
    }
    // Studio calls only after the matching volume group is actually active.
    // Legacy draw() retains the older surface-only behavior.
    void submit(render::Renderer3D& renderer,bool matchingVolumeReady,bool exactOrigin=false)const {
        if(!authoring_||(group_&&(!matchingVolumeReady||!surfaceGroupUploaded())))return;
        const int64_t span=int64_t(authoring_->chunkSizeMetres())*100;
        for(const auto& [key,entry]:models_) {
            if(!entry.gpu)continue;
            render::ModelDraw draw;
            draw.model=entry.gpu.get();
            draw.world=render::mat4::translate({
                float(int64_t(key.x)*span)*0.01f,
                // Scene's legacy RTS grid may still be drawn under this
                // optional overlay. Lift VISUAL preview by 2 cm to avoid
                // z-fighting; authored ECS soil and collider remain exact.
                (exactOrigin||allowCuts_)?0.f:0.02f,
                float(int64_t(key.z)*span)*0.01f
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
    std::shared_ptr<const terraweave::caverns::CaveSurfaceGroupPlan> group_;
    std::set<world::Cell> groupCells_;
    std::string groupDiagnostic_;
    bool allowCuts_=false;
    std::string cachedPath_;
    std::shared_ptr<const terraweave::caverns::CaveVolumeSnapshot> caveSnapshot_;
    uint64_t cachedRevision_=std::numeric_limits<uint64_t>::max();
    std::unique_ptr<terraweave::EarthworksLayer> earth_;
    std::unique_ptr<terraweave::SparseTerrainAuthoring> authoring_;
    std::map<world::Cell,ModelEntry> models_;
};
} // namespace aiverse::editor
