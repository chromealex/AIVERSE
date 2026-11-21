#include "engine/render/GiGpuPagedBvhArena.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

static std::vector<StaticShadowGeometry::Triangle> wall(float x,float z) {
    return {{{x-1,-1,z},{x+1,-1,z},{x+1,1,z},0},
            {{x-1,-1,z},{x+1,1,z},{x-1,1,z},0}};
}
static void parity(const GiChunkedGeometrySnapshot& snapshot,
                   const GiGpuBvh& bvh, float x,float maxDistance=10) {
    const float3 origin{x,0,0},ray{0,0,1};
    float3 normal{},albedo{};
    const float expected=snapshot.distance(origin,ray,maxDistance,&normal,&albedo);
    const auto observed=bvh.trace(origin,ray,maxDistance);
    AIVERSE_TEST_CHECK(std::abs(expected-observed.distance)<.0001f);
    if(expected<maxDistance) {
        AIVERSE_TEST_CHECK(std::abs(albedo.x-observed.albedo.x)<.0001f);
        AIVERSE_TEST_CHECK(std::abs(albedo.y-observed.albedo.y)<.0001f);
    }
}
int main() {
    GiChunkedGeometryBuilder builder;
    builder.replace(1,wall(0,2),{{1,0,0}});
    builder.replace(2,wall(10,2),{{0,1,0}});
    builder.replace(3,wall(20,2),{{0,0,1}});
    auto original=builder.publish();
    GiGpuChunkBvhCache cache;
    AIVERSE_TEST_CHECK(cache.refresh(original).rebuilt==3);
    GiGpuPagedBvhArena arena;

    // A Plan is NOT committed on build alone. A RHI upload can refuse it.
    auto first=arena.plan(original,cache);
    AIVERSE_TEST_CHECK(first.accepted);
    AIVERSE_TEST_CHECK(first.stats.added==3 && first.stats.reused==0);
    AIVERSE_TEST_CHECK(first.stats.tlasNodes==5);
    AIVERSE_TEST_CHECK(first.stats.relevantTriangles==6);
    AIVERSE_TEST_CHECK(first.packet.nodes.size()>=GiGpuPagedBvhArena::kTlasCapacity+3);
    AIVERSE_TEST_CHECK(arena.pageCount()==0);
    for(float x:{0.f,10.f,20.f}) parity(original,first.packet,x);
    arena.commit(std::move(first));
    AIVERSE_TEST_CHECK(arena.pageCount()==3);
    const auto secondPage=*arena.findPage(2);
    const auto thirdPage=*arena.findPage(3);
    AIVERSE_TEST_CHECK(secondPage.nodes.first>=GiGpuPagedBvhArena::kTlasCapacity);

    // Identical source publication keeps all GPU page offsets stable. Because
    // the CPU packet is equal, GiGpuSparsePatch will upload zero bytes.
    auto warm=arena.plan(original,cache);
    AIVERSE_TEST_CHECK(warm.stats.reused==3 && warm.stats.rewritten==0);
    AIVERSE_TEST_CHECK(arena.findPage(2)->nodes.first==secondPage.nodes.first);
    AIVERSE_TEST_CHECK(warm.packet.nodes.size()>=GiGpuPagedBvhArena::kTlasCapacity);
    arena.commit(std::move(warm));

    // A changing FIRST chunk grows and relocates its page, but every later
    // unchanged BLAS keeps its exact absolute triangle/node offsets.
    auto wider=wall(0,2);
    const auto extra=wall(0,4);
    wider.insert(wider.end(),extra.begin(),extra.end());
    builder.replace(1,wider,{{1,0,0}});
    auto modified=builder.publish();
    AIVERSE_TEST_CHECK(cache.refresh(modified).rebuilt==1);
    auto changed=arena.plan(modified,cache);
    AIVERSE_TEST_CHECK(changed.stats.reused==2);
    AIVERSE_TEST_CHECK(changed.stats.rewritten==1);
    AIVERSE_TEST_CHECK(changed.stats.relocated==1);
    AIVERSE_TEST_CHECK(arena.findPage(1)->triangleCount==2);
    // Failed/rejected GPU publication MUST leave the already accepted arena
    // and its previous addresses intact.
    AIVERSE_TEST_CHECK(arena.findPage(2)->nodes.first==secondPage.nodes.first);
    AIVERSE_TEST_CHECK(changed.packet.trace({0,0,0},{0,0,1},10).distance==2);
    arena.commit(std::move(changed));
    AIVERSE_TEST_CHECK(arena.findPage(1)->triangleCount==4);
    AIVERSE_TEST_CHECK(arena.findPage(2)->nodes.first==secondPage.nodes.first);
    AIVERSE_TEST_CHECK(arena.findPage(2)->triangles.first==secondPage.triangles.first);
    AIVERSE_TEST_CHECK(arena.findPage(3)->nodes.first==thirdPage.nodes.first);
    for(float x:{0.f,10.f,20.f}) parity(modified,arena.plan(modified,cache).packet,x);

    // Remove a chunk AFTER a completed compute fence. Its old page becomes
    // free only in the NEW accepted layout, and an unrelated new chunk may
    // safely reuse that page without moving the others.
    builder.remove(2);
    auto removed=builder.publish();
    cache.refresh(removed);
    auto retiring=arena.plan(removed,cache);
    AIVERSE_TEST_CHECK(retiring.stats.removed==1);
    AIVERSE_TEST_CHECK(arena.findPage(2)!=nullptr);
    parity(removed,retiring.packet,10);
    arena.commit(std::move(retiring));
    AIVERSE_TEST_CHECK(arena.findPage(2)==nullptr);
    builder.replace(4,wall(30,2),{{.5f,.5f,.5f}});
    const auto replacement=builder.publish();
    cache.refresh(replacement);
    auto reused=arena.plan(replacement,cache);
    AIVERSE_TEST_CHECK(reused.stats.added==1);
    arena.commit(std::move(reused));
    AIVERSE_TEST_CHECK(arena.findPage(4)!=nullptr);
    AIVERSE_TEST_CHECK(arena.findPage(4)->nodes.first==secondPage.nodes.first);
    AIVERSE_TEST_CHECK(arena.findPage(3)->nodes.first==thirdPage.nodes.first);
    for(float x:{0.f,20.f,30.f}) parity(replacement,arena.plan(replacement,cache).packet,x);

    // Invalid over-budget source MUST fail without touching committed layout.
    builder.replace(99,std::vector<StaticShadowGeometry::Triangle>(8193,
                    wall(50,2).front()),{{1,1,1}});
    const auto overBudget=builder.publish();
    bool rejected=false;
    try {(void)arena.plan(overBudget,cache);}
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(arena.pageCount()==3);
    AIVERSE_TEST_CHECK(arena.findPage(3)->nodes.first==thirdPage.nodes.first);

    // Empty geometry produces an empty GPU packet, never a root with a
    // zero-node self-cycle. A future scene can still reuse the freed spans.
    arena.clear();
    AIVERSE_TEST_CHECK(arena.pageCount()==0);
}
