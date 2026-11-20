// TerraWeave: owner-thread orchestration for excavation and NEREID-driven
// erosion. Live terrain geometry/collision is rebuilt with one shared surface
// topology. No duplicate water solver, implicit ECS scheduler or backend RHI.
#pragma once
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include "engine/world/TerraWeaveSparseTerrainAuthoring.h"
#include "engine/render/TerraWeaveGrassRender.h"
#include "engine/world/TerraWeaveNereidErosionBridge.h"
#include "engine/world/TerraWeaveNavMeshBridge.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct TerrainMutationResult {
    EarthworkChange earth{};
    PlantSupportUpdate vegetation{};
    std::vector<GrassCoverChange> grass{};
    uint64_t tick=0;
};

// This coordinator does not own the authoritative fixed-step ECS world.
// Call excavate()/erode() only after an authoritative game tick; call
// publishVisible() only on the graphics/SpatialWorld owner thread. Geometry
// publication is bounded separately from the gameplay tick and is revisioned.
class TerrainMutationCoordinator {
public:
    using ColliderIdForCell=std::function<uint32_t(world::Cell)>;
    using MaterialForCell=std::function<const render::Model*(world::Cell)>;

    TerrainMutationCoordinator(EarthworksLayer& ground,
                               int32_t cellSizeMetres,uint32_t resolution,
                               RootSupportRegistry* roots=nullptr,
                               GrassCoverRegistry* grass=nullptr)
        : earth_(ground),roots_(roots),grass_(grass),
          water_(earth_),cellSizeMetres_(cellSizeMetres),
          resolution_(resolution) {
        if(cellSizeMetres_<16 || cellSizeMetres_>4096 ||
           resolution_<2 || resolution_>129)
            throw std::invalid_argument("TerraWeave runtime terrain tile geometry");
    }
    NereidEarthworksBridge& waterBridge(){return water_;}
    const NereidEarthworksBridge& waterBridge()const{return water_;}
    size_t pendingTiles()const{return pending_.size();}
    size_t residentTiles()const{return residents_.size();}
    uint64_t soilRevision()const{return earth_.revision();}
    int32_t terrainCellSizeMetres()const{return cellSizeMetres_;}
    std::vector<world::Cell> residentCells()const{
        std::vector<world::Cell> cells;
        cells.reserve(residents_.size());
        for(const auto& [key,patch]:residents_){
            (void)patch;cells.push_back(key);
        }
        return cells;
    }
    // Copy only after an authoritative terrain tick/barrier. The caller must
    // ensure Earthworks' original BaseSampler closure owns immutable inputs;
    // copying std::function cannot freeze captured mutable external data.
    std::shared_ptr<const EarthworksLayer> presentationSnapshot()const{
        return std::make_shared<const EarthworksLayer>(earth_);
    }
    std::vector<TerrainPatchDirtyRegion> pendingTerrainNotifications()const{
        std::vector<TerrainPatchDirtyRegion> result;
        for(const auto& [cell,resident]:residents_){
            (void)cell;
            const auto& pending=resident.pendingDirty();
            result.insert(result.end(),pending.begin(),pending.end());
        }
        std::sort(result.begin(),result.end(),
            [](const auto& a,const auto& b){
                if(a.cell<b.cell)return true;
                if(b.cell<a.cell)return false;
                return a.sourceRevision<b.sourceRevision;
            });
        return result;
    }
    GrassPatchRenderer& grassRenderer(){return grassRhi_;}
    const GrassPatchRenderer& grassRenderer()const{return grassRhi_;}

    // Optional saved root/grass registries remain caller-owned, so native
    // games decide what to persist and how to replicate each plant/area.
    TerrainMutationResult excavate(EarthworkBrush brush,uint64_t tick){
        TerrainMutationResult out;
        out.tick=tick;
        out.earth=earth_.sculpt(brush);
        if(out.earth.vertices.empty())return out;
        if(grass_)out.grass=grass_->applyEarthwork(
            out.earth,earth_.spacingCm());
        if(roots_)out.vegetation=roots_->applyGroundChange(
            soilSnapshot(),out.earth,tick);
        schedule(out.earth);
        grassRhi_.notifyTerrainEdited(out.earth);
        return out;
    }

