#include "engine/render/LumaWeaveGpuPacket.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

int main() {
    LumaWeaveRegistry registry;
    LumaLight a;
    a.key=(uint64_t(17)<<32)|42;
    a.position={2,3,4};
    a.radius=9;
    a.color={.4f,.3f,.2f};
    a.intensity=5;
    a.contributesToGi=true;
    LumaLight b;
    b.key=1;
    b.position={6,3,4};
    b.kind=LumaLightKind::Spot;
    b.direction={0,-1,0};
    b.cosOuter=.6f;
    b.cosInner=.8f;
    b.importanceBias=2;
    b.castsShadows=false;
    (void)registry.publish({a,b});

    LumaWeaveClusters clusters;
    LumaClusterConfig cfg;
    cfg.origin={0,0,0};
    cfg.cellSize={8,8,8};
    cfg.nx=cfg.ny=cfg.nz=1;
    cfg.maxFastPerCell=1;
    cfg.maxFastReferences=1;
    const auto stats=clusters.rebuild(registry,cfg);
    AIVERSE_TEST_CHECK(stats.overflows==1);
    const auto packet=packLumaSources(registry,clusters);
    AIVERSE_TEST_CHECK(packet.sourceRevision==registry.revision());
    AIVERSE_TEST_CHECK(packet.lights.size()==2 && packet.cells.size()==1);
    AIVERSE_TEST_CHECK(packet.cells[0].flags==1);
    AIVERSE_TEST_CHECK(packet.cells[0].count==1 && packet.cells[0].total==2);
    AIVERSE_TEST_CHECK(packet.candidateIndices.size()==1);
    AIVERSE_TEST_CHECK(packet.uniforms.dims[0]==1 && packet.uniforms.dims[3]==1);
    AIVERSE_TEST_CHECK(packet.uniforms.limits[0]==2 &&
                       packet.uniforms.limits[1]==1);
    AIVERSE_TEST_CHECK(sizeof(LumaGpuLight)==80);
    AIVERSE_TEST_CHECK(sizeof(LumaGpuClusterEntry)==16);
    AIVERSE_TEST_CHECK(sizeof(LumaGpuGridUniforms)==64);
    AIVERSE_TEST_CHECK(packet.totalBytes()==2*80+16+4+64);

    // Stable source IDs survive GPU packing even if the registry input was
    // unsorted. Enables later temporal reservoirs to remap keys safely.
    AIVERSE_TEST_CHECK(packet.lights[0].identity[0]==1);
    AIVERSE_TEST_CHECK(packet.lights[0].identity[2]==uint32_t(LumaLightKind::Spot));
    AIVERSE_TEST_CHECK(packet.lights[0].settings.z==0);
    AIVERSE_TEST_CHECK(packet.lights[1].identity[0]==42);
    AIVERSE_TEST_CHECK(packet.lights[1].identity[1]==17);
    AIVERSE_TEST_CHECK(packet.lights[1].identity[3]==1);
    AIVERSE_TEST_CHECK(packet.lights[1].positionRadius.w==9);
    AIVERSE_TEST_CHECK(packet.lights[1].colorIntensity.w==5);

    bool stale=false;
    a.radius=4;
    (void)registry.publish({a,b});
    try {(void)packLumaSources(registry,clusters);}
    catch(const std::logic_error&) {stale=true;}
    AIVERSE_TEST_CHECK(stale);
    clusters.rebuild(registry,cfg);
    AIVERSE_TEST_CHECK(packLumaSources(registry,clusters).sourceRevision==registry.revision());
}
