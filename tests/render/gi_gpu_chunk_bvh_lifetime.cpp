#include "engine/render/GiGpuChunkBvhCache.h"
#include "tests/AlwaysCheck.h"
#include <memory>
#include <vector>

using namespace aiverse::render;

int main() {
    auto builder=std::make_unique<GiChunkedGeometryBuilder>();
    builder->replace(99,{
        {{-1,-1,2},{1,-1,2},{1,1,2},0},
        {{-1,-1,2},{1,1,2},{-1,1,2},0}
    },{{1,0,0}});

    auto scene=std::make_shared<GiChunkedGeometrySnapshot>(builder->publish());
    std::weak_ptr<const StaticShadowGeometry> weakSource;
    scene->forEachChunkShared([&](uint64_t id,GiBounds,
                                  const std::shared_ptr<const StaticShadowGeometry>& geometry) {
        AIVERSE_TEST_CHECK(id==99);
        weakSource=geometry;
    });
    AIVERSE_TEST_CHECK(!weakSource.expired());

    GiGpuChunkBvhCache cache;
    const auto initial=cache.refresh(*scene);
    AIVERSE_TEST_CHECK(initial.rebuilt==1 && initial.triangles==2);
    const auto bvh=cache.chunkBvh(99);
    AIVERSE_TEST_CHECK(bvh && bvh->triangles.size()==2);
    auto shared=cache.chunkSource(99);
    AIVERSE_TEST_CHECK(shared && shared.get()==weakSource.lock().get());

    // Producer and scene snapshots are destroyed. Cached source must stay
    // alive, so raw address reuse cannot masquerade as an unchanged chunk.
    builder.reset();
    scene.reset();
    AIVERSE_TEST_CHECK(!weakSource.expired());
    AIVERSE_TEST_CHECK(cache.chunkBvh(99)==bvh);
    AIVERSE_TEST_CHECK(cache.pack().trace({0,0,0},{0,0,1},5).distance<5);

    // On cache refresh, removing the chunk releases its last owner.
    shared.reset();
    GiChunkedGeometryBuilder empty;
    const auto removed=cache.refresh(empty.publish());
    AIVERSE_TEST_CHECK(removed.removed==1 && cache.chunkCount()==0);
    AIVERSE_TEST_CHECK(cache.chunkSource(99)==nullptr);
    AIVERSE_TEST_CHECK(weakSource.expired());
}
