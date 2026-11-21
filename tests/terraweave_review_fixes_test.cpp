// Regression tests for the 2026-11-21 PHYSIS/TerraWeave review fixes:
//  - TerrainMutationCoordinator re-queues a still-resident tile whose rebuild
//    was abandoned while it was briefly not needed (stale mesh/collision);
//  - undelivered terrain dirty records survive tile unload and are coalesced;
//  - TerrainCollisionPublisher stays transactional without a SpatialWorld copy;
//  - Horizon wind overlay advances its revision only after checked writes;
//  - forest canopy index never throws on an overcrowded bucket;
//  - forest ecology cursor / first-seen tick are stable across recruitment.
#include "engine/world/TerraWeaveTerrainRuntime.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/render/TerraWeaveHorizonWindPatch.h"
#include "tests/AlwaysCheck.h"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::terraweave;

namespace {
#define CHECK(x) AIVERSE_TEST_CHECK(x)

struct Device final : gfx::Device {
    gfx::DeviceInfo inf{};
    std::string err;
    uint32_t serial=1;
    bool refuseShared=false;
    size_t sharedWrites=0;
    const gfx::DeviceInfo& info() const override{return inf;}
    const std::string& lastError() const override{return err;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {serial++};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    bool updateSharedBufferChecked(gfx::BufferHandle h,const void* d,
                                   size_t n,size_t o) override {
        if(refuseShared)return false;
        ++sharedWrites;
        updateBuffer(h,d,n,o);
        return true;
    }
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override{return {serial++};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle) override{}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {serial++};}
    void destroyShader(gfx::ShaderHandle) override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {serial++};}
    void destroyPipeline(gfx::PipelineHandle) override{}
    void beginFrame(int,int) override{}
    void endFrame() override{}
    void beginPass(const gfx::PassDesc&) override{}
    void endPass() override{}
    void setViewport(const gfx::Rect&) override{}
    void setScissor(const gfx::Rect&) override{}
    void setPipeline(gfx::PipelineHandle) override{}
    void setVertexBuffer(gfx::BufferHandle,size_t) override{}
    void setIndexBuffer(gfx::BufferHandle,size_t) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t) override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override{}
};

constexpr int32_t kTileMetres=16;
uint32_t ids(world::Cell c){return 1000u+uint32_t((c.x+8)*64+(c.z+8))*16u;}

uint64_t dirtyRevision(const std::vector<TerrainPatchDirtyRegion>& all,
                       world::Cell cell) {
    uint64_t best=0;
    for(const auto& d:all)if(d.cell==cell)best=std::max(best,d.sourceRevision);
    return best;
}

void residentTileRequeuedAfterBriefAbsence() {
    EarthworksLayer earth(100,[](int64_t,int64_t){return int32_t(0);});
    TerrainMutationCoordinator terrain(earth,kTileMetres,3);
    Device device;
    world::SpatialWorld spatial;
    const world::Cell a{0,0,0},b{3,0,0},c{5,0,0};
    std::vector<world::Cell> visible{a};
    CHECK(terrain.publishVisible(device,spatial,visible,1,ids)==1);
    (void)terrain.takeTerrainDirty();
    // Dig in the middle of tile A: A becomes pending.
    EarthworkBrush brush;
    brush.centerXcm=800;brush.centerZcm=800;
    brush.radiusCm=300;brush.depthCm=150;
    const auto change=terrain.excavate(brush,10);
    CHECK(!change.earth.vertices.empty());
    CHECK(terrain.pendingTiles()==1);
    const uint64_t dug=earth.revision();
    // Camera jumps: A unneeded but stays resident (B,C not all ready).
    visible={b,c};
    CHECK(terrain.publishVisible(device,spatial,visible,1,ids)==1);
    CHECK(terrain.residentTiles()==2); // A pinned + B
    // Camera returns to A: the abandoned rebuild must be re-queued.
    visible={a};
    CHECK(terrain.pendingTiles()==1); // C only (A parked, not lost)
    const size_t published=terrain.publishVisible(device,spatial,visible,1,ids);
    CHECK(published==1);
    CHECK(terrain.pendingTiles()==0);
    const auto dirty=terrain.takeTerrainDirty();
    CHECK(dirtyRevision(dirty,a)==dug);
    // B was unloaded in the same call; its undelivered publish survives.
    CHECK(dirtyRevision(dirty,b)!=0);
    CHECK(terrain.takeTerrainDirty().empty());
    terrain.shutdown(device,spatial);
    CHECK(spatial.colliders().empty());
}

