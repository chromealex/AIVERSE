#include "engine/world/GiWorldGeometryBridge.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::world;

static Vec3 metres(int x,int y,int z) {
    return {Fixed::fromInt(x),Fixed::fromInt(y),Fixed::fromInt(z)};
}
struct FakeGpuScene {
    bool available=true,reject=false;
    uint64_t revision=0;
    uint32_t full=0,regional=0;
    GiBounds lastBounds{};
    std::shared_ptr<const GiChunkedGeometrySnapshot> snapshot;
    bool publishGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> s,uint64_t r) {
        if(reject)return false;
        snapshot=std::move(s);revision=r;++full;return true;
    }
    bool publishGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> s,
                               uint64_t r,GiBounds dirty) {
        if(reject)return false;
        snapshot=std::move(s);revision=r;lastBounds=dirty;++regional;return true;
    }
};
struct TerraPatchLike {
    Cell cell{};
    uint32_t resolution=2;
    int32_t cellSizeMetres=16;
    uint64_t sourceRevision=1;
    std::vector<Triangle> collision;
};
struct TerraChunkLike {
    struct Key {int64_t x=0,z=0;} key;
    uint32_t resolution=2;
    int32_t spacingCm=100;
    std::vector<int32_t> heightsCm;
};
struct TerraRectLike {int64_t minX=0,minZ=0,maxX=0,maxZ=0;};

