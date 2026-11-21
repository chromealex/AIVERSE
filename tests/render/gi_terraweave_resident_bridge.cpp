// GiTerraWeaveResidentBridge: only a committed resident terrain patch reaches
// GI; a refused publication keeps it staged (and the renderer unbound) until
// a later retry succeeds; unload removes the chunk.
#include "engine/world/GiTerraWeaveResidentBridge.h"
#include "tests/AlwaysCheck.h"
#include <memory>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::world;

static Vec3 metres(int x,int y,int z) {return {Fixed::fromInt(x),Fixed::fromInt(y),Fixed::fromInt(z)};}
struct Dirty {Cell cell{};uint64_t sourceRevision=0;};
struct ResidentLike {
    bool loaded=true;Cell at{};uint64_t rev=1;std::vector<Dirty> dirty;
    bool resident() const {return loaded;}
    Cell cell() const {return at;}
    uint64_t revision() const {return rev;}
    const std::vector<Dirty>& pendingDirty() const {return dirty;}
};
struct PatchLike {
    Cell cell{};uint32_t resolution=2;int32_t cellSizeMetres=16;uint64_t sourceRevision=1;
    std::vector<Triangle> collision;
};
struct Target {
    bool reject=false;uint32_t calls=0;
    std::shared_ptr<const GiChunkedGeometrySnapshot> snapshot;
    bool publishGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> s,uint64_t) {
        ++calls;if(reject)return false;snapshot=std::move(s);return true;
    }
    bool publishGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> s,uint64_t r,GiBounds) {
        return publishGeometry(std::move(s),r);
    }
};
struct RendererLike {int cleared=0;void clearDynamicGiOverlay(){++cleared;}};

int main() {
    GiGeometryScene scene;
    GiTerraWeaveResidentBridge bridge;
    Target gpu;
    const Cell cell{1,0,2};
    ResidentLike resident;resident.at=cell;
    PatchLike patch;patch.cell=cell;
    patch.collision={{metres(16,1,32),metres(18,1,32),metres(18,1,34)},
                     {metres(16,1,32),metres(18,1,34),metres(16,1,34)}};
    // No dirty notification for this revision: nothing is published.
    auto r=bridge.syncPublishedPatch(resident,patch,scene,gpu);
    AIVERSE_TEST_CHECK(!r.staged && !r.published && gpu.calls==0);
    // A worker patch for another revision is never published.
    resident.dirty={{cell,1}};
    resident.rev=2;
    r=bridge.syncPublishedPatch(resident,patch,scene,gpu);
    AIVERSE_TEST_CHECK(!r.staged && gpu.calls==0);
    resident.rev=1;
    gpu.reject=true;
    r=bridge.syncPublishedPatch(resident,patch,scene,gpu);
    AIVERSE_TEST_CHECK(r.hasNotification && r.staged && !r.published && r.blocked);
    AIVERSE_TEST_CHECK(!bridge.safeToBind());
    RendererLike renderer;
    bridge.guardRenderer(renderer);
    AIVERSE_TEST_CHECK(renderer.cleared==1);
    // Retry after the notification was consumed elsewhere still publishes.
    resident.dirty.clear();
    gpu.reject=false;
    r=bridge.syncPublishedPatch(resident,patch,scene,gpu);
    AIVERSE_TEST_CHECK(r.published && bridge.safeToBind() && bridge.committedCount()==1);
    AIVERSE_TEST_CHECK(gpu.snapshot && gpu.snapshot->chunkCount()==1);
    r=bridge.syncPublishedPatch(resident,patch,scene,gpu);
    AIVERSE_TEST_CHECK(r.alreadyCurrent);
    r=bridge.unloadPatch(cell.x,cell.z,scene,gpu);
    AIVERSE_TEST_CHECK(r.published && bridge.committedCount()==0);
    AIVERSE_TEST_CHECK(gpu.snapshot->chunkCount()==0);
    return 0;
}
