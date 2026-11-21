#include "engine/render/LumaWeaveDirectReference.h"
#include "tests/AlwaysCheck.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;
static bool near(float a,float b,float eps=.0005f){return std::abs(a-b)<eps;}

int main() {
    LumaWeaveRegistry registry;
    std::vector<LumaLight> many;
    for(uint64_t i=1;i<=32;++i) {
        LumaLight l;
        l.key=i;
        l.position={0,0,2};
        l.color={1,0.5f,.25f};
        l.radius=8;
        l.intensity=6;
        l.importanceBias=1;
        l.castsShadows=false;
        many.push_back(l);
    }
    (void)registry.publish(many);
    LumaWeaveClusters clusters;
    LumaClusterConfig config;
    config.origin={-4,-4,-4};
    config.cellSize={8,8,8};
    config.nx=config.ny=config.nz=1;
    config.maxFastPerCell=2;
    config.maxFastReferences=2;
    const auto built=clusters.rebuild(registry,config);
    AIVERSE_TEST_CHECK(built.overflows==1);
    LumaDirectQuery q;
    q.position={0,0,0};
    q.normal={0,0,1};
    q.randomSeed=1337;
    const auto exact=LumaWeaveDirectReference::exact(registry,q,nullptr,&clusters);
    AIVERSE_TEST_CHECK(exact.stats.fullScan && exact.stats.candidates==32);
    const auto cheap=LumaWeaveDirectReference::estimate(registry,q,1,nullptr,&clusters);
    AIVERSE_TEST_CHECK(cheap.stats.fullScan && cheap.stats.sourcesVisited==32);
    AIVERSE_TEST_CHECK(cheap.stats.samples==1 && cheap.stats.shadowRays==0);
    AIVERSE_TEST_CHECK(near(exact.direct.x,cheap.direct.x));
    AIVERSE_TEST_CHECK(near(exact.direct.y,cheap.direct.y));
    AIVERSE_TEST_CHECK(near(exact.direct.z,cheap.direct.z));

    // Sampling with non-uniform IMPORTANCE changes variance only, never mean
    // energy. Check convergence across deterministic seeds (not a GPU test).
    many[0].importanceBias=12;
    (void)registry.publish(many);
    clusters.rebuild(registry,config);
    const auto exhaustive=LumaWeaveDirectReference::exact(registry,q,nullptr,&clusters);
    float3 sum{};
    for(uint64_t seed=0;seed<8192;++seed) {
        q.randomSeed=seed;
        sum+=LumaWeaveDirectReference::estimate(registry,q,1,nullptr,&clusters).direct;
    }
    sum=sum*(1.f/8192.f);
    AIVERSE_TEST_CHECK(std::abs(sum.x-exhaustive.direct.x)<exhaustive.direct.x*.04f);

    // Spot light cone must be honored rather than treated as an omni source.
    LumaLight spot;
    spot.key=300;
    spot.kind=LumaLightKind::Spot;
    spot.position={0,0,5};
    spot.direction={0,0,-1};
    spot.radius=10;
    spot.intensity=3;
    spot.cosInner=.92f;
    spot.cosOuter=.65f;
    spot.castsShadows=false;
    (void)registry.publish({spot});
    clusters.rebuild(registry,config);
    q.randomSeed=123;
    const auto bright=LumaWeaveDirectReference::exact(registry,q,nullptr,&clusters);
    AIVERSE_TEST_CHECK(bright.direct.x>0);
    spot.direction={0,0,1};
    (void)registry.publish({spot});
    clusters.rebuild(registry,config);
    const auto off=LumaWeaveDirectReference::exact(registry,q,nullptr,&clusters);
    AIVERSE_TEST_CHECK(off.direct.x==0);

    // Reuse the Hybrid Dynamic GI BVH for shadows. Wall at x=0 blocks the
    // right-side lamp, while the nearer left-side lamp is unobstructed.
    LumaLight left;
    left.key=5;
    left.position={-1,0,0};
    left.color={0,1,0};
    left.radius=10;
    left.intensity=5;
    LumaLight right;
    right.key=6;
    right.position={2,0,0};
    right.color={1,0,0};
    right.radius=10;
    right.intensity=5;
    (void)registry.publish({left,right});
    const GiGpuBvh blockers=GiGpuBvh::fromPackedTriangles({
        {{0,-3,-3,0},{0,3,-3,0},{0,3,3,0},{.5f,.5f,.5f,0}},
        {{0,-3,-3,0},{0,3,3,0},{0,-3,3,0},{.5f,.5f,.5f,0}}
    });
    LumaDirectQuery wallQuery;
    wallQuery.position={-2,0,0};
    wallQuery.normal={1,0,0};
    const auto unshadowed=LumaWeaveDirectReference::exact(registry,wallQuery);
    const auto shadowed=LumaWeaveDirectReference::exact(registry,wallQuery,&blockers);
    AIVERSE_TEST_CHECK(unshadowed.direct.x>0 && unshadowed.direct.y>0);
    AIVERSE_TEST_CHECK(shadowed.stats.shadowRays==2);
    AIVERSE_TEST_CHECK(near(shadowed.direct.x,0));
    AIVERSE_TEST_CHECK(shadowed.direct.y>0);
    const auto bounded=LumaWeaveDirectReference::estimate(registry,wallQuery,8,&blockers);
    AIVERSE_TEST_CHECK(bounded.stats.shadowRays<=8 && bounded.stats.samples==8);

    bool rejected=false;
    try {(void)LumaWeaveDirectReference::estimate(registry,q,0);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try {(void)LumaWeaveDirectReference::estimate(registry,q,65);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}
