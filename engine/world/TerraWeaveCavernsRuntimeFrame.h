#pragma once
// Stage35: genre-independent integration point for the real render/simulation
// owner (game client, editor play host, or authoritative server).
// The owner MUST call frameOutsidePass between GPU passes/physics steps and
// publishWithExclusiveWorld with a real lock/paused readers covering the
// entire call. This is NOT installed in the deterministic GameCore tick,
// because that tick must never touch RHI or mutate out-of-band Box3D.
#include "engine/world/TerraWeaveCavernsGameplayPhysicsBarrier.h"
#include "engine/world/TerraWeaveCavernsWorldResidency.h"
#include "engine/world/TerraWeaveCavernsStreamJobs.h"
#include "engine/world/TerraWeaveCavernsNavigationPublication.h"
#include "engine/world/TerraWeaveCavernsPathRouter.h"
#include "engine/world/TerraWeaveCavernsGraphSolvers.h"
#include "engine/world/TerraWeaveCavernsNativeNavSolver.h"
#include "engine/nav/CaveEcsRouteFollow.h"
#include "engine/nav/CaveSimulationNavigation.h"
#include "engine/nav/CaveEcsSteering.h"
#include "engine/nav/CavePathQueryCache.h"
#include "engine/world/TerraWeaveCavernsNavigationEnvironment.h"
#include "engine/world/TerraWeaveCavernsNativeNavMeshCache.h"
#include "engine/world/TerraWeaveCavernsNavigationRegionCache.h"
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::terraweave::caverns {
class CaveGameplayRuntimeFrame {
public:
    explicit CaveGameplayRuntimeFrame(uint32_t resolution=16,
                                      int32_t spacingCm=100,
                                      CaveShardConfig shards={},
                                      size_t pending=1024,size_t workers=2,
                                      StreamSettings streaming={})
        :residency_(streaming),
         streamJobs_(workers),
         host_(resolution,spacingCm,shards,pending,workers) {
        registerCaveSharedGraphSolvers(pathRouter_);
        navigationBus_.subscribe(CaveNavigationConsumer::Custom,
            [this](const CaveNavigationChange&,
                   std::shared_ptr<const CaveNavigationDataProvider>){
                pathCache_.invalidate();
                nativeNavCache_.invalidate();
            });
        navigationBus_.subscribe(CaveNavigationConsumer::HierarchicalAStar,
            [this](const CaveNavigationChange& change,
                   std::shared_ptr<const CaveNavigationDataProvider> source){
                if(regionCache_)regionCache_->refresh(change,std::move(source));
            });
    }
    // Explicit per-world entry point. Instances are single-world owners:
    // after closeWorldAfterGpuIdle succeeds, construct a new one for a new
    // world/device. No GPU shutdown happens in the C++ destructor itself.
    void openWorld(CaveDirtyBounds initialLoadedBounds,
                   std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(closed_)throw std::logic_error("Caverns frame host already closed");
        onEditedVolume(initialLoadedBounds,std::move(snapshot));
    }
    void onEditedVolume(CaveDirtyBounds bounds,
                        std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(closed_||physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns world closed or needs recovery");
        host_.edited(bounds,std::move(snapshot));
        recordNavigationBounds(bounds);
        readyStreamBuilds_.clear();
        pendingRetires_.clear();
        syncStreamingEditPins();
    }
    bool onAuthoringOperation(const Json& response,
                             std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(closed_||physicsBarrier_.status().recoveryRequired)return false;
        const bool accepted=host_.consumeAuthoringOperation(response,std::move(snapshot));
        if(accepted){
            const Json& b=response["dirtyBounds"];
            recordNavigationBounds({
                b.getInt("minXcm"),b.getInt("minYcm"),b.getInt("minZcm"),
                b.getInt("maxXcm"),b.getInt("maxYcm"),b.getInt("maxZcm")});
            readyStreamBuilds_.clear();
            pendingRetires_.clear();
            syncStreamingEditPins();
        }
        return accepted;
    }
    // Optional Stage38 real GPU upload source-byte budget and bounded
    // whole-shard count per frame, used by the gameplay edit host.
    void setGpuUploadLimits(CaveGpuUploadLimits limits){
        host_.setGpuUploadLimits(limits);
    }
    CaveGpuUploadStats gpuUploadStats()const noexcept{
        return host_.gpuUploadStats();
    }
    // Runs on an owner frame boundary, not from a deterministic simulation
    // system or an already open graphics command buffer.
    CaveEditTick frameOutsidePass(
        gfx::Device& device,const world::SpatialWorld& spatial,
        CaveNavConfig navConfig,CaveGameplayEditHost::TerrainHeight height,
        const CaveGameplayEditHost::ColliderIdAllocator& colliderIds,
        uint64_t nextVisualRevision,size_t jobsPerFrame=2,
        CaveBatchSpec graphicsOptions={}) {
        if(closed_||physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns game world closed or requires recovery");
        syncStreamingEditPins();
        (void)host_.collectGpuCompletedWithoutIdle(device);
        return host_.advance(device,spatial,navConfig,std::move(height),
                             colliderIds,nextVisualRevision,jobsPerFrame,
                             graphicsOptions);
    }
    // The exclusive predicate must remain TRUE and actually hold simulation,
    // physics queries, nav readers and draw submissions paused until this
    // method returns. An unconditional-true predicate is NOT sufficient
    // without a real owner quiescence mechanism.
    bool publishWithExclusiveWorld(
        physics::Scene& box3D,world::SpatialWorld& spatial,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive) {
        if(closed_||!exclusive || !exclusive()||
           physicsBarrier_.status().recoveryRequired)return false;
        bool physicsFinalized=false;
        bool published=false;
        try {
            published=host_.commitAtOwnerBarrier([&](const CaveGameplayCommitView& view){
                if(!exclusive())return false;
                physicsFinalized=physicsBarrier_.apply(view,box3D,spatial,exclusive);
                return physicsFinalized;
            });
        }catch(...) {
            // Any exception during native installation may leave a partially
            // inserted collider or body; do not resume a mixed epoch.
            physicsBarrier_.requireRecovery();
            throw;
        }
        if(!published&&physicsFinalized)
            physicsBarrier_.requireRecovery();
        if(published){
            syncStreamingEditPins();
            // Notify ALL derived solver caches only after a real all-owner
            // Box3D/SpatialWorld/NavMesh/GPU commit. Failure to deliver an
            // invalidation leaves a pending retry, not a false physics ACK.
            if(navDirty_){
                (void)navigationBus_.publish(host_.indexedSnapshot(),
                                             navDirtyBounds_);
                navDirty_=false;
            }
        }
        return published;
    }
    using NavigationCallback=CaveNavigationPublication::Callback;
    CaveNavigationSubscription subscribeNavigation(
        CaveNavigationConsumer algorithm,NavigationCallback callback){
        return navigationBus_.subscribe(algorithm,std::move(callback));
    }
    bool unsubscribeNavigation(CaveNavigationSubscription token){
        return navigationBus_.unsubscribe(token);
    }
    // A*/HPA*/FlowField/Hybrid/Custom consumers query this shared data.
    // The NavMesh shadow still uses its existing nav::Source projection.
    std::shared_ptr<const CaveNavigationDataProvider>
    publishedNavigation()const{return navigationBus_.active();}
    bool navigationInvalidationsPending()const noexcept{
        return navigationBus_.hasPending();
    }
    bool retryNavigationInvalidations(){
        return navigationBus_.dispatchPending();
    }
    // Game authority installs this during Simulation::setup with a real
    // same-epoch native physics callback, never visual-only GameClient.
    void installNavigationInSimulation(
        Simulation& sim,nav::CaveMovementCollision collision,
        uint32_t maxRepairsPerTick=8,uint32_t maxMovedPerTick=65536) {
        nav::CaveSimulationNavigationHooks hooks;
        hooks.publishedSource=[this]{return gameplayNavigationSource();};
        hooks.search=[this](const std::string& algorithm,
                            const CavePathQuery& q){
            return this->findPath(algorithm,q);
        };
        hooks.collision=std::move(collision);
        hooks.invalidateDerived=[this]{pathCache_.invalidate();};
        hooks.maxRepairsPerTick=maxRepairsPerTick;
        hooks.maxMovedPerTick=maxMovedPerTick;
        nav::installCaveNavigationSimulation(sim,std::move(hooks));
    }
    // Same-epoch source for actual gameplay consumers, not visual preview.
    // Failed native publication or undelivered navigation invalidation must
    // suppress both movement requests and solver reads until recovered.
    std::shared_ptr<const CaveNavigationDataProvider>
    gameplayNavigationSource()const {
        if(closed_||physicsBarrier_.status().recoveryRequired||
           navigationBus_.hasPending())return nullptr;
        return navigationBus_.active();
    }
    // Preferred motor-neutral integration. The actual authority has already
    // installed its own movement system. Read positions from its ECS world,
    // then insert repath and steering before that exact system. The frame
    // owner must outlive all registered Simulation callbacks.
    void installMotorNavigationInSimulation(
        Simulation& sim,nav::CaveWorldPositionSource position,
        const std::string& motorSystem,
        uint32_t maxRepairsPerTick=8,uint32_t maxSteeredPerTick=65536) {
        nav::CaveSimulationNavigationHooks hooks;
        hooks.publishedSource=[this]{return gameplayNavigationSource();};
        hooks.search=[this](const std::string& algorithm,const CavePathQuery& q){
            return findPath(algorithm,q);
        };
        hooks.invalidateDerived=[this]{invalidateNavigationRouteCache();};
        hooks.maxRepairsPerTick=maxRepairsPerTick;
        nav::installCaveMotorNavigationBefore(sim,std::move(hooks),
            std::move(position),motorSystem,maxSteeredPerTick);
    }
    // Publish the authoritative immutable ECS environment at a fixed-tick
    // owner barrier: NEREID flood and PHYSIS traction are derived read-only.
    bool publishNavigationEnvironment(
        std::shared_ptr<const World> frozenWorld,
        uint64_t environmentEpoch,CaveDirtyBounds affected,
        CaveEnvironmentCostSettings settings={},bool fullRebuild=false) {
        const auto published=navigationBus_.active();
        if(closed_||physicsBarrier_.status().recoveryRequired||!published||
           !frozenWorld||!environmentEpoch)return false;
        CaveEnvironmentSource src{
            published->revision(),environmentEpoch,std::move(frozenWorld)};
        return navigationBus_.publishEnvironment(environmentEpoch,
            makeCaveEnvironmentCallbacks(std::move(src),settings),
            affected,fullRebuild);
    }
    // Convenience entry point at a real fixed-tick authority barrier.
    // Copy-on-write ECS snapshot retains deterministic NEREID/PHYSIS data
    // for every route query that reads this environment epoch. Call only
    // after corresponding simulation systems have committed that tick.
    bool publishNavigationEnvironmentFromSimulation(
        const Simulation& sim,CaveDirtyBounds affected,
        CaveEnvironmentCostSettings settings={},bool fullRebuild=false) {
        const uint64_t epoch=uint64_t(sim.tick())+1;
        auto frozen=std::make_shared<const World>(sim.world());
        return publishNavigationEnvironment(std::move(frozen),epoch,
                                            affected,settings,fullRebuild);
    }
    nav::CavePathCacheStats navigationCacheStats()const noexcept {
        return pathCache_.stats();
    }
    void invalidateNavigationRouteCache(){
        pathCache_.invalidate();
        nativeNavCache_.invalidate();
    }
    // Persistent loaded-region 3D topology. Compared with per-query graph
    // regeneration, retain SDF neighbors, HPA portals and goal flow fields.
    // Named "regional.*" backends are available after first native commit.
    bool enableRegionalNavigation(CaveGraphGrid grid,
                                  CaveAgentProfile profile={},
                                  uint32_t clusterCells=8,
                                  size_t maxFlows=8) {
        const auto source=navigationBus_.active();
        if(!source||navigationBus_.hasPending()||regionCache_)return false;
        regionCache_=std::make_unique<CaveNavigationRegionCache>(
            source,grid,profile,clusterCells,maxFlows);
        for(const auto& entry:std::vector<std::pair<const char*,CavePathBackend>>{
            {"regional.astar",CavePathBackend::AStar},
            {"regional.hpa",CavePathBackend::HPAStar},
            {"regional.flow",CavePathBackend::FlowField},
            {"regional.hybrid",CavePathBackend::Hybrid}}) {
            (void)pathRouter_.registerSolver(entry.first,entry.second,
                [this,backend=entry.second](
                    const CavePathQuery& query,
                    std::shared_ptr<const CaveNavigationDataProvider>) {
                    return regionCache_?regionCache_->find(query,backend):
                                        CavePathResult{};
                });
        }
        return true;
    }
    nav::GraphRegionCache::Stats regionalNavigationStats()const noexcept{
        return regionCache_?regionCache_->stats():
                            nav::GraphRegionCache::Stats{};
    }
    bool registerNativeNavMeshSolver(CaveNavConfig config,
                                     const std::string& name="navmesh") {
        validateCaveNav(config);
        return pathRouter_.registerSolver(name,CavePathBackend::NavMesh,
            [this,config](const CavePathQuery& q,
                          std::shared_ptr<const CaveNavigationDataProvider> src){
                return nativeNavCache_.find(q,std::move(src),config);
            });
    }
    // The ACTUAL authoritative ECS owner calls this on its fixed tick.
    // It must provide a real same-epoch Box3D/SpatialWorld collision
    // callback; never invoke this against visual-only client preview.
    nav::CaveRouteTickStats updateEcsRouteFollowing(
        World& world,const nav::CaveMovementCollision& collision) {
        if(closed_||physicsBarrier_.status().recoveryRequired ||
           navigationBus_.hasPending())return {};
        const auto data=navigationBus_.active();
        if(!data)return {};
        return nav::updateCaveRouteFollowers(world,*data,collision);
    }
    // Default engine/nav solvers registered at construction:
    // "astar", "hpa", "flow", "hybrid". Native NavMesh and custom
    // algorithms use the same registerPathSolver endpoint.
    void setGraphSolverSettings(CaveGraphSolverSettings settings){
        for(const auto* name:{"astar","hpa","flow","hybrid"})
            (void)pathRouter_.unregisterSolver(name);
        registerCaveSharedGraphSolvers(pathRouter_,settings);
        pathCache_.invalidate();
    }
    bool registerPathSolver(std::string name,CavePathBackend kind,
                            CavePathRouter::Solver solver){
        return pathRouter_.registerSolver(std::move(name),kind,std::move(solver));
    }
    bool unregisterPathSolver(const std::string& name){
        return pathRouter_.unregisterSolver(name);
    }
    CavePathResult findPath(const std::string& name,
                            const CavePathQuery& query)const {
        const auto published=gameplayNavigationSource();
        if(!published || published->revision()!=query.sourceRevision)
            return {};
        // Custom solvers can depend on external dynamic state not captured by
        // the common environment epoch. Do not memoize them implicitly.
        if(pathRouter_.hasSolver(name) &&
           pathRouter_.backend(name)==CavePathBackend::Custom)
            return pathRouter_.query(name,query,navigationBus_);
        return pathCache_.get(name,query,published->environmentRevision(),
            [&]{return pathRouter_.query(name,query,navigationBus_);});
    }
    // The host drives interest once per world frame using the camera/player
    // center in CHUNK coordinates. Build tickets may be scheduled on worker
    // threads; retire tickets must go through the actual physics/GPU/nav
    // owner barrier. Tickets are NOT ACKed by pumpStreaming().
    void setStreamCenter(ChunkKey3 center){residency_.center(center);}
    void setStreamLod(uint32_t lod){residency_.visualLod(lod);}
    void enableAdaptiveStreamLod(CaveDistanceLodSettings settings={}) {
        residency_.enableDistanceLod(settings);
    }
    void disableAdaptiveStreamLod(){residency_.disableDistanceLod();}
    std::vector<CaveResidencyTicket> pumpStreaming() {
        if(closed_||physicsBarrier_.status().recoveryRequired)return {};
        syncStreamingEditPins();
        return residency_.pump();
    }
    // Frame-limited streaming CPU dispatch. Returns retire intents which
    // require owner-side native resource retirement. Build intents are sent
    // to bounded immutable CPU workers; no GPU work starts on those threads.
    std::vector<CaveResidencyTicket> dispatchStreamingCpu(
        ChunkKey3 cameraChunk,uint32_t resolution=16,int32_t spacingCm=100) {
        if(physicsBarrier_.status().recoveryRequired)return {};
        setStreamCenter(cameraChunk);
        auto source=host_.sourceSnapshot();
        auto indexed=host_.indexedSnapshot();
        if(!source||!indexed)return {};
        syncStreamingEditPins();
        std::vector<CaveResidencyTicket> retire;
        for(const auto& request:residency_.pump()) {
            if(request.action==CaveResidencyAction::Retire) {
                pendingRetires_.insert_or_assign(request.key,request);
                retire.push_back(request);
            }else{
                try {
                    if(!streamJobs_.dispatch(request,source,resolution,spacingCm,indexed))
                        (void)residency_.fail(request);
                } catch(...) {
                    (void)residency_.fail(request);
                    throw;
                }
            }
        }
        return retire;
    }
    // Poll CPU completions at the owner frame boundary. The caller must
    // stage/publish the exact returned ticket through all real native owners
    // before calling acceptStreaming(ticket). Old generations are discarded.
    std::vector<CaveStreamCpuResult> pollStreamingCpu(
        CaveStreamJobStats* telemetry=nullptr) {
        auto results=streamJobs_.poll(telemetry);
        results.erase(std::remove_if(results.begin(),results.end(),
            [this](const CaveStreamCpuResult& r){
                if(!residency_.current(r.ticket))return true;
                if(!r.success){
                    (void)residency_.fail(r.ticket);
                    return true;
                }
                return false;
            }),results.end());
        for(auto& item:results)
            readyStreamBuilds_.insert_or_assign(item.ticket.key,item);
        // Keep completed CPU results alive until the actual GPU/Box3D/Nav
        // owner has published them; never ACK here.
        for(auto it=readyStreamBuilds_.begin();it!=readyStreamBuilds_.end();){
            if(!residency_.current(it->second.ticket))
                it=readyStreamBuilds_.erase(it);
            else ++it;
        }
        return results;
    }
    std::vector<CaveResidencyTicket> outstandingStreamRetires()const {
        std::vector<CaveResidencyTicket> result;
        for(const auto& [key,ticket]:pendingRetires_){
            (void)key;
            if(residency_.current(ticket))result.push_back(ticket);
        }
        return result;
    }
    // Streaming owner-barrier publication. The game host must actually
    // replace matching GPU, Box3D, SpatialWorld and nav resources BEFORE it
    // reports success. The exclusive proof must represent held exclusion.
    // The TWO meshes are intentionally separate. Visual may be coarsened
    // by camera distance; Box3D/SpatialWorld must use collisionMesh.
    using StreamBuildCommit=std::function<bool(const CaveResidencyTicket&,
                                              const CaveMesh& visualMesh,
                                              const CaveMesh& collisionMesh)>;
    using StreamRetireCommit=std::function<bool(const CaveResidencyTicket&)>;
    bool publishStreamBuild(
        CaveResidencyTicket ticket,
        CaveCertifiedChunk publishedMesh,uint64_t generatorVersion,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
        const StreamBuildCommit& actualNativeCommit) {
        const auto it=readyStreamBuilds_.find(ticket.key);
        if(closed_||physicsBarrier_.status().recoveryRequired||
           ticket.action!=CaveResidencyAction::Build ||
           !exclusive || !exclusive() || !actualNativeCommit ||
           it==readyStreamBuilds_.end() ||
           !residency_.current(ticket) ||
           it->second.ticket.generation!=ticket.generation ||
           it->second.ticket.sourceRevision!=ticket.sourceRevision ||
           !generatorVersion || !publishedMesh.renderRevision ||
           publishedMesh.mesh.key!=ticket.key ||
           publishedMesh.sourceRevision!=ticket.sourceRevision ||
           publishedMesh.resolution==0 || publishedMesh.spacingCm<=0)
            return false;
        const auto navigationBounds=navigationChunkBounds(
            ticket.key,publishedMesh.resolution,publishedMesh.spacingCm);
        try {
            if(!actualNativeCommit(ticket,it->second.mesh,
                                   it->second.collisionMesh))return false;
            if(!exclusive())throw std::logic_error("Caverns stream build lost exclusion");
            // Match the already-published GPU/physics/nav revision with
            // committed CPU source and seam-residency metadata.
            host_.registerCommittedMesh(std::move(publishedMesh),
                                        generatorVersion);
        } catch(...) {
            physicsBarrier_.requireRecovery();
            throw;
        }
        if(!residency_.acknowledge(ticket)) {
            physicsBarrier_.requireRecovery();
            throw std::logic_error("Caverns streaming build ACK refused after native commit");
        }
        readyStreamBuilds_.erase(it);
        // Streaming may alter passable topology at the SAME authored SDF
        // revision. Consumers rebuild local routes/flow seeds/cluster borders.
        (void)navigationBus_.invalidateResidency(navigationBounds);
        return true;
    }
    bool publishStreamRetire(
        CaveResidencyTicket ticket,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
        const StreamRetireCommit& actualNativeCommit) {
        const auto it=pendingRetires_.find(ticket.key);
        if(closed_||physicsBarrier_.status().recoveryRequired||
           ticket.action!=CaveResidencyAction::Retire ||
           !exclusive || !exclusive() || !actualNativeCommit ||
           it==pendingRetires_.end() || !residency_.current(ticket) ||
           it->second.generation!=ticket.generation ||
           it->second.sourceRevision!=ticket.sourceRevision)
            return false;
        // Actual host must retire previous GeometryScene source/Box3D body/
        // SpatialWorld range/NavMesh at an exclusive owner barrier. Never
        // use an empty/dummy callback as proof of full retirement.
        try {
        if(!actualNativeCommit(ticket))return false;
        if(!exclusive())throw std::logic_error("Caverns stream retire lost exclusion");
        // The native owner has removed the old source. Forget its committed
        // catalog entry and loaded-neighbor seam certification; do not
        // forget a pending edit or a shard still in GPU staging.
        if(const auto* previous=host_.geometry().catalog().find(ticket.key)) {
            if(!host_.forgetCommittedMesh(ticket.key,previous->revision)){
                physicsBarrier_.requireRecovery();
                throw std::logic_error("Caverns streaming catalog retirement refused");
            }
        }
        if(!residency_.acknowledge(ticket))
            throw std::logic_error("Caverns streaming retirement ACK refused after native commit");
        pendingRetires_.erase(it);
        (void)navigationBus_.invalidateResidency(
            navigationChunkBounds(ticket.key,chunkResolution_,chunkSpacingCm_));
        return true;
        } catch(...) {
            physicsBarrier_.requireRecovery();
            throw;
        }
    }

    bool acceptStreaming(CaveResidencyTicket ticket){
        if(physicsBarrier_.status().recoveryRequired)return false;
        const bool confirmed=residency_.acknowledge(ticket);
        if(confirmed){
            if(ticket.action==CaveResidencyAction::Retire)
                pendingRetires_.erase(ticket.key);
            else readyStreamBuilds_.erase(ticket.key);
        }
        return confirmed;
    }
    bool failStreaming(CaveResidencyTicket ticket){
        const bool failed=residency_.fail(ticket);
        if(failed){
            if(ticket.action==CaveResidencyAction::Retire)
                pendingRetires_.erase(ticket.key);
            else readyStreamBuilds_.erase(ticket.key);
        }
        return failed;
    }
    uint64_t pinStreamChunk(ChunkKey3 key,CaveLeasePurpose purpose){
        if(closed_||physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns stream pin requires active recovered world");
        if(!residency_.stats().sourceRevision)
            throw std::logic_error("Caverns stream pin requires source revision");
        return residency_.pin(key,purpose);
    }
    bool unpinStreamChunk(ChunkKey3 key,uint64_t lease){
        return residency_.unpin(key,lease);
    }
    CaveResidencyStats streamingStats()const{return residency_.stats();}
    // Native host may override canonical collision chunk dimensions.
    void setNavigationChunkDimensions(uint32_t resolution,int32_t spacingCm){
        if(!resolution||resolution>64||spacingCm<=0||spacingCm>10000)
            throw std::invalid_argument("Caverns nav chunk dimensions");
        chunkResolution_=resolution;
        chunkSpacingCm_=spacingCm;
    }
    void drawCommitted(render::Renderer3D& renderer){
        if(!physicsBarrier_.status().recoveryRequired)host_.draw(renderer);
    }
    size_t collectGpuCompletedWithoutIdle(gfx::Device& device){
        return host_.collectGpuCompletedWithoutIdle(device);
    }
    bool collectAfterGpuIdle(gfx::Device& device){
        return host_.collectAfterGpuIdle(device);
    }
    bool shutdownAfterGpuIdle(gfx::Device& device){
        streamJobs_.drain();
        if(!host_.shutdownAfterGpuIdle(device))return false;
        pendingRetires_.clear();readyStreamBuilds_.clear();editPins_.clear();
        closed_=true;
        return true;
    }
    bool closeWorldAfterGpuIdle(gfx::Device& device){
        return shutdownAfterGpuIdle(device);
    }
    bool closed()const noexcept{return closed_;}
    CaveGameplayEditStats stats()const{return host_.stats();}
    const CavePhysicsCommitStatus& physicsStatus()const noexcept{
        return physicsBarrier_.status();
    }
    CaveGameplayEditHost& host()noexcept{return host_;}
private:
    static CaveDirtyBounds navigationChunkBounds(ChunkKey3 key,
                                                  uint32_t resolution,
                                                  int32_t spacingCm) {
        const int64_t span=int64_t(resolution)*spacingCm;
        return {key.x*span,key.y*span,key.z*span,
                (key.x+1)*span,(key.y+1)*span,(key.z+1)*span};
    }
    void recordNavigationBounds(CaveDirtyBounds b) {
        if(!navDirty_){navDirtyBounds_=b;navDirty_=true;return;}
        auto& old=navDirtyBounds_;
        old.minX=std::min(old.minX,b.minX);
        old.minY=std::min(old.minY,b.minY);
        old.minZ=std::min(old.minZ,b.minZ);
        old.maxX=std::max(old.maxX,b.maxX);
        old.maxY=std::max(old.maxY,b.maxY);
        old.maxZ=std::max(old.maxZ,b.maxZ);
    }
    void syncStreamingEditPins() {
        const auto source=host_.stats().requestedRevision;
        if(!source)return;
        if(residency_.stats().sourceRevision!=source)
            residency_.sourceRevision(source);
        const auto pending=host_.pendingEditKeys();
        const std::set<ChunkKey3,KeyLess> wanted(pending.begin(),pending.end());
        for(auto it=editPins_.begin();it!=editPins_.end();) {
            if(!wanted.contains(it->first)) {
                residency_.unpin(it->first,it->second);
                it=editPins_.erase(it);
            }else ++it;
        }
        for(const auto& key:wanted)
            if(!editPins_.contains(key))
                editPins_.emplace(key,residency_.pin(key,CaveLeasePurpose::Edit));
    }
    CaveNavigationPublication navigationBus_{nullptr};
    CavePathRouter pathRouter_;
    mutable nav::CavePathQueryCache pathCache_{};
    mutable CaveNativeNavMeshCache nativeNavCache_{};
    std::unique_ptr<CaveNavigationRegionCache> regionCache_;
    CaveDirtyBounds navDirtyBounds_{};
    bool navDirty_=false;
    uint32_t chunkResolution_=16;
    int32_t chunkSpacingCm_=100;
    CaveWorldResidency residency_;
    CaveWorldStreamJobs streamJobs_{2};
    std::map<ChunkKey3,CaveResidencyTicket,KeyLess> pendingRetires_;
    std::map<ChunkKey3,CaveStreamCpuResult,KeyLess> readyStreamBuilds_;
    std::map<ChunkKey3,uint64_t,KeyLess> editPins_;
    CaveGameplayEditHost host_;
    bool closed_=false;
    CaveGameplayPhysicsBarrier physicsBarrier_;
};
} // namespace aiverse::terraweave::caverns