    BankTick erode(const std::vector<NereidBankProbe>& probes,
                   uint64_t completedTick,uint32_t dtMs,
                   lib::nereid::ShoreErosionSettings settings={}){
        auto out=water_.step(probes,completedTick,dtMs,
                             roots_,settings,grass_);
        schedule(out.earth);
        grassRhi_.notifyTerrainEdited(out.earth);
        return out;
    }

    // Supports non-water authoritative edits (replay, roadworks, removal of
    // buildings) and keeps the same scene publication policy.
    // Rebind a canonical ECS resource after rollback/load. Existing GPU
    // models and collision remain pinned until the rebuilt replacement
    // succeeds; every resident tile receives the current soil revision.
    void invalidateAllResident(){
        auto next=pending_;
        for(const auto& [cell,patch]:residents_){
            (void)patch;
            next.insert_or_assign(cell,earth_.revision());
        }
        pending_.swap(next);
        grassRhi_.invalidateAll();
    }

    void notifyExternalEarthwork(const EarthworkChange& change){
        if(!change.vertices.empty() && change.revision>earth_.revision())
            throw std::invalid_argument("TerraWeave external soil revision in future");
        schedule(change);
        grassRhi_.notifyTerrainEdited(change);
    }

    // A live, immutable-height callback must only be consumed on the owner
    // thread. Never capture this into async foliage/Horizon workers; first
    // copy committed tile heights or an immutable snapshot of Earthworks.
    SoilHeightSampler soilSnapshot()const {
        return [this](int64_t x,int64_t z)->std::optional<int32_t>{
            const int64_t spacing=earth_.spacingCm();
            if(x<=-kWorldLimitCm+spacing || x>=kWorldLimitCm-spacing ||
               z<=-kWorldLimitCm+spacing || z>=kWorldLimitCm-spacing)
                return std::nullopt;
            return earth_.sampleHeightCm(x,z);
        };
    }

    // "visible" is the host's required/pinned terrain set, not only frustum
    // visible cells: authoritative collision must remain resident wherever
    // gameplay could query it. The host reserves collider ID ranges.
    // At most maxUploads (1..8) RHI+collision replacements per call.
    // Even after many soil ticks, only the latest Earthworks revision is baked
    // once for each affected visible tile.
    size_t publishVisible(gfx::Device& device,world::SpatialWorld& spatial,
                          std::span<const world::Cell> visible,
                          size_t maxUploads,
                          const ColliderIdForCell& ids,
                          const MaterialForCell& materials={},
                          float uvScale=0.05f,
                          std::span<const SparseTerrainView> adaptive={}) {
        const uint64_t trisPerTile=
            uint64_t(resolution_-1u)*(resolution_-1u)*2u;
        if(!ids || maxUploads<1 || maxUploads>8 ||
           visible.size()>512u ||
           uint64_t(visible.size())*trisPerTile>4000000u)
            throw std::invalid_argument("TerraWeave terrain residency budget");
        std::set<world::Cell> needed;
        for(const auto& tile:visible){
            if(tile.y!=0)
                throw std::invalid_argument("TerraWeave tiles are X/Z columns");
            needed.insert(tile);
        }
        std::map<world::Cell,SparseTerrainView> visualPlan;
        for(const auto& tile:adaptive) {
            if(!needed.contains(tile.cell) ||
               tile.resolution<2 || tile.resolution>129 ||
               !visualPlan.emplace(tile.cell,tile).second)
                throw std::invalid_argument("TerraWeave invalid adaptive terrain plan");
        }
        if(!adaptive.empty() && visualPlan.size()!=needed.size())
            throw std::invalid_argument("TerraWeave incomplete adaptive terrain plan");
        // A camera move can require re-meshing with no soil mutation.
        // Do not publish the same stale LOD when neighbor edge ratios changed.
        for(const auto& [tile,desired]:visualPlan){
            const auto resident=residents_.find(tile);
            if(resident!=residents_.end() &&
               (resident->second.visualResolution()!=desired.resolution ||
                resident->second.neighborResolution()!=desired.neighborResolution))
                pending_.insert_or_assign(tile,earth_.revision());
        }
        // Preserve old simulation-pinned collision until ALL newly needed
        // tiles have been published. This is load-first/unload-last, unlike
        // removing old colliders before discovering GPU allocation failure.
        // A camera/streaming jump can abandon work before publication.
        // Remove now-unneeded requests so old pending tiles cannot block
        // the WorldScene snapshot barrier forever.
        for(auto it=pending_.begin();it!=pending_.end();)
            if(!needed.contains(it->first))it=pending_.erase(it);
            else ++it;
        for(const auto& tile:needed)
            if(!residents_.contains(tile) && !pending_.contains(tile))
                pending_.insert_or_assign(tile,earth_.revision());

        size_t published=0;
        for(const auto& tile:needed){
            if(published>=maxUploads)break;
            const auto it=pending_.find(tile);
            if(it==pending_.end())continue;
            // The physics mesh always uses the original fixed resolution.
            // Pure camera-distance LOD updates upload ONLY a visual model.
            // All residents share the same authoritative Earthworks source.
            auto [slot,created]=residents_.try_emplace(tile);
            try {
                const render::Model* material=
                    materials?materials(tile):nullptr;
                const auto spec=visualPlan.find(tile);
                std::optional<TerrainPatch> visual;
                if(spec!=visualPlan.end())
                    visual.emplace(bakeSparseTerrainVisual(
                        earth_,spec->second,cellSizeMetres_));
                const bool visualOnly=visual &&
                    slot->second.resident() &&
                    slot->second.revision()==earth_.revision();
                if(visualOnly){
                    if(!slot->second.replaceVisual(
                            device,*visual,spec->second.neighborResolution,
                            material,uvScale))
                        break; // keep prior GPU model safely resident
                }else{
                    const TerrainPatch patch=bakeEarthworksTerrainPatch(
                        earth_,tile,cellSizeMetres_,resolution_);
                    if(!slot->second.publish(
                            device,spatial,colliders_,patch,ids(tile),
                            material,uvScale,visual?&*visual:nullptr,
                            visual?spec->second.neighborResolution:
                                   std::array<uint32_t,4>{})){
                        if(created)residents_.erase(slot);
                        break; // GPU budget exhausted; retry a later frame
                    }
                }
            }catch(...){
                if(created)residents_.erase(slot);
                throw;
            }
            pending_.erase(it);
            ++published;
        }
        bool complete=true;
        for(const auto& tile:needed)
            if(!residents_.contains(tile)){complete=false;break;}
        if(complete)
            for(auto it=residents_.begin();it!=residents_.end();){
                if(!needed.contains(it->first)){
                    it->second.shutdown(device,spatial,colliders_);
                    it=residents_.erase(it);
                }else ++it;
            }
        return published;
    }

