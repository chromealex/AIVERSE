#include "engine/render/LumaWeaveGpuPacket.h"
#include "engine/render/LumaWeaveTextureRetirement.h"
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

    // Camera-following cluster scroll must publish a NEW CSR generation even
    // when the authored stable light registry is unchanged. Otherwise the
    // Metal compute pass could sample stale candidate lists or miss lights.
    const auto beforeCameraMove=packLumaSources(registry,clusters);
    const auto sourceRevision=registry.revision();
    const auto clusterGeneration=clusters.buildGeneration();
    AIVERSE_TEST_CHECK(beforeCameraMove.clusterGeneration==clusterGeneration);
    AIVERSE_TEST_CHECK(!beforeCameraMove.candidateIndices.empty());
    cfg.origin={100,0,0}; // the same sources no longer intersect this view
    clusters.rebuild(registry,cfg);
    const auto afterCameraMove=packLumaSources(registry,clusters);
    AIVERSE_TEST_CHECK(registry.revision()==sourceRevision);
    AIVERSE_TEST_CHECK(afterCameraMove.sourceRevision==sourceRevision);
    AIVERSE_TEST_CHECK(afterCameraMove.clusterGeneration==clusterGeneration+1);
    AIVERSE_TEST_CHECK(afterCameraMove.uniforms.origin.x==100.0f);
    AIVERSE_TEST_CHECK(afterCameraMove.candidateIndices.empty());
    AIVERSE_TEST_CHECK(afterCameraMove.cells.size()==1);
    AIVERSE_TEST_CHECK(afterCameraMove.cells[0].count==0);
    AIVERSE_TEST_CHECK(afterCameraMove.cells[0].total==0);

    // Stage26: completed GPU timeline is not a CPU frame serial. Completed
    // tokens may release a matching prior texture, but a zero/unknown token
    // must remain quarantined until the host proves FULL graphics GPU idle.
    LumaWeaveTextureRetirement retire;
    AIVERSE_TEST_CHECK(!retire.prepare(LumaWeaveTextureRetirement::kMaxPending+1));
    AIVERSE_TEST_CHECK(retire.prepare(3));
    aiverse::gfx::TextureHandle first{101},unknown{102},third{103};
    retire.retire(first,4);
    retire.retire(unknown,0);
    retire.retire(third,7);
    AIVERSE_TEST_CHECK(!first.valid() && !unknown.valid() && !third.valid());
    AIVERSE_TEST_CHECK(retire.pendingCount()==3 && retire.unfencedCount()==1);
    std::vector<uint32_t> released;
    const auto release=[&](aiverse::gfx::TextureHandle handle) {
        released.push_back(handle.id);
    };
    AIVERSE_TEST_CHECK(retire.collect(3,release)==0);
    AIVERSE_TEST_CHECK(retire.collect(4,release)==1);
    AIVERSE_TEST_CHECK(released.size()==1 && released[0]==101);
    AIVERSE_TEST_CHECK(retire.collect(7,release)==1);
    AIVERSE_TEST_CHECK(released.size()==2 && released[1]==103);
    AIVERSE_TEST_CHECK(retire.pendingCount()==1 && retire.unfencedCount()==1);
    AIVERSE_TEST_CHECK(!retire.prepare(LumaWeaveTextureRetirement::kMaxPending));
    AIVERSE_TEST_CHECK(retire.drainAfterGpuIdle(release)==1);
    AIVERSE_TEST_CHECK(retire.pendingCount()==0);
    AIVERSE_TEST_CHECK(released.size()==3 && released[2]==102);
    struct LegacyRhi {};
    struct FenceRhi {
        uint64_t gpuSubmissionFence() const { return 11; }
        uint64_t gpuCompletedFence() const { return 9; }
    };
    AIVERSE_TEST_CHECK(lumaWeaveSubmittedFence(LegacyRhi{})==0);
    AIVERSE_TEST_CHECK(lumaWeaveCompletedFence(LegacyRhi{})==0);
    AIVERSE_TEST_CHECK(lumaWeaveSubmittedFence(FenceRhi{})==11);
    AIVERSE_TEST_CHECK(lumaWeaveCompletedFence(FenceRhi{})==9);

    // Active graphics program and target ownership remains locked even
    // when an unrelated compute callback reports completion. The queue
    // needs the real graphics submission fence or a host-proven idle.
    LumaWeaveGraphicsUseGate active;
    AIVERSE_TEST_CHECK(active.canRelease(0));
    active.markUsed(0); // old RHI has no graphics submission token
    AIVERSE_TEST_CHECK(!active.canRelease(999));
    active.markUsed(12); // later fence cannot retroactively prove unknown work
    AIVERSE_TEST_CHECK(!active.canRelease(999));
    active.afterGpuIdle();
    AIVERSE_TEST_CHECK(active.canRelease(0));
    active.markUsed(12);
    AIVERSE_TEST_CHECK(!active.canRelease(11));
    AIVERSE_TEST_CHECK(active.canRelease(12));
    active.markUsed(0); // losing fence support fails closed again
    AIVERSE_TEST_CHECK(!active.canRelease(999));
    active.afterGpuIdle();
    AIVERSE_TEST_CHECK(active.canRelease(0));
}
