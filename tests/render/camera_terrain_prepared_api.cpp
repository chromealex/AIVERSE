// LENS camera-independent terrain/fog replay API contract.
// Compile-only type checks; no graphics device, animations or game world.
#include "modules/rts/view/RtsTerrainRender.h"
#include "modules/rts/view/RtsView.h"
#include <type_traits>
using namespace aiverse;
using namespace aiverse::game;
using namespace aiverse::render;
static_assert(std::is_same_v<
    decltype(&TerrainRenderer::drawPrepared),
    void(TerrainRenderer::*)(Renderer3D&,const MapGrid&,const std::vector<uint8_t>*)>);
static_assert(std::is_same_v<
    decltype(&RtsView::drawPreparedTerrainFog),
    void(RtsView::*)(Renderer3D&,const MapGrid&,const std::vector<uint8_t>*,double,uint64_t)>);
int main(){return 0;}