    // Cosmetic grass follows the same staged terrain heights. Patches are
    // proximity-sorted and bounded to the quality's GPU residency budget;
    // each is one batched Model, never an ECS entity/draw per blade.
    // Call on gfx::Device's owner thread after ECS/soil publication.
    size_t publishGrass(gfx::Device& device,RectCm area){
        if(!grass_)return 0;
        if(area.minX>=area.maxX || area.minZ>=area.maxZ ||
           area.minX<-kWorldLimitCm || area.minZ<-kWorldLimitCm ||
           area.maxX>kWorldLimitCm || area.maxZ>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave grass visibility bounds");
        auto patches=grass_->snapshot();
        patches.erase(std::remove_if(patches.begin(),patches.end(),
            [area](const GrassPatch& p){
                return p.bounds.minX>=area.maxX ||
                       p.bounds.maxX<=area.minX ||
                       p.bounds.minZ>=area.maxZ ||
                       p.bounds.maxZ<=area.minZ;
            }),patches.end());
        const int64_t cx=area.minX+(area.maxX-area.minX)/2;
        const int64_t cz=area.minZ+(area.maxZ-area.minZ)/2;
        const auto distance=[&](const GrassPatch& patch)->uint64_t{
            const int64_t x=patch.bounds.minX+
                (patch.bounds.maxX-patch.bounds.minX)/2;
            const int64_t z=patch.bounds.minZ+
                (patch.bounds.maxZ-patch.bounds.minZ)/2;
            const int64_t dx=x-cx,dz=z-cz;
            return uint64_t(dx*dx)+uint64_t(dz*dz);
        };
        std::sort(patches.begin(),patches.end(),
            [&](const GrassPatch& a,const GrassPatch& b){
                const uint64_t da=distance(a),db=distance(b);
                return da==db?a.key<b.key:da<db;
            });
        if(patches.size()>grassRhi_.quality().maxGpuPatches)
            patches.resize(grassRhi_.quality().maxGpuPatches);
        return grassRhi_.update(device,patches,
            [this](int64_t x,int64_t z){
                return earth_.sampleHeightCm(x,z);
            });
    }

    // Both the graphics pass and the collision publisher see the same mesh
    // revision. The original baked terrain draw must be suppressed by host
    // when a live patch is drawn, or z-fighting will occur.
    void draw(render::Renderer3D& renderer,
              TerrainPatchRenderOriginCm origin={}) const {
        for(const auto& [cell,patch]:residents_){
            (void)cell;patch.submit(renderer,origin);
        }
        grassRhi_.draw(renderer,{origin.x,origin.y,origin.z});
    }

    // Refresh the EXISTING nav::Mesh only where newly published collision
    // tiles differ. The caller owns the walkability/water/obstacle snapshot
    // used by EarthworksNavSource. We intentionally DO NOT consume dirty
    // records here: Horizon/foliage and external navmesh listeners must see
    // the same revision via takeTerrainDirty() after all refreshes succeed.
    bool refreshNavMesh(nav::Mesh& mesh,
                        const EarthworksNavSource& source,
                        std::string* error=nullptr) const {
        std::vector<RectCm> dirty;
        for(const auto& [cell,resident]:residents_){
            (void)cell;
            for(const auto& change:resident.pendingDirty())
                dirty.push_back(change.bounds);
        }
        if(dirty.empty())return true;
        return refreshEarthworksNav(mesh,source,dirty,error);
    }

    std::vector<TerrainPatchDirtyRegion> takeTerrainDirty(){
        std::vector<TerrainPatchDirtyRegion> out;
        for(auto& [cell,resident]:residents_){
            (void)cell;
            auto changed=resident.takeDirty();
            out.insert(out.end(),changed.begin(),changed.end());
        }
        std::sort(out.begin(),out.end(),
            [](const auto& a,const auto& b){
                if(a.cell<b.cell)return true;
                if(b.cell<a.cell)return false;
                return a.sourceRevision<b.sourceRevision;
            });
        return out;
    }

    void shutdown(gfx::Device& device,world::SpatialWorld& spatial){
        for(auto& [cell,resident]:residents_){
            (void)cell;resident.shutdown(device,spatial,colliders_);
        }
        residents_.clear();
        grassRhi_.shutdown(device);
        // Preserve pending dirties/earthwater simulation data for reload.
    }

private:
    void schedule(const EarthworkChange& change){
        if(change.vertices.empty())return;
        // Edits to nonresident cells stay authoritative in Earthworks;
        // they are baked on-demand when the tile becomes simulation-pinned.
        // Only currently resident tiles need a queued rebuild. This avoids
        // scanning thousands of unloaded world cells during a large river
        // flood or a sparse set of far-apart NEREID bank probes.
        const int64_t pad=earth_.spacingCm();
        const int64_t x0=std::max<int64_t>(
            -kWorldLimitCm,change.dirty.minX-pad);
        const int64_t x1=std::min<int64_t>(
            kWorldLimitCm,change.dirty.maxX+pad);
        const int64_t z0=std::max<int64_t>(
            -kWorldLimitCm,change.dirty.minZ-pad);
        const int64_t z1=std::min<int64_t>(
            kWorldLimitCm,change.dirty.maxZ+pad);
        const int64_t span=int64_t(cellSizeMetres_)*100;
        auto next=pending_;
        for(const auto& [tile,resident]:residents_) {
            (void)resident;
            const int64_t tx=int64_t(tile.x)*span;
            const int64_t tz=int64_t(tile.z)*span;
            if(tx<x1 && tx+span>x0 &&
               tz<z1 && tz+span>z0)
                next.insert_or_assign(tile,change.revision);
        }
        pending_.swap(next);
    }

    EarthworksLayer& earth_;
    RootSupportRegistry* roots_=nullptr;
    GrassCoverRegistry* grass_=nullptr;
    NereidEarthworksBridge water_;
    int32_t cellSizeMetres_=256;
    uint32_t resolution_=33;
    TerrainCollisionPublisher colliders_;
    GrassPatchRenderer grassRhi_{};
    std::map<world::Cell,TerrainPatchResident> residents_;
    std::map<world::Cell,uint64_t> pending_;
};

} // namespace aiverse::terraweave
