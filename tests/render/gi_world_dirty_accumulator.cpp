#include "engine/render/GiWorldDirtyAccumulator.h"
#include "tests/AlwaysCheck.h"
#include <stdexcept>

using namespace aiverse::render;

int main() {
    GiUpdateScheduler lattice;
    lattice.configure({0,0,0},1,4,1,1);
    lattice.resetClean();
    GiWorldDirtyAccumulator changes;
    GiWorldChange move;
    move.reason=GiDirtyReason::Geometry;
    move.current={{3.5f,.5f,.5f},{3.5f,.5f,.5f}};
    move.previous={{.5f,.5f,.5f},{.5f,.5f,.5f}};
    move.hasPrevious=true;
    changes.submit(move);
    AIVERSE_TEST_CHECK(changes.regionCount()==2);
    auto result=changes.flush(lattice);
    AIVERSE_TEST_CHECK(result.events==1 && result.regions==2 && result.probeMarks==2);
    AIVERSE_TEST_CHECK(lattice.pending()==2);
    const auto near=lattice.claim(1,{0,0,0});
    AIVERSE_TEST_CHECK(near.size()==1 && near[0].index==0);
    AIVERSE_TEST_CHECK(lattice.complete(near[0]));

    // Overlapping categories join into a single changed region, retaining both flags.
    lattice.resetClean();
    changes.submit({GiDirtyReason::Lighting,{{.5f,.5f,.5f},{.5f,.5f,.5f}},{},false,.25f});
    changes.submit({GiDirtyReason::Material,{{.6f,.5f,.5f},{.6f,.5f,.5f}},{},false,.25f});
    AIVERSE_TEST_CHECK(changes.regionCount()==1);
    result=changes.flush(lattice);
    AIVERSE_TEST_CHECK(result.events==2 && result.regions==1);
    const auto batch=lattice.claim(1,{0,0,0});
    AIVERSE_TEST_CHECK(batch.size()==1 && batch[0].index==0);
    const uint8_t mask=giDirtyBit(GiDirtyReason::Lighting)|giDirtyBit(GiDirtyReason::Material);
    AIVERSE_TEST_CHECK(batch[0].reasons==mask);
    AIVERSE_TEST_CHECK(lattice.complete(batch[0]));

    // Large grass/foliage edit batches must have bounded bookkeeping. Saturating
    // the accumulator turns the change batch into a full-volume invalidate.
    lattice.configure({0,0,0},1,256,1,1);
    lattice.resetClean();
    for(int i=0;i<129;++i) {
        const float x=float(i*3)+.5f;
        changes.submit({GiDirtyReason::Geometry,{{x,.5f,.5f},{x,.5f,.5f}},{},false,0});
    }
    AIVERSE_TEST_CHECK(changes.saturated());
    result=changes.flush(lattice);
    AIVERSE_TEST_CHECK(result.fullVolume && result.events==129);
    AIVERSE_TEST_CHECK(result.probeMarks==256);
    AIVERSE_TEST_CHECK(lattice.pending()==256);
    AIVERSE_TEST_CHECK(changes.regionCount()==0 && !changes.saturated());

    bool rejected=false;
    try { changes.submit({GiDirtyReason::Geometry,{{2,2,2},{1,1,1}}}); }
    catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try { changes.submit({static_cast<GiDirtyReason>(16),{{0,0,0},{0,0,0}}}); }
    catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
}