void dirtyRecordsCoalescedPerTile() {
    EarthworksLayer earth(100,[](int64_t,int64_t){return int32_t(0);});
    TerrainMutationCoordinator terrain(earth,kTileMetres,3);
    Device device;
    world::SpatialWorld spatial;
    const world::Cell a{0,0,0};
    std::vector<world::Cell> visible{a};
    CHECK(terrain.publishVisible(device,spatial,visible,1,ids)==1);
    for(int i=0;i<70;++i){
        EarthworkBrush brush;
        brush.centerXcm=800;brush.centerZcm=800;
        brush.radiusCm=200;brush.depthCm=1+i%3;
        brush.mode=i%2?EarthworkMode::Dig:EarthworkMode::Fill;
        (void)terrain.excavate(brush,uint64_t(20+i));
        CHECK(terrain.publishVisible(device,spatial,visible,1,ids)==1);
    }
    const auto pending=terrain.pendingTerrainNotifications();
    CHECK(pending.size()==1);
    CHECK(pending.front().sourceRevision==earth.revision());
    // Coordinator shutdown keeps the undelivered record for listeners.
    terrain.shutdown(device,spatial);
    const auto taken=terrain.takeTerrainDirty();
    CHECK(taken.size()==1 && taken.front().sourceRevision==earth.revision());
}

void collisionPublishStaysTransactional() {
    EarthworksLayer earth(100,[](int64_t,int64_t){return int32_t(0);});
    TerrainCollisionPublisher publisher;
    world::SpatialWorld spatial;
    CHECK(spatial.add({7,{{Fixed::fromInt(-1),{},Fixed::fromInt(-1)},
                          {Fixed::fromInt(1),Fixed::fromInt(1),Fixed::fromInt(1)}}}));
    TerrainPatch patch=bakeEarthworksTerrainPatch(earth,{0,0,0},kTileMetres,3);
    publisher.publish(spatial,patch,100);
    const auto before=spatial.colliders();
    CHECK(before.size()==1+patch.collision.size());
    TerrainPatch broken=patch;
    broken.sourceRevision=99;
    broken.collision[3].b=broken.collision[3].a;
    broken.collision[3].c=broken.collision[3].a; // degenerate
    bool threw=false;
    try{publisher.publish(spatial,broken,200);}
    catch(const std::invalid_argument&){threw=true;}
    CHECK(threw);
    CHECK(spatial.colliders().size()==before.size());
    for(const auto& [id,collider]:before)
        CHECK(spatial.colliders().contains(id) &&
              spatial.colliders().at(id).bounds.min==collider.bounds.min &&
              spatial.colliders().at(id).bounds.max==collider.bounds.max);
    CHECK(publisher.ranges().at({0,0,0}).firstId==100);
    // A valid move to another ID range replaces the old triangles.
    publisher.publish(spatial,patch,300);
    CHECK(!spatial.colliders().contains(100) && spatial.colliders().contains(300));
    CHECK(spatial.colliders().contains(7));
    CHECK(spatial.colliders().size()==before.size());
    publisher.unload(spatial,{0,0,0});
    CHECK(spatial.colliders().size()==1 && publisher.ranges().empty());
}

