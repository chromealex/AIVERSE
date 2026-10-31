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
};
struct PaintLayers { std::map<int,PaintLayer> layers; int nextId=1; };
}