int main() {
    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::stableChunkId(-2,3,GiGeometrySource::WorldCollision)!=
                       GiWorldGeometryBridge::stableChunkId(2,3,GiGeometrySource::WorldCollision));
    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::stableChunkId(-2,3,GiGeometrySource::WorldCollision)!=
                       GiWorldGeometryBridge::stableChunkId(-2,3,GiGeometrySource::TerraWeavePatch));

    GiGeometryScene scene;
    WorldAssets assets;
    assets.cellSize=16;
    const Cell cell{-2,0,3};
    ChunkCollision collision;
    collision.triangles.push_back({metres(0,0,0),metres(2,0,0),metres(0,0,2)});
    collision.boxes.push_back({metres(3,0,3),metres(4,2,4)});
    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::replaceAuthoredChunk(scene,assets,cell,collision)==13);
    FakeGpuScene gpu;
    auto result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(result.published && result.fullReset && result.chunks==1);
    AIVERSE_TEST_CHECK(gpu.full==1 && gpu.snapshot->chunkCount()==1);
    AIVERSE_TEST_CHECK(std::abs(gpu.snapshot->distance({-31,1,49},{0,-1,0},5)-1.f)<.01f);
    AIVERSE_TEST_CHECK(std::abs(gpu.snapshot->distance({-28.5f,3,51.5f},{0,-1,0},5)-1.f)<.01f);

    // The live TerraWeave TerrainPatch carries WORLD-SPACE collision, unlike
    // authored WorldAssets triangles. No origin must be added a second time.
    TerraPatchLike patch;
    patch.cell=cell;
    patch.collision={
        {metres(-32,1,48),metres(-30,1,48),metres(-30,1,50)},
        {metres(-32,1,48),metres(-30,1,50),metres(-32,1,50)}
    };
    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::replaceTerraWeavePatch(scene,patch)==2);
    gpu.reject=true;
    const auto before=scene.revision();
    result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(!result.published && scene.hasPendingChanges());
    AIVERSE_TEST_CHECK(scene.revision()==before && gpu.snapshot->chunkCount()==1);
    gpu.reject=false;
    result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(result.published && !result.fullReset);
    AIVERSE_TEST_CHECK(gpu.regional==1 && gpu.snapshot->chunkCount()==2);
    AIVERSE_TEST_CHECK(std::abs(gpu.snapshot->distance({-31,2,49},{0,-1,0},3)-1.f)<.01f);

    // A changed live terrain patch republishes a new revision and only its
    // old/new world-space dirty bounds, without touching authored collision.
    patch.sourceRevision=2;
    for(auto& tri:patch.collision) {
        tri.a.y=Fixed::fromInt(3);
        tri.b.y=Fixed::fromInt(3);
        tri.c.y=Fixed::fromInt(3);
    }
    GiWorldGeometryBridge::replaceTerraWeavePatch(scene,patch);
    result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(result.published && !result.fullReset);
    AIVERSE_TEST_CHECK(gpu.snapshot->chunkCount()==2);
    AIVERSE_TEST_CHECK(gpu.lastBounds.min.y<=1 && gpu.lastBounds.max.y>=3);
    AIVERSE_TEST_CHECK(std::abs(gpu.snapshot->distance({-31,4,49},{0,-1,0},5)-1.f)<.01f);

    // Unloaded procedural tiles use the same TerraWeave cm-grid diagonal.
    TerraChunkLike procedural;
    procedural.key={-5,2};
    procedural.resolution=2;
    procedural.heightsCm.assign(9,100);
    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::replaceTerraWeaveProcedural(scene,procedural)==8);
    result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(result.published && gpu.snapshot->chunkCount()==3);
    AIVERSE_TEST_CHECK(std::abs(gpu.snapshot->distance({-9.5f,2,4.5f},{0,-1,0},5)-1.f)<.01f);

    // A resident foliage collider is published in its own stable namespace:
    // no second tree mesh is generated and no authored terrain is overwritten.
    const std::vector<StaticShadowGeometry::Triangle> canopy={
        {{-32,9,48},{-30,9,48},{-30,9,50},0},
        {{-32,9,48},{-30,9,50},{-32,9,50},0}
    };
    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::replaceVegetationProxy(
        scene,cell.x,cell.z,canopy)==2);
    result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(result.published && gpu.snapshot->chunkCount()==4);
    const auto proxyId=GiWorldGeometryBridge::stableChunkId(
        cell.x,cell.z,GiGeometrySource::VegetationProxy);
    AIVERSE_TEST_CHECK(gpu.snapshot->findChunk(proxyId)!=nullptr);
    AIVERSE_TEST_CHECK(std::abs(gpu.snapshot->distance(
        {-31,10,49},{0,-1,0},8)-1.f)<.01f);
    // Removing/LOD-unloading the foliage proxy invalidates its previous
    // bounds, but must preserve the world and TerraWeave colliders.
    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::replaceVegetationProxy(
        scene,cell.x,cell.z,{})==0);
    result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(result.published && gpu.snapshot->chunkCount()==3);
    AIVERSE_TEST_CHECK(gpu.snapshot->findChunk(proxyId)==nullptr);
    AIVERSE_TEST_CHECK(gpu.snapshot->distance(
        {-31,10,49},{0,-1,0},8)>1.01f);

    // Earthworks / erosion dirty region adapts cm to world m, but the caller
    // still needs to replace the authoritative triangle snapshot before draw.
    GiWorldDirtyAccumulator changes;
    GiWorldGeometryBridge::submitDirtyRect(changes,TerraRectLike{-3200,4800,-2900,5100},
                                            -4.f,8.f,1.f);
    GiUpdateScheduler schedule;
    schedule.configure({-33,-5,47},1,8,14,8);
    schedule.resetClean();
    auto stats=changes.flush(schedule);
    AIVERSE_TEST_CHECK(stats.events==1 && stats.probeMarks>0);
    AIVERSE_TEST_CHECK(schedule.pending()>0);

    AIVERSE_TEST_CHECK(GiWorldGeometryBridge::remove(scene,GiGeometrySource::TerraWeavePatch,
                                                       cell.x,cell.z));
    result=scene.publish(gpu);
    AIVERSE_TEST_CHECK(result.published && gpu.snapshot->chunkCount()==2);

    bool rejected=false;
    try {GiWorldGeometryBridge::stableChunkId(int64_t(1)<<40,0,GiGeometrySource::WorldCollision);}
    catch(const std::out_of_range&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    patch.collision.pop_back();
    try {(void)GiWorldGeometryBridge::replaceTerraWeavePatch(scene,patch);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(!scene.hasPendingChanges());
}
