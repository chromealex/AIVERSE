// TerraWeave editor/open-world sparse creation, adaptive visual LOD and
// flat-ground single-quad regression. No GPU, UI, RTS or FPS required.
#include "engine/world/TerraWeaveSparseTerrainAuthoring.h"
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aiverse;
namespace tw=aiverse::terraweave;
static void check(bool ok,const char* msg){
    if(!ok)throw std::runtime_error(msg);
}
static tw::SparseTerrainView get(
        const std::vector<tw::SparseTerrainView>& views,world::Cell cell) {
    const auto it=std::find_if(views.begin(),views.end(),
        [cell](const auto& x){return x.cell==cell;});
    if(it==views.end())throw std::runtime_error("chunk missing in view");
    return *it;
}
int main(){
    tw::EarthworksLayer ground(100,[](int64_t,int64_t){return 0;});
    tw::SparseTerrainAuthoring paint(ground,64);
    check(paint.ensure(-1,-1),"first world click did not create chunk");
    check(!paint.ensure(-2,-2),"duplicate click created a second chunk");
    check(paint.contains({-1,0,-1})&&paint.chunkCount()==1,
          "negative-world chunk indexing broken");
    check(paint.ensure(100,100),"second distant click did not create chunk");
    check(paint.chunkCount()==2,"independent sparse chunks not kept");
    const auto flatNear=paint.view(0,0);
    const auto flatFar=paint.view(9000000,9000000);
    for(const auto& v:flatNear)
        check(v.flat && v.resolution==2,
              "flat ground near camera is not one quad");
    for(const auto& v:flatFar)
        check(v.flat && v.resolution==2,
              "flat ground far camera changed its single quad");
    const auto flatView=get(flatNear,{0,0,0});
    const auto flatPatch=tw::bakeSparseTerrainVisual(ground,flatView,64);
    check(flatPatch.presentationOnly && flatPatch.resolution==2 &&
          flatPatch.triangleCount()==2,
          "flat patch is not exactly one quad (two triangles)");
    const auto flatModel=tw::makeEarthworksTerrainModel(flatPatch);
    check(flatModel && flatModel->vertices.size()==4 &&
          flatModel->indices.size()==6,
          "flat tile GPU model has more than four vertices or two triangles");

    // A stroke crossing the +X boundary creates neighbor chunks atomically,
    // while both sample ONE global Earthworks height ledger.
    tw::EarthworkBrush brush;
    brush.centerXcm=6400;brush.centerZcm=3200;
    brush.radiusCm=2500;brush.depthCm=130;
    brush.mode=tw::EarthworkMode::Fill;
    const auto change=paint.paint(brush);
    check(!change.vertices.empty() && ground.revision()==1 &&
          paint.contains({1,0,0}) && paint.contains({0,0,0}),
          "cross-chunk sculpt failed to create/modify both neighbors");
    const auto curved=paint.view(100,3200);
    const auto near=get(curved,{0,0,0});
    check(!near.flat && near.resolution>2,
          "sculpted ground incorrectly collapsed into a flat quad");
    const auto far=get(paint.view(9000000,9000000),{0,0,0});
    check(!far.flat && far.resolution<near.resolution,
          "nonflat terrain did not simplify with camera distance");

    // A high-resolution border must exactly interpolate the touching
    // coarse border, without T-junction cracks. This is VISUAL ONLY.
    tw::SparseTerrainView high;
    high.cell={0,0,0}; high.resolution=33;
    high.neighborResolution[1]=17;
    tw::SparseTerrainView low;
    low.cell={1,0,0};low.resolution=17;
    low.neighborResolution[0]=33;
    const auto a=tw::bakeSparseTerrainVisual(ground,high,64);
    const auto b=tw::bakeSparseTerrainVisual(ground,low,64);
    for(uint32_t i=0;i<33;++i){
        const int32_t y=a.heightsCm[size_t(i)*33+32];
        const int32_t ca=b.heightsCm[size_t(i/2)*17];
        if(i%2==0)check(y==ca,"LOD border at shared coarse vertex differs");
        else {
            const int32_t cb=b.heightsCm[size_t(i/2+1)*17];
            const int64_t sum=int64_t(ca)+cb;
            const int32_t expected=sum<0?
                -int32_t((-sum+1)/2):int32_t((sum+1)/2);
            check(y==expected,"LOD high edge does not stitch to coarse edge");
        }
    }
    check(a.presentationOnly && b.presentationOnly,
          "adaptive mesh accidentally owns authoritative collision");

    // Keep saved occupancy and authoritative soil snapshots independent:
    // reloading an editor should NOT regenerate border-height copies.
    const auto cells=paint.chunks();
    tw::SparseTerrainAuthoring reloaded(ground,64);
    reloaded.restore(cells);
    check(reloaded.chunks()==cells &&
          tw::bakeSparseTerrainVisual(ground,
                get(reloaded.view(100,3200),{0,0,0}),64).resolution>2,
          "authoring restore lost chunks or sculpted terrain");
    bool invalid=false;
    try{reloaded.restore(std::vector<world::Cell>{{0,0,0},{0,0,0}});}
    catch(const std::invalid_argument&){invalid=true;}
    check(invalid && reloaded.chunks()==cells,
          "duplicate occupancy restore was not atomic");
    const auto before=ground.snapshot();
    const auto priorCells=paint.chunks();
    brush.depthCm=0;
    invalid=false;
    try{(void)paint.paint(brush);}
    catch(const std::invalid_argument&){invalid=true;}
    check(invalid && ground.snapshot().heights==before.heights &&
          paint.chunks()==priorCells,"failed brush partially edited terrain");

    std::cout<<"PASS TerraWeave sparse click/auto-neighbors, dynamic distance "
                "LOD, flat=1 indexed quad (4 verts/2 tris), stitched edges, "
                "rollback-safe authoring\n";
}