void windOverlayNeedsCheckedWrites() {
    Device device;
    render::Model model;
    model.vb={41};model.ib={42};
    model.batching.allowGeometryPool=false;
    render::MeshDef mesh;
    render::Primitive forest;
    forest.material=1;forest.firstVertex=0;forest.vertexCount=16;
    mesh.primitives.push_back(forest);
    model.meshes.push_back(mesh);
    HorizonForestWindOverlay overlay;
    overlay.vertices.resize(16);
    overlay.centersCm={{100,200},{-5000,7000}};
    const ForestWindState fallback{};
    device.refuseShared=true;
    auto refused=applyHorizonForestWindOverlay(
        device,model,overlay,nullptr,fallback,5,1u<<20);
    CHECK(!refused.ready && !refused.uploaded);
    CHECK(overlay.appliedRevision==0);
    device.refuseShared=false;
    auto ok=applyHorizonForestWindOverlay(
        device,model,overlay,nullptr,fallback,5,1u<<20);
    CHECK(ok.ready && ok.uploaded && device.sharedWrites>0);
    CHECK(overlay.appliedRevision==5);
}

AnchoredPlant tree(uint64_t key,int64_t x,int64_t z) {
    AnchoredPlant p;
    p.anchor.plantKey=key;p.anchor.speciesId=1;
    p.anchor.xCm=x;p.anchor.zCm=z;
    p.anchor.roots=defaultRootProfile(PlantHabit::Tree);
    return p;
}

void canopyBucketOvercrowdingDoesNotThrow() {
    std::vector<AnchoredPlant> plants;
    for(uint64_t i=0;i<2100;++i)
        plants.push_back(tree(i+1,1000+int64_t(i%40),1000+int64_t(i/40)));
    std::map<uint32_t,ForestEcologySpecies> traits;
    ForestEcologySpecies s;s.speciesId=1;
    traits.emplace(1,s);
    const ForestEcologyCanopyIndex index(plants,traits,{});
    CHECK(index.saturated(1000,1000));
    CHECK(index.occupied(1600,1600));
    CHECK(index.lightAt(plants.front(),1000)<1000);
}

void ecologyFirstSeenAndCursor() {
    ForestEcologySpecies s;
    s.speciesId=1;
    s.matureTicks=10;s.lifespanTicks=100000000;
    s.seedIntervalTicks=1;
    s.shadeDeathTicks=s.floodDeathTicks=s.droughtDeathTicks=100000000;
    s.shadeTolerancePermille=0;
    s.seedSpreadCm=5000;
    ForestEcologySettings cfg;
    cfg.intervalTicks=1;cfg.maxPlantsPerStep=2;cfg.maxRecruitsPerStep=4;
    const auto soil=[](int64_t,int64_t)->std::optional<int32_t>{return 0;};
    ForestEcologyResource forest;
    std::vector<AnchoredPlant> rooted{
        tree(10,0,0),tree(20,100000,0),tree(30,200000,0),tree(40,300000,0)};
    for(auto& p:rooted)p.environment.matureTicks=10;
    (void)advanceForestEcology(forest,rooted,1000,{s},soil,{},cfg); // init
    (void)advanceForestEcology(forest,rooted,1001,{s},soil,{},cfg);
    // Every plant got a history at the first real step.
    CHECK(forest.plants.size()>=rooted.size());
    // A plant rooted later must not age from initialTick.
    AnchoredPlant late=tree(25,400000,0);
    late.environment.matureTicks=10;
    rooted.push_back(late);
    std::sort(rooted.begin(),rooted.end(),[](const auto& a,const auto& b){
        return a.anchor.plantKey<b.anchor.plantKey;});
    for(uint64_t t=1002;t<1010;++t)
        (void)advanceForestEcology(forest,rooted,t,{s},soil,{},cfg);
    const auto found=std::find_if(rooted.begin(),rooted.end(),
        [](const AnchoredPlant& p){return p.anchor.plantKey==25;});
    CHECK(found!=rooted.end());
    CHECK(found->environment.ageTicks<=10);
    // Cursor always points into the current population.
    CHECK(forest.cursor<rooted.size());
}
} // namespace

int main() {
    try {
        residentTileRequeuedAfterBriefAbsence();
        dirtyRecordsCoalescedPerTile();
        collisionPublishStaysTransactional();
        windOverlayNeedsCheckedWrites();
        canopyBucketOvercrowdingDoesNotThrow();
        ecologyFirstSeenAndCursor();
    } catch(const std::exception& e) {
        std::printf("TerraWeave review fixes: FAIL %s\n",e.what());
        return 1;
    }
    std::printf("TerraWeave review fixes: PASS\n");
    return 0;
}
