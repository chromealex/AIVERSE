#include "engine/render/GiChunkedGeometry.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace aiverse::render;

static std::vector<StaticShadowGeometry::Triangle> plane(float x,float z) {
    return {
        {{x-1,-1,z},{x+1,-1,z},{x+1,1,z},0},
        {{x-1,-1,z},{x+1,1,z},{x-1,1,z},0}
    };
}
int main() {
    GiChunkedGeometryBuilder builder;
    const float3 origin{0,0,0}, forward{0,0,1};
    auto empty=builder.publish();
    AIVERSE_TEST_CHECK(empty.chunkCount()==0);
    AIVERSE_TEST_CHECK(empty.distance(origin,forward,10)==10);

    builder.replace(20,plane(0,2),{{1,0,0}});
    builder.replace(10,plane(0,4),{{0,1,0}});
    const auto first=builder.publish();
    AIVERSE_TEST_CHECK(first.chunkCount()==2 && first.triangleCount()==4);
    GiChunkedGeometrySnapshot::QueryStats stats;
    float3 n{},color{};
    AIVERSE_TEST_CHECK(std::abs(first.distance(origin,forward,10,&n,&color,&stats)-2)<1e-5f);
    AIVERSE_TEST_CHECK(color.x==1 && color.y==0);
    AIVERSE_TEST_CHECK(n.z>0);
    AIVERSE_TEST_CHECK(stats.nodes>=1 && stats.chunks>=1);
    const auto oldChunk10=first.findChunk(10),oldChunk20=first.findChunk(20);
    AIVERSE_TEST_CHECK(oldChunk10 && oldChunk20);

    // Only chunk 20 is re-BVH'd: chunk 10 remains shared across immutable snapshots.
    builder.replace(20,plane(0,6),{{0,0,1}});
    auto second=builder.publish();
    AIVERSE_TEST_CHECK(second.chunkCount()==2);
    AIVERSE_TEST_CHECK(second.findChunk(10)==oldChunk10);
    AIVERSE_TEST_CHECK(second.findChunk(20)!=oldChunk20);
    AIVERSE_TEST_CHECK(std::abs(second.distance(origin,forward,10,&n,&color)-4)<1e-5f);
    AIVERSE_TEST_CHECK(color.y==1 && color.x==0);
    AIVERSE_TEST_CHECK(std::abs(first.distance(origin,forward,10)-2)<1e-5f);

    // Many irrelevant chunks should be rejected by the top-level bounding hierarchy.
    for(uint64_t i=0;i<40;++i)builder.replace(100+i,plane(float(i+1)*20,3),{{.5f,.5f,.5f}});
    auto crowded=builder.publish();
    AIVERSE_TEST_CHECK(crowded.chunkCount()==42);
    stats={};
    AIVERSE_TEST_CHECK(std::abs(crowded.distance(origin,forward,10,&n,&color,&stats)-4)<1e-5f);
    AIVERSE_TEST_CHECK(stats.chunks<10);

    // An empty chunk update deletes only that entry; old snapshots still own it.
    AIVERSE_TEST_CHECK(builder.remove(10));
    AIVERSE_TEST_CHECK(!builder.remove(10));
    auto third=builder.publish();
    AIVERSE_TEST_CHECK(third.findChunk(10)==nullptr);
    AIVERSE_TEST_CHECK(std::abs(third.distance(origin,forward,10)-6)<1e-5f);
    AIVERSE_TEST_CHECK(second.findChunk(10)==oldChunk10);

    // Reject bad geometry before mutating the canonical builder.
    bool rejected=false;
    const size_t countBefore=builder.chunkCount();
    try {builder.replace(20,plane(0,1),{{-1,0,0}});}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(builder.chunkCount()==countBefore);
    AIVERSE_TEST_CHECK(builder.publish().findChunk(20)==second.findChunk(20));
    rejected=false;
    try {
        auto invalid=plane(0,1);
        invalid[0].a.x=INFINITY;
        builder.replace(20,std::move(invalid),{{1,1,1}});
    }catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(builder.chunkCount()==countBefore);
    return 0;
}
