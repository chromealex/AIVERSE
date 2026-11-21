#include "engine/render/LumaWeaveClusters.h"
#include "tests/AlwaysCheck.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

static bool has(const LumaClusterCandidates& c,const LumaWeaveRegistry& r,uint64_t key) {
    for(auto index:c.fastIndices)
        if(r.lights().at(index).key==key)return true;
    return false;
}
int main() {
    LumaWeaveRegistry lights;
    LumaLight a;
    a.key=50;a.position={-3,0,-3};a.radius=2;
    LumaLight b;
    b.key=10;b.position={10,0,10};b.radius=2;
    (void)lights.publish({a,b});

    LumaWeaveClusters clusters;
    LumaClusterConfig config;
    config.origin={-8,-4,-8};
    config.cellSize={4,4,4};
    config.nx=6;config.ny=2;config.nz=6;
    config.maxFastPerCell=8;
    auto stats=clusters.rebuild(lights,config);
    AIVERSE_TEST_CHECK(stats.clusters==72);
    AIVERSE_TEST_CHECK(stats.sources==2 && stats.overflows==0);
    AIVERSE_TEST_CHECK(stats.occupied>=2);
    auto nearA=clusters.query({-3,0,-3},lights);
    AIVERSE_TEST_CHECK(!nearA.requiresFullScan);
    AIVERSE_TEST_CHECK(has(nearA,lights,50));
    AIVERSE_TEST_CHECK(!has(nearA,lights,10));
    auto nearB=clusters.query({10,0,10},lights);
    AIVERSE_TEST_CHECK(has(nearB,lights,10) && !has(nearB,lights,50));
    auto outside=clusters.query({1000,0,0},lights);
    AIVERSE_TEST_CHECK(outside.requiresFullScan); // outside grid is NOT black
    AIVERSE_TEST_CHECK(outside.fastIndices.empty());

    // Every touching light must survive small fast-list budget overflow via
    // requiresFullScan, never a silently dropped cluster contribution.
    std::vector<LumaLight> crowd;
    for(uint64_t i=1;i<=32;++i) {
        LumaLight l;
        l.key=i;
        l.position={.25f,.25f,.25f};
        l.radius=2;
        crowd.push_back(l);
    }
    (void)lights.publish(crowd);
    config.origin={0,0,0};
    config.nx=1;config.ny=1;config.nz=1;
    config.maxFastPerCell=2;
    config.maxFastReferences=2;
    stats=clusters.rebuild(lights,config);
    AIVERSE_TEST_CHECK(stats.sources==32 && stats.overflows==1);
    AIVERSE_TEST_CHECK(stats.references==2);
    auto dense=clusters.query({.25f,.25f,.25f},lights);
    AIVERSE_TEST_CHECK(dense.requiresFullScan && dense.potentialCount==32);
    AIVERSE_TEST_CHECK(dense.fastIndices.size()==2);
    AIVERSE_TEST_CHECK(lights.lights()[dense.fastIndices[0]].key==1);
    AIVERSE_TEST_CHECK(lights.lights()[dense.fastIndices[1]].key==2);

    // A stale index cannot be queried against changed light inputs.
    (void)lights.publish({crowd[0]});
    bool rejected=false;
    try {(void)clusters.query({.2f,.2f,.2f},lights);}
    catch(const std::logic_error&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    const auto lastRevision=clusters.sourceRevision();
    config.cellSize.x=0;
    rejected=false;
    try {(void)clusters.rebuild(lights,config);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    AIVERSE_TEST_CHECK(clusters.sourceRevision()==lastRevision);
    config.cellSize.x=4;
    stats=clusters.rebuild(lights,config);
    AIVERSE_TEST_CHECK(stats.sources==1 && stats.overflows==0);

    rejected=false;
    config.maxFastReferences=1u<<25;
    try {(void)clusters.rebuild(lights,config);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
}
