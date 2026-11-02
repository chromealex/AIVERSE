#pragma once
#include "RenderTexture.h"
#include <map>
#include <memory>
namespace rts::render {
// CPU command description; scripts own this, the renderer owns all GPU objects.
struct PaintLayer {
    int width=512,height=512;
    float x=0,z=0,w=1,h=1,lift=0.08f,opacity=0.45f,border=0.06f;
    std::array<float,4> color{1,1,1,1};
    int territoryOwner=0; // optional RTS source adapter; zero means manual brush canvas
    bool landOnly=true,clear=true,surface=true;
    int textureId=0,copyFrom=0;
    std::vector<RenderTexture::Stroke> strokes;
    // Categorical cell raster (view.paint_layer{cells=...}): one id byte per cell (row-major, z rows), drawn as
    // smooth categorical regions with borders between different ids (a 5x5 smoothstep coverage filter; 2x2 on low
    // material quality). palette = RGBA8 per id; id `outside` is also used beyond the raster edge.
    bool categorical=false,cellsDirty=false,paletteDirty=false;
    int cellsW=0,cellsH=0,density=1;   // density: texels per cell side (smoothing and border widths are in texels)
    std::vector<uint8_t> cells;
    std::array<uint8_t,256*4> palette{};
    std::array<float,4> borderColor{1,1,1,1};
    float borderWidth=0.55f,smooth=2.0f;
    int outside=255;
    int transparent=-1;   // id drawn fully transparent (overlay layers over another raster), -1: none
    // Ground surface (paint_layer{ground=...}, categorical only): a port of OpenWar's ArmyMapWorld meadow and coast.
    // Ids in [teamMin, teamMax] are dyed meadow (relief of the id colour), [contextMin, contextMax] faint noise, other
    // land ids neutral meadow; `seaId` is water: a static signed shoreline distance (texels, from the cells) drives
    // a beach rim on land and shallows, shadow, a near-shore cliff and animated foam on water.
    bool ground=false,coast=false,groundLinear=false;   // groundLinear: the surface maths runs on linear colour (sRGB in/out)
    float groundScale=8,groundStrength=0.12f,flipHeight=0;   // flipHeight > 0: noise y = flipHeight - z (north-up maps)
    int groundRelief=0;   // 1 / 2: OpenWar biome surface (snow / dune ripples of frequency 8 / 12) instead of the meadow, ids outside the context range
    int teamMin=1,teamMax=8,contextMin=200,contextMax=254,seaId=255;
    float coastDrop=-1;                                      // texture-row direction of the shore "side" (-1: towards -z)
    std::array<float,3> beach{.98f,.94f,.73f},shallow{.28f,.76f,.78f},earthNear{.79f,.72f,.51f},earthFar{.43f,.40f,.29f},foam{.9f,.99f,.97f};
    bool coastDirty=true;
};
struct PaintLayers { std::map<int,PaintLayer> layers; int nextId=1; };
}
